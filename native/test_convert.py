"""Round-trip native image conversion, source preservation and collision tests.

Run with --probe pointing to convert_test.exe (or a Wine wrapper) and Pillow.
Office exports require Microsoft Office and are not exercised by this probe.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument('--probe', type=Path, required=True)
a = ap.parse_args()
probe = str(a.probe.resolve())
def formats(name, office='00'):
    return subprocess.run([probe, '--formats', name, office], capture_output=True, encoding='utf-8', check=True).stdout.split()
assert formats('photo.PNG') == ['jpg', 'bmp', 'tif']
assert formats('photo.jpeg') == ['png', 'bmp', 'tif']
assert formats('page.tiff') == ['png', 'jpg', 'bmp']
assert formats('document.docx') == []
assert formats('document.docx', '10') == ['pdf', 'txt']
assert formats('book.xlsx', '01') == ['pdf', 'csv']
assert formats('archive.zip', '11') == []

with tempfile.TemporaryDirectory(prefix='cursorpad-convert-') as directory:
    root = Path(directory)
    # Native probe reports Windows paths; file assertions use the shared folder.
    source = root / 'Исходник.png'
    image = Image.new('RGBA', (24, 16), (0, 0, 0, 0))
    for y in range(4, 12):
        for x in range(6, 18): image.putpixel((x, y), (220, 30, 20, 255))
    image.save(source)
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    def run(path, target):
        return subprocess.run([probe, str(path), str(target)], capture_output=True, encoding='utf-8', timeout=30)
    for target, ext, expected in [(1, 'jpg', 'JPEG'), (2, 'bmp', 'BMP'), (3, 'tif', 'TIFF')]:
        result = run(source, target)
        assert result.returncode == 0, result.stdout + result.stderr
        converted = root / ('Исходник.' + ext)
        with Image.open(converted) as saved:
            assert saved.size == (24, 16) and saved.format == expected
            if ext == 'jpg':
                pixel = saved.convert('RGB').getpixel((0, 0))
                assert min(pixel) > 235, pixel  # White transparency background.
        digest = hashlib.sha256(converted.read_bytes()).hexdigest()
        assert run(source, target).returncode == 0
        assert (root / ('Исходник (2).' + ext)).is_file()
        assert hashlib.sha256(converted.read_bytes()).hexdigest() == digest
        assert hashlib.sha256(source.read_bytes()).hexdigest() == before
    # Convert back through the real Windows decoder and PNG encoder.
    assert run(root / 'Исходник.bmp', 0).returncode == 0
    with Image.open(root / 'Исходник (2).png') as png: assert png.format == 'PNG' and png.size == (24, 16)
    # Multipage TIFF must not silently lose pages.
    multi = root / 'Страницы.tif'
    frame = Image.new('RGB', (10, 10), 'red')
    frame.save(multi, save_all=True, append_images=[Image.new('RGB', (10, 10), 'blue')])
    multi_hash = hashlib.sha256(multi.read_bytes()).hexdigest()
    assert run(multi, 0).returncode == 1
    assert not (root / 'Страницы.png').exists()
    assert hashlib.sha256(multi.read_bytes()).hexdigest() == multi_hash
    corrupt = root / 'Ошибка.png'; corrupt.write_bytes(b'not an image')
    assert run(corrupt, 1).returncode == 1
    assert not (root / 'Ошибка.jpg').exists()
    assert run(root, 0).returncode == 1
    assert run(source, 0).returncode == 1  # Same-format request is unsupported.
    assert run(source, 99).returncode == 1
    assert not list(root.glob('.CursorPad-*'))
print('PASS: format availability, real encoders, dimensions, JPEG alpha, duplicate names, source hashes, multipage rejection, failed-output cleanup')
