/* Проверка расчёта норм norms.c без Windows:
   gcc -O2 -Wall -Werror -o norms_test norms_test.c && ./norms_test   (-v — показать разбор)
   Таблицы здесь выдуманы: настоящие нормативы — только у пользователя, в репозиторий
   не кладутся. Числа ожидаемых норм посчитаны вручную по этим таблицам. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function" /* из резки нужен только cut_calc */
#include "cutting_calc.c"
#pragma GCC diagnostic pop
#include "techno.c"
#include "norms.c"

static int g_fail, g_n, g_verbose;

static void out8(const wchar_t *w) {
  for (; *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}

static void ok(const char *what, int cond) {
  g_n++;
  if (!cond) g_fail++, printf("FAIL %s\n", what);
}

static void near(const char *what, double got, double want) {
  g_n++;
  if (!(got > want - 1e-3 && got < want + 1e-3)) g_fail++, printf("FAIL %s: %.6f, ждали %.6f\n", what, got, want);
}

static void has(const char *what, const wchar_t *text, const wchar_t *part) {
  g_n++;
  if (!wcsstr(text, part)) {
    g_fail++;
    printf("FAIL %s: нет «", what);
    out8(part);
    printf("» в\n");
    out8(text);
    printf("\n");
  }
}

static wchar_t *dupw(const wchar_t *s) {
  size_t n = wcslen(s) + 1;
  wchar_t *d = (wchar_t *)malloc(n * sizeof(wchar_t));
  memcpy(d, s, n * sizeof(wchar_t));
  return d;
}

/* ---- выдуманные таблицы («Тестовая книга») --------------------------------------- */

static const wchar_t *const kTabs[][2] = {
    {L"установка.tsv",
     L"# выдумано для теста\n"
     L"назначение\tтокарная.установка; сверлильная.установка\n"
     L"источник\tТестовая книга (выдумано)\n"
     L"карта\t1\nстраница\t10\nстолбцы\tмасса, кг\nед\tмин\nсверено\tда 24.10.2026 тест\n"
     L"поз.\tспособ\tкак в книге\tдо 1\tдо 3\tдо 5\tсв. 5 до 10\n"
     L"1\tпатрон\tВ самоцентрирующем патроне\t0,1\t0,2\t0,3\t0,4\n"
     L"2\tцентры\tВ центрах\t0,15\t0,25\t0,35\t—\n"
     L"3\tтиски; стол\tВ тисках или на столе\t0,12\t0,22\t0,32\t0,42\n"},
    {L"переход.tsv",
     L"назначение\tтокарная.переход\nкарта\t2\nстраница\t11\nстолбцы\tразмер станка\nед\tмин\n"
     L"поз.\tD, мм\tдо 250\tдо 500\n"
     L"1\tдо 50\t0,05\t0,07\n"
     L"2\tсв. 50 до 100\t0,08\t0,1\n"},
    {L"подача.tsv", /* «Текст Юникод» из Excel: кавычки вокруг клеток */
     L"назначение\tтокарная.подача\nкарта\t3\nстолбцы\tt, мм\nед\tмм/об\n"
     L"поз.\tматериал\tD\tдо 2\tдо 5\n"
     L"1\tсталь\tдо 50\t0,3\t0,25\r\n"
     L"2\t\tсв. 50\t0,4\t0,35\r\n"
     L"3\t\"алюминий\"\t*\t0,5\t0,45\n"},
    {L"скорость.tsv",
     L"назначение\tтокарная.скорость\nкарта\t4\nстолбцы\tS\nвыбор\tинтерполяция\nед\tм/мин\n"
     L"поз.\tматериал\tдо 0,2\tдо 0,3\tдо 0,4\n"
     L"1\tсталь\t150\t120\t100\n"},
    {L"скорость-к.tsv",
     L"назначение\tтокарная.скорость.к\nкарта\t5\nед\tкоэф\n"
     L"поз.\tмарка\tзначение\n"
     L"1\t45\t0,9\n"
     L"2\t*\t1\n"},
    {L"врезание.tsv",
     L"назначение\tтокарная.врезание\nкарта\t6\nстолбцы\tt\nед\tмм\n"
     L"поз.\tдо 3\tсв. 3\n"
     L"1\t2\t4\n"},
    {L"обслуживание.tsv",
     L"назначение\tтокарная.обслуживание\nкарта\t7\nстолбцы\tразмер станка\nед\t%\n"
     L"поз.\tдо 250\tдо 500\n"
     L"1\t3\t4\n"},
    {L"отдых.tsv",
     L"назначение\tотдых\nкарта\t8\nед\t%\n"
     L"поз.\tзначение\n"
     L"1\t4\n"},
    {L"пз.tsv",
     L"назначение\tтокарная.пз\nкарта\t9\nстолбцы\tразмер станка\nед\tмин\n"
     L"поз.\tспособ\tдо 250\tдо 500\n"
     L"1\tпатрон\t10\t12\n"
     L"2\tцентры\t14\t16\n"},
    {L"сверл-подача.tsv",
     L"назначение\tсверлильная.подача\nкарта\t20\nстолбцы\td\nед\tмм/об\n"
     L"поз.\tматериал\tдо 12\tдо 20\n"
     L"1\tсталь\t0,2\t0,3\n"},
    {L"сверл-скорость.tsv",
     L"назначение\tсверлильная.скорость\nкарта\t21\nед\tм/мин\n"
     L"поз.\tматериал\tзначение\n"
     L"1\tсталь\t25\n"},
    {L"сверл-врезание.tsv",
     L"назначение\tсверлильная.врезание\nкарта\t22\nстолбцы\td\nед\tмм\n"
     L"поз.\tдо 12\tдо 20\n"
     L"1\t4\t6\n"},
    {L"пробивка.tsv",
     L"назначение\tрезка.пробивка\nкарта\t30\nстолбцы\tтолщина\nед\tмин\n"
     L"поз.\tматериал\tдо 3\tдо 6\n"
     L"1\tсталь\t0,02\t0,04\n"},
    {L"станки.tsv",
     L"назначение\tстанки\nвид\tзаписи\nисточник\tпаспорта (выдумано)\n"
     L"поз.\tстанок\tгруппа\tразмер\tряд n\tряд S\n"
     L"1\t16К20\tтокарная\t400\t100 200 400 800 1600\t0,1 0,2 0,3 0,4\n"
     L"2\t2Н135\tсверлильная\t35\t250 500 1000\t0,1 0,2 0,4\n"},
};

static void base_fill(NmBase *b) {
  memset(b, 0, sizeof(*b));
  for (size_t i = 0; i < sizeof(kTabs) / sizeof(kTabs[0]); i++)
    if (!nm_base_add(b, dupw(kTabs[i][1]), kTabs[i][0])) {
      g_fail++;
      printf("FAIL таблица %zu не прочиталась: ", i);
      out8(b->err);
      printf("\n");
    }
}

/* ---- проверки --------------------------------------------------------------------- */

static void test_ranges(void) {
  NmRng r;
  ok("«до 3»", nm_rng(L"до 3", &r) && r.hi == 3 && r.lo < -1e299 && nm_in(&r, 3) && !nm_in(&r, 3.01));
  ok("«До 0,25»", nm_rng(L"До 0,25", &r) && r.hi == 0.25);
  ok("«св. 5 до 10»", nm_rng(L"св. 5 до 10", &r) && r.lo == 5 && r.hi == 10 && !nm_in(&r, 5) && nm_in(&r, 10));
  ok("«свыше 20»", nm_rng(L"свыше 20", &r) && r.lo == 20 && r.hi > 1e299 && nm_in(&r, 21) && !nm_in(&r, 20));
  ok("«3–5»", nm_rng(L"3–5", &r) && r.lo == 3 && r.loIn && r.hi == 5 && nm_in(&r, 3));
  ok("«12,5»", nm_rng(L"12,5", &r) && r.hi == 12.5);
  ok("«до 3 кг»", nm_rng(L"до 3 кг", &r) && r.hi == 3);
  ok("«16К20» — не диапазон", !nm_rng(L"16К20", &r));
  ok("«патрон» — не диапазон", !nm_rng(L"патрон", &r));
  ok("«*» — любой", nm_rng(L"*", &r) && r.any && nm_in(&r, 1e9));
  ok("прочерк — нет значения", nm_na(nm_val(L"—")) && nm_na(nm_val(L"")) && nm_val(L"0,35*") == 0.35);
  ok("латинская K в станке", nm_eq(L"16K20", L"16К20") && !nm_eq(L"D", L"d"));
}

static void test_tables(NmBase *b) {
  ok("все таблицы прочитаны", b->n == (int)(sizeof(kTabs) / sizeof(kTabs[0])) && !b->err[0]);
  NmCtx c;
  NmHit h;
  memset(&c, 0, sizeof(c));
  nm_put(&c, L"способ", L"патрон", NM_NONE, NMO_ASK, NULL);
  nm_putv(&c, L"масса", 2.4, NMO_DRAW, NULL);
  ok("Тус патрон 2,4 кг → «до 3»", nm_find(b, L"токарная.установка", &c, &h) && h.v == 0.2);
  has("разбор Тус", h.why, L"к. 1 с. 10 поз. 1 «В самоцентрирующем патроне», «до 3»");
  ok("сверенная таблица без пометки", !wcsstr(h.why, L"не сверено"));
  nm_putv(&c, L"масса", 3, NMO_DRAW, NULL);
  ok("на границе «до 3» — сам столбец", nm_find(b, L"токарная.установка", &c, &h) && h.v == 0.2);
  nm_putv(&c, L"масса", 0.4, NMO_DRAW, NULL);
  ok("0,4 кг → «до 1»", nm_find(b, L"сверлильная.установка", &c, &h) && h.v == 0.1);
  nm_putv(&c, L"масса", 150, NMO_DRAW, NULL);
  ok("150 кг — за пределами", !nm_find(b, L"токарная.установка", &c, &h) && wcsstr(h.why, L"за пределами"));
  nm_put(&c, L"способ", L"центры", NM_NONE, NMO_ASK, NULL);
  nm_putv(&c, L"масса", 7, NMO_DRAW, NULL);
  ok("прочерк в клетке", !nm_find(b, L"токарная.установка", &c, &h) && wcsstr(h.why, L"прочерк"));
  nm_put(&c, L"способ", L"стол", NM_NONE, NMO_ASK, NULL);
  ok("код из списка «тиски; стол»", nm_find(b, L"токарная.установка", &c, &h) && h.v == 0.42);

  memset(&c, 0, sizeof(c));
  ok("нет параметра — какого", !nm_find(b, L"токарная.установка", &c, &h) && !wcscmp(h.need, L"способ"));
  ok("нет таблицы", !nm_find(b, L"токарная.измерение", &c, &h) && h.notab);

  /* объединённая клетка и строки по D: ближайшее большее по ключу */
  nm_put(&c, L"материал", L"сталь", NM_NONE, NMO_DRAW, NULL);
  nm_putv(&c, L"D", 80, NMO_DRAW, NULL);
  nm_putv(&c, L"t", 3, NMO_DRAW, NULL);
  ok("объединённая клетка «сталь», D 80 → «св. 50»", nm_find(b, L"токарная.подача", &c, &h) && h.v == 0.35);
  nm_putv(&c, L"D", 30, NMO_DRAW, NULL);
  ok("D 30 → «до 50»", nm_find(b, L"токарная.подача", &c, &h) && h.v == 0.25);
  nm_put(&c, L"материал", L"алюминий", NM_NONE, NMO_DRAW, NULL);
  ok("кавычки Excel и «*»", nm_find(b, L"токарная.подача", &c, &h) && h.v == 0.45);
  has("не сверено", h.why, L"не сверено");

  /* интерполяция */
  nm_put(&c, L"материал", L"сталь", NM_NONE, NMO_DRAW, NULL);
  nm_putv(&c, L"S", 0.25, NMO_DRAW, NULL);
  ok("интерполяция 0,25 между 150 и 120", nm_find(b, L"токарная.скорость", &c, &h) && h.v > 134.999 && h.v < 135.001);
  has("разбор интерполяции", h.why, L"между «до 0,2» и «до 0,3»");

  /* перечень станков и ступени */
  int low;
  ok("ряд n станка", nm_step(nm_rec(b, L"станки", L"16K20", L"ряд n", NULL), 799.5, &low) == 400 && !low);
  ok("ниже ряда — наименьшая", nm_step(L"250 500 1000", 100, &low) == 250 && low);
  ok("станок группы", !wcscmp(nm_rec_first(b, L"станки", L"группа", L"сверлильная"), L"2Н135"));
}

static void test_bad_files(void) {
  NmBase b;
  memset(&b, 0, sizeof(b));
  ok("без назначения — ошибка", !nm_base_add(&b, dupw(L"поз.\tзначение\n1\t2\n"), L"а.tsv"));
  ok("без «столбцы» — ошибка", !nm_base_add(&b, dupw(L"назначение\tх\nпоз.\tдо 1\tдо 2\n1\t1\t2\n"), L"б.tsv"));
  has("ошибки по файлам", b.err, L"а.tsv: нет строки «назначение»");
  has("ошибки по файлам", b.err, L"б.tsv: нет строки «столбцы»");
  ok("таблиц нет", b.n == 0);
  nm_base_free(&b);
}

static void test_ask(void) {
  NmCtx c;
  memset(&c, 0, sizeof(c));
  nm_ask(L"норма: L=120, партия 50; станок 16К20 в центрах, переходов 4, длина реза 1500, Ø45, d 8,5, t 2, "
         L"Dзаг 50",
         &c);
  ok("L", nm_v(&c, L"L") == 120);
  ok("партия", nm_v(&c, L"партия") == 50);
  ok("станок одним словом", nm_parc(&c, L"станок") && !wcscmp(nm_parc(&c, L"станок")->s, L"16К20"));
  ok("«в центрах» → центры", nm_parc(&c, L"способ") && !wcscmp(nm_parc(&c, L"способ")->s, L"центры"));
  ok("переходов", nm_v(&c, L"переходов") == 4);
  ok("длина реза → периметр", nm_v(&c, L"периметр") == 1500);
  ok("Ø → D", nm_v(&c, L"D") == 45);
  ok("d", nm_v(&c, L"d") == 8.5);
  ok("t", nm_v(&c, L"t") == 2);
  ok("Dзаг не путается с D", nm_v(&c, L"Dзаг") == 50);
  ok("заданное — с пометкой", nm_parc(&c, L"L")->org == NMO_ASK);
  memset(&c, 0, sizeof(c));
  nm_ask(L"способ в тисках, h11 — не припуск", &c);
  ok("способ словами", nm_parc(&c, L"способ") && !wcscmp(nm_parc(&c, L"способ")->s, L"тиски"));
  ok("h11 у допуска — не параметр", nm_na(nm_v(&c, L"h")));
  memset(&c, 0, sizeof(c));
  nm_ask(L"пластина S=6, подача 0,15, V 90, масса 1,5", &c);
  ok("«S=6» — толщина, не подача", nm_v(&c, L"S") == 0.15);
  ok("V", nm_v(&c, L"V") == 90);
  ok("масса из вопроса", nm_v(&c, L"масса") == 1.5);
}

static void show(const NmBase *b, const NmResult *res) {
  if (!g_verbose) return;
  static wchar_t txt[24000];
  txt[0] = 0;
  nm_text(b, res, L"(тест)", txt, 24000);
  out8(txt);
  printf("\n\n");
}

static void route_one(TnRoute *r, const wchar_t *op) {
  memset(r, 0, sizeof(*r));
  r->n = 1;
  tn_cpy(r->op[0].op, 48, op);
}

static void test_turn(NmBase *b) {
  static TnRoute r;
  static NmResult res;
  route_one(&r, L"Токарная");
  r.cls = TN_ROT;
  r.s.mat = TNM_STEEL;
  tn_cpy(r.s.grade, 48, L"45");
  r.s.dmax = 40;
  tn_cpy(r.blank, TN_TXT, L"Круг 43 ГОСТ 2590");
  nm_route(b, &r, L"2,4", L"норма: L 120, партия 10", &res);
  show(b, &res);
  ok("одна токарная", res.n == 1 && res.op[0].kind == NMK_TURN);
  const NmOp *o = &res.op[0];
  /* h = (43−40)/2 = 1,5 = t, i = 1; S 0,3 (к. 3); V 120·0,9 = 108; n = 108000/(π·43) = 799,5 → 400;
     l1 2; То = 122/(400·0,3) */
  near("То токарной", o->to, 122.0 / 120.0);
  near("Тв = 0,2 + 0,07·3", o->tv, 0.41);
  near("Тшт = Топ·1,08", o->tsht, (122.0 / 120.0 + 0.41) * 1.08);
  near("Тпз", o->tpz, 12);
  near("Тшк при партии 10", o->tshk, (122.0 / 120.0 + 0.41) * 1.08 + 1.2);
  ok("полная", o->full);
  has("способ по умолчанию", o->how, L"способ");
  has("станок по умолчанию в разборе", o->how, L"→ 400 (ступень 16К20, не сверено)");
  has("поправка Kv", o->how, L"Kv ×0,9 — марка 45 → к. 5 поз. 1");
  has("переходы — оценка", o->how, L"× переходов 3 (по умолчанию: оценка: 2 торца и поверхность)");
  has("То по формуле", o->how, L"То = (L + l1)·i/(n·S) = (120 + 2)·1/(400·0,3)");
  has("масса со штампа", o->how, L"масса 2,4");
  static wchar_t txt[24000];
  txt[0] = 0;
  nm_text(b, &res, L"", txt, 24000);
  has("итог операции", txt, L"005 Токарная: Тпз 12 мин · Тшт 1,541 мин · Тшк 2,741 мин");
  has("источники", txt, L"к. 1 с. 10 — токарная.установка; сверлильная.установка — Тестовая книга (выдумано) "
                        L"(установка.tsv, сверено 24.10.2026 тест)");

  /* L не задано: То нет, Тшт — нижняя граница, L — в «Уточнить» */
  nm_route(b, &r, L"2,4", NULL, &res);
  o = &res.op[0];
  ok("без L То нет", nm_na(o->to) && !o->full);
  has("уточнить L", res.need, L"длина обработки L");
  txt[0] = 0;
  nm_text(b, &res, L"", txt, 24000);
  has("Тшт без То", txt, L"Тшт — (нет То; без него ≥ ");
  has("подсказка", txt, L"«норма: L 120, глубина 20, партия 50, станок 16К20»");

  /* длинный вал — в центрах по умолчанию; заданный способ важнее */
  nm_route(b, &r, L"2,4", L"L 300", &res);
  has("L/D > 5 — центры", res.op[0].how, L"центры");
  near("Тпз в центрах", res.op[0].tpz, 16);
  nm_route(b, &r, L"2,4", L"L 300, в патроне", &res);
  near("заданный способ", res.op[0].tpz, 12);
}

static void test_drill(NmBase *b) {
  static TnRoute r;
  static NmResult res;
  route_one(&r, L"Сверлильная");
  r.cls = TN_FLAT;
  r.s.mat = TNM_STEEL_PLAIN;
  tn_cpy(r.s.grade, 48, L"Ст3сп");
  r.s.thick = 6;
  r.s.holes = 4;
  r.s.holeMin = 10;
  nm_route(b, &r, L"1,2", NULL, &res);
  show(b, &res);
  const NmOp *o = &res.op[0];
  /* S 0,2 → ступень 0,2; V 25; n = 25000/(π·10) = 795,8 → 500; l1 4; То = 4·(6+4)/(500·0,2) */
  near("То сверлильной", o->to, 0.4);
  near("Тв — только Тус 0,22", o->tv, 0.22);
  near("Тшт без обслуживания", o->tsht, 0.62 * 1.04);
  ok("неполная", !o->full && nm_na(o->tpz));
  has("глубина — толщина листа", o->how, L"l 6 мм (расчёт: сквозные — толщина листа)");
  has("станок группы", o->how, L"2Н135");
  has("нет таблиц", res.miss, L"сверлильная.переход");
  has("нет таблиц", res.miss, L"сверлильная.обслуживание");
  has("нет таблиц", res.miss, L"сверлильная.пз");
}

static void test_cut(NmBase *b) {
  static TnRoute r;
  static NmResult res;
  route_one(&r, L"Заготовительная (раскрой)");
  r.cls = TN_FLAT;
  r.s.mat = TNM_STEEL_PLAIN;
  tn_cpy(r.s.grade, 48, L"Ст3сп");
  r.s.thick = 2;
  r.s.holes = 2;
  ok("марка → нормы резки", !wcscmp(nm_cut_mat(L"Ст3сп"), L"ст3") && !wcscmp(nm_cut_mat(L"45"), L"ст45") &&
                                !wcscmp(nm_cut_mat(L"12Х18Н10Т"), L"12Х18Н10Т") && !nm_cut_mat(L"Д16"));
  nm_route(b, &r, NULL, L"периметр 1000", &res);
  show(b, &res);
  const NmOp *o = &res.op[0];
  /* ст3 S=2 лазер 4,9 м/мин (cutting_norms.h); пробивок 2 + 1, tпроб 0,02 */
  near("То резки", o->to, 1.0 / 4.9 + 3 * 0.02);
  near("Тшт резки с отдыхом", o->tsht, (1.0 / 4.9 + 0.06) * 1.04);
  has("скорость из норм резки", o->how, L"v 4,9 м/мин — нормы резки завода: Лазер, ст3 S=2");
  ok("резка неполная без обслуживания", !o->full);

  /* таблиц нет вовсе: только То резки, станочные пропущены */
  NmBase e;
  memset(&e, 0, sizeof(e));
  r.n = 2;
  tn_cpy(r.op[1].op, 48, L"Сверлильная");
  nm_route(&e, &r, NULL, L"периметр 1000", &res);
  ok("без таблиц — только резка", res.n == 1);
  near("То резки без пробивок", res.op[0].to, 1.0 / 4.9);
  static wchar_t txt[8000];
  txt[0] = 0;
  nm_text(&e, &res, L"Документы\\CursorPad\\Нормативы", txt, 8000);
  has("без таблиц — пояснение", txt, L"Таблиц нормативов ещё нет (папка Документы\\CursorPad\\Нормативы): у резки");
  has("без таблиц — То резки", txt, L"005 Заготовительная (раскрой): То 0,204 мин");
  if (g_verbose) out8(txt), printf("\n\n");
}

/* весь путь: текст листа → маршрут techno.c → нормы */
static void test_route(NmBase *b) {
  static TnRoute r;
  static NmResult res;
  tn_route(L"Вал", L"Сталь 45 ГОСТ 1050-2013", L"Ø40h9 Ra 3,2 фаска 1×45° 2 отв. Ø6", &r);
  nm_route(b, &r, L"2,4", L"L 120", &res);
  show(b, &res);
  if (g_verbose) {
    static wchar_t txt[9000];
    tn_text(&r, txt, 9000);
    out8(txt);
    printf("\n\n");
  }
  int turn = 0;
  for (int i = 0; i < res.n; i++) turn += res.op[i].kind == NMK_TURN;
  ok("в маршруте вала есть токарная с нормой", turn >= 1 && res.op[0].full);
}

int main(int argc, char **argv) {
  g_verbose = argc > 1 && !strcmp(argv[1], "-v");
  NmBase b;
  base_fill(&b);
  test_ranges();
  test_tables(&b);
  test_bad_files();
  test_ask();
  test_turn(&b);
  test_drill(&b);
  test_cut(&b);
  test_route(&b);
  nm_base_free(&b);
  printf("norms_test: %d проверок, ошибок %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
