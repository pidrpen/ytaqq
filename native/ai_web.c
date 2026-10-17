/* Поиск страниц для чата Qwen. Старый DuckDuckGo-путь мини-ИИ не используется. */
#include <stdio.h>
#include <windows.h>
#include <winhttp.h>
#include <wctype.h>
#include <windns.h>

typedef struct {
  wchar_t note[360];
  DWORD status, error;
  DWORD deadline;
  volatile LONG *generation;
  LONG expected;
  BOOL page;
} AcWebInfo;

static BOOL acw_stopped(const AcWebInfo *info) {
  return (info->generation && InterlockedCompareExchange(info->generation, 0, 0) != info->expected) ||
         (info->deadline && (LONG)(GetTickCount() - info->deadline) >= 0);
}

static void url_encode_utf8(const wchar_t *src, char *out, int cap);

static int acw_hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void acw_unescape(const char *in, char *out, int cap) {
  int o = 0;
  for (int i = 0; in[i] && o + 1 < cap; i++) {
    if (in[i] != '&') {
      out[o++] = in[i];
      continue;
    }
    if (!strncmp(in + i, "&amp;", 5)) {
      out[o++] = '&';
      i += 4;
      continue;
    }
    if (!strncmp(in + i, "&quot;", 6)) {
      out[o++] = '"';
      i += 5;
      continue;
    }
    if (!strncmp(in + i, "&apos;", 6)) { out[o++] = '\''; i += 5; continue; }
    if (!strncmp(in + i, "&lt;", 4)) {
      out[o++] = '<';
      i += 3;
      continue;
    }
    if (!strncmp(in + i, "&gt;", 4)) {
      out[o++] = '>';
      i += 3;
      continue;
    }
    if (!strncmp(in + i, "&nbsp;", 6)) {
      out[o++] = ' ';
      i += 5;
      continue;
    }
    if (in[i + 1] == '#') {
      int v = 0, k = i + 2, hex = 0;
      if (in[k] == 'x' || in[k] == 'X') {
        hex = 1;
        k++;
      }
      int any = 0;
      while (in[k] && in[k] != ';') {
        int d = hex ? acw_hex(in[k]) : (in[k] >= '0' && in[k] <= '9' ? in[k] - '0' : -1);
        if (d < 0 || v > 127) break; /* only ASCII entities are decoded here; bound arithmetic */
        v = hex ? (v << 4) | d : v * 10 + d;
        any = 1;
        k++;
      }
      if (any && in[k] == ';' && v > 0 && v < 128 && o + 1 < cap) {
        out[o++] = (char)v;
        i = k;
        continue;
      }
    }
    out[o++] = in[i];
  }
  out[o] = 0;
}

static void acw_text(const char *in, const char *end, char *out, int cap) {
  int o = 0, skip = 0;
  for (const char *p = in; p < end && o + 1 < cap; p++) {
    if (*p == '<') {
      skip = 1;
      continue;
    }
    if (*p == '>') {
      skip = 0;
      if (o && out[o - 1] != ' ') out[o++] = ' ';
      continue;
    }
    if (skip) continue;
    char c = *p;
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (c == ' ' && o && out[o - 1] == ' ') continue;
    out[o++] = c;
  }
  while (o && out[o - 1] == ' ') o--;
  out[o] = 0;
}

static BOOL acw_get(const wchar_t *host, const wchar_t *path, char **out, DWORD *outlen, AcWebInfo *info) {
  *out = NULL;
  *outlen = 0;
  info->status = info->error = 0;
  if (acw_stopped(info)) { info->error = ERROR_CANCELLED; return FALSE; }
  HINTERNET ses = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                              L"(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) { info->error = GetLastError(); return FALSE; }
  WinHttpSetTimeouts(ses, 2000, 2000, 3000, 3000);
  HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!con) {
    info->error = GetLastError();
    WinHttpCloseHandle(ses);
    return FALSE;
  }
  HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE);
  if (!req) {
    info->error = GetLastError();
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return FALSE;
  }
  { DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)); }
#ifdef WINHTTP_OPTION_DECOMPRESSION
  {
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));
  }
#endif
  WinHttpAddRequestHeaders(req,
                           L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                           L"Accept-Language: ru-RU,ru;q=0.9,en;q=0.8\r\n"
                           L"Upgrade-Insecure-Requests: 1\r\n"
                           L"Sec-Fetch-Dest: document\r\n"
                           L"Sec-Fetch-Mode: navigate\r\n"
                           L"Sec-Fetch-Site: none\r\n"
                           L"Sec-Fetch-User: ?1\r\n",
                           (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
  BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (ok) ok = WinHttpReceiveResponse(req, NULL);
  if (ok) {
    DWORD status = 0, sl = sizeof(status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sl,
                             WINHTTP_NO_HEADER_INDEX)) ok = FALSE;
    info->status = status;
    if (status != 200) ok = FALSE;
    if (ok && info->page) {
      wchar_t type[160] = L""; DWORD size = sizeof(type);
      if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_TYPE, NULL, type, &size, NULL) ||
          (!wcsstr(type, L"text/html") && !wcsstr(type, L"text/plain") && !wcsstr(type, L"application/xhtml+xml"))) {
        info->error = ERROR_NOT_SUPPORTED; ok = FALSE;
      }
    }
  }
  if (!ok) {
    if (!info->error) info->error = info->status ? 0 : GetLastError();
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return FALSE;
  }
  DWORD cap = 0, n = 0;
  char *buf = NULL;
  for (;;) {
    if (acw_stopped(info)) { info->error = ERROR_CANCELLED; ok = FALSE; break; }
    DWORD avail = 0, got = 0;
    if (!WinHttpQueryDataAvailable(req, &avail)) { info->error = GetLastError(); ok = FALSE; break; }
    if (!avail) break;
    if (n + avail + 1 > cap) {
      DWORD nc = n + avail + 4096;
      if (nc > 500000) nc = 500000;
      char *nb = (char *)realloc(buf, nc);
      if (!nb) { info->error = ERROR_NOT_ENOUGH_MEMORY; ok = FALSE; break; }
      buf = nb;
      cap = nc;
    }
    DWORD room = cap - n - 1;
    if (avail > room) avail = room;
    if (!avail) break;
    if (!WinHttpReadData(req, buf + n, avail, &got)) { info->error = GetLastError(); ok = FALSE; break; }
    if (!got) break;
    n += got;
    if (n >= 480000) break;
  }
  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  if (!ok || !buf || !n) {
    free(buf);
    return FALSE;
  }
  buf[n] = 0;
  *out = buf;
  *outlen = n;
  return TRUE;
}

static BOOL acw_has(const char *s, int n, const char *needle) {
  int m = (int)strlen(needle);
  if (m <= 0 || m > n) return FALSE;
  for (int i = 0; i + m <= n; i++)
    if (!memcmp(s + i, needle, (size_t)m)) return TRUE;
  return FALSE;
}

static BOOL acw_skip_url(const char *url, int n) {
  if (n < 12 || strncmp(url, "https://", 8) != 0) return TRUE;
  if (acw_has(url, n, "brave.com") || acw_has(url, n, "bing.com") || acw_has(url, n, "google.com/search"))
    return TRUE;
  return FALSE;
}

static void acw_snip(const char *html, const char *end, char *out, int cap) {
  out[0] = 0;
  const char *g = strstr(html, "generic-snippet");
  if (!g || g >= end) return;
  const char *c = strstr(g, "class=\"content");
  if (!c || c >= end) return;
  const char *gt = strchr(c, '>');
  if (!gt || gt >= end) return;
  const char *stop = strstr(gt, "</div>");
  if (!stop || stop > end) stop = end;
  char raw[900];
  acw_text(gt + 1, stop, raw, 900);
  acw_unescape(raw, out, cap);
}

static void acw_piece(const char *html, const char *end, const char *mark, char *out, int cap) {
  out[0] = 0;
  const char *s = strstr(html, mark);
  if (!s || s >= end) return;
  const char *gt = strchr(s, '>');
  if (!gt || gt >= end) return;
  const char *stop = strstr(gt, "</");
  if (!stop || stop > end) stop = end;
  char raw[900];
  acw_text(gt + 1, stop, raw, 900);
  acw_unescape(raw, out, cap);
}

/* До 4 выдержек из уже скачанной страницы выдачи. */
static BOOL ac_web_parse(const char *html, DWORD n, wchar_t *out, int cap) {
  out[0] = 0;
  if (!html || !n || cap < 200) return FALSE;
  const char *htmlEnd = html + n;
  const char *p = html;
  int hits = 0, used = 0;
  wchar_t wide[3600];
  wide[0] = 0;
  while (hits < 4 && (p = strstr(p, "data-type=\"web\""))) {
    const char *end = strstr(p + 16, "data-type=\"web\"");
    if (!end || end > htmlEnd) end = htmlEnd;
    if (end > p + 20000) end = p + 20000;
    const char *h = p;
    char url[480];
    url[0] = 0;
    while ((h = strstr(h, "href=\"")) && h < end) {
      h += 6;
      const char *q = strchr(h, '"');
      if (!q || q > end) break;
      int ln = (int)(q - h);
      if (ln > 0 && ln < 470 && !acw_skip_url(h, ln)) {
        memcpy(url, h, (size_t)ln);
        url[ln] = 0;
        break;
      }
      h = q + 1;
    }
    char title[300], snip[700];
    acw_piece(p, end, "search-snippet-title", title, 300);
    acw_snip(p, end, snip, 700);
    if (url[0] && (title[0] || snip[0])) {
      char block[1400];
      snprintf(block, sizeof(block), "%s%s%s\n%s\n\n", title[0] ? title : snip, title[0] ? "\n" : "",
               title[0] ? snip : "", url);
      wchar_t wblock[1400];
      MultiByteToWideChar(CP_UTF8, 0, block, -1, wblock, 1400);
      int bl = (int)wcslen(wblock);
      if (used + bl < 3400 && used + bl < cap - 1) {
        memcpy(wide + used, wblock, (size_t)(bl + 1) * sizeof(wchar_t));
        used += bl;
        hits++;
      }
    }
    p = end;
  }
  if (!hits) return FALSE;
  lstrcpynW(out, wide, cap);
  return out[0] != 0;
}

/* The RSS endpoint sometimes returns unrelated pages. Require a meaningful
   query token in a result; this is only a filter, not a proof of relevance. */
static BOOL acw_word(wchar_t c) {
  WORD kind = 0;
  return GetStringTypeW(CT_CTYPE1, &c, 1, &kind) && (kind & (C1_ALPHA | C1_DIGIT));
}

static int acw_score(const wchar_t *query, const wchar_t *text) {
  wchar_t q[600], t[2400]; lstrcpynW(q, query, 600); lstrcpynW(t, text, 2400);
  CharLowerBuffW(q, (DWORD)wcslen(q)); CharLowerBuffW(t, (DWORD)wcslen(t));
  const wchar_t *stop = L"|какие|какой|какая|какое|сколько|когда|почему|найди|найти|интернет|интернете|поищи|поиск|расскажи|пожалуйста|сегодня|сейчас|можешь|нужно|what|where|when|which|please|search|";
  int score = 0;
  for (wchar_t *p = q; *p;) {
    while (*p && !acw_word(*p)) p++;
    wchar_t token[80]; int n = 0;
    while (*p && acw_word(*p)) { if (n < 79) token[n++] = *p; p++; }
    token[n] = 0;
    if (n < 4) continue;
    wchar_t word[84]; _snwprintf(word, 84, L"|%s|", token);
    if (wcsstr(stop, word)) continue;
    if (n >= 5) token[n > 5 ? 5 : 4] = 0; /* e.g. сталь / стали */
    if (wcsstr(t, token)) score++;
  }
  return score;
}

static BOOL acw_append(const char *title, const char *snip, const char *url,
                       const wchar_t *query, wchar_t *out, int cap) {
  if (acw_skip_url(url, (int)strlen(url))) return FALSE;
  char block[2400]; wchar_t wide[2400] = L"";
  snprintf(block, sizeof(block), "%s\n%s\n%s\n\n", title, snip, url);
  if (!MultiByteToWideChar(CP_UTF8, 0, block, -1, wide, 2400)) return FALSE;
  if (query && !acw_score(query, wide)) return FALSE;
  if (wcslen(out) + wcslen(wide) >= (size_t)cap) return FALSE;
  wcscat(out, wide); return TRUE;
}

static BOOL acw_xml_field(const char *start, const char *end, const char *tag, char *out, int cap) {
  char open[40], close[40]; snprintf(open, 40, "<%s>", tag); snprintf(close, 40, "</%s>", tag);
  const char *p = strstr(start, open); if (!p || p >= end) return FALSE;
  p += strlen(open); const char *e = strstr(p, close); if (!e || e > end || e - p >= 1800) return FALSE;
  char raw[1800]; memcpy(raw, p, (size_t)(e - p)); raw[e - p] = 0;
  acw_unescape(raw, out, cap); return TRUE;
}

static BOOL acw_rss(const char *html, const wchar_t *query, wchar_t *out, int cap) {
  out[0] = 0; const char *p = html; int hits = 0;
  while (hits < 3 && (p = strstr(p, "<item>"))) {
    const char *end = strstr(p, "</item>"); if (!end) break;
    char title[600] = "", snip[1100] = "", url[600] = "";
    if (acw_xml_field(p, end, "title", title, 600) && acw_xml_field(p, end, "link", url, 600)) {
      acw_xml_field(p, end, "description", snip, 1100);
      if (acw_append(title, snip, url, query, out, cap)) hits++;
    }
    p = end + 7;
  }
  return hits != 0;
}

static BOOL acw_attr(const char *start, const char *end, const char *name, char *out, int cap) {
  char key[40]; snprintf(key, 40, " %s=\"", name);
  const char *p = strstr(start, key); if (!p || p >= end) return FALSE;
  p += strlen(key); const char *e = strchr(p, '"'); if (!e || e > end || e - p >= 1800) return FALSE;
  char raw[1800]; memcpy(raw, p, (size_t)(e - p)); raw[e - p] = 0;
  acw_unescape(raw, out, cap); return TRUE;
}

/* Explicitly labelled encyclopedia fallback, not a general web search. */
static BOOL acw_wiki(const char *xml, wchar_t *out, int cap) {
  out[0] = 0; const char *p = xml; int hits = 0;
  while (hits < 3 && (p = strstr(p, "<p "))) {
    const char *end = strstr(p, "/>"); if (!end) break;
    char title[600] = "", snip[1600] = "", clean[1200], id[40] = "", url[150];
    if (acw_attr(p, end, "title", title, 600) && acw_attr(p, end, "snippet", snip, 1600) &&
        acw_attr(p, end, "pageid", id, 40) && id[0] && strspn(id, "0123456789") == strlen(id)) {
      acw_text(snip, snip + strlen(snip), clean, 1200);
      snprintf(url, sizeof(url), "https://ru.wikipedia.org/?curid=%s", id);
      if (acw_append(title, clean, url, NULL, out, cap)) hits++;
    }
    p = end + 2;
  }
  return hits != 0;
}

/* Actual search excerpts; HTTP failures are preserved for the chat and log. */
static BOOL ac_web_find(const wchar_t *query, wchar_t *out, int cap, AcWebInfo *info) {
  out[0] = 0;
  info->note[0] = 0; info->status = info->error = 0; info->page = FALSE;
  if (!info->deadline) info->deadline = GetTickCount() + 25000;
  if (!query || !query[0] || cap < 200) return FALSE;
  char enc[1800];
  url_encode_utf8(query, enc, (int)sizeof(enc));
  if (!enc[0]) return FALSE;
  wchar_t wenc[1800], path[2100];
  MultiByteToWideChar(CP_UTF8, 0, enc, -1, wenc, 1800);
  _snwprintf(path, 2100, L"/search?q=%s&source=web", wenc);
  char *html = NULL;
  DWORD n = 0;
  BOOL got = acw_get(L"search.brave.com", path, &html, &n, info);
  BOOL ok = got && ac_web_parse(html, n, out, cap); free(html); html = NULL;
  if (ok) { lstrcpynW(info->note, L"Поиск: Brave; выдержки поисковой выдачи.", 360); return TRUE; }
  wchar_t reason[80];
  if (info->status && info->status != 200) _snwprintf(reason, 80, L"Brave: HTTP %lu", info->status);
  else if (info->error) _snwprintf(reason, 80, L"Brave: ошибка Windows %lu", info->error);
  else lstrcpynW(reason, L"Brave: нет читаемой выдачи", 80);
  _snwprintf(path, 2100, L"/search?q=%s&format=rss", wenc);
  got = acw_get(L"www.bing.com", path, &html, &n, info);
  ok = got && acw_rss(html, query, out, cap); free(html); html = NULL;
  if (ok) { _snwprintf(info->note, 360, L"%s. Резервный поиск: Bing RSS; соответствие вопросу нужно проверить.", reason); return TRUE; }
  _snwprintf(path, 2100, L"/w/api.php?action=query&list=search&srlimit=3&format=xml&srsearch=%s", wenc);
  got = acw_get(L"ru.wikipedia.org", path, &html, &n, info);
  ok = got && acw_wiki(html, out, cap); free(html);
  _snwprintf(info->note, 360, ok ? L"%s. Резерв: поиск по Википедии; общий поиск по сайтам недоступен."
                               : L"%s. Резервные источники не дали данных; ответ без интернета.", reason);
  info->note[359] = 0;
  return ok;
}

static const char *acw_ci(const char *s, const char *end, const char *word) {
  size_t n = strlen(word);
  for (; s + n <= end; s++) if (!_strnicmp(s, word, n)) return s;
  return NULL;
}

/* Remove executable/boilerplate HTML before putting page text in context. */
static void acw_plain(const char *html, char *out, int cap) {
  const char *end = html + strlen(html), *p = acw_ci(html, end, "<main");
  if (!p) p = acw_ci(html, end, "<article");
  if (!p) p = html;
  int n = 0;
  while (p < end && n + 1 < cap) {
    if (*p == '<') {
      if (p + 4 <= end && !memcmp(p, "<!--", 4)) {
        const char *e = strstr(p + 4, "-->"); p = e ? e + 3 : end; continue;
      }
      const char *e = strchr(p, '>'); if (!e) break;
      char tag[32]; int k = 0; const char *t = p + 1;
      if (*t == '/') t++;
      while (t < e && ((*t >= 'a' && *t <= 'z') || (*t >= 'A' && *t <= 'Z')) && k < 31) tag[k++] = *t++;
      tag[k] = 0;
      const char *skip[] = {"script", "style", "head", "nav", "footer", "noscript", "svg", "form"};
      BOOL skipped = FALSE;
      if (p[1] != '/') for (int i = 0; i < 8; i++) if (!_stricmp(tag, skip[i])) {
        char close[40]; snprintf(close, 40, "</%s", tag);
        const char *stop = acw_ci(e + 1, end, close);
        const char *gt = stop ? strchr(stop, '>') : NULL;
        p = gt ? gt + 1 : end; skipped = TRUE; break;
      }
      if (skipped) continue;
      if (n && out[n - 1] != '\n') out[n++] = '\n';
      p = e + 1; continue;
    }
    char c = *p++;
    if (c == '\r' || c == '\t') c = ' ';
    if ((c == ' ' || c == '\n') && n && (out[n - 1] == c || (c == ' ' && out[n - 1] == '\n'))) continue;
    out[n++] = c;
  }
  out[n] = 0;
}

static BOOL acw_public_v4(const BYTE *b) {
  return b[0] && b[0] != 10 && b[0] != 127 && b[0] < 224 &&
    !(b[0] == 100 && b[1] >= 64 && b[1] <= 127) && !(b[0] == 169 && b[1] == 254) &&
    !(b[0] == 172 && b[1] >= 16 && b[1] <= 31) && !(b[0] == 192 && (b[1] == 168 || b[1] == 0)) &&
    !(b[0] == 198 && (b[1] == 18 || b[1] == 19 || b[1] == 51)) && !(b[0] == 203 && b[1] == 0 && b[2] == 113);
}

static BOOL acw_public_host(const wchar_t *host) {
  if (!wcschr(host, L'.') || wcsstr(host, L":") || wcsstr(host, L"localhost")) return FALSE;
  const wchar_t *dot = wcsrchr(host, L'.');
  if (!dot[1] || !iswalpha(dot[1])) return FALSE; /* IP literals and trailing dots */
  HMODULE dll = LoadLibraryW(L"dnsapi.dll"); if (!dll) return FALSE;
  typedef DNS_STATUS (WINAPI *Query)(PCWSTR, WORD, DWORD, PVOID, PDNS_RECORD *, PVOID *);
  typedef void (WINAPI *FreeRecords)(PDNS_RECORD, DNS_FREE_TYPE);
  Query query = (Query)GetProcAddress(dll, "DnsQuery_W");
  FreeRecords release = (FreeRecords)GetProcAddress(dll, "DnsRecordListFree");
  BOOL any = FALSE, valid = query && release;
  const WORD types[] = {DNS_TYPE_A, DNS_TYPE_AAAA};
  for (int i = 0; valid && i < 2; i++) {
    PDNS_RECORD records = NULL;
    if (!query(host, types[i], DNS_QUERY_STANDARD, NULL, &records, NULL)) {
      for (PDNS_RECORD r = records; r; r = r->pNext) {
        if (r->wType == DNS_TYPE_A) {
          any = TRUE; if (!acw_public_v4((const BYTE *)&r->Data.A.IpAddress)) valid = FALSE;
        } else if (r->wType == DNS_TYPE_AAAA) {
          const BYTE *b = (const BYTE *)&r->Data.AAAA.Ip6Address;
          any = TRUE; if ((b[0] & 0xe0) != 0x20) valid = FALSE; /* public IPv6 2000::/3 only */
        }
      }
    }
    if (records) release(records, DnsFreeRecordList);
  }
  FreeLibrary(dll); return valid && any;
}

static BOOL acw_read_page(const wchar_t *url, char **text, AcWebInfo *info) {
  *text = NULL;
  if (acw_stopped(info)) return FALSE;
  wchar_t host[260] = L"", path[2100] = L"", extra[1200] = L"", user[80] = L"", password[80] = L"";
  URL_COMPONENTS uc = {0}; uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host; uc.dwHostNameLength = 260; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2100;
  uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1200;
  uc.lpszUserName = user; uc.dwUserNameLength = 80; uc.lpszPassword = password; uc.dwPasswordLength = 80;
  if (!WinHttpCrackUrl(url, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS ||
      uc.nPort != 443 || uc.dwUserNameLength || uc.dwPasswordLength || !acw_public_host(host)) return FALSE;
  char *html = NULL; DWORD n = 0; BOOL wiki = FALSE;
  const wchar_t *id = NULL;
  if (!_wcsicmp(host, L"ru.wikipedia.org") && !wcscmp(path, L"/") && !wcsncmp(extra, L"?curid=", 7)) {
    id = extra + 7; wiki = id[0] && wcsspn(id, L"0123456789") == wcslen(id) && wcslen(id) < 20;
  }
  if (wiki) _snwprintf(path, 2100, L"/w/api.php?action=query&prop=extracts&explaintext=1&exsectionformat=plain&pageids=%s&format=xml", id);
  else {
    if (wcslen(path) + wcslen(extra) >= 2100) return FALSE;
    wcscat(path, extra);
  }
  info->page = !wiki;
  if (!acw_get(host, path, &html, &n, info)) return FALSE;
  char *clean = (char *)malloc(180000), *decoded = (char *)malloc(180000);
  BOOL ok = FALSE;
  if (clean && decoded) {
    if (wiki) {
      char *s = strstr(html, "<extract"), *e = s ? strstr(s, "</extract>") : NULL;
      s = s ? strchr(s, '>') : NULL;
      if (s && e && e > s && e - s < 180000) { *e = 0; acw_unescape(s + 1, clean, 180000); ok = TRUE; }
    } else {
      acw_plain(html, decoded, 180000); acw_unescape(decoded, clean, 180000); ok = TRUE;
    }
    if (ok && (strlen(clean) < 100 || acw_ci(clean, clean + strlen(clean), "verify you are human") ||
               acw_ci(clean, clean + strlen(clean), "enable javascript"))) ok = FALSE;
  }
  free(html); free(decoded);
  if (ok) *text = clean; else free(clean);
  return ok;
}

/* Read at most two actual pages, then choose bounded passages for the model.
   Failed downloads remain labelled snippets. No redirects or private hosts. */
static void ac_web_read_pages(const wchar_t *query, wchar_t *web, int cap, AcWebInfo *info) {
  wchar_t *joined = (wchar_t *)calloc((size_t)cap, sizeof(wchar_t)); if (!joined) return;
  const wchar_t *p = web, *block = web; int read = 0, tried = 0, used = 0;
  wchar_t first[600] = L"";
  while (tried < 2 && !acw_stopped(info) && (p = wcsstr(p, L"https://"))) {
    const wchar_t *end = p; while (*end && !iswspace(*end)) end++;
    int size = (int)(end - p); if (size < 12 || size >= 590) { p = end; continue; }
    wchar_t url[600]; memcpy(url, p, (size_t)size * sizeof(wchar_t)); url[size] = 0;
    if (!wcscmp(url, first)) { p = end; continue; }
    if (!tried) lstrcpynW(first, url, 600);
    wchar_t passage[1451] = L""; char *text = NULL; BOOL actual = acw_read_page(url, &text, info);
    tried++;
    if (actual) {
      wchar_t *full = (wchar_t *)malloc(180000 * sizeof(wchar_t)); int best = -1;
      if (full && MultiByteToWideChar(CP_UTF8, 0, text, -1, full, 180000)) {
        size_t len = wcslen(full);
        for (size_t offset = 0; offset < len; offset += 900) {
          wchar_t window[1451]; lstrcpynW(window, full + offset, 1451);
          int score = acw_score(query, window);
          if (score > best) { best = score; lstrcpynW(passage, window, 1451); }
        }
      }
      free(full); actual = passage[0] != 0;
    }
    free(text);
    if (!actual) { int n = (int)(p - block); if (n > 700) n = 700;
      if (n > 0) { memcpy(passage, block, (size_t)n * sizeof(wchar_t)); passage[n] = 0; } }
    if (actual) read++;
    int n = _snwprintf(joined + used, cap - used, L"%s\r\n%s\r\n%s\r\n\r\n", url,
       actual ? L"Прочитан текст страницы (выбранный фрагмент):" : L"Страницу прочитать не удалось. Только поисковая выдержка:", passage);
    if (n < 0 || n >= cap - used) { joined[cap - 1] = 0; break; }
    used += n; block = end; p = end;
  }
  if (read) lstrcpynW(web, joined, cap);
  free(joined);
  size_t n = wcslen(info->note);
  _snwprintf(info->note + n, 360 - n, L" Прочитано страниц: %d/%d; модели переданы ограниченные фрагменты.", read, tried);
  info->note[359] = 0;
}

#ifdef AC_WEB_TEST
static void url_encode_utf8(const wchar_t *src, char *out, int cap) {
  char utf8[2400];
  int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, utf8, (int)sizeof(utf8) - 1, NULL, NULL);
  if (n <= 0) {
    out[0] = 0;
    return;
  }
  int o = 0;
  for (int i = 0; utf8[i] && o + 4 < cap; i++) {
    unsigned char c = (unsigned char)utf8[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~')
      out[o++] = (char)c;
    else if (c == ' ')
      out[o++] = '+';
    else {
      static const char *hex = "0123456789ABCDEF";
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 15];
    }
  }
  out[o] = 0;
}

int wmain(int argc, wchar_t **argv) {
  wchar_t out[4000];
  AcWebInfo info = {0};
  if (argc > 2 && !wcscmp(argv[1], L"--page")) {
    char *text = NULL; info.deadline = GetTickCount() + 15000;
    BOOL ok = acw_read_page(argv[2], &text, &info);
    printf("ok %d HTTP %lu error %lu\n%s\n", ok ? 1 : 0, info.status, info.error, text ? text : "");
    free(text); return ok ? 0 : 1;
  }
  if ((argc > 1 && argv[1][0] == L'@') || (argc > 2 && (!wcscmp(argv[1], L"--rss") || !wcscmp(argv[1], L"--wiki") || !wcscmp(argv[1], L"--plain")))) {
    FILE *f = _wfopen(argc > 2 ? argv[2] : argv[1] + 1, L"rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *html = sz > 0 ? (char *)malloc((size_t)sz + 1) : NULL;
    if (!html) return 2;
    fread(html, 1, (size_t)sz, f);
    html[sz] = 0;
    fclose(f);
    if (argc > 2 && !wcscmp(argv[1], L"--plain")) {
      char plain[8000]; acw_plain(html, plain, 8000); free(html); printf("%s\n", plain); return 0;
    }
    BOOL ok = argc > 2 && !wcscmp(argv[1], L"--rss") ? acw_rss(html, argc > 3 ? argv[3] : L"Python", out, 4000)
              : argc > 2 && !wcscmp(argv[1], L"--wiki") ? acw_wiki(html, out, 4000)
              : ac_web_parse(html, (DWORD)sz, out, 4000);
    free(html);
    char utf8[16000]; WideCharToMultiByte(CP_UTF8, 0, out, -1, utf8, 16000, NULL, NULL);
    printf("ok %d\n%s\n", ok ? 1 : 0, utf8);
    return ok ? 0 : 1;
  }
  const wchar_t *q = argc > 1 ? argv[1] : L"сталь 45 закалка";
  BOOL ok = ac_web_find(q, out, 4000, &info);
  if (ok) ac_web_read_pages(q, out, 4000, &info);
  wprintf(L"ok %d\n%s\n%s\n", ok ? 1 : 0, info.note, out);
  return ok ? 0 : 1;
}
#endif
