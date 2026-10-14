"""Собирает конфигурацию Союз ПЛМ (.pmszcfg) из скриптов в ../scripts.

За основу берётся template.pmszcfg (выгрузка конфигурации «Работа с параметрами
изделия» из pidrpen/cursor/pmszcfg): из неё берутся структура пакета, место
в дереве конфигураций (ZUM > Конструкторская часть) и служебные поля. Название,
идентификаторы и скриптовые функции заменяются на свои.

Запуск: python3 build_pmszcfg.py
"""
import copy, datetime, os, struct, sys, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nrbf

NAME_KEY = 'ZumEsiAndMaterials'
NAME_UI = 'Автозагрузка ЭСИ и выгрузка материалов'
VERSION = '1.0.0.0'
OUT = os.path.join(HERE, '..', f'{NAME_UI}-{VERSION}.pmszcfg')

# Kind: 1 автоматическое действие, 4 команда, 5 серверная функция (как в ваших конфигурациях)
TASKS = [
    dict(key='ZumEsiAutoLoadServer', ui='Автозагрузка ЭСИ (сервер)', kind=5, flags=0),
    dict(key='ZumEsiAutoLoad', ui='Автозагрузка ЭСИ по расписанию', kind=1, flags=1,
         repeat_kind=3, mask=7, next_run=datetime.datetime(2026, 10, 15, 22, 0)),
    dict(key='ZumEsiLoadNow', ui='Загрузить ЭСИ сейчас', kind=4, flags=5),
    dict(key='ZumExportMaterialsServer', ui='Выгрузка материалов (сервер)', kind=5, flags=0),
    dict(key='ZumExportMaterials', ui='Выгрузка материалов (наименование, обозначение)', kind=4, flags=5),
]


def ticks(dt, kind=0):
    t = (dt - datetime.datetime(1, 1, 1)) // datetime.timedelta(microseconds=1) * 10
    return t | (kind << 62)


class Builder:
    def __init__(self, path):
        self.doc = nrbf.Doc(open(path, 'rb').read())
        self.objs = {}
        for r in self.doc.records:
            self.index(r)
        self.next_id = max(abs(i) for i in self.objs) + 1

    def index(self, v):
        if not isinstance(v, dict):
            return
        if v['t'] in (4, 5):
            self.objs[v['ci']['id']] = v
        elif 'id' in v and v['t'] != 12:
            self.objs[v['id']] = v
        if isinstance(v.get('vals'), list):
            for x in v['vals']:
                self.index(x)

    def cls(self, o):
        return self.doc.meta[o['mid']][0] if o['t'] == 1 else o['ci']

    def members(self, o):
        return dict(zip(self.cls(o)['members'], range(len(o['vals']))))

    def of_class(self, name):
        return [o for o in self.objs.values() if o['t'] in (1, 4, 5) and self.cls(o)['name'] == name]

    def fresh(self, sign=1):
        self.next_id += 1
        return sign * self.next_id

    def set_str(self, o, member, s):
        i = self.members(o)[member]
        o['vals'][i] = {'t': 10} if s is None else {'t': 6, 'id': self.fresh(), 's': s}

    def set_prim(self, o, member, v):
        x = o['vals'][self.members(o)[member]]
        assert x['t'] == 'prim', member
        x['v'] = v

    def set_enum(self, o, member, v):
        x = o['vals'][self.members(o)[member]]
        assert x['t'] in (1, 4, 5) and len(x['vals']) == 1, member
        x['vals'][0]['v'] = v

    def set_guid(self, o, member, g):
        x = o['vals'][self.members(o)[member]]
        assert self.cls(x)['name'] == 'System.Guid', member
        for slot, v in zip(x['vals'], struct.unpack('<ihhBBBBBBBB', g.bytes_le)):
            slot['v'] = v

    def renumber(self, v):
        """Новые id всем объектам внутри клона (ссылки наружу не трогаем)."""
        if not isinstance(v, dict):
            return
        if v['t'] in (4, 5):
            raise ValueError('clone must use ClassWithId records')
        if 'id' in v and v['t'] != 12:
            v['id'] = self.fresh(-1 if v['id'] < 0 else 1)
        if isinstance(v.get('vals'), list):
            for x in v['vals']:
                self.renumber(x)


def main():
    b = Builder(os.path.join(HERE, 'template.pmszcfg'))
    configs = b.of_class('ProgramSoyuz.DataModel.ConfigurationData')
    own = [c for c in configs if c['vals'][b.members(c)['NameKey']].get('s') == 'WorkProductParametrs']
    assert len(own) == 1
    cfg = own[0]
    cfg_guid = uuid.uuid4()
    b.set_str(cfg, 'NameKey', NAME_KEY)
    b.set_str(cfg, 'NameUI', NAME_UI)
    b.set_str(cfg, 'Version', VERSION)
    b.set_str(cfg, 'Description', 'Автозагрузка ЭСИ из папки и выгрузка материалов с наименованием и обозначением')
    b.set_prim(cfg, 'DateModified', ticks(datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None), 1))
    b.set_guid(cfg, 'GlobalIdentifier', cfg_guid)

    tasks = b.of_class('ProgramSoyuz.DataModel.ScriptingTaskData')
    first = [t for t in tasks if t['t'] == 5][0]
    proto = [t for t in tasks if t['t'] == 1][0]
    # Пустая иконка, как у второй функции шаблона
    icon_none = copy.deepcopy(proto['vals'][b.members(proto)['IconId']])
    holders = [first, proto]
    while len(holders) < len(TASKS):
        clone = copy.deepcopy(proto)
        b.renumber(clone)
        b.doc.records.insert(len(b.doc.records) - 1, clone)
        holders.append(clone)

    task_array = [r for r in b.objs.values() if r['t'] == 7 and r.get('ea') == ('ProgramSoyuz.DataModel.ScriptingTaskData', 3)][0]
    task_array['vals'] = [{'t': 9, 'ref': (h['ci']['id'] if h['t'] == 5 else h['id'])} for h in holders]
    task_array['lens'] = [len(holders)]

    scripts = os.path.join(HERE, '..', 'scripts')
    for h, spec in zip(holders, TASKS):
        code = open(os.path.join(scripts, spec['key'] + '.cs'), encoding='utf-8').read()
        code = code.replace('\r\n', '\n').replace('\n', '\r\n')
        b.set_str(h, 'NameKey', spec['key'])
        b.set_str(h, 'NameUI', spec['ui'])
        b.set_str(h, 'ClassName', None)
        b.set_str(h, 'CustomizationCode', code)
        b.set_enum(h, 'Kind', spec['kind'])
        b.set_enum(h, 'RepeatKind', spec.get('repeat_kind', 0))
        b.set_prim(h, 'NextRunDate', ticks(spec['next_run']) if 'next_run' in spec else 0)
        b.set_enum(h, 'Flags', spec['flags'])
        b.set_prim(h, 'TargetSystemPropsMask', spec.get('mask', 0))
        h['vals'][b.members(h)['IconId']] = copy.deepcopy(icon_none)
        b.renumber(h['vals'][b.members(h)['IconId']])
        b.set_guid(h, 'ParentConfigurationId', cfg_guid)
        b.set_guid(h, 'ParentConfigurationInEffectId', cfg_guid)
        b.set_guid(h, 'GlobalIdentifier', uuid.uuid4())

    data = b.doc.dump()
    check = nrbf.Doc(data)
    assert check.dump() == data
    ids, refs = set(), []

    def walk(v):
        if not isinstance(v, dict):
            return
        if v['t'] in (4, 5):
            ids.add(v['ci']['id'])
        elif 'id' in v and v['t'] != 12:
            assert v['id'] not in ids, f"duplicate id {v['id']}"
            ids.add(v['id'])
        if v['t'] == 9:
            refs.append(v['ref'])
        if isinstance(v.get('vals'), list):
            for x in v['vals']:
                walk(x)
    for r in check.records:
        walk(r)
    missing = [r for r in refs if r not in ids]
    assert not missing, f'dangling refs {missing}'
    with open(OUT, 'wb') as f:
        f.write(data)
    print(os.path.normpath(OUT), len(data))


if __name__ == '__main__':
    main()
