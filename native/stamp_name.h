/* stamp_name.h — имя файла чертежа по штампу: «ОБОЗНАЧЕНИЕ Наименование.ext» (инструмент
   rename_by_stamp в ai_app_tools.c). Чистая логика без Win32: её проверяет stamp_name_test.c
   (в CI). Обозначение и наименование приходят из распознавания (drawrec.c) и могут содержать
   мусор — знаки, запрещённые в именах Windows, лишние пробелы, точки по краям. */
#ifndef CURSORPAD_STAMP_NAME_H
#define CURSORPAD_STAMP_NAME_H

#include <stddef.h>
#include <wchar.h>
#include <wctype.h>

#define SN_MAX 120 /* символов в имени без расширения: длинное наименование режется по слову */

/* убрать из s запрещённые в именах Windows знаки и управляющие (в пробел), схлопнуть
   пробелы, срезать пробелы и точки по краям; возвращает длину */
static size_t sn_clean(wchar_t *s) {
  size_t o = 0;
  int space = 1;
  for (const wchar_t *p = s; *p; p++) {
    wchar_t c = *p;
    if (c < 32 || wcschr(L"\\/:*?\"<>|", c) || iswspace(c)) c = L' ';
    if (c == L' ') {
      if (!space) s[o++] = L' ';
      space = 1;
    } else {
      s[o++] = c;
      space = 0;
    }
  }
  s[o] = 0;
  while (o && (s[o - 1] == L' ' || s[o - 1] == L'.')) s[--o] = 0;
  size_t k = 0;
  while (s[k] == L' ' || s[k] == L'.') k++;
  if (k) {
    wmemmove(s, s + k, o - k + 1);
    o -= k;
  }
  return o;
}

/* имя файла «обозначение[ наименование]ext»; ext — с точкой («.tif») или пусто.
   0 — обозначения нет (после очистки пусто) или не влезает в cap.
   Длиннее SN_MAX — наименование режется по последнему пробелу, обозначение — никогда */
static int sn_build(const wchar_t *des, const wchar_t *name, const wchar_t *ext, wchar_t *out, size_t cap) {
  wchar_t d[SN_MAX + 1], n[SN_MAX * 2 + 2];
  size_t ld = wcslen(des), ln = wcslen(name);
  if (ld > SN_MAX) ld = SN_MAX;
  if (ln > SN_MAX * 2) ln = SN_MAX * 2;
  wmemcpy(d, des, ld);
  d[ld] = 0;
  wmemcpy(n, name, ln);
  n[ln] = 0;
  ld = sn_clean(d);
  ln = sn_clean(n);
  if (!ld) return 0;
  size_t room = ld + 1 < SN_MAX ? SN_MAX - ld - 1 : 0; /* на наименование после пробела */
  if (ln > room) {
    size_t cut = room;
    while (cut && n[cut] != L' ') cut--;
    if (!cut) cut = room; /* одно длинное слово — режем его */
    n[cut] = 0;
    ln = sn_clean(n);
  }
  size_t le = wcslen(ext);
  if (ld + (ln ? ln + 1 : 0) + le + 1 > cap) return 0;
  wmemcpy(out, d, ld);
  size_t o = ld;
  if (ln) {
    out[o++] = L' ';
    wmemcpy(out + o, n, ln);
    o += ln;
  }
  wmemcpy(out + o, ext, le);
  out[o + le] = 0;
  return 1;
}

typedef int (*SnExists)(const wchar_t *fileName, void *ctx);

/* свободное имя: file, затем «stem (2)ext», … «stem (99)ext»; 0 — все заняты или не влезает */
static int sn_unique(const wchar_t *file, SnExists exists, void *ctx, wchar_t *out, size_t cap) {
  size_t len = wcslen(file);
  if (len + 1 > cap) return 0;
  wcscpy(out, file);
  if (!exists(out, ctx)) return 1;
  const wchar_t *dot = wcsrchr(file, L'.');
  size_t stem = dot ? (size_t)(dot - file) : len;
  if (len + 6 > cap) return 0; /* « (99)» — 5 знаков */
  for (int k = 2; k < 100; k++) {
    size_t p = stem;
    wmemcpy(out, file, stem);
    out[p++] = L' ';
    out[p++] = L'(';
    if (k >= 10) out[p++] = (wchar_t)(L'0' + k / 10);
    out[p++] = (wchar_t)(L'0' + k % 10);
    out[p++] = L')';
    wcscpy(out + p, file + stem);
    if (!exists(out, ctx)) return 1;
  }
  return 0;
}

#endif
