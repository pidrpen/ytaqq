/* Проверка правил «Стратегии» (strategiya_rules.c) и компьютерного соперника
   (strategiya_bot.c) без Windows:
   gcc -O2 -Wall -Werror -o strategiya_test strategiya_test.c && ./strategiya_test
   Карты (симметрия, проходимость), ходы и удары по таблице, запись хода,
   отказ на всё, что не по правилам, и партии «компьютер против компьютера»:
   каждая кончается, каждое действие проходит проверку правил.
   ./strategiya_test maps [N] — показать первые N карт буквами. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "strategiya_rules.c"
#include "strategiya_bot.c"

static int g_fail, g_n;

static void ok(int cond, const char *what) {
  g_n++;
  if (!cond) {
    g_fail++;
    printf("НЕ ТАК: %s\n", what);
  }
}

static void dump(const SgState *st) {
  static const char *kT = ".fh^~vC ";
  for (int r = 0; r < SG_H; r++) {
    if (r & 1) putchar(' ');
    for (int c = 0; c < SG_W; c++) {
      int i = sg_idx(c, r);
      if (!sg_valid(c, r)) continue;
      int u = st->at[i];
      char ch = kT[st->terr[i]];
      if (u >= 0) ch = (char)((st->u[u].side ? 'a' : 'A') + st->u[u].type);
      else if (st->terr[i] == SGT_VILLAGE && st->owner[i] >= 0) ch = st->owner[i] ? 'w' : 'V';
      putchar(ch);
      putchar(' ');
    }
    putchar('\n');
  }
}

/* пустая карта для проверок: поле, замки по углам, без отрядов */
static void blank(SgState *st) {
  memset(st, 0, sizeof(*st));
  for (int i = 0; i < SG_CELLS; i++) {
    st->terr[i] = sg_cell_ok(i) ? SGT_PLAIN : SGT_VOID;
    st->owner[i] = -1;
    st->at[i] = -1;
  }
  st->castle[0] = sg_idx(1, 5);
  st->castle[1] = sg_mirror(st->castle[0]);
  st->terr[st->castle[0]] = st->terr[st->castle[1]] = SGT_CASTLE;
  st->owner[st->castle[0]] = 0;
  st->owner[st->castle[1]] = 1;
  st->castleHp[0] = st->castleHp[1] = SG_CASTLE_HP;
  st->turn = 1;
  st->winner = -1;
}

static int put(SgState *st, int type, int side, int c, int r, int hp) {
  for (int i = 0; i < SG_MAXU; i++)
    if (!st->u[i].alive) {
      SgUnit *u = &st->u[i];
      memset(u, 0, sizeof(*u));
      u->type = (unsigned char)type;
      u->side = (unsigned char)side;
      u->hp = (unsigned char)hp;
      u->cell = (unsigned char)sg_idx(c, r);
      u->alive = 1;
      st->at[u->cell] = (signed char)i;
      return i;
    }
  return -1;
}

static void test_geometry(void) {
  int cells = 0;
  for (int i = 0; i < SG_CELLS; i++) {
    if (!sg_cell_ok(i)) continue;
    cells++;
    int m = sg_mirror(i);
    ok(sg_cell_ok(m) && sg_mirror(m) == i && m != i, "зеркало клетки");
    int nb[6], k = sg_neighbors(i, nb);
    for (int j = 0; j < k; j++) {
      ok(sg_dist(i, nb[j]) == 1, "сосед на расстоянии 1");
      int nb2[6], k2 = sg_neighbors(nb[j], nb2), back = 0;
      for (int q = 0; q < k2; q++) back += nb2[q] == i;
      ok(back == 1, "соседство взаимно");
      /* у зеркальных клеток соседи — зеркальные */
      ok(sg_dist(sg_mirror(i), sg_mirror(nb[j])) == 1, "зеркало сохраняет соседство");
    }
    int near = 0;
    for (int j = 0; j < SG_CELLS; j++) near += sg_cell_ok(j) && sg_dist(i, j) == 1;
    ok(near == k, "расстояние 1 — только соседи");
  }
  ok(cells == 138, "клеток 138");
  ok(sg_dist(sg_idx(0, 0), sg_idx(12, 0)) == 12, "расстояние вдоль ряда");
  ok(sg_dist(sg_idx(0, 0), sg_idx(0, 2)) == 2 && sg_dist(sg_idx(0, 0), sg_idx(1, 2)) == 2, "расстояние через ряд");
  ok(sg_dist(sg_idx(0, 0), sg_idx(12, 10)) == 17, "расстояние из угла в угол");
}

static void test_maps(void) {
  int plainOnly = 0;
  for (unsigned seed = 1; seed <= 3000; seed++) {
    SgState st;
    sg_start(&st, seed, (int)(seed & 1));
    int sym = 1, villages = 0, block = 0;
    for (int i = 0; i < SG_CELLS; i++) {
      if (!sg_cell_ok(i)) continue;
      int m = sg_mirror(i);
      if (st.terr[i] != st.terr[m]) sym = 0;
      if (st.owner[i] >= 0 && st.owner[m] != 1 - st.owner[i]) sym = 0;
      if (st.owner[i] < 0 && st.owner[m] >= 0) sym = 0;
      if ((st.at[i] >= 0) != (st.at[m] >= 0)) sym = 0;
      if (st.at[i] >= 0 && st.u[st.at[i]].type != st.u[st.at[m]].type) sym = 0;
      villages += st.terr[i] == SGT_VILLAGE;
      block += st.terr[i] == SGT_MOUNT || st.terr[i] == SGT_WATER;
    }
    if (!block) plainOnly++;
    ok(sym, "карта симметрична");
    ok(villages == 10, "деревень 10");
    ok(sg_villages(&st, 0) == 2 && sg_villages(&st, 1) == 2, "по две своих деревни");
    ok(sg_gen_connected(&st), "до всех деревень и замка можно дойти");
    ok(sg_count(&st, 0) == 3 && sg_count(&st, 1) == 3, "по три отряда");
    ok(st.gold[st.first] == 10 && st.gold[1 - st.first] == 13, "золото в начале");
    int nb[6], k = sg_neighbors(st.castle[0], nb);
    ok(k == 6, "вокруг замка шесть клеток");
    for (int j = 0; j < k; j++) ok(sg_move_cost(SGU_SWORD, st.terr[nb[j]]) == 1, "вокруг замка — поле");
    /* та же партия — та же карта */
    SgState st2;
    sg_start(&st2, seed, (int)(seed & 1));
    ok(!memcmp(&st, &st2, sizeof(st)), "карта по номеру партии одна и та же");
  }
  ok(plainOnly < 30, "карты без гор и воды — редкость");
}

static void test_move(void) {
  SgState st;
  blank(&st);
  st.first = 0;
  int s = put(&st, SGU_SWORD, 0, 4, 4, 10), h = put(&st, SGU_HORSE, 0, 4, 6, 10), c = put(&st, SGU_CATA, 0, 2, 2, 10);
  st.terr[sg_idx(5, 4)] = SGT_FOREST;
  st.terr[sg_idx(6, 4)] = SGT_FOREST;
  st.terr[sg_idx(3, 4)] = SGT_MOUNT;
  st.terr[sg_idx(5, 6)] = SGT_FOREST;
  st.terr[sg_idx(4, 2)] = SGT_VILLAGE;
  put(&st, SGU_SPEAR, 1, 4, 3, 10); /* чужой — не пройти сквозь */
  put(&st, SGU_SPEAR, 0, 3, 3, 10); /* свой — пройти можно, встать нельзя */
  unsigned char cost[SG_CELLS];
  signed short prev[SG_CELLS];
  sg_reach(&st, s, cost, prev);
  ok(cost[sg_idx(5, 4)] == 2 && cost[sg_idx(6, 4)] == 255, "лес — два шага, второй лес уже не достать");
  ok(cost[sg_idx(3, 4)] == 255, "в горы нельзя");
  ok(cost[sg_idx(4, 3)] == 255, "на чужого нельзя");
  ok(cost[sg_idx(3, 3)] == 1 && !sg_can_stop(&st, cost, sg_idx(3, 3)), "сквозь своего можно, встать нельзя");
  ok(cost[sg_idx(2, 3)] == 2 && sg_can_stop(&st, cost, sg_idx(2, 3)), "за своим — можно встать");
  ok(cost[st.castle[0]] == 255, "в замок нельзя");
  sg_reach(&st, h, cost, prev);
  ok(cost[sg_idx(5, 6)] == 3 && cost[sg_idx(8, 6)] == 5 && cost[sg_idx(9, 6)] == 255, "всадник: лес 3, в обход леса 5");
  int path[16], n = sg_path(&st, h, sg_idx(8, 6), path);
  ok(n == 5 && path[4] == sg_idx(8, 6) && sg_dist(path[0], sg_idx(4, 6)) == 1 && path[0] != sg_idx(5, 6),
     "путь всадника в обход леса");
  ok(!sg_path(&st, h, sg_idx(9, 6), path) && !sg_path(&st, h, sg_idx(4, 6), path), "пути нет: далеко; на месте");
  sg_begin_turn(&st);
  SgAct a = {'m', sg_idx(4, 4), sg_idx(6, 4), 0};
  ok(!sg_apply(&st, &a), "слишком далеко — отказ");
  a.b = sg_idx(5, 4);
  ok(sg_apply(&st, &a) && st.at[sg_idx(5, 4)] == s && st.at[sg_idx(4, 4)] < 0, "ход в лес");
  a.a = sg_idx(5, 4), a.b = sg_idx(4, 4);
  ok(!sg_apply(&st, &a), "второй раз за ход — отказ");
  /* катапульта: проехала — бить нельзя, деревню не занимает */
  put(&st, SGU_SWORD, 1, 4, 0, 10);
  SgAct m2 = {'m', sg_idx(2, 2), sg_idx(4, 2), 0};
  ok(sg_apply(&st, &m2) && st.owner[sg_idx(4, 2)] == -1, "катапульта деревню не занимает");
  SgAct at = {'a', sg_idx(4, 2), sg_idx(4, 0), 0};
  ok(sg_dist(sg_idx(4, 2), sg_idx(4, 0)) == 2 && !sg_apply(&st, &at), "катапульта после хода не бьёт");
  (void)c;
  /* деревню занимает тот, кто встал */
  SgAct m3 = {'m', sg_idx(4, 6), sg_idx(3, 6), 0};
  st.terr[sg_idx(3, 6)] = SGT_VILLAGE;
  st.owner[sg_idx(3, 6)] = 1;
  ok(sg_apply(&st, &m3) && st.owner[sg_idx(3, 6)] == 0, "деревня занята");
}

static void test_fight(void) {
  SgState st;
  int dmg, back;
  blank(&st);
  int sw = put(&st, SGU_SWORD, 0, 4, 4, 10);
  int sp = put(&st, SGU_SPEAR, 1, 5, 4, 10);
  sg_forecast(&st, SGU_SWORD, 10, sg_idx(4, 4), sg_idx(5, 4), &dmg, &back);
  ok(dmg == 6 && back == 2, "мечник по копейщику: 6, ответ 2 (4 силы · 4 / 10)");
  st.terr[sg_idx(5, 4)] = SGT_HILL;
  sg_forecast(&st, SGU_SWORD, 10, sg_idx(4, 4), sg_idx(5, 4), &dmg, &back);
  ok(dmg == 4 && back == 2, "на холме урон меньше: 6 · 0.7 = 4");
  sg_forecast(&st, SGU_SWORD, 1, sg_idx(4, 4), sg_idx(5, 4), &dmg, &back);
  ok(dmg == 1, "урон не меньше 1");
  st.terr[sg_idx(5, 4)] = SGT_PLAIN;
  sg_begin_turn(&st); /* ход стороны 0 (first = 0) */
  SgAct a = {'a', sg_idx(4, 4), sg_idx(5, 4), 0};
  ok(sg_apply(&st, &a) && st.u[sp].hp == 4 && st.u[sw].hp == 8, "удар и ответ");
  ok(!sg_apply(&st, &a), "второй удар за ход — отказ");
  SgAct mv = {'m', sg_idx(4, 4), sg_idx(3, 4), 0};
  ok(!sg_apply(&st, &mv), "после удара не ходят");
  /* лучник: с двух клеток мечник не отвечает, лучник отвечает */
  blank(&st);
  put(&st, SGU_ARCHER, 0, 4, 4, 10);
  put(&st, SGU_SWORD, 1, 6, 4, 10);
  int ar2 = put(&st, SGU_ARCHER, 1, 4, 6, 10);
  sg_forecast(&st, SGU_ARCHER, 10, sg_idx(4, 4), sg_idx(6, 4), &dmg, &back);
  ok(dmg == 5 && back == 0, "лучник издали: без ответа");
  sg_forecast(&st, SGU_ARCHER, 10, sg_idx(4, 4), sg_idx(4, 6), &dmg, &back);
  ok(dmg == 5 && back == 3, "лучник по лучнику: ответ 5 сил · 5 / 10 = 3");
  ok(!sg_in_range(&st, SGU_ARCHER, sg_idx(4, 4), sg_idx(7, 4)), "лучник на 3 клетки не бьёт");
  st.terr[sg_idx(4, 4)] = SGT_HILL;
  ok(sg_in_range(&st, SGU_ARCHER, sg_idx(4, 4), sg_idx(7, 4)), "с холма — на 3");
  (void)ar2;
  /* катапульта: вплотную не бьёт, не отвечает */
  blank(&st);
  int ca = put(&st, SGU_CATA, 0, 4, 4, 10);
  put(&st, SGU_HORSE, 1, 5, 4, 10);
  put(&st, SGU_HORSE, 1, 6, 4, 10);
  ok(!sg_can_attack(&st, ca, sg_idx(5, 4)) && sg_can_attack(&st, ca, sg_idx(6, 4)), "катапульта: не вплотную");
  sg_forecast(&st, SGU_HORSE, 10, sg_idx(5, 4), sg_idx(4, 4), &dmg, &back);
  ok(dmg == 9 && back == 0, "всадник по катапульте: 9, ответа нет");
  /* гибель: отряд снят, клетка свободна */
  blank(&st);
  put(&st, SGU_HORSE, 0, 4, 4, 10);
  int weak = put(&st, SGU_ARCHER, 1, 5, 4, 5);
  sg_begin_turn(&st);
  SgAct k = {'a', sg_idx(4, 4), sg_idx(5, 4), 0};
  ok(sg_apply(&st, &k) && !st.u[weak].alive && st.at[sg_idx(5, 4)] < 0, "отряд разбит");
  /* свой по своему, по пустой — отказ */
  blank(&st);
  put(&st, SGU_SWORD, 0, 4, 4, 10);
  put(&st, SGU_SWORD, 0, 5, 4, 10);
  sg_begin_turn(&st);
  SgAct f = {'a', sg_idx(4, 4), sg_idx(5, 4), 0};
  ok(!sg_apply(&st, &f), "по своему — отказ");
  f.b = sg_idx(3, 4);
  ok(!sg_apply(&st, &f), "по пустой клетке — отказ");
  f.b = st.castle[0];
  ok(!sg_apply(&st, &f), "по своему замку — отказ");
}

static void test_castle_buy_turns(void) {
  SgState st;
  blank(&st);
  int cb = st.castle[1], nb[6], k = sg_neighbors(cb, nb);
  ok(k == 6, "у замка шесть соседей");
  int sw = put(&st, SGU_SWORD, 0, sg_col(nb[0]), sg_row(nb[0]), 10);
  int far = -1;
  for (int i = 0; i < SG_CELLS && far < 0; i++)
    if (sg_cell_ok(i) && sg_dist(i, cb) == 2) far = i;
  put(&st, SGU_CATA, 0, sg_col(far), sg_row(far), 10);
  sg_begin_turn(&st);
  ok(st.gold[0] == 5, "доход без деревень — 5");
  SgAct a = {'a', nb[0], cb, 0};
  ok(sg_apply(&st, &a) && st.castleHp[1] == SG_CASTLE_HP - 3 && st.u[sw].hp == 8, "мечник по замку: 3, замок отвечает 2");
  SgAct c = {'a', far, cb, 0};
  ok(sg_apply(&st, &c) && st.castleHp[1] == SG_CASTLE_HP - 9, "катапульта по замку: 6, без ответа");
  st.castleHp[1] = 2;
  sg_end_turn(&st);
  sg_begin_turn(&st);
  ok(sg_side(&st) == 1, "ход второго");
  /* покупка: только у своего замка, хватает золота, место свободно */
  st.gold[1] = 25;
  SgAct b = {'b', 0, nb[0], SGU_HORSE};
  ok(!sg_apply(&st, &b), "на занятую клетку — отказ");
  b.b = nb[1];
  ok(sg_apply(&st, &b) && st.gold[1] == 9 && st.at[nb[1]] >= 0, "всадник куплен");
  int hu = st.at[nb[1]];
  SgAct hm = {'m', nb[1], nb[2], 0};
  ok(st.u[hu].done && !sg_apply(&st, &hm), "купленный в этот ход не ходит");
  b.b = nb[2];
  ok(!sg_apply(&st, &b), "золота не хватает — отказ");
  b.type = SGU_SWORD;
  b.b = sg_idx(6, 5);
  ok(!sg_apply(&st, &b), "не у замка — отказ");
  int n0[6];
  sg_neighbors(st.castle[0], n0);
  b.b = n0[0];
  ok(!sg_apply(&st, &b), "у чужого замка — отказ");
  sg_end_turn(&st);
  sg_begin_turn(&st);
  ok(sg_side(&st) == 0 && !st.u[sw].done && !st.u[sw].moved, "новый ход — отряды снова ходят");
  ok(sg_apply(&st, &a) && st.over == SG_CASTLE_FELL && st.winner == 0, "замок разрушен — победа");
  ok(!sg_apply(&st, &c), "после конца партии действий нет");
  /* не больше 10 отрядов */
  blank(&st);
  for (int i = 0; i < SG_SIDE_UNITS; i++) put(&st, SGU_SWORD, 0, 3 + i, 0, 10);
  sg_begin_turn(&st);
  st.gold[0] = 100;
  int m0[6];
  sg_neighbors(st.castle[0], m0);
  SgAct b2 = {'b', 0, m0[0], SGU_SWORD};
  ok(!sg_apply(&st, &b2), "одиннадцатый отряд — отказ");
  /* лечение на своей деревне, доход с деревень */
  blank(&st);
  int u = put(&st, SGU_SWORD, 0, 4, 4, 5), v = put(&st, SGU_SWORD, 0, 6, 4, 9), w = put(&st, SGU_SWORD, 0, 8, 4, 5);
  st.terr[sg_idx(4, 4)] = st.terr[sg_idx(6, 4)] = st.terr[sg_idx(8, 4)] = SGT_VILLAGE;
  st.owner[sg_idx(4, 4)] = st.owner[sg_idx(6, 4)] = 0;
  st.owner[sg_idx(8, 4)] = 1;
  sg_begin_turn(&st);
  ok(st.u[u].hp == 7 && st.u[v].hp == 10 && st.u[w].hp == 5, "лечение: +2 на своей деревне, не больше 10");
  ok(st.gold[0] == 9, "доход: 5 + 2 за деревню");
  /* конец по числу ходов: у кого целее замок, потом деревни, иначе ничья */
  blank(&st);
  st.turn = SG_TURNS + 1;
  st.castleHp[1] = 20;
  sg_begin_turn(&st);
  ok(st.over == SG_LIMIT && st.winner == 0, "ходы вышли — у кого целее замок");
  blank(&st);
  st.turn = SG_TURNS + 1;
  st.terr[sg_idx(4, 4)] = SGT_VILLAGE;
  st.owner[sg_idx(4, 4)] = 1;
  sg_begin_turn(&st);
  ok(st.over == SG_LIMIT && st.winner == 1, "замки равны — у кого больше деревень");
  blank(&st);
  st.turn = SG_TURNS + 1;
  sg_begin_turn(&st);
  ok(st.over == SG_LIMIT && st.winner == -1, "всё равно — ничья");
}

static void test_record(void) {
  SgAct a = {'m', sg_idx(0, 0), sg_idx(12, 10), 0}, b;
  char t[8];
  ok(sg_act_str(&a, t) == 5 && !strcmp(t, "maamk"), "запись хода");
  ok(sg_act_parse(t, &b) == 5 && b.kind == 'm' && b.a == a.a && b.b == a.b, "разбор хода");
  SgAct c = {'b', 0, sg_idx(2, 5), SGU_CATA};
  ok(sg_act_str(&c, t) == 4 && !strcmp(t, "bccf"), "запись покупки");
  ok(sg_act_parse(t, &b) == 4 && b.kind == 'b' && b.type == SGU_CATA && b.b == c.b, "разбор покупки");
  ok(!sg_act_parse("mmbaa", &b), "клетки нет (нечётный ряд, 13-й столбец) — отказ");
  ok(!sg_act_parse("xaaab", &b) && !sg_act_parse("maa", &b) && !sg_act_parse("bzaa", &b), "мусор — отказ");
  SgState st;
  sg_start(&st, 7, 0);
  sg_begin_turn(&st);
  SgState s0 = st;
  ok(sg_apply_line(&st, "-", NULL, 0) == 0 && !memcmp(&st, &s0, sizeof(st)), "ход без действий");
  ok(sg_apply_line(&st, "", NULL, 0) == 0, "пустая запись — без действий");
  ok(sg_apply_line(&st, "maaab.", NULL, 0) < 0, "точка в конце — отказ");
  st = s0;
  ok(sg_apply_line(&st, "maaab,maaab", NULL, 0) < 0, "не точка между действиями — отказ");
  /* ход компьютера записывается и разыгрывается из записи в ту же позицию */
  st = s0;
  char line[SG_LINE];
  sg_bot_turn(&st, line, SG_LINE);
  SgState again = s0;
  SgAct acts[64];
  int n = sg_apply_line(&again, line, acts, 64);
  ok(n > 0 && !memcmp(&st, &again, sizeof(st)), "запись хода разыгрывается в ту же позицию");
  ok(sg_seed(L"ivanov_PC1-1a2b") == sg_seed(L"ivanov_PC1-1a2b") && sg_seed(L"ivanov_PC1-1a2b") != sg_seed(L"ivanov_PC1-1a2c") &&
         sg_seed(L"") != 0,
     "зерно карты по номеру партии");
}

/* партия «компьютер против компьютера»: каждое действие — через запись и проверку правил */
static int selfplay(unsigned seed, int first, int *turns, int verbose) {
  SgState st;
  sg_start(&st, seed, first);
  for (;;) {
    sg_begin_turn(&st);
    if (st.over) break;
    SgState before = st, botSt = st;
    char line[SG_LINE];
    sg_bot_turn(&botSt, line, SG_LINE);
    int n = sg_apply_line(&st, line, NULL, 0);
    if (n < 0 || memcmp(&st, &botSt, sizeof(st))) {
      printf("НЕ ТАК: партия %u, ход %d: запись «%s» не прошла проверку\n", seed, before.turn, line);
      g_fail++;
      return -2;
    }
    if ((int)strlen(line) >= SG_LINE - 8) {
      printf("НЕ ТАК: партия %u, ход %d: запись хода не влезла\n", seed, before.turn);
      g_fail++;
    }
    if (verbose) {
      printf("ход %d (сторона %d): %s   золото %d/%d замки %d/%d\n", before.turn, sg_side(&before), line, st.gold[0],
             st.gold[1], st.castleHp[0], st.castleHp[1]);
      if (verbose > 1) dump(&st);
    }
    if (st.over) break;
    sg_end_turn(&st);
    if (st.turn > SG_TURNS + 2) {
      printf("НЕ ТАК: партия %u не кончается\n", seed);
      g_fail++;
      return -2;
    }
  }
  *turns = st.turn;
  g_n++;
  return st.over == SG_LIMIT ? (st.winner < 0 ? 2 : 3 + st.winner) : st.winner;
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "maps")) {
    int n = argc >= 3 ? atoi(argv[2]) : 6;
    for (unsigned seed = 1; seed <= (unsigned)n; seed++) {
      SgState st;
      sg_start(&st, seed, 0);
      printf("карта %u\n", seed);
      dump(&st);
    }
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "game")) {
    int turns = 0;
    int r = selfplay((unsigned)atoi(argv[2]), 0, &turns, argc >= 4 ? 2 : 1);
    printf("итог %d, ходов %d\n", r, turns);
    return 0;
  }
  test_geometry();
  test_maps();
  test_move();
  test_fight();
  test_castle_buy_turns();
  test_record();
  int win[2] = {0, 0}, firstWin = 0, limit = 0, draws = 0, total = 0, games = 400;
  long sumTurns = 0;
  for (unsigned seed = 1; seed <= (unsigned)games; seed++) {
    int turns = 0, first = (int)(seed & 1);
    int r = selfplay(seed * 7919u, first, &turns, 0);
    if (r < -1) continue;
    total++;
    sumTurns += turns;
    if (r == 2) draws++, limit++;
    else if (r >= 3) limit++, win[r - 3]++, firstWin += (r - 3) == first;
    else if (r >= 0) win[r]++, firstWin += r == first;
  }
  printf("партий %d: в среднем %ld ходов; до конца ходов дошло %d (ничьих %d); побед первого %d, стороны 0 — %d, стороны 1 — %d\n",
         total, total ? sumTurns / total : 0, limit, draws, firstWin, win[0], win[1]);
  ok(total == games, "все партии доиграны");
  ok(limit * 2 < games, "больше половины партий кончается разрушенным замком");
  printf("%s: проверок %d, не прошло %d\n", g_fail ? "ЕСТЬ ОШИБКИ" : "всё сходится", g_n, g_fail);
  return g_fail ? 1 : 0;
}
