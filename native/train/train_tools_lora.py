"""Дообучение агента CursorPad выбирать инструмент (адаптер инструментов, tool-lora): QLoRA на
шагах tools_data.py. Учится только языковая часть; картинок нет. Потеря — только на ответе
(JSON-вызов и <|im_end|>), логиты считаются только на нём: вход шага ~5,5 тыс. токенов, а словарь
модели большой.

  python train_tools_lora.py --base base/Qwen3.5-4B --data tools/tools_train.jsonl --out runs/t1
  python to_gguf.py --adapter runs/t1/adapter --base base/Qwen3.5-4B --llama llama.cpp --out runs/t1/tool-lora-qwen3.5-4b.gguf

Контроль — 5 % учебных сценариев (целиком, не шагами), на них не учим."""
import argparse
import gc
import json
import math
import pathlib
import random
import sys
import time

import os

os.environ.setdefault('PYTORCH_ALLOC_CONF', 'expandable_segments:True')
import torch  # noqa: E402

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import cpvl  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--preset', default='qwen3.5-4b')
    ap.add_argument('--base', required=True)
    ap.add_argument('--data', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--epochs', type=float, default=1.0)
    ap.add_argument('--lr', type=float, default=1e-4)
    ap.add_argument('--rank', type=int, default=16)
    ap.add_argument('--lora-dropout', type=float, default=0.0,
                    help='0 — без копий входа у каждого слоя: на шаге в 5,5 тыс. токенов это ~0,5 ГБ видеопамяти')
    ap.add_argument('--no-control', action='store_true', help='для пробы скорости: без контрольных замеров')
    ap.add_argument('--accum', type=int, default=4)
    ap.add_argument('--eval-every', type=int, default=200)
    ap.add_argument('--max-samples', type=int, default=0, help='для пробы: остановиться после N примеров')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--vram-gb', type=float, default=5.8,
                    help='предел видеопамяти: шаг в 5,9 тыс. токенов берёт ~4,8 ГБ (таблица слов на процессоре); '
                         'сверх свободной видеопамяти Windows уводит часть в общую, и шаг идёт в 2–4 раза дольше')
    ap.add_argument('--max-tokens', type=int, default=6800, help='длиннее — пропустить (не влезут в --vram-gb)')
    a = ap.parse_args()

    from peft import LoraConfig, get_peft_model
    from transformers import AutoModelForImageTextToText, AutoProcessor, BitsAndBytesConfig
    import transformers.integrations.sdpa_attention as sdpa_mod
    # с enable_gqa (у Qwen3.5 голов ключей меньше) PyTorch уходит в «math» и строит матрицу внимания
    # L×L — на 5,5 тыс. токенов это ~2 ГБ на слой; «memory efficient» GQA не умеет, ключи повторяем сами
    sdpa_mod.use_gqa_in_sdpa = lambda *x, **k: False

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
    rows = [json.loads(l) for l in open(a.data, encoding='utf-8')]
    proc = AutoProcessor.from_pretrained(a.base)
    tok = proc.tokenizer
    kw = preset.get('template_kwargs', {})

    def encode(ex):
        prompt = tok.apply_chat_template(ex['messages'], tokenize=False, add_generation_prompt=True, **kw)
        p = tok(prompt, add_special_tokens=False)['input_ids']
        t = tok(ex['answer'] + '<|im_end|>', add_special_tokens=False)['input_ids']
        return torch.tensor([p + t]), len(p), len(p) + len(t)

    size = [encode(r)[2] for r in rows]
    log(f'длиннее {a.max_tokens} токенов — {sum(s > a.max_tokens for s in size)} шагов из {len(rows)}, пропущены')
    rows = [r for r, s in zip(rows, size) if s <= a.max_tokens]
    goals = sorted({r['goal'] for r in rows})
    rnd.shuffle(goals)
    held_goals = set(goals[:max(1, len(goals) // 20)])
    train = [r for r in rows if r['goal'] not in held_goals]
    held = [] if a.no_control else [r for r in rows if r['goal'] in held_goals]
    rnd.shuffle(train)
    total = int(len(train) * a.epochs)
    if a.max_samples:
        total = min(total, a.max_samples)
    log(f'== {time.strftime("%Y-%m-%d %H:%M")} {a.preset}: шагов агента {len(train)} (сценариев {len(goals) - len(held_goals)}), '
        f'контроль {len(held)}, примеров {total}')

    bnb = BitsAndBytesConfig(load_in_4bit=True, bnb_4bit_quant_type='nf4', bnb_4bit_compute_dtype=torch.bfloat16,
                             bnb_4bit_use_double_quant=True, llm_int8_skip_modules=[preset['vision'], 'lm_head'])
    model = AutoModelForImageTextToText.from_pretrained(a.base, quantization_config=bnb, dtype=torch.bfloat16,
                                                        device_map={'': 0}, attn_implementation='sdpa')
    for n, m in model.named_modules():
        if n.split('.')[-1] == preset['vision']:  # картинок нет: блок зрения — с видеокарты прочь
            for q in m.parameters():
                q.requires_grad_(False)
            m.to('cpu')
    torch.cuda.empty_cache()
    model.gradient_checkpointing_enable(gradient_checkpointing_kwargs={'use_reentrant': False})
    model.enable_input_require_grads()
    model = get_peft_model(model, LoraConfig(r=a.rank, lora_alpha=2 * a.rank, lora_dropout=a.lora_dropout,
                                             target_modules=preset['lora_targets']))
    inner = model.base_model.model
    # таблица слов (вход и выход у Qwen3.5 общие, bf16, 1,3 ГБ) не учится — на процессор: без неё шаг
    # помещается в ~4,8 ГБ, и Windows не уводит видеопамять в общую. Логиты — только на ответе, там же
    emb = inner.get_input_embeddings()
    emb.to('cpu')
    w32 = emb.weight.detach().float()
    torch.cuda.empty_cache()
    params = [q for q in model.parameters() if q.requires_grad]
    log(f'учим {sum(q.numel() for q in params) / 1e6:.1f} млн параметров')
    opt = torch.optim.AdamW(params, lr=a.lr, weight_decay=0.0)
    steps = max(1, total // a.accum)
    warm = max(1, steps // 20)
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: min(1.0, (s + 1) / warm) * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * min(1.0, s / steps)))))

    def loss_of(ex):
        ids, n0, L = encode(ex)
        # без маски: один пример, без отступов — SDPA берёт причинное ядро без матрицы L×L
        h = inner.model(inputs_embeds=emb(ids).to(0), use_cache=False).last_hidden_state[0]
        pos = torch.arange(n0 - 1, L - 1)
        logits = h[pos.to(0)].float().cpu() @ w32.T
        return torch.nn.functional.cross_entropy(logits, ids[0, pos + 1]), L

    def evaluate():
        model.eval()
        ls = []
        with torch.no_grad():
            for ex in held:
                ls.append(loss_of(ex)[0].item())
        model.train()
        # после замера кэш распределителя занимает видеопамять до предела: вместе с чужими программами
        # Windows уходит в общую память, и шаг обучения идёт не 7 с, а 30–80 (проба 28.10)
        gc.collect()
        torch.cuda.empty_cache()
        return sum(ls) / max(1, len(ls))

    best = evaluate() if held else float('inf')
    log(f'контроль до обучения: {best:.4f}')
    best = float('inf')
    model.train()
    t0, run, skipped = time.time(), [], 0
    for i in range(total):
        ex = train[i % len(train)]
        try:
            loss, L = loss_of(ex)
            (loss / a.accum).backward()
        except torch.OutOfMemoryError:
            skipped += 1
            log(f'  пример {i} не влез в {a.vram_gb} ГБ — пропущен ({ex["family"]})')
            loss = None  # иначе остатки графа держат память, и падают все следующие примеры
            opt.zero_grad(set_to_none=True)
            gc.collect()
            torch.cuda.empty_cache()
            continue
        run.append(loss.item())
        if (i + 1) % a.accum == 0 or i + 1 == total:
            torch.nn.utils.clip_grad_norm_(params, 1.0)
            opt.step()
            sched.step()
            opt.zero_grad(set_to_none=True)
        if (i + 1) % 20 == 0 and run:
            el = time.time() - t0
            log(f'{i + 1}/{total} потеря {sum(run) / len(run):.4f} lr {sched.get_last_lr()[0]:.2e} {el / (i + 1):.1f} с/пример, '
                f'осталось {el / (i + 1) * (total - i - 1) / 60:.0f} мин, память {torch.cuda.max_memory_allocated() >> 20} МБ')
            run = []
            torch.cuda.empty_cache()
        if held and ((i + 1) % a.eval_every == 0 or i + 1 == total):
            v = evaluate()
            log(f'контроль после {i + 1}: {v:.4f}')
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
