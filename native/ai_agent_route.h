/* Shared routing for actions AND questions requiring current filesystem facts. */
static BOOL ag_file_intent(const wchar_t *text) {
  wchar_t s[2000]; lstrcpynW(s, text, 2000); CharLowerBuffW(s, (DWORD)wcslen(s));
  if (wcsstr(s, L"штамп") || wcsstr(s, L"из рамки")) return FALSE;
  if (!wcsncmp(s, L"как ", 4) || !wcsncmp(s, L"что такое", 9) || !wcsncmp(s, L"объясни", 7)) return FALSE;
  const wchar_t *verbs[] = {L"созда", L"создай", L"сдела", L"перемест", L"перенес", L"перенёс", L"скопир", L"копир", L"закин", L"засун", L"полож", L"перелож", L"собер", L"собрат", L"найд", L"найти", L"покаж", L"показа", L"откро", L"откры", L"переимен", L"разлож", L"рассорт", L"отсорт", L"прочит", L"удал", L"очист", L"сотр", L"стереть"};
  const wchar_t *nouns[] = {L"файл", L"папк", L"рабоч", L"загруз", L"документ", L"проводник", L"каталог", L"копию", L"резерв", L"stl", L"pdf", L"xlsx", L"ворд", L"word", L"формат", L"расширен", L"туда", L"отсюда"};
  BOOL action = FALSE, file = FALSE;
  for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) if (wcsstr(s, verbs[i])) action = TRUE;
  for (size_t i = 0; i < sizeof(nouns) / sizeof(nouns[0]); i++) if (wcsstr(s, nouns[i])) file = TRUE;
  if (wcschr(s, L'\\')) file = TRUE;
  BOOL inspect = wcsstr(s, L"какие") || wcsstr(s, L"сколько") || wcsstr(s, L"лежит") || wcsstr(s, L"лежат") ||
                 wcsstr(s, L"содержим") || wcsstr(s, L"что в ") || wcsstr(s, L"что есть") || wcsstr(s, L"что на ");
  return file && (action || inspect || wcsstr(s, L"папк") || wcsstr(s, L"каталог") || wcschr(s, L'\\'));
}
