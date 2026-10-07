/* ---- Окно маршрутной ведомости (сам сбор — route.c) ----------------------

   После share.c: копирование берёт его ShareBuf, а сбор у коллеги — его
   share_route. */

#include <commdlg.h> /* окно «Сохранить как» */

/* ---- окно ------------------------------------------------------------------- */


static HWND g_rtWnd, g_rtDes, g_rtOrder, g_rtNum, g_rtList, g_rtLog;
static RtJob *g_rtJob;  /* последняя собранная ведомость */
static volatile LONG g_rtBusy, g_rtCancel;
static wchar_t g_rtStatus[300];
static float g_rtS = 1.0f;
static long g_rtPickId;         /* строка, выбранная в поиске PLM */
static wchar_t g_rtPickDes[200]; /* её обозначение — пока в поле оно же, берём строку */

static int RS(int v) { return (int)(v * g_rtS + 0.5f); }

/* «Входит в» и «Заказ №» помнятся между запусками: route.txt рядом с
   заметками, строки «order» и «num» через табуляцию */
static void rt_prefs_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\route.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static void rt_prefs_save(void) {
  if (!g_dataDir[0] || !g_rtOrder || !g_rtNum) return;
  wchar_t o[120], n[120], buf[300], p[MAX_PATH];
  GetWindowTextW(g_rtOrder, o, 120);
  GetWindowTextW(g_rtNum, n, 120);
  for (wchar_t *c = o; *c; c++)
    if (*c == L'\t' || *c == L'\r' || *c == L'\n') *c = L' ';
  for (wchar_t *c = n; *c; c++)
    if (*c == L'\t' || *c == L'\r' || *c == L'\n') *c = L' ';
  _snwprintf(buf, 300, L"order\t%s\nnum\t%s\n", o, n);
  buf[299] = 0;
  rt_prefs_path(p);
  share_write_ex(p, buf, TRUE);
}

static void rt_prefs_load(void) {
  if (!g_dataDir[0]) return;
  wchar_t p[MAX_PATH];
  rt_prefs_path(p);
  wchar_t *t = share_read(p);
  if (!t) return;
  wchar_t *pp = t, *line;
  while ((line = share_next_line(&pp)) != NULL) {
    wchar_t *f[4];
    int n = share_split(line, f, 4);
    if (n >= 2 && !wcscmp(f[0], L"order")) SetWindowTextW(g_rtOrder, f[1]);
    if (n >= 2 && !wcscmp(f[0], L"num")) SetWindowTextW(g_rtNum, f[1]);
  }
  free(t);
}

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

static void rt_start_ex(BOOL dump) {
  if (InterlockedCompareExchange(&g_rtBusy, 1, 0) != 0) {
    InterlockedExchange(&g_rtCancel, 1); /* второй щелчок — остановить */
    card_cancel_all(&g_rtCancel);        /* и прервать идущие запросы */
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
  rt_prefs_save();
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
  if (g_rtPickId && !_wcsicmp(j->des, g_rtPickDes)) j->rootId = g_rtPickId;
  j->dump = dump;
  g_rtCancel = 0;
  j->cancel = &g_rtCancel;
  j->notify = g_rtWnd;
  j->deadline = GetTickCount64() + 10 * 60 * 1000; /* своя база — до 10 минут на большую сборку */
  rt_log(j, L"Маршрутная ведомость: %s%s%s\r\n", j->des, j->order[0] ? L", входит в " : L"", j->order);
  SetWindowTextW(GetDlgItem(g_rtWnd, ID_RT_BUILD), L"Стоп");
  rt_status(dump ? L"Собираю из PLM с выгрузкой для проверки…" : L"Собираю из PLM…");
  HANDLE t = CreateThread(NULL, 0, rt_thread, j, 0, NULL);
  if (t) CloseHandle(t);
  else {
    rt_job_free(j);
    InterlockedExchange(&g_rtBusy, 0);
  }
}

static void rt_start(void) { rt_start_ex(FALSE); }

/* «Выгрузка для проверки» готова: весь текст — в файл на рабочий стол и
   открыть; этот файл и присылают, если ведомость где-то пустая */
static void rt_dump_save(RtJob *j) {
  wchar_t dir[MAX_PATH], path[MAX_PATH], des[120];
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, dir))) lstrcpynW(dir, g_dataDir, MAX_PATH);
  lstrcpynW(des, j->des, 120);
  for (wchar_t *p = des; *p; p++)
    if (wcschr(L"\\/:*?\"<>|", *p)) *p = L'_';
  SYSTEMTIME t;
  GetLocalTime(&t);
  _snwprintf(path, MAX_PATH, L"%s\\МВ проверка %s %02d.%02d %02d-%02d.txt", dir, des, t.wDay, t.wMonth, t.wHour,
             t.wMinute);
  path[MAX_PATH - 1] = 0;
  if (share_write_ex(path, j->log, TRUE)) {
    rt_status(L"Выгрузка для проверки: %s — пришлите этот файл", path);
    ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
  } else {
    rt_status(L"Не удалось записать %s", path);
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

/* ячейка для Excel: материал — в две строки, сортамент и марка отдельно
   («Круг 40 ГОСТ 2590-2006» / «38ХС ГОСТ 4543-2016»); в окне и в
   «Копировать» — как был, через « / » */
static const wchar_t *rt_cell(void *ctx, int r, int c) {
  const wchar_t *v = ((RtJob *)ctx)->rows[r].f[c];
  if (c != RC_MAT) return v;
  static wchar_t buf[210];
  const wchar_t *sl = wcsstr(v, L" / ");
  if (!sl) return v;
  _snwprintf(buf, 210, L"%.*s\n%s", (int)(sl - v), v, sl + 3);
  buf[209] = 0;
  return buf;
}

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

/* текст для колонтитула Excel: «&» там — начало кода, пишется «&&» */
static void rt_hf_text(const wchar_t *in, wchar_t *out, int cap) {
  int k = 0;
  for (; *in && k < cap - 2; in++) {
    if (*in == L'&') out[k++] = L'&';
    out[k++] = *in;
  }
  out[k] = 0;
}

/* Колонтитулы, как просили для печати ведомости:
   слева вверху (18) — «МВ по <обозначение>» и «Заказ № <номер>»;
   справа вверху (24) — «Заказ № <номер>»;
   справа внизу (18) — то же, что слева вверху;
   внизу по центру (18) — «МВ по <обозначение> Страница N из M». */
static void rt_header_footer(wchar_t *hdr, wchar_t *ftr, int cap) {
  wchar_t des[200], num[120], d[420], z[260];
  const wchar_t *src = g_rtJob->n && g_rtJob->rows[0].f[RC_DES][0] ? g_rtJob->rows[0].f[RC_DES] : g_rtJob->des;
  rt_hf_text(src, des, 200);
  GetWindowTextW(g_rtNum, num, 120);
  wchar_t *q = num;
  while (*q == L' ') q++;
  size_t l = wcslen(q);
  while (l && q[l - 1] == L' ') q[--l] = 0;
  wchar_t zq[260];
  rt_hf_text(q, zq, 260);
  z[0] = 0;
  if (zq[0]) _snwprintf(z, 260, L"Заказ %s%s", zq[0] == L'№' ? L"" : L"№ ", zq);
  z[259] = 0;
  _snwprintf(d, 420, L"МВ по %s%s%s", des, z[0] ? L"\n" : L"", z);
  d[419] = 0;
  if (z[0]) _snwprintf(hdr, cap, L"&L&18%s&R&24%s", d, z);
  else _snwprintf(hdr, cap, L"&L&18%s", d);
  _snwprintf(ftr, cap, L"&C&18МВ по %s Страница &P из &N&R&18%s", des, d);
  hdr[cap - 1] = ftr[cap - 1] = 0;
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
  rt_prefs_save();
  wchar_t hdr[1200], ftr[1200];
  rt_header_footer(hdr, ftr, 1200);
  /* без строки заголовка над шапкой (с 2026.09.23.52): обозначение и заказ —
     в колонтитулах */
  /* «Н. расх. на изделие» — формулой: норма на 1 деталь × количество на изделие */
  XlSheet sh = {RT_NCOL, kRtHead, kRtXlWidth, hdr, ftr, RC_NORMTOT + 1, RC_NORM1, RC_QTYTOT, 0};
  /* масштаб «Разметки страницы» — чтобы лист A3 альбомный (420 мм = 1587
     точек при 100%) встал во всю ширину окна Excel на этом экране: без
     пустот по бокам (с 2026.09.23.58; было 70% — на широком экране по
     бокам серые поля). Минус ~110 точек — номера строк и полоса прокрутки */
  {
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    HDC dc = GetDC(NULL);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    int w = (int)((wa.right - wa.left) * 96L / (dpi > 0 ? dpi : 96)); /* в точках при 100% */
    int z = (w - 110) * 100 / 1587;
    sh.zoom = z < 40 ? 40 : z > 200 ? 200 : z;
  }
  if (!xl_save(file, NULL, &sh, g_rtJob->n, rt_cell, g_rtJob)) {
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

/* ---- окно «Подробности» — общее для ведомости и выгрузки ---------------------
   Крестик окно уничтожает: прежде оставалась ссылка на закрытое окно, и
   «Подробности» больше не открывались (до 2026.09.23.84). Теперь — проверка
   IsWindow и новое окно. */
static BOOL log_window(HWND *pw, HWND owner, const wchar_t *title, int w, int h) {
  if (*pw && IsWindow(*pw)) return TRUE;
  *pw = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"EDIT", title,
                        WS_OVERLAPPEDWINDOW | ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL |
                            ES_AUTOHSCROLL,
                        CW_USEDEFAULT, CW_USEDEFAULT, w, h, owner, NULL, g_inst, NULL);
  if (!*pw) return FALSE;
  if (g_fontMono) SendMessageW(*pw, WM_SETFONT, (WPARAM)g_fontMono, FALSE);
  SendMessageW(*pw, EM_SETLIMITTEXT, RT_LOG, 0); /* проверочная выгрузка длиннее 32 тысяч */
  return TRUE;
}

/* n знаков журнала — в окно: заменить всё или дописать в конец. Дописанное
   прокручивается, только если читали конец; листали выше — место остаётся. */
static void log_put(HWND w, const wchar_t *s, int n, BOOL append) {
  if (!w || n < 0) return;
  wchar_t *t = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
  if (!t) return;
  memcpy(t, s, (size_t)n * sizeof(wchar_t));
  t[n] = 0;
  if (!append) {
    SetWindowTextW(w, t);
  } else if (n) {
    int len = GetWindowTextLengthW(w);
    DWORD a = 0, b = 0;
    SendMessageW(w, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
    int top = (int)SendMessageW(w, EM_GETFIRSTVISIBLELINE, 0, 0);
    BOOL atEnd = (int)b >= len;
    SendMessageW(w, WM_SETREDRAW, FALSE, 0);
    SendMessageW(w, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(w, EM_REPLACESEL, FALSE, (LPARAM)t);
    if (!atEnd) {
      SendMessageW(w, EM_SETSEL, (WPARAM)a, (LPARAM)b);
      int now = (int)SendMessageW(w, EM_GETFIRSTVISIBLELINE, 0, 0);
      SendMessageW(w, EM_LINESCROLL, 0, (LPARAM)(top - now));
    }
    SendMessageW(w, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(w, NULL, TRUE);
  }
  free(t);
}

/* «Подробности»: весь проход сбора — чтобы было видно, где пусто и почему */
static void rt_show_log(void) {
  if (!g_rtJob || !g_rtJob->log[0]) {
    rt_status(g_rtBusy ? L"Подробности — когда сбор закончится" : L"Подробностей пока нет — сначала «Собрать»");
    return;
  }
  if (!log_window(&g_rtLog, g_rtWnd, L"Маршрутная ведомость — подробности", RS(820), RS(560))) return;
  log_put(g_rtLog, g_rtJob->log, g_rtJob->logLen, FALSE);
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
  MoveWindow(g_rtNum, pad + RS(494), y, RS(150), h, TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_BUILD), pad + RS(656), y - RS(1), RS(110), h + RS(2), TRUE);
  int by = rc.bottom - pad - RS(30);
  int ly = RS(116);
  MoveWindow(g_rtList, pad, ly, rc.right - pad * 2, by - RS(10) - ly, TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_SAVE), pad, by, RS(170), RS(30), TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_COPY), pad + RS(180), by, RS(120), RS(30), TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_LOG), pad + RS(310), by, RS(120), RS(30), TRUE);
  MoveWindow(GetDlgItem(g_rtWnd, ID_RT_DUMP), pad + RS(440), by, RS(200), RS(30), TRUE);
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
  RECT a = {pad, y, pad + RS(300), y + RS(18)}, b = {pad + RS(312), y, pad + RS(482), y + RS(18)},
       c = {pad + RS(494), y, pad + RS(644), y + RS(18)};
  DrawTextW(hdc, L"Обозначение сборки или детали", -1, &a, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  DrawTextW(hdc, L"Входит в (КЗ), можно пусто", -1, &b, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  DrawTextW(hdc, L"Заказ № — в колонтитулы", -1, &c, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
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
    SetFocus(hwnd == g_rtDes ? g_rtOrder : hwnd == g_rtOrder ? g_rtNum : g_rtDes);
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
    else if (j->dump) rt_dump_save(j);
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
    if (HIWORD(wParam) == EN_KILLFOCUS && ((HWND)lParam == g_rtOrder || (HWND)lParam == g_rtNum)) {
      rt_prefs_save(); /* ушли из поля — запомнить */
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (id == ID_RT_BUILD) rt_start();
    if (id == ID_RT_SAVE) rt_save();
    if (id == ID_RT_COPY) rt_copy();
    if (id == ID_RT_LOG) rt_show_log();
    if (id == ID_RT_DUMP) rt_start_ex(TRUE);
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
    g_rtNum = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
                              0, 10, 10, g_rtWnd, NULL, g_inst, NULL);
    HWND ed[3] = {g_rtDes, g_rtOrder, g_rtNum};
    for (int i = 0; i < 3; i++) {
      if (g_fontBody) SendMessageW(ed[i], WM_SETFONT, (WPARAM)g_fontBody, FALSE);
      SetWindowLongPtrW(ed[i], GWLP_USERDATA, (LONG_PTR)SetWindowLongPtrW(ed[i], GWLP_WNDPROC, (LONG_PTR)RtEditProc));
    }
    mk_btn(g_rtWnd, L"Собрать", ID_RT_BUILD);
    mk_btn(g_rtWnd, L"Сохранить в Excel…", ID_RT_SAVE);
    mk_btn(g_rtWnd, L"Копировать", ID_RT_COPY);
    mk_btn(g_rtWnd, L"Подробности", ID_RT_LOG);
    mk_btn(g_rtWnd, L"Выгрузка для проверки", ID_RT_DUMP);
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
    rt_prefs_load();
    rt_layout();
  }
  /* выбрана строка в поиске PLM — её обозначение сразу в поле */
  int i = plm_selected_index();
  if (i >= 0 && i < g_plmCount && !g_resultFiles && !g_rtBusy) {
    wchar_t des[200];
    card_des_from_name(g_plmEsi[i], des, 200);
    if (des[0]) {
      SetWindowTextW(g_rtDes, des);
      g_rtPickId = g_plmIds[i];
      lstrcpynW(g_rtPickDes, des, 200);
    }
  }
  ShowWindow(g_rtWnd, IsIconic(g_rtWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_rtWnd);
  SetFocus(g_rtDes);
  SendMessageW(g_rtDes, EM_SETSEL, 0, -1);
}
