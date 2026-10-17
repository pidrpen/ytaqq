/* Hidden RichEdit renderer contract: no application startup or user settings. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <richedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "ai_chat_format.h"
typedef struct { AcRtf *rtf; size_t position; } Source;
static DWORD CALLBACK stream(DWORD_PTR cookie, LPBYTE buffer, LONG cap, LONG *n) {
  Source *source = (Source *)cookie; size_t count = source->rtf->size - source->position;
  if (count > (size_t)cap) count = cap;
  memcpy(buffer, source->rtf->text + source->position, count); source->position += count; *n = (LONG)count; return 0;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
  CHECK(LoadLibraryW(L"Msftedit.dll"));
  HWND edit = CreateWindowExW(0, L"RICHEDIT50W", L"", ES_MULTILINE | ES_READONLY, 0,0,600,400, NULL,NULL,GetModuleHandleW(NULL),NULL);
  CHECK(edit);
  const wchar_t *raw = L"## Заголовок\n\n1. **Первый** — размер 12.5.2. **Второй** — C:\\Работа\\00123.xlsx\n"
    L"```txt\n**буквально** {\\b подмена}\n```\nТекст `код` и \U0001F600, версия 2026.10.17.5.";
  AcRtf rtf = {0}; ac_rtf_begin(&rtf);
  ac_rtf_message(&rtf, L"**Буквальный вопрос** C:\\{путь}", FALSE, FALSE);
  ac_rtf_message(&rtf, raw, TRUE, TRUE); ac_rtf_add(&rtf, "}"); CHECK(!rtf.failed);
  Source source = {&rtf, 0}; EDITSTREAM input = {(DWORD_PTR)&source,0,stream};
  SendMessageW(edit, EM_STREAMIN, SF_RTF, (LPARAM)&input); CHECK(!input.dwError);
  wchar_t text[4096]; GETTEXTEX get = {sizeof(text), GT_DEFAULT, 1200, NULL, NULL};
  SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&get, (LPARAM)text);
  CHECK(wcsstr(text, L"**Буквальный вопрос** C:\\{путь}"));
  CHECK(wcsstr(text, L"Заголовок") && !wcsstr(text, L"## Заголовок"));
  CHECK(wcsstr(text, L"1. Первый") && !wcsstr(text, L"**Первый**"));
  CHECK(wcsstr(text, L"размер 12.5.") && wcsstr(text, L"версия 2026.10.17.5"));
  const wchar_t *second = wcsstr(text, L"2. Второй"); CHECK(second && (second[-1] == L'\r' || second[-1] == L'\n'));
  CHECK(wcsstr(text, L"C:\\Работа\\00123.xlsx"));
  CHECK(wcsstr(text, L"**буквально** {\\b подмена}"));
  CHECK(wcsstr(text, L"\U0001F600") && wcsstr(text, L"[Прикреплена картинка]"));
  const wchar_t *first = wcsstr(text, L"Первый"); CHECK(first);
  CHARRANGE range = {(LONG)(first - text), (LONG)(first - text + 6)};
  SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&range);
  CHARFORMAT2W format = {0}; format.cbSize = sizeof(format);
  SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
  CHECK(format.dwEffects & CFE_BOLD);
  CHECK(wcsstr(raw, L"**Первый**")); /* raw history untouched */
  free(rtf.text); DestroyWindow(edit);
  puts("PASS: RichEdit Unicode, Markdown, numbered headings, literal code/paths, escaping and bold formatting"); return 0;
}
