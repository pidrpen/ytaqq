/* ---- «Ещё»: страницы из pidrpen/giriaja-hall ----------------------------------

   Кнопка «Ещё» внизу окна — список страниц: расчёт резки и газов,
   объединение TIFF / PDF, сортировка TIFF A4/A3. Сами страницы — HTML с
   библиотеками внутри (собирает native/make_tools.py в native/tools/), и
   они вшиты в exe (cursorpad.rc, RCDATA 320–322): сеть не нужна вовсе.

   При выборе страница выкладывается из exe в %LOCALAPPDATA%\CursorPad\tools\
   (каждый раз заново — так у неё всегда та версия, что в этом exe) и
   открывается в окне CursorPad со встроенным Edge (webview.c); нет WebView2
   на ПК — отдельным окном Edge без вкладок, нет Edge — браузером.

   В 2026.09.23.29 страницы скачивались с GitHub, но проверка скачанного
   читала не больше 4 КБ файла и браковала любую страницу — «скачать не
   удалось» было всегда. Теперь скачивания нет. */

#define ID_MORE_BTN 193

typedef struct {
  int res;
  const wchar_t *file, *title;
} ToolPage;

static const ToolPage kTools[] = {
    {0, NULL, L"Расчёт резки и газов"}, /* своё окно: cutting.c (с 2026.09.23.34) */
    {321, L"tiff-merge.html", L"Объединение TIFF / PDF"},
    {322, L"tiff-a4-a3.html", L"Сортировка TIFF A4 / A3"},
};
#define TOOLS_N ((int)(sizeof(kTools) / sizeof(kTools[0])))

static void tools_open(int k) {
  if (k < 0 || k >= TOOLS_N || !g_dataDir[0]) return;
  if (kTools[k].res == 0) { /* переписан своим окном — без Edge, открывается сразу */
    cutting_show();
    return;
  }
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  _snwprintf(dir, MAX_PATH, L"%s\\tools", g_dataDir);
  dir[MAX_PATH - 1] = 0;
  CreateDirectoryW(dir, NULL);
  _snwprintf(path, MAX_PATH, L"%s\\%s", dir, kTools[k].file);
  path[MAX_PATH - 1] = 0;
  /* не вышло записать (страница открыта и заперта?) — откроем прежнюю копию */
  if (!extract_rcdata(kTools[k].res, path) && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
    show_status(L"Не удалось выложить страницу");
    return;
  }
  /* своё окно со встроенным Edge; нет WebView2 — отдельное окно Edge без вкладок */
  if (!webview_open(path, kTools[k].title)) wv_open_outside(path);
}

/* список под кнопкой «Ещё» */
static void tools_menu(HWND owner, HWND btn) {
  HMENU m = CreatePopupMenu();
  if (!m) return;
  for (int k = 0; k < TOOLS_N; k++) AppendMenuW(m, MF_STRING, 1 + k, kTools[k].title);
  webview_prewarm(); /* пока выбирают пункт, движок Edge уже просыпается */
  RECT r;
  GetWindowRect(btn, &r);
  SetForegroundWindow(owner);
  /* окно внизу экрана — список сам раскроется вверх, если вниз не помещается */
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, owner, NULL);
  DestroyMenu(m);
  if (cmd >= 1 && cmd <= TOOLS_N) tools_open(cmd - 1);
}
