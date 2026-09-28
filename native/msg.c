/* ---- Чат с коллегами ------------------------------------------------------

   С 2026.09.23.68 — обычный чат вместо исчезающих сообщений: «Ещё» → «Чат»
   (или меню значка у часов). Слева — коллеги: кто сейчас в сети (тот же
   список, что у нард) и с кем уже была переписка, у кого непрочитанное — число.
   Справа — переписка пузырями (мои — справа), внизу поле: Enter — отправить,
   Shift+Enter — новая строка. Двойной щелчок по пузырю — скопировать текст.

   Пришло сообщение, а чат не открыт на этом человеке — одно окошко в углу
   экрана («Сообщение от …», последнее сообщение и сколько ещё
   непрочитанных). Новое сообщение не открывает второе окошко, а обновляет
   это — окошки больше не ложатся друг на друга. «Открыть чат» — к
   переписке; само прячется через 12 секунд, после того как человек тронул
   мышь или клавиатуру (пока мышь над ним — ждёт).

   Как ходит — как раньше, через общую папку (старые версии курсора
   понимают эти файлы и показывают их своим окошком):

     CursorPad-Msg\<кому>\<сообщение>.txt   (from, name, ttl, text)

   Программа получателя забирает файлы раз в секунду, пока окно чата
   открыто, иначе — раз в 3 секунды, и сразу удаляет. Отправитель видит
   «доставлено», когда файла не стало. Не забрали за сутки — удаляется.

   История — у каждого на своём компьютере: chat\<коллега>.txt рядом с
   заметками («#name», строки «время  1/0 (моё/его)  текст»), в памяти —
   последние 400 сообщений на человека. «Очистить» стирает переписку с
   выбранным. Файлы в папке не шифруются — пароли так не передавать. */

#define MSG_SUB L"CursorPad-Msg"
#define WM_MSG_IN (WM_APP + 19)   /* lParam — MsgIn*, освобождает получатель */
#define WM_MSG_SENT (WM_APP + 20) /* lParam — «id\tвремя» доставленного (malloc) */
#define MSG_MAXLEN 2000
/* ID_MSG_* — в cursorpad.c: «Отправить» и «Открыть чат» рисуются синими */
#define TIMER_MSG_TICK 1

typedef struct {
  wchar_t from[96], name[128];
  int ttl;
  wchar_t *text;
} MsgIn;

/* ---- переписка ----------------------------------------------------------- */

#define CH_MAXC 64  /* собеседников */
#define CH_MAXM 400 /* сообщений на собеседника в памяти */

typedef struct {
  ULONGLONG t; /* FILETIME */
  BYTE out;    /* 1 — моё */
  BYTE state;  /* моё: 0 — из истории, 1 — отправлено, ждёт; 2 — доставлено */
  wchar_t *text;
} ChMsg;

typedef struct {
  wchar_t id[96], name[128];
  ChMsg *m;
  int n, cap;
  int unread;
  BOOL loaded;
} ChContact;

static ChContact g_ch[CH_MAXC];
static int g_chN;
static int g_chSel = -1;     /* выбранный собеседник (индекс в g_ch) */
static int g_chOrder[CH_MAXC]; /* строка списка → собеседник */
static int g_chOrderN;
static int g_chScroll;       /* на сколько точек переписка прокручена вверх */
static BOOL g_chScanned;     /* истории с диска перечислены */
static volatile LONG g_msgFast; /* окно чата открыто — забирать почаще */

static float g_msgS = 1.0f;
static HWND g_msgOut, g_msgList, g_msgEdit, g_msgView;
static WNDPROC g_msgEditOld;
static wchar_t g_msgStatus[200];
static BOOL g_msgInSlots[8]; /* [0] — окошко пришедшего на экране (автообновление ждёт) */
static HWND g_msgToast;
static BOOL g_msgThread;

/* ждут доставки: поток смотрит, не забрали ли файл */
#define MSG_MAXPEND 32
static struct {
  wchar_t path[SHARE_PATH];
  wchar_t id[96];
  ULONGLONG t, t0;
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

/* «user  ·  pc» → «user» */
static void msg_short_name(const wchar_t *name, wchar_t *out, int cap) {
  lstrcpynW(out, name, cap);
  wchar_t *dot = wcsstr(out, L"  ·");
  if (dot) *dot = 0;
}

/* ---- поток: забрать пришедшее, проверить доставку отправленного ---------- */
static DWORD WINAPI msg_thread(LPVOID param) {
  (void)param;
  int beat = 0;
  for (;;) {
    Sleep(1000);
    /* окно чата открыто — каждую секунду, иначе раз в три */
    if (!g_msgFast && ++beat % 3) continue;
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
          wchar_t *raw = msg_field_dup(t, L"text");
          if (m && raw) {
            nd_field(t, L"from", m->from, 96);
            nd_field(t, L"name", m->name, 128);
            m->ttl = 60;
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
      BOOL old = GetTickCount64() - g_msgPend[i].t0 > 24ull * 3600 * 1000;
      if (gone || old) {
        if (gone && g_hwnd) {
          wchar_t *key = (wchar_t *)malloc(160 * sizeof(wchar_t));
          if (key) {
            _snwprintf(key, 160, L"%s\t%llu", g_msgPend[i].id, (unsigned long long)g_msgPend[i].t);
            key[159] = 0;
            if (!PostMessageW(g_hwnd, WM_MSG_SENT, 0, (LPARAM)key)) free(key);
          }
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

/* ---- история на диске -------------------------------------------------------- */

static BOOL ch_path(const wchar_t *id, wchar_t *out) {
  if (!g_dataDir[0] || !id[0]) return FALSE;
  _snwprintf(out, MAX_PATH, L"%s\\chat", g_dataDir);
  out[MAX_PATH - 1] = 0;
  CreateDirectoryW(out, NULL);
  size_t l = wcslen(out);
  _snwprintf(out + l, MAX_PATH - l, L"\\%s.txt", id);
  out[MAX_PATH - 1] = 0;
  for (wchar_t *c = out + l + 1; *c; c++)
    if (wcschr(L"\\/:*?\"<>|", *c)) *c = L'_';
  return TRUE;
}

static void ch_append_file(const wchar_t *path, const wchar_t *line) {
  int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
  if (n <= 1) return;
  char *u = (char *)malloc((size_t)n);
  if (!u) return;
  WideCharToMultiByte(CP_UTF8, 0, line, -1, u, n, NULL, NULL);
  HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD w = 0;
    WriteFile(f, u, (DWORD)(n - 1), &w, NULL);
    CloseHandle(f);
  }
  free(u);
}

static void ch_push(ChContact *c, ULONGLONG t, BOOL out, BYTE state, wchar_t *text /* забирает */) {
  if (c->n == c->cap) {
    if (c->cap >= CH_MAXM) { /* старое — вон из памяти (в файле остаётся) */
      free(c->m[0].text);
      memmove(c->m, c->m + 1, (size_t)(c->n - 1) * sizeof(ChMsg));
      c->n--;
    } else {
      int nc = c->cap ? c->cap * 2 : 32;
      if (nc > CH_MAXM) nc = CH_MAXM;
      ChMsg *nm = (ChMsg *)realloc(c->m, (size_t)nc * sizeof(ChMsg));
      if (!nm) {
        free(text);
        return;
      }
      c->m = nm;
      c->cap = nc;
    }
  }
  ChMsg *m = &c->m[c->n++];
  m->t = t;
  m->out = (BYTE)(out ? 1 : 0);
  m->state = state;
  m->text = text;
}

static void ch_load(ChContact *c) {
  if (c->loaded) return;
  c->loaded = TRUE;
  wchar_t p[MAX_PATH];
  if (!ch_path(c->id, p)) return;
  wchar_t *t = share_read(p);
  if (!t) return;
  wchar_t *pp = t, *line;
  while ((line = share_next_line(&pp)) != NULL) {
    if (line[0] == L'#') {
      wchar_t *f[3];
      if (share_split(line, f, 3) >= 2 && !wcscmp(f[0], L"#name") && !c->name[0]) lstrcpynW(c->name, f[1], 128);
      continue;
    }
    wchar_t *f[3];
    if (share_split(line, f, 3) < 3) continue;
    ULONGLONG tm = wcstoull(f[0], NULL, 10);
    wchar_t *txt = msg_unescape(f[2]);
    if (txt) ch_push(c, tm, f[1][0] == L'1', 0, txt);
  }
  free(t);
}

static void ch_save_line(ChContact *c, const ChMsg *m) {
  wchar_t p[MAX_PATH];
  if (!ch_path(c->id, p)) return;
  BOOL fresh = GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES;
  if (fresh && c->name[0]) {
    wchar_t h[200];
    _snwprintf(h, 200, L"#name\t%s\n", c->name);
    h[199] = 0;
    ch_append_file(p, h);
  }
  wchar_t *esc = msg_escape(m->text);
  if (!esc) return;
  size_t cap = wcslen(esc) + 64;
  wchar_t *line = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (line) {
    _snwprintf(line, cap, L"%llu\t%d\t%s\n", (unsigned long long)m->t, m->out ? 1 : 0, esc);
    line[cap - 1] = 0;
    ch_append_file(p, line);
    free(line);
  }
  free(esc);
}

/* собеседник по id; нет — завести (name может быть пустым) */
static ChContact *ch_get(const wchar_t *id, const wchar_t *name) {
  for (int i = 0; i < g_chN; i++)
    if (!wcscmp(g_ch[i].id, id)) {
      if (name && name[0]) lstrcpynW(g_ch[i].name, name, 128);
      return &g_ch[i];
    }
  if (g_chN >= CH_MAXC) return NULL;
  ChContact *c = &g_ch[g_chN++];
  memset(c, 0, sizeof(*c));
  lstrcpynW(c->id, id, 96);
  if (name) lstrcpynW(c->name, name, 128);
  return c;
}

/* с кем уже переписывались — из папки chat (один раз) */
static void ch_scan(void) {
  if (g_chScanned || !g_dataDir[0]) return;
  g_chScanned = TRUE;
  wchar_t pat[MAX_PATH];
  _snwprintf(pat, MAX_PATH, L"%s\\chat\\*.txt", g_dataDir);
  WIN32_FIND_DATAW fd;
  HANDLE f = FindFirstFileW(pat, &fd);
  if (f == INVALID_HANDLE_VALUE) return;
  do {
    wchar_t id[96];
    lstrcpynW(id, fd.cFileName, 96);
    wchar_t *dot = wcsrchr(id, L'.');
    if (dot) *dot = 0;
    ChContact *c = ch_get(id, NULL);
    if (c) ch_load(c); /* заодно имя из «#name» */
  } while (FindNextFileW(f, &fd));
  FindClose(f);
}

static BOOL ch_online(const wchar_t *id) {
  NdPoll *pl = g_nd.poll;
  if (!pl) return FALSE;
  for (int i = 0; i < pl->nOnline; i++)
    if (!wcscmp(pl->onId[i], id)) return TRUE;
  return FALSE;
}

static int ch_unread_total(void) {
  int n = 0;
  for (int i = 0; i < g_chN; i++) n += g_ch[i].unread;
  return n;
}

/* ---- окошко «пришло сообщение» — одно на всех -------------------------------- */

#define MSGIN_W 340
typedef struct {
  wchar_t fromId[96], fromName[128];
  wchar_t *text;
  int more; /* ещё непрочитанных */
  DWORD arrived;
  BOOL started;
  int leftMs;
  DWORD lastTick;
} MsgToast;
static MsgToast g_toast;

static void msg_scale(void) {
  HDC s = GetDC(NULL);
  g_msgS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
  if (s) ReleaseDC(NULL, s);
}

static void toast_hide(void) {
  if (g_msgToast) ShowWindow(g_msgToast, SW_HIDE);
  g_msgInSlots[0] = FALSE;
  free(g_toast.text);
  g_toast.text = NULL;
}

static void toast_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Сообщение");
  SetBkMode(hdc, TRANSPARENT);
  int pad = MS_(16);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  wchar_t from[220], nm[128];
  msg_short_name(g_toast.fromName[0] ? g_toast.fromName : g_toast.fromId, nm, 128);
  if (g_toast.more > 0) _snwprintf(from, 220, L"от %s · и ещё непрочитанных: %d", nm, g_toast.more);
  else _snwprintf(from, 220, L"от %s", nm);
  from[219] = 0;
  RECT fr = {pad, PANEL_TITLE_H + MS_(4), rc.right - pad, PANEL_TITLE_H + MS_(22)};
  DrawTextW(hdc, from, -1, &fr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (g_fontBody) SelectObject(hdc, g_fontBody);
  SetTextColor(hdc, COL_INK);
  RECT t = {pad, PANEL_TITLE_H + MS_(26), rc.right - pad, rc.bottom - MS_(50)};
  if (g_toast.text)
    DrawTextW(hdc, g_toast.text, -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS);
}

static void msg_open_chat(const wchar_t *id);

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
    toast_paint(hwnd, mem);
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
    if (wParam == TIMER_MSG_TICK && IsWindowVisible(hwnd)) {
      DWORD now = GetTickCount();
      if (!g_toast.started) {
        /* отсчёт — с первого движения человека после прихода: вдруг он отошёл */
        LASTINPUTINFO li = {sizeof(li), 0};
        if (GetLastInputInfo(&li) && (int)(li.dwTime - g_toast.arrived) > 0) {
          g_toast.started = TRUE;
          g_toast.lastTick = now;
        }
        return 0;
      }
      POINT pt;
      RECT wr;
      GetCursorPos(&pt);
      GetWindowRect(hwnd, &wr);
      if (!PtInRect(&wr, pt)) g_toast.leftMs -= (int)(now - g_toast.lastTick);
      g_toast.lastTick = now;
      if (g_toast.leftMs <= 0) toast_hide();
    }
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_MSG_REPLY) {
      wchar_t fid[96];
      lstrcpynW(fid, g_toast.fromId, 96);
      toast_hide();
      msg_open_chat(fid);
    }
    if (id == ID_MSG_DISMISS || id == ID_PANEL_CLOSE) toast_hide();
    return 0;
  }
  case WM_CLOSE:
    toast_hide();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void toast_show(const wchar_t *fromId, const wchar_t *fromName, const wchar_t *text) {
  msg_scale();
  if (!g_msgToast) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgInProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadMsgIn";
    RegisterClassExW(&wc);
    g_msgToast = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"CursorPadMsgIn", L"Сообщение",
                                 WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, 0, 0, MS_(MSGIN_W), MS_(200), NULL, NULL,
                                 g_inst, NULL);
    if (!g_msgToast) return;
    round_corners(g_msgToast);
    mk_btn(g_msgToast, L"×", ID_PANEL_CLOSE);
    mk_btn(g_msgToast, L"Открыть чат", ID_MSG_REPLY);
    mk_btn(g_msgToast, L"Закрыть", ID_MSG_DISMISS);
    SetTimer(g_msgToast, TIMER_MSG_TICK, 250, NULL);
  }
  lstrcpynW(g_toast.fromId, fromId, 96);
  lstrcpynW(g_toast.fromName, fromName, 128);
  free(g_toast.text);
  g_toast.text = _wcsdup(text ? text : L"");
  g_toast.more = ch_unread_total() - 1;
  g_toast.arrived = GetTickCount();
  g_toast.started = FALSE;
  g_toast.leftMs = 12000;
  /* высота — по тексту, но не больше 8 строк */
  int W = MS_(MSGIN_W), textW = W - MS_(32), textH = MS_(20);
  HDC dc = GetDC(NULL);
  if (dc) {
    HGDIOBJ of = g_fontBody ? SelectObject(dc, g_fontBody) : NULL;
    RECT c = {0, 0, textW, 0};
    DrawTextW(dc, g_toast.text, -1, &c, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_CALCRECT);
    textH = c.bottom;
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    if (textH > tm.tmHeight * 8) textH = tm.tmHeight * 8;
    if (of) SelectObject(dc, of);
    ReleaseDC(NULL, dc);
  }
  int H = PANEL_TITLE_H + MS_(26) + textH + MS_(58);
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  SetWindowPos(g_msgToast, HWND_TOPMOST, wa.right - W - MS_(16), wa.bottom - H - MS_(16), W, H,
               SWP_NOACTIVATE);
  place_panel_close(g_msgToast);
  RECT rc;
  GetClientRect(g_msgToast, &rc);
  int bh = MS_(28), by = rc.bottom - MS_(12) - bh;
  MoveWindow(GetDlgItem(g_msgToast, ID_MSG_DISMISS), rc.right - MS_(16) - MS_(84), by, MS_(84), bh, TRUE);
  MoveWindow(GetDlgItem(g_msgToast, ID_MSG_REPLY), rc.right - MS_(16) - MS_(84) - MS_(8) - MS_(116), by, MS_(116), bh,
             TRUE);
  g_msgInSlots[0] = TRUE;
  InvalidateRect(g_msgToast, NULL, FALSE);
  ShowWindow(g_msgToast, SW_SHOWNOACTIVATE);
}

/* ---- окно чата ------------------------------------------------------------------ */

static void msg_status(const wchar_t *s) {
  lstrcpynW(g_msgStatus, s, 200);
  if (g_msgOut) InvalidateRect(g_msgOut, NULL, FALSE);
}

static BOOL chat_active_on(const wchar_t *id) {
  return g_msgOut && IsWindowVisible(g_msgOut) && !IsIconic(g_msgOut) && GetForegroundWindow() == g_msgOut &&
         g_chSel >= 0 && !wcscmp(g_ch[g_chSel].id, id);
}

/* список слева: сперва кто в сети, потом остальные; у кого непрочитанное — число */
static void msg_refresh_list(void) {
  NdPoll *pl = g_nd.poll;
  if (pl)
    for (int i = 0; i < pl->nOnline; i++) ch_get(pl->onId[i], pl->onName[i]);
  if (!g_msgOut || !g_msgList) return;
  g_chOrderN = 0;
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < g_chN; i++)
      if ((pass == 0) == (ch_online(g_ch[i].id) != 0)) g_chOrder[g_chOrderN++] = i;
  SendMessageW(g_msgList, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_msgList, LB_RESETCONTENT, 0, 0);
  int keep = -1;
  for (int k = 0; k < g_chOrderN; k++) {
    ChContact *c = &g_ch[g_chOrder[k]];
    wchar_t nm[128], row[200];
    msg_short_name(c->name[0] ? c->name : c->id, nm, 128);
    if (c->unread) _snwprintf(row, 200, L"%s %s   (%d)", ch_online(c->id) ? L"●" : L"○", nm, c->unread);
    else _snwprintf(row, 200, L"%s %s", ch_online(c->id) ? L"●" : L"○", nm);
    row[199] = 0;
    SendMessageW(g_msgList, LB_ADDSTRING, 0, (LPARAM)row);
    if (g_chOrder[k] == g_chSel) keep = k;
  }
  if (keep >= 0) SendMessageW(g_msgList, LB_SETCURSEL, keep, 0);
  SendMessageW(g_msgList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_msgList, NULL, TRUE);
  EnableWindow(GetDlgItem(g_msgOut, ID_MSG_SEND), g_chSel >= 0);
  EnableWindow(GetDlgItem(g_msgOut, ID_MSG_CLEAR), g_chSel >= 0 && g_ch[g_chSel].n > 0);
  InvalidateRect(g_msgOut, NULL, FALSE);
}

static void chat_select(int ci) {
  g_chSel = ci;
  g_chScroll = 0;
  if (ci >= 0) {
    ch_load(&g_ch[ci]);
    g_ch[ci].unread = 0;
  }
  msg_refresh_list();
  if (g_msgView) InvalidateRect(g_msgView, NULL, FALSE);
}

static void msg_send(void) {
  if (g_chSel < 0) {
    msg_status(L"Выберите, кому — слева");
    return;
  }
  ChContact *c = &g_ch[g_chSel];
  int len = GetWindowTextLengthW(g_msgEdit);
  if (len <= 0) return;
  wchar_t *text = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
  if (!text) return;
  GetWindowTextW(g_msgEdit, text, len + 1);
  /* пустые строки по краям — долой */
  size_t tl = wcslen(text);
  while (tl && (text[tl - 1] == L'\n' || text[tl - 1] == L'\r' || text[tl - 1] == L' ')) text[--tl] = 0;
  if (!tl) {
    free(text);
    return;
  }
  wchar_t *esc = msg_escape(text);
  if (!esc) {
    free(text);
    return;
  }
  wchar_t root[MAX_PATH], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  wchar_t myId[96], user[128], pc[64];
  nd_myid(myId, 96, NULL, 0);
  share_me(user, pc);
  BOOL ok = FALSE;
  ULONGLONG now = ft_now();
  if (root[0] && msg_dir(root, c->id, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s-%llu-%lu.txt", dir, myId, (unsigned long long)now,
               (unsigned long)(GetTickCount() ^ GetCurrentProcessId()));
    path[SHARE_PATH - 1] = 0;
    size_t cap = wcslen(esc) + 600;
    wchar_t *body = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (body) {
      /* ttl — для старых версий: они показывают сообщение своим окошком */
      _snwprintf(body, cap, L"from\t%s\nname\t%s  ·  %s\nttl\t300\ntext\t%s\n", myId, user, pc, esc);
      body[cap - 1] = 0;
      ok = share_write(path, body);
      free(body);
    }
  }
  free(esc);
  if (!ok) {
    free(text);
    msg_status(root[0] ? L"Не получилось записать в общую папку — она доступна?" : L"Не задана общая папка (Настройки)");
    return;
  }
  ch_push(c, now, TRUE, 1, text);
  ch_save_line(c, &c->m[c->n - 1]);
  msg_lock();
  if (g_msgPendN == MSG_MAXPEND) g_msgPend[0] = g_msgPend[--g_msgPendN]; /* самое старое больше не ждём */
  lstrcpynW(g_msgPend[g_msgPendN].path, path, SHARE_PATH);
  lstrcpynW(g_msgPend[g_msgPendN].id, c->id, 96);
  g_msgPend[g_msgPendN].t = now;
  g_msgPend[g_msgPendN].t0 = GetTickCount64();
  g_msgPendN++;
  msg_unlock();
  SetWindowTextW(g_msgEdit, L"");
  g_chScroll = 0;
  msg_status(L"");
  InvalidateRect(g_msgView, NULL, FALSE);
  msg_refresh_list();
  SetFocus(g_msgEdit);
}

/* пришло от потока: файл забрали — «доставлено» */
static void msg_on_sent(wchar_t *key) {
  if (!key) return;
  wchar_t *tab = wcschr(key, L'\t');
  if (tab) {
    *tab = 0;
    ULONGLONG t = wcstoull(tab + 1, NULL, 10);
    for (int i = 0; i < g_chN; i++) {
      if (wcscmp(g_ch[i].id, key)) continue;
      for (int k = g_ch[i].n - 1; k >= 0; k--)
        if (g_ch[i].m[k].out && g_ch[i].m[k].t == t) {
          g_ch[i].m[k].state = 2;
          break;
        }
    }
  }
  free(key);
  if (g_msgView) InvalidateRect(g_msgView, NULL, FALSE);
}

/* пришло сообщение: в переписку; чат не открыт на этом человеке — окошко */
static void msg_show_in(MsgIn *m) {
  if (!m) return;
  ch_scan();
  ChContact *c = ch_get(m->from[0] ? m->from : L"?", m->name);
  if (c) {
    ch_load(c);
    wchar_t *text = _wcsdup(m->text ? m->text : L"");
    if (text) {
      ch_push(c, ft_now(), FALSE, 0, text);
      ch_save_line(c, &c->m[c->n - 1]);
    }
    if (chat_active_on(c->id)) {
      g_chScroll = 0;
      InvalidateRect(g_msgView, NULL, FALSE);
    } else {
      c->unread++;
      toast_show(c->id, c->name, m->text);
      MessageBeep(MB_ICONASTERISK);
      if (g_msgOut && IsWindowVisible(g_msgOut) && c - g_ch == g_chSel) InvalidateRect(g_msgView, NULL, FALSE);
    }
    msg_refresh_list();
  }
  msg_wipe(m->text);
  free(m->text);
  free(m);
}

/* ---- переписка: пузыри -------------------------------------------------------- */

#define CH_HITS 64
static struct {
  RECT r;
  int k;
} g_chHit[CH_HITS];
static int g_chHitN;

static void ch_time_text(ULONGLONG t, wchar_t *out, int cap) {
  FILETIME ft, lf;
  ft.dwLowDateTime = (DWORD)t;
  ft.dwHighDateTime = (DWORD)(t >> 32);
  SYSTEMTIME s, now;
  FileTimeToLocalFileTime(&ft, &lf);
  FileTimeToSystemTime(&lf, &s);
  GetLocalTime(&now);
  if (s.wYear == now.wYear && s.wMonth == now.wMonth && s.wDay == now.wDay)
    _snwprintf(out, cap, L"%02d:%02d", s.wHour, s.wMinute);
  else _snwprintf(out, cap, L"%02d.%02d %02d:%02d", s.wDay, s.wMonth, s.wHour, s.wMinute);
  out[cap - 1] = 0;
}

static void view_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  HBRUSH bg = CreateSolidBrush(blend_rgb(COL_PAPER, COL_LINE, 60));
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  SetBkMode(hdc, TRANSPARENT);
  g_chHitN = 0;
  if (g_chSel < 0) {
    if (g_fontUi) SelectObject(hdc, g_fontUi);
    SetTextColor(hdc, COL_MUTED);
    DrawTextW(hdc, L"Выберите коллегу слева", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  ChContact *c = &g_ch[g_chSel];
  if (!c->n) {
    if (g_fontUi) SelectObject(hdc, g_fontUi);
    SetTextColor(hdc, COL_MUTED);
    DrawTextW(hdc, L"Переписки пока нет — напишите ниже", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  int pad = MS_(10), maxW = (rc.right - pad * 2) * 72 / 100, gap = MS_(8);
  int y = rc.bottom - pad + g_chScroll;
  COLORREF mineFill = blend_rgb(COL_PAPER, RGB(70, 130, 220), 60), theirFill = COL_PAPER;
  for (int k = c->n - 1; k >= 0 && y > 0; k--) {
    ChMsg *m = &c->m[k];
    if (g_fontBody) SelectObject(hdc, g_fontBody);
    RECT calc = {0, 0, maxW - MS_(20), 0};
    DrawTextW(hdc, m->text, -1, &calc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_CALCRECT);
    int tw = calc.right, th = calc.bottom;
    wchar_t meta[64];
    ch_time_text(m->t, meta, 40);
    if (m->out && m->state == 1) wcscat(meta, L" · отправлено");
    else if (m->out && m->state == 2) wcscat(meta, L" · доставлено");
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SIZE ms;
    GetTextExtentPoint32W(hdc, meta, (int)wcslen(meta), &ms);
    int bw = (tw > ms.cx ? tw : ms.cx) + MS_(20), bh = th + ms.cy + MS_(14);
    int top = y - bh;
    int x = m->out ? rc.right - pad - bw : pad;
    if (top < rc.bottom && top + bh > 0) {
      RECT b = {x, top, x + bw, top + bh};
      fill_round_rect(hdc, b, m->out ? mineFill : theirFill, COL_LINE, MS_(10));
      RECT tr = {x + MS_(10), top + MS_(6), x + bw - MS_(10), top + MS_(6) + th};
      if (g_fontBody) SelectObject(hdc, g_fontBody);
      SetTextColor(hdc, COL_INK);
      DrawTextW(hdc, m->text, -1, &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
      RECT mr = {x + MS_(10), tr.bottom + MS_(2), x + bw - MS_(10), top + bh - MS_(4)};
      if (g_fontSmall) SelectObject(hdc, g_fontSmall);
      SetTextColor(hdc, m->out && m->state == 2 ? COL_SAGE : COL_MUTED);
      DrawTextW(hdc, meta, -1, &mr, (m->out ? DT_RIGHT : DT_LEFT) | DT_SINGLELINE);
      if (g_chHitN < CH_HITS) {
        g_chHit[g_chHitN].r = b;
        g_chHit[g_chHitN].k = k;
        g_chHitN++;
      }
    }
    y = top - gap;
  }
  /* выше пузырей — ничего: дальше не прокручивать */
  if (y > 0 && g_chScroll > 0) {
    g_chScroll -= y;
    if (g_chScroll < 0) g_chScroll = 0;
  }
}

static LRESULT CALLBACK ChatViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    view_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_MOUSEWHEEL: {
    int d = GET_WHEEL_DELTA_WPARAM(wParam);
    g_chScroll += d / 120 * MS_(40);
    if (g_chScroll < 0) g_chScroll = 0;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }
  case WM_LBUTTONDBLCLK: { /* двойной щелчок по пузырю — текст в буфер */
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    for (int i = 0; i < g_chHitN; i++)
      if (PtInRect(&g_chHit[i].r, pt) && g_chSel >= 0 && g_chHit[i].k < g_ch[g_chSel].n) {
        clipboard_set(g_ch[g_chSel].m[g_chHit[i].k].text);
        msg_status(L"Текст сообщения скопирован");
        break;
      }
    return 0;
  }
  case WM_LBUTTONDOWN:
    SetFocus(g_msgEdit);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
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
    g_msgFast = 0;
    return 0;
  }
  if (msg == WM_CHAR && wParam == 1) { /* Ctrl+A — выделить всё */
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  if (msg == WM_MOUSEWHEEL && g_msgView) return SendMessageW(g_msgView, msg, wParam, lParam);
  return CallWindowProcW(g_msgEditOld, hwnd, msg, wParam, lParam);
}

#define CH_LISTW 200
static void msg_out_layout(void) {
  RECT rc;
  GetClientRect(g_msgOut, &rc);
  place_panel_close(g_msgOut);
  int pad = MS_(12), top = PANEL_TITLE_H + MS_(8), lw = MS_(CH_LISTW);
  int bottom = rc.bottom - MS_(26);
  MoveWindow(g_msgList, pad, top + MS_(22), lw, bottom - top - MS_(22) - MS_(36), TRUE);
  MoveWindow(GetDlgItem(g_msgOut, ID_MSG_CLEAR), pad, bottom - MS_(30), lw, MS_(28), TRUE);
  int rx = pad + lw + MS_(10), rw = rc.right - rx - pad;
  int ih = MS_(64);
  MoveWindow(g_msgView, rx, top + MS_(30), rw, bottom - top - MS_(30) - ih - MS_(8), TRUE);
  MoveWindow(g_msgEdit, rx, bottom - ih, rw - MS_(110), ih, TRUE);
  MoveWindow(GetDlgItem(g_msgOut, ID_MSG_SEND), rc.right - pad - MS_(102), bottom - ih, MS_(102), ih, TRUE);
  /* текст пузырей переносится по новой ширине — перерисовать всю переписку
     (с 2026.09.23.70: раньше при растягивании старые пузыри оставались на
     месте и новые ложились поверх) */
  if (g_msgView) InvalidateRect(g_msgView, NULL, FALSE);
}

static void msg_out_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Чат");
  SetBkMode(hdc, TRANSPARENT);
  int pad = MS_(12), top = PANEL_TITLE_H + MS_(8), lw = MS_(CH_LISTW), rx = pad + lw + MS_(10);
  wchar_t root[MAX_PATH];
  share_root_copy(root);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  RECT l1 = {pad, top, pad + lw, top + MS_(20)};
  DrawTextW(hdc, root[0] ? L"● в сети   ○ не в сети" : L"Задайте общую папку в Настройках", -1, &l1,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  /* над перепиской — с кем и в сети ли */
  if (g_chSel >= 0) {
    ChContact *c = &g_ch[g_chSel];
    wchar_t nm[128], line[260];
    msg_short_name(c->name[0] ? c->name : c->id, nm, 128);
    BOOL on = ch_online(c->id);
    if (g_fontUi) SelectObject(hdc, g_fontUi);
    SetTextColor(hdc, COL_INK);
    RECT h1 = {rx, top, rc.right - pad, top + MS_(22)};
    _snwprintf(line, 260, L"%s", nm);
    line[259] = 0;
    DrawTextW(hdc, line, -1, &h1, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SIZE s;
    GetTextExtentPoint32W(hdc, line, (int)wcslen(line), &s);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, on ? COL_SAGE : COL_MUTED);
    RECT h2 = {rx + s.cx + MS_(10), top + MS_(3), rc.right - pad, top + MS_(22)};
    DrawTextW(hdc, on ? L"в сети" : L"не в сети — получит, когда запустит CursorPad", -1, &h2,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  RECT st = {pad, rc.bottom - MS_(24), rc.right - pad, rc.bottom - MS_(4)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, g_msgStatus[0] ? COL_SAGE : COL_MUTED);
  DrawTextW(hdc,
            g_msgStatus[0] ? g_msgStatus
                           : L"Enter — отправить, Shift+Enter — новая строка · двойной щелчок по сообщению — "
                             L"скопировать · не шифруется, пароли так не передавайте",
            -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void chat_clear(void) {
  if (g_chSel < 0) return;
  ChContact *c = &g_ch[g_chSel];
  wchar_t nm[128], q[220];
  msg_short_name(c->name[0] ? c->name : c->id, nm, 128);
  _snwprintf(q, 220, L"Стереть переписку с %s на этом компьютере?", nm);
  q[219] = 0;
  if (MessageBoxW(g_msgOut, q, L"Чат", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  for (int i = 0; i < c->n; i++) {
    msg_wipe(c->m[i].text);
    free(c->m[i].text);
  }
  c->n = 0;
  wchar_t p[MAX_PATH];
  if (ch_path(c->id, p)) DeleteFileW(p);
  InvalidateRect(g_msgView, NULL, FALSE);
  msg_refresh_list();
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
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = MS_(560);
    mm->ptMinTrackSize.y = MS_(360);
    return 0;
  }
  case WM_SIZE:
    msg_out_layout();
    /* и кнопки: скруглённые углы берут фон окна — без этого после растягивания
       оставались чёрные уголки */
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    return 0;
  case WM_ACTIVATE:
    /* вернулись в чат — открытая переписка прочитана */
    if (LOWORD(wParam) != WA_INACTIVE && g_chSel >= 0 && g_ch[g_chSel].unread) {
      g_ch[g_chSel].unread = 0;
      msg_refresh_list();
    }
    break;
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
    if (id == ID_PANEL_CLOSE) {
      ShowWindow(hwnd, SW_HIDE);
      g_msgFast = 0;
    }
    if (id == ID_MSG_SEND) msg_send();
    if (id == ID_MSG_CLEAR) chat_clear();
    if (id == ID_MSG_LIST && (HIWORD(wParam) == LBN_SELCHANGE || HIWORD(wParam) == LBN_DBLCLK)) {
      int k = (int)SendMessageW(g_msgList, LB_GETCURSEL, 0, 0);
      if (k >= 0 && k < g_chOrderN) chat_select(g_chOrder[k]);
      SetFocus(g_msgEdit);
    }
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      ShowWindow(hwnd, SW_HIDE);
      g_msgFast = 0;
    }
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    g_msgFast = 0;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void msg_open_chat(const wchar_t *id) {
  ch_scan();
  if (!g_msgOut) {
    msg_scale();
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW; /* растянули — перерисовать целиком, а не только новую полосу */
    wc.lpfnWndProc = MsgOutProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadMsgOut";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    WNDCLASSEXW vc;
    memset(&vc, 0, sizeof(vc));
    vc.cbSize = sizeof(vc);
    vc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW; /* пузыри привязаны к низу — при смене размера все сдвигаются */
    vc.lpfnWndProc = ChatViewProc;
    vc.hInstance = g_inst;
    vc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    vc.lpszClassName = L"CursorPadChatView";
    RegisterClassExW(&vc);
    int w = MS_(760), h = MS_(540);
    g_msgOut = CreateWindowExW(WS_EX_APPWINDOW, L"CursorPadMsgOut", L"Чат — CursorPad",
                               WS_POPUP | WS_BORDER | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX, 0,
                               0, w, h, NULL, NULL, g_inst, NULL);
    if (!g_msgOut) return;
    round_corners(g_msgOut);
    mk_btn(g_msgOut, L"×", ID_PANEL_CLOSE);
    g_msgList = CreateWindowExW(0, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY |
                                    LBS_NOINTEGRALHEIGHT,
                                0, 0, 100, 100, g_msgOut, (HMENU)(INT_PTR)ID_MSG_LIST, g_inst, NULL);
    g_msgView = CreateWindowExW(0, L"CursorPadChatView", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 0, 100, 100,
                                g_msgOut, NULL, g_inst, NULL);
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
    mk_btn(g_msgOut, L"Отправить", ID_MSG_SEND);
    mk_btn(g_msgOut, L"Очистить переписку", ID_MSG_CLEAR);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_msgOut, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                 SWP_NOZORDER);
    msg_out_layout();
  }
  g_msgStatus[0] = 0;
  msg_refresh_list();
  int ci = -1;
  if (id && id[0]) {
    ChContact *c = ch_get(id, NULL);
    if (c) ci = (int)(c - g_ch);
  }
  if (ci < 0 && g_chSel < 0) { /* сразу — у кого непрочитанное, иначе первый в сети */
    for (int i = 0; i < g_chN && ci < 0; i++)
      if (g_ch[i].unread) ci = i;
    if (ci < 0 && g_chOrderN) ci = g_chOrder[0];
  }
  if (ci >= 0) chat_select(ci);
  else if (g_chSel >= 0) chat_select(g_chSel);
  g_msgFast = 1;
  if (g_msgToast && IsWindowVisible(g_msgToast) && g_chSel >= 0 && !wcscmp(g_toast.fromId, g_ch[g_chSel].id))
    toast_hide();
  ShowWindow(g_msgOut, IsIconic(g_msgOut) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_msgOut);
  SetFocus(g_msgEdit);
}

static void msg_compose_show(void) { msg_open_chat(NULL); }
