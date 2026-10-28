"""Проверка агента CursorPad по шагам: верный ли инструмент и аргументы на проверочном наборе
tools_data.py --split eval. Каждому шагу даются сообщения ровно как в программе (история —
эталонная), модель отвечает под той же JSON-схемой, что в CursorPad (agent_probe --schema).

  python eval_tools.py --data tools_eval.jsonl --out runs/tools_base.json
  python eval_tools.py --data tools_eval.jsonl --lora runs/t1/tool-lora-qwen3.5-4b.gguf --out runs/tools_t1.json

С --lora сервер грузит адаптер выключенным и каждый запрос просит вес 1 (как адаптер чертежей);
--both — в том же прогоне и без адаптера (вес 0), чтобы сравнение шло на одном сервере."""
import argparse
import json
import pathlib
import re
import sys
import time
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import agent_drive as d  # noqa: E402
import cpvl  # noqa: E402

MSG = 'Considering the limited time by the user, I have to give the solution based on the thinking directly now.'


def norm_path(p, ex):
    """Путь к одному виду: псевдонимы @desktop, @downloads, @documents, @last, @new — как их
    раскрывает программа на этом шаге (ex — пример с desktop, last, new)."""
    p = (p or '').strip().strip('"').replace('/', '\\').rstrip('\\')
    low = p.lower()
    desk = ex['desktop']
    for alias, base in (('@desktop', desk), ('@downloads', desk + '\\Downloads'), ('@documents', desk + '\\Documents'),
                        ('@last', ex.get('last') or desk), ('@new', ex.get('new') or '@new')):
        if low.startswith(alias):
            p = base + p[len(alias):]
            break
    p = re.sub(r'\\+', r'\\', p)
    return p.lower()


def norm_text(t):  # пробелы, регистр и точка в конце не меняют смысла записанного текста
    return re.sub(r'\s+', ' ', (t or '').replace('\r', '')).strip().rstrip('.').lower()


def same(want, got, ex):
    """(инструмент верный, аргументы верные) — главное: что программа в итоге сделает."""
    if not isinstance(got, dict) or got.get('tool') != want['tool']:
        return False, False
    t = want['tool']
    if t == 'app':
        if got.get('name') != want.get('name'):
            return False, False
        a = got.get('text') or got.get('path') or ''
        b = want.get('text', '')
        if want['name'] in ('file_read', 'rename_by_stamp'):
            wp, gp = b.split(';')[0], a.split(';')[0]
            ok = norm_path(wp, ex) == norm_path(gp, ex) and (('наимен' in b.lower()) == ('наимен' in a.lower()))
            return True, ok
        if want['name'] in ('plm_card', 'plm_where_used', 'archive_find'):
            key = re.sub(r'[\s.\-]', '', b.lower())
            return True, key in re.sub(r'[\s.\-]', '', a.lower())
        words = [w for w in re.findall(r'\w+', b.lower()) if len(w) > 1]
        return True, all(w in a.lower() for w in words)
    if t in ('done', 'ask'):
        return True, True
    keys = {'move': ['src', 'dst', 'pattern', 'recursive'], 'copy': ['src', 'dst', 'pattern', 'recursive'],
            'find': ['src', 'pattern', 'recursive'], 'list': ['path'], 'mkdir': ['path'], 'new_folder': ['path'],
            'inventory': ['path'], 'write': ['path', 'text'], 'excel_write': ['path', 'text', 'mode'],
            'rename': ['path', 'name'], 'office_read': ['path']}.get(t, list(want))
    for k in keys:
        w, g = want.get(k, ''), got.get(k, '')
        if k == 'recursive':
            w, g = w or 'no', g or 'no'
        if k in ('src', 'dst', 'path'):
            if norm_path(w, ex) != norm_path(g, ex):
                return True, False
        elif k == 'pattern':
            if w.lower() != g.lower():
                return True, False
        elif k == 'text' and t == 'excel_write':
            if [r.split('\t') for r in w.strip().split('\n')] != [r.split('\t') for r in g.strip().split('\n')]:
                return True, False
        elif norm_text(w) != norm_text(g):
            return True, False
    return True, True


def ask(url, ex, schema, scale):
    body = {'messages': ex['messages'], 'temperature': 0.2, 'seed': 1, 'max_tokens': 1024, 'stream': False,
            'cache_prompt': True, 'chat_template_kwargs': {'enable_thinking': False}, **schema}
    if scale is not None:
        body['lora'] = [{'id': 0, 'scale': scale}]
    req = urllib.request.Request(url + '/v1/chat/completions', json.dumps(body, ensure_ascii=False).encode(),
                                 {'Content-Type': 'application/json'})
    t = time.time()
    d_ = json.loads(urllib.request.urlopen(req, timeout=600).read())
    return d_['choices'][0]['message']['content'] or '', time.time() - t


def run_pass(url, rows, schema, scale, log):
    res = []
    for i, ex in enumerate(rows):
        raw, secs = ask(url, ex, schema, scale)
        try:
            got = json.loads(raw)
        except ValueError:
            got = None
        want = json.loads(ex['answer'])
        tool_ok, args_ok = same(want, got, ex)
        res.append({'family': ex['family'], 'step': ex['step'], 'goal': ex['goal'], 'want': ex['answer'], 'got': raw,
                    'tool': tool_ok, 'args': args_ok, 'json': got is not None, 'secs': round(secs, 2)})
        if (i + 1) % 20 == 0:
            log(f'  {i + 1}/{len(rows)}: инструмент {sum(r["tool"] for r in res)}, целиком {sum(r["args"] for r in res)}')
    return res


def summary(res):
    fam = {}
    for r in res:
        f = fam.setdefault(r['family'], [0, 0, 0])
        f[0] += 1
        f[1] += r['tool']
        f[2] += r['args']
    first = [r for r in res if r['step'] == 1]
    return {'шагов': len(res), 'инструмент верно': sum(r['tool'] for r in res), 'целиком верно': sum(r['args'] for r in res),
            'JSON': sum(r['json'] for r in res), 'первый шаг целиком': f'{sum(r["args"] for r in first)}/{len(first)}',
            'сек на шаг': round(sum(r['secs'] for r in res) / max(1, len(res)), 2),
            'по семействам (шагов, инструмент, целиком)': {k: v for k, v in sorted(fam.items())}}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--lora', help='адаптер инструментов GGUF')
    ap.add_argument('--both', action='store_true', help='с --lora: ещё и без адаптера на том же сервере')
    ap.add_argument('--preset', default='qwen3.5-4b')
    ap.add_argument('--server', help='llama-server.exe (по умолчанию — CursorPad)')
    ap.add_argument('--port', type=int, default=8393)
    ap.add_argument('--limit', type=int, default=0, help='для пробы: первые N шагов')
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    rows = [json.loads(l) for l in open(a.data, encoding='utf-8')]
    if a.limit:
        rows = rows[:a.limit]
    schema = d.probe_text('--schema')
    p = cpvl.load_presets()[a.preset]
    model = cpvl.models_dir() / p['dir'] / p['gguf']
    mmproj = cpvl.models_dir() / p['dir'] / p['mmproj']
    out = pathlib.Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    logf = open(out.with_suffix('.log'), 'w', encoding='utf-8')

    def log(*s):
        line = ' '.join(map(str, s))
        print(line, flush=True)
        logf.write(line + '\n')
        logf.flush()

    # ключи — как у CursorPad (ai_local.c): шаблон, размышление выключено, предел размышления, картинки
    args = ['--jinja', '--chat-template-kwargs', '{"enable_thinking":false}', '--reasoning-budget', '1792',
            '--reasoning-budget-message', MSG, '--image-min-tokens', '1024']
    report = {}
    with cpvl.Server(model, mmproj, args, a.lora, a.server, a.port, ctx=16384, log=out.with_suffix('.server.log')) as srv:
        passes = ([('без адаптера', 0.0)] if a.lora and a.both else []) + [('с адаптером' if a.lora else 'без адаптера',
                                                                            1.0 if a.lora else None)]
        for name, scale in passes:
            log(f'== {name}: {len(rows)} шагов')
            t = time.time()
            res = run_pass(srv.url, rows, schema, scale, log)
            s = summary(res)
            s['минут'] = round((time.time() - t) / 60, 1)
            log(json.dumps(s, ensure_ascii=False))
            report[name] = {'итог': s, 'шаги': res}
    out.write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main()
