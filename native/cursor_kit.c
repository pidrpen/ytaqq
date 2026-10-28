/* ---- «Современный» набор курсоров (с 2026.10.29) ---------------------------

   Все шестнадцать видов рисуются кодом при выборе — под размер указателя
   (32 / 48 / 64 точки и крупнее, если выбран размер «Крупнее» / «Крупный»).
   Формы заданы расстоянием до контура (SDF) в сетке 32×32: от него сразу
   получаются ровные края, обводка любой толщины, ореол и мягкая тень, без
   лесенки и без размытия картинки при увеличении.

   Настраивается карточкой «Курсор» на вкладке «Кастомизация» — как «Панель
   Shift»: четыре стиля и переключатели.
   - Стиль: «Светлый» (белый с тёмной каймой, как привычная стрелка),
     «Тёмный» (графит с белой каймой), «Акцент» (цвет темы с белой каймой,
     стрелка без хвоста), «Стекло» (полупрозрачный, сквозь него виден фон).
   - Размер: обычный, крупнее (×1,25), крупный (×1,5).
   - Обводка: кайма толще и снаружи тонкий ореол другого цвета — курсор
     виден и на белом, и на чёрном, и на пёстром.
   - Тень: мягкая тень вниз вправо.
   - Цвет темы: значки (справка, место, ожидание) — акцентом темы; иначе синим.
   - Анимация: «Ожидание» и «Фоновая работа» крутятся (.ani, 12 кадров).
   - Эффект нажатия: своя «Волна» цвета акцента, если в «Эффекте» выбрано
     «Как у курсора».
   Значения (g_kitStyle, g_kitSize и переключатели — в cursorpad.c) — в
   cursor.txt после полей панели Shift. */

#define KIT_FRAMES 12 /* кадров у крутящихся видов */
#define KIT_MAXP 10

static const wchar_t *const kKitNames[KIT_STYLES] = {L"Светлый", L"Тёмный", L"Акцент", L"Стекло"};
static const wchar_t *const kKitSzNames[KIT_SIZES] = {L"Обычный", L"Крупнее", L"Крупный"};

static HCURSOR g_kitCur[ST_SLOTS]; /* неподвижные виды под нынешние настройки */
static unsigned g_kitSig;           /* под какие настройки нарисованы */
static unsigned g_kitAniSig;        /* под какие записаны .ani крутящихся видов */

/* ---- расстояния до фигур в сетке 32×32: внутри минус, снаружи плюс ---- */

static float kd_clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

/* до отрезка — для «капсулы» вычесть радиус */
static float kd_seg(float px, float py, float ax, float ay, float bx, float by) {
  float wx = px - ax, wy = py - ay, ex = bx - ax, ey = by - ay;
  float t = kd_clamp((wx * ex + wy * ey) / (ex * ex + ey * ey + 1e-6f), 0.0f, 1.0f);
  return hypotf(wx - ex * t, wy - ey * t);
}

/* многоугольник (точный, со знаком по чётности пересечений) */
static float kd_poly(const float *v, int n, float px, float py) {
  float d = (px - v[0]) * (px - v[0]) + (py - v[1]) * (py - v[1]), s = 1.0f;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float ex = v[j * 2] - v[i * 2], ey = v[j * 2 + 1] - v[i * 2 + 1];
    float wx = px - v[i * 2], wy = py - v[i * 2 + 1];
    float t = kd_clamp((wx * ex + wy * ey) / (ex * ex + ey * ey + 1e-6f), 0.0f, 1.0f);
    float bx = wx - ex * t, by = wy - ey * t;
    d = fminf(d, bx * bx + by * by);
    BOOL c1 = py >= v[i * 2 + 1], c2 = py < v[j * 2 + 1], c3 = ex * wy > ey * wx;
    if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
  }
  return s * sqrtf(d);
}

static float kd_circle(float px, float py, float cx, float cy, float r) { return hypotf(px - cx, py - cy) - r; }

/* прямоугольник со скруглёнными углами */
static float kd_box(float px, float py, float x0, float y0, float x1, float y1, float r) {
  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
  float qx = fabsf(px - cx) - ((x1 - x0) * 0.5f - r), qy = fabsf(py - cy) - ((y1 - y0) * 0.5f - r);
  return hypotf(fmaxf(qx, 0.0f), fmaxf(qy, 0.0f)) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

/* дуга кольца радиуса R толщиной w: от угла a0 по часовой на span (радианы),
   концы круглые. *t — где точка вдоль дуги, 0 у хвоста, 1 у головы */
static float kd_arc(float px, float py, float cx, float cy, float R, float w, float a0, float span, float *t) {
  float dx = px - cx, dy = py - cy;
  float a = atan2f(dy, dx) - a0;
  while (a < 0) a += 6.2831853f;
  while (a >= 6.2831853f) a -= 6.2831853f;
  if (a <= span) {
    *t = a / span;
    return fabsf(hypotf(dx, dy) - R) - w * 0.5f;
  }
  float h0x = cx + cosf(a0) * R, h0y = cy + sinf(a0) * R;
  float h1x = cx + cosf(a0 + span) * R, h1y = cy + sinf(a0 + span) * R;
  float d0 = hypotf(px - h0x, py - h0y), d1 = hypotf(px - h1x, py - h1y);
  *t = d0 < d1 ? 0.0f : 1.0f;
  return fminf(d0, d1) - w * 0.5f;
}

/* ---- виды ---- */

enum { KR_BODY, KR_ACC, KR_RED, KR_MARK, KR_LINE }; /* тело, значок акцентом, запрет, знак на значке, складка */

typedef struct {
  float d, t; /* t — оттенок вдоль дуги ожидания */
  int role;
} KitPart;

/* стрелка с хвостом — «Светлый», «Тёмный», «Стекло» */
static const float kKitArrow[] = {3.2f, 2.6f, 3.2f, 22.9f, 7.9f, 18.5f, 11.0f, 25.6f, 14.1f, 24.3f, 11.1f, 17.3f, 17.4f, 17.3f};
/* наконечник без хвоста, с выемкой — «Акцент» */
static const float kKitPointer[] = {3.4f, 2.9f, 3.4f, 22.2f, 8.6f, 17.6f, 16.6f, 17.0f};
/* двусторонняя стрелка ↔ */
static const float kKitWE[] = {2.6f, 16, 9.2f, 9.6f, 9.2f, 14.3f, 22.8f, 14.3f, 22.8f, 9.6f, 29.4f, 16,
                               22.8f, 22.4f, 22.8f, 17.7f, 9.2f, 17.7f, 9.2f, 22.4f};
static const float kKitUp[] = {16, 2.6f, 24.8f, 11.6f, 18.2f, 11.6f, 18.2f, 28.8f, 13.8f, 28.8f, 13.8f, 11.6f, 7.2f, 11.6f};
#define KD_N(a) ((int)(sizeof(a) / sizeof(a[0]) / 2))

static float kd_arrow(float x, float y) {
  if (g_kitStyle == 2) return kd_poly(kKitPointer, KD_N(kKitPointer), x, y) - 1.0f; /* сильно скруглён */
  return kd_poly(kKitArrow, KD_N(kKitArrow), x, y) - 0.4f;
}

/* ↔, повёрнутая на угол ang вокруг середины и сжатая в sc раз */
static float kd_we(float x, float y, float ang, float sc) {
  float c = cosf(ang), s = sinf(ang), dx = x - 16, dy = y - 16;
  float u = (dx * c + dy * s) / sc, v = (-dx * s + dy * c) / sc;
  return (kd_poly(kKitWE, KD_N(kKitWE), 16 + u, 16 + v) - 0.5f) * sc;
}

/* наконечник стрелки «во все стороны», смотрит под углом ang */
static float kd_head(float x, float y, float ang) {
  static const float tri[] = {13.7f, 0, 8.3f, -4.7f, 8.3f, 4.7f};
  float c = cosf(ang), s = sinf(ang), dx = x - 16, dy = y - 16;
  return kd_poly(tri, 3, dx * c + dy * s, -dx * s + dy * c) - 0.45f;
}

/* «?» на значке с центром (cx, cy) */
static float kd_question(float x, float y, float cx, float cy) {
  float t;
  float arc = kd_arc(x, y, cx, cy - 1.5f, 1.9f, 1.25f, 3.30f, 4.10f, &t);
  float stem = kd_seg(x, y, cx + 0.15f, cy + 0.55f, cx, cy + 1.25f) - 0.62f;
  float dot = kd_circle(x, y, cx, cy + 3.25f, 0.8f);
  return fminf(arc, fminf(stem, dot));
}

/* части вида slot (порядок kRias) в точке (x, y); frame — кадр крутящихся */
static int kit_shape(int slot, int frame, float x, float y, KitPart *p) {
  int n = 0;
#define KP(D, R) (p[n].d = (D), p[n].t = 1.0f, p[n].role = (R), n++)
  float spin = frame * 6.2831853f / KIT_FRAMES - 1.5708f;
  switch (slot) {
  case 1: /* текст: тонкая планка с засечками */
    KP(fminf(kd_seg(x, y, 16, 6.6f, 16, 25.4f) - 0.85f,
             fminf(kd_seg(x, y, 12.4f, 4.9f, 19.6f, 4.9f) - 0.85f, kd_seg(x, y, 12.4f, 27.1f, 19.6f, 27.1f) - 0.85f)),
       KR_BODY);
    break;
  case 2: { /* рука: указательный вверх, три пальца поджаты, большой в сторону */
    float fi = kd_seg(x, y, 12.6f, 4.3f, 12.6f, 16.5f) - 2.25f;
    float fm = kd_seg(x, y, 16.9f, 12.9f, 16.9f, 18.0f) - 2.15f;
    float fr = kd_seg(x, y, 21.0f, 13.9f, 21.0f, 18.5f) - 2.05f;
    float fp = kd_seg(x, y, 24.8f, 15.9f, 24.8f, 19.6f) - 1.85f;
    float palm = kd_box(x, y, 10.4f, 16.2f, 26.7f, 28.0f, 4.6f);
    float th = kd_seg(x, y, 10.8f, 22.4f, 6.6f, 17.6f) - 2.15f;
    KP(fminf(fminf(fminf(fi, fm), fminf(fr, fp)), fminf(palm, th)), KR_BODY);
    /* складки между пальцами: край соседнего пальца внутри кисти */
    float ln = 1e9f;
    if (y > 11.0f && y < 19.6f && x > 12.6f) ln = fminf(ln, fabsf(fi) - 0.42f);
    if (y < 19.8f && x > 16.9f) ln = fminf(ln, fabsf(fm) - 0.42f);
    if (y < 20.2f && x > 21.0f) ln = fminf(ln, fabsf(fr) - 0.42f);
    if (x > 9.6f && x < 12.4f && y > 19.0f) ln = fminf(ln, fabsf(th) - 0.42f);
    KP(ln, KR_LINE);
    break;
  }
  case 4: { /* нельзя: красное кольцо, перечёркнутое */
    float ring = fabsf(kd_circle(x, y, 16, 16, 9.0f)) - 1.7f;
    float bar = kd_seg(x, y, 10.2f, 10.2f, 21.8f, 21.8f) - 1.7f;
    KP(fminf(ring, fmaxf(bar, kd_circle(x, y, 16, 16, 9.0f))), KR_RED);
    break;
  }
  case 5: /* точный выбор: перекрестье с просветом и точкой */
    KP(fminf(fminf(fminf(kd_seg(x, y, 16, 3.2f, 16, 12.4f), kd_seg(x, y, 16, 19.6f, 16, 28.8f)),
                   fminf(kd_seg(x, y, 3.2f, 16, 12.4f, 16), kd_seg(x, y, 19.6f, 16, 28.8f, 16))) - 0.8f,
            kd_circle(x, y, 16, 16, 1.25f)),
       KR_BODY);
    break;
  case 6: KP(kd_we(x, y, 0, 1.0f), KR_BODY); break;                        /* ↔ */
  case 7: KP(kd_we(x, y, 1.5708f, 1.0f), KR_BODY); break;                  /* ↕ */
  case 8: KP(kd_we(x, y, 0.7854f, 0.86f), KR_BODY); break;                 /* ↘ */
  case 9: KP(kd_we(x, y, -0.7854f, 0.86f), KR_BODY); break;                /* ↗ */
  case 10: { /* во все стороны: тонкий крест и четыре наконечника */
    float d = fminf(kd_box(x, y, 7.2f, 14.55f, 24.8f, 17.45f, 0.6f), kd_box(x, y, 14.55f, 7.2f, 17.45f, 24.8f, 0.6f));
    for (int i = 0; i < 4; i++) d = fminf(d, kd_head(x, y, i * 1.5708f));
    KP(d, KR_BODY);
    break;
  }
  case 11: KP(kd_poly(kKitUp, KD_N(kKitUp), x, y) - 0.5f, KR_BODY); break; /* ↑ */
  case 14: { /* ожидание: кольцо, по нему бежит дуга акцентом */
    float t;
    KP(fabsf(kd_circle(x, y, 16, 16, 8.6f)) - 2.0f, KR_BODY);
    float d = kd_arc(x, y, 16, 16, 8.6f, 4.0f, spin, 2.3f, &t);
    KP(d, KR_ACC);
    p[n - 1].t = t;
    break;
  }
  default: { /* стрелка и стрелка со значком */
    KP(kd_arrow(x, y), KR_BODY);
    const float bx = 24.2f, by = 24.0f;
    if (slot == 3) { /* справка */
      KP(kd_circle(x, y, bx, by, 6.1f), KR_ACC);
      KP(kd_question(x, y, bx, by), KR_MARK);
    } else if (slot == 12) { /* место: булавка */
      float head = kd_circle(x, y, bx, 20.6f, 4.6f);
      static const float tip[] = {20.2f, 22.4f, 28.2f, 22.4f, 24.2f, 30.2f};
      KP(fminf(head, kd_poly(tip, 3, x, y) - 0.3f), KR_ACC);
      KP(kd_circle(x, y, bx, 20.6f, 1.75f), KR_MARK);
    } else if (slot == 13) { /* человек */
      KP(fminf(kd_circle(x, y, bx, 18.4f, 3.0f), fmaxf(kd_circle(x, y, bx, 29.6f, 5.8f), y - 28.6f)), KR_ACC);
    } else if (slot == 15) { /* фоновая работа: маленькое кольцо с дугой */
      float t;
      KP(fabsf(kd_circle(x, y, bx, by, 4.7f)) - 1.5f, KR_BODY);
      float d = kd_arc(x, y, bx, by, 4.7f, 3.0f, spin, 2.3f, &t);
      KP(d, KR_ACC);
      p[n - 1].t = t;
    }
    break;
  }
  }
#undef KP
  return n;
}

/* острие: куда «смотрит» курсор, в сетке 32 */
static void kit_hotspot(int slot, float *hx, float *hy) {
  switch (slot) {
  case 1: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 14: *hx = 16, *hy = 16; return;
  case 2: *hx = 12.6f, *hy = 2.0f; return;
  case 11: *hx = 16, *hy = 2.0f; return;
  default:
    if (g_kitStyle == 2) *hx = 2.9f, *hy = 2.3f;
    else *hx = 2.6f, *hy = 1.9f;
  }
}

static BOOL kit_spins(int slot) { return slot == 14 || slot == 15; }

/* ---- цвета ---- */

typedef struct {
  float top[3], bot[3]; /* тело сверху и снизу */
  float out[3], outA;   /* кайма */
  float halo[3], haloA; /* ореол за каймой (с «Обводкой») */
  float acc[3], tail[3]; /* значки; хвост дуги ожидания */
  float mark[3];        /* знак на значке */
  float red[3];
  float bodyA;          /* «Стекло» — полупрозрачное */
} KitLook;

static COLORREF kit_accent(void) { return g_kitThemeAcc ? COL_SAGE : RGB(0, 103, 192); }

static void kit_rgb(float *d, COLORREF c) {
  d[0] = GetRValue(c), d[1] = GetGValue(c), d[2] = GetBValue(c);
}
static void kit_set(float *d, float r, float g, float b) { d[0] = r, d[1] = g, d[2] = b; }

static void kit_look(KitLook *L) {
  memset(L, 0, sizeof(*L));
  COLORREF a = kit_accent();
  int luma = (GetRValue(a) * 299 + GetGValue(a) * 587 + GetBValue(a) * 114) / 1000;
  kit_rgb(L->acc, a);
  kit_set(L->mark, 255, 255, 255);
  if (luma > 165) kit_set(L->mark, 24, 26, 31); /* светлый акцент — знак тёмный */
  kit_set(L->red, 226, 54, 62);
  L->outA = 1.0f;
  L->haloA = 0.0f;
  L->bodyA = 1.0f;
  switch (g_kitStyle) {
  case 1: /* тёмный */
    kit_set(L->top, 58, 62, 70);
    kit_set(L->bot, 30, 32, 37);
    kit_set(L->out, 255, 255, 255);
    kit_set(L->halo, 0, 0, 0);
    L->haloA = 0.22f; /* графит с белой каймой и так виден везде — ореол едва заметен */
    break;
  case 2: /* акцент: тело цветом темы */
    kit_rgb(L->bot, a);
    kit_rgb(L->top, blend_rgb(a, RGB(255, 255, 255), 64));
    kit_set(L->out, 255, 255, 255);
    kit_set(L->halo, 0, 0, 0);
    L->haloA = 0.40f; /* белая кайма на белом не видна — тёмный ореол держит край */
    kit_set(L->acc, 255, 255, 255); /* значок белый, знак — акцентом */
    kit_rgb(L->mark, a);
    break;
  case 3: /* стекло */
    kit_set(L->top, 255, 255, 255);
    kit_set(L->bot, 236, 240, 246);
    kit_set(L->out, 22, 25, 31);
    L->outA = 0.88f;
    kit_set(L->halo, 255, 255, 255);
    L->haloA = 0.55f;
    L->bodyA = 0.55f;
    break;
  default: /* светлый */
    kit_set(L->top, 255, 255, 255);
    kit_set(L->bot, 238, 240, 244);
    kit_set(L->out, 24, 26, 31);
    kit_set(L->halo, 255, 255, 255);
    L->haloA = 0.22f;
  }
  for (int i = 0; i < 3; i++) L->tail[i] = L->acc[i] * 0.45f + (L->top[i] + L->bot[i]) * 0.275f;
}

static float kit_cov(float d, float k) { return kd_clamp(0.5f - d * k, 0.0f, 1.0f); }

/* вид slot (кадр frame) в N×N, BGRA без домножения альфы — как st_draw */
static void kit_draw(int slot, int frame, int N, BYTE *out) {
  KitLook L;
  kit_look(&L);
  float k = N / 32.0f;
  float ow = g_kitOutline ? 1.55f : 1.05f, hw = 0.95f;
  KitPart p[KIT_MAXP], q[KIT_MAXP];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float gx = (x + 0.5f) / k, gy = (y + 0.5f) / k;
      int n = kit_shape(slot, frame, gx, gy, p);
      float dall = 1e9f;
      for (int i = 0; i < n; i++)
        if (p[i].role != KR_MARK && p[i].role != KR_LINE) dall = fminf(dall, p[i].d);
      float a = 0, r = 0, g = 0, b = 0;
#define KIT_OVER(C, CA)                                                                                    \
  do {                                                                                                     \
    float ca_ = (CA), inv_ = 1.0f - ca_;                                                                   \
    r = (C)[0] * ca_ + r * inv_, g = (C)[1] * ca_ + g * inv_, b = (C)[2] * ca_ + b * inv_, a = ca_ + a * inv_; \
  } while (0)
/* заменить на долю c: полупрозрачное тело стекла не показывает кайму под собой */
#define KIT_PUT(C, CA, c)                                                                                  \
  do {                                                                                                     \
    float c_ = (c), al_ = (CA), inv_ = 1.0f - c_;                                                          \
    r = r * inv_ + (C)[0] * al_ * c_, g = g * inv_ + (C)[1] * al_ * c_, b = b * inv_ + (C)[2] * al_ * c_;  \
    a = a * inv_ + al_ * c_;                                                                               \
  } while (0)
      if (g_kitShadow) {
        int m = kit_shape(slot, frame, gx - 0.75f, gy - 1.35f, q);
        float ds = 1e9f;
        for (int i = 0; i < m; i++)
          if (q[i].role != KR_MARK && q[i].role != KR_LINE) ds = fminf(ds, q[i].d);
        float s = kd_clamp((2.4f - (ds - ow)) / 4.6f, 0.0f, 1.0f);
        static const float blk[3] = {0, 0, 0};
        if (s > 0) KIT_OVER(blk, s * s * (3 - 2 * s) * 0.30f);
      }
      if (g_kitOutline && L.haloA > 0) KIT_OVER(L.halo, kit_cov(dall - ow - hw, k) * L.haloA);
      KIT_OVER(L.out, kit_cov(dall - ow, k) * L.outA);
      float tt = kd_clamp(gy / 32.0f, 0, 1);
      for (int i = 0; i < n; i++) {
        float c = kit_cov(p[i].d, k);
        if (c <= 0) continue;
        float col[3];
        float al = 1.0f;
        switch (p[i].role) {
        case KR_BODY:
          for (int j = 0; j < 3; j++) col[j] = L.top[j] + (L.bot[j] - L.top[j]) * tt;
          if (L.bodyA < 1.0f) /* стекло: сверху плотнее, у края — светлая кромка */
            al = L.bodyA + 0.18f * (1.0f - tt) + 0.35f * kd_clamp(c - kit_cov(p[i].d + 1.0f, k), 0, 1);
          break;
        case KR_ACC:
          for (int j = 0; j < 3; j++) col[j] = L.tail[j] + (L.acc[j] - L.tail[j]) * (0.25f + 0.75f * p[i].t);
          break;
        case KR_RED: memcpy(col, L.red, sizeof(col)); break;
        case KR_MARK: memcpy(col, L.mark, sizeof(col)); break;
        default: /* складка — цветом каймы, только внутри тела */
          memcpy(col, L.out, sizeof(col));
          c = fminf(c, kit_cov(dall, k)) * 0.85f;
          al = L.outA;
        }
        KIT_PUT(col, al, c);
      }
#undef KIT_OVER
#undef KIT_PUT
      BYTE *o = out + ((size_t)y * N + x) * 4;
      if (a <= 0.004f) {
        o[0] = o[1] = o[2] = o[3] = 0;
      } else {
        o[0] = (BYTE)fminf(255.0f, b / a + 0.5f);
        o[1] = (BYTE)fminf(255.0f, g / a + 0.5f);
        o[2] = (BYTE)fminf(255.0f, r / a + 0.5f);
        o[3] = (BYTE)fminf(255.0f, a * 255.0f + 0.5f);
      }
    }
}

/* размер курсора: как у указателя Windows, с выбранным увеличением */
static int kit_size(void) {
  static const int mul[KIT_SIZES] = {4, 5, 6}; /* четверти */
  int N = st_cursor_size() * mul[g_kitSize >= 0 && g_kitSize < KIT_SIZES ? g_kitSize : 0] / 4;
  return N > 160 ? 160 : N;
}

/* подпись настроек: что-то сменилось — нарисовать заново */
static unsigned kit_sig(void) {
  unsigned s = (unsigned)kit_size();
  s = s * 31u + (unsigned)g_kitStyle;
  s = s * 31u + (g_kitOutline ? 1u : 0u) + (g_kitShadow ? 2u : 0u) + (g_kitThemeAcc ? 4u : 0u);
  s = s * 31u + (unsigned)kit_accent();
  return s ? s : 1u;
}

static HCURSOR kit_make(int slot, int frame, int N) {
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
  kit_draw(slot, frame, N, (BYTE *)bits);
  HBITMAP mask = CreateBitmap(N, N, 1, 1, NULL);
  if (!mask) {
    DeleteObject(color);
    return NULL;
  }
  {
    HDC mdc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(mdc, mask);
    PatBlt(mdc, 0, 0, N, N, BLACKNESS);
    SelectObject(mdc, old);
    DeleteDC(mdc);
  }
  float hx, hy;
  kit_hotspot(slot, &hx, &hy);
  ICONINFO ii;
  memset(&ii, 0, sizeof(ii));
  ii.fIcon = FALSE;
  ii.xHotspot = (DWORD)(hx * N / 32.0f);
  ii.yHotspot = (DWORD)(hy * N / 32.0f);
  ii.hbmMask = mask;
  ii.hbmColor = color;
  HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color);
  return c;
}

static void kit_ensure(void) {
  unsigned sig = kit_sig();
  if (sig == g_kitSig && g_kitCur[0]) return;
  int N = kit_size();
  for (int i = 0; i < ST_SLOTS; i++) {
    if (g_kitCur[i]) DestroyCursor(g_kitCur[i]);
    g_kitCur[i] = kit_make(i, 0, N);
  }
  g_kitSig = sig;
}

/* ---- крутящиеся виды: .ani из кадров (RIFF ACON, кадры — .cur 32 бита) ---- */

static void kit_put32(BYTE *p, DWORD v) { memcpy(p, &v, 4); }

/* записать .ani вида slot; TRUE — записан */
static BOOL kit_write_ani(const wchar_t *path, int slot, int N) {
  int maskRow = ((N + 31) / 32) * 4;
  DWORD img = 40u + (DWORD)N * N * 4 + (DWORD)maskRow * N; /* BITMAPINFOHEADER, цвет, маска */
  DWORD cur = 6 + 16 + img;                                  /* файл .cur одного кадра */
  DWORD frame = 8 + cur + (cur & 1);
  DWORD list = 4 + frame * KIT_FRAMES;
  DWORD total = 4 + (8 + 36) + (8 + list);
  BYTE *buf = (BYTE *)calloc(1, 8 + total);
  BYTE *px = (BYTE *)malloc((size_t)N * N * 4);
  if (!buf || !px) {
    free(buf);
    free(px);
    return FALSE;
  }
  float hx, hy;
  kit_hotspot(slot, &hx, &hy);
  BYTE *w = buf;
  memcpy(w, "RIFF", 4), kit_put32(w + 4, total), memcpy(w + 8, "ACON", 4), w += 12;
  memcpy(w, "anih", 4), kit_put32(w + 4, 36), w += 8;
  {
    DWORD anih[9] = {36, KIT_FRAMES, KIT_FRAMES, 0, 0, 0, 0, 5 /* 1/60 с: 12 кадров ≈ 1 с на оборот */, 1 /* AF_ICON */};
    memcpy(w, anih, 36), w += 36;
  }
  memcpy(w, "LIST", 4), kit_put32(w + 4, list), memcpy(w + 8, "fram", 4), w += 12;
  for (int f = 0; f < KIT_FRAMES; f++) {
    memcpy(w, "icon", 4), kit_put32(w + 4, cur), w += 8;
    BYTE *c = w;
    c[2] = 2, c[4] = 1; /* ICONDIR: тип 2 — курсор, один образ */
    c[6] = (BYTE)(N >= 256 ? 0 : N), c[7] = (BYTE)(N >= 256 ? 0 : N);
    WORD hsx = (WORD)(hx * N / 32.0f), hsy = (WORD)(hy * N / 32.0f);
    memcpy(c + 10, &hsx, 2), memcpy(c + 12, &hsy, 2);
    kit_put32(c + 14, img), kit_put32(c + 18, 22);
    BYTE *h = c + 22;
    kit_put32(h, 40), kit_put32(h + 4, (DWORD)N), kit_put32(h + 8, (DWORD)N * 2);
    h[12] = 1, h[14] = 32;
    kit_put32(h + 20, img - 40);
    kit_draw(slot, f, N, px);
    BYTE *rows = h + 40;
    for (int y = 0; y < N; y++) memcpy(rows + (size_t)y * N * 4, px + (size_t)(N - 1 - y) * N * 4, (size_t)N * 4);
    /* маска нулевая: всё решает альфа */
    w += cur + (cur & 1);
  }
  free(px);
  BOOL ok = FALSE;
  HANDLE fh = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (fh != INVALID_HANDLE_VALUE) {
    DWORD wr = 0;
    ok = WriteFile(fh, buf, 8 + total, &wr, NULL) && wr == 8 + total;
    CloseHandle(fh);
  }
  free(buf);
  return ok;
}

static void kit_ani_path(int slot, wchar_t *path) {
  wchar_t dir[MAX_PATH];
  if (g_dataDir[0]) lstrcpynW(dir, g_dataDir, MAX_PATH);
  else GetTempPathW(MAX_PATH, dir);
  size_t n = wcslen(dir);
  if (n && dir[n - 1] == L'\\') dir[n - 1] = 0;
  _snwprintf(path, MAX_PATH, L"%s\\kit_%s.ani", dir, kRias[slot].name);
  path[MAX_PATH - 1] = 0;
}

/* крутящийся вид slot — из своего .ani (пишется, когда сменились настройки) */
static HCURSOR kit_ani_load(int slot) {
  wchar_t path[MAX_PATH];
  kit_ani_path(slot, path);
  int N = kit_size();
  if (g_kitAniSig != g_kitSig) {
    BOOL ok = TRUE;
    for (int i = 0; i < ST_SLOTS; i++) {
      if (!kit_spins(i)) continue;
      wchar_t pi[MAX_PATH];
      kit_ani_path(i, pi);
      ok = kit_write_ani(pi, i, N) && ok;
    }
    if (!ok) return NULL;
    g_kitAniSig = g_kitSig;
  }
  return (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, N, N, LR_LOADFROMFILE);
}

/* поставить набор вместо курсоров Windows. Крутящиеся — без CopyCursor:
   копия .ani — один неподвижный кадр (как у «Риас») */
static void kit_apply(void) {
  kit_ensure();
  for (int i = 0; i < ST_SLOTS; i++) {
    if (g_kitAnim && kit_spins(i)) {
      HCURSOR c = kit_ani_load(i);
      if (c && SetSystemCursor(c, kRias[i].ocr)) continue;
    }
    set_slot(g_kitCur[i], kRias[i].ocr);
  }
}

/* неподвижный вид slot — для просмотра на вкладке «Курсор» */
static HCURSOR kit_cursor(int slot) {
  kit_ensure();
  return slot >= 0 && slot < ST_SLOTS ? g_kitCur[slot] : NULL;
}

static void kit_free(void) {
  for (int i = 0; i < ST_SLOTS; i++)
    if (g_kitCur[i]) {
      DestroyCursor(g_kitCur[i]);
      g_kitCur[i] = NULL;
    }
  g_kitSig = 0;
}

/* ---- живой просмотр в карточке «Курсор» ------------------------------------

   Две полосы — на светлом и на тёмном фоне — с главными видами в настоящий
   размер: стрелка, рука, текст, ожидание, фоновая работа, справка, нельзя,
   перемещение. Ожидание крутится (если включена анимация). Раз в пару
   секунд стрелка «нажимает» — показан эффект; щелчок по окошку — тоже. */

#define KPV_KINDS 8
static const int kKitPvSlot[KPV_KINDS] = {0, 2, 1, 14, 15, 3, 4, 10};
static HWND g_kpvHwnd;
static BYTE *g_kpvImg[ST_SLOTS][KIT_FRAMES]; /* картинки видов, под g_kpvSig */
static unsigned g_kpvSig;
static int g_kpvN, g_kpvTick;
static FxWin g_kpvFx;
static BOOL g_kpvFxOn;
static POINT g_kpvAt;

static void kpv_free(void) {
  for (int i = 0; i < ST_SLOTS; i++)
    for (int f = 0; f < KIT_FRAMES; f++) {
      free(g_kpvImg[i][f]);
      g_kpvImg[i][f] = NULL;
    }
  g_kpvSig = 0;
}

static const BYTE *kpv_img(int slot, int frame) {
  unsigned sig = kit_sig();
  if (sig != g_kpvSig) {
    kpv_free();
    g_kpvSig = sig;
    g_kpvN = kit_size();
  }
  if (!kit_spins(slot)) frame = 0;
  if (!g_kpvImg[slot][frame]) {
    g_kpvImg[slot][frame] = (BYTE *)malloc((size_t)g_kpvN * g_kpvN * 4);
    if (g_kpvImg[slot][frame]) kit_draw(slot, frame, g_kpvN, g_kpvImg[slot][frame]);
  }
  return g_kpvImg[slot][frame];
}

/* картинка BGRA (без домножения) поверх DIB W×H */
static void kpv_blend(BYTE *dst, int W, int H, const BYTE *src, int N, int ox, int oy) {
  if (!src) return;
  for (int y = 0; y < N; y++) {
    int yy = oy + y;
    if (yy < 0 || yy >= H) continue;
    for (int x = 0; x < N; x++) {
      int xx = ox + x;
      if (xx < 0 || xx >= W) continue;
      const BYTE *s = src + ((size_t)y * N + x) * 4;
      if (!s[3]) continue;
      BYTE *d = dst + ((size_t)yy * W + xx) * 4;
      int a = s[3], ia = 255 - a;
      d[0] = (BYTE)((s[0] * a + d[0] * ia) / 255);
      d[1] = (BYTE)((s[1] * a + d[1] * ia) / 255);
      d[2] = (BYTE)((s[2] * a + d[2] * ia) / 255);
    }
  }
}

static int kit_fx_kind(void); /* ниже: какой эффект нажатия у набора */

/* где ячейка вида i: полоса row (0 светлая, 1 тёмная) */
static void kpv_cells(const RECT *rc, int *cell, int *count, int *rowH, int *top) {
  int N = g_kpvN ? g_kpvN : kit_size();
  *cell = N + 14;
  if (*cell < 40) *cell = 40;
  *count = (rc->right - 16) / *cell;
  if (*count > KPV_KINDS) *count = KPV_KINDS;
  if (*count < 1) *count = 1;
  *top = 24;
  *rowH = (rc->bottom - *top - 4) / 2;
}

static void kpv_click(int x, int y) {
  int pick = g_fxPick;
  int kind = pick >= 0 ? pick : kit_fx_kind();
  if (!kind) {
    g_kpvFxOn = FALSE;
    return;
  }
  fx_init_sprites();
  int S = (int)(200 * g_fxScale);
  if (g_kpvFx.W != S) {
    free(g_kpvFx.bits);
    memset(&g_kpvFx, 0, sizeof(g_kpvFx));
    g_kpvFx.bits = (BYTE *)malloc((size_t)S * S * 4);
    if (!g_kpvFx.bits) return;
    g_kpvFx.W = g_kpvFx.H = S;
  }
  g_fxSeed ^= GetTickCount() * 2654435761u;
  fx_seed(&g_kpvFx, kind, S);
  g_kpvAt.x = x;
  g_kpvAt.y = y;
  g_kpvFxOn = TRUE;
}

static void kpv_paint(HWND hwnd, HDC hdc) {
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
  kit_sig();
  kpv_img(0, 0); /* g_kpvN под нынешний размер */
  int cell, count, rowH, top;
  kpv_cells(&rc, &cell, &count, &rowH, &top);
  /* подложка: подпись на цвете карточки, ниже светлая и тёмная полосы */
  DWORD *px = (DWORD *)bits;
  static const COLORREF rowBg[2] = {RGB(246, 247, 249), RGB(30, 32, 37)};
  for (int y = 0; y < H; y++) {
    COLORREF c = y < top || y >= top + 2 * rowH ? COL_PAPER : rowBg[y < top + rowH ? 0 : 1];
    DWORD v = (DWORD)GetBValue(c) | ((DWORD)GetGValue(c) << 8) | ((DWORD)GetRValue(c) << 16);
    for (int x = 0; x < W; x++) px[(size_t)y * W + x] = v;
  }
  int frame = g_kitAnim ? (g_kpvTick / 5) % KIT_FRAMES : 0;
  int x0 = (W - count * cell) / 2;
  for (int row = 0; row < 2; row++)
    for (int i = 0; i < count; i++) {
      int s = kKitPvSlot[i];
      int cx = x0 + i * cell + (cell - g_kpvN) / 2, cy = top + row * rowH + (rowH - g_kpvN) / 2;
      kpv_blend((BYTE *)bits, W, H, kpv_img(s, frame), g_kpvN, cx, cy);
    }
  if (g_kpvFxOn && g_kpvFx.bits) { /* эффект поверх (буфер с домноженной альфой) */
    fx_paint(&g_kpvFx);
    int S = g_kpvFx.W, ox = g_kpvAt.x - S / 2, oy = g_kpvAt.y - S / 2;
    for (int y = 0; y < S; y++) {
      int yy = oy + y;
      if (yy < top || yy >= H) continue;
      const BYTE *sr = g_kpvFx.bits + (size_t)y * S * 4;
      BYTE *d = (BYTE *)bits + (size_t)yy * W * 4;
      for (int x = 0; x < S; x++) {
        int xx = ox + x;
        if (xx < 0 || xx >= W) continue;
        const BYTE *sp = sr + x * 4;
        if (!sp[3]) continue;
        BYTE *dp = d + xx * 4;
        int inv = 255 - sp[3];
        dp[0] = (BYTE)(sp[0] + dp[0] * inv / 255);
        dp[1] = (BYTE)(sp[1] + dp[1] * inv / 255);
        dp[2] = (BYTE)(sp[2] + dp[2] * inv / 255);
      }
    }
  }
  SetBkMode(mem, TRANSPARENT);
  if (g_fontSmall) SelectObject(mem, g_fontSmall);
  SetTextColor(mem, COL_MUTED);
  wchar_t cap[160];
  _snwprintf(cap, 160, L"%s%s · размер %d%s%s%s", g_skin == SKIN_KIT ? L"Стоит сейчас: " : L"Просмотр (не выбран): ",
             kKitNames[g_kitStyle], g_kpvN, g_kitOutline ? L" · обводка" : L"", g_kitShadow ? L" · тень" : L"",
             g_kitAnim ? L" · анимация" : L"");
  RECT tr = {2, 2, W - 2, top - 4};
  DrawTextW(mem, cap, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
  /* рамка полос */
  HPEN pen = CreatePen(PS_SOLID, 1, COL_LINE);
  HGDIOBJ op = SelectObject(mem, pen);
  HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
  Rectangle(mem, 0, top, W, top + 2 * rowH);
  SelectObject(mem, ob);
  SelectObject(mem, op);
  DeleteObject(pen);
  BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
}

static LRESULT CALLBACK KitPreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_TIMER: {
    if (!IsWindowVisible(hwnd) || !g_setHwnd || !IsWindowVisible(g_setHwnd)) {
      KillTimer(hwnd, 1);
      return 0;
    }
    g_kpvTick++;
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (g_kpvTick % 150 == 40) { /* стрелка на светлой полосе «нажимает» */
      int cell, count, rowH, top;
      kpv_cells(&rc, &cell, &count, &rowH, &top);
      float hx, hy;
      kit_hotspot(0, &hx, &hy);
      int x0 = (rc.right - count * cell) / 2;
      kpv_click(x0 + (cell - g_kpvN) / 2 + (int)(hx * g_kpvN / 32), top + (rowH - g_kpvN) / 2 + (int)(hy * g_kpvN / 32));
    } else if (g_kpvFxOn) {
      g_kpvFx.frame++;
      if (g_kpvFx.frame >= g_kpvFx.frames) g_kpvFxOn = FALSE;
      else fx_step(&g_kpvFx);
    }
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }
  case WM_LBUTTONDOWN:
    kpv_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    kpv_paint(hwnd, hdc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DESTROY:
    KillTimer(hwnd, 1);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND kit_preview_create(HWND parent, int id) {
  static BOOL reg = FALSE;
  if (!reg) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = KitPreviewProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CursorPadKitPreview";
    RegisterClassExW(&wc);
    reg = TRUE;
  }
  g_kpvHwnd = CreateWindowExW(0, L"CursorPadKitPreview", L"", WS_CHILD | WS_VISIBLE, 0, 0, 300, 140, parent,
                              (HMENU)(INT_PTR)id, g_inst, NULL);
  return g_kpvHwnd;
}

static void kit_preview_wake(void) {
  if (g_kpvHwnd && IsWindowVisible(g_kpvHwnd) && g_setHwnd && IsWindowVisible(g_setHwnd))
    SetTimer(g_kpvHwnd, 1, 16, NULL);
}

/* высота окошка просмотра под нынешний размер курсора */
static int kit_preview_h(void) { return 24 + 2 * (kit_size() + 16) + 4; }

/* эффект нажатия набора, если в «Эффекте» — «Как у курсора»: «Волна» */
static int kit_fx_kind(void) { return g_kitFx ? 17 : 0; }
