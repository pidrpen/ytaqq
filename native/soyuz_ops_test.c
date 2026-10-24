/* Проверка soyuz_ops.c без Windows:
   gcc -O2 -Wall -Werror -o soyuz_ops_test soyuz_ops_test.c && ./soyuz_ops_test   (-v — показать разбор)
   Маршрут берётся живой — techno.c по тексту листа, как он ложится в чат (tn_text),
   и его же поправленный руками вариант с нормами (norms.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "techno.c"
#include "soyuz_ops.c"

static void out8(const wchar_t *w) {
  for (; *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}

static int g_fail, g_n, g_verbose;

static void ok(int cond, const char *what) {
  g_n++;
  if (!cond) {
    g_fail++;
    printf("FAIL %s\n", what);
  }
}

static void eqw(const wchar_t *got, const wchar_t *want, const char *what) {
  g_n++;
  if (wcscmp(got, want)) {
    g_fail++;
    printf("FAIL %s: «", what);
    out8(got);
    printf("», ждали «");
    out8(want);
    printf("»\n");
  }
}

static void dump(const SoRoute *r) {
  printf("деталь «");
  out8(r->name);
  printf("» обозначение «");
  out8(r->des);
  printf("», операций %d\n", r->n);
  for (int i = 0; i < r->n; i++) {
    const SoItem *it = &r->it[i];
    printf("  ");
    out8(it->num);
    printf(" ");
    out8(it->op);
    printf(" → ");
    out8(it->code[0] ? it->code : L"(нет кода)");
    printf(" ");
    out8(it->name);
    if (it->alt[0]) printf(" (ещё "), out8(it->alt), printf(")");
    if (it->tpz >= 0 || it->tsht >= 0) printf(" · Тпз %g Тшт %g", it->tpz, it->tsht);
    printf("\n");
  }
}

/* операции живого маршрута: тот же порядок, номера 005, 010…, у каждой код */
static void live_route(void) {
  TnRoute tr;
  tn_route(L"Удлинитель вала", L"Круг 50 ГОСТ 2590-2006 / 45 ГОСТ 1050-2013",
           L"Ra 3,2\nRa 0,8\nØ30 -0,02\n-0,04\nM12-6H\n2 отв. Ø4+0,03\n1. 30...34 HRC\n"
           L"2. Общие допуски по ГОСТ 30893.2-mK\n3. Покрытие: аэрозольный лак\n",
           &tr);
  static wchar_t txt[9000];
  int n = swprintf(txt, 9000, L"Лист: УВ-01.pdf · АБВГ.711141.001\nЛист прочитан только Tesseract.\n");
  tn_text(&tr, txt + n, 9000 - n);
  SoRoute r;
  so_parse(txt, &r);
  if (g_verbose) out8(txt), printf("\n\n"), dump(&r);
  ok(r.n == tr.n, "живой маршрут: число операций");
  for (int i = 0; i < r.n && i < tr.n; i++) {
    eqw(r.it[i].op, tr.op[i].op, "живой маршрут: название");
    ok(r.it[i].code[0] != 0, "живой маршрут: у каждой операции есть код");
    ok(r.it[i].state == SO_WAIT && r.it[i].tpz < 0 && r.it[i].aTsht < 0, "живой маршрут: ждёт, норм нет");
  }
  wchar_t want[8];
  swprintf(want, 8, L"%03d", 5 * r.n);
  if (r.n) eqw(r.it[r.n - 1].num, want, "живой маршрут: номер последней");
  eqw(r.des, L"АБВГ.711141.001", "обозначение из «Лист:»");
  eqw(r.name, L"Удлинитель вала", "наименование из «Деталь:»");
  eqw(r.it[0].code, L"0110", "Заготовительная → 0110");
  eqw(r.it[1].code, L"4110", "Токарная (черновая) → 4110");
  eqw(r.it[1].what, tr.op[1].what, "что делать — без причины");
}

/* маршрут, поправленный в чате, с нормами */
static void edited_route(void) {
  SoRoute r;
  so_parse(L"Маршрут обработки — черновик\n\nДеталь: (наименование не прочитано) — тело вращения\n"
           L"Заготовка: Круг 50\n\n"
           L"005 Заготовительная (раскрой). Резать лазером по программе — лист S=6\n"
           L"010 Слесарная (разметка). Разметить отверстия\n"
           L"015 Термическая. Закалка 30...34 HRC\n"
           L"020 Контрольная. Контроль ОТК\n"
           L"  - 025 Гальваническая. Покрытие Ц9.хр — на чертеже\n"
           L"030 Притирочная. Притереть — неизвестная правилам\n"
           L"2026 г. выпуск\n"
           L"12 отв. Ø6\n"
           L"\nНормы времени — считает код по таблицам нормативов, проверяет технолог\n"
           L"005 Заготовительная (раскрой): То 1,2 мин · Тпз 12 мин · Тшт 3,45 мин (по картам)\n"
           L"015 Термическая: Тпз — · Тшт ≈ 4 мин (неполная)\n"
           L"\nУточнить: длина обработки\n",
           &r);
  if (g_verbose) dump(&r);
  ok(r.n == 6, "поправленный маршрут: 6 операций, «2026 г.» и «12 отв.» — не операции");
  ok(r.name[0] == 0, "«(наименование не прочитано)» — не имя");
  eqw(r.it[0].code, L"9171", "раскрой лазером → 9171");
  eqw(r.it[1].code, L"0101", "Слесарная (разметка) → 0101");
  eqw(r.it[2].code, L"5000", "Термическая → 5000");
  eqw(r.it[2].alt, L"5030, 5050", "Термическая: другие коды");
  eqw(r.it[3].code, L"0200", "Контрольная → 0200");
  eqw(r.it[3].alt, L"0201", "Контрольная: и 0201");
  eqw(r.it[4].code, L"7174", "Гальваническая с цинком → 7174");
  eqw(r.it[4].num, L"025", "строка с маркером списка");
  ok(r.it[5].code[0] == 0, "неизвестная операция — без кода");
  ok(r.it[0].tpz == 12 && r.it[0].tsht > 3.449 && r.it[0].tsht < 3.451, "нормы раскроя");
  ok(r.it[2].tpz < 0 && r.it[2].tsht == 4, "нормы термической: Тпз — нет, Тшт ≈ 4");
  ok(r.it[1].tpz < 0 && r.it[1].tsht < 0, "у разметки норм нет");
  wchar_t t[80];
  so_clip_text(&r.it[0], t, 80);
  eqw(t, L"9171 Термическая резка лазерная", "текст в буфер — как для 1С");
  so_clip_text(&r.it[5], t, 80);
  eqw(t, L"Притирочная", "без кода — название маршрута");
  SoItem x;
  memset(&x, 0, sizeof(x));
  tn_cpy(x.op, 64, L"Токарная (резьба)");
  so_clip_text(&x, t, 80);
  eqw(t, L"Токарная", "без кода — название без уточнения в скобках");
  so_min(12, t, 80);
  eqw(t, L"12 мин", "минуты");
  so_min(3.4, t, 80);
  eqw(t, L"3,4 мин", "минуты с запятой");
  so_min(-1, t, 80);
  eqw(t, L"—", "нет нормы");
}

/* сверка: какие строки маршрута встали в ТП */
static void matching(void) {
  SoRoute r;
  so_parse(L"005 Токарная. Точить начерно\n010 Фрезерная. Лыска\n015 Токарная (чистовая). Точить начисто\n"
           L"020 Контрольная. ОТК\n",
           &r);
  r.it[0].dir = 4478, r.it[1].dir = 4500, r.it[2].dir = 4478, r.it[3].dir = 0; /* контроля нет в справочнике */
  r.cur = 0;
  ok(so_match(&r, 0, 77, L"005") == -1, "операция без справочной — не сверяется");
  ok(so_match(&r, 4478, 77, L"005") == 0 && r.cur == 1 && r.target == 77, "первая вставка: версия запомнена");
  eqw(r.it[0].got, L"005", "номер, под которым встала");
  ok(so_match(&r, 4478, 99, L"010") == -1, "другой ТП после первой вставки не считается");
  ok(so_match(&r, 4478, 77, L"010") == 2 && r.cur == 1, "вставили не по порядку: вторая токарная, буфер на месте");
  ok(so_match(&r, 4478, 77, L"015") == -1, "третьей токарной в маршруте нет");
  ok(so_left(&r) == 2, "осталось две");
  ok(so_match(&r, 4500, 77, L"015") == 1 && r.cur == 3, "фрезерная встала — буфер на контроль");
  r.it[3].state = SO_SKIP;
  ok(so_next_wait(&r, 0) == -1 && so_left(&r) == 0, "контроль пропущен — всё");

  /* текстом: две токарные подряд — первую вставили Ctrl+V, буфер уже на второй */
  so_parse(L"005 Токарная. Начерно\n010 Токарная (чистовая). Начисто\n015 Сверлильная. Отв.\n", &r);
  r.it[0].dir = r.it[1].dir = 4478, r.it[2].dir = 4600;
  r.cur = 0;
  r.it[0].pasted = 1;
  ok(so_advance(&r) == 1, "после Ctrl+V — следующая");
  ok(so_match(&r, 4478, 5, L"005") == 0 && r.cur == 1, "встала вставленная текстом, а не та, что в буфере");
  r.it[1].pasted = 1;
  ok(so_advance(&r) == 2, "дальше — сверлильная");
  r.it[2].pasted = 1;
  ok(so_advance(&r) == -1, "всё подано — ждём базу");
  ok(so_match(&r, 4600, 5, L"015") == 2 && so_match(&r, 4478, 5, L"010") == 1 && so_left(&r) == 0,
     "встали не по порядку — обе отмечены");
}

int main(int argc, char **argv) {
  g_verbose = argc > 1 && !strcmp(argv[1], "-v");
  live_route();
  edited_route();
  matching();
  printf("%d проверок, ошибок %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
