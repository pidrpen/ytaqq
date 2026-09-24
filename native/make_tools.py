#!/usr/bin/env python3
"""Страницы из pidrpen/giriaja-hall для меню «Ещё» — с библиотеками внутри.

Страницы берут свои библиотеки (pako, UTIF, pdf-lib, jsPDF, Tailwind) с
CDN. На рабочем ПК без интернета они бы не открылись, поэтому здесь каждая
<script src="https://…"> заменяется самим скриптом, и страница становится
одним файлом. Готовое кладётся в public/tools/, откуда CursorPad скачивает
его теми же зеркалами, что и обновления, и хранит у себя.

Запуск:  python3 native/make_tools.py [путь к клону giriaja-hall]
Без пути страницы берутся с raw.githubusercontent.com. Страницы поменялись
в giriaja-hall — перезапустить и выложить public/tools/ (без нового exe).
"""
import hashlib
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "public", "tools")
RAW = "https://raw.githubusercontent.com/pidrpen/giriaja-hall/main/"
HUB = "https://pidrpen.github.io/giriaja-hall/"

# файл в giriaja-hall → имя у нас (его же знает CursorPad, см. tools.c)
PAGES = [
    ("cutting-calculator.html", "cutting.html"),
    ("tiff_merger.html", "tiff-merge.html"),
    ("tiff-a4-a3.html", "tiff-a4-a3.html"),
]


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "CursorPad-make-tools"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def page_source(src_dir, name):
    if src_dir:
        with open(os.path.join(src_dir, name), "rb") as f:
            return f.read().decode("utf-8")
    return get(RAW + name).decode("utf-8")


SCRIPT_RE = re.compile(r'<script([^>]*?)\ssrc="(https?://[^"]+)"([^>]*)>\s*</script>', re.I)


def inline_scripts(html):
    def repl(m):
        url = m.group(2)
        js = get(url).decode("utf-8")
        # внутри <script> не должно встретиться «</script» — иначе он закроется раньше
        js = re.sub(r"</(script)", r"<\\/\1", js, flags=re.I)
        print("   +", url, len(js) // 1024, "КБ")
        return "<script%s%s>/* %s */\n%s\n</script>" % (m.group(1), m.group(3), url, js)

    return SCRIPT_RE.sub(repl, html)


def main():
    src_dir = sys.argv[1] if len(sys.argv) > 1 else None
    os.makedirs(OUT, exist_ok=True)
    stamp = hashlib.sha256()
    for src, dst in PAGES:
        print(src, "→", dst)
        html = page_source(src_dir, src)
        html = inline_scripts(html)
        # ссылки на соседние страницы хаба локально никуда не ведут — на сайт
        html = re.sub(r'href="(index|[\w-]+)\.html"', lambda m: 'href="%s%s.html"' % (HUB, m.group(1)), html)
        data = html.encode("utf-8")
        stamp.update(data)
        with open(os.path.join(OUT, dst), "wb") as f:
            f.write(data)
        print("   =", len(data) // 1024, "КБ")
    # метка набора: CursorPad сверяет её и перекачивает страницы, только если она сменилась
    with open(os.path.join(OUT, "tools.txt"), "w") as f:
        f.write(stamp.hexdigest()[:16] + "\n")


if __name__ == "__main__":
    main()
