/* ---- PLM для коллег через общую папку --------------------------------------

   Раздаёт тот, у кого есть свой логин SQL и стоит галочка «Раздавать PLM
   коллегам». Коллега без логина кладёт запрос файлом в общую папку:

     <папка>\CursorPad-PLM\online\ПК.txt   — «я раздаю», обновляется раз в 30 с
     <папка>\CursorPad-PLM\req\*.req       — запросы: поиск или карточка
     <папка>\CursorPad-PLM\ans\*.ans       — ответы

   Программа раздающего забирает запрос (переименованием — двое один и тот же
   не возьмут) и выполняет его отдельным процессом того же exe: у процесса свои
   списки находок, и то, что сейчас открыто у самого раздающего, не сбивается.
   В папку попадает только найденное. Пароль остаётся на компьютере раздающего,
   запросы — только поиск по тексту и карточка по номеру: чужой SQL не выполнить. */

#define SHARE_SUB L"CursorPad-PLM"
#define SHARE_PATH 1024

/* g_shareRoot, g_shareServe, g_shareEdit, g_chkServe — в cursorpad.c: их читают настройки */
static CRITICAL_SECTION g_shareCs;
static BOOL g_shareCsOk;
static BOOL g_serveChild;           /* этот процесс — одноразовый исполнитель запроса */
static wchar_t g_shareBeat[SHARE_PATH]; /* наш файл «я раздаю» — удалить при выходе */

static void share_init(void) {
  if (g_shareCsOk) return;
  InitializeCriticalSection(&g_shareCs);
  g_shareCsOk = TRUE;
}

static void share_root_copy(wchar_t *out) {
  share_init();
  EnterCriticalSection(&g_shareCs);
  lstrcpynW(out, g_shareRoot, MAX_PATH);
  LeaveCriticalSection(&g_shareCs);
}

static void share_root_set(const wchar_t *root) {
  share_init();
  EnterCriticalSection(&g_shareCs);
  lstrcpynW(g_shareRoot, root, MAX_PATH);
  size_t n = wcslen(g_shareRoot);
  while (n > 0 && (g_shareRoot[n - 1] == L'\\' || g_shareRoot[n - 1] == L' ')) g_shareRoot[--n] = 0;
  LeaveCriticalSection(&g_shareCs);
}

/* <папка>\CursorPad-PLM\sub, созданная при необходимости */
static BOOL share_dir(const wchar_t *root, const wchar_t *sub, wchar_t *out) {
  if (!root[0]) return FALSE;
  _snwprintf(out, SHARE_PATH, L"%s\\%s", root, SHARE_SUB);
  out[SHARE_PATH - 1] = 0;
  CreateDirectoryW(out, NULL);
  size_t l = wcslen(out);
  _snwprintf(out + l, SHARE_PATH - l, L"\\%s", sub);
  out[SHARE_PATH - 1] = 0;
  CreateDirectoryW(out, NULL);
  DWORD a = GetFileAttributesW(out);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void share_me(wchar_t *user, wchar_t *pc) {
  DWORD n = 128;
  if (!GetUserNameW(user, &n)) lstrcpynW(user, L"?", 128);
  n = 64;
  if (!GetComputerNameW(pc, &n)) lstrcpynW(pc, L"PC", 64);
}

static ULONGLONG ft_u64(FILETIME f) {
  return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime;
}

static ULONGLONG ft_now(void) {
  FILETIME f;
  GetSystemTimeAsFileTime(&f);
  return ft_u64(f);
}

/* разница во времени по модулю: часы у компьютеров расходятся в обе стороны */
static BOOL ft_within(FILETIME f, ULONGLONG seconds) {
  ULONGLONG a = ft_u64(f), b = ft_now();
  ULONGLONG d = a > b ? a - b : b - a;
  return d < seconds * 10000000ULL;
}

/* Файл пишется целиком под временным именем и переименовывается: второй
   компьютер никогда не прочтёт половину. */
/* direct — если подмена файла не выходит (на сетевом диске её может не
   пустить чужая открытая копия), записать прямо поверх. Читающая сторона
   тогда может застать файл недописанным и должна это проверять. */
static BOOL share_write_ex(const wchar_t *path, const wchar_t *text, BOOL direct) {
  int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  if (need <= 0) return FALSE;
  char *utf = (char *)malloc((size_t)need);
  if (!utf) return FALSE;
  WideCharToMultiByte(CP_UTF8, 0, text, -1, utf, need, NULL, NULL);
  wchar_t tmp[SHARE_PATH + 8];
  _snwprintf(tmp, SHARE_PATH + 8, L"%s.part", path);
  tmp[SHARE_PATH + 7] = 0;
  HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  BOOL ok = FALSE;
  if (h != INVALID_HANDLE_VALUE) {
    DWORD w = 0;
    ok = WriteFile(h, utf, (DWORD)(need - 1), &w, NULL) && w == (DWORD)(need - 1);
    CloseHandle(h);
    if (ok) {
      BOOL moved = FALSE;
      for (int tries = 0; tries < 5 && !moved; tries++) {
        if (tries) Sleep(40); /* другой компьютер как раз читает — подождать */
        moved = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
      }
      if (!moved && direct) {
        moved = FALSE;
        for (int tries = 0; tries < 5 && !moved; tries++) {
          if (tries) Sleep(40);
          HANDLE d = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
          if (d == INVALID_HANDLE_VALUE) continue;
          DWORD w2 = 0;
          moved = WriteFile(d, utf, (DWORD)(need - 1), &w2, NULL) && w2 == (DWORD)(need - 1);
          CloseHandle(d);
        }
      }
      ok = moved;
    }
    if (!ok) DeleteFileW(tmp);
  }
  free(utf);
  return ok;
}

static BOOL share_write(const wchar_t *path, const wchar_t *text) { return share_write_ex(path, text, FALSE); }

static wchar_t *share_read(const wchar_t *path) {
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return NULL;
  DWORD sz = GetFileSize(h, NULL);
  if (sz == INVALID_FILE_SIZE || sz > 8u * 1024u * 1024u) {
    CloseHandle(h);
    return NULL;
  }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    CloseHandle(h);
    return NULL;
  }
  DWORD r = 0;
  BOOL ok = ReadFile(h, buf, sz, &r, NULL);
  CloseHandle(h);
  if (!ok) {
    free(buf);
    return NULL;
  }
  buf[r] = 0;
  int n = MultiByteToWideChar(CP_UTF8, 0, buf, (int)r + 1, NULL, 0);
  wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
  if (w) MultiByteToWideChar(CP_UTF8, 0, buf, (int)r + 1, w, n);
  free(buf);
  return w;
}

/* растущая строка для ответа */
typedef struct {
  wchar_t *w;
  size_t len, cap;
} ShareBuf;

static void sb_add(ShareBuf *b, const wchar_t *fmt, ...) {
  for (;;) {
    if (!b->w || b->cap - b->len < 64) {
      size_t nc = b->cap ? b->cap * 2 : 8192;
      wchar_t *nw = (wchar_t *)realloc(b->w, nc * sizeof(wchar_t));
      if (!nw) return;
      b->w = nw;
      b->cap = nc;
      b->w[b->len] = 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int r = _vsnwprintf(b->w + b->len, b->cap - b->len - 1, fmt, ap);
    va_end(ap);
    if (r >= 0 && (size_t)r < b->cap - b->len - 1) {
      b->len += (size_t)r;
      b->w[b->len] = 0;
      return;
    }
    b->w[b->len] = 0;
    if (b->cap > 16u * 1024u * 1024u) return;
    size_t nc = b->cap * 2;
    wchar_t *nw = (wchar_t *)realloc(b->w, nc * sizeof(wchar_t));
    if (!nw) return;
    b->w = nw;
    b->cap = nc;
  }
}

/* поле строки ответа: табуляция и переводы строк ломали бы разбор */
static void sb_field(ShareBuf *b, const wchar_t *s) {
  wchar_t tmp[PLM_LINK];
  size_t i = 0;
  for (; s && s[i] && i < PLM_LINK - 1; i++)
    tmp[i] = (s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n') ? L' ' : s[i];
  tmp[i] = 0;
  sb_add(b, L"\t%s", tmp);
}

/* Разбирает «ключ\tполе\tполе…\n» до строки «text»; *text — всё после неё. */
static int share_split(wchar_t *line, wchar_t **f, int max) {
  int n = 0;
  f[n++] = line;
  for (wchar_t *p = line; *p && n < max; p++) {
    if (*p == L'\t') {
      *p = 0;
      f[n++] = p + 1;
    }
  }
  return n;
}

static wchar_t *share_next_line(wchar_t **pp) {
  wchar_t *p = *pp;
  if (!p || !*p) return NULL;
  wchar_t *nl = wcschr(p, L'\n');
  if (nl) {
    *nl = 0;
    *pp = nl + 1;
  } else {
    *pp = p + wcslen(p);
  }
  size_t l = wcslen(p);
  if (l && p[l - 1] == L'\r') p[l - 1] = 0;
  return p;
}

static void share_log(const wchar_t *who, const wchar_t *what) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%s\\plm_share.log", g_dataDir);
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t line[700];
  _snwprintf(line, 700, L"%04u-%02u-%02u %02u:%02u  %s  %s\r\n", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, who, what);
  line[699] = 0;
  char utf[2100];
  int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf, sizeof(utf), NULL, NULL);
  if (n <= 1) return;
  HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(h, utf, (DWORD)(n - 1), &w, NULL);
  CloseHandle(h);
}

/* ---- сторона раздающего ---------------------------------------------------- */

/* Отдельный процесс: CursorPad.exe --plm-serve запрос ответ */
static int share_serve_child(const wchar_t *reqPath, const wchar_t *ansPath) {
  g_serveChild = TRUE;
  notes_path();
  load_plm_pref();
  wchar_t *req = share_read(reqPath);
  if (!req) return 1;
  wchar_t kind[16] = L"", q[400] = L"", from[200] = L"", idl[1400] = L"";
  long id = 0;
  BOOL verbose = FALSE;
  wchar_t *p = req, *line;
  while ((line = share_next_line(&p)) != NULL) {
    wchar_t *f[4];
    int n = share_split(line, f, 4);
    if (n < 2) continue;
    if (!wcscmp(f[0], L"kind")) lstrcpynW(kind, f[1], 16);
    else if (!wcscmp(f[0], L"q")) lstrcpynW(q, f[1], 400);
    else if (!wcscmp(f[0], L"id")) id = wcstol(f[1], NULL, 10);
    else if (!wcscmp(f[0], L"verbose")) verbose = f[1][0] == L'1';
    else if (!wcscmp(f[0], L"from")) lstrcpynW(from, f[1], 200);
    else if (!wcscmp(f[0], L"ids")) lstrcpynW(idl, f[1], 1400);
  }
  free(req);
  wchar_t user[128], pc[64];
  share_me(user, pc);
  ShareBuf b = {0};
  sb_add(&b, L"CPANS1\nvia");
  wchar_t via[200];
  _snwprintf(via, 200, L"%s (%s)", user, pc);
  sb_field(&b, via);
  sb_add(&b, L"\n");
  wchar_t *out = (wchar_t *)malloc(160000 * sizeof(wchar_t));
  if (!out) return 1;
  out[0] = 0;
  if (!wcscmp(kind, L"search") && q[0]) {
    plm_lookup(q, out, 1800);
    for (int i = 0; i < g_plmCount; i++) {
      sb_add(&b, L"row\t%ld\t%d\t%ld\t%ld", g_plmIds[i], g_plmTmpl[i], g_plmTpId[i],
             g_plmRealId[i]);
      sb_field(&b, g_plmEsi[i]);
      sb_field(&b, g_plmTp[i]);
      sb_field(&b, g_plmLinks[i]);
      sb_add(&b, L"\n");
      /* заготовка — отдельной строкой: старая версия у коллеги её пропустит,
         а не примет за часть ссылки */
      if (g_plmPf[i][0]) {
        sb_add(&b, L"pf\t%d", i);
        sb_field(&b, g_plmPf[i]);
        sb_add(&b, L"\n");
      }
    }
  } else if (!wcscmp(kind, L"card") && id > 0) {
    load_files_pref(); /* чертёж для карточки ищется в своём индексе */
    g_cardVerbose = verbose;
    plm_card(id, out, 160000);
    for (int i = 0; i < g_opsPendN; i++) {
      sb_add(&b, L"op");
      sb_field(&b, g_opsPend[i]);
      sb_add(&b, L"\n");
    }
    if (g_cardDraw[0]) {
      sb_add(&b, L"draw");
      sb_field(&b, g_cardDraw);
      sb_add(&b, L"\n");
    }
  } else if (!wcscmp(kind, L"pf") && idl[0]) {
    /* столбец «Заготовка» для находок коллеги — отдельно от самого поиска */
    PfJob *j = (PfJob *)calloc(1, sizeof(PfJob));
    if (j) {
      for (wchar_t *t = idl; *t && j->n < PF_JOB_MAX;) {
        long v = wcstol(t, &t, 10);
        if (v > 0) j->ids[j->n++] = v;
        while (*t == L',' || *t == L' ') t++;
        if (*t && !iswdigit(*t)) break;
      }
      SQLHENV env = SQL_NULL_HENV;
      SQLHDBC dbc = SQL_NULL_HDBC;
      wchar_t err[280];
      if (j->n && plm_connect(&env, &dbc, err, 280)) {
        pf_compute(dbc, j, 15000);
        SQLDisconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
      }
      for (int k = 0; k < j->n; k++) {
        if (!j->text[k][0]) continue;
        sb_add(&b, L"pfr\t%ld", j->ids[k]);
        sb_field(&b, j->text[k]);
        sb_add(&b, L"\n");
      }
      free(j);
    }
  } else {
    lstrcpynW(out, L"PLM\r\n\r\nНепонятный запрос.", 160000);
  }
  sb_add(&b, L"text\n%s", out);
  free(out);
  BOOL ok = b.w && share_write(ansPath, b.w);
  free(b.w);
  return ok ? 0 : 1;
}

static BOOL ends_with(const wchar_t *s, const wchar_t *suf) {
  size_t a = wcslen(s), b = wcslen(suf);
  return a >= b && _wcsicmp(s + a - b, suf) == 0;
}

/* выполнить один забранный запрос отдельным процессом */
/* Заготовки для столбца коллеги — фоновая работа: её исполнитель идёт сам по
   себе, а поиски и карточки коллег за ним не ждут в очереди. Одновременно
   не больше одного такого. */
static HANDLE g_pfProc;
static wchar_t g_pfWork[SHARE_PATH], g_pfAns[SHARE_PATH];
static ULONGLONG g_pfT0;

static void share_pf_reap(BOOL force) {
  if (!g_pfProc) return;
  BOOL done = WaitForSingleObject(g_pfProc, 0) == WAIT_OBJECT_0;
  if (!done && !force && GetTickCount64() - g_pfT0 < 60000) return;
  if (!done) TerminateProcess(g_pfProc, 2);
  CloseHandle(g_pfProc);
  g_pfProc = NULL;
  /* пустой ответ — только если исполнителя пришлось прервать: закончивший
     свой ответ уже положил, и коллега мог его забрать раньше этой проверки */
  if (!done) share_write(g_pfAns, L"CPANS1\ntext\n");
  DeleteFileW(g_pfWork);
}

static void share_pf_start(const wchar_t *work, const wchar_t *ans) {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  wchar_t *cmd = (wchar_t *)malloc(sizeof(wchar_t) * (MAX_PATH + SHARE_PATH * 2 + 64));
  if (!cmd) return;
  _snwprintf(cmd, MAX_PATH + SHARE_PATH * 2 + 64, L"\"%s\" --plm-serve \"%s\" \"%s\"", exe, work, ans);
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  memset(&pi, 0, sizeof(pi));
  BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  free(cmd);
  if (!ok) {
    share_write(ans, L"CPANS1\ntext\n");
    DeleteFileW(work);
    return;
  }
  CloseHandle(pi.hThread);
  g_pfProc = pi.hProcess;
  g_pfT0 = GetTickCount64();
  lstrcpynW(g_pfWork, work, SHARE_PATH);
  lstrcpynW(g_pfAns, ans, SHARE_PATH);
}

static void share_run_one(const wchar_t *work, const wchar_t *ans) {
  wchar_t *req = share_read(work);
  wchar_t who[200] = L"?", what[440] = L"?";
  if (req) {
    wchar_t *p = req, *line;
    wchar_t kind[16] = L"";
    while ((line = share_next_line(&p)) != NULL) {
      wchar_t *f[4];
      if (share_split(line, f, 4) < 2) continue;
      if (!wcscmp(f[0], L"from")) lstrcpynW(who, f[1], 200);
      else if (!wcscmp(f[0], L"kind")) lstrcpynW(kind, f[1], 16);
      else if (!wcscmp(f[0], L"q")) _snwprintf(what, 440, L"поиск «%.300s»", f[1]);
      else if (!wcscmp(f[0], L"id")) _snwprintf(what, 440, L"карточка IO.%.20s", f[1]);
    }
    what[439] = 0;
    free(req);
  }
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  wchar_t *cmd = (wchar_t *)malloc(sizeof(wchar_t) * (MAX_PATH + SHARE_PATH * 2 + 64));
  if (!cmd) return;
  _snwprintf(cmd, MAX_PATH + SHARE_PATH * 2 + 64, L"\"%s\" --plm-serve \"%s\" \"%s\"", exe, work, ans);
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  memset(&pi, 0, sizeof(pi));
  BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  free(cmd);
  BOOL answered = FALSE, killed = FALSE;
  ULONGLONG t0 = GetTickCount64();
  if (started) {
    /* тяжёлая карточка на загруженном сервере — до полутора минут */
    if (WaitForSingleObject(pi.hProcess, 90000) == WAIT_TIMEOUT) {
      TerminateProcess(pi.hProcess, 2);
      killed = TRUE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    answered = GetFileAttributesW(ans) != INVALID_FILE_ATTRIBUTES;
  }
  if (!answered)
    share_write(ans, L"CPANS1\ntext\nPLM\r\n\r\nУ коллеги запрос не выполнился — попробуйте ещё раз.");
  DeleteFileW(work);
  /* в журнал — и сколько шло: без этого «не отвечает» не с чем сверить */
  wchar_t what2[520];
  _snwprintf(what2, 520, L"%s — %.1f с%s", what, (double)(GetTickCount64() - t0) / 1000.0,
             !started ? L", не запустился исполнитель" : (killed ? L", прерван: дольше 90 с" :
             (answered ? L"" : L", без ответа")));
  what2[519] = 0;
  share_log(who, what2);
}

/* Фоновый поток раздающего. Пока галочка снята — только спит. */
static DWORD WINAPI share_serve_thread(LPVOID param) {
  (void)param;
  ULONGLONG lastBeat = 0, lastClean = 0;
  for (;;) {
    Sleep(1000);
    wchar_t root[MAX_PATH];
    share_root_copy(root);
    BOOL on = g_shareServe && root[0] && g_sqlUser[0] && g_sqlPass[0];
    wchar_t dOn[SHARE_PATH], dReq[SHARE_PATH], dAns[SHARE_PATH];
    if (on) on = share_dir(root, L"online", dOn) && share_dir(root, L"req", dReq) &&
                 share_dir(root, L"ans", dAns);
    wchar_t beat[SHARE_PATH] = L"";
    wchar_t user[128], pc[64];
    share_me(user, pc);
    if (on) _snwprintf(beat, SHARE_PATH, L"%s\\%s.txt", dOn, pc);
    EnterCriticalSection(&g_shareCs);
    BOOL moved = g_shareBeat[0] && wcscmp(beat, g_shareBeat) != 0;
    wchar_t old[SHARE_PATH];
    lstrcpynW(old, g_shareBeat, SHARE_PATH);
    if (moved || !on) g_shareBeat[0] = 0;
    LeaveCriticalSection(&g_shareCs);
    if (moved) DeleteFileW(old); /* галочку сняли или папку сменили */
    if (!on) continue;
    ULONGLONG now = GetTickCount64();
    if (moved || !old[0] || now - lastBeat > 30000) {
      wchar_t txt[300];
      _snwprintf(txt, 300, L"%s\t%s\t%s\n", user, pc, APP_VERSION_STR);
      if (share_write(beat, txt)) {
        lastBeat = now;
        EnterCriticalSection(&g_shareCs);
        lstrcpynW(g_shareBeat, beat, SHARE_PATH);
        LeaveCriticalSection(&g_shareCs);
      }
    }
    /* сначала имена, потом работа: пока выполняется один запрос, папку не держим */
    wchar_t pat[SHARE_PATH + 8];
    _snwprintf(pat, SHARE_PATH + 8, L"%s\\*.req", dReq);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    wchar_t names[8][MAX_PATH];
    FILETIME times[8];
    int nn = 0;
    if (f != INVALID_HANDLE_VALUE) {
      do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!ends_with(fd.cFileName, L".req")) continue;
        lstrcpynW(names[nn], fd.cFileName, MAX_PATH);
        times[nn] = fd.ftLastWriteTime;
        nn++;
      } while (nn < 8 && FindNextFileW(f, &fd));
      FindClose(f);
    }
    share_pf_reap(FALSE);
    /* два прохода: сперва поиски и карточки (их ждут), потом заготовки */
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < nn; i++) {
      BOOL isPf = ends_with(names[i], L"-pf.req");
      if ((pass == 0) == isPf) continue;
      if (isPf && g_pfProc) continue; /* прежний ещё считает — этот подождёт */
      wchar_t req[SHARE_PATH], work[SHARE_PATH], ans[SHARE_PATH];
      _snwprintf(req, SHARE_PATH, L"%s\\%s", dReq, names[i]);
      /* брошенный запрос (коллега давно не ждёт) — убрать */
      if (!ft_within(times[i], 600)) {
        DeleteFileW(req);
        continue;
      }
      size_t bl = wcslen(names[i]) - 4;
      _snwprintf(work, SHARE_PATH, L"%s\\%.*s.work", dReq, (int)bl, names[i]);
      _snwprintf(ans, SHARE_PATH, L"%s\\%.*s.ans", dAns, (int)bl, names[i]);
      /* забрать: переименование удаётся только одному из раздающих */
      if (!MoveFileW(req, work)) continue;
      if (isPf) share_pf_start(work, ans);
      else share_run_one(work, ans);
    }
    /* ответы, которые никто не забрал, — раз в десять минут */
    if (now - lastClean > 600000) {
      lastClean = now;
      _snwprintf(pat, SHARE_PATH + 8, L"%s\\*", dAns);
      f = FindFirstFileW(pat, &fd);
      if (f != INVALID_HANDLE_VALUE) {
        do {
          if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
          if (ft_within(fd.ftLastWriteTime, 600)) continue;
          wchar_t full[SHARE_PATH];
          _snwprintf(full, SHARE_PATH, L"%s\\%s", dAns, fd.cFileName);
          DeleteFileW(full);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
      }
    }
  }
  return 0;
}

static void share_start(void) {
  share_init();
  HANDLE th = CreateThread(NULL, 0, share_serve_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
}

/* при выходе: коллеги не должны слать запросы тому, кого уже нет */
static void share_stop(void) {
  share_init();
  wchar_t beat[SHARE_PATH];
  EnterCriticalSection(&g_shareCs);
  lstrcpynW(beat, g_shareBeat, SHARE_PATH);
  g_shareBeat[0] = 0;
  g_shareServe = FALSE;
  LeaveCriticalSection(&g_shareCs);
  if (beat[0]) DeleteFileW(beat);
}


/* ---- сторона коллеги без логина ------------------------------------------- */

static BOOL share_client_on(void) {
  if (g_serveChild) return FALSE;
  wchar_t root[MAX_PATH];
  share_root_copy(root);
  return root[0] && (!g_sqlUser[0] || !g_sqlPass[0]);
}

/* кто сейчас раздаёт: первый, кто отметился за последние три минуты */
static BOOL share_find_server(const wchar_t *root, wchar_t *who, int cap) {
  wchar_t dOn[SHARE_PATH], pat[SHARE_PATH + 8];
  if (!share_dir(root, L"online", dOn)) return FALSE;
  _snwprintf(pat, SHARE_PATH + 8, L"%s\\*.txt", dOn);
  WIN32_FIND_DATAW fd;
  HANDLE f = FindFirstFileW(pat, &fd);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  BOOL found = FALSE;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (!ft_within(fd.ftLastWriteTime, 180)) continue;
    wchar_t full[SHARE_PATH];
    _snwprintf(full, SHARE_PATH, L"%s\\%s", dOn, fd.cFileName);
    wchar_t *t = share_read(full);
    if (t) {
      wchar_t *fl[3];
      wchar_t *p = t, *line = share_next_line(&p);
      if (line && share_split(line, fl, 3) >= 2) _snwprintf(who, cap, L"%s (%s)", fl[0], fl[1]);
      else lstrcpynW(who, fd.cFileName, cap);
      free(t);
    } else {
      lstrcpynW(who, fd.cFileName, cap);
    }
    found = TRUE;
  } while (!found && FindNextFileW(f, &fd));
  FindClose(f);
  return found;
}

/* Отправить запрос и дождаться ответа. Возвращает текст ответа (освобождает
   вызывающий) или NULL — тогда в out уже объяснение. */
static wchar_t *share_ask(const wchar_t *body, const wchar_t *tag, int waitSec, wchar_t *out,
                          int cap) {
  wchar_t root[MAX_PATH];
  share_root_copy(root);
  wchar_t who[200] = L"";
  if (!share_find_server(root, who, 200)) {
    ans_printf(out, cap,
               L"PLM\r\n\r\nСвоего логина SQL нет, а из коллег сейчас никто не раздаёт PLM.\r\n\r\n"
               L"Впишите логин в Настройках или попросите коллегу с доступом включить "
               L"«Раздавать PLM коллегам через меня».");
    return NULL;
  }
  wchar_t dReq[SHARE_PATH], dAns[SHARE_PATH];
  if (!share_dir(root, L"req", dReq) || !share_dir(root, L"ans", dAns)) {
    ans_printf(out, cap, L"PLM\r\n\r\nНет доступа на запись в общую папку\r\n%s", root);
    return NULL;
  }
  wchar_t user[128], pc[64];
  share_me(user, pc);
  wchar_t id[160];
  _snwprintf(id, 160, L"%s-%lu-%llu%s", pc, GetCurrentProcessId(), GetTickCount64(), tag);
  for (wchar_t *c = id; *c; c++)
    if (wcschr(L"\\/:*?\"<>| ", *c)) *c = L'_';
  wchar_t req[SHARE_PATH], ans[SHARE_PATH];
  _snwprintf(req, SHARE_PATH, L"%s\\%s.req", dReq, id);
  _snwprintf(ans, SHARE_PATH, L"%s\\%s.ans", dAns, id);
  ShareBuf b = {0};
  sb_add(&b, L"CPREQ1\n%s\nfrom\t%s (%s)\n", body, user, pc);
  BOOL sent = b.w && share_write(req, b.w);
  free(b.w);
  if (!sent) {
    ans_printf(out, cap, L"PLM\r\n\r\nНе удалось положить запрос в общую папку\r\n%s", root);
    return NULL;
  }
  for (int t = 0; t < waitSec * 5; t++) {
    Sleep(200);
    if (GetFileAttributesW(ans) == INVALID_FILE_ATTRIBUTES) continue;
    wchar_t *a = share_read(ans);
    if (!a) continue; /* ещё дописывается или занят антивирусом — следующий круг */
    DeleteFileW(ans);
    if (wcsncmp(a, L"CPANS1", 6) != 0) {
      free(a);
      break;
    }
    return a;
  }
  /* Два разных случая, и по ним разное делать: запрос так и лежит — его не
     забрали (у раздающего не видна папка или выключена раздача); запроса нет —
     забрали, но ответ не успел (долго идёт запрос к базе). */
  BOOL untouched = GetFileAttributesW(req) != INVALID_FILE_ATTRIBUTES;
  DeleteFileW(req); /* не взяли — забираем обратно, чтобы не выполнили впустую */
  if (untouched)
    ans_printf(out, cap,
               L"PLM\r\n\r\n%s не забрал запрос за %d с.\r\n\r\n"
               L"Программа там запущена, но запрос не видит: проверьте, что у раздающего "
               L"в Настройках та же общая папка и стоит «Раздавать PLM коллегам через меня», "
               L"а логин и пароль SQL вписаны.",
               who, waitSec);
  else
    ans_printf(out, cap,
               L"PLM\r\n\r\n%s забрал запрос, но ответ не пришёл за %d с.\r\n\r\n"
               L"Запрос к базе у раздающего идёт долго. Сколько именно — видно у него "
               L"в файле plm_share.log (папка программы).",
               who, waitSec);
  return NULL;
}

static BOOL share_lookup(const wchar_t *query, wchar_t *out, int cap) {
  g_plmLastLink[0] = 0;
  g_plmCount = 0;
  wchar_t q[400];
  lstrcpynW(q, query, 400);
  for (wchar_t *c = q; *c; c++)
    if (*c == L'\t' || *c == L'\r' || *c == L'\n') *c = L' ';
  wchar_t body[480];
  _snwprintf(body, 480, L"kind\tsearch\nq\t%s", q);
  body[479] = 0;
  wchar_t *a = share_ask(body, L"", 60, out, cap);
  if (!a) return FALSE;
  wchar_t via[200] = L"";
  wchar_t *p = a, *line;
  int n = 0;
  while ((line = share_next_line(&p)) != NULL) {
    if (!wcscmp(line, L"text")) break;
    wchar_t *f[8];
    int k = share_split(line, f, 8);
    if (!wcscmp(f[0], L"via") && k >= 2) lstrcpynW(via, f[1], 200);
    else if (!wcscmp(f[0], L"row") && k >= 8 && n < PLM_ROWS) {
      g_plmIds[n] = wcstol(f[1], NULL, 10);
      g_plmTmpl[n] = (int)wcstol(f[2], NULL, 10);
      g_plmTpId[n] = wcstol(f[3], NULL, 10);
      g_plmRealId[n] = wcstol(f[4], NULL, 10);
      lstrcpynW(g_plmEsi[n], f[5], PLM_COL1);
      lstrcpynW(g_plmTp[n], f[6], PLM_COL2);
      lstrcpynW(g_plmLinks[n], f[7], PLM_LINK);
      g_plmPf[n][0] = 0;
      n++;
    } else if (!wcscmp(f[0], L"pf") && k >= 3) {
      int r = (int)wcstol(f[1], NULL, 10);
      if (r >= 0 && r < n) lstrcpynW(g_plmPf[r], f[2], PLM_COL1);
    }
  }
  g_plmCount = n;
  if (n > 0) lstrcpynW(g_plmLastLink, g_plmLinks[0], PLM_LINK);
  ans_printf(out, cap, L"%s\r\n\r\nPLM через компьютер: %s", p ? p : L"", via[0] ? via : L"коллеги");
  free(a);
  return n > 0;
}

static void share_card(long id, BOOL verbose, wchar_t *out, int cap) {
  g_opsPendN = 0;
  g_cardDraw[0] = 0;
  wchar_t body[120];
  _snwprintf(body, 120, L"kind\tcard\nid\t%ld\nverbose\t%d", id, verbose ? 1 : 0);
  wchar_t *a = share_ask(body, L"", 100, out, cap);
  if (!a) return;
  wchar_t via[200] = L"";
  wchar_t *p = a, *line;
  while ((line = share_next_line(&p)) != NULL) {
    if (!wcscmp(line, L"text")) break;
    wchar_t *f[3];
    int k = share_split(line, f, 3);
    if (k < 2) continue;
    if (!wcscmp(f[0], L"via")) lstrcpynW(via, f[1], 200);
    else if (!wcscmp(f[0], L"op") && g_opsPendN < OPS_1C) lstrcpynW(g_opsPend[g_opsPendN++], f[1], PLM_COL1);
    /* путь к чертежу — из индекса коллеги; берём, только если файл виден и отсюда */
    else if (!wcscmp(f[0], L"draw") && GetFileAttributesW(f[1]) != INVALID_FILE_ATTRIBUTES)
      lstrcpynW(g_cardDraw, f[1], PLM_LINK);
  }
  ans_printf(out, cap, L"%s\r\n\r\n  PLM через компьютер: %s", p ? p : L"", via[0] ? via : L"коллеги");
  free(a);
}

/* Столбец «Заготовка» через раздающего: отдельным фоновым запросом. */
static void share_pf(PfJob *j) {
  wchar_t body[1500];
  size_t l = (size_t)_snwprintf(body, 1500, L"kind\tpf\nids\t");
  for (int k = 0; k < j->n; k++) {
    int w = _snwprintf(body + l, 1500 - l, k ? L",%ld" : L"%ld", j->ids[k]);
    if (w <= 0 || l + (size_t)w >= 1490) break;
    l += (size_t)w;
  }
  body[1499] = 0;
  wchar_t out[600];
  wchar_t *a = share_ask(body, L"-pf", 45, out, 600);
  if (!a) return;
  wchar_t *p = a, *line;
  while ((line = share_next_line(&p)) != NULL) {
    if (!wcscmp(line, L"text")) break;
    wchar_t *f[3];
    if (share_split(line, f, 3) < 3 || wcscmp(f[0], L"pfr")) continue;
    long id = wcstol(f[1], NULL, 10);
    for (int k = 0; k < j->n; k++)
      if (j->ids[k] == id) lstrcpynW(j->text[k], f[2], PLM_COL1);
  }
  free(a);
}
