#!/usr/bin/env python3
"""Страницы из pidrpen/giriaja-hall для меню «Ещё» — с библиотеками внутри.

Страницы берут свои библиотеки (pako, UTIF, pdf-lib, jsPDF, Tailwind) с
CDN. На рабочем ПК без интернета они бы не открылись, поэтому здесь каждая
<script src="https://…"> заменяется самим скриптом, и страница становится
одним файлом. Готовое кладётся в native/tools/, а оттуда build.sh вшивает
страницы в exe (cursorpad.rc, RCDATA 322): у курсора они всегда с собой,
сеть не нужна.

Запуск:  python3 native/make_tools.py [путь к клону giriaja-hall]
Без пути страницы берутся с raw.githubusercontent.com. Страницы поменялись
в giriaja-hall — перезапустить, собрать и выпустить новый CursorPad.
"""
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "tools")
RAW = "https://raw.githubusercontent.com/pidrpen/giriaja-hall/main/"
HUB = "https://pidrpen.github.io/giriaja-hall/"

# файл в giriaja-hall → имя у нас (его же знает CursorPad, см. tools.c)
PAGES = [
    # расчёт резки и объединение TIFF / PDF переписаны своими окнами CursorPad
    # (cutting.c, tiffmerge.c) — страницы для них больше не нужны
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


FA_CSS = "https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css"
FA_FONTS = "https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/webfonts/"


def offline_styles(html):
    """Внешние шрифты и значки — главный тормоз: на рабочей сети без доступа
    к fonts.googleapis.com / cdnjs браузер ждёт их до таймаута, прежде чем
    нарисовать страницу. Шрифты Google убираем (будет системный), Font
    Awesome встраиваем: его CSS и два нужных шрифта (solid, regular) —
    прямо в страницу, остальные его шрифты ссылаются на несуществующий
    локальный файл и отваливаются сразу."""
    import base64

    html = re.sub(r'<link[^>]*rel="preconnect"[^>]*>\s*', "", html)
    html = re.sub(r'<link[^>]*href="https://fonts\.googleapis\.com/[^"]*"[^>]*>\s*', "", html)
    html = re.sub(r"""@import\s+url\(['"]?https://fonts\.googleapis\.com/[^)]*\);?""", "", html)
    if FA_CSS in html:
        css = get(FA_CSS).decode("utf-8")
        for font in ("fa-solid-900.woff2", "fa-regular-400.woff2"):
            data = base64.b64encode(get(FA_FONTS + font)).decode("ascii")
            # шрифт упомянут трижды (Font Awesome 6 и совместимость с 5 и 4) — вшиваем
            # только в первое, для «6 Free»; остальные отвалятся сразу, они не нужны
            css = css.replace("../webfonts/" + font, "data:font/woff2;base64," + data, 1)
        print("   + Font Awesome (solid, regular)", len(css) // 1024, "КБ")
        html = re.sub(r'<link[^>]*href="%s"[^>]*>' % re.escape(FA_CSS), lambda m: "<style>%s</style>" % css, html)
    left = re.findall(r'(?:href|src)="(https?://(?!pidrpen\.github\.io)[^"]+)"', html)
    left += re.findall(r"""@import\s+url\(['"]?(https?://[^'")]+)""", html)
    if left:
        print("   ! остались внешние ссылки:", left)
    return html


def main():
    src_dir = sys.argv[1] if len(sys.argv) > 1 else None
    os.makedirs(OUT, exist_ok=True)
    for src, dst in PAGES:
        print(src, "→", dst)
        html = page_source(src_dir, src)
        html = inline_scripts(html)
        html = offline_styles(html)
        # ссылки на соседние страницы хаба локально никуда не ведут — на сайт
        html = re.sub(r'href="(index|[\w-]+)\.html"', lambda m: 'href="%s%s.html"' % (HUB, m.group(1)), html)
        data = html.encode("utf-8")
        with open(os.path.join(OUT, dst), "wb") as f:
            f.write(data)
        print("   =", len(data) // 1024, "КБ")


if __name__ == "__main__":
    main()
