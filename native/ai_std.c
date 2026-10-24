/* ai_std.c — «Спросить ИИ»: вопросы о стандартных изделиях и материалах (с
   2026.10.24). «размеры болта М12 ГОСТ 7798», «свойства стали 40Х», «сколько
   весит круг 50 длиной 300», «аналог DIN 933». Строки находит код в справочнике
   на этом компьютере (std.c, std_files.c) — они и есть ответ, с таблицей,
   источником и пометкой, сверены ли; модель их не пересказывает, как и карточку
   материалов (ac_materials_direct), так что числа не искажаются и модель для
   ответа не нужна. В справочнике ничего нет — вопрос идёт обычным путём.
   Подключается из ai_chat.c после ai_plm_check.c. */

static BOOL ac_std_start(const wchar_t *a) {
  if (g_acImg || !gs_wanted(a) || ac_names_item(a)) return FALSE; /* картинка, PLM — свои пути */
  wchar_t low[600];
  lstrcpynW(low, a, 600);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  if (wcsstr(low, L"http") || ac_quick_route(a) == 1) return FALSE; /* ссылка, файловая задача */
  wchar_t *cite = NULL, why[600];
  wchar_t *txt = gs_answer(a, &cite, why, 600);
  if (!txt) {
    free(cite);
    return FALSE;
  }
  size_t n = wcslen(txt) + (cite ? wcslen(cite) : 0) + 200;
  wchar_t *ans = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (!ans) {
    free(txt);
    free(cite);
    return FALSE;
  }
  _snwprintf(ans, n, L"%s\nСправочник стандартов на этом компьютере: %s. Числа — из таблиц, модель их не пересказывала.",
             txt, cite ? cite : L"");
  ans[n - 1] = 0;
  ag_stop();
  ac_push(0, a);
  ac_push(1, ans);
  ac_redraw();
  ac_avatar_answer(ans);
  ac_status(L"Ответ из справочника стандартов — числа из таблиц, без модели");
  free(ans);
  free(txt);
  free(cite);
  return TRUE;
}
