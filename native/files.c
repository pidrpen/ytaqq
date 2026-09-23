/* Included from pad_extra.c — network folder index as JSON cache.

   No depth limit, no file-count limit, no index-size limit:
   - the walk uses \\?\ extended paths and a work queue instead of recursion,
     so it is bound neither by MAX_PATH nor by nesting depth nor by the stack;
   - 8 workers; leaf folders (TIFF without subfolders) whose date has not
     changed are taken from the previous JSON and not opened again;
   - entries live in an arena with de-duplicated folder names, so a million
     files cost tens of megabytes instead of hundreds;
   - the JSON is written compactly through a buffered writer into a temp file
     that replaces the old one atomically, so an interrupted refresh never
     destroys a working index. */

#define WM_FILES_DONE (WM_APP + 11)
#define WM_FILES_CHANGES (WM_APP + 15) /* lParam — FileChanges*, освобождает получатель */
#define CHG_MAX 200 /* столько строк влезает в список находок */

/* Что появилось и что перезаписали с прошлого обхода. Счётчики — все,
   список — первые CHG_MAX, новые впереди. */
typedef struct {
  int nNew, nUpd, n;
  BOOL full;
  wchar_t name[CHG_MAX][PLM_COL1];
  wchar_t dir[CHG_MAX][PLM_COL2];
  BYTE upd[CHG_MAX];
} FileChanges;

/* Уведомления о новых и изменённых файлах и полные обходы — одной галочкой */
static BOOL g_filesNotify = TRUE;
/* расписание полных обходов: день (ГГГГММДД), сколько уже было, следующий — в минутах от полуночи */
static int g_fullDay, g_fullRuns, g_fullNext;
/* За какой папкой следить: полный обход по датам и уведомления — только в ней.
   Пусто — вся папка архива. Должна лежать внутри неё. */
static wchar_t g_filesWatch[MAX_PATH];
static wchar_t g_walkWatch[MAX_PATH]; /* снимок для идущего обхода */
static HWND g_filesWatchEdit;
static volatile LONG g_filesUserStop; /* обход остановили кнопкой, а не выходом */

/* dir — это base или папка внутри неё */
static BOOL path_under(const wchar_t *dir, const wchar_t *base) {
  size_t b = wcslen(base);
  while (b > 0 && base[b - 1] == L'\\') b--;
  if (b == 0) return FALSE;
  if (_wcsnicmp(dir, base, b) != 0) return FALSE;
  return dir[b] == 0 || dir[b] == L'\\';
}
#define TIMER_FILES 8
#define TIMER_FILES_TICK 9
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
  /* дата изменения файла (та, что в Проводнике), в секундах с 1601 года;
     0 — неизвестно (индекс от старой версии). По ней видно файл, перезаписанный
     под тем же именем. */
  ULONGLONG t;
} FileEnt;

typedef struct {
  FileArena *arena;
  const wchar_t **dirs;
  ULONGLONG *mtime; /* LastWrite of that folder; 0 = unknown */
  int *subs;        /* how many child folders it had when indexed */
  int *begin;       /* first FileEnt of this dir, if grouped */
  int *count;
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
static volatile LONG g_filesRestart; /* обход брошен из-за смены папки, а не кнопкой */
static volatile LONG g_filesDirs;    /* folders enumerated, for the speed line */
static ULONGLONG g_filesT0;          /* when the current walk started */
static ULONGLONG g_filesTook;        /* how long the last one took, ms */
static long g_filesDirsDone;         /* folders in the finished index */
static int g_filesWorkers;           /* how many folders were fetched at once */
static volatile LONG g_filesIdle;
static volatile LONG g_filesCached; /* files reused from the previous JSON */
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
  free(ix->mtime);
  free(ix->subs);
  free(ix->begin);
  free(ix->count);
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
    ULONGLONG *mt = (ULONGLONG *)realloc(ix->mtime, (size_t)cap * sizeof(ULONGLONG));
    int *su = (int *)realloc(ix->subs, (size_t)cap * sizeof(int));
    if (!grown || !mt || !su) return -1;
    if (cap > ix->dirsCap) {
      memset(mt + ix->dirsCap, 0, (size_t)(cap - ix->dirsCap) * sizeof(ULONGLONG));
      memset(su + ix->dirsCap, 0, (size_t)(cap - ix->dirsCap) * sizeof(int));
    }
    ix->dirs = grown;
    ix->mtime = mt;
    ix->subs = su;
    ix->dirsCap = cap;
  }
  const wchar_t *copy = idx_intern(ix, dir);
  if (!copy) return -1;
  ix->dirs[ix->dirsN] = copy;
  ix->mtime[ix->dirsN] = 0;
  ix->subs[ix->dirsN] = 0;
  return ix->dirsN++;
}

/* Служебные файлы, которые Windows и программы раскидывают сами: в поиске
   они только мешают и раздувают индекс. Проверка стоит здесь, а не в обходе,
   чтобы старый files.json тоже читался уже без них. */
static const wchar_t *kSkipExt[] = {L"db",  L"tmp",  L"temp",    L"bak",    L"swp",
                                    L"part", L"partial", L"crdownload", L"lock"};
static const wchar_t *kSkipName[] = {L"thumbs.db", L"ehthumbs.db", L"desktop.ini",
                                     L".ds_store", L"folder.ini"};

static BOOL files_junk(const wchar_t *name) {
  if (name[0] == L'~' && name[1] == L'$') return TRUE; /* замок Office */
  size_t n = wcslen(name);
  if (n && name[n - 1] == L'~') return TRUE;
  for (int i = 0; i < (int)(sizeof(kSkipName) / sizeof(kSkipName[0])); i++)
    if (_wcsicmp(name, kSkipName[i]) == 0) return TRUE;
  const wchar_t *dot = wcsrchr(name, L'.');
  if (!dot || !dot[1]) return FALSE;
  for (int i = 0; i < (int)(sizeof(kSkipExt) / sizeof(kSkipExt[0])); i++)
    if (_wcsicmp(dot + 1, kSkipExt[i]) == 0) return TRUE;
  return FALSE;
}

static BOOL idx_add(FileIdx *ix, int dirIdx, const wchar_t *name, ULONGLONG t) {
  if (dirIdx < 0 || !name || !name[0]) return FALSE;
  /* не ошибка, а «этот файл нам не нужен» — вызывающий считает FALSE сбоем */
  if (files_junk(name)) return TRUE;
  if (ix->n >= ix->cap) {
    int cap = ix->cap ? ix->cap * 2 : 16384;
    FileEnt *grown = (FileEnt *)realloc(ix->ent, (size_t)cap * sizeof(FileEnt));
    if (!grown) return FALSE;
    ix->ent = grown;
    ix->cap = cap;
  }
  const wchar_t *copy = idx_intern(ix, name);
  if (!copy) return FALSE;
  ix->ent[ix->n].dir = dirIdx;
  ix->ent[ix->n].name = copy;
  ix->ent[ix->n].t = t;
  ix->n++;
  return TRUE;
}

/* Move everything from src into dst. The string arena is spliced rather than
   copied: its blocks are heap pointers that stay valid, so a merge costs one
   pointer walk instead of re-interning every name. Two threads never meet the
   same folder, so no de-duplication is needed here. */
static BOOL idx_merge(FileIdx *dst, FileIdx *src) {
  if (!src || (!src->n && !src->dirsN)) return TRUE;
  if (dst->dirsN + src->dirsN > dst->dirsCap) {
    int cap = dst->dirsCap ? dst->dirsCap : 256;
    while (cap < dst->dirsN + src->dirsN) cap *= 2;
    const wchar_t **grown =
        (const wchar_t **)realloc((void *)dst->dirs, (size_t)cap * sizeof(wchar_t *));
    ULONGLONG *mt = (ULONGLONG *)realloc(dst->mtime, (size_t)cap * sizeof(ULONGLONG));
    int *su = (int *)realloc(dst->subs, (size_t)cap * sizeof(int));
    if (!grown || !mt || !su) return FALSE;
    if (cap > dst->dirsCap) {
      memset(mt + dst->dirsCap, 0, (size_t)(cap - dst->dirsCap) * sizeof(ULONGLONG));
      memset(su + dst->dirsCap, 0, (size_t)(cap - dst->dirsCap) * sizeof(int));
    }
    dst->dirs = grown;
    dst->mtime = mt;
    dst->subs = su;
    dst->dirsCap = cap;
  }
  if (dst->n + src->n > dst->cap) {
    int cap = dst->cap ? dst->cap : 4096;
    while (cap < dst->n + src->n) cap *= 2;
    FileEnt *grown = (FileEnt *)realloc(dst->ent, (size_t)cap * sizeof(FileEnt));
    if (!grown) return FALSE;
    dst->ent = grown;
    dst->cap = cap;
  }
  if (src->arena) {
    FileArena *last = src->arena;
    while (last->next) last = last->next;
    last->next = dst->arena;
    dst->arena = src->arena;
    src->arena = NULL;
  }
  int base = dst->dirsN;
  for (int i = 0; i < src->dirsN; i++) {
    dst->dirs[base + i] = src->dirs[i];
    dst->mtime[base + i] = src->mtime ? src->mtime[i] : 0;
    dst->subs[base + i] = src->subs ? src->subs[i] : 0;
  }
  dst->dirsN = base + src->dirsN;
  for (int i = 0; i < src->n; i++) {
    dst->ent[dst->n].dir = base + src->ent[i].dir;
    dst->ent[dst->n].name = src->ent[i].name;
    dst->ent[dst->n].t = src->ent[i].t;
    dst->n++;
  }
  return TRUE;
}

static void idx_index_ents(FileIdx *ix) {
  if (!ix || ix->dirsN <= 0) return;
  free(ix->begin);
  free(ix->count);
  ix->begin = (int *)malloc((size_t)ix->dirsN * sizeof(int));
  ix->count = (int *)calloc((size_t)ix->dirsN, sizeof(int));
  if (!ix->begin || !ix->count) {
    free(ix->begin);
    free(ix->count);
    ix->begin = ix->count = NULL;
    return;
  }
  for (int i = 0; i < ix->dirsN; i++) ix->begin[i] = -1;
  for (int i = 0; i < ix->n; i++) {
    int d = ix->ent[i].dir;
    if (d < 0 || d >= ix->dirsN) continue;
    if (ix->begin[d] < 0) ix->begin[d] = i;
    ix->count[d]++;
  }
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
  /* сколько индексу лет: свежий после перезапуска (обновление программы,
     перезагрузка) заново не обходим — ждём, пока ему исполнится час */
  FILETIME nowFt;
  GetSystemTimeAsFileTime(&nowFt);
  ULONGLONG now = ((ULONGLONG)nowFt.dwHighDateTime << 32) | nowFt.dwLowDateTime;
  ULONGLONG wr = ((ULONGLONG)ad.ftLastWriteTime.dwHighDateTime << 32) | ad.ftLastWriteTime.dwLowDateTime;
  if (now > wr && now - wr < 3600ULL * 10000000ULL) {
    ULONGLONG ageMs = (now - wr) / 10000ULL, tick = GetTickCount64();
    g_filesAt = tick > ageMs ? tick - ageMs : 1;
  }
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
  if (busy) {
    long done = (long)InterlockedCompareExchange(&g_filesScanned, 0, 0);
    long dirs = (long)InterlockedCompareExchange(&g_filesDirs, 0, 0);
    ULONGLONG now = GetTickCount64();
    /* окно узкое — одна скорость, сглаженная, без «потоков/ждут/п/с» */
    static ULONGLONG s_t0, s_prevT;
    static long s_prevDone, s_rateF;
    if (g_filesT0 != s_t0) {
      s_t0 = g_filesT0;
      s_prevT = now;
      s_prevDone = done;
      s_rateF = 0;
    }
    if (now > s_prevT + 1500) {
      ULONGLONG dt = now - s_prevT;
      long inst = (long)((done - s_prevDone) * 1000ull / dt);
      s_rateF = s_rateF > 0 ? (s_rateF * 2 + inst) / 3 : inst;
      s_prevT = now;
      s_prevDone = done;
    }
    long cached = (long)InterlockedCompareExchange(&g_filesCached, 0, 0);
    if (s_rateF > 0 && cached)
      _snwprintf(t, 200, L"%ld файлов, %ld папок · %ld/с · кэш %ld", done, dirs, s_rateF, cached);
    else if (s_rateF > 0)
      _snwprintf(t, 200, L"%ld файлов, %ld папок · %ld/с", done, dirs, s_rateF);
    else
      _snwprintf(t, 200, L"%ld файлов, %ld папок…", done, dirs);
  }
  else if (!g_filesRoot[0])
    lstrcpynW(t, L"Индекс: укажите папку", 200);
  else if (g_filesNote[0])
    lstrcpynW(t, g_filesNote, 200);
  else if (g_filesN <= 0)
    lstrcpynW(t, L"JSON пуст — «Обновить JSON»", 200);
  else if (g_filesWhenOk && g_filesTook)
    _snwprintf(t, 200, L"%d файлов, %ld папок · %lu:%02lu",
               g_filesN, g_filesDirsDone,
               (unsigned long)(g_filesTook / 60000ull),
               (unsigned long)((g_filesTook / 1000ull) % 60ull));
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
  if (!o->ok) return;
  if (!s) s = "";
  int n = (int)strlen(s);
  /* worst case every byte becomes \u00xx plus quotes */
  int worst = n * 6 + 2;
  if (worst >= o->cap) {
    json_put(o, "\"");
    for (; *s; s++) {
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
    return;
  }
  if (o->used + worst > o->cap && !json_flush(o)) return;
  char *d = o->buf + o->used;
  *d++ = '"';
  for (int i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') {
      *d++ = '\\';
      *d++ = (char)c;
    } else if (c == '\n') {
      *d++ = '\\';
      *d++ = 'n';
    } else if (c == '\r') {
      *d++ = '\\';
      *d++ = 'r';
    } else if (c == '\t') {
      *d++ = '\\';
      *d++ = 't';
    } else if (c < 32) {
      static const char hx[] = "0123456789abcdef";
      d[0] = '\\';
      d[1] = 'u';
      d[2] = '0';
      d[3] = '0';
      d[4] = hx[c >> 4];
      d[5] = hx[c & 15];
      d += 6;
    } else {
      *d++ = (char)c;
    }
  }
  *d++ = '"';
  o->used = (int)(d - o->buf);
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
  o.cap = 256 * 1024;
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
  json_put(&o, "],\"S\":[");
  for (int i = 0; i < ix->dirsN; i++) {
    char b[32];
    snprintf(b, sizeof(b), "%s%d", i ? "," : "", ix->subs ? ix->subs[i] : 0);
    json_put(&o, b);
  }
  json_put(&o, "],\"Th\":[");
  for (int i = 0; i < ix->dirsN; i++) {
    char b[32];
    unsigned hi = ix->mtime ? (unsigned)(ix->mtime[i] >> 32) : 0;
    snprintf(b, sizeof(b), "%s%u", i ? "," : "", hi);
    json_put(&o, b);
  }
  json_put(&o, "],\"Tl\":[");
  for (int i = 0; i < ix->dirsN; i++) {
    char b[32];
    unsigned lo = ix->mtime ? (unsigned)ix->mtime[i] : 0;
    snprintf(b, sizeof(b), "%s%u", i ? "," : "", lo);
    json_put(&o, b);
  }
  json_put(&o, "],\"Data\":[");
  for (int i = 0; i < ix->n; i++) {
    char head[32];
    snprintf(head, sizeof(head), "%s{\"D\":%d,\"F\":", i ? "," : "", ix->ent[i].dir);
    json_put(&o, head);
    json_put_w(&o, ix->ent[i].name);
    if (ix->ent[i].t) {
      char tb[32];
      snprintf(tb, sizeof(tb), ",\"T\":%llu", (unsigned long long)ix->ent[i].t);
      json_put(&o, tb);
    }
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

/* Compact form: {"Path":…,"Dirs":[…],"S":[…],"Th":[…],"Tl":[…],"Data":[…]} */
static BOOL json_u32_list(const char *buf, const char *key, unsigned *out, int n) {
  char pat[16];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = strstr(buf, pat);
  if (!p) return FALSE;
  p = strchr(p, '[');
  if (!p) return FALSE;
  p++;
  for (int i = 0; i < n; i++) {
    p = json_skip(p);
    if (*p < '0' || *p > '9') return i > 0;
    unsigned v = 0;
    while (*p >= '0' && *p <= '9') {
      v = v * 10u + (unsigned)(*p - '0');
      p++;
    }
    out[i] = v;
    p = json_skip(p);
  }
  return TRUE;
}

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
  if (ix->dirsN > 0 && ix->mtime && ix->subs) {
    unsigned *hi = (unsigned *)calloc((size_t)ix->dirsN, sizeof(unsigned));
    unsigned *lo = (unsigned *)calloc((size_t)ix->dirsN, sizeof(unsigned));
    unsigned *su = (unsigned *)calloc((size_t)ix->dirsN, sizeof(unsigned));
    if (hi && lo && su && json_u32_list(buf, "Th", hi, ix->dirsN) &&
        json_u32_list(buf, "Tl", lo, ix->dirsN)) {
      json_u32_list(buf, "S", su, ix->dirsN);
      for (int i = 0; i < ix->dirsN; i++) {
        ix->mtime[i] = ((ULONGLONG)hi[i] << 32) | (ULONGLONG)lo[i];
        ix->subs[i] = (int)su[i];
      }
    }
    free(hi);
    free(lo);
    free(su);
  }
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
    ULONGLONG t = 0;
    v = json_key(obj, end, "T");
    if (v) t = (ULONGLONG)strtoull(v, NULL, 10);
    if (name[0] && d >= 0 && d < ix->dirsN) idx_add(ix, d, name, t);
    data = end + 1;
  }
  (void)idxRoot;
  idx_index_ents(ix);
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
      idx_add(ix, idx_dir(ix, dir), name, 0);
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

static BOOL wp_init(WalkPath *p, const wchar_t *root) {
  /* \\?\ отключает кэш метаданных SMB — на 40 тысячах папок это минуты.
     Обычный путь, пока он короткий; длинный префикс только если уже дан. */
  memset(p, 0, sizeof(*p));
  p->shown = L"";
  p->skip = 0;
  size_t n = wcslen(root);
  /* длиннее 32767 знаков путей в Windows не бывает; без этой границы
     компилятор считает, что размер копирования может переполниться */
  if (n > 32767) return FALSE;
  if (!wp_reserve(p, n + 2)) return FALSE;
  memcpy(p->w, root, (n + 1) * sizeof(wchar_t));
  p->len = n;
  while (p->len > 0 && p->w[p->len - 1] == L'\\') p->w[--p->len] = 0;
  return TRUE;
}

/* Обычный FindFirst, без CreateFile на каждую папку и без \\?\ — иначе
   SMB-кэш Windows молчит и 40 тысяч каталогов идут на сервер дважды.
   8 потоков: 32 на одном диске Linux только дерутся. Занятый берёт
   свежую папку (вглубь), простаивающий — старую (соседа). */
typedef struct {
  wchar_t **item;
  ULONGLONG *time;
  int head, n, cap;
  int active;
  BOOL oom;
  CRITICAL_SECTION cs;
  CONDITION_VARIABLE cv;
  size_t skip;
  const wchar_t *shown;
  FileIdx *old; /* previous JSON, borrowed for the walk */
  int *map;
  int mapCap;
} WalkQ;

static BOOL files_cancelled(void) {
  return InterlockedCompareExchange(&g_filesCancel, 0, 0) != 0;
}

static BOOL wq_grow(wchar_t **buf, size_t *cap, size_t need) {
  if (*cap >= need) return TRUE;
  size_t c = *cap ? *cap : 1024;
  while (c < need) c *= 2;
  wchar_t *grown = (wchar_t *)realloc(*buf, c * sizeof(wchar_t));
  if (!grown) return FALSE;
  *buf = grown;
  *cap = c;
  return TRUE;
}

static BOOL wq_push_owned(WalkQ *q, wchar_t *owned, ULONGLONG mtime) {
  if (!owned) return FALSE;
  if (q->head && q->head + q->n >= q->cap) {
    memmove(q->item, q->item + q->head, (size_t)q->n * sizeof(wchar_t *));
    memmove(q->time, q->time + q->head, (size_t)q->n * sizeof(ULONGLONG));
    q->head = 0;
  }
  if (q->head + q->n >= q->cap) {
    int cap = q->cap ? q->cap * 2 : 4096;
    wchar_t **grown = (wchar_t **)realloc(q->item, (size_t)cap * sizeof(wchar_t *));
    ULONGLONG *tm = (ULONGLONG *)realloc(q->time, (size_t)cap * sizeof(ULONGLONG));
    if (!grown || !tm) {
      free(owned);
      return FALSE;
    }
    q->item = grown;
    q->time = tm;
    q->cap = cap;
  }
  q->item[q->head + q->n] = owned;
  q->time[q->head + q->n] = mtime;
  q->n++;
  return TRUE;
}

static wchar_t *wq_pop_tail(WalkQ *q, ULONGLONG *mtime) {
  if (q->n <= 0) return NULL;
  q->n--;
  wchar_t *dir = q->item[q->head + q->n];
  if (mtime) *mtime = q->time[q->head + q->n];
  q->item[q->head + q->n] = NULL;
  if (q->n == 0) q->head = 0;
  return dir;
}

static wchar_t *wq_pop_head(WalkQ *q, ULONGLONG *mtime) {
  if (q->n <= 0) return NULL;
  wchar_t *dir = q->item[q->head];
  if (mtime) *mtime = q->time[q->head];
  q->item[q->head] = NULL;
  q->head++;
  q->n--;
  if (q->n == 0) q->head = 0;
  return dir;
}

typedef struct {
  WalkQ *q;
  FileIdx *ix;
  BOOL oom;
  BOOL stopped;
  wchar_t *pat;
  size_t patCap;
} WalkWorker;

static BOOL walk_entry(WalkWorker *w, const wchar_t *dir, size_t dl, const wchar_t *name,
                       size_t nl, DWORD attr, ULONGLONG childTime, int *dirIdx) {
  WalkQ *q = w->q;
  if (nl == 0) return TRUE;
  if (name[0] == L'.' && (nl == 1 || (nl == 2 && name[1] == L'.'))) return TRUE;
  if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return TRUE;

  wchar_t stackn[280];
  wchar_t *nm = stackn;
  if (nl >= 280) {
    nm = (wchar_t *)malloc((nl + 1) * sizeof(wchar_t));
    if (!nm) {
      w->oom = TRUE;
      return FALSE;
    }
  }
  memcpy(nm, name, nl * sizeof(wchar_t));
  nm[nl] = 0;

  BOOL ok = TRUE;
  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    wchar_t *owned = (wchar_t *)malloc((dl + nl + 2) * sizeof(wchar_t));
    if (!owned) {
      w->oom = TRUE;
      ok = FALSE;
    } else {
      memcpy(owned, dir, dl * sizeof(wchar_t));
      owned[dl] = L'\\';
      memcpy(owned + dl + 1, nm, (nl + 1) * sizeof(wchar_t));
      EnterCriticalSection(&q->cs);
      BOOL pushed = wq_push_owned(q, owned, childTime);
      if (pushed) WakeConditionVariable(&q->cv);
      else q->oom = TRUE;
      LeaveCriticalSection(&q->cs);
      if (!pushed) {
        w->oom = TRUE;
        ok = FALSE;
      }
    }
  } else {
    if (*dirIdx < 0) {
      *dirIdx = idx_dir(w->ix, dir);
      if (*dirIdx < 0) {
        w->oom = TRUE;
        ok = FALSE;
      }
    }
    /* дату файла Windows отдаёт вместе с именем — лишних обращений к сети нет */
    if (ok && !idx_add(w->ix, *dirIdx, nm, childTime / 10000000ULL)) {
      w->oom = TRUE;
      ok = FALSE;
    }
    if (ok && (InterlockedIncrement(&g_filesScanned) & 255) == 0 && files_cancelled()) {
      w->stopped = TRUE;
      ok = FALSE;
    }
  }
  if (nm != stackn) free(nm);
  return ok;
}

static unsigned walk_hash(const wchar_t *s) {
  unsigned h = 2166136261u;
  for (; *s; s++) {
    wchar_t c = *s;
    if (c >= L'A' && c <= L'Z') c += 32;
    h ^= (unsigned)c;
    h *= 16777619u;
  }
  return h;
}

static int walk_lookup(WalkQ *q, const wchar_t *path) {
  if (!q->old || !q->map || q->mapCap <= 0) return -1;
  unsigned m = (unsigned)q->mapCap - 1u;
  unsigned h = walk_hash(path) & m;
  for (int i = 0; i < q->mapCap; i++) {
    int d = q->map[h];
    if (d < 0) return -1;
    if (!_wcsicmp(q->old->dirs[d], path)) return d;
    h = (h + 1u) & m;
  }
  return -1;
}

/* TIFF-папка без подпапок: если дата та же, имена уже в JSON — не открываем. */
static volatile LONG g_filesFull; /* полный обход: читаем все папки подряд */

static BOOL walk_reuse(WalkWorker *w, const wchar_t *dir, ULONGLONG mt) {
  if (!mt) return FALSE;
  /* Если файл перезаписали под тем же именем, дата папки не меняется,
     и быстрый обход такую замену не увидит. Полный — заглядывает везде. */
  if (InterlockedCompareExchange(&g_filesFull, 0, 0) &&
      (!g_walkWatch[0] || path_under(dir, g_walkWatch)))
    return FALSE;
  WalkQ *q = w->q;
  FileIdx *old = q->old;
  if (!old || !old->mtime || !old->subs || !old->begin || !old->count) return FALSE;
  int od = walk_lookup(q, dir);
  if (od < 0 || old->mtime[od] != mt || old->subs[od] != 0) return FALSE;
  int nd = idx_dir(w->ix, dir);
  if (nd < 0) {
    w->oom = TRUE;
    return TRUE;
  }
  w->ix->mtime[nd] = mt;
  w->ix->subs[nd] = 0;
  int b = old->begin[od], c = old->count[od];
  if (b >= 0 && c > 0) {
    for (int i = 0; i < c; i++) {
      if (!idx_add(w->ix, nd, old->ent[b + i].name, old->ent[b + i].t)) {
        w->oom = TRUE;
        return TRUE;
      }
    }
    InterlockedExchangeAdd(&g_filesScanned, c);
    InterlockedExchangeAdd(&g_filesCached, c);
  }
  InterlockedIncrement(&g_filesDirs);
  return TRUE;
}

/* FindFirst path: обычный, \\?\ только если путь длиннее 240. */
static BOOL walk_pat(WalkWorker *w, const wchar_t *dir, size_t dl) {
  BOOL already = dl >= 4 && dir[0] == L'\\' && dir[1] == L'\\' && dir[2] == L'?' && dir[3] == L'\\';
  BOOL unc = dl >= 2 && dir[0] == L'\\' && dir[1] == L'\\' && !already;
  size_t extra = 0;
  if (!already && dl >= 240) extra = unc ? 6 : 4;
  if (!wq_grow(&w->pat, &w->patCap, extra + dl + 4)) return FALSE;
  wchar_t *p = w->pat;
  size_t o = 0;
  if (extra && unc) {
    memcpy(p, L"\\\\?\\UNC\\", 8 * sizeof(wchar_t));
    memcpy(p + 8, dir + 2, (dl - 2) * sizeof(wchar_t));
    o = 6 + dl;
  } else if (extra) {
    memcpy(p, L"\\\\?\\", 4 * sizeof(wchar_t));
    memcpy(p + 4, dir, dl * sizeof(wchar_t));
    o = 4 + dl;
  } else {
    memcpy(p, dir, dl * sizeof(wchar_t));
    o = dl;
  }
  p[o] = L'\\';
  p[o + 1] = L'*';
  p[o + 2] = 0;
  return TRUE;
}

static void walk_dir_find(WalkWorker *w, const wchar_t *dir, size_t dl, ULONGLONG mt) {
  if (walk_reuse(w, dir, mt)) return;
  if (!walk_pat(w, dir, dl)) {
    w->oom = TRUE;
    return;
  }
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(w->pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL,
                              FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) h = FindFirstFileW(w->pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  InterlockedIncrement(&g_filesDirs);
  int dirIdx = -1, subs = 0;
  do {
    size_t nl = wcslen(fd.cFileName);
    DWORD attr = fd.dwFileAttributes;
    ULONGLONG child = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) |
                      (ULONGLONG)fd.ftLastWriteTime.dwLowDateTime;
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) &&
        !(fd.cFileName[0] == L'.' &&
          (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))))
      subs++;
    if (!walk_entry(w, dir, dl, fd.cFileName, nl, attr, child, &dirIdx)) break;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  if (dirIdx >= 0) {
    w->ix->mtime[dirIdx] = mt;
    w->ix->subs[dirIdx] = subs;
  }
}

static DWORD WINAPI files_walk_worker(LPVOID param) {
  WalkWorker *w = (WalkWorker *)param;
  WalkQ *q = w->q;
  for (;;) {
    BOOL stole = FALSE;
    ULONGLONG mt = 0;
    EnterCriticalSection(&q->cs);
    if (q->n == 0 && q->active > 0) {
      stole = TRUE;
      InterlockedIncrement(&g_filesIdle);
      while (q->n == 0 && q->active > 0) SleepConditionVariableCS(&q->cv, &q->cs, 50);
      InterlockedDecrement(&g_filesIdle);
    }
    if (q->n == 0) {
      LeaveCriticalSection(&q->cs);
      break;
    }
    wchar_t *dir = stole ? wq_pop_head(q, &mt) : wq_pop_tail(q, &mt);
    q->active++;
    LeaveCriticalSection(&q->cs);

    if (dir && !files_cancelled()) walk_dir_find(w, dir, wcslen(dir), mt);
    else if (files_cancelled()) w->stopped = TRUE;
    free(dir);

    EnterCriticalSection(&q->cs);
    q->active--;
    if (q->n == 0 && q->active == 0) WakeAllConditionVariable(&q->cv);
    LeaveCriticalSection(&q->cs);
    if (w->oom || w->stopped) break;
  }
  free(w->pat);
  w->pat = NULL;
  return 0;
}

static int walk_worker_count(const wchar_t *root) {
  BOOL net = (root[0] == L'\\' && root[1] == L'\\') ||
             _wcsnicmp(root, L"\\\\?\\UNC\\", 8) == 0;
  if (!net) {
    const wchar_t *p = root;
    if (_wcsnicmp(p, L"\\\\?\\", 4) == 0) p += 4;
    if (p[0] && p[1] == L':') {
      wchar_t r[4] = {p[0], L':', L'\\', 0};
      UINT t = GetDriveTypeW(r);
      if (t == DRIVE_REMOTE) net = TRUE;
    }
  }
  if (net) return 8;
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  int cpus = (int)si.dwNumberOfProcessors;
  if (cpus < 1) cpus = 1;
  int n = cpus;
  if (n > 4) n = 4;
  if (n < 2) n = 2;
  return n;
}

static void files_walk_all(const wchar_t *root, FileIdx *out, BOOL *oom, BOOL *stopped) {
  WalkPath p;
  if (!wp_init(&p, root)) {
    *oom = TRUE;
    return;
  }
  WalkQ q;
  memset(&q, 0, sizeof(q));
  InitializeCriticalSection(&q.cs);
  InitializeConditionVariable(&q.cv);
  q.skip = p.skip;
  q.shown = p.shown;
  files_lock();
  q.old = g_idx;
  if (q.old && q.old->mtime && q.old->dirsN > 0) {
    if (!q.old->begin) idx_index_ents(q.old);
    int cap = 1;
    while (cap < q.old->dirsN * 2) cap *= 2;
    q.map = (int *)malloc((size_t)cap * sizeof(int));
    if (q.map) {
      q.mapCap = cap;
      for (int i = 0; i < cap; i++) q.map[i] = -1;
      unsigned mask = (unsigned)cap - 1u;
      for (int d = 0; d < q.old->dirsN; d++) {
        unsigned h = walk_hash(q.old->dirs[d]) & mask;
        while (q.map[h] >= 0) h = (h + 1u) & mask;
        q.map[h] = d;
      }
    }
  }
  files_unlock();
  BOOL ok = FALSE;
  {
    size_t need = wcslen(p.w) + 1;
    wchar_t *rootCopy = (wchar_t *)malloc(need * sizeof(wchar_t));
    if (rootCopy) {
      memcpy(rootCopy, p.w, need * sizeof(wchar_t));
      ok = wq_push_owned(&q, rootCopy, 0);
    }
  }
  free(p.w);

  int n = walk_worker_count(root);
  g_filesWorkers = n;
  WalkWorker *w = ok ? (WalkWorker *)calloc((size_t)n, sizeof(WalkWorker)) : NULL;
  HANDLE *th = w ? (HANDLE *)calloc((size_t)n, sizeof(HANDLE)) : NULL;
  int started = 0;
  if (th) {
    for (int i = 0; i < n; i++) {
      w[i].q = &q;
      w[i].ix = idx_new();
      if (!w[i].ix) break;
      th[started] = CreateThread(NULL, 0, files_walk_worker, &w[i], 0, NULL);
      if (!th[started]) {
        idx_free(w[i].ix);
        w[i].ix = NULL;
        break;
      }
      started++;
    }
  }
  if (!started) {
    /* not a single worker could start — walk it on this thread rather than
       come back with nothing */
    if (w && (w[0].ix || (w[0].ix = idx_new()) != NULL)) {
      w[0].q = &q;
      files_walk_worker(&w[0]);
    } else {
      *oom = TRUE;
    }
  } else {
    WaitForMultipleObjects((DWORD)started, th, TRUE, INFINITE);
    for (int i = 0; i < started; i++) CloseHandle(th[i]);
  }
  if (w) {
    for (int i = 0; i < n; i++) {
      if (!w[i].ix) continue;
      if (w[i].oom) *oom = TRUE;
      if (w[i].stopped) *stopped = TRUE;
      if (!idx_merge(out, w[i].ix)) *oom = TRUE;
      idx_free(w[i].ix);
    }
  }
  if (q.oom || !ok) *oom = TRUE;
  for (int i = 0; i < q.n; i++) free(q.item[q.head + i]);
  free(q.item);
  free(q.time);
  free(q.map);
  free(w);
  free(th);
  DeleteCriticalSection(&q.cs);
}

/* ---- что изменилось с прошлого обхода ------------------------------------ */
static unsigned chg_key(unsigned dirHash, const wchar_t *name) {
  unsigned h = dirHash ^ (unsigned)L'\\';
  h *= 16777619u;
  for (; *name; name++) {
    wchar_t c = *name;
    if (c >= L'A' && c <= L'Z') c += 32;
    h ^= (unsigned)c;
    h *= 16777619u;
  }
  return h;
}

static void chg_put(FileChanges *r, const FileIdx *ix, int i, BOOL upd) {
  if (upd) r->nUpd++;
  else r->nNew++;
  if (r->n >= CHG_MAX) return;
  const FileEnt *e = &ix->ent[i];
  lstrcpynW(r->name[r->n], e->name, PLM_COL1);
  lstrcpynW(r->dir[r->n], e->dir < ix->dirsN ? ix->dirs[e->dir] : L"", PLM_COL2);
  r->upd[r->n] = (BYTE)upd;
  r->n++;
}

/* Новый — такого пути раньше не было. Обновлён — путь тот же, а дата другая.
   Именно «другая», а не «новее»: при копировании Windows сохраняет файлу старую
   дату, и вернувшаяся прежняя версия чертежа — тоже изменение. Дату 0 (индекс от
   старой версии) не сравниваем: иначе первый же обход объявил бы всё обновлённым. */
static FileChanges *files_diff(const FileIdx *old, const FileIdx *fresh) {
  if (!old || old->n <= 0 || !fresh || fresh->n <= 0) return NULL;
  int cap = 1;
  while (cap < old->n * 2) cap <<= 1;
  unsigned mask = (unsigned)cap - 1u;
  int *tab = (int *)malloc((size_t)cap * sizeof(int));
  unsigned *odh = (unsigned *)malloc((size_t)(old->dirsN > 0 ? old->dirsN : 1) * sizeof(unsigned));
  unsigned *ndh = (unsigned *)malloc((size_t)(fresh->dirsN > 0 ? fresh->dirsN : 1) * sizeof(unsigned));
  FileChanges *r = (FileChanges *)calloc(1, sizeof(FileChanges));
  if (!tab || !odh || !ndh || !r) {
    free(tab);
    free(odh);
    free(ndh);
    free(r);
    return NULL;
  }
  for (int i = 0; i < cap; i++) tab[i] = -1;
  for (int d = 0; d < old->dirsN; d++) odh[d] = walk_hash(old->dirs[d]);
  for (int d = 0; d < fresh->dirsN; d++) ndh[d] = walk_hash(fresh->dirs[d]);
  for (int i = 0; i < old->n; i++) {
    int d = old->ent[i].dir;
    if (d < 0 || d >= old->dirsN) continue;
    unsigned h = chg_key(odh[d], old->ent[i].name) & mask;
    while (tab[h] >= 0) h = (h + 1u) & mask;
    tab[h] = i;
  }
  /* два прохода: сначала новые, потом обновлённые — в списке новые идут первыми */
  for (int pass = 0; pass < 2; pass++) {
    for (int j = 0; j < fresh->n; j++) {
      int d = fresh->ent[j].dir;
      if (d < 0 || d >= fresh->dirsN) continue;
      const wchar_t *nm = fresh->ent[j].name;
      if (g_walkWatch[0] && !path_under(fresh->dirs[d], g_walkWatch)) continue;
      unsigned h = chg_key(ndh[d], nm) & mask;
      int found = -1;
      while (tab[h] >= 0) {
        int o = tab[h];
        if (_wcsicmp(old->ent[o].name, nm) == 0 &&
            _wcsicmp(old->dirs[old->ent[o].dir], fresh->dirs[d]) == 0) {
          found = o;
          break;
        }
        h = (h + 1u) & mask;
      }
      if (pass == 0 && found < 0) chg_put(r, fresh, j, FALSE);
      if (pass == 1 && found >= 0 && old->ent[found].t && fresh->ent[j].t &&
          old->ent[found].t != fresh->ent[j].t)
        chg_put(r, fresh, j, TRUE);
    }
  }
  free(tab);
  free(odh);
  free(ndh);
  if (r->nNew + r->nUpd == 0) {
    free(r);
    return NULL;
  }
  return r;
}

static void files_post_changes(FileChanges *chg) {
  if (chg && (!g_hwnd || !PostMessageW(g_hwnd, WM_FILES_CHANGES, 0, (LPARAM)chg))) free(chg);
}

static DWORD WINAPI files_index_thread(LPVOID param) {
  (void)param;
  FileChanges *pend = NULL; /* отдаём после WM_FILES_DONE, чтобы его строка не затёрла нашу */
  BOOL lastOk = FALSE;
again:;
  InterlockedExchange(&g_filesCancel, 0); /* иначе повтор оборвётся сразу же */
  InterlockedExchange(&g_filesScanned, 0);
  InterlockedExchange(&g_filesCached, 0);
  InterlockedExchange(&g_filesDirs, 0);
  InterlockedExchange(&g_filesIdle, 0);
  g_filesT0 = GetTickCount64();
  FileIdx *ix = idx_new();
  BOOL oom = FALSE, stopped = FALSE;
  if (ix && g_filesRoot[0]) files_walk_all(g_filesRoot, ix, &oom, &stopped);
  g_filesTook = GetTickCount64() - g_filesT0;
  g_filesDirsDone = (long)InterlockedCompareExchange(&g_filesDirs, 0, 0);
  if (stopped) {
    /* a half-finished walk is worse than the index already on disk, so the
       partial result is thrown away and the previous one left alone */
    idx_free(ix);
    if (InterlockedExchange(&g_filesRestart, 0)) {
      files_clear();
      g_filesNote[0] = 0;
      InterlockedExchange(&g_filesAgain, 1);
    } else {
      lstrcpynW(g_filesNote, L"Обход прерван — прежний индекс сохранён", 120);
      InterlockedExchange(&g_filesAgain, 0);
      /* остановили — следующий плановый через час, а не через пять минут */
      g_filesAt = GetTickCount64();
    }
    lastOk = FALSE;
  } else if (ix) {
    BOOL saved = files_save_idx(ix);
    g_filesNote[0] = 0;
    if (oom) lstrcpynW(g_filesNote, L"Не хватило памяти — индекс неполный", 120);
    else if (!saved) lstrcpynW(g_filesNote, L"Не удалось записать индекс", 120);
    /* сравниваем с тем, что было, до того как старый индекс уйдёт; неполный
       обход (не хватило памяти) с прошлым не сравниваем */
    FileChanges *chg = NULL;
    if (g_filesNotify && !oom) {
      files_lock();
      chg = files_diff(g_idx, ix);
      files_unlock();
      if (chg) chg->full = InterlockedCompareExchange(&g_filesFull, 0, 0) != 0;
    }
    idx_publish(ix);
    if (chg) {
      files_post_changes(pend);
      pend = chg;
    }
    lastOk = TRUE;
  }
  if (InterlockedExchange(&g_filesAgain, 0)) goto again;
  /* полный обход засчитывается, если дошёл до конца или его остановили
     кнопкой. Оборванный выходом (обновление, перезагрузка) повторится. */
  BOOL wasFull = InterlockedExchange(&g_filesFull, 0) != 0;
  BOOL userStop = InterlockedExchange(&g_filesUserStop, 0) != 0;
  InterlockedExchange(&g_filesBusy, 0);
  if (g_hwnd)
    PostMessageW(g_hwnd, WM_FILES_DONE, (WPARAM)g_filesN, (LPARAM)(wasFull && (lastOk || userStop)));
  files_post_changes(pend);
  return 0;
}

static void files_start_index(BOOL force) {
  files_lock_init();
  if (!g_filesRoot[0]) {
    files_refresh_status();
    return;
  }
  /* раз в час, считая от последнего обхода, — и через перезапуск тоже:
     время берётся по дате files.json */
  if (!force && g_filesAt && GetTickCount64() - g_filesAt < 3600000ULL)
    return;
  if (InterlockedCompareExchange(&g_filesBusy, 1, 0) != 0) {
    InterlockedExchange(&g_filesAgain, 1);
    files_refresh_status();
    return;
  }
  InterlockedExchange(&g_filesScanned, 0);
  InterlockedExchange(&g_filesCancel, 0);
  InterlockedExchange(&g_filesUserStop, 0);
  if (g_filesWatch[0] && path_under(g_filesWatch, g_filesRoot))
    lstrcpynW(g_walkWatch, g_filesWatch, MAX_PATH);
  else
    g_walkWatch[0] = 0;
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

/* ---- полный обход дважды в день ------------------------------------------- */
/* Быстрый обход раз в час видит новые и удалённые файлы, но не перезаписанные под
   тем же именем: у такой папки дата не меняется, и он в неё не заходит. Дважды в
   день идёт полный — в случайное время между 8:00 и 16:30, с разницей четыре часа.
   Случайное — чтобы компьютеры всех, у кого стоит программа, не шли на сетевой
   диск разом. */
#define FULL_OPEN (8 * 60)
#define FULL_CLOSE (16 * 60 + 30)
#define FULL_GAP (4 * 60)

static unsigned files_rand(unsigned range) {
  if (range == 0) return 0;
  unsigned x = (unsigned)GetTickCount64() ^ (GetCurrentProcessId() * 2654435761u);
  x ^= x >> 13;
  x *= 0x5bd1e995u;
  x ^= x >> 15;
  return x % range;
}

static BOOL files_start_full(void) {
  if (InterlockedCompareExchange(&g_filesBusy, 0, 0)) return FALSE; /* идёт другой — позже */
  InterlockedExchange(&g_filesFull, 1);
  files_start_index(TRUE);
  if (!InterlockedCompareExchange(&g_filesBusy, 0, 0)) {
    InterlockedExchange(&g_filesFull, 0);
    return FALSE;
  }
  return TRUE;
}

/* Зовётся раз в минуту. План на день составляется при первом тике дня и
   запоминается в files.txt: перезапуск программы не должен давать третий проход. */
static void files_plan_tick(void) {
  static BOOL firstTick = TRUE;
  BOOL wasFirst = firstTick;
  firstTick = FALSE;
  if (!g_filesNotify || !g_filesRoot[0]) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  int day = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
  int now = st.wHour * 60 + st.wMinute;
  if (day != g_fullDay) {
    g_fullDay = day;
    g_fullRuns = 0;
    g_fullNext = FULL_OPEN + (int)files_rand(FULL_CLOSE - FULL_GAP - FULL_OPEN + 1);
    save_files_pref();
  }
  if (g_fullRuns >= 2 || now < FULL_OPEN || now > FULL_CLOSE || now < g_fullNext) return;
  /* компьютер включили позже назначенного — не бежим сразу при запуске,
     а через случайные минуты: утром все включаются одновременно */
  if (wasFirst && now > g_fullNext) {
    g_fullNext = now + 1 + (int)files_rand(20);
    save_files_pref();
    return;
  }
  /* идёт обычный обход — попробуем через минуту. Сам запуск в план не
     пишется: засчитывается законченный обход (files_full_done), а оборванный
     выходом программы — например, её обновлением — пройдёт заново. */
  files_start_full();
}

/* Полный обход дошёл до конца: следующий — через четыре часа. */
static void files_full_done(void) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  int day = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
  int now = st.wHour * 60 + st.wMinute;
  if (day != g_fullDay) return; /* начался вчера — сегодняшнему плану не мешает */
  g_fullRuns++;
  g_fullNext = now + FULL_GAP;
  /* второй не влезает до 16:30 — сегодня его не будет */
  if (g_fullNext > FULL_CLOSE) g_fullRuns = 2;
  save_files_pref();
}

/* ---- что нового в папке ------------------------------------------------ */
/* Последний найденный список держим до следующего: по щелчку на всплывашке
   или из меню в трее его можно открыть и позже. */
static FileChanges *g_chgLast;

static void files_show_changes(void) {
  const FileChanges *c = g_chgLast;
  if (!c) {
    show_status(g_filesNotify ? L"Новых файлов в папке пока не было"
                              : L"Уведомления о файлах выключены в Настройках");
    return;
  }
  int n = c->n < PLM_ROWS ? c->n : PLM_ROWS;
  int cap = 400 + n * (PLM_COL1 + PLM_COL2 + 16);
  wchar_t *text = (wchar_t *)malloc((size_t)cap * sizeof(wchar_t));
  if (!text) return;
  int len = _snwprintf(text, cap, L"Новых: %d, обновлено: %d", c->nNew, c->nUpd);
  if (len < 0) len = 0;
  if (c->nNew + c->nUpd > n)
    len += _snwprintf(text + len, cap - len, L" · показаны первые %d", n);
  len += _snwprintf(text + len, cap - len, L"\r\n\r\n");
  g_resultFiles = TRUE;
  g_plmLastLink[0] = 0;
  for (int i = 0; i < n; i++) {
    const wchar_t *kind = c->upd[i] ? L"обновлён" : L"новый";
    lstrcpynW(g_plmEsi[i], c->name[i], PLM_COL1);
    _snwprintf(g_plmTp[i], PLM_COL2, L"%s · %s", kind, c->dir[i]);
    g_plmTp[i][PLM_COL2 - 1] = 0;
    _snwprintf(g_plmLinks[i], PLM_LINK, L"%s\\%s", c->dir[i], c->name[i]);
    g_plmLinks[i][PLM_LINK - 1] = 0;
    if (!g_plmLastLink[0]) lstrcpynW(g_plmLastLink, g_plmLinks[i], PLM_LINK);
    if (len < cap - 1) {
      int w = _snwprintf(text + len, cap - len, L"%s  %s\r\n", kind, g_plmLinks[i]);
      len = w < 0 ? cap - 1 : len + w;
    }
  }
  text[cap - 1] = 0;
  g_plmCount = n;
  g_ansTitle = L"Изменения в папке";
  show_answer_text(text);
  free(text);
}

/* Обход закончился и нашёл новое: всплывашка у часов и строка в окне. */
static void files_on_changes(FileChanges *c) {
  free(g_chgLast);
  g_chgLast = c;
  if (!c) return;
  wchar_t msg[160];
  if (c->nNew && c->nUpd)
    _snwprintf(msg, 160, L"Новых файлов: %d, обновлено: %d", c->nNew, c->nUpd);
  else if (c->nNew)
    _snwprintf(msg, 160, L"Новых файлов: %d", c->nNew);
  else
    _snwprintf(msg, 160, L"Обновлено файлов: %d", c->nUpd);
  msg[159] = 0;
  if (g_trayAdded) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    g_balloonKind = 0;
    lstrcpynW(n.szInfoTitle, L"Новое в папке", 64);
    _snwprintf(n.szInfo, 256, L"%s\nНажмите, чтобы посмотреть список", msg);
    n.szInfo[255] = 0;
    n.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
  }
  show_status(msg);
}

/* Asks the walk to give up; it notices within a few hundred files. */
static void files_stop_index(void) {
  if (!InterlockedCompareExchange(&g_filesBusy, 0, 0)) return;
  InterlockedExchange(&g_filesAgain, 0);
  InterlockedExchange(&g_filesUserStop, 1);
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
    /* обход уже идёт и ходит по прежней папке. Если его не оборвать, он
       допишет json, где Path уже новый, а файлы ещё старые. */
    if (InterlockedCompareExchange(&g_filesBusy, 0, 0) != 0) {
      InterlockedExchange(&g_filesRestart, 1);
      InterlockedExchange(&g_filesCancel, 1);
    } else {
      files_clear();
      wchar_t jp[MAX_PATH];
      files_idx_path(jp, MAX_PATH);
      DeleteFileW(jp);
    }
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
      ans_printf(out, cap, L"Файлы\r\n\r\n%s", g_filesNote);
    else
      ans_printf(out, cap,
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
    ans_printf(out, cap, L"Файлы\r\n\r\nВ JSON нет «%.80s» (%d записей).", q, g_filesN);
    return FALSE;
  }
  if (total > n)
    ans_printf(out, cap, L"Файлы · %d из %d найденных (всего в JSON %d)\r\n\r\n%s", n,
               total, g_filesN, links);
  else
    ans_printf(out, cap, L"Файлы · %d из JSON\r\n\r\n%s", n, links);
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
    char buf[MAX_PATH * 6 + 256];
    DWORD n = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &n, NULL);
    CloseHandle(h);
    buf[n] = 0;
    /* первая строка — папка; дальше (если есть) — уведомления и план полных
       обходов. Старый файл из одной строки читается как раньше. */
    char *line = buf;
    int ln = 0;
    while (line && *line) {
      char *nl = strchr(line, '\n');
      if (nl) *nl = 0;
      char *cr = strchr(line, '\r');
      if (cr) *cr = 0;
      if (ln == 0) {
        if (line[0]) MultiByteToWideChar(CP_UTF8, 0, line, -1, g_filesRoot, MAX_PATH);
      } else if (!strncmp(line, "watch ", 6)) {
        MultiByteToWideChar(CP_UTF8, 0, line + 6, -1, g_filesWatch, MAX_PATH);
      } else if (!strncmp(line, "notify ", 7)) {
        g_filesNotify = atoi(line + 7) != 0;
      } else if (!strncmp(line, "plan ", 5)) {
        int d = 0, r = 0, nx = 0;
        if (sscanf(line + 5, "%d %d %d", &d, &r, &nx) == 3) {
          g_fullDay = d;
          g_fullRuns = r;
          g_fullNext = nx;
        }
      }
      ln++;
      line = nl ? nl + 1 : NULL;
    }
  }
  files_load_idx();
  files_refresh_status();
}

static void save_files_pref(void) {
  if (g_filesRootEdit) GetWindowTextW(g_filesRootEdit, g_filesRoot, MAX_PATH);
  wchar_t path[MAX_PATH];
  if (!g_dataDir[0]) return;
  _snwprintf(path, MAX_PATH, L"%s\\files.txt", g_dataDir);
  char utf[MAX_PATH * 6 + 128];
  int ul = WideCharToMultiByte(CP_UTF8, 0, g_filesRoot, -1, utf, MAX_PATH * 3, NULL, NULL);
  if (ul <= 0) utf[0] = 0;
  size_t used = strlen(utf);
  snprintf(utf + used, sizeof(utf) - used, "\nnotify %d\nplan %d %d %d\n", g_filesNotify ? 1 : 0,
           g_fullDay, g_fullRuns, g_fullNext);
  if (g_filesWatch[0]) {
    used = strlen(utf);
    memcpy(utf + used, "watch ", 6);
    used += 6;
    ul = WideCharToMultiByte(CP_UTF8, 0, g_filesWatch, -1, utf + used, MAX_PATH * 3, NULL, NULL);
    if (ul <= 0) utf[used - 6] = 0;
    else strcat(utf, "\n");
  }
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(h, utf, (DWORD)strlen(utf), &w, NULL);
  CloseHandle(h);
}
