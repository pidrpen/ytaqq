"""Offline Z.ai protocol tests: native UI/DPAPI -> real bundled bridge -> mock
HTTPS -> native chat/Word plan/Apply. No real key, model, API usage or user files.
"""
import argparse, hashlib, json, os, subprocess, zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

ap=argparse.ArgumentParser();ap.add_argument('--probe',type=Path,required=True);ap.add_argument('--root',type=Path,required=True)
a=ap.parse_args();root=a.root.resolve();root.mkdir(parents=True,exist_ok=False)
repo=Path(__file__).resolve().parent.parent;lock=json.loads((repo/'native/agent_pack.json').read_text(encoding='utf-8'))
probe=a.probe.resolve();archive=probe.parent/lock['file']
if not archive.exists():os.link(repo/'public'/lock['file'],archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest()==lock['sha256']
profile=root/'profile';runtime=profile/'agent'/lock['sha256'];fake='cursorpad-zai-test-only-not-a-real-key'
q=root/'request.json';q.write_text('{}',encoding='utf-8');report=[]
def run(mode,label,request):
    q.write_text(json.dumps(request,ensure_ascii=False),encoding='utf-8');out=root/(label+'.json')
    p=subprocess.run([str(probe),str(profile),str(q),str(out),str(mode),'unused',str(root/'stub.py')],timeout=180)
    r=json.loads(out.read_text(encoding='utf-8'));report.append(dict(case=label,returncode=p.returncode,**r));return r

stub=r'''import importlib.util,json,sys,urllib.error
from pathlib import Path
spec=importlib.util.spec_from_file_location('production_bridge',Path(BRIDGE))
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
original=json.loads(Path(sys.argv[sys.argv.index('--request')+1]).read_text(encoding='utf-8'))
mode=original.get('test_case','chat')
class Response:
 def __init__(self,data):self.data=json.dumps(data).encode('utf-8')
 def __enter__(self):return self
 def __exit__(self,*a):pass
 def read(self,*a):return self.data
class Mock:
 def open(self,request,timeout):
  assert request.full_url==b.ZAI_ENDPOINT,'Unexpected destination'
  assert request.get_header('Authorization')=='Bearer '+FAKE,'Wrong protected credential'
  body=json.loads(request.data);assert body['model']=='glm-4.7'
  assert not any(k in body for k in ['cursorpad','cache_prompt','chat_template_kwargs','test_case'])
  if mode in ['401','429']:raise urllib.error.HTTPError(request.full_url,int(mode),'test',{},None)
  schema=None
  for m in body['messages']:
   if m['role']=='system' and '\n' not in m['content'] and False:pass
   if m['content'].startswith('Return ONLY one JSON value'):
    schema=json.loads(m['content'].split('\n',1)[1])
  content='Ответ Z.ai'
  if schema:
   if 'requirements' in schema.get('properties',{}):content=json.dumps({'requirements':[original['cursorpad']['goal']]},ensure_ascii=False)
   elif 'anyOf' in schema:content=json.dumps({'tool':'write','path':'@desktop\\API.docx','text':'Проверка Z.ai'},ensure_ascii=False)
   else:content=json.dumps({'ok':True})
  if mode=='bad-schema':content='{"wrong":1}'
  if mode=='bad-json':content='not JSON'
  return Response({'choices':[{'message':{'content':content},'finish_reason':'length' if mode=='length' else 'stop'}]})
b.urllib.request.build_opener=lambda *args:Mock()
b.main()
'''.replace('BRIDGE',repr(str(runtime/'bridge.py'))).replace('FAKE',repr(fake))
# Use actual newline escape literals inside the generated transport code.
stub=stub.replace("split('\\\\n',1)","split('\\n',1)")
(root/'stub.py').write_text(stub,encoding='utf-8')
schema={'type':'object','properties':{'ok':{'const':True}},'required':['ok'],'additionalProperties':False}
body={'model':'glm-4.7','messages':[{'role':'user','content':'Тест'}],'stream':True,'max_tokens':100,'cache_prompt':True,'chat_template_kwargs':{'enable_thinking':False}}
try:
    r=run(-29,'credentials',{});assert r['status']==0,r
    r=run(-30,'chat',body);assert r['status']==0 and r['text']=='Ответ Z.ai',r
    structured={**body,'response_format':{'type':'json_schema','json_schema':{'name':'test','strict':True,'schema':schema}}}
    r=run(-30,'structured',structured);assert r['status']==0 and json.loads(r['text'])=={'ok':True},r
    for label in ['bad-schema','bad-json','length','401','429']:
        r=run(-30,label,{**structured,'test_case':label});assert r['status']!=0 and fake not in r['text'],r
    protocol=json.loads(subprocess.check_output([str(probe),'--schema']).decode('utf-8'))
    request={**body,**protocol,'cursorpad':dict(goal='Создай Word с текстом API',plan='',plan_count=0,observation='',observation_state=0)}
    r=run(-31,'word',request);assert r['status']==0,r
    with zipfile.ZipFile(profile/'files/API.docx') as z:
        xml=ET.fromstring(z.read('word/document.xml'));assert 'Проверка Z.ai' in ''.join(xml.itertext())
    for p in profile.rglob('*'):
        if p.is_file() and (p.suffix in ['.json','.key'] or p.name=='ai.txt'):
            assert fake.encode() not in p.read_bytes(),f'Credential leaked to {p.name}'
    unit=r'''import importlib.util,json
from pathlib import Path
spec=importlib.util.spec_from_file_location('b',BRIDGE);b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
assert b.read_protected_key(Path(KEY))==FAKE
b.CLOUD=True
for endpoint in ['http://api.z.ai/api/paas/v4/chat/completions','https://example.org','http://127.0.0.1:1234/v1/chat/completions']:
 try:b.local_call(endpoint,FAKE,{},1)
 except ValueError:pass
 else:raise AssertionError('Unapproved endpoint accepted')
b.CLOUD=False
try:b.local_call(b.ZAI_ENDPOINT,FAKE,{},1)
except ValueError:pass
else:raise AssertionError('Local mode reached cloud')
wire,s=b.zai_wire({'model':'glm-5.3','messages':[],'max_tokens':100,'cache_prompt':True});assert wire['thinking']['type']=='enabled' and wire['max_tokens']>=8192 and 'cache_prompt' not in wire
wire,s=b.zai_wire({'model':'glm-4.7','messages':[]});assert wire['thinking']['type']=='disabled'
print('PASS: DPAPI interoperability, endpoint allowlist, no local cloud fallback, GLM thinking compatibility')
'''.replace('BRIDGE',repr(str(runtime/'bridge.py'))).replace('KEY',repr(str(profile/'zai-key.dat'))).replace('FAKE',repr(fake))
    (root/'unit.py').write_text(unit,encoding='utf-8')
    subprocess.run([str(runtime/'python.exe'),'-I','-B',str(root/'unit.py')],check=True,timeout=60)
    subprocess.run([str(runtime/'python.exe'),'-I','-B',str(repo/'native/test_agent_coverage.py'),str(runtime/'bridge.py')],check=True,timeout=60)
    (root/'verified.json').write_text(json.dumps(dict(passed=True,native_cases=len(report),live_api_calls=0,protected_credentials=True,endpoint_allowlist=True,local_mode_preserved=True,word_apply_verified=True),indent=2),encoding='utf-8')
    print('PASS: native UI/credential lifecycle, chat, schema validation, 401/429/truncation, Word plan/Apply; no live API calls',flush=True)
finally:
    (root/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
