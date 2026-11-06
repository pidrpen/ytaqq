"""Шаблон Excel для окна «ЭСИ и ТП» (с 2026.11.06): листы как у пакета (kPkSheets в export.c) —
Изделия, Состав, Операции, Заготовки, шапка в первой строке, — плюс «Как заполнять» и скрытый
«Списки» (выбор вида изделия и операции 1С списком в ячейке). Загрузчик (pk_xlsx.c) листы
«Как заполнять» и «Списки» пропускает.

    python make_esi_template.py                       -> esi_tp_shablon.xlsx (пустой, в exe — RCDATA 315)
    python make_esi_template.py --example primer.xlsx -> с примером: сборка, детали, операции, заготовки

Нужен openpyxl. Операции — из ops_1c.h (как в окне), виды — как kPpKinds в pk_paste.c."""

import datetime
import os
import re
import sys
import zipfile

from openpyxl import Workbook
from openpyxl.comments import Comment
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.worksheet.datavalidation import DataValidation

HERE = os.path.dirname(os.path.abspath(__file__))

KINDS = ["Сборочные единицы", "Детали", "Детали БЧ", "Стандартные изделия", "Прочие изделия", "Материалы",
         "Комплекты", "Комплексы", "Комплекты разборки", "Документация"]

# шапки — ровно как kPkSheets (export.c): по ним загрузчик узнаёт листы и столбцы
SHEETS = [
    ("Изделия", ["Обозначение", "Наименование", "Вид изделия", "Материал", "Масса, кг", "Чертёж (файл)",
                 "Признак покупки"], [26, 34, 20, 46, 10, 34, 24]),
    ("Состав", ["Сборка", "Входящее", "Кол-во", "Позиция"], [26, 36, 9, 9]),
    ("Операции", ["Обозначение", "№ опер.", "Операция", "Участок", "Оборудование", "Тпз, мин", "Тшт, мин",
                  "Профессия"], [26, 9, 30, 26, 34, 9, 9, 24]),
    ("Заготовки", ["Деталь", "Материал заготовки", "Норма, кг", "Размеры, мм"], [26, 48, 10, 20]),
]

NOTES = {
    ("Изделия", "Обозначение"): "Обязательно, как в штампе: АБВГ.123456.002.\nУ стандартных и прочих изделий — "
                                "наименование целиком: «Болт М10-6gx30.58.019 ГОСТ 7798-70».",
    ("Изделия", "Вид изделия"): "Выбор списком. Пусто — «Детали» (у сборки из «Состава» — «Сборочные единицы»).",
    ("Изделия", "Материал"): "Как в сортаменте «Союза» целиком:\nКруг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013",
    ("Изделия", "Чертёж (файл)"): "Путь к файлу или только имя файла — тогда он ищется рядом с этой книгой.\n"
                                  "Ляжет к изделию подлинником.",
    ("Изделия", "Признак покупки"): "Пусто — «Собственного изготовления». Иначе — как в «Союзе» (Покупное…).",
    ("Состав", "Сборка"): "Обозначение сборки. Пусто — та же, что строкой выше.",
    ("Состав", "Кол-во"): "В штуках. Пусто — 1.",
    ("Операции", "Обозначение"): "Деталь. Пусто — та же, что строкой выше.",
    ("Операции", "№ опер."): "Порядок операций. Номера станут 005, 010… сами; пусто — по порядку строк.",
    ("Операции", "Операция"): "Выбор списком (код 1С и название). Можно и просто «Токарная» или «4110» — "
                              "код подставится.",
    ("Операции", "Участок"): "Пусто — подберётся по ТП завода (нужен вход в базу «Союза»).",
    ("Операции", "Оборудование"): "Пусто — подберётся по операции и участку.",
    ("Операции", "Тпз, мин"): "В минутах. Пусто — посчитается по таблицам ОМН, где можно.",
    ("Операции", "Профессия"): "Пусто — подберётся по операции.",
    ("Заготовки", "Материал заготовки"): "Как в сортаменте «Союза» целиком — иначе заготовка не создастся.",
    ("Заготовки", "Размеры, мм"): "D=40;L=265 — круг, S=10;B=280;L=440 — лист. «Ø40x265», «10х280х440» тоже поймёт.",
}

HELP = [
    ("ЭСИ и ТП — шаблон для загрузки в CursorPad", "title"),
    ("", None),
    ("Как загрузить", "head"),
    ("Окно «ЭСИ и ТП» → кнопка «Из Excel…» (или просто перетащите эту книгу мышью в окно).", None),
    ("Строки лягут по листам окна. Пустые участок, станок, профессия и нормы Тпз/Тшт подставятся сами.", None),
    ("Дальше, как обычно: «Проверить» → «В Союз…».", None),
    ("", None),
    ("Листы", "head"),
    ("Изделия — что заводить в «Союзе»: обозначение, наименование, вид, материал, масса, файл чертежа.", None),
    ("Состав — что во что входит: сборка, входящее, количество, позиция.", None),
    ("Операции — техпроцесс детали: операции по порядку, участок, станок, нормы, профессия.", None),
    ("Заготовки — материал заготовки детали, норма расхода, размеры.", None),
    ("", None),
    ("Правила", "head"),
    ("Первая строка листа — шапка: названия столбцов не меняйте (порядок можно, лишние столбцы пропускаются).", None),
    ("Подсказка по столбцу — в примечании к его шапке (наведите мышь на красный уголок).", None),
    ("В «Составе» и «Операциях» пустое обозначение — то же, что строкой выше: пишите сборку и деталь один раз.", None),
    ("Лишний лист или своя таблица тоже загрузится: столбцы найдутся по шапке («Обозн.», «Кол.», «№ опер.», "
     "«Станок», «Тшт, ч»…), как при «Вставить».", None),
    ("Чего нет в справочниках «Союза» (материал, участок, станок), «Проверить» покажет до отправки.", None),
]


def ops_list():
    names = []
    with open(os.path.join(HERE, "ops_1c.h"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r'\s*\{L"(\d{4})", L"[^"]*", L"([^"]*)"', line)
            if m:
                names.append(m.group(2))
    return names


EXAMPLE = {
    "Изделия": [
        ["АБВГ.301000.000", "Опора", "Сборочные единицы", "", "4,1", "", ""],
        ["АБВГ.301000.001", "Основание", "Детали", "Лист 10 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89", "2,4", "", ""],
        ["АБВГ.301000.002", "Стойка", "Детали", "Круг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013", "1,1", "", ""],
        ["АБВГ.301000.003", "Втулка", "Детали", "Круг 30 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013", "0,2", "", ""],
        ["Болт М10-6gx30.58.019 ГОСТ 7798-70", "", "Стандартные изделия", "", "", "", ""],
        ["Шайба 10 ГОСТ 11371-78", "", "Стандартные изделия", "", "", "", ""],
    ],
    "Состав": [
        ["АБВГ.301000.000", "АБВГ.301000.001", 1, 1],
        ["", "АБВГ.301000.002", 1, 2],
        ["", "АБВГ.301000.003", 2, 3],
        ["", "Болт М10-6gx30.58.019 ГОСТ 7798-70", 4, 4],
        ["", "Шайба 10 ГОСТ 11371-78", 4, 5],
    ],
    "Операции": [
        ["АБВГ.301000.001", "005", "4280 Отрезная", "", "", "", "", ""],
        ["", "010", "4260 Фрезерная", "", "", "", "", ""],
        ["", "015", "4210 Сверлильная", "", "", "", "", ""],
        ["", "020", "0108 Слесарная", "", "", "", "", ""],
        ["", "025", "0200 Контроль", "", "", "", "", ""],
        ["АБВГ.301000.002", "005", "4280 Отрезная", "", "", "", "", ""],
        ["", "010", "4110 Токарная", "", "", "12", "6,5", ""],
        ["", "015", "4210 Сверлильная", "", "", "", "", ""],
        ["", "020", "0200 Контроль", "", "", "", "", ""],
        ["АБВГ.301000.003", "005", "Токарная", "", "", "", "", ""],
        ["", "010", "0200", "", "", "", "", ""],
    ],
    "Заготовки": [
        ["АБВГ.301000.001", "Лист 10 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89", "2,9", "S=10;B=160;L=230"],
        ["АБВГ.301000.002", "Круг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013", "1,6", "D=40;L=165"],
        ["АБВГ.301000.003", "Круг 30 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013", "0,35", "Ø30x62"],
    ],
}


def build(path, example):
    wb = Workbook()
    fixed = datetime.datetime(2026, 11, 6, 9, 0, 0)
    wb.properties.creator = "CursorPad"
    wb.properties.created = fixed
    wb.properties.modified = fixed
    head_font = Font(bold=True, color="FFFFFF")
    head_fill = PatternFill("solid", fgColor="2F5D8A")
    thin = Side(style="thin", color="B7C3D0")
    border = Border(left=thin, right=thin, top=thin, bottom=thin)

    hs = wb.active
    hs.title = "Как заполнять"
    hs.column_dimensions["A"].width = 120
    for i, (text, kind) in enumerate(HELP, start=1):
        c = hs.cell(row=i, column=1, value=text or None)
        if kind == "title":
            c.font = Font(bold=True, size=14, color="2F5D8A")
        elif kind == "head":
            c.font = Font(bold=True, size=11)
        c.alignment = Alignment(wrap_text=True, vertical="top")
    hs.sheet_view.showGridLines = False

    lists = wb.create_sheet("Списки")
    for i, k in enumerate(KINDS, start=1):
        lists.cell(row=i, column=1, value=k)
    ops = ops_list()
    for i, o in enumerate(ops, start=1):
        lists.cell(row=i, column=2, value=o)
    lists.sheet_state = "hidden"

    for name, heads, widths in SHEETS:
        ws = wb.create_sheet(name)
        for c, (h, w) in enumerate(zip(heads, widths), start=1):
            cell = ws.cell(row=1, column=c, value=h)
            cell.font = head_font
            cell.fill = head_fill
            cell.border = border
            cell.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
            ws.column_dimensions[cell.column_letter].width = w
            note = NOTES.get((name, h))
            if note:
                cm = Comment(note, "CursorPad")
                cm.width = 320
                cm.height = 110
                cell.comment = cm
        ws.row_dimensions[1].height = 30
        ws.freeze_panes = "A2"
        last = 2000
        # столбцы — текстом: «005» и «АБВГ.123456.002» Excel не превратит в число или дату
        for c in range(1, len(heads) + 1):
            ws.column_dimensions[ws.cell(row=1, column=c).column_letter].number_format = "@"
        if name == "Изделия":
            dv = DataValidation(type="list", formula1=f"=Списки!$A$1:$A${len(KINDS)}", allow_blank=True,
                                showErrorMessage=False)
            dv.add(f"C2:C{last}")
            ws.add_data_validation(dv)
        if name == "Операции":
            dv = DataValidation(type="list", formula1=f"=Списки!$B$1:$B${len(ops)}", allow_blank=True,
                                showErrorMessage=False)
            dv.add(f"C2:C{last}")
            ws.add_data_validation(dv)
        if example:
            for r, row in enumerate(EXAMPLE[name], start=2):
                for c, v in enumerate(row, start=1):
                    if v != "":
                        ws.cell(row=r, column=c, value=v)
    wb.active = 0
    wb.save(path)
    _fix_zip_time(path, fixed)


def _fix_zip_time(path, when):
    """Время файлов в zip — постоянное: шаблон собирается заново байт в байт (не меняется в git зря)."""
    with zipfile.ZipFile(path) as z:
        items = [(i.filename, z.read(i.filename)) for i in z.infolist()]
    stamp = when.strftime("%Y-%m-%dT%H:%M:%SZ").encode()
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in items:
            if name == "docProps/core.xml":  # openpyxl при сохранении ставит «изменён» — сейчас
                data = re.sub(rb"(<dcterms:modified[^>]*>)[^<]*", lambda m: m.group(1) + stamp, data)
            z.writestr(zipfile.ZipInfo(name, date_time=when.timetuple()[:6]), data, zipfile.ZIP_DEFLATED)


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--example":
        build(sys.argv[2], True)
        print("пример:", sys.argv[2])
        return
    out = os.path.join(HERE, "esi_tp_shablon.xlsx")
    build(out, False)
    print("шаблон:", out, os.path.getsize(out), "байт")


if __name__ == "__main__":
    main()
