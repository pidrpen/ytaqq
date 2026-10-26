/* ai_think.h — размышление модели отдельно от ответа. Чистая логика, без Win32:
   её проверяет ai_think_test.c (в CI). Модель с enable_thinking пишет рассуждения
   либо в поле reasoning_content, либо прямо в текст: «…</think>ответ» (открывающий
   тег уже в шаблоне Qwen3) или «<think>…</think>ответ». Размышление, которое
   оборвалось по лимиту токенов, — «<think>…» без закрытия: ответа в нём нет. */
#ifndef CURSORPAD_AI_THINK_H
#define CURSORPAD_AI_THINK_H

#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#define AI_THINK_TOKENS 2048 /* сверх ответа: сколько токенов можно отдать размышлению */
#define AI_THINK_MS 240000   /* сверх срока ответа: на эти токены нужно время, особенно на ЦП */

/* вынуть размышление из t: в *think — malloc без пробелов по краям (или NULL),
   в t остаётся ответ без тегов */
static void ai_think_split(wchar_t *t, wchar_t **think) {
  static const wchar_t kOpen[] = L"<think>", kClose[] = L"</think>";
  *think = NULL;
  wchar_t *open = wcsstr(t, kOpen), *close = wcsstr(t, kClose);
  if (!open && !close) return;
  if (open && close && close < open) close = NULL;
  wchar_t *from = open ? open + 7 : t;
  wchar_t *to = close ? close : t + wcslen(t);
  while (from < to && iswspace(*from)) from++;
  while (to > from && iswspace(to[-1])) to--;
  if (to > from) {
    size_t n = (size_t)(to - from);
    *think = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (*think) {
      wmemcpy(*think, from, n);
      (*think)[n] = 0;
    }
  }
  /* ответ: всё до открывающего тега и всё после закрывающего */
  size_t head = open ? (size_t)(open - t) : 0;
  if (close) memmove(t + head, close + 8, (wcslen(close + 8) + 1) * sizeof(wchar_t));
  else t[head] = 0;
  wchar_t *s = t;
  while (*s && iswspace(*s)) s++;
  if (s != t) memmove(t, s, (wcslen(s) + 1) * sizeof(wchar_t));
  size_t n = wcslen(t);
  while (n && iswspace(t[n - 1])) t[--n] = 0;
}

#endif
