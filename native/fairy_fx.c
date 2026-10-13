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
   искры это доли миллисекунды.

   У «Дракона» (с 2026.09.23.94) то же окно и тот же хук, но вместо звёздочек
   из точки нажатия вырывается немного пламени: дюжина мягких язычков
   поднимается вверх, колышется, по пути из жёлтых становятся оранжевыми,
   потом красными, уменьшаются и гаснут примерно за полсекунды. Выключается
   отдельно — в Настройках, в «Эффект при нажатии».

   У «Чёрного кота» (с 2026.09.23.97) — пара царапин: из точки нажатия
   наискосок одна за другой проводятся две светлые когтистые полосы,
   сужающиеся к концам (тёмно-фиолетовый край, сиреневая середина, белая
   жилка), держатся и гаснут примерно за полсекунды. Вместо царапин коту можно
   включить звёздочки, как у Феи (g_catFx == 2), или выключить эффект совсем:
   «Эффект при нажатии» в Настройках.

   «Разрез» (g_fxPick == 4) — красный разрез в духе Сукуны: из точки нажатия
   наискосок проводится один тонкий алый росчерк. Он проводится быстрым взмахом,
   держится и тонеет, закрываясь, примерно за треть секунды.

   Редкие удачи, в среднем: большой разрез через весь монитор, где кликнули, —
   раз в 120 нажатий, толстый и тонкий рядом, с красным свечением; раскол экрана —
   раз в 340: снимок режется большими разрезами на куски, куски разъезжаются и
   падают, экран становится чёрным, потом куски возвращаются на место. На каждое
   нажатие один бросок: раскол проверяется первым, затем большой разрез.
   Всё в том же окне на весь монитор. */

#define WM_FAIRY_CLICK (WM_APP + 17)
#define FX_WINDOWS 4    /* столько облачков может лететь одновременно */
#define FX_ALL (FX_WINDOWS + 1) /* + отдельное окно на весь экран для большого разреза */
#define FX_PARTS 4   /* четыре звёздочки (было 34, потом 7) */
#define FX_FRAMES 22    /* ~0.35 с при 16 мс на кадр (было 44 — медленно) */
#define FX_SPR 8        /* 4 размера × (пятиконечная звезда, четырёхлучевая искра) */
#define FX_FLAMES 12    /* язычков пламени у «Дракона» */
#define FX_FLAME_FRAMES 34 /* ~0.55 с */
#define FX_SCRATCHES 2  /* царапин у «Чёрного кота» */
#define FX_SCRATCH_FRAMES 18 /* ~0.3 с */
#define FX_SLASH_FRAMES 22 /* ~0.35 с: разрез проводится, держится и тонеет */
#define FX_BIG_CUTS 2   /* толстый разрез и тонкая спутница рядом */
#define FX_BIG_FRAMES 40 /* ~0.65 с */
#define FX_BIG_CHANCE (1.0f / 120.0f) /* в среднем раз в 120 нажатий в «Разрезе»: большой разрез через весь монитор */
#define FX_SHATTER_CHANCE (1.0f / 340.0f) /* в среднем раз в 340: раскол экрана; проверяется раньше большого */
#define FX_SHATTER_CUTS 7 /* столько больших разрезов режут экран */
#define FX_SHATTER_PIECES (1 << FX_SHATTER_CUTS) /* кусков — не больше */
#define FX_SHATTER_CUT_FRAMES 8  /* разрезы проводятся поверх экрана */
#define FX_SHATTER_SPLIT_FRAMES 32 /* куски разъезжаются и падают */
#define FX_SHATTER_HOLD_FRAMES 12  /* экран чёрный */
#define FX_SHATTER_BACK_FRAMES 36  /* куски возвращаются на место */
#define FX_SHATTER_FRAMES (FX_SHATTER_CUT_FRAMES + FX_SHATTER_SPLIT_FRAMES + \
                           FX_SHATTER_HOLD_FRAMES + FX_SHATTER_BACK_FRAMES)
#define FX_MAXP 12      /* больше из трёх: звёздочек, язычков и царапин */

typedef struct {
  float x, y, vx, vy;
  int spr, delay;
  float tw;
  float sz;  /* язычок пламени: радиус в начале, точки при 100 % */
  float rev; /* царапина и разрез: за сколько кадров их проводят */
  int life;  /* язычок пламени: сколько кадров горит */
  BYTE r, g, b;
} FxPart;

typedef struct {
  HWND w;
  HDC dc;
  HBITMAP bmp, old;
  BYTE *bits;
  int W, H, frame; /* окно квадратное, кроме большого разреза — он на весь монитор */
  int n, frames; /* сколько частиц и кадров у этого облачка */
  BOOL busy;
  BOOL flame;    /* пламя «Дракона», а не звёздочки «Феи» */
  BOOL scratch;  /* царапины «Чёрного кота» */
  BOOL slash;    /* красный «Разрез» — и обычный, и большой */
  BOOL shatter;  /* раскол экрана на куски (каждое двадцатое нажатие) */
  POINT org;
  FxPart p[FX_MAXP];
} FxWin;

static FxWin g_fx[FX_ALL];
static HHOOK g_fxHook;

/* раскол экрана: снимок в момент нажатия, номер куска у каждого пикселя и
   на сколько каждый кусок уезжает к концу разъезда. Живёт, пока идёт анимация */
typedef struct {
  BYTE *src;   /* экран в момент нажатия, непрозрачный */
  BYTE *shard; /* кусок каждого пикселя: битовая маска «по какую сторону» каждого разреза */
  float outx[FX_SHATTER_PIECES], outy[FX_SHATTER_PIECES];
} FxShatter;
static FxShatter g_shat;

static void fx_shatter_free(void) {
  free(g_shat.src);
  free(g_shat.shard);
  g_shat.src = NULL;
  g_shat.shard = NULL;
}
/* g_sparkle («звёздочки при нажатии»), g_flame («пламя при нажатии») и g_catFx
   (эффект у «Чёрного кота») — в cursorpad.c:
   их читают настройки */
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

static float fx_mix(float a, float b, float t) { return a + (b - a) * t; }

/* язычок пламени: капля острым концом вверх, серединка светлее, край темнее;
   цвет идёт от жёлтого через оранжевый к красному, язычок тает и уменьшается */
static void fx_flame_part(FxWin *f, const FxPart *p, int age) {
  float t = (float)age / p->life;
  if (t >= 1.0f) return;
  static const float stops[4][3] = {{255, 232, 110}, {255, 184, 30}, {244, 96, 20}, {168, 34, 16}};
  static const float at[4] = {0.0f, 0.2f, 0.55f, 1.0f};
  int k = t < at[1] ? 0 : (t < at[2] ? 1 : 2);
  float u = (t - at[k]) / (at[k + 1] - at[k]);
  float cr = fx_mix(stops[k][0], stops[k + 1][0], u);
  float cg = fx_mix(stops[k][1], stops[k + 1][1], u);
  float cb = fx_mix(stops[k][2], stops[k + 1][2], u);
  float a = 0.92f * (1.0f - t * t);
  if (age < 2) a *= (age + 1) / 2.0f;
  float r = p->sz * g_fxScale * (1.0f - 0.55f * t);
  if (r < 0.8f) return;
  int S = f->W; /* язычки только в квадратных окнах */
  int x0 = (int)(p->x - r - 1), x1 = (int)(p->x + r + 1);
  int y0 = (int)(p->y - r * 2.2f - 1), y1 = (int)(p->y + r + 1);
  for (int y = y0; y <= y1; y++) {
    if (y < 0 || y >= S) continue;
    BYTE *row = f->bits + (size_t)y * S * 4;
    for (int x = x0; x <= x1; x++) {
      if (x < 0 || x >= S) continue;
      float dx = (x + 0.5f - p->x) / r;
      float dy = (y + 0.5f - p->y) / r;
      if (dy < 0) { /* кверху язычок вытянут и сужается в острие */
        float w = 1.0f + dy / 2.2f * 0.95f;
        dx /= w > 0.06f ? w : 0.06f;
        dy /= 2.2f;
      }
      float d = sqrtf(dx * dx + dy * dy);
      if (d >= 1.0f) continue;
      float m = 1.0f - d;
      float sa = a * (m < 0.35f ? m / 0.35f : 1.0f); /* мягкий край */
      float hot = m * m * m * (1.0f - t) * 0.45f;     /* светлая серединка */
      float dark = 0.78f + 0.22f * m;                 /* край темнее — видно на белом */
      float pr = fx_mix(cr * dark, 255, hot), pg = fx_mix(cg * dark, 250, hot),
            pb = fx_mix(cb * dark, 220, hot);
      BYTE *d4 = row + x * 4;
      float inv = 1.0f - sa;
      d4[0] = (BYTE)(pb * sa + d4[0] * inv);
      d4[1] = (BYTE)(pg * sa + d4[1] * inv);
      d4[2] = (BYTE)(pr * sa + d4[2] * inv);
      d4[3] = (BYTE)(255 * sa + d4[3] * inv);
    }
  }
}

/* царапина или разрез: отрезок от (x, y) на вектор (vx, vy), шире всего
   посередине и тоньше к концам. Сначала его «проводят» (за p->rev кадров),
   потом он держится и гаснет. У царапин край тёмно-фиолетовый — чтобы было
   видно на белом, середина сиреневая, по оси — белая жилка. У «Разреза»
   (f->slash) — красный: бледная жилка, алая середина, тёмно-бордовый край и
   мягкое красное свечение вокруг */
static void fx_scratch_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  float len = sqrtf(p->vx * p->vx + p->vy * p->vy);
  if (len < 1.0f) return;
  float ux = p->vx / len, uy = p->vy / len;
  float t = (age + 1) / p->rev; /* за p->rev кадров дорисовывается до конца */
  if (t > 1.0f) t = 1.0f;
  /* у красного разреза — быстрый взмах, потом дотягивает; у царапин — ровно */
  float reveal = f->slash ? 1.0f - (1.0f - t) * (1.0f - t) : t;
  if (reveal > 1.0f) reveal = 1.0f;
  float hold = p->life * 0.45f;
  float a = age < hold ? 1.0f : 1.0f - (age - hold) / (p->life - hold);
  /* красный разрез тонеет к концу жизни: закрывается, а не просто гаснет */
  float maxw = p->sz * g_fxScale * (f->slash ? 1.0f - 0.45f * age / (float)p->life : 1.0f);
  float reach = f->slash ? maxw * 1.7f + 3.0f : maxw; /* у красного разреза есть свечение */
  float ex = p->x + p->vx * reveal, ey = p->y + p->vy * reveal;
  int x0 = (int)(fminf(p->x, ex) - reach - 2), x1 = (int)(fmaxf(p->x, ex) + reach + 2);
  int y0 = (int)(fminf(p->y, ey) - reach - 2), y1 = (int)(fmaxf(p->y, ey) + reach + 2);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= f->W) x1 = f->W - 1;
  if (y1 >= f->H) y1 = f->H - 1;
  for (int y = y0; y <= y1; y++) {
    BYTE *row = f->bits + (size_t)y * f->W * 4;
    for (int x = x0; x <= x1; x++) {
      float rx = x + 0.5f - p->x, ry = y + 0.5f - p->y;
      float s = (rx * ux + ry * uy) / len; /* 0..1 вдоль разреза */
      if (s < 0.0f || s > reveal) continue;
      float d = fabsf(-rx * uy + ry * ux);
      if (d >= reach) continue;
      float prof = sinf(3.14159265f * s);
      float hw = maxw * (prof > 0 ? powf(prof, 0.7f) : 0.0f);
      /* головка ещё бежит — мягкий кончик */
      float head = reveal < 1.0f && s > reveal - 0.12f ? (reveal - s) / 0.12f : 1.0f;
      float sa = 0.0f, cr = 0.0f, cg = 0.0f, cb = 0.0f;
      float edge = hw - d; /* пикселей до края внутрь */
      if (edge > 0.0f && hw >= 0.4f) {
        sa = a * (edge < 1.0f ? edge : 1.0f) * head;
        float m = d / hw; /* 0 по оси, 1 на краю */
        if (f->slash) {                      /* бледная жилка, алая середина, бордо по краю */
          if (m < 0.18f) {
            cr = 255, cg = 226, cb = 222;
          } else if (m < 0.50f) {
            float u = (m - 0.18f) / 0.32f;
            cr = fx_mix(255, 226, u), cg = fx_mix(70, 22, u), cb = fx_mix(60, 30, u);
          } else {
            float u = (m - 0.50f) / 0.50f;
            cr = fx_mix(226, 84, u), cg = fx_mix(22, 0, u), cb = fx_mix(30, 14, u);
          }
        } else if (m < 0.18f) {              /* белая жилка */
          cr = 255, cg = 252, cb = 255;
        } else if (m < 0.50f) {              /* сиреневая середина */
          float u = (m - 0.18f) / 0.32f;
          cr = fx_mix(255, 206, u), cg = fx_mix(252, 170, u), cb = fx_mix(255, 255, u);
        } else {                             /* тёмно-фиолетовый край */
          float u = (m - 0.50f) / 0.50f;
          cr = fx_mix(206, 58, u), cg = fx_mix(170, 22, u), cb = fx_mix(255, 104, u);
        }
      } else if (f->slash) {                 /* мимо красного разреза — свечение, тает к краю */
        float gr = hw * 0.7f + 2.5f * g_fxScale;
        float g = 1.0f - fmaxf(d - hw, 0.0f) / gr;
        if (g <= 0.0f) continue;
        sa = 0.42f * a * g * g * head;
        cr = 220, cg = 20, cb = 30;
      } else {
        continue;
      }
      BYTE *d4 = row + x * 4;
      float inv = 1.0f - sa;
      d4[0] = (BYTE)(cb * sa + d4[0] * inv);
      d4[1] = (BYTE)(cg * sa + d4[1] * inv);
      d4[2] = (BYTE)(cr * sa + d4[2] * inv);
      d4[3] = (BYTE)(255 * sa + d4[3] * inv);
    }
  }
}

/* раскол: сколько кусков уехало на этом кадре, от 0 (на месте) до 1 (уехали).
   Сначала разрезы проводятся поверх экрана, потом куски разъезжаются, экран
   чёрный, потом куски возвращаются */
static float fx_shatter_k(int fr) {
  if (fr < FX_SHATTER_CUT_FRAMES) return 0.0f;
  int sp = fr - FX_SHATTER_CUT_FRAMES;
  if (sp < FX_SHATTER_SPLIT_FRAMES) {
    float t = sp / (float)FX_SHATTER_SPLIT_FRAMES;
    return t * t * (3.0f - 2.0f * t);
  }
  sp -= FX_SHATTER_SPLIT_FRAMES;
  if (sp < FX_SHATTER_HOLD_FRAMES) return 1.0f;
  sp -= FX_SHATTER_HOLD_FRAMES;
  if (sp < FX_SHATTER_BACK_FRAMES) {
    float t = sp / (float)FX_SHATTER_BACK_FRAMES;
    return 1.0f - t * t * (3.0f - 2.0f * t);
  }
  return 0.0f;
}

static void fx_shatter_render(FxWin *f) {
  int W = f->W, H = f->H;
  size_t n = (size_t)W * H;
  float k = fx_shatter_k(f->frame);
  if (k <= 0.0f) {
    memcpy(f->bits, g_shat.src, n * 4);
  } else {
    DWORD *dst = (DWORD *)f->bits;
    const DWORD *src = (const DWORD *)g_shat.src;
    int ox[FX_SHATTER_PIECES], oy[FX_SHATTER_PIECES];
    for (int s = 0; s < FX_SHATTER_PIECES; s++) {
      ox[s] = (int)floorf(g_shat.outx[s] * k + 0.5f);
      oy[s] = (int)floorf(g_shat.outy[s] * k + 0.5f);
    }
    for (size_t i = 0; i < n; i++) dst[i] = 0xFF000000u; /* между кусками — чёрное */
    /* каждый пиксель снимка уходит со своим куском; что вышло за край — пропадает */
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        size_t i = (size_t)y * W + x;
        int s = g_shat.shard[i];
        int nx = x + ox[s], ny = y + oy[s];
        if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
        dst[(size_t)ny * W + nx] = src[i];
      }
    }
  }
  /* разрезы поверх экрана; через 12 кадров гаснут окончательно */
  for (int i = 0; i < f->n; i++) fx_scratch_part(f, &f->p[i], f->frame - f->p[i].delay);
}

/* кадр: пустой буфер и поверх — каждая звёздочка своим цветом */
static void fx_render(FxWin *f) {
  int W = f->W, H = f->H;
  int S = W; /* звёздочки, язычки и кольцо — только в квадратных окнах */
  if (f->shatter) {
    fx_shatter_render(f);
    goto show;
  }
  memset(f->bits, 0, (size_t)W * H * 4);
  if (f->scratch || f->slash) {
    for (int i = 0; i < f->n; i++) fx_scratch_part(f, &f->p[i], f->frame - f->p[i].delay);
    goto show;
  }
  if (f->flame) {
    /* сначала тёплая вспышка в точке нажатия, потом язычки — поздние сверху */
    if (f->frame < 5) {
      FxPart glow;
      memset(&glow, 0, sizeof(glow));
      glow.x = S / 2.0f;
      glow.y = S / 2.0f;
      glow.sz = 7.0f + f->frame * 2.0f;
      glow.life = 9;
      fx_flame_part(f, &glow, 3 + f->frame);
    }
    for (int i = f->n - 1; i >= 0; i--) {
      int age = f->frame - f->p[i].delay;
      if (age >= 0) fx_flame_part(f, &f->p[i], age);
    }
    goto show;
  }
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
  for (int i = 0; i < f->n; i++) {
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
show:;
  POINT src = {0, 0};
  SIZE sz = {W, H};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(f->w, NULL, &f->org, &sz, f->dc, &src, 0, &bf, ULW_ALPHA);
}

static void fx_step(FxWin *f) {
  if (f->scratch || f->slash) return; /* царапины и разрезы на месте, только проявляются и гаснут */
  for (int i = 0; i < f->n; i++) {
    FxPart *p = &f->p[i];
    if (f->frame < p->delay) continue;
    if (f->flame) {
      /* огонь рвётся вверх всё быстрее и колышется из стороны в сторону */
      int age = f->frame - p->delay;
      p->x += p->vx + sinf(age * 0.7f + p->tw) * 0.35f * g_fxScale;
      p->y += p->vy;
      p->vx *= 0.90f;
      p->vy = p->vy * 0.96f - 0.07f * g_fxScale;
      continue;
    }
    p->x += p->vx;
    p->y += p->vy;
    p->vx *= 0.88f;
    p->vy = p->vy * 0.88f + 0.06f * g_fxScale; /* волшебная пыль чуть оседает */
  }
}

static LRESULT CALLBACK FxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_TIMER) {
    for (int k = 0; k < FX_ALL; k++) {
      FxWin *f = &g_fx[k];
      if (f->w != hwnd || !f->busy) continue;
      f->frame++;
      if (f->frame >= f->frames) {
        KillTimer(hwnd, 1);
        ShowWindow(hwnd, SW_HIDE);
        f->busy = FALSE;
        if (f->shatter) {
          f->shatter = FALSE;
          fx_shatter_free();
        }
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

static BOOL fx_prepare_wh(FxWin *f, int W, int H) {
  if (f->w && f->W == W && f->H == H) return TRUE;
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
                           L"CursorPadFx", L"", WS_POPUP, 0, 0, W, H, NULL, NULL, g_inst, NULL);
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
  bi.bmiHeader.biWidth = W;
  bi.bmiHeader.biHeight = -H; /* сверху вниз */
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
  f->W = W;
  f->H = H;
  return TRUE;
}

/* какой эффект при нажатии сейчас: 0 нет, 1 звёздочки, 2 пламя, 3 царапины,
   4 разрез. Если эффект выбран в меню отдельно (g_fxPick >= 0), он важнее курсора;
   иначе — как раньше: фея со звёздочками, дракон с пламенем, кот с царапинами */
static int fx_kind(void) {
  if (g_fxPick >= 0) return g_fxPick;
  if (g_skin == 3) return g_sparkle ? 1 : 0;
  if (g_skin == 4) return g_flame ? 2 : 0;
  if (g_skin == 5) return g_catFx == 1 ? 3 : (g_catFx == 2 ? 1 : 0);
  return 0;
}

/* бросить то, что сейчас идёт на большом окне: и разрез, и раскол */
static void fx_big_reset(FxWin *f) {
  if (f->busy) {
    KillTimer(f->w, 1);
    ShowWindow(f->w, SW_HIDE);
    f->busy = FALSE;
  }
  f->shatter = FALSE;
  fx_shatter_free();
}

/* большой разрез на весь монитор, где щёлкнули: по диагонали через середину
   экрана, толстый и рядом тонкий; проводится быстро и гаснет. Окно у него своё
   (g_fx[FX_WINDOWS]), поэтому не мешает обычным облачкам */
static void fx_big_slash(int x, int y) {
  POINT pt = {x, y};
  MONITORINFO mi;
  memset(&mi, 0, sizeof(mi));
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) return;
  FxWin *f = &g_fx[FX_WINDOWS];
  fx_big_reset(f);
  int W = mi.rcMonitor.right - mi.rcMonitor.left;
  int H = mi.rcMonitor.bottom - mi.rcMonitor.top;
  if (!fx_prepare_wh(f, W, H)) return;
  f->org.x = mi.rcMonitor.left;
  f->org.y = mi.rcMonitor.top;
  f->frame = 0;
  f->flame = FALSE;
  f->scratch = FALSE;
  f->slash = TRUE;
  f->n = FX_BIG_CUTS;
  f->frames = FX_BIG_FRAMES;
  /* угол от горизонтали ~26…43°, в одну из сторон */
  float dir = fx_rand() < 0.5f ? 1.0f : -1.0f;
  float ang = 0.45f + fx_rand() * 0.3f;
  float ux = dir * cosf(ang), uy = sinf(ang);
  /* половина длины — до края экрана по той оси, что раньше упрётся (+5 %, чтобы не оборвалось) */
  float tx = (W / 2.0f) / fabsf(ux), ty = (H / 2.0f) / fabsf(uy);
  float t = fminf(tx, ty) * 1.05f;
  for (int i = 0; i < FX_BIG_CUTS; i++) {
    FxPart *p = &f->p[i];
    memset(p, 0, sizeof(*p));
    float off = i == 0 ? 0.0f : 56.0f * g_fxScale; /* спутница — поперёк, в стороне */
    p->x = W / 2.0f - ux * t - uy * off;
    p->y = H / 2.0f - uy * t + ux * off;
    p->vx = ux * 2.0f * t;
    p->vy = uy * 2.0f * t;
    p->sz = i == 0 ? 20.0f : 6.0f; /* полуширина в середине, в пикселях × масштаб */
    p->delay = i * 3;
    p->rev = i == 0 ? 6.0f : 5.0f;
    p->life = FX_BIG_FRAMES - p->delay;
  }
  f->busy = TRUE;
  fx_render(f);
  SetWindowPos(f->w, HWND_TOPMOST, f->org.x, f->org.y, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetTimer(f->w, 1, 16, NULL);
}

/* подготовка раскола: снимок из буфера окна, разрезы через середину, номер куска
   у каждого пикселя и куда он уедет. Вызывается после того, как экран снят */
static BOOL fx_shatter_setup(FxWin *f) {
  int W = f->W, H = f->H;
  size_t n = (size_t)W * H;
  fx_shatter_free();
  g_shat.src = (BYTE *)malloc(n * 4);
  g_shat.shard = (BYTE *)malloc(n);
  if (!g_shat.src || !g_shat.shard) {
    fx_shatter_free();
    return FALSE;
  }
  DWORD *src = (DWORD *)g_shat.src;
  const DWORD *cap = (const DWORD *)f->bits;
  for (size_t i = 0; i < n; i++) src[i] = cap[i] | 0xFF000000u; /* экран непрозрачный */

  /* разрезы: прямые через середину, под разными углами, чуть в стороны */
  float la[FX_SHATTER_CUTS], lb[FX_SHATTER_CUTS], lc[FX_SHATTER_CUTS];
  float cx = W / 2.0f, cy = H / 2.0f;
  float side = fminf((float)W, (float)H);
  float R = sqrtf((float)W * W + (float)H * H) / 2.0f; /* полдиагонали — линия пересечёт весь экран */
  for (int i = 0; i < FX_SHATTER_CUTS; i++) {
    float th = i * 3.14159265f / FX_SHATTER_CUTS + (fx_rand() - 0.5f) * 0.3f;
    float ux = cosf(th), uy = sinf(th); /* вдоль разреза */
    float nx = -uy, ny = ux;            /* поперёк */
    float off = (fx_rand() - 0.5f) * 0.6f * side;
    float qx = cx + nx * off, qy = cy + ny * off;
    la[i] = nx;
    lb[i] = ny;
    lc[i] = -(nx * qx + ny * qy);
    FxPart *p = &f->p[i];
    memset(p, 0, sizeof(*p));
    p->x = qx - ux * R;
    p->y = qy - uy * R;
    p->vx = ux * 2.0f * R;
    p->vy = uy * 2.0f * R;
    p->sz = 5.0f;
    p->rev = 5.0f;
    p->life = FX_SHATTER_CUT_FRAMES + 4;
  }
  f->n = FX_SHATTER_CUTS;

  /* кусок — это набор пикселей с одинаковой маской «по какую сторону» каждого разреза */
  double sumx[FX_SHATTER_PIECES], sumy[FX_SHATTER_PIECES];
  int cnt[FX_SHATTER_PIECES];
  memset(sumx, 0, sizeof(sumx));
  memset(sumy, 0, sizeof(sumy));
  memset(cnt, 0, sizeof(cnt));
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      float px = x + 0.5f, py = y + 0.5f;
      int m = 0;
      for (int i = 0; i < FX_SHATTER_CUTS; i++)
        if (la[i] * px + lb[i] * py + lc[i] > 0.0f) m |= 1 << i;
      g_shat.shard[(size_t)y * W + x] = (BYTE)m;
      sumx[m] += px;
      sumy[m] += py;
      cnt[m]++;
    }
  }
  /* куда уезжает кусок: от середины экрана в сторону его центра, с разбросом,
     и вниз — так, что к концу разъезда он уже за краем */
  for (int m = 0; m < FX_SHATTER_PIECES; m++) {
    float dx0 = 0.0f, dy0 = 0.0f;
    if (cnt[m]) {
      dx0 = (float)(sumx[m] / cnt[m]) - cx;
      dy0 = (float)(sumy[m] / cnt[m]) - cy;
    }
    float ang = atan2f(dy0, dx0) + (fx_rand() - 0.5f) * 0.8f;
    float dist = side * (0.25f + fx_rand() * 0.15f);
    g_shat.outx[m] = cosf(ang) * dist;
    g_shat.outy[m] = sinf(ang) * dist + H * (1.6f + fx_rand() * 0.3f);
  }
  return TRUE;
}

/* раскол экрана на куски: снимок экрана в момент нажатия, семь больших разрезов
   через середину под разными углами, у каждого куска — свой уход: от середины
   в сторону и вниз за край. Разрезы сначала видны поверх экрана, потом куски
   разъезжаются, экран темнеет и куски возвращаются на место */
static void fx_shatter(int x, int y) {
  POINT pt = {x, y};
  MONITORINFO mi;
  memset(&mi, 0, sizeof(mi));
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) return;
  FxWin *f = &g_fx[FX_WINDOWS];
  fx_big_reset(f); /* чтобы в снимок не попал прошлый разрез */
  RECT r = mi.rcMonitor;
  int W = r.right - r.left, H = r.bottom - r.top;
  if (!fx_prepare_wh(f, W, H)) return;
  HDC scr = GetDC(NULL);
  BitBlt(f->dc, 0, 0, W, H, scr, r.left, r.top, SRCCOPY); /* экран — прямо в буфер окна */
  ReleaseDC(NULL, scr);
  if (!fx_shatter_setup(f)) return;
  f->shatter = TRUE;
  f->flame = FALSE;
  f->scratch = FALSE;
  f->slash = TRUE;
  f->org.x = r.left;
  f->org.y = r.top;
  f->frame = 0;
  f->frames = FX_SHATTER_FRAMES;
  f->busy = TRUE;
  fx_render(f);
  SetWindowPos(f->w, HWND_TOPMOST, f->org.x, f->org.y, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetTimer(f->w, 1, 16, NULL);
}

/* облачко из точки нажатия */
static void fx_burst(int x, int y) {
  fx_init_sprites();
  int kind = fx_kind();
  if (kind == 4) {
    /* редкие удачи поверх обычного разреза: раскол экрана или большой разрез */
    float r = fx_rand();
    if (r < FX_SHATTER_CHANCE) fx_shatter(x, y);
    else if (r < FX_SHATTER_CHANCE + FX_BIG_CHANCE) fx_big_slash(x, y);
  }
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
  if (!fx_prepare_wh(f, S, S)) return;
  g_fxSeed ^= GetTickCount() * 2654435761u;
  f->org.x = x - S / 2;
  f->org.y = y - S / 2;
  f->frame = 0;
  f->flame = kind == 2;
  f->scratch = kind == 3;
  f->slash = kind == 4;
  if (f->slash) {
    /* один тонкий красный разрез через точку нажатия, примерно под 45° */
    f->n = 1;
    f->frames = FX_SLASH_FRAMES;
    float dir = fx_rand() < 0.5f ? 1.0f : -1.0f;
    float a = 0.75f + (fx_rand() - 0.5f) * 0.3f;
    float dx = dir * cosf(a), dy = sinf(a);
    float len = 110.0f * g_fxScale;
    FxPart *p = &f->p[0];
    memset(p, 0, sizeof(*p));
    p->x = S / 2.0f - dx * len / 2;
    p->y = S / 2.0f - dy * len / 2; /* по центру: через точку нажатия */
    p->vx = dx * len;
    p->vy = dy * len;
    p->sz = 3.2f;
    p->rev = 4.0f;
    p->life = FX_SLASH_FRAMES;
    goto start;
  }
  if (f->scratch) {
    /* пара параллельных царапин наискосок, вниз и в одну из сторон; вторая
       чуть позже и чуть короче, как будто провели лапой дважды */
    f->n = FX_SCRATCHES;
    f->frames = FX_SCRATCH_FRAMES;
    float dir = fx_rand() < 0.5f ? 1.0f : -1.0f;
    float ang = (0.95f + (fx_rand() - 0.5f) * 0.3f); /* от вертикали, ~55° */
    float dx = dir * cosf(ang), dy = sinf(ang);      /* вдоль царапины */
    float nx = -dy, ny = dx;                         /* поперёк */
    for (int i = 0; i < FX_SCRATCHES; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      float len = (i == 0 ? 56.0f : 48.0f) * g_fxScale;
      float off = (i == 0 ? -6.0f : 6.0f) * g_fxScale;
      float along = (i == 0 ? -0.5f : -0.42f) * len;
      p->x = S / 2.0f + dx * along + nx * off;
      p->y = S / 2.0f + dy * along + ny * off;
      p->vx = dx * len;
      p->vy = dy * len;
      p->sz = i == 0 ? 3.4f : 3.0f;
      p->delay = i * 3;
      p->rev = 3.0f;
      p->life = FX_SCRATCH_FRAMES - p->delay;
    }
    goto start;
  }
  if (f->flame) {
    /* немного пламени: язычки из точки нажатия, вверх и чуть в стороны */
    f->n = FX_FLAMES;
    f->frames = FX_FLAME_FRAMES;
    for (int i = 0; i < FX_FLAMES; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      p->x = S / 2.0f + (fx_rand() - 0.5f) * 10.0f * g_fxScale;
      p->y = S / 2.0f + (fx_rand() - 0.5f) * 3.0f * g_fxScale;
      p->vx = (fx_rand() - 0.5f) * 1.6f * g_fxScale;
      p->vy = -(0.6f + fx_rand() * 0.9f) * g_fxScale;
      p->sz = 5.0f + fx_rand() * 4.0f;
      p->delay = (int)(fx_rand() * 7);
      p->life = 16 + (int)(fx_rand() * 11);
      if (p->delay + p->life > FX_FLAME_FRAMES) p->life = FX_FLAME_FRAMES - p->delay;
      p->tw = fx_rand() * 6.2832f;
    }
    goto start;
  }
  f->n = FX_PARTS;
  f->frames = FX_FRAMES;
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
start:
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

/* хук стоит, только пока он нужен: есть эффект при нажатии */
static void fx_sync(void) {
  BOOL want = fx_kind() != 0;
  if (want && !g_fxHook) g_fxHook = SetWindowsHookExW(WH_MOUSE_LL, fx_mouse_hook, g_inst, 0);
  else if (!want && g_fxHook) {
    UnhookWindowsHookEx(g_fxHook);
    g_fxHook = NULL;
  }
}

static void fx_shutdown(void) {
  if (g_fxHook) UnhookWindowsHookEx(g_fxHook);
  g_fxHook = NULL;
  fx_shatter_free();
  for (int k = 0; k < FX_ALL; k++) {
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
