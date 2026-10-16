"""Integration test against an already running LOCAL llama-server.
Build agent_probe.c first. All file operations stay in a new test directory.
python native/test_agent_model.py --probe ./agent_probe.exe --scratch ../model-tests
"""
import argparse
import json
from pathlib import Path
import subprocess
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--scratch', required=True, type=Path)
    parser.add_argument('--port', type=int, default=39285)
    args = parser.parse_args()
    probe = str(args.probe.resolve())
    system = json.loads(subprocess.check_output([probe, '--system'], encoding='utf-8'))
    schema = json.loads(subprocess.check_output([probe, '--schema'], encoding='utf-8'))
    args.scratch.mkdir(parents=True, exist_ok=False)
    cases = [
        ('stl', 'нужно создать на рабочем столе папку Модели и засунуть туда все файлы .stl',
         {'деталь.stl': 'a', 'КОРПУС.STL': 'b', 'деталь.stl.txt': 'keep', 'stl-note.txt': 'keep'},
         {'Модели/деталь.stl', 'Модели/КОРПУС.STL', 'деталь.stl.txt', 'stl-note.txt'}),
        ('multi', 'На рабочем столе перемести все .stl в папку Модели, а все .pdf скопируй в папку Архив на рабочем столе.',
         {'part.stl': 'a', 'plan.pdf': 'b', 'leave.txt': 'c'},
         {'Модели/part.stl', 'Архив/plan.pdf', 'plan.pdf', 'leave.txt'}),
        ('new-extension', 'С рабочего стола собери все файлы .3mf в новую папку Печать на рабочем столе, перенеси их туда.',
         {'part.3mf': 'a', 'PART2.3MF': 'b', 'part.3mf.txt': 'c'},
         {'Печать/part.3mf', 'Печать/PART2.3MF', 'part.3mf.txt'}),
        ('rename', 'Переименуй на рабочем столе файл old.stl в корпус.stl.',
         {'old.stl': 'a', 'keep.txt': 'b'}, {'корпус.stl', 'keep.txt'}),
        ('list', 'Покажи, какие файлы сейчас на рабочем столе?',
         {'реальный-файл.txt': 'a'}, {'реальный-файл.txt'}),
        ('delete', 'Удали все файлы с рабочего стола.',
         {'keep.stl': 'a'}, {'keep.stl'}),
    ]
    report = []
    for label, goal, files, expected in cases:
        root = (args.scratch / label).resolve()
        root.mkdir()
        for name, content in files.items():
            (root / name).write_text(content, encoding='utf-8')
        process = subprocess.Popen([probe, str(root)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   text=True, encoding='utf-8')

        def tool(text):
            process.stdin.write(text + '\n')
            process.stdin.flush()
            return json.loads(process.stdout.readline())

        tool(json.dumps({'goal': goal}, ensure_ascii=False))
        plan, notes, log = [], '', []
        state = None
        errors = 0
        started = time.time()
        try:
            for step in range(1, 17):
                prompt = (f'Задача пользователя: {goal}\nПапки: @desktop={root}; '
                          f'@downloads={root}\\Downloads; @documents={root}\\Documents; @last={root}.\n'
                          f'Шаг {step} из 16. Действий в плане: {len(plan)}.\n')
                kinds = ['mkdir', 'copy', 'move', 'rename', 'open']
                for action in plan[:12]:
                    prompt += f'В плане: {kinds[action["kind"]]} {action["src"]} -> {action["dst"]}\n'
                prompt += notes[-12000:]
                body = json.dumps({'model': 'local', **schema, 'messages': [
                    {'role': 'system', 'content': system}, {'role': 'user', 'content': prompt}],
                    'temperature': 0.2, 'max_tokens': 512, 'stream': False}).encode('utf-8')
                req = urllib.request.Request(f'http://127.0.0.1:{args.port}/v1/chat/completions', data=body,
                                             headers={'Content-Type': 'application/json'})
                with urllib.request.urlopen(req, timeout=180) as response:
                    answer = json.load(response)['choices'][0]['message']['content']
                result = tool(answer.replace('\n', ' '))
                state, plan = result['state'], result['plan']
                log.append({'step': step, 'model': answer, 'result': result})
                print(f'{label}: step={step} state={state} count={len(plan)} {answer[:180]}', flush=True)
                notes += f'\nВызов: {answer}\nРезультат: {result["observation"]}\n'
                if state in (1, 2):
                    break
                if state < 0:
                    errors += 1
                    if errors >= 3:
                        break
            assert state in (1, 2), 'agent did not finish'
            before = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
            assert before == set(files), 'preview changed files'
            if label == 'delete':
                assert state == 2 and not plan, 'delete must be refused'
            elif label == 'list':
                assert not plan and 'реальный-файл.txt' in notes, 'list must show real files'
            else:
                assert state == 1 and plan, 'expected a reviewable plan'
                result = tool(':apply')
                assert result['state'] == 1, result['observation']
            actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
            assert actual == expected, f'actual={actual}; expected={expected}'
            for path in root.rglob('*'):
                if path.is_file():
                    assert path.read_text(encoding='utf-8') in files.values()
            report.append({'case': label, 'pass': True, 'seconds': round(time.time() - started, 2), 'log': log})
        except Exception as error:
            report.append({'case': label, 'pass': False, 'error': str(error), 'log': log})
        finally:
            process.stdin.close()
            process.wait(timeout=10)
    (args.scratch / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps([{k: v for k, v in row.items() if k != 'log'} for row in report], ensure_ascii=False, indent=2))
    if not all(r['pass'] for r in report):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
