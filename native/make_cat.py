#!/usr/bin/env python3
"""Чёрный кот в колпаке: шестой набор курсоров (k6).

Стрелка — голова чёрного кота в фиолетовом остроконечном колпаке с золотой
пряжкой; кончик колпака загнут влево-вверх, им и нажимает — он в левом верхнем
углу, как у обычной стрелки. Глаза жёлто-зелёные с вертикальными зрачками,
розовый нос, светлые усы. «Рука» над ссылками — чёрная кошачья лапа с розовыми
подушечками, вверх указывает один выпущенный коготь. Остальные виды (текст,
размеры, запрет…) рисует make_cursors.py в фиолетово-золотой гамме, чтобы набор
читался как один.

Чёрное на чёрном не видно, поэтому у кота два ободка: тёмно-фиолетовый снаружи
(чтобы читался на белом) и сиреневый внутри (чтобы читался на тёмном фоне).

Размер — как у Мечника, Рукавицы и Дракона: 32, 48 и 64 точки.
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageFilter

import make_cursors as mc
from make_dragon import Layer, smooth, talon

HERE = Path(__file__).resolve().parent
SRC = HERE / "cursors"
SIZES = (32, 48, 64)

EDGE = (22, 10, 36)            # внешний контур: чтобы кота было видно на белом
RIM = (196, 170, 238)          # внутренний ободок: чтобы было видно на чёрном
FUR = (30, 26, 40)             # шерсть: чёрная с фиолетовым отливом
FUR_LIGHT = (66, 58, 88)       # блик на макушке и щеках
EAR_IN = (222, 120, 160)       # внутри ушей
EYE = (214, 238, 70)
PUPIL = (12, 10, 16)
NOSE = (240, 124, 156)
WHISKER = (236, 232, 246)
HAT = (104, 56, 170)
HAT_DARK = (62, 30, 112)
HAT_LIGHT = (150, 98, 214)
BAND = (252, 206, 64)
BAND_DARK = (190, 140, 24)
PAD = (236, 120, 156)

mc.THEMES["k6"] = {"body": ((184, 132, 238), (70, 34, 124)),
                   "accent": ((255, 228, 120), (214, 150, 28)),
                   "edge": EDGE, "rim": (240, 228, 255)}

TIP = (0.05, 0.05)       # кончик колпака — точка нажатия
HOT_HAND = (0.40, 0.04)  # кончик когтя у лапы


def ring(im: Image.Image, color, grow) -> Image.Image:
    """Ободок снаружи силуэта шириной в `grow` // 2 точек."""
    a = im.getchannel("A").point(lambda v: 255 if v > 40 else 0)
    big = a.filter(ImageFilter.MaxFilter(grow))
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(Image.new("RGBA", im.size, color + (255,)), (0, 0), big)
    out.alpha_composite(im)
    return out


def finish(L):
    im = L.im.resize((L.px, L.px), Image.LANCZOS)
    # сначала сиреневая кайма вплотную к рисунку, поверх неё тёмный контур
    return ring(ring(im, RIM, 3), EDGE, 3)


def cat(px):
    L = Layer(px)

    # хвост: из-за туловища вправо и кольцом вверх
    tail = smooth([(0.66, 0.90), (0.82, 0.93), (0.94, 0.82), (0.93, 0.66)])
    L.line(tail, 0.085, FUR + (255,))

    # туловище и передние лапки
    L.ellipse(0.50, 0.84, 0.25, 0.16, FUR + (255,))
    for x in (0.38, 0.62):
        L.ellipse(x, 0.95, 0.075, 0.045, FUR_LIGHT + (255,))

    # уши — за колпаком, снаружи от него
    L.poly([(0.18, 0.56), (0.16, 0.26), (0.38, 0.44)], FUR + (255,))
    L.poly([(0.80, 0.56), (0.84, 0.26), (0.62, 0.44)], FUR + (255,))
    L.poly([(0.21, 0.50), (0.20, 0.34), (0.32, 0.45)], EAR_IN + (255,))
    L.poly([(0.77, 0.50), (0.80, 0.34), (0.68, 0.45)], EAR_IN + (255,))

    # голова
    L.ellipse(0.49, 0.62, 0.32, 0.25, FUR + (255,))
    L.ellipse(0.49, 0.50, 0.20, 0.07, FUR_LIGHT + (150,))

    # глаза с вертикальными зрачками
    for x in (0.37, 0.62):
        L.ellipse(x, 0.60, 0.060, 0.055, EYE + (255,))
        L.ellipse(x, 0.60, 0.016, 0.050, PUPIL + (255,))
        L.ellipse(x + 0.018, 0.575, 0.012, 0.012, (255, 255, 255, 255))
    # нос и рот
    L.poly([(0.455, 0.69), (0.525, 0.69), (0.49, 0.735)], NOSE + (255,))
    L.line([(0.49, 0.735), (0.49, 0.765)], 0.012, FUR_LIGHT + (255,))
    L.line(smooth([(0.43, 0.775), (0.46, 0.80), (0.49, 0.765), (0.52, 0.80), (0.55, 0.775)], 4),
           0.012, FUR_LIGHT + (255,))
    # усы
    for sgn, x0 in ((-1, 0.40), (1, 0.58)):
        for dy in (-0.03, 0.02):
            L.line([(x0, 0.72 + dy * 0.4), (x0 + sgn * 0.20, 0.70 + dy)], 0.011,
                   WHISKER + (255,))

    # колпак: загнутый кончик в левом верхнем углу, поля эллипсом
    L.ellipse(0.46, 0.43, 0.27, 0.075, HAT_DARK + (255,))
    cone = smooth([(0.05, 0.05), (0.14, 0.12), (0.22, 0.24), (0.27, 0.40),
                   (0.66, 0.40), (0.58, 0.28), (0.44, 0.16), (0.28, 0.08),
                   (0.12, 0.04), (0.05, 0.05)], 6)
    L.poly(cone, HAT + (255,))
    L.poly(smooth([(0.20, 0.14), (0.28, 0.22), (0.31, 0.38), (0.40, 0.38),
                   (0.38, 0.26), (0.28, 0.14)], 4), HAT_LIGHT + (190,))
    # лента и пряжка
    L.poly([(0.265, 0.34), (0.625, 0.34), (0.645, 0.41), (0.265, 0.41)], BAND_DARK + (255,))
    L.poly([(0.265, 0.34), (0.625, 0.34), (0.635, 0.38), (0.265, 0.38)], BAND + (255,))
    L.poly([(0.405, 0.325), (0.50, 0.325), (0.50, 0.415), (0.405, 0.415)], BAND_DARK + (255,))
    L.poly([(0.425, 0.345), (0.48, 0.345), (0.48, 0.395), (0.425, 0.395)], HAT + (255,))
    return finish(L)


def paw(px):
    """«Рука» над ссылкой: лапа снизу, вверх выпущен один коготь."""
    L = Layer(px)
    # предплечье и ладонь
    L.poly(smooth([(0.30, 0.99), (0.26, 0.74), (0.30, 0.56), (0.50, 0.48), (0.70, 0.54),
                   (0.78, 0.72), (0.74, 0.99)], 6), FUR + (255,))
    # поджатые пальцы
    for cx in (0.34, 0.52, 0.70):
        L.ellipse(cx, 0.60, 0.085, 0.10, FUR + (255,))
        L.ellipse(cx, 0.585, 0.045, 0.06, FUR_LIGHT + (200,))
    # указательный палец вверх и длинный коготь — им и нажимает
    L.line([(0.50, 0.56), (0.46, 0.30)], 0.15, FUR + (255,))
    L.line([(0.50, 0.50), (0.47, 0.30)], 0.05, FUR_LIGHT + (200,))
    talon_l = Layer(px)
    talon(talon_l, (0.46, 0.30), HOT_HAND, (0.012, 0.0), w=0.09)
    L.im.alpha_composite(talon_l.im)
    # розовые подушечки
    L.ellipse(0.52, 0.80, 0.12, 0.09, PAD + (255,))
    for x, y in ((0.34, 0.66), (0.50, 0.62), (0.68, 0.66)):
        L.ellipse(x, y, 0.035, 0.040, PAD + (255,))
    return finish(L)


def main():
    mc.write_cur([cat(px) for px in SIZES], SRC / "k6_static.cur", TIP)
    mc.write_cur([paw(px) for px in SIZES], SRC / "k6_hand.cur", HOT_HAND)

    # остальные виды — общими фигурами, в фиолетово-золотой гамме
    mc.SIZES = SIZES
    mc.build_theme("k6")

    # «помощь» и «запуск программы» — кот со значком. build_theme берёт
    # спрайт из готового .cur, а PIL читает 32-битный курсор без прозрачности,
    # поэтому эти виды собираем из рисунка напрямую (как у дракона)
    badges = {}
    for px in SIZES:
        c = mc.Canvas(int(px * 0.46))
        mc.role_question(c)
        badges[px] = c.render()
    mc.write_cur([mc.compose_with_sprite(cat(px), badges[px], px) for px in SIZES],
                 SRC / "k6_help.cur", TIP)
    app_frames = []
    for i in range(8):
        imgs = []
        for px in mc.ANI_SIZES:
            b = mc.Canvas(int(px * 0.46))
            mc.role_spinner(b, i)
            imgs.append(mc.compose_with_sprite(cat(px), b.render(), px))
        f = SRC / f"k6_app{i + 1:02d}.cur"
        mc.write_cur(imgs, f, TIP)
        app_frames.append(f)
    mc.write_ani(app_frames, SRC / "k6_app.ani")
    print("k6: чёрный кот в колпаке готов,", "/".join(map(str, SIZES)))


def preview(out: Path):
    """Лист с котом и лапой на белом и тёмном фоне, в 6 раз крупнее."""
    sheet = Image.new("RGBA", (64 * 4 * 6 // 2 + 40, 64 * 6 * 2 // 2 + 40), (255, 255, 255, 255))
    x = 10
    for bg in ((255, 255, 255), (24, 24, 28)):
        y = 10 if bg[0] == 255 else 64 * 3 + 20
        x = 10
        for im in (cat(64), paw(64), cat(32), paw(32)):
            tile = Image.new("RGBA", im.size, bg + (255,))
            tile.alpha_composite(im)
            tile = tile.resize((im.size[0] * 3, im.size[1] * 3), Image.NEAREST)
            sheet.paste(tile, (x, y))
            x += tile.size[0] + 10
    sheet.save(out)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--preview":
        preview(Path(sys.argv[2]))
    else:
        main()
