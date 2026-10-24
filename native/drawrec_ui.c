/* ---- Окно «Распознать чертёж» (распознавание — drawrec.c) ----------------------

   Сканы чертежей, спецификаций и ТП — перетащить в окно, «Добавить файлы…»,
   Ctrl+V (файлы из Проводника или снимок) или «С экрана» (выделить штамп
   мышью). Каждая страница TIFF — своя строка. В фоне Tesseract читает штамп
   (обозначение, наименование, материал, масса, масштаб, лист) и таблицу
   спецификации; тип листа — по форме штампа.

   Вкладки «Листы» и «Спецификация» — таблицы как в «Выгрузке из PLM»: клик —
   ячейка, Shift / мышью — диапазон, Ctrl+C — в Excel по ячейкам. Жёлтым —
   прочитано неуверенно или не сходится с шаблоном: проверьте. F2, Enter или
   двойной щелчок — поправить. Внизу: строки для Excel, в пакет PLM, CSV для
   автозагрузки Союза (soyuz-plm), открыть лист.

   Всё на этом компьютере. Локальный ИИ (ai.h), если включён, только
   подсказывает в сомнительных графах штампа. */

#define ID_DR_ADD 920
#define ID_DR_SCREEN 921
#define ID_DR_AGAIN 922
#define ID_DR_STOP 923
#define ID_DR_CLEAR 924
#define ID_DR_TAB 925 /* +вкладка: 925, 926 */
#define ID_DR_COPY 928
#define ID_DR_PKG 929
#define ID_DR_CSV 930
#define ID_DR_OPEN 931
#define ID_DR_TIMER 932
#define ID_DR_SORT 933 /* «Разложить по папкам…» — drawrec_sort.c */
#define ID_DR_ROUTE 934 /* «Маршрут обработки» — techno.c, итог в чат ИИ */
#define WM_DR_ADD (WM_APP + 140)  /* lParam — DrItem*: новая страница (ещё не прочитана) */
#define WM_DR_DONE (WM_APP + 141) /* lParam — DrItem*: страница прочитана */
/* вызвать один раз, когда очередь распознавания опустела (окно чата: команда «разложить») */
static void (*g_drSortHook)(void);

typedef struct {
  int id;
  BOOL busy;  /* в очереди или читается */
  BOOL first; /* первая страница файла: файл в счёте «осталось» был одним листом */
  DrPage pg;
} DrItem;

typedef struct DrJob {
  struct DrJob *next;
  int id;   /* 0 — файл целиком (страницы ещё не известны) */
  LONG gen; /* «Стоп» меняет g_drGen — задания прежнего поколения не делаются */
  UINT frame;
  BOOL text; /* и весь лист — для маршрута */
  wchar_t path[MAX_PATH];
} DrJob;

enum { DR_TAB_PAGES, DR_TAB_SPEC, DR_NTAB };
/* «Маршрут обработки» ждёт, пока эта строка дочитается целиком (0 — не ждёт) */
static int g_drRouteId;                  /* строка, для которой дочитывается лист */
static wchar_t g_drRouteF[DF_N][DR_CELL]; /* её поля штампа — с правками пользователя */
#define DR_PCOLS 12
static const wchar_t *const kDrPageHead[DR_PCOLS] = {L"Файл",  L"Стр.",    L"Тип",  L"Обозначение",
                                                     L"Наименование", L"Материал", L"Масса", L"Масштаб",
                                                     L"Лист",  L"Листов",  L"Формат", L"Заметки"};
static const int kDrPageW[DR_PCOLS] = {150, 40, 95, 190, 170, 220, 60, 65, 45, 55, 60, 260};
#define DR_SCOLS (DS_N + 1)
static const int kDrSpecW[DR_SCOLS] = {180, 140, 60, 45, 45, 200, 240, 50, 120};

static HWND g_drWnd, g_drList, g_drEdit;
static WNDPROC g_drListOld;
static float g_drS = 1.0f;
static DrItem *g_dri;
static int g_drn, g_drcap;
static int g_drTab, g_drShownTab = -1;
static int *g_drMap; /* вкладка «Спецификация»: строка → (лист << 16) | строка листа */
static int g_drMapN;
static wchar_t g_drStatus[300];
static CRITICAL_SECTION g_drLock;
static BOOL g_drLockInit, g_drRunning;
static DrJob *g_drQ, *g_drQTail;
static volatile LONG g_drGen, g_drNextId;
static int g_drTodo; /* страниц ждёт или читается — для строки состояния */
static int g_drAr = -1, g_drAc, g_drCr = -1, g_drCc; /* выбор ячеек: якорь и текущая */
static BOOL g_drDrag;
static int g_drEditRow = -1, g_drEditCol = -1;

static int DRS(int v) { return (int)(v * g_drS + 0.5f); }
static void drs_show(void);

static void dr_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_drStatus, 300, fmt, ap);
  va_end(ap);
  g_drStatus[299] = 0;
  if (g_drWnd) {
    RECT rc;
    GetClientRect(g_drWnd, &rc);
    RECT st = {0, PANEL_TITLE_H + DRS(44), rc.right, PANEL_TITLE_H + DRS(76)};
    InvalidateRect(g_drWnd, &st, FALSE);
  }
}

/* ---- фоновое чтение ------------------------------------------------------------- */

static void dr_post_item(UINT msg, DrItem *it) {
  if (!g_drWnd || !PostMessageW(g_drWnd, msg, 0, (LPARAM)it)) {
    dr_page_free(&it->pg);
    free(it);
  }
}

static void dr_work_page(IWICImagingFactory *f, int id, const wchar_t *path, UINT frame, BOOL text) {
  DrItem *it = (DrItem *)calloc(1, sizeof(DrItem));
  if (!it) return;
  it->id = id;
  lstrcpynW(it->pg.path, path, MAX_PATH);
  it->pg.frame = frame;
  it->pg.wantText = text;
  if (f) dr_recognize(f, &it->pg);
  else lstrcpynW(it->pg.note, L"не запустился WIC — картинки не открыть", 300), it->pg.done = TRUE;
  dr_post_item(WM_DR_DONE, it);
}

static BOOL dr_is_pdf(const wchar_t *p) {
  const wchar_t *d = wcsrchr(p, L'.');
  return d && !lstrcmpiW(d, L".pdf");
}

/* файл целиком: страницы — в окно, потом каждую прочитать */
static void dr_work_file(IWICImagingFactory *f, const DrJob *j) {
  DrPages l;
  memset(&l, 0, sizeof(l));
  HRESULT hr = f && !dr_is_pdf(j->path) ? tm_probe(f, j->path, FALSE, dr_pages_cb, &l) : E_FAIL;
  if (!l.n) { /* не открылся — строка с пояснением */
    DrItem *it = (DrItem *)calloc(1, sizeof(DrItem));
    if (it) {
      it->id = (int)InterlockedIncrement(&g_drNextId);
      lstrcpynW(it->pg.path, j->path, MAX_PATH);
      it->pg.done = TRUE;
      it->first = TRUE;
      if (dr_is_pdf(j->path)) {
        lstrcpynW(it->pg.note, L"PDF пока не читаю — сохраните листы в TIFF или PNG", 300);
      } else {
        wchar_t why[120];
        tm_why(hr, why, 120);
        _snwprintf(it->pg.note, 300, L"не открылся: %s", why);
      }
      dr_post_item(WM_DR_ADD, it);
    }
    return;
  }
  int *ids = (int *)malloc(sizeof(int) * (size_t)l.n);
  for (int i = 0; i < l.n; i++) {
    DrItem *it = (DrItem *)calloc(1, sizeof(DrItem));
    int id = (int)InterlockedIncrement(&g_drNextId);
    if (ids) ids[i] = id;
    if (!it) continue;
    it->id = id;
    it->busy = TRUE;
    it->first = i == 0;
    lstrcpynW(it->pg.path, l.p[i].path, MAX_PATH);
    it->pg.frame = l.p[i].frame;
    dr_post_item(WM_DR_ADD, it);
  }
  for (int i = 0; i < l.n && ids && j->gen == g_drGen; i++) dr_work_page(f, ids[i], l.p[i].path, l.p[i].frame, FALSE);
  free(ids);
  free(l.p);
}

static DWORD WINAPI dr_worker(LPVOID arg) {
  (void)arg;
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  IWICImagingFactory *f = NULL;
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  for (;;) {
    EnterCriticalSection(&g_drLock);
    DrJob *j = g_drQ;
    if (j) {
      g_drQ = j->next;
      if (!g_drQ) g_drQTail = NULL;
    } else {
      g_drRunning = FALSE;
    }
    LeaveCriticalSection(&g_drLock);
    if (!j) break;
    if (j->gen == g_drGen) {
      if (j->id) dr_work_page(f, j->id, j->path, j->frame, j->text);
      else dr_work_file(f, j);
    }
    free(j);
  }
  if (f) IWICImagingFactory_Release(f);
  CoUninitialize();
  if (g_drWnd) PostMessageW(g_drWnd, WM_DR_DONE, 0, 0); /* очередь пуста — обновить состояние */
  return 0;
}

static void dr_enqueue(int id, const wchar_t *path, UINT frame, BOOL text) {
  if (!g_drLockInit) {
    InitializeCriticalSection(&g_drLock);
    g_drLockInit = TRUE;
  }
  DrJob *j = (DrJob *)calloc(1, sizeof(DrJob));
  if (!j) return;
  j->id = id;
  j->gen = g_drGen;
  j->frame = frame;
  j->text = text;
  lstrcpynW(j->path, path, MAX_PATH);
  EnterCriticalSection(&g_drLock);
  if (g_drQTail) g_drQTail->next = j;
  else g_drQ = j;
  g_drQTail = j;
  BOOL start = !g_drRunning;
  if (start) g_drRunning = TRUE;
  LeaveCriticalSection(&g_drLock);
  if (start) {
    HANDLE t = CreateThread(NULL, 0, dr_worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else g_drRunning = FALSE;
  }
  SetTimer(g_drWnd, ID_DR_TIMER, 500, NULL);
}

/* «Стоп»: очередь — прочь, текущая страница дочитается */
static void dr_stop(void) {
  InterlockedIncrement(&g_drGen);
  g_drRouteId = 0;
  if (g_drLockInit) {
    EnterCriticalSection(&g_drLock);
    while (g_drQ) {
      DrJob *n = g_drQ->next;
      free(g_drQ);
      g_drQ = n;
    }
    g_drQTail = NULL;
    LeaveCriticalSection(&g_drLock);
  }
  int left = 0;
  for (int i = 0; i < g_drn; i++)
    if (g_dri[i].busy && !g_dri[i].pg.done) {
      g_dri[i].busy = FALSE;
      lstrcpynW(g_dri[i].pg.note, L"остановлено — «Распознать заново»", 300);
      left++;
    }
  g_drTodo = 0;
  dr_status(left ? L"Остановлено. Не прочитано листов: %d" : L"Остановлено", left);
}

/* ---- таблица ------------------------------------------------------------------- */

static int dr_find(int id) {
  for (int i = 0; i < g_drn; i++)
    if (g_dri[i].id == id) return i;
  return -1;
}

static void dr_build_map(void) {
  int n = 0;
  for (int i = 0; i < g_drn; i++) n += g_dri[i].pg.nrows;
  free(g_drMap);
  g_drMap = n ? (int *)malloc(sizeof(int) * (size_t)n) : NULL;
  g_drMapN = 0;
  for (int i = 0; i < g_drn && g_drMap; i++)
    for (int r = 0; r < g_dri[i].pg.nrows; r++) g_drMap[g_drMapN++] = (i << 16) | r;
}

static int dr_nrows(void) { return g_drTab == DR_TAB_PAGES ? g_drn : g_drMapN; }
static int dr_ncols(void) { return g_drTab == DR_TAB_PAGES ? DR_PCOLS : DR_SCOLS; }

/* текст ячейки; *doubt — подсветить */
static const wchar_t *dr_cell(int tab, int r, int c, wchar_t *buf, int cap, BOOL *doubt) {
  if (doubt) *doubt = FALSE;
  buf[0] = 0;
  if (tab == DR_TAB_PAGES) {
    if (r < 0 || r >= g_drn) return buf;
    DrItem *it = &g_dri[r];
    DrPage *pg = &it->pg;
    switch (c) {
    case 0: return tm_base(pg->path);
    case 1: _snwprintf(buf, cap, L"%u", pg->frame + 1); return buf;
    case 2: return pg->done ? kDrType[pg->type] : L"…";
    case 10: return pg->fmt;
    case 11:
      if (pg->done) return pg->note;
      return it->busy ? L"распознаю…" : pg->note;
    default:
      if (c >= 3 && c < 3 + DF_N) {
        if (doubt) *doubt = pg->doubt[c - 3];
        return pg->f[c - 3];
      }
      return buf;
    }
  }
  if (r < 0 || r >= g_drMapN) return buf;
  int i = g_drMap[r] >> 16, k = g_drMap[r] & 0xFFFF;
  if (i >= g_drn || k >= g_dri[i].pg.nrows) return buf;
  if (c == 0) return g_dri[i].pg.f[DF_DES];
  if (c < 1 || c > DS_N) return buf;
  if (doubt) *doubt = g_dri[i].pg.rows[k].doubt[c - 1];
  return g_dri[i].pg.rows[k].c[c - 1];
}

static wchar_t *dr_cell_ptr(int r, int c, BYTE **doubt) {
  if (g_drTab == DR_TAB_PAGES) {
    if (r < 0 || r >= g_drn) return NULL;
    DrPage *pg = &g_dri[r].pg;
    if (c >= 3 && c < 3 + DF_N) {
      *doubt = &pg->doubt[c - 3];
      return pg->f[c - 3];
    }
    if (c == 10) {
      *doubt = NULL;
      return pg->fmt;
    }
    return NULL;
  }
  if (r < 0 || r >= g_drMapN || c < 1 || c > DS_N) return NULL;
  int i = g_drMap[r] >> 16, k = g_drMap[r] & 0xFFFF;
  *doubt = &g_dri[i].pg.rows[k].doubt[c - 1];
  return g_dri[i].pg.rows[k].c[c - 1];
}

static void dr_tabs_text(void) {
  wchar_t s[60];
  _snwprintf(s, 60, L"%sЛисты · %d", g_drTab == DR_TAB_PAGES ? L"● " : L"", g_drn);
  SetWindowTextW(GetDlgItem(g_drWnd, ID_DR_TAB), s);
  _snwprintf(s, 60, L"%sСпецификация · %d", g_drTab == DR_TAB_SPEC ? L"● " : L"", g_drMapN);
  SetWindowTextW(GetDlgItem(g_drWnd, ID_DR_TAB + 1), s);
}

static void dr_fill(void) {
  if (!g_drList) return;
  dr_build_map();
  if (g_drShownTab != g_drTab) {
    while (SendMessageW(g_drList, LVM_DELETECOLUMN, 0, 0)) {
    }
    int n = dr_ncols();
    for (int c = 0; c < n; c++) {
      LVCOLUMNW col;
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      col.pszText = (wchar_t *)(g_drTab == DR_TAB_PAGES ? kDrPageHead[c] : c == 0 ? L"Сборка" : kDrSpecCol[c - 1]);
      col.cx = DRS(g_drTab == DR_TAB_PAGES ? kDrPageW[c] : kDrSpecW[c]);
      col.iSubItem = c;
      SendMessageW(g_drList, LVM_INSERTCOLUMNW, (WPARAM)c, (LPARAM)&col);
    }
    g_drShownTab = g_drTab;
    g_drAr = g_drCr = -1;
  }
  int nr = dr_nrows();
  if (g_drCr >= nr || g_drAr >= nr) g_drAr = g_drCr = -1;
  ListView_SetItemCountEx(g_drList, nr, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
  InvalidateRect(g_drList, NULL, FALSE);
  dr_tabs_text();
}

static void dr_run_status(void) {
  if (g_drTodo > 0) {
    dr_status(L"Распознаю… осталось листов: %d. Можно смотреть и править готовые строки", g_drTodo);
    return;
  }
  int done = 0, doubt = 0, spec = 0;
  for (int i = 0; i < g_drn; i++) {
    if (!g_dri[i].pg.done) continue;
    done++;
    for (int k = 0; k < DF_N; k++) doubt += g_dri[i].pg.doubt[k];
    for (int r = 0; r < g_dri[i].pg.nrows; r++)
      for (int c = 0; c < DS_N; c++) doubt += g_dri[i].pg.rows[r].doubt[c];
    spec += g_dri[i].pg.nrows;
  }
  if (!g_drn) dr_status(L"");
  else
    dr_status(doubt ? L"Готово: листов %d, строк спецификации %d. Жёлтых ячеек %d — проверьте их (F2 — поправить)"
                    : L"Готово: листов %d, строк спецификации %d",
              done, spec, doubt);
}

/* ---- добавить файлы --------------------------------------------------------------- */

static void dr_add_paths(TmPaths *l) {
  for (int i = 0; i < l->n; i++) {
    dr_enqueue(0, l->p[i], 0, FALSE);
    g_drTodo++; /* файл — пока как один лист; страницы уточнит WM_DR_ADD */
  }
  if (l->n) dr_run_status();
  free(l->p);
  l->p = NULL;
  l->n = l->cap = 0;
}

static void dr_pick_files(void) {
  TmPaths l = {0};
  if (tm_pick(g_drWnd, &l, L"Сканы чертежей и спецификаций")) dr_add_paths(&l);
  else free(l.p);
}

/* «С экрана»: окно прячется, выделить штамп мышью — снимок в «вставки» */
static void dr_from_screen(RECT r) {
  if (g_drWnd) ShowWindow(g_drWnd, SW_SHOWNORMAL);
  int w = r.right - r.left, h = r.bottom - r.top;
  if (!w && !h) { /* Esc */
    dr_status(L"Снимок с экрана отменён");
    return;
  }
  if (w < 40 || h < 30) {
    dr_status(L"Выделено слишком мало — обведите весь штамп");
    return;
  }
  wchar_t dir[MAX_PATH], p[MAX_PATH];
  if (g_dataDir[0]) _snwprintf(dir, MAX_PATH, L"%s\\вставки", g_dataDir);
  else GetTempPathW(MAX_PATH, dir);
  dir[MAX_PATH - 1] = 0;
  CreateDirectoryW(dir, NULL);
  SYSTEMTIME st;
  GetLocalTime(&st);
  _snwprintf(p, MAX_PATH, L"%s\\Экран %02d-%02d-%02d.bmp", dir, st.wHour, st.wMinute, st.wSecond);
  p[MAX_PATH - 1] = 0;
  if (!save_rect_bmp(r.left, r.top, w, h, p)) {
    dr_status(L"Не удалось снять экран");
    return;
  }
  TmPaths l = {0};
  tm_paths_add(&l, p);
  dr_add_paths(&l);
}

static void dr_screen(void) {
  ShowWindow(g_drWnd, SW_HIDE);
  Sleep(150); /* окно успело исчезнуть с экрана */
  g_pickDone = dr_from_screen;
  start_ocr_pick();
}

static void dr_clear(void) {
  if (g_drTodo > 0) dr_stop();
  for (int i = 0; i < g_drn; i++) dr_page_free(&g_dri[i].pg);
  g_drn = 0;
  g_drAr = g_drCr = -1;
  dr_fill();
  dr_status(L"");
}

/* выбранные строки вкладки «Листы» (или листы выбранных строк спецификации) */
static int dr_sel_items(int *out, int cap) {
  if (g_drCr < 0) return 0;
  int r0 = g_drAr < g_drCr ? g_drAr : g_drCr, r1 = g_drAr < g_drCr ? g_drCr : g_drAr, n = 0;
  for (int r = r0; r <= r1 && n < cap; r++) {
    int i = g_drTab == DR_TAB_PAGES ? r : r < g_drMapN ? g_drMap[r] >> 16 : -1;
    if (i < 0 || i >= g_drn || (n && out[n - 1] == i)) continue;
    out[n++] = i;
  }
  return n;
}

static void dr_again(void) {
  g_drRouteId = 0;
  int sel[512], n = dr_sel_items(sel, 512);
  BOOL all = n == 0;
  if (all) n = g_drn < 512 ? g_drn : 512;
  int q = 0;
  for (int k = 0; k < n; k++) {
    int i = all ? k : sel[k];
    DrItem *it = &g_dri[i];
    if (it->busy && !it->pg.done) continue;
    it->busy = TRUE;
    it->pg.done = FALSE;
    it->pg.note[0] = 0;
    dr_enqueue(it->id, it->pg.path, it->pg.frame, FALSE);
    q++;
  }
  g_drTodo += q;
  dr_fill();
  if (q) dr_run_status();
  else dr_status(L"Нечего распознавать — добавьте файлы");
}

/* «Маршрут обработки»: выбранный лист → techno.c → в чат ИИ, где маршрут можно
   обсудить с моделью. Весь лист читается только здесь: пакетное распознавание
   его не читает (это ещё 1–10 с на лист). */
static void ac_route_show(const wchar_t *text); /* ai_chat.c */

/* Вход правил маршрута: прочитанное моделью с картинками — впереди (почерк и чертёжный
   шрифт она читает точнее), Tesseract — следом: правила берут первое, что узнали. */
static void dr_tn_route(const DrPage *pg, TnRoute *r) {
  wchar_t low[16], name[DR_CELL], mat[DR_CELL * 2 + 4];
  lstrcpynW(low, pg->vlName, 16);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  BOOL vn = pg->vlName[0] && wcsncmp(low, L"наимен", 6) != 0;
  lstrcpynW(name, vn ? pg->vlName : pg->f[DF_NAME], DR_CELL);
  _snwprintf(mat, DR_CELL * 2 + 4, L"%s\n%s", pg->vlMat, pg->f[DF_MAT]);
  mat[DR_CELL * 2 + 3] = 0;
  size_t n = (pg->vlText ? wcslen(pg->vlText) : 0) + (pg->text ? wcslen(pg->text) : 0) + 4;
  wchar_t *text = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (text) _snwprintf(text, n, L"%s\n%s", pg->vlText ? pg->vlText : L"", pg->text ? pg->text : L""), text[n - 1] = 0;
  tn_route(name, mat, text ? text : L"", r);
  free(text);
}

/* чем читали лист — первой строкой маршрута */
static const wchar_t *dr_tn_how(const DrPage *pg) {
  if (pg->vlText || pg->vlName[0]) return L"Лист прочитан: Tesseract и локальная модель с картинками.\n";
  return L"Лист прочитан только Tesseract: с локальной моделью с картинками (Настройки → ИИ, «Средний») точнее.\n";
}

static void dr_route_push(int i) {
  DrPage *pg = &g_dri[i].pg;
  TnRoute *r = (TnRoute *)malloc(sizeof(TnRoute));
  wchar_t *txt = (wchar_t *)malloc(sizeof(wchar_t) * 9000);
  if (r && txt) {
    dr_tn_route(pg, r);
    int n = _snwprintf(txt, 9000, L"Лист: %s%s%s\n%s\n", tm_base(pg->path), pg->f[DF_DES][0] ? L" · " : L"",
                       pg->f[DF_DES], dr_tn_how(pg));
    if (n < 0) n = 0;
    txt[n] = 0;
    tn_text(r, txt + n, 9000 - n);
    gs_route_note(r, txt, 9000); /* заготовка и марка — по справочнику стандартов, если он есть */
    ac_route_show(txt);
    dr_status(L"Маршрут — в чате ИИ: там его можно обсудить и поправить");
  }
  free(r);
  free(txt);
}

static void dr_route(void) {
  int sel[1];
  if (!dr_sel_items(sel, 1)) {
    dr_status(L"Выберите лист чертежа в списке");
    return;
  }
  DrItem *it = &g_dri[sel[0]];
  if (it->busy && !it->pg.done) {
    dr_status(L"Лист ещё распознаётся — подождите");
    return;
  }
  if (it->pg.type == DRT_SPEC || it->pg.type == DRT_TP) {
    dr_status(L"Маршрут строится по чертежу детали, а это лист «%s»", kDrType[it->pg.type]);
    return;
  }
  if (it->pg.text) {
    dr_route_push(sel[0]);
    return;
  }
  g_drRouteId = it->id;
  memcpy(g_drRouteF, it->pg.f, sizeof(g_drRouteF));
  it->busy = TRUE;
  it->pg.done = FALSE;
  it->pg.note[0] = 0;
  dr_enqueue(it->id, it->pg.path, it->pg.frame, TRUE);
  g_drTodo++;
  dr_fill();
  dr_status(L"Читаю весь лист — ТТ, шероховатость, допуски, резьбы — для маршрута…");
}

static void dr_remove_sel(void) {
  if (g_drTab != DR_TAB_PAGES || g_drCr < 0) return;
  int r0 = g_drAr < g_drCr ? g_drAr : g_drCr, r1 = g_drAr < g_drCr ? g_drCr : g_drAr;
  if (r1 >= g_drn) r1 = g_drn - 1;
  int n = 0;
  for (int r = r0; r <= r1; r++) {
    if (g_dri[r].busy && !g_dri[r].pg.done) g_drTodo--; /* ответ придёт — строки уже нет, его выбросим */
    dr_page_free(&g_dri[r].pg);
    n++;
  }
  memmove(g_dri + r0, g_dri + r1 + 1, sizeof(DrItem) * (size_t)(g_drn - r1 - 1));
  g_drn -= n;
  if (g_drTodo < 0) g_drTodo = 0;
  g_drAr = g_drCr = -1;
  dr_fill();
  dr_status(L"Убрано строк: %d (файлы не тронуты)", n);
}

/* ---- копирование ---------------------------------------------------------------- */

static void dr_clip_put(const wchar_t *s, size_t len) {
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
  if (!mem) return;
  memcpy(GlobalLock(mem), s, (len + 1) * sizeof(wchar_t));
  GlobalUnlock(mem);
  if (OpenClipboard(g_drWnd)) {
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    CloseClipboard();
  } else {
    GlobalFree(mem);
  }
}

/* ячейки через таб, строки через перевод строки; head — строка заголовков */
static void dr_copy_range(int r0, int r1, int c0, int c1, BOOL head) {
  if (r1 < r0 || c1 < c0) return;
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  wchar_t tmp[64];
  for (int r = head ? r0 - 1 : r0; r <= r1; r++) {
    for (int c = c0; c <= c1; c++) {
      const wchar_t *v = r < r0 ? (g_drTab == DR_TAB_PAGES ? kDrPageHead[c] : c == 0 ? L"Сборка" : kDrSpecCol[c - 1])
                                : dr_cell(g_drTab, r, c, tmp, 64, NULL);
      BOOL q = wcspbrk(v, L"\t\r\n\"") != NULL;
      if (q) {
        sb_add(&b, L"\"");
        for (const wchar_t *p = v; *p; p++) sb_add(&b, *p == L'"' ? L"\"\"" : L"%c", *p);
        sb_add(&b, L"\"");
      } else {
        sb_add(&b, L"%s", v);
      }
      if (c < c1) sb_add(&b, L"\t");
    }
    if (r < r1) sb_add(&b, L"\r\n");
  }
  if (b.w) {
    dr_clip_put(b.w, wcslen(b.w));
    int cells = (r1 - r0 + 1) * (c1 - c0 + 1);
    dr_status(cells == 1 ? L"Ячейка скопирована" : L"Скопировано ячеек: %d — вставьте в Excel", cells);
  }
  free(b.w);
}

#define DR_CM_CELL 1
#define DR_CM_ROW 2
#define DR_CM_COL 3
#define DR_CM_SEL 4
#define DR_CM_ALL 5
#define DR_CM_EDIT 6
#define DR_CM_OPEN 7
#define DR_CM_AGAIN 8
#define DR_CM_DEL 9

static void dr_sel_rect(int *r0, int *r1, int *c0, int *c1) {
  *r0 = g_drAr < g_drCr ? g_drAr : g_drCr;
  *r1 = g_drAr < g_drCr ? g_drCr : g_drAr;
  *c0 = g_drAc < g_drCc ? g_drAc : g_drCc;
  *c1 = g_drAc < g_drCc ? g_drCc : g_drAc;
}

static void dr_copy_cmd(int what) {
  int nr = dr_nrows(), nc = dr_ncols();
  if (nr <= 0) return;
  int r0, r1, c0, c1;
  if (what == DR_CM_ALL) r0 = 0, r1 = nr - 1, c0 = 0, c1 = nc - 1;
  else if (g_drCr < 0) return;
  else if (what == DR_CM_SEL) dr_sel_rect(&r0, &r1, &c0, &c1);
  else if (what == DR_CM_ROW) r0 = r1 = g_drCr, c0 = 0, c1 = nc - 1;
  else if (what == DR_CM_COL) r0 = 0, r1 = nr - 1, c0 = c1 = g_drCc;
  else r0 = r1 = g_drCr, c0 = c1 = g_drCc;
  dr_copy_range(r0, r1, c0, c1, what == DR_CM_ALL);
}

/* «Копировать для Excel»: выбранные строки целиком, ничего не выбрано — всё с заголовком */
static void dr_copy_excel(void) {
  int nr = dr_nrows();
  if (nr <= 0) {
    dr_status(L"Таблица пуста — добавьте сканы");
    return;
  }
  if (g_drCr < 0) {
    dr_copy_range(0, nr - 1, 0, dr_ncols() - 1, TRUE);
    return;
  }
  int r0, r1, c0, c1;
  dr_sel_rect(&r0, &r1, &c0, &c1);
  dr_copy_range(r0, r1, 0, dr_ncols() - 1, FALSE);
}

/* ---- выбор и правка ячеек -------------------------------------------------------- */

static void dr_sel_set(int r, int c, BOOL extend) {
  int nr = dr_nrows(), nc = dr_ncols();
  if (nr <= 0) return;
  r = r < 0 ? 0 : r >= nr ? nr - 1 : r;
  c = c < 0 ? 0 : c >= nc ? nc - 1 : c;
  g_drCr = r;
  g_drCc = c;
  if (!extend || g_drAr < 0) g_drAr = r, g_drAc = c;
  ListView_EnsureVisible(g_drList, r, FALSE);
  RECT cr, lr;
  GetClientRect(g_drList, &lr);
  if (ListView_GetSubItemRect(g_drList, r, c, LVIR_BOUNDS, &cr) && c > 0) {
    if (cr.left < 0) SendMessageW(g_drList, LVM_SCROLL, cr.left, 0);
    else if (cr.right > lr.right)
      SendMessageW(g_drList, LVM_SCROLL, cr.right - lr.right < cr.left ? cr.right - lr.right : cr.left, 0);
  }
  InvalidateRect(g_drList, NULL, FALSE);
}

static BOOL dr_hit(LPARAM lp, int *r, int *c) {
  LVHITTESTINFO h;
  memset(&h, 0, sizeof(h));
  h.pt.x = (short)LOWORD(lp);
  h.pt.y = (short)HIWORD(lp);
  if (SendMessageW(g_drList, LVM_SUBITEMHITTEST, 0, (LPARAM)&h) < 0 || h.iItem < 0 || h.iSubItem < 0) return FALSE;
  *r = h.iItem;
  *c = h.iSubItem;
  return TRUE;
}

static void dr_edit_end(BOOL commit);
static void dr_edit_begin(int r, int c);

static LRESULT CALLBACK DrEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (msg == WM_KEYDOWN) {
    if (wParam == VK_ESCAPE) {
      dr_edit_end(FALSE);
      return 0;
    }
    if (wParam == VK_RETURN || wParam == VK_TAB) {
      int r = g_drEditRow, c = g_drEditCol;
      BOOL shift = GetKeyState(VK_SHIFT) < 0;
      dr_edit_end(TRUE);
      if (wParam == VK_TAB) c = shift ? c - 1 : c + 1;
      else r++;
      if (r < dr_nrows() && c >= 0 && c < dr_ncols()) {
        dr_sel_set(r, c, FALSE);
        dr_edit_begin(r, c);
      }
      return 0;
    }
  }
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\t' || wParam == 27)) return 0;
  if (msg == WM_KILLFOCUS) {
    LRESULT res = CallWindowProcW(old, hwnd, msg, wParam, lParam);
    if (g_drEditRow >= 0) dr_edit_end(TRUE);
    return res;
  }
  return CallWindowProcW(old, hwnd, msg, wParam, lParam);
}

static void dr_edit_begin(int r, int c) {
  BYTE *dz;
  wchar_t *v = dr_cell_ptr(r, c, &dz);
  if (!v) {
    dr_status(c <= 2 || (g_drTab == DR_TAB_SPEC && c == 0) ? L"Эта графа не правится: она из файла"
                                                             : L"Эта графа не правится");
    return;
  }
  RECT rc;
  ListView_EnsureVisible(g_drList, r, FALSE);
  if (c == 0) {
    ListView_GetItemRect(g_drList, r, &rc, LVIR_LABEL);
  } else {
    rc.top = c;
    rc.left = LVIR_BOUNDS;
    SendMessageW(g_drList, LVM_GETSUBITEMRECT, (WPARAM)r, (LPARAM)&rc);
  }
  if (!g_drEdit) {
    g_drEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, g_drList, NULL,
                               g_inst, NULL);
    if (g_fontBody) SendMessageW(g_drEdit, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    SetWindowLongPtrW(g_drEdit, GWLP_USERDATA,
                      (LONG_PTR)SetWindowLongPtrW(g_drEdit, GWLP_WNDPROC, (LONG_PTR)DrEditProc));
  }
  g_drEditRow = r;
  g_drEditCol = c;
  SetWindowTextW(g_drEdit, v);
  int w = rc.right - rc.left < DRS(120) ? DRS(120) : rc.right - rc.left;
  MoveWindow(g_drEdit, rc.left, rc.top, w, rc.bottom - rc.top + 2, TRUE);
  ShowWindow(g_drEdit, SW_SHOW);
  SetFocus(g_drEdit);
  SendMessageW(g_drEdit, EM_SETSEL, 0, -1);
}

static void dr_edit_end(BOOL commit) {
  int r = g_drEditRow, c = g_drEditCol;
  g_drEditRow = g_drEditCol = -1;
  if (!g_drEdit) return;
  if (commit && r >= 0) {
    BYTE *dz;
    wchar_t *v = dr_cell_ptr(r, c, &dz);
    if (v) {
      wchar_t t[DR_CELL];
      GetWindowTextW(g_drEdit, t, DR_CELL);
      dr_trim(t);
      if (wcscmp(t, v)) {
        lstrcpynW(v, t, c == 10 && g_drTab == DR_TAB_PAGES ? 16 : DR_CELL);
        if (dz) *dz = 0; /* поправлено человеком — не подсвечиваем */
        if (g_drTab == DR_TAB_PAGES && c == 3) dr_build_map(); /* обозначение — это и «Сборка» */
      }
    }
    InvalidateRect(g_drList, NULL, FALSE);
  }
  BOOL had = GetFocus() == g_drEdit;
  ShowWindow(g_drEdit, SW_HIDE);
  if (had) SetFocus(g_drList);
}

static void dr_open_sheet(void) {
  int sel[1];
  if (!dr_sel_items(sel, 1)) {
    dr_status(L"Выберите строку — откроется её файл");
    return;
  }
  ShellExecuteW(NULL, L"open", g_dri[sel[0]].pg.path, NULL, NULL, SW_SHOWNORMAL);
}

static void dr_paste(void) {
  TmPaths l = {0};
  tm_clip_paths(g_drWnd, &l);
  if (l.n) dr_add_paths(&l);
  else dr_status(L"В буфере нет файлов или картинки");
}

static LRESULT CALLBACK DrListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  int r, c;
  switch (msg) {
  case WM_GETDLGCODE:
    return DLGC_WANTARROWS | DLGC_WANTCHARS;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
    SetFocus(hwnd);
    if (dr_hit(lParam, &r, &c)) {
      dr_sel_set(r, c, (wParam & MK_SHIFT) != 0);
      if (msg == WM_LBUTTONDBLCLK) {
        dr_edit_begin(r, c);
      } else {
        g_drDrag = TRUE;
        SetCapture(hwnd);
      }
    }
    return 0;
  case WM_MOUSEMOVE:
    if (g_drDrag && (wParam & MK_LBUTTON)) {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      RECT lr;
      GetClientRect(hwnd, &lr);
      if (pt.y < lr.top + DRS(24)) SendMessageW(hwnd, WM_VSCROLL, SB_LINEUP, 0);
      else if (pt.y > lr.bottom - DRS(4)) SendMessageW(hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
      if (dr_hit(lParam, &r, &c) && (r != g_drCr || c != g_drCc)) dr_sel_set(r, c, TRUE);
      return 0;
    }
    break;
  case WM_LBUTTONUP:
    if (g_drDrag) {
      g_drDrag = FALSE;
      ReleaseCapture();
    }
    return 0;
  case WM_CAPTURECHANGED:
    g_drDrag = FALSE;
    break;
  case WM_RBUTTONDOWN:
    SetFocus(hwnd);
    if (dr_hit(lParam, &r, &c)) {
      int r0, r1, c0, c1;
      BOOL inside = FALSE;
      if (g_drCr >= 0) {
        dr_sel_rect(&r0, &r1, &c0, &c1);
        inside = r >= r0 && r <= r1 && c >= c0 && c <= c1;
      }
      if (!inside) dr_sel_set(r, c, FALSE);
    }
    return 0;
  case WM_CONTEXTMENU: {
    if (g_drCr < 0) return 0;
    POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
    if (pt.x == -1 && pt.y == -1) {
      pt.x = pt.y = 0;
      ClientToScreen(hwnd, &pt);
    }
    int r0, r1, c0, c1;
    dr_sel_rect(&r0, &r1, &c0, &c1);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, DR_CM_CELL, L"Копировать ячейку");
    AppendMenuW(m, MF_STRING, DR_CM_ROW, L"Копировать строку");
    AppendMenuW(m, MF_STRING, DR_CM_COL, L"Копировать столбец");
    if (r1 > r0 || c1 > c0) AppendMenuW(m, MF_STRING, DR_CM_SEL, L"Копировать выделенное\tCtrl+C");
    AppendMenuW(m, MF_STRING, DR_CM_ALL, L"Копировать всё\tCtrl+A, Ctrl+C");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, DR_CM_EDIT, L"Поправить\tF2");
    AppendMenuW(m, MF_STRING, DR_CM_OPEN, L"Открыть лист");
    AppendMenuW(m, MF_STRING, DR_CM_AGAIN, L"Распознать заново");
    if (g_drTab == DR_TAB_PAGES) AppendMenuW(m, MF_STRING, DR_CM_DEL, L"Убрать из списка\tDelete");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
    if (cmd >= DR_CM_CELL && cmd <= DR_CM_ALL) dr_copy_cmd(cmd);
    else if (cmd == DR_CM_EDIT) dr_edit_begin(g_drCr, g_drCc);
    else if (cmd == DR_CM_OPEN) dr_open_sheet();
    else if (cmd == DR_CM_AGAIN) dr_again();
    else if (cmd == DR_CM_DEL) dr_remove_sel();
    return 0;
  }
  case WM_KEYDOWN: {
    BOOL sh = GetKeyState(VK_SHIFT) < 0, ct = GetKeyState(VK_CONTROL) < 0;
    int nr = dr_nrows(), nc = dr_ncols();
    if (ct && (wParam == 'C' || wParam == VK_INSERT)) {
      dr_copy_cmd(DR_CM_SEL);
      return 0;
    }
    if ((ct && wParam == 'V') || (sh && wParam == VK_INSERT)) {
      dr_paste();
      return 0;
    }
    if (ct && wParam == 'A') {
      if (nr > 0) {
        g_drAr = 0, g_drAc = 0, g_drCr = nr - 1, g_drCc = nc - 1;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }
    if ((wParam == VK_F2 || wParam == VK_RETURN) && g_drCr >= 0) {
      dr_edit_begin(g_drCr, g_drCc);
      return 0;
    }
    if (wParam == VK_DELETE) {
      dr_remove_sel();
      return 0;
    }
    if (wParam == VK_ESCAPE) {
      ShowWindow(g_drWnd, SW_HIDE);
      return 0;
    }
    if (g_drCr < 0 && nr > 0 && wParam >= VK_PRIOR && wParam <= VK_DOWN) {
      dr_sel_set(0, 0, FALSE);
      return 0;
    }
    int page = ListView_GetCountPerPage(hwnd);
    switch (wParam) {
    case VK_UP: dr_sel_set(g_drCr - 1, g_drCc, sh); return 0;
    case VK_DOWN: dr_sel_set(g_drCr + 1, g_drCc, sh); return 0;
    case VK_LEFT: dr_sel_set(g_drCr, g_drCc - 1, sh); return 0;
    case VK_RIGHT: dr_sel_set(g_drCr, g_drCc + 1, sh); return 0;
    case VK_PRIOR: dr_sel_set(g_drCr - page, g_drCc, sh); return 0;
    case VK_NEXT: dr_sel_set(g_drCr + page, g_drCc, sh); return 0;
    case VK_HOME: dr_sel_set(ct ? 0 : g_drCr, 0, sh); return 0;
    case VK_END: dr_sel_set(ct ? nr - 1 : g_drCr, nc - 1, sh); return 0;
    }
    break;
  }
  case WM_CHAR:
    return 0;
  }
  return CallWindowProcW(g_drListOld, hwnd, msg, wParam, lParam);
}

/* выбор — синим, сомнительное — жёлтым, ещё не прочитанное — серым */
static LRESULT dr_list_draw(NMLVCUSTOMDRAW *cd) {
  switch (cd->nmcd.dwDrawStage) {
  case CDDS_PREPAINT:
    return CDRF_NOTIFYITEMDRAW;
  case CDDS_ITEMPREPAINT:
    return CDRF_NOTIFYSUBITEMDRAW;
  case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
    int r = (int)cd->nmcd.dwItemSpec, c = cd->iSubItem;
    BOOL doubt = FALSE;
    wchar_t tmp[64];
    dr_cell(g_drTab, r, c, tmp, 64, &doubt);
    cd->clrTextBk = doubt ? RGB(0xFF, 0xF0, 0xA8) : ListView_GetBkColor(g_drList);
    cd->clrText = GetSysColor(COLOR_WINDOWTEXT);
    if (g_drTab == DR_TAB_PAGES && r < g_drn && !g_dri[r].pg.done) cd->clrText = COL_MUTED;
    if (g_drCr >= 0) {
      int r0, r1, c0, c1;
      dr_sel_rect(&r0, &r1, &c0, &c1);
      if (r >= r0 && r <= r1 && c >= c0 && c <= c1) {
        BOOL cur = r == g_drCr && c == g_drCc;
        cd->clrTextBk = cur ? RGB(0x1F, 0x5F, 0xA8) : doubt ? RGB(0xE8, 0xD8, 0x90) : RGB(0xCF, 0xE3, 0xF8);
        cd->clrText = cur ? RGB(255, 255, 255) : RGB(0, 0, 0);
      }
    }
    return CDRF_NEWFONT;
  }
  }
  return CDRF_DODEFAULT;
}

/* ---- в пакет PLM и в Союз ---------------------------------------------------------- */

static const wchar_t *dr_kind_of(const wchar_t *sect) {
  static const struct {
    const wchar_t *sect, *kind;
  } k[] = {{L"Сборочные единицы", L"Сборочная единица"}, {L"Детали", L"Деталь"},
           {L"Стандартные изделия", L"Стандартное изделие"}, {L"Прочие изделия", L"Прочее изделие"},
           {L"Материалы", L"Материал"}, {L"Комплекты", L"Комплект"}, {L"Комплексы", L"Комплекс"}};
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
    if (!_wcsicmp(sect, k[i].sect)) return k[i].kind;
  return L"";
}

static BOOL dr_is_docs(const wchar_t *sect) { return !_wcsicmp(sect, L"Документация"); }

/* количество в CSV и пакет: «0,1» — как есть, пусто — 1 у изделий с обозначением */
static void dr_pk_item(const wchar_t *des, const wchar_t *name, const wchar_t *kind, const wchar_t *mat,
                       const wchar_t *mass) {
  if (!des[0]) return;
  for (int r = 0; r < g_pk[PK_ITEMS].n; r++)
    if (!_wcsicmp(xp_cell(&g_pk[PK_ITEMS], r, 0), des)) return; /* уже есть в пакете */
  wchar_t **w = xp_row(&g_pk[PK_ITEMS]);
  if (!w) return;
  xp_set(w, 0, des);
  xp_set(w, 1, name);
  xp_set(w, 2, kind);
  xp_set(w, 3, mat);
  xp_set(w, 4, mass);
}

static void dr_to_package(void) {
  int items = 0, links = 0, before;
  package_show(FALSE); /* окно пакета и его черновик */
  pk_init();
  before = g_pk[PK_ITEMS].n;
  for (int i = 0; i < g_drn; i++) {
    DrPage *pg = &g_dri[i].pg;
    if (!pg->done || !pg->f[DF_DES][0]) continue;
    if (pg->type == DRT_DRAW) dr_pk_item(pg->f[DF_DES], pg->f[DF_NAME], L"Деталь", pg->f[DF_MAT], pg->f[DF_MASS]);
    if (pg->type != DRT_SPEC) continue;
    if (pg->f[DF_NAME][0]) dr_pk_item(pg->f[DF_DES], pg->f[DF_NAME], L"Сборочная единица", L"", L"");
    for (int r = 0; r < pg->nrows; r++) {
      DrSpecRow *s = &pg->rows[r];
      if (dr_is_docs(s->c[DS_SECT])) continue;
      const wchar_t *child = s->c[DS_DES][0] ? s->c[DS_DES] : s->c[DS_NAME];
      if (!child[0]) continue;
      if (s->c[DS_DES][0]) dr_pk_item(s->c[DS_DES], s->c[DS_NAME], dr_kind_of(s->c[DS_SECT]), L"", L"");
      if (xp_seen2(&g_pk[PK_LINKS], pg->f[DF_DES], child)) continue;
      wchar_t **w = xp_row(&g_pk[PK_LINKS]);
      if (!w) break;
      xp_set(w, 0, pg->f[DF_DES]);
      xp_set(w, 1, child);
      xp_set(w, 2, s->c[DS_QTY]);
      xp_set(w, 3, s->c[DS_POS]);
      links++;
    }
  }
  items = g_pk[PK_ITEMS].n - before;
  pk_fill(TRUE);
  pk_changed();
  pk_status(items || links ? L"Из «Распознать чертёж»: изделий %d, строк состава %d. Проверьте и «Сохранить пакет…»"
                           : L"Из «Распознать чертёж» добавить нечего: таких строк в пакете уже нет или обозначения пусты",
            items, links);
}

static void dr_safe_name(const wchar_t *in, wchar_t *out, int cap) {
  int n = 0;
  for (; *in && n < cap - 1; in++) out[n++] = wcschr(L"\\/:*?\"<>|", *in) || *in < 32 ? L'_' : *in;
  while (n && (out[n - 1] == L' ' || out[n - 1] == L'.')) n--;
  out[n] = 0;
}

static void dr_csv_field(ShareBuf *b, const wchar_t *v) {
  if (wcspbrk(v, L";\"\r\n")) {
    sb_add(b, L"\"");
    for (; *v; v++) sb_add(b, *v == L'"' ? L"\"\"" : L"%c", *v);
    sb_add(b, L"\"");
  } else {
    sb_add(b, L"%s", v);
  }
}

static BOOL dr_write_utf8(const wchar_t *path, const wchar_t *text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  char *u = n > 0 ? (char *)malloc((size_t)n) : NULL;
  if (!u) return FALSE;
  WideCharToMultiByte(CP_UTF8, 0, text, -1, u, n, NULL, NULL);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  DWORD wr = 0;
  BOOL ok = h != INVALID_HANDLE_VALUE && WriteFile(h, u, (DWORD)(n - 1), &wr, NULL) && wr == (DWORD)(n - 1);
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  free(u);
  return ok;
}

/* по CSV на сборку в папку dir: «<обозначение>.csv», «Обозначение;Наименование;Количество»;
   существующий файл не перезаписывается — рядом «(2)». Возвращает число файлов. */
static int dr_csv_write(const wchar_t *dir, wchar_t *first, int cap) {
  int files = 0;
  if (first) first[0] = 0;
  for (int i = 0; i < g_drn; i++) {
    DrPage *pg = &g_dri[i].pg;
    if (!pg->done || pg->type != DRT_SPEC || !pg->f[DF_DES][0]) continue;
    BOOL seen = FALSE; /* продолжение той же сборки — уже записано с первым листом */
    for (int k = 0; k < i && !seen; k++)
      seen = g_dri[k].pg.type == DRT_SPEC && !_wcsicmp(g_dri[k].pg.f[DF_DES], pg->f[DF_DES]);
    if (seen) continue;
    ShareBuf b;
    memset(&b, 0, sizeof(b));
    sb_add(&b, L"Обозначение;Наименование;Количество\r\n");
    int rows = 0;
    for (int k = i; k < g_drn; k++) {
      DrPage *q = &g_dri[k].pg;
      if (q->type != DRT_SPEC || _wcsicmp(q->f[DF_DES], pg->f[DF_DES])) continue;
      for (int r = 0; r < q->nrows; r++) {
        DrSpecRow *s = &q->rows[r];
        if (dr_is_docs(s->c[DS_SECT]) || (!s->c[DS_DES][0] && !s->c[DS_NAME][0])) continue;
        dr_csv_field(&b, s->c[DS_DES]);
        sb_add(&b, L";");
        dr_csv_field(&b, s->c[DS_NAME]);
        sb_add(&b, L";");
        dr_csv_field(&b, s->c[DS_QTY][0] ? s->c[DS_QTY] : L"1");
        sb_add(&b, L"\r\n");
        rows++;
      }
    }
    if (rows && b.w) {
      wchar_t name[200], p[MAX_PATH];
      dr_safe_name(pg->f[DF_DES], name, 200);
      _snwprintf(p, MAX_PATH, L"%s\\%s.csv", dir, name);
      for (int n = 2; GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES && n < 100; n++)
        _snwprintf(p, MAX_PATH, L"%s\\%s (%d).csv", dir, name, n);
      p[MAX_PATH - 1] = 0;
      if (dr_write_utf8(p, b.w)) {
        if (first && !first[0]) lstrcpynW(first, p, cap);
        files++;
      }
    }
    free(b.w);
  }
  return files;
}

static void dr_csv(void) {
  BOOL any = FALSE;
  for (int i = 0; i < g_drn && !any; i++) any = g_dri[i].pg.type == DRT_SPEC && g_dri[i].pg.nrows > 0;
  if (!any) {
    dr_status(L"Нет распознанных спецификаций — CSV для Союза делается из них");
    return;
  }
  wchar_t dir[MAX_PATH];
  if (!pick_folder(g_drWnd, dir, MAX_PATH)) return;
  wchar_t first[MAX_PATH];
  int n = dr_csv_write(dir, first, MAX_PATH);
  if (!n) {
    dr_status(L"Не удалось записать CSV в %s", dir);
    return;
  }
  dr_status(L"CSV для автозагрузки Союза: %d — в %s. Положите их в папку автозагрузки на сервере", n, dir);
  tm_show_in_folder(first);
}

/* ---- окно ------------------------------------------------------------------------ */

static void dr_layout(void) {
  RECT rc;
  GetClientRect(g_drWnd, &rc);
  int pad = DRS(14), y = PANEL_TITLE_H + DRS(8), h = DRS(30);
  place_panel_close(g_drWnd);
  static const struct {
    int id, w;
  } top[] = {{ID_DR_ADD, 150}, {ID_DR_SCREEN, 110}, {ID_DR_AGAIN, 160}, {ID_DR_STOP, 80}, {ID_DR_CLEAR, 100}};
  int x = pad;
  for (size_t i = 0; i < sizeof(top) / sizeof(top[0]); i++) {
    MoveWindow(GetDlgItem(g_drWnd, top[i].id), x, y, DRS(top[i].w), h, TRUE);
    x += DRS(top[i].w + 8);
  }
  int ty = PANEL_TITLE_H + DRS(80);
  MoveWindow(GetDlgItem(g_drWnd, ID_DR_TAB), pad, ty, DRS(150), h, TRUE);
  MoveWindow(GetDlgItem(g_drWnd, ID_DR_TAB + 1), pad + DRS(158), ty, DRS(190), h, TRUE);
  int by = rc.bottom - pad - DRS(30), ly = ty + DRS(38);
  MoveWindow(g_drList, pad, ly, rc.right - pad * 2, by - DRS(10) - ly, TRUE);
  static const struct {
    int id, w;
  } bot[] = {{ID_DR_COPY, 190}, {ID_DR_PKG, 130}, {ID_DR_CSV, 160}, {ID_DR_SORT, 200}, {ID_DR_ROUTE, 170}, {ID_DR_OPEN, 130}};
  x = pad;
  for (size_t i = 0; i < sizeof(bot) / sizeof(bot[0]); i++) {
    MoveWindow(GetDlgItem(g_drWnd, bot[i].id), x, by, DRS(bot[i].w), DRS(30), TRUE);
    x += DRS(bot[i].w + 8);
  }
}

static void dr_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Распознать чертёж");
  SetBkMode(hdc, TRANSPARENT);
  int pad = DRS(14);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, g_drTodo > 0 ? COL_SAGE : COL_INK);
  RECT st = {pad, PANEL_TITLE_H + DRS(46), rc.right - pad, PANEL_TITLE_H + DRS(70)};
  DrawTextW(hdc,
            g_drStatus[0] ? g_drStatus
                          : L"Перетащите сюда сканы чертежей и спецификаций (TIFF, PNG, JPG, BMP), вставьте Ctrl+V "
                            L"или нажмите «Добавить файлы…». «С экрана» — обвести штамп мышью.",
            -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  int by = rc.bottom - pad - DRS(30);
  RECT hint = {pad + DRS(860), by, rc.right - pad, by + DRS(30)};
  DrawTextW(hdc, L"Жёлтым — проверьте. F2 — поправить.", -1, &hint,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK DrProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    dr_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = DRS(760);
    mm->ptMinTrackSize.y = DRS(400);
    return 0;
  }
  case WM_SIZE:
    dr_layout();
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_NOTIFY: {
    NMHDR *nh = (NMHDR *)lParam;
    if (nh->hwndFrom == g_drList && nh->code == NM_CUSTOMDRAW) return dr_list_draw((NMLVCUSTOMDRAW *)lParam);
    if (nh->hwndFrom == g_drList && nh->code == LVN_GETDISPINFOW) {
      NMLVDISPINFOW *di = (NMLVDISPINFOW *)lParam;
      if (di->item.mask & LVIF_TEXT) {
        static wchar_t buf[DR_CELL];
        const wchar_t *v = dr_cell(g_drTab, di->item.iItem, di->item.iSubItem, buf, DR_CELL, NULL);
        lstrcpynW(di->item.pszText, v, di->item.cchTextMax);
      }
      return 0;
    }
    if (nh->hwndFrom == g_drList && nh->code == LVN_BEGINSCROLL && g_drEditRow >= 0) dr_edit_end(TRUE);
    if (nh->hwndFrom == ListView_GetHeader(g_drList) && (nh->code == HDN_ITEMCLICKW || nh->code == HDN_ITEMCLICKA)) {
      int nr = dr_nrows();
      if (nr > 0) { /* клик по заголовку — весь столбец */
        g_drAr = 0;
        g_drAc = g_drCc = ((NMHEADERW *)lParam)->iItem;
        g_drCr = nr - 1;
        InvalidateRect(g_drList, NULL, FALSE);
        SetFocus(g_drList);
      }
    }
    break;
  }
  case WM_DR_ADD: {
    DrItem *it = (DrItem *)lParam;
    if (it->first && g_drTodo > 0) g_drTodo--; /* файл был «одним листом» в счёте */
    if (it->busy) g_drTodo++;
    if (g_drn >= g_drcap) {
      int c = g_drcap ? g_drcap * 2 : 32;
      DrItem *q = (DrItem *)realloc(g_dri, sizeof(DrItem) * (size_t)c);
      if (!q) {
        free(it);
        return 0;
      }
      g_dri = q;
      g_drcap = c;
    }
    g_dri[g_drn++] = *it;
    free(it);
    dr_fill();
    dr_run_status();
    return 0;
  }
  case WM_DR_DONE: {
    DrItem *it = (DrItem *)lParam;
    if (!it) { /* поток закончил очередь */
      if (!g_drRunning) {
        g_drTodo = 0;
        for (int i = 0; i < g_drn; i++)
          if (g_dri[i].busy && !g_dri[i].pg.done) g_dri[i].busy = FALSE;
        KillTimer(hwnd, ID_DR_TIMER);
        dr_fill();
        dr_run_status();
        if (g_drSortHook) {
          void (*h)(void) = g_drSortHook;
          g_drSortHook = NULL;
          h();
        }
      }
      return 0;
    }
    int i = dr_find(it->id);
    if (i >= 0 && g_dri[i].busy) {
      if (g_drEditRow >= 0) dr_edit_end(TRUE);
      dr_page_free(&g_dri[i].pg);
      g_dri[i].pg = it->pg;
      g_dri[i].busy = FALSE;
      if (g_drTodo > 0) g_drTodo--;
      free(it);
      dr_fill();
      dr_run_status();
      if (g_drRouteId && g_dri[i].id == g_drRouteId) {
        g_drRouteId = 0;
        memcpy(g_dri[i].pg.f, g_drRouteF, sizeof(g_drRouteF)); /* правки полей штампа — не терять */
        dr_route_push(i);
      }
    } else { /* строку убрали или остановили — ответ не нужен */
      dr_page_free(&it->pg);
      free(it);
    }
    return 0;
  }
  case WM_TIMER:
    if (wParam == ID_DR_TIMER) dr_run_status();
    return 0;
  case WM_DROPFILES: {
    HDROP hd = (HDROP)wParam;
    TmPaths l = {0};
    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < n; i++) {
      wchar_t p[MAX_PATH];
      if (DragQueryFileW(hd, i, p, MAX_PATH)) {
        DWORD a = GetFileAttributesW(p);
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) tm_paths_add(&l, p); /* PDF — тоже: скажем про него */
        else tm_paths_add_any(&l, p);
      }
    }
    DragFinish(hd);
    dr_add_paths(&l);
    SetForegroundWindow(hwnd);
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (id == ID_DR_ADD) dr_pick_files();
    if (id == ID_DR_SCREEN) dr_screen();
    if (id == ID_DR_AGAIN) dr_again();
    if (id == ID_DR_STOP) {
      if (g_drTodo > 0) dr_stop();
      else dr_status(L"Сейчас ничего не распознаётся");
      dr_fill();
    }
    if (id == ID_DR_CLEAR) dr_clear();
    if (id == ID_DR_TAB || id == ID_DR_TAB + 1) {
      if (g_drEditRow >= 0) dr_edit_end(TRUE);
      g_drTab = id - ID_DR_TAB;
      dr_fill();
      SetFocus(g_drList);
    }
    if (id == ID_DR_COPY) dr_copy_excel();
    if (id == ID_DR_PKG) dr_to_package();
    if (id == ID_DR_CSV) dr_csv();
    if (id == ID_DR_OPEN) dr_open_sheet();
    if (id == ID_DR_SORT) drs_show();
    if (id == ID_DR_ROUTE) dr_route();
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void drawrec_show(void) {
  if (!g_drWnd) {
    HDC s = GetDC(NULL);
    g_drS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DrProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadDrawRec";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = DRS(1180), h = DRS(660);
    if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
    if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
    g_drWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_ACCEPTFILES, L"CursorPadDrawRec",
                              L"Распознать чертёж — CursorPad",
                              WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                              wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                              NULL, NULL, g_inst, NULL);
    if (!g_drWnd) return;
    round_corners(g_drWnd);
    mk_btn(g_drWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_drWnd, L"–", ID_PANEL_MIN);
    mk_btn(g_drWnd, L"Добавить файлы…", ID_DR_ADD);
    mk_btn(g_drWnd, L"С экрана", ID_DR_SCREEN);
    mk_btn(g_drWnd, L"Распознать заново", ID_DR_AGAIN);
    mk_btn(g_drWnd, L"Стоп", ID_DR_STOP);
    mk_btn(g_drWnd, L"Очистить", ID_DR_CLEAR);
    mk_btn(g_drWnd, L"● Листы · 0", ID_DR_TAB);
    mk_btn(g_drWnd, L"Спецификация · 0", ID_DR_TAB + 1);
    mk_btn(g_drWnd, L"Копировать для Excel", ID_DR_COPY);
    mk_btn(g_drWnd, L"В пакет PLM", ID_DR_PKG);
    mk_btn(g_drWnd, L"CSV для Союза…", ID_DR_CSV);
    mk_btn(g_drWnd, L"Разложить по папкам…", ID_DR_SORT);
    mk_btn(g_drWnd, L"Маршрут обработки", ID_DR_ROUTE);
    mk_btn(g_drWnd, L"Открыть лист", ID_DR_OPEN);
    g_drList = CreateWindowExW(0, WC_LISTVIEWW, L"",
                               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
                                   LVS_SHOWSELALWAYS | WS_CLIPCHILDREN,
                               0, 0, 10, 10, g_drWnd, NULL, g_inst, NULL);
    SendMessageW(g_drList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    g_drListOld = (WNDPROC)SetWindowLongPtrW(g_drList, GWLP_WNDPROC, (LONG_PTR)DrListProc);
    if (g_fontBody) SendMessageW(g_drList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    dr_fill();
    dr_layout();
  }
  ShowWindow(g_drWnd, IsIconic(g_drWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_drWnd);
  SetFocus(g_drList);
}

#ifdef SET_TEST
/* только проверочная сборка: --techno итог.txt файл… — весь лист и маршрут по каждому */
static void dr_techno_test(int argc, wchar_t **argv) {
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  IWICImagingFactory *f = NULL;
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  DrPages l;
  memset(&l, 0, sizeof(l));
  for (int i = 3; i < argc && f; i++) tm_probe(f, argv[i], FALSE, dr_pages_cb, &l);
  if (getenv("CP_DRVL")) { /* окно настроек ещё не создано — настройки ИИ прочитать самим */
    ai_load();
    g_aiUiThread = 0; /* здесь и окно, и чтение — в одном потоке; в программе модель зовёт рабочий поток */
  }
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  TnRoute *r = (TnRoute *)malloc(sizeof(TnRoute));
  wchar_t *txt = (wchar_t *)malloc(sizeof(wchar_t) * 9000);
  for (int i = 0; i < l.n && r && txt; i++) {
    DrPage *pg = &l.p[i];
    pg->wantText = TRUE;
    dr_recognize(f, pg);
    dr_tn_route(pg, r);
    tn_text(r, txt, 9000);
    sb_add(&b, L"==== %s · %.1f с · штамп: %s / %s · модель: %s / %s\n%s%s\n\n", tm_base(pg->path), pg->secs,
           pg->f[DF_NAME], pg->f[DF_MAT], pg->vlName, pg->vlMat, dr_tn_how(pg), txt);
    dr_page_free(pg);
  }
  if (b.w) share_write_ex(argv[2], b.w, TRUE);
  free(b.w);
  free(r);
  free(txt);
  free(l.p);
  if (f) IWICImagingFactory_Release(f);
  ExitProcess(0);
}
#endif

#ifdef SET_TEST
/* только проверочная сборка: --drawui снимок файл… — окно, распознать, снять
   «Листы» и «Спецификацию» (снимок.pages.bmp, снимок.spec.bmp), буфер «для
   Excel» — снимок.clip.txt, CSV для Союза — в папку снимка */
static void dr_ui_pump(DWORD ms) {
  ULONGLONG end = GetTickCount64() + ms;
  while (GetTickCount64() < end) {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
    Sleep(20);
  }
}

static void dr_ui_snap_wnd(HWND w, const wchar_t *out, const wchar_t *suffix) {
  RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  dr_ui_pump(400);
  RECT r;
  GetWindowRect(w, &r);
  int cw = r.right - r.left, ch = r.bottom - r.top, stride = (cw * 3 + 3) & ~3;
  HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = cw;
  bi.bmiHeader.biHeight = ch;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 24;
  void *bits = NULL;
  HBITMAP hb = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HGDIOBJ o = SelectObject(mdc, hb);
  BitBlt(mdc, 0, 0, cw, ch, sdc, r.left, r.top, SRCCOPY);
  GdiFlush();
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s.%s.bmp", out, suffix);
  FILE *f = _wfopen(p, L"wb");
  if (f && bits) {
    BITMAPFILEHEADER fh;
    memset(&fh, 0, sizeof(fh));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
    fh.bfSize = fh.bfOffBits + (DWORD)stride * ch;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
    fwrite(bits, (size_t)stride * ch, 1, f);
  }
  if (f) fclose(f);
  SelectObject(mdc, o);
  DeleteObject(hb);
  DeleteDC(mdc);
  ReleaseDC(NULL, sdc);
}

static void dr_ui_snap(const wchar_t *out, const wchar_t *suffix) { dr_ui_snap_wnd(g_drWnd, out, suffix); }

static void dr_ui_test(int argc, wchar_t **argv) {
  if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE); /* главное окно не загораживает снимок */
  drawrec_show();
  TmPaths l = {0};
  for (int i = 3; i < argc; i++) tm_paths_add(&l, argv[i]);
  dr_add_paths(&l);
  ULONGLONG end = GetTickCount64() + 600000;
  dr_ui_pump(500);
  while ((g_drTodo > 0 || g_drRunning) && GetTickCount64() < end) dr_ui_pump(200);
  dr_ui_pump(300);
  const wchar_t *out = argv[2];
  dr_sel_set(0, 3, FALSE);
  dr_sel_set(1, 5, TRUE);
  dr_ui_snap(out, L"pages");
  g_drTab = DR_TAB_SPEC;
  dr_fill();
  dr_sel_set(2, 5, FALSE);
  dr_ui_snap(out, L"spec");
  g_drAr = g_drCr = -1;
  dr_copy_excel();
  wchar_t p[MAX_PATH];
  if (OpenClipboard(NULL)) {
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    _snwprintf(p, MAX_PATH, L"%s.clip.txt", out);
    FILE *f = _wfopen(p, L"wb");
    if (h && f) fwrite(GlobalLock(h), 2, wcslen((wchar_t *)GlobalLock(h)), f);
    if (f) fclose(f);
    CloseClipboard();
  }
  lstrcpynW(p, out, MAX_PATH);
  for (wchar_t *q = p; *q; q++)
    if (*q == L'/') *q = L'\\';
  wchar_t *sl = wcsrchr(p, L'\\');
  if (sl) *sl = 0;
  dr_csv_write(p, NULL, 0);
  dr_to_package();
  dr_ui_pump(300);
  if (g_pkWnd) dr_ui_snap_wnd(g_pkWnd, out, L"pkg");
  ExitProcess(0);
}
#endif
