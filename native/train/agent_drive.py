"""Шаги агента CursorPad тем же путём, что в программе (адаптер инструментов, tools_data.py,
eval_tools.py): подсказка и схема из agent_probe (kAgSystem, kAiAgentSchema), сообщение шага —
как ag_request в ai_agent.c, затем то, что из него делают bridge.py и QwenPaw.

paw_messages собирает итоговые сообщения модели без запуска bridge; совпадение с настоящим
bridge.py проверяет run() с «суфлёром» (Model без upstream): он отвечает заданным действием
и записывает ровно то, что увидела бы модель. С upstream запрос уходит в llama-server.

Инструменты app (Союз, справочник, резка, rename_by_stamp, file_read) в agent_probe не
исполняются: их наблюдения задаёт сценарий (выдуманные данные).

CP_PAW — папка комплекта QwenPaw (python.exe, bridge.py, skills), по умолчанию самая новая в
%LOCALAPPDATA%\\CursorPad\\agent; CP_PROBE — agent_probe.exe (сборка — в tools_data.py)."""
import json
import os
import subprocess
import tempfile
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def _runtime():
    if os.environ.get('CP_PAW'):
        return Path(os.environ['CP_PAW'])
    base = Path(os.environ.get('LOCALAPPDATA', '')) / 'CursorPad' / 'agent'
    packs = [p for p in base.glob('*') if (p / 'bridge.py').exists()] if base.exists() else []
    return max(packs, key=lambda p: (p / 'bridge.py').stat().st_mtime) if packs else base


RUNTIME = _runtime()
HERE = Path(__file__).resolve().parent
PROBE = Path(os.environ.get('CP_PROBE', HERE / 'agent_probe.exe'))
KINDS = ['mkdir', 'copy', 'move', 'rename', 'open', 'write', 'write', 'write', 'excel_write', 'inventory',
         'move каталог', 'word_format']
STAMP_REN = ('переимен', 'назови', 'назва', 'имена файл', 'имя файл')
STAMP_SRC = ('штамп', 'обознач', 'по чертеж', 'с чертеж')


def probe_text(arg):
    return json.loads(subprocess.check_output([str(PROBE), arg], encoding='utf-8'))


def skills_for(goal):  # bridge.skills_for
    names = ['task-completion', 'files']
    g = goal.lower()
    if any(w in g for w in ('excel', 'эксел', 'ексел', 'таблиц', 'формул', 'xlsx', 'материал')):
        names.append('spreadsheets')
    if any(w in g for w in ('word', 'ворд', 'docx', 'документ')):
        names.append('documents')
    return '\n'.join((RUNTIME / 'skills' / n / 'SKILL.md').read_text(encoding='utf-8') for n in names)


def paw_messages(system, goal, requirements, history, current):
    """Сообщения модели так, как их собирает bridge.py + QwenPaw (проверено снимками суфлёра):
    system = kAgSystem + навыки + требования; затем просьба; затем по шагу на сообщение
    ассистента «вызов\\nРезультат инструмент: наблюдение» (не больше ~5000 символов, свежие);
    последним — сообщение шага от программы."""
    sysm = system + '\n' + skills_for(goal) + '\nОбязательные требования задачи:\n' + \
        '\n'.join(f'{i + 1}. {t}' for i, t in enumerate(requirements)) + '\n'
    hist = [{'role': 'user', 'content': goal}]
    if history:  # QwenPaw держит все вызовы и результаты одним сообщением ассистента
        parts = []
        for action, obs in history:
            parts += [json.dumps(action, ensure_ascii=False), f'Результат {action["tool"]}: {obs}']
        hist.append({'role': 'assistant', 'content': '\n'.join(parts)})
    room = max(0, min(5000, 29000 - len(sysm) - len(current) - 1000))
    kept = []
    for m in reversed(hist):
        if len(m['content']) > room:
            break
        kept.insert(0, m)
        room -= len(m['content'])
    return [{'role': 'system', 'content': sysm}] + kept + [{'role': 'user', 'content': current}]


def stamp_task(goal):  # ag_stamp_task
    g = goal.lower()
    return any(k in g for k in STAMP_REN) and any(k in g for k in STAMP_SRC)


def stamp_base(goal):  # ag_stamp_base: полный путь из задачи (в JSON — с \\), иначе по словам
    for i, ch in enumerate(goal):
        if ch.isascii() and ch.isalpha() and goal[i + 1:i + 3] == ':\\':
            j = i
            while j < len(goal) and goal[j] not in ' "»\r\n':
                j += 1
            return goal[i:j].replace('\\', '\\\\')
    g = goal.lower()
    return '@downloads' if 'загрузк' in g else '@documents' if 'документ' in g else '@desktop'


def step_prompt(goal, fs, step, plan, notes):
    """Сообщение шага ровно как ag_request (ai_agent.c), без контекста беседы и @document."""
    p = (f'Задача пользователя: {goal}\nПапки: @desktop={fs["desktop"]}; @downloads={fs["downloads"]}; '
         f'@documents={fs["documents"]}; @last={fs["last"]}; @new={fs["new"]}.\n'
         f'Шаг {step} из 16. Действий в плане: {len(plan)}.\n')
    if not plan and stamp_task(goal):
        base = stamp_base(goal)
        tail = ' (если в задаче названа подпапка или файл — допиши их: @desktop\\\\Имя)' if base.startswith('@') else ''
        p += ('Подсказка: это переименование чертежей по штампу — первый вызов {"tool":"app","name":"rename_by_stamp",'
              f'"text":"{base}"}}. В text — папка или файл из задачи{tail}; не rename, не list, не file_read. '
              'После его результата — done.\n')
    shown = 0
    for a in plan[:12]:
        p += f'В плане: {KINDS[a["kind"]]} {a["src"]} -> {a["dst"]}\n'
        shown += 1
    if len(plan) > shown:
        p += 'Остальные действия скрыты для экономии контекста; ориентируйся на результаты инструментов.\n'
    return p + notes[-24000:]


class Model:
    """Сервер на 127.0.0.1 для bridge.py: суфлёр (script) или пересылка к llama-server (upstream)."""

    def __init__(self, upstream=None, lora=None):
        self.upstream, self.lora = upstream, lora
        self.next_action, self.log = None, []
        outer = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                name = body.get('response_format', {}).get('json_schema', {}).get('name', '')
                schema = body.get('response_format', {}).get('json_schema', {}).get('schema', {})
                kind = 'action' if name == 'cursorpad_files' else 'audit' if 'checks' in schema.get('properties', {}) \
                    else 'checklist' if 'requirements' in schema.get('properties', {}) else 'other'
                if outer.upstream:
                    if outer.lora is not None:
                        body['lora'] = [{'id': 0, 'scale': outer.lora}]
                    t = time.time()
                    req = urllib.request.Request(outer.upstream, json.dumps(body, ensure_ascii=False).encode(),
                                                 {'Content-Type': 'application/json'})
                    raw = urllib.request.urlopen(req, timeout=600).read()
                    content = json.loads(raw)['choices'][0]['message']['content']
                    outer.log.append({'kind': kind, 'messages': body['messages'], 'answer': content,
                                      'secs': round(time.time() - t, 2)})
                else:
                    if kind == 'action':
                        content = json.dumps(outer.next_action, ensure_ascii=False, separators=(',', ':'))
                        outer.log.append({'kind': kind, 'messages': body['messages'], 'answer': content})
                    elif kind == 'audit':
                        keys = list(schema['properties']['checks']['properties'])
                        n = max(1, outer.plan_count)
                        content = json.dumps({'checks': {k: {'status': 'planned' if outer.plan_count else 'answered',
                                                             'actions': list(range(1, n + 1)) if outer.plan_count else [],
                                                             'reason': 'ок'} for k in keys}}, ensure_ascii=False)
                    else:
                        content = json.dumps({'requirements': [outer.goal]}, ensure_ascii=False)
                data = json.dumps({'choices': [{'message': {'role': 'assistant', 'content': content},
                                                'finish_reason': 'stop'}]}, ensure_ascii=False).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), H)
        self.port = self.server.server_address[1]
        self.goal, self.plan_count = '', 0
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()


class Probe:
    def __init__(self, root):
        self.p = subprocess.Popen([str(PROBE), str(root)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, encoding='utf-8')

    def call(self, action):
        self.p.stdin.write((action if isinstance(action, str) else json.dumps(action, ensure_ascii=False)) + '\n')
        self.p.stdin.flush()
        return json.loads(self.p.stdout.readline())

    def close(self):
        self.p.stdin.close()
        self.p.wait(10)


def run(task, model, work, system, schema, max_steps=8):
    """Один сценарий. task: goal, files {путь: текст}, dirs [..], steps [{action, observation?}] (суфлёр)
    или без steps (настоящая модель). Возвращает шаги: сообщения модели, ответ, наблюдение."""
    root = Path(tempfile.mkdtemp(prefix='d_', dir=work))
    for d in task.get('dirs', []):
        (root / d).mkdir(parents=True, exist_ok=True)
    for rel, data in task.get('files', {}).items():
        f = root / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)
    (root / 'Downloads').mkdir(exist_ok=True)
    (root / 'Documents').mkdir(exist_ok=True)
    goal = task['goal'].replace('{desktop}', str(root))
    probe = Probe(root)
    first = probe.call({'goal': goal})
    fs = {'desktop': str(root), 'downloads': str(root / 'Downloads'), 'documents': str(root / 'Documents'),
          'last': str(root), 'new': ''}
    plan, notes, obs, state = [], '', '', 0
    meta_goal, evidence = first['model_goal'], first['evidence']
    state_file = Path(work) / (root.name + '.state.json')
    out = []
    model.goal = goal
    steps = task.get('steps')
    for n in range(1, max_steps + 1):
        if steps is not None and n > len(steps):
            break
        prompt = step_prompt(goal, fs, n, plan, notes)
        req = {'cursorpad': {'goal': meta_goal, 'plan': evidence, 'plan_count': len(plan), 'observation': obs,
                             'observation_state': state},
               'model': 'local', 'messages': [{'role': 'system', 'content': system}, {'role': 'user', 'content': prompt}],
               'temperature': 0.2, 'max_tokens': 1024, 'stream': True, 'cache_prompt': True,
               'chat_template_kwargs': {'enable_thinking': False}, **schema}
        if steps is not None:
            model.next_action = steps[n - 1]['action']
        model.plan_count = len(plan)
        before = len(model.log)
        rq, rs = Path(work) / 'request.json', Path(work) / 'response.json'
        rq.write_text(json.dumps(req, ensure_ascii=False), encoding='utf-8')
        subprocess.run([str(RUNTIME / 'python.exe'), '-I', '-B', str(RUNTIME / 'bridge.py'), '--request', str(rq),
                        '--response', str(rs), '--state', str(state_file),
                        '--endpoint', f'http://127.0.0.1:{model.port}/v1/chat/completions'], check=True, timeout=900)
        reply = json.loads(rs.read_text(encoding='utf-8'))
        calls = [e for e in model.log[before:] if e['kind'] == 'action']
        if not reply.get('ok'):
            out.append({'n': n, 'error': reply.get('error', ''), 'calls': calls})
            break
        action = json.loads(reply['action'])
        raw = reply['action']
        if action.get('tool') == 'app':
            given = steps[n - 1] if steps is not None else task.get('app', {}).get(action.get('name', ''), {})
            obs = given.get('observation', 'Инструмент на стенде не исполняется.')
            state = given.get('state', 0)
        else:
            res = probe.call(action)
            obs, state = res['observation'], res['state']
            plan, evidence, meta_goal = res['plan'], res['evidence'], res['model_goal']
            fs['last'], fs['new'] = res['last'] or fs['last'], res['new_dir']
        notes += f'\nВызов: {raw}\nРезультат: {obs}\n'
        out.append({'n': n, 'action': action, 'raw': raw, 'observation': obs, 'state': state, 'calls': calls})
        if state in (1, 2):
            break
    probe.close()
    return {'goal': goal, 'root': str(root), 'steps': out}
