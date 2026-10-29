/* ---- «В Союз»: маршрут по чертежу → операции ТП обычным клиентом (с 2026.10.24) ----

   В чате ИИ пишут «в союз» — последний маршрут беседы (tn_text, можно и
   поправленный) идёт в очередь, а рядом с клиентом «Союза» встаёт окно-подсказка:
   какая операция сейчас, её код, цех, оборудование, Тпз и Тшт — у каждого поля
   «Копировать». Разбор маршрута и сверка — soyuz_ops.c.

   В буфер ложится:
     • GUID операции-аналога с тем же кодом справочника — типовой или из готового
       ТП завода. В дереве ТП правая кнопка на версии → «Вставить операцию»:
       «Союз» сам делает копию со следующим номером и сохраняет;
     • текст «4110 Токарная», как для 1С, — если аналога нет или выбран «Текст».
       После Ctrl+V в «Союзе» следом ложится следующая строка.
   Встала ли операция, CursorPad видит в базе (только чтение): раз в 1,5 с — новые
   TSOperation с той же справочной операцией. Первая вставка запоминает версию
   ТП, дальше считаются только её операции. Нет связи с базой — переносится по
   Ctrl+V и «Дальше», без отметок.

   В «Союзе» ничего не меняется и не сохраняется помимо того, что делает сам
   человек его командами. Аналог переносит и свои атрибуты (оборудование, нормы,
   содержание) — окно напоминает, что поправить. */

#define WM_SZ_READY (WM_APP + 91) /* lParam — SzLoad*, освобождает окно */
#define WM_SZ_NEWOP (WM_APP + 92) /* lParam — SzNew*, освобождает окно */
#define WM_SZ_DB (WM_APP + 93)    /* wParam: 1 — слежу за базой, 0 — нет связи; lParam — wchar_t* ошибки или NULL */
#define ID_SZ_PREV 4840
#define ID_SZ_SKIP 4841
#define ID_SZ_NEXT 4842 /* главная кнопка подвала — синяя (cursorpad.c, draw_pad_button) */
#define ID_SZ_MODE 4843
#define ID_SZ_STOP 4844
#define ID_SZ_LIST 4845
#define ID_SZ_PUT 4846  /* «В буфер» — вернуть операцию в буфер */
#define ID_SZ_AUTO 4847 /* «Вставить сам» — автоввод (soyuz_auto.c) */
#define ID_SZ_MAKE 4848 /* «Создать ТП» — функцией пакета «CursorPad — Союз» (soyuz_make.c) */
#define ID_SZ_COPY 4850 /* + поле SZF_… */
#define TIMER_SZ_PASTE 4860
#define TIMER_SZ_CLIP 4861
#define TIMER_SZ_WAIT 4862       /* после автоввода — сколько ждать операцию в базе */
#define WM_SZ_AUTO (WM_APP + 94) /* wParam: 1 — «Вставить операцию» нажата, 2 — открыто «Создать операцию», 0 — нет;
                                   lParam — wchar_t* что вышло, освобождает окно */

enum { SZF_CODE, SZF_NAME, SZF_CEH, SZF_EQUIP, SZF_TPZ, SZF_TSHT, SZF_N };
static const wchar_t *const kSzField[SZF_N] = {L"Код", L"Операция", L"Цех, участок", L"Оборудование", L"Тпз",
                                               L"Тшт"};

static SoRoute g_szR;
static HWND g_szWnd, g_szList;
static BOOL g_szOn;        /* очередь идёт */
static BOOL g_szTextOnly;  /* текст «4110 Токарная» для «Создать операцию» — по умолчанию: в сквозной ТП копию
                              не вставить (FixThroughTechProcess); FALSE — GUID аналога для «Вставить операцию» */
static int g_szClip;       /* что в буфере: 0 — строка очереди, 1 — поле (кнопка «Копировать»), 2 — чужое */
static int g_szDb;         /* база: 0 — не спрашивали, 1 — ищу аналоги, 2 — слежу, 3 — нет связи */
static BOOL g_szLoaded;    /* аналоги из базы пришли */
static wchar_t g_szNote[300]; /* строка состояния окна */
static HANDLE g_szThread;
static volatile LONG g_szGen; /* номер очереди: сменился — поток базы выходит, его ответы отбрасываются */
static HHOOK g_szHook;
static BOOL g_szVDown, g_szInsDown, g_szPend;
static int g_szRetry;
static float g_szS = 1.0f;
static volatile LONG g_szAutoBusy, g_szAutoAbort; /* автоввод идёт; Esc — прервать */
static BOOL g_szAutoWait;                         /* пункт нажат — ждём операцию в базе */
static void sz_auto_start(void);                  /* soyuz_auto.c */
static void szm_open(void);                       /* soyuz_make.c */

static int SZS(int v) { return (int)(v * g_szS + 0.5f); }

/* проверочный стенд (soyuz_win_test.c) подменяет связь с базой и буфер: буфер у
   стенда общий с рабочим столом человека, а входа SQL у него нет */
static BOOL (*g_szConnect)(SQLHENV *, SQLHDBC *, wchar_t *, int) = plm_connect;
static BOOL (*g_szClipSet)(const wchar_t *) = clipboard_set;
static BOOL g_szNoHook;

/* ---- поток базы ------------------------------------------------------------------- */

typedef struct {
  LONG gen;
  int n;
  wchar_t code[SO_MAX][8], alt[SO_MAX][64];
} SzJob;

typedef struct {
  long dir;
  wchar_t code[8], name[120];
  int uses;
  wchar_t guid[40], from[200], ceh[120], equip[120];
  double tpz, tsht;
} SzGot;

typedef struct {
  LONG gen;
  int n;
  SzGot it[SO_MAX];
  wchar_t err[300];
} SzLoad;

typedef struct {
  LONG gen;
  long dir, parent;
  wchar_t num[16];
} SzNew;

/* «0110» и «110» — один код: справочник в разных базах пишет его по-разному */
static long sz_code_num(const wchar_t *s) {
  long v = 0;
  int d = 0;
  for (; *s == L' '; s++) {}
  for (; *s >= L'0' && *s <= L'9'; s++, d++) v = v * 10 + (*s - L'0');
  return d ? v : -1;
}

static void sz_post_db(int ok, const wchar_t *err) {
  wchar_t *e = NULL;
  if (err && err[0]) {
    e = (wchar_t *)malloc(300 * sizeof(wchar_t));
    if (e) lstrcpynW(e, err, 300);
  }
  if (!g_szWnd || !PostMessageW(g_szWnd, WM_SZ_DB, (WPARAM)ok, (LPARAM)e)) free(e);
}

/* через запятую — во фразу IN (…) */
static void sz_idlist(const long *ids, int n, wchar_t *out, int cap) {
  int q = 0;
  out[0] = 0;
  for (int i = 0; i < n && q < cap - 14; i++) q += _snwprintf(out + q, 13, q ? L",%ld" : L"%ld", ids[i]);
  out[q] = 0;
}

/* справочник, частоты, аналоги и их цех, оборудование, нормы */
static void sz_load(SQLHDBC dbc, const SzJob *j, SzLoad *ld, CardRow *rows) {
  wchar_t err[300];
  /* 1. все коды очереди и их запасные — в справочнике операций */
  wchar_t codes[SO_MAX * 3][8];
  int nc = 0;
  for (int i = 0; i < j->n; i++) {
    const wchar_t *src[2] = {j->code[i], j->alt[i]};
    for (int s = 0; s < 2; s++)
      for (const wchar_t *p = src[s]; p && *p;) {
        while (*p == L' ' || *p == L',') p++;
        int k = 0;
        wchar_t c[8];
        while (p[k] >= L'0' && p[k] <= L'9' && k < 7) c[k] = p[k], k++;
        c[k] = 0;
        p += k;
        while (*p && *p != L',') p++;
        if (!k) continue;
        int dup = 0;
        for (int q = 0; q < nc && !dup; q++) dup = sz_code_num(codes[q]) == sz_code_num(c);
        if (!dup && nc < SO_MAX * 3) lstrcpynW(codes[nc++], c, 8);
      }
  }
  if (!nc) return;
  wchar_t in[SO_MAX * 3 * 26];
  int q = 0;
  for (int i = 0; i < nc && q < SO_MAX * 3 * 26 - 30; i++) { /* «0110» и «110» */
    long v = sz_code_num(codes[i]);
    int k = _snwprintf(in + q, 30, L"%sN'%ld',N'%04ld'", q ? L"," : L"", v, v);
    if (k > 0) q += k;
  }
  in[q] = 0;
  wchar_t *sql = (wchar_t *)malloc(9000 * sizeof(wchar_t));
  if (!sql) return;
  _snwprintf(sql, 9000,
             L"SELECT TOP 300 o.InfoObjectId, cd.V, CAST(o.Name AS NVARCHAR(250)), 0, 0 "
             L"FROM InfoObjects AS o WITH(NOLOCK) "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId AND t.NameKey=N'Operation' "
             L"CROSS APPLY (SELECT TOP 1 COALESCE(NULLIF(LTRIM(RTRIM(CAST(c.ShortText AS NVARCHAR(40)))),N''), "
             L"CONVERT(NVARCHAR(40), c.IntegerNumber), CONVERT(NVARCHAR(40), c.LongNumber)) AS V "
             L"FROM InfoObjectAttributes AS c WITH(NOLOCK) "
             L"JOIN NameKeys AS nkc WITH(NOLOCK) ON nkc.NameKeyId=c.NameKeyId AND nkc.Value=N'Code' "
             L"WHERE c.OwnerId=o.InfoObjectId AND c.Outdated=0 AND ISNULL(c.CollectionElementId,0)=0) AS cd "
             L"WHERE o.Erased=0 AND cd.V IN (%s)",
             in);
  sql[8999] = 0;
  int nd = card_query(dbc, sql, rows, CARD_ROWS, err, 300);
  if (nd < 0) {
    lstrcpynW(ld->err, err, 300);
    free(sql);
    return;
  }
  typedef struct {
    long id, code;
    int uses;
    wchar_t name[120];
  } SzDir;
  SzDir *dirs = (SzDir *)calloc((size_t)(nd > 0 ? nd : 1), sizeof(SzDir));
  if (!dirs) {
    free(sql);
    return;
  }
  long ids[CARD_ROWS];
  for (int i = 0; i < nd; i++) {
    dirs[i].id = rows[i].n1;
    dirs[i].code = sz_code_num(rows[i].s1);
    /* имя объекта — «4110 Токарная», с номером: номер — отдельно, в коде */
    const wchar_t *nm = rows[i].s2;
    if (sz_code_num(nm) == dirs[i].code) nm += op_num_prefix(nm);
    lstrcpynW(dirs[i].name, nm, 120);
    ids[i] = rows[i].n1;
  }
  /* 2. сколько раз каждая встречается в ТП завода: из двух «Контроль» берём ходовой */
  wchar_t *list = (wchar_t *)malloc((size_t)(nd * 13 + 2) * sizeof(wchar_t));
  if (list && nd) {
    sz_idlist(ids, nd, list, nd * 13 + 2);
    _snwprintf(sql, 9000,
               L"SELECT l.Link, N'', N'', COUNT(*), 0 FROM InfoObjectAttributes AS l WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=l.NameKeyId AND nk.Value=N'Operation' "
               L"JOIN InfoObjects AS o WITH(NOLOCK) ON o.InfoObjectId=l.OwnerId AND o.Erased=0 "
               L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId "
               L"AND t.NameKey IN (N'TSOperation',N'TypesOperation') "
               L"WHERE l.Link IN (%s) AND l.Outdated=0 GROUP BY l.Link",
               list);
    sql[8999] = 0;
    int nu = card_query(dbc, sql, rows, CARD_ROWS, err, 300);
    for (int i = 0; i < nu; i++)
      for (int k = 0; k < nd; k++)
        if (dirs[k].id == rows[i].n1) dirs[k].uses = rows[i].n2;
  }
  free(list);
  /* 3. у каждой строки — справочная с наибольшим числом ТП среди своего кода и запасных */
  for (int i = 0; i < j->n; i++) {
    SzGot *g = &ld->it[i];
    g->tpz = g->tsht = -1;
    long want[8];
    int nw = 0;
    want[nw++] = sz_code_num(j->code[i]);
    for (const wchar_t *p = j->alt[i]; *p && nw < 8;) {
      while (*p == L' ' || *p == L',') p++;
      long v = sz_code_num(p);
      if (v >= 0) want[nw++] = v;
      while (*p && *p != L',') p++;
    }
    int best = -1;
    for (int w = 0; w < nw; w++)
      for (int k = 0; k < nd; k++)
        if (dirs[k].code == want[w] && want[w] >= 0 && (best < 0 || dirs[k].uses > dirs[best].uses)) best = k;
    if (best < 0) continue;
    g->dir = dirs[best].id;
    g->uses = dirs[best].uses;
    lstrcpynW(g->name, dirs[best].name, 120);
    _snwprintf(g->code, 8, L"%04ld", dirs[best].code);
    g->code[7] = 0;
  }
  free(dirs);
  /* 4. аналог: типовая операция, иначе свежая операция из ТП с актуальным вариантом
     (без ActualOperation «Вставить операцию» отказывает) */
  long anId[SO_MAX], anVar[SO_MAX], anOwn[SO_MAX * 2];
  int na = 0;
  for (int i = 0; i < j->n; i++) {
    SzGot *g = &ld->it[i];
    anId[i] = anVar[i] = 0;
    if (!g->dir || !g->uses) continue;
    int same = -1;
    for (int k = 0; k < i && same < 0; k++)
      if (ld->it[k].dir == g->dir && anId[k]) same = k;
    if (same >= 0) { /* тот же код — тот же аналог */
      anId[i] = anId[same], anVar[i] = anVar[same];
      lstrcpynW(g->guid, ld->it[same].guid, 40);
      lstrcpynW(g->from, ld->it[same].from, 200);
      continue;
    }
    _snwprintf(sql, 9000,
               L"SELECT TOP 1 o.InfoObjectId, CONVERT(NVARCHAR(40), o.GlobalIdentifier), "
               L"CASE WHEN t.NameKey=N'TypesOperation' THEN N'типовая · ' + CAST(o.Name AS NVARCHAR(200)) "
               L"ELSE ISNULL(CAST(tp.Name AS NVARCHAR(200)) + N' · ', N'') + CAST(o.Name AS NVARCHAR(200)) END, "
               L"0, ISNULL(ao.L,0) "
               L"FROM InfoObjectAttributes AS l WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=l.NameKeyId AND nk.Value=N'Operation' "
               L"JOIN InfoObjects AS o WITH(NOLOCK) ON o.InfoObjectId=l.OwnerId AND o.Erased=0 "
               L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId "
               L"AND t.NameKey IN (N'TSOperation',N'TypesOperation') "
               L"LEFT JOIN InfoObjects AS v WITH(NOLOCK) ON v.InfoObjectId=o.ParentId "
               L"LEFT JOIN InfoObjects AS tp WITH(NOLOCK) ON tp.InfoObjectId=v.ParentId "
               L"OUTER APPLY (SELECT TOP 1 a.Link AS L FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nka WITH(NOLOCK) ON nka.NameKeyId=a.NameKeyId AND nka.Value=N'ActualOperation' "
               L"WHERE a.OwnerId=o.InfoObjectId AND a.Outdated=0 AND ISNULL(a.Link,0)<>0) AS ao "
               L"WHERE l.Link=%ld AND l.Outdated=0 AND ISNULL(l.CollectionElementId,0)=0 "
               L"AND (t.NameKey=N'TypesOperation' OR ao.L IS NOT NULL) "
               L"ORDER BY CASE WHEN t.NameKey=N'TypesOperation' THEN 0 ELSE 1 END, o.DateModified DESC",
               g->dir);
    sql[8999] = 0;
    if (card_query(dbc, sql, rows, 1, err, 300) == 1) {
      anId[i] = rows[0].n1;
      anVar[i] = rows[0].n3;
      lstrcpynW(g->guid, rows[0].s1, 40);
      lstrcpynW(g->from, rows[0].s2, 200);
      anOwn[na++] = anId[i];
      if (anVar[i]) anOwn[na++] = anVar[i];
    }
  }
  if (!na) {
    free(sql);
    return;
  }
  /* 5. цех, участок, оборудование аналога — ссылки на нём или на его варианте (у
     оборудования строки коллекции, но владелец — та же операция) */
  wchar_t own[SO_MAX * 2 * 13 + 2];
  sz_idlist(anOwn, na, own, SO_MAX * 2 * 13 + 2);
  _snwprintf(sql, 9000,
             L"SELECT TOP 400 a.OwnerId, nk.Value, CAST(e.Name AS NVARCHAR(250)), 0, 0 "
             L"FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"AND nk.Value IN (N'WorkShop',N'Area',N'Equipment') "
             L"JOIN InfoObjects AS e WITH(NOLOCK) ON e.InfoObjectId=a.Link "
             L"WHERE a.OwnerId IN (%s) AND a.Outdated=0 AND a.DataType=6 "
             L"ORDER BY a.OwnerId, nk.Value DESC",
             own);
  sql[8999] = 0;
  int ne = card_query(dbc, sql, rows, CARD_ROWS, err, 300);
  for (int i = 0; i < j->n; i++) {
    SzGot *g = &ld->it[i];
    if (!anId[i]) continue;
    wchar_t ws[120] = L"", ar[120] = L"";
    for (int k = 0; k < ne; k++) {
      if (rows[k].n1 != anId[i] && rows[k].n1 != anVar[i]) continue;
      if (!wcscmp(rows[k].s1, L"WorkShop") && !ws[0]) lstrcpynW(ws, rows[k].s2, 120);
      else if (!wcscmp(rows[k].s1, L"Area") && !ar[0]) lstrcpynW(ar, rows[k].s2, 120);
      else if (!wcscmp(rows[k].s1, L"Equipment") && !wcsstr(g->equip, rows[k].s2)) {
        int l = (int)wcslen(g->equip);
        if (l + (int)wcslen(rows[k].s2) + 3 < 120) _snwprintf(g->equip + l, 120 - l, L"%s%s", l ? L", " : L"", rows[k].s2);
      }
    }
    _snwprintf(g->ceh, 120, L"%s%s%s", ws, ws[0] && ar[0] ? L" · " : L"", ar);
    g->ceh[119] = 0;
  }
  /* 6. нормы аналога — в строках составных атрибутов (как у карточки) */
  int nn = card_norms(dbc, own, rows, err);
  for (int i = 0; i < j->n; i++) {
    SzGot *g = &ld->it[i];
    for (int k = 0; k < nn && anId[i]; k++) {
      if (rows[k].n1 != anId[i] && rows[k].n1 != anVar[i]) continue;
      double m = 0;
      wchar_t t[64];
      if (!card_time_text(rows[k].s2, t, 64, &m)) continue;
      if (!wcscmp(rows[k].s1, L"SetupTime") && g->tpz < 0) g->tpz = m;
      if (!wcscmp(rows[k].s1, L"TimePerPiece") && g->tsht < 0) g->tsht = m;
    }
  }
  free(sql);
}

/* новые операции ТП с id в (from, to], и ещё раз — те, у кого не было ссылки на справочник */
static int sz_poll(SQLHDBC dbc, long from, long to, const long *again, int na, CardRow *rows, wchar_t *err) {
  wchar_t extra[400] = L"";
  if (na) {
    wchar_t l[300];
    sz_idlist(again, na, l, 300);
    _snwprintf(extra, 400, L" OR o.InfoObjectId IN (%s)", l);
    extra[399] = 0;
  }
  wchar_t sql[2600];
  _snwprintf(sql, 2600,
             L"SELECT TOP 100 o.InfoObjectId, ISNULL(nr.S,N''), N'', ISNULL(op.L,0), ISNULL(o.ParentId,0) "
             L"FROM InfoObjects AS o WITH(NOLOCK) "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId AND t.NameKey=N'TSOperation' "
             L"OUTER APPLY (SELECT TOP 1 l.Link AS L FROM InfoObjectAttributes AS l WITH(NOLOCK) "
             L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=l.NameKeyId AND nk.Value=N'Operation' "
             L"WHERE l.OwnerId=o.InfoObjectId AND l.Outdated=0 AND ISNULL(l.CollectionElementId,0)=0 "
             L"AND ISNULL(l.Link,0)<>0) AS op "
             L"OUTER APPLY (SELECT TOP 1 CAST(a.ShortText AS NVARCHAR(40)) AS S FROM InfoObjectAttributes AS a WITH(NOLOCK) "
             L"JOIN NameKeys AS nk2 WITH(NOLOCK) ON nk2.NameKeyId=a.NameKeyId AND nk2.Value=N'Number' "
             L"WHERE a.OwnerId=o.InfoObjectId AND a.Outdated=0) AS nr "
             L"WHERE o.Erased=0 AND ((o.InfoObjectId>%ld AND o.InfoObjectId<=%ld)%s) "
             L"ORDER BY o.InfoObjectId",
             from, to, extra);
  sql[2599] = 0;
  return card_query(dbc, sql, rows, 100, err, 300);
}

static DWORD WINAPI sz_thread(LPVOID arg) {
  SzJob *j = (SzJob *)arg;
  SzLoad *ld = (SzLoad *)calloc(1, sizeof(SzLoad));
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * CARD_ROWS);
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[300] = L"";
  g_qTimeout = 30;
  if (!ld || !rows) goto done;
  ld->gen = j->gen;
  ld->n = j->n;
  if (!g_szConnect(&env, &dbc, err, 300)) {
    lstrcpynW(ld->err, err, 300);
    if (!g_szWnd || !PostMessageW(g_szWnd, WM_SZ_READY, 0, (LPARAM)ld)) free(ld);
    ld = NULL;
    sz_post_db(0, err);
    goto done;
  }
  sz_load(dbc, j, ld, rows);
  if (!g_szWnd || !PostMessageW(g_szWnd, WM_SZ_READY, 0, (LPARAM)ld)) free(ld);
  ld = NULL;
  /* Слежение. Каждый круг: сначала верх номеров объектов, потом операции до
     него, с захлёстом на 64 номера назад (номер выдаётся раньше, чем строка
     видна), — так ни одна вставка не проскочит между запросами. Что было до
     начала очереди (floor), не считается. Ссылка на справочник может
     записаться позже самой операции: такие смотрим ещё раз. */
  long last = 0, floor = 0, again[16], seen[64];
  int na = 0, ns = 0, sw = 0, fails = 0, tries[16];
  g_qTimeout = 10;
  if (card_query(dbc, L"SELECT ISNULL(MAX(InfoObjectId),0), N'', N'', 0, 0 FROM InfoObjects WITH(NOLOCK)", rows,
                 1, err, 300) == 1)
    last = floor = rows[0].n1;
  sz_post_db(last > 0, last > 0 ? NULL : err);
  while (last > 0 && j->gen == g_szGen) {
    for (int t = 0; t < 15 && j->gen == g_szGen; t++) Sleep(100);
    if (j->gen != g_szGen) break;
    long top = 0;
    int n = -1;
    if (card_query(dbc, L"SELECT ISNULL(MAX(InfoObjectId),0), N'', N'', 0, 0 FROM InfoObjects WITH(NOLOCK)",
                   rows, 1, err, 300) == 1) {
      top = rows[0].n1;
      n = sz_poll(dbc, last - 64 > floor ? last - 64 : floor, top, again, na, rows, err);
    }
    if (n < 0) {
      if (++fails == 3) sz_post_db(0, err);
      continue;
    }
    if (fails >= 3) sz_post_db(1, NULL);
    fails = 0;
    last = top > last ? top : last;
    int keep = 0;
    long kept[16];
    int keptTries[16];
    for (int i = 0; i < n; i++) {
      long id = rows[i].n1, dir = rows[i].n2;
      int was = 0;
      for (int k = 0; k < ns && !was; k++) was = seen[k] == id;
      if (was) continue;
      if (!dir) { /* ссылки на справочник ещё нет — посмотрим ещё раз, до 20 кругов */
        int t = 0;
        for (int k = 0; k < na; k++)
          if (again[k] == id) t = tries[k];
        if (t < 20 && keep < 16) kept[keep] = id, keptTries[keep++] = t + 1;
        continue;
      }
      seen[sw] = id;
      sw = (sw + 1) % 64;
      if (ns < 64) ns++;
      SzNew *nw = (SzNew *)calloc(1, sizeof(SzNew));
      if (!nw) continue;
      nw->gen = j->gen;
      nw->dir = dir;
      nw->parent = rows[i].n3;
      lstrcpynW(nw->num, rows[i].s1, 16);
      if (!g_szWnd || !PostMessageW(g_szWnd, WM_SZ_NEWOP, 0, (LPARAM)nw)) free(nw);
    }
    memcpy(again, kept, sizeof(long) * (size_t)keep);
    memcpy(tries, keptTries, sizeof(int) * (size_t)keep);
    na = keep;
  }
  SQLDisconnect(dbc);
done:
  if (dbc != SQL_NULL_HDBC) SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  if (env != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, env);
  free(ld);
  free(rows);
  free(j);
  return 0;
}

static void sz_thread_stop(void) {
  InterlockedIncrement(&g_szGen);
  if (g_szThread) {
    /* идущий запрос к базе кончится по своему пределу; окно не ждёт дольше секунды */
    WaitForSingleObject(g_szThread, 1000);
    CloseHandle(g_szThread);
    g_szThread = NULL;
  }
}

/* ---- буфер ------------------------------------------------------------------------ */

static SoItem *sz_cur(void) { return g_szR.cur >= 0 && g_szR.cur < g_szR.n ? &g_szR.it[g_szR.cur] : NULL; }

static BOOL sz_guid_mode(const SoItem *it) { return it && it->guid[0] && !g_szTextOnly; }

static void sz_refresh(void);

/* текущую строку — в буфер; 1С-подобная пауза и повтор, если буфер занят */
static void CALLBACK sz_clip_tick(HWND h, UINT m, UINT_PTR id, DWORD t) {
  (void)h, (void)m, (void)t;
  KillTimer(g_szWnd, id);
  SoItem *it = sz_cur();
  if (!g_szOn || !it) return;
  wchar_t txt[160];
  if (sz_guid_mode(it)) lstrcpynW(txt, it->guid, 160);
  else so_clip_text(it, txt, 160);
  if (!g_szClipSet(txt)) {
    if (++g_szRetry < 20) {
      SetTimer(g_szWnd, TIMER_SZ_CLIP, 100, sz_clip_tick);
      return;
    }
    lstrcpynW(g_szNote, L"Буфер занят другой программой — нажмите «В буфер»", 300);
    g_szClip = 2;
    sz_refresh();
    return;
  }
  g_szRetry = 0;
  g_szClip = 0;
  g_szPend = FALSE;
  sz_refresh();
}

static void sz_put(int delayMs) {
  g_szRetry = 0;
  if (!g_szWnd) return;
  if (delayMs <= 0) sz_clip_tick(g_szWnd, 0, TIMER_SZ_CLIP, 0);
  else SetTimer(g_szWnd, TIMER_SZ_CLIP, (UINT)delayMs, sz_clip_tick);
}

/* человек скопировал своё (cursorpad.c, WM_CLIPBOARDUPDATE) — очередь не трогаем,
   только говорим, что в буфере уже не операция */
static void sz_foreign_copy(void) {
  if (!g_szOn || g_szClip == 2) return;
  g_szClip = 2;
  sz_refresh();
}

static BOOL sz_ours(HWND fg) {
  if (!fg) return FALSE;
  HWND r = GetAncestor(fg, GA_ROOT);
  return r == g_szWnd || r == g_hwnd || onec_ours(fg);
}

/* Ctrl+V текста — как у 1С (pad_extra.c, onec_kbd): следующая строка ложится с
   паузой, чтобы «Союз» успел забрать текущую */
static void CALLBACK sz_paste_tick(HWND h, UINT m, UINT_PTR id, DWORD t) {
  (void)h, (void)m, (void)t;
  KillTimer(g_szWnd, id);
  SoItem *it = sz_cur();
  if (!g_szOn || !it || sz_guid_mode(it) || g_szClip) {
    g_szPend = FALSE;
    return;
  }
  it->pasted = 1;
  if (so_advance(&g_szR) < 0) {
    g_szPend = FALSE;
    lstrcpynW(g_szNote, g_szDb == 2 ? L"Все строки вставлены — жду, когда операции появятся в ТП"
                                    : L"Все строки вставлены", 300);
    sz_refresh();
    return;
  }
  sz_put(0);
}

static LRESULT CALLBACK sz_kbd(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && g_szOn) {
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
    BOOL down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN), up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
    BOOL ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    BOOL alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0, paste = FALSE;
    if (k->vkCode == 'V') {
      if (down && !g_szVDown && ctrl && !alt) paste = TRUE;
      if (down) g_szVDown = TRUE;
      if (up) g_szVDown = FALSE;
    } else if (k->vkCode == VK_INSERT) {
      if (down && !g_szInsDown && shift && !ctrl) paste = TRUE;
      if (down) g_szInsDown = TRUE;
      if (up) g_szInsDown = FALSE;
    }
    if (k->vkCode == VK_ESCAPE && down && g_szAutoBusy) g_szAutoAbort = 1; /* Esc — стоп автоввода */
    SoItem *it = sz_cur();
    if (paste && !g_szPend && it && !sz_guid_mode(it) && !g_szClip && !sz_ours(GetForegroundWindow())) {
      g_szPend = TRUE;
      SetTimer(g_szWnd, TIMER_SZ_PASTE, 450, sz_paste_tick);
    }
  }
  return CallNextHookEx(g_szHook, code, wp, lp);
}

/* ---- окно ------------------------------------------------------------------------- */

static void sz_field(const SoItem *it, int f, wchar_t *out, int cap, BOOL *fromAnalog) {
  out[0] = 0;
  if (fromAnalog) *fromAnalog = FALSE;
  if (!it) return;
  switch (f) {
  case SZF_CODE: lstrcpynW(out, it->code, cap); break;
  case SZF_NAME: lstrcpynW(out, it->name[0] ? it->name : it->op, cap); break;
  case SZF_CEH: lstrcpynW(out, it->ceh, cap); break;
  case SZF_EQUIP: lstrcpynW(out, it->equip, cap); break;
  case SZF_TPZ:
  case SZF_TSHT: {
    double own = f == SZF_TPZ ? it->tpz : it->tsht, an = f == SZF_TPZ ? it->aTpz : it->aTsht;
    if (own < 0 && an >= 0 && fromAnalog) *fromAnalog = TRUE;
    if (own >= 0 || an >= 0) tn_numstr(own >= 0 ? own : an, out, cap);
    break;
  }
  }
}

/* что делать сейчас — одной-двумя фразами */
static void sz_hint(wchar_t *out, int cap) {
  SoItem *it = sz_cur();
  if (!g_szR.n) {
    lstrcpynW(out, L"В маршруте нет операций.", cap);
  } else if (!it) {
    lstrcpynW(out,
              so_left(&g_szR) ? L"Все строки поданы. Как операции появятся в ТП, они отметятся ✓."
                              : L"Маршрут перенесён. Проверьте ТП в «Союзе»: номера, оборудование, нормы, содержание.",
              cap);
  } else if (g_szClip == 2) {
    lstrcpynW(out, L"В буфере уже не операция — вы скопировали своё. «В буфер» — положить её снова.", cap);
  } else if (g_szClip == 1) {
    lstrcpynW(out, L"В буфере — поле из окна. «В буфер» — вернуть туда операцию.", cap);
  } else if (sz_guid_mode(it)) {
    _snwprintf(out, cap,
               L"В буфере — операция-аналог (только для несквозного ТП). Правая кнопка на версии ТП (строка «… :1», не "
               L"«Основной») → «Вставить операцию», или выделите версию и «Вставить» здесь.");
  } else {
    wchar_t t[160];
    so_clip_text(it, t, 160);
    _snwprintf(out, cap,
               L"В буфере — «%s». Правая кнопка на версии ТП → «Создать операцию» (или выделите версию и «Вставить» "
               L"здесь), Ctrl+V в поле «Операция», сохраните — следом ляжет следующая.",
               t);
  }
  out[cap - 1] = 0;
}

static void sz_list_fill(void) {
  if (!g_szList) return;
  SendMessageW(g_szList, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_szList, LB_RESETCONTENT, 0, 0);
  for (int i = 0; i < g_szR.n; i++) {
    const SoItem *it = &g_szR.it[i];
    wchar_t line[260];
    const wchar_t *mark = it->state == SO_DONE ? L"✓" : it->state == SO_SKIP ? L"—" : i == g_szR.cur ? L"→"
                          : it->pasted ? L"…" : L"·";
    _snwprintf(line, 260, L"%s  %s %s  %s%s%s%s", mark, it->num, it->op, it->code[0] ? it->code : L"нет кода",
               it->guid[0] ? L" · аналог" : L"", it->got[0] ? L" · в ТП № " : L"", it->got);
    line[259] = 0;
    SendMessageW(g_szList, LB_ADDSTRING, 0, (LPARAM)line);
  }
  if (g_szR.cur >= 0) SendMessageW(g_szList, LB_SETCURSEL, (WPARAM)g_szR.cur, 0);
  SendMessageW(g_szList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_szList, NULL, TRUE);
}

static void sz_refresh(void) {
  if (!g_szWnd) return;
  SoItem *it = sz_cur();
  for (int f = 0; f < SZF_N; f++) {
    wchar_t v[160];
    sz_field(it, f, v, 160, NULL);
    EnableWindow(GetDlgItem(g_szWnd, ID_SZ_COPY + f), v[0] != 0);
  }
  SetWindowTextW(GetDlgItem(g_szWnd, ID_SZ_MODE), g_szTextOnly ? L"Аналог" : L"● Аналог");
  EnableWindow(GetDlgItem(g_szWnd, ID_SZ_MODE), it && it->guid[0]);
  EnableWindow(GetDlgItem(g_szWnd, ID_SZ_PUT), it != NULL);
  EnableWindow(GetDlgItem(g_szWnd, ID_SZ_AUTO), it && !g_szAutoBusy);
  EnableWindow(GetDlgItem(g_szWnd, ID_SZ_NEXT), it != NULL);
  EnableWindow(GetDlgItem(g_szWnd, ID_SZ_SKIP), it != NULL);
  sz_list_fill();
  InvalidateRect(g_szWnd, NULL, FALSE);
}

#define SZ_ROW 26
static int sz_fields_top(void) { return PANEL_TITLE_H + SZS(176); }

static void sz_layout(void) {
  if (!g_szWnd) return;
  RECT rc;
  GetClientRect(g_szWnd, &rc);
  place_panel_close(g_szWnd);
  int pad = SZS(22), bw = SZS(96), y = sz_fields_top();
  for (int f = 0; f < SZF_N; f++, y += SZS(SZ_ROW))
    MoveWindow(GetDlgItem(g_szWnd, ID_SZ_COPY + f), rc.right - pad - bw, y, bw, SZS(SZ_ROW) - SZS(3), TRUE);
  y += SZS(34); /* строка аналога — рисуется */
  int foot = rc.bottom - SZS(TOOL_FOOT_H), by = foot + (SZS(TOOL_FOOT_H) - SZS(30)) / 2;
  MoveWindow(g_szList, pad, y, rc.right - pad * 2, foot - SZS(54) - y, TRUE);
  int x = SZS(12), w = (rc.right - SZS(24) - SZS(5) * 6) / 7;
  int ids[7] = {ID_SZ_PREV, ID_SZ_SKIP, ID_SZ_MODE, ID_SZ_PUT, ID_SZ_AUTO, ID_SZ_MAKE, ID_SZ_NEXT};
  for (int i = 0; i < 7; i++, x += w + SZS(5)) MoveWindow(GetDlgItem(g_szWnd, ids[i]), x, by, w, SZS(30), TRUE);
  InvalidateRect(g_szWnd, NULL, FALSE);
}

static void sz_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int foot = rc.bottom - SZS(TOOL_FOOT_H);
  RECT card = {SZS(10), PANEL_TITLE_H + SZS(10), rc.right - SZS(10), foot - SZS(10)};
  wchar_t desc[220];
  _snwprintf(desc, 220, L"%s%s%s · %d из %d", g_szR.des, g_szR.des[0] && g_szR.name[0] ? L" " : L"", g_szR.name,
             g_szR.n - so_left(&g_szR), g_szR.n);
  desc[219] = 0;
  tool_frame_paint(hwnd, hdc, L"В Союз", desc, card, foot);
  SetBkMode(hdc, TRANSPARENT);
  int pad = SZS(22), y = PANEL_TITLE_H + SZS(20), right = rc.right - pad;
  SoItem *it = sz_cur();
  wchar_t t[600];
  /* операция крупно, под ней — что делать по маршруту */
  if (g_fontDisplay) SelectObject(hdc, g_fontDisplay);
  SetTextColor(hdc, COL_INK);
  if (it) _snwprintf(t, 600, L"%s  %s", it->num, it->op);
  else lstrcpynW(t, so_left(&g_szR) ? L"Ждём ТП" : L"Готово ✓", 600);
  RECT a = {pad, y, right, y + SZS(30)};
  DrawTextW(hdc, t, -1, &a, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  y += SZS(32);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  if (it) {
    _snwprintf(t, 600, L"%s", it->what);
    t[599] = 0;
    RECT b = {pad, y, right, y + SZS(32)};
    DrawTextW(hdc, t, -1, &b, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX | DT_EDITCONTROL);
  }
  y += SZS(36);
  /* что сделать в «Союзе» — цветом */
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, g_szClip == 2 ? ui_warn(RGB(185, 74, 34)) : COL_SAGE);
  sz_hint(t, 600);
  RECT h = {pad, y, right, sz_fields_top() - SZS(6)};
  DrawTextW(hdc, t, -1, &h, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX | DT_EDITCONTROL);
  /* поля: название слева, значение посередине, «Копировать» справа (кнопки) */
  y = sz_fields_top();
  int bw = SZS(96);
  for (int f = 0; f < SZF_N; f++, y += SZS(SZ_ROW)) {
    BOOL an = FALSE;
    wchar_t v[200];
    sz_field(it, f, v, 160, &an); /* в буфер — число, на экране — с «мин» */
    if (v[0] && (f == SZF_TPZ || f == SZF_TSHT)) {
      double own = f == SZF_TPZ ? it->tpz : it->tsht;
      so_min(own >= 0 ? own : f == SZF_TPZ ? it->aTpz : it->aTsht, v, 160);
      if (an) wcscat(v, L" · у аналога");
    }
    if (!v[0]) lstrcpynW(v, f == SZF_CEH || f == SZF_EQUIP ? L"— (нет аналога в базе)" : L"—", 200);
    if (f == SZF_CODE && it && it->alt[0] && wcslen(v) < 80) wcscat(v, L" · проверить: "), wcscat(v, it->alt);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, COL_MUTED);
    RECT l = {pad, y, pad + SZS(104), y + SZS(SZ_ROW) - SZS(3)};
    DrawTextW(hdc, kSzField[f], -1, &l, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (g_fontBody) SelectObject(hdc, g_fontBody);
    SetTextColor(hdc, COL_INK);
    RECT vr = {pad + SZS(108), y, right - bw - SZS(8), y + SZS(SZ_ROW) - SZS(3)};
    DrawTextW(hdc, v, -1, &vr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  if (it && it->guid[0])
    _snwprintf(t, 600, L"Аналог: %s · с этим кодом в ТП %d", it->from, it->uses);
  else if (it && g_szLoaded)
    _snwprintf(t, 600, L"Аналога нет%s",
               it->dir ? L": с этим кодом ещё нет ни одной операции в ТП"
                       : it->code[0] ? L": кода нет в справочнике операций" : L": операция не сопоставлена");
  else lstrcpynW(t, g_szDb == 1 ? L"Ищу аналоги в базе…" : L"", 600);
  t[599] = 0;
  RECT an = {pad, y + SZS(4), right, y + SZS(30)};
  DrawTextW(hdc, t, -1, &an, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  /* состояние — над подвалом */
  const wchar_t *st = g_szNote[0] ? g_szNote
                      : g_szDb == 2 ? L"Слежу за ТП в базе: вставленная операция отметится ✓"
                      : g_szDb == 3 ? L"Нет связи с базой — переносите по Ctrl+V и «Дальше», без отметок"
                      : g_szDb == 1 ? L"Связываюсь с базой…"
                                    : L"Нет входа в базу (Настройки → PLM): только текст по Ctrl+V";
  RECT sr = {pad, foot - SZS(48), right, foot - SZS(12)}; /* две строки: сообщение автоввода бывает длинным */
  if (g_szNote[0] && wcsncmp(g_szNote, L"Шаг ", 4) == 0) SetTextColor(hdc, ui_warn(RGB(185, 74, 34)));
  DrawTextW(hdc, st, -1, &sr, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX | DT_EDITCONTROL);
}

static void sz_stop(void);

static void sz_select(int i) {
  if (i < 0 || i >= g_szR.n) return;
  g_szR.cur = i;
  g_szR.it[i].pasted = 0;
  if (g_szR.it[i].state == SO_SKIP) g_szR.it[i].state = SO_WAIT;
  g_szNote[0] = 0;
  sz_put(0);
}

static void sz_command(int id) {
  SoItem *it = sz_cur();
  if (id >= ID_SZ_COPY && id < ID_SZ_COPY + SZF_N) {
    wchar_t v[160];
    sz_field(it, id - ID_SZ_COPY, v, 160, NULL);
    if (v[0] && g_szClipSet(v)) {
      g_szClip = 1;
      _snwprintf(g_szNote, 300, L"Скопировано: %s", v);
      g_szNote[299] = 0;
      sz_refresh();
    }
    return;
  }
  switch (id) {
  case ID_SZ_PUT:
    g_szNote[0] = 0;
    sz_put(0);
    break;
  case ID_SZ_AUTO:
    sz_auto_start();
    break;
  case ID_SZ_MAKE: /* деталь и ТП целиком — функцией пакета в клиенте (soyuz_make.c) */
    szm_open();
    break;
  case ID_SZ_NEXT: /* эта строка сделана — следующая */
    if (!it) break;
    it->pasted = 1;
    g_szNote[0] = 0;
    if (so_advance(&g_szR) >= 0) sz_put(0);
    else sz_refresh();
    break;
  case ID_SZ_SKIP:
    if (!it) break;
    it->state = SO_SKIP;
    g_szNote[0] = 0;
    if (so_advance(&g_szR) >= 0) sz_put(0);
    else sz_refresh();
    break;
  case ID_SZ_PREV: { /* шаг назад — та строка снова ждёт */
    int from = g_szR.cur >= 0 ? g_szR.cur : g_szR.n;
    for (int i = from - 1; i >= 0; i--)
      if (g_szR.it[i].state != SO_DONE) {
        sz_select(i);
        return;
      }
    lstrcpynW(g_szNote, L"Раньше — только перенесённые: их не вернуть, в ТП они уже есть", 300);
    sz_refresh();
    break;
  }
  case ID_SZ_MODE:
    g_szTextOnly = !g_szTextOnly;
    g_szNote[0] = 0;
    sz_put(0);
    break;
  case ID_SZ_STOP:
  case ID_PANEL_CLOSE:
    sz_stop();
    break;
  }
}

static LRESULT CALLBACK SzProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    sz_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_PRINTCLIENT: /* снимок окна (стенд) */
    sz_paint(hwnd, (HDC)wParam);
    return 0;
  case WM_MOUSEACTIVATE: /* щелчок по окну не уводит ввод из «Союза» */
    return MA_NOACTIVATE;
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = SZS(600); /* семь кнопок подвала */
    mm->ptMinTrackSize.y = SZS(520);
    return 0;
  }
  case WM_SIZE:
    sz_layout();
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_SZ_READY: {
    SzLoad *ld = (SzLoad *)lParam;
    if (ld && ld->gen == g_szGen && g_szOn) {
      for (int i = 0; i < ld->n && i < g_szR.n; i++) {
        SoItem *it = &g_szR.it[i];
        const SzGot *g = &ld->it[i];
        if (!g->dir) continue;
        it->dir = g->dir;
        lstrcpynW(it->code, g->code, 8);
        if (g->name[0]) lstrcpynW(it->name, g->name, 120);
        it->uses = g->uses;
        lstrcpynW(it->guid, g->guid, 40);
        lstrcpynW(it->from, g->from, 200);
        lstrcpynW(it->ceh, g->ceh, 120);
        lstrcpynW(it->equip, g->equip, 120);
        it->aTpz = g->tpz;
        it->aTsht = g->tsht;
      }
      g_szLoaded = !ld->err[0];
      if (ld->err[0]) _snwprintf(g_szNote, 300, L"База: %s", ld->err), g_szNote[299] = 0;
      if (g_szClip != 2) sz_put(0); /* аналог нашёлся — в буфер его GUID */
      else sz_refresh();
    }
    free(ld);
    return 0;
  }
  case WM_SZ_NEWOP: {
    SzNew *nw = (SzNew *)lParam;
    if (nw && nw->gen == g_szGen && g_szOn) {
      int was = g_szR.cur;
      int hit = so_match(&g_szR, nw->dir, nw->parent, nw->num);
      if (hit >= 0) {
        KillTimer(hwnd, TIMER_SZ_WAIT);
        g_szAutoWait = FALSE;
        _snwprintf(g_szNote, 300, L"%s %s — в ТП под № %s", g_szR.it[hit].num, g_szR.it[hit].op,
                   nw->num[0] ? nw->num : L"?");
        g_szNote[299] = 0;
        if (!so_left(&g_szR)) {
          lstrcpynW(g_szNote, L"Все операции маршрута — в ТП. Проверьте их в «Союзе»", 300);
          MessageBeep(MB_OK);
        }
        if (g_szR.cur != was && g_szR.cur >= 0) sz_put(300); /* «Союз» ещё держит буфер после вставки */
        else sz_refresh();
      }
    }
    free(nw);
    return 0;
  }
  case WM_SZ_AUTO: { /* автоввод нажал пункт (или сказал, почему нет) */
    wchar_t *m = (wchar_t *)lParam;
    if (m) lstrcpynW(g_szNote, m, 300);
    free(m);
    g_szAutoWait = wParam == 1 && g_szOn; /* 1 — «Вставить операцию» нажата; 2 — открыто «Создать операцию» */
    if (g_szAutoWait) SetTimer(hwnd, TIMER_SZ_WAIT, g_szDb == 2 ? 15000 : 3000, NULL);
    sz_refresh();
    return 0;
  }
  case WM_TIMER:
    if (wParam == TIMER_SZ_WAIT) {
      KillTimer(hwnd, TIMER_SZ_WAIT);
      if (g_szAutoWait)
        lstrcpynW(g_szNote,
                  g_szDb == 2 ? L"За 15 с операция не появилась в базе. Посмотрите ТП в «Союзе»: если он что-то спросил — "
                                L"ответьте в его окне."
                              : L"Пункт нажат. Без связи с базой отметить нельзя — проверьте ТП и нажмите «Дальше».",
                  300);
      g_szAutoWait = FALSE;
      sz_refresh();
    }
    return 0;
  case WM_SZ_DB: {
    wchar_t *e = (wchar_t *)lParam;
    g_szDb = wParam ? 2 : 3;
    if (!wParam && e) _snwprintf(g_szNote, 300, L"Нет связи с базой: %s", e), g_szNote[299] = 0;
    else if (wParam && !wcsncmp(g_szNote, L"Нет связи", 9)) g_szNote[0] = 0;
    free(e);
    sz_refresh();
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_SZ_LIST) {
      if (HIWORD(wParam) == LBN_SELCHANGE) {
        int i = (int)SendMessageW(g_szList, LB_GETCURSEL, 0, 0);
        if (i != g_szR.cur && i >= 0 && g_szR.it[i].state != SO_DONE) sz_select(i);
        else if (i != g_szR.cur) sz_list_fill(); /* перенесённую не выбрать: она уже в ТП */
      }
      return 0;
    }
    sz_command(id);
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) sz_stop();
    return 0;
  case WM_CLOSE:
    sz_stop();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* окно клиента «Союза»: процесс PLMClient.exe, видимое главное окно */
typedef struct {
  HWND found;
} SzFind;

static BOOL CALLBACK sz_find_cb(HWND h, LPARAM lp) {
  SzFind *f = (SzFind *)lp;
  if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return TRUE;
  wchar_t path[MAX_PATH];
  DWORD n = MAX_PATH;
  BOOL ok = QueryFullProcessImageNameW(p, 0, path, &n);
  CloseHandle(p);
  if (!ok) return TRUE;
  const wchar_t *b = wcsrchr(path, L'\\');
  if (_wcsicmp(b ? b + 1 : path, L"PLMClient.exe")) return TRUE;
  f->found = h;
  return FALSE;
}

/* рядом с «Союзом»: справа от его окна, если есть место, иначе у правого края его экрана */
static void sz_place(int w, int h, int *x, int *y) {
  SzFind f = {NULL};
  EnumWindows(sz_find_cb, (LPARAM)&f);
  RECT pr = {0, 0, 0, 0};
  HMONITOR mon = MonitorFromWindow(f.found && !IsIconic(f.found) ? f.found : g_hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(mon, &mi);
  RECT wa = mi.rcWork;
  if (f.found && !IsIconic(f.found) && !IsZoomed(f.found)) GetWindowRect(f.found, &pr);
  if (pr.right && pr.right + w + 8 <= wa.right) *x = pr.right + 8;
  else *x = wa.right - w - SZS(16);
  *y = pr.bottom ? pr.top + SZS(60) : wa.top + SZS(80);
  if (*y + h > wa.bottom) *y = wa.bottom - h - 8;
  if (*y < wa.top) *y = wa.top + 8;
}

static void sz_window(void) {
  if (g_szWnd) return;
  HDC s = GetDC(NULL);
  g_szS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
  if (s) ReleaseDC(NULL, s);
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = SzProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = g_paper;
  wc.lpszClassName = L"CursorPadSoyuz";
  RegisterClassExW(&wc);
  int w = SZS(620), h = SZS(700), x, y;
  sz_place(w, h, &x, &y);
  /* WS_EX_NOACTIVATE: «Копировать» и «Дальше» жмутся, а ввод остаётся в «Союзе» */
  g_szWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"CursorPadSoyuz",
                            L"В Союз — CursorPad", WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU, x, y, w,
                            h, NULL, NULL, g_inst, NULL);
  if (!g_szWnd) return;
  round_corners(g_szWnd);
  mk_btn(g_szWnd, L"×", ID_PANEL_CLOSE);
  tool_frame_init(g_szWnd);
  for (int f = 0; f < SZF_N; f++) mk_btn(g_szWnd, L"Копировать", ID_SZ_COPY + f);
  mk_btn(g_szWnd, L"Назад", ID_SZ_PREV);
  mk_btn(g_szWnd, L"Пропуск", ID_SZ_SKIP);
  mk_btn(g_szWnd, L"Аналог", ID_SZ_MODE);
  mk_btn(g_szWnd, L"В буфер", ID_SZ_PUT);
  mk_btn(g_szWnd, L"Вставить", ID_SZ_AUTO);
  mk_btn(g_szWnd, L"Создать ТП", ID_SZ_MAKE);
  mk_btn(g_szWnd, L"Дальше", ID_SZ_NEXT);
  g_szList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                             0, 0, 10, 10, g_szWnd, (HMENU)(INT_PTR)ID_SZ_LIST, g_inst, NULL);
  if (g_fontBody) SendMessageW(g_szList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
  sz_layout();
}

static void sz_stop(void) {
  if (g_szHook) {
    UnhookWindowsHookEx(g_szHook);
    g_szHook = NULL;
  }
  if (g_szWnd) {
    KillTimer(g_szWnd, TIMER_SZ_PASTE);
    KillTimer(g_szWnd, TIMER_SZ_CLIP);
  }
  BOOL was = g_szOn;
  g_szOn = FALSE;
  g_szAutoAbort = 1; /* автоввод, если идёт, бросает на следующем шаге */
  g_szAutoWait = FALSE;
  sz_thread_stop();
  if (g_szWnd) ShowWindow(g_szWnd, SW_HIDE);
  if (was) show_status(L"«В Союз» остановлен");
}

/* «в союз», «/союз», «перенеси в союз», «маршрут в союз» — целым коротким сообщением;
   «есть ли в Союзе…» — это сверка (ai_plm_check.c), не сюда */
static BOOL sz_chat_cmd(const wchar_t *a) {
  wchar_t q[64];
  if (wcslen(a) >= 64) return FALSE;
  so_low(a, q, 64);
  int k = 0; /* «Союз» в кавычках — то же */
  for (int i = 0; q[i]; i++)
    if (q[i] != L'«' && q[i] != L'»' && q[i] != L'"') q[k++] = q[i];
  q[k] = 0;
  for (size_t n = wcslen(q); n && wcschr(L" .!?»", q[n - 1]); n--) q[n - 1] = 0;
  if (!wcscmp(q, L"/союз") || !wcscmp(q, L"/в союз")) return TRUE;
  const wchar_t *p = wcsstr(q, L"в союз");
  if (!p || (p > q && p[-1] != L' ' && p[-1] != L'«')) return FALSE;
  wchar_t after = p[6];
  if (after && after != L' ' && after != L'»' && after != L',') return FALSE; /* «в союзе…» — не команда */
  int words = 1;
  for (const wchar_t *c = q; *c; c++) words += *c == L' ';
  return words <= 5;
}

/* очередь по тексту маршрута; FALSE — в тексте нет строк «005 Операция. …» */
static BOOL sz_start(const wchar_t *route) {
  SoRoute *r = (SoRoute *)malloc(sizeof(SoRoute));
  if (!r) return FALSE;
  if (so_parse(route, r) <= 0) {
    free(r);
    return FALSE;
  }
  sz_stop();
  g_szR = *r;
  free(r);
  g_szR.cur = 0;
  g_szTextOnly = TRUE;
  g_szClip = 0;
  g_szLoaded = FALSE;
  g_szNote[0] = 0;
  g_szPend = FALSE;
  g_szOn = TRUE;
  sz_window();
  if (!g_szWnd) {
    g_szOn = FALSE;
    return FALSE;
  }
  int x, y;
  RECT wr;
  GetWindowRect(g_szWnd, &wr);
  sz_place(wr.right - wr.left, wr.bottom - wr.top, &x, &y);
  SetWindowPos(g_szWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (!g_szNoHook) g_szHook = SetWindowsHookExW(WH_KEYBOARD_LL, sz_kbd, g_inst, 0);
  g_szVDown = g_szInsDown = FALSE;
  /* база — только с собственным входом SQL: раздача через коллегу слежения не умеет */
  g_szDb = 0;
  if ((g_sqlUser[0] && g_sqlPass[0]) || g_szConnect != plm_connect) {
    SzJob *j = (SzJob *)calloc(1, sizeof(SzJob));
    if (j) {
      j->gen = InterlockedIncrement(&g_szGen);
      j->n = g_szR.n;
      for (int i = 0; i < g_szR.n; i++) {
        lstrcpynW(j->code[i], g_szR.it[i].code, 8);
        lstrcpynW(j->alt[i], g_szR.it[i].alt, 64);
      }
      g_szThread = CreateThread(NULL, 0, sz_thread, j, 0, NULL);
      if (g_szThread) g_szDb = 1;
      else free(j);
    }
  }
  sz_put(0);
  return TRUE;
}
