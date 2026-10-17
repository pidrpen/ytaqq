#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "ai_office.c"
#include "ai_compare.c"
static void print(const wchar_t *text) { char u[24000]; int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, u, sizeof(u), NULL, NULL); if (n) puts(u); }
int wmain(int argc, wchar_t **argv) {
  if (argc == 3 && !wcscmp(argv[1], L"--read")) { wchar_t out[6000]; BOOL ok = of_read(argv[2], L"", 0, 0, out, 6000); print(out); return ok ? 0 : 1; }
  if (argc != 2) return 2;
  wchar_t root[MAX_PATH], dst[MAX_PATH], out[6000];
  if (!GetFullPathNameW(argv[1], MAX_PATH, root, NULL) || !CreateDirectoryW(root, NULL)) return 2;
  if (!of_formula(L"=SUM(A1:B1)") || of_formula(L"=WEBSERVICE(A1)") || of_formula(L"=CMD|'x'!A1") || of_formula(L"=[book.xlsx]Sheet1!A1")) return 3;
  swprintf(dst, MAX_PATH, L"%ls\\Формулы.xlsx", root);
  BOOL ok = of_write_excel(L"", dst, L"", L"A1", L"12\t3\t=SUM(A1:B1)\n00123\t=ROUND(C1/2,1)\t=IF(C1>10,1,0)\n-2.5\t+3.75\t=SUM(A3:B3)\n1234567890123456\t1,5\t=3", TRUE, out, 6000); print(out);
  if (!ok) return 4;
  if (!of_read(dst, L"", 0, 0, out, 6000)) { print(out); return 5; } print(out);
  if (!wcsstr(out, L"C1: 15") || !wcsstr(out, L"A2: 00123") || !wcsstr(out, L"B2: 7.5")) return 6;
  if (!wcsstr(out, L"C3: 1.25") || !wcsstr(out, L"A4: 1234567890123456") || !wcsstr(out, L"B4: 1,5")) return 20;
  if (of_write_excel(L"", dst, L"", L"A1", L"overwrite", FALSE, out, 6000)) return 7;
  wchar_t copy[MAX_PATH]; swprintf(copy, MAX_PATH, L"%ls\\Копия.xlsx", root);
  if (!of_write_excel(dst, copy, L"Лист1", L"A5", L"Материал\t38ХС", FALSE, out, 6000)) { print(out); return 8; }
  if (!of_read(dst, L"", 0, 0, out, 6000) || wcsstr(out, L"A5:")) return 9;
  swprintf(copy, MAX_PATH, L"%ls\\Ошибка.xlsx", root);
  if (of_write_excel(L"", copy, L"", L"A1", L"=1/0", TRUE, out, 6000) || GetFileAttributesW(copy) != INVALID_FILE_ATTRIBUTES) return 10;
  wchar_t source[MAX_PATH], reference[MAX_PATH];
  swprintf(source, MAX_PATH, L"%ls\\Материалы.xlsx", root); swprintf(reference, MAX_PATH, L"%ls\\СОЮЗ-тест.xlsx", root);
  if (!of_write_excel(L"", source, L"", L"A1", L"Материал\nКруг 40 ГОСТ 2590-2006 / 38ХС ГОСТ 4543-2016\nСталь 45 ГОСТ 1050-2013\nZZZ_UNKNOWN_849\nКруг 18 ГОСТ 2590-2006", FALSE, out, 6000)) return 11;
  if (!of_write_excel(L"", reference, L"", L"A1", L"Материал\nКруг 40 ГОСТ 2590-2006 / 38ХС ГОСТ 4543-2016\nСталь 45 ГОСТ 1050-2013\nКруг 18 ГОСТ 2590-2006\nКруг 18 ГОСТ 2590-2006", FALSE, out, 6000)) return 12;
  if (!ag_compare_materials(source, reference, L"", L"", L"", 0, NULL, out, 6000)) { print(out); return 13; } print(out);
  if (!wcsstr(out, L"без кандидата: 1") || !wcsstr(out, L"неоднозначных: 1")) return 14;
  wchar_t wide[MAX_PATH], table[2000]; int at = 0;
  swprintf(wide, MAX_PATH, L"%ls\\Страницы.xlsx", root);
  for (int r = 0; r < 30; r++) for (int c = 0; c < 15; c++) {
    const wchar_t *value = r == 29 && c == 14 ? L"КОНЕЦ" : L"x";
    int n = (int)wcslen(value); memcpy(table + at, value, n * sizeof(wchar_t)); at += n;
    if (c < 14) table[at++] = L'\t'; else if (r < 29) table[at++] = L'\n';
  }
  table[at] = 0;
  if (!of_write_excel(L"", wide, L"Данные", L"A1", table, FALSE, out, 6000)) return 15;
  if (!of_read(wide, L"Данные", 0, 0, out, 6000) || !wcsstr(out, L"offset=20") || !wcsstr(out, L"cell=M1")) return 16;
  if (!of_read(wide, L"Данные", 20, 12, out, 6000) || !wcsstr(out, L"O30: КОНЕЦ")) return 17;
  OfTable all; wchar_t error[1000] = L"";
  if (!of_table_load(wide, L"Данные", &all, error, 1000)) return 18;
  BOOL complete = all.rows == 30 && all.cols == 15 && !wcscmp(all.cell[29 * 15 + 14], L"КОНЕЦ");
  of_table_free(&all); if (!complete) return 19;
  puts("PASS: real Excel recalculation, leading zeros, independent expected values, new copy, source unchanged, formula error refused, no overwrite.");
  return 0;
}
