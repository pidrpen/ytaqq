/* ---- Стратегия: правила ----------------------------------------------------

   Без Windows: этот файл проверяется на любой машине (native/strategiya_test.c —
   карты, ходы, удары, запись хода и сотни партий «компьютер против
   компьютера»). Всё считается в целых числах и без случайностей: у обоих
   игроков из одних и тех же записей выходит одна и та же позиция.

   Карта — шестиугольники, 11 рядов: в чётных рядах 13 клеток, в нечётных 12
   (они сдвинуты на полклетки вправо). Клетка — номер ряд·13 + столбец.
   Карта строится по номеру партии и симметрична относительно центра: у
   обоих одинаковые места. Местность:
     поле     — ход 1;
     лес      — ход 2 (всаднику 3), защита 2;
     холм     — ход 2, защита 3; лучник и катапульта с холма бьют на клетку дальше;
     деревня  — ход 1, защита 2; чья деревня — тому +2 золота в ход, и его
                отряд на ней лечится на 2 в начале своего хода;
     горы, вода — не пройти;
     замок    — не пройти; у замка 24 прочности, отряды покупаются на клетках
                вокруг своего замка.
   Защита 2 — урон по отряду на этой клетке меньше на 20 %.

   Отряды (у каждого 10 сил; чем меньше сил, тем слабее удар):
     мечник     10 золота, ход 3, бьёт рядом
     копейщик   10 золота, ход 3, бьёт рядом
     лучник     12 золота, ход 3, бьёт на 1–2 клетки
     всадник    16 золота, ход 5, бьёт рядом
     катапульта 18 золота, ход 2, бьёт на 2–3 клетки; в один ход — или ехать,
                или бить; на удар не отвечает, деревни не занимает
   Кто кого: мечник сильнее копейщика, копейщик — всадника, всадник —
   мечника; лучника и катапульту вблизи бьют все. Урон = таблица · силы
   бьющего / 10 · (10 − защита клетки) / 10, не меньше 1. Уцелевший отвечает,
   если достаёт. По замку: мечник, копейщик, всадник — 3, лучник — 2,
   катапульта — 6 (тоже по силам); замок отвечает на удар вплотную — 2.

   Ход: каждым отрядом — пройти и (или) ударить, купить отряды (новый ходит
   со следующего хода), «Завершить ход». В начале хода: 5 золота + 2 за
   каждую свою деревню. Не больше 10 отрядов у стороны. Победа — разрушить
   замок соперника. После 60 ходов (по 30 у каждого) — у кого целее замок,
   при равенстве — у кого больше деревень, иначе ничья.

   Запись хода в файле партии — действия через точку, клетка — две буквы
   (столбец a…m, ряд a…k):
     m<откуда><куда>   — пройти
     a<откуда><цель>   — ударить (цель — отряд или замок соперника)
     b<кто><куда>      — купить: s мечник, p копейщик, a лучник, h всадник,
                         c катапульта
   Ход без действий — «-». */

#define SG_W 13
#define SG_H 11
#define SG_CELLS (SG_W * SG_H)
#define SG_MAXU 24       /* отрядов на карте — у обоих вместе */
#define SG_SIDE_UNITS 10 /* не больше у одной стороны */
#define SG_CASTLE_HP 24
#define SG_TURNS 60 /* всего ходов: по 30 у каждого */
#define SG_HP 10
#define SG_LINE 400 /* запись одного хода — знаков */

enum { SGT_PLAIN, SGT_FOREST, SGT_HILL, SGT_MOUNT, SGT_WATER, SGT_VILLAGE, SGT_CASTLE, SGT_VOID };
enum { SGU_SWORD, SGU_SPEAR, SGU_ARCHER, SGU_HORSE, SGU_CATA, SGU_N };
enum { SG_PLAYING, SG_CASTLE_FELL, SG_LIMIT };

typedef struct {
  unsigned char type, side, hp, cell;
  unsigned char alive;
  unsigned char moved; /* в этот ход уже ходил */
  unsigned char done;  /* в этот ход уже бил (или только что куплен) — больше ничего */
} SgUnit;

typedef struct {
  unsigned char terr[SG_CELLS];
  signed char owner[SG_CELLS]; /* деревня и замок: −1 ничья, 0 или 1 */
  signed char at[SG_CELLS];    /* какой отряд на клетке; −1 — пусто */
  SgUnit u[SG_MAXU];
  int gold[2];
  int castle[2]; /* клетки замков */
  int castleHp[2];
  int first;  /* кто ходит первым (нечётные ходы) */
  int turn;   /* номер хода на очереди, с 1 */
  int over;   /* SG_PLAYING, SG_CASTLE_FELL, SG_LIMIT */
  int winner; /* сторона; −1 — ничья (или партия идёт) */
} SgState;

typedef struct {
  const char *letter; /* в записи хода */
  int cost, move, rmin, rmax, siege;
} SgKind;

static const SgKind kSgKind[SGU_N] = {
    {"s", 10, 3, 1, 1, 3}, /* мечник */
    {"p", 10, 3, 1, 1, 3}, /* копейщик */
    {"a", 12, 3, 1, 2, 2}, /* лучник */
    {"h", 16, 5, 1, 1, 3}, /* всадник */
    {"c", 18, 2, 2, 3, 6}, /* катапульта */
};

/* урон по таблице: кто бьёт (строка) → кого (столбец), при 10 силах на поле */
static const unsigned char kSgDmg[SGU_N][SGU_N] = {
    /*            меч коп луч всд кат */
    /* мечник  */ {5, 6, 7, 4, 8},
    /* копейщ. */ {4, 5, 6, 7, 7},
    /* лучник  */ {5, 5, 5, 5, 6},
    /* всадник */ {6, 3, 8, 5, 9},
    /* катап.  */ {6, 6, 6, 5, 6},
};

static int sg_valid(int c, int r) { return r >= 0 && r < SG_H && c >= 0 && c < SG_W - (r & 1); }
static int sg_idx(int c, int r) { return r * SG_W + c; }
static int sg_col(int i) { return i % SG_W; }
static int sg_row(int i) { return i / SG_W; }
static int sg_cell_ok(int i) { return i >= 0 && i < SG_CELLS && sg_valid(sg_col(i), sg_row(i)); }

/* та же клетка с другой стороны карты (поворот на пол-оборота) */
static int sg_mirror(int i) {
  int r = sg_row(i), c = sg_col(i);
  return sg_idx(SG_W - 1 - (r & 1) - c, SG_H - 1 - r);
}

/* шесть соседей: в чётном ряду верхние и нижние — левее, в нечётном — правее */
static int sg_neighbors(int i, int out[6]) {
  int r = sg_row(i), c = sg_col(i), o = r & 1, n = 0;
  const int d[6][2] = {{c - 1, r}, {c + 1, r}, {c - 1 + o, r - 1}, {c + o, r - 1}, {c - 1 + o, r + 1}, {c + o, r + 1}};
  for (int k = 0; k < 6; k++)
    if (sg_valid(d[k][0], d[k][1])) out[n++] = sg_idx(d[k][0], d[k][1]);
  return n;
}

static int sg_dist(int a, int b) {
  int ra = sg_row(a), rb = sg_row(b);
  int qa = sg_col(a) - (ra - (ra & 1)) / 2, qb = sg_col(b) - (rb - (rb & 1)) / 2;
  int dx = qa - qb, dz = ra - rb, dy = -dx - dz;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  if (dz < 0) dz = -dz;
  return dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
}

/* во сколько шагов хода отряду встаёт клетка; 0 — не пройти */
static int sg_move_cost(int type, int terr) {
  switch (terr) {
  case SGT_PLAIN:
  case SGT_VILLAGE:
    return 1;
  case SGT_FOREST:
    return type == SGU_HORSE ? 3 : 2;
  case SGT_HILL:
    return 2;
  }
  return 0;
}

static int sg_defense(int terr) { return terr == SGT_FOREST || terr == SGT_VILLAGE ? 2 : terr == SGT_HILL ? 3 : 0; }

static int sg_side(const SgState *st) { return (st->turn & 1) ? st->first : 1 - st->first; }

static int sg_count(const SgState *st, int side) {
  int n = 0;
  for (int i = 0; i < SG_MAXU; i++) n += st->u[i].alive && st->u[i].side == side;
  return n;
}

static int sg_villages(const SgState *st, int side) {
  int n = 0;
  for (int i = 0; i < SG_CELLS; i++) n += st->terr[i] == SGT_VILLAGE && st->owner[i] == side;
  return n;
}

static int sg_income(const SgState *st, int side) { return 5 + 2 * sg_villages(st, side); }

/* ---- карта ------------------------------------------------------------------ */

static unsigned sg_rnd(unsigned *s) {
  *s = *s * 1664525u + 1013904223u;
  unsigned x = *s;
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;
  return x;
}

static int sg_rn(unsigned *s, int n) { return (int)(sg_rnd(s) % (unsigned)n); }

/* клетку и её пару с другой стороны */
static void sg_put(SgState *st, int cell, int terr) {
  st->terr[cell] = (unsigned char)terr;
  st->terr[sg_mirror(cell)] = (unsigned char)terr;
}

/* свободное поле не у самых замков */
static int sg_gen_free(const SgState *st, int cell, int keep) {
  return sg_cell_ok(cell) && st->terr[cell] == SGT_PLAIN && sg_dist(cell, st->castle[0]) > keep &&
         sg_dist(cell, st->castle[1]) > keep;
}

/* пятно местности из size клеток (и такое же напротив) */
static void sg_blob(SgState *st, unsigned *s, int terr, int size, int keep) {
  int cells[8], n = 0;
  for (int tries = 0; tries < 40 && !n; tries++) {
    int c = sg_rn(s, SG_CELLS);
    if (sg_gen_free(st, c, keep)) cells[n++] = c;
  }
  if (!n) return;
  sg_put(st, cells[0], terr);
  for (int tries = 0; tries < 30 && n < size && n < 8; tries++) {
    int nb[6], k = sg_neighbors(cells[sg_rn(s, n)], nb);
    int c = nb[sg_rn(s, k)];
    if (!sg_gen_free(st, c, keep)) continue;
    sg_put(st, c, terr);
    cells[n++] = c;
  }
}

/* деревня (и такая же напротив): клетка подходит по расстояниям до замков
   и стоит не ближе 3 клеток от других деревень */
static int sg_gen_village(SgState *st, unsigned *s, int dminA, int dmaxA, int diffMax) {
  for (int tries = 0; tries < 200; tries++) {
    int c = sg_rn(s, SG_CELLS);
    if (!sg_cell_ok(c) || st->terr[c] == SGT_VILLAGE || st->terr[c] == SGT_CASTLE) continue;
    int da = sg_dist(c, st->castle[0]), db = sg_dist(c, st->castle[1]);
    if (da < dminA || da > dmaxA || da > db) continue;
    if (diffMax >= 0 && db - da > diffMax) continue;
    int m = sg_mirror(c), ok = sg_dist(c, m) >= 3;
    for (int i = 0; i < SG_CELLS && ok; i++)
      if (st->terr[i] == SGT_VILLAGE && (sg_dist(i, c) < 3 || sg_dist(i, m) < 3)) ok = 0;
    if (!ok) continue;
    sg_put(st, c, SGT_VILLAGE);
    return 1;
  }
  return 0;
}

/* можно ли пешком от замка дойти до всех деревень и до замка соперника */
static int sg_gen_connected(const SgState *st) {
  unsigned char seen[SG_CELLS];
  int q[SG_CELLS], h = 0, t = 0;
  memset(seen, 0, sizeof(seen));
  q[t++] = st->castle[0];
  seen[st->castle[0]] = 1;
  int reachB = 0;
  while (h < t) {
    int c = q[h++], nb[6], k = sg_neighbors(c, nb);
    for (int i = 0; i < k; i++) {
      if (nb[i] == st->castle[1]) reachB = 1;
      if (seen[nb[i]] || !sg_move_cost(SGU_SWORD, st->terr[nb[i]])) continue;
      seen[nb[i]] = 1;
      q[t++] = nb[i];
    }
  }
  if (!reachB) return 0;
  for (int i = 0; i < SG_CELLS; i++)
    if (st->terr[i] == SGT_VILLAGE && !seen[i]) return 0;
  return 1;
}

static int sg_gen_try(SgState *st, unsigned seed, int plainOnly) {
  unsigned s = seed;
  for (int i = 0; i < SG_CELLS; i++) {
    st->terr[i] = sg_cell_ok(i) ? SGT_PLAIN : SGT_VOID;
    st->owner[i] = -1;
    st->at[i] = -1;
  }
  st->castle[0] = sg_idx(1, 2 + sg_rn(&s, 7));
  st->castle[1] = sg_mirror(st->castle[0]);
  st->terr[st->castle[0]] = st->terr[st->castle[1]] = SGT_CASTLE;
  st->owner[st->castle[0]] = 0;
  st->owner[st->castle[1]] = 1;
  if (!plainOnly) {
    for (int i = 0; i < 2; i++) sg_blob(st, &s, SGT_MOUNT, 2 + sg_rn(&s, 2), 2);
    for (int i = 1 + sg_rn(&s, 2); i > 0; i--) sg_blob(st, &s, SGT_WATER, 2 + sg_rn(&s, 3), 2);
    for (int i = 0; i < 4; i++) sg_blob(st, &s, SGT_FOREST, 2 + sg_rn(&s, 3), 1);
    for (int i = 0; i < 3; i++) sg_blob(st, &s, SGT_HILL, 1 + sg_rn(&s, 3), 1);
  }
  /* по две деревни у дома, по одной на подступах и две пары посередине */
  int v = sg_gen_village(st, &s, 2, 3, -1) + sg_gen_village(st, &s, 2, 3, -1) + sg_gen_village(st, &s, 4, 6, -1) +
          sg_gen_village(st, &s, 5, 9, 2) + sg_gen_village(st, &s, 5, 9, 2);
  if (v < 5) return 0;
  /* свои деревни у дома — с самого начала свои */
  for (int i = 0; i < SG_CELLS; i++)
    if (st->terr[i] == SGT_VILLAGE)
      st->owner[i] = sg_dist(i, st->castle[0]) <= 3 ? 0 : sg_dist(i, st->castle[1]) <= 3 ? 1 : -1;
  return sg_gen_connected(st);
}

/* начало партии: карта по seed, по три отряда у замков; first — кто ходит первым */
static void sg_start(SgState *st, unsigned seed, int first) {
  memset(st, 0, sizeof(*st));
  int ok = 0;
  for (int a = 0; a < 60 && !ok; a++) ok = sg_gen_try(st, seed * 2654435761u + (unsigned)a * 40503u + 17u, 0);
  for (int a = 0; a < 60 && !ok; a++) ok = sg_gen_try(st, seed + (unsigned)a, 1);
  st->first = first ? 1 : 0;
  st->turn = 1;
  st->winner = -1;
  st->castleHp[0] = st->castleHp[1] = SG_CASTLE_HP;
  st->gold[0] = st->gold[1] = 10;
  st->gold[1 - st->first] += 3; /* второму — за то, что ходит вторым */
  /* мечник, копейщик и лучник — на клетках у замка, ближних к сопернику */
  int nb[6], k = sg_neighbors(st->castle[0], nb);
  for (int i = 0; i < k; i++)
    for (int j = i + 1; j < k; j++) {
      int di = sg_dist(nb[i], st->castle[1]), dj = sg_dist(nb[j], st->castle[1]);
      if (dj < di || (dj == di && nb[j] < nb[i])) {
        int t = nb[i];
        nb[i] = nb[j];
        nb[j] = t;
      }
    }
  int n = 0;
  for (int t = 0; t < 3 && t < k; t++)
    for (int side = 0; side < 2; side++) {
      int cell = side ? sg_mirror(nb[t]) : nb[t];
      if (!sg_move_cost(SGU_SWORD, st->terr[cell])) continue;
      SgUnit *u = &st->u[n];
      u->type = (unsigned char)t; /* SGU_SWORD, SGU_SPEAR, SGU_ARCHER */
      u->side = (unsigned char)side;
      u->hp = SG_HP;
      u->cell = (unsigned char)cell;
      u->alive = 1;
      st->at[cell] = (signed char)n++;
    }
}

/* ---- ход -------------------------------------------------------------------- */

/* Куда отряд может дойти: cost[клетка] — сколько шагов (255 — не дойти),
   prev — откуда пришли (для пути). Сквозь своих идти можно, сквозь чужих —
   нет; встать — только на пустую. */
static void sg_reach(const SgState *st, int ui, unsigned char cost[SG_CELLS], signed short prev[SG_CELLS]) {
  const SgUnit *u = &st->u[ui];
  memset(cost, 255, SG_CELLS);
  for (int i = 0; i < SG_CELLS; i++) prev[i] = -1;
  if (!u->alive) return;
  int mv = kSgKind[u->type].move;
  cost[u->cell] = 0;
  /* карта маленькая: просто повторяем проход, пока что-то улучшается */
  for (int changed = 1; changed;) {
    changed = 0;
    for (int c = 0; c < SG_CELLS; c++) {
      if (cost[c] == 255) continue;
      int nb[6], k = sg_neighbors(c, nb);
      for (int i = 0; i < k; i++) {
        int mc = sg_move_cost(u->type, st->terr[nb[i]]);
        if (!mc) continue;
        int o = st->at[nb[i]];
        if (o >= 0 && st->u[o].side != u->side) continue;
        int nc = cost[c] + mc;
        if (nc > mv || nc >= cost[nb[i]]) continue;
        cost[nb[i]] = (unsigned char)nc;
        prev[nb[i]] = (signed short)c;
        changed = 1;
      }
    }
  }
}

static int sg_can_stop(const SgState *st, const unsigned char cost[SG_CELLS], int cell) {
  return cost[cell] != 255 && cost[cell] != 0 && st->at[cell] < 0;
}

/* путь отряда до клетки: клетки по порядку, без начальной; 0 — не дойти */
static int sg_path(const SgState *st, int ui, int to, int out[16]) {
  unsigned char cost[SG_CELLS];
  signed short prev[SG_CELLS];
  sg_reach(st, ui, cost, prev);
  if (!sg_cell_ok(to) || !sg_can_stop(st, cost, to)) return 0;
  int rev[16], n = 0;
  for (int c = to; c != st->u[ui].cell && c >= 0 && n < 16; c = prev[c]) rev[n++] = c;
  for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
  return n;
}

/* на сколько клеток бьёт отряд, стоя на клетке cell */
static int sg_range_max(const SgState *st, int type, int cell) {
  return kSgKind[type].rmax + ((type == SGU_ARCHER || type == SGU_CATA) && st->terr[cell] == SGT_HILL ? 1 : 0);
}

static int sg_in_range(const SgState *st, int type, int from, int target) {
  int d = sg_dist(from, target);
  return d >= kSgKind[type].rmin && d <= sg_range_max(st, type, from);
}

/* может ли отряд сейчас бить вообще (ещё не бил; катапульта — и не ехала) */
static int sg_can_strike(const SgUnit *u) { return u->alive && !u->done && !(u->type == SGU_CATA && u->moved); }

/* может ли отряд ui со своей клетки ударить по клетке target (отряд или замок соперника) */
static int sg_can_attack(const SgState *st, int ui, int target) {
  const SgUnit *u = &st->u[ui];
  if (!sg_can_strike(u) || !sg_cell_ok(target) || !sg_in_range(st, u->type, u->cell, target)) return 0;
  if (target == st->castle[1 - u->side]) return 1;
  int o = st->at[target];
  return o >= 0 && st->u[o].side != u->side;
}

static int sg_damage(int att, int hp, int def, int terr) {
  int d = (kSgDmg[att][def] * hp * (10 - sg_defense(terr)) + 50) / 100;
  return d < 1 ? 1 : d;
}

/* Что будет, если отряд типа type с hp сил ударит с клетки from по target:
   dmg — урон цели, back — ответный урон (0 — не ответит). Цель — отряд или
   замок. */
static void sg_forecast(const SgState *st, int type, int hp, int from, int target, int *dmg, int *back) {
  *dmg = *back = 0;
  int o = st->at[target];
  if (o < 0) { /* замок */
    int d = (kSgKind[type].siege * hp + 5) / 10;
    *dmg = d < 1 ? 1 : d;
    if (sg_dist(from, target) == 1) *back = 2;
    return;
  }
  const SgUnit *v = &st->u[o];
  *dmg = sg_damage(type, hp, v->type, st->terr[target]);
  int left = v->hp - *dmg;
  if (left > 0 && v->type != SGU_CATA && sg_in_range(st, v->type, target, from))
    *back = sg_damage(v->type, left, type, st->terr[from]);
}

typedef struct {
  char kind;  /* 'm', 'a', 'b' */
  int a, b;   /* клетки: откуда и куда (цель); у покупки — только b */
  int type;   /* у покупки — кого */
} SgAct;

static void sg_kill(SgState *st, int ui) {
  st->u[ui].alive = 0;
  st->at[st->u[ui].cell] = -1;
}

/* клетка у своего замка, куда можно поставить купленного */
static int sg_can_buy_at(const SgState *st, int side, int cell) {
  return sg_cell_ok(cell) && st->at[cell] < 0 && sg_dist(cell, st->castle[side]) == 1 &&
         sg_move_cost(SGU_SWORD, st->terr[cell]);
}

/* Выполнить действие стороны, чей ход. 1 — сделано, 0 — по правилам нельзя
   (позиция тогда не тронута). */
static int sg_apply(SgState *st, const SgAct *a) {
  if (st->over) return 0;
  int side = sg_side(st);
  if (a->kind == 'b') {
    if (a->type < 0 || a->type >= SGU_N || !sg_can_buy_at(st, side, a->b)) return 0;
    if (st->gold[side] < kSgKind[a->type].cost || sg_count(st, side) >= SG_SIDE_UNITS) return 0;
    int slot = -1;
    for (int i = 0; i < SG_MAXU && slot < 0; i++)
      if (!st->u[i].alive) slot = i;
    if (slot < 0) return 0;
    SgUnit *u = &st->u[slot];
    memset(u, 0, sizeof(*u));
    u->type = (unsigned char)a->type;
    u->side = (unsigned char)side;
    u->hp = SG_HP;
    u->cell = (unsigned char)a->b;
    u->alive = u->moved = u->done = 1;
    st->at[a->b] = (signed char)slot;
    st->gold[side] -= kSgKind[a->type].cost;
    return 1;
  }
  if (!sg_cell_ok(a->a) || !sg_cell_ok(a->b)) return 0;
  int ui = st->at[a->a];
  if (ui < 0 || st->u[ui].side != side) return 0;
  SgUnit *u = &st->u[ui];
  if (a->kind == 'm') {
    if (u->moved || u->done) return 0;
    unsigned char cost[SG_CELLS];
    signed short prev[SG_CELLS];
    sg_reach(st, ui, cost, prev);
    if (!sg_can_stop(st, cost, a->b)) return 0;
    st->at[u->cell] = -1;
    u->cell = (unsigned char)a->b;
    st->at[a->b] = (signed char)ui;
    u->moved = 1;
    if (st->terr[a->b] == SGT_VILLAGE && u->type != SGU_CATA) st->owner[a->b] = (signed char)side;
    return 1;
  }
  if (a->kind == 'a') {
    if (!sg_can_attack(st, ui, a->b)) return 0;
    int dmg, back;
    sg_forecast(st, u->type, u->hp, u->cell, a->b, &dmg, &back);
    u->moved = u->done = 1;
    int o = st->at[a->b];
    if (o < 0) {
      st->castleHp[1 - side] -= dmg;
      if (st->castleHp[1 - side] <= 0) {
        st->castleHp[1 - side] = 0;
        st->over = SG_CASTLE_FELL;
        st->winner = side;
        return 1;
      }
    } else if (st->u[o].hp <= dmg) sg_kill(st, o);
    else st->u[o].hp = (unsigned char)(st->u[o].hp - dmg);
    if (back >= u->hp) sg_kill(st, ui);
    else u->hp = (unsigned char)(u->hp - back);
    return 1;
  }
  return 0;
}

/* начало хода стороны: не кончилась ли партия по числу ходов; золото,
   лечение на своих деревнях, отряды снова могут ходить */
static void sg_begin_turn(SgState *st) {
  if (st->over) return;
  if (st->turn > SG_TURNS) {
    st->over = SG_LIMIT;
    int d = st->castleHp[0] - st->castleHp[1];
    if (!d) d = sg_villages(st, 0) - sg_villages(st, 1);
    st->winner = d > 0 ? 0 : d < 0 ? 1 : -1;
    return;
  }
  int side = sg_side(st);
  st->gold[side] += sg_income(st, side);
  for (int i = 0; i < SG_MAXU; i++) {
    SgUnit *u = &st->u[i];
    if (!u->alive || u->side != side) continue;
    u->moved = u->done = 0;
    if (st->terr[u->cell] == SGT_VILLAGE && st->owner[u->cell] == side && u->hp < SG_HP)
      u->hp = (unsigned char)(u->hp + 2 > SG_HP ? SG_HP : u->hp + 2);
  }
}

static void sg_end_turn(SgState *st) {
  if (!st->over) st->turn++;
}

/* ---- запись хода ------------------------------------------------------------- */

static void sg_cell_str(int cell, char *out) {
  out[0] = (char)('a' + sg_col(cell));
  out[1] = (char)('a' + sg_row(cell));
}

static int sg_cell_parse(const char *s) {
  int c = s[0] - 'a', r = s[1] - 'a';
  return sg_valid(c, r) ? sg_idx(c, r) : -1;
}

/* действие → запись (без точки); возвращает длину */
static int sg_act_str(const SgAct *a, char *out) {
  out[0] = a->kind;
  if (a->kind == 'b') {
    out[1] = kSgKind[a->type].letter[0];
    sg_cell_str(a->b, out + 2);
    out[4] = 0;
    return 4;
  }
  sg_cell_str(a->a, out + 1);
  sg_cell_str(a->b, out + 3);
  out[5] = 0;
  return 5;
}

/* запись → действие; возвращает, сколько знаков прочитано, 0 — не запись */
static int sg_act_parse(const char *s, SgAct *a) {
  memset(a, 0, sizeof(*a));
  a->kind = s[0];
  if (s[0] == 'b') {
    a->type = -1;
    for (int t = 0; t < SGU_N; t++)
      if (s[1] == kSgKind[t].letter[0]) a->type = t;
    if (a->type < 0 || !s[2] || !s[3]) return 0;
    a->b = sg_cell_parse(s + 2);
    return a->b < 0 ? 0 : 4;
  }
  if (s[0] != 'm' && s[0] != 'a') return 0;
  if (!s[1] || !s[2] || !s[3] || !s[4]) return 0;
  a->a = sg_cell_parse(s + 1);
  a->b = sg_cell_parse(s + 3);
  return a->a < 0 || a->b < 0 ? 0 : 5;
}

/* Выполнить записанные действия (через точку; «-» или пусто — без действий).
   Возвращает, сколько действий выполнено, или −1 — запись неверна или
   действие не по правилам (выполненные до него остаются). acts — куда
   сложить разобранные (можно NULL), cap — сколько влезет. */
static int sg_apply_line(SgState *st, const char *line, SgAct *acts, int cap) {
  int n = 0;
  if (!line || !line[0] || (line[0] == '-' && !line[1])) return 0;
  for (const char *p = line; *p;) {
    SgAct a;
    int l = sg_act_parse(p, &a);
    if (!l || (p[l] && p[l] != '.')) return -1;
    if (!sg_apply(st, &a)) return -1;
    if (acts && n < cap) acts[n] = a;
    n++;
    p += l;
    if (*p == '.') {
      p++;
      if (!*p) return -1;
    }
    if (st->over) return *p ? -1 : n; /* после разрушенного замка действий нет */
  }
  return n;
}

/* зерно карты по номеру партии */
static unsigned sg_seed(const wchar_t *game) {
  unsigned h = 2166136261u;
  for (const wchar_t *p = game; p && *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
  return h ? h : 1u;
}
