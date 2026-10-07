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

enum { XP_COMP, XP_OPS, XP_MAT, XP_TOOL, XP_ECN, XP_NT };

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
};

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

/* значение поля по псевдониму таблицы атрибутов (как PF_VALUE_SQL, но для любого
   псевдонима): у ссылки — имя объекта (lo), у перечисления — его название */
#define XP_VALUE(al)                                                                          \
  L"COALESCE(lo.Name, NULLIF(CASE " al L".DataType "                                           \
  L"WHEN 3 THEN CASE WHEN " al L".BoolValue=1 THEN N'да' ELSE N'нет' END "                     \
  L"WHEN 2 THEN CAST(" al L".ShortText AS NVARCHAR(400)) "                                     \
  L"WHEN 24 THEN CAST(" al L".LargeText AS NVARCHAR(400)) "                                    \
  L"WHEN 11 THEN (SELECT TOP 1 CAST(cvn.NameUI AS NVARCHAR(400)) FROM NamedValues AS cvn WITH(NOLOCK) " \
  L"WHERE cvn.NamedValueId=" al L".Link) "                                                     \
  L"WHEN 22 THEN (SELECT TOP 1 CAST(cvt.NameUI AS NVARCHAR(400)) FROM Templates AS cvt WITH(NOLOCK) " \
  L"WHERE cvt.TemplateId=" al L".Link) "                                                       \
  L"WHEN 25 THEN (SELECT TOP 1 CAST(cvt.NameUI AS NVARCHAR(400)) FROM Templates AS cvt WITH(NOLOCK) " \
  L"WHERE cvt.TemplateId=" al L".Link) END, N''), "                                            \
  L"CONVERT(NVARCHAR(64), " al L".FloatNumber), CONVERT(NVARCHAR(64), " al L".IntegerNumber), " \
  L"CASE WHEN " al L".DataType<>6 THEN CONVERT(NVARCHAR(64), " al L".LongNumber) END, N'')"

/* Поля объектов ids одним запросом:
     свои поля из списка own                       — путь «Поле»;
     строки списков и составных из списка coll     — «Список|Поле»;
     составные внутри этих строк                   — «Список|Поле|Подполе».
   own и coll — списки для IN: N'Area',N'WorkShop'. n1 — владелец, n2 — строка
   списка (0 — своё поле), n3 — DataType + 1000, если значение устаревшее
   (поля составных PLM читает без отбора по Outdated — актуальные первыми,
   так и здесь: порядок владелец · строка · путь · устаревшее), s1 — путь,
   s2 — значение. */
static int xp_fields(SQLHDBC dbc, const wchar_t *ids, const wchar_t *own, const wchar_t *coll, CardRow *rows, int max,
                     wchar_t *err) {
  size_t cap = 9000 + wcslen(ids) * 3;
  wchar_t *sql = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!sql) return -1;
  _snwprintf(sql, cap,
             L"SELECT TOP %d a.OwnerId, CAST(nk.Value AS NVARCHAR(200)), CAST(" XP_VALUE(L"a") L" AS NVARCHAR(600)), "
             L"0, a.DataType "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value IN (%s) "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0 "
             L"UNION ALL "
             L"SELECT p.OwnerId, CAST(nkp.Value AS NVARCHAR(90)) + N'|' + CAST(nk.Value AS NVARCHAR(100)), "
             L"CAST(" XP_VALUE(L"a") L" AS NVARCHAR(600)), ce.CollectionElementId, "
             L"a.DataType + 1000 * ISNULL(CAST(a.Outdated AS INT),0) "
             L"FROM InfoObjectAttributes AS p WITH(NOLOCK) "
             L"JOIN NameKeys AS nkp WITH(NOLOCK) ON nkp.NameKeyId=p.NameKeyId AND nkp.Value IN (%s) "
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
             L"ON ce.AttributeId IN (p.AttributeId, ISNULL(p.Link,0)) AND ce.Outdated=0 "
             L"JOIN InfoObjectAttributes AS a WITH(NOLOCK) ON a.CollectionElementId=ce.CollectionElementId "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value<>N'LastChanged' "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE p.OwnerId IN (%s) AND p.Outdated=0 AND ISNULL(p.CollectionElementId,0)=0 "
             L"UNION ALL "
             L"SELECT p.OwnerId, CAST(nkp.Value AS NVARCHAR(60)) + N'|' + CAST(nka.Value AS NVARCHAR(60)) + N'|' + "
             L"CAST(nk.Value AS NVARCHAR(70)), CAST(" XP_VALUE(L"b") L" AS NVARCHAR(600)), ce.CollectionElementId, "
             L"b.DataType + 1000 * ISNULL(CAST(b.Outdated AS INT),0) "
             L"FROM InfoObjectAttributes AS p WITH(NOLOCK) "
             L"JOIN NameKeys AS nkp WITH(NOLOCK) ON nkp.NameKeyId=p.NameKeyId AND nkp.Value IN (%s) "
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
             L"ON ce.AttributeId IN (p.AttributeId, ISNULL(p.Link,0)) AND ce.Outdated=0 "
             L"JOIN InfoObjectAttributes AS a WITH(NOLOCK) ON a.CollectionElementId=ce.CollectionElementId "
             L"AND a.DataType=23 AND ISNULL(a.Outdated,0)=0 "
             L"JOIN NameKeys AS nka WITH(NOLOCK) ON nka.NameKeyId=a.NameKeyId "
             L"JOIN InfoObjectCollectionElements AS ce2 WITH(NOLOCK) "
             L"ON ce2.AttributeId IN (a.AttributeId, ISNULL(a.Link,0)) AND ce2.Outdated=0 "
             L"JOIN InfoObjectAttributes AS b WITH(NOLOCK) ON b.CollectionElementId=ce2.CollectionElementId "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=b.NameKeyId AND nk.Value<>N'LastChanged' "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON b.DataType=6 AND lo.InfoObjectId=b.Link "
             L"WHERE p.OwnerId IN (%s) AND p.Outdated=0 AND ISNULL(p.CollectionElementId,0)=0 "
             L"ORDER BY 1, 4, 2, 5",
             max, own, ids, coll, ids, coll, ids);
  sql[cap - 1] = 0;
  int n = card_query(dbc, sql, rows, max, err, 280);
  free(sql);
  return n;
}

/* Первое значение по пути у владельца a (нет — у b, это его TSOperation);
   row — только эта строка списка (0 — любая). Пути — через «;», по порядку
   важности: «TimePerPiece|Value;TimePerPiece». Устаревшее — только если
   актуального нет (оно в выборке после актуального). */
static const wchar_t *xp_get(const CardRow *f, int nf, long a, long b, long row, const wchar_t *paths) {
  wchar_t buf[400];
  lstrcpynW(buf, paths, 400);
  for (wchar_t *p = buf, *next; p && *p; p = next) {
    next = wcschr(p, L';');
    if (next) *next++ = 0;
    for (int pass = 0; pass < 2; pass++) {
      long own = pass ? b : a;
      if (!own) continue;
      for (int i = 0; i < nf; i++) {
        if (f[i].n1 != own || (row && f[i].n2 != row) || !f[i].s2[0]) continue;
        if (!_wcsicmp(f[i].s1, p)) return f[i].s2;
      }
    }
  }
  return NULL;
}

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

/* ---- операции ТП позиции: общее для листов операций, материалов, инструмента -- */

#define XP_OPS_MAX 200
#define XP_FIELDS 3000

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

static void xp_ops_sheets(SQLHDBC dbc, XpJob *x, XpPos *pos, int np, CardRow *rows) {
  RtJob *j = x->rt;
  BOOL wantOps = (x->what >> XP_OPS) & 1, wantMat = (x->what >> XP_MAT) & 1, wantTool = (x->what >> XP_TOOL) & 1;
  if (!wantOps && !wantMat && !wantTool) return;
  CardRow *f = (CardRow *)malloc(sizeof(CardRow) * XP_FIELDS);
  CardRow *dr = (CardRow *)malloc(sizeof(CardRow) * 300);
  XpOp *ops = (XpOp *)malloc(sizeof(XpOp) * XP_OPS_MAX);
  if (!f || !dr || !ops) {
    free(f);
    free(dr);
    free(ops);
    return;
  }
  BOOL keysShown = FALSE;
  int totalOps = 0;
  for (int p = 0; p < np && !xp_late(x); p++) {
    const RtRow *r = pos[p].r;
    if (!r->tpVar) continue;
    int no = xp_ops_of(dbc, r->tpVar, rows, ops, XP_OPS_MAX, j);
    if (!no) continue;
    wchar_t qty[40];
    xp_fmt3(pos[p].qtyAll, qty, 40);
    /* по 8 операций за запрос: у каждой десятки полей и строк */
    for (int b0 = 0; b0 < no && !xp_late(x); b0 += 8) {
      int b1 = b0 + 8 < no ? b0 + 8 : no;
      wchar_t ids[400], opIds[200];
      int q = 0, w = 0;
      for (int i = b0; i < b1; i++) {
        q += _snwprintf(ids + q, 400 - q, q ? L",%ld" : L"%ld", ops[i].id);
        if (ops[i].ts) q += _snwprintf(ids + q, 400 - q, L",%ld", ops[i].ts);
        w += _snwprintf(opIds + w, 200 - w, w ? L",%ld" : L"%ld", ops[i].id);
      }
      wchar_t err[280];
      int nf = xp_fields(dbc, ids, XP_OP_OWN, XP_OP_COLL, f, XP_FIELDS, err);
      if (nf < 0) {
        rt_log(j, L"  поля операций %s: запрос не выполнился — %s\r\n", r->f[RC_DES], err);
        nf = 0;
      }
      if (nf >= XP_FIELDS) rt_log(j, L"  поля операций %s: строк больше %d — часть не прочитана\r\n", r->f[RC_DES],
                                  XP_FIELDS);
      if (!keysShown && nf > 0) { /* какие поля пришли — раз, для проверки */
        keysShown = TRUE;
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
          wchar_t **row = xp_row(&x->t[XP_OPS]);
          if (!row) break;
          totalOps++;
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
            xp_fmt3(v * pos[p].qtyAll, nb, 40);
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
            wchar_t **row = xp_row(&x->t[XP_MAT]);
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
              xp_fmt3(v * pos[p].qtyAll, nb, 40);
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
            wchar_t **row = xp_row(&x->t[XP_TOOL]);
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
    xp_stage(x, XP_OPS, p + 1);
  }
  rt_log(j, L"\r\nОпераций: %d, материалов: %d, инструмента: %d\r\n", totalOps, x->t[XP_MAT].n, x->t[XP_TOOL].n);
  free(f);
  free(dr);
  free(ops);
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
   Цели: ТП позиции и все его версии; объект позиции, его версия изделия и
   другие версии того же изделия. */
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

/* извещений не нашлось — какие объекты ссылаются на цели (их шаблоны), раз */
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
  rt_log(j, L"  извещений у %s не нашлось; на него, его версии, ТП и техсостав ссылаются (для проверки):\r\n",
         r->f[RC_DES]);
  if (n < 0) rt_log(j, L"    запрос не выполнился — %s\r\n", err);
  for (int i = 0; i < n; i++) rt_log(j, L"    %ld × %s — поле %s\r\n", rows[i].n1, rows[i].s1, rows[i].s2);
}

static void xp_ecn(SQLHDBC dbc, XpJob *x, XpPos *pos, int np, CardRow *rows) {
  RtJob *j = x->rt;
  wchar_t *sql = (wchar_t *)malloc(12000 * sizeof(wchar_t));
  CardRow *tg = (CardRow *)malloc(sizeof(CardRow) * 200);
  CardRow *nd = (CardRow *)malloc(sizeof(CardRow) * 400);
  if (!sql || !tg || !nd) {
    free(sql);
    free(tg);
    free(nd);
    return;
  }
  wchar_t err[280];
  int total = 0;
  BOOL diag = FALSE;
  for (int p = 0; p < np && !xp_late(x); p++) {
    const RtRow *r = pos[p].r;
    long tp = r->tp, id = r->id, par = r->par, tc = r->tcCard;
    /* цели: n1 — объект, s1 — его имя, n2 — 2 у ТП и его версий, 1 — изделие */
    _snwprintf(sql, 12000,
               L"SELECT TOP 200 o.InfoObjectId, CAST(o.Name AS NVARCHAR(250)), N'', "
               L"CASE WHEN %ld<>0 AND (o.InfoObjectId=%ld OR o.ParentId=%ld) THEN 2 ELSE 1 END, 0 "
               L"FROM InfoObjects AS o WITH(NOLOCK) WHERE o.Erased=0 AND (o.InfoObjectId IN (%ld,%ld,%ld) "
               L"OR (%ld<>0 AND o.ParentId=%ld) "
               L"OR (%ld<>0 AND o.ParentId=(SELECT ParentId FROM InfoObjects WITH(NOLOCK) WHERE InfoObjectId=%ld) "
               L"AND o.TemplateId=(SELECT TemplateId FROM InfoObjects WITH(NOLOCK) WHERE InfoObjectId=%ld)) "
               /* карточка техсостава и её версии — их утверждают извещения (с 2026.09.23.80) */
               L"OR (%ld<>0 AND (o.InfoObjectId=%ld OR o.ParentId=%ld)))",
               tp, tp, tp, id, par ? par : id, tp ? tp : id, tp, tp, par, par, par, tc, tc, tc);
    int nt = card_query(dbc, sql, tg, 200, err, 280);
    if (nt <= 0) {
      if (nt < 0) rt_log(j, L"  извещения %s: цели не прочитались — %s\r\n", r->f[RC_DES], err);
      continue;
    }
    wchar_t list[200 * 12];
    int q = 0;
    for (int i = 0; i < nt; i++) q += _snwprintf(list + q, 12, q ? L",%ld" : L"%ld", tg[i].n1);
    list[q] = 0;
    _snwprintf(sql, 12000,
               L"SELECT TOP 300 n.InfoObjectId, CAST(n.Name AS NVARCHAR(250)), "
               L"CAST(nk.Value AS NVARCHAR(100)) + N'|' + CAST(t.NameKey AS NVARCHAR(100)) + N'|' + "
               L"CAST(ISNULL(t.NameUI,N'') AS NVARCHAR(200)), x.T, 0 "
               L"FROM (SELECT a.Link AS T, a.NameKeyId AS K, a.OwnerId AS O FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"WHERE a.Link IN (%s) AND a.DataType=6 AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0 "
               L"UNION ALL SELECT a.Link, a.NameKeyId, la.OwnerId FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
               L"ON ce.CollectionElementId=a.CollectionElementId AND ce.Outdated=0 "
               L"JOIN InfoObjectAttributes AS la WITH(NOLOCK) ON la.AttributeId=ce.AttributeId "
               L"WHERE a.Link IN (%s) AND a.DataType=6 AND a.Outdated=0 "
               L"UNION ALL SELECT a.OwnerId, a.NameKeyId, a.Link FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS k WITH(NOLOCK) ON k.NameKeyId=a.NameKeyId "
               L"AND k.Value IN (N'ECNDocument',N'CanceledNotification') "
               L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 AND ISNULL(a.Link,0)<>0) AS x "
               L"JOIN InfoObjects AS n WITH(NOLOCK) ON n.InfoObjectId=x.O AND n.Erased=0 "
               /* шаблон извещения: штатные ChangeNotification / TechChangeNotification, а
                  на случай заводских модификаторов — любой «…Notification» или «извещени…» */
               L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=n.TemplateId "
               L"AND (t.NameKey IN (N'ChangeNotification',N'TechChangeNotification') "
               L"OR CAST(t.NameKey AS NVARCHAR(200)) LIKE N'%%Notification%%' "
               L"OR CAST(ISNULL(t.NameUI,N'') AS NVARCHAR(200)) LIKE N'%%звещени%%') "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=x.K "
               L"ORDER BY n.InfoObjectId",
               list, list, list);
    sql[11999] = 0;
    int nn = card_query(dbc, sql, rows, 300, err, 280);
    if (nn < 0) {
      rt_log(j, L"  извещения %s: запрос не выполнился — %s\r\n", r->f[RC_DES], err);
      continue;
    }
    if (!nn) {
      if (!diag) { /* раз — кто вообще ссылается на эти объекты: по этому видно, где извещения */
        diag = TRUE;
        xp_ecn_diag(dbc, j, r, list, rows, sql);
      }
      continue;
    }
    /* номер и состояние извещений */
    wchar_t nl[300 * 12];
    q = 0;
    for (int i = 0; i < nn; i++)
      if (!i || rows[i].n1 != rows[i - 1].n1) q += _snwprintf(nl + q, 12, q ? L",%ld" : L"%ld", rows[i].n1);
    nl[q] = 0;
    _snwprintf(sql, 12000,
               L"SELECT TOP 400 a.OwnerId, CAST(nk.Value AS NVARCHAR(100)), CAST(" XP_VALUE(L"a") L" AS NVARCHAR(600)), "
               L"0, 0 FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value IN (N'ECNDesignation',"
               L"N'TechChangeNotificationDesignation',N'ChangeNotificationDocumentNumber',N'Designation',N'Number',"
               L"N'LifeCycleState') "
               L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
               L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0",
               nl);
    sql[11999] = 0;
    int ndn = card_query(dbc, sql, nd, 400, err, 280);
    if (ndn < 0) ndn = 0;
    /* по извещению — одна строка: что делает и с чем — списком */
    for (int i = 0; i < nn;) {
      long nid = rows[i].n1;
      wchar_t roles[300] = L"", objs[600] = L"", kind[8] = L"";
      int lr = 0, lo = 0;
      const wchar_t *name = rows[i].s1;
      for (; i < nn && rows[i].n1 == nid; i++) {
        wchar_t key[200];
        lstrcpynW(key, rows[i].s2, 200);
        wchar_t *bar = wcschr(key, L'|');
        if (bar) { /* «ключ|шаблон|название шаблона» */
          *bar = 0;
          const wchar_t *tk = bar + 1, *tn = wcschr(tk, L'|');
          BOOL tech = wcsstr(tk, L"Tech") != NULL || (tn && (wcsstr(tn, L"ТП") || wcsstr(tn, L"технол")));
          lstrcpynW(kind, tech ? L"ТП" : L"КД", 8);
        }
        const wchar_t *role = xp_ecn_role(key);
        if (!wcsstr(roles, role)) {
          int w = _snwprintf(roles + lr, 300 - lr, L"%s%s", lr ? L", " : L"", role);
          if (w > 0 && lr + w < 299) lr += w;
        }
        const wchar_t *on = NULL;
        for (int k = 0; k < nt && !on; k++)
          if (tg[k].n1 == rows[i].n2) on = tg[k].s1;
        if (on && on[0] && !wcsstr(objs, on)) {
          int w = _snwprintf(objs + lo, 600 - lo, L"%s%s", lo ? L"; " : L"", on);
          if (w > 0 && lo + w < 599) lo += w;
        }
      }
      roles[299] = objs[599] = 0;
      const wchar_t *numv = NULL, *state = NULL;
      static const wchar_t *const numKeys[] = {L"ECNDesignation", L"TechChangeNotificationDesignation",
                                               L"ChangeNotificationDocumentNumber", L"Designation", L"Number"};
      for (int k = 0; k < 5 && !numv; k++)
        for (int m = 0; m < ndn && !numv; m++)
          if (nd[m].n1 == nid && nd[m].s2[0] && !_wcsicmp(nd[m].s1, numKeys[k])) numv = nd[m].s2;
      for (int m = 0; m < ndn && !state; m++)
        if (nd[m].n1 == nid && nd[m].s2[0] && !_wcsicmp(nd[m].s1, L"LifeCycleState")) state = nd[m].s2;
      wchar_t **row = xp_row(&x->t[XP_ECN]);
      if (!row) break;
      total++;
      xp_set(row, 0, r->f[RC_DES]);
      xp_set(row, 1, r->f[RC_NAME]);
      xp_set(row, 2, kind);
      xp_set(row, 3, numv);
      xp_set(row, 4, name);
      xp_set(row, 5, state);
      xp_set(row, 6, roles);
      xp_set(row, 7, objs);
    }
    xp_stage(x, XP_ECN, p + 1);
  }
  rt_log(j, L"Извещений (строк): %d\r\n", total);
  free(sql);
  free(tg);
  free(nd);
}

/* ---- весь сбор ------------------------------------------------------------------ */

static void xp_build(XpJob *x) {
  RtJob *j = x->rt;
  /* 1. обход ЭСИ — тот же, что у маршрутной ведомости */
  xp_stage(x, XP_COMP, 0);
  rt_build(j);
  if (!j->n) return;
  if ((x->what >> XP_COMP) & 1) xp_comp(x);
  if (xp_late(x)) return;
  /* 2. листы по уникальным позициям — своим подключением */
  XpPos *pos = (XpPos *)malloc(sizeof(XpPos) * RT_MAX);
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 400);
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[280];
  if (pos && rows && plm_connect(&env, &dbc, err, 280)) {
    g_qTimeout = 60;
    int np = xp_positions(x, pos, RT_MAX);
    rt_log(j, L"\r\n===== Выгрузка: позиций %d (разных %d) =====\r\n", j->n, np);
    xp_ops_sheets(dbc, x, pos, np, rows);
    if (((x->what >> XP_ECN) & 1) && !xp_late(x)) xp_ecn(dbc, x, pos, np, rows);
    g_qTimeout = 0;
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
  } else if (pos && rows) {
    _snwprintf(j->err, 400, L"Не удалось подключиться к PLM: %s", err);
  }
  if (rt_late(j) && !(j->cancel && *j->cancel)) rt_log(j, L"\r\nВремя вышло — собрано не всё.\r\n");
  free(pos);
  free(rows);
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
