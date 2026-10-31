/* ai_memory.c — окно «Что помнит ИИ» (Ещё → ИИ). Две части:
   1) тетрадь ai_learn.txt — записи, которые ИИ сохранил по «запомни: …» и
      кнопке «Запомнить»: список, правка выбранной записи, «Стереть запись»;
   2) общая записная книжка ai_notebook.txt — её пишет человек, а ИИ читает её
      в каждом ответе (ac_sys_build, до 1000 знаков).
   Файлы лежат в %LOCALAPPDATA%\CursorPad, блоки тетради — через строку ---,
   как и в ai_chat.c. Правка сохраняется кнопкой; если уйти к другой записи или
   закрыть окно с несохранённой правкой, она сохранится сама. */

#define ID_MM_LIST 4720
#define ID_MM_TEXT 4721
#define ID_MM_SAVE 4722      /* главная кнопка: в draw_pad_button (cursorpad.c) */
#define ID_MM_DEL 4723
#define ID_MM_NOTE 4724
#define ID_MM_NOTE_SAVE 4725 /* главная кнопка: в draw_pad_button (cursorpad.c) */
#define MEMO_RECS_MAX 256           /* записей в тетради; больше не читаем */
#define MEMO_REC_CAP 2000      /* как «запомни» в ai_chat.c: длиннее не пишем */
#define MEMO_NOTE_CAP 1000     /* книжку ИИ читает целиком */
#define MEMO_PX(v) MulDiv((v), g_mmDpi, 96)

typedef struct {
  RECT intro, recLbl, list, text, save, del, noteLbl, note, noteSave, msg;
} MmGeom;

static HWND g_mmWnd, g_mmList, g_mmText, g_mmSave, g_mmDel, g_mmNote, g_mmNoteSave;
static wchar_t *g_mmRec[MEMO_RECS_MAX];
static int g_mmN, g_mmSel = -1, g_mmDpi = 96;
static wchar_t g_mmMsg[240];
static BOOL g_mmBad; /* строка внизу — про ошибку */

static void mm_msg(const wchar_t *s, BOOL bad) {
  lstrcpynW(g_mmMsg, s, 240);
  g_mmBad = bad;
  if (g_mmWnd) InvalidateRect(g_mmWnd, NULL, FALSE);
}

static BOOL mm_ws(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

/* копия [a, b) без пробелов и переносов по краям; пусто — NULL */
static wchar_t *mm_dup(const wchar_t *a, const wchar_t *b) {
  while (a < b && mm_ws(*a)) a++;
  while (b > a && mm_ws(b[-1])) b--;
  if (a == b) return NULL;
  wchar_t *s = (wchar_t *)malloc((size_t)(b - a + 1) * sizeof(wchar_t));
  if (!s) return NULL;
  memcpy(s, a, (size_t)(b - a) * sizeof(wchar_t));
  s[b - a] = 0;
  return s;
}

static wchar_t *mm_text_of(HWND edit) {
  int n = GetWindowTextLengthW(edit);
  wchar_t *t = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
  if (t) {
    t[0] = 0;
    GetWindowTextW(edit, t, n + 1);
  }
  return t;
}

static void mm_free(void) {
  for (int i = 0; i < g_mmN; i++) free(g_mmRec[i]);
  g_mmN = 0;
}

static void mm_add(const wchar_t *a, const wchar_t *b) {
  wchar_t *s = mm_dup(a, b);
  if (!s) return;
  if (g_mmN < MEMO_RECS_MAX) g_mmRec[g_mmN++] = s;
  else free(s);
}

/* тетрадь на блоки: строка «---» делит записи, пустые блоки не берём */
static void mm_split(const wchar_t *all) {
  const wchar_t *blk = all, *p = all;
  while (*p) {
    const wchar_t *eol = wcschr(p, L'\n');
    const wchar_t *next = eol ? eol + 1 : p + wcslen(p);
    size_t len = (size_t)((eol ? eol : next) - p);
    if (len && p[len - 1] == L'\r') len--;
    if (len == 3 && wcsncmp(p, L"---", 3) == 0) {
      mm_add(blk, p);
      blk = next;
    }
    p = next;
  }
  mm_add(blk, p);
}

/* первая строка записи для списка, с многоточием, если записи не хватило */
static void mm_label(const wchar_t *rec, wchar_t *out, int n) {
  size_t k = wcscspn(rec, L"\r\n");
  if (k > (size_t)n - 4) k = (size_t)n - 4;
  wcsncpy(out, rec, k);
  out[k] = 0;
  if (k < wcslen(rec)) wcscat(out, L"…");
}

static void mm_show_rec(void) {
  SetWindowTextW(g_mmText, g_mmSel >= 0 ? g_mmRec[g_mmSel] : L"");
  SendMessageW(g_mmText, EM_SETMODIFY, FALSE, 0);
  EnableWindow(g_mmSave, g_mmSel >= 0);
  EnableWindow(g_mmDel, g_mmSel >= 0);
  if (g_mmWnd) InvalidateRect(g_mmWnd, NULL, FALSE);
}

/* список заново; выбрана запись sel (или ближайшая к ней) */
static void mm_fill(int sel) {
  SendMessageW(g_mmList, LB_RESETCONTENT, 0, 0);
  for (int i = 0; i < g_mmN; i++) {
    wchar_t lab[80];
    mm_label(g_mmRec[i], lab, 80);
    SendMessageW(g_mmList, LB_ADDSTRING, 0, (LPARAM)lab);
  }
  if (sel >= g_mmN) sel = g_mmN - 1;
  g_mmSel = sel;
  if (sel >= 0) SendMessageW(g_mmList, LB_SETCURSEL, sel, 0);
}

/* тетрадь целиком: блоки через «\r\n---\r\n», как пишет «запомни» */
static BOOL mm_write(void) {
  wchar_t path[MAX_PATH];
  ac_learn_path(path);
  if (!path[0]) return FALSE;
  if (!g_mmN) return DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND;
  size_t total = 1;
  for (int i = 0; i < g_mmN; i++) total += wcslen(g_mmRec[i]) + 8;
  wchar_t *buf = (wchar_t *)malloc(total * sizeof(wchar_t));
  if (!buf) return FALSE;
  buf[0] = 0;
  for (int i = 0; i < g_mmN; i++) {
    if (i) wcscat(buf, L"\r\n---\r\n");
    wcscat(buf, g_mmRec[i]);
  }
  BOOL ok = ac_write_utf8(path, buf);
  free(buf);
  return ok;
}

/* 1 — записано, 0 — пусто (пустую запись не пишем), -1 — файл не записался */
static int mm_set(int i, const wchar_t *text) {
  wchar_t *s = mm_dup(text, text + wcslen(text));
  if (!s) return 0;
  wchar_t *old = g_mmRec[i];
  g_mmRec[i] = s;
  if (mm_write()) {
    free(old);
    return 1;
  }
  g_mmRec[i] = old;
  free(s);
  return -1;
}

/* правка, которую не нажали «Сохранить», пишется сама: при уходе к другой записи или закрытии */
static void mm_flush(void) {
  if (g_mmSel < 0 || !SendMessageW(g_mmText, EM_GETMODIFY, 0, 0)) return;
  wchar_t *t = mm_text_of(g_mmText);
  if (!t) return;
  int r = mm_set(g_mmSel, t);
  free(t);
  if (r == 1) {
    mm_fill(g_mmSel);
    SendMessageW(g_mmText, EM_SETMODIFY, FALSE, 0);
    mm_msg(L"Правка сохранена.", FALSE);
  } else if (r < 0) {
    mm_msg(L"Не записал правку: нет папки данных или файл занят.", TRUE);
  }
}

/* тетрадь с диска; sel — какую запись выбрать */
static void mm_reload_recs(int sel) {
  mm_free();
  wchar_t path[MAX_PATH];
  ac_learn_path(path);
  wchar_t *all = path[0] ? ac_read_utf8(path, 48 * 1024) : NULL;
  if (all) {
    mm_split(all);
    free(all);
  }
  mm_fill(sel);
  mm_show_rec();
}

static void mm_load_note(void) {
  wchar_t *t = ac_notebook_read();
  SetWindowTextW(g_mmNote, t ? t : L"");
  SendMessageW(g_mmNote, EM_SETMODIFY, FALSE, 0);
  free(t);
}

static void mm_save_note(void) {
  wchar_t path[MAX_PATH];
  ac_notebook_path(path);
  wchar_t *t = mm_text_of(g_mmNote);
  if (!path[0] || !t) {
    free(t);
    mm_msg(L"Не сохранил книжку: нет папки данных.", TRUE);
    return;
  }
  BOOL ok = t[0] ? ac_write_utf8(path, t) : (DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND);
  free(t);
  if (!ok) {
    mm_msg(L"Не сохранил книжку: файл занят или нет места.", TRUE);
    return;
  }
  SendMessageW(g_mmNote, EM_SETMODIFY, FALSE, 0);
  mm_msg(L"Книжка сохранена. ИИ прочитает её в следующем ответе.", FALSE);
}

/* несохранённое — в файлы: и правку записи, и книжку */
static void mm_park(void) {
  mm_flush();
  if (SendMessageW(g_mmNote, EM_GETMODIFY, 0, 0)) mm_save_note();
}

static void mm_select(int s) {
  g_mmSel = s;
  if (s >= 0) SendMessageW(g_mmList, LB_SETCURSEL, s, 0);
  mm_show_rec();
}

static void mm_pick(void) {
  int s = (int)SendMessageW(g_mmList, LB_GETCURSEL, 0, 0);
  if (s == g_mmSel) return;
  mm_flush();
  mm_select(s);
}

static void mm_save_rec(void) {
  if (g_mmSel < 0) return;
  wchar_t *t = mm_text_of(g_mmText);
  if (!t) return;
  int r = mm_set(g_mmSel, t);
  free(t);
  if (r == 1) {
    mm_fill(g_mmSel);
    SendMessageW(g_mmText, EM_SETMODIFY, FALSE, 0);
    mm_msg(L"Запись сохранена. Дальше ИИ отвечает с учётом правки.", FALSE);
  } else if (r == 0) {
    mm_msg(L"Пустую запись не сохраняю. Чтобы убрать её, нажмите «Стереть запись».", TRUE);
  } else {
    mm_msg(L"Не записал: нет папки данных или файл занят.", TRUE);
  }
}

static void mm_delete(void) {
  if (g_mmSel < 0) return;
  if (MessageBoxW(g_mmWnd, L"Стереть эту запись? ИИ её забудет.", L"Что помнит ИИ",
                  MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    return;
  free(g_mmRec[g_mmSel]);
  memmove(&g_mmRec[g_mmSel], &g_mmRec[g_mmSel + 1], (size_t)(g_mmN - g_mmSel - 1) * sizeof(g_mmRec[0]));
  g_mmN--;
  BOOL ok = mm_write();
  int next = g_mmSel < g_mmN ? g_mmSel : g_mmN - 1;
  mm_fill(next);
  mm_show_rec();
  mm_msg(ok ? L"Запись стёрта. ИИ её больше не помнит." : L"Запись стёрта из окна, но файл не записался.", !ok);
}

/* «запомни» в чате, пока окно открыто: показать свежую тетрадь */
static void mm_refresh(void) {
  if (!g_mmWnd || !IsWindowVisible(g_mmWnd)) return;
  mm_flush();
  mm_reload_recs(g_mmSel);
}

/* ---- окно ---------------------------------------------------------------- */

static void mm_geom(int W, MmGeom *g) {
  int pad = MEMO_PX(16), gap = MEMO_PX(8), bh = MEMO_PX(28), lh = MEMO_PX(20);
  int x1 = W - pad, y = PANEL_TITLE_H + gap;
  SetRect(&g->intro, pad, y, x1, y + MEMO_PX(72));
  y = g->intro.bottom + gap;
  SetRect(&g->recLbl, pad, y, x1, y + lh);
  y = g->recLbl.bottom + MEMO_PX(4);
  SetRect(&g->list, pad, y, x1, y + MEMO_PX(132));
  y = g->list.bottom + gap;
  SetRect(&g->text, pad, y, x1, y + MEMO_PX(84));
  y = g->text.bottom + gap;
  SetRect(&g->save, pad, y, pad + MEMO_PX(150), y + bh);
  SetRect(&g->del, g->save.right + gap, y, g->save.right + gap + MEMO_PX(130), y + bh);
  y = g->save.bottom + MEMO_PX(16);
  SetRect(&g->noteLbl, pad, y, x1, y + lh);
  y = g->noteLbl.bottom + MEMO_PX(4);
  SetRect(&g->note, pad, y, x1, y + MEMO_PX(92));
  y = g->note.bottom + gap;
  SetRect(&g->noteSave, pad, y, pad + MEMO_PX(170), y + bh);
  y = g->noteSave.bottom + MEMO_PX(10);
  SetRect(&g->msg, pad, y, x1, y + lh);
}

static void mm_place(HWND c, RECT r) { MoveWindow(c, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE); }

static void mm_layout(void) {
  if (!g_mmWnd || !g_mmList) return;
  RECT rc;
  GetClientRect(g_mmWnd, &rc);
  place_panel_close(g_mmWnd);
  MmGeom g;
  mm_geom(rc.right, &g);
  mm_place(g_mmList, g.list);
  mm_place(g_mmText, g.text);
  mm_place(g_mmSave, g.save);
  mm_place(g_mmDel, g.del);
  mm_place(g_mmNote, g.note);
  mm_place(g_mmNoteSave, g.noteSave);
  InvalidateRect(g_mmWnd, NULL, TRUE);
}

static void mm_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)bg_brush(TRUE));
  FillRect(hdc, &rc, bg_brush(TRUE));
  draw_panel_header(hwnd, hdc, L"Что помнит ИИ");
  MmGeom g;
  mm_geom(rc.right, &g);
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontBody) SelectObject(hdc, g_fontBody);
  SetTextColor(hdc, COL_INK);
  DrawTextW(hdc,
            L"Здесь всё, что ИИ запомнил. Записи — «запомни: …» и кнопка «Запомнить»: выберите запись, "
            L"поправьте её или сотрите. Беседа с ИИ тоже сохраняется между запусками; стереть её можно "
            L"кнопкой «Очистить» в «Спросить ИИ».",
            -1, &g.intro, DT_LEFT | DT_WORDBREAK);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  wchar_t lab[160];
  if (g_mmN) _snwprintf(lab, 160, L"ЧТО ИИ ЗАПОМНИЛ · записей: %d", g_mmN);
  else lstrcpynW(lab, L"ЧТО ИИ ЗАПОМНИЛ · пока пусто: в «Спросить ИИ» напишите «запомни: …»", 160);
  DrawTextW(hdc, lab, -1, &g.recLbl, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  DrawTextW(hdc, L"ОБЩАЯ ЗАПИСНАЯ КНИЖКА · ИИ читает её в каждом ответе, до 1000 знаков", -1, &g.noteLbl,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  SetTextColor(hdc, g_mmBad ? RGB(0xB4, 0x23, 0x18) : COL_SAGE);
  DrawTextW(hdc, g_mmMsg, -1, &g.msg, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static HWND mm_child(const wchar_t *cls, DWORD style, int id) {
  HWND c = CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 10, 10, g_mmWnd,
                           (HMENU)(INT_PTR)id, g_inst, NULL);
  if (c) SendMessageW(c, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  return c;
}

static LRESULT CALLBACK MemoryProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    mm_paint(hwnd, hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_SIZE:
    mm_layout();
    return 0;
  case WM_COMMAND: {
    int id = LOWORD(wParam), code = HIWORD(wParam);
    if (id == ID_PANEL_CLOSE && code == BN_CLICKED) SendMessageW(hwnd, WM_CLOSE, 0, 0);
    else if (id == ID_MM_LIST && code == LBN_SELCHANGE) mm_pick();
    else if (id == ID_MM_SAVE && code == BN_CLICKED) mm_save_rec();
    else if (id == ID_MM_DEL && code == BN_CLICKED) mm_delete();
    else if (id == ID_MM_NOTE_SAVE && code == BN_CLICKED) mm_save_note();
    return 0;
  }
  case WM_CLOSE:
    mm_park();
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void mm_show(void) {
  if (!g_mmWnd) {
    g_mmDpi = ui_dpi(NULL);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MemoryProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = bg_brush(TRUE);
    wc.lpszClassName = L"CursorPadMemory";
    wc.hIcon = app_icon(FALSE);
    wc.hIconSm = app_icon(TRUE);
    RegisterClassExW(&wc);
    MmGeom g;
    mm_geom(MEMO_PX(620), &g);
    int w = MEMO_PX(620), h = g.msg.bottom + MEMO_PX(16);
    g_mmWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadMemory", L"Что помнит ИИ",
                              WS_POPUP | WS_CLIPCHILDREN, 0, 0, w, h, NULL, NULL, g_inst, NULL);
    if (!g_mmWnd) return;
    round_corners(g_mmWnd);
    mk_btn(g_mmWnd, L"×", ID_PANEL_CLOSE);
    g_mmList = mm_child(L"LISTBOX", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, ID_MM_LIST);
    g_mmText = mm_child(L"EDIT", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, ID_MM_TEXT);
    g_mmNote = mm_child(L"EDIT", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, ID_MM_NOTE);
    g_mmSave = mk_btn(g_mmWnd, L"Сохранить запись", ID_MM_SAVE);
    g_mmDel = mk_btn(g_mmWnd, L"Стереть запись", ID_MM_DEL);
    g_mmNoteSave = mk_btn(g_mmWnd, L"Сохранить книжку", ID_MM_NOTE_SAVE);
    SendMessageW(g_mmText, EM_SETLIMITTEXT, MEMO_REC_CAP, 0);
    SendMessageW(g_mmNote, EM_SETLIMITTEXT, MEMO_NOTE_CAP, 0);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_mmWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                 SWP_NOZORDER);
  }
  mm_park(); /* открыто уже — правки не потерять, даже если окно звали снова */
  mm_reload_recs(0);
  mm_load_note();
  mm_msg(L"", FALSE);
  mm_layout();
  ShowWindow(g_mmWnd, SW_SHOW);
  SetWindowPos(g_mmWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  SetForegroundWindow(g_mmWnd);
}
