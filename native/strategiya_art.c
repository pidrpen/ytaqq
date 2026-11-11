/* ---- Стратегия: рисунок -----------------------------------------------------

   Всё рисуется программой, без картинок, и сглаживается пробами на пиксель,
   как фигуры шахмат (shahmaty_art.c). Карта — расписные плитки-шестиугольники
   в ореховой раме: поле, лес (кроны со светом слева сверху), холмы, горы со
   снегом, вода с бликами, пашня у деревень, мощёный двор замка. Деревни и
   замки — отдельными спрайтами (крыши и флаги — цвета хозяина). Отряды —
   фишки цвета стороны с костяным значком: меч, копьё со щитом, лук, голова
   коня, катапульта. Ещё — подсветка клеток (заливка и обводка шестиугольника).

   Геометрия: шестиугольник вершиной вверх, R — от центра до вершины, ширина
   √3·R, ряды через 1.5·R, нечётные ряды сдвинуты на полширины. Местные
   координаты клетки — в долях R от её центра, Y вниз. */

#define SG_SS 4     /* проб на пиксель по каждой оси у спрайтов */
#define SG_MAP_SS 3 /* у карты */

typedef struct {
  float R, w;   /* радиус и ширина шестиугольника, пикселей */
  float ox, oy; /* левый верхний угол поля клеток */
  int flip;     /* карта повёрнута на пол-оборота: свой замок всегда слева */
} SgGeo;

static void sg_geo_center(const SgGeo *g, int cell, float *x, float *y) {
  if (g->flip) cell = sg_mirror(cell);
  int c = sg_col(cell), r = sg_row(cell);
  *x = g->ox + g->w * (c + 0.5f * (r & 1) + 0.5f);
  *y = g->oy + g->R * (1 + 1.5f * r);
}

/* до края шестиугольника (в долях R; минус — снаружи) */
static float sg_hex_edge(float dx, float dy) {
  float ax = fabsf(dx), ay = fabsf(dy), k = ax * 0.5f + ay * 0.8660254f;
  return 0.8660254f - (ax > k ? ax : k);
}

/* какая клетка под точкой; −1 — мимо. dx, dy — место в клетке (в долях R) */
static int sg_geo_hit(const SgGeo *g, float x, float y, float *dx, float *dy) {
  int r0 = (int)floorf((y - g->oy - g->R) / (1.5f * g->R) + 0.5f);
  int best = -1;
  float bd = 1e18f, bx = 0, by = 0;
  for (int r = r0 - 1; r <= r0 + 1; r++) {
    if (r < 0 || r >= SG_H) continue;
    int c0 = (int)floorf((x - g->ox) / g->w - 0.5f * (r & 1));
    for (int c = c0 - 1; c <= c0 + 1; c++) {
      if (!sg_valid(c, r)) continue;
      float cx = g->ox + g->w * (c + 0.5f * (r & 1) + 0.5f), cy = g->oy + g->R * (1 + 1.5f * r);
      float d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d < bd) bd = d, best = sg_idx(c, r), bx = x - cx, by = y - cy;
    }
  }
  if (best < 0) return -1;
  bx /= g->R;
  by /= g->R;
  if (sg_hex_edge(bx, by) < 0) return -1;
  if (dx) *dx = bx;
  if (dy) *dy = by;
  return g->flip ? sg_mirror(best) : best;
}

/* ---- расстояния до простых форм (минус — внутри) ----------------------------- */

/* отрезок с толщиной: у начала радиус ra, у конца rb */
static float sg_sd_seg(float x, float y, float ax, float ay, float bx, float by, float ra, float rb) {
  float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
  float t = l2 > 0 ? ((x - ax) * vx + (y - ay) * vy) / l2 : 0;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  float px = x - (ax + vx * t), py = y - (ay + vy * t);
  return sqrtf(px * px + py * py) - (ra + (rb - ra) * t);
}

static float sg_sd_circle(float x, float y, float cx, float cy, float r) {
  return sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r;
}

/* дуга окружности толщиной 2·hw от угла a0 до a1 (градусы, по часовой от оси X, Y вниз) */
static float sg_sd_arc(float x, float y, float cx, float cy, float r, float hw, float a0, float a1) {
  float a = atan2f(y - cy, x - cx) * 57.29578f;
  while (a < a0) a += 360;
  while (a >= a0 + 360) a -= 360;
  if (a <= a1) return fabsf(sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r) - hw;
  float e0x = cx + r * cosf(a0 / 57.29578f), e0y = cy + r * sinf(a0 / 57.29578f);
  float e1x = cx + r * cosf(a1 / 57.29578f), e1y = cy + r * sinf(a1 / 57.29578f);
  float d0 = sg_sd_circle(x, y, e0x, e0y, hw), d1 = sg_sd_circle(x, y, e1x, e1y, hw);
  return d0 < d1 ? d0 : d1;
}

/* многоугольник (точки x, y подряд) */
static float sg_sd_poly(const float *p, int n, float x, float y) {
  float d = 1e18f;
  int in = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float ax = p[j * 2], ay = p[j * 2 + 1], bx = p[i * 2], by = p[i * 2 + 1];
    float s = sg_sd_seg(x, y, ax, ay, bx, by, 0, 0);
    if (s < d) d = s;
    if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax) in = !in;
  }
  return in ? -d : d;
}

static float sg_sd_box(float x, float y, float x0, float y0, float x1, float y1) {
  float dx = x < x0 ? x0 - x : x > x1 ? x - x1 : 0, dy = y < y0 ? y0 - y : y > y1 ? y - y1 : 0;
  if (dx > 0 || dy > 0) return sqrtf(dx * dx + dy * dy);
  float in = x - x0;
  if (x1 - x < in) in = x1 - x;
  if (y - y0 < in) in = y - y0;
  if (y1 - y < in) in = y1 - y;
  return -in;
}

static float sg_min(float a, float b) { return a < b ? a : b; }
static float sg_clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float sg_smooth(float a, float b, float v) {
  float t = sg_clamp01((v - a) / (b - a));
  return t * t * (3 - 2 * t);
}

/* цвета сторон: 0 — синие, 1 — красные */
static void sg_side_rgb(int side, float *r, float *g, float *b) {
  if (side == 0) *r = 42, *g = 98, *b = 180;
  else *r = 188, *g = 50, *b = 44;
}

/* ---- местность ---------------------------------------------------------------- */

static void sg_mul(float *r, float *g, float *b, float k) { *r *= k, *g *= k, *b *= k; }
static void sg_mix(float *r, float *g, float *b, float r2, float g2, float b2, float t) {
  *r += (r2 - *r) * t;
  *g += (g2 - *g) * t;
  *b += (b2 - *b) * t;
}

static void sg_grass(float px, float py, float sc, float *r, float *g, float *b) {
  float n = gn_noise(px / (11 * sc), py / (11 * sc), 11u), n2 = gn_noise(px / (2.4f * sc), py / (2.4f * sc), 5u);
  *r = 138, *g = 178, *b = 96;
  sg_mul(r, g, b, 0.93f + 0.11f * n + 0.05f * (n2 - 0.5f));
  /* редкие травинки */
  float gx = px / (7 * sc), gy = py / (6 * sc);
  int ix = (int)floorf(gx), iy = (int)floorf(gy);
  if (gn_hash01(ix, iy, 77u) < 0.2f) {
    float lx = (gx - ix) - (0.3f + 0.4f * gn_hash01(ix, iy, 78u)), ly = (gy - iy) - 0.5f;
    if (fabsf(lx) < 0.07f && fabsf(ly) < 0.3f) sg_mul(r, g, b, 0.86f);
  }
}

/* лес: кроны спереди закрывают задние; свет слева сверху, под кроной тень */
static void sg_forest(unsigned seed, float dx, float dy, float px, float py, float sc, float *r, float *g, float *b) {
  static const float kT[6][2] = {{0.04f, -0.46f}, {-0.40f, -0.20f}, {0.44f, -0.16f}, {0.02f, 0.02f}, {-0.26f, 0.34f}, {0.28f, 0.36f}};
  *r = 108, *g = 150, *b = 86;
  sg_mul(r, g, b, 0.92f + 0.12f * gn_noise(px / (9 * sc), py / (9 * sc), 21u));
  float shade = 1;
  for (int i = 5; i >= 0; i--) {
    float tx = kT[i][0] + (gn_hash01(i, 1, seed) - 0.5f) * 0.12f, ty = kT[i][1] + (gn_hash01(i, 2, seed) - 0.5f) * 0.10f;
    float rad = 0.25f + gn_hash01(i, 3, seed) * 0.06f;
    float ux = (dx - tx) / rad, uy = (dy - (ty - 0.10f)) / rad, d2 = ux * ux + uy * uy;
    if (d2 < 1) {
      float lit = sg_clamp01(0.5f - ux * 0.36f - uy * 0.50f);
      *r = 36 + (104 - 36) * lit, *g = 90 + (164 - 90) * lit, *b = 50 + (88 - 50) * lit;
      sg_mul(r, g, b, 0.92f + 0.16f * gn_noise(px / (1.7f * sc), py / (1.7f * sc), 31u + (unsigned)i));
      if (d2 > 0.70f) sg_mul(r, g, b, 1.0f - 0.34f * (d2 - 0.70f) / 0.30f);
      return;
    }
    float sx = (dx - tx - 0.07f) / (rad * 0.95f), sy = (dy - ty - 0.13f) / (rad * 0.5f);
    if (sx * sx + sy * sy < 1) shade = 0.76f;
  }
  sg_mul(r, g, b, shade);
}

static void sg_hill(unsigned seed, float dx, float dy, float px, float py, float sc, float *r, float *g, float *b) {
  static const float kM[3][4] = {{0.26f, -0.26f, 0.40f, 0.30f}, {-0.24f, 0.00f, 0.46f, 0.36f}, {0.22f, 0.36f, 0.34f, 0.24f}};
  float gr, gg, gb;
  sg_grass(px, py, sc, &gr, &gg, &gb);
  sg_mix(&gr, &gg, &gb, 188, 186, 112, 0.55f);
  *r = gr, *g = gg, *b = gb;
  for (int i = 0; i < 3; i++) {
    float mx = kM[i][0] + (gn_hash01(i, 5, seed) - 0.5f) * 0.08f, my = kM[i][1];
    float u = (dx - mx) / kM[i][2], v = (dy - my) / kM[i][3], d2 = u * u + v * v;
    if (d2 >= 1) continue;
    float h = sqrtf(1 - d2), lit = sg_clamp01(0.52f - u * 0.40f - v * 0.46f + h * 0.22f);
    float cr = 132 + (226 - 132) * lit, cg = 128 + (214 - 128) * lit, cb = 70 + (130 - 70) * lit;
    if (d2 > 0.80f && v < 0.15f) sg_mul(&cr, &cg, &cb, 0.80f); /* кромка сверху */
    float fade = sg_smooth(0.15f, 0.92f, v);                   /* низ холма уходит в землю */
    *r = cr + (gr - cr) * fade, *g = cg + (gg - cg) * fade, *b = cb + (gb - cb) * fade;
  }
}

static void sg_mount(unsigned seed, float dx, float dy, float px, float py, float sc, float *r, float *g, float *b) {
  /* вершина x, y; полуширина у подножия; подножие y */
  static const float kP[3][4] = {{-0.30f, -0.50f, 0.34f, 0.20f}, {0.34f, -0.40f, 0.32f, 0.26f}, {0.00f, -0.30f, 0.44f, 0.56f}};
  *r = 150, *g = 146, *b = 126;
  sg_mul(r, g, b, 0.90f + 0.16f * gn_noise(px / (5 * sc), py / (5 * sc), 41u));
  for (int i = 0; i < 3; i++) {
    float ax = kP[i][0] + (gn_hash01(i, 7, seed) - 0.5f) * 0.08f, ay = kP[i][1], hw = kP[i][2], by = kP[i][3];
    if (dy < ay || dy > by) continue;
    float t = (dy - ay) / (by - ay), half = hw * t, off = dx - ax;
    if (fabsf(off) > half) continue;
    float ridge = half * 0.22f * sinf(t * 9 + (float)i);
    int left = off < ridge;
    if (left) *r = 204, *g = 198, *b = 186;
    else *r = 116, *g = 110, *b = 110;
    sg_mul(r, g, b, 0.92f + 0.14f * gn_noise(px / (2.2f * sc), py / (3.5f * sc), 43u));
    float snow = 0.34f + 0.07f * sinf(off * 38 + (float)i * 2);
    if (t < snow) {
      if (left) *r = 250, *g = 251, *b = 253;
      else *r = 198, *g = 208, *b = 226;
    }
    if (half - fabsf(off) < 0.035f || by - dy < 0.03f) *r = 74, *g = 68, *b = 66; /* контур */
  }
}

static void sg_water(float e, float px, float py, float sc, float *r, float *g, float *b) {
  float n = gn_noise(px / (16 * sc), py / (8 * sc), 51u);
  *r = 50 + 30 * n, *g = 106 + 42 * n, *b = 170 + 32 * n;
  float w = sinf(py / sc * 0.62f + gn_noise(px / (9 * sc), py / (9 * sc), 53u) * 7);
  if (w > 0.90f) sg_mix(r, g, b, 190, 226, 244, 0.42f * (w - 0.90f) / 0.10f);
  if (e < 0.16f) sg_mix(r, g, b, 132, 196, 214, 0.55f * (1 - e / 0.16f)); /* мелководье у берега */
}

static void sg_village_ground(float dx, float dy, float px, float py, float sc, float *r, float *g, float *b) {
  sg_grass(px, py, sc, r, g, b);
  /* пашня: две полосатые делянки по краям */
  for (int i = 0; i < 2; i++) {
    float cx = i ? 0.44f : -0.46f, cy = i ? -0.34f : -0.30f, ang = i ? 0.5f : -0.45f;
    float lx = (dx - cx) * cosf(ang) + (dy - cy) * sinf(ang), ly = -(dx - cx) * sinf(ang) + (dy - cy) * cosf(ang);
    if (fabsf(lx) < 0.22f && fabsf(ly) < 0.17f) {
      *r = 212, *g = 190, *b = 104;
      float f = fabsf(fmodf(ly * 14 + 20, 1.0f) - 0.5f);
      if (f < 0.2f) sg_mul(r, g, b, 0.84f);
    }
  }
  /* дорожка к домам */
  float path = fabsf(dx + 0.10f * sinf(dy * 5)) - 0.07f;
  if (path < 0 && dy > 0.30f) sg_mix(r, g, b, 196, 176, 132, 0.8f);
}

static void sg_court(float dx, float dy, float px, float py, float sc, float *r, float *g, float *b) {
  *r = 182, *g = 174, *b = 156;
  float d = sqrtf(dx * dx + dy * dy), ring = fabsf(fmodf(d * 6 + 0.5f, 1.0f) - 0.5f);
  float a = atan2f(dy, dx), seam = fabsf(fmodf(a * (2 + floorf(d * 6)) * 1.3f + 40, 1.0f) - 0.5f);
  sg_mul(r, g, b, 0.90f + 0.16f * gn_noise(px / (2.6f * sc), py / (2.6f * sc), 61u));
  if (ring < 0.08f || seam < 0.07f) sg_mul(r, g, b, 0.84f);
}

/* цвет точки плитки: terr — местность, e — до края (в долях R) */
static void sg_tile_px(int terr, unsigned seed, float dx, float dy, float e, float px, float py, float sc, float *r,
                       float *g, float *b) {
  switch (terr) {
  case SGT_FOREST:
    sg_forest(seed, dx, dy, px, py, sc, r, g, b);
    break;
  case SGT_HILL:
    sg_hill(seed, dx, dy, px, py, sc, r, g, b);
    break;
  case SGT_MOUNT:
    sg_mount(seed, dx, dy, px, py, sc, r, g, b);
    break;
  case SGT_WATER:
    sg_water(e, px, py, sc, r, g, b);
    break;
  case SGT_VILLAGE:
    sg_village_ground(dx, dy, px, py, sc, r, g, b);
    break;
  case SGT_CASTLE:
    sg_court(dx, dy, px, py, sc, r, g, b);
    break;
  default:
    sg_grass(px, py, sc, r, g, b);
  }
  /* фаска плитки: к свету светлее, от света темнее; у воды наоборот — она ниже */
  float band = 0.10f;
  if (e < band) {
    float l = sqrtf(dx * dx + dy * dy), lit = l > 0 ? (-dx * 0.45f - dy * 0.89f) / l : 0;
    float k = (1 - e / band) * (terr == SGT_WATER ? -0.16f : 0.20f) * lit;
    sg_mul(r, g, b, 1 + k);
  }
}

/* Карта целиком: рама fr, внутри — тёмное сукно и плитки. Деревни и замки
   рисуются отдельно, поверх (их хозяин меняется). */
static void sg_render_map(GnSprite *s, const SgState *st, const SgGeo *geo, int W, int H, int fr) {
  if (!gn_sprite_alloc(s, W, H)) return;
  float sc = geo->R / 28.0f, groove = 0.045f;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int dl = x, dt = y, drt = W - 1 - x, db = H - 1 - y;
      int m = dl, side = 0;
      if (dt < m) m = dt, side = 1;
      if (drt < m) m = drt, side = 2;
      if (db < m) m = db, side = 3;
      float r, g, b;
      if (m < fr) { /* рама: орех, волокна вдоль стороны, золотая нить — как у доски шашек */
        float wd = (side & 1) ? gn_wood((float)x, (float)y, sc, 41u + (unsigned)side)
                              : gn_wood((float)y, (float)x, sc, 53u + (unsigned)side);
        r = 98 * wd, g = 58 * wd, b = 32 * wd;
        float k = m / (float)fr;
        if (k < 0.14f) sg_mul(&r, &g, &b, side == 0 || side == 1 ? 1.22f : 0.72f);
        else if (k > 0.78f) sg_mul(&r, &g, &b, side == 0 || side == 1 ? 0.70f : 1.12f);
        else sg_mul(&r, &g, &b, 1.0f + 0.10f * (0.5f - fabsf(k - 0.46f)));
        if (m == fr - (int)(4 * sc + 0.5f)) r = 214, g = 170, b = 88;
        s->px[(size_t)y * W + x] = gn_pm(r, g, b, 1.0f);
        continue;
      }
      float ar = 0, ag = 0, ab = 0;
      for (int sy = 0; sy < SG_MAP_SS; sy++)
        for (int sx = 0; sx < SG_MAP_SS; sx++) {
          float px = x + (sx + 0.5f) / SG_MAP_SS, py = y + (sy + 0.5f) / SG_MAP_SS, dx, dy;
          int cell = sg_geo_hit(geo, px, py, &dx, &dy);
          float e = cell < 0 ? -1 : sg_hex_edge(dx, dy);
          if (e < groove) {
            /* сукно под плитками; у самой плитки — тень */
            r = 30, g = 46, b = 58;
            sg_mul(&r, &g, &b, 0.86f + 0.24f * gn_noise(px / (3 * sc), py / (3 * sc), 71u));
            if (cell >= 0) sg_mul(&r, &g, &b, 0.62f);
          } else {
            unsigned seed = (unsigned)(geo->flip ? sg_mirror(cell) : cell) * 7919u + 13u;
            sg_tile_px(st->terr[cell], seed, dx, dy, e - groove, px, py, sc, &r, &g, &b);
            if (e - groove < 0.03f) sg_mul(&r, &g, &b, 0.80f); /* тёмная кромка */
          }
          ar += r, ag += g, ab += b;
        }
      float k = 1.0f / (SG_MAP_SS * SG_MAP_SS);
      r = ar * k, g = ag * k, b = ab * k;
      /* тень рамы на поле */
      int in = m - fr;
      float lim = 8 * sc;
      if (in < lim && (side == 0 || side == 1)) sg_mul(&r, &g, &b, 1 - 0.30f * (1 - in / lim) * (1 - in / lim));
      s->px[(size_t)y * W + x] = gn_pm(r, g, b, 1.0f);
    }
}

/* ---- спрайты из проб -------------------------------------------------------- */

/* спрайт d×d: fn даёт цвет и непрозрачность точки (x, y — от −0.5 до 0.5) */
typedef float (*SgPaintFn)(void *ctx, float x, float y, float *r, float *g, float *b);

static void sg_render_fn(GnSprite *s, int d, SgPaintFn fn, void *ctx) {
  if (!gn_sprite_alloc(s, d, d)) return;
  for (int y = 0; y < d; y++)
    for (int x = 0; x < d; x++) {
      float ar = 0, ag = 0, ab = 0, aa = 0;
      for (int sy = 0; sy < SG_SS; sy++)
        for (int sx = 0; sx < SG_SS; sx++) {
          float r = 0, g = 0, b = 0;
          float a = fn(ctx, (x + (sx + 0.5f) / SG_SS) / d - 0.5f, (y + (sy + 0.5f) / SG_SS) / d - 0.5f, &r, &g, &b);
          ar += r * a, ag += g * a, ab += b * a, aa += a;
        }
      if (aa <= 0) continue;
      s->px[(size_t)y * d + x] = gn_pm(ar / aa, ag / aa, ab / aa, aa / (SG_SS * SG_SS));
    }
}

/* ---- фишки отрядов ----------------------------------------------------------- */

/* значок отряда: расстояние до него (минус — внутри); x, y — от −0.5 до 0.5 по фишке */
static float sg_icon(int type, float x, float y) {
  float d = 1e9f;
  switch (type) {
  case SGU_SWORD: /* меч остриём вправо вверх: клинок, гарда, рукоять, навершие */
    d = sg_sd_seg(x, y, -0.085f, 0.085f, 0.235f, -0.235f, 0.050f, 0.012f);
    d = sg_min(d, sg_sd_seg(x, y, -0.185f, -0.015f, 0.015f, 0.185f, 0.034f, 0.034f));
    d = sg_min(d, sg_sd_seg(x, y, -0.10f, 0.10f, -0.19f, 0.19f, 0.030f, 0.030f));
    d = sg_min(d, sg_sd_circle(x, y, -0.215f, 0.215f, 0.048f));
    break;
  case SGU_SPEAR: { /* копьё за щитом; щит — «утюжок» остриём вниз */
    static const float kS[] = {-0.190f, -0.050f, 0.085f, -0.050f, 0.085f, 0.085f, -0.0525f, 0.270f, -0.190f, 0.085f};
    float sh = sg_sd_poly(kS, 5, x, y) - 0.016f;
    float sp = sg_min(sg_sd_seg(x, y, -0.255f, 0.31f, 0.13f, -0.16f, 0.024f, 0.024f),
                      sg_sd_seg(x, y, 0.11f, -0.135f, 0.225f, -0.285f, 0.066f, 0.0f));
    if (0.034f - sh > sp) sp = 0.034f - sh; /* у щита копьё прерывается: оно за ним */
    d = sg_min(sp, sh);
    break;
  }
  case SGU_ARCHER: /* лук с натянутой тетивой и стрела вправо */
    d = sg_sd_arc(x, y, -0.12f, 0.0f, 0.30f, 0.030f, -62, 62);
    d = sg_min(d, sg_sd_seg(x, y, 0.021f, -0.265f, -0.17f, 0.0f, 0.011f, 0.011f));
    d = sg_min(d, sg_sd_seg(x, y, 0.021f, 0.265f, -0.17f, 0.0f, 0.011f, 0.011f));
    d = sg_min(d, sg_sd_seg(x, y, -0.17f, 0.0f, 0.22f, 0.0f, 0.020f, 0.020f));
    d = sg_min(d, sg_sd_seg(x, y, 0.20f, 0.0f, 0.315f, 0.0f, 0.062f, 0.0f));
    break;
  case SGU_HORSE: { /* голова коня в профиль, как шахматный конь */
    static const float kH[] = {-0.285f, -0.020f, -0.250f, -0.085f, -0.150f, -0.165f, -0.045f, -0.270f, 0.005f, -0.365f,
                               0.060f,  -0.265f, 0.160f,  -0.170f, 0.235f,  0.020f,  0.250f, 0.275f,  -0.130f, 0.275f,
                               -0.085f, 0.130f,  -0.020f, 0.030f,  -0.110f, 0.055f,  -0.215f, 0.105f, -0.275f, 0.075f};
    d = sg_sd_poly(kH, 15, x, y) - 0.012f;
    float eye = sg_sd_circle(x, y, -0.055f, -0.125f, 0.030f);
    if (-eye > d) d = -eye; /* глаз — вырез */
    break;
  }
  case SGU_CATA: /* катапульта: рама на колёсах, стойка, рычаг с ядром */
    d = sg_sd_seg(x, y, -0.25f, 0.17f, 0.25f, 0.17f, 0.030f, 0.030f);
    d = sg_min(d, sg_sd_circle(x, y, -0.17f, 0.215f, 0.078f));
    d = sg_min(d, sg_sd_circle(x, y, 0.17f, 0.215f, 0.078f));
    d = sg_min(d, sg_sd_seg(x, y, -0.02f, 0.15f, 0.05f, -0.07f, 0.030f, 0.026f));
    d = sg_min(d, sg_sd_seg(x, y, 0.13f, 0.15f, 0.05f, -0.07f, 0.026f, 0.026f));
    d = sg_min(d, sg_sd_seg(x, y, -0.235f, -0.215f, 0.19f, 0.03f, 0.024f, 0.030f));
    d = sg_min(d, sg_sd_circle(x, y, -0.245f, -0.235f, 0.072f));
    break;
  }
  return d;
}

typedef struct {
  int type, side, dim;
} SgTokenCtx;

static float sg_token_px(void *ctx, float x, float y, float *r, float *g, float *b) {
  const SgTokenCtx *t = (const SgTokenCtx *)ctx;
  float rad = sqrtf(x * x + y * y);
  if (rad > 0.485f) return 0;
  float sr, sg, sb;
  sg_side_rgb(t->side, &sr, &sg, &sb);
  float lit = sg_clamp01(0.5f - x * 0.75f - y * 1.0f); /* свет слева сверху */
  if (rad > 0.468f) *r = sr * 0.26f, *g = sg * 0.26f, *b = sb * 0.26f; /* обводка */
  else if (rad > 0.405f) { /* ободок: скруглённый валик */
    float k = (rad - 0.405f) / 0.063f, round = 1 - (2 * k - 1) * (2 * k - 1);
    float m = 0.50f + 0.62f * lit * round + 0.10f * round;
    *r = sr * m, *g = sg * m, *b = sb * m;
  } else if (rad > 0.385f) { /* золотая нить */
    *r = 236 * (0.72f + 0.4f * lit), *g = 198 * (0.72f + 0.4f * lit), *b = 112 * (0.72f + 0.4f * lit);
  } else {
    float m = 0.84f + 0.34f * lit;
    *r = sr * m, *g = sg * m, *b = sb * m;
    float d = sg_icon(t->type, x, y);
    if (d < 0) { /* кость: чуть темнее книзу */
      float sh = 1.0f - 0.14f * sg_clamp01(y * 2 + 0.5f);
      *r = 250 * sh, *g = 244 * sh, *b = 226 * sh;
    } else if (d < 0.030f) {
      *r = sr * 0.30f, *g = sg * 0.30f, *b = sb * 0.30f;
    } else {
      /* тень значка на фишке */
      float d2 = sg_icon(t->type, x - 0.018f, y - 0.03f);
      if (d2 < 0.02f) sg_mul(r, g, b, 0.74f);
    }
  }
  if (t->dim) { /* уже ходил: тусклее и серее */
    float l = (*r * 0.30f + *g * 0.55f + *b * 0.15f);
    *r = (*r + (l - *r) * 0.62f) * 0.74f + 30;
    *g = (*g + (l - *g) * 0.62f) * 0.74f + 30;
    *b = (*b + (l - *b) * 0.62f) * 0.74f + 30;
  }
  return 1;
}

static void sg_render_token(GnSprite *s, int d, int type, int side, int dim) {
  SgTokenCtx c = {type, side, dim};
  sg_render_fn(s, d, sg_token_px, &c);
}

/* ---- деревня и замок ---------------------------------------------------------- */

/* слой рисунка: если точка в форме (расстояние d) — цвет заливки, у края — контур */
static void sg_layer(float d, float o, float fr, float fg, float fb, float *r, float *g, float *b, float *a) {
  if (d < 0) *r = fr, *g = fg, *b = fb, *a = 1;
  else if (d < o) *r = 58, *g = 50, *b = 46, *a = 1;
}

/* owner: −1 — ничья, 0 или 1 */
static float sg_village_px(void *ctx, float x, float y, float *r, float *g, float *b) {
  int owner = *(const int *)ctx;
  /* центр, ширина, высота стены, высота крыши — в долях спрайта (2·R) */
  static const float kB[3][5] = {{0.03f, -0.115f, 0.185f, 0.115f, 0.105f}, {-0.155f, 0.085f, 0.20f, 0.125f, 0.115f}, {0.15f, 0.105f, 0.185f, 0.125f, 0.105f}};
  float rr = 160, rg = 112, rb = 74, a = 0, o = 0.016f;
  if (owner >= 0) {
    sg_side_rgb(owner, &rr, &rg, &rb);
    rr = rr * 0.9f + 40, rg = rg * 0.9f + 40, rb = rb * 0.9f + 40;
  }
  for (int i = 0; i < 3; i++) {
    float cx = kB[i][0], cy = kB[i][1], w = kB[i][2], h = kB[i][3], rh = kB[i][4];
    float x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h / 2, y1 = cy + h / 2;
    /* тень дома на земле */
    if (a == 0 && sg_sd_box(x, y, x0 + 0.03f, y1 - 0.02f, x1 + 0.05f, y1 + 0.03f) < 0) *r = 40, *g = 50, *b = 30, a = 0.35f;
    float wall = sg_sd_box(x, y, x0, y0, x1, y1), sh = x > cx + w * 0.12f ? 0.80f : 1.0f;
    sg_layer(wall, o, 240 * sh, 228 * sh, 198 * sh, r, g, b, &a);
    if (sg_sd_box(x, y, cx - w * 0.10f, y1 - h * 0.58f, cx + w * 0.10f, y1) < 0) *r = 92, *g = 62, *b = 44, a = 1; /* дверь */
    float roof[6] = {x0 - 0.028f, y0 + 0.004f, x1 + 0.028f, y0 + 0.004f, cx, y0 - rh};
    float rs = x > cx ? 0.74f : 1.0f;
    sg_layer(sg_sd_poly(roof, 3, x, y), o, rr * rs, rg * rs, rb * rs, r, g, b, &a);
  }
  return a;
}

static float sg_castle_px(void *ctx, float x, float y, float *r, float *g, float *b) {
  int side = *(const int *)ctx;
  float cr, cg, cb, a = 0, o = 0.016f;
  sg_side_rgb(side, &cr, &cg, &cb);
  cr = cr * 0.9f + 36, cg = cg * 0.9f + 36, cb = cb * 0.9f + 36;
  y -= 0.06f; /* замок стоит чуть ниже центра: над ним флаг */
  /* тень на дворе */
  if (sg_sd_box(x, y, -0.24f, 0.17f, 0.30f, 0.225f) < 0) *r = 40, *g = 36, *b = 30, a = 0.35f;
  /* флаг: древко и вымпел цвета стороны */
  sg_layer(sg_sd_seg(x, y, 0, -0.20f, 0, -0.385f, 0.008f, 0.008f), 0.008f, 70, 60, 54, r, g, b, &a);
  {
    float fl[6] = {0.008f, -0.385f, 0.150f, -0.335f, 0.008f, -0.285f};
    sg_layer(sg_sd_poly(fl, 3, x, y), o * 0.8f, cr * 1.08f, cg * 1.08f, cb * 1.08f, r, g, b, &a);
  }
  /* башни по бокам, потом середина — она впереди */
  for (int t = 0; t < 3; t++) {
    float x0 = t == 0 ? -0.275f : t == 1 ? 0.135f : -0.135f, x1 = t == 0 ? -0.135f : t == 1 ? 0.275f : 0.135f;
    float top = t == 2 ? -0.145f : -0.045f, bot = 0.205f, mw = (x1 - x0) / 5, mh = 0.052f;
    float d = sg_sd_box(x, y, x0, top, x1, bot);
    for (int k = 0; k < 3; k++) /* зубцы */
      d = sg_min(d, sg_sd_box(x, y, x0 + mw * 2 * k, top - mh, x0 + mw * (2 * k + 1), top + 0.01f));
    float sh = (x - x0) / (x1 - x0), m = 1.0f - 0.24f * sg_smooth(0.45f, 1.0f, sh);
    /* кладка: ряды и швы */
    float row = (y - top) / 0.047f, fr = row - floorf(row);
    float col = (x - x0) / 0.058f + ((int)floorf(row) & 1) * 0.5f, fc = col - floorf(col);
    if (y > top && (fr < 0.14f || fc < 0.10f)) m *= 0.90f;
    sg_layer(d, o, 212 * m, 206 * m, 192 * m, r, g, b, &a);
    if (t < 2 && sg_sd_box(x, y, (x0 + x1) / 2 - 0.012f, 0.02f, (x0 + x1) / 2 + 0.012f, 0.085f) < 0) *r = 52, *g = 44, *b = 40, a = 1; /* бойница */
  }
  /* полоса цвета стороны и ворота */
  if (sg_sd_box(x, y, -0.127f, -0.085f, 0.127f, -0.030f) < 0) {
    float m = 1.0f - 0.2f * sg_smooth(0.0f, 0.127f, x);
    *r = cr * m, *g = cg * m, *b = cb * m, a = 1;
  }
  float gate = sg_min(sg_sd_box(x, y, -0.048f, 0.095f, 0.048f, 0.205f), sg_sd_circle(x, y, 0, 0.095f, 0.048f));
  if (gate < 0) *r = 56, *g = 42, *b = 34, a = 1;
  else if (gate < 0.014f && y < 0.205f) *r = 120, *g = 112, *b = 100, a = 1;
  return a;
}

/* ---- подсветка клеток --------------------------------------------------------- */

typedef struct {
  float fr, fg, fb, fillA; /* заливка */
  float rr, rg, rb, ringA; /* обводка шириной ringW (в долях R) */
  float ringW, inset;
} SgHexCtx;

static float sg_hexmark_px(void *ctx, float x, float y, float *r, float *g, float *b) {
  const SgHexCtx *h = (const SgHexCtx *)ctx;
  float e = sg_hex_edge(x * 2, y * 2) - h->inset; /* спрайт — 2·R на 2·R */
  if (e < 0) return 0;
  if (e < h->ringW && h->ringA > 0) {
    *r = h->rr, *g = h->rg, *b = h->rb;
    return h->ringA;
  }
  *r = h->fr, *g = h->fg, *b = h->fb;
  return h->fillA;
}

/* шестиугольник-метка под клетку радиуса R: заливка и обводка */
static void sg_make_hex(GnSprite *s, float R, COLORREF fill, float fillA, COLORREF ring, float ringA, float ringW) {
  SgHexCtx c = {GetRValue(fill), GetGValue(fill), GetBValue(fill), fillA, GetRValue(ring), GetGValue(ring), GetBValue(ring), ringA, ringW, 0.06f};
  sg_render_fn(s, (int)(R * 2 + 0.5f), sg_hexmark_px, &c);
}
