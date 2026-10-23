/* ---- Шахматы: фигуры -------------------------------------------------------

   Фигуры рисуются программой, без картинок: как точёные стаунтоновские из
   кости и эбена. Каждая собрана из частей — подставка, ободки, тело,
   голова; у точёных частей профиль задан точками (высота → полуширина),
   и свет считается по настоящей нормали тела вращения: блик сверху слева,
   тень справа снизу, у расширений снизу — светлый скос. Голова коня,
   крест короля — плоские детали со скруглённым краем (высота по расстоянию
   до края). Между частями — тонкая бороздка, по контуру — тёмная обводка,
   у края внутри — лёгкое затенение. Всё считается в 4 раза крупнее и
   сглаживается при уменьшении.

   Координаты деталей — в долях размера фигуры: X от −0.5 до 0.5 вправо,
   Y от 0 (верх) до 1 (низ); фигура стоит на Y ≈ 0.93. */

#define SX_SS 4 /* проб на пиксель по каждой оси */

typedef struct {
  float y, w;
} SxPt;

/* полуширина и её наклон dw/dy на профиле — плавно (кубический Эрмит) */
static int sx_prof(const SxPt *p, int n, float y, float *w, float *dw) {
  if (y < p[0].y || y > p[n - 1].y) return 0;
  int i = 0;
  while (i < n - 2 && y > p[i + 1].y) i++;
  float h = p[i + 1].y - p[i].y;
  if (h <= 0) {
    *w = p[i + 1].w;
    *dw = 0;
    return 1;
  }
  float s0 = i > 0 ? (p[i].w - p[i - 1].w) / (p[i].y - p[i - 1].y) : (p[i + 1].w - p[i].w) / h;
  float s1 = (p[i + 1].w - p[i].w) / h;
  float s2 = i + 2 < n ? (p[i + 2].w - p[i + 1].w) / (p[i + 2].y - p[i + 1].y) : s1;
  float m0 = (s0 + s1) * 0.5f, m1 = (s1 + s2) * 0.5f;
  float t = (y - p[i].y) / h, t2 = t * t, t3 = t2 * t;
  *w = (2 * t3 - 3 * t2 + 1) * p[i].w + (t3 - 2 * t2 + t) * h * m0 + (-2 * t3 + 3 * t2) * p[i + 1].w + (t3 - t2) * h * m1;
  *dw = ((6 * t2 - 6 * t) * p[i].w + (3 * t2 - 4 * t + 1) * h * m0 + (-6 * t2 + 6 * t) * p[i + 1].w + (3 * t2 - 2 * t) * h * m1) / h;
  return 1;
}

/* что под пробой: номер части (0 — мимо), плоская ли она, нормаль */
typedef struct {
  int part;
  int flat;  /* нормаль посчитать по расстоянию до края */
  int mark;  /* 1 — тёмная деталь (глаз, ноздря, грива, прорезь), 2 — светлая кромка под ней */
  float nx, ny, nz;
} SxHit;

static void sx_norm(SxHit *h, float x, float y, float z) {
  float l = sqrtf(x * x + y * y + z * z);
  if (l < 1e-6f) l = 1;
  h->nx = x / l, h->ny = y / l, h->nz = z / l;
}

/* точёная часть: внутри профиля — нормаль тела вращения */
static int sx_lathe(SxHit *h, int part, const SxPt *p, int n, float X, float Y) {
  float w, dw;
  if (!sx_prof(p, n, Y, &w, &dw) || w <= 0 || fabsf(X) > w) return 0;
  float s = X / w;
  if (s > 1) s = 1;
  if (s < -1) s = -1;
  h->part = part;
  h->flat = 0;
  sx_norm(h, s, -dw, sqrtf(1 - s * s));
  return 1;
}

static int sx_ball(SxHit *h, int part, float cx, float cy, float r, float X, float Y) {
  float dx = (X - cx) / r, dy = (Y - cy) / r, d = dx * dx + dy * dy;
  if (d > 1) return 0;
  h->part = part;
  h->flat = 0;
  sx_norm(h, dx, dy, sqrtf(1 - d));
  return 1;
}

static int sx_flat(SxHit *h, int part) {
  h->part = part;
  h->flat = 1;
  h->nx = h->ny = 0;
  h->nz = 1;
  return 1;
}

/* внутри многоугольника (чётно-нечётно) */
static int sx_in_poly(const float *p, int n, float x, float y) {
  int in = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float xi = p[i * 2], yi = p[i * 2 + 1], xj = p[j * 2], yj = p[j * 2 + 1];
    if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) in = !in;
  }
  return in;
}

/* расстояние до ломаной */
static float sx_dist_line(const float *p, int n, float x, float y) {
  float best = 1e9f;
  for (int i = 0; i + 1 < n; i++) {
    float ax = p[i * 2], ay = p[i * 2 + 1], ex = p[i * 2 + 2] - ax, ey = p[i * 2 + 3] - ay;
    float l2 = ex * ex + ey * ey, t = l2 > 0 ? ((x - ax) * ex + (y - ay) * ey) / l2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float dx = x - ax - ex * t, dy = y - ay - ey * t, d = sqrtf(dx * dx + dy * dy);
    if (d < best) best = d;
  }
  return best;
}

/* подставка: ободок и широкая ступня; small — у пешки поменьше */
static int sx_base(SxHit *h, float X, float Y, int small) {
  static const SxPt torus[] = {{0.770f, 0.215f}, {0.785f, 0.268f}, {0.805f, 0.288f}, {0.825f, 0.282f}, {0.842f, 0.255f}};
  static const SxPt foot[] = {{0.832f, 0.280f}, {0.848f, 0.338f}, {0.866f, 0.362f}, {0.900f, 0.368f},
                              {0.920f, 0.356f}, {0.934f, 0.330f}};
  static const SxPt torusS[] = {{0.790f, 0.190f}, {0.803f, 0.236f}, {0.820f, 0.252f}, {0.838f, 0.247f}, {0.852f, 0.225f}};
  static const SxPt footS[] = {{0.842f, 0.245f}, {0.857f, 0.295f}, {0.873f, 0.316f}, {0.903f, 0.320f},
                               {0.921f, 0.309f}, {0.934f, 0.288f}};
  if (sx_lathe(h, 2, small ? torusS : torus, 5, X, Y)) return 1;
  return sx_lathe(h, 1, small ? footS : foot, 6, X, Y);
}

static int sx_pawn(SxHit *h, float X, float Y) {
  static const SxPt collar[] = {{0.462f, 0.100f}, {0.474f, 0.160f}, {0.490f, 0.172f}, {0.506f, 0.160f}, {0.516f, 0.105f}};
  static const SxPt body[] = {{0.470f, 0.088f}, {0.560f, 0.094f}, {0.640f, 0.122f}, {0.720f, 0.172f}, {0.800f, 0.226f}};
  if (sx_ball(h, 5, 0, 0.330f, 0.142f, X, Y)) return 1;
  if (sx_lathe(h, 4, collar, 5, X, Y)) return 1;
  if (sx_base(h, X, Y, 1)) return 1;
  return sx_lathe(h, 3, body, 5, X, Y);
}

static int sx_rook(SxHit *h, float X, float Y) {
  static const SxPt head[] = {{0.150f, 0.252f}, {0.200f, 0.252f}, {0.290f, 0.247f}, {0.318f, 0.232f}, {0.340f, 0.198f}, {0.352f, 0.180f}};
  static const SxPt collar[] = {{0.705f, 0.205f}, {0.722f, 0.252f}, {0.742f, 0.266f}, {0.762f, 0.256f}, {0.778f, 0.225f}};
  static const SxPt body[] = {{0.345f, 0.172f}, {0.460f, 0.176f}, {0.580f, 0.190f}, {0.720f, 0.222f}};
  float ax = fabsf(X);
  /* зубцы: три спереди, между ними прорези */
  if (Y < 0.222f && ((ax > 0.062f && ax < 0.126f))) {
    /* в прорези видна задняя стенка башни — темнее и ниже */
    if (Y > 0.172f && sx_lathe(h, 7, head, 6, X, Y)) {
      h->mark = 3;
      return 1;
    }
    return 0;
  }
  /* верх зубцов — светлая площадка */
  if (Y < 0.172f && Y >= 0.150f && ax < 0.252f) {
    if (sx_lathe(h, 6, head, 6, X, Y)) {
      sx_norm(h, h->nx * 0.4f, -0.85f, 0.45f);
      return 1;
    }
  }
  if (sx_lathe(h, 6, head, 6, X, Y)) return 1;
  if (sx_lathe(h, 5, collar, 5, X, Y)) return 1;
  if (sx_base(h, X, Y, 0)) return 1;
  return sx_lathe(h, 4, body, 4, X, Y);
}

static int sx_bishop(SxHit *h, float X, float Y) {
  static const SxPt mitre[] = {{0.142f, 0.000f}, {0.160f, 0.050f}, {0.195f, 0.100f}, {0.255f, 0.146f},
                               {0.325f, 0.162f}, {0.395f, 0.150f}, {0.445f, 0.118f}, {0.478f, 0.080f}};
  static const SxPt collar[] = {{0.470f, 0.092f}, {0.482f, 0.158f}, {0.497f, 0.170f}, {0.512f, 0.158f}, {0.524f, 0.100f}};
  static const SxPt body[] = {{0.500f, 0.082f}, {0.580f, 0.090f}, {0.660f, 0.118f}, {0.730f, 0.160f}, {0.790f, 0.215f}};
  if (sx_ball(h, 7, 0, 0.110f, 0.042f, X, Y)) return 1;
  if (sx_lathe(h, 6, mitre, 8, X, Y)) {
    /* прорезь наискось — справа сверху к середине */
    static const float slit[] = {-0.030f, 0.330f, 0.118f, 0.212f};
    float d = sx_dist_line(slit, 2, X, Y);
    if (d < 0.016f) h->mark = 1;
    else if (d < 0.030f && Y > 0.212f + (X - 0.118f) * (0.330f - 0.212f) / (-0.030f - 0.118f)) h->mark = 2;
    return 1;
  }
  if (sx_lathe(h, 5, collar, 5, X, Y)) return 1;
  if (sx_base(h, X, Y, 0)) return 1;
  return sx_lathe(h, 4, body, 5, X, Y);
}

/* верхний край короны ферзя: зубцы к шарикам и впадины между ними */
static float sx_crown_edge(float ax) {
  static const float pt[][2] = {{0.000f, 0.140f}, {0.052f, 0.232f}, {0.105f, 0.160f}, {0.156f, 0.246f},
                                {0.205f, 0.190f}, {0.240f, 0.236f}, {0.300f, 0.300f}};
  for (int i = 0; i + 1 < 7; i++)
    if (ax <= pt[i + 1][0]) {
      float t = (ax - pt[i][0]) / (pt[i + 1][0] - pt[i][0]);
      return pt[i][1] + (pt[i + 1][1] - pt[i][1]) * t;
    }
  return 1;
}

static int sx_queen(SxHit *h, float X, float Y) {
  static const SxPt crown[] = {{0.135f, 0.232f}, {0.200f, 0.218f}, {0.265f, 0.178f}, {0.318f, 0.140f}, {0.350f, 0.128f}};
  static const SxPt collar[] = {{0.338f, 0.100f}, {0.350f, 0.150f}, {0.365f, 0.162f}, {0.380f, 0.150f}, {0.392f, 0.108f}};
  static const SxPt body[] = {{0.370f, 0.090f}, {0.470f, 0.098f}, {0.570f, 0.124f}, {0.670f, 0.170f}, {0.790f, 0.250f}};
  static const float balls[5][3] = {{0, 0.128f, 0.036f}, {-0.105f, 0.148f, 0.032f}, {0.105f, 0.148f, 0.032f},
                                    {-0.205f, 0.178f, 0.030f}, {0.205f, 0.178f, 0.030f}};
  for (int i = 0; i < 5; i++)
    if (sx_ball(h, 9, balls[i][0], balls[i][1], balls[i][2], X, Y)) return 1;
  if (Y >= sx_crown_edge(fabsf(X)) && sx_lathe(h, 8, crown, 5, X, Y)) return 1;
  if (sx_lathe(h, 7, collar, 5, X, Y)) return 1;
  if (sx_base(h, X, Y, 0)) return 1;
  return sx_lathe(h, 6, body, 5, X, Y);
}

static int sx_king(SxHit *h, float X, float Y) {
  static const SxPt cap[] = {{0.150f, 0.000f}, {0.158f, 0.040f}, {0.172f, 0.068f}, {0.190f, 0.084f}, {0.205f, 0.090f}};
  static const SxPt cup[] = {{0.192f, 0.214f}, {0.212f, 0.222f}, {0.255f, 0.196f}, {0.300f, 0.160f}, {0.336f, 0.132f}, {0.352f, 0.126f}};
  static const SxPt collar[] = {{0.336f, 0.100f}, {0.348f, 0.152f}, {0.364f, 0.165f}, {0.380f, 0.152f}, {0.392f, 0.110f}};
  static const SxPt body[] = {{0.370f, 0.092f}, {0.470f, 0.102f}, {0.570f, 0.128f}, {0.670f, 0.176f}, {0.790f, 0.258f}};
  float ax = fabsf(X);
  /* крест */
  if ((ax < 0.031f && Y > 0.030f && Y < 0.168f) || (ax < 0.084f && Y > 0.066f && Y < 0.112f)) return sx_flat(h, 10);
  if (sx_lathe(h, 9, cap, 5, X, Y)) return 1;
  /* верх чаши короны — светлый эллипс: смотрим чуть сверху */
  {
    float dx = X / 0.212f, dy = (Y - 0.214f) / 0.032f;
    if (dx * dx + dy * dy <= 1) {
      h->part = 8;
      h->flat = 0;
      sx_norm(h, dx * 0.35f, -0.9f + dy * 0.2f, 0.45f);
      return 1;
    }
  }
  if (sx_lathe(h, 7, cup, 6, X, Y)) return 1;
  if (sx_lathe(h, 6, collar, 5, X, Y)) return 1;
  if (sx_base(h, X, Y, 0)) return 1;
  return sx_lathe(h, 5, body, 5, X, Y);
}

/* голова коня смотрит влево */
static const float kSxHorse[] = {
    0.235f,  0.775f, 0.262f,  0.600f, 0.262f,  0.470f, 0.232f,  0.345f, 0.178f,  0.240f, 0.110f,  0.165f,
    0.050f,  0.128f, 0.032f,  0.052f, 0.004f,  0.095f, -0.040f, 0.132f, -0.105f, 0.162f, -0.180f, 0.222f,
    -0.250f, 0.300f, -0.312f, 0.372f, -0.345f, 0.425f, -0.342f, 0.468f, -0.318f, 0.494f, -0.262f, 0.505f,
    -0.205f, 0.488f, -0.140f, 0.458f, -0.086f, 0.462f, -0.070f, 0.520f, -0.110f, 0.600f, -0.150f, 0.690f,
    -0.172f, 0.775f};

static int sx_knight(SxHit *h, float X, float Y) {
  if (sx_base(h, X, Y, 0)) return 1;
  if (!sx_in_poly(kSxHorse, (int)(sizeof(kSxHorse) / sizeof(kSxHorse[0]) / 2), X, Y)) return 0;
  sx_flat(h, 5);
  /* глаз, ноздря, рот, грива */
  float ex = (X + 0.080f) / 0.030f, ey = (Y - 0.240f) / 0.020f;
  if (ex * ex + ey * ey < 1) h->mark = 1;
  float nx = (X + 0.288f) / 0.018f, ny = (Y - 0.418f) / 0.015f;
  if (nx * nx + ny * ny < 1) h->mark = 1;
  static const float mouth[] = {-0.338f, 0.470f, -0.290f, 0.468f, -0.252f, 0.458f};
  if (sx_dist_line(mouth, 3, X, Y) < 0.008f) h->mark = 1;
  static const float mane[] = {0.040f, 0.150f, 0.112f, 0.212f, 0.170f, 0.300f, 0.206f, 0.400f, 0.220f, 0.520f, 0.222f, 0.640f};
  float dm = sx_dist_line(mane, 6, X, Y);
  if (dm < 0.010f) h->mark = 1;
  else if (dm < 0.020f && X < 0.21f) h->mark = 2;
  /* пряди гривы — короткие насечки от гребня к спине */
  for (int i = 0; i < 5; i++) {
    float t = 0.20f + i * 0.09f;
    float a[4] = {0.112f + (0.222f - 0.112f) * (t - 0.20f) / 0.44f, t, 0.0f, 0.0f};
    a[2] = a[0] + 0.040f;
    a[3] = t - 0.018f;
    if (sx_dist_line(a, 2, X, Y) < 0.007f) h->mark = 1;
  }
  return 1;
}

static int sx_piece_at(int kind, float X, float Y, SxHit *h) {
  memset(h, 0, sizeof(*h));
  X /= 1.07f; /* профили чуть шире, чем у точёных: на маленьком поле фигура читается лучше */
  switch (kind) {
  case SX_P: return sx_pawn(h, X, Y);
  case SX_N: return sx_knight(h, X, Y);
  case SX_B: return sx_bishop(h, X, Y);
  case SX_R: return sx_rook(h, X, Y);
  case SX_Q: return sx_queen(h, X, Y);
  case SX_K: return sx_king(h, X, Y);
  }
  return 0;
}

/* расстояние (в пробах, ×3) до ближайшей пробы, где in[] ложно: фаска 3-4 */
static void sx_chamfer(const unsigned char *in, int W, int H, unsigned short *d) {
  for (int i = 0; i < W * H; i++) d[i] = in[i] ? 60000 : 0;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      unsigned short *p = &d[y * W + x];
      if (!*p) continue;
      unsigned v = *p;
      if (x > 0 && d[y * W + x - 1] + 3u < v) v = d[y * W + x - 1] + 3u;
      if (y > 0) {
        if (d[(y - 1) * W + x] + 3u < v) v = d[(y - 1) * W + x] + 3u;
        if (x > 0 && d[(y - 1) * W + x - 1] + 4u < v) v = d[(y - 1) * W + x - 1] + 4u;
        if (x + 1 < W && d[(y - 1) * W + x + 1] + 4u < v) v = d[(y - 1) * W + x + 1] + 4u;
      }
      if (x == 0 || y == 0 || x == W - 1) v = v > 3 ? 3 : v;
      *p = (unsigned short)v;
    }
  for (int y = H - 1; y >= 0; y--)
    for (int x = W - 1; x >= 0; x--) {
      unsigned short *p = &d[y * W + x];
      if (!*p) continue;
      unsigned v = *p;
      if (x + 1 < W && d[y * W + x + 1] + 3u < v) v = d[y * W + x + 1] + 3u;
      if (y + 1 < H) {
        if (d[(y + 1) * W + x] + 3u < v) v = d[(y + 1) * W + x] + 3u;
        if (x + 1 < W && d[(y + 1) * W + x + 1] + 4u < v) v = d[(y + 1) * W + x + 1] + 4u;
        if (x > 0 && d[(y + 1) * W + x - 1] + 4u < v) v = d[(y + 1) * W + x - 1] + 4u;
      }
      if (y == H - 1) v = v > 3 ? 3 : v;
      *p = (unsigned short)v;
    }
}

/* Фигура kind (SX_P…SX_K) размером D точек, black — из эбена. Спрайт
   (D + 4)², фигура по центру, низ подставки — на 0.93 D от верха фигуры. */
static void sx_render_piece(GnSprite *s, int D, int kind, int black) {
  int m = 2, W = D + m * 2;
  if (!gn_sprite_alloc(s, W, W)) return;
  int HW = W * SX_SS, N = HW * HW;
  unsigned char *part = (unsigned char *)calloc(N, 1), *mask = (unsigned char *)calloc(N, 1);
  unsigned char *mk = (unsigned char *)calloc(N, 1), *flat = (unsigned char *)calloc(N, 1);
  unsigned short *din = (unsigned short *)calloc(N, sizeof(unsigned short));
  unsigned short *dfl = (unsigned short *)calloc(N, sizeof(unsigned short));
  unsigned short *dcr = (unsigned short *)calloc(N, sizeof(unsigned short));
  float *nrm = (float *)calloc((size_t)N * 3, sizeof(float));
  if (!part || !mask || !mk || !flat || !din || !dfl || !dcr || !nrm) goto done;
  float sc = 1.0f / ((float)D * SX_SS);
  for (int y = 0; y < HW; y++)
    for (int x = 0; x < HW; x++) {
      float X = ((x + 0.5f) - HW * 0.5f) * sc, Y = ((y + 0.5f) - m * SX_SS) * sc;
      SxHit h;
      if (!sx_piece_at(kind, X, Y, &h)) continue;
      int i = y * HW + x;
      part[i] = (unsigned char)h.part;
      mk[i] = (unsigned char)h.mark;
      flat[i] = (unsigned char)h.flat;
      nrm[i * 3] = h.nx, nrm[i * 3 + 1] = h.ny, nrm[i * 3 + 2] = h.nz;
    }
  /* до края фигуры изнутри: там обводка и затенение */
  for (int i = 0; i < N; i++) mask[i] = part[i] != 0;
  sx_chamfer(mask, HW, HW, din);
  /* до шва между частями: шов — проба, у которой сосед из другой части */
  for (int y = 0; y < HW; y++)
    for (int x = 0; x < HW; x++) {
      int i = y * HW + x, p = part[i], seam = 0;
      if (p) {
        if (x + 1 < HW && part[i + 1] && part[i + 1] != p) seam = 1;
        if (x > 0 && part[i - 1] && part[i - 1] != p) seam = 1;
        if (y + 1 < HW && part[i + HW] && part[i + HW] != p) seam = 1;
        if (y > 0 && part[i - HW] && part[i - HW] != p) seam = 1;
      }
      mask[i] = !seam;
    }
  sx_chamfer(mask, HW, HW, dcr);
  /* плоские части: высота по расстоянию до края своей части — скруглённая
     кромка, нормаль — по наклону высоты */
  {
    /* край плоской части — и снаружи, и там, где она встречает другую часть */
    float R = D * SX_SS * 0.075f;
    for (int i = 0; i < N; i++) mask[i] = flat[i] != 0;
    sx_chamfer(mask, HW, HW, dfl);
    for (int y = 1; y < HW - 1; y++)
      for (int x = 1; x < HW - 1; x++) {
        int i = y * HW + x;
        if (!flat[i]) continue;
        float hv[4];
        int nb[4] = {i - 1, i + 1, i - HW, i + HW};
        for (int k = 0; k < 4; k++) {
          float d = dfl[nb[k]] / 3.0f, t = d >= R ? 1 : d / R;
          hv[k] = R * sqrtf(1 - (1 - t) * (1 - t));
        }
        float gx = (hv[1] - hv[0]) * 0.5f, gy = (hv[3] - hv[2]) * 0.5f;
        float l = sqrtf(gx * gx + gy * gy + 1);
        nrm[i * 3] = -gx / l, nrm[i * 3 + 1] = -gy / l, nrm[i * 3 + 2] = 1 / l;
      }
  }
  {
    /* свет сверху слева и чуть спереди */
    float lx = -0.48f, ly = -0.62f, lz = 0.62f, ll = sqrtf(lx * lx + ly * ly + lz * lz);
    lx /= ll, ly /= ll, lz /= ll;
    float hx = lx, hy = ly, hz = lz + 1, hl = sqrtf(hx * hx + hy * hy + hz * hz);
    hx /= hl, hy /= hl, hz /= hl;
    const float *sh = black ? (const float[]){14, 10, 9} : (const float[]){138, 108, 76};
    const float *lt = black ? (const float[]){112, 92, 82} : (const float[]){252, 244, 226};
    const float *ol = black ? (const float[]){8, 6, 5} : (const float[]){62, 44, 30};
    float specK = black ? 0.50f : 0.30f, specP = black ? 28.0f : 18.0f;
    float rimIn = SX_SS * 1.6f, outW = SX_SS * 1.05f, seamW = SX_SS * 0.55f;
    for (int y = 0; y < W; y++)
      for (int x = 0; x < W; x++) {
        float ar = 0, ag = 0, ab = 0, cov = 0;
        for (int sy = 0; sy < SX_SS; sy++)
          for (int sx = 0; sx < SX_SS; sx++) {
            int i = (y * SX_SS + sy) * HW + x * SX_SS + sx;
            float r, g, b;
            if (part[i]) {
              const float *n = &nrm[i * 3];
              float ndl = n[0] * lx + n[1] * ly + n[2] * lz;
              float t = 0.30f + 0.70f * (ndl > 0 ? ndl : 0);
              /* снизу отражённый свет от доски — тени не проваливаются */
              if (n[1] > 0) t += 0.10f * n[1];
              if (t > 1.15f) t = 1.15f;
              r = sh[0] + (lt[0] - sh[0]) * t, g = sh[1] + (lt[1] - sh[1]) * t, b = sh[2] + (lt[2] - sh[2]) * t;
              /* фактура кости / волокна эбена — чуть-чуть */
              float X = (x * SX_SS + sx) * sc, Y = (y * SX_SS + sy) * sc;
              float tex = gn_noise(X * 60, Y * 14, black ? 71u : 29u) - 0.5f;
              r *= 1 + tex * 0.06f, g *= 1 + tex * 0.06f, b *= 1 + tex * 0.06f;
              float nh = n[0] * hx + n[1] * hy + n[2] * hz;
              if (nh > 0) {
                float sp = powf(nh, specP) * specK * 255;
                r += sp, g += sp * 0.97f, b += sp * 0.92f;
              }
              if (mk[i] == 1) {
                float k = black ? 1.9f : 0.34f;
                r *= k, g *= k, b *= k;
                if (black) r += 18, g += 14, b += 10;
              } else if (mk[i] == 2) {
                r = r * 0.8f + 255 * 0.2f, g = g * 0.8f + 250 * 0.2f, b = b * 0.8f + 240 * 0.2f;
              } else if (mk[i] == 3) {
                r *= 0.55f, g *= 0.55f, b *= 0.55f;
              }
              /* шов между частями */
              float dc = dcr[i] / 3.0f;
              if (dc < seamW) {
                float k = 0.62f + 0.38f * dc / seamW;
                r *= k, g *= k, b *= k;
              }
              /* у края изнутри — тень и обводка */
              float di = din[i] / 3.0f;
              if (di < outW) r = ol[0], g = ol[1], b = ol[2];
              else if (di < outW + rimIn) {
                float k = 0.80f + 0.20f * (di - outW) / rimIn;
                r *= k, g *= k, b *= k;
              }
            } else {
              continue; /* снаружи — пусто: обводка внутри силуэта */
            }
            ar += r, ag += g, ab += b, cov += 1;
          }
        if (cov <= 0) continue;
        s->px[(size_t)y * W + x] = gn_pm(ar / cov, ag / cov, ab / cov, cov / (SX_SS * SX_SS));
      }
  }
done:
  free(part);
  free(mask);
  free(mk);
  free(flat);
  free(din);
  free(dfl);
  free(dcr);
  free(nrm);
}

/* мягкая овальная тень под фигурой: полуоси rx × ry */
static void sx_make_shadow(GnSprite *s, int rx, int ry, float alpha) {
  int w = rx * 2 + 2, h = ry * 2 + 2;
  if (!gn_sprite_alloc(s, w, h)) return;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float dx = (x + 0.5f - w / 2.0f) / rx, dy = (y + 0.5f - h / 2.0f) / ry, d = sqrtf(dx * dx + dy * dy);
      if (d >= 1) continue;
      float a = d < 0.45f ? 1 : 1 - (d - 0.45f) / 0.55f;
      a = a * a * (3 - 2 * a) * alpha;
      s->px[(size_t)y * w + x] = gn_pm(0, 0, 0, a);
    }
}
