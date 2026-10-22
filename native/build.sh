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
# Проверочная сборка без файлов выпуска (облако, проверка кода):
#   sh native/build.sh --dev   → build/dev/CursorPad.exe, public/ не трогается.
#   Несходство комплектов QwenPaw и tools в ней — только предупреждение
#   (перед выпуском их пересобирают). Выпуск — без --dev, пишет public/.
#   С ключом подписи (ПК) version.txt подписан сразу; без ключа — нет, и
#   такой выпуск программа не поставит: его подписывает GitHub после
#   одобрения владельца (RELEASE_SIGNING.md).
# Номер выпуска правится в двух местах — version.h и version.txt; скрипт
# проверяет, что они совпадают, иначе программа будет врать о своей версии.
# Номер — ГГГГ.ММ.ДД.N, число — ГГГГММДДNN: на N всего две цифры, поэтому
# после .99 (и в новый день) меняется дата, а N начинается с 1. «.100» нельзя:
# 2026.09.23.100 даёт то же число, что 2026.09.24.00.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
PUB="$ROOT/public"
CC=${CC:-x86_64-w64-mingw32-gcc}
RC=${RC:-x86_64-w64-mingw32-windres}
PY=${PY:-python3}
# На самой Windows (Git Bash + WinLibs из winget, gcc 16):
#   CC=gcc RC=windres PY=python LDX=-static sh native/build.sh
# -static — у WinLibs потоки POSIX, без него exe просит libwinpthread-1.dll.
# У WinLibs свой default-manifest.o спорит с нашим манифестом («multiple
# non-default manifests»): переименуйте его в mingw64/x86_64-w64-mingw32/lib.
LDX=${LDX:-}
DEV=0
[ "${1:-}" = "--dev" ] && DEV=1
OUT="$PUB"
[ "$DEV" = 1 ] && OUT="$ROOT/build/dev" && mkdir -p "$OUT"
# предупреждение компилятора — это ошибка: сейчас их ноль, пусть так и будет
CFLAGS="-O2 -Wall -Werror -Wno-unknown-pragmas"
# -s при сборке: без отладочных символов exe меньше примерно на 0,7 МБ — быстрее качается обновление

for tool in "$CC" "$RC" "$PY"; do
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
if [ "$DEV" = 1 ]; then
  "$PY" "$HERE/build_agent_pack.py" || echo "ВНИМАНИЕ: комплект QwenPaw не сходится с native/agent — перед выпуском пересоберите его"
  "$PY" "$HERE/build_addons_pack.py" || echo "ВНИМАНИЕ: комплект tools не сходится — перед выпуском пересоберите его"
else
  "$PY" "$HERE/build_agent_pack.py"
  "$PY" "$HERE/build_addons_pack.py"
fi
# помощник распознавания зашит в exe ресурсом, поэтому собирается первым
$CC $CFLAGS -o CursorPadOcr.exe ocr_module.c -lole32 -lruntimeobject -luser32 -municode -s -Wl,--no-insert-timestamp $LDX
$RC cursorpad.rc -O coff -o "$TMP/cursorpad.res"
$CC $CFLAGS -finput-charset=UTF-8 -o "$OUT/CursorPad.exe" cursorpad.c "$TMP/cursorpad.res" \
  -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 -lcomdlg32 -lmsimg32 \
  -Wl,--subsystem,windows -municode -s -Wl,--no-insert-timestamp $LDX

if [ "$DEV" = 1 ]; then
  echo "Проверочная сборка (не выпуск): $OUT/CursorPad.exe"
  exit 0
fi
"$PY" "$HERE/package_release.py" "$PUB" "$HERE"
