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
static void touch_at(const wchar_t *path, const char *text) {
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  CHECK(h != INVALID_HANDLE_VALUE); DWORD n; CHECK(WriteFile(h, text, (DWORD)strlen(text), &n, NULL)); CloseHandle(h);
}
static BOOL exists_at(const wchar_t *path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }
static void json_path(const wchar_t *path, wchar_t *out, int cap) {
  int n = 0;
  for (const wchar_t *p = path; *p && n + 2 < cap; p++) { if (*p == L'\\' || *p == L'"') out[n++] = L'\\'; out[n++] = *p; }
  out[n] = 0;
}
static BOOL run_hidden(wchar_t *cmd) {
  STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof(si)); memset(&pi, 0, sizeof(pi)); si.cb = sizeof(si);
  if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return FALSE;
  WaitForSingleObject(pi.hProcess, 20000); DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return code == 0;
}
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
  CHECK(ag_file_intent(L"с нуля создай ексель"));
  CHECK(ag_file_intent(L"создай эксель"));
  CHECK(ag_file_intent(L"создай Excel"));
  CHECK(ag_file_intent(L"создай там ворд"));
  CHECK(ag_needs_word(L"создай папку и создай в ней ворд и перенеси STL"));
  CHECK(!ag_needs_word(L"создай папку и скопируй в неё ворд файл"));
  CHECK(ag_needs_document(L"с нуля создай ексель", TRUE));
  CHECK(!ag_needs_document(L"скопируй Excel в новую папку", TRUE));
  const wchar_t *importGoal = L"ее братан как жизнь, создай мне ворд в \"C:\\Работа\" и засунь туда данные из \"\"C:\\Данные\\Развитие_AI.xlsx\"\"";
  CHECK(ag_needs_word(importGoal)); CHECK(!ag_needs_document(importGoal, TRUE));
  CHECK(ag_needs_document(L"Создай Excel и Word с данными из \"C:\\Вход.xlsx\"", TRUE));
  CHECK(!ag_needs_document(L"создай Word в \"C:\\Excel-to-Word\" с данными из \"C:\\Вход.xlsx\"", TRUE));
  wchar_t importSource[MAX_PATH]; CHECK(ag_word_import(importGoal, importSource));
  CHECK(!wcscmp(importSource, L"C:\\Данные\\Развитие_AI.xlsx"));
  CHECK(!ag_word_import(L"создай Word с краткими выводами из C:\\Вход.xlsx", importSource));
  CHECK(!ag_input_excel(L"прочитай из \"C:\\A.xlsx\" и из \"C:\\B.xlsx\"", importSource));
  CHECK(ag_file_intent(L"сравни материалы в Excel с СОЮЗ"));
  CHECK(ag_file_intent(L"открой расчёт краски"));
  CHECK(ag_file_intent(L"что умеешь?"));
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
  lstrcpynW(fs.goal, L"Создай папку WordTest и в ней ворд файл и туда STL", 4000);
  CHECK(run(L"{\"tool\":\"done\"}") < 0);
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\WordTest\\\\Документ.docx\",\"text\":\"Текст & <проверка>\\nВторая строка\"}") == 0);
  CHECK(!exists(L"WordTest")); CHECK(run(L"{\"tool\":\"done\"}") == 1);
  CHECK(ag_apply(&fs, result, 6000)); CHECK(exists(L"WordTest\\Документ.docx"));
  reset();
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\WordTest\\\\Документ.docx\",\"text\":\"\"}") == 0);
  CHECK(wcsstr(fs.plan[0].dst, L"(2)")); CHECK(ag_apply(&fs, result, 6000));
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\evil.exe\"}") < 0);
  reset(); lstrcpynW(fs.goal, importGoal, 4000);
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\Import.docx\",\"text\":\"\"}") < 0);
  CHECK(fs.count == 0 && !exists(L"Import.docx"));
  CHECK(run(L"{\"tool\":\"excel_write\",\"path\":\"@desktop\\\\Import.xlsx\",\"text\":\"A\\tB\"}") < 0);
  CHECK(run(L"{\"tool\":\"excel_write\",\"path\":\"@desktop\\\\Import.docx\",\"text\":\"A\\tB\"}") < 0);
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\Import.xlsx\",\"text\":\"\"}") < 0);
  CHECK(run(L"{\"tool\":\"done\"}") < 0 && fs.count == 0);
  reset();
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\Draft.docx\",\"text\":\"\"}") == 0);
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\Draft.docx\",\"text\":\"Updated\"}") == 0);
  CHECK(fs.count == 1 && !wcscmp(fs.plan[0].text, L"Updated")); CHECK(!exists(L"Draft.docx"));
  reset();
  lstrcpynW(fs.goal, L"создай ексель и ворд", 4000);
  CHECK(run(L"{\"tool\":\"done\"}") < 0);
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\OfficeTest\\\\Книга.xlsx\",\"text\":\"\"}") == 0);
  CHECK(!exists(L"OfficeTest")); CHECK(run(L"{\"tool\":\"done\"}") < 0);
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\OfficeTest\\\\Документ.docx\",\"text\":\"\"}") == 0);
  CHECK(run(L"{\"tool\":\"done\"}") == 1);
  wchar_t evidence[6000]; ag_plan_evidence(&fs, evidence, 6000);
  CHECK(wcsstr(evidence, L"Создать Word: 1") && wcsstr(evidence, L"Создать пустой Excel: 1"));
  CHECK(wcsstr(evidence, L"ещё не выполнен") && wcsstr(evidence, L"Документ.docx"));
  CHECK(ag_apply(&fs, result, 6000)); CHECK(exists(L"OfficeTest\\Книга.xlsx"));
  reset();
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\OfficeTest\\\\Книга.xlsx\"}") == 0);
  CHECK(wcsstr(fs.plan[0].dst, L"(2)")); CHECK(ag_apply(&fs, result, 6000));
  reset();
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\NotEmpty.xlsx\",\"text\":\"=SUM(A1:A5)\"}") < 0);
  CHECK(fs.count == 0); CHECK(!exists(L"NotEmpty.xlsx"));
  reset();
  lstrcpynW(fs.goal, L"Создай Excel, запиши числа и посчитай формулой", 4000);
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@desktop\\\\Формула.xlsx\",\"text\":\"\"}") == 0);
  CHECK(run(L"{\"tool\":\"done\"}") < 0); /* empty book cannot satisfy a formula request */
  CHECK(run(L"{\"tool\":\"excel_write\",\"path\":\"@desktop\\\\Формула.xlsx\",\"mode\":\"formulas\"}") < 0);
  CHECK(run(L"{\"tool\":\"excel_write\",\"path\":\"@desktop\\\\Формула.xlsx\",\"text\":\"=WEBSERVICE(A1)\",\"mode\":\"formulas\"}") < 0);
  CHECK(run(L"{\"tool\":\"excel_write\",\"path\":\"@desktop\\\\Формула.xlsx\",\"text\":\"=12\\t=3\\t=SUM(A1:B1)\",\"mode\":\"formulas\"}") == 0);
  CHECK(fs.count == 1 && fs.plan[0].kind == AG_EXCEL); /* upgrade same plan entry */
  ag_plan_evidence(&fs, evidence, 6000); CHECK(wcsstr(evidence, L"=SUM(A1:B1)"));
  CHECK(wcsstr(evidence, L"[tool=excel_write]") && wcsstr(evidence, L"создать НОВЫЙ файл Excel И заполнить"));
  CHECK(run(L"{\"tool\":\"done\"}") == 1);
  CHECK(run(L"{\"tool\":\"excel_write\",\"path\":\"@desktop\\\\Формула.xlsx\",\"text\":\"=1\",\"mode\":\"formulas\"}") < 0);
  CHECK(fs.count == 1); CHECK(!exists(L"Формула.xlsx")); /* preview still changes no files */
  reset();
  CHECK(ag_glob(L"@excel", L"a.XLSX")); CHECK(ag_glob(L"@excel", L"a.xlsm"));
  CHECK(!ag_glob(L"@excel", L"a.xlsx.txt")); CHECK(!ag_glob(L"@excel", L"a.xls.backup"));
  CHECK(ag_needs_inventory(L"на рабочем столе создай папку и закинь туда ексели, и создай в нем ексель с перечислением того что в папке"));
  CHECK(!ag_needs_inventory(L"Создай Excel со списком сотрудников в папке Офис"));
  reset();
  CHECK(run(L"{\"tool\":\"new_folder\",\"path\":\"@desktop\\\\MissingParent\"}") < 0);
  CHECK(fs.count == 0); /* parent and child must never be silently confused */
  lstrcpynW(fs.goal, L"на рабочем столе создай папку и закинь туда ексели, и создай в нем ексель с перечислением того что в папке", 4000);
  swprintf(fs.last, MAX_PATH, L"%ls\\Модели", scratch);
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\",\"dst\":\"@last\",\"pattern\":\"@excel\"}") < 0);
  CHECK(fs.count == 0); CHECK(run(L"{\"tool\":\"new_folder\",\"path\":\"@last\"}") < 0);
  CHECK(run(L"{\"tool\":\"new_folder\",\"path\":\"@desktop\"}") == 0);
  CHECK(fs.count == 1); CHECK(!exists(L"Новая папка"));
  CHECK(run(L"{\"tool\":\"write\",\"path\":\"@new\\\\Список_файлов.xlsx\",\"text\":\"\"}") < 0);
  CHECK(run(L"{\"tool\":\"done\"}") < 0); /* inventory cannot be replaced with an empty book */
  touch(L"a&b.xlsx", "opaque XLSX test payload"); touch(L"UP.XLSM", "opaque XLSM test payload");
  touch(L"=1+1.xlsx", "filename is data, never a formula"); touch(L"not.xlsx.txt", "keep");
  CHECK(run(L"{\"tool\":\"ask\",\"text\":\"Откуда взять Excel?\"}") < 0);
  CHECK(wcsstr(result, L"План НЕПОЛОН") && wcsstr(result, L"@excel"));
  CHECK(run(L"{\"tool\":\"inventory\",\"path\":\"@new\"}") == 0); /* deliberately before move */
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\",\"dst\":\"@new\",\"pattern\":\"@excel\"}") == 0);
  CHECK(run(L"{\"tool\":\"done\"}") == 1); CHECK(fs.plan[fs.count - 1].kind == AG_INVENTORY);
  CHECK(!wcscmp(fs.plan[fs.count - 1].cell, L"3"));
  CHECK(!exists(L"Новая папка")); CHECK(ag_apply(&fs, result, 6000));
  CHECK(exists(L"Новая папка\\Список_файлов.xlsx")); CHECK(exists(L"Новая папка\\a&b.xlsx"));
  CHECK(exists(L"Новая папка\\UP.XLSM")); CHECK(exists(L"not.xlsx.txt"));
  CHECK(wcsstr(result, L"Элементов в них: 3"));
  reset();
  CHECK(run(L"{\"tool\":\"list\",\"path\":\"@desktop\"}") == 0); CHECK(wcsstr(result, L"НЕПОЛНЫЙ"));
  CHECK(run(L"{\"tool\":\"list\",\"path\":\"@desktop\",\"offset\":\"-1\"}") < 0);
  /* Directory move and named parent. Scratch only; user folders are not touched. */
  reset();
  wchar_t parent[MAX_PATH], source[MAX_PATH], nested[MAX_PATH], emptyDir[MAX_PATH], marker[MAX_PATH];
  wchar_t escapedParent[MAX_PATH * 2], escapedSource[MAX_PATH * 2], goal[4000], json[4000];
  swprintf(parent, MAX_PATH, L"%ls\\Вайбкокинг", scratch);
  swprintf(source, MAX_PATH, L"%ls\\вайбокок2", scratch);
  swprintf(nested, MAX_PATH, L"%ls\\влож", source);
  swprintf(marker, MAX_PATH, L"%ls\\влож\\заметка.txt", source);
  CHECK(CreateDirectoryW(parent, NULL)); CHECK(CreateDirectoryW(source, NULL));
  CHECK(CreateDirectoryW(nested, NULL));
  swprintf(emptyDir, MAX_PATH, L"%ls\\влож\\пусто", source); CHECK(CreateDirectoryW(emptyDir, NULL));
  touch_at(marker, "cyrillic nested");
  swprintf(emptyDir, MAX_PATH, L"%ls\\пустая", source); CHECK(CreateDirectoryW(emptyDir, NULL));
  swprintf(goal, 4000, L"Создай в %ls папку и перенеси туда папку %ls", parent, source);
  lstrcpynW(fs.goal, goal, 4000);
  CHECK(run(L"{\"tool\":\"new_folder\",\"path\":\"@desktop\"}") < 0);
  { wchar_t low[6000]; lstrcpynW(low, result, 6000); CharLowerBuffW(low, (DWORD)wcslen(low)); CHECK(wcsstr(low, L"вайбкокинг")); }
  CHECK(fs.count == 0);
  { wchar_t child[MAX_PATH]; swprintf(child, MAX_PATH, L"%ls\\Новая папка", parent); CHECK(!exists_at(child)); }
  CHECK(exists_at(source));
  json_path(parent, escapedParent, MAX_PATH * 2); json_path(source, escapedSource, MAX_PATH * 2);
  swprintf(json, 4000, L"{\"tool\":\"new_folder\",\"path\":\"%ls\"}", escapedParent);
  CHECK(run(L"{\"tool\":\"new_folder\",\"path\":\"@parent\"}") == 0); CHECK(fs.count == 1); CHECK(!exists_at(fs.newDir));
  CHECK(wcsstr(fs.newDir, L"Вайбкокинг\\Новая папка") || wcsstr(fs.newDir, L"вайбкокинг\\Новая папка"));
  swprintf(json, 4000, L"{\"tool\":\"move\",\"src\":\"%ls\",\"dst\":\"@new\",\"pattern\":\"\"}", escapedSource);
  CHECK(run(json) == 0); CHECK(fs.plan[fs.count - 1].kind == AG_MOVE_DIR);
  CHECK(wcsstr(fs.plan[fs.count - 1].dst, L"Новая папка\\вайбокок2"));
  CHECK(exists_at(marker)); CHECK(!exists_at(fs.plan[fs.count - 1].dst));
  fs.count = 0; fs.newDir[0] = fs.newBase[0] = 0; /* cancel before apply */
  CHECK(ag_apply(&fs, result, 6000)); CHECK(exists_at(marker));
  { wchar_t child[MAX_PATH]; swprintf(child, MAX_PATH, L"%ls\\Новая папка", parent); CHECK(!exists_at(child)); }
  swprintf(json, 4000, L"{\"tool\":\"new_folder\",\"path\":\"%ls\"}", escapedParent); CHECK(run(json) == 0);
  swprintf(json, 4000, L"{\"tool\":\"move\",\"src\":\"%ls\",\"dst\":\"@new\",\"pattern\":\"\"}", escapedSource); CHECK(run(json) == 0);
  CHECK(run(L"{\"tool\":\"ask\",\"text\":\"уточнение\"}") == 2); CHECK(exists_at(marker)); /* ask does not touch disk */
  fs.count = 0; fs.newDir[0] = 0;
  swprintf(json, 4000, L"{\"tool\":\"new_folder\",\"path\":\"%ls\"}", escapedParent); CHECK(run(json) == 0);
  swprintf(json, 4000, L"{\"tool\":\"move\",\"src\":\"%ls\",\"dst\":\"@new\",\"pattern\":\"\"}", escapedSource); CHECK(run(json) == 0);
  CHECK(ag_apply(&fs, result, 6000));
  { wchar_t moved[MAX_PATH]; swprintf(moved, MAX_PATH, L"%ls\\Новая папка\\вайбокок2\\влож\\заметка.txt", parent);
    wchar_t movedEmpty[MAX_PATH]; swprintf(movedEmpty, MAX_PATH, L"%ls\\Новая папка\\вайбокок2\\пустая", parent);
    CHECK(exists_at(moved)); CHECK(exists_at(movedEmpty)); CHECK(!exists_at(source)); }
  reset();
  wchar_t box[MAX_PATH], inside[MAX_PATH], other[MAX_PATH], kept[MAX_PATH], movedFile[MAX_PATH];
  swprintf(box, MAX_PATH, L"%ls\\коробка", scratch); swprintf(inside, MAX_PATH, L"%ls\\внутри", box);
  swprintf(other, MAX_PATH, L"%ls\\куда", scratch); swprintf(kept, MAX_PATH, L"%ls\\коробка\\оставь.txt", other);
  CHECK(CreateDirectoryW(box, NULL)); CHECK(CreateDirectoryW(inside, NULL)); CHECK(CreateDirectoryW(other, NULL));
  { wchar_t occupied[MAX_PATH]; swprintf(occupied, MAX_PATH, L"%ls\\коробка", other); CHECK(CreateDirectoryW(occupied, NULL)); }
  touch_at(kept, "keep"); swprintf(movedFile, MAX_PATH, L"%ls\\едет.txt", box); touch_at(movedFile, "move");
  swprintf(json, 4000, L"{\"tool\":\"move\",\"src\":\"@desktop\\\\коробка\",\"dst\":\"@desktop\\\\коробка\\\\внутри\",\"pattern\":\"\"}");
  CHECK(run(json) < 0); CHECK(wcsstr(result, L"внутрь самого себя")); CHECK(exists_at(movedFile)); CHECK(fs.count == 0);
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\\\\коробка\",\"dst\":\"@desktop\\\\куда\",\"pattern\":\"\"}") == 0);
  CHECK(wcsstr(fs.plan[0].dst, L"коробка (2)")); CHECK(ag_apply(&fs, result, 6000));
  CHECK(exists_at(kept)); CHECK(exists(L"куда\\коробка (2)\\едет.txt")); CHECK(!exists(L"коробка"));
  reset();
  wchar_t changed[MAX_PATH], changedFile[MAX_PATH], destParent[MAX_PATH];
  swprintf(changed, MAX_PATH, L"%ls\\меняется", scratch); swprintf(changedFile, MAX_PATH, L"%ls\\данные.txt", changed);
  swprintf(destParent, MAX_PATH, L"%ls\\после", scratch);
  CHECK(CreateDirectoryW(changed, NULL)); CHECK(CreateDirectoryW(destParent, NULL)); touch_at(changedFile, "before");
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\\\\меняется\",\"dst\":\"@desktop\\\\после\",\"pattern\":\"\"}") == 0);
  touch_at(changedFile, "after preview");
  CHECK(!ag_apply(&fs, result, 6000)); CHECK(exists_at(changedFile)); CHECK(!exists(L"после\\меняется"));
  reset();
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\\\\нет-такой-каталог\",\"dst\":\"@desktop\",\"pattern\":\"\"}") < 0);
  CHECK(wcsstr(result, L"не существует")); CHECK(wcsstr(result, L"нет-такой-каталог")); CHECK(fs.count == 0);
  CHECK(run(L"{\"tool\":\"find\",\"src\":\"@desktop\",\"pattern\":\"\"}") < 0); CHECK(wcsstr(result, L"маска"));
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"\\\\\\\\server\\\\share\\\\dir\",\"dst\":\"@desktop\",\"pattern\":\"\"}") < 0);
  CHECK(wcsstr(result, L"Сетевой")); CHECK(fs.count == 0);
  wchar_t absent[MAX_PATH]; swprintf(absent, MAX_PATH, L"%ls\\нет-родителя", scratch);
  swprintf(goal, 4000, L"Создай в %ls папку и перенеси туда папку %ls", absent, scratch);
  lstrcpynW(fs.goal, goal, 4000);
  CHECK(run(L"{\"tool\":\"new_folder\",\"path\":\"@desktop\"}") < 0); CHECK(wcsstr(result, L"нет-родителя")); CHECK(fs.count == 0);
  swprintf(goal, 4000, L"На рабочем столе создай в %ls папку и перенеси туда файлы", parent);
  lstrcpynW(fs.goal, goal, 4000);
  CHECK(run(L"{\"tool\":\"new_folder\",\"path\":\"@desktop\"}") < 0); CHECK(fs.count == 0);
  wchar_t deepMissing[MAX_PATH]; swprintf(deepMissing, MAX_PATH, L"%ls\\нет-внутри", parent);
  swprintf(goal, 4000, L"Создай в %ls папку и положи туда файлы", deepMissing);
  lstrcpynW(fs.goal, goal, 4000); json_path(parent, escapedParent, MAX_PATH * 2);
  swprintf(json, 4000, L"{\"tool\":\"new_folder\",\"path\":\"%ls\"}", escapedParent);
  CHECK(run(json) < 0); CHECK(wcsstr(result, L"нет-внутри")); CHECK(fs.count == 0);
  reset();
  wchar_t alone[MAX_PATH]; swprintf(alone, MAX_PATH, L"%ls\\пустой-каталог", scratch); CHECK(CreateDirectoryW(alone, NULL));
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\\\\пустой-каталог\",\"dst\":\"@desktop\\\\после\",\"pattern\":\"\"}") == 0);
  CHECK(ag_apply(&fs, result, 6000)); CHECK(exists(L"после\\пустой-каталог")); CHECK(!exists(L"пустой-каталог"));
  reset();
  const char *junctionNote = "junction: not created";
  wchar_t linkTarget[MAX_PATH], linkPath[MAX_PATH], host[MAX_PATH], cmd[1200];
  swprintf(linkTarget, MAX_PATH, L"%ls\\цель-ссылки", scratch); swprintf(linkPath, MAX_PATH, L"%ls\\ссылка", scratch);
  swprintf(host, MAX_PATH, L"%ls\\с-ссылкой", scratch);
  CHECK(CreateDirectoryW(linkTarget, NULL)); CHECK(CreateDirectoryW(host, NULL));
  swprintf(cmd, 1200, L"cmd /c mklink /J \"%ls\" \"%ls\"", linkPath, linkTarget);
  if (run_hidden(cmd)) {
    CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\\\\ссылка\",\"dst\":\"@desktop\\\\после\",\"pattern\":\"\"}") < 0);
    CHECK(wcsstr(result, L"junction") || wcsstr(result, L"ссылка")); CHECK(exists_at(linkPath)); CHECK(fs.count == 0);
    wchar_t nestedLink[MAX_PATH]; swprintf(nestedLink, MAX_PATH, L"%ls\\внутр", host);
    swprintf(cmd, 1200, L"cmd /c mklink /J \"%ls\" \"%ls\"", nestedLink, linkTarget);
    if (run_hidden(cmd)) {
      CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\\\\с-ссылкой\",\"dst\":\"@desktop\\\\после\",\"pattern\":\"\"}") < 0);
      CHECK(wcsstr(result, L"junction") || wcsstr(result, L"ссылка")); CHECK(exists_at(host)); CHECK(fs.count == 0);
      RemoveDirectoryW(nestedLink);
    }
    RemoveDirectoryW(linkPath); junctionNote = "junction: rejected";
  }
  const char *aclNote = "acl: not changed";
  wchar_t lockedDir[MAX_PATH], user[256]; DWORD userLen = 256;
  swprintf(lockedDir, MAX_PATH, L"%ls\\закрыто", scratch); CHECK(CreateDirectoryW(lockedDir, NULL));
  if (GetUserNameW(user, &userLen)) {
    swprintf(cmd, 1200, L"cmd /c icacls \"%ls\" /deny \"%ls\":(OI)(CI)(F)", lockedDir, user);
    if (run_hidden(cmd)) {
      int denied = run(L"{\"tool\":\"move\",\"src\":\"@desktop\\\\закрыто\",\"dst\":\"@desktop\\\\после\",\"pattern\":\"\"}");
      BOOL remains = exists_at(lockedDir); BOOL planned = fs.count != 0;
      swprintf(cmd, 1200, L"cmd /c icacls \"%ls\" /remove:d \"%ls\"", lockedDir, user); run_hidden(cmd);
      CHECK(denied < 0); CHECK(remains); CHECK(!planned); CHECK(wcsstr(result, L"недоступ") || wcsstr(result, L"Windows"));
      aclNote = "acl: rejected";
    }
  }
  wchar_t partialSrc[MAX_PATH], partialDst[MAX_PATH], partialLock[MAX_PATH], partialOther[MAX_PATH];
  swprintf(partialSrc, MAX_PATH, L"%ls\\частичный", scratch); swprintf(partialDst, MAX_PATH, L"%ls\\частичный-копия", scratch);
  swprintf(partialOther, MAX_PATH, L"%ls\\a.txt", partialSrc); swprintf(partialLock, MAX_PATH, L"%ls\\b.txt", partialSrc);
  CHECK(CreateDirectoryW(partialSrc, NULL)); touch_at(partialOther, "a"); touch_at(partialLock, "b");
  HANDLE lock = CreateFileW(partialLock, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  CHECK(lock != INVALID_HANDLE_VALUE);
  CHECK(!ag_move_directory(partialSrc, partialDst, TRUE, NULL, result, 6000));
  CloseHandle(lock);
  CHECK(exists_at(partialOther)); CHECK(exists_at(partialLock)); CHECK(!exists_at(partialDst));
  CHECK(wcsstr(result, L"не изменён") || wcsstr(result, L"удалена"));
  /* A file changed or created during a cross-volume copy must never be
     removed by source cleanup, even if a complete copy was made earlier. */
  wchar_t safeSrc[MAX_PATH], safeDst[MAX_PATH], safeFile[MAX_PATH], safeCopy[MAX_PATH], problem[MAX_PATH];
  DWORD cleanupErr = 0;
  swprintf(safeSrc, MAX_PATH, L"%ls\\проверка-источник", scratch);
  swprintf(safeDst, MAX_PATH, L"%ls\\проверка-копия", scratch);
  swprintf(safeFile, MAX_PATH, L"%ls\\данные.txt", safeSrc);
  swprintf(safeCopy, MAX_PATH, L"%ls\\данные.txt", safeDst);
  CHECK(CreateDirectoryW(safeSrc, NULL)); touch_at(safeFile, "before");
  CHECK(ag_copy_tree(safeSrc, safeDst, NULL, problem, &cleanupErr, 0));
  touch_at(safeFile, "AFTER!"); /* same length: compare actual bytes */
  CHECK(!ag_remove_copied_tree(safeSrc, safeDst, NULL, problem, &cleanupErr, 0));
  CHECK(exists_at(safeFile)); CHECK(exists_at(safeCopy));
  touch_at(safeCopy, "AFTER!");
  HANDLE writer = CreateFileW(safeFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  CHECK(writer != INVALID_HANDLE_VALUE);
  CHECK(!ag_remove_copied_file(safeFile, safeCopy, &cleanupErr)); CHECK(exists_at(safeFile)); CloseHandle(writer);
  CHECK(ag_remove_copied_tree(safeSrc, safeDst, NULL, problem, &cleanupErr, 0));
  CHECK(!exists_at(safeSrc)); CHECK(exists_at(safeCopy));
  CHECK(CreateDirectoryW(safeSrc, NULL));
  wchar_t emptyCopy[MAX_PATH]; swprintf(emptyCopy, MAX_PATH, L"%ls\\изначально-пусто", scratch);
  CHECK(ag_copy_tree(safeSrc, emptyCopy, NULL, problem, &cleanupErr, 0));
  touch_at(safeFile, "created after copy");
  CHECK(!ag_remove_copied_tree(safeSrc, emptyCopy, NULL, problem, &cleanupErr, 0)); CHECK(exists_at(safeFile));
  volatile LONG stopped = 1;
  CHECK(!ag_remove_copied_tree(safeSrc, emptyCopy, &stopped, problem, &cleanupErr, 0)); CHECK(exists_at(safeFile));
  reset();
  swprintf(goal, 4000, L"Создай папку в \"%ls\" и перенеси туда папку %ls", parent, safeSrc);
  lstrcpynW(fs.goal, goal, 4000);
  wchar_t namedParent[MAX_PATH]; CHECK(ag_named_parent(&fs, namedParent) == AG_PARENT_FOUND); CHECK(ag_same_path(namedParent, parent));
  swprintf(goal, 4000, L"Создай в \"%ls\" новую папку и перенеси туда папку %ls", parent, safeSrc);
  lstrcpynW(fs.goal, goal, 4000); CHECK(ag_named_parent(&fs, namedParent) == AG_PARENT_FOUND); CHECK(ag_same_path(namedParent, parent));
  wchar_t resolvedGoal[5000]; ag_model_goal(&fs, resolvedGoal, 5000);
  CHECK(wcsstr(resolvedGoal, L"уже существующая РОДИТЕЛЬСКАЯ") && wcsstr(resolvedGoal, parent));
  CHECK(!wcscmp(fs.goal, goal)); /* interpretation must not alter path scope */
  reset();
  CHECK(run(L"{\"tool\":\"move\",\"src\":\"@desktop\\\\проверка-источник\",\"dst\":\"@desktop\\\\после\"}") == 0);
  CHECK(run(L"{\"tool\":\"copy\",\"src\":\"@desktop\\\\проверка-источник\",\"dst\":\"@desktop\\\\куда\",\"pattern\":\"*.txt\"}") == 0);
  CHECK(!ag_apply(&fs, result, 6000)); CHECK(wcsstr(result, L"пересекающиеся")); CHECK(exists_at(safeFile));
  const char *volumeNote = "cross-volume live: no second writable drive";
  DWORD drives = GetLogicalDrives();
  for (int letter = 0; letter < 26; letter++) {
    if (!(drives & (1u << letter))) continue;
    wchar_t drive = (wchar_t)(L'A' + letter);
    if (towupper(drive) == towupper(scratch[0])) continue;
    wchar_t root[4] = {drive, L':', L'\\', 0}; UINT kind = GetDriveTypeW(root);
    if (kind != DRIVE_FIXED && kind != DRIVE_REMOVABLE) continue;
    wchar_t probe[MAX_PATH], crossSrc[MAX_PATH], crossDst[MAX_PATH], crossFile[MAX_PATH], problem[MAX_PATH];
    swprintf(probe, MAX_PATH, L"%lsCursorPadDirMoveProbe", root);
    if (!CreateDirectoryW(probe, NULL)) continue;
    swprintf(crossSrc, MAX_PATH, L"%ls\\кросс-том", scratch); swprintf(crossFile, MAX_PATH, L"%ls\\файл.txt", crossSrc);
    swprintf(crossDst, MAX_PATH, L"%ls\\кросс-том", probe);
    CHECK(CreateDirectoryW(crossSrc, NULL)); touch_at(crossFile, "volume");
    BOOL moved = ag_move_directory(crossSrc, crossDst, FALSE, NULL, result, 6000);
    BOOL fileMoved = exists_at(crossDst) && !exists_at(crossSrc);
    wchar_t copied[MAX_PATH]; swprintf(copied, MAX_PATH, L"%ls\\файл.txt", crossDst); fileMoved = fileMoved && exists_at(copied);
    DWORD err = 0; if (exists_at(crossDst)) ag_remove_tree(crossDst, problem, &err, 0); if (exists_at(crossSrc)) ag_remove_tree(crossSrc, problem, &err, 0);
    RemoveDirectoryW(probe);
    CHECK(moved); CHECK(fileMoved); volumeNote = "cross-volume live: tested"; break;
  }
  printf("PASS: %d checks; directory move, named parent, %s, %s.\n", checks, junctionNote, aclNote);
  printf("VOLUME: %s\n", volumeNote);
  return 0;
}
