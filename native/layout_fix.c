/* ---- Исправить раскладку: «ghbdtn» → «привет» по клавише Pause -------------

   Напечатали не в той раскладке — жмёте Pause (не отпуская ничего), и:

     • если текст выделен — переделывается выделенное;
     • если нет — последнее слово перед курсором (и пробелы за ним).

   Переделка — по месту клавиш: q↔й, [↔х, ,↔б, /↔. и так далее, в обе
   стороны; в какую — решают буквы: латинских больше — в русскую, русских —
   в латинскую. Раскладка окна тоже переключается, дальше печатать можно
   сразу. Нажать Pause ещё раз — вернётся как было.

   Как это сделано. Программа не следит за клавиатурой постоянно (так
   делают «переключатели» вроде Punto, и такие программы антивирусы любят
   подозревать). Вместо этого в момент нажатия она сама «нажимает» в чужом
   окне обычные клавиши:
     1. Ctrl+C — есть выделение? Тогда берём его.
     2. Нет — Shift+Home и Ctrl+C: текст от начала строки до курсора, из
        него — последнее слово; Shift+→ столько раз, сколько букв до слова, —
        выделено ровно слово.
     3. Вместо выделенного «печатается» исправленное (как будто набрали
        руками); многострочное — вставляется через буфер.
   Что было в буфере обмена до этого — возвращается на место, в «5 последних
   копирований» эти копии не попадают. В окнах самого CursorPad то же самое
   делается напрямую, без нажатий. */

#define HOTKEY_LAYOUT 6
#define TIMER_LF_RESTORE 34 /* вставили через буфер — вернуть прежний буфер чуть позже */

static UINT g_lfVk;               /* на какой клавише (Pause, если свободна) */
static wchar_t g_lfKeyName[24];
static ULONGLONG g_lfQuietUntil;  /* до этого времени перемены буфера — наши */

typedef struct {
  UINT fmt;
  HGLOBAL mem;
} LfClip;
static LfClip *g_lfSaved;
static int g_lfSavedN;
static BOOL g_lfSavedOk;

/* по месту клавиш: английская ↔ русская (ЙЦУКЕН) */
static const wchar_t kLfEn[] = L"`qwertyuiop[]asdfghjkl;'zxcvbnm,./"
                               L"~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?"
                               L"@#$^&|";
static const wchar_t kLfRu[] = L"ёйцукенгшщзхъфывапролджэячсмитьбю."
                               L"ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ,"
                               L"\"№;:?/";

static BOOL lf_quiet(void) { return GetTickCount64() < g_lfQuietUntil; }
static void lf_hush(DWORD ms) {
  ULONGLONG t = GetTickCount64() + ms;
  if (t > g_lfQuietUntil) g_lfQuietUntil = t;
}

static BOOL lf_is_lat(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
static BOOL lf_is_cyr(wchar_t c) { return (c >= 0x0410 && c <= 0x044F) || c == 0x0401 || c == 0x0451; }

/* в какую сторону: 1 — в русскую, 0 — в английскую, -1 — букв нет */
static int lf_direction(const wchar_t *s, int n) {
  int lat = 0, cyr = 0;
  for (int i = 0; i < n; i++) {
    if (lf_is_lat(s[i])) lat++;
    else if (lf_is_cyr(s[i])) cyr++;
  }
  if (!lat && !cyr) {
    /* одни знаки: «[f» не бывает, а «,.;» — это «бюж» */
    for (int i = 0; i < n; i++)
      if (wcschr(kLfEn, s[i]) && s[i] != L' ') return 1;
    return -1;
  }
  return lat >= cyr ? 1 : 0;
}

static void lf_convert(wchar_t *s, int n, int toRu) {
  const wchar_t *from = toRu ? kLfEn : kLfRu, *to = toRu ? kLfRu : kLfEn;
  for (int i = 0; i < n; i++) {
    const wchar_t *p = s[i] ? wcschr(from, s[i]) : NULL;
    if (p) s[i] = to[p - from];
  }
}

/* ---- буфер обмена: сохранить всё, что в нём было, и вернуть -------------- */

static void lf_wait(DWORD ms);
static BOOL lf_open_clip(void) {
  for (int i = 0; i < 20; i++) {
    if (OpenClipboard(g_hwnd)) return TRUE;
    lf_wait(15);
  }
  return FALSE;
}

static void lf_saved_free(void) {
  for (int i = 0; i < g_lfSavedN; i++)
    if (g_lfSaved[i].mem) GlobalFree(g_lfSaved[i].mem);
  free(g_lfSaved);
  g_lfSaved = NULL;
  g_lfSavedN = 0;
  g_lfSavedOk = FALSE;
}

static BOOL lf_clip_save(void) {
  lf_saved_free();
  if (!lf_open_clip()) return FALSE;
  int cap = CountClipboardFormats() + 1;
  g_lfSaved = (LfClip *)calloc((size_t)cap, sizeof(LfClip));
  SIZE_T total = 0;
  for (UINT f = 0; g_lfSaved && (f = EnumClipboardFormats(f)) != 0 && g_lfSavedN < cap;) {
    /* у этих не память, а картинки и метафайлы Windows — их она сама
       соберёт заново из соседних (CF_DIB и т. п.) */
    if (f == CF_BITMAP || f == CF_METAFILEPICT || f == CF_PALETTE || f == CF_ENHMETAFILE ||
        f == CF_OWNERDISPLAY || f == CF_DSPBITMAP || f == CF_DSPMETAFILEPICT || f == CF_DSPENHMETAFILE)
      continue;
    HANDLE h = GetClipboardData(f);
    if (!h) continue;
    SIZE_T sz = GlobalSize(h);
    if (!sz || total + sz > 64u * 1024u * 1024u) continue; /* огромное — не держим */
    void *src = GlobalLock(h);
    if (!src) continue;
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, sz);
    void *dst = copy ? GlobalLock(copy) : NULL;
    if (dst) {
      memcpy(dst, src, sz);
      GlobalUnlock(copy);
      g_lfSaved[g_lfSavedN].fmt = f;
      g_lfSaved[g_lfSavedN].mem = copy;
      g_lfSavedN++;
      total += sz;
    } else if (copy) {
      GlobalFree(copy);
    }
    GlobalUnlock(h);
  }
  CloseClipboard();
  g_lfSavedOk = TRUE;
  return TRUE;
}

static void lf_clip_restore(void) {
  if (!g_lfSavedOk) return;
  lf_hush(1500);
  if (lf_open_clip()) {
    EmptyClipboard();
    for (int i = 0; i < g_lfSavedN; i++)
      if (SetClipboardData(g_lfSaved[i].fmt, g_lfSaved[i].mem)) g_lfSaved[i].mem = NULL; /* теперь её */
    CloseClipboard();
  }
  lf_saved_free();
}

static BOOL lf_clip_put(const wchar_t *text) {
  size_t n = wcslen(text);
  HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, (n + 1) * sizeof(wchar_t));
  wchar_t *p = m ? (wchar_t *)GlobalLock(m) : NULL;
  if (!p) {
    if (m) GlobalFree(m);
    return FALSE;
  }
  memcpy(p, text, (n + 1) * sizeof(wchar_t));
  GlobalUnlock(m);
  lf_hush(1500);
  if (!lf_open_clip()) {
    GlobalFree(m);
    return FALSE;
  }
  EmptyClipboard();
  BOOL ok = SetClipboardData(CF_UNICODETEXT, m) != NULL;
  CloseClipboard();
  if (!ok) GlobalFree(m);
  return ok;
}

/* ---- нажатия в чужом окне ----------------------------------------------- */

static void lf_key(INPUT *in, int *n, WORD vk, BOOL up) {
  memset(&in[*n], 0, sizeof(INPUT));
  in[*n].type = INPUT_KEYBOARD;
  in[*n].ki.wVk = vk;
  if (up) in[*n].ki.dwFlags = KEYEVENTF_KEYUP;
  /* стрелки и Home — «расширенные» клавиши; без флага часть программ
     принимает их за цифры на боковой клавиатуре */
  if (vk == VK_HOME || vk == VK_LEFT || vk == VK_RIGHT) in[*n].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  (*n)++;
}

static void lf_press(WORD mod, WORD vk) {
  INPUT in[4];
  int n = 0;
  if (mod) lf_key(in, &n, mod, FALSE);
  lf_key(in, &n, vk, FALSE);
  lf_key(in, &n, vk, TRUE);
  if (mod) lf_key(in, &n, mod, TRUE);
  SendInput((UINT)n, in, sizeof(INPUT));
}

/* Пауза, но с ответами на присланное: копируя, чужая программа сперва
   сообщает прежнему хозяину буфера «ваше больше не действует» — и ждёт
   ответа. Хозяин часто мы (вернули человеку его буфер в прошлый раз); спи мы
   просто так, она бы стояла, а мы решили бы, что копировать нечего. */
static void lf_wait(DWORD ms) {
  DWORD t0 = GetTickCount();
  for (;;) {
    MSG m;
    PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    DWORD el = GetTickCount() - t0;
    if (el >= ms) break;
    MsgWaitForMultipleObjects(0, NULL, FALSE, ms - el, QS_SENDMESSAGE);
  }
}

/* Ctrl+C в чужом окне и ждём, пока буфер сменится; текст — в *out (malloc) */
static BOOL lf_copy(wchar_t **out, int waitMs) {
  *out = NULL;
  /* буфер сейчас открыт кем-то (менеджер буфера, удалённый стол, сама
     Windows после прошлой перемены) — чужой Ctrl+C молча не сработал бы */
  for (int i = 0; i < 40 && GetOpenClipboardWindow(); i++) lf_wait(10);
  DWORD seq = GetClipboardSequenceNumber();
  lf_hush((DWORD)waitMs + 1500);
  lf_press(VK_CONTROL, 'C');
  DWORD t0 = GetTickCount();
  while (GetClipboardSequenceNumber() == seq) {
    if (GetTickCount() - t0 > (DWORD)waitMs) return FALSE;
    lf_wait(10);
  }
  lf_wait(20); /* программа могла ещё дописывать форматы */
  if (!lf_open_clip()) return FALSE;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  const wchar_t *p = h ? (const wchar_t *)GlobalLock(h) : NULL;
  if (p) {
    SIZE_T cap = GlobalSize(h) / sizeof(wchar_t);
    size_t n = 0;
    while (n < cap && p[n]) n++;
    *out = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (*out) {
      memcpy(*out, p, n * sizeof(wchar_t));
      (*out)[n] = 0;
    }
    GlobalUnlock(h);
  }
  CloseClipboard();
  return *out != NULL;
}

/* Shift+→ count раз: выделение от начала строки сжимается к курсору.
   Край, где курсор стоял, при этом не двигается — так во всех программах,
   в отличие от просто «→», которое кто куда ставит */
static void lf_shrink(int count) {
  if (count <= 0) return;
  INPUT *in = (INPUT *)calloc((size_t)count * 2 + 2, sizeof(INPUT));
  if (!in) return;
  int k = 0;
  lf_key(in, &k, VK_SHIFT, FALSE);
  for (int i = 0; i < count; i++) {
    lf_key(in, &k, VK_RIGHT, FALSE);
    lf_key(in, &k, VK_RIGHT, TRUE);
  }
  lf_key(in, &k, VK_SHIFT, TRUE);
  SendInput((UINT)k, in, sizeof(INPUT));
  free(in);
}

/* «напечатать» текст — как будто набрали руками (раскладка не важна) */
static void lf_type(const wchar_t *s) {
  size_t n = wcslen(s);
  INPUT *in = (INPUT *)calloc(n * 2 + 1, sizeof(INPUT));
  if (!in) return;
  int k = 0;
  for (size_t i = 0; i < n; i++) {
    for (int up = 0; up < 2; up++) {
      in[k].type = INPUT_KEYBOARD;
      in[k].ki.wScan = s[i];
      in[k].ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
      k++;
    }
  }
  SendInput((UINT)k, in, sizeof(INPUT));
  free(in);
}

/* найти раскладку нужного языка среди установленных */
static HKL lf_find_hkl(BOOL ru) {
  HKL list[32];
  int n = GetKeyboardLayoutList(32, list);
  WORD want = ru ? LANG_RUSSIAN : LANG_ENGLISH;
  for (int i = 0; i < n; i++)
    if (PRIMARYLANGID(LOWORD((ULONG_PTR)list[i])) == want) return list[i];
  return NULL;
}

static void lf_switch_layout(HWND target, BOOL ru) {
  HKL k = lf_find_hkl(ru);
  if (!k || !target) return;
  PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)k);
}

/* последнее слово в строке s[0..n): начало слова, n — конец с пробелами */
static int lf_last_word(const wchar_t *s, int n) {
  int e = n;
  while (e > 0 && (s[e - 1] == L' ' || s[e - 1] == 0xA0)) e--;
  int b = e;
  while (b > 0 && s[b - 1] != L' ' && s[b - 1] != 0xA0 && s[b - 1] != L'\t' && s[b - 1] != L'\n' &&
         s[b - 1] != L'\r')
    b--;
  return b == e ? -1 : b;
}

static void lf_done_status(const wchar_t *was, const wchar_t *now) {
  wchar_t m[160];
  wchar_t a[40], b[40];
  lstrcpynW(a, was, 40);
  lstrcpynW(b, now, 40);
  for (wchar_t *c = a; *c; c++)
    if (*c == L'\r' || *c == L'\n' || *c == L'\t') *c = L' ';
  for (wchar_t *c = b; *c; c++)
    if (*c == L'\r' || *c == L'\n' || *c == L'\t') *c = L' ';
  _snwprintf(m, 160, L"Раскладка: «%s» → «%s»", a, b);
  m[159] = 0;
  show_status(m);
}

/* ---- в своих окнах (блокнот, поиск, окна «Ещё») — напрямую -------------- */
static void lf_fix_own(void) {
  HWND f = GetFocus();
  wchar_t cls[32] = L"";
  if (f) GetClassNameW(f, cls, 32);
  if (!f || _wcsicmp(cls, L"Edit")) {
    show_status(L"Раскладка: поставьте курсор в поле с текстом");
    return;
  }
  if (GetWindowLongW(f, GWL_STYLE) & ES_READONLY) return;
  int len = GetWindowTextLengthW(f);
  wchar_t *all = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
  if (!all) return;
  GetWindowTextW(f, all, len + 1);
  DWORD s = 0, e = 0;
  SendMessageW(f, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
  if (e > (DWORD)len) e = (DWORD)len;
  if (s > e) s = e;
  if (s == e) { /* без выделения: последнее слово в строке перед курсором */
    int ls = (int)e;
    while (ls > 0 && all[ls - 1] != L'\n') ls--;
    int b = lf_last_word(all + ls, (int)e - ls);
    if (b < 0) {
      free(all);
      show_status(L"Раскладка: перед курсором нет слова");
      return;
    }
    s = (DWORD)(ls + b);
  }
  int n = (int)(e - s);
  int dir = lf_direction(all + s, n);
  if (dir < 0) {
    free(all);
    return;
  }
  wchar_t *was = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
  if (!was) {
    free(all);
    return;
  }
  memcpy(was, all + s, (size_t)n * sizeof(wchar_t));
  was[n] = 0;
  wchar_t *fix = _wcsdup(was);
  if (fix) {
    lf_convert(fix, n, dir);
    SendMessageW(f, EM_SETSEL, s, e);
    SendMessageW(f, EM_REPLACESEL, TRUE, (LPARAM)fix);
    HKL k = lf_find_hkl(dir == 1);
    if (k) ActivateKeyboardLayout(k, 0);
    lf_done_status(was, fix);
    free(fix);
  }
  free(was);
  free(all);
  return;
}

/* ---- по клавише -------------------------------------------------------------- */
static void layout_fix(void) {
  HWND fg = GetForegroundWindow();
  if (!fg) return;
  DWORD pid = 0;
  DWORD tid = GetWindowThreadProcessId(fg, &pid);
  if (pid == GetCurrentProcessId()) {
    lf_fix_own();
    return;
  }
  /* зажатые Shift/Ctrl/Alt превратили бы наш Ctrl+C во что-то другое —
     ждём, пока отпустят (не дольше секунды) */
  static const int mods[] = {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN};
  for (int t = 0; t < 100; t++) {
    BOOL held = FALSE;
    for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); i++)
      if (GetAsyncKeyState(mods[i]) & 0x8000) held = TRUE;
    if (!held) break;
    if (t == 99) return;
    Sleep(10);
  }
  /* куда потом переключить раскладку — окно с курсором ввода */
  GUITHREADINFO gi;
  memset(&gi, 0, sizeof(gi));
  gi.cbSize = sizeof(gi);
  HWND focus = (GetGUIThreadInfo(tid, &gi) && gi.hwndFocus) ? gi.hwndFocus : fg;

  if (!lf_clip_save()) {
    show_status(L"Раскладка: буфер обмена занят, попробуйте ещё раз");
    return;
  }
  wchar_t *txt = NULL, *fix = NULL;
  int word = 0; /* сколько букв строки до слова — на столько сузить выделение */
  /* 1. выделенное */
  BOOL got = lf_copy(&txt, 300);
  /* редакторы кода без выделения копируют всю строку — она кончается
     переводом строки; такое за выделение не считаем */
  if (got && txt) {
    size_t l = wcslen(txt);
    if (!l || txt[l - 1] == L'\n') {
      free(txt);
      txt = NULL;
      got = FALSE;
    }
  }
  if (!got) {
    /* 2. последнее слово: от начала строки до курсора */
    lf_press(VK_SHIFT, VK_HOME);
    got = lf_copy(&txt, 500);
    if (!got) got = lf_copy(&txt, 700); /* ещё раз: буфер мог быть занят */
    /* ячейки таблицы (табуляции, переводы строк) или не поле ввода — не трогаем */
    BOOL line = got && txt && txt[0] && !wcschr(txt, L'\t') && !wcschr(txt, L'\n') && wcslen(txt) < 4000;
    if (!line) {
      free(txt);
      lf_clip_restore();
      show_status(L"Раскладка: не нашёл слово перед курсором — выделите его мышкой и нажмите ещё раз");
      return;
    }
    int n = (int)wcslen(txt);
    int b = lf_last_word(txt, n);
    if (b < 0 || lf_direction(txt + b, n - b) < 0) {
      lf_shrink(n); /* выделение назад — курсор, где был */
      free(txt);
      lf_clip_restore();
      show_status(L"Раскладка: перед курсором нет слова");
      return;
    }
    memmove(txt, txt + b, ((size_t)(n - b) + 1) * sizeof(wchar_t));
    word = b;
  }
  int n = (int)wcslen(txt);
  int dir = lf_direction(txt, n);
  if (dir < 0 || n > 20000) {
    free(txt);
    lf_clip_restore();
    return;
  }
  fix = _wcsdup(txt);
  if (!fix) {
    free(txt);
    lf_clip_restore();
    return;
  }
  lf_convert(fix, n, dir);
  if (word) lf_shrink(word); /* выделено от начала строки — оставить только слово */
  if (wcschr(fix, L'\n')) {
    /* несколько строк: «напечатанный» Enter в чате отправил бы сообщение —
       вставляем через буфер, а прежний буфер вернём через секунду */
    if (lf_clip_put(fix)) {
      lf_press(VK_CONTROL, 'V');
      SetTimer(g_hwnd, TIMER_LF_RESTORE, 900, NULL);
    } else {
      lf_clip_restore();
    }
  } else {
    lf_type(fix);
    lf_clip_restore();
  }
  lf_switch_layout(focus, dir == 1);
  lf_done_status(txt, fix);
  free(txt);
  free(fix);
}

static void layout_fix_timer(void) {
  KillTimer(g_hwnd, TIMER_LF_RESTORE);
  lf_clip_restore();
}

static const UINT kLfKeys[] = {VK_PAUSE, VK_SCROLL};
static const wchar_t *const kLfKeyNames[] = {L"Pause", L"Scroll Lock"};

static void layout_fix_register(HWND hwnd) {
  g_lfVk = 0;
  g_lfKeyName[0] = 0;
  for (int i = 0; i < 2; i++) {
    if (kLfKeys[i] == g_hotkeyVk) continue; /* её уже взяла «окно за курсором» */
    if (RegisterHotKey(hwnd, HOTKEY_LAYOUT, MOD_NOREPEAT, kLfKeys[i])) {
      g_lfVk = kLfKeys[i];
      lstrcpynW(g_lfKeyName, kLfKeyNames[i], 24);
      return;
    }
  }
}

/* из меню «Ещё»: подсказка — нажимать надо там, где печатали */
static void layout_fix_hint(void) {
  wchar_t m[400];
  if (!g_lfVk)
    _snwprintf(m, 400, L"Клавиша Pause занята другой программой — исправление раскладки выключено.");
  else
    _snwprintf(m, 400,
               L"Напечатали не в той раскладке («ghbdtn» вместо «привет»)?\n\n"
               L"Не стирайте — нажмите %s прямо там, где печатали:\n"
               L"• без выделения исправится последнее слово;\n"
               L"• выделенный текст исправится целиком.\n\n"
               L"Раскладка переключится сама. Нажать ещё раз — вернётся как было.",
               g_lfKeyName);
  m[399] = 0;
  MessageBoxW(g_hwnd, m, L"Исправить раскладку", MB_OK | MB_ICONINFORMATION);
}
