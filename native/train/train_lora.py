"""Дообучение модели с картинками читать чертежи: QLoRA на одной видеокарте (8 ГБ хватает).

  python train_lora.py --preset qwen3.5-4b --base base/Qwen3.5-4B --dump data/dump \\
      --labels data/labels.json --synth data/synth/synth.jsonl --out runs/r1

Учит только слои языковой части (models.json: lora_targets), картинки режет так же, как
llama.cpp (min/max токенов картинки), вопросы — из выгрузки CursorPad. Потеря — только на
ответе. Листы с eval: true в обучение не идут: на них считается контрольная потеря.
Итог — runs/r1/adapter (лучший по контрольной потере); в GGUF — to_gguf.py."""
import argparse
import json
import math
import pathlib
import random
import time

import torch
from PIL import Image, ImageEnhance, ImageFilter

import cpvl


def augment(im, r):
    """заводской скан чуть иначе: сдвиг, поворот, контраст, точки — размер тот же"""
    w, h = im.size
    dx, dy = int(w * r.uniform(-0.015, 0.015)), int(h * r.uniform(-0.015, 0.015))
    im = im.rotate(r.uniform(-0.6, 0.6), translate=(dx, dy), fillcolor=255, resample=Image.BILINEAR)
    if r.random() < 0.5:
        im = ImageEnhance.Contrast(im).enhance(r.uniform(0.7, 1.4))
    if r.random() < 0.3:
        im = im.filter(ImageFilter.GaussianBlur(r.uniform(0.3, 0.9)))
    return im


def samples(a, prompts, labels, sheets, rnd):
    train, held = [], []
    for k, s in sheets.items():
        lab = labels.get(k)
        if not lab:
            continue
        for tag in cpvl.TAGS:
            if s[tag] is None or tag not in prompts:
                continue
            ex = {'image': str(s[tag]), 'tag': tag, 'answer': cpvl.target(lab, tag), 'real': True}
            if lab.get('eval'):
                held.append(ex)
            else:
                train += [ex] * a.real_repeat
    if a.synth:
        for line in open(a.synth, encoding='utf-8'):
            ex = json.loads(line)
            if ex['tag'] in prompts:
                train.append({**ex, 'real': False})
    rnd.shuffle(train)
    return train, held


class Encoder:
    def __init__(self, proc, preset):
        self.proc, self.tok = proc, proc.tokenizer
        self.kw = preset.get('template_kwargs', {})
        ip = proc.image_processor
        px = (getattr(ip, 'patch_size', 16) * getattr(ip, 'merge_size', 2)) ** 2
        lo, hi = preset.get('min_image_tokens', 8) * px, preset.get('max_image_tokens', 4096) * px
        ip.size = {'shortest_edge': lo, 'longest_edge': hi}  # как llama.cpp: пределы токенов картинки
        for k, v in (('min_pixels', lo), ('max_pixels', hi)):
            if hasattr(ip, k):
                setattr(ip, k, v)

    def __call__(self, image, question, answer):
        msgs = [{'role': 'user', 'content': [{'type': 'image'}, {'type': 'text', 'text': question}]}]
        prompt = self.proc.apply_chat_template(msgs, tokenize=False, add_generation_prompt=True, **self.kw)
        full = self.proc.apply_chat_template(msgs + [{'role': 'assistant', 'content': [{'type': 'text', 'text': answer}]}],
                                             tokenize=False, **self.kw)
        tail = full[len(prompt):].rstrip('\n') if full.startswith(prompt) else answer + (self.tok.eos_token or '')
        b = self.proc(text=[prompt], images=[image], return_tensors='pt')
        tail_ids = self.tok(tail, add_special_tokens=False, return_tensors='pt')['input_ids']
        n0, L = b['input_ids'].shape[1], b['input_ids'].shape[1] + tail_ids.shape[1]
        out = {}
        for k, v in b.items():  # ответ — в конец всех потокенных полей
            if torch.is_tensor(v) and v.dim() == 2 and v.shape[1] == n0:
                add = tail_ids if k == 'input_ids' else torch.ones_like(tail_ids) if k == 'attention_mask' else \
                    torch.zeros(1, tail_ids.shape[1], dtype=v.dtype)
                v = torch.cat([v, add.to(v.dtype)], 1)
            out[k] = v
        return out, n0, L


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--preset', default='qwen3.5-4b')
    ap.add_argument('--base', required=True, help='папка весов Hugging Face (hf download …)')
    ap.add_argument('--dump', required=True), ap.add_argument('--labels', required=True)
    ap.add_argument('--synth', help='synth.jsonl из synth.py')
    ap.add_argument('--out', required=True)
    ap.add_argument('--real-repeat', type=int, default=6, help='сколько раз каждый заводской пример (с искажениями)')
    ap.add_argument('--epochs', type=float, default=1.0)
    ap.add_argument('--lr', type=float, default=1e-4)
    ap.add_argument('--rank', type=int, default=16)
    ap.add_argument('--accum', type=int, default=4)
    ap.add_argument('--eval-every', type=int, default=300, help='примеров между контрольными замерами')
    ap.add_argument('--max-samples', type=int, default=0, help='для пробы: остановиться после N примеров')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--resume', help='продолжить: папка адаптера (adapter) прерванного обучения')
    ap.add_argument('--start', type=int, default=0, help='с какого примера продолжить (см. train.log)')
    ap.add_argument('--vram-gb', type=float, default=5.6,
                    help='предел видеопамяти обучения: сверх него Windows уводит данные в общую память, и шаг '
                         'замедляется в разы — лучше пропустить пример')
    a = ap.parse_args()

    from peft import LoraConfig, get_peft_model
    from transformers import AutoModelForImageTextToText, AutoProcessor, BitsAndBytesConfig

    preset = cpvl.load_presets()[a.preset]
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    logf = open(out / 'train.log', 'a', encoding='utf-8')

    def log(*s):
        line = ' '.join(map(str, s))
        print(line, flush=True)
        logf.write(line + '\n')
        logf.flush()

    rnd = random.Random(a.seed)
    torch.manual_seed(a.seed)
    torch.cuda.set_per_process_memory_fraction(min(1.0, a.vram_gb * 2 ** 30 / torch.cuda.get_device_properties(0).total_memory))
    prompts = cpvl.load_prompts(a.dump)
    train, held = samples(a, prompts, cpvl.load_labels(a.labels), cpvl.load_sheets(a.dump), rnd)
    total = int(len(train) * a.epochs)
    if a.max_samples:
        total = min(total, a.max_samples)
    log(f'== {time.strftime("%Y-%m-%d %H:%M")} {a.preset}: обучение {len(train)} примеров '
        f'(заводских {sum(e["real"] for e in train)}), контроль {len(held)}, шагов {total}')

    proc = AutoProcessor.from_pretrained(a.base)
    enc = Encoder(proc, preset)
    bnb = BitsAndBytesConfig(load_in_4bit=True, bnb_4bit_quant_type='nf4', bnb_4bit_compute_dtype=torch.bfloat16,
                             bnb_4bit_use_double_quant=True, llm_int8_skip_modules=[preset['vision'], 'lm_head'])
    model = AutoModelForImageTextToText.from_pretrained(a.base, quantization_config=bnb, dtype=torch.bfloat16,
                                                        device_map={'': 0}, attn_implementation='sdpa')
    vis = next(m for n, m in model.named_modules() if n.split('.')[-1] == preset['vision'])
    for p in vis.parameters():
        p.requires_grad_(False)
    vis_forward = vis.forward

    def vis_nograd(*x, **k):  # картинки не учим: без градиентов и без памяти под них
        with torch.no_grad():
            return vis_forward(*x, **k)

    vis.forward = vis_nograd
    model.gradient_checkpointing_enable(gradient_checkpointing_kwargs={'use_reentrant': False})
    model.enable_input_require_grads()
    model = get_peft_model(model, LoraConfig(r=a.rank, lora_alpha=2 * a.rank, lora_dropout=0.05,
                                             target_modules=preset['lora_targets']))
    if a.resume:
        from peft import set_peft_model_state_dict
        from safetensors.torch import load_file
        set_peft_model_state_dict(model, load_file(str(pathlib.Path(a.resume) / 'adapter_model.safetensors')))
        log(f'продолжаю с примера {a.start}, адаптер {a.resume}')
    inner = model.base_model.model
    head = inner.get_output_embeddings()
    params = [p for p in model.parameters() if p.requires_grad]
    log(f'учим {sum(p.numel() for p in params) / 1e6:.1f} млн параметров')
    opt = torch.optim.AdamW(params, lr=a.lr, weight_decay=0.0)
    steps = max(1, total // a.accum)
    warm = max(1, steps // 20)
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: min(1.0, (s + 1) / warm) * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * min(1.0, s / steps)))))
    for _ in range(a.start // a.accum):
        sched.step()

    def loss_of(ex, aug):
        im = Image.open(ex['image']).convert('L')
        if aug:
            im = augment(im, rnd)
        b, n0, L = enc(im.convert('RGB'), prompts[ex['tag']]['q'], ex['answer'])
        b = {k: v.to(0) if torch.is_tensor(v) else v for k, v in b.items()}
        h = inner.model(**b).last_hidden_state[0]
        pos = torch.arange(n0 - 1, L - 1, device=0)
        logits = head(h[pos]).float()  # словарь большой: логиты только на ответе
        return torch.nn.functional.cross_entropy(logits, b['input_ids'][0, pos + 1]), L

    def evaluate():
        model.eval()
        tot = {}
        with torch.no_grad():
            for ex in held:
                l, _ = loss_of(ex, False)
                tot.setdefault(ex['tag'], []).append(l.item())
        model.train()
        per = {k: round(sum(v) / len(v), 4) for k, v in tot.items()}
        return (sum(sum(v) for v in tot.values()) / max(1, sum(len(v) for v in tot.values()))), per

    best = float('inf')
    if held:
        best, per = evaluate()
        log(f'контроль {"на старте" if a.resume else "до обучения"}: {best:.4f} {per}')
        if not a.resume:
            best = float('inf')
    model.train()
    t0, run, ntok = time.time(), [], 0
    skipped = 0
    for i in range(a.start, total):
        ex = train[i % len(train)]
        try:
            loss, L = loss_of(ex, ex['real'])
            (loss / a.accum).backward()
        except torch.OutOfMemoryError:
            skipped += 1
            log(f'  пример {i} не влез в {a.vram_gb} ГБ — пропущен ({ex["tag"]})')
            opt.zero_grad(set_to_none=True)
            torch.cuda.empty_cache()
            continue
        run.append(loss.item())
        ntok += L
        if (i + 1) % a.accum == 0 or i + 1 == total:
            torch.nn.utils.clip_grad_norm_(params, 1.0)
            opt.step()
            sched.step()
            opt.zero_grad(set_to_none=True)
        if (i + 1) % 20 == 0 and run:
            el, done = time.time() - t0, i + 1 - a.start
            log(f'{i + 1}/{total} потеря {sum(run) / len(run):.4f} lr {sched.get_last_lr()[0]:.2e} '
                f'{el / done:.1f} с/пример, осталось {el / done * (total - i - 1) / 60:.0f} мин, '
                f'память {torch.cuda.max_memory_allocated() >> 20} МБ')
            run = []
            torch.cuda.empty_cache()  # кэш не копится до предела видеопамяти
        if held and ((i + 1) % a.eval_every == 0 or i + 1 == total):
            v, per = evaluate()
            log(f'контроль после {i + 1}: {v:.4f} {per}')
            if v < best:
                best = v
                model.save_pretrained(out / 'adapter')
                log('  сохранён адаптер (лучший)')
        elif not held and i + 1 == total:
            model.save_pretrained(out / 'adapter')
    model.save_pretrained(out / 'adapter_last')
    log(f'готово за {(time.time() - t0) / 60:.0f} мин; лучший контроль {best:.4f}; пропущено {skipped}')


if __name__ == '__main__':
    main()
