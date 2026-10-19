"""Build a deterministic, pinned Windows runtime. Build-time tool only.

Normal application builds verify public/agent pack against agent_pack.json.
To rebuild: --python-zip FILE --wheels DIR --paw-source DIR.
No installer, pip, administrator rights or system Python is used at runtime.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from agent_vendor_patch import patch_paw_source

HERE = Path(__file__).resolve().parent
PUB = HERE.parent / 'public'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_bytes(path):
    # Git on Windows may check text out as CRLF. The runtime pins canonical LF
    # application sources so clean clones build identically on either platform.
    return path.read_bytes().replace(b'\r\n', b'\n')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--python-zip', type=Path)
    p.add_argument('--wheels', type=Path)
    p.add_argument('--paw-source', type=Path)
    p.add_argument('--refresh-app', action='store_true', help='Reuse the verified pinned runtime; refresh only adapter/skills')
    args = p.parse_args()
    lock_path = HERE / 'agent_pack.json'
    if not args.python_zip and not args.refresh_app:
        lock = json.loads(lock_path.read_text(encoding='utf-8'))
        pack = PUB / lock['file']
        if not pack.exists() or pack.stat().st_size != lock['size'] or digest(pack.read_bytes()) != lock['sha256']:
            raise SystemExit('Pinned agent pack missing or corrupt: ' + str(pack))
        # Fail a build if bridge/skills were edited without rebuilding the pack.
        actual_sources = {}
        for source in (HERE / 'agent').rglob('*'):
            if source.is_file() and '__pycache__' not in source.parts:
                rel = source.relative_to(HERE / 'agent').as_posix()
                actual_sources[rel] = digest(source_bytes(source))
                if lock['application_sources'].get(rel) != actual_sources[rel]:
                    raise SystemExit('Rebuild agent pack after editing ' + rel)
        if actual_sources != lock['application_sources']:
            raise SystemExit('Rebuild agent pack after removing application sources')
        if lock.get('backend') != 'QwenPaw':
            raise SystemExit('Expected QwenPaw runtime; rebuild agent pack')
        if lock.get('vendor_patch_sha256') != digest(source_bytes(Path(__file__).with_name('agent_vendor_patch.py'))):
            raise SystemExit('Rebuild agent pack after editing agent_vendor_patch.py')
        print('Verified QwenPaw pack: ' + lock['file'])
        return
    files = {}
    if args.refresh_app:
        if args.python_zip or args.wheels or args.paw_source:
            raise SystemExit('--refresh-app cannot be combined with runtime inputs')
        lock = json.loads(lock_path.read_text(encoding='utf-8'))
        old = PUB / lock['file']
        if digest(old.read_bytes()) != lock['sha256']:
            raise SystemExit('Pinned runtime hash mismatch')
        if lock['vendor_patch_sha256'] != digest(source_bytes(Path(__file__).with_name('agent_vendor_patch.py'))):
            raise SystemExit('Vendor patch changed; rebuild full runtime')
        with zipfile.ZipFile(old) as z:
            manifest = json.loads(z.read('manifest.json'))
            for name, expected in manifest.items():
                data = z.read(name)
                if digest(data) != expected:
                    raise SystemExit('Runtime entry hash mismatch: ' + name)
                if name not in lock['application_sources']:
                    files[name] = data
        source_hashes, wheels = lock['upstream_source_files'], lock['wheels']
        python_lock = lock['python']
    else:
        with zipfile.ZipFile(args.python_zip) as z:
            for name in z.namelist():
                if not name.endswith('/'):
                    files[name] = z.read(name)
        # Embedded distribution isolates imports from system/user installs and CWD.
        files['python313._pth'] = b'python313.zip\n.\nLib/site-packages\nLib/site-packages/win32\nLib/site-packages/win32/lib\nLib/site-packages/pywin32_system32\n'
        wheels = []
        seen_wheels = set()
        for wheel in sorted(args.wheels.glob('*.whl')):
            distribution = wheel.name.split('-')[0].lower().replace('_', '-')
            if distribution in seen_wheels:
                raise SystemExit('Multiple versions of dependency: ' + distribution)
            seen_wheels.add(distribution)
            wheels.append({'file': wheel.name, 'sha256': digest(wheel.read_bytes()),
                           'url': 'https://pypi.org/project/' + wheel.name.split('-')[0] + '/'})
            with zipfile.ZipFile(wheel) as z:
                for name in z.namelist():
                    if name.endswith('/'):
                        continue
                    data = z.read(name)
                    if '.data/' in name:
                        _, kind, rel = name.split('/', 2)
                        if kind not in ('purelib', 'platlib'):
                            continue
                        name = rel
                    files['Lib/site-packages/' + name] = data
        source_hashes = {}
        package = args.paw_source / 'src'
        for source in sorted((package / 'qwenpaw').rglob('*')):
            if source.is_file() and '__pycache__' not in source.parts and source.suffix != '.pyc':
                rel = source.relative_to(package).as_posix()
                # Upstream document skills have separate proprietary licenses.
                # Ship CursorPad's own skills, not these third-party skill copies.
                if rel.startswith('qwenpaw/agents/skills/'):
                    continue
                data = source.read_bytes()
                source_hashes[rel] = digest(data)
                files['Lib/site-packages/' + rel] = patch_paw_source(rel, data)
        files['licenses/QwenPaw-LICENSE.txt'] = (args.paw_source / 'LICENSE').read_bytes()
        files['licenses/CursorPad-QwenPaw-integration.txt'] = (
            'QwenPaw 2.2.2, Apache-2.0. CursorPad native integration.\n'
            'Optional browser bootstrap is skipped only when QWENPAW_CURSORPAD_NATIVE_ONLY=1; '
            'see native/agent_vendor_patch.py. Upstream proprietary document skill copies are excluded.\n'
            'Only CursorPad tools and original CursorPad skills are provided to the agent.\n').encode()
        python_lock = {'version': '3.13.16', 'sha256': digest(args.python_zip.read_bytes()),
                       'url': 'https://www.python.org/ftp/python/3.13.16/python-3.13.16-embed-amd64.zip'}
    application_sources = {}
    for source in sorted((HERE / 'agent').rglob('*')):
        if source.is_file() and '__pycache__' not in source.parts:
            rel = source.relative_to(HERE / 'agent').as_posix()
            files[rel] = source_bytes(source)
            application_sources[rel] = digest(files[rel])
    manifest = {name: digest(data) for name, data in sorted(files.items())}
    files['manifest.json'] = json.dumps(manifest, sort_keys=True, indent=2).encode()
    PUB.mkdir(exist_ok=True)
    pack = PUB / 'agent-building.zip'
    with zipfile.ZipFile(pack, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2026, 10, 17, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            z.writestr(entry, data, compresslevel=9)
    sha = digest(pack.read_bytes())
    name = 'CursorPad-paw-' + sha[:16] + '.zip'
    target = PUB / name
    pack.replace(target)
    lock = {'file': name, 'sha256': sha, 'size': target.stat().st_size,
            'python': python_lock,
            'backend': 'QwenPaw', 'qwenpaw_version': '2.2.2', 'upstream_source_files': source_hashes,
            'vendor_patch_sha256': digest(source_bytes(Path(__file__).with_name('agent_vendor_patch.py'))),
            'wheels': wheels, 'application_sources': application_sources}
    lock_path.write_text(json.dumps(lock, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (HERE / 'agent_pack.h').write_text(
        '/* Generated by build_agent_pack.py. Pinned as part of this executable. */\n'
        f'#define AQ_PACK_FILE L"{name}"\n#define AQ_PACK_SHA "{sha}"\n'
        f'#define AQ_PACK_SIZE {target.stat().st_size}UL\n', encoding='utf-8')
    print(json.dumps({'file': name, 'size': target.stat().st_size, 'sha256': sha, 'files': len(files)}))


if __name__ == '__main__':
    main()
