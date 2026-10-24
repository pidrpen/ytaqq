#!/usr/bin/env python3
"""Проверка листов сверки нормативов (norms_check.py) на выдуманных таблицах.

Запуск: python -I -B native/test_norms_check.py
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import norms_check  # noqa: E402

GOOD = ("назначение\tтокарная.установка\nисточник\tТестовая книга (выдумано)\nкарта\t1\nстраница\t10\n"
        "столбцы\tмасса\nед\tмин\nсверено\tда 24.10.2026 тест\n"
        "поз.\tспособ\tкак в книге\tдо 1\tдо 3\tдо 5\n"
        "1\tпатрон\tВ патроне\t0,1\t0,2\t0,3\n"
        "2\tцентры\tВ центрах\t0,15\t0,25\t—\n")
# опечатки переноса: «0,8» вместо «0,08», «О,3» буквой, лишняя клетка, повтор позиции
BAD = ("назначение\tтокарная.переход\nкарта\t2\nстраница\t11\nстолбцы\tD\n"
       "поз.\tD\tдо 50\tдо 100\tдо 200\tдо 400\n"
       "1\tдо 50\t0,05\t0,8\t0,1\t0,12\n"
       "2\tсв. 50\tО,3\t0,1\t0,2\t0,3\t9\n"
       "2\tсв. 100\t0,1\t0,2\t0,3\t0,4\n")


def main():
    fails = 0

    def ok(what, cond):
        nonlocal fails
        if not cond:
            fails += 1
            print("FAIL", what)

    with tempfile.TemporaryDirectory() as d:
        norms = os.path.join(d, "Нормативы")
        pages = os.path.join(d, "страницы")
        os.makedirs(norms)
        os.makedirs(pages)
        open(os.path.join(norms, "установка.tsv"), "w", encoding="utf-8").write(GOOD)
        open(os.path.join(norms, "переход.txt"), "wb").write(b"\xff\xfe" + BAD.replace("\n", "\r\n").encode("utf-16-le"))
        open(os.path.join(norms, "ПРОЧТИ.txt"), "w", encoding="utf-8").write("не таблица\n")
        open(os.path.join(pages, "p0012.png"), "wb").write(b"\x89PNG\r\n\x1a\n")  # с. 10 + 2

        head, cols, rows = norms_check.parse(GOOD)
        ok("хорошая таблица без замечаний", norms_check.check(head, cols, rows) == [])
        head, cols, rows = norms_check.parse(norms_check.read_text(os.path.join(norms, "переход.txt")))
        warns = [t for _, _, t in norms_check.check(head, cols, rows)]
        ok("нет источника", any("источник" in t for t in warns))
        ok("нет единицы", any("«ед»" in t for t in warns))
        ok("буква вместо цифры", any("«О,3» — не число" in t for t in warns))
        ok("лишняя клетка", any("клеток 7, а в шапке 6" in t for t in warns))
        ok("повтор позиции", any("поз. 2 повторяется" in t for t in warns))
        ok("скачок в строке", any("поз. 1: значения по строке" in t for t in warns))
        ok("скачок в столбце", any("столбец «до 100»" in t for t in warns))

        ok("запуск", norms_check.main([norms, "--pages", pages, "--offset", "2"]) == 0)
        out = os.path.join(norms, "сверка")
        idx = open(os.path.join(out, "index.html"), encoding="utf-8").read()
        ok("список таблиц", "установка.tsv" in idx and "переход.txt" in idx and "ПРОЧТИ" not in idx)
        s1 = open(os.path.join(out, "установка.html"), encoding="utf-8").read()
        ok("картинка страницы со смещением", "p0012.png" in s1)
        ok("сверено", "да 24.10.2026 тест" in s1)
        s2 = open(os.path.join(out, "переход.html"), encoding="utf-8").read()
        ok("подсветка ошибочной клетки", "class='w'>О,3<" in s2)
        ok("без картинки — подсказка", "Картинки страницы нет" in s2)
    print(f"test_norms_check: {'ошибок ' + str(fails) if fails else 'всё верно'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
