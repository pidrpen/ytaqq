#!/usr/bin/env python3
"""Листы сверки таблиц нормативов (norms.c) со страницами книги.

Таблицы нормативов лежат только у пользователя (Документы\\CursorPad\\Нормативы):
их переносят из PDF один раз, а человек сверяет с книгой. Этот скрипт для каждой
таблицы пишет локальную страницу: слева — картинка страницы книги, справа — таблица,
как её прочитает CursorPad, сверху — замечания (не число, лишняя или пустая клетка,
ряд не монотонный — обычно это опечатка переноса). Ничего никуда не отправляет.

Страницы книги — картинками p0001.png… (так их пишет CursorPadOcr.exe --pdf книга.pdf
папка 150). Номер в книге и номер страницы PDF расходятся на --offset.

Запуск:  python native/norms_check.py ПАПКА_НОРМАТИВОВ [--pages ПАПКА_СТРАНИЦ] [--offset N]
Итог:    ПАПКА_НОРМАТИВОВ\\сверка\\index.html — список таблиц и листы сверки.
После сверки в файле таблицы строка «сверено<TAB>да ДД.ММ.ГГГГ Фамилия».
"""
import argparse
import html
import os
import re
import sys

NUM = re.compile(r"^\s*(\d+(?:[.,]\d+)?)\s*\*?\s*$")
RANGE = re.compile(r"^\s*(до|св\.?|свыше|более|больше)?\s*\d+(?:[.,]\d+)?(?:\s*(?:[-–—…]|\.\.)\s*\d+(?:[.,]\d+)?)?"
                   r"(?:\s+до\s+\d+(?:[.,]\d+)?)?(?=\s|$|[*])", re.I)
DASH = {"", "—", "-", "–", "х", "x"}
SKIP = {"readme.txt", "прочти.txt"}


def read_text(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\xff\xfe":
        return raw[2:].decode("utf-16-le")
    return raw.decode("utf-8-sig", errors="replace")


def unq(s):
    s = s.strip("  ")
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        s = s[1:-1].replace('""', '"')
    return s


def num(s):
    m = NUM.match(s)
    return float(m.group(1).replace(",", ".")) if m else None


def is_range(s):
    s = s.strip()
    return s in ("*",) or bool(RANGE.match(s))


def parse(text):
    """Как nm_base_add в norms.c: шапка «ключ — значение», строка «поз.», строки."""
    head, cols, rows = {}, None, []
    for line in text.replace("\r", "").split("\n"):
        if not line.strip() or line.startswith("#"):
            continue
        cells = [unq(c) for c in line.split("\t")]
        while cells and not cells[-1]:
            cells.pop()
        if not cells:
            continue
        if cols is None:
            if cells[0].lower().startswith("поз"):
                cols = cells
            elif len(cells) >= 2:
                head[cells[0].strip().lower()] = cells[1]
            continue
        rows.append(cells)
    return head, cols or [], rows


def layout(head, cols):
    """Какие столбцы — ключи, «как в книге», значения."""
    records = head.get("вид", "").strip().lower() == "записи"
    keys, vals, book = [], [], None
    for j, c in enumerate(cols[1:], 1):
        lc = c.strip().lower()
        if lc in ("как в книге", "в книге", "наименование"):
            book = j
        elif (records and keys) or lc == "значение" or (not records and is_range(c)):
            vals.append(j)
        else:
            keys.append(j)
    return records, keys, vals, book


def monotonic(xs):
    xs = [x for x in xs if x is not None]
    up = all(b >= a for a, b in zip(xs, xs[1:]))
    down = all(b <= a for a, b in zip(xs, xs[1:]))
    return up or down


def check(head, cols, rows):
    """Замечания: (номер строки или None, номер столбца или None, текст)."""
    out = []
    for k in ("назначение", "источник", "страница"):
        if not head.get(k):
            out.append((None, None, f"нет строки «{k}»"))
    if not cols:
        out.append((None, None, "нет шапки таблицы (строка «поз.»)"))
        return out
    records, keys, vals, book = layout(head, cols)
    if not vals:
        out.append((None, None, "нет столбцов значений"))
    if not records and len(vals) > 1 and not head.get("столбцы"):
        out.append((None, None, "нет строки «столбцы» — по какому параметру выбирать столбец"))
    if not head.get("ед") and not records:
        out.append((None, None, "нет строки «ед» (мин, %, коэф…)"))
    if records:
        return out
    seen = {}
    for i, r in enumerate(rows):
        if len(r) > len(cols):
            out.append((i, None, f"поз. {r[0]}: клеток {len(r)}, а в шапке {len(cols)}"))
        pos = r[0] if r else ""
        if pos and pos in seen:
            out.append((i, 0, f"поз. {pos} повторяется (строка {seen[pos] + 1})"))
        seen.setdefault(pos, i)
        for j in vals:
            cell = r[j] if j < len(r) else ""
            if cell.strip() not in DASH and num(cell) is None:
                out.append((i, j, f"поз. {pos}, «{cols[j]}»: «{cell}» — не число"))
        xs = [num(r[j]) if j < len(r) else None for j in vals]
        if len([x for x in xs if x is not None]) >= 3 and not monotonic(xs):
            out.append((i, None, f"поз. {pos}: значения по строке то растут, то падают — проверьте"))
    # по столбцу — внутри групп с одинаковым словом-ключом (числовые ключи меняются)
    for j in vals:
        group, prev_key = [], None
        for i, r in enumerate(rows + [None]):
            key = None
            if r is not None:
                key = tuple(r[k] if k < len(r) and not is_range(r[k]) else "" for k in keys)
                key = tuple(v if v else (prev_key[n] if prev_key else "") for n, v in enumerate(key))
            if r is None or (prev_key is not None and key != prev_key):
                xs = [num(rows[g][j]) if j < len(rows[g]) else None for g in group]
                if len([x for x in xs if x is not None]) >= 3 and not monotonic(xs):
                    out.append((group[0], j, f"столбец «{cols[j]}», поз. {rows[group[0]][0]}…{rows[group[-1]][0]}: "
                                             "значения то растут, то падают — проверьте"))
                group = []
            if r is not None:
                group.append(i)
                prev_key = key
    return out


CSS = """
:root{--bg:#f6f5f2;--fg:#1d1d1b;--mut:#6b6a66;--line:#d9d6cf;--warn:#fff1b8;--warnb:#c99a00;--ok:#2f7d32;--card:#fff}
@media (prefers-color-scheme: dark){:root{--bg:#1b1b1a;--fg:#ecebe7;--mut:#a3a19b;--line:#3a3936;--warn:#4a3d00;
--warnb:#e0b400;--ok:#7bc47f;--card:#242422}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 Segoe UI,system-ui,sans-serif}
header{padding:12px 16px;border-bottom:1px solid var(--line)}h1{font-size:17px;margin:0 0 4px}
.mut{color:var(--mut)}.ok{color:var(--ok)}.wrap{display:flex;gap:16px;padding:16px;align-items:flex-start}
.page{flex:1 1 50%;min-width:0;position:sticky;top:8px;max-height:calc(100vh - 16px);overflow:auto;
border:1px solid var(--line);background:#fff}.page img{width:100%;display:block}
.tab{flex:1 1 50%;min-width:0;overflow:auto}table{border-collapse:collapse;background:var(--card)}
td,th{border:1px solid var(--line);padding:3px 7px;text-align:right;white-space:nowrap}
th{background:var(--bg)}td.k,th.k{text-align:left}td.w{background:var(--warn);outline:2px solid var(--warnb)}
ul.w li{margin:2px 0}.zoom{margin-left:8px}a{color:inherit}
@media (max-width:800px){.wrap{flex-direction:column}.page{position:static;max-height:none}}
"""


def page_image(pages, offset, page):
    if not pages or not page:
        return None
    m = re.match(r"\s*(\d+)", page)
    if not m:
        return None
    p = os.path.join(pages, "p%04d.png" % (int(m.group(1)) + offset))
    return p if os.path.exists(p) else None


def sheet(name, head, cols, rows, warns, img, out_dir):
    title = f"{head.get('назначение', name)} — к. {head.get('карта', '?')} с. {head.get('страница', '?')}"
    bad = {(i, j) for i, j, _ in warns if i is not None and j is not None}
    badrow = {i for i, j, _ in warns if i is not None and j is None}
    records, keys, vals, book = layout(head, cols) if cols else (False, [], [], None)
    h = ["<!doctype html><html lang='ru'><head><meta charset='utf-8'>",
         "<meta name='viewport' content='width=device-width, initial-scale=1'>",
         f"<title>Сверка {html.escape(name)}</title><style>{CSS}</style></head><body><header>",
         f"<h1>{html.escape(title)}</h1><div class='mut'>{html.escape(name)} · "
         f"{html.escape(head.get('источник', 'источник не указан'))} · ед.: {html.escape(head.get('ед', '—'))}"
         f" · столбцы по: {html.escape(head.get('столбцы', '—'))}</div>"]
    checked = head.get("сверено", "")
    h.append(f"<div class='{'ok' if checked.lower().startswith('да') else 'mut'}'>сверено: "
             f"{html.escape(checked or 'нет')}</div>")
    if warns:
        h.append("<ul class='w'>" + "".join(f"<li>{html.escape(t)}</li>" for _, _, t in warns) + "</ul>")
    else:
        h.append("<div class='ok'>Замечаний нет — осталось сверить числа с книгой.</div>")
    h.append("</header><div class='wrap'>")
    if img:
        rel = os.path.relpath(img, out_dir).replace("\\", "/")
        h.append(f"<div class='page'><img src='{html.escape(rel)}' alt='страница книги'></div>")
    else:
        h.append("<div class='page'><p class='mut' style='padding:12px'>Картинки страницы нет: "
                 "укажите --pages (и --offset) или сверяйте с PDF рядом.</p></div>")
    h.append("<div class='tab'><table><tr>")
    for j, c in enumerate(cols):
        h.append(f"<th class='{'k' if j == 0 or j in keys or j == book else ''}'>{html.escape(c)}</th>")
    h.append("</tr>")
    for i, r in enumerate(rows):
        h.append("<tr>")
        for j in range(max(len(cols), len(r))):
            cell = r[j] if j < len(r) else ""
            cls = []
            if j == 0 or j in keys or j == book:
                cls.append("k")
            if (i, j) in bad or (i in badrow and j in vals):
                cls.append("w")
            h.append(f"<td class='{' '.join(cls)}'>{html.escape(cell)}</td>")
        h.append("</tr>")
    h.append("</table></div></div></body></html>")
    path = os.path.join(out_dir, os.path.splitext(name)[0] + ".html")
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join(h))
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description="Листы сверки таблиц нормативов с книгой")
    ap.add_argument("folder", help="папка нормативов (Документы\\CursorPad\\Нормативы)")
    ap.add_argument("--pages", help="папка с p0001.png… (CursorPadOcr.exe --pdf книга.pdf папка 150)")
    ap.add_argument("--offset", type=int, default=0, help="номер страницы PDF минус номер в книге")
    a = ap.parse_args(argv)
    out_dir = os.path.join(a.folder, "сверка")
    os.makedirs(out_dir, exist_ok=True)
    items = []
    for name in sorted(os.listdir(a.folder)):
        p = os.path.join(a.folder, name)
        if not os.path.isfile(p) or not name.lower().endswith((".tsv", ".txt")) or name.lower() in SKIP:
            continue
        head, cols, rows = parse(read_text(p))
        warns = check(head, cols, rows)
        img = page_image(a.pages, a.offset, head.get("страница", ""))
        sheet_path = sheet(name, head, cols, rows, warns, img, out_dir)
        items.append((name, head, len(rows), warns, os.path.basename(sheet_path)))
    idx = ["<!doctype html><html lang='ru'><head><meta charset='utf-8'>",
           "<meta name='viewport' content='width=device-width, initial-scale=1'>",
           f"<title>Сверка нормативов</title><style>{CSS}</style></head><body><header><h1>Сверка нормативов</h1>",
           f"<div class='mut'>{html.escape(a.folder)} · таблиц: {len(items)} · сверено: "
           f"{sum(1 for _, h, *_ in items if h.get('сверено', '').lower().startswith('да'))}</div></header>",
           "<div class='wrap'><div class='tab'><table><tr><th class='k'>файл</th><th class='k'>назначение</th>"
           "<th>карта</th><th>стр.</th><th>строк</th><th>замечаний</th><th class='k'>сверено</th></tr>"]
    for name, head, n, warns, link in items:
        ok = head.get("сверено", "").lower().startswith("да")
        idx.append(f"<tr><td class='k'><a href='{html.escape(link)}'>{html.escape(name)}</a></td>"
                   f"<td class='k'>{html.escape(head.get('назначение', '—'))}</td>"
                   f"<td>{html.escape(head.get('карта', ''))}</td><td>{html.escape(head.get('страница', ''))}</td>"
                   f"<td>{n}</td><td class='{'w' if warns else ''}'>{len(warns)}</td>"
                   f"<td class='k {'ok' if ok else 'mut'}'>{html.escape(head.get('сверено', 'нет'))}</td></tr>")
    idx.append("</table></div></div></body></html>")
    with open(os.path.join(out_dir, "index.html"), "w", encoding="utf-8") as f:
        f.write("".join(idx))
    bad = sum(1 for *_, w, _ in items if w)
    print(f"таблиц {len(items)}, с замечаниями {bad}: {os.path.join(out_dir, 'index.html')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
