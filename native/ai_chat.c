/* ai_chat.c — окно «Спросить ИИ» (пункт «Ещё»): чат с локальной моделью (ai.h).
   Беседа помнит реплики этого окна: модели уходят последние, что влезают в
   окно контекста (вкладка «ИИ»). Enter — отправить, Shift+Enter — новая строка.
   «запомни: …» и кнопка «Запомнить» пишут тетрадь ai_learn.txt: к похожему
   вопросу подмешиваются эти факты и встроенная карточка (mini_ai.c). Веса
   модели не меняются — свой LoRA по-прежнему файлом lora*.gguf рядом с моделью.
   Ответ приходит сообщением WM_AI_CHAT с AiResult*. Беседа живёт в памяти окна. */

/* GUID форматов WIC: без библиотеки wincodecsdk их нет в ссылке */
static const GUID kAcPngFormat = {0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
static const GUID kAcBgra = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f}};
#define WM_AI_CHAT (WM_APP + 72) /* lParam — AiResult*, освободить ai_result_free */
#define WM_AC_SEND (WM_APP + 73) /* из поля ввода: отправить */
#define ID_AC_LOG 4701
#define ID_AC_IN 4702
#define ID_AC_SEND 4703
#define ID_AC_STOP 4704
#define ID_AC_CLEAR 4705
#define ID_AC_COPY 4706
#define ID_AC_SET 4707
#define ID_AC_STAT 4708
#define ID_AC_TITLE 4709
#define ID_AC_SHOT 4711
#define ID_AC_THUMB 4712
#define ID_AC_LEARN 4713
#define AC_CMD_SYS                                                                                                      \
  L"Ты выполняешь навык «Файлы» программы CursorPad. Ответь ОДНОЙ строкой JSON, без пояснений и без ``` . "            \
  L"Сам файлы не трогаешь: человек ещё нажмёт «Применить»."
#define AC_SYS                                                                                                           \
  L"Ты мини-ИИ в программе CursorPad на этом компьютере (модель Qwen, без интернета). "                                  \
  L"Отвечай по-русски, коротко и по делу. Числа, граммовки, пути и номера ГОСТ не выдумывай. "                           \
  L"Если ниже есть блок «Выучено» — отвечай по нему и не противоречь ему.\r\n"                                           \
  L"CursorPad: этот чат открывают «Мини-ИИ» и «Ещё → Спросить ИИ». Поиск PLM — это Союз, «Файлы» — имена в архиве. "      \
  L"«Распознать чертёж» читает штамп ГОСТ 2.104. Фразы про файлы (создать папку, скопировать, переместить, переименовать, разложить сканы) понимаются сами, без галочки. "     \
  L"Копирование и перенос не меняют диск, пока не нажали «Применить». "                                                     \
  L"Ты сам диск не видишь и папки не создаёшь. Никогда не пиши, что папка создана, проверена или уже есть: это скажет программа.\r\n" \
  L"«запомни: …» пишет факт в тетрадь на этом компьютере. Уровни модели: Слабый Qwen2.5 1.5B, Средний Qwen3 4B, "        \
  L"Мощный Qwen2.5 7B, С картинками Qwen3-VL 4B."

typedef struct {
  int assistant;
  wchar_t *text;
  BYTE *png; /* картинка к вопросу, PNG; NULL — без неё */
  DWORD pngLen;
} AcTurn;
static HWND g_acWnd, g_acLog, g_acIn, g_acSend, g_acStop, g_acClear, g_acCopy, g_acSet, g_acStat, g_acTitle,
    g_acShot, g_acThumb, g_acLearn;
static BYTE *g_acImg;      /* картинка, ждёт отправки */
static DWORD g_acImgLen;
static HBITMAP g_acThumbBmp; /* миниатюра ждущей картинки (и той, что не ушла) */
static void ac_relayout(void);
static void ac_layout(int W, int H);
static BOOL g_acPlanOn; /* план «было → станет» ждёт «Применить» */
static void ac_plan_ui(BOOL on);
static void ac_files_run(const wchar_t *j);
static void ac_files_apply(void);
static void ac_files_cancel(void);
static void ac_mkdir_now(const wchar_t *dst);
static void ac_group_plan(const wchar_t *user, int mode);
static const wchar_t *ac_cmd_instruction(void);
static void af_set_last(const wchar_t *dir);
static void af_known_folders(void);
static BOOL af_resolve(const wchar_t *in, wchar_t *out, BOOL allowNew);
static BOOL af_is_dir(const wchar_t *p);
static BOOL af_trusted(const wchar_t *p);
static void af_clear(void);
static BOOL g_acCmdWait;      /* ответ на команду для файлов, а не чат */
static wchar_t g_acMkName[80];     /* имя новой папки, если путь ещё не назвали */
static wchar_t g_acMkParent[MAX_PATH]; /* папка, куда класть, если имя ещё не назвали */
static wchar_t g_acMkLast[MAX_PATH];   /* что создавали в прошлый раз — проверка диска, не слова чата */
static int g_acMkWait;                 /* 1 ждём имя, 2 ждём путь */
static wchar_t g_acCmdText[2000];
static wchar_t g_acSortDst[MAX_PATH];
static BOOL g_acSortMove;
static WNDPROC g_acEditOld;
static AcTurn *g_acTurns;
static int g_acN, g_acCap;
static int g_acId; /* запрос в пути, 0 — нет */

static void ac_push(int assistant, const wchar_t *text) {
  if (g_acN == g_acCap) {
    int cap = g_acCap ? g_acCap * 2 : 16;
    AcTurn *t = (AcTurn *)realloc(g_acTurns, cap * sizeof(AcTurn));
    if (!t) return;
    g_acTurns = t;
    g_acCap = cap;
  }
  wchar_t *c = (wchar_t *)malloc((wcslen(text) + 1) * sizeof(wchar_t));
  if (!c) return;
  wcscpy(c, text);
  g_acTurns[g_acN].assistant = assistant;
  g_acTurns[g_acN].text = c;
  g_acTurns[g_acN].png = NULL;
  g_acTurns[g_acN].pngLen = 0;
  g_acN++;
}

/* снять последнюю реплику и вернуть её текст в поле ввода (вопрос не дошёл) */
/* вопрос не дошёл: текст и картинка возвращаются в поле ввода */
static void ac_unsend(void) {
  if (!g_acN) return;
  AcTurn *t = &g_acTurns[g_acN - 1];
  SetWindowTextW(g_acIn, t->text);
  if (t->png) {
    if (g_acImg) free(t->png);
    else {
      g_acImg = t->png;
      g_acImgLen = t->pngLen;
      ac_relayout();
    }
  }
  free(t->text);
  g_acN--;
}

static void ac_clear_turns(void) {
  for (int i = 0; i < g_acN; i++) {
    free(g_acTurns[i].text);
    free(g_acTurns[i].png);
  }
  g_acN = 0;
}

static void ac_redraw(void) {
  size_t cap = 64;
  for (int i = 0; i < g_acN; i++) cap += wcslen(g_acTurns[i].text) + 32;
  wchar_t *s = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!s) return;
  s[0] = 0;
  for (int i = 0; i < g_acN; i++) {
    wcscat(s, g_acTurns[i].assistant ? L"ИИ:\r\n" : L"Вы:\r\n");
    if (g_acTurns[i].png) wcscat(s, L"[картинка] ");
    wcscat(s, g_acTurns[i].text);
    wcscat(s, L"\r\n\r\n");
  }
  SetWindowTextW(g_acLog, s);
  int len = GetWindowTextLengthW(g_acLog);
  SendMessageW(g_acLog, EM_SETSEL, len, len);
  SendMessageW(g_acLog, EM_SCROLLCARET, 0, 0);
  free(s);
}

static void ac_status(const wchar_t *t) { SetWindowTextW(g_acStat, t ? t : L""); }

static void ac_busy(BOOL on) {
  EnableWindow(g_acSend, !on);
  EnableWindow(g_acStop, on);
}

/* план ждёт ответа: кнопки «Отправить» и «Стоп» становятся «Применить» и «Отмена» */
static void ac_plan_ui(BOOL on) {
  SetWindowTextW(g_acSend, on ? L"Применить" : L"Отправить");
  SetWindowTextW(g_acStop, on ? L"Отмена" : L"Стоп");
  if (on) {
    EnableWindow(g_acSend, TRUE);
    EnableWindow(g_acStop, TRUE);
  } else
    ac_busy(g_acId != 0);
}

/* «ИИ выключен» / «нет модели» — ведём на вкладку «ИИ» Настроек */
static void ac_open_settings(void) {
  select_tab(SEC_AI);
  if (g_setHwnd && !IsWindowVisible(g_setHwnd)) toggle_settings();
}

static void ac_clip(const wchar_t *s) {
  if (!OpenClipboard(g_acWnd)) return;
  EmptyClipboard();
  size_t bytes = (wcslen(s) + 1) * sizeof(wchar_t);
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (g) {
    memcpy(GlobalLock(g), s, bytes);
    GlobalUnlock(g);
    if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
  }
  CloseClipboard();
}

/* тетрадь: %LOCALAPPDATA%\CursorPad\ai_learn.txt, блоки через строку --- */
static void ac_learn_path(wchar_t *out) {
  if (g_dataDir[0]) _snwprintf(out, MAX_PATH, L"%s\\ai_learn.txt", g_dataDir);
  else out[0] = 0;
}

static wchar_t *ac_read_utf8(const wchar_t *path, DWORD maxb) {
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return NULL;
  DWORD sz = GetFileSize(h, NULL);
  if (sz == INVALID_FILE_SIZE || sz == 0) {
    CloseHandle(h);
    return NULL;
  }
  if (sz > maxb) sz = maxb;
  char *b = (char *)malloc((size_t)sz + 1);
  DWORD got = 0;
  if (!b || !ReadFile(h, b, sz, &got, NULL)) {
    CloseHandle(h);
    free(b);
    return NULL;
  }
  CloseHandle(h);
  b[got] = 0;
  if (got >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF)
    memmove(b, b + 3, got - 2);
  int n = MultiByteToWideChar(CP_UTF8, 0, b, -1, NULL, 0);
  wchar_t *w = n > 1 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
  if (w) MultiByteToWideChar(CP_UTF8, 0, b, -1, w, n);
  free(b);
  return w;
}

static BOOL ac_write_utf8(const wchar_t *path, const wchar_t *text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  if (n <= 1) return FALSE;
  char *b = (char *)malloc((size_t)n);
  if (!b) return FALSE;
  WideCharToMultiByte(CP_UTF8, 0, text, -1, b, n, NULL, NULL);
  wchar_t tmp[MAX_PATH];
  _snwprintf(tmp, MAX_PATH, L"%s.new", path);
  HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  BOOL ok = FALSE;
  if (h != INVALID_HANDLE_VALUE) {
    DWORD wr = 0;
    ok = WriteFile(h, b, (DWORD)(n - 1), &wr, NULL) && wr == (DWORD)(n - 1);
    CloseHandle(h);
  }
  free(b);
  if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(tmp);
    return FALSE;
  }
  return TRUE;
}

/* «запомни», «запомни: текст». rest — что записать (может быть пустым). */
static BOOL ac_learn_prefix(const wchar_t *a, const wchar_t **rest) {
  wchar_t q[24];
  lstrcpynW(q, a, 24);
  mini_fold(q);
  if (wcsncmp(q, L"запомни", 7) != 0) return FALSE;
  if (q[7] != 0 && q[7] != L' ' && q[7] != L':') return FALSE;
  const wchar_t *p = a + 7;
  while (*p == L' ' || *p == L':' || *p == L'\t' || *p == L'\r' || *p == L'\n') p++;
  *rest = p;
  return TRUE;
}

static const wchar_t *ac_last_role(int assistant) {
  for (int i = g_acN - 1; i >= 0; i--)
    if (g_acTurns[i].assistant == assistant && g_acTurns[i].text && g_acTurns[i].text[0]) return g_acTurns[i].text;
  return NULL;
}

/* 0 — не записал, 1 — записал, 2 — уже было */
static int ac_learn_save(const wchar_t *text) {
  wchar_t path[MAX_PATH];
  wchar_t cut[2000];
  ac_learn_path(path);
  if (!path[0] || !text || !text[0]) return 0;
  lstrcpynW(cut, text, 2000);
  wchar_t *old = ac_read_utf8(path, 48 * 1024);
  if (old && wcsstr(old, cut)) {
    free(old);
    return 2;
  }
  size_t nold = old ? wcslen(old) : 0;
  if (nold + wcslen(cut) + 8 > 20000) {
    free(old);
    return 0;
  }
  wchar_t *neu = (wchar_t *)malloc((nold + wcslen(cut) + 16) * sizeof(wchar_t));
  if (!neu) {
    free(old);
    return 0;
  }
  neu[0] = 0;
  if (old && old[0]) {
    wcscpy(neu, old);
    size_t L = wcslen(neu);
    while (L && (neu[L - 1] == L'\n' || neu[L - 1] == L'\r' || neu[L - 1] == L' ')) neu[--L] = 0;
    wcscat(neu, L"\r\n---\r\n");
  }
  wcscat(neu, cut);
  BOOL ok = ac_write_utf8(path, neu);
  free(neu);
  free(old);
  return ok ? 1 : 0;
}

static int ac_words_hit(const wchar_t *qFold, const wchar_t *textFold) {
  int hit = 0, seen = 0;
  const wchar_t *p = qFold;
  while (*p) {
    while (*p == L' ') p++;
    const wchar_t *s = p;
    while (*p && *p != L' ') p++;
    int n = (int)(p - s);
    if (n < 4) continue;
    seen++;
    if (n > 47) n = 47;
    wchar_t tok[48];
    memcpy(tok, s, (size_t)n * sizeof(wchar_t));
    tok[n] = 0;
    if (wcsstr(textFold, tok)) hit++;
  }
  if (!seen) return 0;
  return hit * 10 / seen;
}

/* подходящие блоки тетради, не длиннее cap-1 символов */
static void ac_learn_recall(const wchar_t *query, wchar_t *out, int cap) {
  out[0] = 0;
  if (cap < 8) return;
  wchar_t path[MAX_PATH];
  ac_learn_path(path);
  if (!path[0]) return;
  wchar_t *all = ac_read_utf8(path, 48 * 1024);
  if (!all) return;
  wchar_t q[400];
  lstrcpynW(q, query ? query : L"", 400);
  mini_fold(q);
  wchar_t *blocks[64];
  int nb = 0;
  blocks[nb++] = all;
  for (wchar_t *p = all; *p && nb < 64; p++) {
    if ((p == all || p[-1] == L'\n') && p[0] == L'-' && p[1] == L'-' && p[2] == L'-' &&
        (p[3] == L'\n' || p[3] == L'\r' || p[3] == 0)) {
      *p = 0;
      p += 3;
      while (*p == L'\r' || *p == L'\n') p++;
      blocks[nb++] = p;
      if (!*p) break;
      p--;
    }
  }
  int score[64], order[64];
  wchar_t *folded[64];
  memset(folded, 0, sizeof(folded));
  for (int i = 0; i < nb; i++) {
    while (*blocks[i] == L'\r' || *blocks[i] == L'\n' || *blocks[i] == L' ') blocks[i]++;
    order[i] = i;
    score[i] = 0;
    size_t len = wcslen(blocks[i]);
    folded[i] = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!folded[i] || !blocks[i][0]) continue;
    wcscpy(folded[i], blocks[i]);
    mini_fold(folded[i]);
    score[i] = ac_words_hit(q, folded[i]);
  }
  for (int i = 1; i < nb; i++) {
    int k = order[i], j = i;
    while (j > 0 && score[order[j - 1]] < score[k]) {
      order[j] = order[j - 1];
      j--;
    }
    order[j] = k;
  }
  int used = 0;
  for (int i = 0; i < nb && used < cap - 1; i++) {
    int k = order[i];
    if (score[k] <= 0) break;
    int bl = (int)wcslen(blocks[k]);
    if (bl < 2) continue;
    if (used > 0 && used + 2 < cap) {
      out[used++] = L'\r';
      out[used++] = L'\n';
    }
    if (bl > cap - 1 - used) bl = cap - 1 - used;
    if (bl <= 0) break;
    memcpy(out + used, blocks[k], (size_t)bl * sizeof(wchar_t));
    used += bl;
    out[used] = 0;
    if (used > 1400) break;
  }
  for (int i = 0; i < nb; i++) free(folded[i]);
  free(all);
}

static void ac_sys_build(const wchar_t *q, wchar_t *out, int cap) {
  const wchar_t *title = NULL;
  const wchar_t *card = mini_ai_card(q, &title);
  wchar_t lessons[1500];
  ac_learn_recall(q, lessons, 1500);
  _snwprintf(out, cap, L"%s%s%s%s%s%s%s", AC_SYS, card ? L"\r\nВыучено (карточка «" : L"", card && title ? title : L"",
             card ? L"», числа не менять):\r\n" : L"", card ? card : L"", lessons[0] ? L"\r\nВыучено (тетрадь):\r\n" : L"",
             lessons);
  out[cap - 1] = 0;
}

/* showUser — вписать реплику в беседу (это был вопрос в поле, а не кнопка) */
static void ac_remember(const wchar_t *raw, BOOL showUser) {
  const wchar_t *rest = NULL;
  const wchar_t *save = raw;
  wchar_t built[2000];
  if (raw && raw[0] && ac_learn_prefix(raw, &rest)) save = rest;
  if (!save || !save[0]) {
    const wchar_t *uq = ac_last_role(0);
    const wchar_t *aq = ac_last_role(1);
    if (!uq || !aq) {
      ac_status(L"Нечего запоминать. Напишите «запомни: факт» или сначала дождитесь ответа.");
      return;
    }
    _snwprintf(built, 2000, L"%s\r\n%s", uq, aq);
    built[1999] = 0;
    save = built;
  }
  int st = ac_learn_save(save);
  const wchar_t *msg = st == 2 ? L"Это уже есть в тетради."
                       : st == 1 ? L"Запомнил. На похожий вопрос отвечу с учётом этого. Тетрадь — ai_learn.txt рядом с "
                                   L"заметками, блоки разделяет строка ---."
                                 : L"Не записал: нет папки данных или тетрадь уже большая (сотрите старое в ai_learn.txt).";
  if (showUser && raw && raw[0]) ac_push(0, raw);
  ac_push(1, msg);
  ac_redraw();
  ac_status(st == 1 ? L"Записано в ai_learn.txt" : L"");
}

/* файловая фраза: первое слово — действие над файлами. Вопрос со знаком «?» — чат */
static BOOL ac_is_file_cmd(const wchar_t *q) {
  wchar_t s[400];
  lstrcpynW(s, q, 400);
  CharLowerBuffW(s, (DWORD)wcslen(s));
  if (wcschr(s, L'?')) return FALSE;
  const wchar_t *p = s;
  while (*p == L' ') p++;
  wchar_t w[32];
  int n = 0;
  while (*p && *p != L' ' && n < 31) w[n++] = *p++;
  w[n] = 0;
  static const wchar_t *const kVerbs[] = {L"закинь", L"закидывай", L"перемести", L"перенеси", L"переложи", L"положи",
                                          L"скопируй", L"копируй", L"переименуй", L"разложи", L"удали", L"сотри"};
  for (size_t k = 0; k < sizeof(kVerbs) / sizeof(kVerbs[0]); k++)
    if (!wcscmp(w, kVerbs[k])) return TRUE;
  if (!wcscmp(w, L"создай") || !wcscmp(w, L"сделай")) return wcsstr(s, L"папк") != NULL;
  return FALSE;
}

/* полный путь Windows в тексте: с буквы диска или с \\ */
static BOOL ac_pick_path(const wchar_t *q, wchar_t *out) {
  const wchar_t *p = q, *hit = NULL;
  while (*p) {
    BOOL drv = ((*p >= L'A' && *p <= L'Z') || (*p >= L'a' && *p <= L'z')) && p[1] == L':';
    if (drv || (p[0] == L'\\' && p[1] == L'\\')) {
      hit = p;
      break;
    }
    p++;
  }
  if (!hit) return FALSE;
  int n = 0;
  while (hit[n] && hit[n] != L'\r' && hit[n] != L'\n' && hit[n] != L'"' && n < MAX_PATH - 1) {
    out[n] = hit[n];
    n++;
  }
  while (n > 3 && (out[n - 1] == L' ' || out[n - 1] == L'\\' || out[n - 1] == L'/')) n--;
  out[n] = 0;
  return n > 3;
}

/* самое длинное начало пути, которое уже есть на диске. Хвост фразы — в *after. */
static BOOL ac_live_dir(const wchar_t *q, wchar_t *dir, const wchar_t **after) {
  const wchar_t *hit = NULL;
  for (const wchar_t *p = q; *p; p++) {
    BOOL drv = ((*p >= L'A' && *p <= L'Z') || (*p >= L'a' && *p <= L'z')) && p[1] == L':';
    if (drv || (p[0] == L'\\' && p[1] == L'\\')) {
      hit = p;
      break;
    }
  }
  if (!hit) return FALSE;
  wchar_t best[MAX_PATH], buf[MAX_PATH];
  best[0] = 0;
  int bestN = 0, n = 0;
  while (hit[n] && hit[n] != L'\r' && hit[n] != L'\n' && hit[n] != L'"' && hit[n] != L'«' && n < MAX_PATH - 1) {
    buf[n] = hit[n];
    n++;
    buf[n] = 0;
    wchar_t try[MAX_PATH];
    lstrcpynW(try, buf, MAX_PATH);
    int m = n;
    while (m > 3 && (try[m - 1] == L' ' || try[m - 1] == L'\\' || try[m - 1] == L'/')) try[--m] = 0;
    if (m > 3 && af_is_dir(try)) {
      lstrcpynW(best, try, MAX_PATH);
      bestN = n;
    }
  }
  if (!best[0]) return FALSE;
  lstrcpynW(dir, best, MAX_PATH);
  const wchar_t *a = hit + bestN;
  while (*a == L' ' || *a == L'\t') a++;
  if (after) *after = a;
  return TRUE;
}

static BOOL ac_mk_stop(const wchar_t *w) {
  static const wchar_t *const k[] = {L"создай", L"сделай",     L"папку", L"папка", L"папке", L"папок", L"в",    L"во",
                                     L"и",      L"назови",     L"назвать", L"ее",    L"её",    L"его",   L"тут", L"здесь",
                                     L"сюда",   L"пожалуйста", L"новую",   L"новый", L"имя"};
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
    if (!wcscmp(w, k[i])) return TRUE;
  return FALSE;
}

/* имя из кавычек или из слов хвоста, которые не «создай», «папку», «тут» */
static BOOL ac_mk_name(const wchar_t *q, const wchar_t *rest, wchar_t *name) {
  name[0] = 0;
  const wchar_t *a = wcschr(q, L'"');
  wchar_t end = L'"';
  if (!a) {
    a = wcschr(q, L'«');
    end = L'»';
  }
  if (a && a[1]) {
    const wchar_t *b = wcschr(a + 1, end);
    int n = b ? (int)(b - a - 1) : (int)wcslen(a + 1);
    if (n > 0 && n < 80) {
      lstrcpynW(name, a + 1, n + 1);
      while (name[0] == L' ') memmove(name, name + 1, wcslen(name) * sizeof(wchar_t));
      return name[0] != 0;
    }
  }
  if (!rest) return FALSE;
  wchar_t acc[80];
  acc[0] = 0;
  const wchar_t *p = rest;
  while (*p) {
    while (*p == L' ' || *p == L'\t' || *p == L'"' || *p == L'«' || *p == L'»' || *p == L'\\' || *p == L'/') p++;
    if (!*p) break;
    wchar_t w[80];
    int n = 0;
    while (*p && *p != L' ' && *p != L'\t' && *p != L'"' && *p != L'\\' && *p != L'/' && n < 79) w[n++] = *p++;
    w[n] = 0;
    wchar_t low[80];
    lstrcpynW(low, w, 80);
    CharLowerBuffW(low, (DWORD)wcslen(low));
    if (ac_mk_stop(low)) continue;
    if (acc[0] && wcslen(acc) + 1 < 79) wcscat(acc, L" ");
    if (wcslen(acc) + wcslen(w) < 79) wcscat(acc, w);
  }
  if (!acc[0]) return FALSE;
  lstrcpynW(name, acc, 80);
  return TRUE;
}

static BOOL ac_mk_bad_name(const wchar_t *n) {
  return !n[0] || !wcscmp(n, L".") || !wcscmp(n, L"..") || wcspbrk(n, L"\\/:*?\"<>|") != NULL;
}

static void ac_mk_go(const wchar_t *parent, const wchar_t *name) {
  wchar_t full[MAX_PATH], msg[MAX_PATH + 80];
  if (ac_mk_bad_name(name)) {
    _snwprintf(msg, MAX_PATH + 80, L"Имя «%s» для папки не годится.", name);
    ac_push(1, msg);
    return;
  }
  _snwprintf(full, MAX_PATH, L"%s\\%s", parent, name);
  lstrcpynW(g_acCmdText, full, 2000);
  lstrcpynW(g_acMkLast, full, MAX_PATH);
  g_acMkName[0] = 0;
  g_acMkParent[0] = 0;
  g_acMkWait = 0;
  ac_mkdir_now(full);
}

/* «создай папку…» разбирает программа: где кончается настоящая папка и как зовут новую.
   Модель сюда не пускаем — она пишет «создал», не трогая диск. */
static BOOL ac_mkdir_handle(const wchar_t *q) {
  wchar_t low[500];
  lstrcpynW(low, q, 500);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  BOOL verb = (wcsstr(low, L"создай") || wcsstr(low, L"сделай")) && wcsstr(low, L"папк");
  BOOL complain = wcsstr(low, L"не сделал") || wcsstr(low, L"не создал") || wcsstr(low, L"нет папки") ||
                  wcsstr(low, L"не появил");
  wchar_t dir[MAX_PATH], name[80];
  const wchar_t *after = L"";
  BOOL hasDir = ac_live_dir(q, dir, &after);
  BOOL hasName = ac_mk_name(q, hasDir ? after : q, name);
  if (complain && g_acMkLast[0] && !verb) {
    wchar_t msg[MAX_PATH + 80];
    ac_push(0, q);
    if (af_is_dir(g_acMkLast))
      _snwprintf(msg, MAX_PATH + 80, L"На диске она есть:\r\n%s", g_acMkLast);
    else
      _snwprintf(msg, MAX_PATH + 80, L"На диске её нет:\r\n%s", g_acMkLast);
    ac_push(1, msg);
    return TRUE;
  }
  if (!verb && !(g_acMkWait && (hasDir || (g_acMkWait == 1 && !wcschr(q, L'\\') && wcslen(q) < 80)))) return FALSE;
  ac_push(0, q);
  if (!hasName && g_acMkName[0] && (wcsstr(low, L"тут") || wcsstr(low, L"здесь") || wcsstr(low, L"сюда") || !verb))
    lstrcpynW(name, g_acMkName, 80), hasName = TRUE;
  if (hasDir && hasName) {
    ac_mk_go(dir, name);
    return TRUE;
  }
  if (hasName && g_acMkParent[0] && !hasDir) {
    ac_mk_go(g_acMkParent, name);
    return TRUE;
  }
  if (hasDir && g_acMkName[0] && !hasName) {
    ac_mk_go(dir, g_acMkName);
    return TRUE;
  }
  wchar_t msg[MAX_PATH + 120];
  if (hasName && !hasDir) {
    lstrcpynW(g_acMkName, name, 80);
    g_acMkWait = 2;
    _snwprintf(msg, MAX_PATH + 120, L"Как назвать, понял: «%s». В какой папке её создать? Напишите путь, как в проводнике.",
               name);
    ac_push(1, msg);
    return TRUE;
  }
  if (hasDir && !hasName) {
    lstrcpynW(g_acMkParent, dir, MAX_PATH);
    g_acMkWait = 1;
    _snwprintf(msg, MAX_PATH + 120, L"Папка уже есть:\r\n%s\r\nКак назвать новую внутри неё?", dir);
    ac_push(1, msg);
    return TRUE;
  }
  ac_push(1, L"Напишите, где создать и как назвать. Например: создай папку в C:\\Папка и назови её Архив");
  return TRUE;
}

/* 1 — по имени, 2 — по формату, 3 — и то и другое, -1 — сказано «отсортируй», но не сказано как.
   «по штампу» сюда не входит: это прежняя раскладка сканов. */
static int ac_wants_group(const wchar_t *q) {
  wchar_t s[500];
  lstrcpynW(s, q, 500);
  CharLowerBuffW(s, (DWORD)wcslen(s));
  if (wcschr(s, L'?')) return 0;
  if (wcsstr(s, L"штамп") || wcsstr(s, L"обозначен")) return 0;
  BOOL sortVerb = wcsstr(s, L"рассорт") || wcsstr(s, L"отсорт") || wcsstr(s, L"сортир");
  BOOL lay = wcsstr(s, L"разлож") != NULL;
  BOOL copy = wcsstr(s, L"скопир") != NULL;
  BOOL byName = wcsstr(s, L"назван") || wcsstr(s, L"имени") || wcsstr(s, L"именам");
  BOOL byExt = wcsstr(s, L"формат") || wcsstr(s, L"расширен");
  if (!byName && !byExt) return sortVerb ? -1 : 0;
  if (!sortVerb && !lay && !copy) return 0;
  return (byName ? 1 : 0) | (byExt ? 2 : 0);
}

static void ac_send(void) {
  if (g_acPlanOn) {
    ac_status(L"Сначала «Применить» или «Отмена» для плана");
    return;
  }
  int len = GetWindowTextLengthW(g_acIn);
  wchar_t *in = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
  if (!in) return;
  GetWindowTextW(g_acIn, in, len + 1);
  wchar_t *a = in;
  while (*a == L' ' || *a == L'\r' || *a == L'\n' || *a == L'\t') a++;
  size_t n = wcslen(a);
  while (n && (a[n - 1] == L' ' || a[n - 1] == L'\r' || a[n - 1] == L'\n' || a[n - 1] == L'\t')) a[--n] = 0;
  if (!n) {
    free(in);
    return;
  }
  {
    const wchar_t *rest = NULL;
    if (ac_learn_prefix(a, &rest)) {
      (void)rest;
      if (g_acId) {
        ac_status(L"Сначала дождитесь ответа или нажмите «Стоп» — потом запомню.");
        free(in);
        return;
      }
      ac_remember(a, TRUE);
      SetWindowTextW(g_acIn, L"");
      free(in);
      return;
    }
  }
  if (g_acId) {
    free(in);
    return;
  }
  int group = !g_acImg ? ac_wants_group(a) : 0;
  if (group) {
    ac_push(0, a);
    lstrcpynW(g_acCmdText, a, 2000);
    if (group < 0)
      ac_push(1, L"Как разложить: по формату, по имени файла или и так и так? Например: «разложи по форматам C:\\Папка» или «разложи по именам и форматам C:\\Папка».");
    else
      ac_group_plan(a, group);
    ac_redraw();
    SetWindowTextW(g_acIn, L"");
    free(in);
    return;
  }
  if (!g_acImg && ac_mkdir_handle(a)) {
    ac_redraw();
    SetWindowTextW(g_acIn, L"");
    free(in);
    return;
  }
  if (!ai_ready()) {
    ac_status(ai_status_text(g_aiOn ? AI_NO_MODEL : AI_OFF));
    ac_open_settings();
    free(in);
    return;
  }
  if ((int)n > ai_chat_budget()) {
    ac_status(L"Сообщение длиннее окна контекста. Сократите его или увеличьте контекст на вкладке «ИИ».");
    free(in);
    return;
  }
  BOOL cmd = !g_acImg && ac_is_file_cmd(a); /* файловая фраза — навык «Файлы»; с картинкой — чат */
  ac_push(0, a);
  if (g_acImg && g_acN) {
    g_acTurns[g_acN - 1].png = g_acImg; /* пока только ссылка: отдадим, если запрос пойдёт */
    g_acTurns[g_acN - 1].pngLen = g_acImgLen;
  }
  AiTurn *t = (AiTurn *)malloc(g_acN * sizeof(AiTurn));
  int id = 0;
  if (t) {
    for (int i = 0; i < g_acN; i++) {
      t[i].assistant = g_acTurns[i].assistant;
      t[i].text = g_acTurns[i].text;
      t[i].png = g_acTurns[i].png;
      t[i].pngLen = g_acTurns[i].pngLen;
    }
    /* команда разбирается без истории: ответ — только JSON.
       обычный вопрос — с карточкой и тетрадью в роли system */
    int from = cmd ? g_acN - 1 : 0;
    wchar_t *sys = NULL;
    if (!cmd) {
      sys = (wchar_t *)malloc(8000 * sizeof(wchar_t));
      if (sys) ac_sys_build(a, sys, 8000);
    }
    if (cmd) ai_out_cap(192); /* ответ — одна строка JSON, навык и так длинный */
    id = ai_ask_chat_async(cmd ? ac_cmd_instruction() : (sys ? sys : AC_SYS), t + from, g_acN - from, 180000, g_acWnd,
                           WM_AI_CHAT, NULL);
    if (cmd) ai_out_cap(0);
    free(sys);
    free(t);
  }
  if (!id) {
    if (g_acN) g_acTurns[g_acN - 1].png = NULL; /* картинка осталась ждать */
    ac_unsend();
    ac_status(L"Не вышло спросить модель, попробуйте ещё раз");
    free(in);
    return;
  }
  g_acId = id;
  g_acCmdWait = cmd;
  if (g_acImg) { /* ушла вместе с вопросом */
    g_acImg = NULL;
    g_acImgLen = 0;
    ac_relayout();
  }
  if (cmd) lstrcpynW(g_acCmdText, a, 2000);
  SetWindowTextW(g_acIn, L"");
  ac_redraw();
  ac_busy(TRUE);
  ac_status(L"Думаю… первый ответ после запуска модели может грузиться до минуты");
  free(in);
}

/* поле "key" из JSON-строки ответа модели; \\ и \" раскрываются */
static BOOL ac_json_field(const wchar_t *j, const wchar_t *key, wchar_t *out, int cap) {
  wchar_t pat[64];
  _snwprintf(pat, 64, L"\"%s\"", key);
  const wchar_t *p = wcsstr(j, pat);
  if (!p) return FALSE;
  p = wcschr(p + wcslen(pat), L'"');
  if (!p) return FALSE;
  int n = 0;
  for (p++; *p && *p != L'"' && n < cap - 1; p++) {
    if (*p == L'\\' && p[1]) p++;
    out[n++] = *p;
  }
  out[n] = 0;
  return TRUE;
}

/* путь назван в тексте пользователя — модель не может подсунуть свою папку */
static BOOL ac_named(const wchar_t *path) {
  wchar_t t[MAX_PATH];
  lstrcpynW(t, path, MAX_PATH);
  size_t n = wcslen(t);
  while (n > 3 && (t[n - 1] == L'\\' || t[n - 1] == L'/')) t[--n] = 0;
  return wcsstr(g_acCmdText, t) != NULL;
}

/* запуск раскладки: сканы папки идут в «Распознать чертёж», потом предпросмотр */
static void ac_sort_ready(void) {
  drs_show();
  if (!g_drsWnd) {
    ac_push(1, L"Нечего раскладывать: ни один скан не распознан.");
    ac_redraw();
    return;
  }
  SetWindowTextW(g_drsDir, g_acSortDst);
  SendMessageW(g_drsMove, BM_SETCHECK, g_acSortMove ? BST_CHECKED : BST_UNCHECKED, 0);
  drs_plan();
  InvalidateRect(g_drsWnd, NULL, FALSE);
  ac_push(1, L"Предпросмотр в окне «Разложить по папкам». Ничего не изменится, пока вы не нажмёте «Применить».");
  ac_redraw();
  ac_status(L"");
}

static void ac_sort_start(const wchar_t *src, const wchar_t *dst, BOOL move) {
  TmPaths l = {0};
  wchar_t pat[MAX_PATH];
  _snwprintf(pat, MAX_PATH, L"%s\\*", src);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
      if (!ext) continue;
      if (!_wcsicmp(ext, L".pdf") || !_wcsicmp(ext, L".tif") || !_wcsicmp(ext, L".tiff") || !_wcsicmp(ext, L".png") ||
          !_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg") || !_wcsicmp(ext, L".bmp")) {
        wchar_t full[MAX_PATH];
        _snwprintf(full, MAX_PATH, L"%s\\%s", src, fd.cFileName);
        tm_paths_add(&l, full);
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  if (!l.n) {
    free(l.p);
    ac_push(1, L"В этой папке нет сканов (pdf, tif, png, jpg, bmp).");
    ac_redraw();
    return;
  }
  drawrec_show();
  af_set_last(dst);
  lstrcpynW(g_acSortDst, dst, MAX_PATH);
  g_acSortMove = move;
  g_drSortHook = ac_sort_ready;
  dr_add_paths(&l);
  ac_push(1, L"Распознаю сканы из папки. Когда закончу, покажу предпросмотр.");
  ac_redraw();
}

/* ответ модели на команду: проверки здесь, а не в модели */
static void ac_cmd_run(const wchar_t *j) {
  wchar_t op[32] = L"", src[MAX_PATH] = L"", dst[MAX_PATH] = L"", mode[16] = L"", q[300] = L"";
  ac_json_field(j, L"op", op, 32);
  if (!wcscmp(op, L"sort") && ac_json_field(j, L"src", src, MAX_PATH) && ac_json_field(j, L"dst", dst, MAX_PATH)) {
    wchar_t rs[MAX_PATH], rd[MAX_PATH];
    ac_json_field(j, L"mode", mode, 16);
    af_known_folders();
    /* короткое имя — из списка известных; полный путь — как назвал пользователь.
       обе папки должны уже быть. новую для штампа не выдумываем. */
    if (!af_resolve(src, rs, FALSE) || !af_is_dir(rs) || !af_trusted(rs) || !af_resolve(dst, rd, FALSE) ||
        !af_is_dir(rd) || !af_trusted(rd)) {
      ac_push(1, L"Не нашёл обе папки. Напишите, откуда сканы и куда класть, как в проводнике. Обе должны уже быть.");
      ac_redraw();
      return;
    }
    /* перемещать — только если пользователь так и написал */
    BOOL move = !wcscmp(mode, L"move") && (wcsstr(g_acCmdText, L"перемест") || wcsstr(g_acCmdText, L"перенес"));
    ac_sort_start(rs, rd, move);
    return;
  }
  if (!wcscmp(op, L"ask") && ac_json_field(j, L"question", q, 300)) {
    ac_push(1, q);
    return;
  }
  ac_files_run(j);
}

static void ac_relayout(void) {
  if (!g_acWnd) return;
  RECT rc;
  GetClientRect(g_acWnd, &rc);
  ac_layout(rc.right, rc.bottom);
}

static IWICImagingFactory *ac_factory(void) {
  IWICImagingFactory *f = NULL;
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  return f;
}

/* картинка по большей стороне не больше maxSide, 32 бита BGRA */
static IWICBitmapSource *ac_scaled(IWICImagingFactory *f, IWICBitmapSource *src, UINT maxSide, UINT *ow, UINT *oh) {
  UINT w = 0, h = 0;
  if (FAILED(src->lpVtbl->GetSize(src, &w, &h)) || !w || !h) return NULL;
  if (w > maxSide || h > maxSide) {
    if (w >= h) {
      h = (UINT)((UINT64)h * maxSide / w);
      w = maxSide;
    } else {
      w = (UINT)((UINT64)w * maxSide / h);
      h = maxSide;
    }
  }
  if (!w) w = 1;
  if (!h) h = 1;
  IWICBitmapScaler *sc = NULL;
  IWICFormatConverter *cv = NULL;
  if (FAILED(f->lpVtbl->CreateBitmapScaler(f, &sc))) return NULL;
  sc->lpVtbl->Initialize(sc, src, w, h, WICBitmapInterpolationModeFant);
  if (FAILED(f->lpVtbl->CreateFormatConverter(f, &cv))) {
    sc->lpVtbl->Release(sc);
    return NULL;
  }
  cv->lpVtbl->Initialize(cv, (IWICBitmapSource *)sc, &kAcBgra, WICBitmapDitherTypeNone, NULL, 0.0,
                         WICBitmapPaletteTypeCustom);
  sc->lpVtbl->Release(sc);
  *ow = w;
  *oh = h;
  return (IWICBitmapSource *)cv;
}

static BOOL ac_png(IWICImagingFactory *f, IWICBitmapSource *src, UINT w, UINT h, BYTE **out, DWORD *outLen) {
  IStream *st = NULL;
  IWICBitmapEncoder *enc = NULL;
  IWICBitmapFrameEncode *fr = NULL;
  IPropertyBag2 *pb = NULL;
  BOOL ok = FALSE;
  if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &st))) return FALSE;
  if (SUCCEEDED(f->lpVtbl->CreateEncoder(f, &kAcPngFormat, NULL, &enc)) &&
      SUCCEEDED(enc->lpVtbl->Initialize(enc, st, WICBitmapEncoderNoCache)) &&
      SUCCEEDED(enc->lpVtbl->CreateNewFrame(enc, &fr, &pb)) && SUCCEEDED(fr->lpVtbl->Initialize(fr, pb)) &&
      SUCCEEDED(fr->lpVtbl->SetSize(fr, w, h))) {
    WICPixelFormatGUID pf = kAcBgra;
    if (SUCCEEDED(fr->lpVtbl->SetPixelFormat(fr, &pf)) && SUCCEEDED(fr->lpVtbl->WriteSource(fr, src, NULL)) &&
        SUCCEEDED(fr->lpVtbl->Commit(fr)) && SUCCEEDED(enc->lpVtbl->Commit(enc))) {
      LARGE_INTEGER zero = {{0, 0}};
      ULARGE_INTEGER pos = {{0, 0}};
      HGLOBAL g = NULL;
      st->lpVtbl->Seek(st, zero, STREAM_SEEK_CUR, &pos); /* конец записанного */
      if (SUCCEEDED(GetHGlobalFromStream(st, &g))) {
        BYTE *lk = (BYTE *)GlobalLock(g);
        BYTE *p = (BYTE *)malloc((size_t)pos.QuadPart);
        if (p && lk) {
          memcpy(p, lk, (size_t)pos.QuadPart);
          *out = p;
          *outLen = (DWORD)pos.QuadPart;
          ok = TRUE;
        } else
          free(p);
        GlobalUnlock(g);
      }
    }
  }
  if (pb) pb->lpVtbl->Release(pb);
  if (fr) fr->lpVtbl->Release(fr);
  if (enc) enc->lpVtbl->Release(enc);
  st->lpVtbl->Release(st);
  return ok;
}

/* картинка прикрепляется к следующему вопросу: PNG до 768 px и миниатюра 80 px */
static void ac_take(IWICImagingFactory *f, IWICBitmapSource *src) {
  if (!ai_vision_ready()) {
    ac_status(L"Картинки читает модель «С картинками». Скачайте её на вкладке «ИИ».");
    ac_open_settings();
    return;
  }
  UINT w = 0, h = 0, tw = 0, th = 0;
  IWICBitmapSource *big = ac_scaled(f, src, 768, &w, &h);
  BYTE *png = NULL;
  DWORD pl = 0;
  if (!big || !ac_png(f, big, w, h, &png, &pl)) {
    if (big) big->lpVtbl->Release(big);
    ac_status(L"Не вышло прочитать картинку");
    return;
  }
  HBITMAP hb = NULL;
  IWICBitmapSource *small = ac_scaled(f, big, 80, &tw, &th);
  if (small) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)tw;
    bi.bmiHeader.biHeight = -(LONG)th;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HDC dc = GetDC(NULL);
    hb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (hb && bits) small->lpVtbl->CopyPixels(small, NULL, tw * 4, tw * 4 * th, (BYTE *)bits);
    small->lpVtbl->Release(small);
  }
  big->lpVtbl->Release(big);
  free(g_acImg);
  g_acImg = png;
  g_acImgLen = pl;
  if (g_acThumbBmp) DeleteObject(g_acThumbBmp);
  g_acThumbBmp = hb;
  SendMessageW(g_acThumb, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hb);
  ac_relayout();
  ac_status(L"Картинка прикреплена к следующему вопросу. Клик по миниатюре — убрать.");
}

static void ac_drop_img(void) {
  free(g_acImg);
  g_acImg = NULL;
  g_acImgLen = 0;
  ac_relayout();
  ac_status(L"Картинка убрана");
}

static void ac_from_file(const wchar_t *path) {
  IWICImagingFactory *f = ac_factory();
  IWICBitmapDecoder *dec = NULL;
  if (!f || FAILED(f->lpVtbl->CreateDecoderFromFilename(f, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) {
    ac_status(L"Это не картинка (нужны PNG, JPG, BMP, TIFF)");
  } else {
    IWICBitmapFrameDecode *fr = NULL;
    if (SUCCEEDED(dec->lpVtbl->GetFrame(dec, 0, &fr))) ac_take(f, (IWICBitmapSource *)fr);
    if (fr) fr->lpVtbl->Release(fr);
  }
  if (dec) dec->lpVtbl->Release(dec);
  if (f) f->lpVtbl->Release(f);
}

/* Ctrl+V: картинка из буфера (скриншот) */
static void ac_paste_img(void) {
  if (!OpenClipboard(g_acWnd)) return;
  HBITMAP clip = (HBITMAP)GetClipboardData(CF_BITMAP);
  IWICImagingFactory *f = clip ? ac_factory() : NULL;
  IWICBitmap *bmp = NULL;
  if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromHBITMAP(f, clip, NULL, WICBitmapUseAlpha, &bmp)))
    ac_take(f, (IWICBitmapSource *)bmp);
  if (bmp) bmp->lpVtbl->Release(bmp);
  if (f) f->lpVtbl->Release(f);
  CloseClipboard();
}

/* «С экрана»: область мышью, как в «Распознать чертёж» */
static void ac_shot_done(RECT r) {
  ShowWindow(g_acWnd, SW_SHOWNORMAL);
  int w = r.right - r.left, h = r.bottom - r.top;
  if (w < 20 || h < 20) {
    ac_status(L"Выделено слишком мало — обведите картинку целиком");
    return;
  }
  HDC s = GetDC(NULL);
  HDC m = CreateCompatibleDC(s);
  HBITMAP hb = CreateCompatibleBitmap(s, w, h);
  HGDIOBJ old = SelectObject(m, hb);
  BitBlt(m, 0, 0, w, h, s, r.left, r.top, SRCCOPY);
  SelectObject(m, old);
  DeleteDC(m);
  ReleaseDC(NULL, s);
  IWICImagingFactory *f = ac_factory();
  IWICBitmap *bmp = NULL;
  if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromHBITMAP(f, hb, NULL, WICBitmapUseAlpha, &bmp)))
    ac_take(f, (IWICBitmapSource *)bmp);
  if (bmp) bmp->lpVtbl->Release(bmp);
  if (f) f->lpVtbl->Release(f);
  DeleteObject(hb);
}

static void ac_shot(void) {
  if (!ai_vision_ready()) {
    ac_status(L"Картинки читает модель «С картинками». Скачайте её на вкладке «ИИ».");
    ac_open_settings();
    return;
  }
  ShowWindow(g_acWnd, SW_HIDE);
  Sleep(150);
  g_pickDone = ac_shot_done;
  start_ocr_pick();
}

static void ac_result(AiResult *r) {
  if (!r) return;
  if (r->id != g_acId) {
    ai_result_free(r);
    return;
  }
  g_acId = 0;
  ac_busy(FALSE);
  BOOL cmd = g_acCmdWait;
  g_acCmdWait = FALSE;
  if (r->status == AI_OK && cmd) {
    ac_cmd_run(r->text);
    ac_redraw();
  } else if (r->status == AI_OK) {
    ac_push(1, r->text);
    ac_redraw();
    ac_status(L"");
  } else {
    ac_unsend();
    ac_redraw();
    ac_status(r->status == AI_CANCELLED ? L"Остановлено, вопрос вернулся в поле ввода"
                                        : (r->text && r->text[0] ? r->text : ai_status_text(r->status)));
  }
  ai_result_free(r);
}

static void ac_clear(void) {
  if (g_acPlanOn) af_clear();
  ac_plan_ui(FALSE);
  if (g_acId) ai_cancel(g_acId);
  g_acId = 0;
  ac_busy(FALSE);
  ac_clear_turns();
  free(g_acImg);
  g_acImg = NULL;
  g_acImgLen = 0;
  ac_relayout();
  SetWindowTextW(g_acLog, L"");
  ac_status(L"");
}

static void ac_copy_answer(void) {
  for (int i = g_acN - 1; i >= 0; i--)
    if (g_acTurns[i].assistant) {
      ac_clip(g_acTurns[i].text);
      ac_status(L"Ответ скопирован");
      return;
    }
  ac_status(L"Ответа ещё нет");
}

/* поле ввода: Enter — отправить, Shift+Enter — новая строка; Ctrl+A, Esc */
static LRESULT CALLBACK AcEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (msg == WM_KEYDOWN && wParam == VK_RETURN && !shift) {
    if (hwnd == g_acIn) PostMessageW(g_acWnd, WM_AC_SEND, 0, 0);
    return 0;
  }
  if (msg == WM_CHAR && wParam == L'\r' && !shift) return 0;
  if (msg == WM_KEYDOWN && wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) && hwnd == g_acIn &&
      IsClipboardFormatAvailable(CF_BITMAP) && !IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    ac_paste_img();
    return 0;
  }
  if (msg == WM_CHAR && wParam == 1) {
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    ShowWindow(g_acWnd, SW_HIDE);
    return 0;
  }
  return CallWindowProcW(g_acEditOld, hwnd, msg, wParam, lParam);
}

static void ac_layout(int W, int H) {
  int pad = 12, gap = 8, btnH = 30, inH = 72, statH = 20;
  int btnY = H - pad - btnH;
  int inY = btnY - gap - inH;
  int thumbH = g_acImg ? 84 : 0;
  int cmdY = inY - 4 - 22 - thumbH;
  int stY = cmdY - 4 - statH;
  int logY = 46;
  int logH = stY - 6 - logY;
  if (logH < 40) logH = 40;
  MoveWindow(g_acTitle, pad, 10, W - 2 * pad - 70, 24, TRUE);
  MoveWindow(GetDlgItem(g_acWnd, ID_PANEL_MIN), W - pad - 66, 8, 30, 26, TRUE);
  MoveWindow(GetDlgItem(g_acWnd, ID_PANEL_CLOSE), W - pad - 30, 8, 30, 26, TRUE);
  MoveWindow(g_acLog, pad, logY, W - 2 * pad, logH, TRUE);
  MoveWindow(g_acStat, pad, stY, W - 2 * pad, statH, TRUE);
  MoveWindow(g_acShot, pad, cmdY, 120, 22, TRUE);
  MoveWindow(g_acThumb, pad, inY - 84, 80, 80, TRUE);
  ShowWindow(g_acThumb, g_acImg ? SW_SHOW : SW_HIDE);
  MoveWindow(g_acIn, pad, inY, W - 2 * pad, inH, TRUE);
  HWND row[6] = {g_acSend, g_acStop, g_acClear, g_acCopy, g_acLearn, g_acSet};
  int bw = (W - 2 * pad - 5 * gap) / 6;
  if (bw < 64) bw = 64;
  for (int i = 0; i < 6; i++) MoveWindow(row[i], pad + i * (bw + gap), btnY, bw, btnH, TRUE);
}

static LRESULT CALLBACK AcProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_SIZE:
    ac_layout(LOWORD(lParam), HIWORD(lParam));
    return 0;
  case WM_DROPFILES: {
    wchar_t p[MAX_PATH] = L"";
    HDROP hd = (HDROP)wParam;
    if (DragQueryFileW(hd, 0, p, MAX_PATH)) ac_from_file(p);
    DragFinish(hd);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_AC_THUMB && HIWORD(wParam) == STN_CLICKED) {
      ac_drop_img();
      return 0;
    }
    if (HIWORD(wParam) != BN_CLICKED) break;
    switch (LOWORD(wParam)) {
    case ID_AC_SHOT:
      ac_shot();
      break;
    case ID_PANEL_CLOSE:
      ShowWindow(hwnd, SW_HIDE);
      break;
    case ID_PANEL_MIN:
      ShowWindow(hwnd, SW_MINIMIZE);
      break;
    case ID_AC_SEND:
      if (g_acPlanOn) ac_files_apply();
      else ac_send();
      break;
    case ID_AC_STOP:
      if (g_acPlanOn) ac_files_cancel();
      else if (g_acId) ai_cancel(g_acId);
      break;
    case ID_AC_CLEAR:
      ac_clear();
      break;
    case ID_AC_COPY:
      ac_copy_answer();
      break;
    case ID_AC_LEARN: {
      if (g_acId) {
        ac_status(L"Сначала дождитесь ответа или нажмите «Стоп» — потом запомню.");
        break;
      }
      int ln = GetWindowTextLengthW(g_acIn);
      wchar_t *tx = (wchar_t *)malloc(((size_t)ln + 1) * sizeof(wchar_t));
      if (tx) {
        GetWindowTextW(g_acIn, tx, ln + 1);
        wchar_t *p = tx;
        while (*p == L' ' || *p == L'\r' || *p == L'\n' || *p == L'\t') p++;
        size_t pn = wcslen(p);
        while (pn && (p[pn - 1] == L' ' || p[pn - 1] == L'\r' || p[pn - 1] == L'\n' || p[pn - 1] == L'\t')) p[--pn] = 0;
        ac_remember(p, FALSE);
        if (pn) SetWindowTextW(g_acIn, L"");
        free(tx);
      }
      break;
    }
    case ID_AC_SET:
      ac_open_settings();
      break;
    }
    return 0;
  case WM_AC_SEND:
    ac_send();
    return 0;
  case WM_AI_CHAT:
    ac_result((AiResult *)lParam);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ac_create(void) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = AcProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = L"CursorPadAiChat";
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  RegisterClassExW(&wc);
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int w = 760, h = 640;
  if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
  if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
  g_acWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadAiChat", L"Спросить ИИ — CursorPad",
                            WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                            wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                            NULL, NULL, g_inst, NULL);
  if (!g_acWnd) return;
  round_corners(g_acWnd);
  mk_btn(g_acWnd, L"×", ID_PANEL_CLOSE);
  mk_btn(g_acWnd, L"–", ID_PANEL_MIN);
  g_acTitle = CreateWindowExW(0, L"STATIC", L"Спросить ИИ · всё на этом компьютере, без интернета", WS_CHILD | WS_VISIBLE | SS_LEFT,
                              0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_TITLE, g_inst, NULL);
  g_acLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                            0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_LOG, g_inst, NULL);
  g_acIn = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                           0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_IN, g_inst, NULL);
  g_acStat = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, g_acWnd,
                             (HMENU)(INT_PTR)ID_AC_STAT, g_inst, NULL);
  g_acSend = mk_btn(g_acWnd, L"Отправить", ID_AC_SEND);
  g_acStop = mk_btn(g_acWnd, L"Стоп", ID_AC_STOP);
  g_acClear = mk_btn(g_acWnd, L"Очистить", ID_AC_CLEAR);
  g_acCopy = mk_btn(g_acWnd, L"Копировать", ID_AC_COPY);
  g_acLearn = mk_btn(g_acWnd, L"Запомнить", ID_AC_LEARN);
  g_acSet = mk_btn(g_acWnd, L"Настройки", ID_AC_SET);
  g_acShot = mk_btn(g_acWnd, L"С экрана", ID_AC_SHOT);
  g_acThumb = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_BITMAP | SS_NOTIFY, 0, 0, 10, 10, g_acWnd,
                              (HMENU)(INT_PTR)ID_AC_THUMB, g_inst, NULL);
  DragAcceptFiles(g_acWnd, TRUE);
  for (HWND c = GetWindow(g_acWnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    if (c != g_acSend && c != g_acStop && c != g_acClear && c != g_acCopy && c != g_acLearn && c != g_acSet &&
        c != g_acShot) SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_acLog, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  SendMessageW(g_acIn, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  g_acEditOld = (WNDPROC)SetWindowLongPtrW(g_acIn, GWLP_WNDPROC, (LONG_PTR)AcEditProc);
  SetWindowLongPtrW(g_acLog, GWLP_WNDPROC, (LONG_PTR)AcEditProc);
  RECT rc;
  GetClientRect(g_acWnd, &rc);
  ac_layout(rc.right, rc.bottom);
}

/* пункт «Спросить ИИ» в «Ещё» */
static void aichat_show(void) {
  if (!g_acWnd) ac_create();
  if (!g_acWnd) return;
  ShowWindow(g_acWnd, IsIconic(g_acWnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g_acWnd);
  SetFocus(g_acIn);
  if (!g_acId) {
    if (!ai_ready()) ac_status(ai_status_text(g_aiOn ? AI_NO_MODEL : AI_OFF));
    else ac_status(L"Enter — отправить, Shift+Enter — новая строка. «запомни: …» пишет в тетрадь.");
  }
}

/* поиск с вопросом (мини-ИИ): открыть чат и сразу спросить */
static void aichat_ask(const wchar_t *q) {
  aichat_show();
  if (!g_acWnd) return;
  SetWindowTextW(g_acIn, q);
  ac_send();
}

#include "ai_files.c" /* команды для файлов: копировать, переместить, переименовать, создать папку */
