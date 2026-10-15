/* Бой Годжо и Сукуны при бездействии — рисование (с 2026.10.15.1).

   Всё рисуется кодом, без картинок. Фигуры собраны из простых форм — капсул,
   эллипсов и многоугольников; формы одной части (рукав, голова, волосы)
   сливаются в одну, и обводка идёт только по общему краю. Тень — полумесяц с
   той стороны, что дальше от света (свет слева сверху и для обоих, как в
   одной сцене). Буфер — 32 бита BGRA с уже умноженной альфой: так его берёт
   UpdateLayeredWindow. Окон и Windows тут нет, только арифметика над буфером. */

typedef struct {
  float r, g, b;
} FCol;
#define FC(r, g, b) ((FCol){(float)(r), (float)(g), (float)(b)})

typedef struct {
  unsigned char *px; /* BGRA, альфа умножена */
  int w, h;
  int x0, y0, x1, y1;     /* обрезка: рисуется только внутри [x0,x1)×[y0,y1) */
  int tx0, ty0, tx1, ty1; /* что задето рисованием: отсюда «грязный» прямоугольник кадра */
} FCv;

#define FA_PI 3.14159265f
#define FA_RAD (FA_PI / 180.0f)

static float fa_sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float fa_clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float fa_mixf(float a, float b, float t) { return a + (b - a) * t; }
static FCol fa_mix(FCol a, FCol b, float t) {
  FCol c = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
  return c;
}
static FCol fa_dim(FCol a, float k) {
  FCol c = {a.r * k, a.g * k, a.b * k};
  return c;
}
static float fa_smooth(float t) { /* плавно на концах */
  t = fa_sat(t);
  return t * t * (3.0f - 2.0f * t);
}

static void fv_reset_touch(FCv *c) {
  c->tx0 = c->ty0 = 1 << 30;
  c->tx1 = c->ty1 = -(1 << 30);
}

/* прямоугольник в пикселях, обрезанный по холсту; заодно помечается задетым */
static int fv_clip(FCv *c, float fx0, float fy0, float fx1, float fy1, int *x0, int *y0, int *x1, int *y1) {
  int a = (int)floorf(fx0), b = (int)floorf(fy0), e = (int)ceilf(fx1), d = (int)ceilf(fy1);
  if (a < c->x0) a = c->x0;
  if (b < c->y0) b = c->y0;
  if (e > c->x1) e = c->x1;
  if (d > c->y1) d = c->y1;
  if (a >= e || b >= d) return 0;
  if (a < c->tx0) c->tx0 = a;
  if (b < c->ty0) c->ty0 = b;
  if (e > c->tx1) c->tx1 = e;
  if (d > c->ty1) c->ty1 = d;
  *x0 = a, *y0 = b, *x1 = e, *y1 = d;
  return 1;
}

/* обычное наложение: цвет с прозрачностью a поверх того, что уже есть */
static inline void fv_over(unsigned char *d, FCol c, float a) {
  if (a <= 0.0f) return;
  if (a > 1.0f) a = 1.0f;
  float k = 1.0f - a;
  d[0] = (unsigned char)(c.b * a + d[0] * k + 0.5f);
  d[1] = (unsigned char)(c.g * a + d[1] * k + 0.5f);
  d[2] = (unsigned char)(c.r * a + d[2] * k + 0.5f);
  d[3] = (unsigned char)(255.0f * a + d[3] * k + 0.5f);
}

/* свет: «экран» — светлеет то, что уже нарисовано, и не темнеет ничто.
   Альфа берётся по самому яркому каналу, так что буфер остаётся честным
   (цвет не ярче альфы) и окно Windows его понимает */
static inline void fv_light(unsigned char *d, FCol c, float w) {
  if (w <= 0.0f) return;
  if (w > 1.0f) w = 1.0f;
  float sb = c.b * w, sg = c.g * w, sr = c.r * w;
  float sa = (sr > sg ? (sr > sb ? sr : sb) : (sg > sb ? sg : sb)) * (1.0f / 255.0f);
  d[0] = (unsigned char)(sb + d[0] - sb * d[0] * (1.0f / 255.0f) + 0.5f);
  d[1] = (unsigned char)(sg + d[1] - sg * d[1] * (1.0f / 255.0f) + 0.5f);
  d[2] = (unsigned char)(sr + d[2] - sr * d[2] * (1.0f / 255.0f) + 0.5f);
  d[3] = (unsigned char)(sa * 255.0f + d[3] - sa * d[3] + 0.5f);
}

/* ---- формы и расстояние до края (отрицательное — внутри) ---- */

enum { FS_CAP, FS_ELL, FS_POLY };
#define FS_PTS 20
typedef struct {
  int type, n;
  float a[FS_PTS * 2]; /* капсула: ax ay bx by; эллипс: cx cy; многоугольник: x0 y0 x1 y1 … */
  float ra, rb;        /* капсула: радиусы на концах; эллипс: полуоси */
  float ux, uy, len;   /* капсула: ось и длина; эллипс: cos и sin поворота */
  float kb, ka;        /* капсула с разными концами: наклон боков */
  float x0, y0, x1, y1;
} FShape;

static float fs_sdf(const FShape *s, float x, float y) {
  if (s->type == FS_CAP) {
    float qx = x - s->a[0], qy = y - s->a[1];
    float along = qx * s->ux + qy * s->uy, perp = fabsf(qx * s->uy - qy * s->ux);
    if (s->len < 1e-4f) return sqrtf(qx * qx + qy * qy) - s->ra;
    float k = -s->kb * perp + s->ka * along;
    if (k < 0.0f) return sqrtf(perp * perp + along * along) - s->ra;
    if (k > s->ka * s->len) {
      float t = along - s->len;
      return sqrtf(perp * perp + t * t) - s->rb;
    }
    return s->ka * perp + s->kb * along - s->ra;
  }
  if (s->type == FS_ELL) {
    float dx = x - s->a[0], dy = y - s->a[1];
    float lx = dx * s->ux + dy * s->uy, ly = -dx * s->uy + dy * s->ux;
    float ex = lx / s->ra, ey = ly / s->rb;
    float k0 = sqrtf(ex * ex + ey * ey);
    float fx = ex / s->ra, fy = ey / s->rb, k1 = sqrtf(fx * fx + fy * fy);
    if (k1 < 1e-6f) return -(s->ra < s->rb ? s->ra : s->rb);
    return k0 * (k0 - 1.0f) / k1;
  }
  /* многоугольник любой формы, не только выпуклый */
  const float *v = s->a;
  int n = s->n;
  float d = (x - v[0]) * (x - v[0]) + (y - v[1]) * (y - v[1]), sg = 1.0f;
  for (int i = 0, j = n - 1; i < n; j = i, i++) {
    float ex = v[j * 2] - v[i * 2], ey = v[j * 2 + 1] - v[i * 2 + 1];
    float wx = x - v[i * 2], wy = y - v[i * 2 + 1];
    float ee = ex * ex + ey * ey, t = ee > 1e-9f ? fa_sat((wx * ex + wy * ey) / ee) : 0.0f;
    float bx = wx - ex * t, by = wy - ey * t, bb = bx * bx + by * by;
    if (bb < d) d = bb;
    int c1 = y >= v[i * 2 + 1], c2 = y < v[j * 2 + 1], c3 = ex * wy > ey * wx;
    if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) sg = -sg;
  }
  return sg * sqrtf(d);
}

/* где по строке y форма может оказаться ближе m к точке: [*a, *b] */
static int fs_span(const FShape *s, float y, float m, float *a, float *b) {
  if (y < s->y0 - m || y > s->y1 + m) return 0;
  if (s->type == FS_CAP && s->len > 1e-4f) {
    float R = (s->ra > s->rb ? s->ra : s->rb) + m;
    float ay = s->a[1], dy = s->a[3] - ay, t0 = 0.0f, t1 = 1.0f;
    if (fabsf(dy) > 1e-3f) {
      float ta = (y - R - ay) / dy, tb = (y + R - ay) / dy;
      if (ta > tb) {
        float t = ta;
        ta = tb, tb = t;
      }
      t0 = ta > 0.0f ? ta : 0.0f;
      t1 = tb < 1.0f ? tb : 1.0f;
      if (t0 > t1) return 0;
    }
    float xa = s->a[0] + (s->a[2] - s->a[0]) * t0, xb = s->a[0] + (s->a[2] - s->a[0]) * t1;
    if (xa > xb) {
      float t = xa;
      xa = xb, xb = t;
    }
    *a = xa - R, *b = xb + R;
    return 1;
  }
  *a = s->x0 - m, *b = s->x1 + m;
  return 1;
}

static void fs_cap(FShape *s, float ax, float ay, float bx, float by, float ra, float rb) {
  s->type = FS_CAP;
  s->a[0] = ax, s->a[1] = ay, s->a[2] = bx, s->a[3] = by;
  if (ra < 0.05f) ra = 0.05f;
  if (rb < 0.05f) rb = 0.05f;
  float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
  if (l > 1e-4f && fabsf(ra - rb) >= l * 0.98f) { /* одна окружность внутри другой */
    if (ra < rb) s->a[0] = bx, s->a[1] = by, ra = rb;
    l = 0.0f;
  }
  s->ra = ra, s->rb = rb, s->len = l;
  s->ux = l > 1e-4f ? dx / l : 1.0f, s->uy = l > 1e-4f ? dy / l : 0.0f;
  s->kb = l > 1e-4f ? (ra - rb) / l : 0.0f;
  s->ka = sqrtf(1.0f - s->kb * s->kb);
  float R = ra > rb ? ra : rb;
  s->x0 = fminf(ax, bx) - R, s->x1 = fmaxf(ax, bx) + R;
  s->y0 = fminf(ay, by) - R, s->y1 = fmaxf(ay, by) + R;
}

static void fs_ell(FShape *s, float cx, float cy, float rx, float ry, float ux, float uy) {
  s->type = FS_ELL;
  s->a[0] = cx, s->a[1] = cy;
  s->ra = rx > 0.1f ? rx : 0.1f, s->rb = ry > 0.1f ? ry : 0.1f;
  float l = sqrtf(ux * ux + uy * uy);
  s->ux = l > 1e-6f ? ux / l : 1.0f, s->uy = l > 1e-6f ? uy / l : 0.0f;
  float ex = sqrtf(rx * rx * s->ux * s->ux + ry * ry * s->uy * s->uy);
  float ey = sqrtf(rx * rx * s->uy * s->uy + ry * ry * s->ux * s->ux);
  s->x0 = cx - ex, s->x1 = cx + ex, s->y0 = cy - ey, s->y1 = cy + ey;
}

static void fs_poly(FShape *s, const float *xy, int n) {
  if (n > FS_PTS) n = FS_PTS;
  s->type = FS_POLY, s->n = n;
  s->x0 = s->y0 = 1e9f, s->x1 = s->y1 = -1e9f;
  for (int i = 0; i < n; i++) {
    s->a[i * 2] = xy[i * 2], s->a[i * 2 + 1] = xy[i * 2 + 1];
    if (xy[i * 2] < s->x0) s->x0 = xy[i * 2];
    if (xy[i * 2] > s->x1) s->x1 = xy[i * 2];
    if (xy[i * 2 + 1] < s->y0) s->y0 = xy[i * 2 + 1];
    if (xy[i * 2 + 1] > s->y1) s->y1 = xy[i * 2 + 1];
  }
}

/* ---- часть: несколько форм одного цвета, общая обводка и тень ---- */

#define FP_MAX 18
typedef struct {
  FShape s[FP_MAX];
  int n;
  FCol lit, dark, lit2, dark2; /* свет и тень; lit2/dark2 — внизу, если grad */
  int grad;
  float gy0, gy1; /* переход цвета: от экранного y gy0 к gy1 */
  FCol line;      /* обводка */
  float ow;       /* её толщина, px; 0 — без обводки */
  float sh;       /* ширина тени, px; 0 — заливка ровная */
  float alpha;
} FPart;

static void fp_init(FPart *p, FCol lit, FCol dark, FCol line, float ow, float sh) {
  p->n = 0;
  p->lit = p->lit2 = lit;
  p->dark = p->dark2 = dark;
  p->grad = 0;
  p->gy0 = p->gy1 = 0.0f;
  p->line = line;
  p->ow = ow;
  p->sh = sh;
  p->alpha = 1.0f;
}

static FShape *fp_add(FPart *p) { return p->n < FP_MAX ? &p->s[p->n++] : NULL; }

static void fp_draw(FCv *c, const FPart *p) {
  if (p->n <= 0 || p->alpha <= 0.004f) return;
  float m = p->ow + 1.0f;
  float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
  for (int i = 0; i < p->n; i++) {
    const FShape *s = &p->s[i];
    if (s->x0 < bx0) bx0 = s->x0;
    if (s->y0 < by0) by0 = s->y0;
    if (s->x1 > bx1) bx1 = s->x1;
    if (s->y1 > by1) by1 = s->y1;
  }
  int X0, Y0, X1, Y1;
  if (!fv_clip(c, bx0 - m, by0 - m, bx1 + m, by1 + m, &X0, &Y0, &X1, &Y1)) return;
  float shx = 0.55f * p->sh, shy = 0.83f * p->sh; /* прочь от света — туда смотрит тень */
  float gk = p->grad && p->gy1 != p->gy0 ? 1.0f / (p->gy1 - p->gy0) : 0.0f;
  float edge = p->ow + 0.5f;
  for (int y = Y0; y < Y1; y++) {
    float fy = y + 0.5f, sa[FP_MAX], sb[FP_MAX], rx0 = 1e9f, rx1 = -1e9f;
    int act[FP_MAX], na = 0;
    for (int i = 0; i < p->n; i++) {
      float a, b;
      if (!fs_span(&p->s[i], fy, m + p->sh, &a, &b)) continue;
      act[na] = i, sa[na] = a, sb[na] = b, na++;
      if (a < rx0) rx0 = a;
      if (b > rx1) rx1 = b;
    }
    if (!na) continue;
    int xa = (int)floorf(rx0), xb = (int)ceilf(rx1);
    if (xa < X0) xa = X0;
    if (xb > X1) xb = X1;
    float t = gk != 0.0f ? fa_sat((fy - p->gy0) * gk) : 0.0f;
    FCol lit = p->grad ? fa_mix(p->lit, p->lit2, t) : p->lit;
    FCol dk = p->grad ? fa_mix(p->dark, p->dark2, t) : p->dark;
    unsigned char *row = c->px + (size_t)y * c->w * 4;
    for (int x = xa; x < xb; x++) {
      float fx = x + 0.5f, d = 1e9f;
      for (int k = 0; k < na; k++)
        if (fx >= sa[k] && fx <= sb[k]) {
          float v = fs_sdf(&p->s[act[k]], fx, fy);
          if (v < d) d = v;
        }
      if (d >= edge) continue;
      float aF = fa_sat(0.5f - d), aO = fa_sat(edge - d);
      FCol col = p->line;
      if (aF > 0.0f) {
        FCol fill = lit;
        if (p->sh > 0.0f) {
          float qx = fx + shx, qy = fy + shy, dq = 1e9f;
          for (int k = 0; k < na; k++) {
            float v = fs_sdf(&p->s[act[k]], qx, qy);
            if (v < dq) dq = v;
          }
          fill = fa_mix(dk, lit, fa_sat(0.5f - dq));
        }
        col = p->ow > 0.0f ? fa_mix(p->line, fill, aF / aO) : fill;
      }
      fv_over(row + x * 4, col, (p->ow > 0.0f ? aO : aF) * p->alpha);
    }
  }
}

/* ---- простые мазки без обводки ---- */

static void fv_line(FCv *c, float x0, float y0, float x1, float y1, float r0, float r1, FCol col, float a) {
  FPart p;
  fp_init(&p, col, col, col, 0, 0);
  p.alpha = a;
  fs_cap(fp_add(&p), x0, y0, x1, y1, r0, r1);
  fp_draw(c, &p);
}

static void fv_disc(FCv *c, float x, float y, float r, FCol col, float a) {
  fv_line(c, x, y, x, y, r, r, col, a);
}

/* кольцо толщиной w (по середине — радиус r), с мягкими краями */
static void fv_ring(FCv *c, float cx, float cy, float r, float w, FCol col, float a) {
  int X0, Y0, X1, Y1;
  float R = r + w * 0.5f + 1.0f;
  if (w < 0.2f || a <= 0.003f || !fv_clip(c, cx - R, cy - R, cx + R, cy + R, &X0, &Y0, &X1, &Y1)) return;
  float hw = w * 0.5f, ri = r - hw - 1.0f;
  float ri2 = ri > 0.0f ? ri * ri : 0.0f;
  for (int y = Y0; y < Y1; y++) {
    float dy = y + 0.5f - cy;
    unsigned char *row = c->px + (size_t)y * c->w * 4;
    for (int x = X0; x < X1; x++) {
      float dx = x + 0.5f - cx, q = dx * dx + dy * dy;
      if (q < ri2 || q > R * R) continue;
      float d = fabsf(sqrtf(q) - r);
      fv_over(row + x * 4, col, a * fa_sat(hw + 0.5f - d));
    }
  }
}

/* ===================== фигуры ===================== */

/* поза: углы в градусах. Руки и ноги — от «вниз», плюс — вперёд (к противнику);
   [0] — дальняя, ведущая, [1] — ближняя к зрителю. Корпус и голова — от
   вертикали, плюс — вперёд. roll крутит всю фигуру вокруг таза */
enum {
  PV_DX, PV_DY, PV_ROLL, PV_BODY, PV_HEAD,
  PV_UA0, PV_LA0, PV_UA1, PV_LA1,
  PV_UL0, PV_LL0, PV_UL1, PV_LL1,
  PV_FT0, PV_FT1,
  PV_N
};
typedef struct {
  float v[PV_N];
  unsigned char h0, h1, face, flags;
} FPose;

/* кисти */
enum { FH_FIST, FH_OPEN, FH_POINT, FH_TWO, FH_CLAW, FH_POCKET, FH_SEALG, FH_SEALS, FH_PEACE, FH_FLICK };
/* лица */
enum { FE_SMIRK, FE_GRIN, FE_SHOUT, FE_HURT, FE_STUN, FE_CALM, FE_LAUGH };
/* флаги позы */
#define FPF_FREE 1     /* таз на высоте dy, ноги землю не ищут (лежит, кувырок) */
#define FPF_FARFRONT 2 /* дальняя рука — перед корпусом (печати, руки вместе) */

/* длины, в единицах роста (вся фигура — около 100) */
#define FL_THIGH 22.0f
#define FL_SHIN 21.0f
#define FL_ANKLE 3.0f
#define FL_TORSO 29.0f
#define FL_UARM 15.0f
#define FL_FARM 13.5f

static float fa_leg_h(float ul, float ll) {
  return FL_THIGH * cosf(ul * FA_RAD) + FL_SHIN * cosf(ll * FA_RAD) + FL_ANKLE;
}
static float fa_pose_hip(const FPose *p) {
  if (p->flags & FPF_FREE) return p->v[PV_DY];
  float a = fa_leg_h(p->v[PV_UL0], p->v[PV_LL0]), b = fa_leg_h(p->v[PV_UL1], p->v[PV_LL1]);
  return (a > b ? a : b) + p->v[PV_DY];
}

/* поза между a и b: t от 0 до 1. Высота таза считается у обеих и смешивается,
   поэтому у смеси она всегда задана явно */
static void fa_tween(FPose *o, const FPose *a, const FPose *b, float t) {
  float ha = fa_pose_hip(a), hb = fa_pose_hip(b);
  for (int i = 0; i < PV_N; i++) o->v[i] = a->v[i] + (b->v[i] - a->v[i]) * t;
  const FPose *n = t < 0.5f ? a : b;
  o->h0 = n->h0, o->h1 = n->h1, o->face = n->face;
  o->flags = (unsigned char)((n->flags & ~FPF_FREE) | FPF_FREE);
  o->v[PV_DY] = ha + (hb - ha) * t;
}

/* скелет в «единицах позы»: x — вперёд со знаком взгляда f, y — вверх от земли */
typedef struct {
  int f;
  float hip[2], neck[2], head[2];
  float bodyA, headA;
  float bu[2], bf[2], hu[2], hf[2]; /* оси корпуса и головы: вверх и вперёд */
  float sh[2][2], el[2][2], wr[2][2], hp[2][2], kn[2][2], an[2][2];
  float la[2], ll[2]; /* углы предплечий и голеней, радианы */
  float pivot[2], rc, rs; /* поворот всей фигуры */
} FSkel;

static void fa_dir(int f, float a, float *x, float *y) { /* конечность под углом a от «вниз» */
  *x = f * sinf(a);
  *y = -cosf(a);
}

static void fa_skel(FSkel *k, const FPose *p, int f) {
  const float *v = p->v;
  k->f = f;
  k->hip[0] = v[PV_DX] * f;
  k->hip[1] = fa_pose_hip(p);
  k->bodyA = v[PV_BODY] * FA_RAD;
  k->headA = (v[PV_BODY] + v[PV_HEAD]) * FA_RAD;
  k->bu[0] = f * sinf(k->bodyA), k->bu[1] = cosf(k->bodyA);
  k->bf[0] = f * cosf(k->bodyA), k->bf[1] = -sinf(k->bodyA);
  k->hu[0] = f * sinf(k->headA), k->hu[1] = cosf(k->headA);
  k->hf[0] = f * cosf(k->headA), k->hf[1] = -sinf(k->headA);
  k->neck[0] = k->hip[0] + k->bu[0] * FL_TORSO, k->neck[1] = k->hip[1] + k->bu[1] * FL_TORSO;
  k->head[0] = k->neck[0] + k->hu[0] * 9.6f, k->head[1] = k->neck[1] + k->hu[1] * 9.6f;
  /* плечи: в три четверти дальнее чуть впереди, ближнее — сзади */
  float so[2][2] = {{2.7f, -2.9f}, {-3.1f, -3.2f}};
  for (int i = 0; i < 2; i++) {
    k->sh[i][0] = k->neck[0] + k->bf[0] * so[i][0] + k->bu[0] * so[i][1];
    k->sh[i][1] = k->neck[1] + k->bf[1] * so[i][0] + k->bu[1] * so[i][1];
    float ua = v[i ? PV_UA1 : PV_UA0] * FA_RAD, la = v[i ? PV_LA1 : PV_LA0] * FA_RAD, dx, dy;
    fa_dir(f, ua, &dx, &dy);
    k->el[i][0] = k->sh[i][0] + dx * FL_UARM, k->el[i][1] = k->sh[i][1] + dy * FL_UARM;
    fa_dir(f, la, &dx, &dy);
    k->wr[i][0] = k->el[i][0] + dx * FL_FARM, k->wr[i][1] = k->el[i][1] + dy * FL_FARM;
    k->la[i] = la;
    k->hp[i][0] = k->hip[0] + f * (i ? -1.7f : 1.7f), k->hp[i][1] = k->hip[1];
    float ul = v[i ? PV_UL1 : PV_UL0] * FA_RAD, ll = v[i ? PV_LL1 : PV_LL0] * FA_RAD;
    fa_dir(f, ul, &dx, &dy);
    k->kn[i][0] = k->hp[i][0] + dx * FL_THIGH, k->kn[i][1] = k->hp[i][1] + dy * FL_THIGH;
    fa_dir(f, ll, &dx, &dy);
    k->an[i][0] = k->kn[i][0] + dx * FL_SHIN, k->an[i][1] = k->kn[i][1] + dy * FL_SHIN;
    k->ll[i] = ll;
  }
  float r = -v[PV_ROLL] * f * FA_RAD;
  k->pivot[0] = k->hip[0], k->pivot[1] = k->hip[1];
  k->rc = cosf(r), k->rs = sinf(r);
}

/* точка позы → после поворота всей фигуры (всё ещё в единицах) */
static void fa_rot(const FSkel *k, float x, float y, float *ox, float *oy) {
  float dx = x - k->pivot[0], dy = y - k->pivot[1];
  *ox = k->pivot[0] + dx * k->rc - dy * k->rs;
  *oy = k->pivot[1] + dx * k->rs + dy * k->rc;
}

/* суставы для боя: где кулак, стопа, голова — в единицах мира от опорной точки */
enum { FJ_HAND0, FJ_HAND1, FJ_FOOT0, FJ_FOOT1, FJ_HEAD, FJ_CHEST, FJ_HIP, FJ_KNEE1 };
static void fa_joint(const FSkel *k, float S, int j, float *x, float *y) {
  float px, py;
  switch (j) {
  case FJ_HAND0: case FJ_HAND1: {
    int i = j - FJ_HAND0;
    float dx, dy;
    fa_dir(k->f, k->la[i], &dx, &dy);
    px = k->wr[i][0] + dx * 3.0f, py = k->wr[i][1] + dy * 3.0f;
    break;
  }
  case FJ_FOOT0: case FJ_FOOT1: {
    int i = j - FJ_FOOT0;
    px = k->an[i][0], py = k->an[i][1];
    break;
  }
  case FJ_HEAD: px = k->head[0], py = k->head[1]; break;
  case FJ_KNEE1: px = k->kn[1][0], py = k->kn[1][1]; break;
  case FJ_CHEST: px = (k->neck[0] * 2 + k->hip[0]) / 3, py = (k->neck[1] * 2 + k->hip[1]) / 3; break;
  default: px = k->hip[0], py = k->hip[1]; break;
  }
  fa_rot(k, px, py, x, y);
  *x *= S, *y *= S;
}

/* внешний вид */
typedef struct {
  float S; /* рост: Годжо выше */
  FCol line, skin, skinD;
  FCol hair, hair2, hairD, hairD2;
  FCol top, top2, topD, topD2; /* верх: форма Годжо, кимоно Сукуны */
  FCol bot, botD;              /* низ: брюки, хакама */
  FCol shoe, shoeD;
  FCol eye, eyeD, accent, accentD;
  float thigh, knee, ankle, arm, elbow, wrist;
} FLook;

static const FLook kLookGojo = {
    1.06f, {26, 22, 40}, {252, 224, 204}, {224, 180, 160},
    {255, 255, 255}, {214, 224, 248}, {176, 188, 226}, {150, 162, 210},
    {62, 72, 124}, {44, 52, 96}, {30, 35, 68}, {22, 25, 50},
    {50, 57, 104}, {27, 31, 60},
    {30, 28, 38}, {16, 14, 22},
    {70, 185, 255}, {20, 90, 200}, {120, 132, 182}, {84, 94, 140},
    4.9f, 3.9f, 2.8f, 3.7f, 3.3f, 2.6f};
static const FLook kLookSukuna = {
    1.0f, {32, 12, 20}, {250, 218, 192}, {216, 168, 144},
    {255, 196, 218}, {238, 120, 162}, {216, 104, 146}, {184, 70, 112},
    {96, 72, 100}, {72, 54, 78}, {56, 40, 62}, {42, 30, 48},
    {62, 54, 72}, {38, 32, 46},
    {40, 32, 44}, {22, 16, 26},
    {235, 25, 35}, {150, 10, 20}, {242, 236, 226}, {204, 194, 178},
    6.1f, 5.4f, 4.4f, 3.7f, 3.5f, 2.15f};

typedef struct {
  int who;        /* 0 — Годжо, 1 — Сукуна */
  int f;          /* +1 — смотрит вправо */
  float x, y;     /* экран: точка на земле под тазом (y — земля минус высота прыжка) */
  float U;        /* px на единицу (рост около 100 единиц) */
  FPose p;
  int blind;      /* Годжо: повязка на глазах */
  int power;      /* Сукуна: нижние глаза открыты */
  float wind;     /* волосы сдувает назад: 0…1 */
  float flash;    /* белеет от удара: 0…1 */
  float alpha;    /* прозрачность (телепорт, послеобраз) */
  FCol tint;
  float tintA;    /* окраска послеобраза */
} FFig;

typedef struct {
  FCv *cv;
  const FLook *L;
  const FFig *g;
  FSkel k;
  float U;      /* px на единицу позы (с ростом персонажа) */
  float ow;     /* толщина обводки */
  float rs;     /* множитель толщин: голова и кисти чуть крупнее тела */
} FCtx;

/* единицы позы → экран */
static void fx_scr(const FCtx *c, float x, float y, float *X, float *Y) {
  float rx, ry;
  fa_rot(&c->k, x, y, &rx, &ry);
  *X = c->g->x + rx * c->U;
  *Y = c->g->y - ry * c->U;
}
static FCol fx_col(const FCtx *c, FCol a) {
  if (c->g->flash > 0.0f) a = fa_mix(a, FC(255, 255, 255), c->g->flash * 0.8f);
  if (c->g->tintA > 0.0f) a = fa_mix(a, c->g->tint, c->g->tintA);
  return a;
}
/* обводка от удара не белеет: фигура вспыхивает, но остаётся собой */
static FCol fx_line(const FCtx *c) {
  return c->g->tintA > 0.0f ? fa_mix(c->L->line, c->g->tint, c->g->tintA) : c->L->line;
}
static void fx_part(const FCtx *c, FPart *p, FCol lit, FCol dark, float sh) {
  fp_init(p, fx_col(c, lit), fx_col(c, dark), fx_line(c), c->ow, sh * c->U);
  p->alpha = c->g->alpha;
}
static void fx_grad(const FCtx *c, FPart *p, FCol lit2, FCol dark2, float y0, float y1) {
  float X, Y0, Y1;
  fx_scr(c, 0, y0, &X, &Y0);
  fx_scr(c, 0, y1, &X, &Y1);
  p->grad = 1;
  p->lit2 = fx_col(c, lit2);
  p->dark2 = fx_col(c, dark2);
  p->gy0 = Y0, p->gy1 = Y1;
}
static void fx_cap(const FCtx *c, FPart *p, float ax, float ay, float bx, float by, float ra, float rb) {
  FShape *s = fp_add(p);
  if (!s) return;
  float X0, Y0, X1, Y1;
  fx_scr(c, ax, ay, &X0, &Y0);
  fx_scr(c, bx, by, &X1, &Y1);
  fs_cap(s, X0, Y0, X1, Y1, ra * c->U * c->rs, rb * c->U * c->rs);
}
/* эллипс с осью rx вдоль (ux,uy) — направление в единицах позы */
static void fx_ell(const FCtx *c, FPart *p, float cx, float cy, float rx, float ry, float ux, float uy) {
  FShape *s = fp_add(p);
  if (!s) return;
  float X, Y, X1, Y1;
  fx_scr(c, cx, cy, &X, &Y);
  fx_scr(c, cx + ux, cy + uy, &X1, &Y1);
  fs_ell(s, X, Y, rx * c->U * c->rs, ry * c->U * c->rs, X1 - X, Y1 - Y);
}
/* многоугольник в своей системе: начало o, оси fw (вперёд) и up (вверх) */
static void fx_poly(const FCtx *c, FPart *p, const float *loc, int n, const float *o, const float *fw, const float *up) {
  FShape *s = fp_add(p);
  if (!s) return;
  float xy[FS_PTS * 2];
  if (n > FS_PTS) n = FS_PTS;
  for (int i = 0; i < n; i++) {
    float lx = loc[i * 2], ly = loc[i * 2 + 1];
    fx_scr(c, o[0] + fw[0] * lx + up[0] * ly, o[1] + fw[1] * lx + up[1] * ly, &xy[i * 2], &xy[i * 2 + 1]);
  }
  fs_poly(s, xy, n);
}
/* точка своей системы → единицы позы */
static void fx_loc(const float *o, const float *fw, const float *up, float lx, float ly, float *x, float *y) {
  *x = o[0] + fw[0] * lx + up[0] * ly;
  *y = o[1] + fw[1] * lx + up[1] * ly;
}
/* тонкая линия-деталь по точкам своей системы, без обводки */
static void fx_stroke(const FCtx *c, const float *o, const float *fw, const float *up, float ax, float ay, float bx,
                      float by, float r, FCol col) {
  float x0, y0, x1, y1, X0, Y0, X1, Y1;
  fx_loc(o, fw, up, ax, ay, &x0, &y0);
  fx_loc(o, fw, up, bx, by, &x1, &y1);
  fx_scr(c, x0, y0, &X0, &Y0);
  fx_scr(c, x1, y1, &X1, &Y1);
  float R = r * c->U * c->rs;
  if (R < 0.55f) R = 0.55f; /* тоньше пикселя линия не видна вовсе */
  fv_line(c->cv, X0, Y0, X1, Y1, R, R, fx_col(c, col), c->g->alpha);
}
static void fx_dot(const FCtx *c, const float *o, const float *fw, const float *up, float x, float y, float r, FCol col) {
  fx_stroke(c, o, fw, up, x, y, x, y, r, col);
}

/* ---- кисти ---- */
static void ff_hand(const FCtx *c, int i, int type, float dim) {
  const FSkel *k = &c->k;
  const FLook *L = c->L;
  if (type == FH_POCKET) return; /* рука в кармане */
  float ax, ay;
  fa_dir(k->f, k->la[i], &ax, &ay);
  /* вторая ось кисти — к большому пальцу: по возможности вверх */
  float nx = -ay, ny = ax;
  if (ny < 0.0f || (fabsf(ny) < 0.2f && nx * k->f < 0.0f)) nx = -nx, ny = -ny;
  const float HS = 1.15f;
  float o[2] = {k->wr[i][0], k->wr[i][1]}, fw[2] = {ax * HS, ay * HS}, up[2] = {nx * HS, ny * HS};
  FCtx hc = *c;
  hc.rs = c->rs * HS;
  c = &hc;
  FCol sk = fa_dim(L->skin, dim), skD = fa_dim(L->skinD, dim);
  FCol nail = c->g->who ? FC(28, 14, 22) : sk;
  FPart p;
  fx_part(c, &p, sk, skD, 0.9f);
  float x0, y0, x1, y1;
#define FH_SEG(ax_, ay_, bx_, by_, r_)                                                                                 \
  do {                                                                                                                 \
    fx_loc(o, fw, up, ax_, ay_, &x0, &y0);                                                                             \
    fx_loc(o, fw, up, bx_, by_, &x1, &y1);                                                                             \
    fx_cap(c, &p, x0, y0, x1, y1, r_, r_);                                                                             \
  } while (0)
#define FH_ELL(cx_, cy_, rx_, ry_)                                                                                     \
  do {                                                                                                                 \
    fx_loc(o, fw, up, cx_, cy_, &x0, &y0);                                                                             \
    fx_ell(c, &p, x0, y0, rx_, ry_, fw[0], fw[1]);                                                                     \
  } while (0)
  float tips[5][2];
  int nt = 0;
  switch (type) {
  case FH_OPEN:
  case FH_SEALS:
    FH_ELL(2.2f, 0.0f, 2.3f, 2.0f);
    if (type == FH_SEALS) {
      FH_SEG(3.4f, 0.3f, 7.2f, 0.6f, 1.25f); /* ладонь к ладони, пальцы вместе */
      tips[nt][0] = 7.6f, tips[nt][1] = 0.6f, nt++;
    } else {
      static const float fv[4] = {-1.35f, -0.45f, 0.45f, 1.3f};
      for (int j = 0; j < 4; j++) {
        float L2 = j == 0 ? 5.9f : 6.8f;
        FH_SEG(3.3f, fv[j] * 0.9f, L2, fv[j] * 1.35f, 0.6f);
        tips[nt][0] = L2, tips[nt][1] = fv[j] * 1.35f, nt++;
      }
    }
    FH_SEG(1.2f, 1.5f, 3.2f, 3.0f, 0.7f);
    break;
  case FH_CLAW: {
    FH_ELL(2.2f, 0.0f, 2.3f, 2.1f);
    static const float fv[4] = {-1.35f, -0.45f, 0.45f, 1.3f};
    for (int j = 0; j < 4; j++) {
      FH_SEG(3.2f, fv[j] * 0.9f, 5.4f, fv[j] * 1.3f, 0.62f);
      FH_SEG(5.4f, fv[j] * 1.3f, 6.6f, fv[j] * 1.3f - 1.0f, 0.55f);
      tips[nt][0] = 6.9f, tips[nt][1] = fv[j] * 1.3f - 1.3f, nt++;
    }
    FH_SEG(1.2f, 1.6f, 3.0f, 3.0f, 0.7f);
    break;
  }
  case FH_POINT:
  case FH_TWO:
  case FH_PEACE:
  case FH_SEALG:
  case FH_FLICK:
    FH_ELL(2.3f, -0.2f, 2.35f, 2.0f);
    if (type == FH_POINT) {
      FH_SEG(3.4f, 1.0f, 8.2f, 1.2f, 0.66f);
      tips[nt][0] = 8.5f, tips[nt][1] = 1.2f, nt++;
    } else if (type == FH_TWO) {
      FH_SEG(3.4f, 1.15f, 8.2f, 1.25f, 0.64f);
      FH_SEG(3.4f, 0.0f, 8.4f, 0.0f, 0.64f);
      tips[nt][0] = 8.5f, tips[nt][1] = 1.25f, nt++;
      tips[nt][0] = 8.7f, tips[nt][1] = 0.0f, nt++;
    } else if (type == FH_PEACE) {
      FH_SEG(3.4f, 0.9f, 7.9f, 2.6f, 0.62f);
      FH_SEG(3.4f, -0.3f, 8.1f, -1.4f, 0.62f);
      tips[nt][0] = 8.2f, tips[nt][1] = 2.7f, nt++;
      tips[nt][0] = 8.4f, tips[nt][1] = -1.5f, nt++;
    } else if (type == FH_SEALG) { /* указательный и средний скрещены */
      FH_SEG(3.4f, 0.9f, 8.3f, -0.5f, 0.62f);
      FH_SEG(3.4f, -0.4f, 8.1f, 1.0f, 0.62f);
      tips[nt][0] = 8.5f, tips[nt][1] = -0.6f, nt++;
      tips[nt][0] = 8.3f, tips[nt][1] = 1.1f, nt++;
    } else { /* щелчок: палец сорвался с большого */
      FH_SEG(3.2f, 1.1f, 6.4f, 2.4f, 0.62f);
      tips[nt][0] = 6.6f, tips[nt][1] = 2.5f, nt++;
    }
    FH_SEG(1.3f, 1.4f, 3.1f, 2.1f, 0.72f);
    break;
  default: /* кулак */
    FH_ELL(2.4f, 0.0f, 2.55f, 2.25f);
    FH_SEG(1.4f, 1.5f, 3.0f, 1.9f, 0.75f);
    break;
  }
#undef FH_SEG
#undef FH_ELL
  fp_draw(c->cv, &p);
  if (type == FH_FIST) fx_stroke(c, o, fw, up, 3.9f, -1.2f, 3.9f, 1.0f, 0.18f, fa_dim(skD, 0.85f));
  if (c->g->who) /* чёрные ногти Сукуны */
    for (int j = 0; j < nt; j++) fx_dot(c, o, fw, up, tips[j][0] - 0.35f, tips[j][1], 0.42f, nail);
}

/* ---- руки ---- */
static void ff_arm(const FCtx *c, int i) {
  const FSkel *k = &c->k;
  const FLook *L = c->L;
  const FPose *ps = &c->g->p;
  float dim = i ? 1.0f : 0.8f;
  int hand = i ? ps->h1 : ps->h0;
  FPart p;
  if (c->g->who == 0) {
    /* рукав формы целиком до запястья */
    fx_part(c, &p, fa_dim(L->top, dim), fa_dim(L->topD, dim), 1.3f);
    fx_cap(c, &p, k->sh[i][0], k->sh[i][1], k->el[i][0], k->el[i][1], L->arm, L->elbow);
    fx_cap(c, &p, k->el[i][0], k->el[i][1], k->wr[i][0], k->wr[i][1], L->elbow, L->wrist + 0.2f);
    fp_draw(c->cv, &p);
    if (hand != FH_POCKET) { /* манжета */
      float dx, dy;
      fa_dir(k->f, k->la[i], &dx, &dy);
      float cx = k->wr[i][0] - dx * 1.2f, cy = k->wr[i][1] - dy * 1.2f;
      FPart q;
      fx_part(c, &q, fa_dim(L->accent, dim), fa_dim(L->accentD, dim), 0.0f);
      q.ow = 0;
      fx_cap(c, &q, cx, cy, cx - dx * 0.5f, cy - dy * 0.5f, L->wrist + 0.15f, L->wrist + 0.15f);
      fp_draw(c->cv, &q);
    }
    ff_hand(c, i, hand, dim);
    return;
  }
  /* Сукуна: голое предплечье с чёрными метками-браслетами и широкий рукав
     кимоно до середины предплечья */
  float dx, dy;
  fa_dir(k->f, k->la[i], &dx, &dy);
  fx_part(c, &p, fa_dim(L->skin, dim), fa_dim(L->skinD, dim), 0.9f);
  fx_cap(c, &p, k->el[i][0], k->el[i][1], k->wr[i][0], k->wr[i][1], 2.25f, L->wrist);
  fp_draw(c->cv, &p);
  for (int b = 0; b < 2; b++) { /* метки на запястье */
    float t = 9.6f + b * 1.6f;
    float cx = k->el[i][0] + dx * t, cy = k->el[i][1] + dy * t;
    FPart q;
    fx_part(c, &q, L->line, L->line, 0.0f);
    q.ow = 0;
    fx_cap(c, &q, cx, cy, cx + dx * 0.55f, cy + dy * 0.55f, L->wrist + 0.2f, L->wrist + 0.2f);
    fp_draw(c->cv, &q);
  }
  ff_hand(c, i, hand, dim);
  /* рукав кимоно: от плеча широкий, к середине предплечья — раструбом */
  fx_part(c, &p, fa_dim(L->top, dim), fa_dim(L->topD, dim), 1.5f);
  fx_grad(c, &p, fa_dim(L->top2, dim), fa_dim(L->topD2, dim), k->sh[i][1], k->el[i][1] - 6.0f);
  float ex = k->el[i][0] + dx * 5.4f, ey = k->el[i][1] + dy * 5.4f;
  fx_cap(c, &p, k->sh[i][0], k->sh[i][1], k->el[i][0], k->el[i][1], L->arm, L->elbow);
  fx_cap(c, &p, k->el[i][0], k->el[i][1], ex, ey, L->elbow, L->elbow + 0.9f);
  fp_draw(c->cv, &p);
}

/* ---- ноги ---- */
static void ff_leg(const FCtx *c, int i) {
  const FSkel *k = &c->k;
  const FLook *L = c->L;
  const FPose *ps = &c->g->p;
  float dim = i ? 1.0f : 0.8f;
  FPart p;
  fx_part(c, &p, fa_dim(L->bot, dim), fa_dim(L->botD, dim), 1.4f);
  fx_cap(c, &p, k->hp[i][0], k->hp[i][1], k->kn[i][0], k->kn[i][1], L->thigh, L->knee);
  fx_cap(c, &p, k->kn[i][0], k->kn[i][1], k->an[i][0], k->an[i][1], L->knee, L->ankle);
  fp_draw(c->cv, &p);
  /* стопа: поперёк голени, носком вперёд; ft — поворот носка вверх */
  float sx, sy;
  fa_dir(k->f, k->ll[i], &sx, &sy); /* вдоль голени вниз */
  float fa = k->ll[i] + (90.0f + (i ? ps->v[PV_FT1] : ps->v[PV_FT0])) * FA_RAD;
  float fx_, fy_;
  fa_dir(k->f, fa, &fx_, &fy_);
  float up[2] = {-sx, -sy}, fw[2] = {fx_, fy_};
  float o[2] = {k->an[i][0] + sx * 1.6f, k->an[i][1] + sy * 1.6f};
  FPart q;
  fx_part(c, &q, fa_dim(L->shoe, dim), fa_dim(L->shoeD, dim), 0.8f);
  static const float kShoe[] = {-2.3f, 2.4f, -2.6f, -0.2f, 5.4f, -0.4f, 7.0f, 0.6f, 6.0f, 1.8f, 1.8f, 3.2f};
  fx_poly(c, &q, kShoe, 6, o, fw, up);
  fp_draw(c->cv, &q);
  if (c->g->who) { /* сандалии Сукуны: светлая подошва */
    fx_stroke(c, o, fw, up, -2.2f, -0.35f, 5.6f, -0.55f, 0.32f, fa_dim(L->accentD, dim));
    /* складки хакамы */
    float mx = (k->hp[i][0] + k->kn[i][0]) * 0.5f, my = (k->hp[i][1] + k->kn[i][1]) * 0.5f;
    float zero[2] = {0, 0}, ex[2] = {1, 0}, ey[2] = {0, 1};
    fx_stroke(c, zero, ex, ey, mx, my, (mx + k->an[i][0]) * 0.5f, (my + k->an[i][1]) * 0.5f, 0.28f,
              fa_dim(L->botD, dim * 0.75f));
  }
}

/* ---- корпус ---- */
static void ff_torso(const FCtx *c) {
  const FSkel *k = &c->k;
  const FLook *L = c->L;
  float o[2] = {k->hip[0], k->hip[1]};
  FPart p;
  if (c->g->who == 0) {
    static const float kJacket[] = {-7.0f, -5.0f, 7.4f, -5.4f, 6.6f, 4.0f, 8.0f, 16.0f, 7.6f, 23.6f, 5.4f, 28.6f,
                                    2.6f, 30.3f, -2.8f, 30.3f, -6.2f, 28.4f, -7.6f, 22.6f, -6.4f, 9.0f, -6.9f, 0.0f};
    fx_part(c, &p, L->top, L->topD, 2.0f);
    fx_grad(c, &p, L->top2, L->topD2, k->hip[1] + 26.0f, k->hip[1] - 4.0f);
    fx_poly(c, &p, kJacket, 12, o, k->bf, k->bu);
    fp_draw(c->cv, &p);
    /* молния по середине и карман */
    fx_stroke(c, o, k->bf, k->bu, 3.1f, 28.6f, 3.8f, -4.6f, 0.32f, L->accent);
    fx_stroke(c, o, k->bf, k->bu, 4.4f, 6.2f, 1.2f, 3.2f, 0.26f, L->topD2);
    return;
  }
  /* Сукуна: кимоно, вырез, белая кайма, пояс; ниже — верх хакамы */
  static const float kHakama[] = {-7.4f, 7.0f, 7.4f, 7.0f, 8.6f, -7.0f, -8.4f, -7.0f};
  fx_part(c, &p, L->bot, L->botD, 2.0f);
  fx_poly(c, &p, kHakama, 4, o, k->bf, k->bu);
  fp_draw(c->cv, &p);
  static const float kKimono[] = {-7.3f, 1.0f, 7.3f, 1.0f, 7.0f, 7.0f, 8.4f, 17.0f, 6.4f, 28.4f, 2.6f, 30.2f,
                                  -3.0f, 30.2f, -7.4f, 28.0f, -8.0f, 20.0f, -7.0f, 7.0f};
  fx_part(c, &p, L->top, L->topD, 2.0f);
  fx_grad(c, &p, L->top2, L->topD2, k->hip[1] + 26.0f, k->hip[1] + 2.0f);
  fx_poly(c, &p, kKimono, 10, o, k->bf, k->bu);
  fp_draw(c->cv, &p);
  static const float kChest[] = {-0.6f, 30.3f, 5.2f, 30.0f, 2.6f, 11.0f};
  fx_part(c, &p, L->skin, L->skinD, 1.2f);
  p.ow = 0;
  fx_poly(c, &p, kChest, 3, o, k->bf, k->bu);
  fp_draw(c->cv, &p);
  fx_stroke(c, o, k->bf, k->bu, -0.6f, 30.2f, 2.6f, 10.6f, 0.7f, L->accent); /* кайма */
  fx_stroke(c, o, k->bf, k->bu, 5.2f, 29.9f, 2.6f, 10.6f, 0.7f, L->accentD);
  fx_stroke(c, o, k->bf, k->bu, 1.6f, 22.0f, 2.2f, 17.0f, 0.22f, L->skinD); /* грудь */
  static const float kObi[] = {-7.3f, 1.4f, 7.3f, 1.4f, 7.5f, 6.6f, -7.4f, 6.6f};
  fx_part(c, &p, L->accent, L->accentD, 0.9f);
  fx_poly(c, &p, kObi, 4, o, k->bf, k->bu);
  fp_draw(c->cv, &p);
  fx_stroke(c, o, k->bf, k->bu, 2.0f, 4.0f, 0.6f, -1.6f, 0.45f, FC(70, 46, 58)); /* узел пояса */
  fx_stroke(c, o, k->bf, k->bu, 2.0f, 4.0f, 3.6f, -1.3f, 0.45f, FC(70, 46, 58));
}

/* ---- голова ---- */
static void ff_mouth(const FCtx *c, const float *o, const float *fw, const float *up, int face) {
  const FLook *L = c->L;
  int suk = c->g->who;
  FCol lip = suk ? FC(120, 30, 40) : FC(165, 85, 85);
  if (face == FE_SHOUT || face == FE_LAUGH) {
    static const float kOpen[] = {2.6f, -3.8f, 6.2f, -3.4f, 5.8f, -5.6f, 3.8f, -6.4f};
    FPart p;
    fx_part(c, &p, FC(110, 24, 36), FC(80, 16, 26), 0.0f);
    p.ow = c->ow * 0.7f;
    fx_poly(c, &p, kOpen, 4, o, fw, up);
    fp_draw(c->cv, &p);
    fx_stroke(c, o, fw, up, 3.0f, -3.95f, 5.8f, -3.6f, 0.36f, FC(250, 248, 244)); /* зубы */
    if (suk) fx_stroke(c, o, fw, up, 3.4f, -4.0f, 3.6f, -4.9f, 0.22f, FC(250, 248, 244));
    return;
  }
  if (face == FE_HURT) {
    static const float kTeeth[] = {2.9f, -3.9f, 6.0f, -3.7f, 5.9f, -5.0f, 3.0f, -5.1f};
    FPart p;
    fx_part(c, &p, FC(250, 248, 244), FC(220, 214, 210), 0.0f);
    p.ow = c->ow * 0.7f;
    fx_poly(c, &p, kTeeth, 4, o, fw, up);
    fp_draw(c->cv, &p);
    fx_stroke(c, o, fw, up, 3.0f, -4.45f, 5.9f, -4.3f, 0.14f, L->line);
    return;
  }
  if (face == FE_STUN) {
    fx_dot(c, o, fw, up, 4.6f, -4.6f, 0.75f, FC(90, 20, 30));
    return;
  }
  if (face == FE_CALM) {
    fx_stroke(c, o, fw, up, 3.6f, -4.5f, 5.8f, -4.4f, 0.26f, lip);
    return;
  }
  /* ухмылка: у Сукуны — злая, с клыком */
  if (suk) {
    float w = face == FE_GRIN ? 1.0f : 0.0f;
    fx_stroke(c, o, fw, up, 2.2f, -4.3f - 0.3f * w, 4.2f, -4.85f - 0.3f * w, 0.3f, lip);
    fx_stroke(c, o, fw, up, 4.2f, -4.85f - 0.3f * w, 6.3f, -3.7f, 0.3f, lip);
    fx_stroke(c, o, fw, up, 4.6f, -4.75f, 4.85f, -5.55f, 0.24f, FC(250, 248, 244));
    return;
  }
  fx_stroke(c, o, fw, up, 3.5f, -4.6f, 5.0f, -4.75f, 0.27f, lip);
  fx_stroke(c, o, fw, up, 5.0f, -4.75f, 6.1f, -3.95f, 0.27f, lip);
}

/* глаз: белок с тёмной каймой, радужка, блик. tilt — наклон (злой — к носу вниз) */
static void ff_eye(const FCtx *c, const float *o, const float *fw, const float *up, float x, float y, float rx, float ry,
                   float tilt, float iris, FCol ic, FCol icD, int slit) {
  float ca = cosf(tilt * FA_RAD), sa = sinf(tilt * FA_RAD);
  float ax[2] = {fw[0] * ca + up[0] * sa, fw[1] * ca + up[1] * sa};
  float cx, cy;
  fx_loc(o, fw, up, x, y, &cx, &cy);
  FPart p;
  fx_part(c, &p, FC(255, 255, 255), FC(226, 228, 236), 0.0f);
  p.ow = c->ow * 0.8f;
  fx_ell(c, &p, cx, cy, rx, ry, ax[0], ax[1]);
  fp_draw(c->cv, &p);
  float ix, iy;
  fx_loc(o, fw, up, x + rx * 0.18f, y - ry * 0.05f, &ix, &iy);
  fx_part(c, &p, ic, icD, iris * 0.45f);
  p.ow = 0;
  fx_ell(c, &p, ix, iy, iris * 0.9f, fminf(iris, ry * 0.98f), ax[0], ax[1]);
  fp_draw(c->cv, &p);
  float zero[2] = {0, 0}, ex[2] = {1, 0}, ey[2] = {0, 1};
  if (slit) fx_stroke(c, zero, ex, ey, ix, iy + iris * 0.45f, ix, iy - iris * 0.45f, iris * 0.16f, FC(40, 6, 12));
  /* верхнее веко */
  float lx0, ly0, lx1, ly1;
  fx_loc(o, fw, up, x - rx * ca * 1.05f, y - rx * sa * 1.05f + ry * 0.35f, &lx0, &ly0);
  fx_loc(o, fw, up, x + rx * ca * 1.05f, y + rx * sa * 1.05f + ry * 0.45f, &lx1, &ly1);
  fx_stroke(c, zero, ex, ey, lx0, ly0, lx1, ly1, 0.3f, c->L->line);
  fx_stroke(c, zero, ex, ey, ix - 0.2f * fw[0], iy + iris * 0.35f, ix - 0.2f * fw[0], iy + iris * 0.35f, iris * 0.28f,
            FC(255, 255, 255));
}

static void ff_head(const FCtx *c) {
  const FSkel *k = &c->k;
  const FLook *L = c->L;
  const FFig *g = c->g;
  int face = g->p.face, suk = g->who;
  float o[2] = {k->head[0], k->head[1]};
  const float HS = 1.1f; /* голова чуть крупнее тела: лицо читается и в бою */
  float fw[2] = {k->hf[0] * HS, k->hf[1] * HS}, up[2] = {k->hu[0] * HS, k->hu[1] * HS};
  FPart p;
  /* шея */
  fx_part(c, &p, L->skin, L->skinD, 1.0f);
  fx_cap(c, &p, k->neck[0] - k->bu[0] * 1.5f, k->neck[1] - k->bu[1] * 1.5f, o[0] - k->hu[0] * 5.0f,
         o[1] - k->hu[1] * 5.0f, 2.6f, 2.4f);
  fp_draw(c->cv, &p);
  if (!suk) { /* высокий ворот */
    static const float kCollar[] = {-3.9f, 26.6f, 4.2f, 26.6f, 4.4f, 33.8f, 3.0f, 34.4f, -3.2f, 34.4f, -4.2f, 33.2f};
    float to[2] = {k->hip[0], k->hip[1]};
    fx_part(c, &p, L->top, L->topD, 1.1f);
    fx_poly(c, &p, kCollar, 6, to, k->bf, k->bu);
    fp_draw(c->cv, &p);
    fx_stroke(c, to, k->bf, k->bu, 3.3f, 27.4f, 3.45f, 33.8f, 0.3f, L->accent);
  }
  FCtx hc = *c;
  hc.rs = c->rs * HS;
  c = &hc;
  /* голова: череп, челюсть, ухо — одна форма */
  static const float kJaw[] = {-5.2f, 0.6f, -3.6f, -4.9f, 0.6f, -7.9f, 3.7f, -8.4f, 5.9f, -5.9f, 6.9f, -2.4f,
                               6.8f, 1.2f};
  fx_part(c, &p, L->skin, L->skinD, 1.7f);
  float hx, hy;
  fx_loc(o, fw, up, 0.0f, 0.8f, &hx, &hy);
  fx_ell(c, &p, hx, hy, 6.6f, 7.2f, fw[0], fw[1]);
  fx_poly(c, &p, kJaw, 7, o, fw, up);
  fx_loc(o, fw, up, -1.5f, -0.3f, &hx, &hy);
  fx_ell(c, &p, hx, hy, 1.35f, 2.0f, fw[0], fw[1]);
  fp_draw(c->cv, &p);
  fx_stroke(c, o, fw, up, -1.3f, 0.9f, -1.0f, -1.1f, 0.2f, L->skinD); /* ухо внутри */
  fx_stroke(c, o, fw, up, 6.55f, 0.3f, 7.15f, -1.4f, 0.22f, L->skinD); /* нос */
  /* глаза */
  if (suk) {
    int open = g->power;
    float rage = face == FE_HURT ? -6.0f : 0.0f;
    if (face == FE_STUN) {
      ff_eye(c, o, fw, up, 1.8f, 1.3f, 1.5f, 1.0f, 0.0f, 0.55f, L->eye, L->eyeD, 0);
      ff_eye(c, o, fw, up, 5.6f, 1.4f, 0.95f, 0.9f, 0.0f, 0.45f, L->eye, L->eyeD, 0);
    } else {
      ff_eye(c, o, fw, up, 1.8f, 1.25f, 1.6f, 0.78f, -13.0f + rage, 0.75f, L->eye, L->eyeD, 1);
      ff_eye(c, o, fw, up, 5.6f, 1.35f, 0.98f, 0.7f, 13.0f - rage, 0.6f, L->eye, L->eyeD, 1);
    }
    fx_stroke(c, o, fw, up, -0.3f, 3.15f, 3.3f, 2.15f, 0.42f, L->line); /* брови к носу */
    fx_stroke(c, o, fw, up, 4.5f, 2.3f, 6.7f, 3.05f, 0.36f, L->line);
    if (open) { /* вторая пара глаз под первой */
      ff_eye(c, o, fw, up, 1.9f, -0.75f, 1.15f, 0.5f, -9.0f, 0.42f, L->eye, L->eyeD, 0);
      ff_eye(c, o, fw, up, 5.7f, -0.65f, 0.75f, 0.45f, 9.0f, 0.35f, L->eye, L->eyeD, 0);
    } else {
      fx_stroke(c, o, fw, up, 0.7f, -0.55f, 3.0f, -0.95f, 0.22f, L->line);
      fx_stroke(c, o, fw, up, 5.0f, -0.8f, 6.4f, -0.5f, 0.2f, L->line);
    }
    /* метки: полосы на щеках и черта на лбу */
    fx_stroke(c, o, fw, up, -1.9f, -2.3f, 1.3f, -2.6f, 0.34f, L->line);
    fx_stroke(c, o, fw, up, -1.6f, -3.6f, 1.1f, -3.9f, 0.34f, L->line);
    fx_stroke(c, o, fw, up, 5.9f, -2.5f, 6.75f, -2.45f, 0.3f, L->line);
    fx_stroke(c, o, fw, up, 3.85f, 3.3f, 3.85f, 4.3f, 0.3f, L->line);
  } else if (!g->blind) {
    float w = face == FE_STUN ? 1.15f : 1.0f;
    ff_eye(c, o, fw, up, 1.8f, 1.35f, 1.5f * w, 1.0f * w, -4.0f, 0.85f, L->eye, L->eyeD, 0);
    ff_eye(c, o, fw, up, 5.6f, 1.45f, 0.95f * w, 0.95f * w, 4.0f, 0.66f, L->eye, L->eyeD, 0);
    fx_stroke(c, o, fw, up, 0.3f, 3.2f, 3.0f, 3.4f, 0.3f, FC(236, 240, 250)); /* белые брови */
  }
  ff_mouth(c, o, fw, up, face);
  /* волосы: острые пряди, ветер сдувает кончики назад */
  float wb = -g->wind * 2.2f;
  FPart h;
  fx_part(c, &h, L->hair, L->hairD, 1.5f);
  fx_grad(c, &h, L->hair2, L->hairD2, k->head[1] + 14.0f, k->head[1] + 1.0f);
  if (suk) {
    float hair[] = {6.6f, 3.8f, 9.2f + wb, 6.2f, 5.8f, 6.9f, 6.8f + wb, 11.6f, 3.2f, 9.2f, 2.0f + wb, 13.8f,
                    -0.2f, 9.8f, -4.0f + wb, 13.2f, -3.6f, 8.6f, -9.4f + wb, 9.6f, -6.2f, 5.6f, -9.4f + wb, 3.0f,
                    -6.6f, 2.2f, -6.5f, 0.2f, -4.4f, 2.8f, -1.6f, 4.7f, 2.4f, 5.1f};
    fx_poly(c, &h, hair, 17, o, fw, up);
  } else {
    float hair[] = {7.1f, 3.4f, 9.8f + wb, 7.2f, 6.1f, 7.7f, 7.6f + wb, 12.8f, 4.0f, 10.4f, 3.6f + wb, 16.4f,
                    1.2f, 11.4f, -1.4f + wb, 16.8f, -2.4f, 11.6f, -6.6f + wb, 14.8f, -5.8f, 9.6f, -11.0f + wb, 10.4f,
                    -7.4f, 6.4f, -11.2f + wb, 4.0f, -7.2f, 2.8f, -7.1f, 0.6f};
    fx_poly(c, &h, hair, 16, o, fw, up);
    float nape[] = {-6.6f, 1.0f, -9.2f + wb, -2.6f, -6.5f, -1.8f, -7.6f + wb, -5.0f, -4.8f, -3.0f, -4.4f, 0.8f};
    fx_poly(c, &h, nape, 6, o, fw, up);
    if (!g->blind) { /* без повязки — чёлка на лоб */
      float bangs[] = {7.1f, 3.6f, 7.4f, 0.9f + 0.4f * wb, 5.6f, 2.9f, 4.4f, 0.8f + 0.4f * wb, 3.0f, 3.3f,
                       1.6f, 1.6f + 0.4f * wb, 0.2f, 3.6f, -1.0f, 4.6f, 3.0f, 5.6f};
      fx_poly(c, &h, bangs, 9, o, fw, up);
    }
  }
  fp_draw(c->cv, &h);
  /* пряди-штрихи */
  FCol st = suk ? FC(205, 85, 125) : FC(184, 196, 230);
  fx_stroke(c, o, fw, up, 2.6f, 7.2f, 2.6f + wb * 0.5f, 11.0f, 0.2f, st);
  fx_stroke(c, o, fw, up, -2.2f, 7.4f, -3.0f + wb * 0.5f, 10.6f, 0.2f, st);
  fx_stroke(c, o, fw, up, -5.0f, 5.0f, -7.6f + wb * 0.5f, 6.6f, 0.2f, st);
  if (!suk && g->blind) { /* повязка поверх нижнего края волос */
    static const float kBand[] = {-7.4f, 0.4f, 7.15f, -0.1f, 7.25f, 3.5f, -7.3f, 4.2f};
    fx_part(c, &p, FC(30, 30, 40), FC(16, 16, 24), 0.8f);
    fx_poly(c, &p, kBand, 4, o, fw, up);
    fp_draw(c->cv, &p);
    fx_stroke(c, o, fw, up, -6.8f, 3.6f, 6.8f, 3.0f, 0.22f, FC(84, 84, 104));
  } else if (!suk) { /* снятая повязка висит на шее */
    float to[2] = {k->hip[0], k->hip[1]};
    fx_stroke(c, to, k->bf, k->bu, -3.6f, 33.4f, 4.0f, 32.6f, 0.75f, FC(24, 24, 32));
  }
}

static void ff_draw(FCv *cv, const FFig *g) {
  FCtx c;
  c.cv = cv;
  c.g = g;
  c.L = g->who ? &kLookSukuna : &kLookGojo;
  c.U = g->U * c.L->S;
  c.ow = fa_clampf(g->U * 0.72f, 1.2f, 4.0f);
  c.rs = 1.0f;
  fa_skel(&c.k, &g->p, g->f);
  int farFront = (g->p.flags & FPF_FARFRONT) != 0;
  if (!farFront) ff_arm(&c, 0);
  ff_leg(&c, 0);
  ff_torso(&c);
  ff_leg(&c, 1);
  if (farFront) ff_arm(&c, 0);
  ff_head(&c);
  ff_arm(&c, 1);
}

/* ===================== позы ===================== */

/* {сдвиг x, y; поворот всей фигуры; корпус, голова; рука дальняя (плечо,
   предплечье), ближняя; нога дальняя (бедро, голень), ближняя; стопы},
   кисти, лицо, флаги */
#define FPZ(dx, dy, ro, bo, he, ua0, la0, ua1, la1, ul0, ll0, ul1, ll1, f0, f1, h0, h1, fa, fl)                         \
  {{dx, dy, ro, bo, he, ua0, la0, ua1, la1, ul0, ll0, ul1, ll1, f0, f1}, h0, h1, fa, fl}
enum {
  /* общие */
  FP_HURT_H, FP_HURT_M, FP_FLY, FP_DOWN, FP_KNEEL, FP_GUARD, FP_STUN, FP_KO,
  FP_JSQUAT, FP_JRISE, FP_JTUCK, FP_JFALL, FP_LAND,
  /* Годжо */
  FP_G_IDLE, FP_G_IDLE2, FP_G_STANCE, FP_G_WALK1, FP_G_WALK2, FP_G_WALK3, FP_G_WALK4, FP_G_DASH,
  FP_G_RUN1, FP_G_RUN2,
  FP_G_JAB0, FP_G_JAB, FP_G_PALM0, FP_G_PALM, FP_G_KICK0, FP_G_KICK, FP_G_HIGH0, FP_G_HIGH,
  FP_G_UPKICK0, FP_G_UPKICK, FP_G_FLICK0, FP_G_FLICK, FP_G_DIVE,
  FP_G_BLUE, FP_G_RED0, FP_G_RED, FP_G_PUR_A, FP_G_PUR_B, FP_G_PUR_C, FP_G_SEAL, FP_G_TELE,
  FP_G_TAUNT, FP_G_WIN, FP_G_UNBLIND,
  /* Сукуна */
  FP_S_IDLE, FP_S_IDLE2, FP_S_WALK1, FP_S_WALK2, FP_S_WALK3, FP_S_WALK4, FP_S_RUN1, FP_S_RUN2,
  FP_S_CLAW0, FP_S_CLAW, FP_S_SWIPE0, FP_S_SWIPE, FP_S_PUNCH0, FP_S_PUNCH, FP_S_KNEE,
  FP_S_UPPER0, FP_S_UPPER, FP_S_KICK0, FP_S_KICK, FP_S_SLAM0, FP_S_SLAM,
  FP_S_DISM0, FP_S_DISM, FP_S_CLEAVE, FP_S_FUGA_A, FP_S_FUGA_B, FP_S_FUGA_C, FP_S_SEAL,
  FP_S_LAUGH, FP_S_TAUNT,
  FP_COUNT
};
static const FPose kPoses[FP_COUNT] = {
    /* общие */
    FPZ(0, 0, 0, -24, -18, -30, 10, -42, -12, 16, 2, -14, -22, 0, 0, FH_OPEN, FH_OPEN, FE_HURT, 0),
    FPZ(0, 0, 0, 34, 22, 30, 62, 22, 48, 30, 0, -8, -30, 0, 0, FH_OPEN, FH_OPEN, FE_HURT, 0),
    FPZ(0, 0, -30, -12, -22, -110, -150, -86, -126, 34, 6, -10, -44, 0, 0, FH_OPEN, FH_OPEN, FE_HURT, 0),
    FPZ(0, 5, -90, 0, 14, 22, 44, -32, -12, 12, 0, -6, -14, 0, 0, FH_OPEN, FH_OPEN, FE_HURT, FPF_FREE),
    FPZ(0, 0, 0, 30, 6, 62, 42, 40, 28, 82, -4, 2, -88, 0, 0, FH_OPEN, FH_FIST, FE_HURT, 0),
    FPZ(0, 0, 0, 12, 14, 54, 150, 44, 144, 26, 8, -22, -18, 0, 0, FH_FIST, FH_FIST, FE_HURT, FPF_FARFRONT),
    FPZ(0, 0, 0, -6, -14, 4, 8, -4, 4, 4, 0, -4, -6, 0, 0, FH_OPEN, FH_OPEN, FE_STUN, 0),
    FPZ(0, 5, -90, 0, 20, -100, -130, 70, 110, 16, 4, -10, -10, 0, 0, FH_OPEN, FH_OPEN, FE_STUN, FPF_FREE),
    FPZ(0, 0, 0, 18, 0, -42, -20, -52, -30, 64, -28, 54, -42, 0, 0, FH_FIST, FH_FIST, FE_CALM, 0),
    FPZ(0, 0, 0, -4, -6, 150, 162, 128, 150, 8, -6, -12, -26, -40, -40, FH_OPEN, FH_OPEN, FE_CALM, 0),
    FPZ(0, 0, 0, 10, 4, 60, 122, 40, 110, 96, 8, 74, -24, 0, 0, FH_FIST, FH_FIST, FE_CALM, 0),
    FPZ(0, 0, 0, 4, -2, 82, 72, -62, -40, 30, 8, -6, -22, -20, -20, FH_OPEN, FH_OPEN, FE_CALM, 0),
    FPZ(0, 0, 0, 14, 4, 70, 60, -40, -20, 50, -18, 40, -40, 0, 0, FH_OPEN, FH_OPEN, FE_CALM, 0),
    /* Годжо */
    FPZ(0, 0, 0, 3, -3, -8, 28, -14, 22, 7, 0, -6, -9, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, 4, -1, -7, 29, -13, 23, 7, 1, -6, -10, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, 8, 2, 50, 124, 16, 100, 24, 6, -20, -18, 0, 0, FH_OPEN, FH_FIST, FE_SMIRK, 0),
    FPZ(0, 0, 0, 4, -2, -8, 28, -14, 22, 24, 14, -16, -30, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, 5, -2, -8, 28, -14, 22, 6, 0, 16, -24, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, 4, -2, -8, 28, -14, 22, -16, -30, 24, 14, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, 5, -2, -8, 28, -14, 22, 16, -24, 6, 0, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, 26, -14, -20, 24, -26, 18, 56, 18, -34, -64, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, 22, -10, -40, -12, 54, 112, 56, 22, -32, -72, 0, 0, FH_FIST, FH_FIST, FE_SMIRK, 0),
    FPZ(0, 0, 0, 22, -10, 54, 112, -40, -12, -32, -72, 56, 22, 0, 0, FH_FIST, FH_FIST, FE_SMIRK, 0),
    FPZ(0, 0, 0, 6, 2, 30, 132, 10, 104, 24, 8, -18, -16, 0, 0, FH_FIST, FH_FIST, FE_SMIRK, 0),
    FPZ(0, 0, 0, 16, 2, 88, 90, 14, 110, 36, 6, -26, -24, 0, 0, FH_FIST, FH_FIST, FE_SMIRK, 0),
    FPZ(0, 0, 0, 2, 0, 60, 100, -40, 70, 24, 8, -18, -16, 0, 0, FH_OPEN, FH_OPEN, FE_SMIRK, 0),
    FPZ(0, 0, 0, 22, 2, -20, 20, 86, 88, 44, 4, -32, -30, 0, 0, FH_OPEN, FH_OPEN, FE_GRIN, 0),
    FPZ(0, 0, 0, -8, -2, -8, 28, -14, 22, 4, 0, 96, -36, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, -22, 4, -10, 26, -16, 20, -4, -6, 92, 92, 0, 0, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, -15, 0, -10, 26, -16, 20, 2, -2, 118, 30, 0, -50, FH_POCKET, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, -34, 8, -12, 24, -18, 18, -8, -10, 128, 132, 0, -70, FH_POCKET, FH_POCKET, FE_GRIN, 0),
    FPZ(0, 0, 0, 16, 2, 40, 90, -10, 30, 40, -4, 30, -40, 0, 0, FH_OPEN, FH_OPEN, FE_SMIRK, 0),
    FPZ(0, 0, 0, -30, 10, -30, 0, 50, 64, -10, -12, 168, 172, 0, -60, FH_OPEN, FH_OPEN, FE_GRIN, 0),
    FPZ(0, 0, 0, 2, -4, -8, 28, 60, 122, 8, 2, -8, -10, 0, 0, FH_POCKET, FH_FLICK, FE_SMIRK, 0),
    FPZ(0, 0, 0, 8, 0, -8, 28, 82, 94, 14, 4, -12, -14, 0, 0, FH_POCKET, FH_FLICK, FE_GRIN, 0),
    FPZ(0, 0, 24, 0, 0, -10, 26, -16, 20, -12, -72, 50, 50, 0, -40, FH_POCKET, FH_POCKET, FE_GRIN, 0),
    FPZ(0, 0, 0, -3, -6, 115, 122, -14, 22, 8, 2, -8, -10, 0, 0, FH_TWO, FH_POCKET, FE_SMIRK, 0),
    FPZ(0, 0, 0, -5, -6, -8, 28, 150, 170, 7, 0, -6, -9, 0, 0, FH_POCKET, FH_POINT, FE_CALM, 0),
    FPZ(0, 0, 0, 14, 0, -8, 28, 90, 92, 30, 6, -24, -24, 0, 0, FH_POCKET, FH_POINT, FE_SMIRK, 0),
    FPZ(0, 0, 0, 0, 0, 100, 104, -92, -96, 26, 6, -22, -18, 0, 0, FH_TWO, FH_TWO, FE_CALM, 0),
    FPZ(0, 0, 0, 6, 2, 62, 98, 70, 92, 24, 6, -20, -16, 0, 0, FH_OPEN, FH_OPEN, FE_CALM, FPF_FARFRONT),
    FPZ(0, 0, 0, 14, 0, 30, 60, 92, 92, 34, 6, -28, -26, 0, 0, FH_OPEN, FH_POINT, FE_SHOUT, 0),
    FPZ(0, 0, 0, 0, -3, -8, 28, 22, 168, 7, 0, -6, -9, 0, 0, FH_POCKET, FH_SEALG, FE_CALM, 0),
    FPZ(0, 0, 0, 20, 4, -40, -10, -50, -20, 34, 0, -12, -32, 0, 0, FH_OPEN, FH_OPEN, FE_SMIRK, 0),
    FPZ(0, 0, 0, -6, 14, -8, 28, -14, 22, -2, -4, 8, 0, 0, 0, FH_POCKET, FH_POCKET, FE_GRIN, 0),
    FPZ(0, 0, 0, -4, 10, -8, 28, 18, 162, 7, 0, -6, -9, 0, 0, FH_POCKET, FH_PEACE, FE_GRIN, 0),
    FPZ(0, 0, 0, -2, -8, -8, 28, 30, 172, 7, 0, -6, -9, 0, 0, FH_POCKET, FH_OPEN, FE_CALM, 0),
    /* Сукуна */
    FPZ(0, 0, 0, 14, 6, 34, 78, 6, 52, 30, 8, -26, -14, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 16, 4, 36, 82, 8, 56, 30, 8, -26, -15, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 12, 4, -10, 20, 20, 50, 24, 12, -18, -32, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 13, 4, 6, 36, 6, 40, 6, 0, 16, -24, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 12, 4, 20, 50, -10, 20, -18, -32, 24, 12, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 13, 4, 6, 40, 6, 36, 16, -24, 6, 0, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 30, -8, -54, -24, 60, 118, 58, 22, -34, -74, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 30, -8, 60, 118, -54, -24, -34, -74, 58, 22, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, -6, -4, 150, 176, 10, 60, 26, 8, -22, -16, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 28, 6, 56, 26, 14, 50, 42, 6, -30, -28, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, -4, 2, 50, 92, -62, -22, 26, 8, -22, -16, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 20, 4, 30, 70, 100, 122, 38, 6, -28, -26, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 4, 4, 40, 110, -30, 92, 26, 8, -22, -16, 0, 0, FH_CLAW, FH_FIST, FE_GRIN, 0),
    FPZ(0, 0, 0, 24, 4, 20, 60, 90, 90, 44, 4, -32, -30, 0, 0, FH_CLAW, FH_FIST, FE_SHOUT, 0),
    FPZ(0, 0, 0, 10, 8, 72, 40, 60, 30, 2, 0, 102, -12, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 22, 6, 30, 70, 10, 40, 52, -10, 30, -42, 0, 0, FH_CLAW, FH_FIST, FE_GRIN, 0),
    FPZ(0, 0, 0, -14, -10, -30, -10, 164, 172, 10, 0, -20, -30, 0, -40, FH_CLAW, FH_FIST, FE_SHOUT, 0),
    FPZ(0, 0, 0, -10, 2, 50, 100, -10, 50, 4, 0, 92, -32, 0, 0, FH_CLAW, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, -28, 8, 64, 84, -40, -18, -6, -8, 106, 108, 0, -60, FH_CLAW, FH_CLAW, FE_SHOUT, 0),
    FPZ(0, 0, 10, -8, -8, 172, 178, 166, 176, 70, 0, 50, -30, 0, 0, FH_FIST, FH_FIST, FE_SHOUT, FPF_FARFRONT),
    FPZ(0, 0, 20, 24, 10, 62, 40, 56, 34, 60, 10, 30, -40, 0, 0, FH_FIST, FH_FIST, FE_SHOUT, FPF_FARFRONT),
    FPZ(0, 0, 0, -8, -2, 120, 166, 8, 54, 26, 8, -22, -16, 0, 0, FH_TWO, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 16, 4, 76, 60, 10, 50, 36, 6, -26, -24, 0, 0, FH_TWO, FH_CLAW, FE_GRIN, 0),
    FPZ(0, 0, 0, 26, 6, 20, 60, 88, 92, 44, 4, -32, -30, 0, 0, FH_CLAW, FH_OPEN, FE_GRIN, 0),
    FPZ(0, 0, 0, 4, 8, 40, 130, 34, 136, 26, 8, -22, -16, 0, 0, FH_OPEN, FH_OPEN, FE_SHOUT, FPF_FARFRONT),
    FPZ(0, 0, 0, 0, 2, 92, 90, -100, 86, 28, 8, -24, -18, 0, 0, FH_FIST, FH_FIST, FE_GRIN, 0),
    FPZ(0, 0, 0, 2, 2, 92, 90, -112, 56, 28, 8, -24, -18, 0, 0, FH_FIST, FH_OPEN, FE_GRIN, 0),
    FPZ(0, 0, 0, 2, 6, 38, 152, 32, 158, 26, 8, -22, -16, 0, 0, FH_SEALS, FH_SEALS, FE_GRIN, FPF_FARFRONT),
    FPZ(0, 0, 0, -12, -22, 116, 136, -110, -130, 26, 8, -22, -16, 0, 0, FH_CLAW, FH_CLAW, FE_LAUGH, 0),
    FPZ(0, 0, 0, 6, 8, 70, 150, -30, 60, 28, 8, -24, -15, 0, 0, FH_OPEN, FH_FIST, FE_GRIN, 0),
};
