"""Общее для дообучения и проверки модели, читающей чертежи в CursorPad (см. README.md).

Вход модели — ровно то, что шлёт CursorPad: картинки граф штампа и листа и вопросы из
выгрузки harness.exe --drawrec с CP_DRVLDUMP. Разметка листов — labels.json: чертежи
завода, лежит только на ПК. Здесь же повторено, как CursorPad склеивает ответы модели
с Tesseract перед правилами маршрута (drawrec.c dr_vl_read, drawrec_ui.c dr_tn_route)."""
import base64
import json
import os
import pathlib
import re
import subprocess
import tempfile
import time
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
TAGS = ('name', 'mat', 'sheet')
DR_CELL = 260  # drawrec.c
CLS = ['ROT', 'FLAT', 'BODY', 'UNK', 'BAR', 'BENT', 'REWORK']  # techno.c TN_*
# операция маршрута → группа для сверки с разметкой (Слесарная и Контрольная не считаются)
CAT = [('Заготовительная', 'ЗАГ'), ('Токарная', 'ТОК'), ('Термическая', 'ТО'), ('Зубофрезерная', 'ФРЕЗ'),
       ('Фрезерная', 'ФРЕЗ'), ('Сверлильная', 'СВЕРЛ'), ('шлифовальная', 'ШЛИФ'), ('Шлифовальная', 'ШЛИФ'),
       ('Гибочная', 'ГИБ'), ('Сварочная', 'СВАР'), ('Электроэрозионная', 'ЭЭ'), ('Окрасочная', 'ОКР'),
       ('Гальваническая', 'ГАЛЬВ'), ('Анодирование', 'ГАЛЬВ'), ('Маркировочная', 'МАРК'), ('Долбёжная', 'ДОЛБ'),
       ('Доводочная', 'ДОВ'), ('Отрезная', 'ОТР'), ('Штамповочная', 'ШТАМП'), ('Механическая', 'МЕХ')]
# конструктивные элементы — как в drawrec.c (kDrVlSheetSchemaEl3, dr_json_elements): тип → код строки «§EL»
# для правил techno.c; отверстия идут строкой «n отв. размер» (по оси тела вращения — «§AX n размер»),
# резьбы — самим размером. «уступ» и «карман» — с третьего поколения
ELEMENT_TYPES = ['шпоночный паз', 'шпоночный паз в отверстии', 'паз', 'лыска', 'грани под ключ', 'шлицы',
                 'шлицы в отверстии', 'зубья', 'резьба наружная', 'резьба внутренняя', 'резьбовое отверстие',
                 'отверстие', 'канавка', 'рифление', 'выдавка', 'сварной шов', 'уступ', 'карман']
EL_CODES = {'шпоночный паз в отверстии': 'keyin', 'шпоночный паз': 'key', 'паз': 'slot', 'лыска': 'flat',
            'грани под ключ': 'hex', 'шлицы в отверстии': 'splinein', 'шлицы': 'spline', 'зубья': 'gear',
            'рифление': 'knurl', 'выдавка': 'emboss', 'сварной шов': 'weld', 'уступ': 'step', 'карман': 'pocket'}
HOLE_TYPES = ('отверстие', 'резьбовое отверстие')
WHERE = ['', 'по оси', 'поперёк оси', 'в торце не по оси']  # где отверстие тела вращения
GAB_KEYS = ('d', 'l', 'b', 's')  # габариты: наибольший Ø, общая длина, ширина, толщина


def load_presets():
    return {k: v for k, v in json.loads((HERE / 'models.json').read_text('utf-8')).items() if k != '_'}


def load_prompts(dump):
    """tag → {'q': вопрос, 'schema': JSON-схема или None, 'max_tokens'}"""
    dump = pathlib.Path(dump)
    out = {}
    for t in TAGS:
        p = dump / f'{t}.prompt.txt'
        if not p.exists():
            continue
        s = dump / f'{t}.schema.json'
        out[t] = {'q': p.read_text('utf-8'), 'schema': json.loads(s.read_text('utf-8')) if s.exists() else None,
                  'max_tokens': int((dump / f'{t}.max_tokens.txt').read_text('utf-8'))}
    return out


def load_sheets(dump):
    """лист → {'name'|'mat'|'sheet': картинка или None, 'tess': (наименование, материал, текст)}"""
    dump = pathlib.Path(dump)
    sheets = {}
    for f in sorted(dump.glob('*.tess.txt')):
        base = f.name[:-len('.tess.txt')]
        t = f.read_text('utf-8').split('\n', 2) + ['', '']
        s = {'tess': (t[0], t[1], t[2])}
        for tag in TAGS:
            im = dump / f'{base}.{tag}.bmp'
            s[tag] = im if im.exists() else None
        sheets[base] = s
    return sheets


def load_labels(path):
    return {k: v for k, v in json.loads(pathlib.Path(path).read_text('utf-8')).items() if k != '_'}


def has_elements(prompts):
    """вопрос по листу спрашивает и конструктивные элементы (адаптер второго поколения)"""
    s = (prompts.get('sheet') or {}).get('schema') or {}
    return 'elements' in s.get('properties', {})


def has_gabarit(prompts):
    """вопрос по листу спрашивает и габариты, и где отверстия (третье поколение)"""
    s = (prompts.get('sheet') or {}).get('schema') or {}
    return 'gabarit' in s.get('properties', {})


def gen2_elements(elements):
    """элементы, как их называет второе поколение: без «где», без отверстий по оси, уступов и карманов"""
    return [{k: e[k] for k in ('type', 'size', 'count')} for e in elements
            if e.get('where') != 'по оси' and e['type'] not in ('уступ', 'карман')]


def target(label, tag, elements=False, gabarit=False):
    """что модель должна ответить на вопрос tag по разметке листа"""
    if tag == 'name':
        return label['name']
    if tag == 'mat':
        return label['material']
    ans = {'tt': label['tt'], 'razvertka': label['razvertka'], 'holes': label['holes']}
    if gabarit:
        g = label.get('gabarit') or {}
        ans['gabarit'] = {k: g.get(k) for k in GAB_KEYS}
        ans['elements'] = [{'type': e['type'], 'size': e['size'], 'count': e['count'], 'where': e.get('where', '')}
                           for e in label.get('elements', [])]
    elif elements:
        ans['elements'] = gen2_elements(label.get('elements', []))
    return json.dumps(ans, ensure_ascii=False)


# ---- как CursorPad разбирает ответы --------------------------------------------------

def first_line(ans):
    """dr_vl_line: первая строка без кавычек и пробелов по краям"""
    if not ans:
        return ''
    s = ans.lstrip(' \n\r"«')
    s = re.split(r'[\r\n]', s, maxsplit=1)[0][:DR_CELL - 1]
    return s.rstrip(' "»')


def _json_strings(j, key, out):
    """dr_json_strings: строки массива key — к out через перевод строки"""
    k = f'"{key}"'
    p = j.find(k)
    if p < 0:
        return out
    p = j.find('[', p + len(k))
    if p < 0:
        return out
    buf = list(out)
    p += 1
    while p < len(j) and j[p] != ']':
        if j[p] != '"':
            p += 1
            continue
        if buf:
            buf.append('\n')
        p += 1
        while p < len(j) and j[p] != '"':
            if j[p] == '\\' and p + 1 < len(j):
                p += 1
                buf.append(' ' if j[p] in 'nt' else j[p])
            else:
                buf.append(j[p])
            p += 1
        if p < len(j):
            p += 1
    return ''.join(buf)


def sheet_text(ans):
    """dr_vl_read: текст ТТ, «Развертка», надписи отверстий из JSON-ответа по листу"""
    if ans is None:
        return None
    t = _json_strings(ans, 'tt', '')
    if '"razvertka":true' in ans or '"razvertka": true' in ans:
        t += '\nРазвертка'
    t = _json_strings(ans, 'holes', t)
    g = gabarit_of(ans)  # dr_json_gabarit
    if g is not None:
        t += '\n§GB' + ''.join(f' {k}={g[k]:g}' for k in GAB_KEYS if g.get(k))
    for e in elements_of(ans):  # dr_json_elements
        line = ''
        if e['type'] in EL_CODES:
            line = '§EL ' + EL_CODES[e['type']]
        elif e['type'] in HOLE_TYPES:
            n = max(1, int(e.get('count') or 1))
            line = f"§AX {n} {e.get('size', '')}" if e.get('where') == 'по оси' else f"{n} отв. {e.get('size', '')}"
        elif e['type'].startswith('резьба') and e.get('size'):
            line = e['size']
        if line:
            t += '\n' + line
    return t


def gabarit_of(ans):
    """габариты из JSON-ответа: {'d': число или None, …}; ответа про габариты нет — None"""
    try:
        j = json.loads(ans) if isinstance(ans, str) else ans
        g = j.get('gabarit')
    except (ValueError, AttributeError):
        return None
    if not isinstance(g, dict):
        return None
    return {k: float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) and 0 < v < 100000 else None
            for k, v in ((k, g.get(k)) for k in GAB_KEYS)}


def elements_of(ans):
    """список элементов из JSON-ответа по листу; ответ без элементов или битый — пусто"""
    try:
        j = json.loads(ans) if isinstance(ans, str) else ans
        return [e for e in (j.get('elements') or []) if isinstance(e, dict) and e.get('type')]
    except (ValueError, AttributeError):
        return []


def route_input(tess, vl_name, vl_mat, vl_text):
    """dr_tn_route: наименование, материал и текст для правил маршрута"""
    tn, tm, tt = tess
    vn = bool(vl_name) and not vl_name[:15].lower().startswith('наимен')
    return (vl_name if vn else tn)[:DR_CELL - 1], f'{vl_mat}\n{tm}', f'{vl_text or ""}\n{tt or ""}'


def route(tn_cli, name, mat, text):
    """маршрут правилами techno.c: (вид, [операции], заготовка)"""
    fd, path = tempfile.mkstemp(suffix='.txt')
    with os.fdopen(fd, 'wb') as f:
        f.write(f'{name}\f{mat}\f{text}'.encode('utf-8'))
    try:
        r = subprocess.run([str(pathlib.Path(tn_cli).resolve()), path], capture_output=True, timeout=60).stdout.decode('utf-8')
    finally:
        os.unlink(path)
    head, _, blank = r.partition('||')
    parts = head.split('|')
    return (CLS[int(parts[0])] if parts[0].isdigit() else '?'), parts[1:], blank


def cats(ops):
    out = set()
    for o in ops:
        for k, c in CAT:
            if k in o:
                out.add(c)
                break
    return out


# ---- сравнение прочитанного -----------------------------------------------------------

_LAT = str.maketrans('ABCEHKMOPTXaceopxy', 'АВСЕНКМОРТХасеорху')


def norm(s):
    s = (s or '').translate(_LAT).lower().replace('ё', 'е').replace('⌀', 'ø')
    s = re.sub(r'[«»"“”„.,;:\\/|()\[\]\-–—_]+', ' ', s)
    return ' '.join(s.split())


def similar(a, b):
    import difflib
    return difflib.SequenceMatcher(None, norm(a), norm(b)).ratio()


# ---- llama-server, как его запускает CursorPad (ai_local.c) ---------------------------

def default_server():
    return pathlib.Path(os.environ.get('LOCALAPPDATA', '')) / 'CursorPad' / 'ai' / 'llama-vk' / 'llama-server.exe'


def models_dir():
    return pathlib.Path(os.environ.get('LOCALAPPDATA', '')) / 'CursorPad' / 'ai' / 'models'


class Server:
    """llama-server на 127.0.0.1 с моделью, проектором картинок и, если есть, адаптером
    чертежей (как draw-lora*.gguf в CursorPad: вес 1 только в запросах с картинкой)."""

    def __init__(self, model, mmproj, args=(), lora=None, exe=None, port=8391, ctx=8192, log=None):
        self.cmd = [str(exe or default_server()), '-m', str(model), '--mmproj', str(mmproj), '--host', '127.0.0.1',
                    '--port', str(port), '-c', str(ctx), '-np', '1', '-ngl', '99', *args]
        if lora:
            self.cmd += ['--lora', str(lora), '--lora-init-without-apply']
        self.lora = bool(lora)
        self.url = f'http://127.0.0.1:{port}'
        self.log = log
        self.p = None

    def __enter__(self):
        out = open(self.log, 'wb') if self.log else subprocess.DEVNULL
        self.p = subprocess.Popen(self.cmd, stdout=out, stderr=subprocess.STDOUT,
                                  creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        t = time.time()
        while time.time() - t < 600:
            if self.p.poll() is not None:
                raise RuntimeError(f'llama-server завершился (код {self.p.returncode}), журнал: {self.log}')
            try:
                with urllib.request.urlopen(self.url + '/health', timeout=5) as r:
                    if r.status == 200:
                        if self.lora:  # как CursorPad: общий вес 0, запросы с чертежом просят 1
                            req = urllib.request.Request(self.url + '/lora-adapters', b'[{"id":0,"scale":0.0}]',
                                                         {'Content-Type': 'application/json'})
                            urllib.request.urlopen(req, timeout=30).read()
                        return self
            except Exception:
                pass
            time.sleep(1)
        raise RuntimeError('llama-server не ответил за 10 минут')

    def __exit__(self, *a):
        if self.p and self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(30)
            except subprocess.TimeoutExpired:
                self.p.kill()

    def ask(self, image, question, schema=None, max_tokens=200, timeout=600):
        """ai_ask_image_sync_ex: картинка и вопрос в одной реплике, температура 0"""
        data = pathlib.Path(image).read_bytes()
        ext = pathlib.Path(image).suffix.lower()
        mime = 'image/bmp' if ext == '.bmp' else 'image/jpeg' if ext in ('.jpg', '.jpeg') else 'image/png'
        body = {'messages': [{'role': 'user', 'content': [
            {'type': 'image_url', 'image_url': {'url': f'data:{mime};base64,' + base64.b64encode(data).decode()}},
            {'type': 'text', 'text': question}]}],
            'temperature': 0, 'max_tokens': max_tokens, 'cache_prompt': False,
            'chat_template_kwargs': {'enable_thinking': False}}
        if schema:
            body['response_format'] = {'type': 'json_schema', 'json_schema': {'name': 'r', 'strict': True,
                                                                              'schema': schema}}
        if self.lora:
            body['lora'] = [{'id': 0, 'scale': 1.0}]
        req = urllib.request.Request(self.url + '/v1/chat/completions', json.dumps(body).encode('utf-8'),
                                     {'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            j = json.loads(r.read())
        return j['choices'][0]['message']['content'] or ''
