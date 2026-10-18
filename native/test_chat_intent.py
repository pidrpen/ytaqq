"""Run against a QWEN_TEST probe and user-supplied local model in a fresh profile.
The prior assistant turns below are synthetic fixtures, not real completed actions.
"""
from pathlib import Path
import json,os,subprocess,time,sys
import argparse,shutil
ap=argparse.ArgumentParser(description='Native protocol tests and real local Qwen intent decisions; no file actions or network search executed')
ap.add_argument('--probe',type=Path,required=True)
ap.add_argument('--model',type=Path,required=True)
ap.add_argument('--engine',type=Path,required=True)
ap.add_argument('--root',type=Path,required=True)
args=ap.parse_args()
root=args.root.resolve();root.mkdir(exist_ok=False,parents=True)
profile=root/'profile';profile.mkdir()
q=root/'question.txt';q.write_text('test',encoding='utf-8')
probe=args.probe.resolve()
for mode in [-21,-18,-15,-12]:
    out=root/f'protocol{mode}.json'
    r=subprocess.run([str(probe),str(profile),str(q),str(out),str(mode),'unused','unused'],timeout=30)
    result=json.loads(out.read_text(encoding='utf-8'));print(mode,result['text'],flush=True)
    assert r.returncode==0 and result['status']==0,result
cases=[
    dict(query='бро я набухался',expected=3),
    dict(query='как правильно уебать бабе за кривой базар',expected=3),
    dict(query='создай ексель на рабочем столе и создай таблицу топ причин уебать и топ причин не бить',previous_user='как ударить человека за оскорбление',previous_answer='Лучше отойти и прекратить конфликт.',expected=3),
    dict(query='привет',expected=3),
    dict(query='ты лох понимаешь',expected=3),
    dict(query='ну это вообще какая-то хрень получилась',previous_user='расскажи про обработку вала',previous_answer='Подготовка, токарная обработка и контроль.',expected=3),
    dict(query='ахах ну ты выдал конечно',expected=3),
    dict(query='давай просто поболтаем, как дела',expected=3),
    dict(query='как в ворде сделать текст по центру?',expected=3),
    dict(query='объясни как работает формула СУММ в экселе',expected=3),
    dict(query='создай на рабочем столе папку Модели и перенеси туда все stl',expected=1),
    dict(query='какие файлы лежат в папке 34д на рабочем столе?',expected=1),
    dict(query='создай ворд файл на рабочем столе с описанием пяти методов обработки вала',expected=1),
    dict(query='теперь выровняй его по центру',previous_user='создай на рабочем столе ворд файл про вал',previous_answer='Готово. Создан файл C:\\test\\Вал.docx.',expected=1),
    dict(query='в C:\\test\\Материалы.xlsx сравни первый и второй лист',expected=1),
    dict(query='C:\\test\\Материалы.xlsx',previous_user='прочитай мой эксель',previous_answer='Укажите путь к книге.',pending='1',expected=1),
    dict(query='ты вообще меня не понимаешь',previous_user='перенеси папку',previous_answer='Укажите исходную папку.',pending='1',expected=3),
    dict(query='какая погода завтра в спб',expected=2),
    dict(query='привет, какие сегодня новости в Петербурге?',expected=2),
    dict(query='поищи в интернете характеристики подшипника 608RS',expected=2),
    dict(query='что находится по этой ссылке https://www.ozon.ru/product/item-123/',expected=2),
    dict(query='почему зимой холоднее чем летом',expected=3),
    dict(query='да ты издеваешься что ли',expected=3),
    dict(query='расскажи анекдот про эксель',expected=3),
    dict(query='как перенести папку в другую?',expected=3),
    dict(query='покажи что лежит у меня в Загрузках',expected=1),
    dict(query='запиши в него заголовок Отчёт',previous_user='создай документ',previous_answer='Документ сохранён в C:\\test\\Отчёт.docx.',expected=1),
    dict(query='ничего не создавай, объясни что ты собираешься сделать',previous_user='создай папку для моделей',previous_answer='Как назвать папку?',pending='1',expected=3),
    dict(query='проверь существует ли папка C:\\test\\Архив',expected=1),
    dict(query='можешь создать в C:\\test эксель со списком файлов?',expected=1),
    dict(query='предположи',previous_user='как могла бы выглядеть моя мастерская через пять лет',previous_answer='Это зависит от оборудования и задач.',expected=3),
]
source=root/'cases.jsonl';source.write_text('\n'.join(json.dumps(c,ensure_ascii=False) for c in cases),encoding='utf-8')
modeldir=root/'text-model';modeldir.mkdir()
model=modeldir/args.model.name
try:os.link(args.model.resolve(),model)
except OSError:shutil.copyfile(args.model.resolve(),model)
t=time.monotonic()
p=subprocess.run([str(probe),str(profile),str(source),str(root/'result.json'),'-22',str(model),str(args.engine.resolve())],timeout=900)
results=[json.loads(line) for line in (profile/'routes.jsonl').read_text(encoding='utf-8').splitlines()]
failures=[]
for c,r in zip(cases,results):
    actual=json.loads(r['raw']) if r['valid'] else {}
    r['expected']=c['expected'];r['correct']=r['intent']==c['expected']
    print(('PASS ' if r['correct'] else 'FAIL ')+str(r['intent'])+'/'+str(c['expected'])+' '+c['query'],flush=True)
    if not r['correct']:failures.append(c['query'])
report=dict(cases=len(results),expected_cases=len(cases),correct=sum(r['correct'] for r in results),failures=failures,seconds=round(time.monotonic()-t,2),results=results,scope='real local Qwen chooses intent only; file actions, network search and final chat replies are covered separately')
(root/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert p.returncode==0 and len(results)==len(cases) and not failures,report
print('PASS real local intent suite',flush=True)
