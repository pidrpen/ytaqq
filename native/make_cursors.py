#!/usr/bin/env python3
"""Draw the gauntlet cursor family and pack multi-size Windows .cur files.

The two hand-drawn sprites (arrow, pointing hand) stay as they are; everything
else Windows can ask for is drawn here in the same steel-and-brass language so
the set reads as one theme instead of generic black arrows.

Each role is rendered at 8x and downsampled, then packed into a single .cur
holding 32/48/64/96 px so Windows picks the right one for the cursor-size
setting instead of scaling a 32 px bitmap.
"""
from __future__ import annotations

import math
import struct
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
SRC = HERE / "cursors"
# 32/48/64 covers the cursor-size settings people actually use; a 96 px copy
# in every file cost more exe than it was worth
SIZES = (32, 48, 64)
# animated cursors get one size: eight frames times four sizes was megabytes
ANI_SIZES = (48,)
SS = 8  # supersampling factor

# sampled from the two hand-drawn sprites so the drawn roles match them
THEMES = {
    # k3 "Рукавица": steel with brass
    "k3": {"body": ((194, 202, 212), (56, 65, 77)),
           "accent": ((245, 230, 207), (107, 79, 34)),
           "edge": (24, 29, 36), "rim": (236, 240, 245)},
    # k2 "Мечник": ivory armour with gold
    "k2": {"body": ((253, 251, 253), (138, 138, 146)),
           "accent": ((247, 221, 185), (107, 74, 26)),
           "edge": (32, 26, 20), "rim": (255, 252, 246)},
}
THEME = THEMES["k3"]


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def gradient(size, top, bottom, angle_horizontal=False):
    """Linear ramp used to fake a lit metal surface."""
    g = Image.new("RGB", (size, size))
    d = ImageDraw.Draw(g)
    for i in range(size):
        t = i / max(1, size - 1)
        c = lerp(top, bottom, t)
        if angle_horizontal:
            d.line([(i, 0), (i, size)], fill=c)
        else:
            d.line([(0, i), (size, i)], fill=c)
    return g


class Canvas:
    """Shapes are described in 0..1 space and drawn into a supersampled mask."""

    def __init__(self, px):
        self.px = px
        self.n = px * SS
        self.body = Image.new("L", (self.n, self.n), 0)   # steel areas
        self.accent = Image.new("L", (self.n, self.n), 0)  # brass areas
        self.db = ImageDraw.Draw(self.body)
        self.da = ImageDraw.Draw(self.accent)

    def s(self, pts):
        return [(x * self.n, y * self.n) for x, y in pts]

    def poly(self, pts, accent=False):
        (self.da if accent else self.db).polygon(self.s(pts), fill=255)

    def rect(self, x0, y0, x1, y1, accent=False):
        (self.da if accent else self.db).rectangle(
            self.s([(x0, y0), (x1, y1)]), fill=255)

    def ellipse(self, x0, y0, x1, y1, accent=False, width=0):
        box = self.s([(x0, y0), (x1, y1)])
        d = self.da if accent else self.db
        if width:
            d.ellipse(box, outline=255, width=int(width * self.n))
        else:
            d.ellipse(box, fill=255)

    def line(self, pts, w, accent=False):
        (self.da if accent else self.db).line(
            self.s(pts), fill=255, width=int(w * self.n), joint="curve")

    def render(self):
        """Mask -> lit metal ringed the way system cursors are: a dark edge so
        it reads on white, a light rim inside it so it reads on black."""
        px = self.px
        body = self.body.resize((px, px), Image.LANCZOS)
        accent = self.accent.resize((px, px), Image.LANCZOS)
        alpha = ImageChops.lighter(body, accent)

        out = Image.new("RGBA", (px, px), (0, 0, 0, 0))
        grow = alpha.filter(ImageFilter.MaxFilter(3))
        inner = alpha.filter(ImageFilter.MinFilter(3))
        rim = ImageChops.subtract(alpha, inner)

        out.paste(Image.new("RGBA", (px, px), THEME["edge"] + (255,)), (0, 0), grow)
        out.paste(Image.new("RGBA", (px, px), THEME["rim"] + (255,)), (0, 0), rim)
        out.paste(gradient(px, *THEME["body"]).convert("RGBA"), (0, 0), inner)
        out.paste(gradient(px, *THEME["accent"]).convert("RGBA"), (0, 0),
                  accent.filter(ImageFilter.MinFilter(3)))
        return out


# ---- role shapes ---------------------------------------------------------

def arrow_head(c, tip, direction, w=0.20, l=0.22):
    """Triangular head pointing `direction` (unit vector) from `tip`."""
    dx, dy = direction
    nx, ny = -dy, dx
    bx, by = tip[0] - dx * l, tip[1] - dy * l
    c.poly([tip, (bx + nx * w / 2, by + ny * w / 2),
            (bx - nx * w / 2, by - ny * w / 2)])


def double_arrow(c, direction, shaft=0.085):
    dx, dy = direction
    n = math.hypot(dx, dy)
    dx, dy = dx / n, dy / n
    cx = cy = 0.5
    reach = 0.44
    t1 = (cx + dx * reach, cy + dy * reach)
    t2 = (cx - dx * reach, cy - dy * reach)
    c.line([t2, t1], shaft)
    # heads about three times the shaft, the way system resize arrows read
    arrow_head(c, t1, (dx, dy), w=0.30, l=0.26)
    arrow_head(c, t2, (-dx, -dy), w=0.30, l=0.26)
    # brass band across the middle: the family mark
    px, py = -dy, dx
    c.line([(cx + px * 0.055, cy + py * 0.055),
            (cx - px * 0.055, cy - py * 0.055)], 0.06, accent=True)


def role_ibeam(c):
    c.rect(0.455, 0.10, 0.545, 0.90)
    c.rect(0.36, 0.10, 0.64, 0.155, accent=True)
    c.rect(0.36, 0.845, 0.64, 0.90, accent=True)


def role_cross(c):
    c.rect(0.475, 0.06, 0.525, 0.38)
    c.rect(0.475, 0.62, 0.525, 0.94)
    c.rect(0.06, 0.475, 0.38, 0.525)
    c.rect(0.62, 0.475, 0.94, 0.525)
    c.ellipse(0.44, 0.44, 0.56, 0.56, accent=True)


def role_no(c):
    c.ellipse(0.10, 0.10, 0.90, 0.90, width=0.085)
    c.line([(0.27, 0.27), (0.73, 0.73)], 0.085)
    for a in (45, 135, 225, 315):
        x = 0.5 + 0.40 * math.cos(math.radians(a))
        y = 0.5 + 0.40 * math.sin(math.radians(a))
        c.ellipse(x - 0.035, y - 0.035, x + 0.035, y + 0.035, accent=True)


def role_up(c):
    c.line([(0.5, 0.92), (0.5, 0.22)], 0.10)
    arrow_head(c, (0.5, 0.08), (0, -1))
    c.line([(0.40, 0.60), (0.60, 0.60)], 0.075, accent=True)


def role_pin(c):
    c.poly([(0.5, 0.94), (0.44, 0.52), (0.56, 0.52)])
    c.rect(0.30, 0.30, 0.70, 0.52)
    c.rect(0.26, 0.18, 0.74, 0.30, accent=True)


def role_person(c):
    c.ellipse(0.35, 0.10, 0.65, 0.40)
    c.poly([(0.5, 0.40), (0.82, 0.92), (0.18, 0.92)])
    c.rect(0.36, 0.60, 0.64, 0.68, accent=True)


ROLES = {
    "ibeam": (role_ibeam, (0.5, 0.5)),
    "cross": (role_cross, (0.5, 0.5)),
    "no": (role_no, (0.5, 0.5)),
    "up": (role_up, (0.5, 0.08)),
    "pin": (role_pin, (0.5, 0.94)),
    "person": (role_person, (0.5, 0.5)),
    "sizewe": (lambda c: double_arrow(c, (1, 0)), (0.5, 0.5)),
    "sizens": (lambda c: double_arrow(c, (0, 1)), (0.5, 0.5)),
    "sizenwse": (lambda c: double_arrow(c, (1, 1)), (0.5, 0.5)),
    "sizenesw": (lambda c: double_arrow(c, (1, -1)), (0.5, 0.5)),
}


def role_sizeall(c):
    c.line([(0.12, 0.5), (0.88, 0.5)], 0.095)
    c.line([(0.5, 0.12), (0.5, 0.88)], 0.095)
    for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        arrow_head(c, (0.5 + d[0] * 0.46, 0.5 + d[1] * 0.46), d, w=0.20, l=0.20)
    c.ellipse(0.425, 0.425, 0.575, 0.575, accent=True)


ROLES["sizeall"] = (role_sizeall, (0.5, 0.5))


def compose_with_sprite(sprite: Image.Image, badge: Image.Image, px: int) -> Image.Image:
    """Gauntlet plus a small mark, for 'help' and 'app starting'."""
    base = sprite.resize((px, px), Image.LANCZOS)
    out = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    out.paste(base, (0, 0), base)
    bs = int(px * 0.46)
    b = badge.resize((bs, bs), Image.LANCZOS)
    out.paste(b, (px - bs, px - bs), b)
    return out


def role_question(c):
    c.line([(0.30, 0.30), (0.50, 0.14)], 0.14)
    c.line([(0.50, 0.14), (0.70, 0.32)], 0.14)
    c.line([(0.70, 0.32), (0.50, 0.52)], 0.14)
    c.line([(0.50, 0.52), (0.50, 0.64)], 0.14)
    c.ellipse(0.40, 0.78, 0.60, 0.98, accent=True)


def role_spinner(c, phase):
    for i in range(8):
        a = math.radians(i * 45 - 90)
        x = 0.5 + 0.34 * math.cos(a)
        y = 0.5 + 0.34 * math.sin(a)
        r = 0.055 + 0.045 * (((i - phase) % 8) / 7.0)
        accent = ((i - phase) % 8) >= 6
        c.ellipse(x - r, y - r, x + r, y + r, accent=accent)


def write_cur(images, path: Path, hot):
    """Multi-image .cur: one directory entry per size."""
    blobs = []
    for im in images:
        w, h = im.size
        xor = bytearray()
        for y in range(h - 1, -1, -1):
            for x in range(w):
                r, g, b, a = im.getpixel((x, y))
                xor += struct.pack("BBBB", b, g, r, a)
        # the AND mask still has to mark the see-through pixels: readers that
        # honour it paint black squares otherwise
        stride = ((w + 31) // 32) * 4
        and_mask = bytearray(stride * h)
        for y in range(h):
            src_y = h - 1 - y
            for x in range(w):
                if im.getpixel((x, src_y))[3] < 16:
                    and_mask[y * stride + (x // 8)] |= 0x80 >> (x % 8)
        # biSizeImage counts the mask too, matching the files Windows and the
        # existing sprites already ship with
        dib = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                          len(xor) + len(and_mask), 0, 0, 0, 0)
        blobs.append(bytes(dib) + bytes(xor) + bytes(and_mask))
    header = struct.pack("<HHH", 0, 2, len(blobs))
    offset = 6 + 16 * len(blobs)
    entries, data = b"", b""
    for im, blob in zip(images, blobs):
        w, h = im.size
        hx = max(0, min(w - 1, int(round(hot[0] * w))))
        hy = max(0, min(h - 1, int(round(hot[1] * h))))
        entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, hx, hy,
                               len(blob), offset)
        offset += len(blob)
        data += blob
    path.write_bytes(header + entries + data)


def write_ani(frame_files, path: Path, rate=5):
    """RIFF/ACON wrapper: the same header the existing .ani files carry."""
    frames = [f.read_bytes() for f in frame_files]
    anih = struct.pack("<9I", 36, len(frames), len(frames), 0, 0, 0, 0, rate, 1)
    body = b"anih" + struct.pack("<I", 36) + anih
    body += b"rate" + struct.pack("<I", 4 * len(frames))
    body += b"".join(struct.pack("<I", rate) for _ in frames)
    fram = b"fram"
    for blob in frames:
        pad = len(blob) % 2
        fram += b"icon" + struct.pack("<I", len(blob)) + blob + (b"\0" * pad)
    body += b"LIST" + struct.pack("<I", len(fram)) + fram
    riff = b"ACON" + body
    path.write_bytes(b"RIFF" + struct.pack("<I", len(riff)) + riff)


def build_theme(tag: str):
    """One full cursor family: drawn roles, badge roles, spinner, .ani files."""
    global THEME
    THEME = THEMES[tag]
    sprite = Image.open(SRC / f"{tag}_static.cur").convert("RGBA")

    for name, (draw, hot) in ROLES.items():
        imgs = []
        for px in SIZES:
            c = Canvas(px)
            draw(c)
            imgs.append(c.render())
        write_cur(imgs, SRC / f"{tag}_{name}.cur", hot)

    badges = {}
    for px in SIZES:
        c = Canvas(int(px * 0.46))
        role_question(c)
        badges[px] = c.render()
    write_cur([compose_with_sprite(sprite, badges[px], px) for px in SIZES],
              SRC / f"{tag}_help.cur", (0.02, 0.02))

    wait_frames, app_frames = [], []
    for i in range(8):
        imgs, badge = [], {}
        for px in ANI_SIZES:
            c = Canvas(px)
            role_spinner(c, i)
            imgs.append(c.render())
            b = Canvas(int(px * 0.46))
            role_spinner(b, i)
            badge[px] = b.render()
        f1 = SRC / f"{tag}_{i + 1:02d}.cur"
        f2 = SRC / f"{tag}_app{i + 1:02d}.cur"
        write_cur(imgs, f1, (0.5, 0.5))
        write_cur([compose_with_sprite(sprite, badge[px], px) for px in ANI_SIZES],
                  f2, (0.02, 0.02))
        wait_frames.append(f1)
        app_frames.append(f2)

    write_ani(wait_frames, SRC / f"{tag}_wait.ani")
    write_ani(app_frames, SRC / f"{tag}_app.ani")
    return len(ROLES) + 1 + 16


def main():
    for tag in ("k3", "k2"):
        n = build_theme(tag)
        print(f"{tag}: {n} файлов, размеры {'/'.join(map(str, SIZES))}")


if __name__ == "__main__":
    main()
