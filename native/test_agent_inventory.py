"""Regression for a NEW folder + moving spreadsheets + populated inventory.

All operations use the production C tools in a fresh scratch directory.
Binary spreadsheet fixtures need no Office because their contents are not read.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
import zipfile
import openpyxl

GOAL = 'на рабочем столе создай папку и закинь туда ексели, и создай в нем ексель с перечислением того что в папке'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--port', type=int, default=39285)
    args = parser.parse_args()
    probe = str(args.probe.resolve())
    args.scratch.mkdir(exist_ok=False, parents=True)
    system = json.loads(subprocess.check_output([probe, '--system'], encoding='utf-8'))
    schema = json.loads(subprocess.check_output([probe, '--schema'], encoding='utf-8'))
    report = []
    for count in (3, 140):
        root = (args.scratch / f'files-{count}').resolve()
        root.mkdir()
        (root / 'Модели').mkdir()
        sentinel = root / 'Модели/не трогать.xlsx'
        sentinel.write_bytes(b'Old Models folder must not be reused')
        sentinel_hash = hashlib.sha256(sentinel.read_bytes()).hexdigest()
        for name in ('Новая папка', 'Новая папка (2)'):
            (root / name).mkdir()
        files = {}
        for i in range(count):
            extension = ['.xlsx', '.xlsm', '.xls', '.xlsb'][i % 4]
            name = f'Ведомость_{i:03d}_АДЕ.830 — реальные исходные данные{extension}'
            data = f'opaque spreadsheet fixture {i}'.encode()
            (root / name).write_bytes(data)
            files[name] = data
        (root / 'обманка.xlsx.txt').write_text('leave', encoding='utf-8')
        (root / 'leave.pdf').write_bytes(b'leave')
        process = subprocess.Popen([probe, str(root)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   text=True, encoding='utf-8')
        def tool(action):
            process.stdin.write((action if isinstance(action, str) else json.dumps(action, ensure_ascii=False)) + '\n')
            process.stdin.flush()
            return json.loads(process.stdout.readline())
        tool({'goal': GOAL})
        notes, plan, log, new_dir = '', [], [], ''
        try:
            for step in range(16):
                prompt = (f'Задача пользователя: {GOAL}\nПапки: @desktop={root}; @downloads={root / "Downloads"}; '
                          f'@documents={root / "Documents"}; @last={root / "Модели"}; @new={new_dir}.\n'
                          f'Шаг {step + 1} из 16. Действий в плане: {len(plan)}.\n')
                kinds = ['mkdir', 'copy', 'move', 'rename', 'open', 'write', 'write', 'write', 'excel_write', 'inventory']
                for a in plan[:12]:
                    prompt += f'В плане: {kinds[a["kind"]]} {a["src"]} -> {a["dst"]}\n'
                prompt += notes[-10000:]
                body = json.dumps({'model': 'local', **schema, 'messages': [
                    {'role': 'system', 'content': system}, {'role': 'user', 'content': prompt}],
                    'temperature': 0.2, 'max_tokens': 512, 'stream': False}).encode()
                req = urllib.request.Request(f'http://127.0.0.1:{args.port}/v1/chat/completions', data=body,
                                             headers={'Content-Type': 'application/json'})
                with urllib.request.urlopen(req, timeout=120) as r:
                    answer = json.load(r)['choices'][0]['message']['content']
                action = json.loads(answer)
                result = tool(action)
                plan, new_dir = result['plan'], result['new_dir']
                log.append({'model': action, 'result': result})
                notes += f'\nВызов: {answer}\nРезультат: {result["observation"]}\n'
                print(f'{count} files, step {step+1}: {answer} -> {result["state"]}', flush=True)
                if result['state'] in (1, 2):
                    break
            assert result['state'] == 1, 'model did not finish a reviewable plan'
            assert Path(new_dir) == root / 'Новая папка (3)', 'existing folder was reused'
            assert not Path(new_dir).exists(), 'preview modified disk'
            assert all((root / name).read_bytes() == data for name, data in files.items())
            assert len([a for a in plan if a['kind'] == 9]) == 1
            assert len([a for a in plan if a['kind'] == 2]) == count
            assert plan[-1]['kind'] == 9, 'report must run after transfers'
            applied = tool(':apply')
            assert applied['state'] == 1, applied['observation']
            report_path = Path(plan[-1]['dst'])
            wb = openpyxl.load_workbook(report_path, data_only=False)
            rows = list(wb.active.values)
            assert rows[1][:3] == ('Имя', 'Тип', 'Расширение')
            assert len(rows) == count + 2, 'inventory is missing or has extra rows'
            actual = {row[0]: row for row in rows[2:]}
            assert set(actual) == set(files), 'inventory filenames differ from folder contents'
            for name, data in files.items():
                assert (Path(new_dir) / name).read_bytes() == data and not (root / name).exists()
                assert actual[name][3] == str(len(data))
                assert actual[name][4] == str(Path(new_dir) / name)
            assert all(c.data_type != 'f' for row in wb.active for c in row)
            with zipfile.ZipFile(report_path) as z:
                assert z.testzip() is None
            assert hashlib.sha256(sentinel.read_bytes()).hexdigest() == sentinel_hash
            assert (root / 'обманка.xlsx.txt').read_text(encoding='utf-8') == 'leave'
            report.append({'files': count, 'pass': True, 'inventory': str(report_path), 'log': log})
        except Exception as error:
            report.append({'files': count, 'pass': False, 'error': str(error), 'log': log})
        finally:
            process.stdin.close()
            process.wait(timeout=10)
    (args.scratch / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    assert all(x['pass'] for x in report), [(x['files'], x.get('error')) for x in report]
    print('PASS: exact user request with 3 and 140 spreadsheets; new folder, full inventory, source integrity.')

if __name__ == '__main__':
    main()
