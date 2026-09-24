/* ---- Расчёт резки и газов: логика --------------------------------------------

   Точный перенос расчёта со страницы cutting-calculator.html (giriaja-hall):
   те же нормы (cutting_norms.h, из make_cutting.py), тот же выбор технологий
   по толщине, та же интерполяция и те же числа в тексте. Без Windows —
   сверяется со страницей тестом cutting_test (node + gcc, см. README).

   null из таблицы — CUT_NA (NaN); проверка — cut_na(). */

#include <math.h>
#define CUT_NA NAN
#include "cutting_norms.h"

#define CUT_NNORMS ((int)(sizeof(kCutNorms) / sizeof(kCutNorms[0])))
#define CUT_NMATS ((int)(sizeof(kCutMaterials) / sizeof(kCutMaterials[0])))

static int cut_na(double v) { return isnan(v); }

/* индексы полей, как NUM_FIELDS / STR_FIELDS на странице */
enum { CN_GRIND, CN_RAG, CN_LNOZ, CN_PCATH, CN_PNOZ, CN_PTIP, CN_MOUTH, CN_N2, CN_AR, CN_H2, CN_MIX, CN_O2,
       CN_PROP, CN_SPEED };
enum { CS_EQUIP, CS_GRIND, CS_LNOZ, CS_PCATH, CS_PNOZ, CS_PTIP, CS_MOUTH };

/* ---- числа как в браузере ------------------------------------------------ */

/* Кратчайшая запись числа, которая читается обратно тем же числом (так
   число пишет JavaScript): цифры без точки и порядок. 1.005 → "1005", exp 0. */
static void cut_shortest(double x, char *digits, int *exp10) {
  char buf[40];
  for (int p = 1; p <= 17; p++) {
    snprintf(buf, sizeof(buf), "%.*e", p - 1, x);
    if (strtod(buf, NULL) == x) break;
  }
  /* buf: d.ddde±XX */
  int n = 0;
  const char *s = buf;
  for (; *s && *s != 'e'; s++)
    if (*s >= '0' && *s <= '9') digits[n++] = *s;
  digits[n] = 0;
  *exp10 = atoi(s + 1);
  while (n > 1 && digits[n - 1] == '0') digits[--n] = 0;
}

static void cut_ascii_w(const char *s, wchar_t *out, int cap) {
  int i = 0;
  for (; s[i] && i < cap - 1; i++) out[i] = (wchar_t)(unsigned char)s[i];
  out[i] = 0;
}

/* как Number.prototype.toString() для обычных величин: 1.25, 2, 0.09 */
static void cut_js_num(double x, wchar_t *out, int cap) {
  if (cut_na(x)) { /* speed ?? '—' */
    out[0] = 0x2014;
    out[1] = 0;
    return;
  }
  char d[40], s[80];
  int e, n = 0;
  if (x < 0) {
    s[n++] = '-';
    x = -x;
  }
  if (x == 0) {
    s[n++] = '0';
  } else {
    cut_shortest(x, d, &e);
    int nd = (int)strlen(d);
    if (e < -6 || e >= 21) { /* как JS: меньше 1e-6 и от 1e21 — 4.44e-16, 1e+21 */
      s[n++] = d[0];
      if (nd > 1) {
        s[n++] = '.';
        for (int i = 1; i < nd; i++) s[n++] = d[i];
      }
      n += snprintf(s + n, sizeof(s) - n, "e%c%d", e < 0 ? '-' : '+', e < 0 ? -e : e);
    } else if (e < 0) {
      s[n++] = '0';
      s[n++] = '.';
      for (int i = 0; i < -e - 1; i++) s[n++] = '0';
      for (int i = 0; i < nd; i++) s[n++] = d[i];
    } else {
      for (int i = 0; i <= e; i++) s[n++] = i < nd ? d[i] : '0';
      if (nd > e + 1) {
        s[n++] = '.';
        for (int i = e + 1; i < nd; i++) s[n++] = d[i];
      }
    }
  }
  s[n] = 0;
  cut_ascii_w(s, out, cap);
}

/* как toLocaleString('ru-RU', {maximumFractionDigits: dd}): запятая, тысячи
   через неразрывный пробел, округление половины вверх по десятичной записи */
static void cut_fmt(double x, int dd, wchar_t *out, int cap) {
  if (cut_na(x)) {
    swprintf(out, cap, L"—");
    return;
  }
  int neg = x < 0;
  if (neg) x = -x;
  char d[40];
  int e;
  cut_shortest(x, d, &e);
  if (x == 0) {
    d[0] = '0';
    d[1] = 0;
    e = 0;
  }
  /* все цифры числа по разрядам: целая часть и dd знаков дроби */
  int ip = e >= 0 ? e + 1 : 1; /* цифр в целой части */
  char dig[80];
  int nd = (int)strlen(d), total = ip + dd;
  if (total > 70) total = 70;
  for (int i = 0; i < total; i++) {
    int k = e >= 0 ? i : i - (-e) + 1; /* номер цифры в d для позиции i */
    /* позиция i соответствует разряду 10^(ip-1-i); цифра d[j] — разряду 10^(e-j) */
    int j = e - (ip - 1 - i);
    (void)k;
    dig[i] = (j >= 0 && j < nd) ? d[j] : '0';
  }
  /* следующая цифра после последней оставляемой решает округление */
  int jn = e - (ip - 1 - total);
  int up = jn >= 0 && jn < nd && d[jn] >= '5';
  int carry = up;
  for (int i = total - 1; i >= 0 && carry; i--) {
    if (dig[i] == '9') {
      dig[i] = '0';
    } else {
      dig[i]++;
      carry = 0;
    }
  }
  char s[90];
  int n = 0, lead = carry;
  int intDigits = ip + lead;
  char ib[80];
  int in = 0;
  if (lead) ib[in++] = '1';
  for (int i = 0; i < ip; i++) ib[in++] = dig[i];
  /* ведущие нули целой части убрать (0,5 → «0», а не «00») */
  int st = 0;
  while (st < in - 1 && ib[st] == '0') st++;
  intDigits = in - st;
  wchar_t w[120];
  int wn = 0;
  int isZero = 1;
  for (int i = st; i < in; i++)
    if (ib[i] != '0') isZero = 0;
  int fl = dd;
  while (fl > 0 && dig[ip + fl - 1] == '0') fl--;
  for (int i = 0; i < fl; i++)
    if (dig[ip + i] != '0') isZero = 0;
  if (neg && !isZero) w[wn++] = L'-';
  for (int i = 0; i < intDigits; i++) {
    if (i > 0 && (intDigits - i) % 3 == 0 && intDigits >= 4) w[wn++] = 0x00A0;
    w[wn++] = (wchar_t)ib[st + i];
  }
  if (fl > 0) {
    w[wn++] = L',';
    for (int i = 0; i < fl; i++) w[wn++] = (wchar_t)dig[ip + i];
  }
  w[wn] = 0;
  (void)s;
  (void)n;
  swprintf(out, cap, L"%ls", w);
}

static void cut_fmt_time(double min, wchar_t *out, int cap) {
  if (cut_na(min) || !isfinite(min) || min <= 0) {
    swprintf(out, cap, L"—");
    return;
  }
  if (min < 1) {
    swprintf(out, cap, L"%.0f с", floor(min * 60 + 0.5));
    return;
  }
  if (min < 60) {
    wchar_t f[40];
    cut_fmt(min, 1, f, 40);
    swprintf(out, cap, L"%ls мин", f);
    return;
  }
  double h = floor(min / 60), m = floor(fmod(min, 60) + 0.5);
  if (m != 0) swprintf(out, cap, L"%.0f ч %.0f мин", h, m);
  else swprintf(out, cap, L"%.0f ч", h);
}

/* ---- расчёт --------------------------------------------------------------- */

static const wchar_t *const kCutKindOrder[] = {L"Лазер", L"Плазма", L"Газовая резка", L"Гидроабразив",
                                               L"Прочее", L"—"};
static int cut_kind_rank(const wchar_t *k) {
  for (int i = 0; i < 6; i++)
    if (!wcscmp(k, kCutKindOrder[i])) return i;
  return -1; /* как indexOf на странице */
}

typedef struct {
  const wchar_t *kind;
  int idx[CUT_NNORMS > 0 ? CUT_NNORMS : 1]; /* нормы этого вида в порядке таблицы */
  int n;
  double minT, maxT;
} CutGroup;

typedef struct {
  const wchar_t *key, *label, *detail, *unit;
  double ratePerM, quantity, ratePerMVol, quantityVol, rho;
  int group; /* 0 оснастка, 1 газ, 2 вспомогательное */
  wchar_t detailBuf[64];
} CutLine;

typedef struct {
  const wchar_t *kind;
  double thickness;
  const wchar_t *str[CUT_STR];
  double num[CUT_NUM];
  int mode; /* 0 exact, 1 single, 2 extra, 3 interp */
  double from, to, minT, maxT;
  double lengthM, lengthMm, timeMin; /* timeMin — CUT_NA, если нет скорости */
  int incomplete;
  CutLine lines[16];
  int nlines;
} CutMethod;

typedef struct {
  const wchar_t *material;
  double thickness, lengthM, lengthMm;
  CutMethod m[6];
  int n;
} CutPack;

static double cut_lerp(double a, double b, double u) {
  if (cut_na(a) && cut_na(b)) return CUT_NA;
  if (cut_na(a)) return b;
  if (cut_na(b)) return a;
  double v = a + (b - a) * u;
  return v < 0 ? 0 : v;
}

/* группы по видам резки: порядок первого появления, потом KIND_ORDER (сортировка устойчивая) */
static int cut_groups(const wchar_t *mat, CutGroup *g) {
  int ng = 0;
  for (int i = 0; i < CUT_NNORMS; i++) {
    if (wcscmp(kCutNorms[i].material, mat)) continue;
    int k = -1;
    for (int q = 0; q < ng; q++)
      if (!wcscmp(g[q].kind, kCutNorms[i].kind)) k = q;
    if (k < 0) {
      k = ng++;
      g[k].kind = kCutNorms[i].kind;
      g[k].n = 0;
    }
    g[k].idx[g[k].n++] = i;
  }
  for (int q = 0; q < ng; q++) {
    g[q].minT = g[q].maxT = kCutNorms[g[q].idx[0]].thickness;
    for (int j = 1; j < g[q].n; j++) {
      double t = kCutNorms[g[q].idx[j]].thickness;
      if (t < g[q].minT) g[q].minT = t;
      if (t > g[q].maxT) g[q].maxT = t;
    }
  }
  for (int a = 1; a < ng; a++) /* вставками — устойчиво */
    for (int b = a; b > 0 && cut_kind_rank(g[b - 1].kind) > cut_kind_rank(g[b].kind); b--) {
      CutGroup t = g[b];
      g[b] = g[b - 1];
      g[b - 1] = t;
    }
  return ng;
}

static double cut_gdist(const CutGroup *g, double t) {
  double a = fabs(t - g->minT), b = fabs(t - g->maxT);
  return a < b ? a : b;
}

static int cut_pick(CutGroup *g, int ng, double t, int *pick) {
  int np = 0;
  for (int q = 0; q < ng; q++)
    if (t >= g[q].minT - 1e-9 && t <= g[q].maxT + 1e-9) pick[np++] = q;
  if (np) return np;
  int ord[8];
  for (int q = 0; q < ng; q++) ord[q] = q;
  for (int a = 1; a < ng; a++)
    for (int b = a; b > 0 && cut_gdist(&g[ord[b - 1]], t) > cut_gdist(&g[ord[b]], t); b--) {
      int x = ord[b];
      ord[b] = ord[b - 1];
      ord[b - 1] = x;
    }
  if (ng > 0) pick[np++] = ord[0];
  for (int q = 1; q < ng; q++)
    if (cut_gdist(&g[ord[q]], t) <= 4) pick[np++] = ord[q];
  return np;
}

static void cut_build(const CutGroup *g, double t, CutMethod *m) {
  int s[CUT_NNORMS > 0 ? CUT_NNORMS : 1] = {0}, n = g->n;
  for (int i = 0; i < n; i++) s[i] = g->idx[i];
  for (int a = 1; a < n; a++)
    for (int b = a; b > 0 && kCutNorms[s[b - 1]].thickness > kCutNorms[s[b]].thickness; b--) {
      int x = s[b];
      s[b] = s[b - 1];
      s[b - 1] = x;
    }
  const CutNorm *A, *B;
  m->thickness = t;
  m->minT = kCutNorms[s[0]].thickness;
  m->maxT = kCutNorms[s[n - 1]].thickness;
  for (int i = 0; i < n; i++)
    if (fabs(kCutNorms[s[i]].thickness - t) < 1e-6) {
      A = &kCutNorms[s[i]];
      memcpy(m->str, A->str, sizeof(m->str));
      memcpy(m->num, A->num, sizeof(m->num));
      m->mode = 0;
      m->from = m->to = A->thickness;
      return;
    }
  if (n == 1) {
    A = &kCutNorms[s[0]];
    memcpy(m->str, A->str, sizeof(m->str));
    memcpy(m->num, A->num, sizeof(m->num));
    m->mode = 1;
    m->from = m->to = m->minT = m->maxT = A->thickness;
    return;
  }
  if (t <= kCutNorms[s[0]].thickness) {
    A = &kCutNorms[s[0]];
    B = &kCutNorms[s[1]];
  } else if (t >= kCutNorms[s[n - 1]].thickness) {
    A = &kCutNorms[s[n - 2]];
    B = &kCutNorms[s[n - 1]];
  } else {
    A = &kCutNorms[s[0]];
    B = &kCutNorms[s[1]];
    for (int i = 0; i < n - 1; i++)
      if (t >= kCutNorms[s[i]].thickness && t <= kCutNorms[s[i + 1]].thickness) {
        A = &kCutNorms[s[i]];
        B = &kCutNorms[s[i + 1]];
        break;
      }
  }
  double span = B->thickness - A->thickness;
  double u = span == 0 ? 0 : (t - A->thickness) / span;
  for (int f = 0; f < CUT_NUM; f++) m->num[f] = cut_lerp(A->num[f], B->num[f], u);
  const CutNorm *nr = fabs(t - A->thickness) <= fabs(t - B->thickness) ? A : B;
  memcpy(m->str, nr->str, sizeof(m->str));
  m->mode = (t < m->minT - 1e-9 || t > m->maxT + 1e-9) ? 2 : 3;
  m->from = A->thickness;
  m->to = B->thickness;
}

static void cut_push(CutMethod *m, const wchar_t *key, const wchar_t *label, const wchar_t *detail,
                     const wchar_t *unit, double rate, int group) {
  if (cut_na(rate) || rate == 0 || m->nlines >= 16) return;
  CutLine *l = &m->lines[m->nlines++];
  memset(l, 0, sizeof(*l));
  l->key = key;
  l->label = label;
  l->detail = detail;
  l->unit = unit;
  l->ratePerM = rate;
  l->quantity = rate * m->lengthM;
  l->group = group;
}

static void cut_push_gas(CutMethod *m, const wchar_t *key, const wchar_t *label, double vol, double rho) {
  if (cut_na(vol) || vol == 0 || m->nlines >= 16) return;
  CutLine *l = &m->lines[m->nlines++];
  memset(l, 0, sizeof(*l));
  wchar_t r[32];
  cut_js_num(rho, r, 32);
  swprintf(l->detailBuf, 64, L"ρ %ls кг/м³", r);
  l->key = key;
  l->label = label;
  l->detail = l->detailBuf;
  l->unit = L"кг";
  l->ratePerM = vol * rho;
  l->quantity = vol * rho * m->lengthM;
  l->ratePerMVol = vol;
  l->quantityVol = vol * m->lengthM;
  l->rho = rho;
  l->group = 1;
}

static int cut_has_hydro(const wchar_t *eq) {
  /* «гидро» без учёта регистра: в таблице оборудование пишется и с большой */
  if (!eq) return 0;
  static const wchar_t lo[] = L"гидро", up[] = L"ГИДРО";
  for (const wchar_t *p = eq; *p; p++) {
    int i = 0;
    while (lo[i] && (p[i] == lo[i] || p[i] == up[i])) i++;
    if (!lo[i]) return 1;
  }
  return 0;
}

static void cut_from_norm(CutMethod *m, double lengthM) {
  m->lengthM = lengthM;
  m->lengthMm = lengthM * 1000;
  m->nlines = 0;
  cut_push(m, L"grinding", L"Круг зачистной / лепестковый", m->str[CS_GRIND], L"шт", m->num[CN_GRIND], 0);
  cut_push(m, L"rag", L"Ветошь", NULL, L"кг", m->num[CN_RAG], 2);
  cut_push(m, L"laserNozzle", L"Сопло лазер", m->str[CS_LNOZ], L"шт", m->num[CN_LNOZ], 0);
  cut_push(m, L"plasmaCathode", L"Катод плазма", m->str[CS_PCATH], L"шт", m->num[CN_PCATH], 0);
  cut_push(m, L"plasmaNozzle", L"Сопло плазма", m->str[CS_PNOZ], L"шт", m->num[CN_PNOZ], 0);
  cut_push(m, L"plasmaTip", L"Наконечник сопла плазмы", m->str[CS_PTIP], L"шт", m->num[CN_PTIP], 0);
  cut_push(m, L"mouthpiece", L"Мундштук", m->str[CS_MOUTH], L"шт", m->num[CN_MOUTH], 0);
  cut_push_gas(m, L"nitrogen", L"Азот", m->num[CN_N2], 1.25);
  cut_push_gas(m, L"argon", L"Аргон", m->num[CN_AR], 1.78);
  cut_push_gas(m, L"hydrogen", L"Водород", m->num[CN_H2], 0.09);
  cut_push_gas(m, L"gasMix", L"Смесь газов", m->num[CN_MIX], 1.5);
  cut_push_gas(m, L"oxygen", L"Кислород", m->num[CN_O2], 1.43);
  cut_push_gas(m, L"propane", L"Пропан", m->num[CN_PROP], 2.0);
  double sp = m->num[CN_SPEED];
  m->timeMin = (!cut_na(sp) && sp > 0) ? lengthM / sp : CUT_NA;
  const wchar_t *eq = m->str[CS_EQUIP];
  m->incomplete = !eq || !eq[0] || (cut_has_hydro(eq) && cut_na(sp));
}

/* весь расчёт; FALSE — нечего считать (нет толщины, длины или норм) */
static int cut_calc(const wchar_t *mat, double t, double lengthMm, CutPack *p) {
  memset(p, 0, sizeof(*p));
  double lengthM = lengthMm / 1000;
  if (!mat || !(t > 0) || !(lengthM > 0)) return 0;
  static CutGroup g[8];
  int ng = cut_groups(mat, g);
  if (!ng) return 0;
  int pick[8], np = cut_pick(g, ng, t, pick);
  p->material = mat;
  p->thickness = t;
  p->lengthM = lengthM;
  p->lengthMm = lengthM * 1000;
  for (int i = 0; i < np && p->n < 6; i++) {
    CutMethod *m = &p->m[p->n++];
    memset(m, 0, sizeof(*m));
    m->kind = g[pick[i]].kind;
    cut_build(&g[pick[i]], t, m);
    cut_from_norm(m, lengthM);
  }
  return p->n > 0;
}

/* ---- текст -------------------------------------------------------------- */

typedef struct {
  wchar_t *w;
  int n, cap;
} CutBuf;

static void cut_add(CutBuf *b, const wchar_t *fmt, ...) {
  if (b->n >= b->cap - 1) return;
  va_list ap;
  va_start(ap, fmt);
  int k = vswprintf(b->w + b->n, b->cap - b->n, fmt, ap);
  va_end(ap);
  if (k > 0) b->n += k;
  else b->n = b->cap - 1; /* не влезло — дальше не пишем */
  b->w[b->n] = 0;
}

static void cut_mode_label(const CutMethod *m, wchar_t *out, int cap) {
  wchar_t a[40], b[40], c[40], d[40];
  cut_fmt(m->from, 2, a, 40);
  cut_fmt(m->to, 2, b, 40);
  cut_fmt(m->minT, 2, c, 40);
  cut_fmt(m->maxT, 2, d, 40);
  if (m->mode == 0) swprintf(out, cap, L"норма на %ls мм", a);
  else if (m->mode == 1) swprintf(out, cap, L"единственная норма %ls мм — без пересчёта по толщине", a);
  else if (m->mode == 2) swprintf(out, cap, L"экстраполяция от %ls–%ls мм (вне диапазона %ls–%ls)", a, b, c, d);
  else if (m->from == m->to) swprintf(out, cap, L"норма на %ls мм", a);
  else swprintf(out, cap, L"интерполяция %ls–%ls мм", a, b);
}

static void cut_line_text(const CutLine *l, CutBuf *b) {
  wchar_t q[48], r[48], qv[48], rv[48];
  cut_fmt(l->quantity, 4, q, 48);
  cut_fmt(l->ratePerM, 4, r, 48);
  if (l->group == 1) {
    cut_fmt(l->quantityVol, 4, qv, 48);
    cut_fmt(l->ratePerMVol, 4, rv, 48);
    cut_add(b, L"%ls (%ls): %ls м³ / %ls кг (норма %ls м³ / %ls кг /м)", l->label, l->detail, qv, q, rv, r);
  } else if (l->detail && l->detail[0]) {
    cut_add(b, L"%ls (%ls): %ls %ls (норма %ls /м)", l->label, l->detail, q, l->unit, r);
  } else {
    cut_add(b, L"%ls: %ls %ls (норма %ls /м)", l->label, q, l->unit, r);
  }
}

/* текст кнопки «Копировать» — слово в слово как на странице */
static void cut_copy_text(const CutPack *p, wchar_t *out, int cap) {
  CutBuf b = {out, 0, cap};
  out[0] = 0;
  wchar_t t[40], L[40], ml[200], sp[40], tm[40];
  cut_fmt(p->thickness, 2, t, 40);
  cut_fmt(p->lengthMm, 0, L, 40);
  cut_add(&b, L"Расчёт резки: %ls %ls мм, L=%ls мм", p->material, t, L);
  for (int i = 0; i < p->n; i++) {
    const CutMethod *m = &p->m[i];
    cut_mode_label(m, ml, 200);
    const wchar_t *eq = m->str[CS_EQUIP] && m->str[CS_EQUIP][0] ? m->str[CS_EQUIP] : L"—";
    cut_js_num(m->num[CN_SPEED], sp, 40);
    cut_fmt_time(cut_na(m->timeMin) ? 0 : m->timeMin, tm, 40);
    cut_add(&b, L"\n\n%ls · %ls · %ls\nСкорость: %ls м/мин\nВремя: %ls", m->kind, eq, ml, sp, tm);
    for (int k = 0; k < m->nlines; k++) {
      cut_add(&b, L"\n");
      cut_line_text(&m->lines[k], &b);
    }
  }
}

/* текст в окне: по технологиям, внутри — оснастка, газы с итогом, прочее */
static void cut_view_text(const CutPack *p, wchar_t *out, int cap) {
  CutBuf b = {out, 0, cap};
  out[0] = 0;
  wchar_t t[40], L[40], ml[200], sp[40], tm[40];
  cut_fmt(p->thickness, 2, t, 40);
  cut_fmt(p->lengthMm, 0, L, 40);
  cut_add(&b, L"%ls · %ls мм · %ls мм реза · %ls\r\n", p->material, t, L,
          p->n > 1 ? L"несколько технологий" : L"расчёт по нормам");
  static const wchar_t *const titles[3] = {L"Оснастка и расходники", L"Газы", L"Вспомогательные"};
  static const int order[3] = {0, 1, 2};
  for (int i = 0; i < p->n; i++) {
    const CutMethod *m = &p->m[i];
    cut_mode_label(m, ml, 200);
    const wchar_t *eq = m->str[CS_EQUIP] && m->str[CS_EQUIP][0] ? m->str[CS_EQUIP] : L"Не указано";
    if (!cut_na(m->num[CN_SPEED])) {
      cut_fmt(m->num[CN_SPEED], 2, sp, 40);
      wcscat(sp, L" м/мин");
    } else {
      swprintf(sp, 40, L"—");
    }
    cut_fmt_time(cut_na(m->timeMin) ? 0 : m->timeMin, tm, 40);
    cut_add(&b, L"\r\n■ %ls · %ls\r\n   %ls\r\n   Скорость %ls · Время %ls\r\n", m->kind, eq, ml, sp, tm);
    if (m->incomplete) cut_add(&b, L"   ! Для этой позиции нормы в таблице не заполнены.\r\n");
    if (!m->nlines) cut_add(&b, L"   Нет заполненных норм расхода\r\n");
    for (int gi = 0; gi < 3; gi++) {
      int g = order[gi], cnt = 0;
      double vol = 0, kg = 0;
      for (int k = 0; k < m->nlines; k++)
        if (m->lines[k].group == g) {
          cnt++;
          vol += m->lines[k].quantityVol;
          kg += m->lines[k].quantity;
        }
      if (!cnt) continue;
      if (g == 1) {
        wchar_t a[48], c[48];
        cut_fmt(vol, 4, a, 48);
        cut_fmt(kg, 4, c, 48);
        cut_add(&b, L"\r\n   %ls · итого %ls м³ / %ls кг\r\n", titles[g], a, c);
      } else {
        cut_add(&b, L"\r\n   %ls\r\n", titles[g]);
      }
      for (int k = 0; k < m->nlines; k++) {
        const CutLine *l = &m->lines[k];
        if (l->group != g) continue;
        wchar_t q[48], r[48], qv[48], rv[48];
        cut_fmt(l->quantity, 4, q, 48);
        cut_fmt(l->ratePerM, 4, r, 48);
        if (g == 1) {
          cut_fmt(l->quantityVol, 4, qv, 48);
          cut_fmt(l->ratePerMVol, 4, rv, 48);
          cut_add(&b, L"   • %ls — %ls м³ / %ls кг\r\n       норма %ls м³ / %ls кг на метр · %ls\r\n", l->label, qv, q,
                  rv, r, l->detail);
        } else {
          cut_add(&b, L"   • %ls — %ls %ls\r\n       норма %ls %ls на метр%ls%ls\r\n", l->label, q, l->unit, r,
                  l->unit, l->detail && l->detail[0] ? L" · " : L"", l->detail ? l->detail : L"");
        }
      }
    }
  }
}

/* подсказка под толщиной: какие толщины есть в нормах по видам резки */
static void cut_hint(const wchar_t *mat, wchar_t *out, int cap) {
  static CutGroup g[8];
  int ng = cut_groups(mat, g);
  CutBuf b = {out, 0, cap};
  out[0] = 0;
  if (!ng) return;
  cut_add(&b, L"В нормах: ");
  for (int q = 0; q < ng; q++) {
    wchar_t a[40], c[40], k[40];
    cut_fmt(g[q].minT, 2, a, 40);
    cut_fmt(g[q].maxT, 2, c, 40);
    /* как kind.toLowerCase(): у видов резки заглавная только первая буква */
    int kl = 0;
    for (; g[q].kind[kl] && kl < 39; kl++) k[kl] = g[q].kind[kl];
    k[kl] = 0;
    if (k[0] >= 0x0410 && k[0] <= 0x042F) k[0] += 0x20;
    cut_add(&b, L"%ls%ls %ls–%ls мм", q ? L" · " : L"", k, a, c);
  }
  cut_add(&b, L". Любое значение — посчитаем.");
}

/* CSV как у кнопки «CSV» на странице (без BOM — его пишет тот, кто сохраняет) */
static void cut_csv_cell(CutBuf *b, const wchar_t *s, int first) {
  cut_add(b, first ? L"\"" : L";\"");
  for (const wchar_t *p = s ? s : L""; *p; p++) cut_add(b, *p == L'"' ? L"\"\"" : L"%lc", *p);
  cut_add(b, L"\"");
}

static void cut_csv_row(CutBuf *b, const wchar_t *const *cells, int n) {
  if (b->n) cut_add(b, L"\n");
  for (int i = 0; i < n; i++) cut_csv_cell(b, cells[i], i == 0);
}

static void cut_csv_text(const CutPack *p, wchar_t *out, int cap) {
  CutBuf b = {out, 0, cap};
  out[0] = 0;
  wchar_t t[40], L[40];
  cut_js_num(p->thickness, t, 40);
  cut_js_num(p->lengthMm, L, 40);
  const wchar_t *r0[] = {L"Параметр", L"Значение"}, *r1[] = {L"Материал", p->material},
                *r2[] = {L"Толщина, мм", t}, *r3[] = {L"Длина реза, мм", L};
  cut_csv_row(&b, r0, 2);
  cut_csv_row(&b, r1, 2);
  cut_csv_row(&b, r2, 2);
  cut_csv_row(&b, r3, 2);
  for (int i = 0; i < p->n; i++) {
    const CutMethod *m = &p->m[i];
    wchar_t ml[200], sp[40], tm[40];
    cut_mode_label(m, ml, 200);
    if (cut_na(m->num[CN_SPEED])) sp[0] = 0;
    else cut_js_num(m->num[CN_SPEED], sp, 40);
    if (cut_na(m->timeMin)) tm[0] = 0;
    else cut_js_num(m->timeMin, tm, 40);
    cut_add(&b, L"\n"); /* пустая строка между технологиями */
    const wchar_t *a[] = {L"Технология", m->kind}, *e[] = {L"Оборудование", m->str[CS_EQUIP] ? m->str[CS_EQUIP] : L""},
                  *c[] = {L"Расчёт", ml}, *s1[] = {L"Скорость, м/мин", sp}, *s2[] = {L"Время резки, мин", tm},
                  *h[] = {L"Позиция", L"Деталь/марка", L"Ед.", L"Норма на метр", L"Расход", L"Ед. (кг)", L"Норма кг",
                          L"Расход кг"};
    cut_csv_row(&b, a, 2);
    cut_csv_row(&b, e, 2);
    cut_csv_row(&b, c, 2);
    cut_csv_row(&b, s1, 2);
    cut_csv_row(&b, s2, 2);
    cut_csv_row(&b, h, 8);
    for (int k = 0; k < m->nlines; k++) {
      const CutLine *l = &m->lines[k];
      wchar_t v1[40], v2[40], v3[40], v4[40];
      if (l->group == 1) {
        cut_js_num(l->ratePerMVol, v1, 40);
        cut_js_num(l->quantityVol, v2, 40);
        cut_js_num(l->ratePerM, v3, 40);
        cut_js_num(l->quantity, v4, 40);
        const wchar_t *row[] = {l->label, l->detail ? l->detail : L"", L"м³", v1, v2, L"кг", v3, v4};
        cut_csv_row(&b, row, 8);
      } else {
        cut_js_num(l->ratePerM, v1, 40);
        cut_js_num(l->quantity, v2, 40);
        const wchar_t *row[] = {l->label, l->detail ? l->detail : L"", l->unit, v1, v2, L"", L"", L""};
        cut_csv_row(&b, row, 8);
      }
    }
  }
}
