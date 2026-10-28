# pack_catgirl.py — слои кошко-девочки со скелетом → атлас для CursorPad.
#
# Запуск (Python 3 + Pillow):
#   python -I native/avatar/pack_catgirl.py <папка набора mini-ai-catgirl-actions>
#
# Набор (Codex, 2026-10-25): rig.json — скелет на холсте 320×560, rig/*.png — слои
# (тело, хвост, голова без ушей, уши, закрытые глаза, рот, плечи, руки: плечо и три кисти —
# обычная, раскрытая ладонь, указательный палец). Логика поз — catgirl-core.js; в C её
# переносит ai_avatar.c (ac_girl_pose), рисует слои с поворотом и масштабом сама.
#
# Каждый слой обрезается по непрозрачному и уменьшается сразу до того размера, каким он
# стоит на холсте при самом крупном помощнике (240 %, окно 384 px на 600 единиц холста,
# с запасом в четверть), и ещё раз вдвое мельче — для обычного размера, чтобы билинейная выборка не рябила.
# Выход: native/ai_avatar_girl.png (ресурс 382) и native/ai_avatar_girl.h.
import json, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)
KIT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else None
if not KIT or not os.path.exists(os.path.join(KIT, "rig.json")):
    sys.exit("укажите папку набора кошко-девочки (там rig.json и rig/)")

BOX = 600.0          # сторона окна помощника в единицах холста rig.json
MAX_WINDOW = 384     # самый крупный помощник, px (AC_AVATAR_MAX)
K0 = MAX_WINDOW / BOX * 1.25  # с запасом: слой поворачивается, выборка не должна мылить
LEVELS = (K0, K0 / 2)
ATLAS_W = 1024
PAD = 2

rig = json.load(open(os.path.join(KIT, "rig.json"), encoding="utf-8"))
A = rig["assets"]

# слои, которые рисует catgirl-core.js (head.png и headClosed.png он не берёт)
LAYERS = ["body", "tail", "upperL", "upperR", "lowerL", "lowerR", "lowerOpen", "lowerPoint",
          "headCore", "earL", "earR", "eyesClosed", "mouthTalk", "shoulder"]


def bone_scale(name, length):
    a, e = A[name]["anchor"], A[name]["end"]
    return length / ((e[0] - a[0]) ** 2 + (e[1] - a[1]) ** 2) ** 0.5


# единиц холста на пиксель слоя — так слой стоит в позе
NATURAL = {
    "body": rig["bodyScale"], "tail": rig["tailScale"], "shoulder": 1.0,
    "headCore": rig["headScale"], "earL": rig["headScale"], "earR": rig["headScale"],
    "eyesClosed": rig["headScale"], "mouthTalk": rig["headScale"],
    "upperL": bone_scale("upperL", rig["upperLength"]), "upperR": bone_scale("upperR", rig["upperLength"]),
    "lowerL": bone_scale("lowerL", rig["lowerLength"]), "lowerR": bone_scale("lowerR", rig["lowerLength"]),
    "lowerOpen": bone_scale("lowerOpen", rig["lowerLength"]),
    "lowerPoint": bone_scale("lowerPoint", rig["lowerLength"]),
}

pieces = []  # (уровень, слой, картинка, ox, oy, kx, ky)
for level, K in enumerate(LEVELS):
    for name in LAYERS:
        src = Image.open(os.path.join(KIT, A[name]["src"])).convert("RGBA")
        box = src.getchannel("A").getbbox()
        x0, y0 = max(0, box[0] - 1), max(0, box[1] - 1)
        x1, y1 = min(src.width, box[2] + 1), min(src.height, box[3] + 1)
        crop = src.crop((x0, y0, x1, y1))
        k = NATURAL[name] * K
        w, h = max(2, round(crop.width * k)), max(2, round(crop.height * k))
        img = crop.resize((w, h), Image.LANCZOS)  # Pillow ресэмплит RGBA с premultiplied alpha
        pieces.append((level, name, img, x0, y0, w / crop.width, h / crop.height))

# полки по высоте
order = sorted(range(len(pieces)), key=lambda i: -pieces[i][2].height)
place = {}
x = y = shelf = 0
for i in order:
    img = pieces[i][2]
    if x + img.width > ATLAS_W:
        x, y, shelf = 0, y + shelf + PAD, 0
    place[i] = (x, y)
    x += img.width + PAD
    shelf = max(shelf, img.height)
ATLAS_H = y + shelf
atlas = Image.new("RGBA", (ATLAS_W, ATLAS_H), (0, 0, 0, 0))
for i, (level, name, img, *_rest) in enumerate(pieces):
    atlas.paste(img, place[i])
atlas.save(os.path.join(NATIVE, "ai_avatar_girl.png"), optimize=True)


def f(v):
    t = ("%.4f" % v).rstrip("0")
    return t + ("0f" if t.endswith(".") else "f")


def pt(p):
    return "{%s, %s}" % (f(p[0]), f(p[1]))


rows = []
for level in range(len(LEVELS)):
    cells = []
    for name in LAYERS:
        i = next(j for j, p in enumerate(pieces) if p[0] == level and p[1] == name)
        _, _, img, ox, oy, kx, ky = pieces[i]
        x, y = place[i]
        cells.append("{%d, %d, %d, %d, %s, %s, %s, %s}" % (x, y, img.width, img.height, f(ox), f(oy), f(kx), f(ky)))
    rows.append("  {" + ",\n   ".join(cells) + "}")

table = ",\n".join(rows)
enum = ", ".join("AG_" + n.upper() for n in LAYERS)
anchors = ", ".join(pt(A[n]["anchor"]) for n in LAYERS)
ends = ", ".join(pt(A[n].get("end", A[n]["anchor"])) for n in LAYERS)
out = f"""/* ai_avatar_girl.h — сгенерировано native/avatar/pack_catgirl.py, руками не править.
   Кошко-девочка со скелетом: слои в атласе ai_avatar_girl.png (ресурс 382) на двух уровнях
   (0 — для крупного помощника, 1 — вдвое мельче), опоры слоёв и скелет из rig.json. */
#define AC_GIRL_W {ATLAS_W}
#define AC_GIRL_H {ATLAS_H}
#define AC_GIRL_BOX {f(BOX)}       /* сторона окна в единицах холста */
#define AC_GIRL_K0 {f(K0)}         /* пикселей атласа уровня 0 на единицу холста */
enum {{ {enum}, AG_N }};
/* слой: место в атласе; левый верх в пикселях исходного слоя; пикселей атласа на пиксель слоя */
typedef struct {{ short x, y, w, h; float ox, oy, kx, ky; }} AcGirlLayer;
static const AcGirlLayer kAcGirl[2][AG_N] = {{
{table}
}};
/* опора слоя и конец кости — в пикселях исходного слоя */
static const float kAcGirlAnchor[AG_N][2] = {{{anchors}}};
static const float kAcGirlEnd[AG_N][2] = {{{ends}}};
/* скелет на холсте {rig["width"]}×{rig["height"]} */
static const float kAgFeet[2] = {pt(rig["feet"])}, kAgBody[2] = {pt(rig["bodyAnchor"])}, kAgHead[2] = {pt(rig["headAnchor"])};
static const float kAgShoulderL[2] = {pt(rig["shoulderL"])}, kAgShoulderR[2] = {pt(rig["shoulderR"])};
static const float kAgTail[2] = {pt(rig["tailAnchor"])}, kAgMouth[2] = {pt(rig["mouthHeadLocal"])};
static const float kAgHeadPivot[2] = {pt(A["head"]["anchor"])}; /* опора головы в пикселях слоя головы */
#define AG_BODY_SCALE {f(rig["bodyScale"])}
#define AG_HEAD_SCALE {f(rig["headScale"])}
#define AG_TAIL_SCALE {f(rig["tailScale"])}
#define AG_UPPER {f(rig["upperLength"])}
#define AG_LOWER {f(rig["lowerLength"])}
"""
with open(os.path.join(NATIVE, "ai_avatar_girl.h"), "w", encoding="utf-8", newline="\n") as fh:
    fh.write(out)
print("atlas", ATLAS_W, "x", ATLAS_H, "layers", len(pieces))
