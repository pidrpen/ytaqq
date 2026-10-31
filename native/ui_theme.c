/* ui_theme.c — общий вид CursorPad (с 2026.10.21).

   Спокойное оформление в духе Windows 11: ровные светлые поверхности,
   тонкие рамки, скругление 6–8 px, один цвет акцента темы (COL_SAGE) —
   только у главной кнопки, выбранного варианта и рамки фокуса. Темы
   (kThemes) остаются своими: здесь только то, чем они рисуются.
   Файл подключается из cursorpad.c раньше модулей, которые им пользуются:
   - токены палитры g_uiTok — снимок цветов текущей темы (их может брать
     и аватар);
   - холст в памяти (UiCanvas) и скруглённые прямоугольники со сглаженным
     краем — без GDI+ и без новых библиотек;
   - хром Windows 11 через dwmapi, загруженную на лету: углы окна из темы,
     Mica у окон с системным заголовком; на Windows 10 ничего не меняется;
   - шрифт значков Segoe Fluent Icons / Segoe MDL2 Assets (если есть);
   - единственная анимация — короткое (до 120 мс) затухание подсветки
     кнопки под мышью и быстрое появление окна; таймер живёт, только пока
     что-то меняется, и сам останавливается;
   - тонкие рамки полей ввода и списков, при фокусе — цветом акцента. */

typedef struct {
  COLORREF bg, bg2, surface, ink, muted, line, accent;
} UiTokens;
static UiTokens g_uiTok = {RGB(255, 255, 255), RGB(243, 244, 246), RGB(255, 255, 255), RGB(17, 24, 39),
                           RGB(107, 114, 128), RGB(226, 229, 233), RGB(37, 99, 235)};
static int g_uiGen = 1; /* растёт при смене темы: окна перекрашивают хром */

/* определены ниже, в cursorpad.c */
static BOOL font_exists(const wchar_t *face);
static HBRUSH bg_brush(BOOL dark);
static BOOL knight_on(void);

static int ui_dpi(HWND h) {
  typedef UINT(WINAPI * DpiFn)(HWND);
  static DpiFn fn;
  static BOOL tried;
  if (!tried) {
    tried = TRUE;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) fn = (DpiFn)GetProcAddress(u, "GetDpiForWindow");
  }
  UINT d = (fn && h) ? fn(h) : 0;
  if (!d) {
    HDC dc = GetDC(NULL);
    d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
  }
  return d ? (int)d : 96;
}
static int ui_px(HWND h, int v) { return MulDiv(v, ui_dpi(h), 96); }

static void ui_sync_tokens(void) {
  g_uiTok.bg = COL_PAPER;
  g_uiTok.bg2 = COL_PAPER_DARK;
  g_uiTok.surface = COL_PAPER;
  g_uiTok.ink = COL_INK;
  g_uiTok.muted = COL_MUTED;
  g_uiTok.line = COL_LINE;
  g_uiTok.accent = COL_SAGE;
  g_uiGen++;
}

/* ---- холст в памяти ------------------------------------------------------ */
/* 32-битный DIB: фигуры смешиваются попиксельно, текст рисуется обычным GDI,
   на цель всё кладётся одним BitBlt — без мигания. Логические координаты
   холста совпадают с координатами того, кто рисует (например, окна-родителя
   кнопки), поэтому под кнопкой можно положить ровно тот фон, что вокруг. */
typedef struct {
  HDC dc;
  HBITMAP bm;
  HGDIOBJ old;
  DWORD *px; /* 0x00RRGGBB, строки сверху вниз */
  int w, h;
  int lx, ly; /* логические координаты левого верхнего пикселя */
  int tx, ty; /* куда положить на целевой DC */
} UiCanvas;

static BOOL ui_canvas_begin(UiCanvas *c, HDC target, RECT box, int tx, int ty, BOOL grab) {
  memset(c, 0, sizeof(*c));
  c->w = box.right - box.left;
  c->h = box.bottom - box.top;
  c->lx = box.left;
  c->ly = box.top;
  c->tx = tx;
  c->ty = ty;
  if (c->w <= 0 || c->h <= 0 || c->w > 8192 || c->h > 8192) return FALSE;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = c->w;
  bi.bmiHeader.biHeight = -c->h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  c->dc = CreateCompatibleDC(target);
  if (!c->dc) return FALSE;
  void *bits = NULL;
  c->bm = CreateDIBSection(target, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!c->bm || !bits) {
    if (c->bm) DeleteObject(c->bm);
    DeleteDC(c->dc);
    c->dc = NULL;
    return FALSE;
  }
  c->px = (DWORD *)bits;
  c->old = SelectObject(c->dc, c->bm);
  if (grab) BitBlt(c->dc, 0, 0, c->w, c->h, target, tx, ty, SRCCOPY);
  SetViewportOrgEx(c->dc, -c->lx, -c->ly, NULL);
  SetBkMode(c->dc, TRANSPARENT);
  return TRUE;
}

static void ui_canvas_end(UiCanvas *c, HDC target) {
  if (!c->dc) return;
  GdiFlush();
  SetViewportOrgEx(c->dc, 0, 0, NULL);
  BitBlt(target, c->tx, c->ty, c->w, c->h, c->dc, 0, 0, SRCCOPY);
  SelectObject(c->dc, c->old);
  DeleteObject(c->bm);
  DeleteDC(c->dc);
  c->dc = NULL;
}

static DWORD ui_rgbpx(COLORREF c) {
  return ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
}

/* смешать цвет c с пикселем, a — 0…255 */
static void ui_mix(DWORD *p, COLORREF c, int a) {
  if (a <= 0) return;
  if (a >= 255) {
    *p = ui_rgbpx(c);
    return;
  }
  DWORD d = *p;
  int dr = (int)((d >> 16) & 255), dg = (int)((d >> 8) & 255), db = (int)(d & 255);
  dr += ((int)GetRValue(c) - dr) * a / 255;
  dg += ((int)GetGValue(c) - dg) * a / 255;
  db += ((int)GetBValue(c) - db) * a / 255;
  *p = ((DWORD)dr << 16) | ((DWORD)dg << 8) | (DWORD)db;
}

/* расстояние до прямоугольника со скруглёнными углами (минус — внутри) */
static float ui_sd(float px, float py, float hx, float hy, float r) {
  float qx = fabsf(px) - hx + r, qy = fabsf(py) - hy + r;
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float in = qx > qy ? qx : qy;
  if (in > 0) in = 0;
  return sqrtf(ox * ox + oy * oy) + in - r;
}

typedef struct {
  float r;        /* скругление, px; меньше нуля — «таблетка» */
  COLORREF fill;
  int fillA;      /* 0…255; 0 — без заливки */
  COLORREF border;
  int borderA;    /* 0 — без рамки */
  float bw;       /* толщина рамки (внутрь) */
} UiShape;

/* скруглённый прямоугольник s (в логических координатах холста): край
   сглажен по покрытию пикселя */
static void ui_shape(UiCanvas *c, RECT s, const UiShape *k) {
  if (!c->dc) return;
  float hx = (float)(s.right - s.left) * 0.5f, hy = (float)(s.bottom - s.top) * 0.5f;
  if (hx < 0.5f || hy < 0.5f) return;
  float cx = (float)s.left + hx, cy = (float)s.top + hy;
  float r = k->r < 0 ? (hx < hy ? hx : hy) : k->r;
  if (r > hx) r = hx;
  if (r > hy) r = hy;
  int x0 = s.left - 1, x1 = s.right + 1, y0 = s.top - 1, y1 = s.bottom + 1;
  if (x0 < c->lx) x0 = c->lx;
  if (y0 < c->ly) y0 = c->ly;
  if (x1 > c->lx + c->w) x1 = c->lx + c->w;
  if (y1 > c->ly + c->h) y1 = c->ly + c->h;
  if (x0 >= x1 || y0 >= y1) return;
  GdiFlush();
  BOOL border = k->borderA > 0 && k->bw > 0;
  float edge = (border ? k->bw : 0.0f) + 1.5f;
  DWORD fillPx = ui_rgbpx(k->fill);
  float inL = cx - hx + edge, inR = cx + hx - edge;
  for (int y = y0; y < y1; y++) {
    DWORD *line = c->px + (size_t)(y - c->ly) * (size_t)c->w;
    float py = (float)y + 0.5f - cy;
    /* средние строки: всё, что дальше края, — внутренность без пересчёта */
    BOOL mid = fabsf(py) < hy - r - edge;
    for (int x = x0; x < x1; x++) {
      float fx = (float)x + 0.5f;
      if (mid && fx > inL && fx < inR) {
        if (k->fillA <= 0) {
          int nx = (int)ceilf(inR - 0.5f) - 1;
          if (nx > x) x = nx;
          continue;
        }
        if (k->fillA >= 255) line[x - c->lx] = fillPx;
        else ui_mix(&line[x - c->lx], k->fill, k->fillA);
        continue;
      }
      float d = ui_sd(fx - cx, py, hx, hy, r);
      float cov = 0.5f - d;
      if (cov <= 0) continue;
      if (cov > 1) cov = 1;
      DWORD *p = &line[x - c->lx];
      if (k->fillA > 0) ui_mix(p, k->fill, (int)((float)k->fillA * cov + 0.5f));
      if (border) {
        float ci = 0.5f - (d + k->bw);
        if (ci < 0) ci = 0;
        if (ci > 1) ci = 1;
        float ring = cov - ci;
        if (ring > 0) ui_mix(p, k->border, (int)((float)k->borderA * ring + 0.5f));
      }
    }
  }
}

/* залитый скруглённый прямоугольник прямо на DC — со сглаженным краем */
static void ui_round_fill(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border) {
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, dc, r, r.left, r.top, TRUE)) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pn = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pn);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(pn);
    return;
  }
  UiShape s;
  memset(&s, 0, sizeof(s));
  s.r = (float)rad;
  s.fill = fill;
  s.fillA = 255;
  if (border != fill) {
    s.border = border;
    s.borderA = 255;
    s.bw = 1.0f;
  }
  ui_shape(&cv, r, &s);
  ui_canvas_end(&cv, dc);
}

/* ---- хром Windows 11 ------------------------------------------------------ */
typedef HRESULT(WINAPI *UiDwmSetFn)(HWND, DWORD, LPCVOID, DWORD);
static UiDwmSetFn ui_dwm(void) {
  static UiDwmSetFn fn;
  static BOOL tried;
  if (!tried) {
    tried = TRUE;
    HMODULE m = LoadLibraryW(L"dwmapi.dll"); /* держим до выхода */
    if (m) fn = (UiDwmSetFn)GetProcAddress(m, "DwmSetWindowAttribute");
  }
  return fn;
}
static void ui_dwm_set(HWND h, DWORD attr, DWORD v) {
  UiDwmSetFn fn = ui_dwm();
  if (fn) fn(h, attr, &v, sizeof(v)); /* на Windows 10 новые свойства просто отклоняются */
}
#define UI_DWMWA_DARK 20     /* DWMWA_USE_IMMERSIVE_DARK_MODE */
#define UI_DWMWA_CORNER 33   /* DWMWA_WINDOW_CORNER_PREFERENCE */
#define UI_DWMWA_BACKDROP 38 /* DWMWA_SYSTEMBACKDROP_TYPE */

/* тёмная ли тема — по яркости бумаги */
static BOOL ui_theme_dark(void) {
  COLORREF p = kThemes[(g_theme >= 0 && g_theme < THEME_COUNT) ? g_theme : 0].paper;
  return (GetRValue(p) * 299 + GetGValue(p) * 587 + GetBValue(p) * 114) / 1000 < 128;
}

/* углы окна — из темы (у «Чертежа» прямые); у окон с системным заголовком
   (расчёты, TIFF) заголовок — Mica, как у приложений Windows 11; у тёмных
   тем он тёмный. */
static void ui_window_chrome(HWND h) {
  if (!h || !ui_dwm()) return;
  const PadTheme *t = &kThemes[(g_theme >= 0 && g_theme < THEME_COUNT) ? g_theme : 0];
  ui_dwm_set(h, UI_DWMWA_CORNER, (DWORD)t->corners);
  if ((GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
    /* тёмные темы («Графит», «Синька», «Цех») — тёмный заголовок, остальные светлый */
    ui_dwm_set(h, UI_DWMWA_DARK, ui_theme_dark() ? 1 : 0);
    ui_dwm_set(h, UI_DWMWA_BACKDROP, 2); /* DWMSBT_MAINWINDOW — Mica */
  }
}

typedef HRESULT(WINAPI *UiThemeFn)(HWND, LPCWSTR, LPCWSTR);
static UiThemeFn ui_set_theme_fn(void) {
  static UiThemeFn fn;
  static BOOL tried;
  if (!tried) {
    tried = TRUE;
    HMODULE m = LoadLibraryW(L"uxtheme.dll");
    if (m) fn = (UiThemeFn)GetProcAddress(m, "SetWindowTheme");
  }
  return fn;
}

/* выделение строк и полосы прокрутки — как в Проводнике; у тёмных тем —
   тёмные (DarkMode_Explorer): светлая системная полоса на графите резала глаз.
   Какая стоит — в свойстве CpUiScroll (1 светлая, 2 тёмная) */
static void ui_explorer_theme(HWND h) {
  UiThemeFn fn = ui_set_theme_fn();
  if (!fn) return;
  BOOL dark = ui_theme_dark();
  fn(h, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
  SetPropW(h, L"CpUiScroll", (HANDLE)(INT_PTR)(dark ? 2 : 1));
}

/* поле или список под нынешнюю тему: тёмной теме — тёмные полосы прокрутки,
   светлой — как было (у списков «Проводник», у полей — тема по умолчанию) */
static void ui_scroll_theme(HWND h, BOOL list) {
  int want = ui_theme_dark() ? 2 : 1, have = (int)(INT_PTR)GetPropW(h, L"CpUiScroll");
  if (have == want) return;
  if (want == 2 || list) {
    ui_explorer_theme(h);
  } else if (have == 2) { /* поле было тёмным — вернуть тему по умолчанию */
    UiThemeFn fn = ui_set_theme_fn();
    if (fn) fn(h, NULL, NULL);
    RemovePropW(h, L"CpUiScroll");
  }
}

/* предупреждения красным: на тёмной бумаге тёмно-красный не читается (около 2:1),
   у тёмных тем — светлый красный (не хуже 5:1), у светлых — как задано */
static COLORREF ui_warn(COLORREF light) { return ui_theme_dark() ? RGB(255, 132, 116) : light; }

/* выпадающий список: у тёмной темы — тёмное поле со светлым текстом (DarkMode_CFD,
   как в диалогах Windows), у светлой — обычный; системно-белые поля на графите слепили */
static void ui_combo_theme(HWND h) {
  int want = ui_theme_dark() ? 2 : 1, have = (int)(INT_PTR)GetPropW(h, L"CpUiScroll");
  if (have == want || (want == 1 && !have)) return;
  UiThemeFn fn = ui_set_theme_fn();
  if (!fn) return;
  fn(h, want == 2 ? L"DarkMode_CFD" : NULL, NULL);
  SetPropW(h, L"CpUiScroll", (HANDLE)(INT_PTR)want);
  InvalidateRect(h, NULL, TRUE);
}

/* ---- флажки и переключатели в тёмных темах ------------------------------------
   Подпись флажка со стилями Windows рисует сама и всегда чёрным — цвет из
   WM_CTLCOLORSTATIC она не слушает, и на «Графите», «Синьке», «Цехе» подписи
   пропадали. В тёмной теме флажок рисуется целиком здесь (NM_CUSTOMDRAW у
   родителя: так кнопка зовёт рисование при любом состоянии — наведение, нажатие,
   галочка): квадратик (у переключателя — кружок) со скруглением, галочка цветом
   акцента, подпись цветом текста темы. Родитель подменяется один раз
   (ui_check_parent_proc), в светлых темах всё как было. */
static BOOL ui_is_check(HWND b) {
  wchar_t cls[16];
  if (!GetClassNameW(b, cls, 16) || _wcsicmp(cls, L"Button")) return FALSE;
  LONG st = GetWindowLongW(b, GWL_STYLE);
  if (st & BS_PUSHLIKE) return FALSE;
  switch (st & BS_TYPEMASK) {
  case BS_CHECKBOX: case BS_AUTOCHECKBOX: case BS_3STATE: case BS_AUTO3STATE: case BS_RADIOBUTTON: case BS_AUTORADIOBUTTON:
    return TRUE;
  }
  return FALSE;
}

static LRESULT ui_check_draw(const NMCUSTOMDRAW *cd) {
  HWND b = cd->hdr.hwndFrom, par = GetParent(b);
  HDC dc = cd->hdc;
  RECT rc;
  GetClientRect(b, &rc);
  /* фон — тот, что родитель даёт подписям (на пергаменте — с узором) */
  HBRUSH br = par ? (HBRUSH)SendMessageW(par, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)b) : NULL;
  if (!br || GetObjectType(br) != OBJ_BRUSH) br = bg_brush(FALSE);
  FillRect(dc, &rc, br);
  LONG st = GetWindowLongW(b, GWL_STYLE);
  int type = st & BS_TYPEMASK;
  BOOL radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
  LRESULT chk = SendMessageW(b, BM_GETCHECK, 0, 0);
  BOOL dis = !IsWindowEnabled(b), hot = (cd->uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
  int box = ui_px(b, 16), cy = (rc.top + rc.bottom) / 2;
  if (box > rc.bottom - rc.top) box = rc.bottom - rc.top;
  RECT bx = {rc.left + ui_px(b, 1), cy - box / 2, rc.left + ui_px(b, 1) + box, cy - box / 2 + box};
  UiCanvas cv;
  if (ui_canvas_begin(&cv, dc, bx, bx.left, bx.top, TRUE)) {
    UiShape s;
    memset(&s, 0, sizeof(s));
    s.r = radio ? -1.0f : (float)ui_px(b, 4);
    s.fillA = 255;
    s.borderA = 255;
    s.bw = (float)ui_px(b, 1);
    if (chk && !dis) {
      s.fill = hot ? blend_rgb(COL_SAGE, COL_INK, 40) : COL_SAGE;
      s.border = s.fill;
    } else {
      s.fill = blend_rgb(COL_PAPER, COL_INK, hot ? 34 : 16);
      s.border = dis ? blend_rgb(COL_PAPER, COL_INK, 70) : blend_rgb(COL_PAPER, COL_INK, hot ? 160 : 120);
    }
    ui_shape(&cv, bx, &s);
    if (chk && radio) { /* точка */
      int d = box * 2 / 5;
      RECT dot = {bx.left + (box - d) / 2, bx.top + (box - d) / 2, bx.left + (box - d) / 2 + d, bx.top + (box - d) / 2 + d};
      s.fill = COL_PAPER;
      s.borderA = 0;
      ui_shape(&cv, dot, &s);
    }
    ui_canvas_end(&cv, dc);
  }
  if (chk && !radio) { /* галочка (у третьего состояния — черта) цветом бумаги по акценту */
    HPEN pen = CreatePen(PS_SOLID, ui_px(b, 2), dis ? COL_MUTED : COL_PAPER);
    HGDIOBJ op = SelectObject(dc, pen);
    if (chk == BST_INDETERMINATE) {
      MoveToEx(dc, bx.left + box / 4, cy, NULL);
      LineTo(dc, bx.right - box / 4, cy);
    } else {
      POINT pt[3] = {{bx.left + box * 24 / 100, bx.top + box * 52 / 100},
                     {bx.left + box * 42 / 100, bx.top + box * 70 / 100},
                     {bx.left + box * 76 / 100, bx.top + box * 32 / 100}};
      Polyline(dc, pt, 3);
    }
    SelectObject(dc, op);
    DeleteObject(pen);
  }
  wchar_t t[256];
  GetWindowTextW(b, t, 256);
  if (t[0]) {
    HFONT f = (HFONT)SendMessageW(b, WM_GETFONT, 0, 0);
    HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, dis ? COL_MUTED : COL_INK);
    RECT tr = {bx.right + ui_px(b, 7), rc.top, rc.right, rc.bottom};
    UINT fl = DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS;
    if (st & BS_MULTILINE) { /* по высоте — посередине */
      RECT m = tr;
      DrawTextW(dc, t, -1, &m, fl | DT_WORDBREAK | DT_CALCRECT);
      int h = m.bottom - m.top;
      tr.top = cy - h / 2;
      tr.bottom = tr.top + h;
      DrawTextW(dc, t, -1, &tr, fl | DT_WORDBREAK);
    } else {
      DrawTextW(dc, t, -1, &tr, fl | DT_VCENTER | DT_SINGLELINE);
    }
    if ((cd->uItemState & CDIS_FOCUS) && !(SendMessageW(b, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
      RECT fr = tr;
      DrawTextW(dc, t, -1, &fr, fl | DT_SINGLELINE | DT_CALCRECT);
      InflateRect(&fr, 2, 1);
      DrawFocusRect(dc, &fr);
    }
    if (of) SelectObject(dc, of);
  }
  return CDRF_SKIPDEFAULT;
}

static LRESULT CALLBACK ui_check_parent_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
  (void)id;
  (void)ref;
  if (m == WM_NOTIFY && ui_theme_dark() && !knight_on()) {
    const NMHDR *nh = (const NMHDR *)l;
    if (nh && nh->code == (UINT)NM_CUSTOMDRAW && ui_is_check(nh->hwndFrom)) {
      const NMCUSTOMDRAW *cd = (const NMCUSTOMDRAW *)l;
      if (cd->dwDrawStage == CDDS_PREPAINT) return ui_check_draw(cd);
      return CDRF_DODEFAULT;
    }
  }
  if (m == WM_NCDESTROY) RemoveWindowSubclass(h, ui_check_parent_proc, 0x5547);
  return DefSubclassProc(h, m, w, l);
}

/* кисти фона прежних тем. Окна модулей регистрируют класс с g_paper той темы,
   что стояла при первом открытии; тема сменилась — кисть удалена, а класс
   помнит её, и под кнопками («×» в «Режимах») проступал фон первой темы.
   При показе и смене темы такая кисть класса меняется на нынешнюю */
static HBRUSH g_uiOldBr[32][2];
static int g_uiOldN;
static void ui_brush_retire(HBRUSH paper, HBRUSH dark) {
  if (!paper && !dark) return;
  if (g_uiOldN == 32) {
    memmove(g_uiOldBr[0], g_uiOldBr[1], sizeof(g_uiOldBr[0]) * 31);
    g_uiOldN = 31;
  }
  g_uiOldBr[g_uiOldN][0] = paper;
  g_uiOldBr[g_uiOldN][1] = dark;
  g_uiOldN++;
}
static void ui_class_brush_fix(HWND h) {
  HBRUSH cb = (HBRUSH)GetClassLongPtrW(h, GCLP_HBRBACKGROUND);
  if (!cb || cb == g_paper || cb == g_paperDark) return;
  for (int i = g_uiOldN - 1; i >= 0; i--)
    for (int k = 0; k < 2; k++)
      if (g_uiOldBr[i][k] == cb) {
        SetClassLongPtrW(h, GCLP_HBRBACKGROUND, (LONG_PTR)(k ? g_paperDark : g_paper));
        return;
      }
}

/* ---- шрифт значков --------------------------------------------------------- */
/* Segoe Fluent Icons (Windows 11) или Segoe MDL2 Assets (Windows 10): значки
   чёткие на любом масштабе. Нет ни того, ни другого — кнопки рисуют значки
   линиями, как раньше. Шрифт каждого размера создаётся один раз. */
static wchar_t g_uiIconFace[LF_FACESIZE];
static int g_uiIconState; /* 0 не искали, 1 есть, 2 нет */
static struct {
  int px;
  HFONT f;
} g_uiIcons[4];
static int g_uiIconNext;

static HFONT ui_icon_font(int px) {
  if (!g_uiIconState) {
    g_uiIconState = 2;
    if (font_exists(L"Segoe Fluent Icons")) {
      lstrcpynW(g_uiIconFace, L"Segoe Fluent Icons", LF_FACESIZE);
      g_uiIconState = 1;
    } else if (font_exists(L"Segoe MDL2 Assets")) {
      lstrcpynW(g_uiIconFace, L"Segoe MDL2 Assets", LF_FACESIZE);
      g_uiIconState = 1;
    }
  }
  if (g_uiIconState != 1 || px < 6) return NULL;
  for (int i = 0; i < 4; i++)
    if (g_uiIcons[i].f && g_uiIcons[i].px == px) return g_uiIcons[i].f;
  int slot = -1;
  for (int i = 0; i < 4 && slot < 0; i++)
    if (!g_uiIcons[i].f) slot = i;
  if (slot < 0) {
    slot = g_uiIconNext;
    g_uiIconNext = (g_uiIconNext + 1) % 4;
    DeleteObject(g_uiIcons[slot].f);
    g_uiIcons[slot].f = NULL;
  }
  g_uiIcons[slot].f = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, g_uiIconFace);
  g_uiIcons[slot].px = px;
  return g_uiIcons[slot].f;
}

/* ---- короткие переходы: один таймер, только пока что-то меняется ---------- */
enum { UI_A_HOVER = 1, UI_A_FADE };
typedef struct {
  HWND h;
  int kind;
  int from, to, last;
  DWORD t0, dur;
  BOOL keep; /* окно было прозрачным само — стиль WS_EX_LAYERED не снимать */
} UiAnim;
#define UI_ANIM_N 32
static UiAnim g_uiAnim[UI_ANIM_N];
static int g_uiAnimN;
static UINT_PTR g_uiTimer;
static void CALLBACK ui_anim_tick(HWND hw, UINT msg, UINT_PTR id, DWORD now32);

static void ui_timer_on(void) {
  if (!g_uiTimer) g_uiTimer = SetTimer(NULL, 0, 16, ui_anim_tick);
}

static int ui_anim_find(HWND h, int kind) {
  for (int i = 0; i < g_uiAnimN; i++)
    if (g_uiAnim[i].h == h && g_uiAnim[i].kind == kind) return i;
  return -1;
}

static int ui_anim_add(HWND h, int kind) {
  if (g_uiAnimN >= UI_ANIM_N) return -1;
  UiAnim *a = &g_uiAnim[g_uiAnimN];
  memset(a, 0, sizeof(*a));
  a->h = h;
  a->kind = kind;
  a->t0 = GetTickCount();
  return g_uiAnimN++;
}

static void ui_anim_forget(HWND h) {
  for (int i = 0; i < g_uiAnimN;) {
    if (g_uiAnim[i].h == h) g_uiAnim[i] = g_uiAnim[--g_uiAnimN];
    else i++;
  }
}

/* насколько кнопка «под мышью»: 0…255 */
static int ui_hover_level(HWND h) {
  int v = h ? (int)(INT_PTR)GetPropW(h, L"CpUiHot") : 0;
  return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static void ui_hover_to(HWND h, int to) {
  int cur = ui_hover_level(h);
  int i = ui_anim_find(h, UI_A_HOVER);
  if (cur == to && i < 0) return;
  if (i < 0) i = ui_anim_add(h, UI_A_HOVER);
  if (i < 0) {
    SetPropW(h, L"CpUiHot", (HANDLE)(INT_PTR)to);
    InvalidateRect(h, NULL, FALSE);
    return;
  }
  g_uiAnim[i].from = cur;
  g_uiAnim[i].to = to;
  g_uiAnim[i].t0 = GetTickCount();
  g_uiAnim[i].dur = to > cur ? 90 : 120;
  ui_timer_on();
}

static void CALLBACK ui_anim_tick(HWND hw, UINT msg, UINT_PTR id, DWORD now32) {
  (void)hw;
  (void)msg;
  (void)id;
  (void)now32;
  DWORD now = GetTickCount();
  for (int i = 0; i < g_uiAnimN;) {
    UiAnim *a = &g_uiAnim[i];
    BOOL done = FALSE;
    if (!IsWindow(a->h)) {
      done = TRUE;
    } else {
      float t = a->dur ? (float)(now - a->t0) / (float)a->dur : 1.0f;
      if (t > 1) t = 1;
      float e = 1.0f - (1.0f - t) * (1.0f - t); /* мягко к концу */
      if (a->kind == UI_A_HOVER) {
        SetPropW(a->h, L"CpUiHot", (HANDLE)(INT_PTR)(a->from + (int)((float)(a->to - a->from) * e)));
        if (IsWindowVisible(a->h)) InvalidateRect(a->h, NULL, FALSE);
        done = t >= 1;
      } else {
        BYTE cur = 0;
        DWORD fl = 0;
        if (!GetLayeredWindowAttributes(a->h, NULL, &cur, &fl) || !(fl & LWA_ALPHA) || cur != (BYTE)a->last) {
          done = TRUE; /* прозрачность поменяли без нас — не мешаем */
        } else {
          int v = a->from + (int)((float)(a->to - a->from) * e);
          SetLayeredWindowAttributes(a->h, 0, (BYTE)v, LWA_ALPHA);
          a->last = v;
          if (t >= 1) {
            done = TRUE;
            if (!a->keep) {
              SetWindowLongW(a->h, GWL_EXSTYLE, GetWindowLongW(a->h, GWL_EXSTYLE) & ~WS_EX_LAYERED);
              RedrawWindow(a->h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            }
          }
        }
      }
    }
    if (done) {
      g_uiAnim[i] = g_uiAnim[--g_uiAnimN];
      continue;
    }
    i++;
  }
  if (!g_uiAnimN && g_uiTimer) { /* всё доиграло — таймер не нужен */
    KillTimer(NULL, g_uiTimer);
    g_uiTimer = 0;
  }
}

/* ---- кнопки: подсветка под мышью ------------------------------------------- */
static LRESULT CALLBACK ui_btn_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
  (void)ref;
  switch (m) {
  case WM_MOUSEMOVE:
    if (!GetPropW(h, L"CpUiTrk")) {
      TRACKMOUSEEVENT te;
      te.cbSize = sizeof(te);
      te.dwFlags = TME_LEAVE;
      te.hwndTrack = h;
      te.dwHoverTime = 0;
      if (TrackMouseEvent(&te)) SetPropW(h, L"CpUiTrk", (HANDLE)1);
      ui_hover_to(h, 255);
    }
    break;
  case WM_MOUSELEAVE:
    RemovePropW(h, L"CpUiTrk");
    ui_hover_to(h, 0);
    break;
  case WM_ENABLE:
  case WM_SHOWWINDOW:
    if (!w) { /* выключенная или спрятанная кнопка не остаётся подсвеченной */
      RemovePropW(h, L"CpUiTrk");
      int i = ui_anim_find(h, UI_A_HOVER);
      if (i >= 0) g_uiAnim[i] = g_uiAnim[--g_uiAnimN];
      RemovePropW(h, L"CpUiHot");
    }
    break;
  case WM_NCDESTROY:
    ui_anim_forget(h);
    RemovePropW(h, L"CpUiTrk");
    RemovePropW(h, L"CpUiHot");
    RemovePropW(h, L"CpUiSub");
    RemoveWindowSubclass(h, ui_btn_proc, id);
    break;
  }
  return DefSubclassProc(h, m, w, l);
}

/* кнопка начинает следить за мышью при первой отрисовке — так подсветка
   есть у всех кнопок, как бы и где их ни создали */
static void ui_btn_track(HWND h) {
  if (!h || GetPropW(h, L"CpUiSub")) return;
  if (SetWindowSubclass(h, ui_btn_proc, 0x5542, 0)) SetPropW(h, L"CpUiSub", (HANDLE)1);
}

/* ---- поля ввода и списки: тонкая рамка, при фокусе — цветом акцента -------- */
/* толщина рамки вне клиентской части: по стилю не узнать — поле ввода со
   стилями снимает WS_BORDER и рисует рамку внутри себя */
static int ui_nc_border(HWND h) {
  RECT wr;
  GetWindowRect(h, &wr);
  POINT o = {0, 0};
  ClientToScreen(h, &o);
  int b = o.x - wr.left;
  return b < 0 ? 0 : (b > 2 ? 2 : b);
}

static COLORREF ui_hairline(void) { return blend_rgb(g_uiTok.line, g_uiTok.ink, 30); }

static void ui_edit_frame(HWND h) {
  if (knight_on()) return;
  LONG st = GetWindowLongW(h, GWL_STYLE);
  int bw = ui_nc_border(h);
  if (!bw) return;
  RECT r;
  GetWindowRect(h, &r);
  OffsetRect(&r, -r.left, -r.top);
  HDC dc = GetWindowDC(h);
  if (!dc) return;
  BOOL foc = GetFocus() == h && IsWindowEnabled(h);
  HBRUSH b = CreateSolidBrush(foc ? g_uiTok.accent : ui_hairline());
  FrameRect(dc, &r, b);
  DeleteObject(b);
  if (bw == 2) {
    InflateRect(&r, -1, -1);
    if (foc) { /* фокус — двойная линия акцента */
      b = CreateSolidBrush(g_uiTok.accent);
      FrameRect(dc, &r, b);
      DeleteObject(b);
    } else {
      wchar_t cls[16];
      HBRUSH under = NULL;
      if (GetClassNameW(h, cls, 16) && !_wcsicmp(cls, L"Edit"))
        under = (HBRUSH)SendMessageW(GetParent(h), (st & ES_READONLY) ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT,
                                     (WPARAM)dc, (LPARAM)h);
      FrameRect(dc, &r, (under && GetObjectType(under) == OBJ_BRUSH) ? under : GetSysColorBrush(COLOR_WINDOW));
    }
  }
  ReleaseDC(h, dc);
}

/* поле без рамки вне клиентской части (или с WS_BORDER, который поле со
   стилями рисует внутри себя): рамка — по краю самого поля */
static void ui_edit_flat(HWND h) {
  if (knight_on()) return;
  RECT r;
  GetClientRect(h, &r);
  HDC dc = GetDC(h);
  if (!dc) return;
  BOOL foc = GetFocus() == h && IsWindowEnabled(h);
  HBRUSH b = CreateSolidBrush(foc ? g_uiTok.accent : ui_hairline());
  FrameRect(dc, &r, b);
  DeleteObject(b);
  ReleaseDC(h, dc);
}

/* высота строки текста поля — по его шрифту */
static int ui_edit_text_h(HWND h) {
  HDC dc = GetDC(h);
  if (!dc) return 16;
  HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
  HGDIOBJ of = f ? SelectObject(dc, f) : NULL;
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  if (of) SelectObject(dc, of);
  ReleaseDC(h, dc);
  return tm.tmHeight;
}

/* однострочное поле без рамки (Настройки, окно курсора): поля сверху и снизу
   уходят в неклиентскую часть — текст стоит посередине высоты, рамка
   рисуется по краю */
static void ui_edit_pad_paint(HWND h) {
  RECT wr;
  GetWindowRect(h, &wr);
  POINT o = {0, 0};
  ClientToScreen(h, &o);
  RECT cr;
  GetClientRect(h, &cr);
  OffsetRect(&cr, o.x - wr.left, o.y - wr.top);
  OffsetRect(&wr, -wr.left, -wr.top);
  HDC dc = GetWindowDC(h);
  if (!dc) return;
  ExcludeClipRect(dc, cr.left, cr.top, cr.right, cr.bottom);
  HBRUSH under = (HBRUSH)SendMessageW(GetParent(h), WM_CTLCOLOREDIT, (WPARAM)dc, (LPARAM)h);
  FillRect(dc, &wr, (under && GetObjectType(under) == OBJ_BRUSH) ? under : GetSysColorBrush(COLOR_WINDOW));
  if (!knight_on()) {
    BOOL foc = GetFocus() == h && IsWindowEnabled(h);
    HBRUSH b = CreateSolidBrush(foc ? g_uiTok.accent : ui_hairline());
    FrameRect(dc, &wr, b);
    if (foc) { /* фокус — линия снизу толще, как у полей Windows 11 */
      RECT u = {wr.left + 1, wr.bottom - 2, wr.right - 1, wr.bottom - 1};
      FillRect(dc, &u, b);
    }
    DeleteObject(b);
  }
  ReleaseDC(h, dc);
}

static LRESULT CALLBACK ui_edit_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
  (void)ref;
  int flat = (int)(INT_PTR)GetPropW(h, L"CpUiFlat");
  switch (m) {
  case WM_NCCALCSIZE:
    if (flat == 2 && w) {
      LRESULT res = DefSubclassProc(h, m, w, l);
      RECT *r = &((NCCALCSIZE_PARAMS *)l)->rgrc[0];
      int th = ui_edit_text_h(h), avail = r->bottom - r->top;
      int pad = (avail - th) / 2;
      if (pad < 1) pad = 1;
      r->top += pad;
      if (r->top + th < r->bottom - 1) r->bottom = r->top + th;
      else r->bottom -= 1;
      r->left += 1;
      r->right -= 1;
      if (r->bottom < r->top) r->bottom = r->top;
      return res;
    }
    break;
  case WM_PAINT:
    if (flat == 1) {
      LRESULT res = DefSubclassProc(h, m, w, l);
      ui_edit_flat(h);
      return res;
    }
    break;
  case WM_NCPAINT: {
    if (flat == 2) {
      ui_edit_pad_paint(h);
      return 0;
    }
    LRESULT res = DefSubclassProc(h, m, w, l); /* полосы прокрутки рисует сам элемент */
    ui_edit_frame(h);
    return res;
  }
  case WM_SETFOCUS:
  case WM_KILLFOCUS: {
    LRESULT res = DefSubclassProc(h, m, w, l);
    RedrawWindow(h, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_NOCHILDREN);
    return res;
  }
  case WM_NCDESTROY:
    RemoveWindowSubclass(h, ui_edit_proc, id);
    break;
  }
  return DefSubclassProc(h, m, w, l);
}

static BOOL CALLBACK ui_skin_cb(HWND c, LPARAM lp) {
  (void)lp;
  wchar_t cls[32];
  if (!GetClassNameW(c, cls, 32)) return TRUE;
  if (!_wcsicmp(cls, L"ComboBox")) {
    ui_combo_theme(c);
    return TRUE;
  }
  if (!_wcsicmp(cls, L"Button")) { /* флажок: его родитель рисует его в тёмных темах */
    HWND par = GetParent(c);
    if (par && ui_is_check(c) && !GetPropW(par, L"CpUiChk")) {
      SetPropW(par, L"CpUiChk", (HANDLE)1);
      SetWindowSubclass(par, ui_check_parent_proc, 0x5547, 0);
    }
    if (ui_is_check(c)) InvalidateRect(c, NULL, TRUE);
    return TRUE;
  }
  BOOL edit = !_wcsicmp(cls, L"Edit") || !_wcsicmp(cls, L"RICHEDIT50W") || !_wcsicmp(cls, L"RichEdit20W");
  BOOL list = !wcscmp(cls, L"SysListView32") || !wcscmp(cls, L"SysTreeView32");
  if (!edit && !list && wcscmp(cls, L"ListBox")) return TRUE;
  /* полосы прокрутки — при каждой смене темы (светлая ↔ тёмная) */
  if (list || (GetWindowLongW(c, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL | ES_MULTILINE))) ui_scroll_theme(c, list);
  if (GetPropW(c, L"CpUiSkin")) return TRUE;
  SetPropW(c, L"CpUiSkin", (HANDLE)1);
  if (ui_nc_border(c) > 0 || GetPropW(c, L"CpUiFlat")) {
    SetWindowSubclass(c, ui_edit_proc, 0x5545, 0);
    if ((INT_PTR)GetPropW(c, L"CpUiFlat") == 2) { /* поля сверху и снизу — пересчитать */
      SendMessageW(c, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(5, 5));
      SetWindowPos(c, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    RedrawWindow(c, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_NOCHILDREN);
  }
  return TRUE;
}

/* ---- открытие окна ------------------------------------------------------------ */
static HHOOK g_uiHookShow;

/* свои окна, кроме тех, что живут своей прозрачностью: само окно курсора
   (прозрачность следования), эффекты, бой, аватар, выбор области */
static BOOL ui_our_window(HWND h) {
  wchar_t cls[64];
  if (!GetClassNameW(h, cls, 64) || wcsncmp(cls, L"CursorPad", 9) != 0) return FALSE;
  static const wchar_t *const skip[] = {L"CursorPadWindow", L"CursorPadFx", L"CursorPadIdleFight",
                                        L"CursorPadFloatingAvatar", L"CursorPadPick", L"CursorPadPreview",
                                        /* свои прозрачные окна: пузырь помощника, кольцо конвертера */
                                        L"CursorPadAvatarBubble", L"CursorPadAvatarMenu", L"CursorPadMiniChat",
                                        L"CursorPadConvertRing"};
  for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
    if (!wcscmp(cls, skip[i])) return FALSE;
  return TRUE;
}

/* «Чертёж» (с 2026.10.31): у окон нет ни системной рамки, ни тени — свои окна прозрачные
   (WS_EX_LAYERED для появления), а им DWM рамку не рисует, — и белое поле сливалось с белым
   столом. Поэтому по периметру — сплошная линия цвета чернил, как рамка листа: 2 точки
   при 100 %, толще с масштабом */
static void ui_sheet_frame(HDC dc, RECT rc, HWND h) {
  if (!chertezh_on() || !dc) return;
  int t = ui_px(h, 2);
  if (t < 2) t = 2;
  HBRUSH ink = CreateSolidBrush(COL_INK);
  RECT e[4] = {{rc.left, rc.top, rc.right, rc.top + t},
               {rc.left, rc.bottom - t, rc.right, rc.bottom},
               {rc.left, rc.top, rc.left + t, rc.bottom},
               {rc.right - t, rc.top, rc.right, rc.bottom}};
  for (int i = 0; i < 4; i++) FillRect(dc, &e[i], ink);
  DeleteObject(ink);
}

/* рамка листа — после отрисовки самого окна; у окон с системным заголовком своя рамка Windows */
static LRESULT CALLBACK ui_frame_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
  (void)id;
  (void)ref;
  switch (m) {
  case WM_PAINT: {
    LRESULT r = DefSubclassProc(h, m, w, l);
    if (chertezh_on() && (GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) != WS_CAPTION) {
      HDC dc = GetDC(h);
      RECT rc;
      GetClientRect(h, &rc);
      ui_sheet_frame(dc, rc, h);
      ReleaseDC(h, dc);
    }
    return r;
  }
  case WM_SIZE:
    if (chertezh_on()) InvalidateRect(h, NULL, FALSE); /* рамка уезжает вместе с краем */
    break;
  case WM_NCDESTROY:
    RemoveWindowSubclass(h, ui_frame_proc, 0x5548);
    break;
  }
  return DefSubclassProc(h, m, w, l);
}

static void ui_chrome_once(HWND h) {
  if (!GetPropW(h, L"CpUiFrame")) {
    SetPropW(h, L"CpUiFrame", (HANDLE)1);
    SetWindowSubclass(h, ui_frame_proc, 0x5548, 0);
  }
  if ((int)(INT_PTR)GetPropW(h, L"CpUiGen") == g_uiGen) return;
  ui_class_brush_fix(h);
  ui_window_chrome(h);
  EnumChildWindows(h, ui_skin_cb, 0);
  SetPropW(h, L"CpUiGen", (HANDLE)(INT_PTR)g_uiGen);
}

/* окно вот-вот покажется: хром, рамки полей и быстрое появление — с 60 %
   непрозрачности до полной за 120 мс (с нуля не начинаем: если программа
   сразу займётся долгой работой, окно всё равно видно) */
static void ui_on_show(HWND h) {
  if (GetWindowLongW(h, GWL_STYLE) & WS_CHILD) return;
  if (IsIconic(h) || !ui_our_window(h)) return;
  ui_chrome_once(h);
  int i = ui_anim_find(h, UI_A_FADE);
  if (i >= 0) { /* показали снова, пока ещё проявлялось */
    g_uiAnim[i].t0 = GetTickCount();
    g_uiAnim[i].last = g_uiAnim[i].from;
    SetLayeredWindowAttributes(h, 0, (BYTE)g_uiAnim[i].from, LWA_ALPHA);
    return;
  }
  LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
  int target = 255;
  BOOL keep = FALSE;
  if (ex & WS_EX_LAYERED) {
    BYTE a = 0;
    DWORD fl = 0;
    if (!GetLayeredWindowAttributes(h, NULL, &a, &fl) || !(fl & LWA_ALPHA) || (fl & LWA_COLORKEY) || a < 40) return;
    target = a;
    keep = TRUE;
  }
  i = ui_anim_add(h, UI_A_FADE);
  if (i < 0) return;
  if (!keep) SetWindowLongW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
  UiAnim *a = &g_uiAnim[i];
  a->from = target * 60 / 100;
  a->to = target;
  a->last = a->from;
  a->dur = 120;
  a->keep = keep;
  SetLayeredWindowAttributes(h, 0, (BYTE)a->from, LWA_ALPHA);
  ui_timer_on();
}

static LRESULT CALLBACK ui_show_hook(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION) {
    const CWPSTRUCT *c = (const CWPSTRUCT *)lp;
    if (c->message == WM_WINDOWPOSCHANGING) {
      const WINDOWPOS *p = (const WINDOWPOS *)c->lParam;
      if (p && (p->flags & SWP_SHOWWINDOW) && !IsWindowVisible(c->hwnd)) ui_on_show(c->hwnd);
    } else if (c->message == WM_NCDESTROY) {
      ui_anim_forget(c->hwnd);
    } else if (c->message == WM_NCCREATE) {
      /* как поле ввода задумано, видно только при создании: с WS_BORDER его
         рамку поле со стилями рисует само и стиль снимает */
      const CREATESTRUCTW *cs = (const CREATESTRUCTW *)c->lParam;
      wchar_t pc[32];
      if (cs && (cs->style & WS_CHILD) && !(cs->dwExStyle & WS_EX_CLIENTEDGE) && cs->lpszClass &&
          !IS_INTRESOURCE(cs->lpszClass) && !_wcsicmp(cs->lpszClass, L"Edit") && cs->hwndParent &&
          GetClassNameW(cs->hwndParent, pc, 32) && !wcsncmp(pc, L"CursorPad", 9)) {
        /* без рамки — только однострочные поля Настроек и окна курсора:
           у расчётов и TIFF поля стоят в своих нарисованных рамках */
        BOOL plain = !(cs->style & ES_MULTILINE) && (!wcscmp(pc, L"CursorPadSettings") || !wcscmp(pc, L"CursorPadWindow"));
        if (cs->style & WS_BORDER) SetPropW(c->hwnd, L"CpUiFlat", (HANDLE)1); /* рамка внутри поля */
        else if (plain) SetPropW(c->hwnd, L"CpUiFlat", (HANDLE)2);          /* рамка и поля — снаружи */
      }
    }
  }
  return CallNextHookEx(g_uiHookShow, code, wp, lp);
}

static void ui_init(void) {
  ui_sync_tokens();
  if (!g_uiHookShow) g_uiHookShow = SetWindowsHookExW(WH_CALLWNDPROC, ui_show_hook, NULL, GetCurrentThreadId());
}

static BOOL CALLBACK ui_retheme_cb(HWND h, LPARAM lp) {
  (void)lp;
  if (!ui_our_window(h)) return TRUE;
  ui_chrome_once(h);
  if (IsWindowVisible(h)) RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
  return TRUE;
}

/* тема сменилась: токены, хром и рамки всех открытых окон */
static void ui_theme_changed(void) {
  ui_sync_tokens();
  EnumThreadWindows(GetCurrentThreadId(), ui_retheme_cb, 0);
  /* само окно курсора не проявляется (у него своя прозрачность), а рамки
     его полей — те же, что везде */
  if (g_hwnd) EnumChildWindows(g_hwnd, ui_skin_cb, 0);
}

/* что лежит под кнопкой: у темы с шапкой-полосой в шапке — её цвет, иначе
   фон родителя (с узором, если он есть) — скруглённые углы кнопки сливаются
   с тем, что вокруг. Высоту шапки окно кладёт в свойство CpUiHead, фон
   можно задать свойством CpUiBg (1 — светлый, 2 — тёмный). */
static void ui_underlay(UiCanvas *cv, HWND parent, RECT pr) {
  int hh = parent ? (int)(INT_PTR)GetPropW(parent, L"CpUiHead") : 0;
  int bgKind = parent ? (int)(INT_PTR)GetPropW(parent, L"CpUiBg") : 0;
  HBRUSH br;
  if (hh > 0 && pr.top < hh && kThemes[g_theme].head == HEAD_BAND) br = g_paperDark;
  else if (bgKind) br = bg_brush(bgKind == 2);
  else br = parent ? (HBRUSH)GetClassLongPtrW(parent, GCLP_HBRBACKGROUND) : NULL;
  if (!br || GetObjectType(br) != OBJ_BRUSH) br = bg_brush(FALSE);
  SetBrushOrgEx(cv->dc, -cv->lx, -cv->ly, NULL);
  FillRect(cv->dc, &pr, br);
}

/* ---- ползунки (прозрачность в Настройках) ------------------------------------ */
/* дорожка — тонкая линия, пройденная часть цветом акцента; бегунок — белый
   кружок с каймой акцента, под мышью кайма чуть темнее */
static BOOL ui_trackbar_draw(const NMHDR *nh, LRESULT *res) {
  if (!nh || nh->code != (UINT)NM_CUSTOMDRAW || knight_on()) return FALSE;
  wchar_t cls[32];
  if (!GetClassNameW(nh->hwndFrom, cls, 32) || wcscmp(cls, TRACKBAR_CLASSW)) return FALSE;
  const NMCUSTOMDRAW *cd = (const NMCUSTOMDRAW *)nh;
  if (cd->dwDrawStage == CDDS_PREPAINT) {
    *res = CDRF_NOTIFYITEMDRAW;
    return TRUE;
  }
  if (cd->dwDrawStage != CDDS_ITEMPREPAINT) return FALSE;
  HWND tb = nh->hwndFrom;
  RECT cl;
  GetClientRect(tb, &cl);
  UiShape s;
  memset(&s, 0, sizeof(s));
  s.r = -1;
  s.fillA = 255;
  if (cd->dwItemSpec == TBCD_CHANNEL) {
    RECT th;
    SendMessageW(tb, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
    int cy = (cd->rc.top + cd->rc.bottom) / 2, hh = ui_px(tb, 2);
    RECT bar = {cd->rc.left, cy - hh, cd->rc.right, cy + hh};
    RECT box = bar;
    InflateRect(&box, 1, 1);
    IntersectRect(&box, &box, &cl);
    UiCanvas cv;
    if (!ui_canvas_begin(&cv, cd->hdc, box, box.left, box.top, TRUE)) return FALSE;
    s.fill = blend_rgb(g_uiTok.line, g_uiTok.ink, 30);
    ui_shape(&cv, bar, &s);
    RECT on = bar;
    on.right = (th.left + th.right) / 2;
    if (on.right > on.left + 2) {
      s.fill = g_uiTok.accent;
      ui_shape(&cv, on, &s);
    }
    ui_canvas_end(&cv, cd->hdc);
    *res = CDRF_SKIPDEFAULT;
    return TRUE;
  }
  if (cd->dwItemSpec == TBCD_THUMB) {
    RECT th = cd->rc;
    UiCanvas cv;
    RECT box = th;
    IntersectRect(&box, &box, &cl);
    if (!ui_canvas_begin(&cv, cd->hdc, box, box.left, box.top, TRUE)) return FALSE;
    int w = th.right - th.left, h = th.bottom - th.top, d = (w < h ? w : h) - 1;
    RECT knob = {th.left + (w - d) / 2, th.top + (h - d) / 2, th.left + (w - d) / 2 + d, th.top + (h - d) / 2 + d};
    BOOL hot = (cd->uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
    s.fill = RGB(255, 255, 255);
    s.border = hot ? blend_rgb(g_uiTok.accent, g_uiTok.ink, 50) : g_uiTok.accent;
    s.borderA = 255;
    s.bw = (float)ui_px(tb, 2);
    ui_shape(&cv, knob, &s);
    ui_canvas_end(&cv, cd->hdc);
    *res = CDRF_SKIPDEFAULT;
    return TRUE;
  }
  return FALSE;
}
