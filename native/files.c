/* Included from pad_extra.c — network folder index and hourly cache. */

#define FILES_MAX 50000
#define FILES_DEPTH 14
#define WM_FILES_DONE (WM_APP + 11)
#define TIMER_FILES 8

static wchar_t **g_filesRel;
static int g_filesN;
static volatile LONG g_filesBusy;
static ULONGLONG g_filesAt;

static void files_clear(void) {
  if (!g_filesRel) {
    g_filesN = 0;
    return;
  }
  for (int i = 0; i < g_filesN; i++) free(g_filesRel[i]);
  free(g_filesRel);
  g_filesRel = NULL;
  g_filesN = 0;
}

static void files_add(const wchar_t *rel) {
  if (!rel || !rel[0] || g_filesN >= FILES_MAX) return;
  if ((g_filesN & 1023) == 0) {
    wchar_t **n = (wchar_t **)realloc(g_filesRel, (g_filesN + 1024) * sizeof(wchar_t *));
    if (!n) return;
    g_filesRel = n;
  }
  size_t nch = wcslen(rel) + 1;
  wchar_t *p = (wchar_t *)malloc(nch * sizeof(wchar_t));
  if (!p) return;
  memcpy(p, rel, nch * sizeof(wchar_t));
  g_filesRel[g_filesN++] = p;
}

static void files_idx_path(wchar_t *out, int n) {
  _snwprintf(out, n, L"%s\\files.idx", g_dataDir);
}

static void files_cache_dir(wchar_t *out, int n) {
  _snwprintf(out, n, L"%s\\filecache", g_dataDir);
}

static void files_save_idx(void) {
  wchar_t path[MAX_PATH];
  files_idx_path(path, MAX_PATH);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  char root[MAX_PATH * 3];
  WideCharToMultiByte(CP_UTF8, 0, g_filesRoot, -1, root, (int)sizeof(root), NULL, NULL);
  char line[MAX_PATH * 4 + 8];
  DWORD w = 0;
  int n = snprintf(line, sizeof(line), "ROOT %s\n", root);
  WriteFile(h, line, (DWORD)n, &w, NULL);
  for (int i = 0; i < g_filesN; i++) {
    char rel[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, g_filesRel[i], -1, rel, (int)sizeof(rel), NULL, NULL);
    n = snprintf(line, sizeof(line), "%s\n", rel);
    WriteFile(h, line, (DWORD)n, &w, NULL);
  }
  CloseHandle(h);
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
  char *p = buf;
  while (p && *p) {
    char *nl = strchr(p, '\n');
    if (nl) *nl = 0;
    char *cr = strchr(p, '\r');
    if (cr) *cr = 0;
    if (!strncmp(p, "ROOT ", 5)) {
      MultiByteToWideChar(CP_UTF8, 0, p + 5, -1, g_filesRoot, MAX_PATH);
    } else if (p[0]) {
      wchar_t rel[MAX_PATH];
      MultiByteToWideChar(CP_UTF8, 0, p, -1, rel, MAX_PATH);
      files_add(rel);
    }
    p = nl ? nl + 1 : NULL;
  }
  free(buf);
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
      size_t rootn = wcslen(g_filesRoot);
      const wchar_t *rel = full;
      if (_wcsnicmp(full, g_filesRoot, rootn) == 0) {
        rel = full + rootn;
        while (*rel == L'\\' || *rel == L'/') rel++;
      }
      files_add(rel);
    }
  } while (FindNextFileW(h, &fd) && g_filesN < FILES_MAX);
  FindClose(h);
}

static DWORD WINAPI files_index_thread(LPVOID param) {
  (void)param;
  files_clear();
  if (g_filesRoot[0]) files_walk(g_filesRoot, 0);
  files_save_idx();
  g_filesAt = GetTickCount64();
  InterlockedExchange(&g_filesBusy, 0);
  if (g_hwnd) PostMessageW(g_hwnd, WM_FILES_DONE, (WPARAM)g_filesN, 0);
  return 0;
}

static void files_start_index(BOOL force) {
  if (!g_filesRoot[0]) return;
  if (!force && g_filesN > 0 && g_filesAt && GetTickCount64() - g_filesAt < 3600000ULL)
    return;
  if (InterlockedCompareExchange(&g_filesBusy, 1, 0) != 0) return;
  HANDLE th = CreateThread(NULL, 0, files_index_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else InterlockedExchange(&g_filesBusy, 0);
}

static BOOL wcs_istr(const wchar_t *hay, const wchar_t *needle) {
  if (!needle[0]) return TRUE;
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

static void files_copy_hit(const wchar_t *full, const wchar_t *name) {
  wchar_t dir[MAX_PATH], dest[MAX_PATH];
  files_cache_dir(dir, MAX_PATH);
  CreateDirectoryW(dir, NULL);
  _snwprintf(dest, MAX_PATH, L"%s\\%s", dir, name);
  CopyFileW(full, dest, FALSE);
}

static BOOL files_search(const wchar_t *query, wchar_t *out, int cap) {
  g_plmCount = 0;
  g_resultFiles = TRUE;
  if (g_filesRootEdit) GetWindowTextW(g_filesRootEdit, g_filesRoot, MAX_PATH);
  if (!g_filesRoot[0]) {
    lstrcpynW(out, L"Файлы\r\n\r\nУкажите папку сети в Настройках (\\\\сервер\\шара или диск).", cap);
    return FALSE;
  }
  if (g_filesN == 0) files_load_idx();
  if (g_filesN == 0 || !files_idx_fresh()) files_start_index(FALSE);
  if (g_filesN == 0) {
    _snwprintf(out, cap,
               L"Файлы\r\n\r\nИндексирую «%.80s»… Повторите F3 через минуту.", g_filesRoot);
    return FALSE;
  }
  wchar_t q[200];
  lstrcpynW(q, query, 200);
  flatten_clip_line(q);
  int n = 0;
  wchar_t links[1800] = {0};
  for (int i = 0; i < g_filesN && n < 20; i++) {
    if (!wcs_istr(g_filesRel[i], q)) continue;
    const wchar_t *name = files_name(g_filesRel[i]);
    lstrcpynW(g_plmEsi[n], name, 200);
    const wchar_t *slash = wcsrchr(g_filesRel[i], L'\\');
    if (slash && slash > g_filesRel[i]) {
      size_t dlen = (size_t)(slash - g_filesRel[i]);
      if (dlen > 199) dlen = 199;
      memcpy(g_plmTp[n], g_filesRel[i], dlen * sizeof(wchar_t));
      g_plmTp[n][dlen] = 0;
    } else {
      g_plmTp[n][0] = 0;
    }
    _snwprintf(g_plmLinks[n], 420, L"%s\\%s", g_filesRoot, g_filesRel[i]);
    files_copy_hit(g_plmLinks[n], name);
    if (!g_plmLastLink[0]) lstrcpynW(g_plmLastLink, g_plmLinks[n], 420);
    if (n) wcscat(links, L"\r\n");
    if ((int)(wcslen(links) + wcslen(g_plmLinks[n]) + 8) < 1800) wcscat(links, g_plmLinks[n]);
    n++;
  }
  g_plmCount = n;
  if (n == 0) {
    _snwprintf(out, cap, L"Файлы\r\n\r\nНет совпадений по «%.80s» (в индексе %d файлов).", q, g_filesN);
    return FALSE;
  }
  _snwprintf(out, cap, L"Файлы · %d  (кеш обновляется каждый час)\r\n\r\n%s", n, links);
  return TRUE;
}

static void load_files_pref(void) {
  wchar_t path[MAX_PATH];
  if (!g_dataDir[0]) return;
  _snwprintf(path, MAX_PATH, L"%s\\files.txt", g_dataDir);
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
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
  files_load_idx();
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
