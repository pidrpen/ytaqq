"""Учебные листы спецификации (ГОСТ 2.106) для дообучения модели с картинками: читать строки
таблицы — раздел, формат, зону, позицию, обозначение, наименование (с переносами), количество,
примечание. Лист — A4 книжный, рамка, таблица шириной 185 мм вплотную к правой рамке, штамп
формы 2 (первый лист) или 2а (следующие) по ГОСТ 2.104, шрифт ГОСТ А/Б или «от руки», зерно и
ступеньки скана. Модели отдаётся вырезка таблицы так, как её режет CursorPad (drawrec.c dr_spec):
от левого края штампа до правой рамки, от верхней линии таблицы до верхней линии штампа, потом
целое уменьшение до 3000 по длинной стороне (dr_img_part, DR_SPEC_SIDE). Тексты и обозначения придуманы
(стандартные изделия — по открытым ГОСТ), заводских обозначений здесь нет.

  python synth_specs.py --out data/specs --n 400 [--seed 1]
Выход: img/<id>_p<лист>.png — вырезка для модели; sheets/<id>_p<лист>.png — лист целиком (для
проверки CursorPad и Tesseract); specs.jsonl — на лист {image, tag='spec', answer — JSON строк};
labels.json — разметка листов, ключ — имя файла листа с расширением (тип, обозначение и наименование из штампа, строки)."""
import argparse
import json
import pathlib
import random

from PIL import Image, ImageChops, ImageDraw

from synth import (GOST, HAND, NOUNS, PROFILES, fit_font, font, mat_answer, mat_parts, name_text, put,
                   scribble, text_w, to_scan, wrap)

COLS = [0, 6, 12, 20, 90, 153, 163, 185]  # столбцы таблицы, мм от её левого края (kDrSpecX в drawrec.c)
FIELDS = ['fmt', 'zone', 'pos', 'des', 'name', 'qty', 'note']  # поля строки, по столбцам
HEAD = ['Формат', 'Зона', 'Поз.', 'Обозначение', 'Наименование', 'Кол.', 'Примечание']
CENTER = (0, 1, 2, 5)  # столбцы, где текст по центру; остальные — от левого края
ALPHA = 'АБВГДЕЖИКЛМНПРСТУФХЦЧШЭЮЯ'
SURN = ['Иванов', 'Петров', 'Сидоров', 'Смирнов', 'Кузнецов', 'Попов', 'Волков', 'Орлов', 'Белов', 'Тихонов']
ORGS = ['ООО Пример', 'ООО «Пример»', 'АО Образец', 'ЗАО Пример-Маш', 'ООО Макет', 'АО «Образец-М»']
SPEC_SIDE = 3000  # длинная сторона вырезки для модели (drawrec.c DR_SPEC_SIDE): 200 dpi и первый лист 300 dpi — без уменьшения
_capk = {}


def capk(face):
    """высота прописной в долях кегля — по ней задаётся размер текста в мм"""
    if face not in _capk:
        bb = font(face, 100).getbbox('Ж')
        _capk[face] = (bb[3] - bb[1]) / 100
    return _capk[face]


def paste_dark(im, tmp, x, y):
    x, y = int(x), int(y)
    box = (max(0, x), max(0, y), min(im.size[0], x + tmp.size[0]), min(im.size[1], y + tmp.size[1]))
    if box[2] <= box[0] or box[3] <= box[1]:
        return
    part = tmp.crop((box[0] - x, box[1] - y, box[2] - x, box[3] - y))
    im.paste(ImageChops.darker(im.crop(box), part), box)


def put_rot(im, xy, s, f, stroke, ang):
    """текст с поворотом на ang градусов вокруг начала строки — буквы «от руки» чуть вкось"""
    w, h = f.getbbox(s, stroke_width=stroke)[2:]
    pad = int(h * 0.5) + 4
    tmp = Image.new('L', (w + 2 * pad, h + 2 * pad), 255)
    ImageDraw.Draw(tmp).text((pad, pad), s, font=f, fill=0, stroke_width=stroke, stroke_fill=0)
    tmp = tmp.rotate(ang, resample=Image.BILINEAR, fillcolor=255, center=(pad, pad + h // 2))
    paste_dark(im, tmp, xy[0] - pad, xy[1] - pad)


# ---- обозначения ----------------------------------------------------------------------
def code_letters(r, n):
    """буквенный код: подряд идущие буквы алфавита («ЖИКЛ») — заведомо выдуманный"""
    k = r.randint(0, len(ALPHA) - n)
    return ALPHA[k:k + n]


class Des:
    """обозначения: сборка A (без «СБ»), её сборочные единицы и детали; три манеры записи"""

    def __init__(self, r):
        self.r = r
        self.style = r.choices(['class', 'klm', 'sb'], [45, 35, 20])[0]
        if self.style == 'class':  # АБВГ.301125.001
            self.L = code_letters(r, 4)
            self.six = f'{r.randint(30, 46)}{r.randint(10, 99)}{r.randint(10, 99)}'
            self.six2 = f'{r.randint(71, 75)}{r.randint(10, 99)}{r.randint(10, 99)}' if r.random() < 0.25 else self.six
            self.hund = r.random() < 0.4  # узлы .100, .200…, детали .001…; иначе подряд после A
            self.base = 0 if self.hund else r.choice([1, 1, 1, 2, 5])
            self.A = f'{self.L}.{self.six}.{self.base:03d}'
        elif self.style == 'klm':  # КЛМ 1234-567.01.02
            self.A = f'{code_letters(r, 3)} {r.randint(1000, 9999)}-{r.randint(1, 999)}.{r.randint(1, 9):02d}'
            if r.random() < 0.5:
                self.A += f'.{r.randint(1, 9):02d}'
        else:  # 321-23-сб32
            self.a, self.b, self.c = r.randint(100, 999), r.randint(10, 99), r.randint(1, 60)
            self.A = f'{self.a}-{self.b}-сб{self.c}'
        self.reset()

    def reset(self):
        self.nu = self.np = self.seq = 0

    def _skip(self):
        return 2 if self.r.random() < 0.12 else 1  # иногда номер пропущен

    def unit(self):
        self.nu += 1
        if self.style == 'class':
            if self.hund:
                return f'{self.L}.{self.six}.{self.nu * 100:03d}'
            self.seq += self._skip()
            return f'{self.L}.{self.six}.{self.base + self.seq:03d}'
        if self.style == 'klm':
            self.seq += self._skip()
            return f'{self.A}.{self.seq:02d}'
        return f'{self.a}-{self.b}-сб{self.c + self.nu}'

    def part(self):
        self.np += 1
        if self.style == 'class':
            if self.hund:
                return f'{self.L}.{self.six2}.{self.np:03d}'
            self.seq += self._skip()
            return f'{self.L}.{self.six2}.{self.base + self.seq:03d}'
        if self.style == 'klm':
            self.seq += self._skip()
            return f'{self.A}.{self.seq:02d}'
        self.seq += self._skip()
        return f'{self.a}-{self.b}-{self.seq:02d}'


# ---- наименования и строки ------------------------------------------------------------
ASM_TAIL = {'Узел': ['натяжной', 'поворотный', 'опорный', 'подачи', 'крепления', 'привода', 'зажима', 'фиксации'],
            'Привод': ['конвейера', 'подачи', 'поворота', 'насоса', 'вентилятора', 'заслонки', 'шнека', 'стола'],
            'Редуктор': ['червячный', 'цилиндрический', 'конический', 'планетарный', 'двухступенчатый'],
            'Механизм': ['подъёма', 'зажима', 'фиксации', 'блокировки', 'подачи', 'перемещения', 'прижима'],
            'Блок': ['направляющий', 'подшипниковый', 'зажимной', 'поворотный', 'приводной']}


def asm_name(r):
    if r.random() < 0.2:
        return r.choice(['Крышка', 'Корпус', 'Вал', 'Опора', 'Стойка', 'Рама', 'Ролик', 'Захват', 'Фланец']) + ' в сборе'
    w = r.choice(list(ASM_TAIL))
    return f'{w} {r.choice(ASM_TAIL[w])}'


def part_name(r):
    k = r.random()
    if k < 0.5:
        return r.choice(NOUNS)
    if k < 0.75:
        return name_text(r)
    return f'{r.choice(NOUNS)} {r.choice(PROFILES)}'


def item(fmt='', zone='', pos='', des='', name='', qty='', note=''):
    return {'fmt': fmt, 'zone': zone, 'pos': pos, 'des': des, 'name': name, 'qty': qty, 'note': note}


def zone_of(r):
    return f'{r.choice("АБВГ")}{r.randint(1, 6)}' if r.random() < 0.03 else ''


def note_of(r, p=0.07):
    if r.random() >= p:
        return ''
    return r.choice(['*', '*', 'Заказ', 'Справ.', 'Покупн.', f'Поз. {r.randint(1, 30)} — комплект', 'Комплект'])


LENS = [10, 12, 14, 16, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80, 90, 100, 120]


def std_name(r):
    """стандартное изделие по открытому ГОСТ — с разными размерами"""
    d = r.choice([6, 8, 8, 10, 10, 12, 16, 20])
    L = r.choice([x for x in LENS if 1.5 * d <= x <= 10 * d])
    cls = r.choice(['58', '88', '109'])
    coat = r.choice(['.019', '.016', '.05', '.019', ''])
    t = r.choices(['bolt', 'nut', 'wash', 'sw', 'screw', 'pin', 'key', 'cot', 'stud'], [20, 16, 8, 12, 14, 7, 6, 3, 5])[0]
    if t == 'bolt':
        return r.choice([f'Болт М{d}-6g×{L}.{cls}{coat} ГОСТ 7798-70', f'Болт М{d}-6g×{L}.{cls} ГОСТ 7805-70'])
    if t == 'nut':
        return r.choice([f'Гайка М{d}-6Н.{r.choice(["5", "8", "10"])}{coat} ГОСТ 5915-70', f'Гайка М{d}-6Н.5 ГОСТ 5927-70'])
    if t == 'wash':
        return r.choice([f'Шайба {d}.01{coat or ".019"} ГОСТ 11371-78', f'Шайба {d}.01.019 ГОСТ 6958-78'])
    if t == 'sw':
        return f'Шайба {d} 65Г 019 ГОСТ 6402-70'
    if t == 'screw':
        d = r.choice([3, 4, 5, 6, 8, 10])
        L = r.choice([x for x in LENS if 2 * d <= x <= 8 * d] or [20])
        return r.choice([f'Винт А.М{d}-6g×{L}.{cls}{coat or ".016"} ГОСТ 1491-80', f'Винт М{d}-6g×{L}.{cls} ГОСТ 11738-84',
                         f'Винт А.М{d}-6g×{L}.58 ГОСТ 17473-80'])
    if t == 'pin':
        return f'Штифт {r.choice([3, 4, 5, 6, 8, 10, 12])}×{r.choice([x for x in LENS if x >= 10 and x <= 90])} ГОСТ 3128-70'
    if t == 'key':
        b, h = r.choice([(4, 4), (5, 5), (6, 6), (8, 7), (10, 8), (12, 8), (14, 9), (16, 10), (18, 11)])
        return f'Шпонка {b}×{h}×{r.choice([x for x in LENS if x >= 2.5 * b])} ГОСТ 23360-78'
    if t == 'cot':
        return f'Шплинт {r.choice(["2", "3,2", "4", "5"])}×{r.choice([16, 20, 25, 32, 40])}{coat or ".019"} ГОСТ 397-79'
    return f'Шпилька М{d}-6g×{L}.{cls} ГОСТ 22032-76'


def other_name(r):
    t = r.choices(['brg', 'brg2', 'seal', 'ring', 'chain', 'belt', 'motor'], [30, 8, 18, 18, 6, 8, 6])[0]
    if t == 'brg':
        return f'Подшипник {r.choice([202, 203, 204, 205, 206, 207, 208, 209, 210, 304, 305, 306, 307])} ГОСТ 8338-75'
    if t == 'brg2':
        return f'Подшипник {r.choice([7204, 7205, 7206, 7207, 7208, 7305])} ГОСТ 27365-87'
    if t == 'seal':
        d, D = r.choice([(20, 40), (25, 42), (30, 52), (35, 58), (40, 60), (45, 65), (50, 70), (55, 80), (60, 85)])
        return f'Манжета 1.1-{d}×{D}-{r.choice(["1", "2"])} ГОСТ 8752-79'
    if t == 'ring':
        a, cs = r.choice([16, 20, 25, 30, 36, 40, 45, 50, 60]), r.choice([19, 25, 30, 36, 46])
        return f'Кольцо {a:03d}-{a + round(cs / 5):03d}-{cs}-2-2 ГОСТ 9833-73'
    if t == 'chain':
        return f'Цепь ПР-{r.choice(["12,7-900", "15,875-2300", "19,05-3180"])}-1 ГОСТ 13568-97'
    if t == 'belt':
        return f'Ремень {r.choice(["А", "Б", "В"])}-{r.choice([800, 900, 1000, 1120, 1250, 1400, 1600])} ГОСТ 1284.1-89'
    return f'Электродвигатель 4А{r.choice([71, 80, 90, 100])}{r.choice("АВ")}{r.choice([2, 4, 6])}У3 ГОСТ 19523-81'


def material(r):
    """(наименование, единица) — для раздела «Материалы»"""
    k = r.randrange(10)
    if k == 0:
        return mat_answer(mat_parts(r)), 'кг'
    if k == 1:
        return f'Проволока {r.choice(["0,8", "1,0", "1,2", "2,0", "3,0"])}-О-Ч ГОСТ 3282-74', 'кг'
    if k == 2:
        return f'Эмаль ПФ-115 {r.choice(["серая", "красная", "зелёная", "синяя", "белая", "чёрная"])} ГОСТ 6465-76', 'кг'
    if k == 3:
        return r.choice(['Смазка Литол-24 ГОСТ 21150-87', 'Смазка ЦИАТИМ-201 ГОСТ 6267-74']), 'кг'
    if k == 4:
        return r.choice(['Грунтовка ГФ-021 серая ГОСТ 25129-82', 'Лак БТ-577 ГОСТ 5631-79']), 'кг'
    if k == 5:
        return r.choice(['Масло И-20А ГОСТ 20799-88', 'Масло И-40А ГОСТ 20799-88']), 'л'
    if k == 6:
        return f'Паронит ПМБ {r.choice(["1,0", "1,5", "2,0"])} ГОСТ 481-80', 'кг'
    if k == 7:
        return r.choice(['Шнур асбестовый ШАОН-6 ГОСТ 1779-83', 'Лента изоляционная ПВХ ГОСТ 16214-86']), 'м'
    if k == 8:
        return f'Провод ПВ-3 {r.choice(["0,75", "1,5", "2,5"])} ГОСТ 6323-79', 'м'
    return 'Припой ПОС 61 ГОСТ 21931-76', 'кг'


# ---- содержание -----------------------------------------------------------------------
def build_sections(r, ds, want, tr):
    """разделы спецификации по порядку ГОСТ 2.106: [{name, pos (есть ли позиции), items}]; want — желаемое
    число строк таблицы (с заголовками и пустыми строками)"""
    ds.reset()
    secs = []
    if r.random() < 0.95:
        d = [item(fmt=r.choices(['А1', 'А2', 'А3', 'А4'], [20, 30, 35, 15])[0], name='Сборочный чертёж',
                  # код «СБ» — в конце, после номера; у заводской манеры «321-23-сб32» «сб» — часть номера
                  # самой спецификации, её сборочный чертёж — «321-23-сб32СБ» (пользователь, 29.10)
                  des=f'{ds.A}СБ' if ds.style == 'sb' or r.random() < 0.5 else f'{ds.A} СБ')]
        if r.random() < 0.12:
            d.append(item(fmt=r.choice(['А3', 'А4']), des=f'{ds.A} Э3', name='Схема электрическая принципиальная'))
        if r.random() < 0.10:
            d.append(item(fmt='А4', des=f'{ds.A} ПЗ', name='Пояснительная записка'))
        secs.append({'name': 'Документация', 'pos': False, 'items': d})
    flags = {'units': r.random() < 0.55, 'parts': r.random() < 0.9, 'std': r.random() < 0.9,
             'other': r.random() < 0.4, 'mat': r.random() < 0.4}
    if sum(flags.values()) < 2:
        flags['parts'] = flags['std'] = True
    gap = (1 if tr['gap'] in ('both', 'before') else 0) + (1 if tr['gap'] in ('both', 'after') else 0)
    used = sum(len(s['items']) * 1.5 for s in secs) + (1 + gap) * (sum(flags.values()) + len(secs))
    budget = max(3.0, want - used)
    wgt = {'units': 1, 'parts': 3, 'std': 3, 'other': 1, 'mat': 1}
    lpi = {'units': 1.15, 'parts': 1.2, 'std': 1.8, 'other': 1.7, 'mat': 1.4}
    tot = sum(wgt[k] for k in flags if flags[k])
    cnt = {k: max(1, round(budget * wgt[k] / tot / lpi[k] * r.uniform(0.7, 1.3))) for k in flags if flags[k]}
    if r.random() < 0.03:
        secs.append({'name': 'Комплексы', 'pos': True, 'items': [item(fmt='А4', des=ds.unit(), name='Комплекс ' + r.choice(
            ['управления', 'контроля', 'измерительный']), qty='1')]})
    if flags['units']:
        secs.append({'name': 'Сборочные единицы', 'pos': True, 'items': [
            item(fmt=r.choices(['А3', 'А4', 'А2', 'А1', 'БЧ'], [30, 40, 12, 4, 6])[0], zone=zone_of(r), des=ds.unit(),
                 name=asm_name(r), qty=str(r.choice([1, 1, 1, 1, 2, 3, 4])), note=note_of(r, 0.04))
            for _ in range(cnt['units'])]})
    if flags['parts']:
        secs.append({'name': 'Детали', 'pos': True, 'items': [
            item(fmt=r.choices(['А4', 'А3', 'А2', 'А1', 'БЧ', 'А4х3', 'А3х3'], [40, 22, 4, 2, 13, 1, 1])[0], zone=zone_of(r),
                 des=ds.part(), name=part_name(r), qty=str(r.choice([1, 1, 1, 2, 2, 3, 4, 6, 8])), note=note_of(r))
            for _ in range(cnt['parts'])]})
    if flags['std']:
        names = []
        for _ in range(cnt['std'] * 3):
            n = std_name(r)
            if n not in names:
                names.append(n)
            if len(names) >= cnt['std']:
                break
        secs.append({'name': 'Стандартные изделия', 'pos': True, 'items': [
            item(zone=zone_of(r), name=n, qty=str(r.choice([1, 2, 2, 3, 4, 4, 6, 8, 8, 10, 12, 16, 24])), note=note_of(r, 0.02))
            for n in names]})
    if flags['other']:
        names = []
        for _ in range(cnt['other'] * 3):
            n = other_name(r)
            if n not in names:
                names.append(n)
            if len(names) >= cnt['other']:
                break
        secs.append({'name': 'Прочие изделия', 'pos': True, 'items': [
            item(zone=zone_of(r), name=n, qty=str(r.choice([1, 1, 2, 2, 3, 4, 6])), note=note_of(r, 0.03)) for n in names]})
    if flags['mat']:
        its = []
        for _ in range(cnt['mat']):
            n, unit = material(r)
            if r.random() < 0.25:
                its.append(item(name=n, note=r.choice(['по месту', 'по потребности'])))
            else:
                q = r.choice(['0,02', '0,05', '0,1', '0,2', '0,25', '0,5', '1,2', '0,15', '2,5']) if unit != 'м' else \
                    r.choice(['0,5', '1', '1,5', '2', '3', '5', '10'])
                its.append(item(name=n, qty=q, note=unit))
        secs.append({'name': 'Материалы', 'pos': r.random() < 0.5, 'items': its})
    if r.random() < 0.04:
        secs.append({'name': 'Комплекты', 'pos': r.random() < 0.4, 'items': [
            item(fmt='БЧ', des=ds.unit(), name='Комплект ' + r.choice(['монтажных частей', 'запасных частей', 'упаковки']), qty='1')]})
    n = 1  # сквозные позиции, иногда с пропусками под резерв
    for s in secs:
        if not s['pos']:
            continue
        out = []
        for it in s['items']:
            if tr['reserve'] and r.random() < 0.08:
                out.append({'reserve': r.choice(['blank', 'blank', 'text'])})
                n += 1
            it['pos'] = str(n)
            n += 1
            out.append(it)
            if tr['spaced'] and r.random() < 0.1:
                out.append({'reserve': 'blank'})
        s['items'] = out
    return secs


class Style:
    """манера одного документа: разрешение, шрифт, толщины линий; одинаковы на всех его листах"""

    def __init__(self, r):
        self.ppm = r.choice([200, 300, 300]) / 25.4
        self.hand = r.random() < 0.3
        self.face = r.choice(HAND if self.hand else GOST)
        self.fform = r.choice(GOST)  # печатный бланк (шапка, штамп) — всегда ГОСТ
        self.slant = 0.0 if self.hand or r.random() < 0.4 else r.uniform(0.18, 0.3)
        self.stroke = r.choice([0, 0, 1]) if self.hand else (r.choice([0, 0, 0, 1]) if self.ppm < 10 else r.choice([0, 0, 1, 1]))
        self.cap = r.uniform(3.0, 4.3)  # высота прописной, мм
        self.px = self.cap * self.ppm / capk(self.face) * (0.92 if self.hand else 1.0)
        self.tilt = r.uniform(-0.8, 0.8)  # общий наклон строки «от руки», градусы
        self.rowlines = r.random() < 0.8
        self.lw = max(2, int(self.ppm * r.uniform(0.5, 0.75)))
        self.thin = max(1, round(self.ppm * r.uniform(0.24, 0.32)))
        self.org = r.choice(ORGS)
        self.dither = self.hand or r.random() < 0.3

    def colw(self, c):
        """ширина текста в столбце c, пиксели"""
        return (COLS[c + 1] - COLS[c] - 2.4) * self.ppm - self.slant * self.cap * self.ppm


def item_lines(st, it):
    """строки таблицы позиции: наименование и примечание переносятся на следующие строки"""
    f = font(st.face, st.px)
    nm = wrap(f, it['name'], st.colw(4)) or ['']
    nt = wrap(f, it['note'], st.colw(6)) if it['note'] else []
    out = []
    for k in range(max(len(nm), len(nt))):
        cells = {}
        if k == 0:
            cells = {0: it['fmt'], 1: it['zone'], 2: it['pos'], 3: it['des'], 5: it['qty']}
        if k < len(nm):
            cells[4] = nm[k]
        if k < len(nt):
            cells[6] = nt[k]
        out.append({'kind': 'item' if k == 0 else 'cont', 'cells': {c: s for c, s in cells.items() if s},
                    'item': it if k == 0 else None, 'name': nm, 'note': nt})
    return out


def blank():
    return {'kind': 'blank', 'cells': {}}


def head_line(name):
    return {'kind': 'head', 'cells': {}, 'head': name}


def to_blocks(st, secs, tr):
    """блоки, которые не разрываются между листами: заголовок раздела (с пустыми строками), позиция"""
    blocks = []
    for s in secs:
        pre, post = tr['gap'] in ('both', 'before'), tr['gap'] in ('both', 'after')
        blocks.append({'kind': 'head', 'name': s['name'], 'post': post,
                       'lines': ([blank()] if pre else []) + [head_line(s['name'])] + ([blank()] if post else [])})
        for it in s['items']:
            if 'reserve' in it:
                ln = blank() if it['reserve'] == 'blank' else {'kind': 'reserve', 'cells': {4: 'Резерв'}}
                blocks.append({'kind': 'blank', 'lines': [ln]})
            else:
                blocks.append({'kind': 'item', 'lines': item_lines(st, it)})
    return blocks


def paginate(r, blocks, caps=(29, 32)):
    """блоки по листам: 29 строк на первом (штамп 40 мм) и 32 на следующих (штамп 15 мм). Заголовок не остаётся
    внизу листа без позиции; на продолжении раздел иногда повторяется заголовком"""
    pages, cur, post = [[]], None, False
    for i, b in enumerate(blocks):
        lines = list(b['lines'])
        need = len(lines)
        if b['kind'] == 'head' and i + 1 < len(blocks) and blocks[i + 1]['kind'] == 'item':
            need += len(blocks[i + 1]['lines'])
        room = caps[min(len(pages) - 1, 1)] - len(pages[-1])
        if need > room and b['kind'] != 'blank':
            pages.append([])
            if b['kind'] == 'item' and cur and r.random() < 0.5:
                pages[-1] += [head_line(cur)] + ([blank()] if post else [])
        elif need > room:
            continue  # пустая строка на краю листа не нужна
        if not pages[-1]:
            while lines and lines[0]['kind'] == 'blank':
                lines.pop(0)
        pages[-1] += lines
        if b['kind'] == 'head':
            cur, post = b['name'], b['post']
    return pages


def page_rows(lines):
    """ответ листа: строки позиций с разделом, под которым они стоят ('' — заголовка на этом листе не было)"""
    sect, rows = '', []
    for ln in lines:
        if ln['kind'] == 'head':
            sect = ln['head']
        elif ln['kind'] == 'item':
            it = ln['item']
            row = {'sect': sect}
            for k in FIELDS:
                row[k] = ' '.join(ln['name']) if k == 'name' else ' '.join(ln['note']) if k == 'note' else it[k]
            rows.append(row)
    return rows


# ---- рисование ------------------------------------------------------------------------
class Sheet:
    """лист A4: рамка, таблица спецификации, штамп формы 2 (первый лист) или 2а"""

    def __init__(self, st, r, pageno):
        self.st, self.r = st, r
        self.ppm = st.ppm
        self.first = pageno == 1
        self.hst = 40 if self.first else 15  # высота штампа, мм
        self.W, self.H = round(210 * self.ppm), round(297 * self.ppm)
        self.im = Image.new('L', (self.W, self.H), 255)
        self.d = ImageDraw.Draw(self.im)
        self.X0, self.XR = self.p(20), self.W - self.p(5)  # левый и правый край таблицы (линии рамки)
        self.top, self.bot = self.p(5), self.H - self.p(5)
        self.sy = self.bot - self.p(self.hst)  # верхняя линия штампа
        self.body = self.top + self.p(15)  # низ шапки таблицы
        self.nrows = int((297 - 10 - self.hst - 15) / 8)
        self.d.rectangle([self.X0, self.top, self.XR, self.bot], outline=0, width=st.lw)

    def p(self, mm):
        return round(mm * self.ppm)

    def cx(self, c):
        """x столбца c (границы 0…7), пиксели"""
        return self.X0 + round((self.XR - self.X0) * COLS[c] / 185)

    def sx(self, mm):
        return self.X0 + round((self.XR - self.X0) * mm / 185)

    def rowy(self, i):
        return self.body + self.p(8 * i)

    def hl(self, xa, xb, y, w):
        self.d.rectangle([xa, y - w // 2, xb, y - w // 2 + w - 1], fill=0)

    def vl(self, x, ya, yb, w):
        self.d.rectangle([x - w // 2, ya, x - w // 2 + w - 1, yb], fill=0)

    def text(self, s, xa, xb, yc, align='l', cap=None, filled=True):
        """текст в ячейке xa…xb (пиксели), по вертикали около yc; filled — заполнено рукой или ГОСТом, иначе бланк"""
        if not s:
            return
        st, r, ppm = self.st, self.r, self.ppm
        hand = st.hand and filled
        face = st.face if filled else st.fform
        px = (cap * ppm / capk(face)) * (0.92 if hand else 1.0) if cap else st.px
        px *= r.uniform(0.9, 1.1) if hand else 1.0
        slant = st.slant if filled else 0.0
        f = fit_font(face, s, px, xb - xa - 1.6 * ppm - slant * px)
        bb = f.getbbox('Ж')
        tw = text_w(f, s)
        x = xa + 0.8 * ppm + r.uniform(-0.25, 0.25) * ppm if align == 'l' else (xa + xb - tw) / 2 + r.uniform(-0.2, 0.2) * ppm
        y = yc - (bb[1] + bb[3]) / 2 + (r.gauss(0, 0.3) * ppm if hand else 0)
        stroke = st.stroke if filled else (1 if self.ppm < 10 or px < 40 else r.choice([0, 1]))  # тонкий бланк скан съедает
        ang = st.tilt + r.gauss(0, 0.5) if hand and r.random() < 0.75 else 0
        if abs(ang) > 0.05:
            put_rot(self.im, (x, y), s, f, stroke, ang)
        else:
            put(self.im, (x, y), s, f, stroke, slant)
        return x, y, f, tw

    def vtext(self, s, xc, yc, cap):
        """текст, повёрнутый на 90° (шапка: «Формат», «Зона», «Поз.»), по центру xc, yc"""
        f = fit_font(self.st.fform, s, cap * self.ppm / capk(self.st.fform), 13.5 * self.ppm)
        tmp = Image.new('L', (text_w(f, s) + 8, f.getbbox('Жр')[3] + 6), 255)
        ImageDraw.Draw(tmp).text((4, 2), s, font=f, fill=0, stroke_width=1 if self.ppm < 10 else 0, stroke_fill=0)
        tmp = tmp.rotate(90, expand=True, fillcolor=255)
        paste_dark(self.im, tmp, xc - tmp.size[0] / 2, yc - tmp.size[1] / 2)

    def table(self, lines):
        st, r = self.st, self.r
        for c in range(1, 7):
            self.vl(self.cx(c), self.top, self.sy, st.lw)
        self.hl(self.X0, self.XR, self.body, st.lw)
        if st.rowlines:
            for i in range(1, self.nrows):
                self.hl(self.X0, self.XR, self.rowy(i), st.thin)
        ymid = self.top + self.p(7.5)
        small = r.random() < 0.15  # «Формат», «Зона», «Поз.» мелко вместо поворота на 90°
        for c in range(7):
            if c < 3 and not small:
                self.vtext(HEAD[c], (self.cx(c) + self.cx(c + 1)) // 2, ymid, 3.0)
            else:
                self.text(HEAD[c], self.cx(c), self.cx(c + 1), ymid, 'c', cap=2.2 if c < 3 else r.uniform(3.2, 3.8), filled=False)
        for i, ln in enumerate(lines[:self.nrows]):
            yc = self.rowy(i) + self.p(4) + r.randint(-1, 1)
            if ln['kind'] == 'head':
                res = self.text(ln['head'], self.cx(4), self.cx(5), yc, 'c')
                if res:
                    x, y, f, tw = res
                    yu = round(y + f.getbbox('Ж')[3] + 0.7 * self.ppm)
                    self.hl(round(x - 0.4 * self.ppm), round(x + tw + 0.4 * self.ppm), yu, st.thin)
                continue
            for c, s in ln['cells'].items():
                self.text(s, self.cx(c), self.cx(c + 1), yc, 'c' if c in CENTER else 'l')

    def stamp(self, A, name, pageno, npages):
        """штамп по ГОСТ 2.104: форма 2 (185×40) на первом листе, форма 2а (185×15) на следующих"""
        st, r = self.st, self.r
        sy, lw, thin = self.sy, st.lw, st.thin
        X0, XR = self.X0, self.XR
        row = lambda k: sy + self.p(5 * k)  # noqa: E731 — строка левого блока, k-я линия
        self.hl(X0, XR, sy, lw)
        self.vl(self.sx(65), sy, self.bot, lw)
        full = 8 if self.first else 3
        for k in range(1, full):
            self.hl(X0, self.sx(65), row(k), thin)
        for xx in (17, 40, 55):
            self.vl(self.sx(xx), sy, self.bot, thin)
        self.vl(self.sx(7), sy, row(3) if self.first else self.bot, thin)
        mid = lambda a, b: (self.sx(a), self.sx(b))  # noqa: E731
        cap = 2.3
        hdr = row(2) + self.p(2.5)  # строка «Изм. Лист № докум. Подп. Дата» — третья сверху
        for t, (a, b) in zip(['Изм.', 'Лист', '№ докум.', 'Подп.', 'Дата'], [mid(0, 7), mid(7, 17), mid(17, 40), mid(40, 55), mid(55, 65)]):
            self.text(t, a, b, hdr, 'c', cap=cap, filled=False)
        if self.first:
            self.hl(self.sx(65), XR, sy + self.p(15), lw)
            self.vl(self.sx(135), sy + self.p(15), self.bot, lw)
            self.hl(self.sx(135), XR, sy + self.p(20), thin)
            self.hl(self.sx(135), XR, sy + self.p(25), lw)
            for xx in (150, 165):
                self.vl(self.sx(xx), sy + self.p(15), sy + self.p(25), thin)
            self.text(A, self.sx(65), XR, sy + self.p(7.5), 'c', cap=r.uniform(4.6, 6.0))
            lines, cp = [name], r.uniform(4.2, 5.4)
            while True:  # наименование в 1-2 строки по ширине графы
                lines = wrap(font(st.face, cp * self.ppm / capk(st.face)), name, self.sx(135) - self.sx(65) - 3 * self.ppm)
                if len(lines) <= 2 or cp < 2.5:
                    break
                cp *= 0.85
            for k, ln in enumerate(lines):
                self.text(ln, self.sx(65), self.sx(135), sy + self.p(27.5) + (k - (len(lines) - 1) / 2) * cp * 1.6 * self.ppm, 'c', cap=cp)
            for t, (a, b) in zip(['Лит.', 'Лист', 'Листов'], [mid(135, 150), mid(150, 165), mid(165, 185)]):
                self.text(t, a, b, sy + self.p(17.5), 'c', cap=cap, filled=False)
            if r.random() < 0.2:
                self.text(r.choice('ОА'), self.sx(135), self.sx(150), sy + self.p(22.5), 'c')
            self.text(str(pageno), self.sx(150), self.sx(165), sy + self.p(22.5), 'c', cap=3.5)
            if npages > 1 or r.random() < 0.5:
                self.text(str(npages), self.sx(165), XR, sy + self.p(22.5), 'c', cap=3.5)
            self.text(st.org, self.sx(135), XR, sy + self.p(32.5), 'c', cap=r.uniform(4.0, 5.0))
            for k, t in ((3, 'Разраб.'), (4, 'Пров.'), (6, 'Н. контр.'), (7, 'Утв.')):
                yc = row(k) + self.p(2.5)
                self.text(t, X0, self.sx(17), yc, 'l', cap=cap, filled=False)
                if r.random() < 0.8 and k < 5:
                    self.text(r.choice(SURN), self.sx(17), self.sx(40), yc, 'l', cap=3.0)
                    scribble(self.d, r, self.sx(41), row(k) + self.p(0.7), self.p(12), self.p(3.5), thin)
                    self.text(f'{r.randint(1, 28):02d}.{r.randint(1, 12):02d}.{r.randint(21, 26)}', self.sx(55), self.sx(65), yc, 'c', cap=2.2)
        else:
            self.vl(self.sx(175), sy, self.bot, lw)
            self.hl(self.sx(175), XR, sy + self.p(7), thin)
            self.text(A, self.sx(65), self.sx(175), sy + self.p(7.5), 'c', cap=r.uniform(4.6, 6.0))
            self.text('Лист', self.sx(175), XR, sy + self.p(3.5), 'c', cap=cap, filled=False)
            self.text(str(pageno), self.sx(175), XR, sy + self.p(11), 'c', cap=3.5)


# ---- документ -------------------------------------------------------------------------
def make_spec(r, uid):
    """один документ: [(имя листа, весь лист, вырезка для модели, ответ, обозначение, наименование)]"""
    st, ds = Style(r), Des(r)
    tr = {'gap': r.choices(['both', 'before', 'after', 'none'], [65, 10, 10, 15])[0],
          'reserve': r.random() < 0.2, 'spaced': r.random() < 0.05}
    A, name = ds.A, asm_name(r)
    want = r.choices([1, 2, 3], [45, 35, 20])[0]
    lo, hi = {1: (18, 29), 2: (38, 60), 3: (66, 90)}[want]  # настоящие спецификации плотные: на листе 18 строк и больше
    pages = None
    for k in range(40):
        blocks = to_blocks(st, build_sections(r, ds, r.randint(lo, hi) if k < 12 else max(6, hi // (2 + k // 12)), tr), tr)
        pages = paginate(r, blocks)
        if len(pages) <= 3 and pages[-1]:
            break
    out = []
    for n, lines in enumerate(pages, 1):
        sh = Sheet(st, r, n)
        sh.table(lines)
        sh.stamp(A, name, n, len(pages))
        scan = to_scan(sh.im, r, st.dither)
        # вырезка, как в CursorPad: слева — левый край штампа, справа — внутренний край правой рамки,
        # сверху — верхняя линия таблицы, снизу — верхняя линия штампа; края гуляют на пару точек
        j = lambda: r.randint(-2, 2)  # noqa: E731
        x1 = sh.XR - st.lw + 1
        x0 = x1 - round(185 * st.ppm) + j()
        y0, y1 = sh.top + max(0, j()), sh.sy + j()
        c = scan.crop((x0, y0, x1, y1))
        w, h = c.size
        s = -(-max(w, h) // SPEC_SIDE)
        if s > 1:
            c = c.crop((0, 0, w // s * s, h // s * s)).resize((w // s, h // s), Image.BOX)
        sheet = scan.rotate(r.uniform(-0.4, 0.4), fillcolor=255, resample=Image.BILINEAR) if r.random() < 0.5 else scan
        out.append((f'{uid}_p{n}', sheet, c, page_rows(lines), A, name if n == 1 else ''))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True)
    ap.add_argument('--n', type=int, default=400, help='число спецификаций (листов больше)')
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    for sub in ('img', 'sheets'):
        (out / sub).mkdir(parents=True, exist_ok=True)
    r = random.Random(a.seed)
    labels, nsheets, nrows = {}, 0, 0
    with open(out / 'specs.jsonl', 'w', encoding='utf-8') as js:
        for i in range(a.n):
            for key, sheet, crop, rows, des, name in make_spec(r, f'spec_{i:05d}'):
                sheet.save(out / 'sheets' / f'{key}.png')
                p = out / 'img' / f'{key}.png'
                crop.save(p)
                ans = json.dumps({'rows': rows}, ensure_ascii=False)
                js.write(json.dumps({'image': str(p.resolve()), 'tag': 'spec', 'answer': ans}, ensure_ascii=False) + '\n')
                labels[f'{key}.png'] = {'type': 'spec', 'des': des, 'name': name, 'rows': rows, 'eval': False, 'set': 'synth'}
                nsheets += 1
                nrows += len(rows)
            if (i + 1) % 20 == 0:
                print(i + 1, 'спецификаций,', nsheets, 'листов,', nrows, 'строк', flush=True)
    (out / 'labels.json').write_text(json.dumps(labels, ensure_ascii=False, indent=1), encoding='utf-8')
    print('готово:', a.n, 'спецификаций,', nsheets, 'листов,', nrows, 'строк')


if __name__ == '__main__':
    main()
