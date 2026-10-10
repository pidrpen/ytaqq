/* ---- Tesseract: точное распознавание ----------------------------------------

   Included from pad_extra.c, right before ocr_file_sync (needs run_capture).

   С 2026.10 «Выделить и прочитать» читает Tesseract 5 (русская + английская
   модели), а не Windows OCR. Замер на тексте с экрана — Segoe UI, Calibri,
   Arial, Times, Tahoma, Verdana, Consolas, 11–16 точек, строки из ведомостей,
   ГОСТов, служебок: у Windows OCR 11–12 ошибок на 100 знаков, у Tesseract
   около 2, и он не медленнее. Картинку готовит тот же CursorPadOcr.exe
   (--prep: серое, растяжка контраста, увеличение ×2–3), что и для Windows.

   Пакет — папка tesseract рядом с данными (%LOCALAPPDATA%\CursorPad\tesseract):
     tesseract.exe, его DLL, tessdata\rus.traineddata, tessdata\eng.traineddata,
     pack.txt — номер пакета и sha256 каждого файла.
   В exe он не вшит: это ~28 МБ, а программа — 2. Нет пакета или он не
   сработал — читает Windows OCR, как раньше.

   Откуда берётся пакет — «Проверить обновления» (update.c) после самой
   программы сверяет и его: public/tess/pack.txt на GitHub (те же зеркала,
   jsDelivr), каждый файл как tess/<имя>.bin. Качаются только изменившиеся
   файлы, у каждого сверяется sha256, складывается в tesseract.new и только
   целиком подменяет рабочую папку. Нет интернета — тот же пакет берётся из
   общей папки: <папка>\CursorPad-PLM\tesseract (обычные имена, без .bin).

   Tesseract не любит русские буквы в путях, а в профиле Windows они бывают
   («C:\Users\Иванов»). Поэтому он запускается из своей папки и получает
   только относительные пути. */

#define TESS_PACK_MAX 64        /* файлов в пакете */
#define TESS_FILE_MAX (24u << 20) /* один файл пакета — не больше 24 МБ */

static BOOL g_ocrWinOnly;        /* ocr-engine.txt «windows»: Tesseract не трогать */
static BOOL g_ocrEngineLoaded;
/* g_tessNote — чем кончилась сверка пакета; объявлен в update.c, он его показывает */
static wchar_t g_ocrBy[24];      /* кто прочитал последний фрагмент */
static volatile LONG g_tessShareBusy;

typedef struct {
  char sha[65];
  DWORD size;
  wchar_t name[96]; /* относительный, через «\» */
} TessFile;

typedef struct {
  long ver;
  wchar_t title[80];
  int n;
  TessFile f[TESS_PACK_MAX];
} TessPack;

static void ocr_engine_load(void) {
  if (g_ocrEngineLoaded || !g_dataDir[0]) return;
  g_ocrEngineLoaded = TRUE;
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s\\ocr-engine.txt", g_dataDir);
  char t[32] = {0};
  HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD r = 0;
  ReadFile(f, t, sizeof(t) - 1, &r, NULL);
  CloseHandle(f);
  g_ocrWinOnly = !strncmp(t, "windows", 7);
}

static void ocr_engine_save(void) {
  wchar_t p[MAX_PATH];
  if (!g_dataDir[0]) return;
  _snwprintf(p, MAX_PATH, L"%s\\ocr-engine.txt", g_dataDir);
  const char *t = g_ocrWinOnly ? "windows\n" : "tesseract\n";
  HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(f, t, (DWORD)strlen(t), &w, NULL);
  CloseHandle(f);
}

static BOOL tess_dir_ok(const wchar_t *dir) {
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s\\tesseract.exe", dir);
  if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return FALSE;
  _snwprintf(p, MAX_PATH, L"%s\\tessdata\\rus.traineddata", dir);
  return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

/* рабочая папка пакета: сначала у данных (её обновляем), потом рядом с exe */
static BOOL tess_find(wchar_t *dir) {
  if (g_dataDir[0]) {
    _snwprintf(dir, MAX_PATH, L"%s\\tesseract", g_dataDir);
    if (tess_dir_ok(dir)) return TRUE;
  }
  exe_dir(dir, MAX_PATH);
  wcsncat(dir, L"\\tesseract", MAX_PATH - wcslen(dir) - 1);
  return tess_dir_ok(dir);
}

static BOOL tess_ready(void) {
  wchar_t d[MAX_PATH];
  return tess_find(d);
}

/* Распознать BMP. NULL — не вышло (нет пакета, ошибка, пусто): тогда читает Windows. */
static wchar_t *tess_ocr(const wchar_t *bmp, const wchar_t *helper) {
  wchar_t dir[MAX_PATH], in[MAX_PATH], cmd[1400];
  ocr_engine_load();
  if (g_ocrWinOnly || !tess_find(dir)) return NULL;
  if (GetFileAttributesW(helper) == INVALID_FILE_ATTRIBUTES) return NULL;

  /* подготовленная картинка — в папку пакета, чтобы путь был относительным */
  wchar_t rel[40];
  _snwprintf(rel, 40, L"ocr-in-%lu.bmp", GetTickCount());
  _snwprintf(in, MAX_PATH, L"%s\\%s", dir, rel);
  char *out = NULL, err[256] = {0};
  DWORD n = 0, code = 0;
  _snwprintf(cmd, 1400, L"\"%s\" --prep \"%s\" \"%s\"", helper, bmp, in);
  BOOL prepped = run_capture(cmd, &out, &n, &code, err, 256) && code == 0 &&
                 GetFileAttributesW(in) != INVALID_FILE_ATTRIBUTES;
  free(out);
  out = NULL;
  const wchar_t *arg = rel;
  if (!prepped) {
    /* папка пакета только для чтения (лежит рядом с exe) — тогда во временную;
       русские буквы в пути к ней Tesseract может и не прочитать — тогда Windows */
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    _snwprintf(in, MAX_PATH, L"%s%s", tmp, rel);
    _snwprintf(cmd, 1400, L"\"%s\" --prep \"%s\" \"%s\"", helper, bmp, in);
    if (!run_capture(cmd, &out, &n, &code, err, 256) || code != 0) {
      free(out);
      DeleteFileW(in);
      return NULL;
    }
    free(out);
    out = NULL;
    arg = in;
  }

  wchar_t tessdata[MAX_PATH], langs[24] = L"rus";
  _snwprintf(tessdata, MAX_PATH, L"%s\\tessdata\\eng.traineddata", dir);
  if (GetFileAttributesW(tessdata) != INVALID_FILE_ATTRIBUTES) lstrcpyW(langs, L"rus+eng");
  /* --psm 6 — один блок текста: так выделяют фрагмент экрана */
  _snwprintf(cmd, 1400, L"\"%s\\tesseract.exe\" \"%s\" stdout --tessdata-dir tessdata -l %s --psm 6",
             dir, arg, langs);
  BOOL ran = run_capture_in(cmd, dir, &out, &n, &code, err, 256);
  DeleteFileW(in);
  if (!ran || code != 0 || !out) {
    free(out);
    return NULL;
  }
  /* в конце страницы Tesseract ставит \f; пустые строки по краям не нужны */
  while (n > 0 && (out[n - 1] == '\f' || out[n - 1] == '\n' || out[n - 1] == '\r' ||
                   out[n - 1] == ' '))
    out[--n] = 0;
  DWORD s = 0;
  while (s < n && (out[s] == '\n' || out[s] == '\r' || out[s] == ' ')) s++;
  wchar_t *w = n > s ? utf8_to_alloc(out + s, n - s) : NULL;
  free(out);
  if (!w) return NULL;
  /* Tesseract пишет \n, а окно и буфер обмена Windows ждут \r\n */
  size_t lf = 0, len = wcslen(w);
  for (size_t i = 0; i < len; i++)
    if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) lf++;
  if (lf) {
    wchar_t *c = (wchar_t *)malloc((len + lf + 1) * sizeof(wchar_t));
    if (c) {
      size_t k = 0;
      for (size_t i = 0; i < len; i++) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) c[k++] = L'\r';
        c[k++] = w[i];
      }
      c[k] = 0;
      free(w);
      w = c;
    }
  }
  return w;
}

/* ---- пакет: pack.txt ---------------------------------------------------------

   2026101001
   Tesseract 5.4.0 · rus + eng
   f <sha256> <байт> tesseract.exe
   f <sha256> <байт> tessdata/rus.traineddata
   … */

static BOOL tess_pack_parse(const char *t, TessPack *p) {
  memset(p, 0, sizeof(*p));
  p->ver = parse_ver_file(t);
  if (p->ver <= 0) return FALSE;
  const char *line = strchr(t, '\n');
  if (line) {
    line++;
    const char *e = line;
    while (*e && *e != '\r' && *e != '\n') e++;
    int k = MultiByteToWideChar(CP_UTF8, 0, line, (int)(e - line), p->title, 79);
    p->title[k > 0 ? k : 0] = 0;
  }
  for (const char *q = t; q && *q; q = strchr(q, '\n'), q = q ? q + 1 : NULL) {
    if (q[0] != 'f' || q[1] != ' ') continue;
    if (p->n >= TESS_PACK_MAX) return FALSE;
    TessFile *f = &p->f[p->n];
    char name[200];
    unsigned long sz = 0;
    if (sscanf(q, "f %64s %lu %199[^\r\n]", f->sha, &sz, name) != 3 || strlen(f->sha) != 64)
      return FALSE;
    /* только внутри пакета: ни «..», ни дисков, ни абсолютных путей */
    if (strstr(name, "..") || strchr(name, ':') || name[0] == '/' || name[0] == '\\') return FALSE;
    if (sz == 0 || sz > TESS_FILE_MAX) return FALSE;
    f->size = (DWORD)sz;
    int k = MultiByteToWideChar(CP_UTF8, 0, name, -1, f->name, 96);
    if (k <= 0) return FALSE;
    for (wchar_t *c = f->name; *c; c++)
      if (*c == L'/') *c = L'\\';
    p->n++;
  }
  return p->n > 0;
}

static BOOL tess_pack_read(const wchar_t *path, TessPack *p) {
  char *t = NULL;
  DWORD n = 0;
  if (!read_file_bytes(path, &t, &n, 64 * 1024)) return FALSE;
  BOOL ok = tess_pack_parse(t, p);
  free(t);
  return ok;
}

static BOOL tess_file_ok(const wchar_t *path, const TessFile *f) {
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return FALSE;
  if (a.nFileSizeHigh || a.nFileSizeLow != f->size) return FALSE;
  char got[65];
  return upd_file_sha256(path, got) && !strcmp(got, f->sha);
}

static void tess_rmdir(const wchar_t *dir) {
  /* SHFileOperation хочет двойной ноль в конце */
  wchar_t from[MAX_PATH + 2];
  memset(from, 0, sizeof(from));
  lstrcpynW(from, dir, MAX_PATH);
  SHFILEOPSTRUCTW op;
  memset(&op, 0, sizeof(op));
  op.wFunc = FO_DELETE;
  op.pFrom = from;
  op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
  SHFileOperationW(&op);
}

/* создать папки по пути к файлу внутри base */
static void tess_mkdirs(const wchar_t *base, const wchar_t *rel) {
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s\\%s", base, rel);
  p[MAX_PATH - 1] = 0;
  for (wchar_t *c = p + wcslen(base) + 1; *c; c++) {
    if (*c != L'\\') continue;
    *c = 0;
    CreateDirectoryW(p, NULL);
    *c = L'\\';
  }
}

/* Скачать файл пакета: по метке выпуска (как программу), иначе с зеркал @main. */
static BOOL tess_fetch_net(const wchar_t *rel, const wchar_t *dest, DWORD maxn) {
  wchar_t file[160], path[420];
  _snwprintf(file, 160, L"tess/%s", rel);
  for (wchar_t *c = file; *c; c++)
    if (*c == L'\\') *c = L'/';
  if (fetch_pinned(file, dest, maxn)) return TRUE;
  static const wchar_t *kHosts[] = {L"cdn.jsdelivr.net", L"fastly.jsdelivr.net",
                                    L"gcore.jsdelivr.net"};
  for (int i = 0; i < 3; i++) {
    _snwprintf(path, 420, L"/gh/pidrpen/ytaqq@main/public/%s?t=%lu", file, GetTickCount());
    upd_log(L"  пробую %s", kHosts[i]);
    if (http_get_to_file(kHosts[i], path, dest, maxn, NULL)) return TRUE;
  }
  _snwprintf(path, 420, L"/pidrpen/ytaqq/main/public/%s", file);
  upd_log(L"  пробую raw.githubusercontent.com");
  if (http_get_to_file(L"raw.githubusercontent.com", path, dest, maxn, NULL)) return TRUE;
  _snwprintf(path, 420, L"/pidrpen/ytaqq/raw/main/public/%s", file);
  upd_log(L"  пробую github.com");
  if (http_get_to_file(L"github.com", path, dest, maxn, NULL)) return TRUE;
  /* api.github.com — другой адрес, его пропускают и там, где режут CDN и raw;
     отдаёт base64 в JSON (в 4/3 раза больше), большие файлы — через blob */
  DWORD big = maxn + maxn / 2 + 65536;
  _snwprintf(path, 420, L"/repos/pidrpen/ytaqq/contents/public/%s", file);
  upd_log(L"  пробую api.github.com");
  if (http_get_to_file(L"api.github.com", path, dest, big,
                       L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28") &&
      unwrap_github_json(dest, big))
    return TRUE;
  DeleteFileW(dest);
  return FALSE;
}

/* общая папка: <папка>\CursorPad-PLM\tesseract */
static BOOL tess_share_dir(wchar_t *out) {
  if (!g_shareRoot[0]) return FALSE;
  _snwprintf(out, MAX_PATH, L"%s\\CursorPad-PLM\\tesseract", g_shareRoot);
  return TRUE;
}

/* Сверить пакет и, если нужно, обновить. fromNet — пробовать интернет
   (из «Проверить обновления»); иначе только общая папка. Пишет в upd_log
   и в g_tessNote. TRUE — пакет на месте и актуален. */
static BOOL tess_pack_sync(BOOL fromNet) {
  wchar_t live[MAX_PATH], next[MAX_PATH], old[MAX_PATH], p[MAX_PATH], share[MAX_PATH];
  g_tessNote[0] = 0;
  if (!g_dataDir[0]) return FALSE;
  _snwprintf(live, MAX_PATH, L"%s\\tesseract", g_dataDir);
  _snwprintf(next, MAX_PATH, L"%s\\tesseract.new", g_dataDir);
  _snwprintf(old, MAX_PATH, L"%s\\tesseract.old", g_dataDir);
  tess_rmdir(old); /* остаток прошлой подмены */

  TessPack *have = (TessPack *)calloc(1, sizeof(TessPack));
  TessPack *want = (TessPack *)calloc(1, sizeof(TessPack));
  BOOL ok = FALSE;
  if (!have || !want) goto out;
  _snwprintf(p, MAX_PATH, L"%s\\pack.txt", live);
  if (!tess_pack_read(p, have)) have->ver = 0;

  upd_log(L"");
  upd_log(L"Tesseract (точное распознавание): у вас %s",
          have->ver ? have->title : L"не установлен");
  /* откуда: 1 — интернет, 2 — общая папка */
  int src = 0;
  _snwprintf(p, MAX_PATH, L"%s\\tess-pack-check.txt", g_dataDir);
  if (fromNet) {
    upd_log(L"  читаю tess/pack.txt:");
    if (tess_fetch_net(L"pack.txt", p, 64 * 1024) && tess_pack_read(p, want)) src = 1;
  }
  if (!src && tess_share_dir(share)) {
    wchar_t sp[MAX_PATH];
    _snwprintf(sp, MAX_PATH, L"%s\\pack.txt", share);
    if (tess_pack_read(sp, want)) {
      src = 2;
      upd_log(L"  беру из общей папки: %s", share);
    }
  }
  DeleteFileW(p);
  if (!src) {
    upd_log(L"  пакет нигде не найден");
    if (!have->ver) lstrcpynW(g_tessNote, L"Tesseract не скачался — читает Windows OCR", 160);
    ok = have->ver > 0;
    goto out;
  }
  upd_log(L"  выпущен: %s (%ld)", want->title, want->ver);
  if (have->ver >= want->ver && tess_dir_ok(live)) {
    upd_log(L"  актуален");
    lstrcpynW(g_tessNote, L"Tesseract актуален", 160);
    ok = TRUE;
    goto out;
  }

  /* собрать новую папку целиком, рабочую не трогать */
  tess_rmdir(next);
  CreateDirectoryW(next, NULL);
  int fetched = 0, kept = 0;
  for (int i = 0; i < want->n; i++) {
    const TessFile *f = &want->f[i];
    wchar_t cur[MAX_PATH], dst[MAX_PATH];
    tess_mkdirs(next, f->name);
    _snwprintf(cur, MAX_PATH, L"%s\\%s", live, f->name);
    _snwprintf(dst, MAX_PATH, L"%s\\%s", next, f->name);
    if (tess_file_ok(cur, f) && CopyFileW(cur, dst, FALSE)) {
      kept++;
      continue;
    }
    upd_log(L"  %s:", f->name);
    BOOL got = FALSE;
    if (src == 1) {
      wchar_t rel[110];
      _snwprintf(rel, 110, L"%s.bin", f->name);
      got = tess_fetch_net(rel, dst, f->size + 1);
    } else {
      wchar_t sp[MAX_PATH];
      _snwprintf(sp, MAX_PATH, L"%s\\%s", share, f->name);
      got = CopyFileW(sp, dst, FALSE);
    }
    if (!got || !tess_file_ok(dst, f)) {
      upd_log(L"  %s — не скачался или отпечаток sha256 не совпал", f->name);
      tess_rmdir(next);
      lstrcpynW(g_tessNote, L"Tesseract не обновился — см. отчёт", 160);
      ok = have->ver > 0;
      goto out;
    }
    fetched++;
  }
  /* pack.txt — последним: по нему видно, что папка собрана до конца */
  {
    char *body = NULL;
    DWORD bn = 0;
    wchar_t from[MAX_PATH], to[MAX_PATH];
    _snwprintf(to, MAX_PATH, L"%s\\pack.txt", next);
    if (src == 2) {
      _snwprintf(from, MAX_PATH, L"%s\\pack.txt", share);
      CopyFileW(from, to, FALSE);
    } else if (tess_fetch_net(L"pack.txt", to, 64 * 1024) && read_file_bytes(to, &body, &bn, 64 * 1024)) {
      TessPack *again = (TessPack *)calloc(1, sizeof(TessPack));
      /* пока качали, могли выпустить следующий пакет — тогда в другой раз */
      if (!again || !tess_pack_parse(body, again) || again->ver != want->ver) DeleteFileW(to);
      free(again);
      free(body);
    }
    if (!tess_dir_ok(next) || GetFileAttributesW(to) == INVALID_FILE_ATTRIBUTES) {
      upd_log(L"  пакет собрался не целиком — оставляю прежний");
      tess_rmdir(next);
      ok = have->ver > 0;
      goto out;
    }
  }
  /* подмена: рабочую — в .old, новую — на её место */
  BOOL hadLive = GetFileAttributesW(live) != INVALID_FILE_ATTRIBUTES;
  if (hadLive && !MoveFileW(live, old)) {
    upd_log(L"  папка tesseract занята (идёт распознавание?) — подменю в следующий раз");
    lstrcpynW(g_tessNote, L"Tesseract скачан, встанет при следующей проверке", 160);
    ok = have->ver > 0;
    goto out;
  }
  if (!MoveFileW(next, live)) {
    if (hadLive) MoveFileW(old, live);
    upd_log(L"  не удалось поставить новую папку (код %lu)", GetLastError());
    ok = have->ver > 0;
    goto out;
  }
  tess_rmdir(old);
  upd_log(L"  поставлен: %d файлов скачано, %d уже были", fetched, kept);
  _snwprintf(g_tessNote, 160, L"Tesseract %s: %s", have->ver ? L"обновлён" : L"установлен",
             want->title);
  ok = TRUE;
out:
  free(have);
  free(want);
  return ok;
}

/* Пакета нет, а в общей папке он есть — взять оттуда в фоне, не дожидаясь
   «Проверить обновления». Этот раз читает Windows, следующий — Tesseract. */
static DWORD WINAPI tess_share_thread(LPVOID param) {
  (void)param;
  tess_pack_sync(FALSE);
  InterlockedExchange(&g_tessShareBusy, 0);
  return 0;
}

static void tess_share_sync_async(void) {
  wchar_t share[MAX_PATH], p[MAX_PATH];
  if (tess_ready() || !tess_share_dir(share)) return;
  _snwprintf(p, MAX_PATH, L"%s\\pack.txt", share);
  if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return;
  if (InterlockedCompareExchange(&g_tessShareBusy, 1, 0) != 0) return;
  HANDLE th = CreateThread(NULL, 0, tess_share_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else InterlockedExchange(&g_tessShareBusy, 0);
}
