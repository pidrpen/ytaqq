/* ---- Морской бой по сети через общую папку (с 2026.10.22) -------------------

   Как шашки и нарды: двое, у кого в Настройках одна общая папка; кто в
   сети — тот же список. Связь — общая (game_net.c), свои файлы — в
   <папка>\CursorPad-Morskoy.

   Правила классические: поле 10×10, флот — четырёхпалубный, два
   трёхпалубных, три двухпалубных, четыре однопалубных; корабли не касаются
   друг друга даже углами. Попал — стреляешь ещё, промах — ход сопернику.
   Кто стреляет первым — по номеру партии (у обоих одинаково). Победил тот,
   кто первым потопил весь флот соперника.

   Честная расстановка. Свой флот в общую папку не кладётся — её видят оба.
   Перед боем каждый пишет только отпечаток расстановки (SHA-256 от
   случайной соли и поля), сама расстановка остаётся на своём компьютере
   (morskoy.txt рядом с заметками). На выстрел соперника программа отвечает
   сама — «мимо», «ранил», «убил». Когда партия кончилась, оба открывают
   расстановку, и программа соперника сверяет её с отпечатком и со всеми
   ответами: подменить флот посреди боя или отвечать неправду не выйдет.

   Строки своего файла партии, кроме общих:
     commit\t<отпечаток>       — флот расставлен
     s\t<N>\t<клетка>          — мой выстрел номер N (клетка 0…99 = ряд·10 + столбец)
     a\t<N>\t<0|1|2>           — мой ответ на выстрел соперника N: мимо, ранил, убил
     fleet\t<соль>\t<поле>     — расстановка, открытая после конца партии

   Вид: два моря — своё слева, соперника справа; корабли со стальным
   корпусом и башнями, попадание горит, промах — всплеск, потопленный —
   обгоревший корпус, клетки вокруг отмечаются сами. Расставить флот —
   мышью: взять корабль, повернуть правой кнопкой (или колесом), поставить;
   «Случайно» расставляет заново. */

#define WM_MORSKOY_POLL (WM_APP + 67) /* lParam — GnPoll*, освобождает получатель */
#define MB_TIMER 1
#define MB_N 10
#define MB_SHIPS 10
/* ID_MB_* — в cursorpad.c */

typedef struct {
  signed char r, c, len, horiz;
} MbShip;

static const int kMbLens[MB_SHIPS] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};

enum { MB_SETUP, MB_BATTLE };
/* что видно на поле: 0 — неизвестно, 1 — мимо, 2 — ранил, 3 — убит, 4 — пусто
   (вокруг потопленного, отмечено само) */
enum { MBS_NONE, MBS_MISS, MBS_HIT, MBS_SUNK, MBS_EMPTY };

typedef struct {
  /* мой флот и соль отпечатка — только здесь и в morskoy.txt */
  MbShip ships[MB_SHIPS];
  char salt[40];
  BOOL haveFleet;
  /* из файлов */
  int phase;
  BOOL oppReady, iReady;
  int shooter;     /* кто стреляет сейчас (сторона) */
  int shot;        /* номер выстрела на очереди */
  BOOL waitAnswer; /* выстрел сделан, ответа ещё нет */
  int pendingCell;
  signed char seen[2][100]; /* поле стороны глазами соперника */
  BOOL shotAt[2][100];      /* по этой клетке поля стороны стреляли */
  int sunk[2];
  int sunkLen[2][5];        /* потоплено кораблей такой длины */
  int winner;               /* −1 — нет или партия остановлена */
  BOOL resigned, cheat;
  wchar_t endWhy[200];
  /* мои выстрелы по порядку — сверить с расстановкой соперника */
  unsigned char myCell[100], myRes[100];
  int nMy;
  /* расстановка соперника, открытая в конце */
  char oppFleet[101];
  BOOL oppRevealed;
  int lastCell[2];          /* последний выстрел по полю стороны */
  int notifiedShot;
  /* расстановка мышью: взятый корабль */
  int pick;                 /* −1 — ничего не взято */
  MbShip pickWas;
  int pickDr, pickDc;       /* за какую палубу взяли */
  int hoverBoard, hoverCell;
} MbGame;

static MbGame g_mb = {.pick = -1, .hoverBoard = -1, .hoverCell = -1, .pendingCell = -1, .winner = -1};
static HWND g_mbWnd;
static float g_mbS = 1.0f;

static void mb_fresh(void);
static void mb_refresh_view(void);
static void mb_turn_status(void);
static void mb_extra(wchar_t *out, int cap);

static GnNet g_mbn = {
    .sub = L"CursorPad-Morskoy",
    .local = L"morskoy",
    .title = L"Морской бой",
    .invText = L"зовёт сыграть в морской бой",
    .moveKey = L"s",
    .msg = WM_MORSKOY_POLL,
    .balloon = 7,
    .wnd = &g_mbWnd,
    .sc = 1.0f,
    .idList = ID_MB_LIST,
    .idInvite = ID_MB_INVITE,
    .idAccept = ID_MB_ACCEPT,
    .idDecline = ID_MB_DECLINE,
    .idCancel = ID_MB_CANCEL,
    .fresh = mb_fresh,
    .view = mb_refresh_view,
    .turn = mb_turn_status,
    .extra = mb_extra,
};

#define mb_status(...) gn_status(&g_mbn, __VA_ARGS__)

static int MB_(int v) { return (int)(v * g_mbS + 0.5f); }

/* ---- флот ------------------------------------------------------------------- */

static int mb_cell(int r, int c) { return r * MB_N + c; }

static void mb_ship_cell(const MbShip *s, int i, int *r, int *c) {
  *r = s->r + (s->horiz ? 0 : i);
  *c = s->c + (s->horiz ? i : 0);
}

/* поле из кораблей: '1' — палуба */
static void mb_grid(const MbShip *s, int n, int skip, char *g) {
  memset(g, '0', 100);
  g[100] = 0;
  for (int k = 0; k < n; k++) {
    if (k == skip) continue;
    for (int i = 0; i < s[k].len; i++) {
      int r, c;
      mb_ship_cell(&s[k], i, &r, &c);
      if (r >= 0 && r < MB_N && c >= 0 && c < MB_N) g[mb_cell(r, c)] = '1';
    }
  }
}

/* корабль t встаёт: в поле и не касается остальных (кроме skip) даже углом */
static BOOL mb_fits(const MbShip *s, int n, int skip, MbShip t) {
  char g[101];
  mb_grid(s, n, skip, g);
  for (int i = 0; i < t.len; i++) {
    int r, c;
    mb_ship_cell(&t, i, &r, &c);
    if (r < 0 || r >= MB_N || c < 0 || c >= MB_N) return FALSE;
    for (int dr = -1; dr <= 1; dr++)
      for (int dc = -1; dc <= 1; dc++) {
        int rr = r + dr, cc = c + dc;
        if (rr >= 0 && rr < MB_N && cc >= 0 && cc < MB_N && g[mb_cell(rr, cc)] == '1') return FALSE;
      }
  }
  return TRUE;
}

static unsigned g_mbRnd;
static unsigned mb_rand(void) {
  if (!g_mbRnd) g_mbRnd = (unsigned)GetTickCount64() ^ (GetCurrentProcessId() * 2654435761u) ^ 0x9E3779B9u;
  g_mbRnd ^= g_mbRnd << 13;
  g_mbRnd ^= g_mbRnd >> 17;
  g_mbRnd ^= g_mbRnd << 5;
  return g_mbRnd;
}

/* расставить весь флот случайно */
static void mb_random_fleet(MbShip *s) {
  for (int attempt = 0; attempt < 200; attempt++) {
    BOOL ok = TRUE;
    for (int k = 0; k < MB_SHIPS && ok; k++) {
      ok = FALSE;
      for (int tries = 0; tries < 400 && !ok; tries++) {
        MbShip t;
        t.len = (signed char)kMbLens[k];
        t.horiz = (signed char)(mb_rand() & 1);
        t.r = (signed char)(mb_rand() % (unsigned)(t.horiz ? MB_N : MB_N - t.len + 1));
        t.c = (signed char)(mb_rand() % (unsigned)(t.horiz ? MB_N - t.len + 1 : MB_N));
        if (mb_fits(s, k, -1, t)) {
          s[k] = t;
          ok = TRUE;
        }
      }
    }
    if (ok) return;
  }
}

/* корабли из поля (связные палубы); FALSE — не по правилам: кривой корабль,
   касаются углом, не тот набор */
static BOOL mb_ships_from_grid(const char *g, MbShip *out) {
  BOOL used[100];
  memset(used, 0, sizeof(used));
  int n = 0, have[5] = {0};
  if (!g || strlen(g) != 100) return FALSE;
  for (int i = 0; i < 100; i++) {
    if (g[i] != '0' && g[i] != '1') return FALSE;
    if (g[i] != '1' || used[i]) continue;
    int r = i / MB_N, c = i % MB_N, len = 1;
    BOOL horiz = c + 1 < MB_N && g[i + 1] == '1';
    for (;;) {
      int rr = r + (horiz ? 0 : len), cc = c + (horiz ? len : 0);
      if (rr >= MB_N || cc >= MB_N || g[mb_cell(rr, cc)] != '1') break;
      len++;
    }
    if (len > 4 || n >= MB_SHIPS) return FALSE;
    MbShip s = {(signed char)r, (signed char)c, (signed char)len, (signed char)horiz};
    for (int k = 0; k < len; k++) {
      int rr, cc;
      mb_ship_cell(&s, k, &rr, &cc);
      used[mb_cell(rr, cc)] = TRUE;
    }
    out[n++] = s;
    have[len]++;
  }
  if (n != MB_SHIPS || have[4] != 1 || have[3] != 2 || have[2] != 3 || have[1] != 4) return FALSE;
  /* ни одна палуба не касается чужого корабля */
  for (int k = 0; k < n; k++)
    if (!mb_fits(out, n, k, out[k])) return FALSE;
  return TRUE;
}

static void mb_my_grid(char *g) { mb_grid(g_mb.ships, MB_SHIPS, -1, g); }

/* отпечаток: SHA-256 от «morskoy|соль|поле» */
static void mb_commit_of(const char *salt, const char *grid, char *out) {
  char buf[200];
  BYTE h[32];
  snprintf(buf, sizeof(buf), "morskoy|%s|%s", salt, grid);
  if (gn_sha(buf, h)) gn_hex(h, 32, out);
  else out[0] = 0;
}

/* правдивый ответ на выстрел по клетке cell поля g, когда до него уже
   стреляли по клеткам shot: 0 — мимо, 1 — ранил, 2 — убил */
static int mb_truth(const char *g, const BOOL *shot, int cell) {
  if (g[cell] != '1') return 0;
  int r = cell / MB_N, c = cell % MB_N;
  static const int dr[4] = {0, 0, 1, -1}, dc[4] = {1, -1, 0, 0};
  for (int d = 0; d < 4; d++)
    for (int k = 1; k < 4; k++) {
      int rr = r + dr[d] * k, cc = c + dc[d] * k;
      if (rr < 0 || rr >= MB_N || cc < 0 || cc >= MB_N || g[mb_cell(rr, cc)] != '1') break;
      if (!shot[mb_cell(rr, cc)]) return 1;
    }
  return 2;
}

/* «убил»: палубы корабля — связные попадания вокруг cell; клетки вокруг —
   пустые */
static int mb_mark_sunk(signed char *seen, int cell) {
  int st[100], n = 0, len = 0;
  BOOL in[100];
  memset(in, 0, sizeof(in));
  st[n++] = cell;
  in[cell] = TRUE;
  while (n) {
    int x = st[--n];
    seen[x] = MBS_SUNK;
    len++;
    int r = x / MB_N, c = x % MB_N;
    static const int dr[4] = {0, 0, 1, -1}, dc[4] = {1, -1, 0, 0};
    for (int d = 0; d < 4; d++) {
      int rr = r + dr[d], cc = c + dc[d];
      if (rr < 0 || rr >= MB_N || cc < 0 || cc >= MB_N) continue;
      int y = mb_cell(rr, cc);
      if (!in[y] && (seen[y] == MBS_HIT || seen[y] == MBS_SUNK)) {
        in[y] = TRUE;
        st[n++] = y;
      }
    }
  }
  for (int x = 0; x < 100; x++) {
    if (!in[x]) continue;
    int r = x / MB_N, c = x % MB_N;
    for (int a = -1; a <= 1; a++)
      for (int b = -1; b <= 1; b++) {
        int rr = r + a, cc = c + b;
        if (rr >= 0 && rr < MB_N && cc >= 0 && cc < MB_N && seen[mb_cell(rr, cc)] == MBS_NONE)
          seen[mb_cell(rr, cc)] = MBS_EMPTY;
      }
  }
  return len;
}

/* ---- ход партии по файлам ------------------------------------------------- */

static void mb_reveal_mine(void);

static void mb_over(int winner, const wchar_t *why) {
  g_mbn.mode = GN_OVER;
  g_mb.winner = winner;
  lstrcpynW(g_mb.endWhy, why ? why : L"", 200);
  mb_reveal_mine();
  gn_save_local(&g_mbn);
}

/* конец партии — открыть свою расстановку (соперник сверит) */
static void mb_reveal_mine(void) {
  if (!g_mb.haveFleet || !g_mb.iReady || nd_has_line(g_mbn.log.w, L"fleet")) return;
  wchar_t v[200];
  if (nd_field(g_mbn.log.w, L"fleet", v, 200)) return;
  char g[101];
  mb_my_grid(g);
  wchar_t ws[40], wg[101];
  MultiByteToWideChar(CP_UTF8, 0, g_mb.salt, -1, ws, 40);
  MultiByteToWideChar(CP_UTF8, 0, g, -1, wg, 101);
  sb_add(&g_mbn.log, L"fleet\t%s\t%s\n", ws, wg);
  gn_write_mine(&g_mbn);
}

/* расстановку соперника открыли — сверить с отпечатком и с его ответами */
static void mb_check_reveal(const wchar_t *theirs, const char *commit) {
  wchar_t v[200];
  if (g_mb.oppRevealed || !nd_field(theirs, L"fleet", v, 200)) return;
  char line[200];
  WideCharToMultiByte(CP_UTF8, 0, v, -1, line, 200, NULL, NULL);
  char *tab = strchr(line, '\t');
  if (!tab) return;
  *tab = 0;
  const char *salt = line, *grid = tab + 1;
  char chk[70];
  mb_commit_of(salt, grid, chk);
  MbShip tmp[MB_SHIPS];
  BOOL ok = !strcmp(chk, commit) && mb_ships_from_grid(grid, tmp);
  if (ok) {
    BOOL shot[100];
    memset(shot, 0, sizeof(shot));
    for (int i = 0; i < g_mb.nMy && ok; i++) {
      if (mb_truth(grid, shot, g_mb.myCell[i]) != g_mb.myRes[i]) ok = FALSE;
      shot[g_mb.myCell[i]] = TRUE;
    }
  }
  g_mb.oppRevealed = TRUE;
  lstrcpynA(g_mb.oppFleet, grid, 101);
  if (!ok) {
    g_mb.cheat = TRUE;
    g_mb.winner = -1;
    lstrcpynW(g_mb.endWhy, L"Расстановка соперника не сходится с отпечатком или с его ответами — итог не засчитан", 200);
    gn_status(&g_mbn, L"%s", g_mb.endWhy);
  }
}

/* Разыграть партию с начала по обоим файлам. На выстрелы соперника по
   своему полю программа отвечает сама — ответ дописывается в свой файл. */
static void mb_replay(const wchar_t *p0, const wchar_t *p1) {
  GnNet *n = &g_mbn;
  const wchar_t *theirs = gn_theirs(n, p0, p1);
  const wchar_t *file[2];
  file[n->me] = n->log.w;
  file[1 - n->me] = theirs;
  if (!gn_replay_head(n, theirs)) return;
  if (!gn_accepted(file[1])) {
    n->mode = GN_WAIT;
    gn_wait_status(n);
    return;
  }
  if (n->mode == GN_WAIT) n->mode = GN_PLAY;
  wchar_t wc[80];
  char commit[2][70];
  for (int s = 0; s < 2; s++) {
    commit[s][0] = 0;
    if (nd_field(file[s], L"commit", wc, 80)) WideCharToMultiByte(CP_UTF8, 0, wc, -1, commit[s], 70, NULL, NULL);
  }
  int me = n->me, op = 1 - me;
  g_mb.iReady = commit[me][0] != 0;
  g_mb.oppReady = commit[op][0] != 0;
  memset(g_mb.seen, 0, sizeof(g_mb.seen));
  memset(g_mb.shotAt, 0, sizeof(g_mb.shotAt));
  memset(g_mb.sunk, 0, sizeof(g_mb.sunk));
  memset(g_mb.sunkLen, 0, sizeof(g_mb.sunkLen));
  g_mb.lastCell[0] = g_mb.lastCell[1] = -1;
  g_mb.nMy = 0;
  g_mb.waitAnswer = FALSE;
  g_mb.pendingCell = -1;
  BOOL res0 = nd_has_line(file[0], L"resign"), res1 = nd_has_line(file[1], L"resign");
  if (!g_mb.iReady || !g_mb.oppReady) {
    g_mb.phase = MB_SETUP;
    if (res0 || res1) {
      g_mb.resigned = TRUE;
      mb_over(res0 ? 1 : 0, NULL);
    }
    return;
  }
  /* свой флот должен совпасть со своим отпечатком — иначе отвечать нечем */
  if (g_mb.haveFleet) {
    char g[101], chk[70];
    mb_my_grid(g);
    mb_commit_of(g_mb.salt, g, chk);
    if (strcmp(chk, commit[me])) g_mb.haveFleet = FALSE;
  }
  g_mb.phase = MB_BATTLE;
  char myGrid[101];
  mb_my_grid(myGrid);
  int shooter = gn_game_bit(n->game);
  BOOL wrote = FALSE;
  for (int k = 1;; k++) {
    g_mb.shooter = shooter;
    g_mb.shot = k;
    if (res0 || res1) {
      g_mb.resigned = TRUE;
      mb_over(res0 ? 1 : 0, NULL);
      break;
    }
    char v[40];
    if (!gn_num_line(file[shooter], L"s", k, v, 40)) break; /* выстрел ещё не сделан */
    int cell = atoi(v), def = 1 - shooter;
    if (!v[0] || cell < 0 || cell >= 100 || g_mb.shotAt[def][cell]) {
      wchar_t why[200];
      _snwprintf(why, 200, L"Выстрел %d не по правилам — партия остановлена", k);
      mb_over(-1, why);
      break;
    }
    if (!gn_num_line(file[def], L"a", k, v, 40)) {
      if (def != me) { /* ждём ответа соперника */
        g_mb.waitAnswer = TRUE;
        g_mb.pendingCell = cell;
        break;
      }
      if (!g_mb.haveFleet) {
        mb_status(L"Своя расстановка потерялась (нет morskoy.txt) — ответить нечем, партию придётся сдать");
        g_mb.waitAnswer = TRUE;
        g_mb.pendingCell = cell;
        break;
      }
      /* по мне выстрелили — отвечаю сам */
      int ans = mb_truth(myGrid, g_mb.shotAt[me], cell);
      sb_add(&n->log, L"a\t%d\t%d\n", k, ans);
      file[me] = n->log.w; /* строка могла переехать */
      wrote = TRUE;
      snprintf(v, sizeof(v), "%d", ans);
    }
    int res = atoi(v);
    if (res < 0 || res > 2) {
      wchar_t why[200];
      _snwprintf(why, 200, L"Ответ на выстрел %d не по правилам — партия остановлена", k);
      mb_over(-1, why);
      break;
    }
    g_mb.shotAt[def][cell] = TRUE;
    g_mb.seen[def][cell] = res == 0 ? MBS_MISS : MBS_HIT;
    g_mb.lastCell[def] = cell;
    if (shooter == me && g_mb.nMy < 100) {
      g_mb.myCell[g_mb.nMy] = (unsigned char)cell;
      g_mb.myRes[g_mb.nMy++] = (unsigned char)res;
    }
    if (res == 2) {
      int len = mb_mark_sunk(g_mb.seen[def], cell);
      g_mb.sunk[def]++;
      if (len >= 1 && len <= 4) g_mb.sunkLen[def][len]++;
      if (g_mb.sunk[def] >= MB_SHIPS) {
        g_mb.shooter = shooter;
        mb_over(shooter, NULL);
        break;
      }
    }
    if (res == 0) shooter = def;
  }
  wchar_t fv[8];
  /* последний ответ уже ушёл вместе с открытым флотом (mb_over) — иначе записать */
  if (wrote && (n->mode != GN_OVER || !nd_field(n->log.w, L"fleet", fv, 8))) gn_write_mine(n);
  if (n->mode == GN_OVER && g_mb.oppReady) mb_check_reveal(theirs, commit[op]);
}

/* партия кончилась раньше, чем соперник открыл флот: его файл ещё читаем —
   открыл, сверяем и показываем его корабли */
static void mb_over_poll(void) {
  GnNet *n = &g_mbn;
  GnPoll *pl = n->poll;
  if (n->mode != GN_OVER || g_mb.oppRevealed || !g_mb.oppReady || !pl || !n->game[0] || wcscmp(pl->game, n->game))
    return;
  const wchar_t *theirs = gn_theirs(n, pl->p0, pl->p1);
  wchar_t wc[80];
  char commit[70] = "";
  if (nd_field(theirs, L"commit", wc, 80)) WideCharToMultiByte(CP_UTF8, 0, wc, -1, commit, 70, NULL, NULL);
  if (!commit[0]) return;
  mb_check_reveal(theirs, commit);
  if (!g_mb.oppRevealed) return;
  if (g_mb.cheat && !gn_in_front(n)) gn_balloon(n, g_mb.endWhy);
  mb_refresh_view();
}

static BOOL mb_my_shot(void) {
  return g_mbn.mode == GN_PLAY && g_mb.phase == MB_BATTLE && g_mb.shooter == g_mbn.me && !g_mb.waitAnswer;
}


static void mb_turn_status(void) {
  GnNet *n = &g_mbn;
  if (n->mode != GN_PLAY) return;
  if (g_mb.phase == MB_SETUP) {
    if (!g_mb.iReady)
      mb_status(g_mb.pick >= 0 ? L"Поставьте корабль: щелчок — поставить, правая кнопка или колесо — повернуть, Esc — вернуть"
                : g_mb.oppReady ? L"%s: флот уже расставлен. Расставьте свой и нажмите «В бой!»"
                                : L"Расставьте флот: возьмите корабль мышью или «Случайно», потом «В бой!»",
                n->oppName);
    else mb_status(L"Флот расставлен — ждём, пока %s расставит свой", n->oppName);
    return;
  }
  if (mb_my_shot()) mb_status(L"Ваш выстрел — щёлкните по полю соперника");
  else if (g_mb.shooter == n->me) mb_status(L"Выстрел ушёл — ждём, что ответит %s…", n->oppName);
  else if (!gn_opp_online(n)) mb_status(L"Стреляет %s — сейчас не в сети: выстрел придёт, когда CursorPad у соперника запустится", n->oppName);
  else mb_status(L"Стреляет %s…", n->oppName);
}

/* ---- вспышки и всплески ----------------------------------------------------- */

enum { MBFX_SPLASH, MBFX_BOOM, MBFX_SUNK };
typedef struct {
  int board, cell, kind;
  ULONGLONG t0;
} MbFx;
#define MB_MAXFX 16
static MbFx g_mbFx[MB_MAXFX];
static int g_mbNFx;

static int mb_fx_ms(int kind) { return kind == MBFX_SPLASH ? 650 : kind == MBFX_BOOM ? 700 : 900; }

static void mb_fx_add(int board, int cell, int kind) {
  if (!g_mbWnd || !IsWindowVisible(g_mbWnd) || IsIconic(g_mbWnd)) return;
  if (g_mbNFx >= MB_MAXFX) memmove(g_mbFx, g_mbFx + 1, sizeof(MbFx) * --g_mbNFx);
  MbFx f = {board, cell, kind, GetTickCount64()};
  g_mbFx[g_mbNFx++] = f;
  SetTimer(g_mbWnd, MB_TIMER, 16, NULL);
}

/* что поменялось на полях после разбора — показать вспышками; board 0 —
   своё поле (слева), 1 — соперника */
static void mb_fx_diff(signed char before[2][100]) {
  for (int side = 0; side < 2; side++) {
    int board = side == g_mbn.me ? 0 : 1;
    for (int c = 0; c < 100; c++) {
      int a = before[side][c], b = g_mb.seen[side][c];
      if (a == b) continue;
      if (a == MBS_NONE && b == MBS_MISS) mb_fx_add(board, c, MBFX_SPLASH);
      else if ((a == MBS_NONE || a == MBS_EMPTY) && (b == MBS_HIT || b == MBS_SUNK)) mb_fx_add(board, c, MBFX_BOOM);
      if (b == MBS_SUNK && a != MBS_SUNK) mb_fx_add(board, c, MBFX_SUNK);
    }
  }
}

/* ---- окно: разбор и действия -------------------------------------------------- */

static void mb_after_replay(int prevShot, int prevShooter, int prevMode) {
  GnNet *n = &g_mbn;
  if (prevMode == GN_WAIT && n->mode == GN_PLAY) gn_save_local(n); /* приняли — после перезапуска сразу игра */
  if (n->mode == GN_PLAY) {
    mb_turn_status();
    BOOL turnNow = mb_my_shot() && (g_mb.shot != prevShot || prevShooter != n->me || prevMode != GN_PLAY);
    if (turnNow && g_mb.notifiedShot != g_mb.shot && !gn_in_front(n)) {
      g_mb.notifiedShot = g_mb.shot;
      wchar_t t[160];
      if (g_mb.shot == 1) _snwprintf(t, 160, L"%s: флоты расставлены, бой начался — ваш выстрел первый", n->oppName);
      else _snwprintf(t, 160, L"%s: мимо — ваш выстрел", n->oppName);
      gn_balloon(n, t);
    }
    if (g_mb.phase == MB_SETUP && g_mb.oppReady && !g_mb.iReady && g_mb.notifiedShot != -1 && !gn_in_front(n)) {
      g_mb.notifiedShot = -1;
      wchar_t t[160];
      _snwprintf(t, 160, L"%s: флот расставлен — ваш черёд", n->oppName);
      gn_balloon(n, t);
    }
  }
  if (n->mode == GN_OVER && prevMode != GN_OVER) {
    wchar_t t[240];
    if (g_mb.winner == n->me)
      _snwprintf(t, 240, L"Победа! %s%s", n->oppName, g_mb.resigned ? L" сдаётся" : L": весь флот на дне");
    else if (g_mb.winner >= 0)
      _snwprintf(t, 240, L"Поражение: %s · соперник: %s", g_mb.resigned ? L"вы сдались" : L"ваш флот потоплен", n->oppName);
    else lstrcpynW(t, g_mb.endWhy, 240);
    mb_status(L"%s", t);
    if (!gn_in_front(n)) gn_balloon(n, t);
  }
  mb_refresh_view();
}

static void mb_replay_now(void) {
  int prevShot = g_mb.shot, prevShooter = g_mb.shooter, prevMode = g_mbn.mode;
  signed char before[2][100];
  memcpy(before, g_mb.seen, sizeof(before));
  GnPoll *pl = g_mbn.poll;
  mb_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  if (prevMode == GN_PLAY) mb_fx_diff(before);
  mb_after_replay(prevShot, prevShooter, prevMode);
}

static void mb_on_poll(GnPoll *pl) {
  int prevShot = g_mb.shot, prevShooter = g_mb.shooter, prevMode = g_mbn.mode;
  signed char before[2][100];
  memcpy(before, g_mb.seen, sizeof(before));
  if (!gn_on_poll(&g_mbn, pl)) {
    mb_over_poll();
    return;
  }
  mb_replay(pl->p0, pl->p1);
  if (prevMode == GN_PLAY) mb_fx_diff(before);
  mb_after_replay(prevShot, prevShooter, prevMode);
}

/* новая партия: свой флот — случайно, его можно переставить до «В бой!» */
static void mb_fresh(void) {
  mb_random_fleet(g_mb.ships);
  g_mb.haveFleet = TRUE;
  gn_secret(g_mb.salt);
  g_mb.phase = MB_SETUP;
  g_mb.iReady = g_mb.oppReady = FALSE;
  g_mb.pick = -1;
  g_mb.winner = -1;
  g_mb.resigned = g_mb.cheat = g_mb.oppRevealed = FALSE;
  g_mb.endWhy[0] = 0;
  g_mb.shot = 0;
  g_mb.shooter = 0;
  g_mb.waitAnswer = FALSE;
  g_mb.notifiedShot = 0;
  memset(g_mb.seen, 0, sizeof(g_mb.seen));
  memset(g_mb.shotAt, 0, sizeof(g_mb.shotAt));
  memset(g_mb.sunk, 0, sizeof(g_mb.sunk));
  memset(g_mb.sunkLen, 0, sizeof(g_mb.sunkLen));
  g_mb.nMy = 0;
  g_mbNFx = 0;
}

/* свой флот и соль — в morskoy.txt рядом с заметками: в общую папку не идут */
static void mb_extra(wchar_t *out, int cap) {
  out[0] = 0;
  if (!g_mb.haveFleet) return;
  char g[101];
  mb_my_grid(g);
  wchar_t ws[40], wg[101];
  MultiByteToWideChar(CP_UTF8, 0, g_mb.salt, -1, ws, 40);
  MultiByteToWideChar(CP_UTF8, 0, g, -1, wg, 101);
  _snwprintf(out, cap, L"fleet\t%s\nsalt\t%s\n", wg, ws);
  out[cap - 1] = 0;
}

static void mb_shuffle(void) {
  if (g_mbn.mode != GN_PLAY || g_mb.phase != MB_SETUP || g_mb.iReady) return;
  mb_random_fleet(g_mb.ships);
  g_mb.pick = -1;
  mb_turn_status();
  mb_refresh_view();
}

/* «В бой!»: отпечаток расстановки — в файл, сама расстановка — у себя */
static void mb_ready(void) {
  if (g_mbn.mode != GN_PLAY || g_mb.phase != MB_SETUP || g_mb.iReady || g_mb.pick >= 0) return;
  char g[101], commit[70];
  mb_my_grid(g);
  MbShip chk[MB_SHIPS];
  if (!mb_ships_from_grid(g, chk)) {
    mb_status(L"Корабли стоят не по правилам — «Случайно» расставит заново");
    mb_refresh_view();
    return;
  }
  gn_secret(g_mb.salt);
  mb_commit_of(g_mb.salt, g, commit);
  if (!commit[0]) {
    mb_status(L"Не удалось посчитать отпечаток расстановки");
    mb_refresh_view();
    return;
  }
  g_mb.haveFleet = TRUE;
  gn_save_local(&g_mbn); /* сперва у себя: без этого на выстрелы ответить нечем */
  wchar_t w[70];
  MultiByteToWideChar(CP_UTF8, 0, commit, -1, w, 70);
  sb_add(&g_mbn.log, L"commit\t%s\n", w);
  if (!gn_write_mine(&g_mbn)) mb_status(L"Не удалось записать в общую папку — повторю сам");
  mb_replay_now();
}

static void mb_fire(int cell) {
  if (!mb_my_shot() || cell < 0 || cell >= 100) return;
  int op = 1 - g_mbn.me;
  if (g_mb.shotAt[op][cell] || g_mb.seen[op][cell] == MBS_EMPTY) return;
  sb_add(&g_mbn.log, L"s\t%d\t%d\n", g_mb.shot, cell);
  if (!gn_write_mine(&g_mbn)) mb_status(L"Не удалось записать выстрел в общую папку — повторю сам");
  mb_replay_now();
}

static void mb_resign(void) {
  if (!gn_resign(&g_mbn)) return;
  mb_replay_now();
}

/* ---- поле на экране ----------------------------------------------------------- */

#define MB_TOP 46
#define MB_CELL 30
#define MB_LAB 22
#define MB_GAP 34
#define MB_LEFT 14

static int mb_q(void) { return MB_(MB_CELL); }
/* левый верхний угол поля: board 0 — своё, 1 — соперника */
static int mb_bx(int board) {
  int x = MB_(MB_LEFT) + MB_(MB_LAB);
  return board ? x + mb_q() * MB_N + MB_(MB_GAP) + MB_(MB_LAB) : x;
}
static int mb_by(void) { return MB_(MB_TOP) + MB_(26) + MB_(MB_LAB); }

static void mb_cell_xy(int board, int cell, int *x, int *y) {
  *x = mb_bx(board) + (cell % MB_N) * mb_q();
  *y = mb_by() + (cell / MB_N) * mb_q();
}

/* клетка под мышью; *board — какое поле */
static int mb_hit(int x, int y, int *board) {
  int q = mb_q(), by = mb_by();
  for (int b = 0; b < 2; b++) {
    int bx = mb_bx(b);
    if (x >= bx && y >= by && x < bx + q * MB_N && y < by + q * MB_N) {
      *board = b;
      return mb_cell((y - by) / q, (x - bx) / q);
    }
  }
  *board = -1;
  return -1;
}

/* какой мой корабль на клетке; −1 — никакой */
static int mb_ship_at(int cell) {
  int r = cell / MB_N, c = cell % MB_N;
  for (int k = 0; k < MB_SHIPS; k++) {
    if (k == g_mb.pick) continue;
    const MbShip *s = &g_mb.ships[k];
    for (int i = 0; i < s->len; i++) {
      int rr, cc;
      mb_ship_cell(s, i, &rr, &cc);
      if (rr == r && cc == c) return k;
    }
  }
  return -1;
}

/* где встанет взятый корабль, если мышь на клетке cell */
static MbShip mb_pick_at(int cell) {
  MbShip t = g_mb.ships[g_mb.pick];
  int r = cell / MB_N, c = cell % MB_N;
  int along = t.horiz ? g_mb.pickDc : g_mb.pickDr;
  if (along >= t.len) along = t.len - 1;
  t.r = (signed char)(r - (t.horiz ? 0 : along));
  t.c = (signed char)(c - (t.horiz ? along : 0));
  return t;
}

/* ---- спрайты ------------------------------------------------------------------ */

typedef struct {
  int q;
  GnSprite sea;            /* вода с сеткой — непрозрачно */
  GnSprite ship[5][2][2];  /* [длина][по горизонтали][обгорел] */
  GnSprite miss, empty, fire, glow, cross, aim, hover;
} MbArt;

static MbArt g_mbArt;
static HFONT g_mbBig;

/* корпус корабля длиной len клеток: корма скруглена, нос заострён;
   палуба светлее, по башне на палубу; burnt — обгоревший */
static void mb_render_ship(GnSprite *s, int q, int len, BOOL horiz, BOOL burnt) {
  int L = q * len, W = q;
  if (!gn_sprite_alloc(s, horiz ? L : W, horiz ? W : L)) return;
  float x0 = q * 0.12f, x1 = L - q * 0.06f, cy = W / 2.0f, hw = W * 0.34f;
  float taper = q * (len == 1 ? 0.45f : 0.75f);
  for (int py = 0; py < s->h; py++)
    for (int px = 0; px < s->w; px++) {
      float ar = 0, ag = 0, ab = 0, cov = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          float fx = px + (sx + 0.5f) / 4, fy = py + (sy + 0.5f) / 4;
          float x = horiz ? fx : fy, y = horiz ? fy : W - fx; /* вертикальный — нос вниз */
          if (!horiz) x = fy, y = fx;
          /* полуширина по длине: корма — полукруг, нос сужается */
          float w;
          if (x < x0 || x > x1) continue;
          if (x < x0 + hw) {
            float d = x0 + hw - x;
            w = sqrtf(hw * hw - d * d > 0 ? hw * hw - d * d : 0);
          } else if (x > x1 - taper) {
            float t = (x1 - x) / taper;
            w = hw * (0.18f + 0.82f * sqrtf(t));
          } else w = hw;
          float dy = y - cy;
          if (fabsf(dy) > w) continue;
          float v = dy / (w + 0.01f);
          float r, g, b;
          float lit = 1.12f - 0.30f * (v * 0.5f + 0.5f);
          if (fabsf(v) < 0.58f) { /* палуба */
            r = 178 * lit, g = 186 * lit, b = 196 * lit;
            /* доски вдоль */
            if (((int)(y * 4 / q * 2)) % 2 == 0) r *= 0.97f, g *= 0.97f, b *= 0.97f;
          } else {
            r = 120 * lit, g = 130 * lit, b = 144 * lit;
          }
          if (fabsf(v) > 0.86f) r *= 0.6f, g *= 0.62f, b *= 0.66f; /* борт */
          /* башни: по одной на палубу, на однопалубном — рубка */
          for (int k = 0; k < len; k++) {
            float tx = q * (k + 0.5f) + (k == len - 1 && len > 1 ? -q * 0.12f : 0), ty = cy;
            float dx = x - tx, dd = sqrtf(dx * dx + (y - ty) * (y - ty));
            float tr = q * (len == 1 ? 0.16f : 0.14f);
            if (dd < tr) {
              float l2 = 1.1f - 0.4f * ((dx + (y - ty)) / (2 * tr) + 0.5f);
              r = 92 * l2, g = 100 * l2, b = 112 * l2;
              if (dd > tr - 1.2f) r *= 0.7f, g *= 0.7f, b *= 0.7f;
            } else if (len > 1 && fabsf(y - ty) < q * 0.035f && x > tx && x < tx + q * 0.36f) {
              r = 70, g = 76, b = 86; /* ствол к носу */
            }
          }
          if (burnt) {
            float n = gn_noise(x * 0.35f, y * 0.35f, 17u);
            float k = 0.42f + 0.25f * n;
            r = r * k + 30 * (1 - k) * 0.4f, g = g * k * 0.82f, b = b * k * 0.72f;
            if (n > 0.78f) r = 150, g = 52, b = 24; /* тлеет */
          }
          ar += r, ag += g, ab += b, cov += 1;
        }
      if (cov <= 0) continue;
      s->px[(size_t)py * s->w + px] = gn_pm(ar / cov, ag / cov, ab / cov, cov / 16.0f);
    }
}

/* море: глубокая синева, по ней волны; сетка — тонкими светлыми линиями */
static void mb_render_sea(GnSprite *s, int q) {
  int W = q * MB_N;
  if (!gn_sprite_alloc(s, W, W)) return;
  float sc = q / 30.0f;
  for (int y = 0; y < W; y++)
    for (int x = 0; x < W; x++) {
      float t = (float)y / W;
      float r = 40 - 18 * t, g = 108 - 34 * t, b = 168 - 40 * t;
      float wave = sinf((x / sc) * 0.21f + sinf((y / sc) * 0.07f) * 2.2f + (y / sc) * 0.33f) * 0.5f + 0.5f;
      float n = gn_noise(x / (18 * sc), y / (6 * sc), 5u);
      float k = 0.92f + wave * 0.06f + n * 0.07f;
      r *= k, g *= k, b *= k;
      if (wave > 0.94f && n > 0.55f) r += 26, g += 30, b += 30; /* гребни */
      if (x % q == 0 || y % q == 0) r += (255 - r) * 0.16f, g += (255 - g) * 0.16f, b += (255 - b) * 0.16f;
      s->px[(size_t)y * W + x] = gn_pm(r, g, b, 1.0f);
    }
}

/* огонь попадания: язык пламени — капля с жёлтой серединой */
static void mb_render_fire(GnSprite *s, int q) {
  if (!gn_sprite_alloc(s, q, q)) return;
  float c = q / 2.0f;
  for (int y = 0; y < q; y++)
    for (int x = 0; x < q; x++) {
      float ar = 0, ag = 0, ab = 0, cov = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          float u = (x + (sx + 0.5f) / 4 - c) / q, v = (y + (sy + 0.5f) / 4 - c) / q;
          /* капля: круг внизу, острие вверх */
          float cyb = 0.10f, rb = 0.22f;
          float du = u, dv = v - cyb, d = sqrtf(du * du + dv * dv);
          float in = 0;
          if (d < rb) in = 1 - d / rb;
          else if (v < cyb && v > -0.36f) {
            float k = (v + 0.36f) / (cyb + 0.36f), w = rb * sqrtf(k);
            float wob = 0.03f * sinf(v * 40);
            if (fabsf(u - wob) < w) in = (1 - fabsf(u - wob) / w) * k;
          }
          if (in <= 0) continue;
          float r = 255, g = 90 + 150 * in, b = 30 + 90 * in * in;
          ar += r, ag += g, ab += b, cov += 1;
        }
      if (cov <= 0) continue;
      s->px[(size_t)y * q + x] = gn_pm(ar / cov, ag / cov, ab / cov, cov / 16.0f);
    }
}

/* крест на палубе потопленного */
static void mb_render_cross(GnSprite *s, int q) {
  if (!gn_sprite_alloc(s, q, q)) return;
  float a0 = q * 0.26f, a1 = q * 0.74f, w = q * 0.055f;
  for (int y = 0; y < q; y++)
    for (int x = 0; x < q; x++) {
      float cov = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
          if (px < a0 - w || px > a1 + w || py < a0 - w || py > a1 + w) continue;
          float d1 = fabsf((px - a0) - (py - a0)) * 0.7071f, d2 = fabsf((px - a0) + (py - a1)) * 0.7071f;
          if (d1 < w || d2 < w) cov += 1;
        }
      if (cov > 0) s->px[(size_t)y * q + x] = gn_pm(232, 56, 40, cov / 16.0f);
    }
}

/* прицел: кольцо и четыре риски */
static void mb_render_aim(GnSprite *s, int q, COLORREF col) {
  if (!gn_sprite_alloc(s, q, q)) return;
  float c = q / 2.0f, R = q * 0.30f, w = q * 0.05f;
  for (int y = 0; y < q; y++)
    for (int x = 0; x < q; x++) {
      float cov = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          float px = x + (sx + 0.5f) / 4 - c, py = y + (sy + 0.5f) / 4 - c, d = sqrtf(px * px + py * py);
          BOOL ring = fabsf(d - R) < w;
          BOOL tick = (fabsf(px) < w && fabsf(py) > R - q * 0.12f && fabsf(py) < R + q * 0.14f) ||
                      (fabsf(py) < w && fabsf(px) > R - q * 0.12f && fabsf(px) < R + q * 0.14f);
          if (ring || tick) cov += 1;
        }
      if (cov > 0) s->px[(size_t)y * q + x] = gn_pm(GetRValue(col), GetGValue(col), GetBValue(col), cov / 16.0f);
    }
}

static void mb_art_build(void) {
  int q = mb_q();
  if (g_mbArt.q == q && g_mbArt.sea.px) return;
  g_mbArt.q = q;
  mb_render_sea(&g_mbArt.sea, q);
  for (int len = 1; len <= 4; len++)
    for (int h = 0; h < 2; h++)
      for (int b = 0; b < 2; b++) mb_render_ship(&g_mbArt.ship[len][h][b], q, len, h, b);
  gn_make_ring(&g_mbArt.miss, q * 0.11f, q * 0.11f, q * 0.08f, RGB(226, 238, 250), 0.95f);
  gn_make_dot(&g_mbArt.empty, q * 0.075f, RGB(206, 224, 242), 0.75f);
  mb_render_fire(&g_mbArt.fire, q);
  gn_make_glow(&g_mbArt.glow, (int)(q * 0.48f), RGB(255, 120, 30), 0.55f, 0.15f);
  mb_render_cross(&g_mbArt.cross, q);
  mb_render_aim(&g_mbArt.aim, q, RGB(255, 84, 64));
  mb_render_aim(&g_mbArt.hover, q, RGB(255, 236, 160));
}

static void mb_blit_cell(UiCanvas *c, const GnSprite *s, int board, int cell, int alpha) {
  int x, y, q = mb_q();
  mb_cell_xy(board, cell, &x, &y);
  gn_blit(c, s, x + (q - s->w) / 2, y + (q - s->h) / 2, alpha);
}

static void mb_draw_ship(UiCanvas *c, int board, const MbShip *s, BOOL burnt, int alpha) {
  if (s->len < 1 || s->len > 4) return;
  int x, y;
  mb_cell_xy(board, mb_cell(s->r, s->c), &x, &y);
  gn_blit(c, &g_mbArt.ship[s->len][s->horiz ? 1 : 0][burnt ? 1 : 0], x, y, alpha);
}

/* потопленные корабли поля стороны — по отмеченным клеткам «убит» */
static void mb_draw_sunk_ships(UiCanvas *c, int board, int side) {
  BOOL done[100];
  memset(done, 0, sizeof(done));
  for (int i = 0; i < 100; i++) {
    if (g_mb.seen[side][i] != MBS_SUNK || done[i]) continue;
    int r = i / MB_N, col = i % MB_N, len = 1;
    BOOL horiz = col + 1 < MB_N && g_mb.seen[side][i + 1] == MBS_SUNK;
    while (len < 4) {
      int rr = r + (horiz ? 0 : len), cc = col + (horiz ? len : 0);
      if (rr >= MB_N || cc >= MB_N || g_mb.seen[side][mb_cell(rr, cc)] != MBS_SUNK) break;
      len++;
    }
    MbShip s = {(signed char)r, (signed char)col, (signed char)len, (signed char)horiz};
    for (int k = 0; k < len; k++) {
      int rr, cc;
      mb_ship_cell(&s, k, &rr, &cc);
      done[mb_cell(rr, cc)] = TRUE;
    }
    mb_draw_ship(c, board, &s, TRUE, 255);
  }
}

/* отметки выстрелов поля стороны: промахи, пустые, огонь, кресты */
static void mb_draw_marks(UiCanvas *c, int board, int side, ULONGLONG now) {
  for (int i = 0; i < 100; i++) {
    int v = g_mb.seen[side][i];
    if (v == MBS_MISS) mb_blit_cell(c, &g_mbArt.miss, board, i, 255);
    else if (v == MBS_EMPTY) mb_blit_cell(c, &g_mbArt.empty, board, i, 255);
    else if (v == MBS_HIT) {
      /* огонь чуть дышит — по клетке свой такт */
      int fl = (int)(205 + 50 * sinf((float)((now + i * 137) % 900) / 900.0f * 6.2832f));
      mb_blit_cell(c, &g_mbArt.glow, board, i, fl);
      mb_blit_cell(c, &g_mbArt.fire, board, i, 255);
    } else if (v == MBS_SUNK) mb_blit_cell(c, &g_mbArt.cross, board, i, 235);
  }
}

/* всплески и вспышки поверх */
static void mb_draw_fx(UiCanvas *c, ULONGLONG now) {
  int q = mb_q();
  for (int k = 0; k < g_mbNFx; k++) {
    const MbFx *f = &g_mbFx[k];
    float t = (float)(now - f->t0) / mb_fx_ms(f->kind);
    if (t < 0 || t >= 1) continue;
    int x, y;
    mb_cell_xy(f->board, f->cell, &x, &y);
    float cx = x + q / 2.0f, cy = y + q / 2.0f;
    UiShape s;
    memset(&s, 0, sizeof(s));
    s.r = -1;
    if (f->kind == MBFX_SPLASH) {
      for (int ring = 0; ring < 2; ring++) {
        float tt = t - ring * 0.22f;
        if (tt <= 0) continue;
        float rr = q * (0.12f + 0.62f * tt);
        RECT r = {(int)(cx - rr), (int)(cy - rr), (int)(cx + rr), (int)(cy + rr)};
        s.border = RGB(235, 245, 255);
        s.borderA = (int)(220 * (1 - tt));
        s.bw = (float)MB_(2);
        ui_shape(c, r, &s);
      }
    } else if (f->kind == MBFX_BOOM) {
      float rr = q * (0.18f + 0.75f * sqrtf(t));
      RECT r = {(int)(cx - rr), (int)(cy - rr), (int)(cx + rr), (int)(cy + rr)};
      s.fill = RGB(255, 200, 90);
      s.fillA = (int)(200 * (1 - t) * (1 - t));
      s.border = RGB(255, 110, 40);
      s.borderA = (int)(230 * (1 - t));
      s.bw = (float)MB_(3);
      ui_shape(c, r, &s);
    } else {
      RECT r = {x + 1, y + 1, x + q - 1, y + q - 1};
      UiShape b;
      memset(&b, 0, sizeof(b));
      b.r = (float)MB_(3);
      b.fill = RGB(255, 230, 160);
      b.fillA = (int)(150 * (1 - t));
      ui_shape(c, r, &b);
    }
  }
}

static void mb_paint_boards(HDC hdc, RECT rc) {
  mb_art_build();
  int q = mb_q(), by = mb_by(), W = q * MB_N, me = g_mbn.me, op = 1 - me;
  RECT box = {MB_(MB_LEFT), MB_(MB_TOP), rc.right - MB_(MB_LEFT), by + W + MB_(10)};
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, hdc, box, box.left, box.top, TRUE)) return;
  ULONGLONG now = GetTickCount64();
  for (int b = 0; b < 2; b++) {
    int bx = mb_bx(b);
    /* мягкая тень под морем */
    for (int i = 0; i < 5; i++) {
      RECT r = {bx - i + MB_(1), by - i / 2 + MB_(3), bx + W + i + MB_(1), by + W + i + MB_(4)};
      UiShape k;
      memset(&k, 0, sizeof(k));
      k.r = (float)MB_(6) + i;
      k.fill = RGB(0, 0, 0);
      k.fillA = 14;
      ui_shape(&cv, r, &k);
    }
    gn_blit(&cv, &g_mbArt.sea, bx, by, 255);
  }
  /* своё поле: флот, выстрелы соперника */
  for (int k = 0; k < MB_SHIPS; k++) {
    if (k == g_mb.pick) continue;
    const MbShip *s = &g_mb.ships[k];
    BOOL sunk = g_mb.phase == MB_BATTLE && g_mb.seen[me][mb_cell(s->r, s->c)] == MBS_SUNK;
    mb_draw_ship(&cv, 0, s, sunk, 255);
  }
  mb_draw_marks(&cv, 0, me, now);
  /* расстановка: взятый корабль — за мышью, зелёный, если встаёт, красный — нет */
  if (g_mb.pick >= 0 && g_mb.hoverBoard == 0 && g_mb.hoverCell >= 0) {
    MbShip t = mb_pick_at(g_mb.hoverCell);
    BOOL ok = mb_fits(g_mb.ships, MB_SHIPS, g_mb.pick, t);
    for (int i = 0; i < t.len; i++) {
      int r, c;
      mb_ship_cell(&t, i, &r, &c);
      if (r < 0 || r >= MB_N || c < 0 || c >= MB_N) continue;
      int x, y;
      mb_cell_xy(0, mb_cell(r, c), &x, &y);
      RECT cr = {x + 1, y + 1, x + q, y + q};
      gn_tint(&cv, cr, ok ? RGB(90, 220, 130) : RGB(240, 70, 60), 90);
    }
    if (t.r >= 0 && t.c >= 0 && t.r + (t.horiz ? 0 : t.len - 1) < MB_N && t.c + (t.horiz ? t.len - 1 : 0) < MB_N)
      mb_draw_ship(&cv, 0, &t, FALSE, ok ? 230 : 140);
  } else if (g_mb.phase == MB_SETUP && !g_mb.iReady && g_mb.pick < 0 && g_mb.hoverBoard == 0 && g_mb.hoverCell >= 0 &&
             g_mbn.mode == GN_PLAY) {
    int k = mb_ship_at(g_mb.hoverCell);
    if (k >= 0)
      for (int i = 0; i < g_mb.ships[k].len; i++) {
        int r, c, x, y;
        mb_ship_cell(&g_mb.ships[k], i, &r, &c);
        mb_cell_xy(0, mb_cell(r, c), &x, &y);
        RECT cr = {x + 1, y + 1, x + q, y + q};
        gn_tint(&cv, cr, RGB(255, 236, 160), 70);
      }
  }
  /* поле соперника: потопленные, отметки, в конце — его открытый флот */
  if (g_mb.oppRevealed && !g_mb.cheat) {
    MbShip os[MB_SHIPS];
    if (mb_ships_from_grid(g_mb.oppFleet, os))
      for (int k = 0; k < MB_SHIPS; k++)
        if (g_mb.seen[op][mb_cell(os[k].r, os[k].c)] != MBS_SUNK) mb_draw_ship(&cv, 1, &os[k], FALSE, 150);
  }
  mb_draw_sunk_ships(&cv, 1, op);
  mb_draw_marks(&cv, 1, op, now);
  /* куда сейчас целюсь; выстрел ушёл — прицел, пока нет ответа */
  if (g_mb.waitAnswer && g_mb.shooter == me && g_mb.pendingCell >= 0) {
    int a = (int)(170 + 85 * sinf((float)(now % 700) / 700.0f * 6.2832f));
    mb_blit_cell(&cv, &g_mbArt.aim, 1, g_mb.pendingCell, a);
  }
  if (mb_my_shot() && g_mb.hoverBoard == 1 && g_mb.hoverCell >= 0 && !g_mb.shotAt[op][g_mb.hoverCell] &&
      g_mb.seen[op][g_mb.hoverCell] != MBS_EMPTY) {
    int x, y;
    mb_cell_xy(1, g_mb.hoverCell, &x, &y);
    RECT row = {mb_bx(1), y, mb_bx(1) + W, y + q}, col = {x, by, x + q, by + W};
    gn_tint(&cv, row, RGB(255, 255, 255), 22);
    gn_tint(&cv, col, RGB(255, 255, 255), 22);
    mb_blit_cell(&cv, &g_mbArt.hover, 1, g_mb.hoverCell, 255);
  }
  /* последний выстрел соперника по мне — рамкой */
  if (g_mb.phase == MB_BATTLE && g_mb.lastCell[me] >= 0) {
    int x, y;
    mb_cell_xy(0, g_mb.lastCell[me], &x, &y);
    RECT r = {x + 1, y + 1, x + q, y + q};
    UiShape k;
    memset(&k, 0, sizeof(k));
    k.r = (float)MB_(3);
    k.border = RGB(255, 214, 110);
    k.borderA = 230;
    k.bw = (float)MB_(2);
    ui_shape(&cv, r, &k);
  }
  mb_draw_fx(&cv, now);
  /* поле соперника до боя — пелена с надписью */
  if (g_mb.phase == MB_SETUP && g_mbn.mode == GN_PLAY) {
    RECT r = {mb_bx(1), by, mb_bx(1) + W, by + W};
    gn_tint(&cv, r, RGB(10, 24, 44), 120);
    if (g_fontUi) SelectObject(cv.dc, g_fontUi);
    SetBkMode(cv.dc, TRANSPARENT);
    SetTextColor(cv.dc, RGB(230, 238, 248));
    wchar_t t[200];
    if (g_mb.oppReady) _snwprintf(t, 200, L"%s: флот готов к бою", g_mbn.oppName);
    else _snwprintf(t, 200, L"%s расставляет корабли…", g_mbn.oppName);
    DrawTextW(cv.dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  /* буквы и цифры */
  static const wchar_t *kLet = L"АБВГДЕЖЗИК";
  if (g_fontSmall) SelectObject(cv.dc, g_fontSmall);
  SetBkMode(cv.dc, TRANSPARENT);
  SetTextColor(cv.dc, COL_MUTED);
  for (int b = 0; b < 2; b++) {
    int bx = mb_bx(b);
    for (int i = 0; i < MB_N; i++) {
      wchar_t l[2] = {kLet[i], 0}, d[4];
      _snwprintf(d, 4, L"%d", i + 1);
      RECT rl = {bx + i * q, by - MB_(MB_LAB), bx + (i + 1) * q, by - MB_(2)};
      RECT rd = {bx - MB_(MB_LAB), by + i * q, bx - MB_(4), by + (i + 1) * q};
      DrawTextW(cv.dc, l, -1, &rl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      DrawTextW(cv.dc, d, -1, &rd, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
  }
  /* заголовки полей: кто, сколько на плаву; у соперника — что осталось */
  int ty = MB_(MB_TOP), th = MB_(24);
  if (g_fontUi) SelectObject(cv.dc, g_fontUi);
  BOOL myTurn = mb_my_shot(), oppTurn = g_mbn.mode == GN_PLAY && g_mb.phase == MB_BATTLE && g_mb.shooter == op;
  for (int b = 0; b < 2; b++) {
    int bx = mb_bx(b);
    int side = b ? op : me;
    wchar_t t[180];
    if (b) _snwprintf(t, 180, L"Флот: %s", g_mbn.oppName);
    else lstrcpynW(t, L"Ваш флот", 180);
    /* у того, по чьему полю сейчас стреляют, заголовок ярче */
    BOOL hot = b ? myTurn : oppTurn;
    SetTextColor(cv.dc, hot ? COL_SAGE : COL_INK);
    RECT r = {bx, ty, bx + W - MB_(136), ty + th}; /* справа — полоски кораблей */
    DrawTextW(cv.dc, t, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    /* что осталось на плаву: полоски по длине кораблей */
    int x = bx + W, cy = ty + th / 2, cw = MB_(5), gap = MB_(3);
    for (int k = MB_SHIPS - 1; k >= 0; k--) {
      int len = kMbLens[k];
      /* сколько кораблей этой длины уже потоплено — столько последних серые */
      int idx = 0;
      for (int j = 0; j < k; j++) idx += kMbLens[j] == len;
      BOOL gone = g_mb.phase == MB_BATTLE && idx < g_mb.sunkLen[side][len];
      x -= len * cw;
      RECT sr = {x, cy - MB_(3), x + len * cw - 1, cy + MB_(3)};
      UiShape k2;
      memset(&k2, 0, sizeof(k2));
      k2.r = (float)MB_(2);
      k2.fill = gone ? COL_LINE : (b ? RGB(64, 112, 168) : RGB(110, 122, 138));
      k2.fillA = 255;
      ui_shape(&cv, sr, &k2);
      x -= gap;
    }
  }
  /* итог — полосой поперёк обоих полей */
  if (g_mbn.mode == GN_OVER) {
    RECT band = {mb_bx(0), by + W / 2 - MB_(34), mb_bx(1) + W, by + W / 2 + MB_(34)};
    gn_tint(&cv, band, RGB(8, 18, 34), 185);
    if (!g_mbBig) g_mbBig = CreateFontW(-MB_(26), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    const wchar_t *big = g_mb.winner < 0 ? L"Партия остановлена" : g_mb.winner == me ? L"Победа!" : L"Поражение";
    if (g_mbBig) SelectObject(cv.dc, g_mbBig);
    SetTextColor(cv.dc, g_mb.winner == me ? RGB(255, 214, 110) : RGB(236, 240, 248));
    RECT t1 = {band.left, band.top + MB_(4), band.right, band.top + MB_(40)};
    DrawTextW(cv.dc, big, -1, &t1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (g_fontSmall) SelectObject(cv.dc, g_fontSmall);
    SetTextColor(cv.dc, RGB(206, 216, 230));
    RECT t2 = {band.left + MB_(10), band.top + MB_(40), band.right - MB_(10), band.bottom - MB_(4)};
    const wchar_t *why = g_mb.cheat ? g_mb.endWhy
                         : g_mb.resigned ? (g_mb.winner == me ? L"соперник сдался" : L"вы сдались")
                         : g_mb.winner < 0 ? g_mb.endWhy
                         : g_mb.winner == me ? L"весь флот соперника на дне" : L"ваш флот потоплен";
    DrawTextW(cv.dc, why, -1, &t2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  ui_canvas_end(&cv, hdc);
  /* кончившиеся вспышки — вон */
  int w = 0;
  for (int k = 0; k < g_mbNFx; k++)
    if (now - g_mbFx[k].t0 < (ULONGLONG)mb_fx_ms(g_mbFx[k].kind)) g_mbFx[w++] = g_mbFx[k];
  g_mbNFx = w;
}

/* ---- окно ------------------------------------------------------------------- */

static void mb_layout(void) {
  if (!g_mbWnd) return;
  RECT rc;
  GetClientRect(g_mbWnd, &rc);
  place_panel_close(g_mbWnd);
  gn_layout_lobby(&g_mbn, g_nd.poll);
  int m = g_mbn.mode, pad = MB_(14), bh = MB_(28), gap = MB_(8);
  int by = mb_by() + mb_q() * MB_N + MB_(40);
  HWND shuf = GetDlgItem(g_mbWnd, ID_MB_SHUFFLE), ready = GetDlgItem(g_mbWnd, ID_MB_READY),
       resign = GetDlgItem(g_mbWnd, ID_MB_RESIGN), back = GetDlgItem(g_mbWnd, ID_MB_BACK);
  BOOL play = m == GN_PLAY, over = m == GN_OVER, setup = play && g_mb.phase == MB_SETUP && !g_mb.iReady;
  ShowWindow(shuf, setup ? SW_SHOW : SW_HIDE);
  ShowWindow(ready, setup ? SW_SHOW : SW_HIDE);
  ShowWindow(resign, play ? SW_SHOW : SW_HIDE);
  ShowWindow(back, over ? SW_SHOW : SW_HIDE);
  if (setup) {
    MoveWindow(shuf, pad, by, MB_(130), bh, TRUE);
    MoveWindow(ready, pad + MB_(130) + gap, by, MB_(130), bh, TRUE);
    EnableWindow(ready, g_mb.pick < 0);
  }
  if (play) MoveWindow(resign, rc.right - pad - MB_(100), by, MB_(100), bh, TRUE);
  if (over) MoveWindow(back, pad, by, MB_(160), bh, TRUE);
}

static void mb_mark_btn(void) {
  gn_mark_btn(&g_mbn, g_btnMb, L"Морской бой",
              mb_my_shot() || (g_mbn.mode == GN_PLAY && g_mb.phase == MB_SETUP && !g_mb.iReady));
}

static void mb_refresh_view(void) {
  mb_mark_btn();
  if (!g_mbWnd) return;
  if (g_mbn.mode == GN_LOBBY) gn_list_sync(&g_mbn, GetDlgItem(g_mbWnd, ID_MB_LIST), g_nd.poll);
  mb_layout();
  InvalidateRect(g_mbWnd, NULL, FALSE);
  if (g_mbn.mode == GN_PLAY && ((g_mb.waitAnswer && g_mb.shooter == g_mbn.me) || g_mbNFx ||
                                (g_mb.phase == MB_BATTLE && memchr(g_mb.seen, MBS_HIT, sizeof(g_mb.seen)))))
    SetTimer(g_mbWnd, MB_TIMER, 40, NULL); /* прицел мигает, огонь дышит */
}

static void morskoy_online_changed(void) {
  if (g_mbn.mode == GN_PLAY) mb_turn_status();
  if (g_mbWnd && IsWindowVisible(g_mbWnd) && (g_mbn.mode == GN_LOBBY || g_mbn.mode == GN_PLAY)) mb_refresh_view();
  else mb_mark_btn();
}

static void mb_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Морской бой");
  if (g_mbn.mode == GN_LOBBY) {
    gn_paint_lobby(&g_mbn, hdc, rc, g_nd.poll,
                   L"Морской бой: поле 10×10, корабли 4, 3, 3, 2, 2, 2, 1, 1, 1, 1 палубы, касаться нельзя даже "
                   L"углами. Попали — стреляете ещё. Свой флот видите только вы: в общую папку идёт лишь его "
                   L"отпечаток, а в конце программа соперника сверяет расстановку со всеми ответами.");
    return;
  }
  if (g_mbn.mode == GN_WAIT) {
    gn_paint_wait(&g_mbn, hdc, rc);
    return;
  }
  mb_paint_boards(hdc, rc);
  int pad = MB_(14), y = mb_by() + mb_q() * MB_N + MB_(10);
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  RECT st = {pad, y, rc.right - pad, y + MB_(24)};
  SetTextColor(hdc, g_mbn.mode == GN_OVER ? COL_SAGE : mb_my_shot() ? COL_SAGE : COL_INK);
  DrawTextW(hdc, g_mbn.status, -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  wchar_t dg[400];
  gn_diag_text(&g_mbn, dg, 400);
  RECT dr = {pad, y + MB_(66), rc.right - pad, rc.bottom - MB_(4)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, g_mbn.writeFail ? RGB(200, 60, 40) : COL_MUTED);
  DrawTextW(hdc, dg, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

/* расстановка: щелчок по своему полю */
static void mb_setup_click(int cell) {
  if (g_mbn.mode != GN_PLAY || g_mb.phase != MB_SETUP || g_mb.iReady || cell < 0) return;
  if (g_mb.pick >= 0) {
    MbShip t = mb_pick_at(cell);
    if (!mb_fits(g_mb.ships, MB_SHIPS, g_mb.pick, t)) {
      MessageBeep(MB_ICONWARNING);
      return;
    }
    g_mb.ships[g_mb.pick] = t;
    g_mb.pick = -1;
  } else {
    int k = mb_ship_at(cell);
    if (k < 0) return;
    g_mb.pick = k;
    g_mb.pickWas = g_mb.ships[k];
    g_mb.pickDr = cell / MB_N - g_mb.ships[k].r;
    g_mb.pickDc = cell % MB_N - g_mb.ships[k].c;
  }
  mb_turn_status();
  mb_refresh_view();
}

static void mb_rotate_pick(void) {
  if (g_mb.pick < 0) return;
  MbShip *s = &g_mb.ships[g_mb.pick];
  s->horiz = (signed char)!s->horiz;
  int t = g_mb.pickDr;
  g_mb.pickDr = g_mb.pickDc;
  g_mb.pickDc = t;
  InvalidateRect(g_mbWnd, NULL, FALSE);
}

static void mb_drop_pick(void) {
  if (g_mb.pick < 0) return;
  g_mb.ships[g_mb.pick] = g_mb.pickWas;
  g_mb.pick = -1;
  mb_turn_status();
  mb_refresh_view();
}

static LRESULT CALLBACK MorskoyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    mb_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_TIMER:
    if (wParam == MB_TIMER) {
      BOOL live = g_mbn.mode == GN_PLAY &&
                  ((g_mb.waitAnswer && g_mb.shooter == g_mbn.me) ||
                   (g_mb.phase == MB_BATTLE && memchr(g_mb.seen, MBS_HIT, sizeof(g_mb.seen))));
      if (!g_mbNFx && !live) KillTimer(hwnd, MB_TIMER);
      else if (!g_mbNFx) SetTimer(hwnd, MB_TIMER, 40, NULL);
      RECT rc;
      GetClientRect(hwnd, &rc);
      RECT bd = {0, mb_by() - MB_(4), rc.right, mb_by() + mb_q() * MB_N + MB_(6)};
      InvalidateRect(hwnd, &bd, FALSE);
    }
    return 0;
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_LBUTTONDOWN: {
    int board, cell = mb_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &board);
    if (g_mbn.mode != GN_PLAY) return 0;
    if (board == 0) mb_setup_click(cell);
    else if (board == 1) mb_fire(cell);
    return 0;
  }
  case WM_RBUTTONDOWN:
    if (g_mb.pick >= 0) mb_rotate_pick();
    return 0;
  case WM_MOUSEWHEEL:
    if (g_mb.pick >= 0) mb_rotate_pick();
    return 0;
  case WM_MOUSEMOVE: {
    int board, cell = mb_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &board);
    if (board != g_mb.hoverBoard || cell != g_mb.hoverCell) {
      g_mb.hoverBoard = board;
      g_mb.hoverCell = cell;
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&tme);
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  }
  case WM_MOUSELEAVE:
    g_mb.hoverBoard = g_mb.hoverCell = -1;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT && g_mbn.mode == GN_PLAY &&
        ((g_mb.hoverBoard == 1 && mb_my_shot() && g_mb.hoverCell >= 0 && !g_mb.shotAt[1 - g_mbn.me][g_mb.hoverCell]) ||
         (g_mb.hoverBoard == 0 && g_mb.phase == MB_SETUP && !g_mb.iReady && g_mb.hoverCell >= 0 &&
          (g_mb.pick >= 0 || mb_ship_at(g_mb.hoverCell) >= 0)))) {
      SetCursor(LoadCursorW(NULL, IDC_HAND));
      return TRUE;
    }
    break;
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      if (g_mb.pick >= 0) mb_drop_pick();
      else ShowWindow(hwnd, SW_HIDE);
    }
    if (wParam == 'R' || wParam == VK_SPACE) mb_rotate_pick();
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id != ID_MB_LIST && HIWORD(wParam) != BN_CLICKED) return 0;
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (gn_command(&g_mbn, id, HIWORD(wParam)) && id == ID_MB_LIST) return 0; /* фокус у списка не отнимаем */
    if (id == ID_MB_SHUFFLE) mb_shuffle();
    if (id == ID_MB_READY) mb_ready();
    if (id == ID_MB_RESIGN) mb_resign();
    if (id == ID_MB_BACK) gn_back_to_lobby(&g_mbn);
    SetFocus(hwnd);
    return 0;
  }
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void morskoy_start(void) {
  if (g_mbn.thread) return;
  wchar_t *t = gn_load_local(&g_mbn);
  if (t) {
    /* свой флот с прошлого запуска — только если партия та же */
    wchar_t wg[120], ws[40];
    char g[101];
    if (nd_field(t, L"fleet", wg, 120) && nd_field(t, L"salt", ws, 40)) {
      WideCharToMultiByte(CP_UTF8, 0, wg, -1, g, 101, NULL, NULL);
      WideCharToMultiByte(CP_UTF8, 0, ws, -1, g_mb.salt, 40, NULL, NULL);
      g_mb.haveFleet = mb_ships_from_grid(g, g_mb.ships);
    }
    if (!g_mb.haveFleet) mb_random_fleet(g_mb.ships), g_mb.haveFleet = TRUE, gn_secret(g_mb.salt);
    free(t);
  }
  gn_start(&g_mbn);
}

static void morskoy_show(void) {
  if (!g_mbWnd) {
    HDC s = GetDC(NULL);
    g_mbS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    g_mbn.sc = g_mbS;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MorskoyProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadMorskoy";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    int w = MB_(MB_LEFT * 2 + MB_LAB * 2 + MB_CELL * MB_N * 2 + MB_GAP);
    int h = MB_(MB_TOP + 26 + MB_LAB + MB_CELL * MB_N + 128);
    g_mbWnd = CreateWindowExW(WS_EX_APPWINDOW, L"CursorPadMorskoy", L"Морской бой",
                              WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU, 0, 0, w, h, NULL, NULL,
                              g_inst, NULL);
    if (!g_mbWnd) return;
    round_corners(g_mbWnd);
    mk_btn(g_mbWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_mbWnd, L"–", ID_PANEL_MIN);
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0,
                                100, 100, g_mbWnd, (HMENU)(INT_PTR)ID_MB_LIST, g_inst, NULL);
    const struct {
      const wchar_t *t;
      int id;
    } btns[] = {{L"Позвать играть", ID_MB_INVITE}, {L"Принять", ID_MB_ACCEPT},  {L"Отказаться", ID_MB_DECLINE},
                {L"Случайно", ID_MB_SHUFFLE},      {L"В бой!", ID_MB_READY},    {L"Сдаться", ID_MB_RESIGN},
                {L"К списку игроков", ID_MB_BACK}, {L"Отменить приглашение", ID_MB_CANCEL}};
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
      HWND b = mk_btn(g_mbWnd, btns[i].t, btns[i].id);
      if (g_fontUi) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    }
    if (g_fontBody) SendMessageW(list, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_mbWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                 SWP_NOZORDER);
  }
  if (g_mbn.mode == GN_PLAY) mb_turn_status();
  mb_refresh_view();
  ShowWindow(g_mbWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_mbWnd);
  SetFocus(g_mbWnd);
}
