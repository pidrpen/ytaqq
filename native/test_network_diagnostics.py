"""Offline transport failures through the production bridge, with fake credentials."""
import importlib.util
import io
import json
from pathlib import Path
import socket
import ssl
import sys
import urllib.error

bridge = Path(sys.argv[1]).resolve()
if '--transport-only' in sys.argv:
    # Exercise the exact production transport functions without Windows-only
    # agent dependencies; the default mode imports the complete bundled bridge.
    import ast
    import types
    sys.path.insert(0, str(bridge.parent))
    tree = ast.parse(bridge.read_text(encoding='utf-8'))
    names = {'local_call', 'zai_wire', 'strict_json'}
    tree.body = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    b = types.ModuleType('transport')
    exec('import copy, json, re, time, urllib.request, urllib.error\nfrom network_diagnostics import connection_error, safe_text', b.__dict__)
    b.ZAI_ENDPOINT = 'https://api.z.ai/api/paas/v4/chat/completions'
    exec(compile(tree, str(bridge), 'exec'), b.__dict__)
else:
    spec = importlib.util.spec_from_file_location('bridge', bridge)
    b = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(b)
b.CLOUD = True
key = 'diagnostics-secret-must-not-leak'
body = {'model': 'glm-4.5-flash', 'messages': [{'role': 'user', 'content': 'private prompt'}]}

class Transport:
    def __init__(self, error): self.error = error
    def open(self, *args, **kwargs): raise self.error

cert = ssl.SSLCertVerificationError(1, 'certificate verify failed')
cert.verify_code = 89
cert.verify_message = 'Basic Constraints of CA cert not marked critical'
cases = [
    (urllib.error.URLError(cert), ['этап=TLS', 'verify_code=89', 'Basic Constraints', 'Python', 'OpenSSL']),
    (urllib.error.URLError(socket.gaierror(11001, 'name not resolved')), ['этап=DNS', '11001']),
    (TimeoutError('timed out'), ['время ожидания']),
    (urllib.error.URLError(ConnectionRefusedError(111, 'refused '+key)), ['этап=соединение/чтение', '111']),
    (urllib.error.HTTPError(b.ZAI_ENDPOINT, 429, 'limit', {}, io.BytesIO(b'{"error":{"code":"1305"}}')), ['HTTP 429', '1305', 'этап=HTTP']),
    (urllib.error.HTTPError(b.ZAI_ENDPOINT, 401, 'bad key', {}, io.BytesIO(b'not JSON')), ['HTTP 401', 'неверный API-ключ']),
]
for error, expected in cases:
    b.urllib.request.build_opener = lambda *args, value=error: Transport(value)
    try:
        b.local_call(b.ZAI_ENDPOINT, key, body, 1)
    except ValueError as failure:
        text = str(failure)
        assert all(part in text for part in expected), text
        assert key not in text and 'private prompt' not in text, text
    else:
        raise AssertionError('Transport failure was swallowed')

class Response:
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def read(self, *args): return b'{"unrelated":"private prompt"}'

class InvalidResponse:
    def open(self, *args, **kwargs): return Response()

b.urllib.request.build_opener = lambda *args: InvalidResponse()
try:
    b.local_call(b.ZAI_ENDPOINT, key, body, 1)
except ValueError as error:
    assert 'этап=разбор ответа API' in str(error) and 'private prompt' not in str(error)
else:
    raise AssertionError('Invalid API response accepted')
print('PASS: TLS, DNS, timeout, connection, HTTP errors, invalid JSON shape, credential redaction')
