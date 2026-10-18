"""Requires openpyxl, QWEN_TEST probe and preinstalled packs in a scratch profile.
No real Desktop files or application settings are changed. Uses an independent
server and closes only its own child processes. Inspect report rows for semantics.
"""
from pathlib import Path
import json,os,subprocess,time,urllib.request,argparse,socket
import openpyxl
ap=argparse.ArgumentParser(description='Real local model -> bundled QwenPaw -> native Excel plan -> Apply; isolated fixtures only')
ap.add_argument('--probe',type=Path,required=True)
ap.add_argument('--profile',type=Path,required=True,help='Prepared scratch profile with downloaded agent/tools; never the user profile')
ap.add_argument('--runtime',type=Path,required=True,help='Extracted QwenPaw directory with python.exe and bridge.py')
ap.add_argument('--model',type=Path,required=True)
ap.add_argument('--engine',type=Path,required=True)
ap.add_argument('--root',type=Path,required=True,help='New output directory')
ap.add_argument('--port',type=int,default=39407)
args=ap.parse_args()
root=args.root.resolve();root.mkdir(exist_ok=False,parents=True)
profile=args.profile.resolve();runtime=args.runtime.resolve();probe=args.probe.resolve()
model=args.model.resolve();engine=args.engine.resolve();port=args.port
assert profile != (Path.home()/'AppData/Local/CursorPad').resolve(),'Use a scratch profile'
with socket.socket() as available:available.bind(('127.0.0.1',port))
log=(root/'server.log').open('wb')
server=subprocess.Popen([str(engine),'-m',str(model),'--host','127.0.0.1','--port',str(port),'-c','16384','--parallel','1','-ngl','99','--jinja','--chat-template-kwargs','{"enable_thinking":false}'],stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
report=[]
try:
    start=time.monotonic()
    while True:
        try:
            if urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=2).status==200:break
        except Exception:pass
        assert server.poll() is None,'server exited'
        assert time.monotonic()-start<100,'server startup timeout'
        time.sleep(.2)
    system=json.loads(subprocess.check_output([str(probe),'--system']).decode('utf-8'))
    schema=json.loads(subprocess.check_output([str(probe),'--schema']).decode('utf-8'))
    cases=[
        ('literal','Создай на рабочем столе Материалы.xlsx с оформленной таблицей: колонки Материал и Код, строки Сталь 40Х и 00123, Алюминий и 00124. Коды сохрани текстом с нулями.'),
        ('two-lists','создай ексель на рабочем столе и создай таблицу топ причин купить велосипед и топ причин не покупать велосипед'),
    ]
    for label,goal in cases:
        desktop=root/label;desktop.mkdir()
        env=os.environ.copy();env['CURSORPAD_TEST_PROFILE']=str(profile)
        p=subprocess.Popen([str(probe),str(desktop)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',env=env)
        def tool(action):
            p.stdin.write((action if isinstance(action,str) else json.dumps(action,ensure_ascii=False))+'\n');p.stdin.flush()
            line=p.stdout.readline();assert line,'native probe exited';return json.loads(line)
        result=tool({'goal':goal});state=root/(label+'.state.json');notes='';steps=[]
        try:
            for step in range(16):
                prompt=f'Задача пользователя: {goal}\nПапки: @desktop={desktop}; @downloads={desktop}/Downloads; @documents={desktop}/Documents; @last={desktop}; @new=.\nШаг {step+1}. Действий в плане: {result["count"]}.\n'+result['evidence']+'\n'+notes[-7000:]
                request={'model':'local',**schema,'messages':[{'role':'system','content':system},{'role':'user','content':prompt}], 'temperature':.2,'max_tokens':1024,'stream':False,'chat_template_kwargs':{'enable_thinking':False},'cursorpad':{'goal':result['model_goal'],'plan':result['evidence'],'plan_count':result['count'],'observation':result['observation'],'observation_state':result['state']}}
                inp=root/'request.json';out=root/'response.json';inp.write_text(json.dumps(request,ensure_ascii=False),encoding='utf-8')
                subprocess.run([str(runtime/'python.exe'),'-I','-B',str(runtime/'bridge.py'),'--request',str(inp),'--response',str(out),'--state',str(state),'--endpoint',f'http://127.0.0.1:{port}/v1/chat/completions'],check=True,timeout=180)
                reply=json.loads(out.read_text(encoding='utf-8'));assert reply['ok'],reply
                action=json.loads(reply['action']);result=tool(action)
                steps.append(dict(action=action,result=result))
                print(label,step+1,action['tool'],'state',result['state'],'planned',result['count'],flush=True)
                notes+='\nВызов: '+json.dumps(action,ensure_ascii=False)+'\nРезультат: '+result['observation']+'\n'
                if result['state'] in [1,2]:break
            assert result['state']==1 and result['count']==1,result
            assert not list(desktop.iterdir()),'preview mutated desktop'
            applied=tool(':apply');assert applied['state']==1,applied
            files=list(desktop.glob('*.xlsx'));assert len(files)==1
            book=openpyxl.load_workbook(files[0]);sheet=book.active;rows=[list(r) for r in sheet.values]
            if label=='literal':
                assert rows==[['Материал','Код'],['Сталь 40Х','00123'],['Алюминий','00124']],rows
                assert sheet['A1'].font.bold and sheet.freeze_panes=='A2' and sheet.auto_filter.ref
            else:
                assert len(rows)>=4 and all(len(r)==2 and all(isinstance(x,str) and x.strip() and not x.strip().isdigit() for x in r) for r in rows),rows
                assert rows[0][0] != rows[0][1] and all(len({r[c] for r in rows[1:]})>=3 for c in range(2)),rows
            report.append(dict(case=label,pass_test=True,steps=steps,rows=rows,file=str(files[0]),scope='real model -> QwenPaw -> native plan -> Apply -> XlsxWriter -> independent openpyxl'))
            print(label,'PASS',json.dumps(rows,ensure_ascii=True),flush=True)
        except Exception as e:
            report.append(dict(case=label,pass_test=False,error=str(e),steps=steps))
            raise
        finally:
            p.stdin.close();p.wait(timeout=10)
finally:
    (root/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    server.terminate()
    try:server.wait(timeout=15)
    except subprocess.TimeoutExpired:server.kill();server.wait()
    log.close()
