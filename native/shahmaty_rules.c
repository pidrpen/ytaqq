/* ---- Шахматы: правила -----------------------------------------------------

   Без Windows: этот файл проверяется на любой машине (native/shahmaty_test.c —
   perft: число ходов до глубины N в известных позициях сходится с эталоном,
   значит рокировка, взятие на проходе, превращение и связки — верные).

   Поле — номер 0..63 = ряд·8 + столбец, ряд 0 — «1» (сторона белых),
   столбец 0 — «a». В клетке: 0 — пусто, ±1 пешка, ±2 конь, ±3 слон,
   ±4 ладья, ±5 ферзь, ±6 король; плюс — белые, минус — чёрные.

   Правила — как у FIDE:
     • рокировка — если король и эта ладья ещё не ходили, между ними пусто,
       король не под шахом, не проходит через битое поле и не встаёт на него;
     • взятие на проходе — только сразу после хода пешки на два поля;
     • пешка, дошедшая до последнего ряда, становится ферзём, ладьёй, слоном
       или конём — на выбор;
     • ход, после которого свой король под боем, сделать нельзя;
     • ходить нечем: под шахом — мат, без шаха — пат (ничья);
     • ничья сама: позиция повторилась трижды, 50 ходов без взятий и ходов
       пешкой, мата не поставить (одни короли; король с конём или слоном;
       только слоны на полях одного цвета).

   Ход в файле партии — как в UCI: «e2e4», рокировка — ходом короля «e1g1»,
   превращение — с буквой фигуры «e7e8q». На экране — краткая запись
   по-русски: «Кf3», «exd5», «0-0», «e8=Ф+». */

#define SX_MAXMOVES 256

enum { SX_P = 1, SX_N, SX_B, SX_R, SX_Q, SX_K };
/* рокировки, которые ещё можно: белые в короткую, в длинную, чёрные… */
enum { SX_WK = 1, SX_WQ = 2, SX_BK = 4, SX_BQ = 8 };
/* что за ход */
enum { SXF_CAP = 1, SXF_EP = 2, SXF_CASTLE = 4, SXF_DOUBLE = 8 };

typedef struct {
  signed char sq[64];
  signed char side;     /* чей ход: 1 — белые, −1 — чёрные */
  unsigned char castle; /* SX_WK | SX_WQ | SX_BK | SX_BQ */
  signed char ep;       /* поле, через которое пешка прошла на два; −1 — нет */
  int half;             /* полуходов подряд без взятия и хода пешкой */
} SxPos;

typedef struct {
  unsigned char from, to;
  unsigned char promo; /* во что превращается пешка: SX_N…SX_Q, иначе 0 */
  unsigned char flags; /* SXF_* */
} SxMove;

static int sx_row(int s) { return s >> 3; }
static int sx_col(int s) { return s & 7; }
static int sx_kind(int v) { return v < 0 ? -v : v; }
static int sx_color(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
static int sx_on(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }

static void sx_start(SxPos *p) {
  static const signed char back[8] = {SX_R, SX_N, SX_B, SX_Q, SX_K, SX_B, SX_N, SX_R};
  memset(p, 0, sizeof(*p));
  for (int c = 0; c < 8; c++) {
    p->sq[c] = back[c];
    p->sq[8 + c] = SX_P;
    p->sq[48 + c] = -SX_P;
    p->sq[56 + c] = (signed char)-back[c];
  }
  p->side = 1;
  p->castle = SX_WK | SX_WQ | SX_BK | SX_BQ;
  p->ep = -1;
}

static const int kSxKnight[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
static const int kSxKing[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
/* 0..3 — по прямой (ладья), 4..7 — по диагонали (слон); ферзь — все */
static const int kSxRay[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

/* бьёт ли цвет by поле s */
static int sx_attacked(const SxPos *p, int s, int by) {
  int r0 = sx_row(s), c0 = sx_col(s);
  /* пешка бьёт вперёд наискось: белая — с ряда ниже */
  int pr = r0 - by;
  for (int dc = -1; dc <= 1; dc += 2)
    if (sx_on(pr, c0 + dc) && p->sq[pr * 8 + c0 + dc] == by * SX_P) return 1;
  for (int i = 0; i < 8; i++) {
    int r = r0 + kSxKnight[i][0], c = c0 + kSxKnight[i][1];
    if (sx_on(r, c) && p->sq[r * 8 + c] == by * SX_N) return 1;
    r = r0 + kSxKing[i][0], c = c0 + kSxKing[i][1];
    if (sx_on(r, c) && p->sq[r * 8 + c] == by * SX_K) return 1;
  }
  for (int d = 0; d < 8; d++) {
    int r = r0 + kSxRay[d][0], c = c0 + kSxRay[d][1];
    while (sx_on(r, c) && !p->sq[r * 8 + c]) r += kSxRay[d][0], c += kSxRay[d][1];
    if (!sx_on(r, c)) continue;
    int v = p->sq[r * 8 + c];
    if (sx_color(v) != by) continue;
    int k = sx_kind(v);
    if (k == SX_Q || k == (d < 4 ? SX_R : SX_B)) return 1;
  }
  return 0;
}

static int sx_king_sq(const SxPos *p, int color) {
  for (int s = 0; s < 64; s++)
    if (p->sq[s] == color * SX_K) return s;
  return -1;
}

static int sx_in_check(const SxPos *p, int color) {
  int k = sx_king_sq(p, color);
  return k >= 0 && sx_attacked(p, k, -color);
}

static void sx_apply(SxPos *p, const SxMove *m) {
  int v = p->sq[m->from], color = sx_color(v), k = sx_kind(v);
  if (m->flags & SXF_EP) p->sq[m->to - color * 8] = 0; /* взятая на проходе стоит рядом, а не на поле хода */
  p->sq[m->to] = (signed char)(m->promo ? color * m->promo : v);
  p->sq[m->from] = 0;
  if (m->flags & SXF_CASTLE) {
    int rf = m->to > m->from ? m->from + 3 : m->from - 4, rt = m->to > m->from ? m->from + 1 : m->from - 1;
    p->sq[rt] = p->sq[rf];
    p->sq[rf] = 0;
  }
  /* король или ладья сошли с места, ладью взяли — с ней больше не рокироваться */
  static const struct {
    unsigned char s, bits;
  } kLose[] = {{4, SX_WK | SX_WQ}, {0, SX_WQ}, {7, SX_WK}, {60, SX_BK | SX_BQ}, {56, SX_BQ}, {63, SX_BK}};
  for (int i = 0; i < 6; i++)
    if (m->from == kLose[i].s || m->to == kLose[i].s) p->castle &= (unsigned char)~kLose[i].bits;
  p->ep = (signed char)((m->flags & SXF_DOUBLE) ? (m->from + m->to) / 2 : -1);
  p->half = (k == SX_P || (m->flags & SXF_CAP)) ? 0 : p->half + 1;
  p->side = (signed char)-p->side;
}

/* ---- ходы ----------------------------------------------------------------- */

typedef struct {
  SxMove *out;
  int n, max;
} SxList;

static void sx_push(SxList *l, int from, int to, int promo, int flags) {
  if (l->n >= l->max) return;
  SxMove *m = &l->out[l->n++];
  m->from = (unsigned char)from;
  m->to = (unsigned char)to;
  m->promo = (unsigned char)promo;
  m->flags = (unsigned char)flags;
}

/* ход пешки: на последний ряд — четыре хода, по фигуре на выбор */
static void sx_push_pawn(SxList *l, int from, int to, int flags, int last) {
  if (!last) {
    sx_push(l, from, to, 0, flags);
    return;
  }
  for (int k = SX_Q; k >= SX_N; k--) sx_push(l, from, to, k, flags);
}

/* все ходы по правилам фигур — без проверки, что свой король не под боем */
static void sx_pseudo(const SxPos *p, SxList *l) {
  int me = p->side;
  for (int s = 0; s < 64; s++) {
    int v = p->sq[s];
    if (sx_color(v) != me) continue;
    int k = sx_kind(v), r0 = sx_row(s), c0 = sx_col(s);
    if (k == SX_P) {
      int r = r0 + me, last = r == (me > 0 ? 7 : 0);
      if (!sx_on(r, c0)) continue;
      if (!p->sq[r * 8 + c0]) {
        sx_push_pawn(l, s, r * 8 + c0, 0, last);
        if (r0 == (me > 0 ? 1 : 6) && !p->sq[(r + me) * 8 + c0]) sx_push(l, s, (r + me) * 8 + c0, 0, SXF_DOUBLE);
      }
      for (int dc = -1; dc <= 1; dc += 2) {
        if (!sx_on(r, c0 + dc)) continue;
        int t = r * 8 + c0 + dc;
        if (sx_color(p->sq[t]) == -me) sx_push_pawn(l, s, t, SXF_CAP, last);
        else if (t == p->ep && !p->sq[t]) sx_push(l, s, t, 0, SXF_CAP | SXF_EP);
      }
      continue;
    }
    if (k == SX_N || k == SX_K) {
      const int(*d)[2] = k == SX_N ? kSxKnight : kSxKing;
      for (int i = 0; i < 8; i++) {
        int r = r0 + d[i][0], c = c0 + d[i][1];
        if (!sx_on(r, c)) continue;
        int t = r * 8 + c, tc = sx_color(p->sq[t]);
        if (tc != me) sx_push(l, s, t, 0, tc ? SXF_CAP : 0);
      }
      int base = me > 0 ? 0 : 56;
      if (k == SX_K && s == base + 4 && !sx_attacked(p, s, -me)) {
        unsigned kb = me > 0 ? SX_WK : SX_BK, qb = me > 0 ? SX_WQ : SX_BQ;
        if ((p->castle & kb) && p->sq[base + 7] == me * SX_R && !p->sq[base + 5] && !p->sq[base + 6] &&
            !sx_attacked(p, base + 5, -me) && !sx_attacked(p, base + 6, -me))
          sx_push(l, s, base + 6, 0, SXF_CASTLE);
        if ((p->castle & qb) && p->sq[base] == me * SX_R && !p->sq[base + 1] && !p->sq[base + 2] &&
            !p->sq[base + 3] && !sx_attacked(p, base + 3, -me) && !sx_attacked(p, base + 2, -me))
          sx_push(l, s, base + 2, 0, SXF_CASTLE);
      }
      continue;
    }
    int d0 = k == SX_B ? 4 : 0, d1 = k == SX_R ? 4 : 8;
    for (int d = d0; d < d1; d++) {
      int r = r0 + kSxRay[d][0], c = c0 + kSxRay[d][1];
      while (sx_on(r, c)) {
        int t = r * 8 + c, tc = sx_color(p->sq[t]);
        if (tc == me) break;
        sx_push(l, s, t, 0, tc ? SXF_CAP : 0);
        if (tc) break;
        r += kSxRay[d][0];
        c += kSxRay[d][1];
      }
    }
  }
}

/* все допустимые ходы стороны, чей ход; возвращает их число */
static int sx_legal(const SxPos *p, SxMove *out, int max) {
  SxMove all[SX_MAXMOVES];
  SxList l = {all, 0, SX_MAXMOVES};
  sx_pseudo(p, &l);
  int n = 0;
  for (int i = 0; i < l.n && n < max; i++) {
    SxPos q = *p;
    sx_apply(&q, &all[i]);
    if (!sx_in_check(&q, p->side)) out[n++] = all[i];
  }
  return n;
}

/* ---- ничья сама ------------------------------------------------------------ */

/* мата не поставить никому: одни короли, король с одной лёгкой фигурой
   против короля, только слоны — и все на полях одного цвета */
static int sx_dead(const SxPos *p) {
  int minors = 0, knights = 0, bishopColors = 0;
  for (int s = 0; s < 64; s++) {
    int k = sx_kind(p->sq[s]);
    if (!k || k == SX_K) continue;
    if (k == SX_P || k == SX_R || k == SX_Q) return 0;
    minors++;
    if (k == SX_N) knights++;
    else bishopColors |= ((sx_row(s) + sx_col(s)) & 1) ? 1 : 2;
  }
  return minors <= 1 || (!knights && bishopColors != 3);
}

/* Отпечаток позиции для повторений: фигуры, чей ход, рокировки и поле
   взятия на проходе — только если такое взятие сейчас возможно (иначе
   позиции считаются одинаковыми, как у FIDE). legal — ходы этой позиции. */
static unsigned long long sx_key(const SxPos *p, const SxMove *legal, int n) {
  unsigned long long h = 1469598103934665603ull;
  for (int s = 0; s < 64; s++) h = (h ^ (unsigned char)p->sq[s]) * 1099511628211ull;
  int ep = 0;
  for (int i = 0; i < n && !ep; i++)
    if (legal[i].flags & SXF_EP) ep = legal[i].to + 1;
  h = (h ^ (unsigned char)p->side) * 1099511628211ull;
  h = (h ^ p->castle) * 1099511628211ull;
  h = (h ^ (unsigned)ep) * 1099511628211ull;
  return h;
}

/* ---- запись ходов ----------------------------------------------------------- */

/* «e2e4», «e7e8q» — для файла партии */
static void sx_uci(const SxMove *m, char *out) {
  out[0] = (char)('a' + sx_col(m->from));
  out[1] = (char)('1' + sx_row(m->from));
  out[2] = (char)('a' + sx_col(m->to));
  out[3] = (char)('1' + sx_row(m->to));
  out[4] = m->promo ? "  nbrq"[m->promo] : 0;
  out[5] = 0;
}

/* какой из допустимых ходов записан текстом «e2e4»; −1 — такого нет */
static int sx_find_uci(const SxMove *mv, int n, const char *t) {
  while (*t == ' ') t++;
  for (int i = 0; i < 4; i++)
    if (!t[i] || (i % 2 == 0 && (t[i] < 'a' || t[i] > 'h')) || (i % 2 == 1 && (t[i] < '1' || t[i] > '8'))) return -1;
  int from = (t[1] - '1') * 8 + (t[0] - 'a'), to = (t[3] - '1') * 8 + (t[2] - 'a'), promo = 0;
  if (t[4] == 'q') promo = SX_Q;
  else if (t[4] == 'r') promo = SX_R;
  else if (t[4] == 'b') promo = SX_B;
  else if (t[4] == 'n') promo = SX_N;
  for (int i = 0; i < n; i++)
    if (mv[i].from == from && mv[i].to == to && mv[i].promo == promo) return i;
  return -1;
}

/* буквы фигур по-русски: конь, слон, ладья, ферзь, король */
static const wchar_t *const kSxRu[7] = {L"", L"", L"К", L"С", L"Л", L"Ф", L"Кр"};

/* краткая запись хода по-русски: «Кf3», «Лad1», «exd5», «0-0», «e8=Ф#»;
   p — позиция до хода, legal — её ходы (чтобы различить две одинаковые
   фигуры, которые могут встать на одно поле) */
static void sx_san(const SxPos *p, const SxMove *m, const SxMove *legal, int n, wchar_t *out, int cap) {
  wchar_t b[24];
  int i = 0, v = p->sq[m->from], k = sx_kind(v);
  if (m->flags & SXF_CASTLE) {
    const wchar_t *c = m->to > m->from ? L"0-0" : L"0-0-0";
    while (*c) b[i++] = *c++;
  } else {
    if (k == SX_P) {
      if (m->flags & SXF_CAP) b[i++] = (wchar_t)(L'a' + sx_col(m->from));
    } else {
      for (const wchar_t *c = kSxRu[k]; *c; c++) b[i++] = *c;
      int same = 0, sameCol = 0, sameRow = 0;
      for (int j = 0; j < n; j++) {
        const SxMove *o = &legal[j];
        if (o->to != m->to || o->from == m->from || p->sq[o->from] != v) continue;
        same = 1;
        sameCol |= sx_col(o->from) == sx_col(m->from);
        sameRow |= sx_row(o->from) == sx_row(m->from);
      }
      if (same && (!sameCol || sameRow)) b[i++] = (wchar_t)(L'a' + sx_col(m->from));
      if (same && sameCol) b[i++] = (wchar_t)(L'1' + sx_row(m->from));
    }
    if (m->flags & SXF_CAP) b[i++] = L'x';
    b[i++] = (wchar_t)(L'a' + sx_col(m->to));
    b[i++] = (wchar_t)(L'1' + sx_row(m->to));
    if (m->promo) {
      b[i++] = L'=';
      for (const wchar_t *c = kSxRu[m->promo]; *c; c++) b[i++] = *c;
    }
  }
  SxPos q = *p;
  sx_apply(&q, m);
  if (sx_in_check(&q, q.side)) {
    SxMove r[SX_MAXMOVES];
    b[i++] = sx_legal(&q, r, SX_MAXMOVES) ? L'+' : L'#';
  }
  b[i] = 0;
  int l = 0;
  for (; b[l] && l < cap - 1; l++) out[l] = b[l];
  out[l] = 0;
}

/* сколько стоит фигура (для перевеса в материале) */
static int sx_value(int kind) {
  static const int v[7] = {0, 1, 3, 3, 5, 9, 0};
  return kind >= 0 && kind < 7 ? v[kind] : 0;
}

static int sx_material(const SxPos *p, int color) {
  int t = 0;
  for (int s = 0; s < 64; s++)
    if (sx_color(p->sq[s]) == color) t += sx_value(sx_kind(p->sq[s]));
  return t;
}
