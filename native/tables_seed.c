/* tables_seed.c — встроенные таблицы (нормативы ОМН, справочник стандартов) в папке пользователя.

   Таблицы едут внутри exe (tables_data.h, его пишет make_tables.py из native/tables) и
   обновляются с каждым выпуском. Папку пользователь правит сам — в Excel, отметками сверки, —
   поэтому при каждом запуске, по каждой встроенной таблице:
   - файла нет — кладём встроенный;
   - лежит ровно то, что мы положили прошлый раз (отпечаток в .встроено.txt), — меняем на новый;
   - файл правили (или он был до нас) и он не такой, как встроенный, — не трогаем, новую версию
     кладём рядом: «имя.tsv.новая» (таблицы читаются только *.tsv) — и говорим, какие.
   Отпечаток — FNV-1a 64 содержимого: отличить свою копию от правленой, не больше. */

typedef struct {
  const wchar_t *group, *path; /* группа (папка в native/tables) и путь внутри неё, через «\» */
  unsigned len;
  const char *data;
} BuiltinTable;

#ifndef TABLES_NO_DATA
#include "tables_data.h"
#endif

#define TB_MANIFEST L".встроено.txt"
#define TB_MAX 512 /* строк в .встроено.txt */

static unsigned long long tb_hash(const void *p, DWORD n) {
  unsigned long long h = 1469598103934665603ULL;
  for (DWORD i = 0; i < n; i++) h = (h ^ ((const BYTE *)p)[i]) * 1099511628211ULL;
  return h;
}

/* весь файл в память (malloc); NULL — нет или не прочитать */
static BYTE *tb_read(const wchar_t *path, DWORD *len) {
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return NULL;
  DWORD n = GetFileSize(h, NULL), got = 0;
  BYTE *b = n != INVALID_FILE_SIZE && n < 64u * 1024 * 1024 ? (BYTE *)malloc(n + 1) : NULL;
  if (b && !(ReadFile(h, b, n, &got, NULL) && got == n)) {
    free(b);
    b = NULL;
  }
  CloseHandle(h);
  if (b) *len = n;
  return b;
}

/* записать целиком: во временный файл и подменой — недописанной таблицы не бывает */
static BOOL tb_write(const wchar_t *path, const void *data, DWORD len) {
  wchar_t tmp[MAX_PATH];
  if (_snwprintf(tmp, MAX_PATH, L"%s.tmp", path) < 0) return FALSE;
  HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  DWORD w = 0;
  BOOL ok = WriteFile(h, data, len, &w, NULL) && w == len;
  CloseHandle(h);
  if (ok) ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
  if (!ok) DeleteFileW(tmp);
  return ok;
}

/* папки по пути файла: «dir\a\b.tsv» — создать dir и dir\a */
static void tb_mkdirs(const wchar_t *file) {
  wchar_t p[MAX_PATH];
  lstrcpynW(p, file, MAX_PATH);
  for (wchar_t *s = p + 3; *s; s++)
    if (*s == L'\\') {
      *s = 0;
      CreateDirectoryW(p, NULL);
      *s = L'\\';
    }
}

typedef struct {
  int n;
  unsigned long long h[TB_MAX];
  wchar_t path[TB_MAX][MAX_PATH];
} TbManifest;

/* .встроено.txt: строки «отпечаток(16 hex)\tпуть» в UTF-8 */
static void tb_manifest_load(const wchar_t *dir, TbManifest *m) {
  wchar_t f[MAX_PATH];
  DWORD len = 0;
  m->n = 0;
  _snwprintf(f, MAX_PATH, L"%s\\%s", dir, TB_MANIFEST);
  f[MAX_PATH - 1] = 0;
  char *b = (char *)tb_read(f, &len);
  if (!b) return;
  b[len] = 0;
  for (char *line = b, *next; line && *line && m->n < TB_MAX; line = next) {
    next = strchr(line, '\n');
    if (next) *next++ = 0;
    char *tab = strchr(line, '\t');
    if (!tab) continue;
    char *cr = strchr(tab, '\r');
    if (cr) *cr = 0;
    m->h[m->n] = _strtoui64(line, NULL, 16);
    if (MultiByteToWideChar(CP_UTF8, 0, tab + 1, -1, m->path[m->n], MAX_PATH) > 1) m->n++;
  }
  free(b);
}

static void tb_manifest_save(const wchar_t *dir, const TbManifest *m) {
  wchar_t f[MAX_PATH];
  _snwprintf(f, MAX_PATH, L"%s\\%s", dir, TB_MANIFEST);
  f[MAX_PATH - 1] = 0;
  size_t cap = (size_t)m->n * (MAX_PATH * 3 + 20) + 1;
  char *b = (char *)malloc(cap);
  if (!b) return;
  size_t k = 0;
  for (int i = 0; i < m->n; i++) {
    k += (size_t)snprintf(b + k, cap - k, "%016llx\t", m->h[i]);
    int w = WideCharToMultiByte(CP_UTF8, 0, m->path[i], -1, b + k, (int)(cap - k), NULL, NULL);
    if (w > 0) k += (size_t)w - 1;
    b[k++] = '\n';
  }
  SetFileAttributesW(f, FILE_ATTRIBUTE_NORMAL);
  if (tb_write(f, b, (DWORD)k)) SetFileAttributesW(f, FILE_ATTRIBUTE_HIDDEN);
  free(b);
}

static int tb_manifest_find(const TbManifest *m, const wchar_t *path) {
  for (int i = 0; i < m->n; i++)
    if (!lstrcmpiW(m->path[i], path)) return i;
  return -1;
}

static void tb_manifest_set(TbManifest *m, const wchar_t *path, unsigned long long h) {
  int i = tb_manifest_find(m, path);
  if (i < 0 && m->n < TB_MAX) {
    i = m->n++;
    lstrcpynW(m->path[i], path, MAX_PATH);
  }
  if (i >= 0) m->h[i] = h;
}

/* Разложить таблицы группы group из t[0..n) в папку dir. В note — какие таблицы пользователь
   правил, а в выпуске пришла новая версия (лежит рядом «….новая»); сообщаем один раз на версию.
   Возвращает, сколько таких таблиц появилось сейчас; -1 — папку не создать. */
static int tables_seed_list(const BuiltinTable *t, int n, const wchar_t *group, const wchar_t *dir, wchar_t *note,
                            int cap) {
  if (note && cap) note[0] = 0;
  if (!dir || !dir[0]) return -1;
  wchar_t probe[MAX_PATH];
  _snwprintf(probe, MAX_PATH, L"%s\\x", dir);
  probe[MAX_PATH - 1] = 0;
  tb_mkdirs(probe);
  if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) return -1;
  TbManifest *m = (TbManifest *)malloc(sizeof(TbManifest));
  if (!m) return -1;
  tb_manifest_load(dir, m);
  int fresh = 0, changed = 0;
  for (int i = 0; i < n; i++) {
    if (lstrcmpW(t[i].group, group)) continue;
    wchar_t path[MAX_PATH], side[MAX_PATH];
    if (_snwprintf(path, MAX_PATH, L"%s\\%s", dir, t[i].path) < 0 ||
        _snwprintf(side, MAX_PATH, L"%s\\%s.новая", dir, t[i].path) < 0)
      continue;
    unsigned long long want = tb_hash(t[i].data, t[i].len);
    int rec = tb_manifest_find(m, t[i].path);
    DWORD len = 0;
    BYTE *cur = tb_read(path, &len);
    BOOL have = cur != NULL;
    unsigned long long hh = have ? tb_hash(cur, len) : 0;
    free(cur);
    if (have && hh == want) { /* уже такая — помнить, что она встроенная */
      if (rec < 0 || m->h[rec] != want) {
        tb_manifest_set(m, t[i].path, want);
        changed = 1;
      }
      DeleteFileW(side);
      continue;
    }
    if (!have || (rec >= 0 && m->h[rec] == hh)) { /* нет или лежит прошлая встроенная — заменить */
      tb_mkdirs(path);
      if (tb_write(path, t[i].data, t[i].len)) {
        tb_manifest_set(m, t[i].path, want);
        changed = 1;
      }
      DeleteFileW(side);
      continue;
    }
    /* правили — не трогаем; новая версия рядом, сообщить, если она ещё не лежит */
    DWORD sl = 0;
    BYTE *old = tb_read(side, &sl);
    BOOL same = old && sl == t[i].len && !memcmp(old, t[i].data, sl);
    free(old);
    if (same || !tb_write(side, t[i].data, t[i].len)) continue;
    fresh++;
    if (note && cap > 0) {
      size_t k = wcslen(note);
      _snwprintf(note + k, (size_t)cap - k, L"%s%s", k ? L", " : L"", t[i].path);
      note[cap - 1] = 0;
    }
  }
  if (changed) tb_manifest_save(dir, m);
  free(m);
  return fresh;
}

#ifndef TABLES_NO_DATA
static int tables_seed(const wchar_t *group, const wchar_t *dir, wchar_t *note, int cap) {
  return tables_seed_list(kTables, TABLES_COUNT, group, dir, note, cap);
}
#endif
