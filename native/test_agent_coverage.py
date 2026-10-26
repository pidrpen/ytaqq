"""Run using the bundled isolated Python: python.exe -I -B test_agent_coverage.py bridge.py.
Tests the coverage contract without calling a model or executing file actions.
"""
import importlib.util
from pathlib import Path
import sys

bridge_path = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(bridge_path.parent))  # bridge.py импортирует соседей (network_diagnostics); с -I их папки в sys.path нет
spec = importlib.util.spec_from_file_location('coverage_bridge', bridge_path)
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

def check(number, status='planned', actions=None):
    return dict(requirement=number, status=status, actions=[1] if actions is None else actions, reason='test')

def audit(checks, count=1, plan='[action=1] excel_write'):
    raw = {'checks': {str(c['requirement']): {k:v for k,v in c.items() if k != 'requirement'} for c in checks}}
    return bridge.audit_result(raw, ['create workbook', 'fill table'], plan, count)

assert audit([check(1), check(2)])['covered']
assert not audit([check(1), check(2, 'missing', [])])['covered']
assert not audit([check(1), check(2, actions=[99])], plan='[action=1] data=[action=99]')['covered']
assert not audit([check(1), check(2, actions=[])])['covered']
assert not audit([check(1), check(2, 'answered', [])])['covered']
assert audit([check(1, 'answered', []), check(2, 'answered', [])], 0, '')['covered']
for checks in [[check(1)], [check(1), check(3)]]:
    try:
        audit(checks)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid requirement references accepted')
schema = bridge.audit_schema(2)
bridge.jsonschema.validate({'checks': {'1': {k:v for k,v in check(1).items() if k!='requirement'}, '2': {k:v for k,v in check(2).items() if k!='requirement'}}}, schema)
try:
    bridge.jsonschema.validate({'checks': {'1': {k:v for k,v in check(1).items() if k!='requirement'}}}, schema)
except bridge.jsonschema.ValidationError:
    pass
else:
    raise AssertionError('Schema allowed a missing requirement')
print('PASS: coverage IDs, missing requirements, invalid/duplicate references, staged versus read evidence')

# Completion recovery must use the same coverage gate and only a successful
# native staging observation. No real model or filesystem action is invoked.
import json
import tempfile
scratch = Path(tempfile.mkdtemp(prefix='cursorpad-coverage-'))
original_step, original_review = bridge.paw_step, bridge.review_plan
action = dict(tool='excel_write', path='@desktop/test.xlsx', text='A\tB')
try:
    for label, covered, native_state, count, expected, calls_expected in [
        ('complete', True, 0, 1, 'done', 1),
        ('incomplete', False, 0, 1, 'ask', 1),
        ('failed', True, -1, 1, 'ask', 0),
        ('empty', True, 0, 0, 'ask', 0),
    ]:
        state = scratch/(label+'.json')
        state.write_text(json.dumps(dict(goal='Excel', requirements=['Excel'], backend='QwenPaw',
            coverage_contract=3, last_action=action)), encoding='utf-8')
        async def repeated(*args):
            return dict(action)
        calls = []
        def review(*args):
            calls.append(args)
            return dict(covered=covered, missing=[] if covered else ['data missing'])
        bridge.paw_step, bridge.review_plan = repeated, review
        request = dict(messages=[dict(role='system', content='test'),dict(role='user',content='Папки: @desktop=test')],
            cursorpad=dict(goal='Excel', plan='[action=1]' if count else '', plan_count=count,
                observation='staged' if native_state==0 else 'failed', observation_state=native_state))
        result = bridge.choose(request, 'unused', '', 1, state)
        assert result['tool']==expected and len(calls)==calls_expected,(label,result,len(calls))
finally:
    bridge.paw_step, bridge.review_plan = original_step, original_review
print('PASS: repeated successful staging reaches review only with complete coverage; failed/missing/empty plans cannot finish')
