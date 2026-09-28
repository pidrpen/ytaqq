/* ---- Короткие нарды по сети через общую папку ------------------------------

   Играют двое, у кого в Настройках задана одна общая папка. Всё общение —
   файлами в <папка>\CursorPad-Nardy:

     online\<я>.txt            — «я в сети»: имя, компьютер; раз в минуту
     inv\<кому>\<партия>.txt   — приглашение
     games\<партия>\p0.txt     — всё, что пишет пригласивший (сторона 0)
     games\<партия>\p1.txt     — всё, что пишет принявший (сторона 1)

   Каждый пишет только свой файл (целиком, через временный — половину не
   прочтут) и читает файл соперника. Строки: name, id, commit, seed, t (ход),
   resign, decline, cancel.

   Кубики честные: пригласивший сперва кладёт только отпечаток (SHA-256)
   своего случайного числа, принявший — своё число открыто, после этого
   пригласивший открывает своё, и программа соперника сверяет его с
   отпечатком. Все броски партии считаются из этих двух чисел — ни один
   игрок не может подобрать себе удобный, даже поменяв свою программу.
   Каждый пришедший ход программа соперника проверяет по правилам сама.

   Партия восстанавливается из файлов целиком: закрыли программу или
   выключили компьютер — после запуска доска та же. */

#include "nardy_rules.c"

#define WM_NARDY_POLL (WM_APP + 18) /* lParam — NdPoll*, освобождает получатель */
#define ND_SUB L"CursorPad-Nardy"
/* ID_ND_* — в cursorpad.c: главные из них рисуются синими */

enum { ND_LOBBY, ND_WAIT, ND_PLAY, ND_OVER };

#define ND_MAXONLINE 40
#define ND_MAXINV 8
typedef struct {
  int nOnline;
  wchar_t onId[ND_MAXONLINE][96], onName[ND_MAXONLINE][128];
  int nInv;
  wchar_t invGame[ND_MAXINV][96], invFrom[ND_MAXINV][96], invName[ND_MAXINV][128];
  wchar_t game[96];
  wchar_t *p0, *p1; /* содержимое файлов партии (или NULL) */
  BOOL partial[2];    /* файл застали недописанным — вместо него прошлое целое */
} NdPoll;

typedef struct {
  int mode;
  wchar_t game[96];
  int me;
  wchar_t oppId[96], oppName[128];
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
  /* мой файл целиком */
  ShareBuf log;
  BOOL logLoaded;
  wchar_t status[240];
  /* лобби */
  NdPoll *poll;
  wchar_t seenInv[16][96];
  int nSeenInv;
  int notifiedTurn;
} NdGame;

static NdGame g_nd;
static HWND g_ndWnd, g_ndList;
static CRITICAL_SECTION g_ndCs;
static BOOL g_ndCsOk;
static wchar_t g_ndWatch[96]; /* какую партию читать потоку */
static float g_ndS = 1.0f;
static HBITMAP g_ndChk[2];    /* шашки: 0 — светлая (моя), 1 — тёмная */
static BYTE *g_ndChkBits[2];
static int g_ndChkD;
static BOOL g_ndThread;

static int S_(int v) { return (int)(v * g_ndS + 0.5f); }

/* ---- мелочи ---------------------------------------------------------------- */

static void nd_lock(void) {
  if (!g_ndCsOk) {
    InitializeCriticalSection(&g_ndCs);
    g_ndCsOk = TRUE;
  }
  EnterCriticalSection(&g_ndCs);
}
static void nd_unlock(void) { LeaveCriticalSection(&g_ndCs); }

static void nd_myid(wchar_t *id, int cap, wchar_t *name, int ncap) {
  wchar_t user[128], pc[64];
  share_me(user, pc);
  _snwprintf(id, cap, L"%s_%s", user, pc);
  id[cap - 1] = 0;
  for (wchar_t *c = id; *c; c++)
    if (wcschr(L"\\/:*?\"<>| .", *c)) *c = L'_';
  if (name) lstrcpynW(name, user, ncap);
}

static BOOL nd_dir(const wchar_t *root, const wchar_t *sub, wchar_t *out) {
  if (!root[0]) return FALSE;
  _snwprintf(out, SHARE_PATH, L"%s\\%s", root, ND_SUB);
  out[SHARE_PATH - 1] = 0;
  CreateDirectoryW(out, NULL);
  if (sub && sub[0]) {
    /* по уровням: inv\кому, games\партия */
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

/* SHA-256 строки в hex — отпечаток числа и броски кубиков */
static BOOL nd_sha(const char *s, BYTE out[32]) {
  HCRYPTPROV prov = 0;
  HCRYPTHASH h = 0;
  BOOL ok = FALSE;
  if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return FALSE;
  if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) {
    DWORD n = 32;
    if (CryptHashData(h, (const BYTE *)s, (DWORD)strlen(s), 0) &&
        CryptGetHashParam(h, HP_HASHVAL, out, &n, 0))
      ok = TRUE;
    CryptDestroyHash(h);
  }
  CryptReleaseContext(prov, 0);
  return ok;
}

static void nd_hex(const BYTE *b, int n, char *out) {
  static const char *hx = "0123456789abcdef";
  for (int i = 0; i < n; i++) {
    out[i * 2] = hx[b[i] >> 4];
    out[i * 2 + 1] = hx[b[i] & 15];
  }
  out[n * 2] = 0;
}

static void nd_secret(char *out) {
  BYTE r[16];
  HCRYPTPROV prov = 0;
  BOOL ok = FALSE;
  if (CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
    ok = CryptGenRandom(prov, 16, r);
    CryptReleaseContext(prov, 0);
  }
  if (!ok)
    for (int i = 0; i < 16; i++) r[i] = (BYTE)(GetTickCount() * 2654435761u >> (i % 24));
  nd_hex(r, 16, out);
}

static void nd_commit(const char *secret, char *out) {
  char buf[80];
  BYTE h[32];
  snprintf(buf, sizeof(buf), "nardy|%s", secret);
  if (nd_sha(buf, h)) nd_hex(h, 32, out);
  else out[0] = 0;
}

/* два кубика из общего зерна; k — метка броска */
static void nd_roll(const char *s0, const char *s1, const char *k, int *a, int *b) {
  BYTE h[32];
  char buf[160];
  int got = 0, v[2] = {1, 1};
  for (int round = 0; got < 2 && round < 8; round++) {
    snprintf(buf, sizeof(buf), "%s:%s:%s:%d", s0, s1, k, round);
    if (!nd_sha(buf, h)) break;
    for (int i = 0; i < 32 && got < 2; i++)
      if (h[i] < 252) v[got++] = 1 + h[i] % 6; /* без перекоса: 252 делится на 6 */
  }
  *a = v[0];
  *b = v[1];
}

static int nd_side_of(int turn) { return (turn % 2 == 1) ? g_nd.starter : 1 - g_nd.starter; }

static const wchar_t *nd_field(const wchar_t *text, const wchar_t *key, wchar_t *out, int cap) {
  out[0] = 0;
  if (!text) return NULL;
  size_t kl = wcslen(key);
  for (const wchar_t *p = text; p && *p;) {
    const wchar_t *nl = wcschr(p, L'\n');
    size_t len = nl ? (size_t)(nl - p) : wcslen(p);
    if (len > kl && !wcsncmp(p, key, kl) && p[kl] == L'\t') {
      size_t vl = len - kl - 1;
      if (vl && p[kl + 1 + vl - 1] == L'\r') vl--;
      if ((int)vl >= cap) vl = cap - 1;
      memcpy(out, p + kl + 1, vl * sizeof(wchar_t));
      out[vl] = 0;
      return out;
    }
    p = nl ? nl + 1 : NULL;
  }
  return NULL;
}

static BOOL nd_has_line(const wchar_t *text, const wchar_t *line) {
  if (!text) return FALSE;
  size_t l = wcslen(line);
  for (const wchar_t *p = text; p && *p;) {
    if (!wcsncmp(p, line, l) && (p[l] == L'\n' || p[l] == L'\r' || p[l] == 0)) return TRUE;
    p = wcschr(p, L'\n');
    if (p) p++;
  }
  return FALSE;
}

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

/* ---- файлы ---------------------------------------------------------------- */

static void nd_game_path(const wchar_t *game, int side, wchar_t *out) {
  wchar_t root[MAX_PATH], sub[200], dir[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 200, L"games\\%s", game);
  out[0] = 0;
  if (!nd_dir(root, sub, dir)) return;
  _snwprintf(out, SHARE_PATH, L"%s\\p%d.txt", dir, side);
  out[SHARE_PATH - 1] = 0;
}

static BOOL nd_write_mine(void) {
  wchar_t path[SHARE_PATH];
  nd_game_path(g_nd.game, g_nd.me, path);
  if (!path[0] || !g_nd.log.w) return FALSE;
  if (share_write_ex(path, g_nd.log.w, TRUE)) return TRUE;
  DWORD err = GetLastError();
  if (g_dataDir[0]) { /* для разбора: почему не записалось */
    wchar_t lp[MAX_PATH];
    _snwprintf(lp, MAX_PATH, L"%s\\nardy.log", g_dataDir);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[SHARE_PATH + 80];
    _snwprintf(line, SHARE_PATH + 80, L"%02d.%02d %02d:%02d:%02d не записался %s (ошибка %lu)\r\n", t.wDay, t.wMonth,
               t.wHour, t.wMinute, t.wSecond, path, err);
    line[SHARE_PATH + 79] = 0;
    char u[(SHARE_PATH + 80) * 3];
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, (int)sizeof(u), NULL, NULL);
    HANDLE f = CreateFileW(lp, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
      DWORD w = 0;
      if (n > 1) WriteFile(f, u, (DWORD)(n - 1), &w, NULL);
      CloseHandle(f);
    }
  }
  return FALSE;
}

static void nd_log_reset(void) {
  free(g_nd.log.w);
  memset(&g_nd.log, 0, sizeof(g_nd.log));
}

/* своё состояние между запусками: какая партия, моя сторона, моё число */
static void nd_save_local(void) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%s\\nardy.txt", g_dataDir);
  if (g_nd.mode != ND_WAIT && g_nd.mode != ND_PLAY) {
    DeleteFileW(path);
    return;
  }
  wchar_t sec[40];
  MultiByteToWideChar(CP_UTF8, 0, g_nd.mySecret, -1, sec, 40);
  wchar_t txt[600];
  _snwprintf(txt, 600, L"game\t%s\nme\t%d\nsecret\t%s\nopp\t%s\noppname\t%s\nmode\t%d\n", g_nd.game,
             g_nd.me, sec, g_nd.oppId, g_nd.oppName, g_nd.mode);
  txt[599] = 0;
  share_write(path, txt);
}

static void nd_load_local(void) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%s\\nardy.txt", g_dataDir);
  wchar_t *t = share_read(path);
  if (!t) return;
  wchar_t v[200];
  if (nd_field(t, L"game", v, 96) && v[0]) {
    lstrcpynW(g_nd.game, v, 96);
    if (nd_field(t, L"me", v, 8)) g_nd.me = _wtoi(v) ? 1 : 0;
    if (nd_field(t, L"secret", v, 40)) WideCharToMultiByte(CP_UTF8, 0, v, -1, g_nd.mySecret, 40, NULL, NULL);
    nd_field(t, L"opp", g_nd.oppId, 96);
    nd_field(t, L"oppname", g_nd.oppName, 128);
    g_nd.mode = (nd_field(t, L"mode", v, 8) && _wtoi(v) == ND_WAIT) ? ND_WAIT : ND_PLAY;
    g_nd.logLoaded = FALSE; /* свой файл дочитаем из папки */
    nd_lock();
    lstrcpynW(g_ndWatch, g_nd.game, 96);
    nd_unlock();
  }
  free(t);
}

/* ---- фоновый поток: кто в сети, приглашения, файлы партии ----------------- */

/* Опрос папки бережный: при двадцати коллегах каждый раз перечитывать все
   отметки — сотни чтений в секунду на сетевом диске. Поэтому: кто в сети —
   раз в 10 с (имя читается только у нового), приглашения — раз в 5 с,
   файлы партии — раз в секунду и только пока она идёт. */
static DWORD WINAPI nd_thread(LPVOID param) {
  (void)param;
  ULONGLONG lastBeat = 0, lastOnline = 0, lastInv = 0, lastPost = 0;
  static wchar_t *good[2]; /* последнее целое содержимое файлов партии */
  static wchar_t goodGame[96];
  unsigned lastSum = 0;
  static NdPoll cur; /* последнее известное — копируется в каждое сообщение */
  static wchar_t cacheId[ND_MAXONLINE * 2][96], cacheName[ND_MAXONLINE * 2][128];
  static int nCache;
  for (;;) {
    Sleep(1000);
    wchar_t root[MAX_PATH];
    share_root_copy(root);
    if (!root[0]) continue;
    wchar_t myId[96], myName[128], dOn[SHARE_PATH], dInv[SHARE_PATH], sub[160];
    nd_myid(myId, 96, myName, 128);
    if (!nd_dir(root, L"online", dOn)) continue;
    ULONGLONG now = GetTickCount64();
    if (!lastBeat || now - lastBeat > 60000) {
      wchar_t path[SHARE_PATH], txt[300], user[128], pc[64];
      share_me(user, pc);
      _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dOn, myId);
      _snwprintf(txt, 300, L"name\t%s\npc\t%s\n", user, pc);
      if (share_write(path, txt)) lastBeat = now;
    }
    wchar_t pat[SHARE_PATH + 8];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    if (!lastOnline || now - lastOnline > 10000) {
      lastOnline = now;
      cur.nOnline = 0;
      _snwprintf(pat, SHARE_PATH + 8, L"%s\\*.txt", dOn);
      f = FindFirstFileW(pat, &fd);
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
    }
    if (!lastInv || now - lastInv > 5000) {
      lastInv = now;
      cur.nInv = 0;
      _snwprintf(sub, 160, L"inv\\%s", myId);
      if (nd_dir(root, sub, dInv)) {
        _snwprintf(pat, SHARE_PATH + 8, L"%s\\*.txt", dInv);
        f = FindFirstFileW(pat, &fd);
        if (f != INVALID_HANDLE_VALUE) {
          do {
            wchar_t full[SHARE_PATH];
            _snwprintf(full, SHARE_PATH, L"%s\\%s", dInv, fd.cFileName);
            if (!ft_within(fd.ftLastWriteTime, 900)) { /* старше 15 минут — уже не ждут */
              DeleteFileW(full);
              continue;
            }
            if (cur.nInv >= ND_MAXINV) continue;
            wchar_t *t = share_read(full);
            if (!t) continue;
            nd_field(t, L"game", cur.invGame[cur.nInv], 96);
            nd_field(t, L"from", cur.invFrom[cur.nInv], 96);
            nd_field(t, L"name", cur.invName[cur.nInv], 128);
            free(t);
            if (cur.invGame[cur.nInv][0]) cur.nInv++;
          } while (FindNextFileW(f, &fd));
          FindClose(f);
        }
      }
    }
    NdPoll *pl = (NdPoll *)calloc(1, sizeof(NdPoll));
    if (!pl) continue;
    memcpy(pl, &cur, sizeof(NdPoll));
    pl->p0 = pl->p1 = NULL;
    nd_lock();
    lstrcpynW(pl->game, g_ndWatch, 96);
    nd_unlock();
    if (wcscmp(goodGame, pl->game)) {
      free(good[0]);
      free(good[1]);
      good[0] = good[1] = NULL;
      lstrcpynW(goodGame, pl->game, 96);
    }
    if (pl->game[0]) {
      for (int s = 0; s < 2; s++) {
        wchar_t p[SHARE_PATH];
        nd_game_path(pl->game, s, p);
        wchar_t *t = p[0] ? share_read(p) : NULL;
        /* каждая запись кончается переводом строки; без него файл застали
           недописанным — берём прошлое целое, иначе недописанный ход
           показался бы неправильным и остановил партию */
        size_t l = t ? wcslen(t) : 0;
        if (t && (l == 0 || t[l - 1] != L'\n')) {
          free(t);
          t = good[s] ? _wcsdup(good[s]) : NULL;
          pl->partial[s] = TRUE;
        } else if (t) {
          free(good[s]);
          good[s] = _wcsdup(t);
        }
        if (s == 0) pl->p0 = t;
        else pl->p1 = t;
      }
    }
    /* отправляем, только если что-то поменялось */
    unsigned sum = 2166136261u;
#define ND_MIX(ptr, len)                                                          \
  do {                                                                            \
    const BYTE *m_ = (const BYTE *)(ptr);                                         \
    for (size_t i_ = 0; i_ < (size_t)(len); i_++) sum = (sum ^ m_[i_]) * 16777619u; \
  } while (0)
    ND_MIX(pl->onId, sizeof(pl->onId[0]) * pl->nOnline);
    ND_MIX(pl->onName, sizeof(pl->onName[0]) * pl->nOnline);
    ND_MIX(pl->invGame, sizeof(pl->invGame[0]) * pl->nInv);
    ND_MIX(pl->game, sizeof(pl->game));
    ND_MIX(pl->partial, sizeof(pl->partial));
    if (pl->p0) ND_MIX(pl->p0, wcslen(pl->p0) * sizeof(wchar_t));
    if (pl->p1) ND_MIX(pl->p1, wcslen(pl->p1) * sizeof(wchar_t));
#undef ND_MIX
    /* во время партии — ещё и раз в 5 секунд без перемен: окно сверит свой
       файл в папке и перепишет его, если прошлая запись не удалась */
    BOOL due = sum != lastSum || (pl->game[0] && now - lastPost > 5000);
    if (!due || !g_hwnd || !PostMessageW(g_hwnd, WM_NARDY_POLL, 0, (LPARAM)pl)) {
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

static void nd_poll_free(NdPoll *p) {
  if (!p) return;
  free(p->p0);
  free(p->p1);
  free(p);
}

/* ---- ход партии по файлам ------------------------------------------------- */

static void nd_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_nd.status, 240, fmt, ap);
  va_end(ap);
  g_nd.status[239] = 0;
}

static void nd_balloon(const wchar_t *title, const wchar_t *text) {
  if (!g_trayAdded) return;
  NOTIFYICONDATAW n = g_nid;
  n.uFlags = NIF_INFO;
  lstrcpynW(n.szInfoTitle, title, 64);
  lstrcpynW(n.szInfo, text, 256);
  n.dwInfoFlags = NIIF_INFO;
  g_balloonKind = 1;
  Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void nd_begin_my_turn(void) {
  g_nd.rolled = FALSE;
  g_nd.work = g_nd.board;
  g_nd.ndice = nd_dice_list(g_nd.d1, g_nd.d2, g_nd.dice);
  g_nd.ndone = 0;
  g_nd.sel = 0;
}

/* Разыграть партию с начала по обоим файлам. mine — мой файл (в памяти
   он главнее папки: только что сделанный ход папка ещё не вернула). */
static void nd_replay(const wchar_t *p0, const wchar_t *p1) {
  const wchar_t *mine = g_nd.log.w, *theirs = g_nd.me == 0 ? p1 : p0;
  const wchar_t *file[2];
  file[g_nd.me] = mine;
  file[1 - g_nd.me] = theirs;
  wchar_t v[128];
  if (nd_field(theirs, L"name", v, 128) && v[0]) lstrcpynW(g_nd.oppName, v, 128);
  /* отказ или отмена приглашения */
  if (g_nd.me == 0 && nd_has_line(theirs, L"decline")) {
    g_nd.mode = ND_LOBBY;
    nd_status(L"%s отказался от партии", g_nd.oppName);
    nd_save_local();
    return;
  }
  if (g_nd.me == 1 && nd_has_line(theirs, L"cancel")) {
    g_nd.mode = ND_LOBBY;
    nd_status(L"%s отменил приглашение", g_nd.oppName);
    nd_save_local();
    return;
  }
  /* числа для кубиков */
  wchar_t w0[80], w1[80], wc[80];
  char s0[40] = "", s1[40] = "", c0[70] = "";
  if (nd_field(file[0], L"seed", w0, 80)) WideCharToMultiByte(CP_UTF8, 0, w0, -1, s0, 40, NULL, NULL);
  if (nd_field(file[1], L"seed", w1, 80)) WideCharToMultiByte(CP_UTF8, 0, w1, -1, s1, 40, NULL, NULL);
  if (nd_field(file[0], L"commit", wc, 80)) WideCharToMultiByte(CP_UTF8, 0, wc, -1, c0, 70, NULL, NULL);
  if (g_nd.me == 0 && s1[0] && !s0[0]) {
    /* соперник открыл своё число — открываем своё */
    wchar_t sec[40];
    MultiByteToWideChar(CP_UTF8, 0, g_nd.mySecret, -1, sec, 40);
    sb_add(&g_nd.log, L"seed\t%s\n", sec);
    nd_write_mine();
    lstrcpyA(s0, g_nd.mySecret);
    mine = g_nd.log.w;
    file[0] = mine;
  }
  if (!s0[0] || !s1[0]) {
    g_nd.ready = FALSE;
    if (g_nd.mode == ND_WAIT && s1[0]) g_nd.mode = ND_PLAY;
    if (g_nd.mode == ND_WAIT) nd_status(L"Ждём ответа: %s", g_nd.oppName[0] ? g_nd.oppName : g_nd.oppId);
    else if (g_nd.me == 1 && !s0[0])
      nd_status(L"Готовим кубики: ждём %s (у него должен быть запущен CursorPad)…", g_nd.oppName);
    else nd_status(L"Готовим кубики…");
    return;
  }
  if (g_nd.me == 1) {
    char chk[70];
    nd_commit(s0, chk);
    if (strcmp(chk, c0) != 0) {
      g_nd.mode = ND_OVER;
      g_nd.winner = -1;
      lstrcpynW(g_nd.endWhy, L"Число соперника не совпало с отпечатком — партия остановлена", 200);
      nd_save_local();
      return;
    }
  }
  if (g_nd.mode == ND_WAIT) g_nd.mode = ND_PLAY;
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
      g_nd.mode = ND_OVER;
      g_nd.winner = 1 - loser;
      g_nd.points = 1;
      g_nd.resigned = TRUE;
      nd_save_local();
      return;
    }
    NdStep st[8];
    BOOL found;
    int n = nd_turn_line(file[side], turn, st, 8, &found);
    if (!found) {
      g_nd.board = bd;
      g_nd.turn = turn;
      g_nd.d1 = d1;
      g_nd.d2 = d2;
      return;
    }
    /* проверка по правилам — и своих, и чужих ходов */
    int dice[4], nd = nd_dice_list(d1, d2, dice);
    for (int i = 0; i < n; i++) {
      NdStep legal[64];
      int k = nd_legal_steps(&bd, side, dice, nd, legal, 64), ok = -1;
      for (int j = 0; j < k && ok < 0; j++)
        if (legal[j].from == st[i].from && legal[j].die == st[i].die) ok = j;
      if (ok < 0) {
        g_nd.mode = ND_OVER;
        g_nd.winner = -1;
        _snwprintf(g_nd.endWhy, 200, L"Ход %d не прошёл проверку правил — партия остановлена", turn);
        g_nd.board = bd;
        nd_save_local();
        return;
      }
      nd_apply(&bd, side, legal[ok]);
      nd = nd_use_die(dice, nd, legal[ok].die);
      if (side != g_nd.me && i < 4) g_nd.lastTo[i] = legal[ok].to ? nd_abs(side, legal[ok].to) : -1;
    }
    NdStep more[4];
    if (nd_legal_steps(&bd, side, dice, nd, more, 4) > 0) {
      g_nd.mode = ND_OVER;
      g_nd.winner = -1;
      _snwprintf(g_nd.endWhy, 200, L"Ход %d сыгран не полностью — партия остановлена", turn);
      g_nd.board = bd;
      nd_save_local();
      return;
    }
    g_nd.lastOpp = side != g_nd.me;
    if (side != g_nd.me) {
      g_nd.nlast = n > 4 ? 4 : n;
      g_nd.lastD1 = d1;
      g_nd.lastD2 = d2;
    } else {
      g_nd.nlast = 0;
    }
    if (bd.off[side] >= ND_CHECKERS) {
      g_nd.board = bd;
      g_nd.mode = ND_OVER;
      g_nd.winner = side;
      g_nd.points = nd_points(&bd, side);
      g_nd.turn = turn;
      nd_save_local();
      return;
    }
  }
}

static void nd_refresh_view(void);

static void nd_after_replay(int prevTurn, int prevMode) {
  if (g_nd.mode == ND_PLAY && g_nd.ready) {
    int side = nd_side_of(g_nd.turn);
    if (side == g_nd.me) {
      if (g_nd.turn != prevTurn || prevMode != ND_PLAY) nd_begin_my_turn();
      if (!g_nd.rolled) nd_status(L"Ваш ход — бросайте кубики");
      if (g_nd.notifiedTurn != g_nd.turn && (!g_ndWnd || GetForegroundWindow() != g_ndWnd)) {
        g_nd.notifiedTurn = g_nd.turn;
        wchar_t t[160];
        if (g_nd.turn == 1) _snwprintf(t, 160, L"Партия с %s: вы начинаете", g_nd.oppName);
        else _snwprintf(t, 160, L"%s сходил — ваш ход", g_nd.oppName);
        nd_balloon(L"Нарды", t);
      }
    } else {
      nd_status(L"Ходит %s…", g_nd.oppName);
    }
  }
  if (g_nd.mode == ND_OVER && prevMode != ND_OVER) {
    wchar_t t[200];
    if (g_nd.winner == g_nd.me)
      _snwprintf(t, 200, L"Вы выиграли у %s%s", g_nd.oppName,
                 g_nd.resigned ? L" — соперник сдался" : (g_nd.points == 3 ? L" — кокс!" : (g_nd.points == 2 ? L" — марс!" : L"")));
    else if (g_nd.winner >= 0)
      _snwprintf(t, 200, L"Выиграл %s%s", g_nd.oppName,
                 g_nd.resigned ? L" — вы сдались" : (g_nd.points == 3 ? L" — кокс" : (g_nd.points == 2 ? L" — марс" : L"")));
    else
      lstrcpynW(t, g_nd.endWhy, 200);
    nd_status(L"%s", t);
    if (!g_ndWnd || GetForegroundWindow() != g_ndWnd) nd_balloon(L"Нарды", t);
  }
  nd_refresh_view();
}

static void nd_on_poll(NdPoll *pl) {
  nd_poll_free(g_nd.poll);
  g_nd.poll = pl;
  /* приглашения: всплывашка — один раз на каждое */
  for (int i = 0; i < pl->nInv; i++) {
    BOOL seen = FALSE;
    for (int k = 0; k < g_nd.nSeenInv && !seen; k++) seen = !wcscmp(g_nd.seenInv[k], pl->invGame[i]);
    if (seen) continue;
    lstrcpynW(g_nd.seenInv[g_nd.nSeenInv % 16], pl->invGame[i], 96);
    g_nd.nSeenInv++;
    wchar_t t[200];
    _snwprintf(t, 200, L"%s зовёт сыграть в короткие нарды", pl->invName[i]);
    nd_balloon(L"Нарды", t);
  }
  BOOL thisGame = g_nd.game[0] && !wcscmp(pl->game, g_nd.game);
  const wchar_t *mineNow = g_nd.me == 0 ? pl->p0 : pl->p1;
  if (thisGame && g_nd.mode != ND_LOBBY && !g_nd.logLoaded) {
    /* после перезапуска: свой файл — из папки; не прочёлся — подождём */
    if (!mineNow) {
      nd_refresh_view();
      return;
    }
    nd_log_reset();
    sb_add(&g_nd.log, L"%s", mineNow);
    g_nd.logLoaded = TRUE;
  }
  /* в папке не то, что у нас (запись не удалась или ещё не дошла) — пишем снова;
     и после конца партии: «сдаюсь» тоже должно дойти */
  if (thisGame && g_nd.mode != ND_LOBBY && g_nd.logLoaded && g_nd.log.w && g_nd.log.w[0] &&
      (!mineNow || pl->partial[g_nd.me] || wcscmp(mineNow, g_nd.log.w)))
    nd_write_mine();
  if ((g_nd.mode == ND_WAIT || g_nd.mode == ND_PLAY) && thisGame) {
    int prevTurn = g_nd.turn, prevMode = g_nd.mode;
    nd_replay(pl->p0, pl->p1);
    nd_after_replay(prevTurn, prevMode);
    return;
  }
  nd_refresh_view();
}

/* ---- действия игрока -------------------------------------------------------- */

static void nd_invite(const wchar_t *oppId, const wchar_t *oppName) {
  wchar_t myId[96], myName[128];
  nd_myid(myId, 96, myName, 128);
  memset(&g_nd.board, 0, sizeof(g_nd.board));
  _snwprintf(g_nd.game, 96, L"%s-%llx", myId, GetTickCount64());
  g_nd.game[95] = 0;
  g_nd.me = 0;
  lstrcpynW(g_nd.oppId, oppId, 96);
  lstrcpynW(g_nd.oppName, oppName, 128);
  wchar_t *dot = wcsstr(g_nd.oppName, L"  ·");
  if (dot) *dot = 0;
  nd_secret(g_nd.mySecret);
  char commit[70];
  nd_commit(g_nd.mySecret, commit);
  wchar_t wcom[70];
  MultiByteToWideChar(CP_UTF8, 0, commit, -1, wcom, 70);
  nd_log_reset();
  sb_add(&g_nd.log, L"name\t%s\nid\t%s\ncommit\t%s\n", myName, myId, wcom);
  g_nd.logLoaded = TRUE;
  g_nd.turn = 0;
  g_nd.ready = FALSE;
  if (!nd_write_mine()) {
    nd_status(L"Не удалось записать в общую папку");
    nd_refresh_view();
    return;
  }
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH], txt[400];
  share_root_copy(root);
  _snwprintf(sub, 160, L"inv\\%s", oppId);
  if (nd_dir(root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, g_nd.game);
    _snwprintf(txt, 400, L"from\t%s\nname\t%s\ngame\t%s\n", myId, myName, g_nd.game);
    share_write(path, txt);
  }
  g_nd.mode = ND_WAIT;
  nd_status(L"Ждём ответа: %s", g_nd.oppName);
  nd_lock();
  lstrcpynW(g_ndWatch, g_nd.game, 96);
  nd_unlock();
  nd_save_local();
  nd_refresh_view();
}

static void nd_drop_invite(const wchar_t *game) {
  wchar_t root[MAX_PATH], myId[96], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  nd_myid(myId, 96, NULL, 0);
  _snwprintf(sub, 160, L"inv\\%s", myId);
  if (!nd_dir(root, sub, dir)) return;
  _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, game);
  DeleteFileW(path);
}

static void nd_answer(int idx, BOOL accept) {
  NdPoll *pl = g_nd.poll;
  if (!pl || idx < 0 || idx >= pl->nInv) return;
  wchar_t game[96], from[96], name[128], myId[96], myName[128];
  lstrcpynW(game, pl->invGame[idx], 96);
  lstrcpynW(from, pl->invFrom[idx], 96);
  lstrcpynW(name, pl->invName[idx], 128);
  nd_myid(myId, 96, myName, 128);
  nd_drop_invite(game);
  if (!accept) {
    wchar_t path[SHARE_PATH];
    nd_game_path(game, 1, path);
    if (path[0]) share_write(path, L"decline\n");
    nd_refresh_view();
    return;
  }
  if (g_nd.mode == ND_PLAY || g_nd.mode == ND_WAIT) {
    if (MessageBoxW(g_ndWnd, L"Идёт другая партия. Сдать её и начать новую?", L"Нарды",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
      return;
    if (g_nd.mode == ND_PLAY) {
      sb_add(&g_nd.log, L"resign\n");
      nd_write_mine();
    }
  }
  memset(&g_nd.board, 0, sizeof(g_nd.board));
  lstrcpynW(g_nd.game, game, 96);
  g_nd.me = 1;
  lstrcpynW(g_nd.oppId, from, 96);
  lstrcpynW(g_nd.oppName, name, 128);
  nd_secret(g_nd.mySecret);
  wchar_t sec[40];
  MultiByteToWideChar(CP_UTF8, 0, g_nd.mySecret, -1, sec, 40);
  nd_log_reset();
  /* у принявшего отпечаток не нужен: пригласивший своё число уже закрепил */
  sb_add(&g_nd.log, L"name\t%s\nid\t%s\nseed\t%s\n", myName, myId, sec);
  g_nd.logLoaded = TRUE;
  g_nd.turn = 0;
  g_nd.ready = FALSE;
  g_nd.resigned = FALSE;
  nd_write_mine();
  g_nd.mode = ND_PLAY;
  nd_status(L"Готовим кубики…");
  nd_lock();
  lstrcpynW(g_ndWatch, g_nd.game, 96);
  nd_unlock();
  nd_save_local();
  nd_refresh_view();
}

static void nd_legal_now(NdStep *st, int *n) {
  *n = nd_legal_steps(&g_nd.work, g_nd.me, g_nd.dice, g_nd.ndice, st, 64);
}

static BOOL nd_my_turn(void) {
  return g_nd.mode == ND_PLAY && g_nd.ready && nd_side_of(g_nd.turn) == g_nd.me;
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
      nd_apply(&g_nd.work, g_nd.me, st[pick]);
      g_nd.ndice = nd_use_die(g_nd.dice, g_nd.ndice, st[pick].die);
      g_nd.sel = 0;
      /* у шашки дальше один-единственный путь — выбираем её сразу */
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
  sb_add(&g_nd.log, L"t\t%d\t", g_nd.turn);
  if (!g_nd.ndone) sb_add(&g_nd.log, L"-");
  for (int i = 0; i < g_nd.ndone; i++)
    sb_add(&g_nd.log, L"%s%d:%d", i ? L" " : L"", g_nd.done[i].from, g_nd.done[i].die);
  sb_add(&g_nd.log, L"\n");
  if (!nd_write_mine()) nd_status(L"Не удалось записать ход в общую папку — повторю сам");
  int prevTurn = g_nd.turn, prevMode = g_nd.mode;
  NdPoll *pl = g_nd.poll;
  nd_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  nd_after_replay(prevTurn, prevMode);
}

static void nd_resign(void) {
  if (g_nd.mode != ND_PLAY) return;
  if (MessageBoxW(g_ndWnd, L"Сдать партию?", L"Нарды", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  sb_add(&g_nd.log, L"resign\n");
  nd_write_mine();
  int prevTurn = g_nd.turn, prevMode = g_nd.mode;
  NdPoll *pl = g_nd.poll;
  nd_replay(pl ? pl->p0 : NULL, pl ? pl->p1 : NULL);
  nd_after_replay(prevTurn, prevMode);
}

static void nd_cancel_wait(void) {
  if (g_nd.mode != ND_WAIT) return;
  sb_add(&g_nd.log, L"cancel\n");
  nd_write_mine();
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 160, L"inv\\%s", g_nd.oppId);
  if (nd_dir(root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, g_nd.game);
    DeleteFileW(path);
  }
  g_nd.mode = ND_LOBBY;
  nd_status(L"Приглашение отменено");
  nd_save_local();
  nd_refresh_view();
}

static void nd_back_to_lobby(void) {
  g_nd.mode = ND_LOBBY;
  g_nd.game[0] = 0;
  nd_lock();
  g_ndWatch[0] = 0;
  nd_unlock();
  nd_log_reset();
  nd_status(L"");
  nd_save_local();
  nd_refresh_view();
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
  int me = g_nd.me, op = 1 - me;
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
  if (g_nd.mode == ND_PLAY && g_nd.ready) {
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
  if (g_nd.mode == ND_PLAY || g_nd.mode == ND_OVER) {
    wchar_t t[120];
    _snwprintf(t, 120, L"путь: вы %d · %s %d", nd_pips(b, me), g_nd.oppName, nd_pips(b, op));
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
  int m = g_nd.mode, pad = S_(14), bh = S_(28), gap = S_(8);
  int by = S_(ND_TOP) + S_(ND_H) + S_(28);
  HWND b[10];
  int ids[10] = {ID_ND_INVITE, ID_ND_ACCEPT, ID_ND_DECLINE, ID_ND_ROLL, ID_ND_UNDO, ID_ND_DONE,
                 ID_ND_RESIGN, ID_ND_BACK, ID_ND_CANCEL, ID_ND_LIST};
  for (int i = 0; i < 10; i++) b[i] = GetDlgItem(g_ndWnd, ids[i]);
  BOOL lobby = m == ND_LOBBY;
  NdPoll *pl = g_nd.poll;
  BOOL inv = lobby && pl && pl->nInv > 0;
  ShowWindow(b[9], lobby ? SW_SHOW : SW_HIDE);
  if (lobby) MoveWindow(b[9], pad, S_(ND_TOP) + S_(24), rc.right - pad * 2, S_(200), TRUE);
  ShowWindow(b[0], lobby ? SW_SHOW : SW_HIDE);
  if (lobby) {
    MoveWindow(b[0], pad, S_(ND_TOP) + S_(232), S_(220), bh, TRUE);
    EnableWindow(b[0], pl && pl->nOnline > 0);
  }
  ShowWindow(b[1], inv ? SW_SHOW : SW_HIDE);
  ShowWindow(b[2], inv ? SW_SHOW : SW_HIDE);
  if (inv) {
    MoveWindow(b[1], pad, S_(ND_TOP) + S_(300), S_(120), bh, TRUE);
    MoveWindow(b[2], pad + S_(120) + gap, S_(ND_TOP) + S_(300), S_(120), bh, TRUE);
  }
  BOOL play = m == ND_PLAY, over = m == ND_OVER, wait = m == ND_WAIT;
  BOOL my = nd_my_turn();
  ShowWindow(b[3], play && my && !g_nd.rolled ? SW_SHOW : SW_HIDE);
  ShowWindow(b[4], play && my && g_nd.rolled ? SW_SHOW : SW_HIDE);
  ShowWindow(b[5], play && my && g_nd.rolled ? SW_SHOW : SW_HIDE);
  ShowWindow(b[6], play ? SW_SHOW : SW_HIDE);
  ShowWindow(b[7], over ? SW_SHOW : SW_HIDE);
  ShowWindow(b[8], wait ? SW_SHOW : SW_HIDE);
  int x = pad;
  if (play && my && !g_nd.rolled) {
    MoveWindow(b[3], x, by, S_(150), bh, TRUE);
    x += S_(150) + gap;
  }
  if (play && my && g_nd.rolled) {
    MoveWindow(b[4], x, by, S_(110), bh, TRUE);
    x += S_(110) + gap;
    MoveWindow(b[5], x, by, S_(100), bh, TRUE);
    x += S_(100) + gap;
    EnableWindow(b[4], g_nd.ndone > 0);
    NdStep st[64];
    int n;
    nd_legal_now(st, &n);
    EnableWindow(b[5], n == 0);
  }
  if (play) MoveWindow(b[6], rc.right - pad - S_(100), by, S_(100), bh, TRUE);
  if (over) MoveWindow(b[7], pad, by, S_(160), bh, TRUE);
  if (wait) MoveWindow(b[8], pad, S_(ND_TOP) + S_(60), S_(170), bh, TRUE);
}

/* кнопка «Нарды» в окне курсора: точка, когда вас ждут — ваш ход или зовут */
static void nd_mark_btn(void) {
  if (!g_btnNd) return;
  BOOL wait = nd_my_turn() || (g_nd.mode != ND_PLAY && g_nd.mode != ND_WAIT && g_nd.poll && g_nd.poll->nInv > 0);
  const wchar_t *want = wait ? L"● Нарды" : L"Нарды";
  wchar_t cur[32];
  GetWindowTextW(g_btnNd, cur, 32);
  if (wcscmp(cur, want)) SetWindowTextW(g_btnNd, want);
}

static void nd_refresh_view(void) {
  nd_mark_btn();
  if (!g_ndWnd) return;
  if (g_ndList && g_nd.mode == ND_LOBBY) {
    NdPoll *pl = g_nd.poll;
    int sel = (int)SendMessageW(g_ndList, LB_GETCURSEL, 0, 0);
    wchar_t selId[96] = L"";
    if (sel >= 0 && pl && sel < pl->nOnline) lstrcpynW(selId, pl->onId[sel], 96);
    SendMessageW(g_ndList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_ndList, LB_RESETCONTENT, 0, 0);
    int keep = -1;
    if (pl)
      for (int i = 0; i < pl->nOnline; i++) {
        SendMessageW(g_ndList, LB_ADDSTRING, 0, (LPARAM)pl->onName[i]);
        if (selId[0] && !wcscmp(selId, pl->onId[i])) keep = i;
      }
    if (keep < 0 && pl && pl->nOnline > 0) keep = 0; /* сразу выбран первый — «Позвать» жмётся без поиска */
    if (keep >= 0) SendMessageW(g_ndList, LB_SETCURSEL, keep, 0);
    SendMessageW(g_ndList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_ndList, NULL, TRUE);
  }
  nd_layout();
  InvalidateRect(g_ndWnd, NULL, FALSE);
}

static void nd_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Нарды");
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  int pad = S_(14);
  if (g_nd.mode == ND_LOBBY) {
    wchar_t root[MAX_PATH];
    share_root_copy(root);
    RECT t = {pad, S_(ND_TOP), rc.right - pad, S_(ND_TOP) + S_(22)};
    NdPoll *pl = g_nd.poll;
    if (!root[0])
      DrawTextW(hdc, L"Задайте общую папку в Настройках («общая папка — PLM для коллег»)", -1, &t,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    else
      DrawTextW(hdc, pl && pl->nOnline ? L"Кто в сети: выберите коллегу и нажмите «Позвать играть» ниже" : L"Из коллег сейчас никого в сети",
                -1, &t, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (pl && pl->nInv) {
      wchar_t s[200];
      _snwprintf(s, 200, L"%s зовёт сыграть", pl->invName[0]);
      RECT r2 = {pad, S_(ND_TOP) + S_(272), rc.right - pad, S_(ND_TOP) + S_(296)};
      SetTextColor(hdc, COL_SAGE);
      DrawTextW(hdc, s, -1, &r2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (g_nd.status[0]) {
      RECT r3 = {pad, rc.bottom - S_(34), rc.right - pad, rc.bottom - S_(10)};
      SetTextColor(hdc, COL_MUTED);
      DrawTextW(hdc, g_nd.status, -1, &r3, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    return;
  }
  if (g_nd.mode == ND_WAIT) {
    RECT t = {pad, S_(ND_TOP), rc.right - pad, S_(ND_TOP) + S_(40)};
    DrawTextW(hdc, g_nd.status, -1, &t, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    return;
  }
  nd_paint_board(hdc);
  RECT st = {pad, S_(ND_TOP) + S_(ND_H) + S_(4), rc.right - pad, S_(ND_TOP) + S_(ND_H) + S_(26)};
  SetTextColor(hdc, g_nd.mode == ND_OVER ? COL_SAGE : COL_INK);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  DrawTextW(hdc, g_nd.status, -1, &st, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
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
    if (g_nd.mode == ND_PLAY) {
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
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (id == ID_ND_LIST && HIWORD(wParam) == LBN_DBLCLK) id = ID_ND_INVITE;
    if (id == ID_ND_INVITE) {
      int sel = (int)SendMessageW(g_ndList, LB_GETCURSEL, 0, 0);
      NdPoll *pl = g_nd.poll;
      if (sel < 0 || !pl || sel >= pl->nOnline) {
        nd_status(L"Выберите, кого позвать");
        nd_refresh_view();
      } else {
        nd_invite(pl->onId[sel], pl->onName[sel]);
      }
    }
    if (id == ID_ND_ACCEPT) nd_answer(0, TRUE);
    if (id == ID_ND_DECLINE) nd_answer(0, FALSE);
    if (id == ID_ND_ROLL) nd_click_point(-1);
    if (id == ID_ND_UNDO) nd_undo();
    if (id == ID_ND_DONE) nd_done();
    if (id == ID_ND_RESIGN) nd_resign();
    if (id == ID_ND_BACK) nd_back_to_lobby();
    if (id == ID_ND_CANCEL) nd_cancel_wait();
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
  nd_load_local();
  HANDLE th = CreateThread(NULL, 0, nd_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
}

static void nardy_show(void) {
  if (!g_ndWnd) {
    HDC s = GetDC(NULL);
    g_ndS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
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
    int w = S_(ND_LEFT * 2 + nd_board_w()), h = S_(ND_TOP + ND_H + 72);
    g_ndWnd = CreateWindowExW(WS_EX_APPWINDOW, L"CursorPadNardy", L"Нарды",
                              WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU, 0, 0, w, h,
                              NULL, NULL, g_inst, NULL);
    if (!g_ndWnd) return;
    round_corners(g_ndWnd);
    mk_btn(g_ndWnd, L"×", ID_PANEL_CLOSE);
    g_ndList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                               0, 0, 100, 100, g_ndWnd, (HMENU)(INT_PTR)ID_ND_LIST, g_inst, NULL);
    const struct { const wchar_t *t; int id; } btns[] = {
        {L"Позвать играть", ID_ND_INVITE}, {L"Принять", ID_ND_ACCEPT}, {L"Отказаться", ID_ND_DECLINE},
        {L"Бросить кубики", ID_ND_ROLL}, {L"Отменить ход", ID_ND_UNDO}, {L"Готово", ID_ND_DONE},
        {L"Сдаться", ID_ND_RESIGN}, {L"К списку игроков", ID_ND_BACK}, {L"Отменить приглашение", ID_ND_CANCEL}};
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
      HWND b = mk_btn(g_ndWnd, btns[i].t, btns[i].id);
      if (g_fontUi) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    }
    if (g_fontBody) SendMessageW(g_ndList, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_ndWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2,
                 w, h, SWP_NOZORDER);
  }
  if (g_nd.mode == ND_LOBBY && !g_nd.status[0]) nd_status(L"");
  nd_refresh_view();
  ShowWindow(g_ndWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_ndWnd);
  SetFocus(g_ndWnd);
}
