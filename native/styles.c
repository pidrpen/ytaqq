/* ---- Стили курсоров, нарисованные кодом (с 2026.10.14.2) ------------------

   Персонажи — наборы курсоров
   без картинок: все шестнадцать видов рисуются при первом выборе под размер
   указателя (32 / 48 / 64 точки) и ставятся так же, как наборы из .cur.
   С 2026.10.14.5 это персонажи с маленьким острием указателя, а не
   перекрашенная стрелка (до того были «Неон», «Лёд», «Золото», «Лава», «Космос»).
   С 2026.10.14.11 к ним добавлены «Сукуна» и «Годжо» из «Магической битвы»,
   с 2026.10.14.12 — свои, без общего острия, и со своими эффектами.
   С 2026.10.14.13 Робота, Призрака, Слайма, Огонька и Черепа сменили «Ворон»,
   «Осьминог» и «Скорпион» — тоже свои, со своими эффектами.

   У каждого свой эффект при нажатии, если в Настройках выбрано «Как у
   курсора» (fx_kind_for). Здесь же — окошко просмотра в Настройках: в нём
   курсор водит сам и щёлкает, показывая стиль и эффект до выбора. */

/* ST_COUNT, ST_SKIN0 (g_skin первого стиля) и ST_KNIGHT — в cursorpad.c. Рыцарь
   (Мечник, g_skin 1) с 2026.10.14.6 тоже рисуется здесь, но своей кнопки
   стиля у него нет — он на месте прежнего Мечника */
#define ST_DRAW (ST_COUNT + 1)
#define ST_SLOTS RIAS_SLOTS /* виды — в том же порядке, что у «Риас» (kRias) */


static HCURSOR g_stCur[ST_DRAW][ST_SLOTS];
static int g_stSize[ST_DRAW]; /* под какой размер указателя нарисованы */

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
  static const BYTE hs[ST_SLOTS][2] = {{2, 1},   {16, 16}, {16, 1},  {2, 1},  {16, 16}, {16, 16}, {16, 16}, {16, 16},
                                        {16, 16}, {16, 16}, {16, 16}, {16, 2}, {2, 1},   {2, 1},   {16, 16}, {2, 1}};
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

/* прочие виды — прежние формы в цветах персонажа */
static const StLook kLooks[ST_DRAW] = {
    {{120, 125, 175}, {65, 68, 105}, {35, 37, 60}, {15, 15, 25}, {150, 130, 230}, 1.2f, 0.40f, 0},   /* Ворон */
    {{255, 160, 215}, {230, 95, 175}, {150, 40, 120}, {40, 10, 35}, {255, 120, 200}, 1.2f, 0.30f, 0},  /* Осьминог */
    {{255, 210, 110}, {240, 150, 40}, {170, 70, 15}, {45, 20, 5}, {120, 230, 70}, 1.2f, 0.35f, 0},     /* Скорпион */
    {{255, 90, 90}, {200, 20, 30}, {110, 0, 10}, {20, 0, 5}, {255, 30, 40}, 1.2f, 0.40f, 0},     /* Разрез */
    {{255, 175, 200}, {235, 110, 150}, {150, 30, 60}, {30, 10, 15}, {255, 40, 60}, 1.2f, 0.35f, 0},   /* Сукуна */
    {{245, 248, 255}, {170, 210, 255}, {60, 120, 230}, {15, 20, 45}, {90, 190, 255}, 1.2f, 0.45f, 0}, /* Годжо */
    {{235, 238, 245}, {185, 190, 202}, {130, 135, 148}, {30, 30, 40}, {0, 0, 0}, 1.2f, 0.0f, 2}, /* Рыцарь */
};

static float st_mix(float a, float b, float t) { return a + (b - a) * t; }

static unsigned st_hash(int x, int y) {
  unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

/* ---- персонажи (с 2026.10.14.5) ----
   Стрелка, рука, справка, место, человек, ожидание и фоновая работа — это
   персонаж с маленьким острием указателя; остальные виды (текст, размеры,
   крест…) — прежние формы в цветах персонажа. Персонаж задан в единицах
   своего размера: u вправо, v вниз, середина — 0,0 */
static BOOL st_char_slot(int slot) {
  return slot == 0 || slot == 2 || slot == 3 || slot == 12 || slot == 13 || slot == 14 || slot == 15;
}

static BOOL st_ell(float u, float v, float cu, float cv, float ru, float rv) {
  float a = (u - cu) / ru, b = (v - cv) / rv;
  return a * a + b * b <= 1.0f;
}

#define ST_SET(R, G, B) (c[0] = (R), c[1] = (G), c[2] = (B), 1)
/* цвет персонажа st в точке (u, v); 0 — мимо */
static int st_char_px(int st, float u, float v, float *c) {
  float au = fabsf(u);
  switch (st) {
  case ST_KNIGHT: /* Рыцарь: топфхельм с Т-образной прорезью, золотой обод и алый плюмаж */
    if (st_ell(u, v, 0.22f, -0.9f, 0.5f, 0.17f)) return ST_SET(st_mix(235, 150, (v + 1.07f) / 0.34f), 30, 40);
    if ((v >= -0.14f && v <= 0.0f && au <= 0.56f) || (au <= 0.07f && v >= 0.0f && v <= 0.5f)) return ST_SET(25, 25, 35);
    if (v >= 0.5f && v <= 0.68f && (st_ell(au, v, 0.28f, 0.58f, 0.05f, 0.05f) || st_ell(au, v, 0.44f, 0.58f, 0.05f, 0.05f)))
      return ST_SET(25, 25, 35);
    if (st_ell(au, v, 0.6f, 0.72f, 0.06f, 0.06f)) return ST_SET(90, 90, 100);
    if (v >= -0.34f && v <= -0.24f && au <= 0.72f) return ST_SET(215, 172, 50);
    if ((v < -0.1f && st_ell(u, v, 0, -0.1f, 0.72f, 0.72f)) || (au <= 0.72f && v >= -0.1f && v <= 0.86f)) {
      float t = (v + 0.82f) / 1.7f, hl = au < 0.2f && v < -0.36f ? 0.35f : 0.0f; /* блик по гребню */
      return ST_SET(st_mix(st_mix(228, 125, t), 255, hl), st_mix(st_mix(232, 130, t), 255, hl),
                    st_mix(st_mix(240, 145, t), 255, hl));
    }
    return 0;
  }
  return 0;
}
#undef ST_SET

/* клинок вдоль оси от острия (tx, ty) в направлении (dx, dy): s — вдоль, w — поперёк.
   len — длина лезвия; за ним гарда, рукоять и навершие. 0 — мимо */
static int st_blade_px(int st, float x, float y, float tx, float ty, float dx, float dy, float len, float *c) {
  float s = (x - tx) * dx + (y - ty) * dy, w = (x - tx) * dy - (y - ty) * dx, aw = fabsf(w);
  float sc = len / 22.0f; /* гарда и рукоять — в долях длины разрезного клинка */
  BOOL red = st != ST_KNIGHT;
  if (s < 0) return 0;
  if (s <= len) {
    float hw = fminf(s * 0.35f, red ? 2.3f : 1.8f);
    if (aw > hw) return 0;
    float m = aw / hw;
    if (red) {
      if (m < 0.3f) return c[0] = 255, c[1] = 120, c[2] = 110, 1;
      return c[0] = st_mix(215, 105, m), c[1] = st_mix(25, 0, m), c[2] = st_mix(35, 10, m), 1;
    }
    if (m < 0.3f) return c[0] = 255, c[1] = 255, c[2] = 255, 1;
    return c[0] = st_mix(225, 140, m), c[1] = st_mix(230, 145, m), c[2] = st_mix(240, 160, m), 1;
  }
  s -= len;
  if (s <= 2.0f * sc) return aw <= 4.2f * sc ? (red ? (c[0] = 30, c[1] = 22, c[2] = 24, 1) : (c[0] = 215, c[1] = 172, c[2] = 50, 1)) : 0;
  s -= 2.0f * sc;
  if (s <= 9.0f * sc) {
    if (aw > 1.7f * sc) return 0;
    if (red) return fmodf(s, 3.0f * sc) < 1.3f * sc && aw < 1.0f * sc ? (c[0] = 200, c[1] = 30, c[2] = 40, 1) : (c[0] = 25, c[1] = 20, c[2] = 20, 1);
    return c[0] = 120, c[1] = 75, c[2] = 40, 1;
  }
  s -= 9.0f * sc;
  if (s <= 1.6f * sc && aw <= 2.1f * sc) return c[0] = 215, c[1] = 172, c[2] = 50, 1;
  return 0;
}

static const BYTE kCharTip[ST_DRAW][3] = {{150, 130, 230}, {255, 120, 200}, {120, 230, 70}, {255, 60, 60},
                                          {230, 40, 50},   {90, 190, 255},  {225, 230, 240}};
static const float kTip[] = {2, 1, 2, 15, 5.8f, 11.6f, 11, 11.2f};   /* острие стрелки */
static const float kTipUp[] = {16, 1, 21, 8, 11, 8};                 /* острие руки — вверх */

/* ---- «Сукуна» и «Годжо» (с 2026.10.14.12) ----
   Свои, ни на кого не похожие: как у Дракона и Кота, указывает сам персонаж —
   острый вихор волос тянется в левый верхний угол, отдельного острия нет.
   Рука — своя у каждого: у Сукуны палец с длинным чёрным ногтем и чёрные
   браслеты-метки на запястье, у Годжо два пальца вверх с голубым светом
   («Синий»). В ожидании Сукуна открывает все четыре глаза, а Годжо снимает
   повязку — под ней голубые «Шесть глаз». Рисуется прямо в сетке 32. */
#define ST_SUKUNA 4
#define ST_GOJO 5

#define ST_C(R, G, B) (c[0] = (R), c[1] = (G), c[2] = (B), 1)
/* отрезок толщиной w от (x0, y0) до (x1, y1) */
static BOOL st_seg(float x, float y, float x0, float y0, float x1, float y1, float w) {
  float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
  float t = l2 > 0 ? ((x - x0) * vx + (y - y0) * vy) / l2 : 0;
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return hypotf(x - x0 - vx * t, y - y0 - vy * t) <= w * 0.5f;
}

/* глаз с тёмным краем, наклонён к переносице (tilt) */
static int st_eye(float x, float y, float cx, float cy, float rx, float ry, float tilt, BYTE r, BYTE g, BYTE b, float *c) {
  float yy = y - (x - cx) * tilt;
  if (!st_ell(x, yy, cx, cy, rx + 0.55f, ry + 0.5f)) return 0;
  if (st_ell(x, yy, cx, cy, rx, ry)) {
    if (st_disc(x, yy, cx - rx * 0.3f, cy - ry * 0.3f, ry * 0.45f)) return ST_C(255, 255, 255); /* блик */
    return ST_C(r, g, b);
  }
  return ST_C(30, 10, 15);
}

/* по пояс, с 2026.10.14.16: голова справа, рука вытянута влево-вверх, и
   указательный палец упирается в рабочую точку курсора (2,1). Размер — как у
   прочих (в 2026.10.14.16 был в 1,25 раза крупнее). wait — «ожидание»: Годжо без повязки */
static int st_jjk_fig(int st, float x, float y, BOOL wait, float *c) {
  float dx = x - 21;
  BOOL suk = st == ST_SUKUNA;
  /* рука: указательный палец, кулак, предплечье, рукав от плеча */
  if (st_seg(x, y, 6.2f, 7.2f, 2.2f, 1.3f, 1.8f)) {
    if (suk && hypotf(x - 2.2f, y - 1.3f) < 1.5f) return ST_C(25, 15, 20); /* чёрный ноготь */
    if (!suk && hypotf(x - 2.2f, y - 1.3f) < 2.2f) return ST_C(150, 215, 255); /* голубой свет на кончике */
    return ST_C(252, 222, 198);
  }
  if (st_disc(x, y, 7.2f, 9, 2.5f) || st_ell(x, y, 9.4f, 8.4f, 1.1f, 1.6f)) return ST_C(245, 212, 188);
  {
    float vx = 11.6f - 7.6f, vy = 16.6f - 10, l2 = vx * vx + vy * vy;
    float t = ((x - 7.6f) * vx + (y - 10) * vy) / l2;
    if (st_seg(x, y, 7.6f, 10, 11.6f, 16.6f, 3.6f)) {
      if (!suk) return t < 0.18f ? ST_C(110, 120, 165) : ST_C(32, 36, 66); /* рукав формы */
      if (fabsf(t - 0.22f) < 0.06f || fabsf(t - 0.4f) < 0.06f) return ST_C(25, 12, 18); /* метки-браслеты */
      return t < 0.6f ? ST_C(245, 212, 188) : ST_C(45, 35, 45);
    }
  }
  if (st_seg(x, y, 11.2f, 16, 15.5f, 23.5f, 5.2f)) return suk ? ST_C(48, 38, 48) : ST_C(28, 32, 60);
  if (suk) {
    if (st_eye(x, y, 18.8f, 11, 1.45f, 0.8f, 0.28f, 235, 25, 35, c)) return 1;
    if (st_eye(x, y, 23.4f, 11, 1.45f, 0.8f, -0.28f, 235, 25, 35, c)) return 1;
    if (fabsf(y - 12.9f - (x < 21 ? x - 18.8f : 23.4f - x) * 0.28f) <= 0.33f && fabsf(fabsf(dx) - 2.3f) <= 1.1f)
      return ST_C(30, 10, 15); /* сомкнутые нижние глаза */
    if (st_seg(x, y, 17, 9.4f, 19.9f, 10.1f, 0.75f) || st_seg(x, y, 25.2f, 9.4f, 22.3f, 10.1f, 0.75f)) return ST_C(40, 12, 18);
    if (fabsf(fabsf(dx) - 4.3f) <= 0.7f && ((y >= 13.7f && y <= 14.2f) || (y >= 14.8f && y <= 15.3f))) return ST_C(25, 12, 18);
    if (fabsf(dx) <= 0.35f && y >= 7.8f && y <= 9.4f) return ST_C(25, 12, 18); /* метка на лбу */
    if (dx >= -1.6f && dx <= 2 && fabsf(y - (15.9f - 0.22f * (dx + 1.6f) + 0.02f * dx * dx)) <= 0.3f) return ST_C(120, 30, 40); /* ухмылка */
    static const float kSp[4][6] = {{15.6f, 6, 16.6f, -0.4f, 19.8f, 3.6f}, {19, 3.6f, 22.6f, -0.8f, 24, 4},
                                    {23.4f, 4, 28.6f, 1.2f, 26.8f, 7.4f},  {26.2f, 6.8f, 31, 7.4f, 27, 10.4f}};
    BOOL hair = st_ell(x, y, 21, 7.6f, 6.3f, 5) && y < 8.7f + 0.9f * fabsf(sinf(x * 1.5f));
    for (int i = 0; i < 4 && !hair; i++) hair = st_poly(kSp[i], 3, x, y);
    if (hair) {
      float t = fx_clamp01((y + 1) / 11);
      if (fabsf(sinf((x * 0.8f + y * 0.35f) * 1.1f)) < 0.07f) return ST_C(205, 85, 125);
      return ST_C(st_mix(255, 228, t), st_mix(185, 110, t), st_mix(210, 150, t));
    }
    if (st_ell(x, y, 21, 12, 5.3f, 6)) return ST_C(250, 218, 192);
    if (fabsf(dx) <= 1.8f && y >= 16.8f && y <= 19.8f) return ST_C(215, 170, 145);
    if (y >= 19 && st_ell(x, y, 21, 31, 10.5f, 12)) {
      float k = (y - 18.6f) * 0.28f;
      if (fabsf(dx) <= k) return ST_C(215, 170, 145);
      if (fabsf(dx) <= k + 1.3f) return ST_C(240, 235, 228);
      return ST_C(st_mix(55, 32, (y - 19) / 13), st_mix(45, 26, (y - 19) / 13), st_mix(55, 32, (y - 19) / 13));
    }
    return 0;
  }
  /* Годжо */
  if (!wait && fabsf(dx) <= 6.1f && y >= 9.4f && y <= 12.3f) return y < 9.9f ? ST_C(72, 72, 90) : ST_C(22, 22, 30);
  if (wait) {
    if (st_eye(x, y, 18.9f, 11, 1.45f, 0.95f, 0.1f, 70, 185, 255, c)) return 1;
    if (st_eye(x, y, 23.1f, 11, 1.45f, 0.95f, -0.1f, 70, 185, 255, c)) return 1;
  }
  if (dx >= -1.4f && dx <= 1.8f && fabsf(y - (15.8f - 0.15f * dx)) <= 0.3f) return ST_C(165, 85, 85); /* усмешка */
  static const float kSp[4][6] = {{15.4f, 5.4f, 15.6f, -1, 19.4f, 3.4f}, {18.6f, 3.4f, 21.6f, -1.6f, 23.2f, 3.4f},
                                  {22.6f, 3.4f, 27.4f, -0.6f, 25.8f, 6.2f}, {25.6f, 5.6f, 31, 4.4f, 27, 9}};
  BOOL hair = st_ell(x, y, 21, 7.4f, 6.4f, 5.4f) && y < (wait ? 9.2f + 0.8f * fabsf(sinf(x * 1.5f)) : 9.4f);
  for (int i = 0; i < 4 && !hair; i++) hair = st_poly(kSp[i], 3, x, y);
  if (hair) {
    float t = fx_clamp01((y + 1) / 11);
    return ST_C(st_mix(255, 200, t), st_mix(255, 212, t), st_mix(255, 238, t));
  }
  if (st_ell(x, y, 21, 12, 5.2f, 6)) return ST_C(252, 224, 204);
  if (fabsf(dx) <= 3.3f && y >= 16.6f && y <= 20.6f) return fabsf(dx) <= 0.3f ? ST_C(110, 120, 165) : ST_C(28, 32, 60); /* высокий ворот */
  if (y >= 19.5f && st_ell(x, y, 21, 31, 10.5f, 12))
    return fabsf(dx) <= 0.3f ? ST_C(110, 120, 165) : ST_C(st_mix(45, 22, fabsf(dx) / 10.5f), st_mix(52, 26, fabsf(dx) / 10.5f), st_mix(90, 50, fabsf(dx) / 10.5f));
  return 0;
}

/* рука над ссылками, указывает вверх (острие — 16,1) */
static int st_jjk_hand(int st, float x, float y, float *c) {
  if (st == ST_SUKUNA) {
    static const float kNail[] = {14.2f, 5.2f, 16, 0.4f, 17.8f, 5.2f};
    if (st_poly(kNail, 3, x, y)) return ST_C(25, 15, 20); /* длинный чёрный коготь */
    BOOL skin = (st_rect(x, y, 14.2f, 4.5f, 17.8f, 15) || st_ell(x, y, 21, 19.5f, 6.8f, 6) ||
                 st_ell(x, y, 13.6f, 20.5f, 2.4f, 3.6f) || st_rect(x, y, 15, 24, 25, 32));
    if (!skin) return 0;
    if (y >= 27 && ((y <= 28.3f) || (y >= 29.8f && y <= 31.1f))) return ST_C(25, 12, 18); /* браслеты-метки */
    if (x > 18.2f && x < 27 && y > 14 && y < 23 && fabsf(fmodf(y - 13.6f, 3.2f)) < 0.35f) return ST_C(205, 155, 130);
    return ST_C(st_mix(252, 225, (y - 1) / 31), st_mix(220, 180, (y - 1) / 31), st_mix(196, 155, (y - 1) / 31));
  }
  if (st_rect(x, y, 13, 3, 16, 15) || st_rect(x, y, 16.4f, 3.6f, 19.4f, 15) || st_disc(x, y, 14.5f, 3, 1.5f) ||
      st_disc(x, y, 17.9f, 3.6f, 1.5f)) { /* указательный и средний вместе — жест Годжо */
    float g = fx_clamp01(1.0f - (y - 1.5f) / 5.5f); /* кончики светятся голубым */
    return ST_C(st_mix(252, 130, g), st_mix(224, 205, g), st_mix(204, 255, g));
  }
  if (st_ell(x, y, 21, 19.5f, 6.6f, 5.8f) || st_ell(x, y, 12.4f, 20.5f, 2.3f, 3.6f))
    return (x > 19.6f && x < 27 && y > 14 && y < 23 && fabsf(fmodf(y - 13.6f, 3.2f)) < 0.35f) ? ST_C(205, 160, 140)
                                                                                                : ST_C(252, 224, 204);
  if (st_rect(x, y, 14, 24, 25, 32)) return y < 25.5f ? ST_C(110, 120, 165) : ST_C(28, 32, 60); /* рукав формы */
  return 0;
}

static int st_jjk_slot_px(int st, int slot, float x, float y, float *c) {
  if (slot == 2) return st_jjk_hand(st, x, y, c);
  return st_jjk_fig(st, x, y, slot == 14, c);
}

/* ---- «Ворон», «Осьминог», «Скорпион» (с 2026.10.14.13) ----
   Пришли на место Робота, Призрака, Слайма, Огонька и Черепа — те были
   головой в углу при общем цветном острие. Здесь, как у Дракона и Кота,
   указывает сам зверь: у Ворона — клюв, у Осьминога — кончик щупальца, у
   Скорпиона — жало. Над ссылками — своё: перо, щупальце, хвост с жалом. */
#define ST_CROW 0
#define ST_OCTO 1
#define ST_SCORP 2

/* ломаная из n точек с толщиной от w0 (начало) до w1 (конец): попала ли точка;
   *along — сколько пройдено вдоль от начала, *rel — расстояние от оси / полутолщина */
static BOOL st_chain(float x, float y, const float *p, int n, float w0, float w1, float *along, float *rel) {
  float total = 0, best = 1e9f, run = 0;
  for (int i = 0; i + 1 < n; i++) total += hypotf(p[i * 2 + 2] - p[i * 2], p[i * 2 + 3] - p[i * 2 + 1]);
  BOOL hit = FALSE;
  for (int i = 0; i + 1 < n; i++) {
    float x0 = p[i * 2], y0 = p[i * 2 + 1], vx = p[i * 2 + 2] - x0, vy = p[i * 2 + 3] - y0;
    float l = hypotf(vx, vy), t = l > 0 ? ((x - x0) * vx + (y - y0) * vy) / (l * l) : 0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float d = hypotf(x - x0 - vx * t, y - y0 - vy * t), s = run + l * t;
    float hw = 0.5f * st_mix(w0, w1, total > 0 ? s / total : 0);
    if (d <= hw && d / hw < best) best = d / hw, *along = s, *rel = best, hit = TRUE;
    run += l;
  }
  return hit;
}

/* повёрнутый на 45° эллипс: ось u — от левого верхнего угла к правому нижнему */
static BOOL st_ell45(float x, float y, float cx, float cy, float ru, float rv) {
  float u = ((x - cx) + (y - cy)) * 0.70710678f, v = ((y - cy) - (x - cx)) * 0.70710678f;
  return st_ell(u, v, 0, 0, ru, rv);
}

static int st_crow(float x, float y, float *c) {
  static const float kBeak[] = {2, 1, 10.2f, 5, 5.2f, 10};
  static const float kTail[] = {24, 22.5f, 31.5f, 26.5f, 30.5f, 30, 26.8f, 31.5f};
  if (st_poly(kBeak, 3, x, y)) return (x - 2) - (y - 1) > 0 ? ST_C(245, 200, 80) : ST_C(195, 140, 45);
  if (st_disc(x, y, 10.6f, 8.8f, 1.5f)) /* жёлтый глаз с чёрным зрачком */
    return st_disc(x, y, 10.9f, 9, 0.65f) ? ST_C(10, 10, 15) : ST_C(255, 210, 40);
  if (st_seg(x, y, 18, 25.5f, 17, 30, 0.9f) || st_seg(x, y, 21.5f, 25.5f, 21.5f, 30, 0.9f) ||
      st_seg(x, y, 15.6f, 30.4f, 18.6f, 30.4f, 0.8f) || st_seg(x, y, 20, 30.4f, 23, 30.4f, 0.8f))
    return ST_C(130, 120, 100); /* лапы */
  BOOL wing = st_ell45(x, y, 20.5f, 17.5f, 8, 4.2f);
  if (wing || st_poly(kTail, 4, x, y)) { /* крыло и хвост: перья со светлой кромкой */
    float u = ((x - 20) + (y - 18)) * 0.70710678f;
    if (fabsf(fmodf(u + 40, 2.6f)) < 0.45f) return ST_C(125, 120, 185);
    return ST_C(42, 44, 72);
  }
  if (st_disc(x, y, 11.3f, 11.3f, 5.3f) || st_ell45(x, y, 18.5f, 18.5f, 9.5f, 6.2f)) {
    if (st_disc(x, y, 9.6f, 9.4f, 2.6f) && !st_disc(x, y, 10.6f, 8.8f, 2.2f)) return ST_C(105, 110, 165); /* отлив */
    float t = fx_clamp01((x + y - 12) / 36);
    return ST_C(st_mix(70, 34, t), st_mix(74, 36, t), st_mix(110, 58, t));
  }
  return 0;
}

static int st_octo(float x, float y, float *c) {
  static const float kArm[] = {16, 21, 10, 18.8f, 6, 13.5f, 3.6f, 7.5f, 2.4f, 3.2f, 2, 1};
  float s, r;
  if (st_ell(x, y, 18.6f, 15.8f, 1.8f, 2.1f) || st_ell(x, y, 24.6f, 15.8f, 1.8f, 2.1f)) { /* глаза */
    float ex = x < 21.6f ? 18.6f : 24.6f;
    if (st_ell(x, y, ex + 0.2f, 16.3f, 1.2f, 0.6f)) return ST_C(15, 10, 20);
    return ST_C(255, 252, 240);
  }
  if (st_ell(x, y, 21.6f, 13, 7.2f, 8.2f) && y < 20.5f) {
    if (st_ell(x, y, 19, 8.5f, 2.4f, 1.8f)) return ST_C(255, 175, 220); /* блик */
    float t = fx_clamp01((y - 5) / 16);
    if (st_disc(x, y, 26, 11, 1) || st_disc(x, y, 17.5f, 12.5f, 0.8f) || st_disc(x, y, 24, 7.5f, 0.7f)) return ST_C(180, 45, 130);
    return ST_C(st_mix(245, 195, t), st_mix(120, 55, t), st_mix(190, 150, t));
  }
  BOOL arm = st_chain(x, y, kArm, 6, 4.2f, 1.0f, &s, &r);
  for (int i = 0; i < 4 && !arm; i++) {
    float bx = 16.5f + i * 3.4f;
    float leg[] = {bx, 19.5f, bx - 1.4f + (i & 1) * 2.4f, 24, bx + 0.8f - (i & 1) * 2.2f, 27.6f, bx - 0.6f + (i & 1) * 1.6f, 31};
    arm = st_chain(x, y, leg, 4, 3.2f, 1.0f, &s, &r);
  }
  if (arm) {
    if (r < 0.45f && fmodf(s, 2.4f) < 0.95f) return ST_C(255, 200, 225); /* присоски */
    return ST_C(st_mix(225, 175, r), st_mix(85, 45, r), st_mix(165, 130, r));
  }
  return 0;
}

static int st_scorp(float x, float y, float *c) {
  static const float kSting[] = {2, 1, 9.2f, 2.6f, 8, 6};
  static const float kSeg[6][3] = {{13.6f, 4.6f, 2}, {17.3f, 5.5f, 2.2f}, {20.6f, 7.6f, 2.4f},
                                   {22.9f, 11, 2.6f}, {24, 14.8f, 2.8f}, {23.6f, 18.6f, 3}};
  if (st_poly(kSting, 3, x, y)) return ST_C(115, 25, 25);
  if (st_ell(x, y, 10, 4.3f, 2.5f, 2.1f)) return st_disc(x, y, 9.4f, 3.6f, 0.8f) ? ST_C(255, 210, 120) : ST_C(215, 110, 30);
  for (int i = 5; i >= 0; i--) /* хвост из звеньев, у каждого тёмный край */
    if (st_disc(x, y, kSeg[i][0], kSeg[i][1], kSeg[i][2]))
      return hypotf(x - kSeg[i][0], y - kSeg[i][1]) > kSeg[i][2] - 0.6f ? ST_C(140, 60, 15) : ST_C(245, 165, 50);
  if (st_seg(x, y, 16, 26, 14.4f, 30.2f, 0.9f) || st_seg(x, y, 19.5f, 26.5f, 19, 30.6f, 0.9f) ||
      st_seg(x, y, 23, 26, 24.6f, 29.8f, 0.9f) || st_seg(x, y, 11.5f, 22.4f, 6.6f, 18.6f, 1.6f) ||
      st_seg(x, y, 11.5f, 25.8f, 6.4f, 28.4f, 1.6f))
    return ST_C(175, 85, 20); /* ноги и руки клешней */
  if ((st_ell(x, y, 5, 17.2f, 2.5f, 1.6f) || st_ell(x, y, 4.6f, 28.8f, 2.5f, 1.6f)) && fabsf(y - (y < 23 ? 16.9f : 29.1f)) > 0.3f)
    return ST_C(230, 130, 35); /* клешни с прорезью */
  if (st_ell(x, y, 12.8f, 24, 3.2f, 3)) {
    if (st_disc(x, y, 11.6f, 23, 0.55f) || st_disc(x, y, 13, 22.6f, 0.55f)) return ST_C(15, 10, 10);
    return ST_C(235, 150, 45);
  }
  if (st_ell(x, y, 19.5f, 22.8f, 6.8f, 4.4f)) {
    if (fabsf(fmodf(x, 2.6f)) < 0.45f) return ST_C(150, 65, 15);
    float t = fx_clamp01((y - 18.5f) / 8);
    return ST_C(st_mix(250, 205, t), st_mix(180, 100, t), st_mix(60, 25, t));
  }
  return 0;
}

/* над ссылками: перо, щупальце или хвост с жалом; острие — 16,1 */
static int st_beast_hand(int st, float x, float y, float *c) {
  float dx = x - 16, adx = fabsf(dx), s, r;
  if (st == ST_CROW) {
    if (adx <= 0.45f && y >= 1 && y <= 31) return ST_C(205, 195, 165); /* очин */
    if (y < 1 || y > 27) return 0;
    float hw = 4.4f * sinf(3.14159265f * (y - 0.6f) / 26.4f);
    if (adx > hw) return 0;
    if (adx > hw - 0.9f) return ST_C(130, 122, 190);
    if (fabsf(fmodf(y + adx * 0.8f, 2.6f)) < 0.45f) return ST_C(28, 28, 48);
    return ST_C(48, 50, 82);
  }
  if (st == ST_OCTO) {
    static const float kUp[] = {16, 31.5f, 18.6f, 24.5f, 13.8f, 17.5f, 17.6f, 9.5f, 16.2f, 3.8f, 16, 1};
    if (!st_chain(x, y, kUp, 6, 6, 1.1f, &s, &r)) return 0;
    if (r < 0.5f && fmodf(s, 2.6f) < 1.0f) return ST_C(255, 200, 225);
    return ST_C(st_mix(230, 175, r), st_mix(90, 45, r), st_mix(170, 130, r));
  }
  static const float kSting[] = {14.4f, 5.6f, 16, 0.6f, 17.6f, 5.6f};
  static const float kSeg[5][3] = {{16, 27.6f, 3.5f}, {16, 22.7f, 3.1f}, {16, 18.3f, 2.8f}, {16, 14.2f, 2.5f}, {16, 10.4f, 2.2f}};
  if (st_poly(kSting, 3, x, y)) return ST_C(115, 25, 25);
  if (st_ell(x, y, 16, 7, 2.3f, 2.4f)) return st_disc(x, y, 15.3f, 6.3f, 0.8f) ? ST_C(255, 210, 120) : ST_C(215, 110, 30);
  for (int i = 4; i >= 0; i--)
    if (st_disc(x, y, kSeg[i][0], kSeg[i][1], kSeg[i][2]))
      return hypotf(x - kSeg[i][0], y - kSeg[i][1]) > kSeg[i][2] - 0.6f ? ST_C(140, 60, 15) : ST_C(245, 165, 50);
  return 0;
}

static int st_beast_slot_px(int st, int slot, float x, float y, float *c) {
  if (slot == 2) return st_beast_hand(st, x, y, c);
  if (slot == 14) x += 2, y += 1;
  return st == ST_CROW ? st_crow(x, y, c) : st == ST_OCTO ? st_octo(x, y, c) : st_scorp(x, y, c);
}
#undef ST_C

/* цвет вида slot стиля st в точке сетки 32: значок поверх острия, острие поверх персонажа */
static int st_char_slot_px(int st, int slot, float x, float y, float *c) {
  float bx = 26, by = 6;
  if (slot == 3 || slot == 12 || slot == 13 || slot == 15) {
    float d2 = (x - bx) * (x - bx) + (y - by) * (y - by);
    if (slot == 15) {
      if (d2 <= 4.6f * 4.6f && d2 >= 2.6f * 2.6f) return c[0] = kCharTip[st][0], c[1] = kCharTip[st][1], c[2] = kCharTip[st][2], 1;
    } else if (d2 <= 4.5f * 4.5f) {
      if (slot == 3) return c[0] = 60, c[1] = 140, c[2] = 255, 1;
      if (slot == 12) return c[0] = 240, c[1] = 70, c[2] = 70, 1;
      return c[0] = 70, c[1] = 190, c[2] = 110, 1;
    }
  }
  if (st == ST_SUKUNA || st == ST_GOJO) return st_jjk_slot_px(st, slot, x, y, c);
  if (st <= ST_SCORP) return st_beast_slot_px(st, slot, x, y, c);
  if (st == ST_RAZREZ) { /* «Разрез»: сам курсор — алый клинок */
    if (slot == 2) return st_blade_px(st, x, y, 16, 1, 0, 1, 15, c);
    return st_blade_px(st, x, y, 2, 1, 0.70710678f, 0.70710678f, 19, c);
  }
  if (st == ST_KNIGHT && slot != 2 && slot != 14) { /* у рыцаря вместо острия — меч */
    if (st_blade_px(st, x, y, 2, 1, 0.70710678f, 0.70710678f, 11, c)) return 1;
    return st_char_px(st, (x - 21) / 9.5f, (y - 21) / 9.5f, c);
  }
  if (slot == 2) {
    if (st_poly(kTipUp, 3, x, y)) return c[0] = kCharTip[st][0], c[1] = kCharTip[st][1], c[2] = kCharTip[st][2], 1;
    return st_char_px(st, (x - 16) / 11.0f, (y - 20) / 11.0f, c);
  }
  if (slot == 14) return st_char_px(st, (x - 16) / 13.0f, (y - 17) / 13.0f, c);
  if (st_poly(kTip, 4, x, y)) return c[0] = kCharTip[st][0], c[1] = kCharTip[st][1], c[2] = kCharTip[st][2], 1;
  return st_char_px(st, (x - 20) / 10.5f, (y - 20) / 10.5f, c);
}

static void st_draw_char(int st, int slot, int N, BYTE *out) {
  float k = N / 32.0f;
  size_t n = (size_t)N * N;
  float *P = (float *)calloc(n * 5, sizeof(float)); /* r g b (домноженные), альфа, кайма */
  if (!P) {
    memset(out, 0, n * 4);
    return;
  }
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float acc[4] = {0, 0, 0, 0}, c[3];
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++)
          if (st_char_slot_px(st, slot, (x + (sx + 0.5f) / 4.0f) / k, (y + (sy + 0.5f) / 4.0f) / k, c)) {
            acc[0] += c[0], acc[1] += c[1], acc[2] += c[2], acc[3] += 1.0f;
          }
      float *p = P + ((size_t)y * N + x) * 5;
      p[0] = acc[0] / 16, p[1] = acc[1] / 16, p[2] = acc[2] / 16, p[3] = acc[3] / 16;
    }
  /* тёмный контур вокруг: видно и на белом, и на тёмном */
  float ro = 1.2f * k;
  int ri = (int)ceilf(ro);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float best = 0;
      for (int dy = -ri; dy <= ri; dy++)
        for (int dx = -ri; dx <= ri; dx++) {
          int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= N || yy >= N) continue;
          float w = fx_clamp01(ro + 0.5f - sqrtf((float)(dx * dx + dy * dy)));
          float v = P[((size_t)yy * N + xx) * 5 + 3] * w;
          if (v > best) best = v;
        }
      P[((size_t)y * N + x) * 5 + 4] = best;
    }
  for (size_t i = 0; i < n; i++) {
    float *p = P + i * 5;
    float a = p[3], inv = 1.0f - a, ol = p[4];
    float r = p[0] + 25 * ol * inv, g = p[1] + 22 * ol * inv, b = p[2] + 35 * ol * inv, aa = a + ol * inv;
    BYTE *o = out + i * 4;
    if (aa <= 0.004f) {
      o[0] = o[1] = o[2] = o[3] = 0;
      continue;
    }
    o[0] = (BYTE)fminf(255.0f, b / aa);
    o[1] = (BYTE)fminf(255.0f, g / aa);
    o[2] = (BYTE)fminf(255.0f, r / aa);
    o[3] = (BYTE)(aa * 255.0f + 0.5f);
  }
  free(P);
}

/* картинка вида slot стиля st в N×N, BGRA без домножения альфы */
static void st_draw(int st, int slot, int N, BYTE *out) {
  if (st_char_slot(slot)) {
    st_draw_char(st, slot, N, out);
    return;
  }
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
  if (st < 0 || st >= ST_DRAW) return;
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
  for (int s = 0; s < ST_DRAW; s++)
    for (int i = 0; i < ST_SLOTS; i++)
      if (g_stCur[s][i]) {
        DestroyCursor(g_stCur[s][i]);
        g_stCur[s][i] = NULL;
      }
}

/* ---- просмотр в Настройках ----------------------------------------------

   Окошко в разделе «Курсор и эффекты». Курсор сам ходит по нему и раз в
   секунду щёлкает — видно и стиль, и эффект при нажатии. Пока мышь над
   кнопкой курсора или эффекта, показано то, что она выберет, а не то,
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
  if (skin == 1) {
    st_ensure(ST_KNIGHT);
    return g_stCur[ST_KNIGHT][slotOf[which]];
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
/* только проверочная сборка: --gallery файл.bmp — все виды всех стилей и кадры
   всех эффектов на одном листе, чтобы посмотреть их без мыши */
static void st_write_bmp(const wchar_t *path, const BYTE *img, int W, int H);
static void st_gallery(const wchar_t *path) {
  fx_init_sprites();
  int S = (int)(200 * g_fxScale), N = 48, cell = 56;
  int W = S * 4 > cell * ST_SLOTS ? S * 4 : cell * ST_SLOTS;
  int rowsCur = ST_DRAW * 2, H = rowsCur * cell + 13 * (S / 2);
  BYTE *img = (BYTE *)malloc((size_t)W * H * 4);
  BYTE *cur = (BYTE *)malloc((size_t)N * N * 4);
  FxWin fw;
  memset(&fw, 0, sizeof(fw));
  fw.W = fw.H = S;
  fw.bits = (BYTE *)malloc((size_t)S * S * 4);
  if (!img || !cur || !fw.bits) return;
  for (int i = 0; i < W * H; i++) ((DWORD *)img)[i] = 0xFFFFFFFFu;
  /* курсоры: каждый стиль — на белом и на тёмном */
  for (int st = 0; st < ST_DRAW; st++)
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
  /* эффекты 1…13: по четыре кадра, половина облачка */
  for (int kind = 1; kind <= 13; kind++) {
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
  st_write_bmp(path, img, W, H);
  free(img);
  free(cur);
  free(fw.bits);
}

static void st_write_bmp(const wchar_t *path, const BYTE *img, int W, int H) {
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
}

/* только проверочная сборка: --slash файл.bmp — кадры «Разреза»: обычный на
   белом и на тёмном, большой разрез и раскол поверх выдуманного экрана 1280×720
   (уменьшены вдвое) */
static void st_fake_screen(BYTE *b, int W, int H) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      BYTE *d = b + ((size_t)y * W + x) * 4;
      BOOL win = (x > W / 8 && x < W / 2 && y > H / 6 && y < H * 3 / 4) || (x > W * 5 / 9 && x < W * 7 / 8 && y > H / 3 && y < H * 5 / 6);
      BOOL bar = win && ((y > H / 6 && y < H / 6 + 18) || (y > H / 3 && y < H / 3 + 18 && x > W * 5 / 9));
      d[0] = (BYTE)(bar ? 200 : win ? 245 : 120 + 100 * y / H);
      d[1] = (BYTE)(bar ? 120 : win ? 245 : 80 + 60 * y / H);
      d[2] = (BYTE)(bar ? 40 : win ? 245 : 40 + 30 * x / W);
      d[3] = 255;
    }
}
static void st_slash_strip(const wchar_t *path) {
  fx_init_sprites();
  int S = (int)(200 * g_fxScale), BW = 1280, BH = 720, W = 1920, H = S * 2 + BH;
  BYTE *img = (BYTE *)calloc((size_t)W * H, 4), *scr = (BYTE *)malloc((size_t)BW * BH * 4);
  FxWin fw;
  memset(&fw, 0, sizeof(fw));
  fw.bits = (BYTE *)malloc((size_t)BW * BH * 4);
  if (!img || !scr || !fw.bits) return;
  for (int i = 0; i < W * H; i++) ((DWORD *)img)[i] = 0xFFFFFFFFu;
  st_fake_screen(scr, BW, BH);
  /* обычный: 8 кадров, на белом и на тёмном */
  int at[8] = {0, 1, 2, 4, 7, 11, 16, 21};
  for (int bg = 0; bg < 2; bg++) {
    g_fxSeed = 4242u;
    fw.W = fw.H = S;
    fx_seed(&fw, 4, S);
    for (int k = 0; k < 8; k++) {
      fw.frame = at[k];
      fx_paint(&fw);
      BYTE back = bg ? 34 : 255;
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          const BYTE *s = fw.bits + ((size_t)y * S + x) * 4;
          BYTE *d = img + ((size_t)(bg * S + y) * W + k * (W / 8) + x) * 4;
          for (int c = 0; c < 3; c++) d[c] = (BYTE)(s[c] + back * (255 - s[3]) / 255);
        }
    }
  }
  /* большой разрез (кадры 3, 8, 20) и раскол (кадры 6, 10, 46): по 640×360 */
  int fr[2][3] = {{3, 8, 20}, {6, 10, 46}};
  for (int kind = 0; kind < 2; kind++)
    for (int k = 0; k < 3; k++) {
      g_fxSeed = 99u + kind;
      fw.W = BW, fw.H = BH;
      if (kind == 0) {
        fx_big_seed(&fw, BW, BH);
      } else {
        memcpy(fw.bits, scr, (size_t)BW * BH * 4);
        fx_shatter_setup(&fw);
        fw.shatter = TRUE, fw.slash = TRUE, fw.frames = FX_SHATTER_FRAMES;
      }
      fw.frame = fr[kind][k];
      fx_paint(&fw);
      fw.shatter = FALSE;
      for (int y = 0; y < BH / 2; y++)
        for (int x = 0; x < BW / 2; x++) {
          BYTE *d = img + ((size_t)(2 * S + kind * (BH / 2) + y) * W + k * (W / 3) + x) * 4;
          for (int c = 0; c < 3; c++) {
            int sum = 0;
            for (int q = 0; q < 4; q++) {
              size_t i = ((size_t)(2 * y + q / 2) * BW + 2 * x + q % 2) * 4;
              const BYTE *sb = fw.bits + i, *bb = scr + i;
              sum += kind ? sb[c] : sb[c] + bb[c] * (255 - sb[3]) / 255;
            }
            d[c] = (BYTE)(sum / 4);
          }
        }
    }
  fx_shatter_free();
  st_write_bmp(path, img, W, H);
  free(img);
  free(scr);
  free(fw.bits);
}

/* только проверочная сборка: --slashbench файл.txt — сколько миллисекунд
   рисуется кадр большого разреза и раскола на экране 1920×1080 */
static void st_slash_bench(const wchar_t *path) {
  fx_init_sprites();
  int BW = 1920, BH = 1080;
  FxWin fw;
  memset(&fw, 0, sizeof(fw));
  fw.bits = (BYTE *)malloc((size_t)BW * BH * 4);
  if (!fw.bits) return;
  LARGE_INTEGER fq, t0, t1;
  QueryPerformanceFrequency(&fq);
  char out[512];
  int len = 0;
  for (int kind = 0; kind < 2; kind++) {
    g_fxSeed = 99u + kind;
    fw.W = BW, fw.H = BH;
    if (kind == 0) {
      fx_big_seed(&fw, BW, BH);
    } else {
      st_fake_screen(fw.bits, BW, BH);
      fx_shatter_setup(&fw);
      fw.shatter = TRUE, fw.slash = TRUE, fw.frames = FX_SHATTER_FRAMES;
    }
    double worst = 0, sum = 0;
    for (fw.frame = 0; fw.frame < fw.frames; fw.frame++) {
      QueryPerformanceCounter(&t0);
      fx_paint(&fw);
      QueryPerformanceCounter(&t1);
      double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart;
      sum += ms;
      if (ms > worst) worst = ms;
    }
    fw.shatter = FALSE;
    len += snprintf(out + len, sizeof(out) - len, "%s: avg %.1f ms, worst %.1f ms, frames %d\n",
                    kind ? "shatter" : "big", sum / fw.frames, worst, fw.frames);
  }
  fx_shatter_free();
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD w;
    WriteFile(h, out, (DWORD)len, &w, NULL);
    CloseHandle(h);
  }
  free(fw.bits);
}
#endif
