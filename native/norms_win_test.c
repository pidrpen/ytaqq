/* Папка нормативов под Windows (norms.c, часть с windows.h) — без окон и без CursorPad:
     gcc -O2 -Wall -Werror -municode -o norms_win_test.exe norms_win_test.c -lshell32 && norms_win_test.exe папка
   Папка — пустая временная: стенд кладёт туда выдуманные таблицы в UTF-8 (с BOM и без),
   «Текст Юникод» Excel (UTF-16) и испорченный файл, читает их как CursorPad и считает норму.
   В CI не входит (нужен Windows). */
#include <windows.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define SET_TEST
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "cutting_calc.c"
#include "techno.c"
#include "norms.c"
#pragma GCC diagnostic pop

static int g_fail, g_n;

static void ok(const char *what, int cond) {
  g_n++;
  if (!cond) g_fail++, printf("FAIL %s\n", what);
}

static void put(const wchar_t *dir, const wchar_t *name, const void *data, DWORD n) {
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%s\\%s", dir, name);
  path[MAX_PATH - 1] = 0;
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  DWORD wr = 0;
  if (h != INVALID_HANDLE_VALUE) WriteFile(h, data, n, &wr, NULL), CloseHandle(h);
}

static void put8(const wchar_t *dir, const wchar_t *name, const wchar_t *text, int bom) {
  char buf[8192];
  int k = 0;
  if (bom) buf[k++] = (char)0xEF, buf[k++] = (char)0xBB, buf[k++] = (char)0xBF;
  k += WideCharToMultiByte(CP_UTF8, 0, text, -1, buf + k, (int)sizeof(buf) - k, NULL, NULL) - 1;
  put(dir, name, buf, (DWORD)k);
}

static void put16(const wchar_t *dir, const wchar_t *name, const wchar_t *text) {
  static wchar_t buf[4096];
  buf[0] = 0xFEFF;
  lstrcpynW(buf + 1, text, 4095);
  put(dir, name, buf, (DWORD)((wcslen(buf)) * sizeof(wchar_t)));
}

int wmain(int argc, wchar_t **argv) {
  if (argc < 2) return printf("нужна пустая папка\n"), 2;
  SetEnvironmentVariableW(L"CP_NORMS", argv[1]);
  _wputenv_s(L"CP_NORMS", argv[1]);
  put8(argv[1], L"установка.tsv",
       L"назначение\tтокарная.установка\nкарта\t1\nстолбцы\tмасса\nед\tмин\nсверено\tда\n"
       L"поз.\tспособ\tдо 3\tдо 5\n1\tпатрон\t0,2\t0,3\n",
       1);
  put8(argv[1], L"переход.tsv",
       L"назначение\tтокарная.переход\nкарта\t2\nед\tмин\nпоз.\tзначение\n1\t0,05\n", 0);
  put16(argv[1], L"обслуживание.txt",
        L"\"назначение\"\t\"токарная.обслуживание\"\r\nкарта\t3\r\nед\t%\r\nпоз.\tзначение\r\n1\t5\r\n");
  put16(argv[1], L"пз.txt", L"назначение\tтокарная.пз\r\nкарта\t4\r\nед\tмин\r\nпоз.\tзначение\r\n1\t15\r\n");
  put8(argv[1], L"испорчен.tsv", L"просто текст\n", 0);
  put8(argv[1], L"ПРОЧТИ.txt", L"не таблица — пропускается\n", 0);
  /* раскладка встроенных таблиц: отпечатки и новая версия правленой таблицы рядом */
  put8(argv[1], L".встроено.txt", L"0123456789abcdef\tпереход.tsv\n", 0);
  put8(argv[1], L"переход.tsv.новая", L"назначение\tтокарная.переход\nкарта\t2\nед\tмин\nпоз.\tзначение\n1\t9\n", 0);
  put8(argv[1], L"старое.tsvx", L"просто текст\n", 0);

  NmBase b;
  wchar_t dir[MAX_PATH];
  ok("папка найдена", nm_base_load(&b, dir) && !wcscmp(dir, argv[1]));
  ok("прочитано 4 таблицы (UTF-8 с BOM и без, UTF-16)", b.n == 4);
  ok("испорченный — в ошибках", wcsstr(b.err, L"испорчен.tsv") != NULL);
  ok("ПРОЧТИ.txt не читается", wcsstr(b.err, L"ПРОЧТИ") == NULL);
  ok("служебные и «.новая» не читаются", !wcsstr(b.err, L"встроено") && !wcsstr(b.err, L"tsvx"));
  nm_base_free(&b);

  static TnRoute r;
  memset(&r, 0, sizeof(r));
  r.n = 1;
  lstrcpynW(r.op[0].op, L"Токарная", 48);
  r.cls = TN_ROT;
  r.s.mat = TNM_STEEL;
  r.s.dmax = 40;
  wchar_t *t = nm_route_text(&r, L"2", L"L 100, подача 0,2, V 100, h 1");
  ok("текст норм", t && wcsstr(t, L"Тпз 15 мин"));
  /* То = 100/(n·0,2), n = 1000·100/(π·40) = 795,775 (ступеней нет); Тв 0,2 + 0,05·3; ×1,05 */
  double to = 100 / (100000 / (NM_PI * 40) * 0.2), tsht = (to + 0.35) * 1.05;
  wchar_t want[64], num[32];
  tn_numstr(tsht, num, 32);
  _snwprintf(want, 64, L"Тшт %s мин", num);
  ok("Тшт по таблицам из файлов", t && wcsstr(t, want));
  if (t && g_fail) {
    char u[24000];
    WideCharToMultiByte(CP_UTF8, 0, t, -1, u, sizeof(u), NULL, NULL);
    printf("%s\n", u);
  }
  free(t);

  /* досчёт в чате: прежнее задание и новое */
  nm_last_keep(nm_last_new(&r, L"2", L"L 100, подача 0,2, V 100, h 1"));
  ok("вопрос о нормах", nm_ask_wanted(L"норма: партия 30") && nm_ask_wanted(L"какое Тшт?") &&
                            !nm_ask_wanted(L"норма расхода материала") && !nm_ask_wanted(L"нормально?"));
  t = nm_follow_text(L"норма: партия 30");
  ok("досчёт с партией — Тшк", t && wcsstr(t, L"Тшк "));
  free(t);
  t = nm_follow_text(L"норма: L 200");
  ok("новое L важнее прежнего", t && wcsstr(t, L"L 200 мм (задано)"));
  free(t);

  SetEnvironmentVariableW(L"CP_NORMS", L"Z:\\нет такой папки");
  _wputenv_s(L"CP_NORMS", L"Z:\\нет такой папки");
  ok("нет папки — норм нет", nm_route_text(&r, NULL, NULL) == NULL);
  t = nm_follow_text(L"норма: L 120");
  ok("нет папки — подсказка", t && wcsstr(t, L"Нормирование не включено"));
  free(t);
  printf("norms_win_test: %d проверок, ошибок %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
