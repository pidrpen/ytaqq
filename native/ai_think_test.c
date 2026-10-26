/* ai_think_test.c — размышление отделяется от ответа (ai_think.h). Без Win32. */
#include <stdio.h>
#include <string.h>
#include "ai_think.h"

static int fails;

static void check(int no, const wchar_t *in, const wchar_t *answer, const wchar_t *think) {
  wchar_t buf[256];
  wcsncpy(buf, in, 255);
  buf[255] = 0;
  wchar_t *got = NULL;
  ai_think_split(buf, &got);
  int ok = !wcscmp(buf, answer) && (think ? got && !wcscmp(got, think) : !got);
  if (!ok) {
    fails++;
    printf("FAIL %d\n", no);
  }
  free(got);
}

int main(void) {
  check(1, L"Просто ответ", L"Просто ответ", NULL);
  check(2, L"", L"", NULL);
  check(3, L"<think>\nДумаю над ответом\n</think>\n\nОтвет", L"Ответ", L"Думаю над ответом");
  check(4, L"Думаю так</think>Ответ", L"Ответ", L"Думаю так");
  check(5, L"<think>Недописано до лимита", L"", L"Недописано до лимита");
  check(6, L"Текст <think>x</think> хвост", L"Текст  хвост", L"x");
  check(7, L"<think>   </think>Ответ", L"Ответ", NULL);
  check(8, L"</think>Ответ", L"Ответ", NULL);
  printf(fails ? "ai_think_test: ошибок %d\n" : "ai_think_test: все проверки прошли\n", fails);
  return fails ? 1 : 0;
}
