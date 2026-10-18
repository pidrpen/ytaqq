"""Run using the bundled isolated Python: python.exe -I -B test_agent_coverage.py bridge.py.
Tests the coverage contract without calling a model or executing file actions.
"""
import importlib.util
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('coverage_bridge', Path(sys.argv[1]).resolve())
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
