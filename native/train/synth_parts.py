"""Учебные чертежи деталей с конструктивными элементами: валы, втулки и фланцы, пластины, корпуса,
листовые детали с выдавками — в духе ГОСТ 2.305: виды, сечения с буквами, выносные элементы,
размеры и выноски. На каждом листе известно, какие элементы нарисованы, — это ответ модели
(тип из списка drawrec.c, размер, как подписан, сколько). Тексты и детали придуманы.

  python synth_parts.py --out data/parts --n 1200
Выход: картинки и parts.jsonl (image, tag='sheet', answer — JSON: tt, razvertka, holes, elements)."""
import argparse
import json
import math
import pathlib
import random
import re

from PIL import Image, ImageChops, ImageDraw

from synth import GOST, LOW, STEEL, des, fit_font, font, put, text_w, to_scan, tt_items, wrap, yr

LETTERS = 'АБВГДЕЖИКЛМН'


class Sheet:
    """лист с рамкой и штампом; рисование в мм от левого верхнего угла"""

    def __init__(self, r, name, mat):
        self.r = r
        self.ppm = r.choice([200, 300, 300]) / 25.4
        self.fmt = r.choice(['A4', 'A3', 'A3', 'A3'])
        self.Wmm, self.Hmm = (210, 297) if self.fmt == 'A4' else (420, 297)
        self.W, self.H = int(self.Wmm * self.ppm), int(self.Hmm * self.ppm)
        self.im = Image.new('L', (self.W, self.H), 255)
        self.d = ImageDraw.Draw(self.im)
        self.lw = max(2, int(self.ppm * 0.55))
        self.thin = max(1, self.lw // 2)
        self.face = r.choice(GOST)
        self.slant = 0.0 if r.random() < 0.35 else r.uniform(0.18, 0.3)
        self.fd = font(self.face, self.m(r.uniform(3.2, 4.4)))
        self.elements, self.holes, self.letters = [], [], list(LETTERS)
        self._stamp(name, mat)
        # поле чертежа, мм: слева рамка 20, справа и сверху 5, снизу штамп и место под ТТ
        self.ax0, self.ay0, self.ax1 = 28.0, 24.0, self.Wmm - 12.0
        self.ay1 = self.Hmm - 5 - 55 - 38

    def m(self, v):
        return v * self.ppm

    def _stamp(self, name, mat):
        d, m, lw, thin, W, H = self.d, self.m, self.lw, self.thin, self.W, self.H
        d.rectangle([m(20), m(5), W - m(5), H - m(5)], outline=0, width=lw)
        sx0, sy0 = W - m(190), H - m(60)
        d.rectangle([sx0, sy0, W - m(5), H - m(5)], outline=0, width=lw)
        for yy in (15, 40):
            d.line([(sx0 + m(65), sy0 + m(yy)), (W - m(5), sy0 + m(yy))], fill=0, width=lw)
        for xx in (7, 17, 40, 55, 65):
            d.line([(sx0 + m(xx), sy0), (sx0 + m(xx), H - m(5))], fill=0, width=thin if xx < 65 else lw)
        for k in range(1, 11):
            d.line([(sx0, sy0 + m(5 * k)), (sx0 + m(65), sy0 + m(5 * k))], fill=0, width=thin)
        d.line([(sx0 + m(135), sy0 + m(15)), (sx0 + m(135), H - m(5))], fill=0, width=lw)
        f = self.face
        dz = des(self.r)
        put(self.im, (sx0 + m(70), sy0 + m(3)), dz, font(f, m(6)), 0, self.slant)
        put(self.im, (sx0 + m(68), sy0 + m(23)), name, fit_font(f, name, m(6), m(64)), 0, self.slant)
        put(self.im, (sx0 + m(68), sy0 + m(45)), mat, fit_font(f, mat, m(3.6), m(64)), 0, self.slant)
        for t, xx, yy in (('Лит.', 137, 16), ('Масса', 152, 16), ('Масштаб', 168, 16), ('Лист', 137, 36),
                          ('Изм.', 1, 26), ('Разраб.', 1, 31), ('Пров.', 1, 36), ('Утв.', 1, 51)):
            d.text((sx0 + m(xx), sy0 + m(yy)), t, font=font(f, m(2.5)), fill=0)
        tl = Image.new('L', (int(m(70)), int(m(14))), 255)
        ImageDraw.Draw(tl).text((m(4), m(3)), dz, font=font(f, m(6)), fill=0)
        self.im.paste(tl.rotate(180), (int(m(20)), int(m(5))))
        d.rectangle([m(20), m(5), m(90), m(19)], outline=0, width=lw)
        self.sx0, self.sy0 = sx0, sy0

    # ---- примитивы (координаты в мм) ----
    def P(self, x, y):
        return (self.m(x), self.m(y))

    def line(self, pts, thin=False):
        self.d.line([self.P(*p) for p in pts], fill=0, width=self.thin if thin else self.lw)

    def poly(self, pts, thin=False):
        self.line(list(pts) + [pts[0]], thin)

    def circle(self, cx, cy, rad, thin=False):
        self.d.ellipse([self.m(cx - rad), self.m(cy - rad), self.m(cx + rad), self.m(cy + rad)], outline=0,
                       width=self.thin if thin else self.lw)

    def arc(self, cx, cy, rad, a0, a1, thin=True):
        self.d.arc([self.m(cx - rad), self.m(cy - rad), self.m(cx + rad), self.m(cy + rad)], a0, a1, fill=0,
                   width=self.thin if thin else self.lw)

    def dashes(self, x0, y0, x1, y1, pat):
        L = math.hypot(x1 - x0, y1 - y0)
        if L < 0.1:
            return
        ux, uy, s, i = (x1 - x0) / L, (y1 - y0) / L, 0.0, 0
        while s < L:
            seg = pat[i % len(pat)]
            if i % 2 == 0:
                e = min(L, s + seg)
                self.line([(x0 + ux * s, y0 + uy * s), (x0 + ux * e, y0 + uy * e)], thin=True)
            s += seg
            i += 1

    def axis(self, x0, y0, x1, y1):
        self.dashes(x0, y0, x1, y1, [8, 1.2, 0.6, 1.2])

    def hidden(self, x0, y0, x1, y1):
        self.dashes(x0, y0, x1, y1, [2.5, 1.2])

    def text(self, x, y, s, f=None, center=False):
        f = f or self.fd
        if center:
            x -= text_w(f, s) / self.ppm / 2
        put(self.im, self.P(x, y), s, f, 0, self.slant)

    def arrow(self, x, y, ang, size=2.6):
        a, w = math.radians(ang), 0.33
        pts = [(x, y), (x - size * math.cos(a - w), y - size * math.sin(a - w)),
               (x - size * math.cos(a + w), y - size * math.sin(a + w))]
        self.d.polygon([self.P(*p) for p in pts], fill=0)

    def dim(self, x0, y0, x1, y1, txt, off):
        """линейный размер между точками, вынесенный на off мм (горизонтальный или вертикальный)"""
        if abs(y1 - y0) < abs(x1 - x0):  # горизонтальный
            y = min(y0, y1) - off if off > 0 else max(y0, y1) - off
            self.line([(x0, y0), (x0, y - 1.5 * (1 if off > 0 else -1))], thin=True)
            self.line([(x1, y1), (x1, y - 1.5 * (1 if off > 0 else -1))], thin=True)
            self.line([(x0, y), (x1, y)], thin=True)
            self.arrow(x0, y, 180)
            self.arrow(x1, y, 0)
            self.text((x0 + x1) / 2, y - 5.2, txt, center=True)
        else:
            x = min(x0, x1) - off if off > 0 else max(x0, x1) - off
            self.line([(x0, y0), (x - 1.5, y0)], thin=True)
            self.line([(x1, y1), (x - 1.5, y1)], thin=True)
            self.line([(x, y0), (x, y1)], thin=True)
            self.arrow(x, y0, -90)
            self.arrow(x, y1, 90)
            img = Image.new('L', (int(text_w(self.fd, txt) + self.m(2)), int(self.m(6))), 255)
            put(img, (self.m(1), 0), txt, self.fd, 0, self.slant)
            img = img.rotate(90, expand=True)
            px, py = self.P(x - 5.6, (y0 + y1) / 2)
            box = (int(px), int(py - img.size[1] / 2))
            self.im.paste(ImageChops.darker(self.im.crop((box[0], box[1], box[0] + img.size[0], box[1] + img.size[1])),
                                            img), box)

    def leader(self, x, y, txt, dx, dy):
        """выноска: стрелка в точку (x, y), полка с надписью"""
        ex, ey = x + dx, y + dy
        self.line([(ex, ey), (x, y)], thin=True)
        self.arrow(x, y, math.degrees(math.atan2(y - ey, x - ex)))
        w = text_w(self.fd, txt) / self.ppm + 2
        x2 = ex + (w if dx >= 0 else -w)
        self.line([(ex, ey), (x2, ey)], thin=True)
        self.text(min(ex, x2) + 1, ey - 5.4, txt)

    def hatch(self, pts_mm, step=2.2):
        """штриховка многоугольника под 45°"""
        px = [self.P(*p) for p in pts_mm]
        x0, y0 = int(min(p[0] for p in px)) - 2, int(min(p[1] for p in px)) - 2
        x1, y1 = int(max(p[0] for p in px)) + 2, int(max(p[1] for p in px)) + 2
        w, h = x1 - x0, y1 - y0
        if w < 4 or h < 4:
            return
        mask = Image.new('L', (w, h), 0)
        ImageDraw.Draw(mask).polygon([(p[0] - x0, p[1] - y0) for p in px], fill=255)
        lines = Image.new('L', (w, h), 0)
        ld, s = ImageDraw.Draw(lines), self.m(step)
        k = -h
        while k < w:
            ld.line([(k, h), (k + h, 0)], fill=255, width=self.thin)
            k += s
        self.im.paste(0, (x0, y0), ImageChops.multiply(mask, lines))

    def cut(self, x, ytop, ybot):
        """линия сечения с буквой: штрихи сверху и снизу, стрелки к виду"""
        L = self.letters.pop(0)
        for y, sgn in ((ytop - 4, -1), (ybot + 4, 1)):
            self.line([(x, y - 3 * sgn * -1), (x, y + 3 * sgn * -1)])
            self.line([(x, y), (x - 6, y)], thin=True)
            self.arrow(x - 6, y, 180)
            self.text(x - 9, y - (9 if sgn < 0 else -1), L)
        return L

    def el(self, t, size='', count=1):
        self.elements.append({'type': t, 'size': size, 'count': count})

    def finish(self, tt):
        r, m = self.r, self.m
        if r.random() < 0.75:
            x, y = self.Wmm - 45, 10
            self.line([(x, y + 2), (x + 1.5, y + 5), (x + 4, y)], thin=True)
            self.text(x + 5, y, r.choice(['Ra 6,3', 'Ra 12,5', 'Ra 3,2', '6,3', '12,5']) + ' (  )')
        f_tt = font(self.face, m(r.uniform(3.5, 4.6)))
        lines = []
        for t in tt:
            ws = wrap(f_tt, t, m(178))
            lines += [ws[0]] + ['   ' + w for w in ws[1:]]
        lh = f_tt.getbbox('Ж')[3] * 1.55
        y = self.sy0 - m(r.uniform(4, 8)) - lh * len(lines)
        for s in lines:
            put(self.im, (self.sx0 + m(r.uniform(0, 6)), y), s, f_tt, 0, self.slant)
            y += lh
        im = self.im
        if r.random() < 0.5:
            im = im.rotate(r.uniform(-0.5, 0.5), fillcolor=255, resample=Image.BILINEAR)
        im = to_scan(im, r, dither=r.random() < 0.3)
        s = -(-max(self.W, self.H) // 1600)  # dr_img_part: целое уменьшение до 1600
        if s > 1:
            im = im.resize((self.W // s, self.H // s), Image.BOX)
        ans = {'tt': tt, 'razvertka': getattr(self, 'razv', False), 'holes': self.holes, 'elements': self.elements}
        return im, json.dumps(ans, ensure_ascii=False)


def num(v):
    return (f'{v:.1f}'.rstrip('0').rstrip('.')).replace('.', ',')


def key_b(d):
    """ширина шпоночного паза по диаметру, ГОСТ 23360"""
    for dm, b in ((12, 4), (17, 5), (22, 6), (30, 8), (38, 10), (44, 12), (50, 14), (58, 16), (65, 18)):
        if d <= dm:
            return b
    return 20


def circle_pts(cx, cy, rad, a0, a1, n=48):
    return [(cx + rad * math.cos(a0 + (a1 - a0) * i / n), cy + rad * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def notch_circle(cx, cy, R, b, t, inward=True):
    """окружность с прямоугольным пазом сверху: вал со шпоночным пазом (паз внутрь) или отверстие
    со шпоночным пазом (паз наружу)"""
    dl = math.asin(min(0.95, b / 2 / R))
    pts = circle_pts(cx, cy, R, -math.pi / 2 + dl, 3 * math.pi / 2 - dl)
    ye = cy - R * math.cos(dl)
    yb = cy - R + t if inward else cy - R - t
    return pts + [(cx - b / 2, ye), (cx - b / 2, yb), (cx + b / 2, yb), (cx + b / 2, ye)]


def flats_circle(cx, cy, R, h, two):
    """окружность с лыской (хордой) сверху, h — от центра до лыски; two — и снизу"""
    a = math.asin(h / R)
    top = circle_pts(cx, cy, R, -a, math.pi + a) if not two else None
    if not two:
        return [(cx + math.sqrt(R * R - h * h), cy - h)] + circle_pts(cx, cy, R, -a, math.pi + a)
    right = circle_pts(cx, cy, R, -a, a, 12)
    left = circle_pts(cx, cy, R, math.pi - a, math.pi + a, 12)
    return right + left


def spline_circle(cx, cy, Ro, Ri, z, outward_teeth=True):
    """шлицевой профиль: z зубьев между Ri и Ro"""
    pts = []
    for k in range(z):
        a0 = 2 * math.pi * k / z
        w = math.pi / z * 0.9
        for a, rr in ((a0 - w / 2, Ri), (a0 - w / 2, Ro), (a0 + w / 2, Ro), (a0 + w / 2, Ri)):
            if not outward_teeth:
                rr = Ro if rr == Ri else Ri
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts


# ---- семейства деталей ----

def shaft(r):
    names = ['Вал', 'Валик', 'Ось', 'Вал ведомый', 'Вал ведущий', 'Вал-шестерня', 'Шпиндель', 'Палец', 'Шток', 'Ось блока']
    g, st, y = r.choice(STEEL[:12])
    sh = Sheet(r, r.choice(names), f'{g} {st}-{yr(r, y)}')
    n = r.randint(2, 5)
    # ступени: к середине толще, к концам тоньше, соседние — разные
    pool = sorted(r.sample([16, 20, 22, 25, 28, 30, 32, 35, 40, 45, 50, 55, 60], n))
    peak = r.randrange(n)
    ds = [0] * n
    ds[peak] = pool[-1]
    left, right = list(range(peak - 1, -1, -1)), list(range(peak + 1, n))
    rest = pool[:-1][::-1]
    for j, idx in enumerate(sorted(left + right, key=lambda q: abs(q - peak))):
        ds[idx] = rest[j]
    Ls = [r.uniform(15, 60) for _ in range(n)]
    total = sum(Ls)
    room = (sh.ax1 - sh.ax0) * (0.55 if sh.fmt == 'A3' else 0.75)
    k = min(room / total, 40 / max(ds) * 1.4, 2.0)
    cx0, cy = sh.ax0 + 15, sh.ay0 + max(45, max(ds) / 2 * k + 32)  # размер длины не лезет на рамку обозначения
    xs, x = [], cx0
    for L in Ls:
        xs.append((x, x + L * k))
        x += L * k
    # контур ступеней
    for i, (xa, xb) in enumerate(xs):
        R = ds[i] / 2 * k
        sh.line([(xa, cy - R), (xb, cy - R)])
        sh.line([(xa, cy + R), (xb, cy + R)])
        prevR = ds[i - 1] / 2 * k if i else 0
        sh.line([(xa, cy - max(R, prevR)), (xa, cy + max(R, prevR))])
    xe = xs[-1][1]
    sh.line([(xe, cy - ds[-1] / 2 * k), (xe, cy + ds[-1] / 2 * k)])
    sh.axis(cx0 - 6, cy, xe + 6, cy)
    sh.dim(cx0, cy - ds[0] / 2 * k, xe, cy - ds[-1] / 2 * k, num(total), max(ds) / 2 * k + 8 - ds[0] / 2 * k)
    for i in r.sample(range(n), min(n, r.randint(1, 3))):
        xa, xb = xs[i]
        R = ds[i] / 2 * k
        sh.dim(xa + (xb - xa) * 0.5, cy - R, xa + (xb - xa) * 0.5, cy + R, 'Ø' + str(ds[i]) + r.choice(['', 'h9', 'k6', 'f7', '']), -2 if r.random() < 0.5 else 0.01)
    if r.random() < 0.6:  # фаска на левом торце с выноской; фаски — не элемент
        c, R0 = r.choice([1, 1.6, 2]) * k, ds[0] / 2 * k
        sh.line([(cx0, cy - R0 + c), (cx0 + c, cy - R0)])
        sh.line([(cx0, cy + R0 - c), (cx0 + c, cy + R0)])
        sh.line([(cx0 + c, cy - R0), (cx0 + c, cy + R0)], thin=True)
        sh.leader(cx0 + c / 2, cy + R0 - c / 2, r.choice(['1x45°', '2 фаски 1x45°', '1,6x45°']), -8, 12)
    sx = sh.ax0 + 20  # виды и сечения — ниже главного вида
    sy = cy + max(ds) / 2 * k + 40
    used = set()
    # чаще то, что важно для маршрута и часто на чертежах: шпоночные пазы, лыски, резьбы, отверстия
    pool = ['key'] * 4 + ['flat'] * 3 + ['thr'] * 2 + ['thrin'] * 2 + ['cross'] * 2 + ['groove'] * 2 +         ['knurl', 'spline', 'gear', 'hex']
    feats = []
    for _ in range(r.choice([0, 1, 1, 2, 2, 2, 3, 3, 4])):
        f = r.choice(pool)
        if f not in feats:
            feats.append(f)
    for f in feats:
        i = r.randrange(n)
        if (f, i) in used or sx > sh.ax1 - 30 or sy > sh.ay1 - 10:
            continue
        used.add((f, i))
        xa, xb = xs[i]
        R = ds[i] / 2 * k
        d0 = ds[i]
        if f == 'key':
            b = key_b(d0)
            t = max(2, round(b * 0.6))
            l1, l2 = xa + (xb - xa) * 0.2, xb - (xb - xa) * 0.2
            bw = b * k / 2
            sh.line([(l1 + bw, cy - bw), (l2 - bw, cy - bw)])
            sh.line([(l1 + bw, cy + bw), (l2 - bw, cy + bw)])
            sh.arc(l1 + bw, cy, bw, 90, 270, thin=False)
            sh.arc(l2 - bw, cy, bw, -90, 90, thin=False)
            L = sh.cut((l1 + l2) / 2, cy - R, cy + R)
            Rs = min(R, 16)
            ccx, ccy = sx + Rs + 4, sy + Rs + 8
            pts = notch_circle(ccx, ccy, Rs, b * Rs / (d0 / 2) / 2 * 2 / 2 * 2 if False else b / d0 * 2 * Rs, t / d0 * 2 * Rs)
            sh.hatch(pts)
            sh.poly(pts)
            sh.axis(ccx - Rs - 3, ccy, ccx + Rs + 3, ccy)
            sh.text(ccx, ccy - Rs - 14, f'{L}–{L}', center=True)
            size = f'{b}' + r.choice(['N9', 'P9', 'Js9', 'N9', ''])
            sh.leader(ccx + b / d0 * Rs, ccy - Rs + 1, size, 10, -9)
            bws, tts = b / d0 * Rs, t / d0 * 2 * Rs
            sh.dim(ccx - bws, ccy - Rs + tts, ccx, ccy + Rs, num(d0 - t), Rs - bws + 5)
            cnt = 2 if r.random() < 0.15 else 1
            if cnt == 2:
                sh.text(ccx + 6, ccy - Rs - 14, '2 места')
            sh.el('шпоночный паз', size, cnt)
            sx += 2 * Rs + 30
        elif f == 'flat':
            two = r.random() < 0.35
            h = R * r.uniform(0.55, 0.8)
            sh.line([(xa + 1, cy - h), (xb - 1, cy - h)])
            if two:
                sh.line([(xa + 1, cy + h), (xb - 1, cy + h)])
            L = sh.cut((xa + xb) / 2, cy - R, cy + R)
            Rs = min(R, 16)
            ccx, ccy = sx + Rs + 4, sy + Rs + 8
            hs = h / R * Rs
            pts = flats_circle(ccx, ccy, Rs, hs, two)
            sh.hatch(pts)
            sh.poly(pts)
            sh.text(ccx, ccy - Rs - 14, f'{L}–{L}', center=True)
            v = round(d0 / 2 + h / k) if not two else round(2 * h / k)
            size = f'{v}' + r.choice(['h10', 'h12', '', 'h11'])
            sh.dim(ccx + Rs + 2, ccy - hs, ccx + Rs + 2, ccy + (Rs if not two else hs), size, -6)
            sh.el('лыска', size, 2 if two else 1)
            sx += 2 * Rs + 34
        elif f == 'thr' and i in (0, n - 1):
            p = r.choice(['', 'x1,5', 'x2', 'x1,25', ''])
            size = f'M{d0}{p}' + r.choice(['', '-6g', '-8g', ''])
            rm = R * 0.84
            ta, tb = (xa, xa + (xb - xa) * 0.7) if i == 0 else (xb - (xb - xa) * 0.7, xb)
            sh.line([(ta, cy - rm), (tb, cy - rm)], thin=True)
            sh.line([(ta, cy + rm), (tb, cy + rm)], thin=True)
            xt = tb if i == 0 else ta
            sh.line([(xt, cy - R), (xt, cy + R)])
            sh.leader((ta + tb) / 2, cy - R, size, -14 if i == 0 else 14, -12)
            sh.el('резьба наружная', size, 1)
        elif f == 'thrin' and i in (0, n - 1):
            dt = min(d0 - 6, r.choice([6, 8, 10, 12, 16, 20]))
            if dt < 4:
                continue
            size = f'M{dt}' + r.choice(['-7H', '-6H', '', 'x1-7H'])
            dep = min((xb - xa) * 0.8, dt * k * 2)
            x0e = xa if i == 0 else xb
            sgn = 1 if i == 0 else -1
            for yy in (dt / 2 * k * 0.85, -dt / 2 * k * 0.85):
                sh.hidden(x0e, cy + yy, x0e + sgn * dep, cy + yy)
            sh.hidden(x0e, cy + dt / 2 * k, x0e + sgn * dep * 0.8, cy + dt / 2 * k)
            sh.hidden(x0e, cy - dt / 2 * k, x0e + sgn * dep * 0.8, cy - dt / 2 * k)
            sh.leader(x0e + sgn * 2, cy - dt / 2 * k, size, -14 * sgn, -14 - R)
            sh.el('резьба внутренняя', size, 1)
        elif f == 'cross':
            dh = r.choice([4, 5, 6, 8, 10])
            if dh * k > R * 1.2:
                continue
            xc = (xa + xb) / 2
            sh.circle(xc, cy, dh / 2 * k)
            sh.axis(xc, cy - dh / 2 * k - 3, xc, cy + dh / 2 * k + 3)
            size = f'Ø{dh}' + r.choice(['', 'H7', '', 'H9'])
            cnt = 2 if r.random() < 0.2 else 1
            txt = f'{cnt} отв. {size}' if cnt > 1 else size
            sh.leader(xc + dh / 2 * k * 0.7, cy - dh / 2 * k * 0.7, txt, 12, -R - 10)
            if 'отв' in txt:
                sh.holes.append(txt)
            sh.el('отверстие', size, cnt)
        elif f == 'groove' and i < n - 1 and ds[i + 1] != ds[i]:
            xg = xb - 2.5
            w = 2.0
            for sgn in (-1, 1):
                sh.line([(xg - w / 2, cy + sgn * R), (xg - w / 2, cy + sgn * (R - 1.2))], thin=False)
                sh.line([(xg - w / 2, cy + sgn * (R - 1.2)), (xg + w / 2, cy + sgn * (R - 1.2))], thin=False)
            L = sh.letters.pop(0)
            sh.circle(xg, cy - R, 4, thin=True)
            sh.leader(xg - 3, cy - R - 3, L, -10, -10)
            gx, gy = sx + 14, sy + 12
            if gx < sh.ax1 - 30:
                sh.text(gx, gy - 12, f'{L} ({r.choice(["2:1", "4:1", "5:1"])})')
                sh.line([(gx - 12, gy + 6), (gx - 3, gy + 6), (gx - 3, gy + 12)])
                sh.arc(gx + 1, gy + 12, 4, 0, 180, thin=False)
                sh.line([(gx + 5, gy + 12), (gx + 5, gy), (gx + 16, gy)])
                sh.text(gx + 7, gy + 14, r.choice(['R0,5', 'R1', '2', 'R0,3']))
                sx += 40
            sh.el('канавка', '', 1)
        elif f == 'knurl':
            pts = [(xa + 1, cy - R), (xb - 1, cy - R), (xb - 1, cy + R), (xa + 1, cy + R)]
            sh.hatch(pts, 1.6)
            flip = [(p[0], 2 * cy - p[1]) for p in pts]
            sh.hatch([(2 * xa + 1 - p[0] + (xb - xa) - 1, p[1]) for p in pts], 1.6) if False else None
            px = [(xb - 1 - (p[0] - xa - 1), p[1]) for p in pts]
            img_pts = px
            sh.hatch(img_pts, 1.6)
            sh.leader((xa + xb) / 2, cy + R - 1, r.choice(['Рифление сетчатое 1,0 ГОСТ 21474-75',
                                                           'Рифление прямое 1,2 ГОСТ 21474-75']), 12, R + 12)
            sh.el('рифление', '', 1)
            del flip
        elif f == 'spline':
            zc = r.choice([6, 8, 10])
            dd, DD = d0 - r.choice([4, 6]), d0
            size = r.choice([f'{zc}х{dd}х{DD}', f'D-{zc}x{dd}x{DD}x{r.choice([4, 5, 6])}'])
            sh.line([(xa + 1, cy - R * 0.86), (xb - 1, cy - R * 0.86)], thin=True)
            sh.line([(xa + 1, cy + R * 0.86), (xb - 1, cy + R * 0.86)], thin=True)
            L = sh.cut((xa + xb) / 2, cy - R, cy + R)
            Rs = min(R, 16)
            ccx, ccy = sx + Rs + 4, sy + Rs + 8
            pts = spline_circle(ccx, ccy, Rs, Rs * dd / DD, zc)
            sh.hatch(pts)
            sh.poly(pts)
            sh.text(ccx, ccy - Rs - 14, f'{L}–{L}', center=True)
            sh.leader(ccx + Rs * 0.7, ccy - Rs * 0.7, size, 10, -8)
            sh.el('шлицы', size, 1)
            sx += 2 * Rs + 34
        elif f == 'gear' and 'gear_done' not in used:
            used.add('gear_done')
            mod, z = r.choice([1.5, 2, 2.5, 3]), r.randint(18, 40)
            sh.axis(xa, cy - R + 1.5, xb, cy - R + 1.5)
            tx, ty = sh.ax1 - 75, sh.ay0
            rows = [('Модуль', 'm', num(mod)), ('Число зубьев', 'z', str(z)), ('Исходный контур', '—', 'ГОСТ 13755-81'),
                    ('Степень точности', '—', r.choice(['8-B', '7-C', '9-B']))]
            for j, (a, b, c) in enumerate(rows):
                sh.line([(tx, ty + j * 8), (tx + 70, ty + j * 8)], thin=True)
                sh.text(tx + 1, ty + j * 8 + 1, a, font(sh.face, sh.m(3)))
                sh.text(tx + 42, ty + j * 8 + 1, b, font(sh.face, sh.m(3)))
                sh.text(tx + 50, ty + j * 8 + 1, c, font(sh.face, sh.m(3)))
            sh.line([(tx, ty + 32), (tx + 70, ty + 32)], thin=True)
            for xx in (0, 40, 48, 70):
                sh.line([(tx + xx, ty), (tx + xx, ty + 32)], thin=True)
            sh.el('зубья', f'm {num(mod)} z {z}', 1)
        elif f == 'hex' and i in (0, n - 1):
            S = d0 - r.choice([2, 4, 6])
            if S < 8:
                continue
            ccx, ccy = sx + 18, sy + 18
            Rh = min(R, 15)
            pts = [(ccx + Rh * math.cos(math.pi / 6 + j * math.pi / 3), ccy + Rh * math.sin(math.pi / 6 + j * math.pi / 3)) for j in range(6)]
            sh.poly(pts)
            sh.circle(ccx, ccy, Rh * 0.866 * 0.5, thin=True)
            sh.dim(ccx - Rh * 0.866, ccy + Rh + 2, ccx + Rh * 0.866, ccy + Rh + 2, f'{S}' if r.random() < 0.6 else f'S{S}', -6)
            for yy in (-0.5, 0.5):
                sh.line([(xa + 1, cy + yy * R), (xb - 1, cy + yy * R)], thin=True)
            sh.el('грани под ключ', f'{S}' if r.random() < 0.6 else f'S{S}', 1)
            sh.elements[-1]['size'] = sh.elements[-1]['size']
            sx += 50
    return sh


def flange(r):
    names = ['Фланец', 'Втулка', 'Крышка', 'Ступица', 'Шкив', 'Втулка фланцевая', 'Крышка подшипника', 'Полумуфта']
    g, st, y = r.choice(STEEL)
    sh = Sheet(r, r.choice(names), f'{g} {st}-{yr(r, y)}' if not st.startswith('ТУ') else f'{g} {st}')
    D = r.choice([60, 70, 80, 90, 100, 120, 140])
    Dh, db = D * r.uniform(0.45, 0.65), D * r.uniform(0.2, 0.38)
    Lf, Lh = r.uniform(8, 16), r.uniform(15, 40)
    k = min(70 / D, 1.4)
    cx, cy = sh.ax0 + 25, sh.ay0 + 25 + D / 2 * k
    R, Rh, Rb = D / 2 * k, Dh / 2 * k, db / 2 * k
    x1, x2 = cx + Lf * k, cx + (Lf + Lh) * k
    upper = [(cx, cy - Rb), (cx, cy - R), (x1, cy - R), (x1, cy - Rh), (x2, cy - Rh), (x2, cy - Rb)]
    sh.hatch(upper)
    sh.line(upper)
    sh.line([(cx, cy - Rb), (x2, cy - Rb)])
    sh.line([(cx, cy), (cx, cy + R), (x1, cy + R), (x1, cy + Rh), (x2, cy + Rh), (x2, cy)])
    sh.hidden(cx, cy + Rb, x2, cy + Rb)
    sh.axis(cx - 6, cy, x2 + 6, cy)
    sh.dim(cx, cy - R, cx, cy + R, 'Ø' + str(D), 8)
    sh.dim(cx, cy - R, x2, cy - Rh, num(Lf + Lh), R + 8 - R)
    # вид слева: отверстия по окружности, отверстие в центре
    ex, ey = x2 + 30 + R, cy
    sh.circle(ex, ey, R)
    sh.circle(ex, ey, Rh, thin=True)
    sh.axis(ex - R - 4, ey, ex + R + 4, ey)
    sh.axis(ex, ey - R - 4, ex, ey + R + 4)
    nh = r.choice([3, 4, 4, 6, 8])
    pcd = (R + Rh) / 2
    sh.arc(ex, ey, pcd, 0, 360)
    thr = r.random() < 0.35
    dh = r.choice([6.6, 9, 11, 13.5, 9, 7]) if not thr else r.choice([6, 8, 10, 12])
    rr = dh / 2 * k
    for j in range(nh):
        a = 2 * math.pi * j / nh + math.pi / 2
        hx, hy = ex + pcd * math.cos(a), ey + pcd * math.sin(a)
        if thr:
            sh.circle(hx, hy, rr * 0.84)
            sh.arc(hx, hy, rr, 0, 270)
        else:
            sh.circle(hx, hy, rr)
    size = (f'M{int(dh)}' + r.choice(['-7H', '-6H', ''])) if thr else ('Ø' + num(dh))
    txt = f'{nh} отв. {size}' if r.random() < 0.8 else f'{size} {nh} отв.'
    a = math.pi / 2
    sh.leader(ex + pcd * math.cos(a) + rr * 0.7, ey + pcd * math.sin(a) + rr * 0.7, txt, 14, 12)
    sh.holes.append(txt)
    sh.el('резьбовое отверстие' if thr else 'отверстие', size, nh)
    # отверстие в центре: гладкое, со шпоночным пазом или шлицами
    kind = r.choice(['plain', 'plain', 'key', 'spline', 'thr'])
    if kind == 'key':
        b = key_b(db)
        t1 = max(1.5, round(b * 0.45, 1))
        pts = notch_circle(ex, ey, Rb, b * k, t1 * k, inward=False)
        sh.poly(pts)
        size = f'{b}' + r.choice(['Js9', 'D10', 'P9', ''])
        sh.leader(ex + b * k / 2, ey - Rb - t1 * k, size, 12, -10)
        xk = ex - b * k / 2  # d + t2 — размер сквозь отверстие у левой стенки паза
        sh.dim(xk, ey - Rb - t1 * k, xk, ey + math.sqrt(Rb * Rb - (b * k / 2) ** 2), num(db + t1), 2)
        sh.el('шпоночный паз в отверстии', size, 1)
    elif kind == 'spline':
        zc = r.choice([6, 8, 10])
        pts = spline_circle(ex, ey, Rb * 1.18, Rb, zc, outward_teeth=False)
        sh.poly(pts)
        size = f'{zc}х{round(db)}х{round(db * 1.18)}'
        sh.leader(ex + Rb * 0.8, ey - Rb * 0.8, size, 12, -12)
        sh.el('шлицы в отверстии', size, 1)
    elif kind == 'thr':
        sh.circle(ex, ey, Rb * 0.86)
        sh.arc(ex, ey, Rb, 0, 270)
        size = f'M{round(db)}' + r.choice(['x1,5-7H', '-7H', 'x2'])
        sh.leader(ex + Rb * 0.7, ey - Rb * 0.7, size, 12, -12)
        sh.el('резьба внутренняя', size, 1)
    else:
        sh.circle(ex, ey, Rb)
        sh.dim(ex - Rb, ey + R + 6, ex + Rb, ey + R + 6, 'Ø' + str(round(db)) + r.choice(['H7', 'H8', '', 'H9']), -4)
    if r.random() < 0.35:
        sh.el('канавка', '', 1)
        xg = x1 + 1.5
        sh.line([(xg - 1, cy - Rh), (xg - 1, cy - Rh + 1), (xg + 1, cy - Rh + 1), (xg + 1, cy - Rh)], thin=False)
    return sh


def plate(r, body=False):
    if body:
        names = ['Корпус', 'Кронштейн', 'Основание', 'Плита', 'Стойка', 'Опора']
        g, st, y = r.choice(STEEL)
        mat = f'{g} {st}-{yr(r, y)}' if not st.startswith('ТУ') else f'{g} {st}'
    else:
        names = ['Пластина', 'Планка', 'Плита', 'Накладка', 'Крышка', 'Прокладка', 'Фланец', 'Косынка']
        S = r.choice([2, 3, 4, 5, 6, 8, 10, 12])
        g, ls, ly = r.choice(LOW)
        mat = f'Лист {S} ГОСТ 19903-{yr(r, 2015)} / {g} {ls}-{yr(r, ly)}'
    sh = Sheet(r, r.choice(names), mat)
    Wp, Hp = r.uniform(80, 220), r.uniform(50, 140)
    k = min((sh.ax1 - sh.ax0 - 60) / Wp, (sh.ay1 - sh.ay0 - 30) / Hp, 1.5)
    x0, y0 = sh.ax0 + 20, sh.ay0 + 18
    x1, y1 = x0 + Wp * k, y0 + Hp * k
    rc = r.choice([0, 0, 3, 5, 10]) * k
    if rc:
        sh.line([(x0 + rc, y0), (x1 - rc, y0)])
        sh.line([(x0 + rc, y1), (x1 - rc, y1)])
        sh.line([(x0, y0 + rc), (x0, y1 - rc)])
        sh.line([(x1, y0 + rc), (x1, y1 - rc)])
        for (cx_, cy_, a0) in ((x0 + rc, y0 + rc, 180), (x1 - rc, y0 + rc, 270), (x1 - rc, y1 - rc, 0), (x0 + rc, y1 - rc, 90)):
            sh.arc(cx_, cy_, rc, a0, a0 + 90, thin=False)
    else:
        sh.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
    sh.dim(x0, y0, x1, y0, num(round(Wp)), 8)
    sh.dim(x0, y0, x0, y1, num(round(Hp)), 8)
    # вид сбоку — толщина
    T = (r.choice([2, 3, 4, 5, 6, 8, 10, 12]) if not body else r.uniform(15, 40)) * k
    sh.poly([(x1 + 18, y0), (x1 + 18 + T, y0), (x1 + 18 + T, y1), (x1 + 18, y1)])
    if not body:
        sh.text(x1 + 20 + T, y1 + 2, r.choice([f'S={num(T / k)}*', f'{num(T / k)}*', f'S{num(T / k)}']))
    # отверстия
    kinds = r.sample(['hole', 'hole', 'thr', 'slot'], r.choice([1, 2, 2, 3]))
    rows = r.sample([0.18, 0.4, 0.62, 0.84], 4)  # у каждого вида элементов — свой ряд, чтобы не налезали
    for kind in kinds:
        if not rows:
            break
        row = rows.pop()
        if kind == 'hole':
            dh = r.choice([5, 6.6, 7, 9, 11, 13, 14, 17, 18, 20, 22])
            dh = min(dh, (y1 - y0) / k * 0.16)
            dh = max(3, round(dh * 2) / 2)
            nx = r.choice([1, 2, 2, 3, 4])
            ys = [y0 + (y1 - y0) * row]
            xs_ = [x0 + (x1 - x0) * (j + 1) / (nx + 1) for j in range(nx)]
            for xx in xs_:
                for yy in ys:
                    sh.circle(xx, yy, dh / 2 * k)
                    sh.axis(xx - dh / 2 * k - 2, yy, xx + dh / 2 * k + 2, yy)
                    sh.axis(xx, yy - dh / 2 * k - 2, xx, yy + dh / 2 * k + 2)
            cnt = nx * len(ys)
            size = 'Ø' + num(dh) + r.choice(['', '', 'H7', 'H12'])
            txt = r.choice([f'{cnt} отв. {size}', f'{size} {cnt} отв.', f'{cnt}отв. {size}']) if cnt > 1 else size
            sh.leader(xs_[-1] + dh / 2 * k * 0.7, ys[0] - dh / 2 * k * 0.7, txt, 12, -10)
            if 'отв' in txt:
                sh.holes.append(txt)
            sh.el('отверстие', size, cnt)
        elif kind == 'thr':
            dt = r.choice([4, 5, 6, 8, 10, 12, 16])
            cnt = r.choice([2, 3, 4, 6])
            dt = min(dt, int((y1 - y0) / k * 0.16) or 4)
            xs_ = [x0 + (x1 - x0) * (j + 1) / (cnt + 1) for j in range(cnt)]
            yy = y0 + (y1 - y0) * row
            for xx in xs_:
                sh.circle(xx, yy, dt / 2 * k * 0.84)
                sh.arc(xx, yy, dt / 2 * k, 0, 270)
            size = f'M{dt}' + r.choice(['-7H', '-6H', ''])
            txt = r.choice([f'{cnt} отв. {size}', f'{cnt}отв {size}'])
            sh.leader(xs_[0] + dt / 2 * k * 0.7, yy - dt / 2 * k * 0.7, txt, -12, -12)
            sh.holes.append(txt)
            sh.el('резьбовое отверстие', size, cnt)
        elif kind == 'slot':
            w = r.choice([6, 8, 10, 12, 14, 18])
            w = max(4, min(w, int((y1 - y0) / k * 0.14)))
            Ls = r.uniform(20, 50)
            cnt = r.choice([1, 2])
            for j in range(cnt):
                yy = y0 + (y1 - y0) * row
                xa = x0 + (x1 - x0) * (0.15 + 0.45 * j)
                xb = min(xa + Ls * k, xa + (x1 - x0) * 0.35)
                bw = w / 2 * k
                sh.line([(xa, yy - bw), (xb, yy - bw)])
                sh.line([(xa, yy + bw), (xb, yy + bw)])
                sh.arc(xa, yy, bw, 90, 270, thin=False)
                sh.arc(xb, yy, bw, -90, 90, thin=False)
            size = f'{w}' + r.choice(['', 'H12', '+0,2'])
            sh.dim(xb + bw + 2, yy - bw, xb + bw + 2, yy + bw, size, -6)
            if cnt == 2:
                sh.text(xb + bw + 12, yy + 2, '2 паза')
            sh.el('паз', size, cnt)
    return sh


def sheet_metal(r):
    names = ['Дно', 'Панель', 'Щиток', 'Крышка', 'Кожух', 'Лоток', 'Полка']
    S = r.choice([1, 1.5, 2, 2.5])
    g, ls, ly = r.choice(LOW)
    sh = Sheet(r, r.choice(names), f'Лист {num(S)} ГОСТ 19904-90 / {g} {ls}-{yr(r, ly)}')
    Wp, Hp = r.uniform(200, 500), r.uniform(150, 350)
    k = min((sh.ax1 - sh.ax0 - 50) / Wp, (sh.ay1 - sh.ay0 - 40) / Hp)
    x0, y0 = sh.ax0 + 20, sh.ay0 + 15
    x1, y1 = x0 + Wp * k, y0 + Hp * k
    sh.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
    sh.dim(x0, y0, x1, y0, num(round(Wp)), 8)
    sh.dim(x0, y0, x0, y1, num(round(Hp)), 8)
    n = r.choice([2, 3, 4])
    bw = r.uniform(3, 6)
    for j in range(n):
        yy = y0 + (y1 - y0) * (j + 1) / (n + 1)
        xa, xb = x0 + (x1 - x0) * 0.12, x1 - (x1 - x0) * 0.12
        for off in (bw, bw * 0.55):
            sh.line([(xa, yy - off), (xb, yy - off)], thin=off != bw)
            sh.line([(xa, yy + off), (xb, yy + off)], thin=off != bw)
            sh.arc(xa, yy, off, 90, 270, thin=off != bw)
            sh.arc(xb, yy, off, -90, 90, thin=off != bw)
    L = sh.cut(x0 + (x1 - x0) * 0.6, y0, y1)
    gx, gy = x1 + 20, y1 - 10
    if gx < sh.ax1 - 30:
        sh.text(gx, gy - 14, f'{L}–{L}')
        pts = [(gx, gy), (gx + 6, gy), (gx + 8, gy - 3), (gx + 12, gy - 3), (gx + 14, gy), (gx + 20, gy)]
        sh.line(pts)
        sh.text(gx + 4, gy + 3, r.choice(['R5', 'R3', 'R10']))
    sh.el('выдавка', '', n)
    if r.random() < 0.4:
        sh.razv = True
        sh.text(x0, y1 + 12, r.choice(['Развертка', 'Линии гиба']))
    if r.random() < 0.3:
        sh.leader(x1, y0, r.choice(['У4', 'Т1', 'С2']), 10, -8)
        sh.el('сварной шов', '', 1)
    return sh


def fit_tt(tt, types, fam):
    """технические требования не спорят с рисунком: про швы, шлицы, пазы, резьбу, отверстия — только
    если они нарисованы, центровые — у вала, гибка — у листовой; иначе модель учится выдумывать по тексту"""
    numbered = any(re.match(r'\d+\. ', t) for t in tt)
    keep = []
    for t in tt:
        s = re.sub(r'^\d+\. ', '', t)
        lo = s.lower()
        if ('свар' in lo and 'сварной шов' not in types
                or 'шлиц' in lo and not types & {'шлицы', 'шлицы в отверстии'}
                or lo.startswith('паз ') and not types & {'паз', 'шпоночный паз', 'шпоночный паз в отверстии'}
                or 'резьба' in lo and not any(x.startswith('резьб') for x in types)
                or 'отверстия сверлить' in lo and not types & {'отверстие', 'резьбовое отверстие'}
                or 'центровые' in lo and fam != 'shaft'
                or 'гибку' in lo and fam != 'sheet'):
            continue
        keep.append(s)
    if 'сварной шов' in types and not any('свар' in s.lower() for s in keep):
        keep.append('Сварные швы по ГОСТ 14771-76.')
    if numbered or len(keep) > 1:
        return [f'{i + 1}. {s}' for i, s in enumerate(keep)]
    return keep


def part_image(r):
    fam = r.random()
    if fam < 0.45:
        sh, fam = shaft(r), 'shaft'
    elif fam < 0.7:
        sh, fam = flange(r), 'flange'
    elif fam < 0.85:
        sh, fam = plate(r, body=r.random() < 0.35), 'plate'
    else:
        sh, fam = sheet_metal(r), 'sheet'
    return sh.finish(fit_tt(tt_items(r), {e['type'] for e in sh.elements}, fam))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True)
    ap.add_argument('--n', type=int, default=1200)
    ap.add_argument('--seed', type=int, default=2)
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    r = random.Random(a.seed)
    with open(out / 'parts.jsonl', 'w', encoding='utf-8') as js:
        for i in range(a.n):
            im, ans = part_image(r)
            p = out / f'part_{i:05d}.png'
            im.save(p)
            js.write(json.dumps({'image': str(p.resolve()), 'tag': 'sheet', 'answer': ans}, ensure_ascii=False) + '\n')
            if (i + 1) % 100 == 0:
                print(i + 1, flush=True)


if __name__ == '__main__':
    main()
