/* ---- Русские шашки: правила -----------------------------------------------

   Без Windows: этот файл проверяется тестами на любой машине
   (native/tests/shashki_test.c).

   Доска 8×8, играют на тёмных полях. Поле — номер 0..63 = ряд·8 + столбец,
   ряд 0 — «1» (сторона белых), столбец 0 — «a»; тёмные — где (ряд+столбец)
   чётно (a1 тёмное). В клетке: 0 — пусто, 1 — белая простая, 2 — белая
   дамка, −1 — чёрная простая, −2 — чёрная дамка. Цвет стороны: 1 — белые,
   −1 — чёрные; белые ходят первыми.

   Правила русских шашек:
     • простая ходит на одно поле вперёд по диагонали, бьёт и вперёд, и
       назад; дамка ходит и бьёт на любое расстояние («дальнобойная»);
     • бить обязательно, из нескольких взятий можно выбрать любое (не обязательно
       самое длинное), но начатое взятие продолжается до конца;
     • взятые шашки снимаются после хода (турецкий удар): через взятую
       второй раз не бьют, и она мешает, пока ход не кончен;
     • простая, дошедшая до последнего ряда, становится дамкой, в том числе
       посреди взятия — и продолжает бить уже как дамка;
     • дамка после взятия встаёт на любое свободное поле за взятой, но если
       с какого-то из них можно бить дальше — только туда;
     • кому нечем ходить (шашек нет или все заперты) — проиграл;
     • 15 ходов подряд (30 полуходов) только дамками и без взятий — ничья.

   Ход — путь по полям: «c3-d4» или со взятием «c3:e5:c7». */

#define SH_MAXPATH 14
#define SH_MAXMOVES 192

typedef struct {
  signed char sq[64];
} ShBoard;

typedef struct {
  unsigned char path[SH_MAXPATH]; /* поля пути: откуда, (куда встаёт после каждого взятия)… */
  unsigned char npath;
  unsigned char cap[12]; /* взятые поля */
  unsigned char ncap;
  unsigned char promote; /* простая стала дамкой */
} ShMove;

static int sh_row(int s) { return s >> 3; }
static int sh_col(int s) { return s & 7; }
static int sh_dark(int r, int c) { return ((r + c) & 1) == 0; }
static int sh_color(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
static int sh_isking(int v) { return v == 2 || v == -2; }
/* последний ряд для цвета */
static int sh_last_row(int color) { return color > 0 ? 7 : 0; }

static void sh_start(ShBoard *b) {
  for (int s = 0; s < 64; s++) {
    int r = sh_row(s), c = sh_col(s);
    b->sq[s] = 0;
    if (!sh_dark(r, c)) continue;
    if (r <= 2) b->sq[s] = 1;
    else if (r >= 5) b->sq[s] = -1;
  }
}

static const int kShDr[4] = {1, 1, -1, -1}, kShDc[4] = {1, -1, 1, -1};

/* ---- взятия: обход в глубину -------------------------------------------- */

typedef struct {
  ShMove *out;
  int n, max;
  int color;
} ShGen;

static void sh_add(ShGen *g, const ShMove *m) {
  if (g->n < g->max) g->out[g->n++] = *m;
}

static int sh_was_cap(const ShMove *m, int s) {
  for (int i = 0; i < m->ncap; i++)
    if (m->cap[i] == s) return 1;
  return 0;
}

/* может ли шашка с поля s (king — дамка ли она) бить дальше;
   b — доска с уже снятой с исходного поля шашкой, взятые — в m */
static int sh_can_capture_from(const ShBoard *b, const ShMove *m, int s, int king, int color) {
  int r0 = sh_row(s), c0 = sh_col(s);
  for (int d = 0; d < 4; d++) {
    int r = r0 + kShDr[d], c = c0 + kShDc[d];
    if (king)
      while (r >= 0 && r < 8 && c >= 0 && c < 8 && b->sq[r * 8 + c] == 0) {
        r += kShDr[d];
        c += kShDc[d];
      }
    if (r < 0 || r > 7 || c < 0 || c > 7) continue;
    int t = r * 8 + c;
    if (sh_color(b->sq[t]) != -color || sh_was_cap(m, t)) continue;
    int r2 = r + kShDr[d], c2 = c + kShDc[d];
    if (r2 < 0 || r2 > 7 || c2 < 0 || c2 > 7) continue;
    if (b->sq[r2 * 8 + c2] == 0) return 1;
  }
  return 0;
}

static void sh_dfs(ShGen *g, const ShBoard *b, ShMove *m, int s, int king) {
  int color = g->color, any = 0;
  int r0 = sh_row(s), c0 = sh_col(s);
  for (int d = 0; d < 4; d++) {
    int r = r0 + kShDr[d], c = c0 + kShDc[d];
    if (king)
      while (r >= 0 && r < 8 && c >= 0 && c < 8 && b->sq[r * 8 + c] == 0) {
        r += kShDr[d];
        c += kShDc[d];
      }
    if (r < 0 || r > 7 || c < 0 || c > 7) continue;
    int t = r * 8 + c;
    if (sh_color(b->sq[t]) != -color || sh_was_cap(m, t)) continue;
    /* поля за взятой */
    int land[8], nl = 0;
    int r2 = r + kShDr[d], c2 = c + kShDc[d];
    while (r2 >= 0 && r2 < 8 && c2 >= 0 && c2 < 8 && b->sq[r2 * 8 + c2] == 0) {
      land[nl++] = r2 * 8 + c2;
      if (!king) break; /* простая встаёт сразу за взятой */
      r2 += kShDr[d];
      c2 += kShDc[d];
    }
    if (!nl) continue;
    if (m->npath >= SH_MAXPATH || m->ncap >= 12) continue;
    m->cap[m->ncap++] = (unsigned char)t;
    /* дамке — только поля, откуда бой продолжается, если такие есть */
    int cont[8], ncont = 0;
    for (int i = 0; i < nl; i++) {
      int k2 = king || sh_row(land[i]) == sh_last_row(color);
      if (sh_can_capture_from(b, m, land[i], k2, color)) cont[ncont++] = i;
    }
    for (int i = 0; i < nl; i++) {
      if (ncont) {
        int ok = 0;
        for (int j = 0; j < ncont; j++) ok |= cont[j] == i;
        if (!ok) continue;
      }
      int k2 = king || sh_row(land[i]) == sh_last_row(color);
      unsigned char prom = m->promote;
      if (!king && k2) m->promote = 1;
      m->path[m->npath++] = (unsigned char)land[i];
      sh_dfs(g, b, m, land[i], k2);
      m->npath--;
      m->promote = prom;
      any = 1;
    }
    m->ncap--;
  }
  if (!any && m->ncap > 0) sh_add(g, m); /* дальше бить нечем — ход кончен */
}

/* все допустимые ходы цвета color; возвращает их число */
static int sh_legal(const ShBoard *b, int color, ShMove *out, int max) {
  ShGen g = {out, 0, max, color};
  /* сперва взятия — они обязательны */
  for (int s = 0; s < 64; s++) {
    if (sh_color(b->sq[s]) != color) continue;
    ShBoard w = *b;
    int king = sh_isking(w.sq[s]);
    w.sq[s] = 0; /* шашка ушла со своего поля — оно свободно для пути дамки */
    ShMove m;
    memset(&m, 0, sizeof(m));
    m.path[m.npath++] = (unsigned char)s;
    sh_dfs(&g, &w, &m, s, king);
  }
  if (g.n) return g.n;
  for (int s = 0; s < 64; s++) {
    int v = b->sq[s];
    if (sh_color(v) != color) continue;
    int king = sh_isking(v), r0 = sh_row(s), c0 = sh_col(s);
    for (int d = 0; d < 4; d++) {
      if (!king && kShDr[d] != color) continue; /* простая — только вперёд */
      int r = r0 + kShDr[d], c = c0 + kShDc[d];
      while (r >= 0 && r < 8 && c >= 0 && c < 8 && b->sq[r * 8 + c] == 0) {
        ShMove m;
        memset(&m, 0, sizeof(m));
        m.path[0] = (unsigned char)s;
        m.path[1] = (unsigned char)(r * 8 + c);
        m.npath = 2;
        if (!king && r == sh_last_row(color)) m.promote = 1;
        sh_add(&g, &m);
        if (!king) break;
        r += kShDr[d];
        c += kShDc[d];
      }
    }
  }
  return g.n;
}

static void sh_apply(ShBoard *b, const ShMove *m) {
  int from = m->path[0], to = m->path[m->npath - 1];
  int v = b->sq[from];
  b->sq[from] = 0;
  for (int i = 0; i < m->ncap; i++) b->sq[m->cap[i]] = 0;
  if (m->promote) v = v > 0 ? 2 : -2;
  b->sq[to] = (signed char)v;
}

static int sh_count(const ShBoard *b, int color) {
  int n = 0;
  for (int s = 0; s < 64; s++) n += sh_color(b->sq[s]) == color;
  return n;
}

/* «c3» */
static void sh_sqname(int s, char *out) {
  out[0] = (char)('a' + sh_col(s));
  out[1] = (char)('1' + sh_row(s));
  out[2] = 0;
}

/* путь хода текстом: «c3-d4», «c3:e5:c7» */
static void sh_move_text(const ShMove *m, char *out, int cap) {
  int k = 0;
  for (int i = 0; i < m->npath && k + 4 < cap; i++) {
    if (i) out[k++] = m->ncap ? ':' : '-';
    char nm[3];
    sh_sqname(m->path[i], nm);
    out[k++] = nm[0];
    out[k++] = nm[1];
  }
  out[k] = 0;
}

/* разобрать путь «c3:e5:c7» в поля; вернуть их число (0 — не разобрался) */
static int sh_parse_path(const char *t, unsigned char *sq, int max) {
  int n = 0;
  while (*t == ' ') t++;
  while (*t && n < max) {
    if (t[0] < 'a' || t[0] > 'h' || t[1] < '1' || t[1] > '8') return 0;
    sq[n++] = (unsigned char)((t[1] - '1') * 8 + (t[0] - 'a'));
    t += 2;
    if (*t == '-' || *t == ':' || *t == 'x') t++;
    else break;
  }
  return n;
}

/* какой из допустимых ходов записан этим путём; −1 — такого нет */
static int sh_find_move(const ShMove *mv, int n, const unsigned char *sq, int nsq) {
  for (int i = 0; i < n; i++) {
    if (mv[i].npath != nsq) continue;
    int same = 1;
    for (int k = 0; k < nsq && same; k++) same = mv[i].path[k] == sq[k];
    if (same) return i;
  }
  return -1;
}
