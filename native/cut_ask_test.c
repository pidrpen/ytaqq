/* cut_ask_test.c — расход на резку по вопросу словами (cut_ask.c): разбор марки, толщины,
   длины реза и количества, ответ по нормам резки завода. Windows не нужен:
     gcc -O2 -Wall -Werror -o cut_ask_test cut_ask_test.c -lm && ./cut_ask_test */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "cutting_calc.c"
#include "techno.c"
#include "norms.c"
#include "cut_ask.c"
#pragma GCC diagnostic pop

static int g_n, g_fail;
static void ok(const char *what, int cond) {
  g_n++;
  if (!cond) {
    g_fail++;
    printf("ОШИБКА: %s\n", what);
  }
}

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

static void one(const char *what, const wchar_t *q, const wchar_t *mat, double t, double len, int qty) {
  CutAsk a;
  cut_ask_parse(q, &a);
  char m[200];
  snprintf(m, sizeof(m), "%s: марка", what);
  ok(m, mat ? a.mat && !wcscmp(a.mat, mat) : !a.mat);
  snprintf(m, sizeof(m), "%s: толщина", what);
  ok(m, near(a.t, t));
  snprintf(m, sizeof(m), "%s: длина реза", what);
  ok(m, near(a.len, len));
  snprintf(m, sizeof(m), "%s: количество", what);
  ok(m, a.qty == qty);
}

int main(void) {
  one("ст3, мм и ×", L"резка ст3 10 мм рез 2400 мм ×20", L"ст3", 10, 2400, 20);
  one("08пс, S=, метры, шт", L"08пс S=3 периметр 1,6 м, 15 шт", L"08пс", 3, 1600, 15);
  one("нержавейка словами", L"расход на резку 12Х18Н10Т толщина 4 длина реза 3200", L"12Х18Н10Т", 4, 3200, 0);
  one("«сталь 45»", L"резка сталь 45 лист 12 мм, контур 980", L"ст45", 12, 980, 0);
  one("s10 и метры", L"резка s10 ст3 2,4 м", L"ст3", 10, 2400, 0);
  one("без подписей", L"резка 09Г2С 8 мм 1500 мм", L"09Г2С", 8, 1500, 0);
  one("марки нет", L"резка листа 6 мм, рез 900", NULL, 6, 900, 0);

  ok("вопрос о резке", cut_ask_wanted(L"резка ст3 10 мм") && cut_ask_wanted(L"длина реза 2400, ст3 4 мм"));
  ok("не о резке", !cut_ask_wanted(L"привет") && !cut_ask_wanted(L"норма резки L 120"));

  static wchar_t out[8000];
  ok("ответ посчитан", cut_answer(L"резка ст3 10 мм рез 2400 мм ×20", out, 8000) == 1);
  ok("ответ: по нормам резки", wcsstr(out, L"Расчёт резки: ст3 10 мм, L=48") && wcsstr(out, L"Скорость:") &&
                                   wcsstr(out, L"Время:"));
  ok("ответ: количество", wcsstr(out, L"20 шт × 2") && wcsstr(out, L"мм реза"));
  ok("не хватает — что назвать", cut_answer(L"резка листа", out, 8000) == 0 && wcsstr(out, L"марку") &&
                                     wcsstr(out, L"толщину листа") && wcsstr(out, L"ст3"));
  ok("марки нет в нормах", cut_answer(L"резка сталь 20Х13 4 мм рез 500", out, 8000) == 0 && wcsstr(out, L"20х13"));
  printf("cut_ask_test: %d проверок, ошибок %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
