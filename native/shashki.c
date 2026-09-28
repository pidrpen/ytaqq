/* ---- Русские шашки по сети через общую папку ------------------------------

   Как нарды (nardy.c) и через ту же общую папку из Настроек; кто в сети —
   тот же список, что у нард (их поток отмечает «я в сети»). Свои файлы — в
   <папка>\CursorPad-Shashki:

     inv\<кому>\<партия>.txt   — приглашение
     games\<партия>\p0.txt     — пишет пригласивший (сторона 0)
     games\<партия>\p1.txt     — пишет принявший (сторона 1)

   Каждый пишет только свой файл (целиком, через временный) и читает чужой.
   Строки: name, id, m (ход: «m\t<номер полухода>\t<путь>», путь — «c3-d4»
   или «c3:e5:c7»), draw (предлагаю ничью на полуходе N), drawok (согласен),
   resign, decline, cancel. Кто белые — по номеру партии, обе программы
   считают одинаково. Каждый ход программа соперника проверяет по правилам
   (shashki_rules.c); партия восстанавливается из файлов целиком — после
   перезапуска доска та же. Ход уходит сразу, как только путь шашки дошёл до
   конца (со взятием — до последнего поля).

   С 2026.09.23.73 каждый свой файл партии программа вдобавок кладёт копией в
   папку соперника mv\<кому>\ — новым файлом, как сообщение чата: соперник
   забирает его и удаляет. Файл партии, переписываемый на месте, на
   рабочей сети до соперника не доходил (чат и приглашения — новые файлы —
   доходили); из двух — файла партии и копии — берётся более длинный: запись
   только дописывается, длиннее — значит новее. Своя запись хранится ещё и
   на своём компьютере (shashki_log.txt) — после перезапуска партия
   восстанавливается, даже если общая папка свой файл не отдаёт. */

#include "shashki_rules.c"

#define WM_SHASHKI_POLL (WM_APP + 63) /* lParam — ShPoll*, освобождает получатель */
#define SH_SUB L"CursorPad-Shashki"
/* ID_SH_* — в cursorpad.c */

enum { SH_LOBBY, SH_WAIT, SH_PLAY, SH_OVER };

#define SH_MAXINV 8
typedef struct {
  int nInv;
  wchar_t invGame[SH_MAXINV][96], invFrom[SH_MAXINV][96], invName[SH_MAXINV][128];
  wchar_t game[96];
  wchar_t *p0, *p1;
  BOOL partial[2];
  /* для строки «связь» под доской: когда файл менялся, почему не прочёлся,
     когда читали */
  ULONGLONG mt[2];
  DWORD rerr[2];
  ULONGLONG readAt;
  /* копии от соперника (mv\<я>): сколько пришло, когда последняя; взята ли
     из копии, а не из файла партии; забрал ли соперник мою: −1 — не знаю */
  int mvIn;
  ULONGLONG mvInAt;
  BOOL viaMv[2];
  int mvTaken;
} ShPoll;

typedef struct {
  int mode;
  wchar_t game[96];
  int me; /* 0 — пригласил, 1 — принял */
  wchar_t oppId[96], oppName[128];
  int white; /* какая сторона (0/1) играет белыми */
  ShBoard board;
  int ply;   /* номер полухода на очереди, с 1: нечётный — белых */
  int quiet; /* полуходов подряд только дамками и без взятий */
  int winner; /* сторона-победитель; −1 — ничья или партия остановлена */
  BOOL draw, resigned;
  wchar_t endWhy[200];
  /* мой ход: выбранная шашка и пройденная часть пути */
  unsigned char part[SH_MAXPATH];
  int npart;
  /* последний ход — подсветить */
  unsigned char lastPath[SH_MAXPATH];
  int nlast;
  BOOL lastOpp;
  BOOL oppOffersDraw, iOffered;
  ShareBuf log;
  BOOL logLoaded;
  wchar_t status[240];
  ShPoll *poll;
  wchar_t seenInv[16][96];
  int nSeenInv;
  int notifiedPly;
  /* встречные приглашения: он отказался от моей партии, потому что звал меня
     в свою, — его приглашение принять сразу, как придёт */
  wchar_t swapFrom[96];
  ULONGLONG swapT;
  /* приглашения, на которые уже ответили: поток помнит список до 5 с —
     чтобы то же самое не всплывало и не обрабатывалось снова */
  wchar_t doneInv[8][96];
  int nDoneInv;
  /* своя запись в папку: когда удалась, не сорвалась ли последняя */
  ULONGLONG wroteAt;
  BOOL writeFail, fileFail;
  /* копия своей записи в папке соперника */
  unsigned mvHash;
  wchar_t mvPath[SHARE_PATH];
  ULONGLONG mvAt;
  BOOL mvFail;
} ShGame;

static ShGame g_sh;
static HWND g_shWnd, g_shList;
static wchar_t g_shWatch[96]; /* какую партию читать потоку (под nd_lock) */
static wchar_t g_shMvPath[SHARE_PATH]; /* моя последняя копия — забрал ли её соперник (под nd_lock) */
/* кто сейчас в списке игроков — по строкам списка, а не по свежему «кто в
   сети»: тот мог смениться, и выбор перескакивал на другого */
static wchar_t g_shListId[ND_MAXONLINE][96], g_shListName[ND_MAXONLINE][128];
static int g_shListN;
static float g_shS = 1.0f;
static HBITMAP g_shPc[2]; /* 0 — белая, 1 — чёрная */
static int g_shPcD;
static BOOL g_shThread;

static int SS_(int v) { return (int)(v * g_shS + 0.5f); }

/* ---- мелочи ---------------------------------------------------------------- */

static BOOL sh_dir(const wchar_t *root, const wchar_t *sub, wchar_t *out) {
  if (!root[0]) return FALSE;
  _snwprintf(out, SHARE_PATH, L"%s\\%s", root, SH_SUB);
  out[SHARE_PATH - 1] = 0;
  CreateDirectoryW(out, NULL);
  if (sub && sub[0]) {
    wchar_t part[SHARE_PATH];
    lstrcpynW(part, sub, SHARE_PATH);
    wchar_t *p = part;
    while (p && *p) {
      wchar_t *slash = wcschr(p, L'\\');
      if (slash) *slash = 0;
      size_t l = wcslen(out);
      _snwprintf(out + l, SHARE_PATH - l, L"\\%s", p);
      out[SHARE_PATH - 1] = 0;
      CreateDirectoryW(out, NULL);
      p = slash ? slash + 1 : NULL;
    }
  }
  DWORD a = GetFileAttributesW(out);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* кто белые: по номеру партии — у обоих одинаково, а заранее не угадать */
static int sh_white_side(const wchar_t *game) {
  unsigned h = 2166136261u;
  for (const wchar_t *p = game; *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
  return (int)((h >> 7) & 1);
}

static int sh_color_of_side(int side) { return side == g_sh.white ? 1 : -1; }
static int sh_side_of_ply(int ply) { return (ply % 2 == 1) ? g_sh.white : 1 - g_sh.white; }
static int sh_my_color(void) { return sh_color_of_side(g_sh.me); }

/* «m\t<ply>\t<путь>» — путь из файла; FALSE — такого хода нет */
static BOOL sh_move_line(const wchar_t *text, int ply, char *path, int cap) {
  path[0] = 0;
  if (!text) return FALSE;
  wchar_t key[24];
  _snwprintf(key, 24, L"m\t%d\t", ply);
  size_t kl = wcslen(key);
  for (const wchar_t *p = text; p && *p;) {
    if (!wcsncmp(p, key, kl)) {
      const wchar_t *q = p + kl;
      int k = 0;
      while (*q && *q != L'\n' && *q != L'\r' && k < cap - 1) {
        path[k++] = *q < 128 ? (char)*q : '?';
        q++;
      }
      path[k] = 0;
      return TRUE;
    }
    p = wcschr(p, L'\n');
    if (p) p++;
  }
  return FALSE;
}

/* есть ли строка «key\t<n>» */
static BOOL sh_has_num(const wchar_t *text, const wchar_t *key, int n) {
  wchar_t line[40];
  _snwprintf(line, 40, L"%s\t%d", key, n);
  return nd_has_line(text, line);
}

/* ---- файлы ---------------------------------------------------------------- */

static void sh_game_path(const wchar_t *game, int side, wchar_t *out) {
  wchar_t root[MAX_PATH], sub[200], dir[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 200, L"games\\%s", game);
  out[0] = 0;
  if (!sh_dir(root, sub, dir)) return;
  _snwprintf(out, SHARE_PATH, L"%s\\p%d.txt", dir, side);
  out[SHARE_PATH - 1] = 0;
}

static unsigned sh_hash(const wchar_t *a, const wchar_t *b, int side) {
  unsigned h = 2166136261u ^ (unsigned)side;
  for (const wchar_t *p = a; p && *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
  h = (h ^ 0x7Cu) * 16777619u;
  for (const wchar_t *p = b; p && *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
  return h;
}

/* Копия своей записи — новым файлом в папку соперника mv\<кому>\, как
   сообщение чата. track — копия текущей партии: одинаковую второй раз не
   шлём, прежнюю незабранную убираем (в новой всё то же и новее). */
static BOOL sh_send_copy(const wchar_t *game, int side, const wchar_t *to, const wchar_t *text, BOOL track) {
  if (!game[0] || !to[0] || !text || !text[0]) return FALSE;
  unsigned h = sh_hash(game, text, side);
  if (track && h == g_sh.mvHash) return TRUE;
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 160, L"mv\\%s", to);
  sub[159] = 0;
  BOOL ok = FALSE;
  if (sh_dir(root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s-%d-%llx.txt", dir, game, side, ft_now());
    path[SHARE_PATH - 1] = 0;
    size_t n = wcslen(text) + 240;
    wchar_t *body = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (body) {
      _snwprintf(body, n, L"game\t%s\nside\t%d\n%s", game, side, text);
      body[n - 1] = 0;
      ok = share_write(path, body);
      free(body);
    }
  }
  if (track) {
    g_sh.mvFail = !ok;
    if (ok) {
      if (g_sh.mvPath[0]) DeleteFileW(g_sh.mvPath);
      lstrcpynW(g_sh.mvPath, path, SHARE_PATH);
      nd_lock();
      lstrcpynW(g_shMvPath, path, SHARE_PATH);
      nd_unlock();
      g_sh.mvHash = h;
      g_sh.mvAt = ft_now();
    }
  }
  return ok;
}

/* своя запись — ещё и на своём компьютере: после перезапуска партия
   восстанавливается, даже если общая папка свой файл не отдаёт */
static void sh_log_local_path(wchar_t *out) { _snwprintf(out, MAX_PATH, L"%s\\shashki_log.txt", g_dataDir); }

static void sh_save_log_local(void) {
  if (!g_dataDir[0] || !g_sh.game[0] || !g_sh.log.w) return;
  wchar_t path[MAX_PATH];
  sh_log_local_path(path);
  size_t n = wcslen(g_sh.log.w) + 140;
  wchar_t *b = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (!b) return;
  _snwprintf(b, n, L"game\t%s\n%s", g_sh.game, g_sh.log.w);
  b[n - 1] = 0;
  share_write(path, b);
  free(b);
}

static BOOL sh_write_mine(void) {
  wchar_t path[SHARE_PATH];
  sh_game_path(g_sh.game, g_sh.me, path);
  BOOL ok = path[0] && g_sh.log.w && share_write_ex(path, g_sh.log.w, TRUE);
  g_sh.fileFail = !ok;
  if (ok) g_sh.wroteAt = ft_now();
  BOOL sent = g_sh.log.w && sh_send_copy(g_sh.game, g_sh.me, g_sh.oppId, g_sh.log.w, TRUE);
  sh_save_log_local();
  g_sh.writeFail = !ok && !sent; /* дошло хоть одним путём — не пугаем */
  return ok || sent;
}

static void sh_log_reset(void) {
  free(g_sh.log.w);
  memset(&g_sh.log, 0, sizeof(g_sh.log));
}

static void sh_save_local(void) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%s\\shashki.txt", g_dataDir);
  if (g_sh.mode != SH_WAIT && g_sh.mode != SH_PLAY) {
    DeleteFileW(path);
    sh_log_local_path(path);
    DeleteFileW(path);
    return;
  }
  wchar_t txt[600];
  _snwprintf(txt, 600, L"game\t%s\nme\t%d\nopp\t%s\noppname\t%s\nmode\t%d\n", g_sh.game, g_sh.me, g_sh.oppId,
             g_sh.oppName, g_sh.mode);
  txt[599] = 0;
  share_write(path, txt);
}

static void sh_load_local(void) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%s\\shashki.txt", g_dataDir);
  wchar_t *t = share_read(path);
  if (!t) return;
  wchar_t v[200];
  if (nd_field(t, L"game", v, 96) && v[0]) {
    lstrcpynW(g_sh.game, v, 96);
    if (nd_field(t, L"me", v, 8)) g_sh.me = _wtoi(v) ? 1 : 0;
    nd_field(t, L"opp", g_sh.oppId, 96);
    nd_field(t, L"oppname", g_sh.oppName, 128);
    g_sh.mode = (nd_field(t, L"mode", v, 8) && _wtoi(v) == SH_WAIT) ? SH_WAIT : SH_PLAY;
    g_sh.white = sh_white_side(g_sh.game);
    sh_start(&g_sh.board);
    g_sh.ply = 1;
    g_sh.logLoaded = FALSE; /* свой файл дочитаем из папки */
    {
      /* или со своего компьютера — если там та же партия */
      wchar_t lp[MAX_PATH], g[96] = L"";
      sh_log_local_path(lp);
      wchar_t *lt = share_read(lp);
      const wchar_t *body = lt ? wcschr(lt, L'\n') : NULL;
      if (lt && body && nd_field(lt, L"game", g, 96) && !wcscmp(g, g_sh.game) && body[1]) {
        sh_log_reset();
        sb_add(&g_sh.log, L"%s", body + 1);
        g_sh.logLoaded = TRUE;
      }
      free(lt);
    }
    nd_lock();
    lstrcpynW(g_shWatch, g_sh.game, 96);
    nd_unlock();
  }
  free(t);
}

/* ---- фоновый поток: приглашения и файлы партии ----------------------------- */

/* копия записи: «game\t…», «side\t…», дальше сама запись; NULL — не копия
   (или застали недописанной) */
static const wchar_t *sh_parse_copy(const wchar_t *t, wchar_t *game, int *side) {
  size_t l = t ? wcslen(t) : 0;
  wchar_t sd[8] = L"";
  game[0] = 0;
  if (!l || t[l - 1] != L'\n' || !nd_field(t, L"game", game, 96) || !game[0] || !nd_field(t, L"side", sd, 8))
    return NULL;
  *side = _wtoi(sd) ? 1 : 0;
  const wchar_t *q = wcschr(t, L'\n');
  if (q) q = wcschr(q + 1, L'\n');
  return q && q[1] ? q + 1 : NULL;
}

/* последняя принятая копия — и на своём компьютере: из папки её уже убрали,
   а после перезапуска ходы соперника иначе взять было бы неоткуда */
static void sh_in_path(wchar_t *out) { _snwprintf(out, MAX_PATH, L"%s\\shashki_in.txt", g_dataDir); }

static DWORD WINAPI sh_thread(LPVOID param) {
  (void)param;
  ULONGLONG lastInv = 0, lastPost = 0, lastMv = 0;
  static wchar_t *good[2];
  static wchar_t goodGame[96];
  static ShPoll cur;
  static ULONGLONG invFt[SH_MAXINV];
  /* копии записей соперников из своей папки mv\<я>: по партии и стороне */
  static wchar_t inGame[8][96];
  static int inSide[8], inNext;
  static wchar_t *inText[8];
  unsigned lastSum = 0;
  if (g_dataDir[0]) {
    wchar_t ip[MAX_PATH], g[96];
    int sd1 = 0;
    sh_in_path(ip);
    wchar_t *t = share_read(ip);
    const wchar_t *body = sh_parse_copy(t, g, &sd1);
    if (body) {
      lstrcpynW(inGame[0], g, 96);
      inSide[0] = sd1;
      inText[0] = _wcsdup(body);
      inNext = 1;
    }
    free(t);
  }
  for (;;) {
    Sleep(1000);
    wchar_t root[MAX_PATH];
    share_root_copy(root);
    if (!root[0]) continue;
    wchar_t myId[96], dInv[SHARE_PATH], sub[160];
    nd_myid(myId, 96, NULL, 0);
    ULONGLONG now = GetTickCount64();
    if (!lastInv || now - lastInv > 5000) {
      lastInv = now;
      cur.nInv = 0;
      _snwprintf(sub, 160, L"inv\\%s", myId);
      if (sh_dir(root, sub, dInv)) {
        wchar_t pat[SHARE_PATH + 8];
        WIN32_FIND_DATAW fd;
        _snwprintf(pat, SHARE_PATH + 8, L"%s\\*.txt", dInv);
        HANDLE f = FindFirstFileW(pat, &fd);
        if (f != INVALID_HANDLE_VALUE) {
          do {
            wchar_t full[SHARE_PATH];
            _snwprintf(full, SHARE_PATH, L"%s\\%s", dInv, fd.cFileName);
            if (!ft_within(fd.ftLastWriteTime, 900)) { /* старше 15 минут — уже не ждут */
              DeleteFileW(full);
              continue;
            }
            if (cur.nInv >= SH_MAXINV) continue;
            wchar_t *t = share_read(full);
            if (!t) continue;
            nd_field(t, L"game", cur.invGame[cur.nInv], 96);
            nd_field(t, L"from", cur.invFrom[cur.nInv], 96);
            nd_field(t, L"name", cur.invName[cur.nInv], 128);
            free(t);
            if (!cur.invGame[cur.nInv][0]) continue;
            /* От одного человека — только последнее приглашение: он ждёт ответа
               на одно, старые (двойной щелчок по «Позвать», прошлые попытки)
               устарели. Раньше «Принять» брало первое в списке — соперник
               садился в старую партию, а позвавший ждал в новой (2026.09.23.71) */
            ULONGLONG ft = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            int dup = -1;
            for (int k = 0; k < cur.nInv && dup < 0; k++)
              if (!wcscmp(cur.invFrom[k], cur.invFrom[cur.nInv])) dup = k;
            if (dup < 0) {
              invFt[cur.nInv] = ft;
              cur.nInv++;
              continue;
            }
            wchar_t old[SHARE_PATH];
            if (ft > invFt[dup]) { /* новое свежее — оно вместо старого */
              _snwprintf(old, SHARE_PATH, L"%s\\%s.txt", dInv, cur.invGame[dup]);
              lstrcpynW(cur.invGame[dup], cur.invGame[cur.nInv], 96);
              lstrcpynW(cur.invName[dup], cur.invName[cur.nInv], 128);
              invFt[dup] = ft;
            } else {
              _snwprintf(old, SHARE_PATH, L"%s\\%s.txt", dInv, cur.invGame[cur.nInv]);
            }
            old[SHARE_PATH - 1] = 0;
            DeleteFileW(old);
          } while (FindNextFileW(f, &fd));
          FindClose(f);
        }
      }
    }
    /* копии ходов — новыми файлами в своей папке, как сообщения чата:
       прочитать, удалить, запомнить самую длинную по партии и стороне */
    BOOL watching;
    nd_lock();
    watching = g_shWatch[0] != 0;
    nd_unlock();
    if (watching || !lastMv || now - lastMv > 5000) { /* без партии — реже */
      lastMv = now;
      wchar_t dMv[SHARE_PATH];
      _snwprintf(sub, 160, L"mv\\%s", myId);
      if (sh_dir(root, sub, dMv)) {
        wchar_t pat[SHARE_PATH + 8];
        WIN32_FIND_DATAW fd;
        _snwprintf(pat, SHARE_PATH + 8, L"%s\\*.txt", dMv);
        HANDLE f = FindFirstFileW(pat, &fd);
        if (f != INVALID_HANDLE_VALUE) {
          do {
            wchar_t full[SHARE_PATH];
            _snwprintf(full, SHARE_PATH, L"%s\\%s", dMv, fd.cFileName);
            full[SHARE_PATH - 1] = 0;
            if (!ft_within(fd.ftLastWriteTime, 86400)) { /* сутки пролежало — партия давно не та */
              DeleteFileW(full);
              continue;
            }
            wchar_t *t = share_read(full);
            if (!t) continue;
            wchar_t g[96];
            int sd1 = 0;
            const wchar_t *body = sh_parse_copy(t, g, &sd1);
            DeleteFileW(full);
            if (body) {
              int slot = -1;
              for (int k = 0; k < 8 && slot < 0; k++)
                if (inText[k] && inSide[k] == sd1 && !wcscmp(inGame[k], g)) slot = k;
              if (slot < 0) {
                slot = inNext++ % 8;
                free(inText[slot]);
                inText[slot] = NULL;
                lstrcpynW(inGame[slot], g, 96);
                inSide[slot] = sd1;
              }
              if (!inText[slot] || wcslen(body) > wcslen(inText[slot])) {
                free(inText[slot]);
                inText[slot] = _wcsdup(body);
                if (g_dataDir[0]) {
                  wchar_t ip[MAX_PATH];
                  sh_in_path(ip);
                  share_write(ip, t); /* как пришла: с шапкой */
                }
              }
              cur.mvIn++;
              cur.mvInAt = ft_now();
            }
            free(t);
          } while (FindNextFileW(f, &fd));
          FindClose(f);
        }
      }
    }
    ShPoll *pl = (ShPoll *)calloc(1, sizeof(ShPoll));
    if (!pl) continue;
    memcpy(pl, &cur, sizeof(ShPoll));
    pl->p0 = pl->p1 = NULL;
    wchar_t mvp[SHARE_PATH];
    nd_lock();
    lstrcpynW(pl->game, g_shWatch, 96);
    lstrcpynW(mvp, g_shMvPath, SHARE_PATH);
    nd_unlock();
    /* моя копия у соперника: файла нет — забрал */
    pl->mvTaken = -1;
    if (mvp[0]) {
      if (GetFileAttributesW(mvp) != INVALID_FILE_ATTRIBUTES) pl->mvTaken = 0;
      else if (GetLastError() == ERROR_FILE_NOT_FOUND) pl->mvTaken = 1;
    }
    if (wcscmp(goodGame, pl->game)) {
      free(good[0]);
      free(good[1]);
      good[0] = good[1] = NULL;
      lstrcpynW(goodGame, pl->game, 96);
    }
    if (pl->game[0]) {
      for (int s = 0; s < 2; s++) {
        wchar_t p[SHARE_PATH];
        sh_game_path(pl->game, s, p);
        wchar_t *t = p[0] ? share_read(p) : NULL;
        if (!t) pl->rerr[s] = p[0] ? GetLastError() : ERROR_PATH_NOT_FOUND;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (p[0] && GetFileAttributesExW(p, GetFileExInfoStandard, &fa))
          pl->mt[s] = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
        /* без перевода строки в конце — застали недописанным: берём прошлое целое */
        size_t l = t ? wcslen(t) : 0;
        if (t && (l == 0 || t[l - 1] != L'\n')) {
          free(t);
          t = good[s] ? _wcsdup(good[s]) : NULL;
          pl->partial[s] = TRUE;
        } else if (t) {
          free(good[s]);
          good[s] = _wcsdup(t);
        }
        /* копия из своей папки длиннее — значит, новее: берём её */
        for (int k = 0; k < 8; k++)
          if (inText[k] && inSide[k] == s && !wcscmp(inGame[k], pl->game) &&
              wcslen(inText[k]) > (t ? wcslen(t) : 0)) {
            free(t);
            t = _wcsdup(inText[k]);
            pl->viaMv[s] = TRUE;
          }
        if (s == 0) pl->p0 = t;
        else pl->p1 = t;
      }
      pl->readAt = ft_now();
    }
    unsigned sum = 2166136261u;
#define SH_MIX(ptr, len)                                                          \
  do {                                                                            \
    const BYTE *m_ = (const BYTE *)(ptr);                                         \
    for (size_t i_ = 0; i_ < (size_t)(len); i_++) sum = (sum ^ m_[i_]) * 16777619u; \
  } while (0)
    SH_MIX(pl->invGame, sizeof(pl->invGame[0]) * pl->nInv);
    SH_MIX(pl->game, sizeof(pl->game));
    SH_MIX(pl->partial, sizeof(pl->partial));
    SH_MIX(&pl->mvTaken, sizeof(pl->mvTaken));
    SH_MIX(pl->viaMv, sizeof(pl->viaMv));
    if (pl->p0) SH_MIX(pl->p0, wcslen(pl->p0) * sizeof(wchar_t));
    if (pl->p1) SH_MIX(pl->p1, wcslen(pl->p1) * sizeof(wchar_t));
#undef SH_MIX
    BOOL due = sum != lastSum || (pl->game[0] && now - lastPost > 5000);
    if (!due || !g_hwnd || !PostMessageW(g_hwnd, WM_SHASHKI_POLL, 0, (LPARAM)pl)) {
      free(pl->p0);
      free(pl->p1);
      free(pl);
    } else {
      lastSum = sum;
      lastPost = now;
    }
  }
  return 0;
}

static void sh_poll_free(ShPoll *p) {
  if (!p) return;
  free(p->p0);
  free(p->p1);
  free(p);
}

/* ---- ход партии по файлам ------------------------------------------------- */

static void sh_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_sh.status, 240, fmt, ap);
  va_end(ap);
  g_sh.status[239] = 0;
}

static void sh_balloon(const wchar_t *text) {
  if (!g_trayAdded) return;
  NOTIFYICONDATAW n = g_nid;
  n.uFlags = NIF_INFO;
  lstrcpynW(n.szInfoTitle, L"Шашки", 64);
  lstrcpynW(n.szInfo, text, 256);
  n.dwInfoFlags = NIIF_INFO;
  g_balloonKind = 5; /* щелчок по всплывашке — окно шашек */
  Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void sh_over(int winner, const wchar_t *why) {
  g_sh.mode = SH_OVER;
  g_sh.winner = winner;
  lstrcpynW(g_sh.endWhy, why ? why : L"", 200);
  sh_save_local();
}

static int sh_count_moves(const wchar_t *t);

/* Разыграть партию с начала по обоим файлам (свой — из памяти: только что
   сделанный ход папка могла ещё не вернуть). */
static void sh_replay(const wchar_t *p0, const wchar_t *p1) {
  const wchar_t *mine = g_sh.log.w, *theirs = g_sh.me == 0 ? p1 : p0;
  const wchar_t *file[2];
  file[g_sh.me] = mine;
  file[1 - g_sh.me] = theirs;
  wchar_t v[128];
  if (nd_field(theirs, L"name", v, 128) && v[0]) lstrcpynW(g_sh.oppName, v, 128);
  if (g_sh.me == 0 && nd_has_line(theirs, L"decline")) {
    g_sh.mode = SH_LOBBY;
    if (nd_has_line(theirs, L"swap")) { /* позвали друг друга — играем в его партии */
      lstrcpynW(g_sh.swapFrom, g_sh.oppId, 96);
      g_sh.swapT = GetTickCount64();
      sh_status(L"%s тоже позвал вас — соединяю в его партии…", g_sh.oppName);
    } else {
      sh_status(L"%s отказался от партии", g_sh.oppName);
    }
    sh_save_local();
    return;
  }
  if (g_sh.me == 1 && nd_has_line(theirs, L"cancel")) {
    g_sh.mode = SH_LOBBY;
    sh_status(L"%s отменил приглашение", g_sh.oppName);
    sh_save_local();
    return;
  }
  g_sh.white = sh_white_side(g_sh.game);
  if (!nd_field(file[1], L"id", v, 96) || !v[0]) { /* принявший ещё не ответил */
    g_sh.mode = SH_WAIT;
    if (sh_count_moves(mine) > 0) /* партия уже шла: после перезапуска его запись ещё не пришла */
      sh_status(L"Восстанавливаю партию — жду запись %s (до минуты)…", g_sh.oppName[0] ? g_sh.oppName : g_sh.oppId);
    else
      sh_status(L"Ждём ответа: %s", g_sh.oppName[0] ? g_sh.oppName : g_sh.oppId);
    return;
  }
  if (g_sh.mode == SH_WAIT) g_sh.mode = SH_PLAY;
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
    if (nd_has_line(file[0], L"resign") || nd_has_line(file[1], L"resign")) {
      int loser = nd_has_line(file[0], L"resign") ? 0 : 1;
      g_sh.resigned = TRUE;
      sh_over(1 - loser, NULL);
      return;
    }
    /* ничья по согласию: один предложил на этом полуходе, другой согласился */
    for (int s = 0; s < 2; s++)
      if (sh_has_num(file[s], L"draw", ply) && sh_has_num(file[1 - s], L"drawok", ply)) {
        g_sh.draw = TRUE;
        sh_over(-1, L"Ничья по согласию");
        return;
      }
    int side = sh_side_of_ply(ply), color = sh_color_of_side(side);
    int n = sh_legal(&bd, color, mv, SH_MAXMOVES);
    if (n == 0) { /* ходить нечем — проиграл */
      sh_over(1 - side, sh_count(&bd, color) ? L"Соперника заперли — ходить нечем" : L"Все шашки взяты");
      return;
    }
    char path[80];
    if (!sh_move_line(file[side], ply, path, 80)) {
      g_sh.oppOffersDraw = sh_has_num(theirs, L"draw", ply) && !sh_has_num(mine, L"drawok", ply);
      g_sh.iOffered = sh_has_num(mine, L"draw", ply);
      return; /* этот ход ещё не сделан */
    }
    unsigned char sq[SH_MAXPATH];
    int ns = sh_parse_path(path, sq, SH_MAXPATH);
    int k = ns ? sh_find_move(mv, n, sq, ns) : -1;
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
    memcpy(g_sh.lastPath, mv[k].path, SH_MAXPATH);
    g_sh.nlast = mv[k].npath;
    g_sh.lastOpp = side != g_sh.me;
    if (quiet >= 30) {
      g_sh.board = bd;
      g_sh.ply = ply + 1;
      g_sh.draw = TRUE;
      sh_over(-1, L"15 ходов только дамками без взятий — ничья");
      return;
    }
  }
}

static void sh_refresh_view(void);

static BOOL sh_my_turn(void) {
  return g_sh.mode == SH_PLAY && sh_side_of_ply(g_sh.ply) == g_sh.me;
}

/* соперник в сети — по общему списку нард (отметка «я в сети» раз в минуту) */
static BOOL sh_opp_online(void) {
  NdPoll *pl = g_nd.poll;
  if (!pl) return TRUE; /* ещё не знаем — не пугаем */
  for (int i = 0; i < pl->nOnline; i++)
    if (!wcscmp(pl->onId[i], g_sh.oppId)) return TRUE;
  return FALSE;
}

static void sh_turn_status(void) {
  if (g_sh.mode != SH_PLAY) return;
  if (!sh_my_turn()) {
    if (g_sh.oppOffersDraw) sh_status(L"%s предлагает ничью — «Согласиться на ничью» или ждите его хода", g_sh.oppName);
    else if (!sh_opp_online())
      sh_status(L"Ходит %s — он сейчас не в сети: ход увидит, когда запустит CursorPad", g_sh.oppName);
    else sh_status(L"Ходит %s…%s", g_sh.oppName, g_sh.iOffered ? L" (вы предложили ничью)" : L"");
    return;
  }
  static ShMove mv[SH_MAXMOVES];
  int n = sh_legal(&g_sh.board, sh_my_color(), mv, SH_MAXMOVES);
  BOOL cap = n > 0 && mv[0].ncap > 0;
  if (g_sh.oppOffersDraw) sh_status(L"%s предлагает ничью — «Согласиться на ничью» или просто ходите", g_sh.oppName);
  else if (g_sh.npart > 1) sh_status(L"Бейте дальше — отмечены поля, куда можно");
  else if (g_sh.npart == 1) sh_status(L"Куда? Отмечены поля, куда можно");
  else if (cap) sh_status(L"Ваш ход — бить обязательно: подсвечены шашки, которые бьют");
  else sh_status(L"Ваш ход — выберите шашку");
}

static void sh_after_replay(int prevPly, int prevMode) {
  if (prevMode == SH_WAIT && g_sh.mode == SH_PLAY) sh_save_local(); /* приняли — после перезапуска сразу игра */
  if (g_sh.mode == SH_PLAY) {
    if (g_sh.ply != prevPly || prevMode != SH_PLAY) g_sh.npart = 0;
    sh_turn_status();
    if (sh_my_turn() && g_sh.notifiedPly != g_sh.ply && (!g_shWnd || GetForegroundWindow() != g_shWnd)) {
      g_sh.notifiedPly = g_sh.ply;
      wchar_t t[160];
      if (g_sh.ply <= 2) _snwprintf(t, 160, L"Партия с %s: вы играете %s — ваш ход", g_sh.oppName,
                                   sh_my_color() > 0 ? L"белыми" : L"чёрными");
      else _snwprintf(t, 160, L"%s сходил — ваш ход", g_sh.oppName);
      sh_balloon(t);
    }
  }
  if (g_sh.mode == SH_OVER && prevMode != SH_OVER) {
    wchar_t t[240];
    if (g_sh.draw) lstrcpynW(t, g_sh.endWhy, 240);
    else if (g_sh.winner == g_sh.me)
      _snwprintf(t, 240, L"Вы выиграли у %s%s", g_sh.oppName, g_sh.resigned ? L" — соперник сдался" : L"");
    else if (g_sh.winner >= 0)
      _snwprintf(t, 240, L"Выиграл %s%s", g_sh.oppName, g_sh.resigned ? L" — вы сдались" : L"");
    else lstrcpynW(t, g_sh.endWhy, 240);
    if (!g_sh.draw && !g_sh.resigned && g_sh.winner >= 0 && g_sh.endWhy[0]) {
      size_t l = wcslen(t);
      _snwprintf(t + l, 240 - l, L" · %s", g_sh.endWhy);
      t[239] = 0;
    }
    sh_status(L"%s", t);
    if (!g_shWnd || GetForegroundWindow() != g_shWnd) sh_balloon(t);
  }
  sh_refresh_view();
}

static void sh_answer_ex(int idx, BOOL accept, BOOL ask);

static void sh_on_poll(ShPoll *pl) {
  sh_poll_free(g_sh.poll);
  g_sh.poll = pl;
  /* уже отвеченные — вон из списка */
  for (int i = 0; i < pl->nInv;) {
    BOOL done = FALSE;
    for (int k = 0; k < 8 && k < g_sh.nDoneInv && !done; k++) done = !wcscmp(g_sh.doneInv[k], pl->invGame[i]);
    if (!done) {
      i++;
      continue;
    }
    for (int k = i; k + 1 < pl->nInv; k++) {
      lstrcpynW(pl->invGame[k], pl->invGame[k + 1], 96);
      lstrcpynW(pl->invFrom[k], pl->invFrom[k + 1], 96);
      lstrcpynW(pl->invName[k], pl->invName[k + 1], 128);
    }
    pl->nInv--;
  }
  /* Позвали друг друга одновременно: я жду ответа от X, а от X пришло его
     приглашение. Обе программы решают одинаково — остаётся партия с
     меньшим номером: её приглашённый принимает сам, лишнюю отменяют. */
  if (g_sh.mode == SH_LOBBY && g_sh.swapFrom[0]) {
    if (GetTickCount64() - g_sh.swapT > 120000) g_sh.swapFrom[0] = 0; /* две минуты — не пришло */
    else
      for (int i = 0; i < pl->nInv; i++)
        if (!wcscmp(pl->invFrom[i], g_sh.swapFrom)) {
          g_sh.swapFrom[0] = 0;
          lstrcpynW(g_sh.seenInv[g_sh.nSeenInv % 16], pl->invGame[i], 96);
          g_sh.nSeenInv++;
          sh_answer_ex(i, TRUE, FALSE);
          return;
        }
  }
  if (g_sh.mode == SH_WAIT)
    for (int i = 0; i < pl->nInv; i++) {
      if (wcscmp(pl->invFrom[i], g_sh.oppId) || !wcscmp(pl->invGame[i], g_sh.game)) continue;
      lstrcpynW(g_sh.seenInv[g_sh.nSeenInv % 16], pl->invGame[i], 96); /* без всплывашки */
      g_sh.nSeenInv++;
      sh_answer_ex(i, wcscmp(pl->invGame[i], g_sh.game) < 0, FALSE);
      return;
    }
  for (int i = 0; i < pl->nInv; i++) {
    BOOL seen = FALSE;
    for (int k = 0; k < g_sh.nSeenInv && k < 16 && !seen; k++) seen = !wcscmp(g_sh.seenInv[k], pl->invGame[i]);
    if (seen) continue;
    lstrcpynW(g_sh.seenInv[g_sh.nSeenInv % 16], pl->invGame[i], 96);
    g_sh.nSeenInv++;
    wchar_t t[200];
    _snwprintf(t, 200, L"%s зовёт сыграть в шашки", pl->invName[i]);
    sh_balloon(t);
  }
  BOOL thisGame = g_sh.game[0] && !wcscmp(pl->game, g_sh.game);
  const wchar_t *mineNow = g_sh.me == 0 ? pl->p0 : pl->p1;
  if (thisGame && g_sh.mode != SH_LOBBY && !g_sh.logLoaded) {
    if (!mineNow) {
      sh_refresh_view();
      return;
    }
    sh_log_reset();
    sb_add(&g_sh.log, L"%s", mineNow);
    g_sh.logLoaded = TRUE;
  }
  /* в папке своя запись длиннее той, что с компьютера (та не сохранилась) — её */
  if (thisGame && g_sh.mode != SH_LOBBY && g_sh.logLoaded && g_sh.log.w && mineNow && !pl->partial[g_sh.me] &&
      wcslen(mineNow) > g_sh.log.len && !wcsncmp(mineNow, g_sh.log.w, g_sh.log.len)) {
    sh_log_reset();
    sb_add(&g_sh.log, L"%s", mineNow);
  }
  /* ждём соперника дольше минуты — повторить ему свою копию: вдруг он её
     потерял (забрал и перезапустился) и ждёт нашего хода, а мы — его */
  if (thisGame && (g_sh.mode == SH_WAIT || (g_sh.mode == SH_PLAY && !sh_my_turn())) && g_sh.mvAt &&
      ft_now() - g_sh.mvAt > 60ull * 10000000ull)
    g_sh.mvHash = 0;
  if (thisGame && g_sh.mode != SH_LOBBY && g_sh.logLoaded && g_sh.log.w && g_sh.log.w[0] &&
      (!mineNow || pl->partial[g_sh.me] || wcscmp(mineNow, g_sh.log.w)))
    sh_write_mine(); /* в папке не то, что у нас — пишем снова (и копию) */
  else if (thisGame && g_sh.mode != SH_LOBBY && g_sh.logLoaded && g_sh.log.w && g_sh.log.w[0])
    sh_send_copy(g_sh.game, g_sh.me, g_sh.oppId, g_sh.log.w, TRUE); /* копия не ушла — ещё раз */
  if ((g_sh.mode == SH_WAIT || g_sh.mode == SH_PLAY) && thisGame) {
    int prevPly = g_sh.ply, prevMode = g_sh.mode;
    sh_replay(pl->p0, pl->p1);
    sh_after_replay(prevPly, prevMode);
    return;
  }
  sh_refresh_view();
}

/* ---- действия игрока -------------------------------------------------------- */

static void sh_invite(const wchar_t *oppId, const wchar_t *oppName) {
  if (g_sh.mode != SH_LOBBY) return; /* уже позвали — второй раз (двойной щелчок) не зовём */
  wchar_t myId[96], myName[128];
  nd_myid(myId, 96, myName, 128);
  _snwprintf(g_sh.game, 96, L"%s-%llx", myId, GetTickCount64());
  g_sh.game[95] = 0;
  g_sh.me = 0;
  g_sh.white = sh_white_side(g_sh.game);
  lstrcpynW(g_sh.oppId, oppId, 96);
  lstrcpynW(g_sh.oppName, oppName, 128);
  wchar_t *dot = wcsstr(g_sh.oppName, L"  ·");
  if (dot) *dot = 0;
  sh_log_reset();
  sb_add(&g_sh.log, L"name\t%s\nid\t%s\n", myName, myId);
  g_sh.logLoaded = TRUE;
  sh_start(&g_sh.board);
  g_sh.ply = 1;
  g_sh.npart = 0;
  g_sh.nlast = 0;
  g_sh.draw = g_sh.resigned = FALSE;
  if (!sh_write_mine()) {
    sh_status(L"Не удалось записать в общую папку");
    sh_refresh_view();
    return;
  }
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH], txt[400];
  share_root_copy(root);
  _snwprintf(sub, 160, L"inv\\%s", oppId);
  if (sh_dir(root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, g_sh.game);
    _snwprintf(txt, 400, L"from\t%s\nname\t%s\ngame\t%s\n", myId, myName, g_sh.game);
    share_write(path, txt);
  }
  g_sh.mode = SH_WAIT;
  sh_status(L"Ждём ответа: %s", g_sh.oppName);
  nd_lock();
  lstrcpynW(g_shWatch, g_sh.game, 96);
  nd_unlock();
  sh_save_local();
  sh_refresh_view();
}

static void sh_drop_invite(const wchar_t *game) {
  wchar_t root[MAX_PATH], myId[96], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  nd_myid(myId, 96, NULL, 0);
  _snwprintf(sub, 160, L"inv\\%s", myId);
  if (!sh_dir(root, sub, dir)) return;
  _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, game);
  DeleteFileW(path);
}

static void sh_cancel_mine_quiet(void);

/* ask — спрашивать ли, если идёт другая партия (при взаимных приглашениях
   решает сама программа и не спрашивает) */
static void sh_answer_ex(int idx, BOOL accept, BOOL ask) {
  ShPoll *pl = g_sh.poll;
  if (!pl || idx < 0 || idx >= pl->nInv) return;
  wchar_t game[96], from[96], name[128], myId[96], myName[128];
  lstrcpynW(game, pl->invGame[idx], 96);
  lstrcpynW(g_sh.doneInv[g_sh.nDoneInv % 8], game, 96);
  g_sh.nDoneInv++;
  lstrcpynW(from, pl->invFrom[idx], 96);
  lstrcpynW(name, pl->invName[idx], 128);
  nd_myid(myId, 96, myName, 128);
  sh_drop_invite(game);
  if (!accept) {
    wchar_t path[SHARE_PATH];
    sh_game_path(game, 1, path);
    /* отказ из-за встречного приглашения — с пометкой: тот примет наше сам */
    const wchar_t *dt = ask ? L"decline\n" : L"decline\nswap\n";
    if (path[0]) share_write(path, dt);
    sh_send_copy(game, 1, from, dt, FALSE); /* и копией — как ходы */
    sh_refresh_view();
    return;
  }
  if (g_sh.mode == SH_PLAY || g_sh.mode == SH_WAIT) {
    if (ask && g_sh.mode == SH_PLAY &&
        MessageBoxW(g_shWnd, L"Идёт другая партия. Сдать её и начать новую?", L"Шашки",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
      return;
    if (g_sh.mode == SH_PLAY) {
      sb_add(&g_sh.log, L"resign\n");
      sh_write_mine();
    } else {
      /* ждали ответа на своё приглашение — отменить его, иначе тот, кого
         звали, мог принять и ждать наших ходов в брошенной партии (так
         партия и вставала, когда двое звали друг друга одновременно) */
      sh_cancel_mine_quiet();
    }
  }
  lstrcpynW(g_sh.game, game, 96);
  g_sh.me = 1;
  g_sh.white = sh_white_side(game);
  lstrcpynW(g_sh.oppId, from, 96);
  lstrcpynW(g_sh.oppName, name, 128);
  sh_log_reset();
  sb_add(&g_sh.log, L"name\t%s\nid\t%s\n", myName, myId);
  g_sh.logLoaded = TRUE;
  sh_start(&g_sh.board);
  g_sh.ply = 1;
  g_sh.npart = 0;
  g_sh.nlast = 0;
  g_sh.draw = g_sh.resigned = FALSE;
  sh_write_mine();
  g_sh.mode = SH_PLAY;
  nd_lock();
  lstrcpynW(g_shWatch, g_sh.game, 96);
  nd_unlock();
  sh_save_local();
  sh_turn_status();
  sh_refresh_view();
}

/* мой ход сделан — в файл и разыграть заново */
static void sh_commit(const ShMove *m) {
  char t[80];
  sh_move_text(m, t, 80);
  wchar_t w[80];
  MultiByteToWideChar(CP_UTF8, 0, t, -1, w, 80);
  sb_add(&g_sh.log, L"m\t%d\t%s\n", g_sh.ply, w);
  g_sh.npart = 0;
  if (!sh_write_mine()) sh_status(L"Не удалось записать ход в общую папку — повторю сам");
  int prevPly = g_sh.ply, prevMode = g_sh.mode;
  ShPoll *pl = g_sh.poll;
  sh_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  sh_after_replay(prevPly, prevMode);
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
    BOOL next = FALSE;
    for (int i = 0; i < nc; i++)
      if (cand[i].npath > g_sh.npart && cand[i].path[g_sh.npart] == sq) next = TRUE;
    if (next) {
      g_sh.part[g_sh.npart++] = (unsigned char)sq;
      nc = sh_candidates(mv, n, cand);
      for (int i = 0; i < nc; i++)
        if (cand[i].npath == g_sh.npart) { /* путь кончился — ход сделан */
          sh_commit(&cand[i]);
          return;
        }
    } else if (g_sh.npart == 1) {
      /* другую шашку (или ту же — снять выбор) */
      if (isStart && sq != g_sh.part[0]) g_sh.part[0] = (unsigned char)sq;
      else g_sh.npart = 0;
    }
    /* посреди взятия щелчок мимо — ничего: бой надо довести */
  }
  sh_turn_status();
  sh_refresh_view();
}

static void sh_resign(void) {
  if (g_sh.mode != SH_PLAY) return;
  if (MessageBoxW(g_shWnd, L"Сдать партию?", L"Шашки", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  sb_add(&g_sh.log, L"resign\n");
  sh_write_mine();
  int prevPly = g_sh.ply, prevMode = g_sh.mode;
  ShPoll *pl = g_sh.poll;
  sh_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  sh_after_replay(prevPly, prevMode);
}

static void sh_offer_draw(void) {
  if (g_sh.mode != SH_PLAY || g_sh.iOffered) return;
  sb_add(&g_sh.log, L"draw\t%d\n", g_sh.ply);
  sh_write_mine();
  g_sh.iOffered = TRUE;
  sh_status(L"Ничья предложена — ждём ответа %s", g_sh.oppName);
  sh_refresh_view();
}

static void sh_accept_draw(void) {
  if (g_sh.mode != SH_PLAY || !g_sh.oppOffersDraw) return;
  sb_add(&g_sh.log, L"drawok\t%d\n", g_sh.ply);
  sh_write_mine();
  int prevPly = g_sh.ply, prevMode = g_sh.mode;
  ShPoll *pl = g_sh.poll;
  sh_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  sh_after_replay(prevPly, prevMode);
}

static void sh_answer(int idx, BOOL accept) { sh_answer_ex(idx, accept, TRUE); }

/* своё приглашение — отменить без слов (перед тем как принять чужое) */
static void sh_cancel_mine_quiet(void) {
  if (g_sh.mode != SH_WAIT || !g_sh.game[0]) return;
  sb_add(&g_sh.log, L"cancel\n");
  sh_write_mine();
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 160, L"inv\\%s", g_sh.oppId);
  if (sh_dir(root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, g_sh.game);
    DeleteFileW(path);
  }
}

static void sh_cancel_wait(void) {
  if (g_sh.mode != SH_WAIT) return;
  sb_add(&g_sh.log, L"cancel\n");
  sh_write_mine();
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 160, L"inv\\%s", g_sh.oppId);
  if (sh_dir(root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, g_sh.game);
    DeleteFileW(path);
  }
  g_sh.mode = SH_LOBBY;
  sh_status(L"Приглашение отменено");
  sh_save_local();
  sh_refresh_view();
}

static void sh_back_to_lobby(void) {
  g_sh.mode = SH_LOBBY;
  g_sh.game[0] = 0;
  nd_lock();
  g_shWatch[0] = 0;
  nd_unlock();
  sh_log_reset();
  sh_status(L"");
  sh_save_local();
  sh_refresh_view();
}

/* ---- доска ------------------------------------------------------------------ */

#define SH_SQ 50   /* поле, логических точек */
#define SH_TOP 46  /* под шапкой окна */
#define SH_LEFT 14
#define SH_FRAME 20 /* рамка с буквами и цифрами */

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

static int sh_hit(int x, int y) {
  int q = SS_(SH_SQ), bx = sh_bx(), by = sh_by();
  if (x < bx || y < by || x >= bx + 8 * q || y >= by + 8 * q) return -1;
  int sc = (x - bx) / q, sr = (y - by) / q;
  BOOL flip = sh_my_color() < 0;
  int c = flip ? 7 - sc : sc, r = flip ? sr : 7 - sr;
  if (!sh_dark(r, c)) return -1;
  return r * 8 + c;
}

static void sh_make_pieces(void) {
  int D = SS_(SH_SQ) - SS_(8);
  if (g_shPc[0] && g_shPcD == D) return;
  for (int k = 0; k < 2; k++)
    if (g_shPc[k]) DeleteObject(g_shPc[k]);
  g_shPcD = D;
  for (int k = 0; k < 2; k++) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = D;
    bi.bmiHeader.biHeight = -D;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    g_shPc[k] = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bits) continue;
    float c = D / 2.0f, R = D / 2.0f - 0.5f;
    float base[2][3] = {{248, 240, 222}, {52, 30, 28}};
    float edge[2][3] = {{130, 104, 76}, {16, 8, 8}};
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
            float lit = 1.0f - 0.35f * ((px + py) / (2 * R) + 0.5f);
            float rr = base[k][0] * lit, gg = base[k][1] * lit, bb = base[k][2] * lit;
            if (t > 0.88f) { rr = edge[k][0]; gg = edge[k][1]; bb = edge[k][2]; }
            else if ((t > 0.55f && t < 0.62f) || (t > 0.30f && t < 0.35f)) { rr *= 0.84f; gg *= 0.84f; bb *= 0.84f; }
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
        p[0] = (BYTE)(sb / cov * a);
        p[1] = (BYTE)(sg / cov * a);
        p[2] = (BYTE)(sr / cov * a);
        p[3] = (BYTE)(255 * a);
      }
    }
  }
}

static void sh_draw_piece(HDC hdc, HDC mem, int v, int cx, int cy, BYTE alpha) {
  int k = v > 0 ? 0 : 1;
  if (!g_shPc[k]) return;
  HGDIOBJ o = SelectObject(mem, g_shPc[k]);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
  GdiAlphaBlend(hdc, cx - g_shPcD / 2, cy - g_shPcD / 2, g_shPcD, g_shPcD, mem, 0, 0, g_shPcD, g_shPcD, bf);
  SelectObject(mem, o);
  if (sh_isking(v)) { /* дамка: золотая корона-кольцо */
    int r = g_shPcD / 4;
    HBRUSH gb = CreateSolidBrush(RGB(222, 176, 60));
    HPEN gp = CreatePen(PS_SOLID, SS_(2), RGB(120, 84, 20));
    HGDIOBJ ob = SelectObject(hdc, gb), op = SelectObject(hdc, gp);
    Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(gb);
    DeleteObject(gp);
    HBRUSH db = CreateSolidBrush(RGB(120, 84, 20));
    ob = SelectObject(hdc, db);
    op = SelectObject(hdc, GetStockObject(NULL_PEN));
    int r2 = r / 3 + 1;
    Ellipse(hdc, cx - r2, cy - r2, cx + r2 + 1, cy + r2 + 1);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(db);
  }
}

static void sh_frame_sq(HDC hdc, int s, COLORREF c, int w) {
  int x, y, q = SS_(SH_SQ);
  sh_sq_xy(s, &x, &y);
  HPEN pn = CreatePen(PS_SOLID, w, c);
  HGDIOBJ op = SelectObject(hdc, pn), ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, x + w / 2, y + w / 2, x + q - w / 2 + 1, y + q - w / 2 + 1);
  SelectObject(hdc, op);
  SelectObject(hdc, ob);
  DeleteObject(pn);
}

static void sh_dot(HDC hdc, int s, COLORREF c, int r) {
  int x, y, q = SS_(SH_SQ);
  sh_sq_xy(s, &x, &y);
  HBRUSH b = CreateSolidBrush(c);
  HGDIOBJ ob = SelectObject(hdc, b), op = SelectObject(hdc, GetStockObject(NULL_PEN));
  Ellipse(hdc, x + q / 2 - r, y + q / 2 - r, x + q / 2 + r + 1, y + q / 2 + r + 1);
  SelectObject(hdc, ob);
  SelectObject(hdc, op);
  DeleteObject(b);
}

static void sh_paint_board(HDC hdc) {
  sh_make_pieces();
  int q = SS_(SH_SQ), bx = sh_bx(), by = sh_by(), bp = sh_board_px(), fr = SS_(SH_FRAME);
  RECT frame = {bx - fr, by - fr, bx + bp + fr, by + bp + fr};
  fill_round_rect(hdc, frame, RGB(104, 64, 34), RGB(60, 36, 18), SS_(8));
  for (int sr = 0; sr < 8; sr++)
    for (int sc = 0; sc < 8; sc++) {
      RECT r = {bx + sc * q, by + sr * q, bx + (sc + 1) * q, by + (sr + 1) * q};
      BOOL dark = ((sr + sc) & 1) == 1; /* a1 — тёмное снизу слева при любом повороте */
      HBRUSH b = CreateSolidBrush(dark ? RGB(122, 78, 46) : RGB(238, 222, 190));
      FillRect(hdc, &r, b);
      DeleteObject(b);
    }
  /* буквы и цифры по краям */
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(238, 222, 190));
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  BOOL flip = sh_my_color() < 0;
  for (int i = 0; i < 8; i++) {
    wchar_t l[2] = {(wchar_t)(L'a' + (flip ? 7 - i : i)), 0}, d[2] = {(wchar_t)(L'1' + (flip ? i : 7 - i)), 0};
    RECT rl = {bx + i * q, by + bp, bx + (i + 1) * q, by + bp + fr};
    DrawTextW(hdc, l, -1, &rl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT rd = {bx - fr, by + i * q, bx, by + (i + 1) * q};
    DrawTextW(hdc, d, -1, &rd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  /* последний ход соперника: откуда и куда */
  if (g_sh.nlast >= 2 && g_sh.lastOpp && g_sh.npart == 0) {
    sh_frame_sq(hdc, g_sh.lastPath[0], RGB(70, 140, 230), SS_(3));
    sh_frame_sq(hdc, g_sh.lastPath[g_sh.nlast - 1], RGB(70, 140, 230), SS_(3));
  }
  /* доска с учётом пройденной части моего пути: шашка — в конце пути,
     уже взятые — полупрозрачны */
  ShBoard b = g_sh.board;
  BOOL taken[64];
  memset(taken, 0, sizeof(taken));
  int movedTo = -1;
  if (g_sh.npart > 1) {
    int v = b.sq[g_sh.part[0]];
    for (int i = 1; i < g_sh.npart; i++) {
      int a = g_sh.part[i - 1], z = g_sh.part[i];
      int dr = sh_row(z) > sh_row(a) ? 1 : -1, dc = sh_col(z) > sh_col(a) ? 1 : -1;
      for (int r = sh_row(a) + dr, c = sh_col(a) + dc; r != sh_row(z); r += dr, c += dc)
        if (b.sq[r * 8 + c]) taken[r * 8 + c] = TRUE;
    }
    b.sq[g_sh.part[0]] = 0;
    movedTo = g_sh.part[g_sh.npart - 1];
    b.sq[movedTo] = (signed char)v;
  }
  HDC mem = CreateCompatibleDC(hdc);
  for (int s = 0; s < 64; s++) {
    if (!b.sq[s]) continue;
    int x, y;
    sh_sq_xy(s, &x, &y);
    sh_draw_piece(hdc, mem, b.sq[s], x + q / 2, y + q / 2, taken[s] ? 90 : 255);
  }
  DeleteDC(mem);
  /* подсветки моего хода */
  if (sh_my_turn()) {
    static ShMove mv[SH_MAXMOVES], cand[SH_MAXMOVES];
    int n = sh_legal(&g_sh.board, sh_my_color(), mv, SH_MAXMOVES);
    if (g_sh.npart == 0) {
      for (int i = 0; i < n; i++) sh_frame_sq(hdc, mv[i].path[0], RGB(230, 200, 110), SS_(2));
    } else {
      sh_frame_sq(hdc, g_sh.npart > 1 ? movedTo : g_sh.part[0], RGB(255, 190, 0), SS_(3));
      int nc = sh_candidates(mv, n, cand);
      for (int i = 0; i < nc; i++)
        if (cand[i].npath > g_sh.npart) sh_dot(hdc, cand[i].path[g_sh.npart], RGB(60, 170, 90), q / 7);
    }
  }
}

/* ---- окно ------------------------------------------------------------------- */

static void sh_layout(void) {
  if (!g_shWnd) return;
  RECT rc;
  GetClientRect(g_shWnd, &rc);
  place_panel_close(g_shWnd);
  int m = g_sh.mode, pad = SS_(14), bh = SS_(28), gap = SS_(8);
  int by = SS_(SH_TOP) + sh_board_px() + SS_(SH_FRAME) * 2 + SS_(50); /* под строкой хода и счётом */
  int ids[9] = {ID_SH_INVITE, ID_SH_ACCEPT, ID_SH_DECLINE, ID_SH_DRAW, ID_SH_DRAWOK,
                ID_SH_RESIGN, ID_SH_BACK,   ID_SH_CANCEL,  ID_SH_LIST};
  HWND b[9];
  for (int i = 0; i < 9; i++) b[i] = GetDlgItem(g_shWnd, ids[i]);
  BOOL lobby = m == SH_LOBBY;
  NdPoll *on = g_nd.poll;
  ShPoll *pl = g_sh.poll;
  BOOL inv = lobby && pl && pl->nInv > 0;
  ShowWindow(b[8], lobby ? SW_SHOW : SW_HIDE);
  if (lobby) MoveWindow(b[8], pad, SS_(SH_TOP) + SS_(24), rc.right - pad * 2, SS_(200), TRUE);
  ShowWindow(b[0], lobby ? SW_SHOW : SW_HIDE);
  if (lobby) {
    MoveWindow(b[0], pad, SS_(SH_TOP) + SS_(232), SS_(220), bh, TRUE);
    EnableWindow(b[0], on && on->nOnline > 0);
  }
  ShowWindow(b[1], inv ? SW_SHOW : SW_HIDE);
  ShowWindow(b[2], inv ? SW_SHOW : SW_HIDE);
  if (inv) {
    MoveWindow(b[1], pad, SS_(SH_TOP) + SS_(300), SS_(120), bh, TRUE);
    MoveWindow(b[2], pad + SS_(120) + gap, SS_(SH_TOP) + SS_(300), SS_(120), bh, TRUE);
  }
  BOOL play = m == SH_PLAY, over = m == SH_OVER, wait = m == SH_WAIT;
  ShowWindow(b[3], play && !g_sh.iOffered && !g_sh.oppOffersDraw ? SW_SHOW : SW_HIDE);
  ShowWindow(b[4], play && g_sh.oppOffersDraw ? SW_SHOW : SW_HIDE);
  ShowWindow(b[5], play ? SW_SHOW : SW_HIDE);
  ShowWindow(b[6], over ? SW_SHOW : SW_HIDE);
  ShowWindow(b[7], wait ? SW_SHOW : SW_HIDE);
  if (play) {
    MoveWindow(b[3], pad, by, SS_(170), bh, TRUE);
    MoveWindow(b[4], pad, by, SS_(200), bh, TRUE);
    MoveWindow(b[5], rc.right - pad - SS_(100), by, SS_(100), bh, TRUE);
  }
  if (over) MoveWindow(b[6], pad, by, SS_(160), bh, TRUE);
  if (wait) MoveWindow(b[7], pad, SS_(SH_TOP) + SS_(60), SS_(170), bh, TRUE);
}

/* кнопки «Шашки»: точка, когда вас ждут — ваш ход или зовут */
static void sh_mark_btn(void) {
  if (!g_btnSh) return;
  BOOL wait = sh_my_turn() || (g_sh.mode != SH_PLAY && g_sh.mode != SH_WAIT && g_sh.poll && g_sh.poll->nInv > 0);
  const wchar_t *want = wait ? L"● Шашки" : L"Шашки";
  wchar_t cur[32];
  GetWindowTextW(g_btnSh, cur, 32);
  if (wcscmp(cur, want)) SetWindowTextW(g_btnSh, want);
}

static void sh_refresh_view(void) {
  sh_mark_btn();
  if (!g_shWnd) return;
  if (g_shList && g_sh.mode == SH_LOBBY) {
    NdPoll *pl = g_nd.poll; /* кто в сети — общий список с нардами */
    int n = pl ? pl->nOnline : 0;
    /* тот же список — не трогаем: пересборка посреди щелчка сбрасывала выбор */
    BOOL same = n == g_shListN && (int)SendMessageW(g_shList, LB_GETCOUNT, 0, 0) == n;
    for (int i = 0; i < n && same; i++)
      same = !wcscmp(g_shListId[i], pl->onId[i]) && !wcscmp(g_shListName[i], pl->onName[i]);
    if (!same) {
      int sel = (int)SendMessageW(g_shList, LB_GETCURSEL, 0, 0);
      wchar_t selId[96] = L"";
      if (sel >= 0 && sel < g_shListN) lstrcpynW(selId, g_shListId[sel], 96);
      SendMessageW(g_shList, WM_SETREDRAW, FALSE, 0);
      SendMessageW(g_shList, LB_RESETCONTENT, 0, 0);
      int keep = -1;
      g_shListN = 0;
      for (int i = 0; i < n; i++) {
        SendMessageW(g_shList, LB_ADDSTRING, 0, (LPARAM)pl->onName[i]);
        lstrcpynW(g_shListId[g_shListN], pl->onId[i], 96);
        lstrcpynW(g_shListName[g_shListN], pl->onName[i], 128);
        g_shListN++;
        if (selId[0] && !wcscmp(selId, pl->onId[i])) keep = i;
      }
      if (keep < 0 && n > 0) keep = 0;
      if (keep >= 0) SendMessageW(g_shList, LB_SETCURSEL, keep, 0);
      SendMessageW(g_shList, WM_SETREDRAW, TRUE, 0);
      InvalidateRect(g_shList, NULL, TRUE);
    }
  }
  sh_layout();
  InvalidateRect(g_shWnd, NULL, FALSE);
}

/* список «кто в сети» у нард обновился — и у шашек */
static void shashki_online_changed(void) {
  if (g_sh.mode == SH_PLAY) sh_turn_status(); /* соперник пропал из сети или вернулся */
  if (g_shWnd && IsWindowVisible(g_shWnd) && (g_sh.mode == SH_LOBBY || g_sh.mode == SH_PLAY)) sh_refresh_view();
  else sh_mark_btn();
}

/* сколько ходов («m\t…») в файле */
static int sh_count_moves(const wchar_t *t) {
  int n = 0;
  for (const wchar_t *p = t; p && *p;) {
    if (p[0] == L'm' && p[1] == L'\t') n++;
    p = wcschr(p, L'\n');
    if (p) p++;
  }
  return n;
}

static void sh_hms(ULONGLONG ft, wchar_t *out, int cap) {
  if (!ft) {
    lstrcpynW(out, L"—", cap);
    return;
  }
  FILETIME f, lf;
  f.dwLowDateTime = (DWORD)ft;
  f.dwHighDateTime = (DWORD)(ft >> 32);
  SYSTEMTIME st;
  FileTimeToLocalFileTime(&f, &lf);
  FileTimeToSystemTime(&lf, &st);
  _snwprintf(out, cap, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
  out[cap - 1] = 0;
}

/* «связь»: что у меня записано и что видно в файле соперника — по этой
   строке видно, где застряло, если ход не доходит (с 2026.09.23.69) */
static void sh_diag_text(wchar_t *out, int cap) {
  ShPoll *pl = g_sh.poll;
  int opp = 1 - g_sh.me;
  BOOL same = pl && !wcscmp(pl->game, g_sh.game);
  const wchar_t *theirs = same ? (opp == 0 ? pl->p0 : pl->p1) : NULL;
  wchar_t w1[16], w2[16], w3[16], w4[16], w5[16], nm[128], err[60] = L"", cp[80];
  sh_hms(g_sh.wroteAt, w1, 16);
  sh_hms(pl ? pl->mt[opp] : 0, w2, 16);
  sh_hms(pl ? pl->readAt : 0, w3, 16);
  sh_hms(g_sh.mvAt, w4, 16);
  sh_hms(pl ? pl->mvInAt : 0, w5, 16);
  lstrcpynW(nm, g_sh.oppName[0] ? g_sh.oppName : L"соперник", 128);
  if (pl && pl->rerr[opp] == ERROR_FILE_NOT_FOUND) lstrcpynW(err, L" (файла нет)", 60);
  else if (pl && pl->rerr[opp]) _snwprintf(err, 60, L" (не читается, ошибка %lu)", pl->rerr[opp]);
  /* моя копия в его папке: забрал ли */
  if (g_sh.mvFail) lstrcpynW(cp, L"копия НЕ ЗАПИСАЛАСЬ", 80);
  else if (!g_sh.mvAt) lstrcpynW(cp, L"копии нет", 80);
  else _snwprintf(cp, 80, L"копия %s%s", w4,
                  !pl || pl->mvTaken < 0 ? L"" : pl->mvTaken ? L" — забрал" : L" — ещё не забрал");
  _snwprintf(out, cap,
             L"связь: у вас ходов %d · файл %s · %s  |  у %s ходов %d%s · файл %s%s · копий от него %d "
             L"(последняя %s) · проверено %s",
             sh_count_moves(g_sh.log.w), g_sh.fileFail ? L"НЕ ЗАПИСАЛСЯ" : w1, cp, nm, sh_count_moves(theirs),
             same && pl->viaMv[opp] ? L" (по копии)" : L"", w2, err, pl ? pl->mvIn : 0, w5, w3);
  out[cap - 1] = 0;
}

static void sh_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Шашки");
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  int pad = SS_(14);
  if (g_sh.mode == SH_LOBBY) {
    wchar_t root[MAX_PATH];
    share_root_copy(root);
    RECT t = {pad, SS_(SH_TOP), rc.right - pad, SS_(SH_TOP) + SS_(22)};
    NdPoll *on = g_nd.poll;
    ShPoll *pl = g_sh.poll;
    if (!root[0])
      DrawTextW(hdc, L"Задайте общую папку в Настройках («общая папка — PLM для коллег»)", -1, &t,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    else
      DrawTextW(hdc, on && on->nOnline ? L"Кто в сети: выберите коллегу и нажмите «Позвать играть» ниже"
                                        : L"Из коллег сейчас никого в сети",
                -1, &t, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (pl && pl->nInv) {
      wchar_t s[200];
      _snwprintf(s, 200, L"%s зовёт сыграть в шашки", pl->invName[0]);
      RECT r2 = {pad, SS_(SH_TOP) + SS_(272), rc.right - pad, SS_(SH_TOP) + SS_(296)};
      SetTextColor(hdc, COL_SAGE);
      DrawTextW(hdc, s, -1, &r2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    RECT r4 = {pad, SS_(SH_TOP) + SS_(340), rc.right - pad, SS_(SH_TOP) + SS_(420)};
    SetTextColor(hdc, COL_MUTED);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    DrawTextW(hdc,
              L"Русские шашки: бить обязательно, простая бьёт и назад, дамка ходит и бьёт на любое "
              L"расстояние. Кто белые — выпадает случайно. Ход уходит сразу, как шашка дошла до места.",
              -1, &r4, DT_LEFT | DT_WORDBREAK);
    if (g_sh.status[0]) {
      if (g_fontUi) SelectObject(hdc, g_fontUi);
      RECT r3 = {pad, rc.bottom - SS_(34), rc.right - pad, rc.bottom - SS_(10)};
      DrawTextW(hdc, g_sh.status, -1, &r3, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    return;
  }
  if (g_sh.mode == SH_WAIT) {
    RECT t = {pad, SS_(SH_TOP), rc.right - pad, SS_(SH_TOP) + SS_(40)};
    DrawTextW(hdc, g_sh.status, -1, &t, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    wchar_t dg[400];
    sh_diag_text(dg, 400);
    RECT dr = {pad, SS_(SH_TOP) + SS_(100), rc.right - pad, SS_(SH_TOP) + SS_(170)};
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, COL_MUTED);
    DrawTextW(hdc, dg, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    return;
  }
  sh_paint_board(hdc);
  int yb = SS_(SH_TOP) + sh_board_px() + SS_(SH_FRAME) * 2;
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  RECT st = {pad, yb + SS_(4), rc.right - pad, yb + SS_(26)};
  SetTextColor(hdc, g_sh.mode == SH_OVER ? COL_SAGE : COL_INK);
  DrawTextW(hdc, g_sh.status, -1, &st, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  /* кто чем играет и сколько шашек */
  wchar_t info[200];
  int mc = sh_my_color();
  _snwprintf(info, 200, L"вы — %s: %d · %s — %s: %d", mc > 0 ? L"белые" : L"чёрные", sh_count(&g_sh.board, mc),
             g_sh.oppName, mc > 0 ? L"чёрные" : L"белые", sh_count(&g_sh.board, -mc));
  RECT ir = {pad, yb + SS_(26), rc.right - pad, yb + SS_(44)};
  SetTextColor(hdc, COL_MUTED);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  DrawTextW(hdc, info, -1, &ir, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  wchar_t dg[400];
  sh_diag_text(dg, 400);
  RECT dr = {pad, yb + SS_(82), rc.right - pad, yb + SS_(132)};
  SetTextColor(hdc, g_sh.writeFail ? RGB(200, 60, 40) : COL_MUTED);
  DrawTextW(hdc, dg, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
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
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_LBUTTONDOWN:
    if (g_sh.mode == SH_PLAY) sh_click_square(sh_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
    return 0;
  case WM_RBUTTONDOWN:
    /* правая — снять выбор (посреди взятия — начать ход заново) */
    g_sh.npart = 0;
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
    /* от списка — только двойной щелчок; фокус у него не отнимаем: иначе
       Windows не давала выбрать в нём другого игрока */
    if (id == ID_SH_LIST && HIWORD(wParam) != LBN_DBLCLK) return 0;
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (id == ID_SH_LIST) id = ID_SH_INVITE;
    if (id == ID_SH_INVITE) {
      int sel = (int)SendMessageW(g_shList, LB_GETCURSEL, 0, 0);
      if (sel < 0 || sel >= g_shListN) {
        sh_status(L"Выберите, кого позвать");
        sh_refresh_view();
      } else {
        sh_invite(g_shListId[sel], g_shListName[sel]);
      }
    }
    if (id == ID_SH_ACCEPT) sh_answer(0, TRUE);
    if (id == ID_SH_DECLINE) sh_answer(0, FALSE);
    if (id == ID_SH_DRAW) sh_offer_draw();
    if (id == ID_SH_DRAWOK) sh_accept_draw();
    if (id == ID_SH_RESIGN) sh_resign();
    if (id == ID_SH_BACK) sh_back_to_lobby();
    if (id == ID_SH_CANCEL) sh_cancel_wait();
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
  if (g_shThread) return;
  g_shThread = TRUE;
  sh_load_local();
  HANDLE th = CreateThread(NULL, 0, sh_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
}

static void shashki_show(void) {
  if (!g_shWnd) {
    HDC s = GetDC(NULL);
    g_shS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
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
    g_shList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0,
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
    if (g_fontBody) SendMessageW(g_shList, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_shWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                 SWP_NOZORDER);
  }
  if (g_sh.mode == SH_PLAY) sh_turn_status();
  sh_refresh_view();
  ShowWindow(g_shWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_shWnd);
  SetFocus(g_shWnd);
}
