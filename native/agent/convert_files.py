"""Local file tools. No model, network, pip, source edits or silent overwrite."""
import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import hashlib
import io
import json
import math
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import time
import uuid
import zipfile

MAX_FILES = 128
MAX_PAGES = 2000
# Pillow by default refuses images over ~179 MP as a "decompression bomb". Files
# here are local and chosen by the user; an A0 drawing scanned at 400 dpi is 266 MP.
MAX_PIXELS = 1_000_000_000
IMAGE_EXT = {'.png', '.jpg', '.jpeg', '.bmp', '.tif', '.tiff'}
PDF_DPI = 300  # usual for drawings: A4 = 2480x3508, readable dimensions and thin lines


def size_label(size):
    if size < 1024:
        return f'{size} байт'
    if size < 1048576:
        return f'{size / 1024:.1f} КБ'
    return f'{size / 1048576:.2f} МБ'


def digest_file(path):
    with open(path, 'rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def stamp(path):
    path = Path(path)
    info = path.stat()
    return {'path': str(path.resolve()), 'size': info.st_size, 'dev': info.st_dev,
            'ino': info.st_ino, 'sha256': digest_file(path)}


def load_history(path):
    if not path.exists():
        return {'version': 1, 'entries': []}
    data = json.loads(path.read_text(encoding='utf-8'))
    if data.get('version') != 1 or not isinstance(data.get('entries'), list):
        raise ValueError('Не удалось прочитать историю операций')
    return data


def save_history(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                     prefix='history-', suffix='.tmp', delete=False) as file:
        temp = Path(file.name)
        json.dump(data, file, ensure_ascii=False)
        file.flush()
        os.fsync(file.fileno())
    try:
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def delete_owned(record):
    """On Windows lock, verify and delete the very same file handle."""
    path = Path(record['path'])
    if path.is_symlink() or not path.is_file():
        return False
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        import msvcrt
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                       ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.SetFileInformationByHandle.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        kernel.SetFileInformationByHandle.restype = wintypes.BOOL
        handle = kernel.CreateFileW(str(path), 0x80010000, 0, None, 3, 0x00200000, None)
        if handle == ctypes.c_void_p(-1).value:
            return False
        fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
        with os.fdopen(fd, 'rb') as file:
            info = os.fstat(file.fileno())
            if not same_file(info, record) or hashlib.file_digest(file, 'sha256').hexdigest() != record['sha256']:
                return False
            remove = ctypes.c_ubyte(1)  # FILE_DISPOSITION_INFO.DeleteFile
            return bool(kernel.SetFileInformationByHandle(handle, 4, ctypes.byref(remove), 1))
    with path.open('rb') as file:
        info = os.fstat(file.fileno())
        if not same_file(info, record) or hashlib.file_digest(file, 'sha256').hexdigest() != record['sha256']:
            return False
        current = path.lstat()
        if (current.st_dev, current.st_ino) != (info.st_dev, info.st_ino):
            return False
        path.unlink()
        return True


def same_file(info, record):
    return stat.S_ISREG(info.st_mode) and (info.st_dev, info.st_ino, info.st_size) == (record['dev'], record['ino'], record['size'])


def pages(spec, count):
    if not spec.strip():
        return list(range(count))
    result = []
    for part in spec.replace(' ', '').split(','):
        bits = part.split('-')
        if not part or len(bits) > 2 or not all(x.isdecimal() for x in bits):
            raise ValueError('Страницы: используйте вид 1-3,5,7')
        start, end = int(bits[0]), int(bits[-1])
        if not 1 <= start <= end <= count:
            raise ValueError(f'Номер страницы вне диапазона 1–{count}')
        result.extend(range(start - 1, end))
        if len(result) > MAX_PAGES:
            raise ValueError('Не больше 2000 страниц за операцию')
    if not result:
        raise ValueError('Не выбраны страницы')
    return result


class Task:
    def __init__(self, request):
        self.request = request
        self.stack = ExitStack()
        self.staged, self.published = [], []

    def check(self):
        cancel = self.request.get('cancel')
        if cancel and Path(cancel).exists():
            raise ValueError('Операция отменена; исходники сохранены')

    def stage(self, folder, name):
        self.check()
        folder = Path(folder).resolve()
        if not folder.is_dir():
            raise ValueError('Папка результата не существует')
        if name != Path(name).name or name in ('', '.', '..'):
            raise ValueError('Недопустимое имя результата')
        directory = Path(self.stack.enter_context(tempfile.TemporaryDirectory(prefix='.CursorPad-', dir=folder)))
        temp = directory / name
        self.staged.append((temp, folder / name))
        return temp

    def destination(self, source):
        return Path(self.request.get('output_dir') or source.parent)

    def commit(self, history_path, label):
        data = load_history(history_path)
        entry = {'id': uuid.uuid4().hex, 'time': datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC'),
                 'label': label, 'outputs': [], 'state': 'ready'}
        entry['time'] = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')
        anchor = self.request.get('anchor') or next(iter(self.request.get('files', [])), '')
        if not anchor and self.request.get('prepared'):
            anchor = self.request['prepared'][0].get('original', '')
        entry['source'] = Path(anchor).name if anchor else ''
        try:
            for temp, wanted in self.staged:
                self.check()
                if not temp.is_file() or temp.stat().st_size == 0:
                    raise ValueError('Конвертер не создал содержимое результата')
                # Capture the identity/hash while the file is still private.
                # After publication another process may replace or lock it;
                # never record that replacement as an output owned by us.
                record = stamp(temp)
                for number in range(1, 10001):
                    target = wanted if number == 1 else wanted.with_name(f'{wanted.stem} ({number}){wanted.suffix}')
                    # rename on Windows and link on POSIX both refuse replacement.
                    try:
                        if os.name == 'nt':
                            os.rename(temp, target)
                        else:
                            os.link(temp, target)
                            temp.unlink()
                        self.published.append(target)
                        record['path'] = str(target)
                        entry['outputs'].append(record)
                        break
                    except FileExistsError:
                        continue
                else:
                    raise ValueError('Не удалось подобрать свободное имя результата')
            data['entries'].insert(0, entry)
            data['entries'] = data['entries'][:200]
            save_history(history_path, data)
        except BaseException:
            for output in entry['outputs']:
                try:
                    delete_owned(output)
                except OSError:
                    pass
            raise
        total = sum(x['size'] for x in entry['outputs'])
        return {'ok': True, 'first_output': entry['outputs'][0]['path'], 'id': entry['id'],
                'summary': f"Готово: файлов {len(entry['outputs'])}, размер {size_label(total)}. Исходники сохранены."}


def flatten(image):
    from PIL import Image
    if 'A' not in image.getbands() and image.mode != 'P':
        return image.convert('RGB')
    rgba = image.convert('RGBA')
    result = Image.new('RGB', rgba.size, 'white')
    result.paste(rgba, mask=rgba.getchannel('A'))
    return result


def eight_bit(image):
    """16/32-bit or float grayscale to 8 bits by its real range; plain convert clips it to white."""
    if image.mode != 'F' and not image.mode.startswith('I'):
        return image
    wide = image.convert('F')
    top = wide.getextrema()[1]
    scale = 255.0 if top <= 1 else 1.0 if top <= 255 else 255.0 / 65535 if top <= 65535 else 255.0 / top
    return wide.point(lambda value: value * scale).convert('L')


def fit(image, ext):
    """A mode the target format stores as is: B/W scans stay 1-bit, CMYK and 16-bit no longer fail."""
    transparent = 'A' in image.getbands() or (image.mode == 'P' and 'transparency' in image.info)
    if image.mode == 'F' or (image.mode.startswith('I') and not (ext in ('png', 'tif') and image.mode == 'I;16')):
        image = eight_bit(image)
    if ext in ('jpg', 'bmp', 'pdf'):
        if transparent:
            return flatten(image)
        if image.mode == '1':
            return image.convert('L') if ext == 'jpg' else image
        if image.mode == 'L' or (image.mode == 'P' and ext == 'bmp'):
            return image
        return image.convert('RGB')
    keep = ('1', 'L', 'LA', 'P', 'RGB', 'RGBA', 'I;16') + (('CMYK',) if ext == 'tif' else ())
    return image if image.mode in keep else image.convert('RGBA' if transparent else 'RGB')


def resolution(image, scale=1.0):
    """The source dpi if plausible; scale keeps the physical size after a resize."""
    try:
        dpi = tuple(float(value) * scale for value in image.info.get('dpi', ()))
    except (TypeError, ValueError):
        return None
    return dpi if len(dpi) == 2 and all(math.isfinite(value) and 10 <= value <= 2400 for value in dpi) else None


def save_image(image, output, ext, dpi, icc):
    formats = {'png': 'PNG', 'jpg': 'JPEG', 'bmp': 'BMP', 'tif': 'TIFF'}
    # without a source dpi TIFF would get Pillow's 1 dpi; 96 is the usual screen value
    options = {'dpi': dpi or (96, 96)} if dpi or ext == 'tif' else {}
    if ext == 'jpg':
        options['quality'] = 95
    if ext == 'tif':
        options['compression'] = 'group4' if image.mode == '1' else 'tiff_lzw'
    if icc and ext != 'bmp':
        options['icc_profile'] = icc
    image.save(output, formats[ext], **options)


def image_frames(source):
    from PIL import Image, ImageOps
    Image.MAX_IMAGE_PIXELS = MAX_PIXELS
    with Image.open(source) as image:
        count = getattr(image, 'n_frames', 1)
        if count > MAX_PAGES:
            raise ValueError('В изображении больше 2000 страниц')
        for index in range(count):
            image.seek(index)
            yield index, count, ImageOps.exif_transpose(image).copy()


def small_jpeg(image, budget, task):
    from PIL import Image
    dpi = resolution(image)
    image = fit(image, 'jpg')
    for attempt in range(40):
        task.check()
        best = None
        low, high = 25, 95
        while low <= high:
            quality = (low + high) // 2
            buffer = io.BytesIO()
            image.save(buffer, 'JPEG', quality=quality, optimize=True, progressive=True, **({'dpi': dpi} if dpi else {}))
            value = buffer.getvalue()
            if len(value) <= budget:
                best = value
                low = quality + 1
            else:
                high = quality - 1
        if best is not None:
            return best
        width, height = image.size
        if width == height == 1:
            break
        image = image.resize((max(1, int(width * .8)), max(1, int(height * .8))), Image.Resampling.LANCZOS)
        dpi = dpi and (dpi[0] * .8, dpi[1] * .8)
    raise ValueError('Лимит слишком мал: даже минимальный JPG не помещается')


def images(task, files, op):
    from PIL import Image
    ext = task.request.get('format', 'jpg' if op == 'compress' else 'png')
    if ext not in ('png', 'jpg', 'bmp', 'tif'):
        raise ValueError('Неподдерживаемый формат изображения')
    for source in files:
        if op == 'convert' and source.suffix.lower() == '.pdf':
            pdf_images(task, source, ext)
            continue
        if source.suffix.lower() not in IMAGE_EXT:
            raise ValueError(f'Ожидается изображение: {source.name}')
        for index, count, image in image_frames(source):
            task.check()
            suffix = f'-стр-{index + 1:03}' if count > 1 else ''
            original_ext = {'.jpeg': 'jpg', '.tiff': 'tif'}.get(source.suffix.lower(), source.suffix.lower()[1:])
            if op == 'convert' and count == 1 and original_ext == ext:
                shutil.copyfile(source, task.stage(task.destination(source), source.name))
                continue
            dpi, icc = resolution(image), image.info.get('icc_profile')
            if op == 'resize':
                width, height = image.size
                if task.request.get('resize_mode') == 'percent':
                    percent = int(task.request.get('percent', 50))
                    if not 1 <= percent <= 1000:
                        raise ValueError('Масштаб должен быть от 1 до 1000%')
                    size = max(1, round(width * percent / 100)), max(1, round(height * percent / 100))
                else:
                    target = int(task.request.get('width', 1920))
                    if not 1 <= target <= 30000:
                        raise ValueError('Ширина должна быть от 1 до 30000 пикселей')
                    size = target, max(1, round(height * target / width))
                if size[0] * size[1] > 100000000:
                    raise ValueError('Результат больше 100 миллионов пикселей')
                if image.mode in ('1', 'P'):  # 1-bit/palette resize only by nearest pixel: lines would break
                    image = image.convert('L' if image.mode == '1' else 'RGBA')
                dpi = resolution(image, size[0] / width)
                image = image.resize(size, Image.Resampling.LANCZOS)
                suffix += '-размер'
            if op == 'compress':
                budget = int(task.request.get('max_bytes', 1048576))
                if not 512 <= budget <= 104857600:
                    raise ValueError('Лимит файла: от 512 байт до 100 МБ')
                output = task.stage(task.destination(source), f'{source.stem}{suffix}-сжат.jpg')
                output.write_bytes(small_jpeg(image, budget, task))
            else:
                output = task.stage(task.destination(source), f'{source.stem}{suffix}.{ext}')
                ready = fit(image, ext)
                save_image(ready, output, ext, dpi, icc if ready.mode == image.mode else None)


def line_art(image):
    """A rendered page of only dark lines on white (drawing, text) becomes 1-bit; colour and photos stay."""
    from PIL import ImageChops
    rgb = image.convert('RGB')
    red, green, blue = rgb.split()
    spread = max(ImageChops.difference(a, b).getextrema()[1] for a, b in ((red, green), (green, blue), (red, blue)))
    if spread > 24:
        return rgb
    gray = rgb.convert('L')
    histogram = gray.histogram()
    middle = sum(histogram[48:208]) / max(1, gray.width * gray.height)
    # light threshold keeps thin anti-aliased lines; mid-tones only on edges mean no photo on the page
    return gray.point(lambda value: 255 if value > 200 else 0, mode='1') if middle < 0.04 else gray


def pdf_images(task, source, ext):
    """PDF pages to images. Drawn by CursorPadOcr.exe --pdf (Windows.Data.Pdf) in its own process."""
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = MAX_PIXELS
    renderer = task.request.get('pdf_renderer')
    if not renderer:
        raise ValueError('Перевод PDF в картинки недоступен: нет модуля отрисовки')
    count = len(pdf_reader(source, task.stack).pages)  # password and page limit in plain words first
    dpi = int(task.request.get('pdf_dpi', PDF_DPI))
    if not 72 <= dpi <= 600:
        raise ValueError('DPI для PDF: от 72 до 600')
    work = Path(task.stack.enter_context(tempfile.TemporaryDirectory(prefix='CursorPad-pdf-')))
    command = ([renderer] if isinstance(renderer, str) else list(renderer)) + ['--pdf', str(source), str(work), str(dpi)]
    listing = work / 'pages.txt'
    with open(listing, 'w', encoding='utf-8') as out, open(work / 'errors.txt', 'w', encoding='utf-8') as err:
        process = subprocess.Popen(command, stdout=out, stderr=err, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        deadline = time.monotonic() + 60 + 30 * count
        try:
            while process.poll() is None:
                task.check()
                if time.monotonic() > deadline:
                    raise ValueError(f'PDF рисуется слишком долго: {source.name}')
                time.sleep(0.1)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    if process.returncode:
        reason = (work / 'errors.txt').read_text(encoding='utf-8', errors='replace').strip()
        raise ValueError(f'Не удалось отрисовать PDF {source.name}: {reason or process.returncode}')
    pages = [line.split() for line in listing.read_text(encoding='utf-8').splitlines() if line.strip()]
    if len(pages) != count or any(len(page) != 2 or Path(page[0]).name != page[0] for page in pages):
        raise ValueError(f'PDF отрисован не полностью: {source.name}')
    for number, (name, used) in enumerate(pages, 1):
        task.check()
        with Image.open(work / name) as image:
            page = line_art(image)
        target = f'{source.stem}.{ext}' if count == 1 else f'{source.stem}-стр-{number:03}.{ext}'
        save_image(fit(page, ext), task.stage(task.destination(source), target), ext, (float(used), float(used)), None)


def pdf_reader(path, stack):
    from pypdf import PdfReader
    reader = PdfReader(stack.enter_context(open(path, 'rb')), strict=False)
    if reader.is_encrypted:
        raise ValueError(f'PDF защищён паролем: {path.name}')
    if not 1 <= len(reader.pages) <= MAX_PAGES:
        raise ValueError('PDF должен содержать от 1 до 2000 страниц')
    return reader


def add_pdf(writer, source, task):
    reader = pdf_reader(source, task.stack)
    if len(writer.pages) + len(reader.pages) > MAX_PAGES:
        raise ValueError('В итоговом PDF больше 2000 страниц')
    for page in reader.pages:
        task.check()
        writer.add_page(page)


def add_image_pdf(writer, source, task):
    from pypdf import PdfReader
    for index, count, image in image_frames(source):
        task.check()
        if len(writer.pages) >= MAX_PAGES:
            raise ValueError('В итоговом PDF больше 2000 страниц')
        buffer = io.BytesIO()
        # B/W scans stay 1-bit (CCITT G4, lossless and small) instead of a JPEG photo of the page
        page = fit(image, 'pdf')
        page.save(buffer, 'PDF', dpi=resolution(image) or (150, 150), **({} if page.mode == '1' else {'quality': 95}))
        buffer.seek(0)
        writer.add_page(PdfReader(buffer).pages[0])


def pdfs(task, files, op):
    from pypdf import PdfWriter
    if op in ('merge', 'packet'):
        writer = PdfWriter()
        for source in files:
            if source.suffix.lower() == '.pdf':
                add_pdf(writer, source, task)
            elif op == 'packet' and source.suffix.lower() in IMAGE_EXT:
                add_image_pdf(writer, source, task)
            else:
                raise ValueError(f'Этот файл нельзя включить в PDF: {source.name}')
        name = 'Пакет чертежей.pdf' if op == 'packet' else 'Объединённый.pdf'
        writer.write(task.stage(task.destination(Path(task.request.get('anchor') or files[0])), name))
        return
    for source in files:
        if op == 'to_pdf' and source.suffix.lower() in IMAGE_EXT:
            writer = PdfWriter()
            add_image_pdf(writer, source, task)
            writer.write(task.stage(task.destination(source), source.stem + '.pdf'))
            continue
        if source.suffix.lower() != '.pdf':
            raise ValueError(f'Ожидается PDF: {source.name}')
        reader = pdf_reader(source, task.stack)
        selected = pages(task.request.get('pages', ''), len(reader.pages))
        if op == 'split':
            for index in selected:
                task.check()
                writer = PdfWriter()
                writer.add_page(reader.pages[index])
                writer.write(task.stage(task.destination(source), f'{source.stem}-стр-{index + 1:03}.pdf'))
        else:
            writer = PdfWriter()
            if op == 'rotate':
                angle = int(task.request.get('rotation', 90))
                if angle not in (90, 180, 270):
                    raise ValueError('Поворот: 90, 180 или 270 градусов')
                for index, page in enumerate(reader.pages):
                    task.check()
                    writer.add_page(page)
                    if index in selected:
                        writer.pages[-1].rotate(angle)
            else:
                for index in selected:
                    task.check()
                    writer.add_page(reader.pages[index])
            ending = '-поворот' if op == 'rotate' else '-страницы'
            writer.write(task.stage(task.destination(source), source.stem + ending + '.pdf'))


def archive(task, files):
    output = task.stage(task.destination(files[0]), files[0].stem + '.zip')
    source_bytes = 0
    used = set()
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive_file:
        for source in files:
            task.check()
            before = source.stat()
            name, number = source.name, 2
            while name.casefold() in used:
                name = f'{source.stem} ({number}){source.suffix}'
                number += 1
            used.add(name.casefold())
            archive_file.write(source, name)
            after = source.stat()
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError(f'Файл изменился во время упаковки: {source.name}')
            source_bytes += before.st_size
    return source_bytes, output.stat().st_size


def execute(request):
    history = Path(request['history'])
    op = request['op']
    data = load_history(history)
    if op == 'history':
        entries = data['entries']
        return {'ok': True, 'history_ids': '\n'.join(e['id'] for e in entries),
                'history_labels': '\n'.join(f"{e['time']} · {e['label']} · {e.get('source', '')} · {len(e['outputs'])} файлов · {size_label(sum(o['size'] for o in e['outputs']))} · " + {'ready': 'готово', 'undone': 'отменено', 'partial': 'частично'}.get(e['state'], e['state']) for e in entries),
                'summary': f'Операций в истории: {len(entries)}'}
    if op in ('undo', 'reveal'):
        entry = next((e for e in data['entries'] if e['id'] == request.get('entry_id')), None)
        if entry is None:
            raise ValueError('Операция не найдена в истории')
        if op == 'reveal':
            output = next((x['path'] for x in entry['outputs'] if Path(x['path']).is_file()), '')
            return {'ok': True, 'first_output': output, 'summary': 'Файлы результата не найдены' if not output else 'Папка результата'}
        removed, skipped = 0, 0
        for output in entry['outputs']:
            if output.get('removed'):
                continue
            try:
                if delete_owned(output):
                    output['removed'] = True
                    removed += 1
                else:
                    skipped += 1
            except OSError:
                skipped += 1
        entry['state'] = 'undone' if not skipped else 'partial'
        save_history(history, data)
        return {'ok': True, 'summary': f'Удалены созданные копии: {removed}. Пропущены изменённые, занятые или отсутствующие: {skipped}. Исходники не затронуты.'}
    files = [Path(p).resolve() for p in request.get('files', [])]
    prepared = request.get('prepared', [])
    if not 1 <= len(files) + len(prepared) <= MAX_FILES:
        raise ValueError('Выберите от 1 до 128 файлов')
    for source in files:
        if not source.is_file():
            raise ValueError(f'Файл не найден: {source.name}')
    task = Task(request)
    labels = {'convert': 'Конвертация', 'resize': 'Размер картинок', 'compress': 'Сжатие картинок', 'zip': 'ZIP',
              'merge': 'Объединение PDF', 'packet': 'Пакет чертежей', 'extract': 'Страницы PDF',
              'split': 'Разделение PDF', 'rotate': 'Поворот PDF', 'to_pdf': 'Преобразование в PDF'}
    try:
        zip_sizes = None
        if files:
            if op in ('convert', 'resize', 'compress'):
                images(task, files, op)
            elif op == 'zip':
                zip_sizes = archive(task, files)
            elif op in ('merge', 'packet', 'extract', 'split', 'rotate', 'to_pdf'):
                pdfs(task, files, op)
            else:
                raise ValueError('Неизвестная операция')
        for item in prepared:
            source = Path(item['path'])
            if not source.is_file():
                raise ValueError('Подготовленный документ отсутствует')
            destination = task.destination(Path(item['original']))
            shutil.copyfile(source, task.stage(destination, item['name']))
        if not task.staged:
            raise ValueError('Нет результатов для сохранения')
        result = task.commit(history, labels.get(op, 'Конвертация'))
        if zip_sizes is not None:
            source_bytes, archive_bytes = zip_sizes
            savings = (1 - archive_bytes / source_bytes) * 100 if source_bytes else 0
            detail = (f'Уменьшение: {savings:.1f}%.' if savings > 0 else
                      'Размер не уменьшился. Уже сжатые файлы, например видео, ZIP почти не сжимает.')
            result['summary'] += f' Исходный размер: {size_label(source_bytes)}. ZIP: {size_label(archive_bytes)}. {detail}'
            result['source_bytes'], result['archive_bytes'] = zip_sizes
        return result
    finally:
        task.stack.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--request', type=Path, required=True)
    parser.add_argument('--response', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = execute(json.loads(args.request.read_text(encoding='utf-8')))
    except Exception as error:
        result = {'ok': False, 'error': str(error)[:1200] or type(error).__name__, 'summary': 'Операция не завершена; исходники сохранены'}
    args.response.write_text(json.dumps(result, ensure_ascii=False), encoding='utf-8')


if __name__ == '__main__':
    main()
