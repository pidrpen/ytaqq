#!/usr/bin/env python3
"""Риас: седьмой набор курсоров (k7) — анимированный, из готового набора .ani.

    python native/make_rias.py "<папка Rias Gremory Cursor>" [--bmp]

Исходный набор (17 файлов .ani, ~17 МБ) — пиксель-арт 32×32, растянутый
ровно в 4 раза до 128×128, по одной картинке на кадр. Здесь каждый кадр
возвращается к родным 32 точкам и раскладывается на 32, 48 и 64 (как у
остальных наборов: Windows берёт размер по настройке указателя). 32 и 64 —
пиксель в пиксель, 48 — каждая вторая точка вдвое (полтора раза).
Картинки кадров — в PNG (Windows читает такие курсоры с Vista), поэтому
набор весит меньше мегабайта.

Кадры, их порядок (seq) и скорость (rate, jif) — ровно как в исходном
наборе: анимация та же. Точка нажатия пересчитывается под каждый размер.

Пишет native/cursors/k7_<вид>.ani — их берёт cursorpad.rc (RCDATA 360…).
Исходный набор в репозиторий не кладётся.
"""
from __future__ import annotations

import io
import struct
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
OUT = HERE / "cursors"
SIZES = (32, 48, 64)
NATIVE = 32

# вид в CursorPad ← файл набора (порядок схемы — Install.inf).
# «Rias hand.ani» — это «рукописный ввод» (карандаш), такого вида CursorPad
# не подменяет; рука над ссылками — «Rias link.ani».
SLOTS = {
    "static": "normal",
    "help": "help",
    "app": "work",
    "wait": "busy",
    "cross": "precision",
    "ibeam": "text",
    "no": "unavailable",
    "sizens": "vert",
    "sizewe": "horz",
    "sizenwse": "dgn1",
    "sizenesw": "dgn2",
    "sizeall": "move",
    "up": "alt",
    "hand": "link",
    "pin": "location",
    "person": "person",
}


def chunks(b: bytes, s: int, e: int):
    while s + 8 <= e:
        cid = b[s:s + 4]
        n = struct.unpack("<I", b[s + 4:s + 8])[0]
        yield cid, s + 8, n
        s += 8 + n + (n & 1)


def read_ani(path: Path):
    d = path.read_bytes()
    if d[:4] != b"RIFF" or d[8:12] != b"ACON":
        sys.exit(f"{path.name}: не .ani")
    anih = rate = seq = None
    frames = []
    for cid, s, n in chunks(d, 12, len(d)):
        if cid == b"anih":
            anih = struct.unpack("<9I", d[s:s + 36])
        elif cid == b"rate":
            rate = d[s:s + n]
        elif cid == b"seq ":
            seq = d[s:s + n]
        elif cid == b"LIST" and d[s:s + 4] == b"fram":
            frames = [d[fs:fs + fn] for fc, fs, fn in chunks(d, s + 4, s + n) if fc == b"icon"]
    if not anih or not frames:
        sys.exit(f"{path.name}: нет кадров")
    return anih, rate, seq, frames


def frame_image(cur: bytes):
    """Кадр — .cur с одной картинкой: картинка и точка нажатия."""
    count = struct.unpack("<H", cur[4:6])[0]
    if count < 1:
        sys.exit("пустой кадр")
    w, h, _, _, hx, hy, size, off = struct.unpack("<BBBBHHII", cur[6:22])
    im = Image.open(io.BytesIO(cur)).convert("RGBA")
    return im, (hx, hy)


def native(im: Image.Image) -> Image.Image:
    k = im.width // NATIVE
    small = im.resize((NATIVE, NATIVE), Image.NEAREST)
    # проверка: исходник — ровно растянутый пиксель-арт, иначе потеряли бы детали
    if small.resize(im.size, Image.NEAREST).tobytes() != im.tobytes() or k * NATIVE != im.width:
        sys.exit("кадр не растянут ровно в целое число раз — пересборка без потерь невозможна")
    return small


def bmp_image(im: Image.Image) -> bytes:
    w, h = im.size
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    px = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            px += bytes((b, g, r, a))
    row = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        bits = bytearray(row)
        for x in range(w):
            if im.getpixel((x, y))[3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    return hdr + bytes(px) + bytes(mask)


def png_image(im: Image.Image) -> bytes:
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def make_cur(src: Image.Image, hot128, as_png: bool) -> bytes:
    nat = native(src)
    imgs = []
    for s in SIZES:
        im = nat.resize((s, s), Image.NEAREST)
        hx = min(s - 1, round(hot128[0] * s / src.width))
        hy = min(s - 1, round(hot128[1] * s / src.height))
        imgs.append((s, hx, hy, png_image(im) if as_png else bmp_image(im)))
    head = struct.pack("<HHH", 0, 2, len(imgs))
    off = 6 + 16 * len(imgs)
    dirs, data = b"", b""
    for s, hx, hy, blob in imgs:
        dirs += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, hx, hy, len(blob), off + len(data))
        data += blob
    return head + dirs + data


def write_ani(path: Path, anih, rate, seq, frames):
    nf = len(frames)
    cb, _, steps, _, _, _, _, jif, flags = anih
    body = b"anih" + struct.pack("<I", 36) + struct.pack("<9I", 36, nf, steps, 0, 0, 0, 0, jif, flags)
    if rate:
        body += b"rate" + struct.pack("<I", len(rate)) + rate
    if seq:
        body += b"seq " + struct.pack("<I", len(seq)) + seq
    fram = b"fram"
    for f in frames:
        fram += b"icon" + struct.pack("<I", len(f)) + f + (b"\0" if len(f) & 1 else b"")
    body += b"LIST" + struct.pack("<I", len(fram)) + fram
    riff = b"ACON" + body
    path.write_bytes(b"RIFF" + struct.pack("<I", len(riff)) + riff)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    as_png = "--bmp" not in sys.argv
    if len(args) != 1:
        sys.exit(__doc__)
    src = Path(args[0])
    total_in = total_out = 0
    for slot, name in SLOTS.items():
        p = src / f"Rias {name}.ani"
        anih, rate, seq, frames = read_ani(p)
        out = []
        for cur in frames:
            im, hot = frame_image(cur)
            out.append(make_cur(im, hot, as_png))
        dst = OUT / f"k7_{slot}.ani"
        write_ani(dst, anih, rate, seq, out)
        total_in += p.stat().st_size
        total_out += dst.stat().st_size
        print(f"k7_{slot}.ani  ← Rias {name}.ani  кадров {len(frames)}, {dst.stat().st_size // 1024} КБ")
    print(f"итого {total_in / 1048576:.1f} МБ → {total_out / 1048576:.2f} МБ")


if __name__ == "__main__":
    main()
