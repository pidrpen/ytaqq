/* ---- Выгрузка из PLM: общий каркас -------------------------------------------

   «Ещё» → «Выгрузка из PLM»: всё, что связано с изделием, — одной книгой Excel,
   по листу на тему:
     Состав (ЭСИ)   — техсостав вглубь: позиция, количество, заготовка, ТП, маршрут;
     Операции       — операции основного ТП каждой позиции: участок, оборудование,
                      профессия, Тпз и Тшт, название для 1С;
     Материалы      — материалы операций (SummaryList) с нормой;
     Инструмент     — инструмент и оснастка операций (SummaryToolList);
     Извещения      — извещения об изменении, которые касаются позиции: КД — через
                      версию изделия, ТП — через версии её техпроцесса.

   Как устроено (это и есть каркас):
     1. Обход ЭСИ — один, тот же, что у маршрутной ведомости (rt_walk): он
        проверен на базе. У каждой позиции он оставляет её связи в PLM — ТП,
        версию, вариант с операциями, версию изделия, исполнение (RtRow.tp…).
     2. Лист — функция над списком позиций, которая заполняет свою таблицу
        (XpTable); столбцы — массив заголовков. Поля объектов PLM читает общий
        xp_fields: свои поля, строки списков и составные внутри строк — одним
        запросом, путь поля через «|» («EquipmentList|Equipment|Equipment»).
     3. Книга — xl_save_book: лист выгрузки — ярлык Excel.
   Новый лист: заголовки, функция сбора, строка в kXpSheets — и всё.

   Только чтение. Имена полей — из конфигураций PLM завода (pidrpen/cursor:
   «Базовая конфигурация для выгрузок», «Извещения об изменении», «…ТП»). */

#define WM_XP_PROGRESS (WM_APP + 64)
#define WM_XP_DONE (WM_APP + 65) /* lParam — XpJob*, освобождает получатель */

/* XP_PF — в конце: номера листов ходят и через раздающего коллегу */
enum { XP_COMP, XP_OPS, XP_MAT, XP_TOOL, XP_ECN, XP_PF, XP_NT };

typedef struct {
  int ncols, n, cap;
  wchar_t **cell; /* n × ncols; каждая ячейка — своя строка в памяти */
} XpTable;

typedef struct {
  RtJob *rt;     /* обход ЭСИ: позиции, журнал «Подробности», срок, отмена */
  unsigned what; /* какие листы собирать: 1 << XP_… */
  XpTable t[XP_NT];
  HWND notify;
  int stage; /* что сейчас собирается — для строки состояния */
  double secWalk, secSheets, secEcn; /* сколько шли этапы — в строку состояния */
} XpJob;

/* ---- листы: столбцы ---------------------------------------------------------- */

static const wchar_t *const kXpCompHead[] = {
    L"Уровень",       L"Обозначение",         L"Наименование", L"Входит в",       L"Вид изделия",
    L"Кол-во",        L"Кол-во на изд.",      L"Материал",     L"Сортамент заготовки", L"Припуск",
    L"Масса, кг",     L"Норма на 1 дет.",     L"Норма на изд.", L"Ед. нормы",     L"Техпроцесс",
    L"Маршрут",       L"Примечание"};
static const int kXpCompW[] = {7, 24, 30, 22, 14, 8, 9, 34, 20, 8, 9, 9, 9, 7, 30, 18, 30};

static const wchar_t *const kXpOpsHead[] = {
    L"Обозначение", L"Наименование", L"Кол-во на изд. (всего)", L"Техпроцесс", L"№ опер.",
    L"Операция",    L"Операция для 1С", L"Участок",            L"Цех",        L"Оборудование",
    L"Профессия",   L"Тпз",          L"Тшт",                   L"Ед. времени", L"Тшт × кол-во"};
static const int kXpOpsW[] = {24, 28, 10, 28, 7, 26, 28, 18, 14, 30, 24, 8, 8, 8, 10};

static const wchar_t *const kXpMatHead[] = {L"Обозначение", L"Наименование", L"Кол-во на изд. (всего)",
                                            L"№ опер.",     L"Операция",     L"Материал",
                                            L"Норма",       L"Ед.",          L"Норма × кол-во"};
static const int kXpMatW[] = {24, 28, 10, 7, 26, 44, 10, 8, 12};

static const wchar_t *const kXpToolHead[] = {L"Обозначение", L"Наименование", L"№ опер.", L"Операция",
                                             L"Инструмент, оснастка", L"Кол-во", L"Коэф. применения"};
static const int kXpToolW[] = {24, 28, 7, 26, 50, 9, 11};

static const wchar_t *const kXpPfHead[] = {L"Обозначение", L"Наименование", L"Кол-во на изд. (всего)",
                                           L"Заготовка",   L"Материал",     L"Сортамент",
                                           L"Размеры",     L"Припуск",      L"Норма на 1",
                                           L"Ед. нормы",   L"Норма на изд.", L"Из изделия",
                                           L"Для версии"};
static const int kXpPfW[] = {24, 28, 10, 28, 40, 24, 18, 8, 9, 7, 10, 26, 26};

static const wchar_t *const kXpEcnHead[] = {L"Обозначение", L"Наименование", L"Вид", L"Номер извещения",
                                            L"Извещение",   L"Состояние",    L"Что делает", L"С чем связано"};
static const int kXpEcnW[] = {24, 28, 6, 20, 40, 16, 26, 40};

typedef struct {
  const wchar_t *name;
  int ncols;
  const wchar_t *const *head;
  const int *width;
} XpSheetDef;

#define XP_N(a) ((int)(sizeof(a) / sizeof(a[0])))
static const XpSheetDef kXpSheets[XP_NT] = {
    {L"Состав (ЭСИ)", XP_N(kXpCompHead), kXpCompHead, kXpCompW},
    {L"Операции", XP_N(kXpOpsHead), kXpOpsHead, kXpOpsW},
    {L"Материалы", XP_N(kXpMatHead), kXpMatHead, kXpMatW},
    {L"Инструмент", XP_N(kXpToolHead), kXpToolHead, kXpToolW},
    {L"Извещения", XP_N(kXpEcnHead), kXpEcnHead, kXpEcnW},
    {L"Заготовки", XP_N(kXpPfHead), kXpPfHead, kXpPfW},
};
/* порядок листов в книге и в окне: заготовки — сразу за составом */
static const int kXpOrder[XP_NT] = {XP_COMP, XP_PF, XP_OPS, XP_MAT, XP_TOOL, XP_ECN};

/* ---- таблица ------------------------------------------------------------------ */

static wchar_t **xp_row(XpTable *t) {
  if (t->n >= t->cap) {
    int nc = t->cap ? t->cap * 2 : 256;
    wchar_t **p = (wchar_t **)realloc(t->cell, sizeof(wchar_t *) * (size_t)nc * (size_t)t->ncols);
    if (!p) return NULL;
    t->cell = p;
    t->cap = nc;
  }
  wchar_t **r = t->cell + (size_t)t->n * (size_t)t->ncols;
  for (int c = 0; c < t->ncols; c++) r[c] = NULL;
  t->n++;
  return r;
}

static void xp_set(wchar_t **r, int c, const wchar_t *v) {
  if (!r || !v || !v[0]) return;
  free(r[c]);
  r[c] = _wcsdup(v);
}

static void xp_setf(wchar_t **r, int c, const wchar_t *fmt, ...) {
  wchar_t b[600];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(b, 600, fmt, ap);
  va_end(ap);
  b[599] = 0;
  xp_set(r, c, b);
}

static const wchar_t *xp_cell(void *ctx, int r, int c) {
  XpTable *t = (XpTable *)ctx;
  const wchar_t *v = t->cell[(size_t)r * (size_t)t->ncols + (size_t)c];
  return v ? v : L"";
}

static void xp_job_free(XpJob *x) {
  if (!x) return;
  for (int k = 0; k < XP_NT; k++) {
    XpTable *t = &x->t[k];
    for (size_t i = 0; i < (size_t)t->n * (size_t)t->ncols; i++) free(t->cell[i]);
    free(t->cell);
  }
  rt_job_free(x->rt);
  free(x);
}

static XpJob *xp_job_new(void) {
  XpJob *x = (XpJob *)calloc(1, sizeof(XpJob));
  if (!x) return NULL;
  x->rt = rt_job_new();
  if (!x->rt) {
    free(x);
    return NULL;
  }
  for (int k = 0; k < XP_NT; k++) x->t[k].ncols = kXpSheets[k].ncols;
  return x;
}

/* ---- общее чтение полей -------------------------------------------------------- */

/* все разные значения пути у владельца — через «; » (оборудование, профессии) */
static void xp_join(const CardRow *f, int nf, long a, const wchar_t *path, wchar_t *out, int cap) {
  out[0] = 0;
  int len = 0;
  for (int i = 0; i < nf; i++) {
    if (f[i].n1 != a || f[i].n3 >= 1000 || !f[i].s2[0] || _wcsicmp(f[i].s1, path)) continue;
    if (wcsstr(out, f[i].s2)) continue;
    int w = _snwprintf(out + len, cap - len, L"%s%s", len ? L"; " : L"", f[i].s2);
    if (w < 0 || len + w >= cap - 1) break;
    len += w;
  }
  out[cap - 1] = 0;
}

/* число из поля; FALSE — пусто или не число */
static BOOL xp_num(const wchar_t *s, double *v) { return s && s[0] && rt_num(s, v); }

static BOOL xp_late(XpJob *x) { return rt_late(x->rt); }

/* этап и сколько на нём всего — для полосы хода; сделанное — rt->progDone */
static void xp_prog(XpJob *x, int stage, int total) {
  InterlockedExchange(&x->rt->progTotal, 0);
  InterlockedExchange(&x->rt->progDone, 0);
  x->stage = stage;
  InterlockedExchange(&x->rt->progTotal, total);
}

static void xp_stage(XpJob *x, int stage, int done) {
  x->stage = stage;
  if (x->notify) PostMessageW(x->notify, WM_XP_PROGRESS, (WPARAM)done, (LPARAM)stage);
}

/* ---- позиции: уникальные объекты ЭСИ ------------------------------------------ */

/* Позиция встречается в составе несколько раз (болт в трёх подсборках) — для
   листов операций, материалов, извещений она одна, а количество на изделие —
   сумма по всем вхождениям (так считает «Маршрутная ведомость» в PLM). */
typedef struct {
  const RtRow *r;
  double qtyAll;
} XpPos;

static int xp_positions(XpJob *x, XpPos *out, int max) {
  int n = 0;
  RtJob *j = x->rt;
  for (int i = 0; i < j->n; i++) {
    const RtRow *r = &j->rows[i];
    int k = 0;
    while (k < n && out[k].r->id != r->id) k++;
    if (k < n) {
      out[k].qtyAll += r->qtyTot;
      continue;
    }
    if (n >= max) break;
    out[n].r = r;
    out[n].qtyAll = r->qtyTot;
    n++;
  }
  return n;
}

/* ---- лист «Состав (ЭСИ)» ------------------------------------------------------- */

static void xp_comp(XpJob *x) {
  XpTable *t = &x->t[XP_COMP];
  RtJob *j = x->rt;
  static const int map[] = {RC_DES, RC_NAME, RC_PARENT, RC_KIND, RC_QTY, RC_QTYTOT, RC_MAT, RC_SORT, RC_ALLOW,
                            RC_MASS, RC_NORM1, RC_NORMTOT, RC_UNIT};
  for (int i = 0; i < j->n; i++) {
    const RtRow *r = &j->rows[i];
    wchar_t **w = xp_row(t);
    if (!w) return;
    xp_setf(w, 0, L"%d", r->level);
    for (int c = 0; c < XP_N(map); c++) xp_set(w, c + 1, r->f[map[c]]);
    xp_set(w, 14, r->tpName);
    xp_set(w, 15, r->f[RC_ROUTE]);
    xp_set(w, 16, r->f[RC_NOTE]);
  }
}

/* ---- листы по позициям: операции, материалы, инструмент, заготовки ------------

   Каждая позиция — свои запросы, а позиций сотни: потому (с 2026.09.23.81) их
   разбирают XP_WORKERS потоков, у каждого своё подключение к PLM; строки каждой
   позиции — в её таблицах, потом сшиваются в порядке состава. */

#define XP_OPS_MAX 200
#define XP_FIELDS 3000
#define XP_WORKERS 4

typedef struct {
  long id, ts; /* операция и её TSOperation */
  wchar_t num[24], name[260], tsName[260];
} XpOp;

/* операции варианта ТП по порядку (номер — строка «005», OP_NUM_APPLY) */
static int xp_ops_of(SQLHDBC dbc, long var, CardRow *rows, XpOp *ops, int max, RtJob *j) {
  wchar_t sql[3000], err[280];
  _snwprintf(sql, 3000,
             L"SELECT TOP 200 ch.InfoObjectId, ch.Name, ISNULL(op.NM,N''), ISNULL(num.N,0), ISNULL(op.OID,0) "
             L"FROM InfoObjects AS ch WITH(NOLOCK) "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=ch.TemplateId "
             L"OUTER APPLY (SELECT TOP 1 o2.Name AS NM, o2.InfoObjectId AS OID "
             L"FROM InfoObjectAttributes AS ts WITH(NOLOCK) "
             L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=ts.NameKeyId AND nkt.Value=N'TSOperation' "
             L"JOIN InfoObjects AS o2 WITH(NOLOCK) ON o2.InfoObjectId=ts.Link "
             L"WHERE ts.OwnerId=ch.InfoObjectId AND ts.Outdated=0) AS op "
             OP_NUM_APPLY(L"ch.InfoObjectId")
             L"WHERE ch.ParentId=%ld AND ch.Erased=0 "
             L"ORDER BY " OP_NUM_ORDER L", ch.InfoObjectId",
             var);
  sql[2999] = 0;
  int n = card_query(dbc, sql, rows, max, err, 280);
  if (n < 0) {
    rt_log(j, L"  операции варианта %ld: запрос не выполнился — %s\r\n", var, err);
    return 0;
  }
  for (int i = 0; i < n; i++) {
    ops[i].id = rows[i].n1;
    ops[i].ts = rows[i].n3;
    lstrcpynW(ops[i].name, rows[i].s1, 260);
    lstrcpynW(ops[i].tsName, rows[i].s2, 260);
    if (rows[i].n2) _snwprintf(ops[i].num, 24, L"%03ld", rows[i].n2);
    else ops[i].num[0] = 0;
  }
  return n;
}

/* Поля операции, по завод. выгрузкам («Выгрузка всех данных!», «Базовая
   конфигурация для выгрузок»): Number, Area, WorkShop, TimePerPiece и
   SetupTime (составные: Value, ComputingUnit), EquipmentList → Equipment →
   Equipment, CraftList → Craft → Craft, SummaryList (Material,
   NormOfExpensesWithUnit: Value, ComputingUnit), SummaryToolList (Tooling,
   QuantityC: Value, zum_ToolApplicationRate). */
#define XP_OP_OWN L"N'Number',N'Area',N'WorkShop'"
#define XP_OP_COLL                                                                                      \
  L"N'TimePerPiece',N'SetupTime',N'EquipmentList',N'CraftList',N'SummaryList',N'SummaryToolList'"

/* строки списка coll у владельца: номера строк по порядку */
static int xp_coll_rows(const CardRow *f, int nf, long own, const wchar_t *coll, long *out, int max) {
  int n = 0;
  size_t cl = wcslen(coll);
  for (int i = 0; i < nf && n < max; i++) {
    if (f[i].n1 != own || !f[i].n2 || _wcsnicmp(f[i].s1, coll, cl) || f[i].s1[cl] != L'|') continue;
    int k = 0;
    while (k < n && out[k] != f[i].n2) k++;
    if (k == n) out[n++] = f[i].n2;
  }
  return n;
}

static void xp_fmt3(double v, wchar_t *out, int cap) { rt_fmt(floor(v * 1000 + 0.5) / 1000, 3, out, cap); }

/* буферы одного потока */
typedef struct {
  CardRow *f, *dr, *rows;
  XpOp *ops;
  RtJob *lg; /* его журнал — в «Подробности» после сбора */
  BOOL keysShown;
} XpBuf;

static BOOL xp_buf_new(XpBuf *b) {
  memset(b, 0, sizeof(*b));
  b->f = (CardRow *)malloc(sizeof(CardRow) * XP_FIELDS);
  b->dr = (CardRow *)malloc(sizeof(CardRow) * 300);
  b->rows = (CardRow *)malloc(sizeof(CardRow) * 400);
  b->ops = (XpOp *)malloc(sizeof(XpOp) * XP_OPS_MAX);
  b->lg = (RtJob *)calloc(1, sizeof(RtJob));
  if (b->lg) b->lg->log = (wchar_t *)calloc(RT_LOG, sizeof(wchar_t));
  return b->f && b->dr && b->rows && b->ops && b->lg && b->lg->log;
}

static void xp_buf_free(XpBuf *b) {
  free(b->f);
  free(b->dr);
  free(b->rows);
  free(b->ops);
  if (b->lg) free(b->lg->log);
  free(b->lg);
}

/* операции, материалы и инструмент одной позиции — в out[XP_OPS…XP_TOOL] */
static void xp_pos_ops(SQLHDBC dbc, XpJob *x, const XpPos *pp, XpTable *out, XpBuf *b) {
  const RtRow *r = pp->r;
  BOOL wantOps = (x->what >> XP_OPS) & 1, wantMat = (x->what >> XP_MAT) & 1, wantTool = (x->what >> XP_TOOL) & 1;
  if ((!wantOps && !wantMat && !wantTool) || !r->tpVar) return;
  RtJob *j = b->lg;
  CardRow *f = b->f, *dr = b->dr;
  XpOp *ops = b->ops;
  int no = xp_ops_of(dbc, r->tpVar, b->rows, ops, XP_OPS_MAX, j);
  if (!no) return;
  wchar_t qty[40];
  xp_fmt3(pp->qtyAll, qty, 40);
  /* по 25 операций за запрос: меньше запросов — быстрее */
  for (int b0 = 0; b0 < no && !xp_late(x); b0 += 25) {
    int b1 = b0 + 25 < no ? b0 + 25 : no;
    wchar_t ids[1300], opIds[700];
    int q = 0, w = 0;
    for (int i = b0; i < b1; i++) {
      q += _snwprintf(ids + q, 1300 - q, q ? L",%ld" : L"%ld", ops[i].id);
      if (ops[i].ts) q += _snwprintf(ids + q, 1300 - q, L",%ld", ops[i].ts);
      w += _snwprintf(opIds + w, 700 - w, w ? L",%ld" : L"%ld", ops[i].id);
    }
    wchar_t err[280];
    int nf = xp_fields(dbc, ids, XP_OP_OWN, XP_OP_COLL, f, XP_FIELDS, err);
    if (nf < 0) {
      rt_log(j, L"  поля операций %s: запрос не выполнился — %s\r\n", r->f[RC_DES], err);
      nf = 0;
    }
    if (nf >= XP_FIELDS)
      rt_log(j, L"  поля операций %s: строк больше %d — часть не прочитана\r\n", r->f[RC_DES], XP_FIELDS);
    if (!b->keysShown && nf > 0) { /* какие поля пришли — раз, для проверки */
      b->keysShown = TRUE;
      rt_log(j, L"  поля операции %s (для проверки):\r\n", ops[b0].name);
      for (int i = 0; i < nf && i < 80; i++)
        if (f[i].n1 == ops[b0].id || f[i].n1 == ops[b0].ts)
          rt_log(j, L"    %s = %s%s\r\n", f[i].s1, f[i].s2, f[i].n3 >= 1000 ? L" (устар.)" : L"");
    }
    int nd = wantOps ? card_op_dirs(dbc, opIds, dr, err) : 0;
    for (int i = b0; i < b1; i++) {
      const XpOp *o = &ops[i];
      const wchar_t *num = xp_get(f, nf, o->id, 0, 0, L"Number");
      const wchar_t *opNum = num && num[0] ? num : o->num;
      if (wantOps) {
        wchar_t **row = xp_row(&out[XP_OPS]);
        if (!row) break;
        xp_set(row, 0, r->f[RC_DES]);
        xp_set(row, 1, r->f[RC_NAME]);
        xp_set(row, 2, qty);
        xp_set(row, 3, r->tpName);
        xp_set(row, 4, opNum);
        xp_set(row, 5, o->name);
        const CardRow *d = NULL;
        for (int k = 0; k < nd && !d; k++)
          if (dr[k].n1 == o->id) d = &dr[k];
        wchar_t t1c[PLM_COL1];
        op1c_text(d ? d->s1 : NULL, d ? d->s2 : NULL, o->ts ? o->tsName : NULL, o->name, t1c, PLM_COL1);
        xp_set(row, 6, t1c);
        xp_set(row, 7, xp_get(f, nf, o->id, o->ts, 0, L"Area"));
        xp_set(row, 8, xp_get(f, nf, o->id, o->ts, 0, L"WorkShop"));
        wchar_t eq[600], cr[600];
        xp_join(f, nf, o->id, L"EquipmentList|Equipment|Equipment", eq, 600);
        if (!eq[0]) xp_join(f, nf, o->id, L"EquipmentList|Equipment", eq, 600);
        xp_join(f, nf, o->id, L"CraftList|Craft|Craft", cr, 600);
        if (!cr[0]) xp_join(f, nf, o->id, L"CraftList|Craft", cr, 600);
        xp_set(row, 9, eq);
        xp_set(row, 10, cr);
        const wchar_t *tpz = xp_get(f, nf, o->id, o->ts, 0, L"SetupTime|Value");
        const wchar_t *tsh = xp_get(f, nf, o->id, o->ts, 0, L"TimePerPiece|Value");
        const wchar_t *unit = xp_get(f, nf, o->id, o->ts, 0, L"TimePerPiece|ComputingUnit;SetupTime|ComputingUnit");
        double v;
        wchar_t nb[40];
        if (xp_num(tpz, &v) && v != 0) xp_fmt3(v, nb, 40), xp_set(row, 11, nb);
        if (xp_num(tsh, &v) && v != 0) {
          xp_fmt3(v, nb, 40);
          xp_set(row, 12, nb);
          xp_fmt3(v * pp->qtyAll, nb, 40);
          xp_set(row, 14, nb);
        }
        xp_set(row, 13, unit);
      }
      if (wantMat) {
        long rr[60];
        int nr = xp_coll_rows(f, nf, o->id, L"SummaryList", rr, 60);
        for (int k = 0; k < nr; k++) {
          const wchar_t *mat = xp_get(f, nf, o->id, 0, rr[k], L"SummaryList|Material");
          if (!mat) continue;
          wchar_t **row = xp_row(&out[XP_MAT]);
          if (!row) break;
          xp_set(row, 0, r->f[RC_DES]);
          xp_set(row, 1, r->f[RC_NAME]);
          xp_set(row, 2, qty);
          xp_set(row, 3, opNum);
          xp_set(row, 4, o->name);
          xp_set(row, 5, mat);
          const wchar_t *nv = xp_get(f, nf, o->id, 0, rr[k], L"SummaryList|NormOfExpensesWithUnit|Value");
          double v;
          wchar_t nb[40];
          if (xp_num(nv, &v)) {
            xp_fmt3(v, nb, 40);
            xp_set(row, 6, nb);
            xp_fmt3(v * pp->qtyAll, nb, 40);
            xp_set(row, 8, nb);
          }
          xp_set(row, 7, xp_get(f, nf, o->id, 0, rr[k], L"SummaryList|NormOfExpensesWithUnit|ComputingUnit"));
        }
      }
      if (wantTool) {
        long rr[80];
        int nr = xp_coll_rows(f, nf, o->id, L"SummaryToolList", rr, 80);
        for (int k = 0; k < nr; k++) {
          const wchar_t *tl = xp_get(f, nf, o->id, 0, rr[k], L"SummaryToolList|Tooling");
          if (!tl) continue;
          wchar_t **row = xp_row(&out[XP_TOOL]);
          if (!row) break;
          xp_set(row, 0, r->f[RC_DES]);
          xp_set(row, 1, r->f[RC_NAME]);
          xp_set(row, 2, opNum);
          xp_set(row, 3, o->name);
          xp_set(row, 4, tl);
          xp_set(row, 5, xp_get(f, nf, o->id, 0, rr[k], L"SummaryToolList|QuantityC|Value"));
          xp_set(row, 6, xp_get(f, nf, o->id, 0, rr[k], L"SummaryToolList|zum_ToolApplicationRate"));
        }
      }
    }
  }
}

/* Заготовки позиции — все из её карточки заготовок (ProductPreformsCard), по
   строке на заготовку: материал, сортамент, размеры, припуск, норма. В
   карточке бывают заготовки разных версий изделия — берутся этой версии
   (ProductVersionConfiguration), нет таких — все. */
static void xp_pos_pf(SQLHDBC dbc, XpJob *x, const XpPos *pp, XpTable *out, XpBuf *b) {
  const RtRow *r = pp->r;
  if (!((x->what >> XP_PF) & 1) || !r->pfCard) return;
  wchar_t sql[4000], err[280];
  _snwprintf(sql, 4000,
             L"SELECT TOP 30 pf.PfId, pf.PfName, ISNULL(CAST(po.Name AS NVARCHAR(250)),N''), ISNULL(pvc.L,0), 0 "
             L"FROM (SELECT %ld AS CardId) AS k " PF_APPLY(L"k.CardId")
             L"OUTER APPLY (SELECT TOP 1 a.Link AS L FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'ProductVersionConfiguration' "
             L"WHERE a.OwnerId=pf.PfId AND a.Outdated=0) AS pvc "
             L"LEFT JOIN InfoObjects AS po WITH(NOLOCK) ON po.InfoObjectId=pvc.L "
             L"ORDER BY pf.PfId",
             r->pfCard);
  sql[3999] = 0;
  int n = card_query(dbc, sql, b->rows, 30, err, 280);
  if (n < 0) {
    rt_log(b->lg, L"  заготовки %s: запрос не выполнился — %s\r\n", r->f[RC_DES], err);
    return;
  }
  int mine = 0;
  for (int i = 0; i < n; i++) mine += b->rows[i].n2 == r->id;
  CardRow pfs[30];
  int np = 0;
  for (int i = 0; i < n && np < 30; i++)
    if (!mine || b->rows[i].n2 == r->id) pfs[np++] = b->rows[i];
  wchar_t qty[40];
  xp_fmt3(pp->qtyAll, qty, 40);
  for (int i = 0; i < np && !xp_late(x); i++) {
    RtPf pf;
    rt_pf_read(dbc, pfs[i].n1, &pf, b->lg);
    wchar_t **row = xp_row(&out[XP_PF]);
    if (!row) return;
    xp_set(row, 0, r->f[RC_DES]);
    xp_set(row, 1, r->f[RC_NAME]);
    xp_set(row, 2, qty);
    xp_set(row, 3, pfs[i].s1);
    xp_set(row, 4, pf.mat);
    xp_set(row, 5, pf.sort);
    xp_set(row, 6, pf.dims);
    xp_set(row, 7, pf.add);
    if (pf.hasNorm) {
      wchar_t nb[40];
      xp_fmt3(pf.normV, nb, 40);
      xp_set(row, 8, nb);
      xp_set(row, 9, pf.unit[0] ? pf.unit : L"кг");
      xp_fmt3(pf.normV * pp->qtyAll, nb, 40);
      xp_set(row, 10, nb);
    }
    xp_set(row, 11, pf.from);
    xp_set(row, 12, pfs[i].s2);
  }
}

typedef struct {
  XpJob *x;
  XpPos *pos;
  int np;
  XpTable (*out)[XP_NT];
  char *done;
  volatile LONG *next, *count;
  XpBuf buf;
  BOOL ok;
} XpWork;

static void xp_pos_one(SQLHDBC dbc, XpWork *k, int i) {
  xp_pos_ops(dbc, k->x, &k->pos[i], k->out[i], &k->buf);
  xp_pos_pf(dbc, k->x, &k->pos[i], k->out[i], &k->buf);
  k->done[i] = 1;
  InterlockedIncrement(&k->x->rt->progDone);
  xp_stage(k->x, XP_OPS, (int)InterlockedIncrement(k->count));
}

static DWORD WINAPI xp_worker(LPVOID param) {
  XpWork *k = (XpWork *)param;
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[280];
  if (!k->ok || !plm_connect(&env, &dbc, err, 280)) return 0; /* позиции возьмут другие */
  g_qTimeout = 60;
  g_qCancel = k->x->rt->cancel;
  while (!xp_late(k->x)) {
    LONG i = InterlockedIncrement(k->next) - 1;
    if (i >= k->np) break;
    xp_pos_one(dbc, k, (int)i);
  }
  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);
  return 0;
}

/* таблицу позиции — в общую: строки переходят целиком, без копирования */
static void xp_move_rows(XpTable *dst, XpTable *src) {
  for (int r = 0; r < src->n; r++) {
    wchar_t **w = xp_row(dst);
    if (!w) break;
    for (int c = 0; c < dst->ncols; c++) {
      w[c] = src->cell[(size_t)r * (size_t)src->ncols + (size_t)c];
      src->cell[(size_t)r * (size_t)src->ncols + (size_t)c] = NULL;
    }
  }
  for (size_t i = 0; i < (size_t)src->n * (size_t)src->ncols; i++) free(src->cell[i]);
  free(src->cell);
  src->cell = NULL;
  src->n = src->cap = 0;
}

static void xp_pos_sheets(SQLHDBC dbc, XpJob *x, XpPos *pos, int np) {
  RtJob *j = x->rt;
  if (!(x->what & ((1u << XP_OPS) | (1u << XP_MAT) | (1u << XP_TOOL) | (1u << XP_PF)))) return;
  XpTable(*out)[XP_NT] = (XpTable(*)[XP_NT])calloc((size_t)np, sizeof(XpTable[XP_NT]));
  char *done = (char *)calloc((size_t)np, 1);
  if (!out || !done) {
    free(out);
    free(done);
    return;
  }
  for (int i = 0; i < np; i++)
    for (int k = 0; k < XP_NT; k++) out[i][k].ncols = kXpSheets[k].ncols;
  xp_prog(x, XP_OPS, np);
  volatile LONG next = 0, count = 0;
  int nw = np < XP_WORKERS ? np : XP_WORKERS;
  XpWork work[XP_WORKERS + 1];
  HANDLE th[XP_WORKERS];
  int started = 0;
  for (int k = 0; k <= nw; k++) {
    work[k].x = x;
    work[k].pos = pos;
    work[k].np = np;
    work[k].out = out;
    work[k].done = done;
    work[k].next = &next;
    work[k].count = &count;
    work[k].ok = xp_buf_new(&work[k].buf);
  }
  for (int k = 0; k < nw; k++) {
    th[started] = CreateThread(NULL, 0, xp_worker, &work[k], 0, NULL);
    if (th[started]) started++;
  }
  if (started) WaitForMultipleObjects((DWORD)started, th, TRUE, INFINITE);
  for (int k = 0; k < started; k++) CloseHandle(th[k]);
  /* что потоки не взяли — здесь, своим подключением */
  XpWork *rest = &work[nw];
  for (int i = 0; i < np && !xp_late(x) && rest->ok; i++)
    if (!done[i]) xp_pos_one(dbc, rest, i);
  for (int i = 0; i < np; i++)
    for (int k = 0; k < XP_NT; k++) xp_move_rows(&x->t[k], &out[i][k]);
  for (int k = 0; k <= nw; k++) {
    if (work[k].buf.lg && work[k].buf.lg->logLen) {
      rt_log(j, k < nw ? L"\r\n— поток %d —\r\n" : L"\r\n— без потоков —\r\n", k + 1);
      rt_log_raw(j, work[k].buf.lg->log, work[k].buf.lg->logLen);
    }
    xp_buf_free(&work[k].buf);
  }
  free(out);
  free(done);
  rt_log(j, L"\r\nОпераций: %d, материалов: %d, инструмента: %d, заготовок: %d (потоков %d)\r\n", x->t[XP_OPS].n,
         x->t[XP_MAT].n, x->t[XP_TOOL].n, x->t[XP_PF].n, started);
}

/* ---- лист «Извещения» ---------------------------------------------------------- */

/* Как их находит сам PLM: «где используется» версии (WhereUsed с отбором по
   шаблону): извещение ссылается на версию строкой пакета —
     ТП (TechChangeNotification): ObjectsPacket.ApprovedObject / CanceledObject,
       ThroughPacketVersion, ThroughPacket.TechProc (сквозной);
     КД (ChangeNotification): ControlledPackets.ControlledObject /
       CanceledControlledObject.
   И обратно: версия сама держит ECNDocument (извещение, по которому
   утверждена) и CanceledNotification (по которому аннулирована).
   Цели позиции: она сама, её версия изделия и другие версии того же изделия;
   ТП и все его версии; карточка техсостава и её версии.
   С 2026.09.23.81 — общими запросами на все позиции сразу (прежде три запроса
   на позицию, и поиск «кто ссылается» по каждой — долго): сначала прямые
   ссылки целей на извещения (быстро), потом «кто ссылается» пачками. */
static const wchar_t *xp_ecn_role(const wchar_t *key) {
  static const wchar_t *const k[][2] = {
      {L"ApprovedObject", L"утверждает"},  {L"CanceledObject", L"аннулирует"},
      {L"ControlledObject", L"вводит"},    {L"CanceledControlledObject", L"аннулирует"},
      {L"TechProc", L"сквозной ТП"},       {L"ECNDocument", L"утверждено им"},
      {L"CanceledNotification", L"аннулировано им"}};
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
    if (!_wcsicmp(key, k[i][0])) return k[i][1];
  return key;
}

/* шаблон извещения: штатные ChangeNotification / TechChangeNotification, а на
   случай заводских модификаторов — любой «…Notification» или «извещени…» */
#define XP_ECN_TEMPLATE                                                                       \
  L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=n.TemplateId "                           \
  L"AND (t.NameKey IN (N'ChangeNotification',N'TechChangeNotification') "                     \
  L"OR CAST(t.NameKey AS NVARCHAR(200)) LIKE N'%%Notification%%' "                            \
  L"OR CAST(ISNULL(t.NameUI,N'') AS NVARCHAR(200)) LIKE N'%%звещени%%') "
#define XP_ECN_KEY                                                                            \
  L"CAST(nk.Value AS NVARCHAR(100)) + N'|' + CAST(t.NameKey AS NVARCHAR(100)) + N'|' + "      \
  L"CAST(ISNULL(t.NameUI,N'') AS NVARCHAR(200))"

typedef struct {
  long id, parent, tpl;
  wchar_t name[160];
} XpTarget;

typedef struct {
  long notice, target;
  BOOL tech;
  wchar_t key[60], name[200];
} XpLink;

typedef struct {
  long id;
  wchar_t num[100], state[100];
} XpNotice;

static int xp_tg_cmp(const void *a, const void *b) {
  long x = ((const XpTarget *)a)->id, y = ((const XpTarget *)b)->id;
  return x < y ? -1 : x > y;
}

static const XpTarget *xp_tg_find(const XpTarget *t, int n, long id) {
  XpTarget key;
  key.id = id;
  return (const XpTarget *)bsearch(&key, t, (size_t)n, sizeof(XpTarget), xp_tg_cmp);
}

/* список id через запятую из ids[from…to) */
static void xp_idlist(const long *ids, int from, int to, wchar_t *out, int cap) {
  int q = 0;
  out[0] = 0;
  for (int i = from; i < to && q < cap - 14; i++) q += _snwprintf(out + q, 13, q ? L",%ld" : L"%ld", ids[i]);
  out[q] = 0;
}

/* объекты по id (by=0) или по родителю (by=1) — в targets без повторов */
static int xp_tg_load(SQLHDBC dbc, const long *ids, int n, int by, XpTarget *tg, int ntg, int cap, CardRow *rows,
                      wchar_t *sql, RtJob *j) {
  wchar_t list[300 * 13], err[280];
  for (int c = 0; c < n; c += 300) {
    xp_idlist(ids, c, c + 300 < n ? c + 300 : n, list, 300 * 13);
    if (!list[0]) continue;
    _snwprintf(sql, 12000,
               L"SELECT TOP 2000 o.InfoObjectId, CAST(o.Name AS NVARCHAR(250)), N'', ISNULL(o.ParentId,0), o.TemplateId "
               L"FROM InfoObjects AS o WITH(NOLOCK) WHERE o.Erased=0 AND o.%s IN (%s)",
               by ? L"ParentId" : L"InfoObjectId", list);
    sql[11999] = 0;
    int m = card_query(dbc, sql, rows, 2000, err, 280);
    if (m < 0) rt_log(j, L"  извещения: объекты не прочитались — %s\r\n", err);
    for (int i = 0; i < m && ntg < cap; i++) {
      int dup = 0;
      for (int k = 0; k < ntg && !dup; k++) dup = tg[k].id == rows[i].n1;
      if (dup) continue;
      tg[ntg].id = rows[i].n1;
      tg[ntg].parent = rows[i].n2;
      tg[ntg].tpl = rows[i].n3;
      lstrcpynW(tg[ntg].name, rows[i].s1, 160);
      ntg++;
    }
  }
  return ntg;
}

static int xp_add_id(long *ids, int n, int cap, long id) {
  if (!id || n >= cap) return n;
  for (int i = 0; i < n; i++)
    if (ids[i] == id) return n;
  ids[n] = id;
  return n + 1;
}

/* входит ли объект в цели позиции */
static BOOL xp_tg_of(const RtRow *r, long prod, long ptpl, const XpTarget *t) {
  if (t->id == r->id || t->id == r->par || t->id == r->tp || t->id == r->tcCard) return TRUE;
  if (t->parent && (t->parent == r->tp || t->parent == r->tcCard)) return TRUE;
  return prod && t->parent == prod && t->tpl == ptpl;
}

/* извещений нет совсем — кто ссылается на цели первой позиции (их шаблоны) */
static void xp_ecn_diag(SQLHDBC dbc, RtJob *j, const RtRow *r, const wchar_t *list, CardRow *rows, wchar_t *sql) {
  wchar_t err[280];
  _snwprintf(sql, 12000,
             L"SELECT TOP 30 COUNT(*), CAST(t.NameKey AS NVARCHAR(200)) + N' · ' + CAST(ISNULL(t.NameUI,N'') AS NVARCHAR(200)), "
             L"CAST(nk.Value AS NVARCHAR(200)), 0, 0 "
             L"FROM (SELECT a.NameKeyId AS K, a.OwnerId AS O FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"WHERE a.Link IN (%s) AND a.DataType=6 AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0 "
             L"UNION ALL SELECT a.NameKeyId, la.OwnerId FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
             L"ON ce.CollectionElementId=a.CollectionElementId AND ce.Outdated=0 "
             L"JOIN InfoObjectAttributes AS la WITH(NOLOCK) ON la.AttributeId=ce.AttributeId "
             L"WHERE a.Link IN (%s) AND a.DataType=6 AND a.Outdated=0) AS x "
             L"JOIN InfoObjects AS n WITH(NOLOCK) ON n.InfoObjectId=x.O AND n.Erased=0 "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=n.TemplateId "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=x.K "
             L"GROUP BY t.NameKey, t.NameUI, nk.Value ORDER BY COUNT(*) DESC",
             list, list);
  sql[11999] = 0;
  int n = card_query(dbc, sql, rows, 30, err, 280);
  rt_log(j, L"  извещений не нашлось; на %s, его версии, ТП и техсостав ссылаются (для проверки):\r\n", r->f[RC_DES]);
  if (n < 0) rt_log(j, L"    запрос не выполнился — %s\r\n", err);
  for (int i = 0; i < n; i++) rt_log(j, L"    %ld × %s — поле %s\r\n", rows[i].n1, rows[i].s1, rows[i].s2);
  _snwprintf(sql, 12000,
             L"SELECT TOP 30 COUNT(*), CAST(nk.Value AS NVARCHAR(200)), CAST(t.NameKey AS NVARCHAR(200)) + N' · ' + "
             L"CAST(ISNULL(t.NameUI,N'') AS NVARCHAR(200)), 0, 0 FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"JOIN InfoObjects AS n WITH(NOLOCK) ON n.InfoObjectId=a.Link "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=n.TemplateId "
             L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 AND a.DataType=6 AND ISNULL(a.Link,0)<>0 "
             L"GROUP BY nk.Value, t.NameKey, t.NameUI ORDER BY COUNT(*) DESC",
             list);
  sql[11999] = 0;
  n = card_query(dbc, sql, rows, 30, err, 280);
  rt_log(j, L"  а сами они ссылаются на:\r\n");
  if (n < 0) rt_log(j, L"    запрос не выполнился — %s\r\n", err);
  for (int i = 0; i < n; i++) rt_log(j, L"    %ld × поле %s → %s\r\n", rows[i].n1, rows[i].s1, rows[i].s2);
}

#define XP_TG_MAX 20000
#define XP_LINK_MAX 20000

static void xp_ecn(SQLHDBC dbc, XpJob *x, XpPos *pos, int np) {
  RtJob *j = x->rt;
  xp_prog(x, XP_ECN, 0);
  wchar_t *sql = (wchar_t *)malloc(12000 * sizeof(wchar_t));
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 2000);
  XpTarget *tg = (XpTarget *)malloc(sizeof(XpTarget) * XP_TG_MAX);
  XpLink *ln = (XpLink *)malloc(sizeof(XpLink) * XP_LINK_MAX);
  long *ids = (long *)calloc(XP_TG_MAX, sizeof(long));
  long *prod = (long *)calloc((size_t)np, sizeof(long)), *ptpl = (long *)calloc((size_t)np, sizeof(long));
  XpNotice *nt = (XpNotice *)malloc(sizeof(XpNotice) * 3000);
  if (!sql || !rows || !tg || !ln || !ids || !prod || !ptpl || !nt) goto out;
  wchar_t err[280], list[300 * 13];
  int ntg = 0, nl = 0, nn = 0, n;
  /* 1. цели: сами позиции, их версии изделия, ТП, карточки техсостава */
  n = 0;
  for (int p = 0; p < np; p++) {
    const RtRow *r = pos[p].r;
    n = xp_add_id(ids, n, XP_TG_MAX, r->id);
    n = xp_add_id(ids, n, XP_TG_MAX, r->par);
    n = xp_add_id(ids, n, XP_TG_MAX, r->tp);
    n = xp_add_id(ids, n, XP_TG_MAX, r->tcCard);
  }
  ntg = xp_tg_load(dbc, ids, n, 0, tg, 0, XP_TG_MAX, rows, sql, j);
  /* изделие версии (её родитель) и шаблон версии — по ним другие версии */
  for (int p = 0; p < np; p++)
    for (int k = 0; k < ntg; k++)
      if (tg[k].id == pos[p].r->par) {
        prod[p] = tg[k].parent;
        ptpl[p] = tg[k].tpl;
        break;
      }
  /* 2. вложенные: версии ТП, версии техсостава, версии изделия */
  n = 0;
  for (int p = 0; p < np; p++) {
    n = xp_add_id(ids, n, XP_TG_MAX, pos[p].r->tp);
    n = xp_add_id(ids, n, XP_TG_MAX, pos[p].r->tcCard);
    n = xp_add_id(ids, n, XP_TG_MAX, prod[p]);
  }
  ntg = xp_tg_load(dbc, ids, n, 1, tg, ntg, XP_TG_MAX, rows, sql, j);
  /* чужое под изделием (исполнения, документы) — не цели: оставим только нужное */
  int keep = 0;
  for (int k = 0; k < ntg; k++) {
    BOOL any = FALSE;
    for (int p = 0; p < np && !any; p++) any = xp_tg_of(pos[p].r, prod[p], ptpl[p], &tg[k]);
    if (any) tg[keep++] = tg[k];
  }
  ntg = keep;
  qsort(tg, (size_t)ntg, sizeof(XpTarget), xp_tg_cmp);
  for (int k = 0; k < ntg; k++) ids[k] = tg[k].id;
  rt_log(j, L"\r\nИзвещения: целей %d (позиции, версии изделия, ТП, техсостав)\r\n", ntg);
  xp_prog(x, XP_ECN, (ntg + 299) / 300 + (ntg + 149) / 150);
  xp_stage(x, XP_ECN, 0);
  /* 3а. прямые ссылки целей на извещения (ECNDocument, CanceledNotification…) — быстро */
  for (int c = 0; c < ntg && !xp_late(x); c += 300) {
    xp_idlist(ids, c, c + 300 < ntg ? c + 300 : ntg, list, 300 * 13);
    _snwprintf(sql, 12000,
               L"SELECT TOP 2000 n.InfoObjectId, CAST(n.Name AS NVARCHAR(250)), " XP_ECN_KEY L", a.OwnerId, 0 "
               L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
               L"JOIN InfoObjects AS n WITH(NOLOCK) ON n.InfoObjectId=a.Link AND n.Erased=0 " XP_ECN_TEMPLATE
               L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 AND a.DataType=6 AND ISNULL(a.Link,0)<>0",
               list);
    sql[11999] = 0;
    int m = card_query(dbc, sql, rows, 2000, err, 280);
    if (m < 0) rt_log(j, L"  прямые ссылки на извещения: запрос не выполнился — %s\r\n", err);
    for (int i = 0; i < m && nl < XP_LINK_MAX; i++, nl++) {
      ln[nl].notice = rows[i].n1;
      ln[nl].target = rows[i].n2;
      lstrcpynW(ln[nl].name, rows[i].s1, 200);
      lstrcpynW(ln[nl].key, rows[i].s2, 60);
      wchar_t *bar = wcschr(ln[nl].key, L'|');
      if (bar) *bar = 0;
      const wchar_t *tk = wcschr(rows[i].s2, L'|');
      ln[nl].tech = tk && (wcsstr(tk, L"Tech") || wcsstr(tk, L"ТП") || wcsstr(tk, L"технол"));
    }
    InterlockedIncrement(&j->progDone);
  }
  int direct = nl;
  /* 3б. «кто ссылается» — пачками по 150 целей; не уложилась пачка — дальше не ищем */
  ULONGLONG tw = GetTickCount64();
  int save = g_qTimeout;
  g_qTimeout = 90;
  for (int c = 0; c < ntg && !xp_late(x); c += 150) {
    xp_idlist(ids, c, c + 150 < ntg ? c + 150 : ntg, list, 300 * 13);
    _snwprintf(sql, 12000,
               L"SELECT TOP 2000 n.InfoObjectId, CAST(n.Name AS NVARCHAR(250)), " XP_ECN_KEY L", x.T, 0 "
               L"FROM (SELECT a.Link AS T, a.NameKeyId AS K, a.OwnerId AS O FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"WHERE a.Link IN (%s) AND a.DataType=6 AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0 "
               L"UNION ALL SELECT a.Link, a.NameKeyId, la.OwnerId FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
               L"ON ce.CollectionElementId=a.CollectionElementId AND ce.Outdated=0 "
               L"JOIN InfoObjectAttributes AS la WITH(NOLOCK) ON la.AttributeId=ce.AttributeId "
               L"WHERE a.Link IN (%s) AND a.DataType=6 AND a.Outdated=0) AS x "
               L"JOIN InfoObjects AS n WITH(NOLOCK) ON n.InfoObjectId=x.O AND n.Erased=0 " XP_ECN_TEMPLATE
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=x.K",
               list, list);
    sql[11999] = 0;
    int m = card_query(dbc, sql, rows, 2000, err, 280);
    if (m < 0) {
      rt_log(j, L"  «кто ссылается» на извещения: пачка не уложилась — %s; дальше — только прямые ссылки\r\n", err);
      break;
    }
    for (int i = 0; i < m && nl < XP_LINK_MAX; i++, nl++) {
      ln[nl].notice = rows[i].n1;
      ln[nl].target = rows[i].n2;
      lstrcpynW(ln[nl].name, rows[i].s1, 200);
      lstrcpynW(ln[nl].key, rows[i].s2, 60);
      wchar_t *bar = wcschr(ln[nl].key, L'|');
      if (bar) *bar = 0;
      const wchar_t *tk = wcschr(rows[i].s2, L'|');
      ln[nl].tech = tk && (wcsstr(tk, L"Tech") || wcsstr(tk, L"ТП") || wcsstr(tk, L"технол"));
    }
    InterlockedIncrement(&j->progDone);
    xp_stage(x, XP_ECN, c + 150);
  }
  g_qTimeout = save;
  rt_log(j, L"  ссылок на извещения: прямых %d, «кто ссылается» %d (%.0f с)\r\n", direct, nl - direct,
         (double)(GetTickCount64() - tw) / 1000.0);
  if (!nl) {
    if (np) {
      long first[200];
      int nf = 0;
      for (int k = 0; k < ntg && nf < 200; k++)
        if (xp_tg_of(pos[0].r, prod[0], ptpl[0], &tg[k])) first[nf++] = tg[k].id;
      xp_idlist(first, 0, nf, list, 300 * 13);
      if (list[0]) xp_ecn_diag(dbc, j, pos[0].r, list, rows, sql);
    }
    goto out;
  }
  /* 4. номер и состояние извещений */
  long *nids = ids; /* цели больше не нужны списком */
  int nni = 0;
  for (int i = 0; i < nl; i++) nni = xp_add_id(nids, nni, 3000, ln[i].notice);
  for (int c = 0; c < nni; c += 300) {
    xp_idlist(nids, c, c + 300 < nni ? c + 300 : nni, list, 300 * 13);
    _snwprintf(sql, 12000,
               L"SELECT TOP 2000 a.OwnerId, CAST(nk.Value AS NVARCHAR(100)), CAST(" XP_VALUE(L"a") L" AS NVARCHAR(600)), "
               L"0, 0 FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value IN (N'ECNDesignation',"
               L"N'TechChangeNotificationDesignation',N'ChangeNotificationDocumentNumber',N'Designation',N'Number',"
               L"N'LifeCycleState') "
               L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
               L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0",
               list);
    sql[11999] = 0;
    int m = card_query(dbc, sql, rows, 2000, err, 280);
    static const wchar_t *const numKeys[] = {L"ECNDesignation", L"TechChangeNotificationDesignation",
                                             L"ChangeNotificationDocumentNumber", L"Designation", L"Number"};
    for (int i = c; i < nni && i < c + 300 && nn < 3000; i++) {
      XpNotice *e = &nt[nn++];
      e->id = nids[i];
      e->num[0] = e->state[0] = 0;
      for (int k = 0; k < 5 && !e->num[0]; k++)
        for (int q = 0; q < m && !e->num[0]; q++)
          if (rows[q].n1 == e->id && rows[q].s2[0] && !_wcsicmp(rows[q].s1, numKeys[k])) lstrcpynW(e->num, rows[q].s2, 100);
      for (int q = 0; q < m && !e->state[0]; q++)
        if (rows[q].n1 == e->id && rows[q].s2[0] && !_wcsicmp(rows[q].s1, L"LifeCycleState"))
          lstrcpynW(e->state, rows[q].s2, 100);
    }
  }
  /* 5. по позиции: её извещения — по строке, что делает и с чем — списком */
  long *seen = (long *)malloc(sizeof(long) * 600);
  for (int p = 0; seen && p < np; p++) {
    const RtRow *r = pos[p].r;
    int ns = 0;
    for (int i = 0; i < nl && ns < 600; i++) {
      const XpTarget *t = xp_tg_find(tg, ntg, ln[i].target);
      if (!t || !xp_tg_of(r, prod[p], ptpl[p], t)) continue;
      int dup = 0;
      for (int k = 0; k < ns && !dup; k++) dup = seen[k] == ln[i].notice;
      if (!dup) seen[ns++] = ln[i].notice;
    }
    for (int s = 0; s < ns; s++) {
      wchar_t roles[300] = L"", objs[600] = L"";
      int lr = 0, lo = 0;
      BOOL tech = FALSE;
      const wchar_t *name = L"";
      for (int i = 0; i < nl; i++) {
        if (ln[i].notice != seen[s]) continue;
        const XpTarget *t = xp_tg_find(tg, ntg, ln[i].target);
        if (!t || !xp_tg_of(r, prod[p], ptpl[p], t)) continue;
        name = ln[i].name;
        tech |= ln[i].tech;
        const wchar_t *role = xp_ecn_role(ln[i].key);
        if (!wcsstr(roles, role)) {
          int w = _snwprintf(roles + lr, 300 - lr, L"%s%s", lr ? L", " : L"", role);
          if (w > 0 && lr + w < 299) lr += w;
        }
        if (t->name[0] && !wcsstr(objs, t->name)) {
          int w = _snwprintf(objs + lo, 600 - lo, L"%s%s", lo ? L"; " : L"", t->name);
          if (w > 0 && lo + w < 599) lo += w;
        }
      }
      roles[299] = objs[599] = 0;
      const XpNotice *e = NULL;
      for (int k = 0; k < nn && !e; k++)
        if (nt[k].id == seen[s]) e = &nt[k];
      wchar_t **row = xp_row(&x->t[XP_ECN]);
      if (!row) break;
      xp_set(row, 0, r->f[RC_DES]);
      xp_set(row, 1, r->f[RC_NAME]);
      xp_set(row, 2, tech ? L"ТП" : L"КД");
      xp_set(row, 3, e ? e->num : NULL);
      xp_set(row, 4, name);
      xp_set(row, 5, e ? e->state : NULL);
      xp_set(row, 6, roles);
      xp_set(row, 7, objs);
    }
  }
  free(seen);
  rt_log(j, L"Извещений (строк): %d\r\n", x->t[XP_ECN].n);
out:
  free(sql);
  free(rows);
  free(tg);
  free(ln);
  free(ids);
  free(prod);
  free(ptpl);
  free(nt);
}

/* ---- весь сбор ------------------------------------------------------------------ */

static void xp_build(XpJob *x) {
  RtJob *j = x->rt;
  ULONGLONG t0 = GetTickCount64();
  /* 1. обход ЭСИ — тот же, что у маршрутной ведомости (параллельный) */
  xp_prog(x, XP_COMP, 0);
  xp_stage(x, XP_COMP, 0);
  rt_build(j);
  x->secWalk = (double)(GetTickCount64() - t0) / 1000.0;
  if (!j->n) return;
  if ((x->what >> XP_COMP) & 1) xp_comp(x);
  if (xp_late(x)) return;
  /* 2. листы по уникальным позициям */
  XpPos *pos = (XpPos *)malloc(sizeof(XpPos) * RT_MAX);
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[280];
  if (pos && plm_connect(&env, &dbc, err, 280)) {
    g_qTimeout = 60;
    g_qCancel = j->cancel;
    int np = xp_positions(x, pos, RT_MAX);
    rt_log(j, L"\r\n===== Выгрузка: позиций %d (разных %d), обход %.0f с =====\r\n", j->n, np, x->secWalk);
    ULONGLONG t1 = GetTickCount64();
    xp_pos_sheets(dbc, x, pos, np);
    x->secSheets = (double)(GetTickCount64() - t1) / 1000.0;
    ULONGLONG t2 = GetTickCount64();
    if (((x->what >> XP_ECN) & 1) && !xp_late(x)) xp_ecn(dbc, x, pos, np);
    x->secEcn = (double)(GetTickCount64() - t2) / 1000.0;
    rt_log(j, L"\r\nВремя: обход %.0f с, операции и заготовки %.0f с, извещения %.0f с\r\n", x->secWalk, x->secSheets,
           x->secEcn);
    g_qTimeout = 0;
    g_qCancel = NULL;
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
  } else if (pos) {
    _snwprintf(j->err, 400, L"Не удалось подключиться к PLM: %s", err);
  }
  if (rt_late(j) && !(j->cancel && *j->cancel)) rt_log(j, L"\r\nВремя вышло — собрано не всё.\r\n");
  free(pos);
}

/* ---- пакет для загрузки в PLM ---------------------------------------------------

   Команда PLM «Загрузить ЭСИ из CursorPad» (pidrpen/cursor, папка «Загрузка ЭСИ
   из CursorPad») создаёт по нему изделия, состав, техсостав и ТП с операциями —
   от имени пользователя PLM. Три листа, шапки — ровно такие (команда ищет
   столбцы по началу шапки). «Шаблон пакета…» — пустые листы; «Пакет для
   загрузки…» — из собранной выгрузки: состав, изделия и операции похожего
   изделия, чтобы поменять обозначения и загрузить как новое. */

enum { PK_ITEMS, PK_LINKS, PK_OPS, PK_NT };
static const wchar_t *const kPkItemsHead[] = {L"Обозначение", L"Наименование", L"Вид изделия", L"Материал",
                                              L"Масса, кг"};
static const int kPkItemsW[] = {26, 34, 18, 40, 10};
static const wchar_t *const kPkLinksHead[] = {L"Сборка", L"Входящее", L"Кол-во", L"Позиция"};
static const int kPkLinksW[] = {26, 26, 9, 9};
static const wchar_t *const kPkOpsHead[] = {L"Обозначение", L"№ опер.", L"Операция", L"Участок",
                                            L"Оборудование", L"Тпз, мин", L"Тшт, мин"};
static const int kPkOpsW[] = {26, 8, 30, 20, 34, 9, 9};
static const XpSheetDef kPkSheets[PK_NT] = {
    {L"Изделия", XP_N(kPkItemsHead), kPkItemsHead, kPkItemsW},
    {L"Состав", XP_N(kPkLinksHead), kPkLinksHead, kPkLinksW},
    {L"Операции", XP_N(kPkOpsHead), kPkOpsHead, kPkOpsW},
};

static BOOL xp_seen2(XpTable *t, const wchar_t *a, const wchar_t *b) {
  for (int r = 0; r < t->n; r++)
    if (!_wcsicmp(xp_cell(t, r, 0), a) && !_wcsicmp(xp_cell(t, r, 1), b ? b : L"")) return TRUE;
  return FALSE;
}

/* x — собранная выгрузка (NULL — пустой шаблон); pk — PK_NT таблиц */
static void xp_package(const XpJob *x, XpTable *pk) {
  for (int k = 0; k < PK_NT; k++) pk[k].ncols = kPkSheets[k].ncols;
  if (!x) return;
  const XpTable *comp = &x->t[XP_COMP], *ops = &x->t[XP_OPS];
  for (int r = 0; r < comp->n; r++) {
    const wchar_t *des = xp_cell((void *)comp, r, 1), *parent = xp_cell((void *)comp, r, 3);
    if (!des[0]) continue;
    if (!xp_seen2(&pk[PK_ITEMS], des, NULL)) {
      wchar_t **w = xp_row(&pk[PK_ITEMS]);
      if (!w) return;
      xp_set(w, 0, des);
      xp_set(w, 1, xp_cell((void *)comp, r, 2));
      xp_set(w, 2, xp_cell((void *)comp, r, 4));
      xp_set(w, 3, xp_cell((void *)comp, r, 7));
      xp_set(w, 4, xp_cell((void *)comp, r, 10));
    }
    /* сборка в дереве встречается не раз — её строки состава один раз */
    if (parent[0] && !xp_seen2(&pk[PK_LINKS], parent, des)) {
      wchar_t **w = xp_row(&pk[PK_LINKS]);
      if (!w) return;
      xp_set(w, 0, parent);
      xp_set(w, 1, des);
      xp_set(w, 2, xp_cell((void *)comp, r, 5));
    }
  }
  for (int r = 0; r < ops->n; r++) {
    const wchar_t *des = xp_cell((void *)ops, r, 0), *num = xp_cell((void *)ops, r, 4);
    if (!des[0] || xp_seen2(&pk[PK_OPS], des, num)) continue;
    wchar_t **w = xp_row(&pk[PK_OPS]);
    if (!w) return;
    xp_set(w, 0, des);
    xp_set(w, 1, num);
    const wchar_t *op1c = xp_cell((void *)ops, r, 6);
    xp_set(w, 2, op1c[0] ? op1c : xp_cell((void *)ops, r, 5)); /* операция — с кодом 1С: по нему команда найдёт её */
    xp_set(w, 3, xp_cell((void *)ops, r, 7));
    xp_set(w, 4, xp_cell((void *)ops, r, 9));
    /* нормы — в минутах; в PLM бывают в часах (единица «ч», «час») */
    const wchar_t *unit = xp_cell((void *)ops, r, 13);
    double k = (unit[0] == L'ч' || unit[0] == L'Ч') ? 60.0 : 1.0;
    for (int c = 0; c < 2; c++) {
      double v;
      if (!xp_num(xp_cell((void *)ops, r, 11 + c), &v)) continue;
      wchar_t nb[40];
      xp_fmt3(v * k, nb, 40);
      xp_set(w, 5 + c, nb);
    }
  }
}

static void xp_package_free(XpTable *pk) {
  for (int k = 0; k < PK_NT; k++) {
    for (size_t i = 0; i < (size_t)pk[k].n * (size_t)pk[k].ncols; i++) free(pk[k].cell[i]);
    free(pk[k].cell);
    memset(&pk[k], 0, sizeof(pk[k]));
  }
}
