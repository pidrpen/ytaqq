/* ai_avatar_girl.c — третий вид помощника: кошко-девочка со скелетом (подключается
   в ai_avatar.c). Слои — ai_avatar_girl.png (ресурс 382, native/avatar/pack_catgirl.py),
   скелет и позы — перенос catgirl-core.js из набора Codex (2026-10-25): двенадцать
   действий (покой, привет, говорит, думает, слушает, читает файл, радость, огорчение,
   сон, потягивается, гладят, касание), руки — две кости с обратной кинематикой, голова
   с ушами, хвост. Между действиями поза смешивается 280 мс. Слои рисуются здесь же
   с поворотом и масштабом билинейной выборкой; общая «живость» окна (дыхание, прыжки,
   раскачивание на курсоре) — та же поза AcXf, что у робота и кошки. */
static int g_acGirlLevel; /* уровень атласа под нынешний размер окна */

enum { AGA_IDLE, AGA_WAVE, AGA_TALK, AGA_THINK, AGA_LISTEN, AGA_READ, AGA_HAPPY, AGA_SAD, AGA_SLEEP, AGA_WAKE,
       AGA_PET, AGA_TAP, AGA_N };
static const float kAgaDur[AGA_N] = {4.0f, 2.6f, 3.0f, 3.4f, 3.0f, 3.4f, 2.2f, 2.8f, 4.0f, 2.6f, 2.6f, 1.6f};
static const BYTE kAgaLoop[AGA_N] = {1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0};

typedef struct {
  float leftA, leftB, rightA, rightB, head, earL, earR, tail, lean, sx, sy, y, blink, mouth, mouthY, open, point, paper,
      joy, zzz;
} AgPose;
#define AG_FIELDS ((int)(sizeof(AgPose) / sizeof(float)))

static int g_agAct = AGA_IDLE, g_agShot = -1; /* действие сейчас; разовое поверх состояния (касание, гладят…) */
static DWORD g_agShotAt, g_agBlendAt;
static AgPose g_agFrom, g_agLast;             /* поза, из которой смешиваем, и последняя нарисованная */

static float ag_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float ag_smooth(float x) {
  x = ag_clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}
static float ag_mixf(float a, float b, float t) { return a + (b - a) * t; }

/* рука от плеча (sx, sy) к точке (tx, ty): углы плеча и предплечья, side — куда гнётся локоть */
static void ag_ik(float sx, float sy, float tx, float ty, float side, float *a, float *b) {
  const float l1 = AG_UPPER, l2 = AG_LOWER;
  float dx = tx - sx, dy = ty - sy, len = sqrtf(dx * dx + dy * dy);
  if (len < 0.001f) len = 0.001f;
  float d = ag_clamp(len, fabsf(l1 - l2) + 0.1f, l1 + l2 - 0.1f), ux = dx / len, uy = dy / len;
  float p = (l1 * l1 - l2 * l2 + d * d) / (2.0f * d), h = sqrtf(fmaxf(0.0f, l1 * l1 - p * p));
  float ex = sx + ux * p - uy * h * side, ey = sy + uy * p + ux * h * side;
  *a = atan2f(ex - sx, ey - sy);
  *b = atan2f(tx - ex, ty - ey);
}

/* поза действия act в момент t секунд от его начала (catgirl-core.js, pose) */
static void ag_pose(int act, float t, float lookX, AgPose *p) {
  const float PI = 3.14159265f, TAU = 6.2831853f;
  float d = kAgaDur[act];
  t = kAgaLoop[act] ? fmodf(fmodf(t, d) + d, d) : ag_clamp(t, 0.0f, d);
  float q = t / d, ph = TAU * q;
  float env = ag_smooth(t / 0.32f) * ag_smooth((d - t) / 0.32f);
  AgPose z = {-0.10f, -0.025f, 0.10f, 0.025f, 0, 0, 0, 0.07f * sinf(ph), 0.006f * sinf(ph), 1.0f - 0.0018f * sinf(ph),
              1.0f + 0.004f * sinf(ph), 0, 0, 0, 1.0f, 0, 0, 0, 0, 0};
  *p = z;
  float bt = t - d * 0.62f;
  if (bt >= 0.0f && bt < 0.21f) p->blink = bt < 0.05f ? ag_smooth(bt / 0.05f) : bt < 0.085f ? 1.0f : 1.0f - ag_smooth((bt - 0.085f) / 0.125f);
  float twitch = fmaxf(0.0f, 1.0f - fabsf(q - 0.32f) / 0.05f);
  p->earL = 0.07f * sinf(twitch * PI) * twitch;
  switch (act) {
  case AGA_WAVE:
    p->rightA = ag_mixf(0.1f, 0.9f, env);
    p->rightB = ag_mixf(0.025f, 2.88f + 0.24f * sinf(ph * 3), env);
    p->open = env;
    p->head = -0.045f * env;
    p->earR = -0.06f * env;
    p->tail += 0.12f * env * sinf(ph * 2);
    break;
  case AGA_TALK:
    p->head = 0.025f * sinf(ph * 2);
    p->rightA = 0.20f + 0.10f * sinf(ph * 2);
    p->rightB = -0.44f + 0.20f * sinf(ph * 3);
    p->mouth = 0.94f;
    p->mouthY = 0.42f + 0.58f * (0.5f + 0.5f * sinf(ph * 9)) * (0.75f + 0.25f * sinf(ph * 2));
    p->tail += 0.055f * sinf(ph * 2);
    break;
  case AGA_THINK:
    ag_ik(kAgShoulderR[0], kAgShoulderR[1], 177.0f + sinf(ph), 182.0f + cosf(ph), -1.0f, &p->rightA, &p->rightB);
    p->point = 1.0f;
    p->head = -0.075f + 0.012f * sinf(ph);
    p->earL = -0.08f;
    p->earR = 0.08f;
    p->tail = 0.14f + 0.1f * sinf(ph);
    break;
  case AGA_LISTEN:
    p->head = 0.075f + 0.02f * sinf(ph);
    p->lean = 0.009f;
    p->earL = -0.045f;
    p->earR = 0.055f;
    p->tail = 0.045f * sinf(ph);
    break;
  case AGA_READ:
    ag_ik(kAgShoulderL[0], kAgShoulderL[1], 143.0f, 279.0f + 2.0f * sinf(ph), 1.0f, &p->leftA, &p->leftB);
    ag_ik(kAgShoulderR[0], kAgShoulderR[1], 179.0f, 279.0f + 2.0f * sinf(ph), -1.0f, &p->rightA, &p->rightB);
    p->head = 0.025f * sinf(ph * 2);
    p->paper = 1.0f;
    p->tail = 0.045f * sinf(ph);
    break;
  case AGA_HAPPY: {
    float u = (t - 0.38f) / 0.95f, j = u > 0.0f && u < 1.0f ? sinf(PI * u) : 0.0f;
    p->y = -29.0f * j;
    p->leftA = -0.1f - 0.62f * j;
    p->leftB = -0.1f - 0.56f * j;
    p->rightA = 0.1f + 0.62f * j;
    p->rightB = 0.1f + 0.56f * j;
    p->open = j;
    p->head = -0.03f * env;
    p->blink = env;
    p->joy = env;
    p->tail = 0.20f * sinf(ph * 2) * env;
    float land = t - 1.33f;
    if (land > 0.0f && land < 0.55f) {
      float a = 0.035f * expf(-land * 8.0f) * sinf(land * 24.0f);
      p->sy -= a;
      p->sx += a * 0.55f;
    }
    break;
  }
  case AGA_SAD:
    p->head = 0.075f * env;
    p->sy -= 0.015f * env;
    p->earL = -0.19f * env;
    p->earR = 0.19f * env;
    p->tail = 0.02f * sinf(ph);
    p->blink = fmaxf(p->blink, 0.25f * env);
    break;
  case AGA_SLEEP:
    p->head = 0.12f + 0.012f * sinf(ph);
    p->lean = 0.018f;
    p->sy = 0.985f + 0.004f * sinf(ph);
    p->blink = 1.0f;
    p->tail = 0.02f * sinf(ph);
    p->zzz = 1.0f;
    break;
  case AGA_WAKE:
    p->leftA = ag_mixf(-0.1f, -2.12f, env);
    p->rightA = ag_mixf(0.1f, 2.12f, env);
    p->leftB = ag_mixf(-0.025f, -2.7f, env);
    p->rightB = ag_mixf(0.025f, 2.7f, env);
    p->sy += 0.018f * env;
    p->head = -0.055f * env;
    p->blink = env;
    p->open = env;
    p->tail = 0.16f * sinf(ph) * env;
    break;
  case AGA_PET:
    p->head = -0.12f * env + 0.012f * sinf(ph * 2) * env;
    p->lean = -0.015f * env;
    p->blink = env;
    p->earL = -0.17f * env;
    p->earR = 0.15f * env;
    p->tail = 0.18f * sinf(ph * 2) * env;
    p->joy = 0.6f * env;
    break;
  case AGA_TAP: {
    float pulse = sinf(PI * ag_clamp((t - 0.12f) / 0.55f, 0.0f, 1.0f));
    p->y = -8.0f * pulse;
    p->sy -= 0.017f * pulse;
    p->sx += 0.008f * pulse;
    p->head = 0.06f * sinf(ph * 2) * env;
    p->earL = 0.10f * env;
    p->earR = -0.10f * env;
    p->tail = 0.24f * sinf(ph * 2) * env;
    p->rightA += 0.28f * pulse;
    break;
  }
  }
  p->head += lookX * 0.035f;
  p->lean += lookX * 0.003f;
}

/* разовое действие поверх состояния: касание, гладят, потягивается, радость */
static void ac_girl_shot(int act) {
  g_agShot = act;
  g_agShotAt = GetTickCount();
}

/* состояние ИИ (AC_AV_…) → действие: рассказывает факт — говорит, смеётся — радуется */
static const BYTE kAgFromState[AC_AVATAR_ROWS] = {AGA_IDLE, AGA_LISTEN, AGA_THINK, AGA_READ, AGA_WAVE, AGA_HAPPY,
                                                  AGA_SAD,  AGA_TALK,   AGA_SLEEP, AGA_TALK, AGA_HAPPY, AGA_WAKE};

/* сколько мс длится «петля» состояния у нынешнего вида: девочке нужно больше, чем 24 кадра робота */
static DWORD ac_girl_state_ms(int state) { return (DWORD)(kAgaDur[kAgFromState[state]] * 1000.0f); }

/* какое действие сейчас: разовое, иначе по состоянию и эмоции */
static int ac_girl_action(DWORD now, int emo, float *t) {
  if (g_agShot >= 0) {
    float s = (now - g_agShotAt) / 1000.0f;
    if (s < kAgaDur[g_agShot]) {
      *t = s;
      return g_agShot;
    }
    g_agShot = -1;
  }
  int act = kAgFromState[g_acAvatarState];
  if (act == AGA_IDLE && emo == AC_EMO_SAD) act = AGA_SAD;
  *t = (now - g_acAvatarStateAt) / 1000.0f;
  return act;
}

/* ---------- матрицы как у холста: x' = a·x + c·y + e, y' = b·x + d·y + f */
typedef struct { float a, b, c, d, e, f; } AgM;
static AgM ag_mul(AgM m, AgM n) {
  AgM r = {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d,
           m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f};
  return r;
}
static AgM ag_t(AgM m, float x, float y) { AgM n = {1, 0, 0, 1, x, y}; return ag_mul(m, n); }
static AgM ag_r(AgM m, float a) { AgM n = {cosf(a), sinf(a), -sinf(a), cosf(a), 0, 0}; return ag_mul(m, n); }
static AgM ag_s(AgM m, float sx, float sy) { AgM n = {sx, 0, 0, sy, 0, 0}; return ag_mul(m, n); }

/* Слой L: m — пиксели исходного слоя → окно. clipY — рисовать только строки слоя выше
   этой (голова поверх тела без длинных волос), 1e9 — весь. Билинейно, вне слоя прозрачно. */
static void ag_layer(AgM m, int L, float alpha, float clipY) {
  if (alpha < 0.004f) return;
  const AcGirlLayer *g = &kAcGirl[g_acGirlLevel][L];
  AgM t = ag_s(ag_t(m, g->ox, g->oy), 1.0f / g->kx, 1.0f / g->ky); /* атлас → окно */
  float det = t.a * t.d - t.b * t.c;
  if (fabsf(det) < 1e-9f) return;
  float ia = t.d / det, ib = -t.b / det, ic = -t.c / det, id = t.a / det;
  float ie = -(ia * t.e + ic * t.f), ifv = -(ib * t.e + id * t.f);
  int W = g->w, H = g->h, S = g_acAvatarSize;
  if (clipY < 1e8f) {
    float v = (clipY - g->oy) * g->ky;
    if (v <= 0.0f) return;
    if (v < H) H = (int)v;
  }
  float cx[4] = {0, (float)W, 0, (float)W}, cy[4] = {0, 0, (float)H, (float)H};
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < 4; i++) {
    float px = t.a * cx[i] + t.c * cy[i] + t.e, py = t.b * cx[i] + t.d * cy[i] + t.f;
    x0 = fminf(x0, px); x1 = fmaxf(x1, px); y0 = fminf(y0, py); y1 = fmaxf(y1, py);
  }
  int bx0 = (int)floorf(x0) - 1, bx1 = (int)ceilf(x1) + 1, by0 = (int)floorf(y0) - 1, by1 = (int)ceilf(y1) + 1;
  if (bx0 < 0) bx0 = 0;
  if (by0 < 0) by0 = 0;
  if (bx1 > S) bx1 = S;
  if (by1 > S) by1 = S;
  const BYTE *src = g_acGirlPixels + ((size_t)g->y * AC_GIRL_W + g->x) * 4;
  int A = (int)(ag_clamp(alpha, 0.0f, 1.0f) * 256.0f);
  for (int y = by0; y < by1; y++) {
    BYTE *dst = g_acAvatarFramePixels + ((size_t)y * S + bx0) * 4;
    float fy = y + 0.5f;
    for (int x = bx0; x < bx1; x++, dst += 4) {
      float fx = x + 0.5f;
      float u = ia * fx + ic * fy + ie - 0.5f, v = ib * fx + id * fy + ifv - 0.5f;
      if (u <= -1.0f || v <= -1.0f || u >= W || v >= H) continue;
      int u0 = (int)floorf(u), v0 = (int)floorf(v);
      int wu = (int)((u - u0) * 256.0f), wv = (int)((v - v0) * 256.0f);
      int acc[4] = {0, 0, 0, 0};
      for (int k = 0; k < 4; k++) {
        int uu = u0 + (k & 1), vv = v0 + (k >> 1);
        if (uu < 0 || vv < 0 || uu >= W || vv >= H) continue;
        int w = ((k & 1) ? wu : 256 - wu) * ((k >> 1) ? wv : 256 - wv);
        const BYTE *s = src + ((size_t)vv * AC_GIRL_W + uu) * 4;
        acc[0] += s[0] * w; acc[1] += s[1] * w; acc[2] += s[2] * w; acc[3] += s[3] * w;
      }
      int sa = (acc[3] >> 16) * A >> 8;
      if (!sa) continue;
      int keep = 255 - sa;
      for (int c = 0; c < 3; c++) dst[c] = (BYTE)(((acc[c] >> 16) * A >> 8) + dst[c] * keep / 255);
      dst[3] = (BYTE)(sa + dst[3] * keep / 255);
    }
  }
}

/* слой на точке (x, y) холста с масштабом и поворотом вокруг своей опоры */
static void ag_asset(AgM m, int L, float x, float y, float scale, float rot, float alpha) {
  m = ag_t(m, x, y);
  m = ag_r(m, rot);
  m = ag_s(m, scale, scale);
  ag_layer(ag_t(m, -kAcGirlAnchor[L][0], -kAcGirlAnchor[L][1]), L, alpha, 1e9f);
}

/* кость: от (x, y) под углом angle (0 — вниз) длиной length */
static void ag_bone(AgM m, int L, float x, float y, float angle, float length, float alpha) {
  float dx = kAcGirlEnd[L][0] - kAcGirlAnchor[L][0], dy = kAcGirlEnd[L][1] - kAcGirlAnchor[L][1];
  float rot = atan2f(cosf(angle), sinf(angle)) - atan2f(dy, dx);
  ag_asset(m, L, x, y, length / sqrtf(dx * dx + dy * dy), rot, alpha);
}

/* голова: уши, лицо, закрытые глаза, рот; front — поверх тела, только верх (без длинных волос) */
static void ag_head(AgM m, const AgPose *p, BOOL front) {
  float clip = front ? 305.0f : 1e9f;
  m = ag_t(m, kAgHead[0], kAgHead[1]);
  m = ag_r(m, p->head);
  m = ag_s(m, AG_HEAD_SCALE, AG_HEAD_SCALE);
  m = ag_t(m, -kAgHeadPivot[0], -kAgHeadPivot[1]);
  static const int kEars[2] = {AG_EARL, AG_EARR};
  for (int i = 0; i < 2; i++) {
    int L = kEars[i];
    AgM e = ag_r(ag_t(m, kAcGirlAnchor[L][0], kAcGirlAnchor[L][1]), i ? p->earR : p->earL);
    ag_layer(ag_t(e, -kAcGirlAnchor[L][0], -kAcGirlAnchor[L][1]), L, 1.0f, 1e9f);
  }
  ag_layer(m, AG_HEADCORE, 1.0f, clip);
  ag_layer(m, AG_EYESCLOSED, p->blink, clip);
  if (p->mouth > 0.001f) {
    AgM r = ag_s(ag_t(m, kAgMouth[0], kAgMouth[1]), 1.0f, p->mouthY);
    ag_layer(ag_t(r, -kAcGirlAnchor[AG_MOUTHTALK][0], -kAcGirlAnchor[AG_MOUTHTALK][1]), AG_MOUTHTALK, p->mouth, 1e9f);
  }
}

static void ac_rrect(BYTE *pix, int W, int H, float x0, float y0, float x1, float y1, float r, COLORREF fill, float a);

/* точка холста → окно */
static void ag_pt(AgM m, float x, float y, float *out) {
  out[0] = m.a * x + m.c * y + m.e;
  out[1] = m.b * x + m.d * y + m.f;
}

/* холст → окно без позы: середина по ширине, подошвы на AC_GIRL_FEET высоты */
static AgM ag_canvas(void) {
  float s = g_acAvatarSize / AC_GIRL_BOX;
  AgM m = {s, 0, 0, s, g_acAvatarSize * 0.5f - kAgFeet[0] * s, g_acAvatarSize * AC_GIRL_FEET - kAgFeet[1] * s};
  return m;
}

/* кадр кошко-девочки в g_acAvatarFramePixels (окно уже очищено) */
static void ac_girl_render(DWORD now, const AcXf *xf) {
  int S = g_acAvatarSize;
  g_acGirlLevel = S / AC_GIRL_BOX > AC_GIRL_K0 * 0.52f ? 0 : 1;
  int emo = ac_emo_now(now);
  float t;
  int act = ac_girl_action(now, emo, &t);
  if (act != g_agAct) { /* новое действие — смешиваем от последней позы */
    g_agFrom = g_agLast;
    g_agBlendAt = now | 1;
    g_agAct = act;
  }
  AgPose p;
  ag_pose(act, t, g_acAvatarLookX, &p);
  /* эмоции поверх действия */
  if (emo == AC_EMO_LOVE || g_acAvatarJoyUntil) { p.joy = fmaxf(p.joy, emo == AC_EMO_LOVE ? 1.0f : 0.0f); p.blink = 1.0f; }
  if (emo == AC_EMO_WINK || emo == AC_EMO_SHY) p.blink = 1.0f;
  if (emo == AC_EMO_SHY) { p.head -= 0.08f; p.earL -= 0.12f; p.earR += 0.12f; }
  if (emo == AC_EMO_SURPRISED) { p.earL += 0.12f; p.earR -= 0.12f; p.blink = 0.0f; }
  if (emo == AC_EMO_ANGRY) { p.earL -= 0.2f; p.earR += 0.2f; p.head += 0.05f; p.tail += 0.25f * sinf(now / 90.0f); }
  if (emo == AC_EMO_DIZZY) { p.head += 0.12f * sinf(now / 120.0f); p.blink = 1.0f; }
  if ((g_acAvatarState == AC_AV_TALKING || g_acAvatarState == AC_AV_TELL) && act == AGA_TALK && !emo) p.mouth = 0.94f;
  if (g_acHeld) { /* держат: правая рука тянется к курсору, левая отведена */
    AgM inv = ag_canvas();
    float gx = (g_acGrip.x - inv.e) / inv.a, gy = (g_acGrip.y - inv.f) / inv.d;
    ag_ik(kAgShoulderR[0], kAgShoulderR[1], gx, gy, -1.0f, &p.rightA, &p.rightB);
    p.open = 1.0f;
    p.point = 0.0f;
    p.leftA = -0.35f;
    p.leftB = -0.5f;
  }
  if (g_agBlendAt) {
    float k = ag_smooth((now - g_agBlendAt) / 280.0f);
    if (k >= 1.0f) g_agBlendAt = 0;
    else {
      float *a = (float *)&g_agFrom, *b = (float *)&p;
      for (int i = 0; i < AG_FIELDS; i++) b[i] = a[i] + (b[i] - a[i]) * k;
    }
  }
  g_agLast = p;

  /* окно: поза AcXf (дыхание окна, прыжки, раскачивание) поверх холста */
  AgM m = {1, 0, 0, 1, xf->pvx + xf->ox, xf->pvy + xf->oy};
  m = ag_r(m, atan2f(xf->s, xf->c));
  m = ag_s(m, xf->sx, xf->sy);
  m = ag_t(m, -xf->pvx, -xf->pvy);
  m = ag_mul(m, ag_canvas());
  /* поза тела: прыжок, наклон и «дыхание» вокруг подошв */
  m = ag_t(m, 0.0f, p.y);
  m = ag_t(m, kAgFeet[0], kAgFeet[1]);
  m = ag_r(m, p.lean);
  m = ag_s(m, p.sx, p.sy);
  m = ag_t(m, -kAgFeet[0], -kAgFeet[1]);

  memset(g_acAvatarFramePixels, 0, (size_t)S * S * 4);
  ag_asset(m, AG_TAIL, kAgTail[0], kAgTail[1], AG_TAIL_SCALE, p.tail, 1.0f);
  ag_head(m, &p, FALSE);
  const float *sl = kAgShoulderL, *sr = kAgShoulderR;
  float el[2] = {sl[0] + sinf(p.leftA) * AG_UPPER, sl[1] + cosf(p.leftA) * AG_UPPER};
  float er[2] = {sr[0] + sinf(p.rightA) * AG_UPPER, sr[1] + cosf(p.rightA) * AG_UPPER};
  ag_bone(m, AG_UPPERL, sl[0], sl[1], p.leftA, AG_UPPER, 1.0f);
  ag_bone(m, AG_UPPERR, sr[0], sr[1], p.rightA, AG_UPPER, 1.0f);
  ag_asset(m, AG_BODY, kAgBody[0], kAgBody[1], AG_BODY_SCALE, 0.0f, 1.0f);
  ag_asset(m, AG_SHOULDER, sl[0], sl[1] - 3.0f, 1.0f, 0.0f, 1.0f);
  ag_asset(m, AG_SHOULDER, sr[0], sr[1] - 3.0f, 1.0f, 0.0f, 1.0f);
  ag_head(m, &p, TRUE);
  float scale = sqrtf(fabsf(m.a * m.d - m.b * m.c)); /* пикселей окна на единицу холста */
  if (p.paper > 0.001f) { /* лист в руках: бумага, красная метка и строки */
    float c[2];
    ag_pt(m, 160.0f, 267.0f + 2.0f * sinf(now * 6.2831853f / 3400.0f), c);
    float w = 21.0f * scale, h = 24.0f * scale;
    ac_rrect(g_acAvatarFramePixels, S, S, c[0] - w - 1, c[1] - h - 1, c[0] + w + 1, c[1] + h + 1, 3.0f * scale + 1,
             RGB(98, 107, 128), p.paper);
    ac_rrect(g_acAvatarFramePixels, S, S, c[0] - w, c[1] - h, c[0] + w, c[1] + h, 3.0f * scale, RGB(243, 241, 237), p.paper);
    float mark[4] = {c[0] - 13 * scale, c[1] - 12 * scale, c[0], c[1] - 12 * scale};
    ac_paint_stroke(g_acAvatarFramePixels, S, S, mark, 2, 1.5f * scale, RGB(184, 66, 87), RGB(184, 66, 87), 0.0f, p.paper);
    for (int i = 0; i < 4; i++) {
      float ln[4] = {c[0] - 13 * scale, c[1] + (-2 + i * 6) * scale, c[0] + (12 - (i % 2) * 6) * scale, c[1] + (-2 + i * 6) * scale};
      ac_paint_stroke(g_acAvatarFramePixels, S, S, ln, 2, 0.7f * scale, RGB(137, 147, 162), RGB(137, 147, 162), 0.0f, p.paper);
    }
  }
  ag_bone(m, AG_LOWERL, el[0], el[1], p.leftB, AG_LOWER, 1.0f);
  float open = ag_clamp(p.open, 0.0f, 1.0f), point = ag_clamp(p.point, 0.0f, 1.0f), normal = ag_clamp(1.0f - open - point, 0.0f, 1.0f);
  ag_bone(m, AG_LOWERR, er[0], er[1], p.rightB, AG_LOWER, normal);
  ag_bone(m, AG_LOWEROPEN, er[0], er[1], p.rightB, AG_LOWER, open);
  ag_bone(m, AG_LOWERPOINT, er[0], er[1], p.rightB, AG_LOWER, point);
  float sec = now / 1000.0f;
  if (p.joy > 0.1f) { /* сердечки поднимаются над головой */
    for (int i = 0; i < 3; i++) {
      float u = fmodf(sec * 0.55f + i * 0.31f, 1.0f), c[2], pts[50];
      ag_pt(m, 95.0f + i * 64.0f + 9.0f * sinf(u * 6.2831853f), 120.0f - u * 60.0f, c);
      for (int k = 0; k <= 24; k++) {
        float a = 6.2831853f * k / 24, st = sinf(a);
        pts[2 * k] = c[0] + 16.0f * st * st * st * scale * 0.42f;
        pts[2 * k + 1] = c[1] - (13.0f * cosf(a) - 5.0f * cosf(2 * a) - 2.0f * cosf(3 * a) - cosf(4 * a)) * scale * 0.42f;
      }
      COLORREF pink = RGB(232, 145, 163);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 25, 2.6f * scale, pink, pink, 0.0f, sinf(u * 3.14159265f) * p.joy * 0.8f);
    }
  }
  if (p.zzz > 0.1f) { /* «z» поднимаются от головы */
    for (int i = 0; i < 2; i++) {
      float c[2], k = (i ? 5.0f : 7.0f) * scale, pts[8];
      ag_pt(m, 217.0f + i * 18.0f, 82.0f - i * 17.0f + 3.0f * sinf(sec * 2.0f + i * 0.6f), c);
      pts[0] = c[0] - k; pts[1] = c[1] - k; pts[2] = c[0] + k; pts[3] = c[1] - k;
      pts[4] = c[0] - k; pts[5] = c[1] + k; pts[6] = c[0] + k; pts[7] = c[1] + k;
      COLORREF zc = RGB(150, 163, 196);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 4, 1.1f * scale + 0.3f, zc, zc, 0.0f, 0.75f * p.zzz);
    }
  }
}

/* макушка и середина головы в окне — пузырь над ней */
static void ac_girl_head(int *x, int *top) {
  AgM m = ag_canvas();
  float c[2];
  ag_pt(m, 160.0f, 40.0f, c);
  *x = (int)c[0];
  *top = (int)c[1];
}
