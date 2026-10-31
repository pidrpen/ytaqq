"""Проверка модели с картинками на размеченных листах: чтение и маршрут, как в CursorPad.

  python eval_model.py --dump data/dump --labels data/labels.json --out runs/base.json
  python eval_model.py ... --lora runs/r1/draw-lora-qwen3.5-4b.gguf --out runs/r1.json
  python eval_model.py ... --preset qwen3-vl-4b            # другая модель из models.json
  python eval_model.py ... --model my.gguf --mmproj mm.gguf  # любая своя
  python eval_model.py ... --tess-only                      # только Tesseract, без модели
  python eval_model.py ... --answers runs/base.json         # пересчитать без модели
  python eval_model.py --dump data/dump2 ... --lora draw-lora2.gguf --gab-lora draw-gab-lora.gguf --gab-dump data/dump3
      # как CursorPad с адаптером габаритов: лист — адаптером чертежей, габариты — вторым вопросом

По умолчанию — только отложенные листы (eval: true в разметке): на них не учат."""
import argparse
import json
from collections import Counter
import pathlib
import sys
import time

import cpvl


def score_des(r, lab, got):
    """графа «Обозначение» моделью: как прочитано и после поправки кодом (dr_des_fix_sb), отдельно — «СБ»:
    по нему сборочный чертёж отличают от спецификации"""
    if not lab.get('des') or got.get('des') is None:
        return
    raw = cpvl.first_line(got['des'])
    fixed = cpvl.des_fix_sb(raw.translate(cpvl._LAT))
    want = cpvl.des_key(lab['des'])
    r['des_ok'], r['des_fix_ok'] = cpvl.des_key(raw) == want, cpvl.des_key(fixed) == want
    if cpvl.has_sb(lab['des']):
        r['sb_n'], r['sb_ok'], r['sb_fix_ok'] = 1, cpvl.has_sb(raw), cpvl.has_sb(fixed)
    elif cpvl.has_sb(fixed):
        r['sb_fp'] = 1


def score_sheet(lab, got, tess, tn, spec_tess=None):
    r = {}
    if lab is None:
        return r
    score_des(r, lab, got)
    if lab.get('type') == 'spec':  # спецификация: только строки (маршрута у неё нет)
        if got.get('spec') is not None:
            r['spec'] = cpvl.score_spec(lab['rows'], cpvl.spec_post(cpvl.spec_rows(got['spec'])))  # как разбирает CursorPad
        if spec_tess is not None:
            r['spec_tess'] = cpvl.score_spec(lab['rows'], cpvl.spec_rows(spec_tess))
        return r
    if 'route' not in lab:  # размечено только обозначение (новые листы): маршрута не считаем
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
    ap.add_argument('--gab-lora', help='адаптер габаритов (draw-gab…): второй вопрос по листу, из ответа — только gabarit')
    ap.add_argument('--gab-dump', help='выгрузка с вопросом третьего поколения (data/dump3) для --gab-lora')
    ap.add_argument('--server', help='llama-server.exe (по умолчанию — CursorPad)')
    ap.add_argument('--tn', default='tn_cli.exe', help='tn_cli.exe, собранный из tn_cli.c (README.md)')
    ap.add_argument('--split', choices=['eval', 'train', 'all'], default='eval')
    ap.add_argument('--tess-only', action='store_true')
    ap.add_argument('--answers', help='взять ответы модели из прежнего итога')
    ap.add_argument('--limit', type=int, default=0, help='для пробы: первые N листов')
    ap.add_argument('--tags', help='спрашивать только эти вопросы, через запятую (например des,spec): меньше видеокарты')
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
        loras = [a.lora, a.gab_lora] if a.gab_lora else a.lora
        gabq = cpvl.load_prompts(a.gab_dump)['sheet'] if a.gab_lora else None
        with cpvl.Server(model, mmproj, p.get('server', []), loras, a.server, log=log) as srv:
            for k in keep:
                for tag in cpvl.TAGS:
                    im = sheets[k][tag]
                    if im is None or tag not in prompts or (a.tags and tag not in a.tags.split(',')):
                        continue
                    t = time.time()
                    q = prompts[tag]
                    answers[k][tag] = srv.ask(im, q['q'], q['schema'], q['max_tokens'])
                    print(f'{k[:34]:34} {tag:5} {time.time() - t:5.1f} с  {answers[k][tag][:70]!r}', flush=True)
                if gabq and sheets[k].get('sheet') is not None and answers[k].get('sheet'):
                    t = time.time()  # как drawrec.c: второй вопрос адаптеру габаритов, берём только gabarit
                    g = srv.ask(sheets[k]['sheet'], gabq['q'], gabq['schema'], gabq['max_tokens'], lora_id=1)
                    try:
                        j, gj = json.loads(answers[k]['sheet']), json.loads(g)
                        j['gabarit'] = gj.get('gabarit')
                        answers[k]['sheet'] = json.dumps(j, ensure_ascii=False)
                    except ValueError:
                        pass
                    print(f'{k[:34]:34} gab   {time.time() - t:5.1f} с  {g[:70]!r}', flush=True)
    if a.tess_only:
        answers = {k: {} for k in keep}

    res = {}
    for k in keep:
        res[k] = {'got': answers[k], **score_sheet(labels[k], answers[k], sheets[k]['tess'], a.tn,
                                                   sheets[k].get('spec_tess'))}
    n = len(res)
    draw = {k: r for k, r in res.items() if 'route' in labels[k]}  # с маршрутом в разметке

    def cnt(key):
        v = [r[key] for r in res.values() if key in r]
        return f'{sum(v)}/{len(v)}' if v else '—'

    tts = [r['tt_sim'] for r in res.values() if 'tt_sim' in r]
    summary = {
        'листов': n,
        'маршрут целиком': cnt('route_ok'), 'вид детали': cnt('cls_ok'),
        'операций верных': sum(r['tp'] for r in draw.values()), 'лишних': sum(r['fp'] for r in draw.values()),
        'пропущено': sum(r['fn'] for r in draw.values()),
        'наименование': cnt('name_ok'), 'материал': cnt('mat_ok'),
        'ТТ сходство': round(sum(tts) / len(tts), 3) if tts else '—', 'ТТ число пунктов': cnt('tt_n_ok'),
        'развертка': cnt('razv_ok'), 'отверстия': cnt('holes_ok'),
    }
    if any('des_ok' in r for r in res.values()):
        summary.update({'обозначение верно': cnt('des_ok'), 'обозначение верно после поправки': cnt('des_fix_ok'),
                        'СБ в обозначении верно': cnt('sb_ok'), 'СБ после поправки': cnt('sb_fix_ok'),
                        'СБ лишнее': sum(r.get('sb_fp', 0) for r in res.values())})
    for key, who in (('spec', 'модель'), ('spec_tess', 'Tesseract')):  # строки спецификации
        sp = [r[key] for r in res.values() if key in r]
        if not sp:
            continue
        t = lambda f: sum(s[f] for s in sp)
        summary[f'спецификация, {who}'] = {
            'листов': len(sp), 'строк в разметке': t('n'), 'верно целиком': t('ok'),
            'обозначений верно': f"{t('des_ok')}/{t('des_n')}", 'количеств верно': t('qty_ok'),
            'разделов верно': t('sect_ok'), 'позиций верно': t('pos_ok'),
            'наименования сходство': round(t('sim') / max(1, t('n') - t('fn')), 3),
            'выдумано строк': t('fp'), 'пропущено строк': t('fn')}
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
        if lab.get('type') == 'spec':
            line = lambda s: f"{s['ok']}/{s['n']} целиком, обозн. {s['des_ok']}/{s['des_n']}, +{s['fp']} −{s['fn']}"
            print(f'СП {k[:34]:34} модель {line(r["spec"]) if "spec" in r else "—":34} '
                  f'Tesseract {line(r["spec_tess"]) if "spec_tess" in r else "—"}')
            continue
        if 'route' not in lab:
            continue
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
