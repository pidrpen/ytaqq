/* ---- Маршрутная ведомость из PLM → Excel ----------------------------------

   «Ещё» → «Маршрутная ведомость»: вписали обозначение сборки (или детали),
   «Собрать» — курсор проходит по PLM её состав вглубь и складывает таблицу
   теми же столбцами, что «Ведомость маршрутная КЗ …» (pidrpen/wowdroch):

     Обозначение · Наименование · Входит в · Вид изделия · Количество, шт ·
     Количество на изд., шт · Материал · Сортамент заготовки · Технологич.
     припуск · Масса, кг · Н. расх. на 1 деталь · Н. расх. на изделие ·
     Единицы измерений нормы · Технологический маршрут · Примечание

   «Сохранить в Excel» — настоящий .xlsx (xlsx_write.c), «Копировать» — та же
   таблица через табуляцию.

   Откуда что (те же пути, что у карточки, — они проверены на базе):
     • состав — техсостав сборки: TechCompCard → ActualVersionTechComp →
       вариант этой конфигурации → коллекция TechComposition; строка
       ссылается на входящее изделие. Строки без обозначения (материалы,
       нормали без него) в ведомость не идут;
     • количество — число в строке техсостава; какое поле — ищется по
       названию (Count, Quantity, Amount…); не нашлось — 1 и пометка;
     • «на изделие» — перемножается вниз по дереву;
     • вид изделия — раздел (Section) изделия или строки; нет — «Сборочные
       единицы», если у позиции есть свой состав, иначе «Детали»;
     • материал, сортамент, припуск, норма — заготовка (как в карточке:
       обход pf_explore), масса — Mass изделия;
     • маршрут — цеха операций основного ТП по порядку, через «;»:
       WorkShop операции или её TSOperation, из названия цеха — номер.
   Чего не нашлось — пишется в «Примечание», а весь проход — в «Подробности»:
   его можно прислать, если где-то пусто. Через компьютер коллеги
   (раздача PLM) — тот же сбор у него, ответ целиком. */

#include "xlsx_write.c"

#define WM_RT_PROGRESS (WM_APP + 61)
#define WM_RT_DONE (WM_APP + 62) /* lParam — RtJob*, освобождает получатель */
#define RT_MAX 400
#define RT_DEPTH 8
#define RT_NCOL 15
#define RT_LOG 30000

typedef struct {
  long id;
  int level;
  wchar_t f[RT_NCOL][200]; /* столбцы ведомости, как в файле */
  double qty, qtyTot, norm1;
  BOOL hasNorm;
} RtRow;

typedef struct {
  wchar_t des[200], order[120];
  int n;
  RtRow *rows;
  wchar_t *log;
  int logLen;
  wchar_t err[400];
  ULONGLONG deadline;
  HWND notify;
  volatile LONG *cancel;
  BOOL keysShown; /* поля строки техсостава — в подробности один раз */
  ULONGLONG t0;
} RtJob;

enum { RC_DES, RC_NAME, RC_PARENT, RC_KIND, RC_QTY, RC_QTYTOT, RC_MAT, RC_SORT, RC_ALLOW, RC_MASS, RC_NORM1,
       RC_NORMTOT, RC_UNIT, RC_ROUTE, RC_NOTE };
static const wchar_t *const kRtHead[RT_NCOL] = {
    L"Обозначение",        L"Наименование",         L"Входит в",
    L"Вид изделия",        L"Количество, шт",       L"Количество на изд., шт",
    L"Материал",           L"Сортамент заготовки",  L"Технологич. припуск",
    L"Масса, кг",          L"Н. расх. на 1 деталь", L"Н. расх. на изделие",
    L"Единицы измерений нормы", L"Технологический маршрут", L"Примечание"};
static const int kRtWidth[RT_NCOL] = {26, 30, 24, 16, 10, 12, 40, 20, 10, 10, 12, 12, 10, 26, 30};

static void rt_log(RtJob *j, const wchar_t *fmt, ...) {
  if (!j->log || j->logLen >= RT_LOG - 2) return;
  va_list ap;
  va_start(ap, fmt);
  int n = _vsnwprintf(j->log + j->logLen, (size_t)(RT_LOG - j->logLen - 1), fmt, ap);
  va_end(ap);
  if (n < 0 || n > RT_LOG - j->logLen - 1) n = RT_LOG - j->logLen - 1;
  j->logLen += n;
  j->log[j->logLen] = 0;
}

static BOOL rt_late(RtJob *j) {
  return (j->deadline && GetTickCount64() > j->deadline) || (j->cancel && *j->cancel);
}

/* число из строки («0,397», «0.397 кг»); FALSE — числа нет */
static BOOL rt_num(const wchar_t *s, double *v) {
  wchar_t t[64];
  lstrcpynW(t, s ? s : L"", 64);
  for (wchar_t *c = t; *c; c++)
    if (*c == L',') *c = L'.';
  wchar_t *p = t;
  while (*p && !(iswdigit(*p) || (*p == L'-' && iswdigit(p[1])) || (*p == L'.' && iswdigit(p[1])))) p++;
  if (!*p) return FALSE;
  wchar_t *end = NULL;
  *v = wcstod(p, &end);
  return end != p;
}

/* для таблицы: «0,397», целое — без запятой */
static void rt_fmt(double v, int dec, wchar_t *out, int cap) {
  _snwprintf(out, cap, L"%.*f", dec, v);
  out[cap - 1] = 0;
  wchar_t *dot = wcschr(out, L'.');
  if (dot) {
    size_t l = wcslen(out);
    while (l > 0 && out[l - 1] == L'0') out[--l] = 0;
    if (l > 0 && out[l - 1] == L'.') out[--l] = 0;
    dot = wcschr(out, L'.');
    if (dot) *dot = L',';
  }
}

/* «Цех 31 (механический)» → «31»; без цифр — как есть */
static void rt_ws_code(const wchar_t *v, wchar_t *out, int cap) {
  out[0] = 0;
  const wchar_t *p = v;
  while (*p && !iswdigit(*p)) p++;
  if (!*p) {
    lstrcpynW(out, v, cap);
    return;
  }
  int k = 0;
  while (*p && (iswdigit(*p) || (*p == L'.' && iswdigit(p[1]))) && k < cap - 1) out[k++] = *p++;
  out[k] = 0;
}

/* Где у изделия его свойства и карточки: само, родитель, конфигурация,
   карта взаимосвязей (как PLM_HOLDERS, но и со значениями, не только ссылки).
   После макроса — условие на nk.Value. */
#define RT_HOLDERS(id)                                                                      \
  L"FROM (SELECT InfoObjectId AS Id, ISNULL(ParentId,0) AS Par "                            \
  L"FROM InfoObjects WITH(NOLOCK) WHERE InfoObjectId=" id L") AS x "                         \
  L"CROSS APPLY (SELECT x.Id AS H, 0 AS P UNION SELECT x.Par, 2 "                           \
  L"UNION SELECT hc.Link, 1 FROM InfoObjectAttributes AS hc WITH(NOLOCK) "                  \
  L"JOIN NameKeys AS nhc WITH(NOLOCK) ON nhc.NameKeyId=hc.NameKeyId "                       \
  L"AND nhc.Value=N'ProductConfiguration' "                                                 \
  L"WHERE hc.OwnerId=x.Id AND hc.Outdated=0 AND ISNULL(hc.Link,0)<>0) AS h1 "              \
  L"CROSS APPLY (SELECT h1.H AS H, h1.P AS P UNION SELECT hi.Link, h1.P + 1 "               \
  L"FROM InfoObjectAttributes AS hi WITH(NOLOCK) "                                          \
  L"JOIN NameKeys AS nhi WITH(NOLOCK) ON nhi.NameKeyId=hi.NameKeyId "                       \
  L"AND nhi.Value=N'ProductInterconnectCard' "                                              \
  L"WHERE hi.OwnerId=h1.H AND hi.Outdated=0 AND ISNULL(hi.Link,0)<>0) AS h2 "              \
  L"JOIN InfoObjectAttributes AS a WITH(NOLOCK) ON a.OwnerId=h2.H AND a.Outdated=0 "        \
  L"AND ISNULL(a.CollectionElementId,0)=0 "                                                 \
  L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "

typedef struct {
  wchar_t des[200], name[260], objName[260], section[120], mass[64], massUnit[40];
  long tpCard, pfCard, tcCard, prodConf;
  BOOL desAttr; /* обозначение — свой атрибут, а не из имени */
} RtObj;

static BOOL rt_obj(SQLHDBC dbc, long id, RtObj *o, CardRow *rows, RtJob *j) {
  memset(o, 0, sizeof(*o));
  wchar_t sql[3600], err[280];
  _snwprintf(sql, 3600, L"SELECT TOP 1 o.InfoObjectId, o.Name, N'', 0, 0 FROM InfoObjects AS o WITH(NOLOCK) "
                        L"WHERE o.InfoObjectId=%ld",
             id);
  if (card_query(dbc, sql, rows, 2, err, 280) > 0) lstrcpynW(o->objName, rows[0].s1, 260);
  wchar_t ids[24];
  _snwprintf(ids, 24, L"%ld", id);
  _snwprintf(sql, 3600,
             L"SELECT TOP 80 h2.P, nk.Value, COALESCE(lo.Name, " CARD_VALUE_SQL L", N''), ISNULL(a.Link,0), "
             L"a.DataType " RT_HOLDERS(L"%s")
             L"AND nk.Value IN (N'Designation',N'Name',N'Section',N'Mass',N'MassMeasureUnit',"
             L"N'TechnologicalProcessesCard',N'ProductPreformsCard',N'TechCompCard',N'ProductConfiguration') "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"ORDER BY h2.P",
             ids);
  sql[3599] = 0;
  int n = card_query(dbc, sql, rows, 80, err, 280);
  if (n < 0) {
    rt_log(j, L"  свойства %ld: запрос не выполнился — %s\r\n", id, err);
    return FALSE;
  }
  /* ближнее (само изделие) важнее дальнего (родитель, карта) — берём первое */
  for (int i = 0; i < n; i++) {
    const wchar_t *k = rows[i].s1, *v = rows[i].s2;
    long link = rows[i].n2;
    if (!_wcsicmp(k, L"Designation") && !o->des[0] && v[0]) {
      lstrcpynW(o->des, v, 200);
      o->desAttr = TRUE;
    }
    else if (!_wcsicmp(k, L"Name") && !o->name[0] && v[0]) lstrcpynW(o->name, v, 260);
    else if (!_wcsicmp(k, L"Section") && !o->section[0] && v[0]) lstrcpynW(o->section, v, 120);
    else if (!_wcsicmp(k, L"Mass") && !o->mass[0] && v[0]) lstrcpynW(o->mass, v, 64);
    else if (!_wcsicmp(k, L"MassMeasureUnit") && !o->massUnit[0] && v[0]) lstrcpynW(o->massUnit, v, 40);
    else if (!_wcsicmp(k, L"TechnologicalProcessesCard") && !o->tpCard) o->tpCard = link;
    else if (!_wcsicmp(k, L"ProductPreformsCard") && !o->pfCard) o->pfCard = link;
    else if (!_wcsicmp(k, L"TechCompCard") && !o->tcCard) o->tcCard = link;
    else if (!_wcsicmp(k, L"ProductConfiguration") && !o->prodConf) o->prodConf = link;
  }
  /* своего обозначения нет (конфигурация версии) — из начала имени */
  if (!o->des[0] && o->objName[0]) card_des_from_name(o->objName, o->des, 200);
  if (!o->name[0] && o->objName[0]) card_title_from_name(o->objName, o->name, 260);
  return TRUE;
}

/* маршрут: цеха операций основного ТП через «;» */
static void rt_route(SQLHDBC dbc, const RtObj *o, CardRow *rows, RtJob *j, wchar_t *out, int cap,
                     wchar_t *note, int ncap) {
  out[0] = 0;
  wchar_t sql[3600], err[280];
  long tp = 0, ver = 0;
  if (o->tpCard) {
    _snwprintf(sql, 3600,
               L"SELECT TOP 20 tp.InfoObjectId, tp.Name, ISNULL(flag.V,N'нет'), ISNULL(av.L,0), 0 "
               L"FROM InfoObjectAttributes AS la WITH(NOLOCK) "
               L"JOIN NameKeys AS nkl WITH(NOLOCK) ON nkl.NameKeyId=la.NameKeyId "
               L"AND nkl.Value=N'TechnologicalProcesses' "
               L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId=la.AttributeId "
               L"AND ce.Outdated=0 "
               L"JOIN InfoObjectAttributes AS ea WITH(NOLOCK) "
               L"ON ea.CollectionElementId=ce.CollectionElementId AND ea.DataType=6 "
               L"JOIN InfoObjects AS tp WITH(NOLOCK) ON tp.InfoObjectId=ea.Link AND tp.Erased=0 "
               L"OUTER APPLY (SELECT TOP 1 N'да' AS V FROM InfoObjectAttributes AS ia WITH(NOLOCK) "
               L"JOIN NameKeys AS nki WITH(NOLOCK) ON nki.NameKeyId=ia.NameKeyId "
               L"WHERE ia.OwnerId=tp.InfoObjectId AND ia.Outdated=0 "
               L"AND nki.Value IN (N'MainTP',N'IsActual') AND ia.BoolValue=1) AS flag "
               L"OUTER APPLY (SELECT TOP 1 ISNULL(iv.Link,0) AS L FROM InfoObjectAttributes AS iv WITH(NOLOCK) "
               L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=iv.NameKeyId "
               L"WHERE iv.OwnerId=tp.InfoObjectId AND nkv.Value=N'ActualVersion' AND iv.Outdated=0) AS av "
               L"WHERE la.OwnerId=%ld AND la.Outdated=0 "
               L"ORDER BY CASE WHEN flag.V=N'да' THEN 0 ELSE 1 END, tp.InfoObjectId",
               o->tpCard);
    int n = card_query(dbc, sql, rows, 20, err, 280);
    if (n > 0 && rows[0].n2) {
      tp = rows[0].n1;
      ver = rows[0].n2;
      rt_log(j, L"    ТП: %s (%ld)%s\r\n", rows[0].s1, tp, _wcsicmp(rows[0].s2, L"да") ? L", не помечен основным" : L"");
    }
  }
  if (!tp && o->des[0]) { /* как карточка: по обозначению среди техпроцессов */
    CardOut none = {NULL, 0, 0};
    tp = card_tp_by_designation(dbc, o->des, &none, rows, err, &ver);
    if (tp) rt_log(j, L"    ТП по обозначению: %ld\r\n", tp);
  }
  if (!tp || !ver) {
    lstrcpynW(note, L"нет техпроцесса", ncap);
    rt_log(j, L"    техпроцесса не нашлось\r\n");
    return;
  }
  _snwprintf(sql, 3600,
             L"SELECT TOP 1 ISNULL(mv.Link,0), N'', N'', 0, 0 FROM InfoObjectAttributes AS mv WITH(NOLOCK) "
             L"JOIN NameKeys AS nkm WITH(NOLOCK) ON nkm.NameKeyId=mv.NameKeyId "
             L"WHERE mv.OwnerId=%ld AND mv.Outdated=0 AND nkm.Value=N'MainVariantInVersion'",
             ver);
  int k = card_query(dbc, sql, rows, 2, err, 280);
  long parent = (k > 0 && rows[0].n1) ? rows[0].n1 : ver;
  /* операции по порядку и цех каждой: свой WorkShop или у TSOperation */
  _snwprintf(sql, 3600,
             L"SELECT TOP 200 ch.InfoObjectId, ISNULL(ws.V,N''), ch.Name, ISNULL(num.N,0), 0 "
             L"FROM InfoObjects AS ch WITH(NOLOCK) "
             L"OUTER APPLY (SELECT TOP 1 ts.Link AS L FROM InfoObjectAttributes AS ts WITH(NOLOCK) "
             L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=ts.NameKeyId AND nkt.Value=N'TSOperation' "
             L"WHERE ts.OwnerId=ch.InfoObjectId AND ts.Outdated=0) AS op "
             L"OUTER APPLY (SELECT TOP 1 COALESCE(lo.Name, " CARD_VALUE_SQL L", N'') AS V "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nkw WITH(NOLOCK) ON nkw.NameKeyId=a.NameKeyId AND nkw.Value=N'WorkShop' "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE a.OwnerId IN (ch.InfoObjectId, ISNULL(op.L,0)) AND a.Outdated=0 "
             L"ORDER BY CASE WHEN a.OwnerId=ch.InfoObjectId THEN 0 ELSE 1 END) AS ws "
             L"OUTER APPLY (SELECT TOP 1 ISNULL(nn.IntegerNumber,0) AS N "
             L"FROM InfoObjectAttributes AS nn WITH(NOLOCK) "
             L"JOIN NameKeys AS nkn WITH(NOLOCK) ON nkn.NameKeyId=nn.NameKeyId "
             L"WHERE nn.OwnerId=ch.InfoObjectId AND nn.Outdated=0 "
             L"AND nkn.Value IN (N'Number',N'OperationNumber',N'LocalId')) AS num "
             L"WHERE ch.ParentId=%ld AND ch.Erased=0 ORDER BY num.N, ch.InfoObjectId",
             parent);
  sql[3599] = 0;
  int n = card_query(dbc, sql, rows, 200, err, 280);
  if (n < 0) {
    lstrcpynW(note, L"маршрут не прочитался", ncap);
    rt_log(j, L"    операции: запрос не выполнился — %s\r\n", err);
    return;
  }
  int used = 0, blank = 0;
  for (int i = 0; i < n; i++) {
    if (!rows[i].s1[0]) {
      blank++;
      continue;
    }
    wchar_t code[40];
    rt_ws_code(rows[i].s1, code, 40);
    if (!code[0]) continue;
    size_t l = wcslen(out);
    if (l + wcslen(code) + 2 >= (size_t)cap) break;
    _snwprintf(out + l, cap - l, L"%s%s", l ? L";" : L"", code);
    if (!used) rt_log(j, L"    цех первой операции: «%s» → %s\r\n", rows[i].s1, code);
    used++;
  }
  rt_log(j, L"    операций %d, с цехом %d\r\n", n, used);
  if (!used) lstrcpynW(note, n ? L"у операций нет цеха" : L"в ТП нет операций", ncap);
  (void)blank;
}

/* заготовка: материал, сортамент, припуск, норма (масса заготовки) */
static void rt_preform(SQLHDBC dbc, long pfCard, CardRow *rows, RtJob *j, RtRow *r, wchar_t *note, int ncap) {
  if (!pfCard) {
    lstrcpynW(note, L"нет заготовки", ncap);
    return;
  }
  wchar_t sql[3000], err[280], card[24];
  _snwprintf(card, 24, L"%ld", pfCard);
  _snwprintf(sql, 3000, L"SELECT TOP 5 pf.PfId, pf.PfName, N'', 0, 0 FROM (SELECT %s AS CardId) AS k "
                        PF_APPLY(L"k.CardId") L"ORDER BY pf.PfId",
             card);
  int n = card_query(dbc, sql, rows, 5, err, 280);
  if (n <= 0) {
    lstrcpynW(note, L"нет заготовки", ncap);
    rt_log(j, L"    заготовки в карточке %ld нет\r\n", pfCard);
    return;
  }
  PfSum *sm = (PfSum *)calloc(1, sizeof(PfSum));
  if (!sm) return;
  ULONGLONG one = GetTickCount64() + 6000;
  sm->deadline = j->deadline && j->deadline < one ? j->deadline : one;
  pf_explore(dbc, rows[0].n1, 3, NULL, L"", sm);
  wchar_t mt[480];
  pf_material_text(sm, mt, 480);
  lstrcpynW(r->f[RC_MAT], mt, 200);
  /* сортамент заготовки — как в ведомости: «Круг Ø40 L=35» */
  wchar_t d[40] = L"", l[40] = L"", kind[40] = L"";
  if (sm->zd[0]) pf_num(sm->zd, d, 40);
  if (sm->zl[0]) pf_num(sm->zl, l, 40);
  const wchar_t *src = sm->sort[0] ? sm->sort : sm->mat;
  int k = 0;
  while (src[k] && src[k] != L' ' && k < 39) { /* первое слово сортамента: «Круг», «Лист» */
    kind[k] = src[k];
    k++;
  }
  kind[k] = 0;
  if (d[0] || l[0]) {
    wchar_t *so = r->f[RC_SORT];
    int w = 0;
    if (kind[0]) w += _snwprintf(so + w, 200 - w, L"%s", kind);
    if (d[0] && w >= 0 && w < 200) w += _snwprintf(so + w, 200 - w, L"%sØ%s", w ? L" " : L"", d);
    if (l[0] && w >= 0 && w < 200) _snwprintf(so + w, 200 - w, L"%sL=%s", w ? L" " : L"", l);
    so[199] = 0;
  }
  if (!r->f[RC_SORT][0] && sm->dims[0]) {
    const wchar_t *dv = sm->dims;
    if (!wcsncmp(dv, L"Габариты ", 9)) dv += 9;
    lstrcpynW(r->f[RC_SORT], dv, 200);
  }
  if (sm->za[0]) pf_num(sm->za, r->f[RC_ALLOW], 200);
  double v;
  if (sm->mass[0] && rt_num(sm->mass, &v)) {
    r->norm1 = v;
    r->hasNorm = TRUE;
    rt_fmt(v, 3, r->f[RC_NORM1], 200);
    lstrcpynW(r->f[RC_UNIT], L"кг", 200);
  }
  rt_log(j, L"    заготовка %s: %s · %s · норма %s\r\n", rows[0].s1, mt[0] ? mt : L"—",
         r->f[RC_SORT][0] ? r->f[RC_SORT] : L"—", r->f[RC_NORM1][0] ? r->f[RC_NORM1] : L"—");
  if (!mt[0] && !r->hasNorm) lstrcpynW(note, L"в заготовке нет материала и массы", ncap);
  free(sm);
}

static BOOL rt_looks_des(const wchar_t *s) {
  /* обозначение: есть цифры и точка или дефис — «АДЕ 3424-1073.00.00.01» */
  int dig = 0, sep = 0;
  for (; *s; s++) {
    if (iswdigit(*s)) dig++;
    if (*s == L'.' || *s == L'-') sep++;
  }
  return dig >= 3 && sep >= 1;
}

/* строка техсостава: на что ссылается и сколько */
typedef struct {
  long child;
  double qty;
  BOOL qtyFound;
  wchar_t section[120];
} RtEl;

static BOOL rt_is_qty_key(const wchar_t *k) {
  wchar_t low[80];
  lstrcpynW(low, k, 80);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  static const wchar_t *const want[] = {L"count", L"quantity", L"amount", L"qty", L"kolvo", L"колич"};
  for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++)
    if (wcsstr(low, want[i])) return TRUE;
  return FALSE;
}

static int rt_children(SQLHDBC dbc, const RtObj *o, CardRow *rows, RtJob *j, RtEl *el, int max) {
  if (!o->tcCard) return 0;
  wchar_t *sql = (wchar_t *)malloc(4000 * sizeof(wchar_t));
  if (!sql) return 0;
  wchar_t err[280];
  _snwprintf(sql, 4000,
             L"SELECT DISTINCT TOP 300 ce.CollectionElementId, N'', N'', 0, 0 "
             L"FROM InfoObjectAttributes AS av WITH(NOLOCK) "
             L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=av.NameKeyId "
             L"AND nkv.Value=N'ActualVersionTechComp' "
             L"JOIN InfoObjects AS ch WITH(NOLOCK) ON ch.ParentId=av.Link AND ch.Erased=0 "
             L"JOIN InfoObjectAttributes AS tc WITH(NOLOCK) ON tc.OwnerId=ch.InfoObjectId AND tc.Outdated=0 "
             L"JOIN NameKeys AS nktc WITH(NOLOCK) ON nktc.NameKeyId=tc.NameKeyId AND nktc.Value=N'TechComposition' "
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId=tc.AttributeId "
             L"AND ce.Outdated=0 "
             L"WHERE av.OwnerId=%ld AND av.Outdated=0 "
             L"AND (%ld=0 OR EXISTS (SELECT 1 FROM InfoObjectAttributes AS pr WITH(NOLOCK) "
             L"JOIN NameKeys AS nkpr WITH(NOLOCK) ON nkpr.NameKeyId=pr.NameKeyId AND nkpr.Value=N'Product' "
             L"WHERE pr.OwnerId=ch.InfoObjectId AND pr.Outdated=0 AND pr.Link=%ld)) "
             L"AND NOT EXISTS (SELECT 1 FROM InfoObjectAttributes AS ir WITH(NOLOCK) "
             L"JOIN NameKeys AS nkr WITH(NOLOCK) ON nkr.NameKeyId=ir.NameKeyId AND nkr.Value=N'IsRemoved' "
             L"WHERE ir.CollectionElementId=ce.CollectionElementId AND ir.BoolValue=1) "
             L"ORDER BY ce.CollectionElementId",
             o->tcCard, o->prodConf, o->prodConf);
  int n = card_query(dbc, sql, rows, 300, err, 280);
  if (n <= 0) {
    if (n < 0) rt_log(j, L"    техсостав: запрос не выполнился — %s\r\n", err);
    free(sql);
    return 0;
  }
  int ne = n > max ? max : n;
  long ids[300];
  wchar_t list[3000];
  size_t il = 0;
  list[0] = 0;
  int used = 0;
  for (int i = 0; i < ne; i++) {
    int w = _snwprintf(list + il, 3000 - il, il ? L",%ld" : L"%ld", rows[i].n1);
    if (w <= 0 || il + (size_t)w >= 2990) break;
    il += (size_t)w;
    ids[used++] = rows[i].n1;
  }
  _snwprintf(sql, 4000,
             L"SELECT TOP 900 a.CollectionElementId, nk.Value, COALESCE(lo.Name, " CARD_VALUE_SQL L", N''), "
             L"ISNULL(a.Link,0), a.DataType "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE a.CollectionElementId IN (%s) AND a.Outdated=0 "
             L"ORDER BY a.CollectionElementId, a.DataType DESC, nk.Value",
             list);
  CardRow *ar = (CardRow *)malloc(sizeof(CardRow) * 900);
  int m = ar ? card_query(dbc, sql, ar, 900, err, 280) : -1;
  free(sql);
  if (m < 0) {
    rt_log(j, L"    строки техсостава не прочитались — %s\r\n", err);
    free(ar);
    return 0;
  }
  int out = 0;
  for (int e = 0; e < used && out < max; e++) {
    RtEl x;
    memset(&x, 0, sizeof(x));
    x.qty = 1;
    BOOL childDes = FALSE;
    for (int i = 0; i < m; i++) {
      if (ar[i].n1 != ids[e]) continue;
      /* ссылок в строке бывает несколько — берём ту, что похожа на изделие */
      if (ar[i].n3 == 6 && ar[i].n2 && (!x.child || (!childDes && rt_looks_des(ar[i].s2)))) {
        x.child = ar[i].n2;
        childDes = rt_looks_des(ar[i].s2);
      }
      else if (!_wcsicmp(ar[i].s1, L"Section") && ar[i].s2[0]) lstrcpynW(x.section, ar[i].s2, 120);
      else if (rt_is_qty_key(ar[i].s1) && !x.qtyFound) {
        double v;
        if (rt_num(ar[i].s2, &v) && v > 0) {
          x.qty = v;
          x.qtyFound = TRUE;
        }
      }
    }
    if (!j->keysShown) {
      j->keysShown = TRUE;
      rt_log(j, L"    поля строки техсостава:");
      for (int i = 0; i < m; i++)
        if (ar[i].n1 == ids[e]) rt_log(j, L" %s=%s", ar[i].s1, ar[i].s2[0] ? ar[i].s2 : L"·");
      rt_log(j, L"\r\n");
    }
    if (x.child) el[out++] = x;
  }
  free(ar);
  rt_log(j, L"    в техсоставе строк %d, со ссылкой %d\r\n", used, out);
  return out;
}

static void rt_walk(SQLHDBC dbc, long id, int level, const wchar_t *parentDes, double qty, BOOL qtyFound,
                    double parentTot, const wchar_t *elSection, CardRow *rows, RtJob *j) {
  if (j->n >= RT_MAX || rt_late(j)) return; /* по кругу не уйдёт: глубина не больше RT_DEPTH */
  RtObj o;
  if (!rt_obj(dbc, id, &o, rows, j)) return;
  /* материалы и покупные в ведомость не идут: у них нет ни своего
     обозначения, ни карточек изделия (ТП, заготовки, техсостава) */
  BOOL product = o.desAttr || o.tpCard || o.pfCard || o.tcCard;
  if (level > 0 && (!product || !rt_looks_des(o.des) || (elSection && wcsstr(elSection, L"атериал")))) {
    rt_log(j, L"  %*s· пропуск «%s» — нет обозначения (материал?)\r\n", level * 2, L"", o.objName);
    return;
  }
  RtRow *r = &j->rows[j->n++];
  memset(r, 0, sizeof(*r));
  r->id = id;
  r->level = level;
  r->qty = qty;
  r->qtyTot = qty * parentTot;
  lstrcpynW(r->f[RC_DES], o.des, 200);
  lstrcpynW(r->f[RC_NAME], o.name, 200);
  lstrcpynW(r->f[RC_PARENT], parentDes, 200);
  rt_fmt(r->qty, 3, r->f[RC_QTY], 200);
  rt_fmt(r->qtyTot, 3, r->f[RC_QTYTOT], 200);
  double v;
  if (o.mass[0] && rt_num(o.mass, &v)) rt_fmt(v, 3, r->f[RC_MASS], 200);
  rt_log(j, L"%*s%s %s (%ld), кол-во %s%s\r\n", level * 2, L"", o.des, o.name, id, r->f[RC_QTY],
         level && !qtyFound ? L" (поле количества не найдено — 1)" : L"");
  if (j->notify) PostMessageW(j->notify, WM_RT_PROGRESS, (WPARAM)j->n, 0);
  wchar_t notes[3][60] = {L"", L"", L""};
  rt_route(dbc, &o, rows, j, r->f[RC_ROUTE], 200, notes[0], 60);
  rt_preform(dbc, o.pfCard, rows, j, r, notes[1], 60);
  if (r->hasNorm) rt_fmt(r->norm1 * r->qtyTot, 3, r->f[RC_NORMTOT], 200);
  if (level && !qtyFound) lstrcpynW(notes[2], L"кол-во не найдено", 60);
  RtEl *el = (RtEl *)malloc(sizeof(RtEl) * 150);
  int ne = el ? rt_children(dbc, &o, rows, j, el, 150) : 0;
  /* вид изделия */
  const wchar_t *kind = o.section[0] ? o.section : (elSection && elSection[0] ? elSection : NULL);
  lstrcpynW(r->f[RC_KIND], kind ? kind : (ne > 0 ? L"Сборочные единицы" : L"Детали"), 200);
  /* сборке заготовка не нужна — «нет заготовки» у неё не пишем */
  if (ne > 0 && !wcscmp(notes[1], L"нет заготовки")) notes[1][0] = 0;
  for (int i = 0; i < 3; i++) {
    if (!notes[i][0]) continue;
    size_t l = wcslen(r->f[RC_NOTE]);
    _snwprintf(r->f[RC_NOTE] + l, 200 - l, L"%s%s", l ? L"; " : L"", notes[i]);
  }
  if (level >= RT_DEPTH) {
    free(el);
    return;
  }
  double tot = r->qtyTot;
  wchar_t myDes[200];
  lstrcpynW(myDes, o.des, 200);
  for (int i = 0; i < ne && !rt_late(j); i++)
    rt_walk(dbc, el[i].child, level + 1, myDes, el[i].qty, el[i].qtyFound, tot, el[i].section, rows, j);
  free(el);
}

/* найти изделие по обозначению: у кого есть карточки (техсостав, ТП, заготовка) */
static long rt_find_root(SQLHDBC dbc, const wchar_t *des, CardRow *rows, RtJob *j) {
  wchar_t pat[260], exact[260], sql[3600], err[280];
  /* сперва точное совпадение, потом — «содержит» */
  int e = 0;
  for (const wchar_t *c = des; *c && e < 250; c++) {
    if (*c == L'\'') exact[e++] = L'\'';
    exact[e++] = *c;
  }
  exact[e] = 0;
  like_escape(des, pat, 240);
  int n = 0;
  for (int pass = 0; pass < 3 && n <= 0; pass++) {
    if (pass == 0)
      _snwprintf(sql, 3600,
                 L"SELECT TOP 30 a.OwnerId, o.Name, N'', 0, 0 FROM InfoObjectAttributes AS a WITH(NOLOCK) "
                 L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'Designation' "
                 L"JOIN InfoObjects AS o WITH(NOLOCK) ON o.InfoObjectId=a.OwnerId AND o.Erased=0 "
                 L"WHERE a.Outdated=0 AND a.ShortText=N'%s' ORDER BY a.OwnerId DESC",
                 exact);
    else if (pass == 1)
      _snwprintf(sql, 3600,
                 L"SELECT TOP 30 a.OwnerId, o.Name, N'', 0, 0 FROM InfoObjectAttributes AS a WITH(NOLOCK) "
                 L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'Designation' "
                 L"JOIN InfoObjects AS o WITH(NOLOCK) ON o.InfoObjectId=a.OwnerId AND o.Erased=0 "
                 L"WHERE a.Outdated=0 AND a.ShortText LIKE N'%s' ESCAPE '\\' COLLATE Cyrillic_General_CI_AS "
                 L"ORDER BY a.OwnerId DESC",
                 pat);
    else /* обозначения атрибутом нет — по имени объекта */
      _snwprintf(sql, 3600,
                 L"SELECT TOP 30 o.InfoObjectId, o.Name, N'', 0, 0 FROM InfoObjects AS o WITH(NOLOCK) "
                 L"WHERE o.Erased=0 AND o.Name LIKE N'%s' ESCAPE '\\' COLLATE Cyrillic_General_CI_AS "
                 L"ORDER BY o.InfoObjectId DESC",
                 pat);
    sql[3599] = 0;
    n = card_query(dbc, sql, rows, 30, err, 280);
    if (n < 0) {
      _snwprintf(j->err, 400, L"Поиск по обозначению не выполнился: %s", err);
      return 0;
    }
  }
  if (n <= 0) {
    _snwprintf(j->err, 400, L"В PLM нет изделия с обозначением «%s».", des);
    return 0;
  }
  long cand[30];
  int nc = n;
  wchar_t list[800];
  size_t il = 0;
  list[0] = 0;
  for (int i = 0; i < nc; i++) {
    cand[i] = rows[i].n1;
    int w = _snwprintf(list + il, 800 - il, il ? L",%ld" : L"%ld", cand[i]);
    if (w > 0 && il + (size_t)w < 790) il += (size_t)w;
  }
  /* у кого больше карточек — то и изделие (а не документ или ТП с тем же номером) */
  _snwprintf(sql, 3600,
             L"SELECT TOP 100 x.Id, nkp.Value, N'', 0, 0 " PLM_HOLDERS(L"%s")
             L"AND nkp.Value IN (N'TechCompCard',N'TechnologicalProcessesCard',N'ProductPreformsCard')",
             list);
  sql[3599] = 0;
  int m = card_query(dbc, sql, rows, 100, err, 280);
  long best = cand[0];
  int bestScore = -1;
  for (int i = 0; i < nc; i++) {
    int sc = 0;
    for (int k = 0; k < m; k++)
      if (rows[k].n1 == cand[i]) sc += !_wcsicmp(rows[k].s1, L"TechCompCard") ? 3 : 1;
    if (sc > bestScore) {
      bestScore = sc;
      best = cand[i];
    }
  }
  rt_log(j, L"Найдено объектов с таким обозначением: %d, взят %ld (карточек %d)\r\n\r\n", nc, best,
         bestScore < 0 ? 0 : bestScore);
  return best;
}

/* весь сбор — в своём потоке; у раздающего — в его исполнителе */
static void rt_build(RtJob *j) {
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[280];
  if (!plm_connect(&env, &dbc, err, 280)) {
    _snwprintf(j->err, 400, L"Не удалось подключиться к PLM: %s", err);
    return;
  }
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 300);
  if (rows) {
    g_qTimeout = 15;
    long root = rt_find_root(dbc, j->des, rows, j);
    if (root) rt_walk(dbc, root, 0, j->order, 1, TRUE, 1, NULL, rows, j);
    g_qTimeout = 0;
    if (rt_late(j) && !(j->cancel && *j->cancel))
      rt_log(j, L"\r\nВремя вышло — собрано не всё (%d позиций).\r\n", j->n);
    free(rows);
  }
  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);
}

static RtJob *rt_job_new(void) {
  RtJob *j = (RtJob *)calloc(1, sizeof(RtJob));
  if (!j) return NULL;
  j->rows = (RtRow *)calloc(RT_MAX, sizeof(RtRow));
  j->log = (wchar_t *)calloc(RT_LOG, sizeof(wchar_t));
  if (!j->rows || !j->log) {
    free(j->rows);
    free(j->log);
    free(j);
    return NULL;
  }
  return j;
}

static void rt_job_free(RtJob *j) {
  if (!j) return;
  free(j->rows);
  free(j->log);
  free(j);
}

