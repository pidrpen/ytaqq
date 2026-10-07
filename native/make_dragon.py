#!/usr/bin/env python3
"""Зелёный дракон: четвёртый набор курсоров (k5).

Стрелка — голова дракона на длинной шее: нажимает кончик морды, он в левом
верхнем углу, как у обычной стрелки. За шеей — перепончатое крыло, внизу
закручен хвост. «Рука» над ссылками — драконья лапа, указывает длинный
коготь. Остальные виды (текст, размеры, запрет…) рисует make_cursors.py
в изумрудно-огненной гамме, чтобы набор читался как один.

Размер — как у Мечника и Рукавицы: 32, 48 и 64 точки.
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

import make_cursors as mc

HERE = Path(__file__).resolve().parent
SRC = HERE / "cursors"
SIZES = (32, 48, 64)
SS = 8

EDGE = (16, 44, 26)            # контур: чтобы дракона было видно на белом
SCALE_TOP = (120, 214, 112)
SCALE_BOT = (22, 120, 64)
BELLY = (246, 214, 120)
HORN = (252, 240, 214)
WING = (60, 150, 92)
WING_LIGHT = (150, 222, 132)
EYE = (255, 196, 40)
PUPIL = (20, 20, 20)
FIRE_OUT = (236, 72, 30)
FIRE_MID = (255, 150, 30)
FIRE_IN = (255, 236, 130)

mc.THEMES["k5"] = {"body": ((130, 222, 120), (24, 118, 62)),
                   "accent": ((255, 226, 120), (232, 96, 28)),
                   "edge": EDGE, "rim": (236, 252, 232)}

TIP = (0.05, 0.05)  # кончик морды — точка нажатия
HOT_HAND = (0.40, 0.04)  # кончик когтя у лапы


def smooth(pts, steps=8):
    """Плавная кривая через точки (Катмулл — Ром)."""
    out = []
    ext = [pts[0]] + list(pts) + [pts[-1]]
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        for k in range(steps):
            t = k / steps
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t
                                    + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3)
                             for j in range(2)))
    out.append(pts[-1])
    return out


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

    def ellipse(self, cx, cy, rx, ry, fill):
        self.d.ellipse(self.p([(cx - rx, cy - ry), (cx + rx, cy + ry)]), fill=fill)

    def line(self, pts, w, fill):
        self.d.line(self.p(pts), fill=fill, width=max(1, int(w * self.n)), joint="curve")
        r = w * self.n / 2
        for x, y in self.p([pts[0], pts[-1]]):
            self.d.ellipse((x - r, y - r, x + r, y + r), fill=fill)


def scales(L, mask_pts):
    """Залить фигуру зелёной чешуёй: сверху светлее, снизу темнее."""
    m = Layer(L.px)
    m.poly(mask_pts, (255, 255, 255, 255))
    g = Image.new("RGBA", (L.n, L.n))
    d = ImageDraw.Draw(g)
    for y in range(L.n):
        d.line([(0, y), (L.n, y)], fill=mc.lerp(SCALE_TOP, SCALE_BOT, y / L.n) + (255,))
    L.im.paste(g, (0, 0), m.im.getchannel("A"))


def outline(im: Image.Image, color, grow=3) -> Image.Image:
    """Тёмный ободок снаружи силуэта — как у системных курсоров."""
    a = im.getchannel("A").point(lambda v: 255 if v > 40 else 0)
    ring = a.filter(ImageFilter.MaxFilter(grow))
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(Image.new("RGBA", im.size, color + (255,)), (0, 0), ring)
    out.alpha_composite(im)
    return out


def finish(L):
    return outline(L.im.resize((L.px, L.px), Image.LANCZOS), EDGE)


def dragon(px):
    L = Layer(px)

    # хвост: из-под туловища вниз и колечком вправо, на конце шип
    tail = smooth([(0.66, 0.78), (0.70, 0.92), (0.84, 0.95), (0.93, 0.86)])
    L.line(tail, 0.085, SCALE_BOT + (255,))
    L.line(tail, 0.05, SCALE_TOP + (255,))
    L.poly([(0.90, 0.88), (0.99, 0.74), (0.97, 0.90)], FIRE_OUT + (255,))

    # крыло летучей мыши — за спиной, к правому верху
    sh = (0.60, 0.56)
    wrist = (0.80, 0.24)
    fingers = [(0.70, 0.06), (0.97, 0.22), (0.97, 0.50)]
    L.poly([sh, wrist, fingers[0], (0.78, 0.20), fingers[1], (0.88, 0.40), fingers[2],
            (0.80, 0.56), (0.70, 0.66)], WING + (255,))
    L.poly([wrist, (0.78, 0.20), fingers[1], (0.88, 0.40), fingers[2], (0.80, 0.56),
            (0.66, 0.56)], WING_LIGHT + (210,))
    L.line([sh, wrist], 0.035, EDGE + (255,))
    for f in fingers:
        L.line([wrist, f], 0.022, EDGE + (255,))

    # шея и туловище
    neck = smooth([(0.32, 0.30), (0.44, 0.44), (0.54, 0.58), (0.62, 0.70)])
    L.line(neck, 0.17, SCALE_BOT + (255,))
    L.line(neck, 0.13, SCALE_TOP + (255,))
    L.ellipse(0.64, 0.72, 0.15, 0.11, SCALE_BOT + (255,))
    L.ellipse(0.63, 0.70, 0.12, 0.08, SCALE_TOP + (255,))
    # светлое брюхо по нижней стороне шеи
    belly = smooth([(0.30, 0.38), (0.40, 0.52), (0.50, 0.66), (0.60, 0.78)])
    L.line(belly, 0.045, BELLY + (255,))
    # гребень по спине
    for (x, y) in ((0.44, 0.34), (0.52, 0.44), (0.60, 0.55)):
        L.poly([(x - 0.045, y - 0.02), (x + 0.03, y - 0.075), (x + 0.03, y + 0.02)],
               FIRE_MID + (255,))
    # лапки
    for x in (0.56, 0.70):
        L.line([(x, 0.78), (x - 0.02, 0.88)], 0.05, SCALE_BOT + (255,))

    # рога — назад от затылка
    L.line(smooth([(0.30, 0.17), (0.40, 0.11), (0.52, 0.10)]), 0.045, HORN + (255,))
    L.line(smooth([(0.36, 0.25), (0.46, 0.22), (0.56, 0.24)]), 0.036, HORN + (255,))

    # голова: морда клином от кончика, затылок шире
    head = smooth([(0.05, 0.05), (0.18, 0.11), (0.30, 0.15), (0.40, 0.22), (0.42, 0.32),
                   (0.34, 0.42), (0.22, 0.40), (0.14, 0.30), (0.08, 0.16), (0.05, 0.05)], 6)
    scales(L, head)
    # нижняя челюсть светлее, пасть — тёмная черта
    L.poly(smooth([(0.08, 0.13), (0.16, 0.30), (0.26, 0.40), (0.20, 0.38), (0.12, 0.28),
                   (0.07, 0.15)], 4), BELLY + (255,))
    L.line(smooth([(0.08, 0.12), (0.14, 0.24), (0.24, 0.33)]), 0.016, EDGE + (255,))
    # ноздря, глаз с вертикальным зрачком
    L.ellipse(0.12, 0.09, 0.012, 0.012, EDGE + (255,))
    L.ellipse(0.27, 0.21, 0.040, 0.034, EYE + (255,))
    L.ellipse(0.275, 0.21, 0.011, 0.028, PUPIL + (255,))
    return finish(L)


def talon(L, base, tip, bend, w=0.075):
    """Коготь: сужается от основания к острию, чуть загнут в сторону bend."""
    mx, my = (base[0] + tip[0]) / 2 + bend[0], (base[1] + tip[1]) / 2 + bend[1]
    pts = smooth([base, (mx, my), tip], 6)
    left, right = [], []
    for i, (x, y) in enumerate(pts):
        a, b = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        n = math.hypot(dx, dy) or 1
        h = w / 2 * (1 - i / (len(pts) - 1))
        left.append((x - dy / n * h, y + dx / n * h))
        right.append((x + dy / n * h, y - dx / n * h))
    L.poly(left + right[::-1], HORN + (255,))


def claw(px):
    """«Рука» над ссылкой: лапа снизу, вверх указывает длинный коготь."""
    L = Layer(px)
    # запястье и ладонь
    scales(L, smooth([(0.40, 0.99), (0.30, 0.74), (0.32, 0.56), (0.50, 0.48), (0.70, 0.52),
                      (0.80, 0.70), (0.74, 0.99)], 6))
    # поджатые пальцы с когтями
    for (x0, y0), (x1, y1), tip, bend in (
            ((0.62, 0.56), (0.76, 0.48), (0.88, 0.52), (0.0, -0.04)),
            ((0.70, 0.66), (0.84, 0.64), (0.94, 0.72), (0.01, -0.03)),
            ((0.36, 0.62), (0.24, 0.56), (0.12, 0.62), (0.0, -0.04))):
        L.line([(x0, y0), (x1, y1)], 0.12, SCALE_BOT + (255,))
        L.line([(x0, y0), (x1, y1)], 0.08, SCALE_TOP + (255,))
        talon(L, (x1, y1), tip, bend)
    # указательный палец вверх и длинный коготь — им и нажимает
    L.line([(0.48, 0.56), (0.44, 0.30)], 0.13, SCALE_BOT + (255,))
    L.line([(0.48, 0.56), (0.44, 0.30)], 0.09, SCALE_TOP + (255,))
    talon(L, (0.44, 0.30), HOT_HAND, (0.015, 0.0), w=0.09)
    # чешуйки на тыльной стороне
    for cx, cy in ((0.50, 0.68), (0.62, 0.74), (0.46, 0.82), (0.60, 0.88)):
        L.ellipse(cx, cy, 0.035, 0.022, BELLY + (255,))
    return finish(L)


def main():
    mc.write_cur([dragon(px) for px in SIZES], SRC / "k5_static.cur", TIP)
    mc.write_cur([claw(px) for px in SIZES], SRC / "k5_hand.cur", HOT_HAND)

    # остальные виды — общими фигурами, в изумрудно-огненной гамме
    mc.SIZES = SIZES
    mc.build_theme("k5")

    # «помощь» и «запуск программы» — дракон со значком. build_theme берёт
    # спрайт из готового .cur, а PIL читает 32-битный курсор без прозрачности,
    # поэтому эти виды собираем из рисунка напрямую (как у феи)
    badges = {}
    for px in SIZES:
        c = mc.Canvas(int(px * 0.46))
        mc.role_question(c)
        badges[px] = c.render()
    mc.write_cur([mc.compose_with_sprite(dragon(px), badges[px], px) for px in SIZES],
                 SRC / "k5_help.cur", TIP)
    app_frames = []
    for i in range(8):
        imgs = []
        for px in mc.ANI_SIZES:
            b = mc.Canvas(int(px * 0.46))
            mc.role_spinner(b, i)
            imgs.append(mc.compose_with_sprite(dragon(px), b.render(), px))
        f = SRC / f"k5_app{i + 1:02d}.cur"
        mc.write_cur(imgs, f, TIP)
        app_frames.append(f)
    mc.write_ani(app_frames, SRC / "k5_app.ani")
    print("k5: дракон готов,", "/".join(map(str, SIZES)))


if __name__ == "__main__":
    main()
