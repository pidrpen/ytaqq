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
#define ID_ANS_CLOSE 122
#define ID_ANS_SHOW 151 /* 131 was already ID_CLIP */
#define ID_ANS_CARD 152
#define ID_ANS_DRAW 154
#define ID_CARD_OPEN 160
#define ID_CARD_DRAW 161
#define ID_CARD_SHOW 162
#define ID_CARD_CLOSE 163
#define WM_CARD_DONE (WM_APP + 14)
#define ID_OCR_COPY 164
#define ID_OCR_FIND 165
#define ID_OCR_AGAIN 166
#define ID_OCR_CLOSE 167
#define ID_CARD_1C 168
#define ID_ANS_OPENTP 170
#define TIMER_1C 21

#define TIMER_CURSOR_KEEP 6
#define WM_SEARCH_DONE (WM_APP + 8)
#define WM_OCR_DONE (WM_APP + 9)
#define WM_SHOW_PAD (WM_APP + 10)
#define WM_SEL_DRAW (WM_APP + 13) /* чертёж для выбранной строки найдён */
/* нижний ряд теперь из четырёх именованных кнопок и «Закрыть»: уже не влезал */
#define ANS_W 640
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

#include "mini_ai.c"
#include "web_lookup.c"

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

static void ans_zoom(int delta);
static WNDPROC g_oldAnsEdit;

static LRESULT CALLBACK AnsEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_MOUSEWHEEL && (GetKeyState(VK_CONTROL) & 0x8000)) {
    ans_zoom(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
    return 0;
  }
  return CallWindowProcW(g_oldAnsEdit, hwnd, msg, wParam, lParam);
}

/* и над списком находок Ctrl+колесо — масштаб, а не прокрутка */
static WNDPROC g_oldAnsList;
static LRESULT CALLBACK AnsListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_MOUSEWHEEL && (GetKeyState(VK_CONTROL) & 0x8000)) {
    ans_zoom(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
    return 0;
  }
  return CallWindowProcW(g_oldAnsList, hwnd, msg, wParam, lParam);
}

static RECT g_ansPrev;
static BOOL g_ansBig; /* окно находок развёрнуто на весь экран */
static wchar_t g_cardDraw[PLM_LINK]; /* чертёж, найденный для карточки */
/* Чертёж выбранной строки находок. Ищется в индексе файлов сразу,
   без отдельного поиска «Файлы»: иначе кнопка «Открыть чертёж» не знает,
   гореть ей или быть потухшей. */
static wchar_t g_selDraw[PLM_LINK];
static volatile LONG g_drawGen;
/* объект, на который открыта карточка: списка уже нет, а «в СОЮЗ» живо */
static wchar_t g_ansObj[PLM_LINK];
static void ans_sync_buttons(void);
static void request_row_draw(void);
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

/* столбец «Заготовка» в находках — коротко; полностью — в подсказке и карточке */
static void pf_short(const wchar_t *full, wchar_t *out, int cap);
static HFONT ans_font(void);

/* Заготовка не влезает в ширину столбца — строки находок становятся в две
   строки текста, и она переносится (сам список так не умеет: высоту строк
   задаёт картинка-распорка, а текст столбца рисуем сами, с переносом).
   Влезает всё — строки обычные. Считается заново при смене ширины окна,
   масштаба (Ctrl+колесо) и когда приходят заготовки. */
static BOOL g_ansTall;
static int g_ansRowH;
static HIMAGELIST g_ansRowIl;

static void ans_rows_fit(void) {
  if (!g_answerList) return;
  HFONT f = (HFONT)SendMessageW(g_answerList, WM_GETFONT, 0, 0);
  HDC dc = GetDC(g_answerList);
  if (!dc) return;
  HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int colW = (int)SendMessageW(g_answerList, LVM_GETCOLUMNWIDTH, 2, 0) - 12;
  int lines = 1; /* сколько строк текста нужно самой длинной заготовке, не больше трёх */
  if (!g_resultFiles && colW > 20 && tm.tmHeight > 0)
    for (int i = 0; i < g_plmCount && lines < 3; i++) {
      wchar_t sh[PLM_COL1];
      pf_short(g_plmPf[i], sh, PLM_COL1);
      if (!sh[0]) continue;
      RECT m = {0, 0, colW, 0};
      DrawTextW(dc, sh, -1, &m, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_CALCRECT);
      int n = (m.bottom + tm.tmHeight - 1) / tm.tmHeight;
      if (n > lines) lines = n > 3 ? 3 : n;
    }
  if (of) SelectObject(dc, of);
  ReleaseDC(g_answerList, dc);
  BOOL need = lines > 1;
  int h = need ? tm.tmHeight * lines + 6 : 0;
  if (need == g_ansTall && h == g_ansRowH) return;
  g_ansTall = need;
  g_ansRowH = h;
  HIMAGELIST old = g_ansRowIl;
  g_ansRowIl = need ? ImageList_Create(1, h, ILC_COLOR32, 1, 0) : NULL;
  SendMessageW(g_answerList, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_ansRowIl);
  if (old) ImageList_Destroy(old);
  if (!need && f) SendMessageW(g_answerList, WM_SETFONT, (WPARAM)f, FALSE); /* вернуть обычную высоту */
  InvalidateRect(g_answerList, NULL, TRUE);
}

/* ячейка заготовки в высоких строках — с переносом по словам */
static LRESULT ans_list_customdraw(NMLVCUSTOMDRAW *cd) {
  DWORD st = cd->nmcd.dwDrawStage;
  if (st == CDDS_PREPAINT) return g_ansTall && !g_resultFiles ? CDRF_NOTIFYITEMDRAW : CDRF_DODEFAULT;
  if (st == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
  if (st != (CDDS_ITEMPREPAINT | CDDS_SUBITEM) || cd->iSubItem != 2) return CDRF_DODEFAULT;
  int i = (int)cd->nmcd.dwItemSpec;
  if (i < 0 || i >= g_plmCount) return CDRF_DODEFAULT;
  RECT r;
  r.top = 2;
  r.left = LVIR_BOUNDS;
  if (!SendMessageW(g_answerList, LVM_GETSUBITEMRECT, (WPARAM)i, (LPARAM)&r)) return CDRF_DODEFAULT;
  BOOL sel = (SendMessageW(g_answerList, LVM_GETITEMSTATE, (WPARAM)i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
  BOOL foc = GetFocus() == g_answerList;
  COLORREF bg = sel ? GetSysColor(foc ? COLOR_HIGHLIGHT : COLOR_BTNFACE) : COL_PAPER;
  COLORREF fg = sel && foc ? GetSysColor(COLOR_HIGHLIGHTTEXT) : COL_INK;
  HDC dc = cd->nmcd.hdc;
  HBRUSH br = CreateSolidBrush(bg);
  FillRect(dc, &r, br);
  DeleteObject(br);
  wchar_t sh[PLM_COL1];
  pf_short(g_plmPf[i], sh, PLM_COL1);
  RECT t = {r.left + 6, r.top + 2, r.right - 4, r.bottom - 2};
  RECT m = t;
  UINT fl = DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;
  DrawTextW(dc, sh, -1, &m, fl | DT_CALCRECT);
  int th = m.bottom - m.top, bh = t.bottom - t.top;
  if (th < bh) t.top += (bh - th) / 2; /* по середине строки, как соседние столбцы */
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, fg);
  DrawTextW(dc, sh, -1, &t, fl | DT_END_ELLIPSIS);
  return CDRF_SKIPDEFAULT;
}

static void fill_plm_list(void) {
  if (!g_answerList) return;
  LVCOLUMNW col;
  memset(&col, 0, sizeof(col));
  col.mask = LVCF_TEXT;
  wchar_t h0[64], h1[64], h2[64];
  const wchar_t *mark = g_sortDesc ? L" ↓" : L" ↑";
  _snwprintf(h0, 64, L"%s%s", g_resultFiles ? L"файл" : L"ЭСИ", g_sortCol == 0 ? mark : L"");
  _snwprintf(h1, 64, L"%s%s", g_resultFiles ? L"папка" : L"ТП", g_sortCol == 1 ? mark : L"");
  _snwprintf(h2, 64, L"%s%s", g_resultFiles ? L"" : L"Заготовка", g_sortCol == 2 ? mark : L"");
  col.pszText = h0;
  SendMessageW(g_answerList, LVM_SETCOLUMNW, 0, (LPARAM)&col);
  col.pszText = h1;
  SendMessageW(g_answerList, LVM_SETCOLUMNW, 1, (LPARAM)&col);
  col.pszText = h2;
  SendMessageW(g_answerList, LVM_SETCOLUMNW, 2, (LPARAM)&col);
  layout_answer(); /* у файлов столбца заготовки нет — ширины другие */
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
    if (!g_resultFiles) {
      wchar_t sh[PLM_COL1];
      pf_short(g_plmPf[i], sh, PLM_COL1);
      it.iSubItem = 2;
      it.pszText = sh;
      SendMessageW(g_answerList, LVM_SETITEMW, 0, (LPARAM)&it);
    }
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
  ans_rows_fit();
  /* чертёж для первой строки ищется сразу: его кнопка должна быть
     живой или потухшей ещё до того, как на неё потянутся */
  request_row_draw();
}

/* Rows are three parallel arrays; with at most PLM_ROWS of them an insertion
   sort that swaps whole rows is simpler than juggling an index permutation. */

/* Строка — это несколько параллельных массивов. Переставлять надо все:
   раньше сортировка меняла местами только имена, ссылки и номера, а номер
   ТП и шаблон оставались на старых местах — «Открыть ТП» брал чужой. */
static void plm_swap_rows(int a, int b, wchar_t *tmp) {
#define SWAP_STR(arr, n)                                   \
  do {                                                     \
    memcpy(tmp, arr[a], (n) * sizeof(wchar_t));            \
    memcpy(arr[a], arr[b], (n) * sizeof(wchar_t));         \
    memcpy(arr[b], tmp, (n) * sizeof(wchar_t));            \
  } while (0)
#define SWAP_NUM(arr)                                      \
  do {                                                     \
    long long t_ = (long long)arr[a];                      \
    arr[a] = arr[b];                                       \
    arr[b] = t_;                                           \
  } while (0)
  SWAP_STR(g_plmEsi, PLM_COL1);
  SWAP_STR(g_plmTp, PLM_COL2);
  SWAP_STR(g_plmPf, PLM_COL1);
  SWAP_STR(g_plmLinks, PLM_LINK);
  SWAP_NUM(g_plmIds);
  SWAP_NUM(g_plmTpId);
  SWAP_NUM(g_plmRealId);
  SWAP_NUM(g_plmTmpl);
#undef SWAP_STR
#undef SWAP_NUM
}

static void plm_sort(int col) {
  if (col < 0 || col > 2 || g_plmCount < 2) return;
  if (col == 2 && g_resultFiles) return;
  wchar_t *tmp = (wchar_t *)malloc(PLM_LINK * sizeof(wchar_t));
  if (!tmp) return;
  for (int i = 1; i < g_plmCount; i++) {
    for (int j = i; j > 0; j--) {
      const wchar_t *a = col == 0 ? g_plmEsi[j] : (col == 1 ? g_plmTp[j] : g_plmPf[j]);
      const wchar_t *b = col == 0 ? g_plmEsi[j - 1] : (col == 1 ? g_plmTp[j - 1] : g_plmPf[j - 1]);
      int cmp = _wcsicmp(a, b);
      if (g_sortDesc) cmp = -cmp;
      if (cmp >= 0) break;
      plm_swap_rows(j, j - 1, tmp);
    }
  }
  free(tmp);
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

/* ТП строки: сведённый к изделию — его собственный номер; строка, где ТП
   осталось само по себе, — её номер (ссылка строки ведёт на родителя). */
static long plm_row_tp(int i) {
  if (g_resultFiles || i < 0 || i >= g_plmCount) return 0;
  if (g_plmTpId[i]) return g_plmTpId[i];
  if (g_plmTmpl[i] == 1794) return g_plmRealId[i];
  return 0;
}

static void open_plm_tp_selected(void) {
  long tp = plm_row_tp(plm_selected_index());
  if (!tp) {
    show_status(L"У этой строки нет ТП");
    return;
  }
  wchar_t link[PLM_LINK];
  make_plm_link(link, PLM_LINK, tp);
  lstrcpynW(g_plmLastLink, link, PLM_LINK);
  open_plm_link(link);
  show_status(L"ТП — в текущий клиент СОЮЗ");
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

/* Техпроцесс сам знает, на какое изделие он написан: это ссылка
   ManufacturedProducts. Сличать обозначения по именам — гадание: хвосты
   у всех разные. Спрашиваем базу — один запрос на все найденные ТП. */
typedef struct {
  long tp;
  long prod;
  wchar_t name[PLM_COL1];
} TpProd;

static int plm_tp_products(SQLHDBC dbc, const wchar_t *ids, TpProd *out, int max) {
  wchar_t *sql = (wchar_t *)malloc(3000 * sizeof(wchar_t));
  if (!sql) return 0;
  _snwprintf(sql, 3000,
             L"SELECT TOP %d a.OwnerId, o.InfoObjectId, o.Name "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"AND nk.Value=N'ManufacturedProducts' "
             L"OUTER APPLY (SELECT TOP 1 ea.Link AS L "
             L"FROM InfoObjectCollectionElements AS ce WITH(NOLOCK) "
             L"JOIN InfoObjectAttributes AS ea WITH(NOLOCK) "
             L"ON ea.CollectionElementId=ce.CollectionElementId AND ea.DataType=6 "
             /* без этой оговорки пустая ссылка тянет чужие строки */
             L"WHERE ce.Outdated=0 AND (ce.AttributeId=a.AttributeId "
             L"OR (ISNULL(a.Link,0)<>0 AND ce.AttributeId=a.Link))) AS el "
             L"JOIN InfoObjects AS o WITH(NOLOCK) "
             L"ON o.InfoObjectId=COALESCE(el.L, a.Link) AND o.Erased=0 "
             L"WHERE a.OwnerId IN (%s) AND a.Outdated=0",
             max, ids);
  SQLHSTMT st = SQL_NULL_HSTMT;
  if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
    free(sql);
    return 0;
  }
  SQLRETURN r = SQLExecDirectW(st, (SQLWCHAR *)sql, SQL_NTS);
  free(sql);
  if (!SQL_SUCCEEDED(r)) {
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return 0;
  }
  SQLINTEGER tpId = 0, prId = 0;
  SQLWCHAR nm[200];
  SQLLEN t1 = 0, t2 = 0, t3 = 0;
  SQLBindCol(st, 1, SQL_C_SLONG, &tpId, sizeof(tpId), &t1);
  SQLBindCol(st, 2, SQL_C_SLONG, &prId, sizeof(prId), &t2);
  SQLBindCol(st, 3, SQL_C_WCHAR, nm, sizeof(nm), &t3);
  int n = 0;
  while (n < max && SQL_SUCCEEDED(SQLFetch(st))) {
    out[n].tp = (t1 == SQL_NULL_DATA) ? 0 : (long)tpId;
    out[n].prod = (t2 == SQL_NULL_DATA) ? 0 : (long)prId;
    out[n].name[0] = 0;
    if (t3 > 0) lstrcpynW(out[n].name, (wchar_t *)nm, PLM_COL1);
    n++;
  }
  SQLFreeHandle(SQL_HANDLE_STMT, st);
  return n;
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

/* Имена у изделия и его техпроцесса пишутся по-разному: у одного
   впереди буквенный код («АДЕ 3422-682.01.01.02»), у другого его нет, зато
   сзади висит «ТП». Приводим оба к голому обозначению, иначе сравнивать их
   бессмысленно. */
static void plm_core_des(const wchar_t *name, wchar_t *out, int cap) {
  wchar_t tmp[PLM_COL1];
  plm_designation(name, tmp, PLM_COL1);
  size_t l = wcslen(tmp);
  /* хвост «ТП» и похожие пометки в обозначение не входят */
  while (l > 2 && (tmp[l - 1] == L' ' || tmp[l - 1] == L'_' || tmp[l - 1] == L'-')) l--;
  if (l > 2 && _wcsnicmp(tmp + l - 2, L"ТП", 2) == 0) l -= 2;
  while (l > 0 && (tmp[l - 1] == L' ' || tmp[l - 1] == L'_' || tmp[l - 1] == L'-')) l--;
  tmp[l] = 0;
  /* буквенный код впереди — только если дальше идёт само число */
  const wchar_t *p = tmp;
  const wchar_t *sp = wcschr(p, L' ');
  if (sp && sp > p && sp - p <= 8) {
    BOOL letters = TRUE;
    for (const wchar_t *q = p; q < sp; q++)
      if (*q >= L'0' && *q <= L'9') letters = FALSE;
    const wchar_t *rest = sp;
    while (*rest == L' ') rest++;
    if (letters && *rest >= L'0' && *rest <= L'9') p = rest;
  }
  lstrcpynW(out, p, cap);
}

/* Одно ли это изделие. Точное совпадение или короткий хвост сверх —
   «-01», « вар.2» и тому подобное. Четырёх знаков мало, чтобы считать
   совпадение неслучайным. */
static BOOL plm_same_item(const wchar_t *a, const wchar_t *b) {
  wchar_t ca[PLM_COL1], cb[PLM_COL1];
  plm_core_des(a, ca, PLM_COL1);
  plm_core_des(b, cb, PLM_COL1);
  size_t la = wcslen(ca), lb = wcslen(cb);
  if (la < 5 || lb < 5) return FALSE;
  if (_wcsicmp(ca, cb) == 0) return TRUE;
  const wchar_t *lng = la >= lb ? ca : cb;
  const wchar_t *shr = la >= lb ? cb : ca;
  size_t ll = la >= lb ? la : lb, sl = la >= lb ? lb : la;
  if (ll - sl > 6) return FALSE;
  if (_wcsnicmp(lng, shr, sl) != 0) return FALSE;
  /* хвост вроде «-01» — это исполнение того же изделия. А вот лишняя
     точка — уже другая ступень состава: техпроцесс детали прилипал бы
     к строке сборки, в которую она входит. */
  for (size_t k = sl; k < ll; k++)
    if (lng[k] == L'.') return FALSE;
  return TRUE;
}

/* share.c: PLM через компьютер коллеги, если своего логина нет */
static BOOL share_client_on(void);
static BOOL share_lookup(const wchar_t *query, wchar_t *out, int cap);
static void share_card(long id, BOOL verbose, wchar_t *out, int cap);

/* Где у изделия лежат его карточки (заготовок, техсостава, ТП). Не в самом
   объекте из поиска: по PlmApi путь такой — объект (и его родитель) →
   ProductConfiguration → ProductInterconnectCard, а уже в ней ссылки на
   карточки. Перебираем все эти звенья: где нашлось, там и лежит. После
   макроса продолжается условие ON по nkp.Value. */
#define PLM_HOLDERS(ids)                                                                    \
  L"FROM (SELECT InfoObjectId AS Id, ISNULL(ParentId,0) AS Par "                           \
  L"FROM InfoObjects WITH(NOLOCK) WHERE InfoObjectId IN (" ids L")) AS x "                   \
  L"CROSS APPLY (SELECT x.Id AS H UNION SELECT x.Par "                                      \
  L"UNION SELECT hc.Link FROM InfoObjectAttributes AS hc WITH(NOLOCK) "                     \
  L"JOIN NameKeys AS nhc WITH(NOLOCK) ON nhc.NameKeyId=hc.NameKeyId "                       \
  L"AND nhc.Value=N'ProductConfiguration' "                                                 \
  L"WHERE hc.OwnerId=x.Id AND hc.Outdated=0 AND ISNULL(hc.Link,0)<>0 "                     \
  L"UNION SELECT hp.Link FROM InfoObjectAttributes AS hp WITH(NOLOCK) "                     \
  L"JOIN NameKeys AS nhp WITH(NOLOCK) ON nhp.NameKeyId=hp.NameKeyId "                       \
  L"AND nhp.Value=N'ProductConfiguration' "                                                 \
  L"WHERE hp.OwnerId=x.Par AND hp.Outdated=0 AND ISNULL(hp.Link,0)<>0) AS h1 "             \
  L"CROSS APPLY (SELECT h1.H AS H UNION SELECT hi.Link "                                    \
  L"FROM InfoObjectAttributes AS hi WITH(NOLOCK) "                                          \
  L"JOIN NameKeys AS nhi WITH(NOLOCK) ON nhi.NameKeyId=hi.NameKeyId "                       \
  L"AND nhi.Value=N'ProductInterconnectCard' "                                              \
  L"WHERE hi.OwnerId=h1.H AND hi.Outdated=0 AND ISNULL(hi.Link,0)<>0) AS h2 "              \
  L"JOIN InfoObjectAttributes AS pa WITH(NOLOCK) ON pa.OwnerId=h2.H AND pa.Outdated=0 "     \
  L"AND ISNULL(pa.Link,0)<>0 AND ISNULL(pa.CollectionElementId,0)=0 "                       \
  L"JOIN NameKeys AS nkp WITH(NOLOCK) ON nkp.NameKeyId=pa.NameKeyId "

/* Заготовки изделия: ProductPreformsCard → её дети (так их грузит PlmApi),
   а если в карточке есть коллекция ProductPreforms — и её элементы. */
/* Раньше тут было «ParentId=карточка ИЛИ номер в списке» одним условием — такое
   ИЛИ по разным столбцам сервер проходит перебором всей таблицы объектов, и
   поиск у раздающего не укладывался в ожидание коллеги. Две половины через
   UNION идут каждая по своему индексу. */
#define PF_APPLY(card)                                                                      \
  L"CROSS APPLY (SELECT o.InfoObjectId AS PfId, CAST(o.Name AS NVARCHAR(200)) AS PfName, "  \
  L"o.TemplateId AS PfT FROM InfoObjects AS o WITH(NOLOCK) "                                \
  L"WHERE o.ParentId=" card L" AND o.Erased=0 "                                             \
  L"UNION SELECT o2.InfoObjectId, CAST(o2.Name AS NVARCHAR(200)), o2.TemplateId "           \
  L"FROM InfoObjectAttributes AS la WITH(NOLOCK) "                                          \
  L"JOIN NameKeys AS nkl WITH(NOLOCK) ON nkl.NameKeyId=la.NameKeyId "                       \
  L"AND nkl.Value=N'ProductPreforms' "                                                      \
  L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId=la.AttributeId " \
  L"AND ce.Outdated=0 "                                                                     \
  L"JOIN InfoObjectAttributes AS ea WITH(NOLOCK) "                                          \
  L"ON ea.CollectionElementId=ce.CollectionElementId AND ea.DataType=6 "                    \
  L"JOIN InfoObjects AS o2 WITH(NOLOCK) ON o2.InfoObjectId=ea.Link AND o2.Erased=0 "        \
  L"WHERE la.OwnerId=" card L" AND la.Outdated=0) AS pf "

static BOOL plm_lookup(const wchar_t *query, wchar_t *out, int cap) {
  if (share_client_on()) return share_lookup(query, out, cap);
  g_plmLastLink[0] = 0;
  g_plmCount = 0;
  memset(g_plmTpId, 0, sizeof(g_plmTpId));
  memset(g_plmRealId, 0, sizeof(g_plmRealId));
  wchar_t pat[420];
  like_escape(query, pat, 420);
  wchar_t sql[3800];
  _snwprintf(
      sql, 3800,
      /* ищут обычно не целиком обозначение, а его начало: двадцать строк
         режут семейство пополам, и техпроцесс остаётся без своего изделия */
      L"SELECT TOP 60 "
      L"CASE WHEN o0.TemplateId=1794 AND ISNULL(o0.ParentId,0)<>0 "
      L"THEN o0.ParentId ELSE o0.InfoObjectId END AS OpenId, "
      L"o0.TemplateId, o0.Name, p.Name, o0.InfoObjectId "
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
  SQLINTEGER openId = 0, tmpl = 0, ownId = 0;
  SQLWCHAR nm[200], pnm[200];
  SQLLEN idInd = 0, tmInd = 0, nmInd = 0, pInd = 0, ownInd = 0;
  SQLBindCol(st, 1, SQL_C_SLONG, &openId, sizeof(openId), &idInd);
  SQLBindCol(st, 2, SQL_C_SLONG, &tmpl, sizeof(tmpl), &tmInd);
  SQLBindCol(st, 3, SQL_C_WCHAR, nm, sizeof(nm), &nmInd);
  SQLBindCol(st, 4, SQL_C_WCHAR, pnm, sizeof(pnm), &pInd);
  SQLBindCol(st, 5, SQL_C_SLONG, &ownId, sizeof(ownId), &ownInd);
  int n = 0;
  wchar_t links[4200] = {0};
  size_t linkLen = 0;
  /* та же беда, что и в карточке: SQL_SUCCESS_WITH_INFO обрывал выдачу */
  while (SQL_SUCCEEDED(SQLFetch(st)) && n < 60) {
    long oid = (idInd == SQL_NULL_DATA || openId == 0) ? 0 : (long)openId;
    make_plm_link(g_plmLinks[n], PLM_LINK, oid);
    g_plmIds[n] = oid;
    g_plmRealId[n] = (ownInd == SQL_NULL_DATA) ? 0 : (long)ownId;
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
    if (linkLen + (n ? 2 : 0) + add + 1 < 4200) {
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
     это одно и то же изделие. Сводим их в одну по ссылке из базы, а не по
     созвучию имён: хвосты у техпроцессов разные, и на них сведение разваливалось. */
  TpProd *tps = NULL;
  int tn = 0;
  {
    wchar_t ids[800];
    int idn = 0;
    size_t idl = 0;
    ids[0] = 0;
    for (int i = 0; i < n; i++) {
      if (g_plmTmpl[i] != 1794 || !g_plmRealId[i]) continue;
      wchar_t one[24];
      int k = _snwprintf(one, 24, idn ? L",%ld" : L"%ld", g_plmRealId[i]);
      if (k <= 0 || idl + (size_t)k + 1 >= 800) break;
      memcpy(ids + idl, one, ((size_t)k + 1) * sizeof(wchar_t));
      idl += (size_t)k;
      idn++;
    }
    if (idn) {
      tps = (TpProd *)calloc(64, sizeof(TpProd));
      if (tps) tn = plm_tp_products(dbc, ids, tps, 64);
    }
  }
  for (int i = 0; i < n; i++) {
    if (g_plmTmpl[i] != 1794 || !g_plmTp[i][0]) continue;
    long prod = 0;
    const wchar_t *pname = NULL;
    for (int k = 0; k < tn; k++) {
      if (tps[k].tp != g_plmRealId[i]) continue;
      prod = tps[k].prod;
      if (tps[k].name[0]) pname = tps[k].name;
      break;
    }
    /* изделие у техпроцесса теперь известно точно — показываем его
       в строке, даже если своей строки у изделия в находках нет */
    if (pname) lstrcpynW(g_plmEsi[i], pname, PLM_COL1);
    if (prod && !g_plmIds[i]) g_plmIds[i] = prod;
    int hit = -1;
    for (int j = 0; j < n && hit < 0; j++) {
      if (j == i || g_plmTmpl[j] == 1794 || g_plmTp[j][0] || !g_plmEsi[j][0]) continue;
      if (prod && (g_plmIds[j] == prod || g_plmRealId[j] == prod)) hit = j;
    }
    if (hit < 0 && pname) {
      /* ссылка может вести на другую версию того же изделия — тогда
         сводим по обозначению самого изделия, а не техпроцесса */
      for (int j = 0; j < n && hit < 0; j++) {
        if (j == i || g_plmTmpl[j] == 1794 || g_plmTp[j][0] || !g_plmEsi[j][0]) continue;
        if (plm_same_item(pname, g_plmEsi[j])) hit = j;
      }
    }
    if (hit < 0) {
      /* ссылки нет вовсе — остаётся сверка обозначений */
      for (int j = 0; j < n && hit < 0; j++) {
        if (j == i || g_plmTmpl[j] == 1794 || g_plmTp[j][0] || !g_plmEsi[j][0]) continue;
        if (plm_same_item(g_plmTp[i], g_plmEsi[j])) hit = j;
      }
    }
    if (hit < 0) continue;
    lstrcpynW(g_plmTp[hit], g_plmTp[i], PLM_COL2);
    /* номер техпроцесса нужен его собственный, а не родительский:
       раньше карточка брала из снимка чужой номер и операций не находила */
    g_plmTpId[hit] = g_plmRealId[i] ? g_plmRealId[i] : g_plmIds[i];
    g_plmTmpl[i] = -1;
  }
  free(tps);
  {
    int w = 0;
    for (int i = 0; i < n; i++) {
      if (g_plmTmpl[i] == -1) continue;
      if (w != i) {
        lstrcpynW(g_plmEsi[w], g_plmEsi[i], PLM_COL1);
        lstrcpynW(g_plmTp[w], g_plmTp[i], PLM_COL2);
        lstrcpynW(g_plmLinks[w], g_plmLinks[i], PLM_LINK);
        g_plmIds[w] = g_plmIds[i];
        g_plmTpId[w] = g_plmTpId[i];
        g_plmRealId[w] = g_plmRealId[i];
        g_plmTmpl[w] = g_plmTmpl[i];
      }
      w++;
    }
    n = w;
    /* текстовый список ссылок собирался до сведения и остался бы с уже
       убранными строками. Пересобираем по тому, что видно на экране. */
    linkLen = 0;
    links[0] = 0;
    for (int i = 0; i < n; i++) {
      size_t add = wcslen(g_plmLinks[i]);
      if (linkLen + (i ? 2 : 0) + add + 1 >= 4200) break;
      if (i) {
        links[linkLen++] = L'\r';
        links[linkLen++] = L'\n';
      }
      memcpy(links + linkLen, g_plmLinks[i], (add + 1) * sizeof(wchar_t));
      linkLen += add;
    }
    g_plmLastLink[0] = 0;
    if (n > 0) lstrcpynW(g_plmLastLink, g_plmLinks[0], PLM_LINK);
  }
  g_plmCount = n;
  SQLFreeHandle(SQL_HANDLE_STMT, st);
  /* столбец «Заготовка» заполняется потом, в фоне (pf_start_async): раньше
     поиск ждал его, и результат приходил на десятки секунд позже */
  for (int i = 0; i < n; i++) g_plmPf[i][0] = 0;
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
    char buf[2400];
    DWORD n = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &n, NULL);
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
        else if (!strcmp(line, "share") && val[0])
          MultiByteToWideChar(CP_UTF8, 0, val, -1, g_shareRoot, MAX_PATH);
        else if (!strcmp(line, "serve"))
          g_shareServe = atoi(val) != 0;
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
  char share[MAX_PATH * 3];
  WideCharToMultiByte(CP_UTF8, 0, g_shareRoot, -1, share, sizeof(share), NULL, NULL);
  char buf[400 + MAX_PATH * 3];
  snprintf(buf, sizeof(buf), "sql %s\nplm %s\nport %s\ndb %s\nuser %s\nshare %s\nserve %d\n",
           sql[0] ? sql : "UM-SQLSRV", plm[0] ? plm : "um-splmsrv",
           port[0] ? port : "4450", db[0] ? db : "-", user[0] ? user : "", share,
           g_shareServe ? 1 : 0);
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

/* предел в секундах для очередного запроса карточки; 0 — без предела */
static __thread int g_qTimeout; /* у каждого потока свой: карточка и фон не мешают */
static volatile LONG g_pfGen; /* номер задания столбца «Заготовка»: старые ответы отбрасываем */

static int card_query(SQLHDBC dbc, const wchar_t *sql, CardRow *rows, int max, wchar_t *err,
                      int ecap) {
  if (err && ecap) err[0] = 0;
  SQLHSTMT st = SQL_NULL_HSTMT;
  if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) return -1;
  if (g_qTimeout > 0)
    SQLSetStmtAttr(st, SQL_ATTR_QUERY_TIMEOUT, (SQLPOINTER)(SQLULEN)g_qTimeout, 0);
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
  /* перечисления и ссылки на шаблоны — словами (так их читает PlmApi) */                \
  L"WHEN 11 THEN (SELECT TOP 1 CAST(cvn.NameUI AS NVARCHAR(400)) "                          \
  L"FROM NamedValues AS cvn WITH(NOLOCK) "                                               \
  L"WHERE cvn.NamedValueId=a.Link) "                                                     \
  L"WHEN 22 THEN (SELECT TOP 1 CAST(cvt.NameUI AS NVARCHAR(400)) "                          \
  L"FROM Templates AS cvt WITH(NOLOCK) "                                                 \
  L"WHERE cvt.TemplateId=a.Link) "                                                       \
  L"WHEN 25 THEN (SELECT TOP 1 CAST(cvt.NameUI AS NVARCHAR(400)) "                          \
  L"FROM Templates AS cvt WITH(NOLOCK) "                                                 \
  L"WHERE cvt.TemplateId=a.Link) "                                                       \
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
    {L"PreformSize", L"Габариты заготовки"},
    {L"ZDiametr", L"Диаметр заготовки"},
    {L"ZLength", L"Длина заготовки"},
    {L"ZSizeAdd", L"Припуск"},
    {L"PreformExpense", L"Масса заготовки"},
    {L"Weight", L"Масса"},
    {L"Length", L"Длина"},
    {L"Width", L"Ширина"},
    {L"Height", L"Высота"},
    {L"Thickness", L"Толщина"},
    {L"Diameter", L"Диаметр"},
    {L"Size", L"Размер"},
    {L"Dimensions", L"Габариты"},
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

/* «0.7083 ч · 42.5 мин» одной строкой — для итогов */
static void card_total(CardOut *c, const wchar_t *pad, const wchar_t *title, double mins) {
  card_add(c, L"%s%-28s %.4f ч · %g мин\r\n", pad, title, mins / 60.0, mins);
}

#define CARD_ROWS 900
#define CARD_OPS 40 /* для скольких операций тянем все атрибуты */
/* Названия операций ТП по порядку — для режима «Для 1С». Карточка собирается
   в фоновом потоке, поэтому список два: поток пишет в черновой, а окно
   перекладывает его в готовый, когда получает текст карточки. */
#define OPS_1C 120
static wchar_t g_opsPend[OPS_1C][PLM_COL1];
static int g_opsPendN;
static wchar_t g_ops1c[OPS_1C][PLM_COL1];
static int g_ops1cN;

/* Для 1С нужно название из справочника операций (TSOperation), а не имя строки в
   ТП: имя строки бывает с номером или своим текстом. Нет ссылки на справочник —
   берём имя строки и срезаем с него ведущий номер вроде «005 ». */
static void ops_1c_add(const wchar_t *dirName, BOOL hasDir, const wchar_t *rowName) {
  if (g_opsPendN >= OPS_1C) return;
  const wchar_t *src = (hasDir && dirName && dirName[0]) ? dirName : rowName;
  if (!src || !src[0]) return;
  while (*src == L' ') src++;
  const wchar_t *p = src;
  int digits = 0;
  while (*p >= L'0' && *p <= L'9') {
    p++;
    digits++;
  }
  if (digits >= 2 && digits <= 4 && (*p == L' ' || *p == L'.' || *p == L'-')) {
    while (*p == L' ' || *p == L'.' || *p == L'-') p++;
    if (*p) src = p;
  }
  wchar_t *dst = g_opsPend[g_opsPendN];
  lstrcpynW(dst, src, PLM_COL1);
  int n = (int)wcslen(dst);
  while (n > 0 && dst[n - 1] == L' ') dst[--n] = 0;
  if (n) g_opsPendN++;
}

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
    ops_1c_add(o->s2, o->n3 != 0, o->s1);
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

/* Чертёж для строки находок ищется не в потоке окна: на двухстах
   тысячах файлов проход занимает долю секунды, а список должен
   прокручиваться без рывков. Номер запроса нужен, чтобы ответ на давно
   уже другую строку не пришёл к текущей. */
typedef struct {
  LONG gen;
  wchar_t key[PLM_COL1];
} DrawJob;

static DWORD WINAPI sel_draw_thread(LPVOID param) {
  DrawJob *job = (DrawJob *)param;
  wchar_t *res = (wchar_t *)calloc(PLM_LINK, sizeof(wchar_t));
  if (res) {
    /* индекс может читать и карточка в своём потоке; замок повторный,
       так что вложенные захваты внутри загрузки ничего не ломают */
    files_lock_init();
    files_lock();
    if (g_filesN == 0) files_load_idx();
    files_unlock();
    wchar_t key[PLM_COL1];
    lstrcpynW(key, job->key, PLM_COL1);
    CardFiles *f = (CardFiles *)malloc(sizeof(CardFiles));
    if (f) {
      memset(f, 0, sizeof(*f));
      /* чертёж назван по самой детали; не нашли — отрезаем хвост */
      for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt) {
          wchar_t *cut = wcsrchr(key, L'-');
          if (!cut || cut == key) break;
          *cut = 0;
        }
        card_scan_files(key, f);
        if (f->nd) break;
      }
      if (f->nd) lstrcpynW(res, f->draw[0], PLM_LINK);
      free(f);
    }
    if (!g_answer || !PostMessageW(g_answer, WM_SEL_DRAW, (WPARAM)job->gen, (LPARAM)res))
      free(res);
  }
  free(job);
  return 0;
}

static void request_row_draw(void) {
  g_selDraw[0] = 0;
  LONG gen = InterlockedIncrement(&g_drawGen);
  ans_sync_buttons();
  if (g_resultFiles || g_plmCount <= 0) return;
  int i = plm_selected_index();
  if (i < 0 || i >= g_plmCount) return;
  DrawJob *job = (DrawJob *)calloc(1, sizeof(DrawJob));
  if (!job) return;
  job->gen = gen;
  plm_core_des(g_plmEsi[i], job->key, PLM_COL1);
  if (wcslen(job->key) < 4) {
    free(job);
    return;
  }
  HANDLE th = CreateThread(NULL, 0, sel_draw_thread, job, 0, NULL);
  if (th) CloseHandle(th);
  else free(job);
}

/* Чертёж выбранной строки находок. У карточки своё окно и свой
   чертёж — они больше не перепутываются. */
static const wchar_t *ans_draw_file(void) {
  return g_selDraw[0] ? g_selDraw : NULL;
}

/* Нижний ряд находок всегда один и тот же. Чего сейчас нет — то потухшее,
   а не исчезнувшее: раньше кнопки прятались, ряд съезжал, и мышь
   попадала не туда, куда целилась. */
static void ans_sync_buttons(void) {
  if (!g_answer) return;
  HWND open = GetDlgItem(g_answer, ID_ANS_OPEN);
  HWND openTp = GetDlgItem(g_answer, ID_ANS_OPENTP);
  HWND card = GetDlgItem(g_answer, ID_ANS_CARD);
  HWND draw = GetDlgItem(g_answer, ID_ANS_DRAW);
  HWND show = GetDlgItem(g_answer, ID_ANS_SHOW);
  int i = plm_selected_index();
  BOOL row = i >= 0 && i < g_plmCount;
  const wchar_t *file = ans_draw_file();
  BOOL haveFile = file && file[0];
  if (open) {
    SetWindowTextW(open, g_resultFiles ? L"Открыть файл" : L"Открыть ЭСИ в СОЮЗ");
    EnableWindow(open, row || (!g_resultFiles && g_ansObj[0] != 0));
  }
  /* у найденных файлов ТП нет — кнопку прячет раскладка */
  if (openTp) EnableWindow(openTp, plm_row_tp(i) != 0);
  if (card) EnableWindow(card, row && !g_resultFiles && g_plmIds[i] != 0);
  if (draw) EnableWindow(draw, haveFile && !g_resultFiles);
  if (show) EnableWindow(show, (g_resultFiles && row) || (!g_resultFiles && haveFile));
  layout_answer();
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

/* ---- заготовка: материал, габариты, масса ---------------------------------
   Где именно в заготовке лежат эти три вещи, схема не говорит: в самом
   объекте, в его списке, во вложенном объекте или по ссылке на материал.
   Поэтому заготовку обходим вглубь (сама, её дети, её списки, ссылки на
   материал и версии) и собираем поля по названию. Весь обход виден в
   «Атрибутах» (Shift + «Все данные») — по нему, если что-то не нашлось,
   видно, куда смотреть. */
typedef struct {
  wchar_t mat[200], mass[80], dims[240];
  wchar_t sort[240]; /* сортамент: «Круг 45 ГОСТ 2590-2006 / 38ХС ГОСТ 4543-2016» */
  BOOL dimsFixed, massFixed; /* нашлись PreformSize / PreformExpense — другие не нужны */
  wchar_t zd[40], zl[40], za[40]; /* ZDiametr, ZLength, ZSizeAdd (припуск) */
  int sortScore;
  int ndims;
  long seen[30];
  int nseen;
  int queries;
  ULONGLONG deadline; /* 0 — без предела */
} PfSum;

/* Значение для обхода заготовки: как в карточке, а если по типу не вышло —
   любое число или текст, что есть в строке (так читается и время операций:
   поле Value внутри составного атрибута бывает и не тех типов, что в CASE). */
#define PF_VALUE_SQL                                                                        \
  L"COALESCE(lo.Name, NULLIF(" CARD_VALUE_SQL L", N''), "                                    \
  L"CONVERT(NVARCHAR(64), a.FloatNumber), CONVERT(NVARCHAR(64), a.IntegerNumber), "          \
  L"CASE WHEN a.DataType<>6 THEN CONVERT(NVARCHAR(64), a.LongNumber) END, a.ShortText, N'')"

static BOOL pf_late(const PfSum *sm) {
  return sm->deadline && GetTickCount64() > sm->deadline;
}

static void pf_lower(const wchar_t *in, wchar_t *out, int cap) {
  lstrcpynW(out, in, cap);
  /* towlower в «C»-локали кириллицу не трогает — «Круг» остался бы «Круг» */
  CharLowerBuffW(out, (DWORD)wcslen(out));
}

/* 1 материал, 2 масса, 3 размер, 0 прочее */
static int pf_kind(const wchar_t *key) {
  wchar_t k[128];
  pf_lower(key, k, 128);
  if (wcsstr(k, L"unit") || wcsstr(k, L"substitute") || wcsstr(k, L"measure") ||
      wcsstr(k, L"единиц") || wcsstr(k, L"заменит"))
    return 0;
  if (wcsstr(k, L"material") || wcsstr(k, L"материал") || wcsstr(k, L"марка")) return 1;
  if (wcsstr(k, L"mass") || wcsstr(k, L"weight") || wcsstr(k, L"масс") || !wcscmp(k, L"вес") ||
      wcsstr(k, L"вес заг"))
    return 2;
  static const wchar_t *ru[] = {L"длин", L"ширин", L"высот", L"толщин", L"диаметр", L"размер", L"габарит"};
  for (size_t i = 0; i < sizeof(ru) / sizeof(ru[0]); i++)
    if (wcsstr(k, ru[i])) return 3;
  static const wchar_t *dims[] = {L"length", L"width",     L"height", L"thick",
                                  L"diam",   L"dimension", L"gabar",  L"size"};
  for (size_t i = 0; i < sizeof(dims) / sizeof(dims[0]); i++)
    if (wcsstr(k, dims[i])) return 3;
  return 0;
}

/* Похоже ли значение на сортамент: начинается с вида проката и в нём есть
   цифры («Круг 45 ГОСТ 2590-2006 / 38ХС ГОСТ 4543-2016», «Лист 4 …»).
   Где такое лежит — в названии заготовки, в ссылке на материал или в его
   карточке, — схема не говорит, поэтому смотрим на все значения подряд. */
static int pf_sort_score(const wchar_t *key, const wchar_t *val) {
  if (!val || !val[0]) return 0;
  while (*val == L' ') val++;
  wchar_t v[64];
  pf_lower(val, v, 64);
  static const wchar_t *kinds[] = {L"круг",     L"лист",     L"полоса",   L"квадрат",  L"шестигранник",
                                   L"труба",    L"пруток",   L"уголок",   L"швеллер",  L"лента",
                                   L"проволока", L"двутавр", L"балка",    L"профиль",  L"плита",
                                   L"катанка",  L"шина",     L"арматура", L"отливка",  L"поковка",
                                   L"штамповка", L"прокат",  L"сетка",    L"рулон"};
  BOOL digit = FALSE;
  for (const wchar_t *q = val; *q && !digit; q++) digit = *q >= L'0' && *q <= L'9';
  for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
    size_t l = wcslen(kinds[i]);
    if (!wcsncmp(v, kinds[i], l) && (v[l] == L' ' || (v[l] >= L'0' && v[l] <= L'9')) && digit)
      return wcschr(val, L'/') ? 4 : 3; /* с «/» — уже и сортамент, и марка */
  }
  if (key) {
    wchar_t k[128];
    pf_lower(key, k, 128);
    if (wcsstr(k, L"sortament") || wcsstr(k, L"assortment") || wcsstr(k, L"rolled")) return 1;
  }
  return 0;
}

static void pf_sort_candidate(PfSum *sm, const wchar_t *key, const wchar_t *val) {
  int sc = pf_sort_score(key, val);
  if (sc <= sm->sortScore) return;
  while (*val == L' ') val++;
  lstrcpynW(sm->sort, val, 240);
  sm->sortScore = sc;
}

/* сортамент, размеры и масса найдены — для сводки обход можно не продолжать
   (одной марки мало: «Круг 45 …» может лежать глубже, в карточке материала) */
static BOOL pf_complete(const PfSum *sm) {
  return sm->sortScore >= 3 && sm->mass[0] && (sm->zl[0] || (sm->ndims > 0 && !sm->zd[0]));
}

static void pf_take(PfSum *sm, const wchar_t *key, const wchar_t *val) {
  if (!val || !val[0] || !wcscmp(val, L"0") || !wcscmp(val, L"нет")) return;
  /* Где у заготовки габариты и масса, известно точно: PreformSize и
     PreformExpense (масса — в его строке Value). Они главнее любых других
     размеров и масс, найденных при обходе (у материала, у изделия). */
  /* диаметр, длина и припуск заготовки — свои поля, их собираем отдельно */
  if (!_wcsicmp(key, L"ZDiametr") || !_wcsicmp(key, L"ZDiameter")) {
    if (!sm->zd[0]) lstrcpynW(sm->zd, val, 40);
    return;
  }
  if (!_wcsicmp(key, L"ZLength")) {
    if (!sm->zl[0]) lstrcpynW(sm->zl, val, 40);
    return;
  }
  if (!_wcsicmp(key, L"ZSizeAdd")) {
    if (!sm->za[0]) lstrcpynW(sm->za, val, 40);
    return;
  }
  if (!_wcsicmp(key, L"PreformSize")) {
    if (!sm->dimsFixed) {
      _snwprintf(sm->dims, 240, L"Габариты %s", val);
      sm->dims[239] = 0;
      sm->ndims = 1;
      sm->dimsFixed = TRUE;
    }
    return;
  }
  if (!_wcsicmp(key, L"PreformExpense")) {
    if (!sm->massFixed) {
      lstrcpynW(sm->mass, val, 80);
      sm->massFixed = TRUE;
    }
    return;
  }
  int k = pf_kind(key);
  if ((k == 2 && sm->massFixed) || (k == 3 && sm->dimsFixed)) return;
  const wchar_t *lab = card_label(key);
  if (k == 1 && !sm->mat[0]) lstrcpynW(sm->mat, val, 200);
  if (k == 2 && !sm->mass[0]) lstrcpynW(sm->mass, val, 80);
  if (k == 3 && sm->ndims < 4) {
    size_t l = wcslen(sm->dims);
    _snwprintf(sm->dims + l, 240 - l, L"%s%s %s", l ? L" · " : L"", lab ? lab : key, val);
    sm->dims[239] = 0;
    sm->ndims++;
  }
}

static BOOL pf_follow_link(const wchar_t *key) {
  wchar_t k[128];
  pf_lower(key, k, 128);
  return wcsstr(k, L"preform") || wcsstr(k, L"material") || wcsstr(k, L"actualversion") ||
         wcsstr(k, L"mainvariant") || wcsstr(k, L"blank");
}

/* c == NULL — только собрать сводку, ничего не печатая */
static void pf_explore(SQLHDBC dbc, long id, int depth, CardOut *c, const wchar_t *pad, PfSum *sm) {
  if (!id || sm->nseen >= 30 || sm->queries > 60 || pf_late(sm)) return;
  for (int i = 0; i < sm->nseen; i++)
    if (sm->seen[i] == id) return;
  sm->seen[sm->nseen++] = id;
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 300);
  CardRow *el = (CardRow *)malloc(sizeof(CardRow) * 300);
  wchar_t *sql = (wchar_t *)malloc(4000 * sizeof(wchar_t));
  long *follow = (long *)malloc(sizeof(long) * 40);
  if (!rows || !el || !sql || !follow) goto out;
  int nf = 0;
  wchar_t err[280];
  wchar_t pad2[40];
  _snwprintf(pad2, 40, L"%s    ", pad);
  pad2[39] = 0;

  _snwprintf(sql, 4000,
             L"SELECT TOP 300 a.AttributeId, nk.Value, " PF_VALUE_SQL L", "
             L"ISNULL(a.Link,0), a.DataType "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE a.OwnerId=%ld AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0 "
             L"ORDER BY nk.Value",
             id);
  sm->queries++;
  int n = card_query(dbc, sql, rows, 300, err, 280);
  for (int i = 0; i < n; i++) {
    pf_sort_candidate(sm, rows[i].s1, rows[i].s2);
    if (card_hidden(rows[i].s1)) continue;
    if (rows[i].n3 != 8 && rows[i].n3 != 23) pf_take(sm, rows[i].s1, rows[i].s2); /* у составных число — в строках */
    if (c && rows[i].s2[0]) card_pair_t(c, pad, rows[i].s1, rows[i].s2, 0, rows[i].n3);
    if (rows[i].n3 == 6 && rows[i].n2 && nf < 40 && pf_follow_link(rows[i].s1)) follow[nf++] = rows[i].n2;
  }
  /* списки и составные атрибуты: строка за строкой */
  for (int i = 0; i < n && sm->queries <= 60 && !pf_late(sm); i++) {
    BOOL known = !_wcsicmp(rows[i].s1, L"PreformSize") || !_wcsicmp(rows[i].s1, L"PreformExpense") ||
                 !_wcsicmp(rows[i].s1, L"ZDiametr") || !_wcsicmp(rows[i].s1, L"ZLength") ||
                 !_wcsicmp(rows[i].s1, L"ZSizeAdd");
    if (!known && ((rows[i].n3 != 8 && rows[i].n3 != 23) || card_hidden(rows[i].s1))) continue;
    _snwprintf(sql, 4000,
               L"SELECT TOP 300 a.CollectionElementId, nk.Value, " PF_VALUE_SQL
               L", ISNULL(a.Link,0), a.DataType "
               L"FROM InfoObjectCollectionElements AS ce WITH(NOLOCK) "
               L"JOIN InfoObjectAttributes AS a WITH(NOLOCK) "
               L"ON a.CollectionElementId=ce.CollectionElementId AND a.Outdated=0 "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
               L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
               L"WHERE ce.AttributeId IN (%ld,%ld) AND ce.Outdated=0 AND nk.Value<>N'LastChanged' "
               L"AND NOT EXISTS (SELECT 1 FROM InfoObjectAttributes AS ir WITH(NOLOCK) "
               L"JOIN NameKeys AS nkr WITH(NOLOCK) ON nkr.NameKeyId=ir.NameKeyId "
               L"AND nkr.Value=N'IsRemoved' "
               L"WHERE ir.CollectionElementId=ce.CollectionElementId AND ir.BoolValue=1) "
               L"ORDER BY a.CollectionElementId, a.DataType DESC, nk.Value",
               rows[i].n1, rows[i].n2 ? rows[i].n2 : rows[i].n1);
    sm->queries++;
    int m = card_query(dbc, sql, el, 300, err, 280);
    if (m <= 0) continue;
    if (c) {
      const wchar_t *lab = card_label(rows[i].s1);
      card_add(c, L"%s[%s]\r\n", pad, lab ? lab : rows[i].s1);
    }
    long cur = -1;
    int shown = 0;
    const wchar_t *valKey = rows[i].s1; /* чьё значение лежит в поле Value этой строки */
    for (int e = 0; e < m; e++) {
      if (el[e].n1 != cur) {
        if (c && shown) card_add(c, L"\r\n");
        cur = el[e].n1;
        shown = 0;
        if (c) card_add(c, L"%s- ", pad2);
        /* Составной атрибут (Масса, Длина…): число в его строке под именем
           Value — значит, это масса или длина. В списке параметров имя
           параметра — в соседнем поле строки (Name, Parameter…). */
        valKey = rows[i].s1;
        /* у массы, длины и т.п. имя уже известно — подпись строки его не меняет */
        for (int q = e; q < m && el[q].n1 == cur && pf_kind(rows[i].s1) == 0 && !known; q++) {
          wchar_t kq[64];
          pf_lower(el[q].s1, kq, 64);
          if (el[q].s2[0] && (!wcscmp(kq, L"name") || wcsstr(kq, L"parameter") || wcsstr(kq, L"characteristic") ||
                              wcsstr(kq, L"property")))
            valKey = el[q].s2;
        }
      }
      if (!el[e].s2[0]) continue;
      pf_sort_candidate(sm, el[e].s1, el[e].s2);
      if (card_hidden(el[e].s1)) continue;
      if (!_wcsicmp(el[e].s1, L"Value") || !_wcsicmp(el[e].s1, L"NumberValue") ||
          !_wcsicmp(el[e].s1, L"DoubleValue"))
        pf_take(sm, valKey, el[e].s2);
      else
        pf_take(sm, el[e].s1, el[e].s2);
      const wchar_t *lab = card_label(el[e].s1);
      if (c) card_add(c, L"%s%s %s", shown ? L" · " : L"", lab ? lab : el[e].s1, el[e].s2);
      shown++;
      /* строка списка часто ссылается на саму заготовку или материал */
      if (el[e].n3 == 6 && el[e].n2 && nf < 40) follow[nf++] = el[e].n2;
    }
    if (c && shown) card_add(c, L"\r\n");
  }
  if (depth <= 0 || pf_late(sm) || (!c && pf_complete(sm))) goto out;
  /* вложенные объекты (версии, сами заготовки) */
  _snwprintf(sql, 4000,
             L"SELECT TOP 15 o.InfoObjectId, CAST(o.Name AS NVARCHAR(200)), "
             L"CAST(t.NameUI AS NVARCHAR(200)), 0, 0 FROM InfoObjects AS o WITH(NOLOCK) "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId "
             L"WHERE o.ParentId=%ld AND o.Erased=0 ORDER BY o.InfoObjectId",
             id);
  sm->queries++;
  int k = card_query(dbc, sql, el, 300, err, 280);
  for (int i = 0; i < k; i++) {
    pf_sort_candidate(sm, NULL, el[i].s1);
    if (!c && pf_complete(sm)) break; /* для сводки всё уже есть — дальше не ходим */
    if (c) card_add(c, L"%s> %s  (%s)\r\n", pad, el[i].s1[0] ? el[i].s1 : L"без имени", el[i].s2);
    long child = el[i].n1;
    pf_explore(dbc, child, depth - 1, c, pad2, sm);
  }
  for (int i = 0; i < nf; i++) {
    if (!c && pf_complete(sm)) break;
    if (c) card_add(c, L"%s> по ссылке\r\n", pad);
    pf_explore(dbc, follow[i], depth - 1, c, pad2, sm);
  }
out:
  free(rows);
  free(el);
  free(sql);
  free(follow);
}

/* «45.000» → «45», «2.5» → «2,5»; не число — как есть */
static void pf_num(const wchar_t *v, wchar_t *out, int cap) {
  wchar_t tmp[64], *end = NULL;
  lstrcpynW(tmp, v, 64);
  for (wchar_t *q = tmp; *q; q++)
    if (*q == L',') *q = L'.';
  double d = wcstod(tmp, &end);
  while (end && *end == L' ') end++;
  if (!end || end == tmp || *end) {
    lstrcpynW(out, v, cap);
    return;
  }
  _snwprintf(out, cap, L"%.3f", d);
  out[cap - 1] = 0;
  size_t l = wcslen(out);
  while (l > 0 && out[l - 1] == L'0') out[--l] = 0;
  if (l > 0 && out[l - 1] == L'.') out[--l] = 0;
  wchar_t *dot = wcschr(out, L'.');
  if (dot) *dot = L',';
}

/* Габариты: из ZDiametr / ZLength / ZSizeAdd, если они есть, — «Ø45×L118,
   припуск 3»; иначе то, что нашлось (PreformSize или поля с размерами). */
static void pf_dims_text(const PfSum *sm, wchar_t *out, int cap) {
  out[0] = 0;
  if (!sm->zd[0] && !sm->zl[0]) {
    lstrcpynW(out, sm->dims, cap);
    return;
  }
  wchar_t d[40] = L"", l[40] = L"", a[40] = L"";
  if (sm->zd[0]) pf_num(sm->zd, d, 40);
  if (sm->zl[0]) pf_num(sm->zl, l, 40);
  if (sm->za[0]) pf_num(sm->za, a, 40);
  int n = _snwprintf(out, cap, L"Габариты %s%s%s%s%s", d[0] ? L"Ø" : L"", d, d[0] && l[0] ? L"×" : L"",
                     l[0] ? L"L" : L"", l);
  if (a[0] && wcscmp(a, L"0") && n > 0 && n < cap) _snwprintf(out + n, cap - n, L", припуск %s", a);
  out[cap - 1] = 0;
}

/* «Круг 45 ГОСТ 2590-2006 / 38ХС ГОСТ 4543-2016»: сортамент и марка вместе;
   если одно уже содержит другое — без повтора */
static void pf_material_text(const PfSum *sm, wchar_t *out, int cap) {
  out[0] = 0;
  const wchar_t *so = sm->sortScore >= 1 ? sm->sort : L"", *ma = sm->mat;
  wchar_t a[240], b[200];
  pf_lower(so, a, 240);
  pf_lower(ma, b, 200);
  if (so[0] && ma[0] && !wcsstr(a, b) && !wcsstr(b, a))
    _snwprintf(out, cap, L"%s / %s", so, ma);
  else if (so[0] && (!ma[0] || wcsstr(a, b)))
    lstrcpynW(out, so, cap);
  else
    lstrcpynW(out, ma, cap);
  out[cap - 1] = 0;
}

/* масса числом — «1,52 кг»; если в ней уже есть буквы (единицы), как есть */
static void pf_mass_text(const wchar_t *m, wchar_t *out, int cap) {
  out[0] = 0;
  if (!m[0]) return;
  wchar_t *end = NULL;
  wchar_t tmp[80];
  lstrcpynW(tmp, m, 80);
  for (wchar_t *q = tmp; *q; q++)
    if (*q == L',') *q = L'.';
  double v = wcstod(tmp, &end);
  while (end && *end == L' ') end++;
  if (!end || end == tmp || *end) {
    lstrcpynW(out, m, cap);
    return;
  }
  _snwprintf(out, cap, v < 10 ? L"%.3f" : (v < 100 ? L"%.2f" : L"%.1f"), v);
  out[cap - 1] = 0;
  wchar_t *dot = wcschr(out, L'.');
  if (dot) {
    size_t l = wcslen(out);
    while (l > 0 && out[l - 1] == L'0') out[--l] = 0;
    if (l > 0 && out[l - 1] == L'.') out[--l] = 0;
    dot = wcschr(out, L'.');
    if (dot) *dot = L',';
  }
  size_t l = wcslen(out);
  _snwprintf(out + l, cap - l, L" кг");
  out[cap - 1] = 0;
}

static void pf_summary_text(const PfSum *sm, wchar_t *out, int cap) {
  out[0] = 0;
  int l = 0;
  wchar_t mt[480], ms[80];
  pf_material_text(sm, mt, 480);
  pf_mass_text(sm->mass, ms, 80);
  if (mt[0]) l += _snwprintf(out + l, cap - l, L"%s", mt);
  wchar_t dt[240];
  pf_dims_text(sm, dt, 240);
  if (dt[0] && l >= 0 && l < cap) l += _snwprintf(out + l, cap - l, L"%s%s", l ? L" · " : L"", dt);
  if (ms[0] && l >= 0 && l < cap) _snwprintf(out + l, cap - l, L"%s%s", l ? L" · " : L"", ms);
  out[cap - 1] = 0;
}

/* Коротко для столбца: без номеров ГОСТ/ТУ, размеры через «×», масса как есть.
   «Круг 45 ГОСТ 2590-2006 / 38ХС ГОСТ 4543-2016 · Диаметр 45 · Длина 118 · 1,49 кг»
   → «Круг 45 / 38ХС · Ø45×118 · 1,49 кг». Строка столбца узкая, а целиком
   такое не помещалось и обрезалось на середине сортамента. */
static BOOL pf_is_std(const wchar_t *tok) {
  wchar_t t[16];
  pf_lower(tok, t, 16);
  static const wchar_t *std[] = {L"гост", L"ост", L"сто", L"ту", L"din", L"iso", L"en"};
  for (size_t i = 0; i < sizeof(std) / sizeof(std[0]); i++) {
    size_t l = wcslen(std[i]);
    if (!wcsncmp(t, std[i], l) && (!t[l] || (t[l] >= L'0' && t[l] <= L'9') || t[l] == L'-')) return TRUE;
  }
  return FALSE;
}

static void pf_strip_std(const wchar_t *in, wchar_t *out, int cap) {
  wchar_t buf[600];
  int b = 0;
  for (const wchar_t *p = in; *p && b < 590; p++) { /* «/» — отдельным словом */
    if (*p == L'/') {
      buf[b++] = L' ';
      buf[b++] = L'/';
      buf[b++] = L' ';
    } else {
      buf[b++] = *p;
    }
  }
  buf[b] = 0;
  out[0] = 0;
  int ol = 0;
  BOOL skipping = FALSE;
  wchar_t *ctx = NULL;
  for (wchar_t *tok = wcstok_s(buf, L" ", &ctx); tok; tok = wcstok_s(NULL, L" ", &ctx)) {
    BOOL digit = FALSE;
    for (const wchar_t *q = tok; *q && !digit; q++) digit = *q >= L'0' && *q <= L'9';
    if (pf_is_std(tok)) {
      skipping = TRUE;
      continue;
    }
    if (skipping && (digit || !wcscmp(tok, L"Р") || !wcscmp(tok, L"р"))) continue;
    skipping = FALSE;
    if (ol && !wcscmp(tok, L"/") && out[ol - 1] == L'/') continue;
    ol += _snwprintf(out + ol, cap - ol, L"%s%s", ol ? L" " : L"", tok);
    if (ol >= cap - 1) break;
  }
  /* «/» в конце (марки за ним не было) не нужен */
  while (ol >= 2 && (out[ol - 1] == L'/' || out[ol - 1] == L' ')) out[--ol] = 0;
  out[cap - 1] = 0;
}

static void pf_short_one(const wchar_t *seg, wchar_t *out, int cap) {
  wchar_t mat[300] = L"", dims[120] = L"", mass[60] = L"", dim1[16] = L"";
  int nd = 0;
  const wchar_t *p = seg;
  while (p && *p) {
    const wchar_t *sep = wcsstr(p, L" · ");
    size_t len = sep ? (size_t)(sep - p) : wcslen(p);
    wchar_t part[300];
    if (len > 299) len = 299;
    memcpy(part, p, len * sizeof(wchar_t));
    part[len] = 0;
    wchar_t low[300];
    pf_lower(part, low, 300);
    size_t pl = wcslen(part);
    if (pl > 3 && !wcscmp(part + pl - 3, L" кг")) {
      lstrcpynW(mass, part, 60);
    } else if (pf_kind(part) == 3 && wcsrchr(part, L' ')) {
      /* значение — с первой цифры или значка диаметра: «Габариты Ø45 x 118» */
      const wchar_t *v = part;
      while (*v && !(*v >= L'0' && *v <= L'9') && *v != 0x00D8 && *v != 0x00F8 && *v != 0x2205) v++;
      if (!*v) v = wcsrchr(part, L' ') + 1;
      if (wcsstr(low, L"габарит")) { /* уже целиком, со своими «×» */
        lstrcpynW(dims, v, 120);
        nd = 2;
        p = sep ? sep + 3 : NULL;
        continue;
      }
      if (!nd++) /* одна длина без подписи непонятна — ей буква: L=120 */
        lstrcpynW(dim1, wcsstr(low, L"длин") ? L"L=" : wcsstr(low, L"толщ") ? L"s=" :
                        wcsstr(low, L"шир") ? L"B=" : wcsstr(low, L"выс") ? L"H=" : L"", 16);
      size_t dl = wcslen(dims);
      _snwprintf(dims + dl, 120 - dl, L"%s%s%s", dl ? L"×" : L"", wcsstr(low, L"диам") ? L"Ø" : L"", v);
      dims[119] = 0;
    } else if (!wcsncmp(low, L"масса ", 6)) {
      pf_mass_text(part + 6, mass, 60);
    } else {
      wchar_t st[300];
      pf_strip_std(part, st, 300);
      size_t ml = wcslen(mat);
      if (st[0]) _snwprintf(mat + ml, 300 - ml, L"%s%s", ml ? L" · " : L"", st);
      mat[299] = 0;
    }
    p = sep ? sep + 3 : NULL;
  }
  if (nd == 1 && dim1[0]) {
    wchar_t t[120];
    _snwprintf(t, 120, L"%s%s", dim1, dims);
    t[119] = 0;
    lstrcpynW(dims, t, 120);
  }
  int l = 0;
  out[0] = 0;
  const wchar_t *pieces[3] = {mat, dims, mass};
  for (int i = 0; i < 3; i++)
    if (pieces[i][0] && l >= 0 && l < cap) l += _snwprintf(out + l, cap - l, L"%s%s", l ? L" · " : L"", pieces[i]);
  out[cap - 1] = 0;
}

static void pf_short(const wchar_t *full, wchar_t *out, int cap) {
  out[0] = 0;
  if (!full || !full[0]) return;
  if (!wcscmp(full, L"…")) {
    lstrcpynW(out, full, cap);
    return;
  }
  int l = 0;
  const wchar_t *p = full;
  while (p && *p && l >= 0 && l < cap - 1) { /* несколько заготовок — через «; » */
    const wchar_t *sep = wcsstr(p, L"; ");
    size_t len = sep ? (size_t)(sep - p) : wcslen(p);
    wchar_t seg[PLM_COL1], one[PLM_COL1];
    if (len >= PLM_COL1) len = PLM_COL1 - 1;
    memcpy(seg, p, len * sizeof(wchar_t));
    seg[len] = 0;
    pf_short_one(seg, one, PLM_COL1);
    l += _snwprintf(out + l, cap - l, L"%s%s", l ? L"; " : L"", one[0] ? one : seg);
    p = sep ? sep + 2 : NULL;
  }
  out[cap - 1] = 0;
}

/* Столбец «Заготовка» считается в фоне уже после того, как находки показаны.
   Задание — список изделий; ответ — текст для каждого. */
#define PF_JOB_MAX 60
typedef struct {
  LONG gen;
  int n;
  long ids[PF_JOB_MAX];
  wchar_t text[PF_JOB_MAX][PLM_COL1];
} PfJob;

static void pf_compute(SQLHDBC dbc, PfJob *j, ULONGLONG budgetMs) {
  if (j->n <= 0) return;
  ULONGLONG t0 = GetTickCount64();
  wchar_t ids[1600];
  size_t il = 0;
  ids[0] = 0;
  for (int k = 0; k < j->n; k++) {
    wchar_t one[24];
    int w = _snwprintf(one, 24, il ? L",%ld" : L"%ld", j->ids[k]);
    if (w <= 0 || il + (size_t)w + 1 >= 1600) break;
    memcpy(ids + il, one, ((size_t)w + 1) * sizeof(wchar_t));
    il += (size_t)w;
  }
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 400);
  wchar_t *sql = (wchar_t *)malloc(4000 * sizeof(wchar_t));
  long *pk = (long *)malloc(sizeof(long) * 400), *pid = (long *)malloc(sizeof(long) * 400);
  if (!rows || !sql || !pk || !pid) goto out;
  _snwprintf(sql, 4000,
             L"SELECT TOP 400 x.Id, pf.PfName, N'', pf.PfId, 0 "
             PLM_HOLDERS(L"%s") L"AND nkp.Value=N'ProductPreformsCard' "
             PF_APPLY(L"pa.Link")
             L"ORDER BY x.Id, pf.PfId",
             ids);
  sql[3999] = 0;
  g_qTimeout = 8;
  wchar_t err[280];
  int n = card_query(dbc, sql, rows, 400, err, 280);
  int np = 0;
  for (int r = 0; r < n; r++) {
    int k = -1;
    for (int q = 0; q < j->n && k < 0; q++)
      if (j->ids[q] == rows[r].n1) k = q;
    if (k < 0) continue;
    wchar_t *d = j->text[k];
    if (rows[r].s1[0] && !wcsstr(d, rows[r].s1)) {
      size_t dl = wcslen(d);
      _snwprintf(d + dl, PLM_COL1 - dl, L"%s%s", dl ? L"; " : L"", rows[r].s1);
      d[PLM_COL1 - 1] = 0;
    }
    if (np < 400) {
      pk[np] = k;
      pid[np] = rows[r].n2;
      np++;
    }
  }
  /* вместо названий — материал, габариты и масса, где их удалось найти */
  g_qTimeout = 3;
  for (int k = 0; k < j->n; k++) {
    if (GetTickCount64() - t0 > budgetMs) break;
    wchar_t line[PLM_COL1];
    line[0] = 0;
    int parts = 0;
    for (int p = 0; p < np && parts < 3; p++) {
      if (pk[p] != k) continue;
      PfSum *sm = (PfSum *)calloc(1, sizeof(PfSum));
      if (!sm) break;
      sm->deadline = t0 + budgetMs;
      pf_explore(dbc, pid[p], 3, NULL, L"", sm);
      wchar_t one[PLM_COL1];
      pf_summary_text(sm, one, PLM_COL1);
      free(sm);
      if (!one[0]) continue;
      size_t l = wcslen(line);
      _snwprintf(line + l, PLM_COL1 - l, L"%s%s", l ? L"; " : L"", one);
      line[PLM_COL1 - 1] = 0;
      parts++;
    }
    if (line[0]) lstrcpynW(j->text[k], line, PLM_COL1);
  }
out:
  g_qTimeout = 0;
  free(rows);
  free(sql);
  free(pk);
  free(pid);
}

/* Заготовка в карточке: сводка (материал, габариты, масса) у каждой; в
   «Атрибутах» — ещё и весь обход, по которому сводка собиралась. */
static void card_preforms(SQLHDBC dbc, long pfCard, CardOut *c, CardRow *rows, wchar_t *err) {
  wchar_t card[24];
  _snwprintf(card, 24, L"%ld", pfCard);
  wchar_t *sql = (wchar_t *)malloc(4000 * sizeof(wchar_t));
  if (!sql) return;
  _snwprintf(sql, 4000,
             L"SELECT TOP 10 pf.PfId, pf.PfName, CAST(t.NameKey AS NVARCHAR(200)), 0, 0 "
             L"FROM (SELECT %s AS CardId) AS k " PF_APPLY(L"k.CardId")
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=pf.PfT "
             L"ORDER BY pf.PfId",
             card);
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  free(sql);
  if (n < 0) {
    card_add(c, L"ЗАГОТОВКА: запрос не выполнился.\r\n%s\r\n\r\n", err);
    return;
  }
  if (n == 0) {
    card_add(c, L"ЗАГОТОВКА: карточка заготовок есть (%ld), но заготовок в ней нет.\r\n\r\n", pfCard);
    return;
  }
  long ids[10];
  wchar_t names[10][200];
  int pn = n > 10 ? 10 : n;
  for (int i = 0; i < pn; i++) {
    ids[i] = rows[i].n1;
    lstrcpynW(names[i], rows[i].s1[0] ? rows[i].s1 : L"(без имени)", 200);
  }
  card_add(c, L"%s\r\n", pn > 1 ? L"ЗАГОТОВКИ" : L"ЗАГОТОВКА");
  /* на все заготовки карточки — не больше 20 с, на одну — 8 */
  ULONGLONG cardEnd = GetTickCount64() + 20000;
  for (int p = 0; p < pn; p++) {
    PfSum *sm = (PfSum *)calloc(1, sizeof(PfSum));
    if (!sm) break;
    ULONGLONG one = GetTickCount64() + 8000;
    sm->deadline = one < cardEnd ? one : cardEnd;
    card_add(c, L"  %s", names[p]);
    if (g_cardVerbose) card_add(c, L"   ID %ld", ids[p]);
    card_add(c, L"\r\n");
    /* сначала тихо собрать сводку, потом (в «Атрибутах») показать весь обход */
    pf_explore(dbc, ids[p], 3, NULL, L"", sm);
    wchar_t mt[480], ms[80];
    pf_material_text(sm, mt, 480);
    pf_mass_text(sm->mass, ms, 80);
    card_add(c, L"      %-14s%s\r\n", L"Материал", mt[0] ? mt : L"—");
    wchar_t dt[240];
    pf_dims_text(sm, dt, 240);
    const wchar_t *dv = dt;
    if (!wcsncmp(dv, L"Габариты ", 9)) dv += 9; /* из PreformSize — подпись уже в строке слева */
    card_add(c, L"      %-14s%s\r\n", L"Габариты", dv[0] ? dv : L"—");
    card_add(c, L"      %-14s%s\r\n", L"Масса", ms[0] ? ms : L"—");
    if (!mt[0] && !dv[0] && !sm->mass[0])
      card_add(c, L"      (в заготовке не нашлось ни материала, ни размеров, ни массы —\r\n"
                  L"       Shift + «Все данные» покажет, что в ней лежит)\r\n");
    if (g_cardVerbose) {
      PfSum *sv = (PfSum *)calloc(1, sizeof(PfSum));
      if (sv) {
        sv->deadline = GetTickCount64() + 8000;
        card_add(c, L"      ── что внутри ──\r\n");
        pf_explore(dbc, ids[p], 3, c, L"      ", sv);
        free(sv);
      }
    }
    free(sm);
  }
  card_add(c, L"\r\n");
}

/* Техсостав (материалы, покупные) — как его собирает PlmApi: TechCompCard →
   ActualVersionTechComp → среди детей версии та, чей Product — конфигурация
   изделия → её коллекция TechComposition, без строк с IsRemoved. */
static void card_techcomp(SQLHDBC dbc, long tcCard, long prodConf, CardOut *c, CardRow *rows,
                          wchar_t *err) {
  wchar_t *sql = (wchar_t *)malloc(4000 * sizeof(wchar_t));
  if (!sql) return;
  _snwprintf(sql, 4000,
             L"SELECT DISTINCT TOP 300 ce.CollectionElementId, N'', N'', 0, 0 "
             L"FROM InfoObjectAttributes AS av WITH(NOLOCK) "
             L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=av.NameKeyId "
             L"AND nkv.Value=N'ActualVersionTechComp' "
             L"JOIN InfoObjects AS ch WITH(NOLOCK) ON ch.ParentId=av.Link AND ch.Erased=0 "
             L"JOIN InfoObjectAttributes AS tc WITH(NOLOCK) ON tc.OwnerId=ch.InfoObjectId "
             L"AND tc.Outdated=0 "
             L"JOIN NameKeys AS nktc WITH(NOLOCK) ON nktc.NameKeyId=tc.NameKeyId "
             L"AND nktc.Value=N'TechComposition' "
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId=tc.AttributeId "
             L"AND ce.Outdated=0 "
             L"WHERE av.OwnerId=%ld AND av.Outdated=0 "
             L"AND (%ld=0 OR EXISTS (SELECT 1 FROM InfoObjectAttributes AS pr WITH(NOLOCK) "
             L"JOIN NameKeys AS nkpr WITH(NOLOCK) ON nkpr.NameKeyId=pr.NameKeyId "
             L"AND nkpr.Value=N'Product' "
             L"WHERE pr.OwnerId=ch.InfoObjectId AND pr.Outdated=0 AND pr.Link=%ld)) "
             L"AND NOT EXISTS (SELECT 1 FROM InfoObjectAttributes AS ir WITH(NOLOCK) "
             L"JOIN NameKeys AS nkr WITH(NOLOCK) ON nkr.NameKeyId=ir.NameKeyId "
             L"AND nkr.Value=N'IsRemoved' "
             L"WHERE ir.CollectionElementId=ce.CollectionElementId AND ir.BoolValue=1) "
             L"ORDER BY ce.CollectionElementId",
             tcCard, prodConf, prodConf);
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  if (n < 0) {
    card_add(c, L"ТЕХСОСТАВ: запрос не выполнился.\r\n%s\r\n\r\n", err);
    free(sql);
    return;
  }
  if (n == 0) {
    card_add(c, L"ТЕХСОСТАВ: карточка есть (%ld), но строк в нём нет.\r\n\r\n", tcCard);
    free(sql);
    return;
  }
  int en = n > 120 ? 120 : n;
  long *els = (long *)malloc(sizeof(long) * (size_t)en);
  wchar_t *ids = (wchar_t *)malloc(sizeof(wchar_t) * 1800);
  if (!els || !ids) {
    free(els);
    free(ids);
    free(sql);
    return;
  }
  size_t il = 0;
  ids[0] = 0;
  int used = 0;
  for (int i = 0; i < en; i++) {
    wchar_t one[24];
    int k = _snwprintf(one, 24, il ? L",%ld" : L"%ld", rows[i].n1);
    if (k <= 0 || il + (size_t)k + 1 >= 1800) break;
    memcpy(ids + il, one, ((size_t)k + 1) * sizeof(wchar_t));
    il += (size_t)k;
    els[used++] = rows[i].n1;
  }
  /* все атрибуты всех строк одним запросом; ссылки — именем объекта */
  _snwprintf(sql, 4000,
             L"SELECT TOP %d a.CollectionElementId, nk.Value, COALESCE(lo.Name, " CARD_VALUE_SQL L"), "
             L"0, a.DataType "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE a.CollectionElementId IN (%s) AND a.Outdated=0 "
             L"AND nk.Value NOT IN (N'LastChanged',N'IsRemoved') "
             L"ORDER BY a.CollectionElementId, a.DataType DESC, nk.Value",
             CARD_ROWS, ids);
  n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  card_add(c, L"ТЕХСОСТАВ (%d)\r\n", used);
  for (int e = 0; e < used; e++) {
    card_add(c, L"  %2d. ", e + 1);
    int shown = 0;
    /* сначала то, на что строка ссылается (материал, деталь), потом прочее */
    for (int pass = 0; pass < 2; pass++) {
      for (int i = 0; i < n; i++) {
        if (rows[i].n1 != els[e] || !rows[i].s2[0] || card_hidden(rows[i].s1)) continue;
        BOOL link = rows[i].n3 == 6;
        if ((pass == 0) != link) continue;
        const wchar_t *lab = card_label(rows[i].s1);
        if (link)
          card_add(c, L"%s%s", shown ? L" · " : L"", rows[i].s2);
        else
          card_add(c, L"%s%s %s", shown ? L" · " : L"", lab ? lab : rows[i].s1, rows[i].s2);
        shown++;
      }
    }
    if (!shown) card_add(c, L"(пустая строка)");
    card_add(c, L"\r\n");
  }
  if (n < 0) card_add(c, L"  состав строк не прочитался: %s\r\n", err);
  card_add(c, L"\r\n");
  free(els);
  free(ids);
  free(sql);
}

static void plm_card(long id, wchar_t *out, int cap) {
  g_opsPendN = 0;
  if (share_client_on()) {
    share_card(id, g_cardVerbose, out, cap);
    return;
  }
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
  long actualVer = 0, tpCard = 0, pfCard = 0, tcCard = 0, prodConf = 0;
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

  /* карточки заготовок и техсостава лежат в карте взаимосвязей изделия */
  _snwprintf(sql, 3600,
             L"SELECT DISTINCT TOP 20 pa.Link, nkp.Value, N'', 0, 0 " PLM_HOLDERS(L"%ld")
             L"AND nkp.Value IN (N'ProductPreformsCard',N'TechCompCard',N'ProductConfiguration')",
             id);
  g_qTimeout = 10; /* новые разделы не должны задерживать карточку */
  n = card_query(dbc, sql, rows, CARD_ROWS, err, 280);
  g_qTimeout = 0;
  if (n < 0) card_add(&c, L"Карточки заготовок и техсостава: запрос не выполнился.\r\n%s\r\n\r\n", err);
  for (int i = 0; i < n; i++) {
    if (!pfCard && _wcsicmp(rows[i].s1, L"ProductPreformsCard") == 0) pfCard = rows[i].n1;
    if (!tcCard && _wcsicmp(rows[i].s1, L"TechCompCard") == 0) tcCard = rows[i].n1;
    if (!prodConf && _wcsicmp(rows[i].s1, L"ProductConfiguration") == 0) prodConf = rows[i].n1;
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
  /* не нашлось — пишем прямо, иначе не отличить «нет заготовки» от «не нашли» */
  g_qTimeout = 15;
  if (pfCard) card_preforms(dbc, pfCard, &c, rows, err);
  else if (!actualVer) card_add(&c, L"ЗАГОТОВКА: карточки заготовок у изделия не нашлось.\r\n\r\n");
  if (tcCard) card_techcomp(dbc, tcCard, prodConf, &c, rows, err);
  else if (!actualVer) card_add(&c, L"ТЕХСОСТАВ: карточки техсостава у изделия не нашлось.\r\n\r\n");
  g_qTimeout = 0;
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

#include "share.c"

/* ---- столбец «Заготовка» в фоне ------------------------------------------- */
#define WM_PF_DONE (WM_APP + 16) /* lParam — PfJob*, освобождает получатель */

static DWORD WINAPI pf_thread(LPVOID param) {
  PfJob *j = (PfJob *)param;
  if (share_client_on()) {
    share_pf(j); /* своего логина нет — спрашиваем того же, кто искал */
  } else {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    wchar_t err[280];
    if (plm_connect(&env, &dbc, err, 280)) {
      pf_compute(dbc, j, 30000);
      SQLDisconnect(dbc);
      SQLFreeHandle(SQL_HANDLE_DBC, dbc);
      SQLFreeHandle(SQL_HANDLE_ENV, env);
    }
  }
  if (!g_hwnd || !PostMessageW(g_hwnd, WM_PF_DONE, 0, (LPARAM)j)) free(j);
  return 0;
}

static void pf_update_column(void) {
  if (!g_answerList || g_resultFiles) return;
  for (int i = 0; i < g_plmCount; i++) {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    wchar_t sh[PLM_COL1];
    pf_short(g_plmPf[i], sh, PLM_COL1);
    it.iSubItem = 2;
    it.pszText = sh;
    SendMessageW(g_answerList, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&it);
  }
  ans_rows_fit();
}

/* находки показаны — теперь заготовки к ним */
static void pf_start_async(void) {
  if (g_resultFiles || g_plmCount <= 0 || g_engine != 4) return;
  PfJob *j = (PfJob *)calloc(1, sizeof(PfJob));
  if (!j) return;
  j->gen = InterlockedIncrement(&g_pfGen);
  for (int i = 0; i < g_plmCount && j->n < PF_JOB_MAX; i++) {
    if (!g_plmIds[i]) continue;
    BOOL dup = FALSE;
    for (int k = 0; k < j->n && !dup; k++) dup = j->ids[k] == g_plmIds[i];
    if (!dup) j->ids[j->n++] = g_plmIds[i];
  }
  if (!j->n) {
    free(j);
    return;
  }
  for (int i = 0; i < g_plmCount; i++) lstrcpynW(g_plmPf[i], L"…", PLM_COL1);
  pf_update_column();
  HANDLE th = CreateThread(NULL, 0, pf_thread, j, 0, NULL);
  if (th) CloseHandle(th);
  else free(j);
}

static void pf_apply(PfJob *j) {
  if (!j) return;
  /* пока считали, могли начать новый поиск — тогда ответ уже не про него */
  if (j->gen == g_pfGen && !g_resultFiles) {
    for (int i = 0; i < g_plmCount; i++) {
      const wchar_t *t = L"";
      for (int k = 0; k < j->n; k++)
        if (j->ids[k] == g_plmIds[i]) t = j->text[k];
      lstrcpynW(g_plmPf[i], t, PLM_COL1);
    }
    pf_update_column();
  }
  free(j);
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

/* Сколько места нужно ряду кнопок, чтобы ни одна надпись не обрезалась.
   У тем разные шрифты: Georgia заметно шире Segoe UI, и окно, которое в
   «Обычной» влезало, в «Бумаге» резало бы подписи многоточием. */
static int btn_row_width(HWND parent, const int *ids, int n) {
  if (!parent) return 0;
  HDC dc = GetDC(parent);
  if (!dc) return 0;
  HGDIOBJ old = g_fontUi ? SelectObject(dc, g_fontUi) : NULL;
  int total = 0, vis = 0;
  for (int k = 0; k < n; k++) {
    HWND b = GetDlgItem(parent, ids[k]);
    if (!b) continue;
    wchar_t t[96];
    t[0] = 0;
    GetWindowTextW(b, t, 96);
    SIZE sz;
    sz.cx = 60;
    sz.cy = 0;
    GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    int w = sz.cx + 24;
    total += w < 56 ? 56 : w;
    vis++;
  }
  if (old) SelectObject(dc, old);
  ReleaseDC(parent, dc);
  if (!vis) return 0;
  /* поля по 12 с краёв, зазоры по 7, рамка окна */
  return total + 7 * (vis - 1) + 24 + 16;
}

static const int kAnsBtns[6] = {ID_ANS_OPEN, ID_ANS_OPENTP, ID_ANS_CARD, ID_ANS_DRAW, ID_ANS_SHOW,
                                ID_ANS_CLOSE};
static const int kCardBtns[5] = {ID_CARD_OPEN, ID_CARD_DRAW, ID_CARD_SHOW, ID_CARD_1C, ID_CARD_CLOSE};
static const int kOcrBtns[4] = {ID_OCR_COPY, ID_OCR_FIND, ID_OCR_AGAIN, ID_OCR_CLOSE};

/* Дотянуть открытое окно до ширины ряда кнопок — сразу после смены темы.
   Сузить руками потом можно: кнопки тогда пожмутся. */
static void widen_to_row(HWND w, const int *ids, int n) {
  if (!w || !IsWindowVisible(w)) return;
  int need = btn_row_width(w, ids, n);
  RECT wr;
  GetWindowRect(w, &wr);
  if (need <= wr.right - wr.left) return;
  POINT c = {wr.left, wr.top};
  RECT wa;
  get_work_area(c, &wa);
  if (need > wa.right - wa.left) need = wa.right - wa.left;
  int x = wr.left;
  if (x + need > wa.right) x = wa.right - need;
  SetWindowPos(w, NULL, x, wr.top, need, wr.bottom - wr.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void theme_fit_windows(void) {
  widen_to_row(g_answer, kAnsBtns, 6);
  widen_to_row(g_card, kCardBtns, 5);
  widen_to_row(g_ocrWnd, kOcrBtns, 4);
}

static void layout_answer(void) {
  if (!g_answer) return;
  RECT rc;
  GetClientRect(g_answer, &rc);
  int pad = 12, btnH = 28, gap = 7;
  int top = PANEL_TITLE_H + 6;
  int by = rc.bottom - pad - btnH;
  /* чертёж переехал в окно карточки — здесь остался только текст и список */
  if (g_answerEdit) MoveWindow(g_answerEdit, pad, top, rc.right - pad * 2, by - top - 6, TRUE);
  if (g_answerList) {
    MoveWindow(g_answerList, pad, top, rc.right - pad * 2, by - top - 6, TRUE);
    int cw = rc.right - pad * 2 - 24;
    if (cw < 80) cw = 80;
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_WIDTH;
    /* у файлов два столбца пополам; в PLM третий — заготовка */
    int w0 = g_resultFiles ? cw / 2 : cw * 36 / 100;
    int w1 = g_resultFiles ? cw - cw / 2 : cw * 36 / 100;
    col.cx = w0;
    SendMessageW(g_answerList, LVM_SETCOLUMNW, 0, (LPARAM)&col);
    col.cx = w1;
    SendMessageW(g_answerList, LVM_SETCOLUMNW, 1, (LPARAM)&col);
    col.cx = g_resultFiles ? 0 : cw - w0 - w1;
    SendMessageW(g_answerList, LVM_SETCOLUMNW, 2, (LPARAM)&col);
    ans_rows_fit();
  }
  /* У кнопок разная длина надписи, и делить ряд поровну нельзя:
     «Закрыть» болталась бы пустой, а «Открыть файл в проводнике»
     обрезалось многоточием. Мерим надписи и раздаём место по ним. */
  HWND btns[6];
  for (int k = 0; k < 6; k++) btns[k] = GetDlgItem(g_answer, kAnsBtns[k]);
  /* у найденных файлов ТП нет — кнопка не нужна вовсе */
  HWND tpBtn = btns[1];
  if (tpBtn) ShowWindow(tpBtn, g_resultFiles ? SW_HIDE : SW_SHOW);
  if (g_resultFiles) btns[1] = NULL;
  int bwid[6] = {0, 0, 0, 0, 0, 0};
  int total = 0, vis = 0;
  HDC dc = GetDC(g_answer);
  HGDIOBJ oldFont = (dc && g_fontUi) ? SelectObject(dc, g_fontUi) : NULL;
  for (int k = 0; k < 6; k++) {
    if (!btns[k]) continue;
    wchar_t t[96];
    t[0] = 0;
    GetWindowTextW(btns[k], t, 96);
    SIZE sz;
    sz.cx = 60;
    sz.cy = 0;
    if (dc) GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    bwid[k] = sz.cx + 24;
    if (bwid[k] < 56) bwid[k] = 56;
    total += bwid[k];
    vis++;
  }
  if (oldFont) SelectObject(dc, oldFont);
  if (dc) ReleaseDC(g_answer, dc);
  if (vis > 0) {
    int avail = rc.right - pad * 2 - gap * (vis - 1);
    if (avail < vis * 40) avail = vis * 40;
    if (total > avail && total > 0)
      for (int k = 0; k < 6; k++) bwid[k] = bwid[k] * avail / total;
    int bx = pad;
    for (int k = 0; k < 6; k++) {
      if (!btns[k]) continue;
      MoveWindow(btns[k], bx, by, bwid[k], btnH, TRUE);
      ShowWindow(btns[k], SW_SHOW);
      bx += bwid[k] + gap;
    }
  }
}

static LRESULT CALLBACK AnswerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  /* поле текста — цветами темы; без этого оно было системным серым во всех темах
     (поле только для чтения спрашивает цвет через CTLCOLORSTATIC) */
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, bg_brush(FALSE));
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
    if (LOWORD(wParam) == ID_ANS_OPENTP) open_plm_tp_selected();
    if (LOWORD(wParam) == ID_ANS_OPEN) {
      if (g_plmCount > 0 || !g_ansObj[0]) open_plm_selected();
      else {
        /* карточка заняла место списка, но сам объект никуда не делся */
        lstrcpynW(g_plmLastLink, g_ansObj, PLM_LINK);
        open_plm_link(g_ansObj);
        show_status(L"В текущий клиент СОЮЗ");
      }
    }
    if (LOWORD(wParam) == ID_ANS_SHOW) {
      /* в находках PLM строка — это ссылка в СОЮЗ, показывать в
         проводнике нужно найденный файл чертежа, а не её */
      if (g_resultFiles) {
        show_selected_in_explorer();
      } else {
        const wchar_t *f = ans_draw_file();
        if (f && f[0]) show_in_explorer(f);
        else show_status(L"Файла для этой строки нет");
      }
    }
    /* «Все данные» — карточка по делу. С шифтом туда же попадает
       сырой список атрибутов: отдельная кнопка под него убрана */
    if (LOWORD(wParam) == ID_ANS_CARD) {
      if (GetKeyState(VK_SHIFT) & 0x8000) show_card_full();
      else show_card_selected();
    }
    if (LOWORD(wParam) == ID_ANS_DRAW) {
      const wchar_t *f = ans_draw_file();
      if (f && f[0]) ShellExecuteW(NULL, L"open", f, NULL, NULL, SW_SHOWNORMAL);
      else show_status(L"Чертежа для этой строки в индексе нет");
    }
    return 0;
  case WM_SEL_DRAW: {
    wchar_t *res = (wchar_t *)lParam;
    if (res) {
      /* ответ на уже другую строку просто выбрасываем */
      if ((LONG)wParam == g_drawGen) lstrcpynW(g_selDraw, res, PLM_LINK);
      free(res);
    }
    ans_sync_buttons();
    return 0;
  }
  case WM_NOTIFY: {
    NMHDR *nm = (NMHDR *)lParam;
    if (nm && nm->idFrom == ID_ANS_LIST &&
        (nm->code == NM_DBLCLK || nm->code == NM_RETURN || nm->code == LVN_ITEMACTIVATE)) {
      open_plm_selected();
      return 0;
    }
    /* сменилась строка — ищем чертёж для неё и пересчитываем кнопки */
    if (nm && nm->idFrom == ID_ANS_LIST && nm->code == LVN_ITEMCHANGED) {
      const NMLISTVIEW *lv = (const NMLISTVIEW *)lParam;
      if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) &&
          !(lv->uOldState & LVIS_SELECTED))
        request_row_draw();
      break;
    }
    if (nm && nm->idFrom == ID_ANS_LIST && nm->code == NM_CUSTOMDRAW)
      return ans_list_customdraw((NMLVCUSTOMDRAW *)lParam);
    /* наведёшь на строку — заготовка целиком: в столбце она сокращена */
    if (nm && nm->idFrom == ID_ANS_LIST && nm->code == LVN_GETINFOTIPW && !g_resultFiles) {
      NMLVGETINFOTIPW *tip = (NMLVGETINFOTIPW *)lParam;
      int i = tip->iItem;
      if (i >= 0 && i < g_plmCount && g_plmPf[i][0] && wcscmp(g_plmPf[i], L"…") && tip->pszText &&
          tip->cchTextMax > 0) {
        _snwprintf(tip->pszText, tip->cchTextMax, L"Заготовка: %s", g_plmPf[i]);
        tip->pszText[tip->cchTextMax - 1] = 0;
      }
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
  /* та же беда, что и у карточки: окно перетащили, а галочка
     «оставлять на месте» возвращала его на прежнее */
  case WM_MOVE:
    if (!g_ansBig && IsWindowVisible(hwnd)) {
      RECT wr;
      GetWindowRect(hwnd, &wr);
      g_ansX = wr.left;
      g_ansY = wr.top;
    }
    return 0;
  case WM_SIZE:
    layout_answer();
    /* moving the children is not enough: the uncovered strip has to be
       repainted too, or the list is left drawn at its old offset */
    InvalidateRect(hwnd, NULL, TRUE);
    if (g_answerList) InvalidateRect(g_answerList, NULL, TRUE);
    /* пока окно развёрнуто, его размер и место временные: запоминать их
       нельзя, иначе оно навсегда останется во весь экран */
    if (wParam != SIZE_MINIMIZED && !g_ansBig) {
      RECT wr;
      GetWindowRect(hwnd, &wr);
      g_ansX = wr.left;
      g_ansY = wr.top;
      g_ansW = wr.right - wr.left;
      g_ansH = wr.bottom - wr.top;
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
  /* колесо достаётся самому полю, а не окну: без этого Ctrl+колесо
     работало только если крутить по рамке */
  g_oldAnsEdit = (WNDPROC)SetWindowLongPtrW(g_answerEdit, GWLP_WNDPROC, (LONG_PTR)AnsEditProc);
  g_answerList = CreateWindowExW(
      0, WC_LISTVIEWW, L"",
      WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
      0, 0, 100, 100, g_answer, (HMENU)(INT_PTR)ID_ANS_LIST, NULL, NULL);
  if (g_answerList) {
    SendMessageW(g_answerList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
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
    col.cx = 150;
    col.iSubItem = 2;
    col.pszText = L"3 Заготовка";
    SendMessageW(g_answerList, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);
  }
  HWND open = mk_btn(g_answer, L"Открыть ЭСИ в СОЮЗ", ID_ANS_OPEN);
  HWND openTp = mk_btn(g_answer, L"Открыть ТП в СОЮЗ", ID_ANS_OPENTP);
  HWND card = mk_btn(g_answer, L"Все данные", ID_ANS_CARD);
  HWND draw = mk_btn(g_answer, L"Открыть чертёж", ID_ANS_DRAW);
  HWND show = mk_btn(g_answer, L"Открыть файл в проводнике", ID_ANS_SHOW);
  HWND cls = mk_btn(g_answer, L"Закрыть", ID_ANS_CLOSE);
  if (g_fontBody) SendMessageW(g_answerEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  if (g_answerList) {
    SendMessageW(g_answerList, WM_SETFONT, (WPARAM)(ans_font() ? ans_font() : g_fontBody), TRUE);
    g_oldAnsList = (WNDPROC)SetWindowLongPtrW(g_answerList, GWLP_WNDPROC, (LONG_PTR)AnsListProc);
  }
  if (g_fontUi) {
    SendMessageW(open, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(openTp, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(card, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(draw, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(show, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(cls, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  }
  ans_sync_buttons();
}

/* The card can take a few seconds on a loaded server, so it runs off the UI
   thread exactly like a search does. */
static DWORD WINAPI card_thread(LPVOID param) {
  long id = (long)(LONG_PTR)param;
  wchar_t *out = (wchar_t *)malloc(160000 * sizeof(wchar_t));
  if (out) {
    out[0] = 0;
    plm_card(id, out, 160000);
    /* сам чертёж здесь не открываем: GDI+ не терпит, когда картинку готовит
       один поток, а рисует другой. Его откроет окно, когда получит текст. */
    if (!PostMessageW(g_hwnd, WM_CARD_DONE, 0, (LPARAM)out)) free(out);
  }
  InterlockedExchange(&g_netBusy, 0);
  return 0;
}

static void show_card_selected_mode(void);
static void show_card_text(const wchar_t *text);

static void show_card_selected(void) {
  g_cardVerbose = FALSE; /* «Карточка»: обозначение, ТП с операциями, входимость */
  show_card_selected_mode();
}

static void show_card_full(void) {
  g_cardVerbose = TRUE; /* «Атрибуты»: то же плюс всё содержимое объекта */
  show_card_selected_mode();
}

static void show_card_selected_mode(void) {
  int i = plm_selected_index();
  if (i < 0 || i >= g_plmCount || !g_plmIds[i]) {
    show_status(L"Выберите строку");
    return;
  }
  if (InterlockedCompareExchange(&g_netBusy, 1, 0) != 0) {
    show_status(L"Запрос уже идёт");
    return;
  }
  draw_close();
  g_snapN = 0;
  for (int k = 0; k < g_plmCount && k < PLM_ROWS; k++) {
    lstrcpynW(g_snapName[g_snapN], g_plmEsi[k], PLM_COL1);
    g_snapId[g_snapN] = g_plmIds[k];
    g_snapN++;
    /* техпроцесс, сведённый в эту же строку, — отдельной записью снимка */
    if (g_plmTpId[k] && g_plmTp[k][0] && g_snapN < PLM_ROWS) {
      lstrcpynW(g_snapName[g_snapN], g_plmTp[k], PLM_COL1);
      g_snapId[g_snapN] = g_plmTpId[k];
      g_snapN++;
    }
  }
  /* карточка уходит в своё окно — список находок остаётся как был */
  lstrcpynW(g_ansObj, g_plmLinks[i], PLM_LINK);
  g_cardDraw[0] = 0;
  draw_close();
  wchar_t wait[120];
  _snwprintf(wait, 120, L"Смотрю объект %ld в PLM…", g_plmIds[i]);
  show_card_text(wait);
  HANDLE th = CreateThread(NULL, 0, card_thread, (LPVOID)(LONG_PTR)g_plmIds[i], 0, NULL);
  if (th) CloseHandle(th);
  else InterlockedExchange(&g_netBusy, 0);
}

/* Масштаб текста ответа. Карточка бывает длинной, и подгонять окно мышью
   каждый раз утомительно — проще уменьшить текст. Ctrl+колесо. */
static HFONT make_font(const wchar_t *face, int pt, int weight);
static HFONT g_ansFontZoom;

static HFONT ans_font(void) {
  if (g_ansFontZoom) return g_ansFontZoom;
  g_ansFontZoom = make_font(g_faceBody, g_ansPt, FW_NORMAL);
  return g_ansFontZoom;
}

/* Карточка стоит колонками, ей нужен ровный шрифт и свой масштаб:
   окна теперь два и одно на другое не должно влиять. */
static HFONT card_font(void) {
  if (g_cardFontZoom) return g_cardFontZoom;
  g_cardFontZoom = make_font(L"Consolas", g_cardPt, FW_NORMAL);
  if (!g_cardFontZoom) g_cardFontZoom = make_font(L"Courier New", g_cardPt, FW_NORMAL);
  return g_cardFontZoom;
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
  if (g_answerList) { /* список находок — тем же масштабом */
    SendMessageW(g_answerList, WM_SETFONT, (WPARAM)ans_font(), TRUE);
    g_ansRowH = -1; /* высоту строк пересчитать заново */
    ans_rows_fit();
  }
  save_cursor_pref();
  wchar_t m[64];
  _snwprintf(m, 64, L"Масштаб текста: %d", g_ansPt);
  show_status(m);
}

/* Подобрать размер окна под текст: длина самой длинной строки и число строк.
   Больше рабочей области не делаем, меньше разумного — тоже. */
static void ans_fit(const wchar_t *text, const RECT *wa, int *outW, int *outH, HWND edit,
                    HFONT font) {
  int minW = ANS_W, minH = ANS_H;
  *outW = minW;
  *outH = minH;
  if (!text || !text[0] || !edit) return;
  HDC dc = GetDC(edit);
  if (!dc) return;
  HFONT prev = (HFONT)SelectObject(dc, font);
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
  ReleaseDC(edit, dc);
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
  int aw = g_ansW > 0 ? g_ansW : ANS_W;
  int ah = g_ansH > 0 ? g_ansH : ANS_H;
  /* запомненная с прошлых версий ширина бывает такой, что надписи
     на кнопках обрезаются многоточием — ниже этого не опускаемся */
  if (aw < ANS_W) aw = ANS_W;
  {
    int row = btn_row_width(g_answer, kAnsBtns, 6);
    if (aw < row) aw = row;
  }
  if (aw > wa.right - wa.left) aw = wa.right - wa.left;
  if (x + aw > wa.right) x = wa.right - aw - 8;
  if (y + ah > wa.bottom) y = wa.bottom - ah - 8;
  if (x < wa.left) x = wa.left + 8;
  if (y < wa.top) y = wa.top + 8;
  g_ansBig = FALSE;
  SetWindowPos(g_answer, HWND_TOPMOST, x, y, aw, ah, SWP_SHOWWINDOW);
  ans_sync_buttons();
  if (g_plmCount > 0 && g_answerList) SetFocus(g_answerList);
}

/* ---- режим «Для 1С» ------------------------------------------------ */
/* Операции ТП надо перенести в 1С по одной, в порядке ТП. Никаких особых
   клавиш: в буфере лежит очередная операция, человек жмёт обычный Ctrl+V в 1С,
   и после вставки в буфер сама ложится следующая.

   Как понять, что вставили: следим за нажатием Ctrl+V и Shift+Insert. Спросить
   сам буфер «тебя уже забрали?» нельзя: журнал буфера Windows читает его сразу
   после каждого копирования, и очередь убегала бы вперёд сама. Следующая
   операция кладётся с паузой — 1С должна успеть забрать текущую. */
static wchar_t g_q1c[OPS_1C][PLM_COL1];
static int g_q1cN, g_q1cIdx, g_q1cRetry;
static BOOL g_1cOn, g_1cPending, g_1cVDown, g_1cInsDown;
static HHOOK g_1cHook;

static BOOL onec_ours(HWND fg) {
  if (!fg) return FALSE;
  HWND r = GetAncestor(fg, GA_ROOT);
  HWND mine[6] = {g_hwnd, g_setHwnd, g_askHwnd, g_answer, g_card, g_ocrWnd};
  for (int i = 0; i < 6; i++)
    if (mine[i] && (fg == mine[i] || r == mine[i])) return TRUE;
  return FALSE;
}

/* Строка состояния держится, пока режим включён: сразу видно, что идёт
   перенос в 1С и какая по счёту операция сейчас в буфере. */
static void onec_show(void) {
  if (!g_1cOn) return;
  wchar_t m[160];
  _snwprintf(m, 160, L"Для 1С · %d из %d · %.90s", g_q1cIdx + 1, g_q1cN, g_q1c[g_q1cIdx]);
  show_status(m);
  if (g_clipEdit) SetWindowTextW(g_clipEdit, g_q1c[g_q1cIdx]);
}

static void onec_stop(const wchar_t *why) {
  if (g_1cHook) {
    UnhookWindowsHookEx(g_1cHook);
    g_1cHook = NULL;
  }
  KillTimer(g_hwnd, TIMER_1C);
  BOOL was = g_1cOn;
  g_1cOn = FALSE;
  g_1cPending = FALSE;
  if (was && why) show_status(why);
}

static void CALLBACK onec_tick(HWND h, UINT m, UINT_PTR id, DWORD t) {
  (void)h;
  (void)m;
  (void)t;
  KillTimer(g_hwnd, id);
  if (!g_1cOn) return;
  int next = g_q1cIdx + 1;
  if (next >= g_q1cN) {
    wchar_t done[96];
    _snwprintf(done, 96, L"Для 1С: все %d операций вставлены", g_q1cN);
    onec_stop(done);
    return;
  }
  /* 1С может ещё держать буфер открытым — тогда пробуем чуть позже */
  if (!clipboard_set(g_q1c[next])) {
    if (++g_q1cRetry < 20) {
      SetTimer(g_hwnd, TIMER_1C, 100, onec_tick);
      return;
    }
    onec_stop(L"Для 1С: буфер занят другой программой, перенос остановлен");
    return;
  }
  g_q1cRetry = 0;
  g_q1cIdx = next;
  g_1cPending = FALSE;
  onec_show();
}

static LRESULT CALLBACK onec_kbd(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && g_1cOn) {
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
    BOOL down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    BOOL up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
    BOOL ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    BOOL alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    BOOL paste = FALSE;
    if (k->vkCode == 'V') {
      /* зажатая клавиша повторяется — считаем только первое нажатие */
      if (down && !g_1cVDown && ctrl && !alt) paste = TRUE;
      if (down) g_1cVDown = TRUE;
      if (up) g_1cVDown = FALSE;
    } else if (k->vkCode == VK_INSERT) {
      if (down && !g_1cInsDown && shift && !ctrl) paste = TRUE;
      if (down) g_1cInsDown = TRUE;
      if (up) g_1cInsDown = FALSE;
    }
    /* два Ctrl+V подряд до смены буфера вставляют одно и то же — и сдвиг
       должен быть один, иначе операция проскочит */
    if (paste && !g_1cPending && !onec_ours(GetForegroundWindow())) {
      g_1cPending = TRUE;
      g_q1cRetry = 0;
      SetTimer(g_hwnd, TIMER_1C, 450, onec_tick);
    }
  }
  return CallNextHookEx(g_1cHook, code, wp, lp);
}

static void onec_start(void) {
  if (g_ops1cN <= 0) {
    show_status(L"В карточке нет операций — переносить нечего");
    return;
  }
  onec_stop(NULL);
  /* своя копия списка: откроют другую карточку — идущий перенос не собьётся */
  memcpy(g_q1c, g_ops1c, sizeof(g_q1c));
  g_q1cN = g_ops1cN;
  g_q1cIdx = 0;
  if (!clipboard_set(g_q1c[0])) {
    show_status(L"Буфер занят другой программой — попробуйте ещё раз");
    return;
  }
  g_1cHook = SetWindowsHookExW(WH_KEYBOARD_LL, onec_kbd, g_inst, 0);
  if (!g_1cHook) {
    show_status(L"Для 1С: Windows не дала следить за вставкой");
    return;
  }
  g_1cVDown = g_1cInsDown = FALSE;
  g_1cPending = FALSE;
  g_1cOn = TRUE;
  onec_show();
}

/* Человек скопировал что-то своё — очередь больше не в буфере, и следующий
   Ctrl+V вставит его текст, а не операцию. Двигать очередь дальше нельзя. */
static void onec_foreign_copy(void) {
  if (g_1cOn) onec_stop(L"Для 1С: скопировано другое — перенос остановлен");
}

static void card_ops_commit(void) {
  memcpy(g_ops1c, g_opsPend, sizeof(g_ops1c));
  g_ops1cN = g_opsPendN;
}

/* ---- карточка: второе окно ------------------------------------------ */
/* Раньше карточка занимала то же окно, и список находок пропадал:
   посмотрел одну деталь — ищи заново. Теперь окна два: слева список,
   справа карточка, и чертёж живёт в карточке. */
static void layout_card(void);

static void card_zoom(int delta) {
  int pt = g_cardPt + delta;
  if (pt < 7) pt = 7;
  if (pt > 22) pt = 22;
  if (pt == g_cardPt) return;
  g_cardPt = pt;
  if (g_cardFontZoom) {
    DeleteObject(g_cardFontZoom);
    g_cardFontZoom = NULL;
  }
  if (g_cardEdit) {
    SendMessageW(g_cardEdit, WM_SETFONT, (WPARAM)card_font(), TRUE);
    InvalidateRect(g_cardEdit, NULL, TRUE);
  }
  save_cursor_pref();
  wchar_t m[64];
  _snwprintf(m, 64, L"Масштаб карточки: %d", g_cardPt);
  show_status(m);
}

static WNDPROC g_oldCardEdit;

static LRESULT CALLBACK CardEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_MOUSEWHEEL && (GetKeyState(VK_CONTROL) & 0x8000)) {
    card_zoom(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
    return 0;
  }
  return CallWindowProcW(g_oldCardEdit, hwnd, msg, wParam, lParam);
}

static void card_sync_buttons(void) {
  if (!g_card) return;
  HWND open = GetDlgItem(g_card, ID_CARD_OPEN);
  HWND draw = GetDlgItem(g_card, ID_CARD_DRAW);
  HWND show = GetDlgItem(g_card, ID_CARD_SHOW);
  BOOL haveFile = g_cardDraw[0] != 0;
  HWND onec = GetDlgItem(g_card, ID_CARD_1C);
  if (open) EnableWindow(open, g_ansObj[0] != 0);
  if (draw) EnableWindow(draw, haveFile);
  if (show) EnableWindow(show, haveFile);
  if (onec) EnableWindow(onec, g_ops1cN > 0);
  layout_card();
}

static void layout_card(void) {
  if (!g_card) return;
  RECT rc;
  GetClientRect(g_card, &rc);
  int pad = 12, btnH = 28, gap = 7;
  int top = PANEL_TITLE_H + 6;
  int by = rc.bottom - pad - btnH;
  BOOL withPane = g_drawPane && g_drawImg;
  int textLeft = pad, textRight = rc.right - pad;
  if (withPane) {
    int split = (rc.right - pad * 2) * 55 / 100;
    if (split < 120) split = 120;
    MoveWindow(g_drawPane, pad, top, split, by - top - 6, TRUE);
    textLeft = pad + split + 8;
  }
  if (g_drawPane) ShowWindow(g_drawPane, withPane ? SW_SHOW : SW_HIDE);
  if (g_cardEdit) MoveWindow(g_cardEdit, textLeft, top, textRight - textLeft, by - top - 6, TRUE);
  HWND btns[5];
  btns[0] = GetDlgItem(g_card, ID_CARD_OPEN);
  btns[1] = GetDlgItem(g_card, ID_CARD_DRAW);
  btns[2] = GetDlgItem(g_card, ID_CARD_SHOW);
  btns[3] = GetDlgItem(g_card, ID_CARD_1C);
  btns[4] = GetDlgItem(g_card, ID_CARD_CLOSE);
  int bwid[5] = {0, 0, 0, 0, 0};
  int total = 0, vis = 0;
  HDC dc = GetDC(g_card);
  HGDIOBJ oldFont = (dc && g_fontUi) ? SelectObject(dc, g_fontUi) : NULL;
  for (int k = 0; k < 5; k++) {
    if (!btns[k]) continue;
    wchar_t t[96];
    t[0] = 0;
    GetWindowTextW(btns[k], t, 96);
    SIZE sz;
    sz.cx = 60;
    sz.cy = 0;
    if (dc) GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    bwid[k] = sz.cx + 24;
    if (bwid[k] < 56) bwid[k] = 56;
    total += bwid[k];
    vis++;
  }
  if (oldFont) SelectObject(dc, oldFont);
  if (dc) ReleaseDC(g_card, dc);
  if (vis > 0) {
    int avail = rc.right - pad * 2 - gap * (vis - 1);
    if (avail < vis * 40) avail = vis * 40;
    if (total > avail && total > 0)
      for (int k = 0; k < 5; k++) bwid[k] = bwid[k] * avail / total;
    int bx = pad;
    for (int k = 0; k < 5; k++) {
      if (!btns[k]) continue;
      MoveWindow(btns[k], bx, by, bwid[k], btnH, TRUE);
      ShowWindow(btns[k], SW_SHOW);
      bx += bwid[k] + gap;
    }
  }
}

static RECT g_cardPrev;
static BOOL g_cardBig;

static void card_toggle_big(HWND hwnd) {
  POINT pt;
  RECT wa, wr;
  GetWindowRect(hwnd, &wr);
  pt.x = (wr.left + wr.right) / 2;
  pt.y = (wr.top + wr.bottom) / 2;
  get_work_area(pt, &wa);
  if (!g_cardBig) {
    g_cardPrev = wr;
    g_cardBig = TRUE;
    SetWindowPos(hwnd, HWND_TOPMOST, wa.left + 8, wa.top + 8, wa.right - wa.left - 16,
                 wa.bottom - wa.top - 16, SWP_SHOWWINDOW);
  } else {
    g_cardBig = FALSE;
    SetWindowPos(hwnd, HWND_TOPMOST, g_cardPrev.left, g_cardPrev.top,
                 g_cardPrev.right - g_cardPrev.left, g_cardPrev.bottom - g_cardPrev.top,
                 SWP_SHOWWINDOW);
  }
  layout_card();
}

static LRESULT CALLBACK CardProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  /* поле текста — цветами темы; без этого оно было системным серым во всех темах
     (поле только для чтения спрашивает цвет через CTLCOLORSTATIC) */
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, bg_brush(FALSE));
    draw_panel_header(hwnd, hdc, g_cardVerbose ? L"Атрибуты объекта" : L"Карточка");
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_MOUSEWHEEL:
    if (GetKeyState(VK_CONTROL) & 0x8000) {
      card_zoom(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
      return 0;
    }
    break;
  case WM_NCLBUTTONDBLCLK:
    card_toggle_big(hwnd);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_CARD_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (LOWORD(wParam) == ID_CARD_OPEN && g_ansObj[0]) {
      lstrcpynW(g_plmLastLink, g_ansObj, PLM_LINK);
      open_plm_link(g_ansObj);
      show_status(L"В текущий клиент СОЮЗ");
    }
    if (LOWORD(wParam) == ID_CARD_DRAW && g_cardDraw[0])
      ShellExecuteW(NULL, L"open", g_cardDraw, NULL, NULL, SW_SHOWNORMAL);
    if (LOWORD(wParam) == ID_CARD_SHOW && g_cardDraw[0]) show_in_explorer(g_cardDraw);
    if (LOWORD(wParam) == ID_CARD_1C) onec_start();
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_EXITSIZEMOVE:
    layout_card();
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  /* простой перетаскиванием WM_SIZE не приходит, и место не запоминалось:
     карточка возвращалась туда, откуда её увели */
  case WM_MOVE:
    if (!g_cardBig && IsWindowVisible(hwnd)) {
      RECT wr;
      GetWindowRect(hwnd, &wr);
      g_cardX = wr.left;
      g_cardY = wr.top;
    }
    return 0;
  case WM_SIZE:
    layout_card();
    InvalidateRect(hwnd, NULL, TRUE);
    if (wParam != SIZE_MINIMIZED && !g_cardBig) {
      RECT wr;
      GetWindowRect(hwnd, &wr);
      g_cardX = wr.left;
      g_cardY = wr.top;
      g_cardW = wr.right - wr.left;
      g_cardH = wr.bottom - wr.top;
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void create_card(HWND owner) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = CardProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = g_paper;
  wc.lpszClassName = L"CursorPadCard";
  RegisterClassExW(&wc);
  g_card = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadCard", L"Карточка",
                           WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN, 0, 0, ANS_W, ANS_H, owner,
                           NULL, g_inst, NULL);
  round_corners(g_card);
  g_cardEdit = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 100,
      100, g_card, NULL, NULL, NULL);
  g_oldCardEdit = (WNDPROC)SetWindowLongPtrW(g_cardEdit, GWLP_WNDPROC, (LONG_PTR)CardEditProc);
  /* чертёж показывается рядом с карточкой, значит и живёт в её окне */
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
    g_drawPane =
        CreateWindowExW(0, L"CursorPadDraw", L"", WS_CHILD, 0, 0, 10, 10, g_card, NULL, g_inst,
                        NULL);
  }
  HWND open = mk_btn(g_card, L"Открыть в СОЮЗ", ID_CARD_OPEN);
  HWND draw = mk_btn(g_card, L"Открыть чертёж", ID_CARD_DRAW);
  HWND show = mk_btn(g_card, L"Открыть файл в проводнике", ID_CARD_SHOW);
  /* перенос операций в 1С: после каждой вставки в буфере следующая */
  HWND onec = mk_btn(g_card, L"В 1С", ID_CARD_1C);
  HWND cls = mk_btn(g_card, L"Закрыть", ID_CARD_CLOSE);
  SendMessageW(g_cardEdit, WM_SETFONT, (WPARAM)card_font(), TRUE);
  if (g_fontUi) {
    SendMessageW(open, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(draw, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(show, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(onec, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(cls, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  }
  card_sync_buttons();
}

static void show_card_text(const wchar_t *text) {
  if (!g_card) return;
  /* картинку открывает то же окно, которое её рисует: GDI+ не терпит,
     когда готовит один поток, а показывает другой */
  if (g_cardDraw[0] && !g_drawImg) draw_open(g_cardDraw);
  if (g_cardEdit) {
    SendMessageW(g_cardEdit, WM_SETFONT, (WPARAM)card_font(), TRUE);
    SetWindowTextW(g_cardEdit, text ? text : L"");
  }
  RECT wa;
  POINT pt;
  GetCursorPos(&pt);
  get_work_area(pt, &wa);
  int aw = g_cardW, ah = g_cardH;
  if (aw <= 0 || ah <= 0) ans_fit(text, &wa, &aw, &ah, g_cardEdit, card_font());
  if (aw < ANS_W) aw = ANS_W;
  {
    int row = btn_row_width(g_card, kCardBtns, 5);
    if (aw < row) aw = row;
  }
  if (aw > wa.right - wa.left) aw = wa.right - wa.left;
  /* карточка остаётся там, куда её положили: прыгать к мыши ей незачем,
     читают её долго. Первый раз — рядом с находками, чтобы не накрывать их */
  int x = g_cardX, y = g_cardY;
  if (!x && !y) {
    RECT ar;
    if (g_answer && IsWindowVisible(g_answer) && GetWindowRect(g_answer, &ar)) {
      /* справа от находок, а если справа не влезает — слева: закрывать
         собой список ей незачем, ради этого всё и затевалось */
      y = ar.top;
      if (ar.right + 10 + aw <= wa.right) x = ar.right + 10;
      else if (ar.left - 10 - aw >= wa.left) x = ar.left - 10 - aw;
      else x = wa.right - aw - 8;
    } else {
      x = pt.x + 18;
      y = pt.y + 22;
    }
  }
  if (x + aw > wa.right) x = wa.right - aw - 8;
  if (y + ah > wa.bottom) y = wa.bottom - ah - 8;
  if (x < wa.left) x = wa.left + 8;
  if (y < wa.top) y = wa.top + 8;
  g_cardBig = FALSE;
  SetWindowPos(g_card, HWND_TOPMOST, x, y, aw, ah, SWP_SHOWWINDOW);
  card_sync_buttons();
  InvalidateRect(g_card, NULL, TRUE);
}

/* ---- распознанный текст: своё окно ------------------------------------ */
/* Раньше результат падал в окно находок и затирал выжимку. Теперь
   окно своё, и текст в нём можно править: распознавание путает «О» с нулём,
   а искать потом по испорченному обозначению бесполезно. */
static void layout_ocr(void);
static void start_ocr_pick(void);

static HFONT ocr_font(void) {
  if (g_ocrFontZoom) return g_ocrFontZoom;
  g_ocrFontZoom = make_font(g_faceBody, g_ocrPt, FW_NORMAL);
  return g_ocrFontZoom;
}

/* Сменилась тема — у находок и распознанного текста свой шрифт с масштабом,
   его собираем заново из шрифта темы. Карточка остаётся моноширинной: её
   колонки на другом шрифте разъедутся. */
static void theme_zoom_fonts_reset(void) {
  if (g_ansFontZoom) {
    DeleteObject(g_ansFontZoom);
    g_ansFontZoom = NULL;
  }
  if (g_answerEdit) SendMessageW(g_answerEdit, WM_SETFONT, (WPARAM)ans_font(), TRUE);
  if (g_ocrFontZoom) {
    DeleteObject(g_ocrFontZoom);
    g_ocrFontZoom = NULL;
  }
  if (g_ocrEdit) SendMessageW(g_ocrEdit, WM_SETFONT, (WPARAM)ocr_font(), TRUE);
}

static void ocr_zoom(int delta) {
  int pt = g_ocrPt + delta;
  if (pt < 7) pt = 7;
  if (pt > 22) pt = 22;
  if (pt == g_ocrPt) return;
  g_ocrPt = pt;
  if (g_ocrFontZoom) {
    DeleteObject(g_ocrFontZoom);
    g_ocrFontZoom = NULL;
  }
  if (g_ocrEdit) {
    SendMessageW(g_ocrEdit, WM_SETFONT, (WPARAM)ocr_font(), TRUE);
    InvalidateRect(g_ocrEdit, NULL, TRUE);
  }
  save_cursor_pref();
}

static WNDPROC g_oldOcrEdit;

static LRESULT CALLBACK OcrEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_MOUSEWHEEL && (GetKeyState(VK_CONTROL) & 0x8000)) {
    ocr_zoom(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
    return 0;
  }
  return CallWindowProcW(g_oldOcrEdit, hwnd, msg, wParam, lParam);
}

static void layout_ocr(void) {
  if (!g_ocrWnd) return;
  RECT rc;
  GetClientRect(g_ocrWnd, &rc);
  int pad = 12, btnH = 28, gap = 7;
  int top = PANEL_TITLE_H + 6;
  int by = rc.bottom - pad - btnH;
  if (g_ocrEdit) MoveWindow(g_ocrEdit, pad, top, rc.right - pad * 2, by - top - 6, TRUE);
  HWND btns[4];
  btns[0] = GetDlgItem(g_ocrWnd, ID_OCR_COPY);
  btns[1] = GetDlgItem(g_ocrWnd, ID_OCR_FIND);
  btns[2] = GetDlgItem(g_ocrWnd, ID_OCR_AGAIN);
  btns[3] = GetDlgItem(g_ocrWnd, ID_OCR_CLOSE);
  int bwid[4] = {0, 0, 0, 0};
  int total = 0, vis = 0;
  HDC dc = GetDC(g_ocrWnd);
  HGDIOBJ oldFont = (dc && g_fontUi) ? SelectObject(dc, g_fontUi) : NULL;
  for (int k = 0; k < 4; k++) {
    if (!btns[k]) continue;
    wchar_t t[96];
    t[0] = 0;
    GetWindowTextW(btns[k], t, 96);
    SIZE sz;
    sz.cx = 60;
    sz.cy = 0;
    if (dc) GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    bwid[k] = sz.cx + 24;
    if (bwid[k] < 56) bwid[k] = 56;
    total += bwid[k];
    vis++;
  }
  if (oldFont) SelectObject(dc, oldFont);
  if (dc) ReleaseDC(g_ocrWnd, dc);
  if (vis > 0) {
    int avail = rc.right - pad * 2 - gap * (vis - 1);
    if (avail < vis * 40) avail = vis * 40;
    if (total > avail && total > 0)
      for (int k = 0; k < 4; k++) bwid[k] = bwid[k] * avail / total;
    int bx = pad;
    for (int k = 0; k < 4; k++) {
      if (!btns[k]) continue;
      MoveWindow(btns[k], bx, by, bwid[k], btnH, TRUE);
      ShowWindow(btns[k], SW_SHOW);
      bx += bwid[k] + gap;
    }
  }
}

static BOOL ocr_is_digit(wchar_t c) {
  return c >= L'0' && c <= L'9';
}

/* В обозначении буквам взяться неоткуда: где вокруг цифры, «О» — это ноль,
   «З» — тройка, «б» — шестёрка, «l» — единица. Правим только в цифровом
   окружении, иначе испортим буквенный код вроде «АДЕ» или «ГОСТ». */
static wchar_t ocr_digitize(wchar_t c) {
  switch (c) {
  case L'\u041e':
  case L'\u043e':
  case L'O':
  case L'o':
    return L'0';
  case L'\u0417':
  case L'\u0437':
    return L'3';
  case L'\u0431':
    return L'6';
  case L'l':
  case L'I':
  case L'|':
    return L'1';
  default:
    return c;
  }
}

static BOOL ocr_joiner(wchar_t c) {
  return c == L'.' || c == L'-';
}

/* Строка из распознавания — в то, что имеет смысл искать. Сначала убираем
   пробел, придуманный возле точки или дефиса («3422 -682» в базе не найдётся
   никогда), потом разбираем на куски из цифр, точек, дефисов и похожих
   на цифры букв. Если в куске есть хоть одна настоящая цифра — это число,
   и все буквы в нём тоже цифры. Смотреть только на соседей нельзя: в «.Ol.» у
   каждой буквы сосед — такая же буква, и ни одна так не исправляется. */
static void ocr_fix_query(const wchar_t *in, wchar_t *out, int cap) {
  int n = 0;
  for (int i = 0; in[i] && n < cap - 1; i++) {
    wchar_t c = in[i];
    if (c == L' ') {
      const wchar_t *p = in + i;
      while (*p == L' ') p++;
      wchar_t prev = n > 0 ? out[n - 1] : 0;
      wchar_t next = *p;
      BOOL prevOk = ocr_is_digit(prev) || ocr_joiner(prev);
      BOOL nextOk = ocr_is_digit(next) || ocr_joiner(next);
      if (prevOk && nextOk && (ocr_joiner(prev) || ocr_joiner(next))) {
        i = (int)(p - in) - 1;
        continue;
      }
    }
    out[n++] = c;
  }
  out[n] = 0;
  for (int i = 0; out[i];) {
    if (!ocr_is_digit(out[i]) && !ocr_joiner(out[i]) && ocr_digitize(out[i]) == out[i]) {
      i++;
      continue;
    }
    int j = i;
    BOOL hasDigit = FALSE;
    while (out[j] && (ocr_is_digit(out[j]) || ocr_joiner(out[j]) ||
                      ocr_digitize(out[j]) != out[j])) {
      if (ocr_is_digit(out[j])) hasDigit = TRUE;
      j++;
    }
    if (hasDigit)
      for (int k = i; k < j; k++) out[k] = ocr_digitize(out[k]);
    i = j;
  }
}

/* Что искать: выделенное в окне, а если ничего не выделено — первая
   непустая строка. Целиком распознанный лист в поиск отправлять бессмысленно. */
static void ocr_find_selected(void) {
  if (!g_ocrEdit) return;
  DWORD a = 0, b = 0;
  SendMessageW(g_ocrEdit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
  int len = GetWindowTextLengthW(g_ocrEdit);
  wchar_t *all = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
  if (!all) return;
  GetWindowTextW(g_ocrEdit, all, len + 1);
  wchar_t q[400];
  q[0] = 0;
  if (b > a && b - a < 399) {
    lstrcpynW(q, all + a, (int)(b - a) + 1);
  } else {
    const wchar_t *p = all;
    while (*p == L'\r' || *p == L'\n' || *p == L' ' || *p == L'\t') p++;
    int i = 0;
    while (p[i] && p[i] != L'\r' && p[i] != L'\n' && i < 399) {
      q[i] = p[i];
      i++;
    }
    q[i] = 0;
  }
  free(all);
  int n = (int)wcslen(q);
  while (n > 0 && (q[n - 1] == L' ' || q[n - 1] == L'\t')) q[--n] = 0;
  if (!q[0]) {
    show_status(L"Нечего искать — выделите текст");
    return;
  }
  wchar_t fixed[400];
  ocr_fix_query(q, fixed, 400);
  if (fixed[0] && wcscmp(fixed, q) != 0) {
    /* поправку не прячем: видно, что именно ушло в поиск */
    wchar_t m[440];
    _snwprintf(m, 440, L"Ищу с поправкой: %.200s", fixed);
    show_status(m);
    start_lookup(fixed);
    return;
  }
  start_lookup(q);
}

static RECT g_ocrPrev;
static BOOL g_ocrBig;

static void ocr_toggle_big(HWND hwnd) {
  POINT pt;
  RECT wa, wr;
  GetWindowRect(hwnd, &wr);
  pt.x = (wr.left + wr.right) / 2;
  pt.y = (wr.top + wr.bottom) / 2;
  get_work_area(pt, &wa);
  if (!g_ocrBig) {
    g_ocrPrev = wr;
    g_ocrBig = TRUE;
    SetWindowPos(hwnd, HWND_TOPMOST, wa.left + 8, wa.top + 8, wa.right - wa.left - 16,
                 wa.bottom - wa.top - 16, SWP_SHOWWINDOW);
  } else {
    g_ocrBig = FALSE;
    SetWindowPos(hwnd, HWND_TOPMOST, g_ocrPrev.left, g_ocrPrev.top,
                 g_ocrPrev.right - g_ocrPrev.left, g_ocrPrev.bottom - g_ocrPrev.top,
                 SWP_SHOWWINDOW);
  }
  layout_ocr();
}

static LRESULT CALLBACK OcrProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  /* поле текста — цветами темы; без этого оно было системным серым во всех темах
     (поле только для чтения спрашивает цвет через CTLCOLORSTATIC) */
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, bg_brush(FALSE));
    draw_panel_header(hwnd, hdc, L"Распознанный текст");
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_MOUSEWHEEL:
    if (GetKeyState(VK_CONTROL) & 0x8000) {
      ocr_zoom(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1);
      return 0;
    }
    break;
  case WM_NCLBUTTONDBLCLK:
    ocr_toggle_big(hwnd);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_OCR_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (LOWORD(wParam) == ID_OCR_COPY && g_ocrEdit) {
      int len = GetWindowTextLengthW(g_ocrEdit);
      wchar_t *w = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
      if (w) {
        GetWindowTextW(g_ocrEdit, w, len + 1);
        clipboard_set(w);
        free(w);
        show_status(L"Распознанное скопировано");
      }
    }
    if (LOWORD(wParam) == ID_OCR_FIND) ocr_find_selected();
    if (LOWORD(wParam) == ID_OCR_AGAIN) start_ocr_pick();
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_EXITSIZEMOVE:
    layout_ocr();
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  case WM_MOVE:
    if (!g_ocrBig && IsWindowVisible(hwnd)) {
      RECT wr;
      GetWindowRect(hwnd, &wr);
      g_ocrX = wr.left;
      g_ocrY = wr.top;
    }
    return 0;
  case WM_SIZE:
    layout_ocr();
    InvalidateRect(hwnd, NULL, TRUE);
    if (wParam != SIZE_MINIMIZED && !g_ocrBig) {
      RECT wr;
      GetWindowRect(hwnd, &wr);
      g_ocrX = wr.left;
      g_ocrY = wr.top;
      g_ocrW = wr.right - wr.left;
      g_ocrH = wr.bottom - wr.top;
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void create_ocr(HWND owner) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = OcrProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = g_paper;
  wc.lpszClassName = L"CursorPadOcrWnd";
  RegisterClassExW(&wc);
  g_ocrWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadOcrWnd",
                             L"Распознанный текст",
                             WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN, 0, 0, ANS_W, 300, owner,
                             NULL, g_inst, NULL);
  round_corners(g_ocrWnd);
  /* поле не только для чтения: одну букву поправить бывает нужнее всего */
  g_ocrEdit = CreateWindowExW(0, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                              0, 0, 100, 100, g_ocrWnd, NULL, NULL, NULL);
  g_oldOcrEdit = (WNDPROC)SetWindowLongPtrW(g_ocrEdit, GWLP_WNDPROC, (LONG_PTR)OcrEditProc);
  HWND copy = mk_btn(g_ocrWnd, L"Копировать", ID_OCR_COPY);
  HWND find = mk_btn(g_ocrWnd, L"Найти это", ID_OCR_FIND);
  HWND again = mk_btn(g_ocrWnd, L"Распознать ещё", ID_OCR_AGAIN);
  HWND cls = mk_btn(g_ocrWnd, L"Закрыть", ID_OCR_CLOSE);
  SendMessageW(g_ocrEdit, WM_SETFONT, (WPARAM)ocr_font(), TRUE);
  if (g_fontUi) {
    SendMessageW(copy, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(find, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(again, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(cls, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  }
  layout_ocr();
}

static void show_ocr_text(const wchar_t *text) {
  if (!g_ocrWnd) return;
  if (g_ocrEdit) {
    SendMessageW(g_ocrEdit, WM_SETFONT, (WPARAM)ocr_font(), TRUE);
    SetWindowTextW(g_ocrEdit, text ? text : L"");
  }
  POINT pt;
  RECT wa;
  GetCursorPos(&pt);
  get_work_area(pt, &wa);
  int aw = g_ocrW > 0 ? g_ocrW : ANS_W;
  int ah = g_ocrH > 0 ? g_ocrH : 300;
  if (aw < 420) aw = 420;
  {
    int row = btn_row_width(g_ocrWnd, kOcrBtns, 4);
    if (aw < row) aw = row;
  }
  if (aw > wa.right - wa.left) aw = wa.right - wa.left;
  /* где окно оставили — там и откроется: читают и правят его долго */
  int x = g_ocrX, y = g_ocrY;
  if (!x && !y) {
    x = pt.x + 18;
    y = pt.y + 22;
  }
  if (x + aw > wa.right) x = wa.right - aw - 8;
  if (y + ah > wa.bottom) y = wa.bottom - ah - 8;
  if (x < wa.left) x = wa.left + 8;
  if (y < wa.top) y = wa.top + 8;
  g_ocrBig = FALSE;
  SetWindowPos(g_ocrWnd, HWND_TOPMOST, x, y, aw, ah, SWP_SHOWWINDOW);
  layout_ocr();
  InvalidateRect(g_ocrWnd, NULL, TRUE);
  if (g_ocrEdit) SetFocus(g_ocrEdit);
}

static void start_lookup(const wchar_t *q) {
  if (!q) return;
  /* пароль ввели и сразу ищут, не уходя из поля, — берём его и так */
  if (g_engine == 4) save_plm_pref();
  g_ansTitle = NULL;
  g_selDraw[0] = 0;
  InterlockedIncrement(&g_drawGen);
  /* открытую карточку новый поиск не трогает: на то она и отдельное окно */
  while (*q == L' ' || *q == L'\t' || *q == L'\r' || *q == L'\n') q++;
  if (!q[0]) {
    show_status(L"Нечего искать — скопируйте текст");
    return;
  }
  /* сперва — не идёт ли уже поиск: раньше «Ищу…» затирало окно, а потом
     приходил ответ того, прежнего поиска */
  if (InterlockedCompareExchange(&g_netBusy, 1, 0) != 0) {
    show_status(L"Поиск уже идёт");
    return;
  }
  /* список прошлого поиска убираем сразу: иначе окно открывалось со старыми
     находками и только потом менялось на новые */
  g_plmCount = 0;
  InterlockedIncrement(&g_pfGen);
  wchar_t wait[440];
  _snwprintf(wait, 440,
             g_engine == 4 ? L"Ищу в PLM «%.80s»…" :
             (g_engine == 5 ? L"Ищу файлы «%.80s»…" : L"Мини-ИИ «%.80s»…"), q);
  show_answer_text(wait);
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

/* Распознаванию Windows нужна буква высотой от двадцати пяти точек, а на
   чертеже в окне просмотра она раза в два мельче. Растягиваем вырезку со
   сглаживанием: мелкое тянем втрое, среднее вдвое, крупное оставляем. */
static int ocr_scale_for(int bw, int bh) {
  int big = bw > bh ? bw : bh;
  int k = 1;
  if (big < 500) k = 3;
  else if (big < 1200) k = 2;
  while (k > 1 && ((long)bw * k > 4000 || (long)bh * k > 4000)) k--;
  return k;
}

/* Чертёж — чёрные линии на белом, но на экране это серое на сероватом.
   Переводим в серое, растягиваем контраст по крайним процентам и, если фон
   тёмный (бывает в просмотрщиках с чёрным полем), обращаем: распознавание
   ждёт тёмный текст на светлом. */
static void ocr_clean_bits(unsigned char *bits, int w, int h, int row) {
  long hist[256];
  memset(hist, 0, sizeof(hist));
  long total = 0;
  for (int yy = 0; yy < h; yy++) {
    unsigned char *p = bits + (size_t)yy * row;
    for (int xx = 0; xx < w; xx++, p += 3) {
      int v = (p[0] * 29 + p[1] * 150 + p[2] * 77) >> 8;
      if (v > 255) v = 255;
      p[0] = p[1] = p[2] = (unsigned char)v;
      hist[v]++;
      total++;
    }
  }
  if (total <= 0) return;
  long cut = total / 50; /* по два процента с краёв — единичный блик не решает */
  int lo = 0, hi = 255;
  long acc = 0;
  for (int i = 0; i < 256; i++) {
    acc += hist[i];
    if (acc > cut) {
      lo = i;
      break;
    }
  }
  acc = 0;
  for (int i = 255; i >= 0; i--) {
    acc += hist[i];
    if (acc > cut) {
      hi = i;
      break;
    }
  }
  if (hi - lo < 12) return; /* ровный фон — тянуть нечего, только шум поднимем */
  double mean = 0;
  for (int i = 0; i < 256; i++) mean += (double)hist[i] * i;
  mean /= (double)total;
  BOOL invert = mean < 110.0;
  unsigned char map[256];
  for (int i = 0; i < 256; i++) {
    int v = i;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    v = (v - lo) * 255 / (hi - lo);
    if (invert) v = 255 - v;
    map[i] = (unsigned char)v;
  }
  for (int yy = 0; yy < h; yy++) {
    unsigned char *p = bits + (size_t)yy * row;
    for (int xx = 0; xx < w; xx++, p += 3) {
      unsigned char v = map[p[0]];
      p[0] = p[1] = p[2] = v;
    }
  }
}

static BOOL save_rect_bmp(int x, int y, int bw, int bh, const wchar_t *path) {
  if (bw < 8) bw = 8;
  if (bh < 8) bh = 8;
  if (bw > 2400) bw = 2400;
  if (bh > 1600) bh = 1600;
  int k = ocr_scale_for(bw, bh);
  int ow = bw * k, oh = bh * k;
  HDC screen = GetDC(NULL);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bm = CreateCompatibleBitmap(screen, bw, bh);
  HGDIOBJ old = SelectObject(mem, bm);
  BitBlt(mem, 0, 0, bw, bh, screen, x, y, SRCCOPY);
  HDC big = NULL;
  HBITMAP bigBm = NULL;
  HGDIOBJ oldBig = NULL;
  if (k > 1) {
    big = CreateCompatibleDC(screen);
    bigBm = CreateCompatibleBitmap(screen, ow, oh);
    if (big && bigBm) {
      oldBig = SelectObject(big, bigBm);
      /* HALFTONE даёт сглаживание; без SetBrushOrgEx он мусорит по краям */
      SetStretchBltMode(big, HALFTONE);
      SetBrushOrgEx(big, 0, 0, NULL);
      StretchBlt(big, 0, 0, ow, oh, mem, 0, 0, bw, bh, SRCCOPY);
    } else {
      if (bigBm) DeleteObject(bigBm);
      if (big) DeleteDC(big);
      big = NULL;
      bigBm = NULL;
      k = 1;
      ow = bw;
      oh = bh;
    }
  }
  HDC srcDc = big ? big : mem;
  HBITMAP srcBm = bigBm ? bigBm : bm;
  BITMAPINFOHEADER ih;
  memset(&ih, 0, sizeof(ih));
  ih.biSize = sizeof(ih);
  ih.biWidth = ow;
  ih.biHeight = oh;
  ih.biPlanes = 1;
  ih.biBitCount = 24;
  int row = (ow * 3 + 3) & ~3;
  int img = row * oh;
  char *bits = (char *)malloc((size_t)img);
  BOOL ok = FALSE;
  if (bits) {
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader = ih;
    GetDIBits(srcDc, srcBm, 0, (UINT)oh, bits, &info, DIB_RGB_COLORS);
    ocr_clean_bits((unsigned char *)bits, ow, oh, row);
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
  if (big) {
    if (oldBig) SelectObject(big, oldBig);
    DeleteObject(bigBm);
    DeleteDC(big);
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
