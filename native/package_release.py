"""Shared deterministic packaging for build.sh and build.ps1. No dependencies."""
import hashlib
from pathlib import Path
import re
import shutil
import sys
import zipfile

pub, native = map(Path, sys.argv[1:3])
version = (native / 'version.txt').read_text(encoding='utf-8').splitlines()[:2]
header = (native / 'version.h').read_text(encoding='utf-8')
assert re.search(r'#define APP_VERSION (\d+)', header)[1] == version[0]
assert re.search(r'#define APP_VERSION_A "([^"]+)"', header)[1] == version[1]
date = tuple(map(int, version[1].split('.')[:3])) + (0, 0, 0)
shutil.copyfile(native / 'cursorpad.manifest', pub / 'CursorPad.exe.manifest')
shutil.copyfile(pub / 'CursorPad.exe', pub / 'CursorPad.bin')
(pub / 'cursors-win').mkdir(exist_ok=True)
for cursor in (native / 'cursors').glob('*.cur'):
    shutil.copyfile(cursor, pub / 'cursors-win' / cursor.name)
with zipfile.ZipFile(pub / 'CursorPad.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for source, name in [(pub / 'CursorPad.exe', 'CursorPad.exe'),
                         (native / 'cursorpad.manifest', 'CursorPad.exe.manifest'),
                         (native / 'README.txt', 'README.txt'), (native / 'AI_TOOLS.md', 'AI_TOOLS.md'),
                         (native / 'ocr.ps1', 'ocr.ps1')]:
        entry = zipfile.ZipInfo(name, date)
        entry.compress_type = zipfile.ZIP_DEFLATED
        entry.external_attr = 0o644 << 16
        archive.writestr(entry, source.read_bytes())
sha = hashlib.sha256((pub / 'CursorPad.bin').read_bytes()).hexdigest()
notes = (native / 'whatsnew.txt').read_text(encoding='utf-8').rstrip()
(pub / 'version.txt').write_text('\n'.join(version + ['sha256 ' + sha, notes]) + '\n', encoding='utf-8')
print(f'CursorPad {version[1]} sha256 {sha}')
