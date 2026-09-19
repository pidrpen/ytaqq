#!/bin/sh
set -eu
cd "$(dirname "$0")"
ZIG="${ZIG:-/opt/zig/zig}"
"$ZIG" rc /fo /tmp/cursorpad.res cursorpad.rc
"$ZIG" cc -target x86_64-windows-gnu -O2 -finput-charset=UTF-8 \
  -o /workspace/public/CursorPad.exe cursorpad.c /tmp/cursorpad.res \
  -luser32 -lgdi32 -lshell32 -lole32 -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 \
  -Wl,--subsystem,windows -municode
cp cursorpad.manifest /workspace/public/CursorPad.exe.manifest
mkdir -p /workspace/public/cursors-win
cp -f cursors/*.cur /workspace/public/cursors-win/
python3 - << 'PY'
import zipfile, pathlib
root = pathlib.Path("/workspace/public")
native = pathlib.Path("/workspace/native")
with zipfile.ZipFile(root / "CursorPad.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.write(root / "CursorPad.exe", "CursorPad.exe")
    z.write(native / "cursorpad.manifest", "CursorPad.exe.manifest")
    z.write(native / "README.txt", "README.txt")
    z.write(native / "ocr.ps1", "ocr.ps1")
    for p in sorted((native / "cursors").glob("*.cur")):
        z.write(p, f"cursors/{p.name}")
    for p in sorted((native / "cursors").glob("*.ani")):
        z.write(p, f"cursors/{p.name}")
print("wrote", root / "CursorPad.zip")
PY
cp -f /workspace/public/CursorPad.exe /workspace/artifacts/CursorPad.exe
cp -f /workspace/public/CursorPad.zip /workspace/artifacts/CursorPad.zip
ls -lh /workspace/public/CursorPad.exe /workspace/public/CursorPad.zip
