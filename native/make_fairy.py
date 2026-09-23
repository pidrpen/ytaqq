#!/usr/bin/env python3
"""Розовая фея: третий набор курсоров (k4).

Стрелка — сама фея с волшебной палочкой: нажимает кончик палочки, звёздочка
в левом верхнем углу, как у обычной стрелки. «Рука» над ссылками — одна
палочка со звездой, остальные виды (текст, размеры, запрет…) рисует
make_cursors.py в розово-золотой гамме, чтобы набор читался как один.

Фея крупнее прочих: программа загружает её в полтора размера системного
курсора, поэтому здесь рисуются 48 и 72 точки (100 % и 150 % масштаба).
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

import make_cursors as mc

HERE = Path(__file__).resolve().parent
SRC = HERE / "cursors"
SIZES = (48, 72)
SS = 8

PLUM = (96, 22, 74)          # контур: чтобы фею было видно на белом
SKIN = (255, 226, 212)
HAIR = (236, 72, 158)
HAIR_HI = (255, 150, 205)
DRESS_TOP = (255, 136, 198)
DRESS_BOT = (214, 52, 146)
WING = (255, 205, 238)
WING_EDGE = (206, 104, 190)
GOLD = (255, 214, 92)
GOLD_DEEP = (236, 150, 40)
WAND = (255, 244, 250)

mc.THEMES["k4"] = {"body": ((255, 186, 222), (206, 58, 146)),
                   "accent": ((255, 236, 170), (220, 150, 40)),
                   "edge": PLUM, "rim": (255, 246, 252)}


class Layer:
    """Рисуем в 0..1 на холсте в 8 раз крупнее, потом уменьшаем."""

    def __init__(self, px):
        self.px = px
        self.n = px * SS
        self.im = Image.new("RGBA", (self.n, self.n), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def p(self, pts):
        return [(x * self.n, y * self.n) for x, y in pts]

    def poly(self, pts, fill):
        self.d.polygon(self.p(pts), fill=fill)

    def ellipse(self, cx, cy, rx, ry, fill, outline=None, width=0.0):
        box = self.p([(cx - rx, cy - ry), (cx + rx, cy + ry)])
        self.d.ellipse(box, fill=fill, outline=outline,
                       width=max(1, int(width * self.n)) if outline else 0)

    def line(self, pts, w, fill):
        self.d.line(self.p(pts), fill=fill, width=max(1, int(w * self.n)), joint="curve")
        r = w * self.n / 2
        for x, y in self.p([pts[0], pts[-1]]):
            self.d.ellipse((x - r, y - r, x + r, y + r), fill=fill)


def rotated_ellipse(cx, cy, rx, ry, deg, steps=48):
    a = math.radians(deg)
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)))
    return pts


def star(cx, cy, ro, ri, points=5, rot=-90):
    pts = []
    for i in range(points * 2):
        r = ro if i % 2 == 0 else ri
        a = math.radians(rot + i * 180 / points)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def sparkle(L, cx, cy, r, fill):
    L.poly(star(cx, cy, r, r * 0.28, points=4, rot=0), fill)


def gradient_fill(size, top, bottom, y0, y1):
    g = Image.new("RGBA", (size, size), top + (255,))
    d = ImageDraw.Draw(g)
    for y in range(size):
        t = min(1.0, max(0.0, (y / size - y0) / max(1e-6, y1 - y0)))
        d.line([(0, y), (size, y)], fill=mc.lerp(top, bottom, t) + (255,))
    return g


def outline(im: Image.Image, color, grow=3) -> Image.Image:
    """Тёмный ободок снаружи силуэта — как у системных курсоров."""
    a = im.getchannel("A").point(lambda v: 255 if v > 40 else 0)
    ring = a.filter(ImageFilter.MaxFilter(grow))
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(Image.new("RGBA", im.size, color + (255,)), (0, 0), ring)
    out.alpha_composite(im)
    return out


def halo(px, cx, cy, r):
    """Мягкое розовое сияние вокруг звезды. Кладётся под контур, а не в него:
    иначе контур обводил и сияние, и вокруг звезды выходило тёмное пятно."""
    h = Layer(px)
    h.ellipse(cx, cy, r, r, (255, 160, 220, 120))
    h.im = h.im.filter(ImageFilter.GaussianBlur(h.n * 0.025))
    return h.im.resize((px, px), Image.LANCZOS)


def finish(L, glow):
    small = L.im.resize((L.px, L.px), Image.LANCZOS)
    out = glow.copy()
    out.alpha_composite(outline(small, PLUM))
    return out


def draw_star_tip(L, cx, cy, ro):
    """Звезда на кончике палочки: золото с белой серединкой."""
    L.poly(star(cx, cy, ro, ro * 0.45), GOLD_DEEP + (255,))
    L.poly(star(cx, cy, ro * 0.80, ro * 0.36), GOLD + (255,))
    L.ellipse(cx, cy, ro * 0.20, ro * 0.20, (255, 255, 245, 255))


def fairy(px):
    L = Layer(px)
    # искорки за звездой — «след» волшебства
    sparkle(L, 0.30, 0.07, 0.035, (255, 180, 225, 255))
    sparkle(L, 0.07, 0.31, 0.030, (255, 214, 92, 255))
    sparkle(L, 0.24, 0.24, 0.022, (255, 255, 255, 255))

    # крылья — за спиной, полупрозрачные, с прожилкой
    w = Layer(px)
    for cx, cy, rx, ry, deg in ((0.83, 0.40, 0.17, 0.10, -38), (0.82, 0.60, 0.12, 0.075, 28)):
        w.poly(rotated_ellipse(cx, cy, rx, ry, deg), WING + (215,))
        w.d.line(w.p(rotated_ellipse(cx, cy, rx, ry, deg) + [rotated_ellipse(cx, cy, rx, ry, deg)[0]]),
                 fill=WING_EDGE + (255,), width=max(1, int(0.022 * w.n)))
        a = math.radians(deg)
        w.line([(cx - rx * 0.7 * math.cos(a), cy - rx * 0.7 * math.sin(a)),
                (cx + rx * 0.55 * math.cos(a), cy + rx * 0.55 * math.sin(a))],
               0.012, (255, 255, 255, 230))
    L.im.alpha_composite(w.im)

    # палочка: от звезды к руке
    L.line([(0.14, 0.14), (0.50, 0.50)], 0.045, PLUM + (255,))
    L.line([(0.14, 0.14), (0.50, 0.50)], 0.026, WAND + (255,))

    # ножки
    for x0, x1 in ((0.61, 0.585), (0.70, 0.725)):
        L.line([(x0, 0.84), (x1, 0.95)], 0.036, SKIN + (255,))
        L.ellipse(x1, 0.955, 0.028, 0.018, HAIR + (255,))

    # платье-колокольчик с волнистым подолом
    dress = Layer(px)
    hem = [(0.49, 0.84)]
    for i in range(1, 7):
        x = 0.49 + i * 0.058
        y = 0.88 if i % 2 else 0.845
        hem.append((x, y))
    body = [(0.595, 0.50), (0.685, 0.50), (0.72, 0.60)] + hem[::-1] + [(0.555, 0.60)]
    dress.poly(body, (255, 255, 255, 255))
    grad = gradient_fill(dress.n, DRESS_TOP, DRESS_BOT, 0.50, 0.88)
    L.im.paste(grad, (0, 0), dress.im.getchannel("A"))
    # поясок
    L.line([(0.575, 0.585), (0.705, 0.585)], 0.022, (255, 236, 170, 255))

    # рука к палочке
    L.line([(0.60, 0.53), (0.505, 0.505)], 0.034, SKIN + (255,))
    L.ellipse(0.50, 0.50, 0.026, 0.026, SKIN + (255,))

    # голова, розовые волосы с пучком и прядкой
    L.ellipse(0.665, 0.285, 0.055, 0.050, HAIR + (255,))           # пучок
    L.ellipse(0.64, 0.39, 0.095, 0.095, HAIR + (255,))             # волосы сзади
    L.ellipse(0.635, 0.405, 0.075, 0.075, SKIN + (255,))           # лицо
    L.poly([(0.55, 0.37), (0.60, 0.315), (0.70, 0.315), (0.72, 0.37), (0.66, 0.35), (0.60, 0.36)],
           HAIR + (255,))                                           # чёлка
    L.line([(0.56, 0.38), (0.55, 0.47)], 0.03, HAIR + (255,))      # прядка
    L.ellipse(0.65, 0.29, 0.02, 0.014, HAIR_HI + (255,))           # блик
    # корона-диадема
    L.poly([(0.60, 0.315), (0.615, 0.285), (0.635, 0.305), (0.655, 0.275), (0.675, 0.305),
            (0.69, 0.285), (0.70, 0.318)], GOLD + (255,))
    # глазки и румянец
    L.ellipse(0.612, 0.41, 0.011, 0.014, PLUM + (255,))
    L.ellipse(0.658, 0.41, 0.011, 0.014, PLUM + (255,))
    L.ellipse(0.600, 0.445, 0.014, 0.008, (255, 150, 170, 200))
    L.ellipse(0.672, 0.445, 0.014, 0.008, (255, 150, 170, 200))

    draw_star_tip(L, 0.125, 0.125, 0.095)
    return finish(L, halo(px, 0.125, 0.125, 0.12))


def wand(px):
    """«Рука» над ссылкой: палочка вертикально, жмёт звезда наверху."""
    L = Layer(px)
    L.line([(0.50, 0.24), (0.62, 0.92)], 0.075, PLUM + (255,))
    L.line([(0.50, 0.24), (0.62, 0.92)], 0.045, WAND + (255,))
    for y in (0.45, 0.62, 0.79):
        x = 0.50 + (y - 0.24) * (0.12 / 0.68)
        L.line([(x - 0.03, y), (x + 0.03, y + 0.01)], 0.02, HAIR + (255,))
    sparkle(L, 0.20, 0.30, 0.05, (255, 180, 225, 255))
    sparkle(L, 0.82, 0.22, 0.045, (255, 214, 92, 255))
    sparkle(L, 0.78, 0.46, 0.03, (255, 255, 255, 255))
    draw_star_tip(L, 0.48, 0.17, 0.13)
    return finish(L, halo(px, 0.48, 0.17, 0.16))


def main():
    fairy_imgs = [fairy(px) for px in SIZES]
    mc.write_cur(fairy_imgs, SRC / "k4_static.cur", (0.125, 0.125))
    mc.write_cur([wand(px) for px in SIZES], SRC / "k4_hand.cur", (0.48, 0.17))

    # остальные виды — общими фигурами, в розово-золотой гамме
    mc.SIZES = SIZES
    mc.build_theme("k4")

    # «помощь» и «запуск программы» — фея со значком. build_theme берёт фею
    # из готового .cur, а PIL читает 32-битный курсор без прозрачности (выходил
    # чёрный квадрат), поэтому эти виды собираем из рисунка напрямую.
    badges = {}
    for px in SIZES:
        c = mc.Canvas(int(px * 0.46))
        mc.role_question(c)
        badges[px] = c.render()
    mc.write_cur([mc.compose_with_sprite(fairy(px), badges[px], px) for px in SIZES],
                 SRC / "k4_help.cur", (0.125, 0.125))
    app_frames = []
    for i in range(8):
        imgs = []
        for px in mc.ANI_SIZES:
            b = mc.Canvas(int(px * 0.46))
            mc.role_spinner(b, i)
            imgs.append(mc.compose_with_sprite(fairy(px), b.render(), px))
        f = SRC / f"k4_app{i + 1:02d}.cur"
        mc.write_cur(imgs, f, (0.125, 0.125))
        app_frames.append(f)
    mc.write_ani(app_frames, SRC / "k4_app.ani")
    print("k4: фея готова,", "/".join(map(str, SIZES)))


if __name__ == "__main__":
    main()
