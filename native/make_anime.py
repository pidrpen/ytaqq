#!/usr/bin/env python3
"""«Тяночка»: восьмой набор курсоров (k8) — аниме-девочка, блондинка, анимированная.

    python native/make_anime.py              → native/cursors/k8_<вид>.ani (16 видов)
    python native/make_anime.py --preview out.png

Стрелка — девочка по плечи: золотые волосы с двумя хвостиками и красным бантом,
большие голубые глаза, тёмно-синий матросский воротник с красным галстуком.
Правой рукой она указывает в левый верхний угол — кончик пальца и есть точка
нажатия, как у обычной стрелки. Восемь кадров: хвостики качаются, бант
подрагивает, на пятом-шестом кадре она моргает. «Рука» над ссылками — её
ладонь с поднятым указательным пальцем и манжетой матроски; тоже покачивается.
Остальные виды (текст, размеры, запрет…) рисует make_cursors.py в золотисто-
красной гамме одним кадром, но тоже в .ani: программа ставит весь набор
одинаково, как «Риас» (rias_apply → ani_apply).

Девочка рисуется кодом, как «Чёрный кот» (make_cat.py): фигуры в 8 раз крупнее,
уменьшение, два ободка — тёмный снаружи (видно на белом) и светлый внутри
(видно на чёрном). Размеры 32, 48 и 64.
"""
from __future__ import annotations

import math
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageFilter

import make_cursors as mc
from make_dragon import Layer, smooth

HERE = Path(__file__).resolve().parent
SRC = HERE / "cursors"
SIZES = (32, 48, 64)
FRAMES = 8
RATE = 7  # jiffies на кадр (1/60 с): ~8,5 кадра в секунду

EDGE = (46, 32, 20)          # внешний контур
RIM = (255, 250, 238)        # внутренний ободок
HAIR = (255, 214, 92)
HAIR_DARK = (222, 160, 36)
HAIR_LIGHT = (255, 240, 170)
SKIN = (255, 224, 200)
SKIN_SHADE = (240, 190, 165)
EYE = (70, 136, 255)
EYE_DARK = (28, 60, 160)
MOUTH = (220, 90, 110)
CHEEK = (255, 160, 170)
SAILOR = (38, 48, 92)
SAILOR_LINE = (235, 240, 255)
TIE = (222, 40, 60)
BOW = (230, 36, 58)
BOW_LIGHT = (255, 120, 136)

mc.THEMES["k8"] = {"body": ((255, 232, 150), (214, 160, 40)),
                   "accent": ((255, 130, 150), (184, 24, 56)),
                   "edge": EDGE, "rim": RIM}

TIP = (0.05, 0.05)       # кончик указательного пальца — точка нажатия
HOT_HAND = (0.40, 0.04)  # кончик поднятого пальца у «руки»


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
    return ring(ring(im, RIM, 3), EDGE, 3)


def girl(px, frame=0):
    """Девочка по плечи; frame 0…7 — кадр анимации."""
    L = Layer(px)
    ph = frame / FRAMES * 2 * math.pi
    sway = math.sin(ph) * 0.028          # хвостики качаются
    bob = math.sin(ph) * 0.006           # бант подрагивает
    blink = 1.0 if frame not in (4, 5) else (0.35 if frame == 4 else 0.08)

    cx, cy = 0.60, 0.50                  # середина головы
    # хвостики — за головой, от висков вниз
    for sgn in (-1, 1):
        x0 = cx + sgn * 0.21
        tail = smooth([(x0, cy - 0.02), (x0 + sgn * (0.10 + sway * sgn), cy + 0.20),
                       (x0 + sgn * (0.06 + sway * 2 * sgn), cy + 0.44)], 6)
        L.line(tail, 0.13, HAIR_DARK + (255,))
        L.line(tail, 0.075, HAIR + (255,))
    # резинки у хвостиков
    for sgn in (-1, 1):
        L.ellipse(cx + sgn * 0.22, cy, 0.035, 0.045, BOW + (255,))

    # плечи и матроска
    L.poly(smooth([(0.30, 0.99), (0.33, 0.84), (0.46, 0.76), (0.74, 0.76), (0.88, 0.84), (0.92, 0.99)], 4),
           SAILOR + (255,))
    # воротник — светлые линии
    L.line([(0.46, 0.80), (0.60, 0.90), (0.74, 0.80)], 0.016, SAILOR_LINE + (255,))
    # шея
    L.poly([(0.54, 0.70), (0.66, 0.70), (0.67, 0.80), (0.53, 0.80)], SKIN_SHADE + (255,))
    # галстук
    L.poly([(0.60, 0.80), (0.55, 0.86), (0.60, 0.99), (0.65, 0.86)], TIE + (255,))

    # шапка волос — за лицом: по бокам остаётся объём
    L.ellipse(cx, cy - 0.04, 0.27, 0.23, HAIR + (255,))
    # голова
    L.ellipse(cx, cy + 0.02, 0.235, 0.25, SKIN + (255,))
    L.ellipse(cx, cy + 0.10, 0.20, 0.17, SKIN + (255,))  # щёки округлее
    # румянец
    for sgn in (-1, 1):
        L.ellipse(cx + sgn * 0.13, cy + 0.12, 0.045, 0.025, CHEEK + (150,))
    # глаза — большие, голубые; моргание сжимает по высоте
    for sgn in (-1, 1):
        ex, ey = cx + sgn * 0.10, cy + 0.06
        rx, ry = 0.062, 0.075 * blink
        L.ellipse(ex, ey, rx, ry, (255, 255, 255, 255))
        L.ellipse(ex, ey + 0.01 * blink, rx * 0.8, ry * 0.85, EYE + (255,))
        L.ellipse(ex, ey + 0.02 * blink, rx * 0.5, ry * 0.5, EYE_DARK + (255,))
        if blink > 0.3:
            L.ellipse(ex - 0.02, ey - 0.03 * blink, 0.02, 0.022 * blink, (255, 255, 255, 255))
        # ресницы — верхняя линия века
        L.line([(ex - rx, ey - ry * 0.6), (ex, ey - ry * 1.05), (ex + rx, ey - ry * 0.6)], 0.018,
               EYE_DARK + (255,))
    # рот
    L.line(smooth([(cx - 0.035, cy + 0.17), (cx, cy + 0.19), (cx + 0.035, cy + 0.17)], 4), 0.014, MOUTH + (255,))

    # чёлка — только на лбу, глаза открыты; зубчики опускаются не ниже cy − 0.03
    bangs = smooth([(cx - 0.26, cy + 0.04), (cx - 0.20, cy - 0.08), (cx - 0.11, cy - 0.03), (cx - 0.03, cy - 0.09),
                    (cx + 0.06, cy - 0.03), (cx + 0.15, cy - 0.08), (cx + 0.26, cy + 0.04), (cx + 0.27, cy - 0.10),
                    (cx, cy - 0.28), (cx - 0.27, cy - 0.10)], 4)
    L.poly(bangs, HAIR + (255,))
    L.ellipse(cx - 0.06, cy - 0.17, 0.12, 0.05, HAIR_LIGHT + (150,))
    # бант — справа на голове
    bx, by = cx + 0.18, cy - 0.22 + bob
    L.poly([(bx, by), (bx - 0.10, by - 0.07), (bx - 0.09, by + 0.06)], BOW + (255,))
    L.poly([(bx, by), (bx + 0.10, by - 0.07), (bx + 0.09, by + 0.06)], BOW + (255,))
    L.ellipse(bx, by, 0.03, 0.03, BOW_LIGHT + (255,))

    # правая рука — к левому верхнему углу, указательный палец на точке нажатия
    L.line([(0.40, 0.84), (0.24, 0.44)], 0.095, SAILOR + (255,))         # рукав
    L.line([(0.25, 0.44), (0.17, 0.24)], 0.075, SKIN + (255,))            # предплечье
    L.ellipse(0.165, 0.21, 0.06, 0.055, SKIN + (255,))                    # кулачок
    L.line([(0.16, 0.19), (0.07, 0.07)], 0.045, SKIN + (255,))            # указательный палец
    L.line([(0.09, 0.09), TIP], 0.03, SKIN + (255,))
    L.line([(0.28, 0.47), (0.22, 0.36)], 0.02, SAILOR_LINE + (255,))      # манжета
    return finish(L)


def hand(px, frame=0):
    """«Рука» над ссылкой: ладонь с поднятым указательным пальцем, манжета матроски."""
    L = Layer(px)
    bob = math.sin(frame / FRAMES * 2 * math.pi) * 0.012
    # манжета и рукав снизу
    L.poly(smooth([(0.30, 0.99), (0.28, 0.74), (0.74, 0.74), (0.76, 0.99)], 2), SAILOR + (255,))
    L.line([(0.29, 0.78), (0.75, 0.78)], 0.018, SAILOR_LINE + (255,))
    # ладонь
    L.poly(smooth([(0.30, 0.78), (0.28, 0.58 + bob), (0.36, 0.48 + bob), (0.54, 0.44 + bob), (0.72, 0.50 + bob),
                   (0.76, 0.68 + bob), (0.74, 0.78)], 6), SKIN + (255,))
    # поджатые пальцы
    for cx in (0.42, 0.58, 0.72):
        L.ellipse(cx, 0.52 + bob, 0.075, 0.09, SKIN + (255,))
        L.ellipse(cx, 0.50 + bob, 0.04, 0.05, SKIN_SHADE + (120,))
    # большой палец сбоку
    L.ellipse(0.27, 0.62 + bob, 0.06, 0.08, SKIN + (255,))
    # указательный — вверх, к точке нажатия
    L.line([(0.42, 0.52 + bob), (0.40, 0.08 + bob)], 0.14, SKIN + (255,))
    L.line([(0.40, 0.20 + bob), (0.40, 0.08 + bob)], 0.10, SKIN + (255,))
    L.ellipse(0.40, HOT_HAND[1] + 0.03 + bob, 0.055, 0.05, SKIN + (255,))
    # ноготок — красный лак
    L.ellipse(0.40, HOT_HAND[1] + 0.025 + bob, 0.028, 0.02, BOW + (255,))
    return finish(L)


def ani_frames(draw, hot, tmp: Path, name: str, frames: int, sizes):
    files = []
    for i in range(frames):
        f = tmp / f"{name}{i:02d}.cur"
        mc.write_cur([draw(px, i) for px in sizes], f, hot)
        files.append(f)
    return files


def main():
    mc.SIZES = SIZES
    mc.THEME = mc.THEMES["k8"]
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        # девочка и рука — восемь кадров во всех размерах
        mc.write_ani(ani_frames(girl, TIP, tmp, "static", FRAMES, SIZES), SRC / "k8_static.ani", RATE)
        mc.write_ani(ani_frames(hand, HOT_HAND, tmp, "hand", FRAMES, SIZES), SRC / "k8_hand.ani", RATE)
        # остальные виды — общими фигурами в золотисто-красной гамме, одним кадром
        for name, (draw, hot) in mc.ROLES.items():
            imgs = []
            for px in SIZES:
                c = mc.Canvas(px)
                draw(c)
                imgs.append(c.render())
            f = tmp / f"{name}.cur"
            mc.write_cur(imgs, f, hot)
            mc.write_ani([f], SRC / f"k8_{name}.ani", RATE)
        # «помощь» — девочка со значком вопроса; «запуск» — с крутилкой
        badges = {}
        for px in SIZES:
            c = mc.Canvas(int(px * 0.46))
            mc.role_question(c)
            badges[px] = c.render()
        f = tmp / "help.cur"
        mc.write_cur([mc.compose_with_sprite(girl(px, 0), badges[px], px) for px in SIZES], f, TIP)
        mc.write_ani([f], SRC / "k8_help.ani", RATE)
        app = []
        for i in range(FRAMES):
            imgs = []
            for px in SIZES:
                b = mc.Canvas(int(px * 0.46))
                mc.role_spinner(b, i)
                imgs.append(mc.compose_with_sprite(girl(px, i), b.render(), px))
            f = tmp / f"app{i:02d}.cur"
            mc.write_cur(imgs, f, TIP)
            app.append(f)
        mc.write_ani(app, SRC / "k8_app.ani", RATE)
        wait = []
        for i in range(FRAMES):
            imgs = []
            for px in SIZES:
                c = mc.Canvas(px)
                mc.role_spinner(c, i)
                imgs.append(c.render())
            f = tmp / f"wait{i:02d}.cur"
            mc.write_cur(imgs, f, (0.5, 0.5))
            wait.append(f)
        mc.write_ani(wait, SRC / "k8_wait.ani", RATE)
    total = sum(p.stat().st_size for p in SRC.glob("k8_*.ani"))
    print(f"k8: тяночка готова, {len(list(SRC.glob('k8_*.ani')))} видов, {total // 1024} КБ, "
          f"размеры {'/'.join(map(str, SIZES))}")


def preview(out: Path):
    """Лист: кадры девочки и руки в 64 и 32 на белом и тёмном, в 3 раза крупнее."""
    cols = FRAMES
    tile64, tile32, gap = 64 * 3, 32 * 3, 8
    row_h = tile64 + gap + tile32 + gap
    sheet = Image.new("RGBA", (cols * (tile64 + gap) + gap, 4 * row_h + gap), (255, 255, 255, 255))
    y = gap
    for bg in ((255, 255, 255), (28, 28, 34)):
        for draw in (girl, hand):
            x = gap
            for i in range(cols):
                for px, yy in ((64, y), (32, y + tile64 + gap)):
                    im = draw(px, i)
                    tile = Image.new("RGBA", im.size, bg + (255,))
                    tile.alpha_composite(im)
                    tile = tile.resize((px * 3, px * 3), Image.NEAREST)
                    sheet.paste(tile, (x, yy))
                x += tile64 + gap
            y += row_h
    sheet.save(out)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--preview":
        preview(Path(sys.argv[2]))
    else:
        main()
