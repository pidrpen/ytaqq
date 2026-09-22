#!/bin/sh
# Сборка CursorPad — одна на всех.
#
# Раньше выпуски собирались двумя разными компиляторами (zig и mingw), а
# настоящий скрипт выпуска лежал вне репозитория: exe выходили разного
# размера, и следующий, кто садился собирать, собирал не то, что выходило.
# Теперь сборка одна: mingw-w64, пути только внутри репозитория.
#
# Нужно: x86_64-w64-mingw32-gcc, x86_64-w64-mingw32-windres, python3.
#   Debian/Ubuntu: apt install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64
#
# Запуск из любого места:  sh native/build.sh
# Номер выпуска правится в двух местах — version.h и version.txt; скрипт
# проверяет, что они совпадают, иначе программа будет врать о своей версии.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
PUB="$ROOT/public"
CC=x86_64-w64-mingw32-gcc
RC=x86_64-w64-mingw32-windres
# предупреждение компилятора — это ошибка: сейчас их ноль, пусть так и будет
CFLAGS="-O2 -Wall -Werror -Wno-unknown-pragmas"

for tool in "$CC" "$RC" python3; do
  command -v "$tool" >/dev/null 2>&1 || { echo "нет $tool — см. шапку build.sh"; exit 1; }
done

V_NUM=$(sed -n 's/^#define APP_VERSION \([0-9][0-9]*\).*/\1/p' "$HERE/version.h")
V_STR=$(sed -n 's/^#define APP_VERSION_A "\(.*\)".*/\1/p' "$HERE/version.h")
T_NUM=$(sed -n '1p' "$HERE/version.txt")
T_STR=$(sed -n '2p' "$HERE/version.txt")
if [ "$V_NUM" != "$T_NUM" ] || [ "$V_STR" != "$T_STR" ]; then
  echo "версия расходится: version.h $V_NUM/$V_STR, version.txt $T_NUM/$T_STR"
  exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cd "$HERE"
# помощник распознавания зашит в exe ресурсом, поэтому собирается первым
$CC $CFLAGS -o CursorPadOcr.exe ocr_module.c -lole32 -lruntimeobject -luser32 -municode
$RC cursorpad.rc -O coff -o "$TMP/cursorpad.res"
$CC $CFLAGS -finput-charset=UTF-8 -o "$PUB/CursorPad.exe" cursorpad.c "$TMP/cursorpad.res" \
  -luser32 -lgdi32 -lshell32 -lole32 -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 \
  -Wl,--subsystem,windows -municode

cp -f cursorpad.manifest "$PUB/CursorPad.exe.manifest"
mkdir -p "$PUB/cursors-win"
cp -f cursors/*.cur "$PUB/cursors-win/"

python3 - "$PUB" "$HERE" <<'PY'
import sys, zipfile, pathlib
pub, native = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
with zipfile.ZipFile(pub / "CursorPad.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.write(pub / "CursorPad.exe", "CursorPad.exe")
    z.write(native / "cursorpad.manifest", "CursorPad.exe.manifest")
    z.write(native / "README.txt", "README.txt")
    z.write(native / "ocr.ps1", "ocr.ps1")
    # курсоры зашиты в exe ресурсами; в архиве они были бы второй копией
PY

# jsDelivr не отдаёт .exe, поэтому зеркала получают те же байты как .bin.
# Сам .exe в public не публикуется (он в .gitignore): это копия .bin.
cp -f "$PUB/CursorPad.exe" "$PUB/CursorPad.bin"
cp -f "$HERE/version.txt" "$PUB/version.txt"

echo "CursorPad $V_STR"
ls -l "$PUB/CursorPad.bin" "$PUB/CursorPad.zip" | awk '{print "  " $9 "  " $5}'
