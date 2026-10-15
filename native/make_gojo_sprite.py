# gojo_sprite.png (32x32, PixelLab) -> gojo_sprite.h: пиксели RGBA и точка кончика пальца.
# Запуск: python3 native/make_gojo_sprite.py (нужен pillow). Заголовок лежит в репозитории,
# build.sh этот скрипт не вызывает.
import os
from PIL import Image
here = os.path.dirname(os.path.abspath(__file__))
im = Image.open(os.path.join(here, "gojo_sprite.png")).convert("RGBA")
assert im.size == (32, 32)
a = im.split()[3]
# кончик пальца — самая верхняя непрозрачная точка руки (левее и ниже волос)
tip = min(((y, x) for y in range(32) for x in range(7) if y >= 6 and a.getpixel((x, y)) > 128))
ty, tx = tip
px = list(im.tobytes())
lines = []
for i in range(0, len(px), 16):
    lines.append("  " + ",".join(str(v) for v in px[i:i + 16]) + ",")
with open(os.path.join(here, "gojo_sprite.h"), "w") as f:
    f.write("/* создано make_gojo_sprite.py из gojo_sprite.png — руками не править */\n")
    f.write("#define GOJO_TIP_X %d\n#define GOJO_TIP_Y %d\n" % (tx, ty))
    f.write("static const unsigned char kGojoPx[32 * 32 * 4] = {\n" + "\n".join(lines) + "\n};\n")
print("tip", tx, ty)
