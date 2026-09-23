/* ---- Короткие нарды: правила -----------------------------------------------

   Без Windows: этот файл проверяется тестами на любой машине.

   Доска в «абсолютных» номерах — как её видит игрок 0: пункты 1..24,
   pt[i] > 0 — шашки игрока 0, pt[i] < 0 — игрока 1. Игрок 0 идёт от 24 к 1
   и выбрасывает за 0; игрок 1 — от 1 к 24 и выбрасывает за 25.

   Внутри правил удобнее «свои» номера того, кто ходит: для него всегда путь
   от 24 к 1, дом — 1..6, бар — 25, выброс — 0. nd_abs() переводит свой номер
   в абсолютный, для игрока 1 это 25 − номер.

   Ходы. Надо использовать столько кубиков, сколько вообще возможно; если
   возможно сыграть только один из двух разных — обязан больший. Поэтому
   допустимый шаг — тот, с которого начинается хотя бы одна самая длинная
   последовательность (nd_legal_steps). Дубль — четыре хода. */

#define ND_CHECKERS 15

typedef struct {
  signed char pt[26]; /* 1..24; 0 и 25 не используются */
  unsigned char bar[2], off[2];
} NdBoard;

typedef struct {
  signed char from, to, die; /* в своих номерах ходящего: from 25 — с бара, to 0 — выброс */
} NdStep;

static int nd_abs(int side, int rel) { return side == 0 ? rel : 25 - rel; }

/* сколько шашек стороны side на её «своём» пункте rel (1..24) */
static int nd_own(const NdBoard *b, int side, int rel) {
  int v = b->pt[nd_abs(side, rel)];
  return side == 0 ? (v > 0 ? v : 0) : (v < 0 ? -v : 0);
}

static int nd_opp(const NdBoard *b, int side, int rel) { return nd_own(b, 1 - side, 25 - rel); }

static void nd_start(NdBoard *b) {
  memset(b, 0, sizeof(*b));
  /* у каждого: 2 на 24, 5 на 13, 3 на 8, 5 на 6 (в своих номерах) */
  static const int where[4] = {24, 13, 8, 6}, cnt[4] = {2, 5, 3, 5};
  for (int k = 0; k < 4; k++) {
    b->pt[nd_abs(0, where[k])] += (signed char)cnt[k];
    b->pt[nd_abs(1, where[k])] -= (signed char)cnt[k];
  }
}

static void nd_put(NdBoard *b, int side, int rel, int delta) {
  int a = nd_abs(side, rel);
  b->pt[a] = (signed char)(b->pt[a] + (side == 0 ? delta : -delta));
}

static int nd_all_home(const NdBoard *b, int side) {
  if (b->bar[side]) return 0;
  for (int r = 7; r <= 24; r++)
    if (nd_own(b, side, r)) return 0;
  return 1;
}

/* можно ли сыграть кубик d шашкой с from; куда — в *to */
static int nd_can(const NdBoard *b, int side, int from, int d, int *to) {
  if (b->bar[side] && from != 25) return 0;
  if (from == 25) {
    if (!b->bar[side]) return 0;
  } else if (from < 1 || from > 24 || !nd_own(b, side, from)) {
    return 0;
  }
  int t = from - d;
  if (t >= 1) {
    if (nd_opp(b, side, t) >= 2) return 0;
    *to = t;
    return 1;
  }
  if (!nd_all_home(b, side)) return 0;
  if (t == 0) {
    *to = 0;
    return 1;
  }
  /* выброс кубиком больше нужного — только с самого дальнего пункта */
  for (int r = from + 1; r <= 6; r++)
    if (nd_own(b, side, r)) return 0;
  *to = 0;
  return 1;
}

static void nd_apply(NdBoard *b, int side, NdStep s) {
  if (s.from == 25) b->bar[side]--;
  else nd_put(b, side, s.from, -1);
  if (s.to == 0) {
    b->off[side]++;
    return;
  }
  if (nd_opp(b, side, s.to) == 1) { /* бьём одиночку — она уходит на бар */
    nd_put(b, 1 - side, 25 - s.to, -1);
    b->bar[1 - side]++;
  }
  nd_put(b, side, s.to, +1);
}

/* наибольшее число кубиков, которое удаётся сыграть из dice[0..n) */
static int nd_max_depth(const NdBoard *b, int side, const int *dice, int n) {
  if (n == 0) return 0;
  int best = 0;
  for (int k = 0; k < n; k++) {
    if (k > 0 && dice[k] == dice[k - 1]) continue; /* одинаковые кубики — одно и то же */
    int rest[4], m = 0;
    for (int j = 0; j < n; j++)
      if (j != k) rest[m++] = dice[j];
    for (int from = 25; from >= 1; from--) {
      int to;
      if (!nd_can(b, side, from, dice[k], &to)) continue;
      NdBoard c = *b;
      NdStep s = {(signed char)from, (signed char)to, (signed char)dice[k]};
      nd_apply(&c, side, s);
      int got = 1 + nd_max_depth(&c, side, rest, m);
      if (got > best) best = got;
      if (best == n) return best;
    }
  }
  return best;
}

/* Какие шаги можно сделать сейчас: только те, что начинают самую длинную
   последовательность. dice — оставшиеся кубики (по возрастанию). */
static int nd_legal_steps(const NdBoard *b, int side, const int *dice, int n, NdStep *out, int cap) {
  int want = nd_max_depth(b, side, dice, n);
  if (want == 0) return 0;
  int cnt = 0;
  for (int k = 0; k < n; k++) {
    if (k > 0 && dice[k] == dice[k - 1]) continue;
    int rest[4], m = 0;
    for (int j = 0; j < n; j++)
      if (j != k) rest[m++] = dice[j];
    for (int from = 25; from >= 1; from--) {
      int to;
      if (!nd_can(b, side, from, dice[k], &to)) continue;
      NdBoard c = *b;
      NdStep s = {(signed char)from, (signed char)to, (signed char)dice[k]};
      nd_apply(&c, side, s);
      if (1 + nd_max_depth(&c, side, rest, m) < want) continue;
      if (cnt < cap) out[cnt++] = s;
    }
  }
  /* один ход из двух разных кубиков — обязательно больший, если он возможен */
  if (want == 1 && n == 2 && dice[0] != dice[1]) {
    int hi = dice[0] > dice[1] ? dice[0] : dice[1], anyHi = 0;
    for (int i = 0; i < cnt; i++)
      if (out[i].die == hi) anyHi = 1;
    if (anyHi) {
      int w = 0;
      for (int i = 0; i < cnt; i++)
        if (out[i].die == hi) out[w++] = out[i];
      cnt = w;
    }
  }
  return cnt;
}

/* кубики хода по двум числам: четыре одинаковых при дубле */
static int nd_dice_list(int d1, int d2, int *out) {
  if (d1 == d2) {
    out[0] = out[1] = out[2] = out[3] = d1;
    return 4;
  }
  out[0] = d1 < d2 ? d1 : d2;
  out[1] = d1 < d2 ? d2 : d1;
  return 2;
}

/* убрать сыгранный кубик из списка (список остаётся по возрастанию) */
static int nd_use_die(int *dice, int n, int d) {
  for (int k = 0; k < n; k++) {
    if (dice[k] != d) continue;
    for (int j = k; j < n - 1; j++) dice[j] = dice[j + 1];
    return n - 1;
  }
  return n;
}

/* 1 — обычная победа, 2 — «марс» (у проигравшего ни одной выброшенной),
   3 — «кокс» (к тому же его шашка на баре или в доме победителя) */
static int nd_points(const NdBoard *b, int winner) {
  int loser = 1 - winner;
  if (b->off[loser]) return 1;
  if (b->bar[loser]) return 3;
  for (int r = 19; r <= 24; r++) /* дом победителя в номерах проигравшего — 19..24 */
    if (nd_own(b, loser, r)) return 3;
  return 2;
}
