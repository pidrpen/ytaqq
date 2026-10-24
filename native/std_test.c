/* Проверка справочника std.c без Windows:
   gcc -O2 -Wall -Werror -o std_test std_test.c && ./std_test   (-v — показать ответы)
   Стандарты и числа здесь ВЫДУМАНЫ (номера 999xx): настоящие таблицы лежат
   только у пользователя, в репозиторий их не кладём. Проверяется разбор файла
   (в том числе после Excel: BOM, CRLF, лишние табуляции, кавычки), поиск по
   словам вопроса и строке штампа, соседние размеры, масса заготовки, аналоги. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "std.c"

static void out8(const wchar_t *w) {
  for (; w && *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}

static int g_fail, g_n, g_verbose;

static void load(GsDb *db, const wchar_t *text, const wchar_t *file) {
  size_t n = wcslen(text);
  wchar_t *t = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
  memcpy(t, text, (n + 1) * sizeof(wchar_t));
  gs_parse(db, t, file);
}

static void ok(int cond, const char *what) {
  g_n++;
  if (!cond) g_fail++, printf("FAIL %s\n", what);
}

/* want — подстроки ответа через «|», no — каких быть не должно */
static void ask(const GsDb *db, const wchar_t *q, int hits, const wchar_t *want, const wchar_t *no) {
  GsHit hit[40];
  GsQuery Q;
  int n = gs_find(db, q, hit, 40, &Q);
  GsBuf b = {0};
  gs_text(&Q, hit, n, &b);
  const wchar_t *txt = b.s ? b.s : L"";
  int good = n == hits;
  for (int pass = 0; pass < 2; pass++)
    for (const wchar_t *p = pass ? no : want; p && *p;) {
      const wchar_t *e = wcschr(p, L'|');
      wchar_t one[120] = {0};
      wcsncpy(one, p, e ? (size_t)(e - p) : wcslen(p));
      int found = wcsstr(txt, one) != NULL;
      if (found == pass) { /* нужного нет или есть лишнее */
        good = 0;
        printf("  %s «", pass ? "лишнее" : "нет");
        out8(one);
        printf("»\n");
      }
      p = e ? e + 1 : NULL;
    }
  g_n++;
  if (!good) g_fail++;
  if (!good || g_verbose) {
    printf("%s «", good ? "----" : "FAIL");
    out8(q);
    printf("»: находок %d (ждали %d)\n", n, hits);
    out8(txt);
    printf("\n");
  }
  free(b.s);
}

int main(int argc, char **argv) {
  g_verbose = argc > 1 && !strcmp(argv[1], "-v");
  GsDb db;
  memset(&db, 0, sizeof(db));

  /* после Excel: BOM, CRLF, добивка табуляциями, ячейка в кавычках */
  load(&db,
       L"\xFEFF## болты — выдуманные числа для теста\r\n"
       L"#стандарт\tГОСТ 99001-70\t\t\t\r\n"
       L"#название\tБолты тестовые с шестигранной головкой\t\t\t\r\n"
       L"#таблица\t1 — Размеры, мм\r\n"
       L"#источник\tтестовая страница, табл. 1, с. 3\r\n"
       L"#вид\tболт; винт с шестигранной головкой\r\n"
       L"#ключ\td\r\n"
       L"#проверено\tда, 2026-10-25\r\n"
       L"#примечание\t\"Размеры \"\"под ключ\"\" выдуманы\"\r\n"
       L"d\tP\tS, мм\tk, мм\t\t\r\n"
       L"M6\t1\t11\t4,1\t\t\r\n"
       L"M8\t1,25\t14\t5,1\r\n"
       L"M12\t1,75\t18,5\t7,7\r\n"
       L"M12x1,25\t1,25\t18,5\t7,7\r\n"
       L"M16\t2\t23\t9,9\r\n",
       L"bolt.tsv");
  load(&db,
       L"#стандарт\tDIN 99934\n#название\tГайки тестовые\n#таблица\t2\n#вид\tгайка\n#проверено\tнет\n"
       L"d\tS\tm\nМ12\t17,5\t9,9\nМ16\t22,5\t12,9\n",
       L"nut.tsv");
  load(&db,
       L"#стандарт\tГОСТ 99902-2006\n#название\tКруг тестовый\n#таблица\t1 — Сортамент\n"
       L"#источник\tтест, табл. 1\n#вид\tкруг; пруток\n#масса\tМасса 1 м\n"
       L"Диаметр, мм\tПлощадь, см²\tМасса 1 м, кг\n45\t15,9\t12,1\n48\t18,1\t14,2\n50\t19,6\t12,5\n52\t21,2\t16,7\n",
       L"round.tsv");
  load(&db,
       L"#стандарт\tГОСТ 99901-2013\n#название\tСталь тестовая углеродистая\n#таблица\t1 — Химический состав, %\n"
       L"#вид\tсталь; марка\nМарка\tC\tMn\n40\t0,37–0,45\t0,5–0,8\n45\t0,42–0,50\t0,5–0,8\n50, 50А\t0,47–0,55\t0,5–0,8\n",
       L"steel.tsv");
  /* две таблицы в одном файле: вторая наследует стандарт, вид и источник */
  load(&db,
       L"#стандарт\tГОСТ 99943-2016\n#название\tСталь тестовая легированная\n#вид\tсталь; марка\n"
       L"#источник\tтест\n#таблица\t1 — Химический состав, %\n#проверено\tда, тест\n"
       L"Марка\tC\tCr\n20Х\t0,17–0,23\t0,7–1,0\n40Х\t0,36–0,44\t0,8–1,1\n12Х18Н10Т\t≤0,12\t17–19\n"
       L"#таблица\t2 — Механические свойства\n#ключ\tМарка\n"
       L"Марка\tЗакалка, °C\tσв, Н/мм²\n40Х\t860, масло\t980\n",
       L"alloy.tsv");
  load(&db,
       L"#стандарт\tГОСТ 99380\n#вид\tсталь\nМарка\tC\nСт3сп\t0,14–0,22\nСт3пс\t0,14–0,22\n",
       L"plain.tsv");
  load(&db,
       L"#стандарт\tСоответствия тестовые\n#вид\tаналог; замена\n#ключ\tвсе\n"
       L"ГОСТ\tDIN\tISO\tПримечание\n"
       L"ГОСТ 99001-70\tDIN 99933\tISO 99017\tS у M12 другой\n"
       L"40Х\t41Cr4 (1.7035)\t\tтестовая пара\n",
       L"analog.tsv");
  /* ошибки: нет #стандарт, неизвестная строка, лишняя ячейка, только шапка */
  load(&db, L"#вид\tкруг\nd\tm\n1\t2\n", L"bad1.tsv");
  load(&db, L"#стандарт\tГОСТ 99777\n#цвет\tсиний\nd\tm\n1\t2\t3\n#стандарт\tГОСТ 99778\nd\n", L"bad2.tsv");
  load(&db, L"# простой комментарий\n#стандарт\tГОСТ 99893.2\n#таблица\t1\nd\tm\n7\t8\n", L"part.tsv");

  ok(db.files == 10, "прочитано 10 файлов");
  ok(db.n == 10, "таблиц 10 (bad1 и пустая из bad2 пропущены)");
  ok(db.warn.s && wcsstr(db.warn.s, L"bad1.tsv, строка 2: нет строки #стандарт"), "предупреждение: нет #стандарт");
  ok(db.warn.s && wcsstr(db.warn.s, L"bad2.tsv, строка 2: неизвестная строка"), "предупреждение: неизвестная строка #");
  ok(db.warn.s && wcsstr(db.warn.s, L"bad2.tsv, строка 4: ячеек больше"), "предупреждение: лишняя ячейка");
  ok(db.warn.s && wcsstr(db.warn.s, L"bad2.tsv, строка 6: в таблице только шапка"), "предупреждение: только шапка");
  ok(db.warn.s && !wcsstr(db.warn.s, L"part.tsv"), "«# комментарий» — без предупреждения");
  const GsTable *bolt = &db.t[0];
  ok(bolt->ncol == 4 && bolt->nrow == 5, "болты: 4 столбца, 5 строк (пустые столбцы Excel отброшены)");
  ok(!wcscmp(bolt->std, L"ГОСТ 99001-70") && gs_is_checked(bolt), "болты: стандарт без хвоста табуляций, сверено");
  ok(!wcscmp(bolt->note, L"Размеры \"под ключ\" выдуманы"), "кавычки Excel сняты");
  ok(!wcscmp(gs_cell(bolt, 2, 3), L"5,1"), "CRLF снят");
  ok(!wcscmp(db.t[5].std, L"ГОСТ 99943-2016") && !wcscmp(db.t[5].table, L"2 — Механические свойства") &&
         !wcscmp(db.t[5].src, L"тест") && !gs_is_checked(&db.t[5]),
     "вторая таблица файла: стандарт и источник наследованы, сверка — нет");

  GsRef a, b;
  ok(gs_ref_at(L"DIN EN ISO 99017", &a) && gs_ref_at(L"ISO 99017-2", &b) && gs_ref_eq(&a, &b), "DIN EN ISO = ISO");
  ok(gs_ref_at(L"ГОСТ Р 99999-2006", &a) && gs_ref_at(L"ГОСТ 99999", &b) && !gs_ref_eq(&a, &b), "ГОСТ Р ≠ ГОСТ");
  ok(gs_ref_at(L"ГОСТ7798", &a) && !wcscmp(a.num, L"7798"), "ГОСТ слитно с номером");
  ok(!gs_ref_at(L"гостиница 5", &a), "«гостиница» — не ГОСТ");

  ask(&db, L"болт М12 ГОСТ 99001", 1, L"d: M12; P: 1,75; S, мм: 18,5|Сверено человеком: да, 2026-10-25",
      L"M12x1,25|НЕ СВЕРЕНО");
  ask(&db, L"болт m12x1.25", 1, L"d: M12x1,25", L"P: 1,75");
  ask(&db, L"болт М12х60", 1, L"d: M12;", L"M12x1,25");
  ask(&db, L"размеры гайки M12", 1, L"DIN 99934|d: М12; S: 17,5|НЕ СВЕРЕНО человеком", L"ГОСТ 99001");
  ask(&db, L"М12", 3, L"ГОСТ 99001-70|DIN 99934", NULL);
  ask(&db, L"шайба 12", 0, NULL, NULL);
  ask(&db, L"круг 51", 2, L"«51» в таблице нет, ближайшие строки:|Диаметр, мм: 50|Диаметр, мм: 52", NULL);
  ask(&db, L"масса круга Ø50 длиной 120 мм", 1, L"12,5 кг/м × 0,12 м = 1,5 кг", NULL);
  ask(&db, L"Круг 50-В ГОСТ 99902-2006 / 45 ГОСТ 99901-2013", 2, L"Диаметр, мм: 50|Марка: 45; C: 0,42–0,50",
      L"Диаметр, мм: 45");
  ask(&db, L"круг 50 сталь 45", 2, L"Диаметр, мм: 50|Марка: 45", L"Диаметр, мм: 45|Марка: 40;");
  ask(&db, L"свойства стали 40Х", 2, L"Cr: 0,8–1,1|σв, Н/мм²: 980", L"«40Х» в таблице нет|0,37–0,45");
  ask(&db, L"40X", 3, L"Cr: 0,8–1,1|σв, Н/мм²: 980|41Cr4", NULL);
  ask(&db, L"механические свойства 40Х", 3, L"2 — Механические свойства", NULL);
  ask(&db, L"Ст3", 2, L"Марка: Ст3сп|Марка: Ст3пс", L"Ст3кп");
  ask(&db, L"сталь 50А", 1, L"Марка: 50, 50А; C: 0,47–0,55", NULL);
  ask(&db, L"сталь 41", 4, L"«41» в таблице нет. Строк в таблице: 3; Марка от 40 до 50, 50А", L"ближайшие");
  ask(&db, L"болт 1,25", 1, L"«1,25» в таблице нет", L"d: M12x1,25"); /* десятичная запятая — не граница частей */
  ask(&db, L"аналог DIN 99933", 1, L"ГОСТ: ГОСТ 99001-70; DIN: DIN 99933|S у M12 другой", L"ГОСТ 99001-70 «");
  ask(&db, L"чем заменить 1.7035", 1, L"ГОСТ: 40Х", NULL);
  ask(&db, L"ГОСТ 99001", 1, L"Строк в таблице: 5; d от M6 до M16|Нужен размер или марка", L"DIN 99933");
  ask(&db, L"аналог ГОСТ 99001", 2, L"Строк в таблице: 5|DIN: DIN 99933", NULL);
  ask(&db, L"болт М13 ГОСТ 99001", 1, L"«М13» в таблице нет. Строк в таблице: 5", L"Нужен размер");
  ask(&db, L"ГОСТ 99893", 1, L"ГОСТ 99893.2", NULL);
  ask(&db, L"ГОСТ 12345", 0, NULL, NULL);
  ask(&db, L"привет, как дела?", 0, NULL, NULL);

  GsBuf c = {0};
  gs_cite(bolt, &c);
  gs_cat(&c, L"; ");
  gs_cite(&db.t[1], &c);
  ok(c.s && !wcscmp(c.s, L"ГОСТ 99001-70, табл. 1 — Размеры, мм — сверено; DIN 99934, табл. 2 — НЕ сверено человеком"),
     "подпись таблиц для подвала ответа");
  free(c.s);

  GsHit h[4];
  GsQuery Q;
  double m = 0;
  ok(gs_find(&db, L"ГОСТ 99902-2006 50", h, 4, &Q) == 1 && h[0].row &&
         gs_number(gs_cell(h[0].t, h[0].row, gs_col_find(h[0].t, L"Масса 1 м")), &m) && m == 12.5,
     "строка круга 50 для кода: 12,5 кг/м");

  /* что чат отдаёт справочнику, а что — как обычно */
  static const wchar_t *const kYes[] = {L"болт М12 ГОСТ 7798", L"размеры гайки M16", L"DIN 933", L"40Х", L"М12х1,25",
                                        L"свойства 40Х", L"чем заменить 20Х?", L"сколько весит круг 50 длиной 300",
                                        L"химсостав 12Х18Н10Т", L"Круг 50 ГОСТ 2590-2006 / 45 ГОСТ 1050-2013"};
  static const wchar_t *const kNo[] = {L"привет", L"у меня 2 болта", L"win11 тормозит", L"что лучше i5 или i7",
                                       L"как дела 2026", L"найди АДЕ 3424-123.01", L"размер файла 12 МБ"};
  for (size_t i = 0; i < sizeof(kYes) / sizeof(kYes[0]); i++) {
    g_n++;
    if (!gs_wanted(kYes[i])) g_fail++, printf("FAIL справочнику: «"), out8(kYes[i]), printf("»\n");
  }
  for (size_t i = 0; i < sizeof(kNo) / sizeof(kNo[0]); i++) {
    g_n++;
    if (gs_wanted(kNo[i])) g_fail++, printf("FAIL не справочнику: «"), out8(kNo[i]), printf("»\n");
  }

  if (g_verbose && db.warn.s) printf("Предупреждения:\n"), out8(db.warn.s);
  gs_free(&db);
  printf("%d/%d ok\n", g_n - g_fail, g_n);
  return g_fail ? 1 : 0;
}
