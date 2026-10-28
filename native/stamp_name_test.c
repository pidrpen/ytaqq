/* stamp_name_test.c — имя файла чертежа по штампу (stamp_name.h): без Win32, в CI.
   gcc -O2 -Wall -Werror -o stamp_name_test stamp_name_test.c && ./stamp_name_test */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "stamp_name.h"

static int g_fail, g_n;
static void eq(const wchar_t *got, const wchar_t *want, const char *what) {
  g_n++;
  if (wcscmp(got, want)) {
    g_fail++;
    printf("FAIL %s: got «%ls», want «%ls»\n", what, got, want);
  }
}
static void ok(int cond, const char *what) {
  g_n++;
  if (!cond) {
    g_fail++;
    printf("FAIL %s\n", what);
  }
}

/* занятые имена для sn_unique */
static const wchar_t *g_taken[8];
static int taken(const wchar_t *name, void *ctx) {
  (void)ctx;
  for (int i = 0; g_taken[i]; i++)
    if (!wcscmp(g_taken[i], name)) return 1;
  return 0;
}

int main(void) {
  wchar_t s[300], out[300];

  wcscpy(s, L"  Корпус: верхний/нижний?  ");
  sn_clean(s);
  eq(s, L"Корпус верхний нижний", "clean: запрещённые знаки и пробелы");
  wcscpy(s, L"..АДЕ 3422-682.01.01. ");
  sn_clean(s);
  eq(s, L"АДЕ 3422-682.01.01", "clean: точки по краям");
  wcscpy(s, L"Вал\tшестерня\r\n");
  sn_clean(s);
  eq(s, L"Вал шестерня", "clean: табуляция и перевод строки");
  wcscpy(s, L" . ");
  ok(sn_clean(s) == 0 && !s[0], "clean: только мусор — пусто");

  ok(sn_build(L"АДЕ 3422-682.01.01", L"Корпус", L".tif", out, 300), "build: обычное");
  eq(out, L"АДЕ 3422-682.01.01 Корпус.tif", "build: обозначение и наименование");
  ok(sn_build(L"АДЕ 3422-682.01.01", L"", L".TIF", out, 300), "build: без наименования");
  eq(out, L"АДЕ 3422-682.01.01.TIF", "build: только обозначение, расширение как было");
  ok(sn_build(L"ИВК 1234-001", L"Втулка", L"", out, 300), "build: без расширения");
  eq(out, L"ИВК 1234-001 Втулка", "build: без расширения — текст");
  ok(!sn_build(L"  ?? ", L"Корпус", L".tif", out, 300), "build: обозначения нет — отказ");
  ok(!sn_build(L"", L"Корпус", L".tif", out, 300), "build: пустое обозначение — отказ");
  ok(sn_build(L"АДЕ 3422-682.01.01", L"Кронштейн / опора: \"левая\"", L".png", out, 300), "build: знаки в наименовании");
  eq(out, L"АДЕ 3422-682.01.01 Кронштейн опора левая.png", "build: знаки убраны");
  ok(!sn_build(L"АДЕ 3422-682.01.01", L"Корпус", L".tif", out, 10), "build: не влезает в cap — отказ");

  /* длинное наименование режется по слову, обозначение целиком */
  wchar_t longName[400] = L"";
  for (int i = 0; i < 30; i++) wcscat(longName, L"длинное ");
  wcscat(longName, L"слово");
  ok(sn_build(L"АДЕ 3422-682.01.01", longName, L".tif", out, 300), "build: длинное наименование");
  size_t stem = wcslen(out) - 4;
  ok(stem <= SN_MAX, "build: длина имени не больше SN_MAX");
  ok(!wcsncmp(out, L"АДЕ 3422-682.01.01 ", 19), "build: обозначение в начале целиком");
  ok(out[stem - 1] != L' ' && !wcsstr(out, L"  ") && wcsstr(out, L"длинное.tif"), "build: обрезано по целому слову");
  /* одно длинное слово — режется само */
  wchar_t word[400] = L"";
  for (int i = 0; i < 200; i++) wcscat(word, L"я");
  ok(sn_build(L"АДЕ 1", word, L".tif", out, 300) && wcslen(out) - 4 <= SN_MAX, "build: одно длинное слово");

  g_taken[0] = L"АДЕ 1 Корпус.tif";
  g_taken[1] = L"АДЕ 1 Корпус (2).tif";
  g_taken[2] = NULL;
  ok(sn_unique(L"АДЕ 1 Корпус.tif", taken, NULL, out, 300), "unique: нашлось");
  eq(out, L"АДЕ 1 Корпус (3).tif", "unique: номер после занятых");
  ok(sn_unique(L"АДЕ 2 Вал.tif", taken, NULL, out, 300), "unique: свободно сразу");
  eq(out, L"АДЕ 2 Вал.tif", "unique: имя как есть");
  g_taken[0] = L"АДЕ 1";
  g_taken[1] = NULL;
  ok(sn_unique(L"АДЕ 1", taken, NULL, out, 300), "unique: без расширения");
  eq(out, L"АДЕ 1 (2)", "unique: номер в конце без расширения");
  g_taken[0] = L"АДЕ 1 Корпус.tif";
  ok(!sn_unique(L"АДЕ 1 Корпус.tif", taken, NULL, out, 18), "unique: занято, номер не влезает — отказ");
  ok(!sn_unique(L"АДЕ 1 Корпус.tif", taken, NULL, out, 10), "unique: само имя не влезает — отказ");

  if (g_fail) {
    printf("stamp_name_test: %d проверок, ошибок %d\n", g_n, g_fail);
    return 1;
  }
  printf("stamp_name_test: %d проверок, ошибок 0\n", g_n);
  return 0;
}
