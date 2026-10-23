"""Учебные картинки для дообучения: графы штампа и листы с ТТ, нарисованные шрифтами
ГОСТ 2.304 (типы А и Б) и «от руки», с зерном и ступеньками скана — такими, какими их
вырезает CursorPad (drawrec.c dr_vl_read: графа ×2 по пикселям, лист — уменьшение до 1600).
Ответ известен заранее, так модель учится читать то, чего нет в заводских листах.
Тексты придуманы из типовых формулировок: чертежей завода здесь нет.

  python synth.py --out data/synth --n-name 600 --n-mat 600 --n-sheet 300
Выход: картинки и synth.jsonl (image, tag, answer)."""
import argparse
import json
import math
import pathlib
import random

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

FONTS = pathlib.Path('C:/Windows/Fonts')
GOST = ['GOST_BU.ttf', 'GOST_AU.ttf']  # GOST_B.TTF без Юникода: Ø в нём — Ш
HAND = ['segoepr.ttf', 'segoesc.ttf', 'Inkfree.ttf']
_fc = {}


def font(name, px):
    k = (name, int(px))
    if k not in _fc:
        _fc[k] = ImageFont.truetype(str(FONTS / name), max(6, int(px)))
    return _fc[k]


def yr(r, y4):
    return str(y4) if r.random() < 0.4 else f'{y4 % 100:02d}'


# ---- наименования ---------------------------------------------------------------------
NOUNS = ['Втулка', 'Вал', 'Валик', 'Ось', 'Шайба', 'Кольцо', 'Гайка', 'Винт', 'Болт', 'Шпилька', 'Штифт',
         'Палец', 'Пробка', 'Крышка', 'Корпус', 'Фланец', 'Стакан', 'Колесо зубчатое', 'Шестерня', 'Звёздочка',
         'Шкив', 'Муфта', 'Планка', 'Пластина', 'Плита', 'Накладка', 'Прокладка', 'Кронштейн', 'Скоба', 'Уголок',
         'Ребро', 'Косынка', 'Стойка', 'Упор', 'Ручка', 'Рукоятка', 'Рычаг', 'Тяга', 'Серьга', 'Ушко', 'Проушина',
         'Основание', 'Опора', 'Ножка', 'Пята', 'Лапа', 'Боковина', 'Стенка', 'Дно', 'Заглушка', 'Бирка',
         'Шаблон', 'Калибр', 'Калибр-пробка', 'Калибр-кольцо', 'Контршаблон', 'Оправка', 'Кондуктор',
         'Втулка кондукторная', 'Направляющая', 'Полоз', 'Рамка', 'Каркас', 'Хомут', 'Зажим', 'Прижим',
         'Толкатель', 'Поршень', 'Шток', 'Цилиндр', 'Плунжер', 'Сухарь', 'Шпонка', 'Клин', 'Эксцентрик',
         'Кулачок', 'Ролик', 'Барабан', 'Тройник', 'Штуцер', 'Ниппель', 'Патрубок', 'Переходник', 'Пружина',
         'Матрица', 'Пуансон', 'Съёмник', 'Ключ', 'Головка', 'Наконечник', 'Державка', 'Резец', 'Сверло',
         'Развёртка', 'Метчик', 'Фреза', 'Протяжка', 'Цанга', 'Хвостовик', 'Удлинитель', 'Подставка',
         'Площадка', 'Решётка', 'Лоток', 'Короб', 'Кожух', 'Щиток', 'Щека', 'Ребро продольное',
         'Ребро поперечное', 'Плита верхняя', 'Плита нижняя', 'Крышка торцевая', 'Вал-шестерня']
ADJ = ['верхняя', 'нижний', 'левая', 'правый', 'промежуточная', 'опорная', 'упорное', 'регулировочная',
       'установочный', 'резьбовой', 'шлицевое', 'комплексное', 'направляющая', 'сменная', 'разрезная']
PROFILES = ['6х21х25', '8х32х38', 'М20х1,5', 'М12', 'Трап 27х5 лев.', 'Ø36,8', 'Ø25H7', 'R12', '5,1h11']


def name_text(r):
    n = r.choice(NOUNS)
    k = r.random()
    if k < 0.15:
        n += ' ' + r.choice(ADJ)
    elif k < 0.3:
        n += ' ' + r.choice(PROFILES)
    elif k < 0.42:
        dd = f'{r.randint(100, 999)}-{r.randint(10, 99)}-{r.randint(1, 999)}'
        n += ' ' + r.choice(['д\\д', 'д.д.', 'для дет.']) + ' ' + dd
    elif k < 0.5:
        n += ' ' + r.choice(['на размер', 'на сферу', 'на угол']) + ' ' + r.choice(PROFILES)
    elif k < 0.55:
        n = r.choice(['Доработка', 'Переделка']) + ' ' + r.choice(['кронштейна', 'корпуса', 'вала', 'плиты',
                                                                   'крышки', 'рамы']) + ' ' + r.choice(['отвала', 'привода', 'редуктора', 'стола', ''])
    return ' '.join(n.split())


# ---- материалы ------------------------------------------------------------------------
STEEL = [('Сталь 45', 'ГОСТ 1050', 2013), ('Сталь 20', 'ГОСТ 1050', 2013), ('Сталь 35', 'ГОСТ 1050', 2013),
         ('Сталь 40Х', 'ГОСТ 4543', 2016), ('Сталь 38ХС', 'ГОСТ 4543', 2016), ('Сталь 20Х', 'ГОСТ 4543', 2016),
         ('Сталь 12ХН3А', 'ГОСТ 4543', 2016), ('Сталь 65Г', 'ГОСТ 14959', 1979), ('Сталь У8А', 'ГОСТ 1435', 1999),
         ('Сталь У10А', 'ГОСТ 1435', 1999), ('Сталь ХВГ', 'ГОСТ 5950', 2000), ('Сталь 9ХС', 'ГОСТ 5950', 2000),
         ('Сталь Х12МФ', 'ГОСТ 5950', 2000), ('Сталь 12Х18Н10Т', 'ГОСТ 5632', 2014),
         ('Сталь 40Х13', 'ГОСТ 5632', 2014), ('Ст3пс', 'ГОСТ 380', 2005), ('Ст3сп', 'ГОСТ 380', 2005),
         ('БрАЖ9-4', 'ГОСТ 18175', 1978), ('БрО5Ц5С5', 'ГОСТ 613', 1979), ('ЛС59-1', 'ГОСТ 15527', 2004),
         ('Д16Т', 'ГОСТ 4784', 2019), ('АМг6', 'ГОСТ 4784', 2019), ('АК12', 'ГОСТ 1583', 1993),
         ('СЧ20', 'ГОСТ 1412', 1985), ('ВЧ50', 'ГОСТ 7293', 1985), ('Капролон В', 'ТУ 2224-036-00203803', 2012),
         ('Фторопласт-4', 'ГОСТ 10007', 1980)]
SORT = [('Лист', lambda r: r.choice(['1,5', '2', '3', '3,0', '4', '5', '6', '8', '10', '12', '16', '20']),
         [('ГОСТ 19903', 2015), ('ГОСТ 19904', 1990)]),
        ('Круг', lambda r: r.choice(['10', '12', '16', '18', '20', '25', '30', '36', '40', '50', '60', '80',
                                     'h11-9', '20-h11']), [('ГОСТ 2590', 2006), ('ГОСТ 7417', 1975)]),
        ('Труба', lambda r: r.choice(['40х40х2', '40х20х2', '60х40х3', '57х4', '32х3', '20х2,8', '89х5']),
         [('ГОСТ 8639', 1982), ('ГОСТ 8645', 1968), ('ГОСТ 8732', 1978), ('ГОСТ 3262', 1975)]),
        ('Квадрат', lambda r: r.choice(['12', '16', '20', '25', '30']), [('ГОСТ 2591', 2006)]),
        ('Шестигранник', lambda r: r.choice(['12', '14', '17', '19', '22', '24', '27', '32']), [('ГОСТ 2879', 2006)]),
        ('Полоса', lambda r: r.choice(['4х40', '6х40', '8х50', '10х60', '5х25']), [('ГОСТ 103', 2006)]),
        ('Уголок', lambda r: r.choice(['25х25х4', '40х40х4', '50х50х5', '63х63х5']), [('ГОСТ 8509', 1993)])]
LOW = [('Ст3пс', 'ГОСТ 14637', 1989), ('Ст3сп', 'ГОСТ 14637', 1989), ('20', 'ГОСТ 1577', 1993),
       ('45', 'ГОСТ 1050', 2013), ('20', 'ГОСТ 1050', 2013), ('12Х18Н10Т', 'ГОСТ 5582', 1975),
       ('09Г2С', 'ГОСТ 19281', 2014), ('345-12 09Г2С', 'ГОСТ 19281', 2014), ('ВСт1пс', 'ГОСТ 13663', 1986),
       ('ОК360В-IV-Н-Ст3', 'ГОСТ 16523', 1997), ('Б-ПН-О-08Ю', 'ГОСТ 9045', 1993), ('40Х', 'ГОСТ 4543', 2016),
       ('В-Ст3пс', 'ГОСТ 10705', 1980), ('АМг6', 'ГОСТ 21631', 1976)]


def mat_parts(r):
    """(слово, верх, низ) — дробь, или (текст, None, None) — одной строкой"""
    if r.random() < 0.45:
        g, s, y = r.choice(STEEL)
        return f'{g} {s}-{yr(r, y)}' if not s.startswith('ТУ') else f'{g} {s}', None, None
    w, size, std = r.choice(SORT)
    s, y = r.choice(std)
    g, ls, ly = r.choice(LOW)
    return w, f'{size(r)} {s}-{yr(r, y)}', f'{g} {ls}-{yr(r, ly)}'


def mat_answer(p):
    return p[0] if p[1] is None else f'{p[0]} {p[1]} / {p[2]}'


# ---- ТТ -------------------------------------------------------------------------------
def des(r):
    return f'{r.choice(["АБВ", "ГДЕ", "КЛМ", "РСТ", "ИЖН", "УФХ"])} {r.randint(1000, 9999)}-{r.randint(1, 3999)}' + \
        (f'.{r.randint(0, 9):02d}.{r.randint(0, 9):02d}.{r.randint(1, 20):02d}' if r.random() < 0.6 else '')


def tt_items(r):
    hb = r.choice([(30, 35), (35, 40), (40, 45), (45, 50), (28, 32), (56, 62), (58, 62), (48, 52)])
    pool = [
        lambda: r.choice(['*Размеры для справок.', '*Размер для справок.']),
        lambda: r.choice(['H14, h14, ±IT14/2.', 'H14; h14; ±IT14/2.', 'h14, ±IT14/2.', 'h14; ±IT14/2.',
                          'Неуказанные предельные отклонения размеров: H14, h14, ±IT14/2.']),
        lambda: r.choice([f'{hb[0]}...{hb[1]} HRC.', f'HRC {hb[0]}...{hb[1]}.', f'{hb[0]}...{hb[1]} HRCэ.',
                          f'{r.choice([229, 241, 255])}...{r.choice([269, 285, 302])} HB.',
                          f'Цементировать h 0,{r.randint(5, 9)}...1,{r.randint(0, 2)} мм, {hb[0]}...{hb[1]} HRCэ.',
                          f'HRC ≥{hb[0]}.']),
        lambda: 'Материал-заменитель: ' + r.choice(['Сталь 40, 50 ГОСТ 1050-13.', 'Сталь 10, 15 ГОСТ 1050-13.',
                                                     'Сталь 65Г ГОСТ 14959-79.', 'Сталь 38ХС, 45Х ГОСТ 4543-16.']),
        lambda: 'Допускается замена материала на ' + r.choice(['Ст3; 09Г2С; Сталь 25.', 'БрАМц9-2.', 'Сталь 20.']),
        lambda: r.choice(['Острые кромки притупить.', 'Острые кромки притупить R0,3.', 'Неуказанные радиусы R1.',
                          'Заусенцы не допускаются.', 'Неуказанные фаски 0,5х45°.']),
        lambda: f'Маркировать: {des(r)}' + r.choice(['.', '; ПР; НЕ.', f'; {r.randint(2, 40)},{r.randint(0, 9)}h11.']),
        lambda: r.choice(['Покрытие Хим.Окс.прм.', 'Покрытие Ц9.хр.', 'Покрытие Ан.Окс.нхр.',
                          'Покрытие наружных поверхностей, кроме отверстий: -Грунтовка ГФ-021 ГОСТ 25129-82 (1 слой); '
                          '-Эмаль ПФ-115 (серая) ГОСТ 6465-76 (2 слоя).']),
        lambda: r.choice(['Сварные швы по ГОСТ 14771-76.', 'Сварные швы по ГОСТ 5264-80.',
                          'Сварочная проволока Св08Г2С ГОСТ 2246-70.', 'Сварные швы зачистить.']),
        lambda: r.choice(['Обработку шлицов производить электроэрозионным способом.',
                          'Паз обработать электроэрозионным способом.']),
        lambda: '**Размеры контролировать 1 раз в год.',
        lambda: f'Предельный размер изношенного ПР калибра {r.randint(5, 60)},{r.randint(0, 9)}.',
        lambda: f'Допуск плоскостности {r.choice(["0,02", "0,05", "0,1"])} мм.',
        lambda: f'Неуказанная шероховатость Ra {r.choice(["3,2", "6,3", "12,5"])}.',
        lambda: f'Центровые отверстия {r.choice(["А", "Б"])}{r.choice(["2", "2,5", "3,15"])} ГОСТ 14034-74.',
        lambda: r.choice(['Гибку производить в холодном состоянии.', 'По окончании мех. обработки деталь разрезать на две части.',
                          'Отверстия сверлить по месту.', 'Резьба по ГОСТ 24705-2004.']),
    ]
    n = r.choice([0, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 6, 7])
    idx = r.sample(range(len(pool)), n)
    items = [pool[i]() for i in idx]
    if n == 1 and r.random() < 0.5:
        return items  # один пункт часто без номера
    return [f'{i + 1}. {t}' for i, t in enumerate(items)]


def holes(r):
    out = []
    for _ in range(r.choice([0, 0, 0, 1, 1, 2, 3])):
        n = r.choice([2, 3, 4, 6, 8])
        d = r.choice([f'Ø{r.choice([5, 6, 7, 9, 11, 13, 14, 17, 18, 22])}',
                      f'Ø{r.randint(4, 30)},{r.choice([2, 5])}', f'Ø{r.randint(5, 25)}H{r.choice([7, 9, 10])}',
                      f'M{r.choice([4, 5, 6, 8, 10, 12, 16])}-{r.choice(["7H", "6H"])}'])
        out.append(f'{n} отв. {d}' + ('*' if r.random() < 0.1 else ''))
    return out


# ---- рисование ------------------------------------------------------------------------
def text_w(f, s):
    return f.getbbox(s)[2] if s else 0


def put(im, xy, s, f, stroke=0, slant=0.0):
    """текст на картинку; slant — наклон шрифта (ГОСТ 2.304: 75° — около 0,27), 0 — прямой"""
    if not slant:
        ImageDraw.Draw(im).text(xy, s, font=f, fill=0, stroke_width=stroke, stroke_fill=0)
        return
    _, _, w, h = f.getbbox(s, stroke_width=stroke)
    h += 4
    sh = int(h * slant) + 4
    tmp = Image.new('L', (w + sh + 4, h), 255)
    ImageDraw.Draw(tmp).text((2, 2), s, font=f, fill=0, stroke_width=stroke, stroke_fill=0)
    tmp = tmp.transform(tmp.size, Image.AFFINE, (1, slant, -slant * h, 0, 1, 0), fillcolor=255,
                        resample=Image.BILINEAR)
    x, y = int(xy[0]), int(xy[1])
    box = (max(0, x), max(0, y), min(im.size[0], x + tmp.size[0]), min(im.size[1], y + tmp.size[1]))
    if box[2] <= box[0] or box[3] <= box[1]:
        return
    part = tmp.crop((box[0] - x, box[1] - y, box[2] - x, box[3] - y))
    im.paste(ImageChops.darker(im.crop(box), part), box)


def fit_font(face, s, px, maxw):
    while px > 8 and text_w(font(face, px), s) > maxw:
        px *= 0.92
    return font(face, px)


def scribble(d, r, x, y, w, h, width):
    """подпись или загогулина от руки"""
    pts = [(x + r.random() * w * 0.2, y + h * r.random())]
    for _ in range(r.randint(4, 10)):
        pts.append((min(x + w, pts[-1][0] + r.random() * w / 4), y + h * r.random()))
    d.line(pts, fill=0, width=width)


def to_scan(im, r, dither=True):
    """серая картинка → как скан в 1 бит: зерно, рваные края, точки"""
    a = np.asarray(im, dtype=np.float32)
    if r.random() < 0.7:
        a = np.asarray(Image.fromarray(a.astype(np.uint8)).filter(ImageFilter.GaussianBlur(r.uniform(0.4, 1.4))),
                       dtype=np.float32)
    a += np.random.default_rng(r.randint(0, 1 << 30)).normal(0, r.uniform(10, 45) if dither else 6, a.shape)
    a = np.where(a < r.uniform(110, 170), 0, 255).astype(np.uint8)
    rng = np.random.default_rng(r.randint(0, 1 << 30))
    specks = rng.random(a.shape) < r.uniform(0, 0.0015)
    a[specks] = 0
    return Image.fromarray(a, 'L')


def cell_image(r, kind):
    """графа штампа, как её вырезает dr_vl_read: 72 мм × 27 (наименование) или 17 мм (материал), затем ×2"""
    ppm = r.choice([7.87, 11.81]) * r.uniform(0.9, 1.05)  # 200 или 300 dpi, штамп чуть сжат или растянут
    hmm = 27 if kind == 'name' else 17
    W, H = int(72 * ppm), int(hmm * ppm)
    im = Image.new('L', (W, H), 255)
    d = ImageDraw.Draw(im)
    lw = max(1, int(ppm * r.uniform(0.25, 0.8)))
    top = ppm * r.uniform(0.3, 2.6)
    bot = H - ppm * r.uniform(0.6, 1.6) if kind == 'name' else top + 15 * ppm * r.uniform(0.94, 1.02)
    left = ppm * r.uniform(-0.5, 6.5)
    right = W - ppm * r.uniform(0.5, 5)
    d.line([(0, top), (W, top)], fill=0, width=lw + (1 if r.random() < 0.5 else 0))
    d.line([(0, bot), (W, bot)], fill=0, width=lw + 1)
    if left > 1:
        d.line([(left, 0), (left, H)], fill=0, width=lw)
        if r.random() < 0.6:  # подписи и даты в соседних графах
            scribble(d, r, 0, top + 2, left - 2, (bot - top) * 0.45, max(1, lw // 2 + 1))
            scribble(d, r, 0, top + (bot - top) * 0.5, left - 2, (bot - top) * 0.45, max(1, lw // 2 + 1))
    d.line([(right, 0), (right, H)], fill=0, width=lw)
    if r.random() < 0.7:  # обрывки соседних граф справа: Лит., Масса, Масштаб, Лист
        f = font(r.choice(GOST), ppm * r.uniform(2.5, 4))
        d.text((right + ppm, top + ppm * r.uniform(0, 8)), r.choice(['Лит.', 'Масса', 'Масштаб', 'Лист', '1:1',
                                                                    'Листов', '/1', 'Л']), font=f, fill=0)
        if kind == 'name':
            for yy in (top + 5 * ppm, top + 20 * ppm):
                d.line([(right, yy), (W, yy)], fill=0, width=lw)
    hand = r.random() < 0.3
    face = r.choice(HAND if hand else GOST)
    stroke = r.choice([0, 1, 1, 2]) if not hand else r.choice([1, 2, 3])
    sl = 0.0 if hand or r.random() < 0.35 else r.uniform(0.18, 0.3)
    cw, ch = right - max(left, 0) - 2 * ppm, bot - top
    if kind == 'name':
        ans = name_text(r)
        px = ppm * r.uniform(5, 10) * (1.25 if face in GOST else 0.9)
        words, lines = ans.split(), []
        f = font(face, px)
        if text_w(f, ans) > cw and len(words) > 1:  # длинное — в две-три строки помельче
            px = min(px, ch / 2.6)
            f = font(face, px)
            cur = ''
            for w in words:
                if cur and text_w(f, cur + ' ' + w) > cw:
                    lines.append(cur)
                    cur = w
                else:
                    cur = (cur + ' ' + w).strip()
            lines.append(cur)
            if len(lines) > 3:
                lines = [' '.join(words)]
        else:
            lines = [ans]
        fs = [fit_font(face, s, px, cw) for s in lines]
        lh = [f.getbbox(s)[3] for f, s in zip(fs, lines)]
        y = top + (ch - sum(lh) * 1.1) / 2 + r.uniform(-0.1, 0.1) * ch
        for f, s, h in zip(fs, lines, lh):
            x = max(left, 0) + ppm + (cw - text_w(f, s)) / 2 + r.uniform(-0.2, 0.2) * (cw - text_w(f, s))
            put(im, (x, y), s, f, stroke, sl)
            y += h * 1.1
    else:
        p = mat_parts(r)
        ans = mat_answer(p)
        x0 = max(left, 0) + ppm * r.uniform(1, 4)
        if p[1] is None:
            f = fit_font(face, p[0], ppm * r.uniform(4, 8), cw)
            put(im, (x0 + r.uniform(0, 0.3) * max(0, cw - text_w(f, p[0])), top + (ch - f.getbbox(p[0])[3]) / 2),
                p[0], f, stroke, sl)
        else:
            px = ppm * r.uniform(3.5, 6.2)
            fw = font(face, px * r.uniform(1, 1.25))
            put(im, (x0, top + (ch - fw.getbbox(p[0])[3]) / 2), p[0], fw, stroke, sl)
            xf = x0 + text_w(fw, p[0]) + ppm * r.uniform(1, 3)
            room = right - xf - ppm
            ft, fb = fit_font(face, p[1], px, room), fit_font(face, p[2], px, room)
            wt, wb = text_w(ft, p[1]), text_w(fb, p[2])
            mid = top + ch / 2 + r.uniform(-0.5, 0.5) * ppm
            put(im, (xf + (max(wt, wb) - wt) / 2, mid - ft.getbbox(p[1])[3] - ppm * 0.4), p[1], ft, stroke, sl)
            d.line([(xf - ppm * 0.5, mid), (xf + max(wt, wb) + ppm * 0.5, mid)], fill=0, width=max(1, lw // 2 + 1))
            put(im, (xf + (max(wt, wb) - wb) / 2, mid + ppm * 0.4), p[2], fb, stroke, sl)
    if r.random() < 0.5:
        im = im.rotate(r.uniform(-0.8, 0.8), fillcolor=255, resample=Image.BILINEAR)
    im = to_scan(im, r, dither=hand or r.random() < 0.4)
    return im.resize((W * 2, H * 2), Image.NEAREST), ans  # dr_img_part zoom 2: повтор пикселей


def wrap(f, s, maxw):
    words, lines, cur = s.split(), [], ''
    for w in words:
        if cur and text_w(f, cur + ' ' + w) > maxw:
            lines.append(cur)
            cur = w
        else:
            cur = (cur + ' ' + w).strip()
    return lines + [cur] if cur else lines


def arrow_dim(d, r, x0, y0, x1, y1, txt, f, lw):
    d.line([(x0, y0), (x1, y1)], fill=0, width=lw)
    for (ax, ay), (bx, by) in (((x0, y0), (x1, y1)), ((x1, y1), (x0, y0))):
        ang = math.atan2(by - ay, bx - ax)
        L = lw * 6
        for s in (0.3, -0.3):
            d.line([(ax, ay), (ax + L * math.cos(ang + s), ay + L * math.sin(ang + s))], fill=0, width=lw)
    d.text(((x0 + x1) / 2 - text_w(f, txt) / 2, (y0 + y1) / 2 - f.getbbox(txt)[3] - lw * 2), txt, font=f, fill=0)


def sheet_image(r):
    """лист целиком: рамка, штамп, виды с размерами, ТТ над штампом, надписи отверстий, «Развертка»"""
    dpi = r.choice([200, 300, 300])
    ppm = dpi / 25.4
    fmt = r.choice(['A4', 'A4', 'A3'])
    Wmm, Hmm = (210, 297) if fmt == 'A4' else (420, 297)
    W, H = int(Wmm * ppm), int(Hmm * ppm)
    im = Image.new('L', (W, H), 255)
    d = ImageDraw.Draw(im)
    lw = max(2, int(ppm * 0.5))
    thin = max(1, lw // 2)
    m = lambda v: v * ppm
    d.rectangle([m(20), m(5), W - m(5), H - m(5)], outline=0, width=lw)
    sx0, sy0 = W - m(5) - m(185), H - m(5) - m(55)
    d.rectangle([sx0, sy0, W - m(5), H - m(5)], outline=0, width=lw)
    for yy in (15, 40):
        d.line([(sx0 + m(65), sy0 + m(yy)), (W - m(5), sy0 + m(yy))], fill=0, width=lw)
    for xx in (7, 17, 40, 55, 65):
        d.line([(sx0 + m(xx), sy0), (sx0 + m(xx), H - m(5))], fill=0, width=thin if xx < 65 else lw)
    for k in range(1, 11):
        d.line([(sx0, sy0 + m(5 * k)), (sx0 + m(65), sy0 + m(5 * k))], fill=0, width=thin)
    d.line([(sx0 + m(135), sy0 + m(15)), (sx0 + m(135), H - m(5))], fill=0, width=lw)
    gface = r.choice(GOST)
    stamp_name = name_text(r)
    d.text((sx0 + m(70), sy0 + m(3)), des(r), font=font(gface, m(6)), fill=0)
    d.text((sx0 + m(68), sy0 + m(24)), stamp_name, font=fit_font(gface, stamp_name, m(6), m(64)), fill=0)
    mp = mat_parts(r)
    d.text((sx0 + m(68), sy0 + m(45)), mat_answer(mp), font=fit_font(gface, mat_answer(mp), m(4), m(64)), fill=0)
    for t, xx, yy in (('Лит.', 137, 16), ('Масса', 152, 16), ('Масштаб', 168, 16), ('Лист', 137, 36),
                      ('Изм.', 1, 26), ('Разраб.', 1, 31), ('Пров.', 1, 36), ('Утв.', 1, 51)):
        d.text((sx0 + m(xx), sy0 + m(yy)), t, font=font(gface, m(2.5)), fill=0)
    # перевёрнутое обозначение в левом верхнем углу
    tl = Image.new('L', (int(m(70)), int(m(14))), 255)
    ImageDraw.Draw(tl).text((m(4), m(3)), des(r), font=font(gface, m(6)), fill=0)
    im.paste(tl.rotate(180), (int(m(20)), int(m(5))))
    d.rectangle([m(20), m(5), m(90), m(19)], outline=0, width=lw)
    # виды: прямоугольники и окружности с осями, штриховкой и размерами
    f_dim = font(gface, m(r.uniform(3.2, 5)))
    area = (m(30), m(30), W - m(15), sy0 - m(60))
    for _ in range(r.randint(1, 3)):
        cx, cy = r.uniform(area[0] + m(30), max(area[0] + m(31), area[2] - m(30))), r.uniform(area[1] + m(25), max(area[1] + m(26), area[3]))
        w, h = m(r.uniform(15, 80)), m(r.uniform(10, 50))
        if r.random() < 0.5:
            d.rectangle([cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2], outline=0, width=lw)
            arrow_dim(d, r, cx - w / 2, cy + h / 2 + m(10), cx + w / 2, cy + h / 2 + m(10), f'{w / ppm:.0f}', f_dim, thin)
            if r.random() < 0.5:
                for k in range(int(w // m(4))):
                    d.line([(cx - w / 2 + k * m(4), cy + h / 2), (cx - w / 2 + k * m(4) + m(4), cy - h / 2)], fill=0, width=thin)
        else:
            rr = min(w, h) / 2
            d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=0, width=lw)
            arrow_dim(d, r, cx - rr, cy, cx + rr, cy, f'Ø{2 * rr / ppm:.0f}' + r.choice(['', 'h7', 'H7', 'f9']), f_dim, thin)
        for k in range(0, int(w + m(20)), int(m(8))):  # штрихпунктирная ось
            d.line([(cx - w / 2 - m(10) + k, cy), (cx - w / 2 - m(10) + k + m(5), cy)], fill=0, width=thin)
        if r.random() < 0.6:
            d.text((cx + w / 2 + m(3), cy - h / 2 - m(8)), r.choice(['Ra 3,2', 'Ra 1,6', 'Ra 6,3', '45°', 'R5',
                                                                      '2 фаски 1х45°', 'А-А', 'Б', 'М2:1', '*']),
                    font=f_dim, fill=0)
    if r.random() < 0.7:
        x, y = W - m(45), m(10)  # знак шероховатости в углу: «Ra 6,3 (√)»; √ — линиями, в шрифтах его нет
        d.line([(x, y + m(2)), (x + m(1.5), y + m(5)), (x + m(4), y)], fill=0, width=thin)
        d.text((x + m(5), y), r.choice(['Ra 6,3', 'Ra 12,5', 'Ra 3,2']) + ' (  )', font=font(gface, m(4)), fill=0)
    hs = holes(r)
    for h in hs:
        x, y = r.uniform(area[0], max(area[0] + 1, area[2] - m(50))), r.uniform(area[1], max(area[1] + 1, area[3]))
        d.text((x, y), h, font=f_dim, fill=0)
        d.line([(x, y + f_dim.getbbox(h)[3] + m(1)), (x + text_w(f_dim, h), y + f_dim.getbbox(h)[3] + m(1))], fill=0, width=thin)
        d.line([(x, y + f_dim.getbbox(h)[3] + m(1)), (x - m(r.uniform(5, 15)), y + m(r.uniform(5, 15)))], fill=0, width=thin)
    razv = r.random() < 0.2
    if razv:
        d.text((area[0] + m(r.uniform(0, 60)), area[1]), r.choice(['Развертка', 'Развертка', 'Линии гиба']),
               font=font(gface, m(5)), fill=0)
    # ТТ над штампом
    tt = tt_items(r)
    f_tt = font(gface, m(r.uniform(3.5, 5)))
    sl = 0.0 if r.random() < 0.4 else r.uniform(0.18, 0.3)
    lines = []
    for t in tt:
        ws = wrap(f_tt, t, m(180))
        lines += [ws[0]] + ['   ' + w for w in ws[1:]]
    lh = f_tt.getbbox('Ж')[3] * 1.55
    y = sy0 - m(r.uniform(4, 12)) - lh * len(lines)
    x = sx0 + m(r.uniform(0, 10))
    for s in lines:
        put(im, (x, y), s, f_tt, 0, sl)
        y += lh
    if r.random() < 0.5:
        im = im.rotate(r.uniform(-0.5, 0.5), fillcolor=255, resample=Image.BILINEAR)
    im = to_scan(im, r, dither=r.random() < 0.3)
    s = -(-max(W, H) // 1600)  # dr_img_part: целое уменьшение до 1600
    if s > 1:
        im = im.resize((W // s, H // s), Image.BOX)
    return im, json.dumps({'tt': tt, 'razvertka': razv, 'holes': hs}, ensure_ascii=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True)
    ap.add_argument('--n-name', type=int, default=600)
    ap.add_argument('--n-mat', type=int, default=600)
    ap.add_argument('--n-sheet', type=int, default=300)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    r = random.Random(a.seed)
    with open(out / 'synth.jsonl', 'w', encoding='utf-8') as js:
        for tag, n in (('name', a.n_name), ('mat', a.n_mat), ('sheet', a.n_sheet)):
            for i in range(n):
                im, ans = sheet_image(r) if tag == 'sheet' else cell_image(r, tag)
                p = out / f'{tag}_{i:05d}.png'
                im.save(p)
                js.write(json.dumps({'image': str(p.resolve()), 'tag': tag, 'answer': ans}, ensure_ascii=False) + '\n')
            print(tag, n, flush=True)


if __name__ == '__main__':
    main()
