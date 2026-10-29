/* ---- Окно «ЭСИ и ТП» (пакет для загрузки в PLM) ----------------------------------------

   Сюда вбивается всё, что нужно для ЭСИ и техпроцессов в «Союзе»: четыре листа —
   Изделия, Состав, Операции, Заготовки (столбцы — kPkSheets в export.c). Двойной щелчок
   или F2 — править ячейку (Enter — дальше вниз, Tab — вправо, Esc — отмена); «+ Строка»,
   Delete — удалить выделенные. «Вставить» (Ctrl+V) — что бы ни скопировали: таблицу Excel
   или Word с шапкой, спецификацию, маршрут из чата, операции словами, список «поз.
   обозначение наименование кол-во» — строки сами ложатся по листам (pk_paste.c); операции
   и заготовки без детали идут к выбранной строке. «Из Союза» — изделие, уже заведённое в
   «Союзе», с составом: выбираете в нём позицию и вставляете или вписываете ей ТП;
   строки, пришедшие из «Союза», серые и сами туда не уходят (они там есть), пока их не
   поменяли. «Из выгрузки» — изделия, состав и операции из окна «Выгрузка из PLM»
   (похожее изделие как образец). «Чертежи…» — файлы чертежей из архива по обозначениям:
   в «Союзе» файл ляжет подлинником к изделию. «Проверить» — что входящие есть в
   изделиях, вид изделия как в «Союзе», операции в справочнике 1С («4110 Токарная»),
   номера «005», размеры заготовки «D=40;L=265». «Что нужно?» — какие данные «Союз» берёт.
   «В Союз…» — сразу в клиент (soyuz_make.c), «Сохранить пакет…» — xlsx для команды PLM
   «Загрузить ЭСИ из CursorPad». Черновик помнится между запусками (package.txt). */

#define ID_PK_TAB 300 /* +лист: 300…303 */
#define ID_PK_ADD 304
#define ID_PK_DEL 305
#define ID_PK_PASTE 306
#define ID_PK_FROMXP 307
#define ID_PK_CHECK 308
#define ID_PK_SAVE 309
#define ID_PK_CLEAR 310
#define ID_PK_SOYUZ 311 /* «В Союз…» — сразу в клиент функцией пакета «CursorPad — Союз» (soyuz_make.c) */
#define ID_PK_HELP 312  /* «Что нужно?» — какие данные нужны «Союзу» для ЭСИ и ТП */
#define ID_PK_ESI 313   /* «Из Союза» — изделие с составом из базы «Союза» */
#define ID_PK_ESIDES 314
#define ID_PK_DRAW 315 /* «Чертежи…» — файлы из архива к изделиям */
#define ID_PK_ASM 316  /* «+ Сборка» — строка сборки: на неё бросают чертежи её состава */
/* строка быстрого ТП над листом «Операции»: деталь, операция (печатать — список сужается),
   участок, станок, «+ Операция» (Enter) — номер 005, 010… сам */
#define ID_PK_QPART 317
#define ID_PK_QOP 318
#define ID_PK_QAREA 319
#define ID_PK_QEQUIP 320
#define ID_PK_QADD 321
#define WM_PK_ESI (WM_APP + 99) /* lParam — RtJob*, освобождает окно */
/* чертежи, брошенные мышью (pk_stamp.c): штамп читает рабочий поток */
#define WM_PK_STAMP (WM_APP + 96)   /* lParam — PkStRes*, освобождает окно */
#define WM_PK_STAMPGO (WM_APP + 95) /* читается следующий файл */

/* Справочники для выпадающих списков листа «Операции» (с 2026.10.28): операции — из 1С
   (kOps1c, зашиты), участки и станки — из базы «Союза» (soyuz_make.c, szm_dicts_start:
   поток читает и присылает WM_SZM_DICTS, здесь они остаются до конца работы). */
#define WM_SZM_DICTS (WM_APP + 97) /* lParam — SzmDicts*, получатель забирает szm_dicts_take */
/* станок к операции на участке — из операций «Союза»: типовые (TypesOperation) весят 1000, операции
   ТП завода (TSOperation) — по 1 */
typedef struct {
  long area, equip; /* InfoObjectId; area 0 — операция без участка */
  long code;        /* код справочной операции: 4110 */
  long score;
} SzmFit;

typedef struct {
  int na, ne;
  wchar_t (*area)[200];  /* «31 Механический участок 31» */
  wchar_t (*equip)[200]; /* «16К20, Токарно-винторезный станок» */
  long *areaId, *equipId; /* InfoObjectId тех же строк */
  wchar_t (*areaCode)[40]; /* «31» */
  int nb;
  wchar_t (*buy)[200];   /* значения «Признака покупки» (PurchasedKind), какие есть в базе */
  int nf;
  SzmFit *fit;
  wchar_t err[300];
} SzmDicts;
static SzmDicts g_szmDicts;
static BOOL g_szmDictsAsked;
static void szm_open_text(const wchar_t *text, const wchar_t *label); /* soyuz_make.c */
static void szm_dicts_start(HWND notify);                               /* soyuz_make.c */
/* «Вставить»: разбор (pk_paste.c) и раскладка по листам — pk_paste_ui.c, после маршрута (techno.c) */
static int pk_paste_text(const wchar_t *text, int sheet, const wchar_t *assembly, const wchar_t *part, int *show,
                         wchar_t *msg, int cap);
static const wchar_t *pk_kind_name(int i); /* pk_paste_ui.c: виды изделий, как в «Союзе»; NULL — кончились */
static int pk_kind_fix(const wchar_t *in, wchar_t *out, int cap); /* 1 — поправлен под «Союз», -1 — такого нет */
static int pk_kind_noname(const wchar_t *kind);  /* стандартные и прочие — без наименования */
static int pk_sizes_fix(const wchar_t *in, wchar_t *out, int cap); /* «Ø40x265» → «D=40;L=265»; 0 — не понял */
static int pk_sizes_ok(const wchar_t *s);

static HWND g_pkWnd, g_pkList, g_pkEdit, g_pkCombo, g_pkEditCtl; /* правка ячейки: поле или список; какое сейчас */
static int g_pkComboCol = -1;                                     /* чем заполнен список */
static XpTable g_pk[PK_NT];
static int g_pkSheet;             /* какой лист в таблице */
static int g_pkEditRow = -1, g_pkEditCol = -1;
static wchar_t g_pkStatus[300];
static float g_pkS = 1.0f;
static BOOL g_pkLoaded;
static HWND g_pkEsiEdit, g_pkHelp;
static volatile LONG g_pkEsiBusy, g_pkEsiCancel;
static wchar_t g_pkCur[200]; /* выбранная позиция: к ней — вставленные операции и заготовка */
static volatile LONG g_pkStGen; /* «Очистить» — недочитанные чертежи (pk_stamp.c) мимо */
static HWND g_pkQPart, g_pkQOp, g_pkQArea, g_pkQEquip; /* строка быстрого ТП (лист «Операции») */
static BOOL g_pkQShown, g_pkQBusy;
static wchar_t g_pkQLastArea[200], g_pkQLastEquip[200]; /* участок — для следующей операции, в черновике (станок —
                                                           только из старых черновиков) */
static void pk_layout(void);
static void pk_stamp_drop(HWND hwnd, HDROP hd); /* pk_stamp.c: бросили файлы — читать штампы */
static void pk_stamp_done(void *res);           /* pk_stamp.c: WM_PK_STAMP */
static void pk_stamp_progress(void);            /* pk_stamp.c: WM_PK_STAMPGO */
static void pk_stamp_typed(int row);            /* pk_stamp.c: обозначение вписали — состав, который его ждал */
static void pk_stamp_forget(void);              /* pk_stamp.c: «Очистить» */
static void pk_stamp_pick(void);                /* pk_stamp.c: выбрать файлы чертежей — как бросить их */

/* Строки, пришедшие из «Союза» («Из Союза»): изделие и связь там уже есть. Пока строка как
   была — в «Союз» не идёт (у изделия функция выровняла бы наименование, пустое стёрла бы);
   поменяли наименование — идёт. tp — у изделия в «Союзе» уже есть ТП: новый функция не создаст. */
typedef struct {
  wchar_t des[200], name[200];
  BOOL tp;
} PkSoyItem;
typedef struct {
  wchar_t a[200], c[200];
} PkSoyLink;
static PkSoyItem *g_pkSoyIt;
static PkSoyLink *g_pkSoyLn;
static int g_pkSoyItN, g_pkSoyItCap, g_pkSoyLnN, g_pkSoyLnCap;

static PkSoyItem *pk_soy_item(const wchar_t *des) {
  for (int i = 0; des[0] && i < g_pkSoyItN; i++)
    if (!_wcsicmp(g_pkSoyIt[i].des, des)) return &g_pkSoyIt[i];
  return NULL;
}

static BOOL pk_soy_link(const wchar_t *a, const wchar_t *c) {
  for (int i = 0; i < g_pkSoyLnN; i++)
    if (!_wcsicmp(g_pkSoyLn[i].a, a) && !_wcsicmp(g_pkSoyLn[i].c, c)) return TRUE;
  return FALSE;
}

static void pk_soy_add_item(const wchar_t *des, const wchar_t *name, BOOL tp) {
  if (!des[0] || pk_soy_item(des)) return;
  if (g_pkSoyItN == g_pkSoyItCap) {
    int nc = g_pkSoyItCap ? g_pkSoyItCap * 2 : 64;
    PkSoyItem *n = (PkSoyItem *)realloc(g_pkSoyIt, sizeof(PkSoyItem) * (size_t)nc);
    if (!n) return;
    g_pkSoyIt = n, g_pkSoyItCap = nc;
  }
  PkSoyItem *it = &g_pkSoyIt[g_pkSoyItN++];
  lstrcpynW(it->des, des, 200);
  lstrcpynW(it->name, name, 200);
  it->tp = tp;
}

static void pk_soy_add_link(const wchar_t *a, const wchar_t *c) {
  if (!a[0] || !c[0] || pk_soy_link(a, c)) return;
  if (g_pkSoyLnN == g_pkSoyLnCap) {
    int nc = g_pkSoyLnCap ? g_pkSoyLnCap * 2 : 64;
    PkSoyLink *n = (PkSoyLink *)realloc(g_pkSoyLn, sizeof(PkSoyLink) * (size_t)nc);
    if (!n) return;
    g_pkSoyLn = n, g_pkSoyLnCap = nc;
  }
  lstrcpynW(g_pkSoyLn[g_pkSoyLnN].a, a, 200);
  lstrcpynW(g_pkSoyLn[g_pkSoyLnN].c, c, 200);
  g_pkSoyLnN++;
}

static void pk_soy_clear(void) { g_pkSoyItN = g_pkSoyLnN = 0; }

/* строка листа — как пришла из «Союза» (серая, в «Союз» не идёт) */
static BOOL pk_row_from_soyuz(int sheet, int r) {
  const XpTable *t = &g_pk[sheet];
  if (sheet == PK_ITEMS) {
    PkSoyItem *it = pk_soy_item(xp_cell((void *)t, r, 0));
    const wchar_t *nm = xp_cell((void *)t, r, 1);
    return it && (!nm[0] || !_wcsicmp(nm, it->name));
  }
  if (sheet == PK_LINKS) return pk_soy_link(xp_cell((void *)t, r, 0), xp_cell((void *)t, r, 1));
  return FALSE;
}

static int PKS(int v) { return (int)(v * g_pkS + 0.5f); }

static void pk_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_pkStatus, 300, fmt, ap);
  va_end(ap);
  g_pkStatus[299] = 0;
  if (g_pkWnd) {
    RECT rc;
    GetClientRect(g_pkWnd, &rc);
    RECT st = {0, PANEL_TITLE_H + PKS(56), rc.right, PANEL_TITLE_H + PKS(82)}; /* строка состояния (pk_paint) */
    InvalidateRect(g_pkWnd, &st, FALSE);
  }
}

/* ячейка: пусто — NULL (xp_set пустое не пишет, а здесь его надо и стирать) */
static void pk_put(XpTable *t, int r, int c, const wchar_t *v) {
  wchar_t **cell = &t->cell[(size_t)r * (size_t)t->ncols + (size_t)c];
  free(*cell);
  *cell = NULL;
  if (!v) return;
  while (*v == L' ') v++;
  size_t l = wcslen(v);
  while (l && (v[l - 1] == L' ' || v[l - 1] == L'\r')) l--;
  if (!l) return;
  *cell = (wchar_t *)malloc((l + 1) * sizeof(wchar_t));
  if (*cell) {
    memcpy(*cell, v, l * sizeof(wchar_t));
    (*cell)[l] = 0;
  }
}

static void pk_del_row(XpTable *t, int r) {
  if (r < 0 || r >= t->n) return;
  for (int c = 0; c < t->ncols; c++) free(t->cell[(size_t)r * (size_t)t->ncols + (size_t)c]);
  memmove(t->cell + (size_t)r * (size_t)t->ncols, t->cell + (size_t)(r + 1) * (size_t)t->ncols,
          sizeof(wchar_t *) * (size_t)(t->n - r - 1) * (size_t)t->ncols);
  t->n--;
}

static void pk_init(void) {
  for (int k = 0; k < PK_NT; k++)
    if (!g_pk[k].ncols) g_pk[k].ncols = kPkSheets[k].ncols;
}

/* ---- черновик: package.txt, строки «лист\tячейки…» ---- */
static void pk_draft_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\package.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static void pk_draft_save(void) {
  if (!g_dataDir[0]) return;
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  sb_add(&b, L"pkg\t1\n");
  for (int k = 0; k < PK_NT; k++)
    for (int r = 0; r < g_pk[k].n; r++) {
      sb_add(&b, L"%d", k);
      for (int c = 0; c < g_pk[k].ncols; c++) sb_field(&b, xp_cell(&g_pk[k], r, c));
      sb_add(&b, L"\n");
    }
  /* что пришло из «Союза»: «si» — изделие (наименование там, есть ли ТП), «sl» — связь состава */
  for (int i = 0; i < g_pkSoyItN; i++) {
    sb_add(&b, L"si");
    sb_field(&b, g_pkSoyIt[i].des);
    sb_field(&b, g_pkSoyIt[i].name);
    sb_add(&b, L"\t%d\n", g_pkSoyIt[i].tp ? 1 : 0);
  }
  for (int i = 0; i < g_pkSoyLnN; i++) {
    sb_add(&b, L"sl");
    sb_field(&b, g_pkSoyLn[i].a);
    sb_field(&b, g_pkSoyLn[i].c);
    sb_add(&b, L"\n");
  }
  if (g_pkCur[0]) {
    sb_add(&b, L"cur");
    sb_field(&b, g_pkCur);
    sb_add(&b, L"\n");
  }
  if (g_pkQLastArea[0] || g_pkQLastEquip[0]) { /* «qa» — участок и станок строки быстрого ТП */
    sb_add(&b, L"qa");
    sb_field(&b, g_pkQLastArea);
    sb_field(&b, g_pkQLastEquip);
    sb_add(&b, L"\n");
  }
  wchar_t p[MAX_PATH];
  pk_draft_path(p);
  if (b.w) share_write_ex(p, b.w, TRUE);
  free(b.w);
}

static void pk_draft_load(void) {
  pk_init();
  if (!g_dataDir[0]) return;
  wchar_t p[MAX_PATH];
  pk_draft_path(p);
  wchar_t *t = share_read(p);
  if (!t) return;
  wchar_t *pp = t, *line;
  while ((line = share_next_line(&pp)) != NULL) {
    wchar_t *f[12];
    int n = share_split(line, f, 12);
    if (n >= 3 && !wcscmp(f[0], L"si")) pk_soy_add_item(f[1], f[2], n > 3 && f[3][0] == L'1');
    if (n >= 3 && !wcscmp(f[0], L"sl")) pk_soy_add_link(f[1], f[2]);
    if (n >= 2 && !wcscmp(f[0], L"cur")) lstrcpynW(g_pkCur, f[1], 200);
    if (n >= 2 && !wcscmp(f[0], L"qa")) lstrcpynW(g_pkQLastArea, f[1], 200), lstrcpynW(g_pkQLastEquip, n > 2 ? f[2] : L"", 200);
    if (n < 2 || f[0][0] < L'0' || f[0][0] >= L'0' + PK_NT || f[0][1]) continue;
    int k = f[0][0] - L'0';
    wchar_t **w = xp_row(&g_pk[k]);
    if (!w) break;
    for (int c = 0; c < g_pk[k].ncols && c + 1 < n; c++) pk_put(&g_pk[k], g_pk[k].n - 1, c, f[c + 1]);
  }
  free(t);
}

/* ---- таблица окна ---- */
static void pk_tabs(void) {
  if (g_pkQPart && (g_pkSheet == PK_OPS) != g_pkQShown) pk_layout(); /* строка быстрого ТП — только у «Операций» */
  for (int k = 0; k < PK_NT; k++) {
    wchar_t s[60];
    _snwprintf(s, 60, L"%s%s · %d", k == g_pkSheet ? L"● " : L"", kPkSheets[k].name, g_pk[k].n);
    SetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_TAB + k), s);
  }
}

static void pk_set_item(int r) {
  XpTable *t = &g_pk[g_pkSheet];
  for (int c = 0; c < t->ncols; c++) {
    LVITEMW si;
    memset(&si, 0, sizeof(si));
    si.iSubItem = c;
    si.pszText = (wchar_t *)xp_cell(t, r, c);
    SendMessageW(g_pkList, LVM_SETITEMTEXTW, (WPARAM)r, (LPARAM)&si);
  }
}

static void pk_fill(BOOL columns) {
  XpTable *t = &g_pk[g_pkSheet];
  SendMessageW(g_pkList, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(g_pkList);
  if (columns) {
    while (SendMessageW(g_pkList, LVM_DELETECOLUMN, 0, 0)) {
    }
    for (int c = 0; c < t->ncols; c++) {
      LVCOLUMNW col;
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      col.pszText = (wchar_t *)kPkSheets[g_pkSheet].head[c];
      col.cx = PKS(kPkSheets[g_pkSheet].width[c] * 7 + 14);
      col.iSubItem = c;
      SendMessageW(g_pkList, LVM_INSERTCOLUMNW, (WPARAM)c, (LPARAM)&col);
    }
  }
  for (int r = 0; r < t->n; r++) {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = r;
    it.pszText = (wchar_t *)xp_cell(t, r, 0);
    SendMessageW(g_pkList, LVM_INSERTITEMW, 0, (LPARAM)&it);
    pk_set_item(r);
  }
  SendMessageW(g_pkList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_pkList, NULL, TRUE);
  pk_tabs();
}

/* «Признак покупки» сам — «Собственного изготовления» (как оно записано в базе, если список прочитан):
   у строк «Изделий» с обозначением или наименованием; у строк «Из Союза» — нет, их признак не трогать */
static const wchar_t kPkBuyOwn[] = L"Собственного изготовления";

static const wchar_t *pk_buy_default(void) {
  for (int i = 0; i < g_szmDicts.nb; i++)
    if (wcslen(g_szmDicts.buy[i]) >= 8 &&
        CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, g_szmDicts.buy[i], 8, L"собствен", 8) == CSTR_EQUAL)
      return g_szmDicts.buy[i];
  return kPkBuyOwn;
}

/* dicts — список из базы только что пришёл: подставленное раньше «Собственного изготовления» — его словами */
static void pk_buy_fill(BOOL dicts) {
  XpTable *t = &g_pk[PK_ITEMS];
  const wchar_t *def = pk_buy_default();
  for (int r = 0; r < t->n; r++) {
    const wchar_t *v = xp_cell(t, r, 6);
    if (pk_row_from_soyuz(PK_ITEMS, r) || (!xp_cell(t, r, 0)[0] && !xp_cell(t, r, 1)[0])) continue;
    if (v[0] && !(dicts && def != kPkBuyOwn && !wcscmp(v, kPkBuyOwn))) continue;
    pk_put(t, r, 6, def);
    if (g_pkSheet == PK_ITEMS && g_pkList) pk_set_item(r);
  }
}

/* станок к операции: какой в «Союзе» ставят на операцию с этим кодом на этом участке (типовые
   операции, потом ТП завода — SzmFit); участок пуст — самый частый у этой операции вообще.
   Участка нет в справочнике или пары нет в базе — 0 */
static int pk_equip_for(const wchar_t *op, const wchar_t *area, wchar_t *out, int cap) {
  out[0] = 0;
  if (!g_szmDicts.nf || !op[0]) return 0;
  while (*op == L' ') op++;
  long code = -1;
  if (*op >= L'0' && *op <= L'9') code = wcstol(op, NULL, 10);
  else { /* «Токарная» без номера — номер из справочника 1С */
    wchar_t low[200];
    lstrcpynW(low, op, 200);
    CharLowerBuffW(low, (DWORD)wcslen(low));
    for (int i = 0; i < OPS_1C_N && code < 0; i++)
      if (!wcscmp(kOps1c[i].key, low)) code = wcstol(kOps1c[i].num, NULL, 10);
  }
  if (code < 0) return 0;
  while (*area == L' ') area++;
  long aid = 0;
  if (area[0]) {
    wchar_t tok[40];
    int k = 0;
    while (area[k] && area[k] != L' ' && k < 39) tok[k] = area[k], k++;
    tok[k] = 0;
    for (int i = 0; i < g_szmDicts.na && !aid; i++)
      if (!wcscmp(g_szmDicts.area[i], area) || (g_szmDicts.areaCode && !_wcsicmp(g_szmDicts.areaCode[i], tok)))
        aid = g_szmDicts.areaId ? g_szmDicts.areaId[i] : 0;
    if (!aid) return 0;
  }
  const SzmFit *best = NULL;
  for (int i = 0; i < g_szmDicts.nf; i++) {
    const SzmFit *f = &g_szmDicts.fit[i];
    if (f->code != code || (aid && f->area != aid) || (best && f->score <= best->score)) continue;
    for (int e = 0; e < g_szmDicts.ne && g_szmDicts.equipId; e++) /* только станки из списка: их функция найдёт */
      if (g_szmDicts.equipId[e] == f->equip) {
        best = f;
        lstrcpynW(out, g_szmDicts.equip[e], cap);
        break;
      }
  }
  return best != NULL;
}

/* пустое оборудование операций — подобранным; сколько подставлено */
static int pk_equip_fill(void) {
  XpTable *t = &g_pk[PK_OPS];
  int n = 0;
  for (int r = 0; r < t->n && g_szmDicts.nf; r++) {
    if (xp_cell(t, r, 4)[0] || pk_row_from_soyuz(PK_OPS, r)) continue;
    wchar_t eq[200];
    if (!pk_equip_for(xp_cell(t, r, 2), xp_cell(t, r, 3), eq, 200)) continue;
    pk_put(t, r, 4, eq);
    if (g_pkSheet == PK_OPS && g_pkList) pk_set_item(r);
    n++;
  }
  return n;
}

static void pk_changed(void) {
  pk_buy_fill(FALSE);
  pk_equip_fill();
  pk_tabs();
  pk_draft_save();
}

/* ---- правка ячейки поверх таблицы ---- */
static void pk_edit_end(BOOL commit);
static void pk_edit_begin(int r, int c);

static LRESULT CALLBACK PkEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (msg == WM_KEYDOWN) {
    if (wParam == VK_ESCAPE) {
      pk_edit_end(FALSE);
      return 0;
    }
    if (wParam == VK_RETURN || wParam == VK_TAB) {
      int r = g_pkEditRow, c = g_pkEditCol;
      BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      pk_edit_end(TRUE);
      XpTable *t = &g_pk[g_pkSheet];
      if (wParam == VK_TAB) c = shift ? c - 1 : c + 1;
      else r++;
      if (c >= t->ncols) c = 0, r++;
      if (c < 0) c = t->ncols - 1, r--;
      if (r >= 0 && r < t->n) pk_edit_begin(r, c);
      return 0;
    }
  }
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\t' || wParam == 27)) return 0;
  if (msg == WM_KILLFOCUS) {
    LRESULT res = CallWindowProcW(old, hwnd, msg, wParam, lParam);
    if (g_pkEditCtl == g_pkCombo && g_pkCombo) { /* ушли в раскрытый список того же поля — правка продолжается */
      COMBOBOXINFO ci;
      memset(&ci, 0, sizeof(ci));
      ci.cbSize = sizeof(ci);
      if (GetComboBoxInfo(g_pkCombo, &ci) && ((HWND)wParam == ci.hwndList || (HWND)wParam == g_pkCombo)) return res;
    }
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    return res;
  }
  return CallWindowProcW(old, hwnd, msg, wParam, lParam);
}

/* список для столбца: 2 — операции 1С, 3 — участки, 4 — станки (лист «Операции»); 10 — обозначения
   из «Изделий» (деталь операции и заготовки, сборка и входящее: к какой позиции ЭСИ); 11 — виды
   изделий, как в «Союзе»; 12 — признак покупки (значения из базы); иначе -1 */
static int pk_combo_kind(int sheet, int c) {
  if (sheet == PK_OPS && c >= 2 && c <= 4) return c;
  if ((sheet == PK_OPS || sheet == PK_BLANKS) && c == 0) return 10;
  if (sheet == PK_LINKS && c <= 1) return 10;
  if (sheet == PK_ITEMS && c == 2) return 11;
  if (sheet == PK_ITEMS && c == 6) return 12;
  return -1;
}

static void pk_combo_fill(int kind) {
  if (g_pkComboCol == kind && kind != 10) return; /* обозначения меняются вместе с листом */
  g_pkComboCol = kind;
  SendMessageW(g_pkCombo, CB_RESETCONTENT, 0, 0);
  SendMessageW(g_pkCombo, WM_SETREDRAW, FALSE, 0);
  if (kind == 2)
    for (int i = 0; i < OPS_1C_N; i++) SendMessageW(g_pkCombo, CB_ADDSTRING, 0, (LPARAM)kOps1c[i].name);
  else if (kind == 3)
    for (int i = 0; i < g_szmDicts.na; i++) SendMessageW(g_pkCombo, CB_ADDSTRING, 0, (LPARAM)g_szmDicts.area[i]);
  else if (kind == 4)
    for (int i = 0; i < g_szmDicts.ne; i++) SendMessageW(g_pkCombo, CB_ADDSTRING, 0, (LPARAM)g_szmDicts.equip[i]);
  else if (kind == 10)
    for (int r = 0; r < g_pk[PK_ITEMS].n; r++) {
      const wchar_t *d = xp_cell(&g_pk[PK_ITEMS], r, 0);
      if (d[0] && SendMessageW(g_pkCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)d) == CB_ERR)
        SendMessageW(g_pkCombo, CB_ADDSTRING, 0, (LPARAM)d);
    }
  else if (kind == 11)
    for (int i = 0; pk_kind_name(i); i++) SendMessageW(g_pkCombo, CB_ADDSTRING, 0, (LPARAM)pk_kind_name(i));
  else if (kind == 12) { /* как в базе; базы нет — два обычных, функция «Союза» сверит по началу слова */
    for (int i = 0; i < g_szmDicts.nb; i++) SendMessageW(g_pkCombo, CB_ADDSTRING, 0, (LPARAM)g_szmDicts.buy[i]);
    if (!g_szmDicts.nb) {
      SendMessageW(g_pkCombo, CB_ADDSTRING, 0, (LPARAM)kPkBuyOwn);
      SendMessageW(g_pkCombo, CB_ADDSTRING, 0, (LPARAM)L"Покупное");
    }
  }
  SendMessageW(g_pkCombo, WM_SETREDRAW, TRUE, 0);
  SendMessageW(g_pkCombo, CB_SETDROPPEDWIDTH, (WPARAM)PKS(360), 0);
}

static void pk_edit_begin(int r, int c) {
  XpTable *t = &g_pk[g_pkSheet];
  if (r < 0 || r >= t->n || c < 0 || c >= t->ncols) return;
  RECT rc;
  ListView_EnsureVisible(g_pkList, r, FALSE);
  if (c == 0) {
    ListView_GetItemRect(g_pkList, r, &rc, LVIR_LABEL);
  } else {
    rc.top = c;
    rc.left = LVIR_BOUNDS;
    SendMessageW(g_pkList, LVM_GETSUBITEMRECT, (WPARAM)r, (LPARAM)&rc);
  }
  if (!g_pkEdit) {
    g_pkEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, g_pkList, NULL,
                               g_inst, NULL);
    if (g_fontBody) SendMessageW(g_pkEdit, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    SetWindowLongPtrW(g_pkEdit, GWLP_USERDATA,
                      (LONG_PTR)SetWindowLongPtrW(g_pkEdit, GWLP_WNDPROC, (LONG_PTR)PkEditProc));
  }
  int kind = pk_combo_kind(g_pkSheet, c);
  if (kind >= 0 && !g_pkCombo) { /* операция, участок, станок — со списком, но вписать можно и своё */
    g_pkCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, 0, 0, 10,
                                PKS(260), g_pkList, NULL, g_inst, NULL);
    if (g_fontBody) SendMessageW(g_pkCombo, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    COMBOBOXINFO ci;
    memset(&ci, 0, sizeof(ci));
    ci.cbSize = sizeof(ci);
    if (GetComboBoxInfo(g_pkCombo, &ci) && ci.hwndItem)
      SetWindowLongPtrW(ci.hwndItem, GWLP_USERDATA,
                        (LONG_PTR)SetWindowLongPtrW(ci.hwndItem, GWLP_WNDPROC, (LONG_PTR)PkEditProc));
  }
  g_pkEditCtl = kind >= 0 && g_pkCombo ? g_pkCombo : g_pkEdit;
  if (g_pkEditCtl == g_pkCombo) pk_combo_fill(kind);
  g_pkEditRow = r;
  g_pkEditCol = c;
  SetWindowTextW(g_pkEditCtl, xp_cell(t, r, c));
  int h = rc.bottom - rc.top + 2;
  MoveWindow(g_pkEditCtl, rc.left, rc.top, rc.right - rc.left, g_pkEditCtl == g_pkCombo ? PKS(260) : h, TRUE);
  ShowWindow(g_pkEditCtl, SW_SHOW);
  SetFocus(g_pkEditCtl);
  if (g_pkEditCtl == g_pkCombo) SendMessageW(g_pkCombo, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
  else SendMessageW(g_pkEdit, EM_SETSEL, 0, -1);
}

static void pk_edit_end(BOOL commit) {
  int r = g_pkEditRow, c = g_pkEditCol;
  g_pkEditRow = g_pkEditCol = -1; /* до ShowWindow: уход фокуса не должен сохранить второй раз */
  HWND ctl = g_pkEditCtl ? g_pkEditCtl : g_pkEdit;
  if (!ctl) return;
  if (commit && r >= 0 && r < g_pk[g_pkSheet].n) {
    wchar_t v[600];
    GetWindowTextW(ctl, v, 600);
    pk_put(&g_pk[g_pkSheet], r, c, v);
    pk_set_item(r);
    if (g_pkSheet == PK_ITEMS && c == 0) pk_stamp_typed(r);
    pk_changed();
  }
  HWND focus = GetFocus(); /* ушли щелчком на кнопку — фокус ей, а не таблице */
  BOOL had = focus == ctl || (focus && GetParent(focus) == ctl);
  ShowWindow(ctl, SW_HIDE);
  if (had) SetFocus(g_pkList);
}

/* ---- действия ---- */
static void pk_add_row(void) {
  XpTable *t = &g_pk[g_pkSheet];
  if (!xp_row(t)) return;
  /* в составе, операциях и заготовках — сборка/деталь как у строки выше, а в пустом листе —
     выбранная позиция: вбивать меньше */
  const wchar_t *first = g_pkSheet == PK_ITEMS ? L"" : t->n > 1 ? xp_cell(t, t->n - 2, 0) : g_pkCur;
  if (first[0]) pk_put(t, t->n - 1, 0, first);
  pk_fill(FALSE);
  pk_changed();
  pk_edit_begin(t->n - 1, first[0] ? 1 : 0);
}

/* «+ Сборка»: строка сборки в «Изделиях» — обозначение впишут или даст её чертёж СБ, а чертежи,
   брошенные на эту строку, лягут в её состав (pk_stamp.c) */
static void pk_add_asm(void) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  if (g_pkSheet != PK_ITEMS) {
    g_pkSheet = PK_ITEMS;
    pk_fill(TRUE);
  }
  XpTable *t = &g_pk[PK_ITEMS];
  if (!xp_row(t)) return;
  pk_put(t, t->n - 1, 2, L"Сборочные единицы");
  pk_fill(FALSE);
  pk_changed();
  ListView_SetItemState(g_pkList, -1, 0, LVIS_SELECTED);
  ListView_SetItemState(g_pkList, t->n - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
  ListView_EnsureVisible(g_pkList, t->n - 1, FALSE);
  pk_status(L"Сборка: впишите обозначение и наименование или бросьте на строку её СБ (спецификацию — с составом). "
            L"Чертежи деталей, брошенные на эту строку, лягут в её состав");
  pk_edit_begin(t->n - 1, 0);
}

/* ---- строка быстрого ТП (лист «Операции») ---- */

/* операция по тому, что напечатали: «Токарная», «ток», «4110» → «4110 Токарная» из справочника
   1С; одно совпадение по началу кода или слова — оно, иначе первое по вхождению; нет — как есть */
static void pk_q_resolve(const wchar_t *in, wchar_t *out, int cap) {
  wchar_t low[200];
  lstrcpynW(low, in, 200);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  int first = -1, pre = -1, npre = 0;
  for (int i = 0; i < OPS_1C_N; i++) {
    wchar_t n[200];
    lstrcpynW(n, kOps1c[i].name, 200);
    CharLowerBuffW(n, (DWORD)wcslen(n));
    if (!wcscmp(n, low)) {
      lstrcpynW(out, kOps1c[i].name, cap);
      return;
    }
    if (!wcsncmp(n, low, wcslen(low)) || !wcsncmp(n + 5, low, wcslen(low))) pre = pre < 0 ? i : pre, npre++;
    if (first < 0 && wcsstr(n, low)) first = i;
  }
  int k = npre == 1 ? pre : first;
  lstrcpynW(out, low[0] && k >= 0 ? kOps1c[k].name : in, cap);
}

/* список операций — по напечатанному (пусто — весь справочник) */
static void pk_q_filter(void) {
  if (g_pkQBusy || !g_pkQOp) return;
  g_pkQBusy = TRUE;
  wchar_t t[200], low[200];
  GetWindowTextW(g_pkQOp, t, 200);
  DWORD s0 = 0, s1 = 0;
  SendMessageW(g_pkQOp, CB_GETEDITSEL, (WPARAM)&s0, (LPARAM)&s1);
  lstrcpynW(low, t, 200);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  SendMessageW(g_pkQOp, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_pkQOp, CB_RESETCONTENT, 0, 0);
  int n = 0;
  for (int i = 0; i < OPS_1C_N && n < 300; i++) {
    wchar_t nm[200];
    lstrcpynW(nm, kOps1c[i].name, 200);
    CharLowerBuffW(nm, (DWORD)wcslen(nm));
    if (!low[0] || wcsstr(nm, low)) SendMessageW(g_pkQOp, CB_ADDSTRING, 0, (LPARAM)kOps1c[i].name), n++;
  }
  SendMessageW(g_pkQOp, WM_SETREDRAW, TRUE, 0);
  if (low[0] && n && !SendMessageW(g_pkQOp, CB_GETDROPPEDSTATE, 0, 0)) {
    SendMessageW(g_pkQOp, CB_SHOWDROPDOWN, TRUE, 0);
    SetCursor(LoadCursorW(NULL, IDC_ARROW)); /* раскрытый список прячет курсор мыши */
  } else if (!n && SendMessageW(g_pkQOp, CB_GETDROPPEDSTATE, 0, 0)) {
    SendMessageW(g_pkQOp, CB_SHOWDROPDOWN, FALSE, 0); /* нет такой — пустой список не нужен */
  }
  SetWindowTextW(g_pkQOp, t); /* раскрытие и очистка списка трогают поле — вернуть напечатанное */
  SendMessageW(g_pkQOp, CB_SETEDITSEL, 0, MAKELPARAM(s0, s1));
  InvalidateRect(g_pkQOp, NULL, TRUE);
  g_pkQBusy = FALSE;
}

/* списки детали, участка и станка — при раскрытии: изделия и справочники могли поменяться */
static void pk_q_list(HWND cb) {
  wchar_t t[200];
  GetWindowTextW(cb, t, 200);
  SendMessageW(cb, CB_RESETCONTENT, 0, 0);
  if (cb == g_pkQPart) {
    for (int r = 0; r < g_pk[PK_ITEMS].n; r++) {
      const wchar_t *d = xp_cell(&g_pk[PK_ITEMS], r, 0);
      if (d[0] && !pk_kind_noname(xp_cell(&g_pk[PK_ITEMS], r, 2)) &&
          SendMessageW(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)d) == CB_ERR)
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)d);
    }
  } else if (cb == g_pkQArea) {
    for (int i = 0; i < g_szmDicts.na; i++) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)g_szmDicts.area[i]);
  } else if (cb == g_pkQEquip) {
    for (int i = 0; i < g_szmDicts.ne; i++) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)g_szmDicts.equip[i]);
  }
  SetWindowTextW(cb, t);
}

static void pk_q_trim(wchar_t *s) {
  wchar_t *b = s;
  while (*b == L' ') b++;
  if (b != s) memmove(s, b, (wcslen(b) + 1) * sizeof(wchar_t));
  size_t n = wcslen(s);
  while (n && s[n - 1] == L' ') s[--n] = 0;
}

/* «+ Операция» (Enter): строка в «Операции» с номером на 5 больше последнего у этой детали */
static void pk_q_add(void) {
  wchar_t part[200], op[200], area[200], eq[200];
  GetWindowTextW(g_pkQPart, part, 200);
  GetWindowTextW(g_pkQOp, op, 200);
  GetWindowTextW(g_pkQArea, area, 200);
  GetWindowTextW(g_pkQEquip, eq, 200);
  pk_q_trim(part), pk_q_trim(op), pk_q_trim(area), pk_q_trim(eq);
  if (!part[0]) {
    pk_status(L"Сначала деталь — выберите в списке слева или впишите обозначение");
    SetFocus(g_pkQPart);
    return;
  }
  if (!op[0]) {
    pk_status(L"Операция — начните печатать название или номер («ток», «4110»), Enter — в ТП");
    SetFocus(g_pkQOp);
    return;
  }
  wchar_t name[200];
  pk_q_resolve(op, name, 200);
  XpTable *t = &g_pk[PK_OPS];
  int last = 0, cnt = 0;
  for (int r = 0; r < t->n; r++)
    if (!_wcsicmp(xp_cell(t, r, 0), part)) {
      int v = _wtoi(xp_cell(t, r, 1));
      if (v > last) last = v;
      cnt++;
    }
  wchar_t num[8];
  _snwprintf(num, 8, L"%03d", last + 5);
  num[7] = 0;
  wchar_t **w = xp_row(t);
  if (!w) return;
  xp_set(w, 0, part);
  xp_set(w, 1, num);
  xp_set(w, 2, name);
  xp_set(w, 3, area);
  xp_set(w, 4, eq); /* пусто — pk_changed подберёт по операции и участку */
  lstrcpynW(g_pkQLastArea, area, 200);
  g_pkQLastEquip[0] = 0; /* станок у каждой операции свой */
  lstrcpynW(g_pkCur, part, 200);
  if (g_pkSheet == PK_OPS) {
    pk_fill(FALSE);
    ListView_EnsureVisible(g_pkList, t->n - 1, FALSE);
  }
  pk_changed();
  g_pkQBusy = TRUE; /* очистка поля — не фильтр */
  SetWindowTextW(g_pkQOp, L"");
  SetWindowTextW(g_pkQEquip, L"");
  g_pkQBusy = FALSE;
  pk_q_filter();
  SetFocus(g_pkQOp);
  const wchar_t *got = xp_cell(t, t->n - 1, 4);
  pk_status(L"ТП «%s»: операций %d, последняя — %s %s%s%s%s%s. Печатайте следующую; «В Союз…» создаст ТП и привяжет к "
            L"детали",
            part, cnt + 1, num, name, wcscmp(name, op) && !wcschr(op, L' ') ? L" (из справочника 1С)" : L"",
            got[0] ? L", " : L"", got, got[0] && !eq[0] ? L" (подобран по «Союзу»)" : L"");
}

/* Enter в полях строки быстрого ТП — «+ Операция»; в раскрытом списке — сперва выбранное */
static LRESULT CALLBACK PkQProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  HWND cb = GetParent(hwnd);
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n' || wParam == L'\t')) return 0; /* без писка */
  if (msg == WM_KEYDOWN && wParam == VK_TAB) { /* Tab / Shift+Tab — по полям строки по кругу */
    HWND f[4] = {g_pkQPart, g_pkQOp, g_pkQArea, g_pkQEquip};
    int i = 0;
    while (i < 4 && f[i] != cb) i++;
    if (SendMessageW(cb, CB_GETDROPPEDSTATE, 0, 0)) SendMessageW(cb, CB_SHOWDROPDOWN, FALSE, 0);
    SetFocus(f[(i + (GetKeyState(VK_SHIFT) < 0 ? 3 : 1)) % 4]);
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    if (SendMessageW(cb, CB_GETDROPPEDSTATE, 0, 0)) {
      int sel = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
      wchar_t t[200] = L"";
      if (sel >= 0 && SendMessageW(cb, CB_GETLBTEXTLEN, (WPARAM)sel, 0) < 200)
        SendMessageW(cb, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)t);
      g_pkQBusy = TRUE;
      SendMessageW(cb, CB_SHOWDROPDOWN, FALSE, 0);
      if (t[0]) SetWindowTextW(cb, t);
      g_pkQBusy = FALSE;
      if (cb != g_pkQOp) { /* в списке детали, участка, станка Enter — только выбрать */
        SetFocus(g_pkQOp);
        return 0;
      }
    }
    pk_q_add();
    return 0;
  }
  return CallWindowProcW(old, hwnd, msg, wParam, lParam);
}

static HWND pk_q_combo(int id, const wchar_t *cue) {
  HWND cb = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, 0, 0,
                            10, 10, g_pkWnd, (HMENU)(INT_PTR)id, g_inst, NULL);
  if (!cb) return NULL;
  if (g_fontBody) SendMessageW(cb, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
  SendMessageW(cb, CB_SETCUEBANNER, 0, (LPARAM)cue);
  SendMessageW(cb, CB_SETDROPPEDWIDTH, (WPARAM)PKS(340), 0);
  COMBOBOXINFO ci;
  memset(&ci, 0, sizeof(ci));
  ci.cbSize = sizeof(ci);
  if (GetComboBoxInfo(cb, &ci) && ci.hwndItem) {
    SendMessageW(ci.hwndItem, EM_SETCUEBANNER, TRUE, (LPARAM)cue); /* и при фокусе: строка вводится подряд */
    SetWindowLongPtrW(ci.hwndItem, GWLP_USERDATA, (LONG_PTR)SetWindowLongPtrW(ci.hwndItem, GWLP_WNDPROC, (LONG_PTR)PkQProc));
  }
  return cb;
}

static void pk_delete_selected(void) {
  XpTable *t = &g_pk[g_pkSheet];
  int removed = 0;
  for (int r = t->n - 1; r >= 0; r--)
    if (ListView_GetItemState(g_pkList, r, LVIS_SELECTED) & LVIS_SELECTED) pk_del_row(t, r), removed++;
  if (!removed) {
    pk_status(L"Выделите строки, которые удалить");
    return;
  }
  pk_fill(FALSE);
  pk_changed();
  pk_status(L"Удалено строк: %d", removed);
}

/* куда вставлять то, у чего в тексте нет детали или сборки: выделенная строка открытого листа,
   иначе — выбранная позиция (g_pkCur) */
static void pk_target(wchar_t *asmb, wchar_t *part) {
  int r = ListView_GetNextItem(g_pkList, -1, LVNI_SELECTED);
  const wchar_t *sel = r >= 0 && r < g_pk[g_pkSheet].n ? xp_cell(&g_pk[g_pkSheet], r, 0) : L"";
  lstrcpynW(part, sel[0] ? sel : g_pkCur, 200);
  lstrcpynW(asmb, sel[0] ? sel : g_pkCur, 200);
}

/* «Вставить» (Ctrl+V): что бы ни скопировали — строки сами ложатся по листам (pk_paste.c) */
static void pk_paste(void) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  if (!OpenClipboard(g_pkWnd)) {
    pk_status(L"Буфер занят другой программой");
    return;
  }
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  wchar_t *text = NULL;
  if (h) {
    const wchar_t *src = (const wchar_t *)GlobalLock(h);
    if (src) {
      text = _wcsdup(src);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  if (!text) {
    pk_status(L"В буфере нет текста — скопируйте таблицу, спецификацию, маршрут или список");
    return;
  }
  wchar_t asmb[200], part[200], msg[400];
  pk_target(asmb, part);
  int show = g_pkSheet;
  int n = pk_paste_text(text, g_pkSheet, asmb, part, &show, msg, 400);
  free(text);
  if (n) {
    BOOL cols = show != g_pkSheet;
    g_pkSheet = show; /* лист, куда легло больше всего */
    pk_fill(cols);
    pk_changed();
  }
  pk_status(L"%s", msg);
}

/* двойной щелчок по «Чертёж (файл)» — выбрать файл */
static void pk_pick_drawing(int r) {
  XpTable *it = &g_pk[PK_ITEMS];
  if (r < 0 || r >= it->n) return;
  wchar_t file[MAX_PATH];
  lstrcpynW(file, xp_cell(it, r, 5), MAX_PATH);
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_pkWnd;
  of.lpstrFilter = L"Чертежи (PDF, сканы, КОМПАС, DWG)\0*.pdf;*.tif;*.tiff;*.jpg;*.jpeg;*.png;*.cdw;*.frw;*.dwg;*.dxf\0"
                   L"Все файлы\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrTitle = L"Чертёж к изделию — ляжет в «Союзе» подлинником";
  of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&of)) return;
  pk_put(it, r, 5, file);
  pk_set_item(r);
  pk_changed();
  pk_status(L"Чертёж к %s: %s", xp_cell(it, r, 0), file);
}

/* «Чертежи…»: к изделиям без файла — чертёж из индекса архива по обозначению (как в карточке
   PLM, card_find_files). PDF вперёд: подлинник в «Союзе» — PDF; нет — скан, потом что есть. */
static void pk_find_drawings(void) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  if (g_filesN == 0) files_load_idx();
  if (g_filesN == 0) {
    pk_status(L"Индекс архива пуст («Обновить JSON» в Настройках) — файл можно выбрать двойным щелчком по «Чертёж (файл)»");
    return;
  }
  XpTable *it = &g_pk[PK_ITEMS];
  int found = 0, miss = 0, had = 0;
  HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
  for (int r = 0; r < it->n; r++) {
    const wchar_t *des = xp_cell(it, r, 0);
    if (!des[0] || pk_kind_noname(xp_cell(it, r, 2))) continue; /* у стандартных чертежей нет */
    if (xp_cell(it, r, 5)[0]) {
      had++;
      continue;
    }
    CardFiles f;
    wchar_t key[PLM_COL1];
    const wchar_t *pick = NULL;
    if (card_find_files(des, &f, key, PLM_COL1) && f.nd) {
      for (int i = 0; i < f.nd && !pick; i++) {
        const wchar_t *dot = wcsrchr(f.draw[i], L'.');
        if (dot && !_wcsicmp(dot, L".pdf")) pick = f.draw[i];
      }
      if (!pick) pick = card_pack_pick(&f);
    }
    if (pick) pk_put(it, r, 5, pick), found++;
    else miss++;
  }
  SetCursor(old);
  if (found) {
    if (g_pkSheet == PK_ITEMS) pk_fill(FALSE);
    pk_changed();
  }
  pk_status(L"Чертежи из архива: нашлось %d, нет в архиве %d%s — лист «Изделия», столбец «Чертёж (файл)»; другой файл — "
            L"двойной щелчок по ячейке",
            found, miss, had ? L", у остальных уже был" : L"");
}

/* «Чертежи…»: файлы выбрать (штамп прочитает ИИ, как при перетаскивании) или найти в архиве */
static void pk_draw_menu(void) {
  HMENU m = CreatePopupMenu();
  if (!m) return;
  AppendMenuW(m, MF_STRING, 1, L"Выбрать файлы чертежей… — штамп прочитает ИИ (на выделенную строку: сборка — в состав)");
  AppendMenuW(m, MF_STRING, 2, L"Найти в архиве по обозначениям — к изделиям без файла");
  RECT r;
  GetWindowRect(GetDlgItem(g_pkWnd, ID_PK_DRAW), &r);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN, r.left, r.top, 0, g_pkWnd, NULL);
  DestroyMenu(m);
  if (cmd == 1) pk_stamp_pick();
  if (cmd == 2) pk_find_drawings();
}

static void pk_from_export(void) {
  if (!g_xpJob || !g_xpJob->t[XP_COMP].n) {
    pk_status(L"Сначала соберите похожее изделие в «Выгрузке из PLM» (лист «Состав (ЭСИ)», лучше и «Операции»)");
    return;
  }
  int was = g_pk[PK_ITEMS].n + g_pk[PK_LINKS].n + g_pk[PK_OPS].n + g_pk[PK_BLANKS].n;
  if (was) {
    int a = MessageBoxW(g_pkWnd, L"В пакете уже есть строки.\n\nДа — заменить их выгрузкой, Нет — добавить к ним.",
                        L"ЭСИ и ТП", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (a == IDCANCEL) return;
    if (a == IDYES) xp_package_free(g_pk), pk_soy_clear(), g_pkCur[0] = 0;
  }
  pk_init();
  xp_package(g_xpJob, g_pk); /* добавляет без повторов */
  pk_fill(FALSE);
  pk_changed();
  pk_status(L"Из выгрузки %s: изделий %d, состав %d, операций %d — поменяйте обозначения и названия", g_xpJob->rt->des,
            g_pk[PK_ITEMS].n, g_pk[PK_LINKS].n, g_pk[PK_OPS].n);
}

static BOOL pk_has_item(const wchar_t *des);

/* ---- «Из Союза»: изделие, уже заведённое в «Союзе», с составом -------------------------------
   Тот же обход ЭСИ, что у маршрутной ведомости и выгрузки (route.c), — своей базой или через
   раздающего коллегу. Позиции — в «Изделия», связи — в «Состав»; они помечаются как пришедшие
   из «Союза» и сами туда не уходят. Дальше выбираете позицию и даёте ей операции — «В Союз…»
   создаст ТП и внесёт его в карточку ТП этой позиции. */
static DWORD WINAPI pk_esi_thread(LPVOID param) {
  RtJob *j = (RtJob *)param;
  j->t0 = GetTickCount64();
  if (share_client_on()) share_route(j);
  else rt_build(j);
  if (!g_pkWnd || !PostMessageW(g_pkWnd, WM_PK_ESI, 0, (LPARAM)j)) rt_job_free(j);
  InterlockedExchange(&g_pkEsiBusy, 0);
  return 0;
}

static void pk_esi_start(void) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  if (InterlockedCompareExchange(&g_pkEsiBusy, 1, 0) != 0) {
    InterlockedExchange(&g_pkEsiCancel, 1); /* второй щелчок — остановить */
    card_cancel_all(&g_pkEsiCancel);
    pk_status(L"Останавливаю…");
    return;
  }
  wchar_t des[200];
  GetWindowTextW(g_pkEsiEdit, des, 200);
  wchar_t *d = des;
  while (*d == L' ') d++;
  size_t l = wcslen(d);
  while (l && d[l - 1] == L' ') d[--l] = 0;
  if (!d[0]) { /* поле пустое — выделенная строка */
    int r = ListView_GetNextItem(g_pkList, -1, LVNI_SELECTED);
    if (r >= 0 && r < g_pk[g_pkSheet].n) lstrcpynW(des, xp_cell(&g_pk[g_pkSheet], r, 0), 200), d = des;
  }
  if (!d[0]) {
    InterlockedExchange(&g_pkEsiBusy, 0);
    pk_status(L"Впишите обозначение изделия, которое уже есть в «Союзе», и нажмите «Из Союза»");
    SetFocus(g_pkEsiEdit);
    return;
  }
  if (!share_client_on() && (!g_sqlUser[0] || !g_sqlPass[0])) {
    InterlockedExchange(&g_pkEsiBusy, 0);
    pk_status(L"Нет входа в базу «Союза» (Настройки → PLM: пользователь и пароль SQL) и коллега не раздаёт PLM");
    return;
  }
  RtJob *j = rt_job_new();
  if (!j) {
    InterlockedExchange(&g_pkEsiBusy, 0);
    return;
  }
  lstrcpynW(j->des, d, 200);
  g_pkEsiCancel = 0;
  j->cancel = &g_pkEsiCancel;
  j->notify = g_pkWnd;
  j->deadline = GetTickCount64() + 10 * 60 * 1000;
  rt_log(j, L"ЭСИ и ТП — из «Союза»: %s\r\n", j->des);
  HANDLE t = CreateThread(NULL, 0, pk_esi_thread, j, 0, NULL);
  if (!t) {
    rt_job_free(j);
    InterlockedExchange(&g_pkEsiBusy, 0);
    return;
  }
  CloseHandle(t);
  SetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_ESI), L"Стоп");
  pk_status(L"Читаю из «Союза» %s с составом…", d);
}

static void pk_esi_done(RtJob *j) {
  SetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_ESI), L"Из Союза");
  int items = 0, links = 0, tps = 0, n = 0;
  pk_init();
  for (int r = 0; r < j->n; r++) {
    const RtRow *w = &j->rows[r];
    const wchar_t *des = w->f[RC_DES], *parent = w->f[RC_PARENT];
    if (!des[0] || !wcscmp(w->f[RC_KIND], L"Материалы ТП")) continue;
    BOOL tp = w->tp > 0 || w->tpName[0] || w->f[RC_ROUTE][0];
    if (!pk_soy_item(des)) n++, tps += tp;
    pk_soy_add_item(des, w->f[RC_NAME], tp);
    if (!pk_has_item(des)) {
      wchar_t **c = xp_row(&g_pk[PK_ITEMS]);
      if (!c) break;
      xp_set(c, 0, des);
      xp_set(c, 1, w->f[RC_NAME]);
      xp_set(c, 2, w->f[RC_KIND]);
      xp_set(c, 3, w->f[RC_MAT]);
      xp_set(c, 4, w->f[RC_MASS]);
      items++;
    }
    if (!parent[0] || r == 0) continue;
    pk_soy_add_link(parent, des);
    if (xp_seen2(&g_pk[PK_LINKS], parent, des)) continue;
    wchar_t **c = xp_row(&g_pk[PK_LINKS]);
    if (!c) break;
    xp_set(c, 0, parent);
    xp_set(c, 1, des);
    xp_set(c, 2, w->f[RC_QTY]);
    links++;
  }
  if (!n) {
    pk_status(L"Из «Союза»: %s", j->err[0] ? j->err : L"такого изделия в базе не нашлось — проверьте обозначение");
    rt_job_free(j);
    return;
  }
  lstrcpynW(g_pkCur, j->rows[0].f[RC_DES], 200);
  g_pkSheet = PK_ITEMS;
  pk_fill(TRUE);
  pk_changed();
  pk_status(L"Из «Союза» %s: позиций %d (новых строк %d), связей %d, уже с ТП %d. Выберите позицию и вставьте "
            L"ей маршрут (Ctrl+V) или впишите операции — «В Союз…» создаст ТП к ней",
            g_pkCur, n, items, links, tps);
  rt_job_free(j);
}

/* «Что нужно?» — какие данные «Союз» берёт для ЭСИ и ТП (функция CursorPadCreate пакета «CursorPad — Союз») */
static void pk_help(void) {
  if (!log_window(&g_pkHelp, g_pkWnd, L"Что нужно «Союзу» для ЭСИ и ТП", PKS(860), PKS(620))) return;
  SetWindowTextW(
      g_pkHelp,
      L"ЧТО НУЖНО «СОЮЗУ», ЧТОБЫ СОЗДАТЬ ЭСИ И ТП\r\n"
      L"«В Союз…» отдаёт строки функции пакета «CursorPad — Союз» в запущенном клиенте «Союза» — под тем, кто вошёл.\r\n"
      L"Справочники функция не пополняет: чего в них нет (материал, участок, станок, операция), пишет в отчёт.\r\n\r\n"
      L"ИЗДЕЛИЯ — только для новых; уже заведённые «Союз» находит по обозначению, их строки не нужны.\r\n"
      L"  Обозначение   обязательно, как в штампе: «АБВГ.123456.002».\r\n"
      L"  Наименование  обязательно. У стандартных и прочих изделий его нет: обозначение = «Болт М6х20 ГОСТ 7798-70».\r\n"
      L"                У изделия, которое уже есть, наименование выравнивается по строке; пустое стёрло бы его —\r\n"
      L"                поэтому строки без наименования в «Союз» не уходят (только в состав).\r\n"
      L"  Вид изделия   как в «Союзе»: Сборочные единицы, Детали, Детали БЧ, Стандартные изделия, Прочие изделия,\r\n"
      L"                Материалы, Комплекты, Комплексы, Комплекты разборки. Пусто — «Детали». Выбор — списком.\r\n"
      L"  Материал      наименование материала в сортаменте целиком, как в «Союзе»:\r\n"
      L"                «Круг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013». Нет такого — изделие без материала.\r\n"
      L"  Масса, кг     по желанию.\r\n"
      L"  Признак покупки  сам — «Собственного изготовления»; сменить на «Покупное»… — выбор списком (значения\r\n"
      L"                из базы «Союза», если есть вход). Изделию «Из Союза» — только если выбрать. Нужен пакет 1.0.0.4.\r\n"
      L"  Чертёж (файл) по желанию: файл ляжет к изделию подлинником, как «Загрузить подлинники с локального\r\n"
      L"                диска» (нужна новая функция пакета). «Чертежи…» — найти в архиве по обозначениям.\r\n"
      L"  Папка         куда создать новые изделия — выбирается в окне «В Союз…»; должна быть: папок CursorPad не создаёт.\r\n\r\n"
      L"СОСТАВ — сборка, входящее, кол-во (в штуках; пусто — 1), позиция.\r\n"
      L"  Сборка и входящее — из «Изделий» или уже в «Союзе». Техсостав сборки создаётся сам.\r\n"
      L"  Строка, которая в составе уже есть, не дублируется (количество в ней не меняется).\r\n\r\n"
      L"ЗАГОТОВКИ — деталь, материал заготовки, норма расхода в кг, размеры в мм.\r\n"
      L"  Материал — в сортаменте и точно (иначе заготовка не создастся).\r\n"
      L"  Размеры — «D=40;L=265» (круг), «S=10;B=280;L=440» (лист); «Ø40x265», «10х280х440» CursorPad переведёт сам.\r\n\r\n"
      L"ОПЕРАЦИИ (ТП) — деталь, № операции («005»), операция с кодом справочника («4110 Токарная»),\r\n"
      L"  участок (код в начале: «31 Механический участок 31»; цех берётся от участка),\r\n"
      L"  оборудование («16К20, Токарно-винторезный станок»), Тпз и Тшт в минутах. Оборудование пусто — CursorPad\r\n"
      L"  подставит сам, какое в «Союзе» стоит на этой операции на этом участке (сначала типовые операции,\r\n"
      L"  потом ТП завода; нужен вход в базу SQL); сменить — выбором в ячейке.\r\n"
      L"  Операция без кода в справочнике пропускается; участок и станок, которых нет, не ставятся.\r\n"
      L"  ТП создаётся с версией 1 «В разработке», вариантом «Основной», признаком «основной ТП» и вносится\r\n"
      L"  в карточку ТП детали. Если у детали ТП уже есть — новый не создаётся (такие позиции «Проверить» назовёт).\r\n"
      L"  Папка для ТП — в окне «В Союз…»; пусто — «Для основных ТП».\r\n\r\n"
      L"ЧЕРТЕЖИ МЫШЬЮ\r\n"
      L"  Перетащите в окно чертежи (PDF, TIF, PNG, JPG; папку — все картинки в ней): обозначение и наименование\r\n"
      L"  со штампа читает мини-ИИ с картинками (если включён; иначе Tesseract — сверьте), файл ложится в «Чертёж».\r\n"
      L"  Не прочиталось (КОМПАС, DWG, без штампа) — из имени файла «АБВГ.123456.002 Корпус.pdf».\r\n"
      L"  На пустое место — новые изделия; один чертёж на строку детали — в эту строку.\r\n"
      L"  Сборка: «+ Сборка» (или любая строка вида «Сборочные единицы»), и бросайте на её строку чертежи —\r\n"
      L"  лягут в её состав по 1 шт (количество — на листе «Состав»). Её СБ даёт ей обозначение и наименование,\r\n"
      L"  спецификация — ещё и весь состав. На листе «Состав» — бросок на строку кладёт в сборку этой строки.\r\n\r\n"
      L"ТП БЫСТРО — лист «Операции», строка над таблицей: деталь (выбранная позиция — сама), операция — начните\r\n"
      L"  печатать («ток», «4110»), список сузится; участок остаётся для следующей; станок пустой — подберётся сам\r\n"
      L"  (какой в «Союзе» стоит на этой операции на этом участке), можно вписать свой. Tab — к следующему полю.\r\n"
      L"  Enter или «+ Операция» — строка с номером 005, 010…; потом «В Союз…» — ТП создастся в папке ТП\r\n"
      L"  и привяжется к детали.\r\n\r\n"
      L"КАК БЫСТРО ВСТАВИТЬ\r\n"
      L"  Ctrl+V или «Вставить» понимает: таблицу Excel и Word с шапкой (столбцы в любом порядке: «Обозн.», «Кол.»,\r\n"
      L"  «№ опер.», «Станок», «Тшт, ч»…), спецификацию с разделами, маршрут из чата ИИ, операции строками\r\n"
      L"  «005 Токарная 16К20 Тпз 12 Тшт 3,5», список «1 АБВГ.123456.002 Корпус 2 шт», строки ИЗДЕЛИЕ / СОСТАВ /\r\n"
      L"  ЗАГОТОВКА / ТП / ЧЕРТЕЖ. Операции и заготовка без детали идут к выделенной (выбранной) позиции.\r\n"
      L"  «Из Союза» — изделие, уже заведённое в «Союзе», с составом (серые строки — оттуда, туда не уходят):\r\n"
      L"  выберите позицию в «Изделиях», вставьте ей маршрут — «В Союз…» создаст ТП и привяжет к ней.\r\n"
      L"  «Распознать чертёж» → «В пакет» — изделия и состав со штампа и спецификации, файл листа — в «Чертёж».\r\n");
  ShowWindow(g_pkHelp, SW_SHOWNORMAL);
}

static BOOL pk_has_item(const wchar_t *des) {
  for (int r = 0; r < g_pk[PK_ITEMS].n; r++)
    if (!_wcsicmp(xp_cell(&g_pk[PK_ITEMS], r, 0), des)) return TRUE;
  return FALSE;
}

/* сборка ли изделие: оно — сборка в листе «Состав» */
static BOOL pk_is_parent(const wchar_t *des) {
  for (int r = 0; r < g_pk[PK_LINKS].n; r++)
    if (!_wcsicmp(xp_cell(&g_pk[PK_LINKS], r, 0), des)) return TRUE;
  return FALSE;
}

/* Проверка (и что можно — правит сама): FALSE — есть ошибки */
static BOOL pk_check(BOOL quiet) {
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  int errs = 0, fixes = 0;
  XpTable *it = &g_pk[PK_ITEMS], *ln = &g_pk[PK_LINKS], *op = &g_pk[PK_OPS], *bl = &g_pk[PK_BLANKS];
  if (!it->n && !op->n && !bl->n) sb_add(&b, L"• Пакет пуст\n"), errs++;
  for (int r = 0; r < it->n; r++) {
    const wchar_t *des = xp_cell(it, r, 0);
    if (!des[0]) sb_add(&b, L"• Изделия, строка %d: нет обозначения\n", r + 1), errs++;
    for (int q = 0; q < r; q++)
      if (des[0] && !_wcsicmp(des, xp_cell(it, q, 0))) {
        sb_add(&b, L"• Изделия: «%s» дважды (строки %d и %d)\n", des, q + 1, r + 1);
        errs++;
        break;
      }
    const wchar_t *buy = xp_cell(it, r, 6); /* признак: список из базы есть — значение из него */
    if (des[0] && buy[0] && g_szmDicts.nb) {
      BOOL known = FALSE;
      for (int i = 0; i < g_szmDicts.nb && !known; i++) known = !_wcsicmp(g_szmDicts.buy[i], buy);
      if (!known)
        sb_add(&b, L"• Изделия, строка %d: признака покупки «%s» в базе нет — функция возьмёт похожий по началу или "
                   L"напишет в отчёт\n", r + 1, buy);
    }
    if (!des[0] || pk_row_from_soyuz(PK_ITEMS, r)) continue;
    /* вид — как в «Союзе» («Деталь» → «Детали»); у сборки из «Состава» пустой вид — «Сборочные единицы» */
    wchar_t kind[80];
    const wchar_t *k = xp_cell(it, r, 2);
    int kf = k[0] ? pk_kind_fix(k, kind, 80) : 0;
    if (kf > 0) pk_put(it, r, 2, kind), fixes++;
    else if (kf < 0)
      sb_add(&b, L"• Изделия, строка %d: вида «%s» в «Союзе» нет — будут «Детали» (выбор — списком)\n", r + 1, k);
    if (!xp_cell(it, r, 2)[0] && pk_is_parent(des)) pk_put(it, r, 2, pk_kind_name(0)), fixes++;
    else if (pk_is_parent(des) && !_wcsicmp(xp_cell(it, r, 2), pk_kind_name(1)))
      sb_add(&b, L"• Изделия: у «%s» есть состав, а вид — «Детали»\n", des);
    if (!xp_cell(it, r, 1)[0] && !pk_kind_noname(xp_cell(it, r, 2)) && !pk_soy_item(des))
      sb_add(&b, L"• Изделия, строка %d: у «%s» нет наименования — в «Союз» пойдёт только в состав (если уже там есть)\n",
             r + 1, des);
    const wchar_t *file = xp_cell(it, r, 5);
    if (file[0] && GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES)
      sb_add(&b, L"• Изделия, строка %d: файла чертежа нет: %s\n", r + 1, file), errs++;
  }
  for (int r = 0; r < ln->n; r++) {
    const wchar_t *a = xp_cell(ln, r, 0), *c = xp_cell(ln, r, 1);
    if (!a[0] || !c[0]) sb_add(&b, L"• Состав, строка %d: нужны и сборка, и входящее\n", r + 1), errs++;
    else {
      if (!_wcsicmp(a, c)) sb_add(&b, L"• Состав, строка %d: изделие входит само в себя\n", r + 1), errs++;
      /* нет в «Изделиях» — «Союз» поищет у себя по обозначению */
      if (!pk_has_item(a)) sb_add(&b, L"• Состав, строка %d: сборки «%s» нет в «Изделиях» — в «Союзе» она должна быть\n", r + 1, a);
      if (!pk_has_item(c)) sb_add(&b, L"• Состав, строка %d: входящего «%s» нет в «Изделиях» — в «Союзе» оно должно быть\n", r + 1, c);
    }
    double v;
    if (xp_cell(ln, r, 2)[0] && !rt_num(xp_cell(ln, r, 2), &v))
      sb_add(&b, L"• Состав, строка %d: количество «%s» — не число\n", r + 1, xp_cell(ln, r, 2)), errs++;
  }
  int withTp = 0;
  wchar_t tpList[400] = L"";
  for (int r = 0; r < op->n; r++) {
    const wchar_t *des = xp_cell(op, r, 0);
    if (!des[0]) sb_add(&b, L"• Операции, строка %d: нет детали — выберите её списком\n", r + 1), errs++;
    else if (!pk_has_item(des))
      sb_add(&b, L"• Операции, строка %d: «%s» нет в «Изделиях» — «Союз» поищет её у себя по обозначению\n", r + 1, des);
    PkSoyItem *si = des[0] ? pk_soy_item(des) : NULL;
    if (si && si->tp && !wcsstr(tpList, des) && wcslen(tpList) + wcslen(des) + 4 < 400) {
      if (tpList[0]) wcscat(tpList, L", ");
      wcscat(tpList, des);
      withTp++;
    }
    /* номер — «005» */
    const wchar_t *num = xp_cell(op, r, 1);
    if (num[0]) {
      wchar_t *end = NULL;
      long n = wcstol(num, &end, 10);
      if (end && !*end && n > 0 && wcslen(num) < 3) {
        wchar_t nb[16];
        _snwprintf(nb, 16, L"%03ld", n);
        pk_put(op, r, 1, nb);
        fixes++;
      }
    }
    /* операция — как в 1С: по ней команда PLM ищет операцию справочника */
    const wchar_t *o = xp_cell(op, r, 2);
    if (!o[0]) {
      sb_add(&b, L"• Операции, строка %d: нет операции\n", r + 1);
      errs++;
      continue;
    }
    wchar_t t1c[PLM_COL1];
    BOOL found = op1c_text(NULL, NULL, NULL, o, t1c, PLM_COL1);
    if (found && wcscmp(t1c, o)) {
      pk_put(op, r, 2, t1c);
      fixes++;
    } else if (!found) {
      sb_add(&b, L"• Операции, строка %d: «%s» нет в справочнике 1С — без кода операция в ТП не пойдёт\n", r + 1, o);
    }
    double v;
    for (int c = 5; c <= 6; c++)
      if (xp_cell(op, r, c)[0] && !rt_num(xp_cell(op, r, c), &v))
        sb_add(&b, L"• Операции, строка %d: «%s» — не число\n", r + 1, xp_cell(op, r, c)), errs++;
  }
  if (withTp) sb_add(&b, L"• В «Союзе» уже есть ТП у: %s — новый ТП «Союз» к ним не создаст\n", tpList);
  for (int r = 0; r < bl->n; r++) {
    const wchar_t *des = xp_cell(bl, r, 0), *mat = xp_cell(bl, r, 1), *norm = xp_cell(bl, r, 2), *sz = xp_cell(bl, r, 3);
    if (!des[0]) sb_add(&b, L"• Заготовки, строка %d: нет детали\n", r + 1), errs++;
    if (!mat[0]) sb_add(&b, L"• Заготовки, строка %d: нет материала (наименование в сортаменте)\n", r + 1), errs++;
    double v;
    if (norm[0] && !rt_num(norm, &v)) sb_add(&b, L"• Заготовки, строка %d: норма «%s» — не число\n", r + 1, norm), errs++;
    if (sz[0] && !pk_sizes_ok(sz)) {
      wchar_t fix[120];
      if (pk_sizes_fix(sz, fix, 120)) pk_put(bl, r, 3, fix), fixes++;
      else sb_add(&b, L"• Заготовки, строка %d: размеры «%s» — нужно «D=40;L=265» или «S=10;B=280;L=440»\n", r + 1, sz), errs++;
    }
  }
  if (fixes) {
    pk_fill(FALSE);
    pk_changed();
  }
  if (b.w && b.w[0] && !quiet) {
    wchar_t *msg = b.w;
    if (wcslen(msg) > 3000) msg[3000] = 0, wcscat(msg, L"\n…");
    MessageBoxW(g_pkWnd, msg, errs ? L"ЭСИ и ТП: есть ошибки" : L"ЭСИ и ТП: замечания", errs ? MB_ICONWARNING : MB_ICONINFORMATION);
  }
  free(b.w);
  if (errs) pk_status(L"Ошибок: %d%s", errs, fixes ? L" (что можно — поправлено)" : L"");
  else pk_status(L"Пакет в порядке%s: изделий %d, состав %d, операций %d, заготовок %d",
                 fixes ? L" (что можно — поправлено)" : L"", it->n, ln->n, op->n, bl->n);
  return errs == 0;
}

static void pk_save(void) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  if (!pk_check(TRUE) &&
      MessageBoxW(g_pkWnd, L"В пакете есть ошибки (см. «Проверить»). Всё равно сохранить?", L"Пакет для загрузки",
                  MB_YESNO | MB_ICONWARNING) != IDYES)
    return;
  wchar_t file[MAX_PATH];
  const wchar_t *root = g_pk[PK_ITEMS].n ? xp_cell(&g_pk[PK_ITEMS], 0, 0) : L"";
  _snwprintf(file, MAX_PATH - 6, L"Пакет для PLM %s", root);
  file[MAX_PATH - 7] = 0;
  for (wchar_t *p = file; *p; p++)
    if (wcschr(L"\\/:*?\"<>|", *p)) *p = L'_';
  wcscat(file, L".xlsx");
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_pkWnd;
  of.lpstrFilter = L"Книга Excel (*.xlsx)\0*.xlsx\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"xlsx";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  XlSheet sh[PK_NT];
  XlBookSheet bs[PK_NT];
  for (int k = 0; k < PK_NT; k++) {
    memset(&sh[k], 0, sizeof(sh[k]));
    sh[k].ncols = kPkSheets[k].ncols;
    sh[k].head = kPkSheets[k].head;
    sh[k].width = kPkSheets[k].width;
    sh[k].plain = 1;
    bs[k].name = kPkSheets[k].name;
    bs[k].title = NULL; /* шапка — в первой строке: так её ищет команда PLM */
    bs[k].sh = &sh[k];
    bs[k].nrows = g_pk[k].n;
    bs[k].cell = xp_cell;
    bs[k].ctx = &g_pk[k];
  }
  if (!xl_save_book(file, bs, PK_NT)) {
    MessageBoxW(g_pkWnd, L"Не удалось записать файл — он не открыт сейчас в Excel?", L"Пакет для загрузки",
                MB_ICONWARNING);
    return;
  }
  pk_status(L"Сохранено: %s — в PLM: «Загрузить ЭСИ из CursorPad»", file);
}

/* Описание для функции CursorPadCreate из листов (без ПАПКА и ПАПКА_ТП — их спросит окно «В Союз…»):
   ИЗДЕЛИЕ, СОСТАВ, ЗАГОТОВКА, ТП, ЧЕРТЕЖ. Строки, пришедшие из «Союза» и не тронутые, — мимо: они там
   есть. Изделие без наименования (кроме стандартных и прочих) — мимо: у существующего функция стёрла бы
   его наименование. Код операции — число в начале «4110 Токарная» (после «Проверить» названия уже
   в виде 1С), код участка — число в начале участка. Возвращает число строк ТП; что пропущено — в note. */
typedef struct {
  int items, links, ops, blanks, draws;
} PkSent;

static int pk_soyuz_text(wchar_t **out, wchar_t *note, int ncap, PkSent *st) {
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  memset(st, 0, sizeof(*st));
  note[0] = 0;
  XpTable *it = &g_pk[PK_ITEMS], *ln = &g_pk[PK_LINKS], *op = &g_pk[PK_OPS], *bl = &g_pk[PK_BLANKS];
  int noName = 0;
  for (int r = 0; r < it->n; r++) {
    const wchar_t *des = xp_cell(it, r, 0);
    if (!des[0]) continue;
    if (xp_cell(it, r, 5)[0]) { /* чертёж — и к новому изделию, и к тому, что уже в «Союзе» */
      sb_add(&b, L"ЧЕРТЕЖ");
      sb_field(&b, des);
      sb_field(&b, xp_cell(it, r, 5));
      sb_add(&b, L"\r\n");
      st->draws++;
    }
    const wchar_t *buy = xp_cell(it, r, 6);
    if (pk_row_from_soyuz(PK_ITEMS, r)) {
      /* из «Союза»: идёт, только если ему выбрали признак покупки — с наименованием оттуда
         (пустое функция поняла бы как «стереть»); сам признак у них не ставится — не затереть */
      PkSoyItem *si = pk_soy_item(des);
      if (!buy[0] || !si || !si->name[0]) continue;
      sb_add(&b, L"ИЗДЕЛИЕ");
      sb_field(&b, des);
      sb_field(&b, si->name);
      for (int c = 2; c < 5; c++) sb_field(&b, xp_cell(it, r, c));
      sb_field(&b, buy);
      sb_add(&b, L"\r\n");
      st->items++;
      continue;
    }
    if (!xp_cell(it, r, 1)[0] && !pk_kind_noname(xp_cell(it, r, 2))) {
      noName++;
      continue;
    }
    sb_add(&b, L"ИЗДЕЛИЕ");
    for (int c = 0; c < 5; c++) sb_field(&b, xp_cell(it, r, c));
    sb_field(&b, buy[0] ? buy : pk_buy_default()); /* признак покупки — значение списка по названию */
    sb_add(&b, L"\r\n");
    st->items++;
  }
  for (int r = 0; r < ln->n; r++) {
    if (!xp_cell(ln, r, 0)[0] || !xp_cell(ln, r, 1)[0] || pk_row_from_soyuz(PK_LINKS, r)) continue;
    sb_add(&b, L"СОСТАВ");
    for (int c = 0; c < 4; c++) sb_field(&b, xp_cell(ln, r, c));
    sb_add(&b, L"\r\n");
    st->links++;
  }
  for (int r = 0; r < bl->n; r++) {
    if (!xp_cell(bl, r, 0)[0] || !xp_cell(bl, r, 1)[0]) continue;
    sb_add(&b, L"ЗАГОТОВКА");
    for (int c = 0; c < 4; c++) sb_field(&b, xp_cell(bl, r, c));
    sb_add(&b, L"\r\n");
    st->blanks++;
  }
  int skipped = 0;
  for (int r = 0; r < op->n; r++) {
    const wchar_t *des = xp_cell(op, r, 0), *o = xp_cell(op, r, 2), *ar = xp_cell(op, r, 3);
    if (!des[0]) continue;
    wchar_t code[8] = L"", area[8] = L"";
    int k = 0;
    for (; o[k] >= L'0' && o[k] <= L'9' && k < 7; k++) code[k] = o[k];
    code[k] = 0;
    if (k < 2) { /* название без номера: справочник 1С знает код («4110 Токарная») */
      wchar_t t1c[PLM_COL1];
      if (op1c_text(NULL, NULL, NULL, o, t1c, PLM_COL1)) lstrcpynW(code, t1c, 5);
      else code[0] = 0;
    }
    if (!code[0]) {
      skipped++;
      continue;
    }
    k = 0;
    while (*ar == L' ') ar++;
    for (; ar[k] >= L'0' && ar[k] <= L'9' && k < 7; k++) area[k] = ar[k];
    area[k] = 0;
    sb_add(&b, L"ТП");
    sb_field(&b, des);
    sb_field(&b, xp_cell(op, r, 1));
    sb_field(&b, code);
    sb_field(&b, area);
    sb_field(&b, xp_cell(op, r, 4));
    sb_field(&b, xp_cell(op, r, 5));
    sb_field(&b, xp_cell(op, r, 6));
    sb_add(&b, L"\r\n");
    st->ops++;
  }
  if (skipped) _snwprintf(note, ncap, L"операций без кода справочника: %d", skipped);
  if (noName) {
    size_t l = wcslen(note);
    _snwprintf(note + l, ncap - l, L"%sизделий без наименования: %d", l ? L"; " : L"", noName);
  }
  note[ncap - 1] = 0;
  *out = b.w;
  return st->ops;
}

static void pk_to_soyuz(void) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  pk_check(TRUE); /* номера «005», названия операций как в 1С — отсюда коды; виды и размеры — как в «Союзе» */
  wchar_t *text = NULL, note[200];
  PkSent st;
  pk_soyuz_text(&text, note, 200, &st);
  if (!text || !text[0]) {
    free(text);
    pk_status(g_pkSoyItN ? L"Нечего отправлять: строки из «Союза» там уже есть — дайте позиции операции, заготовку или чертёж"
                         : L"Нечего отправлять: заполните «Изделия», «Операции» или «Заготовки»");
    return;
  }
  wchar_t label[240];
  _snwprintf(label, 240, L"В «Союз»: изделий %d, состав %d, заготовок %d, операций %d, чертежей %d%s%s", st.items, st.links,
             st.blanks, st.ops, st.draws, note[0] ? L" · " : L"", note);
  label[239] = 0;
  szm_open_text(text, label);
  free(text);
}

static void pk_clear(void) {
  if (MessageBoxW(g_pkWnd, L"Очистить все листы пакета?", L"ЭСИ и ТП", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  xp_package_free(g_pk);
  pk_soy_clear();
  g_pkCur[0] = 0;
  InterlockedIncrement(&g_pkStGen); /* недочитанные чертежи — мимо */
  pk_stamp_forget();
  pk_init();
  pk_fill(FALSE);
  pk_changed();
  pk_status(L"Пакет пуст");
}

/* ---- окно ---- */
static void pk_layout(void) {
  RECT rc;
  GetClientRect(g_pkWnd, &rc);
  int pad = PKS(26), y = PANEL_TITLE_H + PKS(20), h = PKS(30);
  place_panel_close(g_pkWnd);
  for (int k = 0; k < PK_NT; k++) MoveWindow(GetDlgItem(g_pkWnd, ID_PK_TAB + k), pad + k * PKS(132), y, PKS(126), h, TRUE);
  /* справа: обозначение и «Из Союза» — изделие с составом из базы, «Из выгрузки…» — образец */
  int x = rc.right - pad - PKS(120);
  MoveWindow(GetDlgItem(g_pkWnd, ID_PK_FROMXP), x, y, PKS(120), h, TRUE);
  x -= PKS(8 + 100);
  MoveWindow(GetDlgItem(g_pkWnd, ID_PK_ESI), x, y, PKS(100), h, TRUE);
  x -= PKS(6 + 170);
  MoveWindow(g_pkEsiEdit, x, y + PKS(3), PKS(170), h - PKS(6), TRUE);
  int foot = rc.bottom - PKS(TOOL_FOOT_H), by = foot + (PKS(TOOL_FOOT_H) - PKS(30)) / 2;
  int ly = PANEL_TITLE_H + PKS(86);
  /* лист «Операции»: над таблицей — строка быстрого ТП */
  g_pkQShown = g_pkSheet == PK_OPS;
  if (g_pkQPart) {
    int gap = PKS(6), wPart = PKS(170), wOp = PKS(250), wArea = PKS(230), wBtn = PKS(116);
    int wEq = rc.right - pad * 2 - wPart - wOp - wArea - wBtn - gap * 4;
    if (wEq < PKS(100)) wEq = PKS(100);
    HWND q[] = {g_pkQPart, g_pkQOp, g_pkQArea, g_pkQEquip, GetDlgItem(g_pkWnd, ID_PK_QADD)};
    int qw[] = {wPart, wOp, wArea, wEq, wBtn}, qx = pad;
    for (int i = 0; i < 5; i++) {
      if (i < 4) MoveWindow(q[i], qx, ly, qw[i], PKS(320), TRUE);
      else MoveWindow(q[i], qx, ly - PKS(1), qw[i], PKS(27), TRUE);
      ShowWindow(q[i], g_pkQShown ? SW_SHOW : SW_HIDE);
      qx += qw[i] + gap;
    }
    if (g_pkQShown) {
      wchar_t t[200];
      GetWindowTextW(g_pkQPart, t, 200);
      if (!t[0] && g_pkCur[0]) SetWindowTextW(g_pkQPart, g_pkCur); /* выбранная позиция — деталь ТП */
      ly += PKS(34);
    }
  }
  MoveWindow(g_pkList, pad, ly, rc.right - pad * 2, foot - PKS(26) - ly, TRUE);
  /* подвал (с 2026.10.21): правка строк — слева, главное «Сохранить» — справа */
  static const int ids[] = {ID_PK_ADD, ID_PK_ASM, ID_PK_DEL, ID_PK_PASTE, ID_PK_CHECK, ID_PK_DRAW, ID_PK_CLEAR, ID_PK_HELP, ID_PK_SOYUZ};
  static const int w[] = {84, 88, 80, 88, 90, 90, 84, 96, 90};
  x = PKS(16);
  for (int i = 0; i < 9; i++) {
    MoveWindow(GetDlgItem(g_pkWnd, ids[i]), x, by, PKS(w[i]), PKS(30), TRUE);
    x += PKS(w[i] + 8);
  }
  MoveWindow(GetDlgItem(g_pkWnd, ID_PK_SAVE), rc.right - PKS(16) - PKS(170), by, PKS(170), PKS(30), TRUE);
  InvalidateRect(g_pkWnd, NULL, FALSE); /* карточка и подвал — по новому размеру */
}

static void pk_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int foot = rc.bottom - PKS(TOOL_FOOT_H);
  RECT card = {PKS(12), PANEL_TITLE_H + PKS(10), rc.right - PKS(12), foot - PKS(12)};
  tool_frame_paint(hwnd, hdc, L"ЭСИ и ТП", L"изделия, состав, заготовки и техпроцессы — в «Союз» или пакетом для PLM", card,
                   foot);
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  int pad = PKS(26);
  RECT st = {pad, PANEL_TITLE_H + PKS(56), rc.right - pad, PANEL_TITLE_H + PKS(82)};
  DrawTextW(hdc,
            g_pkStatus[0] ? g_pkStatus
                          : L"Перетащите чертежи — штамп прочитает ИИ; на строку сборки — в её состав. Ctrl+V — таблица, "
                            L"маршрут. «Из Союза» — ЭСИ с составом. «Что нужно?» — какие данные.",
            -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* Enter в поле обозначения — «Из Союза» */
static WNDPROC g_pkEsiOld;
static LRESULT CALLBACK PkEsiProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    pk_esi_start();
    return 0;
  }
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n')) return 0;
  return CallWindowProcW(g_pkEsiOld, hwnd, msg, wParam, lParam);
}

static WNDPROC g_pkListOld;
static LRESULT CALLBACK PkListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN) {
    BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (wParam == VK_F2 || wParam == VK_RETURN) {
      int r = ListView_GetNextItem(hwnd, -1, LVNI_FOCUSED);
      if (r >= 0) pk_edit_begin(r, 0);
      return 0;
    }
    if (wParam == VK_DELETE) {
      pk_delete_selected();
      return 0;
    }
    if (wParam == VK_INSERT) {
      pk_add_row();
      return 0;
    }
    if (ctrl && wParam == 'V') {
      pk_paste();
      return 0;
    }
    if (ctrl && wParam == 'A') {
      ListView_SetItemState(hwnd, -1, LVIS_SELECTED, LVIS_SELECTED);
      return 0;
    }
  }
  return CallWindowProcW(g_pkListOld, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK PkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    pk_paint(hwnd, mem);
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
    mm->ptMinTrackSize.x = PKS(1080); /* девять кнопок подвала и «Сохранить»; вверху — листы и «Из Союза» */
    mm->ptMinTrackSize.y = PKS(360);
    return 0;
  }
  case WM_SIZE:
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    pk_layout();
    InvalidateRect(hwnd, NULL, FALSE);
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
  case WM_NOTIFY: {
    NMHDR *nh = (NMHDR *)lParam;
    if (nh->hwndFrom == g_pkList && nh->code == NM_CUSTOMDRAW) { /* пришедшее из «Союза» — серым */
      NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)lParam;
      if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
      if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        int r = (int)cd->nmcd.dwItemSpec;
        if (r >= 0 && r < g_pk[g_pkSheet].n && pk_row_from_soyuz(g_pkSheet, r)) cd->clrText = COL_MUTED;
      }
      return CDRF_DODEFAULT;
    }
    if (nh->hwndFrom == g_pkList && nh->code == NM_DBLCLK) {
      NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lParam;
      if (ia->iItem >= 0 && g_pkSheet == PK_ITEMS && ia->iSubItem == 5) pk_pick_drawing(ia->iItem);
      else if (ia->iItem >= 0) pk_edit_begin(ia->iItem, ia->iSubItem);
      else pk_add_row(); /* по пустому месту — новая строка */
      return 0;
    }
    /* строка «Операций» — её деталь в строку быстрого ТП: дописывать этот ТП */
    if (nh->hwndFrom == g_pkList && nh->code == LVN_ITEMCHANGED && g_pkSheet == PK_OPS && g_pkQPart) {
      NMLISTVIEW *lv = (NMLISTVIEW *)lParam;
      if (lv->iItem >= 0 && lv->iItem < g_pk[PK_OPS].n && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)) {
        const wchar_t *d = xp_cell(&g_pk[PK_OPS], lv->iItem, 0);
        if (d[0]) SetWindowTextW(g_pkQPart, d);
      }
    }
    /* выбрали позицию в «Изделиях» — к ней пойдут вставленные операции и заготовка */
    if (nh->hwndFrom == g_pkList && nh->code == LVN_ITEMCHANGED && g_pkSheet == PK_ITEMS) {
      NMLISTVIEW *lv = (NMLISTVIEW *)lParam;
      if (lv->iItem >= 0 && lv->iItem < g_pk[PK_ITEMS].n && (lv->uNewState & LVIS_SELECTED) &&
          !(lv->uOldState & LVIS_SELECTED) && ListView_GetSelectedCount(g_pkList) == 1) {
        const wchar_t *d = xp_cell(&g_pk[PK_ITEMS], lv->iItem, 0);
        if (d[0] && _wcsicmp(d, g_pkCur)) {
          lstrcpynW(g_pkCur, d, 200);
          PkSoyItem *si = pk_soy_item(d);
          pk_status(L"Выбрано: %s%s — %s", d,
                    si ? (si->tp ? L" (из «Союза», ТП уже есть)" : L" (из «Союза», без ТП)") : L"",
                    !_wcsicmp(xp_cell(&g_pk[PK_ITEMS], lv->iItem, 2), L"Сборочные единицы") || pk_is_parent(d)
                        ? L"сборка: чертежи, брошенные на её строку, лягут в её состав"
                        : L"вставленные операции (Ctrl+V) и заготовка пойдут к нему");
        }
      }
    }
    if (nh->hwndFrom == g_pkList && nh->code == LVN_BEGINSCROLL && g_pkEditRow >= 0) pk_edit_end(TRUE);
    return 0;
  }
  case WM_PK_ESI:
    if (lParam) pk_esi_done((RtJob *)lParam);
    return 0;
  case WM_DROPFILES: /* чертежи мышью: штамп → строка; на строку сборки — в её состав */
    pk_stamp_drop(hwnd, (HDROP)wParam);
    return 0;
  case WM_PK_STAMP:
    if (lParam) pk_stamp_done((void *)lParam);
    return 0;
  case WM_PK_STAMPGO:
    pk_stamp_progress();
    return 0;
  case WM_RT_PROGRESS:
    if (g_pkEsiBusy) pk_status(L"Читаю из «Союза»: позиций %d…", (int)wParam);
    return 0;
  case WM_SZM_DICTS: { /* участки и станки из базы — для списков листа «Операции» */
    SzmDicts *d = (SzmDicts *)lParam;
    if (d) {
      free(g_szmDicts.area);
      free(g_szmDicts.equip);
      free(g_szmDicts.areaId);
      free(g_szmDicts.equipId);
      free(g_szmDicts.areaCode);
      free(g_szmDicts.buy);
      free(g_szmDicts.fit);
      g_szmDicts = *d;
      free(d);
      g_pkComboCol = -1; /* список заполнить заново */
      if (g_szmDicts.err[0]) pk_status(L"Справочники из базы не прочитаны: %s — участки и станки вписывайте руками", g_szmDicts.err);
      else pk_status(L"Из базы «Союза»: участков %d, станков %d, признаков покупки %d — выбор списком в ячейках",
                     g_szmDicts.na, g_szmDicts.ne, g_szmDicts.nb);
      pk_buy_fill(TRUE); /* «Собственного изготовления» — как он записан в базе */
      int eqn = pk_equip_fill();
      if (eqn) pk_status(L"Из базы «Союза»: участков %d, станков %d; к операциям подобрано оборудование: %d", g_szmDicts.na,
                         g_szmDicts.ne, eqn);
      if (eqn || g_pkQEquip) pk_draft_save();
    }
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) {
      if (g_pkEditRow >= 0) pk_edit_end(TRUE);
      ShowWindow(hwnd, SW_HIDE);
    }
    if (id >= ID_PK_TAB && id < ID_PK_TAB + PK_NT) {
      if (g_pkEditRow >= 0) pk_edit_end(TRUE);
      g_pkSheet = id - ID_PK_TAB;
      pk_fill(TRUE);
    }
    if (id == ID_PK_ADD) pk_add_row();
    if (id == ID_PK_ASM) pk_add_asm();
    if (id == ID_PK_QADD) pk_q_add();
    if (id == ID_PK_QOP && HIWORD(wParam) == CBN_EDITCHANGE) pk_q_filter();
    if ((id == ID_PK_QPART || id == ID_PK_QAREA || id == ID_PK_QEQUIP) && HIWORD(wParam) == CBN_DROPDOWN)
      pk_q_list((HWND)lParam);
    if (id == ID_PK_DEL) pk_delete_selected();
    if (id == ID_PK_PASTE) pk_paste();
    if (id == ID_PK_FROMXP) pk_from_export();
    if (id == ID_PK_CHECK) pk_check(FALSE);
    if (id == ID_PK_SAVE) pk_save();
    if (id == ID_PK_CLEAR) pk_clear();
    if (id == ID_PK_SOYUZ) pk_to_soyuz();
    if (id == ID_PK_HELP) pk_help();
    if (id == ID_PK_ESI) pk_esi_start();
    if (id == ID_PK_DRAW) pk_draw_menu();
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_CLOSE:
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* fill — сразу заполнить из собранной выгрузки (кнопка в окне выгрузки) */
static void package_show(BOOL fill) {
  if (!g_pkWnd) {
    HDC s = GetDC(NULL);
    g_pkS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PkProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadPackage";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = PKS(1100), h = PKS(640);
    if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
    if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
    g_pkWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_ACCEPTFILES, L"CursorPadPackage", L"ЭСИ и ТП — CursorPad",
                              WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                              wa.left + (wa.right - wa.left - w) / 2 + PKS(30), wa.top + (wa.bottom - wa.top - h) / 2 + PKS(30),
                              w, h, NULL, NULL, g_inst, NULL);
    if (!g_pkWnd) return;
    round_corners(g_pkWnd);
    mk_btn(g_pkWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_pkWnd, L"–", ID_PANEL_MIN);
    tool_frame_init(g_pkWnd); /* шапка, карточка и подвал — общие (cursorpad.c) */
    for (int k = 0; k < PK_NT; k++) mk_btn(g_pkWnd, kPkSheets[k].name, ID_PK_TAB + k);
    mk_btn(g_pkWnd, L"Из выгрузки…", ID_PK_FROMXP);
    mk_btn(g_pkWnd, L"Из Союза", ID_PK_ESI);
    g_pkEsiEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                                  10, 10, g_pkWnd, (HMENU)(INT_PTR)ID_PK_ESIDES, g_inst, NULL);
    if (g_fontBody) SendMessageW(g_pkEsiEdit, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    SendMessageW(g_pkEsiEdit, EM_SETCUEBANNER, FALSE, (LPARAM)L"обозначение в «Союзе»");
    g_pkEsiOld = (WNDPROC)SetWindowLongPtrW(g_pkEsiEdit, GWLP_WNDPROC, (LONG_PTR)PkEsiProc);
    mk_btn(g_pkWnd, L"+ Строка", ID_PK_ADD);
    mk_btn(g_pkWnd, L"+ Сборка", ID_PK_ASM);
    mk_btn(g_pkWnd, L"Удалить", ID_PK_DEL);
    mk_btn(g_pkWnd, L"Вставить", ID_PK_PASTE);
    mk_btn(g_pkWnd, L"Проверить", ID_PK_CHECK);
    mk_btn(g_pkWnd, L"Чертежи…", ID_PK_DRAW);
    mk_btn(g_pkWnd, L"Сохранить пакет…", ID_PK_SAVE);
    mk_btn(g_pkWnd, L"Очистить", ID_PK_CLEAR);
    mk_btn(g_pkWnd, L"Что нужно?", ID_PK_HELP);
    mk_btn(g_pkWnd, L"В Союз…", ID_PK_SOYUZ);
    g_pkQPart = pk_q_combo(ID_PK_QPART, L"деталь (обозначение)");
    g_pkQOp = pk_q_combo(ID_PK_QOP, L"операция: печатайте «ток», «4110»… Enter");
    g_pkQArea = pk_q_combo(ID_PK_QAREA, L"участок");
    g_pkQEquip = pk_q_combo(ID_PK_QEQUIP, L"оборудование — само по операции и участку");
    mk_btn(g_pkWnd, L"+ Операция", ID_PK_QADD);
    g_pkList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT |
                                                       LVS_SHOWSELALWAYS | WS_CLIPCHILDREN,
                               0, 0, 10, 10, g_pkWnd, NULL, g_inst, NULL);
    SendMessageW(g_pkList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    if (g_fontBody) SendMessageW(g_pkList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    g_pkListOld = (WNDPROC)SetWindowLongPtrW(g_pkList, GWLP_WNDPROC, (LONG_PTR)PkListProc);
    if (!g_pkLoaded) {
      g_pkLoaded = TRUE;
      pk_draft_load();
    }
    SetWindowTextW(g_pkQArea, g_pkQLastArea); /* участок — как в прошлый раз; станок у операции свой */
    g_pkQBusy = TRUE;
    pk_q_filter(); /* весь справочник операций */
    g_pkQBusy = FALSE;
    pk_fill(TRUE);
    pk_layout();
  }
  ShowWindow(g_pkWnd, IsIconic(g_pkWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_pkWnd);
  SetFocus(g_pkList);
  if (!g_szmDictsAsked && g_sqlUser[0] && g_sqlPass[0]) { /* участки и станки — один раз за работу */
    g_szmDictsAsked = TRUE;
    szm_dicts_start(g_pkWnd);
  }
  if (fill) pk_from_export();
}
