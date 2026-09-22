/* Included from cursorpad.c — search, OCR picker, autostart, crash restore. */

#define ID_ENG_WIKI 116
#define ID_ENG_DDG 117
#define ID_ENG_YA 118
#define ID_ENG_AI 123
#define ID_ENG_PLM 124
#define ID_ENG_FILES 132
#define ID_FILES_ROOT 133
#define ID_FILES_REFRESH 135
#define ID_FILES_BROWSE 152
#define ID_PLM_SERVER 125
#define ID_PLM_DB 126
#define ID_PLM_USER 128
#define ID_PLM_PASS 129
#define ID_ANS_OPEN 127
#define ID_ANS_LIST 130
#define ID_AUTOSTART 119
#define ID_ANS_COPY 120
#define ID_ANS_NOTES 121
#define ID_ANS_CLOSE 122
#define ID_ANS_SHOW 151 /* 131 was already ID_CLIP */
#define ID_ANS_CARD 152
#define ID_ANS_DRAW 154
#define ID_ANS_FULL 155

#define TIMER_CURSOR_KEEP 6
#define WM_SEARCH_DONE (WM_APP + 8)
#define WM_OCR_DONE (WM_APP + 9)
#define WM_SHOW_PAD (WM_APP + 10)
#define ANS_W 560
#define ANS_H 340

#include <wctype.h>
static void autostart_write(BOOL on, const wchar_t *exe);
static void layout_answer(void);

/* _snwprintf leaves the buffer unterminated when the text does not fit, and
   the caller then hands it to Windows, which keeps reading past the end until
   it stumbles on a zero. Every answer is built through this instead. */
static void ans_printf(wchar_t *out, int cap, const wchar_t *fmt, ...) {
  if (!out || cap <= 0) return;
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(out, (size_t)cap, fmt, ap);
  va_end(ap);
  out[cap - 1] = 0;
}

#include "files.c"
#include "update.c"

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

static void autostart_write(BOOL on, const wchar_t *exe) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                    KEY_SET_VALUE, &k) != ERROR_SUCCESS)
    return;
  if (!on) {
    RegDeleteValueW(k, L"CursorPad");
  } else {
    wchar_t cmd[MAX_PATH + 4];
    _snwprintf(cmd, MAX_PATH + 4, L"\"%s\"", exe);
    RegSetValueExW(k, L"CursorPad", 0, REG_SZ, (const BYTE *)cmd,
                   (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
  }
  RegCloseKey(k);
}

static void autostart_set(BOOL on) {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  autostart_write(on, exe);
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

static void like_escape(const wchar_t *in, wchar_t *out, int cap) {
  int o = 0;
  if (o + 1 < cap) out[o++] = L'%';
  for (; *in && o + 4 < cap - 1; in++) {
    wchar_t c = *in;
    if (c == L'\\' || c == L'%' || c == L'_' || c == L'[') {
      out[o++] = L'\\';
    }
    if (c == L'\'') {
      out[o++] = L'\'';
      out[o++] = L'\'';
    } else {
      out[o++] = c;
    }
  }
  if (o + 1 < cap) out[o++] = L'%';
  out[o] = 0;
}

static void odbc_err(SQLHANDLE h, SQLSMALLINT ht, wchar_t *out, int cap) {
  SQLWCHAR st[8] = {0}, msg[256] = {0};
  SQLINTEGER native = 0;
  SQLSMALLINT n = 0;
  if (SQLGetDiagRecW(ht, h, 1, st, &native, msg, 255, &n) != SQL_SUCCESS) {
    lstrcpynW(out, L"ошибка ODBC", cap);
    return;
  }
  ans_printf(out, cap, L"%s %s", st, msg);
}

static void odbc_brace(const wchar_t *in, wchar_t *out, int cap) {
  int o = 0;
  if (o + 1 < cap) out[o++] = L'{';
  for (; *in && o + 3 < cap; in++) {
    if (*in == L'}') {
      out[o++] = L'}';
      out[o++] = L'}';
    } else {
      out[o++] = *in;
    }
  }
  if (o + 1 < cap) out[o++] = L'}';
  out[o] = 0;
}

static BOOL plm_connect(SQLHENV *env, SQLHDBC *dbc, wchar_t *err, int ecap) {
  *env = SQL_NULL_HENV;
  *dbc = SQL_NULL_HDBC;
  if (!g_sqlUser[0] || !g_sqlPass[0]) {
    lstrcpynW(err, L"Укажите пользователя и пароль SQL (не Windows-учётку).", ecap);
    return FALSE;
  }
  if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, env) != SQL_SUCCESS) {
    lstrcpynW(err, L"ODBC недоступен", ecap);
    return FALSE;
  }
  SQLSetEnvAttr(*env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
  if (SQLAllocHandle(SQL_HANDLE_DBC, *env, dbc) != SQL_SUCCESS) {
    lstrcpynW(err, L"нет соединения ODBC", ecap);
    SQLFreeHandle(SQL_HANDLE_ENV, *env);
    *env = SQL_NULL_HENV;
    return FALSE;
  }
  SQLSetConnectAttr(*dbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)8, 0);
  const wchar_t *drivers[] = {
      L"SQL Server", L"ODBC Driver 17 for SQL Server", L"ODBC Driver 18 for SQL Server",
      NULL};
  wchar_t uid[200], pwd[280], conn[900], outc[640];
  odbc_brace(g_sqlUser, uid, 200);
  odbc_brace(g_sqlPass, pwd, 280);
  SQLSMALLINT outn = 0;
  for (int i = 0; drivers[i]; i++) {
    if (g_plmDatabase[0])
      _snwprintf(conn, 900,
                 L"DRIVER={%s};SERVER=%s;DATABASE=%s;UID=%s;PWD=%s;"
                 L"Trusted_Connection=No;Encrypt=No;TrustServerCertificate=Yes;",
                 drivers[i], g_sqlHost, g_plmDatabase, uid, pwd);
    else
      _snwprintf(conn, 900,
                 L"DRIVER={%s};SERVER=%s;UID=%s;PWD=%s;"
                 L"Trusted_Connection=No;Encrypt=No;TrustServerCertificate=Yes;",
                 drivers[i], g_sqlHost, uid, pwd);
    SQLRETURN r = SQLDriverConnectW(*dbc, NULL, (SQLWCHAR *)conn, SQL_NTS,
                                    (SQLWCHAR *)outc, 640, &outn, SQL_DRIVER_NOPROMPT);
    if (SQL_SUCCEEDED(r)) return TRUE;
  }
  odbc_err(*dbc, SQL_HANDLE_DBC, err, ecap);
  SQLFreeHandle(SQL_HANDLE_DBC, *dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, *env);
  *dbc = SQL_NULL_HDBC;
  *env = SQL_NULL_HENV;
  return FALSE;
}

static void make_plm_link(wchar_t *out, int n, long id) {
  /* No [WindowsUser] — that broke the running client (TertsovDA).
     Without brackets the already-open PLM session takes the object. */
  _snwprintf(out, n, L"pmsz-plm:%s:%s/IO.%ld", g_plmHost, g_plmPort, id);
}

static void open_plm_link(const wchar_t *link) {
  if (!link || !link[0]) return;
  if (wcsncmp(link, L"pmsz-plm:", 9) == 0) {
    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = link;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
      ShellExecuteW(NULL, L"open", link, NULL, NULL, SW_SHOWNORMAL);
    return;
  }
  ShellExecuteW(NULL, L"open", link, NULL, NULL, SW_SHOWNORMAL);
}

static wchar_t g_cardDraw[PLM_LINK]; /* чертёж, найденный для карточки */
/* Поиск уже показал список находок, и техпроцесс обычно в нём есть. Снимаем
   его перед тем, как список сменится карточкой: искать по базе то, что уже
   найдено, — это те самые тридцать семь секунд. */
static wchar_t g_snapName[PLM_ROWS][PLM_COL1];
static long g_snapId[PLM_ROWS];
static int g_snapN;
/* Сколько заняли шаги карточки. Без этого непонятно, что именно медленное:
   база, обратный поиск по ссылкам или обход индекса файлов. */
static ULONGLONG g_cardT0, g_cardTAttrs, g_cardTTp, g_cardTOps, g_cardTUsed, g_cardTFiles;
static ULONGLONG card_lap(void) {
  ULONGLONG now = GetTickCount64();
  ULONGLONG d = now - g_cardT0;
  g_cardT0 = now;
  return d;
}
static BOOL g_fullMode;     /* показывать чертёж рядом с карточкой */
static BOOL g_cardVerbose;  /* показывать ещё и все атрибуты */
static int g_sortCol = -1, g_sortDesc = 0; /* which column the list is ordered by */

/* ---- просмотр чертежа ----------------------------------------------------

   TIFF, в котором лежат сканы чертежей, Windows умеет разбирать сама — в
   gdiplus.dll, которая есть в любой Windows. Поэтому никакой сторонней
   библиотеки и никакого интернета: подгружаем dll по имени, берём пяток
   нужных функций и рисуем. Если dll почему-то нет, просмотр молча
   отключается, а кнопка «Открыть чертёж» работает как работала. */

typedef struct {
  UINT32 version;
  void *debugCallback;
  BOOL suppressBackgroundThread;
  BOOL suppressExternalCodecs;
} GdipStartInput;

typedef int(WINAPI *GdipStartupFn)(ULONG_PTR *, const GdipStartInput *, void *);
typedef int(WINAPI *GdipLoadFn)(const WCHAR *, void **);
typedef int(WINAPI *GdipDisposeFn)(void *);
typedef int(WINAPI *GdipFromHdcFn)(HDC, void **);
typedef int(WINAPI *GdipDeleteGfxFn)(void *);
typedef int(WINAPI *GdipDrawRectFn)(void *, void *, int, int, int, int);
typedef int(WINAPI *GdipDimFn)(void *, UINT *);
typedef int(WINAPI *GdipModeFn)(void *, int);
typedef int(WINAPI *GdipFrameCountFn)(void *, const GUID *, UINT *);
typedef int(WINAPI *GdipSelectFrameFn)(void *, const GUID *, UINT);

static HMODULE g_gdipDll;
static ULONG_PTR g_gdipToken;
static GdipLoadFn p_load;
static GdipDisposeFn p_dispose;
static GdipFromHdcFn p_fromHdc;
static GdipDeleteGfxFn p_delGfx;
static GdipDrawRectFn p_drawRect;
static GdipDimFn p_width, p_height;
static GdipModeFn p_interp;
static GdipFrameCountFn p_frames;
static GdipSelectFrameFn p_selFrame;

/* FrameDimensionPage — по нему листаются страницы многостраничного TIFF */
static const GUID kFramePage = {0x7462dc86,
                                0x6180,
                                0x4c7e,
                                {0x8e, 0x3f, 0xee, 0x73, 0x33, 0xa7, 0xa4, 0x83}};

static BOOL gdip_ready(void) {
  if (g_gdipToken) return TRUE;
  if (!g_gdipDll) g_gdipDll = LoadLibraryW(L"gdiplus.dll");
  if (!g_gdipDll) return FALSE;
  GdipStartupFn start = (GdipStartupFn)GetProcAddress(g_gdipDll, "GdiplusStartup");
  p_load = (GdipLoadFn)GetProcAddress(g_gdipDll, "GdipLoadImageFromFile");
  p_dispose = (GdipDisposeFn)GetProcAddress(g_gdipDll, "GdipDisposeImage");
  p_fromHdc = (GdipFromHdcFn)GetProcAddress(g_gdipDll, "GdipCreateFromHDC");
  p_delGfx = (GdipDeleteGfxFn)GetProcAddress(g_gdipDll, "GdipDeleteGraphics");
  p_drawRect = (GdipDrawRectFn)GetProcAddress(g_gdipDll, "GdipDrawImageRectI");
  p_width = (GdipDimFn)GetProcAddress(g_gdipDll, "GdipGetImageWidth");
  p_height = (GdipDimFn)GetProcAddress(g_gdipDll, "GdipGetImageHeight");
  p_interp = (GdipModeFn)GetProcAddress(g_gdipDll, "GdipSetInterpolationMode");
  p_frames = (GdipFrameCountFn)GetProcAddress(g_gdipDll, "GdipImageGetFrameCount");
  p_selFrame = (GdipSelectFrameFn)GetProcAddress(g_gdipDll, "GdipImageSelectActiveFrame");
  if (!start || !p_load || !p_fromHdc || !p_drawRect || !p_width || !p_height) return FALSE;
  GdipStartInput in;
  memset(&in, 0, sizeof(in));
  in.version = 1;
  if (start(&g_gdipToken, &in, NULL) != 0) {
    g_gdipToken = 0;
    return FALSE;
  }
  return TRUE;
}

static void *g_drawImg;        /* открытый чертёж */
static UINT g_drawPages = 1;   /* сколько в нём страниц */
static UINT g_drawPage;        /* какая показана */
static HWND g_drawPane;

static void draw_close(void) {
  if (g_drawImg && p_dispose) p_dispose(g_drawImg);
  g_drawImg = NULL;
  g_drawPages = 1;
  g_drawPage = 0;
}

static BOOL draw_open(const wchar_t *path) {
  draw_close();
  if (!path || !path[0] || !gdip_ready()) return FALSE;
  if (p_load(path, &g_drawImg) != 0 || !g_drawImg) {
    g_drawImg = NULL;
    return FALSE;
  }
  if (p_frames) {
    UINT n = 0;
    if (p_frames(g_drawImg, &kFramePage, &n) == 0 && n > 0) g_drawPages = n;
  }
  g_drawPage = 0;
  return TRUE;
}

static void draw_page(int delta) {
  if (!g_drawImg || g_drawPages < 2 || !p_selFrame) return;
  int p = (int)g_drawPage + delta;
  if (p < 0) p = (int)g_drawPages - 1;
  if (p >= (int)g_drawPages) p = 0;
  if (p_selFrame(g_drawImg, &kFramePage, (UINT)p) == 0) g_drawPage = (UINT)p;
  if (g_drawPane) InvalidateRect(g_drawPane, NULL, TRUE);
}

static void draw_paint(HWND pane) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(pane, &ps);
  RECT rc;
  GetClientRect(pane, &rc);
  FillRect(dc, &rc, g_paperDark ? g_paperDark : g_paper);
  if (g_drawImg) {
    UINT iw = 0, ih = 0;
    p_width(g_drawImg, &iw);
    p_height(g_drawImg, &ih);
    if (iw && ih) {
      int pw = rc.right - 8, ph = rc.bottom - 8;
      if (pw > 0 && ph > 0) {
        /* вписываем целиком, пропорции не трогаем — чертёж нельзя растягивать */
        double k = (double)pw / iw;
        double k2 = (double)ph / ih;
        if (k2 < k) k = k2;
        int w = (int)(iw * k), h = (int)(ih * k);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        void *gfx = NULL;
        if (p_fromHdc(dc, &gfx) == 0 && gfx) {
          if (p_interp) p_interp(gfx, 7 /* HighQualityBicubic */);
          p_drawRect(gfx, g_drawImg, 4 + (pw - w) / 2, 4 + (ph - h) / 2, w, h);
          if (p_delGfx) p_delGfx(gfx);
        }
      }
    }
  } else {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_MUTED);
    DrawTextW(dc, L"чертёж не открылся", -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  if (g_drawImg && g_drawPages > 1) {
    wchar_t t[64];
    _snwprintf(t, 64, L"страница %u из %u  ·  колесо мыши", g_drawPage + 1, g_drawPages);
    RECT tr = rc;
    tr.top = rc.bottom - 20;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COL_MUTED);
    DrawTextW(dc, t, -1, &tr, DT_CENTER | DT_SINGLELINE);
  }
  EndPaint(pane, &ps);
}

static LRESULT CALLBACK DrawPaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_PAINT) {
    draw_paint(hwnd);
    return 0;
  }
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_MOUSEWHEEL) {
    draw_page(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
    return 0;
  }
  if (msg == WM_LBUTTONDBLCLK && g_cardDraw[0]) {
    ShellExecuteW(NULL, L"open", g_cardDraw, NULL, NULL, SW_SHOWNORMAL);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}


static void fill_plm_list(void) {
  if (!g_answerList) return;
  LVCOLUMNW col;
  memset(&col, 0, sizeof(col));
  col.mask = LVCF_TEXT;
  wchar_t h0[64], h1[64];
  const wchar_t *mark = g_sortDesc ? L" ↓" : L" ↑";
  _snwprintf(h0, 64, L"%s%s", g_resultFiles ? L"файл" : L"ЭСИ", g_sortCol == 0 ? mark : L"");
  _snwprintf(h1, 64, L"%s%s", g_resultFiles ? L"папка" : L"ТП", g_sortCol == 1 ? mark : L"");
  col.pszText = h0;
  SendMessageW(g_answerList, LVM_SETCOLUMNW, 0, (LPARAM)&col);
  col.pszText = h1;
  SendMessageW(g_answerList, LVM_SETCOLUMNW, 1, (LPARAM)&col);
  SendMessageW(g_answerList, LVM_DELETEALLITEMS, 0, 0);
  for (int i = 0; i < g_plmCount; i++) {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = i;
    it.pszText = g_plmEsi[i];
    SendMessageW(g_answerList, LVM_INSERTITEMW, 0, (LPARAM)&it);
    it.iSubItem = 1;
    it.pszText = g_plmTp[i];
    SendMessageW(g_answerList, LVM_SETITEMW, 0, (LPARAM)&it);
  }
  if (g_plmCount > 0) {
    LVITEMW sel;
    memset(&sel, 0, sizeof(sel));
    sel.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    sel.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(g_answerList, LVM_SETITEMSTATE, 0, (LPARAM)&sel);
  }
  InvalidateRect(g_answer, NULL, TRUE);
  ShowWindow(g_answerList, g_plmCount > 0 ? SW_SHOW : SW_HIDE);
  if (g_answerEdit) ShowWindow(g_answerEdit, g_plmCount > 0 ? SW_HIDE : SW_SHOW);
  HWND open = GetDlgItem(g_answer, ID_ANS_OPEN);
  HWND show = GetDlgItem(g_answer, ID_ANS_SHOW);
  /* plain text (a lookup answer, an update report) has nothing to open */
  if (open) {
    SetWindowTextW(open, g_resultFiles ? L"Открыть файл" : L"Открыть PLM");
    ShowWindow(open, g_plmCount > 0 ? SW_SHOW : SW_HIDE);
  }
  if (show) ShowWindow(show, g_resultFiles && g_plmCount > 0 ? SW_SHOW : SW_HIDE);
  HWND card = GetDlgItem(g_answer, ID_ANS_CARD);
  if (card) ShowWindow(card, !g_resultFiles && g_plmCount > 0 ? SW_SHOW : SW_HIDE);
  HWND draw = GetDlgItem(g_answer, ID_ANS_DRAW);
  if (draw) ShowWindow(draw, g_cardDraw[0] ? SW_SHOW : SW_HIDE);
  HWND full = GetDlgItem(g_answer, ID_ANS_FULL);
  if (full) ShowWindow(full, !g_resultFiles && g_plmCount > 0 ? SW_SHOW : SW_HIDE);
  layout_answer();
}

/* Rows are three parallel arrays; with at most PLM_ROWS of them an insertion
   sort that swaps whole rows is simpler than juggling an index permutation. */

static void plm_sort(int col) {
  if (col < 0 || col > 1 || g_plmCount < 2) return;
  wchar_t *tmpA = (wchar_t *)malloc(PLM_COL1 * sizeof(wchar_t));
  wchar_t *tmpB = (wchar_t *)malloc(PLM_COL2 * sizeof(wchar_t));
  wchar_t *tmpL = (wchar_t *)malloc(PLM_LINK * sizeof(wchar_t));
  if (!tmpA || !tmpB || !tmpL) {
    free(tmpA);
    free(tmpB);
    free(tmpL);
    return;
  }
  for (int i = 1; i < g_plmCount; i++) {
    for (int j = i; j > 0; j--) {
      const wchar_t *a = col == 0 ? g_plmEsi[j] : g_plmTp[j];
      const wchar_t *b = col == 0 ? g_plmEsi[j - 1] : g_plmTp[j - 1];
      int cmp = _wcsicmp(a, b);
      if (g_sortDesc) cmp = -cmp;
      if (cmp >= 0) break;
      memcpy(tmpA, g_plmEsi[j], PLM_COL1 * sizeof(wchar_t));
      memcpy(g_plmEsi[j], g_plmEsi[j - 1], PLM_COL1 * sizeof(wchar_t));
      memcpy(g_plmEsi[j - 1], tmpA, PLM_COL1 * sizeof(wchar_t));
      memcpy(tmpB, g_plmTp[j], PLM_COL2 * sizeof(wchar_t));
      memcpy(g_plmTp[j], g_plmTp[j - 1], PLM_COL2 * sizeof(wchar_t));
      memcpy(g_plmTp[j - 1], tmpB, PLM_COL2 * sizeof(wchar_t));
      memcpy(tmpL, g_plmLinks[j], PLM_LINK * sizeof(wchar_t));
      memcpy(g_plmLinks[j], g_plmLinks[j - 1], PLM_LINK * sizeof(wchar_t));
      memcpy(g_plmLinks[j - 1], tmpL, PLM_LINK * sizeof(wchar_t));
      long tmpId = g_plmIds[j];
      g_plmIds[j] = g_plmIds[j - 1];
      g_plmIds[j - 1] = tmpId;
    }
  }
  free(tmpA);
  free(tmpB);
  free(tmpL);
}

static int plm_selected_index(void) {
  if (!g_answerList || g_plmCount <= 0) return -1;
  return (int)SendMessageW(g_answerList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

static void open_plm_selected(void) {
  int i = plm_selected_index();
  if (i < 0 || i >= g_plmCount) {
    show_status(L"Выберите строку в списке");
    return;
  }
  lstrcpynW(g_plmLastLink, g_plmLinks[i], PLM_LINK);
  open_plm_link(g_plmLinks[i]);
  show_status(g_resultFiles ? L"Открываю файл" : L"В текущий клиент PLM");
}

/* Reveal the selected file in Explorer with the row highlighted; if the file
   itself is gone, settle for opening the folder it lived in. */
static void show_in_explorer(const wchar_t *path) {
  if (!path || !path[0]) return;
  size_t n = wcslen(path) + 24;
  wchar_t *args = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (!args) return;
  if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
    _snwprintf(args, n, L"/select,\"%s\"", path);
    args[n - 1] = 0;
    if ((INT_PTR)ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL) > 32) {
      free(args);
      show_status(L"Показано в проводнике");
      return;
    }
  }
  lstrcpynW(args, path, (int)n);
  wchar_t *slash = wcsrchr(args, L'\\');
  if (slash) *slash = 0;
  if ((INT_PTR)ShellExecuteW(NULL, L"open", args, NULL, NULL, SW_SHOWNORMAL) > 32)
    show_status(L"Открыта папка — файла уже нет");
  else
    show_status(L"Проводник не открылся — проверьте путь");
  free(args);
}

static void show_selected_in_explorer(void) {
  int i = plm_selected_index();
  if (i < 0 || i >= g_plmCount) {
    show_status(L"Выберите строку в списке");
    return;
  }
  show_in_explorer(g_plmLinks[i]);
}

/* Обозначение — всё до первой скобки или двоеточия в имени объекта. */
static void plm_designation(const wchar_t *name, wchar_t *out, int cap) {
  int i = 0;
  while (name[i] && i < cap - 1) {
    wchar_t ch = name[i];
    if (ch == L'[' || ch == L'<' || ch == L'(' || ch == L':') break;
    out[i] = ch;
    i++;
  }
  while (i > 0 && out[i - 1] == L' ') i--;
  out[i] = 0;
}

static BOOL plm_lookup(const wchar_t *query, wchar_t *out, int cap) {
  g_plmLastLink[0] = 0;
  g_plmCount = 0;
  wchar_t pat[420];
  like_escape(query, pat, 420);
  wchar_t sql[3800];
  _snwprintf(
      sql, 3800,
      L"SELECT TOP 20 "
      L"CASE WHEN o0.TemplateId=1794 AND ISNULL(o0.ParentId,0)<>0 "
      L"THEN o0.ParentId ELSE o0.InfoObjectId END AS OpenId, "
      L"o0.TemplateId, o0.Name, p.Name "
      L"FROM InfoObjects AS o0 WITH(NOLOCK) "
      L"LEFT JOIN InfoObjects AS p WITH(NOLOCK) ON p.InfoObjectId=o0.ParentId "
      L"WHERE o0.Erased=0 AND ("
      L"o0.TemplateId IN (1767) OR o0.TemplateId IN (20,39) OR o0.TemplateId IN (633) "
      L"OR (o0.TemplateId IN (1794) AND o0.InfoObjectId IN ("
      L"SELECT a0.OwnerId FROM InfoObjectAttributes AS a0 WITH(NOLOCK) "
      L"WHERE a0.DataType=3 AND a0.Outdated=0 AND a0.CollectionElementId IS NULL "
      L"AND a0.NameKeyId=1739 AND a0.Indexed=1 AND a0.BoolValue=1))"
      L") AND (o0.Name LIKE N'%s' ESCAPE '\\' COLLATE Cyrillic_General_CI_AS "
      /* обозначение живёт не в объекте, а его атрибутом — по имени его не найти */
      L"OR EXISTS (SELECT 1 FROM InfoObjectAttributes AS ad WITH(NOLOCK) "
      L"JOIN NameKeys AS nkd WITH(NOLOCK) ON nkd.NameKeyId=ad.NameKeyId "
      L"WHERE ad.OwnerId=o0.InfoObjectId AND ad.Outdated=0 AND nkd.Value=N'Designation' "
      L"AND ad.ShortText LIKE N'%s' ESCAPE '\\' COLLATE Cyrillic_General_CI_AS)) "
      L"OPTION(MAXDOP 0)",
      pat, pat);

  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[280];
  if (!plm_connect(&env, &dbc, err, 280)) {
    ans_printf(out, cap,
               L"PLM\r\n\r\nНе удалось подключиться к %s (логин SQL, не Windows).\r\n%s\r\n\r\n"
               L"В Настройках: сервер UM-SQLSRV, пользователь и пароль SQL, при необходимости база.",
               g_sqlHost, err);
    return FALSE;
  }
  SQLHSTMT st = SQL_NULL_HSTMT;
  SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st);
  SQLRETURN r = SQLExecDirectW(st, (SQLWCHAR *)sql, SQL_NTS);
  if (!SQL_SUCCEEDED(r)) {
    odbc_err(st, SQL_HANDLE_STMT, err, 280);
    ans_printf(out, cap, L"PLM\r\n\r\nЗапрос не выполнился.\r\n%s", err);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
    return FALSE;
  }
  SQLINTEGER openId = 0, tmpl = 0;
  SQLWCHAR nm[200], pnm[200];
  SQLLEN idInd = 0, tmInd = 0, nmInd = 0, pInd = 0;
  SQLBindCol(st, 1, SQL_C_SLONG, &openId, sizeof(openId), &idInd);
  SQLBindCol(st, 2, SQL_C_SLONG, &tmpl, sizeof(tmpl), &tmInd);
  SQLBindCol(st, 3, SQL_C_WCHAR, nm, sizeof(nm), &nmInd);
  SQLBindCol(st, 4, SQL_C_WCHAR, pnm, sizeof(pnm), &pInd);
  int n = 0;
  wchar_t links[1800] = {0};
  size_t linkLen = 0;
  /* та же беда, что и в карточке: SQL_SUCCESS_WITH_INFO обрывал выдачу */
  while (SQL_SUCCEEDED(SQLFetch(st)) && n < 20) {
    long oid = (idInd == SQL_NULL_DATA || openId == 0) ? 0 : (long)openId;
    make_plm_link(g_plmLinks[n], PLM_LINK, oid);
    g_plmIds[n] = oid;
    g_plmTmpl[n] = (int)tmpl;
    g_plmEsi[n][0] = 0;
    g_plmTp[n][0] = 0;
    if (tmpl == 1794) {
      if (pInd > 0 && pnm[0]) lstrcpynW(g_plmEsi[n], (wchar_t *)pnm, PLM_COL1);
      else _snwprintf(g_plmEsi[n], PLM_COL1, L"IO.%ld", oid);
      if (nmInd > 0) lstrcpynW(g_plmTp[n], (wchar_t *)nm, PLM_COL2);
    } else if (nmInd > 0) {
      lstrcpynW(g_plmEsi[n], (wchar_t *)nm, PLM_COL1);
    } else {
      _snwprintf(g_plmEsi[n], PLM_COL1, L"IO.%ld", oid);
    }
    if (!g_plmLastLink[0]) lstrcpynW(g_plmLastLink, g_plmLinks[n], PLM_LINK);
    /* the separator has to fit too, or a long result list walks off the end */
    size_t add = wcslen(g_plmLinks[n]);
    if (linkLen + (n ? 2 : 0) + add + 1 < 1800) {
      if (n) {
        links[linkLen++] = L'\r';
        links[linkLen++] = L'\n';
      }
      memcpy(links + linkLen, g_plmLinks[n], (add + 1) * sizeof(wchar_t));
      linkLen += add;
    }
    n++;
  }
  /* ЭСИ и его техпроцесс приходят из поиска двумя отдельными строками, хотя
     это одно и то же изделие. Сводим их в одну: обозначение у них общее,
     у техпроцесса на конце лишнее «ТП». Запросов для этого не нужно. */
  for (int i = 0; i < n; i++) {
    if (g_plmTmpl[i] != 1794 || !g_plmTp[i][0]) continue;
    wchar_t dtp[PLM_COL1];
    plm_designation(g_plmTp[i], dtp, PLM_COL1);
    size_t dl = wcslen(dtp);
    if (dl > 2 && _wcsicmp(dtp + dl - 2, L"ТП") == 0) dtp[dl - 2] = 0;
    if (!dtp[0]) continue;
    for (int j = 0; j < n; j++) {
      if (j == i || g_plmTmpl[j] == 1794 || g_plmTp[j][0] || !g_plmEsi[j][0]) continue;
      wchar_t des[PLM_COL1];
      plm_designation(g_plmEsi[j], des, PLM_COL1);
      if (_wcsicmp(des, dtp) != 0) continue;
      lstrcpynW(g_plmTp[j], g_plmTp[i], PLM_COL2);
      g_plmTmpl[i] = -1; /* строка техпроцесса больше не нужна отдельно */
      break;
    }
  }
  {
    int w = 0;
    for (int i = 0; i < n; i++) {
      if (g_plmTmpl[i] == -1) continue;
      if (w != i) {
        lstrcpynW(g_plmEsi[w], g_plmEsi[i], PLM_COL1);
        lstrcpynW(g_plmTp[w], g_plmTp[i], PLM_COL2);
        lstrcpynW(g_plmLinks[w], g_plmLinks[i], PLM_LINK);
        g_plmIds[w] = g_plmIds[i];
        g_plmTmpl[w] = g_plmTmpl[i];
      }
      w++;
    }
    n = w;
  }
  g_plmCount = n;
  SQLFreeHandle(SQL_HANDLE_STMT, st);
  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);
  if (n == 0) {
    ans_printf(out, cap, L"PLM\r\n\r\nНичего не найдено по «%.120s».", query);
    return FALSE;
  }
  ans_printf(out, cap,
             L"PLM · %d  (двойной клик — в уже открытый клиент)\r\n\r\n%s", n, links);
  return TRUE;
}

static void load_plm_pref(void) {
  wchar_t path[MAX_PATH];
  if (!g_dataDir[0]) return;
  _snwprintf(path, MAX_PATH, L"%s\\plm.txt", g_dataDir);
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    char buf[800];
    DWORD n = 0;
    ReadFile(h, buf, 799, &n, NULL);
    CloseHandle(h);
    buf[n] = 0;
    char *line = buf;
    while (line && *line) {
      char *nl = strchr(line, '\n');
      if (nl) *nl = 0;
      char *cr = strchr(line, '\r');
      if (cr) *cr = 0;
      char *sp = strchr(line, ' ');
      if (sp) {
        *sp = 0;
        char *val = sp + 1;
        while (*val == ' ') val++;
        if (!strcmp(line, "sql") && val[0])
          MultiByteToWideChar(CP_UTF8, 0, val, -1, g_sqlHost, 96);
        else if (!strcmp(line, "plm") && val[0])
          MultiByteToWideChar(CP_UTF8, 0, val, -1, g_plmHost, 96);
        else if (!strcmp(line, "port") && val[0])
          MultiByteToWideChar(CP_UTF8, 0, val, -1, g_plmPort, 16);
        else if (!strcmp(line, "db") && val[0] && val[0] != '-')
          MultiByteToWideChar(CP_UTF8, 0, val, -1, g_plmDatabase, 96);
        else if (!strcmp(line, "user") && val[0])
          MultiByteToWideChar(CP_UTF8, 0, val, -1, g_sqlUser, 96);
      }
      line = nl ? nl + 1 : NULL;
    }
  }
  _snwprintf(path, MAX_PATH, L"%s\\plm.key", g_dataDir);
  h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD sz = GetFileSize(h, NULL);
  if (sz == INVALID_FILE_SIZE || sz == 0 || sz > 4096) {
    CloseHandle(h);
    return;
  }
  BYTE *blob = (BYTE *)malloc(sz);
  if (!blob) {
    CloseHandle(h);
    return;
  }
  DWORD r = 0;
  ReadFile(h, blob, sz, &r, NULL);
  CloseHandle(h);
  DATA_BLOB in, out;
  in.cbData = r;
  in.pbData = blob;
  out.cbData = 0;
  out.pbData = NULL;
  if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
    int nch = (int)(out.cbData / sizeof(wchar_t));
    if (nch < 1) nch = 1;
    if (nch > 127) nch = 127;
    memcpy(g_sqlPass, out.pbData, (nch) * sizeof(wchar_t));
    g_sqlPass[nch] = 0;
    if (nch > 0 && g_sqlPass[nch - 1] != 0) g_sqlPass[nch] = 0;
    LocalFree(out.pbData);
  }
  free(blob);
}

static void save_plm_pref(void) {
  if (g_plmServer) GetWindowTextW(g_plmServer, g_sqlHost, 96);
  if (g_plmDb) GetWindowTextW(g_plmDb, g_plmDatabase, 96);
  if (g_plmUser) GetWindowTextW(g_plmUser, g_sqlUser, 96);
  if (g_plmPass) {
    wchar_t p[128] = {0};
    GetWindowTextW(g_plmPass, p, 128);
    if (p[0] && wcscmp(p, L"********") != 0) lstrcpynW(g_sqlPass, p, 128);
  }
  wchar_t path[MAX_PATH];
  if (!g_dataDir[0]) return;
  _snwprintf(path, MAX_PATH, L"%s\\plm.txt", g_dataDir);
  char sql[96], plm[96], port[16], db[96], user[96];
  WideCharToMultiByte(CP_UTF8, 0, g_sqlHost, -1, sql, 96, NULL, NULL);
  WideCharToMultiByte(CP_UTF8, 0, g_plmHost, -1, plm, 96, NULL, NULL);
  WideCharToMultiByte(CP_UTF8, 0, g_plmPort, -1, port, 16, NULL, NULL);
  WideCharToMultiByte(CP_UTF8, 0, g_plmDatabase, -1, db, 96, NULL, NULL);
  WideCharToMultiByte(CP_UTF8, 0, g_sqlUser, -1, user, 96, NULL, NULL);
  char buf[400];
  snprintf(buf, sizeof(buf), "sql %s\nplm %s\nport %s\ndb %s\nuser %s\n",
           sql[0] ? sql : "UM-SQLSRV", plm[0] ? plm : "um-splmsrv",
           port[0] ? port : "4450", db[0] ? db : "-", user[0] ? user : "");
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD w = 0;
    WriteFile(h, buf, (DWORD)strlen(buf), &w, NULL);
    CloseHandle(h);
  }
  if (!g_sqlPass[0]) return;
  DATA_BLOB in, out;
  in.cbData = (DWORD)((wcslen(g_sqlPass) + 1) * sizeof(wchar_t));
  in.pbData = (BYTE *)g_sqlPass;
  out.cbData = 0;
  out.pbData = NULL;
  if (!CryptProtectData(&in, L"CursorPad PLM", NULL, NULL, NULL, 0, &out)) return;
  _snwprintf(path, MAX_PATH, L"%s\\plm.key", g_dataDir);
  h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD w = 0;
    WriteFile(h, out.pbData, out.cbData, &w, NULL);
    CloseHandle(h);
  }
  LocalFree(out.pbData);
}


/* ---- PLM: карточка объекта и его техпроцессы ------------------------------

   Схема PLM разобрана по сервису PlmApi (репозиторий wowdroch): значения
   атрибутов лежат в InfoObjectAttributes в колонке по типу (DataType),
   имя атрибута — в NameKeys.Value, а не в самой таблице. Путь к техпроцессам:
   изделие → TechnologicalProcessesCard → TechnologicalProcesses (коллекция)
   → элементы коллекции → сами ТП; актуальность — булев атрибут IsActual.
   Операции: ТП → ActualVersion → MainVariantInVersion → дети варианта.

   Имена атрибутов спрашиваются по названию, а не по номеру: номера в разных
   базах разные, названия одни и те же. */

typedef struct {
  wchar_t *w;
  int cap, len;
} CardOut;

static void card_add(CardOut *c, const wchar_t *fmt, ...) {
  if (!c->w || c->len >= c->cap - 2) return;
  va_list ap;
  va_start(ap, fmt);
  int n = _vsnwprintf(c->w + c->len, (size_t)(c->cap - c->len - 1), fmt, ap);
  va_end(ap);
  if (n < 0 || n > c->cap - c->len - 1) n = c->cap - c->len - 1;
  c->len += n;
  c->w[c->len] = 0;
}

/* Каждый запрос карточки возвращает один и тот же набор колонок, поэтому
   привязка столбцов написана один раз. */
typedef struct {
  long n1, n2, n3;
  wchar_t s1[260];
  wchar_t s2[600];
} CardRow;

static int card_query(SQLHDBC dbc, const wchar_t *sql, CardRow *rows, int max, wchar_t *err,
                      int ecap) {
  if (err && ecap) err[0] = 0;
  SQLHSTMT st = SQL_NULL_HSTMT;
  if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) return -1;
  if (!SQL_SUCCEEDED(SQLExecDirectW(st, (SQLWCHAR *)sql, SQL_NTS))) {
    if (err && ecap) odbc_err(st, SQL_HANDLE_STMT, err, ecap);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return -1;
  }
  SQLINTEGER v1 = 0, v2 = 0, v3 = 0;
  SQLWCHAR t1[260], t2[600];
  SQLLEN i1 = 0, i2 = 0, i3 = 0, i4 = 0, i5 = 0;
  SQLBindCol(st, 1, SQL_C_SLONG, &v1, sizeof(v1), &i1);
  SQLBindCol(st, 2, SQL_C_WCHAR, t1, sizeof(t1), &i2);
  SQLBindCol(st, 3, SQL_C_WCHAR, t2, sizeof(t2), &i3);
  SQLBindCol(st, 4, SQL_C_SLONG, &v2, sizeof(v2), &i4);
  SQLBindCol(st, 5, SQL_C_SLONG, &v3, sizeof(v3), &i5);
  int n = 0;
  /* SQL_SUCCESS_WITH_INFO — это «получилось, но есть замечание», например
     значение подрезано по ширине колонки. Сравнение ровно с SQL_SUCCESS
     обрывало чтение на первой же такой строке, и запрос выглядел пустым. */
  while (n < max && SQL_SUCCEEDED(SQLFetch(st))) {
    rows[n].n1 = (i1 == SQL_NULL_DATA) ? 0 : (long)v1;
    rows[n].n2 = (i4 == SQL_NULL_DATA) ? 0 : (long)v2;
    rows[n].n3 = (i5 == SQL_NULL_DATA) ? 0 : (long)v3;
    rows[n].s1[0] = 0;
    rows[n].s2[0] = 0;
    if (i2 > 0) lstrcpynW(rows[n].s1, (wchar_t *)t1, 260);
    if (i3 > 0) lstrcpynW(rows[n].s2, (wchar_t *)t2, 600);
    n++;
  }
  SQLFreeHandle(SQL_HANDLE_STMT, st);
  return n;
}

/* значение атрибута одним выражением: колонка зависит от DataType */
#define CARD_VALUE_SQL                                                                   \
  L"CASE a.DataType "                                                                    \
  L"WHEN 3 THEN CASE WHEN a.BoolValue=1 THEN N'да' ELSE N'нет' END "                     \
  L"WHEN 2 THEN a.ShortText "                                                            \
  L"WHEN 24 THEN CAST(a.LargeText AS NVARCHAR(400)) "                                    \
  L"WHEN 1 THEN CONVERT(NVARCHAR(64), a.FloatNumber) "                                   \
  L"WHEN 13 THEN CONVERT(NVARCHAR(64), a.IntegerNumber) "                                \
  L"WHEN 32 THEN CONVERT(NVARCHAR(64), a.LongNumber) "                                   \
  L"WHEN 4 THEN CONVERT(NVARCHAR(64), a.LongNumber) "                                    \
  L"WHEN 5 THEN CONVERT(NVARCHAR(64), a.LongNumber) "                                    \
  L"WHEN 33 THEN CONVERT(NVARCHAR(64), a.LongNumber) "                                   \
  L"ELSE N'' END"

/* Внутренние имена атрибутов человеку ничего не говорят. Известные
   переводим, неизвестные показываем как есть — угадывать не нужно, просто
   видно, что это редкий атрибут. */
typedef struct {
  const wchar_t *key;
  const wchar_t *ru;
} CardLabel;

static const CardLabel kCardLabels[] = {
    {L"Name", L"Наименование"},
    {L"Designation", L"Обозначение"},
    {L"TradeDesignation", L"Торговое обозначение"},
    {L"ProductCode", L"Код изделия"},
    {L"ProductClass", L"Класс изделия"},
    {L"Mass", L"Масса"},
    {L"MassMeasureUnit", L"Единица массы"},
    {L"MeasureUnit", L"Единица измерения"},
    {L"VolumeMeasureUnit", L"Единица объёма"},
    {L"Material", L"Материал"},
    {L"MaterialLink", L"Материал"},
    {L"SubstituteMaterial", L"Материал-заменитель"},
    {L"Section", L"Раздел"},
    {L"Format", L"Формат"},
    {L"IsActual", L"Актуальный"},
    {L"MainTP", L"Основной техпроцесс"},
    {L"Through", L"Сквозной"},
    {L"IsGroup", L"Групповой"},
    {L"IsRemoved", L"Удалён"},
    {L"ActualVersion", L"Актуальная версия"},
    {L"PreviousActualVersion", L"Предыдущая версия"},
    {L"VersionNumber", L"Номер версии"},
    {L"MainVariantInVersion", L"Основной вариант"},
    {L"LifeCycleState", L"Состояние"},
    {L"DateTimeOfActualization", L"Актуализирован"},
    {L"ManufactureKind", L"Вид изготовления"},
    {L"KindOfTechnologicalProcess", L"Вид техпроцесса"},
    {L"KindOfStorageItems", L"Вид хранения"},
    {L"KindOfTimeLimitTSClassification", L"Вид нормирования"},
    {L"HideVariantsInTree", L"Прятать варианты"},
    {L"TechnologicalProcesses", L"Техпроцессы"},
    {L"TechnologicalProcessesCard", L"Карточка техпроцессов"},
    {L"TechCompCard", L"Карточка техсостава"},
    {L"TechComposition", L"Техсостав"},
    {L"ActualVersionTechComp", L"Актуальная версия техсостава"},
    {L"ProductConfiguration", L"Конфигурация"},
    {L"ProductConfigurationCode", L"Код конфигурации"},
    {L"ProductInterconnectCard", L"Карта взаимосвязей"},
    {L"ProductPreformsCard", L"Карточка заготовок"},
    {L"PreformConfigurationLink", L"Конфигурация заготовки"},
    {L"Product", L"Изделие"},
    {L"UnifiedProduct", L"Унифицированное изделие"},
    {L"PrimaryUsage", L"Первичное применение"},
    {L"EnterpriseName", L"Предприятие"},
    {L"MainDocument", L"Основной документ"},
    {L"OriginalDocument", L"Подлинник"},
    {L"SpecificationDocument", L"Спецификация"},
    {L"ECNDocument", L"Извещение"},
    {L"TSOperation", L"Операция"},
    {L"Items", L"Позиции"},
    {L"PurchasedKind", L"Вид покупного"},
    {L"TechPurchasedKind", L"Вид покупного (тех.)"},
    {L"ComponentOfTheTooling", L"Составляющая оснастки"},
    {L"ManufacturingSign", L"Признак изготовления"},
    {L"MainPVC", L"Основной ПВС"},
    {L"UnitOfNormalization", L"Единица нормирования"},
    {L"SetupTime", L"Тпз"},
    {L"TimePerPiece", L"Тшт"},
    {L"ZUM_TempCraftName", L"Профессия"},
    {L"ZUM_TempEquipmentName", L"Оборудование"},
    {L"WorkShop", L"Цех"},
    {L"Area", L"Участок"},
};

/* Служебное: счётчики, флаги интерфейса, история. В карточке только мешают. */
/* В операции почти всё — внутренние счётчики и виды отображения. Человеку
   нужны профессия, оборудование и норма; остальное только мешает читать. */
static const wchar_t *kOpHidden[] = {
    L"ActualOperation", L"Area",         L"BlankPages",   L"Operation",
    L"OperationId",     L"OperationInVariantView",        L"OperationView",
    L"StepView",        L"UniquenessOperationNumber",     L"WorkShop",
    L"KOID",            L"WhoAuthor",    L"Number",       L"Name",
    L"ShortFormForOperationContent",      L"TSOperation",  L"OperationNumber",
};

static BOOL card_op_hidden(const wchar_t *key) {
  for (int i = 0; i < (int)(sizeof(kOpHidden) / sizeof(kOpHidden[0])); i++)
    if (_wcsicmp(key, kOpHidden[i]) == 0) return TRUE;
  return FALSE;
}

static const wchar_t *kCardHidden[] = {
    L"BlockInit",      L"CommandSaveOnly",  L"IgnoreStructure", L"SortedManual",
    L"ShowAgreements", L"IndexView",        L"LocalIdCounter",  L"OperationIdCounter",
    L"SourceApplication", L"HistoryList",   L"SignaturesTable", L"LastChanged",
};

static const wchar_t *card_label(const wchar_t *key) {
  for (int i = 0; i < (int)(sizeof(kCardLabels) / sizeof(kCardLabels[0])); i++)
    if (_wcsicmp(key, kCardLabels[i].key) == 0) return kCardLabels[i].ru;
  return NULL;
}

static BOOL card_hidden(const wchar_t *key) {
  for (int i = 0; i < (int)(sizeof(kCardHidden) / sizeof(kCardHidden[0])); i++)
    if (_wcsicmp(key, kCardHidden[i]) == 0) return TRUE;
  return FALSE;
}

/* Нормы времени. Имя атрибута в каждой базе своё, поэтому узнаём их двумя
   путями: по типу данных TIMESPAN и по приметам в имени (Tsht, Tpz, Time,
   Norm, Labor…). Показываем часы с четырьмя знаками и рядом минуты. */
static BOOL card_is_time(const wchar_t *key, long dataType) {
  if (dataType == 5) return TRUE; /* TIMESPAN */
  /* UnitOfNormalization — это в чём меряют («1 минута»), а не сколько.
     Раньше он попадал в приметы по слову norm и складывался в итог. */
  if (_wcsicmp(key, L"UnitOfNormalization") == 0) return FALSE;
  if (_wcsicmp(key, L"SetupTime") == 0 || _wcsicmp(key, L"TimePerPiece") == 0) return TRUE;
  static const wchar_t *marks[] = {L"tsht", L"tpz",  L"tshk",  L"time",
                                   L"normtime", L"labor", L"labour", L"duration"};
  wchar_t low[64];
  lstrcpynW(low, key, 64);
  for (int i = 0; low[i]; i++)
    if (low[i] >= L'A' && low[i] <= L'Z') low[i] = (wchar_t)(low[i] - L'A' + L'a');
  for (int i = 0; i < (int)(sizeof(marks) / sizeof(marks[0])); i++)
    if (wcsstr(low, marks[i])) return TRUE;
  return FALSE;
}

/* В базе норма лежит в минутах, поэтому часы — это оно же на шестьдесят
   делённое. Единицу подтвердил заказчик; если где-то окажется иначе,
   правится здесь одной строкой. */
static BOOL card_time_text(const wchar_t *val, wchar_t *out, int cap, double *mins_out) {
  if (!val || !val[0]) return FALSE;
  wchar_t norm[64];
  int j = 0;
  for (int i = 0; val[i] && j < 62; i++) norm[j++] = val[i] == L',' ? L'.' : val[i];
  norm[j] = 0;
  wchar_t *stop = NULL;
  double mins = wcstod(norm, &stop);
  if (stop == norm) return FALSE; /* не число — пусть показывается как есть */
  _snwprintf(out, cap, L"%.4f ч · %g мин", mins / 60.0, mins);
  out[cap - 1] = 0;
  if (mins_out) *mins_out += mins;
  return TRUE;
}

/* одна строка «название — значение» с выровненной колонкой */
static void card_pair_s(CardOut *c, const wchar_t *pad, const wchar_t *key, const wchar_t *val,
                        long link, long dataType, double *sum) {
  const wchar_t *ru = card_label(key);
  wchar_t name[64], t[64];
  lstrcpynW(name, ru ? ru : key, 64);
  if (val && val[0]) {
    if (card_is_time(key, dataType) && card_time_text(val, t, 64, sum))
      card_add(c, L"%s%-28s %s\r\n", pad, name, t);
    else
      card_add(c, L"%s%-28s %s\r\n", pad, name, val);
  } else if (link) {
    card_add(c, L"%s%-28s → %ld\r\n", pad, name, link);
  }
}

static void card_pair_t(CardOut *c, const wchar_t *pad, const wchar_t *key, const wchar_t *val,
                        long link, long dataType) {
  card_pair_s(c, pad, key, val, link, dataType, NULL);
}

static void card_pair(CardOut *c, const wchar_t *pad, const wchar_t *key, const wchar_t *val,
                      long link) {
  card_pair_s(c, pad, key, val, link, 0, NULL);
}

/* «0.7083 ч · 42.5 мин» одной строкой — для итогов */
static void card_total(CardOut *c, const wchar_t *pad, const wchar_t *title, double mins) {
  card_add(c, L"%s%-28s %.4f ч · %g мин\r\n", pad, title, mins / 60.0, mins);
}

static const wchar_t *card_type_name(long t) {
  switch (t) {
  case 1: return L"число";
  case 2: return L"текст";
  case 3: return L"да/нет";
  case 4: return L"время";
  case 6: return L"ссылка";
  case 8: return L"коллекция";
  case 11: return L"перечисление";
  case 13: return L"целое";
  case 23: return L"составной";
  case 24: return L"текст";
  case 32: return L"длинное целое";
  case 33: return L"дата";
  default: return L"—";
  }
}

#define CARD_ROWS 900
#define CARD_OPS 40 /* для скольких операций тянем все атрибуты */

/* Состав техпроцесса: ТП → ActualVersion → MainVariantInVersion → дети
   варианта. У самой операции содержательное имя часто лежит не на ней, а на
   объекте по ссылке TSOperation, поэтому он подтягивается сразу. */
/* Тпз и Тшт лежат не в самой операции, а в строках её коллекции, поэтому
   обычным запросом атрибутов их не видно. Берём только эти два имени и
   сразу по всем операциям — один запрос вместо похода в каждую. */
/* Тпз и Тшт — составные атрибуты (тип 23): сама строка атрибута значения не
   хранит, число лежит в строках его коллекции, и поля внутри зовутся уже
   по-своему. Поэтому берём не сам атрибут, а его содержимое: на каждую
   операцию возвращаем «имя нормы» и «поле=значение» из её строк. */
static int card_norms(SQLHDBC dbc, const wchar_t *ids, CardRow *rows, wchar_t *err) {
  if (!ids || !ids[0]) return 0;
  wchar_t *sql = (wchar_t *)malloc(3000 * sizeof(wchar_t));
  if (!sql) return 0;
  /* Слепок с того запроса, который заведомо работает: отдельные колонки,
     без склейки имени со значением, и отбор поля Value прямо в запросе —
     остальные поля строки (единица измерения и прочее) не нужны. */
  _snwprintf(sql, 3000,
             L"SELECT TOP 200 a.OwnerId, nk.Value, "
             L"COALESCE(CONVERT(NVARCHAR(64), ea.FloatNumber), "
             L"CONVERT(NVARCHAR(64), ea.IntegerNumber), "
             L"CONVERT(NVARCHAR(64), ea.LongNumber), ea.ShortText, N''), "
             L"0, ea.DataType "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"AND nk.Value IN (N'SetupTime',N'TimePerPiece') "
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
             L"ON ce.AttributeId=a.AttributeId "
             L"JOIN InfoObjectAttributes AS ea WITH(NOLOCK) "
             L"ON ea.CollectionElementId=ce.CollectionElementId "
             L"JOIN NameKeys AS nk2 WITH(NOLOCK) ON nk2.NameKeyId=ea.NameKeyId "
             L"AND nk2.Value=N'Value' "
             L"WHERE a.OwnerId IN (%s) ORDER BY a.OwnerId",
             ids);
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  free(sql);
  return n < 0 ? 0 : n;
}

/* Три попытки угадать, где лежит норма, прошли мимо. Вместо четвёртой
   догадки показываем, какие вообще атрибуты со словами Time, Setup, Piece
   или Norm есть у первой операции — без отбора по устаревшим и по
   коллекциям. По списку сразу видно и настоящее имя, и где оно лежит. */
static void card_probe_time(SQLHDBC dbc, const wchar_t *ids, CardOut *c, CardRow *rows,
                            wchar_t *err) {
  if (!ids || !ids[0]) return;
  wchar_t *sql = (wchar_t *)malloc(3000 * sizeof(wchar_t));
  if (!sql) return;
  _snwprintf(sql, 3000,
             L"SELECT TOP 40 a.OwnerId, nk.Value, "
             L"COALESCE(CONVERT(NVARCHAR(64), a.FloatNumber), "
             L"CONVERT(NVARCHAR(64), a.IntegerNumber), "
             L"CONVERT(NVARCHAR(64), a.LongNumber), a.ShortText, N'—'), "
             L"ISNULL(a.CollectionElementId,0), a.DataType "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"WHERE a.OwnerId IN (%s) AND (nk.Value LIKE N'%%Time%%' "
             L"OR nk.Value LIKE N'%%Setup%%' OR nk.Value LIKE N'%%Piece%%' "
             L"OR nk.Value LIKE N'%%Norm%%') "
             L"ORDER BY a.OwnerId, nk.Value",
             ids);
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  free(sql);
  if (n <= 0) {
    card_add(c, L"  Атрибутов со словами Time, Setup, Piece или Norm у операций нет вовсе.\r\n");
    return;
  }
  card_add(c, L"  ── что похоже на время (объект · имя · значение · тип) ──\r\n");
  for (int i = 0; i < n; i++)
    card_add(c, L"     %ld  %-28s %-12s тип %ld%s\r\n", rows[i].n1, rows[i].s1, rows[i].s2,
             rows[i].n3, rows[i].n2 ? L" (в строке коллекции)" : L"");

  /* Составной атрибут найден, а его содержимое — нет. Значит строки привязаны
     не к тому, что я перебрал. Спрашиваем базу прямо: вот номер атрибута, вот
     его ссылка, сколько строк висит на каждом из них. */
  sql = (wchar_t *)malloc(3000 * sizeof(wchar_t));
  if (!sql) return;
  _snwprintf(sql, 3000,
             L"SELECT TOP 20 a.OwnerId, nk.Value, "
             L"CONVERT(NVARCHAR(32), a.AttributeId) + N' / ссылка ' + "
             L"CONVERT(NVARCHAR(32), ISNULL(a.Link,0)), "
             L"(SELECT COUNT(*) FROM InfoObjectCollectionElements AS c1 WITH(NOLOCK) "
             L"WHERE c1.AttributeId=a.AttributeId), "
             L"(SELECT COUNT(*) FROM InfoObjectCollectionElements AS c2 WITH(NOLOCK) "
             L"WHERE c2.AttributeId=ISNULL(a.Link,0)) "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"WHERE a.OwnerId IN (%s) AND a.DataType=23",
             ids);
  n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  free(sql);
  if (n <= 0) return;
  card_add(c, L"  ── что внутри строк составного атрибута ──\r\n");
  {
    wchar_t *d = (wchar_t *)malloc(3200 * sizeof(wchar_t));
    if (d) {
      _snwprintf(d, 3200,
                 L"SELECT TOP 60 a.OwnerId, nk.Value + N' · ' + nk2.Value, "
                 L"COALESCE(CONVERT(NVARCHAR(64), ea.FloatNumber), "
                 L"CONVERT(NVARCHAR(64), ea.IntegerNumber), "
                 L"CONVERT(NVARCHAR(64), ea.LongNumber), ea.ShortText, "
                 L"N'ссылка ' + CONVERT(NVARCHAR(32), ea.Link), N'—'), "
                 L"ISNULL(ea.Outdated,0), ea.DataType "
                 L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
                 L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
                 L"AND nk.Value IN (N'SetupTime',N'TimePerPiece') "
                 L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
                 L"ON ce.AttributeId=a.AttributeId "
                 L"JOIN InfoObjectAttributes AS ea WITH(NOLOCK) "
                 L"ON ea.CollectionElementId=ce.CollectionElementId "
                 L"JOIN NameKeys AS nk2 WITH(NOLOCK) ON nk2.NameKeyId=ea.NameKeyId "
                 L"WHERE a.OwnerId IN (%s) ORDER BY a.OwnerId, nk2.Value",
                 ids);
      int m = card_query(dbc, d, rows, CARD_ROWS, err, 280);
      free(d);
      if (m > 0)
        for (int i = 0; i < m; i++)
          card_add(c, L"     %ld  %-34s %-14s тип %ld%s\r\n", rows[i].n1, rows[i].s1, rows[i].s2,
                   rows[i].n3, rows[i].n2 ? L" (устаревшее)" : L"");
      else
        card_add(c, L"     строки есть, но полей в них не нашлось\r\n");
    }
  }
  card_add(c, L"  ── где строки составного атрибута ──\r\n");
  for (int i = 0; i < n; i++)
    card_add(c, L"     %ld  %-16s атрибут %s · строк по атрибуту %ld, по ссылке %ld\r\n",
             rows[i].n1, rows[i].s1, rows[i].s2, rows[i].n2, rows[i].n3);
}

static void card_operations(SQLHDBC dbc, long tpId, long verId, CardOut *c, CardRow *rows,
                            wchar_t *err) {
  wchar_t sql[3000];
  if (!verId) {
    card_add(c, L"У ТП %ld нет ссылки ActualVersion — состав показать неоткуда.\r\n", tpId);
    return;
  }
  /* основной вариант версии; если его нет, смотрим детей самой версии */
  _snwprintf(sql, 3000,
             L"SELECT TOP 1 ISNULL(mv.Link,0), N'', N'', 0, 0 "
             L"FROM InfoObjectAttributes AS mv WITH(NOLOCK) "
             L"JOIN NameKeys AS nkm WITH(NOLOCK) ON nkm.NameKeyId=mv.NameKeyId "
             L"WHERE mv.OwnerId=%ld AND mv.Outdated=0 AND nkm.Value=N'MainVariantInVersion'",
             verId);
  int k = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  long parent = (k > 0 && rows[0].n1) ? rows[0].n1 : verId;
  card_add(c, L"СОСТАВ  (версия %ld", verId);
  if (parent != verId) card_add(c, L", вариант %ld", parent);
  card_add(c, L")\r\n");

  _snwprintf(sql, 3000,
             L"SELECT TOP 300 ch.InfoObjectId, ch.Name, "
             L"ISNULL(op.NM, t.NameKey), ISNULL(num.N,0), ISNULL(op.OID,0) "
             L"FROM InfoObjects AS ch WITH(NOLOCK) "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=ch.TemplateId "
             L"OUTER APPLY (SELECT TOP 1 o2.Name AS NM, o2.InfoObjectId AS OID "
             L"FROM InfoObjectAttributes AS ts WITH(NOLOCK) "
             L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=ts.NameKeyId "
             L"AND nkt.Value=N'TSOperation' "
             L"JOIN InfoObjects AS o2 WITH(NOLOCK) ON o2.InfoObjectId=ts.Link "
             L"WHERE ts.OwnerId=ch.InfoObjectId AND ts.Outdated=0) AS op "
             L"OUTER APPLY (SELECT TOP 1 ISNULL(nn.IntegerNumber,0) AS N "
             L"FROM InfoObjectAttributes AS nn WITH(NOLOCK) "
             L"JOIN NameKeys AS nkn WITH(NOLOCK) ON nkn.NameKeyId=nn.NameKeyId "
             L"WHERE nn.OwnerId=ch.InfoObjectId AND nn.Outdated=0 "
             L"AND nkn.Value IN (N'Number',N'OperationNumber',N'LocalId')) AS num "
             L"WHERE ch.ParentId=%ld AND ch.Erased=0 "
             L"ORDER BY num.N, ch.InfoObjectId",
             parent);
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  if (n < 0) {
    card_add(c, L"Состав не прочитался.\r\n%s\r\n", err);
    return;
  }
  if (n == 0) {
    card_add(c, L"Состав пуст: у объекта %ld нет детей.\r\n", parent);
    return;
  }
  card_add(c, L"  операций: %d\r\n\r\n", n);
  /* Нормы времени лежат атрибутами на самой операции либо на объекте по
     ссылке TSOperation — как называются, в разных базах по-разному, поэтому
     показываем всё, что есть со значением, а не угаданный список имён. */
  long ids[CARD_OPS * 2];
  int nid = 0, shown = n < CARD_OPS ? n : CARD_OPS;
  for (int i = 0; i < shown; i++) {
    ids[nid++] = rows[i].n1;
    if (rows[i].n3) ids[nid++] = rows[i].n3;
  }
  /* строки состава переживут второй запрос, сам rows — нет */
  CardRow *ops = (CardRow *)malloc(sizeof(CardRow) * (size_t)(n > 0 ? n : 1));
  if (ops) memcpy(ops, rows, sizeof(CardRow) * (size_t)n);

  double totalMins = 0.0;
  int counted = 0;
  /* нормы держим отдельно: они приходят из коллекций, а не из атрибутов */
  CardRow *nr = (CardRow *)malloc(sizeof(CardRow) * CARD_ROWS);
  int nn = 0;
  wchar_t list[CARD_OPS * 2 * 12];
  {
    int q = 0;
    for (int i = 0; i < nid && q < (int)(sizeof(list) / sizeof(list[0])) - 14; i++)
      q += _snwprintf(list + q, 13, i ? L",%ld" : L"%ld", ids[i]);
    list[q] = 0;
  }
  if (nr && nid) nn = card_norms(dbc, list, nr, err);

  int na = 0;
  if (ops && nid) {
    wchar_t *big = (wchar_t *)malloc(4200 * sizeof(wchar_t));
    if (big) {
      _snwprintf(big, 4200,
                 L"SELECT TOP 800 a.OwnerId, nk.Value, %s, ISNULL(a.Link,0), a.DataType "
                 L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
                 L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
                 L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 "
                 L"AND ISNULL(a.CollectionElementId,0)=0 "
                 L"ORDER BY a.OwnerId, nk.Value",
                 CARD_VALUE_SQL, list);
      na = card_query(dbc, big, rows, CARD_ROWS, err, 280);
      free(big);
    }
  }

  for (int i = 0; i < n; i++) {
    CardRow *o = ops ? &ops[i] : &rows[i];
    wchar_t num[16];
    if (o->n2) _snwprintf(num, 16, L"%ld", o->n2);
    else lstrcpynW(num, L"—", 16);
    const wchar_t *nm = o->s1[0] ? o->s1 : (o->s2[0] ? o->s2 : L"(без имени)");
    card_add(c, L"  %-4s %-40s %ld\r\n", num, nm, o->n1);
    if (o->s2[0] && o->s1[0] && _wcsicmp(o->s2, o->s1) != 0)
      card_add(c, L"       %s\r\n", o->s2);
    if (!ops || i >= shown) continue;
    int printed = 0;
    double opMins = 0.0;
    /* атрибуты операции могут и не прийти — нормы приходят отдельным
       запросом и от них не зависят. Раньше это условие глушило и нормы. */
    for (int k = 0; k < na && printed < 40 && g_cardVerbose; k++) {
      if (rows[k].n1 != o->n1 && rows[k].n1 != o->n3) continue;
      if (!g_cardVerbose) break; /* в обычном виде под операцией только время */
      if (card_hidden(rows[k].s1) || card_op_hidden(rows[k].s1)) continue;
      /* голая ссылка в операции — это номер справочника, читать его незачем */
      if (!rows[k].s2[0]) continue;
      card_pair_s(c, L"       ", rows[k].s1, rows[k].s2, rows[k].n2, rows[k].n3, &opMins);
      printed++;
    }
    for (int k = 0; k < nn; k++) {
      if (nr[k].n1 != o->n1 && nr[k].n1 != o->n3) continue;
      if (!nr[k].s2[0]) continue;
      wchar_t t[64];
      if (!card_time_text(nr[k].s2, t, 64, &opMins)) continue;
      const wchar_t *ru = card_label(nr[k].s1);
      card_add(c, L"       %-28s %s\r\n", ru ? ru : nr[k].s1, t);
      printed++;
    }
    if (!printed && g_cardVerbose) card_add(c, L"       (своих значений нет)\r\n");
    if (opMins > 0.0) {
      card_total(c, L"       ", L"Итого на операцию", opMins);
      totalMins += opMins;
      counted++;
    }
    card_add(c, L"\r\n");
  }
  if (counted) {
    card_add(c, L"  ────────────────────────────────────────\r\n");
    card_total(c, L"  ", L"ИТОГО НА ДЕТАЛЬ", totalMins);
    if (counted < n)
      card_add(c, L"  (сложено по %d операциям из %d)\r\n", counted, n);
  } else if (ops && n > 0) {
    card_add(c, L"  ────────────────────────────────────────\r\n");
    card_add(c, L"  Нормы времени не подхватились (строк из базы: %d).\r\n", nn);
    for (int k = 0; k < nn && k < 4; k++)
      card_add(c, L"     %ld · %s · %s\r\n", nr[k].n1, nr[k].s1, nr[k].s2);
    if (g_cardVerbose) card_probe_time(dbc, list, c, rows, err);
  }
  if (n > shown)
    card_add(c, L"  показано подробно первых %d операций из %d\r\n", shown, n);
  free(ops);
  free(nr);
}


/* Куда этот объект входит по техсоставу. Прямой путь — изделие → TechCompCard
   → ActualVersionTechComp → строка с нужной конфигурацией → коллекция
   TechComposition. Здесь он проходится задом наперёд: ищем строки коллекций
   TechComposition, которые ссылаются на нас, и поднимаемся к их владельцу.
   Удалённые строки (IsRemoved) не в счёт — так же, как в сервисе. */
static void card_where_used(SQLHDBC dbc, long id, CardOut *c, CardRow *rows, wchar_t *err) {
  wchar_t *sql = (wchar_t *)malloc(3000 * sizeof(wchar_t));
  if (!sql) return;
  _snwprintf(
      sql, 3000,
      L"SELECT TOP 100 own.InfoObjectId, own.Name, ISNULL(pr.NM,N''), ISNULL(pr.PID,0), "
      L"own.TemplateId "
      L"FROM InfoObjectAttributes AS ea WITH(NOLOCK) "
      L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
      L"ON ce.CollectionElementId=ea.CollectionElementId AND ce.Outdated=0 "
      L"JOIN InfoObjectAttributes AS la WITH(NOLOCK) ON la.AttributeId=ce.AttributeId "
      L"JOIN NameKeys AS nkl WITH(NOLOCK) ON nkl.NameKeyId=la.NameKeyId "
      L"AND nkl.Value=N'TechComposition' "
      L"JOIN InfoObjects AS own WITH(NOLOCK) ON own.InfoObjectId=la.OwnerId AND own.Erased=0 "
      L"OUTER APPLY (SELECT TOP 1 o3.Name AS NM, o3.InfoObjectId AS PID "
      L"FROM InfoObjectAttributes AS pa WITH(NOLOCK) "
      L"JOIN NameKeys AS nkp WITH(NOLOCK) ON nkp.NameKeyId=pa.NameKeyId AND nkp.Value=N'Product' "
      L"JOIN InfoObjects AS o3 WITH(NOLOCK) ON o3.InfoObjectId=pa.Link "
      L"WHERE pa.OwnerId=own.InfoObjectId AND pa.Outdated=0) AS pr "
      L"WHERE ea.Link=%ld AND ea.Outdated=0 AND ea.DataType=6 "
      L"AND ce.CollectionElementId NOT IN ("
      L"SELECT ioa.CollectionElementId FROM InfoObjectAttributes AS ioa WITH(NOLOCK) "
      L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=ioa.NameKeyId "
      L"WHERE nk.Value=N'IsRemoved' AND ioa.BoolValue=1) "
      L"ORDER BY own.InfoObjectId",
      id);
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  free(sql);
  card_add(c, L"\r\nВХОДИМОСТЬ ПО ТЕХСОСТАВУ");
  if (n < 0) {
    card_add(c, L": запрос не выполнился.\r\n%s\r\n", err);
    return;
  }
  if (n == 0) {
    card_add(c, L": нигде не нашлось.\r\n"
                L"Либо этот объект ни в один техсостав не включён, либо он сам верхний.\r\n");
    return;
  }
  card_add(c, L" (%d):\r\n", n);
  for (int i = 0; i < n; i++) {
    card_add(c, L"  %-40s %ld\r\n", rows[i].s1[0] ? rows[i].s1 : L"(без имени)", rows[i].n1);
    if (rows[i].s2[0]) card_add(c, L"       в изделии %s (%ld)\r\n", rows[i].s2, rows[i].n2);
  }
}

/* Файл чертежа в PLM не лежит — там только ссылка на объект-документ, а сами
   файлы у нас на сетевом диске, который уже проиндексирован. Поэтому чертёж
   ищем не в базе, а в том же json, что и обычный поиск файлов. */
static BOOL card_is_drawing(const wchar_t *name) {
  static const wchar_t *ext[] = {L"cdw", L"frw", L"spw", L"a3d", L"m3d", L"dwg",
                                 L"dxf", L"pdf", L"tif", L"tiff", L"jpg", L"png"};
  const wchar_t *dot = wcsrchr(name, L'.');
  if (!dot || !dot[1]) return FALSE;
  for (int i = 0; i < (int)(sizeof(ext) / sizeof(ext[0])); i++)
    if (_wcsicmp(dot + 1, ext[i]) == 0) return TRUE;
  return FALSE;
}

/* Индекс большой — 200 тысяч записей, — поэтому обходим его один раз и
   раскладываем находки сразу по двум спискам, а не ищем трижды. */
typedef struct {
  wchar_t draw[8][PLM_LINK];
  wchar_t other[6][PLM_LINK];
  int nd, no, total;
} CardFiles;

static void card_scan_files(const wchar_t *key, CardFiles *f) {
  memset(f, 0, sizeof(*f));
  files_lock();
  FileIdx *ix = g_idx;
  if (ix) {
    for (int i = 0; i < ix->n; i++) {
      if (!wcs_istr(ix->ent[i].name, key)) continue;
      f->total++;
      const wchar_t *dir = ix->ent[i].dir < ix->dirsN ? ix->dirs[ix->ent[i].dir] : L"";
      if (card_is_drawing(ix->ent[i].name)) {
        if (f->nd < 8) _snwprintf(f->draw[f->nd++], PLM_LINK, L"%s\\%s", dir, ix->ent[i].name);
      } else if (f->no < 6) {
        _snwprintf(f->other[f->no++], PLM_LINK, L"%s\\%s", dir, ix->ent[i].name);
      }
    }
  }
  files_unlock();
}

static void card_show_file(CardOut *c, const wchar_t *full) {
  const wchar_t *slash = wcsrchr(full, L'\\');
  if (slash) {
    wchar_t dir[PLM_LINK];
    lstrcpynW(dir, full, (int)(slash - full) + 1);
    card_add(c, L"  %s\r\n       %s\r\n", slash + 1, dir);
  } else {
    card_add(c, L"  %s\r\n", full);
  }
}

/* У конфигурации изделия своего атрибута Designation нет, а обозначение
   при этом стоит первым словом в имени: «АДЕ 3422-682.01.01.00 [Шаблон]:1».
   Берём всё до первой скобки, двоеточия или угловой скобки. */
/* Наименование стоит в скобках: «АДЕ 3422-682.01.01.01ТП [Секция]». */
static void card_title_from_name(const wchar_t *name, wchar_t *out, int cap) {
  out[0] = 0;
  const wchar_t *open = wcschr(name, L'[');
  if (!open) return;
  open++;
  int i = 0;
  while (open[i] && open[i] != L']' && i < cap - 1) {
    out[i] = open[i];
    i++;
  }
  out[i] = 0;
}

static void card_des_from_name(const wchar_t *name, wchar_t *out, int cap) {
  int i = 0;
  while (name[i] && i < cap - 1) {
    wchar_t ch = name[i];
    if (ch == L'[' || ch == L'<' || ch == L'(' || ch == L':') break;
    out[i] = ch;
    i++;
  }
  while (i > 0 && (out[i - 1] == L' ' || out[i - 1] == L'\t')) i--;
  out[i] = 0;
}

/* Найденный объект бывает не изделием и не техпроцессом, а конфигурацией
   версии изделия: у неё нет ни ActualVersion, ни карточки техпроцессов.
   Но техпроцесс на эту деталь существует и зовётся по тому же обозначению —
   именно так его находит обычный поиск. */
static long card_tp_by_designation(SQLHDBC dbc, const wchar_t *des, CardOut *c, CardRow *rows,
                                   wchar_t *err, long *verOut) {
  if (!des || !des[0]) return 0;
  size_t dlen = wcslen(des);
  /* 1. среди того, что уже нашёл поиск: одна проверка по номерам вместо
        перебора всей базы */
  wchar_t cand[PLM_ROWS * 12];
  int cp = 0, ncand = 0;
  for (int i = 0; i < g_snapN && ncand < 40; i++) {
    if (_wcsnicmp(g_snapName[i], des, dlen) != 0) continue;
    if (!g_snapId[i]) continue;
    if (cp > (int)(sizeof(cand) / sizeof(cand[0])) - 14) break;
    cp += _snwprintf(cand + cp, 13, ncand ? L",%ld" : L"%ld", g_snapId[i]);
    ncand++;
  }
  cand[cp] = 0;
  if (ncand) {
    wchar_t *q = (wchar_t *)malloc(2200 * sizeof(wchar_t));
    if (q) {
      _snwprintf(q, 2200,
                 L"SELECT TOP 20 o.InfoObjectId, o.Name, ISNULL(flag.V,N'нет'), av.L, o.TemplateId "
                 L"FROM InfoObjects AS o WITH(NOLOCK) "
                 L"CROSS APPLY (SELECT TOP 1 ISNULL(iv.Link,0) AS L "
                 L"FROM InfoObjectAttributes AS iv WITH(NOLOCK) "
                 L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=iv.NameKeyId "
                 L"AND nkv.Value=N'ActualVersion' "
                 L"WHERE iv.OwnerId=o.InfoObjectId AND iv.Outdated=0) AS av "
                 L"OUTER APPLY (SELECT TOP 1 N'да' AS V FROM InfoObjectAttributes AS ia "
                 L"WITH(NOLOCK) JOIN NameKeys AS nki WITH(NOLOCK) ON nki.NameKeyId=ia.NameKeyId "
                 L"WHERE ia.OwnerId=o.InfoObjectId AND ia.Outdated=0 "
                 L"AND nki.Value IN (N'MainTP',N'IsActual') AND ia.BoolValue=1) AS flag "
                 L"WHERE o.InfoObjectId IN (%s) AND av.L>0 "
                 L"ORDER BY CASE WHEN flag.V=N'да' THEN 0 ELSE 1 END, o.InfoObjectId",
                 cand);
      int m = card_query(dbc, q, rows, CARD_ROWS, err, 280);
      free(q);
      if (m > 0) {
        card_add(c, L"ТЕХПРОЦЕССЫ ИЗ НАХОДОК (%d)\r\n", m);
        for (int i = 0; i < m; i++)
          card_add(c, L"  %s %-40s %ld\r\n", _wcsicmp(rows[i].s2, L"да") == 0 ? L"●" : L"○",
                   rows[i].s1[0] ? rows[i].s1 : L"(без имени)", rows[i].n1);
        card_add(c, L"\r\n");
        if (verOut) *verOut = rows[0].n2;
        return rows[0].n1;
      }
    }
  }
  /* 2. в находках не оказалось — ищем по базе, но только среди техпроцессов */
  wchar_t pat[260];
  like_escape(des, pat, 240);
  int n = (int)wcslen(pat);
  if (n < 250) {
    pat[n] = L'%';
    pat[n + 1] = 0;
  }
  wchar_t *sql = (wchar_t *)malloc(3000 * sizeof(wchar_t));
  if (!sql) return 0;
  _snwprintf(
      sql, 3000,
      L"SELECT TOP 20 tp.InfoObjectId, tp.Name, ISNULL(flag.V,N'нет'), av.L, tp.TemplateId "
      /* сперва сужаем по имени, и только потом лезем в атрибуты: иначе
         сервер обходит атрибуты каждого объекта базы */
      L"FROM (SELECT TOP 200 InfoObjectId, Name, TemplateId FROM InfoObjects WITH(NOLOCK) "
      L"WHERE Erased=0 AND TemplateId IN (SELECT TemplateId FROM Templates WITH(NOLOCK) "
      L"WHERE NameKey=N'TechnologicalStructure') "
      L"AND Name LIKE N'%s' ESCAPE '\\' COLLATE Cyrillic_General_CI_AS) AS tp "
      L"CROSS APPLY (SELECT TOP 1 ISNULL(iv.Link,0) AS L "
      L"FROM InfoObjectAttributes AS iv WITH(NOLOCK) "
      L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=iv.NameKeyId "
      L"AND nkv.Value=N'ActualVersion' "
      L"WHERE iv.OwnerId=tp.InfoObjectId AND iv.Outdated=0) AS av "
      L"OUTER APPLY (SELECT TOP 1 CASE WHEN ia.BoolValue=1 THEN N'да' ELSE N'нет' END AS V "
      L"FROM InfoObjectAttributes AS ia WITH(NOLOCK) "
      L"JOIN NameKeys AS nki WITH(NOLOCK) ON nki.NameKeyId=ia.NameKeyId "
      L"WHERE ia.OwnerId=tp.InfoObjectId AND ia.Outdated=0 "
      L"AND nki.Value IN (N'MainTP',N'IsActual') AND ia.BoolValue=1) AS flag "
      L"WHERE av.L>0 "
      L"ORDER BY CASE WHEN flag.V=N'да' THEN 0 ELSE 1 END, tp.InfoObjectId",
      pat);
  int k = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  free(sql);
  if (k < 0) {
    card_add(c, L"Поиск техпроцесса по обозначению не выполнился.\r\n%s\r\n", err);
    return 0;
  }
  if (k == 0) {
    card_add(c, L"Техпроцессов с обозначением «%s» в базе нет.\r\n", des);
    return 0;
  }
  card_add(c, L"ТЕХПРОЦЕССЫ ПО ОБОЗНАЧЕНИЮ «%s» (%d)\r\n", des, k);
  for (int i = 0; i < k; i++)
    card_add(c, L"  %s %-40s %ld\r\n", _wcsicmp(rows[i].s2, L"да") == 0 ? L"●" : L"○",
             rows[i].s1[0] ? rows[i].s1 : L"(без имени)", rows[i].n1);
  card_add(c, L"\r\n");
  if (verOut) *verOut = rows[0].n2;
  return rows[0].n1;
}

static void card_drawings(const wchar_t *designation, CardOut *c) {
  card_add(c, L"\r\nЧЕРТЁЖ И ФАЙЛЫ");
  if (!designation || !designation[0]) {
    card_add(c, L": у объекта нет обозначения, искать нечего.\r\n");
    return;
  }
  if (g_filesN == 0) files_load_idx();
  if (g_filesN == 0) {
    card_add(c, L": индекс файлов пуст — нажмите «Обновить JSON» в Настройках.\r\n");
    return;
  }
  /* Обозначение ЭСИ бывает длиннее имени файла: у техпроцесса на конце -01ТП,
     а чертёж назван по самой детали. Не нашли — отрезаем хвост и пробуем ещё. */
  wchar_t key[200];
  lstrcpynW(key, designation, 200);
  CardFiles f;
  memset(&f, 0, sizeof(f));
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt) {
      wchar_t *cut = wcsrchr(key, L'-');
      if (!cut || cut == key) break;
      *cut = 0;
    }
    card_scan_files(key, &f);
    if (f.total) break;
  }
  if (!f.total) {
    card_add(c, L": по «%s» в индексе ничего нет.\r\n", designation);
    return;
  }
  card_add(c, L" (по «%s», найдено %d)\r\n", key, f.total);
  for (int i = 0; i < f.nd; i++) {
    card_show_file(c, f.draw[i]);
    if (!g_cardDraw[0]) lstrcpynW(g_cardDraw, f.draw[i], PLM_LINK);
  }
  if (!f.nd) card_add(c, L"  чертежей не нашлось\r\n");
  for (int i = 0; i < f.no; i++) card_show_file(c, f.other[i]);
}

/* У техпроцесса есть прямая ссылка на изделие — ManufacturedProducts.
   Раньше я шёл к нему окольным путём, через коллекцию техпроцессов и её
   карточку; эта ссылка короче и надёжнее. Она бывает и одиночной, и
   коллекцией, поэтому смотрим оба вида. Обозначение изделия забираем
   заодно: чертёж назван по нему, а не по техпроцессу. */
static long card_owner_of_tp(SQLHDBC dbc, long tpId, CardOut *c, CardRow *rows, wchar_t *err,
                             wchar_t *desOut, int desCap) {
  wchar_t *sql = (wchar_t *)malloc(3000 * sizeof(wchar_t));
  if (!sql) return 0;
  _snwprintf(
      sql, 3000,
      L"SELECT TOP 20 o.InfoObjectId, o.Name, ISNULL(des.V,N''), 0, o.TemplateId "
      L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
      L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
      L"AND nk.Value=N'ManufacturedProducts' "
      L"OUTER APPLY (SELECT ea.Link AS L FROM InfoObjectCollectionElements AS ce WITH(NOLOCK) "
      L"JOIN InfoObjectAttributes AS ea WITH(NOLOCK) "
      L"ON ea.CollectionElementId=ce.CollectionElementId AND ea.DataType=6 "
      L"WHERE ce.AttributeId IN (a.Link, a.AttributeId) AND ce.Outdated=0) AS el "
      L"JOIN InfoObjects AS o WITH(NOLOCK) "
      L"ON o.InfoObjectId=COALESCE(el.L, a.Link) AND o.Erased=0 "
      L"OUTER APPLY (SELECT TOP 1 ad.ShortText AS V FROM InfoObjectAttributes AS ad WITH(NOLOCK) "
      L"JOIN NameKeys AS nkd WITH(NOLOCK) ON nkd.NameKeyId=ad.NameKeyId "
      L"AND nkd.Value=N'Designation' "
      L"WHERE ad.OwnerId=o.InfoObjectId AND ad.Outdated=0) AS des "
      L"WHERE a.OwnerId=%ld AND a.Outdated=0",
      tpId);
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  free(sql);
  if (n <= 0) {
    card_add(c, L"Изделие: прямой ссылки на него у техпроцесса нет.\r\n");
    /* Разбор, как она зовётся и на чём висит, нужен не в карточке,
       а когда с ней разбираются. */
    sql = g_cardVerbose ? (wchar_t *)malloc(2600 * sizeof(wchar_t)) : NULL;
    if (sql) {
      _snwprintf(sql, 2600,
                 L"SELECT TOP 30 a.OwnerId, nk.Value, "
                 L"COALESCE(a.ShortText, CONVERT(NVARCHAR(32), a.Link), N'—'), "
                 L"ISNULL(a.Link,0), a.DataType "
                 L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
                 L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
                 L"WHERE a.OwnerId=%ld AND (nk.Value LIKE N'%%Manufactur%%' "
                 L"OR nk.Value LIKE N'%%Product%%' OR nk.Value LIKE N'%%Part%%')",
                 tpId);
      int k = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
      free(sql);
      if (k > 0) {
        card_add(c, L"  ── что есть про изделие у техпроцесса ──\r\n");
        for (int i = 0; i < k; i++)
          card_add(c, L"     %-30s %-12s тип %ld\r\n", rows[i].s1, rows[i].s2, rows[i].n3);
      } else {
        card_add(c, L"  у техпроцесса нет ни одного атрибута со словом Product\r\n");
      }
    }
    card_add(c, L"\r\n");
    return 0;
  }
  card_add(c, L"ИЗДЕЛИЕ%s\r\n", n > 1 ? L" (несколько)" : L"");
  for (int i = 0; i < n; i++) {
    card_add(c, L"  %-40s %ld\r\n", rows[i].s1[0] ? rows[i].s1 : L"(без имени)", rows[i].n1);
    if (rows[i].s2[0]) card_add(c, L"       %s\r\n", rows[i].s2);
  }
  card_add(c, L"\r\n");
  if (desOut && desCap > 0) {
    if (rows[0].s2[0]) lstrcpynW(desOut, rows[0].s2, desCap);
    else if (rows[0].s1[0]) card_des_from_name(rows[0].s1, desOut, desCap);
  }
  return rows[0].n1;
}

static void plm_card(long id, wchar_t *out, int cap) {
  CardOut c;
  c.w = out;
  c.cap = cap;
  c.len = 0;
  out[0] = 0;

  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[280];
  if (!plm_connect(&env, &dbc, err, 280)) {
    ans_printf(out, cap, L"Не удалось подключиться к %s.\r\n%s", g_sqlHost, err);
    return;
  }
  g_cardT0 = GetTickCount64();
  g_cardTAttrs = g_cardTTp = g_cardTOps = g_cardTUsed = g_cardTFiles = 0;
  long actualVer = 0, tpCard = 0;
  wchar_t designation[200] = {0}, objName[260] = {0};
  BOOL mainFlag = FALSE;
  g_cardDraw[0] = 0;
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * CARD_ROWS);
  if (!rows) {
    ans_printf(out, cap, L"Не хватило памяти");
    goto done;
  }
  wchar_t sql[3600];

  /* 1. сам объект */
  _snwprintf(sql, 3600,
             L"SELECT TOP 1 o.InfoObjectId, o.Name, t.NameKey, ISNULL(o.ParentId,0), o.TemplateId "
             L"FROM InfoObjects AS o WITH(NOLOCK) "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId "
             L"WHERE o.InfoObjectId=%ld",
             id);
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  if (n < 0) {
    card_add(&c, L"Запрос об объекте не выполнился.\r\n%s\r\n", err);
    goto freed;
  }
  if (n == 0) {
    card_add(&c, L"Объект %ld в базе не найден.\r\n", id);
    goto freed;
  }
  lstrcpynW(objName, rows[0].s1, 260);
  {
    wchar_t des0[200] = {0}, title0[200] = {0};
    card_des_from_name(objName, des0, 200);
    card_title_from_name(objName, title0, 200);
    card_add(&c, L"┌──────────────────────────────────────────────────────────┐\r\n");
    card_add(&c, L"  %-14s%s\r\n", L"Обозначение", des0[0] ? des0 : objName);
    if (title0[0]) card_add(&c, L"  %-14s%s\r\n", L"Наименование", title0);
    card_add(&c, L"  %-14sID %ld", L"", id);
    if (g_cardVerbose) card_add(&c, L" · %s", rows[0].s2[0] ? rows[0].s2 : L"?");
    card_add(&c, L"\r\n└──────────────────────────────────────────────────────────┘\r\n\r\n");
  }

  /* 2. его атрибуты */
  _snwprintf(sql, 3600,
             L"SELECT TOP 200 a.AttributeId, nk.Value, %s, ISNULL(a.Link,0), a.DataType "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"WHERE a.OwnerId=%ld AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0 "
             L"ORDER BY nk.Value",
             CARD_VALUE_SQL, id);
  n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  if (n < 0) {
    card_add(&c, L"Атрибуты не прочитались.\r\n%s\r\n\r\n", err);
  } else if (n == 0) {
    card_add(&c, L"Атрибутов нет.\r\n\r\n");
  } else {
    if (g_cardVerbose) card_add(&c, L"СВОЙСТВА\r\n");
    int skipped = 0, unknown = 0;
    for (int i = 0; i < n; i++) {
      /* эти три решают, куда идти дальше, и они уже прочитаны — лишний
         запрос к серверу за ними не нужен */
      if (_wcsicmp(rows[i].s1, L"Designation") == 0 && rows[i].s2[0])
        lstrcpynW(designation, rows[i].s2, 200);
      if (_wcsicmp(rows[i].s1, L"ActualVersion") == 0) actualVer = rows[i].n2;
      if (_wcsicmp(rows[i].s1, L"TechnologicalProcessesCard") == 0) tpCard = rows[i].n2;
      if (_wcsicmp(rows[i].s1, L"MainTP") == 0 || _wcsicmp(rows[i].s1, L"IsActual") == 0)
        mainFlag = _wcsicmp(rows[i].s2, L"да") == 0;
      if (card_hidden(rows[i].s1)) {
        skipped++;
        continue;
      }
      if (!rows[i].s2[0] && !rows[i].n2) {
        /* коллекции и составные атрибуты своего значения не имеют, но знать
           об их существовании надо: раньше они просто исчезали из списка */
        if (g_cardVerbose && (rows[i].n3 == 8 || rows[i].n3 == 23))
          card_add(&c, L"  %-28s (%s)\r\n",
                   card_label(rows[i].s1) ? card_label(rows[i].s1) : rows[i].s1,
                   rows[i].n3 == 8 ? L"коллекция" : L"составной");
        continue;
      }
      if (!card_label(rows[i].s1)) unknown++;
      if (g_cardVerbose) card_pair_t(&c, L"  ", rows[i].s1, rows[i].s2, rows[i].n2, rows[i].n3);
    }
    if (skipped && g_cardVerbose) card_add(&c, L"  (служебных скрыто: %d)\r\n", skipped);
    (void)unknown;
    if (g_cardVerbose) card_add(&c, L"\r\n");
  }

  g_cardTAttrs = card_lap();
  if (!designation[0] && objName[0]) card_des_from_name(objName, designation, 200);

  /* 3. дальше зависит от того, что это за объект.
        Сам техпроцесс несёт ActualVersion — тогда идём прямо в его состав.
        Изделие несёт TechnologicalProcessesCard — тогда сперва находим,
        какой из его техпроцессов основной. */
  if (actualVer) {
    card_add(&c, L"Это сам техпроцесс%s.\r\n\r\n",
              mainFlag ? L", помечен основным" : L"");
    /* чертёж назван по обозначению детали, а не техпроцесса — берём его отсюда */
    card_owner_of_tp(dbc, id, &c, rows, err, designation, 200);
    g_cardTTp = card_lap();
    card_operations(dbc, id, actualVer, &c, rows, err);
    g_cardTOps = card_lap();
    card_where_used(dbc, id, &c, rows, err);
    goto freed;
  }

  if (!tpCard) {
    /* это не изделие и не техпроцесс — скорее всего конфигурация версии.
       Техпроцесс на деталь всё равно существует, ищем его по обозначению */
    card_add(&c, L"Своего техпроцесса у объекта нет — ищу по обозначению.\r\n\r\n");
    long ver = 0;
    long tp = card_tp_by_designation(dbc, designation, &c, rows, err, &ver);
    g_cardTTp = card_lap();
    if (tp) card_operations(dbc, tp, ver, &c, rows, err);
    g_cardTOps = card_lap();
    card_where_used(dbc, id, &c, rows, err);
    goto freed;
  }

  _snwprintf(
      sql, 3600,
      L"SELECT TOP 50 tp.InfoObjectId, tp.Name, ISNULL(flag.V,N'нет'), "
      L"ISNULL(av.L,0), ISNULL(tp.TemplateId,0) "
      L"FROM InfoObjectAttributes AS la WITH(NOLOCK) "
      L"JOIN NameKeys AS nkl WITH(NOLOCK) ON nkl.NameKeyId=la.NameKeyId "
      L"AND nkl.Value=N'TechnologicalProcesses' "
      L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId=la.AttributeId "
      L"AND ce.Outdated=0 "
      L"JOIN InfoObjectAttributes AS ea WITH(NOLOCK) "
      L"ON ea.CollectionElementId=ce.CollectionElementId AND ea.DataType=6 "
      L"JOIN InfoObjects AS tp WITH(NOLOCK) ON tp.InfoObjectId=ea.Link AND tp.Erased=0 "
      L"OUTER APPLY (SELECT TOP 1 CASE WHEN ia.BoolValue=1 THEN N'да' ELSE N'нет' END AS V "
      L"FROM InfoObjectAttributes AS ia WITH(NOLOCK) "
      L"JOIN NameKeys AS nki WITH(NOLOCK) ON nki.NameKeyId=ia.NameKeyId "
      L"WHERE ia.OwnerId=tp.InfoObjectId AND ia.Outdated=0 "
      L"AND nki.Value IN (N'MainTP',N'IsActual') AND ia.BoolValue=1) AS flag "
      L"OUTER APPLY (SELECT TOP 1 ISNULL(iv.Link,0) AS L "
      L"FROM InfoObjectAttributes AS iv WITH(NOLOCK) "
      L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=iv.NameKeyId "
      L"WHERE iv.OwnerId=tp.InfoObjectId AND nkv.Value=N'ActualVersion' AND iv.Outdated=0) AS av "
      L"WHERE la.OwnerId=%ld AND la.Outdated=0",
      tpCard);
  n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  if (n < 0) {
    card_add(&c, L"Техпроцессы: запрос не выполнился.\r\n%s\r\n", err);
    card_where_used(dbc, id, &c, rows, err);
    goto freed;
  }
  if (n == 0) {
    card_add(&c, L"Карточка ТП: объект %ld, но техпроцессов в ней нет.\r\n", tpCard);
    _snwprintf(sql, 3600,
               L"SELECT TOP 1 a.AttributeId, N'', N'', 0, a.DataType "
               L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
               L"WHERE a.OwnerId=%ld AND a.Outdated=0 AND nk.Value=N'TechnologicalProcesses'",
               tpCard);
    int k = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
    if (k <= 0) {
      card_add(&c, L"  в карточке нет списка TechnologicalProcesses\r\n");
      card_where_used(dbc, id, &c, rows, err);
      goto freed;
    }
    _snwprintf(sql, 3600,
               L"SELECT COUNT(*), N'', N'', 0, 0 FROM InfoObjectCollectionElements WITH(NOLOCK) "
               L"WHERE AttributeId=%ld AND Outdated=0",
               rows[0].n1);
    k = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
    card_add(&c, L"  строк в списке: %ld, но ссылок на сами ТП в них нет\r\n\r\n",
             k > 0 ? rows[0].n1 : 0);
    long ver2 = 0;
    long tp2 = card_tp_by_designation(dbc, designation, &c, rows, err, &ver2);
    if (tp2) card_operations(dbc, tp2, ver2, &c, rows, err);
    card_where_used(dbc, id, &c, rows, err);
    goto freed;
  }

  card_add(&c, L"ТЕХПРОЦЕССЫ (%d)\r\n", n);
  long chosen = 0, chosenVer = 0;
  for (int i = 0; i < n; i++) {
    BOOL act = _wcsicmp(rows[i].s2, L"да") == 0;
    if (act && !chosen) {
      chosen = rows[i].n1;
      chosenVer = rows[i].n2;
    }
    card_add(&c, L"  %s %-40s %ld\r\n", act ? L"●" : L"○",
             rows[i].s1[0] ? rows[i].s1 : L"(без имени)", rows[i].n1);
  }
  card_add(&c, L"\r\n");
  if (!chosen) {
    card_add(&c, L"Основного среди них нет: ни у одного не стоит MainTP/IsActual.\r\n\r\n");
    long ver3 = 0;
    long tp3 = card_tp_by_designation(dbc, designation, &c, rows, err, &ver3);
    if (tp3) card_operations(dbc, tp3, ver3, &c, rows, err);
    card_where_used(dbc, id, &c, rows, err);
    goto freed;
  }
  g_cardTTp = card_lap();
  card_operations(dbc, chosen, chosenVer, &c, rows, err);
  g_cardTOps = card_lap();
  card_where_used(dbc, id, &c, rows, err);

freed:
  g_cardTUsed = card_lap();
  card_drawings(designation, &c);
  g_cardTFiles = card_lap();
  if (g_cardVerbose)
    card_add(&c,
             L"\r\n── время сбора ──────────────────────────\r\n"
             L"  свойства %llu мс · техпроцесс %llu мс · операции %llu мс\r\n"
             L"  входимость %llu мс · файлы %llu мс\r\n",
             g_cardTAttrs, g_cardTTp, g_cardTOps, g_cardTUsed, g_cardTFiles);
  else
    card_add(&c, L"\r\n  собрано за %llu мс\r\n",
             g_cardTAttrs + g_cardTTp + g_cardTOps + g_cardTUsed + g_cardTFiles);
  free(rows);
done:
  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);
}

static void compose_answer(const wchar_t *query, wchar_t *out, int cap) {
  wchar_t a[1200] = {0};
  const wchar_t *src = L"";
  if (g_engine != 4 && g_engine != 5) g_plmCount = 0;
  g_resultFiles = FALSE;
  if (g_engine == 5) {
    files_search(query, out, cap);
    return;
  }
  if (g_engine == 4) {
    plm_lookup(query, out, cap);
    return;
  }
  if (internet_ask(query, a, 1200) && a[0]) {
    ans_printf(out, cap, L"Мини-ИИ\r\n\r\n%s", a);
    return;
  }
  src = mini_ai_ask(query, a, 1200);
  ans_printf(out, cap, L"%s (офлайн)\r\n\r\n%s", src, a);
}

typedef struct {
  wchar_t q[400];
} SearchJob;

static void show_answer_text(const wchar_t *text);
static void ans_zoom(int delta);
static void ans_toggle_big(HWND hwnd);
static void show_card_selected(void);
static void show_card_full(void);

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
  int pad = 12, btnH = 28, gap = 7;
  int top = PANEL_TITLE_H + 6;
  int by = rc.bottom - pad - btnH;
  BOOL withPane = g_fullMode && g_drawPane && g_drawImg;
  int textLeft = pad, textRight = rc.right - pad;
  if (withPane) {
    int split = (rc.right - pad * 2) * 55 / 100;
    if (split < 120) split = 120;
    MoveWindow(g_drawPane, pad, top, split, by - top - 6, TRUE);
    textLeft = pad + split + 8;
  }
  if (g_drawPane) ShowWindow(g_drawPane, withPane ? SW_SHOW : SW_HIDE);
  if (g_answerEdit)
    MoveWindow(g_answerEdit, textLeft, top, textRight - textLeft, by - top - 6, TRUE);
  if (g_answerList) {
    MoveWindow(g_answerList, pad, top, rc.right - pad * 2, by - top - 6, TRUE);
    int cw = rc.right - pad * 2 - 24;
    if (cw < 80) cw = 80;
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_WIDTH;
    col.cx = cw / 2;
    SendMessageW(g_answerList, LVM_SETCOLUMNW, 0, (LPARAM)&col);
    col.cx = cw - cw / 2;
    SendMessageW(g_answerList, LVM_SETCOLUMNW, 1, (LPARAM)&col);
  }
  HWND open = GetDlgItem(g_answer, ID_ANS_OPEN);
  HWND show = GetDlgItem(g_answer, ID_ANS_SHOW);
  HWND copy = GetDlgItem(g_answer, ID_ANS_COPY);
  HWND notes = GetDlgItem(g_answer, ID_ANS_NOTES);
  HWND cls = GetDlgItem(g_answer, ID_ANS_CLOSE);
  /* "В проводнике" only makes sense for file hits, so the row is 4 or 5 wide */
  HWND card = GetDlgItem(g_answer, ID_ANS_CARD);
  HWND draw = GetDlgItem(g_answer, ID_ANS_DRAW);
  HWND full = GetDlgItem(g_answer, ID_ANS_FULL);
  BOOL withDraw = draw && g_cardDraw[0] != 0;
  BOOL withFull = full && !g_resultFiles && g_plmCount > 0;
  BOOL withOpen = open && g_plmCount > 0;
  BOOL withShow = show && g_resultFiles && g_plmCount > 0;
  BOOL withCard = card && !g_resultFiles && g_plmCount > 0;
  int cols = 3 + (withOpen ? 1 : 0) + (withShow ? 1 : 0) + (withCard ? 1 : 0) +
             (withDraw ? 1 : 0) + (withFull ? 1 : 0);
  int bw = (rc.right - pad * 2 - gap * (cols - 1)) / cols;
  int slot = 0;
  if (withOpen) MoveWindow(open, pad + (bw + gap) * slot++, by, bw, btnH, TRUE);
  if (withShow) MoveWindow(show, pad + (bw + gap) * slot++, by, bw, btnH, TRUE);
  if (withCard) MoveWindow(card, pad + (bw + gap) * slot++, by, bw, btnH, TRUE);
  if (withFull) MoveWindow(full, pad + (bw + gap) * slot++, by, bw, btnH, TRUE);
  if (withDraw) MoveWindow(draw, pad + (bw + gap) * slot++, by, bw, btnH, TRUE);
  if (copy) MoveWindow(copy, pad + (bw + gap) * slot++, by, bw, btnH, TRUE);
  if (notes) MoveWindow(notes, pad + (bw + gap) * slot++, by, bw, btnH, TRUE);
  if (cls) MoveWindow(cls, pad + (bw + gap) * slot++, by, bw, btnH, TRUE);
}

static LRESULT CALLBACK AnswerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, g_paper);
    draw_panel_header(hwnd, hdc,
                      g_ansTitle ? g_ansTitle
                                 : (g_resultFiles ? L"Найденные файлы" : L"Находки"));
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_MOUSEWHEEL:
    if (GetKeyState(VK_CONTROL) & 0x8000) {
      ans_zoom(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
      return 0;
    }
    break;
  case WM_NCLBUTTONDBLCLK:
    /* двойной щелчок по шапке — на весь экран и обратно, как у обычных окон */
    ans_toggle_big(hwnd);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_ANS_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (LOWORD(wParam) == ID_ANS_OPEN) open_plm_selected();
    if (LOWORD(wParam) == ID_ANS_SHOW) show_selected_in_explorer();
    if (LOWORD(wParam) == ID_ANS_CARD) show_card_selected();
    if (LOWORD(wParam) == ID_ANS_FULL) show_card_full();
    if (LOWORD(wParam) == ID_ANS_DRAW && g_cardDraw[0])
      ShellExecuteW(NULL, L"open", g_cardDraw, NULL, NULL, SW_SHOWNORMAL);
    if (LOWORD(wParam) == ID_ANS_COPY) {
      int i = plm_selected_index();
      if (i >= 0 && i < g_plmCount) {
        clipboard_set(g_plmLinks[i]);
        show_status(L"Ссылка скопирована");
      } else if (g_answerEdit) {
        int len = GetWindowTextLengthW(g_answerEdit);
        wchar_t *w = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        if (w) {
          GetWindowTextW(g_answerEdit, w, len + 1);
          clipboard_set(w);
          free(w);
          show_status(L"Выжимка скопирована");
        }
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
  case WM_NOTIFY: {
    NMHDR *nm = (NMHDR *)lParam;
    if (nm && nm->idFrom == ID_ANS_LIST &&
        (nm->code == NM_DBLCLK || nm->code == NM_RETURN || nm->code == LVN_ITEMACTIVATE)) {
      open_plm_selected();
      return 0;
    }
    if (nm && nm->idFrom == ID_ANS_LIST && nm->code == LVN_COLUMNCLICK) {
      int col = ((NMLISTVIEW *)lParam)->iSubItem;
      g_sortDesc = (col == g_sortCol) ? !g_sortDesc : 0;
      g_sortCol = col;
      plm_sort(col);
      fill_plm_list();
      return 0;
    }
    break;
  }
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_EXITSIZEMOVE:
    layout_answer();
    InvalidateRect(hwnd, NULL, TRUE);
    if (g_answerList) InvalidateRect(g_answerList, NULL, TRUE);
    return 0;
  case WM_SIZE:
    layout_answer();
    /* moving the children is not enough: the uncovered strip has to be
       repainted too, or the list is left drawn at its old offset */
    InvalidateRect(hwnd, NULL, TRUE);
    if (g_answerList) InvalidateRect(g_answerList, NULL, TRUE);
    if (wParam != SIZE_MINIMIZED) {
      RECT wr;
      GetWindowRect(hwnd, &wr);
      g_ansX = wr.left;
      g_ansY = wr.top;
      if (g_ansMono) {
        g_cardW = wr.right - wr.left;
        g_cardH = wr.bottom - wr.top;
      } else {
        g_ansW = wr.right - wr.left;
        g_ansH = wr.bottom - wr.top;
      }
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void create_answer(HWND owner) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  /* without these the header keeps whatever it was drawn at before a resize */
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = AnswerProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = g_paper;
  wc.lpszClassName = L"CursorPadAnswer";
  RegisterClassExW(&wc);
  g_answer = CreateWindowExW(
      /* no NOACTIVATE: the list is meant to be walked with the keyboard */
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadAnswer",
      L"Находки", WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN, 0, 0,
      ANS_W, ANS_H, owner, NULL, g_inst, NULL);
  round_corners(g_answer);
  g_answerEdit = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
      0, 0, 100, 100, g_answer, NULL, NULL, NULL);
  {
    WNDCLASSEXW pc;
    memset(&pc, 0, sizeof(pc));
    pc.cbSize = sizeof(pc);
    pc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    pc.lpfnWndProc = DrawPaneProc;
    pc.hInstance = g_inst;
    pc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    pc.lpszClassName = L"CursorPadDraw";
    RegisterClassExW(&pc);
    g_drawPane = CreateWindowExW(0, L"CursorPadDraw", L"", WS_CHILD, 0, 0, 10, 10, g_answer,
                                 NULL, g_inst, NULL);
  }
  g_answerList = CreateWindowExW(
      0, WC_LISTVIEWW, L"",
      WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
      0, 0, 100, 100, g_answer, (HMENU)(INT_PTR)ID_ANS_LIST, NULL, NULL);
  if (g_answerList) {
    SendMessageW(g_answerList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(g_answerList, LVM_SETBKCOLOR, 0, (LPARAM)COL_PAPER);
    SendMessageW(g_answerList, LVM_SETTEXTBKCOLOR, 0, (LPARAM)COL_PAPER);
    SendMessageW(g_answerList, LVM_SETTEXTCOLOR, 0, (LPARAM)COL_INK);
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx = 210;
    col.iSubItem = 0;
    col.pszText = L"1 ЭСИ";
    SendMessageW(g_answerList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    col.cx = 210;
    col.iSubItem = 1;
    col.pszText = L"2 ТП";
    SendMessageW(g_answerList, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
  }
  HWND open = mk_btn(g_answer, L"Открыть PLM", ID_ANS_OPEN);
  HWND show = mk_btn(g_answer, L"В проводнике", ID_ANS_SHOW);
  HWND card = mk_btn(g_answer, L"Карточка", ID_ANS_CARD);
  HWND draw = mk_btn(g_answer, L"Открыть чертёж", ID_ANS_DRAW);
  HWND full = mk_btn(g_answer, L"Атрибуты", ID_ANS_FULL);
  HWND copy = mk_btn(g_answer, L"Копировать", ID_ANS_COPY);
  HWND notes = mk_btn(g_answer, L"В блокнот", ID_ANS_NOTES);
  HWND cls = mk_btn(g_answer, L"Закрыть", ID_ANS_CLOSE);
  if (g_fontBody) SendMessageW(g_answerEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  if (g_fontBody && g_answerList) SendMessageW(g_answerList, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  if (g_fontUi) {
    SendMessageW(open, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(show, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(card, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(draw, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(full, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(copy, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(notes, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(cls, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  }
  layout_answer();
}

/* The card can take a few seconds on a loaded server, so it runs off the UI
   thread exactly like a search does. */
static DWORD WINAPI card_thread(LPVOID param) {
  long id = (long)(LONG_PTR)param;
  wchar_t *out = (wchar_t *)malloc(160000 * sizeof(wchar_t));
  if (out) {
    out[0] = 0;
    plm_card(id, out, 160000);
    /* чертёж грузится здесь же: распаковка большого TIFF не должна
       подвешивать окно */
    if (g_fullMode && g_cardDraw[0]) draw_open(g_cardDraw);
    if (!PostMessageW(g_hwnd, WM_SEARCH_DONE, 0, (LPARAM)out)) free(out);
  }
  InterlockedExchange(&g_netBusy, 0);
  return 0;
}

static void show_card_selected_mode(BOOL full);

static void show_card_selected(void) {
  g_cardVerbose = FALSE; /* «Карточка»: обозначение, ТП с операциями, входимость */
  show_card_selected_mode(TRUE);
}

static void show_card_full(void) {
  g_cardVerbose = TRUE; /* «Атрибуты»: то же плюс всё содержимое объекта */
  show_card_selected_mode(TRUE);
}

static void show_card_selected_mode(BOOL full) {
  int i = plm_selected_index();
  if (i < 0 || i >= g_plmCount || !g_plmIds[i]) {
    show_status(L"Выберите строку");
    return;
  }
  if (InterlockedCompareExchange(&g_netBusy, 1, 0) != 0) {
    show_status(L"Запрос уже идёт");
    return;
  }
  g_snapN = 0;
  for (int k = 0; k < g_plmCount && k < PLM_ROWS; k++) {
    lstrcpynW(g_snapName[g_snapN], g_plmEsi[k], PLM_COL1);
    g_snapId[g_snapN] = g_plmIds[k];
    g_snapN++;
  }
  g_ansTitle = g_cardVerbose ? L"Атрибуты объекта" : L"Карточка";
  g_ansMono = TRUE;
  g_cardDraw[0] = 0;
  g_fullMode = full;
  draw_close();
  g_plmCount = 0; /* текст вместо списка */
  wchar_t wait[120];
  _snwprintf(wait, 120, L"Смотрю объект %ld в PLM…", g_plmIds[i]);
  show_answer_text(wait);
  HANDLE th = CreateThread(NULL, 0, card_thread, (LPVOID)(LONG_PTR)g_plmIds[i], 0, NULL);
  if (th) CloseHandle(th);
  else InterlockedExchange(&g_netBusy, 0);
}

/* Масштаб текста ответа. Карточка бывает длинной, и подгонять окно мышью
   каждый раз утомительно — проще уменьшить текст. Ctrl+колесо. */
static HFONT make_font(const wchar_t *face, int pt, int weight);
static HFONT g_ansFontZoom;
static BOOL g_ansZoomMono;

static HFONT ans_font(void) {
  if (g_ansFontZoom && g_ansZoomMono == (g_ansMono != 0)) return g_ansFontZoom;
  if (g_ansFontZoom) DeleteObject(g_ansFontZoom);
  g_ansZoomMono = g_ansMono != 0;
  g_ansFontZoom = make_font(g_ansMono ? L"Consolas" : L"Segoe UI Variable Text", g_ansPt,
                            FW_NORMAL);
  if (!g_ansFontZoom)
    g_ansFontZoom = make_font(g_ansMono ? L"Courier New" : L"Segoe UI", g_ansPt, FW_NORMAL);
  return g_ansFontZoom;
}

static void ans_zoom(int delta) {
  int pt = g_ansPt + delta;
  if (pt < 7) pt = 7;
  if (pt > 22) pt = 22;
  if (pt == g_ansPt) return;
  g_ansPt = pt;
  if (g_ansFontZoom) {
    DeleteObject(g_ansFontZoom);
    g_ansFontZoom = NULL;
  }
  if (g_answerEdit) {
    SendMessageW(g_answerEdit, WM_SETFONT, (WPARAM)ans_font(), TRUE);
    InvalidateRect(g_answerEdit, NULL, TRUE);
  }
  save_cursor_pref();
  wchar_t m[64];
  _snwprintf(m, 64, L"Масштаб текста: %d", g_ansPt);
  show_status(m);
}

/* Подобрать размер окна под текст: длина самой длинной строки и число строк.
   Больше рабочей области не делаем, меньше разумного — тоже. */
static void ans_fit(const wchar_t *text, const RECT *wa, int *outW, int *outH) {
  int minW = ANS_W, minH = ANS_H;
  *outW = minW;
  *outH = minH;
  if (!text || !text[0] || !g_answerEdit) return;
  HDC dc = GetDC(g_answerEdit);
  if (!dc) return;
  HFONT prev = (HFONT)SelectObject(dc, ans_font());
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int maxw = 0, lines = 1;
  const wchar_t *line = text;
  for (const wchar_t *p = text;; p++) {
    if (*p == L'\n' || !*p) {
      int len = (int)(p - line);
      if (len > 0 && line[len - 1] == L'\r') len--;
      if (len > 0 && len < 400) {
        SIZE sz;
        if (GetTextExtentPoint32W(dc, line, len, &sz) && sz.cx > maxw) maxw = sz.cx;
      } else if (len >= 400) {
        maxw = 100000; /* очень длинная строка — упрёмся в ширину экрана */
      }
      if (!*p) break;
      lines++;
      line = p + 1;
    }
  }
  if (prev) SelectObject(dc, prev);
  ReleaseDC(g_answerEdit, dc);
  int w = maxw + 64;  /* поля и полоса прокрутки */
  int h = lines * tm.tmHeight + PANEL_TITLE_H + 70;
  int maxW = wa->right - wa->left - 48, maxH = wa->bottom - wa->top - 48;
  if (w > maxW) w = maxW;
  if (h > maxH) h = maxH;
  if (w < minW) w = minW;
  if (h < minH) h = minH;
  *outW = w;
  *outH = h;
}

/* Разворот на всю рабочую область и возврат к прежнему размеру. Окно без
   обычной рамки, поэтому штатной кнопки у него нет — делаем сами. */
static RECT g_ansPrev;
static BOOL g_ansBig;

static void ans_toggle_big(HWND hwnd) {
  POINT pt;
  RECT wa, wr;
  GetWindowRect(hwnd, &wr);
  pt.x = (wr.left + wr.right) / 2;
  pt.y = (wr.top + wr.bottom) / 2;
  get_work_area(pt, &wa);
  if (!g_ansBig) {
    g_ansPrev = wr;
    g_ansBig = TRUE;
    SetWindowPos(hwnd, HWND_TOPMOST, wa.left + 8, wa.top + 8, wa.right - wa.left - 16,
                 wa.bottom - wa.top - 16, SWP_SHOWWINDOW);
  } else {
    g_ansBig = FALSE;
    SetWindowPos(hwnd, HWND_TOPMOST, g_ansPrev.left, g_ansPrev.top,
                 g_ansPrev.right - g_ansPrev.left, g_ansPrev.bottom - g_ansPrev.top,
                 SWP_SHOWWINDOW);
  }
  layout_answer();
}

static void show_answer_text(const wchar_t *text) {
  if (!g_answer) return;
  if (g_answerEdit) {
    SendMessageW(g_answerEdit, WM_SETFONT, (WPARAM)ans_font(), TRUE);
    SetWindowTextW(g_answerEdit, text ? text : L"");
  }
  fill_plm_list();
  POINT pt;
  GetCursorPos(&pt);
  int x = pt.x + 18, y = pt.y + 22;
  RECT wa;
  get_work_area(pt, &wa);
  /* по умолчанию окно выходит у курсора — в этом смысл программы. Но для
     карточки это мешает: открыл в удобном месте, а она снова прыгает.
     Галочка в Настройках оставляет её там, где её положили. */
  if (g_ansKeepPos && (g_ansX || g_ansY)) {
    x = g_ansX;
    y = g_ansY;
  }
  if (x + ANS_W > wa.right) x = wa.right - ANS_W - 8;
  if (y + ANS_H > wa.bottom) y = wa.bottom - ANS_H - 8;
  if (x < wa.left) x = wa.left + 8;
  if (y < wa.top) y = wa.top + 8;
  /* у карточки свой запомненный размер; пока его нет — подгоняем под текст,
     чтобы не тянуть окно мышью каждый раз */
  int aw, ah;
  if (g_ansMono) {
    if (g_cardW > 0 && g_cardH > 0) {
      aw = g_cardW;
      ah = g_cardH;
    } else {
      ans_fit(text, &wa, &aw, &ah);
    }
  } else {
    aw = g_ansW > 0 ? g_ansW : ANS_W;
    ah = g_ansH > 0 ? g_ansH : ANS_H;
  }
  if (x + aw > wa.right) x = wa.right - aw - 8;
  if (y + ah > wa.bottom) y = wa.bottom - ah - 8;
  if (x < wa.left) x = wa.left + 8;
  if (y < wa.top) y = wa.top + 8;
  g_ansBig = FALSE;
  SetWindowPos(g_answer, HWND_TOPMOST, x, y, aw, ah, SWP_SHOWWINDOW);
  layout_answer();
  if (g_plmCount > 0 && g_answerList) SetFocus(g_answerList);
}

static void start_lookup(const wchar_t *q) {
  if (!q) return;
  g_ansTitle = NULL;
  g_ansMono = FALSE;
  g_cardDraw[0] = 0;
  g_fullMode = FALSE;
  draw_close();
  while (*q == L' ' || *q == L'\t' || *q == L'\r' || *q == L'\n') q++;
  if (!q[0]) {
    show_status(L"Нечего искать — скопируйте текст");
    return;
  }
  wchar_t wait[440];
  _snwprintf(wait, 440,
             g_engine == 4 ? L"Ищу в PLM «%.80s»…" :
             (g_engine == 5 ? L"Ищу файлы «%.80s»…" : L"Мини-ИИ «%.80s»…"), q);
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

/* Spawn a helper and collect everything it writes. The old code stopped at a
   fixed 4 KB, which silently cut off anything past about a page. */
static BOOL run_capture(wchar_t *cmd, char **out, DWORD *outn, DWORD *code,
                        char *err, int errcap) {
  *out = NULL;
  *outn = 0;
  *code = (DWORD)-1;
  if (err && errcap) err[0] = 0;
  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE ord = NULL, owr = NULL, erd = NULL, ewr = NULL;
  if (!CreatePipe(&ord, &owr, &sa, 0)) return FALSE;
  if (!CreatePipe(&erd, &ewr, &sa, 0)) {
    CloseHandle(ord);
    CloseHandle(owr);
    return FALSE;
  }
  SetHandleInformation(ord, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(erd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = owr;
  si.hStdError = ewr;
  BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  CloseHandle(owr);
  CloseHandle(ewr);
  if (!ok) {
    CloseHandle(ord);
    CloseHandle(erd);
    return FALSE;
  }
  DWORD cap = 1 << 16, n = 0;
  char *buf = (char *)malloc(cap);
  if (buf) {
    for (;;) {
      if (n + 4096 > cap) {
        DWORD grow = cap * 2;
        char *nb = (char *)realloc(buf, grow);
        if (!nb) break;
        buf = nb;
        cap = grow;
      }
      DWORD got = 0;
      if (!ReadFile(ord, buf + n, cap - n - 1, &got, NULL) || !got) break;
      n += got;
    }
    buf[n] = 0;
  }
  /* helpers write one short line here, far below the pipe buffer, so reading
     it after stdout cannot wedge them */
  if (err && errcap > 1) {
    DWORD got = 0;
    if (ReadFile(erd, err, (DWORD)errcap - 1, &got, NULL)) err[got] = 0;
  }
  WaitForSingleObject(pi.hProcess, 30000);
  GetExitCodeProcess(pi.hProcess, code);
  CloseHandle(ord);
  CloseHandle(erd);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
  *out = buf;
  *outn = n;
  return buf != NULL;
}

static wchar_t *utf8_to_alloc(const char *s, DWORD n) {
  if (!s || !n) return NULL;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
  if (wlen <= 0) return NULL;
  wchar_t *w = (wchar_t *)malloc(((size_t)wlen + 1) * sizeof(wchar_t));
  if (!w) return NULL;
  MultiByteToWideChar(CP_UTF8, 0, s, (int)n, w, wlen);
  w[wlen] = 0;
  return w;
}

/* Turn the helper's one-line reason into something a person can act on. */
static void ocr_explain(const char *reason) {
  if (!reason || !reason[0]) {
    lstrcpynW(g_ocrNote, L"Текст не распознан", 160);
    return;
  }
  if (strstr(reason, "component missing"))
    lstrcpynW(g_ocrNote, L"Распознавание Windows недоступно в этой системе", 160);
  else if (strstr(reason, "no recognition languages"))
    lstrcpynW(g_ocrNote, L"Нет пакетов распознавания — добавьте язык в параметрах Windows", 160);
  else if (strstr(reason, "no engine for this language"))
    lstrcpynW(g_ocrNote, L"Нет русского распознавания — добавьте его в параметрах Windows", 160);
  else if (strstr(reason, "cannot read bitmap"))
    lstrcpynW(g_ocrNote, L"Не удалось прочитать снимок экрана", 160);
  else
    lstrcpynW(g_ocrNote, L"Текст не распознан", 160);
}

/* Recognition runs in a helper process on purpose: it reaches into system
   codecs, and a crash there must not take the notepad down. The companion
   module is tried first; the PowerShell script stays as a fallback so an
   older or stripped-down install is no worse off than before. */
static wchar_t *ocr_file_sync(const wchar_t *bmp) {
  wchar_t dir[MAX_PATH], path[MAX_PATH], cmd[1024];
  char err[256] = {0}, *out = NULL;
  DWORD n = 0, code = 0;
  exe_dir(dir, MAX_PATH);
  g_ocrNote[0] = 0;

  _snwprintf(path, MAX_PATH, L"%s\\CursorPadOcr.exe", g_dataDir[0] ? g_dataDir : dir);
  if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
    _snwprintf(cmd, 1024, L"\"%s\" \"%s\" ru", path, bmp);
    if (run_capture(cmd, &out, &n, &code, err, 256)) {
      if (code == 0 && n) {
        wchar_t *w = utf8_to_alloc(out, n);
        free(out);
        return w;
      }
      free(out);
      out = NULL;
      /* a missing engine or empty page will not go better through PowerShell */
      if (code == 2 || code == 3) {
        ocr_explain(code == 3 ? "" : err);
        return NULL;
      }
    }
  }

  _snwprintf(path, MAX_PATH, L"%s\\ocr.ps1", g_dataDir[0] ? g_dataDir : dir);
  _snwprintf(cmd, 1024,
             L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -Path \"%s\"",
             path, bmp);
  if (run_capture(cmd, &out, &n, &code, NULL, 0) && n) {
    wchar_t *w = utf8_to_alloc(out, n);
    free(out);
    return w;
  }
  free(out);
  ocr_explain(err);
  return NULL;
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
  if (g_btnAi) SetWindowTextW(g_btnAi, g_engine == 3 ? L"● Мини-ИИ" : L"Мини-ИИ");
  if (g_btnPlm) SetWindowTextW(g_btnPlm, g_engine == 4 ? L"● PLM" : L"PLM");
  if (g_btnFiles) SetWindowTextW(g_btnFiles, g_engine == 5 ? L"● Файлы" : L"Файлы");
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
