/* ai_minichat.c — мини-чат у помощника (с 2026.10.21.3). Подключается из
   ai_chat.c после окна чата: беседа та же (g_acTurns, ac_send), ответы приходят
   в окно чата, даже скрытое, — полный чат (⤢) покажет её целиком.

   Пока помощник на столе, «Спросить ИИ» открывает маленькое тёмное окно у его
   головы: последние реплики пузырями, поле ввода и кнопка «отправить» (пока ИИ
   думает — «стоп»). План файловой задачи — «Применить» / «Отмена» прямо здесь.
   × или Esc — закрыть. Окно ездит за роботом; за шапку его тянут — робот идёт
   следом. */

#define WM_AM_SEND (WM_APP + 76) /* Enter в поле ввода */
#define ID_AM_IN 4801
#define AM_TIMER 0xA181          /* точки «думает…» */
enum { AM_NONE, AM_FULL, AM_CLOSE, AM_SEND, AM_APPLY, AM_CANCEL, AM_BTNS };
enum { AM_USER, AM_AI, AM_DOTS, AM_HINT };

#define AM_BG RGB(17, 21, 38)
#define AM_FIELD RGB(26, 32, 56)
#define AM_INK RGB(232, 236, 255)
#define AM_MUTED RGB(140, 150, 190)
#define AM_CYAN RGB(56, 225, 255)
#define AM_VIOLET RGB(123, 92, 255)

typedef struct {
  int kind;
  RECT r; /* в координатах ленты: от её верха */
  wchar_t text[720];
} AmItem;

static HWND g_amWnd, g_amIn;
static WNDPROC g_amInOld;
static HFONT g_amFont, g_amBold, g_amSmall;
static HBRUSH g_amBgBr, g_amFieldBr;
static int g_amK = 100, g_amScroll, g_amHot, g_amContentH, g_amCount;
static BOOL g_amTrack, g_amPlacing, g_amLeft;
static POINT g_amGap; /* робот относительно угла окна у его стороны */
static RECT g_amMsgs, g_amField, g_amBtn[AM_BTNS];
static AmItem g_amItems[9];
static wchar_t g_amStatus[200];

static void aichat_show_full(void);
static void am_layout(void);

static int am_px(int v) { return v * g_amK / 100; }
static BOOL am_busy(void) { return g_acId || g_agActive || g_acWebOn; }

static BOOL ac_mini_open(void) { return g_amWnd && IsWindowVisible(g_amWnd); }
static void ac_mini_close(void) {
  if (g_amWnd) ShowWindow(g_amWnd, SW_HIDE);
}

/* реплика для мини-чата: без разметки Markdown и повторов пробелов, до ~700 знаков */
static void am_clean(const wchar_t *s, wchar_t *out, int cap) {
  int n = 0;
  BOOL space = FALSE;
  for (; *s && n < cap - 2; s++) {
    wchar_t c = *s;
    if (c == L'*' || c == L'`' || c == L'#' || c == L'\r') continue;
    if (c == L'\t') c = L' ';
    if (c == L' ' && space) continue;
    space = c == L' ';
    out[n++] = c;
  }
  while (n && (out[n - 1] == L' ' || out[n - 1] == L'\n')) n--;
  if (*s) out[n++] = L'…';
  out[n] = 0;
}

/* последние реплики пузырями: ширина, высота, место в ленте */
static void am_build(HDC dc, int width) {
  int padX = am_px(10), padY = am_px(7), gap = am_px(8), y = 0;
  g_amCount = 0;
  SelectObject(dc, g_amFont);
  if (!g_acN && !am_busy()) {
    AmItem *it = &g_amItems[g_amCount++];
    it->kind = AM_HINT;
    lstrcpynW(it->text, L"Спросите что-нибудь — отвечу здесь. ⤢ — полный чат с историей.", 720);
    RECT tr = {0, 0, width - 2 * padX, 0};
    DrawTextW(dc, it->text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_CENTER);
    SetRect(&it->r, 0, 0, width, tr.bottom + 2 * padY);
    y = it->r.bottom + gap;
  }
  for (int i = g_acN > 6 ? g_acN - 6 : 0; i < g_acN && g_amCount < 8; i++) {
    AmItem *it = &g_amItems[g_amCount++];
    it->kind = g_acTurns[i].assistant ? AM_AI : AM_USER;
    wchar_t *t = it->text;
    int pre = 0;
    if (g_acTurns[i].png) pre = swprintf(t, 720, L"[картинка] ");
    am_clean(g_acTurns[i].text, t + pre, 720 - pre);
    int maxW = (int)(width * (it->kind == AM_AI ? 0.94f : 0.80f)) - 2 * padX;
    RECT tr = {0, 0, maxW, 0};
    DrawTextW(dc, t, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    int bw = tr.right + 2 * padX, bh = tr.bottom + 2 * padY;
    int x = it->kind == AM_AI ? 0 : width - bw;
    SetRect(&it->r, x, y, x + bw, y + bh);
    y += bh + gap;
  }
  if (am_busy()) { /* ИИ ещё работает — пузырь с бегущими точками */
    AmItem *it = &g_amItems[g_amCount++];
    it->kind = AM_DOTS;
    it->text[0] = 0;
    SetRect(&it->r, 0, y, am_px(58), y + am_px(30));
    y = it->r.bottom + gap;
  }
  g_amContentH = y > gap ? y - gap : 0;
}

/* скруглённая фигура со сглаженным краем (ui_theme.c) */
static void am_shape(HDC dc, RECT r, float rad, COLORREF fill, COLORREF border, float bw) {
  RECT box = r;
  InflateRect(&box, 1, 1);
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, dc, box, box.left, box.top, TRUE)) return;
  UiShape s;
  memset(&s, 0, sizeof(s));
  s.r = rad;
  s.fill = fill;
  s.fillA = 255;
  if (bw > 0.0f) {
    s.border = border;
    s.borderA = 255;
    s.bw = bw;
  }
  ui_shape(&cv, r, &s);
  ui_canvas_end(&cv, dc);
}

static void am_icon(HDC dc, RECT r, wchar_t glyph, const wchar_t *fallback, COLORREF col, int px) {
  HFONT f = ui_icon_font(am_px(px));
  SetTextColor(dc, col);
  HGDIOBJ of = SelectObject(dc, f ? f : g_amBold);
  if (f) DrawTextW(dc, &glyph, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  else DrawTextW(dc, fallback, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, of);
}

static void am_paint(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC pdc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  HDC dc = CreateCompatibleDC(pdc);
  HBITMAP bm = dc ? CreateCompatibleBitmap(pdc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1) : NULL;
  if (!dc || !bm) {
    if (bm) DeleteObject(bm);
    if (dc) DeleteDC(dc);
    EndPaint(hwnd, &ps);
    return;
  }
  HGDIOBJ ob = SelectObject(dc, bm);
  FillRect(dc, &rc, g_amBgBr);
  SetBkMode(dc, TRANSPARENT);
  /* шапка: огонёк настроения, имя, чем занят; ⤢ и × справа */
  int head = am_px(42);
  RECT dot = {am_px(14), head / 2 - am_px(5), am_px(24), head / 2 + am_px(5)};
  COLORREF mood = kAcAvatarMood[g_acAvatarState];
  am_shape(dc, dot, -1.0f, mood, mood, 0.0f);
  SelectObject(dc, g_amBold);
  SetTextColor(dc, AM_INK);
  RECT tt = {am_px(32), 0, am_px(120), head};
  DrawTextW(dc, L"Мини-ИИ", -1, &tt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SIZE nameSz = {0, 0};
  GetTextExtentPoint32W(dc, L"Мини-ИИ", 7, &nameSz);
  const wchar_t *sub = g_amStatus[0] ? g_amStatus : am_busy() ? L"думает…"
                       : g_aiProvider            ? L"Z.ai API"
                                                 : L"на этом компьютере";
  SelectObject(dc, g_amSmall);
  SetTextColor(dc, am_busy() ? AM_CYAN : AM_MUTED);
  RECT st = {am_px(32) + nameSz.cx + am_px(8), 0, g_amBtn[AM_FULL].left - am_px(4), head};
  DrawTextW(dc, sub, -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  for (int b = AM_FULL; b <= AM_CLOSE; b++) {
    if (g_amHot == b) am_shape(dc, g_amBtn[b], 6.0f, b == AM_CLOSE ? RGB(196, 43, 28) : RGB(40, 47, 80), 0, 0.0f);
    am_icon(dc, g_amBtn[b], b == AM_CLOSE ? 0xE8BB : 0xE740, b == AM_CLOSE ? L"×" : L"⤢", AM_INK, 11);
  }
  TRIVERTEX v[2] = {{0, head - 2, 0x7B00, 0x5C00, 0xFF00, 0}, {rc.right, head, 0x3800, 0xE100, 0xFF00, 0}};
  GRADIENT_RECT gr = {0, 1};
  GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H);
  /* лента: новое — внизу, колесом — к старому */
  am_build(dc, g_amMsgs.right - g_amMsgs.left);
  int areaH = g_amMsgs.bottom - g_amMsgs.top;
  int maxScroll = g_amContentH > areaH ? g_amContentH - areaH : 0;
  if (g_amScroll > maxScroll) g_amScroll = maxScroll;
  int top = g_amMsgs.bottom - g_amContentH + g_amScroll;
  int saved = SaveDC(dc);
  IntersectClipRect(dc, g_amMsgs.left, g_amMsgs.top, g_amMsgs.right, g_amMsgs.bottom);
  for (int i = 0; i < g_amCount; i++) {
    AmItem *it = &g_amItems[i];
    RECT r = it->r;
    OffsetRect(&r, g_amMsgs.left, top);
    if (r.bottom < g_amMsgs.top || r.top > g_amMsgs.bottom) continue;
    if (it->kind == AM_HINT) {
      SelectObject(dc, g_amFont);
      SetTextColor(dc, AM_MUTED);
      RECT tr = r;
      InflateRect(&tr, -am_px(10), -am_px(7));
      DrawTextW(dc, it->text, -1, &tr, DT_WORDBREAK | DT_NOPREFIX | DT_CENTER);
      continue;
    }
    if (it->kind == AM_DOTS) {
      am_shape(dc, r, (float)am_px(12), RGB(24, 30, 52), 0, 0.0f);
      DWORD now = GetTickCount();
      for (int d = 0; d < 3; d++) {
        float ph = 0.5f + 0.5f * sinf(now / 170.0f - d * 1.1f);
        int cx = r.left + am_px(17) + d * am_px(12), cy = (r.top + r.bottom) / 2 - (int)(ph * am_px(3));
        int rr = am_px(3) + (ph > 0.5f ? 1 : 0);
        RECT dd = {cx - rr, cy - rr, cx + rr, cy + rr};
        am_shape(dc, dd, -1.0f, ac_mix(AM_VIOLET, AM_CYAN, d / 2.0f), 0, 0.0f);
      }
      continue;
    }
    BOOL ai = it->kind == AM_AI;
    am_shape(dc, r, (float)am_px(12), ai ? RGB(24, 30, 52) : RGB(40, 48, 88), 0, 0.0f);
    if (ai) { /* у ответа — бирюзовая черта слева, как у пузыря помощника */
      RECT bar = {r.left + am_px(3), r.top + am_px(8), r.left + am_px(5), r.bottom - am_px(8)};
      am_shape(dc, bar, 1.0f, AM_CYAN, 0, 0.0f);
    }
    SelectObject(dc, g_amFont);
    SetTextColor(dc, AM_INK);
    RECT tr = r;
    InflateRect(&tr, -am_px(10), -am_px(7));
    DrawTextW(dc, it->text, -1, &tr, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
  }
  RestoreDC(dc, saved);
  /* план файловой задачи ждёт решения */
  if (g_acPlanOn) {
    SelectObject(dc, g_amSmall);
    SetTextColor(dc, AM_MUTED);
    RECT pt = {g_amMsgs.left + am_px(4), g_amBtn[AM_APPLY].top, g_amBtn[AM_APPLY].left - am_px(6), g_amBtn[AM_APPLY].bottom};
    DrawTextW(dc, L"План готов — проверьте", -1, &pt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    am_shape(dc, g_amBtn[AM_APPLY], (float)am_px(8), g_amHot == AM_APPLY ? RGB(120, 236, 255) : AM_CYAN, 0, 0.0f);
    am_shape(dc, g_amBtn[AM_CANCEL], (float)am_px(8), g_amHot == AM_CANCEL ? RGB(48, 56, 94) : RGB(32, 38, 66),
             RGB(56, 66, 106), 1.0f);
    SelectObject(dc, g_amFont);
    SetTextColor(dc, RGB(8, 14, 30));
    DrawTextW(dc, L"Применить", -1, &g_amBtn[AM_APPLY], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, AM_INK);
    DrawTextW(dc, L"Отмена", -1, &g_amBtn[AM_CANCEL], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  /* поле ввода и «отправить» / «стоп» */
  BOOL focus = GetFocus() == g_amIn;
  am_shape(dc, g_amField, (float)am_px(14), AM_FIELD, focus ? AM_CYAN : RGB(46, 54, 88), focus ? 1.5f : 1.0f);
  BOOL busy = am_busy();
  COLORREF sendBg = busy ? RGB(72, 40, 60) : AM_CYAN;
  if (g_amHot == AM_SEND) sendBg = busy ? RGB(100, 48, 70) : RGB(120, 236, 255);
  am_shape(dc, g_amBtn[AM_SEND], -1.0f, sendBg, 0, 0.0f);
  am_icon(dc, g_amBtn[AM_SEND], busy ? 0xE71A : 0xE724, busy ? L"■" : L"➤", busy ? RGB(255, 140, 160) : RGB(8, 14, 30), 13);
  BitBlt(pdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
  SelectObject(dc, ob);
  DeleteObject(bm);
  DeleteDC(dc);
  EndPaint(hwnd, &ps);
}

/* Высота — по содержимому (лента 70…300 px), низ окна — на уровне плеч робота. */
static void am_place(void) {
  if (!g_amWnd || !g_acAvatar || !IsWindowVisible(g_acAvatar)) return;
  RECT ar, cur;
  GetWindowRect(g_acAvatar, &ar);
  GetWindowRect(g_amWnd, &cur);
  MONITORINFO mi = {sizeof(mi)};
  if (!GetMonitorInfoW(MonitorFromWindow(g_acAvatar, MONITOR_DEFAULTTONEAREST), &mi)) return;
  RECT wa = mi.rcWork;
  int w = cur.right - cur.left, h = cur.bottom - cur.top, S = ar.right - ar.left;
  int tuck = S / 5, gap = am_px(2); /* у окна робота прозрачные поля — встаём к самому телу */
  g_amLeft = (ar.left + ar.right) / 2 > (wa.left + wa.right) / 2;
  int x = g_amLeft ? ar.left + tuck - gap - w : ar.right - tuck + gap;
  if (x < wa.left || x + w > wa.right) {
    g_amLeft = !g_amLeft;
    x = g_amLeft ? ar.left + tuck - gap - w : ar.right - tuck + gap;
  }
  if (x + w > wa.right) x = wa.right - w;
  if (x < wa.left) x = wa.left;
  int y = ar.top + S * 45 / 100 - h;
  if (y + h > wa.bottom) y = wa.bottom - h;
  if (y < wa.top) y = wa.top;
  g_amGap.x = ar.left - (g_amLeft ? x + w : x);
  g_amGap.y = ar.top - (y + h);
  g_amPlacing = TRUE;
  SetWindowPos(g_amWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
  g_amPlacing = FALSE;
}

static void am_layout(void) {
  if (!g_amWnd) return;
  int W = am_px(360), pad = am_px(10), head = am_px(42), field = am_px(52), act = g_acPlanOn ? am_px(42) : 0;
  HDC dc = GetDC(g_amWnd);
  if (dc) {
    am_build(dc, W - 2 * pad);
    ReleaseDC(g_amWnd, dc);
  }
  int msgsH = g_amContentH + am_px(8);
  if (msgsH < am_px(70)) msgsH = am_px(70);
  if (msgsH > am_px(300)) msgsH = am_px(300);
  int H = head + am_px(6) + msgsH + act + field + pad * 2;
  SetRect(&g_amMsgs, pad, head + am_px(6), W - pad, head + am_px(6) + msgsH);
  int y = g_amMsgs.bottom + am_px(6);
  memset(g_amBtn, 0, sizeof(g_amBtn));
  SetRect(&g_amBtn[AM_CLOSE], W - am_px(8) - am_px(30), (head - am_px(28)) / 2, W - am_px(8), (head + am_px(28)) / 2);
  SetRect(&g_amBtn[AM_FULL], g_amBtn[AM_CLOSE].left - am_px(32), g_amBtn[AM_CLOSE].top, g_amBtn[AM_CLOSE].left - am_px(2),
          g_amBtn[AM_CLOSE].bottom);
  if (act) {
    SetRect(&g_amBtn[AM_CANCEL], W - pad - am_px(82), y + am_px(4), W - pad, y + act - am_px(6));
    SetRect(&g_amBtn[AM_APPLY], g_amBtn[AM_CANCEL].left - am_px(8) - am_px(104), y + am_px(4),
            g_amBtn[AM_CANCEL].left - am_px(8), y + act - am_px(6));
    y += act;
  }
  SetRect(&g_amField, pad, y, W - pad, y + field);
  int sd = am_px(34);
  SetRect(&g_amBtn[AM_SEND], g_amField.right - am_px(9) - sd, (g_amField.top + g_amField.bottom - sd) / 2,
          g_amField.right - am_px(9), (g_amField.top + g_amField.bottom + sd) / 2);
  MoveWindow(g_amIn, g_amField.left + am_px(14), g_amField.top + am_px(9),
             g_amBtn[AM_SEND].left - am_px(8) - (g_amField.left + am_px(14)), field - am_px(18), TRUE);
  RECT cur;
  GetWindowRect(g_amWnd, &cur);
  if (cur.right - cur.left != W || cur.bottom - cur.top != H) {
    g_amPlacing = TRUE;
    SetWindowPos(g_amWnd, NULL, 0, 0, W, H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    g_amPlacing = FALSE;
    am_place(); /* низ — снова у робота */
  }
  InvalidateRect(g_amWnd, NULL, FALSE);
}

static void am_send(void) {
  if (am_busy()) return;
  int n = GetWindowTextLengthW(g_amIn);
  if (n <= 0) return;
  wchar_t *t = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
  if (!t) return;
  GetWindowTextW(g_amIn, t, n + 1);
  BOOL empty = TRUE;
  for (wchar_t *c = t; *c; c++)
    if (!iswspace(*c)) empty = FALSE;
  if (!empty) {
    if (!g_acWnd) ac_create();
    if (g_acWnd) {
      SetWindowTextW(g_acIn, t);
      SetWindowTextW(g_amIn, L"");
      g_amScroll = 0;
      ac_send(); /* дальше — как из полного чата: модель, поиск, файловые задачи */
    }
  }
  free(t);
  am_layout();
}

/* «Стоп», «Применить», «Отмена» — те же кнопки полного чата */
static void am_chat_command(int id, HWND btn) {
  if (g_acWnd) SendMessageW(g_acWnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)btn);
}

static int am_hit(int x, int y) {
  POINT p = {x, y};
  for (int b = AM_FULL; b < AM_BTNS; b++)
    if ((b != AM_APPLY && b != AM_CANCEL) || g_acPlanOn)
      if (PtInRect(&g_amBtn[b], p)) return b;
  return AM_NONE;
}

static LRESULT CALLBACK am_in_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (m == WM_KEYDOWN && w == VK_RETURN && !shift) {
    PostMessageW(g_amWnd, WM_AM_SEND, 0, 0);
    return 0;
  }
  if (m == WM_CHAR && (w == L'\r' || w == L'\n') && !shift) return 0;
  if (m == WM_KEYDOWN && w == VK_ESCAPE) {
    ac_mini_close();
    return 0;
  }
  if (m == WM_CHAR && w == 27) return 0;
  if (m == WM_CHAR && w == 1) { /* Ctrl+A — выделить всё */
    SendMessageW(h, EM_SETSEL, 0, -1);
    return 0;
  }
  if (m == WM_SETFOCUS || m == WM_KILLFOCUS) InvalidateRect(g_amWnd, &g_amField, FALSE);
  return CallWindowProcW(g_amInOld, h, m, w, l);
}

static LRESULT CALLBACK AmProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_NCHITTEST: {
    POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &p);
    if (p.y < am_px(42) && am_hit(p.x, p.y) == AM_NONE) return HTCAPTION; /* за шапку — тянуть */
    return HTCLIENT;
  }
  case WM_NCMOUSEMOVE:
  case WM_MOUSEMOVE: {
    POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (msg == WM_NCMOUSEMOVE) ScreenToClient(hwnd, &p);
    if (!g_amTrack) {
      TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0};
      g_amTrack = TrackMouseEvent(&t);
    }
    int hot = am_hit(p.x, p.y);
    if (hot != g_amHot) {
      g_amHot = hot;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  }
  case WM_MOUSELEAVE:
    g_amTrack = FALSE;
    if (g_amHot) {
      g_amHot = AM_NONE;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_SETCURSOR: {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(hwnd, &p);
    if (LOWORD(lParam) == HTCLIENT && am_hit(p.x, p.y) != AM_NONE) {
      SetCursor(LoadCursorW(NULL, IDC_HAND));
      return TRUE;
    }
    break;
  }
  case WM_LBUTTONUP:
    switch (am_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
    case AM_FULL: ac_mini_close(); aichat_show_full(); break;
    case AM_CLOSE: ac_mini_close(); break;
    case AM_SEND:
      if (am_busy()) am_chat_command(ID_AC_STOP, g_acStop);
      else am_send();
      break;
    case AM_APPLY: am_chat_command(ID_AC_APPLY, g_acApply); break;
    case AM_CANCEL: am_chat_command(ID_AC_CANCEL, g_acCancel); break;
    default: SetFocus(g_amIn); break;
    }
    return 0;
  case WM_MOUSEWHEEL:
    g_amScroll += GET_WHEEL_DELTA_WPARAM(wParam) * am_px(40) / WHEEL_DELTA;
    if (g_amScroll < 0) g_amScroll = 0;
    InvalidateRect(hwnd, &g_amMsgs, FALSE);
    return 0;
  case WM_MOVE:
    if (!g_amPlacing && IsWindowVisible(hwnd)) { /* окно потянули — робот идёт следом */
      RECT r;
      GetWindowRect(hwnd, &r);
      ac_avatar_follow_panel((g_amLeft ? r.right : r.left) + g_amGap.x, r.bottom + g_amGap.y);
    }
    break;
  case WM_EXITSIZEMOVE: ac_avatar_save_settings(); break;
  case WM_ACTIVATE:
    if (LOWORD(wParam) != WA_INACTIVE) SetFocus(g_amIn);
    return 0;
  case WM_AM_SEND: am_send(); return 0;
  case WM_TIMER:
    if (wParam == AM_TIMER) {
      if (!am_busy() || !IsWindowVisible(hwnd)) KillTimer(hwnd, AM_TIMER);
      InvalidateRect(hwnd, &g_amMsgs, FALSE);
      return 0;
    }
    break;
  case WM_CTLCOLOREDIT:
    SetBkColor((HDC)wParam, AM_FIELD);
    SetTextColor((HDC)wParam, AM_INK);
    return (LRESULT)g_amFieldBr;
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: am_paint(hwnd); return 0;
  case WM_CLOSE: ac_mini_close(); return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* беседа или строка состояния сменились — перерисовать (и подогнать высоту) */
static void ac_mini_refresh(void) {
  if (!ac_mini_open()) return;
  am_layout();
  if (am_busy()) SetTimer(g_amWnd, AM_TIMER, 120, NULL);
}

/* строка состояния чата (ac_status) — в шапку мини-чата, без подсказок про «Стоп» */
static void ac_mini_status(const wchar_t *t) {
  g_amStatus[0] = 0;
  if (t && t[0]) {
    lstrcpynW(g_amStatus, t, 200);
    wchar_t *stop = wcsstr(g_amStatus, L"Стоп");
    if (stop && stop != g_amStatus) *stop = 0;
    for (size_t n = wcslen(g_amStatus); n && wcschr(L" ·—.,", g_amStatus[n - 1]); n--) g_amStatus[n - 1] = 0;
    if (!wcsncmp(g_amStatus, L"Enter", 5)) g_amStatus[0] = 0; /* подсказка полного чата — здесь лишняя */
  }
  if (ac_mini_open()) InvalidateRect(g_amWnd, NULL, FALSE);
}

static void ac_mini_follow(void) {
  if (ac_mini_open() && !g_amPlacing) am_place();
}

/* помощника отозвали — мини-чат закрывается (беседа остаётся в полном чате) */
static void ac_mini_sync(void) {
  if (!ac_avatar_here()) ac_mini_close();
}

static void ac_mini_show(void) {
  if (!g_acWnd) ac_create(); /* беседа живёт в окне чата, оно может оставаться скрытым */
  if (!g_acWnd) return;
  if (!g_amWnd) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = AmProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CursorPadMiniChat";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    g_amK = MulDiv(100, ui_dpi(NULL), 96);
    g_amFont = CreateFontW(-am_px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_amBold = CreateFontW(-am_px(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_amSmall = CreateFontW(-am_px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_amBgBr = CreateSolidBrush(AM_BG);
    g_amFieldBr = CreateSolidBrush(AM_FIELD);
    g_amWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"CursorPadMiniChat", L"Мини-ИИ",
                              WS_POPUP | WS_CLIPCHILDREN, 0, 0, am_px(360), am_px(240), NULL, NULL, g_inst, NULL);
    if (!g_amWnd) return;
    ui_dwm_set(g_amWnd, UI_DWMWA_CORNER, 2); /* DWMWCP_ROUND: скругление Windows 11 */
    ui_dwm_set(g_amWnd, 34, AM_VIOLET);      /* DWMWA_BORDER_COLOR: кромка, как у помощника */
    g_amIn = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                             0, 0, 10, 10, g_amWnd, (HMENU)(INT_PTR)ID_AM_IN, g_inst, NULL);
    SendMessageW(g_amIn, WM_SETFONT, (WPARAM)g_amFont, TRUE);
    SendMessageW(g_amIn, EM_SETCUEBANNER, FALSE, (LPARAM)L"Спросите… Enter — отправить");
    g_amInOld = (WNDPROC)SetWindowLongPtrW(g_amIn, GWLP_WNDPROC, (LONG_PTR)am_in_proc);
  }
  BOOL was = IsWindowVisible(g_amWnd);
  g_amScroll = 0;
  am_layout(); /* размер и место — до показа, чтобы не мигнуло в углу экрана */
  am_place();
  ShowWindow(g_amWnd, SW_SHOWNOACTIVATE);
  SetForegroundWindow(g_amWnd);
  SetFocus(g_amIn);
  ac_bubble_hide(TRUE);
  if (!was && !am_busy()) ac_avatar_set(AC_AV_LISTENING);
  if (am_busy()) SetTimer(g_amWnd, AM_TIMER, 120, NULL);
  if (!am_busy() && !ai_ready()) ac_status(ai_status_text(g_aiOn ? AI_NO_MODEL : AI_OFF));
}
