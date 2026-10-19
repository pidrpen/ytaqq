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
#define ID_AI_CTX 515    /* «Контекст…» — окно контекста модели: 2048…16384 по кругу */
#define ID_AI_OFF 516    /* выключить и сразу выгрузить модель */
#define ID_AI_SOURCE 517
#define ID_AI_ZMODEL 518
#define ID_AI_ZKEY 519
#define ID_AI_ZSAVE 520
#define ID_AI_ZFORGET 521
static HWND g_aiSource, g_aiZModel, g_aiZKey, g_aiZSave, g_aiZForget, g_aiZInfo;
static BOOL ac_provider_change(int provider);

static HWND g_aiBtnReport, g_aiBtnLlama, g_aiBtnCtx, g_aiBtnOff;
static HWND g_aiChkOn, g_aiBtnTier[AI_TIERS], g_aiChkGpu, g_aiStat, g_aiBar, g_aiBtnDl, g_aiBtnTest, g_aiBtnDir,
    g_aiPathEdit, g_aiBtnBrowse;
static int g_aiTestId;          /* идёт «Проверить» */
static wchar_t g_aiTestMsg[300]; /* чем кончилась проверка */

static void ai_ui_refresh(void) {
  if (!g_aiChkOn) return;
  SendMessageW(g_aiSource, CB_SETCURSEL, g_aiProvider, 0);
  SendMessageW(g_aiChkOn, BM_SETCHECK, g_aiOn ? BST_CHECKED : BST_UNCHECKED, 0);
  EnableWindow(g_aiBtnOff, g_aiOn || g_aiProc != NULL);
  SendMessageW(g_aiChkGpu, BM_SETCHECK, g_aiGpu ? BST_CHECKED : BST_UNCHECKED, 0);
  for (int i = 0; i < AI_TIERS; i++) set_mark(g_aiBtnTier[i], g_aiTier == i && !g_aiModel[0], kAiTiers[i].button);
  wchar_t cx[80];
  _snwprintf(cx, 80, L"Контекст: %d токенов (история чата)", g_aiCtx);
  SetWindowTextW(g_aiBtnCtx, cx);
  const AiTier *t = &kAiTiers[g_aiTier];
  ULONGLONG total = 0;
  for (int i = 0; i < t->nf; i++) total += t->f[i].size;
  wchar_t s[420], dl[80];
  if (g_aiProvider) {
    wchar_t model[64]; MultiByteToWideChar(CP_UTF8, 0, g_aiZaiModel, -1, model, 64);
    swprintf(s, 420, L"Z.ai · %ls · %ls\r\n%ls", model, g_aiOn ? L"включён" : L"выключен", g_aiTestMsg[0] ? g_aiTestMsg : ai_zai_has_key() ? L"Ключ сохранён с защитой Windows. Нажмите «Проверить»." : L"Введите API-ключ и нажмите «Сохранить ключ».");
    ShowWindow(g_aiBar, SW_HIDE); dl[0] = 0;
  } else if (g_aiDl.active) {
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
                           : g_aiOn   ? (g_aiProc ? L"сервер запущен" : L"модель выгружена")
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
  if (g_aiProvider) return 0; /* SOYUZ cloud integration is a separate task. */
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
  g_aiChkOn = CreateWindowExW(0, L"BUTTON", L"Включить ИИ",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26, p, (HMENU)(INT_PTR)ID_AI_ON,
                              NULL, NULL);
  g_aiBtnOff = mk_btn(p, L"Выключить и выгрузить", ID_AI_OFF);
  g_aiSource = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_TABSTOP, 0, 0, 300, 180, p, (HMENU)(INT_PTR)ID_AI_SOURCE, NULL, NULL);
  SendMessageW(g_aiSource, CB_ADDSTRING, 0, (LPARAM)L"Локальная модель · без интернета");
  SendMessageW(g_aiSource, CB_ADDSTRING, 0, (LPARAM)L"Z.ai API · через интернет");
  g_aiZModel = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN | WS_TABSTOP | CBS_AUTOHSCROLL, 0, 0, 300, 200, p, (HMENU)(INT_PTR)ID_AI_ZMODEL, NULL, NULL);
  const wchar_t *models[] = {L"glm-4.5-flash", L"glm-4.7-flash", L"glm-4.6v-flash", L"glm-4.7", L"glm-5.3", L"glm-5.3-flash", L"glm-4.6v"};
  for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) SendMessageW(g_aiZModel, CB_ADDSTRING, 0, (LPARAM)models[i]);
  wchar_t model[64]; MultiByteToWideChar(CP_UTF8, 0, g_aiZaiModel, -1, model, 64); SetWindowTextW(g_aiZModel, model);
  g_aiZKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, 0, 0, 300, 26, p, (HMENU)(INT_PTR)ID_AI_ZKEY, NULL, NULL);
  SendMessageW(g_aiZKey, EM_SETLIMITTEXT, 1024, 0);
  SendMessageW(g_aiZKey, 0x1501, TRUE, (LPARAM)L"Вставьте обычный API-ключ Z.ai");
  g_aiZSave = mk_btn(p, L"Сохранить ключ", ID_AI_ZSAVE);
  g_aiZForget = mk_btn(p, L"Удалить ключ", ID_AI_ZFORGET);
  g_aiZInfo = CreateWindowExW(0, L"STATIC", L"Сообщения, картинки и данные файлов из задач передаются Z.ai. Нужен стандартный API-ключ (не Coding Plan). GLM-4.5-Flash, GLM-4.7-Flash и GLM-4.6V-Flash бесплатны; остальные модели могут списывать баланс. Одна задача делает несколько запросов. Модели на ПК не нужны. Изменения файлов — после «Применить».", WS_CHILD | SS_LEFT, 0, 0, 300, 100, p, NULL, NULL, NULL);
  for (int i = 0; i < AI_TIERS; i++) g_aiBtnTier[i] = mk_btn(p, kAiTiers[i].button, ID_AI_TIER + i);
  g_aiBtnCtx = mk_btn(p, L"", ID_AI_CTX);
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
  HWND f[] = {g_aiChkOn, g_aiBtnOff, g_aiChkGpu, g_aiBtnDl, g_aiBtnTest, g_aiBtnDir, g_aiBtnBrowse};
  for (int i = 0; i < (int)(sizeof(f) / sizeof(f[0])); i++) SendMessageW(f[i], WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  for (int i = 0; i < AI_TIERS; i++) SendMessageW(g_aiBtnTier[i], WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_aiStat, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
  SendMessageW(g_aiPathEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  HWND cloud[] = {g_aiSource, g_aiZModel, g_aiZKey, g_aiZSave, g_aiZForget, g_aiZInfo};
  for (int i = 0; i < 6; i++) SendMessageW(cloud[i], WM_SETFONT, (WPARAM)(i == 5 ? g_fontSmall : g_fontUi), TRUE);
  SendMessageW(g_aiPathEdit, 0x1501, TRUE, (LPARAM)L"своя модель .gguf — необязательно");
  ai_ui_refresh();
}

/* раскладка вкладки; возвращает новый y */
static int ai_ui_layout(int pad, int y, int fullW, int cw, int btnH, int gap, int lab, int browseW) {
  HWND cloud[] = {g_aiZModel, g_aiZKey, g_aiZSave, g_aiZForget, g_aiZInfo};
  HWND local[] = {g_aiBtnTier[0], g_aiBtnTier[1], g_aiBtnTier[2], g_aiBtnTier[3], g_aiChkGpu, g_aiBtnDl, g_aiBtnDir, g_aiBtnLlama, g_aiPathEdit, g_aiBtnBrowse};
  ShowWindow(g_aiSource, g_layOpen ? SW_SHOW : SW_HIDE);
  for (int i = 0; i < 5; i++) { ShowWindow(cloud[i], g_layOpen && g_aiProvider ? SW_SHOW : SW_HIDE); if (!g_aiProvider) MoveWindow(cloud[i], 0, -4000, 300, 26, FALSE); }
  for (int i = 0; i < 10; i++) { ShowWindow(local[i], g_layOpen && !g_aiProvider ? SW_SHOW : SW_HIDE); if (g_aiProvider) MoveWindow(local[i], 0, -4000, 300, 26, FALSE); }
  if (g_aiProvider) { ShowWindow(g_aiBar, SW_HIDE); MoveWindow(g_aiBar, 0, -4000, 300, 6, FALSE); }
  int halfOn = (fullW - gap) / 2;
  set_place(g_aiChkOn, pad, y, halfOn, btnH);
  set_place(g_aiBtnOff, pad + halfOn + gap, y, fullW - halfOn - gap, btnH);
  y += btnH + gap + lab;
  set_place(g_aiSource, pad, y, fullW, btnH + 130);
  y += btnH + gap + lab;
  if (g_aiProvider) {
    set_place(g_aiZModel, pad, y, fullW, btnH + 160); y += btnH + gap + lab;
    set_place(g_aiZKey, pad, y, fullW, btnH); y += btnH + gap;
    int half = (fullW - gap) / 2;
    set_place(g_aiZSave, pad, y, half, btnH); set_place(g_aiZForget, pad + half + gap, y, fullW - half - gap, btnH); y += btnH + gap;
    set_place(g_aiZInfo, pad, y, fullW, 96); y += 96 + gap;
    set_place(g_aiStat, pad, y, fullW, 56); y += 56 + gap;
    set_place(g_aiBtnTest, pad, y, half, btnH); set_place(g_aiBtnReport, pad + half + gap, y, fullW - half - gap, btnH); y += btnH + gap;
    set_place(g_aiBtnCtx, pad, y, fullW, btnH); return y + btnH + gap;
  }
  y += set_grid(g_aiBtnTier, AI_TIERS, 2, pad, y, fullW, btnH, gap);
  set_place(g_aiChkGpu, pad, y, fullW, btnH);
  y += btnH + gap;
  set_place(g_aiBtnCtx, pad, y, fullW, btnH);
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
  ai_ui_label(hdc, g_aiSource, L"источник ИИ", rc);
  if (g_aiProvider) {
    ai_ui_label(hdc, g_aiZModel, L"модель Z.ai (можно ввести имя)", rc);
    ai_ui_label(hdc, g_aiZKey, L"API-ключ (не передаётся в отчёт)", rc); return;
  }
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
      _snwprintf(g_aiTestMsg, 300, L"Работает: «%s» за %.1f с%s%s", a, r->ms / 1000.0, g_aiRunNote[0] ? L". " : L"",
                 g_aiRunNote);
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
  wchar_t apiModel[64]; MultiByteToWideChar(CP_UTF8, 0, g_aiZaiModel, -1, apiModel, 64);
  size_t n = (size_t)_snwprintf(
      r, cap,
      L"CursorPad %s, Windows %lu.%lu.%lu\r\nПамять: %.1f ГБ, свободно %.1f ГБ; ядер %d; диск свободно %.1f ГБ\r\n"
      L"Источник: %s; модель API: %s\r\nИИ: %s, локальный уровень %s, видеокарта %s\r\nПапка: %s\r\nllama-server: %s\r\nМодель: %s %s\r\n"
      L"Своя модель: %s\r\nСейчас: %s\r\nПроверка: %s\r\n\r\n--- журнал ---\r\n",
      APP_VERSION_STR, ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber, ms.ullTotalPhys / 1e9,
      ms.ullAvailPhys / 1e9, ai_cores(), fr.QuadPart / 1e9, g_aiProvider ? L"Z.ai API" : L"Локальный", g_aiProvider ? apiModel : L"не используется", g_aiOn ? L"включён" : L"выключен",
      kAiTiers[g_aiTier].button, g_aiGpu ? L"да" : L"нет", d, exe, m, mm, g_aiModel[0] ? g_aiModel : L"нет",
      g_aiDl.msg, g_aiTestMsg);
  AcquireSRWLockShared(&g_aiLogLock);
  if (n < cap) lstrcpynW(r + n, g_aiLog[0] ? g_aiLog : L"(пусто)\r\n", (int)(cap - n));
  ReleaseSRWLockShared(&g_aiLogLock);
  n = wcslen(r);
  wchar_t log[MAX_PATH];
  _snwprintf(log, MAX_PATH, L"%s\\server.log", d);
  HANDLE h = CreateFileW(log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (!g_aiProvider && h != INVALID_HANDLE_VALUE && n + 100 < cap) {
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
  if (id == ID_AI_SOURCE && code == CBN_SELCHANGE) {
    int selected = (int)SendMessageW(g_aiSource, CB_GETCURSEL, 0, 0);
    if (selected == g_aiProvider) return TRUE;
    if (g_aiActive || !ac_provider_change(selected)) { lstrcpynW(g_aiTestMsg, L"Дождитесь текущего ответа или нажмите Стоп; закройте ожидающий план.", 300); ai_ui_refresh(); return TRUE; }
    if (selected == 0 || selected == 1) { ai_server_stop(); g_aiProvider = selected; g_aiTestMsg[0] = g_aiRunNote[0] = 0; ai_save(); ai_ui_refresh(); layout_settings(); }
    return TRUE;
  }
  if (id == ID_AI_ZMODEL && (code == CBN_SELCHANGE || code == CBN_KILLFOCUS)) {
    if (g_aiActive) {
      wchar_t current[64]; MultiByteToWideChar(CP_UTF8, 0, g_aiZaiModel, -1, current, 64); SetWindowTextW(g_aiZModel, current);
      lstrcpynW(g_aiTestMsg, L"Модель можно изменить после текущего ответа.", 300); ai_ui_refresh(); return TRUE;
    }
    wchar_t name[64]; char model[64];
    int selected = (int)SendMessageW(g_aiZModel, CB_GETCURSEL, 0, 0);
    if (code == CBN_SELCHANGE && selected >= 0) SendMessageW(g_aiZModel, CB_GETLBTEXT, selected, (LPARAM)name);
    else GetWindowTextW(g_aiZModel, name, 64);
    wchar_t *trimmed = name; while (*trimmed == L' ' || *trimmed == L'\t') trimmed++;
    size_t length = wcslen(trimmed); while (length && (trimmed[length - 1] == L' ' || trimmed[length - 1] == L'\t')) trimmed[--length] = 0;
    if (!WideCharToMultiByte(CP_UTF8, 0, trimmed, -1, model, 64, NULL, NULL) || !ai_zai_model_valid(model)) {
      wchar_t current[64]; MultiByteToWideChar(CP_UTF8, 0, g_aiZaiModel, -1, current, 64); SetWindowTextW(g_aiZModel, current);
      _snwprintf(g_aiTestMsg, 300, L"Имя не сохранено. Выбрана %s. Укажите glm-… латиницей без кавычек.", current);
    } else { lstrcpynA(g_aiZaiModel, model, 64); g_aiTestMsg[0] = 0; ai_save(); }
    ai_ui_refresh(); return TRUE;
  }
  if ((id == ID_AI_ZSAVE || id == ID_AI_ZFORGET) && code == BN_CLICKED) {
    if (g_aiActive || !ac_provider_change(g_aiProvider)) { lstrcpynW(g_aiTestMsg, L"Дождитесь ответа или нажмите Стоп перед изменением ключа.", 300); ai_ui_refresh(); return TRUE; }
    BOOL ok;
    if (id == ID_AI_ZSAVE) {
      wchar_t key[1025]; GetWindowTextW(g_aiZKey, key, 1025); ok = ai_zai_store_key(key); SecureZeroMemory(key, sizeof(key));
      lstrcpynW(g_aiTestMsg, ok ? L"Ключ сохранён. Нажмите «Проверить»." : L"Не удалось сохранить ключ: проверьте ввод и доступ к папке данных.", 300);
    } else { wchar_t path[MAX_PATH]; ai_zai_key_path(path); ok = DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND; lstrcpynW(g_aiTestMsg, ok ? L"Ключ удалён." : L"Не удалось удалить ключ.", 300); }
    SetWindowTextW(g_aiZKey, L""); ai_ui_refresh(); return TRUE;
  }
  if (id == ID_AI_CTX && code == BN_CLICKED) {
    static const int kCtx[] = {2048, 4096, 8192, 16384};
    int i = 0;
    while (i < 3 && kCtx[i] != g_aiCtx) i++;
    g_aiCtx = kCtx[(i + 1) % 4];
    ai_server_stop(); /* при следующем запросе запустится с новым окном */
    ai_save();
    ai_ui_refresh();
    return TRUE;
  }
  if (id == ID_AI_ON && code == BN_CLICKED) {
    g_aiOn = SendMessageW(g_aiChkOn, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!g_aiOn) ai_disable();
    if (!g_aiOn) g_aiTestId = 0;
    ai_save();
    g_aiTestMsg[0] = 0;
    ai_ui_refresh();
    return TRUE;
  }
  if (id == ID_AI_OFF && code == BN_CLICKED) {
    ai_disable();
    g_aiTestId = 0;
    ai_save();
    lstrcpynW(g_aiTestMsg, ai_ext() ? L"ИИ выключен. Внешний сервер управляется отдельно."
                                  : L"ИИ выключен, модель выгружена из памяти. Без отключения выгружается после 10 минут простоя.", 300);
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
    else if (g_aiProvider && !ai_zai_has_key()) lstrcpynW(g_aiTestMsg, L"Сначала сохраните API-ключ Z.ai.", 300);
    else {
      g_aiTestId = ai_ask_async(L"Отвечай кратко, по-русски.",
                                L"Расшифруй обозначение «Круг 20 ГОСТ 2590-2006» одной строкой.", 180000, hwnd,
                                WM_AI_TEST, NULL);
      if (g_aiTestId) lstrcpynW(g_aiTestMsg, g_aiProvider ? L"Проверяю Z.ai через интернет…" : L"Спрашиваю модель… первый раз она грузится до минуты", 300);
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
