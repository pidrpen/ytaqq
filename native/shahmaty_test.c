/* Проверка правил шахмат (shahmaty_rules.c) без Windows:
   gcc -O2 -Wall -Werror -o shahmaty_test shahmaty_test.c && ./shahmaty_test
   perft — сколько разных партий длиной N полуходов из позиции; числа —
   общеизвестные эталоны (chessprogramming.org/Perft_Results). Сойдутся они
   только если верно всё: рокировки, взятие на проходе, превращения, связки,
   шах через открытую линию. Ещё — запись ходов по-русски, мат, пат, ничьи. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "shahmaty_rules.c"

static int g_fail, g_n;

static void out8(const wchar_t *w) {
  for (; *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) {
      putchar(0xC0 | c >> 6);
      putchar(0x80 | (c & 63));
    } else {
      putchar(0xE0 | c >> 12);
      putchar(0x80 | ((c >> 6) & 63));
      putchar(0x80 | (c & 63));
    }
  }
}

static void ok(int cond, const char *what) {
  g_n++;
  if (!cond) {
    g_fail++;
    printf("НЕ ТАК: %s\n", what);
  }
}

/* позиция из FEN (только для проверки) */
static void fen(SxPos *p, const char *f) {
  memset(p, 0, sizeof(*p));
  int r = 7, c = 0;
  for (; *f && *f != ' '; f++) {
    if (*f == '/') {
      r--;
      c = 0;
    } else if (*f >= '1' && *f <= '8') c += *f - '0';
    else {
      const char *k = strchr("pnbrqk", *f >= 'a' ? *f : *f + 32);
      int v = (int)(k - "pnbrqk") + 1;
      p->sq[r * 8 + c++] = (signed char)(*f >= 'a' ? -v : v);
    }
  }
  f++;
  p->side = *f == 'w' ? 1 : -1;
  f += 2;
  for (; *f && *f != ' '; f++) {
    if (*f == 'K') p->castle |= SX_WK;
    if (*f == 'Q') p->castle |= SX_WQ;
    if (*f == 'k') p->castle |= SX_BK;
    if (*f == 'q') p->castle |= SX_BQ;
  }
  f++;
  p->ep = *f == '-' ? -1 : (signed char)((f[1] - '1') * 8 + (f[0] - 'a'));
  while (*f && *f != ' ') f++;
  p->half = *f ? atoi(f + 1) : 0;
}

static long long perft(const SxPos *p, int depth) {
  SxMove mv[SX_MAXMOVES];
  int n = sx_legal(p, mv, SX_MAXMOVES);
  if (depth == 1) return n;
  long long t = 0;
  for (int i = 0; i < n; i++) {
    SxPos q = *p;
    sx_apply(&q, &mv[i]);
    t += perft(&q, depth - 1);
  }
  return t;
}

static void check_perft(const char *name, const char *f, int depth, long long want) {
  SxPos p;
  fen(&p, f);
  long long got = perft(&p, depth);
  char t[200];
  snprintf(t, sizeof t, "perft %s глубина %d: %lld, а надо %lld", name, depth, got, want);
  ok(got == want, t);
}

/* сыграть ходы «e2e4 e7e5 …» с позиции p; FALSE — какой-то ход не по правилам */
static int play(SxPos *p, const char *moves) {
  char m[8];
  while (*moves) {
    while (*moves == ' ') moves++;
    if (!*moves) break;
    int k = 0;
    while (*moves && *moves != ' ' && k < 7) m[k++] = *moves++;
    m[k] = 0;
    SxMove mv[SX_MAXMOVES];
    int n = sx_legal(p, mv, SX_MAXMOVES), i = sx_find_uci(mv, n, m);
    if (i < 0) return 0;
    sx_apply(p, &mv[i]);
  }
  return 1;
}

/* запись хода uci из позиции f — как её видит игрок */
static void check_san(const char *f, const char *uci, const wchar_t *want) {
  SxPos p;
  fen(&p, f);
  SxMove mv[SX_MAXMOVES];
  int n = sx_legal(&p, mv, SX_MAXMOVES), i = sx_find_uci(mv, n, uci);
  wchar_t got[32] = L"(нет хода)";
  if (i >= 0) sx_san(&p, &mv[i], mv, n, got, 32);
  g_n++;
  if (wcscmp(got, want)) {
    g_fail++;
    printf("НЕ ТАК: запись %s — «", uci);
    out8(got);
    printf("», а надо «");
    out8(want);
    printf("»\n");
  }
}

static int legal_n(const SxPos *p) {
  SxMove mv[SX_MAXMOVES];
  return sx_legal(p, mv, SX_MAXMOVES);
}

int main(void) {
  const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  {
    SxPos a, b;
    sx_start(&a);
    fen(&b, start);
    ok(!memcmp(&a, &b, sizeof a), "начальная позиция");
  }
  check_perft("начальная", start, 1, 20);
  check_perft("начальная", start, 2, 400);
  check_perft("начальная", start, 3, 8902);
  check_perft("начальная", start, 4, 197281);
  check_perft("начальная", start, 5, 4865609);
  const char *kiwi = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  check_perft("kiwipete", kiwi, 1, 48);
  check_perft("kiwipete", kiwi, 2, 2039);
  check_perft("kiwipete", kiwi, 3, 97862);
  check_perft("kiwipete", kiwi, 4, 4085603);
  const char *p3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
  check_perft("позиция 3", p3, 4, 43238);
  check_perft("позиция 3", p3, 5, 674624);
  const char *p4 = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
  check_perft("позиция 4", p4, 3, 9467);
  check_perft("позиция 4", p4, 4, 422333);
  const char *p5 = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
  check_perft("позиция 5", p5, 3, 62379);
  check_perft("позиция 5", p5, 4, 2103487);
  const char *p6 = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";
  check_perft("позиция 6", p6, 3, 89890);
  check_perft("позиция 6", p6, 4, 3894594);

  /* мат и пат */
  {
    SxPos p;
    sx_start(&p);
    ok(play(&p, "f2f3 e7e5 g2g4 d8h4"), "детский мат: ходы");
    ok(legal_n(&p) == 0 && sx_in_check(&p, p.side), "детский мат: мат");
    fen(&p, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    ok(legal_n(&p) == 0 && !sx_in_check(&p, p.side), "пат");
  }
  /* рокировка: через битое поле нельзя, под шахом нельзя, ладья ушла — нельзя */
  {
    SxPos p;
    SxMove mv[SX_MAXMOVES];
    fen(&p, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    int n = sx_legal(&p, mv, SX_MAXMOVES);
    ok(sx_find_uci(mv, n, "e1g1") >= 0 && sx_find_uci(mv, n, "e1c1") >= 0, "рокировки есть");
    fen(&p, "r3k2r/8/8/8/8/8/5r2/R3K2R w KQkq - 0 1"); /* f2 бьёт f1 */
    n = sx_legal(&p, mv, SX_MAXMOVES);
    ok(sx_find_uci(mv, n, "e1g1") < 0 && sx_find_uci(mv, n, "e1c1") >= 0, "через битое поле — нельзя");
    fen(&p, "r3k2r/8/8/8/8/8/4r3/R3K2R w KQkq - 0 1"); /* шах */
    n = sx_legal(&p, mv, SX_MAXMOVES);
    ok(sx_find_uci(mv, n, "e1g1") < 0 && sx_find_uci(mv, n, "e1c1") < 0, "под шахом — нельзя");
    fen(&p, "r3k2r/8/8/8/8/8/1r6/R3K2R w KQkq - 0 1");
    n = sx_legal(&p, mv, SX_MAXMOVES);
    ok(sx_find_uci(mv, n, "e1c1") >= 0, "b1 под боем — длинная всё равно можно");
    sx_start(&p);
    ok(play(&p, "g1f3 g8f6 g2g3 g7g6 f1g2 f8g7 h1g1 h8g8 g1h1 g8h8"), "ладья сходила и вернулась");
    ok(!(p.castle & (SX_WK | SX_BK)) && (p.castle & (SX_WQ | SX_BQ)) == (SX_WQ | SX_BQ), "короткой больше нет");
    n = sx_legal(&p, mv, SX_MAXMOVES);
    ok(sx_find_uci(mv, n, "e1g1") < 0, "рокировка после хода ладьи — нельзя");
    fen(&p, "r3k2r/8/8/8/8/8/6n1/R3K2R b KQkq - 0 1");
    ok(play(&p, "g2h4 a1a8"), "ладья берёт ладью");
    ok(!(p.castle & SX_BQ) && !(p.castle & SX_WQ), "взятая ладья — рокировки с ней нет");
  }
  /* взятие на проходе: только сразу; снимает пешку рядом; связанная пешка не берёт */
  {
    SxPos p;
    SxMove mv[SX_MAXMOVES];
    sx_start(&p);
    ok(play(&p, "e2e4 a7a6 e4e5 d7d5"), "ходы до прохода");
    int n = sx_legal(&p, mv, SX_MAXMOVES), k = sx_find_uci(mv, n, "e5d6");
    ok(k >= 0 && (mv[k].flags & SXF_EP), "на проходе можно");
    ok(play(&p, "e5d6") && p.sq[4 * 8 + 3] == 0 && p.sq[5 * 8 + 3] == SX_P, "на проходе: пешка d5 снята");
    sx_start(&p);
    ok(play(&p, "e2e4 a7a6 e4e5 d7d5 a2a3 a6a5"), "ход спустя");
    n = sx_legal(&p, mv, SX_MAXMOVES);
    ok(sx_find_uci(mv, n, "e5d6") < 0, "на проходе — только сразу");
    fen(&p, "8/8/8/K2pP2r/8/8/8/7k w - d6 0 1"); /* после exd6 горизонталь откроется */
    n = sx_legal(&p, mv, SX_MAXMOVES);
    ok(sx_find_uci(mv, n, "e5d6") < 0, "на проходе со связкой — нельзя");
  }
  /* превращение */
  {
    SxPos p;
    SxMove mv[SX_MAXMOVES];
    fen(&p, "3r3k/4P3/8/8/8/8/8/K7 w - - 0 1");
    int n = sx_legal(&p, mv, SX_MAXMOVES);
    int prom = 0;
    for (int i = 0; i < n; i++) prom += mv[i].promo != 0;
    ok(prom == 8, "превращение: 4 фигуры прямо и 4 со взятием");
    ok(play(&p, "e7d8n") && p.sq[63 - 7 + 3] == SX_N, "превращение в коня со взятием");
    ok(sx_find_uci(mv, n, "e7e8") < 0, "без фигуры превращения хода нет");
  }
  /* запись по-русски */
  check_san(start, "g1f3", L"Кf3");
  check_san(start, "e2e4", L"e4");
  check_san(kiwi, "e1g1", L"0-0");
  check_san(kiwi, "e1c1", L"0-0-0");
  check_san(kiwi, "e5f7", L"Кxf7");
  check_san(kiwi, "d5e6", L"dxe6");
  check_san("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "b1d2", L"Кbd2");   /* и Кf3 мог бы — разные вертикали */
  check_san("6k1/8/8/8/8/8/4K3/R6R w - - 0 1", "a1d1", L"Лad1");     /* Лh1 тоже на d1 */
  check_san("7k/8/8/R7/8/8/8/R3K3 w - - 0 1", "a1a3", L"Л1a3");     /* на одной вертикали */
  check_san("3r3k/4P3/8/8/8/8/8/K7 w - - 0 1", "e7e8q", L"e8=Ф+");
  check_san("3r3k/4P3/8/8/8/8/8/K7 w - - 0 1", "e7d8q", L"exd8=Ф+");
  check_san("6k1/5ppp/8/8/8/8/8/K2R4 w - - 0 1", "d1d8", L"Лd8#");
  check_san("4k3/8/8/8/8/8/8/4K3 w - - 0 1", "e1f2", L"Крf2");
  /* мата не поставить */
  {
    SxPos p;
    fen(&p, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    ok(sx_dead(&p), "два короля — ничья");
    fen(&p, "4k3/8/8/8/8/8/8/4KN2 w - - 0 1");
    ok(sx_dead(&p), "король и конь — ничья");
    fen(&p, "4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1"); /* c1 и f8 — оба тёмные */
    ok(sx_dead(&p), "слоны одного цвета — ничья");
    fen(&p, "2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1"); /* c1 тёмное, c8 светлое */
    ok(!sx_dead(&p), "слоны разного цвета — играть");
    fen(&p, "4k3/8/8/8/8/8/8/2NNK3 w - - 0 1");
    ok(!sx_dead(&p), "два коня — играть (мат возможен с помощью)");
    fen(&p, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    ok(!sx_dead(&p), "пешка — играть");
  }
  /* повторение: поле прохода считается, только если взять на проходе можно */
  {
    SxPos a, b;
    SxMove ma[SX_MAXMOVES], mb[SX_MAXMOVES];
    sx_start(&a);
    play(&a, "e2e4");
    fen(&b, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    int na = sx_legal(&a, ma, SX_MAXMOVES), nb = sx_legal(&b, mb, SX_MAXMOVES);
    ok(sx_key(&a, ma, na) == sx_key(&b, mb, nb), "e3 без взятия на проходе — та же позиция");
    sx_start(&a);
    play(&a, "g1f3 g8f6 f3g1 f6g8");
    sx_start(&b);
    ok(sx_key(&a, ma, sx_legal(&a, ma, SX_MAXMOVES)) == sx_key(&b, mb, sx_legal(&b, mb, SX_MAXMOVES)),
       "кони вернулись — позиция повторилась");
    fen(&a, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    fen(&b, "4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1");
    na = sx_legal(&a, ma, SX_MAXMOVES);
    nb = sx_legal(&b, mb, SX_MAXMOVES);
    ok(sx_key(&a, ma, na) != sx_key(&b, mb, nb), "можно взять на проходе — позиция другая");
  }
  /* запись в файл партии и обратно — каждый ход узнаётся */
  {
    SxPos p;
    SxMove mv[SX_MAXMOVES];
    int bad = 0;
    const char *fs[] = {kiwi, p4, p5, "3r3k/4P3/8/8/8/8/8/K7 w - - 0 1"};
    for (int f = 0; f < 4; f++) {
      fen(&p, fs[f]);
      int n = sx_legal(&p, mv, SX_MAXMOVES);
      for (int i = 0; i < n; i++) {
        char t[8];
        sx_uci(&mv[i], t);
        bad += sx_find_uci(mv, n, t) != i;
      }
    }
    ok(!bad, "uci туда и обратно");
    sx_start(&p);
    ok(sx_material(&p, 1) == 39 && sx_material(&p, -1) == 39, "материал в начале");
  }
  /* 50 ходов */
  {
    SxPos p;
    sx_start(&p);
    play(&p, "g1f3");
    ok(p.half == 1, "ход конём — счётчик");
    play(&p, "e7e5");
    ok(p.half == 0, "ход пешкой — счётчик с нуля");
  }
  printf("шахматы: %d проверок, ошибок %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
