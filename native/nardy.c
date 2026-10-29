/* ---- Короткие нарды по сети через общую папку ------------------------------

   Играют двое, у кого в Настройках задана одна общая папка. Всё общение —
   файлами в <папка>\CursorPad-Nardy:

     online\<я>.txt            — «я в сети»: имя, компьютер; раз в минуту
                                 (этот список — общий у нард, шашек,
                                 морского боя и чата)
     inv\<кому>\<партия>.txt   — приглашение
     games\<партия>\p0.txt     — всё, что пишет пригласивший (сторона 0)
     games\<партия>\p1.txt     — всё, что пишет принявший (сторона 1)
     mv\<кому>\…               — копии файлов партии (с 2026.10.22)

   Приглашения, файлы партии и их копии — общая связь игр (game_net.c):
   до 2026.10.22 у нард она была своя, старая — без копий ходов и без своей
   записи на компьютере, и на рабочей сети ход или число для кубиков до
   соперника не доходили: партия вставала на «Готовим кубики» или «Ходит …».
   Теперь так же, как у шашек: копия каждой записи — новым файлом в папку
   соперника, своя запись хранится и рядом с заметками (nardy_log.txt),
   встречные приглашения сходятся в одну партию, строка «связь» под доской.
   Строки своего файла партии, кроме общих: commit, seed, t (ход).

   Кубики честные: пригласивший сперва кладёт только отпечаток (SHA-256)
   своего случайного числа, принявший — своё число открыто, после этого
   пригласивший открывает своё, и программа соперника сверяет его с
   отпечатком. Все броски партии считаются из этих двух чисел — ни один
   игрок не может подобрать себе удобный, даже поменяв свою программу.
   Каждый пришедший ход программа соперника проверяет по правилам сама.

   Партия восстанавливается из файлов целиком: закрыли программу или
   выключили компьютер — после запуска доска та же. */

#include "nardy_rules.c"

#define WM_NARDY_POLL (WM_APP + 18) /* lParam — NdPoll* («кто в сети»), освобождает получатель */
#define WM_NARDY_GAME (WM_APP + 66) /* lParam — GnPoll* (приглашения и партия), освобождает получатель */
#define ND_SUB L"CursorPad-Nardy"
/* ID_ND_* — в cursorpad.c: главные из них рисуются синими */

typedef struct {
  char mySecret[40]; /* hex */
  /* из файлов */
  BOOL ready;        /* оба числа известны — можно играть */
  int starter, turn; /* turn — номер хода, который сейчас на очереди */
  NdBoard board;     /* доска перед этим ходом */
  int d1, d2;        /* его кубики */
  int winner, points;
  BOOL resigned;
  wchar_t endWhy[200];
  /* мой ход в процессе */
  BOOL rolled;
  NdBoard work;
  int dice[4], ndice;
  NdStep done[4];
  int ndone;
  NdBoard undo[5];
  int sel;
  /* последний ход соперника — подсветить, куда пришли его шашки */
  int lastTo[4], nlast;
  int lastD1, lastD2;
  BOOL lastOpp; /* последний сыгранный ход — соперника */
  /* кто в сети — общий список (его же берут шашки, морской бой и чат) */
  NdPoll *poll;
  int notifiedTurn;
} NdGame;

static NdGame g_nd;
static HWND g_ndWnd;
static float g_ndS = 1.0f;
static HBITMAP g_ndChk[2];    /* шашки: 0 — светлая (моя), 1 — тёмная */
static BYTE *g_ndChkBits[2];
static int g_ndChkD;
static BOOL g_ndThread;

static void nd_fresh(void);
static void nd_refresh_view(void);
static void nd_extra(wchar_t *out, int cap);

static GnNet g_ndn = {
    .sub = ND_SUB,
    .local = L"nardy",
    .title = L"Нарды",
    .invText = L"зовёт сыграть в короткие нарды",
    .moveKey = L"t",
    .msg = WM_NARDY_GAME,
    .balloon = 1,
    .wnd = &g_ndWnd,
    .sc = 1.0f,
    .idList = ID_ND_LIST,
    .idInvite = ID_ND_INVITE,
    .idAccept = ID_ND_ACCEPT,
    .idDecline = ID_ND_DECLINE,
    .idCancel = ID_ND_CANCEL,
    .fresh = nd_fresh,
    .view = nd_refresh_view,
    .extra = nd_extra,
};

#define nd_status(...) gn_status(&g_ndn, __VA_ARGS__)

static int S_(int v) { return (int)(v * g_ndS + 0.5f); }

/* кто в сети — для шашек, морского боя и строки «он не в сети» */
static BOOL nd_is_online(const wchar_t *id, BOOL *known) {
  NdPoll *pl = g_nd.poll;
  *known = pl != NULL;
  if (!pl) return FALSE;
  for (int i = 0; i < pl->nOnline; i++)
    if (!wcscmp(pl->onId[i], id)) return TRUE;
  return FALSE;
}

/* ---- кубики ---------------------------------------------------------------- */

static void nd_commit(const char *secret, char *out) {
  char buf[80];
  BYTE h[32];
  snprintf(buf, sizeof(buf), "nardy|%s", secret);
  if (gn_sha(buf, h)) gn_hex(h, 32, out);
  else out[0] = 0;
}

/* два кубика из общего зерна; k — метка броска */
static void nd_roll(const char *s0, const char *s1, const char *k, int *a, int *b) {
  BYTE h[32];
  char buf[160];
  int got = 0, v[2] = {1, 1};
  for (int round = 0; got < 2 && round < 8; round++) {
    snprintf(buf, sizeof(buf), "%s:%s:%s:%d", s0, s1, k, round);
    if (!gn_sha(buf, h)) break;
    for (int i = 0; i < 32 && got < 2; i++)
      if (h[i] < 252) v[got++] = 1 + h[i] % 6; /* без перекоса: 252 делится на 6 */
  }
  *a = v[0];
  *b = v[1];
}

static int nd_side_of(int turn) { return (turn % 2 == 1) ? g_nd.starter : 1 - g_nd.starter; }

/* ход номер n из файла: строка «t\tn\tf:d f:d …» */
static int nd_turn_line(const wchar_t *text, int n, NdStep *st, int cap, BOOL *found) {
  *found = FALSE;
  if (!text) return 0;
  wchar_t key[24];
  _snwprintf(key, 24, L"t\t%d\t", n);
  size_t kl = wcslen(key);
  for (const wchar_t *p = text; p && *p;) {
    if (!wcsncmp(p, key, kl)) {
      *found = TRUE;
      int cnt = 0;
      const wchar_t *q = p + kl;
      while (*q && *q != L'\n' && *q != L'\r' && cnt < cap) {
        if (*q == L'-') break;
        int f = (int)wcstol(q, (wchar_t **)&q, 10);
        if (*q != L':') break;
        q++;
        int d = (int)wcstol(q, (wchar_t **)&q, 10);
        st[cnt].from = (signed char)f;
        st[cnt].die = (signed char)d;
        st[cnt].to = 0;
        cnt++;
        while (*q == L' ') q++;
      }
      return cnt;
    }
    p = wcschr(p, L'\n');
    if (p) p++;
  }
  return 0;
}

/* ---- фоновый поток: кто в сети ---------------------------------------------- */

/* Опрос папки бережный: при двадцати коллегах каждый раз перечитывать все
   отметки — сотни чтений в секунду на сетевом диске. Поэтому «я в сети» —
   раз в минуту, кто в сети — раз в 10 с (имя читается только у нового).
   Приглашения и партия — в общем потоке связи (game_net.c). */
static DWORD WINAPI nd_thread(LPVOID param) {
  (void)param;
  ULONGLONG lastBeat = 0, lastOnline = 0;
  unsigned lastSum = 0;
  static NdPoll cur; /* последнее известное — копируется в каждое сообщение */
  static wchar_t cacheId[ND_MAXONLINE * 2][96], cacheName[ND_MAXONLINE * 2][128];
  static int nCache;
  for (;;) {
    Sleep(1000);
    if (g_quitting) break;
    wchar_t root[MAX_PATH];
    share_root_copy(root);
    if (!root[0]) continue;
    wchar_t myId[96], dOn[SHARE_PATH];
    nd_myid(myId, 96, NULL, 0);
    if (!gn_dir(&g_ndn, root, L"online", dOn)) continue;
    ULONGLONG now = GetTickCount64();
    if (!lastBeat || now - lastBeat > 60000) {
      wchar_t path[SHARE_PATH], txt[300], user[128], pc[64];
      share_me(user, pc);
      _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dOn, myId);
      _snwprintf(txt, 300, L"name\t%s\npc\t%s\n", user, pc);
      if (share_write(path, txt)) lastBeat = now;
    }
    if (lastOnline && now - lastOnline <= 10000) continue;
    lastOnline = now;
    wchar_t pat[SHARE_PATH + 8];
    WIN32_FIND_DATAW fd;
    cur.nOnline = 0;
    _snwprintf(pat, SHARE_PATH + 8, L"%s\\*.txt", dOn);
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
      do {
        if (cur.nOnline >= ND_MAXONLINE) break;
        if (!ft_within(fd.ftLastWriteTime, 180)) continue;
        wchar_t id[96];
        lstrcpynW(id, fd.cFileName, 96);
        wchar_t *dot = wcsrchr(id, L'.');
        if (dot) *dot = 0;
        if (!_wcsicmp(id, myId)) continue;
        int c = -1;
        for (int k = 0; k < nCache && c < 0; k++)
          if (!wcscmp(cacheId[k], id)) c = k;
        if (c < 0) {
          wchar_t full[SHARE_PATH];
          _snwprintf(full, SHARE_PATH, L"%s\\%s", dOn, fd.cFileName);
          wchar_t *t = share_read(full);
          wchar_t nm[128] = L"", pc[64] = L"";
          if (t) {
            nd_field(t, L"name", nm, 128);
            nd_field(t, L"pc", pc, 64);
            free(t);
          }
          c = nCache < ND_MAXONLINE * 2 ? nCache++ : (int)(now % (ND_MAXONLINE * 2));
          lstrcpynW(cacheId[c], id, 96);
          _snwprintf(cacheName[c], 128, L"%s  ·  %s", nm[0] ? nm : id, pc);
        }
        lstrcpynW(cur.onId[cur.nOnline], id, 96);
        lstrcpynW(cur.onName[cur.nOnline], cacheName[c], 128);
        cur.nOnline++;
      } while (FindNextFileW(f, &fd));
      FindClose(f);
    }
    /* отправляем, только если что-то поменялось (и первый раз — пустой тоже) */
    unsigned sum = 2166136261u ^ (unsigned)cur.nOnline;
    const BYTE *m = (const BYTE *)cur.onId;
    for (size_t i = 0; i < sizeof(cur.onId[0]) * cur.nOnline; i++) sum = (sum ^ m[i]) * 16777619u;
    m = (const BYTE *)cur.onName;
    for (size_t i = 0; i < sizeof(cur.onName[0]) * cur.nOnline; i++) sum = (sum ^ m[i]) * 16777619u;
    if (sum == lastSum) continue;
    NdPoll *pl = (NdPoll *)malloc(sizeof(NdPoll));
    if (!pl) continue;
    memcpy(pl, &cur, sizeof(NdPoll));
    if (!g_hwnd || !PostMessageW(g_hwnd, WM_NARDY_POLL, 0, (LPARAM)pl)) free(pl);
    else lastSum = sum;
  }
  return 0;
}

/* ---- ход партии по файлам ------------------------------------------------- */

static void nd_begin_my_turn(void) {
  g_nd.rolled = FALSE;
  g_nd.work = g_nd.board;
  g_nd.ndice = nd_dice_list(g_nd.d1, g_nd.d2, g_nd.dice);
  g_nd.ndone = 0;
  g_nd.sel = 0;
}

static void nd_over(int winner, int points) {
  g_ndn.mode = GN_OVER;
  g_nd.winner = winner;
  g_nd.points = points;
  gn_save_local(&g_ndn);
}

/* Разыграть партию с начала по обоим файлам. mine — мой файл (в памяти
   он главнее папки: только что сделанный ход папка ещё не вернула). */
static void nd_replay(const wchar_t *p0, const wchar_t *p1) {
  GnNet *n = &g_ndn;
  const wchar_t *mine = gn_mine(n), *theirs = gn_theirs(n, p0, p1);
  const wchar_t *file[2];
  file[n->me] = mine;
  file[1 - n->me] = theirs;
  /* отказ или отмена приглашения */
  if (!gn_replay_head(n, theirs)) return;
  /* числа для кубиков */
  wchar_t w0[80], w1[80], wc[80];
  char s0[40] = "", s1[40] = "", c0[70] = "";
  if (nd_field(file[0], L"seed", w0, 80)) WideCharToMultiByte(CP_UTF8, 0, w0, -1, s0, 40, NULL, NULL);
  if (nd_field(file[1], L"seed", w1, 80)) WideCharToMultiByte(CP_UTF8, 0, w1, -1, s1, 40, NULL, NULL);
  if (nd_field(file[0], L"commit", wc, 80)) WideCharToMultiByte(CP_UTF8, 0, wc, -1, c0, 70, NULL, NULL);
  if (n->me == 0 && s1[0] && !s0[0] && g_nd.mySecret[0]) {
    /* соперник открыл своё число — открываем своё */
    wchar_t sec[40];
    MultiByteToWideChar(CP_UTF8, 0, g_nd.mySecret, -1, sec, 40);
    sb_add(&n->log, L"seed\t%s\n", sec);
    gn_write_mine(n);
    lstrcpyA(s0, g_nd.mySecret);
    mine = n->log.w;
    file[0] = mine;
  }
  if (!s0[0] || !s1[0]) {
    g_nd.ready = FALSE;
    if (n->mode == GN_WAIT && s1[0]) n->mode = GN_PLAY;
    if (n->mode == GN_WAIT) gn_wait_status(n);
    else if (n->me == 1 && !s0[0])
      nd_status(L"Готовим кубики: ждём %s (у него должен быть запущен CursorPad)…", gn_opp(n));
    else if (n->me == 0 && !g_nd.mySecret[0])
      nd_status(L"Своё число для кубиков потерялось (нет nardy.txt) — партию придётся сдать");
    else nd_status(L"Готовим кубики…");
    return;
  }
  if (n->me == 1) {
    char chk[70];
    nd_commit(s0, chk);
    if (strcmp(chk, c0) != 0) {
      lstrcpynW(g_nd.endWhy, L"Число соперника не совпало с отпечатком — партия остановлена", 200);
      nd_over(-1, 0);
      return;
    }
  }
  if (n->mode == GN_WAIT) n->mode = GN_PLAY;
  g_nd.ready = TRUE;
  /* кто начинает: по кубику каждому, до разных */
  int a = 1, b = 1;
  for (int k = 0; k < 50; k++) {
    char lab[16];
    snprintf(lab, sizeof(lab), "o%d", k);
    nd_roll(s0, s1, lab, &a, &b);
    if (a != b) break;
  }
  g_nd.starter = a > b ? 0 : 1;
  NdBoard bd;
  nd_start(&bd);
  int turn = 1;
  g_nd.nlast = 0;
  g_nd.lastOpp = FALSE;
  for (;; turn++) {
    int side = nd_side_of(turn), d1, d2;
    if (turn == 1) {
      d1 = a;
      d2 = b;
    } else {
      char lab[16];
      snprintf(lab, sizeof(lab), "t%d", turn);
      nd_roll(s0, s1, lab, &d1, &d2);
    }
    if (nd_has_line(file[0], L"resign") || nd_has_line(file[1], L"resign")) {
      int loser = nd_has_line(file[0], L"resign") ? 0 : 1;
      g_nd.board = bd;
      g_nd.resigned = TRUE;
      nd_over(1 - loser, 1);
      return;
    }
    NdStep st[8];
    BOOL found;
    int cnt = nd_turn_line(file[side], turn, st, 8, &found);
    if (!found) {
      g_nd.board = bd;
      g_nd.turn = turn;
      g_nd.d1 = d1;
      g_nd.d2 = d2;
      return;
    }
    /* проверка по правилам — и своих, и чужих ходов */
    int dice[4], nd = nd_dice_list(d1, d2, dice);
    for (int i = 0; i < cnt; i++) {
      NdStep legal[64];
      int k = nd_legal_steps(&bd, side, dice, nd, legal, 64), ok = -1;
      for (int j = 0; j < k && ok < 0; j++)
        if (legal[j].from == st[i].from && legal[j].die == st[i].die) ok = j;
      if (ok < 0) {
        _snwprintf(g_nd.endWhy, 200, L"Ход %d не прошёл проверку правил — партия остановлена", turn);
        g_nd.board = bd;
        nd_over(-1, 0);
        return;
      }
      nd_apply(&bd, side, legal[ok]);
      nd = nd_use_die(dice, nd, legal[ok].die);
      if (side != n->me && i < 4) g_nd.lastTo[i] = legal[ok].to ? nd_abs(side, legal[ok].to) : -1;
    }
    NdStep more[4];
    if (nd_legal_steps(&bd, side, dice, nd, more, 4) > 0) {
      _snwprintf(g_nd.endWhy, 200, L"Ход %d сыгран не полностью — партия остановлена", turn);
      g_nd.board = bd;
      nd_over(-1, 0);
      return;
    }
    g_nd.lastOpp = side != n->me;
    if (side != n->me) {
      g_nd.nlast = cnt > 4 ? 4 : cnt;
      g_nd.lastD1 = d1;
      g_nd.lastD2 = d2;
    } else {
      g_nd.nlast = 0;
    }
    if (bd.off[side] >= ND_CHECKERS) {
      g_nd.board = bd;
      g_nd.turn = turn;
      nd_over(side, nd_points(&bd, side));
      return;
    }
  }
}

static BOOL nd_my_turn(void) { return g_ndn.mode == GN_PLAY && g_nd.ready && nd_side_of(g_nd.turn) == g_ndn.me; }

static void nd_after_replay(int prevTurn, int prevMode) {
  GnNet *n = &g_ndn;
  if (prevMode == GN_WAIT && n->mode == GN_PLAY) gn_save_local(n); /* приняли — после перезапуска сразу игра */
  if (n->mode == GN_PLAY && g_nd.ready) {
    int side = nd_side_of(g_nd.turn);
    if (side == n->me) {
      if (g_nd.turn != prevTurn || prevMode != GN_PLAY) nd_begin_my_turn();
      if (!g_nd.rolled) nd_status(L"Ваш ход — бросайте кубики");
      if (g_nd.notifiedTurn != g_nd.turn && !gn_in_front(n)) {
        g_nd.notifiedTurn = g_nd.turn;
        wchar_t t[160];
        if (g_nd.turn == 1) _snwprintf(t, 160, L"Партия с %s: вы начинаете", n->oppName);
        else _snwprintf(t, 160, L"%s сходил — ваш ход", n->oppName);
        gn_balloon(n, t);
      }
    } else if (!gn_opp_online(n)) {
      nd_status(L"Ходит %s — он сейчас не в сети: ход увидит, когда запустит CursorPad", n->oppName);
    } else {
      nd_status(L"Ходит %s…", n->oppName);
    }
  }
  if (n->mode == GN_OVER && prevMode != GN_OVER) {
    wchar_t t[200];
    if (g_nd.winner == n->me)
      _snwprintf(t, 200, L"Вы выиграли у %s%s", n->oppName,
                 g_nd.resigned ? L" — соперник сдался" : (g_nd.points == 3 ? L" — кокс!" : (g_nd.points == 2 ? L" — марс!" : L"")));
    else if (g_nd.winner >= 0)
      _snwprintf(t, 200, L"Выиграл %s%s", n->oppName,
                 g_nd.resigned ? L" — вы сдались" : (g_nd.points == 3 ? L" — кокс" : (g_nd.points == 2 ? L" — марс" : L"")));
    else
      lstrcpynW(t, g_nd.endWhy, 200);
    nd_status(L"%s", t);
    if (!gn_in_front(n)) gn_balloon(n, t);
  }
  nd_refresh_view();
}

static void nd_replay_now(void) {
  int prevTurn = g_nd.turn, prevMode = g_ndn.mode;
  GnPoll *pl = g_ndn.poll;
  nd_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  nd_after_replay(prevTurn, prevMode);
}

/* опрос связи: приглашения и файлы партии */
static void nd_on_game(GnPoll *pl) {
  int prevTurn = g_nd.turn, prevMode = g_ndn.mode;
  if (!gn_on_poll(&g_ndn, pl)) return;
  nd_replay(pl->p0, pl->p1);
  nd_after_replay(prevTurn, prevMode);
}

/* «кто в сети» обновился */
static void nd_on_poll(NdPoll *pl) {
  free(g_nd.poll);
  g_nd.poll = pl;
  if (g_ndn.mode == GN_PLAY && g_nd.ready && !nd_my_turn())
    nd_status(gn_opp_online(&g_ndn) ? L"Ходит %s…" : L"Ходит %s — он сейчас не в сети: ход увидит, когда запустит CursorPad",
              g_ndn.oppName);
  nd_refresh_view();
}

/* ---- действия игрока -------------------------------------------------------- */

/* новая партия: своё случайное число; позвавший кладёт его отпечаток,
   принявший — само число (отпечаток позвавшего уже лежит) */
static void nd_fresh(void) {
  memset(&g_nd.board, 0, sizeof(g_nd.board));
  gn_secret(g_nd.mySecret);
  wchar_t w[70];
  if (g_ndn.me == 0) {
    char commit[70];
    nd_commit(g_nd.mySecret, commit);
    MultiByteToWideChar(CP_UTF8, 0, commit, -1, w, 70);
    sb_add(&g_ndn.log, L"commit\t%s\n", w);
  } else {
    MultiByteToWideChar(CP_UTF8, 0, g_nd.mySecret, -1, w, 70);
    sb_add(&g_ndn.log, L"seed\t%s\n", w);
  }
  g_nd.turn = 0;
  g_nd.ready = FALSE;
  g_nd.resigned = FALSE;
  g_nd.rolled = FALSE;
  g_nd.nlast = 0;
  g_nd.lastD1 = g_nd.lastD2 = 0;
  g_nd.lastOpp = FALSE;
  g_nd.winner = -1;
  if (g_ndn.me == 1) nd_status(L"Готовим кубики…");
}

/* своё число — в nardy.txt рядом с заметками: без него не открыть кубики */
static void nd_extra(wchar_t *out, int cap) {
  wchar_t sec[40];
  MultiByteToWideChar(CP_UTF8, 0, g_nd.mySecret, -1, sec, 40);
  _snwprintf(out, cap, L"secret\t%s\n", sec);
  out[cap - 1] = 0;
}

static void nd_legal_now(NdStep *st, int *n) {
  *n = nd_legal_steps(&g_nd.work, g_ndn.me, g_nd.dice, g_nd.ndice, st, 64);
}

static void nd_update_turn_status(void) {
  if (!nd_my_turn() || !g_nd.rolled) return;
  NdStep st[64];
  int n;
  nd_legal_now(st, &n);
  if (n == 0 && g_nd.ndone == 0) nd_status(L"Выпало %d и %d — ходов нет. Нажмите «Готово»", g_nd.d1, g_nd.d2);
  else if (n == 0) nd_status(L"Ход сделан — «Готово» или «Отменить»");
  else if (g_nd.sel) nd_status(L"Куда? Подсвечены возможные места");
  else nd_status(L"Выпало %d и %d — выберите шашку", g_nd.d1, g_nd.d2);
}

static void nd_click_point(int rel) {
  if (!nd_my_turn()) return;
  if (!g_nd.rolled) {
    g_nd.rolled = TRUE;
    nd_update_turn_status();
    nd_refresh_view();
    return;
  }
  NdStep st[64];
  int n;
  nd_legal_now(st, &n);
  if (g_nd.sel) {
    /* куда-то можно? если к одной цели ведут два кубика — берём меньший */
    int pick = -1;
    for (int i = 0; i < n; i++)
      if (st[i].from == g_nd.sel && st[i].to == rel && (pick < 0 || st[i].die < st[pick].die)) pick = i;
    if (pick >= 0) {
      g_nd.undo[g_nd.ndone] = g_nd.work;
      g_nd.done[g_nd.ndone++] = st[pick];
      nd_apply(&g_nd.work, g_ndn.me, st[pick]);
      g_nd.ndice = nd_use_die(g_nd.dice, g_nd.ndice, st[pick].die);
      g_nd.sel = 0;
      nd_update_turn_status();
      nd_refresh_view();
      return;
    }
  }
  int isSrc = 0;
  for (int i = 0; i < n; i++)
    if (st[i].from == rel) isSrc = 1;
  g_nd.sel = isSrc ? (g_nd.sel == rel ? 0 : rel) : 0;
  nd_update_turn_status();
  nd_refresh_view();
}

static void nd_undo(void) {
  if (!nd_my_turn() || g_nd.ndone == 0) return;
  g_nd.ndone--;
  g_nd.work = g_nd.undo[g_nd.ndone];
  g_nd.ndice = nd_dice_list(g_nd.d1, g_nd.d2, g_nd.dice);
  for (int i = 0; i < g_nd.ndone; i++) g_nd.ndice = nd_use_die(g_nd.dice, g_nd.ndice, g_nd.done[i].die);
  g_nd.sel = 0;
  nd_update_turn_status();
  nd_refresh_view();
}

static void nd_done(void) {
  if (!nd_my_turn() || !g_nd.rolled) return;
  NdStep st[64];
  int n;
  nd_legal_now(st, &n);
  if (n > 0) {
    nd_status(L"Надо сыграть все кубики, какие можно");
    nd_refresh_view();
    return;
  }
  sb_add(&g_ndn.log, L"t\t%d\t", g_nd.turn);
  if (!g_nd.ndone) sb_add(&g_ndn.log, L"-");
  for (int i = 0; i < g_nd.ndone; i++) sb_add(&g_ndn.log, L"%s%d:%d", i ? L" " : L"", g_nd.done[i].from, g_nd.done[i].die);
  sb_add(&g_ndn.log, L"\n");
  if (!gn_write_mine(&g_ndn)) nd_status(L"Не удалось записать ход в общую папку — повторю сам");
  nd_replay_now();
}

static void nd_resign(void) {
  if (!gn_resign(&g_ndn)) return;
  nd_replay_now();
}

/* ---- доска ------------------------------------------------------------------ */

/* размеры доски в логических точках (×g_ndS) */
#define ND_PW 38    /* ширина пункта */
#define ND_BAR 34
#define ND_TRAY 42
#define ND_H 372
#define ND_TOP 46   /* под шапкой окна */
#define ND_LEFT 14

static int nd_board_w(void) { return 12 * ND_PW + ND_BAR + ND_TRAY + 12; }

static void nd_make_checkers(void) {
  int D = S_(ND_PW) - 4;
  if (g_ndChk[0] && g_ndChkD == D) return;
  for (int k = 0; k < 2; k++)
    if (g_ndChk[k]) DeleteObject(g_ndChk[k]);
  g_ndChkD = D;
  for (int k = 0; k < 2; k++) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = D;
    bi.bmiHeader.biHeight = -D;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    g_ndChk[k] = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    g_ndChkBits[k] = (BYTE *)bits;
    if (!bits) continue;
    float c = D / 2.0f, R = D / 2.0f - 0.5f;
    /* светлая — слоновая кость, тёмная — вишнёво-чёрная; ободок и блик */
    float base[2][3] = {{246, 238, 220}, {70, 30, 34}};
    float edge[2][3] = {{120, 96, 70}, {20, 10, 12}};
    for (int y = 0; y < D; y++) {
      for (int x = 0; x < D; x++) {
        int cov = 0;
        float sr = 0, sg = 0, sb = 0;
        for (int sy = 0; sy < 4; sy++) {
          for (int sx = 0; sx < 4; sx++) {
            float px = x + (sx + 0.5f) / 4.0f - c, py = y + (sy + 0.5f) / 4.0f - c;
            float d = sqrtf(px * px + py * py);
            if (d > R) continue;
            cov++;
            float t = d / R;
            float lit = 1.0f - 0.35f * ((px + py) / (2 * R) + 0.5f); /* свет сверху слева */
            const float *bc = base[k];
            float rr = bc[0] * lit, gg = bc[1] * lit, bb = bc[2] * lit;
            if (t > 0.86f) { rr = edge[k][0]; gg = edge[k][1]; bb = edge[k][2]; }
            else if (t > 0.62f && t < 0.70f) { rr *= 0.86f; gg *= 0.86f; bb *= 0.86f; } /* кольцо-желобок */
            sr += rr > 255 ? 255 : rr;
            sg += gg > 255 ? 255 : gg;
            sb += bb > 255 ? 255 : bb;
          }
        }
        BYTE *p = (BYTE *)bits + ((size_t)y * D + x) * 4;
        if (!cov) {
          p[0] = p[1] = p[2] = p[3] = 0;
          continue;
        }
        float a = cov / 16.0f;
        /* домноженная альфа — для GdiAlphaBlend */
        p[0] = (BYTE)(sb / cov * a);
        p[1] = (BYTE)(sg / cov * a);
        p[2] = (BYTE)(sr / cov * a);
        p[3] = (BYTE)(255 * a);
      }
    }
  }
}

static void nd_draw_checker(HDC hdc, HDC mem, int k, int cx, int cy) {
  if (!g_ndChk[k]) return;
  HGDIOBJ o = SelectObject(mem, g_ndChk[k]);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  GdiAlphaBlend(hdc, cx - g_ndChkD / 2, cy - g_ndChkD / 2, g_ndChkD, g_ndChkD, mem, 0, 0, g_ndChkD,
                g_ndChkD, bf);
  SelectObject(mem, o);
}

/* левый край столбца col (0..11) */
static int nd_col_x(int col) {
  int x = S_(ND_LEFT + 6);
  if (col < 6) return x + col * S_(ND_PW);
  return x + 6 * S_(ND_PW) + S_(ND_BAR) + (col - 6) * S_(ND_PW);
}

/* свой номер пункта → столбец и верх/низ (как видит игрок: его дом внизу справа) */
static void nd_rel_pos(int rel, int *col, int *top) {
  if (rel <= 12) {
    *col = 12 - rel;
    *top = 0;
  } else {
    *col = rel - 13;
    *top = 1;
  }
}

static void nd_ring(HDC hdc, int cx, int cy, int r, COLORREF c, int w) {
  HPEN pn = CreatePen(PS_SOLID, w, c);
  HGDIOBJ op = SelectObject(hdc, pn), ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);
  SelectObject(hdc, op);
  SelectObject(hdc, ob);
  DeleteObject(pn);
}

static void nd_fill(HDC hdc, RECT r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(hdc, &r, b);
  DeleteObject(b);
}

static void nd_draw_die(HDC hdc, int x, int y, int sz, int v, BOOL used, BOOL mine) {
  COLORREF face = used ? RGB(200, 196, 188) : (mine ? RGB(252, 250, 244) : RGB(70, 30, 34));
  COLORREF pip = mine ? RGB(40, 30, 30) : RGB(246, 238, 220);
  if (used) pip = RGB(150, 146, 140);
  RECT r = {x, y, x + sz, y + sz};
  fill_round_rect(hdc, r, face, RGB(60, 40, 30), S_(5));
  static const int P[7][7][2] = {
      {{0}}, {{1, 1}}, {{0, 0}, {2, 2}}, {{0, 0}, {1, 1}, {2, 2}},
      {{0, 0}, {2, 0}, {0, 2}, {2, 2}}, {{0, 0}, {2, 0}, {1, 1}, {0, 2}, {2, 2}},
      {{0, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {2, 2}}};
  HBRUSH pb = CreateSolidBrush(pip);
  HGDIOBJ ob = SelectObject(hdc, pb), op = SelectObject(hdc, GetStockObject(NULL_PEN));
  int pr = sz / 10 + 1;
  for (int i = 0; i < v && v <= 6; i++) {
    int px = x + sz / 4 + P[v][i][0] * sz / 4, py = y + sz / 4 + P[v][i][1] * sz / 4;
    Ellipse(hdc, px - pr, py - pr, px + pr + 1, py + pr + 1);
  }
  SelectObject(hdc, ob);
  SelectObject(hdc, op);
  DeleteObject(pb);
}

static int nd_pips(const NdBoard *b, int side) {
  int p = b->bar[side] * 25;
  for (int r = 1; r <= 24; r++) p += nd_own(b, side, r) * r;
  return p;
}

static void nd_paint_board(HDC hdc) {
  nd_make_checkers();
  const NdBoard *b = nd_my_turn() && g_nd.rolled ? &g_nd.work : &g_nd.board;
  int me = g_ndn.me, op = 1 - me;
  int bx = S_(ND_LEFT), by = S_(ND_TOP), bw = S_(nd_board_w()), bh = S_(ND_H);
  RECT fr = {bx, by, bx + bw, by + bh};
  fill_round_rect(hdc, fr, RGB(104, 64, 34), RGB(60, 36, 18), S_(8)); /* рама */
  int fx = nd_col_x(0), fy = by + S_(8), fh = bh - S_(16);
  int fxR = nd_col_x(11) + S_(ND_PW);
  RECT field1 = {fx, fy, fx + 6 * S_(ND_PW), fy + fh};
  RECT field2 = {nd_col_x(6), fy, fxR, fy + fh};
  nd_fill(hdc, field1, RGB(238, 222, 190));
  nd_fill(hdc, field2, RGB(238, 222, 190));
  int ph = fh * 42 / 100; /* высота треугольника */
  NdStep st[64];
  int nst = 0;
  BOOL play = nd_my_turn() && g_nd.rolled;
  if (play) nd_legal_now(st, &nst);
  for (int rel = 1; rel <= 24; rel++) {
    int col, top;
    nd_rel_pos(rel, &col, &top);
    int x = nd_col_x(col), w = S_(ND_PW);
    POINT tri[3];
    if (top) {
      tri[0].x = x; tri[0].y = fy;
      tri[1].x = x + w; tri[1].y = fy;
      tri[2].x = x + w / 2; tri[2].y = fy + ph;
    } else {
      tri[0].x = x; tri[0].y = fy + fh;
      tri[1].x = x + w; tri[1].y = fy + fh;
      tri[2].x = x + w / 2; tri[2].y = fy + fh - ph;
    }
    COLORREF pc = (col % 2 == top) ? RGB(168, 56, 48) : RGB(56, 78, 92);
    HBRUSH pb = CreateSolidBrush(pc);
    HGDIOBJ ob = SelectObject(hdc, pb), opn = SelectObject(hdc, GetStockObject(NULL_PEN));
    Polygon(hdc, tri, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, opn);
    DeleteObject(pb);
  }
  HDC mem = CreateCompatibleDC(hdc);
  int D = g_ndChkD, step = D;
  for (int rel = 1; rel <= 24; rel++) {
    int col, top;
    nd_rel_pos(rel, &col, &top);
    int cx = nd_col_x(col) + S_(ND_PW) / 2;
    int mine = nd_own(b, me, rel), theirs = nd_own(b, op, 25 - rel);
    int cnt = mine ? mine : theirs, k = mine ? 0 : 1;
    int shown = cnt > 5 ? 5 : cnt;
    int st5 = cnt > 5 ? (ph + S_(20)) / 5 : step; /* плотнее, если больше пяти */
    if (st5 > step) st5 = step;
    for (int i = 0; i < shown; i++) {
      int cy = top ? fy + D / 2 + 1 + i * st5 : fy + fh - D / 2 - 1 - i * st5;
      nd_draw_checker(hdc, mem, k, cx, cy);
    }
    int lastY = top ? fy + D / 2 + 1 + (shown - 1) * st5 : fy + fh - D / 2 - 1 - (shown - 1) * st5;
    if (cnt > 5) {
      wchar_t num[8];
      _snwprintf(num, 8, L"%d", cnt);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, k == 0 ? RGB(60, 40, 30) : RGB(246, 238, 220));
      RECT tr = {cx - D / 2, lastY - D / 2, cx + D / 2, lastY + D / 2};
      DrawTextW(hdc, num, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    /* подсветки: откуда можно ходить, выбранная, куда можно */
    if (play && cnt && mine) {
      BOOL src = FALSE;
      for (int i = 0; i < nst; i++)
        if (st[i].from == rel) src = TRUE;
      if (src) nd_ring(hdc, cx, lastY, D / 2 - 1, g_nd.sel == rel ? RGB(255, 190, 0) : RGB(230, 200, 110),
                       g_nd.sel == rel ? S_(3) : 1);
    }
    if (play && g_nd.sel) {
      for (int i = 0; i < nst; i++) {
        if (st[i].from != g_nd.sel || st[i].to != rel) continue;
        int ty = top ? fy + D / 2 + 1 + (cnt < 5 ? cnt : 4) * st5 : fy + fh - D / 2 - 1 - (cnt < 5 ? cnt : 4) * st5;
        HBRUSH gb = CreateSolidBrush(RGB(60, 170, 90));
        HGDIOBJ ob = SelectObject(hdc, gb), opn = SelectObject(hdc, GetStockObject(NULL_PEN));
        int r = D / 4;
        Ellipse(hdc, cx - r, ty - r, cx + r + 1, ty + r + 1);
        SelectObject(hdc, ob);
        SelectObject(hdc, opn);
        DeleteObject(gb);
        break;
      }
    }
    /* куда пришли шашки соперника последним ходом */
    if (!play && theirs) {
      for (int i = 0; i < g_nd.nlast; i++)
        if (g_nd.lastTo[i] == nd_abs(me, rel)) nd_ring(hdc, cx, lastY, D / 2 - 1, RGB(70, 140, 230), S_(2));
    }
  }
  /* бар: мои — снизу, соперника — сверху */
  int barX = nd_col_x(6) - S_(ND_BAR) / 2 - 0;
  int barCx = nd_col_x(5) + S_(ND_PW) + S_(ND_BAR) / 2;
  (void)barX;
  for (int i = 0; i < b->bar[me] && i < 4; i++)
    nd_draw_checker(hdc, mem, 0, barCx, fy + fh / 2 + D / 2 + S_(6) + i * (D * 2 / 3));
  for (int i = 0; i < b->bar[op] && i < 4; i++)
    nd_draw_checker(hdc, mem, 1, barCx, fy + fh / 2 - D / 2 - S_(6) - i * (D * 2 / 3));
  if (play && b->bar[me]) {
    BOOL src = FALSE;
    for (int i = 0; i < nst; i++)
      if (st[i].from == 25) src = TRUE;
    if (src)
      nd_ring(hdc, barCx, fy + fh / 2 + D / 2 + S_(6), D / 2 - 1,
              g_nd.sel == 25 ? RGB(255, 190, 0) : RGB(230, 200, 110), g_nd.sel == 25 ? S_(3) : 1);
  }
  /* лоток выброса справа: мои — снизу, соперника — сверху */
  int trX = fxR + S_(6), trW = S_(ND_TRAY) - S_(8);
  RECT trTop = {trX, fy, trX + trW, fy + fh / 2 - S_(4)}, trBot = {trX, fy + fh / 2 + S_(4), trX + trW, fy + fh};
  nd_fill(hdc, trTop, RGB(80, 48, 24));
  nd_fill(hdc, trBot, RGB(80, 48, 24));
  int slab = (fh / 2 - S_(8)) / 15;
  if (slab < 2) slab = 2;
  for (int i = 0; i < b->off[me]; i++) {
    RECT s = {trX + S_(3), trBot.bottom - S_(2) - (i + 1) * slab, trX + trW - S_(3), trBot.bottom - S_(2) - i * slab - 1};
    nd_fill(hdc, s, RGB(240, 232, 212));
  }
  for (int i = 0; i < b->off[op]; i++) {
    RECT s = {trX + S_(3), trTop.top + S_(2) + i * slab, trX + trW - S_(3), trTop.top + S_(2) + (i + 1) * slab - 1};
    nd_fill(hdc, s, RGB(90, 40, 44));
  }
  if (play && g_nd.sel) {
    for (int i = 0; i < nst; i++)
      if (st[i].from == g_nd.sel && st[i].to == 0) {
        HPEN pn = CreatePen(PS_SOLID, S_(3), RGB(60, 170, 90));
        HGDIOBJ opn = SelectObject(hdc, pn), ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, trBot.left - 1, trBot.top - 1, trBot.right + 1, trBot.bottom + 1);
        SelectObject(hdc, opn);
        SelectObject(hdc, ob);
        DeleteObject(pn);
        break;
      }
  }
  DeleteDC(mem);
  /* кубики: мои — в правой половине, соперника — в левой */
  int dsz = S_(30), dy = fy + fh / 2 - dsz / 2;
  if (g_ndn.mode == GN_PLAY && g_nd.ready) {
    if (nd_my_turn() && g_nd.rolled) {
      int dx = nd_col_x(8) - dsz / 2;
      int all[4], na = nd_dice_list(g_nd.d1, g_nd.d2, all), left[4], nl = g_nd.ndice;
      memcpy(left, g_nd.dice, sizeof(left));
      for (int i = 0; i < na; i++) {
        BOOL still = FALSE;
        for (int j = 0; j < nl; j++)
          if (left[j] == all[i]) {
            still = TRUE;
            left[j] = 0;
            break;
          }
        nd_draw_die(hdc, dx + i * (dsz + S_(8)), dy, dsz, all[i], !still, TRUE);
      }
    } else if (g_nd.lastOpp && g_nd.lastD1) {
      int dx = nd_col_x(1);
      nd_draw_die(hdc, dx, dy, dsz, g_nd.lastD1, FALSE, FALSE);
      nd_draw_die(hdc, dx + dsz + S_(8), dy, dsz, g_nd.lastD2, FALSE, FALSE);
    }
  }
  /* очки пути под доской */
  if (g_ndn.mode == GN_PLAY || g_ndn.mode == GN_OVER) {
    wchar_t t[120];
    _snwprintf(t, 120, L"путь: вы %d · %s %d", nd_pips(b, me), g_ndn.oppName, nd_pips(b, op));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_MUTED);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    RECT tr = {bx, by + bh + S_(2), bx + bw, by + bh + S_(18)};
    DrawTextW(hdc, t, -1, &tr, DT_RIGHT | DT_SINGLELINE);
  }
}

/* куда пришёлся щелчок: свой номер пункта, 25 — бар, 0 — лоток, -1 — мимо */
static int nd_hit(int x, int y) {
  int by = S_(ND_TOP), bh = S_(ND_H);
  int fy = by + S_(8), fh = bh - S_(16);
  if (y < fy || y > fy + fh) return -1;
  int top = y < fy + fh / 2;
  for (int col = 0; col < 12; col++) {
    int cx = nd_col_x(col);
    if (x >= cx && x < cx + S_(ND_PW)) return top ? col + 13 : 12 - col;
  }
  int barL = nd_col_x(5) + S_(ND_PW);
  if (x >= barL && x < barL + S_(ND_BAR)) return 25;
  int trX = nd_col_x(11) + S_(ND_PW);
  if (x >= trX && x < trX + S_(ND_TRAY)) return 0;
  return -1;
}

/* ---- окно ------------------------------------------------------------------- */

static void nd_layout(void) {
  if (!g_ndWnd) return;
  RECT rc;
  GetClientRect(g_ndWnd, &rc);
  place_panel_close(g_ndWnd);
  gn_layout_lobby(&g_ndn, g_nd.poll);
  int m = g_ndn.mode, pad = S_(14), bh = S_(28), gap = S_(8);
  int by = S_(ND_TOP) + S_(ND_H) + S_(28);
  HWND b[5];
  int ids[5] = {ID_ND_ROLL, ID_ND_UNDO, ID_ND_DONE, ID_ND_RESIGN, ID_ND_BACK};
  for (int i = 0; i < 5; i++) b[i] = GetDlgItem(g_ndWnd, ids[i]);
  BOOL play = m == GN_PLAY, over = m == GN_OVER;
  BOOL my = nd_my_turn();
  ShowWindow(b[0], play && my && !g_nd.rolled ? SW_SHOW : SW_HIDE);
  ShowWindow(b[1], play && my && g_nd.rolled ? SW_SHOW : SW_HIDE);
  ShowWindow(b[2], play && my && g_nd.rolled ? SW_SHOW : SW_HIDE);
  ShowWindow(b[3], play ? SW_SHOW : SW_HIDE);
  ShowWindow(b[4], over ? SW_SHOW : SW_HIDE);
  int x = pad;
  if (play && my && !g_nd.rolled) {
    MoveWindow(b[0], x, by, S_(150), bh, TRUE);
    x += S_(150) + gap;
  }
  if (play && my && g_nd.rolled) {
    MoveWindow(b[1], x, by, S_(110), bh, TRUE);
    x += S_(110) + gap;
    MoveWindow(b[2], x, by, S_(100), bh, TRUE);
    x += S_(100) + gap;
    EnableWindow(b[1], g_nd.ndone > 0);
    NdStep st[64];
    int n;
    nd_legal_now(st, &n);
    EnableWindow(b[2], n == 0);
  }
  if (play) MoveWindow(b[3], rc.right - pad - S_(100), by, S_(100), bh, TRUE);
  if (over) MoveWindow(b[4], pad, by, S_(160), bh, TRUE);
}

/* кнопка «Нарды» в окне курсора: точка, когда вас ждут — ваш ход или зовут */
static void nd_mark_btn(void) { gn_mark_btn(&g_ndn, g_btnNd, L"Нарды", nd_my_turn()); }

static void nd_refresh_view(void) {
  nd_mark_btn();
  if (!g_ndWnd) return;
  if (g_ndn.mode == GN_LOBBY) gn_list_sync(&g_ndn, GetDlgItem(g_ndWnd, ID_ND_LIST), g_nd.poll);
  nd_layout();
  InvalidateRect(g_ndWnd, NULL, FALSE);
}

static void nd_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Нарды");
  if (g_ndn.mode == GN_LOBBY) {
    gn_paint_lobby(&g_ndn, hdc, rc, g_nd.poll,
                   L"Короткие нарды: кубики честные — их нельзя подобрать ни одному из двоих. Дубль — четыре "
                   L"хода, одинокую шашку можно побить; выбрасывать — когда все пятнадцать дома.");
    return;
  }
  if (g_ndn.mode == GN_WAIT) {
    gn_paint_wait(&g_ndn, hdc, rc);
    return;
  }
  nd_paint_board(hdc);
  int pad = S_(14);
  /* справа в этой же строке — «путь: вы … · соперник …» */
  RECT st = {pad, S_(ND_TOP) + S_(ND_H) + S_(4), rc.right - pad - S_(170), S_(ND_TOP) + S_(ND_H) + S_(26)};
  SetTextColor(hdc, g_ndn.mode == GN_OVER ? COL_SAGE : COL_INK);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  DrawTextW(hdc, g_ndn.status, -1, &st, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  wchar_t dg[400];
  gn_diag_text(&g_ndn, dg, 400);
  RECT dr = {pad, S_(ND_TOP) + S_(ND_H) + S_(62), rc.right - pad, rc.bottom - S_(4)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, g_ndn.writeFail ? ui_warn(RGB(200, 60, 40)) : COL_MUTED);
  DrawTextW(hdc, dg, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

static LRESULT CALLBACK NardyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    /* в памяти, потом одним движением — доска не мигает */
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    nd_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_LBUTTONDOWN: {
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    if (g_ndn.mode == GN_PLAY) {
      int rel = nd_hit(x, y);
      if (rel >= 0) nd_click_point(rel);
    }
    return 0;
  }
  case WM_RBUTTONDOWN:
    /* правая — снять выбор */
    g_nd.sel = 0;
    nd_update_turn_status();
    nd_refresh_view();
    return 0;
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE); /* спрятать быстро */
    if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) nd_undo();
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
    /* у кнопок — только щелчок (двойной присылает ещё BN_DOUBLECLICKED) */
    if (id != ID_ND_LIST && HIWORD(wParam) != BN_CLICKED) return 0;
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (gn_command(&g_ndn, id, HIWORD(wParam)) && id == ID_ND_LIST) return 0; /* фокус у списка не отнимаем */
    if (id == ID_ND_ROLL) nd_click_point(-1);
    if (id == ID_ND_UNDO) nd_undo();
    if (id == ID_ND_DONE) nd_done();
    if (id == ID_ND_RESIGN) nd_resign();
    if (id == ID_ND_BACK) gn_back_to_lobby(&g_ndn);
    SetFocus(hwnd); /* чтобы Esc и Ctrl+Z доходили до окна */
    return 0;
  }
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void nardy_start(void) {
  if (g_ndThread) return;
  g_ndThread = TRUE;
  wchar_t *t = gn_load_local(&g_ndn);
  if (t) {
    wchar_t v[40];
    if (nd_field(t, L"secret", v, 40)) WideCharToMultiByte(CP_UTF8, 0, v, -1, g_nd.mySecret, 40, NULL, NULL);
    free(t);
  }
  HANDLE th = CreateThread(NULL, 0, nd_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  gn_start(&g_ndn);
}

static void nardy_show(void) {
  if (!g_ndWnd) {
    HDC s = GetDC(NULL);
    g_ndS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    g_ndn.sc = g_ndS;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = NardyProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadNardy";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    int w = S_(ND_LEFT * 2 + nd_board_w()), h = S_(ND_TOP + ND_H + 108);
    g_ndWnd = CreateWindowExW(WS_EX_APPWINDOW, L"CursorPadNardy", L"Нарды",
                              WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU, 0, 0, w, h,
                              NULL, NULL, g_inst, NULL);
    if (!g_ndWnd) return;
    round_corners(g_ndWnd);
    mk_btn(g_ndWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_ndWnd, L"–", ID_PANEL_MIN);
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                0, 0, 100, 100, g_ndWnd, (HMENU)(INT_PTR)ID_ND_LIST, g_inst, NULL);
    const struct { const wchar_t *t; int id; } btns[] = {
        {L"Позвать играть", ID_ND_INVITE}, {L"Принять", ID_ND_ACCEPT}, {L"Отказаться", ID_ND_DECLINE},
        {L"Бросить кубики", ID_ND_ROLL}, {L"Отменить ход", ID_ND_UNDO}, {L"Готово", ID_ND_DONE},
        {L"Сдаться", ID_ND_RESIGN}, {L"К списку игроков", ID_ND_BACK}, {L"Отменить приглашение", ID_ND_CANCEL}};
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
      HWND b = mk_btn(g_ndWnd, btns[i].t, btns[i].id);
      if (g_fontUi) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    }
    if (g_fontBody) SendMessageW(list, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_ndWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2,
                 w, h, SWP_NOZORDER);
  }
  if (g_ndn.mode == GN_LOBBY && !g_ndn.status[0]) nd_status(L"");
  nd_refresh_view();
  ShowWindow(g_ndWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_ndWnd);
  SetFocus(g_ndWnd);
}
