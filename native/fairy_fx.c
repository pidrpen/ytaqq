/* ---- «Фея»: при нажатии — облачко разноцветных звёздочек --------------------

   Пока выбран курсор «Фея», каждое нажатие мыши (левой или правой, где
   угодно на экране) выпускает из точки клика горсть звёздочек и искорок:
   они разлетаются, мерцают, чуть оседают вниз и гаснут меньше чем за секунду.

   Звёздочки рисуются в прозрачном окне поверх всего. Окно пропускает мышь
   насквозь (WS_EX_TRANSPARENT) и не забирает фокус, так что на само нажатие
   эффект не влияет. Нажатия ловит низкоуровневый хук мыши — он ставится,
   только пока выбрана фея и включены искорки, и сразу же передаёт событие
   дальше: здесь только отправляется сообщение главному окну.

   Картинки звёзд готовятся один раз при запуске (маска с мягким краем),
   кадр — это наложение масок нужного цвета на пустой буфер; на четыре
   искры это доли миллисекунды. */

#define WM_FAIRY_CLICK (WM_APP + 17)
#define FX_WINDOWS 4    /* столько облачков может лететь одновременно */
#define FX_PARTS 4   /* четыре звёздочки (было 34, потом 7) */
#define FX_FRAMES 22    /* ~0.35 с при 16 мс на кадр (было 44 — медленно) */
#define FX_SPR 8        /* 4 размера × (пятиконечная звезда, четырёхлучевая искра) */

typedef struct {
  float x, y, vx, vy;
  int spr, delay;
  float tw;
  BYTE r, g, b;
} FxPart;

typedef struct {
  HWND w;
  HDC dc;
  HBITMAP bmp, old;
  BYTE *bits;
  int size, frame;
  BOOL busy;
  POINT org;
  FxPart p[FX_PARTS];
} FxWin;

static FxWin g_fx[FX_WINDOWS];
static HHOOK g_fxHook;
/* g_sparkle («звёздочки при нажатии», меню у часов) — в cursorpad.c: его читают настройки */
static BYTE *g_fxShape[FX_SPR], *g_fxCore[FX_SPR], *g_fxRim[FX_SPR];
static int g_fxW[FX_SPR];
static float g_fxScale = 1.0f;
static unsigned g_fxSeed = 12345;

static float fx_rand(void) {
  g_fxSeed = g_fxSeed * 1103515245u + 12345u;
  return (float)((g_fxSeed >> 8) & 0xFFFF) / 65535.0f;
}

static BOOL fx_inside(const float *px, const float *py, int n, float x, float y) {
  BOOL in = FALSE;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    if (((py[i] > y) != (py[j] > y)) &&
        (x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i]))
      in = !in;
  }
  return in;
}

/* маска звезды с мягким краем (4×4 выборки на точку) и белая серединка */
static void fx_star_poly(float *px, float *py, float c, float r, int points, float inner) {
  for (int i = 0; i < points * 2; i++) {
    float rr = (i % 2) ? r * inner : r;
    float a = (float)(-M_PI / 2 + i * M_PI / points);
    px[i] = c + rr * cosf(a);
    py[i] = c + rr * sinf(a);
  }
}

static BYTE fx_cover(const float *px, const float *py, int n, int x, int y) {
  int hit = 0;
  for (int sy = 0; sy < 4; sy++)
    for (int sx = 0; sx < 4; sx++)
      hit += fx_inside(px, py, n, x + (sx + 0.5f) / 4.0f, y + (sy + 0.5f) / 4.0f);
  return (BYTE)(hit * 255 / 16);
}

/* Три маски на звезду: сама звезда, кайма чуть шире (темнее тем же цветом —
   чтобы звезду было видно на белом) и маленькая светлая серединка. */
static void fx_make_sprite(int k, float r, int points, float inner) {
  float rimR = r + 1.0f * g_fxScale;
  int w = (int)(rimR * 2.0f + 3.0f);
  g_fxW[k] = w;
  g_fxShape[k] = (BYTE *)calloc((size_t)w * w, 1);
  g_fxCore[k] = (BYTE *)calloc((size_t)w * w, 1);
  g_fxRim[k] = (BYTE *)calloc((size_t)w * w, 1);
  if (!g_fxShape[k] || !g_fxCore[k] || !g_fxRim[k]) return;
  float px[16], py[16], qx[16], qy[16];
  float c = w / 2.0f;
  int n = points * 2;
  fx_star_poly(px, py, c, r, points, inner);
  fx_star_poly(qx, qy, c, rimR, points, inner + 0.08f);
  for (int y = 0; y < w; y++) {
    for (int x = 0; x < w; x++) {
      g_fxShape[k][y * w + x] = fx_cover(px, py, n, x, y);
      g_fxRim[k][y * w + x] = fx_cover(qx, qy, n, x, y);
      float d = sqrtf((x + 0.5f - c) * (x + 0.5f - c) + (y + 0.5f - c) * (y + 0.5f - c));
      float core = 1.0f - d / (r * 0.28f);
      g_fxCore[k][y * w + x] = (BYTE)(core > 0 ? (core > 1 ? 180 : core * 180) : 0);
    }
  }
}

static void fx_init_sprites(void) {
  if (g_fxShape[0]) return;
  HDC s = GetDC(NULL);
  g_fxScale = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
  if (s) ReleaseDC(NULL, s);
  if (g_fxScale < 1.0f) g_fxScale = 1.0f;
  /* мелкие: было 4.5…11 — крупно */
  static const float sizes[4] = {3.0f, 3.8f, 4.6f, 5.5f};
  for (int i = 0; i < 4; i++) {
    fx_make_sprite(i * 2, sizes[i] * g_fxScale, 5, 0.45f);     /* звёздочка */
    fx_make_sprite(i * 2 + 1, sizes[i] * g_fxScale, 4, 0.28f); /* искра */
  }
}

/* кадр: пустой буфер и поверх — каждая звёздочка своим цветом */
static void fx_render(FxWin *f) {
  int S = f->size;
  memset(f->bits, 0, (size_t)S * S * 4);
  /* вспышка в точке нажатия: розовое колечко расходится и тает */
  if (f->frame < 6) {
    float t = f->frame / 6.0f;
    float rad = (4.0f + 22.0f * t) * g_fxScale, thick = (2.8f - 1.6f * t) * g_fxScale;
    float ra = 0.75f * (1.0f - t);
    int c0 = S / 2, span = (int)(rad + thick + 2);
    for (int y = c0 - span; y <= c0 + span; y++) {
      if (y < 0 || y >= S) continue;
      for (int x = c0 - span; x <= c0 + span; x++) {
        if (x < 0 || x >= S) continue;
        float d = sqrtf((float)((x - c0) * (x - c0) + (y - c0) * (y - c0)));
        float k = 1.0f - fabsf(d - rad) / thick;
        if (k <= 0) continue;
        float sa = k * ra;
        BYTE *px = f->bits + ((size_t)y * S + x) * 4;
        px[0] = (BYTE)(220 * sa);
        px[1] = (BYTE)(120 * sa);
        px[2] = (BYTE)(255 * sa);
        px[3] = (BYTE)(255 * sa);
      }
    }
  }
  for (int i = 0; i < FX_PARTS; i++) {
    FxPart *p = &f->p[i];
    int age = f->frame - p->delay;
    if (age < 0) continue;
    float t = (float)age / (FX_FRAMES - p->delay);
    if (t >= 1.0f) continue;
    float fade = 1.0f - t * t; /* держится яркой и гаснет к концу */
    /* мерцание: у каждой звезды свой ритм */
    float tw = 0.80f + 0.20f * sinf(age * 0.85f + p->tw);
    float a = fade * tw;
    if (age < 2) a *= (age + 1) / 2.0f; /* вспыхивает, а не появляется рывком */
    int w = g_fxW[p->spr];
    BYTE *sh = g_fxShape[p->spr], *co = g_fxCore[p->spr], *rim = g_fxRim[p->spr];
    if (!sh || !rim) continue;
    int x0 = (int)(p->x - w / 2.0f), y0 = (int)(p->y - w / 2.0f);
    for (int y = 0; y < w; y++) {
      int yy = y0 + y;
      if (yy < 0 || yy >= S) continue;
      BYTE *row = f->bits + (size_t)yy * S * 4;
      for (int x = 0; x < w; x++) {
        int xx = x0 + x;
        if (xx < 0 || xx >= S) continue;
        int m = sh[y * w + x], mr = rim[y * w + x];
        if (!mr) continue;
        /* кайма — тот же цвет вдвое темнее, внутри — сам цвет с белой искрой */
        float fill = m / 255.0f;
        float sa = mr / 255.0f * a;
        float wc = co[y * w + x] / 255.0f;
        float lr = p->r * 0.5f, lg = p->g * 0.5f, lb = p->b * 0.5f;
        float cr0 = lr + (p->r - lr) * fill, cg0 = lg + (p->g - lg) * fill, cb0 = lb + (p->b - lb) * fill;
        float cr = cr0 + (255 - cr0) * wc, cg = cg0 + (255 - cg0) * wc, cb = cb0 + (255 - cb0) * wc;
        BYTE *d = row + xx * 4;
        float inv = 1.0f - sa;
        /* буфер с домноженной альфой — как его ждёт UpdateLayeredWindow */
        d[0] = (BYTE)(cb * sa + d[0] * inv);
        d[1] = (BYTE)(cg * sa + d[1] * inv);
        d[2] = (BYTE)(cr * sa + d[2] * inv);
        d[3] = (BYTE)(255 * sa + d[3] * inv);
      }
    }
  }
  POINT src = {0, 0};
  SIZE sz = {S, S};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(f->w, NULL, &f->org, &sz, f->dc, &src, 0, &bf, ULW_ALPHA);
}

static void fx_step(FxWin *f) {
  for (int i = 0; i < FX_PARTS; i++) {
    FxPart *p = &f->p[i];
    if (f->frame < p->delay) continue;
    p->x += p->vx;
    p->y += p->vy;
    p->vx *= 0.88f;
    p->vy = p->vy * 0.88f + 0.06f * g_fxScale; /* волшебная пыль чуть оседает */
  }
}

static LRESULT CALLBACK FxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_TIMER) {
    for (int k = 0; k < FX_WINDOWS; k++) {
      FxWin *f = &g_fx[k];
      if (f->w != hwnd || !f->busy) continue;
      f->frame++;
      if (f->frame >= FX_FRAMES) {
        KillTimer(hwnd, 1);
        ShowWindow(hwnd, SW_HIDE);
        f->busy = FALSE;
        return 0;
      }
      fx_step(f);
      fx_render(f);
    }
    return 0;
  }
  if (msg == WM_NCHITTEST) return HTTRANSPARENT;
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL fx_prepare(FxWin *f, int S) {
  if (f->w && f->size == S) return TRUE;
  static BOOL reg = FALSE;
  if (!reg) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = FxProc;
    wc.hInstance = g_inst;
    wc.lpszClassName = L"CursorPadFx";
    RegisterClassExW(&wc);
    reg = TRUE;
  }
  if (!f->w)
    f->w = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                               WS_EX_NOACTIVATE,
                           L"CursorPadFx", L"", WS_POPUP, 0, 0, S, S, NULL, NULL, g_inst, NULL);
  if (!f->w) return FALSE;
  if (f->dc) {
    SelectObject(f->dc, f->old);
    DeleteObject(f->bmp);
    DeleteDC(f->dc);
    f->dc = NULL;
  }
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = S;
  bi.bmiHeader.biHeight = -S; /* сверху вниз */
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  HDC scr = GetDC(NULL);
  f->dc = CreateCompatibleDC(scr);
  void *bits = NULL;
  f->bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  ReleaseDC(NULL, scr);
  if (!f->dc || !f->bmp || !bits) return FALSE;
  f->old = (HBITMAP)SelectObject(f->dc, f->bmp);
  f->bits = (BYTE *)bits;
  f->size = S;
  return TRUE;
}

/* облачко из точки нажатия */
static void fx_burst(int x, int y) {
  fx_init_sprites();
  static const BYTE pal[][3] = {
      {255, 105, 180}, {255, 215, 64},  {186, 120, 255}, {90, 200, 255},
      {80, 230, 170},  {255, 140, 90},  {255, 255, 255}, {255, 70, 140},
  };
  FxWin *f = NULL;
  for (int k = 0; k < FX_WINDOWS && !f; k++)
    if (!g_fx[k].busy) f = &g_fx[k];
  if (!f) { /* все заняты — берём то, что почти догорело */
    f = &g_fx[0];
    for (int k = 1; k < FX_WINDOWS; k++)
      if (g_fx[k].frame > f->frame) f = &g_fx[k];
  }
  int S = (int)(200 * g_fxScale);
  if (!fx_prepare(f, S)) return;
  g_fxSeed ^= GetTickCount() * 2654435761u;
  f->org.x = x - S / 2;
  f->org.y = y - S / 2;
  f->frame = 0;
  /* цвета без повторов: перемешанная палитра */
  int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  for (int i = 7; i > 0; i--) {
    int j = (int)(fx_rand() * (i + 1)) % (i + 1);
    int t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  float base = fx_rand() * 6.2832f;
  for (int i = 0; i < FX_PARTS; i++) {
    FxPart *p = &f->p[i];
    /* их мало — поэтому по кругу почти равномерно, чтобы не слиплись */
    float ang = base + i * (6.2832f / FX_PARTS) + (fx_rand() - 0.5f) * 0.6f;
    float sp = (2.6f + fx_rand() * 1.8f) * g_fxScale; /* быстро разлетаются */
    p->x = S / 2.0f + (fx_rand() - 0.5f) * 4.0f;
    p->y = S / 2.0f + (fx_rand() - 0.5f) * 4.0f;
    p->vx = cosf(ang) * sp;
    p->vy = sinf(ang) * sp - 0.8f * g_fxScale; /* чуть вверх: как фонтанчик */
    p->spr = (int)(fx_rand() * FX_SPR) % FX_SPR;
    p->delay = 0;
    p->tw = fx_rand() * 6.2832f;
    const BYTE *c = pal[order[i % 8]];
    p->r = c[0];
    p->g = c[1];
    p->b = c[2];
  }
  f->busy = TRUE;
  fx_render(f);
  SetWindowPos(f->w, HWND_TOPMOST, f->org.x, f->org.y, S, S,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetTimer(f->w, 1, 16, NULL);
}

static LRESULT CALLBACK fx_mouse_hook(int code, WPARAM wParam, LPARAM lParam) {
  if (code == HC_ACTION && (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN)) {
    const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)lParam;
    if (g_hwnd)
      PostMessageW(g_hwnd, WM_FAIRY_CLICK, (WPARAM)(LONG_PTR)m->pt.x, (LPARAM)(LONG_PTR)m->pt.y);
  }
  return CallNextHookEx(g_fxHook, code, wParam, lParam);
}

/* хук стоит, только пока он нужен: фея выбрана и искорки включены */
static void fx_sync(void) {
  BOOL want = g_skin == 3 && g_sparkle;
  if (want && !g_fxHook) g_fxHook = SetWindowsHookExW(WH_MOUSE_LL, fx_mouse_hook, g_inst, 0);
  else if (!want && g_fxHook) {
    UnhookWindowsHookEx(g_fxHook);
    g_fxHook = NULL;
  }
}

static void fx_shutdown(void) {
  if (g_fxHook) UnhookWindowsHookEx(g_fxHook);
  g_fxHook = NULL;
  for (int k = 0; k < FX_WINDOWS; k++) {
    FxWin *f = &g_fx[k];
    if (f->w) DestroyWindow(f->w);
    if (f->dc) {
      SelectObject(f->dc, f->old);
      DeleteObject(f->bmp);
      DeleteDC(f->dc);
    }
    memset(f, 0, sizeof(*f));
  }
}
