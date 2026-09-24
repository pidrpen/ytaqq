/* ---- «Ещё»: инструменты из pidrpen/giriaja-hall -------------------------------

   Кнопка «Ещё» внизу окна — список: расчёт резки и газов, объединение
   TIFF / PDF, сортировка TIFF A4 / A3. Все три — свои окна CursorPad
   (cutting.c, tiffmerge.c, tiffsort.c): открываются сразу, сеть не нужна.

   Раньше (2026.09.23.29–.37) это были HTML-страницы giriaja-hall, вшитые в
   exe и открывавшиеся во встроенном Edge (WebView2); последней переписана
   сортировка, и Edge со страницами из программы убраны. */

#define ID_MORE_BTN 193

typedef struct {
  const wchar_t *title;
  void (*show)(void);
} ToolPage;

static const ToolPage kTools[] = {
    {L"Расчёт резки и газов", cutting_show},      /* с 2026.09.23.34 */
    {L"Объединение TIFF / PDF", tiffmerge_show},  /* с 2026.09.23.37 */
    {L"Сортировка TIFF A4 / A3", tiffsort_show}, /* с 2026.09.23.38 */
};
#define TOOLS_N ((int)(sizeof(kTools) / sizeof(kTools[0])))

/* список под кнопкой «Ещё» */
static void tools_menu(HWND owner, HWND btn) {
  HMENU m = CreatePopupMenu();
  if (!m) return;
  for (int k = 0; k < TOOLS_N; k++) AppendMenuW(m, MF_STRING, 1 + k, kTools[k].title);
  RECT r;
  GetWindowRect(btn, &r);
  SetForegroundWindow(owner);
  /* окно внизу экрана — список сам раскроется вверх, если вниз не помещается */
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, owner, NULL);
  DestroyMenu(m);
  if (cmd >= 1 && cmd <= TOOLS_N) kTools[cmd - 1].show();
}

/* Прежние версии выкладывали страницы в %LOCALAPPDATA%\CursorPad\tools\, а
   Edge держал свой профиль в ...\webview2\ (десятки МБ). Больше не нужны —
   убираем один раз, в фоне, чтобы не задерживать запуск. */
static DWORD WINAPI tools_cleanup_thread(LPVOID arg) {
  (void)arg;
  static const wchar_t *const dirs[] = {L"webview2", L"tools"};
  for (int i = 0; i < 2; i++) {
    wchar_t p[MAX_PATH + 2];
    memset(p, 0, sizeof(p));
    _snwprintf(p, MAX_PATH, L"%s\\%s", g_dataDir, dirs[i]); /* двойной ноль в конце — так просит SHFileOperation */
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
    SHFILEOPSTRUCTW op;
    memset(&op, 0, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = p;
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
  }
  return 0;
}

static void tools_cleanup_old(void) {
  if (wcslen(g_dataDir) < 4) return; /* папка данных не найдена — не трогаем ничего */
  HANDLE t = CreateThread(NULL, 0, tools_cleanup_thread, NULL, 0, NULL);
  if (t) CloseHandle(t);
}
