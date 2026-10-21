# pack_avatar.py — собирает кадры make_avatar.py в атлас для CursorPad.
#
#   python native/avatar/pack_avatar.py <папка кадров>
#
# Пишет native/ai_avatar_atlas.png (строка = состояние, столбец = кадр) и
# native/ai_avatar_face.h (точки глаз и рта в каждом кадре + цвет настроения).
# Поверх рендера: мягкое свечение светящихся деталей и тонкий тёмный контур,
# чтобы белый робот читался и на светлых обоях. Нужны Pillow и numpy.
import json, os, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "frames")
meta = json.load(open(os.path.join(src, "face.json"), encoding="utf-8"))
FRAME, COLS, STATES = meta["frame"], meta["cols"], meta["states"]


def gaussian(img, sigma):
    r = int(sigma * 3)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    out = img
    for axis in (0, 1):
        pad = [(0, 0)] * img.ndim
        pad[axis] = (r, r)
        p = np.pad(out, pad, mode="constant")
        acc = np.zeros_like(out)
        for i, w in enumerate(k):
            sl = [slice(None)] * img.ndim
            sl[axis] = slice(i, i + out.shape[axis])
            acc += w * p[tuple(sl)]
        out = acc
    return out


def dilate(mask, radius):
    out = mask.copy()
    h, w = mask.shape
    p = np.pad(mask, radius)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx * dx + dy * dy <= radius * radius:
                out = np.maximum(out, p[radius + dy:radius + dy + h, radius + dx:radius + dx + w])
    return out


def over(top, bottom):
    """premultiplied: top поверх bottom."""
    return top + bottom * (1 - top[..., 3:4])


def frame(path):
    im = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32) / 255.0
    a = im[..., 3:4]
    pm = np.concatenate([im[..., :3] * a, a], axis=-1)

    rgb = im[..., :3]
    v = rgb.max(-1)
    sat = (v - rgb.min(-1)) / np.maximum(v, 1e-4)
    hot = np.clip((v - 0.80) / 0.15, 0, 1) * np.clip((sat - 0.35) / 0.25, 0, 1) * im[..., 3]
    halo = gaussian(rgb * hot[..., None], 6.0) * 1.6
    glow = np.concatenate([halo, np.clip(halo.max(-1, keepdims=True), 0, 1)], axis=-1)
    glow[..., :3] = np.minimum(glow[..., :3], glow[..., 3:4])

    solid = np.clip((im[..., 3] - 0.6) / 0.3, 0, 1)
    edge = gaussian(dilate(solid, 3), 0.8)
    outline = np.zeros_like(pm)
    outline[..., 0], outline[..., 1], outline[..., 2] = 0.07, 0.08, 0.16
    outline *= (edge * 0.62)[..., None]
    outline[..., 3] = edge * 0.62

    pm = over(pm, over(outline, glow))
    pm = pm.reshape(FRAME, 2, FRAME, 2, 4).mean(axis=(1, 3))  # 2× вниз
    a = pm[..., 3:4]
    rgb = np.where(a > 1e-4, pm[..., :3] / np.maximum(a, 1e-4), 0)
    return np.clip(np.concatenate([rgb, a], axis=-1) * 255 + 0.5, 0, 255).astype(np.uint8)


atlas = np.zeros((FRAME * len(STATES), FRAME * COLS, 4), dtype=np.uint8)
for row, state in enumerate(STATES):
    for col in range(COLS):
        atlas[row * FRAME:(row + 1) * FRAME, col * FRAME:(col + 1) * FRAME] = frame(
            os.path.join(src, "%s_%02d.png" % (state, col)))
    print("packed", state, flush=True)
Image.fromarray(atlas, "RGBA").save(os.path.join(NATIVE, "ai_avatar_atlas.png"), optimize=True)

lines = [
    "/* ai_avatar_face.h — сгенерировано native/avatar/pack_avatar.py, руками не править.",
    "   Точки лица в кадре атласа, 1/8 пикселя: левый глаз, правый глаз, рот. */",
    "#define AC_AVATAR_FRAME %d" % FRAME,
    "#define AC_AVATAR_COLS %d" % COLS,
    "#define AC_AVATAR_ROWS %d" % len(STATES),
    "static const short kAcAvatarFace[AC_AVATAR_ROWS][AC_AVATAR_COLS][6] = {",
]
for state in STATES:
    cells = []
    for anchors in meta[state]:
        nums = []
        for key in ("eyeL", "eyeR", "mouth"):
            nums += [int(round(c * 8)) for c in anchors[key]]
        cells.append("{%s}" % ",".join(map(str, nums)))
    lines.append("  {%s}, /* %s */" % (",".join(cells), state))
lines.append("};")
colors = ", ".join("RGB(0x%s, 0x%s, 0x%s)" % (c[0:2], c[2:4], c[4:6]) for c in (meta["colors"][s] for s in STATES))
lines.append("static const COLORREF kAcAvatarMood[AC_AVATAR_ROWS] = {%s};" % colors)
with open(os.path.join(NATIVE, "ai_avatar_face.h"), "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
print("atlas", atlas.shape, "->", os.path.join(NATIVE, "ai_avatar_atlas.png"))
