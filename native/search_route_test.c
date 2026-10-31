/* Проверка правил search_route.c без Windows:
   gcc -O2 -Wall -Werror -o search_route_test search_route_test.c && ./search_route_test */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "search_route.c"

static void out8(const wchar_t *w) {
  for (; *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}

static int g_fail, g_n;

static void route(const wchar_t *in, int engine, int why, const wchar_t *q) {
  SrRoute r;
  sr_route(in, &r);
  g_n++;
  if (r.engine == engine && r.why == why && !wcscmp(r.q, q)) return;
  g_fail++;
  printf("FAIL route «");
  out8(in);
  printf("»: %d/%d «", r.engine, r.why);
  out8(r.q);
  printf("», ждали %d/%d «", engine, why);
  out8(q);
  printf("»\n");
}

static void score(const wchar_t *name, const wchar_t *dir, const wchar_t *q, int want) {
  SrWords w;
  wchar_t whole[SR_Q];
  sr_words(q, &w, whole, SR_Q);
  int s = sr_score(name, dir, &w, whole);
  g_n++;
  if (s == want) return;
  g_fail++;
  printf("FAIL score «");
  out8(name);
  printf("» по «");
  out8(q);
  printf("»: %d, ждали %d\n", s, want);
}

static void prep(const wchar_t *in, const wchar_t *want) {
  wchar_t out[SR_Q];
  sr_prep(in, out, SR_Q);
  g_n++;
  if (!wcscmp(out, want)) return;
  g_fail++;
  printf("FAIL prep «");
  out8(in);
  printf("» → «");
  out8(out);
  printf("»\n");
}

static void sb(const wchar_t *in, const wchar_t *want, int fixes) {
  wchar_t s[200];
  wcscpy(s, in);
  int n = dr_des_fix_sb(s);
  g_n++;
  if (n == fixes && !wcscmp(s, want)) return;
  g_fail++;
  printf("FAIL сб «");
  out8(in);
  printf("»: %d «", n);
  out8(s);
  printf("», ждали %d «", fixes);
  out8(want);
  printf("»\n");
}

static void code(const wchar_t *in, const wchar_t codes[][8], int n, const wchar_t *want, int fixes) {
  wchar_t s[100];
  wcscpy(s, in);
  int f = dr_des_fix_code(s, 100, codes, n);
  g_n++;
  if (f == fixes && !wcscmp(s, want)) return;
  g_fail++;
  printf("FAIL код «");
  out8(in);
  printf("»: %d «", f);
  out8(s);
  printf("»\n");
}

static void txt(const wchar_t *in, const wchar_t *want, int fixes, const wchar_t *first) {
  wchar_t s[400], f[60];
  wcscpy(s, in);
  int n = dr_text_fix_sb(s, f, 60);
  g_n++;
  if (n == fixes && !wcscmp(s, want) && !wcscmp(f, first)) return;
  g_fail++;
  printf("FAIL текст «");
  out8(in);
  printf("»: %d «", n);
  out8(s);
  printf("» первое «");
  out8(f);
  printf("», ждали %d «", fixes);
  out8(want);
  printf("»\n");
}

static void dbw(const wchar_t *in, const wchar_t *want) {
  SrWords w;
  wchar_t got[SR_Q] = L"";
  sr_db_words(in, &w, 6);
  for (int i = 0; i < w.n; i++) {
    if (i) wcscat(got, L"|");
    wcscat(got, w.w[i]);
  }
  g_n++;
  if (!wcscmp(got, want)) return;
  g_fail++;
  printf("FAIL db words «");
  out8(in);
  printf("» → «");
  out8(got);
  printf("»\n");
}

int main(void) {
  /* обозначения: полные по шаблону штампа и куски */
  route(L"3422-682.01.01.02", SR_PLM, SR_WHY_DES, L"3422-682.01.01.02");
  route(L"АДЕ 3422-682.01.01.02", SR_PLM, SR_WHY_DES, L"АДЕ 3422-682.01.01.02");
  route(L"ABE 3422-682.01.01.02", SR_PLM, SR_WHY_DES, L"АВЕ 3422-682.01.01.02");
  route(L"ADE 3422-682.01.01.02", SR_PLM, SR_WHY_DES, L"3422-682.01.01.02"); /* D на кириллицу не похожа */
  route(L"ade 3422-682.01.01.02", SR_PLM, SR_WHY_DES, L"3422-682.01.01.02");
  route(L"АДЕ 3422-682.01.01.02-01", SR_PLM, SR_WHY_DES, L"АДЕ 3422-682.01.01.02-01");
  route(L"АБВГ.123456.001", SR_PLM, SR_WHY_DES, L"АБВГ.123456.001");
  route(L"абвг.123456.001", SR_PLM, SR_WHY_DES, L"АБВГ.123456.001");
  route(L"830-01-сб115", SR_PLM, SR_WHY_DES, L"830-01-СБ115");
  route(L"АДЕ.830.МК", SR_PLM, SR_WHY_DES, L"АДЕ.830.МК");
  route(L"682.01", SR_PLM, SR_WHY_DES, L"682.01");
  route(L"830.001", SR_PLM, SR_WHY_DES, L"830.001");
  route(L"3422 - 682", SR_PLM, SR_WHY_DES, L"3422-682");
  route(L"где 682.01.01", SR_PLM, SR_WHY_DES, L"682.01.01");
  route(L"ТП 682.01", SR_PLM, SR_WHY_DES, L"682.01");
  route(L"(682.01.01)", SR_PLM, SR_WHY_DES, L"682.01.01");
  route(L"3422-682.01.01.02 корпус", SR_PLM, SR_WHY_DES, L"3422-682.01.01.02");
  route(L"какой ТП у АДЕ 3422-682.01.01.02?", SR_PLM, SR_WHY_DES, L"АДЕ 3422-682.01.01.02");
  route(L"чертёж 682.01.01", SR_FILES, SR_WHY_DES, L"682.01.01");
  /* не обозначения: дробь, дата, номер стандарта */
  route(L"2.5", SR_PLM, SR_WHY_NAME, L"2.5");
  route(L"100.5", SR_PLM, SR_WHY_NAME, L"100.5");
  route(L"22.10.2026", SR_PLM, SR_WHY_NAME, L"22.10.2026");
  route(L"ГОСТ 14637-89", SR_PLM, SR_WHY_MAT, L"ГОСТ 14637-89");
  route(L"ГОСТ 2.104-2006", SR_PLM, SR_WHY_MAT, L"ГОСТ 2.104-2006");
  route(L"ГОСТ Р 52857.1-2007", SR_PLM, SR_WHY_MAT, L"ГОСТ Р 52857.1-2007");
  /* материалы */
  route(L"Сталь 45", SR_PLM, SR_WHY_MAT, L"Сталь 45");
  route(L"09Г2С", SR_PLM, SR_WHY_MAT, L"09Г2С");
  route(L"Ст3сп", SR_PLM, SR_WHY_MAT, L"Ст3сп");
  route(L"лист 3", SR_PLM, SR_WHY_MAT, L"лист 3");
  route(L"труба 1/2", SR_PLM, SR_WHY_MAT, L"труба 1/2");
  route(L"болт М8", SR_PLM, SR_WHY_MAT, L"болт М8");
  route(L"Лист Б-ПН-3 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89", SR_PLM, SR_WHY_MAT,
        L"Лист Б-ПН-3 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89");
  /* вопросы и болтовня */
  route(L"как сделать фаску в компасе", SR_AI, SR_WHY_ASK, L"как сделать фаску в компасе");
  route(L"почему ржавеет нержавейка", SR_AI, SR_WHY_ASK, L"почему ржавеет нержавейка");
  route(L"что такое HRC?", SR_AI, SR_WHY_ASK, L"что такое HRC?");
  route(L"какая твёрдость у стали 45", SR_AI, SR_WHY_ASK, L"какая твёрдость у стали 45");
  route(L"сравни два файла по обозначению", SR_AI, SR_WHY_ASK, L"сравни два файла по обозначению");
  route(L"напомни потом про отпуск в среду", SR_AI, SR_WHY_ASK, L"напомни потом про отпуск в среду");
  route(L"привет", SR_AI, SR_WHY_TALK, L"привет");
  route(L"спасибо большое", SR_AI, SR_WHY_TALK, L"спасибо большое");
  /* файлы */
  route(L"Корпус.tif", SR_FILES, SR_WHY_PATH, L"Корпус.tif");
  route(L"\\\\srv\\Чертежи", SR_FILES, SR_WHY_PATH, L"\\\\srv\\Чертежи");
  route(L"*.pdf", SR_FILES, SR_WHY_PATH, L"*.pdf");
  route(L"скан паспорта насоса", SR_FILES, SR_WHY_FILE, L"паспорта насоса");
  route(L"чертежи", SR_FILES, SR_WHY_FILE, L"чертежи");
  /* наименования */
  route(L"корпус", SR_PLM, SR_WHY_NAME, L"корпус");
  route(L"окно", SR_PLM, SR_WHY_NAME, L"окно");
  route(L"найди кронштейн опоры", SR_PLM, SR_WHY_NAME, L"кронштейн опоры");
  /* префиксы */
  route(L"?корпус", SR_AI, SR_WHY_PREFIX, L"корпус");
  route(L"# Сталь 45", SR_PLM, SR_WHY_PREFIX, L"Сталь 45");
  route(L"/корпус", SR_FILES, SR_WHY_PREFIX, L"корпус");
  route(L"   ", SR_PLM, SR_WHY_NAME, L"");

  prep(L"3422 - 682", L"3422-682");
  prep(L"3422 —682.01", L"3422-682.01");
  prep(L"  корпус   насоса  ", L"корпус насоса");
  prep(L"45 - лист", L"45 - лист");

  dbw(L"корпус насоса", L"корпус|насоса");
  dbw(L"АДЕ 3422 - 682.01", L"АДЕ|3422-682.01");
  dbw(L"Лист Б-ПН-3 ГОСТ 19903-2015 / Ст3сп ГОСТ 14637-89", L"Лист|Б-ПН-3|ГОСТ|19903-2015|Ст3сп|14637-89");
  dbw(L"а б в г д е ж", L"а|б|в|г|д|е");
  dbw(L"—", L"");

  score(L"Корпус.tif", L"\\\\srv\\А", L"корпус", 110);
  score(L"Корпус насоса.tif", L"\\\\srv\\А", L"корпус", 60);
  score(L"Насос корпус.tif", L"\\\\srv\\А", L"корпус", 10);
  score(L"АДЕ 3422-682.01.01.02 Корпус.tif", L"\\\\srv\\А", L"корпус 682.01.01.02", 20);
  score(L"АДЕ 3422-682.01.01.02 Корпус.tif", L"\\\\srv\\А", L"АДЕ 3422-682.01.01.02", 70);
  score(L"АВЕ 3422-682.01.01.02.tif", L"\\\\srv\\А", L"abe 3422 - 682.01.01.02", 120);
  score(L"Елка.txt", L"\\\\srv\\А", L"ёлка", 110);
  score(L"Корпус.tif", L"\\\\srv\\Насос", L"корпус насос", 10);
  score(L"Корпус.tif", NULL, L"корпус насос", -1);
  score(L"Вал.tif", L"\\\\srv\\А", L"корпус", -1);
  score(L"Деталь.m3d", L"\\\\srv\\А", L"деталь", 110);
  score(L"Корпус.pdf", L"\\\\srv\\А", L"*.pdf", 10);
  score(L"Корпус.pdf", L"\\\\srv\\А", L"корп* *", 60);
  score(L"Корпус.tif", L"\\\\srv\\А", L"*.pdf", -1);

  /* «б» из «СБ», прочитанная как «6» (выдуманные обозначения) */
  sb(L"321-23-с632", L"321-23-сб32", 1);
  sb(L"321-23-С632", L"321-23-СБ32", 1);
  sb(L"АДЕ 321-23-с632", L"АДЕ 321-23-сб32", 1);
  sb(L"321-23-сб32", L"321-23-сб32", 0);
  sb(L"АБВГ.123456.000С6", L"АБВГ.123456.000СБ", 1);
  sb(L"АБВГ.123456.000 С6", L"АБВГ.123456.000 СБ", 1);
  sb(L"АБВГ.123456.000с6", L"АБВГ.123456.000СБ", 1);
  sb(L"АДЕ 3422-682.01.01 С6", L"АДЕ 3422-682.01.01 СБ", 1);
  sb(L"АБВГ.123456.000СБ", L"АБВГ.123456.000СБ", 0);
  sb(L"321-23-сб32С6", L"321-23-сб32СБ", 1);   /* сборочный чертёж спецификации «321-23-сб32» */
  sb(L"321-23-с632С6", L"321-23-сб32СБ", 2);
  sb(L"321-23-сб32СБ", L"321-23-сб32СБ", 0);
  sb(L"М6-с632", L"М6-с632", 0);           /* не обозначение: ядро не сходится */
  sb(L"Труба-с6", L"Труба-с6", 0);
  sb(L"3424-с6", L"3424-с6", 0);
  sb(L"Ст3с6", L"Ст3с6", 0);
  sb(L"14637-89 С6", L"14637-89 С6", 0);   /* номер ГОСТ — не ядро обозначения */
  /* свои буквенные коды: по именам архива и поправка одной буквы (коды выдуманные) */
  {
    const wchar_t *names[40];
    int k = 0;
    for (int i = 0; i < 20; i++) names[k++] = L"КЛМ 1234-567.01.02.tif";
    for (int i = 0; i < 8; i++) names[k++] = L"ПРС5678-012.00.00.01.TIF";
    for (int i = 0; i < 4; i++) names[k++] = L"Сборка.tif";
    names[k++] = L"ГОСТ 1050-2013.pdf";
    names[k++] = L"KLM 1234-999.tif"; /* латиница — тот же код */
    wchar_t codes[SR_CODES][8];
    int n = sr_codes_from_names(names, k, codes, SR_CODES, 5);
    g_n++;
    if (!(n == 2 && !wcscmp(codes[0], L"КЛМ") && !wcscmp(codes[1], L"ПРС"))) {
      g_fail++;
      printf("FAIL коды по именам: %d\n", n);
    }
    code(L"КЛН 1234-567.01.02", codes, n, L"КЛМ 1234-567.01.02", 1);
    code(L"ПЛС5678-012.00.00.01", codes, n, L"ПРС5678-012.00.00.01", 1);
    code(L"КЛМ 1234-567.01.02", codes, n, L"КЛМ 1234-567.01.02", 0);
    code(L"АБВ 1234-567", codes, n, L"АБВ 1234-567", 0); /* чужой, не похож — не трогать */
    code(L"ГОСТ 1050-2013", codes, n, L"ГОСТ 1050-2013", 0);
    code(L"Болт М8", codes, n, L"Болт М8", 0);
    code(L"ГОСЕ 1050-2013", codes, n, L"ГОСЕ 1050-2013", 0);
    code(L"КNМ 1234-567.01.02", codes, n, L"КЛМ 1234-567.01.02", 1); /* латиница без пары */
    code(L"КМ 1234-567.01.02", codes, n, L"КЛМ 1234-567.01.02", 1); /* буква пропала */
    code(L"КЛММ 1234-567", codes, n, L"КЛММ 1234-567", 0);
    wchar_t two[1][8] = {L"ТС"};
    code(L"ТУ 14-1-1234", two, 1, L"ТУ 14-1-1234", 0); /* код стандарта и двухбуквенный — не трогать */
    wchar_t u[SR_CODES][8];
    int un = sr_codes_parse(L"клм, TMX;ГОСТ  Ж ВЕРБЛЮД2", u, SR_CODES);
    g_n++;
    if (!(un == 2 && !wcscmp(u[0], L"КЛМ") && !wcscmp(u[1], L"ТМХ"))) {
      g_fail++;
      printf("FAIL коды из Настроек: %d\n", un);
    }
  }
  /* то же в тексте F6: только обозначения, латинская «c» — тоже */
  txt(L"321-23-с632", L"321-23-сб32", 1, L"321-23-сб32");
  txt(L"Лист 1 АБВГ.123456.000С6 Сборочный чертёж", L"Лист 1 АБВГ.123456.000СБ Сборочный чертёж", 1,
      L"АБВГ.123456.000СБ");
  txt(L"Узел 321-23-c632 (латиница)", L"Узел 321-23-сб32 (латиница)", 1, L"321-23-сб32");
  txt(L"АДЕ 3422-682.01.01\r\nУзел 321-23-с632\r\nс6 листами, С6-ая колонка", L"АДЕ 3422-682.01.01\r\nУзел 321-23-сб32\r\nс6 листами, С6-ая колонка", 1, L"321-23-сб32");
  txt(L"с6 листами, С6-ая колонка, лист 2 С6, Ст3с6", L"с6 листами, С6-ая колонка, лист 2 С6, Ст3с6", 0, L"");

  printf("%d проверок, ошибок: %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
