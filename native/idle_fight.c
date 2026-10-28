/* Бой Годжо и Сукуны при бездействии (с 2026.10.15.1).

   Мышь и клавиатуру не трогали дольше, чем задано в Настройках (по умолчанию
   минуту), — поверх экрана выходят Годжо и Сукуна и дерутся: сходятся, бьют
   связками, подбрасывают и добивают в воздухе, ставят блок, применяют техники
   из аниме и расширяют территорию. У каждого полоска жизни и проклятой
   энергии; кто выбил другого — выиграл раунд, оба лечатся обратной техникой,
   и начинается следующий. Так без конца, пока мышь не шевельнётся или не
   нажмут клавишу — тогда бой исчезает сразу.

   Техники. Годжо: Бесконечность (удар Сукуны застревает в воздухе), Синий
   (притягивает), Красный (отталкивает взрывом), Мнимая техника: Фиолетовый,
   телепорт за спину, Расширение территории: Безграничная пустота (противник
   замирает). Сукуна: Рассечение (невидимые лезвия), Разруб (крест по
   цели), Разверзнись — огненная стрела, Расширение территории: Святилище
   злобы (режет всё вокруг). Обоим выпадает Чёрная вспышка.

   Свой поток и своё окно: главный поток программы держит перехват мыши, и
   рисование боя ему мешать не должно. Окно прозрачно для мыши, поверх всех,
   на все мониторы сразу, по рабочим областям: панель задач им как земля, низ
   каждого монитора — свой пол, верх открытого окна — площадка. */

#include "fight_art.c"

/* ---- случайные числа: свои у боя ---- */
static unsigned if_rnd(unsigned *s) {
  unsigned x = *s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return *s = x ? x : 0x9E3779B9u;
}
static float if_rf(unsigned *s) { return (if_rnd(s) >> 8) * (1.0f / 16777216.0f); }
static float if_rr(unsigned *s, float a, float b) { return a + (b - a) * if_rf(s); }

/* ===================== анимации ===================== */

enum { EZ_SMOOTH, EZ_OUT, EZ_LIN };
typedef struct {
  unsigned char pose, dur, ease;
} FKey;
typedef struct {
  const FKey *k;
  unsigned char n, loop; /* loop — с какого ключа повторять; его поза = поза последнего */
} FAnim;
#define NOLOOP 255

static const FKey kAnHurtH[] = {{FP_HURT_H, 4, EZ_OUT}, {FP_HURT_H, 14, EZ_LIN}};
static const FKey kAnHurtM[] = {{FP_HURT_M, 4, EZ_OUT}, {FP_HURT_M, 16, EZ_LIN}};
static const FKey kAnFly[] = {{FP_FLY, 6, EZ_OUT}};
static const FKey kAnDown[] = {{FP_DOWN, 7, EZ_OUT}};
static const FKey kAnGetup[] = {{FP_KNEEL, 14, EZ_SMOOTH}, {FP_LAND, 10, EZ_SMOOTH}};
static const FKey kAnGuard[] = {{FP_GUARD, 5, EZ_OUT}};
static const FKey kAnStun[] = {{FP_STUN, 12, EZ_SMOOTH}};
static const FKey kAnKo[] = {{FP_KO, 12, EZ_SMOOTH}};
static const FKey kAnJump[] = {{FP_JSQUAT, 5, EZ_OUT}, {FP_JRISE, 8, EZ_OUT}, {FP_JTUCK, 12, EZ_SMOOTH}, {FP_JFALL, 14, EZ_SMOOTH}};
static const FKey kAnLand[] = {{FP_LAND, 3, EZ_OUT}, {FP_LAND, 6, EZ_LIN}};
static const FKey kAnGIdle[] = {{FP_G_IDLE, 8, EZ_SMOOTH}, {FP_G_IDLE2, 40, EZ_SMOOTH}, {FP_G_IDLE, 40, EZ_SMOOTH}};
static const FKey kAnGWalk[] = {{FP_G_WALK1, 6, EZ_SMOOTH}, {FP_G_WALK2, 9, EZ_SMOOTH}, {FP_G_WALK3, 9, EZ_SMOOTH},
                                {FP_G_WALK4, 9, EZ_SMOOTH}, {FP_G_WALK1, 9, EZ_SMOOTH}};
static const FKey kAnGBack[] = {{FP_G_WALK1, 6, EZ_SMOOTH}, {FP_G_WALK4, 10, EZ_SMOOTH}, {FP_G_WALK3, 10, EZ_SMOOTH},
                                {FP_G_WALK2, 10, EZ_SMOOTH}, {FP_G_WALK1, 10, EZ_SMOOTH}};
static const FKey kAnGDash[] = {{FP_G_DASH, 6, EZ_OUT}};
static const FKey kAnGRun[] = {{FP_G_RUN1, 5, EZ_SMOOTH}, {FP_G_RUN2, 7, EZ_SMOOTH}, {FP_G_RUN1, 7, EZ_SMOOTH}};
static const FKey kAnGJab[] = {{FP_G_JAB0, 4, EZ_OUT}, {FP_G_JAB, 3, EZ_OUT}, {FP_G_JAB, 6, EZ_LIN}, {FP_G_STANCE, 8, EZ_SMOOTH}};
static const FKey kAnGPalm[] = {{FP_G_PALM0, 5, EZ_SMOOTH}, {FP_G_PALM, 3, EZ_OUT}, {FP_G_PALM, 8, EZ_LIN}, {FP_G_STANCE, 9, EZ_SMOOTH}};
static const FKey kAnGKick[] = {{FP_G_KICK0, 6, EZ_SMOOTH}, {FP_G_KICK, 3, EZ_OUT}, {FP_G_KICK, 8, EZ_LIN}, {FP_G_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnGHigh[] = {{FP_G_HIGH0, 6, EZ_SMOOTH}, {FP_G_HIGH, 4, EZ_OUT}, {FP_G_HIGH, 8, EZ_LIN}, {FP_G_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnGUp[] = {{FP_G_UPKICK0, 6, EZ_SMOOTH}, {FP_G_UPKICK, 4, EZ_OUT}, {FP_G_UPKICK, 9, EZ_LIN}, {FP_G_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnGFlick[] = {{FP_G_FLICK0, 7, EZ_SMOOTH}, {FP_G_FLICK, 2, EZ_OUT}, {FP_G_FLICK, 12, EZ_LIN}, {FP_G_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnGDive[] = {{FP_G_DIVE, 5, EZ_OUT}};
static const FKey kAnGBlue[] = {{FP_G_BLUE, 8, EZ_SMOOTH}, {FP_G_BLUE, 50, EZ_LIN}, {FP_G_IDLE, 12, EZ_SMOOTH}};
static const FKey kAnGRed[] = {{FP_G_RED0, 8, EZ_SMOOTH}, {FP_G_RED0, 22, EZ_LIN}, {FP_G_RED, 3, EZ_OUT}, {FP_G_RED, 22, EZ_LIN}, {FP_G_IDLE, 12, EZ_SMOOTH}};
static const FKey kAnGPurple[] = {{FP_G_PUR_A, 10, EZ_SMOOTH}, {FP_G_PUR_A, 40, EZ_LIN}, {FP_G_PUR_B, 10, EZ_SMOOTH}, {FP_G_PUR_B, 26, EZ_LIN},
                                  {FP_G_PUR_C, 4, EZ_OUT}, {FP_G_PUR_C, 36, EZ_LIN}, {FP_G_IDLE, 14, EZ_SMOOTH}};
static const FKey kAnGSeal[] = {{FP_G_UNBLIND, 10, EZ_SMOOTH}, {FP_G_UNBLIND, 10, EZ_LIN}, {FP_G_SEAL, 10, EZ_SMOOTH}, {FP_G_SEAL, 70, EZ_LIN}};
static const FKey kAnGTele[] = {{FP_G_TELE, 4, EZ_OUT}};
static const FKey kAnGTaunt[] = {{FP_G_TAUNT, 12, EZ_SMOOTH}, {FP_G_TAUNT, 50, EZ_LIN}, {FP_G_IDLE, 12, EZ_SMOOTH}};
static const FKey kAnGWin[] = {{FP_G_UNBLIND, 10, EZ_SMOOTH}, {FP_G_UNBLIND, 12, EZ_LIN}, {FP_G_WIN, 10, EZ_SMOOTH}};
static const FKey kAnSIdle[] = {{FP_S_IDLE, 8, EZ_SMOOTH}, {FP_S_IDLE2, 30, EZ_SMOOTH}, {FP_S_IDLE, 30, EZ_SMOOTH}};
static const FKey kAnSWalk[] = {{FP_S_WALK1, 6, EZ_SMOOTH}, {FP_S_WALK2, 9, EZ_SMOOTH}, {FP_S_WALK3, 9, EZ_SMOOTH},
                                {FP_S_WALK4, 9, EZ_SMOOTH}, {FP_S_WALK1, 9, EZ_SMOOTH}};
static const FKey kAnSBack[] = {{FP_S_WALK1, 6, EZ_SMOOTH}, {FP_S_WALK4, 10, EZ_SMOOTH}, {FP_S_WALK3, 10, EZ_SMOOTH},
                                {FP_S_WALK2, 10, EZ_SMOOTH}, {FP_S_WALK1, 10, EZ_SMOOTH}};
static const FKey kAnSRun[] = {{FP_S_RUN1, 5, EZ_SMOOTH}, {FP_S_RUN2, 7, EZ_SMOOTH}, {FP_S_RUN1, 7, EZ_SMOOTH}};
static const FKey kAnSClaw[] = {{FP_S_CLAW0, 6, EZ_SMOOTH}, {FP_S_CLAW, 3, EZ_OUT}, {FP_S_CLAW, 7, EZ_LIN}, {FP_S_IDLE, 9, EZ_SMOOTH}};
static const FKey kAnSSwipe[] = {{FP_S_SWIPE0, 5, EZ_SMOOTH}, {FP_S_SWIPE, 3, EZ_OUT}, {FP_S_SWIPE, 7, EZ_LIN}, {FP_S_IDLE, 9, EZ_SMOOTH}};
static const FKey kAnSPunch[] = {{FP_S_PUNCH0, 5, EZ_SMOOTH}, {FP_S_PUNCH, 3, EZ_OUT}, {FP_S_PUNCH, 8, EZ_LIN}, {FP_S_IDLE, 9, EZ_SMOOTH}};
static const FKey kAnSKnee[] = {{FP_S_IDLE, 3, EZ_SMOOTH}, {FP_S_KNEE, 4, EZ_OUT}, {FP_S_KNEE, 8, EZ_LIN}, {FP_S_IDLE, 9, EZ_SMOOTH}};
static const FKey kAnSUpper[] = {{FP_S_UPPER0, 6, EZ_SMOOTH}, {FP_S_UPPER, 4, EZ_OUT}, {FP_S_UPPER, 10, EZ_LIN}, {FP_S_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnSKick[] = {{FP_S_KICK0, 6, EZ_SMOOTH}, {FP_S_KICK, 4, EZ_OUT}, {FP_S_KICK, 8, EZ_LIN}, {FP_S_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnSSlam[] = {{FP_S_SLAM0, 6, EZ_SMOOTH}, {FP_S_SLAM0, 4, EZ_LIN}, {FP_S_SLAM, 4, EZ_OUT}};
static const FKey kAnSDism[] = {{FP_S_DISM0, 8, EZ_SMOOTH}, {FP_S_DISM0, 8, EZ_LIN}, {FP_S_DISM, 3, EZ_OUT}, {FP_S_DISM, 10, EZ_LIN},
                                {FP_S_DISM0, 6, EZ_OUT}, {FP_S_DISM, 3, EZ_OUT}, {FP_S_DISM, 14, EZ_LIN}, {FP_S_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnSCleave[] = {{FP_S_SWIPE0, 6, EZ_SMOOTH}, {FP_S_CLEAVE, 3, EZ_OUT}, {FP_S_CLEAVE, 18, EZ_LIN}, {FP_S_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnSFuga[] = {{FP_S_FUGA_A, 10, EZ_SMOOTH}, {FP_S_FUGA_A, 40, EZ_LIN}, {FP_S_FUGA_B, 12, EZ_SMOOTH}, {FP_S_FUGA_B, 30, EZ_LIN},
                                {FP_S_FUGA_C, 3, EZ_OUT}, {FP_S_FUGA_C, 24, EZ_LIN}, {FP_S_IDLE, 12, EZ_SMOOTH}};
static const FKey kAnSSeal[] = {{FP_S_SEAL, 12, EZ_SMOOTH}, {FP_S_SEAL, 70, EZ_LIN}};
static const FKey kAnSTaunt[] = {{FP_S_TAUNT, 10, EZ_SMOOTH}, {FP_S_TAUNT, 40, EZ_LIN}, {FP_S_IDLE, 10, EZ_SMOOTH}};
static const FKey kAnSWin[] = {{FP_S_LAUGH, 12, EZ_SMOOTH}, {FP_S_TAUNT, 26, EZ_SMOOTH}, {FP_S_LAUGH, 26, EZ_SMOOTH}};
static const FKey kAnGRct[] = {{FP_G_RCT, 10, EZ_SMOOTH}, {FP_G_RCT, 50, EZ_LIN}};
static const FKey kAnSSummon[] = {{FP_S_SUMMON, 10, EZ_SMOOTH}, {FP_S_SUMMON, 60, EZ_LIN}};

enum {
  AN_HURT_H, AN_HURT_M, AN_FLY, AN_DOWN, AN_GETUP, AN_GUARD, AN_STUN, AN_KO, AN_JUMP, AN_LAND,
  AN_G_IDLE, AN_G_WALK, AN_G_BACK, AN_G_DASH, AN_G_RUN, AN_G_JAB, AN_G_PALM, AN_G_KICK, AN_G_HIGH, AN_G_UP,
  AN_G_FLICK, AN_G_DIVE, AN_G_BLUE, AN_G_RED, AN_G_PURPLE, AN_G_SEAL, AN_G_TELE, AN_G_TAUNT, AN_G_WIN, AN_G_RCT,
  AN_S_IDLE, AN_S_WALK, AN_S_BACK, AN_S_RUN, AN_S_CLAW, AN_S_SWIPE, AN_S_PUNCH, AN_S_KNEE, AN_S_UPPER,
  AN_S_KICK, AN_S_SLAM, AN_S_DISM, AN_S_CLEAVE, AN_S_FUGA, AN_S_SEAL, AN_S_TAUNT, AN_S_WIN, AN_S_SUMMON,
  AN_COUNT
};
#define AN(arr, lp) {arr, (unsigned char)(sizeof(arr) / sizeof(arr[0])), lp}
static const FAnim kAnims[AN_COUNT] = {
    AN(kAnHurtH, NOLOOP), AN(kAnHurtM, NOLOOP), AN(kAnFly, NOLOOP), AN(kAnDown, NOLOOP), AN(kAnGetup, NOLOOP),
    AN(kAnGuard, NOLOOP), AN(kAnStun, NOLOOP), AN(kAnKo, NOLOOP), AN(kAnJump, NOLOOP), AN(kAnLand, NOLOOP),
    AN(kAnGIdle, 0), AN(kAnGWalk, 0), AN(kAnGBack, 0), AN(kAnGDash, NOLOOP), AN(kAnGRun, 0), AN(kAnGJab, NOLOOP),
    AN(kAnGPalm, NOLOOP), AN(kAnGKick, NOLOOP), AN(kAnGHigh, NOLOOP), AN(kAnGUp, NOLOOP), AN(kAnGFlick, NOLOOP),
    AN(kAnGDive, NOLOOP), AN(kAnGBlue, NOLOOP), AN(kAnGRed, NOLOOP), AN(kAnGPurple, NOLOOP), AN(kAnGSeal, NOLOOP),
    AN(kAnGTele, NOLOOP), AN(kAnGTaunt, NOLOOP), AN(kAnGWin, NOLOOP), AN(kAnGRct, NOLOOP),
    AN(kAnSIdle, 0), AN(kAnSWalk, 0), AN(kAnSBack, 0), AN(kAnSRun, 0), AN(kAnSClaw, NOLOOP), AN(kAnSSwipe, NOLOOP),
    AN(kAnSPunch, NOLOOP), AN(kAnSKnee, NOLOOP), AN(kAnSUpper, NOLOOP), AN(kAnSKick, NOLOOP), AN(kAnSSlam, NOLOOP),
    AN(kAnSDism, NOLOOP), AN(kAnSCleave, NOLOOP), AN(kAnSFuga, NOLOOP), AN(kAnSSeal, NOLOOP), AN(kAnSTaunt, NOLOOP),
    AN(kAnSWin, 0), AN(kAnSSummon, NOLOOP),
};

static float if_ease(int e, float t) {
  if (e == EZ_OUT) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
  }
  if (e == EZ_LIN) return t;
  return t * t * (3.0f - 2.0f * t);
}

/* поза анимации a через t кадров от начала; from — поза в миг смены анимации */
static void if_anim_pose(FPose *out, int a, float t, const FPose *from) {
  const FAnim *A = &kAnims[a];
  const FPose *prev = from;
  for (int i = 0; i < A->n; i++) {
    float d = A->k[i].dur;
    const FPose *to = &kPoses[A->k[i].pose];
    if (t < d) {
      fa_tween(out, prev, to, if_ease(A->k[i].ease, t / d));
      return;
    }
    t -= d;
    prev = to;
  }
  if (A->loop != NOLOOP && A->loop + 1 < A->n) {
    float cyc = 0;
    for (int j = A->loop + 1; j < A->n; j++) cyc += A->k[j].dur;
    t = fmodf(t, cyc);
    prev = &kPoses[A->k[A->loop].pose];
    for (int j = A->loop + 1; j < A->n; j++) {
      float d = A->k[j].dur;
      const FPose *to = &kPoses[A->k[j].pose];
      if (t < d) {
        fa_tween(out, prev, to, if_ease(A->k[j].ease, t / d));
        return;
      }
      t -= d;
      prev = to;
    }
  }
  fa_tween(out, prev, prev, 0.0f);
}

/* ===================== приёмы ===================== */

enum {
  MV_G_JAB, MV_G_PALM, MV_G_KICK, MV_G_HIGH, MV_G_UPKICK, MV_G_FLICK, MV_G_DIVE,
  MV_G_BLUE, MV_G_RED, MV_G_PURPLE, MV_G_TELE, MV_G_VOID, MV_G_TAUNT, MV_G_REDX, MV_G_LUNGE, MV_G_RCT,
  MV_S_CLAW, MV_S_SWIPE, MV_S_PUNCH, MV_S_KNEE, MV_S_UPPER, MV_S_KICK, MV_S_SLAM,
  MV_S_DISM, MV_S_CLEAVE, MV_S_FUGA, MV_S_SHRINE, MV_S_TAUNT, MV_S_FAN, MV_S_LUNGE, MV_S_WORLD, MV_S_MAHO,
  MV_G_HEAL, MV_S_RCT, /* обратная техника на грани нокаута: лечит (if_try_rct) */
  MV_COUNT
};
enum { HK_LIGHT, HK_HEAVY, HK_LAUNCH, HK_KNOCK, HK_BLAST };
#define MVF_TECH 1  /* техника: надпись и энергия */
#define MVF_AIR 2   /* в воздухе: длится до земли */
#define MVF_MELEE 4 /* удар рукой или ногой: может связаться в серию */
#define MVF_LUNGE 8 /* рывок: бьёт издали, на ходу */
typedef struct {
  short anim, len, hitA, hitB;
  unsigned char joint, kind, flags;
  short dmg, ce, cd, cancel;
  float reach, kx, ky, step;
  const wchar_t *name;
} FMove;

static const FMove kMoves[MV_COUNT] = {
    /* Годжо */
    {AN_G_JAB, 21, 6, 10, FJ_HAND0, HK_LIGHT, MVF_MELEE, 34, 0, 0, 11, 7.5f, 1.4f, 0, 1.6f, NULL},
    {AN_G_PALM, 25, 7, 11, FJ_HAND1, HK_HEAVY, MVF_MELEE, 50, 0, 0, 13, 8, 3.2f, 0.5f, 2.0f, NULL},
    {AN_G_KICK, 27, 8, 12, FJ_FOOT1, HK_HEAVY, MVF_MELEE, 60, 0, 0, 14, 9, 4.2f, 0.8f, 1.2f, NULL},
    {AN_G_HIGH, 28, 9, 13, FJ_FOOT1, HK_KNOCK, MVF_MELEE, 76, 0, 0, 15, 9, 6.0f, 4.5f, 0.8f, NULL},
    {AN_G_UP, 29, 9, 13, FJ_FOOT1, HK_LAUNCH, MVF_MELEE, 66, 0, 0, 14, 9, 1.2f, 9.5f, 0.6f, NULL},
    {AN_G_FLICK, 31, 8, 11, FJ_HAND1, HK_BLAST, MVF_MELEE, 55, 40, 360, 99, 7, 10.5f, 3.5f, 0.4f, NULL},
    {AN_G_DIVE, 90, 0, 90, FJ_FOOT1, HK_KNOCK, MVF_MELEE | MVF_AIR, 60, 0, 120, 99, 9.5f, 3.5f, 3.0f, 0, NULL},
    {AN_G_BLUE, 70, 0, 0, 0, 0, MVF_TECH, 0, 180, 600, 99, 0, 0, 0, 0, L"Синий"},
    {AN_G_RED, 67, 0, 0, 0, 0, MVF_TECH, 0, 220, 540, 99, 0, 0, 0, 0, L"Красный"},
    {AN_G_PURPLE, 140, 0, 0, 0, 0, MVF_TECH, 0, 550, 1500, 99, 0, 0, 0, 0, L"Мнимая техника: Фиолетовый"},
    {AN_G_TELE, 20, 0, 0, 0, 0, 0, 0, 120, 360, 99, 0, 0, 0, 0, NULL},
    {AN_G_SEAL, 100, 0, 0, 0, 0, MVF_TECH, 0, 700, 30000, 99, 0, 0, 0, 0, L"Безграничная пустота"},
    {AN_G_TAUNT, 74, 0, 0, 0, 0, 0, 0, 0, 900, 99, 0, 0, 0, 0, NULL},
    {AN_G_RED, 67, 0, 0, 0, 0, MVF_TECH, 0, 200, 720, 99, 0, 0, 0, 0, L"Красный: залп"},
    {AN_G_KICK, 27, 8, 12, FJ_FOOT1, HK_HEAVY, MVF_MELEE | MVF_LUNGE, 62, 0, 200, 14, 9, 4.8f, 1.2f, 9.0f, NULL},
    {AN_G_RCT, 56, 0, 0, 0, 0, 0, 0, 0, 30000, 99, 0, 0, 0, 0, L"Обратная техника"},
    /* Сукуна */
    {AN_S_CLAW, 25, 7, 10, FJ_HAND0, HK_LIGHT, MVF_MELEE, 35, 0, 0, 11, 8, 1.5f, 0, 1.8f, NULL},
    {AN_S_SWIPE, 24, 6, 9, FJ_HAND1, HK_LIGHT, MVF_MELEE, 35, 0, 0, 10, 8, 1.8f, 0, 1.5f, NULL},
    {AN_S_PUNCH, 25, 6, 10, FJ_HAND1, HK_HEAVY, MVF_MELEE, 50, 0, 0, 12, 7.5f, 3.6f, 0.4f, 2.6f, NULL},
    {AN_S_KNEE, 24, 5, 9, FJ_KNEE1, HK_HEAVY, MVF_MELEE, 45, 0, 0, 11, 8, 2.2f, 2.5f, 1.2f, NULL},
    {AN_S_UPPER, 30, 8, 13, FJ_HAND1, HK_LAUNCH, MVF_MELEE, 60, 0, 0, 14, 8, 1.2f, 10.0f, 1.0f, NULL},
    {AN_S_KICK, 28, 8, 12, FJ_FOOT1, HK_KNOCK, MVF_MELEE, 65, 0, 0, 15, 9, 6.2f, 3.5f, 0.8f, NULL},
    {AN_S_SLAM, 90, 10, 90, FJ_HAND0, HK_KNOCK, MVF_MELEE | MVF_AIR, 70, 0, 120, 99, 10, 2.5f, -5.0f, 0, NULL},
    {AN_S_DISM, 62, 0, 0, 0, 0, MVF_TECH, 0, 150, 420, 99, 0, 0, 0, 0, L"Рассечение"},
    {AN_S_CLEAVE, 37, 8, 12, FJ_HAND1, HK_KNOCK, MVF_TECH, 100, 180, 480, 99, 12, 5.5f, 4.5f, 2.4f, L"Разруб"},
    {AN_S_FUGA, 131, 0, 0, 0, 0, MVF_TECH, 0, 500, 1500, 99, 0, 0, 0, 0, L"Разверзнись!"},
    {AN_S_SEAL, 82, 0, 0, 0, 0, MVF_TECH, 0, 700, 30000, 99, 0, 0, 0, 0, L"Святилище злобы"},
    {AN_S_TAUNT, 60, 0, 0, 0, 0, 0, 0, 0, 900, 99, 0, 0, 0, 0, NULL},
    {AN_S_DISM, 62, 0, 0, 0, 0, MVF_TECH, 0, 220, 780, 99, 0, 0, 0, 0, L"Рассечение: веер"},
    {AN_S_PUNCH, 25, 6, 10, FJ_HAND1, HK_HEAVY, MVF_MELEE | MVF_LUNGE, 55, 0, 200, 12, 7.5f, 4.2f, 0.8f, 10.0f, NULL},
    {AN_S_CLEAVE, 37, 0, 0, 0, 0, MVF_TECH, 0, 450, 1800, 99, 0, 0, 0, 0, L"Разрез мира"},
    {AN_S_SUMMON, 70, 0, 0, 0, 0, 0, 0, 0, 30000, 99, 0, 0, 0, 0, L"Десять теней: Махорага"},
    /* обратная техника: Годжо — ладонь у виска, Сукуна — знак */
    {AN_G_RCT, 56, 0, 0, 0, 0, 0, 0, 120, 600, 99, 0, 0, 0, 0, L"Обратная техника"},
    {AN_S_SEAL, 56, 0, 0, 0, 0, 0, 0, 120, 600, 99, 0, 0, 0, 0, L"Обратная техника"},
};

/* ===================== мир ===================== */

enum { FS_IDLE, FS_WALK, FS_RUN, FS_JUMP, FS_MOVE, FS_HIT, FS_FLY, FS_DOWN, FS_GETUP, FS_GUARD, FS_STUN, FS_KO, FS_WIN,
       FS_TELE, FS_LAND, FS_TRAP };
enum { IN_NONE, IN_NEAR, IN_BACK, IN_WAIT, IN_HOME };
#define IF_TRAIL 7

typedef struct {
  int who, f;
  float x, y, vx, vy;    /* единицы роста; y — над землёй */
  int st;
  float stT;
  int anim;
  float at;
  FPose from, pose;
  FSkel sk;
  int mv, mvHit, chain;
  float mt;
  float hp, ghost, ghostT, ce;
  float stun, roll, vroll, flash, think, alpha;
  int intent;
  float intentT, intentD;
  float cd[MV_COUNT], used[MV_COUNT];
  int blind, power, juggle, tellInf, ultUsed;
  int mvId, seenId;      /* номер начатого приёма; на какой удар противника уже реагировал */
  float stuck, inv, wind, burn, infT, home;
  float fl, dropT, homeFl; /* пол под ногами (верх площадки); спрыгивает с окна; пол дома */
  float wallT;             /* сколько упирается в стену по пути */
  float healR;              /* обратная техника: сколько здоровья в кадр */
  int rctN, rctTry;         /* сколько раз лечился в раунде; на каком пороге уже решал, лечиться ли */
  float trail[IF_TRAIL][2]; /* след удара: точки сустава, единицы мира */
  int trailN;
} IfFighter;

enum { PJ_NONE, PJ_BLUE, PJ_RED, PJ_PURPLE, PJ_SLASH, PJ_ARROW };
typedef struct {
  int kind, owner, hit, seen; /* seen — противник уже решал, уворачиваться ли */
  float x, y, vx, vy, r, t, life, a;
} IfProj;

enum { PT_SPARK, PT_DOT, PT_FIRE, PT_SMOKE, PT_DUST, PT_SHARD, PT_STAR, PT_BIT, PT_EMBER };
typedef struct {
  unsigned char kind, light;
  float x, y, vx, vy, t, life, sz, g, rot;
  FCol c;
} IfPart;

enum { EF_NONE, EF_SPARK, EF_GUARD, EF_INF, EF_BF, EF_CUTS, EF_CROSS, EF_RING, EF_FLASH, EF_BOOM, EF_REDBOOM,
       EF_CRACK, EF_AFTER, EF_TELE, EF_HEAL, EF_SCORCH, EF_IMPLODE, EF_CALL, EF_WALL, EF_WCUT };
typedef struct {
  int kind, owner;
  float x, y, t, life, sz, a, b;
  unsigned seed;
  FFig fig;
  const wchar_t *txt;
  FCol c;
} IfEff;

#define IF_MAXP 1100
#define IF_MAXE 96
#define IF_MAXJ 16
#define IF_HP 1000.0f
#define IF_CE 1000.0f

enum { RP_INTRO, RP_FIGHT, RP_KO, RP_WIN, RP_RESET };
enum { DOM_NONE, DOM_VOID, DOM_SHRINE, DOM_CLASH }; /* DOM_CLASH — оба купола разом, граница между ними */
enum { DP_CAST, DP_OPEN, DP_ON, DP_CLOSE, DP_BREAK };  /* DP_BREAK — купол проигравшего рушится */

/* Сценарий раунда — по канону боя Годжо с Сукуной в Синдзюку («Магическая битва», главы 221–236),
   в нашем пересказе. Акты идут по порядку, а кто победит, решает монетка в начале раунда (fate):
   SC_OPEN   — разминка: рукопашная у Бесконечности, Синий и Красный, Рассечение и Разруб;
   SC_CLASH  — оба раскрывают территории в один миг, купола сталкиваются; Годжо сужает барьер,
               и Пустота продавливает Святилище (как в каноне): купол Сукуны рушится, Сукуна на миг
               попадает под Пустоту, потом у обоих техники выгорают;
   SC_BURN   — чистая рукопашная без техник; Годжо чинит мозг обратной техникой;
   SC_VOID2  — Пустота во второй раз, уже один: Сукуна застывает — и призывает Махорагу
               Десятью тенями (чужая техника, она не выгорела); тень рвёт купол;
   SC_MAHO   — Махорага встаёт и сразу идёт на Годжо (прямой, сверху, размах, прыжок, апперкот, таран, Меч
               изгнания), Сукуна с ним заодно: подхватывает подброшенного, перепрыгивает великана и берёт
               Годжо в клещи; Годжо отбивается руками, Синим и Красным, уходит телепортом. Каждый удар,
               вставший в Бесконечности, поворачивает колесо (два шага колесо делает ещё в тени, до
               призыва); после четырёх удары проходят насквозь;
   SC_PURPLE — Годжо отходит и собирает Фиолетовый на всю мощь: сносит Махорагу и Сукуну;
   SC_WORLD  — Сукуна поднимается и режет мир. Монетка: попал — Годжо пополам; нет — Годжо исчез за
               миг до удара и добивает Чёрной вспышкой;
   SC_END    — исход решён, добивание.
   Упоров по здоровью нет: урон настоящий, и раунд может кончиться нокаутом в любом акте, хоть до
   территорий. На грани нокаута боец иногда (не всегда) лечится обратной техникой (if_try_rct);
   сценарий идёт дальше, только если оба выстояли. */
enum { SC_OPEN, SC_CLASH, SC_BURN, SC_VOID2, SC_MAHO, SC_PURPLE, SC_WORLD, SC_END };
#define IF_ADAPT 4
#define IF_MAHOHP 2000.0f /* здоровье Махораги: разбить можно и руками, но Фиолетовый — наверняка */
/* множитель урона по бойцам до развязки: темп раунда. В начале выше — нокаут возможен и до территорий,
   с Махорагой ниже — их двое против одного */
static const float kDmgAct[SC_END] = {0.9f, 0.8f, 0.8f, 0.7f, 0.6f, 0.6f, 0.6f};
#define IF_DMG (s->phase == RP_FIGHT && s->act < SC_END ? kDmgAct[s->act] : 1.0f)
#define IF_DMG_G 1.3f  /* по Годжо — чуть сильнее: Бесконечность и так гасит часть ударов, силы равны */
#define IF_MU 1.3f                     /* единиц мира в единице великана: ростом он в два с половиной раза выше бойцов */
/* Махорага: встаёт из тени, стоит, идёт, бьёт, вздрогнул, отброшен, закрылся наручами; приёмы: прямой, сверху,
   размах, прыжок, апперкот (подбрасывает — Сукуна добивает), таран плечом, Меч изгнания (лезвие на правой руке) */
enum { MS_RISE, MS_IDLE, MS_WALK, MS_ATK, MS_HIT, MS_STAGGER, MS_GUARD };
enum { MM_PUNCH, MM_SLAM, MM_SWEEP, MM_STOMP, MM_UPPER, MM_CHARGE, MM_BLADE, MM_COUNT };
static const float kMahoLen[MM_COUNT] = {36, 48, 42, 60, 40, 58, 54};      /* сколько длится приём */
static const float kMahoHitAt[MM_COUNT] = {14, 20, 16, 999, 15, 999, 22}; /* кадр удара (прыжок — при приземлении, таран — на бегу) */
typedef struct { /* скелет Махораги, единицы великана (IF_MU единиц мира), x — вперёд по взгляду */
  float lean, crouch;
  float hip[2][2], kn[2][2], ft[2][2]; /* 0 — дальняя нога, 1 — ближняя */
  float sh[2][2], el[2][2], hd[2][2];  /* плечи, локти, кулаки */
  float chest[2], neck[2], head[2];
} IfMahoPose;

typedef struct IfText IfText;

/* площадка: низ монитора (solid — сквозь не провалиться, сбоку — стена) или верх окна
   (только сверху: под ним проходят, с него спрыгивают). Единицы над низом холста */
typedef struct {
  float x0, x1, top, bot;
  int solid, wl, wr; /* wl, wr — у окна тут настоящий край: о него бьются */
} IfPlat;
#define IF_MAXPL 48

/* что на экране: мониторы и окна, px холста (холст — все мониторы вместе) */
#define IF_MAXMON 8
#define IF_MAXWIN 40
typedef struct {
  int nMon, prim, nWin;
  RECT mon[IF_MAXMON];
  RECT win[IF_MAXWIN]; /* сверху вниз по Z: первое — самое верхнее */
} IfStage;

typedef struct {
  FCv cv;        /* мир: маленький холст, пиксель-арт (k px экрана на точку) */
  FCv big;       /* окно: мир крупными пикселями, поверх — чёткие надписи и шкалы */
  int k;
  RECT prevS, prevO; /* что задели в прошлом кадре: мир (маленький) и надписи (большой) */
  int W, H;      /* холст мира, px */
  float U, gy;   /* px на единицу; экранный y земли */
  float xMin, xMax;
  float homeX, hudX, hudY; /* середина монитора, где идёт бой: туда возвращаются, там шкалы */
  float hudTop, hs;        /* верх этого монитора, px; размер надписей и шкал, px окна */
  int nMon, arena;         /* мониторы: края — единицы мира, верх и низ — px; на каком сейчас бой */
  float monL[IF_MAXMON], monR[IF_MAXMON], monT[IF_MAXMON], monB[IF_MAXMON];
  float roamT;             /* когда снова можно увести бой на другой монитор */
  int nPl, nFix;           /* сначала полы мониторов (nFix), потом окна */
  IfPlat pl[IF_MAXPL];
  unsigned seed;
  long frame;
  float dt, slowT, hitstop, shake, shx, shy;
  IfFighter F[2];
  IfProj pj[IF_MAXJ];
  IfPart pt[IF_MAXP];
  int ptNext;
  IfEff ef[IF_MAXE];
  int round, phase, winner;
  int *wins;     /* счёт: живёт дольше одного боя */
  float phT, roundT;
  /* территория */
  int dom, domOwner, domPhase;
  float domT, domX, domRx, domRy, domOpen; /* domOpen — сколько раскрыто, 0…1 */
  float domFl, domLen, domBurn;            /* на каком полу купол; сколько держится; выгорание хозяину после */
  unsigned char *bd, *bd2;                /* фон территории, BGRA с альфой; bd2 — Святилище при столкновении (bd — Пустота) */
  int bdX, bdY, bdW, bdH, bdRows, bdKind;
  /* сценарий раунда (см. SC_*) */
  int act, step, fate;      /* акт; шаг внутри акта; кто победит в этом раунде: 0 Годжо, 1 Сукуна (монетка) */
  float actT, waitT;        /* время в акте; сколько ждём, пока сойдутся */
  int clashWin;             /* чья территория устояла */
  float clashK, clashV;     /* граница куполов: -1 (у Годжо: Святилище заняло всё) … 1 (у Сукуны), и её ход */
  int maho, adapt, finish;  /* Махорага призван; шагов адаптации к Бесконечности; добивание взведено */
  float mahoT, mahoDie, mahoX, mahoFl, mahoF; /* тень: время с призыва, гибель (обратный отсчёт), где, куда смотрит */
  float mahoAtk;                              /* пауза до следующего приёма */
  int mahoSt, mahoMv, mahoHit, mahoSwing;     /* MS_*, MM_*, удар дошёл; бьёт сейчас (if_damage: отлетать от него) */
  int mahoId, mahoSeen;                       /* номер приёма; на какой Годжо уже реагировал */
  float mahoMt, mahoVx, mahoY, mahoVy;        /* время в состоянии; ход; высота над полом и скорость (прыжок) */
  float mahoHp, mahoFlash, mahoWalk, mahoLean, mahoTx; /* здоровье; белеет от удара; фаза шага; наклон от удара; куда бьёт */
  float mahoCalm;                                      /* кадров без ударов по нему: потом заживает */
  int mahoFirst, mahoSeenG;                            /* первый приём после призыва — сразу в бой; на какой удар Годжо закрывался */
  float mahoBladeCd;                                   /* Меч изгнания — не чаще раза в несколько секунд */
  float shadowT;   /* колесо над Сукуной: тень учится Бесконечности ещё до призыва (и в Разрезе мира) */
  float tagT;      /* Махорага подбросил или отбросил Годжо — Сукуна сейчас добивает */
  float flankT;    /* Сукуна прыгнул через великана — по ту сторону Годжо: клещи */
  float wheel, wheelTo, adaptT;               /* колесо над головой: поворот, куда крутится, вспышка адаптации */
  float cutX, cutY;         /* где был Годжо, когда Сукуна резал мир: разрез идёт сквозь послеобраз */
  int purpleMax;            /* следующий Фиолетовый — на всю мощь */
  /* надпись посреди экрана */
  const wchar_t *ban1, *ban2, *ban3;
  wchar_t banBuf[48];
  float banT, banLife;
  int banStyle;
  /* шкалы: что показано (плавно догоняет настоящее) */
  float hud[2][2];
  IfText *tx;    /* готовые надписи */
  int stat[MV_COUNT], statKO, statBF, statInf;
} IfScene;

static IfPart *if_part(IfScene *s, int kind, float x, float y, float vx, float vy, float life, float sz, FCol c) {
  IfPart *p = &s->pt[s->ptNext];
  s->ptNext = (s->ptNext + 1) % IF_MAXP;
  p->kind = (unsigned char)kind;
  p->light = 0;
  p->x = x, p->y = y, p->vx = vx, p->vy = vy;
  p->t = 0, p->life = life, p->sz = sz, p->g = 0, p->rot = 0;
  p->c = c;
  return p;
}

static IfEff *if_eff(IfScene *s, int kind, float x, float y, float life, float sz) {
  IfEff *e = NULL;
  for (int i = 0; i < IF_MAXE; i++)
    if (s->ef[i].kind == EF_NONE) {
      e = &s->ef[i];
      break;
    }
  if (!e) { /* все заняты: вытесняем самый старый по доле жизни */
    float best = -1;
    for (int i = 0; i < IF_MAXE; i++) {
      float k = s->ef[i].t / (s->ef[i].life > 0 ? s->ef[i].life : 1);
      if (k > best && s->ef[i].kind != EF_CALL) best = k, e = &s->ef[i];
    }
    if (!e) return NULL;
  }
  memset(e, 0, sizeof(*e));
  e->kind = kind;
  e->x = x, e->y = y, e->life = life, e->sz = sz;
  e->seed = if_rnd(&s->seed);
  return e;
}

static void if_shake(IfScene *s, float px) {
  if (px > s->shake) s->shake = px;
}

/* надпись техники у головы бойца */
static void if_call(IfScene *s, IfFighter *F, const wchar_t *txt, FCol c, float life) {
  for (int i = 0; i < IF_MAXE; i++) /* у бойца одна надпись: новая сменяет старую */
    if (s->ef[i].kind == EF_CALL && s->ef[i].owner == F->who) s->ef[i].kind = EF_NONE;
  IfEff *e = if_eff(s, EF_CALL, F->x, F->y + 118, life, 1);
  if (!e) return;
  e->owner = F->who;
  e->txt = txt;
  e->c = c;
}

/* надпись над Махорагой (выше колеса): owner 2 */
static void if_call_maho(IfScene *s, const wchar_t *txt, FCol c, float life) {
  for (int i = 0; i < IF_MAXE; i++)
    if (s->ef[i].kind == EF_CALL && s->ef[i].owner == 2) s->ef[i].kind = EF_NONE;
  IfEff *e = if_eff(s, EF_CALL, s->mahoX, s->mahoFl, life, 1);
  if (!e) return;
  e->owner = 2;
  e->txt = txt;
  e->c = c;
}

static void if_banner(IfScene *s, const wchar_t *a, const wchar_t *b, int style, float life) {
  s->ban1 = a, s->ban2 = b, s->ban3 = NULL, s->banStyle = style, s->banT = 0, s->banLife = life;
}
static void if_banner3(IfScene *s, const wchar_t *a, const wchar_t *b, const wchar_t *c3, int style, float life) {
  if_banner(s, a, b, style, life);
  s->ban3 = c3;
}

static IfFighter *if_foe(IfScene *s, IfFighter *F) { return &s->F[F == &s->F[0] ? 1 : 0]; }

static void if_set_anim(IfFighter *F, int a, int restart) {
  if (F->anim == a && !restart) return;
  F->from = F->pose;
  F->anim = a;
  F->at = 0;
}

static int if_idle_anim(const IfFighter *F) { return F->who ? AN_S_IDLE : AN_G_IDLE; }

static void if_set_state(IfFighter *F, int st) {
  F->st = st;
  F->stT = 0;
  if (st == FS_IDLE) if_set_anim(F, if_idle_anim(F), 0);
}

/* точка сустава в мире */
static void if_joint(const IfFighter *F, int j, float *x, float *y) {
  const FLook *L = F->who ? &kLookSukuna : &kLookGojo;
  float jx, jy;
  fa_joint(&F->sk, L->S, j, &jx, &jy);
  *x = F->x + jx, *y = F->y + jy;
}

/* земля: самый высокий из полов мониторов над x */
static float if_ground(const IfScene *s, float x) {
  float g = 0;
  for (int i = 0; i < s->nFix; i++)
    if (s->pl[i].solid && x >= s->pl[i].x0 && x <= s->pl[i].x1 && s->pl[i].top > g) g = s->pl[i].top;
  return g;
}

/* пол под точкой (x, y): верх самой высокой площадки не выше y; drop — сквозь окна */
static float if_floor(const IfScene *s, float x, float y, int drop) {
  float g = 0;
  for (int i = 0; i < s->nPl; i++) {
    const IfPlat *p = &s->pl[i];
    if (x < p->x0 || x > p->x1 || p->top > y + 0.5f || p->top <= g) continue;
    if (drop && !p->solid) continue;
    g = p->top;
  }
  return g;
}

/* высота над своим полом */
static float if_up(const IfFighter *F) { return F->y - F->fl; }

static int if_neutral(const IfFighter *F) {
  return F->st == FS_IDLE || F->st == FS_WALK || F->st == FS_RUN;
}

/* Махорага в бою: встал, не разбит */
static int if_maho_on(const IfScene *s) { return s->maho && s->mahoDie <= 0 && s->mahoSt != MS_RISE; }
/* Махорага ближе Сукуны — Годжо бьёт по нему: руками, Синим, Красным */
static int if_maho_first(const IfScene *s, const IfFighter *F) {
  return F->who == 0 && if_maho_on(s) && fabsf(s->mahoX - F->x) < fabsf(s->F[1].x - F->x);
}
/* до кого бойцу сейчас дело: до края тела Махораги или до противника */
static float if_gojo_dist(const IfScene *s, const IfFighter *F) {
  float d = fabsf(s->F[!F->who].x - F->x);
  if (if_maho_first(s, F)) d = fmaxf(0, fabsf(s->mahoX - F->x) - 12);
  return d;
}

/* ===================== урон ===================== */

/* монитор под точкой x (единицы); между мониторами — ближайший */
static int if_mon_at(const IfScene *s, float x) {
  int best = 0;
  float bd = 1e9f;
  for (int i = 0; i < s->nMon; i++) {
    float d = x < s->monL[i] ? s->monL[i] - x : (x > s->monR[i] ? x - s->monR[i] : 0);
    if (d < bd) bd = d, best = i;
  }
  return best;
}

/* бой на этом мониторе: туда возвращаются между раундами, там шкалы и надписи */
static void if_set_arena(IfScene *s, int m) {
  if (m < 0 || m >= s->nMon) return;
  s->arena = m;
  s->homeX = (s->monL[m] + s->monR[m]) * 0.5f;
  s->hudX = s->homeX * s->U, s->hudY = s->monB[m], s->hudTop = s->monT[m];
}

static void if_spark(IfScene *s, float x, float y, int owner, int big) {
  FCol c = owner ? FC(255, 70, 70) : FC(90, 190, 255);
  IfEff *e = if_eff(s, EF_SPARK, x, y, big ? 16 : 11, big ? 1.5f : 1.0f);
  if (e) e->owner = owner, e->c = c;
  int n = big ? 16 : 9;
  for (int i = 0; i < n; i++) {
    float a = if_rr(&s->seed, 0, 2 * FA_PI), v = if_rr(&s->seed, 1.5f, big ? 5.5f : 3.8f);
    IfPart *p = if_part(s, PT_SPARK, x, y, cosf(a) * v, sinf(a) * v, if_rr(&s->seed, 8, 16), if_rr(&s->seed, 0.6f, 1.2f),
                        if_rf(&s->seed) < 0.5f ? FC(255, 255, 255) : c);
    p->g = 0.12f;
  }
}

/* Махорага: удар встал в Бесконечности — колесо повернулось, тень учится */
static void if_adapt(IfScene *s) {
  s->adapt++;
  s->wheelTo += FA_PI * 0.25f;
  s->adaptT = 30;
  const wchar_t *t = s->adapt >= IF_ADAPT ? L"Адаптация завершена" : L"Адаптация";
  if (s->maho) if_call_maho(s, t, FC(200, 170, 255), 80); /* до призыва за тень говорит колесо над Сукуной */
}

/* урон по здоровью: без упоров, до нуля; сколько снято — в ответе */
static float if_hurt(const IfScene *s, IfFighter *D, float dmg) {
  if (dmg <= 0 || D->hp <= 0) return 0;
  if (s->phase == RP_FIGHT && s->act < SC_END) dmg *= IF_DMG * (D->who ? 1.0f : IF_DMG_G);
  float got = fminf(dmg, D->hp);
  D->hp -= got;
  return got;
}

static void if_maho_die(IfScene *s); /* ниже, у снарядов */

/* уязвимые места Махораги: ноги, туловище, голова (единицы мира) */
static int if_maho_zones(const IfScene *s, float *zx, float *zy, float *zr) {
  float bx = s->mahoX, by = s->mahoFl + s->mahoY, f = s->mahoF;
  zx[0] = bx, zy[0] = by + 23 * IF_MU, zr[0] = 26 * IF_MU;
  zx[1] = bx + f * 3 * IF_MU, zy[1] = by + 64 * IF_MU, zr[1] = 35 * IF_MU;
  zx[2] = bx + f * 5 * IF_MU, zy[2] = by + 97 * IF_MU, zr[2] = 18 * IF_MU;
  return 3;
}

/* удар по Махораге: белеет, вздрагивает или отлетает; кончилось здоровье — рассыпается */
static void if_maho_damage(IfScene *s, IfFighter *A, float dmg, int kind, float kx, float cx, float cy, int src) {
  if (!if_maho_on(s)) return;
  int bf = 0;
  float dir = s->mahoX >= A->x ? 1.0f : -1.0f;
  if (src == 0 && kind >= HK_HEAVY && if_rf(&s->seed) < 0.1f) { /* Чёрная вспышка */
    bf = 1, dmg *= 2.5f;
    if (kind == HK_HEAVY) kind = HK_KNOCK, kx = 6.5f;
    IfEff *e = if_eff(s, EF_BF, cx, cy, 30, 1);
    if (e) e->a = dir;
    if_call(s, A, L"Чёрная вспышка!", FC(255, 40, 60), 80);
    s->statBF++;
  }
  if (s->mahoSt == MS_GUARD && !bf && s->mahoF == -dir) { /* закрылся наручами: удар звенит о броню */
    s->mahoHp = fmaxf(1, s->mahoHp - dmg * 0.15f), s->mahoCalm = 0;
    s->mahoVx = dir * (0.5f + kx * 0.08f), s->mahoLean = 5 * dir * s->mahoF, s->mahoMt = fminf(s->mahoMt, 14);
    IfEff *e = if_eff(s, EF_GUARD, cx, cy, 14, 1.6f);
    if (e) e->a = -dir;
    if (s->hitstop < 3) s->hitstop = 3;
    if_shake(s, 3);
    return;
  }
  s->mahoHp -= dmg;
  s->mahoFlash = 1, s->mahoCalm = 0;
  A->ce += dmg * 0.2f;
  if (A->ce > IF_CE) A->ce = IF_CE;
  static const float kStop[5] = {2, 4, 6, 7, 10}, kShake[5] = {2, 4, 6, 7, 12};
  float stop = kStop[kind] + (bf ? 9 : 0);
  if (stop > s->hitstop) s->hitstop = stop;
  if_shake(s, kShake[kind] + (bf ? 10 : 0));
  if_spark(s, cx, cy, A->who, kind >= HK_HEAVY);
  if (s->mahoHp <= 0) { /* разбит не Фиолетовым — тоже конец тени */
    s->mahoHp = 0;
    if_maho_die(s);
    return;
  }
  float back = 12 * dir * s->mahoF; /* наклон: от удара спереди — назад */
  int busy = s->mahoSt == MS_ATK && s->mahoMt >= 8 && s->mahoMv != MM_STOMP; /* замахнулся — лёгкий удар не собьёт */
  if (kind >= HK_KNOCK || bf) {
    if (s->mahoY <= 0) s->mahoSt = MS_STAGGER, s->mahoMt = 0, s->mahoMv = -1, s->mahoVx = dir * (kind == HK_BLAST ? 3.2f : 2.0f);
    s->mahoLean = back;
  } else if (kind >= HK_HEAVY && !busy) {
    if (s->mahoY <= 0) s->mahoSt = MS_HIT, s->mahoMt = 0, s->mahoMv = -1, s->mahoVx = dir * 0.8f;
    s->mahoLean = back * 0.7f;
  } else if (!busy && s->mahoSt != MS_ATK) {
    if (s->mahoY <= 0) s->mahoSt = MS_HIT, s->mahoMt = 0, s->mahoMv = -1;
    s->mahoLean = back * 0.4f;
  }
}

/* удар дошёл: src 0 — рукой или ногой, 1 — снаряд, 2 — техника вплотную или взрыв */
static void if_damage(IfScene *s, IfFighter *A, IfFighter *D, float dmg, int kind, float kx, float ky, float cx,
                      float cy, int src) {
  int fin = s->finish && s->act == SC_END && A->who == s->fate; /* развязка: этим ударом бой кончается */
  if (fin) {
    s->finish = 0;
    D->inv = 0;
    if (D->st == FS_GUARD || D->st == FS_MOVE || D->st == FS_STUN || D->st == FS_TRAP) D->st = FS_IDLE, D->mv = -1;
    dmg = D->hp + 1;
  }
  if (D->st == FS_KO || D->inv > 0 || D->hp <= 0) return;
  float ax = s->mahoSwing ? s->mahoX : A->x; /* бьёт Махорага — отлетает от него, не от Сукуны */
  float dir = D->x >= ax ? 1.0f : -1.0f;
  if (src != 1 && fabsf(D->x - ax) < 1.0f) dir = (float)A->f;
  /* Бесконечность: удар Сукуны застревает в воздухе перед Годжо */
  if (D->who == 0 && !fin && D->ce >= 60 && D->st != FS_HIT && D->st != FS_FLY && D->st != FS_DOWN &&
      D->st != FS_STUN && D->st != FS_TRAP && D->burn <= 0) {
    float ch = src == 0 ? 0.4f : (src == 1 ? 0.5f : 0.0f);
    if (A->st == FS_MOVE && A->mv == MV_S_SLAM) ch = 0.3f;
    if (s->maho && s->adapt >= IF_ADAPT) ch = 0; /* Махорага приспособился: удары проходят сквозь Бесконечность */
    if (if_rf(&s->seed) < ch) {
      D->ce -= 60;
      IfEff *e = if_eff(s, EF_INF, cx, cy, 34, 1);
      if (e) e->a = -dir;
      if (src == 0) { /* кулак Сукуны встал — его же и отбросило */
        A->stuck = 16;
        A->vx = -dir * 1.6f;
      }
      if (s->frame - D->infT > 420 || !D->tellInf) {
        if_call(s, D, L"Бесконечность", FC(150, 215, 255), 70);
        D->infT = (float)s->frame;
        D->tellInf = 1;
      }
      if (s->maho) if_adapt(s);
      else if (A->who == 1 && s->phase == RP_FIGHT && s->shadowT <= 0 && /* колесо в тени: до призыва — не больше двух шагов */
               ((s->act == SC_OPEN && s->actT > 300 && s->adapt < 1) || (s->act == SC_BURN && s->adapt < 2))) {
        if_adapt(s);
        s->shadowT = 80;
      }
      s->statInf++;
      s->hitstop = 3;
      return;
    }
  }
  /* копит Фиолетовый или натягивает лук: удар не сбивает, урон вполовину */
  if (!fin && D->st == FS_MOVE && ((D->mv == MV_G_PURPLE && D->mt < 92) || (D->mv == MV_S_FUGA && D->mt < 97))) {
    if_hurt(s, D, dmg * 0.5f);
    D->ghostT = 30, D->flash = 0.6f;
    if_spark(s, cx, cy, A->who, 0);
    s->hitstop = 3;
    if_shake(s, 3);
    return;
  }
  /* Безграничная пустота: застывший не отлетает, бьют на месте */
  if (!fin && s->dom == DOM_VOID && D->who != s->domOwner && D->st == FS_STUN && D->hp - dmg * 0.7f * IF_DMG * IF_DMG_G > 0) {
    if_hurt(s, D, dmg * 0.7f);
    D->ghostT = 40, D->flash = 1;
    A->ce += dmg * 0.1f;
    if (A->ce > IF_CE) A->ce = IF_CE;
    if_spark(s, cx, cy, A->who, kind >= HK_HEAVY);
    s->hitstop = 4;
    if_shake(s, 4);
    return;
  }
  if (D->st == FS_GUARD && D->f == (int)-dir) { /* блок */
    if (D->hp - dmg * 0.18f >= 1) if_hurt(s, D, dmg * 0.18f);
    D->vx = dir * (1.6f + kx * 0.25f);
    D->stT = 0;
    IfEff *e = if_eff(s, EF_GUARD, cx, cy, 12, 1);
    if (e) e->a = -dir;
    s->hitstop = 3;
    if_shake(s, 2);
    A->ce += 6;
    return;
  }
  int bf = 0;
  if (src == 0 && kind >= HK_HEAVY && (fin || if_rf(&s->seed) < (A->who ? 0.14f : 0.10f))) { /* Чёрная вспышка */
    bf = 1;
    dmg *= 2.5f;
    if (kind == HK_HEAVY) kind = HK_KNOCK, kx = 6.5f, ky = 4.5f;
    IfEff *e = if_eff(s, EF_BF, cx, cy, 30, 1);
    if (e) e->a = dir;
    if_call(s, A, L"Чёрная вспышка!", FC(255, 40, 60), 80);
    s->statBF++;
  }
  if (D->st == FS_FLY || if_up(D) > 1) { /* добивание в воздухе: каждый раз подбрасывает слабее */
    D->juggle++;
    float k = D->juggle > 3 ? 0.35f : 1.0f - D->juggle * 0.15f;
    if (kind < HK_LAUNCH) kind = HK_KNOCK, kx = 2.5f, ky = 4.5f;
    ky *= k;
    dmg *= 0.8f;
  }
  if_hurt(s, D, dmg);
  D->ghostT = 40;
  D->flash = 1;
  A->ce += dmg * 0.3f;
  D->ce += dmg * 0.25f;
  if (A->ce > IF_CE) A->ce = IF_CE;
  if (D->ce > IF_CE) D->ce = IF_CE;
  static const float kStop[5] = {3, 5, 7, 8, 11}, kShake[5] = {2, 4, 6, 7, 12};
  float stop = kStop[kind] + (bf ? 9 : 0);
  if (stop > s->hitstop) s->hitstop = stop;
  if_shake(s, kShake[kind] + (bf ? 10 : 0));
  if_spark(s, cx, cy, A->who, kind >= HK_HEAVY);
  D->stuck = 0;
  if (D->hp <= 0) { /* нокаут: летит далеко */
    kind = HK_BLAST;
    if (kx < 6) kx = 6;
    if (ky < 7) ky = 7;
  }
  if (kind <= HK_HEAVY && if_up(D) <= 0.5f && D->st != FS_FLY) {
    if_set_state(D, FS_HIT);
    D->stun = kind == HK_LIGHT ? 14 : 20;
    if_set_anim(D, (D->hp > 0 && (s->frame & 1)) ? AN_HURT_M : AN_HURT_H, 1);
    D->vx = dir * kx;
  } else {
    if_set_state(D, FS_FLY);
    if_set_anim(D, AN_FLY, 1);
    D->vx = dir * kx;
    D->vy = ky;
    D->vroll = kind == HK_BLAST ? -9.0f : -3.0f; /* кувырок назад, от удара */
  }
  D->f = (int)-dir;
  D->mv = -1;
  if (D->who == 0 && if_maho_on(s) && s->act < SC_END && kind >= HK_KNOCK) { /* вдвоём: один отбросил — другой подхватывает */
    if (s->mahoSwing) {
      s->tagT = 70;
      if (A->think > 2) A->think = 2;
    } else if (A->who == 1 && s->mahoAtk > 6) {
      s->mahoAtk = 6;
    }
  }
}

/* ===================== приёмы: начало и ход ===================== */

static FCol if_tech_col(int mv) {
  switch (mv) {
  case MV_G_BLUE: return FC(90, 175, 255);
  case MV_G_RED: return FC(255, 70, 60);
  case MV_G_PURPLE: return FC(196, 100, 255);
  case MV_S_FUGA: return FC(255, 150, 40);
  case MV_G_RCT: case MV_G_HEAL: case MV_S_RCT: return FC(150, 255, 190);
  case MV_S_MAHO: return FC(190, 160, 255);
  default: return FC(255, 80, 90);
  }
}

static int if_can(const IfFighter *F, int mv) {
  const FMove *m = &kMoves[mv];
  if (F->cd[mv] > 0 || F->ce < m->ce) return 0;
  if ((m->flags & MVF_TECH) && F->burn > 0) return 0;
  return 1;
}

static void if_start(IfScene *s, IfFighter *F, int mv) {
  const FMove *m = &kMoves[mv];
  F->st = FS_MOVE;
  F->stT = 0;
  F->mv = mv;
  F->mt = 0;
  F->mvHit = 0;
  F->ce -= m->ce;
  if (F->ce < 0) F->ce = 0;
  F->cd[mv] = m->cd;
  F->used[mv] += 1.0f;
  F->trailN = 0;
  F->intent = IN_NONE;
  F->mvId++;
  s->stat[mv]++;
  if_set_anim(F, m->anim, 1);
  if (m->name && mv != MV_G_VOID && mv != MV_S_SHRINE)
    if_call(s, F, mv == MV_G_PURPLE && s->purpleMax ? L"Фиолетовый: на всю мощь" : m->name, if_tech_col(mv), 90);
  if (mv == MV_G_VOID || mv == MV_S_SHRINE) { /* купол — там, где бой: фон начинают строить в этом же кадре */
    const IfFighter *O = if_foe(s, F);
    s->domX = mv == MV_G_VOID || s->act == SC_CLASH ? (F->x + O->x) * 0.5f : F->x * 0.6f + O->x * 0.4f, s->domFl = F->fl;
  }
  if (mv == MV_G_PURPLE || mv == MV_G_VOID) F->inv = mv == MV_G_VOID ? 112 : 0;
  if (mv == MV_S_SHRINE) F->inv = 94, F->power = 1;
  if (mv == MV_S_FUGA || mv == MV_S_WORLD || mv == MV_S_MAHO) F->power = 1;
  if (mv == MV_G_RCT) F->inv = 56; /* чинит мозг — не сбить */
  if (mv == MV_G_HEAL || mv == MV_S_RCT) F->healR = if_rr(&s->seed, 6, 9.5f), F->rctN++; /* лечит, но сбить можно */
  if (mv == MV_S_MAHO) F->inv = 70; /* тень встаёт и закрывает собой */
  if (mv == MV_G_PURPLE || mv == MV_S_FUGA || mv == MV_S_WORLD) F->ultUsed = 1;
  if (mv == MV_G_DIVE) F->vy = -3.2f, F->vx = F->f * 4.6f;
  if (mv == MV_S_SLAM) F->vy = 2.5f, F->vx = F->f * 2.0f;
}

static IfProj *if_proj(IfScene *s, int kind, int owner, float x, float y, float vx, float r, float life) {
  for (int i = 0; i < IF_MAXJ; i++)
    if (s->pj[i].kind == PJ_NONE) {
      IfProj *p = &s->pj[i];
      memset(p, 0, sizeof(*p));
      p->kind = kind, p->owner = owner;
      p->x = x, p->y = y, p->vx = vx, p->r = r, p->life = life;
      return p;
    }
  return NULL;
}

/* рука или нога целиком: плечо (бедро) — локоть (колено) — кулак (стопа), в мире.
   Бьёт вся конечность, а не одна точка кулака: вытянутая рука проходит сквозь
   противника, и кулак за его спиной — это тоже попадание */
static int if_limb(const IfFighter *F, int j, float *px, float *py) {
  const FSkel *k = &F->sk;
  float S = F->who ? kLookSukuna.S : kLookGojo.S, p[3][2];
  int n = 3;
  if (j == FJ_HAND0 || j == FJ_HAND1) {
    int i = j - FJ_HAND0;
    float dx, dy;
    fa_dir(k->f, k->la[i], &dx, &dy);
    p[0][0] = k->sh[i][0], p[0][1] = k->sh[i][1], p[1][0] = k->el[i][0], p[1][1] = k->el[i][1];
    p[2][0] = k->wr[i][0] + dx * 3.0f, p[2][1] = k->wr[i][1] + dy * 3.0f;
  } else if (j == FJ_FOOT0 || j == FJ_FOOT1) {
    int i = j - FJ_FOOT0;
    p[0][0] = k->hp[i][0], p[0][1] = k->hp[i][1], p[1][0] = k->kn[i][0], p[1][1] = k->kn[i][1];
    p[2][0] = k->an[i][0], p[2][1] = k->an[i][1];
  } else {
    p[0][0] = k->hp[1][0], p[0][1] = k->hp[1][1], p[1][0] = k->kn[1][0], p[1][1] = k->kn[1][1];
    n = 2;
  }
  for (int i = 0; i < n; i++) {
    float rx, ry;
    fa_rot(k, p[i][0], p[i][1], &rx, &ry);
    px[i] = F->x + rx * S, py[i] = F->y + ry * S;
  }
  return n;
}

/* ближайшая к (x,y) точка отрезка: квадрат расстояния и сама точка */
static float if_seg(float ax, float ay, float bx, float by, float x, float y, float *qx, float *qy) {
  float ex = bx - ax, ey = by - ay, ee = ex * ex + ey * ey;
  float t = ee > 1e-6f ? fa_sat(((x - ax) * ex + (y - ay) * ey) / ee) : 0.0f;
  *qx = ax + ex * t, *qy = ay + ey * t;
  return (x - *qx) * (x - *qx) + (y - *qy) * (y - *qy);
}

/* обычный удар: задела ли рука или нога голову, грудь или таз */
static void if_try_hit(IfScene *s, IfFighter *A, IfFighter *D) {
  const FMove *m = &kMoves[A->mv];
  if (D->st == FS_KO || D->inv > 0 || D->st == FS_TELE || D->st == FS_DOWN || D->st == FS_GETUP) return;
  float lx[3], ly[3];
  int n = if_limb(A, m->joint, lx, ly);
  static const int kHurt[3] = {FJ_HEAD, FJ_CHEST, FJ_HIP};
  static const float kR[3] = {8.5f, 11.0f, 10.0f};
  float lr = m->reach * 0.6f;
  for (int sg = n - 2; sg >= 0; sg--) /* от кулака к плечу: искра — ближе к кулаку */
    for (int i = 0; i < 3; i++) {
      float hx, hy, qx, qy, rr = lr + kR[i];
      if_joint(D, kHurt[i], &hx, &hy);
      if (if_seg(lx[sg], ly[sg], lx[sg + 1], ly[sg + 1], hx, hy, &qx, &qy) > rr * rr) continue;
      float cx = qx + (hx - qx) * 0.4f, cy = qy + (hy - qy) * 0.4f;
      A->mvHit = 1;
      if (A->mv == MV_S_CLEAVE) {
        IfEff *e = if_eff(s, EF_CROSS, hx, hy, 30, 1);
        if (e) e->a = (float)A->f;
      }
      if_damage(s, A, D, m->dmg, m->kind, m->kx, m->ky, cx, cy, A->mv == MV_S_CLEAVE ? 2 : 0);
      return;
    }
}

/* удар Годжо по Махораге: рука или нога задела ноги, туловище или голову великана */
static void if_try_hit_maho(IfScene *s, IfFighter *A) {
  const FMove *m = &kMoves[A->mv];
  float lx[3], ly[3], zx[3], zy[3], zr[3];
  int n = if_limb(A, m->joint, lx, ly), nz = if_maho_zones(s, zx, zy, zr);
  float lr = m->reach * 0.6f;
  for (int sg = n - 2; sg >= 0; sg--)
    for (int i = 0; i < nz; i++) {
      float qx, qy, rr = lr + zr[i];
      if (if_seg(lx[sg], ly[sg], lx[sg + 1], ly[sg + 1], zx[i], zy[i], &qx, &qy) > rr * rr) continue;
      float dx = qx - zx[i], dy = qy - zy[i], dd = sqrtf(dx * dx + dy * dy);
      float cx = dd > 0.01f ? zx[i] + dx / dd * zr[i] * 0.8f : qx, cy = dd > 0.01f ? zy[i] + dy / dd * zr[i] * 0.8f : qy;
      A->mvHit = 1;
      if_maho_damage(s, A, m->dmg, m->kind, m->kx, cx, cy, 0);
      return;
    }
}

/* серии: что бить следом */
static int if_next_melee(IfScene *s, IfFighter *F) {
  float r = if_rf(&s->seed);
  switch (F->mv) {
  case MV_G_JAB: return r < 0.55f ? MV_G_PALM : MV_G_KICK;
  case MV_G_PALM: return r < 0.4f ? MV_G_KICK : (r < 0.7f ? MV_G_HIGH : MV_G_UPKICK);
  case MV_G_KICK: return r < 0.45f ? MV_G_HIGH : (r < 0.8f || !if_can(F, MV_G_FLICK) ? MV_G_UPKICK : MV_G_FLICK);
  case MV_S_CLAW: return r < 0.5f ? MV_S_SWIPE : MV_S_PUNCH;
  case MV_S_SWIPE: return r < 0.55f ? MV_S_PUNCH : MV_S_KNEE;
  case MV_S_PUNCH: return r < 0.35f ? MV_S_UPPER : (r < 0.7f || !if_can(F, MV_S_CLEAVE) ? MV_S_KICK : MV_S_CLEAVE);
  case MV_S_KNEE: return r < 0.5f ? MV_S_UPPER : MV_S_PUNCH;
  default: return -1;
  }
}

static void if_teleport_out(IfScene *s, IfFighter *F) {
  IfEff *e = if_eff(s, EF_AFTER, F->x, F->y, 22, 1);
  if (e) {
    memset(&e->fig, 0, sizeof(e->fig));
    e->fig.who = F->who, e->fig.f = F->f, e->fig.p = F->pose, e->fig.blind = F->blind;
    e->fig.alpha = 0.8f, e->fig.tint = FC(120, 200, 255), e->fig.tintA = 0.55f;
  }
  if_eff(s, EF_TELE, F->x, F->y, 18, 1);
}

/* перед выстрелом — лицом к противнику: мог зайти за спину, пока копилось */
static void if_face_x(IfFighter *F, float x) {
  int f = x >= F->x ? 1 : -1;
  if (f == F->f) return;
  F->f = f;
  fa_skel(&F->sk, &F->pose, F->f);
}
static void if_face(IfFighter *F, const IfFighter *O) { if_face_x(F, O->x); }
/* перед техникой — лицом к цели: к Махораге, если он ближе */
static void if_face_tgt(IfScene *s, IfFighter *F, const IfFighter *O) {
  if_face_x(F, if_maho_first(s, F) ? s->mahoX : O->x);
}

/* снаряд — в грудь противнику: прыгнул — летит вверх, но не круче 30° */
static void if_aim_xy(IfProj *p, float tx, float ty, float speed) {
  float dx = tx - p->x, dy = ty - p->y;
  if (fabsf(dx) < 1) return;
  float k = dy / fabsf(dx);
  k = fa_clampf(k, -0.55f, 0.55f);
  float l = sqrtf(1 + k * k);
  p->vx = (dx > 0 ? 1.0f : -1.0f) * speed / l;
  p->vy = k * speed / l;
}
/* в грудь противнику — или в Махорагу, если он ближе */
static void if_aim(IfScene *s, IfProj *p, const IfFighter *F, const IfFighter *O, float speed) {
  float tx, ty;
  if (if_maho_first(s, F)) tx = s->mahoX + s->mahoF * 2, ty = s->mahoFl + s->mahoY + 64 * IF_MU;
  else if_joint(O, FJ_CHEST, &tx, &ty);
  if_aim_xy(p, tx, ty, speed);
}

/* что происходит в приёме на кадре t (t0 — предыдущий) */
/* оба знака сделаны: купола сходятся — один на двоих, с границей посередине */
static void if_clash_open(IfScene *s) {
  s->dom = DOM_CLASH, s->domOwner = 0, s->domPhase = DP_CAST, s->domT = 0, s->clashK = 0, s->clashV = 0;
  s->domLen = 330, s->domBurn = 600;
  if_banner3(s, L"РАСШИРЕНИЕ ТЕРРИТОРИИ", L"БЕЗГРАНИЧНАЯ ПУСТОТА", L"СВЯТИЛИЩЕ ЗЛОБЫ", 5, 200);
}

static void if_act(IfScene *s, int act) { s->act = act, s->actT = 0, s->step = 0, s->waitT = 0; }

static void if_move_events(IfScene *s, IfFighter *F, float t0, float t) {
  IfFighter *O = if_foe(s, F);
  float hx, hy;
#define AT(E) (t0 < (E) && t >= (E))
  switch (F->mv) {
  case MV_G_BLUE:
    if (AT(10)) {
      float tx = if_maho_first(s, F) ? s->mahoX : O->x, d = fabsf(tx - F->x), off = d * 0.55f > 75 ? 75 : d * 0.55f;
      if (off < 36) off = 36;
      if (if_maho_first(s, F)) if_face_x(F, tx);
      float bx = F->x + F->f * off;
      if_proj(s, PJ_BLUE, F->who, bx, fmaxf(F->fl, O->fl) + 44, 0, 8, 62);
    }
    break;
  case MV_G_RED:
    if (AT(33)) {
      if_face_tgt(s, F, O);
      if_joint(F, FJ_HAND1, &hx, &hy);
      IfProj *p = if_proj(s, PJ_RED, F->who, hx + F->f * 3, hy, F->f * 7.5f, 6, 150);
      if (p) if_aim(s, p, F, O, 7.5f);
      if_shake(s, 4);
    }
    break;
  case MV_G_REDX: /* три малых Красных подряд, веером */
    if (AT(28) || AT(36) || AT(44)) {
      int i = t >= 44 ? 2 : (t >= 36 ? 1 : 0);
      if_face_tgt(s, F, O);
      if_joint(F, FJ_HAND1, &hx, &hy);
      IfProj *p = if_proj(s, PJ_RED, F->who, hx + F->f * 3, hy, F->f * 8.5f, 4.2f, 130);
      if (p) {
        if_aim(s, p, F, O, 8.5f);
        p->vy += (i - 1) * 1.1f, p->a = 1; /* a — малый */
      }
      if_shake(s, 3);
    }
    break;
  case MV_G_LUNGE:
  case MV_S_LUNGE: /* рывок: остаётся силуэт, из-под ног пыль */
    if (AT(1) || AT(4)) {
      IfEff *e = if_eff(s, EF_AFTER, F->x, F->y, 16, 1);
      if (e) {
        memset(&e->fig, 0, sizeof(e->fig));
        e->fig.who = F->who, e->fig.f = F->f, e->fig.p = F->pose, e->fig.blind = F->blind;
        e->fig.alpha = 0.6f, e->fig.tint = F->who ? FC(255, 90, 100) : FC(120, 200, 255), e->fig.tintA = 0.6f;
      }
      for (int i = 0; i < 4; i++)
        if_part(s, PT_DUST, F->x - F->f * if_rr(&s->seed, 0, 10), F->fl + 1, -F->f * if_rr(&s->seed, 0.5f, 2.0f),
                if_rr(&s->seed, 0.2f, 0.9f), if_rr(&s->seed, 14, 24), if_rr(&s->seed, 2, 3.5f), FC(200, 196, 190));
    }
    break;
  case MV_G_PALM: /* развязка: ладонь в грудь, и в ней — Чёрная вспышка, которой бой кончается */
    if (AT(8) && s->finish && !F->mvHit) {
      F->mvHit = 1;
      if_face(F, O);
      if_joint(O, FJ_CHEST, &hx, &hy);
      if_damage(s, F, O, kMoves[MV_G_PALM].dmg, HK_HEAVY, 6.5f, 4.5f, hx, hy, 0);
    }
    break;
  case MV_G_PURPLE: {
    int mx = s->purpleMax; /* на всю мощь: шире вдвое, надпись на весь экран */
    if (AT(2)) F->blind = 0;
    if (AT(78)) {
      if_joint(F, FJ_HAND1, &hx, &hy);
      IfEff *e = if_eff(s, EF_FLASH, hx, hy, 18, mx ? 3.2f : 2.2f);
      if (e) e->c = FC(200, 120, 255);
      if_shake(s, mx ? 12 : 8);
      if (mx) if_banner(s, L"МНИМАЯ ТЕХНИКА", L"ФИОЛЕТОВЫЙ", 6, 150);
    }
    if (AT(90)) {
      if_face(F, O);
      if_joint(F, FJ_HAND1, &hx, &hy);
      IfProj *p = if_proj(s, PJ_PURPLE, F->who, hx + F->f * 10, F->fl + (mx ? 54.0f : 46.0f), F->f * (mx ? 4.4f : 5.2f),
                          mx ? 36.0f : 21.0f, 400);
      if (p && mx) p->a = 1;
      s->purpleMax = 0;
      if_shake(s, mx ? 22 : 14);
    }
    break;
  }
  case MV_G_RCT: /* обратная проклятая техника: Годжо чинит выгоревший мозг, техника возвращается */
    if (AT(6)) {
      IfEff *e = if_eff(s, EF_HEAL, F->x, F->fl, 60, 1);
      if (e) e->owner = F->who;
    }
    if (AT(34)) {
      F->burn = 0, F->ce = 760;
      O->burn = fmaxf(O->burn, 640); /* Сукуне чинить нечем: его техника выгорела надолго */
      if_call(s, F, L"Техника вернулась", FC(150, 255, 190), 70);
    }
    break;
  case MV_G_HEAL:
  case MV_S_RCT: /* на грани нокаута: раны затягиваются на глазах, шкала растёт */
    if (AT(6)) {
      IfEff *e = if_eff(s, EF_HEAL, F->x, F->fl, 60, 1);
      if (e) e->owner = F->who;
    }
    if (t > 8 && t <= 48) F->hp = fminf(IF_HP, F->hp + F->healR * (fminf(t, 48) - fmaxf(t0, 8)));
    break;
  case MV_G_TELE:
    if (AT(1)) if_teleport_out(s, F), F->inv = 16;
    if (AT(8)) {
      float bx = O->x - O->f * 24;
      if (bx < s->xMin + 4 || bx > s->xMax - 4 || fabsf(if_floor(s, bx, O->y + 1, 0) - O->fl) > 2)
        bx = O->x + O->f * 24; /* за спиной стена или край окна — тогда спереди */
      F->x = bx;
      F->y = F->fl = if_floor(s, bx, O->fl + 1, 0); /* к нему на площадку */
      F->vy = 0;
      F->f = O->x >= F->x ? 1 : -1;
      if_eff(s, EF_TELE, F->x, F->y, 16, 1);
    }
    break;
  case MV_G_VOID:
    if (AT(12)) F->blind = 0;
    if (AT(26) && !s->dom) {
      if (s->act == SC_CLASH) if_clash_open(s);
      else {
        s->dom = DOM_VOID, s->domOwner = F->who, s->domPhase = DP_CAST, s->domT = 0;
        s->domLen = s->act == SC_VOID2 ? 200 : 330, s->domBurn = 600;
        if_banner(s, L"РАСШИРЕНИЕ ТЕРРИТОРИИ", L"БЕЗГРАНИЧНАЯ ПУСТОТА", 1, 170);
      }
    }
    break;
  case MV_S_DISM:
    if (AT(19) || AT(40)) {
      int second = t >= 40;
      for (int i = 0; i < 3; i++) {
        IfProj *p = if_proj(s, PJ_SLASH, F->who, F->x + F->f * 14, F->fl + 34.0f + i * 24 + second * 10,
                            F->f * (10.5f + i * 0.6f), 9, 90);
        if (p) p->vy = if_rr(&s->seed, -0.5f, 0.5f), p->a = if_rr(&s->seed, -0.6f, 0.6f), p->hit = 0, p->r = 10 + i;
      }
    }
    break;
  case MV_S_FAN: /* Рассечение веером: четыре взмаха, разрезы вверх и вниз */
    if (AT(14) || AT(24) || AT(34) || AT(44)) {
      int w = t >= 44 ? 3 : (t >= 34 ? 2 : (t >= 24 ? 1 : 0));
      for (int i = 0; i < 2; i++) {
        float vy = (i ? 1.0f : -1.0f) * (0.4f + 0.5f * (w & 1)), y = F->fl + 30.0f + w * 9 + i * 22;
        IfProj *p = if_proj(s, PJ_SLASH, F->who, F->x + F->f * 14, y, F->f * (10.0f + w * 0.5f), 9, 90);
        if (p) p->vy = vy, p->a = -vy * 0.35f * F->f + if_rr(&s->seed, -0.3f, 0.3f), p->hit = 0, p->r = 10 + w;
      }
      if_shake(s, 2);
    }
    break;
  case MV_S_WORLD: /* Разрез мира: пространство рассечено насквозь, через весь экран. Развязка: монетка */
    if (AT(8)) {
      if_joint(O, FJ_CHEST, &s->cutX, &s->cutY);
      if (s->act == SC_WORLD) {
        s->act = SC_END, s->actT = 0;
        if (s->fate == 0) { /* Годжо прочёл удар: исчезает за миг до него, разрез пройдёт сквозь послеобраз */
          O->y = O->fl, O->vy = 0, O->mv = -1;
          if_start(s, O, MV_G_TELE);
        }
      }
    }
    if (AT(13)) {
      hx = s->cutX, hy = s->cutY;
      IfEff *e = if_eff(s, EF_WCUT, hx, hy, 48, 1);
      if (e) e->a = F->f * if_rr(&s->seed, 0.55f, 0.95f);
      if_shake(s, 16);
      s->hitstop = 6;
      if (s->act == SC_END && s->fate == 1) { /* попал: Годжо пополам */
        IfEff *c = if_eff(s, EF_CROSS, hx, hy, 30, 1.4f);
        if (c) c->a = (float)F->f;
        s->finish = 1;
        s->slowT = 46;
        if_damage(s, F, O, 150, HK_BLAST, 7.5f, 6.0f, hx, hy, 2); /* сквозь Бесконечность */
      } else if (s->act == SC_END) {
        if_call(s, O, L"Мимо.", FC(150, 215, 255), 60);
      }
    }
    break;
  case MV_S_MAHO: /* Десять теней: из тени под ногами встаёт Махорага; Пустота о тень трескается */
    if (AT(8)) {
      s->maho = 1, s->mahoT = 0, s->mahoDie = 0; /* колесо и выученное в тени — при нём */
      s->mahoX = fa_clampf(F->x - F->f * 36, s->xMin + 30, s->xMax - 30), s->mahoF = (float)F->f;
      s->mahoFl = if_ground(s, s->mahoX);
      s->mahoAtk = 0, s->mahoFirst = 1, s->mahoSeenG = 0, s->mahoBladeCd = 240, s->mahoSt = MS_RISE, s->mahoMv = -1, s->mahoMt = 0, s->mahoHit = 0, s->mahoSwing = 0;
      s->mahoVx = s->mahoY = s->mahoVy = 0, s->mahoHp = IF_MAHOHP, s->mahoFlash = 0, s->mahoWalk = 0, s->mahoLean = 0, s->mahoCalm = 0;
      s->mahoTx = s->mahoX, s->mahoId = 0, s->mahoSeen = 0;
      for (int i = 0; i < 30; i++) {
        float a = if_rr(&s->seed, 0, FA_PI), v = if_rr(&s->seed, 0.4f, 2.2f);
        if_part(s, PT_SMOKE, s->mahoX + if_rr(&s->seed, -50, 50), s->mahoFl + 2, cosf(a) * v * 0.5f, sinf(a) * v,
                if_rr(&s->seed, 40, 80), if_rr(&s->seed, 8, 18), FC(20, 12, 34));
      }
      if_shake(s, 5);
    }
    if (AT(30)) if_banner(s, L"ДЕСЯТЬ ТЕНЕЙ", L"МАХОРАГА", 2, 150);
    if (AT(40) && s->dom == DOM_VOID && s->domPhase != DP_CLOSE) { /* купол треснул о тень: Годжо снова без техники */
      s->domPhase = DP_CLOSE, s->domT = 0, s->domBurn = 300;
      IfEff *e = if_eff(s, EF_FLASH, s->domX, s->domFl + 60, 24, 3.0f);
      if (e) e->c = FC(220, 230, 255);
      for (int i = 0; i < 40; i++) {
        float u = if_rr(&s->seed, -0.9f, 0.9f), v = if_rr(&s->seed, 0.1f, 0.95f) * sqrtf(1 - u * u);
        IfPart *q = if_part(s, PT_BIT, s->domX + u * s->domRx, s->domFl + v * s->domRy, u * 1.5f, if_rr(&s->seed, -1, 2),
                            if_rr(&s->seed, 30, 50), if_rr(&s->seed, 1.5f, 3.5f), FC(150, 200, 255));
        q->g = 0.2f, q->rot = if_rr(&s->seed, 0, 6);
      }
      if_shake(s, 16);
      if_call(s, O, L"Купол треснул", FC(170, 170, 180), 70);
    }
    break;
  case MV_S_CLEAVE:
    if (AT(13) && !F->mvHit) { /* мимо: разрез повис в воздухе */
      if_joint(F, FJ_HAND1, &hx, &hy);
      IfEff *e = if_eff(s, EF_CROSS, hx + F->f * 8, hy, 20, 0.6f);
      if (e) e->a = (float)F->f;
    }
    break;
  case MV_S_FUGA:
    if (AT(95)) {
      if_face(F, O);
      if_joint(F, FJ_HAND0, &hx, &hy);
      IfProj *p = if_proj(s, PJ_ARROW, F->who, hx - F->f * 6, hy, F->f * 12.0f, 5, 160);
      if (p) if_aim(s, p, F, O, 12.0f);
      if_shake(s, 6);
    }
    break;
  case MV_S_SHRINE:
    if (AT(14) && !s->dom) {
      if (s->act == SC_CLASH) if_clash_open(s);
      else {
        s->dom = DOM_SHRINE, s->domOwner = F->who, s->domPhase = DP_CAST, s->domT = 0, s->domLen = 330, s->domBurn = 600;
        if_banner(s, L"РАСШИРЕНИЕ ТЕРРИТОРИИ", L"СВЯТИЛИЩЕ ЗЛОБЫ", 2, 170);
      }
    }
    break;
  }
#undef AT
}

static void if_move_step(IfScene *s, IfFighter *F, float dt) {
  IfFighter *O = if_foe(s, F);
  const FMove *m = &kMoves[F->mv];
  float t0 = F->mt;
  F->mt += dt;
  float t = F->mt;
  if (m->flags & MVF_AIR) {
    if (F->mv == MV_S_SLAM && t > 10) F->vy -= 0.9f * dt; /* с высоты — вниз, кулаками */
  } else if (m->step > 0 && t < m->hitA + 1) {
    int mh = if_maho_first(s, F);
    float gap = ((mh ? s->mahoX : O->x) - F->x) * F->f, stop = mh ? 36 : 30; /* шаг в удар — но не в самого противника */
    F->vx = gap > stop ? F->f * fminf(m->step, (gap - stop) * 0.5f + 0.4f) : 0;
  } else {
    F->vx *= powf(0.8f, dt);
  }
  if_move_events(s, F, t0, t);
  if (F->st != FS_MOVE) return; /* событие могло сменить состояние */
  if (m->hitB > m->hitA && t >= m->hitA && t <= m->hitB && !F->mvHit) {
    if (if_maho_first(s, F)) if_try_hit_maho(s, F);
    if (!F->mvHit) if_try_hit(s, F, O);
  }
  if (F->st != FS_MOVE) return;
  /* след удара для взмаха */
  if (m->hitB > m->hitA && t >= m->hitA - 3 && t <= m->hitB + 2) {
    float jx, jy;
    if_joint(F, m->joint, &jx, &jy);
    if (F->trailN < IF_TRAIL) F->trailN++;
    memmove(F->trail[1], F->trail[0], sizeof(F->trail[0]) * (IF_TRAIL - 1));
    F->trail[0][0] = jx, F->trail[0][1] = jy;
  } else if (F->trailN > 0) {
    F->trailN--;
  }
  /* серия: попал — бьёт следующий */
  if ((m->flags & MVF_MELEE) && !(m->flags & MVF_AIR) && F->mvHit && t >= m->cancel && s->phase == RP_FIGHT &&
      F->chain < 6 && O->st != FS_KO) {
    float d = if_gojo_dist(s, F);
    int nx = if_next_melee(s, F);
    if (nx >= 0 && d < 52 && O->st != FS_DOWN && if_rf(&s->seed) < 0.9f - F->chain * 0.12f) {
      F->chain++;
      if_start(s, F, nx);
      return;
    }
  }
  if (m->flags & MVF_AIR) {
    if (F->y <= F->fl && F->vy <= 0) {
      F->y = F->fl;
      F->vy = 0;
      if_set_state(F, FS_LAND);
      if_set_anim(F, AN_LAND, 1);
      for (int i = 0; i < 8; i++)
        if_part(s, PT_DUST, F->x + if_rr(&s->seed, -8, 8), F->fl + 1, if_rr(&s->seed, -1.6f, 1.6f), if_rr(&s->seed, 0.2f, 1.0f),
                if_rr(&s->seed, 18, 30), if_rr(&s->seed, 2, 4), FC(200, 196, 190));
      if_shake(s, 4);
    }
    return;
  }
  if (t >= m->len) {
    if ((F->mv == MV_G_VOID || F->mv == MV_S_SHRINE) && s->dom == DOM_CLASH) { /* купола сошлись: знак держат, пока не решится */
      F->mt = t0;
      return;
    }
    int mv = F->mv;
    F->mv = -1;
    if_set_state(F, FS_IDLE);
    if (F->chain >= 2 && if_rf(&s->seed) < 0.2f) /* после серии — отскочить: место для техник */
      F->intent = IN_BACK, F->intentT = if_rr(&s->seed, 10, 20);
    F->chain = 0;
    if (mv == MV_G_TELE && s->act == SC_END && s->fate == 0 && s->phase == RP_FIGHT && O->hp > 0) { /* развязка Годжо */
      s->finish = 1;
      if_start(s, F, MV_G_PALM);
      return;
    }
    if (mv == MV_G_RCT) if_act(s, SC_VOID2);
    if (mv == MV_S_MAHO && s->act == SC_VOID2) if_act(s, SC_MAHO);
    if (mv == MV_G_TELE && s->phase == RP_FIGHT && fabsf(O->x - F->x) < 52 && O->st != FS_DOWN) {
      float r = if_rf(&s->seed);
      if_start(s, F, r < 0.4f ? MV_G_KICK : (r < 0.7f ? MV_G_HIGH : MV_G_PALM));
    }
    F->think = mv == MV_G_BLUE ? 0 : if_rr(&s->seed, 3, 10);
  }
}

/* ===================== ИИ ===================== */

enum { ACT_NEAR = 100, ACT_BACK, ACT_WAIT, ACT_JUMP, ACT_LEAP, ACT_DROP, ACT_PERCH, ACT_ROAM, ACT_FLANK, ACT_CHASE };
typedef struct {
  int act;
  float w;
} IfOpt;

/* сколько энергии беречь: один в раунде копит на территорию, другой — на свой
   сильнейший удар (Фиолетовый, Разверзнись) */
/* приёмы сценария начинает только сценарий; Разверзнись против Годжо не бывает (как в каноне) */
static int if_allowed(const IfScene *s, int mv) {
  switch (mv) {
  case MV_G_VOID: case MV_G_PURPLE: case MV_G_RCT:
  case MV_S_SHRINE: case MV_S_WORLD: case MV_S_MAHO: case MV_S_FUGA: case MV_G_HEAL: case MV_S_RCT:
    return 0;
  default:
    break;
  }
  if (s->act == SC_WORLD && (kMoves[mv].flags & MVF_TECH)) return 0; /* перед развязкой — только руки */
  return 1;
}
static int if_afford(const IfScene *s, const IfFighter *F, int mv) { return if_can(F, mv) && if_allowed(s, mv); }

static void if_opt(IfScene *s, const IfFighter *F, IfOpt *o, int *n, int act, float w) {
  if (w <= 0) return;
  if (act < MV_COUNT) {
    if (!if_afford(s, F, act)) return;
    w /= 1.0f + F->used[act] * 0.7f; /* недавно было — реже */
  }
  o[*n].act = act, o[*n].w = w, (*n)++;
}

/* прыжок, чтобы оказаться на dx вбок и dy выше (dy < 0 — ниже) */
static void if_leap(IfFighter *F, float dx, float dy) {
  const float g = 0.36f;
  float h = fmaxf(dy, 0) + 18, vy = sqrtf(2 * g * h), T = vy / g + sqrtf(2 * (h - dy) / g);
  if_set_state(F, FS_JUMP);
  if_set_anim(F, AN_JUMP, 1);
  F->vy = vy;
  F->vx = fa_clampf(dx / T, -6.0f, 6.0f);
  if (fabsf(dx) > 2) F->f = dx > 0 ? 1 : -1;
  F->intent = IN_NONE;
  F->wallT = 0;
}

/* стоит на окне (не на полу монитора) */
static int if_on_window(const IfScene *s, const IfFighter *F) { return F->fl > if_ground(s, F->x) + 0.5f; }

/* окно рядом, на которое можно запрыгнуть: занять высоту. Куда — в *tx, *ty */
static int if_perch(const IfScene *s, const IfFighter *F, float *tx, float *ty) {
  float best = 1e9f;
  for (int i = s->nFix; i < s->nPl; i++) {
    const IfPlat *p = &s->pl[i];
    float dy = p->top - F->fl;
    if (dy < 30 || p->x1 - p->x0 < 40) continue; /* высота любая: Годжо — телепортом, Сукуна — прыжком */
    float cx = fa_clampf(F->x, p->x0 + 18, p->x1 - 18), dx = fabsf(cx - F->x);
    if (dx > 420 || dx + dy * 0.3f >= best) continue;
    best = dx + dy * 0.3f, *tx = cx, *ty = p->top;
  }
  return best < 1e8f;
}

static void if_do(IfScene *s, IfFighter *F, int act) {
  IfFighter *O = if_foe(s, F);
  float d = if_gojo_dist(s, F);
  F->wallT = 0;
  if (act < MV_COUNT) {
    if ((kMoves[act].flags & MVF_MELEE) && d > ((kMoves[act].flags & MVF_LUNGE) ? 115 : 46)) { /* далеко: сначала подойти */
      F->intent = IN_NEAR, F->intentD = if_rr(&s->seed, 28, 36);
      return;
    }
    F->chain = 0;
    if_start(s, F, act);
    return;
  }
  switch (act) {
  case ACT_NEAR:
    F->intent = IN_NEAR;
    F->intentD = if_rr(&s->seed, 28, 37);
    break;
  case ACT_BACK:
    F->intent = IN_BACK;
    F->intentT = if_rr(&s->seed, 12, 26);
    break;
  case ACT_JUMP:
    if_set_state(F, FS_JUMP);
    if_set_anim(F, AN_JUMP, 1);
    F->vy = 6.6f;
    F->vx = F->f * (d > 60 ? 3.0f : 2.2f);
    break;
  case ACT_LEAP: { /* к нему наверх: прыжок не дальше, чем долетит, иначе сперва подойти */
    float dy = O->fl - F->y, dx = O->x - F->x, h = fmaxf(dy, 0) + 18;
    float T = sqrtf(2 * h / 0.36f) + sqrtf(2 * (h - dy) / 0.36f), reach = 6.0f * T * 0.9f;
    dx -= (dx > 0 ? 1.0f : -1.0f) * 26; /* не на голову, а рядом */
    if (fabsf(dx) > reach) {
      F->intent = IN_NEAR, F->intentD = d - reach * 0.8f;
      break;
    }
    if_leap(F, dx, dy);
    break;
  }
  case ACT_PERCH: { /* на окно: оттуда — техниками */
    float tx, ty;
    if (!if_perch(s, F, &tx, &ty)) break;
    if (F->who == 0 && ty - F->y > 180) { /* высоко: Годжо просто оказывается наверху */
      if_teleport_out(s, F);
      F->x = tx, F->y = F->fl = ty, F->vx = F->vy = 0;
      if_set_state(F, FS_IDLE);
      if_eff(s, EF_TELE, F->x, F->y, 16, 1);
      break;
    }
    if_leap(F, tx - F->x, ty - F->y);
    if (ty - F->y > 180) /* высоко: Сукуна отталкивается со всей силы — пыль и трещина */
      for (int i = 0; i < 10; i++)
        if_part(s, PT_DUST, F->x + if_rr(&s->seed, -10, 10), F->fl + 1, if_rr(&s->seed, -2.2f, 2.2f), if_rr(&s->seed, 0.3f, 1.4f),
                if_rr(&s->seed, 18, 30), if_rr(&s->seed, 2, 4), FC(200, 196, 190));
    break;
  }
  case ACT_ROAM: { /* увести бой на соседний монитор: Годжо — телепортом, Сукуна — бегом */
    int m = if_mon_at(s, F->x), best = -1;
    float bd = 1e9f, cx = (s->monL[m] + s->monR[m]) * 0.5f;
    for (int i = 0; i < s->nMon; i++) {
      if (i == m) continue;
      float c2 = (s->monL[i] + s->monR[i]) * 0.5f, dd = fabsf(c2 - cx) * (0.6f + if_rf(&s->seed));
      if (dd < bd) bd = dd, best = i;
    }
    if (best < 0) break;
    float w = s->monR[best] - s->monL[best];
    F->home = s->monL[best] + w * if_rr(&s->seed, 0.3f, 0.7f);
    F->homeFl = if_ground(s, F->home);
    F->intent = IN_HOME;
    s->roamT = if_rr(&s->seed, 700, 1300);
    if (F->who) if_call(s, F, L"Догони, если сможешь", FC(255, 120, 120), 70);
    break;
  }
  case ACT_FLANK: /* Сукуна — высоким прыжком через великана, по ту сторону от Годжо: клещи */
  case ACT_CHASE: { /* Годжо подброшен — следом в прыжок, сверху ударом о землю */
    float side = O->x >= s->mahoX ? 1.0f : -1.0f, tx = act == ACT_FLANK ? O->x + side * 36 : O->x - (O->x > F->x ? 22.0f : -22.0f);
    if (tx < s->xMin + 8 || tx > s->xMax - 8) { /* за ним край экрана — режет издали */
      if (if_afford(s, F, MV_S_DISM)) if_start(s, F, MV_S_DISM);
      break;
    }
    float vy = act == ACT_FLANK ? 9.5f : 8.0f, T = 2 * vy / 0.36f;
    if_set_state(F, FS_JUMP);
    if_set_anim(F, AN_JUMP, 1);
    F->vy = vy, F->vx = fa_clampf((tx - F->x) / T, -6.0f, 6.0f);
    if (fabsf(tx - F->x) > 2) F->f = tx > F->x ? 1 : -1;
    F->intent = IN_NONE;
    s->flankT = T + 10;
    for (int i = 0; i < 8; i++)
      if_part(s, PT_DUST, F->x + if_rr(&s->seed, -8, 8), F->fl + 1, if_rr(&s->seed, -1.8f, 1.8f), if_rr(&s->seed, 0.3f, 1.2f),
              if_rr(&s->seed, 16, 26), if_rr(&s->seed, 2, 3.5f), FC(200, 196, 190));
    break;
  }
  case ACT_DROP: /* к нему вниз: с окна — спрыгнуть, с высокого монитора — сойти с края */
    if (if_on_window(s, F)) {
      if_set_state(F, FS_JUMP);
      if_set_anim(F, AN_JUMP, 1);
      F->dropT = 12, F->vy = 2.4f, F->vx = F->f * 2.0f;
    } else {
      F->intent = IN_NEAR, F->intentD = 20;
    }
    break;
  default:
    F->intent = IN_WAIT;
    F->intentT = if_rr(&s->seed, 8, 22);
    break;
  }
}

static void if_ai(IfScene *s, IfFighter *F) {
  IfFighter *O = if_foe(s, F);
  float d = fabsf(O->x - F->x), dM = if_maho_on(s) ? fabsf(s->mahoX - F->x) : 1e9f;
  IfOpt o[24];
  int n = 0;
  F->think = if_rr(&s->seed, 4, 12);
  if (s->dom && s->domPhase != DP_CLOSE) { /* в территории */
    if (s->dom == DOM_CLASH) return;
    if (s->domOwner == F->who && F->who == 0) { /* Годжо бьёт замершего; тень встаёт — отходит */
      if (O->st == FS_MOVE && O->mv == MV_S_MAHO) if_opt(s, F, o, &n, ACT_BACK, 10);
      else if (d > 46) if_opt(s, F, o, &n, ACT_NEAR, 10);
      else {
        if_opt(s, F, o, &n, MV_G_JAB, 10), if_opt(s, F, o, &n, MV_G_PALM, 8), if_opt(s, F, o, &n, MV_G_KICK, 8);
        if_opt(s, F, o, &n, ACT_WAIT, 6);
      }
    } else if (s->domOwner == F->who) { /* Сукуна смеётся: режет территория */
      if_opt(s, F, o, &n, ACT_WAIT, 10);
      if_opt(s, F, o, &n, MV_S_TAUNT, 5);
      if (d > 60) if_opt(s, F, o, &n, ACT_NEAR, 3);
    } else {
      return;
    }
  } else if ((O->st == FS_DOWN || O->st == FS_GETUP || O->st == FS_KO) && !if_maho_first(s, F)) {
    if_opt(s, F, o, &n, ACT_WAIT, 3); /* лежит — подойти и ждать, пока встанет */
    if_opt(s, F, o, &n, F->who ? MV_S_TAUNT : MV_G_TAUNT, 1);
    if (d > 50) if_opt(s, F, o, &n, ACT_NEAR, 10);
    if (d < 34) if_opt(s, F, o, &n, ACT_BACK, 4);
  } else if (fabsf(O->fl - F->fl) > 12 && if_up(O) < 4 && !if_maho_first(s, F)) { /* он на другой высоте: окно, другой монитор */
    float dy = O->fl - F->fl;
    int mv = dy > 0 ? ACT_LEAP : ACT_DROP;
    if (F->who == 0) { /* сверху Годжо не спешит вниз: бьёт Красным */
      if_opt(s, F, o, &n, mv, dy > 0 ? 26 : 14), if_opt(s, F, o, &n, MV_G_TELE, 16);
      if_opt(s, F, o, &n, MV_G_RED, dy > 0 ? 14 : 24), if_opt(s, F, o, &n, MV_G_REDX, 12);
      if (d > 40) if_opt(s, F, o, &n, MV_G_BLUE, 5);
    } else {
      if_opt(s, F, o, &n, mv, 34);
      if (dy > 0 && dy < 70) if_opt(s, F, o, &n, MV_S_DISM, 12), if_opt(s, F, o, &n, MV_S_FAN, 10);
      if (dy < 0) if_opt(s, F, o, &n, MV_S_TAUNT, 4);
    }
    if_opt(s, F, o, &n, ACT_WAIT, 2);
  } else {
    int roam = s->act == SC_OPEN && s->actT < 600; /* окна и другие мониторы — пока разминка; потом сходятся */
    float px, py;
    if (roam && d > 46 && if_perch(s, F, &px, &py)) if_opt(s, F, o, &n, ACT_PERCH, 7);
    if (d > 46 && if_on_window(s, F)) if_opt(s, F, o, &n, ACT_DROP, 6); /* бой — больше на земле */
    if (roam && s->nMon > 1 && s->roamT <= 0 && d < 160 && F->hp > 250) if_opt(s, F, o, &n, ACT_ROAM, 9); /* бой — на другой монитор */
    if (!roam && d > 110 && !if_maho_on(s)) if_opt(s, F, o, &n, ACT_NEAR, 30);
    if (if_maho_first(s, F)) { /* великан на нём: бьёт по ногам, Красным в грудь, Синим тянет; Сукуна подождёт */
      if ((O->x - F->x) * (s->mahoX - F->x) < 0 && d < 100 && if_neutral(O)) { /* в клещах: исчезнуть или стянуть обоих Синим */
        if_opt(s, F, o, &n, MV_G_TELE, 16), if_opt(s, F, o, &n, MV_G_BLUE, 14), if_opt(s, F, o, &n, MV_G_REDX, 8);
        if_opt(s, F, o, &n, ACT_JUMP, 6);
      }
      if (dM < 56) {
        if_opt(s, F, o, &n, MV_G_JAB, 20), if_opt(s, F, o, &n, MV_G_PALM, 12), if_opt(s, F, o, &n, MV_G_KICK, 18);
        if_opt(s, F, o, &n, MV_G_HIGH, 12), if_opt(s, F, o, &n, MV_G_UPKICK, 8), if_opt(s, F, o, &n, MV_G_FLICK, 6);
        if_opt(s, F, o, &n, MV_G_RED, 12), if_opt(s, F, o, &n, MV_G_BLUE, 5), if_opt(s, F, o, &n, MV_G_TELE, 6);
        if_opt(s, F, o, &n, ACT_BACK, 8), if_opt(s, F, o, &n, ACT_JUMP, 4);
      } else if (dM < 150) {
        if_opt(s, F, o, &n, ACT_NEAR, 22), if_opt(s, F, o, &n, MV_G_LUNGE, 12), if_opt(s, F, o, &n, MV_G_RED, 18);
        if_opt(s, F, o, &n, MV_G_REDX, 10), if_opt(s, F, o, &n, MV_G_BLUE, 8), if_opt(s, F, o, &n, MV_G_TELE, 5);
        if_opt(s, F, o, &n, ACT_WAIT, 2);
      } else {
        if_opt(s, F, o, &n, ACT_NEAR, 24), if_opt(s, F, o, &n, MV_G_RED, 14), if_opt(s, F, o, &n, MV_G_REDX, 8);
        if_opt(s, F, o, &n, MV_G_TELE, 8), if_opt(s, F, o, &n, ACT_WAIT, 2);
      }
    } else if (if_maho_on(s) && F->who == 1 && s->tagT > 0 && (O->st == FS_FLY || O->st == FS_HIT) && d < 170) {
      /* Махорага отбросил Годжо — Сукуна подхватывает: в прыжок следом, рывком или разрезами */
      if_opt(s, F, o, &n, ACT_CHASE, d < 140 ? 22 : 6), if_opt(s, F, o, &n, MV_S_DISM, 14), if_opt(s, F, o, &n, MV_S_FAN, 8);
      if (d < 46) if_opt(s, F, o, &n, MV_S_UPPER, 12), if_opt(s, F, o, &n, MV_S_CLEAVE, 12);
      else if (d < 115) if_opt(s, F, o, &n, MV_S_LUNGE, 14);
    } else if (if_maho_on(s) && F->who == 1 && (F->x - O->x) * (s->mahoX - O->x) > 0 &&
               fabsf(F->x - O->x) > fabsf(s->mahoX - O->x)) {
      /* великан между ними: Сукуна перепрыгивает на ту сторону (клещи) или режет поверх его плеча */
      if_opt(s, F, o, &n, ACT_FLANK, d < 200 ? 20 : 8), if_opt(s, F, o, &n, MV_S_DISM, 14), if_opt(s, F, o, &n, MV_S_FAN, 9);
      if (d > 200) if_opt(s, F, o, &n, ACT_NEAR, 14);
      if_opt(s, F, o, &n, ACT_WAIT, 4), if_opt(s, F, o, &n, MV_S_TAUNT, 1.5f);
    } else if (s->act == SC_CLASH || s->act == SC_WORLD) { /* перед знаками и перед развязкой — сходятся и ждут */
      if (d > 90) if_opt(s, F, o, &n, ACT_NEAR, 20);
      else if (d < 60) if_opt(s, F, o, &n, ACT_BACK, 10);
      if_opt(s, F, o, &n, ACT_WAIT, 10);
    } else if (F->who == 0) {
      if (O->st == FS_FLY && d < 110) {
        if_opt(s, F, o, &n, MV_G_RED, 18), if_opt(s, F, o, &n, ACT_NEAR, 20), if_opt(s, F, o, &n, MV_G_TELE, 8);
        if_opt(s, F, o, &n, MV_G_REDX, 10), if_opt(s, F, o, &n, MV_G_LUNGE, 10);
      } else if (d < 46) {
        if_opt(s, F, o, &n, MV_G_JAB, 24), if_opt(s, F, o, &n, MV_G_PALM, 14), if_opt(s, F, o, &n, MV_G_KICK, 18);
        if_opt(s, F, o, &n, MV_G_HIGH, 10), if_opt(s, F, o, &n, MV_G_UPKICK, 10), if_opt(s, F, o, &n, MV_G_FLICK, 8);
        if_opt(s, F, o, &n, MV_G_RED, 5), if_opt(s, F, o, &n, MV_G_BLUE, 3);
        if_opt(s, F, o, &n, MV_G_TELE, 5), if_opt(s, F, o, &n, ACT_BACK, 5);
      } else if (d < 110) {
        if_opt(s, F, o, &n, ACT_NEAR, 26), if_opt(s, F, o, &n, MV_G_LUNGE, 20), if_opt(s, F, o, &n, MV_G_RED, 14);
        if_opt(s, F, o, &n, MV_G_BLUE, 12), if_opt(s, F, o, &n, MV_G_REDX, 10);
        if_opt(s, F, o, &n, MV_G_TELE, 10), if_opt(s, F, o, &n, ACT_JUMP, 6), if_opt(s, F, o, &n, MV_G_TAUNT, 1.5f);
        if_opt(s, F, o, &n, ACT_WAIT, 2);
      } else {
        if_opt(s, F, o, &n, MV_G_RED, 18), if_opt(s, F, o, &n, ACT_NEAR, 26), if_opt(s, F, o, &n, MV_G_BLUE, 8);
        if_opt(s, F, o, &n, MV_G_REDX, 14);
        if_opt(s, F, o, &n, MV_G_TELE, d > 220 ? 30 : 9), if_opt(s, F, o, &n, MV_G_TAUNT, 2), if_opt(s, F, o, &n, ACT_WAIT, 1.5f);
      }
    } else {
      if (O->st == FS_FLY && d < 110) {
        if_opt(s, F, o, &n, MV_S_DISM, 18), if_opt(s, F, o, &n, ACT_NEAR, 20), if_opt(s, F, o, &n, ACT_JUMP, 6);
        if_opt(s, F, o, &n, MV_S_LUNGE, 12);
      } else if (d < 46) {
        if_opt(s, F, o, &n, MV_S_CLAW, 22), if_opt(s, F, o, &n, MV_S_SWIPE, 18), if_opt(s, F, o, &n, MV_S_PUNCH, 18);
        if_opt(s, F, o, &n, MV_S_KNEE, 12), if_opt(s, F, o, &n, MV_S_UPPER, 10), if_opt(s, F, o, &n, MV_S_KICK, 12);
        if_opt(s, F, o, &n, MV_S_CLEAVE, 12), if_opt(s, F, o, &n, MV_S_DISM, 4), if_opt(s, F, o, &n, ACT_BACK, 4);
      } else if (d < 110) {
        if_opt(s, F, o, &n, ACT_NEAR, 30), if_opt(s, F, o, &n, MV_S_LUNGE, 22), if_opt(s, F, o, &n, MV_S_DISM, 16);
        if_opt(s, F, o, &n, MV_S_FAN, 10), if_opt(s, F, o, &n, ACT_JUMP, 10);
        if_opt(s, F, o, &n, MV_S_TAUNT, 1.5f), if_opt(s, F, o, &n, ACT_WAIT, 2);
      } else {
        if_opt(s, F, o, &n, MV_S_DISM, 18), if_opt(s, F, o, &n, MV_S_FAN, 14), if_opt(s, F, o, &n, ACT_NEAR, 36);
        if_opt(s, F, o, &n, MV_S_TAUNT, 3);
      }
    }
  }
  if (!n) return;
  float sum = 0;
  for (int i = 0; i < n; i++) sum += o[i].w;
  float r = if_rf(&s->seed) * sum;
  int pick = o[n - 1].act;
  for (int i = 0; i < n; i++) {
    if (r < o[i].w) {
      pick = o[i].act;
      break;
    }
    r -= o[i].w;
  }
  if_do(s, F, pick);
}

/* увернуться или закрыться: от удара, который вот-вот дойдёт, и от снаряда */
static void if_guard(IfFighter *F) {
  if_set_state(F, FS_GUARD);
  if_set_anim(F, AN_GUARD, 1);
  F->vx = 0;
}

static void if_react(IfScene *s, IfFighter *F) {
  IfFighter *O = if_foe(s, F);
  float d = fabsf(O->x - F->x);
  if (F->who == 0 && s->mahoSt == MS_ATK && s->mahoMv >= 0 && s->mahoMv != MM_STOMP && s->mahoSeen != s->mahoId) {
    int mv = s->mahoMv; /* великан замахнулся: исчезнуть, закрыться, перепрыгнуть размах или таран */
    float left = (mv == MM_CHARGE ? 12.0f : kMahoHitAt[mv]) - s->mahoMt, adM = fabsf(s->mahoX - F->x);
    if (left > 0 && left <= 6 && adM < (mv == MM_CHARGE ? 200 : 140)) {
      s->mahoSeen = s->mahoId;
      float r = if_rf(&s->seed);
      int low = mv == MM_SWEEP || mv == MM_CHARGE; /* низом: можно перепрыгнуть */
      if (r < 0.12f && if_afford(s, F, MV_G_TELE)) {
        if_start(s, F, MV_G_TELE);
      } else if (low && r < 0.34f) {
        if_set_state(F, FS_JUMP);
        if_set_anim(F, AN_JUMP, 1);
        F->vy = 8.0f, F->vx = (s->mahoX > F->x ? 1.0f : -1.0f) * (mv == MM_CHARGE ? 2.6f : 0.6f);
      } else if (r < 0.3f) {
        if_guard(F);
      }
      return;
    }
  }
  if (O->st == FS_MOVE && O->mv >= 0 && (kMoves[O->mv].flags & MVF_MELEE) && F->seenId != O->mvId) {
    const FMove *m = &kMoves[O->mv];
    float left = m->hitA - O->mt;
    if (left > 0 && left <= 6 && d < 60) {
      F->seenId = O->mvId;
      float r = if_rf(&s->seed);
      if (F->who == 1 && r < 0.24f) {
        if_guard(F);
      } else if (F->who == 0 && r < 0.10f && if_afford(s, F, MV_G_TELE)) {
        if_start(s, F, MV_G_TELE);
      } else if (F->who == 0 && r < 0.22f) {
        if_guard(F);
      }
      return;
    }
  }
  for (int i = 0; i < IF_MAXJ; i++) {
    IfProj *p = &s->pj[i];
    if (p->kind == PJ_NONE || p->kind == PJ_BLUE || p->owner == F->who || p->hit || p->seen) continue;
    float dx = F->x - p->x;
    if (dx * p->vx <= 0 || fabsf(dx) > 75) continue;
    p->seen = 1;
    float r = if_rf(&s->seed);
    if (F->who == 0 && r < 0.2f && if_afford(s, F, MV_G_TELE)) {
      if_start(s, F, MV_G_TELE);
      return;
    }
    if (F->who == 1 && p->kind == PJ_RED && r < 0.45f) {
      if (r < 0.22f) {
        if_set_state(F, FS_JUMP);
        if_set_anim(F, AN_JUMP, 1);
        F->vy = 7.0f, F->vx = F->f * 1.5f;
      } else {
        if_guard(F);
      }
      return;
    }
  }
}

/* ===================== бойцы: шаг ===================== */

static void if_land_dust(IfScene *s, float x, float y, int n) {
  for (int i = 0; i < n; i++)
    if_part(s, PT_DUST, x + if_rr(&s->seed, -9, 9), y + 1, if_rr(&s->seed, -1.8f, 1.8f), if_rr(&s->seed, 0.2f, 1.1f),
            if_rr(&s->seed, 18, 34), if_rr(&s->seed, 2, 4.5f), FC(205, 200, 194));
}

static int if_victim(IfScene *s, const IfFighter *F) {
  return s->dom && s->dom != DOM_CLASH && F->who != s->domOwner && (s->domPhase == DP_OPEN || s->domPhase == DP_ON);
}

/* обратная техника в крайний миг: здоровье упало ниже четверти, потом ниже десятой — каждый раз
   монетка (лечится не всегда), не больше двух раз за раунд и пока есть энергия */
static int if_try_rct(IfScene *s, IfFighter *F) {
  if (s->act >= SC_WORLD || F->hp <= 0) return 0;
  float k = F->hp / IF_HP;
  if (k > 0.4f) F->rctTry = 0;
  int lvl = k < 0.1f ? 2 : (k < 0.25f ? 1 : 0), mv = F->who ? MV_S_RCT : MV_G_HEAL;
  if (lvl <= F->rctTry) return 0;
  F->rctTry = lvl;
  if (F->rctN >= 2 || !if_can(F, mv) || if_rf(&s->seed) >= 0.55f) return 0;
  if_start(s, F, mv);
  return 1;
}

static void if_fighter_step(IfScene *s, IfFighter *F, float dt) {
  IfFighter *O = if_foe(s, F);
  F->stT += dt;
  for (int i = 0; i < MV_COUNT; i++) {
    if (F->cd[i] > 0) F->cd[i] -= dt;
    F->used[i] *= 1.0f - 0.0015f * dt;
  }
  if (F->st != FS_MOVE && F->trailN > 0) F->trailN--; /* удар сбили — след всё равно гаснет */
  if (s->phase == RP_FIGHT && F->burn <= 0 && F->ce < IF_CE) F->ce += 0.45f * dt; /* энергия копится сама */
  if (F->inv > 0) F->inv -= dt;
  if (F->burn > 0) F->burn -= dt;
  if (F->ghostT > 0) F->ghostT -= dt;
  else if (F->ghost > F->hp) F->ghost -= 7 * dt;
  if (F->ghost < F->hp) F->ghost = F->hp;
  if (F->dropT > 0) F->dropT -= dt;
  F->fl = if_floor(s, F->x, F->y, F->dropT > 0);
  if (F->stuck > 0) { /* кулак застрял в Бесконечности: время у него стоит */
    F->stuck -= dt;
    return;
  }
  F->at += dt;
  int air = 0;
  if ((F->st == FS_IDLE || F->st == FS_WALK || F->st == FS_RUN) && if_up(F) > 6) { /* сошёл с края окна */
    if_set_state(F, FS_JUMP);
    if_set_anim(F, AN_JUMP, 1);
    F->intent = IN_NONE;
  } else if (if_up(F) > 0 && if_up(F) <= 6 && F->vy <= 0 && F->st != FS_FLY && F->st != FS_JUMP && F->st != FS_MOVE) {
    F->y = F->fl; /* ступенька вниз — просто шагнул */
  }
  switch (F->st) {
  case FS_IDLE:
  case FS_WALK:
  case FS_RUN: {
    float tx = if_maho_first(s, F) ? s->mahoX : O->x;
    if (fabsf(tx - F->x) > 2) F->f = tx > F->x ? 1 : -1;
    if (if_victim(s, F)) {
      if_set_state(F, s->dom == DOM_VOID ? FS_STUN : FS_TRAP);
      if_set_anim(F, s->dom == DOM_VOID ? AN_STUN : AN_GUARD, 1);
      F->vx = 0;
      break;
    }
    if (s->phase == RP_FIGHT && if_try_rct(s, F)) break;
    if (s->phase == RP_FIGHT) {
      if_react(s, F);
      if (!if_neutral(F)) break;
      F->think -= dt;
      if (F->think <= 0 && (F->intent == IN_NONE || F->intent == IN_WAIT || (F->intent == IN_NEAR && if_rf(&s->seed) < 0.3f)))
        if_ai(s, F);
      if (!if_neutral(F)) break;
    }
    float d = if_gojo_dist(s, F), mvx = 0;
    int st = FS_IDLE, anim = if_idle_anim(F);
    if (F->intent == IN_NEAR) {
      if (d > F->intentD) {
        int run = F->who ? d > 50 : d > 80;
        float sp = run ? (F->who ? 3.4f : 3.9f) : 1.9f;
        if (d > 220) sp = F->who ? 7.0f : 6.5f; /* догнать на другом мониторе */
        mvx = F->f * sp;
        st = run ? FS_RUN : FS_WALK;
        anim = run ? (F->who ? AN_S_RUN : AN_G_DASH) : (F->who ? AN_S_WALK : AN_G_WALK);
      } else {
        F->intent = IN_NONE;
        F->think = 0;
      }
    } else if (F->intent == IN_BACK) {
      F->intentT -= dt;
      float bx = F->x - F->f * 2;
      if (F->intentT > 0 && bx > s->xMin + 2 && bx < s->xMax - 2) {
        mvx = -F->f * 1.7f;
        st = FS_WALK;
        anim = F->who ? AN_S_BACK : AN_G_BACK;
      } else {
        F->intent = IN_NONE;
      }
    } else if (F->intent == IN_WAIT) {
      F->intentT -= dt;
      if (F->intentT <= 0) F->intent = IN_NONE;
    } else if (F->intent == IN_HOME) {
      float dx = F->home - F->x, dy = F->homeFl - F->fl;
      if (F->who == 0 && fabsf(dx) > 260) { /* Годжо далеко не ходит: телепорт */
        if_teleport_out(s, F);
        F->x = F->home;
        F->y = F->fl = F->homeFl = if_floor(s, F->home, if_ground(s, F->home) + 1, 0);
        F->vx = F->vy = 0;
        F->intent = IN_NONE;
        F->f = O->x >= F->x ? 1 : -1;
        if_eff(s, EF_TELE, F->x, F->y, 16, 1);
        break;
      }
      if (F->wallT > 10 || (dy > 12 && fabsf(dx) < 150)) { /* дом выше или стена: запрыгнуть */
        if (F->wallT > 10) dy = fmaxf(dy, if_ground(s, F->x + (dx > 0 ? 14.0f : -14.0f)) - F->y);
        if_leap(F, dx, dy);
        F->intent = IN_HOME;
        break;
      }
      if (fabsf(dx) <= 3 && dy < -12 && if_on_window(s, F)) { /* дом под окном: спрыгнуть */
        if_set_state(F, FS_JUMP);
        if_set_anim(F, AN_JUMP, 1);
        F->dropT = 12, F->vy = 2.4f;
        break;
      }
      if (fabsf(dx) > 3) {
        int run = fabsf(dx) > 120; /* далеко, с другого монитора — бегом */
        mvx = (dx > 0 ? 1.0f : -1.0f) * (run ? (fabsf(dx) > 300 ? 7.0f : 3.6f) : 2.2f);
        st = run ? FS_RUN : FS_WALK;
        if (run) F->f = dx > 0 ? 1 : -1;
        anim = run ? (F->who ? AN_S_RUN : AN_G_DASH)
                   : (dx > 0) == (F->f > 0) ? (F->who ? AN_S_WALK : AN_G_WALK) : (F->who ? AN_S_BACK : AN_G_BACK);
      } else {
        F->intent = IN_NONE;
      }
    }
    if (F->intent == IN_NEAR && F->wallT > 12 && fabsf(O->fl - F->fl) <= 12) { /* между ними монитор выше — перепрыгнуть */
      if_leap(F, F->f * 70.0f, if_ground(s, F->x + F->f * 14) - F->y);
      break;
    }
    F->vx = mvx;
    F->st = st;
    if_set_anim(F, anim, 0);
    break;
  }
  case FS_JUMP:
    air = 1;
    if (F->vy < 0 && s->phase == RP_FIGHT && if_up(F) > 14 && fabsf(O->x - F->x) < 46 && O->st != FS_DOWN && O->st != FS_KO &&
        F->stT > 10) {
      int mv = F->who ? MV_S_SLAM : MV_G_DIVE;
      if (if_can(F, mv) && if_rf(&s->seed) < (F->who && s->flankT > 0 ? 0.6f : 0.25f)) {
        if_start(s, F, mv);
        air = 1;
      }
    }
    break;
  case FS_LAND:
    F->vx *= powf(0.7f, dt);
    if (F->stT >= 8) if_set_state(F, FS_IDLE);
    break;
  case FS_MOVE:
    if (F->mv == MV_G_TELE) F->alpha = F->mt < 2 ? 1.0f : (F->mt < 10 ? 0.0f : fa_sat((F->mt - 10) / 4));
    if_move_step(s, F, dt);
    if (F->st == FS_MOVE && F->mv >= 0 && (kMoves[F->mv].flags & MVF_AIR)) air = 1;
    if (F->st != FS_MOVE) F->alpha = 1;
    break;
  case FS_HIT:
    F->stun -= dt;
    F->vx *= powf(0.86f, dt);
    if (F->stun <= 0) {
      if_set_state(F, FS_IDLE);
      F->think = if_rr(&s->seed, 4, 12);
    }
    break;
  case FS_FLY:
    air = 1;
    F->roll += F->vroll * dt;
    break;
  case FS_DOWN:
    F->vx *= powf(0.8f, dt);
    if (F->stT >= 34) {
      if_set_state(F, FS_GETUP);
      if_set_anim(F, AN_GETUP, 1);
      F->inv = 26;
    }
    break;
  case FS_GETUP:
    if (F->stT >= 24) if_set_state(F, FS_IDLE);
    break;
  case FS_GUARD:
    F->vx *= powf(0.85f, dt);
    if (F->stT >= 24) if_set_state(F, FS_IDLE);
    break;
  case FS_STUN:
  case FS_TRAP:
    F->vx *= powf(0.8f, dt);
    if (!if_victim(s, F)) if_set_state(F, FS_IDLE);
    else if (F->st == FS_TRAP && F->anim == AN_HURT_H && F->stT > 14) if_set_anim(F, AN_GUARD, 1);
    break;
  case FS_KO:
  case FS_WIN:
    F->vx *= powf(0.8f, dt);
    break;
  }
  /* полёт и земля */
  if (air || F->y > F->fl || F->vy > 0) {
    F->vy -= 0.36f * dt;
    F->y += F->vy * dt;
    if (F->y <= F->fl && F->vy <= 0) {
      F->y = F->fl;
      if (F->st == FS_JUMP) {
        F->vy = 0;
        if_set_state(F, FS_LAND);
        if_set_anim(F, AN_LAND, 1);
        if_land_dust(s, F->x, F->fl, 5);
      } else if (F->st == FS_FLY) {
        if (F->vy < -6.5f) { /* о землю — и ещё раз подлетел */
          F->vy = -F->vy * 0.36f;
          F->vx *= 0.6f;
          if_land_dust(s, F->x, F->fl, 10);
          if_eff(s, EF_CRACK, F->x, F->fl, 70, 1);
          if_shake(s, 6);
        } else {
          F->vy = 0;
          if_set_state(F, F->hp <= 0 ? FS_KO : FS_DOWN);
          if_set_anim(F, F->hp <= 0 ? AN_KO : AN_DOWN, 1);
          float r = F->from.v[PV_ROLL]; /* докрутиться до лёжа ближним путём */
          while (r - (-90.0f) > 180.0f) r -= 360.0f;
          while (r - (-90.0f) < -180.0f) r += 360.0f;
          F->from.v[PV_ROLL] = r;
          F->roll = 0;
          F->juggle = 0;
          if_land_dust(s, F->x, F->fl, 8);
        }
      } else if (F->st != FS_MOVE) {
        F->vy = 0;
      }
    }
  }
  float nx = F->x + F->vx * dt, gn = if_ground(s, nx);
  if (gn > F->y + 0.5f) { /* монитор с полом выше: ступенька или стена */
    if (F->y <= F->fl + 0.5f && gn <= F->y + 12 && F->st != FS_FLY) {
      F->y = F->fl = gn;
    } else {
      if (F->st == FS_FLY && fabsf(F->vx) > 3.5f) {
        IfEff *e = if_eff(s, EF_WALL, F->x, F->y + 40, 26, 1);
        if (e) e->a = F->vx > 0 ? 1.0f : -1.0f;
        F->vx = -F->vx * 0.35f;
        if_shake(s, 8);
      } else {
        F->vx = 0;
        F->wallT += dt;
      }
      nx = F->x;
    }
  } else if (F->st == FS_FLY && fabsf(F->vx) > 3.5f) { /* отлетел в окно — ударился о его край */
    float cy = F->y + 40;
    for (int i = s->nFix; i < s->nPl; i++) {
      const IfPlat *p = &s->pl[i];
      if (cy < p->bot || cy > p->top) continue;
      float ex = p->wl && F->x <= p->x0 && nx > p->x0 ? p->x0 : (p->wr && F->x >= p->x1 && nx < p->x1 ? p->x1 : -1e9f);
      if (ex < -1e8f) continue;
      IfEff *e = if_eff(s, EF_WALL, ex, cy, 26, 1);
      if (e) e->a = F->vx > 0 ? 1.0f : -1.0f;
      F->vx = -F->vx * 0.35f;
      if_shake(s, 8);
      nx = ex - (F->vx > 0 ? -0.5f : 0.5f);
      break;
    }
  }
  if (fabsf(F->vx) > 0.3f && nx != F->x) F->wallT = 0;
  F->x = nx;
  if (F->x < s->xMin || F->x > s->xMax) { /* край экрана — стена */
    float wx = F->x < s->xMin ? s->xMin : s->xMax;
    if (F->st == FS_FLY && fabsf(F->vx) > 3.5f) {
      IfEff *e = if_eff(s, EF_WALL, wx, F->y + 40, 26, 1);
      if (e) e->a = F->vx > 0 ? 1.0f : -1.0f;
      F->vx = -F->vx * 0.35f;
      if_shake(s, 8);
    } else if ((F->x < s->xMin && F->vx < 0) || (F->x > s->xMax && F->vx > 0)) {
      F->vx = 0;
    }
    F->x = wx;
  }
  if_anim_pose(&F->pose, F->anim, F->at, &F->from);
  if (F->st == FS_FLY) F->pose.v[PV_ROLL] += F->roll;
  float w = fa_sat(fabsf(F->vx) / 5.0f + (F->vy < -2 ? 0.4f : 0.0f));
  F->wind += (w - F->wind) * 0.15f * dt;
  fa_skel(&F->sk, &F->pose, F->f);
}

static void if_push(IfScene *s) {
  IfFighter *a = &s->F[0], *b = &s->F[1];
  if (a->alpha < 0.5f || b->alpha < 0.5f || if_up(a) > 20 || if_up(b) > 20 || fabsf(a->y - b->y) > 20) return;
  if (a->st == FS_DOWN || b->st == FS_DOWN || a->st == FS_KO || b->st == FS_KO || a->st == FS_FLY || b->st == FS_FLY)
    return;
  float dx = b->x - a->x, ad = fabsf(dx), need = 21;
  if (ad >= need) return;
  float sg = dx >= 0 ? 1.0f : -1.0f, push = (need - ad) * 0.5f;
  if (ad < 0.01f) sg = (float)a->f;
  a->x -= sg * push, b->x += sg * push;
  if (a->x < s->xMin) b->x += s->xMin - a->x, a->x = s->xMin;
  if (b->x > s->xMax) a->x -= b->x - s->xMax, b->x = s->xMax;
  if (a->x > s->xMax) b->x -= a->x - s->xMax, a->x = s->xMax;
  if (b->x < s->xMin) a->x += s->xMin - b->x, b->x = s->xMin;
}

/* ===================== снаряды ===================== */

static int if_pj_touch(IfProj *p, IfFighter *D, float *cx, float *cy) {
  if (D->st == FS_KO || D->inv > 0 || D->hp <= 0 || D->alpha < 0.5f) return 0;
  static const int kHurt[3] = {FJ_HEAD, FJ_CHEST, FJ_HIP};
  static const float kR[3] = {8.5f, 11.0f, 10.0f};
  for (int i = 0; i < 3; i++) {
    float hx, hy;
    if_joint(D, kHurt[i], &hx, &hy);
    float dx = hx - p->x, dy = hy - p->y, rr = p->r + kR[i];
    if (dx * dx + dy * dy <= rr * rr) {
      *cx = p->x + dx * 0.5f, *cy = p->y + dy * 0.5f;
      return 1;
    }
  }
  return 0;
}

static int if_pj_touch_maho(const IfScene *s, const IfProj *p, float *cx, float *cy) {
  if (!if_maho_on(s)) return 0;
  float zx[3], zy[3], zr[3];
  int n = if_maho_zones(s, zx, zy, zr);
  for (int i = 0; i < n; i++) {
    float dx = p->x - zx[i], dy = p->y - zy[i], rr = p->r + zr[i], dd = sqrtf(dx * dx + dy * dy);
    if (dd > rr) continue;
    *cx = dd > 0.01f ? zx[i] + dx / dd * zr[i] : p->x, *cy = dd > 0.01f ? zy[i] + dy / dd * zr[i] : p->y;
    return 1;
  }
  return 0;
}

static void if_boom(IfScene *s, IfProj *p, float big) { /* огненный взрыв: Разверзнись */
  IfEff *e = if_eff(s, EF_BOOM, p->x, p->y, 70, big);
  if (e) e->owner = p->owner;
  for (int i = 0; i < (int)(46 * big); i++) {
    float a = if_rr(&s->seed, 0, 2 * FA_PI), v = if_rr(&s->seed, 0.8f, 5.5f) * big;
    IfPart *q = if_part(s, PT_FIRE, p->x, p->y, cosf(a) * v, sinf(a) * v * 0.8f + 0.8f, if_rr(&s->seed, 22, 46),
                        if_rr(&s->seed, 6, 13) * big, FC(255, 160, 50));
    q->g = -0.04f;
  }
  for (int i = 0; i < (int)(18 * big); i++) {
    float a = if_rr(&s->seed, 0, 2 * FA_PI), v = if_rr(&s->seed, 2, 7) * big;
    IfPart *q = if_part(s, PT_EMBER, p->x, p->y, cosf(a) * v, sinf(a) * v + 2, if_rr(&s->seed, 30, 60), 1.0f, FC(255, 210, 120));
    q->g = 0.15f;
  }
  for (int i = 0; i < (int)(12 * big); i++)
    if_part(s, PT_SMOKE, p->x + if_rr(&s->seed, -20, 20) * big, p->y + if_rr(&s->seed, -10, 20) * big,
            if_rr(&s->seed, -0.6f, 0.6f), if_rr(&s->seed, 0.3f, 1.0f), if_rr(&s->seed, 70, 110), if_rr(&s->seed, 12, 22) * big,
            FC(70, 60, 64));
  if_eff(s, EF_CRACK, p->x, if_floor(s, p->x, p->y, 0), 90, 1.3f * big);
  if_shake(s, 15 * big);
}

/* снаряд пролетел мимо противника и врезался в окно: в край или сверху */
static int if_pj_win(const IfScene *s, const IfProj *p, float ox, float oy, const IfFighter *D) {
  if ((D->x - p->x) * p->vx > 0) return 0; /* противник ещё впереди — окна не помеха */
  for (int i = s->nFix; i < s->nPl; i++) {
    const IfPlat *q = &s->pl[i];
    if (p->y >= q->bot && p->y <= q->top) {
      if (q->wl && ox < q->x0 && p->x >= q->x0) return 1;
      if (q->wr && ox > q->x1 && p->x <= q->x1) return 1;
    }
    if (p->x >= q->x0 && p->x <= q->x1 && oy > q->top && p->y <= q->top) return 1;
  }
  return 0;
}

/* Махорага разбит: тень рассыпается осколками */
static void if_maho_die(IfScene *s) {
  if (!s->maho) return;
  s->maho = 0, s->mahoDie = 40;
  for (int i = 0; i < 70; i++) {
    float a = if_rr(&s->seed, 0, 2 * FA_PI), v = if_rr(&s->seed, 1, 5);
    IfPart *q = if_part(s, PT_SHARD, s->mahoX + if_rr(&s->seed, -60, 60), s->mahoFl + if_rr(&s->seed, 0, 240), cosf(a) * v,
                        sinf(a) * v + 2, if_rr(&s->seed, 30, 60), if_rr(&s->seed, 1.5f, 4), FC(44, 30, 70));
    q->g = 0.22f, q->rot = if_rr(&s->seed, 0, 6);
  }
  IfEff *e = if_eff(s, EF_FLASH, s->mahoX, s->mahoFl + 190, 26, 3.5f);
  if (e) e->c = FC(210, 150, 255);
  if_shake(s, 20);
  if_call(s, &s->F[1], L"Махорага!..", FC(200, 160, 255), 70);
}

static void if_pj_step(IfScene *s, float dt) {
  for (int i = 0; i < IF_MAXJ; i++) {
    IfProj *p = &s->pj[i];
    if (p->kind == PJ_NONE) continue;
    IfFighter *A = &s->F[p->owner], *D = &s->F[!p->owner];
    p->t += dt;
    float cx, cy, ox = p->x, oy = p->y, g = if_ground(s, p->x), sky = s->gy / s->U + 20;
    int out = p->x < s->xMin - 30 || p->x > s->xMax + 30 || p->y < g - 6 || p->y > sky;
    /* Красный и стрела взрываются о край экрана, о землю и об окно позади противника — так взрыв видно */
    int wall = p->x < s->xMin - 4 || p->x > s->xMax + 4 || p->y < g || p->y > sky;
    switch (p->kind) {
    case PJ_BLUE: {
      if (D->hp > 0 && D->st != FS_KO && D->st != FS_DOWN && D->st != FS_GETUP && D->inv <= 0 && p->t > 4 &&
          p->t < p->life - 4) {
        float dx = p->x - D->x, ad = fabsf(dx);
        if (ad < 180) { /* тянет к себе — не вырваться */
          if (D->st != FS_HIT && D->st != FS_FLY) {
            if_set_state(D, FS_HIT);
            if_set_anim(D, AN_HURT_M, 1);
            D->mv = -1;
          }
          D->stun = 12;
          if (ad > 6) D->vx += (dx > 0 ? 1.0f : -1.0f) * 0.6f * dt;
          else D->vx *= 0.5f;
          if (D->vx > 4.5f) D->vx = 4.5f;
          if (D->vx < -4.5f) D->vx = -4.5f;
          if (if_up(D) < 10 && ad < 40) D->vy = 0.9f;
          if (D->hp - 1.3f * dt >= 1) if_hurt(s, D, 1.3f * dt);
          D->ghostT = 30;
          if (D->flash < 0.25f) D->flash = 0.25f;
        }
      }
      if (p->owner == 0 && if_maho_on(s) && p->t > 4 && p->t < p->life - 4) { /* великана тоже тянет, медленно */
        float dx = p->x - s->mahoX, ad = fabsf(dx);
        if (ad < 200 && s->mahoY <= 0) {
          if (s->mahoSt != MS_STAGGER) s->mahoSt = MS_HIT, s->mahoMv = -1;
          s->mahoMt = 2;
          s->mahoVx = fa_clampf(s->mahoVx + (dx > 0 ? 1.0f : -1.0f) * 0.25f * dt, -2.2f, 2.2f);
          s->mahoHp = fmaxf(1, s->mahoHp - 0.8f * dt), s->mahoCalm = 0;
          if (s->mahoFlash < 0.25f) s->mahoFlash = 0.25f;
        }
      }
      for (int k = 0; k < 2; k++) { /* обломки и пыль летят внутрь */
        float a = if_rr(&s->seed, 0, 2 * FA_PI), r = if_rr(&s->seed, 30, 75);
        IfPart *q = if_part(s, PT_BIT, p->x + cosf(a) * r, p->y + sinf(a) * r, -cosf(a) * r / 16, -sinf(a) * r / 16, 16,
                            if_rr(&s->seed, 0.8f, 2.0f), if_rf(&s->seed) < 0.5f ? FC(120, 190, 255) : FC(70, 80, 110));
        q->rot = if_rr(&s->seed, 0, 6);
      }
      if (p->t >= p->life) {
        IfEff *e = if_eff(s, EF_IMPLODE, p->x, p->y, 22, 1);
        if (e) e->owner = p->owner;
        if (fabsf(D->x - p->x) < 26) if_damage(s, A, D, 35, HK_HEAVY, 1.0f, 0.5f, p->x, p->y, 2);
        if (p->owner == 0 && if_maho_on(s) && fabsf(s->mahoX - p->x) < 60) if_maho_damage(s, A, 60, HK_HEAVY, 1.5f, p->x, p->y, 2);
        if_shake(s, 7);
        p->kind = PJ_NONE;
      }
      break;
    }
    case PJ_RED:
      p->x += p->vx * dt, p->y += p->vy * dt;
      wall = wall || if_pj_win(s, p, ox, oy, D);
      if (if_rf(&s->seed) < 0.9f) {
        IfPart *q = if_part(s, PT_SPARK, p->x - p->vx, p->y + if_rr(&s->seed, -3, 3), -p->vx * 0.2f, if_rr(&s->seed, -1, 1),
                            if_rr(&s->seed, 8, 14), if_rr(&s->seed, 0.7f, 1.4f), FC(255, 120, 80));
        q->light = 1;
      }
      if (p->owner == 0 && if_pj_touch_maho(s, p, &cx, &cy)) { /* Махорага закрывает хозяина собой */
        int sm = p->a > 0.5f;
        IfEff *e = if_eff(s, EF_REDBOOM, p->x, p->y, 34, sm ? 0.8f : 1.3f);
        if (e) e->owner = p->owner;
        if_maho_damage(s, A, sm ? 50 : 150, sm ? HK_KNOCK : HK_BLAST, sm ? 4 : 9, cx, cy, 2);
        if_shake(s, sm ? 7 : 13);
        p->kind = PJ_NONE;
        break;
      }
      if (if_pj_touch(p, D, &cx, &cy) || wall) {
        int sm = p->a > 0.5f; /* из залпа: слабее */
        IfEff *e = if_eff(s, EF_REDBOOM, p->x, p->y, 34, (wall ? 0.7f : 1.3f) * (sm ? 0.6f : 1.0f));
        if (e) e->owner = p->owner;
        if (!wall) {
          if (sm) if_damage(s, A, D, 55, HK_KNOCK, 5, 3.5f, cx, cy, 2);
          else if_damage(s, A, D, 165, HK_BLAST, 11, 6.5f, cx, cy, 2);
        }
        if_shake(s, wall ? 6 : (sm ? 7 : 13));
        p->kind = PJ_NONE;
      }
      break;
    case PJ_PURPLE: {
      int mx = p->a > 0.5f; /* на всю мощь: вдвое шире, сносит Махорагу */
      p->x += p->vx * dt;
      float pfl = if_floor(s, p->x, p->y, 0);
      if (((int)p->t % 3) == 0 && p->y - pfl < (mx ? 90 : 70)) if_eff(s, EF_SCORCH, p->x - p->vx * 4, pfl, 110, mx ? 1.8f : 1.0f);
      if (s->maho && s->mahoDie <= 0 && fabsf(p->x - s->mahoX) < p->r + 24) if_maho_die(s);
      for (int k = 0; k < (mx ? 5 : 3); k++) {
        float a = if_rr(&s->seed, 0, 2 * FA_PI);
        IfPart *q = if_part(s, PT_SPARK, p->x + cosf(a) * p->r, p->y + sinf(a) * p->r, -p->vx * 0.3f + cosf(a) * 1.5f,
                            sinf(a) * 1.5f, if_rr(&s->seed, 10, 20), if_rr(&s->seed, 0.8f, 1.6f),
                            k ? FC(210, 140, 255) : FC(255, 255, 255));
        q->light = 1;
      }
      if (if_rf(&s->seed) < 0.5f) {
        IfPart *q = if_part(s, PT_SHARD, p->x - p->vx * 3, pfl + if_rr(&s->seed, 0, 6), if_rr(&s->seed, -2, 2) - p->vx * 0.3f,
                            if_rr(&s->seed, 2, 6), if_rr(&s->seed, 30, 50), if_rr(&s->seed, 1, 2.5f), FC(90, 80, 100));
        q->g = 0.3f, q->rot = if_rr(&s->seed, 0, 6);
      }
      if_shake(s, mx ? 5 : 3);
      if (!p->hit && if_pj_touch(p, D, &cx, &cy)) {
        p->hit = 1;
        if_damage(s, A, D, mx ? 420 : 260, HK_BLAST, mx ? 14 : 12, mx ? 8.5f : 7.5f, cx, cy, 2);
        IfEff *e = if_eff(s, EF_FLASH, cx, cy, 24, mx ? 4.0f : 3.0f);
        if (e) e->c = FC(210, 130, 255);
        if_shake(s, mx ? 24 : 18);
      }
      if (out) {
        p->kind = PJ_NONE;
        if (mx && s->act == SC_PURPLE) s->step = 2;
      }
      break;
    }
    case PJ_SLASH:
      p->x += p->vx * dt;
      p->y += p->vy * dt;
      if (!p->hit && if_pj_touch(p, D, &cx, &cy)) {
        p->hit = 1;
        IfEff *e = if_eff(s, EF_CUTS, cx, cy, 22, 1);
        if (e) e->a = p->a;
        if_damage(s, A, D, 24, p->r > 11.5f ? HK_HEAVY : HK_LIGHT, 2.0f, 0.5f, cx, cy, 1);
        p->kind = PJ_NONE;
      } else if (out || p->t > p->life) {
        p->kind = PJ_NONE;
      }
      break;
    case PJ_ARROW:
      p->x += p->vx * dt, p->y += p->vy * dt;
      wall = wall || if_pj_win(s, p, ox, oy, D);
      for (int k = 0; k < 3; k++) {
        IfPart *q = if_part(s, PT_FIRE, p->x - p->vx * if_rr(&s->seed, 0.2f, 1.4f), p->y + if_rr(&s->seed, -2, 2),
                            -p->vx * 0.15f, if_rr(&s->seed, 0.1f, 0.8f), if_rr(&s->seed, 10, 20), if_rr(&s->seed, 2.5f, 5),
                            FC(255, 150, 40));
        q->g = -0.03f;
      }
      if (if_pj_touch(p, D, &cx, &cy) || wall) {
        if_boom(s, p, 1.0f);
        if (!wall || fabsf(D->x - p->x) < 50) {
          float hx, hy;
          if_joint(D, FJ_CHEST, &hx, &hy);
          if (hypotf(hx - p->x, hy - p->y) < 58) if_damage(s, A, D, 200, HK_BLAST, 10, 7, hx, hy, 2);
        }
        p->kind = PJ_NONE;
      }
      break;
    }
  }
}

/* ===================== территория ===================== */

/* территория режет сама: без остановки кадра, чтобы бой не дёргался */
static void if_dom_cut(IfScene *s, IfFighter *D, float dmg, int flinch) {
  if (D->hp <= 0 || D->st == FS_KO) return;
  IfFighter *A = if_foe(s, D);
  float hx, hy;
  if_joint(D, if_rf(&s->seed) < 0.5f ? FJ_CHEST : FJ_HEAD, &hx, &hy);
  hx += if_rr(&s->seed, -6, 6), hy += if_rr(&s->seed, -8, 6);
  IfEff *e = if_eff(s, EF_CUTS, hx, hy, flinch ? 22 : 14, flinch ? 1.1f : 0.65f);
  if (e) e->a = if_rr(&s->seed, -1.3f, 1.3f);
  if (D->hp <= dmg * IF_DMG * (D->who || s->act >= SC_END ? 1.0f : IF_DMG_G)) { /* последний разрез — нокаут как от удара */
    if_damage(s, A, D, dmg, HK_BLAST, 6, 7, hx, hy, 2);
    return;
  }
  if_hurt(s, D, dmg);
  D->ghostT = 30;
  if (D->flash < 0.4f) D->flash = 0.4f;
  for (int i = 0; i < (flinch ? 8 : 3); i++) {
    float a = if_rr(&s->seed, 0, 2 * FA_PI), v = if_rr(&s->seed, 1, 3.5f);
    IfPart *p = if_part(s, PT_DOT, hx, hy, cosf(a) * v, sinf(a) * v + 1, if_rr(&s->seed, 10, 20), if_rr(&s->seed, 0.6f, 1.2f),
                        FC(220, 20, 40));
    p->g = 0.18f;
  }
  if (flinch) {
    D->stT = 0;
    if_set_anim(D, AN_HURT_H, 1);
    if_shake(s, 4);
  }
}

/* граница куполов при столкновении (единицы мира) на высоте v (0 — пол, 1 — верх купола):
   clashK двигает её к Сукуне (+) или к Годжо (−), сверху волнами */
static float if_clash_x(const IfScene *s, float v) {
  float sgn = s->F[0].x < s->F[1].x ? 1.0f : -1.0f;
  float wob = (sinf(v * 9.0f + s->domT * 0.25f) * 6.0f + sinf(v * 3.1f - s->domT * 0.13f) * 9.0f) * fa_sat(1.3f - v);
  return s->domX + sgn * s->clashK * s->domRx * 0.8f + wob;
}

/* купола сошлись: граница ходит туда-сюда и трещит; со 100-го кадра Годжо сужает барьер и граница
   ползёт к Сукуне, на 160-м Святилище рушится — как в каноне, первое столкновение за Годжо */
static void if_clash_on(IfScene *s, float t0, float t, float dt) {
  if ((int)(t / 18) != (int)(t0 / 18)) { /* толчок: кто-то надавил */
    s->clashV += if_rr(&s->seed, -0.035f, 0.035f);
    if_shake(s, 4);
  }
  s->clashV -= s->clashK * 0.004f * dt; /* к середине тянет */
  if (t >= 100) {                       /* Годжо сужает барьер: Пустота давит сильнее */
    s->clashV += 0.0025f * dt;
    if (t0 < 100) if_call(s, &s->F[0], L"Сузить барьер", FC(150, 215, 255), 70);
  }
  s->clashV *= powf(0.94f, dt);
  s->clashK = fa_clampf(s->clashK + s->clashV * dt, -0.55f, 0.55f);
  for (int i = 0; i < 2; i++) { /* искры с границы */
    float v = if_rf(&s->seed) * 0.9f;
    IfPart *p = if_part(s, PT_SPARK, if_clash_x(s, v), s->domFl + v * s->domRy, if_rr(&s->seed, -2, 2), if_rr(&s->seed, -1, 2),
                        if_rr(&s->seed, 8, 16), if_rr(&s->seed, 0.7f, 1.3f), i ? FC(255, 80, 90) : FC(160, 220, 255));
    p->light = 1;
  }
  if (t >= 160) {
    s->clashWin = 0; /* канон: Пустота устояла */
    s->domPhase = DP_BREAK, s->domT = 0, s->clashV = 0;
    if_banner(s, L"КУПОЛ РАЗБИТ", s->clashWin ? L"СВЯТИЛИЩЕ ЗЛОБЫ" : L"БЕЗГРАНИЧНАЯ ПУСТОТА", s->clashWin ? 2 : 1, 150);
    IfEff *e = if_eff(s, EF_FLASH, if_clash_x(s, 0.5f), s->domFl + s->domRy * 0.5f, 22, 3.5f);
    if (e) e->c = FC(255, 255, 255);
    if_shake(s, 18);
    s->hitstop = 5;
  }
}

/* купол проигравшего рушится: граница уходит к его краю, с неё летят осколки; дальше — купол победителя */
static void if_clash_break(IfScene *s, float t, float dt) {
  float to = s->clashWin == 0 ? 1.3f : -1.3f;
  s->clashK += (to - s->clashK) * fa_sat(0.16f * dt);
  FCol col = s->clashWin ? FC(120, 170, 255) : FC(190, 40, 50); /* осколки цвета разбитого купола */
  for (int i = 0; i < 4; i++) {
    float v = if_rf(&s->seed) * 0.9f, sgn = s->F[0].x < s->F[1].x ? 1.0f : -1.0f, away = (s->clashWin ? -1.0f : 1.0f) * sgn;
    IfPart *q = if_part(s, PT_BIT, if_clash_x(s, v), s->domFl + v * s->domRy, away * if_rr(&s->seed, 1, 4), if_rr(&s->seed, -1, 3),
                        if_rr(&s->seed, 30, 50), if_rr(&s->seed, 1.5f, 3.5f), col);
    q->g = 0.2f, q->rot = if_rr(&s->seed, 0, 6);
  }
  if (t >= 22) {
    IfFighter *W = &s->F[s->clashWin], *L = &s->F[!s->clashWin];
    s->dom = s->clashWin ? DOM_SHRINE : DOM_VOID;
    s->domOwner = s->clashWin, s->domPhase = DP_ON, s->domT = 0, s->domOpen = 1, s->domLen = 210, s->domBurn = 600;
    if (s->clashWin) free(s->bd), s->bd = s->bd2;
    else free(s->bd2);
    s->bd2 = NULL, s->bdKind = s->dom;
    L->mv = -1, L->burn = 900, L->ce = 0, L->inv = 0;
    if_set_state(L, FS_IDLE);
    if_call(s, L, L"Техника выгорела", FC(170, 170, 180), 80);
    if (W->st == FS_MOVE && W->mv >= 0) W->mt = kMoves[W->mv].len; /* знак кончился: дальше как в своём куполе */
    W->inv = 0;
  }
}

static void if_dom_step(IfScene *s, float dt) {
  if (!s->dom) return;
  int clash = s->dom == DOM_CLASH;
  IfFighter *C = &s->F[s->domOwner], *V = &s->F[!s->domOwner];
  float t0 = s->domT;
  s->domT += dt;
  float t = s->domT;
  switch (s->domPhase) {
  case DP_CAST: /* знак рукой; фон строится понемногу, чтобы не было рывка */
    s->domOpen = 0;
    if (clash && t >= 12 && !(V->st == FS_MOVE && V->mv == MV_S_SHRINE)) { /* второй знак запоздал — силой: купола должны сойтись */
      V->y = V->fl, V->vy = 0, V->mv = -1;
      if_start(s, V, MV_S_SHRINE);
    }
    if (t >= 24) {
      s->domPhase = DP_OPEN, s->domT = 0;
      if (!clash && V->st != FS_FLY && V->st != FS_DOWN && V->st != FS_GETUP && V->st != FS_KO && V->hp > 0) {
        V->mv = -1, V->intent = IN_NONE, V->vx = 0;
        if (if_up(V) <= 0.5f) if_set_state(V, FS_IDLE); /* неизбежное попадание: что начал — бросил */
      }
      IfEff *e = if_eff(s, EF_FLASH, s->domX, s->domFl + 40, 20, clash ? 3.6f : 3.0f);
      if (e) e->c = clash ? FC(240, 220, 255) : (s->dom == DOM_VOID ? FC(160, 210, 255) : FC(255, 60, 70));
      if_shake(s, clash ? 14 : 10);
    }
    break;
  case DP_OPEN:
    s->domOpen = fa_smooth(t / 26);
    if (t >= 26) s->domPhase = DP_ON, s->domT = 0, s->domOpen = 1;
    break;
  case DP_ON:
    if (clash) {
      if_clash_on(s, t0, t, dt);
      break;
    }
    if (s->dom == DOM_SHRINE && V->hp > 0 && V->st != FS_FLY && V->st != FS_DOWN && V->st != FS_GETUP) {
      if ((int)(t / 6) != (int)(t0 / 6)) if_dom_cut(s, V, 5, 0);
      if ((int)(t / 50) != (int)(t0 / 50)) if_dom_cut(s, V, 16, 1);
    }
    if (s->act == SC_VOID2 && s->dom == DOM_VOID && t >= 50 && !s->maho && V->hp > 0 && s->phase == RP_FIGHT &&
        !(V->st == FS_MOVE && V->mv == MV_S_MAHO)) { /* Сукуна рвёт Пустоту тенью */
      V->y = V->fl, V->vy = 0, V->mv = -1;
      if_start(s, V, MV_S_MAHO);
    }
    if (t >= s->domLen || V->hp <= 0 || C->hp <= 0 || s->phase != RP_FIGHT) s->domPhase = DP_CLOSE, s->domT = 0;
    break;
  case DP_BREAK:
    if_clash_break(s, t, dt);
    break;
  case DP_CLOSE:
    s->domOpen = 1.0f - fa_sat(t / 20);
    if (t >= 20) {
      C->burn = s->domBurn; /* техника выгорела: несколько секунд без техник */
      C->ce = 0;
      s->dom = DOM_NONE;
      s->domOpen = 0;
      if (s->act == SC_CLASH) if_act(s, SC_BURN);
    }
    break;
  }
}

/* ===================== раунды ===================== */

static void if_fighter_reset(IfFighter *F) {
  F->hp = F->ghost = IF_HP;
  F->ce = 250;
  for (int i = 0; i < MV_COUNT; i++) F->cd[i] = 0, F->used[i] = 0;
  F->burn = 0, F->juggle = 0, F->chain = 0, F->mv = -1, F->stuck = 0, F->roll = 0, F->vroll = 0;
  F->blind = F->who == 0, F->power = 0, F->tellInf = 0, F->ultUsed = 0;
  F->rctN = F->rctTry = 0;
}

static void if_round_step(IfScene *s, float dt) {
  float t0 = s->phT;
  s->phT += dt;
  float t = s->phT;
  IfFighter *G = &s->F[0], *K = &s->F[1];
#define AT(E) (t0 < (E) && t >= (E))
  switch (s->phase) {
  case RP_INTRO:
    if (s->round == 1) { /* первый выход: Годжо — из телепорта, Сукуна — из тьмы */
      if (AT(16)) {
        G->alpha = 1;
        if_eff(s, EF_TELE, G->x, G->fl, 24, 1.4f);
      }
      if (AT(36)) {
        K->alpha = 1;
        IfEff *e = if_eff(s, EF_RING, K->x, K->fl + 46, 30, 1.5f);
        if (e) e->c = FC(230, 30, 50);
        for (int i = 0; i < 26; i++) {
          float a = if_rr(&s->seed, 0, 2 * FA_PI), v = if_rr(&s->seed, 0.5f, 2.6f);
          if_part(s, PT_SMOKE, K->x + cosf(a) * 8, K->fl + 40 + sinf(a) * 30, cosf(a) * v, sinf(a) * v * 0.5f + 0.4f,
                  if_rr(&s->seed, 40, 70), if_rr(&s->seed, 8, 16), FC(40, 14, 24));
        }
        if_shake(s, 5);
      }
    }
    if (AT(48)) {
      swprintf(s->banBuf, 48, L"РАУНД %d", s->round);
      if_banner(s, NULL, s->banBuf, 0, 66);
    }
    if (AT(118)) if_banner(s, NULL, L"БОЙ!", 3, 44);
    if (t >= 150) {
      s->phase = RP_FIGHT, s->phT = 0, s->roundT = 0, s->roamT = if_rr(&s->seed, 420, 720);
      if_act(s, SC_OPEN); /* сценарий заново; монетка: кто победит в этом раунде */
      s->fate = (int)(if_rnd(&s->seed) & 1);
      s->finish = 0, s->purpleMax = 0, s->maho = 0, s->mahoDie = 0, s->adapt = 0, s->adaptT = 0, s->clashK = 0;
      s->wheel = s->wheelTo = 0, s->shadowT = s->tagT = s->flankT = 0;
      s->mahoY = s->mahoVy = 0, s->mahoSt = MS_IDLE;
      G->think = if_rr(&s->seed, 4, 20), K->think = if_rr(&s->seed, 4, 20);
    }
    break;
  case RP_FIGHT:
    s->roundT += dt;
    s->roamT -= dt;
    if (s->nMon > 1 && (int)t / 30 != (int)t0 / 30) { /* оба на другом мониторе — шкалы и надписи туда */
      int m = if_mon_at(s, G->x);
      if (m != s->arena && m == if_mon_at(s, K->x)) {
        float hx = s->homeX;
        if_set_arena(s, m);
        s->homeX = hx; /* дом раунда прежний: в нём не стоят */
      }
    }
    if (G->hp <= 0 || K->hp <= 0) {
      s->phase = RP_KO, s->phT = 0;
      s->winner = G->hp <= 0 && K->hp <= 0 ? -1 : (G->hp <= 0 ? 1 : 0);
      s->slowT = 70;
      s->statKO++;
      if_banner(s, NULL, L"НОКАУТ!", 3, 96);
      G->intent = K->intent = IN_NONE;
    }
    break;
  case RP_KO: {
    int ready = t > 100;
    for (int i = 0; i < 2; i++) {
      IfFighter *F = &s->F[i];
      if (F->hp <= 0 && F->st != FS_KO) ready = 0;
      if (F->hp > 0 && F->st != FS_IDLE && F->st != FS_WALK && F->st != FS_RUN) ready = 0;
    }
    if (ready || t > 360) {
      s->phase = RP_WIN, s->phT = 0;
      if (s->winner >= 0) {
        IfFighter *W = &s->F[s->winner];
        if (s->wins) s->wins[s->winner]++;
        W->mv = -1, W->vx = 0, W->intent = IN_NONE;
        if (if_up(W) <= 0.5f) {
          if_set_state(W, FS_WIN);
          if_set_anim(W, W->who ? AN_S_WIN : AN_G_WIN, 1);
        }
        if (W->who == 0) W->blind = 0;
        int alt = if_rf(&s->seed) < 0.5f; /* слова победителя — из боя в Синдзюку или прежние */
        if_banner(s, W->who ? (alt ? L"Можешь гордиться. Ты был силён." : L"Знай своё место, глупец.")
                            : (alt ? L"Не, я бы победил." : L"Не переживай. Я ведь сильнейший."),
                  W->who ? L"ПОБЕДИЛ СУКУНА" : L"ПОБЕДИЛ ГОДЖО", 4, 210);
      } else {
        if_banner(s, NULL, L"НИЧЬЯ", 0, 150);
      }
    }
    break;
  }
  case RP_WIN:
    if (t >= 170) s->phase = RP_RESET, s->phT = 0;
    break;
  case RP_RESET: /* обратная проклятая техника: оба лечатся и возвращаются */
    if (AT(1)) {
      if (s->maho) if_maho_die(s);
      if (s->nMon > 1) { /* следующий раунд — на другом мониторе */
        int m = (s->arena + 1 + (int)(if_rf(&s->seed) * (s->nMon - 1))) % s->nMon;
        if_set_arena(s, m);
      }
      float mid = s->homeX;
      for (int i = 0; i < 2; i++) {
        IfFighter *F = &s->F[i];
        if (F->st == FS_KO) {
          F->vx = 0, F->vy = 0, F->y = F->fl;
          if_set_state(F, FS_GETUP);
          if_set_anim(F, AN_GETUP, 1);
          F->inv = 60;
        } else if (F->st == FS_WIN) {
          if_set_state(F, FS_IDLE);
        }
        F->home = mid + (i ? 85.0f : -85.0f);
        F->homeFl = if_ground(s, F->home);
        F->intent = IN_HOME;
        if_call(s, F, L"Обратная техника", FC(150, 255, 190), 90);
        IfEff *e = if_eff(s, EF_HEAL, F->x, F->fl, 110, 1);
        if (e) e->owner = F->who;
      }
    }
    for (int i = 0; i < 2; i++) {
      IfFighter *F = &s->F[i];
      F->hp += IF_HP / 80.0f * dt;
      if (F->hp > IF_HP) F->hp = IF_HP;
      F->ghost = F->hp;
      if (F->st == FS_IDLE && F->intent == IN_NONE && (fabsf(F->x - F->home) > 4 || fabsf(F->fl - F->homeFl) > 12))
        F->intent = IN_HOME;
    }
    if (AT(70)) {
      G->blind = 1;
      K->power = 0;
    }
    if ((t >= 100 && G->intent != IN_HOME && K->intent != IN_HOME && if_neutral(G) && if_neutral(K)) || t >= 480) {
      s->round++;
      s->phase = RP_INTRO;
      s->phT = 40; /* выходить второй раз не нужно: сразу «Раунд» */
      if_fighter_reset(G);
      if_fighter_reset(K);
      G->intent = K->intent = IN_NONE;
    }
    break;
  }
#undef AT
}

/* ===================== сценарий ===================== */

/* оба на ногах, на одном полу, не дальше dmax, купола нет: можно начинать следующий акт */
static int if_near(const IfScene *s, float dmax) {
  const IfFighter *G = &s->F[0], *K = &s->F[1];
  return !s->dom && if_neutral(G) && if_neutral(K) && if_up(G) < 1 && if_up(K) < 1 && fabsf(G->fl - K->fl) < 4 &&
         fabsf(G->x - K->x) < dmax && G->hp > 0 && K->hp > 0;
}

static int if_pj_any(const IfScene *s, int kind) {
  for (int i = 0; i < IF_MAXJ; i++)
    if (s->pj[i].kind == kind) return 1;
  return 0;
}

/* долго не сходятся (разные окна, мониторы): обоим — домой, на пол арены */
static void if_regroup(IfScene *s) {
  for (int i = 0; i < 2; i++) {
    IfFighter *F = &s->F[i];
    if (!if_neutral(F)) continue;
    float side = F->x < s->F[!i].x ? -1.0f : (F->x > s->F[!i].x ? 1.0f : (i ? 1.0f : -1.0f)); /* каждый к своему краю: */
    F->home = s->homeX + side * 60.0f; /* навстречу друг другу пошли бы — упёрлись бы друг в друга навсегда */
    F->homeFl = if_ground(s, F->home);
    F->intent = IN_HOME;
  }
}

/* удар Махораги дошёл? отрезок руки (локоть → кулак) против головы, груди и таза Годжо; дальше if_damage, как
   у бойцов: Бесконечность может остановить (и научить колесо), блок, Чёрная вспышка */
static void if_maho_strike(IfScene *s, float ax, float ay, float bx, float by, float r, float dmg, int kind, float kx,
                           float ky) {
  IfFighter *G = &s->F[0], *K = &s->F[1];
  if (s->mahoHit || G->hp <= 0 || G->st == FS_KO || G->st == FS_TELE || G->alpha < 0.5f) return;
  static const int kHurt[3] = {FJ_HEAD, FJ_CHEST, FJ_HIP};
  static const float kR[3] = {8.5f, 11.0f, 10.0f};
  for (int i = 0; i < 3; i++) {
    float hx, hy, qx, qy, rr = r + kR[i];
    if_joint(G, kHurt[i], &hx, &hy);
    if (if_seg(ax, ay, bx, by, hx, hy, &qx, &qy) > rr * rr) continue;
    s->mahoHit = 1, s->mahoSwing = 1;
    if_damage(s, K, G, dmg * 0.75f, kind, kx, ky, qx + (hx - qx) * 0.4f, qy + (hy - qy) * 0.4f, 1);
    s->mahoSwing = 0;
    return;
  }
}

/* приземлился после прыжка: трещина, пыль, волна по земле сбивает Годжо с ног */
static void if_maho_land(IfScene *s) {
  IfFighter *G = &s->F[0], *K = &s->F[1];
  float gy = s->mahoFl;
  if_eff(s, EF_CRACK, s->mahoX, gy, 70, 1.6f);
  if_land_dust(s, s->mahoX - 30, gy, 10), if_land_dust(s, s->mahoX + 30, gy, 10);
  if_shake(s, 16);
  s->hitstop = 3;
  if (!s->mahoHit && G->hp > 0 && G->st != FS_KO && G->st != FS_TELE && if_up(G) < 16 && fabsf(G->x - s->mahoX) < 100 &&
      G->alpha > 0.5f) {
    float hx, hy;
    if_joint(G, FJ_HIP, &hx, &hy);
    s->mahoHit = 1, s->mahoSwing = 1;
    if_damage(s, K, G, 75, HK_LAUNCH, 3.0f, 8.0f, hx, hy, 1);
    s->mahoSwing = 0;
  }
}

static void if_maho_pose(const IfScene *s, IfMahoPose *P); /* ниже, у рисования */

/* Махорага: сам идёт на Годжо и бьёт — прямой, сверху, размах, прыжок с ударом о землю, апперкот, таран плечом,
   Меч изгнания; закрывается наручами от удара, который видит; от ударов Годжо вздрагивает и отлетает, Синий тянет;
   сквозь его тело не пройти */
static void if_maho_step(IfScene *s, float dt) {
  IfFighter *G = &s->F[0];
  if (!s->maho || s->mahoDie > 0) return;
  s->mahoFl = if_ground(s, s->mahoX);
  if (s->mahoFlash > 0) s->mahoFlash -= 0.15f * dt;
  if (s->mahoBladeCd > 0) s->mahoBladeCd -= dt;
  s->mahoCalm += dt; /* раны затягиваются, если его не бьют (как в каноне) */
  if (s->mahoCalm > 90 && s->mahoHp < IF_MAHOHP) s->mahoHp = fminf(IF_MAHOHP, s->mahoHp + 0.25f * dt);
  s->mahoLean *= powf(0.92f, dt);
  float dG = G->x - s->mahoX, adG = fabsf(dG);
  int gOk = G->hp > 0 && G->st != FS_KO && G->st != FS_DOWN && G->st != FS_GETUP && G->alpha > 0.5f;
  int can = gOk && s->phase == RP_FIGHT && s->act != SC_WORLD && s->act != SC_END && !(s->act == SC_PURPLE && s->actT > 400) &&
            s->mahoT > 44;
  if (s->mahoY > 0 || s->mahoVy > 0) { /* в воздухе: время приёма стоит, пока не приземлится */
    s->mahoY += s->mahoVy * dt, s->mahoVy -= 0.36f * dt, s->mahoX += s->mahoVx * dt;
    if (s->mahoY <= 0 && s->mahoVy <= 0) {
      s->mahoY = s->mahoVy = s->mahoVx = 0;
      if (s->mahoSt == MS_ATK && s->mahoMv == MM_STOMP) s->mahoMt = 40, if_maho_land(s);
      else s->mahoSt = MS_IDLE, s->mahoMt = 0;
    }
  } else {
    s->mahoMt += dt;
    switch (s->mahoSt) {
    case MS_RISE:
      if (s->mahoT >= 40) s->mahoSt = MS_IDLE, s->mahoMt = 0;
      break;
    case MS_HIT:
    case MS_STAGGER:
    case MS_GUARD: {
      int stg = s->mahoSt == MS_STAGGER, grd = s->mahoSt == MS_GUARD;
      s->mahoX += s->mahoVx * dt;
      s->mahoVx *= powf(stg ? 0.9f : 0.85f, dt);
      if (stg && fabsf(s->mahoVx) > 0.6f && (s->frame & 3) == 0)
        if_part(s, PT_DUST, s->mahoX - s->mahoVx * 10, s->mahoFl + 1, -s->mahoVx * 0.5f, if_rr(&s->seed, 0.3f, 1.2f), 24, 3.5f,
                FC(205, 200, 194));
      if (s->mahoMt >= (stg ? 26 : (grd ? 30 : 12))) {
        s->mahoSt = MS_IDLE, s->mahoMt = 0, s->mahoAtk = fmaxf(s->mahoAtk, stg ? 30 : 16);
        if (grd && can && adG < 90) s->mahoAtk = 0; /* из блока — сразу в ответ */
      }
      break;
    }
    case MS_ATK: {
      int mv = s->mahoMv;
      float t = s->mahoMt, t0 = t - dt, f = s->mahoF, bx = s->mahoX, by = s->mahoFl;
      IfMahoPose P;
      if (mv == MM_STOMP) {
        if (t0 < 12 && t >= 12) { /* прыжок: долететь до Годжо */
          float tx = fa_clampf(G->x + G->vx * 8, s->xMin + 30, s->xMax - 30), T = 2 * 8.0f / 0.36f;
          s->mahoVy = 8.0f, s->mahoY = 0.01f, s->mahoVx = fa_clampf((tx - s->mahoX) / T, -5.5f, 5.5f);
          if (fabsf(tx - s->mahoX) > 2) s->mahoF = tx > s->mahoX ? 1 : -1;
          if_land_dust(s, s->mahoX, s->mahoFl, 12);
          if_shake(s, 5);
        } else if (t >= kMahoLen[mv]) {
          s->mahoSt = MS_IDLE, s->mahoMt = 0, s->mahoAtk = if_rr(&s->seed, 40, 80);
        }
      } else if (mv == MM_CHARGE) { /* таран: пригнулся — и бегом, плечом вперёд, сквозь всё на пути */
        s->mahoVx = t >= 10 && t < 34 ? f * 5.2f : s->mahoVx * powf(0.8f, dt);
        float x0 = s->mahoX;
        s->mahoX = fa_clampf(s->mahoX + s->mahoVx * dt, s->xMin + 30, s->xMax - 30);
        if (t >= 10 && t < 34) {
          if (fabsf(s->mahoX - x0) < 0.5f) s->mahoMt = 34; /* упёрся в край */
          float w0 = s->mahoWalk;
          s->mahoWalk += 0.2f * dt;
          if (cosf(w0) * cosf(s->mahoWalk) < 0) if_land_dust(s, s->mahoX - f * 6, s->mahoFl, 6), if_shake(s, 4);
          if_maho_pose(s, &P);
          float cx = bx + P.chest[0] * f * IF_MU, cy = by + P.chest[1] * IF_MU;
          if_maho_strike(s, cx, cy - 10 * IF_MU, cx + f * 24 * IF_MU, cy, 22, 85, HK_KNOCK, 9.5f, 5.5f);
        }
        if (t >= kMahoLen[mv]) s->mahoSt = MS_IDLE, s->mahoMt = 0, s->mahoAtk = if_rr(&s->seed, 40, 70), s->mahoVx = 0;
      } else {
        float ha = kMahoHitAt[mv];
        int span = mv == MM_SWEEP || mv == MM_BLADE || mv == MM_UPPER; /* размах, меч и апперкот задевают не один кадр */
        if ((t0 < ha && t >= ha) || (span && t > ha && t <= ha + 6 && !s->mahoHit)) { /* удар доходит */
          if_maho_pose(s, &P);
          float ex = bx + P.el[1][0] * f * IF_MU, ey = by + P.el[1][1] * IF_MU;
          float hx = bx + P.hd[1][0] * f * IF_MU, hy = by + P.hd[1][1] * IF_MU;
          if (mv == MM_PUNCH) {
            if_maho_strike(s, ex, ey, hx, hy, 15, 70, HK_HEAVY, 6.0f, 2.0f);
          } else if (mv == MM_SWEEP) {
            if_maho_strike(s, ex, ey, hx + f * 10, hy, 16, 80, HK_KNOCK, 8.0f, 5.0f);
          } else if (mv == MM_UPPER) { /* снизу вверх: Годжо взлетает — Сукуне */
            if_maho_strike(s, ex, ey, hx, hy + 8 * IF_MU, 16, 70, HK_LAUNCH, 2.2f, 10.5f);
          } else if (mv == MM_BLADE) { /* Меч изгнания: лезвие от кулака по линии предплечья */
            float ux = hx - ex, uy = hy - ey, l = sqrtf(ux * ux + uy * uy);
            if (l > 0.01f) ux /= l, uy /= l;
            if_maho_strike(s, hx, hy, hx + ux * 44 * IF_MU, hy + uy * 44 * IF_MU, 9, 110, HK_KNOCK, 8.0f, 6.0f);
          } else { /* сверху: кулаки о землю — трещина, пыль */
            if_maho_strike(s, ex, ey, hx, hy, 18, 90, HK_KNOCK, 7.0f, 5.5f);
            if_eff(s, EF_CRACK, hx, by, 70, 1.4f);
            if_land_dust(s, hx, by, 14);
            if_shake(s, 12);
          }
        }
        if (mv == MM_BLADE && t0 < 4 && t >= 4) if_call_maho(s, L"Меч изгнания", FC(255, 236, 180), 70);
        if (t >= kMahoLen[mv]) s->mahoSt = MS_IDLE, s->mahoMt = 0, s->mahoAtk = if_rr(&s->seed, 34, 70);
      }
      break;
    }
    default: { /* стоит или идёт */
      s->mahoAtk -= dt;
      if (adG > 2 && (s->mahoSt == MS_IDLE || adG > 40)) s->mahoF = dG > 0 ? 1 : -1;
      int mv = -1;
      if (gOk && G->st == FS_MOVE && G->mv >= 0 && s->mahoSeenG != G->mvId && if_maho_first(s, G) && adG < 120) {
        const FMove *m = &kMoves[G->mv]; /* видит, что Годжо бьёт: иногда закрывается наручами */
        float left = m->hitA - G->mt;
        int see = ((m->flags & MVF_MELEE) && left > 0 && left <= 8) || ((G->mv == MV_G_RED || G->mv == MV_G_REDX) && G->mt >= 22);
        if (see) {
          s->mahoSeenG = G->mvId;
          if (if_rf(&s->seed) < 0.3f) {
            s->mahoSt = MS_GUARD, s->mahoMt = 0, s->mahoVx = 0, s->mahoF = dG > 0 ? 1 : -1;
            break;
          }
        }
      }
      if (can && s->mahoAtk <= 0) {
        float r = if_rf(&s->seed);
        int blade = s->mahoBladeCd <= 0;
        if (s->mahoFirst) { /* встал — и сразу в бой: таран издали, сверху вблизи */
          mv = adG < 100 ? MM_SLAM : MM_CHARGE;
          s->mahoFirst = 0;
        } else if (adG < 70) {
          mv = r < 0.22f ? MM_PUNCH : (r < 0.40f ? MM_SWEEP : (r < 0.54f ? MM_SLAM : (r < 0.78f ? MM_UPPER : (blade ? MM_BLADE : MM_PUNCH))));
        } else if (adG < 120) {
          mv = r < 0.28f ? MM_SLAM : (r < 0.48f ? MM_STOMP : (r < 0.72f ? (blade ? MM_BLADE : MM_CHARGE) : (r < 0.9f ? MM_CHARGE : -1)));
        } else if (adG < 260) {
          mv = r < 0.45f ? MM_CHARGE : (r < 0.8f ? MM_STOMP : -1);
        }
        if (mv < 0) s->mahoAtk = if_rr(&s->seed, 15, 35);
      }
      if (mv >= 0) {
        s->mahoSt = MS_ATK, s->mahoMv = mv, s->mahoMt = 0, s->mahoHit = 0, s->mahoVx = 0, s->mahoId++;
        s->mahoTx = fa_clampf(G->x + G->vx * (mv == MM_SLAM ? 8 : 4), s->xMin, s->xMax);
        if (mv == MM_BLADE) s->mahoBladeCd = if_rr(&s->seed, 360, 520);
        if (adG > 2) s->mahoF = dG > 0 ? 1 : -1;
      } else if (can && adG > 56) { /* идёт к Годжо, тяжело ступая */
        if (s->mahoSt != MS_WALK) s->mahoSt = MS_WALK, s->mahoMt = 0;
        float w0 = s->mahoWalk;
        s->mahoX += s->mahoF * 1.5f * dt;
        s->mahoWalk += 0.085f * dt;
        if (cosf(w0) * cosf(s->mahoWalk) < 0) { /* нога встала */
          if_land_dust(s, s->mahoX + s->mahoF * 14, s->mahoFl, 5);
          if_shake(s, 2.5f);
        }
      } else if (s->mahoSt != MS_IDLE) {
        s->mahoSt = MS_IDLE, s->mahoMt = 0;
      }
      break;
    }
    }
  }
  s->mahoX = fa_clampf(s->mahoX, s->xMin + 30, s->xMax - 30);
  if (s->mahoSt == MS_RISE) return;
  for (int i = 0; i < 2; i++) { /* сквозь великана не пройти: кто на земле — тот в стороне */
    IfFighter *F = &s->F[i];
    if (if_up(F) > 24 || F->st == FS_FLY || F->st == FS_KO || F->st == FS_DOWN || F->st == FS_TELE || F->alpha < 0.5f ||
        s->mahoY > 20)
      continue;
    float dx = F->x - s->mahoX;
    if (fabsf(dx) >= 24) continue;
    float sg = dx > 0.01f ? 1.0f : (dx < -0.01f ? -1.0f : -s->mahoF);
    F->x = fa_clampf(s->mahoX + sg * 24, s->xMin, s->xMax);
  }
  if (G->intent == IN_NEAR && if_maho_first(s, G) && adG < 40) G->intent = IN_NONE, G->think = 0; /* дошёл до великана */
}

/* ход боя по сценарию (см. SC_*): кто что делает дальше, когда оба готовы */
static void if_script_step(IfScene *s, float dt) {
  if (s->phase != RP_FIGHT) {
    if (s->act == SC_END) s->actT += dt; /* развязка: время идёт и после нокаута (снимки) */
    return;
  }
  IfFighter *G = &s->F[0], *K = &s->F[1];
  float d = fabsf(G->x - K->x);
  s->actT += dt;
  float t = s->actT;
  int wait = 0; /* акт ждёт, пока сойдутся */
  switch (s->act) {
  case SC_OPEN: /* разминка: рукопашная у Бесконечности, Синий и Красный, Рассечение; через 10 секунд — к знакам */
    if (t > 600) {
      wait = 1;
      if (if_near(s, 230) && d > 56) if_act(s, SC_CLASH);
    }
    break;
  case SC_CLASH: /* знаки разом: Сукуна начинает на 12 кадров позже, и купола раскрываются в один миг */
    if (s->step == 0) {
      wait = 1;
      if (if_near(s, 230) && d > 56) {
        if_start(s, G, MV_G_VOID);
        s->step = 1;
      }
    } else if (s->step == 1) {
      if (G->st == FS_MOVE && G->mv == MV_G_VOID) {
        if (G->mt >= 12 && K->st != FS_FLY && K->st != FS_DOWN && K->st != FS_GETUP && K->hp > 0) {
          K->y = K->fl, K->vy = 0, K->mv = -1;
          if_start(s, K, MV_S_SHRINE);
          s->step = 2;
        }
      } else if (!s->dom) {
        s->step = 0; /* Годжо сбили до знака — заново */
      }
    }
    break; /* дальше ведёт if_dom_step: столкновение, разбитый купол, купол победителя, выгорание → SC_BURN */
  case SC_BURN: /* техники выгорели у обоих: чистая рукопашная; Годжо чинит мозг обратной техникой */
    if (t > 240 && if_neutral(G) && if_up(G) < 1 && G->hp > 0) if_start(s, G, MV_G_RCT); /* → SC_VOID2 в конце приёма */
    break;
  case SC_VOID2: /* Пустота — один: Сукуна, без техники, застывает; из тени встаёт Махорага и рвёт купол */
    if (s->step == 0) {
      wait = 1;
      if (if_near(s, 240) && G->burn <= 0) {
        G->ce = fmaxf(G->ce, 700);
        if_start(s, G, MV_G_VOID);
        s->step = 1;
      }
    } else if (!s->dom && !(G->st == FS_MOVE && G->mv == MV_G_VOID) && !s->maho) {
      s->step = 0; /* сорвалось — ещё раз */
    }
    break;
  case SC_MAHO: /* великан с Сукуной вдвоём против Годжо, колесо учится; как приспособился (или надоело ждать) — Фиолетовый */
    if (!s->maho && s->mahoDie <= 0) { /* Годжо разбил тень руками — Фиолетовый уже на одного Сукуну */
      if_act(s, SC_PURPLE);
      G->ce = IF_CE, s->purpleMax = 1;
    } else if (t > 900 && G->burn <= 0 && (s->adapt >= IF_ADAPT || t > 1700)) {
      if_act(s, SC_PURPLE);
      G->ce = IF_CE, s->purpleMax = 1;
    }
    break;
  case SC_PURPLE: /* Годжо отходит и собирает Фиолетовый на всю мощь: сносит Махорагу и Сукуну */
    if (s->step == 0) {
      wait = 1;
      if (if_neutral(G) && if_up(G) < 1 && G->burn <= 0 && G->hp > 0) {
        if (d < 80 && t < 400 && G->intent != IN_BACK) G->intent = IN_BACK, G->intentT = 26;
        else if (d >= 80 || t >= 400 || G->intent != IN_BACK) {
          G->ce = fmaxf(G->ce, 600);
          if_start(s, G, MV_G_PURPLE);
          s->step = 1;
        }
      }
    } else if (s->step == 1) {
      if (!(G->st == FS_MOVE && G->mv == MV_G_PURPLE) && !if_pj_any(s, PJ_PURPLE)) s->step = 2;
    } else if (K->st != FS_FLY && (!s->maho || t > 900)) {
      if (s->maho) if_maho_die(s); /* тень не попала под удар — всё равно рассеялась: хозяин сам доиграет */
      if_act(s, SC_WORLD);
    }
    break;
  case SC_WORLD: /* Сукуна поднимается — и режет мир. Монетка решает, попал ли */
    if (s->step == 0) {
      wait = 1;
      if (if_near(s, 260) && d > 40) {
        K->burn = 0, K->ce = IF_CE;
        if_start(s, K, MV_S_WORLD);
        s->step = 1;
      }
    }
    break;
  case SC_END: /* исход решён; на всякий случай — если добивание сорвалось, довести */
    if (t > 600 && G->hp > 0 && K->hp > 0 && !s->finish) {
      IfFighter *A = &s->F[s->fate], *D = &s->F[!s->fate];
      float hx, hy;
      if_joint(D, FJ_CHEST, &hx, &hy);
      s->finish = 1;
      if_damage(s, A, D, 1, HK_BLAST, 8, 7, hx, hy, 2);
    }
    break;
  }
  if (wait) {
    s->waitT += dt;
    if (s->waitT > 480) if_regroup(s), s->waitT = -300;
  } else {
    s->waitT = 0;
  }
  if_maho_step(s, dt);
}

/* ===================== время ===================== */

static void if_age(IfScene *s, float dt) {
  for (int i = 0; i < 2; i++) /* белеет от удара на миг — и в «стоп-кадре» тоже гаснет */
    if (s->F[i].flash > 0) s->F[i].flash -= 0.17f * dt;
  for (int i = 0; i < IF_MAXP; i++) {
    IfPart *p = &s->pt[i];
    if (p->t >= p->life) continue;
    p->t += dt;
    p->x += p->vx * dt, p->y += p->vy * dt;
    p->vy -= p->g * dt;
    switch (p->kind) {
    case PT_SMOKE:
    case PT_DUST:
      p->vx *= powf(0.96f, dt), p->vy *= powf(0.97f, dt);
      break;
    case PT_FIRE:
      p->vx *= powf(0.93f, dt), p->vy *= powf(0.95f, dt);
      break;
    case PT_SHARD:
    case PT_EMBER:
    case PT_DOT:
      float g = if_ground(s, p->x);
      if (p->y < g) {
        p->y = g, p->vy = -p->vy * 0.3f, p->vx *= 0.6f;
        if (p->kind == PT_DOT) p->t = p->life; /* капля о землю — исчезла */
      }
      p->rot += 0.2f * dt;
      break;
    case PT_BIT:
      p->rot += 0.3f * dt;
      break;
    default:
      p->vx *= powf(0.9f, dt), p->vy *= powf(0.9f, dt);
      break;
    }
  }
  for (int i = 0; i < IF_MAXE; i++) {
    IfEff *e = &s->ef[i];
    if (e->kind == EF_NONE) continue;
    e->t += dt;
    if (e->t >= e->life) e->kind = EF_NONE;
  }
  if (s->ban1 || s->ban2) {
    s->banT += dt;
    if (s->banT >= s->banLife) s->ban1 = s->ban2 = s->ban3 = NULL;
  }
  if (s->maho) s->mahoT += dt;
  if (s->mahoDie > 0) s->mahoDie -= dt;
  if (s->adaptT > 0) s->adaptT -= dt;
  if (s->shadowT > 0) s->shadowT -= dt;
  if (s->tagT > 0) s->tagT -= dt;
  if (s->flankT > 0) s->flankT -= dt;
  s->wheel += (s->wheelTo - s->wheel) * fa_sat(0.12f * dt);
  for (int i = 0; i < 2; i++) {
    s->hud[i][0] += (s->F[i].hp - s->hud[i][0]) * fa_sat(0.3f * dt);
    s->hud[i][1] += (s->F[i].ce - s->hud[i][1]) * fa_sat(0.2f * dt);
  }
}

/* один шаг мира: 1/60 секунды */
static void if_step(IfScene *s) {
  s->frame++;
  float dt = 1.0f;
  if (s->slowT > 0) s->slowT -= 1, dt = 0.3f; /* нокаут — замедленно */
  s->dt = dt;
  if (s->shake > 0.4f) {
    s->shx = if_rr(&s->seed, -1, 1) * s->shake;
    s->shy = if_rr(&s->seed, -1, 1) * s->shake * 0.6f;
    s->shake *= 0.85f;
  } else {
    s->shake = s->shx = s->shy = 0;
  }
  if (s->hitstop > 0) { /* удар дошёл: бойцы замерли на миг, искры летят */
    s->hitstop -= 1;
    if_age(s, dt);
    return;
  }
  int a = (int)(s->frame & 1); /* по очереди, чтобы никто не ходил всегда первым */
  if_fighter_step(s, &s->F[a], dt);
  if_fighter_step(s, &s->F[!a], dt);
  if_push(s);
  if_pj_step(s, dt);
  if_dom_step(s, dt);
  if_round_step(s, dt);
  if_script_step(s, dt);
  if_age(s, dt);
}

/* площадка в список; px холста → единицы */
static void if_plat_add(IfScene *s, float x0, float x1, float topPx, float botPx, int solid, int wl, int wr) {
  if (s->nPl >= IF_MAXPL || x1 - x0 < 1) return;
  IfPlat *p = &s->pl[s->nPl++];
  p->x0 = x0 / s->U, p->x1 = x1 / s->U;
  p->top = (s->gy - topPx) / s->U, p->bot = (s->gy - botPx) / s->U;
  p->solid = solid, p->wl = wl, p->wr = wr;
}

static int if_cmp_px(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return x < y ? -1 : x > y;
}

/* полы мониторов: под каждым x — низ самого нижнего монитора над ним (дыры между
   мониторами — пол соседа); монитор над другим — площадка, с неё спрыгивают */
static void if_stage_floors(IfScene *s, const IfStage *st) {
  s->nPl = 0;
  if (!st || st->nMon <= 0) {
    if_plat_add(s, -1e6f, 1e6f, s->gy, s->gy, 1, 0, 0);
    s->nFix = s->nPl;
    return;
  }
  float xs[IF_MAXMON * 2];
  int nx = 0;
  for (int i = 0; i < st->nMon; i++) xs[nx++] = (float)st->mon[i].left, xs[nx++] = (float)st->mon[i].right;
  qsort(xs, nx, sizeof(float), if_cmp_px);
  float runX = -1e6f, runB = -1;
  for (int i = 0; i + 1 < nx; i++) {
    float a = xs[i], b = xs[i + 1], m = (a + b) * 0.5f, best = -1;
    if (b - a < 1) continue;
    for (int k = 0; k < st->nMon; k++)
      if (m >= st->mon[k].left && m < st->mon[k].right && st->mon[k].bottom - 1 > best) best = (float)(st->mon[k].bottom - 1);
    if (best < 0) best = runB >= 0 ? runB : s->gy; /* дыра: пол соседа слева */
    if (runB >= 0 && best != runB) if_plat_add(s, runX, a, runB, runB, 1, 0, 0), runX = a;
    runB = best;
  }
  if (runB >= 0) if_plat_add(s, runX, 1e6f, runB, runB, 1, 0, 0);
  else if_plat_add(s, -1e6f, 1e6f, s->gy, s->gy, 1, 0, 0);
  s->nFix = s->nPl; /* if_ground смотрит только на них */
  for (int k = 0; k < st->nMon; k++) {
    float b = (float)(st->mon[k].bottom - 1), top = (s->gy - b) / s->U, cx = (st->mon[k].left + st->mon[k].right) * 0.5f / s->U;
    if (top > if_ground(s, cx) + 1) if_plat_add(s, (float)st->mon[k].left, (float)st->mon[k].right, b, b, 0, 0, 0);
  }
  s->nFix = s->nPl;
}

/* окна: видимая часть верхнего края — площадка. Окно, чей верх у самого верха
   монитора (развёрнутое) или у самого низа, — нет: там не встать */
static void if_stage_windows(IfScene *s, const IfStage *st) {
  s->nPl = s->nFix;
  if (!st) return;
  float figH = s->U * 100;
  for (int i = 0; i < st->nWin && s->nPl < IF_MAXPL; i++) {
    const RECT *r = &st->win[i];
    float seg[16][2];
    int ns = 1;
    seg[0][0] = (float)r->left, seg[0][1] = (float)r->right;
    for (int j = 0; j < i && ns > 0; j++) { /* что выше по Z и закрывает край — вычесть */
      const RECT *q = &st->win[j];
      if (q->top > r->top || q->bottom <= r->top) continue;
      int nn = 0;
      float out[16][2];
      for (int k = 0; k < ns; k++) {
        float a = seg[k][0], b = seg[k][1];
        if (q->right <= a || q->left >= b) {
          if (nn < 16) out[nn][0] = a, out[nn][1] = b, nn++;
          continue;
        }
        if (q->left > a && nn < 16) out[nn][0] = a, out[nn][1] = (float)q->left, nn++;
        if (q->right < b && nn < 16) out[nn][0] = (float)q->right, out[nn][1] = b, nn++;
      }
      memcpy(seg, out, sizeof(out[0]) * nn);
      ns = nn;
    }
    for (int k = 0; k < ns; k++)
      for (int m = 0; m < st->nMon; m++) { /* по мониторам: свой верх и низ у каждого */
        const RECT *M = &st->mon[m];
        if (r->top < M->top + figH * 1.15f || r->top >= M->bottom) continue;
        float a = fmaxf(seg[k][0], (float)M->left), b = fminf(seg[k][1], (float)M->right);
        if (b - a < 40 * s->U) continue;
        float top = (s->gy - r->top) / s->U;
        if (top - if_ground(s, (a + b) * 0.5f / s->U) < 30) continue; /* у самой земли — просто ступенька */
        if_plat_add(s, a, b, (float)r->top, fminf((float)r->bottom, (float)M->bottom - 1), 0, a == (float)r->left,
                    b == (float)r->right);
      }
  }
}

/* сцена на холсте W×H; st — мониторы и окна в px холста (NULL — холст и есть
   экран, земля — нижний край) */
/* рост бойцов на экране, px: по главному монитору (его ширина и высота в px экрана).
   g_idleSize (Настройки, «Вид»): 0 — мелкие, 1 — средние (десятая часть высоты), 2 — крупные */
static float if_fig_h(int pw, int ph) {
  static const float kFrac[3] = {0.075f, 0.10f, 0.14f};
  float f = kFrac[g_idleSize < 0 ? 0 : (g_idleSize > 2 ? 2 : g_idleSize)];
  return fa_clampf(fminf(ph * f, pw * f * 0.9f), 56.0f, 300.0f);
}

/* размер пикселя пиксель-арта: боец — около 50–70 точек ростом, как спрайт ретро-файтинга */
static int if_pick_k(int pw, int ph) {
  float h = if_fig_h(pw, ph);
  return h < 70 ? 1 : (h < 170 ? 2 : 3);
}

static void if_scene_init(IfScene *s, int W, int H, unsigned seed, int *wins, const IfStage *st) {
  RECT pm = {0, 0, W, H}; /* главный монитор: по нему рост бойцов, там шкалы */
  if (st && st->nMon > 0) pm = st->mon[st->prim >= 0 && st->prim < st->nMon ? st->prim : 0];
  int pw = pm.right - pm.left, ph = pm.bottom - pm.top, k = s->k > 0 ? s->k : 1;
  float figH = if_fig_h(pw * k, ph * k) / k;
  s->W = W, s->H = H;
  s->U = figH / 100.0f;
  s->hs = fmaxf(figH * k, ph * k * 0.2f); /* надписи и шкалы — по экрану, не по мелким бойцам */
  s->gy = (float)H - 1.0f;
  if (st)
    for (int i = 0; i < st->nMon; i++)
      if (st->mon[i].bottom - 1 > s->gy) s->gy = (float)(st->mon[i].bottom - 1);
  s->xMin = 16.0f, s->xMax = W / s->U - 16.0f;
  s->nMon = 0, s->arena = 0;
  if (st)
    for (int i = 0; i < st->nMon && s->nMon < IF_MAXMON; i++) {
      const RECT *r = &st->mon[i];
      if (r->right - r->left < 40) continue;
      if (i == st->prim) s->arena = s->nMon;
      s->monL[s->nMon] = r->left / s->U, s->monR[s->nMon] = r->right / s->U;
      s->monT[s->nMon] = (float)r->top, s->monB[s->nMon] = (float)(r->bottom - 1);
      s->nMon++;
    }
  if (!s->nMon) s->nMon = 1, s->monL[0] = 0, s->monR[0] = W / s->U, s->monT[0] = 0, s->monB[0] = (float)(H - 1);
  if_set_arena(s, s->arena);
  s->roamT = 600;
  if_stage_floors(s, st);
  if_stage_windows(s, st);
  s->seed = seed ? seed : 0x5EED1234u;
  s->wins = wins;
  s->round = 1, s->phase = RP_INTRO, s->phT = 0;
  s->dom = DOM_NONE, s->winner = -1;
  float mid = s->homeX;
  for (int i = 0; i < 2; i++) {
    IfFighter *F = &s->F[i];
    memset(F, 0, sizeof(*F));
    F->who = i, F->f = i ? -1 : 1;
    F->x = F->home = mid + (i ? 85.0f : -85.0f);
    F->y = F->fl = F->homeFl = if_ground(s, F->x);
    if_fighter_reset(F);
    F->alpha = 0;
    F->seenId = -1;
    F->anim = if_idle_anim(F);
    F->from = kPoses[i ? FP_S_IDLE : FP_G_IDLE];
    if_anim_pose(&F->pose, F->anim, 0, &F->from);
    fa_skel(&F->sk, &F->pose, F->f);
    F->think = 30;
    s->hud[i][0] = IF_HP, s->hud[i][1] = 250;
  }
}

/* ===================== рисование: кисти ===================== */

static float if_X(const IfScene *s, float x) { return x * s->U + s->shx; }
static float if_Y(const IfScene *s, float y) { return s->gy - y * s->U + s->shy; }

/* мягкое пятно по таблице: без корня на каждый пиксель */
static float g_ifFall[257];
static void if_fall_init(void) {
  if (g_ifFall[0] > 0) return;
  for (int i = 0; i <= 256; i++) {
    float t = 1.0f - sqrtf(i / 256.0f);
    g_ifFall[i] = t * t;
  }
}
static void if_glow(FCv *c, float cx, float cy, float r, FCol col, float a, int light) {
  int X0, Y0, X1, Y1;
  if (r < 0.6f || a <= 0.004f || !fv_clip(c, cx - r, cy - r, cx + r, cy + r, &X0, &Y0, &X1, &Y1)) return;
  if (a > 1) a = 1;
  float k = 256.0f / (r * r);
  for (int y = Y0; y < Y1; y++) {
    float dy = y + 0.5f - cy, qy = dy * dy * k;
    if (qy >= 256.0f) continue;
    unsigned char *row = c->px + (size_t)y * c->w * 4;
    for (int x = X0; x < X1; x++) {
      float dx = x + 0.5f - cx, q = dx * dx * k + qy;
      if (q >= 256.0f) continue;
      float w = a * g_ifFall[(int)q];
      if (light) fv_light(row + x * 4, col, w);
      else fv_over(row + x * 4, col, w);
    }
  }
}

/* мягкий эллипс: плотный в середине, тает к краю (тени, ожоги) */
static void if_soft_ell(FCv *c, float cx, float cy, float rx, float ry, FCol col, float a) {
  int X0, Y0, X1, Y1;
  if (rx < 0.5f || ry < 0.5f || a <= 0.004f || !fv_clip(c, cx - rx, cy - ry, cx + rx, cy + ry, &X0, &Y0, &X1, &Y1)) return;
  float kx = 1.0f / (rx * rx), ky = 1.0f / (ry * ry);
  for (int y = Y0; y < Y1; y++) {
    float dy = y + 0.5f - cy, qy = dy * dy * ky;
    if (qy >= 1.0f) continue;
    unsigned char *row = c->px + (size_t)y * c->w * 4;
    for (int x = X0; x < X1; x++) {
      float dx = x + 0.5f - cx, q = dx * dx * kx + qy;
      if (q < 1.0f) fv_over(row + x * 4, col, a * fa_sat((1.0f - q) * 2.2f));
    }
  }
}

/* ломаная одним мазком: все отрезки — одна фигура, без тёмных узлов на стыках */
static void if_path(FCv *c, const float *xy, const float *w, int n, FCol col, float a) {
  if (n < 2 || a <= 0.004f) return;
  FPart p;
  fp_init(&p, col, col, col, 0, 0);
  p.alpha = a;
  for (int i = 0; i + 1 < n && p.n < FP_MAX; i++)
    fs_cap(fp_add(&p), xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], w[i], w[i + 1]);
  fp_draw(c, &p);
}

/* дуга эллипса; taper — тоньше к концам (полумесяц) */
static void if_arc(FCv *c, float cx, float cy, float rx, float ry, float a0, float a1, float w, FCol col, float a,
                   int taper) {
  float xy[34], ww[17];
  int n = 16;
  for (int i = 0; i <= n; i++) {
    float t = (float)i / n, an = a0 + (a1 - a0) * t;
    xy[i * 2] = cx + cosf(an) * rx, xy[i * 2 + 1] = cy + sinf(an) * ry;
    ww[i] = taper ? w * (0.15f + 0.85f * sinf(t * FA_PI)) : w;
  }
  if_path(c, xy, ww, n + 1, col, a);
}

/* молния от (x0,y0) к (x1,y1): ореол и сердцевина */
static void if_bolt(FCv *c, float x0, float y0, float x1, float y1, unsigned seed, int n, float amp, float wg, FCol cg,
                    float wc, FCol cc, float a) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l < 1 || n < 1) return;
  if (n > 12) n = 12;
  float nx = -dy / l, ny = dx / l, xy[26], w1[13], w2[13];
  for (int i = 0; i <= n; i++) {
    float t = (float)i / n, off = (i && i < n) ? (if_rf(&seed) * 2 - 1) * amp : 0;
    xy[i * 2] = x0 + dx * t + nx * off, xy[i * 2 + 1] = y0 + dy * t + ny * off;
    w1[i] = wg * (1.0f - 0.6f * t), w2[i] = wc * (1.0f - 0.7f * t);
  }
  if (wg > 0) if_path(c, xy, w1, n + 1, cg, a * 0.55f);
  if_path(c, xy, w2, n + 1, cc, a);
}

/* острый луч: широкий у начала, игла на конце */
static void if_spike(FCv *c, float x0, float y0, float x1, float y1, float w, FCol col, float a) {
  fv_line(c, x0, y0, x1, y1, w, 0.25f, col, a);
}

/* ===================== рисование: частицы ===================== */

static void if_draw_parts(IfScene *s) {
  FCv *c = &s->cv;
  float U = s->U;
  for (int i = 0; i < IF_MAXP; i++) {
    IfPart *p = &s->pt[i];
    if (p->t >= p->life) continue;
    float k = p->t / p->life, X = if_X(s, p->x), Y = if_Y(s, p->y), r = p->sz * U;
    switch (p->kind) {
    case PT_SPARK: {
      float vx = p->vx * U * 1.6f, vy = -p->vy * U * 1.6f;
      fv_line(c, X - vx, Y - vy, X, Y, 0.3f, fmaxf(0.6f, r * 0.42f * (1 - k * 0.6f)), p->c, 1 - k);
      break;
    }
    case PT_DOT:
      fv_disc(c, X, Y, fmaxf(0.7f, r * 0.5f * (1 - k * 0.5f)), p->c, 1 - k * k);
      break;
    case PT_FIRE: { /* огонь: бело-жёлтый → оранжевый → красный → копоть */
      FCol col = k < 0.25f ? fa_mix(FC(255, 240, 180), FC(255, 150, 40), k / 0.25f)
                           : (k < 0.6f ? fa_mix(FC(255, 150, 40), FC(210, 40, 20), (k - 0.25f) / 0.35f)
                                       : fa_mix(FC(210, 40, 20), FC(50, 30, 30), (k - 0.6f) / 0.4f));
      float rr = r * (0.6f + 0.7f * k);
      if_glow(c, X, Y, rr, col, (1 - k) * 0.95f, k < 0.5f);
      break;
    }
    case PT_SMOKE:
      if_glow(c, X, Y, r * (0.7f + 0.8f * k), p->c, 0.5f * (1 - k) * fa_sat(k * 6), 0);
      break;
    case PT_DUST:
      if_glow(c, X, Y, r * (0.6f + 0.9f * k), p->c, 0.6f * (1 - k), 0);
      break;
    case PT_SHARD: {
      FPart q;
      fp_init(&q, p->c, fa_dim(p->c, 0.6f), FC(24, 20, 30), fa_clampf(U * 0.3f, 0.6f, 1.2f), r * 0.5f);
      q.alpha = 1 - k * k;
      float cr = cosf(p->rot), sr = sinf(p->rot), pts[8];
      static const float kSh[8] = {1.0f, 0.1f, 0.2f, 0.8f, -0.9f, 0.3f, -0.3f, -0.7f};
      for (int j = 0; j < 4; j++)
        pts[j * 2] = X + (kSh[j * 2] * cr - kSh[j * 2 + 1] * sr) * r, pts[j * 2 + 1] = Y + (kSh[j * 2] * sr + kSh[j * 2 + 1] * cr) * r;
      fs_poly(fp_add(&q), pts, 4);
      fp_draw(c, &q);
      break;
    }
    case PT_STAR: {
      float rr = r * (1 - k) * 2.2f, a = 1 - k;
      if_spike(c, X, Y, X + rr, Y, rr * 0.16f, p->c, a), if_spike(c, X, Y, X - rr, Y, rr * 0.16f, p->c, a);
      if_spike(c, X, Y, X, Y + rr, rr * 0.16f, p->c, a), if_spike(c, X, Y, X, Y - rr, rr * 0.16f, p->c, a);
      break;
    }
    case PT_BIT: {
      float cr = cosf(p->rot) * r, sr = sinf(p->rot) * r;
      fv_line(c, X - cr, Y - sr, X + cr, Y + sr, r * 0.35f, r * 0.35f, p->c, fa_sat((1 - k) * 3));
      break;
    }
    case PT_EMBER:
      fv_disc(c, X, Y, fmaxf(0.7f, r * 0.4f), p->c, 1 - k);
      if (k < 0.5f) if_glow(c, X, Y, fmaxf(2.0f, r * 1.6f), p->c, 0.5f - k, 1);
      break;
    }
  }
}

/* ===================== рисование: вспышки и следы ударов ===================== */

/* эллипс на земле обводкой (кольцо телепорта, волна от взрыва) */
static void if_ground_ring(FCv *c, float cx, float cy, float rx, float ry, float w, FCol col, float a) {
  if_arc(c, cx, cy, rx, ry, 0, 2 * FA_PI, w, col, a, 0);
}

static void if_draw_eff(IfScene *s, IfEff *e, int front) {
  FCv *c = &s->cv;
  float U = s->U, k = e->t / (e->life > 0 ? e->life : 1), X = if_X(s, e->x), Y = if_Y(s, e->y);
  unsigned sd = e->seed;
  int back = e->kind == EF_SCORCH || e->kind == EF_CRACK || e->kind == EF_AFTER || e->kind == EF_HEAL;
  if (back == front || e->kind == EF_CALL) return;
  switch (e->kind) {
  case EF_SPARK: { /* звезда удара: острые лучи, белое ядро, кольцо */
    float R = e->sz * U, a = 1 - k, grow = 0.55f + 0.45f * fa_smooth(e->t / 3);
    int n = e->sz > 1.2f ? 9 : 6;
    if_glow(c, X, Y, R * (12 + 8 * k), e->c, 0.45f * a, 1);
    for (int i = 0; i < n; i++) {
      float an = if_rf(&sd) * 2 * FA_PI, len = R * (8 + 10 * if_rf(&sd)) * grow, w = R * 1.3f * a + 0.6f;
      float x1 = X + cosf(an) * len, y1 = Y + sinf(an) * len;
      if_spike(c, X, Y, x1, y1, w * 1.5f, e->c, a * 0.85f);
      if_spike(c, X, Y, x1, y1, w * 0.7f, FC(255, 255, 255), a);
    }
    if_glow(c, X, Y, R * 5 * a, FC(255, 255, 255), a, 1);
    fv_ring(c, X, Y, R * (4 + 14 * k), fmaxf(0.8f, R * 1.3f * a), e->c, a * 0.7f);
    break;
  }
  case EF_GUARD: { /* блок: дуга света перед руками */
    float a = 1 - k, R = U * 14 * (1 + 0.25f * k), base = e->a > 0 ? 0.0f : FA_PI;
    float cx = X - e->a * R * 0.55f;
    if_arc(c, cx, Y, R, R * 1.15f, base - 0.95f, base + 0.95f, fmaxf(1.2f, U * 1.2f * a), FC(150, 210, 255), a * 0.6f, 1);
    if_arc(c, cx, Y, R, R * 1.15f, base - 0.85f, base + 0.85f, fmaxf(0.8f, U * 0.5f * a), FC(255, 255, 255), a, 1);
    for (int i = 0; i < 5; i++) {
      float an = base + if_rr(&sd, -0.9f, 0.9f), l = U * if_rr(&sd, 5, 11) * (0.5f + k);
      float x0 = cx + cosf(an) * R, y0 = Y + sinf(an) * R * 1.15f;
      if_spike(c, x0, y0, x0 + cosf(an) * l, y0 + sinf(an) * l, fmaxf(0.7f, U * 0.5f), FC(220, 240, 255), a);
    }
    break;
  }
  case EF_INF: { /* Бесконечность: кулак упёрся в пустоту — рябь и линза */
    float a = 1 - k;
    for (int i = 0; i < 4; i++) {
      float kk = fa_sat(k * 1.4f - i * 0.16f);
      if (kk <= 0 || kk >= 1) continue;
      fv_ring(c, X, Y, U * (3 + 17 * kk), fmaxf(0.9f, U * 0.55f), FC(170, 225, 255), (1 - kk) * 0.85f);
    }
    float lx = X + e->a * U * 2.5f;
    if_arc(c, lx, Y, U * 4.5f, U * 19, -FA_PI * 0.5f, FA_PI * 0.5f, fmaxf(1.0f, U * 0.7f), FC(200, 235, 255), a * 0.7f, 1);
    if_arc(c, lx, Y, U * 4.5f, U * 19, FA_PI * 0.5f, FA_PI * 1.5f, fmaxf(1.0f, U * 0.7f), FC(200, 235, 255), a * 0.7f, 1);
    if_glow(c, X, Y, U * 16, FC(120, 200, 255), 0.35f * a, 1);
    for (int i = 0; i < 6; i++) { /* искорки на ряби */
      float an = i * FA_PI / 3 + e->t * 0.05f, rr = U * (6 + 12 * k);
      fv_disc(c, X + cosf(an) * rr, Y + sinf(an) * rr, fmaxf(0.7f, U * 0.45f), FC(255, 255, 255), a);
    }
    break;
  }
  case EF_BF: { /* Чёрная вспышка: чёрные молнии в красном ореоле */
    float a = 1 - k * k, R = U * e->sz;
    unsigned fl = e->seed + (unsigned)(e->t / 2) * 7919u; /* молнии мигают — новые каждые два кадра */
    if_glow(c, X, Y, R * 30 * (0.6f + 0.4f * k), FC(255, 30, 50), 0.6f * (1 - k), 1);
    fv_ring(c, X, Y, R * (8 + 34 * k), fmaxf(1.0f, R * 2.2f * (1 - k)), FC(12, 2, 8), 0.75f * (1 - k));
    for (int i = 0; i < 7; i++) {
      float an = if_rf(&fl) * 2 * FA_PI, len = R * (16 + 22 * if_rf(&fl)) * (0.6f + 0.4f * fa_smooth(e->t / 4));
      if_bolt(c, X, Y, X + cosf(an) * len, Y + sinf(an) * len, if_rnd(&fl), 6, len * 0.16f, fmaxf(2.4f, U * 1.6f),
              FC(255, 30, 50), fmaxf(1.2f, U * 0.7f), FC(10, 2, 6), a);
    }
    fv_disc(c, X, Y, R * 5.5f * (1 - k), FC(10, 2, 6), a);
    fv_ring(c, X, Y, R * 5.5f * (1 - k) + 1, fmaxf(1.2f, U * 0.7f), FC(255, 40, 60), a);
    break;
  }
  case EF_CUTS: { /* Рассечение: тонкие разрезы, белые с красной кромкой */
    float a = 1 - k, L = U * 13 * e->sz, grow = fa_smooth(e->t / 3);
    float dx = cosf(e->a), dy = sinf(e->a), nx = -dy, ny = dx;
    for (int i = 0; i < 3; i++) {
      float off = (i - 1) * U * 3.4f * e->sz + if_rr(&sd, -1, 1) * U, sh = if_rr(&sd, -0.3f, 0.3f) * L;
      float mx = X + nx * off + dx * sh, my = Y + ny * off + dy * sh, l = L * (0.7f + 0.3f * if_rf(&sd)) * grow;
      float x0 = mx - dx * l, y0 = my - dy * l, x1 = mx + dx * l, y1 = my + dy * l;
      float w = fmaxf(0.8f, U * 0.75f * e->sz);
      fv_line(c, mx, my, x0, y0, w * 2.4f, 0.4f, FC(255, 40, 60), a * 0.5f);
      fv_line(c, mx, my, x1, y1, w * 2.4f, 0.4f, FC(255, 40, 60), a * 0.5f);
      fv_line(c, mx, my, x0, y0, w, 0.25f, FC(255, 255, 255), a);
      fv_line(c, mx, my, x1, y1, w, 0.25f, FC(255, 255, 255), a);
    }
    break;
  }
  case EF_CROSS: { /* Разруб: крест по цели, края разреза расходятся */
    float a = 1 - fa_sat((k - 0.3f) / 0.7f), L = U * 22 * e->sz, open = fa_smooth(k) * U * 2.6f * e->sz;
    for (int d = 0; d < 2; d++) {
      float an = (d ? 0.82f : -0.82f) * e->a - FA_PI * 0.5f, dx = cosf(an), dy = sinf(an), nx = -dy, ny = dx;
      for (int side = -1; side <= 1; side += 2) {
        float ox = nx * open * side, oy = ny * open * side, w = fmaxf(1.0f, U * 1.2f * e->sz);
        float x0 = X + ox - dx * L, y0 = Y + oy - dy * L, x1 = X + ox + dx * L, y1 = Y + oy + dy * L;
        fv_line(c, X + ox, Y + oy, x0, y0, w * 2.6f, 0.4f, FC(255, 30, 50), a * 0.55f);
        fv_line(c, X + ox, Y + oy, x1, y1, w * 2.6f, 0.4f, FC(255, 30, 50), a * 0.55f);
        fv_line(c, X + ox, Y + oy, x0, y0, w, 0.25f, FC(255, 255, 255), a);
        fv_line(c, X + ox, Y + oy, x1, y1, w, 0.25f, FC(255, 255, 255), a);
      }
    }
    if (e->t < 6) if_glow(c, X, Y, L * 0.8f, FC(255, 220, 220), 1 - e->t / 6, 1);
    break;
  }
  case EF_WCUT: { /* Разрез мира: одна прямая через весь экран — белый шов в алом ореоле, края расходятся */
    float L = (float)(s->W + s->H), grow = fa_smooth(e->t / 3), a = 1 - fa_sat((k - 0.35f) / 0.65f);
    float an = -FA_PI * 0.5f + e->a, dx = cosf(an), dy = sinf(an), nx = -dy, ny = dx, l = L * grow;
    float open = fa_smooth(k) * U * 3.5f, w = fmaxf(1.0f, U * 1.1f);
    if (e->t < 5) fv_line(c, X - dx * l, Y - dy * l, X + dx * l, Y + dy * l, U * 9, U * 9, FC(255, 60, 70), 0.35f * (1 - e->t / 5));
    for (int side = -1; side <= 1; side += 2) {
      float ox = nx * open * side, oy = ny * open * side;
      fv_line(c, X + ox, Y + oy, X + ox - dx * l, Y + oy - dy * l, w * 3.2f, w * 0.8f, FC(255, 30, 50), a * 0.55f);
      fv_line(c, X + ox, Y + oy, X + ox + dx * l, Y + oy + dy * l, w * 3.2f, w * 0.8f, FC(255, 30, 50), a * 0.55f);
      fv_line(c, X + ox, Y + oy, X + ox - dx * l, Y + oy - dy * l, w, 0.4f, FC(255, 255, 255), a);
      fv_line(c, X + ox, Y + oy, X + ox + dx * l, Y + oy + dy * l, w, 0.4f, FC(255, 255, 255), a);
    }
    if (e->t < 8) if_glow(c, X, Y, U * 40, FC(255, 220, 225), 1 - e->t / 8, 1);
    break;
  }
  case EF_RING: {
    float a = 1 - k;
    fv_ring(c, X, Y, U * e->sz * (6 + 30 * k), fmaxf(1.0f, U * e->sz * 1.5f * a), e->c, a * 0.85f);
    if_glow(c, X, Y, U * e->sz * 18 * a, e->c, 0.35f * a, 1);
    break;
  }
  case EF_FLASH: {
    float a = (1 - k) * (1 - k);
    if_glow(c, X, Y, U * e->sz * 24 * (0.5f + 0.5f * k), e->c, 0.9f * a, 1);
    if_glow(c, X, Y, U * e->sz * 8 * (1 - k), FC(255, 255, 255), a, 1);
    break;
  }
  case EF_BOOM: { /* Разверзнись: огненный шар — плотные клубы с одной обводкой, потом дым вверх */
    float big = e->sz, R = U * 40 * big * (0.35f + 0.65f * fa_smooth(e->t / 10)), fade = 1 - fa_sat((k - 0.55f) / 0.45f);
    float burn = fa_sat(k * 1.6f); /* 0 — огонь, 1 — дым */
    if_glow(c, X, Y, R * 2.6f, FC(255, 130, 40), 0.6f * (1 - burn), 1);
    for (int pass = 0; pass < 3; pass++) { /* обводка, клубы, жар внутри */
      unsigned q = sd;
      for (int i = 0; i < 14; i++) {
        float an = if_rf(&q) * 2 * FA_PI, rr = R * sqrtf(if_rf(&q)) * 0.62f, sz = if_rf(&q);
        float bx = X + cosf(an) * rr, by = Y + sinf(an) * rr * 0.8f - R * 0.35f * k * (0.5f + sz);
        float br = R * (0.26f + 0.2f * sz) * (1 + 0.25f * k);
        if (pass == 0)
          fv_disc(c, bx, by, br + fmaxf(1.0f, U * 0.6f), fa_mix(FC(120, 20, 10), FC(30, 22, 26), burn), 0.9f * fade);
        else if (pass == 1)
          fv_disc(c, bx, by, br, fa_mix(fa_mix(FC(255, 160, 40), FC(240, 70, 20), sz), FC(74, 60, 64), burn), fade);
        else if (burn < 0.6f)
          fv_disc(c, bx - br * 0.2f, by - br * 0.25f, br * 0.6f, FC(255, 236, 160), (1 - burn / 0.6f) * fade);
      }
    }
    if (e->t < 10) if_glow(c, X, Y, R * 1.1f, FC(255, 255, 235), 1 - e->t / 10, 1);
    float gy = if_Y(s, if_floor(s, e->x, e->y, 0));
    if_ground_ring(c, X, gy, R * (1.0f + 1.6f * k), R * 0.16f * (1.0f + 1.6f * k), fmaxf(1.2f, U * 1.6f * (1 - k)),
                   FC(255, 190, 120), 0.8f * (1 - k));
    break;
  }
  case EF_REDBOOM: { /* Красный: отталкивающая волна */
    float R = U * 30 * e->sz, a = 1 - k, kk = 1 - (1 - k) * (1 - k) * (1 - k), rr = R * (0.2f + 1.2f * kk);
    if_glow(c, X, Y, R * 1.1f * a, FC(255, 60, 50), 0.9f * a, 1);
    fv_ring(c, X, Y, rr, fmaxf(1.5f, U * 4 * e->sz * a), FC(255, 50, 50), a);
    fv_ring(c, X, Y, rr - U * 1.2f, fmaxf(1.0f, U * 1.4f * a), FC(255, 230, 225), a);
    for (int i = 0; i < 12; i++) {
      float an = i * FA_PI / 6 + if_rr(&sd, -0.2f, 0.2f), r0 = rr * 1.05f, r1 = r0 + R * 0.4f * a;
      if_spike(c, X + cosf(an) * r0, Y + sinf(an) * r0, X + cosf(an) * r1, Y + sinf(an) * r1, fmaxf(0.8f, U * 0.8f),
               FC(255, 200, 190), a * 0.85f);
    }
    if_glow(c, X, Y, R * 0.45f * a, FC(255, 255, 255), a, 1);
    break;
  }
  case EF_CRACK: { /* земля треснула: камни торчат и уходят обратно */
    float a = 1 - fa_sat((k - 0.6f) / 0.4f), sink = fa_sat((k - 0.45f) / 0.55f), gy = if_Y(s, e->y);
    int n = 4 + (int)(e->sz * 3);
    for (int i = 0; i < n; i++) {
      float ox = if_rr(&sd, -1, 1) * U * 15 * e->sz, h = U * if_rr(&sd, 3, 10) * e->sz * (1 - sink) * fa_smooth(e->t / 4);
      float w = U * if_rr(&sd, 1.8f, 4.0f) * e->sz, lean = if_rr(&sd, -0.6f, 0.6f);
      if (h < 1) continue;
      float pts[8] = {X + ox - w, gy + 1, X + ox - w * 0.3f + lean * h * 0.5f, gy - h * 0.7f, X + ox + lean * h, gy - h,
                      X + ox + w, gy + 1};
      FPart q;
      fp_init(&q, FC(150, 140, 136), FC(96, 88, 88), FC(34, 28, 32), fa_clampf(U * 0.4f, 0.8f, 1.6f), U * 0.9f);
      q.alpha = a;
      fs_poly(fp_add(&q), pts, 4);
      fp_draw(c, &q);
    }
    if_bolt(c, X - U * 18 * e->sz, gy - 0.5f, X + U * 18 * e->sz, gy - 0.5f, e->seed, 8, U * 1.0f, 0, FC(0, 0, 0),
            fmaxf(0.8f, U * 0.45f), FC(40, 30, 34), a * 0.7f);
    break;
  }
  case EF_AFTER: { /* послеобраз телепорта */
    FFig g = e->fig;
    g.x = X, g.y = Y, g.U = U;
    g.alpha = e->fig.alpha * (1 - k);
    ff_draw(c, &g);
    break;
  }
  case EF_TELE: { /* телепорт: столб света и кольцо у ног */
    float a = 1 - k, H = U * 112 * e->sz, gy = if_Y(s, e->y), w = U * 4.5f * e->sz * a + 0.8f;
    fv_line(c, X, gy, X, gy - H, w * 2.6f, w * 0.4f, FC(110, 180, 255), 0.4f * a);
    fv_line(c, X, gy, X, gy - H, w, w * 0.15f, FC(255, 255, 255), a);
    if_ground_ring(c, X, gy, U * 18 * e->sz * (0.5f + 0.7f * k), U * 3.4f * e->sz * (0.5f + 0.7f * k), fmaxf(1, U * 0.8f),
                   FC(160, 215, 255), a);
    for (int i = 0; i < 9; i++) {
      float ph = if_rf(&sd), yy = gy - H * fmodf(ph + k * 0.9f, 1.0f), xx = X + if_rr(&sd, -1, 1) * U * 7;
      fv_disc(c, xx, yy, fmaxf(0.7f, U * 0.5f), FC(220, 240, 255), a);
    }
    if_glow(c, X, gy - H * 0.45f, U * 30 * e->sz * a, FC(120, 190, 255), 0.35f * a, 1);
    break;
  }
  case EF_HEAL: { /* обратная техника: мягкий свет и искры вверх */
    IfFighter *F = &s->F[e->owner];
    float fx = if_X(s, F->x), fy = if_Y(s, F->y), a = fa_sat(e->t / 10) * (1 - fa_sat((k - 0.7f) / 0.3f));
    if_glow(c, fx, fy - U * 50, U * 52, FC(150, 255, 200), 0.3f * a, 1);
    for (int i = 0; i < 16; i++) {
      float ph = fmodf(if_rf(&sd) + e->t * (0.01f + 0.012f * if_rf(&sd)), 1.0f);
      float xx = fx + if_rr(&sd, -1, 1) * U * 20, yy = fy - U * (ph * 115);
      fv_disc(c, xx, yy, fmaxf(0.8f, U * 0.6f), FC(210, 255, 225), a * sinf(ph * FA_PI));
    }
    if_ground_ring(c, fx, if_Y(s, F->fl), U * 22, U * 3.6f, fmaxf(1, U * 0.7f), FC(170, 255, 200), 0.6f * a);
    break;
  }
  case EF_SCORCH: { /* след Фиолетового: выжженная полоса */
    float gy = if_Y(s, e->y);
    if_soft_ell(c, X, gy, U * 16 * e->sz, U * 2.6f * e->sz, FC(40, 18, 52), 0.6f * (1 - k));
    if (k < 0.3f) if_glow(c, X, gy - U * 2, U * 15 * e->sz, FC(200, 120, 255), 0.5f * (0.3f - k) / 0.3f, 1);
    break;
  }
  case EF_IMPLODE: { /* Синий схлопнулся */
    for (int i = 0; i < 3; i++) {
      float kk = fa_sat(k * 1.5f - i * 0.2f);
      if (kk <= 0 || kk >= 1) continue;
      fv_ring(c, X, Y, U * 56 * (1 - kk) + 1, fmaxf(1.0f, U * 1.6f * (1 - kk)), FC(130, 195, 255), 0.85f);
    }
    if (k < 0.5f) fv_disc(c, X, Y, U * 9 * (1 - k * 2) + 1, FC(12, 20, 80), 0.9f);
    if (k > 0.5f) {
      float kk = (k - 0.5f) / 0.5f;
      if_glow(c, X, Y, U * 28 * (0.3f + kk), FC(130, 200, 255), 0.85f * (1 - kk), 1);
      if_glow(c, X, Y, U * 9 * (1 - kk), FC(255, 255, 255), 1 - kk, 1);
    }
    break;
  }
  case EF_WALL: { /* о край экрана: трещины по стене */
    float a = 1 - k, base = e->a > 0 ? FA_PI : 0.0f;
    for (int i = 0; i < 6; i++) {
      float an = base + if_rr(&sd, -1.35f, 1.35f), l = U * if_rr(&sd, 10, 28);
      if_bolt(c, X, Y, X + cosf(an) * l, Y + sinf(an) * l, if_rnd(&sd), 4, l * 0.15f, fmaxf(2.0f, U * 1.2f),
              FC(255, 255, 255), fmaxf(0.9f, U * 0.45f), FC(40, 36, 46), a);
    }
    if_glow(c, X, Y, U * 22 * (0.5f + 0.5f * k), FC(255, 255, 255), 0.6f * a * a, 1);
    break;
  }
  }
}

/* ===================== рисование: техники в полёте и в руках ===================== */

static void if_orb_blue(FCv *c, float X, float Y, float R, float t) {
  if_glow(c, X, Y, R * 3.0f, FC(60, 140, 255), 0.8f, 1);
  fv_disc(c, X, Y, R * 1.12f, FC(90, 170, 255), 0.9f);
  fv_disc(c, X, Y, R, FC(14, 24, 96), 0.97f);
  fv_ring(c, X, Y, R, fmaxf(1.0f, R * 0.18f), FC(190, 235, 255), 1);
  if_arc(c, X, Y, R * 0.62f, R * 0.62f, t * 0.3f, t * 0.3f + 2.2f, fmaxf(0.7f, R * 0.12f), FC(120, 190, 255), 0.8f, 1);
  fv_disc(c, X - R * 0.35f, Y - R * 0.35f, fmaxf(0.7f, R * 0.16f), FC(255, 255, 255), 0.9f);
}
static void if_orb_red(FCv *c, float X, float Y, float R, float t) {
  if_glow(c, X, Y, R * 3.2f, FC(255, 50, 40), 0.75f, 1);
  fv_disc(c, X, Y, R, FC(255, 80, 70), 1);
  if_glow(c, X, Y, R * 0.95f, FC(255, 255, 255), 0.9f, 1);
  for (int i = 0; i < 2; i++) {
    float an = t * 0.4f + i * FA_PI;
    if_arc(c, X, Y, R * 1.7f, R * 1.7f, an, an + 1.6f, fmaxf(0.8f, R * 0.2f), FC(255, 205, 195), 0.85f, 1);
  }
}
static void if_orb_purple(FCv *c, float X, float Y, float R, float t, float dir, unsigned seed) {
  if_glow(c, X, Y, R * 2.4f, FC(170, 80, 255), 0.65f, 1);
  fv_disc(c, X, Y, R * 0.93f, FC(34, 8, 62), 0.97f);
  for (int i = 0; i < 4; i++) {
    float an = -t * 0.3f * dir + i * FA_PI * 0.5f, rr = R * (0.3f + 0.13f * i);
    if_arc(c, X, Y, rr, rr, an, an + 1.8f, fmaxf(0.8f, R * 0.06f), FC(205, 135, 255), 0.75f, 1);
  }
  fv_ring(c, X, Y, R * 0.95f, fmaxf(1.6f, R * 0.08f), FC(238, 205, 255), 1);
  fv_ring(c, X, Y, R * 1.12f, fmaxf(1.0f, R * 0.035f), FC(190, 120, 255), 0.6f);
  unsigned fl = seed + (unsigned)(t / 2) * 131u;
  for (int i = 0; i < 3; i++) {
    float an = if_rf(&fl) * 2 * FA_PI, x0 = X + cosf(an) * R, y0 = Y + sinf(an) * R;
    float l = R * if_rr(&fl, 0.4f, 0.8f);
    if_bolt(c, x0, y0, x0 + cosf(an) * l, y0 + sinf(an) * l, if_rnd(&fl), 4, R * 0.12f, fmaxf(2.0f, R * 0.1f),
            FC(200, 140, 255), fmaxf(0.8f, R * 0.04f), FC(255, 255, 255), 0.9f);
  }
}

/* огонь в руках: несколько мигающих языков (без частиц — рисование мир не трогает) */
static void if_flame(FCv *c, float X, float Y, float R, float t, unsigned seed, float a) {
  if_glow(c, X, Y, R * 2.4f, FC(255, 130, 40), 0.6f * a, 1);
  for (int i = 0; i < 7; i++) {
    float ph = if_rf(&seed) * 6.28f, sp = if_rr(&seed, 0.25f, 0.45f);
    float ox = sinf(t * sp + ph) * R * 0.45f, oy = -fabsf(cosf(t * sp * 1.3f + ph)) * R * 0.8f;
    float rr = R * if_rr(&seed, 0.45f, 0.8f);
    FCol col = fa_mix(FC(255, 240, 190), FC(255, 120, 30), if_rf(&seed));
    if_glow(c, X + ox, Y + oy, rr, col, 0.9f * a, 1);
  }
  if_glow(c, X, Y, R * 0.6f, FC(255, 255, 230), a, 1);
}

/* back — слой за бойцами (тень Синего), иначе — перед ними */
static void if_draw_pj(IfScene *s, IfProj *p, int back) {
  FCv *c = &s->cv;
  float U = s->U, X = if_X(s, p->x), Y = if_Y(s, p->y);
  float dir = p->vx >= 0 ? 1.0f : -1.0f;
  if (back && p->kind != PJ_BLUE) return;
  switch (p->kind) {
  case PJ_BLUE: { /* Синий: точка притяжения — тень вокруг, всё течёт внутрь по спирали */
    float grow = fa_smooth(p->t / 8) * (1 - fa_smooth((p->t - (p->life - 6)) / 6)), R = U * p->r * 1.25f * grow;
    if (grow <= 0.01f) break;
    if (back) {
      if_glow(c, X, Y, U * 64 * grow, FC(14, 24, 80), 0.3f, 0);
      if_glow(c, X, Y, U * 30 * grow, FC(70, 150, 255), 0.7f, 1);
      break;
    }
    for (int i = 0; i < 16; i++) { /* струи: снаружи тонкие, к ядру толще */
      float ph = fmodf(p->t * 0.03f + i * 0.382f, 1.0f), rr = U * (14 + 54 * (1 - ph)) * grow;
      float an = i * 2.4f + ph * 2.6f, an2 = an + 0.5f, r2 = rr * 0.72f;
      float a = fa_sat(ph * 2.2f) * fa_sat((1 - ph) * 5) * 0.9f;
      fv_line(c, X + cosf(an) * rr, Y + sinf(an) * rr * 0.9f, X + cosf(an2) * r2, Y + sinf(an2) * r2 * 0.9f,
              fmaxf(0.5f, U * 0.25f), fmaxf(0.8f, U * 0.9f), FC(150, 205, 255), a);
    }
    for (int i = 0; i < 3; i++) { /* кольца сжимаются к ядру */
      float ph = fmodf(p->t / 20 + i / 3.0f, 1.0f), r = U * (46 * (1 - ph) + 9) * grow;
      fv_ring(c, X, Y, r, fmaxf(1.2f, U * 1.0f), FC(150, 210, 255), ph * 0.8f);
    }
    if_orb_blue(c, X, Y, R, p->t);
    break;
  }
  case PJ_RED: { /* Красный: шар с хвостом */
    float vl = hypotf(p->vx, p->vy), dx = vl > 0 ? p->vx / vl : dir, dy = vl > 0 ? -p->vy / vl : 0, R = U * p->r * 1.35f;
    fv_line(c, X - dx * U * 48, Y - dy * U * 48, X, Y, 0.5f, R * 0.95f, FC(255, 60, 50), 0.55f);
    fv_line(c, X - dx * U * 28, Y - dy * U * 28, X, Y, 0.3f, R * 0.45f, FC(255, 220, 210), 0.7f);
    if_orb_red(c, X, Y, R, p->t);
    break;
  }
  case PJ_PURPLE: { /* Фиолетовый: стирает всё на пути */
    float R = U * p->r * fa_smooth(p->t / 6);
    fv_line(c, X - dir * U * 70, Y, X, Y, R * 0.12f, R * 0.95f, FC(150, 80, 230), 0.3f);
    fv_line(c, X - dir * U * 45, Y, X, Y, R * 0.05f, R * 0.4f, FC(235, 210, 255), 0.35f);
    if_orb_purple(c, X, Y, R, p->t, dir, (unsigned)(p - s->pj) * 977u + 5u);
    break;
  }
  case PJ_SLASH: { /* Рассечение: невидимое лезвие — серп с тёмной кромкой (видно и на белом) */
    float R = U * p->r * 1.5f, base = dir > 0 ? 0.0f : FA_PI, cx = X - dir * R * 0.6f, an = base + p->a;
    if_arc(c, cx + dir * U * 0.8f, Y, R, R, an - 1.0f, an + 1.0f, fmaxf(1.2f, U * 0.9f), FC(110, 0, 20), 0.6f, 1);
    if_arc(c, cx, Y, R, R, an - 1.0f, an + 1.0f, fmaxf(2.0f, U * 2.4f), FC(255, 50, 70), 0.5f, 1);
    if_arc(c, cx, Y, R, R, an - 0.95f, an + 0.95f, fmaxf(1.0f, U * 1.0f), FC(255, 255, 255), 0.95f, 1);
    for (int i = 0; i < 3; i++) {
      float oy = (i - 1) * R * 0.45f;
      fv_line(c, X - dir * U * (6 + 6 * i), Y + oy, X - dir * U * (40 + 8 * i), Y + oy, fmaxf(0.6f, U * 0.4f), 0.2f,
              FC(255, 220, 230), 0.4f);
    }
    break;
  }
  case PJ_ARROW: { /* Разверзнись: огненная стрела */
    float vl = hypotf(p->vx, p->vy), dx = vl > 0 ? p->vx / vl : dir, dy = vl > 0 ? -p->vy / vl : 0;
    float L = U * 28, nx = -dy, ny = dx;
    if_glow(c, X, Y, U * 20, FC(255, 140, 40), 0.75f, 1);
    fv_line(c, X - dx * L, Y - dy * L, X, Y, U * 0.7f, U * 1.6f, FC(255, 160, 50), 0.9f);
    fv_line(c, X - dx * L, Y - dy * L, X, Y, U * 0.25f, U * 0.65f, FC(255, 250, 220), 1);
    float pts[6] = {X + dx * U * 8, Y + dy * U * 8, X - dx * U * 2 + nx * U * 3.4f, Y - dy * U * 2 + ny * U * 3.4f,
                    X - dx * U * 2 - nx * U * 3.4f, Y - dy * U * 2 - ny * U * 3.4f};
    FPart q;
    fp_init(&q, FC(255, 245, 210), FC(255, 190, 90), FC(255, 110, 20), fmaxf(1.0f, U * 0.5f), U * 0.8f);
    fs_poly(fp_add(&q), pts, 3);
    fp_draw(c, &q);
    break;
  }
  }
}

/* техника в руках: Красный на пальце, Синий и Красный сходятся в Фиолетовый, лук Сукуны */
static void if_draw_charge(IfScene *s, IfFighter *F) {
  if (F->st != FS_MOVE || F->mv < 0 || F->stuck > 0) return;
  FCv *c = &s->cv;
  float U = s->U, t = F->mt, hx, hy, h2x, h2y;
  switch (F->mv) {
  case MV_G_BLUE:
    if (t < 16) {
      if_joint(F, FJ_HAND0, &hx, &hy);
      if_orb_blue(c, if_X(s, hx), if_Y(s, hy), U * (2.4f + 3.6f * fa_sat(t / 10)) * (t < 12 ? 1 : (16 - t) / 4), t);
    }
    break;
  case MV_G_RED:
  case MV_G_REDX:
    if (t >= 6 && t < (F->mv == MV_G_REDX ? 46 : 34)) {
      if_joint(F, FJ_HAND1, &hx, &hy);
      float k = fa_sat((t - 6) / 24), X0 = if_X(s, hx), Y0 = if_Y(s, hy), R = U * (2.0f + 4.6f * k);
      for (int i = 0; i < 7; i++) { /* сила стягивается к пальцу */
        float ph = fmodf(t * 0.06f + i / 7.0f, 1.0f), an = i * 0.898f + t * 0.04f, r0 = R * 1.4f + U * 20 * (1 - ph);
        fv_line(c, X0 + cosf(an) * r0, Y0 + sinf(an) * r0, X0 + cosf(an) * (r0 - U * 5), Y0 + sinf(an) * (r0 - U * 5), 0.4f,
                fmaxf(0.7f, U * 0.55f), FC(255, 120, 100), ph * 0.9f);
      }
      if_orb_red(c, X0, Y0, R, t);
    }
    break;
  case MV_G_PURPLE:
    if (t < 56) { /* руки врозь: Синий в одной, Красный в другой */
      float k = fa_sat((t - 6) / 40);
      if_joint(F, FJ_HAND0, &hx, &hy);
      if_joint(F, FJ_HAND1, &h2x, &h2y);
      if (t > 6) if_orb_blue(c, if_X(s, hx), if_Y(s, hy), U * (2.4f + 4.0f * k), t);
      if (t > 14) if_orb_red(c, if_X(s, h2x), if_Y(s, h2y), U * (2.2f + 3.8f * fa_sat((t - 14) / 34)), t);
    } else if (t < 92) { /* руки вместе: сходятся и сливаются */
      float k = fa_sat((t - 56) / 30);
      if_joint(F, FJ_HAND0, &hx, &hy);
      if_joint(F, FJ_HAND1, &h2x, &h2y);
      float mx = if_X(s, (hx + h2x) * 0.5f) + F->f * U * 6, my = if_Y(s, (hy + h2y) * 0.5f);
      float orb = U * 13 * (1 - k) + U * 2, an = t * 0.35f;
      if (k < 0.85f) {
        if_orb_blue(c, mx + cosf(an) * orb, my + sinf(an) * orb * 0.6f, U * 6.0f * (1 - k * 0.6f), t);
        if_orb_red(c, mx - cosf(an) * orb, my - sinf(an) * orb * 0.6f, U * 5.6f * (1 - k * 0.6f), t);
      }
      if (k > 0.3f) if_orb_purple(c, mx, my, U * 14 * fa_sat((k - 0.3f) / 0.7f) + U * 2, t, (float)F->f, 77u);
    }
    break;
  case MV_G_VOID: /* знак рукой, глаза открыты */
    if (t >= 20) {
      float a = fa_sat((t - 20) / 10) * (0.7f + 0.3f * sinf(t * 0.3f));
      if_joint(F, FJ_HAND1, &hx, &hy);
      if_glow(c, if_X(s, hx), if_Y(s, hy), U * 12, FC(150, 210, 255), 0.6f * a, 1);
      if_joint(F, FJ_HEAD, &hx, &hy);
      if_glow(c, if_X(s, hx + F->f * 3.5f), if_Y(s, hy + 1), U * 7, FC(120, 220, 255), 0.8f * a, 1);
    }
    break;
  case MV_G_FLICK:
    if (t >= 7 && t < 14) {
      if_joint(F, FJ_HAND1, &hx, &hy);
      if_glow(c, if_X(s, hx), if_Y(s, hy), U * 9 * (1 - (t - 7) / 7), FC(255, 90, 80), 0.9f, 1);
    }
    break;
  case MV_S_DISM: /* взмах — блеск на когтях */
  case MV_S_FAN:
    if ((t >= 16 && t < 24) || (t >= 37 && t < 45)) {
      float k = fmodf(t - 16, 21) / 8;
      if_joint(F, FJ_HAND1, &hx, &hy);
      float ax = if_X(s, hx) - F->f * U * 8, ay = if_Y(s, hy), a0 = F->f > 0 ? -0.9f : FA_PI - 0.9f;
      if_arc(c, ax, ay, U * 22, U * 22, a0, a0 + 1.8f, fmaxf(1.6f, U * 2.2f), FC(255, 50, 70), 0.5f * (1 - k), 1);
      if_arc(c, ax, ay, U * 22, U * 22, a0, a0 + 1.8f, fmaxf(0.9f, U * 1.0f), FC(255, 255, 255), 1 - k, 1);
    }
    break;
  case MV_S_FUGA: {
    unsigned sd = 4242u + (unsigned)F->mvId;
    if (t < 56) { /* огонь собирается в ладонях */
      if_joint(F, FJ_HAND0, &hx, &hy);
      if_joint(F, FJ_HAND1, &h2x, &h2y);
      float k = fa_sat((t - 4) / 44);
      if (t > 4) if_flame(c, if_X(s, (hx + h2x) * 0.5f), if_Y(s, (hy + h2y) * 0.5f), U * (3 + 9 * k), t, sd, 1);
    } else if (t < 112) { /* лук из пламени: тетива к ближней руке, стрела вдоль */
      float a = t < 62 ? (t - 56) / 6 : (t < 97 ? 1.0f : 1 - (t - 97) / 15);
      if_joint(F, FJ_HAND0, &hx, &hy);
      if_joint(F, FJ_HAND1, &h2x, &h2y);
      float bx = if_X(s, hx), by = if_Y(s, hy), nx = if_X(s, h2x), ny = if_Y(s, h2y), R = U * 26;
      float base = F->f > 0 ? 0.0f : FA_PI, cx = bx - F->f * R * 0.55f;
      float tx0 = cx + cosf(base - 1.05f) * R * 0.55f, ty0 = by + sinf(base - 1.05f) * R;
      float tx1 = cx + cosf(base + 1.05f) * R * 0.55f, ty1 = by + sinf(base + 1.05f) * R;
      if_glow(c, bx, by, R * 1.5f, FC(255, 120, 30), 0.55f * a, 1);
      if_arc(c, cx, by, R * 0.55f, R, base - 1.05f, base + 1.05f, U * 3.4f, FC(255, 100, 20), 0.85f * a, 1);
      if_arc(c, cx, by, R * 0.55f, R, base - 1.0f, base + 1.0f, U * 1.5f, FC(255, 240, 190), a, 1);
      for (int i = 0; i < 5; i++) { /* лук горит по всей дуге */
        float an = base - 0.9f + i * 0.45f;
        if_flame(c, cx + cosf(an) * R * 0.55f, by + sinf(an) * R, U * 2.8f, t + i * 7, sd + 31u * i, a * 0.8f);
      }
      if (t < 97) {
        fv_line(c, tx0, ty0, nx, ny, fmaxf(0.6f, U * 0.3f), fmaxf(0.6f, U * 0.3f), FC(255, 200, 120), 0.8f * a);
        fv_line(c, tx1, ty1, nx, ny, fmaxf(0.6f, U * 0.3f), fmaxf(0.6f, U * 0.3f), FC(255, 200, 120), 0.8f * a);
        float ax = bx + F->f * U * 10, ay = by;
        fv_line(c, nx, ny, ax, ay, U * 2.0f, U * 1.3f, FC(255, 130, 30), 0.85f * a);
        fv_line(c, nx, ny, ax, ay, U * 0.8f, U * 0.5f, FC(255, 250, 220), a);
        if_flame(c, ax, ay, U * 5.5f, t, sd + 9u, a);
      }
    }
    break;
  }
  case MV_S_SHRINE: { /* знак Сукуны: тёмно-красное марево */
    float a = fa_sat(t / 12) * (0.6f + 0.4f * sinf(t * 0.4f));
    if_joint(F, FJ_CHEST, &hx, &hy);
    if_glow(c, if_X(s, hx), if_Y(s, hy), U * 46, FC(160, 10, 30), 0.35f * a, 0);
    if_joint(F, FJ_HAND0, &hx, &hy);
    if_glow(c, if_X(s, hx), if_Y(s, hy), U * 10, FC(255, 40, 60), 0.7f * a, 1);
    break;
  }
  case MV_G_RCT: { /* обратная техника: зелёный свет в ладони у виска */
    float a = fa_sat(t / 10) * (0.7f + 0.3f * sinf(t * 0.35f));
    if_joint(F, FJ_HAND1, &hx, &hy);
    if_glow(c, if_X(s, hx), if_Y(s, hy), U * 11, FC(150, 255, 190), 0.75f * a, 1);
    if_joint(F, FJ_HEAD, &hx, &hy);
    if_glow(c, if_X(s, hx), if_Y(s, hy), U * 16, FC(120, 255, 170), 0.3f * a, 1);
    break;
  }
  case MV_S_MAHO: { /* знак Десяти теней: тьма стекает с руки */
    float a = fa_sat(t / 8);
    if_joint(F, FJ_HAND1, &hx, &hy);
    if_glow(c, if_X(s, hx), if_Y(s, hy), U * 13, FC(60, 30, 110), 0.8f * a, 0);
    if_glow(c, if_X(s, hx), if_Y(s, hy), U * 5, FC(200, 160, 255), 0.7f * a, 1);
    break;
  }
  }
}

/* след удара: светлый мазок по пути кулака или стопы */
static void if_draw_trail(IfScene *s, IfFighter *F) {
  int n = F->trailN;
  if (n < 2) return;
  float xy[IF_TRAIL * 2], w[IF_TRAIL], w2[IF_TRAIL], U = s->U;
  for (int i = 0; i < n; i++) {
    xy[i * 2] = if_X(s, F->trail[i][0]), xy[i * 2 + 1] = if_Y(s, F->trail[i][1]);
    float k = 1.0f - (float)i / n;
    w[i] = U * 2.8f * k + 0.4f, w2[i] = U * 1.1f * k + 0.3f;
  }
  if_path(&s->cv, xy, w, n, F->who ? FC(255, 120, 140) : FC(140, 200, 255), 0.4f);
  if_path(&s->cv, xy, w2, n, FC(255, 255, 255), 0.75f);
}

static void if_draw_fighter(IfScene *s, IfFighter *F) {
  if (F->alpha <= 0.01f) return;
  FFig g;
  memset(&g, 0, sizeof(g));
  g.who = F->who, g.f = F->f;
  g.x = if_X(s, F->x), g.y = if_Y(s, F->y), g.U = s->U;
  g.p = F->pose;
  g.blind = F->blind, g.power = F->power;
  g.wind = F->wind, g.flash = fa_sat(F->flash), g.alpha = F->alpha;
  if (F->stuck > 0) g.tint = FC(150, 210, 255), g.tintA = 0.22f; /* застрял в Бесконечности */
  if (F->st == FS_STUN && s->dom == DOM_VOID) g.tint = FC(220, 235, 255), g.tintA = 0.18f + 0.12f * sinf(s->frame * 0.5f);
  ff_draw(&s->cv, &g);
}

static void if_draw_shadow(IfScene *s, IfFighter *F) {
  if (F->alpha <= 0.01f) return;
  float U = s->U, k = fa_sat(1 - if_up(F) / 150), lie = fabsf(sinf(F->pose.v[PV_ROLL] * FA_RAD));
  if_soft_ell(&s->cv, if_X(s, F->x), if_Y(s, F->fl) - 1, U * (20 + 22 * lie) * (0.6f + 0.4f * k), U * 3.2f * (0.6f + 0.4f * k),
              FC(16, 12, 24), 0.34f * k * F->alpha);
}

/* две кости от a к цели t: сустав выгибается в сторону (bx, by); до цели — не дальше, чем достаёт */
static void if_ik2(float ax, float ay, float tx, float ty, float l1, float l2, float bx, float by, float *jx, float *jy,
                   float *ex, float *ey) {
  float dx = tx - ax, dy = ty - ay, d = sqrtf(dx * dx + dy * dy), L = l1 + l2 - 0.5f, lo = fabsf(l1 - l2) + 0.5f;
  if (d < 1e-3f) dx = 0, dy = -1, d = 1;
  if (d > L) dx *= L / d, dy *= L / d, d = L;
  if (d < lo) dx *= lo / d, dy *= lo / d, d = lo;
  float a = (l1 * l1 - l2 * l2 + d * d) / (2 * d), h = sqrtf(fmaxf(l1 * l1 - a * a, 0));
  float ux = dx / d, uy = dy / d, nx = -uy, ny = ux;
  if (nx * bx + ny * by < 0) nx = -nx, ny = -ny;
  *jx = ax + ux * a + nx * h, *jy = ay + uy * a + ny * h;
  *ex = ax + dx, *ey = ay + dy;
}

/* поза Махораги по состоянию и времени: таз и присед, наклон туловища, куда тянутся кулаки и ступни;
   локти и колени — по двум костям */
static void if_maho_pose(const IfScene *s, IfMahoPose *P) {
  float t = s->mahoMt, lean = s->mahoLean, crouch = 0, bob = sinf(s->mahoT * 0.05f) * 0.8f;
  float hand[2][2] = {{-18, 36}, {18, 32}}, foot[2][2] = {{-13, 2}, {13, 2}};
  float bend[2][2] = {{-1, -0.3f}, {-1, -0.3f}}; /* локти — назад и вниз */
  int st = s->mahoSt, mv = s->mahoMv, air = s->mahoY > 0;
  if (st == MS_WALK) {
    for (int i = 0; i < 2; i++) {
      float p = s->mahoWalk + (i ? 0 : FA_PI);
      foot[i][0] += 15 * sinf(p), foot[i][1] += fmaxf(0, 7 * cosf(p));
      hand[i][0] -= 9 * sinf(p);
    }
    bob += fabsf(cosf(s->mahoWalk)) * 1.5f, lean += 4;
  } else if (st == MS_ATK && mv == MM_PUNCH) { /* прямой сверху вниз, с наклоном всем телом */
    float k1 = fa_smooth(fa_sat(t / 10)), k2 = fa_smooth(fa_sat((t - 10) / 5)), k3 = fa_smooth(fa_sat((t - 22) / 14));
    float reach = fa_clampf(fabsf(s->mahoTx - s->mahoX) / IF_MU, 24, 52);
    float hx = fa_mixf(fa_mixf(18, -8, k1), reach, k2), hy = fa_mixf(fa_mixf(32, 70, k1), 16, k2);
    hand[1][0] = fa_mixf(hx, 18, k3), hand[1][1] = fa_mixf(hy, 32, k3);
    hand[0][0] -= 8 * k1 * (1 - k3), hand[0][1] += 12 * k1 * (1 - k3);
    lean += fa_mixf(-7 * k1, 26, k2) * (1 - k3), crouch += 6 * k2 * (1 - k3);
    bend[1][0] = -0.5f, bend[1][1] = -1;
  } else if (st == MS_ATK && mv == MM_SLAM) { /* обе руки над головой — и в землю */
    float k1 = fa_smooth(fa_sat(t / 16)), k2 = fa_smooth(fa_sat((t - 16) / 4)), k3 = fa_smooth(fa_sat((t - 28) / 20));
    float reach = fa_clampf(fabsf(s->mahoTx - s->mahoX) / IF_MU, 22, 48);
    for (int i = 0; i < 2; i++) {
      float ox = i ? 5 : -5, ix = hand[i][0], iy = hand[i][1];
      float hx = fa_mixf(fa_mixf(ix, ox, k1), reach + ox, k2), hy = fa_mixf(fa_mixf(iy, 126, k1), 6, k2);
      hand[i][0] = fa_mixf(hx, ix, k3), hand[i][1] = fa_mixf(hy, iy, k3);
      bend[i][0] = -1, bend[i][1] = 0.2f;
    }
    lean += fa_mixf(-10 * k1, 24, k2) * (1 - k3), crouch += 8 * k2 * (1 - k3);
  } else if (st == MS_ATK && mv == MM_SWEEP) { /* размах из-за спины низом вперёд */
    float k1 = fa_smooth(fa_sat(t / 12)), u = fa_sat((t - 12) / 8), k3 = fa_smooth(fa_sat((t - 24) / 18));
    float reach = fa_clampf(fabsf(s->mahoTx - s->mahoX) / IF_MU + 8, 30, 54);
    float hx = fa_mixf(fa_mixf(18, -30, k1), reach, fa_smooth(u)), hy = fa_mixf(fa_mixf(32, 44, k1), 12, u);
    hand[1][0] = fa_mixf(hx, 18, k3), hand[1][1] = fa_mixf(hy, 32, k3);
    hand[0][0] -= 6 * k1 * (1 - k3), hand[0][1] += 10 * k1 * (1 - k3);
    lean += fa_mixf(-5 * k1, 28, u) * (1 - k3), crouch += 8 * u * (1 - k3);
    bend[1][0] = -0.3f, bend[1][1] = -1;
  } else if (st == MS_ATK && mv == MM_STOMP) {
    if (t < 12) { /* присел перед прыжком */
      float k = fa_smooth(fa_sat(t / 12));
      crouch += 10 * k, lean += 10 * k;
      for (int i = 0; i < 2; i++) hand[i][0] -= 10 * k, hand[i][1] -= 6 * k;
    } else if (air || t < 40) { /* в воздухе: ноги поджаты, руки вверх */
      for (int i = 0; i < 2; i++) foot[i][1] += 14, foot[i][0] += 4, hand[i][0] = i ? 14 : -12, hand[i][1] = 112;
      lean += 6;
      bend[0][0] = bend[1][0] = -1, bend[0][1] = bend[1][1] = 0.3f;
    } else { /* приземлился: присел, кулаки в землю, встаёт */
      float k = fa_smooth(fa_sat((t - 40) / 20));
      crouch += 12 * (1 - k), lean += 14 * (1 - k);
      for (int i = 0; i < 2; i++) hand[i][0] = fa_mixf(i ? 34 : 26, hand[i][0], k), hand[i][1] = fa_mixf(8, hand[i][1], k);
    }
  } else if (st == MS_ATK && mv == MM_UPPER) { /* апперкот: присел, кулак у земли — и снизу вверх, выпрямляясь */
    float k1 = fa_smooth(fa_sat(t / 10)), k2 = fa_smooth(fa_sat((t - 10) / 6)), k3 = fa_smooth(fa_sat((t - 24) / 14));
    float reach = fa_clampf(fabsf(s->mahoTx - s->mahoX) / IF_MU, 22, 40);
    float hx = fa_mixf(fa_mixf(18, 6, k1), reach, k2), hy = fa_mixf(fa_mixf(32, 8, k1), 104, k2);
    hand[1][0] = fa_mixf(hx, 18, k3), hand[1][1] = fa_mixf(hy, 32, k3);
    hand[0][0] -= 10 * k1 * (1 - k3), hand[0][1] += 16 * k2 * (1 - k3);
    lean += fa_mixf(16 * k1, -12, k2) * (1 - k3), crouch += fa_mixf(14 * k1, -2, k2) * (1 - k3);
    foot[1][0] += 6 * k2 * (1 - k3);
    bend[1][0] = -0.4f, bend[1][1] = -1;
  } else if (st == MS_ATK && mv == MM_CHARGE) { /* таран: пригнул голову, плечо вперёд, руки прижаты, бежит */
    float k1 = fa_smooth(fa_sat(t / 10)), k3 = fa_smooth(fa_sat((t - 36) / 20)), run = t >= 10 && t < 36 ? 1.0f : 0.0f;
    for (int i = 0; i < 2; i++) {
      float p = s->mahoWalk + (i ? 0 : FA_PI);
      foot[i][0] += run * 18 * sinf(p), foot[i][1] += run * fmaxf(0, 9 * cosf(p));
    }
    hand[1][0] = fa_mixf(fa_mixf(18, 22, k1), 18, k3), hand[1][1] = fa_mixf(fa_mixf(32, 50, k1), 32, k3);
    hand[0][0] = fa_mixf(fa_mixf(-18, 4, k1), -18, k3), hand[0][1] = fa_mixf(fa_mixf(36, 46, k1), 36, k3);
    lean += 30 * k1 * (1 - k3), crouch += 9 * k1 * (1 - k3);
    bend[1][0] = bend[0][0] = -1, bend[1][1] = bend[0][1] = -0.6f;
  } else if (st == MS_ATK && mv == MM_BLADE) { /* Меч изгнания: рука с лезвием занесена за плечо — и наискось вниз */
    float k1 = fa_smooth(fa_sat(t / 14)), u = fa_sat((t - 14) / 9), k3 = fa_smooth(fa_sat((t - 34) / 18));
    float reach = fa_clampf(fabsf(s->mahoTx - s->mahoX) / IF_MU - 28, 26, 46);
    float hx = fa_mixf(fa_mixf(18, -16, k1), reach, fa_smooth(u)), hy = fa_mixf(fa_mixf(32, 96, k1), 30, fa_smooth(u));
    hand[1][0] = fa_mixf(hx, 18, k3), hand[1][1] = fa_mixf(hy, 32, k3);
    hand[0][0] += 10 * k1 * (1 - k3), hand[0][1] += 14 * k1 * (1 - k3);
    lean += fa_mixf(-9 * k1, 24, u) * (1 - k3), crouch += 8 * u * (1 - k3);
    foot[1][0] += 8 * u * (1 - k3);
    bend[1][0] = -0.6f, bend[1][1] = fa_mixf(0.8f, -1, u);
  } else if (st == MS_GUARD) { /* закрылся: тёмные наручи крест-накрест перед лицом */
    float k = fa_smooth(fa_sat(t / 5)) * (1 - fa_smooth(fa_sat((t - 24) / 6)));
    hand[1][0] = fa_mixf(hand[1][0], 20, k), hand[1][1] = fa_mixf(hand[1][1], 76, k);
    hand[0][0] = fa_mixf(hand[0][0], 24, k), hand[0][1] = fa_mixf(hand[0][1], 66, k);
    lean -= 6 * k, crouch += 5 * k;
    bend[0][0] = bend[1][0] = 0.3f, bend[0][1] = bend[1][1] = -1;
  } else if (st == MS_HIT) {
    float k = 1 - fa_sat(t / 12);
    hand[1][1] += 14 * k, hand[0][1] += 10 * k;
  } else if (st == MS_STAGGER) {
    float k = 1 - fa_sat(t / 26);
    foot[1][0] += 10 * k, foot[0][0] -= 6 * k;
    for (int i = 0; i < 2; i++) hand[i][1] += 20 * k, hand[i][0] -= 8 * k;
  }
  /* таз; туловище, плечи и голова наклонены вокруг таза */
  float hy = 50 - crouch + bob, cr = cosf(lean * FA_RAD), sr = sinf(lean * FA_RAD);
  P->lean = lean, P->crouch = crouch;
  P->hip[0][0] = -7, P->hip[0][1] = hy, P->hip[1][0] = 7, P->hip[1][1] = hy;
  const float pts[5][2] = {{0, 28}, {0, 38}, {3, 48}, {-15, 34}, {13, 36}}; /* грудь, шея, голова, дальнее плечо, ближнее */
  float *out[5] = {P->chest, P->neck, P->head, P->sh[0], P->sh[1]};
  for (int i = 0; i < 5; i++) out[i][0] = pts[i][0] * cr + pts[i][1] * sr, out[i][1] = hy - pts[i][0] * sr + pts[i][1] * cr;
  for (int i = 0; i < 2; i++) {
    if_ik2(P->sh[i][0], P->sh[i][1], hand[i][0], hand[i][1], 30, 30, bend[i][0], bend[i][1], &P->el[i][0], &P->el[i][1],
           &P->hd[i][0], &P->hd[i][1]);
    if_ik2(P->hip[i][0], P->hip[i][1], foot[i][0], foot[i][1], 27, 26, 1, 0.1f, &P->kn[i][0], &P->kn[i][1], &P->ft[i][0],
           &P->ft[i][1]);
  }
}

static void if_rect(FCv *c, float x0, float y0, float x1, float y1, FCol top, FCol bot, float a);

/* цвета Махораги по аниме: белое тело, тёмные кольца на запястьях и щиколотках, тёмно-зелёная юбка с
   тёмным поясом, светло-серый кушак, бинты на правом предплечье; от удара белеет */
enum { MP_BODY, MP_DARK, MP_SKIRT, MP_BANDAGE, MP_SASH };
static void if_maho_part(FPart *p, int kind, float flash, float ow, float sh, float a) {
  static const FCol kLit[5] = {FC(236, 236, 240), FC(46, 48, 58), FC(64, 80, 78), FC(214, 214, 218), FC(196, 198, 204)};
  static const FCol kDk[5] = {FC(156, 158, 176), FC(22, 24, 30), FC(36, 46, 46), FC(150, 150, 160), FC(130, 132, 142)};
  FCol lit = kLit[kind], dk = kDk[kind], line = kind == MP_BODY || kind == MP_BANDAGE ? FC(40, 40, 54) : FC(14, 16, 20);
  if (flash > 0) lit = fa_mix(lit, FC(255, 255, 255), flash * 0.85f), dk = fa_mix(dk, FC(255, 255, 255), flash * 0.7f);
  fp_init(p, lit, dk, line, ow, sh);
  p->core = kind == MP_BODY ? 0.25f : 0;
  p->alpha = a;
}

/* колесо Махораги: золотое кольцо, ступица, восемь спиц с шарами на концах; fla — вспышка адаптации */
static void if_draw_wheel(FCv *c, float WX, float WY, float R, float M, float wheel, float fla, float a) {
  FCol gold = FC(222, 182, 72), dark = FC(120, 88, 30), hi = FC(255, 236, 160);
  if_glow(c, WX, WY, R * 2.0f, FC(255, 214, 120), (0.2f + 0.6f * fla) * a, 1);
  fv_ring(c, WX, WY, R, fmaxf(1.6f, 2.0f * M), dark, 0.95f * a);
  fv_ring(c, WX, WY, R, fmaxf(1.0f, 1.3f * M), gold, a);
  fv_ring(c, WX, WY, R, fmaxf(0.6f, 0.4f * M), hi, 0.7f * a);
  for (int i = 0; i < 8; i++) {
    float an = wheel + i * FA_PI * 0.25f, cx = cosf(an), sy = sinf(an);
    fv_line(c, WX, WY, WX + cx * R, WY + sy * R, fmaxf(0.7f, 0.55f * M), fmaxf(0.7f, 0.55f * M), dark, 0.85f * a);
    fv_line(c, WX + cx * R, WY + sy * R, WX + cx * R * 1.3f, WY + sy * R * 1.3f, fmaxf(0.8f, 0.8f * M), fmaxf(0.8f, 0.8f * M), gold, a);
    fv_disc(c, WX + cx * R * 1.36f, WY + sy * R * 1.36f, fmaxf(1.0f, 1.9f * M), dark, a);
    fv_disc(c, WX + cx * R * 1.36f, WY + sy * R * 1.36f, fmaxf(0.8f, 1.4f * M), gold, a);
  }
  fv_disc(c, WX, WY, fmaxf(1.2f, 3.0f * M), dark, a);
  fv_disc(c, WX, WY, fmaxf(1.0f, 2.2f * M), gold, a);
  if (fla > 0) if_glow(c, WX, WY, R * 1.3f, FC(255, 250, 220), fla * a, 1);
}

/* колесо над Сукуной: тень учится Бесконечности ещё до призыва; в Разрезе мира — выученное */
static void if_draw_shadow_wheel(IfScene *s) {
  IfFighter *K = &s->F[1];
  float a = 0;
  if (!s->maho && s->shadowT > 0) a = fa_sat(s->shadowT / 20) * fa_sat((80 - s->shadowT) / 8);
  if (K->st == FS_MOVE && K->mv == MV_S_WORLD) a = fmaxf(a, fa_sat(K->mt / 6) * fa_sat((kMoves[MV_S_WORLD].len - K->mt) / 8));
  a *= K->alpha;
  if (a <= 0.01f || s->mahoDie > 0) return;
  float hx, hy;
  if_joint(K, FJ_HEAD, &hx, &hy);
  float M = s->U * 0.8f; /* выше надписи над головой, чтобы та его не закрыла */
  if_draw_wheel(&s->cv, if_X(s, hx), if_Y(s, hy + 54), 12.0f * M, M, s->wheel, fa_sat(s->adaptT / 30), 0.9f * a);
}

/* рука Махораги: белое плечо и предплечье (правое — в бинтах), тёмное кольцо на запястье, кулак */
static void if_maho_arm(FCv *c, const IfMahoPose *P, int i, float X, float BY, float lift, float f, float M, float a, float fl,
                        float ow, float sh) {
  float sx = X + P->sh[i][0] * f * M, sy = BY - P->sh[i][1] * M + lift, ex = X + P->el[i][0] * f * M, ey = BY - P->el[i][1] * M + lift;
  float hx = X + P->hd[i][0] * f * M, hy = BY - P->hd[i][1] * M + lift, dx = hx - ex, dy = hy - ey, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-3f) l = 1e-3f, dx = 0, dy = 1;
  float ux = dx / l, uy = dy / l, nx = -uy, ny = ux;
  FPart p;
  if_maho_part(&p, MP_BODY, fl, ow, sh, a);
  fs_cap(fp_add(&p), sx, sy, ex, ey, 7.6f * M, 6.4f * M);                              /* плечо: бугор мышцы */
  if (i == 0) fs_cap(fp_add(&p), ex, ey, hx - ux * 3 * M, hy - uy * 3 * M, 6.2f * M, 5.0f * M);
  fs_ell(fp_add(&p), hx + ux * 2.5f * M, hy + uy * 2.5f * M, 6.0f * M, 5.2f * M, ux, uy); /* кулак */
  fp_draw(c, &p);
  if (i == 1) { /* правое предплечье — в бинтах, полосы наискось */
    if_maho_part(&p, MP_BANDAGE, fl, ow, sh, a);
    fs_cap(fp_add(&p), ex, ey, hx - ux * 3 * M, hy - uy * 3 * M, 6.4f * M, 5.2f * M);
    fp_draw(c, &p);
    for (int k = 1; k <= 4; k++) {
      float t = k * 0.19f, r = (6.4f - 1.2f * t) * M, mx = ex + dx * t, my = ey + dy * t;
      fv_line(c, mx - nx * r + ux * 1.6f * M, my - ny * r + uy * 1.6f * M, mx + nx * r - ux * 1.6f * M, my + ny * r - uy * 1.6f * M,
              fmaxf(0.5f, 0.35f * M), fmaxf(0.5f, 0.35f * M), FC(140, 140, 152), 0.8f * a);
    }
  }
  if_maho_part(&p, MP_DARK, 0, ow, sh * 0.5f, a); /* кольцо на запястье */
  fs_cap(fp_add(&p), hx - ux * 4.2f * M - nx * 5.6f * M, hy - uy * 4.2f * M - ny * 5.6f * M, hx - ux * 4.2f * M + nx * 5.6f * M,
         hy - uy * 4.2f * M + ny * 5.6f * M, 1.3f * M, 1.3f * M);
  fp_draw(c, &p);
}

/* нога: белое бедро и голень, тёмное кольцо на щиколотке, босая ступня */
static void if_maho_leg(FCv *c, const IfMahoPose *P, int i, float X, float BY, float lift, float f, float M, float a, float fl,
                        float ow, float sh) {
  float hx = X + P->hip[i][0] * f * M, hy = BY - P->hip[i][1] * M + lift, kx = X + P->kn[i][0] * f * M, ky = BY - P->kn[i][1] * M + lift;
  float fx = X + P->ft[i][0] * f * M, fy = BY - P->ft[i][1] * M + lift;
  FPart p;
  if_maho_part(&p, MP_BODY, fl, ow, sh, a);
  fs_cap(fp_add(&p), hx, hy, kx, ky, 8.6f * M, 7.0f * M);
  fs_cap(fp_add(&p), kx, ky, fx, fy - 2 * M, 6.8f * M, 4.8f * M);
  fs_ell(fp_add(&p), fx + f * 3.5f * M, fy + 0.6f * M, 8.0f * M, 3.6f * M, 1, 0);
  fp_draw(c, &p);
  float dx = fx - kx, dy = fy - ky, l = fmaxf(1e-3f, sqrtf(dx * dx + dy * dy)), nx = -dy / l, ny = dx / l, ax = fx - dx / l * 5.5f * M,
        ay = fy - dy / l * 5.5f * M;
  if_maho_part(&p, MP_DARK, 0, ow, sh * 0.5f, a);
  fs_cap(fp_add(&p), ax - nx * 5.2f * M, ay - ny * 5.2f * M, ax + nx * 5.2f * M, ay + ny * 5.2f * M, 1.3f * M, 1.3f * M);
  fp_draw(c, &p);
}

/* Махорага по аниме: белый мускулистый великан в два с половиной раза выше бойцов; голова-маска с
   крыльями по бокам, оскал, отросток с затылка; ожерелье; тёмно-зелёная юбка до колен с тёмным
   поясом и светлым кушаком; тёмные кольца на запястьях и щиколотках, правое предплечье в бинтах,
   в правой руке — длинный тонкий меч (Меч изгнания: светится, когда рубит); золотое колесо над
   головой. Встаёт из тени, белеет от удара, над колесом — полоска здоровья; рассыпается, когда разбит */
static void if_draw_maho(IfScene *s) {
  float a = s->maho ? 1.0f : (s->mahoDie > 0 ? fa_sat(s->mahoDie / 40) : 0);
  if (a <= 0.01f) return;
  FCv *c = &s->cv;
  float U = s->U, M = U * IF_MU, f = s->mahoF, rise = fa_smooth(fa_sat(s->mahoT / 40)), fl = fa_sat(s->mahoFlash);
  float X = if_X(s, s->mahoX), GY = if_Y(s, s->mahoFl), BY = if_Y(s, s->mahoFl + s->mahoY), lift = (1 - rise) * 130 * M;
  if (!s->maho) lift = -(1 - a) * 16 * M; /* разбитый всплывает и тает */
  int y1 = c->y1;
  if (rise < 1 && s->maho && (int)GY + 1 < y1) c->y1 = (int)GY + 1; /* из-под земли: ниже пола не видно */
  float ow = fmaxf(1.2f, U * 0.7f), sh = M * 2.6f;
  IfMahoPose P;
  if_maho_pose(s, &P);
#define GX(x) (X + (x) * f * M)
#define GYY(y) (BY - (y) * M + lift)
  if_soft_ell(c, X, GY, M * (30 + 6 * rise), M * 4.2f, FC(10, 6, 20), 0.5f * a * fa_sat(1 - s->mahoY / 120)); /* тень на земле */
  FPart p;
  float lr = P.lean * FA_RAD, cl = cosf(lr), sl = sinf(lr), hdx = P.head[0] + 7 * sl, hdy = P.head[1] + 7 * cl; /* голова над плечами */
  /* точка головы со сдвигом (x — вперёд, y — вверх), повёрнутым вместе с наклоном туловища */
#define HX(u, v) GX(hdx + (u) * cl + (v) * sl)
#define HY(u, v) GYY(hdy - (u) * sl + (v) * cl)
  { /* отросток с затылка: белый, сужается, за спиной загибается вниз, на конце — тонкий тёмный хлыст */
    float sw = sinf(s->mahoT * 0.04f) * 2.0f;
    float pts[5][2] = {{-6, 6}, {-18, 10}, {-30, 6 + sw}, {-38, -4 + sw}, {-40, -14 + sw}};
    if_maho_part(&p, MP_BODY, fl, ow, sh, a);
    for (int k = 0; k < 4; k++)
      fs_cap(fp_add(&p), HX(pts[k][0], pts[k][1]), HY(pts[k][0], pts[k][1]), HX(pts[k + 1][0], pts[k + 1][1]),
             HY(pts[k + 1][0], pts[k + 1][1]), (4.6f - k * 0.9f) * M, (3.7f - k * 0.9f) * M);
    fs_ell(fp_add(&p), HX(-40, -16 + sw), HY(-40, -16 + sw), 2.6f * M, 2.0f * M, 1, 0);
    fp_draw(c, &p);
    fv_line(c, HX(-40, -17 + sw), HY(-40, -17 + sw), HX(-36, -30 + sw), HY(-36, -30 + sw), fmaxf(0.6f, 0.45f * M), fmaxf(0.5f, 0.25f * M),
            FC(40, 40, 50), 0.9f * a);
  }
  if_maho_arm(c, &P, 0, X, BY, lift, f, M, a, fl, ow, sh); /* дальняя рука */
  if_maho_leg(c, &P, 0, X, BY, lift, f, M, a, fl, ow, sh); /* дальняя нога */
  if_maho_leg(c, &P, 1, X, BY, lift, f, M, a, fl, ow, sh); /* ближняя нога: бёдра закроет юбка */
  /* туловище: торс клином, плечи, шея */
  if_maho_part(&p, MP_BODY, fl, ow, sh, a);
  fs_cap(fp_add(&p), GX(0), GYY(P.hip[0][1] + 4), GX(P.chest[0]), GYY(P.chest[1]), 10.0f * M, 16.0f * M);
  fs_cap(fp_add(&p), GX(P.sh[0][0]), GYY(P.sh[0][1]), GX(P.sh[1][0]), GYY(P.sh[1][1]), 9.0f * M, 9.0f * M);
  fs_cap(fp_add(&p), GX(P.neck[0]), GYY(P.neck[1]), GX(hdx), GYY(hdy - 4), 6.0f * M, 5.0f * M); /* толстая шея */
  fp_draw(c, &p);
  { /* мышцы груди и пресса — тонкие серые линии */
    float cx = P.chest[0], cy = P.chest[1];
    FCol ln = fa_mix(FC(150, 152, 170), FC(255, 255, 255), fl * 0.8f);
    fv_line(c, GX(cx - 8), GYY(cy - 4), GX(cx + 9), GYY(cy - 4), fmaxf(0.5f, 0.3f * M), fmaxf(0.5f, 0.3f * M), ln, 0.8f * a);
    for (int k = 0; k < 3; k++)
      fv_line(c, GX(cx - 5 + k * 1.2f), GYY(cy - 10 - k * 6), GX(cx + 6 + k * 0.8f), GYY(cy - 10 - k * 6), fmaxf(0.5f, 0.25f * M),
              fmaxf(0.5f, 0.25f * M), ln, 0.6f * a);
    fv_line(c, GX(cx - 8), GYY(cy + 7), GX(cx + 1), GYY(cy + 3), fmaxf(0.5f, 0.3f * M), fmaxf(0.5f, 0.3f * M), FC(30, 32, 40), a);
    fv_line(c, GX(cx + 1), GYY(cy + 3), GX(cx + 10), GYY(cy + 7), fmaxf(0.5f, 0.3f * M), fmaxf(0.5f, 0.3f * M), FC(30, 32, 40), a);
    for (int k = 0; k < 5; k++) { /* ожерелье: шнур с тёмными бусинами по ключицам, в середине — подвеска */
      float u = (k - 2) * 3.4f, v = fabsf(k - 2.0f) * 1.6f;
      fv_disc(c, GX(cx + u + 1), GYY(cy + 3 + v), fmaxf(0.6f, 0.65f * M), FC(30, 32, 40), a);
    }
    fv_line(c, GX(cx + 1), GYY(cy + 4), GX(cx + 1), GYY(cy - 1), fmaxf(0.6f, 0.7f * M), fmaxf(0.6f, 0.7f * M), FC(30, 32, 40), a);
  }
  { /* юбка до колен: тёмно-зелёная, с пояса расширяется книзу, по ней — складки */
    float hy = P.hip[0][1], k0 = fminf(P.kn[0][0], P.kn[1][0]), k1 = fmaxf(P.kn[0][0], P.kn[1][0]);
    float ky = fminf(P.kn[0][1], P.kn[1][1]) - 3;
    float sk[10] = {GX(-12), GYY(hy + 4), GX(12), GYY(hy + 4), GX(k1 + 11), GYY(ky + 2), GX((k0 + k1) * 0.5f + 2), GYY(ky - 1),
                    GX(k0 - 11), GYY(ky + 2)};
    if_maho_part(&p, MP_SKIRT, fl * 0.4f, ow, sh * 0.7f, a);
    fs_poly(fp_add(&p), sk, 5);
    fp_draw(c, &p);
    for (int k = 0; k < 4; k++) { /* складки */
      float u = -7 + k * 5, b = k0 - 6 + (k1 - k0 + 12) * (k + 0.5f) / 4;
      fv_line(c, GX(u), GYY(hy), GX(b), GYY(ky + 3), fmaxf(0.5f, 0.3f * M), fmaxf(0.5f, 0.4f * M), FC(24, 32, 32), 0.7f * a);
    }
    /* тёмный пояс и светлый кушак: узел спереди, концы свисают */
    if_maho_part(&p, MP_DARK, 0, ow, sh * 0.5f, a);
    fs_cap(fp_add(&p), GX(-12), GYY(hy + 5), GX(12), GYY(hy + 5), 2.8f * M, 2.8f * M);
    fp_draw(c, &p);
    float sa[8] = {GX(3), GYY(hy + 3), GX(10), GYY(hy + 3), GX(9), GYY(hy - 22), GX(4), GYY(hy - 20)};
    if_maho_part(&p, MP_SASH, fl * 0.6f, ow, sh * 0.6f, a);
    fs_poly(fp_add(&p), sa, 4);
    fs_ell(fp_add(&p), GX(7), GYY(hy + 2), 4.0f * M, 3.0f * M, 1, 0);
    fs_cap(fp_add(&p), GX(-11), GYY(hy + 2), GX(4), GYY(hy + 2), 1.6f * M, 1.6f * M);
    fp_draw(c, &p);
  }
  { /* голова-маска: белая, крылья по бокам назад и вверх, оскал */
    float w1[8] = {HX(5, 4), HY(5, 4), HX(-4, 15), HY(-4, 15), HX(-21, 22), HY(-21, 22), HX(-8, 3), HY(-8, 3)};
    float w2[8] = {HX(3, 1), HY(3, 1), HX(-10, 6), HY(-10, 6), HX(-24, 6), HY(-24, 6), HX(-6, -3), HY(-6, -3)};
    if_maho_part(&p, MP_SASH, fl, ow, sh, a); /* дальнее крыло — в тени */
    fs_poly(fp_add(&p), w2, 4);
    fp_draw(c, &p);
    if_maho_part(&p, MP_BODY, fl, ow, sh, a);
    fs_ell(fp_add(&p), HX(1, 1), HY(1, 1), 8.6f * M, 9.8f * M, 1, 0);
    fs_ell(fp_add(&p), HX(5.5f, -4), HY(5.5f, -4), 5.4f * M, 4.4f * M, 1, 0); /* морда */
    fs_poly(fp_add(&p), w1, 4);
    fp_draw(c, &p);
    FCol ln = FC(60, 62, 78);
    fv_line(c, HX(9, 2), HY(9, 2), HX(-2, 3), HY(-2, 3), fmaxf(0.7f, 1.0f * M), fmaxf(0.6f, 0.7f * M), FC(70, 72, 88), 0.9f * a); /* край крыла над глазами */
    fv_line(c, HX(6, 4), HY(6, 4), HX(-14, 14), HY(-14, 14), fmaxf(0.5f, 0.35f * M), fmaxf(0.5f, 0.3f * M), ln, 0.8f * a); /* перо */
    fv_line(c, HX(4, 1), HY(4, 1), HX(-16, 4), HY(-16, 4), fmaxf(0.5f, 0.35f * M), fmaxf(0.5f, 0.3f * M), ln, 0.8f * a);
    fv_line(c, HX(2, -5), HY(2, -5), HX(9.5f, -5), HY(9.5f, -5), fmaxf(0.8f, 1.6f * M), fmaxf(0.8f, 1.4f * M), FC(30, 30, 40), a); /* оскал */
    for (int k = 0; k < 4; k++)
      fv_line(c, HX(3.2f + k * 1.8f, -4.0f), HY(3.2f + k * 1.8f, -4.0f), HX(3.2f + k * 1.8f, -6.0f), HY(3.2f + k * 1.8f, -6.0f),
              fmaxf(0.5f, 0.45f * M), fmaxf(0.5f, 0.45f * M), FC(250, 250, 250), a);
  }
  if_maho_arm(c, &P, 1, X, BY, lift, f, M, a, fl, ow, sh); /* ближняя рука с мечом */
  { /* меч в правой руке: длинный тонкий клинок по линии предплечья, маленькая гарда; в Мече изгнания светится */
    float ex = GX(P.el[1][0]), ey = GYY(P.el[1][1]), hx = GX(P.hd[1][0]), hy = GYY(P.hd[1][1]);
    float ux = hx - ex, uy = hy - ey, l = sqrtf(ux * ux + uy * uy);
    if (l > 0.01f) {
      ux /= l, uy /= l;
      float nx = -uy, ny = ux, L = 44 * M, bx = hx + ux * 3 * M, by = hy + uy * 3 * M, tx = bx + ux * L, ty = by + uy * L;
      float g = 0;
      if (s->maho && s->mahoSt == MS_ATK && s->mahoMv == MM_BLADE)
        g = fa_sat((s->mahoMt - 2) / 6) * fa_sat((kMahoLen[MM_BLADE] - 6 - s->mahoMt) / 8);
      if (g > 0.01f)
        for (int i = 1; i <= 3; i++)
          if_glow(c, bx + ux * L * i * 0.27f, by + uy * L * i * 0.27f, 9 * M, FC(255, 226, 150), 0.45f * g * a, 1);
      float bl[8] = {bx + nx * 1.5f * M, by + ny * 1.5f * M, tx + nx * 0.2f * M, ty + ny * 0.2f * M, tx - nx * 0.6f * M, ty - ny * 0.6f * M,
                     bx - nx * 1.3f * M, by - ny * 1.3f * M};
      fp_init(&p, fa_mix(FC(88, 92, 104), FC(255, 248, 226), g), fa_mix(FC(44, 46, 56), FC(236, 210, 150), g), FC(18, 18, 24), ow,
              sh * 0.4f), p.alpha = a;
      fs_poly(fp_add(&p), bl, 4);
      fs_cap(fp_add(&p), bx - nx * 3.4f * M, by - ny * 3.4f * M, bx + nx * 3.4f * M, by + ny * 3.4f * M, 1.1f * M, 1.1f * M); /* гарда */
      fp_draw(c, &p);
      fv_line(c, bx + nx * 0.6f * M, by + ny * 0.6f * M, tx, ty, fmaxf(0.5f, 0.35f * M), fmaxf(0.4f, 0.2f * M),
              fa_mix(FC(200, 206, 220), FC(255, 255, 255), g), 0.9f * a); /* блик по лезвию */
    }
  }
  /* колесо над головой */
  float WX = GX(hdx - 1), WY = GYY(hdy + 28), R = 12.0f * M;
  if_draw_wheel(c, WX, WY, R, M, s->wheel, fa_sat(s->adaptT / 30), a);
  if (if_maho_on(s)) { /* здоровье великана */
    float bw = 44 * M, bh = fmaxf(2, 1.3f * M), x0 = WX - bw * 0.5f, y0 = WY - R * 1.5f - bh - 3, k = fa_sat(s->mahoHp / IF_MAHOHP);
    if_rect(c, x0 - 1, y0 - 1, x0 + bw + 1, y0 + bh + 1, FC(14, 12, 22), FC(14, 12, 22), 0.7f * a);
    if_rect(c, x0, y0, x0 + bw * k, y0 + bh, FC(214, 176, 255), FC(132, 84, 222), a);
  }
#undef HX
#undef HY
#undef GX
#undef GYY
  c->y1 = y1;
}

/* ===================== территория: картина ===================== */

static unsigned if_hash(unsigned x, unsigned y) {
  unsigned h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float if_vnoise(float x, float y, unsigned seed) { /* плавный шум: облака, туманности */
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float fx = x - xi, fy = y - yi;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = (if_hash(xi + seed, yi) & 1023) / 1023.0f, b = (if_hash(xi + 1 + seed, yi) & 1023) / 1023.0f;
  float c = (if_hash(xi + seed, yi + 1) & 1023) / 1023.0f, d = (if_hash(xi + 1 + seed, yi + 1) & 1023) / 1023.0f;
  return fa_mixf(fa_mixf(a, b, fx), fa_mixf(c, d, fx), fy);
}

/* Безграничная пустота: бездна со звёздами, свет бесконечности в глубине */
static FCol if_void_px(const IfScene *s, int x, int y, float u, float v, float rq) {
  float rx = s->bdW * 0.5f, ry = (float)s->bdH, R = fminf(rx, ry);
  float dx = u * rx, dy = (v - 0.45f) * ry, d = sqrtf(dx * dx + dy * dy) / R;
  FCol col = fa_mix(FC(16, 24, 70), FC(3, 4, 14), fa_sat(rq * 1.15f - 0.1f));
  float n = if_vnoise(x / 70.0f, y / 70.0f, 11u) * 0.6f + if_vnoise(x / 23.0f, y / 23.0f, 29u) * 0.4f;
  col = fa_mix(col, FC(70, 44, 140), fa_sat((n - 0.52f) * 2.4f) * 0.5f * (1 - d * 0.4f));
  col = fa_mix(col, FC(20, 100, 140), fa_sat((0.46f - n) * 2.4f) * 0.35f);
  float g = expf(-d * 6.5f);
  col.r += 200 * g, col.g += 225 * g, col.b += 255 * g;
  float th = (atan2f(dy, dx) + FA_PI) * (48.0f / (2 * FA_PI)), fr = th - floorf(th);
  float sb = (if_hash((unsigned)(int)floorf(th), 7u) & 255) / 255.0f, pr = cosf((fr - 0.5f) * FA_PI);
  pr *= pr, pr *= pr, pr *= pr;
  float st = sb * sb * pr * fa_sat((d - 0.05f) * 5) * fa_sat((1.05f - d) * 2);
  col.r += 110 * st, col.g += 160 * st, col.b += 255 * st;
  unsigned h = if_hash((unsigned)x / 5u, (unsigned)y / 5u); /* звёзды: одна на клетку 5×5, не во всякой */
  if ((h & 255) < 22) {
    float sx = (x / 5) * 5 + 1.0f + ((h >> 8) & 3), sy = (y / 5) * 5 + 1.0f + ((h >> 10) & 3);
    float dd = hypotf(x + 0.5f - sx, y + 0.5f - sy), b = (0.35f + ((h >> 12) & 255) / 400.0f) * fa_sat(1.3f - dd);
    col.r += 255 * b, col.g += 255 * b, col.b += 255 * b;
  }
  float rim = fa_sat((rq - 0.9f) / 0.1f);
  rim *= rim;
  col.r += 130 * rim, col.g += 195 * rim, col.b += 255 * rim;
  col.r = fminf(col.r, 255), col.g = fminf(col.g, 255), col.b = fminf(col.b, 255);
  return col;
}

/* Святилище злобы: кровавое небо, тучи, кровь под ногами; барьера нет — края тают */
static FCol if_shrine_px(int x, int y, float u, float v, float rq) {
  FCol col = v > 0.45f ? fa_mix(FC(100, 8, 18), FC(18, 2, 6), (v - 0.45f) / 0.55f)
                       : fa_mix(FC(190, 30, 30), FC(100, 8, 18), fa_sat((v - 0.08f) / 0.37f));
  float n = if_vnoise(x / 140.0f, y / 26.0f, 5u) * 0.65f + if_vnoise(x / 47.0f, y / 11.0f, 17u) * 0.35f;
  col = fa_dim(col, 0.7f + 0.48f * n);
  float gx = u * 1.4f, gv = (v - 0.12f) * 2.2f, g = expf(-(gx * gx + gv * gv) * 2.5f);
  col = fa_mix(col, FC(255, 96, 64), g * 0.45f);
  if (v < 0.085f) {
    float rip = sinf(x * 0.09f + (if_hash((unsigned)y, 3u) & 15) * 0.4f) * 0.5f + 0.5f;
    col = fa_mix(FC(64, 4, 10), FC(160, 20, 26), rip * rip * 0.55f * fa_sat(v * 14));
  }
  col = fa_dim(col, 1 - 0.7f * fa_sat((rq - 0.62f) / 0.38f));
  return col;
}

static void if_dom_rows(IfScene *s, unsigned char *buf, int kind, int y0, int y1) {
  int W = s->bdW, H = s->bdH;
  float rx = W * 0.5f, ry = (float)H, Rm = fminf(rx, ry);
  for (int y = y0; y < y1; y++) {
    unsigned char *row = buf + (size_t)y * W * 4;
    float v = (H - (y + 0.5f)) / ry;
    for (int x = 0; x < W; x++) {
      float u = (x + 0.5f - rx) / rx, rq = sqrtf(u * u + v * v), edge = (1.0f - rq) * Rm;
      unsigned char *d = row + x * 4;
      if (edge <= -0.5f) {
        d[0] = d[1] = d[2] = d[3] = 0;
        continue;
      }
      float cov = fa_sat(edge + 0.5f);
      FCol col = kind == DOM_VOID ? if_void_px(s, x, y, u, v, rq) : if_shrine_px(x, y, u, v, rq);
      d[0] = (unsigned char)(col.b * cov + 0.5f), d[1] = (unsigned char)(col.g * cov + 0.5f);
      d[2] = (unsigned char)(col.r * cov + 0.5f), d[3] = (unsigned char)(255 * cov + 0.5f);
    }
  }
}

/* храм: ярусы с загнутыми крышами, пасть с зубами, черепа у подножия */
static void if_shrine_roof(FCv *bc, float cx, float gy, float SW, float SH, float y0, float hw, float h, float ow) {
  FPart p;
  fp_init(&p, FC(30, 8, 12), FC(14, 3, 6), FC(160, 28, 36), ow, SH * 0.012f);
  float pts[12] = {cx + (-hw - 0.1f) * SW, gy - (y0 + 0.05f) * SH, cx + (-hw + 0.06f) * SW, gy - y0 * SH,
                   cx + (hw - 0.06f) * SW, gy - y0 * SH, cx + (hw + 0.1f) * SW, gy - (y0 + 0.05f) * SH,
                   cx + hw * 0.6f * SW, gy - (y0 + h) * SH, cx - hw * 0.6f * SW, gy - (y0 + h) * SH};
  fs_poly(fp_add(&p), pts, 6);
  fp_draw(bc, &p);
  fv_line(bc, cx + (-hw + 0.06f) * SW, gy - y0 * SH - 1, cx + (hw - 0.06f) * SW, gy - y0 * SH - 1, ow, ow,
          FC(120, 22, 28), 1);
}
static void if_shrine_box(FCv *bc, float cx, float gy, float SW, float SH, float x0, float y0, float x1, float y1, float ow) {
  FPart p;
  fp_init(&p, FC(36, 10, 14), FC(16, 4, 7), FC(150, 26, 34), ow, SH * 0.012f);
  float pts[8] = {cx + x0 * SW, gy - y0 * SH, cx + x1 * SW, gy - y0 * SH, cx + x1 * SW, gy - y1 * SH, cx + x0 * SW, gy - y1 * SH};
  fs_poly(fp_add(&p), pts, 4);
  fp_draw(bc, &p);
}
static void if_skull(FCv *bc, float x, float y, float r, float ow, int horns) {
  FPart p;
  fp_init(&p, FC(218, 204, 184), FC(150, 128, 112), FC(64, 20, 24), ow, r * 0.35f);
  fs_ell(fp_add(&p), x, y - r * 1.05f, r, r * 0.92f, 1, 0);
  fs_cap(fp_add(&p), x - r * 0.35f, y - r * 0.4f, x + r * 0.35f, y - r * 0.4f, r * 0.42f, r * 0.42f);
  if (horns) {
    fs_cap(fp_add(&p), x - r * 0.8f, y - r * 1.5f, x - r * 1.7f, y - r * 2.3f, r * 0.28f, r * 0.08f);
    fs_cap(fp_add(&p), x + r * 0.8f, y - r * 1.5f, x + r * 1.7f, y - r * 2.3f, r * 0.28f, r * 0.08f);
  }
  fp_draw(bc, &p);
  fv_disc(bc, x - r * 0.38f, y - r * 1.0f, r * 0.26f, FC(30, 6, 10), 1);
  fv_disc(bc, x + r * 0.38f, y - r * 1.0f, r * 0.26f, FC(30, 6, 10), 1);
  fv_line(bc, x, y - r * 0.72f, x, y - r * 0.55f, r * 0.12f, r * 0.05f, FC(40, 10, 14), 1);
}
static void if_shrine_shapes(IfScene *s, unsigned char *buf) {
  FCv bc;
  memset(&bc, 0, sizeof(bc));
  bc.px = buf, bc.w = s->bdW, bc.h = s->bdH, bc.x1 = s->bdW, bc.y1 = s->bdH;
  fv_reset_touch(&bc);
  float W = (float)s->bdW, H = (float)s->bdH, cx = W * 0.5f, gy = H * (1 - 0.06f);
  float SH = H * 0.72f, SW = fminf(W * 0.28f, SH * 0.6f), ow = fmaxf(1.2f, s->U * 0.5f);
  FPart p;
  fp_init(&p, FC(36, 10, 14), FC(16, 4, 7), FC(150, 26, 34), ow, SH * 0.012f);
  float st1[8] = {cx - 1.18f * SW, gy, cx + 1.18f * SW, gy, cx + 1.02f * SW, gy - 0.05f * SH, cx - 1.02f * SW, gy - 0.05f * SH};
  float st2[8] = {cx - 0.95f * SW, gy - 0.05f * SH, cx + 0.95f * SW, gy - 0.05f * SH, cx + 0.86f * SW, gy - 0.09f * SH,
                  cx - 0.86f * SW, gy - 0.09f * SH};
  fs_poly(fp_add(&p), st1, 4);
  fs_poly(fp_add(&p), st2, 4);
  fp_draw(&bc, &p);
  if_shrine_box(&bc, cx, gy, SW, SH, -0.72f, 0.09f, 0.72f, 0.42f, ow);
  for (int i = 0; i < 4; i++) {
    static const float kPil[4] = {-0.62f, -0.44f, 0.44f, 0.62f};
    fv_line(&bc, cx + kPil[i] * SW, gy - 0.1f * SH, cx + kPil[i] * SW, gy - 0.41f * SH, SW * 0.035f, SW * 0.035f,
            FC(78, 18, 24), 1);
  }
  /* пасть */
  fp_init(&p, FC(10, 0, 3), FC(4, 0, 1), FC(130, 12, 22), ow, 0);
  fs_ell(fp_add(&p), cx, gy - 0.25f * SH, SW * 0.3f, SH * 0.09f, 1, 0);
  fp_draw(&bc, &p);
  for (int row = 0; row < 2; row++) {
    int n = row ? 6 : 7;
    for (int i = 0; i < n; i++) {
      float t = (i + 0.5f) / n, x = cx + (t - 0.5f) * 2 * SW * 0.27f, edge = sqrtf(fa_sat(1 - (2 * t - 1) * (2 * t - 1)));
      float yb = gy - 0.25f * SH + (row ? 1 : -1) * SH * 0.085f * edge, len = SH * 0.05f * (0.6f + 0.4f * edge);
      float w = SW * 0.024f;
      fp_init(&p, FC(236, 226, 206), FC(170, 150, 130), FC(40, 8, 12), ow * 0.6f, 0);
      float tp[6] = {x - w, yb, x + w, yb, x, yb + (row ? -len : len)};
      fs_poly(fp_add(&p), tp, 3);
      fp_draw(&bc, &p);
    }
  }
  if_shrine_roof(&bc, cx, gy, SW, SH, 0.42f, 1.0f, 0.11f, ow);
  if_shrine_box(&bc, cx, gy, SW, SH, -0.46f, 0.53f, 0.46f, 0.66f, ow);
  if_shrine_roof(&bc, cx, gy, SW, SH, 0.66f, 0.76f, 0.1f, ow);
  if_shrine_box(&bc, cx, gy, SW, SH, -0.17f, 0.76f, 0.17f, 0.84f, ow);
  if_shrine_roof(&bc, cx, gy, SW, SH, 0.84f, 0.36f, 0.07f, ow);
  fv_line(&bc, cx, gy - 0.9f * SH, cx, gy - 1.0f * SH, SW * 0.03f, SW * 0.012f, FC(150, 26, 34), 1);
  fv_disc(&bc, cx, gy - 0.93f * SH, SW * 0.045f, FC(150, 26, 34), 1);
  /* черепа грудами по бокам */
  static const float kSk[][3] = {{1.2f, 0, 1}, {1.42f, 0, 0}, {1.64f, 0, 1}, {1.31f, 0.055f, 0}, {1.53f, 0.055f, 0},
                                 {1.42f, 0.11f, 1}, {0.9f, 0, 0}};
  for (int side = -1; side <= 1; side += 2)
    for (int i = 0; i < 7; i++) {
      float r = SH * 0.03f;
      if_skull(&bc, cx + side * kSk[i][0] * SW, gy - kSk[i][1] * SH, r, ow * 0.7f, (int)kSk[i][2]);
    }
}

/* размер и место купола, память под фон */
static void if_dom_prepare(IfScene *s, int kind) {
  float U = s->U, fy = s->gy - s->domFl * U; /* экранный y пола под куполом */
  float rxU = fminf((s->xMax - s->xMin) * 0.5f + 16.0f, 330.0f), ryU = fminf(fy / U * 0.96f, 270.0f);
  float lo = s->xMin - 16 + rxU * 0.7f, hi = s->xMax + 16 - rxU * 0.7f;
  s->domX = lo < hi ? fa_clampf(s->domX, lo, hi) : (s->xMin + s->xMax) * 0.5f;
  s->domRx = rxU, s->domRy = ryU;
  s->bdW = (int)(rxU * U) * 2, s->bdH = (int)(ryU * U);
  s->bdX = (int)lroundf(s->domX * U) - s->bdW / 2, s->bdY = (int)lroundf(fy) - s->bdH;
  free(s->bd), free(s->bd2);
  s->bd = (unsigned char *)malloc((size_t)s->bdW * s->bdH * 4);
  s->bd2 = kind == DOM_CLASH ? (unsigned char *)malloc((size_t)s->bdW * s->bdH * 4) : NULL;
  if (kind == DOM_CLASH && !s->bd2) free(s->bd), s->bd = NULL;
  s->bdRows = 0;
  s->bdKind = kind;
}

/* фон строится понемногу — с первого жеста знака и пока территория складывается;
   к раскрытию достраивается */
static void if_dom_build(IfScene *s) {
  int kind = s->dom;
  for (int i = 0; i < 2 && !kind; i++) {
    const IfFighter *F = &s->F[i];
    if (F->st == FS_MOVE && F->mv == MV_G_VOID) kind = s->act == SC_CLASH ? DOM_CLASH : DOM_VOID;
    if (F->st == FS_MOVE && F->mv == MV_S_SHRINE) kind = s->act == SC_CLASH ? DOM_CLASH : DOM_SHRINE;
  }
  if (!kind) {
    free(s->bd), free(s->bd2);
    s->bd = s->bd2 = NULL;
    return;
  }
  if (!s->bd || s->bdKind != kind) if_dom_prepare(s, kind);
  if (!s->bd || s->bdRows > s->bdH) return;
  int chunk = !s->dom || s->domPhase == DP_CAST ? (s->bdH + 39) / 40 : s->bdH;
  int y1 = s->bdRows + chunk > s->bdH ? s->bdH : s->bdRows + chunk;
  if_dom_rows(s, s->bd, kind == DOM_CLASH ? DOM_VOID : kind, s->bdRows, y1); /* при столкновении bd — Пустота, bd2 — Святилище */
  if (s->bd2) if_dom_rows(s, s->bd2, DOM_SHRINE, s->bdRows, y1);
  s->bdRows = y1;
  if (s->bdRows >= s->bdH) {
    if (s->bdKind == DOM_SHRINE) if_shrine_shapes(s, s->bd);
    if (s->bd2) if_shrine_shapes(s, s->bd2);
    s->bdRows = s->bdH + 1;
  }
}

/* фон на холст: раскрывается от середины полуэллипсом */
static void if_dom_blit(IfScene *s) {
  if (!s->bd || s->bdRows <= s->bdH || s->domOpen <= 0.001f) return;
  FCv *c = &s->cv;
  int ox = s->bdX + (int)lroundf(s->shx), oy = s->bdY + (int)lroundf(s->shy);
  float cx = ox + s->bdW * 0.5f, cy = (float)(oy + s->bdH), rx = s->bdW * 0.5f * s->domOpen, ry = s->bdH * s->domOpen;
  int X0, Y0, X1, Y1, clash = s->dom == DOM_CLASH && s->bd2;
  float sgn = s->F[0].x < s->F[1].x ? 1.0f : -1.0f; /* с какой стороны Годжо: там Пустота */
  if (!fv_clip(c, cx - rx, cy - ry, cx + rx, cy, &X0, &Y0, &X1, &Y1)) return;
  for (int y = Y0; y < Y1; y++) {
    float v = (cy - (y + 0.5f)) / ry;
    if (v < 0 || v >= 1) continue;
    float half = rx * sqrtf(1 - v * v), xa = cx - half, xb = cx + half;
    int a = (int)floorf(xa), b = (int)ceilf(xb);
    if (a < X0) a = X0;
    if (b > X1) b = X1;
    const unsigned char *src = s->bd + (size_t)(y - oy) * s->bdW * 4;
    const unsigned char *src2 = clash ? s->bd2 + (size_t)(y - oy) * s->bdW * 4 : src;
    float bx = clash ? if_X(s, if_clash_x(s, (cy - (y + 0.5f)) / s->bdH)) : 0;
    unsigned char *dst = c->px + (size_t)y * c->w * 4;
    for (int x = a; x < b; x++) {
      int sx = x - ox;
      if (sx < 0 || sx >= s->bdW) continue;
      float cov = fa_sat(fminf(x + 1 - xa, xb - x));
      const unsigned char *p = (clash && (x + 0.5f - bx) * sgn > 0 ? src2 : src) + sx * 4;
      unsigned char *d = dst + x * 4;
      if (cov >= 1) {
        memcpy(d, p, 4);
      } else {
        float k = 1 - cov * p[3] / 255.0f;
        for (int q = 0; q < 4; q++) d[q] = (unsigned char)(p[q] * cov + d[q] * k + 0.5f);
      }
    }
  }
  if (s->domPhase == DP_OPEN || s->domPhase == DP_CLOSE) { /* кромка раскрытия */
    FCol col = s->dom == DOM_VOID ? FC(190, 225, 255) : (clash ? FC(240, 230, 255) : FC(255, 70, 70));
    if_arc(c, cx, cy, rx, ry, FA_PI, 2 * FA_PI, fmaxf(1.5f, s->U * 1.2f), col, 0.9f, 0);
  }
}

/* что движется в территории: за бойцами (back) и поверх (front) */
static void if_dom_fx(IfScene *s, int front) {
  if (!s->bd || s->domOpen < 0.3f) return;
  FCv *c = &s->cv;
  float U = s->U, a = fa_sat((s->domOpen - 0.3f) / 0.7f), T = s->dom == DOM_VOID ? s->domT : s->domT;
  float cx = s->bdX + s->bdW * 0.5f + s->shx, gy = s->bdY + s->bdH + s->shy, rx = s->bdW * 0.5f * s->domOpen, ry = s->bdH * s->domOpen;
  IfFighter *V = &s->F[!s->domOwner];
  unsigned sd = 99u;
  if (s->dom == DOM_CLASH) { /* шов между куполами: молния — синяя с одной стороны, алая с другой, белая сердцевина */
    if (front) return;
    float sgn = s->F[0].x < s->F[1].x ? 1.0f : -1.0f, RX = s->bdW * 0.5f, RY = (float)s->bdH;
    float u = (if_X(s, if_clash_x(s, 0)) - cx) / RX, vmax = sqrtf(fa_sat(1 - u * u)) * 0.98f;
    unsigned fl = (unsigned)(s->frame / 2) * 2654435761u + 17u;
    float px = if_X(s, if_clash_x(s, 0)), py = gy, w = fmaxf(1.6f, U * 2.2f), amp = U * 5.0f;
    int n = 7;
    for (int i = 1; i <= n; i++) {
      float v = vmax * i / n, X1 = if_X(s, if_clash_x(s, v)), Y1 = gy - v * RY;
      if (Y1 < gy - ry) break;
      if_glow(c, (px + X1) * 0.5f, (py + Y1) * 0.5f, U * 22, FC(220, 200, 255), 0.4f * a, 1);
      if_bolt(c, px - sgn * U * 3, py, X1 - sgn * U * 3, Y1, if_rnd(&fl), 5, amp, w * 2.2f, FC(90, 160, 255), w * 0.8f,
              FC(170, 220, 255), 0.8f * a);
      if_bolt(c, px + sgn * U * 3, py, X1 + sgn * U * 3, Y1, if_rnd(&fl), 5, amp, w * 2.2f, FC(255, 60, 70), w * 0.8f,
              FC(255, 150, 160), 0.8f * a);
      if_bolt(c, px, py, X1, Y1, if_rnd(&fl), 5, amp * 0.7f, w * 1.4f, FC(255, 255, 255), w * 0.5f, FC(255, 255, 255), a);
      px = X1, py = Y1;
    }
    if (s->domPhase == DP_BREAK) { /* разбитый купол: белая вспышка по шву */
      float kk = fa_sat(s->domT / 22);
      if_glow(c, if_X(s, if_clash_x(s, 0.4f)), gy - 0.4f * RY, U * 60 * (0.5f + kk), FC(255, 255, 255), 0.5f * (1 - kk) * a, 1);
    }
    return;
  }
  if (s->dom == DOM_VOID && !front) {
    float vx = cx, vy = gy - ry * 0.45f;
    for (int i = 0; i < 30; i++) { /* свет течёт в глубину */
      float th = if_rf(&sd) * 2 * FA_PI, sp = if_rr(&sd, 0.006f, 0.015f), f = fmodf(if_rf(&sd) + T * sp, 1.0f);
      float r0 = 1.0f - f, r1 = r0 + 0.1f * (1 - f) + 0.02f;
      float ex = cosf(th) * rx * 0.95f, ey = sinf(th) * ry * 0.55f;
      fv_line(c, vx + ex * r1, vy + ey * r1, vx + ex * r0, vy + ey * r0, fmaxf(0.5f, U * 0.25f), fmaxf(0.7f, U * 0.5f),
              FC(200, 230, 255), a * fa_sat(f * 3) * 0.8f);
    }
    for (int i = 0; i < 24; i++) { /* мерцают звёзды */
      float u = if_rr(&sd, -0.9f, 0.9f), v = if_rr(&sd, 0.08f, 0.92f) * sqrtf(1 - u * u), tw = sinf(T * if_rr(&sd, 0.05f, 0.15f) + i);
      if (tw > 0.3f) if_glow(c, cx + u * rx, gy - v * ry, U * 3.2f * tw, FC(220, 240, 255), a * tw, 1);
    }
    for (int i = 0; i < 3; i++) { /* кольцо вокруг глубины */
      float an = T * 0.03f + i * 2.09f;
      if_arc(c, vx, vy, rx * 0.3f, ry * 0.07f, an, an + 1.5f, fmaxf(0.8f, U * 0.6f), FC(170, 215, 255), a * 0.7f, 1);
    }
  } else if (s->dom == DOM_VOID && front && V->st == FS_STUN) { /* бесконечный поток сведений в голову */
    float hx, hy;
    if_joint(V, FJ_HEAD, &hx, &hy);
    float X = if_X(s, hx), Y = if_Y(s, hy);
    unsigned fl = (unsigned)(s->frame / 3) * 2654435761u;
    if_glow(c, X, Y, U * 22, FC(200, 230, 255), 0.35f * a, 1);
    for (int i = 0; i < 16; i++) {
      float ox = if_rr(&fl, -1, 1) * U * 24, oy = if_rr(&fl, -1, 1) * U * 20, l = U * if_rr(&fl, 1.5f, 7);
      fv_line(c, X + ox, Y + oy, X + ox + l, Y + oy, fmaxf(0.5f, U * 0.28f), fmaxf(0.5f, U * 0.28f),
              (i & 1) ? FC(255, 255, 255) : FC(140, 220, 255), a * 0.85f);
    }
  } else if (s->dom == DOM_SHRINE && !front) {
    for (int i = 0; i < 40; i++) { /* искры и пепел вверх */
      float u = if_rr(&sd, -0.95f, 0.95f), ph = fmodf(if_rf(&sd) + T * if_rr(&sd, 0.004f, 0.01f), 1.0f);
      float X = cx + u * rx + sinf(T * 0.05f + i) * U * 3, Y = gy - ph * ry * 0.9f * sqrtf(1 - u * u);
      fv_disc(c, X, Y, fmaxf(0.7f, U * 0.45f), FC(255, 110, 60), a * sinf(ph * FA_PI));
    }
  } else if (s->dom == DOM_SHRINE && front) { /* режет всё внутри: разрезы вспыхивают тут и там */
    for (int i = 0; i < 16; i++) {
      float ph = T + i * 5.3f, lt = fmodf(ph, 10) / 10;
      unsigned h = if_hash((unsigned)i, (unsigned)(ph / 10));
      float u = ((h & 1023) / 1023.0f * 2 - 1) * 0.92f, v = (((h >> 10) & 1023) / 1023.0f * 0.85f + 0.05f) * sqrtf(1 - u * u);
      float X = cx + u * rx, Y = gy - v * ry, an = ((h >> 20) & 255) / 255.0f * FA_PI, L = U * (14 + ((h >> 4) & 31));
      if (i < 4 && V->hp > 0) { /* часть — прямо по жертве */
        float hx, hy;
        if_joint(V, FJ_CHEST, &hx, &hy);
        X = if_X(s, hx) + u * U * 14, Y = if_Y(s, hy) + (v - 0.4f) * U * 30;
      }
      float grow = fa_sat(lt * 5), fade = a * (1 - fa_sat((lt - 0.25f) / 0.6f));
      if (fade <= 0.01f) continue;
      float dx = cosf(an) * L * grow, dy = sinf(an) * L * grow, w = fmaxf(0.8f, U * 0.6f);
      fv_line(c, X, Y, X - dx, Y - dy, w * 2.6f, 0.4f, FC(255, 40, 60), fade * 0.5f);
      fv_line(c, X, Y, X + dx, Y + dy, w * 2.6f, 0.4f, FC(255, 40, 60), fade * 0.5f);
      fv_line(c, X, Y, X - dx, Y - dy, w, 0.25f, FC(255, 255, 255), fade);
      fv_line(c, X, Y, X + dx, Y + dy, w, 0.25f, FC(255, 255, 255), fade);
    }
  }
}

/* ===================== надписи ===================== */

/* Буквы рисует Windows (GDI) в чёрно-белую маску один раз на строку; дальше
   маска кладётся на холст сама, с обводкой и переходом цвета. Строки — в
   небольшом запасе: названия техник, «Раунд», счёт */
enum { TX_CALL, TX_BAN1, TX_BAN2, TX_BIG, TX_HUD, TX_QUOTE, TX_NUM };
#define IF_TXN 40
typedef struct {
  wchar_t s[48];
  int style, w, h;
  long used;
  unsigned char *m, *o; /* покрытие заливки и обводки, 0…255 */
} IfTxE;
struct IfText {
  HDC dc;
  HFONT font[TX_NUM];
  int ol[TX_NUM]; /* толщина обводки, px */
  IfTxE e[IF_TXN];
  long tick;
};

static void if_text_init(IfText *T, float figH) {
  static const struct {
    const wchar_t *face;
    float size, ol;
    int weight, italic;
  } kSt[TX_NUM] = {
      {L"Arial Black", 0.085f, 0.013f, FW_BLACK, 0}, {L"Segoe UI", 0.07f, 0.011f, FW_BOLD, 1},
      {L"Arial Black", 0.155f, 0.02f, FW_BLACK, 0},  {L"Arial Black", 0.2f, 0.024f, FW_BLACK, 1},
      {L"Segoe UI", 0.062f, 0.005f, FW_BOLD, 0},       {L"Segoe UI", 0.07f, 0.011f, FW_SEMIBOLD, 1},
  };
  memset(T, 0, sizeof(*T));
  T->dc = CreateCompatibleDC(NULL);
  for (int i = 0; i < TX_NUM; i++) {
    int px = (int)(figH * kSt[i].size + 0.5f);
    if (px < 11) px = 11;
    T->font[i] = CreateFontW(-px, 0, 0, 0, kSt[i].weight, kSt[i].italic, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, kSt[i].face);
    T->ol[i] = (int)(figH * kSt[i].ol + 0.5f);
    if (T->ol[i] < 1) T->ol[i] = 1;
  }
}

static void if_text_free(IfText *T) {
  for (int i = 0; i < IF_TXN; i++) free(T->e[i].m), free(T->e[i].o);
  for (int i = 0; i < TX_NUM; i++)
    if (T->font[i]) DeleteObject(T->font[i]);
  if (T->dc) DeleteDC(T->dc);
  memset(T, 0, sizeof(*T));
}

static IfTxE *if_text(IfText *T, const wchar_t *str, int style) {
  if (!T || !T->dc || !str || !*str) return NULL;
  T->tick++;
  IfTxE *E = NULL;
  for (int i = 0; i < IF_TXN; i++)
    if (T->e[i].m && T->e[i].style == style && !wcscmp(T->e[i].s, str)) {
      T->e[i].used = T->tick;
      return &T->e[i];
    }
  for (int i = 0; i < IF_TXN && !E; i++)
    if (!T->e[i].m) E = &T->e[i];
  if (!E) { /* место кончилось: вытесняем ту, что давно не нужна */
    E = &T->e[0];
    for (int i = 1; i < IF_TXN; i++)
      if (T->e[i].used < E->used) E = &T->e[i];
    free(E->m), free(E->o);
    E->m = E->o = NULL;
  }
  int n = (int)wcslen(str);
  if (n > 47) n = 47;
  HGDIOBJ oldF = SelectObject(T->dc, T->font[style]);
  SIZE sz;
  GetTextExtentPoint32W(T->dc, str, n, &sz);
  int R = T->ol[style], pad = R + 2, w = sz.cx + pad * 2 + sz.cy / 4, h = sz.cy + pad * 2;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w, bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32;
  void *bits = NULL;
  HBITMAP bm = CreateDIBSection(T->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  unsigned char *m = (unsigned char *)calloc((size_t)w * h, 1), *o = (unsigned char *)calloc((size_t)w * h, 1);
  if (!bm || !bits || !m || !o) {
    if (bm) DeleteObject(bm);
    free(m), free(o);
    SelectObject(T->dc, oldF);
    return NULL;
  }
  HGDIOBJ oldB = SelectObject(T->dc, bm);
  memset(bits, 0, (size_t)w * h * 4);
  SetBkMode(T->dc, TRANSPARENT);
  SetTextColor(T->dc, RGB(255, 255, 255));
  TextOutW(T->dc, pad, pad, str, n);
  GdiFlush();
  for (int i = 0; i < w * h; i++) m[i] = ((unsigned char *)bits)[i * 4 + 1];
  SelectObject(T->dc, oldB);
  DeleteObject(bm);
  SelectObject(T->dc, oldF);
  /* обводка: каждая точка буквы «штампует» вокруг себя круг радиуса R */
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int v = m[y * w + x];
      if (!v) continue;
      for (int dy = -R; dy <= R; dy++) {
        int yy = y + dy;
        if (yy < 0 || yy >= h) continue;
        for (int dx = -R; dx <= R; dx++) {
          int xx = x + dx;
          if (xx < 0 || xx >= w) continue;
          float d = sqrtf((float)(dx * dx + dy * dy)), k = fa_sat(R + 0.5f - d);
          if (k <= 0) continue;
          int q = (int)(v * k);
          if (q > o[yy * w + xx]) o[yy * w + xx] = (unsigned char)q;
        }
      }
    }
  memcpy(E->s, str, n * sizeof(wchar_t));
  E->s[n] = 0;
  E->style = style, E->w = w, E->h = h, E->m = m, E->o = o, E->used = T->tick;
  return E;
}

/* надпись на холст: обводка, затем заливка с переходом сверху вниз; scale — «удар» при появлении */
static void if_text_draw(FCv *c, const IfTxE *E, float cx, float cy, float scale, FCol top, FCol bot, FCol line, float a) {
  if (!E || a <= 0.01f || scale <= 0.05f) return;
  float w = E->w * scale, h = E->h * scale, x0 = cx - w * 0.5f, y0 = cy - h * 0.5f;
  if (fabsf(scale - 1) < 0.01f) x0 = floorf(x0 + 0.5f), y0 = floorf(y0 + 0.5f), scale = 1;
  int X0, Y0, X1, Y1;
  if (!fv_clip(c, x0, y0, x0 + w, y0 + h, &X0, &Y0, &X1, &Y1)) return;
  if (scale == 1) { /* без увеличения (шкалы, надписи в покое): точка в точку, без сглаживания между точками */
    int ox = (int)x0, oy = (int)y0;
    for (int y = Y0; y < Y1; y++) {
      int iy = y - oy;
      if (iy < 0 || iy >= E->h) continue;
      FCol fill = fa_mix(top, bot, fa_sat((float)iy / E->h * 1.15f - 0.1f));
      const unsigned char *mr = E->m + iy * E->w, *orow = E->o + iy * E->w;
      unsigned char *row = c->px + (size_t)y * c->w * 4;
      for (int x = X0; x < X1; x++) {
        int ix = x - ox;
        if (ix < 0 || ix >= E->w) continue;
        if (orow[ix]) fv_over(row + x * 4, line, a * orow[ix] / 255.0f);
        if (mr[ix]) fv_over(row + x * 4, fill, a * mr[ix] / 255.0f);
      }
    }
    return;
  }
  float inv = 1.0f / scale;
  for (int y = Y0; y < Y1; y++) {
    float sy = (y + 0.5f - y0) * inv - 0.5f;
    int iy = (int)floorf(sy);
    float fy = sy - iy;
    FCol fill = fa_mix(top, bot, fa_sat(sy / E->h * 1.15f - 0.1f));
    unsigned char *row = c->px + (size_t)y * c->w * 4;
    for (int x = X0; x < X1; x++) {
      float sx = (x + 0.5f - x0) * inv - 0.5f, mv = 0, ov = 0;
      int ix = (int)floorf(sx);
      float fx = sx - ix;
      for (int q = 0; q < 4; q++) {
        int xx = ix + (q & 1), yy = iy + (q >> 1);
        if (xx < 0 || yy < 0 || xx >= E->w || yy >= E->h) continue;
        float wq = ((q & 1) ? fx : 1 - fx) * ((q >> 1) ? fy : 1 - fy);
        mv += E->m[yy * E->w + xx] * wq, ov += E->o[yy * E->w + xx] * wq;
      }
      if (ov > 0.5f) fv_over(row + x * 4, line, a * ov / 255.0f);
      if (mv > 0.5f) fv_over(row + x * 4, fill, a * mv / 255.0f);
    }
  }
}

/* название техники над головой */
static void if_draw_call(IfScene *s, IfEff *e) {
  IfTxE *E = if_text(s->tx, e->txt, TX_CALL);
  if (!E) return;
  float k = e->t / e->life, a = fa_sat(e->t / 4) * fa_sat((e->life - e->t) / 12);
  float pop = 1 + 0.35f * (1 - fa_smooth(e->t / 6)), hx, hy;
  if (e->owner == 2) { /* над колесом Махораги */
    if (!s->maho) return;
    hx = s->mahoX, hy = s->mahoFl + s->mahoY + 150 * IF_MU;
  } else {
    IfFighter *F = &s->F[e->owner];
    a *= F->alpha;
    if_joint(F, FJ_HEAD, &hx, &hy);
  }
  float X = if_X(s, hx), Y = if_Y(s, hy) - s->U * 22 - s->U * 6 * k, half = E->w * 0.5f + 6;
  X = fa_clampf(X, half, s->W - half);
  if (Y < E->h) Y = (float)E->h;
  if_text_draw(&s->cv, E, X, Y, pop, FC(255, 255, 255), fa_mix(FC(255, 255, 255), e->c, 0.6f), fa_dim(e->c, 0.28f), a);
}

/* крупная надпись посреди: «Раунд», «Бой!», территория, победитель */
static void if_draw_banner(IfScene *s) {
  if ((!s->ban1 && !s->ban2) || !s->tx) return;
  float t = s->banT, hs = s->hs, a = fa_sat(t / 5) * fa_sat((s->banLife - t) / 14);
  float pop = 1 + (s->banStyle == 3 ? 0.7f : 0.4f) * (1 - fa_smooth(t / 8));
  float cx = s->hudX, cy = s->hudY - hs * 1.62f;
  if (cy < s->hudTop + hs * 0.3f) cy = s->hudTop + hs * 0.3f;
  FCv *c = &s->cv;
  IfTxE *E2 = NULL, *E1 = NULL;
  switch (s->banStyle) {
  case 1: /* Безграничная пустота */
  case 2: /* Святилище злобы */
    E1 = if_text(s->tx, s->ban1, TX_BAN1);
    E2 = if_text(s->tx, s->ban2, TX_BAN2);
    if (E2) {
      int v = s->banStyle == 1;
      if_glow(c, cx, cy, E2->w * 0.62f, v ? FC(80, 150, 255) : FC(220, 20, 40), 0.35f * a, 1);
      if_text_draw(c, E2, cx, cy, pop, v ? FC(255, 255, 255) : FC(255, 236, 236), v ? FC(140, 205, 255) : FC(235, 30, 45),
                   v ? FC(8, 18, 60) : FC(26, 0, 4), a);
    }
    if (E1 && E2)
      if_text_draw(c, E1, cx, cy - E2->h * 0.5f - E1->h * 0.35f, 1, FC(255, 255, 255), FC(225, 225, 235), FC(20, 16, 30),
                   a * fa_sat(t / 10));
    break;
  case 5: { /* столкновение территорий: оба имени, синее над алым */
    IfTxE *E3 = if_text(s->tx, s->ban3, TX_BAN2);
    float sc = 0.78f;
    E1 = if_text(s->tx, s->ban1, TX_BAN1);
    E2 = if_text(s->tx, s->ban2, TX_BAN2);
    if (E2) {
      float y2 = cy - E2->h * sc * 0.55f;
      if_glow(c, cx, y2, E2->w * sc * 0.62f, FC(80, 150, 255), 0.35f * a, 1);
      if_text_draw(c, E2, cx, y2, pop * sc, FC(255, 255, 255), FC(140, 205, 255), FC(8, 18, 60), a);
    }
    if (E3) {
      float y3 = cy + E3->h * sc * 0.55f;
      if_glow(c, cx, y3, E3->w * sc * 0.62f, FC(220, 20, 40), 0.35f * a, 1);
      if_text_draw(c, E3, cx, y3, pop * sc, FC(255, 236, 236), FC(235, 30, 45), FC(26, 0, 4), a);
    }
    if (E1 && E2)
      if_text_draw(c, E1, cx, cy - E2->h * sc * 1.1f - E1->h * 0.4f, 1, FC(255, 255, 255), FC(225, 225, 235), FC(20, 16, 30),
                   a * fa_sat(t / 10));
    break;
  }
  case 6: /* Фиолетовый на всю мощь */
    E1 = if_text(s->tx, s->ban1, TX_BAN1);
    E2 = if_text(s->tx, s->ban2, TX_BAN2);
    if (E2) {
      if_glow(c, cx, cy, E2->w * 0.62f, FC(170, 80, 255), 0.4f * a, 1);
      if_text_draw(c, E2, cx, cy, pop, FC(255, 255, 255), FC(205, 135, 255), FC(30, 8, 62), a);
    }
    if (E1 && E2)
      if_text_draw(c, E1, cx, cy - E2->h * 0.5f - E1->h * 0.35f, 1, FC(255, 255, 255), FC(225, 225, 235), FC(20, 16, 30),
                   a * fa_sat(t / 10));
    break;
  case 4: { /* победитель и его слова */
    int g = s->winner == 0;
    E2 = if_text(s->tx, s->ban2, TX_BIG);
    E1 = if_text(s->tx, s->ban1, TX_QUOTE);
    if (E2) {
      if_glow(c, cx, cy, E2->w * 0.6f, g ? FC(90, 170, 255) : FC(230, 30, 50), 0.3f * a, 1);
      if_text_draw(c, E2, cx, cy, pop, FC(255, 255, 255), g ? FC(120, 200, 255) : FC(255, 90, 100), FC(16, 10, 26), a);
    }
    if (E1 && E2)
      if_text_draw(c, E1, cx, cy + E2->h * 0.5f + E1->h * 0.4f, 1, FC(255, 255, 255), FC(230, 230, 240), FC(16, 10, 26),
                   a * fa_sat((t - 20) / 12));
    break;
  }
  default: /* 0 — «Раунд», 3 — «Бой!», «Нокаут!» */
    E2 = if_text(s->tx, s->ban2, TX_BIG);
    if (E2) {
      int hot = s->banStyle == 3;
      if (hot) if_glow(c, cx, cy, E2->w * 0.55f, FC(255, 140, 30), 0.3f * a, 1);
      if_text_draw(c, E2, cx, cy, pop, hot ? FC(255, 250, 205) : FC(255, 255, 255), hot ? FC(255, 140, 30) : FC(200, 215, 255),
                   hot ? FC(90, 10, 10) : FC(18, 20, 44), a);
    }
    break;
  }
}

/* шкалы: жизнь и проклятая энергия, имена, раунд и счёт */
static void if_rect(FCv *c, float x0, float y0, float x1, float y1, FCol top, FCol bot, float a) {
  int X0, Y0, X1, Y1;
  if (a <= 0.004f || !fv_clip(c, x0, y0, x1, y1, &X0, &Y0, &X1, &Y1)) return;
  for (int y = Y0; y < Y1; y++) {
    FCol col = fa_mix(top, bot, fa_sat((y + 0.5f - y0) / (y1 - y0)));
    unsigned char *row = c->px + (size_t)y * c->w * 4;
    for (int x = X0; x < X1; x++) fv_over(row + x * 4, col, a);
  }
}
static void if_draw_hud(IfScene *s) {
  float a = fmaxf(s->F[0].alpha, s->F[1].alpha);
  if (a <= 0.01f) return;
  FCv *c = &s->cv;
  float hs = s->hs, barW = fminf(s->W * 0.3f, hs * 1.75f), barH = floorf(fmaxf(8, hs * 0.045f));
  float ceH = floorf(fmaxf(4, hs * 0.02f)), gap = hs * 0.46f, cx = floorf(s->hudX);
  float top = floorf(fmaxf(s->hudY - hs * 3.4f, s->hudTop + hs * 0.2f));
  if (top < 30) top = 30;
  for (int i = 0; i < 2; i++) {
    IfFighter *F = &s->F[i];
    float x0 = floorf(i ? cx + gap * 0.5f : cx - gap * 0.5f - barW), x1 = x0 + barW;
    float hp = fa_sat(s->hud[i][0] / IF_HP), gh = fa_sat(F->ghost / IF_HP), ce = fa_sat(s->hud[i][1] / IF_CE);
    if_rect(c, x0 - 2, top - 2, x1 + 2, top + barH + 2, FC(14, 12, 22), FC(14, 12, 22), 0.75f * a);
    if_rect(c, x0, top, x1, top + barH, FC(60, 20, 30), FC(40, 12, 20), 0.6f * a);
    float g0 = i ? x1 - barW * gh : x0, g1 = i ? x1 : x0 + barW * gh;
    if_rect(c, g0, top, g1, top + barH, FC(255, 240, 210), FC(255, 200, 150), 0.9f * a);
    float h0 = i ? x1 - barW * hp : x0, h1 = i ? x1 : x0 + barW * hp;
    FCol t0 = i ? FC(255, 140, 140) : FC(150, 225, 255), t1 = i ? FC(205, 30, 55) : FC(40, 120, 235);
    if (hp < 0.3f) t0 = FC(255, 220, 120), t1 = FC(240, 120, 30);
    if_rect(c, h0, top, h1, top + barH, t0, t1, a);
    if_rect(c, h0, top, h1, top + 1, FC(255, 255, 255), FC(255, 255, 255), 0.5f * a);
    float cy0 = top + barH + 3, c0 = i ? x1 - barW * 0.7f * ce : x0, c1 = i ? x1 : x0 + barW * 0.7f * ce;
    if_rect(c, i ? x1 - barW * 0.7f - 1 : x0 - 1, cy0 - 1, i ? x1 + 1 : x0 + barW * 0.7f + 1, cy0 + ceH + 1, FC(14, 12, 22),
            FC(14, 12, 22), 0.7f * a);
    FCol ec = F->burn > 0 ? FC(110, 110, 120) : (i ? FC(255, 120, 80) : FC(130, 225, 255));
    if_rect(c, c0, cy0, c1, cy0 + ceH, fa_mix(ec, FC(255, 255, 255), 0.35f), ec, a);
    IfTxE *N = if_text(s->tx, i ? L"РЁМЕН СУКУНА" : L"ГОДЖО САТОРУ", TX_HUD);
    if (N)
      if_text_draw(c, N, i ? x1 - N->w * 0.5f + 4 : x0 + N->w * 0.5f - 4, top - N->h * 0.42f, 1, FC(255, 255, 255),
                   FC(225, 230, 240), FC(14, 12, 22), a);
  }
  wchar_t buf[32];
  swprintf(buf, 32, L"РАУНД %d", s->round);
  IfTxE *R = if_text(s->tx, buf, TX_HUD);
  if (R) if_text_draw(c, R, cx, top + barH * 0.5f, 1, FC(255, 255, 255), FC(220, 225, 240), FC(14, 12, 22), a);
  if (s->wins) {
    swprintf(buf, 32, L"%d : %d", s->wins[0], s->wins[1]);
    IfTxE *Wn = if_text(s->tx, buf, TX_HUD);
    if (Wn && R)
      if_text_draw(c, Wn, cx, top + barH * 0.5f + R->h * 0.62f, 1, FC(255, 240, 200), FC(255, 200, 140), FC(14, 12, 22), a);
  }
}

/* ===================== кадр ===================== */

/* сначала тот, кого бьют: бьющий — поверх */
static int if_back_one(const IfScene *s) {
  for (int i = 0; i < 2; i++) {
    int st = s->F[i].st;
    if (st == FS_HIT || st == FS_FLY || st == FS_DOWN || st == FS_STUN || st == FS_TRAP || st == FS_KO || st == FS_GUARD ||
        st == FS_GETUP)
      return i;
  }
  return s->F[0].st == FS_MOVE && s->F[1].st != FS_MOVE ? 1 : 0;
}

static void if_render_world(IfScene *s) {
  FCv *c = &s->cv;
  c->x0 = 0, c->y0 = 0, c->x1 = s->W, c->y1 = s->H;
  fv_reset_touch(c);
  if_dom_build(s);
  if (s->dom) if_dom_blit(s), if_dom_fx(s, 0);
  if_draw_maho(s);
  for (int i = 0; i < IF_MAXE; i++)
    if (s->ef[i].kind != EF_NONE) if_draw_eff(s, &s->ef[i], 0);
  for (int i = 0; i < 2; i++) if_draw_shadow(s, &s->F[i]);
  for (int j = 0; j < IF_MAXJ; j++)
    if (s->pj[j].kind == PJ_BLUE) if_draw_pj(s, &s->pj[j], 1);
  int first = if_back_one(s);
  for (int n = 0; n < 2; n++) if_draw_fighter(s, &s->F[n ? !first : first]);
  if_draw_shadow_wheel(s);
  for (int n = 0; n < 2; n++) { /* сила в руках и след удара — поверх обоих бойцов */
    IfFighter *F = &s->F[n ? !first : first];
    if_draw_charge(s, F);
    if_draw_trail(s, F);
  }
  for (int j = 0; j < IF_MAXJ; j++)
    if (s->pj[j].kind != PJ_NONE) if_draw_pj(s, &s->pj[j], 0);
  for (int i = 0; i < IF_MAXE; i++)
    if (s->ef[i].kind != EF_NONE) if_draw_eff(s, &s->ef[i], 1);
  if_draw_parts(s);
  if (s->dom) if_dom_fx(s, 1);
}

/* надписи и шкалы — на большом холсте, чётко: мир на время пересчитан в его px */
static void if_render_over(IfScene *s) {
  FCv cv0 = s->cv;
  float U0 = s->U, gy0 = s->gy, sx0 = s->shx, sy0 = s->shy, hx0 = s->hudX, hy0 = s->hudY, ht0 = s->hudTop;
  int W0 = s->W, H0 = s->H, k = s->k;
  s->cv = s->big;
  s->U *= k, s->gy = (s->gy + 0.5f) * k - 0.5f, s->shx *= k, s->shy *= k, s->hudX *= k, s->hudY *= k, s->hudTop *= k;
  s->W = s->big.w, s->H = s->big.h;
  FCv *c = &s->cv;
  c->x0 = 0, c->y0 = 0, c->x1 = s->W, c->y1 = s->H;
  fv_reset_touch(c);
  for (int i = 0; i < IF_MAXE; i++)
    if (s->ef[i].kind == EF_CALL) if_draw_call(s, &s->ef[i]);
  if_draw_banner(s);
  if_draw_hud(s);
  s->big = s->cv;
  s->cv = cv0;
  s->U = U0, s->gy = gy0, s->shx = sx0, s->shy = sy0, s->hudX = hx0, s->hudY = hy0, s->hudTop = ht0, s->W = W0, s->H = H0;
}

/* что задето кадром — прямоугольник в пределах холста */
static RECT if_touched(const IfScene *s) {
  RECT r = {s->cv.tx0, s->cv.ty0, s->cv.tx1, s->cv.ty1};
  if (r.left < 0) r.left = 0;
  if (r.top < 0) r.top = 0;
  if (r.right > s->W) r.right = s->W;
  if (r.bottom > s->H) r.bottom = s->H;
  if (r.left >= r.right || r.top >= r.bottom) SetRectEmpty(&r);
  return r;
}

static void if_clear(IfScene *s, const RECT *r) {
  if (IsRectEmpty(r)) return;
  for (int y = r->top; y < r->bottom; y++) memset(s->cv.px + ((size_t)y * s->W + r->left) * 4, 0, (size_t)(r->right - r->left) * 4);
}

/* мониторы и окна — в px маленького холста */
static void if_stage_div(IfStage *d, const IfStage *st, int k) {
  *d = *st;
  for (int i = 0; i < d->nMon; i++) {
    RECT *r = &d->mon[i];
    r->left /= k, r->top /= k, r->right = (r->right + k - 1) / k, r->bottom = (r->bottom + k - 1) / k;
  }
  for (int i = 0; i < d->nWin; i++) {
    RECT *r = &d->win[i];
    r->left = (r->left + k - 1) / k, r->top = (r->top + k - 1) / k, r->right /= k, r->bottom /= k;
  }
}

/* W×H, px — окно (большой холст); k — размер пикселя пиксель-арта; st — в px окна */
static IfScene *if_scene_new(int W, int H, unsigned char *px, unsigned seed, int *wins, const IfStage *st, int k) {
  if (k < 1) k = 1;
  int Ws = (W + k - 1) / k, Hs = (H + k - 1) / k;
  IfScene *s = (IfScene *)calloc(1, sizeof(IfScene));
  IfText *tx = (IfText *)calloc(1, sizeof(IfText));
  unsigned char *sm = (unsigned char *)calloc((size_t)Ws * Hs, 4);
  static IfStage ss;
  if (!s || !tx || !sm) {
    free(s), free(tx), free(sm);
    return NULL;
  }
  if_fall_init();
  s->k = k;
  s->big.px = px, s->big.w = W, s->big.h = H;
  s->cv.px = sm, s->cv.w = Ws, s->cv.h = Hs;
  SetRectEmpty(&s->prevS), SetRectEmpty(&s->prevO);
  if (st) if_stage_div(&ss, st, k);
  if_scene_init(s, Ws, Hs, seed, wins, st ? &ss : NULL);
  if_text_init(tx, s->hs);
  s->tx = tx;
  return s;
}

/* окна сдвинули: заново площадки (st — в px окна) */
static void if_scene_windows(IfScene *s, const IfStage *st) {
  static IfStage ss;
  if_stage_div(&ss, st, s->k);
  if_stage_windows(s, &ss);
}

static void if_scene_free(IfScene *s) {
  if (!s) return;
  if (s->tx) if_text_free(s->tx), free(s->tx);
  free(s->cv.px);
  free(s->bd), free(s->bd2);
  free(s);
}

/* маленький холст → большой, каждая точка — квадрат k×k. Прозрачность ступенями:
   края и свечения не размываются, а идут крупными уровнями, как в пиксель-арте */
static void if_upscale(IfScene *s, RECT r) {
  int k = s->k, Wb = s->big.w, Hb = s->big.h;
  for (int y = r.top; y < r.bottom; y++) {
    const unsigned char *src = s->cv.px + ((size_t)y * s->W + r.left) * 4;
    int by = y * k;
    if (by >= Hb) break;
    unsigned char *dst = s->big.px + ((size_t)by * Wb + (size_t)r.left * k) * 4;
    int bx = r.left * k, n = 0;
    for (int x = r.left; x < r.right; x++, src += 4) {
      unsigned a = src[3], q = a < 10 ? 0 : (a >= 216 ? 255 : (a >= 150 ? 184 : (a >= 84 ? 116 : (a >= 40 ? 56 : 24))));
      unsigned char p[4];
      if (q == a) {
        memcpy(p, src, 4);
      } else if (!q) {
        memset(p, 0, 4);
      } else {
        for (int c = 0; c < 3; c++) {
          unsigned v = src[c] * q / a;
          p[c] = (unsigned char)(v > q ? q : v);
        }
        p[3] = (unsigned char)q;
      }
      for (int i = 0; i < k && bx < Wb; i++, bx++, n++) memcpy(dst + (size_t)n * 4, p, 4);
    }
    for (int j = 1; j < k && by + j < Hb; j++) memcpy(dst + (size_t)j * Wb * 4, dst, (size_t)n * 4);
  }
}

/* кадр: мир на маленьком холсте, на большой — крупными пикселями, сверху чёткие
   надписи. Возвращает, что на большом поменялось (для UpdateLayeredWindow) */
static RECT if_frame(IfScene *s) {
  int k = s->k;
  if_clear(s, &s->prevS);
  g_faHard = k > 1; /* бойцы — с жёстким краем, без сглаживания: пиксель-арт */
  if_render_world(s);
  g_faHard = 0;
  RECT curS = if_touched(s), up, o = s->prevO;
  UnionRect(&up, &s->prevS, &curS);
  if (!IsRectEmpty(&o)) { /* под старыми надписями — снова мир */
    for (int y = o.top; y < o.bottom; y++)
      memset(s->big.px + ((size_t)y * s->big.w + o.left) * 4, 0, (size_t)(o.right - o.left) * 4);
    RECT os = {o.left / k, o.top / k, (o.right + k - 1) / k, (o.bottom + k - 1) / k};
    if (os.right > s->W) os.right = s->W;
    if (os.bottom > s->H) os.bottom = s->H;
    UnionRect(&up, &up, &os);
  }
  if (!IsRectEmpty(&up)) if_upscale(s, up);
  if_render_over(s);
  RECT curO = {s->big.tx0, s->big.ty0, s->big.tx1, s->big.ty1};
  if (curO.left < 0) curO.left = 0;
  if (curO.top < 0) curO.top = 0;
  if (curO.right > s->big.w) curO.right = s->big.w;
  if (curO.bottom > s->big.h) curO.bottom = s->big.h;
  if (curO.left >= curO.right || curO.top >= curO.bottom) SetRectEmpty(&curO);
  RECT dirty = {up.left * k, up.top * k, up.right * k, up.bottom * k};
  if (dirty.right > s->big.w) dirty.right = s->big.w;
  if (dirty.bottom > s->big.h) dirty.bottom = s->big.h;
  if (IsRectEmpty(&up)) SetRectEmpty(&dirty);
  UnionRect(&dirty, &dirty, &curO);
  UnionRect(&dirty, &dirty, &s->prevO);
  s->prevS = curS, s->prevO = curO;
  return dirty;
}

/* ===================== окно, поток, когда начинать ===================== */

static HANDLE g_ifThread;
static volatile LONG g_ifQuit;      /* главный поток: «хватит» */
static volatile LONG g_ifScreenOff; /* экран погас или сменился */
static int g_ifLatched;             /* бой кончился без человека — ждать, пока тронут мышь */
static DWORD g_ifLatchInput;
static int g_ifWins[2]; /* счёт живёт, пока работает программа */
static volatile LONG g_ifWatchOff;  /* в режиме просмотра долго держали Esc: выключить режим */
static void set_idle_watch(int on); /* cursorpad.c: режим просмотра с сохранением и кнопками */
static const GUID kIfDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

static LRESULT CALLBACK if_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_POWERBROADCAST && w == PBT_POWERSETTINGCHANGE && l) {
    const POWERBROADCAST_SETTING *ps = (const POWERBROADCAST_SETTING *)l;
    if (IsEqualGUID(&ps->PowerSetting, &kIfDisplayState) && ps->DataLength >= sizeof(DWORD) && *(const DWORD *)ps->Data == 0)
      InterlockedExchange(&g_ifScreenOff, 1);
    return TRUE;
  }
  if (m == WM_DISPLAYCHANGE) InterlockedExchange(&g_ifScreenOff, 1); /* мониторы переставили — бой уже не на месте */
  if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  return DefWindowProcW(h, m, w, l);
}

/* нельзя: заставка, блокировка, игра или видео на весь экран (на любом мониторе), кто-то держит
   экран включённым (так видео и звонки не дают включиться и заставке),
   экономия заряда. strict 0 — только заставка и блокировка */
static int if_blocked(int strict) {
  BOOL ss = FALSE;
  if (SystemParametersInfoW(SPI_GETSCREENSAVERRUNNING, 0, &ss, 0) && ss) return 1;
  HDESK d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!d) return 1;
  wchar_t name[64] = L"";
  DWORD need = 0;
  GetUserObjectInformationW(d, UOI_NAME, name, sizeof(name) - sizeof(wchar_t), &need);
  CloseDesktop(d);
  if (_wcsicmp(name, L"Default") != 0) return 1;
  if (!strict) return 0;
  QUERY_USER_NOTIFICATION_STATE q;
  if (SHQueryUserNotificationState(&q) == S_OK && q != QUNS_ACCEPTS_NOTIFICATIONS && q != QUNS_QUIET_TIME) return 1;
  HWND fg = GetForegroundWindow();
  if (fg && fg != GetShellWindow() && fg != GetDesktopWindow()) {
    wchar_t cls[32] = L"";
    GetClassNameW(fg, cls, 32);
    LONG st = GetWindowLongW(fg, GWL_STYLE);
    int framed = IsZoomed(fg) && (st & WS_CAPTION) == WS_CAPTION; /* просто развёрнутое окно — не игра */
    if (wcscmp(cls, L"Progman") && wcscmp(cls, L"WorkerW") && !framed) {
      RECT r;
      MONITORINFO mi;
      mi.cbSize = sizeof(mi);
      HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONULL);
      if (mon && GetMonitorInfoW(mon, &mi) && GetWindowRect(fg, &r) &&
          r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right &&
          r.bottom >= mi.rcMonitor.bottom)
        return 1;
    }
  }
  typedef LONG(WINAPI * CnpiFn)(int, PVOID, ULONG, PVOID, ULONG);
  static CnpiFn cnpi;
  static int tried;
  if (!tried) {
    tried = 1;
    HMODULE pp = LoadLibraryExW(L"powrprof.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (pp) cnpi = (CnpiFn)(void *)GetProcAddress(pp, "CallNtPowerInformation");
  }
  ULONG es = 0;
  if (cnpi && cnpi(16 /* SystemExecutionState */, NULL, 0, &es, sizeof(es)) == 0 && (es & ES_DISPLAY_REQUIRED)) return 1;
  SYSTEM_POWER_STATUS ps;
  /* Older mingw headers name the Windows battery-saver byte Reserved1. */
  if (GetSystemPowerStatus(&ps) && (((const BYTE *)&ps)[3] & 1)) return 1;
  return 0;
}

/* мониторы: рабочие области (без панели задач) */
static BOOL CALLBACK if_mon_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM l) {
  (void)dc, (void)rc;
  IfStage *st = (IfStage *)l;
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  if (st->nMon < IF_MAXMON && GetMonitorInfoW(m, &mi)) {
    if (mi.dwFlags & MONITORINFOF_PRIMARY) st->prim = st->nMon;
    st->mon[st->nMon++] = mi.rcWork;
  }
  return TRUE;
}

typedef HRESULT(WINAPI *IfDwmGetFn)(HWND, DWORD, PVOID, DWORD);
typedef struct {
  IfStage *st;
  POINT o;          /* начало холста на экране */
  IfDwmGetFn dwm;
} IfWinCtx;

/* окна сверху вниз по Z: видимые, не свёрнутые, не на другом рабочем столе */
static BOOL CALLBACK if_win_cb(HWND h, LPARAM l) {
  IfWinCtx *c = (IfWinCtx *)l;
  if (c->st->nWin >= IF_MAXWIN) return FALSE;
  if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
  LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
  if (ex & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT)) return TRUE; /* свои и чужие прозрачные слои, всплывашки */
  wchar_t cls[40] = L"";
  GetClassNameW(h, cls, 40);
  if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
      !wcscmp(cls, L"Shell_SecondaryTrayWnd") || !wcscmp(cls, L"CursorPadIdleFight"))
    return TRUE;
  RECT r;
  DWORD cloaked = 0;
  if (c->dwm && c->dwm(h, 14 /* DWMWA_CLOAKED */, &cloaked, sizeof(cloaked)) == S_OK && cloaked) return TRUE;
  if (!c->dwm || c->dwm(h, 9 /* DWMWA_EXTENDED_FRAME_BOUNDS: без невидимой рамки */, &r, sizeof(r)) != S_OK)
    if (!GetWindowRect(h, &r)) return TRUE;
  if (r.right - r.left < 60 || r.bottom - r.top < 40) return TRUE;
  OffsetRect(&r, -c->o.x, -c->o.y);
  c->st->win[c->st->nWin++] = r;
  return TRUE;
}

static void if_get_windows(IfStage *st, POINT o) {
  static IfDwmGetFn dwm;
  static int tried;
  if (!tried) {
    tried = 1;
    HMODULE m = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (m) dwm = (IfDwmGetFn)(void *)GetProcAddress(m, "DwmGetWindowAttribute");
  }
  IfWinCtx c = {st, o, dwm};
  st->nWin = 0;
  EnumWindows(if_win_cb, (LPARAM)&c);
}

/* холст на все мониторы; слишком большой (стена из 4K) — только главный */
static int if_get_stage(IfStage *st, RECT *canvas) {
  memset(st, 0, sizeof(*st));
  st->prim = 0;
  EnumDisplayMonitors(NULL, NULL, if_mon_cb, (LPARAM)st);
  if (st->nMon <= 0) return 0;
  RECT u = st->mon[st->prim];
  for (int i = 0; i < st->nMon; i++) UnionRect(&u, &u, &st->mon[i]);
  if ((double)(u.right - u.left) * (u.bottom - u.top) > 40e6 || u.right - u.left > 16384 || u.bottom - u.top > 16384) {
    st->mon[0] = st->mon[st->prim];
    st->nMon = 1, st->prim = 0;
    u = st->mon[0];
  }
  for (int i = 0; i < st->nMon; i++) OffsetRect(&st->mon[i], -u.left, -u.top);
  *canvas = u;
  return 1;
}

static DWORD WINAPI if_thread(LPVOID arg) {
  int demo = (int)(INT_PTR)arg;
  HINSTANCE hi = GetModuleHandleW(NULL);
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = if_wndproc;
  wc.hInstance = hi;
  wc.lpszClassName = L"CursorPadIdleFight";
  RegisterClassExW(&wc); /* второй раз — просто откажет, класс уже есть */
  static IfStage st; /* один поток боя за раз */
  RECT wa;
  if (!if_get_stage(&st, &wa)) return 0;
  int W = wa.right - wa.left, H = wa.bottom - wa.top;
  if (W < 320 || H < 240) return 0;
  POINT org = {wa.left, wa.top};
  if_get_windows(&st, org);
  HWND w = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                           wc.lpszClassName, L"", WS_POPUP, wa.left, wa.top, W, H, NULL, NULL, hi, NULL);
  if (!w) return 0;
  HDC dc = CreateCompatibleDC(NULL);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = W, bi.bmiHeader.biHeight = -H;
  bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32;
  void *bits = NULL;
  HBITMAP bm = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
  RECT pm = st.mon[st.prim];
  int k = if_pick_k(pm.right - pm.left, pm.bottom - pm.top);
  IfScene *s = bm ? if_scene_new(W, H, (unsigned char *)bits, GetTickCount() * 2654435761u + 17u, g_ifWins, &st, k) : NULL;
  if (!s) {
    if (bm) DeleteObject(bm);
    if (dc) DeleteDC(dc);
    DestroyWindow(w);
    return 0;
  }
  HGDIOBJ oldBm = SelectObject(dc, bm);
  memset(bits, 0, (size_t)W * H * 4);
  HPOWERNOTIFY pn = RegisterPowerSettingNotification(w, &kIfDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
  HANDLE tm = CreateWaitableTimerExW(NULL, NULL, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
  if (!tm) tm = CreateWaitableTimerW(NULL, FALSE, NULL);
  LARGE_INTEGER fq, last, now;
  QueryPerformanceFrequency(&fq);
  QueryPerformanceCounter(&last);
  LASTINPUTINFO li;
  li.cbSize = sizeof(li);
  GetLastInputInfo(&li);
  DWORD in0 = li.dwTime, start = GetTickCount();
  POINT cur0;
  GetCursorPos(&cur0);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  double acc = 0, step = 1.0 / 60, cost = 0; /* cost — сколько стоит кадр, мс, в среднем */
  int shown = 0, frames = 0, battery = 0, why = 0;
  while (!g_ifQuit) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    /* мышь или клавиша — бой исчезает сразу (demo 3 — режим просмотра: смотрят, работая;
       demo 2 — проверочная сборка в Wine:
       там ввод «случается» сам, без мыши) */
    GetLastInputInfo(&li);
    if (li.dwTime != in0 && demo < 2) {
      POINT p;
      GetCursorPos(&p);
      if (!demo) {
        why = 1;
        break;
      }
      /* показ по кнопке: дать отпустить кнопку и убрать руку; дрожь мыши не в счёт */
      int moved = abs(p.x - cur0.x) + abs(p.y - cur0.y);
      if (GetTickCount() - start > 1200 && (moved > 24 || moved == 0)) {
        why = 1;
        break;
      }
      in0 = li.dwTime;
      if (GetTickCount() - start <= 1200) cur0 = p;
    }
    if (g_ifScreenOff) {
      why = 2;
      break;
    }
    if (demo == 3) { /* режим просмотра: Esc, зажатый на секунду, выключает его */
      static DWORD escAt;
      if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        if (!escAt) escAt = GetTickCount() | 1;
        else if (GetTickCount() - escAt > 1000) {
          escAt = 0;
          InterlockedExchange(&g_ifWatchOff, 1);
          break;
        }
      } else {
        escAt = 0;
      }
    }
    if (frames % 120 == 90) { /* окна могли сдвинуть, закрыть, открыть */
      if_get_windows(&st, org);
      if_scene_windows(s, &st);
    }
    if (frames % 60 == 30) {
      if (if_blocked(!demo || demo == 3)) { /* в режиме просмотра — не поверх игры или видео на весь экран */
        why = 2;
        break;
      }
      SYSTEM_POWER_STATUS ps;
      battery = GetSystemPowerStatus(&ps) && ps.ACLineStatus == 0; /* от батареи — 30 кадров в секунду */
    }
    QueryPerformanceCounter(&now);
    double el = (double)(now.QuadPart - last.QuadPart) / fq.QuadPart;
    last = now;
    acc += el > 0.25 ? 0.25 : el;
    int n = 0;
    while (acc >= step && n < 5) if_step(s), acc -= step, n++;
    if (n == 5) acc = 0;
    if (n > 0 || !shown) {
      LARGE_INTEGER r0, r1;
      QueryPerformanceCounter(&r0);
      RECT dirty = if_frame(s);
      POINT pd = {wa.left, wa.top}, ps0 = {0, 0};
      SIZE sz = {W, H};
      UPDATELAYEREDWINDOWINFO ui;
      memset(&ui, 0, sizeof(ui));
      ui.cbSize = sizeof(ui), ui.pptDst = &pd, ui.psize = &sz, ui.hdcSrc = dc, ui.pptSrc = &ps0;
      ui.pblend = &bf, ui.dwFlags = ULW_ALPHA, ui.prcDirty = shown ? &dirty : NULL;
      if (shown && IsRectEmpty(&dirty)) {
        /* нечего обновлять */
      } else if (!UpdateLayeredWindowIndirect(w, &ui) && shown) {
        UpdateLayeredWindow(w, NULL, &pd, &sz, dc, &ps0, 0, &bf, ULW_ALPHA);
      }
      if (!shown) ShowWindow(w, SW_SHOWNOACTIVATE), shown = 1;
      QueryPerformanceCounter(&r1);
      double ms = (double)(r1.QuadPart - r0.QuadPart) * 1000.0 / fq.QuadPart;
      cost = cost * 0.95 + ms * 0.05; /* с нуля: первый, самый дорогой кадр не пугает */
      frames++;
    }
    /* до следующего кадра: ждём таймер, но просыпаемся и на сообщения окна.
       Дорогой кадр (большой экран, слабый процессор) — кадров реже: 30, 20 или 15
       в секунду, чтобы не занять ядро целиком; бой идёт с той же скоростью */
    int every = battery || cost > 10 ? 2 : 1;
    if (cost > 20) every = 3;
    if (cost > 35) every = 4;
    double wait = every * step - acc;
    if (wait < 0.001) wait = 0.001;
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(wait * 1e7);
    if (tm && SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE))
      MsgWaitForMultipleObjects(1, &tm, FALSE, 100, QS_ALLINPUT);
    else
      Sleep((DWORD)(wait * 1000) + 1);
  }
  ShowWindow(w, SW_HIDE);
  if (why == 2) { /* сам: снова — только после того, как тронут мышь или клавиатуру */
    GetLastInputInfo(&li);
    g_ifLatchInput = li.dwTime;
    g_ifLatched = 1;
  }
  if (pn) UnregisterPowerSettingNotification(pn);
  if (tm) CloseHandle(tm);
  DestroyWindow(w);
  SelectObject(dc, oldBm);
  DeleteObject(bm);
  DeleteDC(dc);
  if_scene_free(s);
  return 0;
}

static void if_launch(int demo) {
  if (g_ifThread) return;
  InterlockedExchange(&g_ifQuit, 0);
  InterlockedExchange(&g_ifScreenOff, 0);
  g_ifThread = CreateThread(NULL, 0, if_thread, (LPVOID)(INT_PTR)demo, 0, NULL);
}

/* главный поток, раз в секунду: не пора ли начать; не кончился ли бой */
static void idle_fight_tick(void) {
  if (InterlockedExchange(&g_ifWatchOff, 0) && g_idleWatch) set_idle_watch(0);
  if (g_ifThread) {
    if (WaitForSingleObject(g_ifThread, 0) != WAIT_OBJECT_0) return;
    CloseHandle(g_ifThread);
    g_ifThread = NULL;
    return;
  }
  if (g_idleWatch) { /* режим просмотра: бой идёт всегда, ввод его не прячет */
    if (!if_blocked(1)) if_launch(3);
    return;
  }
  if (g_idleSec <= 0) return;
  LASTINPUTINFO li;
  li.cbSize = sizeof(li);
  if (!GetLastInputInfo(&li)) return;
  if (g_ifLatched) {
    if (li.dwTime == g_ifLatchInput) return;
    g_ifLatched = 0;
  }
  if (GetTickCount() - li.dwTime < (DWORD)g_idleSec * 1000u) return;
  if (if_blocked(1)) return;
  if_launch(0);
}

static void idle_fight_stop(BOOL wait) {
  if (!g_ifThread) return;
  InterlockedExchange(&g_ifQuit, 1);
  if (!wait) return;
  /* не дождались — поток оставляем на учёте: его соберёт idle_fight_tick, а новый бой
     не запустится поверх (иначе старый остался бы на экране без хозяина) */
  if (WaitForSingleObject(g_ifThread, 5000) != WAIT_OBJECT_0) return;
  CloseHandle(g_ifThread);
  g_ifThread = NULL;
}

/* «Показать бой сейчас» в Настройках */
static void idle_fight_demo(void) {
  if (!g_ifThread) if_launch(g_idleWatch ? 3 : 1);
}

/* режим просмотра: включить — бой сразу и уже не прячется от мыши; выключить — убрать */
static void idle_fight_watch(int on) {
  g_idleWatch = on;
  idle_fight_stop(TRUE);
  if (on && !if_blocked(1)) if_launch(3);
}

#ifdef SET_TEST
/* только проверочная сборка.
   --idlefight папка — кадры боя поверх выдуманного экрана 1920×1040: каждый
   приём и техника на своём кадре, территории, нокаут, победа (вырезка 1280×720
   вокруг бойцов; папка — путь Windows). --idlebench файл — сколько стоит кадр на
   том же экране: среднее и худшее, отдельно в территории; заодно проверка, что
   всё нарисованное попало в «грязный» прямоугольник. --idledemo (в cursorpad.c) —
   настоящее окно боя, как по кнопке, но ввод его не останавливает: снимать экран
   в Wine. --idlestage папка — два монитора и окна (ниже) */
enum { SH_MV, SH_EF, SH_DOM, SH_RP, SH_ACT, SH_END, SH_ADAPT, SH_DIE, SH_MAHO, SH_MST, SH_MAIR, SH_TAG, SH_FLANK, SH_SHADOW, SH_PINCH };
typedef struct {
  int kind, a, b;
  float at;
  const char *name;
} IfShot;
static const IfShot kIfShots[] = {
    {SH_RP, RP_INTRO, 1, 22, "intro_gojo"},     {SH_RP, RP_INTRO, 1, 44, "intro_sukuna"},
    {SH_RP, RP_INTRO, 1, 60, "intro_round"},    {SH_RP, RP_INTRO, 1, 124, "intro_fight"},
    {SH_MV, MV_G_JAB, 0, 7, "g_jab"},           {SH_MV, MV_G_PALM, 0, 8, "g_palm"},
    {SH_MV, MV_G_KICK, 0, 9, "g_kick"},         {SH_MV, MV_G_HIGH, 0, 10, "g_high"},
    {SH_MV, MV_G_UPKICK, 0, 10, "g_upkick"},    {SH_MV, MV_G_FLICK, 0, 9, "g_flick"},
    {SH_MV, MV_G_DIVE, 0, 12, "g_dive"},        {SH_EF, EF_TELE, 1, 3, "g_teleport"},
    {SH_EF, EF_INF, 0, 4, "g_infinity"},        {SH_MV, MV_G_BLUE, 0, 7, "g_blue_1"},
    {SH_MV, MV_G_BLUE, 0, 36, "g_blue_2"},      {SH_EF, EF_IMPLODE, 0, 5, "g_blue_3"},
    {SH_MV, MV_G_RED, 0, 14, "g_red_1"},        {SH_MV, MV_G_RED, 0, 30, "g_red_2"},
    {SH_MV, MV_G_RED, 0, 38, "g_red_3"},        {SH_EF, EF_REDBOOM, 0, 4, "g_red_4"},
    {SH_MV, MV_G_PURPLE, 0, 24, "g_purple_1"},  {SH_MV, MV_G_PURPLE, 0, 52, "g_purple_2"},
    {SH_MV, MV_G_PURPLE, 0, 76, "g_purple_3"},  {SH_MV, MV_G_PURPLE, 0, 88, "g_purple_4"},
    {SH_MV, MV_G_PURPLE, 0, 102, "g_purple_5"}, {SH_MV, MV_G_PURPLE, 0, 118, "g_purple_6"},
    {SH_MV, MV_G_VOID, 0, 16, "g_void_seal"},   {SH_DOM, DOM_VOID, DP_CAST, 12, "void_1"},
    {SH_DOM, DOM_VOID, DP_OPEN, 14, "void_2"},  {SH_DOM, DOM_VOID, DP_ON, 50, "void_3"},
    {SH_DOM, DOM_VOID, DP_ON, 200, "void_4"},   {SH_EF, EF_BF, 0, 3, "black_flash"},
    {SH_MV, MV_S_CLAW, 0, 8, "s_claw"},         {SH_MV, MV_S_SWIPE, 0, 7, "s_swipe"},
    {SH_MV, MV_S_PUNCH, 0, 7, "s_punch"},       {SH_MV, MV_S_KNEE, 0, 6, "s_knee"},
    {SH_MV, MV_S_UPPER, 0, 9, "s_upper"},       {SH_MV, MV_S_KICK, 0, 9, "s_kick"},
    {SH_MV, MV_S_SLAM, 0, 14, "s_slam"},        {SH_EF, EF_CRACK, 0, 6, "s_crack"},
    {SH_MV, MV_S_DISM, 0, 16, "s_dism_1"},      {SH_MV, MV_S_DISM, 0, 26, "s_dism_2"},
    {SH_EF, EF_CUTS, 0, 3, "s_dism_3"},         {SH_MV, MV_S_CLEAVE, 0, 6, "s_cleave_1"},
    {SH_EF, EF_CROSS, 0, 3, "s_cleave_2"},      {SH_MV, MV_S_SHRINE, 0, 10, "s_shrine_seal"},
    {SH_EF, EF_GUARD, 0, 2, "guard"},           {SH_EF, EF_WALL, 0, 3, "wall"},
    {SH_MV, MV_G_REDX, 0, 40, "g_redx"},        {SH_MV, MV_G_LUNGE, 0, 6, "g_lunge"},
    {SH_MV, MV_S_FAN, 0, 36, "s_fan"},          {SH_MV, MV_S_LUNGE, 0, 5, "s_lunge"},
    {SH_EF, EF_WCUT, 0, 3, "s_world_1"},        {SH_EF, EF_WCUT, 0, 18, "s_world_2"},
    {SH_MV, MV_G_TAUNT, 0, 40, "g_taunt"},      {SH_MV, MV_S_TAUNT, 0, 30, "s_taunt"},
    {SH_RP, RP_KO, -1, 20, "ko"},               {SH_RP, RP_WIN, -1, 70, "win"},
    {SH_RP, RP_RESET, -1, 40, "heal"},
    /* сценарий: столкновение куполов, выгорание, обратная техника, Махорага, развязки */
    {SH_DOM, DOM_CLASH, DP_CAST, 14, "clash_0"},   {SH_DOM, DOM_CLASH, DP_OPEN, 14, "clash_1"},
    {SH_DOM, DOM_CLASH, DP_ON, 30, "clash_2"},     {SH_DOM, DOM_CLASH, DP_ON, 120, "clash_3"},
    {SH_DOM, DOM_CLASH, DP_BREAK, 6, "clash_break_1"}, {SH_DOM, DOM_CLASH, DP_BREAK, 20, "clash_break_2"},
    {SH_ACT, SC_BURN, 0, 150, "burn_brawl"},    {SH_MV, MV_G_RCT, 0, 12, "g_rct_1"},
    {SH_MV, MV_G_RCT, 0, 40, "g_rct_2"},        {SH_MV, MV_S_MAHO, 0, 20, "s_summon_1"},
    {SH_MV, MV_S_MAHO, 0, 44, "s_summon_2"},    {SH_ACT, SC_MAHO, 0, 60, "maho_1"},
    {SH_ACT, SC_MAHO, 0, 420, "maho_2"},        {SH_MAHO, MM_PUNCH, 0, 15, "maho_punch"},
    {SH_MAHO, MM_SLAM, 0, 12, "maho_slam_1"},   {SH_MAHO, MM_SLAM, 0, 21, "maho_slam_2"},
    {SH_MAHO, MM_SWEEP, 0, 18, "maho_sweep"},   {SH_MAIR, 0, 0, 70, "maho_stomp_1"},
    {SH_MAHO, MM_STOMP, 0, 43, "maho_stomp_2"}, {SH_MST, MS_WALK, 0, 30, "maho_walk"},
    {SH_MST, MS_HIT, 0, 2, "maho_hit"},         {SH_MST, MS_STAGGER, 0, 6, "maho_stagger"},
    {SH_ADAPT, 1, 0, 4, "maho_adapt_1"},
    {SH_ADAPT, 4, 0, 4, "maho_adapt_4"},        {SH_DIE, 0, 0, 6, "maho_die_1"},
    {SH_DIE, 0, 0, 24, "maho_die_2"},           {SH_ACT, SC_WORLD, 0, 30, "world_wait"},
    {SH_END, 0, 0, 3, "end_gojo_1_dodge"},      {SH_END, 0, 0, 6, "end_gojo_2_miss"},
    {SH_END, 0, 0, 29, "end_gojo_3_palm"},      {SH_END, 0, 0, 40, "end_gojo_4_bf"},
    {SH_END, 1, 0, 7, "end_sukuna_1_cut"},      {SH_END, 1, 0, 16, "end_sukuna_2"},
    {SH_END, 1, 0, 60, "end_sukuna_3"},
    /* Махорага с Сукуной вдвоём: новые приёмы великана, клещи, подхват, колесо в тени */
    {SH_MAHO, MM_UPPER, 0, 17, "maho_upper"},   {SH_MAHO, MM_CHARGE, 0, 22, "maho_charge"},
    {SH_MAHO, MM_BLADE, 0, 12, "maho_blade_1"}, {SH_MAHO, MM_BLADE, 0, 20, "maho_blade_2"},
    {SH_MST, MS_GUARD, 0, 6, "maho_guard"},     {SH_TAG, 0, 0, 24, "tag_chase"},
    {SH_FLANK, 0, 0, 22, "s_flank_1"},          {SH_FLANK, 0, 0, 44, "s_flank_2"},
    {SH_PINCH, 0, 0, 0, "pincer"},              {SH_SHADOW, 0, 0, 30, "shadow_wheel"},
    {SH_MV, MV_S_WORLD, 0, 10, "s_world_wheel"},
    {SH_MV, MV_G_HEAL, 0, 24, "g_heal"},        {SH_MV, MV_S_RCT, 0, 24, "s_heal"},
};
#define IF_SHOTS ((int)(sizeof(kIfShots) / sizeof(kIfShots[0])))

/* кадр подходит под снимок: от at и ещё чуть-чуть (бой мог стоять в «стоп-кадре» удара) */
static int if_shot_on(const IfScene *s, const IfShot *h) {
  switch (h->kind) {
  case SH_MV:
    for (int i = 0; i < 2; i++) {
      const IfFighter *F = &s->F[i];
      if (F->st == FS_MOVE && F->mv == h->a && F->mt >= h->at && F->mt < h->at + 2.5f) return 1;
    }
    return 0;
  case SH_EF:
    if (h->b == 1 && s->phase != RP_FIGHT) return 0; /* не выход на арену, а в бою */
    for (int i = 0; i < IF_MAXE; i++)
      if (s->ef[i].kind == h->a && s->ef[i].t >= h->at && s->ef[i].t < h->at + 2.5f) return 1;
    return 0;
  case SH_DOM:
    return s->dom == h->a && s->domPhase == h->b && s->domT >= h->at && s->domT < h->at + 2.5f;
  case SH_ACT:
    return s->phase == RP_FIGHT && s->act == h->a && s->actT >= h->at && s->actT < h->at + 2.5f;
  case SH_END:
    return s->act == SC_END && s->fate == h->a && s->actT >= h->at && s->actT < h->at + 2.5f;
  case SH_ADAPT:
    return s->adapt == h->a && s->adaptT > 30 - h->at - 2.5f && s->adaptT <= 30 - h->at;
  case SH_DIE:
    return s->mahoDie > 40 - h->at - 2.5f && s->mahoDie <= 40 - h->at;
  case SH_MAHO:
    return s->maho && s->mahoSt == MS_ATK && s->mahoMv == h->a && s->mahoMt >= h->at && s->mahoMt < h->at + 2.5f;
  case SH_MST:
    return s->maho && s->mahoSt == h->a && s->mahoMt >= h->at && s->mahoMt < h->at + 2.5f;
  case SH_MAIR:
    return s->maho && s->mahoSt == MS_ATK && s->mahoMv == MM_STOMP && s->mahoY >= h->at;
  case SH_TAG:
    return s->maho && s->tagT > 0 && s->tagT <= 70 - h->at && s->F[1].st != FS_IDLE && s->F[1].st != FS_WALK;
  case SH_FLANK:
    return s->maho && s->flankT > 0 && s->F[1].st == FS_JUMP && s->F[1].stT >= h->at && s->F[1].stT < h->at + 2.5f;
  case SH_SHADOW:
    return !s->maho && s->shadowT > 80 - h->at - 2.5f && s->shadowT <= 80 - h->at;
  case SH_PINCH: {
    const IfFighter *G = &s->F[0], *K = &s->F[1];
    return if_maho_on(s) && s->act == SC_MAHO && (K->x - G->x) * (s->mahoX - G->x) < 0 && fabsf(K->x - G->x) < 70 &&
           fabsf(s->mahoX - G->x) < 70 && (K->st == FS_MOVE || s->mahoSt == MS_ATK);
  }
  default:
    return s->phase == h->a && (h->b < 0 || s->round == h->b) && s->phT >= h->at && s->phT < h->at + 2.5f;
  }
}

/* холст поверх экрана; вырезка CW×CH вокруг бойцов (по низу — земля) */
static void if_shot_write(const IfScene *s, const BYTE *scr, BYTE *img, int CW, int CH, const wchar_t *path) {
  float lo = fminf(s->F[0].x, s->F[1].x), hi = fmaxf(s->F[0].x, s->F[1].x);
  if (s->maho) lo = fminf(lo, s->mahoX), hi = fmaxf(hi, s->mahoX); /* Махорага тоже в кадре */
  float mid = (lo + hi) * 0.5f * s->U * s->k;
  int Wb = s->big.w, ox = (int)mid - CW / 2;
  if (ox < 0) ox = 0;
  if (ox > Wb - CW) ox = Wb - CW;
  int oy = s->big.h - CH;
  for (int y = 0; y < CH; y++)
    for (int x = 0; x < CW; x++) {
      size_t i = ((size_t)(y + oy) * Wb + x + ox) * 4;
      const BYTE *p = s->big.px + i, *b = scr + i;
      BYTE *d = img + ((size_t)y * CW + x) * 4;
      int k = 255 - p[3];
      for (int q = 0; q < 3; q++) {
        int v = p[q] + (b[q] * k + 127) / 255;
        d[q] = (BYTE)(v > 255 ? 255 : v);
      }
      d[3] = 255;
    }
  st_write_bmp(path, img, CW, CH);
}

/* кадр целиком заново (для снимков) */
static void if_frame_all(IfScene *s) {
  SetRect(&s->prevS, 0, 0, s->W, s->H);
  SetRect(&s->prevO, 0, 0, s->big.w, s->big.h);
  if_frame(s);
}

static void if_test_shots(const wchar_t *dir) {
  int W = 1920, H = 1040, CW = 1280, CH = 720, wins[2] = {0, 0};
  BYTE *px = (BYTE *)calloc((size_t)W * H, 4), *scr = (BYTE *)malloc((size_t)W * H * 4), *img = (BYTE *)malloc((size_t)CW * CH * 4);
  IfScene *s = px && scr && img ? if_scene_new(W, H, px, 20261014u, wins, NULL, if_pick_k(W, H)) : NULL;
  if (!s) return;
  st_fake_screen(scr, W, H);
  int was[IF_SHOTS], got[IF_SHOTS], left = IF_SHOTS;
  wchar_t lp[MAX_PATH];
  swprintf(lp, MAX_PATH, L"%ls\\shots.txt", dir);
  FILE *log = _wfopen(lp, L"w");
  memset(was, 0, sizeof(was));
  memset(got, 0, sizeof(got));
  for (long f = 0; f < 60L * 60 * 40 && left > 0; f++) {
    if_step(s);
    if_dom_build(s); /* фон территории строится по кадру, как в окне */
    for (int k = 0; k < IF_SHOTS; k++) {
      int on = if_shot_on(s, &kIfShots[k]);
      if (on && !was[k] && !got[k]) {
        if_frame_all(s);
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\%02d_%hs.bmp", dir, k, kIfShots[k].name);
        if_shot_write(s, scr, img, CW, CH, path);
        if (log) {
          fprintf(log, "%02d %s: frame %ld", k, kIfShots[k].name, s->frame);
          for (int i = 0; i < 2; i++)
            fprintf(log, " | F%d x %.1f y %.1f st %d mv %d mt %.1f", i, s->F[i].x, s->F[i].y, s->F[i].st, s->F[i].mv,
                    s->F[i].mt);
          for (int j = 0; j < IF_MAXJ; j++)
            if (s->pj[j].kind) fprintf(log, " | pj%d %.0f,%.0f", s->pj[j].kind, s->pj[j].x, s->pj[j].y);
          for (int j = 0; j < IF_MAXE; j++)
            if (s->ef[j].kind) fprintf(log, " | ef%d %.0f,%.0f t%.0f", s->ef[j].kind, s->ef[j].x, s->ef[j].y, s->ef[j].t);
          fprintf(log, "\n");
        }
        got[k] = 1, left--;
      }
      was[k] = on;
    }
  }
  if (log) {
    for (int k = 0; k < IF_SHOTS; k++)
      if (!got[k]) fprintf(log, "missing %s\n", kIfShots[k].name);
    fprintf(log, "rounds %d, score %d:%d\n", s->round, wins[0], wins[1]);
    fclose(log);
  }
  if_scene_free(s);
  free(px), free(scr), free(img);
}

static int if_cmpf(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return x < y ? -1 : x > y;
}

static void if_test_bench(const wchar_t *path) {
  int W = 1920, H = 1040, wins[2] = {0, 0};
  long N = 60L * 60 * 12;
  const char *mins = getenv("IFBENCH_MIN"); /* короче — для быстрой прикидки темпа */
  if (mins && atoi(mins) > 0) N = 60L * 60 * atoi(mins);
  BYTE *px = (BYTE *)calloc((size_t)W * H, 4);
  float *ms = (float *)malloc(sizeof(float) * N), *md = (float *)malloc(sizeof(float) * N);
  IfScene *s = px && ms && md ? if_scene_new(W, H, px, 777u, wins, NULL, if_pick_k(W, H)) : NULL;
  if (!s) return;
  LARGE_INTEGER fq, t0, t1, t2;
  QueryPerformanceFrequency(&fq);
  long nd = 0, bad = 0, checks = 0, stc[20], phc[8], inc[8];
  memset(stc, 0, sizeof(stc)), memset(phc, 0, sizeof(phc)), memset(inc, 0, sizeof(inc));
  double sumStep = 0, area = 0, maxStep = 0;
  for (long f = 0; f < N; f++) {
    QueryPerformanceCounter(&t0);
    if_step(s);
    QueryPerformanceCounter(&t1);
    RECT dirty = if_frame(s);
    QueryPerformanceCounter(&t2);
    if (s->phase >= 0 && s->phase < 8) phc[s->phase]++;
    for (int i = 0; i < 2 && s->phase == RP_FIGHT; i++) {
      if (s->F[i].st >= 0 && s->F[i].st < 20) stc[s->F[i].st]++;
      if (s->F[i].st <= FS_RUN && s->F[i].intent >= 0 && s->F[i].intent < 8) inc[s->F[i].intent]++;
    }
    RECT cur = {s->prevS.left * s->k, s->prevS.top * s->k, s->prevS.right * s->k, s->prevS.bottom * s->k};
    UnionRect(&cur, &cur, &s->prevO);
    double st = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart;
    ms[f] = (float)((double)(t2.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart);
    if (s->dom) md[nd++] = ms[f];
    sumStep += st;
    if (st > maxStep) maxStep = st;
    area += (double)(dirty.right - dirty.left) * (dirty.bottom - dirty.top) / ((double)W * H);
    if (f % 97 == 0) { /* всё ли нарисованное внутри «грязного» прямоугольника */
      checks++;
      for (int y = 0; y < H; y++) {
        const DWORD *row = (const DWORD *)(px + (size_t)y * W * 4);
        for (int x = 0; x < W; x++)
          if (row[x] && (x < cur.left || x >= cur.right || y < cur.top || y >= cur.bottom)) {
            bad++;
            y = H;
            break;
          }
      }
    }
  }
  double sum = 0;
  for (long f = 0; f < N; f++) sum += ms[f];
  qsort(ms, N, sizeof(float), if_cmpf);
  qsort(md, nd, sizeof(float), if_cmpf);
  FILE *fo = _wfopen(path, L"w");
  if (fo) {
    fprintf(fo, "screen %dx%d (pixel %d), %ld frames (%.1f min), rounds %d, score %d:%d\n", W, H, s->k, N, N / 3600.0, s->round, wins[0],
            wins[1]);
    fprintf(fo, "frame ms: avg %.2f  p50 %.2f  p95 %.2f  p99 %.2f  max %.2f\n", sum / N, ms[N / 2], ms[N * 95 / 100],
            ms[N * 99 / 100], ms[N - 1]);
    if (nd)
      fprintf(fo, "domain frames %ld: p50 %.2f  p95 %.2f  p99 %.2f  max %.2f\n", nd, md[nd / 2], md[nd * 95 / 100],
              md[nd * 99 / 100], md[nd - 1]);
    fprintf(fo, "sim step ms: avg %.3f  max %.3f\n", sumStep / N, maxStep);
    fprintf(fo, "dirty area avg %.1f%% of screen\n", area * 100.0 / N);
    fprintf(fo, "touch check: %ld of %ld frames drew outside the dirty rect\n", bad, checks);
    fprintf(fo, "stats: KO %d, black flash %d, infinity %d\n", s->statKO, s->statBF, s->statInf);
    fprintf(fo, "phases:");
    for (int i = 0; i < 8; i++) fprintf(fo, " %.1f%%", phc[i] * 100.0 / N);
    fprintf(fo, "\nfighter states in fight:");
    for (int i = 0; i < 16; i++) fprintf(fo, " %d:%.1f%%", i, stc[i] * 50.0 / (phc[RP_FIGHT] + 1));
    fprintf(fo, "\nintents when free:");
    for (int i = 0; i < 5; i++) fprintf(fo, " %d:%.1f%%", i, inc[i] * 50.0 / (phc[RP_FIGHT] + 1));
    fprintf(fo, "\n");
    for (int m = 0; m < MV_COUNT; m++) fprintf(fo, "%d ", s->stat[m]);
    fprintf(fo, "\n");
    fclose(fo);
  }
  if_scene_free(s);
  free(px), free(ms), free(md);
}
/* --idlelog файл — ход нескольких раундов без рисования: каждые 5 кадров акт, здоровье обоих и Махораги,
   кто что делает; по нему видно, плавно ли убывает здоровье и кто бьёт, когда встал Махорага */
static void if_test_hplog(const wchar_t *path) {
  int W = 1920, H = 1040, wins[2] = {0, 0}, rounds = 4;
  const char *rs = getenv("IFLOG_ROUNDS"), *ss = getenv("IFLOG_SEED");
  if (rs && atoi(rs) > 0) rounds = atoi(rs);
  int dom = getenv("IFLOG_DOM") != NULL;
  BYTE *px = (BYTE *)calloc((size_t)W * H, 4);
  IfScene *s = px ? if_scene_new(W, H, px, ss ? (unsigned)strtoul(ss, NULL, 10) : 20261014u, wins, NULL, if_pick_k(W, H)) : NULL;
  FILE *fo = s ? _wfopen(path, L"w") : NULL;
  if (!fo) return;
  fprintf(fo, "round frame phase act actT ghp khp maho mst mmv mhp adapt gst gmv kst kmv fate gx kx gin kin gfl kfl\n");
  for (long f = 0; f < 60L * 60 * 30 && s->round <= rounds; f++) {
    if_step(s);
    if (dom) if_dom_build(s); /* как в --idlefight: фон территории строится по кадру */
    if (f % 5) continue;
    fprintf(fo, "%d %ld %d %d %.0f %.0f %.0f %d %d %d %.0f %d %d %d %d %d %d", s->round, s->frame, s->phase, s->act, s->actT,
            s->F[0].hp, s->F[1].hp, s->maho, s->mahoSt, s->mahoMv, s->mahoHp, s->adapt, s->F[0].st, s->F[0].mv, s->F[1].st,
            s->F[1].mv, s->fate);
    /* где стоят, чего хотят, на каком полу */
    fprintf(fo, " %.0f %.0f %d %d %.0f %.0f\n", s->F[0].x, s->F[1].x, s->F[0].intent, s->F[1].intent, s->F[0].fl, s->F[1].fl);
  }
  fprintf(fo, "score %d:%d\n", wins[0], wins[1]);
  fclose(fo);
  if_scene_free(s);
  free(px);
}
/* --idlestage папка — бой на выдуманных двух мониторах с окнами: левый 1920×1040
   (главный), правый 1280×840 выше и короче; три окна. Кадр всего холста каждые
   полторы секунды и счёт: сколько бойцы были на окнах, на втором мониторе, сколько
   раз прыгали наверх и сколько снарядов разбилось об окна.
   --idlestage3 папка — три монитора: левый ниже главного, главный посередине,
   правый меньше и ниже; кадры вдвое меньше */
static void if_test_stage(const wchar_t *dir, int three) {
  static IfStage st;
  memset(&st, 0, sizeof(st));
  int W = 3200, H = 1040, wins[2] = {0, 0}, half = three;
  if (three) {
    W = 5120, H = 1120;
    SetRect(&st.mon[0], 0, 80, 1920, 1120), SetRect(&st.mon[1], 1920, 0, 3840, 1040), SetRect(&st.mon[2], 3840, 200, 5120, 1000);
    st.nMon = 3, st.prim = 1;
    SetRect(&st.win[0], 2400, 140, 3300, 900); /* высокие окна: верх почти у края экрана */
    SetRect(&st.win[1], 300, 260, 1300, 1000);
    SetRect(&st.win[2], 4100, 330, 4900, 900);
  } else {
    SetRect(&st.mon[0], 0, 0, 1920, 1040), SetRect(&st.mon[1], 1920, 60, 3200, 900);
    st.nMon = 2, st.prim = 0;
    SetRect(&st.win[0], 1060, 420, 1700, 900); /* сверху по Z: закрывает край нижнего */
    SetRect(&st.win[1], 300, 330, 1200, 800);
    SetRect(&st.win[2], 2250, 470, 2950, 880);
  }
  st.nWin = 3;
  BYTE *px = (BYTE *)calloc((size_t)W * H, 4), *scr = (BYTE *)malloc((size_t)W * H * 4), *img = (BYTE *)malloc((size_t)W * H * 4);
  IfScene *s = px && scr && img ? if_scene_new(W, H, px, 4242u, wins, &st, if_pick_k(1920, 1040)) : NULL;
  if (!s) return;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      BYTE *d = scr + ((size_t)y * W + x) * 4;
      int on = 0, win = -1;
      for (int m = 0; m < st.nMon; m++) on |= PtInRect(&st.mon[m], (POINT){x, y});
      for (int k = st.nWin - 1; k >= 0; k--)
        if (PtInRect(&st.win[k], (POINT){x, y})) win = k;
      int bar = win >= 0 && y < st.win[win].top + 30;
      d[0] = (BYTE)(!on ? 20 : bar ? 200 : win >= 0 ? 245 - win * 12 : 120 + 100 * y / H);
      d[1] = (BYTE)(!on ? 20 : bar ? 120 : win >= 0 ? 245 - win * 12 : 80 + 60 * y / H);
      d[2] = (BYTE)(!on ? 20 : bar ? 40 : win >= 0 ? 245 - win * 12 : 40 + 30 * x / 1920);
      d[3] = 255;
    }
  wchar_t lp[MAX_PATH];
  swprintf(lp, MAX_PATH, L"%ls\\stage.txt", dir);
  FILE *log = _wfopen(lp, L"w");
  if (log) {
    for (int i = 0; i < s->nPl; i++)
      fprintf(log, "plat %d: x %.0f..%.0f top %.1f bot %.1f solid %d wl %d wr %d\n", i, s->pl[i].x0, s->pl[i].x1, s->pl[i].top,
              s->pl[i].bot, s->pl[i].solid, s->pl[i].wl, s->pl[i].wr);
  }
  long onWin[2] = {0, 0}, onM2[2] = {0, 0}, leaps[2] = {0, 0}, winBoom = 0, N = 60L * 60 * 8, shots = 0;
  long onMon[IF_MAXMON][2], arenaT[IF_MAXMON];
  memset(onMon, 0, sizeof(onMon)), memset(arenaT, 0, sizeof(arenaT));
  float m2 = 1920.0f / s->k / s->U;
  int wasJump[2] = {0, 0};
  for (long f = 0; f < N; f++) {
    int pj[IF_MAXJ];
    for (int j = 0; j < IF_MAXJ; j++) pj[j] = s->pj[j].kind;
    if_step(s);
    if_dom_build(s);
    for (int j = 0; j < IF_MAXJ; j++) /* снаряд пропал не у края экрана и не у земли — разбился об окно или попал */
      if ((pj[j] == PJ_RED || pj[j] == PJ_ARROW) && s->pj[j].kind == PJ_NONE) {
        const IfProj *p = &s->pj[j];
        for (int i = s->nFix; i < s->nPl; i++) {
          const IfPlat *q = &s->pl[i];
          if (p->x > q->x0 - 12 && p->x < q->x1 + 12 && p->y > q->bot - 12 && p->y < q->top + 12 &&
              (fabsf(p->x - q->x0) < 12 || fabsf(p->x - q->x1) < 12 || fabsf(p->y - q->top) < 12)) {
            winBoom++;
            break;
          }
        }
      }
    for (int i = 0; i < 2; i++) {
      IfFighter *F = &s->F[i];
      if (F->fl > if_ground(s, F->x) + 0.5f && F->y - F->fl < 1) onWin[i]++;
      if (F->x > m2) onM2[i]++;
      onMon[if_mon_at(s, F->x)][i]++;
      int jmp = F->st == FS_JUMP && F->vy > 9;
      if (jmp && !wasJump[i]) leaps[i]++;
      wasJump[i] = jmp;
    }
    arenaT[s->arena]++;
    if (f % 90 == 0 && shots < (half ? 40 : 60)) {
      if_frame_all(s);
      for (size_t k = 0; k < (size_t)W * H; k++) {
        const BYTE *p = px + k * 4, *b = scr + k * 4;
        BYTE *d = img + k * 4;
        int a = 255 - p[3];
        for (int q = 0; q < 3; q++) {
          int v = p[q] + (b[q] * a + 127) / 255;
          d[q] = (BYTE)(v > 255 ? 255 : v);
        }
        d[3] = 255;
      }
      wchar_t path[MAX_PATH];
      swprintf(path, MAX_PATH, L"%ls\\stage_%02ld.bmp", dir, shots);
      if (half) { /* вдвое меньше: среднее из четырёх */
        int w2 = W / 2, h2 = H / 2;
        for (int y = 0; y < h2; y++)
          for (int x = 0; x < w2; x++)
            for (int q = 0; q < 4; q++) {
              const BYTE *a = img + ((size_t)(y * 2) * W + x * 2) * 4 + q, *b = a + (size_t)W * 4;
              img[((size_t)y * w2 + x) * 4 + q] = (BYTE)((a[0] + a[4] + b[0] + b[4] + 2) / 4);
            }
        st_write_bmp(path, img, w2, h2);
      } else {
        st_write_bmp(path, img, W, H);
      }
      if (log)
        fprintf(log, "shot %02ld frame %ld: G x %.0f y %.1f fl %.1f st %d | S x %.0f y %.1f fl %.1f st %d | dom %d\n", shots, f,
                s->F[0].x, s->F[0].y, s->F[0].fl, s->F[0].st, s->F[1].x, s->F[1].y, s->F[1].fl, s->F[1].st, s->dom);
      shots++;
    }
  }
  if (log) {
    fprintf(log, "frames %ld, rounds %d, score %d:%d\n", N, s->round, wins[0], wins[1]);
    fprintf(log, "on window: G %.1f%% S %.1f%%; on monitor 2: G %.1f%% S %.1f%%; high leaps G %ld S %ld; window booms %ld\n",
            onWin[0] * 100.0 / N, onWin[1] * 100.0 / N, onM2[0] * 100.0 / N, onM2[1] * 100.0 / N, leaps[0], leaps[1], winBoom);
    for (int m = 0; m < s->nMon; m++)
      fprintf(log, "monitor %d: G %.1f%% S %.1f%%, arena %.1f%%\n", m, onMon[m][0] * 100.0 / N, onMon[m][1] * 100.0 / N,
              arenaT[m] * 100.0 / N);
    for (int m = 0; m < MV_COUNT; m++) fprintf(log, "%d ", s->stat[m]);
    fprintf(log, "\n");
    fclose(log);
  }
  if_scene_free(s);
  free(px), free(scr), free(img);
}
/* --figsheet файл.bmp — бойцы крупно: Годжо и Сукуна в восьми позах (рост около
   500 px) и они же внизу в настоящем размере, на светлом и тёмном */
static void if_test_figs(const wchar_t *path) {
  static const int kP[8] = {FP_G_IDLE, FP_G_JAB, FP_G_RED, FP_G_PUR_C, FP_S_IDLE, FP_S_PUNCH, FP_S_DISM, FP_S_FUGA_C};
  int W = 2400, H = 1720;
  BYTE *px = (BYTE *)calloc((size_t)W * H, 4), *img = (BYTE *)malloc((size_t)W * H * 4);
  if (!px || !img) return;
  FCv cv;
  memset(&cv, 0, sizeof(cv));
  cv.px = px, cv.w = W, cv.h = H, cv.x1 = W, cv.y1 = H;
  for (int i = 0; i < 8; i++) {
    for (int big = 0; big < 2; big++) {
      FFig g;
      memset(&g, 0, sizeof(g));
      g.who = i >= 4, g.f = 1, g.p = kPoses[kP[i]], g.alpha = 1, g.blind = i < 3, g.power = i == 7;
      g.U = big ? 5.0f : 2.7f;
      g.x = big ? 160.0f + (i % 4) * 600.0f : 150.0f + i * 290.0f + (i >= 4) * 40.0f;
      g.y = big ? 600.0f + (i / 4) * 600.0f : 1690.0f;
      ff_draw(&cv, &g);
    }
  }
  { /* рядом — те же бойцы без фона, с прозрачностью (BGRA, альфа умножена): из них пиксель-арт */
    wchar_t ap[MAX_PATH];
    swprintf(ap, MAX_PATH, L"%ls.alpha.bmp", path);
    st_write_bmp(ap, px, W, H);
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      size_t k = ((size_t)y * W + x) * 4;
      int dark = y > 1250 && x > W / 2;
      int bg = dark ? 40 : 236 - y / 40;
      for (int q = 0; q < 3; q++) {
        int v = px[k + q] + (bg * (255 - px[k + 3]) + 127) / 255;
        img[k + q] = (BYTE)(v > 255 ? 255 : v);
      }
      img[k + 3] = 255;
    }
  st_write_bmp(path, img, W, H);
  free(px), free(img);
}
/* --figbench файл — сколько стоит нарисовать одного бойца (рост около 320 px) */
static void if_test_figbench(const wchar_t *path) {
  static const int kP[8] = {FP_G_IDLE, FP_G_JAB, FP_G_RED, FP_G_PUR_C, FP_S_IDLE, FP_S_PUNCH, FP_S_DISM, FP_S_FUGA_C};
  int W = 800, H = 600, N = 300;
  BYTE *px = (BYTE *)calloc((size_t)W * H, 4);
  if (!px) return;
  FCv cv;
  memset(&cv, 0, sizeof(cv));
  cv.px = px, cv.w = W, cv.h = H, cv.x1 = W, cv.y1 = H;
  LARGE_INTEGER fq, t0, t1;
  QueryPerformanceFrequency(&fq);
  double ms[8];
  for (int i = 0; i < 8; i++) {
    FFig g;
    memset(&g, 0, sizeof(g));
    g.who = i >= 4, g.f = 1, g.p = kPoses[kP[i]], g.alpha = 1, g.blind = i < 3, g.U = 3.2f, g.x = 300, g.y = 560;
    QueryPerformanceCounter(&t0);
    for (int k = 0; k < N; k++) ff_draw(&cv, &g);
    QueryPerformanceCounter(&t1);
    ms[i] = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart / N;
  }
  FILE *f = _wfopen(path, L"w");
  if (f) {
    double sum = 0;
    for (int i = 0; i < 8; i++) fprintf(f, "%.3f ", ms[i]), sum += ms[i];
    fprintf(f, "\navg %.3f ms per figure\n", sum / 8);
    fclose(f);
  }
  free(px);
}
#endif



