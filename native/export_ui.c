/* ---- Окно «Выгрузка из PLM» (сбор — export.c) ------------------------------

   Обозначение изделия (или строка, выбранная в поиске PLM), галочки — какие
   листы, «Собрать». Внизу таблица выбранного листа, «Сохранить в Excel…» —
   книга со всеми собранными листами, «Подробности» — весь проход. Через
   компьютер коллеги (раздача PLM) — тот же сбор у него. */

#define ID_XP_BUILD 280
#define ID_XP_CHK 281 /* +лист: 281…285 */
#define ID_XP_VIEW 287
#define ID_XP_SAVE 288
#define ID_XP_LOG 289
#define ID_XP_PKG 290 /* «В пакет для загрузки…» — окно пакета, заполненное этой выгрузкой */
#define ID_XP_TIMER 291 /* пока идёт сбор: полоса хода, время, свежие «Подробности» */

static HWND g_xpWnd, g_xpDes, g_xpList, g_xpView, g_xpLog, g_xpChk[XP_NT];
static XpJob *g_xpJob; /* последняя собранная выгрузка */
static volatile LONG g_xpBusy, g_xpCancel;
static wchar_t g_xpStatus[300];
static float g_xpS = 1.0f;
static long g_xpPickId;
static wchar_t g_xpPickDes[200];
static int g_xpShown = -1; /* какой лист сейчас в таблице */
static XpJob *g_xpRun;      /* идущий сбор (до WM_XP_DONE) — для полосы хода и «Подробностей» */
static int g_xpCount;       /* позиций пройдено — из сообщений сбора */
static BOOL g_xpStopping;   /* нажат «Стоп» — ждём, пока потоки выйдут */
static RtJob *g_xpLogSrc;   /* чей журнал в окне «Подробности» и сколько знаков в нём уже есть */
static int g_xpLogLen;

static int XS(int v) { return (int)(v * g_xpS + 0.5f); }

/* какие листы отмечены — помнится между запусками: export.txt, «what\tчисло» */
static void xp_prefs_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\export.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static unsigned xp_checked(void) {
  unsigned w = 0;
  for (int k = 0; k < XP_NT; k++)
    if (g_xpChk[k] && SendMessageW(g_xpChk[k], BM_GETCHECK, 0, 0) == BST_CHECKED) w |= 1u << k;
  return w;
}

static void xp_prefs_save(void) {
  if (!g_dataDir[0]) return;
  wchar_t buf[60], p[MAX_PATH];
  _snwprintf(buf, 60, L"what6\t%u\n", xp_checked()); /* «what6» — с листом «Заготовки» */
  xp_prefs_path(p);
  share_write_ex(p, buf, TRUE);
}

static void xp_prefs_load(void) {
  unsigned what = (1u << XP_NT) - 1; /* по умолчанию — все листы */
  wchar_t p[MAX_PATH];
  xp_prefs_path(p);
  wchar_t *t = g_dataDir[0] ? share_read(p) : NULL;
  if (t) {
    wchar_t *pp = t, *line;
    while ((line = share_next_line(&pp)) != NULL) {
      wchar_t *f[3];
      if (share_split(line, f, 3) < 2) continue;
      if (!wcscmp(f[0], L"what6")) what = (unsigned)wcstoul(f[1], NULL, 10);
      else if (!wcscmp(f[0], L"what")) what = (unsigned)wcstoul(f[1], NULL, 10) | (1u << XP_PF); /* до .81 листа не было */
    }
    free(t);
  }
  for (int k = 0; k < XP_NT; k++)
    SendMessageW(g_xpChk[k], BM_SETCHECK, (what >> k) & 1 ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void xp_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_xpStatus, 300, fmt, ap);
  va_end(ap);
  g_xpStatus[299] = 0;
  if (g_xpWnd) {
    RECT rc;
    GetClientRect(g_xpWnd, &rc);
    RECT st = {0, XS(118), rc.right, XS(153)};
    InvalidateRect(g_xpWnd, &st, FALSE);
  }
}

static DWORD WINAPI xp_thread(LPVOID param) {
  XpJob *x = (XpJob *)param;
  x->rt->t0 = GetTickCount64();
  if (share_client_on()) share_export(x);
  else xp_build(x);
  if (!g_xpWnd || !PostMessageW(g_xpWnd, WM_XP_DONE, 0, (LPARAM)x)) xp_job_free(x);
  InterlockedExchange(&g_xpBusy, 0);
  return 0;
}

static void xp_start(void) {
  if (InterlockedCompareExchange(&g_xpBusy, 1, 0) != 0) {
    InterlockedExchange(&g_xpCancel, 1); /* второй щелчок — остановить */
    card_cancel_all(&g_xpCancel);        /* идущие запросы — прервать, а не ждать */
    g_xpStopping = TRUE;
    xp_status(L"Останавливаю — прерываю запросы к базе…");
    return;
  }
  XpJob *x = xp_job_new();
  if (!x) {
    InterlockedExchange(&g_xpBusy, 0);
    return;
  }
  RtJob *j = x->rt;
  GetWindowTextW(g_xpDes, j->des, 200);
  wchar_t *s = j->des;
  while (*s == L' ') s++;
  memmove(j->des, s, (wcslen(s) + 1) * sizeof(wchar_t));
  size_t l = wcslen(j->des);
  while (l && j->des[l - 1] == L' ') j->des[--l] = 0;
  x->what = xp_checked();
  if (!j->des[0] || !x->what) {
    xp_job_free(x);
    InterlockedExchange(&g_xpBusy, 0);
    xp_status(!x->what ? L"Отметьте хотя бы один лист" : L"Впишите обозначение изделия");
    SetFocus(g_xpDes);
    return;
  }
  xp_prefs_save();
  if (g_xpPickId && !_wcsicmp(j->des, g_xpPickDes)) j->rootId = g_xpPickId;
  g_xpCancel = 0;
  j->cancel = &g_xpCancel;
  j->notify = g_xpWnd;
  x->notify = g_xpWnd;
  j->deadline = GetTickCount64() + 20 * 60 * 1000; /* большая сборка со всеми листами — до 20 минут */
  rt_log(j, L"Выгрузка из PLM: %s\r\n", j->des);
  SetWindowTextW(GetDlgItem(g_xpWnd, ID_XP_BUILD), L"Стоп");
  g_xpRun = x;
  g_xpCount = 0;
  g_xpStopping = FALSE;
  x->rt->t0 = GetTickCount64();
  xp_status(L"Собираю состав из PLM…");
  HANDLE t = CreateThread(NULL, 0, xp_thread, x, 0, NULL);
  if (t) {
    CloseHandle(t);
    SetTimer(g_xpWnd, ID_XP_TIMER, 300, NULL);
  } else {
    g_xpRun = NULL;
    SetWindowTextW(GetDlgItem(g_xpWnd, ID_XP_BUILD), L"Собрать");
    xp_job_free(x);
    InterlockedExchange(&g_xpBusy, 0);
  }
}

/* таблица окна — выбранный лист; больше 3000 строк не показываем (в Excel — все) */
#define XP_VIEW_MAX 3000
static void xp_fill_list(void) {
  int sel = (int)SendMessageW(g_xpView, CB_GETCURSEL, 0, 0);
  if (sel < 0 || sel >= XP_NT) sel = 0;
  int k = kXpOrder[sel]; /* в списке листы — в порядке книги */
  SendMessageW(g_xpList, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(g_xpList);
  if (k != g_xpShown) {
    while (SendMessageW(g_xpList, LVM_DELETECOLUMN, 0, 0)) {
    }
    for (int c = 0; c < kXpSheets[k].ncols; c++) {
      LVCOLUMNW col;
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      col.pszText = (wchar_t *)kXpSheets[k].head[c];
      col.cx = XS(kXpSheets[k].width[c] * 7 + 10);
      col.iSubItem = c;
      SendMessageW(g_xpList, LVM_INSERTCOLUMNW, (WPARAM)c, (LPARAM)&col);
    }
    g_xpShown = k;
  }
  if (g_xpJob) {
    XpTable *t = &g_xpJob->t[k];
    int n = t->n < XP_VIEW_MAX ? t->n : XP_VIEW_MAX;
    for (int r = 0; r < n; r++) {
      LVITEMW it;
      memset(&it, 0, sizeof(it));
      it.mask = LVIF_TEXT;
      it.iItem = r;
      it.pszText = (wchar_t *)xp_cell(t, r, 0);
      int idx = (int)SendMessageW(g_xpList, LVM_INSERTITEMW, 0, (LPARAM)&it);
      for (int c = 1; c < t->ncols; c++) {
        LVITEMW si;
        memset(&si, 0, sizeof(si));
        si.iSubItem = c;
        si.pszText = (wchar_t *)xp_cell(t, r, c);
        SendMessageW(g_xpList, LVM_SETITEMTEXTW, (WPARAM)idx, (LPARAM)&si);
      }
    }
  }
  SendMessageW(g_xpList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_xpList, NULL, TRUE);
}

/* названия листов в выпадающем списке — с числом строк */
static void xp_fill_view(void) {
  int cur = (int)SendMessageW(g_xpView, CB_GETCURSEL, 0, 0);
  SendMessageW(g_xpView, CB_RESETCONTENT, 0, 0);
  for (int o = 0; o < XP_NT; o++) {
    int k = kXpOrder[o];
    wchar_t s[80];
    if (g_xpJob && ((g_xpJob->what >> k) & 1)) _snwprintf(s, 80, L"%s — %d", kXpSheets[k].name, g_xpJob->t[k].n);
    else lstrcpynW(s, kXpSheets[k].name, 80);
    SendMessageW(g_xpView, CB_ADDSTRING, 0, (LPARAM)s);
  }
  SendMessageW(g_xpView, CB_SETCURSEL, cur >= 0 ? cur : 0, 0);
}

static void xp_save(void) {
  int any = 0; /* через коллегу обхода у нас нет — только его таблицы */
  for (int k = 0; g_xpJob && k < XP_NT; k++) any += g_xpJob->t[k].n;
  if (!any) {
    xp_status(L"Сначала «Собрать»");
    return;
  }
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t file[MAX_PATH];
  _snwprintf(file, MAX_PATH - 6, L"Выгрузка из PLM %s от %02d.%02d.%02d", g_xpJob->rt->des, st.wDay, st.wMonth,
             st.wYear % 100);
  file[MAX_PATH - 7] = 0;
  for (wchar_t *p = file; *p; p++)
    if (wcschr(L"\\/:*?\"<>|", *p)) *p = L'_';
  wcscat(file, L".xlsx");
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_xpWnd;
  of.lpstrFilter = L"Книга Excel (*.xlsx)\0*.xlsx\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"xlsx";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  XlSheet sh[XP_NT];
  XlBookSheet bs[XP_NT];
  wchar_t titles[XP_NT][300];
  int n = 0;
  for (int o = 0; o < XP_NT; o++) {
    int k = kXpOrder[o];
    if (!((g_xpJob->what >> k) & 1)) continue;
    memset(&sh[n], 0, sizeof(sh[n]));
    sh[n].ncols = kXpSheets[k].ncols;
    sh[n].head = kXpSheets[k].head;
    sh[n].width = kXpSheets[k].width;
    sh[n].plain = 1;
    _snwprintf(titles[n], 300, L"%s · %s · из PLM %02d.%02d.%04d", kXpSheets[k].name, g_xpJob->rt->des, st.wDay,
               st.wMonth, st.wYear);
    titles[n][299] = 0;
    bs[n].name = kXpSheets[k].name;
    bs[n].title = titles[n];
    bs[n].sh = &sh[n];
    bs[n].nrows = g_xpJob->t[k].n;
    bs[n].cell = xp_cell;
    bs[n].ctx = &g_xpJob->t[k];
    n++;
  }
  if (!n || !xl_save_book(file, bs, n)) {
    MessageBoxW(g_xpWnd, L"Не удалось записать файл — он не открыт сейчас в Excel?", L"Выгрузка из PLM",
                MB_ICONWARNING);
    return;
  }
  xp_status(L"Сохранено: %s", file);
  ShellExecuteW(NULL, L"open", file, NULL, NULL, SW_SHOWNORMAL);
}

static void package_show(BOOL fill); /* окно пакета — package_ui.c */

/* «Подробности» — и во время сбора: журнал идущего сбора, дописывается по ходу */
static void xp_show_log(void) {
  RtJob *src = g_xpRun ? g_xpRun->rt : g_xpJob ? g_xpJob->rt : NULL;
  if (!src || !src->logLen) {
    xp_status(L"Подробностей пока нет — сначала «Собрать»");
    return;
  }
  if (!log_window(&g_xpLog, g_xpWnd, L"Выгрузка из PLM — подробности", XS(820), XS(560))) return;
  int len = src->logLen; /* сбор дописывает дальше — берём, сколько есть сейчас */
  log_put(g_xpLog, src->log, len, FALSE);
  g_xpLogSrc = src;
  g_xpLogLen = len;
  ShowWindow(g_xpLog, SW_SHOWNORMAL);
  SetForegroundWindow(g_xpLog);
}

/* в открытые «Подробности» — то, что журнал успел дописать */
static void xp_log_follow(void) {
  if (!g_xpLogSrc || !g_xpLog || !IsWindow(g_xpLog) || !IsWindowVisible(g_xpLog)) return;
  int len = g_xpLogSrc->logLen;
  if (len > g_xpLogLen) log_put(g_xpLog, g_xpLogSrc->log + g_xpLogLen, len - g_xpLogLen, TRUE);
  g_xpLogLen = len;
}

/* ход сбора, 0…1000: состав — половина, листы по позициям — треть, извещения —
   остальное (что не отмечено — не считается). На этапе — rt->progDone из progTotal:
   ветки состава у потоков, позиции, пачки извещений. */
static int xp_permille(const XpJob *x) {
  BOOL sheets = (x->what & ((1u << XP_OPS) | (1u << XP_MAT) | (1u << XP_TOOL) | (1u << XP_PF))) != 0;
  BOOL ecn = ((x->what >> XP_ECN) & 1) != 0;
  int w0 = 50, w1 = sheets ? 35 : 0, w2 = ecn ? 15 : 0;
  LONG done = x->rt->progDone, total = x->rt->progTotal;
  double f = total > 0 ? (double)done / (double)total : 0;
  if (f > 1) f = 1;
  double base = 0, cur = w0;
  if (x->stage == XP_ECN) base = w0 + w1, cur = w2;
  else if (x->stage != XP_COMP) base = w0, cur = w1;
  int p = (int)((base + cur * f) * 1000.0 / (w0 + w1 + w2));
  return p < 0 ? 0 : p > 1000 ? 1000 : p;
}

static void xp_layout(void) {
  RECT rc;
  GetClientRect(g_xpWnd, &rc);
  int pad = XS(14), y = PANEL_TITLE_H + XS(24), h = XS(26);
  place_panel_close(g_xpWnd);
  MoveWindow(g_xpDes, pad, y, XS(320), h, TRUE);
  MoveWindow(GetDlgItem(g_xpWnd, ID_XP_BUILD), pad + XS(332), y - XS(1), XS(110), h + XS(2), TRUE);
  int cx = pad, cy = y + XS(34);
  static const int cw[XP_NT] = {130, 100, 105, 110, 100, 110}; /* по номеру листа */
  for (int o = 0; o < XP_NT; o++) {
    int k = kXpOrder[o];
    MoveWindow(g_xpChk[k], cx, cy, XS(cw[k]), XS(22), TRUE);
    cx += XS(cw[k] + 8);
  }
  int by = rc.bottom - pad - XS(30);
  int vy = XS(158);
  MoveWindow(g_xpView, pad, vy, XS(260), XS(300), TRUE);
  int ly = vy + XS(32);
  MoveWindow(g_xpList, pad, ly, rc.right - pad * 2, by - XS(10) - ly, TRUE);
  MoveWindow(GetDlgItem(g_xpWnd, ID_XP_SAVE), pad, by, XS(170), XS(30), TRUE);
  MoveWindow(GetDlgItem(g_xpWnd, ID_XP_LOG), pad + XS(180), by, XS(120), XS(30), TRUE);
  MoveWindow(GetDlgItem(g_xpWnd, ID_XP_PKG), pad + XS(310), by, XS(210), XS(30), TRUE);
}

static void xp_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Выгрузка из PLM");
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  int pad = XS(14), y = PANEL_TITLE_H + XS(6);
  RECT a = {pad, y, pad + XS(500), y + XS(18)};
  DrawTextW(hdc, L"Обозначение изделия — или выберите строку в поиске PLM", -1, &a,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, g_xpBusy ? COL_SAGE : COL_INK);
  RECT st = {pad, XS(118), rc.right - pad, XS(140)};
  if (g_xpBusy && g_xpRun) { /* полоса хода */
    RECT tr = {pad, XS(143), rc.right - pad, XS(150)};
    HBRUSH bt = CreateSolidBrush(COL_LINE);
    FillRect(hdc, &tr, bt);
    DeleteObject(bt);
    RECT fr = tr;
    fr.right = tr.left + (int)((long long)(tr.right - tr.left) * xp_permille(g_xpRun) / 1000);
    HBRUSH bf = CreateSolidBrush(g_xpStopping ? COL_MUTED : COL_SAGE);
    if (fr.right > fr.left) FillRect(hdc, &fr, bf);
    DeleteObject(bf);
  }
  DrawTextW(hdc,
            g_xpStatus[0] ? g_xpStatus
                          : L"Состав (ЭСИ), операции ТП, материалы, инструмент и извещения — одной книгой Excel. "
                            L"Enter — собрать.",
            -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK XpEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    xp_start();
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    ShowWindow(g_xpWnd, SW_HIDE);
    return 0;
  }
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n' || wParam == 27)) return 0;
  WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  return CallWindowProcW(old, hwnd, msg, wParam, lParam);
}

static const wchar_t *const kXpStage[XP_NT] = {L"состав", L"операции, материалы, инструмент и заготовки", L"",
                                               L"", L"извещения", L""};

/* строка состояния идущего сбора: этап, процент, позиции, время */
static void xp_run_status(void) {
  XpJob *x = g_xpRun;
  if (!x || !g_xpBusy) return;
  ULONGLONG sec = (GetTickCount64() - x->rt->t0) / 1000;
  if (g_xpStopping) {
    xp_status(L"Останавливаю — прерываю запросы к базе… %u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    return;
  }
  int st = x->stage >= 0 && x->stage < XP_NT ? x->stage : XP_COMP;
  LONG done = x->rt->progDone, total = x->rt->progTotal;
  wchar_t part[80] = L"";
  if (total > 0)
    _snwprintf(part, 80, st == XP_COMP ? L" · веток %ld из %ld" : st == XP_ECN ? L" · пачек %ld из %ld"
                                                                               : L" · позиций %ld из %ld",
               done, total);
  wchar_t walked[60] = L"";
  if (st == XP_COMP) _snwprintf(walked, 60, L" · пройдено позиций %d", g_xpCount);
  xp_status(L"Собираю %s… %d%%%s%s · %u:%02u", kXpStage[st][0] ? kXpStage[st] : L"состав", xp_permille(x) / 10,
            part, walked, (unsigned)(sec / 60), (unsigned)(sec % 60));
}

static LRESULT CALLBACK XpProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    xp_paint(hwnd, mem);
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
    mm->ptMinTrackSize.x = XS(700);
    mm->ptMinTrackSize.y = XS(400);
    return 0;
  }
  case WM_SIZE:
    xp_layout();
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
  case WM_CTLCOLORSTATIC: { /* галочки — на фоне окна */
    HDC hdc = (HDC)wParam;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)bg_brush(FALSE);
  }
  case WM_RT_PROGRESS:
  case WM_XP_PROGRESS:
    g_xpCount = (int)wParam;
    xp_run_status();
    return 0;
  case WM_TIMER:
    if (wParam == ID_XP_TIMER) {
      if (g_xpStopping) card_cancel_all(&g_xpCancel); /* запрос, начатый в миг «Стопа», — тоже */
      xp_run_status();
      xp_log_follow();
    }
    return 0;
  case WM_XP_DONE: {
    XpJob *x = (XpJob *)lParam;
    KillTimer(hwnd, ID_XP_TIMER);
    g_xpRun = NULL;
    g_xpStopping = FALSE;
    if (g_xpJob && g_xpLogSrc == g_xpJob->rt) g_xpLogSrc = NULL; /* прежний журнал уходит; текст в окне остаётся */
    xp_job_free(g_xpJob);
    g_xpJob = x;
    xp_log_follow(); /* «Подробности» этого сбора открыты — дописать конец */
    xp_fill_view();
    xp_fill_list();
    SetWindowTextW(GetDlgItem(hwnd, ID_XP_BUILD), L"Собрать");
    double sec = (double)(GetTickCount64() - x->rt->t0) / 1000.0;
    int rows = 0;
    for (int k = 0; k < XP_NT; k++) rows += x->t[k].n;
    if (x->rt->err[0] && !rows) xp_status(L"%s", x->rt->err);
    else if (g_xpCancel) xp_status(L"Остановлено: что успели — в таблице, можно сохранить");
    else
      xp_status(L"Готово за %.0f с (обход %.0f, операции %.0f, извещения %.0f): позиций %d · заготовок %d · "
                L"операций %d · материалов %d · инструмента %d · извещений %d",
                sec, x->secWalk, x->secSheets, x->secEcn, x->t[XP_COMP].n, x->t[XP_PF].n, x->t[XP_OPS].n,
                x->t[XP_MAT].n, x->t[XP_TOOL].n, x->t[XP_ECN].n);
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (id == ID_XP_BUILD) xp_start();
    if (id == ID_XP_SAVE) xp_save();
    if (id == ID_XP_LOG) xp_show_log();
    if (id == ID_XP_PKG) package_show(TRUE);
    if (id >= ID_XP_CHK && id < ID_XP_CHK + XP_NT && HIWORD(wParam) == BN_CLICKED) xp_prefs_save();
    if (id == ID_XP_VIEW && HIWORD(wParam) == CBN_SELCHANGE) xp_fill_list();
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

static void export_show(void) {
  if (!g_xpWnd) {
    HDC s = GetDC(NULL);
    g_xpS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = XpProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadExport";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = XS(1100), h = XS(640);
    if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
    if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
    g_xpWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadExport", L"Выгрузка из PLM — CursorPad",
                              WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                              wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                              NULL, NULL, g_inst, NULL);
    if (!g_xpWnd) return;
    round_corners(g_xpWnd);
    mk_btn(g_xpWnd, L"×", ID_PANEL_CLOSE);
    g_xpDes = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                              10, 10, g_xpWnd, NULL, g_inst, NULL);
    if (g_fontBody) SendMessageW(g_xpDes, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    SetWindowLongPtrW(g_xpDes, GWLP_USERDATA,
                      (LONG_PTR)SetWindowLongPtrW(g_xpDes, GWLP_WNDPROC, (LONG_PTR)XpEditProc));
    mk_btn(g_xpWnd, L"Собрать", ID_XP_BUILD);
    for (int k = 0; k < XP_NT; k++) {
      g_xpChk[k] = CreateWindowExW(0, L"BUTTON", kXpSheets[k].name, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                   0, 0, 10, 10, g_xpWnd, (HMENU)(INT_PTR)(ID_XP_CHK + k), g_inst, NULL);
      if (g_fontUi) SendMessageW(g_xpChk[k], WM_SETFONT, (WPARAM)g_fontUi, FALSE);
    }
    g_xpView = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                               0, 0, 10, 300, g_xpWnd, (HMENU)(INT_PTR)ID_XP_VIEW, g_inst, NULL);
    if (g_fontUi) SendMessageW(g_xpView, WM_SETFONT, (WPARAM)g_fontUi, FALSE);
    mk_btn(g_xpWnd, L"Сохранить в Excel…", ID_XP_SAVE);
    mk_btn(g_xpWnd, L"Подробности", ID_XP_LOG);
    mk_btn(g_xpWnd, L"В пакет для загрузки…", ID_XP_PKG);
    g_xpList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
                               0, 0, 10, 10, g_xpWnd, NULL, g_inst, NULL);
    SendMessageW(g_xpList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    if (g_fontBody) SendMessageW(g_xpList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    xp_prefs_load();
    xp_fill_view();
    xp_fill_list();
    xp_layout();
  }
  /* выбрана строка в поиске PLM — её обозначение сразу в поле */
  int i = plm_selected_index();
  if (i >= 0 && i < g_plmCount && !g_resultFiles && !g_xpBusy) {
    wchar_t des[200];
    card_des_from_name(g_plmEsi[i], des, 200);
    if (des[0]) {
      SetWindowTextW(g_xpDes, des);
      g_xpPickId = g_plmIds[i];
      lstrcpynW(g_xpPickDes, des, 200);
    }
  }
  ShowWindow(g_xpWnd, IsIconic(g_xpWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_xpWnd);
  SetFocus(g_xpDes);
  SendMessageW(g_xpDes, EM_SETSEL, 0, -1);
}
