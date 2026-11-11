/* ---- Стратегия по сети через общую папку (с 2026.11.11) ---------------------

   Пошаговая стратегия на двоих. Как шашки, нарды, морской бой и шахматы:
   двое, у кого в Настройках одна общая папка; кто в сети — тот же список.
   Связь — общая (game_net.c), свои файлы — в <папка>\CursorPad-Strategiya.
   Правила — strategiya_rules.c, компьютерный соперник — strategiya_bot.c,
   рисунок — strategiya_art.c.

   Строки своего файла партии, кроме общих (name, id, decline, cancel, resign):
     m\t<ход>\t<действия>   — весь мой ход разом: действия через точку
                              («mbcdc.adcec.bsbd», без действий — «-»)
   Ход уходит одной записью, когда нажато «Завершить ход»: соперник может
   быть и не в сети — увидит ход, когда запустит CursorPad, и программа
   покажет его движением. Пока ход не завершён, действия лежат только на
   своём компьютере (strategiya.txt) и отменяются по одному. Карта и кто
   ходит первым — по номеру партии, у обоих одинаково. Каждое действие
   программа соперника проверяет по правилам; партия восстанавливается из
   файлов целиком.

   «Игра с компьютером» — та же игра без общей папки: за соперника ходит
   strategiya_bot.c, записи обеих сторон лежат в памяти.

   Вид: карта из шестиугольников в ореховой раме; свой замок всегда слева
   (второму игроку карта повёрнута). Синие — тот, кто позвал, красные — кто
   принял. Щелчок по своему отряду — светлые клетки (куда дойдёт) и красные
   обводки (кого достанет); щелчок по клетке — пройти, по цели — ударить.
   Справа — соперник и вы (замок, золото, деревни), подсказка по тому, что
   под мышью (с прогнозом удара), и «Нанять». */

#include "strategiya_rules.c"
#include "strategiya_bot.c"
#include "strategiya_art.c"

#define WM_STRATEGIYA_POLL (WM_APP + 69) /* lParam — GnPoll*, освобождает получатель */
#define SG_TIMER 1                       /* движение на карте */
#define SG_TIMER_BOT 2                   /* ход компьютера — чуть погодя */
#define SG_MAXACTS 64
/* ID_SG_* — в cursorpad.c */

typedef struct {
  SgState cur; /* позиция: всё сыгранное и мои действия этого хода */
  BOOL have;
  BOOL needCommit; /* мои действия уже кончили партию — запись надо отправить */
  wchar_t endBig[40], endWhy[200];
  int winner; /* сторона-победитель; −1 — ничья или партия остановлена */
  /* мой ход: действия, ещё не отправленные (как в файле партии) */
  char pend[SG_LINE];
  int pendTurn;
  /* последний целый ход — показать движением, повторить по кнопке */
  SgState lastBefore;
  SgAct lastActs[SG_MAXACTS];
  int lastN, lastTurn, lastSide;
  int shownTurn; /* какой ход соперника уже показан */
  /* выбор */
  int sel;      /* выбранный отряд (номер в cur.u); −1 — нет */
  int buy;      /* кого ставим у замка (вид отряда); −1 — никого */
  int hover;    /* клетка под мышью; −1 */
  int hoverBuy; /* вид отряда в «Нанять» под мышью; −1 */
  unsigned char reach[SG_CELLS]; /* куда дойдёт выбранный (255 — нет) */
  int notifiedTurn;
  /* игра с компьютером: я — сторона 0, записи обеих сторон — в памяти */
  BOOL solo;
  ShareBuf soloLog[2];
  wchar_t soloGame[40];
} SgGame;

static SgGame g_sg = {.sel = -1, .buy = -1, .hover = -1, .hoverBuy = -1, .winner = -1};
static HWND g_sgWnd;
static float g_sgS = 1.0f;

static void sg_fresh(void);
static void sg_refresh_view(void);
static void sg_turn_status(void);
static void sg_extra(wchar_t *out, int cap);

static GnNet g_sgn = {
    .sub = L"CursorPad-Strategiya",
    .local = L"strategiya",
    .title = L"Стратегия",
    .invText = L"зовёт сыграть в стратегию",
    .moveKey = L"m",
    .msg = WM_STRATEGIYA_POLL,
    .balloon = 10,
    .wnd = &g_sgWnd,
    .sc = 1.0f,
    .idList = ID_SG_LIST,
    .idInvite = ID_SG_INVITE,
    .idAccept = ID_SG_ACCEPT,
    .idDecline = ID_SG_DECLINE,
    .idCancel = ID_SG_CANCEL,
    .fresh = sg_fresh,
    .view = sg_refresh_view,
    .turn = sg_turn_status,
    .extra = sg_extra,
};

#define sg_status(...) gn_status(&g_sgn, __VA_ARGS__)

static const wchar_t *kSgName[SGU_N] = {L"Мечник", L"Копейщик", L"Лучник", L"Всадник", L"Катапульта"};
static const wchar_t *kSgWhom[SGU_N] = {L"мечника", L"копейщика", L"лучника", L"всадника", L"катапульту"};
static const wchar_t *kSgAbout[SGU_N] = {
    L"Ход 3, бьёт рядом. Сильнее копейщика, слабее всадника.",
    L"Ход 3, бьёт рядом. Сильнее всадника, слабее мечника.",
    L"Ход 3, бьёт на 1–2 клетки: издали — без ответа. Вблизи слаб.",
    L"Ход 5, бьёт рядом. Сильнее мечника и лучника, слабее копейщика.",
    L"Ход 2, бьёт на 2–3 клетки, лучшая против замка. За ход — или едет, или бьёт; не отвечает на удар.",
};
static const wchar_t *kSgSideName[2] = {L"синие", L"красные"};

static int GS_(int v) { return (int)(v * g_sgS + 0.5f); }

static int sg_me(void) { return g_sg.solo ? 0 : g_sgn.me; }
static BOOL sg_my_turn(void) {
  return g_sgn.mode == GN_PLAY && g_sg.have && !g_sg.cur.over && sg_side(&g_sg.cur) == sg_me();
}
static const wchar_t *sg_opp_name(void) { return g_sg.solo ? L"Компьютер" : gn_opp(&g_sgn); }

/* есть ли отряду что делать: пройти или ударить */
static BOOL sg_has_target(const SgState *st, int ui) {
  if (!sg_can_strike(&st->u[ui])) return FALSE;
  for (int c = 0; c < SG_CELLS; c++)
    if (sg_cell_ok(c) && sg_can_attack(st, ui, c)) return TRUE;
  return FALSE;
}

static BOOL sg_unit_can_act(const SgState *st, int ui) {
  const SgUnit *u = &st->u[ui];
  if (!u->alive || u->side != sg_side(st) || u->done) return FALSE;
  return !u->moved || sg_has_target(st, ui);
}

/* ---- движение на карте ------------------------------------------------------ */

/* Что рисуется: view отстаёт от позиции на действия из очереди q — они
   показываются по одному и по одному же применяются к view. Очередь кончилась
   — рисуется сама позиция. */
typedef struct {
  BOOL on;
  BOOL opp; /* показ хода соперника: щелчок — досмотреть сразу */
  SgState view;
  SgAct q[SG_MAXACTS];
  int n, i;
  ULONGLONG t0;
  /* посчитано для действия q[i] */
  int ms;  /* сколько длится показ действия */
  int gap; /* и пауза после него (ход соперника — с расстановкой) */
  int unit, tunit; /* кто действует и по кому (номера во view.u; −1 — замок или нет) */
  int path[16], npath;
  int dmg, back;
  BOOL kill, die, ranged;
} SgAnim;

static SgAnim g_sgAnim;

#define SG_STEP_MS 95  /* клетка пути */
#define SG_HIT_MS 190  /* замах или полёт */
#define SG_AFTER_MS 260 /* вспышка и число урона */
#define SG_BUY_MS 260
#define SG_OPP_PCT 165 /* ход соперника показывается медленнее своих действий, % */
#define SG_OPP_GAP 140 /* и с паузой между действиями */

/* сколько прошло от начала показа действия — в «своих» миллисекундах */
static int sg_anim_t(void) {
  const SgAnim *an = &g_sgAnim;
  ULONGLONG el = GetTickCount64() - an->t0;
  if (el > 60000) el = 60000;
  return an->opp ? (int)el * 100 / SG_OPP_PCT : (int)el;
}

static void sg_anim_prepare(void) {
  SgAnim *an = &g_sgAnim;
  if (an->i >= an->n) {
    an->on = FALSE;
    return;
  }
  const SgAct *a = &an->q[an->i];
  SgState probe = an->view;
  if (!sg_apply(&probe, a)) { /* не сошлось — покажем позицию как есть */
    an->on = FALSE;
    return;
  }
  an->unit = an->tunit = -1;
  an->npath = an->dmg = an->back = 0;
  an->kill = an->die = an->ranged = FALSE;
  if (a->kind == 'm') {
    an->unit = an->view.at[a->a];
    an->npath = sg_path(&an->view, an->unit, a->b, an->path);
    an->ms = SG_STEP_MS * an->npath + 60;
  } else if (a->kind == 'a') {
    an->unit = an->view.at[a->a];
    an->tunit = an->view.at[a->b];
    const SgUnit *u = &an->view.u[an->unit];
    sg_forecast(&an->view, u->type, u->hp, u->cell, a->b, &an->dmg, &an->back);
    an->ranged = sg_dist(a->a, a->b) > 1;
    an->kill = an->tunit >= 0 ? an->dmg >= an->view.u[an->tunit].hp : an->dmg >= an->view.castleHp[1 - u->side];
    an->die = an->back >= u->hp;
    an->ms = (SG_HIT_MS + SG_AFTER_MS) * (an->back > 0 && !an->kill ? 2 : 1);
  } else {
    an->ms = SG_BUY_MS;
  }
  an->gap = an->opp ? SG_OPP_GAP : 0;
  an->t0 = GetTickCount64();
}

static void sg_anim_finish(void) { g_sgAnim.on = FALSE; }

/* показать действия acts, начиная с позиции from */
static void sg_anim_play(const SgState *from, const SgAct *acts, int n, BOOL opp) {
  SgAnim *an = &g_sgAnim;
  an->on = FALSE;
  if (!g_sgWnd || !IsWindowVisible(g_sgWnd) || IsIconic(g_sgWnd) || n <= 0) return;
  if (n > SG_MAXACTS) n = SG_MAXACTS;
  an->view = *from;
  memcpy(an->q, acts, sizeof(SgAct) * (size_t)n);
  an->n = n;
  an->i = 0;
  an->opp = opp;
  an->on = TRUE;
  sg_anim_prepare();
  if (an->on) SetTimer(g_sgWnd, SG_TIMER, 15, NULL);
}

/* моё действие: показать (или поставить в очередь за ещё идущим) */
static void sg_anim_push(const SgState *before, const SgAct *a) {
  SgAnim *an = &g_sgAnim;
  if (an->on && !an->opp && an->n < SG_MAXACTS) {
    an->q[an->n++] = *a;
    return;
  }
  sg_anim_play(before, a, 1, FALSE);
}

static void sg_anim_tick(void) {
  SgAnim *an = &g_sgAnim;
  if (!an->on) return;
  if (sg_anim_t() < an->ms + an->gap) return;
  sg_apply(&an->view, &an->q[an->i]);
  an->i++;
  sg_anim_prepare();
}

static const SgState *sg_shown(void) { return g_sgAnim.on ? &g_sgAnim.view : &g_sg.cur; }

/* ---- партия по записям ------------------------------------------------------- */

static const wchar_t *sg_game_id(void) { return g_sg.solo ? g_sg.soloGame : g_sgn.game; }
static int sg_game_first(void) { return g_sg.solo ? (int)((sg_seed(g_sg.soloGame) >> 9) & 1) : gn_game_bit(g_sgn.game); }

static void sg_over(int winner, const wchar_t *big, const wchar_t *why) {
  g_sgn.mode = GN_OVER;
  g_sg.winner = winner;
  lstrcpynW(g_sg.endBig, big ? big : L"", 40);
  lstrcpynW(g_sg.endWhy, why ? why : L"", 200);
  if (!g_sg.solo) gn_save_local(&g_sgn);
}

/* Разыграть партию с начала по записям сторон 0 и 1; мои неотправленные
   действия этого хода — поверх. */
static void sg_replay_files(const wchar_t *f0, const wchar_t *f1) {
  const wchar_t *file[2] = {f0, f1};
  int me = sg_me();
  SgState st;
  sg_start(&st, sg_seed(sg_game_id()), sg_game_first());
  g_sg.lastN = g_sg.lastTurn = 0;
  g_sg.needCommit = FALSE;
  BOOL stopped = FALSE;
  int resigned = -1;
  for (;;) {
    sg_begin_turn(&st);
    if (st.over) break;
    int side = sg_side(&st);
    char line[SG_LINE];
    if (!gn_num_line(file[side], L"m", st.turn, line, SG_LINE)) {
      /* этот ход ещё не сделан; сдались — партия кончена на этой позиции */
      if (nd_has_line(file[0], L"resign") || nd_has_line(file[1], L"resign")) {
        resigned = nd_has_line(file[0], L"resign") ? 0 : 1;
        break;
      }
      if (side == me) {
        if (g_sg.pendTurn != st.turn) g_sg.pend[0] = 0, g_sg.pendTurn = st.turn;
        SgState mine = st;
        if (sg_apply_line(&mine, g_sg.pend, NULL, 0) < 0) g_sg.pend[0] = 0; /* не от этой позиции — забыть */
        else st = mine;
        if (st.over) g_sg.needCommit = TRUE;
      }
      break;
    }
    SgState before = st;
    SgAct acts[SG_MAXACTS];
    int k = sg_apply_line(&st, line, acts, SG_MAXACTS);
    if (k < 0) {
      stopped = TRUE;
      break;
    }
    g_sg.lastBefore = before;
    g_sg.lastN = k > SG_MAXACTS ? SG_MAXACTS : k;
    memcpy(g_sg.lastActs, acts, sizeof(SgAct) * (size_t)g_sg.lastN);
    g_sg.lastTurn = before.turn;
    g_sg.lastSide = side;
    if (st.over) break;
    sg_end_turn(&st);
  }
  g_sg.cur = st;
  g_sg.have = TRUE;
  if (g_sg.sel >= 0 && !st.u[g_sg.sel].alive) g_sg.sel = -1;
  if (stopped) {
    wchar_t why[200];
    _snwprintf(why, 200, L"ход %d не прошёл проверку правил", st.turn);
    why[199] = 0;
    sg_over(-1, L"Партия остановлена", why);
  } else if (resigned >= 0) {
    sg_over(1 - resigned, NULL, resigned == me ? L"вы сдались" : L"соперник сдался");
  } else if (st.over == SG_CASTLE_FELL && !g_sg.needCommit) {
    sg_over(st.winner, NULL, st.winner == me ? L"замок соперника разрушен" : L"ваш замок разрушен");
  } else if (st.over == SG_LIMIT) {
    wchar_t why[200];
    if (st.castleHp[0] != st.castleHp[1])
      _snwprintf(why, 200, L"ходы вышли — замок целее: %d против %d", st.castleHp[st.winner], st.castleHp[1 - st.winner]);
    else if (st.winner >= 0)
      _snwprintf(why, 200, L"ходы вышли, замки равны — деревень больше: %d против %d", sg_villages(&st, st.winner),
                 sg_villages(&st, 1 - st.winner));
    else _snwprintf(why, 200, L"ходы вышли: замки и деревни поровну");
    why[199] = 0;
    sg_over(st.winner, st.winner < 0 ? L"Ничья" : NULL, why);
  }
}

/* по файлам общей папки (свой — из памяти: только что сделанный ход папка
   могла ещё не вернуть) */
static void sg_replay_net(const wchar_t *p0, const wchar_t *p1) {
  GnNet *n = &g_sgn;
  const wchar_t *mine = gn_mine(n), *theirs = gn_theirs(n, p0, p1);
  const wchar_t *file[2];
  file[n->me] = mine;
  file[1 - n->me] = theirs;
  if (!gn_replay_head(n, theirs)) return;
  if (!gn_accepted(file[1])) { /* принявший ещё не ответил */
    n->mode = GN_WAIT;
    gn_wait_status(n);
    return;
  }
  if (n->mode == GN_WAIT) n->mode = GN_PLAY;
  sg_replay_files(file[0], file[1]);
}

static void sg_select(int ui);
static void sg_commit_turn(void);

static void sg_turn_status(void) {
  if (g_sgn.mode != GN_PLAY || !g_sg.have) return;
  const SgState *st = &g_sg.cur;
  int me = sg_me();
  if (!sg_my_turn()) {
    if (g_sg.solo) sg_status(L"Ходит компьютер…");
    else if (!gn_opp_online(&g_sgn))
      sg_status(L"Ходит %s — он сейчас не в сети: ход увидит, когда запустит CursorPad", g_sgn.oppName);
    else sg_status(L"Ходит %s…", g_sgn.oppName);
    return;
  }
  if (g_sg.buy >= 0) {
    sg_status(L"Куда поставить? Отмечены клетки у вашего замка. Правая кнопка — передумать");
    return;
  }
  if (g_sg.sel >= 0 && st->u[g_sg.sel].alive && st->u[g_sg.sel].side == me && sg_unit_can_act(st, g_sg.sel)) {
    const SgUnit *u = &st->u[g_sg.sel];
    BOOL tg = sg_has_target(st, g_sg.sel);
    if (!u->moved) sg_status(tg ? L"Куда? Светлые клетки — пройти, красные обводки — ударить" : L"Куда? Светлые клетки — пройти");
    else sg_status(L"Ударить? Красные обводки — кого достаёт");
    return;
  }
  int can = 0;
  for (int i = 0; i < SG_MAXU; i++) can += sg_unit_can_act(st, i);
  BOOL canBuy = st->gold[me] >= kSgKind[SGU_SWORD].cost && sg_count(st, me) < SG_SIDE_UNITS;
  if (can) sg_status(L"Ваш ход: щёлкните свой отряд%s. Свободных отрядов: %d", canBuy ? L" или наймите нового справа" : L"", can);
  else if (canBuy) sg_status(L"Все отряды сходили. Можно нанять ещё — или «Завершить ход»");
  else sg_status(L"Все отряды сходили — нажмите «Завершить ход»");
}

static void sg_after_replay(int prevTurn, int prevMode) {
  GnNet *n = &g_sgn;
  int me = sg_me();
  if (g_sg.needCommit) { /* мои действия уже разрушили замок — запись должна уйти */
    g_sg.needCommit = FALSE;
    sg_commit_turn();
    return;
  }
  if (!g_sg.solo && prevMode == GN_WAIT && n->mode == GN_PLAY) gn_save_local(n); /* приняли — после перезапуска сразу игра */
  BOOL going = n->mode == GN_PLAY || n->mode == GN_OVER;
  if (going && g_sg.have && (g_sg.cur.turn != prevTurn || prevMode != n->mode)) {
    g_sg.sel = g_sg.buy = -1;
    memset(g_sg.reach, 255, sizeof(g_sg.reach));
  }
  /* соперник сходил — показать его ход движением (окно скрыто — покажем, когда откроют) */
  if (going && g_sg.lastN > 0 && g_sg.lastSide != me && g_sg.lastTurn > g_sg.shownTurn && g_sgWnd &&
      IsWindowVisible(g_sgWnd) && !IsIconic(g_sgWnd)) {
    g_sg.shownTurn = g_sg.lastTurn;
    sg_anim_play(&g_sg.lastBefore, g_sg.lastActs, g_sg.lastN, TRUE);
  }
  if (n->mode == GN_PLAY) {
    sg_turn_status();
    if (sg_my_turn() && g_sg.notifiedTurn != g_sg.cur.turn && !gn_in_front(n)) {
      g_sg.notifiedTurn = g_sg.cur.turn;
      wchar_t t[200];
      if (g_sg.cur.turn <= 2)
        _snwprintf(t, 200, L"Партия с %s: вы — %s, ваш ход", sg_opp_name(), kSgSideName[me]);
      else _snwprintf(t, 200, L"%s сходил — ваш ход (ход %d из %d)", sg_opp_name(), (g_sg.cur.turn + 1) / 2, SG_TURNS / 2);
      t[199] = 0;
      if (!g_sg.solo) gn_balloon(n, t);
    }
  }
  if (n->mode == GN_OVER && prevMode != GN_OVER) {
    wchar_t t[260];
    if (g_sg.winner == me) _snwprintf(t, 260, L"Вы выиграли у %s — %s", sg_opp_name(), g_sg.endWhy);
    else if (g_sg.winner >= 0) _snwprintf(t, 260, L"Выиграл %s — %s", sg_opp_name(), g_sg.endWhy);
    else _snwprintf(t, 260, L"%s — %s", g_sg.endBig, g_sg.endWhy);
    t[259] = 0;
    sg_status(L"%s", t);
    if (!g_sg.solo && !gn_in_front(n)) gn_balloon(n, t);
  }
  sg_refresh_view();
}

/* разыграть заново по тому, что есть (после своего действия, отмены, сдачи) */
static void sg_replay_now(void) {
  int prevTurn = g_sg.have ? g_sg.cur.turn : 0, prevMode = g_sgn.mode;
  if (g_sg.solo) sg_replay_files(g_sg.soloLog[0].w, g_sg.soloLog[1].w);
  else {
    GnPoll *pl = g_sgn.poll;
    sg_replay_net(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  }
  sg_after_replay(prevTurn, prevMode);
}

static void sg_on_poll(GnPoll *pl) {
  int prevTurn = g_sg.have ? g_sg.cur.turn : 0, prevMode = g_sgn.mode;
  if (!gn_on_poll(&g_sgn, pl)) return; /* игра с компьютером сюда не доходит: у неё нет партии в папке */
  if (g_sg.solo) return;
  sg_replay_net(pl->p0, pl->p1);
  sg_after_replay(prevTurn, prevMode);
}

/* ---- действия игрока -------------------------------------------------------- */

static void sg_reset_game(void) {
  g_sg.have = FALSE;
  g_sg.pend[0] = 0;
  g_sg.pendTurn = 0;
  g_sg.lastN = g_sg.lastTurn = g_sg.shownTurn = 0;
  g_sg.sel = g_sg.buy = g_sg.hover = g_sg.hoverBuy = -1;
  g_sg.winner = -1;
  g_sg.notifiedTurn = 0;
  g_sg.needCommit = FALSE;
  memset(g_sg.reach, 255, sizeof(g_sg.reach));
  g_sgAnim.on = FALSE;
}

/* начальная позиция партии — пока файлы не прочитаны */
static void sg_first_position(void) {
  sg_start(&g_sg.cur, sg_seed(sg_game_id()), sg_game_first());
  sg_begin_turn(&g_sg.cur);
  g_sg.have = TRUE;
}

/* новая партия по сети (позвал или принял) */
static void sg_fresh(void) {
  g_sg.solo = FALSE;
  sg_reset_game();
  sg_first_position();
  g_sg.pendTurn = g_sg.cur.turn;
}

/* мои неотправленные действия — в strategiya.txt рядом с заметками */
static void sg_extra(wchar_t *out, int cap) {
  out[0] = 0;
  if (g_sg.solo || !g_sg.pend[0]) return;
  wchar_t w[SG_LINE];
  MultiByteToWideChar(CP_UTF8, 0, g_sg.pend, -1, w, SG_LINE);
  _snwprintf(out, cap, L"pt\t%d\npa\t%s\n", g_sg.pendTurn, w);
  out[cap - 1] = 0;
}

static void sg_solo_schedule(void) {
  if (g_sg.solo && g_sgWnd && g_sgn.mode == GN_PLAY && g_sg.have && !g_sg.cur.over && sg_side(&g_sg.cur) == 1)
    SetTimer(g_sgWnd, SG_TIMER_BOT, 650, NULL);
}

/* мой ход завершён — запись в файл и разыграть заново */
static void sg_commit_turn(void) {
  if (g_sgn.mode != GN_PLAY || !g_sg.have || sg_side(&g_sg.cur) != sg_me()) return;
  wchar_t w[SG_LINE];
  MultiByteToWideChar(CP_UTF8, 0, g_sg.pend[0] ? g_sg.pend : "-", -1, w, SG_LINE);
  int turn = g_sg.cur.turn;
  g_sg.pend[0] = 0;
  g_sg.pendTurn = 0;
  g_sg.sel = g_sg.buy = -1;
  if (g_sg.solo) sb_add(&g_sg.soloLog[0], L"m\t%d\t%s\n", turn, w);
  else {
    sb_add(&g_sgn.log, L"m\t%d\t%s\n", turn, w);
    BOOL ok = gn_write_mine(&g_sgn);
    gn_save_local(&g_sgn);
    if (!ok) sg_status(L"Не удалось записать ход в общую папку — повторю сам");
  }
  sg_replay_now();
  sg_solo_schedule();
}

/* действие моего хода: выполнить, запомнить, показать */
static BOOL sg_do(const SgAct *a) {
  if (!sg_my_turn()) return FALSE;
  SgState before = g_sg.cur;
  if (!sg_apply(&g_sg.cur, a)) return FALSE;
  char t[8];
  int l = sg_act_str(a, t), have = (int)strlen(g_sg.pend);
  if (have + l + 2 >= SG_LINE) {
    g_sg.cur = before;
    return FALSE;
  }
  if (have) g_sg.pend[have++] = '.';
  memcpy(g_sg.pend + have, t, (size_t)l + 1);
  g_sg.pendTurn = g_sg.cur.turn;
  sg_anim_push(&before, a);
  if (!g_sg.solo) gn_save_local(&g_sgn);
  if (g_sg.cur.over) sg_commit_turn(); /* замок разрушен — ход уходит сразу */
  return TRUE;
}

static void sg_undo(void) {
  if (!sg_my_turn() || !g_sg.pend[0]) return;
  char *dot = strrchr(g_sg.pend, '.');
  if (dot) *dot = 0;
  else g_sg.pend[0] = 0;
  sg_anim_finish();
  g_sg.sel = g_sg.buy = -1;
  if (!g_sg.solo) gn_save_local(&g_sgn);
  sg_replay_now();
}

static void sg_end_turn_click(void) {
  if (!sg_my_turn()) return;
  if (!g_sg.pend[0] &&
      MessageBoxW(g_sgWnd, L"Вы ничего не сделали в этот ход. Завершить его?", L"Стратегия", MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;
  sg_anim_finish();
  sg_commit_turn();
}

static void sg_resign(void) {
  if (g_sg.solo || !gn_resign(&g_sgn)) return;
  sg_replay_now();
}

/* повторить ход соперника */
static void sg_replay_last(void) {
  if (g_sg.lastN <= 0 || g_sg.lastSide == sg_me()) return;
  g_sg.sel = g_sg.buy = -1;
  sg_anim_play(&g_sg.lastBefore, g_sg.lastActs, g_sg.lastN, TRUE);
  sg_refresh_view();
}

static void sg_solo_free(void) {
  for (int i = 0; i < 2; i++) {
    free(g_sg.soloLog[i].w);
    memset(&g_sg.soloLog[i], 0, sizeof(g_sg.soloLog[i]));
  }
  if (g_sgWnd) KillTimer(g_sgWnd, SG_TIMER_BOT);
  g_sg.solo = FALSE;
}

/* к списку игроков (из игры с компьютером — в любой момент) */
static void sg_leave(void) {
  if (g_sg.solo && g_sgn.mode == GN_PLAY &&
      MessageBoxW(g_sgWnd, L"Закончить игру с компьютером?", L"Стратегия", MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;
  sg_solo_free();
  sg_reset_game();
  gn_back_to_lobby(&g_sgn);
}

static void sg_solo_start(void) {
  if (g_sgn.mode != GN_LOBBY) return;
  sg_solo_free();
  sg_reset_game();
  g_sg.solo = TRUE;
  _snwprintf(g_sg.soloGame, 40, L"solo-%llx", GetTickCount64() ^ ((ULONGLONG)GetCurrentProcessId() << 20));
  g_sg.soloGame[39] = 0;
  sb_add(&g_sg.soloLog[0], L"name\tя\n");
  sb_add(&g_sg.soloLog[1], L"name\tкомпьютер\n");
  g_sgn.me = 0;
  g_sgn.mode = GN_PLAY;
  lstrcpynW(g_sgn.oppName, L"Компьютер", 128);
  g_sgn.oppId[0] = 0;
  sg_replay_now();
  sg_solo_schedule();
}

/* компьютер ходит: его запись — как запись соперника */
static void sg_solo_bot(void) {
  if (!g_sg.solo || g_sgn.mode != GN_PLAY || !g_sg.have || g_sg.cur.over || sg_side(&g_sg.cur) != 1) return;
  SgState st = g_sg.cur;
  char line[SG_LINE];
  sg_bot_turn(&st, line, SG_LINE);
  wchar_t w[SG_LINE];
  MultiByteToWideChar(CP_UTF8, 0, line, -1, w, SG_LINE);
  sb_add(&g_sg.soloLog[1], L"m\t%d\t%s\n", g_sg.cur.turn, w);
  sg_replay_now();
}

/* ---- размеры ---------------------------------------------------------------- */

#define SG_TOP 46   /* под шапкой окна */
#define SG_LEFT 14
#define SG_FRAME 14 /* рама карты */
#define SG_PAD 6    /* от рамы до клеток */
#define SG_RAD 28   /* радиус шестиугольника */
#define SG_SIDE 236 /* полоса справа */
#define SG_BELOW 124 /* под картой: строка состояния, кнопки, «связь» */

static float sg_R(void) { return SG_RAD * g_sgS; }
static int sg_field_w(void) { return (int)(1.7320508f * sg_R() * SG_W + 0.5f); }
static int sg_field_h(void) { return (int)(sg_R() * (1.5f * (SG_H - 1) + 2) + 0.5f); }
static int sg_bx(void) { return GS_(SG_LEFT); }
static int sg_by(void) { return GS_(SG_TOP); }
static int sg_board_w(void) { return sg_field_w() + 2 * (GS_(SG_FRAME) + GS_(SG_PAD)); }
static int sg_board_h(void) { return sg_field_h() + 2 * (GS_(SG_FRAME) + GS_(SG_PAD)); }
static int sg_side_x(void) { return sg_bx() + sg_board_w() + GS_(14); }
static int sg_below_y(void) { return sg_by() + sg_board_h() + GS_(10); }
static BOOL sg_flip(void) { return sg_me() == 1; } /* свой замок всегда слева */

static void sg_geo(SgGeo *g) {
  g->R = sg_R();
  g->w = 1.7320508f * g->R;
  g->ox = (float)(sg_bx() + GS_(SG_FRAME) + GS_(SG_PAD));
  g->oy = (float)(sg_by() + GS_(SG_FRAME) + GS_(SG_PAD));
  g->flip = sg_flip();
}

static int sg_hit(int x, int y) {
  SgGeo g;
  sg_geo(&g);
  return sg_geo_hit(&g, (float)x + 0.5f, (float)y + 0.5f, NULL, NULL);
}

/* ---- спрайты ---------------------------------------------------------------- */

typedef struct {
  float R;
  int flip;
  unsigned seed;
  BOOL haveMap;
  GnSprite map;
  GnSprite vil[3], cas[2];
  GnSprite tok[2][SGU_N][2]; /* [сторона][вид][тусклая] */
  GnSprite shadow, flash, shot;
  GnSprite hexMove, hexLook, hexSel, hexAtk, hexBuy, hexHover, hexOwn[2];
  int td; /* поперечник фишки */
} SgArt;

static SgArt g_sgArt;
static HFONT g_sgBig, g_sgNum, g_sgHp;

static void sg_art_build(void) {
  float R = sg_R();
  if (g_sgArt.R != R) {
    g_sgArt.R = R;
    g_sgArt.haveMap = FALSE;
    int d2 = (int)(R * 2 + 0.5f);
    for (int o = -1; o < 2; o++) {
      int oo = o;
      sg_render_fn(&g_sgArt.vil[o + 1], d2, sg_village_px, &oo);
    }
    for (int s = 0; s < 2; s++) {
      int ss = s;
      sg_render_fn(&g_sgArt.cas[s], (int)(R * 2.3f + 0.5f), sg_castle_px, &ss);
    }
    g_sgArt.td = (int)(1.7320508f * R * 0.68f + 0.5f);
    for (int s = 0; s < 2; s++)
      for (int t = 0; t < SGU_N; t++)
        for (int dm = 0; dm < 2; dm++) sg_render_token(&g_sgArt.tok[s][t][dm], g_sgArt.td, t, s, dm);
    gn_make_glow(&g_sgArt.shadow, (int)(g_sgArt.td * 0.56f), RGB(0, 0, 0), 0.50f, 0.55f);
    gn_make_ring(&g_sgArt.flash, R * 0.62f, (float)GS_(4), (float)GS_(9), RGB(255, 226, 130), 1.0f);
    gn_make_ring(&g_sgArt.shot, (float)GS_(4), (float)GS_(4), (float)GS_(3), RGB(60, 44, 36), 1.0f);
    sg_make_hex(&g_sgArt.hexMove, R, RGB(214, 246, 255), 0.36f, RGB(240, 252, 255), 0.95f, 0.07f);
    sg_make_hex(&g_sgArt.hexLook, R, RGB(255, 190, 90), 0.28f, RGB(230, 150, 50), 0.80f, 0.06f);
    sg_make_hex(&g_sgArt.hexSel, R, RGB(255, 214, 96), 0.42f, RGB(255, 196, 60), 1.0f, 0.10f);
    sg_make_hex(&g_sgArt.hexAtk, R, RGB(255, 70, 50), 0.22f, RGB(240, 52, 40), 1.0f, 0.11f);
    sg_make_hex(&g_sgArt.hexBuy, R, RGB(126, 240, 150), 0.40f, RGB(60, 200, 110), 1.0f, 0.10f);
    sg_make_hex(&g_sgArt.hexHover, R, RGB(255, 255, 255), 0.10f, RGB(255, 255, 255), 0.80f, 0.06f);
    sg_make_hex(&g_sgArt.hexOwn[0], R, RGB(60, 130, 230), 0.10f, RGB(70, 140, 240), 0.95f, 0.085f);
    sg_make_hex(&g_sgArt.hexOwn[1], R, RGB(236, 70, 60), 0.10f, RGB(240, 76, 64), 0.95f, 0.085f);
    HFONT *fonts[3] = {&g_sgBig, &g_sgNum, &g_sgHp};
    for (int i = 0; i < 3; i++)
      if (*fonts[i]) DeleteObject(*fonts[i]), *fonts[i] = NULL;
    g_sgBig = CreateFontW(-GS_(28), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_sgNum = CreateFontW(-GS_(19), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    g_sgHp = CreateFontW(-GS_(11), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
  }
  if (!g_sg.have) return;
  unsigned seed = sg_seed(sg_game_id());
  int flip = sg_flip();
  if (!g_sgArt.haveMap || g_sgArt.seed != seed || g_sgArt.flip != flip) {
    HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT)); /* карта рисуется долю секунды */
    SgGeo g;
    g.R = R;
    g.w = 1.7320508f * R;
    g.ox = g.oy = (float)(GS_(SG_FRAME) + GS_(SG_PAD));
    g.flip = flip;
    sg_render_map(&g_sgArt.map, &g_sg.cur, &g, sg_board_w(), sg_board_h(), GS_(SG_FRAME));
    g_sgArt.seed = seed;
    g_sgArt.flip = flip;
    g_sgArt.haveMap = TRUE;
    SetCursor(old);
  }
}

static void sg_blit_c(UiCanvas *c, const GnSprite *s, float cx, float cy, int alpha) {
  gn_blit(c, s, (int)floorf(cx - s->w / 2.0f + 0.5f), (int)floorf(cy - s->h / 2.0f + 0.5f), alpha);
}

/* число с тёмной обводкой — читается на любой местности */
static void sg_text_out(HDC dc, const wchar_t *t, RECT r, COLORREF col, COLORREF edge, UINT fmt) {
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, edge);
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!dx && !dy) continue;
      RECT o = r;
      OffsetRect(&o, dx, dy);
      DrawTextW(dc, t, -1, &o, fmt);
    }
  SetTextColor(dc, col);
  DrawTextW(dc, t, -1, &r, fmt);
}

/* фишка отряда; lift — приподнята (едет) */
static void sg_draw_token(UiCanvas *c, int type, int side, BOOL dim, float cx, float cy, int alpha, float lift) {
  if (alpha <= 0) return;
  sg_blit_c(c, &g_sgArt.shadow, cx + GS_(1) + lift * 0.5f, cy + GS_(3) + lift, alpha * 4 / 5);
  sg_blit_c(c, &g_sgArt.tok[side][type][dim ? 1 : 0], cx, cy - lift, alpha);
}

/* сколько сил осталось — кружок с числом у фишки (у целого отряда его нет) */
static void sg_draw_hp(UiCanvas *c, int hp, float cx, float cy) {
  if (hp >= SG_HP || hp <= 0) return;
  int r = GS_(8), x = (int)(cx + g_sgArt.td * 0.33f), y = (int)(cy + g_sgArt.td * 0.33f);
  RECT b = {x - r, y - r, x + r, y + r};
  UiShape k;
  memset(&k, 0, sizeof(k));
  k.r = -1;
  k.fill = hp <= 3 ? RGB(170, 30, 26) : RGB(24, 28, 36);
  k.fillA = 240;
  k.border = RGB(255, 255, 255);
  k.borderA = 230;
  k.bw = 1.2f;
  ui_shape(c, b, &k);
  wchar_t t[4];
  _snwprintf(t, 4, L"%d", hp);
  if (g_sgHp) SelectObject(c->dc, g_sgHp);
  SetBkMode(c->dc, TRANSPARENT);
  SetTextColor(c->dc, RGB(255, 255, 255));
  RECT tr = {b.left, b.top - 1, b.right, b.bottom - 1};
  DrawTextW(c->dc, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* прочность замка — плашка под ним: полоска и число */
static void sg_draw_castle_hp(UiCanvas *c, int side, int hp, float cx, float cy) {
  int w = GS_(40), h = GS_(15), x = (int)(cx - w / 2.0f), y = (int)(cy + g_sgArt.R * 0.50f);
  RECT b = {x, y, x + w, y + h};
  UiShape k;
  memset(&k, 0, sizeof(k));
  k.r = -1;
  k.fill = RGB(22, 26, 34);
  k.fillA = 235;
  k.border = RGB(255, 255, 255);
  k.borderA = 150;
  k.bw = 1;
  ui_shape(c, b, &k);
  int fill = (w - 4) * hp / SG_CASTLE_HP;
  if (fill > 0) {
    RECT f = {x + 2, y + 2, x + 2 + (fill < h - 4 ? h - 4 : fill), y + h - 2};
    memset(&k, 0, sizeof(k));
    k.r = -1;
    k.fill = hp * 4 <= SG_CASTLE_HP ? RGB(214, 56, 46) : hp * 2 <= SG_CASTLE_HP ? RGB(222, 150, 40) : (side ? RGB(196, 66, 58) : RGB(58, 120, 214));
    k.fillA = 255;
    ui_shape(c, f, &k);
  }
  wchar_t t[8];
  _snwprintf(t, 8, L"%d", hp);
  if (g_sgHp) SelectObject(c->dc, g_sgHp);
  SetBkMode(c->dc, TRANSPARENT);
  SetTextColor(c->dc, RGB(255, 255, 255));
  RECT tr = {b.left, b.top - 1, b.right, b.bottom - 1};
  DrawTextW(c->dc, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static float sg_ease(float t) { return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); }

/* число урона над клеткой: всплывает */
static void sg_draw_dmg(UiCanvas *c, int dmg, float cx, float cy, float f) {
  wchar_t t[8];
  _snwprintf(t, 8, L"−%d", dmg);
  if (g_sgNum) SelectObject(c->dc, g_sgNum);
  int y = (int)(cy - g_sgArt.R * 0.55f - GS_(12) * f);
  RECT r = {(int)cx - GS_(30), y - GS_(12), (int)cx + GS_(30), y + GS_(12)};
  sg_text_out(c->dc, t, r, RGB(255, 236, 130), RGB(40, 16, 10), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* ---- карта ------------------------------------------------------------------ */

static void sg_paint_anim(UiCanvas *cv, const SgGeo *geo, const SgState *v) {
  SgAnim *an = &g_sgAnim;
  const SgAct *a = &an->q[an->i];
  int t = sg_anim_t();
  if (t > an->ms) t = an->ms;
  if (a->kind == 'b') {
    float x, y;
    sg_geo_center(geo, a->b, &x, &y);
    sg_draw_token(cv, a->type, sg_side(v), FALSE, x, y, 255 * t / an->ms, 0);
    return;
  }
  const SgUnit *u = &v->u[an->unit];
  float ax, ay;
  sg_geo_center(geo, u->cell, &ax, &ay);
  if (a->kind == 'm') {
    float pos = sg_ease((float)t / an->ms) * an->npath;
    int seg = (int)pos;
    if (seg >= an->npath) seg = an->npath - 1;
    float fr = pos - seg, x0 = ax, y0 = ay, x1, y1;
    if (seg > 0) sg_geo_center(geo, an->path[seg - 1], &x0, &y0);
    sg_geo_center(geo, an->path[seg], &x1, &y1);
    sg_draw_token(cv, u->type, u->side, FALSE, x0 + (x1 - x0) * fr, y0 + (y1 - y0) * fr, 255, (float)GS_(3));
    sg_draw_hp(cv, u->hp, x0 + (x1 - x0) * fr, y0 + (y1 - y0) * fr - GS_(3));
    return;
  }
  /* удар: замах или полёт, вспышка и число; потом ответ */
  float tx, ty;
  sg_geo_center(geo, a->b, &tx, &ty);
  const SgUnit *d = an->tunit >= 0 ? &v->u[an->tunit] : NULL;
  int one = SG_HIT_MS + SG_AFTER_MS;
  BOOL second = t >= one; /* идёт ответ */
  int tt = second ? t - one : t;
  float fa = tt < SG_HIT_MS ? (float)tt / SG_HIT_MS : 1.0f;                    /* замах */
  float fb = tt < SG_HIT_MS ? 0.0f : (float)(tt - SG_HIT_MS) / SG_AFTER_MS;   /* после удара */
  float lunge = tt < SG_HIT_MS ? sinf(fa * 1.5708f) : 1.0f - sg_ease(fb);
  float aox = 0, aoy = 0, dox = 0, doy = 0;
  if (!an->ranged) {
    if (!second) aox = (tx - ax) * 0.36f * lunge, aoy = (ty - ay) * 0.36f * lunge;
    else if (d) dox = (ax - tx) * 0.36f * lunge, doy = (ay - ty) * 0.36f * lunge;
  }
  int aAlpha = 255, dAlpha = 255;
  if (an->kill) dAlpha = second ? 0 : tt < SG_HIT_MS ? 255 : (int)(255 * (1 - fb));
  if (an->die && second && tt >= SG_HIT_MS) aAlpha = (int)(255 * (1 - fb));
  if (d) {
    sg_draw_token(cv, d->type, d->side, FALSE, tx + dox, ty + doy, dAlpha, 0);
    if (dAlpha == 255) sg_draw_hp(cv, second || tt >= SG_HIT_MS ? d->hp - an->dmg : d->hp, tx + dox, ty + doy);
  }
  sg_draw_token(cv, u->type, u->side, FALSE, ax + aox, ay + aoy, aAlpha, 0);
  if (aAlpha == 255) sg_draw_hp(cv, second && tt >= SG_HIT_MS ? u->hp - an->back : u->hp, ax + aox, ay + aoy);
  /* снаряд: стрела, камень — и ответ замка */
  BOOL shot = second ? (an->ranged || !d) : an->ranged;
  if (shot && tt < SG_HIT_MS) {
    float sx = second ? tx : ax, sy = second ? ty : ay, ex = second ? ax : tx, ey = second ? ay : ty;
    float arc = sinf(fa * 3.14159f) * g_sgArt.R * (an->ranged ? 0.5f : 0.15f);
    sg_blit_c(cv, &g_sgArt.shot, sx + (ex - sx) * fa, sy + (ey - sy) * fa - arc, 255);
  }
  if (tt >= SG_HIT_MS) {
    float hx = second ? ax : tx, hy = second ? ay : ty;
    sg_blit_c(cv, &g_sgArt.flash, hx, hy, (int)(255 * (1 - fb)));
    sg_draw_dmg(cv, second ? an->back : an->dmg, hx, hy, fb);
  } else if (second) {
    sg_draw_dmg(cv, an->dmg, tx, ty, 1.0f); /* урон по цели ещё виден, пока летит ответ */
  }
}

static void sg_paint_board(HDC hdc) {
  sg_art_build();
  if (!g_sg.have || !g_sgArt.haveMap) return;
  SgGeo geo;
  sg_geo(&geo);
  int bx = sg_bx(), by = sg_by(), bw = sg_board_w(), bh = sg_board_h();
  RECT box = {bx - GS_(10), by - GS_(6), bx + bw + GS_(10), by + bh + GS_(12)};
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, hdc, box, box.left, box.top, TRUE)) return;
  for (int i = 0; i < 6; i++) { /* тень карты на окне */
    RECT r = {bx + GS_(2) - i, by + GS_(4) - i / 2, bx + bw + GS_(2) + i, by + bh + GS_(6) + i};
    UiShape k;
    memset(&k, 0, sizeof(k));
    k.r = (float)GS_(8) + i;
    k.fill = RGB(0, 0, 0);
    k.fillA = 14;
    ui_shape(&cv, r, &k);
  }
  gn_blit(&cv, &g_sgArt.map, bx, by, 255);
  BOOL anim = g_sgAnim.on;
  const SgState *v = sg_shown();
  int me = sg_me();
  BOOL my = sg_my_turn() && !anim;
  /* деревни (чья — обводка цвета хозяина) и замки */
  for (int c = 0; c < SG_CELLS; c++) {
    if (v->terr[c] != SGT_VILLAGE) continue;
    float x, y;
    sg_geo_center(&geo, c, &x, &y);
    if (v->owner[c] >= 0) sg_blit_c(&cv, &g_sgArt.hexOwn[v->owner[c]], x, y, 255);
    sg_blit_c(&cv, &g_sgArt.vil[v->owner[c] + 1], x, y, 255);
  }
  for (int s = 0; s < 2; s++) {
    float x, y;
    sg_geo_center(&geo, v->castle[s], &x, &y);
    sg_blit_c(&cv, &g_sgArt.cas[s], x, y - geo.R * 0.10f, v->castleHp[s] > 0 ? 255 : 90);
    sg_draw_castle_hp(&cv, s, v->castleHp[s], x, y);
  }
  /* подсветка: куда поставить купленного; выбранный, куда дойдёт, кого достанет */
  const SgState *st = &g_sg.cur;
  if (my && g_sg.buy >= 0) {
    int nb[6], k = sg_neighbors(st->castle[me], nb);
    for (int i = 0; i < k; i++) {
      if (!sg_can_buy_at(st, me, nb[i])) continue;
      float x, y;
      sg_geo_center(&geo, nb[i], &x, &y);
      sg_blit_c(&cv, &g_sgArt.hexBuy, x, y, nb[i] == g_sg.hover ? 255 : 190);
    }
  }
  if (!anim && g_sg.sel >= 0 && st->u[g_sg.sel].alive && g_sgn.mode == GN_PLAY) {
    const SgUnit *u = &st->u[g_sg.sel];
    BOOL mine = my && u->side == me;
    float x, y;
    sg_geo_center(&geo, u->cell, &x, &y);
    sg_blit_c(&cv, mine ? &g_sgArt.hexSel : &g_sgArt.hexLook, x, y, 255);
    for (int c = 0; c < SG_CELLS; c++) {
      if (g_sg.reach[c] == 255 || c == u->cell || st->at[c] >= 0) continue;
      sg_geo_center(&geo, c, &x, &y);
      sg_blit_c(&cv, mine ? &g_sgArt.hexMove : &g_sgArt.hexLook, x, y, c == g_sg.hover ? 255 : 170);
    }
    if (mine) {
      /* мышь над клеткой, куда можно пройти, — кого достанем оттуда (бледно) */
      int from = -1;
      if (g_sg.hover >= 0 && g_sg.reach[g_sg.hover] != 255 && st->at[g_sg.hover] < 0 && u->type != SGU_CATA) from = g_sg.hover;
      for (int c = 0; c < SG_CELLS; c++) {
        if (!sg_cell_ok(c)) continue;
        int o = st->at[c];
        BOOL enemy = (o >= 0 && st->u[o].side != me) || c == st->castle[1 - me];
        if (!enemy) continue;
        BOOL now = sg_can_attack(st, g_sg.sel, c);
        BOOL then = !now && from >= 0 && !u->done && sg_in_range(st, u->type, from, c);
        if (!now && !then) continue;
        sg_geo_center(&geo, c, &x, &y);
        sg_blit_c(&cv, &g_sgArt.hexAtk, x, y, now ? (c == g_sg.hover ? 255 : 215) : 110);
      }
    }
  }
  if (!anim && g_sg.hover >= 0 && g_sgn.mode == GN_PLAY) {
    float x, y;
    sg_geo_center(&geo, g_sg.hover, &x, &y);
    sg_blit_c(&cv, &g_sgArt.hexHover, x, y, 255);
  }
  /* отряды — сверху вниз по экрану */
  BOOL dimMine = g_sgn.mode == GN_PLAY && sg_side(v) == me && !v->over;
  for (int row = 0; row < SG_H; row++)
    for (int ui = 0; ui < SG_MAXU; ui++) {
      const SgUnit *u = &v->u[ui];
      if (!u->alive || sg_row(geo.flip ? sg_mirror(u->cell) : u->cell) != row) continue;
      if (anim && (ui == g_sgAnim.unit || ui == g_sgAnim.tunit)) continue;
      float x, y;
      sg_geo_center(&geo, u->cell, &x, &y);
      BOOL dim = dimMine && u->side == me && !sg_unit_can_act(v, ui);
      sg_draw_token(&cv, u->type, u->side, dim, x, y, 255, 0);
      sg_draw_hp(&cv, u->hp, x, y);
    }
  if (anim) sg_paint_anim(&cv, &geo, v);
  /* итог партии — полосой поперёк карты */
  if (g_sgn.mode == GN_OVER && !anim) {
    RECT band = {bx + GS_(SG_FRAME), by + bh / 2 - GS_(46), bx + bw - GS_(SG_FRAME), by + bh / 2 + GS_(46)};
    gn_tint(&cv, band, RGB(16, 12, 10), 185);
    RECT l1 = {band.left, band.top, band.right, band.top + GS_(2)}, l2 = {band.left, band.bottom - GS_(2), band.right, band.bottom};
    gn_tint(&cv, l1, RGB(214, 170, 88), 200);
    gn_tint(&cv, l2, RGB(214, 170, 88), 200);
    BOOL won = g_sg.winner == me;
    const wchar_t *big = g_sg.winner < 0 ? g_sg.endBig : won ? L"Победа!" : L"Поражение";
    if (g_sgBig) SelectObject(cv.dc, g_sgBig);
    SetTextColor(cv.dc, won ? RGB(255, 214, 110) : RGB(240, 232, 220));
    RECT t1 = {band.left, band.top + GS_(8), band.right, band.top + GS_(50)};
    DrawTextW(cv.dc, big, -1, &t1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (g_fontSmall) SelectObject(cv.dc, g_fontSmall);
    SetTextColor(cv.dc, RGB(220, 206, 186));
    RECT t2 = {band.left + GS_(10), band.top + GS_(52), band.right - GS_(10), band.bottom - GS_(6)};
    DrawTextW(cv.dc, g_sg.endWhy, -1, &t2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  ui_canvas_end(&cv, hdc);
}

/* ---- справа: игроки, подсказка, «Нанять» -------------------------------------- */

#define SG_CARD_H 88
#define SG_BUY_H 126

static COLORREF sg_side_col(int side) { return side ? RGB(196, 60, 52) : RGB(52, 112, 204); }

static RECT sg_card_rect(BOOL mineCard) {
  int x = sg_side_x(), y0 = sg_by(), h = sg_board_h();
  RECT r = {x, mineCard ? y0 + h - GS_(SG_CARD_H) : y0, x + GS_(SG_SIDE), mineCard ? y0 + h : y0 + GS_(SG_CARD_H)};
  return r;
}

static RECT sg_buy_card(void) {
  RECT me = sg_card_rect(TRUE);
  RECT r = {me.left, me.top - GS_(8) - GS_(SG_BUY_H), me.right, me.top - GS_(8)};
  return r;
}

static RECT sg_info_card(void) {
  RECT opp = sg_card_rect(FALSE), buy = sg_buy_card();
  RECT r = {opp.left, opp.bottom + GS_(8) + GS_(30) + GS_(8), opp.right, buy.top - GS_(8)};
  return r;
}

/* клеточка вида отряда в «Нанять» */
static void sg_buy_rect(int t, RECT *r) {
  RECT c = sg_buy_card();
  int pad = GS_(8), gap = GS_(4), w = (c.right - c.left - pad * 2 - gap * (SGU_N - 1)) / SGU_N;
  r->left = c.left + pad + t * (w + gap);
  r->right = r->left + w;
  r->top = c.top + GS_(28);
  r->bottom = r->top + GS_(62);
}

static int sg_buy_hit(int x, int y) {
  if (g_sgn.mode != GN_PLAY) return -1;
  POINT p = {x, y};
  for (int t = 0; t < SGU_N; t++) {
    RECT r;
    sg_buy_rect(t, &r);
    if (PtInRect(&r, p)) return t;
  }
  return -1;
}

static BOOL sg_can_buy(int t) {
  const SgState *st = &g_sg.cur;
  int me = sg_me();
  if (!sg_my_turn() || st->gold[me] < kSgKind[t].cost || sg_count(st, me) >= SG_SIDE_UNITS) return FALSE;
  int nb[6], k = sg_neighbors(st->castle[me], nb);
  for (int i = 0; i < k; i++)
    if (sg_can_buy_at(st, me, nb[i])) return TRUE;
  return FALSE;
}

static void sg_card(UiCanvas *cv, RECT r, COLORREF fill, COLORREF border, float bw) {
  UiShape k;
  memset(&k, 0, sizeof(k));
  k.r = (float)GS_(10);
  k.fill = fill;
  k.fillA = 255;
  k.border = border;
  k.borderA = 255;
  k.bw = bw;
  ui_shape(cv, r, &k);
}

/* карточка игрока: цвет, имя, замок, золото, деревни, отряды */
static void sg_paint_player(UiCanvas *cv, RECT r, int side, const wchar_t *name, const wchar_t *sub, BOOL turn) {
  const SgState *v = sg_shown();
  sg_card(cv, r, COL_PAPER_DARK, turn ? COL_SAGE : COL_LINE, turn ? (float)GS_(2) : 1.0f);
  int pad = GS_(10);
  RECT dot = {r.left + pad, r.top + GS_(9), r.left + pad + GS_(14), r.top + GS_(23)};
  UiShape k;
  memset(&k, 0, sizeof(k));
  k.r = -1;
  k.fill = sg_side_col(side);
  k.fillA = 255;
  ui_shape(cv, dot, &k);
  SetBkMode(cv->dc, TRANSPARENT);
  if (g_fontSmall) SelectObject(cv->dc, g_fontSmall);
  SetTextColor(cv->dc, turn ? COL_SAGE : COL_MUTED);
  SIZE ss = {0, 0};
  GetTextExtentPoint32W(cv->dc, sub, (int)wcslen(sub), &ss);
  RECT ts = {r.right - pad - ss.cx, r.top + GS_(7), r.right - pad, r.top + GS_(25)};
  DrawTextW(cv->dc, sub, -1, &ts, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (g_fontUi) SelectObject(cv->dc, g_fontUi);
  SetTextColor(cv->dc, COL_INK);
  RECT tn = {dot.right + GS_(7), r.top + GS_(7), ts.left - GS_(6), r.top + GS_(25)};
  DrawTextW(cv->dc, name, -1, &tn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  /* замок: полоска прочности и число */
  if (g_fontSmall) SelectObject(cv->dc, g_fontSmall);
  wchar_t t[80];
  _snwprintf(t, 80, L"замок %d из %d", v->castleHp[side], SG_CASTLE_HP);
  SetTextColor(cv->dc, COL_MUTED);
  RECT tc = {r.left + pad, r.top + GS_(28), r.right - pad, r.top + GS_(44)};
  DrawTextW(cv->dc, t, -1, &tc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  SIZE cs = {0, 0};
  GetTextExtentPoint32W(cv->dc, L"замок 24 из 24", 14, &cs);
  RECT bar = {r.left + pad, r.top + GS_(33), r.right - pad - cs.cx - GS_(8), r.top + GS_(39)};
  memset(&k, 0, sizeof(k));
  k.r = -1;
  k.fill = COL_LINE;
  k.fillA = 255;
  ui_shape(cv, bar, &k);
  int fw = (bar.right - bar.left) * v->castleHp[side] / SG_CASTLE_HP;
  if (fw > 0) {
    RECT f = {bar.left, bar.top, bar.left + (fw < GS_(6) ? GS_(6) : fw), bar.bottom};
    k.fill = sg_side_col(side);
    ui_shape(cv, f, &k);
  }
  _snwprintf(t, 80, L"золото %d (+%d в ход)", v->gold[side], sg_income(v, side));
  t[79] = 0;
  RECT tg = {r.left + pad, r.top + GS_(46), r.right - pad, r.top + GS_(63)};
  SetTextColor(cv->dc, COL_INK);
  DrawTextW(cv->dc, t, -1, &tg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  _snwprintf(t, 80, L"деревень %d · отрядов %d из %d", sg_villages(v, side), sg_count(v, side), SG_SIDE_UNITS);
  t[79] = 0;
  RECT tv = {r.left + pad, r.top + GS_(64), r.right - pad, r.top + GS_(81)};
  SetTextColor(cv->dc, COL_MUTED);
  DrawTextW(cv->dc, t, -1, &tv, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static const wchar_t *sg_terr_text(const SgState *st, int cell, wchar_t *buf, int cap) {
  switch (st->terr[cell]) {
  case SGT_FOREST:
    return L"Лес. Ход 2 (всаднику 3). Защита: урон по отряду в лесу меньше на 20 %.";
  case SGT_HILL:
    return L"Холм. Ход 2. Защита 30 %. Лучник и катапульта с холма бьют на клетку дальше.";
  case SGT_MOUNT:
    return L"Горы. Не пройти.";
  case SGT_WATER:
    return L"Вода. Не пройти.";
  case SGT_VILLAGE:
    _snwprintf(buf, cap, L"Деревня (%s). Хозяину +2 золота в ход, его отряд здесь лечится на 2. Защита 20 %%. Занять — встать на неё.",
               st->owner[cell] < 0 ? L"ничья" : st->owner[cell] == sg_me() ? L"ваша" : L"соперника");
    buf[cap - 1] = 0;
    return buf;
  case SGT_CASTLE:
    _snwprintf(buf, cap, L"Замок (%s): прочность %d из %d. Разрушен — партия проиграна. Вплотную отвечает на удар: −2. Новые отряды — на клетки вокруг.",
               st->owner[cell] == sg_me() ? L"ваш" : L"соперника", st->castleHp[st->owner[cell]], SG_CASTLE_HP);
    buf[cap - 1] = 0;
    return buf;
  }
  return L"Поле. Ход 1, защиты нет.";
}

/* подсказка: что под мышью, прогноз удара, кого нанимаем */
static void sg_paint_info(UiCanvas *cv, RECT r) {
  sg_card(cv, r, COL_PAPER, COL_LINE, 1.0f);
  const SgState *st = &g_sg.cur;
  int me = sg_me(), pad = GS_(10);
  wchar_t title[120] = L"", body[400] = L"", f1[120] = L"", f2[120] = L"", buf[300];
  COLORREF c1 = COL_INK, c2 = COL_MUTED;
  int showType = g_sg.hoverBuy >= 0 ? g_sg.hoverBuy : g_sg.buy;
  int hc = g_sgAnim.on ? -1 : g_sg.hover;
  int hu = hc >= 0 ? st->at[hc] : -1;
  if (showType >= 0) {
    _snwprintf(title, 120, L"%s — %d золота", kSgName[showType], kSgKind[showType].cost);
    lstrcpynW(body, kSgAbout[showType], 400);
    if (g_sg.buy >= 0 && g_sg.hoverBuy < 0) lstrcpynW(f1, L"Поставьте на клетку у замка", 120);
  } else if (g_sg.sel >= 0 && hc >= 0 && sg_my_turn() && st->u[g_sg.sel].side == me && sg_can_attack(st, g_sg.sel, hc)) {
    /* прогноз удара выбранным отрядом по цели под мышью */
    const SgUnit *u = &st->u[g_sg.sel];
    int dmg, back;
    sg_forecast(st, u->type, u->hp, u->cell, hc, &dmg, &back);
    if (hu >= 0) {
      const SgUnit *d = &st->u[hu];
      _snwprintf(title, 120, L"%s бьёт %s", kSgName[u->type], kSgWhom[d->type]);
      if (dmg >= d->hp) _snwprintf(f1, 120, L"Удар −%d: разобьёт", dmg);
      else _snwprintf(f1, 120, L"Удар −%d: останется %d", dmg, d->hp - dmg);
      if (sg_defense(st->terr[hc])) _snwprintf(body, 400, L"Цель под защитой местности: урон меньше на %d %%.", sg_defense(st->terr[hc]) * 10);
    } else {
      _snwprintf(title, 120, L"%s бьёт по замку", kSgName[u->type]);
      if (dmg >= st->castleHp[1 - me]) _snwprintf(f1, 120, L"Удар −%d: замок разрушен!", dmg);
      else _snwprintf(f1, 120, L"Удар −%d: останется %d", dmg, st->castleHp[1 - me] - dmg);
    }
    c1 = RGB(204, 52, 40);
    if (!back) lstrcpynW(f2, L"Ответа не будет", 120);
    else if (back >= u->hp) _snwprintf(f2, 120, L"Ответ −%d: ваш отряд разобьют", back);
    else _snwprintf(f2, 120, L"Ответ −%d: у вас останется %d", back, u->hp - back);
    if (back >= u->hp) c2 = RGB(204, 52, 40);
  } else if (hu >= 0 || (hc < 0 && g_sg.sel >= 0 && st->u[g_sg.sel].alive)) {
    const SgUnit *u = &st->u[hu >= 0 ? hu : g_sg.sel];
    _snwprintf(title, 120, L"%s — %s", kSgName[u->type], u->side == me ? L"ваш" : L"соперника");
    _snwprintf(body, 400, L"Сил %d из %d. %s", u->hp, SG_HP, kSgAbout[u->type]);
    if (sg_defense(st->terr[u->cell])) _snwprintf(f2, 120, L"Стоит под защитой: урон по нему −%d %%", sg_defense(st->terr[u->cell]) * 10);
  } else if (hc >= 0) {
    static const wchar_t *kT[] = {L"Поле", L"Лес", L"Холм", L"Горы", L"Вода", L"Деревня", L"Замок"};
    lstrcpynW(title, kT[st->terr[hc] < 7 ? st->terr[hc] : 0], 120);
    lstrcpynW(body, sg_terr_text(st, hc, buf, 300), 400);
  } else {
    lstrcpynW(title, L"Подсказка", 120);
    lstrcpynW(body, L"Наведите мышь на отряд или клетку. Выберите свой отряд и наведите на врага — здесь будет прогноз удара.", 400);
  }
  title[119] = body[399] = f1[119] = f2[119] = 0;
  SetBkMode(cv->dc, TRANSPARENT);
  if (g_fontUi) SelectObject(cv->dc, g_fontUi);
  SetTextColor(cv->dc, COL_INK);
  RECT tt = {r.left + pad, r.top + GS_(8), r.right - pad, r.top + GS_(26)};
  DrawTextW(cv->dc, title, -1, &tt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  int y = r.top + GS_(30);
  if (f1[0]) {
    SetTextColor(cv->dc, c1);
    RECT a = {r.left + pad, y, r.right - pad, y + GS_(19)};
    DrawTextW(cv->dc, f1, -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    y += GS_(20);
  }
  if (g_fontSmall) SelectObject(cv->dc, g_fontSmall);
  if (f2[0]) {
    SetTextColor(cv->dc, c2);
    RECT a = {r.left + pad, y, r.right - pad, y + GS_(18)};
    DrawTextW(cv->dc, f2, -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    y += GS_(20);
  }
  SetTextColor(cv->dc, COL_MUTED);
  RECT tb = {r.left + pad, y, r.right - pad, r.bottom - GS_(6)};
  DrawTextW(cv->dc, body, -1, &tb, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

static void sg_paint_buy(UiCanvas *cv, RECT r) {
  const SgState *st = g_sgAnim.on && g_sgAnim.opp ? &g_sgAnim.view : &g_sg.cur;
  int me = sg_me(), pad = GS_(10);
  sg_card(cv, r, COL_PAPER, COL_LINE, 1.0f);
  SetBkMode(cv->dc, TRANSPARENT);
  if (g_fontUi) SelectObject(cv->dc, g_fontUi);
  SetTextColor(cv->dc, COL_INK);
  RECT tt = {r.left + pad, r.top + GS_(6), r.right - pad, r.top + GS_(24)};
  DrawTextW(cv->dc, L"Нанять", -1, &tt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  wchar_t t[80];
  _snwprintf(t, 80, L"золото: %d", st->gold[me]);
  SetTextColor(cv->dc, RGB(176, 128, 20));
  DrawTextW(cv->dc, t, -1, &tt, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  for (int k = 0; k < SGU_N; k++) {
    RECT s;
    sg_buy_rect(k, &s);
    BOOL can = sg_can_buy(k), on = g_sg.buy == k, hot = g_sg.hoverBuy == k;
    if (on || (hot && can)) {
      UiShape sh;
      memset(&sh, 0, sizeof(sh));
      sh.r = (float)GS_(8);
      sh.fill = on ? COL_SAGE : COL_PAPER_DARK;
      sh.fillA = on ? 56 : 255;
      sh.border = on ? COL_SAGE : COL_LINE;
      sh.borderA = 255;
      sh.bw = on ? (float)GS_(2) : 1.0f;
      ui_shape(cv, s, &sh);
    }
    float cx = (s.left + s.right) / 2.0f, cy = s.top + GS_(4) + g_sgArt.td / 2.0f;
    sg_draw_token(cv, k, me, !can, cx, cy, 255, 0);
    wchar_t c[8];
    _snwprintf(c, 8, L"%d", kSgKind[k].cost);
    if (g_fontSmall) SelectObject(cv->dc, g_fontSmall);
    SetTextColor(cv->dc, can ? COL_INK : COL_MUTED);
    RECT tc = {s.left, s.bottom - GS_(18), s.right, s.bottom - GS_(2)};
    DrawTextW(cv->dc, c, -1, &tc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  const wchar_t *hint = !sg_my_turn() ? (g_sgn.mode == GN_OVER ? L"" : L"Нанимать можно в свой ход")
                        : sg_count(st, me) >= SG_SIDE_UNITS ? L"У вас уже 10 отрядов — больше нельзя"
                                                            : L"Новый отряд ходит со следующего хода";
  if (g_fontSmall) SelectObject(cv->dc, g_fontSmall);
  SetTextColor(cv->dc, COL_MUTED);
  RECT th = {r.left + pad, r.bottom - GS_(30), r.right - pad, r.bottom - GS_(4)};
  DrawTextW(cv->dc, hint, -1, &th, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void sg_paint_side(HDC hdc) {
  if (!g_sg.have) return;
  int x = sg_side_x(), y0 = sg_by(), w = GS_(SG_SIDE), h = sg_board_h();
  RECT box = {x, y0, x + w, y0 + h};
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, hdc, box, box.left, box.top, TRUE)) return;
  const SgState *v = sg_shown();
  int me = sg_me();
  BOOL play = g_sgn.mode == GN_PLAY, over = g_sgn.mode == GN_OVER;
  BOOL mine = play && !v->over && sg_side(v) == me && !(g_sgAnim.on && g_sgAnim.opp);
  /* соперник сверху, вы — снизу */
  {
    const wchar_t *sub;
    if (over) sub = g_sg.winner == 1 - me ? L"выиграл" : g_sg.winner < 0 ? L"—" : L"проиграл";
    else if (play && !mine) sub = L"ходит…";
    else sub = g_sg.solo ? kSgSideName[1 - me] : gn_opp_online(&g_sgn) ? L"в сети" : L"не в сети";
    sg_paint_player(&cv, sg_card_rect(FALSE), 1 - me, sg_opp_name(), sub, play && !mine);
  }
  {
    wchar_t name[40];
    _snwprintf(name, 40, L"Вы — %s", kSgSideName[me]);
    const wchar_t *sub = over ? (g_sg.winner == me ? L"вы выиграли" : g_sg.winner < 0 ? L"—" : L"вы проиграли") : mine ? L"ваш ход" : L"";
    sg_paint_player(&cv, sg_card_rect(TRUE), me, name, sub, mine);
  }
  /* какой ход и чей — плашкой под карточкой соперника */
  {
    RECT opp = sg_card_rect(FALSE);
    RECT pr = {opp.left, opp.bottom + GS_(8), opp.right, opp.bottom + GS_(8) + GS_(30)};
    wchar_t t[80];
    int round = (v->turn + 1) / 2;
    if (round > SG_TURNS / 2) round = SG_TURNS / 2;
    if (over) lstrcpynW(t, L"Партия окончена", 80);
    else _snwprintf(t, 80, L"Ход %d из %d · %s", round, SG_TURNS / 2, mine ? L"ваш ход" : L"ход соперника");
    t[79] = 0;
    UiShape k;
    memset(&k, 0, sizeof(k));
    k.r = -1;
    k.fill = mine ? COL_SAGE : COL_PAPER_DARK;
    k.fillA = 255;
    if (!mine) {
      k.border = COL_LINE;
      k.borderA = 255;
      k.bw = 1;
    }
    ui_shape(&cv, pr, &k);
    if (g_fontUi) SelectObject(cv.dc, g_fontUi);
    SetBkMode(cv.dc, TRANSPARENT);
    SetTextColor(cv.dc, mine ? RGB(255, 255, 255) : COL_MUTED);
    DrawTextW(cv.dc, t, -1, &pr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  sg_paint_info(&cv, sg_info_card());
  sg_paint_buy(&cv, sg_buy_card());
  ui_canvas_end(&cv, hdc);
}

/* ---- окно ------------------------------------------------------------------- */

static void sg_layout(void) {
  if (!g_sgWnd) return;
  RECT rc;
  GetClientRect(g_sgWnd, &rc);
  place_panel_close(g_sgWnd);
  gn_layout_lobby(&g_sgn, g_nd.poll);
  int m = g_sgn.mode, pad = GS_(14), bh = gn_px(&g_sgn, 28), gap = gn_px(&g_sgn, 8);
  int by = sg_below_y() + GS_(30);
  HWND end = GetDlgItem(g_sgWnd, ID_SG_END), undo = GetDlgItem(g_sgWnd, ID_SG_UNDO), rep = GetDlgItem(g_sgWnd, ID_SG_REPLAY),
       resign = GetDlgItem(g_sgWnd, ID_SG_RESIGN), back = GetDlgItem(g_sgWnd, ID_SG_BACK), solo = GetDlgItem(g_sgWnd, ID_SG_SOLO);
  BOOL play = m == GN_PLAY, over = m == GN_OVER, my = sg_my_turn();
  BOOL canRep = (play || over) && g_sg.lastN > 0 && g_sg.lastSide != sg_me();
  ShowWindow(solo, m == GN_LOBBY ? SW_SHOW : SW_HIDE);
  if (m == GN_LOBBY)
    MoveWindow(solo, pad + gn_px(&g_sgn, 220) + gap, gn_px(&g_sgn, 46) + gn_px(&g_sgn, 232), gn_px(&g_sgn, 200), bh, TRUE);
  ShowWindow(end, my ? SW_SHOW : SW_HIDE);
  ShowWindow(undo, my ? SW_SHOW : SW_HIDE);
  ShowWindow(rep, canRep ? SW_SHOW : SW_HIDE);
  ShowWindow(resign, play && !g_sg.solo ? SW_SHOW : SW_HIDE);
  ShowWindow(back, over || (play && g_sg.solo) ? SW_SHOW : SW_HIDE);
  int x = pad;
  if (my) {
    MoveWindow(end, x, by, gn_px(&g_sgn, 150), bh, TRUE);
    x += gn_px(&g_sgn, 150) + gap;
    MoveWindow(undo, x, by, gn_px(&g_sgn, 170), bh, TRUE);
    x += gn_px(&g_sgn, 170) + gap;
    EnableWindow(undo, g_sg.pend[0] != 0);
  }
  if (canRep) {
    MoveWindow(rep, x, by, gn_px(&g_sgn, 200), bh, TRUE);
    x += gn_px(&g_sgn, 200) + gap;
  }
  /* «Сдаться» и «К списку игроков» — под полосой справа, подальше от «Завершить ход» */
  int rx = sg_side_x() + GS_(SG_SIDE);
  if (play && !g_sg.solo) MoveWindow(resign, rx - gn_px(&g_sgn, 110), by, gn_px(&g_sgn, 110), bh, TRUE);
  if (over || (play && g_sg.solo)) MoveWindow(back, rx - gn_px(&g_sgn, 170), by, gn_px(&g_sgn, 170), bh, TRUE);
}

static void sg_mark_btn(void) { gn_mark_btn(&g_sgn, g_btnSg, L"Стратегия", sg_my_turn() && !g_sg.solo); }

static void sg_refresh_view(void) {
  sg_mark_btn();
  if (!g_sgWnd) return;
  if (g_sgn.mode == GN_LOBBY) gn_list_sync(&g_sgn, GetDlgItem(g_sgWnd, ID_SG_LIST), g_nd.poll);
  sg_layout();
  InvalidateRect(g_sgWnd, NULL, FALSE);
}

/* список «кто в сети» у нард обновился — и у стратегии */
static void strategiya_online_changed(void) {
  if (g_sgn.mode == GN_PLAY && !sg_my_turn()) sg_turn_status(); /* соперник пропал из сети или вернулся */
  if (g_sgWnd && IsWindowVisible(g_sgWnd) && (g_sgn.mode == GN_LOBBY || g_sgn.mode == GN_PLAY)) sg_refresh_view();
  else sg_mark_btn();
}

/* в списке игроков, под правилами: отряды — кто есть кто — и местность */
static void sg_paint_legend(HDC hdc, RECT rc) {
  sg_art_build();
  int pad = GS_(14), top = gn_px(&g_sgn, 46) + gn_px(&g_sgn, 420), rowH = GS_(58);
  if (top + rowH * 3 > rc.bottom - gn_px(&g_sgn, 36)) return; /* не поместилось — обойдёмся правилами */
  int colW = (rc.right - pad * 2) / 2;
  RECT box = {pad, top, rc.right - pad, top + rowH * 3};
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, hdc, box, box.left, box.top, TRUE)) return;
  SetBkMode(cv.dc, TRANSPARENT);
  for (int t = 0; t <= SGU_N; t++) {
    int x = pad + (t % 2) * colW, y = top + (t / 2) * rowH, tx = x + g_sgArt.td + GS_(14);
    wchar_t name[80];
    const wchar_t *about;
    if (t < SGU_N) {
      sg_draw_token(&cv, t, t % 2, FALSE, x + g_sgArt.td / 2.0f + GS_(3), y + rowH / 2.0f - GS_(2), 255, 0);
      _snwprintf(name, 80, L"%s — %d золота", kSgName[t], kSgKind[t].cost);
      about = kSgAbout[t];
    } else {
      sg_blit_c(&cv, &g_sgArt.vil[0], x + g_sgArt.td / 2.0f + GS_(3), y + rowH / 2.0f - GS_(2), 255);
      lstrcpynW(name, L"Местность", 80);
      about = L"Деревня даёт +2 золота в ход и лечит. Лес и холм защищают, с холма стреляют дальше. Горы и воду не пройти.";
    }
    name[79] = 0;
    if (g_fontUi) SelectObject(cv.dc, g_fontUi);
    SetTextColor(cv.dc, COL_INK);
    RECT tn = {tx, y + GS_(4), x + colW - GS_(10), y + GS_(22)};
    DrawTextW(cv.dc, name, -1, &tn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (g_fontSmall) SelectObject(cv.dc, g_fontSmall);
    SetTextColor(cv.dc, COL_MUTED);
    RECT ta = {tx, y + GS_(23), x + colW - GS_(10), y + rowH - GS_(2)};
    DrawTextW(cv.dc, about, -1, &ta, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
  }
  ui_canvas_end(&cv, hdc);
}

static void sg_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, g_sg.solo ? L"Стратегия — игра с компьютером" : L"Стратегия");
  if (g_sgn.mode == GN_LOBBY) {
    gn_paint_lobby(&g_sgn, hdc, rc, g_nd.poll,
                   L"Пошаговая стратегия на двоих. У каждого замок и отряды: мечник, копейщик, лучник, всадник, катапульта. "
                   L"Мечник сильнее копейщика, копейщик — всадника, всадник — мечника; лучник и катапульта бьют издали. "
                   L"Деревни дают золото — на него нанимают новые отряды. В свой ход сходите всеми отрядами и нажмите «Завершить ход»: "
                   L"ход уйдёт сопернику, даже если он сейчас не в сети. Победа — разрушить замок соперника; через 30 ходов побеждает тот, "
                   L"у кого замок целее. Не с кем играть или хотите попробовать — «Игра с компьютером».");
    sg_paint_legend(hdc, rc);
    return;
  }
  if (g_sgn.mode == GN_WAIT) {
    gn_paint_wait(&g_sgn, hdc, rc);
    return;
  }
  sg_paint_board(hdc);
  sg_paint_side(hdc);
  int pad = GS_(14), yb = sg_below_y();
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  RECT st = {pad, yb, rc.right - pad, yb + GS_(22)};
  SetTextColor(hdc, g_sgn.mode == GN_OVER ? COL_SAGE : COL_INK);
  const wchar_t *status = g_sgn.status;
  if (g_sgAnim.on && g_sgAnim.opp && g_sgn.mode == GN_PLAY) status = L"Ход соперника — смотрим… Щелчок по карте — сразу к итогу";
  DrawTextW(hdc, status, -1, &st, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  RECT dr = {pad, yb + GS_(66), rc.right - pad, rc.bottom - GS_(4)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  if (g_sg.solo) {
    /* в игре с компьютером связи нет; зато могут позвать по сети */
    GnPoll *pl = g_sgn.poll;
    if (pl && pl->nInv) {
      wchar_t s[260];
      _snwprintf(s, 260, L"%s %s — чтобы ответить, вернитесь «К списку игроков»", pl->invName[0], g_sgn.invText);
      s[259] = 0;
      SetTextColor(hdc, COL_SAGE);
      DrawTextW(hdc, s, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    return;
  }
  wchar_t dg[400];
  gn_diag_text(&g_sgn, dg, 400);
  SetTextColor(hdc, g_sgn.writeFail ? ui_warn(RGB(200, 60, 40)) : COL_MUTED);
  DrawTextW(hdc, dg, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

/* выбрать отряд: свой — куда дойдёт и кого достанет; чужой — посмотреть, куда дойдёт он */
static void sg_select(int ui) {
  g_sg.sel = ui;
  g_sg.buy = -1;
  memset(g_sg.reach, 255, sizeof(g_sg.reach));
  if (ui < 0 || !g_sg.cur.u[ui].alive) {
    g_sg.sel = -1;
    return;
  }
  const SgUnit *u = &g_sg.cur.u[ui];
  BOOL mine = sg_my_turn() && u->side == sg_me();
  if (mine && (u->moved || u->done)) return; /* уже ходил — только удар */
  signed short prev[SG_CELLS];
  sg_reach(&g_sg.cur, ui, g_sg.reach, prev);
}

static void sg_cancel_pick(void) {
  g_sg.sel = g_sg.buy = -1;
  memset(g_sg.reach, 255, sizeof(g_sg.reach));
  sg_turn_status();
  sg_refresh_view();
}

/* под мышью то, по чему есть смысл щёлкнуть */
static BOOL sg_clickable(int cell) {
  const SgState *st = &g_sg.cur;
  if (cell < 0 || g_sgn.mode != GN_PLAY || !g_sg.have) return FALSE;
  if (g_sgAnim.on) return g_sgAnim.opp;
  if (st->at[cell] >= 0 && g_sg.buy < 0) return TRUE; /* любой отряд можно выбрать — посмотреть */
  if (!sg_my_turn()) return FALSE;
  int me = sg_me();
  if (g_sg.buy >= 0) return sg_can_buy_at(st, me, cell);
  if (g_sg.sel < 0 || st->u[g_sg.sel].side != me) return FALSE;
  if (sg_can_attack(st, g_sg.sel, cell)) return TRUE;
  return g_sg.reach[cell] != 255 && st->at[cell] < 0 && cell != st->u[g_sg.sel].cell;
}

static void sg_mouse_down(int x, int y) {
  if (g_sgn.mode != GN_PLAY || !g_sg.have) return;
  if (g_sgAnim.on) { /* щёлкнули, пока показывается ход, — досмотрено */
    BOOL opp = g_sgAnim.opp;
    sg_anim_finish();
    if (opp) {
      sg_turn_status();
      sg_refresh_view();
      return;
    }
  }
  const SgState *st = &g_sg.cur;
  int me = sg_me();
  BOOL my = sg_my_turn();
  int bt = sg_buy_hit(x, y);
  if (bt >= 0) {
    if (my && sg_can_buy(bt)) {
      g_sg.buy = g_sg.buy == bt ? -1 : bt;
      g_sg.sel = -1;
      memset(g_sg.reach, 255, sizeof(g_sg.reach));
    }
    sg_turn_status();
    sg_refresh_view();
    return;
  }
  int cell = sg_hit(x, y);
  if (cell < 0) {
    sg_cancel_pick();
    return;
  }
  if (my && g_sg.buy >= 0) {
    if (sg_can_buy_at(st, me, cell)) {
      SgAct a = {'b', 0, cell, g_sg.buy};
      sg_do(&a);
    }
    g_sg.buy = -1;
    if (g_sgn.mode == GN_PLAY) sg_turn_status();
    sg_refresh_view();
    return;
  }
  int ui = st->at[cell];
  if (my && g_sg.sel >= 0 && st->u[g_sg.sel].alive && st->u[g_sg.sel].side == me) {
    int sel = g_sg.sel;
    if (sg_can_attack(st, sel, cell)) {
      SgAct a = {'a', st->u[sel].cell, cell, 0};
      sg_do(&a);
      g_sg.sel = -1;
      memset(g_sg.reach, 255, sizeof(g_sg.reach));
      if (g_sgn.mode == GN_PLAY) sg_turn_status();
      sg_refresh_view();
      return;
    }
    if (ui < 0 && g_sg.reach[cell] != 255) {
      SgAct a = {'m', st->u[sel].cell, cell, 0};
      if (sg_do(&a)) {
        /* дошёл: есть кого ударить — остаётся выбранным */
        if (g_sg.cur.u[sel].alive && sg_has_target(&g_sg.cur, sel)) sg_select(sel);
        else sg_select(-1);
      }
      sg_turn_status();
      sg_refresh_view();
      return;
    }
  }
  /* выбрать отряд (второй щелчок по выбранному — снять выбор) */
  if (ui >= 0 && ui != g_sg.sel) sg_select(ui);
  else sg_select(-1);
  sg_turn_status();
  sg_refresh_view();
}

static RECT sg_board_rect(void) {
  RECT r = {0, sg_by() - GS_(6), sg_side_x(), sg_by() + sg_board_h() + GS_(12)};
  return r;
}

static LRESULT CALLBACK StrategiyaProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    sg_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_TIMER:
    if (wParam == SG_TIMER) {
      BOOL was = g_sgAnim.on;
      sg_anim_tick();
      if (!g_sgAnim.on) {
        KillTimer(hwnd, SG_TIMER);
        if (was) { /* досмотрели — карта как есть, с подсказками */
          if (g_sgn.mode == GN_PLAY) sg_turn_status();
          sg_refresh_view();
          return 0;
        }
      }
      RECT bd = sg_board_rect();
      InvalidateRect(hwnd, &bd, FALSE);
    } else if (wParam == SG_TIMER_BOT) {
      KillTimer(hwnd, SG_TIMER_BOT);
      sg_solo_bot();
    }
    return 0;
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_LBUTTONDOWN:
    sg_mouse_down(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  case WM_MOUSEMOVE: {
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    BOOL in = g_sgn.mode == GN_PLAY || g_sgn.mode == GN_OVER;
    int c = in ? sg_hit(x, y) : -1, b = in ? sg_buy_hit(x, y) : -1;
    if (c != g_sg.hover || b != g_sg.hoverBuy) {
      g_sg.hover = c;
      g_sg.hoverBuy = b;
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&tme);
      RECT all = {0, sg_by() - GS_(6), sg_side_x() + GS_(SG_SIDE), sg_by() + sg_board_h() + GS_(12)};
      InvalidateRect(hwnd, &all, FALSE);
    }
    return 0;
  }
  case WM_MOUSELEAVE:
    if (g_sg.hover >= 0 || g_sg.hoverBuy >= 0) {
      g_sg.hover = g_sg.hoverBuy = -1;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT && (sg_clickable(g_sg.hover) || (g_sg.hoverBuy >= 0 && sg_can_buy(g_sg.hoverBuy)))) {
      SetCursor(LoadCursorW(NULL, IDC_HAND));
      return TRUE;
    }
    break;
  case WM_RBUTTONDOWN:
    if (g_sgn.mode == GN_PLAY) sg_cancel_pick(); /* правая — снять выбор */
    return 0;
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) {
      if (g_sgn.mode == GN_PLAY && (g_sg.sel >= 0 || g_sg.buy >= 0)) sg_cancel_pick();
      else ShowWindow(hwnd, SW_HIDE);
    } else if (wParam == VK_BACK || (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000))) {
      sg_undo();
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
    if (id != ID_SG_LIST && HIWORD(wParam) != BN_CLICKED) return 0;
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (gn_command(&g_sgn, id, HIWORD(wParam))) {
      if (id == ID_SG_LIST) return 0; /* фокус у списка не отнимаем */
    }
    if (id == ID_SG_SOLO) sg_solo_start();
    if (id == ID_SG_END) sg_end_turn_click();
    if (id == ID_SG_UNDO) sg_undo();
    if (id == ID_SG_REPLAY) sg_replay_last();
    if (id == ID_SG_RESIGN) sg_resign();
    if (id == ID_SG_BACK) sg_leave();
    SetFocus(hwnd);
    return 0;
  }
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void strategiya_start(void) {
  if (g_sgn.thread) return;
  wchar_t *t = gn_load_local(&g_sgn);
  if (t) {
    /* партия с прошлого запуска: начальная позиция до первого чтения файлов
       и мои неотправленные действия */
    sg_reset_game();
    sg_first_position();
    wchar_t v[SG_LINE];
    if (nd_field(t, L"pt", v, 8)) g_sg.pendTurn = _wtoi(v);
    if (g_sg.pendTurn > 0 && nd_field(t, L"pa", v, SG_LINE))
      WideCharToMultiByte(CP_UTF8, 0, v, -1, g_sg.pend, SG_LINE, NULL, NULL);
    g_sg.pend[SG_LINE - 1] = 0;
    free(t);
  }
  gn_start(&g_sgn);
}

static void strategiya_show(void) {
  if (!g_sgWnd) {
    HDC s = GetDC(NULL);
    float dpi = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    g_sgn.sc = dpi; /* кнопки и список — по размеру шрифта */
    /* карта большая: на маленьком экране окно ужимается, чтобы влезть */
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int baseW = SG_LEFT * 2 + (int)(1.7320508f * SG_RAD * SG_W + 0.5f) + 2 * (SG_FRAME + SG_PAD) + 14 + SG_SIDE;
    int baseH = SG_TOP + (int)(SG_RAD * (1.5f * (SG_H - 1) + 2) + 0.5f) + 2 * (SG_FRAME + SG_PAD) + SG_BELOW;
    g_sgS = dpi;
    float fitW = (float)(wa.right - wa.left - 16) / baseW, fitH = (float)(wa.bottom - wa.top - 16) / baseH;
    if (g_sgS > fitW) g_sgS = fitW;
    if (g_sgS > fitH) g_sgS = fitH;
    if (g_sgS < 0.6f) g_sgS = 0.6f;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StrategiyaProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadStrategiya";
    wc.hIcon = app_icon(FALSE);
    wc.hIconSm = app_icon(TRUE);
    RegisterClassExW(&wc);
    int w = sg_side_x() + GS_(SG_SIDE) + GS_(SG_LEFT), h = sg_by() + sg_board_h() + GS_(SG_BELOW);
    g_sgWnd = CreateWindowExW(WS_EX_APPWINDOW, L"CursorPadStrategiya", L"Стратегия",
                              WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU, 0, 0, w, h, NULL, NULL, g_inst, NULL);
    if (!g_sgWnd) return;
    round_corners(g_sgWnd);
    mk_btn(g_sgWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_sgWnd, L"–", ID_PANEL_MIN);
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 100,
                                100, g_sgWnd, (HMENU)(INT_PTR)ID_SG_LIST, g_inst, NULL);
    const struct {
      const wchar_t *t;
      int id;
    } btns[] = {{L"Позвать играть", ID_SG_INVITE},       {L"Принять", ID_SG_ACCEPT},
                {L"Отказаться", ID_SG_DECLINE},          {L"Игра с компьютером", ID_SG_SOLO},
                {L"Завершить ход", ID_SG_END},           {L"Отменить действие", ID_SG_UNDO},
                {L"Показать ход соперника", ID_SG_REPLAY}, {L"Сдаться", ID_SG_RESIGN},
                {L"К списку игроков", ID_SG_BACK},       {L"Отменить приглашение", ID_SG_CANCEL}};
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
      HWND b = mk_btn(g_sgWnd, btns[i].t, btns[i].id);
      if (g_fontUi) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
      if (btns[i].id != ID_SG_INVITE) ShowWindow(b, SW_HIDE);
    }
    if (g_fontBody) SendMessageW(list, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    SetWindowPos(g_sgWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h, SWP_NOZORDER);
  }
  if (g_sgn.mode == GN_PLAY) sg_turn_status();
  sg_refresh_view();
  ShowWindow(g_sgWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_sgWnd);
  SetFocus(g_sgWnd);
  /* ход соперника пришёл, пока окно было закрыто, — показать его сейчас */
  if ((g_sgn.mode == GN_PLAY || g_sgn.mode == GN_OVER) && g_sg.lastN > 0 && g_sg.lastSide != sg_me() && g_sg.lastTurn > g_sg.shownTurn) {
    g_sg.shownTurn = g_sg.lastTurn;
    sg_anim_play(&g_sg.lastBefore, g_sg.lastActs, g_sg.lastN, TRUE);
  }
}
