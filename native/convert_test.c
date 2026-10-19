/* Native conversion probe. No production-only test entry points. */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "ai_office.c"
#include "convert_core.c"
int wmain(int argc, wchar_t **argv) {
  if (argc == 4 && !wcscmp(argv[1], L"--formats")) {
    int list[7], n = cv_formats(argv[2], argv[3][0] == L'1', argv[3][1] == L'1', list);
    for (int i = 0; i < n; i++) wprintf(L"%ls\n", kCvFormats[list[i]].ext);
    return 0;
  }
  if (argc != 3) return 2;
  CvJob job = {0};
  if (!GetFullPathNameW(argv[1], MAX_PATH, job.src, NULL)) return 2;
  job.format = _wtoi(argv[2]);
  BOOL ok = cv_convert(&job);
  wchar_t text[800]; char utf8[3200]; swprintf(text, 800, L"%ls\n%ls", job.out, job.error);
  WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL); puts(utf8);
  return ok ? 0 : 1;
}
