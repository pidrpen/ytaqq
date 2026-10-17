"""Exercise the shipped MCP boundary itself, including raw write requests."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser(); p.add_argument('--tools', type=Path, required=True); a = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='cursorpad-mcp-test-') as name:
        root = Path(name); inside = root / 'allowed'; inside.mkdir()
        source = inside / 'исходник.txt'; source.write_text('preserve', encoding='utf-8')
        outside = root / 'секрет.txt'; outside.write_text('outside', encoding='utf-8')
        with subprocess.Popen([str(a.tools / 'node.exe'), str(a.tools / 'filesystem.cjs'), str(inside)],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)) as child:
            serial = 0
            def send(value):
                child.stdin.write((json.dumps(value, ensure_ascii=False) + '\n').encode()); child.stdin.flush()
            def call(method, params):
                nonlocal serial
                serial += 1; send({'jsonrpc':'2.0', 'id':serial, 'method':method, 'params':params})
                while True:
                    line = child.stdout.readline()
                    assert line, child.stderr.read().decode(errors='replace')
                    result = json.loads(line)
                    if result.get('id') == serial: return result
            try:
                init = call('initialize', {'protocolVersion':'2024-11-05', 'capabilities':{},
                                          'clientInfo':{'name':'boundary-test', 'version':'1'}})
                assert init['result']['serverInfo']['version'] == '1.0.0'
                send({'jsonrpc':'2.0', 'method':'notifications/initialized'})
                tools = call('tools/list', {})['result']['tools']
                assert {t['name'] for t in tools} == {'list_directory', 'get_file_info', 'read_text_file'}
                for tool, args in [('write_file', {'path':str(source), 'content':'destroy'}),
                                   ('edit_file', {'path':str(source), 'edits':[]}),
                                   ('create_directory', {'path':str(inside / 'new')}),
                                   ('move_file', {'source':str(source), 'destination':str(inside / 'gone.txt')}),
                                   ('delete_file', {'path':str(source)})]:
                    result = call('tools/call', {'name':tool, 'arguments':args})
                    assert result['error']['code'] == -32601, result
                escaped = call('tools/call', {'name':'read_text_file', 'arguments':{'path':str(outside)}})
                assert escaped['result']['isError']
                normal = call('tools/call', {'name':'read_text_file', 'arguments':{'path':str(source)}})
                assert normal['result']['content'][0]['text'] == 'preserve'
                assert source.read_text() == 'preserve' and set(inside.iterdir()) == {source}
            finally:
                child.kill(); child.wait(timeout=5)
    print('PASS: MCP advertises only 3 read tools; 5 raw mutation calls and out-of-root read rejected')


if __name__ == '__main__': main()
