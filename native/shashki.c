/* ---- Русские шашки по сети через общую папку ------------------------------

   Как нарды (nardy.c) и через ту же общую папку из Настроек; кто в сети —
   тот же список, что у нард. Связь — общая с нардами и морским боем
   (game_net.c), свои файлы — в <папка>\CursorPad-Shashki. Строки своего
   файла партии, кроме общих: m (ход: «m\t<номер полухода>\t<путь>», путь —
   «c3-d4» или «c3:e5:c7»), draw (предлагаю ничью на полуходе N), drawok
   (согласен). Кто белые — по номеру партии, обе программы считают
   одинаково. Каждый ход программа соперника проверяет по правилам
   (shashki_rules.c); партия восстанавливается из файлов целиком. Ход уходит
   сразу, как только путь шашки дошёл до конца (со взятием — до последнего
   поля).

   Вид (с 2026.10.22): доска из дерева с фактурой — рама из ореха,
   светлые поля клёна, тёмные — палисандра; объёмные шашки с ободком,
   желобками и бликом, у дамки — золотая корона. Подсветки сглажены: мягкое
   золотое кольцо у шашек, которыми можно ходить, у выбранной — пульсирует,
   сама она приподнята; зелёные точки — куда можно, красное кольцо — кого
   бьём, последний ход — тёплой заливкой полей. Ходы — и свои, и соперника —
   плавно едут по пути, взятые тают, превращение в дамку вспыхивает.
   Над кнопками — табло: чей ход, у кого сколько шашек. */

#include "shashki_rules.c"

#define WM_SHASHKI_POLL (WM_APP + 63) /* lParam — GnPoll*, освобождает получатель */
#define SH_TIMER 1                    /* анимация и пульс выбранной шашки */
/* ID_SH_* — в cursorpad.c */

typedef struct {
  int white; /* какая сторона (0/1) играет белыми */
  ShBoard board;
  int ply;    /* номер полухода на очереди, с 1: нечётный — белых */
  int quiet;  /* полуходов подряд только дамками и без взятий */
  int winner; /* сторона-победитель; −1 — ничья или партия остановлена */
  BOOL draw, resigned;
  wchar_t endWhy[200];
  /* мой ход: выбранная шашка и пройденная часть пути */
  unsigned char part[SH_MAXPATH];
  int npart;
  /* последний ход — подсветить и показать соперника движением */
  ShMove last;
  int nlast;
  BOOL lastOpp;
  BOOL oppOffersDraw, iOffered;
  int notifiedPly;
} ShGame;

static ShGame g_sh;
static HWND g_shWnd;
static float g_shS = 1.0f;
static int g_shHover = -1; /* поле под мышью */

static void sh_fresh(void);
static void sh_refresh_view(void);
static void sh_turn_status(void);

static GnNet g_shn = {
    .sub = L"CursorPad-Shashki",
    .local = L"shashki",
    .title = L"Шашки",
    .invText = L"зовёт сыграть в шашки",
    .moveKey = L"m",
    .msg = WM_SHASHKI_POLL,
    .balloon = 5,
    .wnd = &g_shWnd,
    .sc = 1.0f,
    .idList = ID_SH_LIST,
    .idInvite = ID_SH_INVITE,
    .idAccept = ID_SH_ACCEPT,
    .idDecline = ID_SH_DECLINE,
    .idCancel = ID_SH_CANCEL,
    .fresh = sh_fresh,
    .view = sh_refresh_view,
    .turn = sh_turn_status,
};

#define sh_status(...) gn_status(&g_shn, __VA_ARGS__)

static int SS_(int v) { return (int)(v * g_shS + 0.5f); }

static int sh_color_of_side(int side) { return side == g_sh.white ? 1 : -1; }
static int sh_side_of_ply(int ply) { return (ply % 2 == 1) ? g_sh.white : 1 - g_sh.white; }
static int sh_my_color(void) { return sh_color_of_side(g_shn.me); }

/* ---- движение на доске ------------------------------------------------------ */

/* Анимация хода: доска до хода, сам ход, сколько скачков уже показано
   (посреди своего взятия шашка стоит на последнем пройденном поле). partial —
   ход ещё не кончен: взятые бледнеют и остаются (турецкий удар), иначе после
   хода тают. */
typedef struct {
  BOOL on, partial;
  ULONGLONG t0;
  ShBoard before;
  ShMove m;
  int startHop;
  int hopMs[SH_MAXPATH];
} ShAnim;

static ShAnim g_shAnim;

#define SH_FADE_MS 220
#define SH_CROWN_MS 420

/* длительность скачка: дальний ход дамки — чуть дольше */
static int sh_hop_ms(int a, int b) {
  int d = abs(sh_row(a) - sh_row(b));
  return 150 + (d > 1 ? (d - 1) * 45 : 0);
}

static int sh_anim_moves_ms(const ShAnim *an) {
  int t = 0;
  for (int h = an->startHop; h + 1 < an->m.npath; h++) t += an->hopMs[h];
  return t;
}

static int sh_anim_total(const ShAnim *an) {
  int t = sh_anim_moves_ms(an);
  if (!an->partial) t += an->m.ncap ? SH_FADE_MS : 0;
  if (!an->partial && an->m.promote) t += SH_CROWN_MS;
  return t;
}

static void sh_anim_start(const ShBoard *before, const ShMove *m, int startHop, BOOL partial) {
  if (!g_shWnd || !IsWindowVisible(g_shWnd) || IsIconic(g_shWnd) || m->npath < 2) {
    g_shAnim.on = FALSE;
    return;
  }
  memset(&g_shAnim, 0, sizeof(g_shAnim));
  g_shAnim.before = *before;
  g_shAnim.m = *m;
  g_shAnim.startHop = startHop < 0 ? 0 : startHop;
  g_shAnim.partial = partial;
  for (int h = 0; h + 1 < m->npath; h++) g_shAnim.hopMs[h] = sh_hop_ms(m->path[h], m->path[h + 1]);
  g_shAnim.t0 = GetTickCount64();
  g_shAnim.on = TRUE;
  SetTimer(g_shWnd, SH_TIMER, 15, NULL);
}

static int sh_anim_now(void) { return (int)(GetTickCount64() - g_shAnim.t0); }

/* ---- ход партии по файлам ------------------------------------------------- */

static void sh_over(int winner, const wchar_t *why) {
  g_shn.mode = GN_OVER;
  g_sh.winner = winner;
  lstrcpynW(g_sh.endWhy, why ? why : L"", 200);
  gn_save_local(&g_shn);
}

/* Разыграть партию с начала по обоим файлам (свой — из памяти: только что
   сделанный ход папка могла ещё не вернуть). */
static void sh_replay(const wchar_t *p0, const wchar_t *p1) {
  GnNet *n = &g_shn;
  const wchar_t *mine = gn_mine(n), *theirs = gn_theirs(n, p0, p1);
  const wchar_t *file[2];
  file[n->me] = mine;
  file[1 - n->me] = theirs;
  if (!gn_replay_head(n, theirs)) return;
  g_sh.white = gn_game_bit(n->game);
  if (!gn_accepted(file[1])) { /* принявший ещё не ответил */
    n->mode = GN_WAIT;
    gn_wait_status(n);
    return;
  }
  if (n->mode == GN_WAIT) n->mode = GN_PLAY;
  ShBoard bd;
  sh_start(&bd);
  int quiet = 0;
  g_sh.nlast = 0;
  g_sh.oppOffersDraw = g_sh.iOffered = FALSE;
  static ShMove mv[SH_MAXMOVES];
  for (int ply = 1;; ply++) {
    g_sh.board = bd;
    g_sh.ply = ply;
    g_sh.quiet = quiet;
    /* ничья по согласию: один предложил на этом полуходе, другой согласился */
    for (int s = 0; s < 2; s++)
      if (gn_has_num(file[s], L"draw", ply) && gn_has_num(file[1 - s], L"drawok", ply)) {
        g_sh.draw = TRUE;
        sh_over(-1, L"Ничья по согласию");
        return;
      }
    int side = sh_side_of_ply(ply), color = sh_color_of_side(side);
    int cnt = sh_legal(&bd, color, mv, SH_MAXMOVES);
    if (cnt == 0) { /* ходить нечем — проиграл */
      sh_over(1 - side, sh_count(&bd, color) ? L"Соперника заперли — ходить нечем" : L"Все шашки взяты");
      return;
    }
    char path[80];
    if (!gn_num_line(file[side], L"m", ply, path, 80)) {
      /* сдались — партия кончена на этой позиции (раньше проверялось до
         первого хода, и после сдачи доска вставала в начальную расстановку) */
      if (nd_has_line(file[0], L"resign") || nd_has_line(file[1], L"resign")) {
        int loser = nd_has_line(file[0], L"resign") ? 0 : 1;
        g_sh.resigned = TRUE;
        sh_over(1 - loser, NULL);
        return;
      }
      g_sh.oppOffersDraw = gn_has_num(theirs, L"draw", ply) && !gn_has_num(mine, L"drawok", ply);
      g_sh.iOffered = gn_has_num(mine, L"draw", ply);
      return; /* этот ход ещё не сделан */
    }
    unsigned char sq[SH_MAXPATH];
    int ns = sh_parse_path(path, sq, SH_MAXPATH);
    int k = ns ? sh_find_move(mv, cnt, sq, ns) : -1;
    if (k < 0) {
      wchar_t why[200];
      _snwprintf(why, 200, L"Ход %d не прошёл проверку правил — партия остановлена", (ply + 1) / 2);
      g_sh.winner = -1;
      sh_over(-1, why);
      return;
    }
    BOOL man = !sh_isking(bd.sq[mv[k].path[0]]);
    quiet = (mv[k].ncap || man) ? 0 : quiet + 1;
    sh_apply(&bd, &mv[k]);
    g_sh.last = mv[k];
    g_sh.nlast = mv[k].npath;
    g_sh.lastOpp = side != n->me;
    if (quiet >= 30) {
      g_sh.board = bd;
      g_sh.ply = ply + 1;
      g_sh.draw = TRUE;
      sh_over(-1, L"15 ходов только дамками без взятий — ничья");
      return;
    }
  }
}

static BOOL sh_my_turn(void) { return g_shn.mode == GN_PLAY && sh_side_of_ply(g_sh.ply) == g_shn.me; }

static void sh_turn_status(void) {
  if (g_shn.mode != GN_PLAY) return;
  if (!sh_my_turn()) {
    if (g_sh.oppOffersDraw) sh_status(L"%s предлагает ничью — «Согласиться на ничью» или ждите его хода", g_shn.oppName);
    else if (!gn_opp_online(&g_shn))
      sh_status(L"Ходит %s — он сейчас не в сети: ход увидит, когда запустит CursorPad", g_shn.oppName);
    else sh_status(L"Ходит %s…%s", g_shn.oppName, g_sh.iOffered ? L" (вы предложили ничью)" : L"");
    return;
  }
  static ShMove mv[SH_MAXMOVES];
  int n = sh_legal(&g_sh.board, sh_my_color(), mv, SH_MAXMOVES);
  BOOL cap = n > 0 && mv[0].ncap > 0;
  if (g_sh.oppOffersDraw) sh_status(L"%s предлагает ничью — «Согласиться на ничью» или просто ходите", g_shn.oppName);
  else if (g_sh.npart > 1) sh_status(L"Бейте дальше — отмечены поля, куда можно");
  else if (g_sh.npart == 1) sh_status(L"Куда? Отмечены поля, куда можно");
  else if (cap) sh_status(L"Ваш ход — бить обязательно: подсвечены шашки, которые бьют");
  else sh_status(L"Ваш ход — выберите шашку");
}

static void sh_after_replay(int prevPly, int prevMode) {
  GnNet *n = &g_shn;
  if (prevMode == GN_WAIT && n->mode == GN_PLAY) gn_save_local(n); /* приняли — после перезапуска сразу игра */
  if (n->mode == GN_PLAY) {
    if (g_sh.ply != prevPly || prevMode != GN_PLAY) g_sh.npart = 0;
    sh_turn_status();
    if (sh_my_turn() && g_sh.notifiedPly != g_sh.ply && !gn_in_front(n)) {
      g_sh.notifiedPly = g_sh.ply;
      wchar_t t[160];
      if (g_sh.ply <= 2)
        _snwprintf(t, 160, L"Партия с %s: вы играете %s — ваш ход", n->oppName, sh_my_color() > 0 ? L"белыми" : L"чёрными");
      else _snwprintf(t, 160, L"%s сходил — ваш ход", n->oppName);
      gn_balloon(n, t);
    }
  }
  if (n->mode == GN_OVER && prevMode != GN_OVER) {
    wchar_t t[240];
    if (g_sh.draw) lstrcpynW(t, g_sh.endWhy, 240);
    else if (g_sh.winner == n->me)
      _snwprintf(t, 240, L"Вы выиграли у %s%s", n->oppName, g_sh.resigned ? L" — соперник сдался" : L"");
    else if (g_sh.winner >= 0) _snwprintf(t, 240, L"Выиграл %s%s", n->oppName, g_sh.resigned ? L" — вы сдались" : L"");
    else lstrcpynW(t, g_sh.endWhy, 240);
    if (!g_sh.draw && !g_sh.resigned && g_sh.winner >= 0 && g_sh.endWhy[0]) {
      size_t l = wcslen(t);
      _snwprintf(t + l, 240 - l, L" · %s", g_sh.endWhy);
      t[239] = 0;
    }
    sh_status(L"%s", t);
    if (!gn_in_front(n)) gn_balloon(n, t);
  }
  sh_refresh_view();
}

/* разыграть заново по последнему опросу (после своего хода, сдачи…) */
static void sh_replay_now(void) {
  int prevPly = g_sh.ply, prevMode = g_shn.mode;
  GnPoll *pl = g_shn.poll;
  sh_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  sh_after_replay(prevPly, prevMode);
}

static void sh_on_poll(GnPoll *pl) {
  int prevPly = g_sh.ply, prevMode = g_shn.mode;
  ShBoard before = g_sh.board;
  if (!gn_on_poll(&g_shn, pl)) return;
  sh_replay(pl->p0, pl->p1);
  /* соперник сходил — показать его ход движением */
  if (prevMode == GN_PLAY && g_shn.mode != GN_LOBBY && g_sh.lastOpp && g_sh.nlast >= 2 && g_sh.ply == prevPly + 1)
    sh_anim_start(&before, &g_sh.last, 0, FALSE);
  sh_after_replay(prevPly, prevMode);
}

/* ---- действия игрока -------------------------------------------------------- */

/* новая партия (позвал или принял): начальная позиция */
static void sh_fresh(void) {
  g_sh.white = gn_game_bit(g_shn.game);
  sh_start(&g_sh.board);
  g_sh.ply = 1;
  g_sh.quiet = 0;
  g_sh.winner = -1;
  g_sh.npart = 0;
  g_sh.nlast = 0;
  g_sh.draw = g_sh.resigned = FALSE;
  g_sh.oppOffersDraw = g_sh.iOffered = FALSE;
  g_shAnim.on = FALSE;
}

/* мой ход сделан — в файл и разыграть заново */
static void sh_commit(const ShMove *m) {
  char t[80];
  sh_move_text(m, t, 80);
  wchar_t w[80];
  MultiByteToWideChar(CP_UTF8, 0, t, -1, w, 80);
  sb_add(&g_shn.log, L"m\t%d\t%s\n", g_sh.ply, w);
  g_sh.npart = 0;
  if (!gn_write_mine(&g_shn)) sh_status(L"Не удалось записать ход в общую папку — повторю сам");
  sh_replay_now();
}

/* какие ходы подходят к пройденной части пути */
static int sh_candidates(ShMove *mv, int n, ShMove *out) {
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (mv[i].npath < g_sh.npart) continue;
    int same = 1;
    for (int j = 0; j < g_sh.npart && same; j++) same = mv[i].path[j] == g_sh.part[j];
    if (same) out[k++] = mv[i];
  }
  return k;
}

static void sh_click_square(int sq) {
  if (!sh_my_turn() || sq < 0) return;
  g_shAnim.on = FALSE; /* щёлкнули, пока ехал прошлый ход, — досмотрено */
  static ShMove mv[SH_MAXMOVES], cand[SH_MAXMOVES];
  int n = sh_legal(&g_sh.board, sh_my_color(), mv, SH_MAXMOVES);
  BOOL isStart = FALSE;
  for (int i = 0; i < n; i++) isStart |= mv[i].path[0] == sq;
  if (g_sh.npart == 0) {
    if (isStart) {
      g_sh.part[0] = (unsigned char)sq;
      g_sh.npart = 1;
    }
  } else {
    int nc = sh_candidates(mv, n, cand);
    int next = -1;
    for (int i = 0; i < nc && next < 0; i++)
      if (cand[i].npath > g_sh.npart && cand[i].path[g_sh.npart] == sq) next = i;
    if (next >= 0) {
      ShMove shown = cand[next];
      int hop = g_sh.npart - 1;
      g_sh.part[g_sh.npart++] = (unsigned char)sq;
      nc = sh_candidates(mv, n, cand);
      for (int i = 0; i < nc; i++)
        if (cand[i].npath == g_sh.npart) { /* путь кончился — ход сделан */
          ShBoard before = g_sh.board;
          ShMove done = cand[i];
          sh_commit(&done);
          sh_anim_start(&before, &done, hop, FALSE);
          sh_refresh_view();
          return;
        }
      /* посреди взятия: шашка едет на поле, взятая бледнеет */
      shown.npath = (unsigned char)g_sh.npart;
      shown.ncap = (unsigned char)(g_sh.npart - 1);
      shown.promote = 0;
      sh_anim_start(&g_sh.board, &shown, hop, TRUE);
    } else if (g_sh.npart == 1) {
      /* другую шашку (или ту же — снять выбор) */
      if (isStart && sq != g_sh.part[0]) g_sh.part[0] = (unsigned char)sq;
      else g_sh.npart = 0;
    }
    /* посреди взятия щелчок мимо — ничего: бой надо довести */
  }
  if (g_sh.npart && g_shWnd) SetTimer(g_shWnd, SH_TIMER, 30, NULL); /* пульс выбранной */
  sh_turn_status();
  sh_refresh_view();
}

static void sh_resign(void) {
  if (!gn_resign(&g_shn)) return;
  sh_replay_now();
}

static void sh_offer_draw(void) {
  if (g_shn.mode != GN_PLAY || g_sh.iOffered) return;
  sb_add(&g_shn.log, L"draw\t%d\n", g_sh.ply);
  gn_write_mine(&g_shn);
  g_sh.iOffered = TRUE;
  sh_status(L"Ничья предложена — ждём ответа %s", g_shn.oppName);
  sh_refresh_view();
}

static void sh_accept_draw(void) {
  if (g_shn.mode != GN_PLAY || !g_sh.oppOffersDraw) return;
  sb_add(&g_shn.log, L"drawok\t%d\n", g_sh.ply);
  gn_write_mine(&g_shn);
  sh_replay_now();
}

/* ---- доска ------------------------------------------------------------------ */

#define SH_SQ 50   /* поле, логических точек */
#define SH_TOP 46  /* под шапкой окна */
#define SH_LEFT 14
#define SH_FRAME 20 /* рама с буквами и цифрами */

static int sh_board_px(void) { return SS_(SH_SQ) * 8; }
static int sh_bx(void) { return SS_(SH_LEFT) + SS_(SH_FRAME); }
static int sh_by(void) { return SS_(SH_TOP) + SS_(SH_FRAME); }

/* поле → клетка на экране (свои шашки всегда внизу) */
static void sh_sq_xy(int s, int *x, int *y) {
  int r = sh_row(s), c = sh_col(s), q = SS_(SH_SQ);
  BOOL flip = sh_my_color() < 0;
  int sc = flip ? 7 - c : c, sr = flip ? r : 7 - r;
  *x = sh_bx() + sc * q;
  *y = sh_by() + sr * q;
}

static void sh_sq_center(int s, float *x, float *y) {
  int px, py, q = SS_(SH_SQ);
  sh_sq_xy(s, &px, &py);
  *x = px + q * 0.5f;
  *y = py + q * 0.5f;
}

static int sh_hit(int x, int y) {
  int q = SS_(SH_SQ), bx = sh_bx(), by = sh_by();
  if (x < bx || y < by || x >= bx + 8 * q || y >= by + 8 * q) return -1;
  int sc = (x - bx) / q, sr = (y - by) / q;
  BOOL flip = sh_my_color() < 0;
  int c = flip ? 7 - sc : sc, r = flip ? sr : 7 - sr;
  if (!sh_dark(r, c)) return -1;
  return r * 8 + c;
}

/* ---- спрайты: шашки, тени, кольца ------------------------------------------- */

typedef struct {
  int q;                /* для какого размера поля собраны */
  GnSprite piece[4];    /* 0 — белая, 1 — чёрная, 2 — белая дамка, 3 — чёрная дамка */
  GnSprite mini[2];     /* маленькие для табло */
  GnSprite shadow, lift; /* тень на доске; тень поднятой шашки */
  GnSprite can, sel, cap; /* кольца: можно ходить, выбрана, бьём */
  GnSprite dot, dotHi;  /* куда можно; под мышью */
  GnSprite flash;       /* вспышка превращения */
  GnSprite board;       /* рама и поля целиком — непрозрачно */
  int pieceD, thick, miniD;
} ShArt;

static ShArt g_shArt;
static HFONT g_shBig;

/* точка внутри многоугольника (чётно-нечётно) и расстояние до его рёбер */
static BOOL sh_in_poly(const float *p, int n, float x, float y, float *dist) {
  BOOL in = FALSE;
  float best = 1e9f;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float xi = p[i * 2], yi = p[i * 2 + 1], xj = p[j * 2], yj = p[j * 2 + 1];
    if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) in = !in;
    float ex = xj - xi, ey = yj - yi, l2 = ex * ex + ey * ey;
    float t = l2 > 0 ? ((x - xi) * ex + (y - yi) * ey) / l2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float dx = x - (xi + ex * t), dy = y - (yi + ey * t), d = sqrtf(dx * dx + dy * dy);
    if (d < best) best = d;
  }
  *dist = best;
  return in;
}

/* корона дамки в долях радиуса (u вправо, v вниз от центра): 0 — мимо,
   1 — золото, 2 — тёмный контур, 3 — камень; shade — свет по высоте */
static int sh_crown(float u, float v, float *shade) {
  static const float poly[] = {-0.44f, 0.16f, -0.50f, -0.24f, -0.22f, -0.02f, 0.0f, -0.38f,
                               0.22f,  -0.02f, 0.50f, -0.24f, 0.44f,  0.16f};
  static const float ball[3][2] = {{-0.50f, -0.28f}, {0.0f, -0.43f}, {0.50f, -0.28f}};
  const float ol = 0.045f;
  *shade = 1.12f - (v + 0.45f) * 0.55f;
  /* камни на ободке */
  for (int i = -1; i <= 1; i++) {
    float dx = u - i * 0.22f, dy = v - 0.25f;
    if (dx * dx + dy * dy < 0.055f * 0.055f) return 3;
  }
  /* ободок снизу */
  if (u > -0.46f && u < 0.46f && v > 0.14f && v < 0.36f) {
    BOOL edge = u < -0.46f + ol || u > 0.46f - ol || v < 0.14f + ol * 0.6f || v > 0.36f - ol;
    return edge ? 2 : 1;
  }
  for (int i = 0; i < 3; i++) {
    float dx = u - ball[i][0], dy = v - ball[i][1], d = sqrtf(dx * dx + dy * dy);
    if (d < 0.085f) return d > 0.085f - ol ? 2 : 1;
  }
  float dist;
  if (sh_in_poly(poly, 7, u, v, &dist)) return dist < ol ? 2 : 1;
  return 0;
}

/* Шашка: верх — диск с ободком, двумя желобками и бликом, под ним видна
   боковина (толщина). kind: 0/1 — белая/чёрная простая, 2/3 — дамки. */
static void sh_render_piece(GnSprite *s, int D, int thick, int kind) {
  int w = D + 2, h = D + thick + 2;
  if (!gn_sprite_alloc(s, w, h)) return;
  BOOL black = kind & 1, king = kind >= 2;
  float R = D / 2.0f - 0.5f, cx = w / 2.0f, cyT = R + 1.0f, cyB = cyT + thick;
  float top[3] = {241, 232, 212}, side[3] = {186, 164, 130}, rim[3] = {150, 126, 94};
  if (black) {
    top[0] = 58, top[1] = 44, top[2] = 42;
    side[0] = 28, side[1] = 21, side[2] = 21;
    rim[0] = 20, rim[1] = 15, rim[2] = 15;
  }
  unsigned seed = black ? 911u : 377u;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float ar = 0, ag = 0, ab = 0, cov = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
          float dx = px - cx, dyT = py - cyT, dT = sqrtf(dx * dx + dyT * dyT);
          float r, g, b;
          if (dT <= R) {
            float t = dT / R, u = dx / R, v = dyT / R;
            /* свет сверху слева, к центру чуть ярче */
            float lit = 1.04f - 0.16f * ((u + v) * 0.5f + 0.5f) + 0.06f * (1 - t);
            /* ободок — скос: верх-лево светлее, низ-право темнее */
            if (t > 0.86f) {
              float k = (t - 0.86f) / 0.14f, nx = dx / (dT + 0.001f), ny = dyT / (dT + 0.001f);
              lit += (-(nx * -0.55f + ny * -0.65f)) * -0.30f * k;
            }
            /* желобки: тёмная бороздка и светлая кромка под ней */
            float g1 = (t - 0.70f) / 0.028f, g2 = (t - 0.44f) / 0.022f;
            lit -= 0.16f * expf(-g1 * g1) + 0.10f * expf(-g2 * g2);
            float h1 = (t - 0.745f) / 0.03f;
            lit += 0.07f * expf(-h1 * h1) * (0.5f - v * 0.5f);
            /* мелкая фактура кости или дерева */
            lit += (gn_noise(px * 0.9f, py * 0.25f, seed) - 0.5f) * (black ? 0.07f : 0.04f);
            r = top[0] * lit, g = top[1] * lit, b = top[2] * lit;
            if (t > 0.965f) { /* тонкая кромка */
              r = r * 0.55f + rim[0] * 0.45f, g = g * 0.55f + rim[1] * 0.45f, b = b * 0.55f + rim[2] * 0.45f;
            }
            if (king) {
              float sh;
              int cr = sh_crown(u, v, &sh);
              /* тень от короны — на верх шашки */
              float shd;
              if (!cr && sh_crown(u - 0.05f, v - 0.06f, &shd)) r *= 0.78f, g *= 0.78f, b *= 0.78f;
              if (cr == 1) r = 250 * sh, g = 204 * sh, b = 82 * sh;
              else if (cr == 2) r = 132, g = 86, b = 22;
              else if (cr == 3) {
                if (black) r = 90 * sh, g = 200 * sh, b = 230 * sh;
                else r = 214 * sh, g = 48 * sh, b = 52 * sh;
              }
            }
            /* блик */
            float hx = u + 0.36f, hy = v + 0.42f, hd = sqrtf(hx * hx * 1.3f + hy * hy) / 0.42f;
            if (hd < 1) {
              float k = (1 - hd) * (1 - hd) * (black ? 0.30f : 0.22f);
              r += (255 - r) * k, g += (255 - g) * k, b += (255 - b) * k;
            }
          } else {
            /* боковина: ближе всего к отрезку между центрами верха и низа */
            float cyc = py < cyT ? cyT : (py > cyB ? cyB : py), dyc = py - cyc;
            if (dx * dx + dyc * dyc > R * R) continue;
            float u = dx / R;
            float lit = 0.62f + 0.38f * sqrtf(1 - (u * u > 1 ? 1 : u * u)) - 0.10f * u;
            float bottom = (py - cyT) / (thick + R);
            lit *= 1.0f - 0.25f * bottom;
            r = side[0] * lit, g = side[1] * lit, b = side[2] * lit;
            if (sqrtf(dx * dx + dyc * dyc) > R - 1.0f) r *= 0.8f, g *= 0.8f, b *= 0.8f;
          }
          ar += r, ag += g, ab += b, cov += 1;
        }
      if (cov <= 0) continue;
      s->px[(size_t)y * w + x] = gn_pm(ar / cov, ag / cov, ab / cov, cov / 16.0f);
    }
}

static void sh_art_build(void) {
  int q = SS_(SH_SQ);
  if (g_shArt.q == q && g_shArt.board.px) return;
  int D = q - SS_(8), th = D / 11 < 2 ? 2 : D / 11;
  g_shArt.q = q;
  g_shArt.pieceD = D;
  g_shArt.thick = th;
  for (int k = 0; k < 4; k++) sh_render_piece(&g_shArt.piece[k], D, th, k);
  g_shArt.miniD = SS_(18);
  for (int k = 0; k < 2; k++) sh_render_piece(&g_shArt.mini[k], g_shArt.miniD, g_shArt.miniD / 9 + 1, k);
  gn_make_glow(&g_shArt.shadow, D / 2 + SS_(4), RGB(0, 0, 0), 0.42f, 0.55f);
  gn_make_glow(&g_shArt.lift, D / 2 + SS_(8), RGB(0, 0, 0), 0.30f, 0.35f);
  gn_make_ring(&g_shArt.can, D / 2.0f + SS_(3), (float)SS_(2), (float)SS_(4), RGB(255, 206, 84), 0.85f);
  gn_make_ring(&g_shArt.sel, D / 2.0f + SS_(4), (float)SS_(3), (float)SS_(6), RGB(255, 196, 40), 1.0f);
  gn_make_ring(&g_shArt.cap, D / 2.0f + SS_(3), (float)SS_(3), (float)SS_(5), RGB(236, 64, 52), 0.95f);
  gn_make_ring(&g_shArt.dot, q * 0.15f, q * 0.15f, (float)SS_(3), RGB(110, 226, 150), 0.80f);
  gn_make_ring(&g_shArt.dotHi, q * 0.22f, q * 0.22f, (float)SS_(5), RGB(140, 240, 170), 0.85f);
  gn_make_ring(&g_shArt.flash, D / 2.0f + SS_(10), (float)SS_(4), (float)SS_(10), RGB(255, 222, 120), 1.0f);
  gn_render_board(&g_shArt.board, q, SS_(SH_FRAME), sh_my_color() < 0);
}

/* доска собрана для другой стороны (поворот меняет рисунок полей) */
static int g_shArtFlip = -1;

static void sh_blit_c(UiCanvas *c, const GnSprite *s, float cx, float cy, int alpha) {
  gn_blit(c, s, (int)floorf(cx - s->w / 2.0f + 0.5f), (int)floorf(cy - s->h / 2.0f + 0.5f), alpha);
}

/* шашка на поле с центром (cx, cy): верх чуть выше центра — под ним
   боковина; lift — насколько приподнята */
static void sh_draw_piece(UiCanvas *c, int v, float cx, float cy, int alpha, float lift) {
  int k = (v > 0 ? 0 : 1) + (sh_isking(v) ? 2 : 0);
  const GnSprite *p = &g_shArt.piece[k];
  cy -= g_shArt.thick / 2.0f;
  if (lift > 0.5f) sh_blit_c(c, &g_shArt.lift, cx + SS_(3), cy + SS_(5) + g_shArt.thick, alpha * 4 / 5);
  else sh_blit_c(c, &g_shArt.shadow, cx + SS_(1), cy + SS_(3) + g_shArt.thick / 2.0f, alpha);
  gn_blit(c, p, (int)floorf(cx - p->w / 2.0f + 0.5f), (int)floorf(cy - lift - (g_shArt.pieceD / 2.0f + 1) + 0.5f), alpha);
}

static float sh_ease(float t) { return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); }

static void sh_paint_board(HDC hdc) {
  int flip = sh_my_color() < 0;
  if (g_shArtFlip != flip) {
    g_shArt.q = 0; /* поворот — рисунок полей другой */
    g_shArtFlip = flip;
  }
  sh_art_build();
  int q = SS_(SH_SQ), bx = sh_bx(), by = sh_by(), bp = sh_board_px(), fr = SS_(SH_FRAME);
  RECT box = {bx - fr - SS_(10), by - fr - SS_(6), bx + bp + fr + SS_(10), by + bp + fr + SS_(14)};
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, hdc, box, box.left, box.top, TRUE)) return;
  /* тень доски на окне и сама доска */
  {
    for (int i = 0; i < 6; i++) {
      RECT r = {bx - fr + SS_(2) - i, by - fr + SS_(4) - i / 2, bx + bp + fr + SS_(2) + i, by + bp + fr + SS_(6) + i};
      UiShape k;
      memset(&k, 0, sizeof(k));
      k.r = (float)SS_(8) + i;
      k.fill = RGB(0, 0, 0);
      k.fillA = 14;
      ui_shape(&cv, r, &k);
    }
    gn_blit(&cv, &g_shArt.board, bx - fr, by - fr, 255);
  }
  ULONGLONG nowT = GetTickCount64();
  BOOL anim = g_shAnim.on;
  int at = anim ? sh_anim_now() : 0;
  /* последний ход: тёплая заливка полей «откуда» и «куда», взятые — розоватые */
  if (!anim && g_sh.nlast >= 2 && g_sh.npart == 0) {
    int x, y;
    sh_sq_xy(g_sh.last.path[0], &x, &y);
    RECT a = {x, y, x + q, y + q};
    gn_tint(&cv, a, RGB(255, 196, 70), 70);
    sh_sq_xy(g_sh.last.path[g_sh.last.npath - 1], &x, &y);
    RECT b = {x, y, x + q, y + q};
    gn_tint(&cv, b, RGB(255, 196, 70), 105);
    for (int i = 0; i < g_sh.last.ncap; i++) {
      sh_sq_xy(g_sh.last.cap[i], &x, &y);
      RECT cr = {x, y, x + q, y + q};
      gn_tint(&cv, cr, RGB(220, 70, 60), 40);
    }
  }
  /* какую доску показывать: посреди хода — до хода; посреди моего взятия —
     шашка в конце пройденного пути, уже взятые бледные */
  ShBoard b = anim ? g_shAnim.before : g_sh.board;
  int ghost[64];
  for (int i = 0; i < 64; i++) ghost[i] = 255;
  int movedTo = -1, mover = 0;
  float mvx = 0, mvy = 0, mlift = 0;
  int flashA = 0;
  if (anim) {
    const ShMove *m = &g_shAnim.m;
    mover = b.sq[m->path[0]];
    b.sq[m->path[0]] = 0;
    int movesMs = sh_anim_moves_ms(&g_shAnim);
    /* где шашка: скачок h, доля f */
    int t = at, h = g_shAnim.startHop;
    float f = 1;
    while (h + 1 < m->npath) {
      if (t < g_shAnim.hopMs[h]) {
        f = (float)t / g_shAnim.hopMs[h];
        break;
      }
      t -= g_shAnim.hopMs[h];
      h++;
    }
    if (h + 1 >= m->npath) {
      h = m->npath - 2;
      f = 1;
    }
    float ax, ay, zx, zy;
    sh_sq_center(m->path[h], &ax, &ay);
    sh_sq_center(m->path[h + 1], &zx, &zy);
    float e = sh_ease(f);
    mvx = ax + (zx - ax) * e;
    mvy = ay + (zy - ay) * e;
    mlift = sinf(3.14159f * f) * q * (m->ncap ? 0.22f : 0.12f);
    /* дамкой становится, встав на последний ряд (посреди взятия — тоже) */
    int color = mover > 0 ? 1 : -1;
    if (!sh_isking(mover))
      for (int k = 1; k <= h + (f >= 1 ? 1 : 0) && k < m->npath; k++)
        if (sh_row(m->path[k]) == sh_last_row(color)) mover = color * 2;
    /* взятые: до начала показа — бледные, перепрыгнули — бледнеют; ход
       кончен — тают */
    int tEnd = 0;
    for (int k = 0; k < m->ncap; k++) {
      int a;
      if (k < g_shAnim.startHop) a = 100;
      else {
        tEnd = 0;
        for (int j = g_shAnim.startHop; j <= k && j + 1 < m->npath; j++) tEnd += g_shAnim.hopMs[j];
        a = at < tEnd - g_shAnim.hopMs[k] / 3 ? 255 : 100;
      }
      if (!g_shAnim.partial && at > movesMs) {
        int ft = at - movesMs;
        a = ft >= SH_FADE_MS ? 0 : 100 * (SH_FADE_MS - ft) / SH_FADE_MS;
      }
      ghost[m->cap[k]] = a;
    }
    int crownT = at - movesMs - (m->ncap ? SH_FADE_MS : 0);
    if (!g_shAnim.partial && m->promote && crownT >= 0) {
      float ft = (float)crownT / SH_CROWN_MS;
      flashA = ft < 1 ? (int)(255 * (1 - ft) * (ft < 0.15f ? ft / 0.15f : 1)) : 0;
    }
    movedTo = m->path[m->npath - 1];
  } else if (g_sh.npart > 1) {
    int v = b.sq[g_sh.part[0]];
    for (int i = 1; i < g_sh.npart; i++) {
      int a = g_sh.part[i - 1], z = g_sh.part[i];
      int dr = sh_row(z) > sh_row(a) ? 1 : -1, dc = sh_col(z) > sh_col(a) ? 1 : -1;
      for (int r = sh_row(a) + dr, c = sh_col(a) + dc; r != sh_row(z); r += dr, c += dc)
        if (b.sq[r * 8 + c]) ghost[r * 8 + c] = 100;
    }
    b.sq[g_sh.part[0]] = 0;
    movedTo = g_sh.part[g_sh.npart - 1];
    /* посреди взятия встала на последний ряд — уже дамка */
    if (!sh_isking(v))
      for (int i = 1; i < g_sh.npart; i++)
        if (sh_row(g_sh.part[i]) == sh_last_row(v > 0 ? 1 : -1)) v = v > 0 ? 2 : -2;
    b.sq[movedTo] = (signed char)v;
  }
  /* подсказки моего хода */
  static ShMove mv[SH_MAXMOVES], cand[SH_MAXMOVES];
  int nmv = 0, ncand = 0;
  BOOL my = sh_my_turn() && !anim;
  if (my) {
    nmv = sh_legal(&g_sh.board, sh_my_color(), mv, SH_MAXMOVES);
    if (g_sh.npart) ncand = sh_candidates(mv, nmv, cand);
  }
  int selSq = g_sh.npart ? (g_sh.npart > 1 ? movedTo : g_sh.part[0]) : -1;
  float pulse = 0.72f + 0.28f * sinf((float)(nowT % 1400) / 1400.0f * 6.2832f);
  /* шашки, которых бьём следующим скачком — красное кольцо под ними */
  BOOL capNext[64];
  memset(capNext, 0, sizeof(capNext));
  if (my && g_sh.npart)
    for (int i = 0; i < ncand; i++)
      if (cand[i].npath > g_sh.npart && cand[i].ncap >= g_sh.npart) capNext[cand[i].cap[g_sh.npart - 1]] = TRUE;
  /* шашки — сверху вниз по экрану, чтобы тени ложились правильно */
  for (int sr = 0; sr < 8; sr++)
    for (int sc = 0; sc < 8; sc++) {
      int c = flip ? 7 - sc : sc, r = flip ? sr : 7 - sr, s = r * 8 + c;
      if (!b.sq[s] || ghost[s] <= 0) continue;
      float cx, cy;
      sh_sq_center(s, &cx, &cy);
      BOOL movable = FALSE;
      if (my && g_sh.npart == 0)
        for (int i = 0; i < nmv && !movable; i++) movable = mv[i].path[0] == s;
      float top = cy - g_shArt.thick / 2.0f;
      if (capNext[s]) sh_blit_c(&cv, &g_shArt.cap, cx, top, (int)(200 + 55 * pulse));
      BOOL sel = s == selSq;
      sh_draw_piece(&cv, b.sq[s], cx, cy, ghost[s], sel ? (float)SS_(4) : 0);
      if (sel) sh_blit_c(&cv, &g_shArt.sel, cx, top - SS_(4), (int)(255 * pulse));
      else if (movable) sh_blit_c(&cv, &g_shArt.can, cx, top, s == g_shHover ? 255 : 170);
    }
  /* куда можно */
  if (my && g_sh.npart)
    for (int i = 0; i < ncand; i++)
      if (cand[i].npath > g_sh.npart) {
        int s = cand[i].path[g_sh.npart];
        float cx, cy;
        sh_sq_center(s, &cx, &cy);
        sh_blit_c(&cv, s == g_shHover ? &g_shArt.dotHi : &g_shArt.dot, cx, cy, 255);
      }
  /* едущая шашка — поверх всех */
  if (anim) {
    sh_draw_piece(&cv, mover, mvx, mvy, 255, mlift);
    if (flashA > 0) sh_blit_c(&cv, &g_shArt.flash, mvx, mvy - g_shArt.thick / 2.0f, flashA);
  }
  /* буквы и цифры по краям */
  if (g_fontSmall) SelectObject(cv.dc, g_fontSmall);
  SetBkMode(cv.dc, TRANSPARENT);
  for (int i = 0; i < 8; i++) {
    wchar_t l[2] = {(wchar_t)(L'a' + (flip ? 7 - i : i)), 0}, d[2] = {(wchar_t)(L'1' + (flip ? i : 7 - i)), 0};
    RECT rl = {bx + i * q, by + bp, bx + (i + 1) * q, by + bp + fr};
    RECT rd = {bx - fr, by + i * q, bx, by + (i + 1) * q};
    RECT sl = rl, sd = rd;
    OffsetRect(&sl, 0, 1);
    OffsetRect(&sd, 0, 1);
    SetTextColor(cv.dc, RGB(40, 22, 10));
    DrawTextW(cv.dc, l, -1, &sl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(cv.dc, d, -1, &sd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(cv.dc, RGB(236, 210, 160));
    DrawTextW(cv.dc, l, -1, &rl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(cv.dc, d, -1, &rd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  /* итог партии — полосой поперёк доски */
  if (g_shn.mode == GN_OVER && !anim) {
    RECT band = {bx, by + bp / 2 - SS_(46), bx + bp, by + bp / 2 + SS_(46)};
    gn_tint(&cv, band, RGB(16, 12, 10), 175);
    RECT l1 = {band.left, band.top, band.right, band.top + SS_(2)}, l2 = {band.left, band.bottom - SS_(2), band.right, band.bottom};
    gn_tint(&cv, l1, RGB(214, 170, 88), 200);
    gn_tint(&cv, l2, RGB(214, 170, 88), 200);
    if (!g_shBig) g_shBig = CreateFontW(-SS_(28), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    const wchar_t *big = g_sh.draw || g_sh.winner < 0 ? (g_sh.draw ? L"Ничья" : L"Партия остановлена")
                         : g_sh.winner == g_shn.me ? L"Победа!" : L"Поражение";
    if (g_shBig) SelectObject(cv.dc, g_shBig);
    SetTextColor(cv.dc, g_sh.winner == g_shn.me && !g_sh.draw ? RGB(255, 214, 110) : RGB(240, 232, 220));
    RECT t1 = {band.left, band.top + SS_(8), band.right, band.top + SS_(50)};
    DrawTextW(cv.dc, big, -1, &t1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (g_fontSmall) SelectObject(cv.dc, g_fontSmall);
    SetTextColor(cv.dc, RGB(220, 206, 186));
    RECT t2 = {band.left + SS_(10), band.top + SS_(52), band.right - SS_(10), band.bottom - SS_(6)};
    const wchar_t *why = g_sh.resigned ? (g_sh.winner == g_shn.me ? L"соперник сдался" : L"вы сдались") : g_sh.endWhy;
    DrawTextW(cv.dc, why, -1, &t2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  ui_canvas_end(&cv, hdc);
  /* анимация кончилась — доска как есть */
  if (anim && at >= sh_anim_total(&g_shAnim)) g_shAnim.on = FALSE;
}

/* табло под доской: чей ход, кто чем играет и сколько шашек */
static void sh_paint_score(HDC hdc, RECT rc) {
  int pad = SS_(14), y0 = SS_(SH_TOP) + sh_board_px() + SS_(SH_FRAME) * 2 + SS_(6), h = SS_(30);
  RECT box = {pad, y0, rc.right - pad, y0 + h};
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, hdc, box, box.left, box.top, TRUE)) return;
  int mc = sh_my_color(), md = g_shArt.miniD;
  BOOL mine = sh_my_turn(), over = g_shn.mode == GN_OVER;
  BOOL oppTurn = g_shn.mode == GN_PLAY && !mine;
  int cy = y0 + h / 2;
  gn_blit(&cv, &g_shArt.mini[mc > 0 ? 0 : 1], pad + SS_(2), cy - md / 2 - 1, 255);
  int ox = rc.right - pad - md - SS_(2);
  gn_blit(&cv, &g_shArt.mini[mc > 0 ? 1 : 0], ox, cy - md / 2 - 1, 255);
  /* ход — плашкой посередине */
  const wchar_t *pill = over ? L"Партия окончена" : mine ? L"Ваш ход" : L"Ход соперника";
  HFONT f = g_fontUi ? g_fontUi : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  SelectObject(cv.dc, f);
  SIZE ts;
  GetTextExtentPoint32W(cv.dc, pill, (int)wcslen(pill), &ts);
  int pw = ts.cx + SS_(26), ph = SS_(24);
  RECT pr = {(rc.left + rc.right) / 2 - pw / 2, cy - ph / 2, (rc.left + rc.right) / 2 + pw / 2, cy + ph / 2};
  UiShape k;
  memset(&k, 0, sizeof(k));
  k.r = -1;
  if (mine) {
    k.fill = COL_SAGE;
    k.fillA = 255;
  } else {
    k.fill = COL_PAPER_DARK;
    k.fillA = 255;
    k.border = COL_LINE;
    k.borderA = 255;
    k.bw = 1;
  }
  ui_shape(&cv, pr, &k);
  SetBkMode(cv.dc, TRANSPARENT);
  SetTextColor(cv.dc, mine ? RGB(255, 255, 255) : COL_MUTED);
  DrawTextW(cv.dc, pill, -1, &pr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  /* подписи по бокам */
  wchar_t me[80], op[180];
  _snwprintf(me, 80, L"Вы · %d", sh_count(&g_sh.board, mc));
  _snwprintf(op, 180, L"%d · %s", sh_count(&g_sh.board, -mc), g_shn.oppName);
  op[179] = 0;
  SetTextColor(cv.dc, COL_INK);
  RECT ml = {pad + md + SS_(10), y0, pr.left - SS_(6), y0 + h};
  DrawTextW(cv.dc, me, -1, &ml, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SetTextColor(cv.dc, oppTurn ? COL_INK : COL_MUTED);
  RECT orr = {pr.right + SS_(6), y0, ox - SS_(8), y0 + h};
  DrawTextW(cv.dc, op, -1, &orr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  ui_canvas_end(&cv, hdc);
}

/* ---- окно ------------------------------------------------------------------- */

static void sh_layout(void) {
  if (!g_shWnd) return;
  RECT rc;
  GetClientRect(g_shWnd, &rc);
  place_panel_close(g_shWnd);
  gn_layout_lobby(&g_shn, g_nd.poll);
  int m = g_shn.mode, pad = SS_(14), bh = SS_(28);
  int by = SS_(SH_TOP) + sh_board_px() + SS_(SH_FRAME) * 2 + SS_(66); /* под табло и строкой хода */
  HWND draw = GetDlgItem(g_shWnd, ID_SH_DRAW), drawok = GetDlgItem(g_shWnd, ID_SH_DRAWOK),
       resign = GetDlgItem(g_shWnd, ID_SH_RESIGN), back = GetDlgItem(g_shWnd, ID_SH_BACK);
  BOOL play = m == GN_PLAY, over = m == GN_OVER;
  ShowWindow(draw, play && !g_sh.iOffered && !g_sh.oppOffersDraw ? SW_SHOW : SW_HIDE);
  ShowWindow(drawok, play && g_sh.oppOffersDraw ? SW_SHOW : SW_HIDE);
  ShowWindow(resign, play ? SW_SHOW : SW_HIDE);
  ShowWindow(back, over ? SW_SHOW : SW_HIDE);
  if (play) {
    MoveWindow(draw, pad, by, SS_(170), bh, TRUE);
    MoveWindow(drawok, pad, by, SS_(200), bh, TRUE);
    MoveWindow(resign, rc.right - pad - SS_(100), by, SS_(100), bh, TRUE);
  }
  if (over) MoveWindow(back, pad, by, SS_(160), bh, TRUE);
}

static void sh_mark_btn(void) { gn_mark_btn(&g_shn, g_btnSh, L"Шашки", sh_my_turn()); }

static void sh_refresh_view(void) {
  sh_mark_btn();
  if (!g_shWnd) return;
  if (g_shn.mode == GN_LOBBY) gn_list_sync(&g_shn, GetDlgItem(g_shWnd, ID_SH_LIST), g_nd.poll);
  sh_layout();
  InvalidateRect(g_shWnd, NULL, FALSE);
}

/* список «кто в сети» у нард обновился — и у шашек */
static void shashki_online_changed(void) {
  if (g_shn.mode == GN_PLAY) sh_turn_status(); /* соперник пропал из сети или вернулся */
  if (g_shWnd && IsWindowVisible(g_shWnd) && (g_shn.mode == GN_LOBBY || g_shn.mode == GN_PLAY)) sh_refresh_view();
  else sh_mark_btn();
}

static void sh_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Шашки");
  if (g_shn.mode == GN_LOBBY) {
    gn_paint_lobby(&g_shn, hdc, rc, g_nd.poll,
                   L"Русские шашки: бить обязательно, простая бьёт и назад, дамка ходит и бьёт на любое "
                   L"расстояние. Кто белые — выпадает случайно. Ход уходит сразу, как шашка дошла до места.");
    return;
  }
  if (g_shn.mode == GN_WAIT) {
    gn_paint_wait(&g_shn, hdc, rc);
    return;
  }
  sh_paint_board(hdc);
  sh_paint_score(hdc, rc);
  int pad = SS_(14), yb = SS_(SH_TOP) + sh_board_px() + SS_(SH_FRAME) * 2;
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  RECT st = {pad, yb + SS_(38), rc.right - pad, yb + SS_(60)};
  SetTextColor(hdc, g_shn.mode == GN_OVER ? COL_SAGE : COL_INK);
  DrawTextW(hdc, g_shn.status, -1, &st, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  wchar_t dg[400];
  gn_diag_text(&g_shn, dg, 400);
  RECT dr = {pad, yb + SS_(100), rc.right - pad, rc.bottom - SS_(4)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, g_shn.writeFail ? RGB(200, 60, 40) : COL_MUTED);
  DrawTextW(hdc, dg, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

/* под мышью поле, по которому есть смысл щёлкнуть */
static BOOL sh_clickable(int s) {
  if (s < 0 || !sh_my_turn()) return FALSE;
  static ShMove mv[SH_MAXMOVES], cand[SH_MAXMOVES];
  int n = sh_legal(&g_sh.board, sh_my_color(), mv, SH_MAXMOVES);
  if (g_sh.npart <= 1)
    for (int i = 0; i < n; i++)
      if (mv[i].path[0] == s) return TRUE;
  if (g_sh.npart) {
    int nc = sh_candidates(mv, n, cand);
    for (int i = 0; i < nc; i++)
      if (cand[i].npath > g_sh.npart && cand[i].path[g_sh.npart] == s) return TRUE;
  }
  return FALSE;
}

static LRESULT CALLBACK ShashkiProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    sh_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_TIMER:
    if (wParam == SH_TIMER) {
      /* таймер живёт, пока что-то движется: ход едет или выбранная пульсирует */
      BOOL pulse = g_sh.npart && sh_my_turn() && GetForegroundWindow() == hwnd;
      if (!g_shAnim.on && !pulse) KillTimer(hwnd, SH_TIMER);
      else if (!g_shAnim.on) SetTimer(hwnd, SH_TIMER, 40, NULL);
      if (g_shn.mode == GN_PLAY || g_shn.mode == GN_OVER) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT bd = {0, SS_(SH_TOP) - SS_(6), rc.right, SS_(SH_TOP) + sh_board_px() + SS_(SH_FRAME) * 2 + SS_(14)};
        InvalidateRect(hwnd, &bd, FALSE);
      }
    }
    return 0;
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_LBUTTONDOWN:
    if (g_shn.mode == GN_PLAY) sh_click_square(sh_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
    return 0;
  case WM_MOUSEMOVE: {
    int s = g_shn.mode == GN_PLAY ? sh_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) : -1;
    if (s != g_shHover) {
      g_shHover = s;
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&tme);
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  }
  case WM_MOUSELEAVE:
    if (g_shHover >= 0) {
      g_shHover = -1;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT && sh_clickable(g_shHover)) {
      SetCursor(LoadCursorW(NULL, IDC_HAND));
      return TRUE;
    }
    break;
  case WM_RBUTTONDOWN:
    /* правая — снять выбор (посреди взятия — начать ход заново) */
    g_sh.npart = 0;
    g_shAnim.on = FALSE;
    sh_turn_status();
    sh_refresh_view();
    return 0;
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
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
    /* у кнопок — только щелчок: двойной щелчок присылает ещё и
       BN_DOUBLECLICKED, и «Позвать играть» уходило дважды */
    if (id != ID_SH_LIST && HIWORD(wParam) != BN_CLICKED) return 0;
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (gn_command(&g_shn, id, HIWORD(wParam))) {
      if (id == ID_SH_LIST) return 0; /* фокус у списка не отнимаем */
    }
    if (id == ID_SH_DRAW) sh_offer_draw();
    if (id == ID_SH_DRAWOK) sh_accept_draw();
    if (id == ID_SH_RESIGN) sh_resign();
    if (id == ID_SH_BACK) gn_back_to_lobby(&g_shn);
    SetFocus(hwnd);
    return 0;
  }
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void shashki_start(void) {
  if (g_shn.thread) return;
  wchar_t *t = gn_load_local(&g_shn);
  if (t) {
    g_sh.white = gn_game_bit(g_shn.game);
    sh_start(&g_sh.board);
    g_sh.ply = 1;
    free(t);
  }
  gn_start(&g_shn);
}

static void shashki_show(void) {
  if (!g_shWnd) {
    HDC s = GetDC(NULL);
    g_shS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    g_shn.sc = g_shS;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ShashkiProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadShashki";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    int w = SS_(SH_LEFT * 2 + SH_SQ * 8 + SH_FRAME * 2), h = SS_(SH_TOP + SH_SQ * 8 + SH_FRAME * 2 + 140);
    g_shWnd = CreateWindowExW(WS_EX_APPWINDOW, L"CursorPadShashki", L"Шашки",
                              WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU, 0, 0, w, h, NULL, NULL,
                              g_inst, NULL);
    if (!g_shWnd) return;
    round_corners(g_shWnd);
    mk_btn(g_shWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_shWnd, L"–", ID_PANEL_MIN);
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0,
                                100, 100, g_shWnd, (HMENU)(INT_PTR)ID_SH_LIST, g_inst, NULL);
    const struct {
      const wchar_t *t;
      int id;
    } btns[] = {{L"Позвать играть", ID_SH_INVITE},  {L"Принять", ID_SH_ACCEPT},
                {L"Отказаться", ID_SH_DECLINE},     {L"Предложить ничью", ID_SH_DRAW},
                {L"Согласиться на ничью", ID_SH_DRAWOK}, {L"Сдаться", ID_SH_RESIGN},
                {L"К списку игроков", ID_SH_BACK},  {L"Отменить приглашение", ID_SH_CANCEL}};
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
      HWND b = mk_btn(g_shWnd, btns[i].t, btns[i].id);
      if (g_fontUi) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    }
    if (g_fontBody) SendMessageW(list, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_shWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                 SWP_NOZORDER);
  }
  if (g_shn.mode == GN_PLAY) sh_turn_status();
  sh_refresh_view();
  ShowWindow(g_shWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_shWnd);
  SetFocus(g_shWnd);
}
