/* ---- Сообщения коллегам, которые исчезают ----------------------------------

   «Ещё» → «Написать коллеге»: выбрали, кто сейчас в сети (тот же список,
   что у нард), написали, выбрали, через сколько исчезнет, — «Отправить».

   У коллеги в углу экрана всплывает окошко с текстом. Отсчёт идёт, только
   когда человек за компьютером (тронул мышь или клавиатуру), и замирает,
   пока мышь над окошком — дочитать. Время вышло или нажали «Закрыть» —
   окошко пропадает и текст стирается из памяти. Нигде не сохраняется: ни в
   заметках, ни в истории.

   Как ходит. Всё через ту же общую папку:

     CursorPad-Msg\<кому>\<сообщение>.txt

   Программа получателя раз в 3 секунды заглядывает в свою папку, читает
   сообщение и сразу удаляет файл. Отправитель видит «доставлено», когда
   файла не стало. Не забрали за сутки (компьютер выключен) — удаляется
   непрочитанным.

   Файлы не шифруются: пока сообщение лежит в папке (обычно секунды), его
   может открыть любой, у кого есть доступ к ней. Пароли так не передавать. */

#define MSG_SUB L"CursorPad-Msg"
#define WM_MSG_IN (WM_APP + 19)   /* lParam — MsgIn*, освобождает получатель */
#define WM_MSG_SENT (WM_APP + 20) /* lParam — имя получателя (malloc) */
#define MSG_MAXLEN 1000
/* ID_MSG_* — в cursorpad.c: «Отправить» и «Ответить» рисуются синими */
#define TIMER_MSG_TICK 1

typedef struct {
  wchar_t from[96], name[128];
  int ttl; /* секунд */
  wchar_t *text;
} MsgIn;

typedef struct {
  MsgIn m;
  DWORD arrived;    /* GetTickCount, когда показали */
  BOOL started;     /* человек за компьютером — отсчёт идёт */
  int leftMs;
  DWORD lastTick;
  int slot;         /* место в стопке у края экрана */
} MsgWin;

static const int kMsgTtl[3] = {30, 60, 300};
static const wchar_t *const kMsgTtlName[3] = {L"30 с", L"1 мин", L"5 мин"};

static float g_msgS = 1.0f;
static HWND g_msgOut, g_msgList, g_msgEdit;
static WNDPROC g_msgEditOld;
static int g_msgTtl = 1; /* по умолчанию — минута */
static wchar_t g_msgStatus[200];
/* кому можно написать: кто в сети, плюс тот, кому отвечаем */
static wchar_t g_msgToId[ND_MAXONLINE + 1][96], g_msgToName[ND_MAXONLINE + 1][128];
static int g_msgToN;
static wchar_t g_msgReplyId[96], g_msgReplyName[128];
static BOOL g_msgInSlots[8];
static BOOL g_msgThread;

/* ждут доставки: поток смотрит, не забрали ли файл */
#define MSG_MAXPEND 16
static struct {
  wchar_t path[SHARE_PATH];
  wchar_t name[128];
  ULONGLONG t0;
} g_msgPend[MSG_MAXPEND];
static int g_msgPendN;
static CRITICAL_SECTION g_msgCs;
static BOOL g_msgCsOk;

static int MS_(int v) { return (int)(v * g_msgS + 0.5f); }

static void msg_lock(void) {
  if (!g_msgCsOk) {
    InitializeCriticalSection(&g_msgCs);
    g_msgCsOk = TRUE;
  }
  EnterCriticalSection(&g_msgCs);
}
static void msg_unlock(void) { LeaveCriticalSection(&g_msgCs); }

static BOOL msg_dir(const wchar_t *root, const wchar_t *who, wchar_t *out) {
  if (!root[0]) return FALSE;
  _snwprintf(out, SHARE_PATH, L"%s\\%s", root, MSG_SUB);
  out[SHARE_PATH - 1] = 0;
  CreateDirectoryW(out, NULL);
  size_t l = wcslen(out);
  _snwprintf(out + l, SHARE_PATH - l, L"\\%s", who);
  out[SHARE_PATH - 1] = 0;
  CreateDirectoryW(out, NULL);
  DWORD a = GetFileAttributesW(out);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* текст в одну строку файла: перевод строки → \n, обратная черта → \\ */
static wchar_t *msg_escape(const wchar_t *s) {
  size_t n = wcslen(s);
  wchar_t *o = (wchar_t *)malloc((n * 2 + 1) * sizeof(wchar_t));
  if (!o) return NULL;
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    if (s[i] == L'\r') continue;
    if (s[i] == L'\n') {
      o[k++] = L'\\';
      o[k++] = L'n';
    } else if (s[i] == L'\\') {
      o[k++] = L'\\';
      o[k++] = L'\\';
    } else if (s[i] == L'\t') {
      o[k++] = L' ';
    } else {
      o[k++] = s[i];
    }
  }
  o[k] = 0;
  return o;
}

/* обратно, и сразу с \r\n — так его рисует Windows */
static wchar_t *msg_unescape(const wchar_t *s) {
  size_t n = wcslen(s);
  wchar_t *o = (wchar_t *)malloc((n * 2 + 1) * sizeof(wchar_t));
  if (!o) return NULL;
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    if (s[i] == L'\\' && s[i + 1] == L'n') {
      o[k++] = L'\r';
      o[k++] = L'\n';
      i++;
    } else if (s[i] == L'\\' && s[i + 1] == L'\\') {
      o[k++] = L'\\';
      i++;
    } else {
      o[k++] = s[i];
    }
  }
  o[k] = 0;
  return o;
}

static void msg_wipe(wchar_t *s) {
  if (s) SecureZeroMemory(s, wcslen(s) * sizeof(wchar_t));
}

/* строку «text» nd_field не возьмёт целиком (ограничен размер) — своя */
static wchar_t *msg_field_dup(const wchar_t *t, const wchar_t *key) {
  size_t kl = wcslen(key);
  for (const wchar_t *p = t; p && *p;) {
    const wchar_t *nl = wcschr(p, L'\n');
    size_t len = nl ? (size_t)(nl - p) : wcslen(p);
    if (len > kl && !wcsncmp(p, key, kl) && p[kl] == L'\t') {
      size_t vl = len - kl - 1;
      if (vl && p[kl + vl] == L'\r') vl--;
      wchar_t *o = (wchar_t *)malloc((vl + 1) * sizeof(wchar_t));
      if (!o) return NULL;
      memcpy(o, p + kl + 1, vl * sizeof(wchar_t));
      o[vl] = 0;
      return o;
    }
    p = nl ? nl + 1 : NULL;
  }
  return NULL;
}

/* ---- поток: забрать пришедшее, проверить доставку отправленного ---------- */
static DWORD WINAPI msg_thread(LPVOID param) {
  (void)param;
  for (;;) {
    Sleep(3000);
    wchar_t root[MAX_PATH];
    share_root_copy(root);
    if (!root[0]) continue;
    wchar_t myId[96], dir[SHARE_PATH];
    nd_myid(myId, 96, NULL, 0);
    if (msg_dir(root, myId, dir)) {
      wchar_t pat[SHARE_PATH + 8];
      _snwprintf(pat, SHARE_PATH + 8, L"%s\\*.txt", dir);
      WIN32_FIND_DATAW fd;
      HANDLE f = FindFirstFileW(pat, &fd);
      if (f != INVALID_HANDLE_VALUE) {
        do {
          wchar_t full[SHARE_PATH];
          _snwprintf(full, SHARE_PATH, L"%s\\%s", dir, fd.cFileName);
          full[SHARE_PATH - 1] = 0;
          if (!ft_within(fd.ftLastWriteTime, 86400)) { /* пролежало сутки — уже не нужно */
            DeleteFileW(full);
            continue;
          }
          wchar_t *t = share_read(full);
          if (!t) continue;
          size_t tl = wcslen(t);
          if (!tl || t[tl - 1] != L'\n') { /* ещё пишется */
            free(t);
            continue;
          }
          /* удалили — значит, наше; не вышло — попробуем в следующий раз,
             иначе показали бы одно и то же дважды */
          if (!DeleteFileW(full)) {
            msg_wipe(t);
            free(t);
            continue;
          }
          MsgIn *m = (MsgIn *)calloc(1, sizeof(MsgIn));
          wchar_t ttl[16] = L"";
          wchar_t *raw = msg_field_dup(t, L"text");
          if (m && raw) {
            nd_field(t, L"from", m->from, 96);
            nd_field(t, L"name", m->name, 128);
            nd_field(t, L"ttl", ttl, 16);
            m->ttl = _wtoi(ttl);
            if (m->ttl < 5 || m->ttl > 3600) m->ttl = 60;
            m->text = msg_unescape(raw);
          }
          if (raw) {
            msg_wipe(raw);
            free(raw);
          }
          msg_wipe(t);
          free(t);
          if (m && m->text && g_hwnd && PostMessageW(g_hwnd, WM_MSG_IN, 0, (LPARAM)m)) continue;
          if (m) {
            msg_wipe(m->text);
            free(m->text);
            free(m);
          }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
      }
    }
    /* отправленные: файла нет — коллега забрал */
    msg_lock();
    for (int i = 0; i < g_msgPendN;) {
      BOOL gone = GetFileAttributesW(g_msgPend[i].path) == INVALID_FILE_ATTRIBUTES &&
                  (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND);
      BOOL old = GetTickCount64() - g_msgPend[i].t0 > 30ull * 60 * 1000; /* полчаса — перестаём ждать */
      if (gone || old) {
        if (gone && g_hwnd) {
          wchar_t *nm = _wcsdup(g_msgPend[i].name);
          if (nm && !PostMessageW(g_hwnd, WM_MSG_SENT, 0, (LPARAM)nm)) free(nm);
        }
        g_msgPend[i] = g_msgPend[--g_msgPendN];
      } else {
        i++;
      }
    }
    msg_unlock();
  }
  return 0;
}

static void msg_start(void) {
  if (g_msgThread) return;
  g_msgThread = TRUE;
  HANDLE th = CreateThread(NULL, 0, msg_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
}

/* ---- окошко пришедшего сообщения ------------------------------------------ */

#define MSGIN_W 340
static void msgin_close(HWND hwnd) {
  MsgWin *w = (MsgWin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (w) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    if (w->slot >= 0 && w->slot < 8) g_msgInSlots[w->slot] = FALSE;
    msg_wipe(w->m.text);
    free(w->m.text);
    SecureZeroMemory(w, sizeof(*w));
    free(w);
  }
  DestroyWindow(hwnd);
}

static RECT msgin_text_rect(HWND hwnd) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  RECT t = {MS_(16), PANEL_TITLE_H + MS_(26), rc.right - MS_(16), rc.bottom - MS_(64)};
  return t;
}

static void msgin_paint(HWND hwnd, HDC hdc) {
  MsgWin *w = (MsgWin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Сообщение");
  if (!w) return;
  SetBkMode(hdc, TRANSPARENT);
  int pad = MS_(16);
  /* от кого */
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  wchar_t from[180];
  _snwprintf(from, 180, L"от %s", w->m.name[0] ? w->m.name : w->m.from);
  from[179] = 0;
  RECT fr = {pad, PANEL_TITLE_H + MS_(4), rc.right - pad, PANEL_TITLE_H + MS_(22)};
  DrawTextW(hdc, from, -1, &fr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  /* сам текст */
  if (g_fontBody) SelectObject(hdc, g_fontBody);
  SetTextColor(hdc, COL_INK);
  RECT t = msgin_text_rect(hwnd);
  DrawTextW(hdc, w->m.text, -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS);
  /* полоска: сколько осталось */
  int total = w->m.ttl * 1000;
  int left = w->leftMs < 0 ? 0 : w->leftMs;
  RECT bar = {pad, rc.bottom - MS_(56), rc.right - pad, rc.bottom - MS_(52)};
  HBRUSH bb = CreateSolidBrush(COL_LINE);
  FillRect(hdc, &bar, bb);
  DeleteObject(bb);
  RECT done = bar;
  done.right = bar.left + (int)((long long)(bar.right - bar.left) * left / (total ? total : 1));
  HBRUSH db = CreateSolidBrush(COL_SAGE);
  FillRect(hdc, &done, db);
  DeleteObject(db);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  wchar_t lt[80];
  int sec = (left + 999) / 1000;
  if (sec >= 60) _snwprintf(lt, 80, L"исчезнет через %d:%02d", sec / 60, sec % 60);
  else _snwprintf(lt, 80, L"исчезнет через %d с", sec);
  lt[79] = 0;
  RECT lr = {pad, rc.bottom - MS_(38), rc.right / 2, rc.bottom - MS_(12)};
  DrawTextW(hdc, lt, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void msgin_tick(HWND hwnd) {
  MsgWin *w = (MsgWin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (!w) return;
  DWORD now = GetTickCount();
  if (!w->started) {
    /* отсчёт — с первого движения человека после прихода: вдруг он отошёл */
    LASTINPUTINFO li = {sizeof(li), 0};
    if (GetLastInputInfo(&li) && (int)(li.dwTime - w->arrived) > 0) {
      w->started = TRUE;
      w->lastTick = now;
    }
    return;
  }
  POINT pt;
  RECT wr;
  GetCursorPos(&pt);
  GetWindowRect(hwnd, &wr);
  BOOL hover = PtInRect(&wr, pt); /* мышь над окошком — читают, не торопим */
  if (!hover) w->leftMs -= (int)(now - w->lastTick);
  w->lastTick = now;
  if (w->leftMs <= 0) {
    msgin_close(hwnd);
    return;
  }
  RECT rc;
  GetClientRect(hwnd, &rc);
  RECT low = {0, rc.bottom - MS_(60), rc.right, rc.bottom}; /* полоска и надпись; кнопки — свои окна, их не тронет */
  InvalidateRect(hwnd, &low, FALSE);
}

static void msg_compose(const wchar_t *toId, const wchar_t *toName);

static LRESULT CALLBACK MsgInProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    msgin_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE; /* не отнимать фокус у того, где человек печатает */
  case WM_TIMER:
    if (wParam == TIMER_MSG_TICK) msgin_tick(hwnd);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    MsgWin *w = (MsgWin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (id == ID_MSG_REPLY && w) {
      wchar_t fid[96], fname[128];
      lstrcpynW(fid, w->m.from, 96);
      lstrcpynW(fname, w->m.name, 128);
      msg_compose(fid, fname);
    }
    if (id == ID_MSG_DISMISS || id == ID_PANEL_CLOSE) msgin_close(hwnd);
    return 0;
  }
  case WM_CLOSE:
    msgin_close(hwnd);
    return 0;
  case WM_DESTROY: {
    MsgWin *w = (MsgWin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (w) { /* закрыли не кнопкой (выход из программы) — всё равно стереть */
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      if (w->slot >= 0 && w->slot < 8) g_msgInSlots[w->slot] = FALSE;
      msg_wipe(w->m.text);
      free(w->m.text);
      SecureZeroMemory(w, sizeof(*w));
      free(w);
    }
    return 0;
  }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void msg_scale(void) {
  HDC s = GetDC(NULL);
  g_msgS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
  if (s) ReleaseDC(NULL, s);
}

static void msg_show_in(MsgIn *m) {
  if (!m) return;
  static BOOL reg;
  if (!reg) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgInProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadMsgIn";
    reg = RegisterClassExW(&wc) != 0;
  }
  msg_scale();
  MsgWin *w = (MsgWin *)calloc(1, sizeof(MsgWin));
  if (!w) {
    msg_wipe(m->text);
    free(m->text);
    free(m);
    return;
  }
  w->m = *m;
  free(m);
  w->leftMs = w->m.ttl * 1000;
  w->arrived = GetTickCount();
  w->slot = -1;
  for (int i = 0; i < 8 && w->slot < 0; i++)
    if (!g_msgInSlots[i]) w->slot = i;
  if (w->slot >= 0) g_msgInSlots[w->slot] = TRUE;
  /* высота — по тексту, но не больше 14 строк */
  int W = MS_(MSGIN_W);
  int textW = W - MS_(32);
  int textH = MS_(20);
  HDC dc = GetDC(NULL);
  if (dc) {
    HGDIOBJ of = g_fontBody ? SelectObject(dc, g_fontBody) : NULL;
    RECT c = {0, 0, textW, 0};
    DrawTextW(dc, w->m.text, -1, &c, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_CALCRECT);
    textH = c.bottom;
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    if (textH > tm.tmHeight * 14) textH = tm.tmHeight * 14;
    if (of) SelectObject(dc, of);
    ReleaseDC(NULL, dc);
  }
  int H = PANEL_TITLE_H + MS_(26) + textH + MS_(72);
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  /* правый нижний угол; следующие — стопкой выше, по кругу */
  int slot = w->slot < 0 ? 0 : w->slot;
  int x = wa.right - W - MS_(16) - (slot / 3) * MS_(24);
  int y = wa.bottom - H - MS_(16) - (slot % 3) * MS_(60);
  if (y < wa.top) y = wa.top;
  HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"CursorPadMsgIn", L"Сообщение",
                           WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, x, y, W, H, NULL, NULL, g_inst, NULL);
  if (!h) {
    if (w->slot >= 0) g_msgInSlots[w->slot] = FALSE;
    msg_wipe(w->m.text);
    free(w->m.text);
    free(w);
    return;
  }
  SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)w);
  round_corners(h);
  mk_btn(h, L"×", ID_PANEL_CLOSE);
  place_panel_close(h);
  RECT rc;
  GetClientRect(h, &rc);
  int bh = MS_(28), by = rc.bottom - MS_(12) - bh;
  HWND b1 = mk_btn(h, L"Ответить", ID_MSG_REPLY);
  HWND b2 = mk_btn(h, L"Закрыть", ID_MSG_DISMISS);
  MoveWindow(b2, rc.right - MS_(16) - MS_(84), by, MS_(84), bh, FALSE);
  MoveWindow(b1, rc.right - MS_(16) - MS_(84) - MS_(8) - MS_(96), by, MS_(96), bh, FALSE);
  if (!w->m.from[0]) EnableWindow(b1, FALSE);
  SetTimer(h, TIMER_MSG_TICK, 250, NULL);
  ShowWindow(h, SW_SHOWNOACTIVATE);
  MessageBeep(MB_ICONASTERISK);
}

/* ---- окно «Написать коллеге» ---------------------------------------------- */

static void msg_out_status(const wchar_t *s) {
  lstrcpynW(g_msgStatus, s, 200);
  if (g_msgOut) {
    RECT rc;
    GetClientRect(g_msgOut, &rc);
    RECT st = {0, rc.bottom - MS_(34), rc.right, rc.bottom};
    InvalidateRect(g_msgOut, &st, FALSE);
  }
}

/* список «кому» из того, что знает поток нард (кто в сети) */
static void msg_refresh_list(void) {
  if (!g_msgOut || !g_msgList) return;
  wchar_t selId[96] = L"";
  int sel = (int)SendMessageW(g_msgList, LB_GETCURSEL, 0, 0);
  if (sel >= 0 && sel < g_msgToN) lstrcpynW(selId, g_msgToId[sel], 96);
  if (!selId[0] && g_msgReplyId[0]) lstrcpynW(selId, g_msgReplyId, 96);
  g_msgToN = 0;
  NdPoll *pl = g_nd.poll;
  if (pl)
    for (int i = 0; i < pl->nOnline && g_msgToN < ND_MAXONLINE; i++) {
      lstrcpynW(g_msgToId[g_msgToN], pl->onId[i], 96);
      lstrcpynW(g_msgToName[g_msgToN], pl->onName[i], 128);
      g_msgToN++;
    }
  if (g_msgReplyId[0]) { /* кому отвечаем — в списке, даже если его не видно в сети */
    BOOL have = FALSE;
    for (int i = 0; i < g_msgToN && !have; i++) have = !wcscmp(g_msgToId[i], g_msgReplyId);
    if (!have && g_msgToN < ND_MAXONLINE + 1) {
      lstrcpynW(g_msgToId[g_msgToN], g_msgReplyId, 96);
      lstrcpynW(g_msgToName[g_msgToN], g_msgReplyName[0] ? g_msgReplyName : g_msgReplyId, 128);
      g_msgToN++;
    }
  }
  SendMessageW(g_msgList, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_msgList, LB_RESETCONTENT, 0, 0);
  int keep = -1;
  for (int i = 0; i < g_msgToN; i++) {
    SendMessageW(g_msgList, LB_ADDSTRING, 0, (LPARAM)g_msgToName[i]);
    if (selId[0] && !wcscmp(selId, g_msgToId[i])) keep = i;
  }
  if (keep < 0 && g_msgToN > 0) keep = 0;
  if (keep >= 0) SendMessageW(g_msgList, LB_SETCURSEL, keep, 0);
  SendMessageW(g_msgList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_msgList, NULL, TRUE);
  EnableWindow(GetDlgItem(g_msgOut, ID_MSG_SEND), g_msgToN > 0);
  InvalidateRect(g_msgOut, NULL, FALSE);
}

static void msg_ttl_buttons(void) {
  if (!g_msgOut) return;
  for (int i = 0; i < 3; i++) {
    wchar_t t[32];
    if (i == g_msgTtl) _snwprintf(t, 32, L"\x25CF %s", kMsgTtlName[i]); /* ● — выбрано */
    else lstrcpynW(t, kMsgTtlName[i], 32);
    t[31] = 0;
    SetWindowTextW(GetDlgItem(g_msgOut, ID_MSG_TTL0 + i), t);
  }
}

static void msg_send(void) {
  int sel = (int)SendMessageW(g_msgList, LB_GETCURSEL, 0, 0);
  if (sel < 0 || sel >= g_msgToN) {
    msg_out_status(L"Выберите, кому");
    return;
  }
  int len = GetWindowTextLengthW(g_msgEdit);
  if (len <= 0) {
    msg_out_status(L"Напишите текст");
    return;
  }
  wchar_t *text = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
  if (!text) return;
  GetWindowTextW(g_msgEdit, text, len + 1);
  wchar_t *esc = msg_escape(text);
  msg_wipe(text);
  free(text);
  if (!esc) return;
  wchar_t root[MAX_PATH], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  wchar_t myId[96], user[128], pc[64];
  nd_myid(myId, 96, NULL, 0);
  share_me(user, pc);
  BOOL ok = FALSE;
  if (root[0] && msg_dir(root, g_msgToId[sel], dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s-%llu-%lu.txt", dir, myId, (unsigned long long)ft_now(),
               (unsigned long)(GetTickCount() ^ GetCurrentProcessId()));
    path[SHARE_PATH - 1] = 0;
    size_t cap = wcslen(esc) + 600;
    wchar_t *body = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (body) {
      _snwprintf(body, cap, L"from\t%s\nname\t%s  ·  %s\nttl\t%d\ntext\t%s\n", myId, user, pc, kMsgTtl[g_msgTtl],
                 esc);
      body[cap - 1] = 0;
      ok = share_write(path, body);
      msg_wipe(body);
      free(body);
    }
  }
  msg_wipe(esc);
  free(esc);
  wchar_t st[200];
  if (!ok) {
    msg_out_status(root[0] ? L"Не получилось записать в общую папку — она доступна?" : L"Не задана общая папка (Настройки)");
    return;
  }
  msg_lock();
  if (g_msgPendN == MSG_MAXPEND) g_msgPend[0] = g_msgPend[--g_msgPendN]; /* самое старое больше не ждём */
  lstrcpynW(g_msgPend[g_msgPendN].path, path, SHARE_PATH);
  lstrcpynW(g_msgPend[g_msgPendN].name, g_msgToName[sel], 128);
  g_msgPend[g_msgPendN].t0 = GetTickCount64();
  g_msgPendN++;
  msg_unlock();
  SetWindowTextW(g_msgEdit, L"");
  wchar_t who[128];
  lstrcpynW(who, g_msgToName[sel], 128);
  wchar_t *dot = wcsstr(who, L"  ·");
  if (dot) *dot = 0;
  _snwprintf(st, 200, L"Отправлено: %s — ждём, когда заберёт", who);
  st[199] = 0;
  msg_out_status(st);
  SetFocus(g_msgEdit);
}

/* пришло от потока: файл забрали */
static void msg_on_sent(wchar_t *name) {
  if (!name) return;
  wchar_t *dot = wcsstr(name, L"  ·");
  if (dot) *dot = 0;
  wchar_t st[200];
  _snwprintf(st, 200, L"Доставлено: %s", name);
  st[199] = 0;
  free(name);
  msg_out_status(st);
  show_status(st);
}

/* Enter — отправить, Shift+Enter — новая строка */
static LRESULT CALLBACK MsgEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN && wParam == VK_RETURN && !(GetKeyState(VK_SHIFT) & 0x8000)) {
    msg_send();
    return 0;
  }
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n') && !(GetKeyState(VK_SHIFT) & 0x8000)) return 0;
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    ShowWindow(g_msgOut, SW_HIDE);
    return 0;
  }
  if (msg == WM_CHAR && wParam == 1) { /* Ctrl+A — выделить всё */
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  return CallWindowProcW(g_msgEditOld, hwnd, msg, wParam, lParam);
}

static void msg_out_layout(void) {
  RECT rc;
  GetClientRect(g_msgOut, &rc);
  place_panel_close(g_msgOut);
  int pad = MS_(14), y = PANEL_TITLE_H + MS_(26);
  MoveWindow(g_msgList, pad, y, rc.right - pad * 2, MS_(96), TRUE);
  y += MS_(96) + MS_(26);
  MoveWindow(g_msgEdit, pad, y, rc.right - pad * 2, MS_(84), TRUE);
  y += MS_(84) + MS_(10);
  int bh = MS_(28);
  int x = pad + MS_(110);
  for (int i = 0; i < 3; i++) {
    MoveWindow(GetDlgItem(g_msgOut, ID_MSG_TTL0 + i), x, y, MS_(62), bh, TRUE);
    x += MS_(62) + MS_(4);
  }
  MoveWindow(GetDlgItem(g_msgOut, ID_MSG_SEND), rc.right - pad - MS_(110), y, MS_(110), bh, TRUE);
}

static void msg_out_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Написать коллеге");
  SetBkMode(hdc, TRANSPARENT);
  int pad = MS_(14);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  wchar_t root[MAX_PATH];
  share_root_copy(root);
  RECT t1 = {pad, PANEL_TITLE_H + MS_(4), rc.right - pad, PANEL_TITLE_H + MS_(24)};
  const wchar_t *who = !root[0]       ? L"Сначала задайте общую папку в Настройках"
                       : g_msgToN == 0 ? L"Кому: из коллег сейчас никого в сети"
                                       : L"Кому (кто сейчас в сети):";
  DrawTextW(hdc, who, -1, &t1, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  int y = PANEL_TITLE_H + MS_(26) + MS_(96) + MS_(4);
  RECT t2 = {pad, y, rc.right - pad, y + MS_(20)};
  DrawTextW(hdc, L"Текст (Enter — отправить, Shift+Enter — новая строка):", -1, &t2,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  y += MS_(22) + MS_(84) + MS_(10);
  RECT t3 = {pad, y, pad + MS_(108), y + MS_(28)};
  DrawTextW(hdc, L"Исчезнет через:", -1, &t3, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  RECT st = {pad, rc.bottom - MS_(32), rc.right - pad, rc.bottom - MS_(8)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, g_msgStatus[0] ? COL_SAGE : COL_MUTED);
  DrawTextW(hdc,
            g_msgStatus[0] ? g_msgStatus
                           : L"Не шифруется — пароли так не передавайте",
            -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK MsgOutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    msg_out_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (id >= ID_MSG_TTL0 && id < ID_MSG_TTL0 + 3) {
      g_msgTtl = id - ID_MSG_TTL0;
      msg_ttl_buttons();
      SetFocus(g_msgEdit);
    }
    if (id == ID_MSG_SEND) msg_send();
    if (id == ID_MSG_LIST && HIWORD(wParam) == LBN_DBLCLK) SetFocus(g_msgEdit);
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

static void msg_compose(const wchar_t *toId, const wchar_t *toName) {
  lstrcpynW(g_msgReplyId, toId ? toId : L"", 96);
  lstrcpynW(g_msgReplyName, toName ? toName : L"", 128);
  if (!g_msgOut) {
    msg_scale();
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgOutProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadMsgOut";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    int w = MS_(460), h = PANEL_TITLE_H + MS_(26 + 96 + 26 + 84 + 10 + 28 + 44);
    g_msgOut = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadMsgOut", L"Написать коллеге",
                               WS_POPUP | WS_BORDER | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0, w, h,
                               NULL, NULL, g_inst, NULL);
    if (!g_msgOut) return;
    round_corners(g_msgOut);
    mk_btn(g_msgOut, L"×", ID_PANEL_CLOSE);
    g_msgList = CreateWindowExW(0, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY |
                                    LBS_NOINTEGRALHEIGHT,
                                0, 0, 100, 100, g_msgOut, (HMENU)(INT_PTR)ID_MSG_LIST, g_inst, NULL);
    g_msgEdit = CreateWindowExW(0, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE |
                                    ES_AUTOVSCROLL | ES_WANTRETURN,
                                0, 0, 100, 100, g_msgOut, (HMENU)(INT_PTR)ID_MSG_TEXT, g_inst, NULL);
    SendMessageW(g_msgEdit, EM_SETLIMITTEXT, MSG_MAXLEN, 0);
    g_msgEditOld = (WNDPROC)SetWindowLongPtrW(g_msgEdit, GWLP_WNDPROC, (LONG_PTR)MsgEditProc);
    if (g_fontBody) {
      SendMessageW(g_msgList, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
      SendMessageW(g_msgEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    }
    for (int i = 0; i < 3; i++) mk_btn(g_msgOut, kMsgTtlName[i], ID_MSG_TTL0 + i);
    mk_btn(g_msgOut, L"Отправить", ID_MSG_SEND);
    msg_ttl_buttons();
    msg_out_layout();
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_msgOut, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                 SWP_NOZORDER);
  }
  g_msgStatus[0] = 0;
  if (g_msgReplyId[0]) SendMessageW(g_msgList, LB_SETCURSEL, (WPARAM)-1, 0); /* отвечаем — выбран он */
  msg_refresh_list();
  ShowWindow(g_msgOut, SW_SHOWNORMAL);
  SetForegroundWindow(g_msgOut);
  SetFocus(g_msgEdit);
}

static void msg_compose_show(void) { msg_compose(NULL, NULL); }
