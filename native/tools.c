/* ---- «Ещё»: страницы из pidrpen/giriaja-hall ----------------------------------

   Кнопка «Ещё» внизу окна — список страниц: расчёт резки и газов,
   объединение TIFF / PDF, сортировка TIFF A4/A3. Сами страницы — HTML с
   библиотеками внутри (собирает native/make_tools.py в public/tools/), так
   что открываются в браузере и без интернета.

   Где лежат: %LOCALAPPDATA%\CursorPad\tools\. Скачиваются теми же путями,
   что и обновления (raw.githubusercontent.com, потом зеркала jsDelivr):
   при первом открытии и потом, когда в репозитории сменится метка набора
   tools.txt, — проверка раз за запуск программы, в фоне. Нет сети — просто
   открывается то, что уже скачано. */

#define WM_TOOLS_DONE (WM_APP + 19) /* wParam — какую страницу открыть (или -1), lParam — удалось ли */
#define ID_MORE_BTN 193

typedef struct {
  const wchar_t *file, *title;
} ToolPage;

static const ToolPage kTools[] = {
    {L"cutting.html", L"Расчёт резки и газов"},
    {L"tiff-merge.html", L"Объединение TIFF / PDF"},
    {L"tiff-a4-a3.html", L"Сортировка TIFF A4 / A3"},
};
#define TOOLS_N ((int)(sizeof(kTools) / sizeof(kTools[0])))

static volatile LONG g_toolsBusy;
static BOOL g_toolsChecked; /* за этот запуск метку уже сверяли */

static void tools_dir(wchar_t *out) {
  _snwprintf(out, MAX_PATH, L"%s\\tools", g_dataDir);
  out[MAX_PATH - 1] = 0;
}

static void tools_path(const wchar_t *file, wchar_t *out) {
  wchar_t d[MAX_PATH];
  tools_dir(d);
  _snwprintf(out, MAX_PATH, L"%s\\%s", d, file);
  out[MAX_PATH - 1] = 0;
}

/* Откуда брать: GitHub напрямую (свежее всего), затем зеркала jsDelivr. */
static const struct {
  const wchar_t *host, *prefix;
} kToolSrc[] = {
    {L"raw.githubusercontent.com", L"/pidrpen/ytaqq/main/public/tools/"},
    {L"cdn.jsdelivr.net", L"/gh/pidrpen/ytaqq@main/public/tools/"},
    {L"fastly.jsdelivr.net", L"/gh/pidrpen/ytaqq@main/public/tools/"},
    {L"gcore.jsdelivr.net", L"/gh/pidrpen/ytaqq@main/public/tools/"},
    {L"github.com", L"/pidrpen/ytaqq/raw/main/public/tools/"},
};

/* пришла ли настоящая страница, а не страница входа прокси или пустота */
static BOOL tools_file_ok(const wchar_t *path, BOOL html) {
  char *b = NULL;
  DWORD n = 0;
  if (!read_file_bytes(path, &b, &n, 4096)) return FALSE;
  BOOL ok;
  if (html) {
    const char *s = b;
    if (n >= 3 && (unsigned char)s[0] == 0xEF) s += 3; /* BOM */
    while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t') s++;
    ok = n > 200 && (!_strnicmp(s, "<!doctype html", 14) || !_strnicmp(s, "<html", 5));
    if (ok) { /* страница входа тоже HTML — у нашей есть <title> и она большая */
      WIN32_FILE_ATTRIBUTE_DATA fa;
      ok = GetFileAttributesExW(path, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 20000;
    }
  } else {
    ok = n >= 8 && n < 200 && b[0] != '<';
  }
  free(b);
  return ok;
}

/* один файл набора — во временный, проверить, потом на место */
static BOOL tools_fetch(const wchar_t *file, const wchar_t *dest, BOOL html) {
  wchar_t tmp[MAX_PATH];
  _snwprintf(tmp, MAX_PATH, L"%s.part", dest);
  tmp[MAX_PATH - 1] = 0;
  for (int i = 0; i < (int)(sizeof(kToolSrc) / sizeof(kToolSrc[0])); i++) {
    wchar_t p[300];
    /* метку спрашиваем мимо кэша, страницы — по имени (их меняет метка) */
    _snwprintf(p, 300, html ? L"%s%s" : L"%s%s?t=%lu", kToolSrc[i].prefix, file, GetTickCount());
    p[299] = 0;
    if (!http_get_to_file(kToolSrc[i].host, p, tmp, 8u * 1024u * 1024u, NULL)) continue;
    if (!tools_file_ok(tmp, html)) {
      DeleteFileW(tmp);
      continue;
    }
    if (MoveFileExW(tmp, dest, MOVEFILE_REPLACE_EXISTING)) return TRUE;
    DeleteFileW(tmp);
    return FALSE;
  }
  return FALSE;
}

static BOOL tools_have_all(void) {
  for (int k = 0; k < TOOLS_N; k++) {
    wchar_t p[MAX_PATH];
    tools_path(kTools[k].file, p);
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return FALSE;
  }
  return TRUE;
}

typedef struct {
  int open; /* какую открыть после, -1 — никакую */
  BOOL force;
} ToolsJob;

static DWORD WINAPI tools_thread(LPVOID param) {
  ToolsJob *j = (ToolsJob *)param;
  wchar_t d[MAX_PATH], stampNew[MAX_PATH], stampOld[MAX_PATH];
  tools_dir(d);
  CreateDirectoryW(d, NULL);
  tools_path(L"tools.txt.new", stampNew);
  tools_path(L"tools.txt", stampOld);
  BOOL ok = TRUE;
  if (tools_fetch(L"tools.txt", stampNew, FALSE)) {
    char *a = NULL, *b = NULL;
    DWORD an = 0, bn = 0;
    BOOL same = read_file_bytes(stampNew, &a, &an, 200) && read_file_bytes(stampOld, &b, &bn, 200) &&
                an == bn && !memcmp(a, b, an);
    free(a);
    free(b);
    if (!same || j->force || !tools_have_all()) {
      for (int k = 0; k < TOOLS_N; k++) {
        wchar_t p[MAX_PATH];
        tools_path(kTools[k].file, p);
        if (!tools_fetch(kTools[k].file, p, TRUE)) ok = FALSE;
      }
      /* метку — только когда весь набор на месте: иначе в следующий раз докачаем */
      if (ok) MoveFileExW(stampNew, stampOld, MOVEFILE_REPLACE_EXISTING);
    }
    DeleteFileW(stampNew);
  } else {
    ok = FALSE; /* сети нет — откроется то, что уже есть */
  }
  int open = j->open;
  free(j);
  if (g_hwnd) PostMessageW(g_hwnd, WM_TOOLS_DONE, (WPARAM)open, (LPARAM)ok);
  InterlockedExchange(&g_toolsBusy, 0);
  return 0;
}

static void tools_start(int open, BOOL force) {
  if (!g_dataDir[0]) return;
  if (InterlockedCompareExchange(&g_toolsBusy, 1, 0) != 0) {
    show_status(L"Страницы уже загружаются…");
    return;
  }
  ToolsJob *j = (ToolsJob *)calloc(1, sizeof(ToolsJob));
  if (!j) {
    InterlockedExchange(&g_toolsBusy, 0);
    return;
  }
  j->open = open;
  j->force = force;
  g_toolsChecked = TRUE;
  HANDLE th = CreateThread(NULL, 0, tools_thread, j, 0, NULL);
  if (th) CloseHandle(th);
  else {
    free(j);
    InterlockedExchange(&g_toolsBusy, 0);
  }
}

static BOOL tools_launch(int k) {
  if (k < 0 || k >= TOOLS_N) return FALSE;
  wchar_t p[MAX_PATH];
  tools_path(kTools[k].file, p);
  if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return FALSE;
  ShellExecuteW(NULL, L"open", p, NULL, NULL, SW_SHOWNORMAL);
  return TRUE;
}

static void tools_open(int k) {
  if (tools_launch(k)) {
    /* есть своя копия — открыли сразу; раз за запуск тихо сверяем метку */
    if (!g_toolsChecked) tools_start(-1, FALSE);
    return;
  }
  show_status(L"Загружаю страницу…");
  tools_start(k, FALSE);
}

static void tools_on_done(int open, BOOL ok) {
  if (open >= 0) {
    if (!tools_launch(open)) show_status(L"Страницу скачать не удалось — нет сети?");
    return;
  }
  if (open == -2) show_status(ok ? L"Страницы обновлены" : L"Обновить страницы не удалось");
}

/* список под кнопкой «Ещё» */
static void tools_menu(HWND owner, HWND btn) {
  HMENU m = CreatePopupMenu();
  if (!m) return;
  for (int k = 0; k < TOOLS_N; k++) AppendMenuW(m, MF_STRING, 1 + k, kTools[k].title);
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, 100, L"Обновить страницы с GitHub");
  RECT r;
  GetWindowRect(btn, &r);
  SetForegroundWindow(owner);
  /* окно внизу экрана — список раскрывается вверх, если вниз не помещается */
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERPOSANIMATION, r.left,
                           r.bottom, 0, owner, NULL);
  DestroyMenu(m);
  if (cmd >= 1 && cmd <= TOOLS_N) tools_open(cmd - 1);
  if (cmd == 100) {
    show_status(L"Обновляю страницы…");
    tools_start(-2, TRUE);
  }
}
