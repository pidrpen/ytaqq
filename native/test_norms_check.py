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

# режимы резания: диапазон «от – до» и таблица коэффициентов формулы с отрицательным показателем
RNG = ("назначение\tтокарная.подача\nисточник\tМетодичка (выдумано)\nтаблица\t3\nссылка\thttps://example.org/\n"
       "столбцы\tt\nед\tмм/об\nпоз.\tD\tдо 3\tдо 5\n1\tдо 20\t0,3–0,4\t—\n2\tсв. 20\t0,4–0,5\t0,3-0,4\n")
FLD = ("назначение\tтокарная.скорость.nv\nисточник\tМетодичка (выдумано)\nссылка\thttps://example.org/\nед\tкоэф\n"
       "поля\tnv; база\nпоз.\tматериал\tσв\tnv\tбаза\n1\tсталь\tдо 450\t-1,0\t750\n2\tсталь\tсв. 450\t1,75\t750\n")


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
        ok("скачок в строке", any("поз. 1, «до 100»: 0,8 выбивается из соседних по строке" in t for t in warns))
        ok("скачок в столбце", any("«до 100»: 0,8 выбивается из соседних по столбцу" in t for t in warns))
        ok("диапазон — середина", norms_check.num("0,4–0,5") == 0.45 and norms_check.num("-0,9") == -0.9)
        ok("прочерк и мусор — не число", norms_check.num("-") is None and norms_check.num("-0,3-0,4") is None)
        head, cols, rows = norms_check.parse(RNG)
        ok("таблица с диапазонами без замечаний", norms_check.check(head, cols, rows) == [])
        head, cols, rows = norms_check.parse(FLD)
        ok("таблица полей без замечаний", norms_check.check(head, cols, rows) == [])
        ok("поля — столбцы значений", norms_check.layout(head, cols)[2] == [3, 4])

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
