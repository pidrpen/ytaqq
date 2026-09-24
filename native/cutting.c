/* ---- Окно «Расчёт резки и газов» --------------------------------------------

   Своё окно CursorPad вместо страницы cutting-calculator.html: открывается
   сразу, без Edge. Расчёт — cutting_calc.c (сверен со страницей: текст
   «Копировать» и CSV совпадают символ в символ на 25 тысячах вариантов).

   Окно поверх всех, с кнопками «свернуть/развернуть», растягивается.
   Крестик окно прячет, а не закрывает: при следующем открытии в нём то же,
   что вводили. Материал, толщина и длина по умолчанию — как на странице:
   ст3, 10 мм, 10 000 мм. */

#include <commdlg.h> /* окно «Сохранить как» для CSV */
#include "cutting_calc.c"

#define ID_CUT_MAT 700
#define ID_CUT_THICK 701
#define ID_CUT_LEN 702
#define ID_CUT_QUICK 710 /* 710–715: 1 000 … 50 000 мм */
#define ID_CUT_COPY 716
#define ID_CUT_CSV 717
#define ID_CUT_OUT 718
#define TIMER_CUT_COPIED 1

static HWND g_cutWnd, g_cutMat, g_cutThick, g_cutLen, g_cutOut, g_cutCopy, g_cutCsv, g_cutQuick[6];
static CutPack g_cutPack;
static BOOL g_cutHave;
static wchar_t g_cutHint[400];
static const int kCutQuick[6] = {1000, 2500, 5000, 10000, 25000, 50000};

/* как parseFloat(value.replace(',', '.')) || 0 */
static double cut_parse(HWND e) {
  wchar_t t[64];
  GetWindowTextW(e, t, 64);
  wchar_t *c = wcschr(t, L',');
  if (c) *c = L'.';
  wchar_t *end = NULL;
  double v = wcstod(t, &end);
  if (end == t || v != v) return 0;
  return v;
}

static const wchar_t *cut_cur_material(void) {
  int i = (int)SendMessageW(g_cutMat, CB_GETCURSEL, 0, 0);
  return (i >= 0 && i < CUT_NMATS) ? kCutMaterials[i] : NULL;
}

static void cut_recalc(void) {
  const wchar_t *mat = cut_cur_material();
  cut_hint(mat ? mat : L"", g_cutHint, 400);
  g_cutHave = cut_calc(mat, cut_parse(g_cutThick), cut_parse(g_cutLen), &g_cutPack);
  wchar_t *buf = (wchar_t *)malloc(40000 * sizeof(wchar_t));
  if (!buf) return;
  if (g_cutHave) cut_view_text(&g_cutPack, buf, 40000);
  else lstrcpynW(buf, L"Укажите толщину и длину реза — расход каждой технологии посчитается сам", 40000);
  SetWindowTextW(g_cutOut, buf);
  free(buf);
  EnableWindow(g_cutCopy, g_cutHave);
  EnableWindow(g_cutCsv, g_cutHave);
  InvalidateRect(g_cutWnd, NULL, TRUE);
}

static void cut_to_clipboard(void) {
  if (!g_cutHave) return;
  wchar_t *t = (wchar_t *)malloc(40000 * sizeof(wchar_t));
  if (!t) return;
  cut_copy_text(&g_cutPack, t, 20000);
  /* в Windows строки делит \r\n — иначе Блокнот склеит всё в одну */
  size_t n = wcslen(t), extra = 0;
  for (size_t i = 0; i < n; i++)
    if (t[i] == L'\n') extra++;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (n + extra + 1) * sizeof(wchar_t));
  if (h) {
    wchar_t *d = (wchar_t *)GlobalLock(h);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
      if (t[i] == L'\n') d[k++] = L'\r';
      d[k++] = t[i];
    }
    d[k] = 0;
    GlobalUnlock(h);
    if (OpenClipboard(g_cutWnd)) {
      EmptyClipboard();
      if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
      CloseClipboard();
      SetWindowTextW(g_cutCopy, L"Скопировано");
      SetTimer(g_cutWnd, TIMER_CUT_COPIED, 1500, NULL);
    } else {
      GlobalFree(h);
    }
  }
  free(t);
}

static void cut_save_csv(void) {
  if (!g_cutHave) return;
  wchar_t file[MAX_PATH], t[40], L[40];
  cut_js_num(g_cutPack.thickness, t, 40);
  cut_js_num(g_cutPack.lengthMm, L, 40);
  _snwprintf(file, MAX_PATH, L"raschet-rezki-%s-%smm-%smm.csv", g_cutPack.material, t, L);
  file[MAX_PATH - 1] = 0;
  for (wchar_t *p = file; *p; p++) /* «АДО, АД1М» и прочее — без знаков, запрещённых в имени */
    if (wcschr(L"\\/:*?\"<>|", *p)) *p = L'_';
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_cutWnd;
  of.lpstrFilter = L"CSV (*.csv)\0*.csv\0Все файлы\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"csv";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  wchar_t *w = (wchar_t *)malloc(60000 * sizeof(wchar_t));
  if (!w) return;
  cut_csv_text(&g_cutPack, w, 60000);
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *u = n > 0 ? (char *)malloc((size_t)n + 3) : NULL;
  if (u) {
    u[0] = (char)0xEF; /* BOM, как у страницы: Excel тогда видит UTF-8 */
    u[1] = (char)0xBB;
    u[2] = (char)0xBF;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u + 3, n, NULL, NULL);
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wr = 0;
    BOOL ok = f != INVALID_HANDLE_VALUE && WriteFile(f, u, (DWORD)(n + 2), &wr, NULL);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) MessageBoxW(g_cutWnd, L"Не удалось записать файл.", L"Расчёт резки", MB_ICONWARNING);
    free(u);
  }
  free(w);
}

static void cut_layout(void) {
  RECT rc;
  GetClientRect(g_cutWnd, &rc);
  int pad = 12, gap = 10, h = 26, y = 30;
  int col = (rc.right - pad * 2 - gap * 2) / 3;
  MoveWindow(g_cutMat, pad, y, col, 300, TRUE);
  MoveWindow(g_cutThick, pad + col + gap, y, col, h, TRUE);
  MoveWindow(g_cutLen, pad + (col + gap) * 2, y, rc.right - pad - (pad + (col + gap) * 2), h, TRUE);
  y += h + 26; /* место под подсказку о толщинах */
  int qw = (rc.right - pad * 2 - 6 * 5) / 6;
  for (int i = 0; i < 6; i++) MoveWindow(g_cutQuick[i], pad + i * (qw + 6), y, qw, h, TRUE);
  y += h + 10;
  int by = rc.bottom - pad - 28;
  MoveWindow(g_cutOut, pad, y, rc.right - pad * 2, by - 8 - y, TRUE);
  MoveWindow(g_cutCopy, pad, by, 130, 28, TRUE);
  MoveWindow(g_cutCsv, pad + 140, by, 150, 28, TRUE);
}

static LRESULT CALLBACK CutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND: {
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect((HDC)wParam, &rc, bg_brush(FALSE));
    return 1;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    SetBkMode(dc, TRANSPARENT);
    if (g_fontUi) SelectObject(dc, g_fontUi);
    SetTextColor(dc, COL_MUTED);
    static const wchar_t *const caps[3] = {L"Материал", L"Толщина, мм", L"Длина реза, мм"};
    HWND ctl[3] = {g_cutMat, g_cutThick, g_cutLen};
    for (int i = 0; i < 3; i++) {
      RECT r;
      GetWindowRect(ctl[i], &r);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&r, 2);
      RECT t = {r.left, r.top - 20, r.right, r.top - 2};
      DrawTextW(dc, caps[i], -1, &t, DT_LEFT | DT_SINGLELINE | DT_BOTTOM);
    }
    if (g_fontSmall) SelectObject(dc, g_fontSmall);
    RECT r;
    GetWindowRect(g_cutMat, &r);
    MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&r, 2);
    RECT cr;
    GetClientRect(hwnd, &cr);
    RECT t = {r.left, r.top + 28, cr.right - 12, r.top + 48};
    DrawTextW(dc, g_cutHint, -1, &t, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORLISTBOX: {
    HDC dc = (HDC)wParam;
    SetTextColor(dc, COL_INK);
    SetBkColor(dc, COL_PAPER);
    return (LRESULT)g_paper;
  }
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = 560;
    mm->ptMinTrackSize.y = 380;
    return 0;
  }
  case WM_SIZE:
    if (g_cutOut) cut_layout();
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_TIMER:
    if (wParam == TIMER_CUT_COPIED) {
      KillTimer(hwnd, TIMER_CUT_COPIED);
      SetWindowTextW(g_cutCopy, L"Копировать");
    }
    return 0;
  case WM_COMMAND: {
    int id = LOWORD(wParam), code = HIWORD(wParam);
    if ((id == ID_CUT_THICK || id == ID_CUT_LEN) && code == EN_CHANGE) cut_recalc();
    /* зашли в поле — число выделено целиком: новое набирается поверх, а не дописывается */
    if ((id == ID_CUT_THICK || id == ID_CUT_LEN) && code == EN_SETFOCUS)
      PostMessageW((HWND)lParam, EM_SETSEL, 0, -1);
    if (id == ID_CUT_MAT && code == CBN_SELCHANGE) cut_recalc();
    if (id >= ID_CUT_QUICK && id < ID_CUT_QUICK + 6) {
      wchar_t v[16];
      _snwprintf(v, 16, L"%d", kCutQuick[id - ID_CUT_QUICK]);
      SetWindowTextW(g_cutLen, v); /* EN_CHANGE пересчитает */
    }
    if (id == ID_CUT_COPY) cut_to_clipboard();
    if (id == ID_CUT_CSV) cut_save_csv();
    return 0;
  }
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE); /* прячем: в следующий раз — с тем же, что вводили */
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void cutting_show(void) {
  if (!g_cutWnd) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CutProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CursorPadCutting";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int ww = 780, wh = 680;
    if (wh > wa.bottom - wa.top) wh = wa.bottom - wa.top;
    /* поверх всех окон, со «свернуть» и «развернуть» */
    g_cutWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"CursorPadCutting", L"Расчёт резки и газов — CursorPad",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wa.left + (wa.right - wa.left - ww) / 2,
                               wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, NULL, NULL, g_inst, NULL);
    if (!g_cutWnd) return;
    g_cutMat = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                               0, 0, 100, 300, g_cutWnd, (HMENU)(INT_PTR)ID_CUT_MAT, g_inst, NULL);
    g_cutThick = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 0, 0, 100, 26, g_cutWnd, (HMENU)(INT_PTR)ID_CUT_THICK, g_inst, NULL);
    g_cutLen = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10000", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               0, 0, 100, 26, g_cutWnd, (HMENU)(INT_PTR)ID_CUT_LEN, g_inst, NULL);
    for (int i = 0; i < 6; i++) {
      wchar_t t[24];
      wchar_t n[16];
      cut_fmt(kCutQuick[i], 0, n, 16);
      _snwprintf(t, 24, L"%s мм", n);
      g_cutQuick[i] = mk_btn(g_cutWnd, t, ID_CUT_QUICK + i);
    }
    g_cutOut = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0,
                               100, 100, g_cutWnd, (HMENU)(INT_PTR)ID_CUT_OUT, g_inst, NULL);
    g_cutCopy = mk_btn(g_cutWnd, L"Копировать", ID_CUT_COPY);
    g_cutCsv = mk_btn(g_cutWnd, L"Сохранить CSV…", ID_CUT_CSV);
    HFONT f = g_fontBody ? g_fontBody : g_fontUi;
    HWND all[4] = {g_cutMat, g_cutThick, g_cutLen, g_cutOut};
    for (int i = 0; i < 4; i++)
      if (f) SendMessageW(all[i], WM_SETFONT, (WPARAM)f, TRUE);
    for (int i = 0; i < CUT_NMATS; i++) SendMessageW(g_cutMat, CB_ADDSTRING, 0, (LPARAM)kCutMaterials[i]);
    int def = 0;
    for (int i = 0; i < CUT_NMATS; i++)
      if (!wcscmp(kCutMaterials[i], L"ст3")) def = i;
    SendMessageW(g_cutMat, CB_SETCURSEL, (WPARAM)def, 0);
    cut_layout();
    cut_recalc();
  }
  ShowWindow(g_cutWnd, IsIconic(g_cutWnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g_cutWnd);
  SetFocus(g_cutThick);
}
