/* ---- «Вставить» окна «ЭСИ и ТП»: разбор (pk_paste.c) → листы окна (package_ui.c) ----------
   Подключается после pk_paste.c: тому нужны маршрут и коды операций (techno.c, soyuz_ops.c),
   а окно пакета стоит раньше — оно зовёт отсюда pk_paste_text и мелочи про виды и размеры.
   Строка, которая в листе уже есть (то же обозначение; в составе — та же пара; в операциях —
   та же деталь и номер), не дублируется: у изделий и состава дописываются только пустые
   ячейки (строки из «Союза» вставка не переименует), у операций и заготовок новое заменяет. */

typedef struct {
  int added[PK_NT], merged;
} PkPutCtx;

static int pk_find_row(const XpTable *t, const wchar_t *a, int c2, const wchar_t *b) {
  for (int r = 0; r < t->n; r++)
    if (!_wcsicmp(xp_cell((void *)t, r, 0), a) && (c2 < 0 || !_wcsicmp(xp_cell((void *)t, r, c2), b))) return r;
  return -1;
}

static void pk_put_row(void *ctx, int sheet, const wchar_t *const *c) {
  PkPutCtx *x = (PkPutCtx *)ctx;
  if (sheet < 0 || sheet >= PK_NT) return;
  XpTable *t = &g_pk[sheet];
  int key2 = sheet == PK_LINKS || sheet == PK_OPS ? 1 : -1;
  int r = c[0][0] && (key2 < 0 || c[key2][0]) ? pk_find_row(t, c[0], key2, key2 >= 0 ? c[key2] : L"") : -1;
  if (r >= 0) {
    BOOL replace = sheet == PK_OPS || sheet == PK_BLANKS;
    for (int k = 1; k < t->ncols && k < PP_COLS; k++)
      if (c[k][0] && (replace || !xp_cell(t, r, k)[0])) pk_put(t, r, k, c[k]);
    x->merged++;
    return;
  }
  if (!xp_row(t)) return;
  for (int k = 0; k < t->ncols && k < PP_COLS; k++) pk_put(t, t->n - 1, k, c[k]);
  x->added[sheet]++;
}

static int pk_paste_text(const wchar_t *text, int sheet, const wchar_t *assembly, const wchar_t *part, int *show,
                         wchar_t *msg, int cap) {
  PkPutCtx x;
  memset(&x, 0, sizeof(x));
  PpIn in = {sheet, assembly, part, pk_put_row, &x};
  PpOut o;
  int n = pp_parse(text, &in, &o);
  *show = sheet;
  if (!o.rows[sheet])
    for (int k = 0; k < PK_NT && k < 4; k++)
      if (o.rows[k] > o.rows[*show]) *show = k;
  if (!n) {
    lstrcpynW(msg,
              L"Не понял, что в буфере. Подходит: таблица с шапкой, спецификация, маршрут из чата, строки «005 Токарная …», "
              L"список «поз. обозначение наименование кол-во»",
              cap);
    return 0;
  }
  static const wchar_t *const kHow[] = {L"",         L"строки для «Союза»", L"таблица по шапке", L"таблица по содержимому",
                                        L"маршрут", L"операции",          L"список",           L"по порядку столбцов"};
  const wchar_t *how = o.how >= 0 && o.how < (int)(sizeof(kHow) / sizeof(kHow[0])) ? kHow[o.how] : L"";
  int l = _snwprintf(msg, cap, L"Вставлено (%s): изделий %d, состав %d, операций %d, заготовок %d", how, x.added[PK_ITEMS],
                     x.added[PK_LINKS], x.added[PK_OPS], x.added[PK_BLANKS]);
  if (l < 0) l = 0;
#define PK_MSG(...)                                                                                                     \
  do {                                                                                                                 \
    if (l < cap - 1) {                                                                                                 \
      int m_ = _snwprintf(msg + l, (size_t)(cap - l), __VA_ARGS__);                                                    \
      l = m_ < 0 ? cap - 1 : l + m_;                                                                                   \
    }                                                                                                                  \
  } while (0)
  if (x.merged) PK_MSG(L"; уже были — дополнено: %d", x.merged);
  if (o.part[0] && (o.rows[PK_OPS] || o.rows[PK_BLANKS])) PK_MSG(L"; к детали %s", o.part);
  if (o.noPart) PK_MSG(L"; без детали %d — выберите позицию и вставьте ещё раз", o.noPart);
  if (o.noParent) PK_MSG(L"; в состав без сборки %d — выберите сборку", o.noParent);
  if (o.docs || o.mats) PK_MSG(L"; пропущено: документов %d, материалов %d", o.docs, o.mats);
  if (o.cols[0]) PK_MSG(L"; столбцы: %s", o.cols);
#undef PK_MSG
  msg[cap - 1] = 0;
  return n;
}

static const wchar_t *pk_kind_name(int i) { return i >= 0 && i < PP_KINDS ? kPpKinds[i] : NULL; }

static int pk_kind_fix(const wchar_t *in, wchar_t *out, int cap) {
  if (pp_kind(in, out, cap)) return 1;
  for (int i = 0; i < PP_KINDS; i++)
    if (!wcscmp(out, kPpKinds[i])) return 0;
  return out[0] ? -1 : 0;
}

static int pk_kind_noname(const wchar_t *kind) { return pp_kind_noname(kind); }
static int pk_sizes_fix(const wchar_t *in, wchar_t *out, int cap) { return pp_sizes(in, out, cap); }
static int pk_sizes_ok(const wchar_t *s) { return pp_sizes_ok(s); }
