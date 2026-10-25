/* cut_ask.c — расход на резку по вопросу словами: чат («резка ст3 10 мм рез 2400 ×20») и
   инструмент агента cut_calc. Числа — cutting_calc.c, нормы резки завода, как в окне «Расчёт
   резки и газов»; марка → материал норм — nm_cut_mat (norms.c). Без Windows: проверяет
   cut_ask_test.c в CI. Подключается после cutting_calc.c, techno.c и norms.c. */

typedef struct {
  const wchar_t *mat;   /* материал норм резки; NULL — не назван или в нормах нет */
  wchar_t named[48];    /* что назвали маркой, если в нормах её нет */
  double t, len;        /* толщина, мм; длина реза одной детали, мм */
  int qty;              /* деталей; 0 — не сказано (как 1) */
} CutAsk;

/* вопрос про резку: «резка», «резку», «длина реза», «расход на рез» — но не «норма» (это norms.c) */
static int cut_ask_wanted(const wchar_t *q) {
  wchar_t *f = tn_fold(q, 0);
  if (!f) return 0;
  int yes = (tn_has(f, L"резк") || tn_has(f, L"реза") || tn_has(f, L"на рез")) && !tn_has(f, L"норм");
  free(f);
  return yes;
}

/* слово [p, e) — марка из норм резки? «ст3», «08пс», «12х18н10т», «амг6»; голое число — только
   после «сталь», «ст», «марка» (иначе это размер) */
static const wchar_t *cut_ask_mat(const wchar_t *p, const wchar_t *e, int afterSteel) {
  wchar_t w[48];
  int n = (int)(e - p);
  if (n <= 0 || n >= 48) return NULL;
  int letters = 0;
  for (int i = 0; i < n; i++) letters |= tn_isletter(p[i]);
  if (!letters && !afterSteel) return NULL;
  wmemcpy(w, p, (size_t)n);
  w[n] = 0;
  return nm_cut_mat(w);
}

/* разбор: числа с подписью (S=, толщина, рез/периметр/длина/L=, ×N, N шт) и без неё (до 60 мм —
   толщина, больше или «м» — длина реза); марка — слово из норм резки */
static void cut_ask_parse(const wchar_t *q, CutAsk *a) {
  memset(a, 0, sizeof(*a));
  wchar_t *f = tn_fold(q, 0);
  if (!f) return;
  int steel = 0; /* предыдущее слово — «сталь», «ст», «марка» */
  for (const wchar_t *p = f; *p;) {
    if (tn_isletter(*p) || (tn_isdig(*p) && p > f && tn_isletter(p[-1]))) { /* слово: буквы и цифры подряд */
      const wchar_t *s = p;
      while (tn_isletter(*p) || tn_isdig(*p) || *p == L'-') p++;
      double tv;
      int tl, ts;
      if (!a->t && (s[0] == L's' || s[0] == L't' || s[0] == L'δ') && tn_num(s + 1, &tv, &tl, &ts) && s + 1 + tl == p) {
        a->t = tv; /* «s10», «t4» — толщина */
        steel = 0;
        continue;
      }
      const wchar_t *m = !a->mat ? cut_ask_mat(s, p, 0) : NULL;
      if (m) a->mat = m;
      else if (!a->mat && !a->named[0] && p - s < 40 && (steel || p - s >= 3) && tn_isdig(p[-1])) {
        wmemcpy(a->named, s, (size_t)(p - s)); /* похоже на марку, но в нормах её нет */
        a->named[p - s] = 0;
      }
      steel = (p - s == 5 && !wcsncmp(s, L"сталь", 5)) || (p - s == 2 && !wcsncmp(s, L"ст", 2)) ||
              (p - s == 5 && !wcsncmp(s, L"марка", 5));
      continue;
    }
    double v;
    int len, sep;
    if (!tn_num(p, &v, &len, &sep)) {
      p++;
      continue;
    }
    const wchar_t *e = p + len, *u = e, *we = e;
    while (tn_isletter(*we) || tn_isdig(*we)) we++;
    if (we > e) { /* число с буквами: «08пс», «09г2с», «12х18н10т» — марка, если она в нормах */
      const wchar_t *m = !a->mat ? cut_ask_mat(p, we, 1) : NULL;
      if (m) {
        a->mat = m;
        p = we;
        steel = 0;
        continue;
      }
      if (steel || (*e != L'м' && *e != L'ш' && *e != L'x' && *e != L'х')) { /* не «10мм», «20шт», «2х»: слово */
        if (!a->mat && !a->named[0] && we - p < 40) { /* «сталь 20Х13» — марка, но не из норм резки */
          wmemcpy(a->named, p, (size_t)(we - p));
          a->named[we - p] = 0;
        }
        p = we;
        steel = 0;
        continue;
      }
    }
    if (steel && !a->mat && (a->mat = cut_ask_mat(p, e, 1)) != NULL) { /* «сталь 45» */
      p = e;
      steel = 0;
      continue;
    }
    steel = 0;
    while (*u == L' ') u++;
    int meters = u[0] == L'м' && u[1] != L'м', mm = u[0] == L'м' && u[1] == L'м', pcs = u[0] == L'ш' && u[1] == L'т';
    /* что сказано перед числом: до 14 знаков назад, до запятой */
    wchar_t before[16] = {0};
    int k = 0;
    for (const wchar_t *b = p - 1; b >= f && k < 14 && *b != L',' && *b != L';'; b--) before[k++] = *b;
    before[k] = 0;
    for (int i = 0; i < k / 2; i++) { /* обратно в прямой порядок */
      wchar_t c = before[i];
      before[i] = before[k - 1 - i];
      before[k - 1 - i] = c;
    }
    const wchar_t *bt = before + (k > 0 ? k - 1 : 0);
    while (bt > before && *bt == L' ') bt--;
    int times = k > 0 && (*bt == L'×' || *bt == L'x' || *bt == L'х' || *bt == L'*');
    int isT = tn_has(before, L"толщ") || tn_has(before, L"s=") || tn_has(before, L"s =") || tn_has(before, L"t=") ||
              tn_has(before, L"δ") || (k >= 1 && (*bt == L's' || *bt == L't') && (k == 1 || !tn_isletter(bt[-1])));
    int isL = tn_word(before, L"рез") || tn_word(before, L"реза") || tn_word(before, L"резу") || /* не «резка» */
              tn_has(before, L"периметр") || tn_has(before, L"контур") ||
              tn_has(before, L"длин") || tn_has(before, L"l=") || tn_has(before, L"l =");
    if (times || pcs || tn_has(before, L"по ")) {
      if (!a->qty && v >= 1 && v < 100000 && !sep) a->qty = (int)v;
    } else if (isT && !a->t) {
      a->t = v;
    } else if (isL && !a->len) {
      a->len = meters ? v * 1000 : v;
    } else if (meters && !a->len) {
      a->len = v * 1000;
    } else if (!a->t && v > 0 && v <= 60 && (mm || !a->len)) {
      a->t = v;
    } else if (!a->len && v > 0) {
      a->len = v;
    }
    p = e;
  }
  free(f);
}

/* ответ на вопрос о резке; 1 — посчитано, 0 — не хватает данных (в out — что назвать) */
static int cut_answer(const wchar_t *q, wchar_t *out, int cap) {
  CutAsk a;
  cut_ask_parse(q, &a);
  out[0] = 0;
  if (!a.mat || !(a.t > 0) || !(a.len > 0)) {
    wchar_t need[200] = {0};
    if (!a.mat) tn_item(need, 200, L"марку");
    if (!(a.t > 0)) tn_item(need, 200, L"толщину листа");
    if (!(a.len > 0)) tn_item(need, 200, L"длину реза одной детали (контур и отверстия)");
    swprintf(out, (size_t)cap, L"Для расхода на резку назовите %ls. Например: «резка ст3 10 мм, рез 2400 мм ×20».",
               need);
    if (a.named[0]) {
      size_t n = wcslen(out);
      swprintf(out + n, (size_t)cap - n, L"\nМарки «%ls» в нормах резки нет.", a.named);
    }
    if (!a.mat) {
      size_t n = wcslen(out);
      swprintf(out + n, (size_t)cap - n, L"\nВ нормах резки завода: ");
      for (int i = 0; i < CUT_NMATS; i++) {
        n = wcslen(out);
        swprintf(out + n, (size_t)cap - n, L"%ls%ls", i ? L", " : L"", kCutMaterials[i]);
      }
      n = wcslen(out);
      swprintf(out + n, (size_t)cap - n, L".");
    }
    out[cap - 1] = 0;
    return 0;
  }
  int qty = a.qty > 0 ? a.qty : 1;
  CutPack *pk = (CutPack *)malloc(sizeof(CutPack));
  if (!pk) return 0;
  if (!cut_calc(a.mat, a.t, a.len * qty, pk)) {
    wchar_t t[40];
    cut_fmt(a.t, 2, t, 40);
    swprintf(out, (size_t)cap, L"В нормах резки нет %ls толщиной %ls мм.", a.mat, t);
    out[cap - 1] = 0;
    free(pk);
    return 0;
  }
  size_t n = 0;
  swprintf(out, (size_t)cap, L"Расход на резку — по нормам резки завода (как в окне «Расчёт резки и газов»).\n");
  if (qty > 1) {
    wchar_t one[40], all[40];
    cut_fmt(a.len, 0, one, 40);
    cut_fmt(a.len * qty, 0, all, 40);
    n = wcslen(out);
    swprintf(out + n, (size_t)cap - n, L"%d шт × %ls мм = %ls мм реза.\n", qty, one, all);
  }
  n = wcslen(out);
  cut_copy_text(pk, out + n, cap - (int)n);
  out[cap - 1] = 0;
  free(pk);
  return 1;
}
