/* Included from pad_extra.c — GitHub self-update. */
#include "version.h"

#define ID_UPDATE 134
#define WM_UPDATE_DONE (WM_APP + 12)

#ifndef WINHTTP_OPTION_SECURE_PROTOCOLS
#define WINHTTP_OPTION_SECURE_PROTOCOLS 84
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 0x00000080
#endif
#ifndef WINHTTP_OPTION_REDIRECT_POLICY
#define WINHTTP_OPTION_REDIRECT_POLICY 88
#endif
#ifndef WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS
#define WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS 2
#endif
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

static volatile LONG g_updBusy;
static wchar_t g_updPath[MAX_PATH];
static wchar_t g_updRemote[40];
static wchar_t g_updErr[240];

typedef struct {
  const wchar_t *host;
  const wchar_t *ver;
  const wchar_t *exe;
} UpdSrc;

static const UpdSrc kUpdSrc[] = {
    {L"raw.githubusercontent.com", L"/pidrpen/ytaqq/main/public/version.txt",
     L"/pidrpen/ytaqq/main/public/CursorPad.exe"},
    {L"github.com", L"/pidrpen/ytaqq/raw/main/public/version.txt",
     L"/pidrpen/ytaqq/raw/main/public/CursorPad.exe"},
    {L"cdn.jsdelivr.net", L"/gh/pidrpen/ytaqq@main/public/version.txt",
     L"/gh/pidrpen/ytaqq@main/public/CursorPad.exe"},
};

static void upd_fail(const wchar_t *why, DWORD err) {
  if (err)
    _snwprintf(g_updErr, 240, L"%s (код %lu)", why, err);
  else
    lstrcpynW(g_updErr, why, 240);
}

static void http_tune(HINTERNET ses) {
  DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
  proto |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
  WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto));
  DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(ses, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
  WinHttpSetTimeouts(ses, 8000, 10000, 30000, 90000);
}

static BOOL http_get_to_file(const wchar_t *host, const wchar_t *path, const wchar_t *dest, DWORD maxn) {
  DeleteFileW(dest);
  HANDLE f = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) {
    upd_fail(L"Не удалось создать временный файл", GetLastError());
    return FALSE;
  }
  HINTERNET ses = WinHttpOpen(L"CursorPad/" APP_VERSION_STR,
                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses)
    ses = WinHttpOpen(L"CursorPad/" APP_VERSION_STR, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    upd_fail(L"WinHTTP не открылся", GetLastError());
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  http_tune(ses);
  HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!con) {
    upd_fail(L"Нет соединения с хостом", GetLastError());
    WinHttpCloseHandle(ses);
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!req) {
    upd_fail(L"Запрос не создан", GetLastError());
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  WinHttpAddRequestHeaders(req,
                           L"Accept: text/plain,*/*\r\nAccept-Encoding: identity",
                           (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
#ifdef WINHTTP_OPTION_DECOMPRESSION
  {
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));
  }
#endif
  BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (ok) ok = WinHttpReceiveResponse(req, NULL);
  if (!ok) {
    upd_fail(L"TLS/сеть: GitHub не ответил", GetLastError());
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  DWORD status = 0, slen = sizeof(status);
  WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status,
                      &slen, WINHTTP_NO_HEADER_INDEX);
  if (status != 200) {
    _snwprintf(g_updErr, 240, L"%s → HTTP %lu", host, status);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  DWORD total = 0;
  ok = TRUE;
  for (;;) {
    DWORD avail = 0, got = 0, w = 0;
    if (!WinHttpQueryDataAvailable(req, &avail)) {
      ok = FALSE;
      upd_fail(L"Обрыв загрузки", GetLastError());
      break;
    }
    if (!avail) break;
    if (total + avail > maxn) {
      ok = FALSE;
      upd_fail(L"Файл слишком большой", 0);
      break;
    }
    char chunk[16384];
    DWORD want = avail > sizeof(chunk) ? (DWORD)sizeof(chunk) : avail;
    if (!WinHttpReadData(req, chunk, want, &got) || !got) break;
    if (!WriteFile(f, chunk, got, &w, NULL) || w != got) {
      ok = FALSE;
      upd_fail(L"Не записался временный файл", GetLastError());
      break;
    }
    total += got;
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  CloseHandle(f);
  if (!ok || total < 4) {
    if (ok) upd_fail(L"Пустой ответ", 0);
    DeleteFileW(dest);
    return FALSE;
  }
  return TRUE;
}

static BOOL http_get_any(const wchar_t *kind, const wchar_t *dest, DWORD maxn) {
  int n = (int)(sizeof(kUpdSrc) / sizeof(kUpdSrc[0]));
  for (int i = 0; i < n; i++) {
    const wchar_t *path = (kind[0] == L'e') ? kUpdSrc[i].exe : kUpdSrc[i].ver;
    if (http_get_to_file(kUpdSrc[i].host, path, dest, maxn)) return TRUE;
  }
  return FALSE;
}

static long parse_ver_file(const char *s) {
  if (!s || !s[0]) return 0;
  char tmp[96];
  int n = 0;
  const unsigned char *u = (const unsigned char *)s;
  if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) s += 3, u += 3;
  if (u[0] == 0x1F && u[1] == 0x8B) return 0; /* gzip */
  if (u[0] == 0xFF && u[1] == 0xFE) {
    const unsigned char *p = u + 2;
    while ((p[0] || p[1]) && n < 90) {
      if (p[1] == 0 && p[0] >= 32 && p[0] < 127) tmp[n++] = (char)p[0];
      p += 2;
    }
    tmp[n] = 0;
    s = tmp;
  }
  while (*s && (*s < '0' || *s > '9')) s++;
  if (!*s) return 0;
  const char *line = s;
  int dots = 0;
  for (const char *q = s; *q && *q != '\n' && *q != '\r'; q++)
    if (*q == '.') dots++;
  if (dots >= 2) {
    int y = 0, m = 0, d = 0, p = 0;
    if (sscanf(line, "%d.%d.%d.%d", &y, &m, &d, &p) >= 3 && y >= 2020 && y <= 2099)
      return (long)y * 1000000L + (long)m * 10000L + (long)d * 100L + (long)p;
  }
  long v = 0;
  int digits = 0;
  while (*s >= '0' && *s <= '9' && digits < 10) {
    v = v * 10 + (*s - '0');
    digits++;
    s++;
  }
  return digits >= 8 ? v : 0;
}

static BOOL file_is_pe(const wchar_t *path) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  unsigned char mz[2] = {0};
  DWORD r = 0;
  ReadFile(f, mz, 2, &r, NULL);
  CloseHandle(f);
  return r == 2 && mz[0] == 'M' && mz[1] == 'Z';
}

static void apply_update(const wchar_t *newexe) {
  wchar_t self[MAX_PATH] = {0};
  GetModuleFileNameW(NULL, self, MAX_PATH);
  wchar_t args[1200];
  _snwprintf(args, 1200,
             L"/C ping 127.0.0.1 -n 3 >nul & move /Y \"%s\" \"%s\" & start \"\" \"%s\"",
             newexe, self, self);
  ShellExecuteW(NULL, L"open", L"cmd.exe", args, NULL, SW_HIDE);
  if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
}

static DWORD WINAPI update_thread(LPVOID param) {
  (void)param;
  g_updRemote[0] = 0;
  g_updPath[0] = 0;
  g_updErr[0] = 0;
  wchar_t tmpv[MAX_PATH], tmpe[MAX_PATH], tdir[MAX_PATH];
  GetTempPathW(MAX_PATH, tdir);
  _snwprintf(tmpv, MAX_PATH, L"%sCursorPad-version.txt", tdir);
  _snwprintf(tmpe, MAX_PATH, L"%sCursorPad-update.exe", tdir);
  int code = 0;
  if (!http_get_any(L"v", tmpv, 4096)) {
    if (!g_updErr[0]) upd_fail(L"GitHub/CDN не отдали version.txt", 0);
    code = 0;
    goto done;
  }
  HANDLE h = CreateFileW(tmpv, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  char buf[128] = {0};
  DWORD n = 0;
  if (h != INVALID_HANDLE_VALUE) {
    ReadFile(h, buf, 120, &n, NULL);
    CloseHandle(h);
  }
  DeleteFileW(tmpv);
  long remote = parse_ver_file(buf);
  const char *nl = strchr(buf, '\n');
  if (nl) {
    while (*nl == '\n' || *nl == '\r' || *nl == ' ') nl++;
    char vis[32];
    int i = 0;
    while (nl[i] && nl[i] != '\r' && nl[i] != '\n' && i < 30) {
      vis[i] = nl[i];
      i++;
    }
    vis[i] = 0;
    MultiByteToWideChar(CP_UTF8, 0, vis, -1, g_updRemote, 40);
  }
  if (!g_updRemote[0]) _snwprintf(g_updRemote, 40, L"%ld", remote);
  if (remote <= 0) {
    wchar_t snip[48] = {0};
    int i = 0, j = 0;
    for (; buf[i] && j < 40; i++) {
      unsigned char c = (unsigned char)buf[i];
      if (c >= 32 && c < 127)
        snip[j++] = (wchar_t)c;
      else if (c == '\n' || c == '\r')
        snip[j++] = L' ';
      else
        snip[j++] = L'.';
    }
    _snwprintf(g_updErr, 240, L"Непонятный version.txt: «%s»", snip[0] ? snip : L"пусто");
    code = 0;
    goto done;
  }
  if (remote <= APP_VERSION) {
    code = 1;
    goto done;
  }
  if (!http_get_any(L"e", tmpe, 12 * 1024 * 1024) || !file_is_pe(tmpe)) {
    DeleteFileW(tmpe);
    if (!g_updErr[0]) upd_fail(L"Не скачался CursorPad.exe", 0);
    code = 0;
    goto done;
  }
  lstrcpynW(g_updPath, tmpe, MAX_PATH);
  code = 2;
done:
  InterlockedExchange(&g_updBusy, 0);
  if (g_hwnd) PostMessageW(g_hwnd, WM_UPDATE_DONE, (WPARAM)code, 0);
  return 0;
}

static void start_update(void) {
  if (InterlockedCompareExchange(&g_updBusy, 1, 0) != 0) {
    show_status(L"Обновление уже идёт");
    return;
  }
  show_status(L"Проверяю GitHub…");
  HANDLE th = CreateThread(NULL, 0, update_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else {
    InterlockedExchange(&g_updBusy, 0);
    show_status(L"Не удалось запустить проверку");
  }
}

static void on_update_done(int code) {
  if (code == 1) {
    wchar_t m[160];
    _snwprintf(m, 160, L"Уже последняя версия (%s)", APP_VERSION_STR);
    show_status(m);
    return;
  }
  if (code != 2 || !g_updPath[0]) {
    show_status(g_updErr[0] ? g_updErr : L"GitHub недоступен");
    return;
  }
  wchar_t msg[280];
  _snwprintf(msg, 280,
             L"На GitHub версия %s (сейчас %s).\r\nСкачать, заменить .exe и перезапустить?",
             g_updRemote, APP_VERSION_STR);
  int r = MessageBoxW(g_hwnd, msg, L"CursorPad — обновление", MB_YESNO | MB_ICONQUESTION);
  if (r == IDYES) apply_update(g_updPath);
  else DeleteFileW(g_updPath);
}
