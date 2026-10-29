/* ---- Приоритет «операция → участок, станок» — таблица технолога (с 2026.10.29) -------------

   Файл soyuz-prio.txt в папке данных (UTF-8, поля через Tab), правится в окне «ЭСИ и ТП»
   («Ещё ▾» → «Приоритеты операций…») или любым редактором:
     операция<Tab>участок<Tab>станок[<Tab>участок 2<Tab>станок 2 …]
   Операция — код справочника («4110») или название («Токарная»; название сводится к коду
   по справочнику 1С). Участок — код («31») или как в «Союзе» («31 Механический участок 31»),
   станок — как в справочнике «Союза» («16К20, Токарно-винторезный станок»). Пары идут по
   убыванию предпочтения; если участок у операции уже задан, берётся пара с этим участком.
   Эта таблица важнее подбора по частоте в базе: её смотрят первой и окно «ЭСИ и ТП»
   (участок и станок пустых операций), и «Создать ТП» по маршруту (soyuz_make.c), где иначе
   берутся участок и станок аналога из ТП завода. */

#define SZP_MAX 300
#define SZP_PAIRS 4

typedef struct {
  long code;                  /* код операции; -1 — только название */
  wchar_t name[80];           /* название строчными, без уточнения в скобках */
  int n;                      /* пар участок/станок */
  wchar_t area[SZP_PAIRS][80];
  wchar_t equip[SZP_PAIRS][200];
} SzPrio;

static SzPrio *g_szp;
static int g_szpN;
static wchar_t g_szpMech[80]; /* «МЕХОБРАБОТКА<Tab>12»: участок всех операций резанием без своей строки */
static SzPrio g_szpMechRow;   /* та же настройка в виде строки таблицы — что отдаёт szp_find */
static FILETIME g_szpStamp; /* файл перечитывается, когда менялся */
static BOOL g_szpLoaded;
static HWND g_szpWnd, g_szpEdit;
static void pk_prio_changed(void); /* package_ui.c: таблицу сохранили — пересмотреть пустые участки и станки */

/* масштаб DPI — окно правки (SZS/PKS объявлены позже) */
static int szp_s(int v) {
  static float k;
  if (k == 0) {
    HDC d = GetDC(NULL);
    k = d ? GetDeviceCaps(d, LOGPIXELSX) / 96.0f : 1.0f;
    if (d) ReleaseDC(NULL, d);
  }
  return (int)(v * k + 0.5f);
}

static void szp_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\soyuz-prio.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static const wchar_t kSzpTemplate[] =
    L"# Приоритет «операция → участок, станок». Правит технолог; эта таблица важнее частоты в базе «Союза».\r\n"
    L"# Строка: операция (код 4110 или название)\tучасток\tстанок\t[участок 2\tстанок 2 …] — поля через Tab.\r\n"
    L"# Участок — код («31») или как в «Союзе» («31 Механический участок 31»); станок — как в справочнике.\r\n"
    L"# Пары — по убыванию предпочтения: если участок операции уже задан, берётся пара с этим участком.\r\n"
    L"# МЕХОБРАБОТКА\t12 — все операции резанием (токарная, фрезерная, сверлильная, шлифовальная…)\r\n"
    L"#   на участок 12, если у операции нет своей строки; уберите «#», чтобы включить, и смените номер.\r\n"
    L"#   Заготовка (отрезная 4280, термическая резка 9171/9176/9172) сюда не идёт — ей нужна своя строка.\r\n"
    L"# Пример (уберите «#» в начале, чтобы строка заработала):\r\n"
    L"# 4110\t31\t16К20, Токарно-винторезный станок\t32\t1К62\r\n"
    L"# Сверлильная\t31\t2Н135\r\n";

/* название операции → ключ сравнения: строчные, без кода впереди и уточнения в скобках */
static void szp_key(const wchar_t *s, wchar_t *out, int cap) {
  while (*s == L' ') s++;
  s += op_num_prefix(s);
  int k = 0;
  for (; *s && *s != L'(' && k < cap - 1; s++) out[k++] = *s;
  while (k && out[k - 1] == L' ') k--;
  out[k] = 0;
  CharLowerBuffW(out, (DWORD)k);
}

/* код операции по строке «4110 Токарная», «4110» или «Токарная» (справочник 1С); -1 — нет */
static long szp_code_of(const wchar_t *s) {
  while (*s == L' ') s++;
  if (*s >= L'0' && *s <= L'9') return wcstol(s, NULL, 10);
  wchar_t key[80];
  szp_key(s, key, 80);
  for (int i = 0; i < OPS_1C_N; i++)
    if (!wcscmp(kOps1c[i].key, key)) return wcstol(kOps1c[i].num, NULL, 10);
  return -1;
}

static void szp_load(void) {
  wchar_t p[MAX_PATH];
  szp_path(p);
  WIN32_FILE_ATTRIBUTE_DATA fa;
  BOOL have = g_dataDir[0] && GetFileAttributesExW(p, GetFileExInfoStandard, &fa);
  if (!have) {
    g_szpN = 0;
    g_szpLoaded = TRUE;
    memset(&g_szpStamp, 0, sizeof(g_szpStamp));
    return;
  }
  if (g_szpLoaded && !memcmp(&fa.ftLastWriteTime, &g_szpStamp, sizeof(FILETIME))) return;
  g_szpStamp = fa.ftLastWriteTime;
  g_szpLoaded = TRUE;
  g_szpN = 0;
  g_szpMech[0] = 0;
  if (!g_szp) g_szp = (SzPrio *)calloc(SZP_MAX, sizeof(SzPrio));
  wchar_t *t = g_szp ? share_read(p) : NULL;
  if (!t) return;
  wchar_t *pp = t, *line;
  while ((line = share_next_line(&pp)) != NULL && g_szpN < SZP_MAX) {
    while (*line == L' ') line++;
    if (!*line || *line == L'#') continue;
    wchar_t *f[2 + SZP_PAIRS * 2];
    int n = share_split(line, f, 2 + SZP_PAIRS * 2);
    if (n >= 2 && !_wcsicmp(f[0], L"МЕХОБРАБОТКА")) { /* настройка, не операция */
      const wchar_t *a = f[1];
      while (*a == L' ') a++;
      lstrcpynW(g_szpMech, a, 80);
      continue;
    }
    if (n < 3) continue;
    SzPrio *r = &g_szp[g_szpN];
    memset(r, 0, sizeof(*r));
    r->code = szp_code_of(f[0]);
    szp_key(f[0], r->name, 80);
    for (int k = 1; k + 1 < n && r->n < SZP_PAIRS; k += 2) {
      const wchar_t *a = f[k], *e = f[k + 1];
      while (*a == L' ') a++;
      while (*e == L' ') e++;
      if (!a[0] && !e[0]) continue;
      lstrcpynW(r->area[r->n], a, 80);
      lstrcpynW(r->equip[r->n], e, 200);
      r->n++;
    }
    if (r->n && (r->code >= 0 || r->name[0])) g_szpN++;
  }
  free(t);
}

/* операция резанием: коды 4100…4279 справочника (обработка резанием) или название. Заготовка —
   отрезные 4280…4287 и термическая резка 9170…9177 — не мехобработка (с 2026.10.29): ей участок
   из своей строки таблицы, из справочника или аналога, иначе пусто с пометкой «проверить» */
static BOOL szp_is_cut(long code, const wchar_t *key) {
  if ((code >= 4280 && code <= 4287) || (code >= 9170 && code <= 9177)) return TRUE;
  return key && (wcsstr(key, L"отрезн") || wcsstr(key, L"резка") || wcsstr(key, L"раскро"));
}

static BOOL szp_is_mech(long code, const wchar_t *key) {
  if (szp_is_cut(code, key)) return FALSE;
  if (code >= 4100 && code <= 4299) return TRUE;
  static const wchar_t *const stems[] = {L"токарн",  L"фрезерн",  L"сверлильн", L"шлифовальн", L"расточн",
                                        L"долбежн", L"долбёжн",  L"зубо",      L"резьбо",     L"протяжн",
                                        L"строгальн", L"хонинг", L"притир",    L"доводочн"};
  for (size_t i = 0; key && i < sizeof(stems) / sizeof(stems[0]); i++)
    if (wcsstr(key, stems[i])) return TRUE;
  return FALSE;
}

/* строка таблицы для операции («4110 Токарная», «4110», «Токарная (черновая)»); нет своей, но это
   операция резанием и задана МЕХОБРАБОТКА — строка с этим участком; NULL — нет */
static const SzPrio *szp_find(const wchar_t *op) {
  if (!op || !op[0]) return NULL;
  szp_load();
  long code = szp_code_of(op);
  wchar_t key[80];
  szp_key(op, key, 80);
  for (int i = 0; i < g_szpN; i++)
    if ((code >= 0 && g_szp[i].code == code) || (key[0] && !wcscmp(g_szp[i].name, key))) return &g_szp[i];
  if (g_szpMech[0] && szp_is_mech(code, key)) {
    memset(&g_szpMechRow, 0, sizeof(g_szpMechRow));
    g_szpMechRow.code = -1;
    lstrcpynW(g_szpMechRow.name, L"мехобработка", 80);
    lstrcpynW(g_szpMechRow.area[0], g_szpMech, 80);
    g_szpMechRow.n = 1;
    return &g_szpMechRow;
  }
  return NULL;
}

/* код участка — число в начале («31 Механический участок 31» → «31»; «Цех 3 · 31 …» — после « · ») */
static void szp_area_code(const wchar_t *s, wchar_t *out, int cap) {
  const wchar_t *p = wcsstr(s, L" · ");
  p = p ? p + 3 : s;
  while (*p && !(*p >= L'0' && *p <= L'9')) p++;
  int k = 0;
  while (*p >= L'0' && *p <= L'9' && k < cap - 1) out[k++] = *p++;
  out[k] = 0;
}

/* пара для операции: curArea задан и есть пара с тем же кодом участка — она, иначе первая.
   Возвращает номер пары или -1 */
static int szp_pick(const SzPrio *p, const wchar_t *curArea) {
  if (!p || !p->n) return -1;
  wchar_t want[16] = L"";
  if (curArea && curArea[0]) szp_area_code(curArea, want, 16);
  if (want[0])
    for (int k = 0; k < p->n; k++) {
      wchar_t c[16];
      szp_area_code(p->area[k], c, 16);
      if (!wcscmp(c, want)) return k;
    }
  return want[0] ? -1 : 0; /* участок задан, а пары под него нет — таблица не вмешивается */
}

/* ---- окно правки ------------------------------------------------------------------ */

#define ID_SZP_SAVE 4880
#define ID_SZP_CLOSE 4881

static void szp_editor_load(void) {
  wchar_t p[MAX_PATH];
  szp_path(p);
  wchar_t *t = share_read(p);
  SetWindowTextW(g_szpEdit, t && t[0] ? t : kSzpTemplate);
  free(t);
}

static void szp_editor_save(void) {
  int n = GetWindowTextLengthW(g_szpEdit);
  wchar_t *t = (wchar_t *)malloc(((size_t)n + 2) * sizeof(wchar_t));
  if (!t) return;
  GetWindowTextW(g_szpEdit, t, n + 1);
  wchar_t p[MAX_PATH];
  szp_path(p);
  share_write_ex(p, t, TRUE);
  free(t);
  g_szpLoaded = FALSE; /* перечитать */
  szp_load();
  pk_prio_changed();
  wchar_t m[200];
  _snwprintf(m, 200, L"Приоритеты операций — CursorPad · сохранено, строк %d", g_szpN);
  m[199] = 0;
  SetWindowTextW(g_szpWnd, m);
}

static void szp_layout(void) {
  RECT rc;
  GetClientRect(g_szpWnd, &rc);
  int pad = szp_s(12), bh = szp_s(30), bw = szp_s(120);
  MoveWindow(g_szpEdit, pad, pad, rc.right - pad * 2, rc.bottom - pad * 3 - bh, TRUE);
  MoveWindow(GetDlgItem(g_szpWnd, ID_SZP_SAVE), rc.right - pad - bw * 2 - szp_s(8), rc.bottom - pad - bh, bw, bh, TRUE);
  MoveWindow(GetDlgItem(g_szpWnd, ID_SZP_CLOSE), rc.right - pad - bw, rc.bottom - pad - bh, bw, bh, TRUE);
}

static LRESULT CALLBACK SzpProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_SIZE:
    szp_layout();
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_SZP_SAVE) szp_editor_save();
    if (LOWORD(wParam) == ID_SZP_CLOSE) ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* «Приоритеты операций…» — таблица в окне, как есть в файле */
static void szp_edit(HWND owner) {
  if (!g_szpWnd) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SzpProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadSoyuzPrio";
    RegisterClassExW(&wc);
    int w = szp_s(820), h = szp_s(460), x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT pr;
    if (owner && GetWindowRect(owner, &pr)) x = pr.left + szp_s(60), y = pr.top + szp_s(60);
    g_szpWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadSoyuzPrio", L"Приоритеты операций — CursorPad",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, x, y, w, h, owner, NULL, g_inst, NULL);
    if (!g_szpWnd) return;
    g_szpEdit = CreateWindowExW(0, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_WANTRETURN |
                                    ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                0, 0, 10, 10, g_szpWnd, NULL, g_inst, NULL);
    if (g_fontMono) SendMessageW(g_szpEdit, WM_SETFONT, (WPARAM)g_fontMono, FALSE);
    SendMessageW(g_szpEdit, EM_SETLIMITTEXT, 200000, 0);
    mk_btn(g_szpWnd, L"Сохранить", ID_SZP_SAVE);
    mk_btn(g_szpWnd, L"Закрыть", ID_SZP_CLOSE);
    szp_layout();
  }
  szp_editor_load();
  SetWindowTextW(g_szpWnd, L"Приоритеты операций — CursorPad");
  ShowWindow(g_szpWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_szpWnd);
  SetFocus(g_szpEdit);
}
