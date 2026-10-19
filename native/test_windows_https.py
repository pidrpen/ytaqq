"""Exercise production WinHTTP bindings and urllib integration without API usage."""
import ctypes
import importlib.util
import json
from pathlib import Path
import sys
import urllib.error
import urllib.request
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('windows_https', Path(sys.argv[1]).resolve())
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)


class Function:
    def __init__(self, callback): self.callback = callback
    def __call__(self, *args): return self.callback(*args)


class API:
    def __init__(self, status=200, body=b'{"choices":[]}', fail=False):
        self.status, self.body, self.fail = status, body, fail
        self.opened, self.closed, self.sent = [], [], 0
        for name in ['Open', 'Connect', 'OpenRequest', 'SetTimeouts', 'SetOption',
                     'SendRequest', 'ReceiveResponse', 'QueryHeaders', 'ReadData', 'CloseHandle']:
            setattr(self, 'WinHttp'+name, Function(getattr(self, name)))
    def new(self):
        value = 0x100000000 + len(self.opened) + 1
        self.opened.append(value)
        return value
    def Open(self, agent, proxy, *args):
        assert proxy == 4
        return self.new()
    def Connect(self, session, host, port, reserved):
        assert host == 'api.z.ai' and port == 443 and session == self.opened[0]
        return self.new()
    def OpenRequest(self, connection, method, path, version, referer, types, flags):
        assert method == 'POST' and path == '/api/paas/v4/chat/completions'
        assert flags == 0x00800000 and connection == self.opened[1]
        return self.new()
    def SetTimeouts(self, *args): return 1
    def SetOption(self, handle, option, value, size):
        assert option == 88 and value._obj.value == 0  # Never disable TLS verification.
        return 1
    def SendRequest(self, handle, headers, header_size, data, size, total, context):
        assert 'Authorization: Bearer fake-key' in headers
        assert json.loads(ctypes.string_at(data, size))['model'] == 'glm-4.5-flash'
        self.sent += 1
        return not self.fail
    def ReceiveResponse(self, *args): return 1
    def QueryHeaders(self, handle, query, name, result, size, index):
        assert query == 19 | 0x20000000
        result._obj.value = self.status
        return 1
    def ReadData(self, handle, buffer, size, got):
        chunk, self.body = self.body[:size], self.body[size:]
        ctypes.memmove(buffer, chunk, len(chunk))
        got._obj.value = len(chunk)
        return 1
    def CloseHandle(self, handle):
        self.closed.append(handle)
        return 1


def run(api):
    with patch.object(ctypes, 'WinDLL', lambda *args, **kwargs: api, create=True), \
         patch.object(ctypes, 'get_last_error', lambda: 12175, create=True), \
         patch.object(ctypes, 'FormatError', lambda code: 'certificate rejected', create=True):
        request = urllib.request.Request(w.ENDPOINT, b'{"model":"glm-4.5-flash"}',
                                         {'Authorization': 'Bearer fake-key'})
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), w.WindowsHTTPSHandler())
        return opener.open(request, timeout=2)


for status in [200, 401, 403, 429, 302]:
    api = API(status)
    try:
        response = run(api)
        assert status == 200 and response.read() == b'{"choices":[]}'
        response.close()
    except urllib.error.HTTPError as error:
        assert error.code == status != 200
    assert api.closed == list(reversed(api.opened)) and api.sent == 1

for api, expected in [(API(body=b'x' * (w.LIMIT + 1)), ValueError), (API(fail=True), OSError)]:
    try:
        run(api)
    except expected:
        pass
    else:
        raise AssertionError('Failed request was accepted')
    assert api.closed == list(reversed(api.opened))

for url in ['http://api.z.ai/api/paas/v4/chat/completions', 'https://example.org/', w.ENDPOINT+'?key=fake']:
    try:
        w.windows_post(urllib.request.Request(url, b'{}'))
    except ValueError:
        pass
    else:
        raise AssertionError('Unapproved endpoint accepted')

# Run the actual bridge transport through urllib -> WinHTTP -> API reply.
import ast
import types
bridge = Path(sys.argv[1]).resolve().with_name('bridge.py')
sys.path.insert(0, str(bridge.parent))
tree = ast.parse(bridge.read_text(encoding='utf-8'))
tree.body = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in {'local_call', 'zai_wire', 'strict_json'}]
scope = {}
exec('import copy, json, re, time, urllib.request, urllib.error\nfrom network_diagnostics import connection_error, safe_text', scope)
scope.update(CLOUD=True, sys=types.SimpleNamespace(platform='win32'), WindowsHTTPSHandler=w.WindowsHTTPSHandler, ZAI_ENDPOINT=w.ENDPOINT)
exec(compile(tree, str(bridge), 'exec'), scope)
api = API(body=json.dumps({'choices': [{'message': {'content': 'Windows HTTPS works'}, 'finish_reason': 'stop'}]}).encode())
with patch.object(ctypes, 'WinDLL', lambda *args, **kwargs: api, create=True):
    assert scope['local_call'](w.ENDPOINT, 'fake-key', {'model': 'glm-4.5-flash', 'messages': []}, 2) == 'Windows HTTPS works'
assert api.closed == list(reversed(api.opened))
print('PASS: WinHTTP 64-bit bindings, OS proxy, strict TLS flags, redirects blocked, status/body, bounds, handle cleanup')
