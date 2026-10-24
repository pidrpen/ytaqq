"""Страница сверки справочника стандартов (std.c) — для человека, без CursorPad.

  python -I native/std_review.py [папка]    (по умолчанию %USERPROFILE%\\CursorPad-std)

Читает все *.tsv папки (кроме pdf), пишет рядом «сверка.html»: каждая таблица
с источником-ссылкой, пометками «## ПРОВЕРИТЬ» из файла и автоматическими
проверками (масса круга по формуле, e шестигранника против S, диапазоны
«a-b»). Сами таблицы остаются только у пользователя: страница — локальный
файл, ни в репозиторий, ни в сеть она не идёт. Сверили — в файле таблицы
строку «#проверено нет» меняют на «#проверено да, дата, кто».
"""
import html
import math
import os
import re
import sys


def read(path):
    raw = open(path, 'rb').read()
    if raw[:2] == b'\xff\xfe':
        return raw[2:].decode('utf-16-le')
    try:
        return raw.decode('utf-8-sig')
    except UnicodeDecodeError:
        return raw.decode('cp1251')


def tables(path):
    """Как gs_parse: # — поля, первая строка без # — шапка, # после строк — новая таблица."""
    out, cur, notes = [], None, []
    inherit = ('стандарт', 'название', 'вид', 'источник')
    for line in read(path).replace('\r', '').split('\n'):
        s = line.strip()
        if s.startswith('##'):
            notes.append(s[2:].strip())
            continue
        if not s or s.startswith('# ') or s == '#':
            continue
        if s.startswith('#'):
            if cur is None or cur['head']:
                prev = cur
                cur = {'meta': {}, 'head': None, 'rows': [], 'notes': notes}
                notes = []
                if prev:
                    out.append(prev)
                    cur['meta'] = {k: v for k, v in prev['meta'].items() if k in inherit}
            key, _, val = s[1:].replace('\t', ' ', 1).partition(' ')
            val = val.replace('\t', ' ').strip()
            if key == 'примечание' and cur['meta'].get(key):
                val = cur['meta'][key] + ' ' + val
            cur['meta'][key] = val
            continue
        if cur is None:
            cur = {'meta': {}, 'head': None, 'rows': [], 'notes': notes}
            notes = []
        cells = [c.strip() for c in line.split('\t')]
        if cur['head'] is None:
            while len(cells) > 1 and not cells[-1]:
                cells.pop()
            cur['head'] = cells
        else:
            cur['rows'].append(cells)
    if cur and cur['head']:
        out.append(cur)
    return out


def num(s):
    s = s.replace(',', '.').replace('−', '-').strip()
    try:
        return float(s)
    except ValueError:
        return None


def col(t, *names):
    for i, h in enumerate(t['head']):
        hl = h.lower()
        if any(hl.startswith(n.lower()) for n in names):
            return i
    return -1


def checks(t):
    """Автоматические проверки: список (номер строки, текст)."""
    bad = []
    kind = t['meta'].get('вид', '').lower()
    ci_m = col(t, 'масса 1 м')
    ci_a = col(t, 'площадь')
    if 'круг' in kind and ci_m >= 0:
        for i, r in enumerate(t['rows']):
            d, m = num(r[0]), num(r[ci_m]) if ci_m < len(r) else None
            if d is None or m is None:
                continue
            area = math.pi * d * d / 400
            if abs(m - area * 0.785) > 0.0006 + m * 2e-3:
                bad.append((i, f'масса {r[ci_m]} кг/м, по формуле π·d²/4·7,85 — {area * 0.785:.3f}'))
            if ci_a >= 0 and ci_a < len(r) and num(r[ci_a]) is not None and abs(num(r[ci_a]) - area) > 0.0006 + area * 1e-4:
                bad.append((i, f'площадь {r[ci_a]} см², по формуле {area:.3f}'))
    ci_s, ci_e = col(t, 's, мм', 's номин'), col(t, 'e, не менее', 'e min (b)', 'e min')
    if ci_s >= 0 and ci_e >= 0:
        for i, r in enumerate(t['rows']):
            s, e = (num(r[ci_s]) if ci_s < len(r) else None), (num(r[ci_e]) if ci_e < len(r) else None)
            if s and e and not (1.05 * s <= e <= 1.16 * s):
                bad.append((i, f'e = {r[ci_e]} при S = {r[ci_s]}: у шестигранника e ≈ 1,13·S'))
    for i, r in enumerate(t['rows']):
        for j, c in enumerate(r[1:], 1):
            m = re.fullmatch(r'(\d+(?:[.,]\d+)?)\s*[-–]\s*(\d+(?:[.,]\d+)?)', c)
            if m and num(m.group(1)) >= num(m.group(2)):
                bad.append((i, f'диапазон «{c}» в столбце «{t["head"][j] if j < len(t["head"]) else j}»: начало не меньше конца'))
    return bad


def page(folder):
    files = []
    for root, dirs, names in os.walk(folder):
        dirs[:] = [d for d in dirs if d.lower() != 'pdf' and not d.startswith('.')]
        files += [os.path.join(root, n) for n in names if n.lower().endswith('.tsv')]
    files.sort()
    e = html.escape
    parts, total, unchecked, flagged, crossed = [], 0, 0, 0, 0
    for f in files:
        rel = os.path.relpath(f, folder)
        for t in tables(f):
            total += 1
            m = t['meta']
            ok = m.get('проверено', '').lower().startswith('да')
            unchecked += not ok
            crossed += bool(m.get('сверка'))
            bad = checks(t)
            warn = [n for n in t['notes'] if 'ПРОВЕРИТЬ' in n or 'исправлен' in n.lower()]
            flagged += bool(bad or warn)
            src = m.get('источник', 'не указан')
            link = lambda s: re.sub(r'(https?://[^\s,;)]+)', lambda x: f'<a href="{e(x.group(1))}">{e(x.group(1))}</a>', e(s))
            src_html = re.sub(r'(https?://[^\s,;)]+)', lambda x: f'<a href="{e(x.group(1))}">{e(x.group(1))}</a>', e(src))
            badrows = {i for i, _ in bad}
            rows = ''.join(
                '<tr' + (' class="bad"' if i in badrows else '') + '>' + ''.join(f'<td>{e(c)}</td>' for c in r) + '</tr>'
                for i, r in enumerate(t['rows']))
            items = ''.join(f'<li>{e(n)}</li>' for n in warn) + ''.join(
                f'<li>строка {i + 1} ({e(t["rows"][i][0])}): {e(x)}</li>' for i, x in bad)
            parts.append(f'''<section class="{'ok' if ok else 'todo'}">
<h2>{e(m.get('стандарт', '?'))} — табл. {e(m.get('таблица', ''))}</h2>
<p class="meta">{e(m.get('название', ''))}<br>Файл: <code>{e(rel)}</code> · строк: {len(t['rows'])} ·
{'<b class="yes">сверено: ' + e(m.get('проверено', '')) + '</b>' if ok else '<b class="no">не сверено</b>'}<br>Источник: {src_html}<br>Второй источник: {link(m['сверка']) if m.get('сверка') else '<b class="no">нет — сверить вручную</b>'}</p>
{'<ul class="warn">' + items + '</ul>' if items else '<p class="clean">Автоматические проверки замечаний не нашли.</p>'}
{'<p class="note">' + e(m['примечание']) + '</p>' if m.get('примечание') else ''}
<div class="scroll"><table><tr>{''.join(f'<th>{e(h)}</th>' for h in t['head'])}</tr>{rows}</table></div>
</section>''')
    return f'''<!doctype html><html lang="ru"><head><meta charset="utf-8"><title>Сверка справочника</title>
<meta name="viewport" content="width=device-width, initial-scale=1"><style>
:root{{--bg:#fafaf8;--fg:#1d1d1b;--mute:#6b6b66;--line:#ddd;--bad:#fde8e4;--ok:#e7f4ea;--acc:#b3541e}}
@media (prefers-color-scheme:dark){{:root{{--bg:#1b1b1a;--fg:#ecebe6;--mute:#a4a39c;--line:#3a3a37;--bad:#4a2620;--ok:#1f3a27;--acc:#e8915c}}}}
body{{background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,"Segoe UI",sans-serif;margin:0 auto;max-width:1200px;padding:16px}}
h1{{font-size:22px}} h2{{font-size:17px;margin:0 0 4px}} a{{color:var(--acc)}}
section{{border:1px solid var(--line);border-radius:8px;padding:12px 14px;margin:14px 0}}
section.ok{{border-left:5px solid #3c9a5f}} section.todo{{border-left:5px solid var(--acc)}}
.meta,.note,.clean{{color:var(--mute);margin:4px 0}} .yes{{color:#3c9a5f}} .no{{color:var(--acc)}}
ul.warn{{background:var(--bad);border-radius:6px;padding:8px 8px 8px 28px;margin:8px 0}}
.scroll{{overflow-x:auto}} table{{border-collapse:collapse;font-size:13px;margin-top:6px}}
td,th{{border:1px solid var(--line);padding:3px 6px;white-space:nowrap;text-align:left}} th{{background:var(--ok)}}
tr.bad td{{background:var(--bad)}} code{{font-size:13px}}
</style></head><body>
<h1>Сверка справочника стандартов</h1>
<p>Таблиц: <b>{total}</b>, не сверено человеком: <b>{unchecked}</b>, сверено кодом по второму источнику: <b>{crossed}</b>, с замечаниями: <b>{flagged}</b>. Папка: <code>{e(folder)}</code></p>
<p>Как сверять: откройте источник таблицы, сравните строки (подсвеченные — в первую очередь). Сошлось — в файле таблицы
замените строку <code>#проверено	нет</code> на <code>#проверено	да, дата, кто сверил</code> (Блокнот или Excel; сохранять как текст
с разделителями табуляции). Ответы CursorPad по несверенным таблицам идут с пометкой «НЕ СВЕРЕНО».</p>
{''.join(parts)}
</body></html>'''


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('USERPROFILE', '.'), 'CursorPad-std')
    out = os.path.join(folder, 'сверка.html')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(page(folder))
    print(out)


if __name__ == '__main__':
    main()
