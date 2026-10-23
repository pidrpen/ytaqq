/* Проверка сверки с Союзом (plm_check.c) без Windows и без PLM:
   gcc -O2 -Wall -Werror -o plm_check_test plm_check_test.c && ./plm_check_test   (-v — показать итог)
   Строки PLM — в том виде, как их отдаёт запрос «check»: имя объекта,
   атрибуты Designation и Name. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "search_route.c"
#include "plm_check.c"

static void out8(const wchar_t *w) {
  for (; w && *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}

static int g_fail, g_n, g_verbose;

static void pc_cat(wchar_t *d, int cap, const wchar_t *s) {
  int n = (int)wcslen(d);
  while (s && *s && n < cap - 1) d[n++] = *s++;
  d[n] = 0;
}
static PcItem g_items[PC_MAX];

static void fail_head(const char *what, const wchar_t *in) {
  g_fail++;
  printf("FAIL %s «", what);
  out8(in);
  printf("»");
}

/* разбор: want — «обозначение=наименование|…» */
static void parse(const wchar_t *in, int chat, const wchar_t *want) {
  PcList l = {g_items, 0, 0, 0};
  pc_parse(&l, in, chat);
  wchar_t got[2000] = L"";
  for (int i = 0; i < l.n; i++) {
    if (i) pc_cat(got, 2000, L"|");
    pc_cat(got, 2000, l.it[i].des);
    pc_cat(got, 2000, L"=");
    pc_cat(got, 2000, l.it[i].name);
  }
  g_n++;
  if (!wcscmp(got, want)) return;
  fail_head("parse", in);
  printf(": «");
  out8(got);
  printf("», ждали «");
  out8(want);
  printf("»\n");
}

static void name_cmp(const wchar_t *a, const wchar_t *b, int want) {
  int r = pc_name_cmp(a, b);
  g_n++;
  if (r == want) return;
  fail_head("name", a);
  printf(" и «");
  out8(b);
  printf("»: %d, ждали %d\n", r, want);
}

static void des_cmp(const wchar_t *item, const wchar_t *row, int want) {
  wchar_t code[PC_CODE], core[PC_DES];
  pc_split_des(item, code, core);
  int r = pc_des_cmp(code, core, row);
  g_n++;
  if (r == want) return;
  fail_head("des", item);
  printf(" и «");
  out8(row);
  printf("»: %d, ждали %d\n", r, want);
}

static void row(const wchar_t *raw, const wchar_t *d, const wchar_t *n, const wchar_t *wantDes,
                const wchar_t *wantName) {
  PcRow r;
  pc_row_set(&r, 1, 1767, raw, d, n);
  g_n++;
  if (!wcscmp(r.des, wantDes) && !wcscmp(r.name, wantName)) return;
  fail_head("row", raw);
  printf(": «");
  out8(r.des);
  printf("» «");
  out8(r.name);
  printf("», ждали «");
  out8(wantDes);
  printf("» «");
  out8(wantName);
  printf("»\n");
}

/* Позиция из списка и строки PLM («имя\tDesignation\tName», через «|») → итог */
static void judge(const wchar_t *line, const wchar_t *rows, int want) {
  PcList l = {g_items, 0, 0, 0};
  pc_parse(&l, line, 0);
  if (l.n != 1) {
    g_n++;
    fail_head("judge: не одна позиция", line);
    printf("\n");
    return;
  }
  wchar_t buf[2000];
  wcsncpy(buf, rows, 1999);
  buf[1999] = 0;
  long id = 100;
  for (wchar_t *save = NULL, *r = wcstok(buf, L"|", &save); r; r = wcstok(NULL, L"|", &save)) {
    wchar_t *f[3] = {r, L"", L""};
    wchar_t *t = wcschr(r, L'\t');
    if (t) *t = 0, f[1] = t + 1, t = wcschr(f[1], L'\t');
    if (t) *t = 0, f[2] = t + 1;
    pc_row_add(&l.it[0], id++, 1767, f[0], f[1], f[2]);
  }
  pc_judge(&l.it[0]);
  g_n++;
  if (g_verbose) {
    wchar_t *rep = pc_report(&l, L"проверка", NULL);
    out8(rep);
    printf("\n\n");
    free(rep);
  }
  if (l.it[0].state == want) return;
  fail_head("judge", line);
  printf(": %d, ждали %d\n", l.it[0].state, want);
}

int main(int argc, char **argv) {
  g_verbose = argc > 1 && !strcmp(argv[1], "-v");
  (void)sr_route, (void)sr_score, (void)sr_db_words, (void)sr_words; /* поиск здесь не проверяется */

  /* ---- разбор списка ---- */
  /* буфер Excel: поз., формат, обозначение, наименование, кол. */
  parse(L"Поз.\tФормат\tОбозначение\tНаименование\tКол.\n"
        L"1\tА4\tАДЕ 3424-123.01\tКронштейн\t2\n"
        L"2\tА3\tАДЕ 3424-124.01\tПланка опорная\t1\n",
        0, L"АДЕ 3424-123.01=Кронштейн|АДЕ 3424-124.01=Планка опорная");
  /* наименование левее обозначения */
  parse(L"Кронштейн\tАДЕ 3424-123.01\t2", 0, L"АДЕ 3424-123.01=Кронштейн");
  /* CSV через «;» и латиница в коде */
  parse(L"ABE 3424-123.01;Кронштейн;2\n", 0, L"АВЕ 3424-123.01=Кронштейн");
  /* без табуляций: обозначение и наименование в одной строке */
  parse(L"АДЕ 3424-123.01 Кронштейн\nАДЕ 3424-124.01 - Планка, 2 шт.\n", 0,
        L"АДЕ 3424-123.01=Кронштейн|АДЕ 3424-124.01=Планка");
  /* повтор не считается, наименование берётся у повтора, если у первого нет */
  parse(L"АДЕ 3424-123.01\nАДЕ 3424-123.01 Кронштейн\n", 0, L"АДЕ 3424-123.01=Кронштейн");
  /* код прилип к числу */
  parse(L"АДЕ3424-123.01\tКронштейн\n", 0, L"АДЕ 3424-123.01=Кронштейн");
  /* исполнение — своя позиция */
  parse(L"АДЕ 3424-123.01\nАДЕ 3424-123.01-01\n", 0, L"АДЕ 3424-123.01=|АДЕ 3424-123.01-01=");
  /* стандартные изделия по ГОСТ — не обозначения */
  parse(L"Болт М12х40 ГОСТ 7798-70\t4\nАДЕ 3424-123.01\tОсь\n", 0, L"АДЕ 3424-123.01=Ось");

  /* чат: слова вопроса не наименование */
  parse(L"есть ли в союзе АДЕ 3424-123.01?", 1, L"АДЕ 3424-123.01=");
  parse(L"есть ли АДЕ 3424-123.01 в союзе?", 1, L"АДЕ 3424-123.01=");
  parse(L"проверь в союзе наличие обозначения АДЕ 3424-123.01 и наименование Кронштейн", 1,
        L"АДЕ 3424-123.01=Кронштейн");
  parse(L"есть ли в союзе деталь АДЕ 3424-123.01", 1, L"АДЕ 3424-123.01=");
  parse(L"есть ли в Союзе кронштейн АДЕ 3424-123.01", 1, L"АДЕ 3424-123.01=кронштейн");
  parse(L"Сверь с союзом: АДЕ 3424-123.01 Кронштейн опорный, АДЕ 3424-124.01 Планка и 3424-125.02 «Ось»", 1,
        L"АДЕ 3424-123.01=Кронштейн опорный|АДЕ 3424-124.01=Планка|3424-125.02=Ось");
  parse(L"есть в PLM 682.01.01 «Корпус насоса»?", 1, L"682.01.01=Корпус насоса");
  /* без обозначений — позиция по наименованию */
  parse(L"есть ли в союзе кронштейн опорный?", 1, L"=кронштейн опорный");
  parse(L"есть ли в союзе?", 1, L"");
  /* длинная строка без табуляций режется по запятым */
  {
    wchar_t big[4000] = L"";
    for (int i = 0; i < 30; i++) {
      wchar_t one[80];
      swprintf(one, 80, L"%lsАДЕ 3424-%03d.01 Планка", i ? L", " : L"", 100 + i);
      wcscat(big, one);
    }
    PcList l = {g_items, 0, 0, 0};
    pc_parse(&l, big, 1);
    g_n++;
    if (l.n != 30 || wcscmp(l.it[29].name, L"Планка")) {
      g_fail++;
      printf("FAIL длинная строка: %d позиций\n", l.n);
    }
  }

  /* штамп и спецификация чертежа: графы распознаны отдельно */
  {
    PcList l = {g_items, 0, 0, 0};
    int a = pc_add_field(&l, L"АДЕ 3424-123.00 СБ", L"Кронштейн ", 1, 0);
    int b = pc_add_field(&l, L"АДЕ3424-123.01", L"Ось", 2, 1);
    int c = pc_add_field(&l, L"", L"Болт М12х40 ГОСТ 7798-70", 3, 0);
    g_n++;
    if (!a || !b || c || l.n != 2 || wcscmp(l.it[0].des, L"АДЕ 3424-123.00") || wcscmp(l.it[0].name, L"Кронштейн") ||
        !l.it[1].doubt) {
      g_fail++;
      printf("FAIL графы чертежа: %d %d %d, позиций %d «", a, b, c, l.n);
      out8(l.n ? l.it[0].des : L"");
      printf("»\n");
    }
  }

  /* ---- обозначения ---- */
  des_cmp(L"АДЕ 3424-123.01", L"АДЕ 3424-123.01", 2);
  des_cmp(L"3424-123.01", L"АДЕ 3424-123.01", 2);
  des_cmp(L"АДЕ 3424-123.01", L"3424-123.01", 2);
  des_cmp(L"АДЕ 3424-123.01", L"АДЕ 3424-123.01 [Кронштейн]", 2);
  des_cmp(L"АДЕ 3424-123.01", L"СГИ 3424-123.01", 1);
  des_cmp(L"АДЕ 3424-123.01-01", L"АДЕ 3424-123.01", 1);
  des_cmp(L"АДЕ 3424-123.01", L"АДЕ 3424-123.01.02", 1);
  des_cmp(L"АДЕ 3424-123.01", L"АДЕ 3424-123.010", 0);
  des_cmp(L"АДЕ 3424-123.01", L"АДЕ 3424-124.01", 0);

  /* ---- наименования ---- */
  name_cmp(L"Кронштейн", L"КРОНШТЕЙН", 2);
  name_cmp(L"Ёлка", L"елка", 2);
  name_cmp(L"Кронштейн опорный", L"Опорный кронштейн", 1);
  name_cmp(L"Кронш. опорный", L"Кронштейн опорный", 1);
  name_cmp(L"Кронштейн опорная", L"Кронштейн опорный", 1);
  name_cmp(L"Кронштейн", L"Кронштейн опорный", 0);
  name_cmp(L"Кронштейн", L"Планка", 0);
  name_cmp(L"Кронштейн отверстием", L"Кронштейн с отверстием", 2);
  name_cmp(L"Шайба 12", L"Шайба 120", 0);

  /* ---- строки PLM ---- */
  row(L"АДЕ 3424-123.01 [Кронштейн]", L"", L"", L"АДЕ 3424-123.01", L"Кронштейн");
  row(L"АДЕ 3424-123.01 [Шаблон]:1", L"АДЕ 3424-123.01", L"Кронштейн опорный", L"АДЕ 3424-123.01",
      L"Кронштейн опорный");
  row(L"АДЕ 3424-123.01 Кронштейн", L"", L"", L"АДЕ 3424-123.01", L"Кронштейн");
  row(L"АДЕ 3424-123.01", L"", L"АДЕ 3424-123.01 Кронштейн", L"АДЕ 3424-123.01", L"Кронштейн");
  row(L"АДЕ 3424-123.01", L"", L"", L"АДЕ 3424-123.01", L"");

  /* ---- итог позиции ---- */
  judge(L"АДЕ 3424-123.01\tКронштейн", L"АДЕ 3424-123.01 [Кронштейн]", PCS_OK);
  judge(L"АДЕ 3424-123.01\tКронштейн", L"АДЕ 3424-123.01 [Кронштейн опорный]", PCS_DIFF);
  judge(L"АДЕ 3424-123.01\tОпорный кронштейн", L"АДЕ 3424-123.01 [Кронштейн опорный]", PCS_NEAR);
  judge(L"АДЕ 3424-123.01", L"АДЕ 3424-123.01 [Кронштейн]", PCS_OK);
  judge(L"АДЕ 3424-123.01\tКронштейн", L"", PCS_NONE);
  judge(L"АДЕ 3424-123.01\tКронштейн", L"АДЕ 3424-123.01.01 [Ось]|АДЕ 3424-123.01.02 [Втулка]", PCS_SIMILAR);
  judge(L"АДЕ 3424-123.01-01\tКронштейн", L"АДЕ 3424-123.01 [Кронштейн]", PCS_SIMILAR);
  /* у изделия и конфигурации одно обозначение — берётся строка с тем же наименованием */
  judge(L"АДЕ 3424-123.01\tКронштейн",
        L"АДЕ 3424-123.01 [Шаблон]:1|АДЕ 3424-123.01.01 [Ось]|АДЕ 3424-123.01 [Кронштейн]", PCS_OK);
  /* дочерние первыми пришли из базы — точное всё равно находится */
  judge(L"3424-123.01\tКронштейн", L"АДЕ 3424-123.01.01 [Ось]|АДЕ 3424-123.01.02 [Втулка]|АДЕ 3424-123.01.03 "
                                   L"[Ось]|АДЕ 3424-123.01.04 [Ось]|АДЕ 3424-123.01.05 [Ось]|АДЕ 3424-123.01.06 "
                                   L"[Ось]|АДЕ 3424-123.01.07 [Ось]|АДЕ 3424-123.01 [Кронштейн]",
        PCS_OK);

  /* ---- итог целиком ---- */
  {
    PcList l = {g_items, 0, 0, 0};
    pc_parse(&l, L"АДЕ 3424-123.01\tКронштейн\nАДЕ 3424-124.01\tПланка\nАДЕ 3424-125.01\tОсь\n", 0);
    pc_row_add(&l.it[0], 11, 1767, L"АДЕ 3424-123.01 [Кронштейн]", L"", L"");
    pc_row_add(&l.it[1], 12, 1767, L"АДЕ 3424-124.01 [Планка прижимная]", L"", L"");
    for (int i = 0; i < 2; i++) pc_judge(&l.it[i]);
    l.it[2].state = PCS_FAIL;
    wchar_t *rep = pc_report(&l, L"буфер обмена", L"PLM не ответил вовремя");
    g_n++;
    if (!rep || !wcsstr(rep, L"Наименование другое") || !wcsstr(rep, L"«Планка прижимная» (IO.12)") ||
        !wcsstr(rep, L"Не проверено — PLM не ответил вовремя") || !wcsstr(rep, L"ничего не изменено") ||
        wcsstr(rep, L"Нет в Союзе")) {
      g_fail++;
      printf("FAIL итог:\n");
      out8(rep);
      printf("\n");
    } else if (g_verbose) {
      out8(rep);
      printf("\n\n");
    }
    free(rep);
    for (int i = 0; i < 3; i++) l.it[i].state = PCS_FAIL;
    rep = pc_report(&l, L"", L"нет связи");
    g_n++;
    if (!rep || !wcsstr(rep, L"не вышло: нет связи")) {
      g_fail++;
      printf("FAIL итог без связи: ");
      out8(rep);
      printf("\n");
    }
    free(rep);
  }

  printf("plm_check: %d проверок, ошибок %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
