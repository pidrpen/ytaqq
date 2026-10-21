"""Shared deterministic packaging for build.sh and build.ps1. No dependencies."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_sign  # noqa: E402 — подпись выпуска (RELEASE_SIGNING.md)

pub, native = map(Path, sys.argv[1:3])
version = (native / 'version.txt').read_text(encoding='utf-8').splitlines()[:2]
header = (native / 'version.h').read_text(encoding='utf-8')
assert re.search(r'#define APP_VERSION (\d+)', header)[1] == version[0]
assert re.search(r'#define APP_VERSION_A "([^"]+)"', header)[1] == version[1]
date = tuple(map(int, version[1].split('.')[:3])) + (0, 0, 0)
# Выпуск без подписи программа (с 2026.10.21.2) не поставит — лучше не собрать его вовсе.
signing_key = update_sign.load_key()
update_sign.check_header(signing_key)
# Пакет Tesseract принимается только закреплённый в exe (tess.c, tess_pack.h).
tess_list = native.parent / 'public' / 'tess' / 'pack.txt'
if tess_list.exists():
    tess_sha = hashlib.sha256(tess_list.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
    if tess_sha not in (native / 'tess_pack.h').read_text(encoding='utf-8'):
        raise SystemExit('tess_pack.h не от public/tess/pack.txt: python native/make_tess_pack.py --pin')
shutil.copyfile(native / 'cursorpad.manifest', pub / 'CursorPad.exe.manifest')
shutil.copyfile(pub / 'CursorPad.exe', pub / 'CursorPad.bin')
(pub / 'cursors-win').mkdir(exist_ok=True)
for cursor in (native / 'cursors').glob('*.cur'):
    shutil.copyfile(cursor, pub / 'cursors-win' / cursor.name)
with zipfile.ZipFile(pub / 'CursorPad.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for source, name in [(pub / 'CursorPad.exe', 'CursorPad.exe'),
                         (native / 'cursorpad.manifest', 'CursorPad.exe.manifest'),
                         (native / 'README.txt', 'README.txt'), (native / 'AI_TOOLS.md', 'AI_TOOLS.md'),
                         (native / 'QWEN35.md', 'QWEN35.md'),
                         (native / 'ocr.ps1', 'ocr.ps1')]:
        entry = zipfile.ZipInfo(name, date)
        entry.compress_type = zipfile.ZIP_DEFLATED
        entry.external_attr = 0o644 << 16
        archive.writestr(entry, source.read_bytes())
    lock = json.loads((native / 'agent_pack.json').read_text(encoding='utf-8'))
    agent_pack = native.parent / 'public' / lock['file']
    if hashlib.sha256(agent_pack.read_bytes()).hexdigest() != lock['sha256']:
        raise RuntimeError('Agent pack hash mismatch')
    if agent_pack.parent.resolve() != pub.resolve():
        shutil.copyfile(agent_pack, pub / lock['file'])
    entry = zipfile.ZipInfo(lock['file'], date)
    entry.compress_type = zipfile.ZIP_STORED
    entry.external_attr = 0o644 << 16
    archive.writestr(entry, agent_pack.read_bytes())
sha = hashlib.sha256((pub / 'CursorPad.bin').read_bytes()).hexdigest()
tools_lock = json.loads((native / 'addons_pack.json').read_text(encoding='utf-8'))
tools_pack = native.parent / 'public' / tools_lock['file']
if hashlib.sha256(tools_pack.read_bytes()).hexdigest() != tools_lock['sha256']:
    raise RuntimeError('Tools companion hash mismatch')
if tools_pack.parent.resolve() != pub.resolve():
    shutil.copyfile(tools_pack, pub / tools_lock['file'])
# Separate release asset: embedding both companions exceeds GitHub's 100 MiB
# per-file limit. The EXE prepares tools automatically from the pinned asset.
notes = (native / 'whatsnew.txt').read_text(encoding='utf-8').rstrip()
signature = update_sign.signature_line(version[0], version[1], sha, signing_key)
# только LF: на Windows иначе вторая строка «2026.10.21.2\r», и метка выпуска в release.yml ломается
(pub / 'version.txt').write_text('\n'.join(version + ['sha256 ' + sha, signature, notes]) + '\n',
                                 encoding='utf-8', newline='\n')
print(f'CursorPad {version[1]} sha256 {sha}')
