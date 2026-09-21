/* Included from pad_extra.c — network folder index as JSON cache.

   No depth limit, no file-count limit, no index-size limit:
   - the walk uses \\?\ extended paths and a growable buffer, so it is not
     bound by MAX_PATH or by a fixed nesting depth;
   - entries live in an arena with de-duplicated folder names, so a million
     files cost tens of megabytes instead of hundreds;
   - the JSON is written compactly through a buffered writer into a temp file
     that replaces the old one atomically, so an interrupted refresh never
     destroys a working index. */

#define WM_FILES_DONE (WM_APP + 11)
#define TIMER_FILES 8
#define TIMER_FILES_TICK 9
#define FILES_WALK_GUARD 4096      /* runaway-recursion valve, not a feature limit */
#define FILES_ARENA_CHUNK 262144   /* wchar_t per arena chunk */
#define FILES_JSON_MAX (1024ull * 1024ull * 1024ull)

typedef struct FileArena {
  struct FileArena *next;
  size_t used, cap;
  wchar_t *data;
} FileArena;

typedef struct {
  int dir;            /* index into FileIdx.dirs */
  const wchar_t *name;/* arena-owned */
} FileEnt;

typedef struct {
  FileArena *arena;
  const wchar_t **dirs;
  int dirsN, dirsCap;
  FileEnt *ent;
  int n, cap;
} FileIdx;

static FileIdx *g_idx;             /* read under g_filesCs */
static CRITICAL_SECTION g_filesCs;
static BOOL g_filesCsOk;
static int g_filesN;               /* entry count of g_idx, for the status line */
static volatile LONG g_filesBusy;
static volatile LONG g_filesAgain;
static ULONGLONG g_filesAt;
static SYSTEMTIME g_filesWhen;
static BOOL g_filesWhenOk;
static wchar_t g_filesNote[120];   /* last problem worth telling the user about */
static volatile LONG g_filesScanned; /* live counter for the progress line */
static volatile LONG g_filesCancel;  /* set to abandon a walk in progress */
static void save_files_pref(void);
static void files_refresh_status(void);

static void files_lock_init(void) {
  if (g_filesCsOk) return;
  InitializeCriticalSection(&g_filesCs);
  g_filesCsOk = TRUE;
}

static void files_lock(void) {
  if (g_filesCsOk) EnterCriticalSection(&g_filesCs);
}

static void files_unlock(void) {
  if (g_filesCsOk) LeaveCriticalSection(&g_filesCs);
}

/* ---- index container ---------------------------------------------------- */

static FileIdx *idx_new(void) {
  return (FileIdx *)calloc(1, sizeof(FileIdx));
}

static void idx_free(FileIdx *ix) {
  if (!ix) return;
  FileArena *a = ix->arena;
  while (a) {
    FileArena *next = a->next;
    free(a->data);
    free(a);
    a = next;
  }
  free(ix->dirs);
  free(ix->ent);
  free(ix);
}

static const wchar_t *idx_intern(FileIdx *ix, const wchar_t *s) {
  if (!s) s = L"";
  size_t need = wcslen(s) + 1;
  FileArena *a = ix->arena;
  if (!a || a->cap - a->used < need) {
    size_t cap = need > FILES_ARENA_CHUNK ? need : FILES_ARENA_CHUNK;
    FileArena *fresh = (FileArena *)malloc(sizeof(FileArena));
    if (!fresh) return NULL;
    fresh->data = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!fresh->data) {
      free(fresh);
      return NULL;
    }
    fresh->used = 0;
    fresh->cap = cap;
    fresh->next = ix->arena;
    ix->arena = fresh;
    a = fresh;
  }
  wchar_t *dst = a->data + a->used;
  memcpy(dst, s, need * sizeof(wchar_t));
  a->used += need;
  return dst;
}

/* Files arrive grouped by folder (both from the walk and from the JSON), so
   comparing against the most recent folders de-duplicates without a hash. */
static int idx_dir(FileIdx *ix, const wchar_t *dir) {
  int back = ix->dirsN < 64 ? ix->dirsN : 64;
  for (int i = 1; i <= back; i++)
    if (_wcsicmp(ix->dirs[ix->dirsN - i], dir) == 0) return ix->dirsN - i;
  if (ix->dirsN >= ix->dirsCap) {
    int cap = ix->dirsCap ? ix->dirsCap * 2 : 256;
    const wchar_t **grown = (const wchar_t **)realloc((void *)ix->dirs, (size_t)cap * sizeof(wchar_t *));
    if (!grown) return -1;
    ix->dirs = grown;
    ix->dirsCap = cap;
  }
  const wchar_t *copy = idx_intern(ix, dir);
  if (!copy) return -1;
  ix->dirs[ix->dirsN] = copy;
  return ix->dirsN++;
}

static BOOL idx_add(FileIdx *ix, int dirIdx, const wchar_t *name) {
  if (dirIdx < 0 || !name || !name[0]) return FALSE;
  if (ix->n >= ix->cap) {
    int cap = ix->cap ? ix->cap * 2 : 4096;
    FileEnt *grown = (FileEnt *)realloc(ix->ent, (size_t)cap * sizeof(FileEnt));
    if (!grown) return FALSE;
    ix->ent = grown;
    ix->cap = cap;
  }
  const wchar_t *copy = idx_intern(ix, name);
  if (!copy) return FALSE;
  ix->ent[ix->n].dir = dirIdx;
  ix->ent[ix->n].name = copy;
  ix->n++;
  return TRUE;
}

/* Publish a freshly built index and retire the old one. */
static void idx_publish(FileIdx *fresh) {
  FileIdx *old;
  files_lock();
  old = g_idx;
  g_idx = fresh;
  g_filesN = fresh ? fresh->n : 0;
  files_unlock();
  idx_free(old);
}

static void files_clear(void) {
  idx_publish(NULL);
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
  BOOL busy = InterlockedCompareExchange(&g_filesBusy, 0, 0) != 0;
  if (g_filesBar) {
    /* the walk has no total to count towards, so the bar just shows life
       while the number next to it says how far it has got */
    SendMessageW(g_filesBar, PBM_SETMARQUEE, (WPARAM)busy, 40);
    ShowWindow(g_filesBar, busy ? SW_SHOW : SW_HIDE);
  }
  if (g_btnIdx) SetWindowTextW(g_btnIdx, busy ? L"Остановить" : L"Обновить JSON");
  if (busy)
    _snwprintf(t, 200, L"Обход папки: %ld файлов…",
               (long)InterlockedCompareExchange(&g_filesScanned, 0, 0));
  else if (!g_filesRoot[0])
    lstrcpynW(t, L"Индекс: укажите папку", 200);
  else if (g_filesNote[0])
    lstrcpynW(t, g_filesNote, 200);
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

/* ---- buffered JSON writer ----------------------------------------------- */

typedef struct {
  HANDLE h;
  char *buf;
  int used, cap;
  BOOL ok;
} JsonOut;

static BOOL json_flush(JsonOut *o) {
  if (!o->ok || o->used <= 0) return o->ok;
  DWORD w = 0;
  if (!WriteFile(o->h, o->buf, (DWORD)o->used, &w, NULL) || (int)w != o->used) o->ok = FALSE;
  o->used = 0;
  return o->ok;
}

static void json_put(JsonOut *o, const char *s) {
  if (!o->ok || !s) return;
  int n = (int)strlen(s);
  if (n >= o->cap) {
    json_flush(o);
    DWORD w = 0;
    if (o->ok && (!WriteFile(o->h, s, (DWORD)n, &w, NULL) || (int)w != n)) o->ok = FALSE;
    return;
  }
  if (o->used + n > o->cap && !json_flush(o)) return;
  memcpy(o->buf + o->used, s, (size_t)n);
  o->used += n;
}

static void json_put_esc(JsonOut *o, const char *s) {
  json_put(o, "\"");
  for (; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    char buf[8];
    if (c == '"' || c == '\\') {
      buf[0] = '\\';
      buf[1] = (char)c;
      buf[2] = 0;
      json_put(o, buf);
    } else if (c == '\n')
      json_put(o, "\\n");
    else if (c == '\r')
      json_put(o, "\\r");
    else if (c == '\t')
      json_put(o, "\\t");
    else if (c < 32) {
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      json_put(o, buf);
    } else {
      buf[0] = (char)c;
      buf[1] = 0;
      json_put(o, buf);
    }
  }
  json_put(o, "\"");
}

static void json_put_w(JsonOut *o, const wchar_t *ws) {
  int need = WideCharToMultiByte(CP_UTF8, 0, ws ? ws : L"", -1, NULL, 0, NULL, NULL);
  if (need <= 0) {
    json_put(o, "\"\"");
    return;
  }
  char stack[1024];
  char *utf = need <= (int)sizeof(stack) ? stack : (char *)malloc((size_t)need);
  if (!utf) {
    json_put(o, "\"\"");
    return;
  }
  WideCharToMultiByte(CP_UTF8, 0, ws ? ws : L"", -1, utf, need, NULL, NULL);
  json_put_esc(o, utf);
  if (utf != stack) free(utf);
}

/* Writes to files.json.tmp and swaps it in, so a failed or interrupted
   refresh leaves the previous index untouched. */
static BOOL files_save_idx(FileIdx *ix) {
  if (!ix) return FALSE;
  wchar_t path[MAX_PATH], tmp[MAX_PATH];
  files_idx_path(path, MAX_PATH);
  _snwprintf(tmp, MAX_PATH, L"%s.tmp", path);
  HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  JsonOut o;
  o.h = h;
  o.cap = 1 << 16;
  o.used = 0;
  o.ok = TRUE;
  o.buf = (char *)malloc((size_t)o.cap);
  if (!o.buf) {
    CloseHandle(h);
    DeleteFileW(tmp);
    return FALSE;
  }
  json_put(&o, "{\"Path\":");
  json_put_w(&o, g_filesRoot);
  json_put(&o, ",\"Dirs\":[");
  for (int i = 0; i < ix->dirsN; i++) {
    if (i) json_put(&o, ",");
    json_put_w(&o, ix->dirs[i]);
  }
  json_put(&o, "],\"Data\":[");
  for (int i = 0; i < ix->n; i++) {
    char head[32];
    snprintf(head, sizeof(head), "%s{\"D\":%d,\"F\":", i ? "," : "", ix->ent[i].dir);
    json_put(&o, head);
    json_put_w(&o, ix->ent[i].name);
    json_put(&o, "}");
  }
  json_put(&o, "]}");
  json_flush(&o);
  BOOL ok = o.ok;
  free(o.buf);
  if (!FlushFileBuffers(h)) ok = FALSE;
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(tmp);
    return FALSE;
  }
  if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(tmp);
    return FALSE;
  }
  files_stamp_now();
  return TRUE;
}

/* ---- JSON reading -------------------------------------------------------- */

static const char *json_skip(const char *p) {
  while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')) p++;
  return p;
}

static BOOL json_parse_str(const char **pp, wchar_t *out, int n) {
  const char *p = json_skip(*pp);
  if (!p || *p != '"') return FALSE;
  p++;
  char utf[MAX_PATH * 6];
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
          char hx = *++p;
          v <<= 4;
          if (hx >= '0' && hx <= '9')
            v += (unsigned)(hx - '0');
          else if (hx >= 'a' && hx <= 'f')
            v += (unsigned)(hx - 'a' + 10);
          else if (hx >= 'A' && hx <= 'F')
            v += (unsigned)(hx - 'A' + 10);
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
  /* skip anything past the buffer instead of leaving the cursor mid-string */
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) p++;
    p++;
  }
  utf[o] = 0;
  if (*p == '"') p++;
  *pp = p;
  MultiByteToWideChar(CP_UTF8, 0, utf, -1, out, n);
  out[n - 1] = 0;
  return TRUE;
}

static const char *json_key(const char *from, const char *lim, const char *key) {
  char pat[80];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  size_t plen = strlen(pat);
  const char *p = from;
  while (p && p < lim) {
    if ((size_t)(lim - p) < plen) return NULL;
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

/* Compact form: {"Path":…,"Dirs":[…],"Data":[{"D":n,"F":"name"}…]} */
static BOOL files_parse_compact(const char *buf, FileIdx *ix, wchar_t *idxRoot) {
  const char *dirs = strstr(buf, "\"Dirs\"");
  if (!dirs) return FALSE;
  dirs = strchr(dirs, '[');
  if (!dirs) return FALSE;
  dirs++;
  wchar_t *dir = (wchar_t *)malloc(32768 * sizeof(wchar_t));
  if (!dir) return FALSE;
  while (*dirs) {
    dirs = json_skip(dirs);
    if (*dirs != '"') break;
    if (!json_parse_str(&dirs, dir, 32768)) break;
    if (idx_dir(ix, dir) < 0) break;
  }
  free(dir);
  const char *data = strstr(buf, "\"Data\"");
  if (!data) return FALSE;
  data = strchr(data, '[');
  if (!data) return FALSE;
  data++;
  wchar_t name[MAX_PATH * 2];
  while (*data) {
    data = json_skip(data);
    if (*data != '{') break;
    const char *obj = data;
    const char *end = strchr(obj, '}');
    if (!end) break;
    int d = -1;
    const char *v = json_key(obj, end, "D");
    if (v) d = atoi(v);
    v = json_key(obj, end, "F");
    name[0] = 0;
    if (v) json_parse_str(&v, name, MAX_PATH * 2);
    if (name[0] && d >= 0 && d < ix->dirsN) idx_add(ix, d, name);
    data = end + 1;
  }
  (void)idxRoot;
  return TRUE;
}

/* Legacy form written by earlier versions: one object per file carrying the
   full folder path. Still read so an existing files.json keeps working. */
static BOOL files_parse_legacy(const char *buf, FileIdx *ix, const wchar_t *idxRoot) {
  const char *data = strstr(buf, "\"Data\"");
  if (!data) return FALSE;
  data = strchr(data, '[');
  if (!data) return FALSE;
  data++;
  wchar_t *dir = (wchar_t *)malloc(32768 * sizeof(wchar_t));
  if (!dir) return FALSE;
  wchar_t name[MAX_PATH * 2];
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
    dir[0] = 0;
    name[0] = 0;
    const char *v = json_key(obj, end, "Path");
    if (v) json_parse_str(&v, dir, 32768);
    v = json_key(obj, end, "Filename");
    if (v) json_parse_str(&v, name, MAX_PATH * 2);
    if (name[0]) {
      if (!dir[0] && idxRoot[0]) lstrcpynW(dir, idxRoot, 32768);
      idx_add(ix, idx_dir(ix, dir), name);
    }
    data = end + 1;
  }
  free(dir);
  return TRUE;
}

static BOOL files_load_idx(void) {
  files_lock_init();
  g_filesNote[0] = 0;
  wchar_t path[MAX_PATH];
  files_idx_path(path, MAX_PATH);
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    files_clear();
    return FALSE;
  }
  LARGE_INTEGER li;
  li.QuadPart = 0;
  if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0) {
    CloseHandle(h);
    files_clear();
    return FALSE;
  }
  if ((unsigned long long)li.QuadPart > FILES_JSON_MAX) {
    CloseHandle(h);
    lstrcpynW(g_filesNote, L"Индекс больше 1 ГБ — сузьте папку", 120);
    files_clear();
    return FALSE;
  }
  size_t sz = (size_t)li.QuadPart;
  char *buf = (char *)malloc(sz + 1);
  if (!buf) {
    CloseHandle(h);
    lstrcpynW(g_filesNote, L"Не хватило памяти на индекс", 120);
    files_clear();
    return FALSE;
  }
  size_t got = 0;
  while (got < sz) {
    DWORD want = (DWORD)((sz - got) > 0x10000000u ? 0x10000000u : (sz - got));
    DWORD r = 0;
    if (!ReadFile(h, buf + got, want, &r, NULL) || r == 0) break;
    got += r;
  }
  CloseHandle(h);
  buf[got] = 0;
  wchar_t idxRoot[MAX_PATH] = {0};
  const char *rootp = json_key(buf, buf + got, "Path");
  if (rootp) {
    const char *tmp = rootp;
    json_parse_str(&tmp, idxRoot, MAX_PATH);
  }
  FileIdx *ix = idx_new();
  if (!ix) {
    free(buf);
    files_clear();
    return FALSE;
  }
  if (!files_parse_compact(buf, ix, idxRoot)) files_parse_legacy(buf, ix, idxRoot);
  free(buf);
  if (g_filesRoot[0] && idxRoot[0] && _wcsicmp(g_filesRoot, idxRoot) != 0) {
    idx_free(ix);
    files_clear();
    return FALSE;
  }
  if (idxRoot[0] && !g_filesRoot[0]) lstrcpynW(g_filesRoot, idxRoot, MAX_PATH);
  idx_publish(ix);
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

/* ---- the walk ------------------------------------------------------------ */

/* Growable path buffer: nesting is limited by disk, not by MAX_PATH. */
typedef struct {
  wchar_t *w;
  size_t len, cap;
  size_t skip;          /* chars of \\?\ prefix to hide from the user */
  const wchar_t *shown; /* what to show in their place */
} WalkPath;

static BOOL wp_reserve(WalkPath *p, size_t need) {
  if (p->cap >= need) return TRUE;
  size_t cap = p->cap ? p->cap : 1024;
  while (cap < need) cap *= 2;
  wchar_t *grown = (wchar_t *)realloc(p->w, cap * sizeof(wchar_t));
  if (!grown) return FALSE;
  p->w = grown;
  p->cap = cap;
  return TRUE;
}

static BOOL wp_push(WalkPath *p, const wchar_t *part) {
  size_t add = wcslen(part);
  if (!wp_reserve(p, p->len + add + 2)) return FALSE;
  p->w[p->len++] = L'\\';
  memcpy(p->w + p->len, part, add * sizeof(wchar_t));
  p->len += add;
  p->w[p->len] = 0;
  return TRUE;
}

static void wp_pop(WalkPath *p, size_t mark) {
  p->len = mark;
  if (p->w) p->w[p->len] = 0;
}

/* Build the display form of the current folder: \\?\C:\x → C:\x,
   \\?\UNC\srv\share\x → \\srv\share\x. */
static BOOL wp_display(const WalkPath *p, wchar_t **out, size_t *cap) {
  size_t shown = wcslen(p->shown);
  size_t need = shown + (p->len - p->skip) + 1;
  if (*cap < need) {
    size_t c = *cap ? *cap : 1024;
    while (c < need) c *= 2;
    wchar_t *grown = (wchar_t *)realloc(*out, c * sizeof(wchar_t));
    if (!grown) return FALSE;
    *out = grown;
    *cap = c;
  }
  memcpy(*out, p->shown, shown * sizeof(wchar_t));
  memcpy(*out + shown, p->w + p->skip, (p->len - p->skip) * sizeof(wchar_t));
  (*out)[need - 1] = 0;
  return TRUE;
}

static BOOL wp_init(WalkPath *p, const wchar_t *root) {
  memset(p, 0, sizeof(*p));
  const wchar_t *body = root;
  const wchar_t *prefix = L"\\\\?\\";
  p->shown = L"";
  if (wcsncmp(root, L"\\\\?\\", 4) == 0) {
    prefix = L"";
    body = root;
    p->skip = wcsncmp(root + 4, L"UNC\\", 4) == 0 ? 8 : 4;
    p->shown = wcsncmp(root + 4, L"UNC\\", 4) == 0 ? L"\\\\" : L"";
  } else if (root[0] == L'\\' && root[1] == L'\\') {
    prefix = L"\\\\?\\UNC\\";
    body = root + 2;
    p->skip = 8;
    p->shown = L"\\\\";
  } else {
    p->skip = 4;
    p->shown = L"";
  }
  size_t need = wcslen(prefix) + wcslen(body) + 2;
  if (!wp_reserve(p, need)) return FALSE;
  _snwprintf(p->w, need, L"%s%s", prefix, body);
  p->len = wcslen(p->w);
  while (p->len > p->skip && p->w[p->len - 1] == L'\\') p->w[--p->len] = 0;
  return TRUE;
}

typedef struct {
  FileIdx *ix;
  WalkPath path;
  wchar_t *disp;
  size_t dispCap;
  BOOL oom;
  BOOL stopped;
} WalkCtx;

static BOOL files_cancelled(void) {
  return InterlockedCompareExchange(&g_filesCancel, 0, 0) != 0;
}

static void files_walk(WalkCtx *c, int depth) {
  if (c->oom || c->stopped || depth > FILES_WALK_GUARD) return;
  if (files_cancelled()) {
    c->stopped = TRUE;
    return;
  }
  size_t mark = c->path.len;
  if (!wp_push(&c->path, L"*")) {
    c->oom = TRUE;
    return;
  }
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(c->path.w, &fd);
  wp_pop(&c->path, mark);
  if (h == INVALID_HANDLE_VALUE) return;
  int dirIdx = -1;
  do {
    if (fd.cFileName[0] == L'.' &&
        (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!wp_push(&c->path, fd.cFileName)) {
        c->oom = TRUE;
        break;
      }
      files_walk(c, depth + 1);
      wp_pop(&c->path, mark);
      if (c->oom || c->stopped) break;
    } else {
      if (dirIdx < 0) {
        if (!wp_display(&c->path, &c->disp, &c->dispCap)) {
          c->oom = TRUE;
          break;
        }
        dirIdx = idx_dir(c->ix, c->disp);
        if (dirIdx < 0) {
          c->oom = TRUE;
          break;
        }
      }
      if (!idx_add(c->ix, dirIdx, fd.cFileName)) {
        c->oom = TRUE;
        break;
      }
      /* checked in batches: a folder full of files should not pay for an
         interlocked read on every entry */
      if ((InterlockedIncrement(&g_filesScanned) & 255) == 0 && files_cancelled()) {
        c->stopped = TRUE;
        break;
      }
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

static DWORD WINAPI files_index_thread(LPVOID param) {
  (void)param;
again:;
  InterlockedExchange(&g_filesScanned, 0);
  FileIdx *ix = idx_new();
  BOOL oom = FALSE, stopped = FALSE;
  if (ix && g_filesRoot[0]) {
    WalkCtx c;
    memset(&c, 0, sizeof(c));
    c.ix = ix;
    if (wp_init(&c.path, g_filesRoot)) files_walk(&c, 0);
    else c.oom = TRUE;
    oom = c.oom;
    stopped = c.stopped;
    free(c.path.w);
    free(c.disp);
  }
  if (stopped) {
    /* a half-finished walk is worse than the index already on disk, so the
       partial result is thrown away and the previous one left alone */
    idx_free(ix);
    lstrcpynW(g_filesNote, L"Обход прерван — прежний индекс сохранён", 120);
    InterlockedExchange(&g_filesAgain, 0);
  } else if (ix) {
    BOOL saved = files_save_idx(ix);
    g_filesNote[0] = 0;
    if (oom) lstrcpynW(g_filesNote, L"Не хватило памяти — индекс неполный", 120);
    else if (!saved) lstrcpynW(g_filesNote, L"Не удалось записать индекс", 120);
    idx_publish(ix);
  }
  if (InterlockedExchange(&g_filesAgain, 0)) goto again;
  InterlockedExchange(&g_filesBusy, 0);
  if (g_hwnd) PostMessageW(g_hwnd, WM_FILES_DONE, (WPARAM)g_filesN, 0);
  return 0;
}

static void files_start_index(BOOL force) {
  files_lock_init();
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
  InterlockedExchange(&g_filesScanned, 0);
  InterlockedExchange(&g_filesCancel, 0);
  files_refresh_status();
  HANDLE th = CreateThread(NULL, 0, files_index_thread, NULL, 0, NULL);
  if (th) {
    CloseHandle(th);
    if (g_hwnd) SetTimer(g_hwnd, TIMER_FILES_TICK, 200, NULL);
  } else {
    InterlockedExchange(&g_filesBusy, 0);
    files_refresh_status();
  }
}

/* Asks the walk to give up; it notices within a few hundred files. */
static void files_stop_index(void) {
  if (!InterlockedCompareExchange(&g_filesBusy, 0, 0)) return;
  InterlockedExchange(&g_filesAgain, 0);
  InterlockedExchange(&g_filesCancel, 1);
  if (g_filesStat) SetWindowTextW(g_filesStat, L"Останавливаю обход…");
}

/* Called on the way out so a walk over a slow share cannot outlive the app. */
static void files_wait_idle(DWORD ms) {
  InterlockedExchange(&g_filesCancel, 1);
  for (DWORD i = 0; i < ms / 20 && InterlockedCompareExchange(&g_filesBusy, 0, 0); i++)
    Sleep(20);
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
    g_filesNote[0] = 0;
  }
  if ((force || changed) && g_filesRoot[0]) files_start_index(TRUE);
  files_refresh_status();
}

/* ---- search -------------------------------------------------------------- */

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
  files_lock_init();
  if (!g_filesRoot[0]) {
    lstrcpynW(out, L"Файлы\r\n\r\nУкажите папку в Настройках.", cap);
    return FALSE;
  }
  if (g_filesN == 0) files_load_idx();
  if (g_filesN == 0) {
    if (g_filesNote[0])
      _snwprintf(out, cap, L"Файлы\r\n\r\n%s", g_filesNote);
    else
      _snwprintf(out, cap,
                 L"Файлы\r\n\r\nJSON пуст для «%.80s».\r\nНажмите «Обновить JSON».",
                 g_filesRoot);
    return FALSE;
  }
  wchar_t q[200];
  lstrcpynW(q, query, 200);
  flatten_clip_line(q);
  int n = 0, total = 0;
  wchar_t links[1800] = {0};
  size_t linkLen = 0;
  files_lock();
  FileIdx *ix = g_idx;
  if (ix) {
    for (int i = 0; i < ix->n; i++) {
      const wchar_t *dir = ix->ent[i].dir < ix->dirsN ? ix->dirs[ix->ent[i].dir] : L"";
      if (!wcs_istr(ix->ent[i].name, q) && !wcs_istr(dir, q)) continue;
      total++;
      if (n >= PLM_ROWS) continue;
      wchar_t full[PLM_LINK];
      _snwprintf(full, PLM_LINK, L"%s\\%s", dir, ix->ent[i].name);
      full[PLM_LINK - 1] = 0;
      lstrcpynW(g_plmEsi[n], ix->ent[i].name, PLM_COL1);
      lstrcpynW(g_plmTp[n], dir, PLM_COL2);
      lstrcpynW(g_plmLinks[n], full, PLM_LINK);
      if (!g_plmLastLink[0]) lstrcpynW(g_plmLastLink, full, PLM_LINK);
      /* the separator has to fit too, or a long result list walks off the end */
      size_t add = wcslen(full);
      if (linkLen + (n ? 2 : 0) + add + 1 < 1800) {
        if (n) {
          links[linkLen++] = L'\r';
          links[linkLen++] = L'\n';
        }
        memcpy(links + linkLen, full, (add + 1) * sizeof(wchar_t));
        linkLen += add;
      }
      n++;
    }
  }
  files_unlock();
  g_plmCount = n;
  if (n == 0) {
    _snwprintf(out, cap, L"Файлы\r\n\r\nВ JSON нет «%.80s» (%d записей).", q, g_filesN);
    return FALSE;
  }
  if (total > n)
    _snwprintf(out, cap, L"Файлы · %d из %d найденных (всего в JSON %d)\r\n\r\n%s", n,
               total, g_filesN, links);
  else
    _snwprintf(out, cap, L"Файлы · %d из JSON\r\n\r\n%s", n, links);
  return TRUE;
}

/* ---- preferences --------------------------------------------------------- */

static void load_files_pref(void) {
  files_lock_init();
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
