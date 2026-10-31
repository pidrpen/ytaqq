"""Значок CursorPad (с 2026.10.31) — вариант 5 «Курсор с размерной линией», выбран
пользователем: плитка цвета «красного карандаша» вида «Чертёж», белая стрелка
с тёмной каймой, слева — размерная линия, как на чертеже.

Рисуется кодом в сетке 64×64 (те же координаты, что в макете на холсте):
  16, 20, 24 — по пикселям: стрелка по маске центров пикселей, кайма — соседи
               маски, размерная линия и её стрелки — отдельными пикселями,
               скругление углов — по покрытию; без мыла уменьшения;
  32, 40, 48, 64, 256 — с 16-кратным сглаживанием.
В .ico: до 64 — 32-битный BMP с маской (так Windows берёт мелкие значки
везде), 256 — PNG. Ресурс ICON 1 в cursorpad.rc — его показывают Проводник,
ярлык и панель задач.

  python native/make_icon.py                → native/cursorpad.ico
  python native/make_icon.py --preview DIR  → ещё и PNG каждого размера
"""
import io
import os
import struct
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = (16, 20, 24, 32, 40, 48, 64, 256)

RUST = (180, 56, 10, 255)
WHITE = (255, 255, 255, 255)
INK = (22, 24, 29, 255)

# сетка 64×64
TILE = (2, 2, 62, 62, 8)  # x0, y0, x1, y1, скругление
ARROW = [(24, 10), (24, 50), (33, 41.5), (39.5, 54.5), (46, 51.5), (39.6, 38.6), (51.5, 38.6)]
DIM_X, DIM_Y0, DIM_Y1 = 12, 11, 53  # размерная линия: x, концы стрелок


def inside(poly, x, y):
    c = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            c = not c
        j = i
    return c


def tile_cover(n, x, y):
    """доля пикселя (x, y) внутри скруглённой плитки, 4×4 выборки"""
    k = n / 64.0
    x0, y0, x1, y1, r = (v * k for v in TILE)
    hit = 0
    for sy in range(4):
        for sx in range(4):
            px, py = x + (sx + 0.5) / 4, y + (sy + 0.5) / 4
            if px < x0 or px > x1 or py < y0 or py > y1:
                continue
            cx = min(max(px, x0 + r), x1 - r)
            cy = min(max(py, y0 + r), y1 - r)
            if (px - cx) ** 2 + (py - cy) ** 2 <= r * r:
                hit += 1
    return hit / 16.0


def draw_pixel(n):
    """16, 20, 24: каждый пиксель решён отдельно"""
    k = n / 64.0
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    px = im.load()
    for y in range(n):
        for x in range(n):
            a = tile_cover(n, x, y)
            if a > 0:
                px[x, y] = (RUST[0], RUST[1], RUST[2], int(round(255 * a)))
    # стрелка: пиксель белый, если фигура покрывает его хотя бы на 3/8 (4×4 выборки) —
    # так стрелка не тоньше задуманной и без полутонов
    def cover(x, y):
        return sum(inside(ARROW, (x + (sx + 0.5) / 4) / k, (y + (sy + 0.5) / 4) / k)
                   for sy in range(4) for sx in range(4)) / 16.0
    mask = [[cover(x, y) >= 0.375 for x in range(n)] for y in range(n)]
    for y in range(n):
        for x in range(n):
            if mask[y][x]:
                continue
            near = any(0 <= x + dx < n and 0 <= y + dy < n and mask[y + dy][x + dx]
                       for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            if near and px[x, y][3] > 0:
                px[x, y] = INK
    for y in range(n):
        for x in range(n):
            if mask[y][x]:
                px[x, y] = WHITE
    # размерная линия: столбик и стрелки на концах
    lx = int(DIM_X * k)
    y0, y1 = int(round(DIM_Y0 * k)), int(round(DIM_Y1 * k)) - 1
    for y in range(y0, y1 + 1):
        px[lx, y] = WHITE
    for dx in (-1, 1):
        px[lx + dx, y0 + 1] = WHITE
        px[lx + dx, y1 - 1] = WHITE
    if n >= 20:  # засечки выносных линий — где на них есть место
        for x in range(lx - 1, lx + 3):
            px[x, y0 - 1] = WHITE
            px[x, y1 + 1] = WHITE
    return im


def draw_smooth(n):
    """32 и крупнее: та же фигура с 16-кратным сглаживанием"""
    s = 16
    N = n * s
    k = N / 64.0
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1, r = (v * k for v in TILE)
    d.rounded_rectangle((x0, y0, x1, y1), radius=r, fill=RUST)
    w = max(1, int(2.5 * k))
    d.line([(DIM_X * k, 12 * k), (DIM_X * k, 52 * k)], fill=WHITE, width=w)
    for ya, yb in ((17, 11), (47, 53)):
        d.line([(8.5 * k, ya * k), (DIM_X * k, yb * k), (15.5 * k, ya * k)], fill=WHITE, width=w, joint='curve')
    d.line([(8 * k, 10 * k), (22 * k, 10 * k)], fill=WHITE, width=max(1, int(1.5 * k)))
    d.line([(8 * k, 54 * k), (22 * k, 54 * k)], fill=WHITE, width=max(1, int(1.5 * k)))
    pts = [(x * k, y * k) for x, y in ARROW]
    d.polygon(pts, fill=WHITE)
    d.line(pts + [pts[0]], fill=INK, width=max(1, int(3 * k)), joint='curve')
    return im.convert('RGBa').resize((n, n), Image.LANCZOS).convert('RGBA')


def draw(n):
    return draw_pixel(n) if n <= 24 else draw_smooth(n)


def bmp_entry(im):
    n = im.width
    rows = []
    for y in range(n - 1, -1, -1):  # снизу вверх
        row = bytearray()
        for x in range(n):
            r, g, b, a = im.getpixel((x, y))
            row += bytes((b, g, r, a))
        rows.append(bytes(row))
    stride = ((n + 31) // 32) * 4
    mask = bytearray()
    for y in range(n - 1, -1, -1):
        bits = bytearray(stride)
        for x in range(n):
            if im.getpixel((x, y))[3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    head = struct.pack('<IiiHHIIiiII', 40, n, n * 2, 1, 32, 0, n * n * 4 + len(mask), 0, 0, 0, 0)
    return head + b''.join(rows) + bytes(mask)


def png_entry(im):
    buf = io.BytesIO()
    im.save(buf, 'PNG', optimize=True)
    return buf.getvalue()


def write_ico(path, images):
    data = [png_entry(im) if im.width >= 256 else bmp_entry(im) for im in images]
    out = struct.pack('<HHH', 0, 1, len(images))
    off = 6 + 16 * len(images)
    for im, blob in zip(images, data):
        w = 0 if im.width >= 256 else im.width
        out += struct.pack('<BBBBHHII', w, w, 0, 0, 1, 32, len(blob), off)
        off += len(blob)
    with open(path, 'wb') as f:
        f.write(out + b''.join(data))


def main():
    images = [draw(n) for n in SIZES]
    path = os.path.join(HERE, 'cursorpad.ico')
    write_ico(path, images)
    print('записан', path, os.path.getsize(path), 'байт:', ', '.join(map(str, SIZES)))
    if '--preview' in sys.argv:
        out = sys.argv[sys.argv.index('--preview') + 1]
        os.makedirs(out, exist_ok=True)
        for im in images:
            im.save(os.path.join(out, 'icon-%d.png' % im.width))


if __name__ == '__main__':
    main()
