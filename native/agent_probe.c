/* Interactive integration test driver for the exact production filesystem
   tools and prompt. Aliases point ONLY into the supplied test directory. */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "ai_agent_fs.c"
#include "ai_agent_prompt.h"
#include "ai_agent_schema.h"
#include "ai_agent_route.h"
static AgFs fs;
static void json_string(const wchar_t *s) {
  putchar('"');
  for (; *s; s++) {
    if (*s == L'"' || *s == L'\\') printf("\\%c", (char)*s);
    else if (*s < 32) printf("\\u%04x", (unsigned)*s);
    else {
      char b[8]; int n = WideCharToMultiByte(CP_UTF8, 0, s, 1, b, 8, NULL, NULL);
      fwrite(b, 1, n, stdout);
    }
  }
  putchar('"');
}
int wmain(int argc, wchar_t **argv) {
  if (argc == 2 && !wcscmp(argv[1], L"--system")) { json_string(kAgSystem); return 0; }
  if (argc == 2 && !wcscmp(argv[1], L"--schema")) { printf("{%s}\n", kAiAgentSchema + 1); return 0; }
  if (argc == 3 && !wcscmp(argv[1], L"--route")) { printf("%d\n", ag_file_intent(argv[2]) ? 1 : 0); return 0; }
  if (argc != 2) return 2;
  ag_fs_init(&fs, L"C:\\protected-cursorpad", L"");
  if (!GetFullPathNameW(argv[1], MAX_PATH, fs.desktop, NULL) || !ag_dir(fs.desktop)) return 2;
  swprintf(fs.downloads, MAX_PATH, L"%ls\\Downloads", fs.desktop);
  swprintf(fs.documents, MAX_PATH, L"%ls\\Documents", fs.desktop);
  lstrcpynW(fs.last, fs.desktop, MAX_PATH);
  char line[32768]; wchar_t w[16000], out[6000];
  while (fgets(line, sizeof(line), stdin)) {
    int n = MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 16000);
    if (!n) return 3;
    int state;
    if (!strncmp(line, ":apply", 6)) state = ag_apply(&fs, out, 6000) ? 1 : -1;
    else {
      AgJson j;
      if (ag_json(w, &j) && ag_get(&j, L"goal")[0]) {
        fs.count = 0;
        lstrcpynW(fs.goal, ag_get(&j, L"goal"), 4000);
        state = 0; lstrcpynW(out, L"Готов к задаче.", 6000);
      } else state = ag_tool(&fs, w, out, 6000);
    }
    printf("{\"state\":%d,\"count\":%d,\"observation\":", state, fs.count); json_string(out);
    printf(",\"plan\":[");
    for (int i = 0; i < fs.count; i++) {
      AgAction *a = &fs.plan[i];
      printf("%s{\"kind\":%d,\"src\":", i ? "," : "", a->kind); json_string(a->src);
      printf(",\"dst\":"); json_string(a->dst); putchar('}');
    }
    printf("],\"last\":"); json_string(fs.last); printf("}\n"); fflush(stdout);
  }
  return 0;
}
