/* Run only inside a fresh scratch directory: agent_files_test.exe scratch.
   Tests the production filesystem tool implementation, with isolated aliases. */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "ai_agent_fs.c"
#include "ai_agent_route.h"
static AgFs fs;
static wchar_t result[6000], scratch[MAX_PATH];
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void touch(const wchar_t *name, const char *text) {
  wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\%ls", scratch, name);
  HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  CHECK(h != INVALID_HANDLE_VALUE); DWORD n; CHECK(WriteFile(h, text, (DWORD)strlen(text), &n, NULL)); CloseHandle(h);
}
static BOOL exists(const wchar_t *name) {
  wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\%ls", scratch, name);
  return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}
static int run(const wchar_t *json) { return ag_tool(&fs, json, result, 6000); }
static void reset(void) {
  ag_fs_init(&fs, L"C:\\protected-data", L"");
  lstrcpynW(fs.desktop, scratch, MAX_PATH);
  fs.downloads[0] = fs.documents[0] = 0;
  lstrcpynW(fs.goal, L"Создай папку Модели на рабочем столе и засунь туда все файлы .stl", 4000);
}
int wmain(int argc, wchar_t **argv) {
  CHECK(argc == 2);
  CHECK(GetFullPathNameW(argv[1], MAX_PATH, scratch, NULL));
  CHECK(CreateDirectoryW(scratch, NULL)); /* refuse a pre-existing directory */
  ag_fs_init(&fs, L"C:\\protected-data", L"");
  wchar_t actualDesktop[MAX_PATH];
  CHECK(ag_path(&fs, L"@desktop", actualDesktop)); /* read-only OneDrive compatibility */
  WIN32_FIND_DATAW tag; memset(&tag, 0, sizeof(tag));
  tag.dwFileAttributes = FILE_ATTRIBUTE_REPARSE_POINT;
  tag.dwReserved0 = 0x9000601aUL; CHECK(!ag_redirect(&tag));
  tag.dwReserved0 = 0xa0000003UL; CHECK(ag_redirect(&tag)); /* junction */
  tag.dwReserved0 = 0xa000000cUL; CHECK(ag_redirect(&tag)); /* symlink */
  reset();
  CHECK(ag_file_intent(L"какие файлы лежат в папке 34д на рабочем столе?"));
  CHECK(ag_file_intent(L"сколько файлов в загрузках?"));
  CHECK(!ag_file_intent(L"Что такое файл STL?"));
  CHECK(ag_needs_word(L"создай папку и создай в ней ворд и перенеси STL"));
  CHECK(!ag_needs_word(L"создай папку и скопируй в неё ворд файл"));
  wchar_t p[MAX_PATH];
  CHECK(ag_path(&fs, L"@desktop\\Модели", p));
  CHECK(!ag_path(&fs, L"@desktop\\..\\outside", p));
  CHECK(!ag_path(&fs, L"@desktop\\part.stl:secret", p));
  CHECK(!ag_path(&fs, L"\\\\?\\C:\\Windows", p));
  CHECK(!ag_path(&fs, L"C:\\Windows", p));
  CHECK(!ag_name(L"..")); CHECK(!ag_name(L"CON.txt")); CHECK(!ag_name(L"foo.")); CHECK(ag_name(L"Деталь.stl"));
  CHECK(ag_glob(L"*.stl", L"Деталь.STL")); CHECK(!ag_glob(L"*.stl", L"Деталь.stl.txt"));
  CHECK(!ag_glob(L"*.stl", L"stl-note.txt")); CHECK(ag_glob(L"*830*", L"АДЕ.830.pdf"));
  CHECK(ag_glob(L"*корпус*", L"КОРПУС.stl"));
  AgJson j;
  CHECK(!ag_json(L"{\"tool\":\"move\",\"tool\":\"done\"}", &j));
  CHECK(!ag_json(L"{\"tool\":\"done\",}", &j)); CHECK(!ag_json(L"{\"tool\":\"done\"} text", &j));
  CHECK(!ag_json(L"{\"tool\":\"read\",\"path\":\"C:\\oops\"}", &j));
  CHECK(!ag_json(L"{\"tool\":\"read\",\"path\":\"\\u0\"}", &j));
  CHECK(ag_json(L"{\"tool\":\"ask\",\"text\":\"\\u0410\"}", &j));
  CHECK(!wcscmp(ag_get(&j, L"text"), L"А"));
  touch(L"деталь.stl", "solid a"); touch(L"КОРПУС.STL", "solid b");
  touch(L"деталь.stl.txt", "do not move"); touch(L"stl-note.txt", "do not move");
  CHECK(run(L"{\"tool\":\"find\",\"src\":\"@desktop\",\"pattern\":\"*.stl\"}") == 0);
  CHECK(wcsstr(result, L"2") != NULL); CHECK(fs.count == 0);
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\",\"dst\":\"@desktop\\\\Модели\",\"pattern\":\"*.stl\"}") == 0);
  CHECK(fs.count == 3); CHECK(!exists(L"Модели")); CHECK(exists(L"деталь.stl"));
  CHECK(run(L"{\"tool\":\"done\"}") == 1);
  CHECK(ag_apply(&fs, result, 6000));
  CHECK(exists(L"Модели\\деталь.stl")); CHECK(exists(L"Модели\\КОРПУС.STL"));
  CHECK(!exists(L"деталь.stl")); CHECK(exists(L"деталь.stl.txt")); CHECK(exists(L"stl-note.txt"));
  reset(); touch(L"деталь.stl", "new data");
  CHECK(run(L"{\"tool\":\"copy\",\"src\":\"@desktop\",\"dst\":\"@desktop\\\\Модели\",\"pattern\":\"*.stl\"}") == 0);
  CHECK(wcsstr(fs.plan[0].dst, L"(2)") != NULL);
  CHECK(ag_apply(&fs, result, 6000)); CHECK(exists(L"Модели\\деталь (2).stl")); CHECK(exists(L"деталь.stl"));
  reset();
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\",\"dst\":\"@desktop\\\\Changed\",\"pattern\":\"*.stl\"}") == 0);
  touch(L"деталь.stl", "changed after preview - must refuse");
  CHECK(!ag_apply(&fs, result, 6000)); CHECK(!exists(L"Changed")); CHECK(exists(L"деталь.stl"));
  reset();
  CHECK(run(L"{\"tool\":\"mkdir\",\"path\":\"@desktop\\\\Nested\\\\Child\"}") == 0);
  CHECK(fs.count == 2); CHECK(!exists(L"Nested"));
  fs.count = 0; CHECK(!exists(L"Nested")); /* cancellation */
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\",\"dst\":\"@desktop\\\\Nothing\",\"pattern\":\"*.xyz\"}") < 0);
  CHECK(fs.count == 0); CHECK(!exists(L"Nothing"));
  CHECK(run(L"{\"tool\":\"rename\",\"path\":\"@desktop\\\\деталь.stl\",\"name\":\"готово.stl\"}") == 0);
  CHECK(ag_apply(&fs, result, 6000)); CHECK(exists(L"готово.stl"));
  reset();
  touch(L"задача.txt", "Read-only text");
  CHECK(run(L"{\"tool\":\"read\",\"path\":\"@desktop\\\\задача.txt\"}") == 0);
  CHECK(wcsstr(result, L"Read-only text"));
  CHECK(run(L"{\"tool\":\"list\",\"path\":\"@desktop\"}") == 0); CHECK(wcsstr(result, L"Модели"));
  CHECK(run(L"{\"tool\":\"delete\",\"path\":\"@desktop\\\\готово.stl\"}") < 0); CHECK(exists(L"готово.stl"));
  /* A recursive flattening collision must reserve unique names within the plan. */
  CHECK(run(L"{\"tool\":\"copy\",\"src\":\"@desktop\",\"dst\":\"@desktop\\\\Combined\",\"pattern\":\"*.stl\",\"recursive\":\"yes\"}") == 0);
  for (int i = 0; i < fs.count; i++) for (int k = i + 1; k < fs.count; k++) CHECK(_wcsicmp(fs.plan[i].dst, fs.plan[k].dst));
  CHECK(ag_apply(&fs, result, 6000)); CHECK(exists(L"Combined\\деталь.stl"));
  reset();
  for (int i = 0; i < 513; i++) { wchar_t name[40]; swprintf(name, 40, L"limit-%d.test", i); touch(name, "x"); }
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\",\"dst\":\"@desktop\\\\TooMany\",\"pattern\":\"*.test\"}") < 0);
  CHECK(fs.count == 0); CHECK(!exists(L"TooMany")); CHECK(exists(L"limit-512.test"));
  reset();
  lstrcpynW(fs.goal, L"Создай папку и в ней ворд файл и туда STL", 4000);
  CHECK(run(L"{\"tool\":\"done\"}") < 0);
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\WordTest\\\\Документ.docx\",\"text\":\"Текст & <проверка>\\nВторая строка\"}") == 0);
  CHECK(!exists(L"WordTest")); CHECK(run(L"{\"tool\":\"done\"}") == 1);
  CHECK(ag_apply(&fs, result, 6000)); CHECK(exists(L"WordTest\\Документ.docx"));
  reset();
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\WordTest\\\\Документ.docx\",\"text\":\"\"}") == 0);
  CHECK(wcsstr(fs.plan[0].dst, L"(2)")); CHECK(ag_apply(&fs, result, 6000));
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\evil.exe\"}") < 0);
  reset();
  CHECK(run(L"{\"tool\":\"list\",\"path\":\"@desktop\"}") == 0); CHECK(wcsstr(result, L"НЕПОЛНЫЙ"));
  CHECK(run(L"{\"tool\":\"list\",\"path\":\"@desktop\",\"offset\":\"-1\"}") < 0);
  printf("PASS: %d checks; filesystem tools, routing, docx, completeness, paging.\n", checks);
  return 0;
}
