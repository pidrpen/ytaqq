/* pk_xlsx.c — книга Excel → листы окна «ЭСИ и ТП» (с 2026.11.06, John: «форму Excel закину в ЭСИ и ТП —
   и всё проставилось само»). Книгу читает xlsx_read.c, здесь — куда что ложится:
     • лист с шапкой пакета (pk_sheets.h: «Обозначение», «Наименование», «Вид изделия»…; «Сборка», «Входящее»…;
       «Операция», «№ опер.»…; «Деталь», «Материал заготовки»…) — столбец в столбец, в любом порядке, все
       до одного (и профессия, и признак покупки, и файл чертежа); лишние столбцы — мимо. Так читается и
       шаблон (make_esi_template.py, «Ещё ▾ → Шаблон Excel»), и «Сохранить пакет .xlsx»;
     • любой другой лист — как «Вставить» (pk_paste.c): спецификация, маршрутная карта, своя таблица с шапкой;
     • скрытые листы и «Как заполнять», «Списки», «Инструкция» — мимо.
   Удобства шаблона: пустая сборка в «Составе» и пустая деталь в «Операциях» и «Заготовках» — как строкой
   выше; пустой № операции — следующий через 5; «Токарная» и «4110» → «4110 Токарная»; «Деталь» → «Детали»;
   «Ø40x265» → «D=40;L=265»; у стандартного изделия без обозначения обозначение — наименование; файл
   чертежа без папки — рядом с книгой.
   Подключается после pk_paste.c; строки отдаёт put (pk_xlsx_ui.c кладёт их в окно, стенд — в текст).
   Windows не нужен: gcc -O2 -Wall -Werror -o pk_xlsx_test pk_xlsx_test.c -lm && ./pk_xlsx_test */

#define PKX_COLS 8 /* столбцов у самого широкого листа пакета — «Операции» */

/* строка в лист sheet: cells — столбцы листа по порядку (kPkSheets[sheet].ncols), пустое — L"" */
typedef void (*PkxPut)(void *ctx, int sheet, const wchar_t *const *cells, int n);

typedef struct {
  int rows[PK_NT];     /* строк по листам */
  int sheets;          /* листов с данными */
  int skipped;         /* строк без главного (обозначения, входящего, операции) — мимо */
  wchar_t used[400];   /* «Изделия, Состав (по шаблону); Лист1 (спецификация)» */
  wchar_t passed[200]; /* листы мимо: служебные, скрытые, пустые, непонятые */
} PkxOut;

static void pkx_cat(wchar_t *d, int cap, const wchar_t *s) {
  size_t l = wcslen(d);
  for (size_t k = 0; s[k] && l < (size_t)cap - 1; k++) d[l++] = s[k];
  d[l] = 0;
}

/* шапку — к сравнению: строчные, ё → е, без пробелов по краям и точки в конце; до запятой или скобки, если
   до них есть слово: «Масса, кг» и «Масса (кг)» → «масса», «№ опер.» → «№ опер» */
static void pkx_key(const wchar_t *s, wchar_t *out, int cap) {
  wchar_t t[120];
  so_low(s, t, 120); /* soyuz_ops.c: строчные, ё → е, пробелы сжаты */
  int n = 0;
  for (; t[n] && t[n] != L',' && t[n] != L'(' && n < cap - 1; n++) out[n] = t[n];
  while (n && (out[n - 1] == L' ' || out[n - 1] == L'.' || out[n - 1] == L':')) n--;
  out[n] = 0;
  if (!n) tn_cpy(out, cap, t);
}

/* служебный лист шаблона или книги — мимо */
static int pkx_service(const wchar_t *name) {
  wchar_t k[120];
  so_low(name, k, 120);
  static const wchar_t *const kSkip[] = {L"как запол", L"инструкц", L"справк", L"списки", L"список значений",
                                         L"readme",   L"подсказ",  L"пояснен"};
  for (size_t i = 0; i < sizeof(kSkip) / sizeof(kSkip[0]); i++)
    if (pp_pre(k, kSkip[i])) return 1;
  return 0;
}

/* столбцы листа k по шапке в строке r: col[h] — столбец книги для столбца h листа (-1 — нет). Узнано —
   сколько столбцов; главные (обозначение и наименование или вид; сборка и входящее; операция; деталь и
   материал заготовки) обязательны, иначе 0 */
static int pkx_head(const XrSheet *s, int r, int k, int *col) {
  const XpSheetDef *d = &kPkSheets[k];
  int found = 0;
  for (int h = 0; h < d->ncols; h++) {
    col[h] = -1;
    wchar_t want[120];
    pkx_key(d->head[h], want, 120);
    for (int c = 0; c < s->ncols && col[h] < 0; c++) {
      const wchar_t *v = xr_at(s, r, c);
      if (!v[0] || wcslen(v) > 60) continue;
      wchar_t got[120];
      pkx_key(v, got, 120);
      if (!wcscmp(got, want)) col[h] = c;
    }
    found += col[h] >= 0;
  }
  if (k == PK_ITEMS) /* «Кол.», «Поз.», «Сборка» — это спецификация: её разберёт «Вставить» (виды по разделам, состав) */
    for (int c = 0; c < s->ncols; c++) {
      int hours, f = xr_at(s, r, c)[0] ? pp_field(xr_at(s, r, c), &hours) : PF_NONE;
      if (f == PF_QTY || f == PF_POS || f == PF_PARENT) return 0;
    }
  int ok = k == PK_ITEMS    ? col[0] >= 0 && (col[1] >= 0 || col[2] >= 0)
           : k == PK_LINKS  ? col[0] >= 0 && col[1] >= 0
           : k == PK_OPS    ? col[2] >= 0 && (col[0] >= 0 || col[1] >= 0)
                            : col[0] >= 0 && col[1] >= 0;
  return ok ? found : 0;
}

/* лист пакета (тот, что по имени листа, — вперёд) и строка шапки в первых 10; -1 — не шаблон */
static int pkx_which(const XrSheet *s, int *hrow, int *col) {
  wchar_t name[120];
  so_low(s->name, name, 120);
  int byName = -1;
  for (int k = 0; k < PK_NT && byName < 0; k++) {
    wchar_t n[60];
    so_low(kPkSheets[k].name, n, 60);
    if (pp_pre(name, n)) byName = k;
  }
  int best = -1, bestScore = 0, tmp[PKX_COLS];
  for (int r = 0; r < s->nrows && r < 10 && best < 0; r++)
    for (int k = 0; k < PK_NT; k++) {
      int sc = pkx_head(s, r, k, tmp);
      if (sc && k == byName) sc += 100;
      if (sc > bestScore) {
        bestScore = sc, best = k, *hrow = r;
        memcpy(col, tmp, sizeof(tmp));
      }
    }
  return best;
}

/* файл чертежа без папки — рядом с книгой */
static void pkx_file(const wchar_t *v, const wchar_t *dir, wchar_t *out, int cap) {
  int abs = (v[0] && v[1] == L':') || (v[0] == L'\\' && v[1] == L'\\') || v[0] == L'/' || v[0] == L'\\';
  if (!v[0] || abs || !dir || !dir[0]) {
    tn_cpy(out, cap, v);
    return;
  }
  tn_cpy(out, cap, dir);
  size_t l = wcslen(out);
  if (l && out[l - 1] != L'\\' && out[l - 1] != L'/') tn_cat(out, cap, L"\\");
  tn_cat(out, cap, v);
}

/* лист по шаблону: строки с шапки hrow вниз; sel — выбранная в окне позиция: сборка состава, деталь операций
   и заготовок, пока их нет в строках */
static int pkx_sheet_tpl(const XrSheet *s, int k, int hrow, const int *col, const wchar_t *dir, const wchar_t *sel,
                         PkxPut put, void *ctx, PkxOut *o) {
  const XpSheetDef *d = &kPkSheets[k];
  wchar_t v[PKX_COLS][PP_CELL];
  wchar_t prevKey[PP_CELL] = L"", lastPart[PP_CELL] = L"";
  tn_cpy(prevKey, PP_CELL, sel ? sel : L"");
  long lastNum = 0;
  int n = 0;
  for (int r = hrow + 1; r < s->nrows; r++) {
    int any = 0;
    for (int h = 0; h < d->ncols; h++) {
      tn_cpy(v[h], PP_CELL, col[h] >= 0 ? xr_at(s, r, col[h]) : L"");
      any |= v[h][0] != 0;
    }
    if (!any) continue;
    if (k == PK_ITEMS) {
      if (!v[0][0] && v[1][0]) { /* стандартное и прочее — обозначение = наименование (как pp_spec_row) */
        wchar_t kind[80];
        pp_kind(v[2], kind, 80);
        if (pp_kind_noname(kind) || pp_std_name(v[1])) tn_cpy(v[0], PP_CELL, v[1]), v[1][0] = 0;
      }
      if (!v[0][0]) {
        o->skipped++;
        continue;
      }
      wchar_t kind[PP_CELL];
      if (v[2][0]) pp_kind(v[2], kind, PP_CELL), tn_cpy(v[2], PP_CELL, kind);
      if (v[5][0]) {
        wchar_t f[PP_CELL];
        pkx_file(v[5], dir, f, PP_CELL);
        tn_cpy(v[5], PP_CELL, f);
      }
    } else if (k == PK_LINKS || k == PK_BLANKS) {
      if (!v[0][0]) tn_cpy(v[0], PP_CELL, prevKey); /* сборка (деталь) — как строкой выше */
      if (!v[0][0] || !v[1][0]) {
        o->skipped++;
        continue;
      }
      tn_cpy(prevKey, PP_CELL, v[0]);
      if (k == PK_BLANKS && v[3][0] && !pp_sizes_ok(v[3])) {
        wchar_t sz[PP_CELL];
        if (pp_sizes(v[3], sz, PP_CELL)) tn_cpy(v[3], PP_CELL, sz);
      }
    } else { /* PK_OPS */
      if (!v[0][0]) tn_cpy(v[0], PP_CELL, prevKey);
      if (!v[2][0] || !v[0][0]) {
        o->skipped++;
        continue;
      }
      tn_cpy(prevKey, PP_CELL, v[0]);
      if (wcscmp(v[0], lastPart)) lastNum = 0, tn_cpy(lastPart, PP_CELL, v[0]);
      long num = 0;
      int dg = 0;
      const wchar_t *p = v[1];
      while (*p == L' ') p++;
      while (tn_isdig(p[dg]) && dg < 5) num = num * 10 + (p[dg] - L'0'), dg++;
      if (!dg || dg > 4 || p[dg]) num = lastNum + 5; /* пусто или не номер — следующий */
      lastNum = num;
      pp_num3(num, v[1], PP_CELL);
      wchar_t op[PP_CELL];
      pp_op_text(v[2], NULL, op, PP_CELL); /* «Токарная», «4110» → «4110 Токарная» */
      if (op[0]) tn_cpy(v[2], PP_CELL, op);
    }
    const wchar_t *cells[PKX_COLS];
    for (int h = 0; h < PKX_COLS; h++) cells[h] = h < d->ncols ? v[h] : L"";
    put(ctx, k, cells, d->ncols);
    o->rows[k]++;
    n++;
  }
  return n;
}

/* лист не по шаблону — как «Вставить»: строки pp_parse (по PP_COLS столбцов) — в put */
typedef struct {
  PkxPut put;
  void *ctx;
} PkxPp;

static void pkx_pp_put(void *ctx, int sheet, const wchar_t *const *c) {
  PkxPp *x = (PkxPp *)ctx;
  const wchar_t *cells[PKX_COLS];
  for (int h = 0; h < PKX_COLS; h++) cells[h] = h < PP_COLS ? c[h] : L"";
  x->put(x->ctx, sheet, cells, PP_COLS);
}

/* Книга → строки листов. dir — папка книги (файлы чертежей без папки), asmb и part — выбранная в окне
   позиция (как у «Вставить»: состав и операции без сборки и детали). Сколько строк легло */
static int pkx_apply(const XrBook *b, const wchar_t *dir, const wchar_t *asmb, const wchar_t *part, PkxPut put, void *ctx,
                     PkxOut *o) {
  memset(o, 0, sizeof(*o));
  int total = 0;
  for (int i = 0; i < b->n; i++) {
    const XrSheet *s = &b->sh[i];
    wchar_t note[160];
    if (!s->nrows || s->hidden || pkx_service(s->name)) continue; /* пустые, скрытые, «Как заполнять» — молча */
    int hrow = 0, col[PKX_COLS];
    int k = pkx_which(s, &hrow, col);
    int n = 0;
    if (k >= 0) {
      n = pkx_sheet_tpl(s, k, hrow, col, dir, k == PK_LINKS ? asmb : part, put, ctx, o);
      note[0] = 0;
      if (wcscmp(s->name, kPkSheets[k].name)) {
        tn_cpy(note, 160, s->name);
        tn_cat(note, 160, L" → ");
      }
      tn_cat(note, 160, kPkSheets[k].name);
    } else {
      wchar_t *t = xr_tsv(s);
      if (t) {
        int hint = PK_ITEMS;
        wchar_t name[120];
        so_low(s->name, name, 120);
        for (int q = 0; q < PK_NT; q++) {
          wchar_t nm[60];
          so_low(kPkSheets[q].name, nm, 60);
          if (pp_pre(name, nm)) hint = q;
        }
        PkxPp x = {put, ctx};
        PpIn in = {hint, asmb ? asmb : L"", part ? part : L"", pkx_pp_put, &x};
        PpOut po;
        n = pp_parse(t, &in, &po);
        for (int q = 0; q < PK_NT; q++) o->rows[q] += po.rows[q];
        free(t);
        static const wchar_t *const kHow[] = {L"",        L"строки для «Союза»", L"по шапке", L"по содержимому",
                                              L"маршрут", L"операции",          L"список",   L"по порядку столбцов"};
        tn_cpy(note, 160, s->name);
        if (po.how > 0 && po.how < (int)(sizeof(kHow) / sizeof(kHow[0]))) {
          tn_cat(note, 160, L" (");
          tn_cat(note, 160, kHow[po.how]);
          tn_cat(note, 160, L")");
        }
      }
    }
    if (!n && k >= 0) continue; /* лист шаблона без строк — молча */
    if (!n) {
      if (o->passed[0]) pkx_cat(o->passed, 200, L", ");
      pkx_cat(o->passed, 200, s->name);
      continue;
    }
    if (o->used[0]) pkx_cat(o->used, 400, L"; ");
    pkx_cat(o->used, 400, note);
    o->sheets++;
    total += n;
  }
  return total;
}
