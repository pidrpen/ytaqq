"""Мост cp-soyuz.exe (CursorPad → клиент «Союза», зашит в CursorPad.exe ресурсом 312) —
готовый exe в репозитории: CI его не собирает (нужны сборки клиента «Союза» и csc .NET
Framework). Чтобы в выпуск не попал подменённый или случайно переписанный файл, его
sha256 записан в native/cp-soyuz.sha256 и сверяется при каждой сборке (build.sh), как
комплекты QwenPaw и tools в package_release.py.

  python native/check_bridge.py          — сверить; не сошлось — код 1 и понятная ошибка
  python native/check_bridge.py --write  — записать хеш заново (после пересборки
                                           native/build-bridge.ps1, осознанно)
"""
import hashlib
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
EXE = HERE / 'cp-soyuz.exe'
LOCK = HERE / 'cp-soyuz.sha256'


def main():
    if not EXE.exists():
        print(f'нет {EXE.name} — мост «Союза» должен лежать в native/ (пересборка: native/build-bridge.ps1)')
        return 1
    sha = hashlib.sha256(EXE.read_bytes()).hexdigest()
    if '--write' in sys.argv[1:]:
        LOCK.write_text(f'{sha}  {EXE.name}\n', encoding='utf-8')
        print(f'{LOCK.name}: {sha} ({EXE.stat().st_size} байт)')
        return 0
    want = LOCK.read_text(encoding='utf-8').split()[0] if LOCK.exists() else ''
    if sha != want:
        print(f'{EXE.name} не соответствует записанному хешу ({LOCK.name}): '
              f'есть {sha[:16]}…, записан {want[:16] or "—"}…. Пересоберите мост по native/build-bridge.ps1 '
              f'и, если это ваша осознанная замена, запишите хеш: python native/check_bridge.py --write')
        return 1
    print(f'{EXE.name}: хеш сходится ({EXE.stat().st_size} байт)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
