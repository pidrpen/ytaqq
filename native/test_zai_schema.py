"""Regression cases for the reported Z.ai schema failures; no network or files changed."""
import importlib.util
import io
import json
from pathlib import Path
import sys
import urllib.error

spec = importlib.util.spec_from_file_location('zai_bridge', Path(sys.argv[1]).resolve())
b = importlib.util.module_from_spec(spec); spec.loader.exec_module(b)
b.CLOUD = True
key = 'cursorpad-fake-schema-test'

class Response:
    def __init__(self, content, finish='stop'):
        self.raw = json.dumps({'choices':[{'message':{'content':content},'finish_reason':finish}]}).encode()
    def __enter__(self): return self
    def __exit__(self,*args): pass
    def read(self,*args): return self.raw

class Transport:
    def __init__(self, replies): self.replies, self.calls = iter(replies), []
    def open(self, request, timeout):
        assert request.full_url == b.ZAI_ENDPOINT
        wire = json.loads(request.data); self.calls.append(wire)
        value = next(self.replies)
        if isinstance(value, BaseException): raise value
        return value

def run(replies, schema):
    mock = Transport(replies); b.urllib.request.build_opener = lambda *args: mock
    body = {'model':'glm-4.5-flash','messages':[{'role':'user','content':'мало информации в ексель, нужно дополнить'}],
            'response_format':{'type':'json_schema','json_schema':{'schema':schema}},'max_tokens':1000}
    return mock, body

# Both real failing explanations now pass without another API request.
for reason in [
    'Требование создания Excel с рейтингом топ 5 инженеров на рабочем столе полностью покрывается действием excel_write в текущем плане',
    "Результат чтения показывает содержимое папки 'Спиннер 608' на рабочем столе: файлы build_spinner.py и spinner_608.stl"]:
    assert len(reason)>100
    data = {'checks':{'1':{'status':'planned','actions':[1],'reason':reason}}}
    mock, body = run([Response(json.dumps(data,ensure_ascii=False))], b.audit_schema(1))
    result = json.loads(b.local_call(b.ZAI_ENDPOINT,key,body,2))
    assert result == data and len(mock.calls)==1
    assert b.audit_result(result,['Excel'],'[action=1] excel_write',1)['covered']
    assert not b.audit_result(result,['Excel'],'[action=1] excel_write',0)['covered']

# A scalar instead of requirements object is repaired exactly once.
expected = {'requirements':['мало информации в ексель, нужно дополнить']}
for invalid in [json.dumps(expected['requirements'][0],ensure_ascii=False), 'not JSON', '{"requirements":[],"requirements":[]}']:
    mock, body = run([Response(invalid),Response(json.dumps(expected,ensure_ascii=False))],b.CHECKLIST_SCHEMA)
    assert json.loads(b.local_call(b.ZAI_ENDPOINT,key,body,2))==expected
    assert len(mock.calls)==2
    correction=mock.calls[1]['messages'][-1]['content']
    assert 'format repair only' in correction and mock.calls[1]['messages'][-2]['content']==invalid

# A second bad answer remains a hard failure: no substitution of file arguments.
for invalid in ['"wrong"','{"requirements":[]}']:
    mock, body = run([Response(invalid),Response(invalid)],b.CHECKLIST_SCHEMA)
    try: b.local_call(b.ZAI_ENDPOINT,key,body,2)
    except ValueError as error:
        assert 'после одной попытки' in str(error) and 'Failed validating' not in str(error) and key not in str(error)
    else: raise AssertionError('invalid JSON accepted')
    assert len(mock.calls)==2

# Length overflow is repaired, while action IDs are still checked by the native gate.
data={'checks':{'1':{'status':'planned','actions':[999],'reason':'x'*1025}}}
fixed={'checks':{'1':{'status':'planned','actions':[999],'reason':'short'}}}
mock, body=run([Response(json.dumps(data)),Response(json.dumps(fixed))],b.audit_schema(1))
result=json.loads(b.local_call(b.ZAI_ENDPOINT,key,body,2))
assert not b.audit_result(result,['Excel'],'[action=1] excel_write',1)['covered']
assert len(mock.calls)==2

# Provider failures never trigger an automatic repeat / switch to a paid model.
for status, code, expected_text in [(429,'1113','недостаточный баланс'),(429,'1302','лимит запросов'),(429,'1305','временно перегружен'),(401,'1000','API-ключ')]:
    error=urllib.error.HTTPError(b.ZAI_ENDPOINT,status,'test',{},io.BytesIO(json.dumps({'error':{'code':code,'message':key}}).encode()))
    mock, body=run([error],b.CHECKLIST_SCHEMA)
    try:b.local_call(b.ZAI_ENDPOINT,key,body,2)
    except ValueError as e:
        assert expected_text in str(e) and code in str(e) and key not in str(e) and 'glm-4.5-flash' in str(e)
    else:raise AssertionError('HTTP error ignored')
    assert len(mock.calls)==1

for finish, expected_text in [('length','предел длины'),('sensitive','ограничен сервисом')]:
    mock, body=run([Response('{}',finish)],b.CHECKLIST_SCHEMA)
    try:b.local_call(b.ZAI_ENDPOINT,key,body,2)
    except ValueError as e:assert expected_text in str(e)
    else:raise AssertionError('incomplete response accepted')
    assert len(mock.calls)==1

# Vision Flash keeps the image, but does not request unsupported JSON-mode flags.
image={'type':'image_url','image_url':{'url':'data:image/png;base64,TEST'}}
wire, schema=b.zai_wire({'model':'glm-4.6v-flash','messages':[{'role':'user','content':[image]}],
    'response_format':{'type':'json_schema','json_schema':{'schema':b.CHECKLIST_SCHEMA}}})
assert wire['messages'][-1]['content']==[image] and 'response_format' not in wire and schema==b.CHECKLIST_SCHEMA
for model in ['glm-4.7','glm-5.3','glm-5.3-flash','glm-4.6v','glm-4.7-flashx','glm-4.6v-flashx','GLM-4.5-Flash','glm-unknown']:
    mock, body=run([],b.CHECKLIST_SCHEMA); body['model']=model
    try:b.local_call(b.ZAI_ENDPOINT,key,body,2)
    except ValueError as e:assert 'только бесплатные' in str(e)
    else:raise AssertionError('paid or unknown model accepted')
    assert not mock.calls
for model in ['glm-4.5-flash','glm-4.7-flash','glm-4.6v-flash']:
    wire,_=b.zai_wire({'model':model,'messages':[]})
    assert wire['model']==model and wire['thinking']['type']=='disabled'
print('PASS: free-model allowlist before network; both reported long reasons; scalar/invalid/duplicate JSON repair; one retry cap; strict IDs; 1113 balance, 401, rate, truncation; vision payload; zero live API calls')
