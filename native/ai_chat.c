/* ai_chat.c — окно «Спросить ИИ» (пункт «Ещё»): чат с локальной моделью (ai.h).
   Беседа помнит реплики этого окна: модели уходят последние, что влезают в
   окно контекста (вкладка «ИИ»). Enter — отправить, Shift+Enter — новая строка.
   Ответ приходит сообщением WM_AI_CHAT с AiResult*. Беседа живёт в памяти окна. */

#define WM_AI_CHAT (WM_APP + 72) /* lParam — AiResult*, освободить ai_result_free */
#define WM_AC_SEND (WM_APP + 73) /* из поля ввода: отправить */
#define ID_AC_LOG 4701
#define ID_AC_IN 4702
#define ID_AC_SEND 4703
#define ID_AC_STOP 4704
#define ID_AC_CLEAR 4705
#define ID_AC_COPY 4706
#define ID_AC_SET 4707
#define ID_AC_STAT 4708
#define ID_AC_TITLE 4709
#define AC_SYS L"Ты помощник в программе CursorPad. Отвечай по-русски, по делу и кратко, если не просят иначе."

typedef struct {
  int assistant;
  wchar_t *text;
} AcTurn;
static HWND g_acWnd, g_acLog, g_acIn, g_acSend, g_acStop, g_acClear, g_acCopy, g_acSet, g_acStat, g_acTitle;
static WNDPROC g_acEditOld;
static AcTurn *g_acTurns;
static int g_acN, g_acCap;
static int g_acId; /* запрос в пути, 0 — нет */

static void ac_push(int assistant, const wchar_t *text) {
  if (g_acN == g_acCap) {
    int cap = g_acCap ? g_acCap * 2 : 16;
    AcTurn *t = (AcTurn *)realloc(g_acTurns, cap * sizeof(AcTurn));
    if (!t) return;
    g_acTurns = t;
    g_acCap = cap;
  }
  wchar_t *c = (wchar_t *)malloc((wcslen(text) + 1) * sizeof(wchar_t));
  if (!c) return;
  wcscpy(c, text);
  g_acTurns[g_acN].assistant = assistant;
  g_acTurns[g_acN].text = c;
  g_acN++;
}

/* снять последнюю реплику и вернуть её текст в поле ввода (вопрос не дошёл) */
static void ac_unsend(void) {
  if (!g_acN) return;
  wchar_t *q = g_acTurns[g_acN - 1].text;
  SetWindowTextW(g_acIn, q);
  g_acN--;
  free(q);
}

static void ac_clear_turns(void) {
  for (int i = 0; i < g_acN; i++) free(g_acTurns[i].text);
  g_acN = 0;
}

static void ac_redraw(void) {
  size_t cap = 64;
  for (int i = 0; i < g_acN; i++) cap += wcslen(g_acTurns[i].text) + 32;
  wchar_t *s = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!s) return;
  s[0] = 0;
  for (int i = 0; i < g_acN; i++) {
    wcscat(s, g_acTurns[i].assistant ? L"ИИ:\r\n" : L"Вы:\r\n");
    wcscat(s, g_acTurns[i].text);
    wcscat(s, L"\r\n\r\n");
  }
  SetWindowTextW(g_acLog, s);
  int len = GetWindowTextLengthW(g_acLog);
  SendMessageW(g_acLog, EM_SETSEL, len, len);
  SendMessageW(g_acLog, EM_SCROLLCARET, 0, 0);
  free(s);
}

static void ac_status(const wchar_t *t) { SetWindowTextW(g_acStat, t ? t : L""); }

static void ac_busy(BOOL on) {
  EnableWindow(g_acSend, !on);
  EnableWindow(g_acStop, on);
}

/* «ИИ выключен» / «нет модели» — ведём на вкладку «ИИ» Настроек */
static void ac_open_settings(void) {
  select_tab(SEC_AI);
  if (g_setHwnd && !IsWindowVisible(g_setHwnd)) toggle_settings();
}

static void ac_clip(const wchar_t *s) {
  if (!OpenClipboard(g_acWnd)) return;
  EmptyClipboard();
  size_t bytes = (wcslen(s) + 1) * sizeof(wchar_t);
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (g) {
    memcpy(GlobalLock(g), s, bytes);
    GlobalUnlock(g);
    if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
  }
  CloseClipboard();
}

static void ac_send(void) {
  if (g_acId) return;
  int len = GetWindowTextLengthW(g_acIn);
  wchar_t *in = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
  if (!in) return;
  GetWindowTextW(g_acIn, in, len + 1);
  wchar_t *a = in;
  while (*a == L' ' || *a == L'\r' || *a == L'\n' || *a == L'\t') a++;
  size_t n = wcslen(a);
  while (n && (a[n - 1] == L' ' || a[n - 1] == L'\r' || a[n - 1] == L'\n' || a[n - 1] == L'\t')) a[--n] = 0;
  if (!n) {
    free(in);
    return;
  }
  if (!ai_ready()) {
    ac_status(ai_status_text(g_aiOn ? AI_NO_MODEL : AI_OFF));
    ac_open_settings();
    free(in);
    return;
  }
  if ((int)n > ai_chat_budget()) {
    ac_status(L"Сообщение длиннее окна контекста. Сократите его или увеличьте контекст на вкладке «ИИ».");
    free(in);
    return;
  }
  ac_push(0, a);
  AiTurn *t = (AiTurn *)malloc(g_acN * sizeof(AiTurn));
  int id = 0;
  if (t) {
    for (int i = 0; i < g_acN; i++) {
      t[i].assistant = g_acTurns[i].assistant;
      t[i].text = g_acTurns[i].text;
    }
    id = ai_ask_chat_async(AC_SYS, t, g_acN, 180000, g_acWnd, WM_AI_CHAT, NULL);
    free(t);
  }
  if (!id) {
    ac_unsend();
    ac_status(L"Не вышло спросить модель, попробуйте ещё раз");
    free(in);
    return;
  }
  g_acId = id;
  SetWindowTextW(g_acIn, L"");
  ac_redraw();
  ac_busy(TRUE);
  ac_status(L"Думаю… первый ответ после запуска модели может грузиться до минуты");
  free(in);
}

static void ac_result(AiResult *r) {
  if (!r) return;
  if (r->id != g_acId) {
    ai_result_free(r);
    return;
  }
  g_acId = 0;
  ac_busy(FALSE);
  if (r->status == AI_OK) {
    ac_push(1, r->text);
    ac_redraw();
    ac_status(L"");
  } else {
    ac_unsend();
    ac_redraw();
    ac_status(r->status == AI_CANCELLED ? L"Остановлено, вопрос вернулся в поле ввода"
                                        : (r->text && r->text[0] ? r->text : ai_status_text(r->status)));
  }
  ai_result_free(r);
}

static void ac_clear(void) {
  if (g_acId) ai_cancel(g_acId);
  g_acId = 0;
  ac_busy(FALSE);
  ac_clear_turns();
  SetWindowTextW(g_acLog, L"");
  ac_status(L"");
}

static void ac_copy_answer(void) {
  for (int i = g_acN - 1; i >= 0; i--)
    if (g_acTurns[i].assistant) {
      ac_clip(g_acTurns[i].text);
      ac_status(L"Ответ скопирован");
      return;
    }
  ac_status(L"Ответа ещё нет");
}

/* поле ввода: Enter — отправить, Shift+Enter — новая строка; Ctrl+A, Esc */
static LRESULT CALLBACK AcEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (msg == WM_KEYDOWN && wParam == VK_RETURN && !shift) {
    if (hwnd == g_acIn) PostMessageW(g_acWnd, WM_AC_SEND, 0, 0);
    return 0;
  }
  if (msg == WM_CHAR && wParam == L'\r' && !shift) return 0;
  if (msg == WM_CHAR && wParam == 1) {
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    ShowWindow(g_acWnd, SW_HIDE);
    return 0;
  }
  return CallWindowProcW(g_acEditOld, hwnd, msg, wParam, lParam);
}

static void ac_layout(int W, int H) {
  int pad = 12, gap = 8, btnH = 30, inH = 72, statH = 20;
  int btnY = H - pad - btnH;
  int inY = btnY - gap - inH;
  int stY = inY - 4 - statH;
  int logY = 46;
  int logH = stY - 6 - logY;
  if (logH < 40) logH = 40;
  MoveWindow(g_acTitle, pad, 10, W - 2 * pad - 70, 24, TRUE);
  MoveWindow(GetDlgItem(g_acWnd, ID_PANEL_MIN), W - pad - 66, 8, 30, 26, TRUE);
  MoveWindow(GetDlgItem(g_acWnd, ID_PANEL_CLOSE), W - pad - 30, 8, 30, 26, TRUE);
  MoveWindow(g_acLog, pad, logY, W - 2 * pad, logH, TRUE);
  MoveWindow(g_acStat, pad, stY, W - 2 * pad, statH, TRUE);
  MoveWindow(g_acIn, pad, inY, W - 2 * pad, inH, TRUE);
  HWND row[5] = {g_acSend, g_acStop, g_acClear, g_acCopy, g_acSet};
  int bw = (W - 2 * pad - 4 * gap) / 5;
  for (int i = 0; i < 5; i++) MoveWindow(row[i], pad + i * (bw + gap), btnY, bw, btnH, TRUE);
}

static LRESULT CALLBACK AcProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_SIZE:
    ac_layout(LOWORD(lParam), HIWORD(lParam));
    return 0;
  case WM_COMMAND:
    if (HIWORD(wParam) != BN_CLICKED) break;
    switch (LOWORD(wParam)) {
    case ID_PANEL_CLOSE:
      ShowWindow(hwnd, SW_HIDE);
      break;
    case ID_PANEL_MIN:
      ShowWindow(hwnd, SW_MINIMIZE);
      break;
    case ID_AC_SEND:
      ac_send();
      break;
    case ID_AC_STOP:
      if (g_acId) ai_cancel(g_acId);
      break;
    case ID_AC_CLEAR:
      ac_clear();
      break;
    case ID_AC_COPY:
      ac_copy_answer();
      break;
    case ID_AC_SET:
      ac_open_settings();
      break;
    }
    return 0;
  case WM_AC_SEND:
    ac_send();
    return 0;
  case WM_AI_CHAT:
    ac_result((AiResult *)lParam);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ac_create(void) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = AcProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = L"CursorPadAiChat";
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  RegisterClassExW(&wc);
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int w = 600, h = 640;
  if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
  if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
  g_acWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadAiChat", L"Спросить ИИ — CursorPad",
                            WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                            wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                            NULL, NULL, g_inst, NULL);
  if (!g_acWnd) return;
  round_corners(g_acWnd);
  mk_btn(g_acWnd, L"×", ID_PANEL_CLOSE);
  mk_btn(g_acWnd, L"–", ID_PANEL_MIN);
  g_acTitle = CreateWindowExW(0, L"STATIC", L"Спросить ИИ · всё на этом компьютере, без интернета", WS_CHILD | WS_VISIBLE | SS_LEFT,
                              0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_TITLE, g_inst, NULL);
  g_acLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                            0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_LOG, g_inst, NULL);
  g_acIn = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                           0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_IN, g_inst, NULL);
  g_acStat = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, g_acWnd,
                             (HMENU)(INT_PTR)ID_AC_STAT, g_inst, NULL);
  g_acSend = mk_btn(g_acWnd, L"Отправить", ID_AC_SEND);
  g_acStop = mk_btn(g_acWnd, L"Стоп", ID_AC_STOP);
  g_acClear = mk_btn(g_acWnd, L"Очистить", ID_AC_CLEAR);
  g_acCopy = mk_btn(g_acWnd, L"Копировать", ID_AC_COPY);
  g_acSet = mk_btn(g_acWnd, L"Настройки", ID_AC_SET);
  for (HWND c = GetWindow(g_acWnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    if (c != g_acSend && c != g_acStop && c != g_acClear && c != g_acCopy && c != g_acSet) SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_acLog, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  SendMessageW(g_acIn, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  g_acEditOld = (WNDPROC)SetWindowLongPtrW(g_acIn, GWLP_WNDPROC, (LONG_PTR)AcEditProc);
  SetWindowLongPtrW(g_acLog, GWLP_WNDPROC, (LONG_PTR)AcEditProc);
  RECT rc;
  GetClientRect(g_acWnd, &rc);
  ac_layout(rc.right, rc.bottom);
}

/* пункт «Спросить ИИ» в «Ещё» */
static void aichat_show(void) {
  if (!g_acWnd) ac_create();
  if (!g_acWnd) return;
  ShowWindow(g_acWnd, IsIconic(g_acWnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g_acWnd);
  SetFocus(g_acIn);
  if (!g_acId) {
    if (!ai_ready()) ac_status(ai_status_text(g_aiOn ? AI_NO_MODEL : AI_OFF));
    else ac_status(L"Enter — отправить, Shift+Enter — новая строка. Модель помнит эту беседу.");
  }
}
