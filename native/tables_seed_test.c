/* tables_seed_test.c — встроенные таблицы в папке пользователя (tables_seed.c): первая
   раскладка, обновление нетронутых, правленые не затираются, новая версия рядом.
     gcc -O2 -Wall -Werror -municode -o tables_seed_test.exe tables_seed_test.c && tables_seed_test.exe
   Работает во временной папке. В CI не входит (нужен Windows). */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define TABLES_NO_DATA
#include "tables_seed.c"

static int failed, checks;
#define CHECK(c)                                                                   \
  do {                                                                             \
    checks++;                                                                      \
    if (!(c)) {                                                                    \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                 \
      failed = 1;                                                                  \
    }                                                                              \
  } while (0)

static wchar_t g_dir[MAX_PATH];

/* содержимое файла в папке стенда равно text (NULL — файла нет) */
static BOOL file_is(const wchar_t *rel, const char *text) {
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s\\%s", g_dir, rel);
  DWORD n = 0;
  BYTE *b = tb_read(p, &n);
  BOOL ok = text ? b && n == strlen(text) && !memcmp(b, text, n) : b == NULL;
  free(b);
  return ok;
}

static void put(const wchar_t *rel, const char *text) {
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s\\%s", g_dir, rel);
  tb_mkdirs(p);
  tb_write(p, text, (DWORD)strlen(text));
}

#define T(g, p, s) {g, p, (unsigned)sizeof(s) - 1, s}

int wmain(void) {
  wchar_t tmp[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  _snwprintf(g_dir, MAX_PATH, L"%sCursorPad-tables-test-%lu\\Нормативы", tmp, GetCurrentProcessId());
  wchar_t note[300];

  /* первая раскладка: обе таблицы группы, чужая группа не трогается */
  BuiltinTable v1[] = {T(L"norms", L"ОМН-к01.tsv", "Кtв\t1,0\n"), T(L"norms", L"под\\ОМН-к02.tsv", "x\t1\n"),
                       T(L"std", L"чужая.tsv", "не наша\n")};
  CHECK(tables_seed_list(v1, 3, L"norms", g_dir, note, 300) == 0);
  CHECK(file_is(L"ОМН-к01.tsv", "Кtв\t1,0\n"));
  CHECK(file_is(L"под\\ОМН-к02.tsv", "x\t1\n"));
  CHECK(file_is(L"чужая.tsv", NULL));
  CHECK(note[0] == 0);

  /* повтор той же версии — ничего не меняется */
  CHECK(tables_seed_list(v1, 3, L"norms", g_dir, note, 300) == 0);
  CHECK(file_is(L"ОМН-к01.tsv", "Кtв\t1,0\n"));

  /* пользователь сверил первую таблицу; в выпуске новые версии обеих */
  put(L"ОМН-к01.tsv", "Кtв\t1,0\nсверено да 24.10.2026\n");
  BuiltinTable v2[] = {T(L"norms", L"ОМН-к01.tsv", "Кtв\t1,1\n"), T(L"norms", L"под\\ОМН-к02.tsv", "x\t2\n")};
  CHECK(tables_seed_list(v2, 2, L"norms", g_dir, note, 300) == 1);
  CHECK(file_is(L"ОМН-к01.tsv", "Кtв\t1,0\nсверено да 24.10.2026\n")); /* правка цела */
  CHECK(file_is(L"ОМН-к01.tsv.новая", "Кtв\t1,1\n"));                   /* новая — рядом */
  CHECK(file_is(L"под\\ОМН-к02.tsv", "x\t2\n"));                        /* нетронутая — обновлена */
  CHECK(wcsstr(note, L"ОМН-к01.tsv") != NULL);
  CHECK(wcsstr(note, L"ОМН-к02") == NULL);

  /* повтор — про ту же новую версию второй раз не говорим */
  CHECK(tables_seed_list(v2, 2, L"norms", g_dir, note, 300) == 0);
  CHECK(note[0] == 0);

  /* пользователь сам привёл таблицу к новой версии — «.новая» больше не нужна, дальше обновляется */
  put(L"ОМН-к01.tsv", "Кtв\t1,1\n");
  CHECK(tables_seed_list(v2, 2, L"norms", g_dir, note, 300) == 0);
  CHECK(file_is(L"ОМН-к01.tsv.новая", NULL));
  BuiltinTable v3[] = {T(L"norms", L"ОМН-к01.tsv", "Кtв\t1,2\n")};
  CHECK(tables_seed_list(v3, 1, L"norms", g_dir, note, 300) == 0);
  CHECK(file_is(L"ОМН-к01.tsv", "Кtв\t1,2\n"));

  /* таблица лежала до встроенных (перенесена руками) и другая — своя, не трогаем */
  put(L"ОМН-к03.tsv", "своя\n");
  BuiltinTable v4[] = {T(L"norms", L"ОМН-к03.tsv", "встроенная\n")};
  CHECK(tables_seed_list(v4, 1, L"norms", g_dir, note, 300) == 1);
  CHECK(file_is(L"ОМН-к03.tsv", "своя\n"));
  CHECK(file_is(L"ОМН-к03.tsv.новая", "встроенная\n"));

  /* лежала до встроенных и совпадает — запоминается встроенной и дальше обновляется */
  put(L"ОМН-к04.tsv", "та же\n");
  BuiltinTable v5[] = {T(L"norms", L"ОМН-к04.tsv", "та же\n")};
  CHECK(tables_seed_list(v5, 1, L"norms", g_dir, note, 300) == 0);
  BuiltinTable v6[] = {T(L"norms", L"ОМН-к04.tsv", "новее\n")};
  CHECK(tables_seed_list(v6, 1, L"norms", g_dir, note, 300) == 0);
  CHECK(file_is(L"ОМН-к04.tsv", "новее\n"));

  /* отпечатки — в скрытом .встроено.txt; без папки — -1 */
  wchar_t man[MAX_PATH];
  _snwprintf(man, MAX_PATH, L"%s\\%s", g_dir, TB_MANIFEST);
  DWORD at = GetFileAttributesW(man);
  CHECK(at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_HIDDEN));
  CHECK(tables_seed_list(v1, 3, L"norms", L"", note, 300) == -1);

  if (failed) return 1;
  printf("tables_seed: %d проверок, ошибок 0\n", checks);
  return 0;
}
