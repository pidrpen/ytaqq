/* ---- Маршрутная ведомость из PLM → Excel ----------------------------------

   «Ещё» → «Маршрутная ведомость»: вписали обозначение сборки (или детали),
   «Собрать» — курсор проходит по PLM её состав вглубь и складывает таблицу
   теми же столбцами, что «Ведомость маршрутная КЗ …» (pidrpen/wowdroch):

     Обозначение · Наименование · Входит в · Вид изделия · Количество, шт ·
     Количество на изд., шт · Материал · Сортамент заготовки · Технологич.
     припуск · Масса, кг · Н. расх. на 1 деталь · Н. расх. на изделие ·
     Ед. Изм. нормы · Технологический маршрут · Примечание

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
       участок (Area) операции или её TSOperation, нет участка — цех
       (WorkShop); из названия — номер.
   Чего не нашлось — пишется в «Примечание», а весь проход — в «Подробности»:
   его можно прислать, если где-то пусто. Через компьютер коллеги
   (раздача PLM) — тот же сбор у него, ответ целиком. */

#include "xlsx_write.c"

#define WM_RT_PROGRESS (WM_APP + 61)
#define WM_RT_DONE (WM_APP + 62) /* lParam — RtJob*, освобождает получатель */
#define RT_MAX 1000 /* с 2026.09.23.78 — и для выгрузки из PLM (export.c): большие сборки */
#define RT_DEPTH 8
#define RT_NCOL 15
#define RT_LOG 500000 /* с проверочной выгрузкой (2026.09.23.59) — до полумиллиона знаков */

typedef struct {
  long id;
  int level;
  wchar_t f[RT_NCOL][200]; /* столбцы ведомости, как в файле */
  double qty, qtyTot, norm1;
  BOOL hasNorm;
  /* связи позиции в PLM — для выгрузки (export.c): листы операций, материалов,
     извещений берут их отсюда, а не ищут заново. 0 — не нашлось */
  long tp, tpVer, tpVar; /* основной ТП, его версия и вариант с операциями */
  long par, prodConf;    /* версия изделия (родитель) и исполнение */
  long tcVariant;        /* вариант техсостава этого исполнения */
  long tcCard;           /* карточка техсостава — её версии бывают в извещениях */
  long pfCard;           /* карточка заготовок — лист «Заготовки» */
  wchar_t tpName[200];
} RtRow;

typedef struct {
  wchar_t des[200], order[120];
  long rootId; /* строка, выбранная в поиске PLM: искать изделие не нужно */
  int n;
  RtRow *rows;
  wchar_t *log;
  int logLen;
  wchar_t err[400];
  ULONGLONG deadline;
  HWND notify;
  volatile LONG *cancel;
  BOOL keysShown; /* поля строки техсостава — в подробности один раз */
  BOOL opsShown;  /* поля операции без цеха — тоже один раз */
  BOOL pfShown;   /* поля заготовки — один раз */
  int diagShown;  /* связи изделия без ТП и заготовки: 1 — сборки, 2 — детали */
  BOOL dump;       /* «Выгрузка для проверки»: всё найденное — в подробности */
  int dumpDet, dumpAsm;
  long lastTp, lastTpVer, lastTpVar; /* техпроцесс последней позиции: для выгрузки */
  int kdUsed; /* у скольких сборок состав взят по КД — техсостава нет */
  BOOL tcDiag; /* почему техсостав не нашёлся — выписано один раз */
  /* параллельный обход (с 2026.09.23.81, глубже — с .83): верхние уровни —
     здесь, ветки ниже — задачи потокам; plan — порядок строк для сшивки */
  BOOL split;             /* раздавать ветки, а не спускаться самому */
  int splitDepth;         /* с какого уровня ветки — задачи: 1 или 2 */
  void *tasks;            /* RtTask[ntask] */
  int ntask;
  int *plan;              /* ≥0 — строка rows; <0 — задача −(k+1) */
  int nplan;
  volatile LONG *shared;  /* общий счётчик позиций — для строки состояния */
  void *memo;             /* RtMemo[nmemo] — уже собранные позиции (с .83) */
  int nmemo;
  wchar_t lastTpName[200];
  ULONGLONG t0;
} RtJob;

enum { RC_DES, RC_NAME, RC_PARENT, RC_KIND, RC_QTY, RC_QTYTOT, RC_MAT, RC_SORT, RC_ALLOW, RC_MASS, RC_NORM1,
       RC_NORMTOT, RC_UNIT, RC_ROUTE, RC_NOTE };
static const wchar_t *const kRtHead[RT_NCOL] = {
    L"Обозначение",        L"Наименование",         L"Входит в",
    L"Вид изделия",        L"Количество, шт",       L"Количество на изд., шт",
    L"Материал",           L"Сортамент заготовки",  L"Технологич. припуск",
    L"Масса, кг",          L"Н. расх. на 1 деталь", L"Н. расх. на изделие",
    L"Ед. Изм. нормы", L"Технологический маршрут", L"Примечание"};
static const int kRtWidth[RT_NCOL] = {26, 30, 24, 16, 10, 12, 40, 20, 10, 10, 12, 12, 10, 26, 30};
/* ширины в Excel — под шрифт 14 и печать A3 в ширину одной страницы: уже,
   текст переносится по словам; не уже самого длинного слова шапки */
static const int kRtXlWidth[RT_NCOL] = {24, 26, 24, 14, 12, 12, 26, 16, 13, 9, 10, 10, 9, 17, 20};

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
  L"AND nhc.Value IN (N'ProductConfiguration',N'BaselineConfiguration') "                   \
  L"WHERE hc.OwnerId=x.Id AND hc.Outdated=0 AND ISNULL(hc.Link,0)<>0 "                     \
  L"UNION SELECT hp.Link, 3 FROM InfoObjectAttributes AS hp WITH(NOLOCK) "                  \
  L"JOIN NameKeys AS nhp WITH(NOLOCK) ON nhp.NameKeyId=hp.NameKeyId "                       \
  L"AND nhp.Value IN (N'ProductConfiguration',N'BaselineConfiguration') "                   \
  L"WHERE hp.OwnerId=x.Par AND hp.Outdated=0 AND ISNULL(hp.Link,0)<>0) AS h1 "             \
  L"CROSS APPLY (SELECT h1.H AS H, h1.P AS P UNION SELECT hi.Link, h1.P + 1 "               \
  L"FROM InfoObjectAttributes AS hi WITH(NOLOCK) "                                          \
  L"JOIN NameKeys AS nhi WITH(NOLOCK) ON nhi.NameKeyId=hi.NameKeyId "                       \
  L"AND nhi.Value=N'ProductInterconnectCard' "                                              \
  L"WHERE hi.OwnerId=h1.H AND hi.Outdated=0 AND ISNULL(hi.Link,0)<>0) AS h2 "              \
  L"JOIN InfoObjectAttributes AS a WITH(NOLOCK) ON a.OwnerId=h2.H AND a.Outdated=0 "        \
  L"AND ISNULL(a.CollectionElementId,0)=0 "                                                 \
  L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "

typedef struct {
  wchar_t des[200], name[260], objName[260], section[120], mass[64], massUnit[40], mat[200];
  long tpCard, pfCard, tcCard, prodConf;
  long prodConfObj; /* исполнение по ссылкам самого изделия — если prodConf взят из строки техсостава */
  BOOL desAttr; /* обозначение — свой атрибут, а не из имени */
  long id, par;
} RtObj;

static BOOL rt_looks_des(const wchar_t *s);

static BOOL rt_obj(SQLHDBC dbc, long id, RtObj *o, CardRow *rows, RtJob *j) {
  memset(o, 0, sizeof(*o));
  wchar_t sql[3600], err[280];
  _snwprintf(sql, 3600, L"SELECT TOP 1 o.InfoObjectId, o.Name, N'', ISNULL(o.ParentId,0), 0 FROM InfoObjects AS o WITH(NOLOCK) "
                        L"WHERE o.InfoObjectId=%ld",
             id);
  if (card_query(dbc, sql, rows, 2, err, 280) > 0) {
    lstrcpynW(o->objName, rows[0].s1, 260);
    o->par = rows[0].n2;
  }
  wchar_t ids[24];
  _snwprintf(ids, 24, L"%ld", id);
  _snwprintf(sql, 3600,
             L"SELECT TOP 80 h2.P, nk.Value, COALESCE(lo.Name, " CARD_VALUE_SQL L", N''), ISNULL(a.Link,0), "
             L"a.DataType " RT_HOLDERS(L"%s")
             L"AND nk.Value IN (N'Designation',N'Name',N'Section',N'Mass',N'MassMeasureUnit',"
             L"N'TechnologicalProcessesCard',N'ProductPreformsCard',N'TechCompCard',N'ProductConfiguration',"
             L"N'BaselineConfiguration',N'Material',N'MaterialLink') "
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
  long desP = 0;
  for (int i = 0; i < n; i++) {
    const wchar_t *k = rows[i].s1, *v = rows[i].s2;
    long link = rows[i].n2;
    if (!_wcsicmp(k, L"Designation") && !o->des[0] && v[0]) {
      lstrcpynW(o->des, v, 200);
      o->desAttr = TRUE;
      desP = rows[i].n1;
    }
    else if ((!_wcsicmp(k, L"Material") || !_wcsicmp(k, L"MaterialLink")) && !o->mat[0] && v[0])
      lstrcpynW(o->mat, v, 200);
    else if (!_wcsicmp(k, L"Name") && !o->name[0] && v[0]) lstrcpynW(o->name, v, 260);
    else if (!_wcsicmp(k, L"Section") && !o->section[0] && v[0]) lstrcpynW(o->section, v, 120);
    else if (!_wcsicmp(k, L"Mass") && !o->mass[0] && v[0]) lstrcpynW(o->mass, v, 64);
    else if (!_wcsicmp(k, L"MassMeasureUnit") && !o->massUnit[0] && v[0]) lstrcpynW(o->massUnit, v, 40);
    else if (!_wcsicmp(k, L"TechnologicalProcessesCard") && !o->tpCard) o->tpCard = link;
    else if (!_wcsicmp(k, L"ProductPreformsCard") && !o->pfCard) o->pfCard = link;
    else if (!_wcsicmp(k, L"TechCompCard") && !o->tcCard) o->tcCard = link;
    else if (!_wcsicmp(k, L"ProductConfiguration") && !o->prodConf) o->prodConf = link;
    else if (!_wcsicmp(k, L"BaselineConfiguration") && !o->prodConf) o->prodConf = link;
  }
  o->id = id;
  /* обозначение не своё, а родителя или карты («АДЕ … 00.02» у сборки 00.00,
     «01.00СБ» у кронштейна) — своё имя объекта точнее, если в нём оно есть */
  if (desP > 0 && o->objName[0]) {
    wchar_t nd[200];
    card_des_from_name(o->objName, nd, 200);
    if (rt_looks_des(nd) && _wcsicmp(nd, o->des)) {
      rt_log(j, L"    обозначение «%s» — из связанного объекта, по имени «%s»\r\n", o->des, nd);
      lstrcpynW(o->des, nd, 200);
    }
  }
  /* карточки — ещё и тем же запросом, что у карточки и столбца «Заготовка»
     в поиске (PLM_HOLDERS): он на этой базе проверен */
  if (!o->pfCard || !o->tcCard || !o->tpCard || !o->prodConf) {
    _snwprintf(sql, 3600,
               L"SELECT DISTINCT TOP 20 pa.Link, nkp.Value, N'', 0, 0 " PLM_HOLDERS(L"%s")
               L"AND nkp.Value IN (N'ProductPreformsCard',N'TechCompCard',N'ProductConfiguration',"
               L"N'TechnologicalProcessesCard')",
               ids);
    sql[3599] = 0;
    int m = card_query(dbc, sql, rows, 20, err, 280);
    for (int i = 0; i < m; i++) {
      const wchar_t *k = rows[i].s1;
      if (!_wcsicmp(k, L"ProductPreformsCard") && !o->pfCard) o->pfCard = rows[i].n1;
      else if (!_wcsicmp(k, L"TechCompCard") && !o->tcCard) o->tcCard = rows[i].n1;
      else if (!_wcsicmp(k, L"ProductConfiguration") && !o->prodConf) o->prodConf = rows[i].n1;
      else if (!_wcsicmp(k, L"TechnologicalProcessesCard") && !o->tpCard) o->tpCard = rows[i].n1;
    }
    if (m < 0) rt_log(j, L"    карточки %ld: запрос не выполнился — %s\r\n", id, err);
  }
  /* карта взаимосвязей сама ссылается на конфигурацию (Product) — ищем с её
     стороны: так находятся карточки, когда у конфигурации ссылки на карту нет
     или карт несколько */
  if (!o->pfCard || !o->tpCard) {
    _snwprintf(sql, 3600,
               L"SELECT DISTINCT TOP 20 ca.Link, nkc.Value, N'', 0, 0 "
               L"FROM InfoObjectAttributes AS pr WITH(NOLOCK) "
               L"JOIN NameKeys AS nkpr WITH(NOLOCK) ON nkpr.NameKeyId=pr.NameKeyId AND nkpr.Value=N'Product' "
               L"JOIN InfoObjectAttributes AS ca WITH(NOLOCK) ON ca.OwnerId=pr.OwnerId AND ca.Outdated=0 "
               L"AND ISNULL(ca.Link,0)<>0 AND ISNULL(ca.CollectionElementId,0)=0 "
               L"JOIN NameKeys AS nkc WITH(NOLOCK) ON nkc.NameKeyId=ca.NameKeyId "
               L"AND nkc.Value IN (N'ProductPreformsCard',N'TechCompCard',N'TechnologicalProcessesCard') "
               L"WHERE pr.Link IN (%ld,%ld,%ld) AND pr.Outdated=0 AND ISNULL(pr.CollectionElementId,0)=0",
               id, o->prodConf ? o->prodConf : id, o->par ? o->par : id);
    sql[3599] = 0;
    int m = card_query(dbc, sql, rows, 20, err, 280);
    int got = 0;
    for (int i = 0; i < m; i++) {
      const wchar_t *k = rows[i].s1;
      if (!_wcsicmp(k, L"ProductPreformsCard") && !o->pfCard) o->pfCard = rows[i].n1, got++;
      else if (!_wcsicmp(k, L"TechCompCard") && !o->tcCard) o->tcCard = rows[i].n1, got++;
      else if (!_wcsicmp(k, L"TechnologicalProcessesCard") && !o->tpCard) o->tpCard = rows[i].n1, got++;
    }
    if (got) rt_log(j, L"    карточки %ld: нашлись по обратной ссылке (Product)\r\n", id);
    if (m < 0) rt_log(j, L"    карточки %ld по Product: запрос не выполнился — %s\r\n", id, err);
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
  wchar_t sql[4600], err[280]; /* запрос операций с участком и номером — около 3600 знаков */
  long tp = 0, ver = 0;
  j->lastTp = j->lastTpVer = j->lastTpVar = 0;
  j->lastTpName[0] = 0;
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
      lstrcpynW(j->lastTpName, rows[0].s1, 200);
      rt_log(j, L"    ТП: %s (%ld)%s\r\n", rows[0].s1, tp, _wcsicmp(rows[0].s2, L"да") ? L", не помечен основным" : L"");
    }
  }
  if (!tp) { /* техпроцесс ссылается на изделие (ManufacturedProducts) — одиночно или списком */
    _snwprintf(sql, 3600,
               L"SELECT TOP 10 tp.InfoObjectId, tp.Name, ISNULL(flag.V,N'нет'), ISNULL(av.L,0), 0 "
               L"FROM (SELECT a.OwnerId AS T FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'ManufacturedProducts' "
               L"WHERE a.Link IN (%ld,%ld,%ld) AND a.Outdated=0 "
               L"UNION SELECT la.OwnerId FROM InfoObjectAttributes AS ea WITH(NOLOCK) "
               L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
               L"ON ce.CollectionElementId=ea.CollectionElementId AND ce.Outdated=0 "
               L"JOIN InfoObjectAttributes AS la WITH(NOLOCK) ON la.AttributeId=ce.AttributeId "
               L"JOIN NameKeys AS nkl WITH(NOLOCK) ON nkl.NameKeyId=la.NameKeyId AND nkl.Value=N'ManufacturedProducts' "
               L"WHERE ea.Link IN (%ld,%ld,%ld) AND ea.DataType=6 AND ea.Outdated=0) AS m "
               L"JOIN InfoObjects AS tp WITH(NOLOCK) ON tp.InfoObjectId=m.T AND tp.Erased=0 "
               L"CROSS APPLY (SELECT TOP 1 ISNULL(iv.Link,0) AS L FROM InfoObjectAttributes AS iv WITH(NOLOCK) "
               L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=iv.NameKeyId AND nkv.Value=N'ActualVersion' "
               L"WHERE iv.OwnerId=tp.InfoObjectId AND iv.Outdated=0) AS av "
               L"OUTER APPLY (SELECT TOP 1 N'да' AS V FROM InfoObjectAttributes AS ia WITH(NOLOCK) "
               L"JOIN NameKeys AS nki WITH(NOLOCK) ON nki.NameKeyId=ia.NameKeyId "
               L"WHERE ia.OwnerId=tp.InfoObjectId AND ia.Outdated=0 "
               L"AND nki.Value IN (N'MainTP',N'IsActual') AND ia.BoolValue=1) AS flag "
               L"WHERE av.L>0 ORDER BY CASE WHEN flag.V=N'да' THEN 0 ELSE 1 END, tp.InfoObjectId",
               /* оба исполнения: из строки техсостава и по ссылкам изделия */
               o->id, o->prodConf ? o->prodConf : o->id, o->prodConfObj ? o->prodConfObj : o->id, o->id,
               o->prodConf ? o->prodConf : o->id, o->prodConfObj ? o->prodConfObj : o->id);
    int n = card_query(dbc, sql, rows, 10, err, 280);
    if (n > 0) {
      tp = rows[0].n1;
      ver = rows[0].n2;
      lstrcpynW(j->lastTpName, rows[0].s1, 200);
      rt_log(j, L"    ТП по ссылке из техпроцесса: %s (%ld)\r\n", rows[0].s1, tp);
    } else if (n < 0) {
      rt_log(j, L"    ТП по ссылке: запрос не выполнился — %s\r\n", err);
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
  j->lastTp = tp;
  j->lastTpVer = ver;
  j->lastTpVar = parent;
  /* ТП найден по обозначению — имени из поиска нет: берём по id (с 2026.09.23.81;
     прежде столбец «Техпроцесс» был пуст, а маршрут из этого ТП — был) */
  if (!j->lastTpName[0]) {
    _snwprintf(sql, 3600, L"SELECT TOP 1 o.InfoObjectId, CAST(o.Name AS NVARCHAR(250)), N'', 0, 0 "
                          L"FROM InfoObjects AS o WITH(NOLOCK) WHERE o.InfoObjectId=%ld", tp);
    if (card_query(dbc, sql, rows, 1, err, 280) > 0) lstrcpynW(j->lastTpName, rows[0].s1, 200);
  }
  /* операции по порядку ТП и участок каждой: Area (с 2026.09.23.49 — прежде
     цеха: в ведомости маршрут по участкам), нет — WorkShop; своё или у TSOperation */
  _snwprintf(sql, 4600,
             L"SELECT TOP 200 ch.InfoObjectId, ISNULL(ws.V,N''), ch.Name, ISNULL(num.N,0), 0 "
             L"FROM InfoObjects AS ch WITH(NOLOCK) "
             L"OUTER APPLY (SELECT TOP 1 ts.Link AS L FROM InfoObjectAttributes AS ts WITH(NOLOCK) "
             L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=ts.NameKeyId AND nkt.Value=N'TSOperation' "
             L"WHERE ts.OwnerId=ch.InfoObjectId AND ts.Outdated=0) AS op "
             L"OUTER APPLY (SELECT TOP 1 COALESCE(lo.Name, " CARD_VALUE_SQL L", N'') AS V "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nkw WITH(NOLOCK) ON nkw.NameKeyId=a.NameKeyId AND nkw.Value IN (N'WorkShop',N'Area') "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE a.OwnerId IN (ch.InfoObjectId, ISNULL(op.L,0)) AND a.Outdated=0 "
             L"AND NULLIF(COALESCE(lo.Name, " CARD_VALUE_SQL L", N''), N'') IS NOT NULL "
             L"ORDER BY CASE WHEN nkw.Value=N'Area' THEN 0 ELSE 1 END, "
             L"CASE WHEN a.OwnerId=ch.InfoObjectId THEN 0 ELSE 1 END) AS ws "
             /* порядок — по номеру операции (строка «005»), как в ТП: OP_NUM_APPLY */
             OP_NUM_APPLY(L"ch.InfoObjectId")
             L"WHERE ch.ParentId=%ld AND ch.Erased=0 ORDER BY " OP_NUM_ORDER L", ch.InfoObjectId",
             parent);
  sql[4599] = 0;
  int n = card_query(dbc, sql, rows, 200, err, 280);
  if (n < 0) {
    lstrcpynW(note, L"маршрут не прочитался", ncap);
    rt_log(j, L"    операции: запрос не выполнился — %s\r\n", err);
    return;
  }
  int used = 0, blank = 0;
  wchar_t last[40] = L"";
  for (int i = 0; i < n; i++) {
    if (!rows[i].s1[0]) {
      blank++;
      continue;
    }
    wchar_t code[40];
    rt_ws_code(rows[i].s1, code, 40);
    if (!code[0]) continue;
    if (!wcscmp(code, last)) { /* тот же участок подряд — один раз: «31;31;12» → «31;12» */
      used++;
      continue;
    }
    lstrcpynW(last, code, 40);
    size_t l = wcslen(out);
    if (l + wcslen(code) + 2 >= (size_t)cap) break;
    _snwprintf(out + l, cap - l, L"%s%s", l ? L";" : L"", code);
    if (!used) rt_log(j, L"    участок первой операции: «%s» → %s\r\n", rows[i].s1, code);
    used++;
  }
  rt_log(j, L"    операций %d, с участком или цехом %d\r\n", n, used);
  if (!used && n > 0 && !j->opsShown) {
    j->opsShown = TRUE;
    long op0 = rows[0].n1;
    _snwprintf(sql, 3600,
               L"SELECT TOP 60 a.OwnerId, nk.Value, COALESCE(lo.Name, " CARD_VALUE_SQL L", N''), 0, a.DataType "
               L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
               L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
               L"WHERE a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0 AND a.OwnerId IN (%ld, "
               L"ISNULL((SELECT TOP 1 ts.Link FROM InfoObjectAttributes AS ts WITH(NOLOCK) "
               L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=ts.NameKeyId AND nkt.Value=N'TSOperation' "
               L"WHERE ts.OwnerId=%ld AND ts.Outdated=0),0)) ORDER BY a.OwnerId, nk.Value",
               op0, op0);
    sql[3599] = 0;
    int k2 = card_query(dbc, sql, rows, 60, err, 280);
    rt_log(j, L"    поля операции %ld (участка и цеха в них не нашлось):", op0);
    for (int i = 0; i < k2; i++) rt_log(j, L" %s=%s", rows[i].s1, rows[i].s2[0] ? rows[i].s2 : L"·");
    rt_log(j, L"\r\n");
  }
  if (!used) lstrcpynW(note, n ? L"у операций нет участка" : L"в ТП нет операций", ncap);
  (void)blank;
}

/* ---- общее чтение полей объектов PLM (и для выгрузки, export.c) ---- */

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
        if (f[i].n3 % 1000 == 23) continue; /* составное поле само значения не держит — только его строки */
        if (!_wcsicmp(f[i].s1, p)) return f[i].s2;
      }
    }
  }
  return NULL;
}

/* заготовка: материал, сортамент, припуск, норма (масса заготовки) */
/* Заготовка (как в выгрузке PlmApi: ProductPreformsCard → вложенные
   ProductPreform). С 2026.09.23.62 — по связям самой заготовки:
     • какая: в карточке их бывает несколько (для разных версий изделия) —
       берётся та, у которой ProductVersionConfiguration = это изделие,
       потом Product = изделие или его конфигурация, потом самая новая;
       раньше бралась первая по номеру — бывала от старой версии;
     • материал — MaterialName заготовки («Круг 40 ГОСТ… / 38ХС ГОСТ…»);
     • сортамент и припуск — PreformSize: он составной, в его строке поля
       размеров (ZDiametr, ZLength…) и ZSizeAdd — читаются все поля строки,
       а не только Value; в «Подробностях» они выписаны как есть;
     • норма — PreformExpense (его строка, поле Value или масса);
   чего так не нашлось — как раньше, обходом заготовки (pf_explore). */
/* Одна заготовка (ProductPreform) — как её читают заводские выгрузки PLM:
     материал — MaterialName (заготовка из изделия — PreformConfigurationName);
     сортамент — PreformSize.DisplayedName (так PLM пишет «Сортамент заготовки»),
       нет — собирается: профиль (PreformSize.Profile) Ø / толщина×ширина, L=длина;
     размеры — ZDiametr, ZThickness, ZWidth, ZLength: каждый — составной (Value и
       единица), то есть на два уровня вглубь: PreformSize → ZDiametr → Value
       (до 2026.09.23.81 читался один уровень — размеров не было, и сортамент
       выходил из названия материала: «Круг 20» без длины);
     припуск — ZSizeAdd тем же путём; норма — PreformExpense: Value и
       ComputingUnit (единица — из PLM, не всегда кг). */
typedef struct {
  wchar_t mat[400], from[260], sort[200], dims[160], profile[80], add[40], unit[40];
  double normV;
  BOOL hasNorm;
} RtPf;

#define RT_PF_OWN L"N'MaterialName',N'PreformConfigurationName',N'ZSizeAdd',N'ZDiametr',N'ZLength'"
#define RT_PF_COLL L"N'PreformSize',N'PreformExpense'"

static void rt_pf_read(SQLHDBC dbc, long pfId, RtPf *pf, RtJob *j) {
  memset(pf, 0, sizeof(*pf));
  CardRow *f = (CardRow *)malloc(sizeof(CardRow) * 200);
  if (!f) return;
  wchar_t ids[24], err[280];
  _snwprintf(ids, 24, L"%ld", pfId);
  int nf = xp_fields(dbc, ids, RT_PF_OWN, RT_PF_COLL, f, 200, err);
  if (nf < 0) {
    rt_log(j, L"    поля заготовки #%ld: запрос не выполнился — %s\r\n", pfId, err);
    nf = 0;
  }
  const wchar_t *v;
  if ((v = xp_get(f, nf, pfId, 0, 0, L"MaterialName"))) lstrcpynW(pf->mat, v, 400);
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformConfigurationName"))) lstrcpynW(pf->from, v, 260);
  if (!pf->mat[0] && pf->from[0]) lstrcpynW(pf->mat, pf->from, 400);
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformSize|Profile"))) lstrcpynW(pf->profile, v, 80);
  wchar_t d[40] = L"", th[40] = L"", wd[40] = L"", len[40] = L"";
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformSize|ZDiametr|Value;PreformSize|ZDiameter|Value;PreformSize|ZDiametr;ZDiametr")))
    pf_num(v, d, 40);
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformSize|ZThickness|Value;PreformSize|ZThickness"))) pf_num(v, th, 40);
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformSize|ZWidth|Value;PreformSize|ZWidth"))) pf_num(v, wd, 40);
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformSize|ZLength|Value;PreformSize|ZLength;ZLength"))) pf_num(v, len, 40);
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformSize|ZSizeAdd|Value;PreformSize|ZSizeAdd;ZSizeAdd"))) pf_num(v, pf->add, 40);
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformExpense|Value")) && rt_num(v, &pf->normV)) pf->hasNorm = TRUE;
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformExpense|ComputingUnit"))) lstrcpynW(pf->unit, v, 40);
  /* размеры одной строкой: «Ø20 L=150», «4×100 L=200» */
  int w = 0;
  if (d[0]) w += _snwprintf(pf->dims + w, 160 - w, L"Ø%s", d);
  if (th[0] && w >= 0 && w < 150) w += _snwprintf(pf->dims + w, 160 - w, L"%s%s", w ? L" " : L"", th);
  if (wd[0] && w >= 0 && w < 150) w += _snwprintf(pf->dims + w, 160 - w, L"%s%s", th[0] ? L"×" : (w ? L" " : L""), wd);
  if (len[0] && w >= 0 && w < 150) _snwprintf(pf->dims + w, 160 - w, L"%sL=%s", w ? L" " : L"", len);
  pf->dims[159] = 0;
  /* сортамент — как в PLM; нет — профиль и размеры; нет и их — вид и размер из материала */
  if ((v = xp_get(f, nf, pfId, 0, 0, L"PreformSize|DisplayedName"))) lstrcpynW(pf->sort, v, 200);
  if (!pf->sort[0] && pf->dims[0]) {
    wchar_t kind[80] = L"";
    if (pf->profile[0]) lstrcpynW(kind, pf->profile, 80);
    else
      for (int q = 0; pf->mat[q] && pf->mat[q] != L' ' && q < 79; q++) kind[q] = pf->mat[q], kind[q + 1] = 0;
    _snwprintf(pf->sort, 200, L"%s%s%s", kind, kind[0] ? L" " : L"", pf->dims);
    pf->sort[199] = 0;
  }
  if (!pf->sort[0] && pf->mat[0]) { /* «Лист 36 ГОСТ 19903-2015 / 45 …» → «Лист 36» */
    wchar_t head[200];
    lstrcpynW(head, pf->mat, 200);
    wchar_t *cut = wcsstr(head, L" / ");
    if (cut) *cut = 0;
    static const wchar_t *const std[] = {L" ГОСТ", L" ТУ ", L" ОСТ", L" СТО", L" DIN", L" ISO"};
    for (size_t i = 0; i < sizeof(std) / sizeof(std[0]); i++) {
      wchar_t *g = wcsstr(head, std[i]);
      if (g) *g = 0;
    }
    size_t hl = wcslen(head);
    while (hl && head[hl - 1] == L' ') head[--hl] = 0;
    BOOL digit = FALSE;
    for (wchar_t *c = head; *c && !digit; c++) digit = iswdigit(*c);
    if (digit) lstrcpynW(pf->sort, head, 200);
  }
  if (j && !j->pfShown && nf > 0) { /* поля первой заготовки — как пришли: для проверки */
    j->pfShown = TRUE;
    rt_log(j, L"    поля заготовки #%ld (для проверки):", pfId);
    for (int i = 0; i < nf && i < 60; i++)
      if (f[i].n3 % 1000 != 23) rt_log(j, L" %s=%s%s", f[i].s1, f[i].s2, f[i].n3 >= 1000 ? L"(устар.)" : L"");
    rt_log(j, L"\r\n");
  }
  free(f);
}


static void rt_preform(SQLHDBC dbc, const RtObj *o, CardRow *rows, RtJob *j, RtRow *r, wchar_t *note, int ncap) {
  long pfCard = o->pfCard;
  if (!pfCard) {
    lstrcpynW(note, L"нет заготовки", ncap);
    return;
  }
  wchar_t sql[3600], err[280], card[24];
  _snwprintf(card, 24, L"%ld", pfCard);
  long conf = o->prodConf ? o->prodConf : o->id;
  _snwprintf(sql, 3600,
             L"SELECT TOP 10 pf.PfId, pf.PfName, ISNULL(mn.V,N''), ISNULL(pvc.L,0), ISNULL(pr.L,0) "
             L"FROM (SELECT %s AS CardId) AS k " PF_APPLY(L"k.CardId")
             L"OUTER APPLY (SELECT TOP 1 a.Link AS L FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'ProductVersionConfiguration' "
             L"WHERE a.OwnerId=pf.PfId AND a.Outdated=0) AS pvc "
             L"OUTER APPLY (SELECT TOP 1 a.Link AS L FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'Product' "
             L"WHERE a.OwnerId=pf.PfId AND a.Outdated=0) AS pr "
             L"OUTER APPLY (SELECT TOP 1 CAST(a.ShortText AS NVARCHAR(400)) AS V FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'MaterialName' "
             L"WHERE a.OwnerId=pf.PfId AND a.Outdated=0) AS mn "
             L"ORDER BY CASE WHEN pvc.L=%ld THEN 0 WHEN pr.L IN (%ld,%ld) THEN 1 ELSE 2 END, pf.PfId DESC",
             card, o->id, o->id, conf);
  sql[3599] = 0;
  int n = card_query(dbc, sql, rows, 10, err, 280);
  if (n <= 0) {
    lstrcpynW(note, L"нет заготовки", ncap);
    if (n < 0) rt_log(j, L"    заготовки карточки %ld: запрос не выполнился — %s\r\n", pfCard, err);
    else rt_log(j, L"    заготовки в карточке %ld нет\r\n", pfCard);
    return;
  }
  long pf0 = rows[0].n1;
  wchar_t pfName[260], matName[400];
  lstrcpynW(pfName, rows[0].s1, 260);
  lstrcpynW(matName, rows[0].s2, 400);
  if (n > 1)
    rt_log(j, L"    заготовок в карточке %d, взята #%ld (%s)\r\n", n, pf0,
           rows[0].n2 == o->id ? L"этой версии изделия"
           : (rows[0].n3 == o->id || rows[0].n3 == conf) ? L"этого изделия"
                                                         : L"самая новая");
  RtPf pf;
  rt_pf_read(dbc, pf0, &pf, j);
  if (!pf.mat[0]) lstrcpynW(pf.mat, matName, 400);
  lstrcpynW(r->f[RC_MAT], pf.mat, 200);
  lstrcpynW(r->f[RC_SORT], pf.sort, 200);
  if (pf.add[0]) lstrcpynW(r->f[RC_ALLOW], pf.add, 200);
  if (pf.hasNorm) {
    r->norm1 = pf.normV;
    r->hasNorm = TRUE;
    rt_fmt(pf.normV, 3, r->f[RC_NORM1], 200);
    lstrcpynW(r->f[RC_UNIT], pf.unit[0] ? pf.unit : L"кг", 200);
  }
  rt_log(j, L"    заготовка %s: %s · %s · припуск %s · норма %s %s\r\n", pfName, pf.mat[0] ? pf.mat : L"—",
         pf.sort[0] ? pf.sort : L"—", pf.add[0] ? pf.add : L"—", r->f[RC_NORM1][0] ? r->f[RC_NORM1] : L"—",
         r->f[RC_UNIT]);
  if (!pf.mat[0] && !r->hasNorm) lstrcpynW(note, L"в заготовке нет материала и нормы", ncap);
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
  /* с 2026.09.23.76 — по коду модуля «Технологический состав» PLM «Союз»
     (pidrpen/cursor): строка ссылается на исполнение входящего изделия
     (ProductConfiguration), порядок строк в редакторе — SortedPosition,
     единица количества — MeasureUnitOfQuaintity (так, с опечаткой, в PLM) */
  long pc;
  double sortPos;
  BOOL hasSort;
  wchar_t unit[40];
  wchar_t kd[160]; /* непринятое изменение КД по этой строке — в примечание */
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

/* строки TechComposition вариантов версии ver: n1 — строка, n2 — вариант;
   prodConf — только вариант этой конфигурации (0 — любой) */
static int rt_tc_rows(SQLHDBC dbc, long ver, long prodConf, CardRow *rows, wchar_t *sql, wchar_t *err) {
  _snwprintf(sql, 4000,
             L"SELECT DISTINCT TOP 300 ce.CollectionElementId, N'', N'', ch.InfoObjectId, 0 "
             L"FROM InfoObjects AS ch WITH(NOLOCK) "
             L"JOIN InfoObjectAttributes AS tc WITH(NOLOCK) ON tc.OwnerId=ch.InfoObjectId AND tc.Outdated=0 "
             L"JOIN NameKeys AS nktc WITH(NOLOCK) ON nktc.NameKeyId=tc.NameKeyId AND nktc.Value=N'TechComposition' "
             /* строки списка — свои или того списка, на который он ссылается (Link):
                техсостав модуль заводит копией состава КД (SetValue(Items)), и пока его
                не правили, своих строк у него нет — они у Items (с 2026.09.23.82; так же
                PLM читает и составные — PreformSize) */
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId IN (tc.AttributeId, ISNULL(tc.Link,0)) "
             L"AND ce.Outdated=0 "
             L"WHERE ch.ParentId=%ld AND ch.Erased=0 "
             L"AND (%ld=0 OR EXISTS (SELECT 1 FROM InfoObjectAttributes AS pr WITH(NOLOCK) "
             L"JOIN NameKeys AS nkpr WITH(NOLOCK) ON nkpr.NameKeyId=pr.NameKeyId AND nkpr.Value=N'Product' "
             L"WHERE pr.OwnerId=ch.InfoObjectId AND pr.Outdated=0 AND pr.Link=%ld)) "
             L"AND NOT EXISTS (SELECT 1 FROM InfoObjectAttributes AS ir WITH(NOLOCK) "
             L"JOIN NameKeys AS nkr WITH(NOLOCK) ON nkr.NameKeyId=ir.NameKeyId AND nkr.Value=N'IsRemoved' "
             L"WHERE ir.CollectionElementId=ce.CollectionElementId AND ir.BoolValue=1) "
             L"ORDER BY ce.CollectionElementId",
             ver, prodConf, prodConf);
  return card_query(dbc, sql, rows, 300, err, 280);
}

/* строки конструкторского состава (Items) ИИВ — когда техсостава нет */
static int rt_kd_rows(SQLHDBC dbc, long pvc, CardRow *rows, wchar_t *sql, wchar_t *err) {
  _snwprintf(sql, 4000,
             L"SELECT DISTINCT TOP 300 ce.CollectionElementId, N'', N'', 0, 0 "
             L"FROM InfoObjectAttributes AS it WITH(NOLOCK) "
             L"JOIN NameKeys AS nki WITH(NOLOCK) ON nki.NameKeyId=it.NameKeyId AND nki.Value=N'Items' "
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId IN (it.AttributeId, ISNULL(it.Link,0)) "
             L"AND ce.Outdated=0 "
             L"WHERE it.OwnerId=%ld AND it.Outdated=0 AND ISNULL(it.CollectionElementId,0)=0 "
             L"ORDER BY ce.CollectionElementId",
             pvc);
  return card_query(dbc, sql, rows, 300, err, 280);
}

static int rt_children(SQLHDBC dbc, const RtObj *o, CardRow *rows, RtJob *j, RtEl *el, int max, long *variant) {
  *variant = 0;
  wchar_t *sql = (wchar_t *)malloc(4000 * sizeof(wchar_t));
  if (!sql) return 0;
  wchar_t err[280];
  int n = 0;
  if (o->tcCard) {
    /* Версия техсостава — как её выбирает модуль «Технологический состав» PLM
       (с 2026.09.23.80): утверждённая (ActualVersionTechComp); её нет — рабочая:
       «В работе» / «На корректировке» с номером больше нуля, то есть самый
       большой положительный VersionNumber; версии с номером меньше нуля — снимки
       «на дату», они последними. Прежде без утверждённой бралась версия с самым
       большим id — бывал снимок или версия без варианта этого исполнения, и
       состав выходил пустым. Пробуем по порядку, пока не найдётся вариант. */
    _snwprintf(sql, 4000,
               L"SELECT TOP 8 c.V, CAST(vo.Name AS NVARCHAR(200)), c.K, ISNULL(vn.N,0), 0 "
               L"FROM (SELECT a.Link AS V, nk.Value AS K FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
               L"WHERE a.OwnerId=%ld AND a.Outdated=0 AND a.DataType=6 AND ISNULL(a.Link,0)<>0 "
               L"AND ISNULL(a.CollectionElementId,0)=0 "
               L"UNION SELECT o.InfoObjectId, N'' FROM InfoObjects AS o WITH(NOLOCK) "
               L"WHERE o.ParentId=%ld AND o.Erased=0) AS c "
               L"JOIN InfoObjects AS vo WITH(NOLOCK) ON vo.InfoObjectId=c.V AND vo.Erased=0 "
               L"OUTER APPLY (SELECT TOP 1 vv.IntegerNumber AS N FROM InfoObjectAttributes AS vv WITH(NOLOCK) "
               L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=vv.NameKeyId AND nkv.Value=N'VersionNumber' "
               L"WHERE vv.OwnerId=c.V AND vv.Outdated=0) AS vn "
               L"WHERE EXISTS (SELECT 1 FROM InfoObjects AS ch WITH(NOLOCK) "
               L"JOIN InfoObjectAttributes AS t WITH(NOLOCK) ON t.OwnerId=ch.InfoObjectId AND t.Outdated=0 "
               L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=t.NameKeyId AND nkt.Value=N'TechComposition' "
               L"WHERE ch.ParentId=c.V AND ch.Erased=0) "
               L"ORDER BY CASE WHEN c.K=N'ActualVersionTechComp' THEN 0 WHEN ISNULL(vn.N,0)<0 THEN 2 ELSE 1 END, "
               L"ISNULL(vn.N,0) DESC, c.V DESC",
               o->tcCard, o->tcCard);
    CardRow vers[8];
    int nv = card_query(dbc, sql, vers, 8, err, 280);
    if (nv < 0) rt_log(j, L"    техсостав: версия не прочиталась — %s\r\n", err);
    else if (nv == 0) rt_log(j, L"    техсостав: в карточке %ld нет версии с составом\r\n", o->tcCard);
    long tried = 0;
    for (int v = 0; v < nv && n <= 0; v++) {
      long ver = vers[v].n1;
      if (ver == tried) continue; /* утверждённая приходит дважды: ссылкой и вложенным объектом */
      tried = ver;
      BOOL actual = !_wcsicmp(vers[v].s2, L"ActualVersionTechComp");
      n = rt_tc_rows(dbc, ver, o->prodConf, rows, sql, err);
      if (n == 0 && o->prodConf) { /* варианта этой конфигурации нет — если вариант один, он и есть */
        n = rt_tc_rows(dbc, ver, 0, rows, sql, err);
        for (int i = 1; i < n; i++)
          if (rows[i].n2 != rows[0].n2) {
            rt_log(j, L"    техсостав «%s»: вариантов несколько, своего (конфигурация %ld) нет\r\n", vers[v].s1,
                   o->prodConf);
            n = 0;
            break;
          }
        if (n > 0) rt_log(j, L"    техсостав: вариант не по конфигурации, но он в версии один — беру\r\n");
      }
      if (n < 0) rt_log(j, L"    техсостав: запрос не выполнился — %s\r\n", err);
      else if (n > 0 && !actual)
        rt_log(j, L"    техсостав не утверждён — беру %s версию №%ld «%s» (#%ld)\r\n",
               vers[v].n2 > 0 ? L"рабочую" : L"снимок, ", vers[v].n2, vers[v].s1, ver);
    }
  }
  /* Через карточку не вышло — прямо: вариант техсостава, у которого Product —
     это исполнение (или исполнение по ссылкам изделия), и у которого есть
     TechComposition; версия — утверждённая, иначе с большим положительным
     номером (с 2026.09.23.82). Так находится техсостав, даже если карточка
     взаимосвязей указывает не туда или карточки нет вовсе. */
  if (n <= 0 && (o->prodConf || o->prodConfObj)) {
    long pc1 = o->prodConf ? o->prodConf : o->prodConfObj, pc2 = o->prodConfObj ? o->prodConfObj : pc1;
    _snwprintf(sql, 4000,
               L"SELECT TOP 10 ch.InfoObjectId, CAST(v.Name AS NVARCHAR(200)), N'', ISNULL(vn.N,0), ch.ParentId "
               L"FROM InfoObjectAttributes AS pr WITH(NOLOCK) "
               L"JOIN NameKeys AS nkp WITH(NOLOCK) ON nkp.NameKeyId=pr.NameKeyId AND nkp.Value=N'Product' "
               L"JOIN InfoObjects AS ch WITH(NOLOCK) ON ch.InfoObjectId=pr.OwnerId AND ch.Erased=0 "
               L"JOIN InfoObjects AS v WITH(NOLOCK) ON v.InfoObjectId=ch.ParentId AND v.Erased=0 "
               L"OUTER APPLY (SELECT TOP 1 vv.IntegerNumber AS N FROM InfoObjectAttributes AS vv WITH(NOLOCK) "
               L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=vv.NameKeyId AND nkv.Value=N'VersionNumber' "
               L"WHERE vv.OwnerId=v.InfoObjectId AND vv.Outdated=0) AS vn "
               L"OUTER APPLY (SELECT TOP 1 1 AS A FROM InfoObjectAttributes AS av WITH(NOLOCK) "
               L"JOIN NameKeys AS nka WITH(NOLOCK) ON nka.NameKeyId=av.NameKeyId AND nka.Value=N'ActualVersionTechComp' "
               L"WHERE av.OwnerId=v.ParentId AND av.Link=v.InfoObjectId AND av.Outdated=0) AS act "
               L"WHERE pr.Link IN (%ld,%ld) AND pr.Outdated=0 AND ISNULL(pr.CollectionElementId,0)=0 "
               L"AND EXISTS (SELECT 1 FROM InfoObjectAttributes AS t WITH(NOLOCK) "
               L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=t.NameKeyId AND nkt.Value=N'TechComposition' "
               L"WHERE t.OwnerId=ch.InfoObjectId AND t.Outdated=0) "
               L"ORDER BY CASE WHEN act.A=1 THEN 0 WHEN ISNULL(vn.N,0)<0 THEN 2 ELSE 1 END, ISNULL(vn.N,0) DESC, ch.InfoObjectId DESC",
               pc1, pc2);
    CardRow cand[10];
    int nc = card_query(dbc, sql, cand, 10, err, 280);
    if (nc < 0) rt_log(j, L"    техсостав напрямую: запрос не выполнился — %s\r\n", err);
    for (int c = 0; c < nc && n <= 0; c++) {
      n = rt_tc_rows(dbc, cand[c].n3, 0, rows, sql, err);
      if (n > 0) {
        for (int i = 0; i < n; i++) /* в версии и чужие варианты — только этот */
          if (rows[i].n2 != cand[c].n1) rows[i--] = rows[--n];
        if (n > 0)
          rt_log(j, L"    техсостав найден напрямую (вариант #%ld исполнения, версия №%ld «%s»)%s\r\n", cand[c].n1,
                 cand[c].n2, cand[c].s1, o->tcCard ? L" — через карточку не нашёлся" : L" — карточки техсостава нет");
      }
    }
    if (n <= 0 && !j->tcDiag) { /* раз — почему: что есть в карточке и на какие исполнения ссылаются варианты */
      j->tcDiag = TRUE;
      rt_log(j, L"    техсостав не найден: карточка #%ld, исполнение #%ld (по изделию #%ld), вариантов с ним напрямую %d\r\n",
             o->tcCard, o->prodConf, o->prodConfObj, nc < 0 ? 0 : nc);
      if (o->tcCard) {
        _snwprintf(sql, 4000,
                   L"SELECT TOP 40 ch.InfoObjectId, CAST(v.Name AS NVARCHAR(200)), "
                   L"ISNULL(CAST(po.Name AS NVARCHAR(250)),N'—'), ISNULL(vn.N,0), ISNULL(pr.Link,0) "
                   L"FROM InfoObjects AS v WITH(NOLOCK) "
                   L"JOIN InfoObjects AS ch WITH(NOLOCK) ON ch.ParentId=v.InfoObjectId AND ch.Erased=0 "
                   L"OUTER APPLY (SELECT TOP 1 vv.IntegerNumber AS N FROM InfoObjectAttributes AS vv WITH(NOLOCK) "
                   L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=vv.NameKeyId AND nkv.Value=N'VersionNumber' "
                   L"WHERE vv.OwnerId=v.InfoObjectId AND vv.Outdated=0) AS vn "
                   L"OUTER APPLY (SELECT TOP 1 a.Link FROM InfoObjectAttributes AS a WITH(NOLOCK) "
                   L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'Product' "
                   L"WHERE a.OwnerId=ch.InfoObjectId AND a.Outdated=0) AS pr "
                   L"LEFT JOIN InfoObjects AS po WITH(NOLOCK) ON po.InfoObjectId=pr.Link "
                   L"WHERE v.ParentId=%ld AND v.Erased=0 ORDER BY v.InfoObjectId, ch.InfoObjectId",
                   o->tcCard);
        CardRow *dg = (CardRow *)malloc(sizeof(CardRow) * 40);
        int nd = dg ? card_query(dbc, sql, dg, 40, err, 280) : -1;
        if (nd < 0) rt_log(j, L"      карточка: запрос не выполнился — %s\r\n", err);
        else if (nd == 0) rt_log(j, L"      в карточке техсостава нет версий с вариантами\r\n");
        for (int i = 0; i < nd; i++)
          rt_log(j, L"      версия «%s» №%ld · вариант #%ld → исполнение %s (#%ld)\r\n", dg[i].s1, dg[i].n2, dg[i].n1,
                 dg[i].s2, dg[i].n3);
        free(dg);
      }
    }
  }
  /* Техсостава нет (не заведён или пуст) — состав по КД: Items самого ИИВ, с
     пометкой (с 2026.09.23.80; прежде позиция выходила без состава) */
  BOOL kd = FALSE;
  if (n <= 0) {
    n = rt_kd_rows(dbc, o->id, rows, sql, err);
    if (n > 0) {
      kd = TRUE;
      j->kdUsed++;
      rt_log(j, L"    техсостава нет — состав по КД (Items): строк %d\r\n", n);
    } else if (n < 0) {
      rt_log(j, L"    состав по КД: запрос не выполнился — %s\r\n", err);
    }
  }
  if (n <= 0) {
    free(sql);
    return 0;
  }
  if (kd)
    for (int i = 0; i < n; i++) rows[i].n2 = 0; /* у состава КД нет исполнения техсостава */
  *variant = rows[0].n2; /* исполнение техсостава — у него и список изменений КД */
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
    BOOL childDes = FALSE, childProd = FALSE;
    for (int i = 0; i < m; i++) {
      if (ar[i].n1 != ids[e]) continue;
      /* ссылок в строке бывает несколько (изделие, документ, версия) — сперва
         Product, как в PlmApi; нет его — та, что похожа на обозначение */
      if (ar[i].n3 == 6 && ar[i].n2 && !_wcsicmp(ar[i].s1, L"ProductConfiguration")) x.pc = ar[i].n2;
      if (ar[i].n3 == 6 && ar[i].n2 && !_wcsicmp(ar[i].s1, L"Product")) {
        if (!childProd) x.child = ar[i].n2;
        childProd = TRUE;
      }
      else if (ar[i].n3 == 6 && ar[i].n2 && !childProd &&
               (!x.child || (!childDes && rt_looks_des(ar[i].s2)))) {
        x.child = ar[i].n2;
        childDes = rt_looks_des(ar[i].s2);
      }
      else if (!_wcsicmp(ar[i].s1, L"Section") && ar[i].s2[0]) lstrcpynW(x.section, ar[i].s2, 120);
      else if (!_wcsicmp(ar[i].s1, L"SortedPosition")) {
        double v;
        if (rt_num(ar[i].s2, &v)) {
          x.sortPos = v;
          x.hasSort = TRUE;
        }
      }
      else if (!_wcsicmp(ar[i].s1, L"MeasureUnitOfQuaintity") && ar[i].s2[0])
        lstrcpynW(x.unit, ar[i].s2, 40); /* поле техсостава PLM — главное */
      else if (!_wcsicmp(ar[i].s1, L"MeasureUnit") && ar[i].s2[0] && !x.unit[0])
        lstrcpynW(x.unit, ar[i].s2, 40);
      else if (!_wcsicmp(ar[i].s1, L"Quantity")) { /* поле PLM — важнее похожих по названию */
        double v;
        if (rt_num(ar[i].s2, &v) && v > 0) {
          x.qty = v;
          x.qtyFound = TRUE;
        }
      }
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
      rt_log(j, L"    поля строки техсостава:\r\n");
      for (int i = 0; i < m; i++) {
        if (ar[i].n1 != ids[e]) continue;
        if (ar[i].n3 == 6) rt_log(j, L"      %s = %s (#%ld)\r\n", ar[i].s1, ar[i].s2[0] ? ar[i].s2 : L"·", ar[i].n2);
        else rt_log(j, L"      %s = %s\r\n", ar[i].s1, ar[i].s2[0] ? ar[i].s2 : L"·");
      }
    }
    if (x.child) el[out++] = x;
  }
  free(ar);
  /* порядок — как в редакторе техсостава (SortedPosition), а не как строки
     легли в базу; без номера — в конце, в прежнем порядке */
  int sorted = 0;
  for (int a = 1; a < out; a++) {
    RtEl t = el[a];
    int b = a;
    while (b > 0 && t.hasSort && (!el[b - 1].hasSort || el[b - 1].sortPos > t.sortPos)) {
      el[b] = el[b - 1];
      b--;
    }
    if (b != a) sorted = 1;
    el[b] = t;
  }
  rt_log(j, L"    в техсоставе строк %d, со ссылкой %d%s\r\n", used, out, sorted ? L" (по порядку редактора PLM)" : L"");
  return out;
}

/* ---- изменения КД, не разобранные технологом ------------------------------

   Модуль «Технологический состав» PLM сам сравнивает конструкторский состав
   с техсоставом и складывает разницу в исполнение техсостава: коллекция
   ChangedCollection — строка на позицию (ProductConfiguration), в ней
   WhatChange — «Изменено количество», «Добавлена в КД», «Удалена из КД»,
   «Изменена ЕИ»…: было (BeforeString), стало (AfterString) и отметка
   технолога AcceptChange (да — согласен, нет — отказался, пусто — не
   разобрано). Не разобранное значит: ведомость собрана по техсоставу, который
   отстал от КД, — это и пишется в примечание. Только чтение. */
static void rt_kd_changes(SQLHDBC dbc, long variant, CardRow *rows, RtJob *j, RtEl *el, int ne, wchar_t *asmNote,
                          int cap) {
  asmNote[0] = 0;
  if (!variant) return;
  wchar_t sql[3600], err[280];
  /* позиции изменений: какое исполнение */
  _snwprintf(sql, 3600,
             L"SELECT TOP 120 r.CollectionElementId, nk.Value, COALESCE(CAST(lo.Name AS NVARCHAR(250)), N''), "
             L"ISNULL(a.Link,0), 0 FROM InfoObjectAttributes AS cc WITH(NOLOCK) "
             L"JOIN NameKeys AS nkc WITH(NOLOCK) ON nkc.NameKeyId=cc.NameKeyId AND nkc.Value=N'ChangedCollection' "
             L"JOIN InfoObjectCollectionElements AS r WITH(NOLOCK) ON r.AttributeId=cc.AttributeId AND r.Outdated=0 "
             L"JOIN InfoObjectAttributes AS a WITH(NOLOCK) ON a.CollectionElementId=r.CollectionElementId "
             L"AND a.Outdated=0 "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'ProductConfiguration' "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON lo.InfoObjectId=a.Link "
             L"WHERE cc.OwnerId=%ld AND cc.Outdated=0 AND ISNULL(cc.CollectionElementId,0)=0",
             variant);
  int np = card_query(dbc, sql, rows, 120, err, 280);
  if (np < 0) {
    rt_log(j, L"    изменения КД: запрос не выполнился — %s\r\n", err);
    return;
  }
  if (np == 0) return;
  typedef struct {
    long row, pc;
    wchar_t name[120];
  } KdPos;
  KdPos pos[120];
  int npos = np;
  for (int i = 0; i < np; i++) {
    pos[i].row = rows[i].n1;
    pos[i].pc = rows[i].n2;
    lstrcpynW(pos[i].name, rows[i].s2, 120);
  }
  /* что изменилось: Change, было, стало, отметка (n3: −1 — нет, 0/1) */
  _snwprintf(sql, 3600,
             L"SELECT TOP 300 w.CollectionElementId, nk.Value, COALESCE(" CARD_VALUE_SQL L", N''), r.CollectionElementId, "
             L"CASE WHEN nk.Value=N'AcceptChange' THEN ISNULL(CAST(a.BoolValue AS INT),-1) ELSE 0 END "
             L"FROM InfoObjectAttributes AS cc WITH(NOLOCK) "
             L"JOIN NameKeys AS nkc WITH(NOLOCK) ON nkc.NameKeyId=cc.NameKeyId AND nkc.Value=N'ChangedCollection' "
             L"JOIN InfoObjectCollectionElements AS r WITH(NOLOCK) ON r.AttributeId=cc.AttributeId AND r.Outdated=0 "
             L"JOIN InfoObjectAttributes AS wa WITH(NOLOCK) ON wa.CollectionElementId=r.CollectionElementId "
             L"AND wa.Outdated=0 "
             L"JOIN NameKeys AS nkw WITH(NOLOCK) ON nkw.NameKeyId=wa.NameKeyId AND nkw.Value=N'WhatChange' "
             L"JOIN InfoObjectCollectionElements AS w WITH(NOLOCK) ON w.AttributeId=wa.AttributeId AND w.Outdated=0 "
             L"JOIN InfoObjectAttributes AS a WITH(NOLOCK) ON a.CollectionElementId=w.CollectionElementId "
             L"AND a.Outdated=0 "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"AND nk.Value IN (N'Change',N'BeforeString',N'AfterString',N'AcceptChange') "
             L"WHERE cc.OwnerId=%ld AND cc.Outdated=0 AND ISNULL(cc.CollectionElementId,0)=0 "
             L"ORDER BY r.CollectionElementId, w.CollectionElementId",
             variant);
  int nw = card_query(dbc, sql, rows, 300, err, 280); /* rows — на 300 строк */
  if (nw < 0) {
    rt_log(j, L"    изменения КД: запрос не выполнился — %s\r\n", err);
    return;
  }
  int open = 0, total = 0;
  wchar_t added[200] = L"";
  rt_log(j, L"    изменения КД в исполнении техсостава #%ld:\r\n", variant);
  for (int i = 0; i < nw;) {
    long w = rows[i].n1, r = rows[i].n2;
    wchar_t chg[120] = L"", before[60] = L"", after[60] = L"";
    int acc = -1;
    for (; i < nw && rows[i].n1 == w; i++) {
      if (!_wcsicmp(rows[i].s1, L"Change")) lstrcpynW(chg, rows[i].s2, 120);
      else if (!_wcsicmp(rows[i].s1, L"BeforeString")) lstrcpynW(before, rows[i].s2, 60);
      else if (!_wcsicmp(rows[i].s1, L"AfterString")) lstrcpynW(after, rows[i].s2, 60);
      else if (!_wcsicmp(rows[i].s1, L"AcceptChange")) acc = (int)rows[i].n3;
    }
    if (!chg[0]) continue;
    total++;
    const KdPos *ps = NULL;
    for (int k = 0; k < npos && !ps; k++)
      if (pos[k].row == r) ps = &pos[k];
    rt_log(j, L"      %s: %s%s%s%s%s — %s\r\n", ps ? ps->name : L"?", chg, before[0] || after[0] ? L" " : L"", before,
           after[0] ? L" → " : L"", after, acc == 1 ? L"согласен" : acc == 0 ? L"отказ" : L"НЕ РАЗОБРАНО");
    if (acc >= 0) continue; /* технолог решил — не предупреждаем */
    open++;
    wchar_t one[160];
    if (before[0] || after[0]) _snwprintf(one, 160, L"КД: %s %s → %s (не разобрано)", chg, before, after);
    else _snwprintf(one, 160, L"КД: %s (не разобрано)", chg);
    one[159] = 0;
    /* к строке ведомости этой позиции; позиции нет (добавлена в КД) — к сборке */
    BOOL put = FALSE;
    for (int e = 0; e < ne && ps && ps->pc; e++)
      if (el[e].pc == ps->pc || el[e].child == ps->pc) {
        if (!el[e].kd[0]) lstrcpynW(el[e].kd, one, 160);
        put = TRUE;
      }
    if (!put && ps) {
      wchar_t nd[120];
      card_des_from_name(ps->name, nd, 120);
      size_t l = wcslen(added);
      if (l + wcslen(nd) + 3 < 200) _snwprintf(added + l, 200 - l, L"%s%s", l ? L", " : L"", nd[0] ? nd : ps->name);
    }
  }
  if (!total) rt_log(j, L"      нет\r\n");
  if (open) {
    if (added[0]) _snwprintf(asmNote, cap, L"КД изменён, не разобрано: %d (%s)", open, added);
    else _snwprintf(asmNote, cap, L"КД изменён, не разобрано: %d", open);
    asmNote[cap - 1] = 0;
  }
}

/* документ (сборочный чертёж и т.п.) — код документа после обозначения:
   «…00.00СБ», «…ВО», «…Э3» */
static BOOL rt_is_doc(const wchar_t *des, const wchar_t *objName) {
  size_t n = wcslen(des), k = n;
  while (k > 0 && iswdigit(des[k - 1]) && n - k < 2) k--;
  size_t e = k;
  while (k > 0 && des[k - 1] >= L'А' && des[k - 1] <= L'Я') k--;
  size_t letters = e - k;
  BOOL code = k > 0 && iswdigit(des[k - 1]) && (letters >= 2 ? letters <= 3 : letters == 1 && e < n);
  wchar_t low[260];
  lstrcpynW(low, objName, 260);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  return code || wcsstr(low, L"чертеж") || wcsstr(low, L"чертёж");
}

/* ни ТП, ни заготовки — один раз выписать, с чем изделие связано: ссылки
   его самого и родителя и кто ссылается на него; по этому видно, где карточки */
static void rt_diag(SQLHDBC dbc, const RtObj *o, CardRow *rows, RtJob *j) {
  wchar_t sql[2400], err[280];
  _snwprintf(sql, 2400,
             L"SELECT TOP 80 a.OwnerId, nk.Value, ISNULL(CAST(lo.Name AS NVARCHAR(200)),N''), ISNULL(a.Link,0), "
             L"0 FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON lo.InfoObjectId=a.Link "
             L"WHERE a.OwnerId IN (%ld,%ld,%ld) AND a.Outdated=0 AND a.DataType=6 AND ISNULL(a.Link,0)<>0 "
             L"AND ISNULL(a.CollectionElementId,0)=0 ORDER BY a.OwnerId, nk.Value",
             o->id, o->par ? o->par : o->id, o->prodConf ? o->prodConf : o->id);
  int n = card_query(dbc, sql, rows, 80, err, 280);
  rt_log(j, L"    связи %ld (родитель %ld, конфигурация %ld):\r\n", o->id, o->par, o->prodConf);
  if (n < 0) rt_log(j, L"      запрос не выполнился — %s\r\n", err);
  for (int i = 0; i < n; i++)
    rt_log(j, L"      %ld · %s → %s (#%ld)\r\n", rows[i].n1, rows[i].s1, rows[i].s2[0] ? rows[i].s2 : L"·",
           rows[i].n2);
  _snwprintf(sql, 2400,
             L"SELECT TOP 30 a.OwnerId, nk.Value, ISNULL(CAST(ow.Name AS NVARCHAR(200)),N''), a.Link, "
             L"ISNULL(a.CollectionElementId,0) FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"JOIN InfoObjects AS ow WITH(NOLOCK) ON ow.InfoObjectId=a.OwnerId AND ow.Erased=0 "
             L"WHERE a.Link IN (%ld,%ld) AND a.Outdated=0 AND a.DataType=6 ORDER BY nk.Value, a.OwnerId",
             o->id, o->prodConf ? o->prodConf : o->id);
  n = card_query(dbc, sql, rows, 30, err, 280);
  rt_log(j, L"    на него ссылаются:\r\n");
  if (n < 0) rt_log(j, L"      запрос не выполнился — %s\r\n", err);
  for (int i = 0; i < n; i++)
    rt_log(j, L"      %s (#%ld) · %s → #%ld%s\r\n", rows[i].s2[0] ? rows[i].s2 : L"·", rows[i].n1, rows[i].s1,
           rows[i].n2, rows[i].n3 ? L" (в строке списка)" : L"");
}

/* ---- «Выгрузка для проверки» ----------------------------------------------
   Всё, что есть у объекта: имя, шаблон, родитель, атрибуты со значениями и
   ссылками, строки списков. По такому тексту видно, где на этой базе лежат
   состав, заготовка, сортамент, припуск, участки, — его присылают, если
   ведомость где-то пустая. Возвращает ссылку атрибута want (или 0). */
static long rt_dump_obj(SQLHDBC dbc, long id, const wchar_t *label, CardRow *rows, RtJob *j, const wchar_t *want) {
  if (!id) return 0;
  wchar_t sql[3000], err[280];
  long found = 0;
  _snwprintf(sql, 3000,
             L"SELECT TOP 1 o.InfoObjectId, CAST(o.Name AS NVARCHAR(250)), ISNULL(CAST(t.NameUI AS NVARCHAR(200)),N''), "
             L"ISNULL(o.ParentId,0), o.TemplateId FROM InfoObjects AS o WITH(NOLOCK) "
             L"LEFT JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId WHERE o.InfoObjectId=%ld",
             id);
  int n = card_query(dbc, sql, rows, 1, err, 280);
  if (n > 0)
    rt_log(j, L"\r\n  ── %s #%ld «%s» [%s, шаблон %ld], родитель #%ld\r\n", label, id, rows[0].s1, rows[0].s2, rows[0].n3,
           rows[0].n2);
  else
    rt_log(j, L"\r\n  ── %s #%ld — не прочитался%s%s\r\n", label, id, n < 0 ? L": " : L"", n < 0 ? err : L"");
  _snwprintf(sql, 3000,
             L"SELECT TOP 90 a.AttributeId, nk.Value, LEFT(" PF_VALUE_SQL L", 160), ISNULL(a.Link,0), a.DataType "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE a.OwnerId=%ld AND a.Outdated=0 AND ISNULL(a.CollectionElementId,0)=0 ORDER BY nk.Value",
             id);
  n = card_query(dbc, sql, rows, 90, err, 280);
  if (n < 0) rt_log(j, L"     атрибуты не прочитались: %s\r\n", err);
  for (int i = 0; i < n; i++) {
    if (!rows[i].s2[0] && !rows[i].n2) continue; /* пустые — мимо */
    if (rows[i].n2 && rows[i].n3 == 6)
      rt_log(j, L"     %s = %s (#%ld)\r\n", rows[i].s1, rows[i].s2[0] ? rows[i].s2 : L"·", rows[i].n2);
    else
      rt_log(j, L"     %s = %s\r\n", rows[i].s1, rows[i].s2);
    if (want && !found && !_wcsicmp(rows[i].s1, want) && rows[i].n3 == 6) found = rows[i].n2;
  }
  /* строки списков и составных атрибутов */
  _snwprintf(sql, 3000,
             L"SELECT TOP 80 a.CollectionElementId, CAST(nkp.Value AS NVARCHAR(100)) + N'|' + CAST(nk.Value AS NVARCHAR(100)), "
             L"LEFT(" PF_VALUE_SQL L", 120), ISNULL(a.Link,0), a.DataType "
             L"FROM InfoObjectAttributes AS p WITH(NOLOCK) "
             L"JOIN NameKeys AS nkp WITH(NOLOCK) ON nkp.NameKeyId=p.NameKeyId "
             L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) "
             L"ON ce.AttributeId IN (p.AttributeId, ISNULL(p.Link,0)) AND ce.Outdated=0 "
             L"JOIN InfoObjectAttributes AS a WITH(NOLOCK) ON a.CollectionElementId=ce.CollectionElementId "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value<>N'LastChanged' "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"WHERE p.OwnerId=%ld AND p.Outdated=0 AND ISNULL(p.CollectionElementId,0)=0 "
             L"ORDER BY p.AttributeId, a.CollectionElementId, a.Outdated, nk.Value",
             id);
  n = card_query(dbc, sql, rows, 80, err, 280);
  long cur = -1;
  for (int i = 0; i < n; i++) {
    wchar_t *bar = wcschr(rows[i].s1, L'|');
    const wchar_t *k = bar ? bar + 1 : rows[i].s1;
    if (bar) *bar = 0;
    if (rows[i].n1 != cur) {
      cur = rows[i].n1;
      rt_log(j, L"\r\n     [%s] строка #%ld:", rows[i].s1, cur);
    }
    if (!rows[i].s2[0] && !rows[i].n2) continue;
    if (rows[i].n2 && rows[i].n3 == 6) rt_log(j, L" %s=%s(#%ld)", k, rows[i].s2[0] ? rows[i].s2 : L"·", rows[i].n2);
    else rt_log(j, L" %s=%s", k, rows[i].s2);
  }
  if (n > 0) rt_log(j, L"\r\n");
  return found;
}

/* вложенные объекты (ParentId): список; ids — первые max */
static int rt_dump_kids(SQLHDBC dbc, long id, const wchar_t *label, CardRow *rows, RtJob *j, long *ids, int max) {
  if (!id) return 0;
  wchar_t sql[800], err[280];
  _snwprintf(sql, 800,
             L"SELECT TOP 25 o.InfoObjectId, CAST(o.Name AS NVARCHAR(250)), ISNULL(CAST(t.NameUI AS NVARCHAR(200)),N''), "
             L"0, o.TemplateId FROM InfoObjects AS o WITH(NOLOCK) "
             L"LEFT JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId "
             L"WHERE o.ParentId=%ld AND o.Erased=0 ORDER BY o.InfoObjectId",
             id);
  int n = card_query(dbc, sql, rows, 25, err, 280);
  rt_log(j, L"\r\n  ── %s #%ld: вложенных %d\r\n", label, id, n < 0 ? 0 : n);
  int k = 0;
  for (int i = 0; i < n; i++) {
    rt_log(j, L"     #%ld «%s» [%s, шаблон %ld]\r\n", rows[i].n1, rows[i].s1, rows[i].s2, rows[i].n3);
    if (k < max) ids[k++] = rows[i].n1;
  }
  return k;
}

static void rt_dump_product(SQLHDBC dbc, const RtObj *o, CardRow *rows, RtJob *j) {
  rt_log(j, L"\r\n===== ПРОВЕРКА: %s %s (#%ld) =====\r\n", o->des, o->name, o->id);
  rt_dump_obj(dbc, o->id, L"объект", rows, j, NULL);
  if (o->par) rt_dump_obj(dbc, o->par, L"родитель", rows, j, NULL);
  long ic = 0;
  if (o->prodConf) ic = rt_dump_obj(dbc, o->prodConf, L"конфигурация", rows, j, L"ProductInterconnectCard");
  if (ic) rt_dump_obj(dbc, ic, L"карта взаимосвязей", rows, j, NULL);
  RtObj tmp = *o;
  rt_diag(dbc, &tmp, rows, j);
  long ids[4];
  if (o->tcCard) {
    long av = rt_dump_obj(dbc, o->tcCard, L"карточка техсостава", rows, j, L"ActualVersionTechComp");
    int nk = rt_dump_kids(dbc, o->tcCard, L"в карточке техсостава", rows, j, ids, 4);
    long ver = av ? av : (nk ? ids[nk - 1] : 0);
    if (ver) {
      rt_dump_obj(dbc, ver, av ? L"версия техсостава (утверждённая)" : L"версия техсостава (последняя)", rows, j, NULL);
      int nv = rt_dump_kids(dbc, ver, L"варианты версии", rows, j, ids, 2);
      for (int i = 0; i < nv; i++) rt_dump_obj(dbc, ids[i], L"вариант техсостава", rows, j, NULL);
    }
  }
  if (o->pfCard) {
    rt_dump_obj(dbc, o->pfCard, L"карточка заготовок", rows, j, NULL);
    int np = rt_dump_kids(dbc, o->pfCard, L"заготовки в карточке", rows, j, ids, 2);
    for (int i = 0; i < np; i++) rt_dump_obj(dbc, ids[i], L"заготовка", rows, j, NULL);
  }
  if (o->tpCard) rt_dump_obj(dbc, o->tpCard, L"карточка ТП", rows, j, NULL);
  if (j->lastTp) {
    long tp = j->lastTp, ver = j->lastTpVer, var = j->lastTpVar;
    rt_dump_obj(dbc, tp, L"техпроцесс", rows, j, NULL);
    if (ver) rt_dump_obj(dbc, ver, L"версия ТП", rows, j, NULL);
    if (var && var != ver) rt_dump_obj(dbc, var, L"основной вариант ТП", rows, j, NULL);
    int no = rt_dump_kids(dbc, var ? var : ver, L"операции", rows, j, ids, 2);
    for (int i = 0; i < no; i++) {
      long ts = rt_dump_obj(dbc, ids[i], L"операция", rows, j, L"TSOperation");
      if (ts) rt_dump_obj(dbc, ts, L"TSOperation операции", rows, j, NULL);
    }
  }
  rt_log(j, L"===== конец проверки #%ld =====\r\n\r\n", o->id);
}

/* ---- уже собранное: повтор позиции — копией, без запросов ---------------------
   Одна и та же подсборка или деталь входит во многие узлы (болт — в десяток), и
   каждый раз это десяток-другой запросов. Позиция с тем же исполнением уже
   собрана — её строки (сама и всё под ней) копируются: уровень сдвигается,
   «входит в», количество и норма на изделие пересчитываются по новому месту
   (с 2026.09.23.83). Свои пометки позиции (маршрут, заготовка, КД) — из
   собранного, пометки строки состава (кол-во, единица) — новые. */
typedef struct {
  long id, pc;
  int row0, row1; /* строки в rows; row0 < 0 — позиция пропущена (материал, документ) */
  wchar_t note[200];
} RtMemo;

static RtMemo *rt_memo_find(RtJob *j, long id, long pc) {
  RtMemo *m = (RtMemo *)j->memo;
  for (int i = 0; m && i < j->nmemo; i++)
    if (m[i].id == id && m[i].pc == pc) return &m[i];
  return NULL;
}

static RtMemo *rt_memo_add(RtJob *j, long id, long pc) {
  if (j->dump || j->split) return NULL; /* в раздаче строки ещё не на месте; проверочная — подробно */
  if (!j->memo) j->memo = calloc(RT_MAX, sizeof(RtMemo));
  if (!j->memo || j->nmemo >= RT_MAX) return NULL;
  RtMemo *m = &((RtMemo *)j->memo)[j->nmemo++];
  memset(m, 0, sizeof(*m));
  m->id = id;
  m->pc = pc;
  m->row0 = m->row1 = -1;
  return m;
}

static void rt_note_add(wchar_t *note, const wchar_t *nt) {
  if (!nt || !nt[0]) return;
  size_t l = wcslen(note);
  if (l + 4 >= 200) return;
  _snwprintf(note + l, 200 - l, L"%s%s", l ? L"; " : L"", nt);
  note[199] = 0;
}

/* пометки строки состава (а не позиции): кол-во не найдено, не в штуках, КД */
static void rt_src_notes(int level, BOOL qtyFound, const RtEl *src, wchar_t *note) {
  if (level && !qtyFound) rt_note_add(note, L"кол-во не найдено");
  if (src && src->unit[0] && _wcsnicmp(src->unit, L"шт", 2) && iswalpha(src->unit[0])) {
    wchar_t u[60];
    _snwprintf(u, 60, L"кол-во в %s", src->unit);
    u[59] = 0;
    rt_note_add(note, u);
  }
  if (src && src->kd[0]) rt_note_add(note, src->kd);
}

static void rt_memo_copy(RtJob *j, const RtMemo *m, int level, const wchar_t *parentDes, double qty, BOOL qtyFound,
                         double parentTot, const RtEl *src) {
  int cnt = m->row1 - m->row0;
  if (cnt <= 0) return;
  if (j->n + cnt > RT_MAX) cnt = RT_MAX - j->n;
  const RtRow *f0 = &j->rows[m->row0];
  double newTot = qty * parentTot;
  double factor = f0->qtyTot != 0 ? newTot / f0->qtyTot : 0;
  int dl = level - f0->level;
  rt_log(j, L"%*s%s %s — повтор: из уже собранного (строк %d), кол-во ", level * 2, L"", f0->f[RC_DES],
         f0->f[RC_NAME], cnt);
  for (int k = 0; k < cnt; k++) {
    RtRow *d = &j->rows[j->n++];
    *d = j->rows[m->row0 + k];
    d->level += dl;
    if (k == 0) {
      d->qty = qty;
      d->qtyTot = newTot;
      lstrcpynW(d->f[RC_PARENT], parentDes, 200);
      rt_fmt(d->qty, 3, d->f[RC_QTY], 200);
      lstrcpynW(d->f[RC_NOTE], m->note, 200);
      rt_src_notes(level, qtyFound, src, d->f[RC_NOTE]);
      rt_log(j, L"%s\r\n", d->f[RC_QTY]);
    } else {
      d->qtyTot *= factor;
    }
    rt_fmt(d->qtyTot, 3, d->f[RC_QTYTOT], 200);
    if (d->hasNorm) rt_fmt(floor(d->norm1 * 1000 + 0.5) / 1000 * d->qtyTot, 3, d->f[RC_NORMTOT], 200);
    if (j->notify)
      PostMessageW(j->notify, WM_RT_PROGRESS, (WPARAM)(j->shared ? InterlockedIncrement(j->shared) : j->n), 0);
  }
}

/* задача для потока: ветка состава с её местом в дереве */
typedef struct {
  RtEl el;
  int level;
  double tot;
  wchar_t des[200];
} RtTask;

static void rt_plan_add(RtJob *j, int v) {
  if (!j->plan) j->plan = (int *)malloc(sizeof(int) * RT_MAX * 2);
  if (j->plan && j->nplan < RT_MAX * 2) j->plan[j->nplan++] = v;
}

static void rt_walk(SQLHDBC dbc, long id, int level, const wchar_t *parentDes, double qty, BOOL qtyFound,
                    double parentTot, const wchar_t *elSection, const RtEl *src, CardRow *rows, RtJob *j) {
  if (j->n >= RT_MAX || rt_late(j)) return; /* по кругу не уйдёт: глубина не больше RT_DEPTH */
  long keyPc = src ? src->pc : 0;
  if (!j->dump && !j->split) {
    const RtMemo *hit = rt_memo_find(j, id, keyPc);
    /* кол-во на изделие было 0 — пересчитать нечем, собрать заново */
    if (hit && !(hit->row0 >= 0 && j->rows[hit->row0].qtyTot == 0)) {
      if (hit->row0 >= 0) rt_memo_copy(j, hit, level, parentDes, qty, qtyFound, parentTot, src);
      return;
    }
  }
  RtMemo *memo = rt_memo_add(j, id, keyPc);
  RtObj o;
  if (!rt_obj(dbc, id, &o, rows, j)) return;
  /* исполнение — то, на которое ссылается строка техсостава: у «-01» свой
     техсостав и ТП, а по ссылкам изделия нашлось бы базовое исполнение */
  if (src && src->pc && src->pc != o.prodConf) {
    rt_log(j, L"    исполнение — из строки техсостава #%ld (по изделию было #%ld)\r\n", src->pc, o.prodConf);
    o.prodConfObj = o.prodConf;
    o.prodConf = src->pc;
  }
  /* материалы и покупные в ведомость не идут: у них нет ни своего
     обозначения, ни карточек изделия (ТП, заготовки, техсостава) */
  BOOL product = o.desAttr || o.tpCard || o.pfCard || o.tcCard;
  if (level > 0 && (!product || !rt_looks_des(o.des) || (elSection && wcsstr(elSection, L"атериал")))) {
    rt_log(j, L"  %*s· пропуск «%s» — нет обозначения (материал?)\r\n", level * 2, L"", o.objName);
    return; /* memo->row0 < 0: повтор — тоже пропуск */
  }
  if (level > 0 && (rt_is_doc(o.des, o.objName) || (elSection && wcsstr(elSection, L"окумент")))) {
    rt_log(j, L"  %*s· пропуск «%s» — документ\r\n", level * 2, L"", o.objName);
    return;
  }
  int myRow = j->n;
  RtRow *r = &j->rows[j->n++];
  if (j->split) rt_plan_add(j, myRow);
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
  if (j->notify) PostMessageW(j->notify, WM_RT_PROGRESS, (WPARAM)(j->shared ? InterlockedIncrement(j->shared) : j->n), 0);
  rt_log(j, L"    карточки: ТП %ld, заготовок %ld, техсостава %ld, конфигурация %ld\r\n", o.tpCard, o.pfCard,
         o.tcCard, o.prodConf);
  if (!o.tpCard && !o.pfCard && !(j->diagShown & (level ? 2 : 1))) {
    j->diagShown |= level ? 2 : 1;
    rt_diag(dbc, &o, rows, j);
  }
  wchar_t notes[2][200] = {L"", L""}; /* своё у позиции: маршрут, заготовка */
  rt_route(dbc, &o, rows, j, r->f[RC_ROUTE], 200, notes[0], 60);
  r->tp = j->lastTp;
  r->tpVer = j->lastTpVer;
  r->tpVar = j->lastTpVar;
  lstrcpynW(r->tpName, j->lastTpName, 200);
  r->par = o.par;
  r->prodConf = o.prodConf;
  RtEl *el = (RtEl *)malloc(sizeof(RtEl) * 150);
  long variant = 0;
  int kdBefore = j->kdUsed;
  int ne = el ? rt_children(dbc, &o, rows, j, el, 150, &variant) : 0;
  r->tcVariant = variant;
  r->tcCard = o.tcCard;
  r->pfCard = o.pfCard;
  wchar_t kdNote[200] = L"";
  if (ne > 0 && !rt_late(j)) rt_kd_changes(dbc, variant, rows, j, el, ne, kdNote, 200);
  if (j->kdUsed > kdBefore && !kdNote[0]) lstrcpynW(kdNote, L"состав по КД — техсостава нет", 200);
  /* вид изделия */
  const wchar_t *kind = o.section[0] ? o.section : (elSection && elSection[0] ? elSection : NULL);
  lstrcpynW(r->f[RC_KIND], kind ? kind : (ne > 0 ? L"Сборочные единицы" : L"Детали"), 200);
  /* Сборке заготовка не нужна: в ведомости (шаблон КЗ 26-112) у сборок
     материал, сортамент, припуск и нормы пустые. Карточка заготовки у сборки
     в PLM бывает (своя или от связанной конфигурации) — с 2026.09.23.53 её
     не берём: сперва состав, и только у позиции без состава — заготовка. */
  BOOL assy = ne > 0 || wcsstr(r->f[RC_KIND], L"борочн") || wcsstr(r->f[RC_KIND], L"омплекс");
  if (assy) {
    if (o.pfCard) rt_log(j, L"    сборка — заготовку (карточка %ld) не берём\r\n", o.pfCard);
  } else {
    rt_preform(dbc, &o, rows, j, r, notes[1], 60);
    if (!r->f[RC_MAT][0] && o.mat[0]) lstrcpynW(r->f[RC_MAT], o.mat, 200); /* без заготовки — материал изделия */
    if (r->hasNorm) rt_fmt(floor(r->norm1 * 1000 + 0.5) / 1000 * r->qtyTot, 3, r->f[RC_NORMTOT], 200); /* как в Excel: показанная норма × кол-во */
  }
  /* проверочная выгрузка: сама сборка, первая подсборка и первые две детали */
  if (j->dump && !rt_late(j) && (level == 0 || (assy && j->dumpAsm < 1) || (!assy && j->dumpDet < 2))) {
    if (level && assy) j->dumpAsm++;
    else if (level) j->dumpDet++;
    rt_dump_product(dbc, &o, rows, j);
  }
  /* пометки: свои у позиции (маршрут, заготовка, КД) — их помнит memo; строки состава — свои у места */
  wchar_t own[200] = L"";
  rt_note_add(own, notes[0]);
  rt_note_add(own, notes[1]);
  rt_note_add(own, kdNote);
  lstrcpynW(r->f[RC_NOTE], own, 200);
  rt_src_notes(level, qtyFound, src, r->f[RC_NOTE]);
  if (memo) lstrcpynW(memo->note, own, 200);
  if (level >= RT_DEPTH) {
    free(el);
    if (memo) memo->row0 = myRow, memo->row1 = j->n;
    return;
  }
  double tot = r->qtyTot;
  wchar_t myDes[200];
  lstrcpynW(myDes, o.des, 200);
  if (j->split) {
    /* Раздача: веток у корня много (8 и больше) — задачи они; мало — их
       состав (уровень 2), иначе потоки простаивают на одной большой ветке */
    if (level == 0) j->splitDepth = ne >= 8 ? 1 : 2;
    if (level + 1 >= j->splitDepth) {
      if (!j->tasks) j->tasks = calloc(RT_MAX, sizeof(RtTask));
      for (int i = 0; i < ne && j->tasks && j->ntask < RT_MAX; i++) {
        RtTask *t = &((RtTask *)j->tasks)[j->ntask];
        t->el = el[i];
        t->level = level + 1;
        t->tot = tot;
        lstrcpynW(t->des, myDes, 200);
        rt_plan_add(j, -(j->ntask + 1));
        j->ntask++;
      }
      free(el);
      return;
    }
  }
  for (int i = 0; i < ne && !rt_late(j); i++)
    rt_walk(dbc, el[i].child, level + 1, myDes, el[i].qty, el[i].qtyFound, tot, el[i].section, &el[i], rows, j);
  free(el);
  if (memo) memo->row0 = myRow, memo->row1 = j->n;
}

/* найти изделие по обозначению: у кого есть карточки (техсостав, ТП, заготовка) */
/* Найти изделие по обозначению — тем же запросом, что обычный поиск PLM:
   только среди объектов-изделий (их шаблоны), по имени или атрибуту
   Designation. Без отбора по шаблону сервер перебирает атрибуты всей базы и
   не укладывается в ожидание (так и было в 2026.09.23.45). Из найденного —
   точное обозначение важнее, потом у кого больше карточек. */
static long rt_find_root(SQLHDBC dbc, const wchar_t *des, CardRow *rows, RtJob *j) {
  wchar_t pat[420], sql[3800], err[280];
  like_escape(des, pat, 420);
  _snwprintf(sql, 3800,
             L"SELECT TOP 60 "
             L"CASE WHEN o0.TemplateId=1794 AND ISNULL(o0.ParentId,0)<>0 THEN o0.ParentId ELSE o0.InfoObjectId END, "
             L"o0.Name, N'', o0.TemplateId, 0 "
             L"FROM InfoObjects AS o0 WITH(NOLOCK) "
             L"WHERE o0.Erased=0 AND ("
             L"o0.TemplateId IN (1767) OR o0.TemplateId IN (20,39) OR o0.TemplateId IN (633) "
             L"OR (o0.TemplateId IN (1794) AND o0.InfoObjectId IN ("
             L"SELECT a0.OwnerId FROM InfoObjectAttributes AS a0 WITH(NOLOCK) "
             L"WHERE a0.DataType=3 AND a0.Outdated=0 AND a0.CollectionElementId IS NULL "
             L"AND a0.NameKeyId=1739 AND a0.Indexed=1 AND a0.BoolValue=1))"
             L") AND (o0.Name LIKE N'%s' ESCAPE '\\' COLLATE Cyrillic_General_CI_AS "
             L"OR EXISTS (SELECT 1 FROM InfoObjectAttributes AS ad WITH(NOLOCK) "
             L"JOIN NameKeys AS nkd WITH(NOLOCK) ON nkd.NameKeyId=ad.NameKeyId "
             L"WHERE ad.OwnerId=o0.InfoObjectId AND ad.Outdated=0 AND nkd.Value=N'Designation' "
             L"AND ad.ShortText LIKE N'%s' ESCAPE '\\' COLLATE Cyrillic_General_CI_AS)) "
             L"OPTION(MAXDOP 0)",
             pat, pat);
  sql[3799] = 0;
  int save = g_qTimeout;
  g_qTimeout = 90; /* как обычный поиск — он идёт без предела и укладывается */
  int n = card_query(dbc, sql, rows, 60, err, 280);
  g_qTimeout = save;
  if (n < 0) {
    _snwprintf(j->err, 400, L"Поиск изделия не выполнился: %s", err);
    return 0;
  }
  if (n == 0) {
    _snwprintf(j->err, 400, L"В PLM нет изделия с обозначением «%s».", des);
    return 0;
  }
  long cand[60];
  int exact[60], nc = 0;
  for (int i = 0; i < n && nc < 60; i++) {
    BOOL dup = FALSE;
    for (int k = 0; k < nc && !dup; k++) dup = cand[k] == rows[i].n1;
    if (dup) continue;
    wchar_t d[200];
    card_des_from_name(rows[i].s1, d, 200);
    exact[nc] = !_wcsicmp(d, des);
    cand[nc++] = rows[i].n1;
  }
  wchar_t list[1400];
  size_t il = 0;
  list[0] = 0;
  for (int i = 0; i < nc; i++) {
    int w = _snwprintf(list + il, 1400 - il, il ? L",%ld" : L"%ld", cand[i]);
    if (w > 0 && il + (size_t)w < 1390) il += (size_t)w;
  }
  /* у кого больше карточек — то и изделие (а не документ с тем же номером) */
  _snwprintf(sql, 3800,
             L"SELECT TOP 200 x.Id, nkp.Value, N'', 0, 0 " PLM_HOLDERS(L"%s")
             L"AND nkp.Value IN (N'TechCompCard',N'TechnologicalProcessesCard',N'ProductPreformsCard')",
             list);
  sql[3799] = 0;
  int m = card_query(dbc, sql, rows, 200, err, 280);
  if (m < 0) rt_log(j, L"Карточки найденных: запрос не выполнился — %s\r\n", err);
  long best = cand[0];
  int bestScore = -1;
  for (int i = 0; i < nc; i++) {
    int sc = exact[i] ? 100 : 0;
    for (int k = 0; k < m; k++)
      if (rows[k].n1 == cand[i]) sc += !_wcsicmp(rows[k].s1, L"TechCompCard") ? 3 : 1;
    if (sc > bestScore) {
      bestScore = sc;
      best = cand[i];
    }
  }
  rt_log(j, L"Найдено изделий: %d, взят %ld%s\r\n\r\n", nc, best, bestScore >= 100 ? L" (обозначение совпало)" : L"");
  return best;
}

/* ---- параллельный обход ----------------------------------------------------
   Каждая позиция — десяток-другой запросов, и они идут по очереди: на большой
   сборке это минуты. Ветки состава корня друг от друга не зависят — их берут
   RT_WORKERS потоков, у каждого своё подключение к PLM и своя «ведомость»
   (строки, журнал); ветки разбираются по очереди номеров, а потом строки и
   журнал сшиваются в порядке состава — как при обходе подряд. */
#define RT_WORKERS 4

typedef struct {
  int worker, row0, row1, log0, log1;
  BOOL done;
} RtSeg;

typedef struct {
  RtJob *main, *w;
  volatile LONG *next;
  RtSeg *seg;
  int idx;
} RtWork;

static RtJob *rt_job_new(void);
static void rt_job_free(RtJob *j);

static void rt_branch(SQLHDBC dbc, RtJob *j, RtJob *w, int i, RtSeg *seg, CardRow *rows) {
  RtTask *t = &((RtTask *)j->tasks)[i];
  seg->row0 = w->n;
  seg->log0 = w->logLen;
  rt_walk(dbc, t->el.child, t->level, t->des, t->el.qty, t->el.qtyFound, t->tot, t->el.section, &t->el, rows, w);
  seg->row1 = w->n;
  seg->log1 = w->logLen;
  seg->done = TRUE;
}

static DWORD WINAPI rt_worker(LPVOID param) {
  RtWork *k = (RtWork *)param;
  RtJob *j = k->main, *w = k->w;
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[280];
  if (!plm_connect(&env, &dbc, err, 280)) return 0; /* ветки возьмут другие потоки */
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 300);
  g_qTimeout = 30;
  while (rows && !rt_late(j)) {
    LONG i = InterlockedIncrement(k->next) - 1;
    if (i >= j->ntask) break;
    k->seg[i].worker = k->idx;
    rt_branch(dbc, j, w, (int)i, &k->seg[i], rows);
  }
  free(rows);
  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);
  return 0;
}

static void rt_log_raw(RtJob *j, const wchar_t *s, int n) {
  if (!j->log || n <= 0) return;
  if (n > RT_LOG - 1 - j->logLen) n = RT_LOG - 1 - j->logLen;
  if (n <= 0) return;
  memcpy(j->log + j->logLen, s, (size_t)n * sizeof(wchar_t));
  j->logLen += n;
  j->log[j->logLen] = 0;
}

static void rt_walk_parallel(SQLHDBC dbc, RtJob *j, CardRow *rows) {
  int nt = j->ntask, nw = nt < RT_WORKERS ? nt : RT_WORKERS;
  RtSeg *seg = (RtSeg *)calloc((size_t)nt, sizeof(RtSeg));
  RtWork work[RT_WORKERS];
  HANDLE th[RT_WORKERS];
  int started = 0;
  volatile LONG next = 0;
  ULONGLONG t0 = GetTickCount64();
  for (int k = 0; seg && k < nw; k++) {
    RtJob *w = rt_job_new();
    if (!w) break;
    w->deadline = j->deadline;
    w->cancel = j->cancel;
    w->notify = j->notify;
    w->shared = j->shared;
    work[started].main = j;
    work[started].w = w;
    work[started].next = &next;
    work[started].seg = seg;
    work[started].idx = started;
    th[started] = CreateThread(NULL, 0, rt_worker, &work[started], 0, NULL);
    if (!th[started]) {
      rt_job_free(w);
      break;
    }
    started++;
  }
  if (started) WaitForMultipleObjects((DWORD)started, th, TRUE, INFINITE);
  for (int k = 0; k < started; k++) CloseHandle(th[k]);
  /* что потоки не взяли (не подключились) — здесь же, по очереди */
  RtJob *rest = NULL;
  for (int i = 0; seg && i < nt && !rt_late(j); i++)
    if (!seg[i].done) {
      if (!rest) rest = rt_job_new();
      if (!rest) break;
      rest->deadline = j->deadline;
      rest->cancel = j->cancel;
      rest->notify = j->notify;
      rest->shared = j->shared;
      seg[i].worker = started;
      rt_branch(dbc, j, rest, i, &seg[i], rows);
    }
  /* сшить по плану: строки верхних уровней и ветки — в порядке состава */
  RtRow *out = seg ? (RtRow *)calloc(RT_MAX, sizeof(RtRow)) : NULL;
  int n = 0;
  for (int p = 0; out && p < j->nplan; p++) {
    int v = j->plan[p];
    if (v >= 0) {
      if (n < RT_MAX) out[n++] = j->rows[v];
      continue;
    }
    int i = -v - 1;
    if (i >= nt || !seg[i].done) continue;
    RtJob *w = seg[i].worker < started ? work[seg[i].worker].w : rest;
    for (int r = seg[i].row0; r < seg[i].row1 && n < RT_MAX; r++) out[n++] = w->rows[r];
    rt_log_raw(j, w->log + seg[i].log0, seg[i].log1 - seg[i].log0);
  }
  if (out) {
    memcpy(j->rows, out, sizeof(RtRow) * (size_t)n);
    j->n = n;
    free(out);
  }
  for (int k = 0; k < started; k++) {
    j->kdUsed += work[k].w->kdUsed;
    rt_job_free(work[k].w);
  }
  if (rest) {
    j->kdUsed += rest->kdUsed;
    rt_job_free(rest);
  }
  rt_log(j, L"\r\nВетвей — задач потокам %d (с уровня %d), потоков %d — %.0f с\r\n", nt, j->splitDepth,
         started ? started : 1, (double)(GetTickCount64() - t0) / 1000.0);
  free(seg);
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
    g_qTimeout = 30; /* на один запрос; не уложился — позиция с пометкой, сбор идёт дальше */
    long root = j->rootId;
    if (root) rt_log(j, L"Изделие — строка из поиска PLM: %ld\r\n\r\n", root);
    else root = rt_find_root(dbc, j->des, rows, j);
    volatile LONG prog = 0;
    j->shared = &prog;
    j->split = !j->dump; /* проверочная выгрузка — по-старому, подряд */
    if (root) rt_walk(dbc, root, 0, j->order, 1, TRUE, 1, NULL, NULL, rows, j);
    if (j->ntask) rt_walk_parallel(dbc, j, rows);
    free(j->tasks);
    free(j->plan);
    j->tasks = NULL;
    j->plan = NULL;
    j->ntask = j->nplan = 0;
    j->shared = NULL;
    j->split = FALSE;
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
  free(j->memo);
  free(j->tasks);
  free(j->plan);
  free(j->rows);
  free(j->log);
  free(j);
}

