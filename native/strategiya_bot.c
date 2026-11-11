/* ---- Стратегия: компьютерный соперник ---------------------------------------

   Без Windows. Играет за сторону, чей ход: бьёт, где размен выгоден, идёт к
   ничьим деревням и к замку соперника, возвращается, когда чужие у своего
   замка, покупает отряды против того, что есть у соперника. Ничего не
   просчитывает вперёд — это соперник для знакомства с игрой («Игра с
   компьютером») и для проверки правил: в strategiya_test.c он сотни партий
   играет сам с собой, и каждое его действие проходит проверку правил.

   Случайностей нет: из одной позиции — всегда один и тот же ход. */

static int sg_bot_val(int type) { return kSgKind[type].cost; }

/* Во сколько шагов хода отряд типа type дойдёт из каждой клетки до goal
   (на чужие отряды не смотрит). Сама goal может быть замком. */
static void sg_bot_field(const SgState *st, int type, int goal, unsigned short d[SG_CELLS]) {
  for (int i = 0; i < SG_CELLS; i++) d[i] = 9999;
  d[goal] = 0;
  for (int changed = 1; changed;) {
    changed = 0;
    for (int c = 0; c < SG_CELLS; c++) {
      if (d[c] == 9999) continue;
      int into = sg_move_cost(type, st->terr[c]);
      if (!into) {
        if (c != goal) continue;
        into = 1;
      }
      int nb[6], k = sg_neighbors(c, nb);
      for (int i = 0; i < k; i++) {
        if (!sg_move_cost(type, st->terr[nb[i]])) continue;
        int nd = d[c] + into;
        if (nd < d[nb[i]]) {
          d[nb[i]] = (unsigned short)nd;
          changed = 1;
        }
      }
    }
  }
}

/* насколько хорош удар отряда ui с клетки from по клетке target */
static int sg_bot_strike(const SgState *st, int ui, int from, int target) {
  const SgUnit *u = &st->u[ui];
  int dmg, back, s;
  sg_forecast(st, u->type, u->hp, from, target, &dmg, &back);
  int o = st->at[target];
  if (o < 0) {
    s = dmg >= st->castleHp[1 - u->side] ? 100000 : dmg * 14;
  } else {
    const SgUnit *v = &st->u[o];
    s = (dmg > v->hp ? v->hp : dmg) * sg_bot_val(v->type);
    if (dmg >= v->hp) s += 4 * sg_bot_val(v->type);
    if (sg_dist(target, st->castle[u->side]) <= 3) s += 30; /* чужой у нашего замка — в первую очередь */
  }
  s -= (back > u->hp ? u->hp : back) * sg_bot_val(u->type) * 8 / 10;
  if (back >= u->hp) s -= 6 * sg_bot_val(u->type);
  return s;
}

/* выполнить действие и дописать его в запись хода */
static int sg_bot_do(SgState *st, const SgAct *a, char *out, int cap) {
  if (!sg_apply(st, a)) return 0;
  char t[8];
  int l = sg_act_str(a, t), have = (int)strlen(out);
  if (have + l + 2 < cap) {
    if (have) out[have++] = '.';
    memcpy(out + have, t, (size_t)l + 1);
  }
  return 1;
}

/* лучший удар стороны: кем, откуда (после перехода) и по кому; 0 — выгодных нет */
static int sg_bot_best_strike(const SgState *st, int side, int *who, int *from, int *target) {
  int best = 0;
  for (int ui = 0; ui < SG_MAXU; ui++) {
    const SgUnit *u = &st->u[ui];
    if (!u->alive || u->side != side || u->done) continue;
    unsigned char cost[SG_CELLS];
    signed short prev[SG_CELLS];
    sg_reach(st, ui, cost, prev);
    for (int p = 0; p < SG_CELLS; p++) {
      int here = p == u->cell;
      if (!here && (u->moved || u->type == SGU_CATA || !sg_can_stop(st, cost, p))) continue;
      if (here && u->type == SGU_CATA && u->moved) continue;
      for (int t = 0; t < SG_CELLS; t++) {
        int o = st->at[t];
        int enemy = (o >= 0 && st->u[o].side != side) || t == st->castle[1 - side];
        if (!enemy || !sg_in_range(st, u->type, p, t)) continue;
        int s = sg_bot_strike(st, ui, p, t) + sg_defense(st->terr[p]) * 3 - (here ? 0 : 1);
        if (!here && st->terr[p] == SGT_VILLAGE && st->owner[p] != side) s += 60;
        if (s > best) {
          best = s;
          *who = ui;
          *from = p;
          *target = t;
        }
      }
    }
  }
  return best > 0;
}

static void sg_bot_strikes(SgState *st, int side, char *out, int cap) {
  int who, from, target;
  while (!st->over && sg_bot_best_strike(st, side, &who, &from, &target)) {
    SgAct a = {'m', st->u[who].cell, from, 0};
    if (from != st->u[who].cell && !sg_bot_do(st, &a, out, cap)) break;
    a.kind = 'a';
    a.a = from;
    a.b = target;
    if (!sg_bot_do(st, &a, out, cap)) break;
  }
}

/* куда идти отряду, которому некого бить: клетка-цель */
static int sg_bot_goal(const SgState *st, int ui, const unsigned char claimed[SG_CELLS]) {
  const SgUnit *u = &st->u[ui];
  int side = u->side, home = st->castle[side];
  /* чужой отряд у нашего замка, а мы недалеко — к нему */
  int raider = -1, rd = 99;
  for (int i = 0; i < SG_MAXU; i++) {
    const SgUnit *v = &st->u[i];
    if (!v->alive || v->side == side || sg_dist(v->cell, home) > 3) continue;
    int d = sg_dist(v->cell, u->cell);
    if (d < rd) rd = d, raider = v->cell;
  }
  if (raider >= 0 && rd <= 7) return raider;
  if (u->type != SGU_CATA) {
    /* ближняя не наша деревня, к которой ещё никто не идёт */
    unsigned short d[SG_CELLS];
    sg_bot_field(st, u->type, u->cell, d);
    int best = -1;
    for (int c = 0; c < SG_CELLS; c++)
      if (st->terr[c] == SGT_VILLAGE && st->owner[c] != side && !claimed[c] && d[c] <= 4 * kSgKind[u->type].move &&
          (best < 0 || d[c] < d[best]))
        best = c;
    if (best >= 0) return best;
  }
  return st->castle[1 - side];
}

static void sg_bot_moves(SgState *st, int side, char *out, int cap) {
  unsigned char claimed[SG_CELLS];
  memset(claimed, 0, sizeof(claimed));
  for (int ui = 0; ui < SG_MAXU; ui++) {
    const SgUnit *u = &st->u[ui];
    if (!u->alive || u->side != side || u->moved || u->done) continue;
    int goal = sg_bot_goal(st, ui, claimed);
    if (st->terr[goal] == SGT_VILLAGE) claimed[goal] = 1;
    unsigned short d[SG_CELLS];
    sg_bot_field(st, u->type, goal, d);
    unsigned char cost[SG_CELLS];
    signed short prev[SG_CELLS];
    sg_reach(st, ui, cost, prev);
    int ranged = kSgKind[u->type].rmax > 1;
    int best = -1, bs = 0;
    for (int p = 0; p < SG_CELLS; p++) {
      int here = p == u->cell;
      if (!here && !sg_can_stop(st, cost, p)) continue;
      int s = -(int)d[p] * 10 + sg_defense(st->terr[p]) * 2;
      /* стрелкам хватит подойти на выстрел, и лучше не вплотную */
      if (ranged && goal == st->castle[1 - side] && sg_in_range(st, u->type, p, goal))
        s = 5 + sg_defense(st->terr[p]) * 2 - (sg_dist(p, goal) == 1 ? 6 : 0);
      if (!here && st->terr[p] == SGT_VILLAGE && st->owner[p] != side && u->type != SGU_CATA) s += 200;
      if (best < 0 || s > bs) best = p, bs = s;
    }
    if (best >= 0 && best != u->cell) {
      SgAct a = {'m', u->cell, best, 0};
      sg_bot_do(st, &a, out, cap);
    }
  }
}

static void sg_bot_buy(SgState *st, int side, char *out, int cap) {
  for (int guard = 0; guard < SG_SIDE_UNITS; guard++) {
    if (sg_count(st, side) >= SG_SIDE_UNITS) return;
    int have[SGU_N] = {0}, mine = 0;
    for (int i = 0; i < SG_MAXU; i++)
      if (st->u[i].alive && st->u[i].side == side) have[st->u[i].type]++, mine++;
    static const int kBase[SGU_N] = {10, 10, 9, 9, 4};
    int best = -1, bs = 0;
    for (int t = 0; t < SGU_N; t++) {
      if (st->gold[side] < kSgKind[t].cost) continue;
      int s = kBase[t] - 3 * have[t];
      for (int i = 0; i < SG_MAXU; i++)
        if (st->u[i].alive && st->u[i].side != side)
          s += ((int)kSgDmg[t][st->u[i].type] - (int)kSgDmg[st->u[i].type][t]) * 2;
      if (t == SGU_CATA) s += mine >= 4 && have[t] < 2 ? 12 : -20;
      if (best < 0 || s > bs) best = t, bs = s;
    }
    if (best < 0) return;
    int nb[6], k = sg_neighbors(st->castle[side], nb), cell = -1;
    for (int i = 0; i < k; i++)
      if (sg_can_buy_at(st, side, nb[i]) &&
          (cell < 0 || sg_dist(nb[i], st->castle[1 - side]) < sg_dist(cell, st->castle[1 - side])))
        cell = nb[i];
    if (cell < 0) return;
    SgAct a = {'b', 0, cell, best};
    if (!sg_bot_do(st, &a, out, cap)) return;
  }
}

/* Ход компьютера: позиция st — после sg_begin_turn; действия выполняются в
   ней же, запись хода — в out (как в файле партии; «-» — без действий). */
static void sg_bot_turn(SgState *st, char *out, int cap) {
  int side = sg_side(st);
  out[0] = 0;
  sg_bot_strikes(st, side, out, cap);
  if (!st->over) sg_bot_moves(st, side, out, cap);
  if (!st->over) sg_bot_strikes(st, side, out, cap); /* подошли — может, теперь достаём */
  if (!st->over) sg_bot_buy(st, side, out, cap);
  if (!out[0] && cap > 1) out[0] = '-', out[1] = 0;
}
