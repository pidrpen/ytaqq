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
  wchar_t note[200];
  wchar_t parent[200];      /* сборка, на которую бросили; L"" — нет */
  int parentRow;            /* её строка в «Изделиях» (обозначения у неё может ещё не быть); -1 — нет */
  int row;                  /* строка детали, на которую бросили один чертёж; -1 — нет */
  wchar_t rowDes[200];      /* её обозначение в момент броска: строка всё та же? */
  PkStRow *rows;            /* спецификация */
  int nrows;
} PkStRes;

typedef struct PkStJob {
  struct PkStJob *next;
  LONG gen;
  wchar_t (*files)[MAX_PATH];
  int n;
  wchar_t parent[200];
  int parentRow, row;
  wchar_t rowDes[200];
} PkStJob;

static CRITICAL_SECTION g_pksLock;
static BOOL g_pksLockInit, g_pksRunning;
static PkStJob *g_pksQ, *g_pksQTail;
/* итог бросков для строки состояния: сколько прочитано, чем, куда легло */
static int g_pksDone, g_pksTotal, g_pksNew, g_pksMerged, g_pksLinks, g_pksNoDes, g_pksBy[4], g_pksWait;
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
    if (dr_is_docs(s->c[DS_SECT])) continue;
    const wchar_t *des = s->c[DS_DES][0] ? s->c[DS_DES] : s->c[DS_NAME];
    if (!des[0]) continue;
    if (!r->rows) r->rows = (PkStRow *)calloc(PKS_MAXROWS, sizeof(PkStRow));
    if (!r->rows) return;
    PkStRow *o = &r->rows[r->nrows++];
    lstrcpynW(o->des, des, DR_CELL);
    lstrcpynW(o->name, s->c[DS_DES][0] ? s->c[DS_NAME] : L"", DR_CELL);
    lstrcpynW(o->kind, dr_kind_of(s->c[DS_SECT]), 40);
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
  int doc = pp_draw_doc(raw, name); /* «… СБ», «Сборочный чертеж» — сборка; код — прочь */
  lstrcpynW(r->des, v, DR_CELL);   /* dr_find_des уже без кода и с кириллицей вместо похожей латиницы */
  lstrcpynW(r->name, name, DR_CELL);
  r->asmDoc = doc == 1 || pg->type == DRT_SPEC;
  lstrcpynW(r->kind, r->asmDoc ? L"Сборочные единицы" : pg->type == DRT_DRAW && !doc ? L"Детали" : L"", 40);
  if (pg->type == DRT_DRAW && !doc) { /* материал и масса — только уверенные: ошибочный материал «Союз» не найдёт */
    if (pg->f[DF_MAT][0] && !pg->doubt[DF_MAT]) lstrcpynW(r->mat, pg->f[DF_MAT], DR_CELL);
    if (pg->f[DF_MASS][0] && !pg->doubt[DF_MASS]) lstrcpynW(r->mass, pg->f[DF_MASS], 40);
  }
  if (pg->type == DRT_SPEC) pks_spec_rows(pg, r);
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
    dr_recognize(f, &pg);
    if (!i) {
      pks_from_page(&pg, r);
      if (!r->how && pg.note[0]) lstrcpynW(why, pg.note, 200);
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
    if (pp_file_title(r->file, r->des, r->name, &doc)) {
      r->how = 3;
      r->asmDoc = doc == 1;
      if (!r->kind[0]) lstrcpynW(r->kind, r->asmDoc ? L"Сборочные единицы" : !doc ? L"Детали" : L"", 40);
    }
  }
  if (!r->des[0]) lstrcpynW(r->note, why[0] ? why : L"обозначение не прочиталось", 200);
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
      lstrcpynW(r->file, j->files[i], MAX_PATH);
      lstrcpynW(r->parent, j->parent, 200);
      lstrcpynW(r->rowDes, j->rowDes, 200);
      pks_read(f, r);
      if (!g_pkWnd || !PostMessageW(g_pkWnd, WM_PK_STAMP, 0, (LPARAM)r)) {
        free(r->rows);
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

static void pks_apply(PkStRes *r) {
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
    if (!xp_row(it)) return;
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
  if (target || !xp_cell(it, row, 5)[0]) pk_put(it, row, 5, r->file); /* на свою строку — файл заменить */
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
  if (g_pksLinks > was)
    pk_status(L"В состав «%s»: %d — см. лист «Состав»", parent[0] ? parent : des, g_pksLinks - was);
}

/* «Очистить»: ждать больше нечего */
static void pk_stamp_forget(void) {
  g_pksPendN = g_pksOrphN = 0;
  g_pksWait = 0;
}

/* итог — в строку состояния */
static void pks_status(const wchar_t *note) {
  wchar_t by[120] = L"", wait[120] = L"";
  if (g_pksBy[2] || g_pksBy[1] || g_pksBy[3])
    _snwprintf(by, 120, L"; обозначения: моделью %d, Tesseract %d, из имени файла %d", g_pksBy[2], g_pksBy[1], g_pksBy[3]);
  if (g_pksWait) _snwprintf(wait, 120, L"; %d ждут обозначения сборки — впишите его", g_pksWait);
  pk_status(L"Чертежи: %d из %d — новых изделий %d, дополнено %d, в состав%ls%ls%ls %d%ls%ls%ls%ls%ls%ls%ls",
            g_pksDone, g_pksTotal, g_pksNew, g_pksMerged, g_pksParent[0] ? L" «" : L"", g_pksParent,
            g_pksParent[0] ? L"»" : L"", g_pksLinks, by, g_pksNoDes ? L"; без обозначения — впишите" : L"",
            g_pksDoubt[0] ? L"; проверьте: " : L"", g_pksDoubt, wait, note && note[0] ? L"; " : L"",
            note ? note : L"");
}

/* WM_PK_STAMP: прочитанный чертёж — в листы */
static void pk_stamp_done(void *p) {
  PkStRes *r = (PkStRes *)p;
  if (r->gen == g_pkStGen) {
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    pks_apply(r);
    g_pksDone++;
    pk_fill(FALSE);
    pk_changed();
    BOOL last = r->i == r->n - 1;
    pks_status(r->note[0] ? r->note : last && !dr_vl_ok() && g_pksBy[1]
                                          ? L"мини-ИИ с картинками выключен — Tesseract путает буквы, сверьте обозначения"
                                          : L"");
  }
  free(r->rows);
  free(r);
}

/* WM_PK_STAMPGO: читается следующий файл (счёт — по всем броскам подряд) */
static void pk_stamp_progress(void) {
  pk_status(L"Читаю штамп: %d из %d%ls… (бросать можно ещё; «Очистить» — прервать)", g_pksDone + 1, g_pksTotal,
            dr_vl_ok() ? L", мини-ИИ" : L"");
}

/* файл или папка (её картинки) — в список чтения */
static void pks_add_path(TmPaths *l, const wchar_t *p) {
  DWORD a = GetFileAttributesW(p);
  if (a == INVALID_FILE_ATTRIBUTES) return;
  if (a & FILE_ATTRIBUTE_DIRECTORY) tm_paths_add_any(l, p); /* папка — её картинки */
  else tm_paths_add(l, p);                                   /* файл — любой: PDF, КОМПАС, DWG — по имени */
}

/* файлы — в очередь чтения; hit — строка открытого листа, куда их отдали (-1 — никуда) */
static void pks_enqueue(TmPaths *pl, int hit) {
  TmPaths l = *pl;
  if (!l.n) {
    pk_status(L"Чертежей не нашлось — бросьте файлы (PDF, TIF, PNG…) или папку с ними");
    free(l.p);
    return;
  }
  tm_paths_sort(&l);
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  pk_init();
  PkStJob *j = (PkStJob *)calloc(1, sizeof(PkStJob));
  if (!j) {
    free(l.p);
    return;
  }
  j->files = l.p;
  j->n = l.n;
  j->parentRow = j->row = -1;
  wchar_t note[200] = L"";
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
  if (g_pkSheet != PK_ITEMS && g_pkSheet != PK_LINKS) {
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
    g_pksDone = g_pksTotal = g_pksNew = g_pksMerged = g_pksLinks = g_pksNoDes = g_pksWait = 0;
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
  pks_enqueue(&l, hi.flags & LVHT_ONITEM ? hit : -1);
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
  pks_enqueue(&l, sel >= 0 && sel < g_pk[g_pkSheet].n && ListView_GetSelectedCount(g_pkList) == 1 ? sel : -1);
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
