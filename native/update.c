/* Included from pad_extra.c — GitHub self-update. */
#include "version.h"

#define ID_UPDATE 134
#define WM_UPDATE_DONE (WM_APP + 12)

static volatile LONG g_updBusy;
static wchar_t g_updPath[MAX_PATH];
static wchar_t g_updRemote[40];

static BOOL http_get_to_file(const wchar_t *host, const wchar_t *path, const wchar_t *dest, DWORD maxn) {
  DeleteFileW(dest);
  HANDLE f = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  HINTERNET ses = WinHttpOpen(L"CursorPad/" APP_VERSION_STR,
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  WinHttpSetTimeouts(ses, 5000, 8000, 30000, 60000);
  HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!con) {
    WinHttpCloseHandle(ses);
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!req) {
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  WinHttpAddRequestHeaders(req, L"Accept: */*\r\nCache-Control: no-cache\r\n",
                           (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
  BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (ok) ok = WinHttpReceiveResponse(req, NULL);
  DWORD total = 0;
  if (ok) {
    for (;;) {
      DWORD avail = 0, got = 0, w = 0;
      if (!WinHttpQueryDataAvailable(req, &avail)) {
        ok = FALSE;
        break;
      }
      if (!avail) break;
      if (total + avail > maxn) {
        ok = FALSE;
        break;
      }
      char chunk[16384];
      DWORD want = avail > sizeof(chunk) ? (DWORD)sizeof(chunk) : avail;
      if (!WinHttpReadData(req, chunk, want, &got) || !got) break;
      if (!WriteFile(f, chunk, got, &w, NULL) || w != got) {
        ok = FALSE;
        break;
      }
      total += got;
    }
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  CloseHandle(f);
  if (!ok || total < 8) {
    DeleteFileW(dest);
    return FALSE;
  }
  return TRUE;
}

static long parse_ver_file(const char *s) {
  while (*s == ' ' || *s == '\t') s++;
  long v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
  }
  return v;
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
  wchar_t tmpv[MAX_PATH], tmpe[MAX_PATH], tdir[MAX_PATH];
  GetTempPathW(MAX_PATH, tdir);
  _snwprintf(tmpv, MAX_PATH, L"%sCursorPad-version.txt", tdir);
  _snwprintf(tmpe, MAX_PATH, L"%sCursorPad-update.exe", tdir);
  int code = 0;
  if (!http_get_to_file(UPDATE_HOST, UPDATE_VER_PATH, tmpv, 4096)) {
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
    code = 0;
    goto done;
  }
  if (remote <= APP_VERSION) {
    code = 1;
    goto done;
  }
  if (!http_get_to_file(UPDATE_HOST, UPDATE_EXE_PATH, tmpe, 12 * 1024 * 1024) || !file_is_pe(tmpe)) {
    DeleteFileW(tmpe);
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
    show_status(L"GitHub недоступен или файл повреждён");
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
