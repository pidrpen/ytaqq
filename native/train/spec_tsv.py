"""Строки спецификации → TSV для окна «ЭСИ и ТП» (загрузчик стенда «В Союз», rowsBy = 3): на лист — файл,
строка таблицы — «Раздел<Tab>Поз.<Tab>Обозначение<Tab>Наименование<Tab>Кол.<Tab>Примечание» (UTF-8, без шапки),
как DrSpecRow без формата и зоны. Источник — разметка (labels.json), ответы модели (итог eval_model.py) или
выгрузка Tesseract (папка с *.spec_tess.json). Листы завода — только на ПК: выход — рядом с данными, не в репозиторий.

  python spec_tsv.py --labels data/new31/labels_new31.json --out data/new31/tsv_labels
  python spec_tsv.py --answers runs/new31_v2.json --out data/new31/tsv_model
  python spec_tsv.py --dump data/dump6 --out data/new31/tsv_tess"""
import argparse
import json
import pathlib

import cpvl

COLS = ('sect', 'pos', 'des', 'name', 'qty', 'note')


def tsv(rows):
    clean = lambda v: ' '.join(str(v or '').split())  # табуляция и переводы строк в графе — пробелом
    return ''.join('\t'.join(clean(r.get(c)) for c in COLS) + '\n' for r in rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--labels', help='разметка: листы с type: spec')
    src.add_argument('--answers', help='итог eval_model.py: ответы модели на вопрос spec')
    src.add_argument('--dump', help='выгрузка harness --drawrec: строки Tesseract (*.spec_tess.json)')
    ap.add_argument('--out', required=True, help='папка: <лист>.tsv')
    a = ap.parse_args()

    sheets = {}
    if a.labels:
        sheets = {k: v['rows'] for k, v in cpvl.load_labels(a.labels).items() if v.get('type') == 'spec'}
    elif a.answers:
        got = json.loads(pathlib.Path(a.answers).read_text('utf-8'))['sheets']
        sheets = {k: cpvl.spec_post(cpvl.spec_rows(v['got']['spec'])) for k, v in got.items() if v.get('got', {}).get('spec')}
    else:
        for f in sorted(pathlib.Path(a.dump).glob('*.spec_tess.json')):
            sheets[f.name[:-len('.spec_tess.json')]] = cpvl.spec_rows(f.read_text('utf-8-sig'))
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for k, rows in sheets.items():
        (out / f'{k}.tsv').write_bytes(tsv(rows).encode('utf-8'))  # LF, без BOM
    print(f'{len(sheets)} листов, {sum(len(r) for r in sheets.values())} строк → {out}')


if __name__ == '__main__':
    main()
