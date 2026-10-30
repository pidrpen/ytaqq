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
   (похожее изделие как образец). «Чертежи…» — выбрать файлы чертежей; «Ещё ▾» — из архива по обозначениям:
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
#define ID_PK_DRAW 315 /* «Чертежи…» — выбрать файлы чертежей: штампы, состав и операции ложатся на листы (с 30.10 — без меню) */
#define ID_PK_ASM 316  /* «+ Сборка» — строка сборки: на неё бросают чертежи её состава */
/* строка быстрого ТП над листом «Операции»: деталь, операция (печатать — список сужается),
   участок, станок, «+ Операция» (Enter) — номер 005, 010… сам */
#define ID_PK_QPART 317
#define ID_PK_QOP 318
#define ID_PK_QAREA 319
#define ID_PK_QEQUIP 320
#define ID_PK_QADD 321
#define ID_PK_QALL 322  /* «Все детали» / «Эта деталь»: на листе «Операции» — весь лист или ТП одной детали */
#define ID_PK_QUP 323   /* ▲ ▼ — выделенную операцию выше или ниже в ТП её детали */
#define ID_PK_QDOWN 324
#define ID_PK_MORE 325 /* «Ещё ▾» — редкое: строки, выгрузка, пакет .xlsx, приоритеты, очистка */
#define ID_PK_PRIO 326 /* «Приоритеты операций…» — таблица технолога (soyuz_prio.c) */
#define ID_PK_DRAWFIND 327   /* «Ещё ▾»: чертежи из архива по обозначениям — к изделиям без файла */
#define ID_PK_DRAWTOGGLE 328 /* «Ещё ▾»: чертёж детали — сразу и операции (галочка) */
#define ID_PK_DRAWOPS 329    /* «Ещё ▾»: операции по чертежам — деталям с файлом и без операций */
#define ID_PK_SOYALL 330     /* меню «В Союз…» (с 2026.10.30): всё сразу */
#define ID_PK_SOYESI 331     /*   только ЭСИ — изделия, состав, чертежи */
#define ID_PK_SOYTP 332      /*   только ТП — операции и заготовки к изделиям, уже заведённым в «Союзе» */
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
  int naf;     /* участок операции по коду, и без станка (с 30.10): equip 0 */
  SzmFit *afit;
  wchar_t err[300];
} SzmDicts;
static SzmDicts g_szmDicts;
static BOOL g_szmDictsAsked;
static void szm_open_text(const wchar_t *text, const wchar_t *label); /* soyuz_make.c */
static void szm_dicts_start(HWND notify);                               /* soyuz_make.c */
static int pk_norms_fill(BOOL quiet); /* pk_norms.c: пустые Тпз/Тшт — по таблицам ОМН */
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
static void pk_q_new(void); /* строка быстрого ТП — снова для новой операции */
static void pk_stamp_drop(HWND hwnd, HDROP hd); /* pk_stamp.c: бросили файлы — читать штампы */
static void pk_stamp_done(void *res);           /* pk_stamp.c: WM_PK_STAMP */
static void pk_stamp_progress(void);            /* pk_stamp.c: WM_PK_STAMPGO */
static void pk_stamp_typed(int row);            /* pk_stamp.c: обозначение вписали — состав, который его ждал */
static void pk_stamp_forget(void);              /* pk_stamp.c: «Очистить» */
static void pk_stamp_pick(void);                /* pk_stamp.c: выбрать файлы чертежей — как бросить их */
static void pk_stamp_ops(void);                 /* pk_stamp.c: операции по чертежам деталей без операций */
static BOOL pk_stamp_ops_on(void);              /* pk_stamp.c: чертёж детали — сразу и операции */
static void pks_ops_toggle(void);               /* pk_stamp.c: та галочка */
static void pk_draw_pick(void);                 /* pk_stamp.c: «Чертежи…» — выбрать файлы, сразу и операции */

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

/* Лист «Операции» — ТП одной детали (с 2026.10.29.9): той, что в поле «деталь» строки быстрого
   ТП, по номерам операций; «Все детали» — весь лист, как лежит. Строка списка — не строка
   таблицы: pk_row (строка списка → таблицы) и pk_item (обратно). На других листах — одно и то же. */
static int *g_pkView, g_pkViewN, g_pkViewCap;
static wchar_t g_pkFilt[200]; /* чьи операции показаны; L"" — все строки листа */
static BOOL g_pkQAll;         /* нажали «Все детали»: не сужать, пока не выберут деталь */
static int g_pkQEdit = -1;    /* строка «Операций», загруженная в строку быстрого ТП для правки; -1 — новая */
static BOOL g_pkFilling;      /* список перестраивается: его LVN_ITEMCHANGED — не выбор человека */

static int pk_row(int item) {
  if (!g_pkFilt[0]) return item;
  return item >= 0 && item < g_pkViewN ? g_pkView[item] : -1;
}

static int pk_item(int row) {
  if (!g_pkFilt[0]) return row;
  for (int i = 0; i < g_pkViewN; i++)
    if (g_pkView[i] == row) return i;
  return -1;
}

static int pk_sel_row(void) { return pk_row(ListView_GetNextItem(g_pkList, -1, LVNI_SELECTED)); }

/* номер операции для порядка: «005» → 5; пустой — в конец */
static int pk_op_key(int r) {
  const wchar_t *n = xp_cell(&g_pk[PK_OPS], r, 1);
  return n[0] >= L'0' && n[0] <= L'9' ? _wtoi(n) : 1000000;
}

/* есть ли такая деталь: в «Изделиях» или у операций */
static BOOL pk_is_part(const wchar_t *d) {
  for (int k = 0; d[0] && k < 2; k++) {
    XpTable *t = &g_pk[k ? PK_OPS : PK_ITEMS];
    for (int r = 0; r < t->n; r++)
      if (!_wcsicmp(xp_cell(t, r, 0), d)) return TRUE;
  }
  return FALSE;
}

/* кого показать: на «Операциях» — деталь из поля строки быстрого ТП (если она есть в пакете) */
static void pk_view_build(void) {
  XpTable *t = &g_pk[g_pkSheet];
  g_pkFilt[0] = 0;
  if (g_pkSheet == PK_OPS && g_pkQPart && !g_pkQAll) {
    wchar_t p[200];
    GetWindowTextW(g_pkQPart, p, 200);
    wchar_t *b = p;
    while (*b == L' ') b++;
    size_t l = wcslen(b);
    while (l && b[l - 1] == L' ') b[--l] = 0;
    if (b[0] && pk_is_part(b)) lstrcpynW(g_pkFilt, b, 200);
  }
  if (g_pkViewCap < t->n + 1) {
    int nc = t->n + 64;
    int *v = (int *)realloc(g_pkView, sizeof(int) * (size_t)nc);
    if (!v) {
      g_pkFilt[0] = 0;
      g_pkViewN = 0;
      return;
    }
    g_pkView = v, g_pkViewCap = nc;
  }
  g_pkViewN = 0;
  for (int r = 0; r < t->n; r++) {
    if (g_pkFilt[0] && _wcsicmp(xp_cell(t, r, 0), g_pkFilt)) continue;
    int i = g_pkViewN++, k = g_pkFilt[0] ? pk_op_key(r) : 0;
    while (g_pkFilt[0] && i > 0 && pk_op_key(g_pkView[i - 1]) > k) g_pkView[i] = g_pkView[i - 1], i--; /* по номерам */
    g_pkView[i] = r;
  }
}

static void pk_tabs(void) {
  if (g_pkQPart && (g_pkSheet == PK_OPS) != g_pkQShown) pk_layout(); /* строка быстрого ТП — только у «Операций» */
  for (int k = 0; k < PK_NT; k++) {
    wchar_t s[60];
    if (k == PK_OPS && g_pkSheet == PK_OPS && g_pkFilt[0])
      _snwprintf(s, 60, L"● %s · %d/%d", kPkSheets[k].name, g_pkViewN, g_pk[k].n); /* показаны операции одной детали */
    else
      _snwprintf(s, 60, L"%s%s · %d", k == g_pkSheet ? L"● " : L"", kPkSheets[k].name, g_pk[k].n);
    SetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_TAB + k), s);
  }
  HWND all = GetDlgItem(g_pkWnd, ID_PK_QALL);
  if (all) {
    SetWindowTextW(all, g_pkQAll ? L"Эта деталь" : L"Все детали");
    EnableWindow(all, g_pkQAll || g_pkFilt[0]);
    InvalidateRect(all, NULL, FALSE);
  }
}

static void pk_set_item(int r) {
  XpTable *t = &g_pk[g_pkSheet];
  int it = pk_item(r);
  if (it < 0) return; /* не эта деталь — в списке её нет */
  for (int c = 0; c < t->ncols; c++) {
    LVITEMW si;
    memset(&si, 0, sizeof(si));
    si.iSubItem = c;
    si.pszText = (wchar_t *)xp_cell(t, r, c);
    SendMessageW(g_pkList, LVM_SETITEMTEXTW, (WPARAM)it, (LPARAM)&si);
  }
}

static void pk_edit_end(BOOL commit);
static void pk_fill(BOOL columns) {
  /* правка ячейки — закончить: список перестраивается и прокручивается к началу, поле правки (дочернее окно
     таблицы) уехало бы в шапку («12» поверх «Участка», снимок пользователя 30.10) */
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  if (g_pkEdit) ShowWindow(g_pkEdit, SW_HIDE);
  if (g_pkCombo) ShowWindow(g_pkCombo, SW_HIDE);
  XpTable *t = &g_pk[g_pkSheet];
  g_pkFilling = TRUE;
  SendMessageW(g_pkList, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(g_pkList);
  pk_view_build();
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
  for (int i = 0; i < g_pkViewN; i++) {
    int r = g_pkView[i];
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = i;
    it.pszText = (wchar_t *)xp_cell(t, r, 0);
    SendMessageW(g_pkList, LVM_INSERTITEMW, 0, (LPARAM)&it);
    pk_set_item(r);
  }
  SendMessageW(g_pkList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_pkList, NULL, TRUE);
  g_pkFilling = FALSE;
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
    if (pk_row_from_soyuz(PK_ITEMS, r) || (!xp_cell(t, r, 0)[0] && !xp_cell(t, r, 1)[0]) ||
        !wcscmp(xp_cell(t, r, 2), L"Документация"))
      continue; /* документу сборки (СБ) признак покупки ни к чему */
    if (v[0] && !(dicts && def != kPkBuyOwn && !wcscmp(v, kPkBuyOwn))) continue;
    pk_put(t, r, 6, def);
    if (g_pkSheet == PK_ITEMS && g_pkList) pk_set_item(r);
  }
}

/* станок к операции: какой в «Союзе» ставят на операцию с этим кодом на этом участке (типовые
   операции, потом ТП завода — SzmFit); участок пуст — самый частый у этой операции вообще, и его
   участок — в *areaId. Участка нет в справочнике или пары нет в базе — 0 */
/* код справочной операции: «4110 Токарная» → 4110, «Токарная» — по справочнику 1С; -1 — нет */
static long pk_op_code(const wchar_t *op) {
  while (*op == L' ') op++;
  if (*op >= L'0' && *op <= L'9') return wcstol(op, NULL, 10);
  wchar_t low[200];
  lstrcpynW(low, op, 200);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  for (int i = 0; i < OPS_1C_N; i++)
    if (!wcscmp(kOps1c[i].key, low)) return wcstol(kOps1c[i].num, NULL, 10);
  return -1;
}

/* участок операции из «Союза» (с 30.10): самый весомый по коду — типовая операция, потом ТП завода
   (SzmDicts.afit), и без станка; в out — как в справочнике участков. FALSE — нет */
static BOOL pk_area_for(const wchar_t *op, wchar_t *out, int cap) {
  out[0] = 0;
  long code = op && op[0] ? pk_op_code(op) : -1;
  if (code < 0 || !g_szmDicts.naf || !g_szmDicts.areaId) return FALSE;
  const SzmFit *best = NULL;
  for (int i = 0; i < g_szmDicts.naf; i++)
    if (g_szmDicts.afit[i].code == code && g_szmDicts.afit[i].area && (!best || g_szmDicts.afit[i].score > best->score))
      best = &g_szmDicts.afit[i];
  for (int i = 0; best && i < g_szmDicts.na; i++)
    if (g_szmDicts.areaId[i] == best->area) {
      lstrcpynW(out, g_szmDicts.area[i], cap);
      return TRUE;
    }
  return FALSE;
}

static int pk_equip_for(const wchar_t *op, const wchar_t *area, wchar_t *out, int cap, long *areaId) {
  out[0] = 0;
  *areaId = 0;
  if (!g_szmDicts.nf || !op[0]) return 0;
  long code = pk_op_code(op);
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
  if (best) *areaId = best->area;
  return best != NULL;
}

/* пустое оборудование операций — подобранным; нет и участка (операции по чертежу, маршрут из чата) —
   участок того же станка: где эта операция чаще всего стоит в ТП завода. Сколько подставлено */
/* участок и станок пустой операции — из таблицы приоритетов технолога (soyuz_prio.c), она важнее
   подбора по базе; код участка из таблицы — как в «Союзе», если справочник уже прочитан.
   1 — что-то подставлено */
/* Участок справочника «Союза» по вписанному (с 30.10, правило пользователя: «12» — номер в начале названия
   «12 Инструментальный участок»): название целиком → код → «<номер> » в начале названия («12» ≠ «120 …») →
   номер отдельным словом. В out — название как в справочнике. FALSE — нет (или справочник не прочитан) */
static BOOL pk_area_dict(const wchar_t *typed, wchar_t *out, int cap) {
  out[0] = 0;
  if (!typed || !g_szmDicts.area) return FALSE;
  while (*typed == L' ') typed++;
  wchar_t w[200];
  lstrcpynW(w, typed, 200);
  size_t l = wcslen(w);
  while (l && w[l - 1] == L' ') w[--l] = 0;
  if (!l) return FALSE;
  int hit = -1;
  for (int i = 0; i < g_szmDicts.na && hit < 0; i++)
    if (!_wcsicmp(g_szmDicts.area[i], w)) hit = i;
  for (int i = 0; i < g_szmDicts.na && hit < 0 && g_szmDicts.areaCode; i++)
    if (g_szmDicts.areaCode[i][0] && !_wcsicmp(g_szmDicts.areaCode[i], w)) hit = i;
  for (int i = 0; i < g_szmDicts.na && hit < 0; i++)
    if (!_wcsnicmp(g_szmDicts.area[i], w, l) && g_szmDicts.area[i][l] == L' ') hit = i;
  BOOL digits = TRUE;
  for (size_t k = 0; k < l; k++) digits = digits && w[k] >= L'0' && w[k] <= L'9';
  for (int i = 0; i < g_szmDicts.na && hit < 0 && digits; i++) {
    const wchar_t *a = g_szmDicts.area[i];
    for (const wchar_t *q = a; (q = wcsstr(q, w)) != NULL && hit < 0; q += l)
      if ((q == a || q[-1] == L' ') && (q[l] == L' ' || !q[l])) hit = i;
  }
  if (hit < 0) return FALSE;
  lstrcpynW(out, g_szmDicts.area[hit], cap);
  return TRUE;
}

static int pk_prio_row(XpTable *t, int r) {
  const SzPrio *p = szp_find_eq(xp_cell(t, r, 2), xp_cell(t, r, 4)); /* станок — для группы ЧПУ */
  int k = szp_pick(p, xp_cell(t, r, 3));
  if (k < 0) return 0;
  int set = 0;
  if (!xp_cell(t, r, 3)[0] && p->area[k][0]) {
    const wchar_t *a = p->area[k];
    wchar_t full[200];
    if (pk_area_dict(a, full, 200)) a = full; /* «12» → «12 Инструментальный участок», как в справочнике */
    pk_put(t, r, 3, a);
    set = 1;
  }
  if (!xp_cell(t, r, 4)[0] && p->equip[k][0]) {
    pk_put(t, r, 4, p->equip[k]);
    set = 1;
  }
  return set;
}

static BOOL pk_is_transport(const wchar_t *op);
static int pk_transport_fill(void);
static int g_pkTrTouched; /* транспортирования или номера поменялись — список листа перестроить */

/* участка нет ни в таблице приоритетов, ни в «Союзе» — как у предыдущей операции этой детали (слесарная,
   маркирование, контроль обычно там же; с 30.10); в «Проверить» такие помечены. Справочники ещё читаются —
   ждать их: участок из «Союза» важнее предыдущей. Сколько подставлено */
static int pk_area_prev(void) {
  if (g_szmDictsAsked && !g_szmDicts.na && !g_szmDicts.err[0]) return 0;
  XpTable *t = &g_pk[PK_OPS];
  int n = 0;
  for (int r = 0; r < t->n; r++) {
    if (pk_row_from_soyuz(PK_OPS, r) || xp_cell(t, r, 3)[0]) continue;
    const wchar_t *op = xp_cell(t, r, 2), *des = xp_cell(t, r, 0);
    if (!op[0] || pk_is_transport(op)) continue;
    int me = _wtoi(xp_cell(t, r, 1)), best = -1, bn = -1;
    for (int q = 0; q < t->n; q++) {
      if (q == r || _wcsicmp(xp_cell(t, q, 0), des) || !xp_cell(t, q, 3)[0] || pk_is_transport(xp_cell(t, q, 2))) continue;
      int k = _wtoi(xp_cell(t, q, 1));
      if (k < me && k > bn) bn = k, best = q;
    }
    if (best < 0) continue;
    pk_put(t, r, 3, xp_cell(t, best, 3));
    if (g_pkSheet == PK_OPS && g_pkList) pk_set_item(r);
    n++;
  }
  return n;
}

/* участок пришёл не из таблицы приоритетов и не из «Союза» (взят по предыдущей или вписан руками) — для
   пометки в «Проверить» */
static BOOL pk_area_sourced(const wchar_t *op, const wchar_t *eq) {
  const SzPrio *p = szp_find_eq(op, eq);
  for (int k = 0; p && k < p->n; k++)
    if (p->area[k][0]) return TRUE;
  wchar_t b[200];
  long aid = 0;
  if (pk_area_for(op, b, 200)) return TRUE;
  return pk_equip_for(op, L"", b, 200, &aid) && aid;
}

/* операция по станку (заготовка, гибка, ЧПУ, шлифовальные, зубообработка, мехобработка): без станка — «проверить» */
static BOOL pk_needs_machine(const wchar_t *op) {
  wchar_t key[80];
  szp_key(op, key, 80);
  int g = szp_group_of(szp_code_of(op), key, NULL);
  return g == SZP_G_ZAG || g == SZP_G_GIB || g == SZP_G_CNC || g == SZP_G_SHLIF || g == SZP_G_ZUBO || g == SZP_G_MECH;
}

/* участок и станок каждой операции (с 30.10 — у каждой): своя строка таблицы приоритетов, потом группа
   (pk_prio_row); участок — из «Союза» по коду (типовая операция, потом ТП завода); станок — какой ставят на
   эту операцию на этом участке, а нет такой пары — на любом (в ТП завода у операций участок бывает не
   записан); участка всё нет — как у предыдущей операции детали. Сколько строк тронуто */
static int pk_equip_fill(void) {
  XpTable *t = &g_pk[PK_OPS];
  int n = 0;
  for (int r = 0; r < t->n; r++) {
    if (pk_row_from_soyuz(PK_OPS, r)) continue;
    const wchar_t *op = xp_cell(t, r, 2);
    if (!op[0] || pk_is_transport(op)) continue;
    int did = pk_prio_row(t, r); /* сначала таблица технолога: своя строка, потом группа */
    wchar_t buf[200];
    if (!xp_cell(t, r, 3)[0] && pk_area_for(op, buf, 200)) pk_put(t, r, 3, buf), did = 1;
    if (!xp_cell(t, r, 4)[0] && g_szmDicts.nf) {
      wchar_t eq[200];
      long aid;
      if (pk_equip_for(xp_cell(t, r, 2), xp_cell(t, r, 3), eq, 200, &aid)) {
        pk_put(t, r, 4, eq);
        for (int i = 0; !xp_cell(t, r, 3)[0] && g_szmDicts.areaId && i < g_szmDicts.na; i++)
          if (g_szmDicts.areaId[i] == aid) pk_put(t, r, 3, g_szmDicts.area[i]);
        did = 1;
      } else if (xp_cell(t, r, 3)[0] && pk_equip_for(xp_cell(t, r, 2), L"", eq, 200, &aid)) {
        pk_put(t, r, 4, eq); /* на этом участке пары нет — станок этой операции на любом участке */
        did = 1;
      }
    }
    if (!did) continue;
    if (g_pkSheet == PK_OPS && g_pkList) pk_set_item(r);
    n++;
  }
  return n + pk_area_prev();
}

/* транспортирования и номера — сразу, как операции легли или поменялись (с 30.10): между сменами участка и
   в конце — «0401 Транспортирование», лишние — прочь, номера деталей шагом 5. Список листа — заново,
   выделенная операция остаётся выделенной */
static void pk_ops_settle(void) {
  g_pkTrTouched = 0;
  pk_transport_fill();
  if (!g_pkTrTouched || g_pkSheet != PK_OPS || !g_pkList || !g_pkWnd) return;
  XpTable *t = &g_pk[PK_OPS];
  int sr = pk_sel_row();
  const wchar_t *key = sr >= 0 && sr < t->n ? t->cell[(size_t)sr * (size_t)t->ncols + 2] : NULL;
  pk_fill(FALSE);
  for (int r = 0; key && r < t->n; r++)
    if (t->cell[(size_t)r * (size_t)t->ncols + 2] == key) {
      int it = pk_item(r);
      if (it >= 0) {
        g_pkFilling = TRUE; /* та же строка — не новый выбор человека */
        ListView_SetItemState(g_pkList, it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        g_pkFilling = FALSE;
        ListView_EnsureVisible(g_pkList, it, FALSE);
      }
      break;
    }
}

static void pk_changed(void) {
  pk_buy_fill(FALSE);
  pk_equip_fill();
  pk_ops_settle();     /* транспортирования и номера шагом 5 — сразу (с 30.10) */
  pk_norms_fill(TRUE); /* новые операции — сразу с Тпз/Тшт по таблицам ОМН */
  pk_tabs();
  pk_draft_save();
}

/* таблицу приоритетов сохранили (soyuz_prio.c): пересмотреть пустые участки и станки */
static void pk_prio_changed(void) {
  if (!g_pkWnd) return;
  int n = pk_equip_fill();
  pk_ops_settle();
  pk_tabs();
  pk_draft_save();
  if (g_pkSheet == PK_OPS) pk_fill(FALSE);
  pk_status(L"Приоритеты операций применены: участок или станок подставлен у %d операций", n);
}

/* ---- правка ячейки поверх таблицы ---- */
static void pk_edit_end(BOOL commit);
static void pk_edit_begin(int r, int c);

/* ---- списки по набору (с 30.10, просьба пользователя): операция, участок, станок — и в ячейках листа
   «Операции», и в строке быстрого ТП. Список оставляет только подходящее к напечатанному: сначала то, что
   начинается так же (код «411…» или название «Ток…»), потом то, где так начинается слово («мех», «ЧПУ»,
   «31» после «Цех 3 · »), потом простое вхождение; без регистра. Ничего — строка «нет в справочнике»,
   напечатанное остаётся. Список раскрыт и обновляется при наборе; Enter — выделенная строка, а если
   стрелками не выбирали — первая подходящая; Esc — как было. */
static const wchar_t kPkNone[] = L"— нет в справочнике —";
static BOOL g_pkFiltBusy;          /* фильтр сам раскрывает список и ставит текст — свои уведомления не считать */
static HWND g_pkFiltCb;            /* какой список отфильтрован последним */
static wchar_t g_pkFiltTyped[300]; /* и что в нём было напечатано: выбрали «нет в справочнике» — вернуть это */
static int pk_combo_kind(int sheet, int c);

/* строка i справочника для столбца kind (2 — операции 1С, 3 — участки, 4 — станки); NULL — кончились */
static const wchar_t *pk_src_item(int kind, int i) {
  if (kind == 2) return i < OPS_1C_N ? kOps1c[i].name : NULL;
  if (kind == 3) return g_szmDicts.area && i < g_szmDicts.na ? g_szmDicts.area[i] : NULL;
  if (kind == 4) return g_szmDicts.equip && i < g_szmDicts.ne ? g_szmDicts.equip[i] : NULL;
  return NULL;
}

static BOOL pk_word_sep(wchar_t c) {
  return c == L' ' || c == L'-' || c == L'(' || c == L'.' || c == L',' || c == L'/' || c == 0x00B7 || c == 0x00AB || c == L'"';
}

/* насколько строка подходит к напечатанному low (строчные, без краевых пробелов): 0 — с начала кода или
   названия, 1 — с начала слова, 2 — вхождение, -1 — нет; пусто — подходит всё */
static int pk_match_rank(const wchar_t *item, const wchar_t *low) {
  if (!low[0]) return 0;
  wchar_t s[400];
  lstrcpynW(s, item, 400);
  CharLowerBuffW(s, (DWORD)wcslen(s));
  size_t l = wcslen(low);
  if (!wcsncmp(s, low, l)) return 0; /* «411» → «4110 …», «31» → «31 Механический…», «16к» → «16К20…» */
  const wchar_t *n = s;
  while (*n >= L'0' && *n <= L'9') n++;
  while (*n == L' ') n++;
  if (n != s && !wcsncmp(n, low, l)) return 0; /* название после кода: «ток» → «4110 Токарная» */
  for (const wchar_t *q = s + 1; *q; q++)
    if (pk_word_sep(q[-1]) && !pk_word_sep(*q) && !wcsncmp(q, low, l)) return 1;
  return wcsstr(s, low) ? 2 : -1;
}

/* список cb (столбца kind) — по напечатанному; open — раскрыть его. Напечатанное и курсор в поле остаются.
   Возвращает число подходящих строк (без «нет в справочнике») */
static int pk_combo_filter(HWND cb, int kind, BOOL open) {
  if (!cb || g_pkFiltBusy || kind < 2 || kind > 4) return 0;
  g_pkFiltBusy = TRUE;
  wchar_t t[300], low[300];
  GetWindowTextW(cb, t, 300);
  DWORD s0 = 0, s1 = 0;
  SendMessageW(cb, CB_GETEDITSEL, (WPARAM)&s0, (LPARAM)&s1);
  const wchar_t *b = t;
  while (*b == L' ') b++;
  lstrcpynW(low, b, 300);
  size_t ll = wcslen(low);
  while (ll && low[ll - 1] == L' ') low[--ll] = 0;
  CharLowerBuffW(low, (DWORD)ll);
  if (!wcscmp(t, kPkNone)) low[0] = 0;
  g_pkFiltCb = cb;
  lstrcpynW(g_pkFiltTyped, t, 300);
  SendMessageW(cb, WM_SETREDRAW, FALSE, 0);
  SendMessageW(cb, CB_RESETCONTENT, 0, 0);
  int n = 0;
  for (int rank = 0; rank <= 2 && (rank == 0 || low[0]); rank++)
    for (int i = 0; !low[0] || n < 400; i++) { /* пусто — весь справочник; по набору — первые 400 подходящих */
      const wchar_t *it = pk_src_item(kind, i);
      if (!it) break;
      if (pk_match_rank(it, low) == rank) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)it), n++;
    }
  if (!n && low[0]) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)kPkNone);
  SendMessageW(cb, WM_SETREDRAW, TRUE, 0);
  if (open && low[0] && !SendMessageW(cb, CB_GETDROPPEDSTATE, 0, 0)) {
    SendMessageW(cb, CB_SHOWDROPDOWN, TRUE, 0);
    SetCursor(LoadCursorW(NULL, IDC_ARROW)); /* раскрытый список прячет курсор мыши */
  }
  SendMessageW(cb, CB_SETCURSEL, (WPARAM)-1, 0); /* ничего не выделено: Enter возьмёт первую, ↓ — встанет на неё */
  SetWindowTextW(cb, t);                         /* раскрытие и очистка списка трогают поле — вернуть напечатанное */
  SendMessageW(cb, CB_SETEDITSEL, 0, MAKELPARAM(s0, s1));
  InvalidateRect(cb, NULL, TRUE);
  if (cb == g_pkCombo) g_pkComboCol = -1; /* в списке ячейки — не весь справочник: в следующий раз заполнить заново */
  g_pkFiltBusy = FALSE;
  return n;
}

/* Enter в раскрытом отфильтрованном списке: выделенная строка, а если не выбирали — первая; «нет в
   справочнике» — напечатанное. В out — что поставить; FALSE — список не раскрыт, брать поле как есть */
static BOOL pk_combo_pick(HWND cb, wchar_t *out, int cap) {
  out[0] = 0;
  if (!cb || !SendMessageW(cb, CB_GETDROPPEDSTATE, 0, 0)) return FALSE;
  int sel = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
  if (sel < 0 && SendMessageW(cb, CB_GETCOUNT, 0, 0) > 0) sel = 0;
  if (sel >= 0 && SendMessageW(cb, CB_GETLBTEXTLEN, (WPARAM)sel, 0) < cap) SendMessageW(cb, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)out);
  if (!wcscmp(out, kPkNone)) lstrcpynW(out, cb == g_pkFiltCb ? g_pkFiltTyped : L"", cap);
  return TRUE;
}

/* в поле осталась строка «нет в справочнике» (щёлкнули по ней) — вернуть напечатанное */
static void pk_none_fix(HWND cb, wchar_t *v, int cap) {
  if (!wcscmp(v, kPkNone)) lstrcpynW(v, cb == g_pkFiltCb ? g_pkFiltTyped : L"", cap);
}

static LRESULT CALLBACK PkEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  /* ячейка со списком (операция, участок, станок): набор — фильтр списка */
  int fkind = hwnd != g_pkEdit && g_pkEditCtl == g_pkCombo && g_pkCombo && g_pkEditRow >= 0
                  ? pk_combo_kind(g_pkSheet, g_pkEditCol) : -1;
  if (fkind >= 2 && fkind <= 4 &&
      ((msg == WM_CHAR && wParam != L'\r' && wParam != L'\t' && wParam != 27) || (msg == WM_KEYDOWN && wParam == VK_DELETE) ||
       msg == WM_PASTE || msg == WM_CUT || msg == WM_UNDO)) {
    LRESULT res = CallWindowProcW(old, hwnd, msg, wParam, lParam);
    pk_combo_filter(g_pkCombo, fkind, TRUE);
    return res;
  }
  if (msg == WM_KEYDOWN) {
    if (wParam == VK_ESCAPE) {
      pk_edit_end(FALSE);
      return 0;
    }
    if (wParam == VK_RETURN || wParam == VK_TAB) {
      int r = pk_item(g_pkEditRow), c = g_pkEditCol; /* дальше — по списку: на «Операциях» он может быть одной детали */
      BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      if (wParam == VK_RETURN && fkind >= 2) { /* раскрытый отфильтрованный список: выделенная или первая подходящая */
        wchar_t pick[300];
        if (pk_combo_pick(g_pkCombo, pick, 300)) {
          g_pkFiltBusy = TRUE;
          SendMessageW(g_pkCombo, CB_SHOWDROPDOWN, FALSE, 0);
          SetWindowTextW(g_pkCombo, pick);
          g_pkFiltBusy = FALSE;
        }
      }
      pk_edit_end(TRUE);
      XpTable *t = &g_pk[g_pkSheet];
      if (wParam == VK_TAB) c = shift ? c - 1 : c + 1;
      else r++;
      if (c >= t->ncols) c = 0, r++;
      if (c < 0) c = t->ncols - 1, r--;
      if (r >= 0 && pk_row(r) >= 0 && pk_row(r) < t->n) pk_edit_begin(pk_row(r), c);
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
  int it = pk_item(r);
  if (r < 0 || r >= t->n || c < 0 || c >= t->ncols || it < 0) return;
  RECT rc;
  ListView_EnsureVisible(g_pkList, it, FALSE);
  if (c == 0) {
    ListView_GetItemRect(g_pkList, it, &rc, LVIR_LABEL);
  } else {
    rc.top = c;
    rc.left = LVIR_BOUNDS;
    SendMessageW(g_pkList, LVM_GETSUBITEMRECT, (WPARAM)it, (LPARAM)&rc);
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
    pk_none_fix(ctl, v, 600);
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
  const wchar_t *first = g_pkSheet == PK_ITEMS ? L"" : g_pkSheet == PK_OPS && g_pkFilt[0] ? g_pkFilt
                        : t->n > 1                                     ? xp_cell(t, t->n - 2, 0)
                                                                       : g_pkCur;
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

/* список операций — по напечатанному (пусто — весь справочник): общий фильтр pk_combo_filter */
static void pk_q_filter(void) {
  if (g_pkQBusy || !g_pkQOp) return;
  pk_combo_filter(g_pkQOp, 2, TRUE);
}

/* списки детали, участка и станка — при раскрытии: изделия и справочники могли поменяться */
static void pk_q_list(HWND cb) {
  if (g_pkFiltBusy && (cb == g_pkQArea || cb == g_pkQEquip)) return; /* раскрыл сам фильтр — список уже по набору */
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
  } else if (cb == g_pkQArea || cb == g_pkQEquip) { /* участок и станок — по напечатанному (фильтр сам раскрывает) */
    SetWindowTextW(cb, t);
    if (!g_pkFiltBusy) pk_combo_filter(cb, cb == g_pkQArea ? 3 : 4, FALSE);
    return;
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
  pk_none_fix(g_pkQOp, op, 200), pk_none_fix(g_pkQArea, area, 200), pk_none_fix(g_pkQEquip, eq, 200);
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
  int ed = g_pkQEdit >= 0 && g_pkQEdit < t->n ? g_pkQEdit : -1;
  int last = 0, cnt = 0;
  for (int r = 0; r < t->n; r++)
    if (r != ed && !_wcsicmp(xp_cell(t, r, 0), part)) {
      int v = _wtoi(xp_cell(t, r, 1));
      if (v > last) last = v;
      cnt++;
    }
  wchar_t num[8];
  _snwprintf(num, 8, L"%03d", last + 5);
  num[7] = 0;
  if (ed >= 0) { /* правка загруженной операции: на месте, номер тот же (к другой детали — следующий её номер) */
    if (_wcsicmp(xp_cell(t, ed, 0), part) || !xp_cell(t, ed, 1)[0]) pk_put(t, ed, 1, num);
    lstrcpynW(num, xp_cell(t, ed, 1), 8);
    pk_put(t, ed, 0, part);
    pk_put(t, ed, 2, name);
    pk_put(t, ed, 3, area);
    pk_put(t, ed, 4, eq); /* стёрли станок — pk_changed подберёт заново */
    lstrcpynW(g_pkQLastArea, area, 200);
    lstrcpynW(g_pkCur, part, 200);
    pk_q_new();
    if (g_pkSheet == PK_OPS) pk_fill(FALSE);
    pk_changed();
    SetFocus(g_pkQOp);
    const wchar_t *got = xp_cell(t, ed, 4);
    pk_status(L"Операция %s «%s» изменена: %s%s%s%s. Следующую — печатайте; править — щёлкните строку", num, part, name,
              got[0] ? L", " : L"", got, got[0] && !eq[0] ? L" (подобран по «Союзу»)" : L"");
    return;
  }
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
    ListView_EnsureVisible(g_pkList, pk_item(t->n - 1), FALSE);
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

/* строка быстрого ТП — снова для новой операции: кнопка «+ Операция», операция и станок пусты,
   в списке ничего не выделено */
static void pk_q_new(void) {
  g_pkQEdit = -1;
  if (!g_pkQPart) return;
  HWND b = GetDlgItem(g_pkWnd, ID_PK_QADD);
  SetWindowTextW(b, L"+ Операция");
  InvalidateRect(b, NULL, FALSE);
  g_pkQBusy = TRUE;
  SetWindowTextW(g_pkQOp, L"");
  SetWindowTextW(g_pkQEquip, L"");
  g_pkQBusy = FALSE;
  if (g_pkSheet == PK_OPS && ListView_GetSelectedCount(g_pkList)) ListView_SetItemState(g_pkList, -1, 0, LVIS_SELECTED);
}

/* выделили одну операцию — она в строке быстрого ТП: поправить поля и Enter («Сохранить 010») */
static void pk_q_load(int row) {
  XpTable *t = &g_pk[PK_OPS];
  if (row < 0 || row >= t->n || !g_pkQPart) return;
  g_pkQBusy = TRUE;
  SetWindowTextW(g_pkQPart, xp_cell(t, row, 0));
  SetWindowTextW(g_pkQOp, xp_cell(t, row, 2));
  SetWindowTextW(g_pkQArea, xp_cell(t, row, 3));
  SetWindowTextW(g_pkQEquip, xp_cell(t, row, 4));
  g_pkQBusy = FALSE;
  g_pkQEdit = row;
  wchar_t b[40];
  _snwprintf(b, 40, L"Сохранить %s", xp_cell(t, row, 1));
  b[39] = 0;
  HWND bt = GetDlgItem(g_pkWnd, ID_PK_QADD);
  SetWindowTextW(bt, b);
  InvalidateRect(bt, NULL, FALSE);
  pk_status(L"Правка операции %s %s: поменяйте поля вверху, Enter — сохранить; ▲ ▼ — переставить; Delete — удалить; Esc — "
            L"новая операция",
            xp_cell(t, row, 1), xp_cell(t, row, 2));
}

/* ▲ ▼: выделенная операция меняется местами с соседней по номеру в ТП своей детали — номера
   остаются на местах (005, 010…), переезжает содержимое: операция, участок, станок, нормы */
static void pk_q_move(int dir) {
  XpTable *t = &g_pk[PK_OPS];
  int row = g_pkSheet == PK_OPS && ListView_GetSelectedCount(g_pkList) == 1 ? pk_sel_row() : -1;
  if (row < 0 || row >= t->n) {
    pk_status(L"Выделите одну операцию — ▲ ▼ переставят её выше или ниже в ТП детали");
    return;
  }
  const wchar_t *part = xp_cell(t, row, 0);
  int me = pk_op_key(row), best = -1, bk = 0;
  for (int r = 0; r < t->n; r++) {
    if (r == row || _wcsicmp(xp_cell(t, r, 0), part)) continue;
    int k = pk_op_key(r);
    if (dir < 0 ? k < me && (best < 0 || k > bk) : k > me && (best < 0 || k < bk)) best = r, bk = k;
  }
  if (best < 0) {
    pk_status(dir < 0 ? L"Операция и так первая в ТП «%s»" : L"Операция и так последняя в ТП «%s»", part);
    return;
  }
  for (int c = 2; c < t->ncols; c++) {
    wchar_t **a = &t->cell[(size_t)row * (size_t)t->ncols + (size_t)c], **b = &t->cell[(size_t)best * (size_t)t->ncols + (size_t)c];
    wchar_t *x = *a;
    *a = *b, *b = x;
  }
  pk_fill(FALSE);
  int it = pk_item(best);
  ListView_SetItemState(g_pkList, it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); /* загрузит её */
  ListView_EnsureVisible(g_pkList, it, FALSE);
  pk_changed();
  pk_status(L"Операция «%s» теперь %s в ТП «%s»", xp_cell(t, best, 2), xp_cell(t, best, 1), xp_cell(t, best, 0));
}

/* в поле «деталь» выбрали или вписали деталь — на листе её операции (sel — выбор в списке:
   поле ещё со старым текстом) */
static void pk_q_part_changed(BOOL sel) {
  if (g_pkQBusy || !g_pkQPart) return;
  if (sel) {
    int i = (int)SendMessageW(g_pkQPart, CB_GETCURSEL, 0, 0);
    wchar_t v[200] = L"";
    if (i >= 0 && SendMessageW(g_pkQPart, CB_GETLBTEXTLEN, (WPARAM)i, 0) < 200)
      SendMessageW(g_pkQPart, CB_GETLBTEXT, (WPARAM)i, (LPARAM)v);
    g_pkQBusy = TRUE;
    SetWindowTextW(g_pkQPart, v);
    g_pkQBusy = FALSE;
  }
  wchar_t was[200];
  lstrcpynW(was, g_pkFilt, 200);
  g_pkQAll = FALSE;
  if (g_pkSheet != PK_OPS) return;
  pk_view_build();
  if (wcscmp(was, g_pkFilt)) {
    if (g_pkQEdit >= 0) pk_q_new();
    pk_fill(FALSE);
    if (g_pkFilt[0])
      pk_status(L"ТП «%s»: операций %d — щёлкните строку, чтобы поправить; новую — печатайте в поле операции", g_pkFilt,
                g_pkViewN);
  }
}

/* «Все детали» ↔ «Эта деталь» */
static void pk_q_all(void) {
  g_pkQAll = !g_pkQAll && g_pkFilt[0];
  if (!g_pkQAll) {
    wchar_t p[200];
    GetWindowTextW(g_pkQPart, p, 200);
    if (!p[0] && g_pkCur[0]) SetWindowTextW(g_pkQPart, g_pkCur);
  }
  if (g_pkQEdit >= 0) pk_q_new();
  pk_fill(FALSE);
  pk_status(g_pkQAll ? L"Все операции листа — щёлкните строку, чтобы поправить; «Эта деталь» — снова одна"
            : g_pkFilt[0] ? L"Операции «%s» — по номерам; другую деталь выберите в поле слева"
                          : L"Впишите или выберите деталь слева — покажу только её операции",
            g_pkFilt);
}

/* Enter в полях строки быстрого ТП — «+ Операция» (или «Сохранить» загруженную); в раскрытом списке —
   сперва выбранное; Esc — бросить правку, дальше — новая операция */
static LRESULT CALLBACK PkQProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  HWND cb = GetParent(hwnd);
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\n' || wParam == L'\t' || wParam == 27)) return 0; /* без писка */
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && !SendMessageW(cb, CB_GETDROPPEDSTATE, 0, 0)) {
    if (g_pkQEdit >= 0) {
      pk_q_new();
      pk_status(L"Правка брошена — печатайте новую операцию");
    }
    return 0;
  }
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
      wchar_t t[300] = L"";
      pk_combo_pick(cb, t, 300); /* выделенная, а если не выбирали — первая подходящая; «нет в справочнике» — набранное */
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
  char *del = t->n ? (char *)calloc((size_t)t->n, 1) : NULL; /* строки списка — не по порядку таблицы */
  if (!del) return;
  for (int i = ListView_GetNextItem(g_pkList, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(g_pkList, i, LVNI_SELECTED)) {
    int r = pk_row(i);
    if (r >= 0 && r < t->n) del[r] = 1;
  }
  if (g_pkSheet == PK_OPS) pk_q_new(); /* номера строк сдвинутся — загруженная правка больше не та */
  for (int r = t->n - 1; r >= 0; r--)
    if (del[r]) pk_del_row(t, r), removed++;
  free(del);
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
  int r = pk_sel_row();
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
    int r = pk_sel_row();
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
      L"Меню кнопки: «ЭСИ и ТП — всё сразу»; «Только ЭСИ» — изделия, состав, чертежи (ТП потом); «Только ТП» —\r\n"
      L"операции и заготовки к изделиям, которые уже есть в «Союзе» (ищутся по обозначению; нет — строка в отчёте).\r\n"
      L"Справочники функция не пополняет: чего в них нет (материал, участок, станок, операция), пишет в отчёт.\r\n\r\n"
      L"ИЗДЕЛИЯ — только для новых; уже заведённые «Союз» находит по обозначению, их строки не нужны.\r\n"
      L"  Обозначение   обязательно, как в штампе: «АБВГ.123456.002».\r\n"
      L"  Наименование  обязательно. У стандартных и прочих изделий его нет: обозначение = «Болт М6х20 ГОСТ 7798-70».\r\n"
      L"                У изделия, которое уже есть, наименование выравнивается по строке; пустое стёрло бы его —\r\n"
      L"                поэтому строки без наименования в «Союз» не уходят (только в состав).\r\n"
      L"  Вид изделия   как в «Союзе»: Сборочные единицы, Детали, Детали БЧ, Стандартные изделия, Прочие изделия,\r\n"
      L"                Материалы, Комплекты, Комплексы, Комплекты разборки, Документация (СБ сборки). Пусто —\r\n"
      L"                «Детали». Выбор — списком.\r\n"
      L"  Материал      наименование материала в сортаменте целиком, как в «Союзе»:\r\n"
      L"                «Круг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013». Нет такого — изделие без материала.\r\n"
      L"  Масса, кг     по желанию.\r\n"
      L"  Признак покупки  сам — «Собственного изготовления»; сменить на «Покупное»… — выбор списком (значения\r\n"
      L"                из базы «Союза», если есть вход). Изделию «Из Союза» — только если выбрать. Нужен пакет 1.0.0.5.\r\n"
      L"  Чертёж (файл) по желанию: файл ляжет к изделию подлинником, как «Загрузить подлинники с локального\r\n"
      L"                диска» (нужна новая функция пакета). «Ещё ▾» → «Чертежи из архива» — найти по обозначениям.\r\n"
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
      L"  У каждой детали свой ТП — не сквозной (пакет 1.0.0.10 снимает «Сквозной»); версия 1 «В разработке»,\r\n"
      L"  вариант «Основной», признак «основной ТП»; ТП вносится в карточку ТП детали. Если у детали ТП уже\r\n"
      L"  есть — новый не создаётся (такие позиции «Проверить» назовёт; сквозной назовёт отчёт «Союза»).\r\n"
      L"  Папка для ТП — в окне «В Союз…»; пусто — «Для основных ТП».\r\n\r\n"
      L"ЧЕРТЕЖИ МЫШЬЮ\r\n"
      L"  Перетащите в окно чертежи (PDF, TIF, PNG, JPG; папку — все картинки в ней): обозначение и наименование\r\n"
      L"  со штампа читает мини-ИИ с картинками (если включён; иначе Tesseract — сверьте), файл ложится в «Чертёж».\r\n"
      L"  Не прочиталось (КОМПАС, DWG, без штампа) — из имени файла «АБВГ.123456.002 Корпус.pdf».\r\n"
      L"  На пустое место — новые изделия; один чертёж на строку детали — в эту строку.\r\n"
      L"  Сборка: «+ Сборка» (или любая строка вида «Сборочные единицы»), и бросайте на её строку чертежи —\r\n"
      L"  лягут в её состав по 1 шт (количество — на листе «Состав»). Её СБ даёт ей обозначение и наименование,\r\n"
      L"  спецификация — ещё и весь состав. На листе «Состав» — бросок на строку кладёт в сборку этой строки.\r\n"
      L"  Сборочный чертёж («…000 СБ», ВО, ГЧ, МЧ, УЧ) сам ложится документом в состав своей сборки: строка\r\n"
      L"  «АБВГ.123456.000СБ Сборочный чертеж», вид «Документация», файл — к ней (в «Союзе» — подлинник СБ).\r\n"
      L"  Чертёж детали — сразу и операции (галочка в «Ещё ▾»; включена): мини-ИИ читает весь лист (ТТ, Ra,\r\n"
      L"  допуски, резьбы), маршрут и нормы — правила технолога, как «Маршрут обработки» в чате; операции с кодами\r\n"
      L"  1С — на лист «Операции» к этой детали, участок и станок — как чаще всего в ТП завода. Свои операции\r\n"
      L"  у детали уже есть — не трогаются. «Ещё ▾» → «Операции по чертежам» — то же для лежащих строк.\r\n\r\n"
      L"ТП БЫСТРО — лист «Операции», строка над таблицей: деталь (выбранная позиция — сама), операция — начните\r\n"
      L"  печатать («ток», «4110»), список сузится; участок остаётся для следующей; станок пустой — подберётся сам\r\n"
      L"  (какой в «Союзе» стоит на этой операции на этом участке), можно вписать свой. Tab — к следующему полю.\r\n"
      L"  Enter или «+ Операция» — строка с номером 005, 010…; потом «В Союз…» — ТП создастся в папке ТП\r\n"
      L"  и привяжется к детали.\r\n"
      L"  Лист показывает операции только детали из поля слева, по номерам (на вкладке — «5/40»); «Все детали» —\r\n"
      L"  весь лист. Щелчок по операции — она в строке вверху: поправьте и Enter («Сохранить 010»), Esc — бросить;\r\n"
      L"  ▲ ▼ (или Ctrl+↑ ↓) — переставить выше или ниже, номера остаются на местах; Delete — удалить.\r\n\r\n"
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
/* ---- «Транспортирование» (с 2026.10.29): при смене участка между соседними операциями детали — операция
   0401 «Транспортирование» с участком следующей операции (куда едет); в конце маршрута детали
   собственного изготовления, входящей в сборку, — на участок первой операции этой сборки. Соседние
   операции на одном участке — без транспортирования; уже стоящее не дублируется. Номера операций
   детали пересчитываются по порядку с тем же шагом (005, 010…). Нормы на транспортирование не
   считаются; строки обычные — их можно удалить или поменять. Сколько строк добавлено */
static BOOL pk_is_transport(const wchar_t *op) {
  while (*op == L' ') op++;
  return !wcsncmp(op, L"0401", 4) || wcsstr(op, L"ранспортир") != NULL;
}

static void pk_area_code_of(const wchar_t *area, wchar_t *out, int cap) {
  while (*area == L' ') area++;
  int k = 0;
  for (; area[k] >= L'0' && area[k] <= L'9' && k < cap - 1; k++) out[k] = area[k];
  out[k] = 0;
  if (!k) lstrcpynW(out, area, cap); /* участок без номера — сравниваем как текст */
}

/* участок первой операции изделия des (по номеру); FALSE — нет операций с участком */
static BOOL pk_first_area(const wchar_t *des, wchar_t *out, int cap) {
  XpTable *t = &g_pk[PK_OPS];
  int best = -1;
  for (int r = 0; r < t->n; r++)
    if (!_wcsicmp(xp_cell(t, r, 0), des) && xp_cell(t, r, 3)[0] && !pk_is_transport(xp_cell(t, r, 2)) &&
        (best < 0 || _wtoi(xp_cell(t, r, 1)) < _wtoi(xp_cell(t, best, 1))))
      best = r;
  if (best < 0) return FALSE;
  lstrcpynW(out, xp_cell(t, best, 3), cap);
  return TRUE;
}

static int pk_transport_fill(void) {
  XpTable *t = &g_pk[PK_OPS], *ln = &g_pk[PK_LINKS], *it = &g_pk[PK_ITEMS];
  int added = 0;
  wchar_t parts[200][200];
  int np = 0;
  for (int r = 0; r < t->n && np < 200; r++) {
    const wchar_t *des = xp_cell(t, r, 0);
    if (!des[0] || pk_row_from_soyuz(PK_OPS, r)) continue;
    int seen = 0;
    for (int q = 0; q < np && !seen; q++) seen = !_wcsicmp(parts[q], des);
    if (!seen) lstrcpynW(parts[np++], des, 200);
  }
  /* лишние транспортирования — прочь (с 30.10): до и после один участок (поменяли участок, удалили
     операцию) или два подряд; оставшимся — участок следующей операции */
  {
    char *del = t->n ? (char *)calloc((size_t)t->n, 1) : NULL;
    for (int p = 0; p < np && del; p++) {
      const wchar_t *des = parts[p];
      int order[200], n = 0;
      for (int r = 0; r < t->n && n < 200; r++)
        if (!_wcsicmp(xp_cell(t, r, 0), des) && xp_cell(t, r, 2)[0]) order[n++] = r;
      for (int i = 1; i < n; i++)
        for (int k = i; k > 0 && _wtoi(xp_cell(t, order[k - 1], 1)) > _wtoi(xp_cell(t, order[k], 1)); k--) {
          int tmp = order[k];
          order[k] = order[k - 1];
          order[k - 1] = tmp;
        }
      wchar_t endArea[200] = L"";
      for (int r = 0; r < ln->n && !endArea[0]; r++)
        if (!_wcsicmp(xp_cell(ln, r, 1), des) && xp_cell(ln, r, 0)[0] && _wcsicmp(xp_cell(ln, r, 0), des))
          pk_first_area(xp_cell(ln, r, 0), endArea, 200);
      for (int i = 0; i < n; i++) {
        int r = order[i];
        if (!pk_is_transport(xp_cell(t, r, 2)) || pk_row_from_soyuz(PK_OPS, r)) continue;
        if (i > 0 && pk_is_transport(xp_cell(t, order[i - 1], 2)) && !del[order[i - 1]]) {
          del[r] = 1; /* два подряд */
          continue;
        }
        const wchar_t *pa = L"", *na = L"";
        for (int j = i - 1; j >= 0 && !pa[0]; j--)
          if (!pk_is_transport(xp_cell(t, order[j], 2))) pa = xp_cell(t, order[j], 3);
        for (int j = i + 1; j < n && !na[0]; j++)
          if (!pk_is_transport(xp_cell(t, order[j], 2))) na = xp_cell(t, order[j], 3);
        BOOL last = TRUE;
        for (int j = i + 1; j < n; j++)
          if (!pk_is_transport(xp_cell(t, order[j], 2))) last = FALSE;
        if (last) na = endArea;
        if (!pa[0] || !na[0]) continue;
        wchar_t a[40], b2[40], c3[40];
        pk_area_code_of(pa, a, 40);
        pk_area_code_of(na, b2, 40);
        pk_area_code_of(xp_cell(t, r, 3), c3, 40);
        if (!wcscmp(a, b2)) del[r] = 1;
        else if (wcscmp(c3, b2)) pk_put(t, r, 3, na), g_pkTrTouched++;
      }
    }
    for (int r = t->n - 1; del && r >= 0; r--)
      if (del[r]) {
        pk_del_row(t, r);
        if (g_pkQEdit == r) g_pkQEdit = -1;
        else if (g_pkQEdit > r) g_pkQEdit--;
        g_pkTrTouched++;
      }
    free(del);
  }
  for (int p = 0; p < np; p++) {
    const wchar_t *des = parts[p];
    /* покупное — не наш маршрут */
    BOOL purchased = FALSE;
    for (int r = 0; r < it->n; r++)
      if (!_wcsicmp(xp_cell(it, r, 0), des) && wcsstr(xp_cell(it, r, 6), L"окупн")) purchased = TRUE;
    if (purchased) continue;
    /* операции детали по номерам */
    int order[200], n = 0;
    for (int r = 0; r < t->n && n < 200; r++)
      if (!_wcsicmp(xp_cell(t, r, 0), des) && xp_cell(t, r, 2)[0]) order[n++] = r;
    for (int i = 1; i < n; i++)
      for (int k = i; k > 0 && _wtoi(xp_cell(t, order[k - 1], 1)) > _wtoi(xp_cell(t, order[k], 1)); k--) {
        int tmp = order[k];
        order[k] = order[k - 1];
        order[k - 1] = tmp;
      }
    if (!n) continue;
    /* куда едет в конце: участок первой операции сборки, в которую входит */
    wchar_t endArea[200] = L"";
    for (int r = 0; r < ln->n && !endArea[0]; r++)
      if (!_wcsicmp(xp_cell(ln, r, 1), des) && xp_cell(ln, r, 0)[0] && _wcsicmp(xp_cell(ln, r, 0), des))
        pk_first_area(xp_cell(ln, r, 0), endArea, 200);
    /* новая последовательность: строки + транспортирования между сменами участка и в конце */
    int seq[400], ns = 0;
    wchar_t prev[200] = L"";
    for (int i = 0; i < n; i++) {
      int r = order[i];
      const wchar_t *area = xp_cell(t, r, 3);
      BOOL tr = pk_is_transport(xp_cell(t, r, 2));
      if (!tr && area[0] && prev[0]) {
        wchar_t a[40], b[40];
        pk_area_code_of(area, a, 40);
        pk_area_code_of(prev, b, 40);
        BOOL already = ns && pk_is_transport(xp_cell(t, seq[ns - 1], 2));
        if (wcscmp(a, b) && !already && ns < 398) {
          wchar_t **w = xp_row(t);
          if (!w) break;
          t = &g_pk[PK_OPS]; /* таблица могла переехать */
          int nr = t->n - 1;
          xp_set(w, 0, des);
          xp_set(w, 2, L"0401 Транспортирование");
          xp_set(w, 3, area);
          seq[ns++] = nr;
          added++;
          g_pkTrTouched++;
        }
      }
      if (ns < 399) seq[ns++] = r;
      if (area[0]) lstrcpynW(prev, tr ? area : area, 200);
    }
    if (endArea[0] && ns && !pk_is_transport(xp_cell(t, seq[ns - 1], 2))) {
      wchar_t a[40], b[40];
      pk_area_code_of(endArea, a, 40);
      pk_area_code_of(prev, b, 40);
      if ((!prev[0] || wcscmp(a, b)) && ns < 399) {
        wchar_t **w = xp_row(t);
        if (w) {
          t = &g_pk[PK_OPS];
          xp_set(w, 0, des);
          xp_set(w, 2, L"0401 Транспортирование");
          xp_set(w, 3, endArea);
          seq[ns++] = t->n - 1;
          added++;
          g_pkTrTouched++;
        }
      }
    }
    /* номера — по порядку с шагом 5 */
    for (int i = 0; i < ns; i++) {
      wchar_t num[8];
      _snwprintf(num, 8, L"%03d", (i + 1) * 5);
      num[7] = 0;
      if (wcscmp(xp_cell(t, seq[i], 1), num)) pk_put(t, seq[i], 1, num), g_pkTrTouched++;
    }
  }
  return added;
}

static int g_pkSendWarn;  /* предупреждения «Проверить», о которых надо сказать до «В Союз…» (с 30.10) */
static BOOL g_pkNoBox;     /* стенд: окна сообщений не показывать (они бы ждали человека) */
static wchar_t *g_pkLastCheck; /* текст последнего «Проверить» — стенду и статусу */

static BOOL pk_check(BOOL quiet) {
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  int errs = 0, fixes = 0;
  g_pkSendWarn = 0;
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
  int trn = pk_transport_fill();
  if (trn) {
    sb_add(&b, L"• Транспортирование добавлено: %d (при смене участка и в конце — на участок сборки)\n", trn);
    if (g_pkSheet == PK_OPS) pk_fill(FALSE);
  }
  int nmf = pk_norms_fill(TRUE);
  if (nmf) sb_add(&b, L"• Нормы Тпз/Тшт по таблицам ОМН посчитаны у %d операций (Тшт — только где есть машинное время)\n", nmf);
  if (g_szmDicts.na) { /* участка нет в справочнике «Союза» (группа по умолчанию с заводским номером, своя цифра) — до отправки */
    wchar_t bad[600] = L"";
    int nb = 0;
    for (int r = 0; r < op->n; r++) {
      const wchar_t *ar = xp_cell(op, r, 3);
      wchar_t full[200];
      if (!ar[0] || pk_row_from_soyuz(PK_OPS, r) || pk_area_dict(ar, full, 200)) continue;
      BOOL seen = FALSE;
      wchar_t needle[220];
      _snwprintf(needle, 220, L"«%s»", ar);
      needle[219] = 0;
      seen = wcsstr(bad, needle) != NULL;
      nb++;
      size_t l = wcslen(bad);
      if (!seen && l < 500) _snwprintf(bad + l, 600 - l, L"%s%s", l ? L", " : L"", needle);
    }
    bad[599] = 0;
    if (nb) {
      sb_add(&b, L"• Участка %s нет в справочнике «Союза» (операций: %d) — в ТП он не попадёт. Впишите свой участок в "
                 L"«Приоритетах операций» (Ещё ▾: группы ЗАГОТОВКА, МЕХОБРАБОТКА… — номер в начале названия, как «31 Механический "
                 L"участок 31») или выберите из списка в ячейке\n",
             bad, nb);
      g_pkSendWarn++;
    }
  }
  if (g_szmDictsAsked && !g_szmDicts.err[0] && g_szmDicts.na && !g_szmDicts.nb) { /* признак покупки — в базе списка нет */
    int withBuy = 0;
    for (int r = 0; r < it->n; r++) withBuy += xp_cell(it, r, 6)[0] != 0;
    if (withBuy) {
      sb_add(&b, L"• В этой базе «Признак покупки» не список — функция «Союза» его не поставит (изделий с признаком: %d), "
                 L"остальное создастся\n",
             withBuy);
      g_pkSendWarn++;
    }
  }
  { /* участок по предыдущей операции (или вписан руками) и операции по станку без станка — проверить (с 30.10) */
    wchar_t prevList[600] = L"", noEq[600] = L"";
    int np2 = 0, ne2 = 0;
    for (int r = 0; r < op->n; r++) {
      const wchar_t *o = xp_cell(op, r, 2);
      if (!o[0] || pk_is_transport(o) || pk_row_from_soyuz(PK_OPS, r)) continue;
      if (xp_cell(op, r, 3)[0] && !pk_area_sourced(o, xp_cell(op, r, 4))) {
        np2++;
        size_t l = wcslen(prevList);
        if (l < 500) _snwprintf(prevList + l, 600 - l, L"%s%s %s %s", l ? L", " : L"", xp_cell(op, r, 0), xp_cell(op, r, 1), o);
      }
      if (!xp_cell(op, r, 4)[0] && pk_needs_machine(o)) {
        ne2++;
        size_t l = wcslen(noEq);
        if (l < 500) _snwprintf(noEq + l, 600 - l, L"%s%s %s %s", l ? L", " : L"", xp_cell(op, r, 0), xp_cell(op, r, 1), o);
      }
    }
    prevList[599] = noEq[599] = 0;
    if (np2) sb_add(&b, L"• Участок как у предыдущей операции (нет ни в приоритетах, ни в «Союзе») — проверить (%d): %s\n", np2, prevList);
    if (ne2) sb_add(&b, L"• Без оборудования — проверить (%d): %s\n", ne2, noEq);
  }
  { /* участок не подобрался (заготовка без своей строки в приоритетах, редкая операция) — проверить */
    int noArea = 0;
    for (int r = 0; r < op->n; r++)
      if (xp_cell(op, r, 2)[0] && !xp_cell(op, r, 3)[0] && !pk_is_transport(xp_cell(op, r, 2))) noArea++;
    if (noArea) sb_add(&b, L"• Операций без участка: %d — проверить (заготовке нужна своя строка в приоритетах)\n", noArea);
  }
  free(g_pkLastCheck);
  g_pkLastCheck = b.w && b.w[0] ? _wcsdup(b.w) : NULL;
  if (b.w && b.w[0] && !quiet && !g_pkNoBox) {
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
   ИЗДЕЛИЕ, СОСТАВ, ЗАГОТОВКА, ТП, ЧЕРТЕЖ. mode (с 2026.10.30, John: «создать ЭСИ от ТП отдельно»):
   PK_SEND_ESI — ИЗДЕЛИЕ, СОСТАВ, ЧЕРТЕЖ; PK_SEND_TP — ТП и ЗАГОТОВКА (к изделиям, которые «Союз» найдёт
   по обозначению) и строка-комментарий «# СБОРКА» — головная сборка листов, для папки ТП сборки. Строки, пришедшие из «Союза» и не тронутые, — мимо: они там
   есть. Изделие без наименования (кроме стандартных и прочих) — мимо: у существующего функция стёрла бы
   его наименование. Код операции — число в начале «4110 Токарная» (после «Проверить» названия уже
   в виде 1С), код участка — число в начале участка. Возвращает число строк ТП; что пропущено — в note. */
typedef struct {
  int items, links, ops, blanks, draws;
} PkSent;
enum { PK_SEND_ALL, PK_SEND_ESI, PK_SEND_TP };

/* головная сборка листов: сборочная единица, которая ни во что не входит; FALSE — такой нет */
static BOOL pk_head_asm(const wchar_t **des, const wchar_t **name) {
  XpTable *it = &g_pk[PK_ITEMS], *ln = &g_pk[PK_LINKS];
  for (int r = 0; r < it->n; r++) {
    const wchar_t *d = xp_cell(it, r, 0);
    if (!d[0] || !wcsstr(xp_cell(it, r, 2), L"борочн")) continue;
    BOOL child = FALSE;
    for (int q = 0; q < ln->n && !child; q++) child = !_wcsicmp(xp_cell(ln, q, 1), d);
    if (child) continue;
    *des = d;
    *name = xp_cell(it, r, 1);
    PkSoyItem *si = (*name)[0] ? NULL : pk_soy_item(d);
    if (si) *name = si->name;
    return TRUE;
  }
  return FALSE;
}

static int pk_soyuz_text(wchar_t **out, wchar_t *note, int ncap, PkSent *st, int mode) {
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  memset(st, 0, sizeof(*st));
  note[0] = 0;
  XpTable *it = &g_pk[PK_ITEMS], *ln = &g_pk[PK_LINKS], *op = &g_pk[PK_OPS], *bl = &g_pk[PK_BLANKS];
  int noName = 0;
  BOOL esi = mode != PK_SEND_TP, tp = mode != PK_SEND_ESI;
  for (int r = 0; r < (esi ? it->n : 0); r++) {
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
    sb_field(&b, buy[0] || !wcscmp(xp_cell(it, r, 2), L"Документация") ? buy : pk_buy_default()); /* признак покупки */
    sb_add(&b, L"\r\n");
    st->items++;
  }
  for (int r = 0; r < (esi ? ln->n : 0); r++) {
    if (!xp_cell(ln, r, 0)[0] || !xp_cell(ln, r, 1)[0] || pk_row_from_soyuz(PK_LINKS, r)) continue;
    sb_add(&b, L"СОСТАВ");
    for (int c = 0; c < 4; c++) sb_field(&b, xp_cell(ln, r, c));
    sb_add(&b, L"\r\n");
    st->links++;
  }
  for (int r = 0; r < (tp ? bl->n : 0); r++) {
    if (!xp_cell(bl, r, 0)[0] || !xp_cell(bl, r, 1)[0]) continue;
    sb_add(&b, L"ЗАГОТОВКА");
    for (int c = 0; c < 4; c++) sb_field(&b, xp_cell(bl, r, c));
    sb_add(&b, L"\r\n");
    st->blanks++;
  }
  int skipped = 0;
  for (int r = 0; r < (tp ? op->n : 0); r++) {
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
  const wchar_t *hd, *hn;
  if (mode == PK_SEND_TP && b.w && b.w[0] && pk_head_asm(&hd, &hn)) {
    /* функция «#» пропускает; CursorPad по ней кладёт ТП в папку ТП сборки (szm_head_asm) */
    size_t need = wcslen(b.w) + wcslen(hd) + wcslen(hn) + 16;
    wchar_t *w = (wchar_t *)malloc(need * sizeof(wchar_t));
    if (w) {
      _snwprintf(w, need, L"# СБОРКА\t%s\t%s\r\n%s", hd, hn, b.w);
      w[need - 1] = 0;
      free(b.w);
      b.w = w;
    }
  }
  *out = b.w;
  return st->ops;
}

/* mode — PK_SEND_ALL, только ЭСИ или только ТП (меню кнопки «В Союз…») */
static void pk_to_soyuz(int mode) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  BOOL okc = pk_check(TRUE); /* номера «005», названия операций как в 1С — отсюда коды; виды и размеры — как в «Союзе» */
  if (!okc || g_pkSendWarn) pk_check(FALSE); /* до отправки — на экран: участка нет в справочнике, признак покупки… */
  wchar_t *text = NULL, note[200];
  PkSent st;
  pk_soyuz_text(&text, note, 200, &st, mode);
  if (!text || !text[0]) {
    free(text);
    pk_status(mode == PK_SEND_ESI  ? L"Только ЭСИ — нечего отправлять: нет новых изделий, строк состава и чертежей (строки из «Союза» "
                                     L"там уже есть)"
              : mode == PK_SEND_TP ? L"Только ТП — нечего отправлять: нет операций с кодом справочника и заготовок"
              : g_pkSoyItN ? L"Нечего отправлять: строки из «Союза» там уже есть — дайте позиции операции, заготовку или чертёж"
                           : L"Нечего отправлять: заполните «Изделия», «Операции» или «Заготовки»");
    return;
  }
  wchar_t label[240];
  if (mode == PK_SEND_ESI)
    _snwprintf(label, 240, L"Только ЭСИ: изделий %d, состав %d, чертежей %d%s%s", st.items, st.links, st.draws,
               note[0] ? L" · " : L"", note);
  else if (mode == PK_SEND_TP)
    _snwprintf(label, 240, L"Только ТП (к изделиям в «Союзе»): операций %d, заготовок %d%s%s", st.ops, st.blanks,
               note[0] ? L" · " : L"", note);
  else
    _snwprintf(label, 240, L"В «Союз»: изделий %d, состав %d, заготовок %d, операций %d, чертежей %d%s%s", st.items,
               st.links, st.blanks, st.ops, st.draws, note[0] ? L" · " : L"", note);
  label[239] = 0;
  szm_open_text(text, label);
  free(text);
}

static void pk_clear(void) {
  if (MessageBoxW(g_pkWnd, L"Очистить все листы пакета?", L"ЭСИ и ТП", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  xp_package_free(g_pk);
  pk_soy_clear();
  g_pkCur[0] = 0;
  pk_q_new();
  g_pkQAll = FALSE;
  if (g_pkQPart) SetWindowTextW(g_pkQPart, L"");
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
  int x = rc.right - pad - PKS(100);
  MoveWindow(GetDlgItem(g_pkWnd, ID_PK_ESI), x, y, PKS(100), h, TRUE);
  x -= PKS(6 + 170);
  MoveWindow(g_pkEsiEdit, x, y + PKS(3), PKS(170), h - PKS(6), TRUE);
  int foot = rc.bottom - PKS(TOOL_FOOT_H), by = foot + (PKS(TOOL_FOOT_H) - PKS(30)) / 2;
  int ly = PANEL_TITLE_H + PKS(86);
  /* лист «Операции»: над таблицей — строка быстрого ТП */
  g_pkQShown = g_pkSheet == PK_OPS;
  if (g_pkQPart) {
    /* деталь, «Все детали», операция, участок, станок, «+ Операция» («Сохранить 010»), ▲ ▼ */
    int gap = PKS(6), wPart = PKS(160), wAll = PKS(92), wOp = PKS(210), wArea = PKS(190), wBtn = PKS(132), wArr = PKS(30);
    int wEq = rc.right - pad * 2 - wPart - wAll - wOp - wArea - wBtn - wArr * 2 - gap * 7;
    if (wEq < PKS(100)) wEq = PKS(100);
    HWND q[] = {g_pkQPart, GetDlgItem(g_pkWnd, ID_PK_QALL), g_pkQOp, g_pkQArea, g_pkQEquip, GetDlgItem(g_pkWnd, ID_PK_QADD),
                GetDlgItem(g_pkWnd, ID_PK_QUP), GetDlgItem(g_pkWnd, ID_PK_QDOWN)};
    int qw[] = {wPart, wAll, wOp, wArea, wEq, wBtn, wArr, wArr}, qx = pad;
    for (int i = 0; i < 8; i++) {
      BOOL btn = i == 1 || i >= 5;
      if (!btn) MoveWindow(q[i], qx, ly, qw[i], PKS(320), TRUE);
      else MoveWindow(q[i], qx, ly - PKS(1), qw[i], PKS(27), TRUE);
      ShowWindow(q[i], g_pkQShown ? SW_SHOW : SW_HIDE);
      qx += qw[i] + gap;
    }
    if (g_pkQShown) ly += PKS(34);
  }
  MoveWindow(g_pkList, pad, ly, rc.right - pad * 2, foot - PKS(26) - ly, TRUE);
  /* подвал (с 2026.10.21): правка строк — слева, главное «Сохранить» — справа */
  /* один путь (с 2026.10.29): чертежи → «Проверить» → «В Союз…»; редкое — в меню «Ещё ▾» */
  /* с 30.10: «+ Строка» и «+ Сборка» — видимыми кнопками рядом с «Чертежи…» (просьба пользователя); на узком
     окне — короче («+ Стр.», «+ Сб.»), но не в меню */
  static const int ids[] = {ID_PK_DRAW, ID_PK_ADD, ID_PK_ASM, ID_PK_PASTE, ID_PK_CHECK, ID_PK_MORE};
  static const int wWide[] = {124, 96, 96, 92, 100, 80}; /* «Чертежи…» — шире и цветом главной (cursorpad.c) */
  static const int wNarrow[] = {104, 64, 64, 84, 92, 66};
  int need = 16 + 8 * 5 + 16 + 170 + 16;
  for (int i = 0; i < 6; i++) need += wWide[i];
  BOOL narrow = rc.right < PKS(need);
  const int *w = narrow ? wNarrow : wWide;
  {
    wchar_t cur[40];
    const wchar_t *la = narrow ? L"+ Стр." : L"+ Строка", *lb = narrow ? L"+ Сб." : L"+ Сборка";
    GetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_ADD), cur, 40);
    if (wcscmp(cur, la)) SetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_ADD), la);
    GetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_ASM), cur, 40);
    if (wcscmp(cur, lb)) SetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_ASM), lb);
  }
  x = PKS(16);
  for (int i = 0; i < 6; i++) {
    MoveWindow(GetDlgItem(g_pkWnd, ids[i]), x, by, PKS(w[i]), PKS(30), TRUE);
    ShowWindow(GetDlgItem(g_pkWnd, ids[i]), SW_SHOW);
    x += PKS(w[i] + 8);
  }
  static const int hidden[] = {ID_PK_DEL, ID_PK_CLEAR, ID_PK_HELP, ID_PK_FROMXP, ID_PK_SAVE};
  for (int i = 0; i < 5; i++) ShowWindow(GetDlgItem(g_pkWnd, hidden[i]), SW_HIDE);
  MoveWindow(GetDlgItem(g_pkWnd, ID_PK_SOYUZ), rc.right - PKS(16) - PKS(170), by, PKS(170), PKS(30), TRUE);
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
                          : L"Чертежи — мышью в окно или «Чертежи…» (штамп и операции прочитает ИИ), затем «Проверить» и "
                            L"«В Союз…». Строки, пакет .xlsx, приоритеты операций — в «Ещё ▾».",
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
      int r = pk_row(ListView_GetNextItem(hwnd, -1, LVNI_FOCUSED));
      if (r >= 0) pk_edit_begin(r, 0);
      return 0;
    }
    if (ctrl && (wParam == VK_UP || wParam == VK_DOWN) && g_pkSheet == PK_OPS) { /* как ▲ ▼ */
      pk_q_move(wParam == VK_UP ? -1 : 1);
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
        int r = pk_row((int)cd->nmcd.dwItemSpec);
        if (r >= 0 && r < g_pk[g_pkSheet].n && pk_row_from_soyuz(g_pkSheet, r)) cd->clrText = COL_MUTED;
      }
      return CDRF_DODEFAULT;
    }
    if (nh->hwndFrom == g_pkList && nh->code == NM_DBLCLK) {
      NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lParam;
      int r = pk_row(ia->iItem);
      if (r >= 0 && g_pkSheet == PK_ITEMS && ia->iSubItem == 5) pk_pick_drawing(r);
      else if (r >= 0) pk_edit_begin(r, ia->iSubItem);
      else pk_add_row(); /* по пустому месту — новая строка */
      return 0;
    }
    /* одна строка «Операций» выделена — она в строке быстрого ТП, для правки; выделение снято — снова новая */
    if (nh->hwndFrom == g_pkList && nh->code == LVN_ITEMCHANGED && g_pkSheet == PK_OPS && g_pkQPart && !g_pkFilling) {
      NMLISTVIEW *lv = (NMLISTVIEW *)lParam;
      if ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED) {
        int r = ListView_GetSelectedCount(g_pkList) == 1 ? pk_sel_row() : -1;
        if (r >= 0 && r < g_pk[PK_OPS].n) {
          if (r != g_pkQEdit) pk_q_load(r);
        } else if (g_pkQEdit >= 0) {
          pk_q_new();
        }
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
          if (g_pkQPart) { /* и в строку быстрого ТП: на «Операциях» — её операции */
            g_pkQBusy = TRUE;
            SetWindowTextW(g_pkQPart, d);
            g_pkQBusy = FALSE;
            g_pkQAll = FALSE;
          }
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
      free(g_szmDicts.afit);
      g_szmDicts = *d;
      free(d);
      g_pkComboCol = -1; /* список заполнить заново */
      if (g_szmDicts.err[0]) pk_status(L"Справочники из базы не прочитаны: %s — участки и станки вписывайте руками", g_szmDicts.err);
      else pk_status(L"Из базы «Союза»: участков %d, станков %d, признаков покупки %d — выбор списком в ячейках",
                     g_szmDicts.na, g_szmDicts.ne, g_szmDicts.nb);
      pk_buy_fill(TRUE); /* «Собственного изготовления» — как он записан в базе */
      int eqn = pk_equip_fill();
      pk_ops_settle(); /* участки теперь известны — транспортирования и номера */
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
      if (g_pkQEdit >= 0) pk_q_new();
      g_pkSheet = id - ID_PK_TAB;
      if (g_pkSheet == PK_OPS && g_pkQPart) { /* выбранная позиция — деталь ТП, её операции */
        wchar_t t[200];
        GetWindowTextW(g_pkQPart, t, 200);
        if (!t[0] && g_pkCur[0]) SetWindowTextW(g_pkQPart, g_pkCur);
      }
      pk_fill(TRUE);
    }
    if (id == ID_PK_ADD) pk_add_row();
    if (id == ID_PK_ASM) pk_add_asm();
    if (id == ID_PK_QADD) pk_q_add();
    if (id == ID_PK_QALL) pk_q_all();
    if (id == ID_PK_QUP || id == ID_PK_QDOWN) pk_q_move(id == ID_PK_QUP ? -1 : 1);
    if (id == ID_PK_QPART && (HIWORD(wParam) == CBN_EDITCHANGE || HIWORD(wParam) == CBN_SELCHANGE))
      pk_q_part_changed(HIWORD(wParam) == CBN_SELCHANGE);
    if (id == ID_PK_QOP && HIWORD(wParam) == CBN_EDITCHANGE) pk_q_filter();
    if ((id == ID_PK_QAREA || id == ID_PK_QEQUIP) && HIWORD(wParam) == CBN_EDITCHANGE && !g_pkQBusy)
      pk_combo_filter((HWND)lParam, id == ID_PK_QAREA ? 3 : 4, TRUE); /* участок и станок — по набору */
    if ((id == ID_PK_QPART || id == ID_PK_QAREA || id == ID_PK_QEQUIP) && HIWORD(wParam) == CBN_DROPDOWN)
      pk_q_list((HWND)lParam);
    if (id == ID_PK_DEL) pk_delete_selected();
    if (id == ID_PK_PASTE) pk_paste();
    if (id == ID_PK_FROMXP) pk_from_export();
    if (id == ID_PK_CHECK) pk_check(FALSE);
    if (id == ID_PK_SAVE) pk_save();
    if (id == ID_PK_CLEAR) pk_clear();
    if (id == ID_PK_SOYUZ) { /* что отправить — меню у кнопки (с 2026.10.30: ЭСИ и ТП можно по отдельности) */
      HMENU m = CreatePopupMenu();
      AppendMenuW(m, MF_STRING, ID_PK_SOYALL, L"ЭСИ и ТП — всё сразу");
      AppendMenuW(m, MF_SEPARATOR, 0, NULL);
      AppendMenuW(m, MF_STRING, ID_PK_SOYESI, L"Только ЭСИ — изделия, состав, чертежи");
      AppendMenuW(m, MF_STRING, ID_PK_SOYTP, L"Только ТП — операции и заготовки к изделиям, уже заведённым в «Союзе»");
      SetMenuDefaultItem(m, ID_PK_SOYALL, FALSE);
      RECT br;
      GetWindowRect(GetDlgItem(hwnd, ID_PK_SOYUZ), &br);
      int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_NONOTIFY, br.right, br.top, 0,
                               hwnd, NULL);
      DestroyMenu(m);
      if (cmd) PostMessageW(hwnd, WM_COMMAND, (WPARAM)cmd, 0);
      return 0;
    }
    if (id == ID_PK_SOYALL || id == ID_PK_SOYESI || id == ID_PK_SOYTP)
      pk_to_soyuz(id == ID_PK_SOYESI ? PK_SEND_ESI : id == ID_PK_SOYTP ? PK_SEND_TP : PK_SEND_ALL);
    if (id == ID_PK_HELP) pk_help();
    if (id == ID_PK_PRIO) szp_edit(hwnd);
    if (id == ID_PK_MORE) { /* редкое — меню у кнопки; выбранное уходит тем же WM_COMMAND */
      HMENU m = CreatePopupMenu();
      AppendMenuW(m, MF_STRING, ID_PK_DEL, L"Удалить выделенные\tDelete"); /* «+ Строка», «+ Сборка» — кнопками (30.10) */
      AppendMenuW(m, MF_SEPARATOR, 0, NULL);
      AppendMenuW(m, MF_STRING, ID_PK_DRAWFIND, L"Чертежи из архива по обозначениям — к изделиям без файла");
      AppendMenuW(m, MF_STRING | (pk_stamp_ops_on() ? MF_CHECKED : 0), ID_PK_DRAWTOGGLE,
                  L"Чертёж детали — сразу и операции (мини-ИИ читает лист, маршрут и нормы — правила технолога)");
      AppendMenuW(m, MF_STRING, ID_PK_DRAWOPS, L"Операции по чертежам — деталям с файлом и без операций (выделены — только им)");
      AppendMenuW(m, MF_SEPARATOR, 0, NULL);
      AppendMenuW(m, MF_STRING, ID_PK_FROMXP, L"Из выгрузки из PLM…");
      AppendMenuW(m, MF_STRING, ID_PK_SAVE, L"Сохранить пакет .xlsx для «Загрузить ЭСИ из CursorPad»…");
      AppendMenuW(m, MF_SEPARATOR, 0, NULL);
      AppendMenuW(m, MF_STRING, ID_PK_PRIO, L"Приоритеты операций: участок и станок…");
      AppendMenuW(m, MF_STRING, ID_PK_HELP, L"Что нужно «Союзу»?");
      AppendMenuW(m, MF_SEPARATOR, 0, NULL);
      AppendMenuW(m, MF_STRING, ID_PK_CLEAR, L"Очистить все листы…");
      RECT br;
      GetWindowRect(GetDlgItem(hwnd, ID_PK_MORE), &br);
      int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_NONOTIFY, br.left, br.top, 0,
                               hwnd, NULL);
      DestroyMenu(m);
      if (cmd) PostMessageW(hwnd, WM_COMMAND, (WPARAM)cmd, 0);
      return 0;
    }
    if (id == ID_PK_ESI) pk_esi_start();
    if (id == ID_PK_DRAW) pk_draw_pick();
    if (id == ID_PK_DRAWFIND) pk_find_drawings();
    if (id == ID_PK_DRAWTOGGLE) pks_ops_toggle();
    if (id == ID_PK_DRAWOPS) pk_stamp_ops();
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
/* окно — без показа: действия из чата (ai_esi.c) кладут строки в скрытое */
static void pk_create(void) {
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
    mk_btn(g_pkWnd, L"Ещё ▾", ID_PK_MORE);
    g_pkQPart = pk_q_combo(ID_PK_QPART, L"деталь (обозначение)");
    g_pkQOp = pk_q_combo(ID_PK_QOP, L"операция: печатайте «ток», «4110»… Enter");
    g_pkQArea = pk_q_combo(ID_PK_QAREA, L"участок");
    g_pkQEquip = pk_q_combo(ID_PK_QEQUIP, L"оборудование — само по операции и участку");
    mk_btn(g_pkWnd, L"+ Операция", ID_PK_QADD);
    mk_btn(g_pkWnd, L"Все детали", ID_PK_QALL);
    mk_btn(g_pkWnd, L"▲", ID_PK_QUP);
    mk_btn(g_pkWnd, L"▼", ID_PK_QDOWN);
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
}

static void package_show(BOOL fill) {
  pk_create();
  if (!g_pkWnd) return;
  ShowWindow(g_pkWnd, IsIconic(g_pkWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_pkWnd);
  SetFocus(g_pkList);
  if (!g_szmDictsAsked && g_sqlUser[0] && g_sqlPass[0]) { /* участки и станки — один раз за работу */
    g_szmDictsAsked = TRUE;
    szm_dicts_start(g_pkWnd);
  }
  if (fill) pk_from_export();
}
