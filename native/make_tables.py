#!/usr/bin/env python3
"""Встроенные таблицы CursorPad: native/tables/<группа>/**/*.tsv → native/tables_data.h.

Группы — папки внутри native/tables:
  norms — нормативы ОМН, у пользователя в Документы\\CursorPad\\Нормативы (norms.c);
  std   — справочник стандартов, у пользователя в %USERPROFILE%\\CursorPad-std (std_files.c).
Таблицы едут внутри exe; tables_seed.c кладёт их в папку пользователя и обновляет с
выпусками, не затирая его правок и отметок сверки. Байты файлов — как есть.

  python native/make_tables.py          # пересобрать tables_data.h
  python native/make_tables.py --check  # сходится ли tables_data.h с таблицами (сборка, CI)
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE / 'tables'
OUT = HERE / 'tables_data.h'
ESC = {ord('\\'): b'\\\\', ord('"'): b'\\"', ord('?'): b'\\?', ord('\t'): b'\\t', ord('\r'): b'\\r',
       ord('\n'): b'\\n'}  # «\?» — без триграфов


def c_bytes(data):
    """строковые литералы C, по литералу на строку файла; не-ASCII — как есть (исходники в UTF-8)"""
    lines, cur = [], bytearray()
    for b in data:
        cur += ESC.get(b) or (b'\\%03o' % b if b < 0x20 or b == 0x7f else bytes([b]))
        if b == ord('\n'):
            lines.append(cur)
            cur = bytearray()
    if cur or not lines:
        lines.append(cur)
    return '\n'.join('     "' + bytes(x).decode('utf-8') + '"' for x in lines)


def c_wide(s):
    return 'L"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def render():
    rows = []
    groups = sorted(p for p in SRC.iterdir() if p.is_dir()) if SRC.is_dir() else []
    for group in groups:
        for f in sorted(group.rglob('*.tsv'), key=lambda p: p.relative_to(group).as_posix()):
            rel = f.relative_to(group).as_posix().replace('/', '\\')
            data = f.read_bytes()
            data.decode('utf-8')  # таблицы — только UTF-8
            rows.append(f'    {{{c_wide(group.name)}, {c_wide(rel)}, {len(data)}u,\n{c_bytes(data)}}},')
    head = ('/* Создано native/make_tables.py из native/tables — руками не править.\n'
            '   Встроенные таблицы: tables_seed.c кладёт их в папку пользователя. */\n')
    if not rows:
        body = '#define TABLES_COUNT 0\nstatic const BuiltinTable kTables[1];\n'
    else:
        body = (f'#define TABLES_COUNT {len(rows)}\nstatic const BuiltinTable kTables[] = {{\n'
                + '\n'.join(rows) + '\n};\n')
    return (head + body).encode('utf-8'), len(rows)


def main():
    text, n = render()
    if '--check' in sys.argv[1:]:
        if not OUT.exists() or OUT.read_bytes() != text:
            sys.exit('tables_data.h не сходится с native/tables: python native/make_tables.py')
        print(f'tables_data.h сходится с native/tables ({n} таблиц)')
        return
    OUT.write_bytes(text)
    print(f'{OUT.name}: {n} таблиц, {len(text) // 1024} КБ')


if __name__ == '__main__':
    main()
