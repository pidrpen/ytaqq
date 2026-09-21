/* Included from pad_extra.c — GitHub self-update. No admin, no UAC. */
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
#ifndef WINHTTP_OPTION_AUTOLOGON_POLICY
#define WINHTTP_OPTION_AUTOLOGON_POLICY 77
#endif
#ifndef WINHTTP_AUTOLOGON_SECURITY_LEVEL_LOW
#define WINHTTP_AUTOLOGON_SECURITY_LEVEL_LOW 0
#endif
#ifndef WINHTTP_AUTH_SCHEME_NTLM
#define WINHTTP_AUTH_SCHEME_NTLM 0x00000002
#endif
#ifndef WINHTTP_AUTH_SCHEME_DIGEST
#define WINHTTP_AUTH_SCHEME_DIGEST 0x00000008
#endif
#ifndef WINHTTP_AUTH_SCHEME_NEGOTIATE
#define WINHTTP_AUTH_SCHEME_NEGOTIATE 0x00000010
#endif
#ifndef CRYPT_STRING_BASE64
#define CRYPT_STRING_BASE64 0x00000001
#endif
#ifndef SEE_MASK_NOZONECHECKS
#define SEE_MASK_NOZONECHECKS 0x00800000
#endif

static volatile LONG g_updBusy;
static wchar_t g_updPath[MAX_PATH];
static wchar_t g_updRemote[40];
static wchar_t g_updErr[240];
static wchar_t g_updLog[3000]; /* every step, so a failure can be read and sent */
static int g_updLogLen;

/* WinHTTP numbers mean nothing to the person reading the report. */
static const wchar_t *wh_reason(DWORD err) {
  switch (err) {
  case 12002: return L"истекло время ожидания";
  case 12007: return L"адрес не разрешается — нет интернета или DNS";
  case 12029: return L"не удалось подключиться";
  case 12030:
  case 12031: return L"соединение оборвалось";
  case 12045:
  case 12175: return L"ошибка защищённого соединения (сертификат)";
  case 12165:
  case 12186: return L"мешает прокси-сервер";
  default: return NULL;
  }
}

static void upd_log(const wchar_t *fmt, ...) {
  if (g_updLogLen > 2800) return;
  wchar_t line[320];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(line, 320, fmt, ap);
  va_end(ap);
  line[319] = 0;
  int n = (int)wcslen(line);
  if (g_updLogLen + n + 3 >= 3000) return;
  if (g_updLogLen) {
    g_updLog[g_updLogLen++] = L'\r';
    g_updLog[g_updLogLen++] = L'\n';
  }
  memcpy(g_updLog + g_updLogLen, line, (size_t)n * sizeof(wchar_t));
  g_updLogLen += n;
  g_updLog[g_updLogLen] = 0;
}
static wchar_t g_updLaunch[MAX_PATH];
static wchar_t g_updHostUsed[80];
static int g_updAuthWall; /* something in this network demanded a login */
static wchar_t g_updPin[96]; /* immutable ref (tag or commit) the CDN can't stale */
static BOOL g_updPinned;

typedef struct {
  const wchar_t *host;
  const wchar_t *ver;
  const wchar_t *exe;
  const wchar_t *hdr;
  int api;  /* unwrap GitHub contents/blob JSON (no raw.githubusercontent.com) */
  int bust; /* cache-bust query */
} UpdSrc;

/* jsDelivr refuses .exe by extension, so the same bytes are published as
   CursorPad.bin for the CDN mirrors; the GitHub hosts keep the plain name. */
static const UpdSrc kUpdSrc[] = {
    {L"cdn.jsdelivr.net", L"/gh/pidrpen/ytaqq@main/public/version.txt",
     L"/gh/pidrpen/ytaqq@main/public/CursorPad.bin", NULL, 0, 1},
    {L"fastly.jsdelivr.net", L"/gh/pidrpen/ytaqq@main/public/version.txt",
     L"/gh/pidrpen/ytaqq@main/public/CursorPad.bin", NULL, 0, 1},
    {L"gcore.jsdelivr.net", L"/gh/pidrpen/ytaqq@main/public/version.txt",
     L"/gh/pidrpen/ytaqq@main/public/CursorPad.bin", NULL, 0, 1},
    {L"raw.githubusercontent.com", L"/pidrpen/ytaqq/main/public/version.txt",
     L"/pidrpen/ytaqq/main/public/CursorPad.exe", NULL, 0, 0},
    {L"github.com", L"/pidrpen/ytaqq/raw/main/public/version.txt",
     L"/pidrpen/ytaqq/raw/main/public/CursorPad.exe", NULL, 0, 0},
    {L"api.github.com", L"/repos/pidrpen/ytaqq/contents/public/version.txt",
     L"/repos/pidrpen/ytaqq/contents/public/CursorPad.exe",
     L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28", 1, 0},
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

static const char *json_find_string(const char *json, const char *key) {
  char pat[72];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = json;
  while ((p = strstr(p, pat))) {
    p += strlen(pat);
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p != ':') continue;
    p++;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p == '"') return p + 1;
    return NULL;
  }
  return NULL;
}

static int b64_clean(const char *in, char *out, int cap) {
  /* GitHub wraps base64 with JSON "\n" — the letter n is valid base64, so
     we must unescape instead of copying every alphabet character. */
  int o = 0;
  while (*in && *in != '"') {
    unsigned char c;
    if (*in == '\\' && in[1]) {
      in++;
      if (*in == 'n' || *in == 'r' || *in == 't') {
        in++;
        continue;
      }
      if (*in == 'u') {
        in++;
        for (int i = 0; i < 4 && *in; i++) in++;
        continue;
      }
      c = (unsigned char)*in++;
    } else {
      c = (unsigned char)*in++;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
        c == '/' || c == '=') {
      if (o + 1 < cap) out[o++] = (char)c;
    }
  }
  if (out && cap > 0) out[o] = 0;
  return o;
}

static long json_find_int(const char *json, const char *key) {
  char pat[72];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = json;
  while ((p = strstr(p, pat))) {
    p += strlen(pat);
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p != ':') continue;
    p++;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return -1;
    long v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v;
  }
  return -1;
}

static BOOL write_all(const wchar_t *dest, const void *p, DWORD n) {
  HANDLE f = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  DWORD w = 0;
  BOOL ok = WriteFile(f, p, n, &w, NULL) && w == n;
  CloseHandle(f);
  return ok;
}

static BOOL b64_to_file(const char *b64, int nb64, const wchar_t *dest) {
  DWORD n = 0;
  if (!CryptStringToBinaryA(b64, (DWORD)nb64, CRYPT_STRING_BASE64, NULL, &n, NULL, NULL) || n < 2)
    return FALSE;
  BYTE *raw = (BYTE *)malloc(n);
  if (!raw) return FALSE;
  BOOL ok = CryptStringToBinaryA(b64, (DWORD)nb64, CRYPT_STRING_BASE64, raw, &n, NULL, NULL) &&
            write_all(dest, raw, n);
  free(raw);
  return ok;
}

static BOOL read_file_bytes(const wchar_t *path, char **out, DWORD *outn, DWORD maxn) {
  *out = NULL;
  *outn = 0;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  DWORD sz = GetFileSize(f, NULL);
  if (sz == INVALID_FILE_SIZE || sz < 2 || sz > maxn) {
    CloseHandle(f);
    return FALSE;
  }
  char *buf = (char *)malloc(sz + 1);
  if (!buf) {
    CloseHandle(f);
    return FALSE;
  }
  DWORD r = 0;
  BOOL ok = ReadFile(f, buf, sz, &r, NULL);
  CloseHandle(f);
  if (!ok) {
    free(buf);
    return FALSE;
  }
  buf[r] = 0;
  *out = buf;
  *outn = r;
  return TRUE;
}

static BOOL looks_like_html(const char *s) {
  while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
  return s[0] == '<' || (s[0] == '{' && strstr(s, "\"message\"") && strstr(s, "\"Not Found\""));
}

static BOOL http_get_to_file(const wchar_t *host, const wchar_t *path, const wchar_t *dest,
                             DWORD maxn, const wchar_t *hdr);

static BOOL github_blob_get(const char *sha, const wchar_t *dest, DWORD maxn);

static BOOL unwrap_github_json(const wchar_t *dest, DWORD maxn) {
  char *buf = NULL;
  DWORD n = 0;
  if (!read_file_bytes(dest, &buf, &n, maxn)) return FALSE;
  while (n && (buf[0] == ' ' || buf[0] == '\n' || buf[0] == '\r' || buf[0] == '\t')) {
    memmove(buf, buf + 1, n--);
    buf[n] = 0;
  }
  if (n < 2 || buf[0] != '{') {
    free(buf);
    return TRUE; /* already raw */
  }
  const char *content = json_find_string(buf, "content");
  const char *sha = json_find_string(buf, "sha");
  char shahex[48] = {0};
  if (sha) {
    int i = 0;
    while (sha[i] && ((sha[i] >= '0' && sha[i] <= '9') || (sha[i] >= 'a' && sha[i] <= 'f') ||
                      (sha[i] >= 'A' && sha[i] <= 'F')) &&
           i < 40) {
      shahex[i] = sha[i];
      i++;
    }
  }
  if (content && content[0] && content[0] != '"') {
    long expect = json_find_int(buf, "size");
    char *clean = (char *)malloc(n + 4);
    if (!clean) {
      free(buf);
      return FALSE;
    }
    int nc = b64_clean(content, clean, (int)n + 2);
    free(buf);
    BOOL ok = nc >= 4 && b64_to_file(clean, nc, dest);
    free(clean);
    if (ok && expect > 0) {
      HANDLE hf = CreateFileW(dest, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
      DWORD got = (hf == INVALID_HANDLE_VALUE) ? 0 : GetFileSize(hf, NULL);
      if (hf != INVALID_HANDLE_VALUE) CloseHandle(hf);
      if ((long)got != expect) {
        DeleteFileW(dest);
        upd_fail(L"GitHub API: размер файла не совпал", 0);
        ok = FALSE;
      }
    }
    if (ok) return TRUE;
    if (shahex[0]) return github_blob_get(shahex, dest, maxn);
    return FALSE;
  }
  free(buf);
  if (shahex[0]) return github_blob_get(shahex, dest, maxn);
  upd_fail(L"GitHub API: пустой ответ", 0);
  return FALSE;
}

static BOOL github_blob_get(const char *sha, const wchar_t *dest, DWORD maxn) {
  wchar_t path[180], wsha[48];
  MultiByteToWideChar(CP_UTF8, 0, sha, -1, wsha, 48);
  _snwprintf(path, 180, L"/repos/pidrpen/ytaqq/git/blobs/%s", wsha);
  if (!http_get_to_file(L"api.github.com", path, dest, maxn,
                        L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28"))
    return FALSE;
  /* blob JSON also has base64 content — unwrap once, no further blob recursion */
  char *buf = NULL;
  DWORD n = 0;
  if (!read_file_bytes(dest, &buf, &n, maxn)) return FALSE;
  const char *content = json_find_string(buf, "content");
  BOOL ok = FALSE;
  if (content) {
    char *clean = (char *)malloc(n + 4);
    if (clean) {
      int nc = b64_clean(content, clean, (int)n + 2);
      ok = nc >= 4 && b64_to_file(clean, nc, dest);
      free(clean);
    }
  } else if (n >= 2 && (unsigned char)buf[0] == 'M' && (unsigned char)buf[1] == 'Z') {
    ok = TRUE; /* already raw PE */
  }
  free(buf);
  return ok;
}

static BOOL http_get_to_file(const wchar_t *host, const wchar_t *path, const wchar_t *dest,
                             DWORD maxn, const wchar_t *hdr) {
  DeleteFileW(dest);
  HANDLE f = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) {
    upd_fail(L"Не удалось создать временный файл", GetLastError());
    return FALSE;
  }
  HINTERNET ses = WinHttpOpen(L"CursorPad/" APP_VERSION_STR L" (+https://github.com/pidrpen/ytaqq)",
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
    {
      DWORD e = GetLastError();
      const wchar_t *why = wh_reason(e);
      if (why) upd_log(L"  %s → %s (код %lu)", host, why, e);
      else upd_log(L"  %s → не соединиться (код %lu)", host, e);
    }
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
                           L"Accept: */*\r\nAccept-Encoding: identity\r\nCache-Control: no-cache",
                           (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
  if (hdr && hdr[0])
    WinHttpAddRequestHeaders(req, hdr, (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
#ifdef WINHTTP_OPTION_DECOMPRESSION
  {
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));
  }
#endif
  {
    /* a company proxy that wants a logged-in user gets the Windows account
       this program already runs as, without asking anybody for a password */
    DWORD lvl = WINHTTP_AUTOLOGON_SECURITY_LEVEL_LOW;
    WinHttpSetOption(req, WINHTTP_OPTION_AUTOLOGON_POLICY, &lvl, sizeof(lvl));
  }
  DWORD status = 0, slen = sizeof(status);
  BOOL ok;
  for (int attempt = 0;; attempt++) {
    ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(req, NULL);
    if (!ok) break;
    status = 0;
    slen = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status,
                        &slen, WINHTTP_NO_HEADER_INDEX);
    if ((status != 401 && status != 407) || attempt >= 2) break;
    /* say who is asking — that is the difference between "GitHub refused"
       and "something in this network refused" */
    wchar_t who[200];
    DWORD wl = sizeof(who);
    if (WinHttpQueryHeaders(req,
                            status == 407 ? WINHTTP_QUERY_PROXY_AUTHENTICATE
                                          : WINHTTP_QUERY_WWW_AUTHENTICATE,
                            NULL, who, &wl, WINHTTP_NO_HEADER_INDEX))
      upd_log(L"  %s → HTTP %lu, спрашивает вход: %s", host, status, who);
    DWORD supported = 0, firstScheme = 0, target = 0;
    if (!WinHttpQueryAuthSchemes(req, &supported, &firstScheme, &target)) break;
    DWORD pick = 0;
    if (supported & WINHTTP_AUTH_SCHEME_NEGOTIATE) pick = WINHTTP_AUTH_SCHEME_NEGOTIATE;
    else if (supported & WINHTTP_AUTH_SCHEME_NTLM) pick = WINHTTP_AUTH_SCHEME_NTLM;
    else if (supported & WINHTTP_AUTH_SCHEME_DIGEST) pick = WINHTTP_AUTH_SCHEME_DIGEST;
    if (!pick) break; /* Basic needs a password we do not have */
    /* the old answer has to be consumed before the request can be sent again */
    for (;;) {
      DWORD avail = 0, got = 0;
      char sink[4096];
      if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
      if (!WinHttpReadData(req, sink, avail > sizeof(sink) ? (DWORD)sizeof(sink) : avail, &got) ||
          !got)
        break;
    }
    if (!WinHttpSetCredentials(req, target, pick, NULL, NULL, NULL)) break;
    upd_log(L"  %s → вхожу под учётной записью Windows", host);
  }
  if (!ok) {
    {
      DWORD e = GetLastError();
      const wchar_t *why = wh_reason(e);
      if (why) upd_log(L"  %s → %s (код %lu)", host, why, e);
      else upd_log(L"  %s → нет ответа (код %lu)", host, e);
    }
    upd_fail(L"Сеть: хост не ответил", GetLastError());
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    CloseHandle(f);
    DeleteFileW(dest);
    return FALSE;
  }
  if (status == 401 || status == 407) g_updAuthWall = 1;
  if (status != 200) {
    wchar_t srv[120];
    DWORD sl2 = sizeof(srv);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_SERVER, NULL, srv, &sl2, WINHTTP_NO_HEADER_INDEX))
      upd_log(L"  %s → HTTP %lu, отвечает: %s", host, status, srv);
    else
      upd_log(L"  %s → HTTP %lu", host, status);
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

/* One source, fetched and sanity-checked. Split out so the version step can
   ask every mirror instead of trusting whichever answers first. */
static BOOL fetch_from(int i, const wchar_t *kind, const wchar_t *dest, DWORD maxn) {
  const wchar_t *base = (kind[0] == L'e') ? kUpdSrc[i].exe : kUpdSrc[i].ver;
  wchar_t path[420];
  if (kUpdSrc[i].bust)
    _snwprintf(path, 420, L"%s?t=%lu", base, GetTickCount());
  else
    lstrcpynW(path, base, 420);
  if (!http_get_to_file(kUpdSrc[i].host, path, dest, maxn, kUpdSrc[i].hdr)) return FALSE;
  if (kUpdSrc[i].api && !unwrap_github_json(dest, maxn)) {
    DeleteFileW(dest);
    return FALSE;
  }
  char *peek = NULL;
  DWORD pn = 0;
  if (read_file_bytes(dest, &peek, &pn, 4096)) {
    BOOL bad = looks_like_html(peek) || (pn >= 2 && (unsigned char)peek[0] == 0x1F &&
                                         (unsigned char)peek[1] == 0x8B);
    if (!bad && kind[0] != L'e' && pn >= 12 && !strncmp(peek, "version https://git-lfs", 12))
      bad = TRUE;
    /* keep a readable snippet before the buffer goes away: it is the
       difference between "a proxy served a login page" and "it was gzip" */
    wchar_t peekw[70];
    int pk = 0;
    for (DWORD q = 0; q < pn && pk < 60; q++) {
      unsigned char c = (unsigned char)peek[q];
      peekw[pk++] = (c >= 32 && c < 127) ? (wchar_t)c : L'.';
    }
    peekw[pk] = 0;
    free(peek);
    if (bad) {
      DeleteFileW(dest);
      upd_log(L"  %s → пришёл не файл, начало ответа: %s", kUpdSrc[i].host, peekw);
      upd_fail(L"Ответ не файл (HTML/сжатие)", 0);
      return FALSE;
    }
  }
  lstrcpynW(g_updHostUsed, kUpdSrc[i].host, 80);
  return TRUE;
}

static int g_updPrefer = -1; /* mirror that reported the newest version */

static BOOL http_get_any(const wchar_t *kind, const wchar_t *dest, DWORD maxn) {
  int n = (int)(sizeof(kUpdSrc) / sizeof(kUpdSrc[0]));
  for (int k = -1; k < n; k++) {
    /* the preferred mirror goes first, then everyone else in order */
    int i = (k < 0) ? g_updPrefer : k;
    if (i < 0 || i >= n) continue;
    if (k >= 0 && i == g_updPrefer) continue;
    upd_log(L"  пробую %s", kUpdSrc[i].host);
    if (fetch_from(i, kind, dest, maxn)) return TRUE;
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

/* jsDelivr caches @main separately on every edge, so minutes after a release
   one mirror still serves yesterday's file while another is current — and
   version.txt and the binary expire independently, so a mirror can even
   promise a version it cannot hand over. A path pinned to a tag or a commit
   is immutable: each edge fetches it once and can never be stale. This finds
   that pin, and does it without raw.githubusercontent.com. */
static const wchar_t *kUpdCdn[] = {L"cdn.jsdelivr.net", L"fastly.jsdelivr.net",
                                   L"gcore.jsdelivr.net"};

static void pin_from_json(const char *body, const char *key, int minlen) {
  const char *p = json_find_string(body, key);
  if (!p) return;
  char v[64];
  int i = 0;
  while (p[i] && p[i] != '"' && i < 60) {
    v[i] = p[i];
    i++;
  }
  v[i] = 0;
  if (i >= minlen) MultiByteToWideChar(CP_UTF8, 0, v, -1, g_updPin, 96);
}

static void upd_resolve_pin(const wchar_t *tmp) {
  char *body = NULL;
  DWORD n = 0;
  g_updPin[0] = 0;
  /* jsDelivr's own metadata API knows the newest git tag of the repo. */
  if (http_get_to_file(L"data.jsdelivr.com", L"/v1/packages/gh/pidrpen/ytaqq/resolved", tmp,
                       64 * 1024, NULL) &&
      read_file_bytes(tmp, &body, &n, 64 * 1024)) {
    pin_from_json(body, "version", 3);
    free(body);
    body = NULL;
  }
  DeleteFileW(tmp);
  if (g_updPin[0]) {
    upd_log(L"  метка выпуска: %s", g_updPin);
    return;
  }
  /* No tag published yet, or that API is unreachable — pin to the newest
     commit instead. api.github.com is a different host from the blocked raw. */
  if (http_get_to_file(L"api.github.com", L"/repos/pidrpen/ytaqq/commits/main", tmp, 512 * 1024,
                       L"Accept: application/vnd.github+json\r\n"
                       L"X-GitHub-Api-Version: 2022-11-28") &&
      read_file_bytes(tmp, &body, &n, 512 * 1024)) {
    pin_from_json(body, "sha", 7);
    free(body);
  }
  DeleteFileW(tmp);
  if (g_updPin[0])
    upd_log(L"  метка коммита: %s", g_updPin);
  else
    upd_log(L"  метку получить не удалось");
}

static BOOL fetch_pinned(const wchar_t *file, const wchar_t *dest, DWORD maxn) {
  if (!g_updPin[0]) return FALSE;
  wchar_t path[320];
  _snwprintf(path, 320, L"/gh/pidrpen/ytaqq@%s/public/%s", g_updPin, file);
  path[319] = 0;
  for (int i = 0; i < (int)(sizeof(kUpdCdn) / sizeof(kUpdCdn[0])); i++) {
    upd_log(L"  пробую %s по метке", kUpdCdn[i]);
    if (http_get_to_file(kUpdCdn[i], path, dest, maxn, NULL)) {
      lstrcpynW(g_updHostUsed, kUpdCdn[i], 80);
      return TRUE;
    }
  }
  return FALSE;
}

static BOOL file_is_pe(const wchar_t *path) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  unsigned char buf[4096];
  DWORD r = 0;
  ReadFile(f, buf, sizeof(buf), &r, NULL);
  if (r < 64 || buf[0] != 'M' || buf[1] != 'Z') {
    CloseHandle(f);
    return FALSE;
  }
  DWORD peoff = (DWORD)buf[0x3C] | ((DWORD)buf[0x3D] << 8) | ((DWORD)buf[0x3E] << 16) |
                ((DWORD)buf[0x3F] << 24);
  unsigned char pe[26];
  if (peoff + 26 <= r) {
    memcpy(pe, buf + peoff, 26);
  } else {
    SetFilePointer(f, (LONG)peoff, NULL, FILE_BEGIN);
    DWORD n2 = 0;
    if (!ReadFile(f, pe, 26, &n2, NULL) || n2 < 26) {
      CloseHandle(f);
      return FALSE;
    }
  }
  CloseHandle(f);
  if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0) return FALSE;
  WORD machine = (WORD)(pe[4] | (pe[5] << 8));
  WORD magic = (WORD)(pe[24] | (pe[25] << 8));
  return machine == 0x8664 && magic == 0x020B; /* AMD64 PE32+ */
}

static BOOL dir_is_writable(const wchar_t *file) {
  wchar_t dir[MAX_PATH];
  lstrcpynW(dir, file, MAX_PATH);
  wchar_t *slash = wcsrchr(dir, L'\\');
  if (slash) *slash = 0;
  wchar_t probe[MAX_PATH];
  _snwprintf(probe, MAX_PATH, L"%s\\cp-w.tmp", dir);
  HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN, NULL);
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  CloseHandle(h);
  DeleteFileW(probe);
  return TRUE;
}

static void user_exe_path(wchar_t *out, int n) {
  if (g_dataDir[0])
    _snwprintf(out, n, L"%s\\CursorPad.exe", g_dataDir);
  else {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    _snwprintf(out, n, L"%sCursorPad.exe", tmp);
  }
}

static BOOL replace_file_no_admin(const wchar_t *src, const wchar_t *dest) {
  wchar_t bak[MAX_PATH];
  _snwprintf(bak, MAX_PATH, L"%s.old", dest);
  DeleteFileW(bak);
  DWORD attr = GetFileAttributesW(dest);
  if (attr != INVALID_FILE_ATTRIBUTES) {
    if (attr & FILE_ATTRIBUTE_READONLY)
      SetFileAttributesW(dest, attr & ~FILE_ATTRIBUTE_READONLY);
    if (!MoveFileExW(dest, bak, MOVEFILE_REPLACE_EXISTING)) {
      /* still running image: rename almost always works; if not, try copy-over */
      if (!CopyFileW(src, dest, FALSE)) return FALSE;
      DeleteFileW(src);
      return TRUE;
    }
  }
  if (MoveFileExW(src, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return TRUE;
  if (CopyFileW(src, dest, FALSE)) {
    DeleteFileW(src);
    return TRUE;
  }
  MoveFileExW(bak, dest, MOVEFILE_REPLACE_EXISTING);
  return FALSE;
}

static BOOL launch_open(const wchar_t *path) {
  SHELLEXECUTEINFOW sei;
  memset(&sei, 0, sizeof(sei));
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS;
  sei.lpVerb = L"open"; /* never runas */
  sei.lpFile = path;
  sei.nShow = SW_SHOWNORMAL;
  wchar_t dir[MAX_PATH];
  lstrcpynW(dir, path, MAX_PATH);
  wchar_t *sl = wcsrchr(dir, L'\\');
  if (sl) *sl = 0;
  sei.lpDirectory = dir;
  if (ShellExecuteExW(&sei)) return TRUE;
  wchar_t args[MAX_PATH + 48];
  _snwprintf(args, MAX_PATH + 48, L"/C start \"\" \"%s\"", path);
  HINSTANCE r = ShellExecuteW(NULL, L"open", L"cmd.exe", args, dir, SW_HIDE);
  return (INT_PTR)r > 32;
}

static void apply_update(const wchar_t *newexe) {
  wchar_t self[MAX_PATH] = {0};
  GetModuleFileNameW(NULL, self, MAX_PATH);
  wchar_t target[MAX_PATH];
  lstrcpynW(target, self, MAX_PATH);
  BOOL relocated = FALSE;
  if (!dir_is_writable(self) || !replace_file_no_admin(newexe, target)) {
    user_exe_path(target, MAX_PATH);
    relocated = TRUE;
    if (lstrcmpiW(target, self) == 0 || !CopyFileW(newexe, target, FALSE)) {
      upd_fail(L"Нет прав на запись рядом с программой и в профиль", GetLastError());
      show_status(g_updErr);
      return;
    }
    DeleteFileW(newexe);
  }
  if (relocated && g_autostart) autostart_write(TRUE, target);
  lstrcpynW(g_updLaunch, target, MAX_PATH);
  if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
  else {
    launch_open(target);
  }
}

static void finish_update_launch(void) {
  if (!g_updLaunch[0]) return;
  if (g_mutex) {
    CloseHandle(g_mutex);
    g_mutex = NULL;
  }
  launch_open(g_updLaunch);
  g_updLaunch[0] = 0;
}

static void cleanup_old_bins(void) {
  wchar_t self[MAX_PATH], bak[MAX_PATH];
  GetModuleFileNameW(NULL, self, MAX_PATH);
  _snwprintf(bak, MAX_PATH, L"%s.old", self);
  DeleteFileW(bak);
  if (g_dataDir[0]) {
    _snwprintf(bak, MAX_PATH, L"%s\\CursorPad.exe.old", g_dataDir);
    DeleteFileW(bak);
    _snwprintf(bak, MAX_PATH, L"%s\\CursorPad-next.bin", g_dataDir);
    if (lstrcmpiW(bak, self) != 0) DeleteFileW(bak);
  }
}

static void clear_runas_layer(void) {
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(NULL, self, MAX_PATH);
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", 0,
                    KEY_SET_VALUE, &k) != ERROR_SUCCESS)
    return;
  RegDeleteValueW(k, self);
  RegCloseKey(k);
}

static DWORD WINAPI update_thread(LPVOID param) {
  (void)param;
  g_updRemote[0] = 0;
  g_updPath[0] = 0;
  g_updErr[0] = 0;
  g_updHostUsed[0] = 0;
  wchar_t tmpv[MAX_PATH], tmpe[MAX_PATH];
  if (g_dataDir[0]) {
    _snwprintf(tmpv, MAX_PATH, L"%s\\version-check.txt", g_dataDir);
    _snwprintf(tmpe, MAX_PATH, L"%s\\CursorPad-next.bin", g_dataDir);
  } else {
    wchar_t tdir[MAX_PATH];
    GetTempPathW(MAX_PATH, tdir);
    _snwprintf(tmpv, MAX_PATH, L"%sCursorPad-version.txt", tdir);
    _snwprintf(tmpe, MAX_PATH, L"%sCursorPad-next.bin", tdir);
  }
  g_updLog[0] = 0;
  g_updLogLen = 0;
  g_updPrefer = -1;
  g_updPin[0] = 0;
  g_updPinned = FALSE;
  g_updAuthWall = 0;
  SYSTEMTIME st;
  GetLocalTime(&st);
  upd_log(L"Обновление CursorPad — %02u.%02u.%04u %02u:%02u", (unsigned)st.wDay,
          (unsigned)st.wMonth, (unsigned)st.wYear, (unsigned)st.wHour, (unsigned)st.wMinute);
  upd_log(L"Установленная версия: %s", APP_VERSION_STR);
  upd_log(L"");
  upd_log(L"Шаг 1 — ищу неизменяемую метку выпуска:");
  upd_resolve_pin(tmpv);

  int code = 0;
  /* Mirrors of the same branch fall out of sync: one edge can still serve a
     version from hours ago. Trusting whichever answers first then reports
     "уже последняя". Ask everybody and believe the newest. */
  char buf[128] = {0};
  long remote = 0;
  int answered = 0;
  if (g_updPin[0]) {
    upd_log(L"");
    upd_log(L"Шаг 2 — читаю version.txt по метке:");
    if (fetch_pinned(L"version.txt", tmpv, 256 * 1024)) {
      char one[128] = {0};
      DWORD got = 0;
      HANDLE fh = CreateFileW(tmpv, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
      if (fh != INVALID_HANDLE_VALUE) {
        ReadFile(fh, one, 120, &got, NULL);
        CloseHandle(fh);
      }
      long v = parse_ver_file(one);
      if (v > 0) {
        remote = v;
        answered = 1;
        g_updPinned = TRUE;
        memcpy(buf, one, sizeof(buf));
        upd_log(L"  %s → %ld (кэш тут ни при чём)", g_updHostUsed, v);
      }
    }
    DeleteFileW(tmpv);
    if (!g_updPinned) upd_log(L"  по метке не вышло — спрашиваю зеркала по очереди");
  }
  upd_log(L"");
  if (!g_updPinned) upd_log(L"Шаг 2 — читаю version.txt (спрашиваю все зеркала):");
  for (int i = 0; !g_updPinned && i < (int)(sizeof(kUpdSrc) / sizeof(kUpdSrc[0])); i++) {
    char one[128] = {0};
    DWORD got = 0;
    upd_log(L"  пробую %s", kUpdSrc[i].host);
    if (!fetch_from(i, L"v", tmpv, 256 * 1024)) continue;
    HANDLE fh = CreateFileW(tmpv, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh != INVALID_HANDLE_VALUE) {
      ReadFile(fh, one, 120, &got, NULL);
      CloseHandle(fh);
    }
    DeleteFileW(tmpv);
    long v = parse_ver_file(one);
    if (v <= 0) {
      upd_log(L"  %s → version.txt не разобрался", kUpdSrc[i].host);
      continue;
    }
    answered++;
    upd_log(L"  %s → %ld", kUpdSrc[i].host, v);
    if (v > remote) {
      remote = v;
      g_updPrefer = i;
      memcpy(buf, one, sizeof(buf));
    }
  }
  if (!answered) {
    upd_log(L"Итог: ни один источник не отдал version.txt.");
    if (g_updAuthWall) {
      upd_log(L"");
      upd_log(L"Все адреса сразу потребовали вход — так отвечает не GitHub,");
      upd_log(L"а что-то в вашей сети: прокси или фильтр посередине.");
      upd_log(L"Программа уже пробует войти под вашей учётной записью Windows.");
      upd_log(L"Если не помогает, сети нужно пропустить cdn.jsdelivr.net.");
    }
    if (!g_updErr[0]) upd_fail(L"GitHub/CDN не отдали version.txt", 0);
    code = 0;
    goto done;
  }
  if (g_updPrefer >= 0)
    upd_log(L"  самое свежее у %s", kUpdSrc[g_updPrefer].host);
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
  upd_log(L"  получено: «%s» (число %ld)", g_updRemote[0] ? g_updRemote : L"—", remote);
  if (remote <= 0) {
    upd_log(L"Итог: version.txt не разобрался.");
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
    upd_log(L"Итог: на GitHub не новее установленной — обновлять нечего.");
    code = 1;
    goto done;
  }
  upd_log(L"");
  upd_log(L"Шаг 3 — качаю программу:");
  BOOL got_exe = FALSE;
  if (g_updPinned) got_exe = fetch_pinned(L"CursorPad.bin", tmpe, 16 * 1024 * 1024) &&
                             file_is_pe(tmpe);
  if (!got_exe) got_exe = http_get_any(L"e", tmpe, 16 * 1024 * 1024) && file_is_pe(tmpe);
  if (!got_exe) {
    upd_log(L"Итог: файл не скачался или это не программа для Windows.");
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
    /* naming both versions saves the "it keeps saying latest" puzzlement */
    wchar_t m[160];
    _snwprintf(m, 160, L"Уже последняя: у вас %s, на GitHub %s", APP_VERSION_STR,
               g_updRemote[0] ? g_updRemote : L"—");
    show_status(m);
    return;
  }
  if (code != 2 || !g_updPath[0]) {
    /* a line in the status bar disappears in two seconds and cannot be
       copied; the full trace goes where it can be read and sent on */
    upd_log(L"");
    upd_log(L"Причина: %s", g_updErr[0] ? g_updErr : L"GitHub недоступен");
    g_resultFiles = FALSE;
    g_plmCount = 0;
    g_ansTitle = L"Отчёт об обновлении";
    show_answer_text(g_updLog);
    show_status(g_updErr[0] ? g_updErr : L"GitHub недоступен");
    return;
  }
  wchar_t msg[360];
  _snwprintf(msg, 360,
             L"На GitHub версия %s (сейчас %s).\r\n"
             L"Скачать, заменить файл и перезапустить?\r\n"
             L"Права администратора не нужны.",
             g_updRemote, APP_VERSION_STR);
  int r = MessageBoxW(g_hwnd, msg, L"CursorPad — обновление", MB_YESNO | MB_ICONQUESTION);
  if (r == IDYES) apply_update(g_updPath);
  else DeleteFileW(g_updPath);
}
