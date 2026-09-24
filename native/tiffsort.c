/* ---- Окно «Сортировка TIFF A4 / A3» -------------------------------------------

   Своё окно CursorPad вместо страницы tiff-a4-a3.html, в её тёмно-коричневом
   виде: три колонки — A4, A3 и «Другое», в каждой сохранение в PDF и TIFF.
   Читает и пишет то же, что «Объединение TIFF / PDF» (tiffmerge.c: WIC и
   tiff_codec.c), работает в том же потоке.

   Как сортируется. У A4 и A3 одинаковые пропорции (A3 — два A4), поэтому по
   форме картинки их не отличить — только по размеру листа в миллиметрах:
   точки ÷ dpi × 25,4. Лист в любом положении — тот же формат (A4 лёжа —
   тоже A4); ориентация показывается отдельно, листы не поворачиваются.
   Допуск — от размера стороны (как на странице, по умолчанию 8 %).

   Что исправлено против страницы:
   · dpi берётся из любого файла, не только из TIFF: страница у JPG/PNG
     всегда брала 300, и A3, снятый в 200 dpi, попадал в A4;
   · нет dpi в файле (или стоит «экранные» 72/96 — так пишут, когда не
     знают) — лист не угадывается молча, а лежит в «Другом» с пометкой
     «нет dpi»; можно указать, в каком dpi сканировали, — тогда посчитается;
   · у «Другого» подписан формат: A2, A4×3, A3×4… (ГОСТ 2.301);
   · лист, который не прочитался, не пропадает молча из файла;
   · ч/б остаётся ч/б без потерь (G4), лист PDF — размера бумаги;
   · в памяти только пути: хоть сотня листов A0.
   Любой лист можно перенести в другую колонку правой кнопкой. */

enum { TS_A4, TS_A3, TS_OTHER };

typedef struct {
  TmPage pg;          /* без миниатюры */
  int grp;            /* TS_A4 / TS_A3 / TS_OTHER */
  int manual;         /* перенесли руками — в эту колонку; -1 — как посчитано */
  BOOL nodpi;         /* dpi не известен и не указан — формат не определить */
  BOOL assumed;       /* посчитано по указанному dpi, а не по файлу */
  double wmm, hmm;    /* размер листа, мм (как лежит) */
  const wchar_t *fmt; /* A4, A3, A2, A4×3… или NULL */
} TsItem;

static TsItem *g_tsItems;
static int g_tsN, g_tsCap, g_tsSel = -1;
static HWND g_tsWnd, g_tsCanvas;
static TmUi g_uiSort = {1, 1, 1, TRUE, {0}, {{{0}}}, 0, FALSE, 0, -1, {0}};
static volatile LONG g_tsCancel;
static int g_tsLoadPending;
static BOOL g_tsExporting;
static const int kTsTols[4] = {3, 5, 8, 12};
static const int kTsDpis[5] = {0, 200, 300, 400, 600}; /* 0 — не угадывать */
static int g_tsTol = 8, g_tsAssume = 0;
static int g_tsScroll, g_tsContentH;
static int g_tsLScroll[3], g_tsLH[3];
static RECT g_tsLRc[3];
static wchar_t g_tsStatus[MAX_PATH + 80], g_tsStatusPath[MAX_PATH];

/* цвета страницы */
#define TSC_BG RGB(0x1A, 0x12, 0x0B)
#define TSC_CARD RGB(0x2C, 0x21, 0x18)
#define TSC_BORDER RGB(0x5C, 0x46, 0x30)
#define TSC_LIST RGB(0x1F, 0x18, 0x12)
#define TSC_TEXT RGB(0xE8, 0xD5, 0xB7)
#define TSC_SUB RGB(0xD4, 0xC3, 0xA3)
#define TSC_MUTED RGB(0x8B, 0x5E, 0x3C)
#define TSC_FAINT RGB(0x6B, 0x53, 0x44)
#define TSC_GOLD RGB(0xC9, 0xA2, 0x27)
#define TSC_GOLD2 RGB(0xD4, 0xAF, 0x37)
#define TSC_ROWH RGB(0x34, 0x28, 0x18)
#define TSC_DARK RGB(0x1A, 0x12, 0x0B)

/* колонки: A4 — изумрудная, A3 — янтарная, «Другое» — серая */
static const COLORREF kTsTitle[3] = {RGB(0x34, 0xD3, 0x99), RGB(0xFB, 0xBF, 0x24), RGB(0x94, 0xA3, 0xB8)};
static const COLORREF kTsBadgeBg[3] = {RGB(0x0D, 0x45, 0x33), RGB(0x6A, 0x31, 0x0F), RGB(0x1E, 0x29, 0x3B)};
static const COLORREF kTsBadgeFg[3] = {RGB(0x6E, 0xE7, 0xB7), RGB(0xFC, 0xD3, 0x4D), RGB(0xCB, 0xD5, 0xE1)};
static const COLORREF kTsBtn[3] = {RGB(0x04, 0x78, 0x57), RGB(0xB4, 0x53, 0x09), RGB(0x47, 0x55, 0x69)};
static const COLORREF kTsBtnHov[3] = {RGB(0x05, 0x96, 0x69), RGB(0xD9, 0x77, 0x06), RGB(0x64, 0x74, 0x8B)};
static const wchar_t *const kTsName[3] = {L"A4", L"A3", L"Другое"};
static const wchar_t *const kTsSub[3] = {L"210 × 297 мм", L"297 × 420 мм", L"другие форматы · без dpi"};

static const TmPal kTsDark = {TSC_CARD,  TSC_TEXT,   TSC_MUTED, TSC_SUB,  TSC_LIST,           TSC_GOLD,          TSC_LIST,
                              TSC_GOLD,  TSC_CARD,   TSC_BORDER, TSC_TEXT, RGB(0x45, 0x1A, 0x14), RGB(0xF8, 0x71, 0x71)};

/* ---- формат листа ------------------------------------------------------------ */

/* форматы ГОСТ 2.301: основные и дополнительные (кратные), короткая × длинная */
static const struct {
  int s, l;
  const wchar_t *n;
} kTsPapers[] = {{210, 297, L"A4"},    {297, 420, L"A3"},    {420, 594, L"A2"},    {594, 841, L"A1"},
                 {841, 1189, L"A0"},   {148, 210, L"A5"},    {297, 630, L"A4×3"}, {297, 841, L"A4×4"},
                 {297, 1051, L"A4×5"}, {297, 1261, L"A4×6"}, {297, 1471, L"A4×7"}, {297, 1682, L"A4×8"},
                 {297, 1892, L"A4×9"}, {420, 891, L"A3×3"},  {420, 1189, L"A3×4"}, {420, 1486, L"A3×5"},
                 {420, 1783, L"A3×6"}, {420, 2080, L"A3×7"}, {594, 1261, L"A2×3"}, {594, 1682, L"A2×4"},
                 {594, 2102, L"A2×5"}, {841, 1783, L"A1×3"}, {841, 2378, L"A1×4"}, {1189, 1682, L"A0×2"},
                 {1189, 2523, L"A0×3"}};

static const wchar_t *ts_paper(double s, double l, int tolPct) {
  double t = tolPct / 100.0;
  for (size_t i = 0; i < sizeof(kTsPapers) / sizeof(kTsPapers[0]); i++)
    if (fabs(s - kTsPapers[i].s) <= kTsPapers[i].s * t && fabs(l - kTsPapers[i].l) <= kTsPapers[i].l * t)
      return kTsPapers[i].n;
  return NULL;
}

/* 72 и 96 dpi — «экранные»: их пишут, когда настоящий dpi не знают (и WIC
   отдаёт 96, если в файле его нет вовсе). Сканов в 72/96 dpi не бывает. */
static BOOL ts_dpi_known(double d) {
  int r = (int)(d + 0.5);
  return r != 72 && r != 96;
}

static void ts_classify(TsItem *it) {
  const TmPage *p = &it->pg;
  double dx = p->dx, dy = p->dy;
  it->nodpi = it->assumed = FALSE;
  it->fmt = NULL;
  it->wmm = it->hmm = 0;
  int grp = TS_OTHER;
  if (!ts_dpi_known(dx) || !ts_dpi_known(dy)) {
    if (g_tsAssume) {
      dx = dy = g_tsAssume;
      it->assumed = TRUE;
    } else {
      it->nodpi = TRUE;
    }
  }
  if (!it->nodpi) {
    it->wmm = p->w * 25.4 / dx;
    it->hmm = p->h * 25.4 / dy;
    double s = it->wmm < it->hmm ? it->wmm : it->hmm, l = it->wmm < it->hmm ? it->hmm : it->wmm;
    it->fmt = ts_paper(s, l, g_tsTol);
    if (it->fmt && !wcscmp(it->fmt, L"A4")) grp = TS_A4;
    else if (it->fmt && !wcscmp(it->fmt, L"A3")) grp = TS_A3;
  }
  it->grp = it->manual >= 0 ? it->manual : grp;
}

static void ts_classify_all(void) {
  for (int i = 0; i < g_tsN; i++) ts_classify(&g_tsItems[i]);
}

/* строка сведений под именем */
static void ts_info(const TsItem *it, wchar_t *out, int cap) {
  const TmPage *p = &it->pg;
  const wchar_t *ori = p->h > p->w ? L"стоя" : p->w > p->h ? L"лёжа" : L"квадрат";
  if (it->nodpi) {
    _snwprintf(out, cap, L"нет dpi в файле · %d×%d точек · %s", p->w, p->h, ori);
  } else {
    wchar_t dpi[40];
    if (it->assumed) _snwprintf(dpi, 40, L"%d dpi (указан)", g_tsAssume);
    else if ((int)(p->dx + 0.5) == (int)(p->dy + 0.5)) _snwprintf(dpi, 40, L"%d dpi", (int)(p->dx + 0.5));
    else _snwprintf(dpi, 40, L"%d×%d dpi", (int)(p->dx + 0.5), (int)(p->dy + 0.5));
    dpi[39] = 0;
    _snwprintf(out, cap, L"%s%d×%d мм · %s · %s", it->fmt ? L"" : L"нестандартный · ", (int)(it->wmm + 0.5),
               (int)(it->hmm + 0.5), dpi, ori);
  }
  out[cap - 1] = 0;
}

/* ---- действия ------------------------------------------------------------------ */

static void ts_relayout(void);

static void ts_load_paths(TmPaths *l) {
  if (!l->n || g_tsExporting) {
    free(l->p);
    return;
  }
  tm_paths_sort(l);
  TmJob *j = tm_job_paths(TJ_LOAD, l);
  if (!j) return;
  j->nothumb = TRUE; /* миниатюры сортировке не нужны — открывается в разы быстрее */
  j->wnd = g_tsWnd;
  j->cancel = &g_tsCancel;
  g_tsLoadPending++;
  InterlockedExchange(&g_tsCancel, 0);
  tm_submit(j);
  ts_relayout();
}

static void ts_add_files(void) {
  TmPaths l = {0};
  if (tm_pick(g_tsWnd, &l, L"Файлы для сортировки")) ts_load_paths(&l);
  else free(l.p);
}

static void ts_paste(void) {
  TmPaths l = {0};
  tm_clip_paths(g_tsWnd, &l);
  if (l.n) ts_load_paths(&l);
  else free(l.p);
}

static void ts_reset(void) {
  if (!g_tsN) return;
  if (MessageBoxW(g_tsWnd, L"Убрать все листы из списка? Сами файлы не трогаются.", L"Сортировка TIFF A4 / A3",
                  MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;
  g_tsN = 0;
  g_tsSel = -1;
  g_tsStatus[0] = 0;
  memset(g_tsLScroll, 0, sizeof(g_tsLScroll));
  ts_relayout();
}

static void ts_remove(int i) {
  if (i < 0 || i >= g_tsN) return;
  memmove(&g_tsItems[i], &g_tsItems[i + 1], sizeof(TsItem) * (size_t)(g_tsN - i - 1));
  g_tsN--;
  if (g_tsSel == i) g_tsSel = -1;
  else if (g_tsSel > i) g_tsSel--;
  ts_relayout();
}

static int ts_count(int g) {
  int n = 0;
  for (int i = 0; i < g_tsN; i++)
    if (g_tsItems[i].grp == g) n++;
  return n;
}

/* колонку — в один PDF или многостраничный TIFF */
static void ts_export(int g, BOOL pdf) {
  int n = ts_count(g);
  if (!n || g_tsExporting || g_tsLoadPending) return;
  TmPage *snap = (TmPage *)malloc(sizeof(TmPage) * (size_t)n);
  TmJob *j = (TmJob *)calloc(1, sizeof(TmJob));
  if (!snap || !j) {
    free(snap);
    free(j);
    return;
  }
  int k = 0;
  for (int i = 0; i < g_tsN; i++) {
    const TsItem *it = &g_tsItems[i];
    if (it->grp != g) continue;
    snap[k] = it->pg;
    if (it->assumed) snap[k].dx = snap[k].dy = g_tsAssume; /* лист PDF — по указанному dpi */
    k++;
  }
  wchar_t file[MAX_PATH];
  _snwprintf(file, MAX_PATH, L"%s_%d_стр", g == TS_OTHER ? L"Другое" : kTsName[g], n);
  file[MAX_PATH - 1] = 0;
  if (!tm_save_as(g_tsWnd, file, pdf, snap[0].path)) {
    free(snap);
    free(j);
    return;
  }
  j->type = pdf ? TJ_PDF : TJ_TIFF;
  j->pages = snap;
  j->npages = n;
  j->wnd = g_tsWnd;
  j->cancel = &g_tsCancel;
  lstrcpynW(j->out, file, MAX_PATH);
  g_tsExporting = TRUE;
  InterlockedExchange(&g_tsCancel, 0);
  EnterCriticalSection(&g_tmLock);
  _snwprintf(g_tmProgTitle, 80, L"%s — %s", kTsName[g], pdf ? L"PDF" : L"TIFF");
  g_tmProgText[0] = 0;
  g_tmProgPct = 0;
  LeaveCriticalSection(&g_tmLock);
  tm_submit(j);
  ts_relayout();
}

/* ---- рисование ----------------------------------------------------------------- */

enum { TH_NONE, TH_ADD, TH_RESET, TH_TOL, TH_DPI, TH_ROW, TH_PDF, TH_TIFF, TH_OPEN_OUT, TH_SHOW_OUT };

/* кнопка-«пилюля» выбора (допуск, dpi) */
static int ts_pill(HDC dc, int x, int y, const wchar_t *t, BOOL on, int id, int arg, BOOL paint) {
  int w = tm_text_w(dc, TF_SMALLM, t) + TS(28), h = TS(34);
  RECT r = {x, y, x + w, y + h};
  if (paint) {
    BOOL hov = tm_is_hover(id, arg);
    if (on) cc_round(dc, r, TS(12), TSC_GOLD, TSC_GOLD);
    else cc_round(dc, r, TS(12), hov ? TSC_ROWH : TSC_CARD, hov ? TSC_GOLD : TSC_BORDER);
    tm_text(dc, TF_SMALLM, on ? TSC_DARK : TSC_TEXT, t, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  tm_hot(r, id, arg);
  return w;
}

/* ряд пилюль с подписью над ним; возвращает ширину ряда */
static int ts_pill_group(HDC dc, int x, int y, const wchar_t *label, int n, const int *vals, int cur, int id,
                         BOOL dpi, BOOL paint) {
  if (paint) tm_text(dc, TF_TINY, TSC_MUTED, label, (RECT){x, y, x + TS(400), y + TS(16)}, DT_LEFT | DT_SINGLELINE);
  int px = x;
  for (int i = 0; i < n; i++) {
    wchar_t t[24];
    if (dpi && !vals[i]) lstrcpyW(t, L"не угадывать");
    else _snwprintf(t, 24, dpi ? L"%d" : L"%d %%", vals[i]);
    px += ts_pill(dc, px, y + TS(22), t, vals[i] == cur, id, i, paint) + TS(8);
  }
  return px - TS(8) - x;
}

static int ts_pill_group_w(HDC dc, int n, const int *vals, BOOL dpi) {
  int w = 0;
  for (int i = 0; i < n; i++) {
    wchar_t t[24];
    if (dpi && !vals[i]) lstrcpyW(t, L"не угадывать");
    else _snwprintf(t, 24, dpi ? L"%d" : L"%d %%", vals[i]);
    w += tm_text_w(dc, TF_SMALLM, t) + TS(28) + (i ? TS(8) : 0);
  }
  return w;
}

/* значок-лист: прямоугольник с загнутым углом */
static void ts_doc_glyph(HDC dc, RECT r, COLORREF c) {
  int w = r.right - r.left, h = r.bottom - r.top, pw = TS(2) > 1 ? TS(2) : 1;
  int x0 = r.left + w / 4, x1 = r.right - w / 4, y0 = r.top + h / 6, y1 = r.bottom - h / 6, k = w / 6;
  tm_line(dc, x0, y0, x1 - k, y0, c, pw);
  tm_line(dc, x1 - k, y0, x1, y0 + k, c, pw);
  tm_line(dc, x1, y0 + k, x1, y1, c, pw);
  tm_line(dc, x1, y1, x0, y1, c, pw);
  tm_line(dc, x0, y1, x0, y0, c, pw);
  for (int i = 1; i <= 3; i++) tm_line(dc, x0 + k / 2 + 2, y0 + k + i * (y1 - y0 - k) / 5, x1 - k / 2 - 2, y0 + k + i * (y1 - y0 - k) / 5, c, 1);
}

/* колонка A4 / A3 / «Другое»: шапка, список со своей прокруткой, кнопки */
static void ts_column(HDC dc, int g, int x, int y, int w, int h, BOOL paint) {
  int pad = TS(20), ix = x + pad, iw = w - pad * 2;
  int cnt = ts_count(g);
  if (paint) {
    cc_round(dc, (RECT){x, y, x + w, y + h}, TS(24), TSC_CARD, TSC_BORDER);
    tm_text(dc, TF_SEC, kTsTitle[g], kTsName[g], (RECT){ix, y + pad - TS(2), ix + iw - TS(70), y + pad + TS(20)},
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    tm_text(dc, TF_TINY, TSC_MUTED, kTsSub[g], (RECT){ix, y + pad + TS(20), ix + iw - TS(70), y + pad + TS(36)},
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    wchar_t c[24];
    _snwprintf(c, 24, L"%d стр.", cnt);
    int bw = tm_text_w(dc, TF_BADGE, c) + TS(20);
    RECT b = {ix + iw - bw, y + pad, ix + iw, y + pad + TS(24)};
    cc_round(dc, b, TS(12), kTsBadgeBg[g], kTsBadgeBg[g]);
    tm_text(dc, TF_BADGE, kTsBadgeFg[g], c, b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  int bh = TS(40);
  RECT lr = {ix, y + pad + TS(48), ix + iw, y + h - pad - bh - TS(12)};
  g_tsLRc[g] = lr;
  int rowH = TS(54), gap = TS(4), lp = TS(8);
  g_tsLH[g] = cnt ? lp * 2 + cnt * (rowH + gap) - gap : 0;
  int vpH = lr.bottom - lr.top, maxs = g_tsLH[g] - vpH;
  if (maxs < 0) maxs = 0;
  if (g_tsLScroll[g] > maxs) g_tsLScroll[g] = maxs;
  if (g_tsLScroll[g] < 0) g_tsLScroll[g] = 0;
  int saved = 0;
  if (paint) {
    cc_round(dc, lr, TS(16), TSC_LIST, TSC_LIST);
    saved = SaveDC(dc);
    IntersectClipRect(dc, lr.left, lr.top, lr.right, lr.bottom);
  }
  int k = 0, rw = iw - lp * 2 - (maxs ? TS(6) : 0);
  for (int i = 0; i < g_tsN; i++) {
    const TsItem *it = &g_tsItems[i];
    if (it->grp != g) continue;
    int ry = lr.top + lp + k * (rowH + gap) - g_tsLScroll[g];
    k++;
    if (ry + rowH < lr.top || ry > lr.bottom) continue;
    RECT r = {lr.left + lp, ry, lr.left + lp + rw, ry + rowH};
    if (!paint) {
      RECT v;
      if (IntersectRect(&v, &r, &lr)) tm_hot(v, TH_ROW, i);
      continue;
    }
    BOOL sel = i == g_tsSel, hov = tm_is_hover(TH_ROW, i);
    cc_round(dc, r, TS(12), hov || sel ? TSC_ROWH : TSC_CARD, sel ? TSC_GOLD : hov ? TSC_BORDER : TSC_CARD);
    /* справа — формат */
    const wchar_t *f = it->nodpi ? L"?" : it->fmt ? it->fmt : L"—";
    int fw = tm_text_w(dc, TF_BADGE, f) + TS(14);
    RECT fb = {r.right - TS(10) - fw, r.top + TS(10), r.right - TS(10), r.top + TS(28)};
    cc_round(dc, fb, TS(8), TSC_LIST, TSC_LIST);
    tm_text(dc, TF_BADGE, it->nodpi ? RGB(0xF8, 0x71, 0x71) : TSC_GOLD, f, fb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    int tx = r.left + TS(12), tr = fb.left - TS(8);
    tm_text(dc, TF_SMALLM, TSC_TEXT, it->pg.name, (RECT){tx, r.top + TS(9), tr, r.top + TS(27)},
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    wchar_t info[160];
    ts_info(it, info, 160);
    if (it->manual >= 0) wcsncat(info, L" · вручную", 159 - wcslen(info));
    tm_text(dc, TF_TINY, it->nodpi ? RGB(0xE0, 0x8A, 0x6A) : TSC_MUTED, info, (RECT){tx, r.top + TS(29), r.right - TS(10), r.top + TS(46)},
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  if (paint) {
    if (!cnt) {
      const wchar_t *t = g_tsLoadPending ? L"Открываю…" : L"Пусто";
      tm_text(dc, TF_SMALL, TSC_MUTED, t, lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    RestoreDC(dc, saved);
    if (maxs) { /* сколько пролистано */
      int track = vpH - TS(8), th = track * vpH / g_tsLH[g];
      if (th < TS(24)) th = TS(24);
      int ty = lr.top + TS(4) + (track - th) * g_tsLScroll[g] / maxs;
      cc_round(dc, (RECT){lr.right - TS(8), ty, lr.right - TS(4), ty + th}, TS(2), TSC_BORDER, TSC_BORDER);
    }
  }
  /* кнопки: «Сохранить PDF» (цветом колонки) и «TIFF» */
  BOOL en = cnt > 0 && !g_tsExporting && !g_tsLoadPending;
  int tw = TS(84);
  RECT pb = {ix, y + h - pad - bh, ix + iw - tw - TS(8), y + h - pad};
  RECT tb = {ix + iw - tw, pb.top, ix + iw, pb.bottom};
  if (en) {
    tm_hot(pb, TH_PDF, g);
    tm_hot(tb, TH_TIFF, g);
  }
  if (paint) {
    COLORREF f1 = en ? (tm_is_hover(TH_PDF, g) ? kTsBtnHov[g] : kTsBtn[g]) : tm_mix(kTsBtn[g], TSC_CARD, 0.6f);
    cc_round(dc, pb, TS(12), f1, f1);
    tm_text(dc, TF_BTN, en ? RGB(0xFF, 0xFF, 0xFF) : tm_mix(RGB(0xFF, 0xFF, 0xFF), f1, 0.5f), L"Сохранить PDF", pb,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    BOOL th = en && tm_is_hover(TH_TIFF, g);
    cc_round(dc, tb, TS(12), th ? TSC_ROWH : TSC_CARD, en ? (th ? TSC_GOLD : TSC_BORDER) : tm_mix(TSC_BORDER, TSC_CARD, 0.5f));
    tm_text(dc, TF_BTN, en ? TSC_TEXT : TSC_FAINT, L"TIFF", tb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

static int ts_draw(HDC dc, int cw, int ch, BOOL paint) {
  g_ui->painting = paint;
  if (!paint) g_ui->nhot = 0;
  int m = TS(24), gap = TS(20), avail = cw - m * 2;
  int y = m;
  if (paint) {
    tm_text(dc, TF_H1, TSC_GOLD, L"TIFF Сортировщик A4 / A3", (RECT){m, y - TS(4), cw - m, y + TS(30)},
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    tm_text(dc, TF_SUB, TSC_MUTED, L"Формат — по размеру листа в мм · ориентация · сохранение в PDF и TIFF",
            (RECT){m, y + TS(28), cw - m, y + TS(46)}, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  y += TS(46) + TS(22);
  /* настройки: допуск и dpi, если его нет в файле */
  {
    int pad = TS(20), ix = m + pad, iw = avail - pad * 2;
    int w1 = ts_pill_group_w(dc, 4, kTsTols, FALSE), w2 = ts_pill_group_w(dc, 5, kTsDpis, TRUE);
    BOOL row = w1 + TS(40) + w2 <= iw;
    const wchar_t *hint = L"A4 и A3 одной формы (A3 — два A4), поэтому формат считается по размеру: точки ÷ dpi. "
                          L"Лист в любом положении — тот же формат; листы не поворачиваются. Нет dpi в файле — лист "
                          L"попадёт в «Другое» с пометкой, пока не укажете, в каком dpi сканировали.";
    int hh = tm_text_h(dc, TF_TINY, hint, iw);
    int ch2 = pad + (row ? TS(56) : TS(56) * 2 + TS(10)) + TS(12) + hh + pad;
    if (paint) cc_round(dc, (RECT){m, y, m + avail, y + ch2}, TS(24), TSC_CARD, TSC_BORDER);
    int gy = y + pad;
    ts_pill_group(dc, ix, gy, L"ДОПУСК ПО РАЗМЕРУ", 4, kTsTols, g_tsTol, TH_TOL, FALSE, paint);
    if (row) ts_pill_group(dc, ix + w1 + TS(40), gy, L"ЕСЛИ В ФАЙЛЕ НЕТ DPI, СКАНИРОВАЛИ В", 5, kTsDpis, g_tsAssume, TH_DPI, TRUE, paint);
    else ts_pill_group(dc, ix, gy + TS(66), L"ЕСЛИ В ФАЙЛЕ НЕТ DPI, СКАНИРОВАЛИ В", 5, kTsDpis, g_tsAssume, TH_DPI, TRUE, paint);
    int hy = gy + (row ? TS(56) : TS(56) * 2 + TS(10)) + TS(12);
    if (paint) tm_text(dc, TF_TINY, TSC_MUTED, hint, (RECT){ix, hy, ix + iw, hy + hh}, DT_LEFT | DT_WORDBREAK);
    y += ch2 + TS(16);
  }
  /* место для файлов */
  {
    int dzH = TS(84);
    RECT dz = {m, y, m + avail, y + dzH};
    BOOL hov = tm_is_hover(TH_ADD, -1);
    int bw1 = tm_text_w(dc, TF_BTN, L"Выбрать файлы") + TS(40), bw2 = tm_text_w(dc, TF_SMALLM, L"Сбросить") + TS(32);
    RECT b1 = {dz.right - TS(18) - bw2 - TS(10) - bw1, y + (dzH - TS(44)) / 2, dz.right - TS(18) - bw2 - TS(10),
               y + (dzH + TS(44)) / 2};
    RECT b2 = {dz.right - TS(18) - bw2, b1.top, dz.right - TS(18), b1.bottom};
    tm_hot(dz, TH_ADD, -1);
    if (g_tsN) tm_hot(b2, TH_RESET, -1);
    if (paint) {
      LOGBRUSH lb = {BS_SOLID, hov ? TSC_GOLD : TSC_BORDER, 0};
      DWORD dash[2] = {(DWORD)TS(6), (DWORD)TS(4)};
      HPEN p = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT, TS(2) > 1 ? TS(2) : 1, &lb, 2, dash);
      HBRUSH br = CreateSolidBrush(hov ? RGB(0x27, 0x20, 0x18) : TSC_BG);
      HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, br);
      RoundRect(dc, dz.left + 1, dz.top + 1, dz.right - 1, dz.bottom - 1, TS(40), TS(40));
      SelectObject(dc, op);
      SelectObject(dc, ob);
      DeleteObject(p);
      DeleteObject(br);
      RECT ic = {dz.left + TS(18), y + (dzH - TS(44)) / 2, dz.left + TS(62), y + (dzH + TS(44)) / 2};
      ts_doc_glyph(dc, ic, hov ? TSC_GOLD : TSC_SUB);
      int tx = ic.right + TS(12), tr = b1.left - TS(12);
      tm_text(dc, TF_BODYM, TSC_TEXT, L"Перетащите файлы или папку сюда", (RECT){tx, y + TS(22), tr, y + TS(42)},
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
      tm_text(dc, TF_TINY, TSC_MUTED, L"TIFF (в т.ч. многостраничные), JPG, PNG, WebP, BMP · Ctrl+V — вставить",
              (RECT){tx, y + TS(46), tr, y + TS(62)}, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
      cc_round(dc, b1, TS(14), hov ? TSC_GOLD2 : TSC_GOLD, hov ? TSC_GOLD2 : TSC_GOLD);
      tm_text(dc, TF_BTN, TSC_DARK, L"Выбрать файлы", b1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      BOOL h2 = tm_is_hover(TH_RESET, -1);
      cc_round(dc, b2, TS(14), h2 ? TSC_CARD : TSC_BG, g_tsN ? (h2 ? TSC_GOLD : TSC_BORDER) : tm_mix(TSC_BORDER, TSC_BG, 0.5f));
      tm_text(dc, TF_SMALLM, g_tsN ? TSC_TEXT : TSC_FAINT, L"Сбросить", b2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    y += dzH + TS(16);
  }
  /* колонки */
  int statsH = TS(50); /* итог внизу: 12 + 18 + 20 */
  int colY = y;
  if (g_ui->two) {
    int cw3 = (avail - gap * 2) / 3;
    int h = ch - y - m - statsH;
    if (h < TS(300)) h = TS(300);
    for (int g = 0; g < 3; g++) ts_column(dc, g, m + (cw3 + gap) * g, colY, g == 2 ? avail - (cw3 + gap) * 2 : cw3, h, paint);
    y = colY + h;
  } else {
    for (int g = 0; g < 3; g++) {
      ts_column(dc, g, m, y, avail, TS(340), paint);
      y += TS(340) + gap;
    }
    y -= gap;
  }
  /* итог и что сохранили */
  y += TS(12);
  {
    int a4 = ts_count(TS_A4), a3 = ts_count(TS_A3), ot = ts_count(TS_OTHER), nd = 0;
    for (int i = 0; i < g_tsN; i++)
      if (g_tsItems[i].nodpi) nd++;
    wchar_t t[200];
    if (g_tsN) {
      _snwprintf(t, 200, L"Страниц: %d · A4: %d · A3: %d · Другое: %d", g_tsN, a4, a3, ot);
      if (nd) _snwprintf(t + wcslen(t), 200 - wcslen(t), L" · без dpi: %d — укажите dpi вверху", nd);
    } else {
      lstrcpyW(t, L"PDF — лист размером с оригинал, ч/б без потерь (G4); TIFF — многостраничный");
    }
    t[199] = 0;
    if (paint)
      tm_text(dc, TF_TINY, nd ? RGB(0xE0, 0x8A, 0x6A) : TSC_MUTED, t, (RECT){m, y, cw - m, y + TS(16)},
              DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += TS(18);
    if (g_tsStatus[0]) {
      const wchar_t *l1 = g_tsStatusPath[0] ? L"Открыть" : NULL, *l2 = g_tsStatusPath[0] ? L"Показать в папке" : NULL;
      int w0 = tm_text_w(dc, TF_TINY, g_tsStatus), w1 = l1 ? tm_text_w(dc, TF_SMALLM, l1) : 0,
          w2 = l2 ? tm_text_w(dc, TF_SMALLM, l2) : 0, sp = TS(14);
      int total = w0 + (w1 ? sp + w1 : 0) + (w2 ? sp + w2 : 0), lx = (cw - total) / 2;
      if (lx < m) lx = m;
      if (paint) tm_text(dc, TF_TINY, kTsTitle[TS_A4], g_tsStatus, (RECT){lx, y, lx + w0, y + TS(18)}, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      lx += w0 + sp;
      if (w1) {
        RECT r1 = {lx, y, lx + w1, y + TS(18)};
        tm_hot(r1, TH_OPEN_OUT, -1);
        if (paint) tm_text(dc, TF_SMALLM, tm_is_hover(TH_OPEN_OUT, -1) ? TSC_GOLD2 : TSC_GOLD, l1, r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        lx += w1 + sp;
      }
      if (w2) {
        RECT r2 = {lx, y, lx + w2, y + TS(18)};
        tm_hot(r2, TH_SHOW_OUT, -1);
        if (paint) tm_text(dc, TF_SMALLM, tm_is_hover(TH_SHOW_OUT, -1) ? TSC_GOLD2 : TSC_GOLD, l2, r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      }
    }
    y += TS(20);
  }
  return y + m;
}

/* ---- полотно ------------------------------------------------------------------- */

static void ts_relayout(void) {
  if (!g_tsCanvas) return;
  g_ui = &g_uiSort;
  RECT rc;
  GetClientRect(g_tsCanvas, &rc);
  /* Полоса прокрутки отнимает ширину, от этого подсказка переносится лишней
     строкой — и содержимое уже не влезает, полоса остаётся навсегда. Поэтому
     сначала пробуем без неё: влезает во всю ширину — полосу убираем. */
  if (GetWindowLongW(g_tsCanvas, GWL_STYLE) & WS_VSCROLL) { /* полоса сейчас видна */
    int full = rc.right + GetSystemMetrics(SM_CXVSCROLL);
    tm_fit(full, 1040.0f, 560.0f);
    HDC d0 = GetDC(g_tsCanvas);
    int h0 = ts_draw(d0, full, rc.bottom, FALSE);
    ReleaseDC(g_tsCanvas, d0);
    if (h0 <= rc.bottom) {
      g_tsScroll = 0;
      SCROLLINFO off;
      memset(&off, 0, sizeof(off));
      off.cbSize = sizeof(off);
      off.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
      off.nMax = 0;
      off.nPage = 1;
      SetScrollInfo(g_tsCanvas, SB_VERT, &off, TRUE); /* WM_SIZE разложит заново во всю ширину */
      GetClientRect(g_tsCanvas, &rc);
    }
  }
  tm_fit(rc.right, 1040.0f, 560.0f);
  HDC dc = GetDC(g_tsCanvas);
  g_tsContentH = ts_draw(dc, rc.right, rc.bottom, FALSE);
  ReleaseDC(g_tsCanvas, dc);
  int maxs = g_tsContentH - rc.bottom;
  if (maxs < 0) maxs = 0;
  if (g_tsScroll > maxs) g_tsScroll = maxs;
  if (g_tsScroll < 0) g_tsScroll = 0;
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMax = g_tsContentH - 1;
  si.nPage = (UINT)rc.bottom;
  si.nPos = g_tsScroll;
  SetScrollInfo(g_tsCanvas, SB_VERT, &si, TRUE);
  InvalidateRect(g_tsCanvas, NULL, FALSE);
}

static void ts_scroll_to(int pos) {
  RECT rc;
  GetClientRect(g_tsCanvas, &rc);
  int maxs = g_tsContentH - rc.bottom;
  if (maxs < 0) maxs = 0;
  if (pos > maxs) pos = maxs;
  if (pos < 0) pos = 0;
  if (pos == g_tsScroll) return;
  g_tsScroll = pos;
  SetScrollPos(g_tsCanvas, SB_VERT, pos, TRUE);
  ts_relayout();
}

static int ts_hit(int x, int y, int *arg) {
  *arg = -1;
  if (g_tsExporting) {
    POINT p = {x, y};
    return PtInRect(&g_ui->cancelRc, p) ? -2 : TH_NONE;
  }
  y += g_tsScroll;
  for (int i = g_ui->nhot - 1; i >= 0; i--) {
    POINT p = {x, y};
    if (PtInRect(&g_ui->hot[i].r, p)) {
      *arg = g_ui->hot[i].arg;
      return g_ui->hot[i].id;
    }
  }
  return TH_NONE;
}

/* выбранный лист — в видимую часть его колонки */
static void ts_reveal(int i) {
  if (i < 0 || i >= g_tsN) return;
  int g = g_tsItems[i].grp, k = 0;
  for (int j = 0; j < i; j++)
    if (g_tsItems[j].grp == g) k++;
  int rowH = TS(54), gap = TS(4), lp = TS(8), vpH = g_tsLRc[g].bottom - g_tsLRc[g].top;
  int top = lp + k * (rowH + gap);
  if (top - lp < g_tsLScroll[g]) g_tsLScroll[g] = top - lp;
  else if (top + rowH + lp > g_tsLScroll[g] + vpH) g_tsLScroll[g] = top + rowH + lp - vpH;
}

/* соседний лист в той же колонке: d = -1 / +1 */
static int ts_neighbor(int i, int d) {
  if (i < 0 || i >= g_tsN) return -1;
  for (int j = i + d; j >= 0 && j < g_tsN; j += d)
    if (g_tsItems[j].grp == g_tsItems[i].grp) return j;
  return i;
}

static void ts_context_menu(int i, int sx, int sy) {
  if (i < 0 || i >= g_tsN) return;
  TsItem *it = &g_tsItems[i];
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, 1, L"Открыть");
  AppendMenuW(m, MF_STRING, 2, L"Показать в папке");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING | (it->grp == TS_A4 ? MF_GRAYED : 0), 10, L"Перенести в A4");
  AppendMenuW(m, MF_STRING | (it->grp == TS_A3 ? MF_GRAYED : 0), 11, L"Перенести в A3");
  AppendMenuW(m, MF_STRING | (it->grp == TS_OTHER ? MF_GRAYED : 0), 12, L"Перенести в «Другое»");
  if (it->manual >= 0) AppendMenuW(m, MF_STRING, 13, L"Вернуть, как посчитано");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, 7, L"Убрать из списка\tDel");
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, sx, sy, 0, g_tsWnd, NULL);
  DestroyMenu(m);
  switch (cmd) {
  case 1: tm_open_file(it->pg.path); break;
  case 2: tm_show_in_folder(it->pg.path); break;
  case 10:
  case 11:
  case 12:
    it->manual = cmd - 10;
    ts_classify(it);
    ts_reveal(i);
    ts_relayout();
    break;
  case 13:
    it->manual = -1;
    ts_classify(it);
    ts_reveal(i);
    ts_relayout();
    break;
  case 7: ts_remove(i); break;
  }
}

static void ts_click(int id, int arg) {
  switch (id) {
  case TH_ADD: ts_add_files(); break;
  case TH_RESET: ts_reset(); break;
  case TH_TOL:
    g_tsTol = kTsTols[arg];
    ts_classify_all();
    ts_relayout();
    break;
  case TH_DPI:
    g_tsAssume = kTsDpis[arg];
    ts_classify_all();
    ts_relayout();
    break;
  case TH_PDF: ts_export(arg, TRUE); break;
  case TH_TIFF: ts_export(arg, FALSE); break;
  case TH_OPEN_OUT: tm_open_file(g_tsStatusPath); break;
  case TH_SHOW_OUT: tm_show_in_folder(g_tsStatusPath); break;
  }
}

static LRESULT CALLBACK TsCanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  g_ui = &g_uiSort;
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
    cc_fill(mem, rc, TSC_BG);
    SetBkMode(mem, TRANSPARENT);
    SetViewportOrgEx(mem, 0, -g_tsScroll, NULL);
    ts_draw(mem, rc.right, rc.bottom, TRUE);
    SetViewportOrgEx(mem, 0, 0, NULL);
    if (g_tsExporting) tm_draw_progress(mem, rc.right, rc.bottom, &g_tsCancel, &kTsDark);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_SIZE:
    ts_relayout();
    return 0;
  case WM_VSCROLL: {
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, &si);
    int pos = g_tsScroll;
    switch (LOWORD(wParam)) {
    case SB_LINEUP: pos -= TS(40); break;
    case SB_LINEDOWN: pos += TS(40); break;
    case SB_PAGEUP: pos -= (int)si.nPage; break;
    case SB_PAGEDOWN: pos += (int)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = g_tsContentH; break;
    }
    ts_scroll_to(pos);
    return 0;
  }
  case WM_MOUSEWHEEL: {
    int d = GET_WHEEL_DELTA_WPARAM(wParam);
    if (GetKeyState(VK_CONTROL) & 0x8000) {
      g_ui->zoom *= d > 0 ? 1.1f : 1 / 1.1f;
      if (g_ui->zoom < 0.6f) g_ui->zoom = 0.6f;
      if (g_ui->zoom > 1.6f) g_ui->zoom = 1.6f;
      ts_relayout();
      tm_pref_save(g_tsWnd, L"tiffsort.txt");
      return 0;
    }
    if (g_tsExporting) return 0;
    POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &p);
    p.y += g_tsScroll;
    for (int g = 0; g < 3; g++) { /* над колонкой — листаем её, пока листается */
      int maxs = g_tsLH[g] - (g_tsLRc[g].bottom - g_tsLRc[g].top);
      if (!PtInRect(&g_tsLRc[g], p) || maxs <= 0) continue;
      int pos = g_tsLScroll[g] - d * TS(100) / WHEEL_DELTA;
      if (pos < 0) pos = 0;
      if (pos > maxs) pos = maxs;
      if (pos != g_tsLScroll[g]) {
        g_tsLScroll[g] = pos;
        ts_relayout();
        return 0;
      }
    }
    ts_scroll_to(g_tsScroll - d * TS(60) / WHEEL_DELTA);
    return 0;
  }
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT) {
      POINT p;
      GetCursorPos(&p);
      ScreenToClient(hwnd, &p);
      int arg, id = ts_hit(p.x, p.y, &arg);
      if (id != TH_NONE && id != TH_ROW) {
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
      }
    }
    break;
  case WM_MOUSEMOVE: {
    int arg, id = ts_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &arg);
    if (id != g_ui->hoverId || arg != g_ui->hoverArg) {
      g_ui->hoverId = id;
      g_ui->hoverArg = arg;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    TRACKMOUSEEVENT te = {sizeof(te), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&te);
    return 0;
  }
  case WM_MOUSELEAVE:
    if (g_ui->hoverId != TH_NONE) {
      g_ui->hoverId = TH_NONE;
      g_ui->hoverArg = -1;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_LBUTTONDOWN: {
    SetFocus(hwnd);
    int arg, id = ts_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &arg);
    if (id == -2) {
      InterlockedExchange(&g_tsCancel, 1);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    if (g_tsExporting) return 0;
    if (id == TH_ROW) {
      g_tsSel = arg;
      InvalidateRect(hwnd, NULL, FALSE);
    } else if (id != TH_NONE) {
      ts_click(id, arg);
    }
    return 0;
  }
  case WM_LBUTTONDBLCLK: {
    int arg, id = ts_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &arg);
    if (id == TH_ROW && arg >= 0 && arg < g_tsN) tm_open_file(g_tsItems[arg].pg.path);
    return 0;
  }
  case WM_RBUTTONUP: {
    int arg, id = ts_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &arg);
    if (id == TH_ROW && !g_tsExporting) {
      g_tsSel = arg;
      InvalidateRect(hwnd, NULL, FALSE);
      POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ClientToScreen(hwnd, &p);
      ts_context_menu(arg, p.x, p.y);
    }
    return 0;
  }
  case WM_GETDLGCODE:
    return DLGC_WANTARROWS | DLGC_WANTCHARS;
  case WM_KEYDOWN: {
    if (g_tsExporting) {
      if (wParam == VK_ESCAPE) InterlockedExchange(&g_tsCancel, 1);
      return 0;
    }
    BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    switch (wParam) {
    case VK_UP:
    case VK_DOWN:
      if (g_tsSel < 0 && g_tsN) g_tsSel = 0;
      else g_tsSel = ts_neighbor(g_tsSel, wParam == VK_UP ? -1 : 1);
      ts_reveal(g_tsSel);
      ts_relayout();
      break;
    case VK_DELETE: ts_remove(g_tsSel); break;
    case VK_RETURN:
      if (g_tsSel >= 0 && g_tsSel < g_tsN) tm_open_file(g_tsItems[g_tsSel].pg.path);
      break;
    case 'V':
      if (ctrl) ts_paste();
      break;
    case 'O':
      if (ctrl) ts_add_files();
      break;
    }
    return 0;
  }
  case WM_DROPFILES: {
    HDROP hd = (HDROP)wParam;
    TmPaths l = {0};
    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < n; i++) {
      wchar_t p[MAX_PATH];
      if (DragQueryFileW(hd, i, p, MAX_PATH)) tm_paths_add_any(&l, p);
    }
    DragFinish(hd);
    ts_load_paths(&l);
    SetForegroundWindow(g_tsWnd);
    return 0;
  }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ts_done(TmResult *r) {
  if (r->type == TJ_LOAD) {
    if (g_tsLoadPending > 0) g_tsLoadPending--;
  } else {
    g_tsExporting = FALSE;
    if (r->ok) {
      wchar_t sz[32];
      double mb = r->bytes / 1048576.0;
      if (mb < 0.1) _snwprintf(sz, 32, L"%d КБ", (int)((r->bytes + 1023) / 1024));
      else _snwprintf(sz, 32, L"%d,%d МБ", (int)mb, (int)((mb - (int)mb) * 10));
      _snwprintf(g_tsStatus, MAX_PATH + 80, L"Сохранено: %s · %d стр. · %s", tm_base(r->out), r->pages, sz);
      g_tsStatus[MAX_PATH + 79] = 0;
      lstrcpynW(g_tsStatusPath, r->out, MAX_PATH);
    } else if (r->cancelled) {
      lstrcpyW(g_tsStatus, L"Сохранение отменено");
      g_tsStatusPath[0] = 0;
    }
  }
  ts_relayout();
  if (r->err[0]) {
    wchar_t msg[2300];
    _snwprintf(msg, 2300, L"%s\n\n%s", r->type == TJ_LOAD ? L"Не удалось открыть:" : L"Файл не сохранён:", r->err);
    msg[2299] = 0;
    MessageBoxW(g_tsWnd, msg, L"Сортировка TIFF A4 / A3", MB_ICONWARNING);
  }
  free(r);
}

static LRESULT CALLBACK TsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  g_ui = &g_uiSort;
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = (int)(480 * g_ui->dpi);
    mm->ptMinTrackSize.y = (int)(420 * g_ui->dpi);
    return 0;
  }
  case WM_SIZE:
    if (g_tsCanvas) MoveWindow(g_tsCanvas, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
    return 0;
  case WM_SETFOCUS:
    if (g_tsCanvas) SetFocus(g_tsCanvas);
    return 0;
  case WM_MOUSEWHEEL:
    if (g_tsCanvas) SendMessageW(g_tsCanvas, msg, wParam, lParam);
    return 0;
  case WM_TM_PAGE: {
    TmPage *pg = (TmPage *)lParam;
    if (pg->thumb) DeleteObject(pg->thumb);
    pg->thumb = NULL;
    if (g_tsN >= g_tsCap) {
      int c = g_tsCap ? g_tsCap * 2 : 64;
      TsItem *q = (TsItem *)realloc(g_tsItems, sizeof(TsItem) * (size_t)c);
      if (!q) {
        free(pg);
        return 0;
      }
      g_tsItems = q;
      g_tsCap = c;
    }
    TsItem *it = &g_tsItems[g_tsN++];
    memset(it, 0, sizeof(*it));
    it->pg = *pg;
    it->manual = -1;
    free(pg);
    ts_classify(it);
    g_tsStatus[0] = 0;
    ts_relayout();
    return 0;
  }
  case WM_TM_PROGRESS:
    if (g_tsCanvas) InvalidateRect(g_tsCanvas, NULL, FALSE);
    return 0;
  case WM_TM_DONE:
    ts_done((TmResult *)lParam);
    return 0;
  case WM_EXITSIZEMOVE:
    tm_pref_save(hwnd, L"tiffsort.txt");
    return 0;
  case WM_CLOSE:
    tm_pref_save(hwnd, L"tiffsort.txt");
    ShowWindow(hwnd, SW_HIDE); /* прячем: список остаётся до следующего открытия */
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void tiffsort_show(void) {
  g_ui = &g_uiSort;
  if (!g_tsWnd) {
    tm_common_init();
    HDC s = GetDC(NULL);
    g_ui->dpi = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    g_ui->s = g_ui->dpi;
    if (s) ReleaseDC(NULL, s);
    tm_fonts();
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TsProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CursorPadTiffSort";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = TsCanvasProc;
    wc.lpszClassName = L"CursorPadTiffSortCanvas";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int waw = wa.right - wa.left, wah = wa.bottom - wa.top;
    int ww = (int)(1080 * g_ui->dpi), wh = (int)(780 * g_ui->dpi);
    if (ww > waw * 3 / 4) ww = waw * 3 / 4;
    if (wh > wah * 7 / 8) wh = wah * 7 / 8;
    int wx = wa.left + (waw - ww) / 2, wy = wa.top + (wah - wh) / 2;
    RECT saved;
    if (tm_pref_load(L"tiffsort.txt", &saved)) {
      wx = saved.left;
      wy = saved.top;
      ww = saved.right - saved.left;
      wh = saved.bottom - saved.top;
    }
    g_tsWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"CursorPadTiffSort", L"Сортировка TIFF A4 / A3 — CursorPad",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wx, wy, ww, wh, NULL, NULL, g_inst, NULL);
    if (!g_tsWnd) return;
    RECT rc;
    GetClientRect(g_tsWnd, &rc);
    g_tsCanvas = CreateWindowExW(WS_EX_ACCEPTFILES, L"CursorPadTiffSortCanvas", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                 0, 0, rc.right, rc.bottom, g_tsWnd, NULL, g_inst, NULL);
    typedef BOOL(WINAPI * ChangeFilterEx)(HWND, UINT, DWORD, void *);
    ChangeFilterEx cf = (ChangeFilterEx)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "ChangeWindowMessageFilterEx");
    if (cf) {
      cf(g_tsCanvas, WM_DROPFILES, 1, NULL);
      cf(g_tsCanvas, 0x0049 /* WM_COPYGLOBALDATA */, 1, NULL);
    }
    ts_relayout();
  }
  ShowWindow(g_tsWnd, IsIconic(g_tsWnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g_tsWnd);
  SetFocus(g_tsCanvas);
}
