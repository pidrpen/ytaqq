#!/usr/bin/env python3
"""Справочник операций 1С из «1с опер.xlsx» (pidrpen/cursor) → native/ops_1c.h.

В файле два столбца: код 1С («00-001323») и название, как оно записано в 1С,
— с номером операции впереди («9186 Термоконтактная сварка внахлестку»).
«В 1С» кладёт в буфер именно это название: по нему 1С находит операцию сразу.
Номер (9186) — тот же код, что у операции в справочнике операций PLM (Code):
по нему операция ТП и сводится со строкой 1С, а по названию — если кода нет.

Запуск:  python3 native/make_ops_1c.py путь/к/1с\\ опер.xlsx
Читается только стандартной библиотекой (xlsx — это zip с xml).
"""
import os
import re
import sys
import zipfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "ops_1c.h")
NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}


def read_rows(path):
    with zipfile.ZipFile(path) as z:
        shared = []
        if "xl/sharedStrings.xml" in z.namelist():
            for si in ET.fromstring(z.read("xl/sharedStrings.xml")).findall("m:si", NS):
                shared.append("".join(t.text or "" for t in si.iter("{%s}t" % NS["m"])))
        sheet = sorted(n for n in z.namelist() if re.match(r"xl/worksheets/sheet\d+\.xml$", n))[0]
        for row in ET.fromstring(z.read(sheet)).iter("{%s}row" % NS["m"]):
            cells = {}
            for c in row.findall("m:c", NS):
                col = re.match(r"[A-Z]+", c.get("r")).group(0)
                v = c.find("m:v", NS)
                if c.get("t") == "s" and v is not None:
                    cells[col] = shared[int(v.text)]
                elif c.get("t") == "inlineStr":
                    cells[col] = "".join(t.text or "" for t in c.iter("{%s}t" % NS["m"]))
                elif v is not None:
                    cells[col] = v.text
            yield cells.get("A", ""), cells.get("B", "")


def norm(s):  # как op1c_norm() в pad_extra.c: строчные, ё→е, один пробел
    s = s.lower().replace("ё", "е")
    return re.sub(r"\s+", " ", s).strip(" .")


def c_str(s):
    return 'L"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ops = []
    for code, name in read_rows(sys.argv[1]):
        code, name = (code or "").strip(), re.sub(r"\s+", " ", name or "").strip()
        m = re.match(r"(\d{4}) (.+)", name)
        if not code or not m:
            continue
        ops.append((m.group(1), norm(m.group(2)), name, code))
    if not ops:
        sys.exit("в файле не нашлось строк «код · NNNN название»")
    ops.sort(key=lambda o: o[0])
    lines = [
        "/* Создано native/make_ops_1c.py из «1с опер.xlsx» (pidrpen/cursor). Руками не править. */",
        "typedef struct {",
        "  const wchar_t *num;  /* номер операции — Code справочника операций PLM */",
        "  const wchar_t *key;  /* название без номера для сравнения: строчные, ё→е */",
        "  const wchar_t *name; /* как в 1С — это и ложится в буфер */",
        "  const wchar_t *code; /* код элемента справочника 1С */",
        "} Op1c;",
        "",
        "static const Op1c kOps1c[] = {",
    ]
    lines += ["    {%s, %s, %s, %s}," % tuple(c_str(x) for x in o) for o in ops]
    lines += ["};", "#define OPS_1C_N ((int)(sizeof(kOps1c) / sizeof(kOps1c[0])))", ""]
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"{OUT}: {len(ops)} операций")


if __name__ == "__main__":
    main()
