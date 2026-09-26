/* ---- Окно маршрутной ведомости (сам сбор — route.c) ----------------------

   После share.c: копирование берёт его ShareBuf, а сбор у коллеги — его
   share_route. */

#include <commdlg.h> /* окно «Сохранить как» */

/* ---- окно ------------------------------------------------------------------- */


static HWND g_rtWnd, g_rtDes, g_rtOrder, g_rtList, g_rtLog;
static RtJob *g_rtJob;  /* последняя собранная ведомость */
static volatile LONG g_rtBusy, g_rtCancel;
static wchar_t g_rtStatus[300];
static float g_rtS = 1.0f;

static int RS(int v) { return (int)(v * g_rtS + 0.5f); }

static void rt_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_rtStatus, 300, fmt, ap);
  va_end(ap);
  g_rtStatus[299] = 0;
  if (g_rtWnd) {
    RECT rc;
    GetClientRect(g_rtWnd, &rc);
    RECT st = {0, RS(84), rc.right, RS(112)};
    InvalidateRect(g_rtWnd, &st, FALSE);
  }
}

static DWORD WINAPI rt_thread(LPVOID param) {
  RtJob *j = (RtJob *)param;
  j->t0 = GetTickCount64();
  if (share_client_on()) share_route(j);
  else rt_build(j);
  if (!g_rtWnd || !PostMessageW(g_rtWnd, WM_RT_DONE, 0, (LPARAM)j)) rt_job_free(j);
  InterlockedExchange(&g_rtBusy, 0);
  return 0;
}

static void rt_start(void) {
  if (InterlockedCompareExchange(&g_rtBusy, 1, 0) != 0) {
    InterlockedExchange(&g_rtCancel, 1); /* второй щелчок — остановить */
    rt_status(L"Останавливаю…");
    return;
  }
  RtJob *j = rt_job_new();
  if (!j) {
    InterlockedExchange(&g_rtBusy, 0);
    return;
  }
  GetWindowTextW(g_rtDes, j->des, 200);
  GetWindowTextW(g_rtOrder, j->order, 120);
  wchar_t *s = j->des; /* пробелы по краям — мимо */
  while (*s == L' ') s++;
  memmove(j->des, s, (wcslen(s) + 1) * sizeof(wchar_t));
  size_t l = wcslen(j->des);
  while (l && j->des[l - 1] == L' ') j->des[--l] = 0;
  if (!j->des[0]) {
    rt_job_free(j);
    InterlockedExchange(&g_rtBusy, 0);
    rt_status(L"Впишите обозначение сборки или детали");
    SetFocus(g_rtDes);
    return;
  }
  g_rtCancel = 0;
  j->cancel = &g_rtCancel;
  j->notify = g_rtWnd;
  j->deadline = GetTickCount64() + 10 * 60 * 1000; /* своя база — до 10 минут на большую сборку */
  rt_log(j, L"Маршрутная ведомость: %s%s%s\r\n", j->des, j->order[0] ? L", входит в " : L"", j->order);
  SetWindowTextW(GetDlgItem(g_rtWnd, ID_RT_BUILD), L"Стоп");
  rt_status(L"Собираю из PLM…");
  HANDLE t = CreateThread(NULL, 0, rt_thread, j, 0, NULL);
  if (t) CloseHandle(t);
  else {
    rt_job_free(j);
    InterlockedExchange(&g_rtBusy, 0);
  }
}

static void rt_fill_list(void) {
  ListView_DeleteAllItems(g_rtList);
  if (!g_rtJob) return;
  for (int r = 0; r < g_rtJob->n; r++) {
    RtRow *w = &g_rtJob->rows[r];
    wchar_t des[240];
    _snwprintf(des, 240, L"%*s%s", w->level * 3, L"", w->f[RC_DES]); /* вложенность — отступом */
    des[239] = 0;
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = r;
    it.pszText = des;
    int idx = (int)SendMessageW(g_rtList, LVM_INSERTITEMW, 0, (LPARAM)&it);
    for (int c = 1; c < RT_NCOL; c++) {
      LVITEMW si;
      memset(&si, 0, sizeof(si));
      si.iSubItem = c;
      si.pszText = w->f[c];
      SendMessageW(g_rtList, LVM_SETITEMTEXTW, (WPARAM)idx, (LPARAM)&si);
    }
  }
}

static const wchar_t *rt_cell(void *ctx, int r, int c) { return ((RtJob *)ctx)->rows[r].f[c]; }

static void rt_title(wchar_t *out, int cap) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  if (g_rtJob->order[0])
    _snwprintf(out, cap, L"Ведомость маршрутная %s (%s) от %02d.%02d.%02d", g_rtJob->order, g_rtJob->des, t.wDay,
               t.wMonth, t.wYear % 100);
  else
    _snwprintf(out, cap, L"Ведомость маршрутная %s от %02d.%02d.%02d", g_rtJob->des, t.wDay, t.wMonth,
               t.wYear % 100);
  out[cap - 1] = 0;
}

static void rt_save(void) {
  if (!g_rtJob || !g_rtJob->n) {
    rt_status(L"Сначала «Собрать»");
    return;
  }
  wchar_t title[300], file[MAX_PATH];
  rt_title(title, 300);
  lstrcpynW(file, title, MAX_PATH - 6);
  for (wchar_t *p = file; *p; p++)
    if (wcschr(L"\\/:*?\"<>|", *p)) *p = L'_';
  wcscat(file, L".xlsx");
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_rtWnd;
  of.lpstrFilter = L"Книга Excel (*.xlsx)\0*.xlsx\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"xlsx";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  XlSheet sh = {RT_NCOL, kRtHead, kRtWidth};
  if (!xl_save(file, title, &sh, g_rtJob->n, rt_cell, g_rtJob)) {
    MessageBoxW(g_rtWnd, L"Не удалось записать файл — он не открыт сейчас в Excel?", L"Маршрутная ведомость",
                MB_ICONWARNING);
    return;
  }
  rt_status(L"Сохранено: %s", file);
  ShellExecuteW(NULL, L"open", file, NULL, NULL, SW_SHOWNORMAL); /* сразу открыть в Excel */
}

static void rt_copy(void) {
  if (!g_rtJob || !g_rtJob->n) return;
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  for (int c = 0; c < RT_NCOL; c++) sb_add(&b, L"%s%s", c ? L"\t" : L"", kRtHead[c]);
  sb_add(&b, L"\r\n");
  for (int r = 0; r < g_rtJob->n; r++) {
    for (int c = 0; c < RT_NCOL; c++) {
      wchar_t v[200];
      lstrcpynW(v, g_rtJob->rows[r].f[c], 200);
      for (wchar_t *p = v; *p; p++)
        if (*p == L'\t' || *p == L'\r' || *p == L'\n') *p = L' ';
      sb_add(&b, L"%s%s", c ? L"\t" : L"", v);
    }
    sb_add(&b, L"\r\n");
  }
  if (b.w && clipboard_set(b.w)) rt_status(L"Таблица в буфере — вставьте в Excel (Ctrl+V)");
  free(b.w);
}

/* «Подробности»: весь проход сбора — чтобы было видно, где пусто и почему */
static void rt_show_log(void) {
  if (!g_rtJob || !g_rtJob->log[0]) return;
  if (!g_rtLog) {
    g_rtLog = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"EDIT", L"Маршрутная ведомость — подробности",
                              WS_OVERLAPPEDWINDOW | ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL |
                                  ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                              CW_USEDEFAULT, CW_USEDEFAULT, RS(820), RS(560), g_rtWnd, NULL, g_inst, NULL);
    if (!g_rtLog) return;
    if (g_fontMono) SendMessageW(g_rtLog, WM_SETFONT, (WPARAM)g_fontMono, FALSE);
  }
  SetWindowTextW(g_rtLog, g_rtJob->log);
  ShowWindow(g_rtLog, SW_SHOWNORMAL);
  SetForegroundWindow(g_rtLog);
}

static void rt_layout(void) {
  RECT rc;
  GetClientRect(g_rtWnd, &rc);
  int pad = RS(14), y = PANEL_TITLE_H + RS(24), h = RS(26);
  place_panel_close(g_rtWnd);
  MoveWindow(g_rtDes, pad, y, RS(300), h, TRUE);
  MoveWindow(g_rtOrder, pad + RS(312), y, RS(170), h, TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_BUILD), pad + RS(494), y - RS(1), RS(110), h + RS(2), TRUE);
  int by = rc.bottom - pad - RS(30);
  int ly = RS(116);
  MoveWindow(g_rtList, pad, ly, rc.right - pad * 2, by - RS(10) - ly, TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_SAVE), pad, by, RS(170), RS(30), TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_COPY), pad + RS(180), by, RS(120), RS(30), TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_LOG), pad + RS(310), by, RS(120), RS(30), TRUE);
}

static void rt_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Маршрутная ведомость");
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  int pad = RS(14), y = PANEL_TITLE_H + RS(6);
  RECT a = {pad, y, pad + RS(300), y + RS(18)}, b = {pad + RS(312), y, pad + RS(482), y + RS(18)};
  DrawTextW(hdc, L"Обозначение сборки или детали", -1, &a, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  DrawTextW(hdc, L"Входит в (заказ), можно пусто", -1, &b, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, g_rtBusy ? COL_SAGE : COL_INK);
  RECT st = {pad, RS(84), rc.right - pad, RS(112)};
  DrawTextW(hdc,
            g_rtStatus[0] ? g_rtStatus
                          : L"Состав, материал, заготовка, норма и маршрут по цехам — из PLM. Enter — собрать.",
            -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK RtEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    rt_start();
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_TAB) { /* между двумя полями */
    SetFocus(hwnd == g_rtDes ? g_rtOrder : g_rtDes);
    SendMessageW(GetFocus(), EM_SETSEL, 0, -1);
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    ShowWindow(g_rtWnd, SW_HIDE);
    return 0;
  }
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n' || wParam == L'\t' || wParam == 27)) return 0;
  WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  return CallWindowProcW(old, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK RtProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    rt_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = RS(660);
    mm->ptMinTrackSize.y = RS(360);
    return 0;
  }
  case WM_SIZE:
    rt_layout();
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_RT_PROGRESS:
    rt_status(L"Собираю из PLM… позиций: %d", (int)wParam);
    return 0;
  case WM_RT_DONE: {
    RtJob *j = (RtJob *)lParam;
    rt_job_free(g_rtJob);
    g_rtJob = j;
    rt_fill_list();
    SetWindowTextW(GetDlgItem(hwnd, ID_RT_BUILD), L"Собрать");
    double sec = (double)(GetTickCount64() - j->t0) / 1000.0;
    if (j->err[0] && !j->n) rt_status(L"%s", j->err);
    else if (g_rtCancel) rt_status(L"Остановлено: собрано %d позиций", j->n);
    else {
      int blanks = 0;
      for (int i = 0; i < j->n; i++)
        if (j->rows[i].f[RC_NOTE][0]) blanks++;
      if (blanks)
        rt_status(L"Готово: %d позиций за %.0f с · с пометками %d — см. «Примечание» и «Подробности»", j->n, sec,
                  blanks);
      else
        rt_status(L"Готово: %d позиций за %.0f с", j->n, sec);
    }
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (id == ID_RT_BUILD) rt_start();
    if (id == ID_RT_SAVE) rt_save();
    if (id == ID_RT_COPY) rt_copy();
    if (id == ID_RT_LOG) rt_show_log();
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void route_show(void) {
  if (!g_rtWnd) {
    HDC s = GetDC(NULL);
    g_rtS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = RtProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadRoute";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = RS(1100), h = RS(620);
    if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
    if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
    g_rtWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadRoute", L"Маршрутная ведомость — CursorPad",
                              WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                              wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                              NULL, NULL, g_inst, NULL);
    if (!g_rtWnd) return;
    round_corners(g_rtWnd);
    mk_btn(g_rtWnd, L"×", ID_PANEL_CLOSE);
    g_rtDes = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                              10, 10, g_rtWnd, NULL, g_inst, NULL);
    g_rtOrder = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
                                0, 10, 10, g_rtWnd, NULL, g_inst, NULL);
    HWND ed[2] = {g_rtDes, g_rtOrder};
    for (int i = 0; i < 2; i++) {
      if (g_fontBody) SendMessageW(ed[i], WM_SETFONT, (WPARAM)g_fontBody, FALSE);
      SetWindowLongPtrW(ed[i], GWLP_USERDATA, (LONG_PTR)SetWindowLongPtrW(ed[i], GWLP_WNDPROC, (LONG_PTR)RtEditProc));
    }
    mk_btn(g_rtWnd, L"Собрать", ID_RT_BUILD);
    mk_btn(g_rtWnd, L"Сохранить в Excel…", ID_RT_SAVE);
    mk_btn(g_rtWnd, L"Копировать", ID_RT_COPY);
    mk_btn(g_rtWnd, L"Подробности", ID_RT_LOG);
    g_rtList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
                               0, 0, 10, 10, g_rtWnd, NULL, g_inst, NULL);
    SendMessageW(g_rtList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    if (g_fontBody) SendMessageW(g_rtList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    for (int c = 0; c < RT_NCOL; c++) {
      LVCOLUMNW col;
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      col.pszText = (wchar_t *)kRtHead[c];
      col.cx = RS(kRtWidth[c] * 7 + 10);
      col.iSubItem = c;
      SendMessageW(g_rtList, LVM_INSERTCOLUMNW, (WPARAM)c, (LPARAM)&col);
    }
    rt_layout();
  }
  /* выбрана строка в поиске PLM — её обозначение сразу в поле */
  int i = plm_selected_index();
  if (i >= 0 && i < g_plmCount && !g_resultFiles && !g_rtBusy) {
    wchar_t des[200];
    card_des_from_name(g_plmEsi[i], des, 200);
    if (des[0]) SetWindowTextW(g_rtDes, des);
  }
  ShowWindow(g_rtWnd, IsIconic(g_rtWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_rtWnd);
  SetFocus(g_rtDes);
  SendMessageW(g_rtDes, EM_SETSEL, 0, -1);
}
