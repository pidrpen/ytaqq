/* Included from pad_extra.c — network folder index as JSON cache. */

#define FILES_MAX 50000
#define FILES_DEPTH 14
#define WM_FILES_DONE (WM_APP + 11)
#define TIMER_FILES 8

typedef struct {
  wchar_t *dir;
  wchar_t *name;
  wchar_t *stem;
} FileEnt;

static FileEnt *g_files;
static int g_filesN;
static volatile LONG g_filesBusy;
static volatile LONG g_filesAgain;
static ULONGLONG g_filesAt;
static SYSTEMTIME g_filesWhen;
static BOOL g_filesWhenOk;
static void save_files_pref(void);
static void files_refresh_status(void);

static void files_clear(void) {
  if (!g_files) {
    g_filesN = 0;
    return;
  }
  for (int i = 0; i < g_filesN; i++) {
    free(g_files[i].dir);
    free(g_files[i].name);
    free(g_files[i].stem);
  }
  free(g_files);
  g_files = NULL;
  g_filesN = 0;
}

static wchar_t *files_dup(const wchar_t *s) {
  if (!s) s = L"";
  size_t n = wcslen(s) + 1;
  wchar_t *p = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (p) memcpy(p, s, n * sizeof(wchar_t));
  return p;
}

static void files_stem_of(const wchar_t *name, wchar_t *out, int n) {
  lstrcpynW(out, name ? name : L"", n);
  wchar_t *dot = wcsrchr(out, L'.');
  wchar_t *slash = wcsrchr(out, L'\\');
  if (dot && (!slash || dot > slash) && dot != out) *dot = 0;
}

static void files_add(const wchar_t *dir, const wchar_t *name) {
  if (!dir || !name || !name[0] || g_filesN >= FILES_MAX) return;
  if ((g_filesN & 1023) == 0) {
    FileEnt *n = (FileEnt *)realloc(g_files, (g_filesN + 1024) * sizeof(FileEnt));
    if (!n) return;
    g_files = n;
  }
  wchar_t stem[MAX_PATH];
  files_stem_of(name, stem, MAX_PATH);
  g_files[g_filesN].dir = files_dup(dir);
  g_files[g_filesN].name = files_dup(name);
  g_files[g_filesN].stem = files_dup(stem);
  if (!g_files[g_filesN].dir || !g_files[g_filesN].name || !g_files[g_filesN].stem) return;
  g_filesN++;
}

static void files_idx_path(wchar_t *out, int n) {
  _snwprintf(out, n, L"%s\\files.json", g_dataDir);
}

static void files_stamp_now(void) {
  GetLocalTime(&g_filesWhen);
  g_filesWhenOk = TRUE;
  g_filesAt = GetTickCount64();
}

static void files_stamp_from_file(void) {
  wchar_t path[MAX_PATH];
  files_idx_path(path, MAX_PATH);
  WIN32_FILE_ATTRIBUTE_DATA ad;
  FILETIME local;
  g_filesWhenOk = FALSE;
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &ad)) return;
  if (!FileTimeToLocalFileTime(&ad.ftLastWriteTime, &local)) return;
  if (!FileTimeToSystemTime(&local, &g_filesWhen)) return;
  g_filesWhenOk = TRUE;
}

static void files_refresh_status(void) {
  wchar_t t[200];
  if (InterlockedCompareExchange(&g_filesBusy, 0, 0))
    lstrcpynW(t, L"Индекс: обновляется…", 200);
  else if (!g_filesRoot[0])
    lstrcpynW(t, L"Индекс: укажите папку", 200);
  else if (g_filesN <= 0)
    lstrcpynW(t, L"JSON пуст — «Обновить JSON»", 200);
  else if (g_filesWhenOk)
    _snwprintf(t, 200, L"JSON: %d файлов · %02u.%02u %02u:%02u", g_filesN,
               (unsigned)g_filesWhen.wDay, (unsigned)g_filesWhen.wMonth,
               (unsigned)g_filesWhen.wHour, (unsigned)g_filesWhen.wMinute);
  else
    _snwprintf(t, 200, L"JSON: %d файлов", g_filesN);
  if (g_filesStat) SetWindowTextW(g_filesStat, t);
}

static void json_put(HANDLE h, const char *s) {
  DWORD w = 0;
  if (s) WriteFile(h, s, (DWORD)strlen(s), &w, NULL);
}

static void json_put_esc(HANDLE h, const char *s) {
  json_put(h, "\"");
  for (; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    char buf[8];
    if (c == '"' || c == '\\') {
      buf[0] = '\\';
      buf[1] = (char)c;
      buf[2] = 0;
      json_put(h, buf);
    } else if (c == '\n')
      json_put(h, "\\n");
    else if (c == '\r')
      json_put(h, "\\r");
    else if (c == '\t')
      json_put(h, "\\t");
    else if (c < 32) {
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      json_put(h, buf);
    } else {
      buf[0] = (char)c;
      buf[1] = 0;
      json_put(h, buf);
    }
  }
  json_put(h, "\"");
}

static void json_put_w(HANDLE h, const wchar_t *ws) {
  char utf[MAX_PATH * 3];
  utf[0] = 0;
  WideCharToMultiByte(CP_UTF8, 0, ws ? ws : L"", -1, utf, (int)sizeof(utf), NULL, NULL);
  json_put_esc(h, utf);
}

static void files_save_idx(void) {
  wchar_t path[MAX_PATH];
  files_idx_path(path, MAX_PATH);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  json_put(h, "{\r\n  \"Path\": ");
  json_put_w(h, g_filesRoot);
  json_put(h, ",\r\n  \"Data\": [\r\n");
  for (int i = 0; i < g_filesN; i++) {
    json_put(h, "    {\r\n      \"Path\": ");
    json_put_w(h, g_files[i].dir);
    json_put(h, ",\r\n      \"Filename\": ");
    json_put_w(h, g_files[i].name);
    json_put(h, ",\r\n      \"FilenameWithoutExt\": ");
    json_put_w(h, g_files[i].stem);
    json_put(h, i + 1 < g_filesN ? "\r\n    },\r\n" : "\r\n    }\r\n");
  }
  json_put(h, "  ]\r\n}\r\n");
  CloseHandle(h);
  files_stamp_now();
}

static const char *json_skip(const char *p) {
  while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')) p++;
  return p;
}

static BOOL json_parse_str(const char **pp, wchar_t *out, int n) {
  const char *p = json_skip(*pp);
  if (!p || *p != '"') return FALSE;
  p++;
  char utf[MAX_PATH * 3];
  int o = 0;
  while (*p && *p != '"' && o < (int)sizeof(utf) - 4) {
    if (*p == '\\' && p[1]) {
      p++;
      if (*p == 'n')
        utf[o++] = '\n';
      else if (*p == 'r')
        utf[o++] = '\r';
      else if (*p == 't')
        utf[o++] = '\t';
      else if (*p == 'u' && p[1] && p[2] && p[3] && p[4]) {
        unsigned v = 0;
        for (int i = 0; i < 4; i++) {
          char h = *++p;
          v <<= 4;
          if (h >= '0' && h <= '9')
            v += (unsigned)(h - '0');
          else if (h >= 'a' && h <= 'f')
            v += (unsigned)(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F')
            v += (unsigned)(h - 'A' + 10);
        }
        if (v < 0x80)
          utf[o++] = (char)v;
        else if (v < 0x800) {
          utf[o++] = (char)(0xC0 | (v >> 6));
          utf[o++] = (char)(0x80 | (v & 0x3F));
        } else {
          utf[o++] = (char)(0xE0 | (v >> 12));
          utf[o++] = (char)(0x80 | ((v >> 6) & 0x3F));
          utf[o++] = (char)(0x80 | (v & 0x3F));
        }
      } else
        utf[o++] = *p;
      p++;
    } else
      utf[o++] = *p++;
  }
  utf[o] = 0;
  if (*p == '"') p++;
  *pp = p;
  MultiByteToWideChar(CP_UTF8, 0, utf, -1, out, n);
  return TRUE;
}

static const char *json_key(const char *from, const char *lim, const char *key) {
  char pat[80];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  size_t plen = strlen(pat);
  const char *p = from;
  while (p && p < lim) {
    size_t left = (size_t)(lim - p);
    if (left < plen) return NULL;
    const char *hit = NULL;
    for (const char *q = p; (size_t)(lim - q) >= plen; q++) {
      if (strncmp(q, pat, plen) == 0) {
        hit = q;
        break;
      }
    }
    if (!hit) return NULL;
    const char *c = json_skip(hit + plen);
    if (*c == ':') return json_skip(c + 1);
    p = hit + 1;
  }
  return NULL;
}

static BOOL files_load_idx(void) {
  files_clear();
  wchar_t path[MAX_PATH];
  files_idx_path(path, MAX_PATH);
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  DWORD sz = GetFileSize(h, NULL);
  if (sz == INVALID_FILE_SIZE || sz == 0 || sz > 40 * 1024 * 1024) {
    CloseHandle(h);
    return FALSE;
  }
  char *buf = (char *)malloc(sz + 1);
  if (!buf) {
    CloseHandle(h);
    return FALSE;
  }
  DWORD r = 0;
  ReadFile(h, buf, sz, &r, NULL);
  CloseHandle(h);
  buf[r] = 0;
  const char *rootp = json_key(buf, buf + r, "Path");
  wchar_t idxRoot[MAX_PATH] = {0};
  if (rootp) {
    const char *tmp = rootp;
    json_parse_str(&tmp, idxRoot, MAX_PATH);
  }
  const char *data = strstr(buf, "\"Data\"");
  if (!data) {
    free(buf);
    return FALSE;
  }
  data = strchr(data, '[');
  if (!data) {
    free(buf);
    return FALSE;
  }
  data++;
  while (data && *data) {
    data = json_skip(data);
    if (*data == ']' || !*data) break;
    if (*data != '{') {
      data++;
      continue;
    }
    const char *obj = data;
    const char *end = strchr(obj, '}');
    if (!end) break;
    wchar_t dir[MAX_PATH] = {0}, name[MAX_PATH] = {0}, stem[MAX_PATH] = {0};
    const char *v = json_key(obj, end, "Path");
    if (v) json_parse_str(&v, dir, MAX_PATH);
    v = json_key(obj, end, "Filename");
    if (v) json_parse_str(&v, name, MAX_PATH);
    v = json_key(obj, end, "FilenameWithoutExt");
    if (v) json_parse_str(&v, stem, MAX_PATH);
    if (name[0]) {
      if (!dir[0] && idxRoot[0]) lstrcpynW(dir, idxRoot, MAX_PATH);
      files_add(dir, name);
      if (stem[0] && g_filesN > 0) {
        free(g_files[g_filesN - 1].stem);
        g_files[g_filesN - 1].stem = files_dup(stem);
      }
    }
    data = end + 1;
  }
  free(buf);
  if (g_filesRoot[0] && idxRoot[0] && _wcsicmp(g_filesRoot, idxRoot) != 0) {
    files_clear();
    return FALSE;
  }
  if (idxRoot[0] && !g_filesRoot[0]) lstrcpynW(g_filesRoot, idxRoot, MAX_PATH);
  files_stamp_from_file();
  return g_filesN > 0;
}

static BOOL files_idx_fresh(void) {
  wchar_t path[MAX_PATH];
  files_idx_path(path, MAX_PATH);
  WIN32_FILE_ATTRIBUTE_DATA ad;
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &ad)) return FALSE;
  ULARGE_INTEGER ft;
  ft.LowPart = ad.ftLastWriteTime.dwLowDateTime;
  ft.HighPart = ad.ftLastWriteTime.dwHighDateTime;
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER n;
  n.LowPart = now.dwLowDateTime;
  n.HighPart = now.dwHighDateTime;
  ULONGLONG hour = 10000000ULL * 3600ULL;
  return n.QuadPart > ft.QuadPart && (n.QuadPart - ft.QuadPart) < hour;
}

static void files_walk(const wchar_t *dir, int depth) {
  if (depth > FILES_DEPTH || g_filesN >= FILES_MAX || !dir[0]) return;
  wchar_t glob[MAX_PATH];
  if (_snwprintf(glob, MAX_PATH, L"%s\\*", dir) < 0) return;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(glob, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.cFileName[0] == L'.' &&
        (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
    wchar_t full[MAX_PATH];
    if (_snwprintf(full, MAX_PATH, L"%s\\%s", dir, fd.cFileName) < 0) continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      files_walk(full, depth + 1);
    } else {
      files_add(dir, fd.cFileName);
    }
  } while (FindNextFileW(h, &fd) && g_filesN < FILES_MAX);
  FindClose(h);
}

static DWORD WINAPI files_index_thread(LPVOID param) {
  (void)param;
again:
  files_clear();
  if (g_filesRoot[0]) files_walk(g_filesRoot, 0);
  files_save_idx();
  if (InterlockedExchange(&g_filesAgain, 0)) goto again;
  InterlockedExchange(&g_filesBusy, 0);
  if (g_hwnd) PostMessageW(g_hwnd, WM_FILES_DONE, (WPARAM)g_filesN, 0);
  return 0;
}

static void files_start_index(BOOL force) {
  if (!g_filesRoot[0]) {
    files_refresh_status();
    return;
  }
  if (!force && g_filesN > 0 && g_filesAt && GetTickCount64() - g_filesAt < 3600000ULL)
    return;
  if (InterlockedCompareExchange(&g_filesBusy, 1, 0) != 0) {
    InterlockedExchange(&g_filesAgain, 1);
    files_refresh_status();
    return;
  }
  files_refresh_status();
  HANDLE th = CreateThread(NULL, 0, files_index_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else {
    InterlockedExchange(&g_filesBusy, 0);
    files_refresh_status();
  }
}

static void files_apply_root(BOOL force) {
  wchar_t old[MAX_PATH];
  lstrcpynW(old, g_filesRoot, MAX_PATH);
  save_files_pref();
  BOOL changed = _wcsicmp(old, g_filesRoot) != 0;
  if (changed) {
    files_clear();
    wchar_t jp[MAX_PATH];
    files_idx_path(jp, MAX_PATH);
    DeleteFileW(jp);
    g_filesWhenOk = FALSE;
    g_filesAt = 0;
  }
  if ((force || changed) && g_filesRoot[0]) files_start_index(TRUE);
  files_refresh_status();
}

static BOOL wcs_istr(const wchar_t *hay, const wchar_t *needle) {
  if (!needle[0]) return TRUE;
  if (!hay) return FALSE;
  for (; *hay; hay++) {
    const wchar_t *h = hay, *n = needle;
    while (*h && *n && towlower(*h) == towlower(*n)) {
      h++;
      n++;
    }
    if (!*n) return TRUE;
  }
  return FALSE;
}

static const wchar_t *files_name(const wchar_t *rel) {
  const wchar_t *s = rel, *last = rel;
  for (; *s; s++)
    if (*s == L'\\' || *s == L'/') last = s + 1;
  return last;
}

static BOOL files_search(const wchar_t *query, wchar_t *out, int cap) {
  g_plmCount = 0;
  g_resultFiles = TRUE;
  save_files_pref();
  if (!g_filesRoot[0]) {
    lstrcpynW(out, L"Файлы\r\n\r\nУкажите папку в Настройках.", cap);
    return FALSE;
  }
  if (g_filesN == 0) files_load_idx();
  if (g_filesN == 0) {
    _snwprintf(out, cap,
               L"Файлы\r\n\r\nJSON пуст для «%.80s».\r\nНажмите «Обновить JSON».",
               g_filesRoot);
    return FALSE;
  }
  wchar_t q[200];
  lstrcpynW(q, query, 200);
  flatten_clip_line(q);
  int n = 0;
  wchar_t links[1800] = {0};
  for (int i = 0; i < g_filesN && n < 20; i++) {
    if (!wcs_istr(g_files[i].name, q) && !wcs_istr(g_files[i].stem, q) &&
        !wcs_istr(g_files[i].dir, q))
      continue;
    wchar_t full[420];
    _snwprintf(full, 420, L"%s\\%s", g_files[i].dir, g_files[i].name);
    lstrcpynW(g_plmEsi[n], g_files[i].name, 200);
    lstrcpynW(g_plmTp[n], g_files[i].dir, 200);
    lstrcpynW(g_plmLinks[n], full, 420);
    if (!g_plmLastLink[0]) lstrcpynW(g_plmLastLink, full, 420);
    if (n) wcscat(links, L"\r\n");
    if ((int)(wcslen(links) + wcslen(full) + 8) < 1800) wcscat(links, full);
    n++;
  }
  g_plmCount = n;
  if (n == 0) {
    _snwprintf(out, cap, L"Файлы\r\n\r\nВ JSON нет «%.80s» (%d записей).", q, g_filesN);
    return FALSE;
  }
  _snwprintf(out, cap, L"Файлы · %d из JSON\r\n\r\n%s", n, links);
  return TRUE;
}

static void load_files_pref(void) {
  wchar_t path[MAX_PATH];
  if (!g_dataDir[0]) return;
  _snwprintf(path, MAX_PATH, L"%s\\files.txt", g_dataDir);
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    char buf[MAX_PATH * 3];
    DWORD n = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &n, NULL);
    CloseHandle(h);
    buf[n] = 0;
    char *nl = strchr(buf, '\n');
    if (nl) *nl = 0;
    char *cr = strchr(buf, '\r');
    if (cr) *cr = 0;
    if (buf[0]) MultiByteToWideChar(CP_UTF8, 0, buf, -1, g_filesRoot, MAX_PATH);
  }
  files_load_idx();
  files_refresh_status();
}

static void save_files_pref(void) {
  if (g_filesRootEdit) GetWindowTextW(g_filesRootEdit, g_filesRoot, MAX_PATH);
  wchar_t path[MAX_PATH];
  if (!g_dataDir[0]) return;
  _snwprintf(path, MAX_PATH, L"%s\\files.txt", g_dataDir);
  char utf[MAX_PATH * 3];
  WideCharToMultiByte(CP_UTF8, 0, g_filesRoot, -1, utf, (int)sizeof(utf), NULL, NULL);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(h, utf, (DWORD)strlen(utf), &w, NULL);
  CloseHandle(h);
}
