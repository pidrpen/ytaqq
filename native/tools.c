/* ---- «Ещё»: инструменты из pidrpen/giriaja-hall -------------------------------

   Кнопка «Ещё» внизу окна — список: расчёт резки и газов, объединение
   TIFF / PDF, сортировка TIFF A4 / A3. Все три — свои окна CursorPad
   (cutting.c, tiffmerge.c, tiffsort.c): открываются сразу, сеть не нужна.

   Раньше (2026.09.23.29–.37) это были HTML-страницы giriaja-hall, вшитые в
   exe и открывавшиеся во встроенном Edge (WebView2); последней переписана
   сортировка, и Edge со страницами из программы убраны. */


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

/* ---- меню «Ещё» (⋯) --------------------------------------------------------------

   С 2026.09.23.41 — разделами, рисуется само: сверху «Инструменты» с цветной
   меткой каждого окна (цвет его страницы), ниже действия (распознать текст,
   нарды, что нового в папке) и внизу красным «Очистить блокнот…» — бывшая
   кнопка «Сброс» (с тем же вопросом и запасной копией заметок). */
typedef struct {
  int cmd;
  const wchar_t *text, *key;
  COLORREF mark; /* цветная метка; 0 — нет */
  int kind;      /* 0 — пункт, 1 — заголовок раздела, 2 — опасный (красный) */
} MoreItem;

enum { MORE_OCR = 20, MORE_NARDY, MORE_CHANGES, MORE_CLEAR, MORE_LAYOUT, MORE_MSG };
static const MoreItem kMore[] = {
    {0, L"ИНСТРУМЕНТЫ", NULL, 0, 1},
    {1, L"Расчёт резки и газов", NULL, RGB(0x1F, 0x6B, 0x5A), 0},
    {2, L"Объединение TIFF / PDF", NULL, RGB(0x25, 0x63, 0xEB), 0},
    {3, L"Сортировка TIFF A4 / A3", NULL, RGB(0xC9, 0xA2, 0x27), 0},
    {-1, NULL, NULL, 0, 0}, /* черта */
    {MORE_OCR, L"Выделить и прочитать", L"F6", 0, 0},
    {MORE_LAYOUT, L"Исправить раскладку", g_lfKeyName, 0, 0}, /* с 2026.09.23.42 */
    {MORE_MSG, L"Написать коллеге", NULL, 0, 0},
    {MORE_NARDY, L"Нарды", NULL, 0, 0},
    {MORE_CHANGES, L"Что нового в папке", NULL, 0, 0},
    {-1, NULL, NULL, 0, 0},
    {MORE_CLEAR, L"Очистить блокнот…", NULL, 0, 2},
};

static int more_px(int v) {
  HDC s = GetDC(NULL);
  int dpi = s ? GetDeviceCaps(s, LOGPIXELSY) : 96;
  if (s) ReleaseDC(NULL, s);
  return MulDiv(v, dpi, 96);
}

static void tools_menu_measure(MEASUREITEMSTRUCT *mi) {
  const MoreItem *it = (const MoreItem *)mi->itemData;
  if (!it) return;
  HDC dc = GetDC(NULL);
  HGDIOBJ of = (dc && g_fontBody) ? SelectObject(dc, g_fontBody) : NULL;
  SIZE sz = {160, 16};
  if (dc) GetTextExtentPoint32W(dc, it->text, (int)wcslen(it->text), &sz);
  if (of) SelectObject(dc, of);
  if (dc) ReleaseDC(NULL, dc);
  mi->itemWidth = (UINT)(sz.cx + more_px(it->key ? 90 : 60));
  mi->itemHeight = (UINT)more_px(it->kind == 1 ? 26 : 34);
}

static void tools_menu_draw(const DRAWITEMSTRUCT *di) {
  const MoreItem *it = (const MoreItem *)di->itemData;
  if (!it) return;
  HDC dc = di->hDC;
  RECT r = di->rcItem;
  BOOL sel = (di->itemState & ODS_SELECTED) && it->kind != 1;
  HBRUSH bg = CreateSolidBrush(sel ? blend_rgb(COL_SAGE, COL_PAPER, 215) : COL_PAPER);
  FillRect(dc, &r, bg);
  DeleteObject(bg);
  SetBkMode(dc, TRANSPARENT);
  int x = r.left + more_px(12);
  if (it->kind == 1) { /* заголовок раздела: мелко, прописными */
    if (g_fontSmall) SelectObject(dc, g_fontSmall);
    SetTextColor(dc, COL_MUTED);
    RECT t = {x, r.top, r.right - more_px(8), r.bottom};
    DrawTextW(dc, it->text, -1, &t, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    return;
  }
  int mk = more_px(18);
  if (it->mark) { /* цветная метка окна */
    RECT m = {x, (r.top + r.bottom - mk) / 2, x + mk, (r.top + r.bottom + mk) / 2};
    fill_round_rect(dc, m, it->mark, it->mark, more_px(4));
  }
  x += mk + more_px(10);
  if (g_fontBody) SelectObject(dc, g_fontBody);
  SetTextColor(dc, it->kind == 2 ? RGB(0xB4, 0x23, 0x18) : COL_INK);
  RECT t = {x, r.top, r.right - more_px(12), r.bottom};
  DrawTextW(dc, it->text, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (it->key) {
    if (g_fontSmall) SelectObject(dc, g_fontSmall);
    SetTextColor(dc, COL_MUTED);
    DrawTextW(dc, it->key, -1, &t, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  }
}

/* список под кнопкой «Ещё» */
static void tools_menu(HWND owner, HWND btn) {
  HMENU m = CreatePopupMenu();
  if (!m) return;
  for (size_t k = 0; k < sizeof(kMore) / sizeof(kMore[0]); k++) {
    const MoreItem *it = &kMore[k];
    if (it->cmd < 0) AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    else
      AppendMenuW(m, MF_OWNERDRAW | (it->kind == 1 ? MF_DISABLED : 0), (UINT_PTR)(it->kind == 1 ? 999 : it->cmd),
                  (LPCWSTR)it);
  }
  RECT r;
  GetWindowRect(btn, &r);
  SetForegroundWindow(owner);
  /* под кнопкой, правым краем к ней; не помещается вниз — раскроется вверх */
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom + 2, 0, owner, NULL);
  DestroyMenu(m);
  if (cmd >= 1 && cmd <= TOOLS_N) kTools[cmd - 1].show();
  else if (cmd == MORE_OCR) run_ocr_test();
  else if (cmd == MORE_LAYOUT) layout_fix_hint();
  else if (cmd == MORE_MSG) msg_compose_show();
  else if (cmd == MORE_NARDY) nardy_show();
  else if (cmd == MORE_CHANGES) files_show_changes();
  else if (cmd == MORE_CLEAR) clear_copied();
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

/* ---- автообновление: когда ставить скачанное ----------------------------------

   Новая версия скачана и проверена (update.c) — ставим, когда человек отошёл:
   3 минуты без мыши и клавиатуры, и не открыто ни одно окно «Ещё» с работой
   в нём и не идёт сохранение. Перезапуск занимает пару секунд, заметки
   сохранены. Щелчок по всплывашке — поставить сразу. */
static void upd_idle_tick(void) {
  if (!g_updReady || !g_updPath[0]) {
    KillTimer(g_hwnd, TIMER_UPD_IDLE);
    return;
  }
  LASTINPUTINFO li = {sizeof(li), 0};
  if (!GetLastInputInfo(&li) || GetTickCount() - li.dwTime < 180000) return;
  if (g_tmExporting || g_tsExporting || g_tmLoadPending || g_tsLoadPending) return;
  for (int i = 0; i < 8; i++)
    if (g_msgInSlots[i]) return; /* на экране сообщение коллеги — перезапуск стёр бы его */
  HWND busy[] = {g_cutWnd, g_tmWnd, g_tsWnd, g_ndWnd, g_msgOut};
  for (size_t i = 0; i < sizeof(busy) / sizeof(busy[0]); i++)
    if (busy[i] && IsWindowVisible(busy[i]) && !IsIconic(busy[i])) return; /* открыто — не мешаем */
  KillTimer(g_hwnd, TIMER_UPD_IDLE);
  g_updReady = FALSE;
  apply_update(g_updPath);
}
