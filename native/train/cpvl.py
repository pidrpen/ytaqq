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
TAGS = ('name', 'mat', 'sheet', 'des', 'spec')  # des — графа «Обозначение», spec — таблица спецификации
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
        st = dump / f'{base}.spec_tess.json'  # строки спецификации, прочитанные Tesseract
        s['spec_tess'] = st.read_text('utf-8-sig') if st.exists() else None
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
    """что модель должна ответить на вопрос tag по разметке листа; None — этого в разметке нет"""
    if tag == 'des':
        return label.get('des')
    if tag == 'spec':
        return json.dumps({'rows': label['rows']}, ensure_ascii=False) if 'rows' in label else None
    if label.get('type') == 'spec':
        return label.get('name') if tag == 'name' else None
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
            w = e.get('where') or ''
            line = f"§AX {n} {e.get('size', '')}" if w == 'по оси' else \
                f"{n} отв. {e.get('size', '')} ({w})" if w else f"{n} отв. {e.get('size', '')}"
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


# ---- обозначение и строки спецификации ----------------------------------------------------

def des_key(s):
    """обозначение для сверки, как его приводит CursorPad (dr_norm_des): латиница → кириллица, запятая → точка
    (рукописные штампы пишут «1249,00,00»), без пробелов, регистр не важен"""
    return re.sub(r'\s+', '', (s or '').translate(_LAT)).upper().replace('Ё', 'Е').replace(',', '.')


def _isdig(s, i):
    return 0 <= i < len(s) and s[i].isdigit()


def _digits(s, i, mn, mx):
    """search_route.c dr_digits: конец цифр с i (их mn…mx) или -1"""
    j = i
    while _isdig(s, j) and j - i < mx:
        j += 1
    return -1 if j - i < mn or _isdig(s, j) else j


def _upper(s, i):
    return 0 <= i < len(s) and ('А' <= s[i] <= 'Я' or s[i] == 'Ё')


def des_core(s, i=0):
    """search_route.c dr_des_core: конец ядра обозначения с позиции i или -1"""
    at = lambda q: s[q] if 0 <= q < len(s) else ''
    j = _digits(s, i, 3, 5)
    if j > 0 and at(j) == '-':
        k, dots = _digits(s, j + 1, 1, 5), 0
        if k < 0:
            return -1
        if at(k) == '-':
            q = k + 1
            if at(q) in 'сС' and at(q + 1) in 'бБ' and at(q) and at(q + 1):
                q += 2
            e = _digits(s, q, 1, 4)
            if e > 0:
                return e
        while at(k) == '.' and _isdig(s, k + 1):
            q = _digits(s, k + 1, 1, 3)
            if q < 0:
                break
            k, dots = q, dots + 1
        return k if dots else -1
    n = 0
    while _upper(s, i + n) and n < 5:
        n += 1
    if n == 4 and at(i + 4) == '.':
        k = _digits(s, i + 5, 6, 6)
        if k > 0 and at(k) == '.':
            return _digits(s, k + 1, 3, 3)
    if n >= 2 and at(i + n) == '.':
        k = _digits(s, i + n + 1, 2, 6)
        while k > 0 and at(k) == '.' and _isdig(s, k + 1):
            k = _digits(s, k + 1, 1, 4)
        if k > 0 and at(k) == '.' and _upper(s, k + 1):
            q = k + 1
            while _upper(s, q) or _isdig(s, q):
                q += 1
            if q - k - 1 <= 4:
                return q
    return -1


def des_fix_sb(s):
    """search_route.c dr_des_fix_sb: «321-23-с632» → «321-23-сб32», «….000С6» → «….000СБ» — как CursorPad
    правит обозначение после распознавания"""
    s = list(s or '')
    for i in range(1, len(s) - 1):
        if s[i] not in 'сС' or s[i + 1] != '6':
            continue
        x = s[i + 2] if i + 2 < len(s) else ''
        if s[i - 1] == '-' and x.isdigit():
            b = i
            while b > 0 and (s[b - 1].isdigit() or s[b - 1] in '-.'):
                b -= 1
            s[i + 1] = 'б' if s[i] == 'с' else 'Б'
            if des_core(''.join(s), b) <= i + 2:
                s[i + 1] = '6'
            continue
        if x not in ('', '.', ' '):
            continue
        e = i - (s[i - 1] == ' ')
        b = e
        while b > 0 and (s[b - 1].isdigit() or s[b - 1] in '-.сб' or _upper(s, b - 1)):
            b -= 1
        if e > 0 and s[e - 1].isdigit() and des_core(''.join(s), b) == e:
            s[i], s[i + 1] = 'С', 'Б'
    return ''.join(s)


def has_sb(s):
    """код документа «СБ» (сборочный чертёж) — только в конце, сразу после номера: «АБВГ.123456.000 СБ»,
    «321-23-сб32СБ». «сб» внутри номера («321-23-сб32») — часть номера: это спецификация (пользователь, 29.10)"""
    k = des_key(s)
    return len(k) > 2 and k.endswith('СБ') and k[-3].isdigit()


SPEC_KEYS = ('sect', 'fmt', 'zone', 'pos', 'des', 'name', 'qty', 'note')  # drawrec.c kDrSpecKey


def spec_rows(ans):
    """строки из ответа {"rows": [...]} (модель или выгрузка Tesseract .spec_tess.json); не JSON — []"""
    try:
        j = json.loads(ans) if isinstance(ans, str) else ans
    except ValueError:
        return []
    rows = (j or {}).get('rows') if isinstance(j, dict) else None
    return [{k: str(r.get(k) or '').strip() for k in SPEC_KEYS} for r in rows or [] if isinstance(r, dict)]


_SECT = ('докум', 'компле', 'сборо', 'детал', 'станд', 'прочи', 'матер')  # drawrec.c dr_section


def section_id(s):
    """номер раздела по началу названия, как dr_section; -1 — не раздел"""
    s = (s or '').strip().lower()
    return next((i for i, p in enumerate(_SECT) if s.startswith(p)), -1)


_DOC_CODES = ('СБ', 'ВО', 'ГЧ', 'МЧ', 'МЭ', 'УЧ', 'ПЗ', 'ТУ', 'ПС', 'РЭ', 'Э3', 'ТЧ', 'ВП')
_DOC_PHRASES = ('сборочный черт', 'чертеж общего', 'габаритный черт', 'монтажный черт', 'схема ', 'пояснительная')


def doc_row(r):
    """drawrec.c dr_spec_doc_row: код документа в конце обозначения или документ по началу наименования"""
    d = des_key(r['des'])
    if any(d.endswith(c) and len(d) > len(c) and d[-len(c) - 1].isdigit() for c in _DOC_CODES):
        return True
    return r['name'].lower().startswith(_DOC_PHRASES)


def spec_post(rows):
    """drawrec.c dr_json_spec: строка из одного заголовка раздела — раздел для строк под ней, строка из одного
    наименования — перенос к предыдущей, «Резерв» и пустые — прочь; документ — в «Документацию» по содержанию,
    строка «Документации», которая не документ, — к заголовку, идущему сразу за ней"""
    out, sect, late = [], '', False
    for r in rows:
        only_name = not any(r[k] for k in ('fmt', 'zone', 'pos', 'des', 'qty', 'note'))
        head = (r['name'] or r['sect']) if only_name else ''
        if head and section_id(head) >= 0:
            if out and late and section_id(head) != 0:
                out[-1] = {**out[-1], 'sect': head}
            late = False
            sect = head
        elif not (r['name'] or not only_name) or (not r['des'] and r['name'].lower().startswith('резерв')):
            pass
        elif only_name and out:
            out[-1] = {**out[-1], 'name': (out[-1]['name'] + ' ' + r['name']).strip()}
        else:
            doc = doc_row(r)
            if doc:
                r = {**r, 'sect': 'Документация'}
            elif not r['sect']:
                r = {**r, 'sect': sect}
            elif section_id(r['sect']) >= 0:
                sect = r['sect']
            late = not doc and section_id(r['sect']) == 0
            out.append(r)
    return out


def _qty(s):
    return (s or '').replace(' ', '').replace('.', ',')


def score_spec(want, got):
    """строки разметки против прочитанных: пара — по обозначению, без него — по наименованию и позиции.
    Счёт: верно целиком, обозначений, количеств, разделов, позиций; сходство наименований; выдумано, пропущено"""
    used, r = set(), dict(n=len(want), ok=0, des_n=0, des_ok=0, qty_ok=0, sect_ok=0, pos_ok=0, sim=0.0, fp=0, fn=0)
    for w in want:
        best, bs = None, 0.0
        for i, g in enumerate(got):
            if i in used:
                continue
            if w['des'] and des_key(g['des']) == des_key(w['des']):
                best, bs = i, 2.0
                break
            s = similar(g['name'], w['name']) + (0.2 if g['pos'] == w['pos'] else 0)
            if s > bs and s >= 0.75:
                best, bs = i, s
        if best is None:
            r['fn'] += 1
            if w['des']:
                r['des_n'] += 1
            continue
        used.add(best)
        g = got[best]
        d_ok = not w['des'] or des_key(g['des']) == des_key(w['des'])
        q_ok, s_ok, p_ok = _qty(g['qty']) == _qty(w['qty']), section_id(g['sect']) == section_id(w['sect']), g['pos'] == w['pos']
        sim = similar(g['name'], w['name'])
        r['des_n'] += bool(w['des'])
        r['des_ok'] += bool(w['des']) and d_ok
        r['qty_ok'] += q_ok
        r['sect_ok'] += s_ok
        r['pos_ok'] += p_ok
        r['sim'] += sim
        r['ok'] += d_ok and q_ok and s_ok and p_ok and sim >= 0.9
    r['fp'] = len(got) - len(used)
    return r


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
        # lora — путь или список путей; номер адаптера — порядок, как у CursorPad (ai_local.c)
        loras = [x for x in (lora if isinstance(lora, (list, tuple)) else [lora]) if x]
        for x in loras:
            self.cmd += ['--lora', str(x)]
        if loras:
            self.cmd += ['--lora-init-without-apply']
        self.lora = len(loras)
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
                            zero = json.dumps([{'id': i, 'scale': 0.0} for i in range(self.lora)]).encode()
                            req = urllib.request.Request(self.url + '/lora-adapters', zero,
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

    def ask(self, image, question, schema=None, max_tokens=200, timeout=600, lora_id=0):
        """ai_ask_image_sync_ex: картинка и вопрос в одной реплике, температура 0; lora_id — какой
        адаптер с весом 1 (остальные — 0)"""
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
            body['lora'] = [{'id': lora_id, 'scale': 1.0}]
        req = urllib.request.Request(self.url + '/v1/chat/completions', json.dumps(body).encode('utf-8'),
                                     {'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            j = json.loads(r.read())
        return j['choices'][0]['message']['content'] or ''
