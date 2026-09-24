#!/usr/bin/env python3
"""Нормы расчёта резки из страницы giriaja-hall → native/cutting_norms.h.

Окно «Расчёт резки и газов» в CursorPad — своё (cutting.c), не HTML, но
считает по той же таблице NORMS, что и страница cutting-calculator.html.
Таблицу отсюда переносим как есть; вид резки (лазер, плазма…) и порядок
материалов считаем здесь же, как их считает страница (материалы — сортировкой
localeCompare('ru') через node, если он есть).

Запуск:  python3 native/make_cutting.py [путь к cutting-calculator.html]
Без пути — с raw.githubusercontent.com.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "cutting_norms.h")
RAW = "https://raw.githubusercontent.com/pidrpen/giriaja-hall/main/cutting-calculator.html"

NUM_FIELDS = ["grindingPerM", "ragPerM", "laserNozzlePerM", "plasmaCathodePerM", "plasmaNozzlePerM",
              "plasmaTipPerM", "mouthpiecePerM", "nitrogen", "argon", "hydrogen", "gasMix", "oxygen",
              "propane", "speed"]
STR_FIELDS = ["equipment", "grindingDisc", "laserNozzle", "plasmaCathode", "plasmaNozzle", "plasmaTip",
              "mouthpiece"]


def equip_kind(eq):  # как equipKind() на странице
    if not eq:
        return "—"
    e = eq.lower()
    if "bystronic" in e or "fiber" in e or "лазер" in e:
        return "Лазер"
    if "плазма" in e or "vanad" in e:
        return "Плазма"
    if "кристал" in e or "мтр" in e:
        return "Газовая резка"
    if "гидро" in e:
        return "Гидроабразив"
    return "Прочее"


def cstr(s):
    if s is None:
        return "NULL"
    return 'L"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cnum(v):
    if v is None:
        return "CUT_NA"
    return repr(float(v))


def ru_sorted(items):
    node = shutil.which("node")
    if node:
        js = "const a=JSON.parse(process.argv[1]);a.sort((x,y)=>x.localeCompare(y,'ru'));console.log(JSON.stringify(a))"
        out = subprocess.run([node, "-e", js, json.dumps(items)], capture_output=True, text=True, check=True)
        return json.loads(out.stdout)
    print("! node нет — материалы по простой сортировке, порядок может отличаться от страницы")
    return sorted(items)


def main():
    if len(sys.argv) > 1:
        html = open(sys.argv[1], encoding="utf-8").read()
    else:
        html = urllib.request.urlopen(RAW, timeout=60).read().decode("utf-8")
    m = re.search(r"const NORMS = (\[.*?\]);\n", html, re.S)
    norms = json.loads(m.group(1))
    mats = ru_sorted(sorted({n["material"] for n in norms}))
    lines = [
        "/* Создано native/make_cutting.py из cutting-calculator.html (giriaja-hall). Руками не править. */",
        "#define CUT_NUM %d /* поля-числа: %s */" % (len(NUM_FIELDS), ", ".join(NUM_FIELDS)),
        "#define CUT_STR %d /* поля-строки: %s */" % (len(STR_FIELDS), ", ".join(STR_FIELDS)),
        "typedef struct {",
        "  double thickness;",
        "  const wchar_t *material, *kind;",
        "  const wchar_t *str[CUT_STR];",
        "  double num[CUT_NUM];",
        "} CutNorm;",
        "",
        "static const CutNorm kCutNorms[] = {",
    ]
    for n in norms:
        lines.append("    {%s, %s, %s, {%s}, {%s}}," % (
            cnum(n["thickness"]), cstr(n["material"]), cstr(equip_kind(n["equipment"])),
            ", ".join(cstr(n[f]) for f in STR_FIELDS), ", ".join(cnum(n[f]) for f in NUM_FIELDS)))
    lines += ["};", "", "/* материалы в порядке страницы (localeCompare 'ru') */",
              "static const wchar_t *const kCutMaterials[] = {" + ", ".join(cstr(x) for x in mats) + "};", ""]
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(OUT, len(norms), "норм,", len(mats), "материалов")


if __name__ == "__main__":
    main()
