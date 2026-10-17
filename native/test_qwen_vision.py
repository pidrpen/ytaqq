"""Real GGUF vision through CursorPad's managed tier and chat request builder.

Uses isolated profiles and model hard links on the same volume. Does not alter
user settings, start the GUI, or download packages. Pillow is test-only.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time

from PIL import Image, ImageDraw, ImageFont


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--profile', type=Path, required=True)
    ap.add_argument('--probe', type=Path, required=True)
    ap.add_argument('--scratch', type=Path, required=True)
    args = ap.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=False)
    src = args.profile / 'ai/models/qwen3.5-4b'
    isolated = args.scratch / 'profile'
    modeldir = isolated / 'ai/models/qwen3.5-4b'
    modeldir.mkdir(parents=True)
    names = ['Qwen3.5-4B-Q4_K_M.gguf', 'mmproj-F16.gguf']
    for name in names:
        (modeldir / name).hardlink_to(src / name)
        shutil.copyfile(src / (name + '.ok'), modeldir / (name + '.ok'))
    assert (modeldir / names[1]).stat().st_size == 672423616
    font = ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf', 56)
    fixtures = [
        ('first', 'red', 'blue', 'РНЕ.20.02', ('красн', 'круг', 'син', 'квадрат')),
        ('second', 'green', 'orange', 'АБВ.73.15', ('зелен', 'треугол', 'оранж', 'круг')),
    ]
    results = []
    for label, left, right, designation, words in fixtures:
        image = Image.new('RGB', (768, 384), 'white')
        draw = ImageDraw.Draw(image)
        if label == 'first':
            draw.ellipse((70, 80, 250, 260), fill=left)
            draw.rectangle((470, 80, 650, 260), fill=right)
        else:
            draw.polygon([(160, 60), (60, 260), (260, 260)], fill=left)
            draw.ellipse((470, 80, 650, 260), fill=right)
        draw.text((180, 295), designation, fill='black', font=font)
        png = args.scratch / (label + '.png')
        image.save(png)
        question = args.scratch / (label + '.txt')
        question.write_text('Назови цвет и форму слева, цвет и форму справа. '
                            'Перепиши обозначение внизу. Кратко.', encoding='utf-8')
        output = args.scratch / (label + '.json')
        started = time.monotonic()
        p = subprocess.run([str(args.probe.resolve()), str(isolated.resolve()),
                            str(question.resolve()), str(output.resolve()), '-4',
                            str(png.resolve()), str(args.profile / 'ai/llama-vk/llama-server.exe')],
                           timeout=210)
        result = json.loads(output.read_text(encoding='utf-8'))
        text = result['text'].lower().replace('ё', 'е')
        shape_pass = result['status'] == 0 and all(word in text for word in words)
        result.update(case=label, seconds=round(time.monotonic()-started, 2),
                      shape_pass=shape_pass, designation_expected=designation,
                      designation_exact=designation in result['text'], exit_code=p.returncode)
        results.append(result)
        shutil.copyfile(isolated / 'ai/server.log', args.scratch / (label + '-server.log'))
        assert p.returncode == 0 and shape_pass, result
        print(json.dumps(result, ensure_ascii=True), flush=True)
    assert results[0]['text'] != results[1]['text'], 'image response reused'
    # A partially upgraded tier must not silently pretend it accepts images.
    marker = modeldir / 'mmproj-F16.gguf.ok'
    marker.rename(marker.with_suffix('.missing'))
    output = args.scratch / 'missing-projector.json'
    p = subprocess.run([str(args.probe.resolve()), str(isolated.resolve()),
                        str(question.resolve()), str(output.resolve()), '-4',
                        str(png.resolve()), str(args.profile / 'ai/llama-vk/llama-server.exe')],
                       timeout=15)
    missing = json.loads(output.read_text(encoding='utf-8'))
    assert p.returncode == 1 and missing['status'] != 0 and 'картинками' in missing['text'], missing
    report = {'pass': True, 'model': names[0], 'projector': names[1],
              'projector_sha256': (src / (names[1]+'.ok')).read_text(),
              'cases': results, 'missing_projector_rejected': missing,
              'scope': 'synthetic shapes and text at the chat attachment resolution; '
                       'drawing dimensions and working 4GB GPU not validated'}
    (args.scratch / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS: two distinct images via native chat + managed Qwen3.5 tier; missing projector rejected')


if __name__ == '__main__':
    main()
