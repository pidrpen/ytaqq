/* Included from cursorpad.c — search, OCR picker, autostart, crash restore. */

#define ID_ENG_WIKI 116
#define ID_ENG_DDG 117
#define ID_ENG_YA 118
#define ID_ENG_AI 123
#define ID_AUTOSTART 119
#define ID_ANS_COPY 120
#define ID_ANS_NOTES 121
#define ID_ANS_CLOSE 122
#define TIMER_CURSOR_KEEP 6
#define WM_SEARCH_DONE (WM_APP + 8)
#define WM_OCR_DONE (WM_APP + 9)
#define WM_SHOW_PAD (WM_APP + 10)
#define ANS_W 360
#define ANS_H 280

static HWND g_answerEdit;
static BOOL g_picking = FALSE;
static BOOL g_pickDrag = FALSE;
static POINT g_pick0, g_pick1;
static wchar_t g_lockPath[MAX_PATH];
static volatile LONG g_netBusy = 0;

static void mark_cursor_dirty(BOOL on) {
  if (!g_lockPath[0] && g_dataDir[0])
    _snwprintf(g_lockPath, MAX_PATH, L"%s\\cursor.lock", g_dataDir);
  if (!g_lockPath[0]) return;
  if (on) {
    HANDLE h = CreateFileW(g_lockPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  } else {
    DeleteFileW(g_lockPath);
  }
}

static void restore_if_stale_lock(void) {
  if (!g_dataDir[0]) return;
  _snwprintf(g_lockPath, MAX_PATH, L"%s\\cursor.lock", g_dataDir);
  if (GetFileAttributesW(g_lockPath) != INVALID_FILE_ATTRIBUTES) {
    SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, 0);
    DeleteFileW(g_lockPath);
    g_cursorOn = FALSE;
  }
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *ex) {
  (void)ex;
  SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, 0);
  if (g_lockPath[0]) DeleteFileW(g_lockPath);
  return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL autostart_get(void) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                    KEY_READ, &k) != ERROR_SUCCESS)
    return FALSE;
  wchar_t val[MAX_PATH + 8];
  DWORD sz = sizeof(val), type = 0;
  LONG er = RegQueryValueExW(k, L"CursorPad", NULL, &type, (LPBYTE)val, &sz);
  RegCloseKey(k);
  return er == ERROR_SUCCESS && type == REG_SZ && val[0] != 0;
}

static void autostart_set(BOOL on) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                    KEY_SET_VALUE, &k) != ERROR_SUCCESS)
    return;
  if (!on) {
    RegDeleteValueW(k, L"CursorPad");
  } else {
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 4];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    _snwprintf(cmd, MAX_PATH + 4, L"\"%s\"", exe);
    RegSetValueExW(k, L"CursorPad", 0, REG_SZ, (const BYTE *)cmd,
                   (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
  }
  RegCloseKey(k);
}

static unsigned hex4(const char *p) {
  unsigned v = 0;
  for (int i = 0; i < 4; i++) {
    char c = p[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
    else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
  }
  return v;
}

static const char *skip_ws(const char *p) {
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
  return p;
}

static BOOL parse_json_string(const char **pp, wchar_t *out, int cap) {
  const char *p = skip_ws(*pp);
  if (*p != '"') return FALSE;
  p++;
  int o = 0;
  while (*p && *p != '"') {
    unsigned cp;
    if (*p == '\\') {
      p++;
      if (*p == 'u' && p[1] && p[2] && p[3] && p[4]) {
        cp = hex4(p + 1);
        p += 5;
      } else if (*p == 'n') {
        cp = L'\n';
        p++;
      } else if (*p == 't') {
        cp = L' ';
        p++;
      } else if (*p == '"') {
        cp = L'"';
        p++;
      } else if (*p == '\\') {
        cp = L'\\';
        p++;
      } else if (*p == '/') {
        cp = L'/';
        p++;
      } else {
        cp = (unsigned char)*p++;
      }
    } else {
      unsigned char c = (unsigned char)*p;
      if (c < 0x80) {
        cp = c;
        p++;
      } else if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = ((c & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        p += 2;
      } else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        p += 3;
      } else {
        p++;
        cp = L'?';
      }
    }
    if (o + 1 < cap) out[o++] = (wchar_t)cp;
  }
  if (*p == '"') p++;
  out[o] = 0;
  *pp = p;
  return TRUE;
}

static BOOL json_field_string(const char *json, const char *key, wchar_t *out, int cap) {
  char pat[96];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = json;
  while ((p = strstr(p, pat))) {
    const char *n = p + strlen(pat);
    n = skip_ws(n);
    if (*n == ':') {
      n++;
      return parse_json_string(&n, out, cap);
    }
    p += 1;
  }
  return FALSE;
}

static BOOL opensearch_title(const char *json, wchar_t *out, int cap) {
  const char *p = strchr(json, '[');
  if (!p) return FALSE;
  p++;
  wchar_t tmp[256];
  if (!parse_json_string(&p, tmp, 256)) return FALSE;
  p = skip_ws(p);
  if (*p == ',') p++;
  p = skip_ws(p);
  if (*p != '[') return FALSE;
  p++;
  return parse_json_string(&p, out, cap);
}

static BOOL http_get(const wchar_t *host, const wchar_t *path, char **out, DWORD *outlen) {
  *out = NULL;
  *outlen = 0;
  HINTERNET ses = WinHttpOpen(
      L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) CursorPad/1.0",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return FALSE;
  WinHttpSetTimeouts(ses, 4000, 5000, 8000, 18000);
  HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    return FALSE;
  }
  HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return FALSE;
  }
#ifdef WINHTTP_OPTION_DECOMPRESSION
  {
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));
  }
#endif
  WinHttpAddRequestHeaders(req,
                           L"Accept: text/html,application/json;q=0.9,*/*;q=0.8\r\n"
                           L"Accept-Language: ru,en;q=0.8\r\n",
                           (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
  BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (ok) ok = WinHttpReceiveResponse(req, NULL);
  if (!ok) {
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return FALSE;
  }
  DWORD cap = 0, n = 0;
  char *buf = NULL;
  for (;;) {
    DWORD avail = 0, got = 0;
    if (!WinHttpQueryDataAvailable(req, &avail)) break;
    if (!avail) break;
    if (n + avail + 1 > cap) {
      DWORD nc = n + avail + 4096;
      if (nc > 400000) nc = 400000;
      char *nb = (char *)realloc(buf, nc);
      if (!nb) break;
      buf = nb;
      cap = nc;
    }
    DWORD room = cap - n - 1;
    if (avail > room) avail = room;
    if (!avail) break;
    if (!WinHttpReadData(req, buf + n, avail, &got) || !got) break;
    n += got;
    if (n >= 399000) break;
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  if (!buf) return FALSE;
  buf[n] = 0;
  *out = buf;
  *outlen = n;
  return n > 0;
}

static BOOL http_get_url(const char *url, char **out, DWORD *outlen) {
  if (!url || strncmp(url, "https://", 8) != 0) return FALSE;
  const char *p = url + 8;
  const char *slash = strchr(p, '/');
  char host8[256];
  wchar_t host[256], path[2400];
  if (!slash) {
    lstrcpynA(host8, p, 256);
    MultiByteToWideChar(CP_UTF8, 0, host8, -1, host, 256);
    return http_get(host, L"/", out, outlen);
  }
  int hn = (int)(slash - p);
  if (hn <= 0 || hn >= 255) return FALSE;
  memcpy(host8, p, (size_t)hn);
  host8[hn] = 0;
  MultiByteToWideChar(CP_UTF8, 0, host8, -1, host, 256);
  MultiByteToWideChar(CP_UTF8, 0, slash, -1, path, 2400);
  return http_get(host, path, out, outlen);
}

static void trim_extract(wchar_t *s, int maxc) {
  int n = (int)wcslen(s);
  if (n > maxc && maxc > 2) {
    s[maxc] = 0;
    wchar_t *dot = wcsrchr(s, L'.');
    if (dot && (dot - s) > 80) {
      dot[1] = 0;
    } else {
      s[maxc - 1] = 0x2026; /* … */
      s[maxc] = 0;
    }
  }
}

static BOOL wiki_summary(const wchar_t *host, const wchar_t *query, wchar_t *out, int cap) {
  char enc[2400];
  url_encode_utf8(query, enc, (int)sizeof(enc));
  wchar_t wenc[2400], path[2800];
  MultiByteToWideChar(CP_UTF8, 0, enc, -1, wenc, 2400);
  _snwprintf(path, 2800,
             L"/w/api.php?action=opensearch&search=%s&limit=1&namespace=0&format=json",
             wenc);
  char *body = NULL;
  DWORD n = 0;
  if (!http_get(host, path, &body, &n) || !body) return FALSE;
  wchar_t title[256] = {0};
  BOOL ok = opensearch_title(body, title, 256);
  free(body);
  if (!ok || !title[0]) return FALSE;
  for (wchar_t *p = title; *p; p++) {
    if (*p == L' ') *p = L'_';
  }
  char tenc[1200];
  url_encode_utf8(title, tenc, (int)sizeof(tenc));
  wchar_t wt[1200], path2[1600];
  MultiByteToWideChar(CP_UTF8, 0, tenc, -1, wt, 1200);
  _snwprintf(path2, 1600, L"/api/rest_v1/page/summary/%s", wt);
  if (!http_get(host, path2, &body, &n) || !body) return FALSE;
  ok = json_field_string(body, "extract", out, cap);
  if (!ok || !out[0]) ok = json_field_string(body, "description", out, cap);
  free(body);
  return ok && out[0];
}

#include "mini_ai.c"
#include "web_lookup.c"

static BOOL ddg_summary(const wchar_t *query, wchar_t *out, int cap) {
  char enc[2400];
  url_encode_utf8(query, enc, (int)sizeof(enc));
  wchar_t wenc[2400], path[2800];
  MultiByteToWideChar(CP_UTF8, 0, enc, -1, wenc, 2400);
  _snwprintf(path, 2800, L"/?q=%s&format=json&no_html=1&skip_disambig=1&t=cursorpad", wenc);
  char *body = NULL;
  DWORD n = 0;
  if (!http_get(L"api.duckduckgo.com", path, &body, &n) || !body) return FALSE;
  BOOL ok = json_field_string(body, "AbstractText", out, cap);
  if (!ok || !out[0]) ok = json_field_string(body, "Answer", out, cap);
  if (!ok || !out[0]) ok = json_field_string(body, "Definition", out, cap);
  if (!ok || !out[0]) ok = json_field_string(body, "Text", out, cap);
  free(body);
  return ok && out[0];
}

static void compose_answer(const wchar_t *query, wchar_t *out, int cap) {
  wchar_t a[1200] = {0};
  const wchar_t *src = L"";
  if (g_engine == 3 || g_engine < 0 || g_engine > 2) {
    if (internet_ask(query, a, 1200) && a[0]) {
      _snwprintf(out, cap, L"Интернет\r\n\r\n%s", a);
      return;
    }
    src = mini_ai_ask(query, a, 1200);
    _snwprintf(out, cap, L"%s (офлайн)\r\n\r\n%s", src, a);
    return;
  }
  if (g_engine == 1) {
    if (ddg_summary(query, a, 900)) src = L"DuckDuckGo";
    if (!a[0]) {
      if (wiki_summary(L"en.wikipedia.org", query, a, 900) ||
          wiki_summary(L"ru.wikipedia.org", query, a, 900))
        src = L"Википедия";
    }
  } else if (g_engine == 2) {
    if (wiki_summary(L"ru.wikipedia.org", query, a, 900)) src = L"Википедия";
    if (!a[0] && ddg_summary(query, a, 900)) src = L"DuckDuckGo";
    if (!a[0] && wiki_summary(L"en.wikipedia.org", query, a, 900)) src = L"Википедия";
  } else {
    if (wiki_summary(L"ru.wikipedia.org", query, a, 900) ||
        wiki_summary(L"en.wikipedia.org", query, a, 900))
      src = L"Википедия";
    if (!a[0] && ddg_summary(query, a, 900)) src = L"DuckDuckGo";
  }
  if (!a[0]) {
    src = mini_ai_ask(query, a, 1200);
    _snwprintf(out, cap, L"%s\r\n\r\n%s", src, a);
    return;
  }
  trim_extract(a, 720);
  _snwprintf(out, cap, L"%s\r\n\r\n%s", src, a);
}

typedef struct {
  wchar_t q[400];
} SearchJob;

static void show_answer_text(const wchar_t *text);

static DWORD WINAPI search_thread(LPVOID param) {
  SearchJob *job = (SearchJob *)param;
  wchar_t *out = (wchar_t *)malloc(1800 * sizeof(wchar_t));
  if (out) {
    out[0] = 0;
    compose_answer(job->q, out, 1800);
    if (!PostMessageW(g_hwnd, WM_SEARCH_DONE, 0, (LPARAM)out)) free(out);
  }
  free(job);
  InterlockedExchange(&g_netBusy, 0);
  return 0;
}

static void layout_answer(void) {
  if (!g_answer) return;
  RECT rc;
  GetClientRect(g_answer, &rc);
  int pad = 10, btnH = 26, gap = 6;
  int by = rc.bottom - pad - btnH;
  if (g_answerEdit)
    MoveWindow(g_answerEdit, pad, pad, rc.right - pad * 2, by - pad - 4, TRUE);
  int bw = (rc.right - pad * 2 - gap * 2) / 3;
  HWND copy = GetDlgItem(g_answer, ID_ANS_COPY);
  HWND notes = GetDlgItem(g_answer, ID_ANS_NOTES);
  HWND cls = GetDlgItem(g_answer, ID_ANS_CLOSE);
  if (copy) MoveWindow(copy, pad, by, bw, btnH, TRUE);
  if (notes) MoveWindow(notes, pad + bw + gap, by, bw, btnH, TRUE);
  if (cls) MoveWindow(cls, pad + (bw + gap) * 2, by, bw, btnH, TRUE);
}

static LRESULT CALLBACK AnswerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_ANS_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (LOWORD(wParam) == ID_ANS_COPY && g_answerEdit) {
      int len = GetWindowTextLengthW(g_answerEdit);
      wchar_t *w = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
      if (w) {
        GetWindowTextW(g_answerEdit, w, len + 1);
        clipboard_set(w);
        free(w);
        show_status(L"Выжимка скопирована");
      }
    }
    if (LOWORD(wParam) == ID_ANS_NOTES && g_answerEdit) {
      int len = GetWindowTextLengthW(g_answerEdit);
      wchar_t *w = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
      if (w) {
        GetWindowTextW(g_answerEdit, w, len + 1);
        append_notes(w);
        free(w);
        show_status(L"Выжимка в блокноте");
      }
    }
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_SIZE:
    layout_answer();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void create_answer(HWND owner) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = AnswerProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = g_paper;
  wc.lpszClassName = L"CursorPadAnswer";
  RegisterClassExW(&wc);
  g_answer = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"CursorPadAnswer",
      L"Краткая выжимка", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, 0, 0,
      ANS_W, ANS_H, owner, NULL, g_inst, NULL);
  g_answerEdit = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
      0, 0, 100, 100, g_answer, NULL, NULL, NULL);
  HWND copy = CreateWindowExW(0, L"BUTTON", L"Копировать", WS_CHILD | WS_VISIBLE,
                              0, 0, 80, 24, g_answer, (HMENU)(INT_PTR)ID_ANS_COPY, NULL, NULL);
  HWND notes = CreateWindowExW(0, L"BUTTON", L"В блокнот", WS_CHILD | WS_VISIBLE,
                               0, 0, 80, 24, g_answer, (HMENU)(INT_PTR)ID_ANS_NOTES, NULL, NULL);
  HWND cls = CreateWindowExW(0, L"BUTTON", L"Закрыть", WS_CHILD | WS_VISIBLE,
                             0, 0, 80, 24, g_answer, (HMENU)(INT_PTR)ID_ANS_CLOSE, NULL, NULL);
  if (g_fontBody) SendMessageW(g_answerEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  if (g_fontUi) {
    SendMessageW(copy, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(notes, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(cls, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  }
  layout_answer();
}

static void show_answer_text(const wchar_t *text) {
  if (!g_answer || !g_answerEdit) return;
  SetWindowTextW(g_answerEdit, text ? text : L"");
  POINT pt;
  GetCursorPos(&pt);
  int x = pt.x + 18, y = pt.y + 22;
  RECT wa;
  get_work_area(pt, &wa);
  if (x + ANS_W > wa.right) x = wa.right - ANS_W - 8;
  if (y + ANS_H > wa.bottom) y = wa.bottom - ANS_H - 8;
  if (x < wa.left) x = wa.left + 8;
  if (y < wa.top) y = wa.top + 8;
  SetWindowPos(g_answer, HWND_TOPMOST, x, y, ANS_W, ANS_H, SWP_SHOWWINDOW);
  layout_answer();
}

static void start_lookup(const wchar_t *q) {
  if (!q) return;
  while (*q == L' ' || *q == L'\t' || *q == L'\r' || *q == L'\n') q++;
  if (!q[0]) {
    show_status(L"Нечего искать — скопируйте текст");
    return;
  }
  wchar_t wait[440];
  _snwprintf(wait, 440, L"Ищу в интернете «%.80s»…", q);
  show_answer_text(wait);
  if (InterlockedCompareExchange(&g_netBusy, 1, 0) != 0) {
    show_status(L"Поиск уже идёт");
    return;
  }
  SearchJob *job = (SearchJob *)calloc(1, sizeof(SearchJob));
  if (!job) {
    InterlockedExchange(&g_netBusy, 0);
    return;
  }
  lstrcpynW(job->q, q, 400);
  HANDLE th = CreateThread(NULL, 0, search_thread, job, 0, NULL);
  if (th) CloseHandle(th);
  else {
    free(job);
    InterlockedExchange(&g_netBusy, 0);
  }
}

static BOOL save_rect_bmp(int x, int y, int bw, int bh, const wchar_t *path) {
  if (bw < 8) bw = 8;
  if (bh < 8) bh = 8;
  if (bw > 2400) bw = 2400;
  if (bh > 1600) bh = 1600;
  HDC screen = GetDC(NULL);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bm = CreateCompatibleBitmap(screen, bw, bh);
  HGDIOBJ old = SelectObject(mem, bm);
  BitBlt(mem, 0, 0, bw, bh, screen, x, y, SRCCOPY);
  BITMAPINFOHEADER ih;
  memset(&ih, 0, sizeof(ih));
  ih.biSize = sizeof(ih);
  ih.biWidth = bw;
  ih.biHeight = bh;
  ih.biPlanes = 1;
  ih.biBitCount = 24;
  int row = (bw * 3 + 3) & ~3;
  int img = row * bh;
  char *bits = (char *)malloc((size_t)img);
  BOOL ok = FALSE;
  if (bits) {
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader = ih;
    GetDIBits(mem, bm, 0, (UINT)bh, bits, &info, DIB_RGB_COLORS);
    BITMAPFILEHEADER fh;
    memset(&fh, 0, sizeof(fh));
    fh.bfType = 0x4D42;
    fh.bfOffBits = (DWORD)(sizeof(fh) + sizeof(ih));
    fh.bfSize = fh.bfOffBits + (DWORD)img;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
      DWORD wri = 0;
      WriteFile(f, &fh, sizeof(fh), &wri, NULL);
      WriteFile(f, &ih, sizeof(ih), &wri, NULL);
      WriteFile(f, bits, (DWORD)img, &wri, NULL);
      CloseHandle(f);
      ok = TRUE;
    }
    free(bits);
  }
  SelectObject(mem, old);
  DeleteObject(bm);
  DeleteDC(mem);
  ReleaseDC(NULL, screen);
  return ok;
}

static wchar_t *ocr_file_sync(const wchar_t *bmp) {
  wchar_t dir[MAX_PATH], script[MAX_PATH], cmd[1024];
  exe_dir(dir, MAX_PATH);
  _snwprintf(script, MAX_PATH, L"%s\\ocr.ps1", g_dataDir[0] ? g_dataDir : dir);
  _snwprintf(cmd, 1024,
             L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -Path \"%s\"",
             script, bmp);
  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE rd = NULL, wr = NULL;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return NULL;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = wr;
  si.hStdError = wr;
  BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  CloseHandle(wr);
  if (!ok) {
    CloseHandle(rd);
    return NULL;
  }
  WaitForSingleObject(pi.hProcess, 20000);
  char out[4096];
  DWORD n = 0, total = 0;
  while (total < sizeof(out) - 1) {
    if (!ReadFile(rd, out + total, (DWORD)(sizeof(out) - 1 - total), &n, NULL) || !n) break;
    total += n;
  }
  out[total] = 0;
  CloseHandle(rd);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  while (total > 0 && (out[total - 1] == '\n' || out[total - 1] == '\r' || out[total - 1] == ' '))
    out[--total] = 0;
  if (total == 0) return NULL;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, out, (int)total, NULL, 0);
  wchar_t *wtxt = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
  if (!wtxt) return NULL;
  MultiByteToWideChar(CP_UTF8, 0, out, (int)total, wtxt, wlen);
  wtxt[wlen] = 0;
  return wtxt;
}

static DWORD WINAPI ocr_thread(LPVOID param) {
  wchar_t *bmp = (wchar_t *)param;
  wchar_t *text = ocr_file_sync(bmp);
  DeleteFileW(bmp);
  free(bmp);
  if (!PostMessageW(g_hwnd, WM_OCR_DONE, 0, (LPARAM)text) && text) free(text);
  return 0;
}

static void finish_ocr_rect(RECT r) {
  int x = r.left, y = r.top, w = r.right - r.left, h = r.bottom - r.top;
  if (w < 12 || h < 12) {
    show_status(L"OCR: область слишком маленькая");
    return;
  }
  wchar_t tmp[MAX_PATH], *bmp = (wchar_t *)malloc(MAX_PATH * sizeof(wchar_t));
  if (!bmp) return;
  GetTempPathW(MAX_PATH, tmp);
  _snwprintf(bmp, MAX_PATH, L"%scursorpad-ocr.bmp", tmp);
  if (!save_rect_bmp(x, y, w, h, bmp)) {
    free(bmp);
    show_status(L"OCR: не удалось снять экран");
    return;
  }
  show_status(L"OCR: читаю выделенное…");
  HANDLE th = CreateThread(NULL, 0, ocr_thread, bmp, 0, NULL);
  if (th) CloseHandle(th);
  else {
    DeleteFileW(bmp);
    free(bmp);
  }
}

static RECT norm_rect(POINT a, POINT b) {
  RECT r;
  r.left = a.x < b.x ? a.x : b.x;
  r.top = a.y < b.y ? a.y : b.y;
  r.right = a.x > b.x ? a.x : b.x;
  r.bottom = a.y > b.y ? a.y : b.y;
  return r;
}

static LRESULT CALLBACK PickProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_SETCURSOR:
    SetCursor(LoadCursorW(NULL, IDC_CROSS));
    return TRUE;
  case WM_LBUTTONDOWN:
    g_pickDrag = TRUE;
    g_pick0.x = GET_X_LPARAM(lParam);
    g_pick0.y = GET_Y_LPARAM(lParam);
    ClientToScreen(hwnd, &g_pick0);
    g_pick1 = g_pick0;
    SetCapture(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_MOUSEMOVE:
    if (g_pickDrag) {
      g_pick1.x = GET_X_LPARAM(lParam);
      g_pick1.y = GET_Y_LPARAM(lParam);
      ClientToScreen(hwnd, &g_pick1);
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_LBUTTONUP:
    if (g_pickDrag) {
      g_pickDrag = FALSE;
      ReleaseCapture();
      g_pick1.x = GET_X_LPARAM(lParam);
      g_pick1.y = GET_Y_LPARAM(lParam);
      ClientToScreen(hwnd, &g_pick1);
      RECT r = norm_rect(g_pick0, g_pick1);
      g_picking = FALSE;
      DestroyWindow(hwnd);
      g_pick = NULL;
      Sleep(30);
      finish_ocr_rect(r);
    }
    return 0;
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      g_picking = FALSE;
      g_pickDrag = FALSE;
      DestroyWindow(hwnd);
      g_pick = NULL;
      show_status(L"OCR отменён");
    }
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH dim = CreateSolidBrush(RGB(18, 16, 14));
    FillRect(hdc, &rc, dim);
    DeleteObject(dim);
    if (g_pickDrag) {
      POINT a = g_pick0, b = g_pick1;
      ScreenToClient(hwnd, &a);
      ScreenToClient(hwnd, &b);
      RECT sel = norm_rect(a, b);
      HBRUSH hole = CreateSolidBrush(RGB(236, 232, 224));
      FillRect(hdc, &sel, hole);
      DeleteObject(hole);
      HPEN pen = CreatePen(PS_SOLID, 2, COL_SAGE);
      HGDIOBJ old = SelectObject(hdc, pen);
      SelectObject(hdc, GetStockObject(NULL_BRUSH));
      Rectangle(hdc, sel.left, sel.top, sel.right, sel.bottom);
      SelectObject(hdc, old);
      DeleteObject(pen);
    }
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(236, 232, 224));
    if (g_fontUi) SelectObject(hdc, g_fontUi);
    RECT tip = {20, 20, rc.right - 20, 56};
    DrawTextW(hdc, L"Выделите фрагмент · Esc отмена", -1, &tip, DT_LEFT | DT_SINGLELINE);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DESTROY:
    g_pick = NULL;
    g_picking = FALSE;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void start_ocr_pick(void) {
  if (g_picking && g_pick) {
    DestroyWindow(g_pick);
    g_pick = NULL;
    g_picking = FALSE;
    show_status(L"OCR отменён");
    return;
  }
  static BOOL registered = FALSE;
  if (!registered) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PickProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_CROSS);
    wc.hbrBackground = GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"CursorPadPick";
    RegisterClassExW(&wc);
    registered = TRUE;
  }
  int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  g_pickDrag = FALSE;
  g_picking = TRUE;
  g_pick = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"CursorPadPick", L"",
      WS_POPUP, vx, vy, vw, vh, NULL, NULL, g_inst, NULL);
  SetLayeredWindowAttributes(g_pick, 0, 120, LWA_ALPHA);
  ShowWindow(g_pick, SW_SHOW);
  SetForegroundWindow(g_pick);
  SetFocus(g_pick);
  show_status(L"OCR: выделите область");
}

static void update_engine_buttons(void) {
  if (g_btnAi) SetWindowTextW(g_btnAi, g_engine == 3 ? L"● Мини-ИИ (в .exe)" : L"Мини-ИИ (в .exe)");
  if (g_btnWiki) SetWindowTextW(g_btnWiki, g_engine == 0 ? L"● Вики" : L"Вики");
  if (g_btnDdg) SetWindowTextW(g_btnDdg, g_engine == 1 ? L"● DDG" : L"DDG");
  if (g_btnYa) SetWindowTextW(g_btnYa, g_engine == 2 ? L"● Яндекс" : L"Яндекс");
}

static BOOL ensure_single_instance(void) {
  g_mutex = CreateMutexW(NULL, TRUE, L"Local\\CursorPad.SingleInstance");
  if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND w = NULL;
    for (int i = 0; i < 8 && !w; i++) {
      w = FindWindowW(L"CursorPadWindow", NULL);
      if (!w) Sleep(50);
    }
    if (w) {
      PostMessageW(w, WM_SHOW_PAD, 0, 0);
      ShowWindow(w, SW_SHOWNOACTIVATE);
    }
    return FALSE;
  }
  return TRUE;
}
