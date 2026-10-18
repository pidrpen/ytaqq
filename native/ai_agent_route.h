/* Shared routing for actions AND questions requiring current filesystem facts. */
static BOOL ag_has_path(const wchar_t *s) {
  if (wcsstr(s, L"@desktop") || wcsstr(s, L"@documents") || wcsstr(s, L"@downloads") || wcsstr(s, L"@last") || wcsstr(s, L"@document")) return TRUE;
  for (const wchar_t *p = s; *p; p++) {
    if (((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z')) && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) return TRUE;
    if (p[0] == L'\\' && p[1] == L'\\') return TRUE;
  }
  return FALSE;
}
static BOOL ag_file_intent(const wchar_t *text) {
  wchar_t s[2000]; lstrcpynW(s, text, 2000); CharLowerBuffW(s, (DWORD)wcslen(s));
  if (wcsstr(s, L"штамп") || wcsstr(s, L"из рамки")) return FALSE;
  if (!wcsncmp(s, L"как ", 4) || !wcsncmp(s, L"что такое", 9) || !wcsncmp(s, L"объясни", 7)) return FALSE;
  if (wcsstr(s, L"что умеешь") || wcsstr(s, L"твои инструменты")) return TRUE;
  const wchar_t *verbs[] = {L"отредакт", L"выровн", L"выравнив", L"измени", L"оформ", L"созда", L"создай", L"сдела", L"сравн", L"сопостав", L"посчита", L"рассчита", L"заполн", L"перемест", L"перенес", L"перенёс", L"скопир", L"копир", L"закин", L"засун", L"полож", L"перелож", L"собер", L"собрат", L"найд", L"найти", L"покаж", L"показа", L"откро", L"откры", L"переимен", L"разлож", L"рассорт", L"отсорт", L"прочит", L"удал", L"очист", L"сотр", L"стереть"};
  const wchar_t *nouns[] = {L"файл", L"папк", L"рабоч", L"загруз", L"документ", L"проводник", L"каталог", L"копию", L"резерв", L"stl", L"pdf", L"xlsx", L"эксел", L"ексел", L"excel", L"таблиц", L"ворд", L"word", L"материал", L"союз", L"plm", L"чертеж", L"чертёж", L"краск", L"резк", L"формат", L"расширен", L"туда", L"там", L"отсюда"};
  BOOL action = FALSE, file = FALSE;
  for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) if (wcsstr(s, verbs[i])) action = TRUE;
  for (size_t i = 0; i < sizeof(nouns) / sizeof(nouns[0]); i++) if (wcsstr(s, nouns[i])) file = TRUE;
  BOOL path = ag_has_path(s);
  if (path) file = TRUE;
  BOOL inspect = wcsstr(s, L"какие") || wcsstr(s, L"сколько") || wcsstr(s, L"лежит") || wcsstr(s, L"лежат") ||
                 wcsstr(s, L"содержим") || wcsstr(s, L"что в ") || wcsstr(s, L"что есть") || wcsstr(s, L"что на ");
  return file && (action || inspect || wcsstr(s, L"папк") || wcsstr(s, L"каталог") || path);
}
