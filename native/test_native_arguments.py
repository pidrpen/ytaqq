"""Regress native argument/JSON schema mismatch before any filesystem action."""
import ast
import copy
import json
from pathlib import Path
import sys
import types
import jsonschema

root = Path(__file__).resolve().parent
sys.path.insert(0, str(root / 'agent'))
tree = ast.parse((root / 'agent/bridge.py').read_text(encoding='utf-8'))
names = {'local_call', 'zai_wire', 'strict_json', 'validate_action', 'NativeArgumentError'}
tree.body = [node for node in tree.body if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names]
b = types.ModuleType('transport')
exec('import copy,json,re,sys,time,urllib.request,urllib.error,jsonschema\nfrom network_diagnostics import connection_error,safe_text\nfrom windows_https import WindowsHTTPSHandler', b.__dict__)
b.CLOUD = True
b.ZAI_ENDPOINT = 'https://api.z.ai/api/paas/v4/chat/completions'
exec(compile(tree, 'bridge.py', 'exec'), b.__dict__)
header = (root / 'ai_agent_schema.h').read_text(encoding='utf-8')
wire = json.loads(header.split('=\n  ', 1)[1].strip().removesuffix(';'))
response_format = json.loads('{' + wire[1:] + '}')['response_format']
schema = response_format['json_schema']['schema']
goal = 'Создай Word на рабочем столе с аргументами за то, чтобы осваивать Солнце как пригодную для жизни планету'
body = {'model': 'glm-4.5-flash', 'messages': [{'role': 'user', 'content': goal}], 'response_format': response_format}
valid = {'tool': 'write', 'path': '@desktop\\Солнце.docx', 'text': 'Проверка создания документа'}
many = {'tool': 'write', **{name: '' for name in schema['anyOf'][1]['properties'] if name != 'tool'}}
many.update(valid)
assert len(many) == 12
for branch in schema['anyOf']:
    assert branch['maxProperties'] == 10
    for name, field in branch['properties'].items():
        if name != 'tool': assert field['maxLength'] == 1999

class Response:
    def __init__(self, value): self.raw = json.dumps({'choices': [{'message': {'content': json.dumps(value, ensure_ascii=False)}, 'finish_reason': 'stop'}]}).encode()
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def read(self, *args): return self.raw

class Transport:
    def __init__(self, replies): self.replies, self.calls = iter(replies), []
    def open(self, request, timeout):
        self.calls.append(json.loads(request.data))
        return Response(next(self.replies))

for bad, marker in [(many, 'полей 12'), ({**valid, 'text': 'я' * 2000}, 'длина 2000'),
                    ({**valid, 'text': '😀' * 1000}, 'длина 2000'), ({**valid, 'text': 'x\x00y'}, 'нулевой')]:
    original = copy.deepcopy(bad)
    mock = Transport([bad, valid]); b.urllib.request.build_opener = lambda *args: mock
    result = b.local_call(b.ZAI_ENDPOINT, 'fake-key', body, 2)
    assert json.loads(result) == valid and len(mock.calls) == 2
    correction = mock.calls[1]['messages'][-1]['content']
    assert marker in correction and 'Never truncate' in correction
    assert mock.calls[1]['messages'][1]['content'] == goal
    assert bad == original and body['messages'] == [{'role': 'user', 'content': goal}]

for text in ['я' * 1999, '😀' * 999 + 'я']:
    assert b.validate_action({**valid, 'text': text}, schema)['text'] == text

bad = {**valid, 'text': 'я' * 2000}
mock = Transport([bad, bad]); b.urllib.request.build_opener = lambda *args: mock
try:
    b.local_call(b.ZAI_ENDPOINT, 'fake-key', body, 2)
except ValueError as error:
    assert 'после одной попытки' in str(error) and 'длина 2000' in str(error)
    assert 'я' * 100 not in str(error)
else:
    raise AssertionError('Oversized tool argument accepted')
assert len(mock.calls) == 2
print('PASS: native/schema limits, excess fields, text length, UTF-16, NUL, one repair, no truncation, unchanged goal')
