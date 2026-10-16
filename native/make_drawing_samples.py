#!/usr/bin/env python3
"""Пробные «сканы» для окна «Распознать чертёж» (drawrec.c).

Рисует по ГОСТ 2.104 / 2.106 лист чертежа A3 (основная надпись форма 1),
первый лист спецификации A4 (форма 2), второй лист спецификации (форма 2а)
и страницу маршрутной карты, как будто их отсканировали: лёгкий перекос,
шум, ч/б. Нужен Pillow. Запуск:

    python3 make_drawing_samples.py папка

В папке появятся sample-drawing.tif, sample-spec.tif (2 страницы),
sample-tp.png, sample-pack.tif (все листы одним файлом) и sample-screen.png
(штамп, снятый с экрана: 96 dpi, без рамки листа).
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont, ImageFilter

DPI = 300
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_I = "/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf"


def mm(v, dpi=DPI):
    return int(round(v * dpi / 25.4))


class Sheet:
    def __init__(self, wmm, hmm, dpi=DPI):
        self.dpi = dpi
        self.wmm, self.hmm = wmm, hmm
        self.img = Image.new("L", (mm(wmm, dpi), mm(hmm, dpi)), 255)
        self.d = ImageDraw.Draw(self.img)
        self.fonts = {}

    def p(self, x, y):
        return (mm(x, self.dpi), mm(y, self.dpi))

    def line(self, x0, y0, x1, y1, thick=False):
        w = mm(0.8 if thick else 0.3, self.dpi) or 1
        self.d.line([self.p(x0, y0), self.p(x1, y1)], fill=0, width=w)

    def rect(self, x0, y0, x1, y1, thick=False):
        for a in ((x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)):
            self.line(*a, thick=thick)

    def font(self, hmm, italic=True):
        k = (hmm, italic)
        if k not in self.fonts:
            self.fonts[k] = ImageFont.truetype(FONT_I if italic else FONT, max(6, mm(hmm * 1.35, self.dpi)))
        return self.fonts[k]

    def text(self, x, y, s, hmm=3.5, center=None, italic=True):
        f = self.font(hmm, italic)
        px, py = self.p(x, y)
        if center is not None:  # center — ширина ячейки в мм, x — её левый край
            w = self.d.textlength(s, font=f)
            px = int(px + (mm(center, self.dpi) - w) / 2)
        self.d.text((px, py), s, font=f, fill=0)

    def frame(self):
        self.rect(20, 5, self.wmm - 5, self.hmm - 5, thick=True)


def stamp_form1(s, f):
    """основная надпись форма 1: 185 × 55, правый нижний угол рамки"""
    R, B = s.wmm - 5, s.hmm - 5
    L, T = R - 185, B - 55
    s.rect(L, T, R, B, thick=True)
    for x in (7, 17, 40, 55, 65):
        s.line(L + x, T, L + x, B, thick=True)
    for i in range(1, 11):
        s.line(L, T + 5 * i, L + 65, T + 5 * i, thick=(i == 6))
    s.line(L + 65, T + 15, R, T + 15, thick=True)
    s.line(L + 65, T + 40, R, T + 40, thick=True)
    s.line(L + 135, T + 15, L + 135, B, thick=True)
    s.line(L + 135, T + 20, R, T + 20, thick=True)
    s.line(L + 135, T + 35, R, T + 35, thick=True)
    for x in (140, 145):
        s.line(L + x, T + 20, L + x, T + 35)
    s.line(L + 150, T + 15, L + 150, T + 35, thick=True)
    s.line(L + 167, T + 15, L + 167, T + 35, thick=True)
    s.line(L + 155, T + 35, L + 155, T + 40, thick=True)
    lab = 2.5
    for x, w, t in ((0, 7, "Изм."), (7, 10, "Лист"), (17, 23, "№ докум."), (40, 15, "Подп."), (55, 10, "Дата")):
        s.text(L + x, T + 25.8, t, lab, center=w)
    for i, t in enumerate(("Разраб.", "Пров.", "Т.контр.", "", "Н.контр.", "Утв.")):
        s.text(L + 0.8, T + 30.8 + 5 * i, t, lab)
    s.text(L + 17.8, T + 30.8, f["razrab"], lab)
    s.text(L + 17.8, T + 35.8, "Петров", lab)
    s.text(L + 135, T + 15.8, "Лит.", lab, center=15)
    s.text(L + 150, T + 15.8, "Масса", lab, center=17)
    s.text(L + 167, T + 15.8, "Масштаб", lab, center=18)
    s.text(L + 136, T + 35.8, "Лист " + f["sheet"], lab)
    s.text(L + 156, T + 35.8, "Листов " + f["sheets"], lab)
    s.text(L + 65, T + 4.5, f["des"], 7, center=120)
    s.text(L + 65, T + 23, f["name"], 7 if len(f["name"]) < 14 else 5, center=70)
    m = f["mat"].split("\n")
    if len(m) == 1:
        s.text(L + 65, T + 45, m[0], 3.5, center=70)
    else:
        s.text(L + 65, T + 41.5, m[0], 3, center=70)
        s.line(L + 75, T + 47.5, L + 125, T + 47.5)
        s.text(L + 65, T + 48.5, m[1], 3, center=70)
    s.text(L + 140, T + 24.5, "О", 5, center=5)
    s.text(L + 150, T + 24.5, f["mass"], 5, center=17)
    s.text(L + 167, T + 24.5, f["scale"], 5, center=18)
    s.text(L + 135, T + 44, f["org"], 5, center=50)


def stamp_form2(s, f):
    """форма 2 (первый лист спецификации, текстовых документов): 185 × 40"""
    R, B = s.wmm - 5, s.hmm - 5
    L, T = R - 185, B - 40
    s.rect(L, T, R, B, thick=True)
    for x in (7, 17, 40, 55, 65):
        s.line(L + x, T, L + x, B, thick=True)
    for i in range(1, 8):
        s.line(L, T + 5 * i, L + 65, T + 5 * i, thick=(i == 3))
    s.line(L + 65, T + 15, R, T + 15, thick=True)
    s.line(L + 135, T + 15, L + 135, B, thick=True)
    s.line(L + 135, T + 20, R, T + 20, thick=True)
    s.line(L + 135, T + 25, R, T + 25, thick=True)
    for x in (140, 145):
        s.line(L + x, T + 20, L + x, T + 25)
    s.line(L + 150, T + 15, L + 150, T + 25, thick=True)
    s.line(L + 165, T + 15, L + 165, T + 25, thick=True)
    lab = 2.5
    for x, w, t in ((0, 7, "Изм."), (7, 10, "Лист"), (17, 23, "№ докум."), (40, 15, "Подп."), (55, 10, "Дата")):
        s.text(L + x, T + 10.8, t, lab, center=w)
    for i, t in enumerate(("Разраб.", "Пров.", "", "Н.контр.", "Утв.")):
        s.text(L + 0.8, T + 15.8 + 5 * i, t, lab)
    s.text(L + 17.8, T + 15.8, f["razrab"], lab)
    s.text(L + 135, T + 15.8, "Лит.", lab, center=15)
    s.text(L + 150, T + 15.8, "Лист", lab, center=15)
    s.text(L + 165, T + 15.8, "Листов", lab, center=20)
    s.text(L + 150, T + 20.7, f["sheet"], 3.5, center=15)
    s.text(L + 165, T + 20.7, f["sheets"], 3.5, center=20)
    s.text(L + 65, T + 4.5, f["des"], 7, center=120)
    s.text(L + 65, T + 24, f["name"], 5, center=70)
    s.text(L + 135, T + 29, f["org"], 5, center=50)


def stamp_form2a(s, des, sheet):
    R, B = s.wmm - 5, s.hmm - 5
    L, T = R - 185, B - 15
    s.rect(L, T, R, B, thick=True)
    for x in (7, 17, 40, 55, 65, 175):
        s.line(L + x, T, L + x, B, thick=True)
    for i in (1, 2):
        s.line(L, T + 5 * i, L + 65, T + 5 * i, thick=(i == 2))
    s.line(L + 175, T + 7, R, T + 7, thick=True)
    s.text(L + 175, T + 1.5, "Лист", 2.5, center=10)
    s.text(L + 175, T + 8.5, sheet, 3.5, center=10)
    s.text(L + 65, T + 4.5, des, 7, center=110)


SPEC_COLS = (0, 6, 12, 20, 90, 153, 163, 185)


def spec_table(s, rows, bottom):
    R = s.wmm - 5
    L = R - 185
    T = 5
    s.line(L, T + 15, R, T + 15, thick=True)
    for x in SPEC_COLS[1:-1]:
        s.line(L + x, T, L + x, bottom, thick=True)
    y = T + 15
    while y + 8 <= bottom + 0.01:
        y += 8
        if y < bottom - 0.5:
            s.line(L, y, R, y)
    heads = ("Формат", "Зона", "Поз.", "Обозначение", "Наименование", "Кол.", "Приме-\nчание")
    for i, h in enumerate(heads):
        x0, x1 = SPEC_COLS[i], SPEC_COLS[i + 1]
        hh = h.split("\n")
        for k, part in enumerate(hh):
            s.text(L + x0, T + 6 - 2.5 * (len(hh) - 1) + 5 * k, part, 2.5 if x1 - x0 < 10 else 3.5,
                   center=x1 - x0)
    y = T + 15
    for r in rows:
        if r[0] == "#":  # раздел: по центру «Наименования», подчёркнут
            f = s.font(3.5)
            w = s.d.textlength(r[1], font=f) * 25.4 / s.dpi
            s.text(L + 90, y + 2, r[1], 3.5, center=63)
            x0 = L + 90 + (63 - w) / 2
            s.line(x0, y + 7, x0 + w, y + 7)
        else:
            fmt, zone, pos, des, name, qty, note = r
            s.text(L + 0.5, y + 2.2, fmt, 3.5)
            s.text(L + 12, y + 2.2, pos, 3.5, center=8)
            s.text(L + 21, y + 2.2, des, 3.5)
            s.text(L + 91, y + 2.2, name, 3.5)
            s.text(L + 153, y + 2.2, qty, 3.5, center=10)
            s.text(L + 164, y + 2.2, note, 3.5)
        y += 8


def scan(img, angle, seed, bw=True):
    rnd = random.Random(seed)
    img = img.rotate(angle, resample=Image.BICUBIC, expand=False, fillcolor=255)
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    px = img.load()
    w, h = img.size
    for _ in range(w * h // 400):  # пыль
        x, y = rnd.randrange(w), rnd.randrange(h)
        px[x, y] = 0
    if bw:
        img = img.point(lambda v: 0 if v < 150 else 255).convert("1")
    return img


def drawing():
    s = Sheet(420, 297)
    s.frame()
    # что-нибудь вместо чертежа: деталь с отверстиями и размерами
    s.rect(80, 60, 260, 170, thick=True)
    for cx in (110, 230):
        s.d.ellipse([s.p(cx - 10, 105), s.p(cx + 10, 125)], outline=0, width=mm(0.8))
    s.line(80, 185, 260, 185)
    s.text(160, 178, "180", 3.5)
    s.text(300, 70, "1. *Размеры для справок.", 5)
    s.text(300, 80, "2. Неуказанные предельные", 5)
    s.text(300, 87, "   отклонения размеров H14, h14.", 5)
    # графа над штампом: «Перв. примен.» не нужна, рисуем только штамп
    stamp_form1(s, {
        "des": "АДЕ 3422-682.01.01.02", "name": "Кронштейн",
        "mat": "Лист Б-ПН-О-4 ГОСТ 19903-2015\nСт3сп ГОСТ 14637-89",
        "mass": "0,85", "scale": "1:2", "sheet": "1", "sheets": "1",
        "org": "ООО «Завод»", "razrab": "Иванов"})
    return s


SPEC_ROWS = [
    ("#", "Документация"),
    ("А3", "", "", "АДЕ 3422-682.01.00.00 СБ", "Сборочный чертеж", "", ""),
    ("#", "Детали"),
    ("А3", "", "1", "АДЕ 3422-682.01.01.02", "Кронштейн", "2", ""),
    ("А4", "", "2", "АДЕ 3422-682.01.01.03", "Пластина", "1", ""),
    ("А4", "", "3", "АДЕ 3422-682.01.01.04", "Втулка", "4", ""),
    ("#", "Стандартные изделия"),
    ("", "", "4", "", "Болт М6-6gx20.58", "8", ""),
    ("", "", "", "", "ГОСТ 7798-70", "", ""),
    ("", "", "5", "", "Шайба 6 ГОСТ 11371-78", "8", ""),
    ("", "", "6", "", "Гайка М6-6Н.5", "8", ""),
    ("", "", "", "", "ГОСТ 5915-70", "", ""),
]
SPEC_ROWS2 = [
    ("#", "Материалы"),
    ("", "", "7", "", "Эмаль ПФ-115 серая", "0,1", "кг"),
]


def spec():
    s1 = Sheet(210, 297)
    s1.frame()
    spec_table(s1, SPEC_ROWS, 297 - 5 - 40)
    stamp_form2(s1, {"des": "АДЕ 3422-682.01.00.00", "name": "Кронштейн в сборе", "sheet": "1",
                     "sheets": "2", "org": "ООО «Завод»", "razrab": "Иванов"})
    s2 = Sheet(210, 297)
    s2.frame()
    spec_table(s2, SPEC_ROWS2, 297 - 5 - 15)
    stamp_form2a(s2, "АДЕ 3422-682.01.00.00", "2")
    return s1, s2


def tp():
    s = Sheet(297, 210)
    s.rect(5, 5, 292, 205, thick=True)
    s.text(10, 8, "ГОСТ 3.1118-82 Форма 1", 2.5)
    s.text(200, 12, "АДЕ 3422-682.01.01.02 ТП", 5)
    s.text(110, 22, "МАРШРУТНАЯ КАРТА", 5, italic=False)
    s.text(10, 32, "М01  Лист Б-ПН-О-4 ГОСТ 19903-2015", 3.5)
    s.line(5, 40, 292, 40)
    for x in (12, 30, 45, 60, 150):
        s.line(x, 40, x, 205)
    s.text(13, 42, "Цех", 2.5)
    s.text(31, 42, "Уч.", 2.5)
    s.text(46, 42, "Опер.", 2.5)
    s.text(61, 42, "Код, наименование операции", 2.5)
    rows = ["005 Заготовительная", "010 Слесарная", "015 Сверлильная", "020 Контрольная"]
    for i, r in enumerate(rows):
        y = 52 + i * 9
        s.text(6, y, "А", 3.5)
        s.text(46, y, r.split()[0], 3.5)
        s.text(61, y, "4110 " + r.split()[1], 3.5)
        s.line(5, y + 7, 292, y + 7)
    s.text(110, 195, "МК", 5)
    return s


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    d = scan(drawing().img, 0.35, 1)
    a, b = spec()
    sa, sb = scan(a.img, -0.25, 2), scan(b.img, 0.15, 3)
    t = scan(tp().img, 0.2, 4, bw=False)
    d.save(os.path.join(out, "sample-drawing.tif"), compression="group4", dpi=(DPI, DPI))
    sa.save(os.path.join(out, "sample-spec.tif"), compression="group4", dpi=(DPI, DPI), save_all=True,
            append_images=[sb])
    t.save(os.path.join(out, "sample-tp.png"), dpi=(DPI, DPI))
    d.save(os.path.join(out, "sample-pack.tif"), compression="group4", dpi=(DPI, DPI), save_all=True,
           append_images=[sa, sb, t.convert("1")])
    # штамп с экрана: просмотрщик в масштабе ~40 %, 96 dpi, вокруг немного поля листа
    big = drawing().img
    k = 0.42
    small = big.resize((int(big.width * k), int(big.height * k)), Image.LANCZOS)
    x1, y1 = small.width - mm(4, DPI * k), small.height - mm(4, DPI * k)
    crop = small.crop((x1 - mm(205, DPI * k), y1 - mm(70, DPI * k), x1 + 6, y1 + 6))
    crop.convert("RGB").save(os.path.join(out, "sample-screen.png"))
    print("ok", out)


if __name__ == "__main__":
    main()
