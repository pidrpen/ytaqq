"""Prepare an isolated profile and run the CVX_LAB native integration probe.

Uses a locally verified pinned runtime, marking it installed to avoid the
unrelated QwenPaw bootstrap under Wine. Production setup is unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile
from PIL import Image
from pypdf import PdfReader, PdfWriter

ap=argparse.ArgumentParser();ap.add_argument('--probe',type=Path,required=True);ap.add_argument('--root',type=Path,required=True)
a=ap.parse_args();repo=Path(__file__).resolve().parent.parent;lock=json.loads((repo/'native/agent_pack.json').read_text())
archive=repo/'public'/lock['file'];assert hashlib.sha256(archive.read_bytes()).hexdigest()==lock['sha256']
root=a.root.resolve();root.mkdir(parents=True,exist_ok=False);(root/'inputs').mkdir();(root/'out').mkdir()
Image.new('RGB',(640,320),'red').save(root/'inputs/one.png');Image.new('RGB',(300,500),'blue').save(root/'inputs/Фото.png')
w=PdfWriter();w.add_blank_page(width=100,height=200);w.add_blank_page(width=300,height=400);w.write(root/'inputs/pages.pdf')
before={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'inputs').iterdir()}
runtime=root/'agent'/lock['sha256'];runtime.mkdir(parents=True)
with zipfile.ZipFile(archive) as z:z.extractall(runtime)
(runtime/'ready.txt').write_text(lock['sha256'])
result=subprocess.run([str(a.probe.resolve()),str(root)],timeout=180)
assert result.returncode==0,(result.returncode,(root/'error.txt').read_text() if (root/'error.txt').exists() else 'no error report')
assert (root/'passed.txt').exists()
for name,size in [('one-размер.png',(320,160)),('Фото-размер.png',(150,250))]:
    with Image.open(root/'out'/name) as image:assert image.size==size
assert len(PdfReader(root/'out/Пакет чертежей.pdf').pages)==3
assert not (root/'out/one.zip').exists()
history=json.loads((root/'conversions/history.json').read_text());assert any(e['state']=='undone' for e in history['entries'])
assert len(history['entries'])==4
for path,digest in before.items():assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
desktop=root/'desktop'
assert (desktop/'Новая папка').is_dir() and (desktop/'Новая папка (2)').is_dir()
assert (desktop/'Новый текст.txt').read_bytes()==b'' and (desktop/'Новый текст (2).txt').read_bytes()==b''
import xml.etree.ElementTree as ET
for name,part in [('Новый документ','word/document.xml'),('Новая таблица','xl/workbook.xml')]:
    extension='.docx' if part.startswith('word') else '.xlsx'
    for ending in ['', ' (2)']:
        with zipfile.ZipFile(desktop/(name+ending+extension)) as z:
            assert z.testzip() is None
            for member in z.namelist():ET.fromstring(z.read(member))
            assert part in z.namelist()
print((root/'passed.txt').read_text())
print('PASS: desktop native TXT/DOCX/XLSX/folder creation, collisions, MSAA whitespace/item distinction, gesture dispatch, Shift/Esc cancellation; no real Desktop writes')
