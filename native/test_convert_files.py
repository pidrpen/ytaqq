"""Local integration tests: real PDFs/images/ZIP and guarded undo, no Office/API."""
import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import sys
import zipfile
from PIL import Image, ImageDraw
from pypdf import PdfReader, PdfWriter
from pypdf.generic import DictionaryObject, NameObject, DecodedStreamObject

module=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).with_name('agent')/'convert_files.py'
spec=importlib.util.spec_from_file_location('convert_files',module)
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)

with tempfile.TemporaryDirectory(prefix='cursorpad-batch-test-') as directory:
    root=Path(directory);history=root/'profile'/'history.json';src=root/'source';src.mkdir();out=root/'out';out.mkdir()
    a=src/'Скан.png';b=src/'Фото.jpg'
    Image.new('RGBA',(640,320),(200,60,20,128)).save(a,dpi=(300,300))
    Image.new('RGB',(300,600),'blue').save(b)
    source_hash={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (a,b)}
    def run(op,files=(),**kwargs):
        return c.execute({'op':op,'files':[str(x) for x in files],'history':str(history),'output_dir':str(out),**kwargs})
    def entry(result):return next(e for e in c.load_history(history)['entries'] if e['id']==result['id'])
    def paths(result):return [Path(x['path']) for x in entry(result)['outputs']]
    converted=run('convert',[a,b],format='png');outputs=paths(converted);assert len(outputs)==2
    for p in outputs:
        with Image.open(p) as image:assert image.format=='PNG'
    again=run('convert',[a,b],format='png');assert set(paths(again)).isdisjoint(outputs)
    resized=run('resize',[a,b],format='png',resize_mode='width',width=1024)
    for p in paths(resized):
        with Image.open(p) as image:assert image.width==1024
    percent=run('resize',[a],format='png',resize_mode='percent',percent=50)
    with Image.open(paths(percent)[0]) as image:assert image.size==(320,160)
    compressed=run('compress',[a,b],max_bytes=4096)
    assert all(p.stat().st_size<=4096 for p in paths(compressed))
    multi=src/'Страницы.tif';Image.new('RGB',(12,18),'red').save(multi,save_all=True,append_images=[Image.new('RGB',(18,12),'blue')])
    converted_multi=run('convert',[multi],format='png');assert len(paths(converted_multi))==2
    pdf1=src/'Первый.pdf';pdf2=src/'Второй.pdf'
    for path,widths in [(pdf1,[100,200,300]),(pdf2,[400,500])]:
        writer=PdfWriter()
        for width in widths:writer.add_blank_page(width=width,height=600)
        if path==pdf1:
            font=DictionaryObject({NameObject('/Type'):NameObject('/Font'),NameObject('/Subtype'):NameObject('/Type1'),NameObject('/BaseFont'):NameObject('/Helvetica')})
            writer.pages[0][NameObject('/Resources')]=DictionaryObject({NameObject('/Font'):DictionaryObject({NameObject('/F1'):font})})
            text=DecodedStreamObject();text.set_data(b'BT /F1 12 Tf 10 10 Td (Vector text preserved) Tj ET')
            writer.pages[0][NameObject('/Contents')]=writer._add_object(text)
        writer.write(path)
    source_hash.update({p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (multi,pdf1,pdf2)})
    merged=run('merge',[pdf2,pdf1]);reader=PdfReader(paths(merged)[0]);assert [int(p.mediabox.width) for p in reader.pages]==[400,500,100,200,300]
    assert 'Vector text preserved' in reader.pages[2].extract_text()
    extracted=run('extract',[pdf1],pages='3,1-2');assert [int(p.mediabox.width) for p in PdfReader(paths(extracted)[0]).pages]==[300,100,200]
    split=run('split',[pdf1]);assert len(paths(split))==3 and all(len(PdfReader(p).pages)==1 for p in paths(split))
    rotated=run('rotate',[pdf1],pages='2',rotation=90);assert [p.rotation for p in PdfReader(paths(rotated)[0]).pages]==[0,90,0]
    packet=run('packet',[pdf1,a,multi,pdf2]);assert len(PdfReader(paths(packet)[0]).pages)==8
    image_pdf=run('to_pdf',[multi]);assert len(PdfReader(paths(image_pdf)[0]).pages)==2
    physical=run('to_pdf',[a]);assert abs(float(PdfReader(paths(physical)[0]).pages[0].mediabox.width)-153.6)<.1
    # Scans of drawings and awkward modes: dpi survives, B/W stays 1-bit, CMYK and 16-bit do not fail.
    scan=src/'Чертёж.tif';page=Image.new('1',(2480,3508),1);ImageDraw.Draw(page).rectangle((100,100,2380,3408),outline=0,width=6)
    page.save(scan,compression='group4',dpi=(300,300))
    cmyk=src/'Печать.jpg';Image.new('CMYK',(100,60),(0,128,255,0)).save(cmyk,dpi=(300,300))
    deep=src/'Глубина.png';gray=Image.new('I;16',(100,60));gray.putdata([30000]*6000);gray.save(deep)
    bw=src/'Линии.png';page.save(bw,dpi=(300,300))
    huge=src/'Лист A0.tif';Image.new('1',(14000,13000),1).save(huge,compression='group4',dpi=(400,400))
    source_hash.update({p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (scan,cmyk,deep,bw,huge)})
    with Image.open(paths(run('convert',[scan],format='png'))[0]) as image:assert image.mode=='1' and round(image.info['dpi'][0])==300
    with Image.open(paths(run('convert',[scan],format='bmp'))[0]) as image:assert image.mode=='1' and round(image.info['dpi'][0])==300
    with Image.open(paths(run('convert',[bw],format='tif'))[0]) as image:assert image.mode=='1' and image.info['compression']=='group4'
    with Image.open(paths(run('convert',[cmyk],format='png'))[0]) as image:assert image.mode=='RGB' and round(image.info['dpi'][0])==300
    with Image.open(paths(run('convert',[deep],format='jpg'))[0]) as image:assert abs(image.getpixel((5,5))-116)<=2
    with Image.open(paths(run('resize',[scan],format='png',resize_mode='percent',percent=50))[0]) as image:assert round(image.info['dpi'][0])==150
    with Image.open(paths(run('convert',[huge],format='png'))[0]) as image:assert image.size==(14000,13000)
    scan_pdf=PdfReader(paths(run('to_pdf',[scan]))[0]);box=scan_pdf.pages[0].mediabox
    assert abs(float(box.width)-595.2)<1 and abs(float(box.height)-842)<1
    picture=list(scan_pdf.pages[0]['/Resources']['/XObject'].values())[0].get_object()
    assert picture['/BitsPerComponent']==1 and '/CCITTFaxDecode' in str(picture['/Filter']) and paths(run('to_pdf',[scan]))[0].stat().st_size<20000
    # Duplicate archive names preserve every file and Unicode.
    other=src/'other';other.mkdir();duplicate=other/a.name;duplicate.write_bytes(b'other file')
    zipped=run('zip',[a,duplicate,pdf1]);archive=paths(zipped)[0]
    assert archive.name == 'Скан.zip'
    with zipfile.ZipFile(archive) as z:
        assert len(z.namelist())==3 and z.read('Скан.png')==a.read_bytes() and z.read('Скан (2).png')==b'other file'
    assert 'размер' in zipped['summary'] and any(unit in zipped['summary'] for unit in ['байт','КБ','МБ'])
    assert zipped['source_bytes'] == sum(p.stat().st_size for p in [a,duplicate,pdf1])
    assert zipped['archive_bytes'] == archive.stat().st_size
    # Single-file names retain intermediate dots, collisions preserve old archives.
    text=src/'Ведомость.РНЕ.20.txt';text.write_text('Материал 40Х\n'*10000,encoding='utf-8')
    single=run('zip',[text]);single_path=paths(single)[0];saved_zip=single_path.read_bytes()
    assert single_path.name == 'Ведомость.РНЕ.20.zip'
    assert single['archive_bytes'] < single['source_bytes'] / 10 and 'Уменьшение:' in single['summary']
    with zipfile.ZipFile(single_path) as z:
        assert z.testzip() is None and z.read(text.name) == text.read_bytes()
        assert z.getinfo(text.name).compress_type == zipfile.ZIP_DEFLATED
    collision=run('zip',[text]);assert paths(collision)[0].name == 'Ведомость.РНЕ.20 (2).zip'
    assert single_path.read_bytes() == saved_zip
    # Already compressed data remains intact even when ZIP cannot reduce it further.
    nested=run('zip',[single_path]);assert nested['archive_bytes'] == paths(nested)[0].stat().st_size
    with zipfile.ZipFile(paths(nested)[0]) as z:assert z.read(single_path.name) == saved_zip
    empty=src/'Пустой.txt';empty.write_bytes(b'');empty_zip=run('zip',[empty])
    assert empty_zip['source_bytes'] == 0 and 'Размер не уменьшился' in empty_zip['summary']
    # Undo removes only matching output files, skips edited copies and sources.
    target=paths(converted)[0];target.write_bytes(b'user changed this result')
    undone=run('undo',entry_id=converted['id']);assert target.exists() and not outputs[1].exists()
    assert target.read_bytes()==b'user changed this result' and '1' in undone['summary']
    assert c.load_history(history)['entries']
    clean=run('undo',entry_id=again['id']);assert not any(p.exists() for p in paths(again))
    assert c.load_history(history)['entries']
    # Replacing a result with identical bytes must still be protected by identity.
    replacement=run('convert',[a],format='jpg');p=paths(replacement)[0];value=p.read_bytes();saved=p.with_suffix('.saved');p.rename(saved);p.write_bytes(value)
    run('undo',entry_id=replacement['id']);assert p.exists();saved.unlink()
    before=set(out.iterdir())
    for op,kwargs in [('extract',{'pages':'999'}),('rotate',{'rotation':45})]:
        try:run(op,[pdf1],**kwargs)
        except ValueError:pass
        else:raise AssertionError('Invalid PDF option accepted')
        assert set(out.iterdir())==before
    encrypted=src/'Защищён.pdf';writer=PdfWriter();writer.add_blank_page(width=100,height=100);writer.encrypt('secret');writer.write(encrypted)
    try:run('merge',[pdf1,encrypted])
    except ValueError as error:assert 'паролем' in str(error)
    else:raise AssertionError('Encrypted PDF accepted')
    bad=src/'Ошибка.png';bad.write_bytes(b'not image')
    before=set(out.iterdir())
    try:run('convert',[a,bad],format='jpg')
    except Exception:pass
    else:raise AssertionError('Bad image accepted')
    assert set(out.iterdir())==before
    cancel=root/'cancel';cancel.touch()
    try:run('convert',[a],format='jpg',cancel=str(cancel))
    except ValueError as error:assert 'отменена' in str(error)
    else:raise AssertionError('Cancellation ignored')
    assert set(out.iterdir())==before
    # PDF pages to images: CursorPadOcr.exe --pdf draws them; CI uses a stand-in with the same protocol.
    fake=root/'render.py';fake.write_text('\n'.join([
        'import sys','from pathlib import Path','from PIL import Image, ImageDraw','from pypdf import PdfReader',
        '_, flag, pdf, folder, dpi = sys.argv','assert flag == "--pdf"',
        'for i, page in enumerate(PdfReader(pdf).pages, 1):',
        '    w, h = round(float(page.mediabox.width) * int(dpi) / 72), round(float(page.mediabox.height) * int(dpi) / 72)',
        '    im = Image.new("RGBA", (w, h), "white"); d = ImageDraw.Draw(im); d.rectangle((5, 5, w - 5, h - 5), outline="black", width=3)',
        '    if i == 2: d.ellipse((20, 20, 60, 60), fill=(220, 30, 30))',
        '    im.save(Path(folder) / f"p{i:04}.png"); print(f"p{i:04}.png {dpi}.000")']),encoding='utf-8')
    renderer=[sys.executable,'-I','-B',str(fake)]
    sheets=src/'Листы.pdf';writer=PdfWriter();writer.add_blank_page(width=595.28,height=841.89);writer.add_blank_page(width=200,height=100);writer.write(sheets)
    sheet=src/'Лист.pdf';writer=PdfWriter();writer.add_blank_page(width=100,height=100);writer.write(sheet)
    source_hash.update({p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (sheets,sheet)})
    rendered=paths(run('convert',[sheets,sheet],format='png',pdf_renderer=renderer,pdf_dpi=150))
    assert sorted(p.name for p in rendered)==['Лист.png','Листы-стр-001.png','Листы-стр-002.png']
    pages={p.name:p for p in rendered}
    with Image.open(pages['Листы-стр-001.png']) as image:assert image.mode=='1' and image.size==(1240,1754) and round(image.info['dpi'][0])==150
    with Image.open(pages['Листы-стр-002.png']) as image:assert image.mode=='RGB'
    tiff=[p for p in paths(run('convert',[sheets],format='tif',pdf_renderer=renderer)) if p.name.endswith('001.tif')][0]
    with Image.open(tiff) as image:assert image.info['compression']=='group4' and round(image.info['dpi'][0])==300
    before=set(out.iterdir())
    for files,kwargs,word in [([encrypted],{'pdf_renderer':renderer},'паролем'),([sheet],{},'недоступен')]:
        try:run('convert',files,format='png',**kwargs)
        except ValueError as error:assert word in str(error)
        else:raise AssertionError('PDF to images accepted a bad case')
        assert set(out.iterdir())==before
    # Prepared Office files participate in the same commit and history entry.
    prepared=root/'office.pdf';prepared.write_bytes(pdf2.read_bytes())
    ready=run('to_pdf',[],prepared=[{'path':str(prepared),'name':'Office.pdf','original':str(src/'Office.docx')}])
    assert len(PdfReader(paths(ready)[0]).pages)==2
    # Failure to persist history rolls back only newly published copies.
    previous=c.save_history
    def fail(*args):raise OSError('history write failed')
    c.save_history=fail;before=set(out.iterdir())
    try:run('convert',[a],format='jpg')
    except OSError:pass
    else:raise AssertionError('History failure hidden')
    finally:c.save_history=previous
    assert set(out.iterdir())==before
    # A failure reading metadata/hash must happen BEFORE any public output.
    previous_stamp=c.stamp;before=set(out.iterdir())
    def bad_stamp(*args):raise OSError('simulated antivirus/read failure')
    c.stamp=bad_stamp
    try:run('convert',[a],format='jpg')
    except OSError:pass
    else:raise AssertionError('Stamp failure hidden')
    finally:c.stamp=previous_stamp
    assert set(out.iterdir())==before
    # If preparing the second output fails, the first is rolled back too.
    called=[]
    def second_stamp(path):
        called.append(path)
        if len(called)==2:raise OSError('second stamp failed')
        return previous_stamp(path)
    c.stamp=second_stamp;before=set(out.iterdir())
    try:run('convert',[a,b],format='png')
    except OSError:pass
    else:raise AssertionError('Second stamp failure hidden')
    finally:c.stamp=previous_stamp
    assert set(out.iterdir())==before
    # A same-byte replacement immediately after publication is NOT ours.
    previous_rename=c.os.rename;previous_link=c.os.link;substitutes=[]
    def substitute(target):
        target=Path(target);value=target.read_bytes()
        saved=root/('retained-original-'+str(len(substitutes))+target.suffix)
        previous_rename(target,saved);target.write_bytes(value);substitutes.append(target)
    def racing_rename(source,target):
        previous_rename(source,target)
        if Path(target).parent==out:substitute(target)
    def racing_link(source,target):
        previous_link(source,target)
        if Path(target).parent==out:substitute(target)
    c.os.rename=racing_rename;c.os.link=racing_link
    try:raced=run('convert',[a],format='jpg')
    finally:c.os.rename=previous_rename;c.os.link=previous_link
    assert len(substitutes)==1
    undo_race=run('undo',entry_id=raced['id']);assert substitutes[0].exists()
    assert entry(raced)['state']=='partial'
    # Rollback after a history failure also preserves a same-byte replacement.
    c.os.rename=racing_rename;c.os.link=racing_link;c.save_history=fail
    try:run('convert',[a],format='jpg')
    except OSError:pass
    else:raise AssertionError('History failure hidden during replacement')
    finally:c.os.rename=previous_rename;c.os.link=previous_link;c.save_history=previous
    assert len(substitutes)==2 and substitutes[-1].exists()
    # The documented batch limit is exercised using real files.
    capfiles=[]
    for index in range(c.MAX_FILES):
        path=src/f'cap-{index}.png';path.write_bytes(a.read_bytes());capfiles.append(path)
    maximum=run('convert',capfiles,format='png');assert len(paths(maximum))==c.MAX_FILES
    run('undo',entry_id=maximum['id']);assert all(not p.exists() for p in paths(maximum))
    before=set(out.iterdir())
    try:run('convert',capfiles+[a],format='png')
    except ValueError:pass
    else:raise AssertionError('129 files accepted')
    assert set(out.iterdir())==before
    for path,digest in source_hash.items():assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
    assert not list(out.glob('.CursorPad-*'))
    listing=run('history');assert listing['history_ids'] and listing['history_labels']
print('PASS: batches, real image formats/size/byte budget, scan dpi/1-bit/G4/CCITT PDF, CMYK, 16-bit, A0 size, PDF pages to 1-bit/colour images, multipage TIFF, PDF ordering/extract/split/rotate/packet, ZIP duplicates, persisted history, identity/hash undo, cancellation, metadata failure rollback, publication replacement guard, atomic failure rollback, unchanged sources')
