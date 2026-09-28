/* ---- Горячие клавиши: своя на каждую функцию ---------------------------------

   С 2026.09.23.72 клавиши не зашиты: Настройки → «Горячие клавиши…» —
   окно, где любую функцию можно пересадить на другую клавишу или сочетание
   (F6 → Ctrl+Alt+R) или оставить совсем без клавиши. Кнопки и меню
   программы при этом работают как раньше.

   Хранится в hotkeys.txt рядом с заметками, строка на функцию:
   «имя клавиша модификаторы» (коды Windows), клавиша 0 — без клавиши.
   Строки нет — как по умолчанию. «Как было» стирает файл.

   По умолчанию «закрепить» — F8, а если её держит другая программа —
   Pause, потом Scroll Lock; «исправить раскладку» — Pause, а если её взяла
   «закрепить» или другая программа — Scroll Lock (так было и раньше).
   Назначенную вручную клавишу так не подменяем: занята — функция остаётся
   без клавиши, и окно пишет, что занята.

   Пока окно ждёт нажатия, все наши клавиши сняты — иначе F6 не
   назначить: её перехватила бы сама «Выделить и прочитать». */

typedef struct {
  int id;               /* номер RegisterHotKey / WM_HOTKEY */
  const char *key;      /* имя в hotkeys.txt */
  const wchar_t *title; /* строка в окне */
  UINT vk, mods;        /* по умолчанию */
} HkFunc;

/* порядок — как enum HK_* в cursorpad.c */
static const HkFunc kHk[HK_COUNT] = {
    {HOTKEY_TOGGLE, "pin", L"Закрепить / следовать за курсором", VK_F8, 0},
    {HOTKEY_MIN, "min", L"Свернуть / показать окно", VK_F9, 0},
    {HOTKEY_SEARCH, "search", L"Искать скопированное", VK_F3, 0},
    {HOTKEY_OCR, "ocr", L"Выделить и прочитать текст с экрана", VK_F6, 0},
    {HOTKEY_CURSOR, "cursor", L"Следующий курсор", VK_F7, 0},
    {HOTKEY_LAYOUT, "layout", L"Исправить раскладку", VK_PAUSE, 0},
    {HOTKEY_SNIP_BASE, "lines", L"Вставить строку блокнота 1…9", '1', MOD_CONTROL},
};

static UINT g_hkVk[HK_COUNT], g_hkMods[HK_COUNT]; /* выбранное; vk 0 — без клавиши */
static BOOL g_hkOwn[HK_COUNT];                    /* выбрано вручную (есть в файле) */
static BOOL g_hkBusy[HK_COUNT];                   /* своя клавиша занята другой программой */
static BOOL g_hkLoaded;

/* ---- названия ------------------------------------------------------------------ */

static void hk_vk_name(UINT vk, wchar_t *out, int n) {
  out[0] = 0;
  if (vk >= VK_F1 && vk <= VK_F24) {
    _snwprintf(out, n, L"F%u", vk - VK_F1 + 1);
  } else if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
    _snwprintf(out, n, L"%c", (wchar_t)vk);
  } else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
    _snwprintf(out, n, L"Num %u", vk - VK_NUMPAD0);
  } else {
    static const struct {
      UINT vk;
      const wchar_t *name;
    } names[] = {{VK_PAUSE, L"Pause"},       {VK_SCROLL, L"Scroll Lock"}, {VK_INSERT, L"Insert"},
                 {VK_DELETE, L"Delete"},     {VK_HOME, L"Home"},          {VK_END, L"End"},
                 {VK_PRIOR, L"PgUp"},        {VK_NEXT, L"PgDn"},          {VK_LEFT, L"←"},
                 {VK_RIGHT, L"→"},           {VK_UP, L"↑"},               {VK_DOWN, L"↓"},
                 {VK_SPACE, L"Пробел"},      {VK_RETURN, L"Enter"},       {VK_TAB, L"Tab"},
                 {VK_BACK, L"Backspace"},    {VK_ESCAPE, L"Esc"},         {VK_SNAPSHOT, L"Print Screen"},
                 {VK_APPS, L"Меню"},         {VK_MULTIPLY, L"Num *"},     {VK_ADD, L"Num +"},
                 {VK_SUBTRACT, L"Num −"},    {VK_DIVIDE, L"Num /"},       {VK_DECIMAL, L"Num ."}};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
      if (names[i].vk == vk) {
        lstrcpynW(out, names[i].name, n);
        return;
      }
    /* знаки (; ' [ ] …) — как их называет раскладка */
    UINT sc = MapVirtualKeyW(vk, 0 /* MAPVK_VK_TO_VSC */);
    if (!sc || GetKeyNameTextW((LONG)(sc << 16), out, n) <= 0) _snwprintf(out, n, L"клавиша %u", vk);
  }
  out[n - 1] = 0;
}

static void hk_combo_name(int f, UINT vk, UINT mods, wchar_t *out, int n) {
  wchar_t k[32];
  if (f == HK_SNIP) lstrcpynW(k, L"1…9", 32);
  else hk_vk_name(vk, k, 32);
  _snwprintf(out, n, L"%s%s%s%s%s", (mods & MOD_CONTROL) ? L"Ctrl+" : L"", (mods & MOD_ALT) ? L"Alt+" : L"",
             (mods & MOD_SHIFT) ? L"Shift+" : L"", (mods & MOD_WIN) ? L"Win+" : L"", k);
  out[n - 1] = 0;
}

/* «Свернуть (F9)» — или просто «Свернуть», если клавиши нет */
static void hk_with(const wchar_t *text, int f, wchar_t *out, int n) {
  if (g_hkName[f][0]) _snwprintf(out, n, L"%s (%s)", text, g_hkName[f]);
  else lstrcpynW(out, text, n);
  out[n - 1] = 0;
}

/* ---- файл ---------------------------------------------------------------------- */

static void hk_path(wchar_t *out) { _snwprintf(out, MAX_PATH, L"%s\\hotkeys.txt", g_dataDir); }

static void hk_defaults(void) {
  for (int f = 0; f < HK_COUNT; f++) {
    g_hkVk[f] = kHk[f].vk;
    g_hkMods[f] = kHk[f].mods;
    g_hkOwn[f] = FALSE;
  }
}

static void hk_load(void) {
  g_hkLoaded = TRUE;
  hk_defaults();
  if (!g_dataDir[0]) return;
  wchar_t p[MAX_PATH];
  hk_path(p);
  HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  char buf[1024];
  DWORD n = 0;
  ReadFile(h, buf, sizeof(buf) - 1, &n, NULL);
  CloseHandle(h);
  buf[n] = 0;
  for (char *line = buf; line && *line;) {
    char *next = strchr(line, '\n');
    if (next) *next++ = 0;
    char name[24] = {0};
    unsigned vk = 0, mods = 0;
    if (sscanf(line, "%23s %u %u", name, &vk, &mods) >= 2 && vk < 256)
      for (int f = 0; f < HK_COUNT; f++)
        if (!strcmp(name, kHk[f].key)) {
          g_hkVk[f] = vk;
          g_hkMods[f] = mods & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN);
          g_hkOwn[f] = TRUE;
        }
    line = next;
  }
}

static void hk_save(void) {
  if (!g_dataDir[0]) return;
  wchar_t p[MAX_PATH];
  hk_path(p);
  char buf[1024];
  int k = 0;
  for (int f = 0; f < HK_COUNT; f++)
    if (g_hkOwn[f])
      k += snprintf(buf + k, sizeof(buf) - (size_t)k, "%s %u %u\n", kHk[f].key, g_hkVk[f], g_hkMods[f]);
  if (!k) {
    DeleteFileW(p); /* всё как по умолчанию */
    return;
  }
  HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(h, buf, (DWORD)k, &w, NULL);
  CloseHandle(h);
}

/* ---- регистрация --------------------------------------------------------------- */

static void hk_unregister_all(HWND hwnd) {
  for (int f = 0; f < HK_COUNT; f++)
    if (f != HK_SNIP) UnregisterHotKey(hwnd, kHk[f].id);
  for (int i = 0; i < SNIP_COUNT; i++) UnregisterHotKey(hwnd, HOTKEY_SNIP_BASE + i);
}

static BOOL hk_try(HWND hwnd, int f, UINT vk, UINT mods) {
  if (!vk) return FALSE;
  if (f == HK_SNIP) {
    /* какая-то цифра может быть занята — остальные всё равно работают */
    int ok = 0;
    for (int i = 0; i < SNIP_COUNT; i++)
      if (RegisterHotKey(hwnd, HOTKEY_SNIP_BASE + i, mods | MOD_NOREPEAT, (UINT)('1' + i))) ok++;
    return ok > 0;
  }
  return RegisterHotKey(hwnd, kHk[f].id, mods | MOD_NOREPEAT, vk);
}

static void hk_refresh_labels(void);

static void hk_register_all(HWND hwnd) {
  if (!g_hkLoaded) hk_load();
  hk_unregister_all(hwnd);
  /* запасные клавиши есть только у двух функций по умолчанию — их в конце,
     чтобы не отнять клавишу у назначенных вручную */
  static const int order[HK_COUNT] = {HK_MIN, HK_SEARCH, HK_OCR, HK_CURSOR, HK_SNIP, HK_TOGGLE, HK_LAYOUT};
  static const UINT pinAlt[] = {VK_F8, VK_PAUSE, VK_SCROLL}, lfAlt[] = {VK_PAUSE, VK_SCROLL};
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < HK_COUNT; i++) {
      int f = order[i];
      BOOL spare = !g_hkOwn[f] && (f == HK_TOGGLE || f == HK_LAYOUT);
      if (spare != (pass == 1)) continue;
      UINT vk = g_hkVk[f], mods = g_hkMods[f];
      BOOL ok = FALSE;
      if (spare) {
        const UINT *alt = f == HK_TOGGLE ? pinAlt : lfAlt;
        int na = f == HK_TOGGLE ? 3 : 2;
        for (int a = 0; a < na && !ok; a++) {
          vk = alt[a];
          mods = 0;
          ok = hk_try(hwnd, f, vk, mods); /* ту, что уже взята нами, Windows второй раз не даст */
        }
      } else {
        ok = hk_try(hwnd, f, vk, mods);
      }
      g_hkBusy[f] = g_hkVk[f] && !ok;
      if (ok) hk_combo_name(f, vk, mods, g_hkName[f], 40);
      else g_hkName[f][0] = 0;
    }
  hk_refresh_labels();
}

/* ---- надписи с клавишами по всей программе -------------------------------------- */

static void tray_tip_text(wchar_t *out, int n) {
  if (g_hkName[HK_TOGGLE][0])
    _snwprintf(out, n, L"CursorPad %s  ·  %s закрепить", APP_VERSION_STR, g_hkName[HK_TOGGLE]);
  else
    _snwprintf(out, n, L"CursorPad %s", APP_VERSION_STR);
  out[n - 1] = 0;
}

static void hk_refresh_labels(void) {
  if (g_trayAdded) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_TIP;
    tray_tip_text(n.szTip, 128);
    lstrcpynW(g_nid.szTip, n.szTip, 128);
    Shell_NotifyIconW(NIM_MODIFY, &n);
  }
  if (g_ocr) {
    wchar_t t[80];
    hk_with(L"Выделить и прочитать", HK_OCR, t, 80);
    SetWindowTextW(g_ocr, t);
    InvalidateRect(g_ocr, NULL, TRUE);
  }
  if (g_clipEdit) {
    static wchar_t cue[64]; /* подсказка поля живёт, пока её показывают */
    if (g_hkName[HK_SEARCH][0]) _snwprintf(cue, 64, L"буфер копии · %s", g_hkName[HK_SEARCH]);
    else lstrcpynW(cue, L"буфер копии", 64);
    cue[63] = 0;
    SendMessageW(g_clipEdit, 0x1501 /* EM_SETCUEBANNER */, TRUE, (LPARAM)cue);
    InvalidateRect(g_clipEdit, NULL, TRUE); /* сама подсказка поля не перерисовывается */
  }
  if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
  if (g_askHwnd) InvalidateRect(g_askHwnd, NULL, FALSE);
}

/* ---- окно «Горячие клавиши» ------------------------------------------------------ */

#define ID_HK_SET 241   /* +f: «Изменить» */
#define ID_HK_CLEAR 251 /* +f: «Убрать» */
#define ID_HK_RESET 260

static HWND g_hkWnd;
static int g_hkCap = -1; /* какой функции ждём клавишу; −1 — не ждём */
static wchar_t g_hkMsg[320];
static float g_hkS = 1.0f;

static int HS_(int v) { return (int)(v * g_hkS + 0.5f); }

#define HK_ROW 40
#define HK_W 620
#define HK_KEYW 150

static int hk_row_y(int f) { return PANEL_TITLE_H + HS_(10) + f * HS_(HK_ROW); }

static void hk_layout(void) {
  if (!g_hkWnd) return;
  RECT rc;
  GetClientRect(g_hkWnd, &rc);
  place_panel_close(g_hkWnd);
  int bh = HS_(28), clrW = HS_(78), setW = HS_(96), gap = HS_(6);
  for (int f = 0; f < HK_COUNT; f++) {
    int y = hk_row_y(f) + (HS_(HK_ROW) - bh) / 2;
    int x = rc.right - HS_(14) - clrW;
    HWND c = GetDlgItem(g_hkWnd, ID_HK_CLEAR + f), s = GetDlgItem(g_hkWnd, ID_HK_SET + f);
    if (c) MoveWindow(c, x, y, clrW, bh, TRUE);
    x -= gap + setW;
    if (s) MoveWindow(s, x, y, setW, bh, TRUE);
  }
  HWND r = GetDlgItem(g_hkWnd, ID_HK_RESET);
  if (r) MoveWindow(r, HS_(14), rc.bottom - HS_(14) - bh, HS_(190), bh, TRUE);
}

static void hk_sync_buttons(void) {
  if (!g_hkWnd) return;
  for (int f = 0; f < HK_COUNT; f++) {
    HWND s = GetDlgItem(g_hkWnd, ID_HK_SET + f), c = GetDlgItem(g_hkWnd, ID_HK_CLEAR + f);
    if (s) {
      SetWindowTextW(s, g_hkCap == f ? L"Отмена" : L"Изменить");
      InvalidateRect(s, NULL, TRUE);
    }
    if (c) EnableWindow(c, g_hkVk[f] != 0);
  }
  /* и кнопки тоже: тему могли сменить, пока окно было закрыто */
  RedrawWindow(g_hkWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static void hk_cap_end(void) {
  if (g_hkCap < 0) return;
  g_hkCap = -1;
  hk_register_all(g_hwnd); /* снятые на время ожидания — обратно */
  hk_sync_buttons();
}

static void hk_cap_start(int f) {
  g_hkCap = f;
  hk_unregister_all(g_hwnd);
  _snwprintf(g_hkMsg, 320, L"Нажмите новую клавишу или сочетание для «%s».  Esc — отмена.", kHk[f].title);
  g_hkMsg[319] = 0;
  SetForegroundWindow(g_hkWnd);
  SetFocus(g_hkWnd);
  hk_sync_buttons();
}

static BOOL hk_is_digit(UINT vk) { return (vk >= '0' && vk <= '9') || (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9); }

/* клавиша, которую можно назначить и без Ctrl/Alt: печатать она не мешает */
static BOOL hk_solo_ok(UINT vk) {
  return (vk >= VK_F1 && vk <= VK_F24) || vk == VK_PAUSE || vk == VK_SCROLL || vk == VK_INSERT ||
         vk == VK_SNAPSHOT || vk == VK_APPS || (vk >= VK_BROWSER_BACK && vk <= VK_LAUNCH_APP2);
}

/* сочетания, без которых не обойтись ни Windows, ни программам */
static BOOL hk_reserved(UINT vk, UINT mods) {
  if (mods == MOD_ALT && (vk == VK_F4 || vk == VK_TAB || vk == VK_ESCAPE || vk == VK_SPACE)) return TRUE;
  if (mods == MOD_CONTROL && (vk == 'C' || vk == 'V' || vk == 'X' || vk == 'Z' || vk == 'Y' || vk == 'A' ||
                              vk == 'S' || vk == VK_ESCAPE || vk == VK_INSERT))
    return TRUE;
  if (mods == MOD_SHIFT && (vk == VK_INSERT || vk == VK_DELETE)) return TRUE;
  if (mods == (MOD_CONTROL | MOD_ALT) && vk == VK_DELETE) return TRUE;
  if (mods == (MOD_CONTROL | MOD_SHIFT) && vk == VK_ESCAPE) return TRUE;
  return FALSE;
}

/* заняты ли у функций f и g одни и те же нажатия */
static BOOL hk_clash(int f, UINT vk, UINT mods, int g) {
  if (!g_hkVk[g] || g_hkMods[g] != mods) return FALSE;
  if (f == HK_SNIP) return g_hkVk[g] >= '1' && g_hkVk[g] <= '9';
  if (g == HK_SNIP) return vk >= '1' && vk <= '9';
  return g_hkVk[g] == vk;
}

static void hk_accept(UINT vk, UINT mods) {
  int f = g_hkCap;
  if (f < 0) return;
  wchar_t name[40];
  hk_combo_name(-1, vk, mods, name, 40);
  BOOL typing = !hk_solo_ok(vk);
  if (f == HK_SNIP) {
    if (!hk_is_digit(vk)) {
      _snwprintf(g_hkMsg, 320,
                 L"Для строк нужна цифра с Ctrl или Alt — например Alt+1 (номер строки подставится сам). "
                 L"Esc — отмена.");
      goto again;
    }
    if (!(mods & (MOD_CONTROL | MOD_ALT | MOD_WIN))) {
      _snwprintf(g_hkMsg, 320, L"Одна цифра мешала бы печатать — нажмите её вместе с Ctrl или Alt. Esc — отмена.");
      goto again;
    }
    vk = '1';
  } else if (typing && !(mods & (MOD_CONTROL | MOD_ALT | MOD_WIN))) {
    _snwprintf(g_hkMsg, 320,
               L"«%s» нужна для набора текста — одна она мешала бы печатать. Нажмите её вместе с Ctrl "
               L"или Alt, или возьмите F1…F12, Pause, Scroll Lock, Insert. Esc — отмена.",
               name);
    goto again;
  }
  if (hk_reserved(f == HK_SNIP ? 0 : vk, mods)) {
    _snwprintf(g_hkMsg, 320, L"%s нужна Windows и программам — выберите другое сочетание. Esc — отмена.", name);
    goto again;
  }
  {
    /* клавиша была у другой функции — та остаётся без неё */
    wchar_t took[200] = {0};
    for (int g = 0; g < HK_COUNT; g++) {
      if (g == f || !hk_clash(f, vk, mods, g)) continue;
      g_hkVk[g] = 0;
      g_hkMods[g] = 0;
      g_hkOwn[g] = TRUE;
      _snwprintf(took, 200, L"  У «%s» клавиши теперь нет.", kHk[g].title);
      took[199] = 0;
    }
    g_hkVk[f] = vk;
    g_hkMods[f] = mods;
    g_hkOwn[f] = TRUE;
    hk_save();
    g_hkCap = -1;
    hk_register_all(g_hwnd);
    if (g_hkBusy[f])
      _snwprintf(g_hkMsg, 320, L"%s занята другой программой — у «%s» клавиши пока нет. Выберите другую.%s",
                 name, kHk[f].title, took);
    else
      _snwprintf(g_hkMsg, 320, L"Готово: «%s» — %s.%s", kHk[f].title, g_hkName[f], took);
    g_hkMsg[319] = 0;
    hk_sync_buttons();
    return;
  }
again:
  g_hkMsg[319] = 0;
  InvalidateRect(g_hkWnd, NULL, TRUE);
}

static void hk_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)bg_brush(TRUE)); /* шапка берёт фон класса, тему могли сменить */
  FillRect(hdc, &rc, bg_brush(TRUE));
  draw_panel_header(hwnd, hdc, L"Горячие клавиши");
  SetBkMode(hdc, TRANSPARENT);
  int bh = HS_(28);
  int keyR = rc.right - HS_(14) - HS_(78) - HS_(6) - HS_(96) - HS_(10);
  int keyL = keyR - HS_(HK_KEYW);
  for (int f = 0; f < HK_COUNT; f++) {
    int y = hk_row_y(f);
    if (f) {
      RECT ln = {HS_(14), y, rc.right - HS_(14), y + 1};
      HBRUSH lb = CreateSolidBrush(COL_LINE);
      FillRect(hdc, &ln, lb);
      DeleteObject(lb);
    }
    RECT t = {HS_(16), y, keyL - HS_(8), y + HS_(HK_ROW)};
    if (g_fontBody) SelectObject(hdc, g_fontBody);
    SetTextColor(hdc, COL_INK);
    DrawTextW(hdc, kHk[f].title, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT k = {keyL, y + (HS_(HK_ROW) - bh) / 2, keyR, y + (HS_(HK_ROW) - bh) / 2 + bh};
    wchar_t kt[80];
    COLORREF fg = COL_INK;
    if (g_hkCap == f) {
      fill_round_rect(hdc, k, COL_PAPER, COL_SAGE, HS_(6));
      lstrcpynW(kt, L"нажмите клавишу…", 80);
      fg = COL_SAGE;
    } else {
      fill_round_rect(hdc, k, COL_PAPER, COL_LINE, HS_(6));
      if (g_hkName[f][0]) {
        lstrcpynW(kt, g_hkName[f], 80);
      } else if (g_hkBusy[f]) {
        wchar_t nm[40];
        hk_combo_name(f, g_hkVk[f], g_hkMods[f], nm, 40);
        _snwprintf(kt, 80, L"%s занята", nm);
        kt[79] = 0;
        fg = RGB(0xB4, 0x23, 0x18);
      } else {
        lstrcpynW(kt, L"нет", 80);
        fg = COL_MUTED;
      }
    }
    if (g_fontUi) SelectObject(hdc, g_fontUi);
    SetTextColor(hdc, fg);
    DrawTextW(hdc, kt, -1, &k, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  /* подсказка или итог последнего нажатия */
  int y = hk_row_y(HK_COUNT) + HS_(8);
  RECT m = {HS_(16), y, rc.right - HS_(16), rc.bottom - HS_(14) - bh - HS_(8)};
  if (g_fontBody) SelectObject(hdc, g_fontBody);
  SetTextColor(hdc, g_hkCap >= 0 ? COL_SAGE : COL_MUTED);
  DrawTextW(hdc,
            g_hkMsg[0] ? g_hkMsg
                       : L"«Изменить» — и нажмите клавишу или сочетание: F1…F12, Pause, Scroll Lock или с Ctrl, "
                         L"Alt, Shift — например Ctrl+Alt+R. «Убрать» — функция останется без клавиши, "
                         L"кнопки и меню программы работают как раньше.",
            -1, &m, DT_LEFT | DT_WORDBREAK);
}

static LRESULT CALLBACK HotkeysProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    hk_paint(hwnd, hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_SIZE:
    hk_layout();
    return 0;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYUP: {
    if (g_hkCap < 0) break;
    UINT vk = (UINT)wParam;
    BOOL down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    /* Print Screen приходит только отпусканием */
    if (!down && vk != VK_SNAPSHOT) return 0;
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN ||
        vk == VK_RWIN || vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_PROCESSKEY || vk == 0xFF)
      return 0; /* ждём саму клавишу */
    UINT mods = 0;
    if (GetKeyState(VK_CONTROL) < 0) mods |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) < 0) mods |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) < 0) mods |= MOD_SHIFT;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) mods |= MOD_WIN;
    if (vk == VK_ESCAPE && !mods) {
      g_hkMsg[0] = 0;
      hk_cap_end();
      return 0;
    }
    hk_accept(vk, mods);
    return 0;
  }
  case WM_CHAR:
  case WM_SYSCHAR:
    if (g_hkCap >= 0) return 0; /* Alt+буква не зовёт системное меню и не пищит */
    break;
  case WM_ACTIVATE:
    /* ушли в другое окно, не нажав клавишу — наши клавиши вернуть */
    if (LOWORD(wParam) == WA_INACTIVE && g_hkCap >= 0) {
      g_hkMsg[0] = 0;
      hk_cap_end();
    }
    break;
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (HIWORD(wParam) != BN_CLICKED) return 0;
    if (id == ID_PANEL_CLOSE) {
      SendMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    if (id >= ID_HK_SET && id < ID_HK_SET + HK_COUNT) {
      int f = id - ID_HK_SET;
      if (g_hkCap == f) {
        g_hkMsg[0] = 0;
        hk_cap_end();
      } else {
        g_hkCap = -1;
        hk_cap_start(f);
      }
      return 0;
    }
    if (id >= ID_HK_CLEAR && id < ID_HK_CLEAR + HK_COUNT) {
      int f = id - ID_HK_CLEAR;
      wchar_t was[40];
      lstrcpynW(was, g_hkName[f], 40);
      g_hkCap = -1;
      g_hkVk[f] = 0;
      g_hkMods[f] = 0;
      g_hkOwn[f] = TRUE;
      hk_save();
      hk_register_all(g_hwnd);
      if (was[0])
        _snwprintf(g_hkMsg, 320, L"У «%s» клавиши нет, %s теперь ничего не делает.", kHk[f].title, was);
      else
        _snwprintf(g_hkMsg, 320, L"У «%s» клавиши нет.", kHk[f].title);
      g_hkMsg[319] = 0;
      hk_sync_buttons();
      return 0;
    }
    if (id == ID_HK_RESET) {
      g_hkCap = -1;
      hk_defaults();
      hk_save();
      hk_register_all(g_hwnd);
      lstrcpynW(g_hkMsg, L"Все клавиши — как были по умолчанию.", 320);
      hk_sync_buttons();
      return 0;
    }
    return 0;
  }
  case WM_CLOSE:
    g_hkMsg[0] = 0;
    hk_cap_end();
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void hk_show(void) {
  if (!g_hkWnd) {
    HDC s = GetDC(NULL);
    g_hkS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HotkeysProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = bg_brush(TRUE);
    wc.lpszClassName = L"CursorPadHotkeys";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    int w = HS_(HK_W), h = hk_row_y(HK_COUNT) + HS_(8 + 66 + 14 + 28 + 14);
    g_hkWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadHotkeys", L"Горячие клавиши",
                              WS_POPUP | WS_CLIPCHILDREN, 0, 0, w, h, NULL, NULL, g_inst, NULL);
    if (!g_hkWnd) return;
    round_corners(g_hkWnd);
    mk_btn(g_hkWnd, L"×", ID_PANEL_CLOSE);
    for (int f = 0; f < HK_COUNT; f++) {
      mk_btn(g_hkWnd, L"Изменить", ID_HK_SET + f);
      mk_btn(g_hkWnd, L"Убрать", ID_HK_CLEAR + f);
    }
    mk_btn(g_hkWnd, L"Как было по умолчанию", ID_HK_RESET);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_hkWnd, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                 SWP_NOZORDER);
  }
  g_hkMsg[0] = 0;
  hk_layout();
  hk_sync_buttons();
  ShowWindow(g_hkWnd, SW_SHOW);
  SetWindowPos(g_hkWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  SetForegroundWindow(g_hkWnd);
}
