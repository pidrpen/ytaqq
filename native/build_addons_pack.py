"""Pinned offline tools companion; normal builds verify without npm or internet.

Rebuild: --filesystem-source DIR --xlsx-source DIR --node-zip FILE --build-dir DIR.
Only build-time npm is needed. Runtime uses the embedded, verified Node binary.
The separate companion keeps each GitHub file below 100 MiB.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

HERE = Path(__file__).resolve().parent
PUB = HERE.parent / 'public'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sources():
    return {p.relative_to(HERE / 'addons').as_posix(): p.read_bytes().replace(b'\r\n', b'\n')
            for p in sorted((HERE / 'addons').rglob('*')) if p.is_file() and '__pycache__' not in p.parts}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--filesystem-source', type=Path); p.add_argument('--xlsx-source', type=Path)
    p.add_argument('--node-zip', type=Path); p.add_argument('--build-dir', type=Path)
    a = p.parse_args(); lock_path = HERE / 'addons_pack.json'
    own = sources()
    if not a.filesystem_source:
        lock = json.loads(lock_path.read_text(encoding='utf-8'))
        pack = PUB / lock['file']
        if sha(pack.read_bytes()) != lock['sha256'] or pack.stat().st_size != lock['size']:
            raise SystemExit('Tools companion missing or corrupt')
        if {k:sha(v) for k,v in own.items()} != lock['application_sources']:
            raise SystemExit('Rebuild tools companion after editing native/addons')
        # Git on Windows may check the lock out as CRLF; the pin is over LF bytes.
        if sha((HERE / 'addons_npm_lock.json').read_bytes().replace(b'\r\n', b'\n')) != lock['npm_lock_sha256']:
            raise SystemExit('Tools npm lock mismatch')
        print('Verified tools companion: ' + pack.name)
        return
    a.build_dir = a.build_dir.resolve()
    a.build_dir.mkdir(parents=True, exist_ok=True)
    dest = a.build_dir / 'filesystem'; dest.mkdir(exist_ok=True)
    upstream_hashes = {}
    for source in sorted((a.filesystem_source / 'src/filesystem').rglob('*')):
        if not source.is_file() or '__tests__' in source.parts or 'node_modules' in source.parts:
            continue
        rel = source.relative_to(a.filesystem_source / 'src/filesystem')
        if rel.suffix not in ('.ts', '.json') or rel.name.endswith('.test.ts'):
            continue
        data = source.read_bytes(); upstream_hashes[rel.as_posix()] = sha(data)
        target = dest / rel; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
    package = json.loads((dest / 'package.json').read_text(encoding='utf-8'))
    upstream_lock = json.loads((a.filesystem_source / 'package-lock.json').read_text(encoding='utf-8'))
    deps = {name:upstream_lock['packages']['node_modules/' + name]['version'] for name in package['dependencies']}
    deps['esbuild'] = '0.25.12'
    (a.build_dir / 'package.json').write_text(json.dumps({'private':True, 'dependencies':deps}), encoding='utf-8')
    (a.build_dir / 'filesystem-entry.ts').write_bytes(own['filesystem-entry.ts'])
    (dest / 'tsconfig.json').write_text('{"compilerOptions":{"target":"ES2022"}}', encoding='utf-8')
    npm = shutil.which('npm.cmd') or shutil.which('npm')
    pinned_lock = HERE / 'addons_npm_lock.json'
    if pinned_lock.exists():
        shutil.copyfile(pinned_lock, a.build_dir / 'package-lock.json')
    subprocess.run([npm, 'ci' if pinned_lock.exists() else 'install', '--ignore-scripts', '--no-audit', '--no-fund'], cwd=a.build_dir, check=True)
    esbuild = a.build_dir / 'node_modules/.bin/esbuild.cmd'
    subprocess.run([str(esbuild), 'filesystem-entry.ts', '--bundle', '--platform=node', '--format=cjs',
                    '--outfile=filesystem.cjs', '--legal-comments=eof', '--define:import.meta.url=cursorpadModuleUrl',
                    '--banner:js=var cursorpadModuleUrl = require("node:url").pathToFileURL(__filename).href;'], cwd=a.build_dir, check=True)
    files = {'executor.py':own['executor.py'], 'filesystem.cjs':(a.build_dir / 'filesystem.cjs').read_bytes()}
    files['package.json'] = json.dumps({'name':package['name'], 'version':package['version']}).encode()
    with zipfile.ZipFile(a.node_zip) as z:
        prefix = z.namelist()[0].split('/')[0] + '/'
        files['node.exe'] = z.read(prefix + 'node.exe')
        files['licenses/Node-LICENSE.txt'] = z.read(prefix + 'LICENSE')
    xlsx_hashes = {}
    for source in sorted((a.xlsx_source / 'xlsxwriter').rglob('*.py')):
        data = source.read_bytes(); rel = source.relative_to(a.xlsx_source).as_posix()
        files['lib/' + rel] = data; xlsx_hashes[rel] = sha(data)
    files['licenses/XlsxWriter-LICENSE.txt'] = (a.xlsx_source / 'LICENSE.txt').read_bytes()
    files['licenses/Filesystem-MCP-LICENSE.txt'] = (a.filesystem_source / 'LICENSE').read_bytes()
    # Include dependency license texts, not only esbuild's bundled comments.
    modules = a.build_dir / 'node_modules'
    for source in sorted(modules.rglob('*')):
        if source.is_file() and source.name.lower() in ('license', 'license.txt', 'license.md', 'copying'):
            files['licenses/npm/' + source.relative_to(modules).as_posix()] = source.read_bytes()
    npm_lock = (a.build_dir / 'package-lock.json').read_bytes()
    pinned_lock.write_bytes(npm_lock)
    files['licenses/npm-package-lock.json'] = npm_lock
    manifest = {name:sha(data) for name,data in sorted(files.items())}
    files['manifest.json'] = json.dumps(manifest, sort_keys=True, indent=2).encode()
    pack = PUB / 'tools-building.zip'
    with zipfile.ZipFile(pack, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name,data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2026,10,17,0,0,0)); entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16; z.writestr(entry, data, compresslevel=9)
    digest = sha(pack.read_bytes()); name = 'CursorPad-tools-' + digest[:16] + '.zip'
    pack.replace(PUB / name)
    lock = {'file':name, 'sha256':digest, 'size':(PUB / name).stat().st_size,
            'node_archive':{'file':a.node_zip.name, 'sha256':sha(a.node_zip.read_bytes()),
                            'url':'https://nodejs.org/dist/' + a.node_zip.name.split('-win-')[0].replace('node-', '') + '/' + a.node_zip.name},
            'filesystem_version':package['version'], 'filesystem_source_files':upstream_hashes,
            'xlsxwriter_source_files':xlsx_hashes, 'npm_lock_sha256':sha(npm_lock),
            'application_sources':{k:sha(v) for k,v in own.items()}}
    if lock['size'] >= 100 * 1024 * 1024:
        raise SystemExit('Tools pack exceeds GitHub file limit')
    lock_path.write_text(json.dumps(lock, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (HERE / 'addons_pack.h').write_text('/* Generated by build_addons_pack.py. */\n'
        f'#define AT_PACK_FILE L"{name}"\n#define AT_PACK_SHA "{digest}"\n#define AT_PACK_SIZE {lock["size"]}UL\n', encoding='utf-8')
    print(json.dumps({k:lock[k] for k in ('file','sha256','size')}))


if __name__ == '__main__':
    main()
