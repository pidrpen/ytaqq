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
       ссылается на входящее изделие. Материалы, стандартные и прочие
       изделия (без своего обозначения или карточек изделия) — строкой
       состава: наименование, вид, количество; ТП, заготовку и состав у них
       не ищем (с 2026.09.23.95; прежде в ведомость не шли). Документы — мимо;
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
/* Строк ведомости — до RT_MAX (с 2026.09.23.85; прежде 1000, и большая
   сборка, где подсборка входит десятки раз, обрезалась; с .86 — 50 000). Память — по мере
   надобности (rt_row_add), а не сразу на все. Глубина — до RT_DEPTH уровней
   (было 8), строк в составе одного узла — до RT_KIDS (было 150). */
#define RT_MAX 50000
#define RT_DEPTH 64 /* с .87: цикл (изделие входит само в себя) ловится по цепочке «входит в»;
                       глубина — только последняя страховка */
#define RT_KIDS 300
#define RT_NCOL 15
#define RT_LOG 3000000 /* с .86 — до 3 млн знаков: у изделия из тысяч позиций журнал длинный */

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
  BOOL light;            /* материал, стандартное или прочее изделие — только строка состава
                            (с .95): листам по позициям (операции, заготовки, извещения) не нужна */
  wchar_t tpName[200];
} RtRow;

typedef struct {
  wchar_t des[200], order[120];
  long rootId; /* строка, выбранная в поиске PLM: искать изделие не нужно */
  int n, cap; /* строк и под сколько выделено */
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
  BOOL split;             /* до .86 — раздача веток верхних уровней; теперь всегда FALSE */
  volatile LONG *shared;  /* общий счётчик позиций — для строки состояния */
  volatile LONG progDone, progTotal; /* полоса хода (с .84): сделано из всего на этапе */
  volatile LONG prog2Done, prog2Total; /* обход (с .89): задачи «ТП и заготовка» — отдельно от позиций */
  void *memo;             /* RtMemo[nmemo] — уже собранные позиции (с .83) */
  int nmemo, memoCap;
  BOOL fullShown;         /* «строк больше RT_MAX» — в подробности один раз */
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
  if (!j || !j->log || j->logLen >= RT_LOG - 2) return; /* без журнала (карточка) — молча */
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

/* место под строки: до need, не больше RT_MAX; FALSE — предел или нет памяти */
static BOOL rt_rows_reserve(RtJob *j, int need) {
  if (need <= j->cap) return TRUE;
  if (need > RT_MAX) return FALSE;
  int cap = j->cap ? j->cap : 256;
  while (cap < need) cap *= 2;
  if (cap > RT_MAX) cap = RT_MAX;
  RtRow *r = (RtRow *)realloc(j->rows, sizeof(RtRow) * (size_t)cap);
  if (!r) return FALSE;
  j->rows = r;
  j->cap = cap;
  return TRUE;
}

/* новая строка в конце (обнулённая); NULL — строк уже RT_MAX. Указатели на
   строки после неё могут смениться (realloc) — держать номер, а не адрес */
static RtRow *rt_row_add(RtJob *j) {
  if (!rt_rows_reserve(j, j->n + 1)) return NULL;
  RtRow *r = &j->rows[j->n++];
  memset(r, 0, sizeof(*r));
  return r;
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
  L"FROM InfoObjects WITH(NOLOCK) WHERE InfoObjectId IN (" id L")) AS x "                    \
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
               L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId IN (la.AttributeId, ISNULL(la.Link,0)) "
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

/* Для карточки и столбца «Заготовка» в поиске (с 2026.09.23.84): та же
   заготовка, что в выгрузке, — в сводку PfSum карточки. FALSE — полей нет,
   тогда карточка обходит заготовку по-старому (pf_explore). */
static BOOL pf_read_full(SQLHDBC dbc, long pfId, PfSum *sm) {
  RtPf pf;
  rt_pf_read(dbc, pfId, &pf, NULL);
  if (!pf.mat[0] && !pf.dims[0] && !pf.sort[0] && !pf.hasNorm) return FALSE;
  if (pf.mat[0]) { /* MaterialName — сортамент с маркой, как в PLM: «Круг 20 ГОСТ… / 45 ГОСТ…» */
    lstrcpynW(sm->sort, pf.mat, 240);
    sm->sortScore = 10;
    sm->mat[0] = 0;
  }
  if (pf.dims[0] || pf.sort[0]) {
    sm->zd[0] = sm->zl[0] = sm->za[0] = 0; /* размеры — уже строкой */
    int w = _snwprintf(sm->dims, 240, L"Габариты %s", pf.dims[0] ? pf.dims : pf.sort);
    if (pf.add[0] && wcscmp(pf.add, L"0") && w > 0 && w < 230) _snwprintf(sm->dims + w, 240 - w, L", припуск %s", pf.add);
    sm->dims[239] = 0;
    sm->ndims = 1;
    sm->dimsFixed = TRUE;
  }
  if (pf.hasNorm) {
    wchar_t v[40];
    rt_fmt(pf.normV, 3, v, 40);
    if (pf.unit[0] && _wcsnicmp(pf.unit, L"кг", 2)) _snwprintf(sm->mass, 80, L"%s %s", v, pf.unit); /* не кг — как в PLM */
    else lstrcpynW(sm->mass, v, 80); /* число — pf_mass_text допишет «кг» */
    sm->mass[79] = 0;
    sm->massFixed = TRUE;
  }
  return TRUE;
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

static int rt_children_fill(SQLHDBC dbc, CardRow *rows, int n, BOOL kd, RtJob *j, RtEl *el, int max, long *variant);

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
  free(sql);
  if (n <= 0) return 0;
  return rt_children_fill(dbc, rows, n, kd, j, el, max, variant);
}

/* строки состава (n1 — строка, n2 — вариант техсостава) → входящие с количеством,
   разделом, единицей; по порядку редактора PLM. kd — состав по КД */
static int rt_children_fill(SQLHDBC dbc, CardRow *rows, int n, BOOL kd, RtJob *j, RtEl *el, int max, long *variant) {
  wchar_t *sql = (wchar_t *)malloc(4000 * sizeof(wchar_t));
  wchar_t err[280];
  if (!sql) return 0;
  if (kd)
    for (int i = 0; i < n; i++) rows[i].n2 = 0; /* у состава КД нет исполнения техсостава */
  *variant = rows[0].n2; /* исполнение техсостава — у него и список изменений КД */
  int ne = n > max ? max : n;
  long ids[RT_KIDS];
  int used = 0;
  for (int i = 0; i < ne && used < RT_KIDS; i++) ids[used++] = rows[i].n1;
  /* поля строк — пачками по 100 строк (с .85: строк в составе бывает и 300,
     а одним запросом поля обрезались по TOP) */
  CardRow *ar = (CardRow *)malloc(sizeof(CardRow) * 3000);
  int m = ar ? 0 : -1;
  for (int c0 = 0; ar && c0 < used && m >= 0; c0 += 100) {
    wchar_t list[1400];
    size_t il = 0;
    list[0] = 0;
    for (int i = c0; i < used && i < c0 + 100; i++) {
      int w = _snwprintf(list + il, 1400 - il, il ? L",%ld" : L"%ld", ids[i]);
      if (w <= 0 || il + (size_t)w >= 1390) break;
      il += (size_t)w;
    }
    _snwprintf(sql, 4000,
               L"SELECT TOP 1000 a.CollectionElementId, nk.Value, COALESCE(lo.Name, " CARD_VALUE_SQL L", N''), "
               L"ISNULL(a.Link,0), a.DataType "
               L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
               L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
               L"WHERE a.CollectionElementId IN (%s) AND a.Outdated=0 "
               L"ORDER BY a.CollectionElementId, a.DataType DESC, nk.Value",
               list);
    int k = card_query(dbc, sql, ar + m, 3000 - m > 1000 ? 1000 : 3000 - m, err, 280);
    if (k < 0) m = -1;
    else m += k;
  }
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

/* материал, стандартное или прочее изделие — строкой состава (с 2026.09.23.95;
   прежде такие строки пропускались как «материал?»): обозначение — только своё,
   наименование — имя объекта (у нормалей и материалов в нём всё: «Болт
   М8-6gx20 ГОСТ 7798-70», «Круг 20 ГОСТ 2590-2006»), вид — раздел строки
   состава. ТП, заготовку и состав у них не ищем. Место в дереве (уровень,
   кол-во, «входит в») заполняет вызывающий. */
static void rt_light_row(const RtObj *o, const wchar_t *elSection, RtRow *r) {
  memset(r, 0, sizeof(*r));
  r->light = TRUE;
  r->id = o->id;
  r->par = o->par;
  r->prodConf = o->prodConf;
  BOOL ownDes = o->desAttr && rt_looks_des(o->des);
  if (ownDes) lstrcpynW(r->f[RC_DES], o->des, 200);
  lstrcpynW(r->f[RC_NAME], ownDes || !o->objName[0] ? o->name : o->objName, 200);
  const wchar_t *kind = elSection && elSection[0] ? elSection : (o->section[0] ? o->section : L"Прочие изделия");
  lstrcpynW(r->f[RC_KIND], kind, 200);
  double v;
  if (o->mass[0] && rt_num(o->mass, &v)) rt_fmt(v, 3, r->f[RC_MASS], 200);
}

/* без своего обозначения или карточек изделия, или в разделе «Материалы» —
   не деталь и не сборка: строка состава без ТП и заготовки */
static BOOL rt_is_light(const RtObj *o, const wchar_t *elSection) {
  BOOL product = o->desAttr || o->tpCard || o->pfCard || o->tcCard;
  return !product || !rt_looks_des(o->des) || (elSection && wcsstr(elSection, L"атериал"));
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
    long ver = av;
    long vn = 0;
    if (!ver) { /* нет утверждённой — рабочая, как в модуле PLM (с .84; прежде — последняя по id, бывал снимок) */
      wchar_t *sql = (wchar_t *)malloc(4000 * sizeof(wchar_t)), err[280];
      CardRow vers[8];
      int nv = sql ? card_tc_versions(dbc, o->tcCard, sql, vers, 8, err) : -1;
      free(sql);
      if (nv > 0) ver = vers[0].n1, vn = vers[0].n2;
      else if (nk) ver = ids[nk - 1];
    }
    if (ver) {
      wchar_t lab[80];
      if (av) lstrcpynW(lab, L"версия техсостава (утверждённая)", 80);
      else _snwprintf(lab, 80, L"версия техсостава (%s №%ld)", vn > 0 ? L"рабочая" : L"не утверждена,", vn);
      lab[79] = 0;
      rt_dump_obj(dbc, ver, lab, rows, j, NULL);
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
  int row0, row1; /* строки в rows; row0 < 0 — позиция пропущена (документ) */
  wchar_t note[200];
} RtMemo;

static RtMemo *rt_memo_find(RtJob *j, long id, long pc) {
  RtMemo *m = (RtMemo *)j->memo;
  for (int i = 0; m && i < j->nmemo; i++)
    if (m[i].id == id && m[i].pc == pc) return &m[i];
  return NULL;
}

/* номер новой записи или -1; номер, а не адрес: список растёт (realloc) */
static int rt_memo_add(RtJob *j, long id, long pc) {
  if (j->dump || j->split) return -1; /* в раздаче строки ещё не на месте; проверочная — подробно */
  if (j->nmemo >= j->memoCap) {
    int cap = j->memoCap ? j->memoCap * 2 : 256;
    RtMemo *m = (RtMemo *)realloc(j->memo, sizeof(RtMemo) * (size_t)cap);
    if (!m) return -1;
    j->memo = m;
    j->memoCap = cap;
  }
  RtMemo *m = &((RtMemo *)j->memo)[j->nmemo];
  memset(m, 0, sizeof(*m));
  m->id = id;
  m->pc = pc;
  m->row0 = m->row1 = -1;
  return j->nmemo++;
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
  if (cnt <= 0 || !rt_rows_reserve(j, j->n + cnt)) return;
  const RtRow *f0 = &j->rows[m->row0]; /* после reserve: строки могли переехать */
  double newTot = qty * parentTot;
  double factor = f0->qtyTot != 0 ? newTot / f0->qtyTot : 0;
  int dl = level - f0->level;
  rt_log(j, L"%*s%s %s — повтор: из уже собранного (строк %d), кол-во ", level * 2, L"", f0->f[RC_DES],
         f0->f[RC_NAME], cnt);
  for (int k = 0; k < cnt; k++) {
    RtRow *d = &j->rows[j->n++]; /* место уже есть (reserve выше) */
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

/* предел строк — сказать один раз, что собрано не всё */
static void rt_full(RtJob *j) {
  if (j->fullShown) return;
  j->fullShown = TRUE;
  rt_log(j, L"\r\n!!! Строк ведомости больше %d — дальше не собираю: собрано не всё.\r\n", RT_MAX);
  if (!j->err[0]) _snwprintf(j->err, 400, L"Строк больше %d — собрано не всё (см. «Подробности»)", RT_MAX);
}

static void rt_walk(SQLHDBC dbc, long id, int level, const wchar_t *parentDes, double qty, BOOL qtyFound,
                    double parentTot, const wchar_t *elSection, const RtEl *src, CardRow *rows, RtJob *j) {
  if (j->n >= RT_MAX) rt_full(j);
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
  int mi = rt_memo_add(j, id, keyPc);
#define RT_MEMO (mi >= 0 ? &((RtMemo *)j->memo)[mi] : NULL)
  RtObj o;
  if (!rt_obj(dbc, id, &o, rows, j)) return;
  /* исполнение — то, на которое ссылается строка техсостава: у «-01» свой
     техсостав и ТП, а по ссылкам изделия нашлось бы базовое исполнение */
  if (src && src->pc && src->pc != o.prodConf) {
    rt_log(j, L"    исполнение — из строки техсостава #%ld (по изделию было #%ld)\r\n", src->pc, o.prodConf);
    o.prodConfObj = o.prodConf;
    o.prodConf = src->pc;
  }
  if (level > 0 && (rt_is_doc(o.des, o.objName) || (elSection && wcsstr(elSection, L"окумент")))) {
    rt_log(j, L"  %*s· пропуск «%s» — документ\r\n", level * 2, L"", o.objName);
    return; /* memo->row0 < 0: повтор — тоже пропуск */
  }
  int myRow = j->n;
  /* материал, стандартное или прочее изделие — строкой, без ТП, заготовки и состава */
  if (level > 0 && rt_is_light(&o, elSection)) {
    RtRow *r = rt_row_add(j);
    if (!r) {
      rt_full(j);
      return;
    }
    rt_light_row(&o, elSection, r);
    r->level = level;
    r->qty = qty;
    r->qtyTot = qty * parentTot;
    lstrcpynW(r->f[RC_PARENT], parentDes, 200);
    rt_fmt(r->qty, 3, r->f[RC_QTY], 200);
    rt_fmt(r->qtyTot, 3, r->f[RC_QTYTOT], 200);
    rt_src_notes(level, qtyFound, src, r->f[RC_NOTE]);
    rt_log(j, L"%*s%s «%s» (%ld), кол-во %s — %s: строкой, без ТП и заготовки\r\n", level * 2, L"", r->f[RC_DES],
           r->f[RC_NAME], id, r->f[RC_QTY], r->f[RC_KIND]);
    if (j->notify)
      PostMessageW(j->notify, WM_RT_PROGRESS, (WPARAM)(j->shared ? InterlockedIncrement(j->shared) : j->n), 0);
    if (RT_MEMO) RT_MEMO->row0 = myRow, RT_MEMO->row1 = j->n;
    return;
  }
  RtRow *r = rt_row_add(j); /* до обхода детей: дальше строки могут переехать — r только до него */
  if (!r) {
    rt_full(j);
    return;
  }
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
  RtEl *el = (RtEl *)malloc(sizeof(RtEl) * RT_KIDS);
  long variant = 0;
  int kdBefore = j->kdUsed;
  int ne = el ? rt_children(dbc, &o, rows, j, el, RT_KIDS, &variant) : 0;
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
  if (RT_MEMO) lstrcpynW(RT_MEMO->note, own, 200);
  if (level >= RT_DEPTH) {
    if (ne > 0) rt_log(j, L"    !!! глубже %d уровней не спускаюсь — состав «%s» не собран\r\n", RT_DEPTH, o.des);
    free(el);
    if (RT_MEMO) RT_MEMO->row0 = myRow, RT_MEMO->row1 = j->n;
    return;
  }
  double tot = r->qtyTot;
  wchar_t myDes[200];
  lstrcpynW(myDes, o.des, 200);
  for (int i = 0; i < ne && !rt_late(j); i++)
    rt_walk(dbc, el[i].child, level + 1, myDes, el[i].qty, el[i].qtyFound, tot, el[i].section, &el[i], rows, j);
  free(el);
  if (RT_MEMO) RT_MEMO->row0 = myRow, RT_MEMO->row1 = j->n;
#undef RT_MEMO
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

/* ---- пачкой (с 2026.09.23.90) ----------------------------------------------
   Позиция до появления её состава — около девяти запросов (изделие, карточки,
   версия техсостава, строки, их поля…), и N в строке хода растёт со скоростью
   этих запросов. Когда в очереди много позиций, свободный поток берёт сразу
   до RT_BATCH новых и читает их одним запросом на вид данных: свойства и
   карточки, версии техсостава, строки состава, состав по КД. Что пачкой не
   нашлось (техсостав не в первой версии, напрямую по исполнению и т. п.) —
   по-старому, по одной (rt_children), со всеми запасными путями и журналом. */
#define RT_BATCH 16

static void rt_idlist(const long *ids, int n, wchar_t *out, int cap) {
  int w = 0;
  out[0] = 0;
  for (int i = 0; i < n && w < cap - 16; i++) w += _snwprintf(out + w, cap - w, i ? L",%ld" : L"%ld", ids[i]);
}

static int rt_bk(const long *ids, int n, long id) { /* номер в пачке по id */
  for (int k = 0; k < n; k++)
    if (ids[k] == id) return k;
  return -1;
}

/* свойства и карточки пачки изделий — те же запросы, что rt_obj, на всех сразу;
   FALSE — запрос не прошёл, тогда по одной */
static BOOL rt_obj_batch(SQLHDBC dbc, const long *ids, int n, RtObj *o, wchar_t (*note)[300]) {
  wchar_t *sql = (wchar_t *)malloc(16000 * sizeof(wchar_t)), list[RT_BATCH * 14], err[280];
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 80 * RT_BATCH);
  BOOL ok = FALSE;
  long desP[RT_BATCH] = {0};
  if (!sql || !rows) goto out;
  for (int k = 0; k < n; k++) {
    memset(&o[k], 0, sizeof(RtObj));
    note[k][0] = 0;
  }
  rt_idlist(ids, n, list, RT_BATCH * 14);
  _snwprintf(sql, 16000,
             L"SELECT TOP %d o.InfoObjectId, o.Name, N'', ISNULL(o.ParentId,0), 0 FROM InfoObjects AS o WITH(NOLOCK) "
             L"WHERE o.InfoObjectId IN (%s)",
             n, list);
  int m = card_query(dbc, sql, rows, 80 * RT_BATCH, err, 280);
  for (int i = 0; i < m; i++) {
    int k = rt_bk(ids, n, rows[i].n1);
    if (k < 0) continue;
    lstrcpynW(o[k].objName, rows[i].s1, 260);
    o[k].par = rows[i].n2;
  }
  _snwprintf(sql, 16000,
             L"SELECT TOP %d x.Id, nk.Value, COALESCE(lo.Name, " CARD_VALUE_SQL L", N''), ISNULL(a.Link,0), h2.P "
             RT_HOLDERS(L"%s")
             L"AND nk.Value IN (N'Designation',N'Name',N'Section',N'Mass',N'MassMeasureUnit',"
             L"N'TechnologicalProcessesCard',N'ProductPreformsCard',N'TechCompCard',N'ProductConfiguration',"
             L"N'BaselineConfiguration',N'Material',N'MaterialLink') "
             L"LEFT JOIN InfoObjects AS lo WITH(NOLOCK) ON a.DataType=6 AND lo.InfoObjectId=a.Link "
             L"ORDER BY x.Id, h2.P",
             80 * n, list);
  sql[15999] = 0;
  m = card_query(dbc, sql, rows, 80 * RT_BATCH, err, 280);
  if (m < 0) goto out;
  for (int i = 0; i < m; i++) { /* по порядку h2.P: ближнее важнее — первое */
    int k = rt_bk(ids, n, rows[i].n1);
    if (k < 0) continue;
    RtObj *q = &o[k];
    const wchar_t *key = rows[i].s1, *v = rows[i].s2;
    long link = rows[i].n2;
    if (!_wcsicmp(key, L"Designation") && !q->des[0] && v[0]) {
      lstrcpynW(q->des, v, 200);
      q->desAttr = TRUE;
      desP[k] = rows[i].n3;
    }
    else if ((!_wcsicmp(key, L"Material") || !_wcsicmp(key, L"MaterialLink")) && !q->mat[0] && v[0])
      lstrcpynW(q->mat, v, 200);
    else if (!_wcsicmp(key, L"Name") && !q->name[0] && v[0]) lstrcpynW(q->name, v, 260);
    else if (!_wcsicmp(key, L"Section") && !q->section[0] && v[0]) lstrcpynW(q->section, v, 120);
    else if (!_wcsicmp(key, L"Mass") && !q->mass[0] && v[0]) lstrcpynW(q->mass, v, 64);
    else if (!_wcsicmp(key, L"MassMeasureUnit") && !q->massUnit[0] && v[0]) lstrcpynW(q->massUnit, v, 40);
    else if (!_wcsicmp(key, L"TechnologicalProcessesCard") && !q->tpCard) q->tpCard = link;
    else if (!_wcsicmp(key, L"ProductPreformsCard") && !q->pfCard) q->pfCard = link;
    else if (!_wcsicmp(key, L"TechCompCard") && !q->tcCard) q->tcCard = link;
    else if (!_wcsicmp(key, L"ProductConfiguration") && !q->prodConf) q->prodConf = link;
    else if (!_wcsicmp(key, L"BaselineConfiguration") && !q->prodConf) q->prodConf = link;
  }
  for (int k = 0; k < n; k++) {
    o[k].id = ids[k];
    if (desP[k] > 0 && o[k].objName[0]) { /* обозначение родителя или карты — своё имя точнее */
      wchar_t nd[200];
      card_des_from_name(o[k].objName, nd, 200);
      if (rt_looks_des(nd) && _wcsicmp(nd, o[k].des)) {
        _snwprintf(note[k], 300, L"    обозначение «%s» — из связанного объекта, по имени «%s»\r\n", o[k].des, nd);
        lstrcpynW(o[k].des, nd, 200);
      }
    }
  }
  /* карточки — ещё и запросом карточки (PLM_HOLDERS), у кого чего-то нет */
  long need[RT_BATCH];
  int nn = 0;
  for (int k = 0; k < n; k++)
    if (!o[k].pfCard || !o[k].tcCard || !o[k].tpCard || !o[k].prodConf) need[nn++] = ids[k];
  if (nn) {
    rt_idlist(need, nn, list, RT_BATCH * 14);
    _snwprintf(sql, 16000,
               L"SELECT DISTINCT TOP %d x.Id, nkp.Value, N'', pa.Link, 0 " PLM_HOLDERS(L"%s")
               L"AND nkp.Value IN (N'ProductPreformsCard',N'TechCompCard',N'ProductConfiguration',"
               L"N'TechnologicalProcessesCard')",
               20 * nn, list);
    sql[15999] = 0;
    m = card_query(dbc, sql, rows, 80 * RT_BATCH, err, 280);
    for (int i = 0; i < m; i++) {
      int k = rt_bk(ids, n, rows[i].n1);
      if (k < 0) continue;
      const wchar_t *key = rows[i].s1;
      if (!_wcsicmp(key, L"ProductPreformsCard") && !o[k].pfCard) o[k].pfCard = rows[i].n2;
      else if (!_wcsicmp(key, L"TechCompCard") && !o[k].tcCard) o[k].tcCard = rows[i].n2;
      else if (!_wcsicmp(key, L"ProductConfiguration") && !o[k].prodConf) o[k].prodConf = rows[i].n2;
      else if (!_wcsicmp(key, L"TechnologicalProcessesCard") && !o[k].tpCard) o[k].tpCard = rows[i].n2;
    }
  }
  /* карта взаимосвязей ссылается на конфигурацию (Product) — с её стороны */
  wchar_t vals[RT_BATCH * 3 * 30];
  int vw = 0, nv = 0;
  vals[0] = 0;
  for (int k = 0; k < n; k++)
    if (!o[k].pfCard || !o[k].tpCard) {
      long l2 = o[k].prodConf ? o[k].prodConf : ids[k], l3 = o[k].par ? o[k].par : ids[k];
      vw += _snwprintf(vals + vw, RT_BATCH * 90 - vw, L"%s(%ld,%ld),(%ld,%ld),(%ld,%ld)", vw ? L"," : L"", ids[k], ids[k],
                       ids[k], l2, ids[k], l3);
      nv++;
    }
  if (nv) {
    _snwprintf(sql, 16000,
               L"SELECT DISTINCT TOP %d v.O, nkc.Value, N'', ca.Link, 0 FROM (VALUES %s) AS v(O,L) "
               L"JOIN InfoObjectAttributes AS pr WITH(NOLOCK) ON pr.Link=v.L AND pr.Outdated=0 "
               L"AND ISNULL(pr.CollectionElementId,0)=0 "
               L"JOIN NameKeys AS nkpr WITH(NOLOCK) ON nkpr.NameKeyId=pr.NameKeyId AND nkpr.Value=N'Product' "
               L"JOIN InfoObjectAttributes AS ca WITH(NOLOCK) ON ca.OwnerId=pr.OwnerId AND ca.Outdated=0 "
               L"AND ISNULL(ca.Link,0)<>0 AND ISNULL(ca.CollectionElementId,0)=0 "
               L"JOIN NameKeys AS nkc WITH(NOLOCK) ON nkc.NameKeyId=ca.NameKeyId "
               L"AND nkc.Value IN (N'ProductPreformsCard',N'TechCompCard',N'TechnologicalProcessesCard')",
               20 * nv, vals);
    sql[15999] = 0;
    m = card_query(dbc, sql, rows, 80 * RT_BATCH, err, 280);
    int got[RT_BATCH] = {0};
    for (int i = 0; i < m; i++) {
      int k = rt_bk(ids, n, rows[i].n1);
      if (k < 0) continue;
      const wchar_t *key = rows[i].s1;
      if (!_wcsicmp(key, L"ProductPreformsCard") && !o[k].pfCard) o[k].pfCard = rows[i].n2, got[k]++;
      else if (!_wcsicmp(key, L"TechCompCard") && !o[k].tcCard) o[k].tcCard = rows[i].n2, got[k]++;
      else if (!_wcsicmp(key, L"TechnologicalProcessesCard") && !o[k].tpCard) o[k].tpCard = rows[i].n2, got[k]++;
    }
    for (int k = 0; k < n; k++)
      if (got[k]) {
        size_t l = wcslen(note[k]);
        _snwprintf(note[k] + l, 300 - l, L"    карточки %ld: нашлись по обратной ссылке (Product)\r\n", ids[k]);
      }
  }
  for (int k = 0; k < n; k++) {
    if (!o[k].des[0] && o[k].objName[0]) card_des_from_name(o[k].objName, o[k].des, 200);
    if (!o[k].name[0] && o[k].objName[0]) card_title_from_name(o[k].objName, o[k].name, 260);
  }
  ok = TRUE;
out:
  free(sql);
  free(rows);
  return ok;
}

/* состав пачки: что нашлось сразу — строки; лист без состава — 0; прочее — по одной */
enum { RT_PRE_SINGLE, RT_PRE_ROWS, RT_PRE_LEAF };
typedef struct {
  int how;
  BOOL kd;
  CardRow *rows;
  int n;
  wchar_t note[300];
} RtPre;

static void rt_pre_take(RtPre *pre, int k, const CardRow *rows, int m, BOOL kd) {
  int c = 0;
  for (int i = 0; i < m; i++)
    if (rows[i].n3 == k) c++;
  if (!c) return;
  if (c > 300) c = 300;
  pre[k].rows = (CardRow *)malloc(sizeof(CardRow) * (size_t)c);
  if (!pre[k].rows) return;
  for (int i = 0; i < m && pre[k].n < c; i++)
    if (rows[i].n3 == k) pre[k].rows[pre[k].n++] = rows[i];
  pre[k].how = RT_PRE_ROWS;
  pre[k].kd = kd;
}

static void rt_children_batch(SQLHDBC dbc, RtObj **o, const BOOL *use, int n, RtPre *pre, BOOL tcDiag) {
  wchar_t *sql = (wchar_t *)malloc(20000 * sizeof(wchar_t)), err[280], list[RT_BATCH * 14], vals[RT_BATCH * 60];
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 300 * RT_BATCH);
  for (int k = 0; k < n; k++) memset(&pre[k], 0, sizeof(RtPre));
  if (!sql || !rows) goto out;
  /* А. с карточкой техсостава: версия — как в rt_children (утверждённая, иначе рабочая) */
  long cards[RT_BATCH];
  int nc = 0;
  for (int k = 0; k < n; k++)
    if (use[k] && o[k]->tcCard && rt_bk(cards, nc, o[k]->tcCard) < 0) cards[nc++] = o[k]->tcCard;
  if (nc) {
    rt_idlist(cards, nc, list, RT_BATCH * 14);
    _snwprintf(sql, 20000,
               L"SELECT TOP %d c.V, CAST(vo.Name AS NVARCHAR(200)), c.K, ISNULL(vn.N,0), c.C "
               L"FROM (SELECT a.Link AS V, nk.Value AS K, a.OwnerId AS C FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
               L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 AND a.DataType=6 AND ISNULL(a.Link,0)<>0 "
               L"AND ISNULL(a.CollectionElementId,0)=0 "
               L"UNION SELECT o.InfoObjectId, N'', o.ParentId FROM InfoObjects AS o WITH(NOLOCK) "
               L"WHERE o.ParentId IN (%s) AND o.Erased=0) AS c "
               L"JOIN InfoObjects AS vo WITH(NOLOCK) ON vo.InfoObjectId=c.V AND vo.Erased=0 "
               L"OUTER APPLY (SELECT TOP 1 vv.IntegerNumber AS N FROM InfoObjectAttributes AS vv WITH(NOLOCK) "
               L"JOIN NameKeys AS nkv WITH(NOLOCK) ON nkv.NameKeyId=vv.NameKeyId AND nkv.Value=N'VersionNumber' "
               L"WHERE vv.OwnerId=c.V AND vv.Outdated=0) AS vn "
               L"WHERE EXISTS (SELECT 1 FROM InfoObjects AS ch WITH(NOLOCK) "
               L"JOIN InfoObjectAttributes AS t WITH(NOLOCK) ON t.OwnerId=ch.InfoObjectId AND t.Outdated=0 "
               L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=t.NameKeyId AND nkt.Value=N'TechComposition' "
               L"WHERE ch.ParentId=c.V AND ch.Erased=0) "
               L"ORDER BY c.C, CASE WHEN c.K=N'ActualVersionTechComp' THEN 0 WHEN ISNULL(vn.N,0)<0 THEN 2 ELSE 1 END, "
               L"ISNULL(vn.N,0) DESC, c.V DESC",
               40 * nc, list, list);
    int nvr = card_query(dbc, sql, rows, 300 * RT_BATCH, err, 280);
    long ver[RT_BATCH] = {0}, vnum[RT_BATCH] = {0};
    BOOL actual[RT_BATCH] = {0};
    wchar_t vname[RT_BATCH][120];
    int vw = 0;
    vals[0] = 0;
    for (int k = 0; k < n; k++) {
      vname[k][0] = 0;
      if (!use[k] || !o[k]->tcCard) continue;
      for (int i = 0; i < nvr; i++) /* первая по порядку — версия этой карточки */
        if (rows[i].n3 == o[k]->tcCard) {
          ver[k] = rows[i].n1;
          vnum[k] = rows[i].n2;
          actual[k] = !_wcsicmp(rows[i].s2, L"ActualVersionTechComp");
          lstrcpynW(vname[k], rows[i].s1, 120);
          break;
        }
      if (ver[k]) vw += _snwprintf(vals + vw, RT_BATCH * 60 - vw, L"%s(%d,%ld,%ld)", vw ? L"," : L"", k, ver[k], o[k]->prodConf);
    }
    if (vw) {
      _snwprintf(sql, 20000,
                 L"SELECT DISTINCT TOP %d ce.CollectionElementId, N'', N'', ch.InfoObjectId, v.K "
                 L"FROM (VALUES %s) AS v(K,V,P) "
                 L"JOIN InfoObjects AS ch WITH(NOLOCK) ON ch.ParentId=v.V AND ch.Erased=0 "
                 L"JOIN InfoObjectAttributes AS tc WITH(NOLOCK) ON tc.OwnerId=ch.InfoObjectId AND tc.Outdated=0 "
                 L"JOIN NameKeys AS nktc WITH(NOLOCK) ON nktc.NameKeyId=tc.NameKeyId AND nktc.Value=N'TechComposition' "
                 L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId IN (tc.AttributeId, ISNULL(tc.Link,0)) "
                 L"AND ce.Outdated=0 "
                 L"WHERE (v.P=0 OR EXISTS (SELECT 1 FROM InfoObjectAttributes AS pr WITH(NOLOCK) "
                 L"JOIN NameKeys AS nkpr WITH(NOLOCK) ON nkpr.NameKeyId=pr.NameKeyId AND nkpr.Value=N'Product' "
                 L"WHERE pr.OwnerId=ch.InfoObjectId AND pr.Outdated=0 AND pr.Link=v.P)) "
                 L"AND NOT EXISTS (SELECT 1 FROM InfoObjectAttributes AS ir WITH(NOLOCK) "
                 L"JOIN NameKeys AS nkr WITH(NOLOCK) ON nkr.NameKeyId=ir.NameKeyId AND nkr.Value=N'IsRemoved' "
                 L"WHERE ir.CollectionElementId=ce.CollectionElementId AND ir.BoolValue=1) "
                 L"ORDER BY v.K, ce.CollectionElementId",
                 300 * n, vals);
      int m = card_query(dbc, sql, rows, 300 * RT_BATCH, err, 280);
      for (int k = 0; k < n && m > 0; k++) {
        if (!ver[k]) continue;
        rt_pre_take(pre, k, rows, m, FALSE);
        if (pre[k].how == RT_PRE_ROWS && !actual[k])
          _snwprintf(pre[k].note, 300, L"    техсостав не утверждён — беру %s версию №%ld «%s» (#%ld)\r\n",
                     vnum[k] > 0 ? L"рабочую" : L"снимок, ", vnum[k], vname[k], ver[k]);
      }
    }
  }
  /* Б. без карточки техсостава: напрямую по исполнению есть вариант — по одной;
     нет — состав по КД (Items); нет и его — позиция без состава */
  int vw = 0;
  vals[0] = 0;
  BOOL firstLeft = !tcDiag; /* почему не нашёлся — разок по одной, с её журналом */
  for (int k = 0; k < n; k++) {
    if (!use[k] || o[k]->tcCard) continue;
    if (firstLeft) {
      firstLeft = FALSE;
      continue;
    }
    pre[k].how = RT_PRE_LEAF; /* пока так; найдётся вариант или КД — поправим */
    long pc1 = o[k]->prodConf ? o[k]->prodConf : o[k]->prodConfObj, pc2 = o[k]->prodConfObj ? o[k]->prodConfObj : pc1;
    if (pc1) vw += _snwprintf(vals + vw, RT_BATCH * 60 - vw, L"%s(%d,%ld),(%d,%ld)", vw ? L"," : L"", k, pc1, k, pc2);
  }
  if (vw) {
    _snwprintf(sql, 20000,
               L"SELECT DISTINCT TOP %d v.K, N'', N'', 0, 0 FROM (VALUES %s) AS v(K,L) "
               L"JOIN InfoObjectAttributes AS pr WITH(NOLOCK) ON pr.Link=v.L AND pr.Outdated=0 "
               L"AND ISNULL(pr.CollectionElementId,0)=0 "
               L"JOIN NameKeys AS nkp WITH(NOLOCK) ON nkp.NameKeyId=pr.NameKeyId AND nkp.Value=N'Product' "
               L"JOIN InfoObjects AS ch WITH(NOLOCK) ON ch.InfoObjectId=pr.OwnerId AND ch.Erased=0 "
               L"JOIN InfoObjects AS vv WITH(NOLOCK) ON vv.InfoObjectId=ch.ParentId AND vv.Erased=0 "
               L"WHERE EXISTS (SELECT 1 FROM InfoObjectAttributes AS t WITH(NOLOCK) "
               L"JOIN NameKeys AS nkt WITH(NOLOCK) ON nkt.NameKeyId=t.NameKeyId AND nkt.Value=N'TechComposition' "
               L"WHERE t.OwnerId=ch.InfoObjectId AND t.Outdated=0)",
               n, vals);
    int m = card_query(dbc, sql, rows, 300 * RT_BATCH, err, 280);
    if (m < 0) { /* не вышло — эти по одной */
      for (int k = 0; k < n; k++)
        if (pre[k].how == RT_PRE_LEAF) pre[k].how = RT_PRE_SINGLE;
    }
    for (int i = 0; i < m; i++)
      if (rows[i].n1 >= 0 && rows[i].n1 < n) pre[rows[i].n1].how = RT_PRE_SINGLE; /* вариант есть — по одной */
  }
  vw = 0;
  vals[0] = 0;
  for (int k = 0; k < n; k++)
    if (pre[k].how == RT_PRE_LEAF) vw += _snwprintf(vals + vw, RT_BATCH * 60 - vw, L"%s(%d,%ld)", vw ? L"," : L"", k, o[k]->id);
  if (vw) {
    _snwprintf(sql, 20000,
               L"SELECT DISTINCT TOP %d ce.CollectionElementId, N'', N'', 0, v.K FROM (VALUES %s) AS v(K,O) "
               L"JOIN InfoObjectAttributes AS it WITH(NOLOCK) ON it.OwnerId=v.O AND it.Outdated=0 "
               L"AND ISNULL(it.CollectionElementId,0)=0 "
               L"JOIN NameKeys AS nki WITH(NOLOCK) ON nki.NameKeyId=it.NameKeyId AND nki.Value=N'Items' "
               L"JOIN InfoObjectCollectionElements AS ce WITH(NOLOCK) ON ce.AttributeId IN (it.AttributeId, ISNULL(it.Link,0)) "
               L"AND ce.Outdated=0 "
               L"ORDER BY v.K, ce.CollectionElementId",
               300 * n, vals);
    int m = card_query(dbc, sql, rows, 300 * RT_BATCH, err, 280);
    for (int k = 0; k < n; k++) {
      if (pre[k].how != RT_PRE_LEAF) continue;
      if (m < 0) {
        pre[k].how = RT_PRE_SINGLE;
        continue;
      }
      rt_pre_take(pre, k, rows, m, TRUE);
      if (pre[k].how == RT_PRE_ROWS)
        _snwprintf(pre[k].note, 300, L"    техсостава нет — состав по КД (Items): строк %d\r\n", pre[k].n);
    }
  }
out:
  free(sql);
  free(rows);
}

/* ---- параллельный обход (с 2026.09.23.86) ---------------------------------
   Каждая позиция — десяток-другой запросов (изделие, ТП, состав, заготовка).
   До .86 потокам доставались ветки двух верхних уровней: у глубокой сборки
   (сборка в сборке в сборке…) почти всё лежит в одной ветке, и её шёл один
   поток — остальные простаивали. Теперь задача — каждое вхождение дерева:
   поток берёт его из общей очереди, собирает позицию и кладёт в очередь её
   состав, так что g_plmThreads потоков заняты до конца. Позиция (изделие +
   исполнение) собирается один раз на все потоки: повторы (болт в сотне узлов)
   берут готовое, а пришедшие, пока её собирает другой поток, ждут в её списке,
   не повторяя запросов. В конце дерево обходится в порядке состава — строки и
   журнал такие же, как при обходе подряд. */
#define RT_WORKERS_MAX 128
static int g_plmThreads = 24; /* потоков сбора: окно «Выгрузка из PLM», 4…128 (с .88; до .89 — до 32) */
#define RT_BLOCK 4096
#define RT_HASH (1 << 17)

typedef struct {
  long id, pc;
  int state;          /* 1 — собирается, 2 — изделие и состав готовы (ТП и заготовка — второй задачей) */
  BOOL skip;          /* документ — не позиция ведомости */
  BOOL light;         /* материал, стандартное, прочее — строка без ТП, заготовки и состава (с .95) */
  BOOL assy;          /* сборка: заготовку не берём */
  int level;          /* уровень первого вхождения — отступ в журнале */
  RtObj o;            /* изделие — для второй задачи */
  wchar_t kdNote[200];
  RtRow row;          /* поля позиции; место в дереве (уровень, кол-во, «входит в») — у вхождения */
  wchar_t own[200];   /* свои пометки: маршрут, заготовка, КД */
  RtEl *kids;         /* строки её состава */
  int nk;
  wchar_t *log;       /* что выписал её сбор — в «Подробности» у первого вхождения */
  int logLen;
  int first;          /* вхождение, которое её собирало */
  int idx;            /* номер в p->items — для задачи «ТП и заготовка» */
  int *wait, nwait, capWait; /* вхождения, пришедшие, пока она собиралась */
} RtItem;

typedef struct {
  long id;
  RtItem *item;       /* NULL — не дошли (стоп, срок) */
  int parent, level;
  double qty, tot;    /* tot — на изделие: произведение количеств по цепочке */
  BOOL qtyFound, hasSrc, deep;
  int cycles;         /* строк состава, которые вели бы в цикл, — пропущены */
  int nomat;          /* строк раздела «Документация» — не позиции, не запрашиваются (с .90; материалы с .95 — строками) */
  RtEl src;           /* строка состава, которой вошла */
  int *kids, nk;
} RtNode;

typedef struct {
  RtJob *j;
  SRWLOCK lock;
  CONDITION_VARIABLE cv;
  RtNode *blk[RT_MAX / RT_BLOCK + 1];
  int nn;
  RtItem **items;
  int ni, capItems;
  int *hash;          /* RT_HASH: номер позиции + 1 */
  int *queue, nq, capQ; /* ≥0 — вхождение: изделие и состав; <0 — позиция −(k+1): ТП и заготовка */
  int nb;               /* задач «ТП и заготовка» — для полосы хода */
  int nskip;            /* вхождений-документов (под замком): в ведомость не идут — из счёта вон */
  int busy;           /* потоков с задачей на руках */
  int nthreads;       /* сколько потоков: пачка — когда в очереди на каждого больше одного */
  volatile LONG batches, batched; /* пачек и позиций в них — в подробности */
  ULONGLONG ms[5];    /* время по видам запросов (сумма по потокам): изделие, ТП, состав, КД, заготовка */
} RtPool;

static RtJob *rt_job_new(void);
static void rt_job_free(RtJob *j);

static RtNode *rt_node(RtPool *p, int i) { return &p->blk[i / RT_BLOCK][i % RT_BLOCK]; }

/* новое вхождение (под замком); -1 — предел строк */
static int rt_node_new(RtPool *p, long id, int parent, int level, double qty, double tot, BOOL qtyFound,
                       const RtEl *src) {
  if (p->nn >= RT_MAX) return -1;
  int b = p->nn / RT_BLOCK;
  if (!p->blk[b] && !(p->blk[b] = (RtNode *)calloc(RT_BLOCK, sizeof(RtNode)))) return -1;
  int i = p->nn++;
  RtNode *nd = rt_node(p, i);
  memset(nd, 0, sizeof(*nd));
  nd->id = id;
  nd->parent = parent;
  nd->level = level;
  nd->qty = qty;
  nd->tot = tot;
  nd->qtyFound = qtyFound;
  if (src) nd->src = *src, nd->hasSrc = TRUE;
  return i;
}

static BOOL rt_grow_int(int **a, int *cap, int need) {
  if (need <= *cap) return TRUE;
  int c = *cap ? *cap * 2 : 256;
  while (c < need) c *= 2;
  int *n = (int *)realloc(*a, sizeof(int) * (size_t)c);
  if (!n) return FALSE;
  *a = n;
  *cap = c;
  return TRUE;
}

static void rt_queue_push(RtPool *p, int ni) { /* под замком */
  if (rt_grow_int(&p->queue, &p->capQ, p->nq + 1)) p->queue[p->nq++] = ni;
}

/* позиция по изделию и исполнению (под замком): найти или завести новую */
static RtItem *rt_item_get(RtPool *p, long id, long pc, BOOL *made) {
  unsigned h = ((unsigned)id * 2654435761u) ^ ((unsigned)pc * 40503u);
  *made = FALSE;
  for (unsigned k = 0; k < RT_HASH; k++) {
    int *slot = &p->hash[(h + k) & (RT_HASH - 1)];
    if (*slot) {
      RtItem *it = p->items[*slot - 1];
      if (it->id == id && it->pc == pc) return it;
      continue;
    }
    if (p->ni >= p->capItems) {
      int c = p->capItems ? p->capItems * 2 : 256;
      RtItem **n = (RtItem **)realloc(p->items, sizeof(RtItem *) * (size_t)c);
      if (!n) return NULL;
      p->items = n;
      p->capItems = c;
    }
    RtItem *it = (RtItem *)calloc(1, sizeof(RtItem));
    if (!it) return NULL;
    it->id = id;
    it->pc = pc;
    it->state = 1;
    it->first = -1;
    it->idx = p->ni;
    p->items[p->ni++] = it;
    *slot = p->ni;
    *made = TRUE;
    return it;
  }
  return NULL;
}

/* журнал задачи — к журналу позиции (часть «изделие и состав», потом «ТП, заготовка») */
static void rt_item_log_add(RtItem *it, const RtJob *w) {
  if (w->logLen <= 0) return;
  wchar_t *n = (wchar_t *)realloc(it->log, sizeof(wchar_t) * ((size_t)it->logLen + (size_t)w->logLen + 1));
  if (!n) return;
  memcpy(n + it->logLen, w->log, sizeof(wchar_t) * ((size_t)w->logLen + 1));
  it->log = n;
  it->logLen += w->logLen;
}

/* «показать один раз» — один раз на весь сбор, а не на поток: флаги главной ⇄ потока */
static void rt_flags_in(RtPool *p, RtJob *w) {
  RtJob *j = p->j;
  w->logLen = 0;
  w->log[0] = 0;
  AcquireSRWLockExclusive(&p->lock);
  w->keysShown = j->keysShown;
  w->opsShown = j->opsShown;
  w->pfShown = j->pfShown;
  w->diagShown = j->diagShown;
  w->tcDiag = j->tcDiag;
  ReleaseSRWLockExclusive(&p->lock);
}

static void rt_flags_out(RtPool *p, RtJob *w, const ULONGLONG *ms) {
  RtJob *j = p->j;
  AcquireSRWLockExclusive(&p->lock);
  if (w->keysShown) j->keysShown = TRUE;
  if (w->opsShown) j->opsShown = TRUE;
  if (w->pfShown) j->pfShown = TRUE;
  if (w->tcDiag) j->tcDiag = TRUE;
  j->diagShown |= w->diagShown;
  for (int k = 0; k < 5; k++) p->ms[k] += ms[k];
  ReleaseSRWLockExclusive(&p->lock);
}

/* часть 1: изделие прочитано — исполнение из строки состава, пропуск документов; материал,
   стандартное или прочее изделие — сразу строка (it->light), дальше не собирается */
static BOOL rt_item_a1(RtJob *w, const RtNode *nd, RtItem *it, BOOL got) {
  int level = nd->level;
  const RtEl *src = nd->hasSrc ? &nd->src : NULL;
  const wchar_t *elSection = src ? src->section : NULL;
  RtObj *o = &it->o;
  it->level = level;
  if (!got) {
    it->skip = TRUE;
    return FALSE;
  }
  if (src && src->pc && src->pc != o->prodConf) {
    rt_log(w, L"    исполнение — из строки техсостава #%ld (по изделию было #%ld)\r\n", src->pc, o->prodConf);
    o->prodConfObj = o->prodConf;
    o->prodConf = src->pc;
  }
  if (level > 0 && (rt_is_doc(o->des, o->objName) || (elSection && wcsstr(elSection, L"окумент")))) {
    rt_log(w, L"  %*s· пропуск «%s» — документ\r\n", level * 2, L"", o->objName);
    it->skip = TRUE;
    return FALSE;
  }
  if (level > 0 && rt_is_light(o, elSection)) {
    o->id = nd->id;
    rt_light_row(o, elSection, &it->row);
    it->light = TRUE;
    rt_log(w, L"%*s%s «%s» (%ld) — %s: строкой, без ТП и заготовки\r\n", level * 2, L"", it->row.f[RC_DES],
           it->row.f[RC_NAME], nd->id, it->row.f[RC_KIND]);
    return FALSE;
  }
  return TRUE;
}

/* часть 2: строка позиции, состав (pre — уже прочитанный пачкой), изменения КД, вид */
static void rt_item_a2(SQLHDBC dbc, RtJob *w, CardRow *rows, const RtNode *nd, RtItem *it, RtPre *pre,
                       ULONGLONG *ms) {
  ULONGLONG t;
  int level = nd->level;
  const RtEl *src = nd->hasSrc ? &nd->src : NULL;
  const wchar_t *elSection = src ? src->section : NULL;
  RtObj *o = &it->o;
  RtRow *r = &it->row;
  memset(r, 0, sizeof(*r));
  r->id = nd->id;
  lstrcpynW(r->f[RC_DES], o->des, 200);
  lstrcpynW(r->f[RC_NAME], o->name, 200);
  double v;
  if (o->mass[0] && rt_num(o->mass, &v)) rt_fmt(v, 3, r->f[RC_MASS], 200);
  wchar_t q[40];
  rt_fmt(nd->qty, 3, q, 40);
  rt_log(w, L"%*s%s %s (%ld), кол-во %s%s\r\n", level * 2, L"", o->des, o->name, nd->id, q,
         level && !nd->qtyFound ? L" (поле количества не найдено — 1)" : L"");
  rt_log(w, L"    карточки: ТП %ld, заготовок %ld, техсостава %ld, конфигурация %ld\r\n", o->tpCard, o->pfCard,
         o->tcCard, o->prodConf);
  r->par = o->par;
  r->prodConf = o->prodConf;
  RtEl *el = (RtEl *)malloc(sizeof(RtEl) * RT_KIDS);
  long variant = 0;
  int kdBefore = w->kdUsed;
  int ne = 0;
  t = GetTickCount64();
  if (el && pre && pre->how == RT_PRE_ROWS) { /* строки уже есть (пачкой) — только их поля */
    if (pre->note[0]) rt_log(w, L"%s", pre->note);
    if (pre->kd) w->kdUsed++;
    int n = pre->n > 300 ? 300 : pre->n;
    memcpy(rows, pre->rows, sizeof(CardRow) * (size_t)n);
    ne = rt_children_fill(dbc, rows, n, pre->kd, w, el, RT_KIDS, &variant);
  } else if (el && !(pre && pre->how == RT_PRE_LEAF)) {
    ne = rt_children(dbc, o, rows, w, el, RT_KIDS, &variant);
  }
  ms[2] += GetTickCount64() - t;
  r->tcVariant = variant;
  r->tcCard = o->tcCard;
  r->pfCard = o->pfCard;
  /* изменения КД — здесь: они пишут пометки в строки состава, а те сейчас уйдут в очередь */
  t = GetTickCount64();
  if (ne > 0 && !rt_late(w)) rt_kd_changes(dbc, variant, rows, w, el, ne, it->kdNote, 200);
  ms[3] += GetTickCount64() - t;
  if (w->kdUsed > kdBefore && !it->kdNote[0]) lstrcpynW(it->kdNote, L"состав по КД — техсостава нет", 200);
  const wchar_t *kind = o->section[0] ? o->section : (elSection && elSection[0] ? elSection : NULL);
  lstrcpynW(r->f[RC_KIND], kind ? kind : (ne > 0 ? L"Сборочные единицы" : L"Детали"), 200);
  it->assy = ne > 0 || wcsstr(r->f[RC_KIND], L"борочн") || wcsstr(r->f[RC_KIND], L"омплекс");
  if (ne > 0 && el) {
    RtEl *k = (RtEl *)realloc(el, sizeof(RtEl) * (size_t)ne);
    it->kids = k ? k : el;
    it->nk = ne;
  } else {
    free(el);
  }
}

/* Позиция собирается в две задачи (с 2026.09.23.88). Первая — изделие и его
   состав: сразу после неё строки состава уходят в очередь, и дерево
   раскрывается вглубь, не дожидаясь ТП и заготовки (прежде каждый уровень
   ждал, пока у позиции над ним соберутся ещё и маршрут, и заготовка). Вторая —
   ТП (маршрут) и заготовка — отдельной задачей, её берёт любой свободный поток. */
static BOOL rt_item_eval_a(RtPool *p, SQLHDBC dbc, RtJob *w, CardRow *rows, const RtNode *nd, RtItem *it) {
  ULONGLONG ms[5] = {0, 0, 0, 0, 0}, t;
  rt_flags_in(p, w);
  t = GetTickCount64();
  BOOL got = rt_obj(dbc, nd->id, &it->o, rows, w);
  ms[0] += GetTickCount64() - t;
  if (rt_item_a1(w, nd, it, got)) rt_item_a2(dbc, w, rows, nd, it, NULL, ms);
  rt_item_log_add(it, w);
  rt_flags_out(p, w, ms);
  return !it->skip && !it->light;
}

/* пачка новых позиций (с .90): свойства и карточки, потом состав — общими запросами;
   more[k] — у позиции будет задача «ТП и заготовка» */
static void rt_items_eval_a_batch(RtPool *p, SQLHDBC dbc, RtJob *w, CardRow *rows, RtNode **nds, RtItem **its, int m,
                                  BOOL *more) {
  if (m == 1) {
    more[0] = rt_item_eval_a(p, dbc, w, rows, nds[0], its[0]);
    return;
  }
  ULONGLONG ms[5] = {0, 0, 0, 0, 0}, t = GetTickCount64();
  long ids[RT_BATCH];
  RtObj ob[RT_BATCH];
  wchar_t note[RT_BATCH][300];
  for (int k = 0; k < m; k++) ids[k] = nds[k]->id;
  if (!rt_obj_batch(dbc, ids, m, ob, note)) { /* не прошло — по одной */
    for (int k = 0; k < m; k++) more[k] = rt_item_eval_a(p, dbc, w, rows, nds[k], its[k]);
    return;
  }
  ms[0] += GetTickCount64() - t;
  InterlockedIncrement(&p->batches);
  InterlockedExchangeAdd(&p->batched, m);
  BOOL use[RT_BATCH];
  RtObj *po[RT_BATCH];
  for (int k = 0; k < m; k++) {
    ULONGLONG z[5] = {0, 0, 0, 0, 0};
    its[k]->o = ob[k];
    po[k] = &its[k]->o;
    rt_flags_in(p, w);
    if (note[k][0]) rt_log(w, L"%s", note[k]);
    use[k] = rt_item_a1(w, nds[k], its[k], TRUE);
    rt_item_log_add(its[k], w);
    rt_flags_out(p, w, z);
  }
  t = GetTickCount64();
  RtPre pre[RT_BATCH];
  AcquireSRWLockExclusive(&p->lock);
  BOOL tcDiag = p->j->tcDiag;
  ReleaseSRWLockExclusive(&p->lock);
  rt_children_batch(dbc, po, use, m, pre, tcDiag);
  ms[2] += GetTickCount64() - t;
  rt_flags_out(p, w, ms); /* время общих запросов пачки */
  for (int k = 0; k < m; k++) {
    ULONGLONG z[5] = {0, 0, 0, 0, 0};
    if (use[k]) {
      rt_flags_in(p, w);
      rt_item_a2(dbc, w, rows, nds[k], its[k], &pre[k], z);
      rt_item_log_add(its[k], w);
      rt_flags_out(p, w, z);
    }
    free(pre[k].rows);
    more[k] = !its[k]->skip && !its[k]->light;
  }
}

static void rt_item_eval_b(RtPool *p, SQLHDBC dbc, RtJob *w, CardRow *rows, RtItem *it) {
  ULONGLONG ms[5] = {0, 0, 0, 0, 0}, t;
  rt_flags_in(p, w);
  RtObj *o = &it->o;
  RtRow *r = &it->row;
  rt_log(w, L"%*s  · %s: ТП и заготовка\r\n", it->level * 2, L"", o->des);
  if (!o->tpCard && !o->pfCard && !(w->diagShown & (it->level ? 2 : 1))) {
    w->diagShown |= it->level ? 2 : 1;
    rt_diag(dbc, o, rows, w);
  }
  wchar_t notes[2][200] = {L"", L""};
  t = GetTickCount64();
  rt_route(dbc, o, rows, w, r->f[RC_ROUTE], 200, notes[0], 60);
  ms[1] += GetTickCount64() - t;
  r->tp = w->lastTp;
  r->tpVer = w->lastTpVer;
  r->tpVar = w->lastTpVar;
  lstrcpynW(r->tpName, w->lastTpName, 200);
  if (it->assy) {
    if (o->pfCard) rt_log(w, L"    сборка — заготовку (карточка %ld) не берём\r\n", o->pfCard);
  } else {
    t = GetTickCount64();
    rt_preform(dbc, o, rows, w, r, notes[1], 60);
    ms[4] += GetTickCount64() - t;
    if (!r->f[RC_MAT][0] && o->mat[0]) lstrcpynW(r->f[RC_MAT], o->mat, 200);
  }
  rt_note_add(it->own, notes[0]);
  rt_note_add(it->own, notes[1]);
  rt_note_add(it->own, it->kdNote);
  rt_item_log_add(it, w);
  rt_flags_out(p, w, ms);
}

/* позиция вхождения готова — его состав в очередь */
static void rt_node_expand(RtPool *p, int ni) {
  RtJob *j = p->j;
  RtNode *nd = rt_node(p, ni);
  RtItem *it = nd->item;
  if (it->skip) { /* документ: не позиция — из «найденных» вон */
    AcquireSRWLockExclusive(&p->lock);
    p->nskip++;
    InterlockedExchange(&j->progTotal, p->nn - p->nskip);
    ReleaseSRWLockExclusive(&p->lock);
    return;
  }
  LONG done = InterlockedIncrement(&j->progDone);
  if (j->notify) PostMessageW(j->notify, WM_RT_PROGRESS, (WPARAM)done, 0);
  if (!it->nk) return;
  if (nd->level >= RT_DEPTH) {
    nd->deep = TRUE;
    return;
  }
  int *kids = (int *)malloc(sizeof(int) * (size_t)it->nk);
  if (!kids) return;
  AcquireSRWLockExclusive(&p->lock);
  int nk = 0;
  for (int i = 0; i < it->nk; i++) {
    const RtEl *e = &it->kids[i];
    /* цикл: входящее уже есть в цепочке над этим вхождением — дальше не идём */
    BOOL loop = FALSE;
    for (int a = ni; a >= 0 && !loop; a = rt_node(p, a)->parent) loop = rt_node(p, a)->id == e->child;
    if (loop) {
      nd->cycles++;
      continue;
    }
    /* документ — видно по разделу строки: в ведомость не идёт, и читать его,
       чтобы это узнать, незачем (прежде — запросы на каждый). Материалы с .95
       читаются: они идут строкой, нужно наименование */
    if (e->section[0] && wcsstr(e->section, L"окумент")) {
      nd->nomat++;
      continue;
    }
    int c = rt_node_new(p, e->child, ni, nd->level + 1, e->qty, e->qty * nd->tot, e->qtyFound, e);
    if (c < 0) {
      rt_full(j);
      break;
    }
    kids[nk++] = c;
    rt_queue_push(p, c);
  }
  nd->kids = kids;
  nd->nk = nk;
  InterlockedExchange(&j->progTotal, p->nn - p->nskip);
  WakeAllConditionVariable(&p->cv);
  ReleaseSRWLockExclusive(&p->lock);
}

/* вхождения (одно или пачка): позиция готова — сразу состав; собирает другой
   поток — ждать в её списке; новые — собрать (пачкой, если их несколько), потом
   раскрыть себя и всех ждавших */
static void rt_pool_nodes(RtPool *p, SQLHDBC dbc, RtJob *w, CardRow *rows, const int *nis, int cnt) {
  RtNode *mnd[RT_BATCH];
  RtItem *mit[RT_BATCH];
  int mni[RT_BATCH], ready[RT_BATCH], nm = 0, nr = 0;
  AcquireSRWLockExclusive(&p->lock);
  for (int c = 0; c < cnt; c++) {
    int ni = nis[c];
    RtNode *nd = rt_node(p, ni);
    long pc = nd->hasSrc ? nd->src.pc : 0;
    BOOL made;
    RtItem *it = rt_item_get(p, nd->id, pc, &made);
    if (!it) continue;
    nd->item = it;
    if (made) {
      it->first = ni;
      mnd[nm] = nd, mit[nm] = it, mni[nm] = ni, nm++;
    } else if (it->state == 1) { /* собирает другой поток (или эта же пачка) — ждать */
      if (rt_grow_int(&it->wait, &it->capWait, it->nwait + 1)) it->wait[it->nwait++] = ni;
    } else {
      ready[nr++] = ni;
    }
  }
  ReleaseSRWLockExclusive(&p->lock);
  if (nm) {
    BOOL more[RT_BATCH];
    rt_items_eval_a_batch(p, dbc, w, rows, mnd, mit, nm, more);
    for (int k = 0; k < nm; k++) {
      RtItem *it = mit[k];
      AcquireSRWLockExclusive(&p->lock);
      it->state = 2;
      if (more[k]) { /* ТП и заготовка — отдельной задачей, раньше состава: состав (он позже) возьмут первым */
        rt_queue_push(p, -(it->idx + 1));
        p->nb++;
        InterlockedExchange(&p->j->prog2Total, p->nb);
        WakeAllConditionVariable(&p->cv);
      }
      int nw = it->nwait, *wl = it->wait;
      it->wait = NULL;
      it->nwait = it->capWait = 0;
      ReleaseSRWLockExclusive(&p->lock);
      rt_node_expand(p, mni[k]);
      for (int q = 0; q < nw; q++) rt_node_expand(p, wl[q]);
      free(wl);
    }
  }
  for (int k = 0; k < nr; k++) rt_node_expand(p, ready[k]);
}

/* поток: брать вхождения из очереди, пока она не опустела и все не освободились */
static void rt_pool_run(RtPool *p, SQLHDBC dbc, RtJob *w, CardRow *rows) {
  AcquireSRWLockExclusive(&p->lock);
  for (;;) {
    while (p->nq == 0 && p->busy > 0 && !rt_late(p->j)) SleepConditionVariableSRW(&p->cv, &p->lock, 500, 0);
    if (p->nq == 0 || rt_late(p->j)) break;
    int ni = p->queue[--p->nq]; /* с конца: в глубину — повторы чаще находят готовое */
    p->busy++;
    RtItem *it = ni < 0 ? p->items[-ni - 1] : NULL;
    /* очередь длинная — взять пачку вхождений: на поток приходится больше одного */
    int batch[RT_BATCH], nb = 0;
    if (!it) {
      batch[nb++] = ni;
      int extra = p->nq / (p->nthreads > 0 ? p->nthreads : 1);
      if (extra > RT_BATCH - 1) extra = RT_BATCH - 1;
      while (extra-- > 0 && p->nq > 0 && p->queue[p->nq - 1] >= 0) batch[nb++] = p->queue[--p->nq];
    }
    ReleaseSRWLockExclusive(&p->lock);
    if (it) {
      rt_item_eval_b(p, dbc, w, rows, it);
      InterlockedIncrement(&p->j->prog2Done);
    } else {
      rt_pool_nodes(p, dbc, w, rows, batch, nb);
    }
    AcquireSRWLockExclusive(&p->lock);
    p->busy--;
  }
  WakeAllConditionVariable(&p->cv);
  ReleaseSRWLockExclusive(&p->lock);
}

typedef struct {
  RtPool *p;
  RtJob *w;
} RtPoolWork;

static DWORD WINAPI rt_pool_worker(LPVOID param) {
  RtPoolWork *k = (RtPoolWork *)param;
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[280];
  if (!plm_connect(&env, &dbc, err, 280)) return 0; /* вхождения возьмут другие потоки */
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * 300);
  g_qTimeout = 30;
  g_qCancel = k->p->j->cancel;
  g_qSerial = TRUE; /* мелкие запросы при десятках потоков — на одном ядре сервера (card_query) */
  if (rows) rt_pool_run(k->p, dbc, k->w, rows);
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

/* дерево — в строки ведомости по порядку состава; журнал — так же */
static void rt_pool_out(RtPool *p, RtJob *j, int ni, const wchar_t *parentDes) {
  RtNode *nd = rt_node(p, ni);
  RtItem *it = nd->item;
  if (!it || it->state != 2) return; /* не дошли: стоп или срок */
  if (it->first == ni) rt_log_raw(j, it->log, it->logLen);
  if (it->skip) return;
  RtRow *r = rt_row_add(j);
  if (!r) {
    rt_full(j);
    return;
  }
  *r = it->row;
  r->level = nd->level;
  r->qty = nd->qty;
  r->qtyTot = nd->tot;
  lstrcpynW(r->f[RC_PARENT], parentDes, 200);
  rt_fmt(r->qty, 3, r->f[RC_QTY], 200);
  rt_fmt(r->qtyTot, 3, r->f[RC_QTYTOT], 200);
  if (r->hasNorm) rt_fmt(floor(r->norm1 * 1000 + 0.5) / 1000 * r->qtyTot, 3, r->f[RC_NORMTOT], 200);
  lstrcpynW(r->f[RC_NOTE], it->own, 200);
  rt_src_notes(nd->level, nd->qtyFound, nd->hasSrc ? &nd->src : NULL, r->f[RC_NOTE]);
  if (it->first != ni)
    rt_log(j, L"%*s%s %s — повтор: из уже собранного, кол-во %s\r\n", nd->level * 2, L"", r->f[RC_DES],
           r->f[RC_NAME], r->f[RC_QTY]);
  if (nd->nomat && it->first == ni)
    rt_log(j, L"%*s    документов в составе: %d — в ведомость не идут\r\n", nd->level * 2, L"", nd->nomat);
  if (nd->cycles)
    rt_log(j, L"%*s    !!! в составе «%s» %d строк ведут в цикл (входящее уже есть выше по цепочке) — пропущены\r\n",
           nd->level * 2, L"", r->f[RC_DES], nd->cycles);
  if (nd->deep)
    rt_log(j, L"%*s    !!! глубже %d уровней не спускаюсь — состав «%s» не собран\r\n", nd->level * 2, L"", RT_DEPTH,
           r->f[RC_DES]);
  wchar_t des[200];
  lstrcpynW(des, r->f[RC_DES], 200); /* r дальше может переехать (строки растут) */
  for (int k = 0; k < nd->nk; k++) rt_pool_out(p, j, nd->kids[k], des);
}

static void rt_walk_pool(SQLHDBC dbc, RtJob *j, CardRow *rows, long root) {
  RtPool *p = (RtPool *)calloc(1, sizeof(RtPool));
  if (!p || !(p->hash = (int *)calloc(RT_HASH, sizeof(int)))) {
    free(p);
    rt_walk(dbc, root, 0, j->order, 1, TRUE, 1, NULL, NULL, rows, j); /* нет памяти — подряд */
    return;
  }
  ULONGLONG t0 = GetTickCount64();
  p->j = j;
  InitializeSRWLock(&p->lock);
  InitializeConditionVariable(&p->cv);
  InterlockedExchange(&j->progDone, 0);
  InterlockedExchange(&j->progTotal, 1);
  InterlockedExchange(&j->prog2Done, 0);
  InterlockedExchange(&j->prog2Total, 0);
  int r0 = rt_node_new(p, root, -1, 0, 1, 1, TRUE, NULL);
  rt_queue_push(p, r0);
  RtPoolWork work[RT_WORKERS_MAX];
  HANDLE th[RT_WORKERS_MAX];
  int started = 0;
  int want = g_plmThreads < 1 ? 1 : g_plmThreads > RT_WORKERS_MAX ? RT_WORKERS_MAX : g_plmThreads;
  p->nthreads = want;
  for (int k = 0; k < want - 1; k++) { /* и этот поток — тоже рабочий */
    work[started].p = p;
    work[started].w = rt_job_new();
    if (!work[started].w) break;
    work[started].w->deadline = j->deadline;
    work[started].w->cancel = j->cancel;
    th[started] = CreateThread(NULL, 0, rt_pool_worker, &work[started], 0, NULL);
    if (!th[started]) {
      rt_job_free(work[started].w);
      break;
    }
    started++;
  }
  RtJob *me = rt_job_new();
  if (me) {
    me->deadline = j->deadline;
    me->cancel = j->cancel;
    g_qSerial = TRUE;
    rt_pool_run(p, dbc, me, rows);
    g_qSerial = FALSE;
  }
  if (started) WaitForMultipleObjects((DWORD)started, th, TRUE, INFINITE);
  for (int k = 0; k < started; k++) {
    CloseHandle(th[k]);
    j->kdUsed += work[k].w->kdUsed;
    rt_job_free(work[k].w);
  }
  if (me) j->kdUsed += me->kdUsed;
  rt_job_free(me);
  double walk = (double)(GetTickCount64() - t0) / 1000.0;
  if (r0 >= 0) rt_pool_out(p, j, r0, j->order);
  rt_log(j, L"\r\nОбход: вхождений %d, разных позиций %d, потоков %d — %.0f с; пачками прочитано позиций %ld (пачек %ld)\r\n",
         p->nn, p->ni, started + 1, walk, p->batched, p->batches);
  rt_log(j, L"  время запросов (сумма по потокам): изделие %.0f с, ТП %.0f с, состав %.0f с, изменения КД %.0f с, "
            L"заготовка %.0f с\r\n",
         p->ms[0] / 1000.0, p->ms[1] / 1000.0, p->ms[2] / 1000.0, p->ms[3] / 1000.0, p->ms[4] / 1000.0);
  for (int i = 0; i < p->ni; i++) {
    free(p->items[i]->kids);
    free(p->items[i]->log);
    free(p->items[i]->wait);
    free(p->items[i]);
  }
  for (int i = 0; i < p->nn; i++) free(rt_node(p, i)->kids);
  for (int b = 0; b <= RT_MAX / RT_BLOCK; b++) free(p->blk[b]);
  free(p->items);
  free(p->hash);
  free(p->queue);
  free(p);
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
    g_qCancel = j->cancel;
    long root = j->rootId;
    if (root) rt_log(j, L"Изделие — строка из поиска PLM: %ld\r\n\r\n", root);
    else root = rt_find_root(dbc, j->des, rows, j);
    volatile LONG prog = 0;
    j->shared = &prog;
    /* проверочная выгрузка — подряд (её журнал читают по порядку); обычная — потоками */
    if (root && j->dump) rt_walk(dbc, root, 0, j->order, 1, TRUE, 1, NULL, NULL, rows, j);
    else if (root) rt_walk_pool(dbc, j, rows, root);
    j->shared = NULL;
    g_qTimeout = 0;
    g_qCancel = NULL;
    if (rt_late(j) && !(j->cancel && *j->cancel))
      rt_log(j, L"\r\nВремя вышло — собрано не всё (%d позиций).\r\n", j->n);
    else if (j->cancel && *j->cancel)
      rt_log(j, L"\r\nОстановлено — собрано %d позиций.\r\n", j->n);
    free(rows);
  }
  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);
}

static RtJob *rt_job_new(void) {
  RtJob *j = (RtJob *)calloc(1, sizeof(RtJob));
  if (!j) return NULL;
  j->cap = 256; /* дальше — по мере надобности (rt_rows_reserve) */
  j->rows = (RtRow *)calloc((size_t)j->cap, sizeof(RtRow));
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
  free(j->rows);
  free(j->log);
  free(j);
}

