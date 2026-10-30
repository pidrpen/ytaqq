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
  free(rtf.text);

  /* ответ-маршрут: операция — номер и название жирно, «почему» серым; нормы — колонками через табуляцию */
  const wchar_t *route = L"Маршрут обработки — черновик по правилам, проверяет технолог\n"
    L"Деталь: Вал промежуточный — тело вращения\n"
    L"005 Заготовительная. Отрезать заготовку от прутка — заготовка — прокат\n"
    L"010 Токарная (черновая). Подрезать торцы — тело вращения\n"
    L"Нормы времени (ОМН; проверяет технолог):\n"
    L"010 Токарная (черновая): Тпз 14 мин · Тшт ≥ 0,864 мин без машинного То\n"
    L"Разбор каждой цифры и источники — «норма подробно».\n"
    L"Ответ 12.5 — не операция.";
  AcRtf r2 = {0}; ac_rtf_begin(&r2);
  ac_rtf_message(&r2, route, TRUE, FALSE); ac_rtf_add(&r2, "}"); CHECK(!r2.failed);
  CHECK(strstr(r2.text, "\\cpnormbeg") && strstr(r2.text, "\\trowd")); /* нормы — таблицей (с 2026.10.30) */
  Source s2 = {&r2, 0}; EDITSTREAM in2 = {(DWORD_PTR)&s2, 0, stream};
  SendMessageW(edit, EM_STREAMIN, SF_RTF, (LPARAM)&in2); CHECK(!in2.dwError);
  SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&get, (LPARAM)text);
  CHECK(wcsstr(text, L"005 Заготовительная. Отрезать заготовку от прутка — заготовка — прокат")); /* текст тот же */
  /* в таблице — те же числа, что написал расчёт: номер, название, Тпз, Тшт и пометка по ячейкам */
  CHECK(wcsstr(text, L"Токарная (черновая)") && wcsstr(text, L"14") && wcsstr(text, L"≥ 0,864") &&
        wcsstr(text, L"без машинного То"));
  CHECK(wcsstr(text, L"ТШТ, МИН"));
  CHECK(wcsstr(text, L"Ответ 12.5 — не операция."));
  const wchar_t *op = wcsstr(text, L"Заготовительная"), *why = wcsstr(text, L"заготовка — прокат");
  CHECK(op && why);
  CHARRANGE r_op = {(LONG)(op - text), (LONG)(op - text + 5)}, r_why = {(LONG)(why - text), (LONG)(why - text + 5)};
  CHARFORMAT2W f2 = {0}; f2.cbSize = sizeof(f2);
  SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&r_op);
  SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&f2);
  CHECK(f2.dwEffects & CFE_BOLD);
  memset(&f2, 0, sizeof(f2)); f2.cbSize = sizeof(f2);
  SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&r_why);
  SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&f2);
  CHECK(!(f2.dwEffects & CFE_BOLD) && f2.crTextColor == RGB(100, 116, 139)); /* «почему» — серым (cf3) */
  free(r2.text); DestroyWindow(edit);
  puts("PASS: RichEdit Unicode, Markdown, numbered headings, literal code/paths, escaping, bold, route and norm lines"); return 0;
}
