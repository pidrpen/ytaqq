#!/usr/bin/env python3
"""PNG (alpha or magenta/purple key) -> 48x48 PNG + Windows .cur"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

from PIL import Image


def is_key(r: int, g: int, b: int, a: int) -> bool:
    if a < 16:
        return True
    if g < 55 and r > 140 and b > 80 and abs(r - b) < 90:
        return True
    if r > 230 and b > 230 and g < 50:
        return True
    return False


def keyed(im: Image.Image) -> Image.Image:
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if is_key(r, g, b, a):
                px[x, y] = (0, 0, 0, 0)
    # eat 1px magenta fringe
    src = im.copy()
    sp = src.load()
    px = im.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = sp[x, y]
            if a < 16:
                continue
            if g < 80 and r > 120 and b > 70 and a < 250:
                px[x, y] = (0, 0, 0, 0)
    return im


def bbox(im: Image.Image) -> tuple[int, int, int, int]:
    px = im.load()
    w, h = im.size
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if px[x, y][3] > 24:
                xs.append(x)
                ys.append(y)
    if not xs:
        return 0, 0, w - 1, h - 1
    return min(xs), min(ys), max(xs), max(ys)


def fingertip(im: Image.Image) -> tuple[int, int]:
    px = im.load()
    w, h = im.size
    top = h
    xs = []
    for y in range(h):
        row = [x for x in range(w) if px[x, y][3] > 40]
        if row:
            top = y
            xs = row
            break
    if not xs:
        return w // 2, 1
    xs.sort()
    return xs[len(xs) // 2], top


def fit48(im: Image.Image) -> tuple[Image.Image, int, int]:
    l, t, r, b = bbox(im)
    crop = im.crop((l, t, r + 1, b + 1))
    pad = 3
    cw, ch = crop.size
    scale = min((48 - pad * 2) / cw, (48 - pad * 2) / ch)
    nw = max(1, int(round(cw * scale)))
    nh = max(1, int(round(ch * scale)))
    resized = crop.resize((nw, nh), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
    ox = (48 - nw) // 2
    oy = (48 - nh) // 2
    canvas.paste(resized, (ox, oy), resized)
    fx, fy = fingertip(crop)
    hx = int(round(ox + fx * scale))
    hy = int(round(oy + fy * scale))
    hx = max(0, min(47, hx))
    hy = max(0, min(47, hy))
    return canvas, hx, hy


def remap_knight(im: Image.Image) -> Image.Image:
    """Shift steel-blue gauntlet toward the knight's silver / gold / crimson."""
    out = im.copy()
    px = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 16:
                continue
            # keep gold / brass
            if r >= g + 20 and g > 70 and b < 90:
                px[x, y] = (min(255, r + 8), min(255, g + 4), min(80, b), a)
                continue
            # leather / brown -> crimson cloth
            if r > g + 15 and r > b + 15 and g < 110 and b < 90:
                px[x, y] = (min(255, int(r * 0.95 + 40)), max(20, g // 2), max(16, b // 2), a)
                continue
            # metal -> silver-white with a cool edge
            lum = 0.28 * r + 0.42 * g + 0.30 * b
            r2 = min(255, int(lum * 1.15 + 38))
            g2 = min(255, int(lum * 1.12 + 34))
            b2 = min(255, int(lum * 1.08 + 32))
            px[x, y] = (r2, g2, b2, a)
    return out


def write_cur(im: Image.Image, path: Path, hotx: int, hoty: int) -> None:
    w, h = im.size
    assert w == h == 48
    xor = bytearray()
    # bottom-up BGRA
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            xor += struct.pack("BBBB", b, g, r, a)
    and_row = ((w + 31) // 32) * 4
    mask = bytearray(and_row * h)
    for y in range(h):
        src_y = h - 1 - y
        for x in range(w):
            a = im.getpixel((x, src_y))[3]
            if a < 16:
                byte_i = y * and_row + (x // 8)
                mask[byte_i] |= 0x80 >> (x % 8)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(xor), 0, 0, 0, 0)
    image = dib + xor + mask
    header = struct.pack("<HHH", 0, 2, 1)
    entry = struct.pack("<BBBBHHII", 48, 48, 0, 0, hotx, hoty, len(image), 22)
    path.write_bytes(header + entry + image)


def main() -> None:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("public/cursors/k3-frame.png")
    raw = keyed(Image.open(src))
    k3, hx, hy = fit48(raw)
    k2, hx2, hy2 = fit48(remap_knight(raw))
    public = Path("public/cursors")
    native = Path("native/cursors")
    k3.save(public / "k3-hand.png")
    k2.save(public / "k2-hand.png")
    write_cur(k3, native / "k3_hand.cur", hx, hy)
    write_cur(k2, native / "k2_hand.cur", hx2, hy2)
    print(f"k3-hand hotspot {hx},{hy}")
    print(f"k2-hand hotspot {hx2},{hy2}")
    k3.save("screenshots/k3-hand-new.png")
    k2.save("screenshots/k2-hand-new.png")


if __name__ == "__main__":
    main()
