"""Boundary tests under the bundled, isolated Python (no model needed)."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sys
import tempfile
import threading
import urllib.error


def main():
    p = argparse.ArgumentParser(); p.add_argument('--runtime', type=Path, required=True)
    a = p.parse_args()
    runtime = a.runtime.resolve(); sys.path.insert(0, str(runtime))
    import bridge
    # No dependency is allowed to contact a cloud endpoint during these tests.
    def network_guard(event, args):
        if event == 'socket.connect' and args[1][0] not in ('127.0.0.1', '::1'):
            raise AssertionError('Unexpected external network access')
    sys.addaudithook(network_guard)
    probe = Path(__file__).with_name('make_agent_schema.py')
    # Read production JSON grammar from the generated header without running it.
    header = probe.with_name('ai_agent_schema.h').read_text(encoding='utf-8')
    wire = json.loads(header.split('=\n  ', 1)[1].strip().removesuffix(';'))
    schema = json.loads('{' + wire[1:] + '}')
    queue, received = [], []
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_POST(self):
            received.append(json.loads(self.rfile.read(int(self.headers['Content-Length']))))
            answer = queue.pop(0)
            if answer == 'REDIRECT':
                self.send_response(302); self.send_header('Location', 'https://example.com/'); self.end_headers(); return
            text = answer if isinstance(answer, str) else json.dumps(answer, ensure_ascii=False)
            data = json.dumps({'choices': [{'message': {'content': text}}]}).encode()
            self.send_response(200); self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    endpoint = f'http://127.0.0.1:{server.server_port}/v1/chat/completions'
    request = {'model': 'local', **schema, 'cursorpad': {'goal': 'Создай Word', 'plan': ''},
               'messages': [{'role': 'system', 'content': 'Use one native tool.'},
                            {'role': 'user', 'content': 'Папки: @desktop=C:\\test\nШаг 1'}], 'max_tokens': 512}
    results = []
    def expect_error(name, action):
        try: action()
        except Exception: results.append({'case': name, 'pass': True})
        else: raise AssertionError(name + ' was accepted')
    try:
        with tempfile.TemporaryDirectory(prefix='cursorpad-qwen-contract-') as directory:
            root = Path(directory)
            def choose():
                return bridge.choose(json.loads(json.dumps(request)), endpoint, '', 5, root/'state.json')
            queue[:] = [{'requirements': ['Создать Word']}, {'tool': 'write', 'path': '@desktop\\Отчёт.docx', 'text': 'Привет'}]
            action = choose(); assert action['text'] == 'Привет'
            assert not (root/'Отчёт.docx').exists(), 'adapter must not edit user files'
            assert not queue and 'documents' in json.loads((root/'state.json').read_text(encoding='utf-8'))['skills']
            results.append({'case': 'registered Qwen tool, real skills, no filesystem mutation', 'pass': True})
            saved = json.loads((root/'state.json').read_text(encoding='utf-8'))
            assert saved['backend'] == 'QwenPaw' and saved['pending']['name'] == 'write' and saved['agent_state']
            request['cursorpad']['observation'] = 'Word-документ добавлен в план. Диск не изменён.'
            request['cursorpad']['observation_state'] = 0
            queue[:] = [{'tool': 'done'}, {'covered': False, 'missing': ['Создать документ']},
                        {'tool': 'write', 'path': '@desktop\\Другой.docx', 'text': 'Текст'}]
            assert choose()['tool'] == 'write' and not queue
            results.append({'case': 'incomplete plan rejected and corrected; requirements persisted', 'pass': True})
            queue[:] = [{'tool': 'excel_write', 'path': '@desktop\\A.xlsx', 'text': ''}]
            expect_error('empty filled workbook rejected', choose)
            queue[:] = [{'tool': 'shell', 'text': 'whoami'}]
            expect_error('unregistered command rejected', choose)
            queue[:] = ['{"tool":"write","tool":"done"}']
            expect_error('duplicate tool fields rejected', choose)
            queue[:] = [{'tool': 'write', 'path': '@desktop\\A.txt', 'text': 'x'*2000}]
            expect_error('native argument length enforced', choose)
            before = len(received)
            expect_error('remote model endpoint rejected', lambda: bridge.local_call('https://example.com/v1/chat/completions', '', {}, 2))
            assert len(received) == before
            queue[:] = ['REDIRECT']
            expect_error('model redirect rejected', lambda: bridge.local_call(endpoint, '', {}, 2))
            assert all('cursorpad' not in body and body.get('stream') is False for body in received)
            results.append({'case': 'model wire excludes host metadata and cloud access', 'pass': True})
    finally:
        server.shutdown(); server.server_close()
    print(json.dumps({'pass': True, 'backend': 'QwenPaw', 'python': sys.version.split()[0], 'checks': results}, ensure_ascii=False, indent=2))


if __name__ == '__main__': main()
