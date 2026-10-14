/* ---- Стили курсоров, нарисованные кодом (с 2026.10.14.2) ------------------

   «Неон», «Лёд», «Золото», «Лава» и «Космос» — пять наборов курсоров без
   картинок: все шестнадцать видов (стрелка, текст, рука, ожидание, размеры…)
   рисуются при первом выборе под размер указателя (32 / 48 / 64 точки) и
   ставятся так же, как наборы из .cur. Форма у всех одна, отличается отделка:

     Неон   — тёмная трубка с бирюзовой каймой и малиновым свечением;
     Лёд    — светло-голубой кристалл с гранями и тёмно-синим контуром;
     Золото — золотой слиток с бликом;
     Лава   — от жёлтого к тёмно-красному, с прожилками и оранжевым жаром;
     Космос — фиолетовая ночь со звёздами и сиреневой каймой.

   У каждого свой эффект при нажатии, если в Настройках выбрано «Как у
   курсора» (fx_kind_for). Здесь же — окошко просмотра в Настройках: в нём
   курсор водит сам и щёлкает, показывая стиль и эффект до выбора. */

/* ST_COUNT и ST_SKIN0 (g_skin первого стиля) — в cursorpad.c */
#define ST_SLOTS RIAS_SLOTS /* виды — в том же порядке, что у «Риас» (kRias) */


static HCURSOR g_stCur[ST_COUNT][ST_SLOTS];
static int g_stSize[ST_COUNT]; /* под какой размер указателя нарисованы */

/* ---- формы в сетке 32×32 ---- */

static BOOL st_poly(const float *xy, int n, float x, float y) {
  BOOL in = FALSE;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float xi = xy[i * 2], yi = xy[i * 2 + 1], xj = xy[j * 2], yj = xy[j * 2 + 1];
    if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) in = !in;
  }
  return in;
}

static BOOL st_disc(float x, float y, float cx, float cy, float r) {
  return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

static BOOL st_rect(float x, float y, float x0, float y0, float x1, float y1) {
  return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

static const float kStArrow[] = {3, 2, 3, 24, 8.2f, 19.2f, 11.8f, 27.2f, 15.4f, 25.6f, 11.8f, 17.8f, 18.8f, 17.8f};
static const float kStHand[] = {11.5f, 3,  12.6f, 1.6f, 14.4f, 1.6f, 15.5f, 3,  15.5f, 12, 17.4f, 11.4f,
                                19, 12.4f, 20.6f, 12, 22.2f, 13.2f, 24, 13.6f, 25, 15.4f, 25, 22,
                                22, 28.5f, 11, 28.5f, 5.6f, 20.4f, 4.6f, 17.2f, 6.6f, 16.4f, 9.6f, 18.6f, 11.5f, 18.6f};
static const float kStWE[] = {2, 16, 9, 9, 9, 13.5f, 23, 13.5f, 23, 9, 30, 16, 23, 23, 23, 18.5f, 9, 18.5f, 9, 23};
static const float kStUp[] = {16, 2, 25, 12, 19, 12, 19, 29, 13, 29, 13, 12, 7, 12};
#define ST_N(a) ((int)(sizeof(a) / sizeof(a[0]) / 2))

static BOOL st_arrow(float x, float y) { return st_poly(kStArrow, ST_N(kStArrow), x, y); }
static BOOL st_we(float x, float y) { return st_poly(kStWE, ST_N(kStWE), x, y); }

/* попадает ли точка (x, y) сетки 32×32 в вид slot (порядок kRias) */
static BOOL st_inside(int slot, float x, float y) {
  const float r2 = 0.70710678f;
  switch (slot) {
  case 0: return st_arrow(x, y);                                       /* стрелка */
  case 1:                                                               /* текст */
    return st_rect(x, y, 11, 3, 21, 6) || st_rect(x, y, 14.5f, 3, 17.5f, 29) || st_rect(x, y, 11, 26, 21, 29);
  case 2: return st_poly(kStHand, ST_N(kStHand), x, y);                 /* рука */
  case 3: return st_arrow(x, y) || st_disc(x, y, 25, 24, 5);            /* справка */
  case 4: {                                                             /* нельзя */
    float d2 = (x - 16) * (x - 16) + (y - 16) * (y - 16);
    if (d2 <= 12.5f * 12.5f && d2 >= 8.5f * 8.5f) return TRUE;
    float u = ((x - 16) - (y - 16)) * r2;
    return d2 <= 10.5f * 10.5f && fabsf(u) <= 2.0f;
  }
  case 5: return st_rect(x, y, 14.5f, 3, 17.5f, 29) || st_rect(x, y, 3, 14.5f, 29, 17.5f); /* крест */
  case 6: return st_we(x, y);                                           /* ↔ */
  case 7: return st_we(y, x);                                           /* ↕ */
  case 8: case 9: {                                                      /* ↘ и ↗ */
    float dx = x - 16, dy = y - 16, s = slot == 8 ? 1.0f : -1.0f;
    return st_we(16 + (dx + s * dy) * r2, 16 + (-s * dx + dy) * r2);
  }
  case 10: return st_we(x, y) || st_we(y, x);                           /* во все стороны */
  case 11: return st_poly(kStUp, ST_N(kStUp), x, y);                    /* ↑ */
  case 12:                                                              /* место */
    return st_arrow(x, y) || st_disc(x, y, 25, 21, 4.5f) ||
           (y >= 23 && y <= 30 && fabsf(x - 25) <= (30 - y) * 0.55f);
  case 13:                                                              /* человек */
    return st_arrow(x, y) || st_disc(x, y, 25, 19, 3) || (st_disc(x, y, 25, 29.5f, 5.5f) && y <= 29.5f);
  case 14: {                                                            /* ожидание */
    float d2 = (x - 16) * (x - 16) + (y - 16) * (y - 16);
    return d2 <= 12 * 12 && d2 >= 7 * 7;
  }
  default: {                                                            /* фоновая работа */
    float d2 = (x - 25) * (x - 25) + (y - 24) * (y - 24);
    return st_arrow(x, y) || (d2 <= 6 * 6 && d2 >= 3.2f * 3.2f);
  }
  }
}

static void st_hotspot(int slot, int *hx, int *hy) {
  static const BYTE hs[ST_SLOTS][2] = {{3, 2},   {16, 16}, {13, 2},  {3, 2},  {16, 16}, {16, 16}, {16, 16}, {16, 16},
                                        {16, 16}, {16, 16}, {16, 16}, {16, 2}, {3, 2},   {3, 2},   {16, 16}, {3, 2}};
  *hx = hs[slot][0];
  *hy = hs[slot][1];
}

/* ---- отделка ---- */

typedef struct {
  BYTE top[3], mid[3], bot[3]; /* заливка сверху вниз */
  BYTE rim[3];                 /* кайма */
  BYTE glow[3];                /* свечение вокруг */
  float rimW, glowA;           /* ширина каймы (в точках сетки 32) и сила свечения */
  int decor;                   /* 0 нет, 1 грани льда, 2 блик золота, 3 прожилки лавы, 4 звёзды */
} StLook;

static const StLook kLooks[ST_COUNT] = {
    {{14, 16, 40}, {22, 14, 52}, {36, 10, 60}, {0, 240, 255}, {255, 40, 200}, 1.7f, 0.80f, 0},
    {{244, 252, 255}, {200, 232, 250}, {140, 200, 240}, {30, 80, 135}, {170, 230, 255}, 1.2f, 0.45f, 1},
    {{255, 242, 170}, {236, 186, 48}, {168, 108, 10}, {84, 52, 0}, {255, 210, 90}, 1.2f, 0.25f, 2},
    {{255, 236, 120}, {255, 120, 10}, {160, 18, 0}, {40, 6, 0}, {255, 96, 0}, 1.2f, 0.55f, 3},
    {{86, 36, 160}, {48, 20, 110}, {18, 10, 56}, {226, 206, 255}, {150, 90, 255}, 1.1f, 0.45f, 4},
};

static float st_mix(float a, float b, float t) { return a + (b - a) * t; }

static unsigned st_hash(int x, int y) {
  unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

/* картинка вида slot стиля st в N×N, BGRA без домножения альфы */
static void st_draw(int st, int slot, int N, BYTE *out) {
  const StLook *L = &kLooks[st];
  float k = N / 32.0f;
  size_t n = (size_t)N * N;
  float *M = (float *)calloc(n * 3, sizeof(float));
  if (!M) {
    memset(out, 0, n * 4);
    return;
  }
  float *D = M + n, *G = D + n;
  /* форма: 4×4 выборки на точку */
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int hit = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++)
          hit += st_inside(slot, (x + (sx + 0.5f) / 4.0f) / k, (y + (sy + 0.5f) / 4.0f) / k);
      M[(size_t)y * N + x] = hit / 16.0f;
    }
  /* кайма — форма, раздутая на rimW; свечение — мягкий ореол вокруг каймы */
  float ro = L->rimW * k, rg = 3.2f * k;
  int ri = (int)ceilf(ro), gi = (int)ceilf(rg);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float best = 0.0f;
      for (int dy = -ri; dy <= ri; dy++)
        for (int dx = -ri; dx <= ri; dx++) {
          int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= N || yy >= N) continue;
          float w = fx_clamp01(ro + 0.5f - sqrtf((float)(dx * dx + dy * dy)));
          float v = M[(size_t)yy * N + xx] * w;
          if (v > best) best = v;
        }
      D[(size_t)y * N + x] = best;
    }
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float best = 0.0f;
      for (int dy = -gi; dy <= gi; dy++)
        for (int dx = -gi; dx <= gi; dx++) {
          int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= N || yy >= N) continue;
          float w = 1.0f - sqrtf((float)(dx * dx + dy * dy)) / (rg + 0.5f);
          if (w <= 0.0f) continue;
          float v = D[(size_t)yy * N + xx] * w * w;
          if (v > best) best = v;
        }
      G[(size_t)y * N + x] = best;
    }
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      size_t i = (size_t)y * N + x;
      /* слои снизу вверх, считаем с домноженной альфой */
      float a = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
#define ST_OVER(cr, cg, cb, ca)                                                                            \
  do {                                                                                                     \
    float ca_ = (ca), inv_ = 1.0f - ca_;                                                                   \
    r = (cr) * ca_ + r * inv_, g = (cg) * ca_ + g * inv_, b = (cb) * ca_ + b * inv_, a = ca_ + a * inv_;   \
  } while (0)
      if (L->glowA > 0.0f && G[i] > 0.0f) ST_OVER(L->glow[0], L->glow[1], L->glow[2], G[i] * L->glowA);
      if (D[i] > 0.0f) ST_OVER(L->rim[0], L->rim[1], L->rim[2], D[i]);
      if (M[i] > 0.0f) {
        float t = (y + 0.5f) / N, fr, fg, fb;
        if (t < 0.5f) {
          fr = st_mix(L->top[0], L->mid[0], t * 2), fg = st_mix(L->top[1], L->mid[1], t * 2),
          fb = st_mix(L->top[2], L->mid[2], t * 2);
        } else {
          fr = st_mix(L->mid[0], L->bot[0], t * 2 - 1), fg = st_mix(L->mid[1], L->bot[1], t * 2 - 1),
          fb = st_mix(L->mid[2], L->bot[2], t * 2 - 1);
        }
        float ux = (x + 0.5f) / k, uy = (y + 0.5f) / k; /* в сетке 32 */
        if (L->decor == 1 && fmodf(ux + uy * 0.6f, 7.0f) < 1.4f) { /* грани льда */
          fr = st_mix(fr, 255, 0.5f), fg = st_mix(fg, 255, 0.5f), fb = st_mix(fb, 255, 0.5f);
        } else if (L->decor == 2) { /* блик по слитку */
          float band = ux - uy * 0.35f;
          if (band > 3.5f && band < 6.5f) fr = st_mix(fr, 255, 0.55f), fg = st_mix(fg, 255, 0.55f), fb = st_mix(fb, 240, 0.55f);
        } else if (L->decor == 3 && fabsf(sinf(ux * 0.9f + sinf(uy * 0.5f) * 2.0f)) < 0.13f) { /* прожилки */
          fr = st_mix(fr, 70, 0.6f), fg = st_mix(fg, 0, 0.6f), fb = st_mix(fb, 0, 0.6f);
        } else if (L->decor == 4 && M[i] >= 1.0f && st_hash((int)ux, (int)uy) % 23 == 0 &&
                   st_hash(x, y) % 3 == 0) { /* звёзды */
          fr = 255, fg = 250, fb = 235;
        }
        ST_OVER(fr, fg, fb, M[i]);
      }
#undef ST_OVER
      BYTE *o = out + i * 4;
      if (a <= 0.004f) {
        o[0] = o[1] = o[2] = o[3] = 0;
      } else {
        o[0] = (BYTE)fminf(255.0f, b / a);
        o[1] = (BYTE)fminf(255.0f, g / a);
        o[2] = (BYTE)fminf(255.0f, r / a);
        o[3] = (BYTE)(a * 255.0f + 0.5f);
      }
    }
  free(M);
}

static HCURSOR st_make(int st, int slot, int N) {
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = N;
  bi.bmiHeader.biHeight = -N;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void *bits = NULL;
  HDC scr = GetDC(NULL);
  HBITMAP color = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  ReleaseDC(NULL, scr);
  if (!color || !bits) {
    if (color) DeleteObject(color);
    return NULL;
  }
  st_draw(st, slot, N, (BYTE *)bits);
  HBITMAP mask = CreateBitmap(N, N, 1, 1, NULL);
  if (!mask) {
    DeleteObject(color);
    return NULL;
  }
  { /* маска пустая: всё решает альфа */
    HDC mdc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(mdc, mask);
    PatBlt(mdc, 0, 0, N, N, BLACKNESS);
    SelectObject(mdc, old);
    DeleteDC(mdc);
  }
  int hx, hy;
  st_hotspot(slot, &hx, &hy);
  ICONINFO ii;
  memset(&ii, 0, sizeof(ii));
  ii.fIcon = FALSE;
  ii.xHotspot = (DWORD)(hx * N / 32);
  ii.yHotspot = (DWORD)(hy * N / 32);
  ii.hbmMask = mask;
  ii.hbmColor = color;
  HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color);
  return c;
}

static int st_cursor_size(void) {
  int sz = GetSystemMetrics(SM_CXCURSOR);
  return sz < 32 ? 32 : (sz > 128 ? 128 : sz);
}

/* курсоры стиля под нынешний размер указателя; рисуются один раз */
static void st_ensure(int st) {
  if (st < 0 || st >= ST_COUNT) return;
  int N = st_cursor_size();
  if (g_stSize[st] == N && g_stCur[st][0]) return;
  for (int i = 0; i < ST_SLOTS; i++) {
    if (g_stCur[st][i]) DestroyCursor(g_stCur[st][i]);
    g_stCur[st][i] = st_make(st, i, N);
  }
  g_stSize[st] = N;
}

/* поставить стиль вместо курсоров Windows; SetSystemCursor забирает курсор
   себе, поэтому ставится копия, а нарисованный остаётся для следующего раза */
static void style_apply(int st) {
  st_ensure(st);
  for (int i = 0; i < ST_SLOTS; i++) set_slot(g_stCur[st][i], kRias[i].ocr);
}

static void styles_free(void) {
  for (int s = 0; s < ST_COUNT; s++)
    for (int i = 0; i < ST_SLOTS; i++)
      if (g_stCur[s][i]) {
        DestroyCursor(g_stCur[s][i]);
        g_stCur[s][i] = NULL;
      }
}

/* ---- просмотр в Настройках ----------------------------------------------

   Окошко в разделе «Курсор и эффекты». Курсор сам ходит по нему и раз в
   секунду щёлкает — видно и стиль, и эффект при нажатии. Пока мышь над
   кнопкой курсора, эффекта или сетапа, показано то, что она выберет, а не то,
   что выбрано сейчас. Щелчок по окошку — эффект прямо там. */

#define PV_FRAMES_PER_CLICK 62
static HWND g_pvHwnd;
static FxWin g_pvFx;     /* облачко просмотра: только буфер, окна у него нет */
static POINT g_pvAt;     /* где оно в окошке */
static BOOL g_pvFxOn;
static int g_pvTick;
static int g_pvSkin = -1, g_pvPick = -2; /* что показано; -2 — ещё ничего */
static BOOL g_pvHover;   /* показан вариант под мышью, а не выбранный */
static HCURSOR g_pvRias[3];
static HCURSOR g_pvWin[3];

/* для просмотра: стрелка, рука и текст набора skin */
static HCURSOR pv_cursor(int skin, int which) {
  static const int slotOf[3] = {0, 2, 1}; /* в порядке kRias: стрелка, рука, текст */
  if (skin >= ST_SKIN0 && skin < ST_SKIN0 + ST_COUNT) {
    st_ensure(skin - ST_SKIN0);
    return g_stCur[skin - ST_SKIN0][slotOf[which]];
  }
  if (skin >= 1 && skin <= 5) {
    int s = skin - 1;
    return which == 0 ? g_staticCur[s] : which == 1 ? g_handCur[s] : g_ibeamCur[s];
  }
  if (skin == 6) {
    if (!g_pvRias[which]) {
      static const wchar_t *const nm[3] = {L"static", L"hand", L"ibeam"};
      wchar_t path[MAX_PATH], dir[MAX_PATH];
      int sz = GetSystemMetrics(SM_CXCURSOR);
      if (g_dataDir[0]) {
        _snwprintf(path, MAX_PATH, L"%s\\k7_%s.ani", g_dataDir, nm[which]);
        g_pvRias[which] = (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, sz, sz, LR_LOADFROMFILE);
      }
      if (!g_pvRias[which]) {
        exe_dir(dir, MAX_PATH);
        _snwprintf(path, MAX_PATH, L"%s\\cursors\\k7_%s.ani", dir, nm[which]);
        g_pvRias[which] = (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, sz, sz, LR_LOADFROMFILE);
      }
    }
    return g_pvRias[which];
  }
  /* Windows: из папки Cursors — системные сейчас могут быть подменены нашими */
  if (!g_pvWin[which]) {
    static const wchar_t *const fn[3] = {L"aero_arrow.cur", L"aero_link.cur", L"beam_r.cur"};
    wchar_t win[MAX_PATH], path[MAX_PATH];
    if (GetWindowsDirectoryW(win, MAX_PATH)) {
      _snwprintf(path, MAX_PATH, L"%s\\Cursors\\%s", win, fn[which]);
      g_pvWin[which] = (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    }
  }
  if (g_pvWin[which]) return g_pvWin[which];
  return LoadCursorW(NULL, which == 0 ? IDC_ARROW : which == 1 ? IDC_HAND : IDC_IBEAM);
}

static void pv_draw_cursor(HDC dc, HCURSOR c, int x, int y) {
  if (!c) return;
  ICONINFO ii;
  int hx = 0, hy = 0;
  if (GetIconInfo(c, &ii)) {
    hx = (int)ii.xHotspot;
    hy = (int)ii.yHotspot;
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
  }
  DrawIconEx(dc, x - hx, y - hy, c, 0, 0, 0, NULL, DI_NORMAL);
}

static int settings_skin_of_id(int id); /* cursorpad.c: кнопка какого курсора; -1 — не курсора */

/* что показывать: вариант под мышью или выбранное */
static void pv_target(int *skin, int *pick) {
  *skin = g_skin;
  *pick = g_fxPick;
  g_pvHover = FALSE;
  POINT pt;
  if (!GetCursorPos(&pt)) return;
  HWND h = WindowFromPoint(pt);
  if (!h || GetParent(h) != g_setHwnd) return;
  int id = GetDlgCtrlID(h);
  int sk = settings_skin_of_id(id);
  if (sk >= 0) {
    *skin = sk;
    g_pvHover = TRUE;
  } else if (id >= ID_FX_BASE && id < ID_FX_BASE + FX_OPTIONS) {
    *pick = kFxPick[id - ID_FX_BASE];
    g_pvHover = TRUE;
  } else if (id >= ID_SETUP_BASE && id < ID_SETUP_BASE + SETUP_COUNT) {
    *skin = g_setups[id - ID_SETUP_BASE].skin;
    *pick = g_setups[id - ID_SETUP_BASE].fx;
    g_pvHover = TRUE;
  }
}

/* где сейчас бегающий курсор: плавная восьмёрка по правой части окошка */
static void pv_demo_pos(const RECT *rc, int tick, int *x, int *y) {
  float t = tick * 0.022f;
  int left = 76, w = rc->right - left - 30, h = rc->bottom - 54;
  *x = left + 15 + (int)((0.5f + 0.42f * sinf(t)) * w);
  *y = 30 + (int)((0.5f + 0.38f * sinf(t * 2.0f)) * h);
}

static void pv_click(int x, int y) {
  int skin, pick;
  pv_target(&skin, &pick);
  int kind = fx_kind_for(skin, pick);
  if (!kind) {
    g_pvFxOn = FALSE;
    return;
  }
  fx_init_sprites();
  int S = (int)(200 * g_fxScale);
  if (g_pvFx.W != S) {
    free(g_pvFx.bits);
    memset(&g_pvFx, 0, sizeof(g_pvFx));
    g_pvFx.bits = (BYTE *)malloc((size_t)S * S * 4);
    if (!g_pvFx.bits) return;
    g_pvFx.W = g_pvFx.H = S;
  }
  g_fxSeed ^= GetTickCount() * 2654435761u;
  fx_seed(&g_pvFx, kind, S);
  g_pvAt.x = x;
  g_pvAt.y = y;
  g_pvFxOn = TRUE;
}

static void pv_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int W = rc.right, H = rc.bottom;
  if (W <= 0 || H <= 0) return;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = W;
  bi.bmiHeader.biHeight = -H;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void *bits = NULL;
  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!mem || !bmp || !bits) {
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    return;
  }
  HGDIOBJ old = SelectObject(mem, bmp);
  /* подложка: светлая, чуть темнее книзу, — на ней видно и светлые, и тёмные курсоры */
  COLORREF c0 = COL_PAPER, c1 = blend_rgb(COL_PAPER, COL_PAPER_DARK, 256);
  DWORD *px = (DWORD *)bits;
  for (int y = 0; y < H; y++) {
    COLORREF c = blend_rgb(c0, c1, y * 256 / H);
    DWORD v = (DWORD)GetBValue(c) | ((DWORD)GetGValue(c) << 8) | ((DWORD)GetRValue(c) << 16);
    for (int x = 0; x < W; x++) px[(size_t)y * W + x] = v;
  }
  /* эффект: облачко с домноженной альфой поверх подложки */
  if (g_pvFxOn && g_pvFx.bits) {
    fx_paint(&g_pvFx);
    int S = g_pvFx.W, ox = g_pvAt.x - S / 2, oy = g_pvAt.y - S / 2;
    for (int y = 0; y < S; y++) {
      int yy = oy + y;
      if (yy < 0 || yy >= H) continue;
      const BYTE *s = g_pvFx.bits + (size_t)y * S * 4;
      BYTE *d = (BYTE *)bits + (size_t)yy * W * 4;
      for (int x = 0; x < S; x++) {
        int xx = ox + x;
        if (xx < 0 || xx >= W) continue;
        const BYTE *sp = s + x * 4;
        if (!sp[3]) continue;
        BYTE *dp = d + xx * 4;
        int inv = 255 - sp[3];
        dp[0] = (BYTE)(sp[0] + dp[0] * inv / 255);
        dp[1] = (BYTE)(sp[1] + dp[1] * inv / 255);
        dp[2] = (BYTE)(sp[2] + dp[2] * inv / 255);
      }
    }
  }
  int skin, pick;
  pv_target(&skin, &pick);
  /* слева столбиком: стрелка, рука, текст — в настоящий размер */
  {
    RECT col = {8, 26, 66, H - 26};
    HBRUSH b = CreateSolidBrush(blend_rgb(COL_PAPER, COL_PAPER_DARK, 160));
    FillRect(mem, &col, b);
    DeleteObject(b);
    int step = (col.bottom - col.top) / 3;
    for (int i = 0; i < 3; i++) {
      HCURSOR c = pv_cursor(skin, i);
      if (!c) continue;
      int sz = GetSystemMetrics(SM_CXCURSOR);
      DrawIconEx(mem, 37 - sz / 2, col.top + step * i + step / 2 - sz / 2, c, 0, 0, 0, NULL, DI_NORMAL);
    }
  }
  /* бегающий курсор: рука, пока «нажимает», иначе стрелка */
  {
    int x, y;
    pv_demo_pos(&rc, g_pvTick, &x, &y);
    int phase = g_pvTick % PV_FRAMES_PER_CLICK;
    pv_draw_cursor(mem, pv_cursor(skin, phase < 8 ? 1 : 0), x, y);
  }
  /* подписи: что показано и как пользоваться */
  SetBkMode(mem, TRANSPARENT);
  if (g_fontUi) SelectObject(mem, g_fontUi);
  SetTextColor(mem, COL_INK);
  wchar_t cap[96];
  int kind = fx_kind_for(skin, pick);
  _snwprintf(cap, 96, L"%s%s · %s", g_pvHover ? L"Наведено: " : L"Сейчас: ", skin_name(skin), fx_pick_label(kind));
  RECT tr = {10, 4, W - 8, 24};
  DrawTextW(mem, cap, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (g_fontSmall) SelectObject(mem, g_fontSmall);
  SetTextColor(mem, COL_MUTED);
  RECT hr = {10, H - 22, W - 8, H - 4};
  DrawTextW(mem, L"Наведите на вариант — покажу его здесь", -1, &hr,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  /* рамка */
  HPEN pen = CreatePen(PS_SOLID, 1, COL_LINE);
  HGDIOBJ op = SelectObject(mem, pen);
  HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
  Rectangle(mem, 0, 0, W, H);
  SelectObject(mem, ob);
  SelectObject(mem, op);
  DeleteObject(pen);
  BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
}

static LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_TIMER: {
    /* окно Настроек спрятано или раздел свёрнут — не крутимся зря */
    if (!IsWindowVisible(hwnd) || !g_setHwnd || !IsWindowVisible(g_setHwnd)) {
      KillTimer(hwnd, 1);
      return 0;
    }
    int skin, pick;
    pv_target(&skin, &pick);
    if (skin != g_pvSkin || pick != g_pvPick) { /* показ сменился — сразу щелчок */
      g_pvSkin = skin;
      g_pvPick = pick;
      g_pvTick = PV_FRAMES_PER_CLICK - 1;
    }
    g_pvTick++;
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (g_pvTick % PV_FRAMES_PER_CLICK == 0) {
      int x, y;
      pv_demo_pos(&rc, g_pvTick, &x, &y);
      pv_click(x, y);
    } else if (g_pvFxOn) {
      g_pvFx.frame++;
      if (g_pvFx.frame >= g_pvFx.frames) g_pvFxOn = FALSE;
      else fx_step(&g_pvFx);
    }
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }
  case WM_LBUTTONDOWN:
    pv_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    pv_paint(hwnd, hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DESTROY:
    KillTimer(hwnd, 1);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND preview_create(HWND parent, int id) {
  static BOOL reg = FALSE;
  if (!reg) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PreviewProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); /* над окошком — выбранная стрелка: есть с чем сравнить */
    wc.lpszClassName = L"CursorPadPreview";
    RegisterClassExW(&wc);
    reg = TRUE;
  }
  g_pvHwnd = CreateWindowExW(0, L"CursorPadPreview", L"", WS_CHILD | WS_VISIBLE, 0, 0, 200, 140, parent,
                             (HMENU)(INT_PTR)id, g_inst, NULL);
  return g_pvHwnd;
}

/* окошко снова на виду — пусть оживает */
static void preview_wake(void) {
  if (g_pvHwnd && IsWindowVisible(g_pvHwnd) && g_setHwnd && IsWindowVisible(g_setHwnd))
    SetTimer(g_pvHwnd, 1, 16, NULL);
}

#ifdef SET_TEST
/* только проверочная сборка: --gallery файл.bmp — все виды пяти стилей и кадры
   всех эффектов на одном листе, чтобы посмотреть их без мыши */
static void st_gallery(const wchar_t *path) {
  fx_init_sprites();
  int S = (int)(200 * g_fxScale), N = 48, cell = 56;
  int W = S * 4 > cell * ST_SLOTS ? S * 4 : cell * ST_SLOTS;
  int rowsCur = ST_COUNT * 2, H = rowsCur * cell + 8 * (S / 2);
  BYTE *img = (BYTE *)malloc((size_t)W * H * 4);
  BYTE *cur = (BYTE *)malloc((size_t)N * N * 4);
  FxWin fw;
  memset(&fw, 0, sizeof(fw));
  fw.W = fw.H = S;
  fw.bits = (BYTE *)malloc((size_t)S * S * 4);
  if (!img || !cur || !fw.bits) return;
  for (int i = 0; i < W * H; i++) ((DWORD *)img)[i] = 0xFFFFFFFFu;
  /* курсоры: каждый стиль — на белом и на тёмном */
  for (int st = 0; st < ST_COUNT; st++)
    for (int bg = 0; bg < 2; bg++)
      for (int sl = 0; sl < ST_SLOTS; sl++) {
        st_draw(st, sl, N, cur);
        int ox = sl * cell + 4, oy = (st * 2 + bg) * cell + 4;
        BYTE back = bg ? 40 : 255;
        for (int y = 0; y < N; y++)
          for (int x = 0; x < N; x++) {
            const BYTE *s = cur + ((size_t)y * N + x) * 4;
            BYTE *d = img + ((size_t)(oy + y) * W + ox + x) * 4;
            for (int c = 0; c < 3; c++) d[c] = (BYTE)((s[c] * s[3] + back * (255 - s[3])) / 255);
          }
      }
  /* эффекты 1…8: по четыре кадра, половина облачка */
  for (int kind = 1; kind <= 8; kind++) {
    g_fxSeed = 777u + kind;
    fx_seed(&fw, kind, S);
    int at[4] = {2, fw.frames / 4, fw.frames / 2, fw.frames * 3 / 4};
    for (int k = 0; k < 4; k++) {
      while (fw.frame < at[k]) {
        fw.frame++;
        fx_step(&fw);
      }
      fx_paint(&fw);
      int oy = rowsCur * cell + (kind - 1) * (S / 2);
      for (int y = 0; y < S / 2; y++)
        for (int x = 0; x < S; x++) {
          const BYTE *s = fw.bits + ((size_t)(y + S / 4) * S + x) * 4;
          BYTE *d = img + ((size_t)(oy + y) * W + k * S + x) * 4;
          for (int c = 0; c < 3; c++) d[c] = (BYTE)(s[c] + d[c] * (255 - s[3]) / 255);
        }
    }
  }
  BITMAPFILEHEADER fh;
  BITMAPINFOHEADER ih;
  memset(&fh, 0, sizeof(fh));
  memset(&ih, 0, sizeof(ih));
  ih.biSize = sizeof(ih);
  ih.biWidth = W;
  ih.biHeight = -H;
  ih.biPlanes = 1;
  ih.biBitCount = 32;
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(ih);
  fh.bfSize = fh.bfOffBits + (DWORD)W * H * 4;
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD w;
    WriteFile(h, &fh, sizeof(fh), &w, NULL);
    WriteFile(h, &ih, sizeof(ih), &w, NULL);
    WriteFile(h, img, (DWORD)W * H * 4, &w, NULL);
    CloseHandle(h);
  }
  free(img);
  free(cur);
  free(fw.bits);
}
#endif
