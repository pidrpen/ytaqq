/* pk_stamp.c — чертежи, перетащенные мышью в окно «ЭСИ и ТП» (package_ui.c, с 2026.10.29).
   Каждый лист читается как «Переименовать по штампу» (ai_app_tools.c): обозначение и
   наименование — локальная модель с картинками по графам штампа (мини-ИИ, если включён),
   иначе Tesseract; не прочиталось — из имени файла («АБВГ.123456.002 Корпус.pdf»). PDF
   сначала рисует CursorPadOcr.exe --pdf (fq_pdf_render), в «Чертёж» ложится сам PDF.
   Куда ложится:
     • на пустое место — новые строки «Изделий» (уже есть такое обозначение — дополняется);
     • один чертёж на строку детали — в эту строку (пустые ячейки и файл);
     • на строку сборки (вид «Сборочные единицы» или она уже сборка в «Составе») — новые
       изделия и строки состава «сборка → изделие, 1 шт»; на листе «Состав» — сборка той строки;
     • чертёж сборки (СБ, ВО) — изделие «Сборочные единицы» без кода в обозначении;
       спецификация — сборка и весь её состав со всех листов файла (до PKS_PAGES).
   Сборка без обозначения («+ Сборка» и сразу бросили её СБ вместе с деталями) получает
   обозначение с первого чертежа сборки; состав, прочитанный раньше, достраивается тогда же.
   Операции сразу (g_pkAiOps, «Ещё ▾» → галочка; с 2026.10.29.8): у чертежа детали лист читается
   целиком (модель с картинками и Tesseract, как «Маршрут обработки» в чате), маршрут и заготовку
   решают правила технолога (techno.c), нормы — norms.c; текст маршрута раскладывает «Вставить»
   (pk_paste_text) — операции с кодами 1С на лист «Операции» к этой детали, если своих у неё нет.
   Участок и станок подставит pk_equip_fill. «Операции по чертежам» — то же для уже лежащих строк.
   Чтение — в своём потоке, по одному файлу; результаты — WM_PK_STAMP. «Очистить» прерывает. */

#define PKS_PAGES 8   /* листов спецификации из одного файла */
#define PKS_MAXROWS 400

typedef struct {
  wchar_t des[DR_CELL], name[DR_CELL], kind[40], qty[32], pos[16];
} PkStRow;

typedef struct {
  LONG gen;
  int i, n;                 /* номер файла в броске, сколько всего */
  wchar_t file[MAX_PATH];
  wchar_t des[DR_CELL], name[DR_CELL], kind[40], mat[DR_CELL], mass[40];
  int how;                  /* 2 — модель, 1 — Tesseract, 3 — имя файла, 0 — не прочитано */
  BOOL doubt;               /* обозначение под вопросом */
  BOOL asmDoc;              /* чертёж сборки или спецификация */
  wchar_t docCode[8];       /* «СБ», «ВО», «ГЧ»… — чертёж сборки: сам ляжет документом в её состав */
  wchar_t note[200];
  wchar_t parent[200];      /* сборка, на которую бросили; L"" — нет */
  int parentRow;            /* её строка в «Изделиях» (обозначения у неё может ещё не быть); -1 — нет */
  int row;                  /* строка детали, на которую бросили один чертёж; -1 — нет */
  wchar_t rowDes[200];      /* её обозначение в момент броска: строка всё та же? */
  PkStRow *rows;            /* спецификация */
  int nrows;
  int doc;                  /* pp_draw_doc: 0 — чертёж детали, 1 — сборки, 2 — другой документ (ВП, ПЗ…) */
  BOOL ops, opsOnly;        /* составить и операции; только они (строка уже есть — по файлу) */
  wchar_t *route;           /* маршрут по листу, как в чате (tn_text + нормы); malloc, NULL — нет */
  BOOL spec;                /* лист — спецификация (с 31.10): строк нет — не пустая сборка молча, а пометка */
  BOOL specEmpty;           /* спецификация, а строк не прочитано */
  wchar_t lastSect[40];     /* раздел последней строки: у листа-продолжения (2а) без заголовка — с прошлой страницы */
} PkStRes;

typedef struct PkStJob {
  struct PkStJob *next;
  LONG gen;
  wchar_t (*files)[MAX_PATH];
  int n;
  wchar_t parent[200];
  int parentRow, row;
  wchar_t rowDes[200];
  BOOL ops, opsOnly;
} PkStJob;

static CRITICAL_SECTION g_pksLock;
static BOOL g_pksLockInit, g_pksRunning;
static PkStJob *g_pksQ, *g_pksQTail;
/* итог бросков для строки состояния: сколько прочитано, чем, куда легло */
static int g_pksDone, g_pksTotal, g_pksNew, g_pksMerged, g_pksLinks, g_pksNoDes, g_pksBy[4], g_pksWait, g_pksDocs;
static wchar_t g_pksDoubt[120], g_pksParent[200];
/* строки состава, ждущие обозначения сборки: строка «Изделий» сборки и входящее */
typedef struct {
  int row;
  wchar_t child[DR_CELL];
} PkStPend;
static PkStPend g_pksPend[64];
static int g_pksPendN;
/* скан без обозначения, брошенный на сборку: впишут обозначение — встанет в её состав (по файлу
   строки: номера строк сдвигаются, файл — нет) */
typedef struct {
  wchar_t file[MAX_PATH], parent[200];
  int parentRow;
} PkStOrphan;
static PkStOrphan g_pksOrph[64];
static int g_pksOrphN;
/* операции сразу по чертежу детали: галочка в «Чертежи…» (package.ini); итог для строки состояния */
static BOOL g_pkAiOps = TRUE, g_pkAiOpsRead;
static int g_pksOps, g_pksOpsParts, g_pksOpsHad, g_pksOpsNo;
static int g_pksSpecEmpty;           /* спецификаций без строк (с 31.10) */
static wchar_t g_pksSpecEmptyDes[200]; /* их обозначения — в карточку и строку состояния */
static BOOL g_esiChat;        /* чтение запущено из чата (ai_esi.c): по последнему файлу — карточка в чат */
static void esi_card(void);   /* ai_esi.c */
/* маршрут скана без обозначения: впишут обозначение — операции лягут к нему (по файлу строки) */
typedef struct {
  wchar_t file[MAX_PATH];
  wchar_t *route;
} PkStRouteWait;
static PkStRouteWait g_pksRw[16];
static int g_pksRwN;

/* ---- чтение (рабочий поток) ------------------------------------------------------------ */

static BOOL pks_is_pdf(const wchar_t *p) {
  const wchar_t *d = wcsrchr(p, L'.');
  return d && !lstrcmpiW(d, L".pdf");
}

/* строки спецификации листа — в результат: документация мимо; у стандартных без обозначения
   обозначением становится наименование (как pp_spec_row) */
static void pks_spec_rows(const DrPage *pg, PkStRes *r) {
  for (int k = 0; k < pg->nrows && r->nrows < PKS_MAXROWS; k++) {
    const DrSpecRow *s = &pg->rows[k];
    /* раздел: у строки свой, иначе — последний известный этой спецификации (лист-продолжение 2а без заголовка
       раздела, с одного листа его не узнать; страницы идут по порядку) */
    if (s->c[DS_SECT][0]) lstrcpynW(r->lastSect, s->c[DS_SECT], 40);
    const wchar_t *sect = s->c[DS_SECT][0] ? s->c[DS_SECT] : r->lastSect;
    if (dr_is_docs(sect)) {
      /* «Документация»: сборочный чертёж (СБ, ВО, ГЧ… в конце обозначения) — документом в состав сборки (с 31.10),
         прочие документы (ПЗ, ВП, схемы) — мимо */
      wchar_t d[DR_CELL], nm[DR_CELL], code[8];
      lstrcpynW(d, s->c[DS_DES], DR_CELL);
      lstrcpynW(nm, s->c[DS_NAME], DR_CELL);
      if (!d[0] || pp_draw_doc_code(d, nm, code) != 1) continue;
      if (!r->rows) r->rows = (PkStRow *)calloc(PKS_MAXROWS, sizeof(PkStRow));
      if (!r->rows) return;
      PkStRow *o = &r->rows[r->nrows++];
      lstrcpynW(o->des, s->c[DS_DES], DR_CELL);
      lstrcpynW(o->name, pp_doc_title(code), DR_CELL);
      lstrcpynW(o->kind, L"Документация", 40);
      lstrcpynW(o->qty, L"1", 32);
      o->pos[0] = 0;
      continue;
    }
    const wchar_t *des = s->c[DS_DES][0] ? s->c[DS_DES] : s->c[DS_NAME];
    if (!des[0]) continue;
    if (!r->rows) r->rows = (PkStRow *)calloc(PKS_MAXROWS, sizeof(PkStRow));
    if (!r->rows) return;
    PkStRow *o = &r->rows[r->nrows++];
    lstrcpynW(o->des, des, DR_CELL);
    lstrcpynW(o->name, s->c[DS_DES][0] ? s->c[DS_NAME] : L"", DR_CELL);
    lstrcpynW(o->kind, dr_kind_of(sect), 40);
    lstrcpynW(o->qty, s->c[DS_QTY], 32);
    lstrcpynW(o->pos, s->c[DS_POS], 16);
  }
}

/* штамп первой страницы → обозначение, наименование, вид; спецификация — и её строки */
static void pks_from_page(DrPage *pg, PkStRes *r) {
  wchar_t raw[DR_CELL] = L"", v[DR_CELL];
  /* обозначение: модель по графе — если похоже на обозначение; иначе Tesseract, под вопросом */
  if (pg->vlDes[0] && dr_find_des(pg->vlDes, v, DR_CELL) && dr_des_ok(v)) {
    lstrcpynW(raw, pg->vlDes, DR_CELL);
    r->how = 2;
  } else if (pg->f[DF_DES][0] && dr_find_des(pg->f[DF_DES], v, DR_CELL)) {
    lstrcpynW(raw, pg->f[DF_DES], DR_CELL);
    r->how = 1;
    r->doubt = pg->doubt[DF_DES] || !dr_des_ok(v);
  }
  if (!r->how) return;
  wchar_t name[DR_CELL];
  lstrcpynW(name, rs_name(pg), DR_CELL);
  int doc = pp_draw_doc_code(raw, name, r->docCode); /* «… СБ», «Сборочный чертеж» — сборка; код — прочь */
  if (doc != 1 || pg->type == DRT_SPEC) r->docCode[0] = 0; /* спецификация — подлинник самой сборки */
  r->doc = doc;
  lstrcpynW(r->des, v, DR_CELL);   /* dr_find_des уже без кода и с кириллицей вместо похожей латиницы */
  lstrcpynW(r->name, name, DR_CELL);
  r->asmDoc = doc == 1 || pg->type == DRT_SPEC;
  lstrcpynW(r->kind, r->asmDoc ? L"Сборочные единицы" : pg->type == DRT_DRAW && !doc ? L"Детали" : L"", 40);
  if (pg->type == DRT_DRAW && !doc) { /* материал и масса — только уверенные: ошибочный материал «Союз» не найдёт */
    if (pg->f[DF_MAT][0] && !pg->doubt[DF_MAT]) lstrcpynW(r->mat, pg->f[DF_MAT], DR_CELL);
    if (pg->f[DF_MASS][0] && !pg->doubt[DF_MASS]) lstrcpynW(r->mass, pg->f[DF_MASS], 40);
  }
  if (pg->type == DRT_SPEC) r->spec = TRUE, pks_spec_rows(pg, r);
}

/* спецификация прочитана, а строк нет (модель выключена, Tesseract не узнал столбцы — «спецификация по шапке»):
   не пустая сборка молча, а пометка «строки не прочитаны» (с 31.10). pgNote — что сказало чтение листа */
static void pks_spec_check(PkStRes *r, const wchar_t *pgNote) {
  if (!r->spec || r->nrows) return;
  r->specEmpty = TRUE;
  if (!r->note[0])
    _snwprintf(r->note, 200, L"спецификация %ls: строки не прочитаны%ls%ls — состав впишите или «Ещё ▾ → Состав из файла спецификации»",
               r->des[0] ? r->des : L"без обозначения", pgNote && pgNote[0] ? L" (" : L"", pgNote && pgNote[0] ? pgNote : L"");
  r->note[199] = 0;
}

/* маршрут по прочитанному листу — тот же текст, что «Маршрут обработки» кладёт в чат (без «Лист:»):
   его разбирает «Вставить»; операций нет — NULL */
static wchar_t *pks_route(const DrPage *pg, const wchar_t *mass) {
  TnRoute *tr = (TnRoute *)malloc(sizeof(TnRoute));
  wchar_t *txt = (wchar_t *)malloc(sizeof(wchar_t) * 9000);
  if (tr && txt) {
    dr_tn_route(pg, tr);
    if (tr->n) {
      tn_text(tr, txt, 9000);
      txt = nm_append(txt, tr, mass, NULL); /* нормы времени по таблицам ОМН, если они есть */
      free(tr);
      return txt;
    }
  }
  free(tr);
  free(txt);
  return NULL;
}

static void pks_read(IWICImagingFactory *f, PkStRes *r) {
  wchar_t tmp[MAX_PATH] = L"", why[200] = L"";
  DrPages pl;
  memset(&pl, 0, sizeof(pl));
  BOOL pdf = pks_is_pdf(r->file);
  int pages = 0;
  if (f && pdf) pages = fq_pdf_render(r->file, tmp, why, 200);
  else if (f && tm_is_image_name(r->file)) {
    HRESULT hr = tm_probe(f, r->file, FALSE, dr_pages_cb, &pl);
    pages = pl.n;
    if (!pages) tm_why(hr, why, 200);
  }
  for (int i = 0; i < pages && i < PKS_PAGES; i++) {
    DrPage pg;
    memset(&pg, 0, sizeof(pg));
    if (pdf) _snwprintf(pg.path, MAX_PATH, L"%ls\\p%04d.png", tmp, i + 1), pg.path[MAX_PATH - 1] = 0;
    else lstrcpynW(pg.path, pl.p[i].path, MAX_PATH), pg.frame = pl.p[i].frame;
    pg.vlStamp = !i && dr_vl_ok() ? 2 : 0; /* модель — графы «Обозначение» и «Наименование», лист не спрашивать */
    pg.wantText = !i && r->ops;            /* операции сразу — и весь лист (ТТ, Ra, допуски), как для маршрута */
    dr_recognize(f, &pg);
    if (!i) {
      pks_from_page(&pg, r);
      if (!r->how && pg.note[0]) lstrcpynW(why, pg.note, 200);
      if (pg.type == DRT_SPEC && pg.note[0] && !why[0]) lstrcpynW(why, pg.note, 200);
      if (r->ops && (pg.type == DRT_DRAW || pg.type == DRT_OTHER) && !r->asmDoc)
        r->route = pks_route(&pg, r->mass[0] ? r->mass : pg.f[DF_MASS]);
    } else if (pg.type == DRT_SPEC) {
      pks_spec_rows(&pg, r); /* продолжение спецификации */
    }
    BOOL more = !i && pg.type == DRT_SPEC;
    dr_page_free(&pg);
    if (!more) break; /* у чертежа штамп — на первом листе; дальше листы читаем только у спецификации */
  }
  for (int k = 0; k < pl.n; k++) dr_page_free(&pl.p[k]);
  free(pl.p);
  if (tmp[0]) dr_rmdir(tmp);
  if (!r->des[0]) { /* штамп не прочитался (КОМПАС, DWG, скан без штампа) — имя файла */
    int doc;
    if (pp_file_title_code(r->file, r->des, r->name, &doc, r->docCode)) {
      if (doc != 1) r->docCode[0] = 0;
      r->doc = doc;
      r->how = 3;
      r->asmDoc = doc == 1;
      if (!r->kind[0]) lstrcpynW(r->kind, r->asmDoc ? L"Сборочные единицы" : !doc ? L"Детали" : L"", 40);
    }
  }
  pks_spec_check(r, why);
  if (r->route && (r->asmDoc || r->doc || r->nrows)) {
    free(r->route); /* чертёж сборки, документ или спецификация — операций по ним нет */
    r->route = NULL;
  }
  if (!r->des[0] && !r->opsOnly) lstrcpynW(r->note, why[0] ? why : L"обозначение не прочиталось", 200);
}

static DWORD WINAPI pks_worker(LPVOID arg) {
  (void)arg;
  HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  IWICImagingFactory *f = NULL;
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  for (;;) {
    EnterCriticalSection(&g_pksLock);
    PkStJob *j = g_pksQ;
    if (j) {
      g_pksQ = j->next;
      if (!g_pksQ) g_pksQTail = NULL;
    } else {
      g_pksRunning = FALSE;
    }
    LeaveCriticalSection(&g_pksLock);
    if (!j) break;
    for (int i = 0; i < j->n && j->gen == g_pkStGen; i++) {
      PostMessageW(g_pkWnd, WM_PK_STAMPGO, (WPARAM)(i + 1), (LPARAM)j->n);
      PkStRes *r = (PkStRes *)calloc(1, sizeof(PkStRes));
      if (!r) break;
      r->gen = j->gen;
      r->i = i;
      r->n = j->n;
      r->parentRow = j->parentRow;
      r->row = j->n == 1 ? j->row : -1;
      r->ops = j->ops;
      r->opsOnly = j->opsOnly;
      lstrcpynW(r->file, j->files[i], MAX_PATH);
      lstrcpynW(r->parent, j->parent, 200);
      lstrcpynW(r->rowDes, j->rowDes, 200);
      pks_read(f, r);
      if (!g_pkWnd || !PostMessageW(g_pkWnd, WM_PK_STAMP, 0, (LPARAM)r)) {
        free(r->rows);
        free(r->route);
        free(r);
      }
    }
    free(j->files);
    free(j);
  }
  if (f) IWICImagingFactory_Release(f);
  if (SUCCEEDED(co)) CoUninitialize();
  return 0;
}

/* ---- раскладка (поток окна) ------------------------------------------------------------ */

static int pks_find(const wchar_t *des) {
  for (int r = 0; r < g_pk[PK_ITEMS].n; r++)
    if (!_wcsicmp(xp_cell(&g_pk[PK_ITEMS], r, 0), des)) return r;
  return -1;
}

/* сборка ли строка «Изделий»: вид или она уже сборка в «Составе» */
static BOOL pks_is_asm(int r) {
  XpTable *it = &g_pk[PK_ITEMS];
  if (r < 0 || r >= it->n) return FALSE;
  const wchar_t *k = xp_cell(it, r, 2), *d = xp_cell(it, r, 0);
  return !_wcsicmp(k, L"Сборочные единицы") || !_wcsicmp(k, L"Комплексы") || !_wcsicmp(k, L"Комплекты") ||
         (d[0] && pk_is_parent(d));
}

static void pks_fill_empty(int r, int c, const wchar_t *v) {
  if (v && v[0] && !xp_cell(&g_pk[PK_ITEMS], r, c)[0]) pk_put(&g_pk[PK_ITEMS], r, c, v);
}

static BOOL pks_link(const wchar_t *a, const wchar_t *c, const wchar_t *qty, const wchar_t *pos) {
  if (!a[0] || !c[0] || !_wcsicmp(a, c) || xp_seen2(&g_pk[PK_LINKS], a, c)) return FALSE;
  wchar_t **w = xp_row(&g_pk[PK_LINKS]);
  if (!w) return FALSE;
  xp_set(w, 0, a);
  xp_set(w, 1, c);
  xp_set(w, 2, qty && qty[0] ? qty : L"1");
  xp_set(w, 3, pos);
  return TRUE;
}

/* состав, который ждал обозначения сборки в строке row */
static void pks_pend_flush(int row) {
  const wchar_t *a = row >= 0 && row < g_pk[PK_ITEMS].n ? xp_cell(&g_pk[PK_ITEMS], row, 0) : L"";
  if (!a[0]) return;
  int k = 0;
  for (int i = 0; i < g_pksPendN; i++) {
    if (g_pksPend[i].row == row) {
      if (pks_link(a, g_pksPend[i].child, L"1", L"")) g_pksLinks++;
      g_pksWait--;
    } else {
      g_pksPend[k++] = g_pksPend[i];
    }
  }
  g_pksPendN = k;
  lstrcpynW(g_pksParent, a, 200);
}

/* СБ (ВО, ГЧ…) сборки — строкой вида «Документация» в её состав: «АБВГ.123456.000СБ Сборочный чертеж»,
   файл чертежа — к этой строке (в «Союзе» ляжет подлинником документа) */
/* номер листа в имени файла: «…00СБ-1.tif», «…00СБ_2.pdf» — 1, 2; без него — 0; пусто — большой */
static int pks_sheet_no(const wchar_t *path) {
  if (!path[0]) return 1000;
  const wchar_t *b = tm_base(path), *e = wcsrchr(b, L'.');
  if (!e) e = b + wcslen(b);
  const wchar_t *d = e;
  while (d > b && d[-1] >= L'0' && d[-1] <= L'9') d--;
  return d < e && d > b && (d[-1] == L'-' || d[-1] == L'_') && e - d <= 2 ? _wtoi(d) : 0;
}

static void pks_doc_row(int asmRow, const PkStRes *r) {
  XpTable *it = &g_pk[PK_ITEMS];
  wchar_t a[DR_CELL], dd[DR_CELL];
  lstrcpynW(a, xp_cell(it, asmRow, 0), DR_CELL); /* копия: новая строка двигает ячейки */
  if (!a[0]) return;
  _snwprintf(dd, DR_CELL, L"%ls%ls", a, r->docCode);
  dd[DR_CELL - 1] = 0;
  int row = pks_find(dd);
  if (row < 0) {
    if (!xp_row(it)) return;
    row = it->n - 1;
    pk_put(it, row, 0, dd);
    g_pksNew++;
  }
  pks_fill_empty(row, 1, pp_doc_title(r->docCode));
  pks_fill_empty(row, 2, L"Документация");
  if (pks_sheet_no(r->file) <= pks_sheet_no(xp_cell(it, row, 5))) pk_put(it, row, 5, r->file); /* «…СБ-1» — лист 2: */
  /* подлинник — первый лист, второй его не заменит */
  if (pks_link(a, dd, L"1", L"")) g_pksLinks++;
  g_pksDocs++;
}

/* куда лёг чертёж: строка «Изделий» (у СБ — сама сборка); -1 — никуда */
static int pks_apply(PkStRes *r) {
  XpTable *it = &g_pk[PK_ITEMS];
  int row = -1;
  /* сборка без обозначения, и это её чертёж — он и даёт ей обозначение */
  BOOL asmRow = !r->parent[0] && r->parentRow >= 0 && r->parentRow < it->n && pks_is_asm(r->parentRow) &&
                !xp_cell(it, r->parentRow, 0)[0];
  if (asmRow && r->asmDoc && r->des[0] && pks_find(r->des) < 0) row = r->parentRow;
  else if (r->row >= 0 && r->row < it->n && !_wcsicmp(xp_cell(it, r->row, 0), r->rowDes)) row = r->row;
  BOOL target = row >= 0;
  if (target && r->des[0] && !xp_cell(it, row, 0)[0]) {
    int other = pks_find(r->des); /* бросили на пустую строку, а такое изделие уже есть ниже */
    if (other >= 0 && other != row) row = other, target = FALSE;
  }
  if (!target && r->des[0]) row = pks_find(r->des);
  BOOL fresh = row < 0;
  if (fresh) {
    if (!xp_row(it)) return -1;
    row = it->n - 1;
  }
  const wchar_t *have = xp_cell(it, row, 0);
  if (target && have[0] && r->des[0] && _wcsicmp(have, r->des))
    _snwprintf(r->note, 200, L"на чертеже «%ls», строка «%ls» оставлена", r->des, have);
  pks_fill_empty(row, 0, r->des);
  pks_fill_empty(row, 1, r->name);
  if (r->asmDoc && !_wcsicmp(xp_cell(it, row, 2), L"Детали")) pk_put(it, row, 2, r->kind); /* «Детали» — пустышка вида */
  pks_fill_empty(row, 2, r->kind);
  pks_fill_empty(row, 3, r->mat);
  pks_fill_empty(row, 4, r->mass);
  BOOL docDraw = r->docCode[0] != 0; /* СБ: файл — не сборке, а её документу */
  if (!docDraw && (target || !xp_cell(it, row, 5)[0])) pk_put(it, row, 5, r->file); /* на свою строку — файл заменить */
  if (fresh) g_pksNew++;
  else g_pksMerged++;
  if (r->how >= 1 && r->how <= 3) g_pksBy[r->how]++;
  if (!r->des[0]) g_pksNoDes++;
  if (r->doubt && wcslen(g_pksDoubt) + wcslen(r->des) + 3 < 120)
    wcscat(g_pksDoubt, g_pksDoubt[0] ? L", " : L""), wcscat(g_pksDoubt, r->des);
  const wchar_t *des = xp_cell(it, row, 0);
  if (asmRow && row == r->parentRow) pks_pend_flush(row); /* у сборки появилось обозначение */
  /* в состав сборки, на которую бросили */
  const wchar_t *parent = r->parent[0] ? r->parent
                          : r->parentRow >= 0 && r->parentRow < it->n ? xp_cell(it, r->parentRow, 0) : L"";
  if (row != r->parentRow && des[0]) {
    if (parent[0]) {
      if (pks_link(parent, des, L"1", L"")) g_pksLinks++;
      lstrcpynW(g_pksParent, parent, 200);
    } else if (r->parentRow >= 0 && g_pksPendN < (int)(sizeof(g_pksPend) / sizeof(g_pksPend[0]))) {
      g_pksPend[g_pksPendN].row = r->parentRow;
      lstrcpynW(g_pksPend[g_pksPendN++].child, des, DR_CELL);
      g_pksWait++;
    }
  } else if (row != r->parentRow && (parent[0] || r->parentRow >= 0) && r->file[0] &&
             g_pksOrphN < (int)(sizeof(g_pksOrph) / sizeof(g_pksOrph[0]))) {
    PkStOrphan *o = &g_pksOrph[g_pksOrphN++];
    lstrcpynW(o->file, r->file, MAX_PATH);
    lstrcpynW(o->parent, r->parent, 200);
    o->parentRow = r->parentRow;
  }
  /* спецификация: её состав — сразу в «Состав», новые позиции — в «Изделия» */
  for (int k = 0; k < r->nrows && des[0]; k++) {
    PkStRow *s = &r->rows[k];
    if (pks_find(s->des) < 0 && (s->name[0] || s->kind[0])) {
      wchar_t **w = xp_row(it);
      if (w) {
        xp_set(w, 0, s->des);
        xp_set(w, 1, s->name);
        xp_set(w, 2, s->kind);
        g_pksNew++;
      }
      des = xp_cell(it, row, 0); /* строки переехали */
    }
    if (pks_link(des, s->des, s->qty, s->pos)) g_pksLinks++;
  }
  if (r->nrows) lstrcpynW(g_pksParent, des, 200);
  if (docDraw) pks_doc_row(row, r);
  return row;
}

static BOOL pks_has_ops(const wchar_t *des) {
  for (int k = 0; k < g_pk[PK_OPS].n; k++)
    if (!_wcsicmp(xp_cell(&g_pk[PK_OPS], k, 0), des)) return TRUE;
  return FALSE;
}

/* маршрут чертежа — операциями детали в строке row: разбирает «Вставить», как маршрут из чата
   («Лист: файл · обозначение» — к этой детали). Свои операции у детали уже есть — не трогаем.
   Обозначения ещё нет — маршрут ждёт его (pk_stamp_typed); route забирается */
static void pks_ops(int row, const wchar_t *file, wchar_t **route) {
  XpTable *it = &g_pk[PK_ITEMS];
  if (!*route || row < 0 || row >= it->n || pks_is_asm(row)) return;
  const wchar_t *des = xp_cell(it, row, 0);
  if (!des[0]) {
    if (g_pksRwN < (int)(sizeof(g_pksRw) / sizeof(g_pksRw[0]))) {
      lstrcpynW(g_pksRw[g_pksRwN].file, file, MAX_PATH);
      g_pksRw[g_pksRwN++].route = *route;
      *route = NULL;
    }
    return;
  }
  if (pks_has_ops(des)) {
    g_pksOpsHad++;
    return;
  }
  size_t n = wcslen(*route) + wcslen(des) + MAX_PATH + 16;
  wchar_t *t = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (!t) return;
  _snwprintf(t, n, L"Лист: %ls · %ls\n%ls", tm_base(file), des, *route);
  t[n - 1] = 0;
  wchar_t d[DR_CELL], msg[400];
  lstrcpynW(d, des, DR_CELL); /* «Вставить» может дописать строки «Изделий» — des переедет */
  int show, was = g_pk[PK_OPS].n;
  pk_paste_text(t, PK_OPS, L"", d, &show, msg, 400);
  free(t);
  if (g_pk[PK_OPS].n > was) g_pksOps += g_pk[PK_OPS].n - was, g_pksOpsParts++;
  else g_pksOpsNo++;
}

/* обозначение в «Изделиях» вписали руками: сборка — её состав из ждавших; скан без обозначения —
   в состав сборки, на которую его бросили */
static void pk_stamp_typed(int row) {
  XpTable *it = &g_pk[PK_ITEMS];
  if (row < 0 || row >= it->n || !xp_cell(it, row, 0)[0]) return;
  int was = g_pksLinks;
  pks_pend_flush(row);
  const wchar_t *des = xp_cell(it, row, 0), *file = xp_cell(it, row, 5);
  wchar_t parent[200] = L"";
  for (int i = 0; file[0] && i < g_pksOrphN; i++) {
    if (_wcsicmp(g_pksOrph[i].file, file)) continue;
    PkStOrphan o = g_pksOrph[i];
    g_pksOrph[i] = g_pksOrph[--g_pksOrphN];
    lstrcpynW(parent, o.parent[0] ? o.parent : o.parentRow >= 0 && o.parentRow < it->n ? xp_cell(it, o.parentRow, 0) : L"",
              200);
    if (parent[0]) {
      if (pks_link(parent, des, L"1", L"")) g_pksLinks++;
    } else if (o.parentRow >= 0 && g_pksPendN < (int)(sizeof(g_pksPend) / sizeof(g_pksPend[0]))) {
      g_pksPend[g_pksPendN].row = o.parentRow;
      lstrcpynW(g_pksPend[g_pksPendN++].child, des, DR_CELL);
      g_pksWait++;
      pk_status(L"«%s» встанет в состав сборки, когда у неё будет обозначение", des);
    }
    if (g_pksNoDes > 0) g_pksNoDes--;
    break;
  }
  int opsWas = g_pksOps;
  for (int i = 0; file[0] && i < g_pksRwN; i++) {
    if (_wcsicmp(g_pksRw[i].file, file)) continue;
    wchar_t *route = g_pksRw[i].route;
    wchar_t f[MAX_PATH];
    lstrcpynW(f, g_pksRw[i].file, MAX_PATH);
    g_pksRw[i] = g_pksRw[--g_pksRwN];
    pks_ops(row, f, &route);
    free(route);
    break;
  }
  if (g_pksOps > opsWas) {
    pk_status(L"К «%s» — операций по чертежу %d: лист «Операции», проверьте", xp_cell(it, row, 0), g_pksOps - opsWas);
    return;
  }
  if (g_pksLinks > was)
    pk_status(L"В состав «%s»: %d — см. лист «Состав»", parent[0] ? parent : des, g_pksLinks - was);
}

/* «Очистить»: ждать больше нечего */
static void pk_stamp_forget(void) {
  g_pksPendN = g_pksOrphN = 0;
  g_pksWait = 0;
  for (int i = 0; i < g_pksRwN; i++) free(g_pksRw[i].route);
  g_pksRwN = 0;
}

/* итог — в строку состояния */
static void pks_status(const wchar_t *note) {
  wchar_t by[120] = L"", wait[120] = L"", docs[80] = L"", ops[200] = L"";
  if (g_pksDocs) _snwprintf(docs, 80, L" (из них сборочных чертежей документом: %d)", g_pksDocs);
  if (g_pksOps || g_pksOpsHad || g_pksOpsNo || g_pksRwN) {
    int l = _snwprintf(ops, 200, L"; операций по чертежам %d у %d дет. — лист «Операции», проверьте", g_pksOps, g_pksOpsParts);
    if (l > 0 && g_pksOpsHad) l += _snwprintf(ops + l, 200 - l, L"; у %d свои уже были — не тронуты", g_pksOpsHad);
    if (l > 0 && l < 200 && g_pksOpsNo) l += _snwprintf(ops + l, 200 - l, L"; без маршрута %d", g_pksOpsNo);
    if (l > 0 && l < 200 && g_pksRwN) _snwprintf(ops + l, 200 - l, L"; %d ждут обозначения", g_pksRwN);
    ops[199] = 0;
  }
  if (g_pksBy[2] || g_pksBy[1] || g_pksBy[3])
    _snwprintf(by, 120, L"; обозначения: моделью %d, Tesseract %d, из имени файла %d", g_pksBy[2], g_pksBy[1], g_pksBy[3]);
  if (g_pksWait) _snwprintf(wait, 120, L"; %d ждут обозначения сборки — впишите его", g_pksWait);
  else if (g_pksSpecEmpty) _snwprintf(wait, 120, L"; спецификаций без строк %d — состав не прочитан, проверьте", g_pksSpecEmpty);
  pk_status(L"Чертежи: %d из %d — новых изделий %d, дополнено %d, в состав%ls%ls%ls %d%ls%ls%ls%ls%ls%ls%ls%ls%ls",
            g_pksDone, g_pksTotal, g_pksNew, g_pksMerged, g_pksParent[0] ? L" «" : L"", g_pksParent,
            g_pksParent[0] ? L"»" : L"", g_pksLinks, docs, ops, by, g_pksNoDes ? L"; без обозначения — впишите" : L"",
            g_pksDoubt[0] ? L"; проверьте: " : L"", g_pksDoubt, wait, note && note[0] ? L"; " : L"",
            note ? note : L"");
}

/* «Операции по чертежам»: только операции — итог отдельно */
static void pks_ops_status(void) {
  pk_status(L"Операции по чертежам: %d из %d — операций %d у %d дет.%ls%ls — лист «Операции», участок и станок проверьте",
            g_pksDone, g_pksTotal, g_pksOps, g_pksOpsParts, g_pksOpsHad ? L"; у кого свои были — не тронуты" : L"",
            g_pksOpsNo ? L"; у части маршрута нет (лист не прочитался)" : L"");
}

/* WM_PK_STAMP: прочитанный чертёж — в листы */
static void pk_stamp_done(void *p) {
  PkStRes *r = (PkStRes *)p;
  if (r->gen == g_pkStGen) {
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    if (r->opsOnly) { /* строка уже есть: та, у которой этот файл */
      int row = -1;
      for (int k = 0; k < g_pk[PK_ITEMS].n && row < 0; k++)
        if (!_wcsicmp(xp_cell(&g_pk[PK_ITEMS], k, 5), r->file)) row = k;
      if (r->route) pks_ops(row, r->file, &r->route);
      else g_pksOpsNo++;
    } else {
      int row = pks_apply(r);
      if (r->specEmpty) {
        g_pksSpecEmpty++;
        size_t l = wcslen(g_pksSpecEmptyDes);
        if (l < 150) _snwprintf(g_pksSpecEmptyDes + l, 200 - l, L"%ls%ls", l ? L", " : L"", r->des[0] ? r->des : L"?");
        g_pksSpecEmptyDes[199] = 0;
      }
      if (r->route) pks_ops(row, r->file, &r->route);
      else if (r->ops && !r->asmDoc && !r->doc && !r->nrows) g_pksOpsNo++;
    }
    g_pksDone++;
    pk_fill(FALSE);
    pk_changed();
    BOOL last = r->i == r->n - 1;
    if (last && g_esiChat) esi_card();
    if (r->opsOnly) pks_ops_status();
    else
      pks_status(r->note[0] ? r->note : last && !dr_vl_ok() && g_pksBy[1]
                                            ? L"мини-ИИ с картинками выключен — Tesseract путает буквы, сверьте обозначения"
                                            : last && r->ops && !dr_vl_ok() && g_pksOps
                                                ? L"мини-ИИ с картинками выключен — маршрут по Tesseract, сверьте операции"
                                                : L"");
  }
  free(r->rows);
  free(r->route);
  free(r);
}

/* WM_PK_STAMPGO: читается следующий файл (счёт — по всем броскам подряд) */
static void pk_stamp_progress(void) {
  pk_status(L"Читаю %ls: %d из %d%ls… (бросать можно ещё; «Очистить» — прервать)",
            g_pkAiOps ? L"чертёж — штамп и лист для операций" : L"штамп", g_pksDone + 1, g_pksTotal,
            dr_vl_ok() ? L", мини-ИИ" : L"");
}

/* галочка «операции сразу» — в package.ini папки данных */
static void pks_ops_pref(void) {
  if (g_pkAiOpsRead || !g_dataDir[0]) return;
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%ls\\package.ini", g_dataDir);
  p[MAX_PATH - 1] = 0;
  g_pkAiOps = GetPrivateProfileIntW(L"Чертежи", L"Операции", 1, p) != 0;
  g_pkAiOpsRead = TRUE;
}

static BOOL pk_stamp_ops_on(void) {
  pks_ops_pref();
  return g_pkAiOps;
}

static void pks_ops_toggle(void) {
  pks_ops_pref();
  g_pkAiOps = !g_pkAiOps;
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%ls\\package.ini", g_dataDir);
  p[MAX_PATH - 1] = 0;
  if (g_dataDir[0]) WritePrivateProfileStringW(L"Чертежи", L"Операции", g_pkAiOps ? L"1" : L"0", p);
  pk_status(g_pkAiOps ? L"Чертёж детали — сразу и операции: мини-ИИ читает лист, маршрут и нормы — по правилам технолога"
                      : L"Чертежи — только штамп; операции — «Чертежи…» → «Операции по чертежам»");
}

/* файл или папка (её картинки) — в список чтения */
static void pks_add_path(TmPaths *l, const wchar_t *p) {
  DWORD a = GetFileAttributesW(p);
  if (a == INVALID_FILE_ATTRIBUTES) return;
  if (a & FILE_ATTRIBUTE_DIRECTORY) tm_paths_add_any(l, p); /* папка — её картинки */
  else tm_paths_add(l, p);                                   /* файл — любой: PDF, КОМПАС, DWG — по имени */
}

/* файлы — в очередь чтения; hit — строка открытого листа, куда их отдали (-1 — никуда);
   opsOnly — строки уже есть, нужны только операции по их чертежам */
static void pks_enqueue(TmPaths *pl, int hit, BOOL opsOnly) {
  TmPaths l = *pl;
  if (!l.n) {
    pk_status(L"Чертежей не нашлось — бросьте файлы (PDF, TIF, PNG…) или папку с ними");
    free(l.p);
    return;
  }
  if (!opsOnly) tm_paths_sort(&l);
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  pk_init();
  pks_ops_pref();
  PkStJob *j = (PkStJob *)calloc(1, sizeof(PkStJob));
  if (!j) {
    free(l.p);
    return;
  }
  j->files = l.p;
  j->n = l.n;
  j->parentRow = j->row = -1;
  j->ops = opsOnly || g_pkAiOps;
  j->opsOnly = opsOnly;
  wchar_t note[200] = L"";
  if (opsOnly) hit = -1;
  if (g_pkSheet == PK_LINKS && hit >= 0 && hit < g_pk[PK_LINKS].n) { /* на строку состава — в её сборку */
    lstrcpynW(j->parent, xp_cell(&g_pk[PK_LINKS], hit, 0), 200);
    j->parentRow = pks_find(j->parent);
  } else if (g_pkSheet == PK_ITEMS && hit >= 0 && hit < g_pk[PK_ITEMS].n) {
    const wchar_t *d = xp_cell(&g_pk[PK_ITEMS], hit, 0);
    if (pks_is_asm(hit)) { /* на сборку — в её состав (обозначения ещё нет — даст её СБ или впишут) */
      lstrcpynW(j->parent, d, 200);
      j->parentRow = hit;
    } else if (l.n == 1) {
      j->row = hit;
      lstrcpynW(j->rowDes, d, 200);
    } else {
      lstrcpynW(note, L"на деталь бросили несколько — легли новыми строками; в состав — бросайте на строку сборки", 200);
    }
  }
  if (!opsOnly && g_pkSheet != PK_ITEMS && g_pkSheet != PK_LINKS) {
    g_pkSheet = PK_ITEMS;
    pk_fill(TRUE);
  }
  j->gen = g_pkStGen;
  if (!g_pksLockInit) {
    InitializeCriticalSection(&g_pksLock);
    g_pksLockInit = TRUE;
  }
  EnterCriticalSection(&g_pksLock);
  BOOL idle = !g_pksRunning;
  if (idle) { /* новый бросок после затишья — итог считать заново */
    g_pksDone = g_pksTotal = g_pksNew = g_pksMerged = g_pksLinks = g_pksNoDes = g_pksWait = g_pksDocs = 0;
    g_pksOps = g_pksOpsParts = g_pksOpsHad = g_pksOpsNo = 0;
    g_pksSpecEmpty = 0, g_pksSpecEmptyDes[0] = 0;
    memset(g_pksBy, 0, sizeof(g_pksBy));
    g_pksDoubt[0] = g_pksParent[0] = 0;
  }
  g_pksTotal += j->n;
  if (g_pksQTail) g_pksQTail->next = j;
  else g_pksQ = j;
  g_pksQTail = j;
  if (idle) g_pksRunning = TRUE;
  LeaveCriticalSection(&g_pksLock);
  if (idle) {
    HANDLE t = CreateThread(NULL, 0, pks_worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else g_pksRunning = FALSE;
  }
  if (opsOnly) {
    pk_status(L"Операции по чертежам: читаю %d %ls%ls…", g_pksTotal, g_pksTotal == 1 ? L"лист" : L"листов",
              dr_vl_ok() ? L" (мини-ИИ)" : L" (Tesseract — мини-ИИ с картинками выключен)");
    return;
  }
  pk_status(L"Читаю штампы: %d %ls%ls%ls%ls%ls%ls…", g_pksTotal, g_pksTotal == 1 ? L"чертёж" : L"чертежей",
            j->parent[0] ? L" → в состав «" : j->parentRow >= 0 ? L" → в состав новой сборки" : L"", j->parent,
            j->parent[0] ? L"»" : L"", note[0] ? L"; " : L"", note);
}

/* WM_DROPFILES: файлы (и картинки из папок) — в очередь чтения, цель — строка под мышью */
static void pk_stamp_drop(HWND hwnd, HDROP hd) {
  POINT pt;
  DragQueryPoint(hd, &pt);
  TmPaths l = {0};
  UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
  for (UINT i = 0; i < n; i++) {
    wchar_t p[MAX_PATH];
    if (DragQueryFileW(hd, i, p, MAX_PATH)) pks_add_path(&l, p);
  }
  DragFinish(hd);
  SetForegroundWindow(hwnd);
  MapWindowPoints(hwnd, g_pkList, &pt, 1);
  LVHITTESTINFO hi;
  memset(&hi, 0, sizeof(hi));
  hi.pt = pt;
  int hit = (int)SendMessageW(g_pkList, LVM_HITTEST, 0, (LPARAM)&hi);
  pks_enqueue(&l, hi.flags & LVHT_ONITEM ? hit : -1, FALSE);
}

/* «Чертежи…» → «Выбрать файлы…»: то же, что бросить их на выделенную строку (нет — в новые строки) */
static void pk_stamp_pick(void) {
  enum { CAP = 32768 };
  wchar_t *buf = (wchar_t *)calloc(CAP, sizeof(wchar_t));
  if (!buf) return;
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_pkWnd;
  of.lpstrFilter = L"Чертежи (PDF, сканы, КОМПАС, DWG)\0*.pdf;*.tif;*.tiff;*.jpg;*.jpeg;*.png;*.bmp;*.cdw;*.frw;*.dwg;*.dxf\0"
                   L"Все файлы\0*.*\0";
  of.lpstrFile = buf;
  of.nMaxFile = CAP;
  of.lpstrTitle = L"Чертежи — обозначение и наименование прочитаются со штампа";
  of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
  if (!GetOpenFileNameW(&of)) {
    free(buf);
    return;
  }
  TmPaths l = {0};
  const wchar_t *first = buf, *next = buf + wcslen(buf) + 1;
  if (!*next) pks_add_path(&l, first); /* один файл — полный путь */
  for (; *next; next += wcslen(next) + 1) { /* несколько: папка, потом имена */
    wchar_t p[MAX_PATH];
    if (wcslen(first) + wcslen(next) + 2 > MAX_PATH) continue;
    _snwprintf(p, MAX_PATH, L"%ls\\%ls", first, next);
    p[MAX_PATH - 1] = 0;
    pks_add_path(&l, p);
  }
  free(buf);
  int sel = ListView_GetNextItem(g_pkList, -1, LVNI_SELECTED);
  pks_enqueue(&l, sel >= 0 && sel < g_pk[g_pkSheet].n && ListView_GetSelectedCount(g_pkList) == 1 ? sel : -1, FALSE);
}

/* «Чертежи…» → «Операции по чертежам»: деталям с файлом чертежа и без своих операций (выделены —
   только выделенным) — маршрут по листу, как при броске */
/* «Чертежи…» окна «ЭСИ и ТП» (с 30.10 — первая кнопка подвала, без меню): выбрать файлы — штампы,
   состав и операции деталей сразу на листы, как делал убранный пункт «Загрузить ЭСИ в Союз» меню
   «Ещё»; редкое (архив по обозначениям, галочка операций, операции деталям с файлом) — в «Ещё ▾» */
static void pk_draw_pick(void) {
  pks_ops_pref();
  g_pkAiOps = TRUE;
  pk_stamp_pick();
}

/* Состав из файла спецификации (с 31.10): TSV UTF-8, строка — «Раздел<Tab>Поз.<Tab>Обозначение<Tab>Наименование<Tab>
   Кол.<Tab>Примечание» (как DrSpecRow без формата и зоны); первая строка «#СБОРКА<Tab><обозначение><Tab><наименование>» —
   чей это состав, иначе — выделенная сборка «Изделий» или выбранная позиция. Строки идут тем же путём, что строки
   листа-спецификации (pks_spec_rows → pks_apply): позиции — в «Изделия», состав — сборке, СБ из «Документации» —
   документом к ней; документы прочие — мимо. Для стенда и ручного импорта, пока модель не читает спецификации
   (её строки ложатся в те же DrSpecRow, pg->rowsBy = 2; из файла — 3). Сколько строк состава; -1 — файл не прочитан */
static int pk_spec_tsv(const wchar_t *path) {
  wchar_t *t = share_read(path);
  if (!t) return -1;
  DrPage pg;
  memset(&pg, 0, sizeof(pg));
  pg.type = DRT_SPEC;
  pg.rows = (DrSpecRow *)calloc(PKS_MAXROWS, sizeof(DrSpecRow));
  PkStRes *r = (PkStRes *)calloc(1, sizeof(PkStRes));
  if (!pg.rows || !r) {
    free(pg.rows), free(r), free(t);
    return -1;
  }
  r->parentRow = r->row = -1;
  r->how = 3;
  r->asmDoc = TRUE;
  lstrcpynW(r->kind, L"Сборочные единицы", 40);
  lstrcpynW(r->file, path, MAX_PATH);
  wchar_t *pp = t, *line;
  while ((line = share_next_line(&pp)) != NULL && pg.nrows < PKS_MAXROWS) {
    wchar_t *f[8] = {0};
    while (*line == 0xFEFF) line++; /* метка UTF-8 в начале файла */
    if (!wcsncmp(line, L"#СБОРКА\t", 8) || !wcsncmp(line, L"# СБОРКА\t", 9)) {
      int n = share_split(line, f, 8);
      if (n >= 2) lstrcpynW(r->des, f[1], DR_CELL);
      if (n >= 3) lstrcpynW(r->name, f[2], DR_CELL);
      continue;
    }
    if (!line[0] || line[0] == L'#') continue;
    int n = share_split(line, f, 8);
    if (n < 3) continue;
    DrSpecRow *o = &pg.rows[pg.nrows];
    static const int kCol[6] = {DS_SECT, DS_POS, DS_DES, DS_NAME, DS_QTY, DS_NOTE};
    for (int c = 0; c < 6 && c < n; c++) {
      const wchar_t *v = f[c];
      while (*v == L' ') v++;
      lstrcpynW(o->c[kCol[c]], v, DR_CELL);
      for (size_t l = wcslen(o->c[kCol[c]]); l && o->c[kCol[c]][l - 1] == L' '; l--) o->c[kCol[c]][l - 1] = 0;
    }
    if (o->c[DS_DES][0] || o->c[DS_NAME][0]) pg.nrows++;
  }
  free(t);
  pg.rowsBy = 3;
  if (!r->des[0]) { /* сборка — выделенная строка «Изделий» или выбранная позиция */
    XpTable *it = &g_pk[PK_ITEMS];
    int sr = g_pkSheet == PK_ITEMS && g_pkList ? pk_sel_row() : -1;
    const wchar_t *d = sr >= 0 && sr < it->n && pks_is_asm(sr) ? xp_cell(it, sr, 0) : g_pkCur;
    lstrcpynW(r->des, d, DR_CELL);
  }
  int links = -1;
  if (r->des[0]) {
    pks_spec_rows(&pg, r);
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    int was = g_pksLinks;
    pks_apply(r);
    links = g_pksLinks - was;
    pk_fill(FALSE);
    pk_changed();
  }
  free(pg.rows);
  free(r->rows);
  free(r->route);
  free(r);
  return links;
}

static void pk_spec_tsv_pick(void) {
  wchar_t file[MAX_PATH] = L"";
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_pkWnd;
  of.lpstrFilter = L"Строки спецификации (TSV, TXT)\0*.tsv;*.txt\0Все файлы\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&of)) return;
  int n = pk_spec_tsv(file);
  if (n < 0)
    pk_status(L"Файл спецификации не прочитан — или нет строки «#СБОРКА<Tab>обозначение», а сборка не выделена");
  else
    pk_status(L"Из файла спецификации в состав сборки легло строк: %d — проверьте количество и разделы, затем «В Союз…»", n);
}

static void pk_stamp_ops(void) {
  XpTable *it = &g_pk[PK_ITEMS];
  TmPaths l = {0};
  int sel = g_pkSheet == PK_ITEMS && g_pkList ? ListView_GetSelectedCount(g_pkList) : 0, had = 0, nofile = 0;
  for (int r = 0; r < it->n; r++) {
    if (sel && !(ListView_GetItemState(g_pkList, r, LVIS_SELECTED) & LVIS_SELECTED)) continue;
    const wchar_t *des = xp_cell(it, r, 0), *file = xp_cell(it, r, 5);
    if (!des[0] || pks_is_asm(r) || pk_kind_noname(xp_cell(it, r, 2)) || !_wcsicmp(xp_cell(it, r, 2), L"Документация"))
      continue;
    if (!file[0] || GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) {
      nofile++;
      continue;
    }
    if (pks_has_ops(des)) {
      had++;
      continue;
    }
    tm_paths_add(&l, file);
  }
  if (!l.n) {
    pk_status(L"Операции по чертежам: некому — %ls%ls", had ? L"у деталей с чертежом операции уже есть" : L"",
              nofile ? (had ? L"; у остальных нет файла чертежа (столбец «Чертёж (файл)»)" : L"у деталей нет файла чертежа (столбец «Чертёж (файл)»)")
                     : had ? L"" : L"деталей нет");
    free(l.p);
    return;
  }
  pks_enqueue(&l, -1, TRUE);
}

#ifdef SET_TEST
/* проверка без мыши: CP_PKDROP="строка|файл|файл…" — бросок на строку листа (-1 — пустое место) */
static void pk_stamp_test_drop(void) {
  /* CP_PKFIT="код участка|участок|станок|код операции" — справочник «Союза» без базы: подбор станка */
  const wchar_t *fit = _wgetenv(L"CP_PKFIT");
  if (fit && !g_szmDicts.na) {
    wchar_t f[4][200] = {L"", L"", L"", L""};
    for (int k = 0, i = 0; k < 4; k++) {
      int j = 0;
      while (fit[i] && fit[i] != L'|' && j < 199) f[k][j++] = fit[i++];
      f[k][j] = 0;
      if (fit[i] == L'|') i++;
    }
    g_szmDicts.area = (wchar_t(*)[200])calloc(1, sizeof(*g_szmDicts.area));
    g_szmDicts.equip = (wchar_t(*)[200])calloc(1, sizeof(*g_szmDicts.equip));
    g_szmDicts.areaCode = (wchar_t(*)[40])calloc(1, sizeof(*g_szmDicts.areaCode));
    g_szmDicts.areaId = (long *)calloc(1, sizeof(long));
    g_szmDicts.equipId = (long *)calloc(1, sizeof(long));
    g_szmDicts.fit = (SzmFit *)calloc(1, sizeof(SzmFit));
    if (g_szmDicts.area && g_szmDicts.equip && g_szmDicts.areaCode && g_szmDicts.areaId && g_szmDicts.equipId &&
        g_szmDicts.fit) {
      _snwprintf(g_szmDicts.area[0], 200, L"%ls %ls", f[0], f[1]);
      lstrcpynW(g_szmDicts.areaCode[0], f[0], 40);
      lstrcpynW(g_szmDicts.equip[0], f[2], 200);
      g_szmDicts.areaId[0] = 101, g_szmDicts.equipId[0] = 201;
      g_szmDicts.fit[0].area = 101, g_szmDicts.fit[0].equip = 201, g_szmDicts.fit[0].score = 1000;
      g_szmDicts.fit[0].code = wcstol(f[3], NULL, 10);
      g_szmDicts.na = g_szmDicts.ne = g_szmDicts.nf = 1;
      g_szmDictsAsked = TRUE;
      pk_changed();
      pk_fill(FALSE);
    }
  }
  const wchar_t *e = _wgetenv(L"CP_PKDROP"); /* широкая: getenv в Wine отдаёт ANSI и портит кириллицу */
  if (!e || !g_pkWnd) return;
  wchar_t w[4096];
  lstrcpynW(w, e, 4096);
  int row = _wtoi(w);
  size_t chars = 0;
  for (wchar_t *p = wcschr(w, L'|'); p; p = wcschr(p + 1, L'|')) {
    wchar_t *q = wcschr(p + 1, L'|');
    chars += (size_t)(q ? q - p - 1 : (ptrdiff_t)wcslen(p + 1)) + 1;
  }
  HGLOBAL g = GlobalAlloc(GHND, sizeof(DROPFILES) + (chars + 1) * sizeof(wchar_t));
  DROPFILES *df = g ? (DROPFILES *)GlobalLock(g) : NULL;
  if (!df) return;
  df->pFiles = sizeof(DROPFILES);
  df->fWide = TRUE;
  RECT ir = {0};
  if (row >= 0) ListView_GetItemRect(g_pkList, row, &ir, LVIR_BOUNDS);
  else GetClientRect(g_pkList, &ir), ir.top = ir.bottom - 20, ir.bottom -= 10;
  POINT pt = {(ir.left + ir.right) / 2, (ir.top + ir.bottom) / 2};
  MapWindowPoints(g_pkList, g_pkWnd, &pt, 1);
  df->pt = pt;
  wchar_t *o = (wchar_t *)((BYTE *)df + sizeof(DROPFILES));
  for (wchar_t *p = wcschr(w, L'|'); p; p = wcschr(p + 1, L'|')) {
    wchar_t *q = wcschr(p + 1, L'|');
    size_t l = q ? (size_t)(q - p - 1) : wcslen(p + 1);
    memcpy(o, p + 1, l * sizeof(wchar_t));
    o += l;
    *o++ = 0;
  }
  *o = 0;
  GlobalUnlock(g);
  PostMessageW(g_pkWnd, WM_DROPFILES, (WPARAM)g, 0);
}
#endif
