"""Проверка модели с картинками на размеченных листах: чтение и маршрут, как в CursorPad.

  python eval_model.py --dump data/dump --labels data/labels.json --out runs/base.json
  python eval_model.py ... --lora runs/r1/draw-lora-qwen3.5-4b.gguf --out runs/r1.json
  python eval_model.py ... --preset qwen3-vl-4b            # другая модель из models.json
  python eval_model.py ... --model my.gguf --mmproj mm.gguf  # любая своя
  python eval_model.py ... --tess-only                      # только Tesseract, без модели
  python eval_model.py ... --answers runs/base.json         # пересчитать без модели

По умолчанию — только отложенные листы (eval: true в разметке): на них не учат."""
import argparse
import json
from collections import Counter
import pathlib
import sys
import time

import cpvl


def score_sheet(lab, got, tess, tn):
    r = {}
    if lab is None:
        return r
    if got.get('name') is not None:
        r['name_ok'] = cpvl.norm(got['name']) == cpvl.norm(lab['name'])
    if got.get('mat') is not None:
        r['mat_ok'] = cpvl.norm(got['mat']) == cpvl.norm(lab['material'])
    if got.get('sheet') is not None:
        try:
            j = json.loads(got['sheet'])
        except ValueError:
            j = {'tt': [], 'razvertka': None, 'holes': []}
        tt = j.get('tt') or []
        sims = [max((cpvl.similar(a, b) for b in tt), default=0) for a in lab['tt']]
        r['tt_sim'] = round(sum(sims) / len(sims), 3) if sims else (1.0 if not tt else 0.0)
        r['tt_n_ok'] = len(tt) == len(lab['tt'])
        r['razv_ok'] = j.get('razvertka') == lab['razvertka']
        r['holes_ok'] = sorted(map(cpvl.norm, j.get('holes') or [])) == sorted(map(cpvl.norm, lab['holes']))
        if 'elements' in lab and 'elements' in j:
            # типы элементов: сколько верно, лишних, пропущено — в наборе второго поколения (без отверстий
            # по оси, уступов и карманов), чтобы поколения сравнивались честно
            got_t = Counter(e['type'] for e in cpvl.gen2_elements(cpvl.elements_of(j)))
            want_t = Counter(e['type'] for e in cpvl.gen2_elements(lab['elements']))
            r['el_tp'] = sum((got_t & want_t).values())
            r['el_fp'] = sum((got_t - want_t).values())
            r['el_fn'] = sum((want_t - got_t).values())
            r['el_ok'] = got_t == want_t
            r['el_got'] = sorted(got_t.elements())
            # отверстия с тем, где они (по оси, поперёк, в торце): пары тип + где, по числу выносок
            hw = lambda els: Counter((e['type'], e.get('where', '')) for e in els if e['type'] in cpvl.HOLE_TYPES)
            gh, wh = hw(cpvl.elements_of(j)), hw(lab['elements'])
            r['ho_tp'], r['ho_fp'], r['ho_fn'] = sum((gh & wh).values()), sum((gh - wh).values()), sum((wh - gh).values())
            # выдуманные размеры: размер у элемента модели, какого нет ни у одного элемента того же типа на листе
            want_sz = {(e['type'], cpvl.norm(e['size'])) for e in lab['elements']}
            r['sz_fp'] = sum(1 for e in cpvl.elements_of(j)
                             if cpvl.norm(e.get('size', '')) and (e['type'], cpvl.norm(e['size'])) not in want_sz)
        if 'gabarit' in lab:  # габариты: верно, неверно, выдумано (на листе нет), пропущено (есть, а не названо)
            g = cpvl.gabarit_of(j) or {}
            for key in ('gb_ok', 'gb_bad', 'gb_fp', 'gb_fn', 'gb_n'):
                r[key] = 0
            for k in cpvl.GAB_KEYS:
                w, h = lab['gabarit'].get(k), g.get(k)
                r['gb_n'] += w is not None
                if w is not None and h is not None:
                    r['gb_ok' if abs(w - h) < 0.01 else 'gb_bad'] += 1
                elif h is not None:
                    r['gb_fp'] += 1
                elif w is not None:
                    r['gb_fn'] += 1
    name, mat, text = cpvl.route_input(tess, cpvl.first_line(got.get('name')), cpvl.first_line(got.get('mat')),
                                       cpvl.sheet_text(got.get('sheet')))
    cls, ops, blank = cpvl.route(tn, name, mat, text)
    want = set(lab['route'][1].split())
    have = cpvl.cats(ops)
    r.update(cls=cls, cls_ok=cls == lab['route'][0], ops=sorted(have), route_ok=have == want,
             tp=len(have & want), fp=len(have - want), fn=len(want - have), blank=blank)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dump', required=True, help='выгрузка harness --drawrec с CP_DRVLDUMP')
    ap.add_argument('--labels', required=True)
    ap.add_argument('--out', required=True, help='итог JSON')
    ap.add_argument('--preset', default='qwen3.5-4b')
    ap.add_argument('--model'), ap.add_argument('--mmproj')
    ap.add_argument('--lora', help='адаптер GGUF (draw-lora*.gguf)')
    ap.add_argument('--server', help='llama-server.exe (по умолчанию — CursorPad)')
    ap.add_argument('--tn', default='tn_cli.exe', help='tn_cli.exe, собранный из tn_cli.c (README.md)')
    ap.add_argument('--split', choices=['eval', 'train', 'all'], default='eval')
    ap.add_argument('--tess-only', action='store_true')
    ap.add_argument('--answers', help='взять ответы модели из прежнего итога')
    ap.add_argument('--limit', type=int, default=0, help='для пробы: первые N листов')
    a = ap.parse_args()

    labels = cpvl.load_labels(a.labels)
    sheets = cpvl.load_sheets(a.dump)
    prompts = cpvl.load_prompts(a.dump)
    keep = [k for k in sheets if k in labels and (a.split == 'all' or labels[k].get('eval', False) == (a.split == 'eval'))]
    if a.limit:
        keep = keep[:a.limit]
    if not keep:
        sys.exit('нет размеченных листов в выгрузке')
    old = json.loads(pathlib.Path(a.answers).read_text('utf-8'))['sheets'] if a.answers else {}

    answers = {k: dict(old.get(k, {}).get('got', {})) for k in keep}
    if not a.tess_only and not a.answers:
        p = cpvl.load_presets().get(a.preset, {})
        model = a.model or cpvl.models_dir() / p['dir'] / p['gguf']
        mmproj = a.mmproj or cpvl.models_dir() / p['dir'] / p['mmproj']
        log = pathlib.Path(a.out).with_suffix('.server.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        with cpvl.Server(model, mmproj, p.get('server', []), a.lora, a.server, log=log) as srv:
            for k in keep:
                for tag in cpvl.TAGS:
                    im = sheets[k][tag]
                    if im is None or tag not in prompts:
                        continue
                    t = time.time()
                    q = prompts[tag]
                    answers[k][tag] = srv.ask(im, q['q'], q['schema'], q['max_tokens'])
                    print(f'{k[:34]:34} {tag:5} {time.time() - t:5.1f} с  {answers[k][tag][:70]!r}', flush=True)
    if a.tess_only:
        answers = {k: {} for k in keep}

    res = {}
    for k in keep:
        res[k] = {'got': answers[k], **score_sheet(labels[k], answers[k], sheets[k]['tess'], a.tn)}
    n = len(res)

    def cnt(key):
        v = [r[key] for r in res.values() if key in r]
        return f'{sum(v)}/{len(v)}' if v else '—'

    tts = [r['tt_sim'] for r in res.values() if 'tt_sim' in r]
    summary = {
        'листов': n,
        'маршрут целиком': cnt('route_ok'), 'вид детали': cnt('cls_ok'),
        'операций верных': sum(r['tp'] for r in res.values()), 'лишних': sum(r['fp'] for r in res.values()),
        'пропущено': sum(r['fn'] for r in res.values()),
        'наименование': cnt('name_ok'), 'материал': cnt('mat_ok'),
        'ТТ сходство': round(sum(tts) / len(tts), 3) if tts else '—', 'ТТ число пунктов': cnt('tt_n_ok'),
        'развертка': cnt('razv_ok'), 'отверстия': cnt('holes_ok'),
    }
    if any('el_tp' in r for r in res.values()):
        tot = lambda key: sum(r.get(key, 0) for r in res.values())
        summary.update({'элементы целиком': cnt('el_ok'), 'элементов верных': tot('el_tp'), 'выдуманных': tot('el_fp'),
                        'пропущенных': tot('el_fn'),
                        'отверстия с местом: верных': tot('ho_tp'), 'отверстий лишних': tot('ho_fp'),
                        'отверстий не названо': tot('ho_fn'), 'выдуманных размеров': tot('sz_fp')})
    if any('gb_n' in r for r in res.values()):
        tot = lambda key: sum(r.get(key, 0) for r in res.values())
        summary.update({'габаритов проставлено': tot('gb_n'), 'габаритов верно': tot('gb_ok'), 'габаритов неверно': tot('gb_bad'),
                        'габаритов выдумано': tot('gb_fp'), 'габаритов не прочитано': tot('gb_fn')})
    for k, r in res.items():
        lab = labels[k]
        miss = set(lab['route'][1].split()) - set(r['ops'])
        extra = set(r['ops']) - set(lab['route'][1].split())
        print(f'{"OK" if r["route_ok"] else "--"} {k[:34]:34} {r["cls"]:6} ждали {lab["route"][0]:6} '
              f'лишнее {" ".join(sorted(extra)) or "-":12} нет {" ".join(sorted(miss)) or "-":12} '
              f'{"имя" if r.get("name_ok") else "---"} {"мат" if r.get("mat_ok") else "---"} '
              f'ТТ {r.get("tt_sim", "-")}' + (f' эл. {r["el_tp"]}/{r["el_fp"]}/{r["el_fn"]} {",".join(r["el_got"])}'
                                              if 'el_tp' in r else ''))
    print(json.dumps(summary, ensure_ascii=False))
    pathlib.Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(a.out).write_text(json.dumps({'summary': summary, 'args': vars(a), 'sheets': res},
                                              ensure_ascii=False, indent=1), 'utf-8')


if __name__ == '__main__':
    main()
