"""Filesystem/OOXML checks for inventory dependencies, collisions and races."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import openpyxl

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    args = parser.parse_args()
    root = args.scratch.resolve()
    root.mkdir(exist_ok=False, parents=True)
    process = subprocess.Popen([str(args.probe.resolve()), str(root)], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, text=True, encoding='utf-8')
    log = []
    def tool(action):
        process.stdin.write((action if isinstance(action, str) else json.dumps(action, ensure_ascii=False)) + '\n')
        process.stdin.flush()
        result = json.loads(process.stdout.readline())
        log.append({'request': action, 'result': result})
        return result
    def file(name, data):
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        return p
    try:
        file('Источник/A.xlsx', b'A')
        file('Источник/B.xlsm', b'B')
        file('Источник/=1+1.xlsx', b'equal-name')
        old = file('Готово/Список_файлов.xlsx', b'old inventory must remain')
        file('Готово/A.xlsx', b'existing file must remain')
        file('Готово/old.txt', b'rename me')
        file('Готово/Подпапка/nested.txt', b'no recursive listing')
        before = hashlib.sha256(old.read_bytes()).hexdigest()
        tool({'goal': 'Создай Excel с перечнем файлов в папке Готово'})
        assert tool({'tool': 'inventory', 'path': '@desktop\\Готово'})['state'] == 0
        assert tool({'tool': 'copy', 'src': '@desktop\\Источник', 'dst': '@desktop\\Готово', 'pattern': '*.xlsx'})['state'] == 0
        # Broader second request fills missing types, rather than duplicating the first batch.
        assert tool({'tool': 'copy', 'src': '@desktop\\Источник', 'dst': '@desktop\\Готово', 'pattern': '@excel'})['state'] == 0
        assert tool({'tool': 'rename', 'path': '@desktop\\Готово\\old.txt', 'name': 'renamed.txt'})['state'] == 0
        done = tool({'tool': 'done'})
        assert done['state'] == 1 and done['plan'][-1]['kind'] == 9
        assert len([a for a in done['plan'] if a['kind'] == 1]) == 3
        assert old.read_bytes() == b'old inventory must remain'
        assert tool(':apply')['state'] == 1
        report_path = Path(done['plan'][-1]['dst'])
        assert report_path.name == 'Список_файлов (2).xlsx'
        values = list(openpyxl.load_workbook(report_path).active.values)
        expected = {'Список_файлов.xlsx', 'A.xlsx', 'renamed.txt', 'Подпапка', 'A (2).xlsx', 'B.xlsm', '=1+1.xlsx'}
        assert {row[0] for row in values[2:]} == expected
        wb = openpyxl.load_workbook(report_path)
        assert all(cell.data_type != 'f' for row in wb.active for cell in row)
        assert hashlib.sha256(old.read_bytes()).hexdigest() == before
        for row in values[2:]:
            p = Path(row[4])
            assert p.exists()
            assert row[3] == ('' if p.is_dir() else str(p.stat().st_size)) or (p.is_dir() and row[3] is None)

        # A change after preview must refuse the whole transfer before touching files.
        file('one.xlsx', b'one')
        tool({'goal': 'На рабочем столе создай папку и закинь туда ексели, создай ексель с перечнем файлов'})
        assert tool({'tool': 'new_folder', 'path': '@desktop'})['state'] == 0
        assert tool({'tool': 'move', 'src': '@desktop', 'dst': '@new', 'pattern': '@excel'})['state'] == 0
        assert tool({'tool': 'inventory', 'path': '@new'})['state'] == 0
        race_plan = tool({'tool': 'done'})
        assert race_plan['state'] == 1
        file('appeared-after-preview.xlsx', b'new file')
        assert tool(':apply')['state'] < 0
        assert not Path(race_plan['new_dir']).exists()
        assert (root / 'one.xlsx').read_bytes() == b'one'

        # A blocked transfer must never produce a misleading completed inventory.
        source = file('Блокировка/locked.xlsx', b'locked fixture')
        tool({'goal': 'Перемести файл и создай Excel с перечнем файлов в папке Результат'})
        assert tool({'tool': 'move', 'src': '@desktop\\Блокировка', 'dst': '@desktop\\Результат', 'pattern': '@excel'})['state'] == 0
        assert tool({'tool': 'inventory', 'path': '@desktop\\Результат'})['state'] == 0
        locked_plan = tool({'tool': 'done'})
        assert locked_plan['state'] == 1
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                                      ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        handle = kernel.CreateFileW(str(source), 0x80000000, 0, None, 3, 0, None)
        assert handle != ctypes.c_void_p(-1).value
        try:
            assert tool(':apply')['state'] < 0
        finally:
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            kernel.CloseHandle(handle)
        assert source.read_bytes() == b'locked fixture'
        assert not Path(locked_plan['plan'][-1]['dst']).exists()
        result = {'pass': True, 'collision_and_existing_data_preserved': True, 'all_rows_present': True,
                  'report_after_copy_and_rename': True, 'partial_group_completed_without_duplicates': True,
                  'formula_filename_is_literal': True, 'source_race_refused': True,
                  'locked_transfer_has_no_inventory': True, 'report_path': str(report_path), 'log': log}
        (root / 'test-report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        print('PASS: inventory dependencies, collisions, all actual rows, formula-looking filename, source race, locked transfer.')
    finally:
        process.stdin.close()
        process.wait(timeout=10)

if __name__ == '__main__':
    main()
