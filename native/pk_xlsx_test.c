/* Проверка загрузки книги Excel в окно «ЭСИ и ТП» (pk_xlsx.c) без Windows:
   gcc -O2 -Wall -Werror -o pk_xlsx_test pk_xlsx_test.c -lm && ./pk_xlsx_test   (-v — показать строки)
   Книга собирается здесь же (zip «store», листы с inlineStr и числами): шаблон с листами пакета в другом
   порядке столбцов и с пропусками «как выше», служебные листы, своя спецификация с шапкой и заголовком,
   лист операций без обозначения — к выбранной позиции. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "search_route.c"
#ifndef OPS_1C_INCLUDED
#define OPS_1C_INCLUDED
#include "ops_1c.h"
#endif
#include "techno.c"
#include "soyuz_ops.c"
typedef struct { /* как в export.c */
  const wchar_t *name;
  int ncols;
  const wchar_t *const *head;
  const int *width;
} XpSheetDef;
#define XP_N(a) ((int)(sizeof(a) / sizeof(a[0])))
#include "pk_sheets.h"
#include "pk_paste.c"
#include "xlsx_read.c"
#include "pk_xlsx.c"

static void out8(const wchar_t *w) {
  for (; w && *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) putchar(0xC0 | c >> 6), putchar(0x80 | (c & 63));
    else putchar(0xE0 | c >> 12), putchar(0x80 | ((c >> 6) & 63)), putchar(0x80 | (c & 63));
  }
}

static int g_fail, g_n, g_verbose;
static const char *g_case;
static wchar_t g_got[40000];
static const wchar_t *const kSheet[] = {L"И", L"С", L"О", L"З"};

static void put(void *ctx, int sheet, const wchar_t *const *c, int n) {
  (void)ctx;
  int last = n - 1;
  while (last > 0 && !c[last][0]) last--;
  wcscat(g_got, kSheet[sheet]);
  for (int i = 0; i <= last; i++) wcscat(g_got, L"|"), wcscat(g_got, c[i]);
  wcscat(g_got, L"\n");
}

static void has(const wchar_t *want) {
  g_n++;
  wchar_t w[800];
  swprintf(w, 800, L"%ls\n", want);
  if (!wcsstr(g_got, w)) {
    g_fail++;
    printf("FAIL %s: нет строки «", g_case);
    out8(want);
    printf("»\n  легло:\n");
    out8(g_got);
  }
}

static void hasnt(const wchar_t *part) {
  g_n++;
  if (wcsstr(g_got, part)) {
    g_fail++;
    printf("FAIL %s: лишнее «", g_case);
    out8(part);
    printf("»\n");
  }
}

static void ok(int cond, const char *what) {
  g_n++;
  if (!cond) g_fail++, printf("FAIL %s: %s\n", g_case, what);
}

/* ---- книга в памяти ---- */

typedef struct {
  unsigned char *p;
  size_t n;
} Buf;

static void bput(Buf *b, const void *d, size_t n) {
  b->p = (unsigned char *)realloc(b->p, b->n + n + 1);
  memcpy(b->p + b->n, d, n);
  b->n += n;
  b->p[b->n] = 0;
}
static void bstr(Buf *b, const char *s) { bput(b, s, strlen(s)); }
static void u16(Buf *b, unsigned v) {
  unsigned char c[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
  bput(b, c, 2);
}
static void u32(Buf *b, unsigned long v) {
  unsigned char c[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
  bput(b, c, 4);
}

/* лист: строки через «\n», ячейки через «|»; «#5» — число 5 */
static char *sheet_xml(const char *rows) {
  Buf b = {0};
  bstr(&b, "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>");
  int r = 1;
  for (const char *p = rows; *p; r++) {
    const char *e = strchr(p, '\n');
    if (!e) e = p + strlen(p);
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "<row r=\"%d\">", r);
    bstr(&b, tmp);
    int c = 0;
    for (const char *q = p; q <= e; c++) {
      const char *ce = q;
      while (ce < e && *ce != '|') ce++;
      if (ce > q) {
        char ref[16];
        snprintf(ref, sizeof(ref), "%c%d", 'A' + c, r);
        if (*q == '#') {
          snprintf(tmp, sizeof(tmp), "<c r=\"%s\"><v>", ref);
          bstr(&b, tmp);
          bput(&b, q + 1, (size_t)(ce - q - 1));
          bstr(&b, "</v></c>");
        } else {
          snprintf(tmp, sizeof(tmp), "<c r=\"%s\" t=\"inlineStr\"><is><t>", ref);
          bstr(&b, tmp);
          for (const char *x = q; x < ce; x++)
            if (*x == '&') bstr(&b, "&amp;");
            else if (*x == '<') bstr(&b, "&lt;");
            else bput(&b, x, 1);
          bstr(&b, "</t></is></c>");
        }
      }
      q = ce + 1;
    }
    bstr(&b, "</row>");
    p = *e ? e + 1 : e;
  }
  bstr(&b, "</sheetData></worksheet>");
  return (char *)b.p;
}

/* книга: имена листов (у скрытого — «!» впереди) и их строки */
static Buf book(const char *const *names, const char *const *rows, int n) {
  const char *fn[40];
  char *data[40];
  int nf = 0;
  Buf wb = {0}, rel = {0};
  bstr(&wb, "<workbook xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>");
  bstr(&rel, "<Relationships>");
  static char pathbuf[20][40];
  for (int i = 0; i < n; i++) {
    char tmp[300];
    int hidden = names[i][0] == '!';
    snprintf(tmp, sizeof(tmp), "<sheet name=\"%s\" sheetId=\"%d\"%s r:id=\"rId%d\"/>", names[i] + hidden, i + 1,
             hidden ? " state=\"hidden\"" : "", i + 1);
    bstr(&wb, tmp);
    snprintf(tmp, sizeof(tmp), "<Relationship Id=\"rId%d\" Target=\"worksheets/s%d.xml\"/>", i + 1, i + 1);
    bstr(&rel, tmp);
    snprintf(pathbuf[i], 40, "xl/worksheets/s%d.xml", i + 1);
    fn[nf] = pathbuf[i];
    data[nf++] = sheet_xml(rows[i]);
  }
  bstr(&wb, "</sheets></workbook>");
  bstr(&rel, "</Relationships>");
  fn[nf] = "xl/workbook.xml", data[nf++] = (char *)wb.p;
  fn[nf] = "xl/_rels/workbook.xml.rels", data[nf++] = (char *)rel.p;
  Buf z = {0}, cd = {0};
  for (int i = 0; i < nf; i++) {
    size_t off = z.n, nl = strlen(fn[i]), dl = strlen(data[i]);
    u32(&z, 0x04034b50), u16(&z, 20), u16(&z, 0), u16(&z, 0), u16(&z, 0), u16(&z, 0), u32(&z, 0), u32(&z, dl), u32(&z, dl);
    u16(&z, (unsigned)nl), u16(&z, 0), bput(&z, fn[i], nl), bput(&z, data[i], dl);
    u32(&cd, 0x02014b50), u16(&cd, 20), u16(&cd, 20), u16(&cd, 0), u16(&cd, 0), u16(&cd, 0), u16(&cd, 0), u32(&cd, 0);
    u32(&cd, dl), u32(&cd, dl), u16(&cd, (unsigned)nl), u16(&cd, 0), u16(&cd, 0), u16(&cd, 0), u16(&cd, 0), u32(&cd, 0);
    u32(&cd, off), bput(&cd, fn[i], nl);
    free(data[i]);
  }
  size_t cdo = z.n;
  bput(&z, cd.p, cd.n);
  u32(&z, 0x06054b50), u16(&z, 0), u16(&z, 0), u16(&z, (unsigned)nf), u16(&z, (unsigned)nf), u32(&z, cd.n), u32(&z, cdo),
      u16(&z, 0);
  free(cd.p);
  return z;
}

static PkxOut g_o;

static int run(const char *name, const char *const *names, const char *const *rows, int n, const wchar_t *asmb,
               const wchar_t *part) {
  g_case = name;
  g_got[0] = 0;
  Buf z = book(names, rows, n);
  XrBook *b = (XrBook *)calloc(1, sizeof(XrBook));
  wchar_t why[200] = L"";
  int got = 0;
  if (!xr_read(z.p, z.n, b, why, 200)) {
    g_fail++, printf("FAIL %s: книга не прочиталась: ", name), out8(why), printf("\n");
  } else {
    got = pkx_apply(b, L"D:\\Заказ 17", asmb, part, put, NULL, &g_o);
  }
  if (g_verbose) {
    printf("== %s: строк %d; листы: ", name, got), out8(g_o.used), printf("; мимо: "), out8(g_o.passed), printf("\n");
    out8(g_got);
  }
  xr_free(b);
  free(b);
  free(z.p);
  return got;
}

int main(int argc, char **argv) {
  g_verbose = argc > 1 && !strcmp(argv[1], "-v");
  (void)sr_route, (void)sr_score, (void)sr_db_words, (void)sr_words;
  (void)so_min, (void)so_match, (void)so_left;
  (void)pp_file_title_code, (void)pp_doc_title, (void)tn_text, (void)tn_route; /* здесь не проверяются */

  /* шаблон: служебные листы, столбцы не по порядку, лишний столбец, «как выше», коды операций */
  {
    const char *names[] = {"Как заполнять", "!Списки", "Изделия", "Состав", "Операции", "Заготовки", "Пустой"};
    const char *rows[] = {
        "ЭСИ и ТП — шаблон для загрузки в CursorPad\nОбозначение|Наименование|Вид изделия\nПервая строка листа — шапка",
        "Сборочные единицы|0000 Списание материалов\nДетали|0101 Разметка",
        "Наименование|Обозначение|Вид изделия|Масса, кг|Материал|Примечание|Чертёж (файл)|Признак покупки\n"
        "Опора|АБВГ.301000.000|Сборочные единицы|#4.1\n"
        "Основание|АБВГ.301000.001|деталь|#2.4|Лист 10 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89|чертёж в архиве|АБВГ.301000.001.pdf\n"
        "Стойка|АБВГ.301000.002|Детали|#1.1000000000000001||||Покупное\n"
        "|Болт М10-6gx30.58.019 ГОСТ 7798-70|Стандартные изделия\n"
        "Шайба 10 ГОСТ 11371-78||Стандартные изделия\n"
        "Без обозначения\n"
        "Кронштейн|АБВГ.301000.004||||||\n"
        "|АБВГ.301000.005|||||Z:\\Архив\\АБВГ.301000.005.tif",
        "Сборка|Входящее|Кол-во|Позиция\n"
        "АБВГ.301000.000|АБВГ.301000.001|#1|#1\n"
        "|АБВГ.301000.002|#1|#2\n"
        "|Болт М10-6gx30.58.019 ГОСТ 7798-70|#4|#4\n"
        "|||\n"
        "АБВГ.302000.000|АБВГ.301000.000|#2",
        "Обозначение|№ опер.|Операция|Участок|Оборудование|Тпз, мин|Тшт, мин|Профессия\n"
        "АБВГ.301000.001|005|4280 Отрезная\n"
        "|10|Фрезерная|31 Механический участок 31|||#0.5\n"
        "||4210|||#12|#6.5|19149, Токарь\n"
        "АБВГ.301000.002||Токарная\n"
        "||\n"
        "|020|Нечто непонятное",
        "Деталь|Материал заготовки|Норма, кг|Размеры, мм\n"
        "АБВГ.301000.001|Лист 10 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89|#2.9|S=10;B=160;L=230\n"
        "АБВГ.301000.002|Круг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013|#1.6|Ø40x165\n"
        "|Круг 30|#0.35|Ø30x62",
        ""};
    int n = run("шаблон", names, rows, 7, L"", L"");
    has(L"И|АБВГ.301000.000|Опора|Сборочные единицы||4,1");
    has(L"И|АБВГ.301000.001|Основание|Детали|Лист 10 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89|2,4|D:\\Заказ 17\\АБВГ.301000.001.pdf");
    has(L"И|АБВГ.301000.002|Стойка|Детали||1,1||Покупное");
    has(L"И|Болт М10-6gx30.58.019 ГОСТ 7798-70||Стандартные изделия");
    has(L"И|Шайба 10 ГОСТ 11371-78||Стандартные изделия"); /* обозначение — наименование */
    has(L"И|АБВГ.301000.004|Кронштейн");
    has(L"И|АБВГ.301000.005|||||Z:\\Архив\\АБВГ.301000.005.tif"); /* путь с диском — как есть */
    hasnt(L"Без обозначения");
    hasnt(L"чертёж в архиве"); /* лишний столбец — мимо */
    hasnt(L"Списание"); /* скрытый лист «Списки» — мимо */
    hasnt(L"Первая строка"); /* «Как заполнять» — мимо */
    has(L"С|АБВГ.301000.000|АБВГ.301000.001|1|1");
    has(L"С|АБВГ.301000.000|АБВГ.301000.002|1|2"); /* сборка — как выше */
    has(L"С|АБВГ.301000.000|Болт М10-6gx30.58.019 ГОСТ 7798-70|4|4");
    has(L"С|АБВГ.302000.000|АБВГ.301000.000|2");
    has(L"О|АБВГ.301000.001|005|4280 Отрезная");
    has(L"О|АБВГ.301000.001|010|4260 Фрезерная|31 Механический участок 31|||0,5");
    has(L"О|АБВГ.301000.001|015|4210 Сверлильная|||12|6,5|19149, Токарь"); /* номер — следующий, код — с названием */
    has(L"О|АБВГ.301000.002|005|4110 Токарная"); /* новая деталь — номера сначала */
    has(L"О|АБВГ.301000.002|020|Нечто непонятное");
    has(L"З|АБВГ.301000.001|Лист 10 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89|2,9|S=10;B=160;L=230");
    has(L"З|АБВГ.301000.002|Круг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013|1,6|D=40;L=165");
    has(L"З|АБВГ.301000.002|Круг 30|0,35|D=30;L=62"); /* деталь — как выше */
    ok(g_o.rows[PK_ITEMS] == 7 && g_o.rows[PK_LINKS] == 4 && g_o.rows[PK_OPS] == 5 && g_o.rows[PK_BLANKS] == 3,
       "строк по листам");
    ok(n == 19, "всего строк");
    ok(g_o.skipped == 1, "мимо: строка без обозначения (пустые — молча)");
    ok(g_o.sheets == 4 && !wcscmp(g_o.used, L"Изделия; Состав; Операции; Заготовки"), "листы по шаблону");
    ok(!g_o.passed[0], "мимо листов нет (служебные и пустые — молча)");
  }

  /* своя форма: спецификация с заголовком над шапкой — как «Вставить»; лист с шапкой пакета под другим именем */
  {
    const char *names[] = {"Лист1", "ТП детали", "Заметки"};
    const char *rows[] = {
        "Спецификация АБВГ.123456.000 Редуктор\n"
        "Формат|Зона|Поз.|Обозначение|Наименование|Кол.|Примечание\n"
        "||||Детали\n"
        "А4||#1|АБВГ.123456.002|Корпус|#1\n"
        "А4||#2|АБВГ.123456.003|Крышка|#2\n"
        "||||Стандартные изделия\n"
        "||#3||Болт М6х20 ГОСТ 7798-70|#8",
        "Маршрутная карта\n"
        "Деталь|Операция|Станок|Тшт, мин\n"
        "АБВГ.123456.002|Токарная|16К20|#4.5\n"
        "|Сверлильная|2Н135",
        "просто текст\nещё текст"};
    run("своя форма", names, rows, 3, L"", L"");
    has(L"И|АБВГ.123456.000|Редуктор|Сборочные единицы");
    has(L"И|АБВГ.123456.002|Корпус|Детали");
    has(L"С|АБВГ.123456.000|АБВГ.123456.003|2|2");
    has(L"С|АБВГ.123456.000|Болт М6х20 ГОСТ 7798-70|8|3");
    has(L"О|АБВГ.123456.002|005|4110 Токарная||16К20||4,5");
    has(L"О|АБВГ.123456.002|010|4210 Сверлильная||2Н135"); /* деталь — как выше (объединённая ячейка) */
    ok(wcsstr(g_o.used, L"Лист1 (по шапке)") != NULL, "спецификация — как «Вставить», по шапке");
    ok(wcsstr(g_o.passed, L"Заметки") != NULL, "непонятый лист — назван");
  }

  /* лист «Операции» без обозначения — к выбранной в окне детали; «Состав» без сборки — к выбранной сборке */
  {
    const char *names[] = {"Операции", "Состав"};
    const char *rows[] = {"№ опер.|Операция|Тшт, мин\n#5|Токарная|#3\n#10|4260|#2.25",
                          "Входящее|Кол-во|Сборка\nАБВГ.1.001|#3"};
    run("к выбранной", names, rows, 2, L"АБВГ.1.000", L"АБВГ.1.002");
    has(L"О|АБВГ.1.002|005|4110 Токарная||||3");
    has(L"О|АБВГ.1.002|010|4260 Фрезерная||||2,25");
    has(L"С|АБВГ.1.000|АБВГ.1.001|3");
  }

  printf("%s: проверок %d, ошибок %d\n", g_fail ? "FAIL" : "ok", g_n, g_fail);
  return g_fail != 0;
}
