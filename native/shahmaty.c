/* ---- Шахматы по сети через общую папку (с 2026.10.23) ----------------------

   Как шашки, нарды и морской бой: двое, у кого в Настройках одна общая
   папка; кто в сети — тот же список. Связь — общая (game_net.c), свои файлы —
   в <папка>\CursorPad-Shahmaty. Правила — shahmaty_rules.c, фигуры —
   shahmaty_art.c.

   Строки своего файла партии, кроме общих (name, id, decline, cancel, resign):
     m\t<полуход>\t<ход>   — ход как в UCI: «e2e4», «e1g1», «e7e8q»
     draw\t<полуход>       — предлагаю ничью
     drawok\t<полуход>     — согласен на ничью
   Предложение ничьей действует, пока соперник не сходил: сделано в свой
   ход — ещё и на ход соперника. Кто белые — по номеру партии, обе
   программы считают одинаково. Каждый ход программа соперника проверяет
   по правилам; партия восстанавливается из файлов целиком. Сдаться можно
   в любой момент — партия кончается на той позиции, где сдались.

   Вид — как у шашек: деревянная доска с рамой, фигуры из кости и эбена.
   Ходить — щелчком (фигура, потом поле) или перетаскиванием. У выбранной
   фигуры — золотое поле, зелёные точки — куда можно, красное кольцо —
   кого можно взять; последний ход — тёплой заливкой, король под шахом —
   красным свечением. Пешка дошла до края — выбор фигуры прямо на доске.
   Ходы едут плавно, взятые тают. Справа — соперник и вы (взятые фигуры,
   перевес), запись партии по-русски и чей ход. */

#include "shahmaty_rules.c"
#include "shahmaty_art.c"

#define WM_SHAHMATY_POLL (WM_APP + 68) /* lParam — GnPoll*, освобождает получатель */
#define SX_TIMER 1                     /* анимация хода */
#define SX_MAXPLY 1200                 /* запись партии — полуходов */
/* ID_SX_* — в cursorpad.c */

typedef struct {
  int white; /* какая сторона (0/1) играет белыми */
  SxPos pos;
  int ply;    /* номер полухода на очереди, с 1: нечётный — белых */
  int winner; /* сторона-победитель; −1 — ничья или партия остановлена */
  BOOL draw, resigned;
  wchar_t endBig[40], endWhy[200];
  /* запись партии и взятые: taken[0] — что взяли белые (чёрные фигуры) */
  wchar_t san[SX_MAXPLY][12];
  int nsan;
  unsigned char taken[2][16];
  int ntaken[2];
  /* последний ход — подсветить; ходил соперник — показать движением */
  SxMove last;
  SxPos before;
  BOOL haveLast, lastOpp;
  /* мой ход: выбранная фигура, перетаскивание, выбор фигуры превращения */
  int sel;
  BOOL drag, dragMoved, selWas;
  int dragX, dragY, downX, downY;
  int promoFrom, promoTo;
  BOOL oppOffersDraw, iOffered;
  int notifiedPly;
  int scroll; /* запись партии: на сколько строк вверх прокручена */
} SxGame;

static SxGame g_sx = {.sel = -1, .promoFrom = -1, .promoTo = -1, .winner = -1};
static HWND g_sxWnd;
static float g_sxS = 1.0f;
static int g_sxHover = -1;

static void sx_fresh(void);
static void sx_refresh_view(void);
static void sx_turn_status(void);

static GnNet g_sxn = {
    .sub = L"CursorPad-Shahmaty",
    .local = L"shahmaty",
    .title = L"Шахматы",
    .invText = L"зовёт сыграть в шахматы",
    .moveKey = L"m",
    .msg = WM_SHAHMATY_POLL,
    .balloon = 8,
    .wnd = &g_sxWnd,
    .sc = 1.0f,
    .idList = ID_SX_LIST,
    .idInvite = ID_SX_INVITE,
    .idAccept = ID_SX_ACCEPT,
    .idDecline = ID_SX_DECLINE,
    .idCancel = ID_SX_CANCEL,
    .fresh = sx_fresh,
    .view = sx_refresh_view,
    .turn = sx_turn_status,
};

#define sx_status(...) gn_status(&g_sxn, __VA_ARGS__)

static int XS_(int v) { return (int)(v * g_sxS + 0.5f); }

static int sx_color_of_side(int side) { return side == g_sx.white ? 1 : -1; }
static int sx_side_of_ply(int ply) { return (ply % 2 == 1) ? g_sx.white : 1 - g_sx.white; }
static int sx_my_color(void) { return sx_color_of_side(g_sxn.me); }
static BOOL sx_my_turn(void) { return g_sxn.mode == GN_PLAY && sx_side_of_ply(g_sx.ply) == g_sxn.me; }

/* ---- движение на доске ------------------------------------------------------ */

/* Анимация хода: позиция до хода и сам ход. slide — фигура едет с поля на
   поле; FALSE — её уже донесли мышью, только тает взятая. */
typedef struct {
  BOOL on, slide;
  ULONGLONG t0;
  SxPos before;
  SxMove m;
  int slideMs;
} SxAnim;

static SxAnim g_sxAnim;

#define SX_FADE_MS 200
#define SX_FLASH_MS 420

static int sx_anim_total(const SxAnim *an) {
  int t = an->slide ? an->slideMs : 0, tail = 0;
  if (an->m.flags & SXF_CAP) tail = SX_FADE_MS;
  if (an->m.promo && SX_FLASH_MS > tail) tail = SX_FLASH_MS;
  return t + tail;
}

static void sx_anim_start(const SxPos *before, const SxMove *m, BOOL slide) {
  if (!g_sxWnd || !IsWindowVisible(g_sxWnd) || IsIconic(g_sxWnd)) {
    g_sxAnim.on = FALSE;
    return;
  }
  memset(&g_sxAnim, 0, sizeof(g_sxAnim));
  g_sxAnim.before = *before;
  g_sxAnim.m = *m;
  g_sxAnim.slide = slide;
  int dr = abs(sx_row(m->to) - sx_row(m->from)), dc = abs(sx_col(m->to) - sx_col(m->from));
  int d = dr > dc ? dr : dc;
  g_sxAnim.slideMs = 170 + (d > 1 ? (d - 1) * 30 : 0);
  if (g_sxAnim.slideMs > 330) g_sxAnim.slideMs = 330;
  g_sxAnim.t0 = GetTickCount64();
  g_sxAnim.on = sx_anim_total(&g_sxAnim) > 0;
  if (g_sxAnim.on) SetTimer(g_sxWnd, SX_TIMER, 15, NULL);
}

static int sx_anim_now(void) { return (int)(GetTickCount64() - g_sxAnim.t0); }

/* ---- ход партии по файлам ------------------------------------------------- */

static void sx_over(int winner, const wchar_t *big, const wchar_t *why) {
  g_sxn.mode = GN_OVER;
  g_sx.winner = winner;
  lstrcpynW(g_sx.endBig, big ? big : L"", 40);
  lstrcpynW(g_sx.endWhy, why ? why : L"", 200);
  gn_save_local(&g_sxn);
}

/* предложение ничьей стороны side действует на полуходе ply: сделано на
   нём или на прошлом своём ходе (предложил и сходил) */
static BOOL sx_offer_active(const wchar_t *file, int side, int ply) {
  if (gn_has_num(file, L"draw", ply)) return TRUE;
  return ply > 1 && sx_side_of_ply(ply - 1) == side && gn_has_num(file, L"draw", ply - 1);
}

/* Разыграть партию с начала по обоим файлам (свой — из памяти: только что
   сделанный ход папка могла ещё не вернуть). */
static void sx_replay(const wchar_t *p0, const wchar_t *p1) {
  GnNet *n = &g_sxn;
  const wchar_t *mine = gn_mine(n), *theirs = gn_theirs(n, p0, p1);
  const wchar_t *file[2];
  file[n->me] = mine;
  file[1 - n->me] = theirs;
  if (!gn_replay_head(n, theirs)) return;
  g_sx.white = gn_game_bit(n->game);
  if (!gn_accepted(file[1])) { /* принявший ещё не ответил */
    n->mode = GN_WAIT;
    gn_wait_status(n);
    return;
  }
  if (n->mode == GN_WAIT) n->mode = GN_PLAY;
  SxPos pos;
  sx_start(&pos);
  unsigned long long keys[128]; /* позиции после последнего взятия или хода пешкой — для повторений */
  g_sx.nsan = 0;
  g_sx.ntaken[0] = g_sx.ntaken[1] = 0;
  g_sx.haveLast = FALSE;
  g_sx.oppOffersDraw = g_sx.iOffered = FALSE;
  g_sx.draw = g_sx.resigned = FALSE;
  static SxMove mv[SX_MAXMOVES];
  for (int ply = 1;; ply++) {
    g_sx.pos = pos;
    g_sx.ply = ply;
    int side = sx_side_of_ply(ply), opp = 1 - n->me;
    /* ничья по согласию: один предложил, другой согласился на этом полуходе */
    for (int s = 0; s < 2; s++)
      if (sx_offer_active(file[s], s, ply) && gn_has_num(file[1 - s], L"drawok", ply)) {
        g_sx.draw = TRUE;
        sx_over(-1, L"Ничья", L"по согласию");
        return;
      }
    int cnt = sx_legal(&pos, mv, SX_MAXMOVES);
    if (cnt == 0) {
      if (sx_in_check(&pos, pos.side)) sx_over(1 - side, NULL, L"мат");
      else {
        g_sx.draw = TRUE;
        sx_over(-1, L"Пат", L"ходить некуда, шаха нет — ничья");
      }
      return;
    }
    if (sx_dead(&pos)) {
      g_sx.draw = TRUE;
      sx_over(-1, L"Ничья", L"мата не поставить — фигур не хватит");
      return;
    }
    unsigned long long key = sx_key(&pos, mv, cnt);
    keys[ply & 127] = key;
    int reps = 1;
    for (int k = ply - 2; k >= 1 && k >= ply - pos.half; k -= 2) reps += keys[k & 127] == key;
    if (reps >= 3) {
      g_sx.draw = TRUE;
      sx_over(-1, L"Ничья", L"позиция повторилась трижды");
      return;
    }
    if (pos.half >= 100) {
      g_sx.draw = TRUE;
      sx_over(-1, L"Ничья", L"50 ходов без взятий и ходов пешкой");
      return;
    }
    char t[16];
    if (!gn_num_line(file[side], L"m", ply, t, 16)) {
      /* этот ход ещё не сделан; сдались — партия кончена на этой позиции */
      if (nd_has_line(file[0], L"resign") || nd_has_line(file[1], L"resign")) {
        int loser = nd_has_line(file[0], L"resign") ? 0 : 1;
        g_sx.resigned = TRUE;
        sx_over(1 - loser, NULL, loser == n->me ? L"вы сдались" : L"соперник сдался");
        return;
      }
      g_sx.oppOffersDraw = sx_offer_active(theirs, opp, ply) && !gn_has_num(mine, L"drawok", ply);
      g_sx.iOffered = sx_offer_active(mine, n->me, ply);
      return;
    }
    int k = sx_find_uci(mv, cnt, t);
    if (k < 0) {
      wchar_t why[200];
      _snwprintf(why, 200, L"ход %d не прошёл проверку правил", (ply + 1) / 2);
      sx_over(-1, L"Партия остановлена", why);
      return;
    }
    if (g_sx.nsan < SX_MAXPLY) sx_san(&pos, &mv[k], mv, cnt, g_sx.san[g_sx.nsan++], 12);
    if (mv[k].flags & SXF_CAP) {
      int ci = pos.side > 0 ? 0 : 1, kind = (mv[k].flags & SXF_EP) ? SX_P : sx_kind(pos.sq[mv[k].to]);
      if (g_sx.ntaken[ci] < 16) g_sx.taken[ci][g_sx.ntaken[ci]++] = (unsigned char)kind;
    }
    g_sx.before = pos;
    sx_apply(&pos, &mv[k]);
    g_sx.last = mv[k];
    g_sx.haveLast = TRUE;
    g_sx.lastOpp = side != n->me;
  }
}

static void sx_turn_status(void) {
  if (g_sxn.mode != GN_PLAY) return;
  if (!sx_my_turn()) {
    if (g_sx.oppOffersDraw) sx_status(L"%s предлагает ничью — «Согласиться на ничью» или ждите его хода", g_sxn.oppName);
    else if (!gn_opp_online(&g_sxn))
      sx_status(L"Ходит %s — он сейчас не в сети: ход увидит, когда запустит CursorPad", g_sxn.oppName);
    else sx_status(L"Ходит %s…%s", g_sxn.oppName, g_sx.iOffered ? L" (вы предложили ничью)" : L"");
    return;
  }
  BOOL check = sx_in_check(&g_sx.pos, g_sx.pos.side);
  const wchar_t *off = g_sx.iOffered ? L" (вы предложили ничью)" : L"";
  if (g_sx.promoFrom >= 0) sx_status(L"Во что превратить пешку? Выберите фигуру на доске");
  else if (g_sx.oppOffersDraw) sx_status(L"%s предлагает ничью — «Согласиться на ничью» или просто ходите", g_sxn.oppName);
  else if (g_sx.sel >= 0) sx_status(L"Куда? Отмечены поля, куда можно%s%s", check ? L" — вам шах" : L"", off);
  else if (check) sx_status(L"Вам шах! Ваш ход — закройтесь, уйдите или возьмите%s", off);
  else sx_status(L"Ваш ход — выберите фигуру%s", off);
}

static void sx_after_replay(int prevPly, int prevMode) {
  GnNet *n = &g_sxn;
  if (prevMode == GN_WAIT && n->mode == GN_PLAY) gn_save_local(n); /* приняли — после перезапуска сразу игра */
  if (n->mode == GN_PLAY) {
    if (g_sx.ply != prevPly || prevMode != GN_PLAY) {
      g_sx.sel = g_sx.promoFrom = g_sx.promoTo = -1;
      g_sx.drag = FALSE;
      g_sx.scroll = 0;
    }
    sx_turn_status();
    if (sx_my_turn() && g_sx.notifiedPly != g_sx.ply && !gn_in_front(n)) {
      g_sx.notifiedPly = g_sx.ply;
      wchar_t t[200];
      if (g_sx.ply <= 2)
        _snwprintf(t, 200, L"Партия с %s: вы играете %s — ваш ход", n->oppName, sx_my_color() > 0 ? L"белыми" : L"чёрными");
      else
        _snwprintf(t, 200, L"%s: %s — %s", n->oppName, g_sx.nsan ? g_sx.san[g_sx.nsan - 1] : L"",
                   sx_in_check(&g_sx.pos, g_sx.pos.side) ? L"вам шах, ваш ход" : L"ваш ход");
      t[199] = 0;
      gn_balloon(n, t);
    }
  }
  if (n->mode == GN_OVER && prevMode != GN_OVER) {
    wchar_t t[240];
    if (g_sx.winner == n->me) _snwprintf(t, 240, L"Вы выиграли у %s — %s", n->oppName, g_sx.endWhy);
    else if (g_sx.winner >= 0) _snwprintf(t, 240, L"Выиграл %s — %s", n->oppName, g_sx.endWhy);
    else _snwprintf(t, 240, L"%s — %s", g_sx.endBig, g_sx.endWhy);
    t[239] = 0;
    sx_status(L"%s", t);
    if (!gn_in_front(n)) gn_balloon(n, t);
  }
  sx_refresh_view();
}

/* разыграть заново по последнему опросу (после своего хода, сдачи…) */
static void sx_replay_now(void) {
  int prevPly = g_sx.ply, prevMode = g_sxn.mode;
  GnPoll *pl = g_sxn.poll;
  sx_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  sx_after_replay(prevPly, prevMode);
}

static void sx_on_poll(GnPoll *pl) {
  int prevPly = g_sx.ply, prevMode = g_sxn.mode;
  if (!gn_on_poll(&g_sxn, pl)) return;
  sx_replay(pl->p0, pl->p1);
  /* соперник сходил — показать его ход движением */
  if (prevMode == GN_PLAY && g_sxn.mode != GN_LOBBY && g_sx.haveLast && g_sx.lastOpp && g_sx.ply == prevPly + 1)
    sx_anim_start(&g_sx.before, &g_sx.last, TRUE);
  sx_after_replay(prevPly, prevMode);
}

/* ---- действия игрока -------------------------------------------------------- */

/* новая партия (позвал или принял): начальная позиция */
static void sx_fresh(void) {
  g_sx.white = gn_game_bit(g_sxn.game);
  sx_start(&g_sx.pos);
  g_sx.ply = 1;
  g_sx.winner = -1;
  g_sx.nsan = g_sx.ntaken[0] = g_sx.ntaken[1] = 0;
  g_sx.haveLast = FALSE;
  g_sx.sel = g_sx.promoFrom = g_sx.promoTo = -1;
  g_sx.drag = FALSE;
  g_sx.draw = g_sx.resigned = FALSE;
  g_sx.oppOffersDraw = g_sx.iOffered = FALSE;
  g_sx.scroll = 0;
  g_sxAnim.on = FALSE;
}

/* мой ход сделан — в файл и разыграть заново; slide — показать движением */
static void sx_commit(const SxMove *m, BOOL slide) {
  char t[8];
  sx_uci(m, t);
  wchar_t w[8];
  MultiByteToWideChar(CP_UTF8, 0, t, -1, w, 8);
  SxPos before = g_sx.pos;
  sb_add(&g_sxn.log, L"m\t%d\t%s\n", g_sx.ply, w);
  g_sx.sel = g_sx.promoFrom = g_sx.promoTo = -1;
  if (!gn_write_mine(&g_sxn)) sx_status(L"Не удалось записать ход в общую папку — повторю сам");
  sx_replay_now();
  sx_anim_start(&before, m, slide);
  sx_refresh_view();
}

/* можно ли выбранной фигурой на поле to; promo — ход с превращением */
static int sx_find_move(int from, int to, int promo, SxMove *out) {
  static SxMove mv[SX_MAXMOVES];
  int n = sx_legal(&g_sx.pos, mv, SX_MAXMOVES), hit = 0;
  for (int i = 0; i < n; i++)
    if (mv[i].from == from && mv[i].to == to && (!mv[i].promo || !promo || mv[i].promo == promo)) {
      if (out) *out = mv[i];
      hit = 1 + (mv[i].promo != 0);
      if (!mv[i].promo || mv[i].promo == promo) break;
    }
  return hit; /* 0 — нельзя, 1 — можно, 2 — можно, но нужна фигура превращения */
}

static BOOL sx_has_moves_from(int s) {
  static SxMove mv[SX_MAXMOVES];
  int n = sx_legal(&g_sx.pos, mv, SX_MAXMOVES);
  for (int i = 0; i < n; i++)
    if (mv[i].from == s) return TRUE;
  return FALSE;
}

/* попробовать ход from→to: сразу, или сперва выбор фигуры превращения */
static BOOL sx_try_move(int from, int to, BOOL slide) {
  SxMove m;
  int r = sx_find_move(from, to, 0, &m);
  if (!r) return FALSE;
  if (r == 2) {
    g_sx.promoFrom = from;
    g_sx.promoTo = to;
    g_sx.drag = FALSE;
    sx_turn_status();
    sx_refresh_view();
    return TRUE;
  }
  sx_commit(&m, slide);
  return TRUE;
}

static void sx_resign(void) {
  if (!gn_resign(&g_sxn)) return;
  sx_replay_now();
}

static void sx_offer_draw(void) {
  if (g_sxn.mode != GN_PLAY || g_sx.iOffered) return;
  sb_add(&g_sxn.log, L"draw\t%d\n", g_sx.ply);
  gn_write_mine(&g_sxn);
  g_sx.iOffered = TRUE;
  sx_status(L"Ничья предложена — ждём ответа %s", g_sxn.oppName);
  sx_refresh_view();
}

static void sx_accept_draw(void) {
  if (g_sxn.mode != GN_PLAY || !g_sx.oppOffersDraw) return;
  sb_add(&g_sxn.log, L"drawok\t%d\n", g_sx.ply);
  gn_write_mine(&g_sxn);
  sx_replay_now();
}

/* ---- доска ------------------------------------------------------------------ */

#define SX_SQ 54    /* поле, логических точек */
#define SX_TOP 46   /* под шапкой окна */
#define SX_LEFT 14
#define SX_FRAME 20 /* рама с буквами и цифрами */
#define SX_SIDE 210 /* полоса справа: игроки, запись партии */

static int sx_q(void) { return XS_(SX_SQ); }
static int sx_board_px(void) { return sx_q() * 8; }
static int sx_bx(void) { return XS_(SX_LEFT) + XS_(SX_FRAME); }
static int sx_by(void) { return XS_(SX_TOP) + XS_(SX_FRAME); }
static BOOL sx_flip(void) { return sx_my_color() < 0; } /* свои фигуры всегда внизу */

static void sx_sq_xy(int s, int *x, int *y) {
  int r = sx_row(s), c = sx_col(s), q = sx_q();
  BOOL flip = sx_flip();
  *x = sx_bx() + (flip ? 7 - c : c) * q;
  *y = sx_by() + (flip ? r : 7 - r) * q;
}

static void sx_sq_center(int s, float *x, float *y) {
  int px, py, q = sx_q();
  sx_sq_xy(s, &px, &py);
  *x = px + q * 0.5f;
  *y = py + q * 0.5f;
}

static int sx_hit(int x, int y) {
  int q = sx_q(), bx = sx_bx(), by = sx_by();
  if (x < bx || y < by || x >= bx + 8 * q || y >= by + 8 * q) return -1;
  int sc = (x - bx) / q, sr = (y - by) / q;
  BOOL flip = sx_flip();
  return (flip ? sr : 7 - sr) * 8 + (flip ? 7 - sc : sc);
}

/* выбор фигуры превращения: столбец из четырёх полей от поля хода к центру
   доски; −1 — мимо, иначе SX_Q, SX_N, SX_R, SX_B */
static const int kSxPromo[4] = {SX_Q, SX_N, SX_R, SX_B};

static void sx_promo_rect(int i, RECT *r) {
  int x, y, q = sx_q();
  sx_sq_xy(g_sx.promoTo, &x, &y);
  int dy = y < sx_by() + 4 * q ? q : -q; /* поле хода сверху — столбец вниз */
  r->left = x;
  r->top = y + dy * i;
  r->right = x + q;
  r->bottom = r->top + q;
}

static int sx_promo_hit(int x, int y) {
  for (int i = 0; i < 4; i++) {
    RECT r;
    sx_promo_rect(i, &r);
    POINT p = {x, y};
    if (PtInRect(&r, p)) return kSxPromo[i];
  }
  return -1;
}

/* ---- спрайты ---------------------------------------------------------------- */

typedef struct {
  int q;
  GnSprite piece[2][7]; /* [чёрная][вид] */
  GnSprite mini[2][7];  /* маленькие — для взятых */
  GnSprite shadow, lift;
  GnSprite dot, dotHi, cap, check, flash;
  GnSprite board;
  int miniD;
} SxArt;

static SxArt g_sxArt;
static int g_sxArtFlip = -1;
static HFONT g_sxBig;

static void sx_art_build(void) {
  int q = sx_q();
  int flip = sx_flip();
  if (g_sxArtFlip != flip) {
    g_sxArt.q = 0; /* поворот — рисунок полей другой */
    g_sxArtFlip = flip;
  }
  if (g_sxArt.q == q && g_sxArt.board.px) return;
  g_sxArt.q = q;
  for (int b = 0; b < 2; b++)
    for (int k = SX_P; k <= SX_K; k++) sx_render_piece(&g_sxArt.piece[b][k], q, k, b);
  g_sxArt.miniD = XS_(22);
  for (int b = 0; b < 2; b++)
    for (int k = SX_P; k <= SX_K; k++) sx_render_piece(&g_sxArt.mini[b][k], g_sxArt.miniD, k, b);
  sx_make_shadow(&g_sxArt.shadow, (int)(q * 0.37f), (int)(q * 0.11f), 0.42f);
  sx_make_shadow(&g_sxArt.lift, (int)(q * 0.40f), (int)(q * 0.14f), 0.30f);
  gn_make_ring(&g_sxArt.dot, q * 0.14f, q * 0.14f, (float)XS_(3), RGB(110, 226, 150), 0.80f);
  gn_make_ring(&g_sxArt.dotHi, q * 0.20f, q * 0.20f, (float)XS_(5), RGB(140, 240, 170), 0.85f);
  gn_make_ring(&g_sxArt.cap, q * 0.46f, (float)XS_(3), (float)XS_(4), RGB(236, 64, 52), 0.90f);
  gn_make_glow(&g_sxArt.check, (int)(q * 0.62f), RGB(236, 40, 30), 0.85f, 0.25f);
  gn_make_ring(&g_sxArt.flash, q * 0.44f, (float)XS_(4), (float)XS_(10), RGB(255, 222, 120), 1.0f);
  gn_render_board(&g_sxArt.board, q, XS_(SX_FRAME), flip);
}

static void sx_blit_c(UiCanvas *c, const GnSprite *s, float cx, float cy, int alpha) {
  gn_blit(c, s, (int)floorf(cx - s->w / 2.0f + 0.5f), (int)floorf(cy - s->h / 2.0f + 0.5f), alpha);
}

/* фигура v на поле с центром (cx, cy); lift — насколько поднята (тащат) */
static void sx_draw_piece(UiCanvas *c, int v, float cx, float cy, int alpha, float lift) {
  if (!v) return;
  const GnSprite *p = &g_sxArt.piece[v < 0][sx_kind(v)];
  int q = g_sxArt.q;
  if (lift > 0.5f) sx_blit_c(c, &g_sxArt.lift, cx + q * 0.06f, cy + q * 0.40f, alpha * 3 / 4);
  else sx_blit_c(c, &g_sxArt.shadow, cx + q * 0.03f, cy + q * 0.38f, alpha);
  /* низ подставки (0.93 высоты фигуры) — у нижнего края поля */
  float top = cy + q * 0.43f - q * 0.934f - 2 - lift;
  gn_blit(c, p, (int)floorf(cx - p->w / 2.0f + 0.5f), (int)floorf(top + 0.5f), alpha);
}

static float sx_ease(float t) { return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); }

static void sx_paint_board(HDC hdc) {
  sx_art_build();
  BOOL flip = sx_flip();
  int q = sx_q(), bx = sx_bx(), by = sx_by(), bp = sx_board_px(), fr = XS_(SX_FRAME);
  RECT box = {bx - fr - XS_(10), by - fr - XS_(6), bx + bp + fr + XS_(10), by + bp + fr + XS_(12)};
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, hdc, box, box.left, box.top, TRUE)) return;
  for (int i = 0; i < 6; i++) { /* тень доски на окне */
    RECT r = {bx - fr + XS_(2) - i, by - fr + XS_(4) - i / 2, bx + bp + fr + XS_(2) + i, by + bp + fr + XS_(6) + i};
    UiShape k;
    memset(&k, 0, sizeof(k));
    k.r = (float)XS_(8) + i;
    k.fill = RGB(0, 0, 0);
    k.fillA = 14;
    ui_shape(&cv, r, &k);
  }
  gn_blit(&cv, &g_sxArt.board, bx - fr, by - fr, 255);
  BOOL anim = g_sxAnim.on;
  int at = anim ? sx_anim_now() : 0;
  BOOL my = sx_my_turn() && !anim;
  /* последний ход — тёплой заливкой «откуда» и «куда» */
  if (g_sx.haveLast && !anim) {
    int x, y;
    sx_sq_xy(g_sx.last.from, &x, &y);
    RECT a = {x, y, x + q, y + q};
    gn_tint(&cv, a, RGB(255, 196, 70), 70);
    sx_sq_xy(g_sx.last.to, &x, &y);
    RECT b = {x, y, x + q, y + q};
    gn_tint(&cv, b, RGB(255, 196, 70), 100);
  }
  /* выбранная фигура — золотое поле */
  if (my && g_sx.sel >= 0) {
    int x, y;
    sx_sq_xy(g_sx.sel, &x, &y);
    RECT a = {x, y, x + q, y + q};
    gn_tint(&cv, a, RGB(255, 206, 84), 120);
  }
  /* что показывать: посреди хода — позицию до него */
  SxPos b = anim ? g_sxAnim.before : g_sx.pos;
  int alphaSq[64];
  for (int i = 0; i < 64; i++) alphaSq[i] = 255;
  int mover = 0, rook = 0, rookFrom = -1, rookTo = -1, capSq = -1;
  float mx = 0, my2 = 0, rx = 0, ry = 0;
  int flashA = 0;
  if (anim) {
    const SxMove *m = &g_sxAnim.m;
    mover = b.sq[m->from];
    b.sq[m->from] = 0;
    float f = g_sxAnim.slide ? (float)at / g_sxAnim.slideMs : 1;
    if (f > 1) f = 1;
    float e = sx_ease(f), ax, ay, zx, zy;
    sx_sq_center(m->from, &ax, &ay);
    sx_sq_center(m->to, &zx, &zy);
    mx = ax + (zx - ax) * e;
    my2 = ay + (zy - ay) * e;
    if (m->flags & SXF_CASTLE) {
      rookFrom = m->to > m->from ? m->from + 3 : m->from - 4;
      rookTo = m->to > m->from ? m->from + 1 : m->from - 1;
      rook = b.sq[rookFrom];
      b.sq[rookFrom] = 0;
      sx_sq_center(rookFrom, &ax, &ay);
      sx_sq_center(rookTo, &zx, &zy);
      rx = ax + (zx - ax) * e;
      ry = ay + (zy - ay) * e;
    }
    if (m->flags & SXF_CAP) {
      capSq = (m->flags & SXF_EP) ? m->to - sx_color(mover) * 8 : m->to;
      int after = at - (g_sxAnim.slide ? g_sxAnim.slideMs : 0);
      alphaSq[capSq] = after <= 0 ? 255 : after >= SX_FADE_MS ? 0 : 255 * (SX_FADE_MS - after) / SX_FADE_MS;
    }
    /* дошла — уже превращённая, со вспышкой */
    int after = at - (g_sxAnim.slide ? g_sxAnim.slideMs : 0);
    if (m->promo && f >= 1) {
      mover = sx_color(mover) * m->promo;
      float ft = (float)after / SX_FLASH_MS;
      flashA = ft < 1 ? (int)(255 * (1 - ft) * (ft < 0.15f ? ft / 0.15f : 1)) : 0;
    }
  }
  /* король под шахом — красное свечение под ним */
  if (!anim && (g_sxn.mode == GN_PLAY || g_sxn.mode == GN_OVER) && sx_in_check(&b, b.side)) {
    int k = sx_king_sq(&b, b.side);
    if (k >= 0) {
      float cx, cy;
      sx_sq_center(k, &cx, &cy);
      sx_blit_c(&cv, &g_sxArt.check, cx, cy, 230);
    }
  }
  /* куда можно выбранной */
  static SxMove mv[SX_MAXMOVES];
  int nmv = my && g_sx.sel >= 0 ? sx_legal(&g_sx.pos, mv, SX_MAXMOVES) : 0;
  BOOL target[64];
  memset(target, 0, sizeof(target));
  for (int i = 0; i < nmv; i++)
    if (mv[i].from == g_sx.sel) target[mv[i].to] = TRUE;
  /* тащат мышью — на своём поле бледная тень фигуры */
  BOOL dragging = my && g_sx.drag && g_sx.dragMoved && g_sx.sel >= 0;
  if (dragging) alphaSq[g_sx.sel] = 80;
  if (g_sx.promoFrom >= 0) alphaSq[g_sx.promoFrom] = 110;
  /* фигуры — сверху вниз по экрану, чтобы тени ложились правильно */
  for (int sr = 0; sr < 8; sr++)
    for (int sc = 0; sc < 8; sc++) {
      int s = (flip ? sr : 7 - sr) * 8 + (flip ? 7 - sc : sc);
      float cx, cy;
      sx_sq_center(s, &cx, &cy);
      if (target[s] && b.sq[s]) sx_blit_c(&cv, &g_sxArt.cap, cx, cy, s == g_sxHover ? 255 : 210);
      if (!b.sq[s] || alphaSq[s] <= 0) continue;
      sx_draw_piece(&cv, b.sq[s], cx, cy, alphaSq[s], 0);
    }
  /* на проходе — красное кольцо на поле хода, а снимается пешка рядом */
  for (int s = 0; s < 64; s++) {
    if (!target[s] || b.sq[s]) continue;
    float cx, cy;
    sx_sq_center(s, &cx, &cy);
    sx_blit_c(&cv, s == g_sxHover ? &g_sxArt.dotHi : &g_sxArt.dot, cx, cy, 255);
  }
  /* едущие фигуры — поверх всех */
  if (anim) {
    if (rook) sx_draw_piece(&cv, rook, rx, ry, 255, 0);
    sx_draw_piece(&cv, mover, mx, my2, 255, g_sxAnim.slide && at < g_sxAnim.slideMs ? (float)XS_(2) : 0);
    if (flashA > 0) sx_blit_c(&cv, &g_sxArt.flash, mx, my2, flashA);
  }
  if (dragging) sx_draw_piece(&cv, g_sx.pos.sq[g_sx.sel], (float)g_sx.dragX, (float)g_sx.dragY + q * 0.12f, 255, (float)XS_(6));
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
  /* выбор фигуры превращения — поверх затемнённой доски */
  if (g_sx.promoFrom >= 0 && g_sxn.mode == GN_PLAY) {
    RECT all = {bx, by, bx + bp, by + bp};
    gn_tint(&cv, all, RGB(16, 12, 10), 120);
    int mc = sx_my_color();
    for (int i = 0; i < 4; i++) {
      RECT r;
      sx_promo_rect(i, &r);
      int pk = kSxPromo[i];
      POINT hp = {g_sx.dragX, g_sx.dragY};
      BOOL hot = PtInRect(&r, hp);
      UiShape k;
      memset(&k, 0, sizeof(k));
      k.r = q * 0.5f;
      k.fill = hot ? RGB(255, 226, 150) : RGB(246, 238, 222);
      k.fillA = 255;
      k.border = RGB(214, 170, 88);
      k.borderA = 255;
      k.bw = (float)XS_(2);
      RECT d = {r.left + XS_(3), r.top + XS_(3), r.right - XS_(3), r.bottom - XS_(3)};
      ui_shape(&cv, d, &k);
      sx_draw_piece(&cv, mc * pk, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, 255, 0);
    }
  }
  /* итог партии — полосой поперёк доски */
  if (g_sxn.mode == GN_OVER && !anim) {
    RECT band = {bx, by + bp / 2 - XS_(46), bx + bp, by + bp / 2 + XS_(46)};
    gn_tint(&cv, band, RGB(16, 12, 10), 175);
    RECT l1 = {band.left, band.top, band.right, band.top + XS_(2)}, l2 = {band.left, band.bottom - XS_(2), band.right, band.bottom};
    gn_tint(&cv, l1, RGB(214, 170, 88), 200);
    gn_tint(&cv, l2, RGB(214, 170, 88), 200);
    if (!g_sxBig) g_sxBig = CreateFontW(-XS_(28), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    BOOL won = g_sx.winner == g_sxn.me;
    const wchar_t *big = g_sx.winner < 0 ? g_sx.endBig : won ? L"Победа!" : L"Поражение";
    if (g_sxBig) SelectObject(cv.dc, g_sxBig);
    SetTextColor(cv.dc, won ? RGB(255, 214, 110) : RGB(240, 232, 220));
    RECT t1 = {band.left, band.top + XS_(8), band.right, band.top + XS_(50)};
    DrawTextW(cv.dc, big, -1, &t1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (g_fontSmall) SelectObject(cv.dc, g_fontSmall);
    SetTextColor(cv.dc, RGB(220, 206, 186));
    RECT t2 = {band.left + XS_(10), band.top + XS_(52), band.right - XS_(10), band.bottom - XS_(6)};
    DrawTextW(cv.dc, g_sx.endWhy, -1, &t2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  ui_canvas_end(&cv, hdc);
  if (anim && at >= sx_anim_total(&g_sxAnim)) g_sxAnim.on = FALSE; /* досмотрели — доска как есть */
}

/* ---- справа: игроки, запись партии, чей ход ---------------------------------- */

static int sx_side_x(void) { return sx_bx() + sx_board_px() + XS_(SX_FRAME) + XS_(14); }

/* карточка игрока: фигурка цвета, имя, строка под ним, взятые и перевес */
static void sx_paint_player(UiCanvas *cv, RECT r, int color, const wchar_t *name, const wchar_t *sub, BOOL turn) {
  UiShape k;
  memset(&k, 0, sizeof(k));
  k.r = (float)XS_(10);
  k.fill = COL_PAPER_DARK;
  k.fillA = 255;
  k.border = turn ? COL_SAGE : COL_LINE;
  k.borderA = 255;
  k.bw = turn ? (float)XS_(2) : 1.0f;
  ui_shape(cv, r, &k);
  int pad = XS_(10), md = g_sxArt.miniD;
  gn_blit(cv, &g_sxArt.mini[color < 0][SX_K], r.left + pad - 2, r.top + XS_(8) - 2, 255);
  SetBkMode(cv->dc, TRANSPARENT);
  if (g_fontUi) SelectObject(cv->dc, g_fontUi);
  SetTextColor(cv->dc, COL_INK);
  RECT tn = {r.left + pad + md + XS_(6), r.top + XS_(6), r.right - pad, r.top + XS_(24)};
  DrawTextW(cv->dc, name, -1, &tn, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (g_fontSmall) SelectObject(cv->dc, g_fontSmall);
  SetTextColor(cv->dc, turn ? COL_SAGE : COL_MUTED);
  RECT ts = {tn.left, r.top + XS_(24), r.right - pad, r.top + XS_(40)};
  DrawTextW(cv->dc, sub, -1, &ts, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  /* что взял этот игрок — по старшинству; и перевес в материале */
  int ci = color > 0 ? 0 : 1, x = r.left + pad - XS_(3), y = r.top + XS_(42);
  for (int kind = SX_Q; kind >= SX_P; kind--)
    for (int i = 0; i < g_sx.ntaken[ci]; i++)
      if (g_sx.taken[ci][i] == kind && x + md < r.right - XS_(34)) {
        gn_blit(cv, &g_sxArt.mini[color > 0][kind], x, y, 255);
        x += md * 3 / 5;
      }
  int adv = sx_material(&g_sx.pos, color) - sx_material(&g_sx.pos, -color);
  if (adv > 0) {
    wchar_t t[16];
    _snwprintf(t, 16, L"+%d", adv);
    SetTextColor(cv->dc, COL_MUTED);
    RECT ta = {x + XS_(6), y, r.right - pad, y + md};
    DrawTextW(cv->dc, t, -1, &ta, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
}

static RECT sx_list_rect(void) {
  int x = sx_side_x(), y0 = XS_(SX_TOP), h = sx_board_px() + XS_(SX_FRAME) * 2;
  RECT r = {x, y0 + XS_(78), x + XS_(SX_SIDE), y0 + h - XS_(78) - XS_(40)};
  return r;
}

static void sx_paint_side(HDC hdc) {
  int x = sx_side_x(), y0 = XS_(SX_TOP), w = XS_(SX_SIDE), h = sx_board_px() + XS_(SX_FRAME) * 2;
  RECT box = {x, y0, x + w, y0 + h};
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, hdc, box, box.left, box.top, TRUE)) return;
  int mc = sx_my_color();
  BOOL mine = sx_my_turn(), play = g_sxn.mode == GN_PLAY, over = g_sxn.mode == GN_OVER;
  BOOL check = (play || over) && sx_in_check(&g_sx.pos, g_sx.pos.side);
  /* соперник сверху — его фигуры на доске тоже сверху */
  {
    RECT r = {x, y0, x + w, y0 + XS_(70)};
    wchar_t sub[80];
    if (over) lstrcpynW(sub, g_sx.winner == 1 - g_sxn.me ? L"выиграл" : g_sx.winner < 0 ? L"ничья" : L"проиграл", 80);
    else if (play && !mine) lstrcpynW(sub, check ? L"думает — ему шах" : L"думает над ходом…", 80);
    else lstrcpynW(sub, gn_opp_online(&g_sxn) ? (mc > 0 ? L"чёрные · в сети" : L"белые · в сети")
                                              : (mc > 0 ? L"чёрные · не в сети" : L"белые · не в сети"), 80);
    sx_paint_player(&cv, r, -mc, gn_opp(&g_sxn), sub, play && !mine);
  }
  {
    RECT r = {x, y0 + h - XS_(70), x + w, y0 + h};
    const wchar_t *sub = over ? (g_sx.winner == g_sxn.me ? L"вы выиграли" : g_sx.winner < 0 ? L"ничья" : L"вы проиграли")
                         : mine ? (check ? L"ваш ход — вам шах!" : L"ваш ход")
                                : (mc > 0 ? L"белые" : L"чёрные");
    sx_paint_player(&cv, r, mc, L"Вы", sub, mine);
  }
  /* чей ход — плашкой над своей карточкой */
  {
    const wchar_t *pill = over ? L"Партия окончена" : mine ? (check ? L"Шах! Ваш ход" : L"Ваш ход") : L"Ход соперника";
    HFONT f = g_fontUi ? g_fontUi : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SelectObject(cv.dc, f);
    RECT pr = {x, y0 + h - XS_(70) - XS_(34), x + w, y0 + h - XS_(70) - XS_(8)};
    UiShape k;
    memset(&k, 0, sizeof(k));
    k.r = -1;
    k.fill = mine ? (check ? RGB(214, 64, 52) : COL_SAGE) : COL_PAPER_DARK;
    k.fillA = 255;
    if (!mine) {
      k.border = COL_LINE;
      k.borderA = 255;
      k.bw = 1;
    }
    ui_shape(&cv, pr, &k);
    SetBkMode(cv.dc, TRANSPARENT);
    SetTextColor(cv.dc, mine ? RGB(255, 255, 255) : COL_MUTED);
    DrawTextW(cv.dc, pill, -1, &pr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  /* запись партии: «1. e4 e5», последний ход выделен */
  {
    RECT lr = sx_list_rect();
    UiShape k;
    memset(&k, 0, sizeof(k));
    k.r = (float)XS_(10);
    k.fill = COL_PAPER;
    k.fillA = 255;
    k.border = COL_LINE;
    k.borderA = 255;
    k.bw = 1;
    ui_shape(&cv, lr, &k);
    if (g_fontSmall) SelectObject(cv.dc, g_fontSmall);
    SetTextColor(cv.dc, COL_MUTED);
    RECT tt = {lr.left + XS_(10), lr.top + XS_(6), lr.right - XS_(10), lr.top + XS_(22)};
    DrawTextW(cv.dc, L"Запись партии", -1, &tt, DT_LEFT | DT_SINGLELINE);
    int rowH = XS_(20), top = lr.top + XS_(26), vis = (lr.bottom - XS_(6) - top) / rowH;
    int rows = (g_sx.nsan + 1) / 2;
    if (vis < 1) vis = 1;
    int maxScroll = rows > vis ? rows - vis : 0;
    if (g_sx.scroll > maxScroll) g_sx.scroll = maxScroll;
    if (g_sx.scroll < 0) g_sx.scroll = 0;
    int first = rows - vis - g_sx.scroll;
    if (first < 0) first = 0;
    if (!rows) {
      SetTextColor(cv.dc, COL_MUTED);
      RECT e = {lr.left + XS_(10), top, lr.right - XS_(10), top + rowH};
      DrawTextW(cv.dc, play ? L"ходов ещё не было" : L"", -1, &e, DT_LEFT | DT_SINGLELINE);
    }
    HFONT fb = g_fontBody ? g_fontBody : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for (int rI = first; rI < rows && rI < first + vis; rI++) {
      int y = top + (rI - first) * rowH;
      wchar_t num[12];
      _snwprintf(num, 12, L"%d.", rI + 1);
      SelectObject(cv.dc, g_fontSmall ? g_fontSmall : fb);
      SetTextColor(cv.dc, COL_MUTED);
      RECT rn = {lr.left + XS_(8), y, lr.left + XS_(40), y + rowH};
      DrawTextW(cv.dc, num, -1, &rn, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
      for (int j = 0; j < 2; j++) {
        int i = rI * 2 + j;
        if (i >= g_sx.nsan) break;
        RECT rm = {lr.left + XS_(48) + j * XS_(76), y + 1, lr.left + XS_(48) + j * XS_(76) + XS_(72), y + rowH - 1};
        if (i == g_sx.nsan - 1) {
          UiShape hk;
          memset(&hk, 0, sizeof(hk));
          hk.r = (float)XS_(5);
          hk.fill = RGB(255, 222, 150);
          hk.fillA = 200;
          RECT hr = {rm.left - XS_(5), rm.top, rm.right - XS_(14), rm.bottom};
          ui_shape(&cv, hr, &hk);
        }
        SelectObject(cv.dc, fb);
        SetTextColor(cv.dc, COL_INK);
        DrawTextW(cv.dc, g_sx.san[i], -1, &rm, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      }
    }
    if (g_sx.scroll > 0) { /* прокручено вверх — внизу есть ещё */
      SetTextColor(cv.dc, COL_MUTED);
      SelectObject(cv.dc, g_fontSmall ? g_fontSmall : fb);
      RECT rm = {lr.left, lr.bottom - XS_(18), lr.right - XS_(10), lr.bottom - XS_(2)};
      DrawTextW(cv.dc, L"▼ ещё ходы", -1, &rm, DT_RIGHT | DT_SINGLELINE);
    }
  }
  ui_canvas_end(&cv, hdc);
}

/* ---- окно ------------------------------------------------------------------- */

static int sx_below_y(void) { return XS_(SX_TOP) + sx_board_px() + XS_(SX_FRAME) * 2 + XS_(10); }

static void sx_layout(void) {
  if (!g_sxWnd) return;
  RECT rc;
  GetClientRect(g_sxWnd, &rc);
  place_panel_close(g_sxWnd);
  gn_layout_lobby(&g_sxn, g_nd.poll);
  int m = g_sxn.mode, pad = XS_(14), bh = XS_(28), gap = XS_(8);
  int by = sx_below_y() + XS_(30);
  HWND draw = GetDlgItem(g_sxWnd, ID_SX_DRAW), drawok = GetDlgItem(g_sxWnd, ID_SX_DRAWOK),
       resign = GetDlgItem(g_sxWnd, ID_SX_RESIGN), back = GetDlgItem(g_sxWnd, ID_SX_BACK);
  BOOL play = m == GN_PLAY, over = m == GN_OVER;
  BOOL offer = play && !g_sx.iOffered && !g_sx.oppOffersDraw, agree = play && g_sx.oppOffersDraw;
  ShowWindow(draw, offer ? SW_SHOW : SW_HIDE);
  ShowWindow(drawok, agree ? SW_SHOW : SW_HIDE);
  ShowWindow(resign, play ? SW_SHOW : SW_HIDE);
  ShowWindow(back, over ? SW_SHOW : SW_HIDE);
  if (play) { /* «Сдаться» — сразу за видимой кнопкой ничьей */
    int x = pad;
    if (offer) {
      MoveWindow(draw, x, by, XS_(170), bh, TRUE);
      x += XS_(170) + gap;
    }
    if (agree) {
      MoveWindow(drawok, x, by, XS_(200), bh, TRUE);
      x += XS_(200) + gap;
    }
    MoveWindow(resign, x, by, XS_(110), bh, TRUE);
  }
  if (over) MoveWindow(back, pad, by, XS_(170), bh, TRUE);
}

static void sx_mark_btn(void) { gn_mark_btn(&g_sxn, g_btnSx, L"Шахматы", sx_my_turn()); }

static void sx_refresh_view(void) {
  sx_mark_btn();
  if (!g_sxWnd) return;
  if (g_sxn.mode == GN_LOBBY) gn_list_sync(&g_sxn, GetDlgItem(g_sxWnd, ID_SX_LIST), g_nd.poll);
  sx_layout();
  InvalidateRect(g_sxWnd, NULL, FALSE);
}

/* список «кто в сети» у нард обновился — и у шахмат */
static void shahmaty_online_changed(void) {
  if (g_sxn.mode == GN_PLAY) sx_turn_status(); /* соперник пропал из сети или вернулся */
  if (g_sxWnd && IsWindowVisible(g_sxWnd) && (g_sxn.mode == GN_LOBBY || g_sxn.mode == GN_PLAY)) sx_refresh_view();
  else sx_mark_btn();
}

static void sx_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Шахматы");
  if (g_sxn.mode == GN_LOBBY) {
    gn_paint_lobby(&g_sxn, hdc, rc, g_nd.poll,
                   L"Шахматы по правилам FIDE: рокировка, взятие на проходе, превращение пешки — фигура на выбор; "
                   L"мат, пат, ничья по согласию, при троекратном повторении и после 50 ходов без взятий. "
                   L"Кто белые — выпадает случайно. Ходите мышью: щелчком по фигуре и полю или перетаскиванием.");
    return;
  }
  if (g_sxn.mode == GN_WAIT) {
    gn_paint_wait(&g_sxn, hdc, rc);
    return;
  }
  sx_paint_board(hdc);
  sx_paint_side(hdc);
  int pad = XS_(14), yb = sx_below_y();
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  RECT st = {pad, yb, rc.right - pad, yb + XS_(22)};
  SetTextColor(hdc, g_sxn.mode == GN_OVER ? COL_SAGE : COL_INK);
  DrawTextW(hdc, g_sxn.status, -1, &st, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  wchar_t dg[400];
  gn_diag_text(&g_sxn, dg, 400);
  RECT dr = {pad, yb + XS_(66), rc.right - pad, rc.bottom - XS_(4)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, g_sxn.writeFail ? RGB(200, 60, 40) : COL_MUTED);
  DrawTextW(hdc, dg, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

/* под мышью поле, по которому есть смысл щёлкнуть */
static BOOL sx_clickable(int s) {
  if (s < 0 || !sx_my_turn() || g_sx.promoFrom >= 0) return FALSE;
  if (sx_color(g_sx.pos.sq[s]) == sx_my_color() && sx_has_moves_from(s)) return TRUE;
  return g_sx.sel >= 0 && sx_find_move(g_sx.sel, s, 0, NULL);
}

static void sx_cancel_pick(void) {
  g_sx.sel = g_sx.promoFrom = g_sx.promoTo = -1;
  if (g_sx.drag) ReleaseCapture();
  g_sx.drag = FALSE;
  sx_turn_status();
  sx_refresh_view();
}

static void sx_mouse_down(int x, int y) {
  if (!sx_my_turn()) return;
  g_sxAnim.on = FALSE; /* щёлкнули, пока ехал прошлый ход, — досмотрено */
  if (g_sx.promoFrom >= 0) {
    int pk = sx_promo_hit(x, y);
    if (pk < 0) {
      sx_cancel_pick();
      return;
    }
    SxMove m;
    if (sx_find_move(g_sx.promoFrom, g_sx.promoTo, pk, &m)) sx_commit(&m, TRUE);
    return;
  }
  int s = sx_hit(x, y);
  if (s < 0) {
    sx_cancel_pick();
    return;
  }
  BOOL own = sx_color(g_sx.pos.sq[s]) == sx_my_color();
  if (g_sx.sel >= 0 && s != g_sx.sel && !own) {
    if (!sx_try_move(g_sx.sel, s, TRUE)) sx_cancel_pick();
    return;
  }
  if (own && sx_has_moves_from(s)) {
    g_sx.selWas = g_sx.sel == s;
    g_sx.sel = s;
    g_sx.drag = TRUE;
    g_sx.dragMoved = FALSE;
    g_sx.downX = g_sx.dragX = x;
    g_sx.downY = g_sx.dragY = y;
    SetCapture(g_sxWnd);
  } else g_sx.sel = -1;
  sx_turn_status();
  sx_refresh_view();
}

static void sx_mouse_up(int x, int y) {
  if (!g_sx.drag) return;
  g_sx.drag = FALSE;
  ReleaseCapture();
  if (!sx_my_turn() || g_sx.sel < 0) return;
  int s = sx_hit(x, y);
  if (g_sx.dragMoved) {
    if (s >= 0 && s != g_sx.sel && sx_try_move(g_sx.sel, s, FALSE)) return;
  } else if (g_sx.selWas) {
    g_sx.sel = -1; /* второй щелчок по выбранной — снять выбор */
  }
  sx_turn_status();
  sx_refresh_view();
}

static LRESULT CALLBACK ShahmatyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    sx_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_TIMER:
    if (wParam == SX_TIMER) {
      if (!g_sxAnim.on) KillTimer(hwnd, SX_TIMER);
      RECT rc;
      GetClientRect(hwnd, &rc);
      RECT bd = {0, XS_(SX_TOP) - XS_(6), sx_side_x(), XS_(SX_TOP) + sx_board_px() + XS_(SX_FRAME) * 2 + XS_(12)};
      InvalidateRect(hwnd, &bd, FALSE);
    }
    return 0;
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_LBUTTONDOWN:
    if (g_sxn.mode == GN_PLAY) sx_mouse_down(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  case WM_LBUTTONUP:
    sx_mouse_up(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  case WM_CAPTURECHANGED:
    if (g_sx.drag && (HWND)lParam != hwnd) {
      g_sx.drag = FALSE;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_MOUSEMOVE: {
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    int s = g_sxn.mode == GN_PLAY ? sx_hit(x, y) : -1;
    BOOL redraw = s != g_sxHover;
    if (g_sx.drag) {
      if (abs(x - g_sx.downX) > XS_(4) || abs(y - g_sx.downY) > XS_(4)) g_sx.dragMoved = TRUE;
      redraw = TRUE;
    }
    if (g_sx.promoFrom >= 0) redraw = TRUE; /* подсветка фигуры превращения под мышью */
    g_sx.dragX = x;
    g_sx.dragY = y;
    if (redraw) {
      g_sxHover = s;
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&tme);
      RECT bd = {0, XS_(SX_TOP) - XS_(6), sx_side_x(), XS_(SX_TOP) + sx_board_px() + XS_(SX_FRAME) * 2 + XS_(12)};
      InvalidateRect(hwnd, &bd, FALSE);
    }
    return 0;
  }
  case WM_MOUSELEAVE:
    if (g_sxHover >= 0) {
      g_sxHover = -1;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_MOUSEWHEEL: {
    POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &p);
    RECT lr = sx_list_rect();
    if (PtInRect(&lr, p)) {
      g_sx.scroll += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 2 : -2;
      InvalidateRect(hwnd, &lr, FALSE);
    }
    return 0;
  }
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT && (g_sx.drag || sx_clickable(g_sxHover) ||
                                       (g_sx.promoFrom >= 0 && sx_promo_hit(g_sx.dragX, g_sx.dragY) >= 0))) {
      SetCursor(LoadCursorW(NULL, IDC_HAND));
      return TRUE;
    }
    break;
  case WM_RBUTTONDOWN:
    sx_cancel_pick(); /* правая — снять выбор */
    return 0;
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      if (g_sx.sel >= 0 || g_sx.promoFrom >= 0) sx_cancel_pick();
      else ShowWindow(hwnd, SW_HIDE);
    }
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
    if (id != ID_SX_LIST && HIWORD(wParam) != BN_CLICKED) return 0;
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (gn_command(&g_sxn, id, HIWORD(wParam))) {
      if (id == ID_SX_LIST) return 0; /* фокус у списка не отнимаем */
    }
    if (id == ID_SX_DRAW) sx_offer_draw();
    if (id == ID_SX_DRAWOK) sx_accept_draw();
    if (id == ID_SX_RESIGN) sx_resign();
    if (id == ID_SX_BACK) gn_back_to_lobby(&g_sxn);
    SetFocus(hwnd);
    return 0;
  }
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void shahmaty_start(void) {
  if (g_sxn.thread) return;
  wchar_t *t = gn_load_local(&g_sxn);
  if (t) {
    g_sx.white = gn_game_bit(g_sxn.game);
    sx_start(&g_sx.pos);
    g_sx.ply = 1;
    free(t);
  }
  gn_start(&g_sxn);
}

static void shahmaty_show(void) {
  if (!g_sxWnd) {
    HDC s = GetDC(NULL);
    g_sxS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    g_sxn.sc = g_sxS;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ShahmatyProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadShahmaty";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    int w = XS_(SX_LEFT * 2 + SX_FRAME * 2 + SX_SQ * 8 + 14 + SX_SIDE), h = XS_(SX_TOP + SX_SQ * 8 + SX_FRAME * 2 + 124);
    g_sxWnd = CreateWindowExW(WS_EX_APPWINDOW, L"CursorPadShahmaty", L"Шахматы",
                              WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU, 0, 0, w, h, NULL, NULL,
                              g_inst, NULL);
    if (!g_sxWnd) return;
    round_corners(g_sxWnd);
    mk_btn(g_sxWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_sxWnd, L"–", ID_PANEL_MIN);
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0,
                                100, 100, g_sxWnd, (HMENU)(INT_PTR)ID_SX_LIST, g_inst, NULL);
    const struct {
      const wchar_t *t;
      int id;
    } btns[] = {{L"Позвать играть", ID_SX_INVITE},  {L"Принять", ID_SX_ACCEPT},
                {L"Отказаться", ID_SX_DECLINE},     {L"Предложить ничью", ID_SX_DRAW},
                {L"Согласиться на ничью", ID_SX_DRAWOK}, {L"Сдаться", ID_SX_RESIGN},
                {L"К списку игроков", ID_SX_BACK},  {L"Отменить приглашение", ID_SX_CANCEL}};
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
      HWND b = mk_btn(g_sxWnd, btns[i].t, btns[i].id);
      if (g_fontUi) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    }
    if (g_fontBody) SendMessageW(list, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_sxWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                 SWP_NOZORDER);
  }
  if (g_sxn.mode == GN_PLAY) sx_turn_status();
  sx_refresh_view();
  ShowWindow(g_sxWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_sxWnd);
  SetFocus(g_sxWnd);
}
