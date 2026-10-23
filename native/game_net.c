/* ---- Игры с коллегами через общую папку: общая связь -----------------------

   Шашки, нарды, морской бой и шахматы ходят одинаково. До 2026.10.22 эта часть жила
   в shashki.c, а у нард была своя, старая: без копий ходов и без записи на
   своём компьютере — на рабочей сети их ходы не доходили, партия вставала на
   «Готовим кубики» или «Ходит …». Теперь у каждой игры свой GnNet и своя
   папка в общей (CursorPad-Shashki, CursorPad-Nardy, CursorPad-Morskoy,
   CursorPad-Shahmaty), а код один:

     inv\<кому>\<партия>.txt   — приглашение: from, name, game
     games\<партия>\p0.txt     — пишет пригласивший (сторона 0)
     games\<партия>\p1.txt     — пишет принявший (сторона 1)
     mv\<кому>\<партия>-<сторона>-<время>.txt — копия своего файла партии

   Каждый пишет только свой файл (целиком, через временный) и читает чужой.
   Общие строки: name, id, decline (со swap — отказ из-за встречного
   приглашения), cancel, resign; остальное — дело игры. Партия
   восстанавливается из файлов целиком.

   С 2026.09.23.73 (шашки): файл партии, который каждый переписывает на месте,
   на рабочей сети до соперника не доходил, а чат и приглашения — новые файлы
   в папке получателя — доходили. Поэтому каждая своя запись уходит ещё и
   копией: новым файлом в mv\<кому>\, программа соперника забирает его и
   удаляет. Из файла партии и копии берётся более длинный — запись только
   дописывается. Своя запись и последняя принятая копия хранятся и на своём
   компьютере (<игра>_log.txt, <игра>_in.txt), какая партия — <игра>.txt.
   Пока ждём соперника, раз в минуту копия повторяется.

   Кто в сети — общий список, его ведёт поток нард (nardy.c, online\<я>.txt). */

enum { GN_LOBBY, GN_WAIT, GN_PLAY, GN_OVER };

#define GN_MAXINV 8
#define ND_MAXONLINE 40

typedef struct {
  int nInv;
  wchar_t invGame[GN_MAXINV][96], invFrom[GN_MAXINV][96], invName[GN_MAXINV][128];
  wchar_t game[96];
  wchar_t *p0, *p1;
  BOOL partial[2];
  /* для строки «связь»: когда файл менялся, почему не прочёлся, когда читали */
  ULONGLONG mt[2];
  DWORD rerr[2];
  ULONGLONG readAt;
  /* копии от соперника (mv\<я>): сколько пришло, когда последняя; взята ли
     из копии, а не из файла партии; забрал ли соперник мою: −1 — не знаю */
  int mvIn;
  ULONGLONG mvInAt;
  BOOL viaMv[2];
  int mvTaken;
} GnPoll;

/* кто в сети (ведёт поток нард): тот же список у шашек, морского боя, шахмат и чата */
typedef struct {
  int nOnline;
  wchar_t onId[ND_MAXONLINE][96], onName[ND_MAXONLINE][128];
} NdPoll;

typedef struct {
  /* какая игра — задаётся один раз */
  const wchar_t *sub;     /* папка в общей: L"CursorPad-Shashki" */
  const wchar_t *local;   /* свои файлы рядом с заметками: L"shashki" → shashki.txt, shashki_log.txt… */
  const wchar_t *title;   /* L"Шашки" — всплывашки и вопросы */
  const wchar_t *invText; /* L"зовёт сыграть в шашки" */
  const wchar_t *moveKey; /* с чего начинаются строки ходов: L"m" — для строки «связь» */
  UINT msg;               /* сообщение главному окну с GnPoll* */
  int balloon;            /* g_balloonKind: куда ведёт щелчок по всплывашке */
  HWND *wnd;
  float sc;               /* масштаб окна (точек на логическую) */
  int idList, idInvite, idAccept, idDecline, idCancel;
  void (*fresh)(void);    /* новая партия: своя начальная позиция, свои строки в log после name/id */
  void (*view)(void);     /* перерисовать окно и кнопку */
  void (*turn)(void);     /* строка хода (после принятия приглашения) */
  void (*extra)(wchar_t *out, int cap); /* что ещё хранить в <игра>.txt — строки «ключ\tзначение\n» */
  /* партия */
  int mode;
  wchar_t game[96];
  int me; /* 0 — пригласил, 1 — принял */
  wchar_t oppId[96], oppName[128];
  ShareBuf log; /* мой файл партии целиком */
  BOOL logLoaded;
  wchar_t status[240];
  GnPoll *poll;
  wchar_t seenInv[16][96];
  int nSeenInv;
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
  /* для потока — под nd_lock: какую партию читать, моя последняя копия */
  wchar_t watch[96], watchMv[SHARE_PATH];
  BOOL thread;
  /* список «кто в сети» в окне — по его строкам, а не по свежему списку:
     тот мог смениться, и выбор перескакивал на другого */
  wchar_t listId[ND_MAXONLINE][96], listName[ND_MAXONLINE][128];
  int listN;
} GnNet;

/* кто в сети — у нард (nardy.c) */
static BOOL nd_is_online(const wchar_t *id, BOOL *known);

static int gn_px(const GnNet *n, int v) { return (int)(v * n->sc + 0.5f); }

/* ---- мелочи (общие с чатом коллег, msg.c) --------------------------------- */

static CRITICAL_SECTION g_ndCs;
static BOOL g_ndCsOk;

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

/* есть ли строка «key\t<n>» */
static BOOL gn_has_num(const wchar_t *text, const wchar_t *key, int n) {
  wchar_t line[40];
  _snwprintf(line, 40, L"%s\t%d", key, n);
  return nd_has_line(text, line);
}

/* значение строки «key\t<n>\t<значение>»; FALSE — такой нет */
static BOOL gn_num_line(const wchar_t *text, const wchar_t *key, int n, char *out, int cap) {
  out[0] = 0;
  if (!text) return FALSE;
  wchar_t k[40];
  _snwprintf(k, 40, L"%s\t%d\t", key, n);
  size_t kl = wcslen(k);
  for (const wchar_t *p = text; p && *p;) {
    if (!wcsncmp(p, k, kl)) {
      const wchar_t *q = p + kl;
      int i = 0;
      while (*q && *q != L'\n' && *q != L'\r' && i < cap - 1) {
        out[i++] = *q < 128 ? (char)*q : '?';
        q++;
      }
      out[i] = 0;
      return TRUE;
    }
    p = wcschr(p, L'\n');
    if (p) p++;
  }
  return FALSE;
}

/* сколько строк «key\t…» в файле — ходов для строки «связь» */
static int gn_count_lines(const wchar_t *t, const wchar_t *key) {
  int n = 0;
  size_t kl = key ? wcslen(key) : 0;
  if (!kl) return 0;
  for (const wchar_t *p = t; p && *p;) {
    if (!wcsncmp(p, key, kl) && p[kl] == L'\t') n++;
    p = wcschr(p, L'\n');
    if (p) p++;
  }
  return n;
}

/* SHA-256 строки — отпечатки (кубики нард, расстановка морского боя) */
static BOOL gn_sha(const char *s, BYTE out[32]) {
  HCRYPTPROV prov = 0;
  HCRYPTHASH h = 0;
  BOOL ok = FALSE;
  if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return FALSE;
  if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) {
    DWORD n = 32;
    if (CryptHashData(h, (const BYTE *)s, (DWORD)strlen(s), 0) && CryptGetHashParam(h, HP_HASHVAL, out, &n, 0))
      ok = TRUE;
    CryptDestroyHash(h);
  }
  CryptReleaseContext(prov, 0);
  return ok;
}

static void gn_hex(const BYTE *b, int n, char *out) {
  static const char *hx = "0123456789abcdef";
  for (int i = 0; i < n; i++) {
    out[i * 2] = hx[b[i] >> 4];
    out[i * 2 + 1] = hx[b[i] & 15];
  }
  out[n * 2] = 0;
}

/* случайное число (16 байт) в hex */
static void gn_secret(char *out) {
  BYTE r[16];
  HCRYPTPROV prov = 0;
  BOOL ok = FALSE;
  if (CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
    ok = CryptGenRandom(prov, 16, r);
    CryptReleaseContext(prov, 0);
  }
  if (!ok)
    for (int i = 0; i < 16; i++) r[i] = (BYTE)(GetTickCount() * 2654435761u >> (i % 24));
  gn_hex(r, 16, out);
}

/* что-то одно у обоих одинаковое, но заранее не угадать: по номеру партии */
static int gn_game_bit(const wchar_t *game) {
  unsigned h = 2166136261u;
  for (const wchar_t *p = game; *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
  return (int)((h >> 7) & 1);
}

/* ---- файлы ---------------------------------------------------------------- */

static BOOL gn_dir(const GnNet *n, const wchar_t *root, const wchar_t *sub, wchar_t *out) {
  if (!root[0]) return FALSE;
  _snwprintf(out, SHARE_PATH, L"%s\\%s", root, n->sub);
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

static void gn_game_path(const GnNet *n, const wchar_t *game, int side, wchar_t *out) {
  wchar_t root[MAX_PATH], sub[200], dir[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 200, L"games\\%s", game);
  out[0] = 0;
  if (!gn_dir(n, root, sub, dir)) return;
  _snwprintf(out, SHARE_PATH, L"%s\\p%d.txt", dir, side);
  out[SHARE_PATH - 1] = 0;
}

/* свой файл рядом с заметками: <игра><tail> */
static void gn_local_path(const GnNet *n, const wchar_t *tail, wchar_t *out) {
  _snwprintf(out, MAX_PATH, L"%s\\%s%s", g_dataDir, n->local, tail);
  out[MAX_PATH - 1] = 0;
}

static unsigned gn_hash(const wchar_t *a, const wchar_t *b, int side) {
  unsigned h = 2166136261u ^ (unsigned)side;
  for (const wchar_t *p = a; p && *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
  h = (h ^ 0x7Cu) * 16777619u;
  for (const wchar_t *p = b; p && *p; p++) h = (h ^ (unsigned)*p) * 16777619u;
  return h;
}

/* Копия своей записи — новым файлом в папку соперника mv\<кому>\, как
   сообщение чата. track — копия текущей партии: одинаковую второй раз не
   шлём, прежнюю незабранную убираем (в новой всё то же и новее). */
static BOOL gn_send_copy(GnNet *n, const wchar_t *game, int side, const wchar_t *to, const wchar_t *text, BOOL track) {
  if (!game[0] || !to[0] || !text || !text[0]) return FALSE;
  unsigned h = gn_hash(game, text, side);
  if (track && h == n->mvHash) return TRUE;
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 160, L"mv\\%s", to);
  sub[159] = 0;
  BOOL ok = FALSE;
  if (gn_dir(n, root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s-%d-%llx.txt", dir, game, side, ft_now());
    path[SHARE_PATH - 1] = 0;
    size_t len = wcslen(text) + 240;
    wchar_t *body = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (body) {
      _snwprintf(body, len, L"game\t%s\nside\t%d\n%s", game, side, text);
      body[len - 1] = 0;
      ok = share_write(path, body);
      free(body);
    }
  }
  if (track) {
    n->mvFail = !ok;
    if (ok) {
      if (n->mvPath[0]) DeleteFileW(n->mvPath);
      lstrcpynW(n->mvPath, path, SHARE_PATH);
      nd_lock();
      lstrcpynW(n->watchMv, path, SHARE_PATH);
      nd_unlock();
      n->mvHash = h;
      n->mvAt = ft_now();
    }
  }
  return ok;
}

/* своя запись — ещё и на своём компьютере: после перезапуска партия
   восстанавливается, даже если общая папка свой файл не отдаёт */
static void gn_save_log_local(GnNet *n) {
  if (!g_dataDir[0] || !n->game[0] || !n->log.w) return;
  wchar_t path[MAX_PATH];
  gn_local_path(n, L"_log.txt", path);
  size_t len = wcslen(n->log.w) + 140;
  wchar_t *b = (wchar_t *)malloc(len * sizeof(wchar_t));
  if (!b) return;
  _snwprintf(b, len, L"game\t%s\n%s", n->game, n->log.w);
  b[len - 1] = 0;
  share_write(path, b);
  free(b);
}

/* не записалось в общую папку — в <игра>.log: для разбора */
static void gn_log_fail(const GnNet *n, const wchar_t *path, DWORD err) {
  if (!g_dataDir[0]) return;
  wchar_t lp[MAX_PATH];
  gn_local_path(n, L".log", lp);
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t line[SHARE_PATH + 80];
  _snwprintf(line, SHARE_PATH + 80, L"%02d.%02d %02d:%02d:%02d не записался %s (ошибка %lu)\r\n", t.wDay, t.wMonth,
             t.wHour, t.wMinute, t.wSecond, path, err);
  line[SHARE_PATH + 79] = 0;
  char u[(SHARE_PATH + 80) * 3];
  int k = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, (int)sizeof(u), NULL, NULL);
  HANDLE f = CreateFileW(lp, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD w = 0;
    if (k > 1) WriteFile(f, u, (DWORD)(k - 1), &w, NULL);
    CloseHandle(f);
  }
}

/* свой файл партии — в папку, копией сопернику и на свой компьютер */
static BOOL gn_write_mine(GnNet *n) {
  wchar_t path[SHARE_PATH];
  gn_game_path(n, n->game, n->me, path);
  BOOL ok = path[0] && n->log.w && share_write_ex(path, n->log.w, TRUE);
  if (!ok && path[0] && n->log.w) gn_log_fail(n, path, GetLastError());
  n->fileFail = !ok;
  if (ok) n->wroteAt = ft_now();
  BOOL sent = n->log.w && gn_send_copy(n, n->game, n->me, n->oppId, n->log.w, TRUE);
  gn_save_log_local(n);
  n->writeFail = !ok && !sent; /* дошло хоть одним путём — не пугаем */
  return ok || sent;
}

static void gn_log_reset(GnNet *n) {
  free(n->log.w);
  memset(&n->log, 0, sizeof(n->log));
}

/* какая партия — между запусками */
static void gn_save_local(GnNet *n) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH];
  gn_local_path(n, L".txt", path);
  if (n->mode != GN_WAIT && n->mode != GN_PLAY) {
    DeleteFileW(path);
    gn_local_path(n, L"_log.txt", path);
    DeleteFileW(path);
    return;
  }
  wchar_t txt[1200], ex[600] = L"";
  if (n->extra) n->extra(ex, 600);
  ex[599] = 0;
  _snwprintf(txt, 1200, L"game\t%s\nme\t%d\nopp\t%s\noppname\t%s\nmode\t%d\n%s", n->game, n->me, n->oppId, n->oppName,
             n->mode, ex);
  txt[1199] = 0;
  share_write(path, txt);
}

/* Партия с прошлого запуска. Возвращает весь <игра>.txt (освобождает
   вызвавший) — в нём и свои строки игры; NULL — партии не было. */
static wchar_t *gn_load_local(GnNet *n) {
  if (!g_dataDir[0]) return NULL;
  wchar_t path[MAX_PATH];
  gn_local_path(n, L".txt", path);
  wchar_t *t = share_read(path);
  if (!t) return NULL;
  wchar_t v[200];
  if (!nd_field(t, L"game", v, 96) || !v[0]) {
    free(t);
    return NULL;
  }
  lstrcpynW(n->game, v, 96);
  if (nd_field(t, L"me", v, 8)) n->me = _wtoi(v) ? 1 : 0;
  nd_field(t, L"opp", n->oppId, 96);
  nd_field(t, L"oppname", n->oppName, 128);
  n->mode = (nd_field(t, L"mode", v, 8) && _wtoi(v) == GN_WAIT) ? GN_WAIT : GN_PLAY;
  n->logLoaded = FALSE; /* свой файл дочитаем из папки */
  {
    /* или со своего компьютера — если там та же партия */
    wchar_t lp[MAX_PATH], g[96] = L"";
    gn_local_path(n, L"_log.txt", lp);
    wchar_t *lt = share_read(lp);
    const wchar_t *body = lt ? wcschr(lt, L'\n') : NULL;
    if (lt && body && nd_field(lt, L"game", g, 96) && !wcscmp(g, n->game) && body[1]) {
      gn_log_reset(n);
      sb_add(&n->log, L"%s", body + 1);
      n->logLoaded = TRUE;
    }
    free(lt);
  }
  nd_lock();
  lstrcpynW(n->watch, n->game, 96);
  nd_unlock();
  return t;
}

/* ---- фоновый поток: приглашения и файлы партии ----------------------------- */

/* копия записи: «game\t…», «side\t…», дальше сама запись; NULL — не копия
   (или застали недописанной) */
static const wchar_t *gn_parse_copy(const wchar_t *t, wchar_t *game, int *side) {
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

/* что поток помнит между проходами — своё у каждой игры (поток у каждой) */
typedef struct {
  wchar_t *good[2]; /* последнее целое содержимое файлов партии */
  wchar_t goodGame[96];
  GnPoll cur;
  ULONGLONG invFt[GN_MAXINV];
  /* копии записей соперников из своей папки mv\<я>: по партии и стороне */
  wchar_t inGame[8][96];
  int inSide[8], inNext;
  wchar_t *inText[8];
} GnThreadState;

static DWORD WINAPI gn_thread(LPVOID param) {
  GnNet *n = (GnNet *)param;
  GnThreadState *s = (GnThreadState *)calloc(1, sizeof(GnThreadState));
  if (!s) return 0;
  ULONGLONG lastInv = 0, lastPost = 0, lastMv = 0;
  unsigned lastSum = 0;
  GnPoll *cur = &s->cur;
  if (g_dataDir[0]) {
    wchar_t ip[MAX_PATH], g[96];
    int sd1 = 0;
    gn_local_path(n, L"_in.txt", ip);
    wchar_t *t = share_read(ip);
    const wchar_t *body = gn_parse_copy(t, g, &sd1);
    if (body) {
      lstrcpynW(s->inGame[0], g, 96);
      s->inSide[0] = sd1;
      s->inText[0] = _wcsdup(body);
      s->inNext = 1;
    }
    free(t);
  }
  for (;;) {
    Sleep(1000);
    if (g_quitting) break; /* закрываемся — копии ходов заберёт следующий запуск */
    wchar_t root[MAX_PATH];
    share_root_copy(root);
    if (!root[0]) continue;
    wchar_t myId[96], dInv[SHARE_PATH], sub[160];
    nd_myid(myId, 96, NULL, 0);
    ULONGLONG now = GetTickCount64();
    if (!lastInv || now - lastInv > 5000) {
      lastInv = now;
      cur->nInv = 0;
      _snwprintf(sub, 160, L"inv\\%s", myId);
      if (gn_dir(n, root, sub, dInv)) {
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
            if (cur->nInv >= GN_MAXINV) continue;
            wchar_t *t = share_read(full);
            if (!t) continue;
            nd_field(t, L"game", cur->invGame[cur->nInv], 96);
            nd_field(t, L"from", cur->invFrom[cur->nInv], 96);
            nd_field(t, L"name", cur->invName[cur->nInv], 128);
            free(t);
            if (!cur->invGame[cur->nInv][0]) continue;
            /* От одного человека — только последнее приглашение: он ждёт ответа
               на одно, старые (двойной щелчок по «Позвать», прошлые попытки)
               устарели. Раньше «Принять» брало первое в списке — соперник
               садился в старую партию, а позвавший ждал в новой (2026.09.23.71) */
            ULONGLONG ft = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            int dup = -1;
            for (int k = 0; k < cur->nInv && dup < 0; k++)
              if (!wcscmp(cur->invFrom[k], cur->invFrom[cur->nInv])) dup = k;
            if (dup < 0) {
              s->invFt[cur->nInv] = ft;
              cur->nInv++;
              continue;
            }
            wchar_t old[SHARE_PATH];
            if (ft > s->invFt[dup]) { /* новое свежее — оно вместо старого */
              _snwprintf(old, SHARE_PATH, L"%s\\%s.txt", dInv, cur->invGame[dup]);
              lstrcpynW(cur->invGame[dup], cur->invGame[cur->nInv], 96);
              lstrcpynW(cur->invName[dup], cur->invName[cur->nInv], 128);
              s->invFt[dup] = ft;
            } else {
              _snwprintf(old, SHARE_PATH, L"%s\\%s.txt", dInv, cur->invGame[cur->nInv]);
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
    watching = n->watch[0] != 0;
    nd_unlock();
    if (watching || !lastMv || now - lastMv > 5000) { /* без партии — реже */
      lastMv = now;
      wchar_t dMv[SHARE_PATH];
      _snwprintf(sub, 160, L"mv\\%s", myId);
      if (gn_dir(n, root, sub, dMv)) {
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
            if (g_quitting) {
              free(t);
              break;
            }
            const wchar_t *body = gn_parse_copy(t, g, &sd1);
            DeleteFileW(full);
            if (body) {
              int slot = -1;
              for (int k = 0; k < 8 && slot < 0; k++)
                if (s->inText[k] && s->inSide[k] == sd1 && !wcscmp(s->inGame[k], g)) slot = k;
              if (slot < 0) {
                slot = s->inNext++ % 8;
                free(s->inText[slot]);
                s->inText[slot] = NULL;
                lstrcpynW(s->inGame[slot], g, 96);
                s->inSide[slot] = sd1;
              }
              if (!s->inText[slot] || wcslen(body) > wcslen(s->inText[slot])) {
                free(s->inText[slot]);
                s->inText[slot] = _wcsdup(body);
                if (g_dataDir[0]) {
                  wchar_t ip[MAX_PATH];
                  gn_local_path(n, L"_in.txt", ip);
                  share_write(ip, t); /* как пришла: с шапкой */
                }
              }
              cur->mvIn++;
              cur->mvInAt = ft_now();
            }
            free(t);
          } while (FindNextFileW(f, &fd));
          FindClose(f);
        }
      }
    }
    GnPoll *pl = (GnPoll *)calloc(1, sizeof(GnPoll));
    if (!pl) continue;
    memcpy(pl, cur, sizeof(GnPoll));
    pl->p0 = pl->p1 = NULL;
    wchar_t mvp[SHARE_PATH];
    nd_lock();
    lstrcpynW(pl->game, n->watch, 96);
    lstrcpynW(mvp, n->watchMv, SHARE_PATH);
    nd_unlock();
    /* моя копия у соперника: файла нет — забрал */
    pl->mvTaken = -1;
    if (mvp[0]) {
      if (GetFileAttributesW(mvp) != INVALID_FILE_ATTRIBUTES) pl->mvTaken = 0;
      else if (GetLastError() == ERROR_FILE_NOT_FOUND) pl->mvTaken = 1;
    }
    if (wcscmp(s->goodGame, pl->game)) {
      free(s->good[0]);
      free(s->good[1]);
      s->good[0] = s->good[1] = NULL;
      lstrcpynW(s->goodGame, pl->game, 96);
    }
    if (pl->game[0]) {
      for (int sd = 0; sd < 2; sd++) {
        wchar_t p[SHARE_PATH];
        gn_game_path(n, pl->game, sd, p);
        wchar_t *t = p[0] ? share_read(p) : NULL;
        if (!t) pl->rerr[sd] = p[0] ? GetLastError() : ERROR_PATH_NOT_FOUND;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (p[0] && GetFileAttributesExW(p, GetFileExInfoStandard, &fa))
          pl->mt[sd] = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
        /* каждая запись кончается переводом строки; без него файл застали
           недописанным — берём прошлое целое, иначе недописанный ход
           показался бы неправильным и остановил партию */
        size_t l = t ? wcslen(t) : 0;
        if (t && (l == 0 || t[l - 1] != L'\n')) {
          free(t);
          t = s->good[sd] ? _wcsdup(s->good[sd]) : NULL;
          pl->partial[sd] = TRUE;
        } else if (t) {
          free(s->good[sd]);
          s->good[sd] = _wcsdup(t);
        }
        /* копия из своей папки длиннее — значит, новее: берём её */
        for (int k = 0; k < 8; k++)
          if (s->inText[k] && s->inSide[k] == sd && !wcscmp(s->inGame[k], pl->game) &&
              wcslen(s->inText[k]) > (t ? wcslen(t) : 0)) {
            free(t);
            t = _wcsdup(s->inText[k]);
            pl->viaMv[sd] = TRUE;
          }
        if (sd == 0) pl->p0 = t;
        else pl->p1 = t;
      }
      pl->readAt = ft_now();
    }
    /* отправляем, только если что-то поменялось */
    unsigned sum = 2166136261u;
#define GN_MIX(ptr, len)                                                          \
  do {                                                                            \
    const BYTE *m_ = (const BYTE *)(ptr);                                         \
    for (size_t i_ = 0; i_ < (size_t)(len); i_++) sum = (sum ^ m_[i_]) * 16777619u; \
  } while (0)
    GN_MIX(pl->invGame, sizeof(pl->invGame[0]) * pl->nInv);
    GN_MIX(pl->game, sizeof(pl->game));
    GN_MIX(pl->partial, sizeof(pl->partial));
    GN_MIX(&pl->mvTaken, sizeof(pl->mvTaken));
    GN_MIX(pl->viaMv, sizeof(pl->viaMv));
    if (pl->p0) GN_MIX(pl->p0, wcslen(pl->p0) * sizeof(wchar_t));
    if (pl->p1) GN_MIX(pl->p1, wcslen(pl->p1) * sizeof(wchar_t));
#undef GN_MIX
    /* во время партии — ещё и раз в 5 секунд без перемен: окно сверит свой
       файл в папке и перепишет его, если прошлая запись не удалась */
    BOOL due = sum != lastSum || (pl->game[0] && now - lastPost > 5000);
    if (!due || !g_hwnd || !PostMessageW(g_hwnd, n->msg, 0, (LPARAM)pl)) {
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

static void gn_poll_free(GnPoll *p) {
  if (!p) return;
  free(p->p0);
  free(p->p1);
  free(p);
}

static void gn_start(GnNet *n) {
  if (n->thread) return;
  n->thread = TRUE;
  HANDLE th = CreateThread(NULL, 0, gn_thread, n, 0, NULL);
  if (th) CloseHandle(th);
}

/* ---- ход партии ------------------------------------------------------------ */

static void gn_status(GnNet *n, const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(n->status, 240, fmt, ap);
  va_end(ap);
  n->status[239] = 0;
}

static void gn_balloon(const GnNet *n, const wchar_t *text) {
  if (!g_trayAdded) return;
  NOTIFYICONDATAW d = g_nid;
  d.uFlags = NIF_INFO;
  lstrcpynW(d.szInfoTitle, n->title, 64);
  lstrcpynW(d.szInfo, text, 256);
  d.dwInfoFlags = NIIF_INFO;
  g_balloonKind = n->balloon; /* щелчок по всплывашке — окно игры */
  Shell_NotifyIconW(NIM_MODIFY, &d);
}

/* окно игры сейчас перед глазами — всплывашка не нужна */
static BOOL gn_in_front(const GnNet *n) { return *n->wnd && GetForegroundWindow() == *n->wnd; }

static const wchar_t *gn_opp(const GnNet *n) { return n->oppName[0] ? n->oppName : n->oppId; }

/* соперник в сети — по общему списку нард (отметка «я в сети» раз в минуту);
   ещё не знаем — не пугаем */
static BOOL gn_opp_online(const GnNet *n) {
  BOOL known = FALSE;
  BOOL on = nd_is_online(n->oppId, &known);
  return on || !known;
}

static const wchar_t *gn_mine(const GnNet *n) { return n->log.w; }
static const wchar_t *gn_theirs(const GnNet *n, const wchar_t *p0, const wchar_t *p1) {
  return n->me == 0 ? p1 : p0;
}

/* Общее начало разбора партии: имя соперника, отказ, отмена. FALSE —
   партии больше нет (вернулись к списку игроков). */
static BOOL gn_replay_head(GnNet *n, const wchar_t *theirs) {
  wchar_t v[128];
  if (nd_field(theirs, L"name", v, 128) && v[0]) lstrcpynW(n->oppName, v, 128);
  if (n->me == 0 && nd_has_line(theirs, L"decline")) {
    n->mode = GN_LOBBY;
    if (nd_has_line(theirs, L"swap")) { /* позвали друг друга — играем в его партии */
      lstrcpynW(n->swapFrom, n->oppId, 96);
      n->swapT = GetTickCount64();
      gn_status(n, L"%s тоже позвал вас — соединяю в его партии…", n->oppName);
    } else {
      gn_status(n, L"%s отказался от партии", n->oppName);
    }
    gn_save_local(n);
    return FALSE;
  }
  if (n->me == 1 && nd_has_line(theirs, L"cancel")) {
    n->mode = GN_LOBBY;
    gn_status(n, L"%s отменил приглашение", n->oppName);
    gn_save_local(n);
    return FALSE;
  }
  return TRUE;
}

/* принявший уже записал свой файл (в нём id) */
static BOOL gn_accepted(const wchar_t *p1) {
  wchar_t v[96];
  return nd_field(p1, L"id", v, 96) && v[0];
}

/* «Ждём ответа» или «Восстанавливаю партию» — пока принявший не ответил */
static void gn_wait_status(GnNet *n) {
  if (gn_count_lines(n->log.w, n->moveKey) > 0) /* партия уже шла: после перезапуска его запись ещё не пришла */
    gn_status(n, L"Восстанавливаю партию — жду запись %s (до минуты)…", gn_opp(n));
  else
    gn_status(n, L"Ждём ответа: %s", gn_opp(n));
}

static void gn_answer(GnNet *n, int idx, BOOL accept, BOOL ask);

/* Пришёл опрос из потока. TRUE — игре разыграть партию заново по pl->p0/p1
   (это её партия и она идёт); иначе всё уже сделано. */
static BOOL gn_on_poll(GnNet *n, GnPoll *pl) {
  gn_poll_free(n->poll);
  n->poll = pl;
  /* уже отвеченные — вон из списка */
  for (int i = 0; i < pl->nInv;) {
    BOOL done = FALSE;
    for (int k = 0; k < 8 && k < n->nDoneInv && !done; k++) done = !wcscmp(n->doneInv[k], pl->invGame[i]);
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
  if (n->mode == GN_LOBBY && n->swapFrom[0]) {
    if (GetTickCount64() - n->swapT > 120000) n->swapFrom[0] = 0; /* две минуты — не пришло */
    else
      for (int i = 0; i < pl->nInv; i++)
        if (!wcscmp(pl->invFrom[i], n->swapFrom)) {
          n->swapFrom[0] = 0;
          lstrcpynW(n->seenInv[n->nSeenInv % 16], pl->invGame[i], 96);
          n->nSeenInv++;
          gn_answer(n, i, TRUE, FALSE);
          return FALSE;
        }
  }
  if (n->mode == GN_WAIT)
    for (int i = 0; i < pl->nInv; i++) {
      if (wcscmp(pl->invFrom[i], n->oppId) || !wcscmp(pl->invGame[i], n->game)) continue;
      lstrcpynW(n->seenInv[n->nSeenInv % 16], pl->invGame[i], 96); /* без всплывашки */
      n->nSeenInv++;
      gn_answer(n, i, wcscmp(pl->invGame[i], n->game) < 0, FALSE);
      return FALSE;
    }
  for (int i = 0; i < pl->nInv; i++) {
    BOOL seen = FALSE;
    for (int k = 0; k < n->nSeenInv && k < 16 && !seen; k++) seen = !wcscmp(n->seenInv[k], pl->invGame[i]);
    if (seen) continue;
    lstrcpynW(n->seenInv[n->nSeenInv % 16], pl->invGame[i], 96);
    n->nSeenInv++;
    wchar_t t[200];
    _snwprintf(t, 200, L"%s %s", pl->invName[i], n->invText);
    gn_balloon(n, t);
  }
  BOOL thisGame = n->game[0] && !wcscmp(pl->game, n->game);
  const wchar_t *mineNow = n->me == 0 ? pl->p0 : pl->p1;
  if (thisGame && n->mode != GN_LOBBY && !n->logLoaded) {
    /* после перезапуска: свой файл — из папки; не прочёлся — подождём */
    if (!mineNow) {
      n->view();
      return FALSE;
    }
    gn_log_reset(n);
    sb_add(&n->log, L"%s", mineNow);
    n->logLoaded = TRUE;
  }
  /* в папке своя запись длиннее той, что с компьютера (та не сохранилась) — её */
  if (thisGame && n->mode != GN_LOBBY && n->logLoaded && n->log.w && mineNow && !pl->partial[n->me] &&
      wcslen(mineNow) > n->log.len && !wcsncmp(mineNow, n->log.w, n->log.len)) {
    gn_log_reset(n);
    sb_add(&n->log, L"%s", mineNow);
  }
  /* ждём соперника дольше минуты — повторить ему свою копию: вдруг он её
     потерял (забрал и перезапустился) и ждёт нашего хода, а мы — его */
  if (thisGame && n->mode != GN_LOBBY && n->mode != GN_OVER && n->mvAt && ft_now() - n->mvAt > 60ull * 10000000ull)
    n->mvHash = 0;
  /* в папке не то, что у нас (запись не удалась или ещё не дошла) — пишем
     снова; и после конца партии: «сдаюсь» тоже должно дойти */
  if (thisGame && n->mode != GN_LOBBY && n->logLoaded && n->log.w && n->log.w[0] &&
      (!mineNow || pl->partial[n->me] || wcscmp(mineNow, n->log.w)))
    gn_write_mine(n);
  else if (thisGame && n->mode != GN_LOBBY && n->logLoaded && n->log.w && n->log.w[0])
    gn_send_copy(n, n->game, n->me, n->oppId, n->log.w, TRUE); /* копия не ушла — ещё раз */
  if ((n->mode == GN_WAIT || n->mode == GN_PLAY) && thisGame) return TRUE;
  n->view();
  return FALSE;
}

/* ---- действия игрока -------------------------------------------------------- */

static void gn_watch(GnNet *n, const wchar_t *game) {
  nd_lock();
  lstrcpynW(n->watch, game, 96);
  nd_unlock();
}

/* новая партия: name/id и что добавит игра */
static void gn_new_log(GnNet *n) {
  wchar_t myId[96], myName[128];
  nd_myid(myId, 96, myName, 128);
  gn_log_reset(n);
  sb_add(&n->log, L"name\t%s\nid\t%s\n", myName, myId);
  n->logLoaded = TRUE;
  n->mvHash = 0;
  n->fileFail = n->writeFail = n->mvFail = FALSE;
  n->wroteAt = n->mvAt = 0;
  if (n->fresh) n->fresh();
}

static void gn_invite(GnNet *n, const wchar_t *oppId, const wchar_t *oppName) {
  if (n->mode != GN_LOBBY) return; /* уже позвали — второй раз (двойной щелчок) не зовём */
  wchar_t myId[96], myName[128];
  nd_myid(myId, 96, myName, 128);
  _snwprintf(n->game, 96, L"%s-%llx", myId, GetTickCount64());
  n->game[95] = 0;
  n->me = 0;
  lstrcpynW(n->oppId, oppId, 96);
  lstrcpynW(n->oppName, oppName, 128);
  wchar_t *dot = wcsstr(n->oppName, L"  ·");
  if (dot) *dot = 0;
  gn_new_log(n);
  if (!gn_write_mine(n)) {
    gn_status(n, L"Не удалось записать в общую папку");
    n->view();
    return;
  }
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH], txt[400];
  share_root_copy(root);
  _snwprintf(sub, 160, L"inv\\%s", oppId);
  if (gn_dir(n, root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, n->game);
    _snwprintf(txt, 400, L"from\t%s\nname\t%s\ngame\t%s\n", myId, myName, n->game);
    share_write(path, txt);
  }
  n->mode = GN_WAIT;
  gn_status(n, L"Ждём ответа: %s", n->oppName);
  gn_watch(n, n->game);
  gn_save_local(n);
  n->view();
}

static void gn_drop_invite(GnNet *n, const wchar_t *game) {
  wchar_t root[MAX_PATH], myId[96], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  nd_myid(myId, 96, NULL, 0);
  _snwprintf(sub, 160, L"inv\\%s", myId);
  if (!gn_dir(n, root, sub, dir)) return;
  _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, game);
  DeleteFileW(path);
}

/* своё приглашение — отменить без слов (перед тем как принять чужое) */
static void gn_cancel_mine_quiet(GnNet *n) {
  if (n->mode != GN_WAIT || !n->game[0]) return;
  sb_add(&n->log, L"cancel\n");
  gn_write_mine(n);
  wchar_t root[MAX_PATH], sub[160], dir[SHARE_PATH], path[SHARE_PATH];
  share_root_copy(root);
  _snwprintf(sub, 160, L"inv\\%s", n->oppId);
  if (gn_dir(n, root, sub, dir)) {
    _snwprintf(path, SHARE_PATH, L"%s\\%s.txt", dir, n->game);
    DeleteFileW(path);
  }
}

/* ask — спрашивать ли, если идёт другая партия (при взаимных приглашениях
   решает сама программа и не спрашивает) */
static void gn_answer(GnNet *n, int idx, BOOL accept, BOOL ask) {
  GnPoll *pl = n->poll;
  if (!pl || idx < 0 || idx >= pl->nInv) return;
  wchar_t game[96], from[96], name[128];
  lstrcpynW(game, pl->invGame[idx], 96);
  lstrcpynW(n->doneInv[n->nDoneInv % 8], game, 96);
  n->nDoneInv++;
  lstrcpynW(from, pl->invFrom[idx], 96);
  lstrcpynW(name, pl->invName[idx], 128);
  gn_drop_invite(n, game);
  if (!accept) {
    wchar_t path[SHARE_PATH];
    gn_game_path(n, game, 1, path);
    /* отказ из-за встречного приглашения — с пометкой: тот примет наше сам */
    const wchar_t *dt = ask ? L"decline\n" : L"decline\nswap\n";
    if (path[0]) share_write(path, dt);
    gn_send_copy(n, game, 1, from, dt, FALSE); /* и копией — как ходы */
    n->view();
    return;
  }
  if (n->mode == GN_PLAY || n->mode == GN_WAIT) {
    if (ask && n->mode == GN_PLAY &&
        MessageBoxW(*n->wnd, L"Идёт другая партия. Сдать её и начать новую?", n->title, MB_YESNO | MB_ICONQUESTION) !=
            IDYES)
      return;
    if (n->mode == GN_PLAY) {
      sb_add(&n->log, L"resign\n");
      gn_write_mine(n);
    } else {
      /* ждали ответа на своё приглашение — отменить его, иначе тот, кого
         звали, мог принять и ждать наших ходов в брошенной партии (так
         партия и вставала, когда двое звали друг друга одновременно) */
      gn_cancel_mine_quiet(n);
    }
  }
  lstrcpynW(n->game, game, 96);
  n->me = 1;
  lstrcpynW(n->oppId, from, 96);
  lstrcpynW(n->oppName, name, 128);
  gn_new_log(n);
  gn_write_mine(n);
  n->mode = GN_PLAY;
  gn_watch(n, n->game);
  gn_save_local(n);
  if (n->turn) n->turn();
  n->view();
}

static void gn_cancel_wait(GnNet *n) {
  if (n->mode != GN_WAIT) return;
  gn_cancel_mine_quiet(n);
  n->mode = GN_LOBBY;
  gn_status(n, L"Приглашение отменено");
  gn_save_local(n);
  n->view();
}

static void gn_back_to_lobby(GnNet *n) {
  n->mode = GN_LOBBY;
  n->game[0] = 0;
  gn_watch(n, L"");
  gn_log_reset(n);
  gn_status(n, L"");
  gn_save_local(n);
  n->view();
}

/* сдаться: строка в файл; разыграть заново — дело игры */
static BOOL gn_resign(GnNet *n) {
  if (n->mode != GN_PLAY) return FALSE;
  if (MessageBoxW(*n->wnd, L"Сдать партию?", n->title, MB_YESNO | MB_ICONQUESTION) != IDYES) return FALSE;
  sb_add(&n->log, L"resign\n");
  gn_write_mine(n);
  return TRUE;
}

/* ---- окно: список игроков, приглашения, ожидание ---------------------------- */

static void gn_hms(ULONGLONG ft, wchar_t *out, int cap) {
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
static void gn_diag_text(const GnNet *n, wchar_t *out, int cap) {
  GnPoll *pl = n->poll;
  int opp = 1 - n->me;
  BOOL same = pl && !wcscmp(pl->game, n->game);
  const wchar_t *theirs = same ? (opp == 0 ? pl->p0 : pl->p1) : NULL;
  wchar_t w1[16], w2[16], w3[16], w4[16], w5[16], nm[128], err[60] = L"", cp[80];
  gn_hms(n->wroteAt, w1, 16);
  gn_hms(pl ? pl->mt[opp] : 0, w2, 16);
  gn_hms(pl ? pl->readAt : 0, w3, 16);
  gn_hms(n->mvAt, w4, 16);
  gn_hms(pl ? pl->mvInAt : 0, w5, 16);
  lstrcpynW(nm, n->oppName[0] ? n->oppName : L"соперник", 128);
  if (pl && pl->rerr[opp] == ERROR_FILE_NOT_FOUND) lstrcpynW(err, L" (файла нет)", 60);
  else if (pl && pl->rerr[opp]) _snwprintf(err, 60, L" (не читается, ошибка %lu)", pl->rerr[opp]);
  /* моя копия в его папке: забрал ли */
  if (n->mvFail) lstrcpynW(cp, L"копия НЕ ЗАПИСАЛАСЬ", 80);
  else if (!n->mvAt) lstrcpynW(cp, L"копии нет", 80);
  else _snwprintf(cp, 80, L"копия %s%s", w4, !pl || pl->mvTaken < 0 ? L"" : pl->mvTaken ? L" — забрал" : L" — ещё не забрал");
  _snwprintf(out, cap,
             L"связь: у вас ходов %d · файл %s · %s  |  у %s ходов %d%s · файл %s%s · копий от него %d "
             L"(последняя %s) · проверено %s",
             gn_count_lines(n->log.w, n->moveKey), n->fileFail ? L"НЕ ЗАПИСАЛСЯ" : w1, cp, nm,
             gn_count_lines(theirs, n->moveKey), same && pl->viaMv[opp] ? L" (по копии)" : L"", w2, err,
             pl ? pl->mvIn : 0, w5, w3);
  out[cap - 1] = 0;
}

/* список «кто в сети»: тот же — не трогаем (пересборка посреди щелчка
   сбрасывала выбор); иначе пересобрать, сохранив выбранного */
static void gn_list_sync(GnNet *n, HWND list, const NdPoll *on) {
  if (!list) return;
  int cnt = on ? on->nOnline : 0;
  BOOL same = cnt == n->listN && (int)SendMessageW(list, LB_GETCOUNT, 0, 0) == cnt;
  for (int i = 0; i < cnt && same; i++) same = !wcscmp(n->listId[i], on->onId[i]) && !wcscmp(n->listName[i], on->onName[i]);
  if (same) return;
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  wchar_t selId[96] = L"";
  if (sel >= 0 && sel < n->listN) lstrcpynW(selId, n->listId[sel], 96);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  int keep = -1;
  n->listN = 0;
  for (int i = 0; i < cnt; i++) {
    SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)on->onName[i]);
    lstrcpynW(n->listId[n->listN], on->onId[i], 96);
    lstrcpynW(n->listName[n->listN], on->onName[i], 128);
    n->listN++;
    if (selId[0] && !wcscmp(selId, on->onId[i])) keep = i;
  }
  if (keep < 0 && cnt > 0) keep = 0; /* сразу выбран первый — «Позвать» жмётся без поиска */
  if (keep >= 0) SendMessageW(list, LB_SETCURSEL, keep, 0);
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, NULL, TRUE);
}

/* «Позвать играть» — того, кто выбран в строках списка */
static void gn_invite_selected(GnNet *n) {
  HWND list = GetDlgItem(*n->wnd, n->idList);
  int sel = list ? (int)SendMessageW(list, LB_GETCURSEL, 0, 0) : -1;
  if (sel < 0 || sel >= n->listN) {
    gn_status(n, L"Выберите, кого позвать");
    n->view();
    return;
  }
  gn_invite(n, n->listId[sel], n->listName[sel]);
}

/* кнопки списка игроков, приглашения и ожидания — у всех игр одинаково */
static void gn_layout_lobby(GnNet *n, const NdPoll *on) {
  HWND w = *n->wnd;
  RECT rc;
  GetClientRect(w, &rc);
  int top = gn_px(n, 46), pad = gn_px(n, 14), bh = gn_px(n, 28), gap = gn_px(n, 8);
  HWND list = GetDlgItem(w, n->idList), inv = GetDlgItem(w, n->idInvite), acc = GetDlgItem(w, n->idAccept),
       dec = GetDlgItem(w, n->idDecline), can = GetDlgItem(w, n->idCancel);
  BOOL lobby = n->mode == GN_LOBBY, invs = lobby && n->poll && n->poll->nInv > 0;
  ShowWindow(list, lobby ? SW_SHOW : SW_HIDE);
  ShowWindow(inv, lobby ? SW_SHOW : SW_HIDE);
  if (lobby) {
    MoveWindow(list, pad, top + gn_px(n, 24), rc.right - pad * 2, gn_px(n, 200), TRUE);
    MoveWindow(inv, pad, top + gn_px(n, 232), gn_px(n, 220), bh, TRUE);
    EnableWindow(inv, on && on->nOnline > 0);
  }
  ShowWindow(acc, invs ? SW_SHOW : SW_HIDE);
  ShowWindow(dec, invs ? SW_SHOW : SW_HIDE);
  if (invs) {
    MoveWindow(acc, pad, top + gn_px(n, 300), gn_px(n, 120), bh, TRUE);
    MoveWindow(dec, pad + gn_px(n, 120) + gap, top + gn_px(n, 300), gn_px(n, 120), bh, TRUE);
  }
  ShowWindow(can, n->mode == GN_WAIT ? SW_SHOW : SW_HIDE);
  if (n->mode == GN_WAIT) MoveWindow(can, pad, top + gn_px(n, 60), gn_px(n, 170), bh, TRUE);
}

/* список игроков: подсказка, кто зовёт, правила игры, строка состояния */
static void gn_paint_lobby(const GnNet *n, HDC hdc, RECT rc, const NdPoll *on, const wchar_t *rules) {
  int top = gn_px(n, 46), pad = gn_px(n, 14);
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  wchar_t root[MAX_PATH];
  share_root_copy(root);
  RECT t = {pad, top, rc.right - pad, top + gn_px(n, 22)};
  if (!root[0])
    DrawTextW(hdc, L"Задайте общую папку в Настройках («общая папка — PLM для коллег»)", -1, &t,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  else
    DrawTextW(hdc, on && on->nOnline ? L"Кто в сети: выберите коллегу и нажмите «Позвать играть» ниже"
                                      : L"Из коллег сейчас никого в сети",
              -1, &t, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  GnPoll *pl = n->poll;
  if (pl && pl->nInv) {
    wchar_t s[200];
    _snwprintf(s, 200, L"%s %s", pl->invName[0], n->invText);
    RECT r2 = {pad, top + gn_px(n, 272), rc.right - pad, top + gn_px(n, 296)};
    SetTextColor(hdc, COL_SAGE);
    DrawTextW(hdc, s, -1, &r2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  if (rules && rules[0]) {
    RECT r4 = {pad, top + gn_px(n, 340), rc.right - pad, top + gn_px(n, 420)};
    SetTextColor(hdc, COL_MUTED);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    DrawTextW(hdc, rules, -1, &r4, DT_LEFT | DT_WORDBREAK);
  }
  if (n->status[0]) {
    if (g_fontUi) SelectObject(hdc, g_fontUi);
    SetTextColor(hdc, COL_MUTED);
    RECT r3 = {pad, rc.bottom - gn_px(n, 34), rc.right - pad, rc.bottom - gn_px(n, 10)};
    DrawTextW(hdc, n->status, -1, &r3, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
}

/* ждём ответа на приглашение: кого и строка «связь» */
static void gn_paint_wait(const GnNet *n, HDC hdc, RECT rc) {
  int top = gn_px(n, 46), pad = gn_px(n, 14);
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  RECT t = {pad, top, rc.right - pad, top + gn_px(n, 40)};
  DrawTextW(hdc, n->status, -1, &t, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  wchar_t dg[400];
  gn_diag_text(n, dg, 400);
  RECT dr = {pad, top + gn_px(n, 100), rc.right - pad, top + gn_px(n, 170)};
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  DrawTextW(hdc, dg, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

/* WM_COMMAND от общих кнопок; TRUE — обработано */
static BOOL gn_command(GnNet *n, int id, int code) {
  /* от списка — только двойной щелчок; фокус у него не отнимаем: иначе
     Windows не давала выбрать в нём другого игрока */
  if (id == n->idList) {
    if (code == LBN_DBLCLK) gn_invite_selected(n);
    return TRUE;
  }
  if (id == n->idInvite) gn_invite_selected(n);
  else if (id == n->idAccept) gn_answer(n, 0, TRUE, TRUE);
  else if (id == n->idDecline) gn_answer(n, 0, FALSE, TRUE);
  else if (id == n->idCancel) gn_cancel_wait(n);
  else return FALSE;
  return TRUE;
}

/* кнопка игры в Настройках: точка, когда вас ждут — ваш ход или зовут */
static void gn_mark_btn(const GnNet *n, HWND btn, const wchar_t *label, BOOL myTurn) {
  if (!btn) return;
  BOOL wait = myTurn || (n->mode != GN_PLAY && n->mode != GN_WAIT && n->poll && n->poll->nInv > 0);
  wchar_t want[48], cur[48];
  _snwprintf(want, 48, L"%s%s", wait ? L"● " : L"", label);
  want[47] = 0;
  GetWindowTextW(btn, cur, 48);
  if (wcscmp(cur, want)) SetWindowTextW(btn, want);
}

/* ---- рисование: спрайты со сглаживанием ------------------------------------- */
/* Фигуры, пятна света и метки ходов рисуются в память попиксельно (по 16
   проб на пиксель) и кладутся на холст UiCanvas с прозрачностью — без GDI+.
   Пиксели спрайта — 0xAARRGGBB с домноженной альфой. */

typedef struct {
  int w, h;
  DWORD *px;
} GnSprite;

static void gn_sprite_free(GnSprite *s) {
  free(s->px);
  memset(s, 0, sizeof(*s));
}

static BOOL gn_sprite_alloc(GnSprite *s, int w, int h) {
  gn_sprite_free(s);
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return FALSE;
  s->px = (DWORD *)calloc((size_t)w * h, sizeof(DWORD));
  if (!s->px) return FALSE;
  s->w = w;
  s->h = h;
  return TRUE;
}

/* пиксель спрайта из цвета (0…255 на канал, можно дробно) и покрытия 0…1 */
static DWORD gn_pm(float r, float g, float b, float a) {
  if (a <= 0) return 0;
  if (a > 1) a = 1;
  if (r < 0) r = 0;
  if (g < 0) g = 0;
  if (b < 0) b = 0;
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return ((DWORD)(a * 255 + 0.5f) << 24) | ((DWORD)(r * a + 0.5f) << 16) | ((DWORD)(g * a + 0.5f) << 8) |
         (DWORD)(b * a + 0.5f);
}

/* положить спрайт на холст: левый верхний угол (x, y) в логических
   координатах холста, alpha — общая прозрачность 0…255 */
static void gn_blit(UiCanvas *c, const GnSprite *s, int x, int y, int alpha) {
  if (!c->dc || !s->px || alpha <= 0) return;
  if (alpha > 255) alpha = 255;
  GdiFlush();
  int x0 = x < c->lx ? c->lx : x, y0 = y < c->ly ? c->ly : y;
  int x1 = x + s->w, y1 = y + s->h;
  if (x1 > c->lx + c->w) x1 = c->lx + c->w;
  if (y1 > c->ly + c->h) y1 = c->ly + c->h;
  for (int yy = y0; yy < y1; yy++) {
    const DWORD *src = s->px + (size_t)(yy - y) * s->w + (x0 - x);
    DWORD *dst = c->px + (size_t)(yy - c->ly) * c->w + (x0 - c->lx);
    for (int xx = x0; xx < x1; xx++, src++, dst++) {
      DWORD p = *src;
      if (!p) continue;
      int a = (int)(p >> 24), r = (int)((p >> 16) & 255), g = (int)((p >> 8) & 255), b = (int)(p & 255);
      if (alpha < 255) {
        a = a * alpha / 255;
        r = r * alpha / 255;
        g = g * alpha / 255;
        b = b * alpha / 255;
      }
      if (!a) continue;
      DWORD d = *dst;
      int ia = 255 - a;
      int dr = (int)((d >> 16) & 255) * ia / 255 + r, dg = (int)((d >> 8) & 255) * ia / 255 + g,
          db = (int)(d & 255) * ia / 255 + b;
      if (dr > 255) dr = 255;
      if (dg > 255) dg = 255;
      if (db > 255) db = 255;
      *dst = ((DWORD)dr << 16) | ((DWORD)dg << 8) | (DWORD)db;
    }
  }
}

/* залить прямоугольник холста цветом с прозрачностью */
static void gn_tint(UiCanvas *c, RECT r, COLORREF col, int a) {
  if (!c->dc || a <= 0) return;
  GdiFlush();
  int x0 = r.left < c->lx ? c->lx : r.left, y0 = r.top < c->ly ? c->ly : r.top;
  int x1 = r.right > c->lx + c->w ? c->lx + c->w : r.right, y1 = r.bottom > c->ly + c->h ? c->ly + c->h : r.bottom;
  for (int y = y0; y < y1; y++) {
    DWORD *p = c->px + (size_t)(y - c->ly) * c->w + (x0 - c->lx);
    for (int x = x0; x < x1; x++, p++) ui_mix(p, col, a);
  }
}

/* мягкое пятно (тень, свечение): радиус r, плотность к краю спадает
   плавно; core — доля радиуса, где ещё полная плотность */
static BOOL gn_make_glow(GnSprite *s, int r, COLORREF col, float alpha, float core) {
  int d = r * 2 + 2;
  if (!gn_sprite_alloc(s, d, d)) return FALSE;
  float c = d / 2.0f;
  for (int y = 0; y < d; y++)
    for (int x = 0; x < d; x++) {
      float px = x + 0.5f - c, py = y + 0.5f - c;
      float t = sqrtf(px * px + py * py) / (float)r;
      if (t >= 1) continue;
      float a = t <= core ? 1.0f : 1.0f - (t - core) / (1.0f - core);
      a = a * a * (3 - 2 * a) * alpha;
      s->px[(size_t)y * d + x] = gn_pm(GetRValue(col), GetGValue(col), GetBValue(col), a);
    }
  return TRUE;
}

/* кольцо со сглаженным краем: внешний радиус r, толщина w, вокруг — мягкое
   свечение шириной glow */
static BOOL gn_make_ring(GnSprite *s, float r, float w, float glow, COLORREF col, float alpha) {
  int d = (int)ceilf((r + glow) * 2) + 4;
  if (!gn_sprite_alloc(s, d, d)) return FALSE;
  float c = d / 2.0f;
  for (int y = 0; y < d; y++)
    for (int x = 0; x < d; x++) {
      float a = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
          float px = x + (sx + 0.5f) / 4 - c, py = y + (sy + 0.5f) / 4 - c;
          float dd = sqrtf(px * px + py * py);
          float in = (dd <= r && dd >= r - w) ? 1.0f : 0.0f;
          if (!in && glow > 0) {
            float off = dd > r ? dd - r : (r - w) - dd;
            if (off < glow) {
              float g = 1 - off / glow;
              in = g * g * 0.55f;
            }
          }
          a += in;
        }
      a /= 16;
      if (a > 0) s->px[(size_t)y * d + x] = gn_pm(GetRValue(col), GetGValue(col), GetBValue(col), a * alpha);
    }
  return TRUE;
}

/* сглаженный круг цветом col */
static BOOL gn_make_dot(GnSprite *s, float r, COLORREF col, float alpha) { return gn_make_ring(s, r, r, 0, col, alpha); }

/* шум для фактуры дерева: значение 0…1, плавно по x, y */
static float gn_hash01(int x, int y, unsigned seed) {
  unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

static float gn_noise(float x, float y, unsigned seed) {
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  float a = gn_hash01(ix, iy, seed), b = gn_hash01(ix + 1, iy, seed), c = gn_hash01(ix, iy + 1, seed),
        d = gn_hash01(ix + 1, iy + 1, seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

/* яркость волокон дерева в точке (вдоль x), 0.8…1.1 */
static float gn_wood(float x, float y, float scale, unsigned seed) {
  float n = gn_noise(x / (60 * scale), y / (9 * scale), seed) * 2.2f + gn_noise(x / (14 * scale), y / (3 * scale), seed + 7) * 0.5f;
  float ring = sinf((y / scale + n * 9.0f) * 0.9f) * 0.5f + 0.5f;
  float fine = gn_noise(x / (2.5f * scale), y / (0.7f * scale), seed + 3);
  return 0.86f + ring * 0.12f + fine * 0.07f;
}

/* Доска шашек и шахмат: дерево с волокнами. Рама — орех, волокна вдоль
   сторон и «на ус» в углах, по внутреннему краю — золотая нить; поля — клён
   и палисандр, у каждого поля свой рисунок; у края поля — лёгкая тень.
   q — поле, fr — рама; flip — доска повёрнута (чёрные внизу): рисунок
   полей свой, а a1 — тёмное снизу слева при любом повороте. */
static void gn_render_board(GnSprite *s, int q, int fr, BOOL flip) {
  int bp = q * 8, W = bp + fr * 2;
  if (!gn_sprite_alloc(s, W, W)) return;
  float sc = q / 50.0f;
  for (int y = 0; y < W; y++)
    for (int x = 0; x < W; x++) {
      float r, g, b;
      int ix = x - fr, iy = y - fr;
      if (ix >= 0 && iy >= 0 && ix < bp && iy < bp) {
        int sc0 = ix / q, sr0 = iy / q;
        BOOL dark = ((sr0 + sc0) & 1) == 1; /* a1 — тёмное снизу слева при любом повороте */
        unsigned seed = (unsigned)(sr0 * 8 + sc0) * 7919u + (flip ? 13u : 0u);
        /* волокна полей: у соседних — поперёк, как в наборной доске */
        BOOL across = ((sr0 + sc0) >> 1 & 1) != 0;
        float wx = across ? (float)y : (float)x, wy = across ? (float)x : (float)y;
        float wd = gn_wood(wx + seed % 97, wy + seed % 89, sc * 0.8f, seed);
        if (dark) r = 112 * wd, g = 64 * wd, b = 38 * wd;
        else r = 236 * wd, g = 212 * wd, b = 166 * wd;
        /* тень от рамы на поле */
        int ed = ix < iy ? ix : iy, ed2 = (bp - 1 - ix) < (bp - 1 - iy) ? (bp - 1 - ix) : (bp - 1 - iy);
        float sh = 1.0f;
        float lim = 7 * sc;
        if (ed < lim) sh -= 0.28f * (1 - ed / lim) * (1 - ed / lim);
        if (ed2 < lim * 0.5f) sh -= 0.10f * (1 - ed2 / (lim * 0.5f));
        r *= sh, g *= sh, b *= sh;
        /* тонкий шов между полями */
        if (ix % q == 0 || iy % q == 0) r *= 0.9f, g *= 0.9f, b *= 0.9f;
      } else {
        /* рама: какая сторона ближе — вдоль неё волокна */
        int dl = x, dt = y, drt = W - 1 - x, db = W - 1 - y;
        int m = dl, side = 0;
        if (dt < m) m = dt, side = 1;
        if (drt < m) m = drt, side = 2;
        if (db < m) m = db, side = 3;
        float wd = (side & 1) ? gn_wood((float)x, (float)y, sc, 41u + side) : gn_wood((float)y, (float)x, sc, 53u + side);
        r = 98 * wd, g = 58 * wd, b = 32 * wd;
        /* скос: наружный край светлее сверху-слева, внутренний — тёмная канавка */
        float k = m / (float)fr;
        if (k < 0.12f) {
          float f = side == 0 || side == 1 ? 1.22f : 0.72f;
          r *= f, g *= f, b *= f;
        } else if (k > 0.80f) {
          float f = side == 0 || side == 1 ? 0.70f : 1.12f;
          r *= f, g *= f, b *= f;
        } else {
          float bev = 1.0f + 0.10f * (0.5f - fabsf(k - 0.46f));
          r *= bev, g *= bev, b *= bev;
        }
        /* золотая нить */
        if (m == fr - (int)(4 * sc + 0.5f)) r = 214, g = 170, b = 88;
      }
      s->px[(size_t)y * W + x] = gn_pm(r, g, b, 1.0f);
    }
}
