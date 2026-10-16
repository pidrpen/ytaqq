/* ---- Окно «Сопоставить с Союзом» (движок — match.c, правила — match_rules.txt) -------

   Слева направо: список позиций из Excel (вставить ячейки из буфера или открыть
   CSV/TXT), данные Союза (CSV выгрузки материалов, буфер или последняя «Выгрузка
   из PLM»), «Сопоставить». Для каждой позиции — лучший кандидат из Союза, оценка
   в процентах и пометка: зелёное — точное совпадение, жёлтое — вероятное,
   красное — ничего похожего. Строку можно раскрыть: ещё до четырёх кандидатов.
   Ячейки выбираются и копируются как в окне «Выгрузка из PLM»; результат
   сохраняется в Excel (.xlsx) или CSV. В Союз ничего не записывается. */

#include "match.c"

#define ID_MT_XPASTE 880
#define ID_MT_XFILE 881
#define ID_MT_SFILE 882
#define ID_MT_SPASTE 883
#define ID_MT_SPLM 884
#define ID_MT_XD 885 /* столбцы: обозначение / наименование у позиций (X) и у Союза (S) */
#define ID_MT_XN 886
#define ID_MT_SD 887
#define ID_MT_SN 888
#define ID_MT_XHDR 889
#define ID_MT_SHDR 890
#define ID_MT_RUN 891
#define ID_MT_EXPAND 892
#define ID_MT_COLLAPSE 893
#define ID_MT_FILTER 894
#define ID_MT_COPY 895
#define ID_MT_SAVE 896
#define ID_MT_RULES 897
#define ID_MT_TIMER 898
#define WM_MT_DONE (WM_APP + 120)
#define MT_NCOLS 8
#define MT_RULES_RES 346 /* match_rules.txt в exe */
#define MT_CM_CELL 1
#define MT_CM_ROW 2
#define MT_CM_COL 3
#define MT_CM_SEL 4
#define MT_CM_ALL 5

/* таблица, разобранная из буфера или файла */
typedef struct {
  wchar_t **cell; /* n × nc */
  int n, nc;
  BOOL hdr; /* первая строка — заголовки */
} MtwTab;

typedef struct {
  int nh; /* сколько кандидатов нашлось */
  MtHit h[MT_MAX_HITS];
  BOOL ai; /* порядок поправил помощник */
} MtwRes;

typedef struct {
  int pos, rank; /* позиция Excel и номер кандидата: 0 — главная строка */
} MtwDisp;

static HWND g_mtWnd, g_mtList, g_mtCb[4], g_mtChk[2], g_mtFilter;
static WNDPROC g_mtListOld;
static float g_mtS = 1.0f;
static MtwTab g_mtX, g_mtS2; /* позиции Excel; данные Союза */
static int g_mtCol[4] = {-1, -1, -1, -1}; /* Xd, Xn, Sd, Sn */
static wchar_t g_mtXSrc[MAX_PATH], g_mtSSrc[MAX_PATH];
static MtwRes *g_mtRes;
static unsigned char *g_mtOpen;
static int g_mtResN;
static MtwDisp *g_mtDisp;
static int g_mtDispN;
static MtRules *g_mtRules;
static MtIndex *g_mtIx;
static int *g_mtIxRow; /* позиция индекса → строка таблицы Союза */
static int g_mtIxN;
static int g_mtCnt[3];
static volatile LONG g_mtBusy, g_mtDone, g_mtTotal;
static wchar_t g_mtStatus[400];
static int g_mtAr = -1, g_mtAc, g_mtCr = -1, g_mtCc;
static BOOL g_mtDrag;

static int MTS(int v) { return (int)(v * g_mtS + 0.5f); }

static void mtw_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_mtStatus, 400, fmt, ap);
  va_end(ap);
  g_mtStatus[399] = 0;
  if (g_mtWnd) InvalidateRect(g_mtWnd, NULL, FALSE);
}

/* ---- файлы и буфер ---------------------------------------------------------- */
static wchar_t *mtw_bytes_to_text(const unsigned char *b, size_t n) {
  if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) { /* UTF-16 */
    wchar_t *w = (wchar_t *)malloc(n);
    if (!w) return NULL;
    memcpy(w, b + 2, n - 2);
    w[(n - 2) / 2] = 0;
    return w;
  }
  if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) b += 3, n -= 3;
  int cp = CP_UTF8;
  int len = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, (const char *)b, (int)n, NULL, 0);
  if (len <= 0 && n > 0) { /* не UTF-8 — «Excel CSV» в кодировке Windows */
    cp = 1251;
    len = MultiByteToWideChar(cp, 0, (const char *)b, (int)n, NULL, 0);
  }
  wchar_t *w = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
  if (!w) return NULL;
  if (len > 0) MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, (const char *)b, (int)n, w, len);
  w[len > 0 ? len : 0] = 0;
  return w;
}

static wchar_t *mtw_read_file(const wchar_t *path) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return NULL;
  DWORD sz = GetFileSize(f, NULL), got = 0;
  if (sz == INVALID_FILE_SIZE || sz > 200u * 1024 * 1024) {
    CloseHandle(f);
    return NULL;
  }
  unsigned char *b = (unsigned char *)malloc((size_t)sz + 2);
  if (!b) {
    CloseHandle(f);
    return NULL;
  }
  ReadFile(f, b, sz, &got, NULL);
  CloseHandle(f);
  wchar_t *w = mtw_bytes_to_text(b, got);
  free(b);
  return w;
}

static wchar_t *mtw_clip_text(void) {
  wchar_t *r = NULL;
  if (!OpenClipboard(g_mtWnd)) return NULL;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (h) {
    const wchar_t *p = (const wchar_t *)GlobalLock(h);
    if (p) {
      r = mt_wdup(p, (int)wcslen(p));
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return r;
}

/* ---- таблица ------------------------------------------------------------------ */
static void mtw_tab_free(MtwTab *t) {
  for (size_t i = 0; i < (size_t)t->n * (size_t)t->nc; i++) free(t->cell[i]);
  free(t->cell);
  memset(t, 0, sizeof(*t));
}

static const wchar_t *mtw_tc(const MtwTab *t, int r, int c) {
  if (r < 0 || r >= t->n || c < 0 || c >= t->nc) return L"";
  const wchar_t *v = t->cell[(size_t)r * (size_t)t->nc + (size_t)c];
  return v ? v : L"";
}

/* Excel кладёт в буфер строки с табами; CSV — с «;» или «,»; в кавычках — любой знак */
#define MT_MAXC 64
static BOOL mtw_parse(MtwTab *t, const wchar_t *s) {
  mtw_tab_free(t);
  if (!s) return FALSE;
  int tabs = 0, semi = 0, comma = 0;
  BOOL q = FALSE;
  for (int i = 0; s[i] && i < 20000; i++) {
    if (s[i] == '"') q = !q;
    else if (!q && s[i] == '\t') tabs++;
    else if (!q && s[i] == ';') semi++;
    else if (!q && s[i] == ',') comma++;
  }
  wchar_t delim = tabs ? L'\t' : (semi && semi >= comma) ? L';' : comma ? L',' : 0;
  size_t cap = 256, nrows = 0;
  wchar_t ***rows = (wchar_t ***)malloc(cap * sizeof(wchar_t **));
  int *nf = (int *)malloc(cap * sizeof(int));
  if (!rows || !nf) {
    free(rows);
    free(nf);
    return FALSE;
  }
  const wchar_t *p = s;
  if (*p == 0xFEFF) p++;
  int maxc = 0;
  while (*p) {
    wchar_t *f[MT_MAXC];
    int n = 0;
    BOOL any = FALSE, eol = FALSE;
    while (!eol) {
      size_t bc = 64, bl = 0;
      wchar_t *b = (wchar_t *)malloc(bc * sizeof(wchar_t));
      BOOL quoted = FALSE;
      if (*p == '"') {
        quoted = TRUE;
        p++;
      }
      for (;;) {
        wchar_t c = *p;
        if (!c) {
          eol = TRUE;
          break;
        }
        if (quoted) {
          if (c == '"') {
            if (p[1] == '"') {
              c = '"';
              p++;
            } else {
              quoted = FALSE;
              p++;
              continue;
            }
          }
        } else if (c == '\n' || c == '\r') {
          if (c == '\r' && p[1] == '\n') p++;
          p++;
          eol = TRUE;
          break;
        } else if (delim && c == delim) {
          p++;
          break;
        }
        if (bl + 2 > bc) {
          bc *= 2;
          b = (wchar_t *)realloc(b, bc * sizeof(wchar_t));
        }
        b[bl++] = c;
        p++;
      }
      b[bl] = 0;
      while (bl > 0 && (b[bl - 1] == ' ' || b[bl - 1] == 0xA0 || b[bl - 1] == '\t')) b[--bl] = 0;
      wchar_t *st = b;
      while (*st == ' ' || *st == 0xA0) st++;
      if (st != b) memmove(b, st, (wcslen(st) + 1) * sizeof(wchar_t));
      if (b[0]) any = TRUE;
      if (n < MT_MAXC) f[n++] = b;
      else free(b);
    }
    if (!any) {
      for (int i = 0; i < n; i++) free(f[i]);
      continue;
    }
    if (nrows == cap) {
      cap *= 2;
      rows = (wchar_t ***)realloc(rows, cap * sizeof(wchar_t **));
      nf = (int *)realloc(nf, cap * sizeof(int));
    }
    rows[nrows] = (wchar_t **)malloc((size_t)n * sizeof(wchar_t *));
    memcpy(rows[nrows], f, (size_t)n * sizeof(wchar_t *));
    nf[nrows] = n;
    if (n > maxc) maxc = n;
    nrows++;
  }
  if (nrows && maxc) {
    t->n = (int)nrows;
    t->nc = maxc;
    t->cell = (wchar_t **)calloc(nrows * (size_t)maxc, sizeof(wchar_t *));
    for (size_t r = 0; r < nrows; r++) {
      for (int c = 0; c < nf[r]; c++) t->cell[r * (size_t)maxc + (size_t)c] = rows[r][c];
      free(rows[r]);
    }
  }
  free(rows);
  free(nf);
  return t->n > 0;
}

static BOOL mtw_has(const wchar_t *cell, const wchar_t *w) {
  wchar_t b[200];
  lstrcpynW(b, cell, 200);
  CharLowerBuffW(b, (DWORD)wcslen(b));
  return wcsstr(b, w) != NULL;
}

/* заголовок есть, если в первой строке встретились «обозначение», «наименование»… */
static void mtw_detect(MtwTab *t, int *d, int *n) {
  t->hdr = FALSE;
  *d = *n = -1;
  if (t->n < 1) return;
  for (int c = 0; c < t->nc; c++) {
    const wchar_t *v = mtw_tc(t, 0, c);
    if (mtw_has(v, L"обознач") || mtw_has(v, L"designation")) {
      t->hdr = TRUE;
      if (*d < 0) *d = c;
    } else if (mtw_has(v, L"наимен") || !_wcsicmp(v, L"name")) {
      t->hdr = TRUE;
      if (*n < 0) *n = c;
    }
  }
  if (*d < 0 && *n < 0) {
    if (t->nc >= 2) *d = 0, *n = 1;
    else *n = 0;
  }
}

static int mtw_first(const MtwTab *t) { return t->hdr ? 1 : 0; }

static void mtw_col_name(int c, wchar_t *out) {
  if (c < 26) _snwprintf(out, 8, L"%c", (wchar_t)(L'A' + c));
  else _snwprintf(out, 8, L"%c%c", (wchar_t)(L'A' + c / 26 - 1), (wchar_t)(L'A' + c % 26));
  out[7] = 0;
}

static void mtw_fill_combo(HWND cb, const MtwTab *t, int sel) {
  SendMessageW(cb, CB_RESETCONTENT, 0, 0);
  SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"— нет —");
  for (int c = 0; c < t->nc; c++) {
    wchar_t nm[8], s[80];
    mtw_col_name(c, nm);
    const wchar_t *v = mtw_tc(t, t->n > 1 || t->hdr ? 0 : 0, c);
    if (!t->hdr && t->n > 0) v = mtw_tc(t, 0, c);
    wchar_t cut[26];
    lstrcpynW(cut, v, 24);
    if (wcslen(v) > 23) wcscpy(cut + 22, L"…");
    _snwprintf(s, 80, L"%s: %s", nm, cut);
    s[79] = 0;
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)s);
  }
  SendMessageW(cb, CB_SETCURSEL, (WPARAM)(sel + 1), 0);
}

/* ---- результаты ---------------------------------------------------------------- */
static int mtw_thr(int which) { return mt_rules_threshold(g_mtRules, which); }

static int mtw_cls(const MtwRes *r) { /* 2 точно, 1 вероятно, 0 нет */
  if (r->nh <= 0) return 0;
  int s = r->h[0].score;
  return s >= mtw_thr(MT_T_EXACT) ? 2 : s >= mtw_thr(MT_T_LIKELY) ? 1 : 0;
}

static int mtw_nshown(const MtwRes *r) { /* строк: главная и раскрытые */
  int n = 0;
  for (int i = 0; i < r->nh; i++)
    if (r->h[i].score >= mtw_thr(MT_T_SHOW)) n++;
  return n;
}

static void mtw_res_free(void) {
  free(g_mtRes);
  free(g_mtOpen);
  free(g_mtDisp);
  g_mtRes = NULL;
  g_mtOpen = NULL;
  g_mtDisp = NULL;
  g_mtResN = g_mtDispN = 0;
  memset(g_mtCnt, 0, sizeof(g_mtCnt));
  if (g_mtIx) mt_index_free(g_mtIx);
  g_mtIx = NULL;
  free(g_mtIxRow);
  g_mtIxRow = NULL;
  g_mtIxN = 0;
  if (g_mtRules) mt_rules_free(g_mtRules);
  g_mtRules = NULL;
}

static void mtw_sel_clear(void) {
  g_mtAr = g_mtCr = -1;
  g_mtDrag = FALSE;
}

static void mtw_rebuild(void) {
  int flt = g_mtFilter ? (int)SendMessageW(g_mtFilter, CB_GETCURSEL, 0, 0) : 0;
  free(g_mtDisp);
  g_mtDisp = NULL;
  g_mtDispN = 0;
  size_t cap = (size_t)g_mtResN * 5 + 4;
  g_mtDisp = (MtwDisp *)malloc(cap * sizeof(MtwDisp));
  for (int k = 0; g_mtDisp && k < g_mtResN; k++) {
    int cls = mtw_cls(&g_mtRes[k]);
    if (flt == 1 && cls != 2) continue;
    if (flt == 2 && cls != 1) continue;
    if (flt == 3 && cls != 0) continue;
    g_mtDisp[g_mtDispN].pos = k;
    g_mtDisp[g_mtDispN++].rank = 0;
    if (g_mtOpen[k]) {
      int ns = mtw_nshown(&g_mtRes[k]);
      for (int i = 1; i < ns; i++) {
        g_mtDisp[g_mtDispN].pos = k;
        g_mtDisp[g_mtDispN++].rank = i;
      }
    }
  }
  if (g_mtList) {
    SendMessageW(g_mtList, LVM_SETITEMCOUNT, (WPARAM)g_mtDispN, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    InvalidateRect(g_mtList, NULL, TRUE);
  }
}

static const wchar_t *mtw_xc(int k, int which) { /* ячейка позиции Excel: 0 обозначение, 1 наименование */
  int c = g_mtCol[which];
  return c < 0 ? L"" : mtw_tc(&g_mtX, k + mtw_first(&g_mtX), c);
}

static const wchar_t *mtw_sc(int item, int which) {
  int c = g_mtCol[2 + which];
  return c < 0 || item < 0 || item >= g_mtIxN ? L"" : mtw_tc(&g_mtS2, g_mtIxRow[item], c);
}

static const wchar_t *mtw_cell(int row, int col, wchar_t *buf) {
  buf[0] = 0;
  if (row < 0 || row >= g_mtDispN) return buf;
  MtwDisp d = g_mtDisp[row];
  const MtwRes *r = &g_mtRes[d.pos];
  const MtHit *h = d.rank < r->nh && r->h[d.rank].score >= mtw_thr(MT_T_SHOW) ? &r->h[d.rank] : NULL;
  switch (col) {
  case 0:
    if (d.rank == 0)
      _snwprintf(buf, 40, L"%ls %d", mtw_nshown(r) > 1 ? (g_mtOpen[d.pos] ? L"−" : L"+") : L" ", d.pos + 1);
    else
      _snwprintf(buf, 40, L"     └ %d", d.rank + 1);
    break;
  case 1:
    if (d.rank == 0) lstrcpynW(buf, mtw_xc(d.pos, 0), 500);
    break;
  case 2:
    if (d.rank == 0) lstrcpynW(buf, mtw_xc(d.pos, 1), 500);
    break;
  case 3:
    if (h) lstrcpynW(buf, mtw_sc(h->item, 0), 500);
    break;
  case 4:
    if (h) lstrcpynW(buf, mtw_sc(h->item, 1), 500);
    break;
  case 5:
    if (h) _snwprintf(buf, 16, L"%d%%", h->score);
    break;
  case 6:
    if (d.rank == 0) {
      int c = mtw_cls(r);
      lstrcpynW(buf, c == 2 ? L"Точное совпадение" : c == 1 ? L"Вероятное" : L"Ничего похожего", 40);
    }
    break;
  case 7:
    if (h) mt_note(h, buf, 300);
    else if (d.rank == 0 && !*mtw_xc(d.pos, 0) && !*mtw_xc(d.pos, 1)) wcscpy(buf, L"пустая позиция");
    break;
  }
  return buf;
}

/* ---- сопоставление (в потоке) ---------------------------------------------------- */
static wchar_t *mtw_rules_text(void) {
  wchar_t *base = NULL, *user = NULL;
  HRSRC rs = FindResourceW(g_inst, MAKEINTRESOURCEW(MT_RULES_RES), (LPCWSTR)RT_RCDATA);
  if (rs) {
    HGLOBAL hg = LoadResource(g_inst, rs);
    const void *p = hg ? LockResource(hg) : NULL;
    if (p) base = mtw_bytes_to_text((const unsigned char *)p, SizeofResource(g_inst, rs));
  }
  if (g_dataDir[0]) {
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\match_rules.txt", g_dataDir);
    path[MAX_PATH - 1] = 0;
    user = mtw_read_file(path);
  }
  size_t lb = base ? wcslen(base) : 0, lu = user ? wcslen(user) : 0;
  wchar_t *all = (wchar_t *)malloc((lb + lu + 4) * sizeof(wchar_t));
  if (all) {
    if (lb) wmemcpy(all, base, lb);
    all[lb] = '\n';
    if (lu) wmemcpy(all + lb + 1, user, lu);
    all[lb + 1 + lu] = 0;
  }
  free(base);
  free(user);
  return all;
}

static void mtw_match_all(void) {
  int nx = g_mtX.n - mtw_first(&g_mtX);
  mtw_res_free();
  g_mtResN = nx > 0 ? nx : 0;
  g_mtRes = (MtwRes *)calloc((size_t)g_mtResN + 1, sizeof(MtwRes));
  g_mtOpen = (unsigned char *)calloc((size_t)g_mtResN + 1, 1);
  wchar_t *rt = mtw_rules_text();
  g_mtRules = mt_rules_parse(rt);
  free(rt);
  g_mtIx = mt_index_new(g_mtRules);
  int ns = g_mtS2.n - mtw_first(&g_mtS2);
  g_mtIxRow = (int *)malloc(((size_t)(ns > 0 ? ns : 0) + 1) * sizeof(int));
  g_mtIxN = 0;
  for (int i = 0; i < ns; i++) {
    int row = i + mtw_first(&g_mtS2);
    const wchar_t *d = g_mtCol[2] < 0 ? L"" : mtw_tc(&g_mtS2, row, g_mtCol[2]);
    const wchar_t *n = g_mtCol[3] < 0 ? L"" : mtw_tc(&g_mtS2, row, g_mtCol[3]);
    if (!*d && !*n) continue;
    g_mtIxRow[g_mtIxN++] = row;
    mt_index_add(g_mtIx, d, n);
  }
  mt_index_build(g_mtIx);
  g_mtTotal = g_mtResN;
  for (int k = 0; k < g_mtResN; k++) {
    MtwRes *r = &g_mtRes[k];
    const wchar_t *d = mtw_xc(k, 0), *n = mtw_xc(k, 1);
    r->nh = mt_find(g_mtIx, d, n, r->h, MT_MAX_HITS);
    if (mt_ai_wanted(r->h, r->nh, g_mtRules)) { /* неоднозначная позиция — спросить помощника (пока заглушка) */
      MtAiCand c[MT_MAX_HITS];
      int order[MT_MAX_HITS];
      for (int i = 0; i < r->nh; i++) {
        c[i].des = mtw_sc(r->h[i].item, 0);
        c[i].name = mtw_sc(r->h[i].item, 1);
        c[i].score = r->h[i].score;
        c[i].flags = r->h[i].flags;
        order[i] = i;
      }
      if (mt_ai_rank_candidates(d, n, c, r->nh, order)) {
        MtHit tmp[MT_MAX_HITS];
        for (int i = 0; i < r->nh; i++) {
          tmp[i] = r->h[order[i] >= 0 && order[i] < r->nh ? order[i] : i];
          tmp[i].score = c[order[i] >= 0 && order[i] < r->nh ? order[i] : i].score;
        }
        memcpy(r->h, tmp, sizeof(tmp[0]) * (size_t)r->nh);
        r->ai = TRUE;
      }
    }
    InterlockedExchange(&g_mtDone, k + 1);
  }
  for (int k = 0; k < g_mtResN; k++) g_mtCnt[mtw_cls(&g_mtRes[k])]++;
}

static DWORD WINAPI mtw_thread(LPVOID param) {
  (void)param;
  mtw_match_all();
  PostMessageW(g_mtWnd, WM_MT_DONE, 0, 0);
  return 0;
}

static void mtw_run(void) {
  if (g_mtBusy) return;
  if (g_mtX.n - mtw_first(&g_mtX) < 1) {
    mtw_status(L"Сначала загрузите позиции из Excel");
    return;
  }
  if (g_mtS2.n - mtw_first(&g_mtS2) < 1) {
    mtw_status(L"Сначала загрузите данные Союза");
    return;
  }
  if (g_mtCol[0] < 0 && g_mtCol[1] < 0) {
    mtw_status(L"У позиций Excel выберите столбец с обозначением или наименованием");
    return;
  }
  if (g_mtCol[2] < 0 && g_mtCol[3] < 0) {
    mtw_status(L"У данных Союза выберите столбец с обозначением или наименованием");
    return;
  }
  mtw_sel_clear();
  g_mtBusy = 1;
  g_mtDone = 0;
  g_mtTotal = 0;
  SetWindowTextW(GetDlgItem(g_mtWnd, ID_MT_RUN), L"Идёт…");
  mtw_status(L"Готовлю данные Союза: позиций %d…", g_mtS2.n - mtw_first(&g_mtS2));
  SetTimer(g_mtWnd, ID_MT_TIMER, 250, NULL);
  HANDLE th = CreateThread(NULL, 0, mtw_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else {
    mtw_thread(NULL);
  }
}

/* ---- настройки окна -------------------------------------------------------------- */
static void mtw_prefs_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\match.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static void mtw_prefs_save(void) {
  if (!g_dataDir[0]) return;
  wchar_t buf[MAX_PATH + 100], p[MAX_PATH];
  _snwprintf(buf, MAX_PATH + 100, L"soyuz\t%s\nsd\t%d\nsn\t%d\n", g_mtSSrc, g_mtCol[2], g_mtCol[3]);
  mtw_prefs_path(p);
  share_write_ex(p, buf, TRUE);
}

static void mtw_info(void) { InvalidateRect(g_mtWnd, NULL, FALSE); }

/* новые данные: столбцы, заголовок, прежние результаты не годятся */
static void mtw_after_load(BOOL soyuz) {
  MtwTab *t = soyuz ? &g_mtS2 : &g_mtX;
  int d, n;
  mtw_detect(t, &d, &n);
  g_mtCol[soyuz ? 2 : 0] = d;
  g_mtCol[soyuz ? 3 : 1] = n;
  mtw_fill_combo(g_mtCb[soyuz ? 2 : 0], t, d);
  mtw_fill_combo(g_mtCb[soyuz ? 3 : 1], t, n);
  SendMessageW(g_mtChk[soyuz ? 1 : 0], BM_SETCHECK, t->hdr ? BST_CHECKED : BST_UNCHECKED, 0);
  mtw_res_free();
  mtw_sel_clear();
  mtw_rebuild();
  mtw_info();
}

static void mtw_load_text(BOOL soyuz, wchar_t *text, const wchar_t *src) {
  MtwTab *t = soyuz ? &g_mtS2 : &g_mtX;
  BOOL ok = text && mtw_parse(t, text);
  free(text);
  wchar_t *dst = soyuz ? g_mtSSrc : g_mtXSrc;
  if (!ok) {
    dst[0] = 0;
    mtw_status(src && *src ? L"В файле нет таблицы" : L"В буфере нет таблицы: скопируйте ячейки в Excel (Ctrl+C)");
    return;
  }
  lstrcpynW(dst, src ? src : L"", MAX_PATH);
  mtw_after_load(soyuz);
  int rows = t->n - mtw_first(t);
  mtw_status(L"%s: %d %s, столбцов %d", soyuz ? L"Данные Союза" : L"Позиции Excel", rows,
             rows % 10 == 1 && rows % 100 != 11 ? L"строка" : (rows % 10 >= 2 && rows % 10 <= 4 && (rows % 100 < 12 || rows % 100 > 14)) ? L"строки" : L"строк",
             t->nc);
  if (soyuz) mtw_prefs_save();
}

static void mtw_open_file(BOOL soyuz) {
  wchar_t file[MAX_PATH] = L"";
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_mtWnd;
  of.lpstrFilter = L"Таблица (*.csv;*.txt;*.tsv)\0*.csv;*.txt;*.tsv\0Все файлы\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrTitle = soyuz ? L"Данные Союза: CSV выгрузки материалов" : L"Позиции из Excel: CSV или TXT";
  of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&of)) return;
  wchar_t *txt = mtw_read_file(file);
  if (!txt) {
    mtw_status(L"Не удалось прочитать файл");
    return;
  }
  mtw_load_text(soyuz, txt, file);
}

/* данные Союза — из последней «Выгрузки из PLM»: лист «Состав (ЭСИ)» с обозначением и наименованием */
static void mtw_from_plm(void) {
  if (!g_xpJob || g_xpJob->t[XP_COMP].n <= 0) {
    mtw_status(L"Сначала соберите «Выгрузку из PLM» (Ещё → Выгрузка из PLM): ее состав попадёт сюда");
    return;
  }
  XpTable *x = &g_xpJob->t[XP_COMP];
  mtw_tab_free(&g_mtS2);
  g_mtS2.nc = x->ncols;
  g_mtS2.n = x->n + 1;
  g_mtS2.cell = (wchar_t **)calloc((size_t)g_mtS2.n * (size_t)g_mtS2.nc, sizeof(wchar_t *));
  if (!g_mtS2.cell) {
    g_mtS2.n = 0;
    return;
  }
  for (int c = 0; c < g_mtS2.nc; c++) g_mtS2.cell[c] = mt_wdup(kXpCompHead[c], (int)wcslen(kXpCompHead[c]));
  for (int r = 0; r < x->n; r++)
    for (int c = 0; c < x->ncols; c++) {
      const wchar_t *v = xp_cell(x, r, c);
      g_mtS2.cell[(size_t)(r + 1) * (size_t)g_mtS2.nc + (size_t)c] = mt_wdup(v, (int)wcslen(v));
    }
  wcscpy(g_mtSSrc, L"");
  mtw_after_load(TRUE);
  mtw_status(L"Данные Союза: состав из выгрузки PLM, %d позиций", x->n);
}

/* ---- выбор ячеек и копирование (как в окне «Выгрузка из PLM») ----------------------- */
static void mtw_sel_rect(int *r0, int *r1, int *c0, int *c1) {
  *r0 = g_mtAr < g_mtCr ? g_mtAr : g_mtCr;
  *r1 = g_mtAr < g_mtCr ? g_mtCr : g_mtAr;
  *c0 = g_mtAc < g_mtCc ? g_mtAc : g_mtCc;
  *c1 = g_mtAc < g_mtCc ? g_mtCc : g_mtAc;
}

static void mtw_sel_set(int r, int c, BOOL extend) {
  int nr = g_mtDispN, nc = MT_NCOLS;
  if (nr <= 0) return;
  r = r < 0 ? 0 : r >= nr ? nr - 1 : r;
  c = c < 0 ? 0 : c >= nc ? nc - 1 : c;
  g_mtCr = r;
  g_mtCc = c;
  if (!extend || g_mtAr < 0) {
    g_mtAr = r;
    g_mtAc = c;
  }
  ListView_EnsureVisible(g_mtList, r, FALSE);
  RECT cr, lr;
  GetClientRect(g_mtList, &lr);
  if (ListView_GetSubItemRect(g_mtList, r, c, LVIR_BOUNDS, &cr) && c > 0) {
    if (cr.left < 0) SendMessageW(g_mtList, LVM_SCROLL, cr.left, 0);
    else if (cr.right > lr.right) SendMessageW(g_mtList, LVM_SCROLL, cr.right - lr.right < cr.left ? cr.right - lr.right : cr.left, 0);
  }
  InvalidateRect(g_mtList, NULL, FALSE);
}

static void mtw_put_clip(const wchar_t *text, size_t len) {
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
  if (!mem) return;
  memcpy(GlobalLock(mem), text, (len + 1) * sizeof(wchar_t));
  GlobalUnlock(mem);
  if (OpenClipboard(g_mtWnd)) {
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    CloseClipboard();
  } else {
    GlobalFree(mem);
  }
}

static const wchar_t *const kMtHead[MT_NCOLS] = {L"№",           L"Excel: обозначение", L"Excel: наименование",
                                                 L"Союз: обозначение", L"Союз: наименование", L"Совпадение",
                                                 L"Результат",   L"Примечание"};

static void mtw_copy_range(int r0, int r1, int c0, int c1, BOOL withHead) {
  if (r0 < 0 || r1 < r0 || c1 < c0) return;
  size_t cap = 4096, len = 0;
  wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!buf) return;
  wchar_t cell[520];
  for (int r = withHead ? -1 : r0; r <= r1; r++) {
    for (int c = c0; c <= c1; c++) {
      const wchar_t *v = r < 0 ? kMtHead[c] : mtw_cell(r, c, cell);
      if (c == 0 && r >= 0) { /* значок ▸ при копировании не нужен: только номер */
        while (*v == L' ' || *v == L'+' || *v == L'−' || *v == L'└') v++;
      }
      BOOL q = wcspbrk(v, L"\t\r\n\"") != NULL;
      size_t need = len + wcslen(v) * 2 + 8;
      if (need > cap) {
        while (need > cap) cap *= 2;
        wchar_t *nb = (wchar_t *)realloc(buf, cap * sizeof(wchar_t));
        if (!nb) {
          free(buf);
          return;
        }
        buf = nb;
      }
      if (q) buf[len++] = L'"';
      for (const wchar_t *p = v; *p; p++) {
        if (*p == L'"' && q) buf[len++] = L'"';
        buf[len++] = *p;
      }
      if (q) buf[len++] = L'"';
      if (c < c1) buf[len++] = L'\t';
    }
    if (r < r1) {
      buf[len++] = L'\r';
      buf[len++] = L'\n';
    }
  }
  buf[len] = 0;
  mtw_put_clip(buf, len);
  int cells = (r1 - r0 + 1) * (c1 - c0 + 1);
  mtw_status(cells == 1 && !withHead ? L"Ячейка скопирована" : L"Скопировано ячеек: %d — вставьте в Excel", cells);
  free(buf);
}

static void mtw_copy_cmd(int what) {
  int nr = g_mtDispN, nc = MT_NCOLS;
  if (nr <= 0) return;
  int r0, r1, c0, c1;
  if (what == MT_CM_ALL) {
    mtw_copy_range(0, nr - 1, 0, nc - 1, TRUE);
    return;
  }
  if (g_mtCr < 0) return;
  if (what == MT_CM_SEL) mtw_sel_rect(&r0, &r1, &c0, &c1);
  else if (what == MT_CM_ROW) r0 = r1 = g_mtCr, c0 = 0, c1 = nc - 1;
  else if (what == MT_CM_COL) r0 = 0, r1 = nr - 1, c0 = c1 = g_mtCc;
  else r0 = r1 = g_mtCr, c0 = c1 = g_mtCc;
  mtw_copy_range(r0, r1, c0, c1, FALSE);
}

/* раскрыть или свернуть позиции */
static void mtw_toggle(int pos) {
  if (pos < 0 || pos >= g_mtResN || mtw_nshown(&g_mtRes[pos]) < 2) return;
  g_mtOpen[pos] ^= 1;
  mtw_rebuild();
}

static void mtw_expand_all(BOOL open) {
  for (int k = 0; k < g_mtResN; k++) g_mtOpen[k] = open && mtw_nshown(&g_mtRes[k]) > 1;
  mtw_rebuild();
}

static BOOL mtw_hit(LPARAM lp, int *r, int *c) {
  LVHITTESTINFO h;
  memset(&h, 0, sizeof(h));
  h.pt.x = (short)LOWORD(lp);
  h.pt.y = (short)HIWORD(lp);
  if (SendMessageW(g_mtList, LVM_SUBITEMHITTEST, 0, (LPARAM)&h) < 0 || h.iItem < 0 || h.iSubItem < 0) return FALSE;
  *r = h.iItem;
  *c = h.iSubItem;
  return TRUE;
}

static LRESULT CALLBACK MtwListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  int r, c;
  switch (msg) {
  case WM_GETDLGCODE:
    return DLGC_WANTARROWS | DLGC_WANTCHARS;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
    SetFocus(hwnd);
    if (mtw_hit(lParam, &r, &c)) {
      if (msg == WM_LBUTTONDBLCLK || c == 0) {
        mtw_sel_set(r, c, FALSE);
        if (r < g_mtDispN) mtw_toggle(g_mtDisp[r].pos);
      } else {
        mtw_sel_set(r, c, (wParam & MK_SHIFT) != 0);
        g_mtDrag = TRUE;
        SetCapture(hwnd);
      }
    }
    return 0;
  case WM_MOUSEMOVE:
    if (g_mtDrag && (wParam & MK_LBUTTON)) {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      RECT lr;
      GetClientRect(hwnd, &lr);
      if (pt.y < lr.top + MTS(24)) SendMessageW(hwnd, WM_VSCROLL, SB_LINEUP, 0);
      else if (pt.y > lr.bottom - MTS(4)) SendMessageW(hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
      if (mtw_hit(lParam, &r, &c) && (r != g_mtCr || c != g_mtCc)) mtw_sel_set(r, c, TRUE);
      return 0;
    }
    break;
  case WM_LBUTTONUP:
    if (g_mtDrag) {
      g_mtDrag = FALSE;
      ReleaseCapture();
    }
    return 0;
  case WM_CAPTURECHANGED:
    g_mtDrag = FALSE;
    break;
  case WM_RBUTTONDOWN:
    SetFocus(hwnd);
    if (mtw_hit(lParam, &r, &c)) {
      int r0, r1, c0, c1;
      BOOL inside = FALSE;
      if (g_mtCr >= 0) {
        mtw_sel_rect(&r0, &r1, &c0, &c1);
        inside = r >= r0 && r <= r1 && c >= c0 && c <= c1;
      }
      if (!inside) mtw_sel_set(r, c, FALSE);
    }
    return 0;
  case WM_CONTEXTMENU: {
    if (g_mtCr < 0) return 0;
    POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
    if (pt.x == -1 && pt.y == -1) {
      pt.x = pt.y = 0;
      ClientToScreen(hwnd, &pt);
    }
    int r0, r1, c0, c1;
    mtw_sel_rect(&r0, &r1, &c0, &c1);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, MT_CM_CELL, L"Копировать ячейку");
    AppendMenuW(m, MF_STRING, MT_CM_ROW, L"Копировать строку");
    AppendMenuW(m, MF_STRING, MT_CM_COL, L"Копировать столбец");
    if (r1 > r0 || c1 > c0) AppendMenuW(m, MF_STRING, MT_CM_SEL, L"Копировать выделенное\tCtrl+C");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, MT_CM_ALL, L"Копировать всё с заголовками");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
    if (cmd) mtw_copy_cmd(cmd);
    return 0;
  }
  case WM_KEYDOWN: {
    BOOL sh = GetKeyState(VK_SHIFT) < 0, ct = GetKeyState(VK_CONTROL) < 0;
    int nr = g_mtDispN;
    if (ct && (wParam == 'C' || wParam == VK_INSERT)) {
      mtw_copy_cmd(MT_CM_SEL);
      return 0;
    }
    if (ct && wParam == 'A') {
      if (nr > 0) {
        g_mtAr = 0;
        g_mtAc = 0;
        g_mtCr = nr - 1;
        g_mtCc = MT_NCOLS - 1;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }
    if (g_mtCr < 0 && nr > 0 && wParam >= VK_PRIOR && wParam <= VK_DOWN) {
      mtw_sel_set(0, 0, FALSE);
      return 0;
    }
    if ((wParam == VK_SPACE || wParam == VK_RETURN) && g_mtCr >= 0 && g_mtCr < nr) {
      mtw_toggle(g_mtDisp[g_mtCr].pos);
      return 0;
    }
    int page = ListView_GetCountPerPage(hwnd);
    switch (wParam) {
    case VK_UP: mtw_sel_set(g_mtCr - 1, g_mtCc, sh); return 0;
    case VK_DOWN: mtw_sel_set(g_mtCr + 1, g_mtCc, sh); return 0;
    case VK_LEFT: mtw_sel_set(g_mtCr, g_mtCc - 1, sh); return 0;
    case VK_RIGHT: mtw_sel_set(g_mtCr, g_mtCc + 1, sh); return 0;
    case VK_PRIOR: mtw_sel_set(g_mtCr - page, g_mtCc, sh); return 0;
    case VK_NEXT: mtw_sel_set(g_mtCr + page, g_mtCc, sh); return 0;
    case VK_HOME: mtw_sel_set(ct ? 0 : g_mtCr, 0, sh); return 0;
    case VK_END: mtw_sel_set(ct ? nr - 1 : g_mtCr, MT_NCOLS - 1, sh); return 0;
    }
    break;
  }
  case WM_CHAR:
    return 0;
  }
  return CallWindowProcW(g_mtListOld, hwnd, msg, wParam, lParam);
}

/* цвет строки по результату; выбранные ячейки — синие */
static LRESULT mtw_list_draw(NMLVCUSTOMDRAW *cd) {
  switch (cd->nmcd.dwDrawStage) {
  case CDDS_PREPAINT:
    return CDRF_NOTIFYITEMDRAW;
  case CDDS_ITEMPREPAINT:
    return CDRF_NOTIFYSUBITEMDRAW;
  case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
    int r = (int)cd->nmcd.dwItemSpec, c = cd->iSubItem;
    cd->clrTextBk = ListView_GetBkColor(g_mtList);
    cd->clrText = GetSysColor(COLOR_WINDOWTEXT);
    if (r >= 0 && r < g_mtDispN) {
      const MtwDisp d = g_mtDisp[r];
      int cls = mtw_cls(&g_mtRes[d.pos]);
      BOOL child = d.rank > 0;
      /* зелёный, жёлтый, красный; у раскрытых кандидатов бледнее */
      static const COLORREF bg[3] = {RGB(0xFB, 0xDD, 0xDA), RGB(0xFF, 0xF1, 0xBF), RGB(0xD3, 0xEF, 0xD9)};
      static const COLORREF bgc[3] = {RGB(0xFD, 0xEE, 0xEC), RGB(0xFF, 0xF8, 0xDF), RGB(0xE9, 0xF7, 0xEC)};
      cd->clrTextBk = child ? bgc[cls] : bg[cls];
      if (child) cd->clrText = RGB(0x4B, 0x55, 0x63);
      if (c == 6 && !child) {
        static const COLORREF fg[3] = {RGB(0xA3, 0x1D, 0x14), RGB(0x7A, 0x56, 0x00), RGB(0x14, 0x6B, 0x2B)};
        cd->clrText = fg[cls];
      }
    }
    if (g_mtCr >= 0) {
      int r0, r1, c0, c1;
      mtw_sel_rect(&r0, &r1, &c0, &c1);
      if (r >= r0 && r <= r1 && c >= c0 && c <= c1) {
        BOOL cur = r == g_mtCr && c == g_mtCc;
        cd->clrTextBk = cur ? RGB(0x1F, 0x5F, 0xA8) : RGB(0xCF, 0xE3, 0xF8);
        cd->clrText = cur ? RGB(255, 255, 255) : RGB(0, 0, 0);
      }
    }
    return CDRF_NEWFONT;
  }
  }
  return CDRF_DODEFAULT;
}

/* ---- сохранение результата ----------------------------------------------------------- */
#define MT_FLAT_N 12
static const wchar_t *const kMtFlatHead[MT_FLAT_N] = {
    L"№",           L"Excel: обозначение", L"Excel: наименование", L"Союз: обозначение",
    L"Союз: наименование", L"Совпадение, %", L"Результат", L"Примечание",
    L"Кандидат 2",  L"Кандидат 3",         L"Кандидат 4",         L"Кандидат 5"};
static const int kMtFlatW[MT_FLAT_N] = {6, 26, 34, 28, 40, 12, 20, 36, 40, 40, 40, 40};

static const wchar_t *mtw_flat_cell(void *ctx, int k, int c) {
  (void)ctx;
  static wchar_t buf[6][700];
  static int ring;
  wchar_t *b = buf[ring++ % 6];
  b[0] = 0;
  if (k < 0 || k >= g_mtResN) return b;
  const MtwRes *r = &g_mtRes[k];
  int ns = mtw_nshown(r);
  if (c == 0) _snwprintf(b, 20, L"%d", k + 1);
  else if (c == 1) lstrcpynW(b, mtw_xc(k, 0), 690);
  else if (c == 2) lstrcpynW(b, mtw_xc(k, 1), 690);
  else if (c == 3 && ns > 0) lstrcpynW(b, mtw_sc(r->h[0].item, 0), 690);
  else if (c == 4 && ns > 0) lstrcpynW(b, mtw_sc(r->h[0].item, 1), 690);
  else if (c == 5 && ns > 0) _snwprintf(b, 20, L"%d", r->h[0].score);
  else if (c == 6) {
    int cl = mtw_cls(r);
    lstrcpynW(b, cl == 2 ? L"Точное совпадение" : cl == 1 ? L"Вероятное" : L"Ничего похожего", 40);
  } else if (c == 7 && ns > 0) mt_note(&r->h[0], b, 300);
  else if (c >= 8 && c - 7 < ns) {
    const MtHit *h = &r->h[c - 7];
    _snwprintf(b, 690, L"%ls — %ls (%d%%)", mtw_sc(h->item, 0), mtw_sc(h->item, 1), h->score);
    b[689] = 0;
  }
  return b;
}

static void mtw_csv_put(FILE *f, const wchar_t *v, BOOL last) {
  BOOL q = wcspbrk(v, L";\"\r\n") != NULL;
  wchar_t tmp[1500];
  size_t l = 0;
  if (q) tmp[l++] = L'"';
  for (; *v && l < 1490; v++) {
    if (*v == L'"') tmp[l++] = L'"';
    tmp[l++] = (*v == L'\r' || *v == L'\n') ? L' ' : *v;
  }
  if (q) tmp[l++] = L'"';
  tmp[l++] = last ? L'\n' : L';';
  tmp[l] = 0;
  char u[4500];
  int n = WideCharToMultiByte(CP_UTF8, 0, tmp, (int)l, u, sizeof(u), NULL, NULL);
  if (n > 0) fwrite(u, 1, (size_t)n, f);
}

static BOOL mtw_write(const wchar_t *file) {
  size_t fl = wcslen(file);
  BOOL csv = fl > 4 && !_wcsicmp(file + fl - 4, L".csv");
  BOOL ok = FALSE;
  SYSTEMTIME st;
  GetLocalTime(&st);
  if (csv) {
    FILE *f = _wfopen(file, L"wb");
    if (f) {
      fwrite("\xEF\xBB\xBF", 1, 3, f);
      for (int c = 0; c < MT_FLAT_N; c++) mtw_csv_put(f, kMtFlatHead[c], c == MT_FLAT_N - 1);
      for (int k = 0; k < g_mtResN; k++)
        for (int c = 0; c < MT_FLAT_N; c++) mtw_csv_put(f, mtw_flat_cell(NULL, k, c), c == MT_FLAT_N - 1);
      ok = fclose(f) == 0;
    }
  } else {
    XlSheet sh;
    memset(&sh, 0, sizeof(sh));
    sh.ncols = MT_FLAT_N;
    sh.head = kMtFlatHead;
    sh.width = kMtFlatW;
    sh.plain = 1;
    XlBookSheet bs;
    memset(&bs, 0, sizeof(bs));
    wchar_t title[200];
    _snwprintf(title, 200, L"Сопоставление позиций Excel с Союзом · %02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);
    title[199] = 0;
    bs.name = L"Сопоставление";
    bs.title = title;
    bs.sh = &sh;
    bs.nrows = g_mtResN;
    bs.cell = mtw_flat_cell;
    ok = xl_save_book(file, &bs, 1);
  }
  return ok;
}

static void mtw_save(void) {
  if (g_mtResN <= 0) {
    mtw_status(L"Сначала «Сопоставить»");
    return;
  }
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t file[MAX_PATH];
  _snwprintf(file, MAX_PATH, L"Сопоставление с Союзом от %02d.%02d.%02d.xlsx", st.wDay, st.wMonth, st.wYear % 100);
  file[MAX_PATH - 1] = 0;
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_mtWnd;
  of.lpstrFilter = L"Книга Excel (*.xlsx)\0*.xlsx\0Таблица CSV (*.csv)\0*.csv\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"xlsx";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  BOOL ok = mtw_write(file);
  if (!ok) {
    MessageBoxW(g_mtWnd, L"Не удалось записать файл — он не открыт сейчас в Excel?", L"Сопоставить с Союзом",
                MB_ICONWARNING);
    return;
  }
  mtw_status(L"Сохранено: %s", file);
  ShellExecuteW(NULL, L"open", file, NULL, NULL, SW_SHOWNORMAL);
}

/* «Правила…»: файл с дополнениями в папке данных; основные правила — в самой программе */
static void mtw_open_rules(void) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%s\\match_rules.txt", g_dataDir);
  path[MAX_PATH - 1] = 0;
  if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
    static const char tpl[] =
        "\xEF\xBB\xBF# Ваши дополнения к правилам сопоставления. Основные правила зашиты в программу;\r\n"
        "# то, что записано здесь, добавляется к ним и важнее. Ниже — примеры (уберите # в начале строки).\r\n"
        "# Каждая строка: основное слово = вариант, вариант…  Регистр, точки и латиница вместо русских не важны.\r\n"
        "# Файл читается при каждом «Сопоставить».\r\n\r\n"
        "[синонимы]\r\n# втулка = вт, втулочка\r\n# корпус = корп\r\n\r\n"
        "[игнорировать]\r\n# слова, которые не считаются при сравнении:\r\n# изделие\r\n\r\n"
        "[пороги]\r\n# точно = 97\r\n# вероятно = 60\r\n# показывать = 35\r\n";
    FILE *f = _wfopen(path, L"wb");
    if (f) {
      fwrite(tpl, 1, sizeof(tpl) - 1, f);
      fclose(f);
    }
  }
  ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
  mtw_status(L"Правила открыты в блокноте; изменения подхватятся при следующем «Сопоставить»");
}

/* ---- окно ----------------------------------------------------------------------------- */
static void mtw_layout(void) {
  RECT rc;
  GetClientRect(g_mtWnd, &rc);
  int pad = MTS(14), h = MTS(26), y1 = PANEL_TITLE_H + MTS(12), y2 = y1 + MTS(50), y3 = y2 + MTS(54);
  place_panel_close(g_mtWnd);
  int bx = pad + MTS(124);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_XPASTE), bx, y1, MTS(168), h, TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_XFILE), bx + MTS(174), y1, MTS(116), h, TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_SFILE), bx, y2, MTS(100), h, TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_SPASTE), bx + MTS(106), y2, MTS(100), h, TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_SPLM), bx + MTS(212), y2, MTS(150), h, TRUE);
  int cx = pad + MTS(124) + MTS(380);
  for (int b = 0; b < 2; b++) {
    int y = b ? y2 : y1;
    MoveWindow(g_mtCb[b * 2], cx + MTS(82), y, MTS(130), MTS(300), TRUE);
    MoveWindow(g_mtCb[b * 2 + 1], cx + MTS(82) + MTS(130) + MTS(96), y, MTS(130), MTS(300), TRUE);
    MoveWindow(g_mtChk[b], cx + MTS(82) + MTS(130) + MTS(96) + MTS(138), y + MTS(2), MTS(130), MTS(22), TRUE);
  }
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_RUN), pad, y3, MTS(130), h + MTS(2), TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_EXPAND), pad + MTS(140), y3, MTS(130), h + MTS(2), TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_COLLAPSE), pad + MTS(276), y3, MTS(110), h + MTS(2), TRUE);
  MoveWindow(g_mtFilter, pad + MTS(396), y3 + MTS(1), MTS(190), MTS(300), TRUE);
  int by = rc.bottom - pad - MTS(30), ly = y3 + MTS(44);
  MoveWindow(g_mtList, pad, ly, rc.right - pad * 2, by - MTS(10) - ly, TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_COPY), pad, by, MTS(190), MTS(30), TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_SAVE), pad + MTS(200), by, MTS(190), MTS(30), TRUE);
  MoveWindow(GetDlgItem(g_mtWnd, ID_MT_RULES), pad + MTS(400), by, MTS(120), MTS(30), TRUE);
}

static void mtw_tab_line(const MtwTab *t, const wchar_t *src, wchar_t *out, int cap) {
  if (t->n <= 0) {
    lstrcpynW(out, L"не загружено", cap);
    return;
  }
  const wchar_t *nm = src && *src ? wcsrchr(src, L'\\') : NULL;
  nm = nm ? nm + 1 : (src && *src ? src : NULL);
  _snwprintf(out, (size_t)cap, L"%d строк%ls%ls", t->n - mtw_first(t), nm ? L" · " : L"", nm ? nm : L"");
  out[cap - 1] = 0;
}

static void mtw_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Сопоставить с Союзом");
  SetBkMode(hdc, TRANSPARENT);
  int pad = MTS(14), y1 = PANEL_TITLE_H + MTS(12), y2 = y1 + MTS(50), y3 = y2 + MTS(54);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  RECT a = {pad, y1, pad + MTS(120), y1 + MTS(26)};
  DrawTextW(hdc, L"Позиции Excel", -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  RECT b = {pad, y2, pad + MTS(120), y2 + MTS(26)};
  DrawTextW(hdc, L"Данные Союза", -1, &b, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  int cx = pad + MTS(124) + MTS(380);
  for (int i = 0; i < 2; i++) {
    int y = i ? y2 : y1;
    RECT l1 = {cx, y, cx + MTS(80), y + MTS(26)};
    DrawTextW(hdc, L"обозначение", -1, &l1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT l2 = {cx + MTS(82) + MTS(130) + MTS(4), y, cx + MTS(82) + MTS(130) + MTS(96), y + MTS(26)};
    DrawTextW(hdc, L"наименование", -1, &l2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  wchar_t s[MAX_PATH + 60];
  mtw_tab_line(&g_mtX, g_mtXSrc, s, MAX_PATH + 60);
  RECT i1 = {pad + MTS(124), y1 + MTS(28), rc.right - pad, y1 + MTS(46)};
  DrawTextW(hdc, s, -1, &i1, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (!g_mtS2.n || g_mtSSrc[0]) mtw_tab_line(&g_mtS2, g_mtSSrc, s, MAX_PATH + 60);
  else _snwprintf(s, MAX_PATH + 60, L"%d строк · из «Выгрузки из PLM» или буфера", g_mtS2.n - mtw_first(&g_mtS2));
  RECT i2 = {pad + MTS(124), y2 + MTS(28), rc.right - pad, y2 + MTS(46)};
  DrawTextW(hdc, s, -1, &i2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  /* итог и состояние справа от кнопок */
  RECT st = {pad + MTS(600), y3, rc.right - pad, y3 + MTS(28)};
  if (g_mtResN > 0 && !g_mtBusy) {
    wchar_t t[160];
    _snwprintf(t, 160, L"Точных %d · вероятных %d · ничего похожего %d", g_mtCnt[2], g_mtCnt[1], g_mtCnt[0]);
    t[159] = 0;
    SetTextColor(hdc, COL_INK);
    DrawTextW(hdc, t, -1, &st, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  RECT st2 = {pad, y3 + MTS(30), rc.right - pad, y3 + MTS(46)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, g_mtBusy ? COL_SAGE : COL_MUTED);
  DrawTextW(hdc,
            g_mtStatus[0] ? g_mtStatus
                          : L"Скопируйте ячейки в Excel и нажмите «Вставить из буфера»; данные Союза — CSV выгрузки "
                            L"материалов. В Союз ничего не записывается.",
            -1, &st2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (g_mtBusy && g_mtTotal > 0) { /* полоса хода */
    RECT tr = {pad, y3 + MTS(44), rc.right - pad, y3 + MTS(48)};
    HBRUSH bt = CreateSolidBrush(COL_LINE);
    FillRect(hdc, &tr, bt);
    DeleteObject(bt);
    RECT fr = tr;
    fr.right = tr.left + (int)((long long)(tr.right - tr.left) * g_mtDone / g_mtTotal);
    HBRUSH bf = CreateSolidBrush(COL_SAGE);
    if (fr.right > fr.left) FillRect(hdc, &fr, bf);
    DeleteObject(bf);
  }
}

static LRESULT CALLBACK MtwProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    mtw_paint(hwnd, mem);
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
    mm->ptMinTrackSize.x = MTS(1060);
    mm->ptMinTrackSize.y = MTS(420);
    return 0;
  }
  case WM_SIZE:
    mtw_layout();
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_NOTIFY: {
    NMHDR *nh = (NMHDR *)lParam;
    if (nh->hwndFrom == g_mtList) {
      if (nh->code == NM_CUSTOMDRAW) return mtw_list_draw((NMLVCUSTOMDRAW *)lParam);
      if (nh->code == LVN_GETDISPINFOW) {
        NMLVDISPINFOW *di = (NMLVDISPINFOW *)lParam;
        if (di->item.mask & LVIF_TEXT) {
          wchar_t cell[520];
          mtw_cell(di->item.iItem, di->item.iSubItem, cell);
          lstrcpynW(di->item.pszText, cell, di->item.cchTextMax);
        }
        return 0;
      }
    }
    if (nh->hwndFrom == ListView_GetHeader(g_mtList) && (nh->code == HDN_ITEMCLICKW || nh->code == HDN_ITEMCLICKA)) {
      if (g_mtDispN > 0) { /* клик по заголовку — весь столбец */
        g_mtAr = 0;
        g_mtAc = g_mtCc = ((NMHEADERW *)lParam)->iItem;
        g_mtCr = g_mtDispN - 1;
        InvalidateRect(g_mtList, NULL, FALSE);
        SetFocus(g_mtList);
      }
    }
    break;
  }
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)bg_brush(FALSE);
  }
  case WM_TIMER:
    if (wParam == ID_MT_TIMER) {
      if (g_mtTotal > 0)
        mtw_status(L"Сопоставляю: %ld из %ld позиций…", (long)g_mtDone, (long)g_mtTotal);
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_MT_DONE: {
    KillTimer(hwnd, ID_MT_TIMER);
    g_mtBusy = 0;
    SetWindowTextW(GetDlgItem(hwnd, ID_MT_RUN), L"Сопоставить");
    mtw_rebuild();
    mtw_status(L"Готово: позиций %d, данные Союза — %d. Нажмите + у строки (или Пробел), чтобы увидеть остальных кандидатов",
               g_mtResN, g_mtIxN);
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam), code = HIWORD(wParam);
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (g_mtBusy) return 0;
    if (id == ID_MT_XPASTE) mtw_load_text(FALSE, mtw_clip_text(), L"");
    if (id == ID_MT_XFILE) mtw_open_file(FALSE);
    if (id == ID_MT_SFILE) mtw_open_file(TRUE);
    if (id == ID_MT_SPASTE) mtw_load_text(TRUE, mtw_clip_text(), L"");
    if (id == ID_MT_SPLM) mtw_from_plm();
    if (id == ID_MT_RUN) mtw_run();
    if (id == ID_MT_EXPAND) mtw_expand_all(TRUE);
    if (id == ID_MT_COLLAPSE) mtw_expand_all(FALSE);
    if (id == ID_MT_COPY) mtw_copy_cmd(MT_CM_ALL);
    if (id == ID_MT_SAVE) mtw_save();
    if (id == ID_MT_RULES) mtw_open_rules();
    if (id == ID_MT_FILTER && code == CBN_SELCHANGE) {
      mtw_sel_clear();
      mtw_rebuild();
    }
    if (id >= ID_MT_XD && id <= ID_MT_SN && code == CBN_SELCHANGE) {
      g_mtCol[id - ID_MT_XD] = (int)SendMessageW(g_mtCb[id - ID_MT_XD], CB_GETCURSEL, 0, 0) - 1;
      mtw_res_free();
      mtw_sel_clear();
      mtw_rebuild();
      if (id >= ID_MT_SD) mtw_prefs_save();
    }
    if ((id == ID_MT_XHDR || id == ID_MT_SHDR) && code == BN_CLICKED) {
      BOOL s = id == ID_MT_SHDR;
      MtwTab *t = s ? &g_mtS2 : &g_mtX;
      t->hdr = SendMessageW(g_mtChk[s], BM_GETCHECK, 0, 0) == BST_CHECKED;
      mtw_fill_combo(g_mtCb[s ? 2 : 0], t, g_mtCol[s ? 2 : 0]);
      mtw_fill_combo(g_mtCb[s ? 3 : 1], t, g_mtCol[s ? 3 : 1]);
      mtw_res_free();
      mtw_sel_clear();
      mtw_rebuild();
      mtw_info();
    }
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* прошлый файл данных Союза и столбцы — из match.txt */
static void mtw_prefs_load(void) {
  if (!g_dataDir[0] || g_mtS2.n) return;
  wchar_t p[MAX_PATH];
  mtw_prefs_path(p);
  wchar_t *t = mtw_read_file(p);
  if (!t) return;
  wchar_t path[MAX_PATH] = L"";
  int sd = -2, sn = -2;
  for (wchar_t *ln = t; ln && *ln;) {
    wchar_t *e = wcschr(ln, L'\n');
    if (e) *e = 0;
    wchar_t *tab = wcschr(ln, L'\t');
    if (tab) {
      *tab = 0;
      wchar_t *v = tab + 1;
      size_t vl = wcslen(v);
      while (vl && (v[vl - 1] == L'\r')) v[--vl] = 0;
      if (!wcscmp(ln, L"soyuz")) lstrcpynW(path, v, MAX_PATH);
      else if (!wcscmp(ln, L"sd")) sd = (int)wcstol(v, NULL, 10);
      else if (!wcscmp(ln, L"sn")) sn = (int)wcstol(v, NULL, 10);
    }
    ln = e ? e + 1 : NULL;
  }
  free(t);
  if (!path[0]) return;
  wchar_t *txt = mtw_read_file(path);
  if (!txt || !mtw_parse(&g_mtS2, txt)) {
    free(txt);
    return;
  }
  free(txt);
  lstrcpynW(g_mtSSrc, path, MAX_PATH);
  mtw_after_load(TRUE);
  if (sd >= -1 && sd < g_mtS2.nc && sn >= -1 && sn < g_mtS2.nc && (sd >= 0 || sn >= 0)) {
    g_mtCol[2] = sd;
    g_mtCol[3] = sn;
    mtw_fill_combo(g_mtCb[2], &g_mtS2, sd);
    mtw_fill_combo(g_mtCb[3], &g_mtS2, sn);
  }
  mtw_status(L"Данные Союза с прошлого раза: %d строк. Вставьте позиции из Excel", g_mtS2.n - mtw_first(&g_mtS2));
}

static void match_show(void) {
  if (!g_mtWnd) {
    HDC s = GetDC(NULL);
    g_mtS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MtwProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadMatch";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = MTS(1260), h = MTS(700);
    if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
    if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
    g_mtWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadMatch", L"Сопоставить с Союзом — CursorPad",
                              WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                              wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h, NULL,
                              NULL, g_inst, NULL);
    if (!g_mtWnd) return;
    round_corners(g_mtWnd);
    mk_btn(g_mtWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_mtWnd, L"–", ID_PANEL_MIN);
    mk_btn(g_mtWnd, L"Вставить из буфера (Excel)", ID_MT_XPASTE);
    mk_btn(g_mtWnd, L"Открыть файл…", ID_MT_XFILE);
    mk_btn(g_mtWnd, L"Файл CSV…", ID_MT_SFILE);
    mk_btn(g_mtWnd, L"Из буфера", ID_MT_SPASTE);
    mk_btn(g_mtWnd, L"Из выгрузки PLM", ID_MT_SPLM);
    for (int i = 0; i < 4; i++) {
      g_mtCb[i] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                  0, 0, 10, 300, g_mtWnd, (HMENU)(INT_PTR)(ID_MT_XD + i), g_inst, NULL);
      if (g_fontUi) SendMessageW(g_mtCb[i], WM_SETFONT, (WPARAM)g_fontUi, FALSE);
      SendMessageW(g_mtCb[i], CB_ADDSTRING, 0, (LPARAM)L"— нет —");
      SendMessageW(g_mtCb[i], CB_SETCURSEL, 0, 0);
    }
    for (int i = 0; i < 2; i++) {
      g_mtChk[i] = CreateWindowExW(0, L"BUTTON", L"есть заголовок", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                   0, 0, 10, 10, g_mtWnd, (HMENU)(INT_PTR)(ID_MT_XHDR + i), g_inst, NULL);
      if (g_fontUi) SendMessageW(g_mtChk[i], WM_SETFONT, (WPARAM)g_fontUi, FALSE);
    }
    mk_btn(g_mtWnd, L"Сопоставить", ID_MT_RUN);
    mk_btn(g_mtWnd, L"Раскрыть все", ID_MT_EXPAND);
    mk_btn(g_mtWnd, L"Свернуть", ID_MT_COLLAPSE);
    g_mtFilter = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 0, 0, 10, 300, g_mtWnd, (HMENU)(INT_PTR)ID_MT_FILTER, g_inst, NULL);
    if (g_fontUi) SendMessageW(g_mtFilter, WM_SETFONT, (WPARAM)g_fontUi, FALSE);
    static const wchar_t *const flt[] = {L"Показать все", L"Только точные", L"Только вероятные", L"Только «ничего похожего»"};
    for (int i = 0; i < 4; i++) SendMessageW(g_mtFilter, CB_ADDSTRING, 0, (LPARAM)flt[i]);
    SendMessageW(g_mtFilter, CB_SETCURSEL, 0, 0);
    mk_btn(g_mtWnd, L"Копировать всё (в Excel)", ID_MT_COPY);
    mk_btn(g_mtWnd, L"Сохранить в Excel или CSV…", ID_MT_SAVE);
    mk_btn(g_mtWnd, L"Правила…", ID_MT_RULES);
    g_mtList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                               0, 0, 10, 10, g_mtWnd, NULL, g_inst, NULL);
    SendMessageW(g_mtList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    g_mtListOld = (WNDPROC)SetWindowLongPtrW(g_mtList, GWLP_WNDPROC, (LONG_PTR)MtwListProc);
    if (g_fontBody) SendMessageW(g_mtList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    static const int cw[MT_NCOLS] = {50, 150, 180, 170, 220, 80, 130, 240};
    for (int c = 0; c < MT_NCOLS; c++) {
      LVCOLUMNW col;
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      col.pszText = (wchar_t *)kMtHead[c];
      col.cx = MTS(cw[c]);
      col.iSubItem = c;
      SendMessageW(g_mtList, LVM_INSERTCOLUMNW, (WPARAM)c, (LPARAM)&col);
    }
    mtw_layout();
    mtw_prefs_load();
  }
  ShowWindow(g_mtWnd, IsIconic(g_mtWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_mtWnd);
}

#ifdef SET_TEST /* только проверочная сборка: --mttest позиции.tsv союз.tsv — окно с готовым сопоставлением, для снимка экрана */
static void match_test(const wchar_t *xf, const wchar_t *sf, const wchar_t *out) {
  match_show();
  wchar_t *tx = mtw_read_file(xf), *ts = mtw_read_file(sf);
  mtw_load_text(FALSE, tx, xf);
  mtw_load_text(TRUE, ts, sf);
  mtw_match_all();
  mtw_rebuild();
  int opened = 0;
  for (int k = 0; k < g_mtResN && opened < 3; k++) /* раскрыть несколько неоднозначных — для снимка */
    if (mtw_cls(&g_mtRes[k]) == 1 && mtw_nshown(&g_mtRes[k]) > 1) g_mtOpen[k] = 1, opened++;
  mtw_rebuild();
  mtw_status(L"Готово: позиций %d, данные Союза — %d. Нажмите + у строки (или Пробел), чтобы увидеть остальных кандидатов",
             g_mtResN, g_mtIxN);
  InvalidateRect(g_mtWnd, NULL, FALSE);
  if (out) { /* проверка: копирование всего в буфер, CSV и xlsx в файлы рядом */
    wchar_t p[MAX_PATH];
    mtw_copy_cmd(MT_CM_ALL);
    if (OpenClipboard(NULL)) {
      HANDLE h = GetClipboardData(CF_UNICODETEXT);
      _snwprintf(p, MAX_PATH, L"%s.clip.txt", out);
      FILE *f = _wfopen(p, L"wb");
      if (h && f) fwrite(GlobalLock(h), 2, wcslen((wchar_t *)GlobalLock(h)), f);
      if (f) fclose(f);
      CloseClipboard();
    }
    _snwprintf(p, MAX_PATH, L"%s.csv", out);
    mtw_write(p);
    _snwprintf(p, MAX_PATH, L"%s.xlsx", out);
    mtw_write(p);
  }
}
#endif
