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
   Всё в том же окне на весь монитор.

   С 2026.10.14.2 — ещё четыре эффекта (styles.c добавил к ним курсоры):
   «Кольца» — три водяных кольца расходятся из точки нажатия; «Конфетти» —
   горсть цветных бумажек подлетает, кувыркается и осыпается; «Молния» — три
   ломаных голубых разряда бьют из точки нажатия и мерцают; «Сердечки» — пять
   сердечек всплывают, покачиваясь, и тают. Тот же движок рисует их и в
   окошке просмотра Настроек (fx_seed и fx_paint без своего окна). */

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
#define FX_SLASH_FRAMES 24 /* ~0.4 с: разрез проводится, раскалён, тонеет и гаснет */
#define FX_BIG_CUTS 2   /* толстый разрез и тонкая спутница рядом */
#define FX_BIG_FRAMES 40 /* ~0.65 с */
#define FX_BIG_CHANCE (1.0f / 120.0f) /* в среднем раз в 120 нажатий в «Разрезе»: большой разрез через весь монитор */
#define FX_SHATTER_CHANCE (1.0f / 340.0f) /* в среднем раз в 340: раскол экрана; проверяется раньше большого */
#define FX_SHATTER_CUTS 7 /* столько больших разрезов режут экран */
#define FX_SHATTER_PIECES (1 << FX_SHATTER_CUTS) /* кусков — не больше */
#define FX_SHATTER_CUT_FRAMES 8  /* разрезы проводятся поверх экрана */
#define FX_SHATTER_SPLIT_FRAMES 32 /* куски расходятся трещинами */
#define FX_SHATTER_HOLD_FRAMES 12  /* куски замерли врозь */
#define FX_SHATTER_BACK_FRAMES 36  /* куски возвращаются на место */
#define FX_SHATTER_FRAMES (FX_SHATTER_CUT_FRAMES + FX_SHATTER_SPLIT_FRAMES + \
                           FX_SHATTER_HOLD_FRAMES + FX_SHATTER_BACK_FRAMES)
#define FX_RINGS 3      /* «Кольца» */
#define FX_RING_FRAMES 36
#define FX_CONFETTI 16  /* «Конфетти» */
#define FX_CONFETTI_FRAMES 48
#define FX_BOLTS 3      /* «Молния» */
#define FX_BOLT_FRAMES 18
#define FX_HEARTS 5     /* «Сердечки» */
#define FX_HEART_FRAMES 44
#define FX_CUTS 7       /* «Рассечение» Сукуны */
#define FX_CUT_FRAMES 30
#define FX_PURPLE_SPARKS 10 /* «Пурпур» Годжо */
#define FX_PURPLE_MERGE 12  /* синий и красный сходятся за столько кадров */
#define FX_PURPLE_FRAMES 44
#define FX_FEATHERS 7    /* «Перья» Ворона */
#define FX_FEATHER_FRAMES 52
#define FX_INK_DROPS 10  /* «Чернила» Осьминога */
#define FX_INK_FRAMES 42
#define FX_VENOM_DROPS 12 /* «Яд» Скорпиона: капли и ещё 4 пузыря */
#define FX_VENOM_FRAMES 42
#define FX_CAVIAR_DROPS 11 /* «Пятна»: мелкие белые пятна */
#define FX_CAVIAR_LEAVES 3 /* крупные белые пятна */
#define FX_CAVIAR_FRAMES 52
#define FX_INF_DUST 16   /* «Бесконечность» Годжо: пылинки, застывающие у барьера */
#define FX_INF_FRAMES 52
#define FX_PETALS 12     /* «Лепестки» Тяночки: лепестки сакуры */
#define FX_PETAL_FRAMES 50
#define FX_MAXP 16      /* больше всех частиц у конфетти */

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
  int kind;      /* какой эффект: как у fx_kind; 5…15 рисует fx_paint_new */
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

static void fx_slash_part(FxWin *f, const FxPart *p, int age);

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
    for (size_t i = 0; i < n; i++) dst[i] = 0xFF1A0004u; /* между кусками — тёмно-бордовая пустота */
    int sp = f->frame - FX_SHATTER_CUT_FRAMES;
    int fl = sp >= 0 && sp < 5 ? (int)(140 * (1.0f - sp / 5.0f)) : 0;
    /* каждый пиксель снимка уходит со своим куском; что вышло за край — пропадает */
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        size_t i = (size_t)y * W + x;
        int s = g_shat.shard[i] & 0x7F;
        int nx = x + ox[s], ny = y + oy[s];
        if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
        DWORD c = src[i];
        if (g_shat.shard[i] & 0x80) /* край куска раскалён: алая кромка с обеих сторон разлома */
          c = 0xFF000000u | (DWORD)(255 - ((255 - ((c >> 16) & 255)) >> 3)) << 16 |
              (DWORD)(60 + (((c >> 8) & 255) >> 3)) << 8 | (DWORD)(40 + ((c & 255) >> 3));
        if (fl) /* в миг раскола — белая вспышка */
          c = 0xFF000000u | (DWORD)(((c >> 16) & 255) + ((255 - ((c >> 16) & 255)) * fl >> 8)) << 16 |
              (DWORD)(((c >> 8) & 255) + ((255 - ((c >> 8) & 255)) * fl >> 8)) << 8 |
              (DWORD)((c & 255) + ((255 - (c & 255)) * fl >> 8));
        dst[(size_t)ny * W + nx] = c;
      }
    }
  }
  /* разрезы поверх экрана; через 12 кадров гаснут окончательно */
  for (int i = 0; i < f->n; i++) fx_slash_part(f, &f->p[i], f->frame - f->p[i].delay);
}

/* ---- «Кольца», «Конфетти», «Молния», «Сердечки» (с 2026.10.14.2) ---------- */

/* одна точка буфера поверх того, что уже есть (буфер с домноженной альфой) */
static void fx_put(FxWin *f, int x, int y, float cr, float cg, float cb, float sa) {
  if (x < 0 || y < 0 || x >= f->W || y >= f->H || sa <= 0.0f) return;
  if (sa > 1.0f) sa = 1.0f;
  BYTE *d = f->bits + ((size_t)y * f->W + x) * 4;
  float inv = 1.0f - sa;
  d[0] = (BYTE)(cb * sa + d[0] * inv);
  d[1] = (BYTE)(cg * sa + d[1] * inv);
  d[2] = (BYTE)(cr * sa + d[2] * inv);
  d[3] = (BYTE)(255 * sa + d[3] * inv);
}

static float fx_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* мягкое круглое пятно: вспышка в точке нажатия */
static void fx_glow_dot(FxWin *f, float cx, float cy, float r, float cr, float cg, float cb, float a) {
  if (r < 0.5f || a <= 0.0f) return;
  for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
    for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
      float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / r;
      if (d < 1.0f) fx_put(f, x, y, cr, cg, cb, a * (1.0f - d) * (1.0f - d));
    }
}

/* ---- «Разрез» (с 2026.10.14.7) ------------------------------------------- */

/* искра из разреза: короткий штрих, летит от разреза и падает, белая с алым */
static void fx_slash_spark(FxWin *f, const FxPart *p, int age) {
  float t = (float)age / p->life, g = 0.22f * g_fxScale;
  float x = p->x + p->vx * age, y = p->y + p->vy * age + g * age * age;
  float tx = x - p->vx * 1.8f, ty = y - (p->vy + 2.0f * g * age) * 1.8f; /* хвост — назад по ходу */
  float vx = x - tx, vy = y - ty, l2 = vx * vx + vy * vy;
  float r = p->sz * g_fxScale * (1.0f - 0.6f * t), a = 1.0f - t * t;
  if (l2 < 0.01f || r < 0.3f) return;
  for (int py = (int)(fminf(y, ty) - r - 2); py <= (int)(fmaxf(y, ty) + r + 2); py++)
    for (int px = (int)(fminf(x, tx) - r - 2); px <= (int)(fmaxf(x, tx) + r + 2); px++) {
      float qx = px + 0.5f - tx, qy = py + 0.5f - ty;
      float s = fx_clamp01((qx * vx + qy * vy) / l2);
      float d = hypotf(qx - vx * s, qy - vy * s), rr = r * (0.35f + 0.65f * s);
      fx_put(f, px, py, 255, 40, 50, a * 0.5f * fx_clamp01(1.0f - (d - rr) / (2.5f * g_fxScale)));
      fx_put(f, px, py, 255, fx_mix(90, 245, s), fx_mix(80, 230, s), a * fx_clamp01(rr + 0.5f - d));
    }
}

/* след клинка: полумесяц с острыми концами (p->tw — изгиб, доля длины). Его
   проводят за p->rev кадров, на кончике — белая вспышка; сразу после взмаха он
   раскалён добела по оси, потом тонеет до волоска и гаснет. Вокруг — алое
   свечение. Края сглажены. p->spr = 1 — это не разрез, а искра */
#define FX_SLASH_SEG 12
#define FX_SLASH_LUT 256 /* толщина вдоль разреза — таблицей, а не powf на каждый пиксель */
static float g_slashProf[FX_SLASH_LUT + 1], g_slashGlow[FX_SLASH_LUT + 1];
static void fx_slash_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  if (g_slashProf[FX_SLASH_LUT / 2] == 0.0f)
    for (int i = 0; i <= FX_SLASH_LUT; i++) {
      g_slashProf[i] = powf(sinf(3.14159265f * i / FX_SLASH_LUT), 0.85f);
      g_slashGlow[i] = sqrtf(g_slashProf[i]);
    }
  if (p->spr) {
    fx_slash_spark(f, p, age);
    return;
  }
  float len = hypotf(p->vx, p->vy);
  if (len < 1.0f) return;
  float nx = -p->vy / len, ny = p->vx / len, bend = 4.0f * p->tw * len;
  float t = fx_clamp01((age + 1) / p->rev);
  float reveal = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
  float after = age + 1 <= p->rev ? 0.0f : (age + 1 - p->rev) / (p->life - p->rev);
  float heat = 1.0f - fx_clamp01(after * 2.5f);  /* белый жар сразу после взмаха */
  float a = after < 0.45f ? 1.0f : 1.0f - (after - 0.45f) / 0.55f;
  float maxw = p->sz * g_fxScale * (1.0f - 0.72f * powf(after, 0.7f)) * (1.0f + 0.25f * heat);
  float gr = (4.0f + p->sz * 1.1f) * g_fxScale, ga = 0.55f * (1.0f - after) * (1.0f - after);
  int nseg = fabsf(p->tw) > 0.001f ? FX_SLASH_SEG : 1;
  float sx[FX_SLASH_SEG + 1], sy[FX_SLASH_SEG + 1];
  float xa = 1e9f, xb = -1e9f, ya = 1e9f, yb = -1e9f;
  for (int k = 0; k <= nseg; k++) {
    float s = reveal * k / nseg, b = bend * s * (1.0f - s);
    sx[k] = p->x + p->vx * s + nx * b;
    sy[k] = p->y + p->vy * s + ny * b;
    xa = fminf(xa, sx[k]), xb = fmaxf(xb, sx[k]), ya = fminf(ya, sy[k]), yb = fmaxf(yb, sy[k]);
  }
  float pad = maxw + gr + 2.0f;
  int x0 = (int)fmaxf(xa - pad, 0.0f), x1 = (int)fminf(xb + pad, f->W - 1.0f);
  int y0 = (int)fmaxf(ya - pad, 0.0f), y1 = (int)fminf(yb + pad, f->H - 1.0f);
  for (int y = y0; y <= y1; y++) {
    /* в строке — только то, что рядом с отрезками: у большого разреза это узкая полоса */
    float ra = 1e9f, rb = -1e9f;
    int act[FX_SLASH_SEG], na = 0; /* и проверять только эти отрезки */
    for (int k = 0; k < nseg; k++)
      if (y + pad >= fminf(sy[k], sy[k + 1]) && y - pad <= fmaxf(sy[k], sy[k + 1])) {
        act[na++] = k;
        float ka = sy[k + 1] != sy[k] ? (y + 0.5f - sy[k]) / (sy[k + 1] - sy[k]) : 0.0f;
        float kb = sy[k + 1] != sy[k] ? pad / fabsf(sy[k + 1] - sy[k]) : 1.0f;
        float u0 = fx_clamp01(ka - kb), u1 = fx_clamp01(ka + kb);
        float xu0 = sx[k] + (sx[k + 1] - sx[k]) * u0, xu1 = sx[k] + (sx[k + 1] - sx[k]) * u1;
        ra = fminf(ra, fminf(xu0, xu1) - pad), rb = fmaxf(rb, fmaxf(xu0, xu1) + pad);
      }
    int xa0 = ra > x0 ? (int)ra : x0, xb1 = rb < x1 ? (int)rb : x1;
    for (int x = xa0; x <= xb1; x++) {
      float d2 = pad * pad, s = 0.0f;
      for (int j = 0; j < na; j++) { /* ближайший отрезок ломаной */
        int k = act[j];
        float vx = sx[k + 1] - sx[k], vy = sy[k + 1] - sy[k], l2 = vx * vx + vy * vy;
        float qx = x + 0.5f - sx[k], qy = y + 0.5f - sy[k];
        float u = l2 > 0.0f ? fx_clamp01((qx * vx + qy * vy) / l2) : 0.0f;
        float ex = qx - vx * u, ey = qy - vy * u, dd = ex * ex + ey * ey;
        if (dd < d2) d2 = dd, s = reveal * (k + u) / nseg;
      }
      if (d2 >= pad * pad) continue;
      float d = sqrtf(d2);
      int li = (int)(s * FX_SLASH_LUT + 0.5f);
      float prof = g_slashProf[li < 0 ? 0 : li > FX_SLASH_LUT ? FX_SLASH_LUT : li];
      float head = reveal < 1.0f ? fx_clamp01((reveal - s) / 0.18f) : 1.0f; /* бегущий острый кончик */
      float hw = maxw * prof * sqrtf(head);
      float g = 1.0f - fmaxf(d - hw, 0.0f) / gr;
      if (g > 0.0f) fx_put(f, x, y, 255, 24, 40, a * ga * g * g * g_slashGlow[li < 0 ? 0 : li > FX_SLASH_LUT ? FX_SLASH_LUT : li]);
      float body = fx_clamp01(hw + 0.5f - d);
      if (body <= 0.0f || hw < 0.3f) continue;
      float m = fx_clamp01(d / hw); /* 0 по оси, 1 на краю */
      fx_put(f, x, y, fx_mix(255, 150, m), fx_mix(fx_mix(50, 140, heat), 0, m), fx_mix(fx_mix(60, 130, heat), 24, m), a * body);
      fx_put(f, x, y, 255, 250, 244, a * fx_clamp01(hw * (0.2f + 0.2f * heat) + 0.5f - d));
    }
  }
  if (reveal < 1.0f) { /* вспышка на кончике, пока клинок идёт */
    float r = (5.0f + p->sz * 1.6f) * g_fxScale;
    fx_glow_dot(f, sx[nseg], sy[nseg], r * 1.6f, 255, 40, 50, 0.6f);
    fx_glow_dot(f, sx[nseg], sy[nseg], r * 0.7f, 255, 245, 235, 0.9f);
  }
}

/* искры вдоль разреза p: n штук с задержкой delay, скорость speed, толщина sz */
static int fx_slash_sparks(FxPart *out, const FxPart *p, int n, float speed, float sz, int delay) {
  float len = hypotf(p->vx, p->vy), nx = -p->vy / len, ny = p->vx / len;
  for (int i = 0; i < n; i++) {
    FxPart *q = &out[i];
    memset(q, 0, sizeof(*q));
    float s = 0.2f + 0.6f * fx_rand(), b = 4.0f * p->tw * len * s * (1.0f - s);
    float side = fx_rand() < 0.5f ? 1.0f : -1.0f, v = speed * (0.5f + fx_rand()) * g_fxScale;
    float along = (fx_rand() - 0.3f) * 0.8f;
    q->x = p->x + p->vx * s + nx * b;
    q->y = p->y + p->vy * s + ny * b;
    q->vx = (nx * side + p->vx / len * along) * v;
    q->vy = (ny * side + p->vy / len * along) * v - 0.6f * v * (fx_rand() < 0.6f);
    q->sz = sz * (0.7f + 0.6f * fx_rand());
    q->spr = 1;
    q->delay = delay + (int)(fx_rand() * 3);
    q->life = 9 + (int)(fx_rand() * 7);
  }
  return n;
}

/* водяное кольцо: светлое изнутри, тёмно-синее снаружи — видно и на белом */
static void fx_ring_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  float t = (float)age / p->life;
  float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
  float rad = (3.0f + p->sz * ease) * g_fxScale;
  float thick = (2.6f - 1.5f * t) * g_fxScale;
  float a = 0.9f * powf(1.0f - t, 1.4f);
  int span = (int)(rad + thick + 2);
  for (int y = (int)p->y - span; y <= (int)p->y + span; y++)
    for (int x = (int)p->x - span; x <= (int)p->x + span; x++) {
      float d = sqrtf((x + 0.5f - p->x) * (x + 0.5f - p->x) + (y + 0.5f - p->y) * (y + 0.5f - p->y));
      float k = 1.0f - fabsf(d - rad) / thick;
      if (k <= 0.0f) continue;
      float u = fx_clamp01((d - rad) / thick * 0.5f + 0.5f); /* 0 изнутри, 1 снаружи */
      fx_put(f, x, y, fx_mix(170, 28, u), fx_mix(235, 104, u), fx_mix(255, 196, u), a * fx_clamp01(k * 1.6f));
    }
}

/* бумажка конфетти: повёрнутый прямоугольник; ширина «дышит» — она кувыркается */
static void fx_confetti_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  float t = (float)age / p->life;
  float a = t < 0.65f ? 1.0f : 1.0f - (t - 0.65f) / 0.35f;
  float hl = p->sz * g_fxScale;
  float hw = p->spr / 10.0f * g_fxScale * (0.25f + 0.75f * fabsf(cosf(p->tw * 1.3f)));
  float c = cosf(p->tw), s = sinf(p->tw);
  float shade = 0.75f + 0.25f * fabsf(cosf(p->tw * 1.3f)); /* повёрнутая ребром — темнее */
  int span = (int)(hl + 2);
  for (int y = (int)p->y - span; y <= (int)p->y + span; y++)
    for (int x = (int)p->x - span; x <= (int)p->x + span; x++) {
      float dx = x + 0.5f - p->x, dy = y + 0.5f - p->y;
      float lx = dx * c + dy * s, ly = -dx * s + dy * c;
      float k = fx_clamp01(hl - fabsf(lx) + 0.5f) * fx_clamp01(hw - fabsf(ly) + 0.5f);
      if (k > 0.0f) fx_put(f, x, y, p->r * shade, p->g * shade, p->b * shade, a * k);
    }
}

/* разряд целиком: ломаная из отрезков разной толщины. Каждую точку красим один
   раз — по ближайшему отрезку, иначе на стыках свечение ложится дважды и молния
   выходит «бусами». Белая сердцевина, насыщенно-синий край (видно на белом) и
   голубое свечение вокруг */
#define FX_BOLT_SEGS 9
static void fx_bolt_draw(FxWin *f, const float (*sg)[5], int n, float a) {
  if (n <= 0) return;
  float glowMax = 6.0f * g_fxScale;
  float xa = 1e9f, xb = -1e9f, ya = 1e9f, yb = -1e9f;
  for (int k = 0; k < n; k++) {
    xa = fminf(xa, fminf(sg[k][0], sg[k][2]));
    xb = fmaxf(xb, fmaxf(sg[k][0], sg[k][2]));
    ya = fminf(ya, fminf(sg[k][1], sg[k][3]));
    yb = fmaxf(yb, fmaxf(sg[k][1], sg[k][3]));
  }
  for (int y = (int)(ya - glowMax - 1); y <= (int)(yb + glowMax + 1); y++)
    for (int x = (int)(xa - glowMax - 1); x <= (int)(xb + glowMax + 1); x++) {
      float core = 0.0f, edge = 0.0f, glow = 0.0f;
      for (int k = 0; k < n; k++) {
        float w = sg[k][4], vx = sg[k][2] - sg[k][0], vy = sg[k][3] - sg[k][1];
        float l2 = vx * vx + vy * vy;
        if (l2 < 0.01f) continue;
        float px = x + 0.5f - sg[k][0], py = y + 0.5f - sg[k][1];
        float s = fx_clamp01((px * vx + py * vy) / l2);
        float ex = px - vx * s, ey = py - vy * s;
        float d = sqrtf(ex * ex + ey * ey);
        float rc = 0.9f * g_fxScale * w, re = rc + 1.4f * g_fxScale * w, rg = glowMax * w;
        core = fmaxf(core, fx_clamp01(rc + 0.5f - d));
        edge = fmaxf(edge, fx_clamp01(re + 0.5f - d));
        if (d < rg) {
          float g = 1.0f - d / rg;
          glow = fmaxf(glow, g * g);
        }
      }
      if (glow <= 0.0f) continue;
      fx_put(f, x, y, 110, 170, 255, a * 0.45f * glow);
      fx_put(f, x, y, 40, 90, 235, a * edge);
      fx_put(f, x, y, 245, 250, 255, a * core);
    }
}

/* ломаная молнии: каждые три кадра излом новый — разряд потрескивает */
static void fx_bolt_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  float fade = age < 10 ? 1.0f : 1.0f - (age - 10) / (float)(p->life - 10);
  float a = fade * (age % 4 == 2 ? 0.5f : 1.0f);
  unsigned rs = (unsigned)p->spr * 2654435761u + (unsigned)(age / 3) * 7919u;
#define FX_BOLT_RND() ((rs = rs * 1103515245u + 12345u), (float)((rs >> 8) & 0xFFFF) / 65535.0f)
  float ux = cosf(p->tw), uy = sinf(p->tw), nx = -uy, ny = ux;
  float len = p->sz * g_fxScale, jit = 7.0f * g_fxScale;
  float px[7], py[7];
  for (int j = 0; j < 7; j++) {
    float s = j / 6.0f, off = j == 0 ? 0.0f : (FX_BOLT_RND() - 0.5f) * 2.0f * jit;
    px[j] = p->x + ux * len * s + nx * off;
    py[j] = p->y + uy * len * s + ny * off;
  }
  float sg[FX_BOLT_SEGS][5];
  int n = 0;
  int shown = (int)((age + 1) * 2.5f); /* разряд пробегает за пару кадров */
  if (shown > 6) shown = 6;
  for (int j = 0; j < shown; j++) {
    sg[n][0] = px[j], sg[n][1] = py[j], sg[n][2] = px[j + 1], sg[n][3] = py[j + 1];
    sg[n++][4] = 1.0f - j * 0.08f;
  }
  if (shown >= 5) { /* отросток от середины */
    float side = FX_BOLT_RND() < 0.5f ? -1.0f : 1.0f;
    float bx = cosf(p->tw + 0.7f * side), by = sinf(p->tw + 0.7f * side);
    float qx = px[3] + bx * len * 0.22f + nx * (FX_BOLT_RND() - 0.5f) * jit;
    float qy = py[3] + by * len * 0.22f + ny * (FX_BOLT_RND() - 0.5f) * jit;
    sg[n][0] = px[3], sg[n][1] = py[3], sg[n][2] = qx, sg[n][3] = qy, sg[n++][4] = 0.7f;
    sg[n][0] = qx, sg[n][1] = qy, sg[n][2] = qx + bx * len * 0.16f, sg[n][3] = qy + by * len * 0.16f;
    sg[n++][4] = 0.55f;
  }
#undef FX_BOLT_RND
  fx_bolt_draw(f, (const float(*)[5])sg, n, a);
}

/* сердечко: (u²+v²−1)³ − u²v³ ≤ 0, тёмная кайма, светлый блик */
static int fx_heart_in(float u, float v) {
  float q = u * u + v * v - 1.0f;
  return q * q * q - u * u * v * v * v <= 0.0f;
}

static void fx_heart_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  float t = (float)age / p->life;
  float a = t < 0.55f ? 1.0f : 1.0f - (t - 0.55f) / 0.45f;
  float pop = age < 4 ? (age + 1) / 4.0f * 1.2f : (age < 7 ? 1.2f - 0.2f * (age - 3) / 3.0f : 1.0f);
  float sc = p->sz * g_fxScale * pop;
  if (sc < 1.0f) return;
  float rim = sc * 1.2f;
  int span = (int)(rim * 1.4f + 2);
  for (int y = (int)p->y - span; y <= (int)p->y + span; y++)
    for (int x = (int)p->x - span; x <= (int)p->x + span; x++) {
      int in = 0, inR = 0;
      for (int sy = 0; sy < 3; sy++)
        for (int sx = 0; sx < 3; sx++) {
          float ox = x + (sx + 0.5f) / 3.0f - p->x, oy = p->y - (y + (sy + 0.5f) / 3.0f) + 0.15f * sc;
          in += fx_heart_in(ox / sc, oy / sc);
          inR += fx_heart_in(ox / rim, oy / rim);
        }
      if (!inR) continue;
      float fill = in / 9.0f, cov = inR / 9.0f;
      float cr = fx_mix(p->r * 0.5f, p->r, fill), cg = fx_mix(p->g * 0.5f, p->g, fill),
            cb = fx_mix(p->b * 0.5f, p->b, fill);
      float hx = (x + 0.5f - p->x) / sc + 0.42f, hy = (y + 0.5f - p->y) / sc + 0.25f;
      float hl = 1.0f - sqrtf(hx * hx + hy * hy) / 0.24f; /* блик слева вверху */
      if (hl > 0.0f && fill > 0.5f) {
        cr = fx_mix(cr, 255, hl * 0.7f), cg = fx_mix(cg, 255, hl * 0.7f), cb = fx_mix(cb, 255, hl * 0.7f);
      }
      fx_put(f, x, y, cr, cg, cb, a * cov);
    }
}

/* «Лепестки» «Тяночки» (с 2026.10.28): розовые лепестки сакуры вылетают из точки
   нажатия, кружатся и, покачиваясь, опадают; гаснут меньше чем за секунду.
   Лепесток — повёрнутый эллипс с выемкой на кончике, к середине светлее */
static void fx_petal_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  float t = (float)age / p->life;
  float a = t < 0.6f ? 0.95f : 0.95f * (1.0f - (t - 0.6f) / 0.4f);
  float pop = age < 3 ? (age + 1) / 3.0f : 1.0f;
  float ra = p->sz * g_fxScale * pop, rb = ra * 0.55f;
  if (ra < 1.0f) return;
  float cs = cosf(p->tw), sn = sinf(p->tw);
  int span = (int)(ra + 2.0f);
  for (int y = (int)p->y - span; y <= (int)p->y + span; y++)
    for (int x = (int)p->x - span; x <= (int)p->x + span; x++) {
      int in = 0;
      float core = 0.0f;
      for (int sy = 0; sy < 2; sy++)
        for (int sx = 0; sx < 2; sx++) {
          float ox = x + (sx + 0.5f) / 2.0f - p->x, oy = y + (sy + 0.5f) / 2.0f - p->y;
          float u = (ox * cs + oy * sn) / ra, v = (-ox * sn + oy * cs) / rb;
          float d = u * u + v * v;
          if (u > 0.55f && fabsf(v) < (u - 0.55f) * 1.2f) continue; /* раздвоенный кончик */
          if (d <= 1.0f) {
            in++;
            core += 1.0f - d;
          }
        }
      if (!in) continue;
      float cov = in / 4.0f, k = core / in;
      fx_put(f, x, y, 255.0f, fx_mix(130.0f, 205.0f, k), fx_mix(170.0f, 220.0f, k), a * cov);
    }
}

/* ---- свои эффекты «Сукуны» и «Годжо» (с 2026.10.14.12) ----------------- */

/* «Рассечение»: тонкие прямые надрезы крест-накрест вокруг нажатия, сеткой,
   как невидимые лезвия. Каждый проводится за p->rev кадров: белая сердцевина,
   алая кромка и тёмно-красное свечение; потом гаснет */
static void fx_cut_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  float L = p->sz * g_fxScale, dx = cosf(p->tw), dy = sinf(p->tw);
  float grow = fx_clamp01((age + 1) / p->rev), fade = age < p->rev ? 1.0f : powf(1.0f - (age - p->rev) / (p->life - p->rev), 1.5f);
  float s1 = -L / 2 + L * grow, w = 1.3f * g_fxScale * (age < p->rev + 3 ? 1.4f : 1.0f), gl = 4.0f * g_fxScale;
  float x0 = p->x - dx * L / 2, y0 = p->y - dy * L / 2, x1 = p->x + dx * s1, y1 = p->y + dy * s1;
  for (int y = (int)(fminf(y0, y1) - gl - 2); y <= (int)(fmaxf(y0, y1) + gl + 2); y++)
    for (int x = (int)(fminf(x0, x1) - gl - 2); x <= (int)(fmaxf(x0, x1) + gl + 2); x++) {
      float qx = x + 0.5f - p->x, qy = y + 0.5f - p->y;
      float s = qx * dx + qy * dy, d = fabsf(qx * dy - qy * dx);
      if (s < -L / 2 || s > s1) continue;
      float e = 2.0f * s / L, ww = w * sqrtf(fx_clamp01(1.0f - e * e)) + 0.25f; /* к концам — в волосок */
      if (d > ww + gl) continue;
      fx_put(f, x, y, 150, 0, 15, fade * 0.45f * fx_clamp01(1.0f - (d - ww) / gl));
      if (d <= ww + 0.8f) fx_put(f, x, y, 225, 20, 40, fade * fx_clamp01(ww + 0.8f - d));
      if (d <= ww * 0.5f + 0.5f) fx_put(f, x, y, 255, 235, 235, fade * fx_clamp01(ww * 0.5f + 0.5f - d));
    }
}

/* «Пурпур»: синий шар слева и красный справа сходятся по спирали в точку
   нажатия и сливаются в лиловую сферу — она вспухает, бьёт кольцом и тает,
   разбрасывая лиловые искры */
static void fx_purple_paint(FxWin *f) {
  float c = f->W / 2.0f, sc = g_fxScale;
  int fr = f->frame;
  if (fr < FX_PURPLE_MERGE) {
    float t = (float)fr / FX_PURPLE_MERGE, e = 1.0f - t * t, ang = 3.1416f * (1.0f - e) * 0.9f;
    for (int k = 0; k < 2; k++) {
      float a = ang + k * 3.1416f, r = 46.0f * sc * e;
      float x = c - cosf(a) * r, y = c - sinf(a) * r * 0.55f;
      float cr = k ? 255 : 60, cg = k ? 50 : 150, cb = k ? 60 : 255;
      fx_glow_dot(f, x, y, 22.0f * sc, cr, cg, cb, 0.9f);
      fx_glow_dot(f, x, y, 10.0f * sc, fx_mix(cr, 255, 0.5f), fx_mix(cg, 255, 0.5f), fx_mix(cb, 255, 0.5f), 1.0f);
      fx_glow_dot(f, x, y, 4.5f * sc, 255, 255, 255, 1.0f);
    }
    return;
  }
  float t = (float)(fr - FX_PURPLE_MERGE) / (FX_PURPLE_FRAMES - FX_PURPLE_MERGE);
  float rad = (8.0f + 30.0f * (1.0f - (1.0f - t) * (1.0f - t))) * sc, a = powf(1.0f - t, 1.3f);
  if (fr - FX_PURPLE_MERGE < 4) fx_glow_dot(f, c, c, 40.0f * sc, 255, 230, 255, 0.9f * (1.0f - (fr - FX_PURPLE_MERGE) / 4.0f));
  fx_glow_dot(f, c, c, rad * 1.6f, 150, 50, 255, 0.55f * a);
  int span = (int)(rad + 2);
  for (int y = (int)c - span; y <= (int)c + span; y++) /* сфера: лиловая, к середине — белая */
    for (int x = (int)c - span; x <= (int)c + span; x++) {
      float d = hypotf(x + 0.5f - c, y + 0.5f - c) / rad;
      if (d >= 1.0f) continue;
      float k = d * d;
      fx_put(f, x, y, fx_mix(255, 175, k), fx_mix(240, 60, k), 255, a * fx_clamp01((1.0f - d) * rad * 0.7f));
    }
  float rr = (12.0f + 70.0f * t) * sc, th = (3.0f - 2.0f * t) * sc; /* ударное кольцо */
  span = (int)(rr + th + 2);
  for (int y = (int)c - span; y <= (int)c + span; y++)
    for (int x = (int)c - span; x <= (int)c + span; x++) {
      float k = 1.0f - fabsf(hypotf(x + 0.5f - c, y + 0.5f - c) - rr) / th;
      if (k > 0) fx_put(f, x, y, 205, 130, 255, 0.8f * a * fx_clamp01(k * 1.5f));
    }
  for (int i = 0; i < f->n; i++) {
    const FxPart *p = &f->p[i];
    int age = fr - p->delay;
    if (age < 0 || age >= p->life) continue;
    float q = 1.0f - (float)age / p->life;
    fx_glow_dot(f, p->x, p->y, p->sz * sc * (0.5f + 0.5f * q), 220, 160, 255, q);
  }
}

/* «Бесконечность» Годжо: вокруг точки нажатия вспыхивает шестигранный барьер,
   три волны бегут к нему снаружи и чем ближе, тем медленнее — никогда не
   доходят, а пылинки застывают в воздухе, не долетев. Потом всё гаснет */
static void fx_inf_paint(FxWin *f) {
  float c = f->W / 2.0f, sc = g_fxScale, R = 30.0f * sc;
  int fr = f->frame, S = f->W;
  float t = (float)fr / FX_INF_FRAMES, life = powf(1.0f - t, 1.15f);
  float grow = fr < 6 ? 0.55f + 0.45f * (1.0f - (1.0f - fr / 6.0f) * (1.0f - fr / 6.0f)) : 1.0f;
  float rh = R * grow, apo = rh * 0.8660254f, a0 = 0.18f * t;
  float rings[3], ringA[3];
  for (int k = 0; k < 3; k++) {
    int age = fr - k * 6;
    rings[k] = age < 0 ? -1.0f : R * 1.07f + R * 2.4f * expf(-0.14f * age);
    ringA[k] = age < 0 ? 0.0f : life * (0.75f - 0.15f * k) * fx_clamp01(age / 3.0f);
  }
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      float qx = x + 0.5f - c, qy = y + 0.5f - c, d = hypotf(qx, qy);
      if (d > 2.6f * R + 6.0f * sc) continue;
      float hd = 0;
      for (int k = 0; k < 3; k++) { /* расстояние до шестиугольника: наибольшая проекция на три оси */
        float n = a0 + 0.5236f + k * 1.0472f, pr = fabsf(qx * cosf(n) + qy * sinf(n));
        if (pr > hd) hd = pr;
      }
      float edge = fabsf(hd - apo), w = 1.7f * sc;
      if (hd < apo) /* внутри — еле заметная голубая дымка, гуще к краю */
        fx_put(f, x, y, 120, 200, 255, 0.16f * life * (hd / apo) * (hd / apo));
      if (edge < w + 2.0f * sc) {
        fx_put(f, x, y, 90, 190, 255, 0.45f * life * fx_clamp01(1.0f - (edge - w) / (2.0f * sc)));
        if (edge < w) fx_put(f, x, y, 235, 248, 255, life * 0.95f * fx_clamp01(1.0f - edge / w));
      }
      for (int k = 0; k < 3; k++) {
        if (rings[k] < 0) continue;
        float e = fabsf(d - rings[k]), th = (2.0f + 1.6f * (rings[k] - R) / R) * sc; /* ближе к барьеру — тоньше */
        if (e < th * 3.0f) fx_put(f, x, y, 150, 215, 255, ringA[k] * 0.12f * fx_clamp01(1.0f - e / (th * 3.0f))); /* «линза» */
        if (e < th) fx_put(f, x, y, 215, 240, 255, ringA[k] * fx_clamp01(1.0f - e / th));
      }
    }
  for (int k = 0; k < 6; k++) { /* блеск в углах барьера */
    float a = a0 + k * 1.0472f;
    fx_glow_dot(f, c + cosf(a) * rh, c + sinf(a) * rh, (6.0f + 3.0f * sinf(fr * 0.5f + k)) * sc, 190, 230, 255, 0.8f * life);
  }
  if (fr < 7) fx_glow_dot(f, c, c, 20.0f * sc, 200, 235, 255, 0.85f * (1.0f - fr / 7.0f));
  for (int i = 0; i < f->n; i++) { /* пылинки: летят к барьеру и застывают */
    const FxPart *p = &f->p[i];
    int age = fr - p->delay;
    if (age < 0 || age >= p->life) continue;
    float q = 1.0f - (float)age / p->life, r = p->rev * sc + (p->sz - p->rev) * sc * expf(-0.12f * age);
    float r2 = p->rev * sc + (p->sz - p->rev) * sc * expf(-0.12f * (age - 2));
    float x = c + cosf(p->tw) * r, y = c + sinf(p->tw) * r;
    for (int s = 1; s <= 4; s++) { /* короткий след: чем медленнее, тем он короче */
      float rs = r + (r2 - r) * s / 4.0f;
      fx_glow_dot(f, c + cosf(p->tw) * rs, c + sinf(p->tw) * rs, 2.2f * sc, 150, 215, 255, 0.3f * q * (1.0f - s / 5.0f));
    }
    fx_glow_dot(f, x, y, 4.0f * sc, 200, 235, 255, 0.9f * q);
  }
}

/* ---- свои эффекты Ворона, Осьминога и Скорпиона (с 2026.10.14.13) ------ */

/* чёрное перо с сиреневой кромкой: кувыркается (p->tw — поворот) и тает */
static void fx_feather_part(FxWin *f, const FxPart *p, int age) {
  if (age < 0 || age >= p->life) return;
  float t = (float)age / p->life, a = (1.0f - t * t) * (age < 2 ? (age + 1) / 2.0f : 1.0f);
  float L = p->sz * g_fxScale, dx = cosf(p->tw), dy = sinf(p->tw);
  int span = (int)(L / 2 + 2);
  for (int y = (int)p->y - span; y <= (int)p->y + span; y++)
    for (int x = (int)p->x - span; x <= (int)p->x + span; x++) {
      float qx = x + 0.5f - p->x, qy = y + 0.5f - p->y;
      float sl = (qx * dx + qy * dy) / L + 0.5f, w = fabsf(qx * dy - qy * dx);
      if (sl < 0 || sl > 1) continue;
      float hw = L * 0.17f * sinf(3.14159265f * sl) + 0.4f;
      if (w > hw + 0.7f) continue;
      float cov = fx_clamp01(hw + 0.7f - w);
      if (w < 0.55f * g_fxScale) fx_put(f, x, y, 200, 190, 170, a * cov);          /* очин */
      else if (w > hw - 1.0f * g_fxScale) fx_put(f, x, y, 140, 125, 205, a * cov); /* кромка */
      else fx_put(f, x, y, 38, 38, 62, a * cov);
    }
}

/* «Чернила»: клякса с волнистым краем растекается из точки нажатия, вокруг
   разлетаются капли; держится и тает */
static void fx_ink_paint(FxWin *f) {
  float c = f->W / 2.0f, sc = g_fxScale;
  int fr = f->frame;
  float grow = fx_clamp01((fr + 1) / 8.0f), a = fr < 18 ? 0.92f : 0.92f * (1.0f - (fr - 18.0f) / (FX_INK_FRAMES - 18));
  float R = 24.0f * sc * (1.0f - (1.0f - grow) * (1.0f - grow)), ph = f->p[0].tw;
  int span = (int)(R * 1.3f + 2);
  for (int y = (int)c - span; y <= (int)c + span; y++)
    for (int x = (int)c - span; x <= (int)c + span; x++) {
      float qx = x + 0.5f - c, qy = y + 0.5f - c, d = hypotf(qx, qy), an = atan2f(qy, qx);
      float edge = R * (1.0f + 0.17f * sinf(5 * an + ph) + 0.08f * sinf(9 * an + ph * 2));
      if (d > edge + 1) continue;
      float k = d / (edge + 0.01f);
      fx_put(f, x, y, fx_mix(30, 85, k * k), fx_mix(18, 45, k * k), fx_mix(48, 130, k * k), a * fx_clamp01(edge + 1 - d));
    }
  for (int i = 0; i < f->n; i++) {
    const FxPart *p = &f->p[i];
    int age = fr - p->delay;
    if (age < 0) continue;
    float r = p->sz * sc;
    for (int y = (int)(p->y - r - 1); y <= (int)(p->y + r + 1); y++)
      for (int x = (int)(p->x - r - 1); x <= (int)(p->x + r + 1); x++) {
        float d = hypotf(x + 0.5f - p->x, y + 0.5f - p->y);
        if (d <= r + 1) fx_put(f, x, y, 34, 20, 55, a * fx_clamp01(r + 1 - d));
      }
  }
}

/* «Яд»: зелёные капли брызгами вверх и вниз, в точке нажатия пузыри лопаются */
static void fx_venom_paint(FxWin *f) {
  float c = f->W / 2.0f, sc = g_fxScale;
  if (f->frame < 7) fx_glow_dot(f, c, c, 22.0f * sc, 90, 255, 60, 0.7f * (1.0f - f->frame / 7.0f));
  for (int i = 0; i < f->n; i++) {
    const FxPart *p = &f->p[i];
    int age = f->frame - p->delay;
    if (age < 0 || age >= p->life) continue;
    float t = (float)age / p->life;
    if (p->spr) { /* пузырь: растёт колечком и лопается */
      float r = (2.0f + p->sz * t) * sc, th = 1.3f * sc, a = t < 0.85f ? 0.9f : 0.9f * (1.0f - (t - 0.85f) / 0.15f);
      for (int y = (int)(p->y - r - 2); y <= (int)(p->y + r + 2); y++)
        for (int x = (int)(p->x - r - 2); x <= (int)(p->x + r + 2); x++) {
          float d = hypotf(x + 0.5f - p->x, y + 0.5f - p->y);
          float k = 1.0f - fabsf(d - r) / th;
          if (k > 0) fx_put(f, x, y, 150, 255, 120, a * fx_clamp01(k * 1.5f));
          else if (d < r) fx_put(f, x, y, 60, 200, 50, a * 0.25f);
        }
      continue;
    }
    float a = 1.0f - t * t;
    fx_glow_dot(f, p->x, p->y, p->sz * 2.4f * sc, 60, 230, 50, 0.7f * a);
    fx_glow_dot(f, p->x, p->y, p->sz * sc, 200, 255, 160, a);
  }
}

/* «Пятна»: белые пятна выскакивают из точки нажатия и гаснут.
   Серый ободок нужен, чтобы пятно было видно и на белом фоне. */
static void fx_caviar_paint(FxWin *f) {
  float sc = g_fxScale;
  for (int i = 0; i < f->n; i++) {
    const FxPart *p = &f->p[i];
    int age = f->frame - p->delay;
    if (age < 0 || age >= p->life) continue;
    float t = (float)age / p->life, grow = fx_clamp01((age + 1) / 5.0f);
    float a = t < 0.55f ? 0.92f : 0.92f * (1.0f - (t - 0.55f) / 0.45f);
    float radius = p->sz * sc * (0.5f + 0.5f * grow);
    int span = (int)(radius + 2.5f);
    for (int y = (int)p->y - span; y <= (int)p->y + span; y++)
      for (int x = (int)p->x - span; x <= (int)p->x + span; x++) {
        float d = hypotf(x + 0.5f - p->x, y + 0.5f - p->y);
        float cover = fx_clamp01(radius + 0.6f - d);
        if (cover <= 0) continue;
        float core = fx_clamp01((radius - d) / 1.6f);
        fx_put(f, x, y, 214.0f + 41.0f * core, 214.0f + 41.0f * core, 222.0f + 33.0f * core, a * cover);
      }
  }
}

static void fx_seed_new(FxWin *f, int kind, int S) {
  float c = S / 2.0f;
  if (kind == 5) { /* три кольца одно за другим */
    f->n = FX_RINGS;
    f->frames = FX_RING_FRAMES;
    for (int i = 0; i < FX_RINGS; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      p->x = c;
      p->y = c;
      p->delay = i * 5;
      p->life = FX_RING_FRAMES - p->delay;
      p->sz = 40.0f - i * 8.0f;
    }
  } else if (kind == 6) { /* конфетти: вверх и в стороны, потом осыпается */
    static const BYTE pal[][3] = {{239, 68, 68},  {249, 115, 22}, {234, 179, 8},  {34, 197, 94},
                                  {14, 165, 233}, {99, 102, 241}, {217, 70, 239}, {236, 72, 153}};
    f->n = FX_CONFETTI;
    f->frames = FX_CONFETTI_FRAMES;
    for (int i = 0; i < FX_CONFETTI; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      p->x = c + (fx_rand() - 0.5f) * 6.0f * g_fxScale;
      p->y = c + (fx_rand() - 0.5f) * 6.0f * g_fxScale;
      p->vx = (fx_rand() - 0.5f) * 5.0f * g_fxScale;
      p->vy = -(2.6f + fx_rand() * 3.4f) * g_fxScale;
      p->sz = 3.0f + fx_rand() * 2.0f;
      p->spr = 14 + (int)(fx_rand() * 10);
      p->tw = fx_rand() * 6.2832f;
      p->rev = (fx_rand() - 0.5f) * 0.6f;
      p->delay = (int)(fx_rand() * 3);
      p->life = FX_CONFETTI_FRAMES - p->delay;
      const BYTE *col = pal[i % 8];
      p->r = col[0];
      p->g = col[1];
      p->b = col[2];
    }
  } else if (kind == 7) { /* три разряда в разные стороны */
    f->n = FX_BOLTS;
    f->frames = FX_BOLT_FRAMES;
    float base = fx_rand() * 6.2832f;
    for (int i = 0; i < FX_BOLTS; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      p->x = c;
      p->y = c;
      p->tw = base + i * (6.2832f / FX_BOLTS) + (fx_rand() - 0.5f) * 0.8f;
      p->sz = 58.0f + fx_rand() * 26.0f;
      p->spr = (int)(fx_rand() * 100000);
      p->delay = i;
      p->life = FX_BOLT_FRAMES - p->delay;
    }
  } else if (kind == 9) { /* надрезы сеткой: два пучка почти параллельных, крест-накрест */
    f->n = FX_CUTS;
    f->frames = FX_CUT_FRAMES;
    float base = fx_rand() * 3.1416f;
    for (int i = 0; i < FX_CUTS; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      float a = base + (i & 1 ? 1.5708f : 0.0f) + (fx_rand() - 0.5f) * 0.25f, off = ((i >> 1) - 1.5f) * 13.0f * g_fxScale;
      p->tw = a;
      p->x = c - sinf(a) * off + (fx_rand() - 0.5f) * 8.0f * g_fxScale;
      p->y = c + cosf(a) * off + (fx_rand() - 0.5f) * 8.0f * g_fxScale;
      p->sz = 70.0f + fx_rand() * 50.0f;
      p->rev = 3.0f;
      p->delay = i * 2;
      p->life = FX_CUT_FRAMES - p->delay;
    }
  } else if (kind == 10) { /* «Пурпур»: искры разлетаются после слияния */
    f->n = FX_PURPLE_SPARKS;
    f->frames = FX_PURPLE_FRAMES;
    for (int i = 0; i < FX_PURPLE_SPARKS; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      float a = fx_rand() * 6.2832f, v = (2.5f + fx_rand() * 3.0f) * g_fxScale;
      p->x = c;
      p->y = c;
      p->vx = cosf(a) * v;
      p->vy = sinf(a) * v;
      p->sz = 2.5f + fx_rand() * 2.0f;
      p->delay = FX_PURPLE_MERGE + (int)(fx_rand() * 3);
      p->life = FX_PURPLE_FRAMES - p->delay;
    }
  } else if (kind == 14) { /* «Бесконечность»: пылинки со всех сторон, барьер и волны рисуются в fx_inf_paint */
    f->n = FX_INF_DUST;
    f->frames = FX_INF_FRAMES;
    for (int i = 0; i < FX_INF_DUST; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      p->tw = i * (6.2832f / FX_INF_DUST) + (fx_rand() - 0.5f) * 0.35f;
      p->sz = 78.0f + fx_rand() * 14.0f; /* откуда летит */
      p->rev = 36.0f + fx_rand() * 18.0f; /* где застынет: не долетев до барьера */
      p->delay = (int)(fx_rand() * 5);
      p->life = FX_INF_FRAMES - p->delay;
    }
  } else if (kind == 11) { /* перья разлетаются и плывут вниз, покачиваясь */
    f->n = FX_FEATHERS;
    f->frames = FX_FEATHER_FRAMES;
    for (int i = 0; i < FX_FEATHERS; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      float a = i * (6.2832f / FX_FEATHERS) + fx_rand() * 0.6f, v = (3.0f + fx_rand() * 2.5f) * g_fxScale;
      p->x = c;
      p->y = c;
      p->vx = cosf(a) * v;
      p->vy = sinf(a) * v - 1.2f * g_fxScale;
      p->tw = a;
      p->rev = (fx_rand() - 0.5f) * 0.16f;
      p->sz = 15.0f + fx_rand() * 7.0f;
      p->spr = (int)(fx_rand() * 100);
      p->life = FX_FEATHER_FRAMES;
    }
  } else if (kind == 12) { /* чернила: капли из кляксы; p[0].tw — узор края */
    f->n = FX_INK_DROPS;
    f->frames = FX_INK_FRAMES;
    for (int i = 0; i < FX_INK_DROPS; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      float a = fx_rand() * 6.2832f, v = (4.0f + fx_rand() * 4.0f) * g_fxScale;
      p->x = c;
      p->y = c;
      p->vx = cosf(a) * v;
      p->vy = sinf(a) * v;
      p->sz = 2.0f + fx_rand() * 3.0f;
      p->tw = fx_rand() * 6.2832f;
      p->delay = 1 + (int)(fx_rand() * 3);
      p->life = FX_INK_FRAMES - p->delay;
    }
  } else if (kind == 13) { /* яд: брызги и четыре пузыря */
    f->n = FX_VENOM_DROPS + 4;
    f->frames = FX_VENOM_FRAMES;
    for (int i = 0; i < f->n; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      if (i < FX_VENOM_DROPS) {
        p->x = c;
        p->y = c;
        p->vx = (fx_rand() - 0.5f) * 6.0f * g_fxScale;
        p->vy = -(2.5f + fx_rand() * 4.0f) * g_fxScale;
        p->sz = 2.8f + fx_rand() * 2.2f;
        p->delay = (int)(fx_rand() * 3);
        p->life = 26 + (int)(fx_rand() * 10);
      } else {
        p->spr = 1;
        p->x = c + (fx_rand() - 0.5f) * 24.0f * g_fxScale;
        p->y = c + (fx_rand() - 0.5f) * 16.0f * g_fxScale;
        p->sz = 3.0f + fx_rand() * 4.0f;
        p->delay = 4 + (int)(fx_rand() * 16);
        p->life = 12 + (int)(fx_rand() * 8);
      }
    }
  } else if (kind == 15) { /* «Пятна»: белые пятна из точки нажатия */
    f->n = FX_CAVIAR_DROPS + FX_CAVIAR_LEAVES;
    f->frames = FX_CAVIAR_FRAMES;
    for (int i = 0; i < f->n; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      float a = fx_rand() * 6.2832f;
      float v = (1.6f + fx_rand() * 2.4f) * g_fxScale;
      p->x = c;
      p->y = c;
      p->vx = cosf(a) * v;
      p->vy = sinf(a) * v;
      p->sz = i < FX_CAVIAR_DROPS ? 3.2f + fx_rand() * 2.4f : 7.0f + fx_rand() * 3.0f;
      p->delay = (int)(fx_rand() * 4);
      p->life = FX_CAVIAR_FRAMES - p->delay;
    }
  } else if (kind == 16) { /* лепестки: вверх и в стороны, потом опадают, кружась */
    f->n = FX_PETALS;
    f->frames = FX_PETAL_FRAMES;
    for (int i = 0; i < FX_PETALS; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      float ang = fx_rand() * 6.2832f;
      float v = (1.2f + fx_rand() * 2.2f) * g_fxScale;
      p->x = c + (fx_rand() - 0.5f) * 4.0f * g_fxScale;
      p->y = c + (fx_rand() - 0.5f) * 4.0f * g_fxScale;
      p->vx = cosf(ang) * v;
      p->vy = sinf(ang) * v - 1.6f * g_fxScale;
      p->sz = 4.5f + fx_rand() * 3.0f;
      p->tw = fx_rand() * 6.2832f;
      p->rev = (fx_rand() - 0.5f) * 0.25f;
      p->spr = (int)(fx_rand() * 1000);
      p->delay = (int)(fx_rand() * 5);
      p->life = FX_PETAL_FRAMES - p->delay;
    }
  } else { /* сердечки всплывают, покачиваясь */
    static const BYTE pal[][3] = {{255, 77, 130}, {255, 120, 170}, {230, 30, 80}, {255, 150, 190}, {244, 63, 94}};
    f->n = FX_HEARTS;
    f->frames = FX_HEART_FRAMES;
    for (int i = 0; i < FX_HEARTS; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      p->x = c + (i - (FX_HEARTS - 1) / 2.0f) * 9.0f * g_fxScale + (fx_rand() - 0.5f) * 6.0f * g_fxScale;
      p->y = c + (fx_rand() - 0.5f) * 6.0f * g_fxScale;
      p->vy = -(1.0f + fx_rand() * 0.9f) * g_fxScale;
      p->sz = 5.0f + fx_rand() * 3.0f;
      p->tw = fx_rand() * 6.2832f;
      p->delay = (int)(fx_rand() * 8);
      p->life = FX_HEART_FRAMES - p->delay;
      const BYTE *col = pal[i % 5];
      p->r = col[0];
      p->g = col[1];
      p->b = col[2];
    }
  }
}

static void fx_paint_new(FxWin *f) {
  float c = f->W / 2.0f;
  if (f->kind == 10) {
    fx_purple_paint(f);
    return;
  }
  if (f->kind == 14) {
    fx_inf_paint(f);
    return;
  }
  if (f->kind == 12) {
    fx_ink_paint(f);
    return;
  }
  if (f->kind == 13) {
    fx_venom_paint(f);
    return;
  }
  if (f->kind == 15) {
    fx_caviar_paint(f);
    return;
  }
  if (f->kind == 11 && f->frame < 5)
    fx_glow_dot(f, c, c, 20.0f * g_fxScale, 90, 70, 150, 0.5f * (1.0f - f->frame / 5.0f));
  if (f->kind == 9 && f->frame < 5)
    fx_glow_dot(f, c, c, 26.0f * g_fxScale, 200, 0, 20, 0.45f * (1.0f - f->frame / 5.0f));
  if (f->kind == 5 && f->frame < 6)
    fx_glow_dot(f, c, c, 5.0f * g_fxScale, 120, 210, 255, 0.8f * (1.0f - f->frame / 6.0f));
  if (f->kind == 7 && f->frame < 5)
    fx_glow_dot(f, c, c, 16.0f * g_fxScale, 150, 200, 255, 0.7f * (1.0f - f->frame / 5.0f));
  for (int i = 0; i < f->n; i++) {
    const FxPart *p = &f->p[i];
    int age = f->frame - p->delay;
    if (f->kind == 5) fx_ring_part(f, p, age);
    else if (f->kind == 6) fx_confetti_part(f, p, age);
    else if (f->kind == 7) fx_bolt_part(f, p, age);
    else if (f->kind == 9) fx_cut_part(f, p, age);
    else if (f->kind == 11) fx_feather_part(f, p, age);
    else if (f->kind == 16) fx_petal_part(f, p, age);
    else fx_heart_part(f, p, age);
  }
}

static void fx_step_new(FxWin *f) {
  for (int i = 0; i < f->n; i++) {
    FxPart *p = &f->p[i];
    int age = f->frame - p->delay;
    if (age < 0) continue;
    if (f->kind == 6) {
      p->x += p->vx;
      p->y += p->vy;
      p->vx *= 0.96f;
      p->vy = p->vy * 0.97f + 0.30f * g_fxScale;
      p->tw += p->rev;
    } else if (f->kind == 10 || f->kind == 12) {
      p->x += p->vx;
      p->y += p->vy;
      p->vx *= f->kind == 12 ? 0.82f : 0.93f;
      p->vy *= f->kind == 12 ? 0.82f : 0.93f;
    } else if (f->kind == 11) {
      p->x += p->vx + sinf(age * 0.16f + p->spr) * 0.7f * g_fxScale;
      p->y += p->vy;
      p->vx *= 0.88f;
      p->vy = p->vy * 0.88f + 0.10f * g_fxScale;
      p->tw += p->rev + sinf(age * 0.16f + p->spr) * 0.03f;
    } else if (f->kind == 13) {
      if (p->spr) continue;
      p->x += p->vx;
      p->y += p->vy;
      p->vx *= 0.97f;
      p->vy += 0.35f * g_fxScale;
    } else if (f->kind == 15) {
      p->x += p->vx;
      p->y += p->vy;
      p->vx *= 0.72f;
      p->vy *= 0.72f;
    } else if (f->kind == 16) {
      p->x += p->vx + sinf(age * 0.14f + p->spr) * 0.5f * g_fxScale;
      p->y += p->vy;
      p->vx *= 0.92f;
      p->vy = p->vy * 0.90f + 0.12f * g_fxScale;
      p->tw += p->rev;
    } else if (f->kind == 8) {
      p->x += sinf(age * 0.18f + p->tw) * 0.7f * g_fxScale;
      p->y += p->vy;
      p->vy *= 0.985f;
    }
  }
}

/* кадр: пустой буфер и поверх — каждая звёздочка своим цветом. Только буфер:
   на экран его выводит fx_render, а просмотр в Настройках — сам */
static void fx_paint(FxWin *f) {
  int W = f->W, H = f->H;
  int S = W; /* звёздочки, язычки и кольцо — только в квадратных окнах */
  if (f->shatter) {
    fx_shatter_render(f);
    return;
  }
  memset(f->bits, 0, (size_t)W * H * 4);
  if (f->kind >= 5) {
    fx_paint_new(f);
    return;
  }
  if (f->slash) {
    if (W != H && f->frame >= 2 && f->frame < 12) { /* большой разрез: экран на миг краснеет */
      BYTE r = (BYTE)(26 * (1.0f - (f->frame - 2) / 10.0f));
      DWORD c = (DWORD)r << 24 | (DWORD)r << 16;
      for (size_t i = 0; i < (size_t)W * H; i++) ((DWORD *)f->bits)[i] = c;
    }
    for (int i = 0; i < f->n; i++) fx_slash_part(f, &f->p[i], f->frame - f->p[i].delay);
    return;
  }
  if (f->scratch) {
    for (int i = 0; i < f->n; i++) fx_scratch_part(f, &f->p[i], f->frame - f->p[i].delay);
    return;
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
    return;
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
}

static void fx_render(FxWin *f) {
  fx_paint(f);
  POINT src = {0, 0};
  SIZE sz = {f->W, f->H};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(f->w, NULL, &f->org, &sz, f->dc, &src, 0, &bf, ULW_ALPHA);
}

static void fx_step(FxWin *f) {
  if (f->kind >= 5) {
    fx_step_new(f);
    return;
  }
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

/* какой эффект при нажатии: 0 нет, 1 звёздочки, 2 пламя, 3 царапины, 4 разрез,
   5 кольца, 6 конфетти, 7 молния, 8 сердечки. Если эффект выбран отдельно
   (pick >= 0), он важнее курсора; иначе — свой у курсора: фея со звёздочками,
   дракон с пламенем, кот с царапинами, у новых стилей (styles.c) — свои,
   у «Баклажана» — белые пятна */
static int fx_kind_for(int skin, int pick) {
  if (pick >= 0) return pick;
  if (skin == 3) return g_sparkle ? 1 : 0;
  if (skin == 4) return g_flame ? 2 : 0;
  if (skin == 5) return g_catFx == 1 ? 3 : (g_catFx == 2 ? 1 : 0);
  if (skin == 7) return 11;  /* Ворон — свои «Перья» */
  if (skin == 8) return 12;  /* Осьминог — свои «Чернила» */
  if (skin == 9) return 13;  /* Скорпион — свой «Яд» */
  if (skin == 10) return 4;  /* Разрез — разрез */
  if (skin == 11) return 9;  /* Сукуна — своё «Рассечение» */
  if (skin == 12) return 14; /* Годжо — своя «Бесконечность» */
  if (skin == 13) return 15; /* Баклажан — белые пятна */
  if (skin == SKIN_K8) return 16; /* Тяночка — лепестки сакуры */
  return 0;
}

static int fx_kind(void) { return fx_kind_for(g_skin, g_fxPick); }

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
static void fx_big_seed(FxWin *f, int W, int H);
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
  fx_big_seed(f, W, H);
  f->busy = TRUE;
  fx_render(f);
  SetWindowPos(f->w, HWND_TOPMOST, f->org.x, f->org.y, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetTimer(f->w, 1, 16, NULL);
}

/* разрезы большого разреза в буфере W×H (без окна: так его снимает и проверка) */
static void fx_big_seed(FxWin *f, int W, int H) {
  f->frame = 0;
  f->kind = 4;
  f->flame = FALSE;
  f->scratch = FALSE;
  f->slash = TRUE;
  f->n = FX_BIG_CUTS;
  f->frames = FX_BIG_FRAMES;
  float bend = (fx_rand() < 0.5f ? 1.0f : -1.0f) * 0.035f;
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
    p->sz = i == 0 ? 22.0f : 5.0f; /* полуширина в середине, в пикселях × масштаб */
    p->tw = bend;
    p->delay = i * 3;
    p->rev = i == 0 ? 5.0f : 4.0f;
    p->life = FX_BIG_FRAMES - p->delay;
  }
  f->n += fx_slash_sparks(&f->p[FX_BIG_CUTS], &f->p[0], 12, 9.0f, 3.0f, 3);
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
    p->sz = 6.0f;
    p->rev = 4.0f;
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
  /* края кусков (по два пикселя с каждой стороны разлома) — старший бит: так
     при каждом кадре не надо сравнивать соседей */
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      size_t i = (size_t)y * W + x;
      BYTE m = g_shat.shard[i] & 0x7F;
      if ((x > 1 && ((g_shat.shard[i - 1] & 0x7F) != m || (g_shat.shard[i - 2] & 0x7F) != m)) ||
          (x + 2 < W && ((g_shat.shard[i + 1] & 0x7F) != m || (g_shat.shard[i + 2] & 0x7F) != m)) ||
          (y > 1 && ((g_shat.shard[i - W] & 0x7F) != m || (g_shat.shard[i - 2 * W] & 0x7F) != m)) ||
          (y + 2 < H && ((g_shat.shard[i + W] & 0x7F) != m || (g_shat.shard[i + 2 * W] & 0x7F) != m)))
        g_shat.shard[i] |= 0x80;
    }
  /* куда уезжает кусок: от середины экрана в сторону его центра, с разбросом,
     и чуть вниз: экран расходится трещинами, а не улетает */
  for (int m = 0; m < FX_SHATTER_PIECES; m++) {
    float dx0 = 0.0f, dy0 = 0.0f;
    if (cnt[m]) {
      dx0 = (float)(sumx[m] / cnt[m]) - cx;
      dy0 = (float)(sumy[m] / cnt[m]) - cy;
    }
    float ang = atan2f(dy0, dx0) + (fx_rand() - 0.5f) * 0.8f;
    float dist = side * (0.05f + fx_rand() * 0.07f);
    g_shat.outx[m] = cosf(ang) * dist;
    g_shat.outy[m] = sinf(ang) * dist + H * (0.03f + fx_rand() * 0.08f);
  }
  return TRUE;
}

/* раскол экрана на куски: снимок экрана в момент нажатия, семь больших разрезов
   через середину под разными углами. Разрезы сначала видны поверх экрана, потом
   белая вспышка, куски расходятся от середины с раскалёнными алыми краями,
   замирают и сходятся обратно */
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
  f->kind = 4;
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

static void fx_seed(FxWin *f, int kind, int S);

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
  fx_seed(f, kind, S);
  f->busy = TRUE;
  fx_render(f);
  SetWindowPos(f->w, HWND_TOPMOST, f->org.x, f->org.y, S, S,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetTimer(f->w, 1, 16, NULL);
}

/* частицы эффекта kind в квадратном буфере S×S, точка нажатия — посередине.
   Окна не трогает: так же заводится облачко в окошке просмотра Настроек */
static void fx_seed(FxWin *f, int kind, int S) {
  static const BYTE pal[][3] = {
      {255, 105, 180}, {255, 215, 64},  {186, 120, 255}, {90, 200, 255},
      {80, 230, 170},  {255, 140, 90},  {255, 255, 255}, {255, 70, 140},
  };
  f->frame = 0;
  f->kind = kind;
  f->shatter = FALSE;
  f->flame = kind == 2;
  f->scratch = kind == 3;
  f->slash = kind == 4;
  if (kind >= 5) {
    fx_seed_new(f, kind, S);
    return;
  }
  if (f->slash) {
    /* изогнутый след клинка через точку нажатия, примерно под 45°, рядом —
       тонкий отголосок, и из разреза летят искры */
    f->frames = FX_SLASH_FRAMES;
    float dir = fx_rand() < 0.5f ? 1.0f : -1.0f;
    float a = 0.75f + (fx_rand() - 0.5f) * 0.3f;
    float dx = dir * cosf(a), dy = sinf(a);
    float len = 120.0f * g_fxScale, bend = dir * 0.11f;
    for (int i = 0; i < 2; i++) {
      FxPart *p = &f->p[i];
      memset(p, 0, sizeof(*p));
      float k = i ? 0.78f : 1.0f, off = i ? 9.0f * g_fxScale : 0.0f;
      p->x = S / 2.0f - dx * len * k / 2 + dy * off; /* по центру: через точку нажатия */
      p->y = S / 2.0f - dy * len * k / 2 - dx * off;
      p->vx = dx * len * k;
      p->vy = dy * len * k;
      p->tw = bend;
      p->sz = i ? 1.2f : 4.4f;
      p->rev = 3.0f;
      p->delay = i * 2;
      p->life = FX_SLASH_FRAMES - 2 - p->delay;
    }
    f->n = 2 + fx_slash_sparks(&f->p[2], &f->p[0], 6, 2.6f, 1.6f, 2);
    return;
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
    return;
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
    return;
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
