"""Черновик разметки новых листов в формате labels.json: что прочитали Tesseract и, если есть ответы
eval_model.py, модель. Черновик правят по картинкам и переносят в labels.json. Листы завода — только на
этом ПК: черновик кладётся рядом с данными (data/), не в репозиторий.

  python prelabel.py --dump data/dump_new --out data/labels_draft.json
  python prelabel.py --dump data/dump_new --answers runs/new_base.json --out data/labels_draft.json --eval-every 3

Уже размеченные листы (--labels) не трогаются. eval: true — каждый N-й лист каждого вида (спецификации
и чертежи отдельно): отложенные, на них не учат. Маршрут чертежа — пустой: его пишет технолог."""
import argparse
import json
import pathlib

import cpvl


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dump', required=True, help='выгрузка harness --drawrec с CP_DRVLDUMP')
    ap.add_argument('--out', required=True)
    ap.add_argument('--answers', help='итог eval_model.py по этой выгрузке: ответы модели')
    ap.add_argument('--labels', help='labels.json: эти листы уже размечены — пропустить')
    ap.add_argument('--eval-every', type=int, default=3, help='каждый N-й лист — в отложенные (0 — ни одного)')
    a = ap.parse_args()

    sheets = cpvl.load_sheets(a.dump)
    got = json.loads(pathlib.Path(a.answers).read_text('utf-8'))['sheets'] if a.answers else {}
    done = cpvl.load_labels(a.labels) if a.labels else {}
    out, seen = {}, {'spec': 0, 'draw': 0}
    for k, s in sheets.items():
        if k in done:
            continue
        g = got.get(k, {}).get('got', {})
        spec = s.get('spec_tess') is not None or 'spec' in g
        kind = 'spec' if spec else 'draw'
        seen[kind] += 1
        des = cpvl.des_fix_sb(cpvl.first_line(g.get('des')).translate(cpvl._LAT)) if g.get('des') else ''
        name, mat, _ = s['tess']
        lab = {'des': des, 'name': cpvl.first_line(g.get('name')) or name}
        if spec:
            lab = {'type': 'spec', **lab, 'rows': cpvl.spec_rows(g['spec'] if 'spec' in g else s['spec_tess'])}
        else:
            try:
                j = json.loads(g['sheet']) if g.get('sheet') else {}
            except ValueError:
                j = {}
            lab.update(material=cpvl.first_line(g.get('mat')) or mat, tt=j.get('tt') or [],
                       razvertka=bool(j.get('razvertka')), holes=j.get('holes') or [],
                       elements=cpvl.elements_of(j) if j else [], gabarit=cpvl.gabarit_of(j) or
                       {key: None for key in cpvl.GAB_KEYS}, route=['', ''])
        lab['eval'] = bool(a.eval_every) and seen[kind] % a.eval_every == 0
        lab['set'] = 'plant'
        out[k] = lab
    pathlib.Path(a.out).write_text(json.dumps(out, ensure_ascii=False, indent=1), 'utf-8')
    print(f'черновик: {len(out)} листов (спецификаций {sum(v.get("type") == "spec" for v in out.values())}, '
          f'отложено {sum(v["eval"] for v in out.values())}) → {a.out}')


if __name__ == '__main__':
    main()
