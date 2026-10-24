/* techno.c — черновой маршрут обработки по чертежу, правилами опытного технолога
   (с 2026.10.22). Вход — штамп (наименование, материал) и текст всего листа
   (drawrec.c: ТТ, Ra, допуски, резьбы). Выход — вид детали, материал,
   заготовка («из чего») и операции («как») — у каждой причина: что на чертеже
   её требует. Числа модель не придумывает: всё, что не прочитано, — в
   «Уточнить». Windows здесь не нужен — правила проверяются и на Linux:
     gcc -O2 -Wall -Werror -o techno_test techno_test.c && ./techno_test

   Порядок решений:
     1. признаки с листа (tn_signs): марка и ГОСТ материала, сортамент, Ra,
        допуски, HRC и термообработка, резьбы, «n отв.», покрытие, слесарка;
     2. вид детали: сортамент → наименование → «S=» листа → литьё →
        признаки вращения (Ø, резьбы, фаски ×45°);
     3. заготовка: с чертежа, иначе по виду детали и материалу;
     4. операции по порядку цеха: заготовительная, черновая, ТО до чистовой,
        чистовая, фрезерная, сверлильная, ТО после, шлифовальная, слесарная,
        маркировка, покрытие, контроль.
   Текст листа — распознанный Tesseract'ом курсив ГОСТ 2.304: латиница вместо
   похожих русских букв, потерянные запятые («Ra 08»). Ключевые слова ищутся в
   «сложенном» тексте (tn_fold), числа — в исходном. */

enum { TN_ROT, TN_FLAT, TN_BODY, TN_UNK, TN_BAR, TN_BENT, TN_REWORK };
static const wchar_t *const kTnCls[] = {L"тело вращения", L"листовая (плоская)", L"корпусная", L"не определён",
                                        L"из проката без обработки", L"гнутая из прутка", L"доработка готовой детали"};

enum {
  TNM_UNK, TNM_STEEL, TNM_STEEL_PLAIN, TNM_STEEL_ALLOY, TNM_STEEL_STAIN, TNM_STEEL_TOOL, TNM_IRON, TNM_STEEL_CAST,
  TNM_ALU, TNM_ALU_CAST, TNM_COPPER, TNM_BRASS, TNM_BRONZE, TNM_PLASTIC
};
static const wchar_t *const kTnMat[] = {
    L"", L"сталь углеродистая качественная", L"сталь обыкновенного качества", L"сталь легированная",
    L"сталь коррозионностойкая", L"сталь инструментальная", L"чугун", L"стальное литьё", L"алюминиевый сплав",
    L"алюминиевый литейный сплав", L"медь", L"латунь", L"бронза", L"пластмасса"};

enum { TNS_NONE, TNS_ROUND, TNS_HEX, TNS_SQUARE, TNS_SHEET, TNS_STRIP, TNS_PIPE, TNS_PROF };
static const wchar_t *const kTnSort[] = {L"", L"Круг", L"Шестигранник", L"Квадрат", L"Лист", L"Полоса", L"Труба",
                                         L"Труба профильная"};

/* термообработка: что прочитано словами */
enum { TNH_CEMENT = 1, TNH_QUENCH = 2, TNH_TEMPER = 4, TNH_NORM = 8, TNH_NITRID = 16, TNH_TVCH = 32 };
enum { TNC_NONE, TNC_GALV, TNC_PAINT, TNC_ANOD };

#define TN_TXT 200
typedef struct {
  wchar_t name[TN_TXT];   /* наименование: из штампа или найденное слово */
  wchar_t grade[48];      /* марка: «45», «20Х», «Л63» */
  wchar_t matStd[40];     /* «ГОСТ 1050-2013» */
  int mat;                /* TM_… */
  double carbon;          /* %C по марке стали; 0 — не знаем */
  wchar_t sortStd[40];    /* «ГОСТ 2590-2006» */
  int sort;               /* TS_… с чертежа */
  double sortSize;        /* размер сортамента: Ø круга, S листа */
  double thick;           /* «S=6» на листе */
  int thickSure;          /* «S=6» или «S6*»: не случайная «S 3» из обозначения сварки */
  double raMin;           /* наименьшая Ra; 0 — не нашли */
  double raGen;           /* Ra «остальное» из знака в углу листа */
  int raDoubt;            /* прочитана без запятой («Ra 08») */
  int nRa;
  double band;            /* наименьший допуск по отклонениям, мм; 0 — нет */
  int itMin;              /* наименьший квалитет по полям допусков (h6, H7); 0 — нет */
  int hrcLo, hrcHi;
  int hbLo, hbHi;         /* «255…302 HB» */
  int heat;               /* TH_… */
  int heatFirst;          /* «калить в заготовке»: ТО до механообработки */
  wchar_t thr[TN_TXT];    /* резьбы, как прочитаны: «M12-6H, M30x2» */
  int nThr, thrIn, thrOut;
  int holes;              /* «n отв.» — сумма разных выносок */
  wchar_t thrHole[TN_TXT]; /* резьбы из «n отв. M10» — в отверстиях: метчиком на сверлильной */
  double holeMin;         /* наименьший Ø у «n отв. Øx»; 0 — не прочитан */
  int edges, mark, weld, bend, key, gear, knurl, flat, hex;
  int edm;                /* «электроэрозионным способом» */
  int cutApart;           /* «разрезать на две части» */
  int gauge;              /* калибр/шаблон: ПР и НЕ */
  int slot;               /* «паз» словами */
  int rework;             /* доработка готовой детали */
  wchar_t sortDim[24];    /* размер сортамента словом: «40х40х2», «h11-9» */
  wchar_t coat[TN_TXT];   /* покрытие, как прочитано */
  int coatKind;           /* TC_… */
  double dmax;            /* наибольший Ø со знаком Ø — уверенно; 0 — нет */
  int rot;                /* признаки тела вращения: Ø, резьбы, фаски ×45° */
} TnSigns;

typedef struct {
  wchar_t op[48], what[300], why[TN_TXT];
} TnOp;

typedef struct {
  TnSigns s;
  int cls;
  wchar_t clsWhy[TN_TXT];
  wchar_t blank[TN_TXT], blankWhy[TN_TXT];
  int blankRolled;        /* заготовка — прокат: нужна отрезка/раскрой */
  TnOp op[24];
  int n;
  wchar_t check[1200];    /* что уточнить технологу */
} TnRoute;

/* ---- строки без printf (одинаково на Windows и Linux) ------------------------ */

static void tn_cat(wchar_t *d, int cap, const wchar_t *s) {
  int n = (int)wcslen(d);
  while (s && *s && n < cap - 1) d[n++] = *s++;
  d[n] = 0;
}

static void tn_cpy(wchar_t *d, int cap, const wchar_t *s) {
  d[0] = 0;
  tn_cat(d, cap, s);
}

/* число без лишних нулей, запятая: 0,8 · 12,5 · 50 */
static void tn_numstr(double v, wchar_t *out, int cap) {
  long m = (long)(v * 1000 + (v < 0 ? -0.5 : 0.5));
  wchar_t t[32];
  int n = 0, neg = m < 0;
  if (neg) m = -m;
  long ip = m / 1000, fp = m % 1000;
  wchar_t d[16];
  int k = 0;
  do d[k++] = (wchar_t)(L'0' + ip % 10), ip /= 10; while (ip && k < 15);
  if (neg) t[n++] = L'-';
  while (k) t[n++] = d[--k];
  if (fp) {
    t[n++] = L',';
    int div = 100;
    while (fp && div) {
      t[n++] = (wchar_t)(L'0' + fp / div);
      fp %= div;
      div /= 10;
    }
  }
  t[n] = 0;
  tn_cpy(out, cap, t);
}

static void tn_catnum(wchar_t *d, int cap, double v) {
  wchar_t t[32];
  tn_numstr(v, t, 32);
  tn_cat(d, cap, t);
}

/* добавить пункт через «; » */
static void tn_item(wchar_t *d, int cap, const wchar_t *s) {
  if (d[0]) tn_cat(d, cap, L"; ");
  tn_cat(d, cap, s);
}

/* есть ли в списке «a; b; c» ровно такой пункт */
static int tn_list_has(const wchar_t *list, const wchar_t *item) {
  size_t n = wcslen(item);
  for (const wchar_t *p = list; p && *p;) {
    if (!wcsncmp(p, item, n) && (p[n] == 0 || p[n] == L';')) return 1;
    p = wcschr(p, L';');
    if (p) p += p[1] == L' ' ? 2 : 1;
  }
  return 0;
}

/* ---- сложенный текст: регистр, ё и латиница, похожая на кириллицу ------------ */

static wchar_t tn_fold1(wchar_t c, int nAsL) {
  switch (c) { /* курсив ГОСТ 2.304 Tesseract часто читает латиницей */
  case L'A': case L'a': return L'а';
  case L'B': case L'b': return L'в';
  case L'C': case L'c': return L'с';
  case L'E': case L'e': return L'е';
  case L'H': case L'h': return L'н';
  case L'K': case L'k': return L'к';
  case L'M': return L'м';
  case L'm': return L'т';
  case L'n': return nAsL ? L'л' : L'п'; /* курсивные «п» и «л» оба похожи на n */
  case L'O': case L'o': return L'о';
  case L'P': case L'p': return L'р';
  case L'T': case L't': return L'т';
  case L'u': return L'и';
  case L'X': case L'x': return L'х';
  case L'Y': case L'y': return L'у';
  case L'r': return L'г';
  case L'Ё': case L'ё': return L'е';
  }
  if (c >= L'А' && c <= L'Я') return (wchar_t)(c + 32);
  return c;
}

static wchar_t *tn_fold(const wchar_t *s, int nAsL) {
  size_t n = wcslen(s);
  wchar_t *f = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
  if (!f) return NULL;
  for (size_t i = 0; i <= n; i++) f[i] = tn_fold1(s[i], nAsL);
  return f;
}

static int tn_isdig(wchar_t c) { return c >= L'0' && c <= L'9'; }
static int tn_isletter(wchar_t c) {
  return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'а' && c <= L'я') ||
         (c >= L'А' && c <= L'Я') || c == L'ё' || c == L'Ё';
}

static int tn_has(const wchar_t *f, const wchar_t *w) { return f && wcsstr(f, w) != NULL; }

/* слово целиком (границы — не буквы) */
static int tn_word(const wchar_t *f, const wchar_t *w) {
  size_t n = wcslen(w);
  for (const wchar_t *p = f; p && (p = wcsstr(p, w)) != NULL; p++)
    if ((p == f || !tn_isletter(p[-1])) && !tn_isletter(p[n])) return 1;
  return 0;
}

/* число с позиции p: «0,015», «12.5», «08»; *len — сколько символов, *sep — была ли запятая */
static int tn_num(const wchar_t *p, double *v, int *len, int *sep) {
  int i = 0;
  double x = 0;
  *sep = 0;
  if (!tn_isdig(p[0])) return 0;
  while (tn_isdig(p[i])) x = x * 10 + (p[i++] - L'0');
  if ((p[i] == L',' || p[i] == L'.') && tn_isdig(p[i + 1])) {
    double k = 0.1;
    *sep = 1;
    i++;
    while (tn_isdig(p[i])) x += (p[i++] - L'0') * k, k /= 10;
  }
  *v = x;
  *len = i;
  return 1;
}

/* ---- шероховатость -------------------------------------------------------------- */

/* «Ra 08» без запятой: 08 → 0,8; 063 → 0,63; 125 → 12,5; 63/32/16 — скорее 6,3/3,2/1,6 */
static double tn_ra_fix(const wchar_t *p, int len, double v, int sep, int *doubt) {
  *doubt = 0;
  if (sep) return v;
  if (p[0] == L'0' && len >= 2) {
    double d = 1;
    for (int i = 1; i < len; i++) d *= 10;
    *doubt = 1;
    return v / d;
  }
  if (v == 125) return *doubt = 1, 12.5;
  if (v == 63 || v == 32 || v == 16) return *doubt = 1, v / 10;
  return v;
}

/* ряд Ra по ГОСТ 2789-73, мкм */
static int tn_ra_std(double v) {
  static const double r[] = {0.012, 0.016, 0.02, 0.025, 0.032, 0.04, 0.05, 0.063, 0.08, 0.1, 0.125, 0.16, 0.2, 0.25,
                             0.32,  0.4,   0.5,  0.63,  0.8,   1,    1.25, 1.6,   2,    2.5, 3.2,   4,    5,   6.3,
                             8,     10,    12.5, 16,    20,    25,   32,   40,    50,   63,  80,    100};
  for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++)
    if (v > r[i] * 0.97 && v < r[i] * 1.03) return 1;
  return 0;
}

static void tn_ra(const wchar_t *t, TnSigns *s) {
  for (const wchar_t *p = t; *p; p++) {
    /* R/Р/P/К + a/а — «Ra», «Pa», «Ка» (курсив) */
    if (!(p[0] == L'R' || p[0] == L'Р' || p[0] == L'P' || p[0] == L'К' || p[0] == L'K')) continue;
    if (!(p[1] == L'a' || p[1] == L'а')) continue;
    if (p > t && tn_isletter(p[-1])) continue;
    const wchar_t *q = p + 2;
    while (*q == L' ' || *q == L'=') q++;
    wchar_t fix[12];
    int m = 0;
    for (const wchar_t *c = q; m < 11 && (tn_isdig(*c) || *c == L',' || *c == L'.' || *c == L'Z' || *c == L'O'); c++)
      fix[m++] = *c == L'Z' ? L'2' : *c == L'O' ? L'0' : *c; /* «RaZ0» — Ra 20: курсивная 2 похожа на Z */
    fix[m] = 0;
    q = fix;
    double v;
    int len, sep, doubt;
    if (!tn_num(q, &v, &len, &sep)) continue;
    v = tn_ra_fix(q, len, v, sep, &doubt);
    if (v < 0.01 || v > 100 || !tn_ra_std(v)) continue;
    s->nRa++;
    if (!s->raMin || v < s->raMin) s->raMin = v, s->raDoubt = doubt;
  }
  /* ГОСТ 2.309-73 (изм. 3): «Ra» над знаком не пишут — на листе только число.
     Знак «остальное» в углу Tesseract читает «\/ 63 (\/)», «VV 63»; местные
     значения стоят отдельной строкой («08», «1,6», «32» — запятая теряется) */
  for (const wchar_t *p = t; *p; p++) {
    int corner = (p[0] == L'\\' && p[1] == L'/') || (p[0] == L'√') || (p[0] == L'V' && p[1] == L'V');
    if (!corner || (p > t && tn_isletter(p[-1]))) continue;
    const wchar_t *q = p + (p[0] == L'√' ? 1 : 2);
    while (*q == L' ') q++;
    double v;
    int len, sep, doubt;
    if (!tn_num(q, &v, &len, &sep)) continue;
    if (!sep && len == 3 && (v == 631 || v == 321 || v == 161 || v == 125)) v = v == 125 ? 125 : (int)v / 10, len = 2;
    v = tn_ra_fix(q, len, v, sep, &doubt);
    if (v == 6.3 || v == 3.2 || v == 12.5 || v == 1.6 || v == 25) {
      s->raGen = v;
      break;
    }
  }
  for (const wchar_t *p = t; *p; p++) {
    if (p != t && p[-1] != L'\n') continue;
    const wchar_t *q = p;
    while (*q == L' ') q++;
    double v;
    int len, sep, doubt;
    if (!tn_num(q, &v, &len, &sep)) continue;
    const wchar_t *e = q + len;
    while (*e == L' ') e++;
    if (*e && *e != L'\n') continue; /* только число одно на строке */
    v = tn_ra_fix(q, len, v, sep, &doubt);
    if (!(v == 0.4 || v == 0.8 || v == 1.6 || v == 3.2 || v == 0.2)) continue;
    /* «16», «32» без запятой — чаще размер (вал: «32» — длина под резьбу); шероховатостью
       считаем, только если на листе есть знак «остальное» в углу, то есть Ra пишут числом */
    if (!sep && q[0] != L'0') {
      if (!s->raGen) continue;
      doubt = 1;
    }
    s->nRa++;
    if (!s->raMin || v < s->raMin) s->raMin = v, s->raDoubt = doubt;
  }
  if (s->raGen && !s->raMin) s->raMin = s->raGen, s->nRa++;
}

/* ---- допуски: отклонения «-0,02 / -0,04», поля «h6», «H7» --------------------------- */

static int tn_line_general(const wchar_t *t, const wchar_t *p) {
  /* строка «Общие допуски…», «H14, h14, ±IT14/2» — не точные размеры */
  const wchar_t *b = p;
  while (b > t && b[-1] != L'\n') b--;
  wchar_t line[200];
  int n = 0;
  while (b[n] && b[n] != L'\n' && n < 199) line[n] = tn_fold1(b[n], 0), n++;
  line[n] = 0;
  return tn_has(line, L"общ") || tn_has(line, L"it") || tn_has(line, L"іт") || tn_has(line, L"14");
}

static void tn_tol(const wchar_t *t, TnSigns *s) {
  double prev = 0;
  const wchar_t *prevAt = NULL;
  for (const wchar_t *p = t; *p; p++) {
    /* отклонение: [+-]0,0dd или 0,0dd; без запятой — только со знаком: «-0070» */
    int sign = (*p == L'-' || *p == L'+' || *p == L'–' || *p == L'−');
    const wchar_t *q = sign ? p + 1 : p;
    if (!sign && p > t && (tn_isdig(p[-1]) || p[-1] == L',' || p[-1] == L'.')) continue;
    if (q[0] != L'0') continue;
    double v;
    int len, sep;
    if (!tn_num(q, &v, &len, &sep)) continue;
    if (!sep) {
      if (!sign || len < 3 || q[1] != L'0') continue;
      double d = 1;
      for (int i = 1; i < len; i++) d *= 10;
      v /= d; /* «-0070» → 0,070 */
    } else if (len < 4) {
      continue; /* «0,8» — не отклонение (Ra, фаска) */
    }
    const wchar_t *e = q + len;
    while (*e == L' ') e++;
    if (*e == L'x' || *e == L'х' || *e == L'×' || tn_isletter(*e)) continue; /* фаска, «мм» */
    if (v <= 0 || v > 0.15 || tn_line_general(t, p)) continue;
    double band = v;
    if (prevAt && p - prevAt < 40 && prev != v) band = prev > v ? prev - v : v - prev;
    if (band >= 0.004 && (!s->band || band < s->band)) s->band = band; /* тоньше 4 мкм — шум распознавания */
    prev = v;
    prevAt = p;
    p = e - 1;
  }
  /* поле допуска после размера: «40k6», «28H7», «Ø40h6» */
  for (const wchar_t *p = t + 1; *p; p++) {
    if (!tn_isdig(p[-1]) || !wcschr(L"hHgfekKnpmjJsd", *p)) continue;
    const wchar_t *q = p + 1;
    if ((*p == L'j' || *p == L'J') && *q == L's') q++;
    int it = 0, k = 0;
    while (tn_isdig(q[k]) && k < 2) it = it * 10 + (q[k] - L'0'), k++;
    if (!k || q[0] == L'0' || tn_isdig(q[k]) || tn_isletter(q[k]) || it < 4 || it > 11 || tn_line_general(t, p)) continue;
    if (!s->itMin || it < s->itMin) s->itMin = it;
  }
}

/* ---- твёрдость и термообработка ------------------------------------------------------ */

/* числа твёрдости в строке вокруг p: сначала перед (≤24 знака), нет — после.
   «415.50» — это «41,5…50» с потерянными запятой и точками: 415 → 41 */
static int tn_hard_nums(const wchar_t *t, const wchar_t *p, int skip, int lo, int hi, int *nums) {
  int k = 0;
  const wchar_t *b = p;
  while (b > t && b[-1] != L'\n' && p - b < 24) b--;
  for (int pass = 0; pass < 2 && !k; pass++)
    for (const wchar_t *q = pass ? p + skip : b; *q && *q != L'\n' && (pass ? q - p < 20 : q < p) && k < 2; q++)
      if (tn_isdig(*q) && (q == t || !tn_isdig(q[-1]))) {
        int v = 0, n = 0;
        while (tn_isdig(*q)) v = v * 10 + (*q++ - L'0'), n++;
        if (hi <= 70 && n == 3 && *q == L'.' && v / 10 >= lo) v /= 10;
        if (hi <= 70 && n == 3 && pass && v / 100 == 2 && v % 100 >= lo) v %= 100; /* «HRC ≥48» — «ARE 248» */
        if (v >= lo && v <= hi) nums[k++] = v;
        q--;
      }
  return k;
}

static void tn_hrc(const wchar_t *t, TnSigns *s) {
  for (const wchar_t *p = t; *p; p++) {
    /* «HRC», «HRCэ»; Tesseract на курсиве: «ARC», «ARE», «НАС» */
    int hrc = (p[0] == L'H' || p[0] == L'Н' || p[0] == L'A' || p[0] == L'А') && (p[1] == L'R' || p[1] == L'Р') &&
              (p[2] == L'C' || p[2] == L'С');
    int odd = (p[0] == L'A' && p[1] == L'R' && p[2] == L'E') ||
              ((p[0] == L'Н' || p[0] == L'H') && (p[1] == L'А' || p[1] == L'A') && (p[2] == L'С' || p[2] == L'C'));
    if (odd && ((p > t && tn_isletter(p[-1])) || tn_isletter(p[3]))) odd = 0;
    if (!hrc && !odd) continue;
    /* «35…40 HRC» — числа перед, «HRC 40…45» — после (в той же строке) */
    int nums[2], k = tn_hard_nums(t, p, 3, 20, 70, nums);
    if (!k) continue;
    int lo = nums[0], hi = k > 1 ? nums[1] : nums[0];
    if (lo > hi) { int x = lo; lo = hi; hi = x; }
    if (!s->hrcHi || hi > s->hrcHi) s->hrcLo = lo, s->hrcHi = hi;
  }
  /* по Бринеллю: «255…302 HB», «НВ 241…285» */
  for (const wchar_t *p = t; *p; p++) {
    if (!((p[0] == L'H' || p[0] == L'Н') && (p[1] == L'B' || p[1] == L'В')) || (p > t && tn_isletter(p[-1])) ||
        tn_isletter(p[2]))
      continue;
    int nums[2], k = tn_hard_nums(t, p, 2, 100, 700, nums);
    if (!k) continue;
    int lo = nums[0], hi = k > 1 ? nums[1] : nums[0];
    if (lo > hi) { int x = lo; lo = hi; hi = x; }
    s->hbLo = lo, s->hbHi = hi;
  }
}

/* ---- резьбы ------------------------------------------------------------------------ */

static wchar_t tn_digitish(wchar_t c) {
  /* «Mi2» — M12: в номинале резьбы i/l/I/| читаются как 1, O — как 0 */
  if (tn_isdig(c)) return c;
  if (c == L'i' || c == L'l' || c == L'I' || c == L'|') return L'1';
  if (c == L'O' || c == L'o' || c == L'О' || c == L'о') return L'0';
  return 0;
}

/* резьбы для токарной: все прочитанные, кроме тех, что в «n отв. M…» — их режут метчиком */
static void tn_thr_turn(const TnSigns *s, wchar_t *out, int cap) {
  out[0] = 0;
  for (const wchar_t *p = s->thr; *p;) {
    wchar_t item[48], base[48];
    int n = 0, k = 0;
    while (*p && *p != L';' && n < 47) item[n++] = *p++;
    item[n] = 0;
    while (*p == L';' || *p == L' ') p++;
    while (item[k] && item[k] != L'x' && item[k] != L'-') base[k] = item[k], k++;
    base[k] = 0;
    if (!tn_list_has(s->thrHole, base)) tn_item(out, cap, item);
  }
}

static void tn_thread_add(TnSigns *s, const wchar_t *txt, wchar_t cls) {
  if (s->nThr >= 8 || tn_has(s->thr, txt)) return;
  tn_item(s->thr, TN_TXT, txt);
  s->nThr++;
  if (cls == L'H' || cls == L'G') s->thrIn++;
  else if (cls) s->thrOut++;
  s->rot++;
}

static void tn_threads(const wchar_t *t, const wchar_t *f, TnSigns *s) {
  for (const wchar_t *p = t; *p; p++) {
    int tr = (p[0] == L'T' && p[1] == L'r');
    if (!tr && !(p[0] == L'M' || p[0] == L'М')) continue;
    if (p > t && tn_isletter(p[-1])) continue;
    const wchar_t *q = p + (tr ? 2 : 1);
    wchar_t num[8];
    int k = 0, real = 0;
    if (tr && *q == L't') num[k++] = L'1', q++; /* «Trt6x2» — Tr16x2 */
    for (wchar_t c; k < 3 && (c = tn_digitish(*q)) != 0; q++) num[k++] = c, real += tn_isdig(*q);
    num[k] = 0;
    int px = (*q == L'x' || *q == L'х' || *q == L'×' || *q == L'*');
    if (!k || !real || (tn_isletter(*q) && !px)) continue;
    int d = 0;
    for (int i = 0; i < k; i++) d = d * 10 + (num[i] - L'0');
    if (d < 2 || d > 160) continue;
    wchar_t txt[40];
    tn_cpy(txt, 40, tr ? L"Tr" : L"M");
    tn_cat(txt, 40, num);
    if (*q == L'x' || *q == L'х' || *q == L'×' || *q == L'*') { /* шаг: M30x2, Tr16x2 */
      double v;
      int len, sep;
      if (tn_num(q + 1, &v, &len, &sep)) {
        if (!tr && !sep && v >= 10 && v < 100) v /= 10; /* «М35*15» — запятая потеряна: шаг 1,5 */
        tn_cat(txt, 40, L"x"), tn_catnum(txt, 40, v), q += 1 + len;
      }
    }
    wchar_t cls = 0;
    /* «M12-8]9» — Tesseract читает g как «]9» */
    wchar_t tc = (*q == L'-' && tn_isdig(q[1])) ? (q[2] == L']' ? L'g' : q[2]) : 0;
    if (tc && wcschr(L"HGhgef", tc)) {
      wchar_t tail[4] = {L'-', q[1], tc, 0};
      tn_cat(txt, 40, tail);
      cls = (tc == L'H' || tc == L'G') ? tc : L'g';
    }
    if (k == 1 && d < 3) continue; /* «М1:1» — масштаб */
    if (k == 1 && !cls && !wcschr(txt, L'x')) { /* «M6» без поля и шага — шум, если рядом нет «отв.» */
      const wchar_t *fp = f + (p - t), *b0 = fp - 16 < f ? f : fp - 16;
      int otv = 0;
      for (const wchar_t *q2 = b0; q2 < fp && !otv; q2++) otv = q2[0] == L'о' && q2[1] == L'т' && (q2[2] == L'в' || q2[2] == L'б');
      if (!otv) continue;
    }
    tn_thread_add(s, txt, cls);
  }
  /* трапецеидальная словом: «Трап 27х5 лев.» (на старых чертежах), Tesseract — «Tran 27х5» */
  for (const wchar_t *p = f; *p; p++) {
    if (!(!wcsncmp(p, L"трап", 4) || !wcsncmp(p, L"тгап", 4)) || (p > f && tn_isletter(p[-1]))) continue;
    const wchar_t *q = p + 4;
    while (*q == L' ' || *q == L'.') q++;
    double d, st;
    int len, sep;
    if (!tn_num(q, &d, &len, &sep) || d < 8 || d > 160) continue;
    wchar_t txt[40] = L"Tr";
    tn_catnum(txt, 40, d);
    q += len;
    if ((*q == L'х' || *q == L'×') && tn_num(q + 1, &st, &len, &sep)) tn_cat(txt, 40, L"x"), tn_catnum(txt, 40, st), q += 1 + len;
    while (*q == L' ') q++;
    if (!wcsncmp(q, L"лев", 3)) tn_cat(txt, 40, L"LH");
    tn_thread_add(s, txt, tn_has(f, L"гайк") ? L'H' : L'g');
  }
  /* трубная: «Труб. 3/4"», «G1/2» */
  for (const wchar_t *p = f; (p = wcsstr(p, L"труб")) != NULL; p++) {
    const wchar_t *q = p + 4;
    while (*q == L'.' || *q == L' ') q++;
    if (tn_isdig(q[0]) && (q[1] == L'/' || q[1] == L'"' || q[1] == L'”')) tn_thread_add(s, L"трубная", 0);
  }
}

/* ---- материал и сортамент ------------------------------------------------------------ */

typedef struct {
  int num;
  int mat;   /* TM_… — стандарт марки */
  int sort;  /* TS_… — стандарт сортамента */
} TnStd;

static const TnStd kTnStd[] = {
    {1050, TNM_STEEL, 0},       {380, TNM_STEEL_PLAIN, 0},   {4543, TNM_STEEL_ALLOY, 0}, {19281, TNM_STEEL_ALLOY, 0},
    {14959, TNM_STEEL_ALLOY, 0}, {801, TNM_STEEL_ALLOY, 0},   {5632, TNM_STEEL_STAIN, 0}, {1435, TNM_STEEL_TOOL, 0},
    {5950, TNM_STEEL_TOOL, 0},  {19265, TNM_STEEL_TOOL, 0},  {977, TNM_STEEL_CAST, 0},   {1412, TNM_IRON, 0},
    {7293, TNM_IRON, 0},        {1215, TNM_IRON, 0},         {4784, TNM_ALU, 0},         {1583, TNM_ALU_CAST, 0},
    {859, TNM_COPPER, 0},       {15527, TNM_BRASS, 0},       {17711, TNM_BRASS, 0},      {18175, TNM_BRONZE, 0},
    {5017, TNM_BRONZE, 0},      {613, TNM_BRONZE, 0},        {493, TNM_BRONZE, 0},       {2590, 0, TNS_ROUND},
    {7417, 0, TNS_ROUND},       {14955, 0, TNS_ROUND},       {5949, 0, TNS_ROUND},       {2060, TNM_BRASS, TNS_ROUND},
    {21488, TNM_ALU, TNS_ROUND}, {1628, TNM_BRONZE, TNS_ROUND}, {1535, TNM_COPPER, TNS_ROUND}, {2591, 0, TNS_SQUARE},
    {2879, 0, TNS_HEX},         {8560, 0, TNS_HEX},          {103, 0, TNS_STRIP},        {82, 0, TNS_STRIP},
    {19903, 0, TNS_SHEET},      {19904, 0, TNS_SHEET},       {16523, TNM_STEEL_PLAIN, TNS_SHEET},
    {14637, TNM_STEEL_PLAIN, TNS_SHEET}, {1577, TNM_STEEL, 0}, {13663, TNM_STEEL_PLAIN, 0},
    {8639, 0, TNS_PROF},        {8645, 0, TNS_PROF},         {30245, 0, TNS_PROF},
    {5582, TNM_STEEL_STAIN, TNS_SHEET}, {7350, TNM_STEEL_STAIN, TNS_SHEET}, {21631, TNM_ALU, TNS_SHEET},
    {8732, 0, TNS_PIPE},        {8734, 0, TNS_PIPE},         {10704, 0, TNS_PIPE},
};

/* марка → вид материала и %C (для стали) */
static void tn_grade(TnSigns *s) {
  wchar_t g[48];
  int n = 0;
  for (const wchar_t *p = s->grade; *p && n < 47; p++) g[n++] = tn_fold1(*p, 0);
  g[n] = 0;
  if (!g[0]) return;
  if (!wcsncmp(g, L"сч", 2) || !wcsncmp(g, L"вч", 2) || !wcsncmp(g, L"кч", 2)) {
    s->mat = TNM_IRON;
    return;
  }
  if (!wcsncmp(g, L"бр", 2)) { s->mat = TNM_BRONZE; return; }
  if (g[0] == L'л' && (tn_isdig(g[1]) || g[1] == L'с' || g[1] == L'о')) { s->mat = TNM_BRASS; return; }
  if (!wcsncmp(g, L"д1", 2) || !wcsncmp(g, L"амг", 3) || !wcsncmp(g, L"ад", 2) || !wcsncmp(g, L"в95", 3)) {
    if (!s->mat) s->mat = TNM_ALU;
    return;
  }
  if (!wcsncmp(g, L"ак", 2) && tn_isdig(g[2])) { s->mat = TNM_ALU_CAST; return; }
  if (!wcsncmp(g, L"ст", 2) && tn_isdig(g[2])) {
    if (!s->mat) s->mat = TNM_STEEL_PLAIN;
    s->carbon = 0.2;
    return;
  }
  if (g[0] == L'у' && tn_isdig(g[1])) { s->mat = TNM_STEEL_TOOL; return; }
  if (tn_isdig(g[0])) {
    int c = 0, i = 0;
    while (tn_isdig(g[i]) && i < 3) c = c * 10 + (g[i++] - L'0');
    /* 12Х18Н10Т, 20Х13 — хрома 13 % и больше: коррозионностойкая */
    for (const wchar_t *p = wcschr(g, L'х'); p; p = wcschr(p + 1, L'х'))
      if (tn_isdig(p[1]) && tn_isdig(p[2]) && (p[1] - L'0') * 10 + (p[2] - L'0') >= 13) s->mat = TNM_STEEL_STAIN;
    if (g[n - 1] == L'л') s->mat = TNM_STEEL_CAST;
    if (i <= 2 && c > 0 && c < 100) s->carbon = c / 100.0;
    if (!s->mat) s->mat = wcschr(g, L'х') || wcschr(g, L'г') || wcschr(g, L'н') ? TNM_STEEL_ALLOY : TNM_STEEL;
  }
}

/* слово-марка перед «ГОСТ»: последнее слово с цифрой в той же строке («Сталь 45», «20Х», «08кп») */
static void tn_grade_before(const wchar_t *t, const wchar_t *p, wchar_t *out, int cap, double *size) {
  out[0] = 0;
  const wchar_t *b = p;
  while (b > t && b[-1] != L'\n' && p - b < 60) b--;
  const wchar_t *best = NULL, *bestEnd = NULL, *prevW = NULL, *prevEnd = NULL;
  for (const wchar_t *q = b; q < p;) {
    while (q < p && (*q == L' ' || *q == L',' || *q == L'(' || *q == L'[' || *q == L'|' || *q == L'/')) q++;
    const wchar_t *w = q;
    int dig = 0;
    /* запятая между цифрами — дробная часть («Лист 1,5»), не разделитель */
    while (q < p && *q != L' ' && !(*q == L',' && !(q > w && tn_isdig(q[-1]) && tn_isdig(q[1]))) && *q != L'|' &&
           *q != L'/')
      dig += tn_isdig(*q), q++;
    if (q > w && dig) prevW = best, prevEnd = bestEnd, best = w, bestEnd = q;
  }
  if (!best) return;
  /* «Круг 50 / 45 ГОСТ…» в одной строке не бывает: размер сортамента — отдельное слово */
  int n = 0;
  for (const wchar_t *q = best; q < bestEnd && n < cap - 1; q++) out[n++] = *q;
  out[n] = 0;
  if (size && prevW) {
    double v;
    int len, sep;
    if (tn_num(prevW, &v, &len, &sep) && prevW + len == prevEnd) *size = v;
  }
}

static void tn_material(const wchar_t *t, const wchar_t *f, TnSigns *s) {
  /* «ГОСТ 1050-2013»; Tesseract: «OCT», «TOCT», «ГОСТ1050» */
  for (const wchar_t *p = f; (p = wcsstr(p, L"ост")) != NULL; p++) {
    wchar_t prev = p > f ? p[-1] : L' ';
    if (tn_isletter(prev) && prev != L'г' && prev != L'т') continue;
    const wchar_t *q = p + 3;
    while (*q == L' ' || *q == L'.') q++;
    if (!tn_isdig(*q)) continue;
    int num = 0, k = 0;
    while (tn_isdig(q[k]) && k < 6) num = num * 10 + (q[k] - L'0'), k++;
    const TnStd *st = NULL;
    for (size_t i = 0; i < sizeof(kTnStd) / sizeof(kTnStd[0]); i++)
      if (kTnStd[i].num == num) st = &kTnStd[i];
    if (!st) continue;
    wchar_t std[40];
    tn_cpy(std, 40, L"ГОСТ ");
    int n = 5;
    for (int i = 0; i < k + 6 && q[i] && q[i] != L' ' && q[i] != L'\n' && q[i] != L',' && n < 39; i++) {
      if (i >= k && !(q[i] == L'-' || tn_isdig(q[i]))) break;
      std[n++] = q[i];
    }
    std[n] = 0;
    const wchar_t *raw = t + (p - f);
    if (st->sort && !s->sort) {
      s->sort = st->sort;
      tn_cpy(s->sortStd, 40, std);
      wchar_t g[48];
      double size = 0;
      tn_grade_before(t, raw, g, 48, &size);
      /* «Лист 08кп ГОСТ 16523»: перед ГОСТ сортамента — марка, у «Круг 50 ГОСТ 2590» — размер */
      double v;
      int len, sep;
      const wchar_t *dash = wcsrchr(g, L'-');
      if (g[0] && tn_num(g, &v, &len, &sep) && !g[len]) s->sortSize = v;
      else if (g[0] && tn_num(g, &v, &len, &sep) && (g[len] == L'х' || g[len] == L'x' || g[len] == L'×'))
        tn_cpy(s->sortDim, 24, g); /* «Труба 40х40х2» */
      else if (g[0] && !tn_isdig(g[0]) && dash && tn_num(dash + 1, &v, &len, &sep) && !dash[1 + len])
        s->sortSize = v, tn_cpy(s->sortDim, 24, g); /* «Круг h11-9 ГОСТ 7417»: поле допуска и Ø */
      else if (g[0] && !s->grade[0]) tn_cpy(s->grade, 48, g);
    }
    if (st->mat && !s->matStd[0]) {
      s->mat = st->mat;
      tn_cpy(s->matStd, 40, std);
      if (!st->sort || !s->grade[0]) {
        wchar_t g[48];
        tn_grade_before(t, raw, g, 48, NULL);
        if (g[0]) {
          /* «63 ГОСТ 15527» — латунь Л63 с потерянной «Л» */
          if (st->mat == TNM_BRASS && tn_isdig(g[0])) tn_cpy(s->grade, 48, L"Л"), tn_cat(s->grade, 48, g);
          else tn_cpy(s->grade, 48, g);
        }
      }
    }
  }
  /* сортамент словом: «Круг 50», «руг» (потерянная «К»), «Лист», «Шестигранник» */
  if (!s->sort) {
    static const struct { const wchar_t *w; int sort; } kw[] = {
        {L"круг", TNS_ROUND}, {L"пруток", TNS_ROUND}, {L"шестигр", TNS_HEX}, {L"квадрат", TNS_SQUARE},
        {L"лист", TNS_SHEET}, {L"полоса", TNS_STRIP}, {L"труба", TNS_PIPE}};
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]) && !s->sort; i++)
      if (tn_word(f, kw[i].w) && (kw[i].sort != TNS_SHEET || s->thick || tn_has(f, L"лист 0") ||
                                  tn_has(f, L"лист б") || tn_has(f, L"лист s")))
        s->sort = kw[i].sort;
  }
  /* «Труба 40х40х2» — два размера сечения и стенка: профильная, не круглая */
  if (s->sort == TNS_PIPE)
    for (const wchar_t *p = f; (p = wcsstr(p, L"труба")) != NULL; p++) {
      const wchar_t *q = p + 5;
      while (*q == L' ') q++;
      int n = 0;
      double v;
      int len, sep;
      while (tn_num(q, &v, &len, &sep) && n < 3) {
        n++, q += len;
        if (*q != L'х' && *q != L'×') break;
        q++;
      }
      if (n == 3) s->sort = TNS_PROF;
    }
  /* «ОК360В-IV-Н-Ст3» (ГОСТ 16523): марка — хвост после категорий */
  wchar_t *st3 = wcsstr(s->grade, L"-Ст");
  if (st3) memmove(s->grade, st3 + 1, (wcslen(st3 + 1) + 1) * sizeof(wchar_t));
  tn_grade(s);
}

/* ---- покрытие ------------------------------------------------------------------- */

static void tn_coat(const wchar_t *t, const wchar_t *f, TnSigns *s) {
  static const struct { const wchar_t *w; int kind; const wchar_t *name; } kc[] = {
      {L"хим.окс", TNC_GALV, L"химическое оксидирование"}, {L"хим. окс", TNC_GALV, L"химическое оксидирование"},
      {L"хим.фос", TNC_GALV, L"фосфатирование"}, {L"ан.окс", TNC_ANOD, L"анодное оксидирование"},
      {L"анодир", TNC_ANOD, L"анодное оксидирование"}, {L"оцинк", TNC_GALV, L"цинкование"},
      {L"грунт", TNC_PAINT, L"грунт"}, {L"эмал", TNC_PAINT, L"эмаль"}, {L"лак", TNC_PAINT, L"лак"},
      {L"краск", TNC_PAINT, L"краска"}, {L"окрас", TNC_PAINT, L"окраска"}};
  for (size_t i = 0; i < sizeof(kc) / sizeof(kc[0]); i++)
    if (tn_word(f, kc[i].w) || (wcslen(kc[i].w) > 4 && tn_has(f, kc[i].w))) {
      /* «Покраска» — заголовок строки таблицы: общее слово, если уже есть грунт, эмаль или лак */
      if (kc[i].kind == TNC_PAINT && s->coatKind == TNC_PAINT && (!wcscmp(kc[i].w, L"краск") || !wcscmp(kc[i].w, L"окрас")))
        continue;
      if (!s->coatKind || kc[i].kind != TNC_PAINT) s->coatKind = kc[i].kind;
      if (!tn_has(s->coat, kc[i].name)) tn_item(s->coat, TN_TXT, kc[i].name);
    }
  /* гальваника кодом ГОСТ 9.306: «Ц9.хр», «Ц15», «Н12», «Х24», «Кд9» — только в строке
     покрытия: «H14, h14» в допусках — не никель */
  for (const wchar_t *p = f; *p; p++) {
    if (p > f && tn_isletter(p[-1])) continue;
    int k = (p[0] == L'ц' || p[0] == L'н' || p[0] == L'х') ? 1 : (p[0] == L'к' && p[1] == L'д') ? 2 : 0;
    if (!k) continue;
    const wchar_t *b = p;
    while (b > f && b[-1] != L'\n') b--;
    int coatLine = 0;
    for (const wchar_t *q = b; *q && *q != L'\n' && !coatLine; q++)
      coatLine = !wcsncmp(q, L"покр", 4) || !wcsncmp(q, L"гальван", 7);
    if (!coatLine) continue;
    if (!k || !tn_isdig(p[k])) continue;
    int d = 0, i = k;
    while (tn_isdig(p[i]) && i < k + 3) d = d * 10 + (p[i++] - L'0');
    if (d < 3 || d > 60 || tn_isdig(p[i])) continue;
    /* «х24» в размерах («2х45») — не хром: перед «х» должна быть не цифра */
    if (p > f && tn_isdig(p[-1])) continue;
    s->coatKind = TNC_GALV;
    wchar_t code[16];
    int n = 0;
    for (const wchar_t *q = t + (p - f); n < 15 && *q && *q != L' ' && *q != L'\n' && *q != L','; q++) code[n++] = *q;
    code[n] = 0;
    if (!tn_has(s->coat, code)) tn_item(s->coat, TN_TXT, code);
  }
}

/* ---- все признаки ------------------------------------------------------------------ */

static void tn_signs(const wchar_t *name, const wchar_t *mat, const wchar_t *text, TnSigns *s) {
  memset(s, 0, sizeof(*s));
  /* материал из штампа идёт первым: он точнее текста всего листа */
  size_t n = (mat ? wcslen(mat) : 0) + (name ? wcslen(name) : 0) + (text ? wcslen(text) : 0) + 8;
  wchar_t *t = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (!t) return;
  t[0] = 0;
  tn_cat(t, (int)n, mat ? mat : L"");
  tn_cat(t, (int)n, L"\n");
  tn_cat(t, (int)n, name ? name : L"");
  tn_cat(t, (int)n, L"\n");
  tn_cat(t, (int)n, text ? text : L"");
  for (wchar_t *p = t; *p; p++)
    if (*p == L'/' && p[1] == L' ' && p > t && p[-1] == L' ') *p = L'\n'; /* «Круг 50 ГОСТ 2590 / 45 ГОСТ 1050» */
  wchar_t *f = tn_fold(t, 0), *fl = tn_fold(t, 1);
  if (!f || !fl) {
    free(t), free(f), free(fl);
    return;
  }
  if (name && name[0]) tn_cpy(s->name, TN_TXT, name);
  /* «S=6», «S 6 мм» — толщина листа; на заводских чертежах — «S8*» (размер для справок) */
  for (const wchar_t *p = t; *p; p++)
    if ((p[0] == L'S' || p[0] == L's') && (p == t || !tn_isletter(p[-1]))) {
      const wchar_t *q = p + 1;
      while (*q == L' ' || *q == L'=') q++;
      double v;
      int len, sep;
      if (tn_num(q, &v, &len, &sep) && (q > p + 1 || q[len] == L'*') && v >= 0.3 && v <= 100)
        s->thick = v, s->thickSure = wcschr(p, L'=') == p + 1 || q[len] == L'*';
    }
  tn_material(t, f, s);
  tn_ra(t, s);
  tn_tol(t, s);
  tn_hrc(t, s);
  tn_threads(t, f, s);
  tn_coat(t, f, s);
  if (tn_has(f, L"цемент")) s->heat |= TNH_CEMENT;
  if (tn_has(f, L"закал")) s->heat |= TNH_QUENCH;
  if (tn_has(f, L"улучш")) s->heat |= TNH_TEMPER;
  if (tn_has(f, L"нормализ") || tn_has(f, L"отжиг")) s->heat |= TNH_NORM;
  if (tn_has(f, L"азотир")) s->heat |= TNH_NITRID;
  if (tn_has(f, L"твч")) s->heat |= TNH_TVCH;
  /* «калить в заготовке» (Tesseract: «калить 6 заготовке») — ТО до механообработки */
  for (const wchar_t *p = f; (p = wcsstr(p, L"калит")) != NULL; p++)
    if (wcsstr(p, L"заготов") && wcsstr(p, L"заготов") - p < 20) s->heatFirst = 1;
  /* «4 отв.», «3 отб» (курсивное «в» Tesseract читает как «б»). Одну выноску читают не раз — модель
     и Tesseract в трёх поворотах, — поэтому одинаковые «n отв. <размер>» считаются однажды */
  int seen[24], nSeen = 0;
  for (const wchar_t *p = f; *p; p++)
    if (tn_isdig(*p) && (p == f || !tn_isdig(p[-1]))) {
      int v = 0;
      const wchar_t *q = p;
      while (tn_isdig(*q)) v = v * 10 + (*q++ - L'0');
      while (*q == L' ') q++;
      if (v > 0 && v <= 64 && q[0] == L'о' && q[1] == L'т' && (q[2] == L'в' || q[2] == L'б')) {
        /* «4 отв. Ø13» — диаметр после «отв.» (знак Ø Tesseract читает как ф, $, #, 0); «2 отв. M10» — резьба */
        const wchar_t *r2 = q + 3;
        while (*r2 == L'.' || *r2 == L' ') r2++;
        int thr = *r2 == L'м' && tn_isdig(r2[1]);
        if (thr || *r2 == L'ø' || *r2 == L'ф' || *r2 == L'$' || *r2 == L'#' || *r2 == L'Ø' || *r2 == L'⌀') r2++;
        double hd;
        int hl, hs;
        int sized = tn_num(r2, &hd, &hl, &hs) && hd >= 1 && hd <= 200;
        int key = v * 10000 + (sized ? (int)(hd * 10 + 0.5) : 0), dup = 0;
        for (int i = 0; i < nSeen && !dup; i++) dup = seen[i] == key;
        if (dup) continue;
        if (nSeen < 24) seen[nSeen++] = key;
        s->holes += v;
        if (sized && thr) {
          wchar_t m[16] = L"M";
          tn_catnum(m, 16, hd);
          if (!tn_list_has(s->thrHole, m)) tn_item(s->thrHole, TN_TXT, m);
        } else if (sized && (!s->holeMin || hd < s->holeMin)) {
          s->holeMin = hd;
        }
      }
    }
  s->edges = tn_has(f, L"притуп") || tn_has(f, L"заусен");
  /* «Маркировать»: Tesseract на курсиве — «Mapxupobamb», «Mapkypobamb» */
  s->mark = tn_has(f, L"маркир") || tn_has(f, L"мархир") || tn_has(f, L"маркур") || tn_has(f, L"клейм");
  s->weld = tn_has(f, L"свар") || tn_word(f, L"шов") || tn_has(f, L"5264") || tn_has(f, L"14771");
  /* «Развертка», «Линии сгиба»; курсивное «в» Tesseract читает «б»: «Разбертка» */
  s->bend = tn_has(f, L"гибк") || tn_has(f, L"гнут") || tn_has(f, L"развертк") || tn_has(f, L"разбертк") ||
            tn_has(f, L"розбертк") || tn_has(f, L"сгиб") || tn_has(f, L"линии гиб") || tn_has(f, L"линия гиб");
  s->key = tn_has(f, L"шпон");
  s->gear = tn_has(f, L"модуль") && tn_has(f, L"зуб");
  s->knurl = tn_has(f, L"накат") || tn_has(f, L"рифлен") || tn_has(f, L"сетчат"); /* «Рифление сетчатое 1,0 ГОСТ 21474» */
  s->edm = tn_has(f, L"эрозион") || tn_has(f, L"розион") || tn_has(f, L"электрораз");    /* «злектрозрозионным» */
  s->cutApart = tn_has(f, L"разрезать на");
  s->slot = tn_word(f, L"паз") || tn_has(f, L"пазов") || tn_has(f, L"паза");
  /* калибр, шаблон: исполнительные размеры ПР и НЕ, «изношенного», поверка раз в год */
  s->gauge = (tn_word(f, L"пр") && tn_word(f, L"не")) || tn_has(f, L"изношен") || tn_has(f, L"калибр") ||
             tn_has(f, L"шаблон");
  s->flat = tn_has(f, L"лыск");
  s->hex = tn_has(f, L"под ключ") || tn_has(f, L"шестигр");
  /* признаки вращения: Ø со знаком (уверенно — наибольший Ø), фаски ×45° */
  for (const wchar_t *p = t; *p; p++) {
    wchar_t c = *p;
    int sure = (c == L'Ø' || c == L'ø' || c == L'⌀' || c == L'Φ' || c == L'φ' || c == L'ф' || c == L'Ф');
    int maybe = (c == L'#' || c == L'$' || c == L'@');
    if (!sure && !maybe) continue;
    const wchar_t *q = p + 1;
    while (*q == L' ') q++;
    double v;
    int len, sep;
    if (!tn_num(q, &v, &len, &sep) || v < 1 || v > 2000 || (maybe && len < 2)) continue;
    s->rot++;
    if (sure && v > s->dmax) s->dmax = v;
  }
  for (const wchar_t *p = f; *p; p++)
    if (tn_isdig(*p) && (p[1] == L'х' || p[1] == L'×') && p[2] == L'4' && p[3] == L'5') s->rot++;
  for (const wchar_t *p = f; *p; p++)
    if (tn_isdig(*p) && p[1] == L' ' && (p[2] == L'х' || p[2] == L'×') && p[3] == L'4' && p[4] == L'5') s->rot++;
  /* наименование, если штампа нет: слово вида детали в именительном падеже */
  if (!s->name[0]) {
    static const wchar_t *const kw[] = {
        L"вал",     L"валик",   L"ось",     L"втулка",  L"палец",   L"штуцер",   L"гайка",   L"винт",
        L"болт",    L"шпилька", L"кольцо",  L"фланец",  L"шкив",    L"колесо",   L"шестерня", L"стакан",
        L"пробка",  L"ниппель", L"муфта",   L"ролик",   L"диск",    L"цапфа",    L"пластина", L"планка",
        L"косынка", L"накладка", L"скоба",  L"корпус",  L"плита",   L"основание", L"станина", L"рычаг",
        L"стойка",  L"кронштейн", L"крышка", L"удлинитель", L"шайба", L"штифт", L"оправка", L"упор",
        L"калибр",  L"шаблон",  L"щека",    L"бирка",   L"ребро",   L"пята",     L"угольник", L"боковина",
        L"рамка"};
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]) && !s->name[0]; i++)
      if (tn_word(f, kw[i]) || tn_word(fl, kw[i])) tn_cpy(s->name, TN_TXT, kw[i]);
  }
  free(t);
  free(f);
  free(fl);
}

/* ---- вид детали ---------------------------------------------------------------------- */

/* расстояние Дамерау–Левенштейна не больше 1: замена, вставка, удаление или перестановка соседних */
static int tn_dl1(const wchar_t *a, const wchar_t *b) {
  size_t la = wcslen(a), lb = wcslen(b), i = 0;
  if (la + 1 < lb || lb + 1 < la) return 0;
  while (i < la && i < lb && a[i] == b[i]) i++;
  if (i == la && i == lb) return 1;
  if (la == lb) {
    if (!wcscmp(a + i + 1, b + i + 1)) return 1;                                  /* замена */
    return i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i] && !wcscmp(a + i + 2, b + i + 2); /* перестановка */
  }
  return la > lb ? !wcscmp(a + i + 1, b + i) : !wcscmp(a + i, b + i + 1); /* вставка / удаление */
}

/* первое слово наименования, без кода обозначения впереди («АДЕ 3427 Оправка») */
static void tn_first_word(const wchar_t *f, wchar_t *first, int cap) {
  const wchar_t *p = f;
  for (;;) {
    while (p && *p && !tn_isletter(*p)) p++;
    int n = 0;
    while (p && *p && tn_isletter(*p) && n < cap - 1) first[n++] = *p++;
    first[n] = 0;
    const wchar_t *q = p;
    while (q && *q == L' ') q++;
    if (!n || !q || !tn_isdig(*q) || n > 4) return; /* за словом не номер — это и есть наименование */
    p = q;
    while (*p && !tn_isletter(*p)) p++;
  }
}

static int tn_name_cls(const wchar_t *name, wchar_t *word, int cap) {
  static const struct { const wchar_t *w; int cls; } kn[] = {
      {L"вал", TN_ROT},     {L"валик", TN_ROT},   {L"ось", TN_ROT},      {L"втулка", TN_ROT},
      {L"палец", TN_ROT},   {L"штуцер", TN_ROT},  {L"гайка", TN_ROT},    {L"винт", TN_ROT},
      {L"болт", TN_ROT},    {L"шпилька", TN_ROT}, {L"кольцо", TN_ROT},   {L"фланец", TN_ROT},
      {L"шкив", TN_ROT},    {L"колесо", TN_ROT},  {L"шестерня", TN_ROT}, {L"стакан", TN_ROT},
      {L"пробка", TN_ROT},  {L"ниппель", TN_ROT}, {L"муфта", TN_ROT},    {L"ролик", TN_ROT},
      {L"диск", TN_ROT},    {L"цапфа", TN_ROT},   {L"удлинитель", TN_ROT}, {L"пластина", TN_FLAT},
      {L"планка", TN_FLAT}, {L"косынка", TN_FLAT}, {L"накладка", TN_FLAT}, {L"скоба", TN_FLAT},
      {L"корпус", TN_BODY}, {L"плита", TN_BODY},  {L"основание", TN_BODY}, {L"станина", TN_BODY},
      {L"рычаг", TN_BODY},  {L"кронштейн", TN_BODY},
      /* по чертежам АДЕ 3424…3427 (2026): калибр-кольцо и пробка — точёные, шаблон — из листа;
         «стойка» бывает и валиком с резьбами, и корпусом — решают признаки */
      {L"шайба", TN_ROT},   {L"штифт", TN_ROT},   {L"оправка", TN_ROT},  {L"упор", TN_ROT},
      {L"калибр", TN_ROT},  {L"шаблон", TN_FLAT}, {L"щека", TN_FLAT},    {L"бирка", TN_FLAT},
      {L"ребро", TN_FLAT},  {L"пята", TN_FLAT},   {L"угольник", TN_FLAT}, {L"боковина", TN_FLAT},
      {L"рамка", TN_FLAT},  {L"дно", TN_FLAT}};
  wchar_t *f = tn_fold(name, 0);
  int cls = TN_UNK;
  /* первое слово — главное: «Крышка корпуса» — не корпус, «Удлинитель вала» — по «вала» нет */
  wchar_t first[40];
  first[0] = 0;
  if (f) tn_first_word(f, first, 40);
  for (size_t i = 0; f && i < sizeof(kn) / sizeof(kn[0]); i++)
    if (!wcscmp(first, kn[i].w)) cls = kn[i].cls, tn_cpy(word, cap, kn[i].w);
  /* не нашлось — одна ошибка чтения в слове из 5 букв и длиннее («Шайдба», «Тайка», «Орпавка») */
  for (size_t i = 0; f && cls == TN_UNK && wcslen(first) >= 4 && i < sizeof(kn) / sizeof(kn[0]); i++)
    if (wcslen(kn[i].w) >= 5 && tn_dl1(first, kn[i].w)) cls = kn[i].cls, tn_cpy(word, cap, kn[i].w);
  free(f);
  return cls;
}

static int tn_is_steel(int m) {
  return m == TNM_STEEL || m == TNM_STEEL_PLAIN || m == TNM_STEEL_ALLOY || m == TNM_STEEL_STAIN || m == TNM_STEEL_TOOL;
}

/* ГОСТ 2590-2006: диаметры круга, мм (ряд до 270) */
static const double kTnRound[] = {
    5,  5.5, 6,  6.3, 6.5, 7,  8,  9,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,
    24, 25,  26, 27,  28,  29, 30, 31, 32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,
    46, 47,  48, 50,  52,  53, 54, 55, 56,  58,  60,  62,  63,  65,  67,  68,  70,  72,  75,  78,  80,  82,
    85, 87,  90, 92,  95,  97, 100, 105, 110, 115, 120, 125, 130, 135, 140, 145, 150, 155, 160, 165, 170, 175,
    180, 185, 190, 195, 200, 210, 220, 230, 240, 250, 260, 270};

/* припуск на диаметр при обработке из проката (упрощённо, по справочнику технолога) */
static double tn_allow(double d) { return d <= 30 ? 2 : d <= 80 ? 3 : d <= 180 ? 4 : 5; }

static void tn_op(TnRoute *r, const wchar_t *op, const wchar_t *what, const wchar_t *why) {
  if (r->n >= 24) return;
  TnOp *o = &r->op[r->n++];
  tn_cpy(o->op, 48, op);
  tn_cpy(o->what, 300, what);
  tn_cpy(o->why, TN_TXT, why);
}

static void tn_hrc_txt(const TnSigns *s, wchar_t *out, int cap) {
  out[0] = 0;
  if (!s->hrcHi && s->hbHi) {
    tn_catnum(out, cap, s->hbLo);
    if (s->hbHi != s->hbLo) tn_cat(out, cap, L"…"), tn_catnum(out, cap, s->hbHi);
    tn_cat(out, cap, L" HB");
    return;
  }
  if (!s->hrcHi) return;
  tn_catnum(out, cap, s->hrcLo);
  if (s->hrcHi != s->hrcLo) tn_cat(out, cap, L"…"), tn_catnum(out, cap, s->hrcHi);
  tn_cat(out, cap, L" HRC");
}

static void tn_route(const wchar_t *name, const wchar_t *mat, const wchar_t *text, TnRoute *r) {
  memset(r, 0, sizeof(*r));
  TnSigns *s = &r->s;
  tn_signs(name, mat, text, s);
  wchar_t w[300], why[TN_TXT], hrc[40], ra[24];
  tn_hrc_txt(s, hrc, 40);
  ra[0] = 0;
  if (s->raMin) tn_cpy(ra, 24, L"Ra "), tn_catnum(ra, 24, s->raMin);

  /* 1. вид детали */
  wchar_t word[40] = {0};
  int byName = tn_name_cls(s->name, word, 40);
  wchar_t *nf = tn_fold(s->name, 0), *mf = tn_fold(mat ? mat : L"", 0);
  /* «Доработка кронштейна», материал «См. ТТ п.1» — деталь уже есть, заготовки нет */
  wchar_t nw[40] = {0};
  if (nf) tn_first_word(nf, nw, 40);
  s->rework = (nf && !wcsncmp(nf, L"доработк", 8)) || tn_dl1(nw, L"доработка") ||
              (mf && (tn_has(mf, L"см. тт") || tn_has(mf, L"см.тт")));
  free(nf), free(mf);
  /* пруток Ø с листа = Ø детали, ни резьб, ни допусков, ни чистых поверхностей: только отрезать */
  int asBar = s->sort == TNS_ROUND && s->sortSize && (!s->dmax || s->dmax == s->sortSize) && !s->nThr && !s->holes &&
              !s->band && !s->itMin && (!s->raMin || s->raMin >= 6.3) && byName != TN_ROT;
  if (s->rework) {
    r->cls = TN_REWORK;
    tn_cpy(r->clsWhy, TN_TXT, L"доработка: заготовка — готовая деталь (см. ТТ)");
  } else if (s->sort == TNS_PROF) {
    r->cls = TN_BAR;
    tn_cpy(r->clsWhy, TN_TXT, L"профильная труба: резка в размер");
  } else if ((s->sort == TNS_ROUND || s->sort == TNS_PIPE) && s->bend && s->rot < 3) {
    r->cls = TN_BENT;
    tn_cpy(r->clsWhy, TN_TXT, L"пруток и развёртка с линиями сгиба");
  } else if (asBar) {
    r->cls = TN_BAR;
    tn_cpy(r->clsWhy, TN_TXT, L"Ø детали равен Ø прутка, обрабатывать нечего — только отрезать");
  } else if (s->sort == TNS_ROUND || s->sort == TNS_HEX || s->sort == TNS_PIPE) {
    r->cls = TN_ROT;
    tn_cpy(r->clsWhy, TN_TXT, L"заготовка на чертеже — ");
    tn_cat(r->clsWhy, TN_TXT, kTnSort[s->sort]);
  } else if ((s->sort == TNS_SHEET || s->sort == TNS_STRIP) && !(byName == TN_BODY && s->sortSize >= 20)) {
    /* «Плита» из листа 8 мм — раскрой, а не фрезеровка корпуса */
    r->cls = TN_FLAT;
    tn_cpy(r->clsWhy, TN_TXT, L"заготовка на чертеже — лист");
  } else if (byName != TN_UNK) {
    r->cls = byName;
    tn_cpy(r->clsWhy, TN_TXT, L"наименование «");
    tn_cat(r->clsWhy, TN_TXT, word);
    tn_cat(r->clsWhy, TN_TXT, L"»");
    if (byName == TN_BODY && s->rot >= 3 && !s->thick) tn_cat(r->clsWhy, TN_TXT, L"; есть и поверхности вращения");
  } else if (s->sort == TNS_SHEET || s->sort == TNS_STRIP || s->thick) {
    r->cls = TN_FLAT;
    tn_cpy(r->clsWhy, TN_TXT, s->thick ? L"толщина листа S на чертеже" : L"заготовка на чертеже — лист");
  } else if (s->mat == TNM_IRON || s->mat == TNM_STEEL_CAST || s->mat == TNM_ALU_CAST) {
    r->cls = TN_BODY;
    tn_cpy(r->clsWhy, TN_TXT, L"литейный материал");
  } else if (s->rot >= 3 || s->knurl || s->thrOut) {
    /* рифление и наружная резьба «-8g» — только на точёной детали */
    r->cls = TN_ROT;
    tn_cpy(r->clsWhy, TN_TXT, s->rot >= 3 ? L"по признакам: диаметры, резьбы, фаски ×45°"
                              : s->knurl  ? L"по признакам: рифление (накатка)"
                                          : L"по признакам: наружная резьба");
    tn_item(r->check, 1200, L"вид детали определён по косвенным признакам — проверить");
  } else if (s->rot <= 1 && !s->nThr && s->mat) {
    r->cls = TN_FLAT;
    tn_cpy(r->clsWhy, TN_TXT, L"на листе нет ни Ø, ни резьб — призматическая, из листа или полосы");
    tn_item(r->check, 1200, L"вид детали определён по косвенным признакам — проверить");
  } else {
    r->cls = TN_UNK;
    tn_item(r->check, 1200, L"вид детали не определён: нет наименования и сортамента");
  }

  /* 2. заготовка */
  if (!s->mat && !s->grade[0] && !s->rework) tn_item(r->check, 1200, L"материал не прочитан");
  if (s->sort == TNS_SHEET && s->sortSize && s->thick && s->thick == s->sortSize * 10) s->thick = s->sortSize; /* «S=15*» — запятая потеряна */
  if (s->sort == TNS_SHEET && s->sortSize && s->thickSure && s->sortSize != s->thick) {
    /* «Лист 10» в штампе и «S12*» на поле — ошибка чертежа, а не распознавания */
    wchar_t c[120] = L"толщина: в штампе лист ";
    tn_catnum(c, 120, s->sortSize), tn_cat(c, 120, L", на поле S="), tn_catnum(c, 120, s->thick);
    tn_cat(c, 120, L" — уточнить у конструктора");
    tn_item(r->check, 1200, c);
  }
  if (r->cls == TN_REWORK) {
    tn_cpy(r->blank, TN_TXT, L"Готовая деталь");
    tn_cpy(r->blankWhy, TN_TXT, L"по ТТ: доработка имеющейся детали");
  } else if (s->sort) {
    tn_cpy(r->blank, TN_TXT, kTnSort[s->sort]);
    if (s->sortSize) tn_cat(r->blank, TN_TXT, L" "), tn_catnum(r->blank, TN_TXT, s->sortSize);
    else if (s->sortDim[0]) tn_cat(r->blank, TN_TXT, L" "), tn_cat(r->blank, TN_TXT, s->sortDim);
    else if (s->sort == TNS_SHEET && s->thick) tn_cat(r->blank, TN_TXT, L" S="), tn_catnum(r->blank, TN_TXT, s->thick);
    if (s->sortStd[0]) tn_cat(r->blank, TN_TXT, L" "), tn_cat(r->blank, TN_TXT, s->sortStd);
    tn_cpy(r->blankWhy, TN_TXT, L"указана на чертеже");
    r->blankRolled = 1;
  } else if (r->cls == TN_ROT) {
    if (s->mat == TNM_IRON || s->mat == TNM_STEEL_CAST || s->mat == TNM_ALU_CAST) {
      tn_cpy(r->blank, TN_TXT, L"Отливка");
      tn_cpy(r->blankWhy, TN_TXT, L"литейный материал");
    } else {
      r->blankRolled = 1;
      const wchar_t *std = s->mat == TNM_BRASS    ? L"Пруток ГОСТ 2060"
                           : s->mat == TNM_BRONZE ? L"Пруток ГОСТ 1628"
                           : s->mat == TNM_ALU    ? L"Пруток ГОСТ 21488"
                           : s->mat == TNM_COPPER ? L"Пруток ГОСТ 1535"
                           : s->mat == TNM_STEEL_STAIN ? L"Круг ГОСТ 2590 (5949)"
                           : s->mat == TNM_PLASTIC     ? L"Стержень"
                                                      : L"Круг ГОСТ 2590";
      tn_cpy(r->blank, TN_TXT, std);
      if (s->dmax && tn_is_steel(s->mat)) {
        double need = s->dmax + tn_allow(s->dmax), d = 0;
        for (size_t i = 0; i < sizeof(kTnRound) / sizeof(kTnRound[0]) && !d; i++)
          if (kTnRound[i] >= need) d = kTnRound[i];
        if (d) {
          tn_cpy(r->blank, TN_TXT, L"Круг ");
          tn_catnum(r->blank, TN_TXT, d);
          tn_cat(r->blank, TN_TXT, std + 4);
          tn_cpy(r->blankWhy, TN_TXT, L"наибольший Ø ");
          tn_catnum(r->blankWhy, TN_TXT, s->dmax);
          tn_cat(r->blankWhy, TN_TXT, L" + припуск ");
          tn_catnum(r->blankWhy, TN_TXT, tn_allow(s->dmax));
          tn_cat(r->blankWhy, TN_TXT, L" мм, ближайший больший по ГОСТ 2590");
        }
      }
      if (!r->blankWhy[0]) {
        tn_cpy(r->blankWhy, TN_TXT, L"тело вращения из проката; Ø — наибольший диаметр детали + 2…5 мм");
        tn_item(r->check, 1200, L"размер прутка: наибольший Ø на листе не прочитан");
      }
      if (s->dmax > 160) tn_item(r->check, 1200, L"при большом перепаде диаметров выгоднее поковка");
    }
  } else if (r->cls == TN_FLAT) {
    r->blankRolled = 1;
    const wchar_t *std = s->mat == TNM_ALU          ? L"Лист ГОСТ 21631"
                         : s->mat == TNM_STEEL_STAIN ? (s->thick && s->thick < 4 ? L"Лист ГОСТ 5582" : L"Лист ГОСТ 7350")
                         : s->thick && s->thick < 4 ? L"Лист ГОСТ 19904"
                                                    : L"Лист ГОСТ 19903";
    tn_cpy(r->blank, TN_TXT, std);
    if (s->thick) tn_cat(r->blank, TN_TXT, L", S="), tn_catnum(r->blank, TN_TXT, s->thick);
    tn_cpy(r->blankWhy, TN_TXT, L"плоская деталь");
    if (!s->thick) tn_item(r->check, 1200, L"толщина листа не прочитана");
  } else if (r->cls == TN_BODY) {
    if (s->mat == TNM_IRON || s->mat == TNM_STEEL_CAST || s->mat == TNM_ALU_CAST) {
      tn_cpy(r->blank, TN_TXT, L"Отливка");
      tn_cpy(r->blankWhy, TN_TXT, L"литейный материал");
    } else if (s->weld) {
      tn_cpy(r->blank, TN_TXT, L"Сварная заготовка (детали из листа и проката)");
      tn_cpy(r->blankWhy, TN_TXT, L"на чертеже сварка");
    } else {
      r->blankRolled = 1;
      tn_cpy(r->blank, TN_TXT, s->rot >= 3 ? L"Круг ГОСТ 2590 или поковка" : L"Плита/толстый лист ГОСТ 19903 или поковка");
      tn_cpy(r->blankWhy, TN_TXT, L"корпусная из проката");
      tn_item(r->check, 1200, L"заготовка корпусной детали — выбрать по серийности и размерам");
    }
  } else {
    tn_cpy(r->blank, TN_TXT, L"—");
    tn_item(r->check, 1200, L"заготовка не выбрана");
  }

  /* 3. термообработка: где в маршруте */
  int steel = tn_is_steel(s->mat) || s->mat == TNM_STEEL_CAST;
  int cement = (s->heat & TNH_CEMENT) || (steel && s->hrcLo >= 50 && s->carbon && s->carbon <= 0.25);
  /* «30…35 HRC» — ещё улучшение (резец берёт), «35…40» — уже закалка; HB до 350 — улучшение */
  int harden = !cement && ((s->heat & (TNH_QUENCH | TNH_TVCH)) || (s->hrcLo >= 35 && s->hrcHi > 35) ||
                           (!s->hrcHi && s->hbHi > 350));
  int temper = !cement && !harden &&
               ((s->heat & TNH_TEMPER) || (s->hrcHi && s->hrcHi <= 35) || (!s->hrcHi && s->hbHi && s->hbHi <= 350));
  int hard = cement || harden;
  int grind = (s->raMin && s->raMin <= 0.8) || (s->itMin && s->itMin <= 6) || (s->band && s->band <= 0.02) ||
              (hard && s->hrcHi >= 45) ||
              (hard && ((s->raMin && s->raMin <= 1.6) || (s->itMin && s->itMin <= 7) || (s->band && s->band <= 0.03)));
  /* цветные металлы не шлифуют — круг засаливается: тонкое (алмазное) точение */
  int fineTurn = grind && (s->mat == TNM_ALU || s->mat == TNM_ALU_CAST || s->mat == TNM_COPPER ||
                           s->mat == TNM_BRASS || s->mat == TNM_BRONZE || s->mat == TNM_PLASTIC);
  if (fineTurn) grind = 0;
  /* «калить в заготовке» — ТО сразу после отрезки, потом обработка уже твёрдого металла */
  int heatFirst = s->heatFirst && (harden || temper);
  if (s->raDoubt && s->raMin <= 0.8) tn_item(r->check, 1200, L"Ra прочитана без запятой — проверить шероховатость");
  if (!s->nRa && r->cls != TN_REWORK && r->cls != TN_BAR && r->cls != TN_BENT) tn_item(r->check, 1200, L"шероховатость не прочитана — проверить, нужна ли шлифовка");
  if (s->hrcHi && !steel && s->mat) tn_item(r->check, 1200, L"твёрдость указана, но материал не сталь — проверить");

  /* 4. операции */
  if (r->cls == TN_BAR || r->cls == TN_BENT) {
    tn_cpy(w, 300, r->cls == TN_BENT ? L"Отрезать пруток по длине развёртки"
                                     : L"Отрезать в размер на ленточнопильном станке; торцы под углом — по чертежу");
    tn_op(r, L"Заготовительная", w, r->cls == TN_BENT ? L"развёртка на чертеже" : L"заготовка — прокат в размер детали");
  } else if (r->blankRolled && r->cls != TN_FLAT) {
    tn_cpy(w, 300, r->cls == TN_ROT ? L"Отрезать заготовку от прутка: длина детали + припуск на подрезку торцов"
                                    : L"Отрезать (вырезать) заготовку с припуском на обработку");
    tn_op(r, L"Заготовительная", w, L"заготовка — прокат");
  }
  if (s->heat & TNH_NORM) tn_op(r, L"Термическая", L"Нормализация (отжиг) заготовки", L"ТТ: нормализация/отжиг");
  if (heatFirst) {
    tn_cpy(w, 300, harden ? L"Закалка и отпуск заготовки" : L"Улучшение заготовки");
    if (hrc[0]) tn_cat(w, 300, L" до "), tn_cat(w, 300, hrc);
    tn_op(r, L"Термическая", w, L"ТТ: калить в заготовке — до механообработки");
    harden = temper = 0;
  }

  if (r->cls == TN_REWORK) {
    tn_op(r, L"Слесарная (разметка)", L"Разметить по чертежу на готовой детали", L"доработка по месту");
    tn_op(r, L"Сверлильная", s->slot ? L"Сверлить отверстия; пазы — фрезеровать или сверлить с распиловкой"
                                     : L"Сверлить отверстия по разметке",
          L"ТТ: разметка и сверление");
  } else if (r->cls == TN_BENT) {
    tn_op(r, L"Гибочная", L"Гнуть по линиям сгиба (на трубогибе или в приспособлении), радиусы — по чертежу",
          L"на чертеже развёртка и линии сгиба");
  } else if (r->cls == TN_ROT) {
    int two = temper || hard || grind;
    tn_op(r, two ? L"Токарная (черновая)" : L"Токарная",
          two ? L"Подрезать торцы, точить наружные поверхности и обработать отверстия с припуском под чистовую"
              : L"Подрезать торцы, точить наружные поверхности и отверстия начисто, фаски, канавки",
          r->clsWhy[0] ? L"тело вращения" : L"");
    if (temper) {
      tn_cpy(w, 300, L"Улучшение (закалка и высокий отпуск)");
      if (hrc[0]) tn_cat(w, 300, L" до "), tn_cat(w, 300, hrc);
      tn_op(r, L"Термическая", w, L"твёрдость невысокая — ТО до чистовой, чтобы поводки ушли в припуск");
    }
    if (two) {
      tn_cpy(w, 300, L"Точить начисто, фаски, канавки");
      if (grind) tn_cat(w, 300, L"; под шлифование оставить припуск 0,2…0,4 мм на диаметр");
      tn_op(r, L"Токарная (чистовая)", w, grind ? L"точные поверхности — потом шлифование" : L"после ТО");
    }
    if (fineTurn)
      tn_op(r, L"Токарная (тонкая)", L"Тонкое (алмазное) точение точных поверхностей",
            L"точная поверхность у цветного металла — шлифовать нельзя");
    if (s->knurl) tn_op(r, L"Токарная (накатка)", L"Накатать рифление", heatFirst ? L"на чертеже рифление" : L"на чертеже накатка (рифление) — до закалки");
    wchar_t turn[TN_TXT];
    tn_thr_turn(s, turn, TN_TXT);
    if (turn[0]) {
      tn_cpy(w, 300, L"Нарезать резьбу ");
      tn_cat(w, 300, turn);
      tn_cat(w, 300, L" (на токарном станке; внутреннюю — метчиком или резцом)");
      tn_cpy(why, TN_TXT, L"на чертеже резьба ");
      tn_cat(why, TN_TXT, turn);
      /* резьба — переход токарной, но отдельной строкой, чтобы её не потерять */
      tn_op(r, L"Токарная (резьба)", w, why);
    }
  } else if (r->cls == TN_FLAT) {
    tn_cpy(w, 300, L"Вырезать деталь из листа");
    if (s->thick) tn_cat(w, 300, L" S="), tn_catnum(w, 300, s->thick);
    tn_cat(w, 300, L": лазерная или плазменная резка по контуру");
    if (s->holes) tn_cat(w, 300, L" вместе с отверстиями");
    tn_cat(w, 300, L"; прямоугольную без отверстий — на гильотине");
    tn_op(r, L"Заготовительная (раскрой)", w, L"листовая деталь");
    if (s->bend) tn_op(r, L"Гибочная", L"Гнуть по чертежу (развёртка — по месту гибов)", L"на чертеже гибка");
  } else if (r->cls == TN_BODY) {
    tn_op(r, L"Фрезерная", L"Фрезеровать базовые плоскости, затем остальные плоскости, пазы, уступы",
          L"корпусная деталь: сначала базы");
    if (s->rot >= 3)
      tn_op(r, L"Токарная", L"Обработать поверхности вращения: диаметры, торцы, фаски", L"есть Ø, фаски ×45°, резьбы");
  } else if (r->cls == TN_UNK) {
    tn_op(r, L"Механическая обработка", L"Вид обработки уточнить по виду детали", L"вид детали не определён");
  }

  if (s->weld && r->cls != TN_BODY) tn_op(r, L"Сварочная", L"Сварить по чертежу", L"на чертеже сварка");
  if (s->edm) s->gear = 0; /* шлицы по ТТ — электроэрозией, не зубофрезерованием */
  if (s->key || s->flat || s->hex || s->gear) {
    w[0] = why[0] = 0;
    if (s->key) tn_item(w, 300, L"фрезеровать шпоночный паз"), tn_item(why, TN_TXT, L"шпоночный паз");
    if (s->flat) tn_item(w, 300, L"фрезеровать лыски"), tn_item(why, TN_TXT, L"лыски");
    if (s->hex) tn_item(w, 300, L"фрезеровать шестигранник под ключ"), tn_item(why, TN_TXT, L"размер под ключ");
    if (s->gear) tn_item(w, 300, L"нарезать зубья"), tn_item(why, TN_TXT, L"зубчатый венец");
    if (w[0] >= L'а' && w[0] <= L'я') w[0] = (wchar_t)(w[0] - 32);
    tn_op(r, s->gear && !s->key && !s->flat && !s->hex ? L"Зубофрезерная" : L"Фрезерная", w, why);
  }
  int precHoles = (s->band && s->band <= 0.05) || (s->itMin && s->itMin <= 9);
  double sheetS = s->thick ? s->thick : s->sort == TNS_SHEET ? s->sortSize : 0;
  if (r->cls == TN_REWORK || r->cls == TN_BAR || r->cls == TN_BENT) {
    /* отверстия доработки — выше; у проката в размер своих отверстий нет */
  } else if (s->holes && r->cls != TN_FLAT) {
    tn_cpy(w, 300, L"Сверлить ");
    tn_catnum(w, 300, s->holes);
    tn_cat(w, 300, L" отв.");
    if (precHoles) tn_cat(w, 300, L"; точные — сверлить и развернуть");
    /* у тела вращения резьба — на токарной, кроме резьбовых отверстий («2 отв. M10» во фланце);
       у корпуса и листа — вся метчиком в отверстиях */
    if (s->nThr && r->cls != TN_ROT) tn_cat(w, 300, L"; нарезать резьбу "), tn_cat(w, 300, s->thr);
    else if (s->thrHole[0]) tn_cat(w, 300, L" под резьбу, нарезать резьбу "), tn_cat(w, 300, s->thrHole), tn_cat(w, 300, L" метчиком");
    tn_cpy(why, TN_TXT, L"на чертеже «");
    tn_catnum(why, TN_TXT, s->holes);
    tn_cat(why, TN_TXT, L" отв.»");
    if (s->thrHole[0] && r->cls == TN_ROT) tn_cat(why, TN_TXT, L" — резьба в отверстиях, не на токарной");
    tn_op(r, L"Сверлильная", w, why);
  } else if (s->nThr && r->cls != TN_ROT) {
    tn_cpy(w, 300, L"Сверлить отверстия под резьбу, нарезать резьбу ");
    tn_cat(w, 300, s->thr);
    tn_cpy(why, TN_TXT, L"на чертеже резьба ");
    tn_cat(why, TN_TXT, s->thr);
    tn_op(r, L"Сверлильная", w, why);
  } else if (r->cls == TN_FLAT && s->holes && s->holeMin && sheetS && s->holeMin < sheetS) {
    tn_cpy(w, 300, L"Сверлить отверстия Ø");
    tn_catnum(w, 300, s->holeMin);
    tn_cat(w, 300, L" и меньше толщины листа (лазером и плазмой такие не вырезают)");
    tn_op(r, L"Сверлильная", w, L"отверстие меньше толщины листа");
  } else if (r->cls == TN_FLAT && precHoles && s->holes) {
    tn_op(r, L"Сверлильная", L"Сверлить и развернуть точные отверстия", L"отверстия с допуском");
  }
  if (cement) {
    tn_cpy(w, 300, L"Цементация, закалка и низкий отпуск");
    if (hrc[0]) tn_cat(w, 300, L" до "), tn_cat(w, 300, hrc);
    tn_op(r, L"Термическая", w,
          s->heat & TNH_CEMENT ? L"ТТ: цементировать" : L"низкоуглеродистая сталь и высокая твёрдость — цементация");
  } else if (harden) {
    tn_cpy(w, 300, s->heat & TNH_TVCH ? L"Закалка ТВЧ и отпуск" : L"Закалка и отпуск");
    if (hrc[0]) tn_cat(w, 300, L" до "), tn_cat(w, 300, hrc);
    tn_op(r, L"Термическая", w, L"высокая твёрдость — после механообработки, перед шлифованием");
  }
  if (s->heat & TNH_NITRID) tn_op(r, L"Термическая", L"Азотирование", L"ТТ: азотировать");
  if (s->edm)
    tn_op(r, L"Электроэрозионная", L"Обработать шлицы (профиль) электроэрозией — проволокой или прошивкой",
          L"ТТ: электроэрозионным способом; по закалённому металлу");
  if (grind) {
    why[0] = 0;
    if (s->raMin && s->raMin <= 1.6) tn_item(why, TN_TXT, ra);
    if (s->band && s->band <= 0.03) {
      wchar_t b[40] = L"допуск ";
      tn_catnum(b, 40, s->band);
      tn_cat(b, 40, L" мм");
      tn_item(why, TN_TXT, b);
    }
    if (s->itMin && s->itMin <= 7) {
      wchar_t b[24] = L"квалитет ";
      tn_catnum(b, 24, s->itMin);
      tn_item(why, TN_TXT, b);
    }
    if (hard) tn_item(why, TN_TXT, L"после закалки резцом не взять");
    tn_cpy(w, 300,
           r->cls == TN_ROT   ? L"Шлифовать точные поверхности: наружные — круглым, отверстия — внутренним шлифованием"
           : r->cls == TN_UNK ? L"Шлифовать точные поверхности"
                              : L"Шлифовать точные плоскости");
    if (s->raMin && s->raMin < 0.4) tn_cat(w, 300, L"; Ra ниже 0,4 — тонкое шлифование или полирование");
    tn_op(r, r->cls == TN_ROT ? L"Круглошлифовальная" : r->cls == TN_UNK ? L"Шлифовальная" : L"Плоскошлифовальная", w,
          why);
  } else if (r->cls == TN_FLAT && s->raMin && s->raMin <= 3.2) {
    tn_op(r, L"Фрезерная", L"Фрезеровать плоскости с заданной шероховатостью", ra);
  }
  if (s->knurl && r->cls != TN_ROT) tn_op(r, L"Токарная (накатка)", L"Накатать рифление", L"на чертеже накатка");
  if (s->gauge && hard && grind && r->cls != TN_REWORK)
    tn_op(r, L"Доводочная", L"Довести рабочие поверхности (ПР/НЕ) до исполнительных размеров", L"калибр/шаблон");
  if (s->cutApart) tn_op(r, L"Отрезная", L"Разрезать деталь на части по ТТ", L"ТТ: по окончании обработки разрезать");
  tn_op(r, L"Слесарная",
        r->cls == TN_FLAT ? L"Зачистить заусенцы и окалину после резки, притупить острые кромки"
                          : L"Притупить острые кромки, снять заусенцы",
        s->edges ? L"ТТ: притупить кромки" : r->cls == TN_FLAT ? L"после резки" : L"после механообработки");
  if (s->mark) tn_op(r, L"Маркировочная", L"Маркировать по ТТ", L"ТТ: маркировать");
  if (s->coatKind) {
    tn_cpy(w, 300, L"Нанести покрытие: ");
    tn_cat(w, 300, s->coat);
    tn_op(r, s->coatKind == TNC_PAINT ? L"Окрасочная" : s->coatKind == TNC_ANOD ? L"Анодирование" : L"Гальваническая", w,
          L"ТТ: покрытие");
  }
  tn_cpy(w, 300, L"Проверить размеры");
  if (s->nRa) tn_cat(w, 300, L", шероховатость");
  if (hrc[0]) tn_cat(w, 300, L", твёрдость ");
  if (hrc[0]) tn_cat(w, 300, hrc);
  if (s->coatKind) tn_cat(w, 300, L", покрытие");
  tn_op(r, L"Контрольная", w, L"");
}

/* текст для чата и окна чертежа */
static void tn_text(const TnRoute *r, wchar_t *out, int cap) {
  const TnSigns *s = &r->s;
  out[0] = 0;
  tn_cat(out, cap, L"Маршрут обработки — черновик по правилам, проверяет технолог\n\n");
  tn_cat(out, cap, L"Деталь: ");
  tn_cat(out, cap, s->name[0] ? s->name : L"(наименование не прочитано)");
  tn_cat(out, cap, L" — ");
  tn_cat(out, cap, kTnCls[r->cls]);
  if (r->clsWhy[0]) tn_cat(out, cap, L" ("), tn_cat(out, cap, r->clsWhy), tn_cat(out, cap, L")");
  tn_cat(out, cap, L"\nМатериал: ");
  if (s->grade[0] || s->matStd[0]) {
    tn_cat(out, cap, s->grade);
    if (s->matStd[0]) tn_cat(out, cap, s->grade[0] ? L" " : L""), tn_cat(out, cap, s->matStd);
    if (s->mat) tn_cat(out, cap, L" — "), tn_cat(out, cap, kTnMat[s->mat]);
  } else {
    tn_cat(out, cap, L"не прочитан");
  }
  tn_cat(out, cap, L"\nЗаготовка: ");
  tn_cat(out, cap, r->blank);
  if (r->blankWhy[0]) tn_cat(out, cap, L" ("), tn_cat(out, cap, r->blankWhy), tn_cat(out, cap, L")");
  wchar_t sg[600] = {0}, t[80];
  if (s->raMin) tn_cpy(t, 80, L"Ra от "), tn_catnum(t, 80, s->raMin), tn_item(sg, 600, t);
  if (s->band) tn_cpy(t, 80, L"допуск от "), tn_catnum(t, 80, s->band), tn_cat(t, 80, L" мм"), tn_item(sg, 600, t);
  if (s->itMin) tn_cpy(t, 80, L"квалитет "), tn_catnum(t, 80, s->itMin), tn_item(sg, 600, t);
  if (s->hrcHi) tn_hrc_txt(s, t, 80), tn_item(sg, 600, t);
  if (s->nThr) tn_cpy(t, 80, L"резьба "), tn_cat(t, 80, s->thr), tn_item(sg, 600, t);
  if (s->holes) t[0] = 0, tn_catnum(t, 80, s->holes), tn_cat(t, 80, L" отв."), tn_item(sg, 600, t);
  if (s->coat[0]) tn_cpy(t, 80, L"покрытие: "), tn_cat(t, 80, s->coat), tn_item(sg, 600, t);
  if (sg[0]) tn_cat(out, cap, L"\nПрочитано на листе: "), tn_cat(out, cap, sg);
  tn_cat(out, cap, L"\n");
  for (int i = 0; i < r->n; i++) {
    wchar_t num[8];
    int v = (i + 1) * 5;
    num[0] = L'0' + v / 100, num[1] = L'0' + v / 10 % 10, num[2] = L'0' + v % 10, num[3] = 0;
    tn_cat(out, cap, L"\n");
    tn_cat(out, cap, num);
    tn_cat(out, cap, L" ");
    tn_cat(out, cap, r->op[i].op);
    tn_cat(out, cap, L". ");
    tn_cat(out, cap, r->op[i].what);
    if (r->op[i].why[0]) tn_cat(out, cap, L" — "), tn_cat(out, cap, r->op[i].why);
  }
  if (r->check[0]) tn_cat(out, cap, L"\n\nУточнить: "), tn_cat(out, cap, r->check);
}
