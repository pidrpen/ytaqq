/* store_text_test.c — записи истории: экранирование, разбор, битые строки.
   Сборка и запуск на любой машине с gcc:
     gcc -Wall -Werror -o store_text_test store_text_test.c && ./store_text_test */
#include <stdio.h>
#include <wchar.h>
#include "store_text.h"

static int failed;
#define CHECK(c)                                                                   \
  do {                                                                             \
    if (!(c)) {                                                                    \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                 \
      failed = 1;                                                                  \
    }                                                                              \
  } while (0)

static void round_trip(const wchar_t *text) {
  StBuf b = {0};
  st_quoted(&b, text);
  CHECK(b.s && wcschr(b.s, L'\n') == NULL);
  wchar_t tag = 0;
  wchar_t line[1024];
  swprintf(line, 1024, L"1\t%ls", b.s);
  wchar_t *back = st_record(line, &tag);
  CHECK(back != NULL);
  CHECK(tag == L'1');
  CHECK(back && wcscmp(back, text) == 0);
  free(back);
  free(b.s);
}

int main(void) {
  round_trip(L"");
  round_trip(L"полка 40Х");
  round_trip(L"сказал \"да\" и ушёл \\ назад");
  round_trip(L"первая строка\nвторая строка\r\nтретья\tс табом");
  round_trip(L"управляющий \x01 знак и \x1f");

  /* несколько записей подряд: строки читаются по одной, перенос внутри текста не рвёт файл */
  StBuf file = {0};
  StBuf one = {0};
  st_char(&one, L'0');
  st_char(&one, L'\t');
  st_quoted(&one, L"ответ\nна\nтри строки");
  st_char(&one, L'\n');
  st_cat(&file, one.s);
  free(one.s);
  memset(&one, 0, sizeof(one));
  st_char(&one, L'1');
  st_char(&one, L'\t');
  st_quoted(&one, L"вопрос");
  st_char(&one, L'\n');
  st_cat(&file, one.s);
  free(one.s);

  int count = 0;
  for (const wchar_t *p = file.s; *p; p = st_next(p)) {
    wchar_t tag = 0;
    wchar_t *text = st_record(p, &tag);
    if (text) count++;
    if (count == 1) CHECK(tag == L'0' && wcscmp(text, L"ответ\nна\nтри строки") == 0);
    if (count == 2) CHECK(tag == L'1' && wcscmp(text, L"вопрос") == 0);
    free(text);
  }
  CHECK(count == 2);
  free(file.s);

  /* битые строки: пропускаются, не падают */
  wchar_t tag = 0;
  wchar_t *bad;
  bad = st_record(L"2\t\"метка не та\"", &tag);
  CHECK(bad == NULL);
  bad = st_record(L"1 без табуляции", &tag);
  CHECK(bad == NULL);
  bad = st_record(L"1\t\"не закрыта", &tag);
  CHECK(bad == NULL);
  bad = st_record(L"1\t\"плохой \\q\"", &tag);
  CHECK(bad == NULL);
  bad = st_record(L"1\t\"плохой \\u12G4\"", &tag);
  CHECK(bad == NULL);
  bad = st_record(L"1\t\"обрыв \\u12", &tag);
  CHECK(bad == NULL);
  bad = st_record(L"", &tag);
  CHECK(bad == NULL);

  if (failed) return 1;
  printf("PASS: store_text round trip, escapes, multi-line file, broken lines skipped\n");
  return 0;
}
