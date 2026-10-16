/* ai_ui.c — вкладка «ИИ» в Настройках: включить, уровень, видеокарта,
   скачать, проверить, своя модель. Само ИИ — ai_local.c. */

#define ID_AI_ON 500
#define ID_AI_TIER 501 /* 501..504 */
#define ID_AI_GPU 505
#define ID_AI_DL 506
#define ID_AI_TEST 507
#define ID_AI_DIR 508
#define ID_AI_PATH 509
#define ID_AI_BROWSE 510
#define ID_AI_STAT 511
#define ID_AI_BAR 512
#define ID_AI_REPORT 513 /* «Скопировать отчёт» */
#define ID_AI_LLAMA 514  /* «llama.cpp…» — указать скачанный руками архив */

static HWND g_aiBtnReport, g_aiBtnLlama;
static HWND g_aiChkOn, g_aiBtnTier[AI_TIERS], g_aiChkGpu, g_aiStat, g_aiBar, g_aiBtnDl, g_aiBtnTest, g_aiBtnDir,
    g_aiPathEdit, g_aiBtnBrowse;
static int g_aiTestId;          /* идёт «Проверить» */
static wchar_t g_aiTestMsg[300]; /* чем кончилась проверка */

static void ai_ui_refresh(void) {
  if (!g_aiChkOn) return;
  SendMessageW(g_aiChkOn, BM_SETCHECK, g_aiOn ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(g_aiChkGpu, BM_SETCHECK, g_aiGpu ? BST_CHECKED : BST_UNCHECKED, 0);
  for (int i = 0; i < AI_TIERS; i++) set_mark(g_aiBtnTier[i], g_aiTier == i && !g_aiModel[0], kAiTiers[i].button);
  const AiTier *t = &kAiTiers[g_aiTier];
  ULONGLONG total = 0;
  for (int i = 0; i < t->nf; i++) total += t->f[i].size;
  wchar_t s[420], dl[80];
  if (g_aiDl.active) {
    _snwprintf(dl, 80, L"Отменить скачивание");
    LONGLONG tot = g_aiDl.total, done = g_aiDl.done;
    int pm = tot > 0 ? (int)(done * 1000 / tot) : 0;
    SendMessageW(g_aiBar, PBM_SETPOS, pm > 1000 ? 1000 : pm, 0);
    ShowWindow(g_aiBar, SW_SHOW);
    _snwprintf(s, 420, L"%s\r\n%.2f из %.2f ГБ", g_aiDl.msg, done / 1e9, tot / 1e9);
  } else {
    ShowWindow(g_aiBar, SW_HIDE);
    wchar_t exe[MAX_PATH];
    BOOL haveSrv = ai_server_exe(g_aiGpu, exe) || ai_server_exe(!g_aiGpu, exe);
    BOOL haveModel = g_aiModel[0] ? ai_file_exists(g_aiModel) : ai_tier_ready(g_aiTier);
    if (haveModel && haveSrv) _snwprintf(dl, 80, L"Скачано");
    else _snwprintf(dl, 80, L"Скачать модель (%.1f ГБ)", total / 1e9);
    const wchar_t *name = t->model;
    if (g_aiModel[0]) {
      name = wcsrchr(g_aiModel, L'\\');
      name = name ? name + 1 : g_aiModel;
    }
    const wchar_t *state = !haveModel ? L"не скачана" : !haveSrv ? L"нет llama.cpp — нажмите «Скачать»"
                           : g_aiOn   ? L"включена"
                                      : L"скачана, ИИ выключен";
    if (g_aiTestMsg[0]) _snwprintf(s, 420, L"%s — %s\r\n%s", name, state, g_aiTestMsg);
    else if (g_aiDl.msg[0]) _snwprintf(s, 420, L"%s — %s\r\n%s", name, state, g_aiDl.msg);
    else _snwprintf(s, 420, L"%s — %s\r\n%s", name, state, g_aiModel[0] ? L"своя модель" : t->hint);
  }
  s[419] = 0;
  SetWindowTextW(g_aiStat, s);
  SetWindowTextW(g_aiBtnDl, dl);
  EnableWindow(g_aiBtnDl, g_aiDl.active || wcscmp(dl, L"Скачано") != 0);
  SetWindowTextW(g_aiBtnTest, g_aiTestId ? L"Отменить проверку" : L"Проверить");
  InvalidateRect(g_aiBtnDl, NULL, FALSE);
}

/* «Сопоставить с Союзом» (match.c) спрашивает, когда правила не уверены. ИИ выключен
   или не ответил — 0, и остаётся порядок правил. Два таймаута подряд — ИИ не
   спрашиваем 10 минут: на медленном компьютере сотня неясных строк иначе
   растянулась бы на час. */
static volatile LONG g_aiMtFails;
static volatile DWORD g_aiMtPause;

static int ai_mt_rank(const wchar_t *des, const wchar_t *name, MtAiCand *cand, int n, int *order) {
  int all = n;
  if (!ai_ready() || n < 2) return 0;
  if (g_aiMtFails >= 2) {
    if (GetTickCount() - g_aiMtPause < 10 * 60 * 1000) return 0;
    InterlockedExchange(&g_aiMtFails, 0);
  }
  if (n > 40) n = 40;
  wchar_t q[600], *txt[40];
  const wchar_t *cp[40];
  _snwprintf(q, 600, L"%s %s", des ? des : L"", name ? name : L"");
  q[599] = 0;
  for (int i = 0; i < n; i++) {
    txt[i] = (wchar_t *)malloc(600 * sizeof(wchar_t));
    if (txt[i]) {
      _snwprintf(txt[i], 600, L"%s %s", cand[i].des ? cand[i].des : L"", cand[i].name ? cand[i].name : L"");
      txt[i][599] = 0;
    }
    cp[i] = txt[i] ? txt[i] : L"";
  }
  int pick[40];
  DWORD t0 = GetTickCount();
  int got = ai_rank_candidates(q, cp, n, pick, 30000);
  for (int i = 0; i < n; i++) free(txt[i]);
  if (!got) {
    if (GetTickCount() - t0 >= 29000 && InterlockedIncrement(&g_aiMtFails) >= 2) g_aiMtPause = GetTickCount();
    return 0;
  }
  InterlockedExchange(&g_aiMtFails, 0);
  /* названные ИИ — вперёд, остальные в порядке правил */
  BOOL used[40] = {0};
  int k = 0;
  for (int i = 0; i < got; i++) {
    order[k++] = pick[i];
    used[pick[i]] = TRUE;
  }
  for (int i = 0; i < all; i++)
    if (i >= 40 || !used[i]) order[k++] = i;
  return 1;
}

static void ai_ui_create(HWND p) {
  ai_load();
  mt_set_ai_ranker(ai_mt_rank);
  g_aiChkOn = CreateWindowExW(0, L"BUTTON", L"Включить ИИ (без интернета)",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26, p, (HMENU)(INT_PTR)ID_AI_ON,
                              NULL, NULL);
  for (int i = 0; i < AI_TIERS; i++) g_aiBtnTier[i] = mk_btn(p, kAiTiers[i].button, ID_AI_TIER + i);
  g_aiChkGpu = CreateWindowExW(0, L"BUTTON", L"Считать на видеокарте (если в ней от 4 ГБ)",
                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26, p, (HMENU)(INT_PTR)ID_AI_GPU,
                               NULL, NULL);
  g_aiStat = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 200, 40, p,
                             (HMENU)(INT_PTR)ID_AI_STAT, NULL, NULL);
  g_aiBar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD, 0, 0, 200, 6, p, (HMENU)(INT_PTR)ID_AI_BAR, NULL, NULL);
  SendMessageW(g_aiBar, PBM_SETRANGE32, 0, 1000);
  g_aiBtnDl = mk_btn(p, L"Скачать модель", ID_AI_DL);
  g_aiBtnTest = mk_btn(p, L"Проверить", ID_AI_TEST);
  g_aiBtnDir = mk_btn(p, L"Папка ИИ", ID_AI_DIR);
  g_aiBtnReport = mk_btn(p, L"Скопировать отчёт", ID_AI_REPORT);
  g_aiBtnLlama = mk_btn(p, L"llama.cpp…", ID_AI_LLAMA);
  SendMessageW(g_aiBtnReport, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_aiBtnLlama, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  g_aiPathEdit = CreateWindowExW(0, L"EDIT", g_aiModel, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 200, 26, p,
                                 (HMENU)(INT_PTR)ID_AI_PATH, NULL, NULL);
  g_aiBtnBrowse = mk_btn(p, L"Обзор…", ID_AI_BROWSE);
  HWND f[] = {g_aiChkOn, g_aiChkGpu, g_aiBtnDl, g_aiBtnTest, g_aiBtnDir, g_aiBtnBrowse};
  for (int i = 0; i < (int)(sizeof(f) / sizeof(f[0])); i++) SendMessageW(f[i], WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  for (int i = 0; i < AI_TIERS; i++) SendMessageW(g_aiBtnTier[i], WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_aiStat, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
  SendMessageW(g_aiPathEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  SendMessageW(g_aiPathEdit, 0x1501, TRUE, (LPARAM)L"своя модель .gguf — необязательно");
  ai_ui_refresh();
}

/* раскладка вкладки; возвращает новый y */
static int ai_ui_layout(int pad, int y, int fullW, int cw, int btnH, int gap, int lab, int browseW) {
  set_place(g_aiChkOn, pad, y, fullW, btnH);
  y += btnH + gap + lab;
  y += set_grid(g_aiBtnTier, AI_TIERS, 2, pad, y, fullW, btnH, gap);
  set_place(g_aiChkGpu, pad, y, fullW, btnH);
  y += btnH + gap;
  set_place(g_aiStat, pad, y, fullW, 44);
  set_place(g_aiBar, pad, y + 46, fullW, 6);
  y += 54 + gap;
  set_place(g_aiBtnDl, pad, y, fullW, btnH);
  y += btnH + gap;
  {
    int half = (fullW - gap) / 2;
    set_place(g_aiBtnTest, pad, y, half, btnH);
    set_place(g_aiBtnReport, pad + half + gap, y, fullW - half - gap, btnH);
    y += btnH + gap;
    set_place(g_aiBtnDir, pad, y, half, btnH);
    set_place(g_aiBtnLlama, pad + half + gap, y, fullW - half - gap, btnH);
  }
  y += btnH + gap + lab;
  set_place(g_aiPathEdit, pad, y, fullW - browseW - gap, btnH);
  set_place(g_aiBtnBrowse, cw - browseW, y, browseW, btnH);
  y += btnH + gap;
  return y;
}

static void ai_ui_label(HDC hdc, HWND under, const wchar_t *text, RECT rc) {
  if (!under) return;
  RECT wr;
  GetWindowRect(under, &wr);
  MapWindowPoints(HWND_DESKTOP, g_setHwnd, (POINT *)&wr, 2);
  RECT a = {14, wr.top - 18, rc.right - 14, wr.top - 2};
  draw_set_label(hdc, text, a, rc.bottom);
}

static void ai_ui_paint(HDC hdc, RECT rc) {
  if (g_tab != SEC_AI) return;
  ai_ui_label(hdc, g_aiBtnTier[0], L"модель — по силе компьютера", rc);
  ai_ui_label(hdc, g_aiPathEdit, L"или своя модель GGUF", rc);
}

/* ответ на «Проверить» */
static void ai_ui_test_done(AiResult *r) {
  if (!r) return;
  if (r->id == g_aiTestId) {
    g_aiTestId = 0;
    if (r->status == AI_OK) {
      wchar_t a[80];
      lstrcpynW(a, r->text, 80);
      for (wchar_t *c = a; *c; c++)
        if (*c == L'\r' || *c == L'\n') *c = L' ';
      _snwprintf(g_aiTestMsg, 300, L"Работает: «%s» за %.1f с", a, r->ms / 1000.0);
    } else {
      _snwprintf(g_aiTestMsg, 300, L"Не вышло: %s. Подробности — «Скопировать отчёт»", r->text);
    }
    g_aiTestMsg[299] = 0;
    ai_ui_refresh();
  }
  ai_result_free(r);
}

static void ai_ui_apply_path(void) {
  wchar_t p[MAX_PATH];
  GetWindowTextW(g_aiPathEdit, p, MAX_PATH);
  /* кавычки вокруг пути — частое «Копировать как путь» из проводника */
  wchar_t *s = p;
  while (*s == L' ' || *s == L'"') s++;
  size_t n = wcslen(s);
  while (n && (s[n - 1] == L' ' || s[n - 1] == L'"')) s[--n] = 0;
  if (!wcscmp(s, g_aiModel)) return;
  lstrcpynW(g_aiModel, s, MAX_PATH);
  ai_server_stop(); /* следующий запрос поднимет сервер с новой моделью */
  ai_save();
  ai_ui_refresh();
}

/* «Скопировать отчёт»: всё, что нужно, чтобы понять, что сломалось, — в буфер обмена */
static void ai_ui_copy_report(HWND hwnd) {
  size_t cap = 24000;
  wchar_t *r = (wchar_t *)calloc(cap, sizeof(wchar_t));
  if (!r) return;
  MEMORYSTATUSEX ms = {sizeof(ms)};
  GlobalMemoryStatusEx(&ms);
  wchar_t d[MAX_PATH], exe[MAX_PATH] = L"нет", m[MAX_PATH] = L"нет", mm[MAX_PATH] = L"";
  ai_dir(d, NULL);
  ULARGE_INTEGER fr = {{0}};
  GetDiskFreeSpaceExW(g_dataDir, &fr, NULL, NULL);
  if (!ai_server_exe(g_aiGpu, exe) && !ai_server_exe(!g_aiGpu, exe)) lstrcpynW(exe, L"нет", MAX_PATH);
  if (!ai_pick_model(m, mm)) lstrcpynW(m, L"нет", MAX_PATH);
  OSVERSIONINFOW ov = {sizeof(ov)};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  GetVersionExW(&ov);
#pragma GCC diagnostic pop
  size_t n = (size_t)_snwprintf(
      r, cap,
      L"CursorPad %s, Windows %lu.%lu.%lu\r\nПамять: %.1f ГБ, свободно %.1f ГБ; ядер %d; диск свободно %.1f ГБ\r\n"
      L"ИИ: %s, уровень %s, видеокарта %s\r\nПапка: %s\r\nllama-server: %s\r\nМодель: %s %s\r\n"
      L"Своя модель: %s\r\nСейчас: %s\r\nПроверка: %s\r\n\r\n--- журнал ---\r\n",
      APP_VERSION_STR, ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber, ms.ullTotalPhys / 1e9,
      ms.ullAvailPhys / 1e9, ai_cores(), fr.QuadPart / 1e9, g_aiOn ? L"включён" : L"выключен",
      kAiTiers[g_aiTier].button, g_aiGpu ? L"да" : L"нет", d, exe, m, mm, g_aiModel[0] ? g_aiModel : L"нет",
      g_aiDl.msg, g_aiTestMsg);
  AcquireSRWLockShared(&g_aiLogLock);
  if (n < cap) lstrcpynW(r + n, g_aiLog[0] ? g_aiLog : L"(пусто)\r\n", (int)(cap - n));
  ReleaseSRWLockShared(&g_aiLogLock);
  n = wcslen(r);
  wchar_t log[MAX_PATH];
  _snwprintf(log, MAX_PATH, L"%s\\server.log", d);
  HANDLE h = CreateFileW(log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (h != INVALID_HANDLE_VALUE && n + 100 < cap) {
    char b[3000];
    DWORD sz = GetFileSize(h, NULL), got = 0;
    if (sz > sizeof(b) - 1) SetFilePointer(h, (LONG)(sz - (sizeof(b) - 1)), NULL, FILE_BEGIN);
    ReadFile(h, b, sizeof(b) - 1, &got, NULL);
    b[got] = 0;
    n += (size_t)_snwprintf(r + n, cap - n, L"\r\n--- конец server.log ---\r\n");
    if (n < cap) MultiByteToWideChar(CP_UTF8, 0, b, -1, r + n, (int)(cap - n));
  }
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  r[cap - 1] = 0;
  BOOL ok = FALSE;
  if (OpenClipboard(hwnd)) {
    EmptyClipboard();
    size_t bytes = (wcslen(r) + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
      memcpy(GlobalLock(g), r, bytes);
      GlobalUnlock(g);
      ok = SetClipboardData(CF_UNICODETEXT, g) != NULL;
      if (!ok) GlobalFree(g);
    }
    CloseClipboard();
  }
  free(r);
  _snwprintf(g_aiTestMsg, 300, ok ? L"Отчёт скопирован — вставьте его в чат (Ctrl+V)" : L"Не удалось скопировать отчёт");
  ai_ui_refresh();
}

static wchar_t g_aiZipPick[MAX_PATH];

static DWORD WINAPI ai_ui_unzip_thread(LPVOID p) {
  (void)p;
  const wchar_t *f = g_aiZipPick, *nm = wcsrchr(f, L'\\');
  nm = nm ? nm + 1 : f;
  BOOL gpu = wcsstr(nm, L"vulkan") || wcsstr(nm, L"cuda");
  wchar_t dir[MAX_PATH], exe[MAX_PATH];
  ai_mkdirs();
  ai_dir(dir, gpu ? L"llama-vk" : L"llama");
  g_aiSrvExe[0] = 0;
  BOOL ok = ai_unzip(f, dir) && ai_server_exe(gpu, exe);
  ai_log(L"llama.cpp из %s → %s: %s", f, dir, ok ? L"готово" : L"не вышло");
  ai_dl_say(ok ? L"llama.cpp распакован. Нажмите «Проверить»" : L"Не распаковался архив — в нём нет llama-server.exe?");
  g_aiTestMsg[0] = 0;
  ai_save();
  InterlockedExchange(&g_aiDl.active, 0);
  if (g_aiDlNotify) PostMessageW(g_aiDlNotify, WM_AI_UI, 0, 0);
  return 0;
}

/* «llama.cpp…»: архив llama-…-bin-win-…-x64.zip, скачанный руками, или сам llama-server.exe */
static void ai_ui_pick_llama(HWND hwnd) {
  wchar_t f[MAX_PATH] = L"";
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = hwnd;
  of.lpstrFilter = L"llama.cpp (zip или llama-server.exe)\0*.zip;llama-server.exe\0Все файлы\0*.*\0";
  of.lpstrFile = f;
  of.nMaxFile = MAX_PATH;
  of.lpstrTitle = L"Архив llama.cpp для Windows";
  of.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&of)) return;
  const wchar_t *ext = wcsrchr(f, L'.');
  if (ext && !_wcsicmp(ext, L".exe")) {
    lstrcpynW(g_aiSrvExe, f, MAX_PATH);
    ai_log(L"llama-server указан руками: %s", f);
    _snwprintf(g_aiTestMsg, 300, L"llama-server: %s", f);
  } else {
    if (g_aiDl.active) return;
    lstrcpynW(g_aiZipPick, f, MAX_PATH);
    g_aiDlNotify = hwnd;
    g_aiDl.msg[0] = 0;
    InterlockedExchange(&g_aiDl.active, 1);
    _snwprintf(g_aiTestMsg, 300, L"Распаковываю llama.cpp…");
    HANDLE th = CreateThread(NULL, 0, ai_ui_unzip_thread, NULL, 0, NULL); /* не держать поток окон */
    if (th) CloseHandle(th);
    else InterlockedExchange(&g_aiDl.active, 0);
  }
  ai_server_stop();
  ai_save();
  ai_ui_refresh();
}

/* TRUE — команда наша */
static BOOL ai_ui_command(HWND hwnd, int id, int code) {
  if (id == ID_AI_ON && code == BN_CLICKED) {
    g_aiOn = SendMessageW(g_aiChkOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!g_aiOn) ai_server_stop();
    ai_save();
    g_aiTestMsg[0] = 0;
    ai_ui_refresh();
    return TRUE;
  }
  if (id >= ID_AI_TIER && id < ID_AI_TIER + AI_TIERS) {
    if (g_aiDl.active) return TRUE; /* уровень не меняем посреди скачивания */
    g_aiTier = id - ID_AI_TIER;
    g_aiModel[0] = 0;
    SetWindowTextW(g_aiPathEdit, L"");
    ai_server_stop();
    ai_save();
    g_aiTestMsg[0] = 0;
    g_aiDl.msg[0] = 0;
    ai_ui_refresh();
    return TRUE;
  }
  if (id == ID_AI_GPU && code == BN_CLICKED) {
    g_aiGpu = SendMessageW(g_aiChkGpu, BM_GETCHECK, 0, 0) == BST_CHECKED;
    ai_server_stop();
    ai_save();
    ai_ui_refresh();
    return TRUE;
  }
  if (id == ID_AI_DL) {
    g_aiTestMsg[0] = 0;
    ai_download_toggle(hwnd);
    ai_ui_refresh();
    return TRUE;
  }
  if (id == ID_AI_TEST) {
    if (g_aiTestId) {
      ai_cancel(g_aiTestId);
      return TRUE;
    }
    if (!g_aiOn) _snwprintf(g_aiTestMsg, 300, L"Сначала включите ИИ галочкой выше");
    else {
      g_aiTestId = ai_ask_async(L"Отвечай кратко, по-русски.",
                                L"Расшифруй обозначение «Круг 20 ГОСТ 2590-2006» одной строкой.", 180000, hwnd,
                                WM_AI_TEST, NULL);
      if (g_aiTestId) _snwprintf(g_aiTestMsg, 300, L"Спрашиваю модель… первый раз она грузится до минуты");
      else {
        wchar_t exe[MAX_PATH], m[MAX_PATH], mm[MAX_PATH];
        if (!ai_server_exe(g_aiGpu, exe) && !ai_server_exe(!g_aiGpu, exe))
          _snwprintf(g_aiTestMsg, 300, L"Не вышло: нет llama.cpp — «Скачать» или «llama.cpp…»");
        else if (!ai_pick_model(m, mm))
          _snwprintf(g_aiTestMsg, 300, L"Не вышло: модель не скачана или файла своей модели нет");
        else _snwprintf(g_aiTestMsg, 300, L"Не вышло: %s", ai_status_text(AI_NO_MODEL));
        ai_log(L"Проверить: %s", g_aiTestMsg);
      }
    }
    ai_ui_refresh();
    return TRUE;
  }
  if (id == ID_AI_REPORT) {
    ai_ui_copy_report(hwnd);
    return TRUE;
  }
  if (id == ID_AI_LLAMA) {
    ai_ui_pick_llama(hwnd);
    return TRUE;
  }
  if (id == ID_AI_DIR) {
    wchar_t d[MAX_PATH];
    ai_dir(d, NULL);
    CreateDirectoryW(d, NULL);
    ShellExecuteW(hwnd, L"open", d, NULL, NULL, SW_SHOWNORMAL);
    return TRUE;
  }
  if (id == ID_AI_BROWSE) {
    wchar_t f[MAX_PATH] = L"";
    OPENFILENAMEW of;
    memset(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = hwnd;
    of.lpstrFilter = L"Модели GGUF\0*.gguf\0Все файлы\0*.*\0";
    of.lpstrFile = f;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Своя модель для ИИ";
    of.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&of)) {
      SetWindowTextW(g_aiPathEdit, f);
      ai_ui_apply_path();
    }
    return TRUE;
  }
  if (id == ID_AI_PATH) {
    if (code == EN_KILLFOCUS) ai_ui_apply_path();
    return TRUE;
  }
  return FALSE;
}
