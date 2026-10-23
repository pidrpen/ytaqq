/* store_text.h — строки файлов истории: чат ИИ (ai_chat.txt) и история поиска
   (search_history.txt). Одна запись — одна строка «метка<TAB>"текст"». Текст в
   кавычках, как в JSON: \" \\ \n \r \t и \u00XX для прочих управляющих знаков.
   Так перенос строки внутри реплики не рвёт файл, а битая строка пропускается,
   остальные читаются. Только стандартная библиотека: собирается и в тестах под
   Linux (store_text_test.c). */
#ifndef CURSORPAD_STORE_TEXT_H
#define CURSORPAD_STORE_TEXT_H

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct {
  wchar_t *s; /* с нулём в конце, когда что-то записано */
  size_t n, cap;
} StBuf;

static int st_grow(StBuf *b, size_t extra) {
  size_t need = b->n + extra + 1, cap = b->cap ? b->cap : 256;
  if (need <= b->cap) return 1;
  while (cap < need) cap *= 2;
  wchar_t *t = (wchar_t *)realloc(b->s, cap * sizeof(wchar_t));
  if (!t) return 0;
  b->s = t;
  b->cap = cap;
  return 1;
}

static void st_char(StBuf *b, wchar_t c) {
  if (!st_grow(b, 1)) return;
  b->s[b->n++] = c;
  b->s[b->n] = 0;
}

static void st_cat(StBuf *b, const wchar_t *s) {
  size_t k = wcslen(s);
  if (!st_grow(b, k)) return;
  memcpy(b->s + b->n, s, k * sizeof(wchar_t));
  b->n += k;
  b->s[b->n] = 0;
}

/* строка в кавычках, экранирована; без переносов строк внутри записи */
static void st_quoted(StBuf *b, const wchar_t *s) {
  static const wchar_t hex[] = L"0123456789abcdef";
  st_char(b, L'"');
  for (; *s; s++) {
    wchar_t c = *s;
    if (c == L'"' || c == L'\\') {
      st_char(b, L'\\');
      st_char(b, c);
    } else if (c == L'\n') {
      st_cat(b, L"\\n");
    } else if (c == L'\r') {
      st_cat(b, L"\\r");
    } else if (c == L'\t') {
      st_cat(b, L"\\t");
    } else if (c < 32) {
      st_cat(b, L"\\u00");
      st_char(b, hex[(c >> 4) & 15]);
      st_char(b, hex[c & 15]);
    } else {
      st_char(b, c);
    }
  }
  st_char(b, L'"');
}

static int st_hex(wchar_t c) {
  if (c >= L'0' && c <= L'9') return c - L'0';
  if (c >= L'a' && c <= L'f') return c - L'a' + 10;
  if (c >= L'A' && c <= L'F') return c - L'A' + 10;
  return -1;
}

/* строка в кавычках с начала p; *end — за закрывающей кавычкой. NULL — не разобрать.
   Результат — malloc, освободить free(). */
static wchar_t *st_unquote(const wchar_t *p, const wchar_t **end) {
  if (*p != L'"') return NULL;
  wchar_t *out = (wchar_t *)malloc((wcslen(p) + 1) * sizeof(wchar_t));
  if (!out) return NULL;
  size_t n = 0;
  for (p++; *p; p++) {
    wchar_t c = *p;
    if (c == L'"') {
      out[n] = 0;
      *end = p + 1;
      return out;
    }
    if (c != L'\\') {
      out[n++] = c;
      continue;
    }
    c = *++p;
    if (c == L'n') {
      out[n++] = L'\n';
    } else if (c == L'r') {
      out[n++] = L'\r';
    } else if (c == L't') {
      out[n++] = L'\t';
    } else if (c == L'"' || c == L'\\' || c == L'/') {
      out[n++] = c;
    } else if (c == L'u') {
      int v = 0;
      for (int k = 1; k <= 4; k++) {
        int d = st_hex(p[k]);
        if (d < 0) {
          free(out);
          return NULL;
        }
        v = v * 16 + d;
      }
      p += 4;
      out[n++] = (wchar_t)v;
    } else {
      free(out);
      return NULL;
    }
  }
  free(out);
  return NULL;
}

/* разбор записи «0<TAB>"текст"» или «1<TAB>"текст"»: метка — в *tag, текст — malloc.
   NULL — строка не запись. */
static wchar_t *st_record(const wchar_t *p, wchar_t *tag) {
  const wchar_t *end = NULL;
  if ((p[0] != L'0' && p[0] != L'1') || p[1] != L'\t' || p[2] != L'"') return NULL;
  wchar_t *s = st_unquote(p + 2, &end);
  if (s) *tag = p[0];
  return s;
}

/* начало следующей строки; у последней — конец строки */
static const wchar_t *st_next(const wchar_t *p) {
  const wchar_t *e = wcschr(p, L'\n');
  return e ? e + 1 : p + wcslen(p);
}

#endif
