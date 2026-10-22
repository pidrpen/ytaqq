/* ---- «Ещё»: инструменты из pidrpen/giriaja-hall -------------------------------

   Кнопка «Ещё» внизу окна — список: расчёт резки и газов, объединение
   TIFF / PDF, сортировка TIFF A4 / A3, расчёт краски. Все — свои окна
   CursorPad (cutting.c, tiffmerge.c, tiffsort.c, paint.c): открываются
   сразу, сеть не нужна.

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
    {L"Расчёт краски", paint_show},              /* с 2026.09.23.43 */
};
#define TOOLS_N ((int)(sizeof(kTools) / sizeof(kTools[0])))
#define MORE_AVATAR 60 /* «Призвать / Отозвать помощника» — аватар мини-ИИ (ai_avatar.c) */

/* ---- меню «Ещё» (⋯) --------------------------------------------------------------

   С 2026.09.23.41 — разделами, рисуется само; внизу красным «Очистить
   блокнот…» — бывшая кнопка «Сброс» (с тем же вопросом и запасной копией
   заметок). С 2026.10.21 разделы по смыслу, у каждого пункта — значок
   (Segoe Fluent Icons / MDL2; нет шрифта — без значка), выделение — серая
   скруглённая подложка, как в меню Windows 11. */
typedef struct {
  int cmd;
  const wchar_t *text, *key;
  wchar_t icon; /* знак из шрифта значков; 0 — нет */
  int kind;     /* 0 — пункт, 1 — заголовок раздела, 2 — опасный (красный), 3 — пока не работает (серый) */
} MoreItem;

enum { MORE_OCR = 20, MORE_NARDY, MORE_CHANGES, MORE_CLEAR, MORE_LAYOUT, MORE_MSG, MORE_ROUTE, MORE_SHASHKI, MORE_EXPORT, MORE_PACKAGE, MORE_MATCH, MORE_DRAWREC, MORE_AICHAT, MORE_CONVERT, MORE_MORSKOY };
static const MoreItem kMore[] = {
    {0, L"Расчёты и чертежи", NULL, 0, 1},
    {1, L"Расчёт резки и газов", NULL, 0xE8C6, 0},
    {4, L"Расчёт краски", NULL, 0xE771, 0},
    {MORE_DRAWREC, L"Распознать чертёж", NULL, 0xE8B9, 0}, /* штамп и спецификация со скана */
    {0, L"Файлы и PDF", NULL, 0, 1},
    {MORE_CONVERT, L"Файлы: конвертер, PDF и история", NULL, 0xE8E5, 0}, /* с 2026.10.19.6: Shift-перетаскивание, пакет */
    {2, L"Объединение TIFF / PDF", NULL, 0xE7C4, 0},
    {3, L"Сортировка TIFF A4 / A3", NULL, 0xE8CB, 0},
    {0, L"PLM", NULL, 0, 1},
    {MORE_ROUTE, L"Маршрутная ведомость", NULL, 0xE8FD, 0},     /* с 2026.09.23.45: из PLM в Excel */
    {MORE_EXPORT, L"Выгрузка из PLM", NULL, 0xE896, 0},        /* с 2026.09.23.78: ЭСИ, ТП, извещения */
    {MORE_PACKAGE, L"Пакет для загрузки в PLM", NULL, 0xE898, 0}, /* с .80: ЭСИ, состав, ТП */
    {MORE_MATCH, L"Сопоставить с Союзом", NULL, 0xE8AB, 0},     /* с 2026.10.16.1: Excel ↔ Союз */
    {0, L"ИИ", NULL, 0, 1},
    {MORE_AICHAT, L"Спросить ИИ", NULL, 0xE8BD, 0},             /* чат с локальной моделью, с историей */
    {MORE_AVATAR, g_acAvatarMenuText, NULL, 0xE99A, 0},         /* текст — по тому, на столе ли он */
    {0, L"Текст", NULL, 0, 1},
    {MORE_OCR, L"Выделить и прочитать", g_hkName[HK_OCR], 0xE7A8, 0},  /* клавиши — какие назначены (hotkeys.c) */
    {MORE_LAYOUT, L"Исправить раскладку", g_hkName[HK_LAYOUT], 0xE765, 0}, /* с 2026.09.23.42 */
    {0, L"С коллегами", NULL, 0, 1},
    {MORE_MSG, L"Чат с коллегами", L"пока не работает", 0xE8F2, 3}, /* с .68; с .75 выключен */
    {MORE_NARDY, L"Нарды", NULL, 0xE7FC, 0},
    {MORE_SHASHKI, L"Шашки", NULL, 0xE7FC, 0}, /* с 2026.09.23.65: по сети, как нарды */
    {MORE_MORSKOY, L"Морской бой", NULL, 0xE7FC, 0}, /* с 2026.10.22: по сети, на той же связи */
    {MORE_CHANGES, L"Что нового в папке", NULL, 0xE8B7, 0},
    {-1, NULL, NULL, 0, 0}, /* черта */
    {MORE_CLEAR, L"Очистить блокнот…", NULL, 0xE74D, 2},
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
  SIZE sz = {160, 16}, kz = {0, 0};
  if (dc) GetTextExtentPoint32W(dc, it->text, (int)wcslen(it->text), &sz);
  /* клавиша бывает и длинной — «Ctrl+Alt+R» */
  if (dc && it->key && it->key[0] && g_fontSmall) {
    SelectObject(dc, g_fontSmall);
    GetTextExtentPoint32W(dc, it->key, (int)wcslen(it->key), &kz);
  }
  if (of) SelectObject(dc, of);
  if (dc) ReleaseDC(NULL, dc);
  mi->itemWidth = (UINT)(sz.cx + kz.cx + more_px(it->key ? 96 : 66));
  /* заголовок раздела — с воздухом сверху, кроме самого первого */
  mi->itemHeight = (UINT)more_px(it->kind == 1 ? (it == kMore ? 26 : 32) : 32);
}

static void tools_menu_draw(const DRAWITEMSTRUCT *di) {
  const MoreItem *it = (const MoreItem *)di->itemData;
  if (!it) return;
  HDC dc = di->hDC;
  RECT r = di->rcItem;
  BOOL sel = (di->itemState & ODS_SELECTED) && it->kind != 1 && it->kind != 3;
  FillRect(dc, &r, g_paper);
  if (sel) { /* выделение — серая скруглённая подложка с отступом от краёв */
    RECT s = {r.left + more_px(4), r.top + 1, r.right - more_px(4), r.bottom - 1};
    COLORREF c = blend_rgb(COL_PAPER, COL_INK, 16);
    fill_round_rect(dc, s, c, c, more_px(4));
  }
  SetBkMode(dc, TRANSPARENT);
  int x = r.left + more_px(14);
  if (it->kind == 1) { /* заголовок раздела: мелко и серым */
    if (g_fontUi) SelectObject(dc, g_fontUi);
    SetTextColor(dc, COL_MUTED);
    RECT t = {x, r.top, r.right - more_px(8), r.bottom - more_px(4)};
    DrawTextW(dc, it->text, -1, &t, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    return;
  }
  COLORREF fg = it->kind == 2 ? RGB(0xB4, 0x23, 0x18) : it->kind == 3 ? COL_MUTED : COL_INK;
  HFONT ifont = it->icon ? ui_icon_font(more_px(16)) : NULL;
  if (ifont) {
    SelectObject(dc, ifont);
    SetTextColor(dc, fg);
    RECT ir = {x, r.top, x + more_px(18), r.bottom};
    DrawTextW(dc, &it->icon, 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  x += more_px(30);
  if (g_fontBody) SelectObject(dc, g_fontBody);
  SetTextColor(dc, fg);
  RECT t = {x, r.top, r.right - more_px(14), r.bottom};
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
  ac_avatar_menu_label();
  for (size_t k = 0; k < sizeof(kMore) / sizeof(kMore[0]); k++) {
    const MoreItem *it = &kMore[k];
    if (it->cmd < 0) AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    else
      AppendMenuW(m, MF_OWNERDRAW | (it->kind == 1 || it->kind == 3 ? MF_DISABLED : 0),
                  (UINT_PTR)(it->kind == 1 ? 999 : it->kind == 3 ? 998 : it->cmd),
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
  else if (cmd == MORE_ROUTE) route_show();
  else if (cmd == MORE_EXPORT) export_show();
  else if (cmd == MORE_PACKAGE) package_show(FALSE);
  else if (cmd == MORE_MATCH) match_show();
  else if (cmd == MORE_DRAWREC) drawrec_show();
  else if (cmd == MORE_CONVERT) cvx_show();
  else if (cmd == MORE_AICHAT) aichat_show();
  else if (cmd == MORE_AVATAR) ac_avatar_toggle();
  else if (cmd == MORE_LAYOUT) layout_fix_hint();
  else if (cmd == MORE_MSG) msg_compose_show();
  else if (cmd == MORE_NARDY) nardy_show();
  else if (cmd == MORE_SHASHKI) shashki_show();
  else if (cmd == MORE_MORSKOY) morskoy_show();
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
  if (g_cvBusy) return; /* Do not restart while a converted copy is being saved. */
  if (!g_updReady || !g_updPath[0]) {
    KillTimer(g_hwnd, TIMER_UPD_IDLE);
    return;
  }
  LASTINPUTINFO li = {sizeof(li), 0};
  if (!GetLastInputInfo(&li) || GetTickCount() - li.dwTime < 180000) return;
  if (g_tmExporting || g_tsExporting || g_tmLoadPending || g_tsLoadPending || g_rtBusy) return;
  for (int i = 0; i < 8; i++)
    if (g_msgInSlots[i]) return; /* на экране сообщение коллеги — перезапуск стёр бы его */
  HWND busy[] = {g_cutWnd, g_tmWnd, g_tsWnd, g_ndWnd, g_shWnd, g_mbWnd, g_msgOut, g_pnWnd, g_rtWnd, g_hkWnd, g_drWnd, g_cvxWnd};
  for (size_t i = 0; i < sizeof(busy) / sizeof(busy[0]); i++)
    if (busy[i] && IsWindowVisible(busy[i]) && !IsIconic(busy[i])) return; /* открыто — не мешаем */
  KillTimer(g_hwnd, TIMER_UPD_IDLE);
  g_updReady = FALSE;
  apply_update(g_updPath);
}
