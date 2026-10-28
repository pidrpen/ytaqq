/* ai_avatar.c — настольный аватар мини-ИИ. Робот смоделирован в Blender
   (native/avatar/make_avatar.py), кадры состояний запечены в атлас
   ai_avatar_atlas.png и вшиты в EXE ресурсом 380. Глаза и рот на тёмном
   экране-лице программа рисует сама поверх кадра по точкам из ai_avatar_face.h:
   робот моргает, следит за курсором и меняет выражение. Пузырь рядом с ним
   показывает, чем ИИ занят фоном: строку состояния чата, шаг файловой задачи,
   начало ответа. Всё рисуется в окнах с попиксельной прозрачностью.
   Второй вид — «Кошка» (аниме-девушка с ушками и хвостом): 2D-кадры по арту
   пользователя (ComfyUI), ресурс 381 — 9 кадров лица в ряд. Лицо у неё уже
   нарисовано в кадре, поэтому эмоции — сменой кадра, а дыхание, прыжки и
   раскачивание — та же поза, что у робота.
   Третий вид — «Кошко-девочка»: та же героиня со скелетом из слоёв, ресурс 382,
   двигает руками, головой, ушами и хвостом (ai_avatar_girl.c). */
#include "ai_avatar_face.h"
#include "ai_avatar_facts.h" /* факты и анекдоты: список, выбор без повторов, разбор ответа модели */
#define IDR_AI_AVATAR_ATLAS 380
#define IDR_AI_AVATAR_CAT 381
#define IDR_AI_AVATAR_GIRL 382
#include "ai_avatar_girl.h" /* слои и скелет кошко-девочки (native/avatar/pack_catgirl.py) */
#define AC_CAT_W 135                /* кадр кошки 135×384 — в квадрате 460×460 окна: внизу посередине, */
#define AC_CAT_H 384                /* сверху запас под прыжок и потягивание, чтобы уши не обрезались */
#define AC_CAT_BOX 460
#define AC_CAT_TOP 72
#define AC_CAT_FEET 0.983f          /* подошвы: (72 + 380) / 460 */
#define AC_CAT_EYE_X 0.49f          /* середина между глаз, доля ширины окна */
#define AC_CAT_HEAD 0.16f           /* макушка, доля высоты окна */
enum { AC_SKIN_ROBOT, AC_SKIN_CAT, AC_SKIN_GIRL, AC_SKIN_N };
/* подпись в меню и значение Skin в ai_avatar.ini */
static const wchar_t *const kAcSkinName[AC_SKIN_N] = {L"Робот", L"Кошка", L"Кошко-девочка"};
static const wchar_t *const kAcSkinKey[AC_SKIN_N] = {L"robot", L"cat", L"catgirl"};
/* порядок = кадры ai_avatar_cat.png */
enum { AC_CAT_IDLE, AC_CAT_BLINK, AC_CAT_TALK, AC_CAT_HAPPY, AC_CAT_SURPRISED, AC_CAT_SAD, AC_CAT_ANGRY, AC_CAT_SHY,
       AC_CAT_WAVE, AC_CAT_N };
#define AC_AVATAR_TIMER 0xA171
#define AC_AVATAR_TICK 33           /* ~30 к/с: кадр выбирается по часам, тик только перерисовывает */
#define AC_AVATAR_FAST_TICK 16       /* пока появляется или уходит */
#define AC_AVATAR_ANIM_MS 380
#define AC_AVATAR_DEFAULT_SIZE 160
#define AC_AVATAR_SLEEP_AFTER 300000 /* засыпает через 5 минут без дела и без курсора рядом */
#define AC_AVATAR_ROAM 140           /* гуляет не дальше стольких пикселей от места, куда поставили */
#define AC_AVATAR_MIN 80             /* 50 % */
#define AC_AVATAR_MAX 384            /* 240 %: кадр 192 px, крупнее — уже мыльно */

/* порядок = строки атласа (STATES в make_avatar.py) */
enum { AC_AV_IDLE, AC_AV_LISTENING, AC_AV_THINKING, AC_AV_FILES, AC_AV_WAVE, AC_AV_SUCCESS, AC_AV_ERROR,
       AC_AV_TALKING, AC_AV_SLEEP };

static HWND g_acAvatar, g_acBubble;
static HBITMAP g_acAvatarAtlas;
static BYTE *g_acAvatarPixels;
static int g_acAvatarSkin;          /* AC_SKIN_…; в ai_avatar.ini — Skin */
static HBITMAP g_acCatAtlas;
static BYTE *g_acCatPixels;
static HBITMAP g_acGirlAtlas;       /* кошко-девочка: слои скелета (ai_avatar_girl.c) */
static BYTE *g_acGirlPixels;
static HBITMAP g_acAvatarFrameBitmap;
static BYTE *g_acAvatarFramePixels;
static int g_acAvatarSize = AC_AVATAR_DEFAULT_SIZE;
static int g_acAvatarX = -1, g_acAvatarY = -1;
static int g_acAvatarState = AC_AV_IDLE;
static int g_acAvatarFrame, g_acAvatarNext, g_acAvatarMix; /* кадр, следующий и доля перехода к нему, 0..255 */
static DWORD g_acAvatarStateAt;
static DWORD g_acAvatarNextIdle, g_acAvatarNextMove, g_acAvatarTransientUntil, g_acAvatarRng;
static BOOL g_acAvatarAmbient, g_acAvatarMoving;
static POINT g_acAvatarTarget, g_acAvatarHome;
static BOOL g_acAvatarWander = TRUE, g_acAvatarBubbles = TRUE;
static BOOL g_acAvatarBusy;          /* ИИ работает: пузырь-мысль держится, пока не закончит */
static DWORD g_acAvatarActiveAt;     /* последнее дело ИИ или курсор рядом — отсчёт до сна */
static DWORD g_acAvatarTalkUntil;    /* «рассказывает» ответ */
static DWORD g_acAvatarJoyUntil;     /* погладили — глаза-дужки */
static DWORD g_acAvatarNextBlink, g_acAvatarBlinkAt; /* моргание по времени: таймер бывает и быстрым */
static int g_acAvatarAnim;           /* призыв: 1 — появляется, 2 — уходит */
static DWORD g_acAvatarAnimAt;
static wchar_t g_acAvatarMenuText[40] = L"Призвать помощника"; /* пункт «Ещё» (tools.c) */
static float g_acAvatarLookX, g_acAvatarLookY;
static RECT g_acAvatarDragFrom;

/* Живость поверх кадров: дыхание, покачивание, прыжки и «пружинка» при
   приземлении считаются кодом 30 раз в секунду. Эмоции — выражение лица
   на время. Держат мышью — висит, держась лапой за курсор, и раскачивается. */
enum { AC_EMO_NONE, AC_EMO_SURPRISED, AC_EMO_LOVE, AC_EMO_SHY, AC_EMO_SAD, AC_EMO_ANGRY, AC_EMO_WINK, AC_EMO_DIZZY };
static int g_acEmo;
static DWORD g_acEmoUntil;
static DWORD g_acHopAt, g_acLandAt;      /* прыжок; приземление — сжатие с отскоком */
static BOOL g_acHeld, g_acHover, g_acHoverShy;
static DWORD g_acHoverAt, g_acHoverOff, g_acHeldAt;
static POINT g_acGrip;                     /* где держат, в координатах окна */
static float g_acSwing, g_acSwingV;        /* наклон, рад, и его скорость */
static int g_acLastX;
static DWORD g_acNextAct;                  /* следующая мелочь в покое */
/* «Садиться на окна»: сидит на верхнем краю окна и едет вместе с ним,
   окно закрыли или свернули — падает вниз до другого окна или панели задач */
static BOOL g_acAvatarWindows;
static HWND g_acPerch, g_acPerchNext;
static int g_acPerchDx;
static BOOL g_acFalling;
static float g_acFallV;

/* «Факты и анекдоты» (меню помощника, ai_avatar.ini Facts): щелчок — сразу рассказывает,
   сам — раз в 15 минут, когда ИИ свободен, а человек за компьютером и не в чате.
   Придумывает модель коротким запросом без размышления, но только если она уже
   поднята и свободна (ai_warm): ради шутки 4B на заводском ПК не грузится.
   Иначе и при негодном ответе — строка из встроенного списка. */
#define WM_AC_FACT (WM_APP + 90) /* ответ модели на факт или анекдот: lParam — AiResult* */
#define AC_FACT_TIMER 0xA173     /* одиночный щелчок ждёт, не окажется ли он двойным */
#define AC_FACT_EVERY (15 * 60 * 1000)
#define AC_FACT_AWAY (5 * 60 * 1000) /* дольше без мыши и клавиатуры — человека нет, рассказывать некому */
static BOOL g_acFacts;
static FjState g_acFactSt;
static DWORD g_acFactNext;
static int g_acFactId, g_acFactKind;  /* запрос к модели в пути и что просили */
static DWORD g_acLaughAt;             /* анекдот досказан — посмеяться */
static BOOL g_acBubbleStory;          /* пузырь с фактом: шире, дольше, щелчок только закрывает */

static void ac_emo(int emo, DWORD ms) {
  g_acEmo = emo;
  g_acEmoUntil = (GetTickCount() + ms) | 1;
}

static wchar_t g_acBubbleText[FJ_MAX + 8];
static BOOL g_acBubbleThought;       /* облачко-мысль (ИИ занят) или реплика с хвостиком */
static BOOL g_acBubbleFading;
static DWORD g_acBubbleUntil;        /* 0 — держать, пока ИИ занят */
static BYTE g_acBubbleAlpha;
static HBITMAP g_acBubbleBitmap;
static BYTE *g_acBubblePixels;
static int g_acBubbleW, g_acBubbleH;
static HFONT g_acBubbleFont;
static int g_acBubbleFontPx;

/* GUID_WICPixelFormat32bppPBGRA from wincodec.h: the layered window needs
   premultiplied alpha. A different GUID makes WIC reject every frame. */
static const GUID kAcAvatarPbgra = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10}};
static void ac_avatar_present(void);
static DWORD ac_avatar_random(void);
static void ac_avatar_tick(void);
static void ac_bubble_update(void);
static void ac_avatar_show(void);
static void ac_avatar_ambient(int state, DWORD now);
static DWORD ac_state_ms(int state);
static void ac_avatar_dismiss(void);
static void ac_avatar_menu_label(void);
/* мини-чат у помощника (ai_minichat.c): маленькое окно у головы робота, ездит за ним */
static BOOL ac_mini_open(void);
static void ac_mini_follow(void);
static void ac_mini_sync(void);
static void ac_mini_refresh(void);
static void ac_mini_status(const wchar_t *t);
static BOOL g_acAvatarFollowing; /* робота двигает сам мини-чат — обратно его не двигать */
static void ac_menu_close(void);

/* GetTickCount переполняется раз в 49,7 суток: сравниваем разность, а не значения */
static BOOL ac_due(DWORD now, DWORD at) { return (LONG)(now - at) >= 0; }

/* PNG из ресурса id — в 32-битный DIB с premultiplied alpha, ровно w×h */
static BOOL ac_png_resource(int id, UINT w, UINT h, HBITMAP *outBitmap, BYTE **outPixels) {
  HRSRC resource = FindResourceW(g_inst, MAKEINTRESOURCEW(id), RT_RCDATA);
  DWORD size = resource ? SizeofResource(g_inst, resource) : 0;
  HGLOBAL loaded = resource ? LoadResource(g_inst, resource) : NULL;
  const void *source = loaded ? LockResource(loaded) : NULL;
  if (!size || !source) return FALSE;

  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
  if (!memory) return FALSE;
  void *copy = GlobalLock(memory);
  if (!copy) { GlobalFree(memory); return FALSE; }
  memcpy(copy, source, size);
  GlobalUnlock(memory);

  IStream *stream = NULL;
  IWICImagingFactory *factory = NULL;
  IWICBitmapDecoder *decoder = NULL;
  IWICBitmapFrameDecode *frame = NULL;
  IWICFormatConverter *converter = NULL;
  BOOL ok = FALSE;
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (SUCCEEDED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
    memory = NULL; /* stream owns the HGLOBAL */
    if (SUCCEEDED(CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&factory)) &&
        SUCCEEDED(factory->lpVtbl->CreateDecoderFromStream(factory, stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->lpVtbl->GetFrame(decoder, 0, &frame)) &&
        SUCCEEDED(factory->lpVtbl->CreateFormatConverter(factory, &converter)) &&
        SUCCEEDED(converter->lpVtbl->Initialize(converter, (IWICBitmapSource *)frame, &kAcAvatarPbgra,
                                                WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
      UINT width = 0, height = 0;
      if (SUCCEEDED(converter->lpVtbl->GetSize(converter, &width, &height)) && width == w && height == h) {
        BITMAPINFO info;
        memset(&info, 0, sizeof(info));
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = (LONG)width;
        info.bmiHeader.biHeight = -(LONG)height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *pixels = NULL;
        HBITMAP bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
        if (bitmap && pixels && SUCCEEDED(converter->lpVtbl->CopyPixels(converter, NULL,
                                                                       width * 4, width * height * 4,
                                                                       (BYTE *)pixels))) {
          *outBitmap = bitmap;
          *outPixels = (BYTE *)pixels;
          ok = TRUE;
        } else if (bitmap) DeleteObject(bitmap);
      }
    }
  }
  if (converter) converter->lpVtbl->Release(converter);
  if (frame) frame->lpVtbl->Release(frame);
  if (decoder) decoder->lpVtbl->Release(decoder);
  if (factory) factory->lpVtbl->Release(factory);
  if (stream) stream->lpVtbl->Release(stream);
  if (memory) GlobalFree(memory);
  return ok;
}

/* атлас выбранного вида; другой не держим — у робота он ~32 МБ */
static BOOL ac_avatar_load(void) {
  if (g_acAvatarSkin == AC_SKIN_CAT)
    return g_acCatAtlas || ac_png_resource(IDR_AI_AVATAR_CAT, AC_CAT_W * AC_CAT_N, AC_CAT_H, &g_acCatAtlas, &g_acCatPixels);
  if (g_acAvatarSkin == AC_SKIN_GIRL)
    return g_acGirlAtlas || ac_png_resource(IDR_AI_AVATAR_GIRL, AC_GIRL_W, AC_GIRL_H, &g_acGirlAtlas, &g_acGirlPixels);
  return g_acAvatarAtlas || ac_png_resource(IDR_AI_AVATAR_ATLAS, AC_AVATAR_FRAME * AC_AVATAR_COLS,
                                            AC_AVATAR_FRAME * AC_AVATAR_ROWS, &g_acAvatarAtlas, &g_acAvatarPixels);
}

static void ac_avatar_schedule_idle(DWORD now) {
  g_acAvatarNextIdle = now + 12000 + (ac_avatar_random() % 14000);
  g_acAvatarNextMove = now + 20000 + (ac_avatar_random() % 22000);
}

static void ac_avatar_set(int state) {
  if (state < AC_AV_IDLE || state > AC_AV_SLEEP) state = AC_AV_IDLE;
  DWORD now = GetTickCount();
  g_acAvatarActiveAt = now;
  g_acAvatarAmbient = FALSE;
  g_acAvatarTransientUntil = 0;
  g_acAvatarMoving = FALSE;
  if (g_acAvatarState == state) {
    if (state == AC_AV_IDLE) {
      g_acAvatarStateAt = now;
      ac_avatar_schedule_idle(now);
    }
    ac_avatar_present();
    return;
  }
  if (state == AC_AV_SUCCESS && (ac_avatar_random() & 1)) ac_emo(AC_EMO_LOVE, 1300); /* иногда — сердечки */
  g_acAvatarState = state;
  g_acAvatarFrame = 0;
  g_acAvatarStateAt = now;
  if (g_acTitle) InvalidateRect(g_acTitle, NULL, FALSE); /* огонёк состояния в шапке чата */
  if (state == AC_AV_IDLE) ac_avatar_schedule_idle(now);
  ac_avatar_present();
}

/* ---------- рисование: штрихи со свечением в premultiplied BGRA */

static void ac_px_over(BYTE *px, COLORREF c, float a) {
  if (a <= 0.0f) return;
  if (a > 1.0f) a = 1.0f;
  float k = 1.0f - a;
  px[0] = (BYTE)(GetBValue(c) * a + px[0] * k + 0.5f);
  px[1] = (BYTE)(GetGValue(c) * a + px[1] * k + 0.5f);
  px[2] = (BYTE)(GetRValue(c) * a + px[2] * k + 0.5f);
  px[3] = (BYTE)(255.0f * a + px[3] * k + 0.5f);
}

static COLORREF ac_mix(COLORREF a, COLORREF b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
             (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
             (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static float ac_seg(float px, float py, float ax, float ay, float bx, float by) {
  float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
  float l = vx * vx + vy * vy, h = l > 0.0f ? (wx * vx + wy * vy) / l : 0.0f;
  if (h < 0.0f) h = 0.0f;
  if (h > 1.0f) h = 1.0f;
  float dx = wx - vx * h, dy = wy - vy * h;
  return sqrtf(dx * dx + dy * dy);
}

/* Ломаная из n точек толщиной 2r: ядро цвета core, ореол glow радиусом glowR. */
static void ac_paint_stroke(BYTE *pixels, int W, int H, const float *p, int n, float r,
                            COLORREF core, COLORREF glow, float glowR, float alpha) {
  float minX = p[0], maxX = p[0], minY = p[1], maxY = p[1];
  for (int i = 1; i < n; i++) {
    if (p[2 * i] < minX) minX = p[2 * i];
    if (p[2 * i] > maxX) maxX = p[2 * i];
    if (p[2 * i + 1] < minY) minY = p[2 * i + 1];
    if (p[2 * i + 1] > maxY) maxY = p[2 * i + 1];
  }
  float pad = r + glowR * 3.0f + 1.0f;
  int x0 = (int)floorf(minX - pad), x1 = (int)ceilf(maxX + pad);
  int y0 = (int)floorf(minY - pad), y1 = (int)ceilf(maxY + pad);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > W - 1) x1 = W - 1;
  if (y1 > H - 1) y1 = H - 1;
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      float px = x + 0.5f, py = y + 0.5f, d;
      if (n == 1) d = sqrtf((px - p[0]) * (px - p[0]) + (py - p[1]) * (py - p[1]));
      else {
        d = 1e9f;
        for (int i = 0; i + 1 < n; i++) {
          float s = ac_seg(px, py, p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3]);
          if (s < d) d = s;
        }
      }
      float e = d - r;
      BYTE *dst = pixels + ((size_t)y * W + x) * 4;
      if (e > 0.0f && glowR > 0.0f) ac_px_over(dst, glow, 0.5f * alpha * expf(-e / glowR));
      ac_px_over(dst, core, (0.5f - e) * alpha);
    }
  }
}

/* базис лица: точка + «вправо» (от левого глаза к правому) + «вниз» */
typedef struct { float ux, uy, vx, vy; } AcFaceAxes;
static void ac_face_pt(const AcFaceAxes *ax, float cx, float cy, float a, float b, float *out) {
  out[0] = cx + ax->ux * a + ax->vx * b;
  out[1] = cy + ax->uy * a + ax->vy * b;
}

/* Призыв и отзыв: масштаб вокруг точки над кольцом и прозрачность. */
static float ac_avatar_zoom(DWORD now, BYTE *alpha) {
  *alpha = 255;
  if (!g_acAvatarAnim) return 1.0f;
  float t = (float)(now - g_acAvatarAnimAt) / AC_AVATAR_ANIM_MS, c1 = 1.70158f, c3 = c1 + 1.0f;
  if (t > 1.0f) t = 1.0f;
  if (g_acAvatarAnim == 1) { /* выпрыгивает чуть больше и садится на место */
    float u = t - 1.0f;
    *alpha = (BYTE)(255.0f * (t * 2.5f < 1.0f ? t * 2.5f : 1.0f));
    return 1.0f + c3 * u * u * u + c1 * u * u;
  }
  *alpha = (BYTE)(255.0f * (1.0f - t)); /* чуть приседает и схлопывается */
  float z = 1.0f - (c3 * t * t * t - c1 * t * t);
  return z > 0.01f ? z : 0.01f;
}

/* Кадр — по часам от начала состояния, а не «следующий на каждом тике»:
   таймер Windows идёт рывками по 15,6 мс, счётчик тиков копил неровность.
   Между соседними кадрами — доля для плавного перехода. Мс на кадр: */
static const BYTE kAcAvatarFrameMs[AC_AVATAR_ROWS] = {80, 60, 50, 50, 45, 45, 45, 50, 90};
static void ac_avatar_frame(DWORD now) {
  DWORD ms = kAcAvatarFrameMs[g_acAvatarState], t = now - g_acAvatarStateAt, n = t / ms;
  g_acAvatarMix = (int)((t % ms) * 256 / ms);
  if (g_acAvatarState == AC_AV_SUCCESS || g_acAvatarState == AC_AV_ERROR) { /* один раз, потом стоит */
    if (n >= AC_AVATAR_COLS - 1) { g_acAvatarFrame = g_acAvatarNext = AC_AVATAR_COLS - 1; g_acAvatarMix = 0; return; }
    g_acAvatarFrame = (int)n;
    g_acAvatarNext = (int)n + 1;
    return;
  }
  g_acAvatarFrame = (int)(n % AC_AVATAR_COLS);
  g_acAvatarNext = (g_acAvatarFrame + 1) % AC_AVATAR_COLS;
}

/* билинейная выборка одного канала атласа p: строки r0/r1, столбцы x0/x1, доли 0..65535 */
static unsigned ac_bilerp(const BYTE *p, size_t r0, size_t r1, int x0, int x1, int wx, int wy, int c) {
  unsigned top = ((unsigned)p[(r0 + x0) * 4 + c] * (65536 - wx) + (unsigned)p[(r0 + x1) * 4 + c] * wx) >> 16;
  unsigned bottom = ((unsigned)p[(r1 + x0) * 4 + c] * (65536 - wx) + (unsigned)p[(r1 + x1) * 4 + c] * wx) >> 16;
  return (top * (65536 - wy) + bottom * wy) >> 16;
}

/* Поза кадра в окне: точка кадра q → p = опора + сдвиг + поворот(масштаб·(q − опора)). */
typedef struct { float pvx, pvy, ox, oy, sx, sy, c, s; } AcXf;
static void ac_xf(const AcXf *x, float qx, float qy, float *px, float *py) {
  float dx = (qx - x->pvx) * x->sx, dy = (qy - x->pvy) * x->sy;
  *px = x->pvx + x->ox + dx * x->c - dy * x->s;
  *py = x->pvy + x->oy + dx * x->s + dy * x->c;
}

#define AC_FEET 0.86f /* где у кадра робота «ноги» (кольцо), доля высоты окна */
#define AC_GIRL_FEET 0.955f /* подошвы кошко-девочки */
static float ac_avatar_feet(void) {
  return g_acAvatarSkin == AC_SKIN_CAT ? AC_CAT_FEET : g_acAvatarSkin == AC_SKIN_GIRL ? AC_GIRL_FEET : 0.84f;
}

static void ac_avatar_pose(DWORD now, float zoom, AcXf *x) {
  int S = g_acAvatarSize;
  float sx = zoom, sy = zoom, ox = 0.0f, oy = 0.0f, ang = g_acSwing;
  float pvx = S * 0.5f, pvy = S * ac_avatar_feet();
  /* дыхание; у кошко-девочки оно своё, в позе скелета */
  float br = g_acAvatarSkin == AC_SKIN_GIRL ? 0.0f : sinf(now / (g_acAvatarState == AC_AV_SLEEP ? 1150.0f : 640.0f)) * 0.016f;
  sy *= 1.0f + br;
  sx *= 1.0f - br * 0.6f;
  if (!g_acHeld) ang += 0.024f * sinf(now / 1750.0f) + g_acAvatarLookX * 0.04f; /* покачивание, тянется к курсору */
  if (g_acHopAt) {
    float t = (now - g_acHopAt) / 420.0f;
    if (t < 1.0f) {
      float a = sinf(t * 3.14159265f);
      oy -= a * S * 0.09f;
      sy *= 1.0f + 0.06f * a;
      sx *= 1.0f - 0.04f * a;
    } else {
      g_acHopAt = 0;
      g_acLandAt = now | 1;
    }
  }
  if (g_acLandAt) { /* сжатие с затухающим отскоком */
    float t = (float)(now - g_acLandAt);
    if (t < 650.0f) {
      float a = 0.13f * expf(-t / 150.0f) * cosf(t / 55.0f);
      sy *= 1.0f - a;
      sx *= 1.0f + a * 0.7f;
    } else g_acLandAt = 0;
  }
  if (g_acHeld) { /* висит за точку, где взяли, и чуть вытягивается */
    pvx = (float)g_acGrip.x;
    pvy = (float)g_acGrip.y;
    sy *= 1.035f;
    sx *= 0.975f;
  }
  x->pvx = pvx; x->pvy = pvy; x->ox = ox; x->oy = oy; x->sx = sx; x->sy = sy;
  x->c = cosf(ang); x->s = sinf(ang);
}

static int ac_emo_now(DWORD now) {
  if (g_acEmoUntil && ac_due(now, g_acEmoUntil)) { g_acEmo = AC_EMO_NONE; g_acEmoUntil = 0; }
  return g_acEmo;
}

/* замкнутая кривая вокруг (cx, cy) в осях лица: n+1 точек */
static int ac_face_ring(const AcFaceAxes *ax, float cx, float cy, float ra, float rb, float *pts, int n) {
  for (int i = 0; i <= n; i++) {
    float a = 6.2831853f * i / n;
    ac_face_pt(ax, cx, cy, ra * cosf(a), rb * sinf(a), pts + 2 * i);
  }
  return n + 1;
}

/* Глаза и рот поверх кадра: выражение по состоянию и эмоции, моргание, взгляд за курсором. */
static void ac_avatar_face(DWORD now, const AcXf *xf) {
  int S = g_acAvatarSize, state = g_acAvatarState, emo = ac_emo_now(now);
  if (state == AC_AV_SLEEP || state == AC_AV_ERROR) emo = AC_EMO_NONE;
  const short *fa = kAcAvatarFace[state][g_acAvatarFrame], *fb = kAcAvatarFace[state][g_acAvatarNext];
  float f[6], m = g_acAvatarMix / 256.0f;
  for (int i = 0; i < 6; i++) f[i] = fa[i] + (fb[i] - fa[i]) * m;
  float k = (float)S / AC_AVATAR_FRAME / 8.0f;
  float lx, ly, rx, ry, mx, my;
  ac_xf(xf, f[0] * k, f[1] * k, &lx, &ly);
  ac_xf(xf, f[2] * k, f[3] * k, &rx, &ry);
  ac_xf(xf, f[4] * k, f[5] * k, &mx, &my);
  float ux = rx - lx, uy = ry - ly, d = sqrtf(ux * ux + uy * uy);
  if (d < 4.0f) return;
  AcFaceAxes ax = {ux / d, uy / d, -uy / d, ux / d};
  float lookA = g_acAvatarLookX * d * 0.17f, lookB = g_acAvatarLookY * d * 0.11f;
  COLORREF mood = kAcAvatarMood[state];
  if (emo == AC_EMO_LOVE || emo == AC_EMO_SHY) mood = RGB(0xFF, 0x6E, 0xB4);
  else if (emo == AC_EMO_ANGRY) mood = RGB(0xFF, 0x5C, 0x7A);
  else if (emo == AC_EMO_SAD) mood = RGB(0x7A, 0xA8, 0xFF);
  COLORREF core = ac_mix(mood, RGB(255, 255, 255), 0.55f);
  float glowR = d * 0.10f + 0.6f, w = d * 0.27f, h = d * 0.50f, line = d * 0.065f + 0.45f;
  BOOL joy = g_acAvatarJoyUntil || state == AC_AV_SUCCESS || state == AC_AV_WAVE;
  float pts[64];
  for (int side = -1; side <= 1; side += 2) {
    float c0[2];
    ac_face_pt(&ax, side < 0 ? lx : rx, side < 0 ? ly : ry, lookA, lookB, c0);
    float cx = c0[0], cy = c0[1];
    float in = -side * 0.6f * w; /* внутренний край глаза — к переносице */
    if (emo == AC_EMO_SURPRISED) { /* круглые глаза */
      int n = ac_face_ring(&ax, cx, cy, w * 0.62f, h * 0.42f, pts, 20);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, n, line, core, mood, glowR, 1.0f);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, c0, 1, w * 0.16f, core, mood, glowR, 1.0f);
      continue;
    }
    if (emo == AC_EMO_LOVE) { /* сердечки */
      for (int i = 0; i <= 24; i++) {
        float t = 6.2831853f * i / 24, st = sinf(t);
        float hx = 16.0f * st * st * st, hy = -(13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t));
        ac_face_pt(&ax, cx, cy, hx * w * 0.042f, hy * w * 0.042f, pts + 2 * i);
      }
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 25, line, core, mood, glowR * 1.3f, 1.0f);
      continue;
    }
    if (emo == AC_EMO_DIZZY) { /* спирали */
      for (int i = 0; i < 24; i++) {
        float t = i / 23.0f, a = t * 15.7f * (float)side;
        ac_face_pt(&ax, cx, cy, cosf(a) * w * 0.6f * t, sinf(a) * h * 0.4f * t, pts + 2 * i);
      }
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 24, line * 0.8f, core, mood, glowR, 1.0f);
      continue;
    }
    if (emo == AC_EMO_SHY || (emo == AC_EMO_WINK && side > 0)) { /* дужки, у стеснения — румянец */
      ac_face_pt(&ax, cx, cy, -0.5f * w, 0.1f * h, pts);
      ac_face_pt(&ax, cx, cy, 0.0f, -0.12f * h, pts + 2);
      ac_face_pt(&ax, cx, cy, 0.5f * w, 0.1f * h, pts + 4);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 3, line, core, mood, glowR, 1.0f);
      if (emo == AC_EMO_SHY) {
        float blush[2];
        ac_face_pt(&ax, cx, cy, -in * 0.4f, 0.62f * h, blush);
        ac_paint_stroke(g_acAvatarFramePixels, S, S, blush, 1, w * 0.32f, RGB(255, 130, 170), RGB(255, 90, 150),
                        w * 0.3f, 0.45f);
      }
      continue;
    }
    if (emo == AC_EMO_SAD || emo == AC_EMO_ANGRY) { /* глаз поменьше и «бровь»-веко наискось */
      float r = w * 0.42f, half = h * 0.18f;
      ac_face_pt(&ax, cx, cy, 0.0f, -half + 0.08f * h, pts);
      ac_face_pt(&ax, cx, cy, 0.0f, half + 0.08f * h, pts + 2);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 2, r, core, mood, glowR, 1.0f);
      float hiIn = emo == AC_EMO_SAD ? -0.62f : -0.32f, hiOut = emo == AC_EMO_SAD ? -0.3f : -0.6f;
      ac_face_pt(&ax, cx, cy, in, hiIn * h, pts);
      ac_face_pt(&ax, cx, cy, -in, hiOut * h, pts + 2);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 2, line * 0.8f, core, mood, glowR, 1.0f);
      continue;
    }
    if (state == AC_AV_SLEEP) {
      ac_face_pt(&ax, cx, cy, -0.55f * w, -0.02f * h, pts);
      ac_face_pt(&ax, cx, cy, 0.0f, 0.12f * h, pts + 2);
      ac_face_pt(&ax, cx, cy, 0.55f * w, -0.02f * h, pts + 4);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 3, line, core, mood, glowR, 0.8f);
    } else if (state == AC_AV_ERROR) {
      float s = (float)-side; /* левый «>», правый «<» */
      ac_face_pt(&ax, cx, cy, -s * 0.45f * w, -0.24f * h, pts);
      ac_face_pt(&ax, cx, cy, s * 0.42f * w, 0.0f, pts + 2);
      ac_face_pt(&ax, cx, cy, -s * 0.45f * w, 0.24f * h, pts + 4);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 3, line, core, mood, glowR, 1.0f);
    } else if (joy) {
      ac_face_pt(&ax, cx, cy, -0.55f * w, 0.12f * h, pts);
      ac_face_pt(&ax, cx, cy, 0.0f, -0.16f * h, pts + 2);
      ac_face_pt(&ax, cx, cy, 0.55f * w, 0.12f * h, pts + 4);
      ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 3, line * 1.15f, core, mood, glowR, 1.0f);
    } else {
      DWORD blink = now - g_acAvatarBlinkAt;
      float open = !g_acAvatarBlinkAt ? 1.0f : blink < 80 ? 0.0f : blink < 150 ? 0.5f : 1.0f;
      if (state == AC_AV_THINKING && side < 0) open *= 0.62f;
      float scale = state == AC_AV_LISTENING ? 1.14f : 1.0f, r = w * 0.5f * scale;
      if (open < 0.2f) {
        ac_face_pt(&ax, cx, cy, -0.5f * w, 0.0f, pts);
        ac_face_pt(&ax, cx, cy, 0.5f * w, 0.0f, pts + 2);
        ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 2, line, core, mood, glowR, 1.0f);
      } else {
        float half = (h * scale * 0.5f - r) * open;
        ac_face_pt(&ax, cx, cy, 0.0f, -half, pts);
        ac_face_pt(&ax, cx, cy, 0.0f, half, pts + 2);
        ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, 2, r * (0.75f + 0.25f * open), core, mood, glowR, 1.0f);
      }
    }
  }

  /* рот: тусклее глаз, взгляд сдвигает его вдвое меньше */
  float c0[2];
  ac_face_pt(&ax, mx, my, lookA * 0.5f, lookB * 0.5f, c0);
  float cx = c0[0], cy = c0[1];
  COLORREF mouth = ac_mix(mood, RGB(255, 255, 255), 0.3f);
  float mr = d * 0.045f + 0.4f, mg = d * 0.06f + 0.4f;
  int n = 3;
  if (emo == AC_EMO_SURPRISED) { /* «о» */
    n = ac_face_ring(&ax, cx, cy, d * 0.07f, d * 0.08f, pts, 12);
  } else if (emo == AC_EMO_SAD) { /* уголки вниз */
    ac_face_pt(&ax, cx, cy, -0.13f * d, 0.04f * d, pts);
    ac_face_pt(&ax, cx, cy, 0.0f, -0.02f * d, pts + 2);
    ac_face_pt(&ax, cx, cy, 0.13f * d, 0.04f * d, pts + 4);
  } else if (emo == AC_EMO_ANGRY) {
    ac_face_pt(&ax, cx, cy, -0.1f * d, 0.01f * d, pts);
    ac_face_pt(&ax, cx, cy, 0.1f * d, 0.01f * d, pts + 2);
    n = 2;
  } else if (emo == AC_EMO_DIZZY || emo == AC_EMO_SHY) { /* волной */
    for (int i = 0; i < 6; i++)
      ac_face_pt(&ax, cx, cy, (-0.13f + 0.052f * i) * d, (i & 1 ? -0.022f : 0.022f) * d, pts + 2 * i);
    n = 6;
  } else if (emo == AC_EMO_LOVE || emo == AC_EMO_WINK) {
    ac_face_pt(&ax, cx, cy, -0.17f * d, -0.03f * d, pts);
    ac_face_pt(&ax, cx, cy, 0.0f, 0.06f * d, pts + 2);
    ac_face_pt(&ax, cx, cy, 0.17f * d, -0.03f * d, pts + 4);
  } else if (state == AC_AV_TALKING) {
    float open = 0.25f + 0.75f * fabsf(sinf(now / 90.0f)) * (0.55f + 0.45f * sinf(now / 237.0f));
    ac_face_pt(&ax, cx, cy, 0.0f, -d * 0.06f * open, pts);
    ac_face_pt(&ax, cx, cy, 0.0f, d * 0.06f * open, pts + 2);
    mr = d * 0.055f + 0.4f;
    n = 2;
  } else if (joy) {
    ac_face_pt(&ax, cx, cy, -0.17f * d, -0.03f * d, pts);
    ac_face_pt(&ax, cx, cy, 0.0f, 0.055f * d, pts + 2);
    ac_face_pt(&ax, cx, cy, 0.17f * d, -0.03f * d, pts + 4);
  } else if (state == AC_AV_ERROR) {
    ac_face_pt(&ax, cx, cy, -0.13f * d, 0.04f * d, pts);
    ac_face_pt(&ax, cx, cy, -0.04f * d, -0.015f * d, pts + 2);
    ac_face_pt(&ax, cx, cy, 0.04f * d, 0.02f * d, pts + 4);
    ac_face_pt(&ax, cx, cy, 0.13f * d, -0.02f * d, pts + 6);
    n = 4;
  } else if (state == AC_AV_SLEEP || state == AC_AV_LISTENING) {
    ac_face_pt(&ax, cx, cy, 0.0f, 0.0f, pts);
    mr = d * (state == AC_AV_SLEEP ? 0.045f + 0.012f * sinf(now / 700.0f) : 0.055f) + 0.4f;
    n = 1;
  } else if (state == AC_AV_THINKING) {
    ac_face_pt(&ax, cx, cy, -0.02f * d, 0.01f * d, pts);
    ac_face_pt(&ax, cx, cy, 0.12f * d, -0.015f * d, pts + 2);
    n = 2;
  } else if (state == AC_AV_FILES) {
    ac_face_pt(&ax, cx, cy, -0.08f * d, 0.0f, pts);
    ac_face_pt(&ax, cx, cy, 0.08f * d, 0.0f, pts + 2);
    n = 2;
  } else {
    ac_face_pt(&ax, cx, cy, -0.11f * d, -0.015f * d, pts);
    ac_face_pt(&ax, cx, cy, 0.0f, 0.03f * d, pts + 2);
    ac_face_pt(&ax, cx, cy, 0.11f * d, -0.015f * d, pts + 4);
  }
  float thick = n == 1 || (state == AC_AV_TALKING && !emo) ? mr : line * 0.8f;
  ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, n, thick, mouth, mood, mg, state == AC_AV_SLEEP ? 0.6f : 0.85f);

  /* держат мышью — лапка вцепилась в курсор: рука от края головы до точки хвата */
  if (g_acHeld) {
    float sh[2], arm[4], grip[2] = {(float)g_acGrip.x, (float)g_acGrip.y};
    ac_face_pt(&ax, rx, ry, d * 0.95f, d * 0.55f, sh);
    arm[0] = sh[0]; arm[1] = sh[1]; arm[2] = grip[0]; arm[3] = grip[1];
    COLORREF pearl = RGB(238, 241, 247), edge = RGB(20, 24, 44), cuff = RGB(123, 92, 255);
    ac_paint_stroke(g_acAvatarFramePixels, S, S, arm, 2, d * 0.15f + 1.2f, edge, edge, 0.0f, 0.75f);
    ac_paint_stroke(g_acAvatarFramePixels, S, S, arm, 2, d * 0.15f, pearl, pearl, 0.0f, 1.0f);
    ac_paint_stroke(g_acAvatarFramePixels, S, S, grip, 1, d * 0.3f + 1.2f, edge, edge, 0.0f, 0.75f);
    ac_paint_stroke(g_acAvatarFramePixels, S, S, grip, 1, d * 0.3f, pearl, pearl, 0.0f, 1.0f);
    float ring[2] = {grip[0] + (sh[0] - grip[0]) * 0.18f, grip[1] + (sh[1] - grip[1]) * 0.18f};
    ac_paint_stroke(g_acAvatarFramePixels, S, S, ring, 1, d * 0.17f, cuff, cuff, d * 0.05f, 0.9f);
  }
}

/* Кадр в окно S×S: поза в обратную сторону и билинейная выборка из атласа. Кадр fw×fh стоит
   в квадрате со стороной box посередине по ширине и на top ниже верха — вокруг прозрачно. */
static void ac_avatar_sample(const BYTE *atlas, int atlasW, int fw, int fh, int box, int top, int frameX, int frameY,
                             int nextX, unsigned mix, const AcXf *xf) {
  int S = g_acAvatarSize;
  float scale = (float)box / S, left = (box - fw) * 0.5f, isx = 1.0f / xf->sx, isy = 1.0f / xf->sy;
  for (int y = 0; y < S; y++) {
    for (int x = 0; x < S; x++) {
      /* точка кадра под пикселем окна: поза в обратную сторону */
      float dx = x + 0.5f - xf->pvx - xf->ox, dy = y + 0.5f - xf->pvy - xf->oy;
      float qx = xf->pvx + (dx * xf->c + dy * xf->s) * isx, qy = xf->pvy + (-dx * xf->s + dy * xf->c) * isy;
      float sx = qx * scale - left - 0.5f, sy = qy * scale - top - 0.5f;
      if (sx <= -1.0f || sx >= fw || sy <= -1.0f || sy >= fh) {
        memset(g_acAvatarFramePixels + ((size_t)y * S + x) * 4, 0, 4);
        continue;
      }
      if (sy < 0.0f) sy = 0.0f;
      int y0 = (int)sy;
      int wy = (int)((sy - y0) * 65535.0f);
      if (y0 >= fh - 1) { y0 = fh - 1; wy = 0; }
      int y1 = y0 + (wy ? 1 : 0);
      if (sx < 0.0f) sx = 0.0f;
      int x0 = (int)sx;
      int wx = (int)((sx - x0) * 65535.0f);
      if (x0 >= fw - 1) { x0 = fw - 1; wx = 0; }
      int x1 = x0 + (wx ? 1 : 0);
      size_t r0 = (size_t)(frameY + y0) * atlasW, r1 = (size_t)(frameY + y1) * atlasW;
      BYTE *dst = g_acAvatarFramePixels + (((size_t)y * S + x) * 4);
      for (int c = 0; c < 4; c++) {
        unsigned v = ac_bilerp(atlas, r0, r1, frameX + x0, frameX + x1, wx, wy, c);
        if (mix) v = (v * (256 - mix) + ac_bilerp(atlas, r0, r1, nextX + x0, nextX + x1, wx, wy, c) * mix) >> 8;
        dst[c] = (BYTE)v;
      }
    }
  }
}

/* Лицо кошки — готовый кадр по состоянию и эмоции (те же, что у робота рисуются глазами) */
static int ac_cat_face(DWORD now) {
  int state = g_acAvatarState, emo = ac_emo_now(now);
  if (state == AC_AV_SLEEP) return AC_CAT_BLINK;
  switch (emo) {
  case AC_EMO_SURPRISED: return AC_CAT_SURPRISED;
  case AC_EMO_LOVE: return AC_CAT_HAPPY;
  case AC_EMO_SHY: return AC_CAT_SHY;
  case AC_EMO_SAD: return AC_CAT_SAD;
  case AC_EMO_ANGRY: return AC_CAT_ANGRY;
  case AC_EMO_WINK: return AC_CAT_BLINK;
  case AC_EMO_DIZZY: return (now / 260) & 1 ? AC_CAT_BLINK : AC_CAT_IDLE; /* своего кадра нет — жмурится */
  }
  if (state == AC_AV_WAVE) return AC_CAT_WAVE;
  if (state == AC_AV_ERROR) return AC_CAT_SAD;
  if (state == AC_AV_SUCCESS || g_acAvatarJoyUntil) return AC_CAT_HAPPY;
  if (state == AC_AV_TALKING) return (now / 130) & 1 ? AC_CAT_TALK : AC_CAT_IDLE;
  if (g_acAvatarBlinkAt && now - g_acAvatarBlinkAt < 150) return AC_CAT_BLINK;
  return AC_CAT_IDLE;
}

#include "ai_avatar_girl.c" /* третий вид: кошко-девочка со скелетом */

/* кадр в g_acAvatarFramePixels; alpha — прозрачность призыва и отзыва */
static void ac_avatar_render(DWORD now, BYTE *alpha) {
  float zoom = ac_avatar_zoom(now, alpha);
  ac_avatar_frame(now);
  AcXf xf;
  ac_avatar_pose(now, zoom, &xf);
  if (g_acAvatarSkin == AC_SKIN_GIRL) {
    ac_girl_render(now, &xf);
    return;
  }
  if (g_acAvatarSkin == AC_SKIN_CAT) {
    int f = ac_cat_face(now) * AC_CAT_W;
    ac_avatar_sample(g_acCatPixels, AC_CAT_W * AC_CAT_N, AC_CAT_W, AC_CAT_H, AC_CAT_BOX, AC_CAT_TOP, f, 0, f, 0, &xf);
    return;
  }
  ac_avatar_sample(g_acAvatarPixels, AC_AVATAR_FRAME * AC_AVATAR_COLS, AC_AVATAR_FRAME, AC_AVATAR_FRAME,
                   AC_AVATAR_FRAME, 0, g_acAvatarFrame * AC_AVATAR_FRAME, g_acAvatarState * AC_AVATAR_FRAME,
                   g_acAvatarNext * AC_AVATAR_FRAME, (unsigned)g_acAvatarMix, &xf);
  ac_avatar_face(now, &xf);
}

static void ac_avatar_present(void) {
  if (!g_acAvatar || !IsWindowVisible(g_acAvatar) || !ac_avatar_load()) return;
  if (!g_acAvatarFrameBitmap) {
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = g_acAvatarSize;
    info.bmiHeader.biHeight = -g_acAvatarSize;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    g_acAvatarFrameBitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS,
                                               (void **)&g_acAvatarFramePixels, NULL, 0);
    if (!g_acAvatarFrameBitmap || !g_acAvatarFramePixels) return;
  }

  BYTE alpha;
  ac_avatar_render(GetTickCount(), &alpha);

  HDC screen = GetDC(NULL);
  HDC memory = screen ? CreateCompatibleDC(screen) : NULL;
  if (memory) {
    HGDIOBJ old = SelectObject(memory, g_acAvatarFrameBitmap);
    RECT wr;
    GetWindowRect(g_acAvatar, &wr);
    POINT at = {wr.left, wr.top}, origin = {0, 0};
    SIZE size = {g_acAvatarSize, g_acAvatarSize};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_acAvatar, screen, &at, &size, memory, &origin, 0, &blend, ULW_ALPHA);
    SelectObject(memory, old);
    DeleteDC(memory);
  }
  if (screen) ReleaseDC(NULL, screen);
}

/* ---------- пузырь: чем ИИ занят */

static float ac_smin(float a, float b, float k) {
  float h = 0.5f + 0.5f * (b - a) / k;
  if (h < 0.0f) h = 0.0f;
  if (h > 1.0f) h = 1.0f;
  return b + (a - b) * h - k * h * (1.0f - h);
}

/* выпуклый треугольник: максимум расстояний до сторон, наружу — плюс */
static float ac_triangle(float px, float py, const float *t) {
  float gx = (t[0] + t[2] + t[4]) / 3.0f, gy = (t[1] + t[3] + t[5]) / 3.0f, d = -1e9f;
  for (int i = 0; i < 3; i++) {
    float ax = t[2 * i], ay = t[2 * i + 1], bx = t[(2 * i + 2) % 6], by = t[(2 * i + 3) % 6];
    float nx = by - ay, ny = ax - bx, l = sqrtf(nx * nx + ny * ny);
    if (l <= 0.0f) continue;
    nx /= l; ny /= l;
    if ((gx - ax) * nx + (gy - ay) * ny > 0.0f) { nx = -nx; ny = -ny; }
    float e = (px - ax) * nx + (py - ay) * ny;
    if (e > d) d = e;
  }
  return d;
}

/* Текст на прозрачном холсте: белым по чёрному во временный DIB, яркость —
   покрытие, поверх pix цветом col. В dc уже выбран шрифт; dc — в памяти. */
static void ac_text(BYTE *pix, int W, int H, HDC dc, const wchar_t *s, int n, RECT r, UINT fmt, COLORREF col) {
  int tw = r.right - r.left, th = r.bottom - r.top;
  if (tw <= 0 || th <= 0) return;
  BITMAPINFO info;
  memset(&info, 0, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = tw;
  info.bmiHeader.biHeight = -th;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  BYTE *tp = NULL;
  HBITMAP tb = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&tp, NULL, 0);
  if (!tb || !tp) {
    if (tb) DeleteObject(tb);
    return;
  }
  HGDIOBJ old = SelectObject(dc, tb);
  memset(tp, 0, (size_t)tw * th * 4);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  RECT lr = {0, 0, tw, th};
  DrawTextW(dc, s, n, &lr, fmt);
  GdiFlush();
  for (int y = 0; y < th; y++) {
    int py = r.top + y;
    if (py < 0 || py >= H) continue;
    for (int x = 0; x < tw; x++) {
      int px = r.left + x;
      const BYTE *c = tp + ((size_t)y * tw + x) * 4;
      BYTE m = c[0] > c[1] ? c[0] : c[1];
      if (c[2] > m) m = c[2];
      if (m && px >= 0 && px < W) ac_px_over(pix + ((size_t)py * W + px) * 4, col, m / 255.0f);
    }
  }
  SelectObject(dc, old);
  DeleteObject(tb);
}

/* расстояние до скруглённого прямоугольника: внутри — минус, снаружи — плюс */
static float ac_rrect_sd(float px, float py, float x0, float y0, float x1, float y1, float r) {
  float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
  if (r > hx) r = hx;
  if (r > hy) r = hy;
  float qx = fabsf(px - (x0 + hx)) - (hx - r), qy = fabsf(py - (y0 + hy)) - (hy - r);
  float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f, in = qx > qy ? qx : qy;
  return sqrtf(ox * ox + oy * oy) + (in < 0.0f ? in : 0.0f) - r;
}

/* скруглённый прямоугольник с мягким краем */
static void ac_rrect(BYTE *pix, int W, int H, float x0, float y0, float x1, float y1, float r, COLORREF fill, float a) {
  int ix0 = (int)x0 - 1, ix1 = (int)x1 + 1, iy0 = (int)y0 - 1, iy1 = (int)y1 + 1;
  if (ix0 < 0) ix0 = 0;
  if (iy0 < 0) iy0 = 0;
  if (ix1 > W - 1) ix1 = W - 1;
  if (iy1 > H - 1) iy1 = H - 1;
  for (int y = iy0; y <= iy1; y++)
    for (int x = ix0; x <= ix1; x++)
      ac_px_over(pix + ((size_t)y * W + x) * 4, fill, a * (0.5f - ac_rrect_sd(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r)));
}

static void ac_bubble_update(void) {
  if (!g_acBubble || !g_acAvatar) return;
  DWORD now = GetTickCount();
  int S = g_acAvatarSize;
  int fontPx = S / 12;
  if (fontPx < 13) fontPx = 13;
  if (fontPx > 20) fontPx = 20;
  if (!g_acBubbleFont || g_acBubbleFontPx != fontPx) {
    if (g_acBubbleFont) DeleteObject(g_acBubbleFont);
    g_acBubbleFont = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_acBubbleFontPx = fontPx;
  }
  HDC screen = GetDC(NULL);
  HDC dc = screen ? CreateCompatibleDC(screen) : NULL;
  if (!dc) { if (screen) ReleaseDC(NULL, screen); return; }
  HGDIOBJ oldFont = SelectObject(dc, g_acBubbleFont);
  int maxW = S * 3 / 2 < 190 ? 190 : S * 3 / 2;
  if (g_acBubbleStory) maxW = S * 2 < 280 ? 280 : S * 2 > 380 ? 380 : S * 2;
  RECT tr = {0, 0, maxW, 0};
  DrawTextW(dc, g_acBubbleText, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int textW = tr.right > maxW ? maxW : tr.right, textH = tr.bottom;
  int lines = g_acBubbleStory ? 8 : 3;
  if (textH > tm.tmHeight * lines) textH = tm.tmHeight * lines;
  BOOL dots = g_acAvatarBusy && g_acBubbleThought;
  int pad = fontPx * 3 / 4 + 2, dotsW = dots ? fontPx * 2 : 0;
  int bw = textW + pad * 2 + dotsW, bh = textH + pad * 2;
  int margin = 10, tail = fontPx + 4;
  int W = bw + margin * 2, H = bh + margin * 2 + tail;

  /* над головой; у верхнего края экрана — под аватаром */
  RECT ar;
  GetWindowRect(g_acAvatar, &ar);
  int headX = ar.left + (int)(S * AC_CAT_EYE_X), headTop = ar.top + (int)(S * AC_CAT_HEAD); /* кошка */
  if (g_acAvatarSkin == AC_SKIN_GIRL) {
    ac_girl_head(&headX, &headTop);
    headX += ar.left;
    headTop += ar.top;
  } else if (g_acAvatarSkin != AC_SKIN_CAT) {
    const short *f = kAcAvatarFace[g_acAvatarState][g_acAvatarFrame];
    float k = (float)S / AC_AVATAR_FRAME / 8.0f;
    float eyeD = sqrtf((float)(f[2] - f[0]) * (f[2] - f[0]) + (float)(f[3] - f[1]) * (f[3] - f[1])) * k;
    headX = ar.left + (int)((f[0] + f[2]) * 0.5f * k);
    headTop = ar.top + (int)((f[1] + f[3]) * 0.5f * k - eyeD * 1.5f);
  }
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(MonitorFromWindow(g_acAvatar, MONITOR_DEFAULTTONEAREST), &mi);
  int x = headX - W / 2, y = headTop - H + margin;
  BOOL below = y < mi.rcWork.top;
  if (below) y = ar.bottom - S / 10 - margin;
  if (x + W > mi.rcWork.right) x = mi.rcWork.right - W;
  if (x < mi.rcWork.left) x = mi.rcWork.left;
  if (y + H > mi.rcWork.bottom) y = mi.rcWork.bottom - H;
  if (y < mi.rcWork.top) y = mi.rcWork.top;

  if (!g_acBubbleBitmap || W != g_acBubbleW || H != g_acBubbleH) {
    if (g_acBubbleBitmap) DeleteObject(g_acBubbleBitmap);
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = W;
    info.bmiHeader.biHeight = -H;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    g_acBubblePixels = NULL;
    g_acBubbleBitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&g_acBubblePixels, NULL, 0);
    g_acBubbleW = W;
    g_acBubbleH = H;
  }
  if (g_acBubbleBitmap && g_acBubblePixels) {
    BYTE *pix = g_acBubblePixels;
    memset(pix, 0, (size_t)W * H * 4);
    float bx = (float)margin, by = (float)(margin + (below ? tail : 0));
    float radius = fontPx * 0.9f, hx = bw * 0.5f, hy = bh * 0.5f, cx = bx + hx, cy = by + hy;
    float tipX = (float)(headX - x), tipY = below ? (float)margin : (float)(H - margin);
    if (tipX < margin + 6.0f) tipX = margin + 6.0f;
    if (tipX > W - margin - 6.0f) tipX = W - margin - 6.0f;
    float baseX = tipX, edge = below ? by + 2.0f : by + bh - 2.0f;
    if (baseX < bx + radius + tail * 0.6f) baseX = bx + radius + tail * 0.6f;
    if (baseX > bx + bw - radius - tail * 0.6f) baseX = bx + bw - radius - tail * 0.6f;
    float tri[6] = {baseX - tail * 0.5f, edge, baseX + tail * 0.5f, edge, tipX, tipY};
    float c1x = baseX + (tipX - baseX) * 0.35f, c1y = edge + (tipY - edge) * 0.42f, c1r = tail * 0.27f;
    float c2x = baseX + (tipX - baseX) * 0.8f, c2y = edge + (tipY - edge) * 0.85f, c2r = tail * 0.16f;
    COLORREF hair = ui_hairline();
    for (int py = 0; py < H; py++) {
      for (int px = 0; px < W; px++) {
        float fx = px + 0.5f, fy = py + 0.5f;
        float qx = fabsf(fx - cx) - (hx - radius), qy = fabsf(fy - cy) - (hy - radius);
        float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
        float inner = qx > qy ? qx : qy;
        float dist = sqrtf(ox * ox + oy * oy) + (inner < 0.0f ? inner : 0.0f) - radius;
        if (g_acBubbleThought) {
          float d1 = sqrtf((fx - c1x) * (fx - c1x) + (fy - c1y) * (fy - c1y)) - c1r;
          float d2 = sqrtf((fx - c2x) * (fx - c2x) + (fy - c2y) * (fy - c2y)) - c2r;
          if (d1 < dist) dist = d1;
          if (d2 < dist) dist = d2;
        } else {
          dist = ac_smin(dist, ac_triangle(fx, fy, tri), 3.0f);
        }
        BYTE *dst = pix + ((size_t)py * W + px) * 4;
        /* бумага темы с тонкой кромкой (с 2026.10.26; раньше — тёмное стекло) */
        if (dist > 0.0f) ac_px_over(dst, RGB(0, 0, 0), 0.26f * expf(-dist / 3.5f));
        ac_px_over(dst, COL_PAPER, 0.97f * (0.5f - dist));
        float band = 1.0f - fabsf(dist + 0.5f);
        if (band > 0.0f) ac_px_over(dst, hair, band);
      }
    }
    RECT tr2 = {(int)bx + pad, (int)by + pad, (int)bx + pad + textW, (int)by + pad + textH};
    ac_text(pix, W, H, dc, g_acBubbleText, -1, tr2, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS,
            COL_INK);
    /* ИИ ещё работает: три точки бегут волной — акцентом темы */
    if (dots) {
      for (int i = 0; i < 3; i++) {
        float ph = 0.5f + 0.5f * sinf(now / 170.0f - i * 1.1f);
        float dot[2] = {bx + bw - pad - dotsW + (i + 0.5f) * dotsW / 3.0f, cy - ph * fontPx * 0.12f};
        ac_paint_stroke(pix, W, H, dot, 1, fontPx * (0.12f + 0.06f * ph), COL_SAGE,
                        blend_rgb(COL_SAGE, RGB(255, 255, 255), 120), 1.5f, 0.55f + 0.45f * ph);
      }
    }
    HGDIOBJ oldBmp = SelectObject(dc, g_acBubbleBitmap);
    POINT at = {x, y}, origin = {0, 0};
    SIZE size = {W, H};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, g_acBubbleAlpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_acBubble, screen, &at, &size, dc, &origin, 0, &blend, ULW_ALPHA);
    SelectObject(dc, oldBmp);
  }
  SelectObject(dc, oldFont);
  DeleteDC(dc);
  ReleaseDC(NULL, screen);
}

static void ac_bubble_hide(BOOL now) {
  if (!g_acBubble || !IsWindowVisible(g_acBubble)) return;
  if (now) { ShowWindow(g_acBubble, SW_HIDE); g_acBubbleAlpha = 0; g_acBubbleFading = FALSE; }
  else g_acBubbleFading = TRUE;
}

static LRESULT CALLBACK AcBubbleProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
  case WM_SETCURSOR: SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE;
  case WM_LBUTTONUP:
    ac_bubble_hide(FALSE);
    if (!g_acBubbleStory) aichat_show(); /* факт или анекдот — просто закрыть */
    return 0;
  case WM_RBUTTONUP: ac_bubble_hide(FALSE); return 0;
  case WM_PAINT: ValidateRect(hwnd, NULL); return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* Показать реплику рядом с аватаром. ms = 0 — держать, пока ИИ занят.
   story — факт или анекдот: длиннее, пузырь шире, показывается и без «Мыслей рядом». */
static void ac_avatar_bubble_ex(const wchar_t *text, BOOL thought, DWORD ms, BOOL story) {
  if ((!g_acAvatarBubbles && !story) || !g_acAvatar || !IsWindowVisible(g_acAvatar) || g_acAvatarAnim == 2 || !text) return;
  if (ac_mini_open()) return; /* мини-чат открыт рядом — там то же самое */
  int n = 0, cap = story ? FJ_MAX : 196;
  BOOL space = FALSE;
  for (const wchar_t *s = text; *s && n < cap; s++) {
    wchar_t c = *s;
    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    if (c == L'*' || c == L'#' || c == L'`') continue; /* разметка ответа */
    if (c == L' ' && (space || !n)) continue;
    space = c == L' ';
    g_acBubbleText[n++] = c;
  }
  while (n && g_acBubbleText[n - 1] == L' ') n--;
  if (n > 120 && !story) {
    n = 118;
    while (n > 80 && g_acBubbleText[n] != L' ') n--;
    g_acBubbleText[n++] = L'…';
  }
  g_acBubbleText[n] = 0;
  if (!n) return;
  g_acBubbleStory = story;
  g_acBubbleThought = thought;
  g_acBubbleUntil = ms ? GetTickCount() + ms : 0;
  g_acBubbleFading = FALSE;
  if (!g_acBubble) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = AcBubbleProc;
    wc.hInstance = g_inst;
    wc.lpszClassName = L"CursorPadAvatarBubble";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    g_acBubble = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                 L"CursorPadAvatarBubble", L"Мини-ИИ думает", WS_POPUP, 0, 0, 10, 10, NULL, NULL,
                                 g_inst, NULL);
    if (!g_acBubble) return;
  }
  if (!IsWindowVisible(g_acBubble)) {
    g_acBubbleAlpha = 0;
    ac_bubble_update();
    ShowWindow(g_acBubble, SW_SHOWNOACTIVATE);
  }
  ac_bubble_update();
}

static void ac_avatar_bubble(const wchar_t *text, BOOL thought, DWORD ms) { ac_avatar_bubble_ex(text, thought, ms, FALSE); }

/* Строка состояния чата (ac_status). Пока ИИ занят — всегда в пузырь; прочие
   сообщения — только если окно чата не перед глазами. Подсказки про «Стоп»
   в пузыре не нужны: кнопка в чате. */
static void ac_avatar_say(const wchar_t *text) {
  ac_mini_status(text);
  if (!text || !text[0]) {
    if (!g_acAvatarBusy && g_acBubbleThought && !g_acBubbleUntil) g_acBubbleUntil = GetTickCount() + 900;
    return;
  }
  if (!g_acAvatarBusy && g_acWnd && GetForegroundWindow() == g_acWnd) return;
  wchar_t line[200];
  lstrcpynW(line, text, 200);
  wchar_t *stop = wcsstr(line, L"Стоп");
  if (stop && stop != line) {
    *stop = 0;
    for (size_t n = wcslen(line); n && (line[n - 1] == L' ' || line[n - 1] == L'·' || line[n - 1] == L'—' ||
                                        line[n - 1] == L'.' || line[n - 1] == L','); n--)
      line[n - 1] = 0;
  }
  size_t len = wcslen(line);
  DWORD ms = (DWORD)(len > 120 ? 120 : len) * 55;
  if (ms < 2600) ms = 2600;
  ac_avatar_bubble(line, g_acAvatarBusy, g_acAvatarBusy ? 0 : ms);
}

/* ИИ начал или закончил работу (ac_busy). */
static void ac_avatar_busy(BOOL on) {
  g_acAvatarBusy = on;
  if (on && g_acFactId) { ai_cancel(g_acFactId); g_acFactId = 0; } /* человек спросил своё — шутка подождёт */
  ac_mini_refresh();
  if (!on && g_acBubbleThought && !g_acBubbleUntil) g_acBubbleUntil = GetTickCount() + 1500;
}

/* Ответ пришёл: робот «рассказывает», в пузыре — начало ответа. */
static void ac_avatar_answer(const wchar_t *text) {
  ac_avatar_set(AC_AV_TALKING);
  size_t len = text ? wcslen(text) : 0;
  DWORD ms = (DWORD)(len > 150 ? 150 : len) * 45;
  if (ms < 2800) ms = 2800;
  g_acAvatarTalkUntil = GetTickCount() + (ms < 3600 ? ms : 3600);
  if (!len || (g_acWnd && GetForegroundWindow() == g_acWnd)) return; /* ответ и так перед глазами */
  wchar_t first[160];
  size_t n = 0;
  for (const wchar_t *s = text; *s && n < 150; s++) {
    first[n++] = *s;
    if (n > 24 && (*s == L'.' || *s == L'!' || *s == L'?' || *s == L'\n') && (!s[1] || iswspace(s[1]))) break;
  }
  first[n] = 0;
  ac_avatar_bubble(first, FALSE, ms);
}

/* Файловая задача: какой инструмент модель вызвала сейчас. */
static void ac_avatar_tool(const wchar_t *tool, const wchar_t *path) {
  static const struct { const wchar_t *tool, *verb; } kVerbs[] = {
    {L"list", L"Смотрю папку"}, {L"filesystem", L"Смотрю папку"}, {L"find", L"Ищу"},
    {L"read", L"Читаю"}, {L"office_read", L"Читаю"}, {L"write", L"Готовлю файл"},
    {L"excel_write", L"Готовлю таблицу"}, {L"mkdir", L"Планирую папку"}, {L"new_folder", L"Планирую папку"},
    {L"inventory", L"Составляю перечень"}, {L"compare_materials", L"Сравниваю материалы"},
    {L"move", L"Планирую перенос"}, {L"copy", L"Планирую копию"}, {L"rename", L"Планирую новое имя"},
    {L"open", L"Открываю"}, {L"ask", L"Хочу уточнить"}, {L"done", L"Проверяю план"},
    {L"app", L"Вызываю команду CursorPad"},
  };
  if (!tool || !tool[0]) return;
  const wchar_t *verb = L"Работаю с файлами";
  for (size_t i = 0; i < sizeof(kVerbs) / sizeof(kVerbs[0]); i++)
    if (!wcscmp(tool, kVerbs[i].tool)) verb = kVerbs[i].verb;
  const wchar_t *name = path ? path : L"";
  for (const wchar_t *s = name; *s; s++)
    if ((*s == L'\\' || *s == L'/') && s[1]) name = s + 1;
  wchar_t line[200];
  if (name[0]) swprintf(line, 200, L"%ls «%.90ls»", verb, name);
  else lstrcpynW(line, verb, 200);
  ac_avatar_bubble(line, TRUE, 0);
}

/* ---------- окно аватара */

static void ac_avatar_save_settings(void) {
  if (!g_dataDir[0] || !g_acAvatar) return;
  wchar_t path[MAX_PATH], value[32];
  _snwprintf(path, MAX_PATH, L"%s\\ai_avatar.ini", g_dataDir);
  RECT rc;
  GetWindowRect(g_acAvatar, &rc);
  swprintf(value, 32, L"%d", g_acAvatarSize);
  WritePrivateProfileStringW(L"Avatar", L"Size", value, path);
  swprintf(value, 32, L"%d", (int)g_acAvatarHome.x);
  WritePrivateProfileStringW(L"Avatar", L"X", value, path);
  swprintf(value, 32, L"%d", (int)g_acAvatarHome.y);
  WritePrivateProfileStringW(L"Avatar", L"Y", value, path);
  WritePrivateProfileStringW(L"Avatar", L"Visible", IsWindowVisible(g_acAvatar) ? L"1" : L"0", path);
  WritePrivateProfileStringW(L"Avatar", L"Wander", g_acAvatarWander ? L"1" : L"0", path);
  WritePrivateProfileStringW(L"Avatar", L"Bubbles", g_acAvatarBubbles ? L"1" : L"0", path);
  WritePrivateProfileStringW(L"Avatar", L"Windows", g_acAvatarWindows ? L"1" : L"0", path);
  WritePrivateProfileStringW(L"Avatar", L"Facts", g_acFacts ? L"1" : L"0", path);
  WritePrivateProfileStringW(L"Avatar", L"Skin", kAcSkinKey[g_acAvatarSkin], path);
}

/* «Вид: Робот / Кошка / Кошко-девочка» — атласы прежних видов освобождаются, новый грузится при отрисовке */
static void ac_avatar_set_skin(int skin) {
  g_acAvatarSkin = skin >= 0 && skin < AC_SKIN_N ? skin : AC_SKIN_ROBOT;
  if (g_acAvatarSkin != AC_SKIN_ROBOT && g_acAvatarAtlas) {
    DeleteObject(g_acAvatarAtlas);
    g_acAvatarAtlas = NULL;
    g_acAvatarPixels = NULL;
  }
  if (g_acAvatarSkin != AC_SKIN_CAT && g_acCatAtlas) {
    DeleteObject(g_acCatAtlas);
    g_acCatAtlas = NULL;
    g_acCatPixels = NULL;
  }
  if (g_acAvatarSkin != AC_SKIN_GIRL && g_acGirlAtlas) {
    DeleteObject(g_acGirlAtlas);
    g_acGirlAtlas = NULL;
    g_acGirlPixels = NULL;
  }
  g_acHopAt = GetTickCount() | 1; /* переоделся — подпрыгнул */
  if (g_acAvatarSkin == AC_SKIN_GIRL) ac_girl_shot(AGA_WAVE);
  ac_avatar_present();
  ac_avatar_save_settings();
}

/* Новый размер окна. Робот растёт и уменьшается «от ног»: середина низа
   остаётся на месте, а не левый верхний угол. */
static void ac_avatar_apply_scale(int size) {
  if (size < AC_AVATAR_MIN) size = AC_AVATAR_MIN;
  if (size > AC_AVATAR_MAX) size = AC_AVATAR_MAX;
  if (size == g_acAvatarSize && g_acAvatarFrameBitmap) return;
  int old = g_acAvatarSize;
  g_acAvatarSize = size;
  if (g_acAvatarFrameBitmap) { DeleteObject(g_acAvatarFrameBitmap); g_acAvatarFrameBitmap = NULL; g_acAvatarFramePixels = NULL; }
  if (g_acAvatar) {
    RECT wr;
    GetWindowRect(g_acAvatar, &wr);
    int x = wr.left + (old - size) / 2, y = wr.top + (old - size);
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromWindow(g_acAvatar, MONITOR_DEFAULTTONEAREST), &mi)) {
      if (x > mi.rcWork.right - size) x = mi.rcWork.right - size;
      if (y > mi.rcWork.bottom - size) y = mi.rcWork.bottom - size;
      if (x < mi.rcWork.left) x = mi.rcWork.left;
      if (y < mi.rcWork.top) y = mi.rcWork.top;
    }
    g_acAvatarHome.x = x;
    g_acAvatarHome.y = y;
    g_acAvatarMoving = FALSE;
    SetWindowPos(g_acAvatar, HWND_TOPMOST, x, y, size, size, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  ac_avatar_present();
  ac_avatar_save_settings();
  ac_mini_follow();
}

/* Мини-чат у помощника тянут мышью — робот едет рядом и запоминает место. */
static void ac_avatar_follow_panel(int x, int y) {
  if (!g_acAvatar || !IsWindowVisible(g_acAvatar)) return;
  g_acAvatarFollowing = TRUE;
  SetWindowPos(g_acAvatar, HWND_TOPMOST, x, y, g_acAvatarSize, g_acAvatarSize, SWP_NOACTIVATE | SWP_NOSIZE);
  g_acAvatarFollowing = FALSE;
  g_acAvatarHome.x = x;
  g_acAvatarHome.y = y;
  g_acAvatarMoving = FALSE;
}

/* размер в процентах от обычных 160 px — подпись в пузыре и в меню */
static int ac_avatar_percent(int size) { return (size * 100 + AC_AVATAR_DEFAULT_SIZE / 2) / AC_AVATAR_DEFAULT_SIZE; }

/* Ctrl + колесо над роботом: плавно, ~8 % на щелчок колеса, тачпад — мельче */
static void ac_avatar_wheel(int delta) {
  float size = g_acAvatarSize * powf(1.08f, delta / (float)WHEEL_DELTA);
  int next = (int)(size + (delta > 0 ? 0.999f : 0.0f)); /* сдвиг хотя бы на пиксель */
  if (next == g_acAvatarSize) next += delta > 0 ? 1 : -1;
  ac_avatar_apply_scale(next);
  wchar_t line[48];
  swprintf(line, 48, L"Размер: %d %%", ac_avatar_percent(g_acAvatarSize));
  if (!g_acAvatarBusy) ac_avatar_bubble(line, FALSE, 1200);
}

static void ac_avatar_hide(void) {
  if (!g_acAvatar) return;
  KillTimer(g_acAvatar, AC_AVATAR_TIMER); /* скрытый не тратит процессор */
  ShowWindow(g_acAvatar, SW_HIDE);
  ac_bubble_hide(TRUE);
  ac_menu_close();
  ac_avatar_save_settings();
  ac_mini_sync(); /* помощника нет — мини-чата тоже */
}

/* ---------- своё меню помощника (ПКМ) вместо строк Windows: тёмное стекло,
   как пузырь, — шапка с состоянием, чат, ползунок размера, переключатели,
   «Отозвать». Закрывается щелчком мимо и Esc, ходит стрелками. */

enum { ACM_HEAD, ACM_CHAT, ACM_SIZE, ACM_SKIN, ACM_BUBBLES, ACM_FACTS, ACM_WANDER, ACM_WINDOWS, ACM_SEP, ACM_DISMISS, ACM_N };
#define AC_MENU_TIMER 0xA172
static HWND g_acMenu;
static RECT g_acMenuRow[ACM_N];
static int g_acMenuHot = -1, g_acMenuW, g_acMenuH, g_acMenuK = 100; /* k — масштаб под DPI, % */
static BOOL g_acMenuDrag, g_acMenuTrack;
static BYTE g_acMenuAlpha;
static HBITMAP g_acMenuBmp;
static BYTE *g_acMenuPix;
static int g_acMenuBmpW, g_acMenuBmpH;
static HFONT g_acMenuFont, g_acMenuBold, g_acMenuSmall, g_acMenuIcons;

static int ac_mpx(int v) { return v * g_acMenuK / 100; }
static BOOL ac_menu_pick(int i) {
  return i == ACM_CHAT || i == ACM_SIZE || i == ACM_SKIN || i == ACM_BUBBLES || i == ACM_FACTS || i == ACM_WANDER ||
         i == ACM_WINDOWS || i == ACM_DISMISS;
}

static HFONT ac_menu_font(int px, int weight, const wchar_t *face) {
  return CreateFontW(-ac_mpx(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}

static void ac_menu_free(void) {
  HFONT *fonts[] = {&g_acMenuFont, &g_acMenuBold, &g_acMenuSmall, &g_acMenuIcons};
  for (int i = 0; i < 4; i++)
    if (*fonts[i]) { DeleteObject(*fonts[i]); *fonts[i] = NULL; }
  if (g_acMenuBmp) { DeleteObject(g_acMenuBmp); g_acMenuBmp = NULL; g_acMenuPix = NULL; }
}

static void ac_menu_close(void) {
  if (!g_acMenu) return;
  HWND h = g_acMenu;
  g_acMenu = NULL;
  g_acMenuDrag = FALSE;
  DestroyWindow(h);
  ac_menu_free();
}

static void ac_menu_layout(void) {
  static const int kRowH[ACM_N] = {56, 38, 62, 38, 38, 38, 38, 38, 9, 38};
  int m = ac_mpx(14), w = ac_mpx(292), pad = ac_mpx(6), y = m + pad;
  for (int i = 0; i < ACM_N; i++) {
    SetRect(&g_acMenuRow[i], m + pad, y, m + w - pad, y + ac_mpx(kRowH[i]));
    y += ac_mpx(kRowH[i]);
  }
  g_acMenuW = w + 2 * m;
  g_acMenuH = y + pad + m;
}

/* ползунок размера: от x0 до x1 на высоте y */
static void ac_menu_track(float *x0, float *x1, float *y) {
  RECT r = g_acMenuRow[ACM_SIZE];
  *x0 = (float)(r.left + ac_mpx(16));
  *x1 = (float)(r.right - ac_mpx(16));
  *y = (float)(r.bottom - ac_mpx(15));
}

static void ac_menu_switch(BYTE *pix, int W, int H, RECT row, BOOL on) {
  float w = (float)ac_mpx(38), h = (float)ac_mpx(21), x1 = (float)(row.right - ac_mpx(12)), x0 = x1 - w;
  float cy = (row.top + row.bottom) * 0.5f;
  ac_rrect(pix, W, H, x0, cy - h / 2, x1, cy + h / 2, h / 2, on ? COL_SAGE : blend_rgb(COL_PAPER, COL_INK, 70), 1.0f);
  float knob[2] = {on ? x1 - h / 2 : x0 + h / 2, cy};
  ac_paint_stroke(pix, W, H, knob, 1, h / 2 - ac_mpx(3), RGB(255, 255, 255), RGB(255, 255, 255), 0.0f, 1.0f);
}

/* строка: значок, подпись, справа — пояснение мелким */
static void ac_menu_line(BYTE *pix, int W, int H, HDC dc, RECT row, wchar_t icon, const wchar_t *label,
                         const wchar_t *note, COLORREF ink, COLORREF iconInk, int height) {
  RECT ir = {row.left + ac_mpx(8), row.top, row.left + ac_mpx(34), row.top + height};
  if (g_acMenuIcons && icon) {
    SelectObject(dc, g_acMenuIcons);
    ac_text(pix, W, H, dc, &icon, 1, ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, iconInk);
  }
  RECT lr = {row.left + ac_mpx(42), row.top, row.right - ac_mpx(60), row.top + height};
  SelectObject(dc, g_acMenuFont);
  ac_text(pix, W, H, dc, label, -1, lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, ink);
  if (note) {
    RECT nr = {row.right - ac_mpx(150), row.top, row.right - ac_mpx(12), row.top + height};
    SelectObject(dc, g_acMenuSmall);
    ac_text(pix, W, H, dc, note, -1, nr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, COL_MUTED);
  }
}

static const wchar_t *ac_menu_state(void) {
  if (g_acAvatarBusy) return g_acAvatarState == AC_AV_FILES ? L"Работает с файлами" : L"Думает…";
  switch (g_acAvatarState) {
  case AC_AV_LISTENING: return L"Слушает";
  case AC_AV_THINKING: return L"Думает…";
  case AC_AV_FILES: return L"Работает с файлами";
  case AC_AV_SUCCESS: return L"Готово!";
  case AC_AV_ERROR: return L"Что-то не вышло";
  case AC_AV_TALKING: return L"Рассказывает ответ";
  case AC_AV_SLEEP: return L"Дремлет";
  }
  return L"Ждёт вопроса";
}

static void ac_menu_render(void) {
  if (!g_acMenu) return;
  int W = g_acMenuW, H = g_acMenuH;
  HDC screen = GetDC(NULL);
  HDC dc = screen ? CreateCompatibleDC(screen) : NULL;
  if (!dc) { if (screen) ReleaseDC(NULL, screen); return; }
  if (!g_acMenuBmp || g_acMenuBmpW != W || g_acMenuBmpH != H) {
    if (g_acMenuBmp) DeleteObject(g_acMenuBmp);
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = W;
    info.bmiHeader.biHeight = -H;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    g_acMenuPix = NULL;
    g_acMenuBmp = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&g_acMenuPix, NULL, 0);
    g_acMenuBmpW = W;
    g_acMenuBmpH = H;
  }
  BYTE *pix = g_acMenuPix;
  if (!g_acMenuBmp || !pix) { DeleteDC(dc); ReleaseDC(NULL, screen); return; }
  memset(pix, 0, (size_t)W * H * 4);
  HGDIOBJ oldFont = SelectObject(dc, g_acMenuFont);
  /* карточка в цветах темы (с 2026.10.26; раньше — тёмное стекло при любой теме):
     тень, бумага, тонкая кромка в цвет линий темы */
  COLORREF paper = COL_PAPER, hair = ui_hairline(), red = RGB(196, 43, 28);
  float m = (float)ac_mpx(14), r = (float)ac_mpx(14);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      float d = ac_rrect_sd(x + 0.5f, y + 0.5f, m, m, W - m, H - m, r);
      BYTE *dst = pix + ((size_t)y * W + x) * 4;
      if (d > 0.0f) ac_px_over(dst, RGB(0, 0, 0), 0.30f * expf(-d / (m * 0.4f)));
      ac_px_over(dst, paper, 0.98f * (0.5f - d));
      float band = 1.0f - fabsf(d + 0.5f);
      if (band > 0.0f) ac_px_over(dst, hair, band);
    }
  }
  if (g_acMenuHot >= 0 && ac_menu_pick(g_acMenuHot)) {
    RECT hr = g_acMenuRow[g_acMenuHot];
    ac_rrect(pix, W, H, (float)hr.left, hr.top + 1.0f, (float)hr.right, hr.bottom - 1.0f, (float)ac_mpx(9),
             g_acMenuHot == ACM_DISMISS ? blend_rgb(paper, red, 30) : blend_rgb(paper, COL_INK, 16), 1.0f);
  }
  COLORREF ink = COL_INK, cyan = COL_SAGE; /* cyan — акцент темы (имя осталось от тёмного вида) */
  /* шапка: огонёк настроения, имя и что сейчас делает */
  RECT hd = g_acMenuRow[ACM_HEAD];
  COLORREF mood = kAcAvatarMood[g_acAvatarState];
  float dot[2] = {hd.left + (float)ac_mpx(21), (hd.top + hd.bottom) * 0.5f};
  ac_paint_stroke(pix, W, H, dot, 1, (float)ac_mpx(7), ac_mix(mood, RGB(255, 255, 255), 0.4f), mood, (float)ac_mpx(4), 1.0f);
  RECT nm = {hd.left + ac_mpx(42), hd.top + ac_mpx(8), hd.right - ac_mpx(8), hd.top + ac_mpx(30)};
  SelectObject(dc, g_acMenuBold);
  ac_text(pix, W, H, dc, L"Мини-ИИ", -1, nm, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, ink);
  RECT st = {hd.left + ac_mpx(42), hd.top + ac_mpx(30), hd.right - ac_mpx(8), hd.top + ac_mpx(50)};
  SelectObject(dc, g_acMenuSmall);
  ac_text(pix, W, H, dc, ac_menu_state(), -1, st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, COL_MUTED);
  float rule[4] = {(float)hd.left + ac_mpx(8), hd.bottom - 0.5f, (float)hd.right - ac_mpx(8), hd.bottom - 0.5f};
  ac_paint_stroke(pix, W, H, rule, 2, 0.5f, hair, hair, 0.0f, 1.0f);
  /* строки */
  int lineH = ac_mpx(38);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_CHAT], 0xE8BD, L"Открыть чат", L"2× щелчок", ink, cyan, lineH);
  wchar_t pct[16];
  swprintf(pct, 16, L"%d %%", ac_avatar_percent(g_acAvatarSize));
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_SIZE], 0xE740, L"Размер", pct, ink, cyan, ac_mpx(34));
  float tx0, tx1, ty;
  ac_menu_track(&tx0, &tx1, &ty);
  float pos = (float)(g_acAvatarSize - AC_AVATAR_MIN) / (AC_AVATAR_MAX - AC_AVATAR_MIN), kx = tx0 + (tx1 - tx0) * pos;
  float track[4] = {tx0, ty, tx1, ty}, fill[4] = {tx0, ty, kx, ty}, knob[2] = {kx, ty};
  ac_paint_stroke(pix, W, H, track, 2, (float)ac_mpx(2), COL_LINE, COL_LINE, 0.0f, 1.0f);
  ac_paint_stroke(pix, W, H, fill, 2, (float)ac_mpx(2), cyan, cyan, (float)ac_mpx(2), 1.0f);
  ac_paint_stroke(pix, W, H, knob, 1, (float)ac_mpx(7), RGB(255, 255, 255), cyan,
                  (float)ac_mpx(g_acMenuDrag ? 5 : 3), 1.0f);
  wchar_t skin[40];
  swprintf(skin, 40, L"%ls ›", kAcSkinName[g_acAvatarSkin]);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_SKIN], 0xE77B, L"Вид", skin, ink, cyan, lineH);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_BUBBLES], 0xE8F2, L"Мысли ИИ рядом", NULL, ink, cyan, lineH);
  ac_menu_switch(pix, W, H, g_acMenuRow[ACM_BUBBLES], g_acAvatarBubbles);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_FACTS], 0xE82F, L"Факты и анекдоты", NULL, ink, cyan, lineH);
  ac_menu_switch(pix, W, H, g_acMenuRow[ACM_FACTS], g_acFacts);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_WANDER], 0xE805, L"Гулять рядом с местом", NULL, ink, cyan, lineH);
  ac_menu_switch(pix, W, H, g_acMenuRow[ACM_WANDER], g_acAvatarWander);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_WINDOWS], 0xE737, L"Садиться на окна", NULL, ink, cyan, lineH);
  ac_menu_switch(pix, W, H, g_acMenuRow[ACM_WINDOWS], g_acAvatarWindows);
  RECT sp = g_acMenuRow[ACM_SEP];
  float sep[4] = {(float)sp.left + ac_mpx(8), (sp.top + sp.bottom) * 0.5f, (float)sp.right - ac_mpx(8),
                  (sp.top + sp.bottom) * 0.5f};
  ac_paint_stroke(pix, W, H, sep, 2, 0.5f, hair, hair, 0.0f, 1.0f);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_DISMISS], 0xE711, L"Отозвать помощника", NULL, red, red, lineH);
  SelectObject(dc, oldFont);
  HGDIOBJ oldBmp = SelectObject(dc, g_acMenuBmp);
  RECT wr;
  GetWindowRect(g_acMenu, &wr);
  POINT at = {wr.left, wr.top}, origin = {0, 0};
  SIZE size = {W, H};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, g_acMenuAlpha, AC_SRC_ALPHA};
  UpdateLayeredWindow(g_acMenu, screen, &at, &size, dc, &origin, 0, &blend, ULW_ALPHA);
  SelectObject(dc, oldBmp);
  DeleteDC(dc);
  ReleaseDC(NULL, screen);
}

static int ac_menu_hit(int x, int y) {
  POINT p = {x, y};
  for (int i = 0; i < ACM_N; i++)
    if (ac_menu_pick(i) && PtInRect(&g_acMenuRow[i], p)) return i;
  return -1;
}

static void ac_menu_size_at(int x) {
  float x0, x1, y;
  ac_menu_track(&x0, &x1, &y);
  float t = (x - x0) / (x1 - x0);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  ac_avatar_apply_scale(AC_AVATAR_MIN + (int)(t * (AC_AVATAR_MAX - AC_AVATAR_MIN) + 0.5f));
  ac_menu_render();
}

static void ac_menu_activate(int i) {
  switch (i) {
  case ACM_CHAT: ac_menu_close(); aichat_show(); break;
  case ACM_SKIN:
    ac_avatar_set_skin((g_acAvatarSkin + 1) % AC_SKIN_N);
    ac_menu_render();
    break;
  case ACM_BUBBLES:
    g_acAvatarBubbles = !g_acAvatarBubbles;
    if (!g_acAvatarBubbles) ac_bubble_hide(TRUE);
    ac_avatar_save_settings();
    ac_menu_render();
    break;
  case ACM_FACTS:
    g_acFacts = !g_acFacts;
    g_acFactNext = GetTickCount() + AC_FACT_EVERY;
    if (!g_acFacts && g_acFactId) { ai_cancel(g_acFactId); g_acFactId = 0; }
    ac_avatar_save_settings();
    ac_menu_render();
    ac_avatar_bubble_ex(g_acFacts ? L"Щёлкните по мне — расскажу факт или анекдот. Сам — раз в 15 минут, если вы не заняты."
                                  : L"Хорошо, молчу. Вернуть — в этом же меню.",
                        FALSE, 4200, TRUE);
    break;
  case ACM_WANDER:
    g_acAvatarWander = !g_acAvatarWander;
    g_acAvatarMoving = FALSE;
    ac_avatar_save_settings();
    ac_menu_render();
    break;
  case ACM_WINDOWS:
    g_acAvatarWindows = !g_acAvatarWindows;
    g_acPerch = g_acPerchNext = NULL;
    g_acFalling = FALSE;
    ac_avatar_save_settings();
    ac_menu_render();
    break;
  case ACM_DISMISS: ac_menu_close(); ac_avatar_dismiss(); break;
  }
}

static LRESULT CALLBACK AcMenuProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_NCHITTEST: return HTCLIENT;
  case WM_MOUSEMOVE: {
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    if (!g_acMenuTrack) {
      TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
      g_acMenuTrack = TrackMouseEvent(&t);
    }
    if (g_acMenuDrag) { ac_menu_size_at(x); return 0; }
    int hot = ac_menu_hit(x, y);
    if (hot != g_acMenuHot) { g_acMenuHot = hot; ac_menu_render(); }
    return 0;
  }
  case WM_MOUSELEAVE:
    g_acMenuTrack = FALSE;
    if (!g_acMenuDrag && g_acMenuHot >= 0) { g_acMenuHot = -1; ac_menu_render(); }
    return 0;
  case WM_LBUTTONDOWN:
    if (ac_menu_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) == ACM_SIZE) {
      g_acMenuDrag = TRUE;
      SetCapture(h);
      ac_menu_size_at(GET_X_LPARAM(lParam));
    }
    return 0;
  case WM_LBUTTONUP:
  case WM_RBUTTONUP: {
    if (g_acMenuDrag) {
      g_acMenuDrag = FALSE;
      ReleaseCapture();
      ac_menu_render();
      return 0;
    }
    int i = ac_menu_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    if (i >= 0 && i != ACM_SIZE) ac_menu_activate(i);
    return 0;
  }
  case WM_CAPTURECHANGED:
    if (g_acMenuDrag) { g_acMenuDrag = FALSE; ac_menu_render(); }
    return 0;
  case WM_MOUSEWHEEL: /* колесо над меню — размер робота */
    ac_avatar_apply_scale((int)(g_acAvatarSize * powf(1.08f, GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA) + 0.5f));
    ac_menu_render();
    return 0;
  case WM_KEYDOWN: {
    int k = (int)wParam;
    if (k == VK_ESCAPE) { ac_menu_close(); return 0; }
    if (k == VK_UP || k == VK_DOWN) {
      int i = g_acMenuHot;
      for (int n = 0; n < ACM_N; n++) {
        i = k == VK_DOWN ? (i + 1) % ACM_N : (i <= 0 ? ACM_N - 1 : i - 1);
        if (ac_menu_pick(i)) break;
      }
      g_acMenuHot = i;
      ac_menu_render();
      return 0;
    }
    if ((k == VK_LEFT || k == VK_RIGHT) && g_acMenuHot == ACM_SKIN) {
      ac_avatar_set_skin((g_acAvatarSkin + (k == VK_RIGHT ? 1 : AC_SKIN_N - 1)) % AC_SKIN_N);
      ac_menu_render();
      return 0;
    }
    if ((k == VK_LEFT || k == VK_RIGHT) && g_acMenuHot == ACM_SIZE) {
      ac_avatar_apply_scale(g_acAvatarSize + (k == VK_RIGHT ? 8 : -8));
      ac_menu_render();
      return 0;
    }
    if ((k == VK_RETURN || k == VK_SPACE) && g_acMenuHot >= 0) { ac_menu_activate(g_acMenuHot); return 0; }
    break;
  }
  case WM_ACTIVATE:
    if (LOWORD(wParam) == WA_INACTIVE) PostMessageW(h, WM_CLOSE, 0, 0); /* щёлкнули мимо */
    return 0;
  case WM_CLOSE:
    if (h == g_acMenu) ac_menu_close();
    return 0;
  case WM_TIMER:
    if (wParam == AC_MENU_TIMER) {
      g_acMenuAlpha = g_acMenuAlpha > 255 - 64 ? 255 : g_acMenuAlpha + 64;
      if (g_acMenuAlpha == 255) KillTimer(h, AC_MENU_TIMER);
      ac_menu_render();
      return 0;
    }
    break;
  case WM_PAINT: ValidateRect(h, NULL); return 0;
  }
  return DefWindowProcW(h, msg, wParam, lParam);
}

static void ac_avatar_menu(HWND hwnd) {
  (void)hwnd;
  ac_menu_close();
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = AcMenuProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.lpszClassName = L"CursorPadAvatarMenu";
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
  HDC screen = GetDC(NULL);
  g_acMenuK = screen ? MulDiv(100, GetDeviceCaps(screen, LOGPIXELSX), 96) : 100;
  if (screen) ReleaseDC(NULL, screen);
  g_acMenuFont = ac_menu_font(14, FW_NORMAL, L"Segoe UI");
  g_acMenuBold = ac_menu_font(15, FW_SEMIBOLD, L"Segoe UI");
  g_acMenuSmall = ac_menu_font(12, FW_NORMAL, L"Segoe UI");
  g_acMenuIcons = ac_menu_font(16, FW_NORMAL, L"Segoe Fluent Icons");
  {
    /* нет Fluent (Windows 10) — MDL2: у значков те же коды */
    HDC probe = CreateCompatibleDC(NULL);
    wchar_t face[64] = L"";
    if (probe) {
      HGDIOBJ o = SelectObject(probe, g_acMenuIcons);
      GetTextFaceW(probe, 64, face);
      SelectObject(probe, o);
      DeleteDC(probe);
    }
    if (wcscmp(face, L"Segoe Fluent Icons")) {
      DeleteObject(g_acMenuIcons);
      g_acMenuIcons = ac_menu_font(16, FW_NORMAL, L"Segoe MDL2 Assets");
    }
  }
  ac_menu_layout();
  POINT p;
  GetCursorPos(&p);
  int m = ac_mpx(14), x = p.x - m, y = p.y - m;
  MONITORINFO mi = {sizeof(mi)};
  if (GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi)) {
    if (x + g_acMenuW > mi.rcWork.right) x = p.x - g_acMenuW + m; /* справа не влезает — влево от курсора */
    if (y + g_acMenuH > mi.rcWork.bottom) y = mi.rcWork.bottom - g_acMenuH;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
  }
  g_acMenu = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"CursorPadAvatarMenu", L"Помощник",
                             WS_POPUP, x, y, g_acMenuW, g_acMenuH, NULL, NULL, g_inst, NULL);
  if (!g_acMenu) { ac_menu_free(); return; }
  ac_bubble_hide(FALSE);
  g_acMenuHot = -1;
  g_acMenuAlpha = 64;
  ac_menu_render();
  ShowWindow(g_acMenu, SW_SHOW);
  SetForegroundWindow(g_acMenu);
  SetFocus(g_acMenu);
  SetTimer(g_acMenu, AC_MENU_TIMER, 16, NULL);
}

/* щелчок без перетаскивания — робот радуется; say — с подсказкой в пузыре */
static void ac_avatar_poke_ex(BOOL say) {
  static const wchar_t *kPhrases[] = {
    L"Я тут! Двойной щелчок — открыть чат",
    L"Щекотно!",
    L"Ctrl + колесо мыши — сделать меня больше или меньше",
    L"Думаю о хорошем. Правая кнопка — настройки",
    L"Если что-то надо — спрашивайте",
    L"Перетащите меня, куда удобно",
  };
  DWORD now = GetTickCount();
  g_acAvatarJoyUntil = (now + 1600) | 1; /* 0 — «не радуется» */
  g_acHopAt = now | 1;
  if (g_acAvatarSkin == AC_SKIN_GIRL) ac_girl_shot(AGA_TAP);
  if (ac_avatar_random() % 3 == 0) ac_emo((ac_avatar_random() & 1) ? AC_EMO_LOVE : AC_EMO_WINK, 1300);
  if (g_acAvatarState == AC_AV_IDLE || g_acAvatarState == AC_AV_SLEEP) {
    ac_avatar_set(AC_AV_IDLE);
    g_acAvatarState = AC_AV_WAVE;
    g_acAvatarFrame = 0;
    g_acAvatarStateAt = now;
    g_acAvatarAmbient = TRUE;
    g_acAvatarTransientUntil = now + ac_state_ms(AC_AV_WAVE);
  }
  if (!g_acAvatarBusy && say)
    ac_avatar_bubble(kPhrases[ac_avatar_random() % (sizeof(kPhrases) / sizeof(kPhrases[0]))], FALSE, 2600);
  ac_avatar_present();
}

static void ac_avatar_poke(void) { ac_avatar_poke_ex(TRUE); }

/* ---------- призвать / отозвать: «Ещё», меню аватара, кнопка в чате */

static BOOL ac_avatar_here(void) { return g_acAvatar && IsWindowVisible(g_acAvatar) && g_acAvatarAnim != 2; }

/* курсор на роботе или его пузыре (прозрачные пиксели окна не считаются) —
   блокнот, который ездит за курсором, в это время прячется (cursorpad.c) */
static BOOL ac_avatar_under(POINT p) {
  if (!g_acAvatar || !IsWindowVisible(g_acAvatar)) return FALSE;
  HWND w = WindowFromPoint(p);
  return w && (w == g_acAvatar || w == g_acBubble || w == g_acMenu);
}

static void ac_avatar_menu_label(void) {
  lstrcpynW(g_acAvatarMenuText, ac_avatar_here() ? L"Отозвать помощника" : L"Призвать помощника", 40);
}

/* Появляется на своём месте: выпрыгивает, машет и здоровается. Уже здесь — просто машет. */
static void ac_avatar_summon(void) {
  if (ac_avatar_here()) { ac_avatar_poke(); return; }
  DWORD now = GetTickCount();
  g_acAvatarAnim = 1;
  g_acAvatarAnimAt = now;
  if (g_acAvatar)
    SetWindowPos(g_acAvatar, HWND_TOPMOST, g_acAvatarHome.x, g_acAvatarHome.y, g_acAvatarSize, g_acAvatarSize,
                 SWP_NOACTIVATE);
  ac_avatar_show();
  if (!g_acAvatar) { g_acAvatarAnim = 0; return; }
  SetTimer(g_acAvatar, AC_AVATAR_TIMER, AC_AVATAR_FAST_TICK, NULL);
  ac_avatar_set(AC_AV_IDLE);
  ac_avatar_ambient(AC_AV_WAVE, now);
  g_acAvatarJoyUntil = (now + 1400) | 1;
  ac_avatar_save_settings();
  ac_avatar_menu_label();
  if (!g_acAvatarBusy) ac_avatar_bubble(L"Я тут! Двойной щелчок — чат с ИИ", FALSE, 2600);
}

/* Схлопывается и исчезает; место и масштаб остаются для следующего призыва. */
static void ac_avatar_dismiss(void) {
  if (!ac_avatar_here()) return;
  ac_bubble_hide(TRUE);
  g_acAvatarAnim = 2;
  g_acAvatarAnimAt = GetTickCount();
  g_acAvatarJoyUntil = 0;
  g_acAvatarMoving = FALSE;
  SetTimer(g_acAvatar, AC_AVATAR_TIMER, AC_AVATAR_FAST_TICK, NULL);
  ac_avatar_menu_label();
}

/* ---------- факты и анекдоты */

/* рассказать: пузырь-реплика, «говорит», после анекдота — смеётся */
static void ac_fact_say(const wchar_t *text, int joke) {
  fj_note(&g_acFactSt, text, joke);
  size_t len = wcslen(text);
  DWORD ms = (DWORD)len * 62, talk = (DWORD)len * 38;
  if (ms < 5000) ms = 5000;
  if (ms > 16000) ms = 16000;
  ac_avatar_set(AC_AV_TALKING);
  g_acAvatarTalkUntil = GetTickCount() + (talk < 2500 ? 2500 : talk > 7000 ? 7000 : talk);
  g_acLaughAt = joke ? g_acAvatarTalkUntil | 1 : 0;
  ac_avatar_bubble_ex(text, FALSE, ms, TRUE);
}

static void ac_fact_from_list(int joke) {
  ac_fact_say(kFjItems[fj_pick(&g_acFactSt, joke, ac_avatar_random())].text, joke);
}

/* click — просили щелчком. Модель поднята и свободна — придумывает она, иначе список. */
static void ac_fact_tell(BOOL click) {
  if (!ac_avatar_here()) return;
  if (g_acFactId) {
    if (click) ac_avatar_bubble_ex(L"Секунду, придумываю…", TRUE, 1800, TRUE);
    return;
  }
  int joke = fj_kind(&g_acFactSt, ac_avatar_random());
  if (!g_acAvatarBusy && ai_warm()) {
    wchar_t q[300];
    fj_question(joke, (int)(ac_avatar_random() % FJ_TOPICS), q, 300);
    AiTurn turn = {0, q, NULL, 0};
    ai_out_cap(200);
    int id = ai_ask_chat_plain_async(fj_instruction(), &turn, 1, click ? 25000 : 45000, g_acAvatar, WM_AC_FACT, NULL);
    ai_out_cap(0);
    if (id) {
      g_acFactId = id;
      g_acFactKind = joke;
      if (click) {
        ac_avatar_ambient(AC_AV_THINKING, GetTickCount());
        ac_avatar_bubble_ex(joke ? L"Сейчас, вспомню анекдот…" : L"Сейчас, вспомню кое-что…", TRUE, 25000, TRUE);
      }
      return;
    }
  }
  ac_fact_from_list(joke);
}

/* ответ модели: годный и не повтор — его, иначе строку из списка; отменённый — ничего */
static void ac_fact_result(AiResult *r) {
  if (!r) return;
  if (r->id == g_acFactId) {
    g_acFactId = 0;
    if (r->status == AI_OK && r->text && fj_clean(r->text) && !fj_seen(&g_acFactSt, fj_key(r->text)))
      ac_fact_say(r->text, g_acFactKind);
    else if (r->status != AI_CANCELLED && ac_avatar_here())
      ac_fact_from_list(g_acFactKind);
  }
  ai_result_free(r);
}

/* раз в 15 минут — если ИИ не занят, человек за компьютером и не пишет в чате; нет — через минуту */
static void ac_fact_auto(DWORD now) {
  LASTINPUTINFO li = {sizeof(li), 0};
  BOOL away = GetLastInputInfo(&li) && now - li.dwTime > AC_FACT_AWAY;
  BOOL chatting = (g_acWnd && GetForegroundWindow() == g_acWnd) || ac_mini_open();
  BOOL bubble = g_acBubble && IsWindowVisible(g_acBubble);
  if (away || chatting || bubble || g_acAvatarBusy || g_acFactId || g_acHeld || g_acMenu || g_acAvatarAnim) {
    g_acFactNext = now + 60000;
    return;
  }
  g_acFactNext = now + AC_FACT_EVERY;
  ac_fact_tell(FALSE);
}

static void ac_avatar_toggle(void) {
  if (ac_avatar_here()) ac_avatar_dismiss();
  else ac_avatar_summon();
}

static LRESULT CALLBACK AcAvatarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
  case WM_NCHITTEST: return HTCAPTION;
  case WM_MOUSEWHEEL:
    if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) { ac_avatar_wheel(GET_WHEEL_DELTA_WPARAM(wParam)); return 0; }
    break;
  case WM_NCRBUTTONUP:
  case WM_RBUTTONUP: ac_avatar_menu(hwnd); return 0;
  case WM_CONTEXTMENU: ac_avatar_menu(hwnd); return 0;
  case WM_NCLBUTTONDBLCLK:
  case WM_LBUTTONDBLCLK:
    KillTimer(hwnd, AC_FACT_TIMER); /* двойной щелчок — чат, а не факт */
    aichat_show();
    return 0;
  case WM_AC_FACT: ac_fact_result((AiResult *)lParam); return 0;
  case WM_ENTERSIZEMOVE: { /* взяли мышью: висит за точку хвата */
    POINT p;
    GetCursorPos(&p);
    GetWindowRect(hwnd, &g_acAvatarDragFrom);
    g_acGrip.x = p.x - g_acAvatarDragFrom.left;
    g_acGrip.y = p.y - g_acAvatarDragFrom.top;
    g_acLastX = g_acAvatarDragFrom.left;
    g_acHeld = TRUE;
    g_acHeldAt = GetTickCount();
    g_acAvatarMoving = FALSE;
    g_acPerch = g_acPerchNext = NULL;
    g_acFalling = FALSE;
    g_acHopAt = 0;
    return 0;
  }
  case WM_EXITSIZEMOVE: {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    g_acHeld = FALSE;
    g_acLandAt = GetTickCount() | 1;
    if (abs(rc.left - g_acAvatarDragFrom.left) + abs(rc.top - g_acAvatarDragFrom.top) < 4) {
      BOOL fact = g_acFacts && !ac_mini_open(); /* щелчок — факт, если не окажется двойным */
      ac_avatar_poke_ex(!fact);
      if (fact) SetTimer(hwnd, AC_FACT_TIMER, GetDoubleClickTime(), NULL);
    } else {
      g_acAvatarHome.x = rc.left;
      g_acAvatarHome.y = rc.top;
      if (fabsf(g_acSwingV) > 0.06f || fabsf(g_acSwing) > 0.3f) ac_emo(AC_EMO_DIZZY, 1400); /* раскачали — кружится голова */
      else g_acAvatarJoyUntil = (GetTickCount() + 900) | 1;
      if (g_acAvatarWindows) { g_acFalling = TRUE; g_acFallV = 0.0f; } /* отпустили в воздухе — падает */
    }
    ac_avatar_save_settings();
    return 0;
  }
  case WM_MOVE:
    if (g_acBubble && IsWindowVisible(g_acBubble)) ac_bubble_update();
    if (!g_acAvatarFollowing) ac_mini_follow(); /* мини-чат едет следом */
    break;
  case WM_TIMER:
    if (wParam == AC_AVATAR_TIMER) { ac_avatar_tick(); return 0; }
    if (wParam == AC_FACT_TIMER) { KillTimer(hwnd, AC_FACT_TIMER); ac_fact_tell(TRUE); return 0; }
    break;
  case WM_PAINT: ValidateRect(hwnd, NULL); return 0;
  case WM_CLOSE: ac_avatar_hide(); return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ac_avatar_show(void) {
  if (g_acAvatar) {
    SetTimer(g_acAvatar, AC_AVATAR_TIMER, AC_AVATAR_TICK, NULL);
    ShowWindow(g_acAvatar, SW_SHOWNOACTIVATE);
    ac_avatar_present();
    return;
  }
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = AcAvatarProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_SIZEALL);
  wc.lpszClassName = L"CursorPadFloatingAvatar";
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

  RECT work;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  if (g_dataDir[0]) {
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\ai_avatar.ini", g_dataDir);
    g_acAvatarSize = GetPrivateProfileIntW(L"Avatar", L"Size", AC_AVATAR_DEFAULT_SIZE, path);
    if (g_acAvatarSize < AC_AVATAR_MIN) g_acAvatarSize = AC_AVATAR_MIN;
    if (g_acAvatarSize > AC_AVATAR_MAX) g_acAvatarSize = AC_AVATAR_MAX;
    /* GetPrivateProfileIntW отдаёт 0 вместо отрицательных: монитор слева от главного */
    wchar_t value[32];
    GetPrivateProfileStringW(L"Avatar", L"X", L"", value, 32, path);
    g_acAvatarX = value[0] ? _wtoi(value) : -1;
    GetPrivateProfileStringW(L"Avatar", L"Y", L"", value, 32, path);
    g_acAvatarY = value[0] ? _wtoi(value) : -1;
    g_acAvatarWander = GetPrivateProfileIntW(L"Avatar", L"Wander", 1, path) != 0;
    g_acAvatarBubbles = GetPrivateProfileIntW(L"Avatar", L"Bubbles", 1, path) != 0;
    g_acAvatarWindows = GetPrivateProfileIntW(L"Avatar", L"Windows", 0, path) != 0;
    g_acFacts = GetPrivateProfileIntW(L"Avatar", L"Facts", 0, path) != 0;
    g_acFactNext = GetTickCount() + AC_FACT_EVERY;
    GetPrivateProfileStringW(L"Avatar", L"Skin", L"robot", value, 32, path);
    g_acAvatarSkin = AC_SKIN_ROBOT;
    for (int i = 0; i < AC_SKIN_N; i++)
      if (!_wcsicmp(value, kAcSkinKey[i])) g_acAvatarSkin = i;
  }
  /* сохранённое место — если оно на каком-нибудь мониторе */
  RECT saved = {g_acAvatarX, g_acAvatarY, g_acAvatarX + g_acAvatarSize, g_acAvatarY + g_acAvatarSize};
  HMONITOR monitor = MonitorFromRect(&saved, MONITOR_DEFAULTTONULL);
  MONITORINFO mi = {sizeof(mi)};
  if (monitor && GetMonitorInfoW(monitor, &mi) && g_acAvatarX >= mi.rcWork.left && g_acAvatarY >= mi.rcWork.top &&
      saved.right <= mi.rcWork.right && saved.bottom <= mi.rcWork.bottom) {
    g_acAvatarHome.x = g_acAvatarX;
    g_acAvatarHome.y = g_acAvatarY;
  } else {
    g_acAvatarHome.x = work.right - g_acAvatarSize - 24;
    g_acAvatarHome.y = work.bottom - g_acAvatarSize - 36;
  }
  g_acAvatarRng = GetTickCount() | 1;
  g_acAvatar = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                               L"CursorPadFloatingAvatar", L"Мини-ИИ — перетащите мышью, ПКМ: настройки",
                               WS_POPUP, g_acAvatarHome.x, g_acAvatarHome.y, g_acAvatarSize, g_acAvatarSize,
                               NULL, NULL, g_inst, NULL);
  if (!g_acAvatar) return;
  SetTimer(g_acAvatar, AC_AVATAR_TIMER, AC_AVATAR_TICK, NULL);
  ShowWindow(g_acAvatar, SW_SHOWNOACTIVATE);
  ac_avatar_save_settings();
  ac_avatar_set(AC_AV_IDLE);
  g_acAvatarNextBlink = GetTickCount() + 1500;
}

static void ac_avatar_startup(void) {
  if (g_dataDir[0]) {
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\ai_avatar.ini", g_dataDir);
    if (!GetPrivateProfileIntW(L"Avatar", L"Visible", 1, path)) return;
  }
  ac_avatar_show();
}

#ifdef SET_TEST
/* проверочная сборка: --avatarshot выход.bmp — кадры помощника полосой на светлом фоне, без окон:
   робот, затем кошка в покое, говорит, рада, удивлена, ошибка, смущена, злится, машет, в прыжке и на курсоре,
   затем кошко-девочка: покой, говорит, думает, читает, машет, радость, огорчение, сон, слушает,
   потягивается, гладят, касание, на курсоре */
static void ac_avatar_shot(const wchar_t *out) {
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  /* вид, состояние, эмоция; 1 — в прыжке, 2 — держат мышью; мс от начала состояния (0 — 400);
     разовое действие кошко-девочки (−1 — нет) */
  static const int kShot[][6] = {
      {AC_SKIN_ROBOT, AC_AV_IDLE, AC_EMO_NONE, 0, 0, -1},        {AC_SKIN_CAT, AC_AV_IDLE, AC_EMO_NONE, 0, 0, -1},
      {AC_SKIN_CAT, AC_AV_TALKING, AC_EMO_NONE, 0, 0, -1},       {AC_SKIN_CAT, AC_AV_SUCCESS, AC_EMO_NONE, 0, 0, -1},
      {AC_SKIN_CAT, AC_AV_IDLE, AC_EMO_SURPRISED, 0, 0, -1},     {AC_SKIN_CAT, AC_AV_ERROR, AC_EMO_NONE, 0, 0, -1},
      {AC_SKIN_CAT, AC_AV_IDLE, AC_EMO_SHY, 0, 0, -1},           {AC_SKIN_CAT, AC_AV_IDLE, AC_EMO_ANGRY, 0, 0, -1},
      {AC_SKIN_CAT, AC_AV_WAVE, AC_EMO_NONE, 0, 0, -1},          {AC_SKIN_CAT, AC_AV_IDLE, AC_EMO_NONE, 1, 0, -1},
      {AC_SKIN_CAT, AC_AV_IDLE, AC_EMO_NONE, 2, 0, -1},
      {AC_SKIN_GIRL, AC_AV_IDLE, AC_EMO_NONE, 0, 0, -1},         {AC_SKIN_GIRL, AC_AV_TALKING, AC_EMO_NONE, 0, 1100, -1},
      {AC_SKIN_GIRL, AC_AV_THINKING, AC_EMO_NONE, 0, 1200, -1},  {AC_SKIN_GIRL, AC_AV_FILES, AC_EMO_NONE, 0, 1200, -1},
      {AC_SKIN_GIRL, AC_AV_WAVE, AC_EMO_NONE, 0, 1300, -1},      {AC_SKIN_GIRL, AC_AV_SUCCESS, AC_EMO_NONE, 0, 850, -1},
      {AC_SKIN_GIRL, AC_AV_ERROR, AC_EMO_NONE, 0, 1400, -1},     {AC_SKIN_GIRL, AC_AV_SLEEP, AC_EMO_NONE, 0, 1000, -1},
      {AC_SKIN_GIRL, AC_AV_LISTENING, AC_EMO_NONE, 0, 1000, -1}, {AC_SKIN_GIRL, AC_AV_IDLE, AC_EMO_NONE, 0, 1300, AGA_WAKE},
      {AC_SKIN_GIRL, AC_AV_IDLE, AC_EMO_NONE, 0, 1300, AGA_PET}, {AC_SKIN_GIRL, AC_AV_IDLE, AC_EMO_NONE, 0, 450, AGA_TAP},
      {AC_SKIN_GIRL, AC_AV_IDLE, AC_EMO_NONE, 2, 0, -1}};
  int n = (int)(sizeof(kShot) / sizeof(kShot[0])), S = g_acAvatarSize = 240, W = S * n, stride = (W * 3 + 3) & ~3;
  BITMAPINFO info;
  memset(&info, 0, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = S;
  info.bmiHeader.biHeight = -S;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  g_acAvatarFrameBitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&g_acAvatarFramePixels, NULL, 0);
  BYTE *strip = (BYTE *)calloc((size_t)stride * S, 1);
  if (!g_acAvatarFrameBitmap || !strip) ExitProcess(1);
  DWORD now = (GetTickCount() / 260) * 260 + 135; /* «говорит» — кадр с открытым ртом */
  for (int i = 0; i < n; i++) {
    g_acAvatarSkin = kShot[i][0];
    if (!ac_avatar_load()) ExitProcess(2);
    g_acAvatarState = kShot[i][1];
    DWORD ms = kShot[i][4] ? (DWORD)kShot[i][4] : 400;
    g_acAvatarStateAt = now - ms;
    g_agShot = kShot[i][5];
    g_agShotAt = now - ms;
    g_acEmo = kShot[i][2];
    g_acEmoUntil = g_acEmo ? now + 10000 : 0;
    g_acHopAt = kShot[i][3] == 1 ? (now - 210) | 1 : 0; /* верх прыжка */
    g_acLandAt = 0;
    g_acHeld = kShot[i][3] == 2;
    g_acGrip.x = S * 6 / 10;
    g_acGrip.y = S / 5;
    g_acSwing = g_acHeld ? 0.35f : 0.0f;
    BYTE alpha;
    ac_avatar_render(now, &alpha);
    if (g_agBlendAt) { /* без смешивания с прежним кадром полосы */
      g_agBlendAt = 0;
      ac_avatar_render(now, &alpha);
    }
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++) {
        const BYTE *s = g_acAvatarFramePixels + ((size_t)y * S + x) * 4;
        BYTE *d = strip + (size_t)(S - 1 - y) * stride + (size_t)(i * S + x) * 3;
        static const BYTE kBg[3] = {244, 238, 232};
        for (int c = 0; c < 3; c++) d[c] = (BYTE)(s[c] + kBg[c] * (255 - s[3]) / 255);
      }
  }
  BITMAPFILEHEADER fh;
  BITMAPINFOHEADER ih;
  memset(&fh, 0, sizeof(fh));
  memset(&ih, 0, sizeof(ih));
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(ih);
  fh.bfSize = fh.bfOffBits + (DWORD)stride * S;
  ih.biSize = sizeof(ih);
  ih.biWidth = W;
  ih.biHeight = S;
  ih.biPlanes = 1;
  ih.biBitCount = 24;
  ih.biSizeImage = (DWORD)stride * S;
  HANDLE f = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  DWORD w;
  BOOL ok = f != INVALID_HANDLE_VALUE && WriteFile(f, &fh, sizeof(fh), &w, NULL) && WriteFile(f, &ih, sizeof(ih), &w, NULL) &&
            WriteFile(f, strip, (DWORD)stride * S, &w, NULL);
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  ExitProcess(ok ? 0 : 3);
}
#endif

static void ac_avatar_destroy(void) {
  if (g_acBubble) { DestroyWindow(g_acBubble); g_acBubble = NULL; }
  if (g_acBubbleBitmap) { DeleteObject(g_acBubbleBitmap); g_acBubbleBitmap = NULL; g_acBubblePixels = NULL; }
  if (g_acBubbleFont) { DeleteObject(g_acBubbleFont); g_acBubbleFont = NULL; }
  if (g_acAvatar) { KillTimer(g_acAvatar, AC_AVATAR_TIMER); DestroyWindow(g_acAvatar); g_acAvatar = NULL; }
  if (g_acAvatarFrameBitmap) { DeleteObject(g_acAvatarFrameBitmap); g_acAvatarFrameBitmap = NULL; }
  if (g_acAvatarAtlas) { DeleteObject(g_acAvatarAtlas); g_acAvatarAtlas = NULL; g_acAvatarPixels = NULL; }
  if (g_acCatAtlas) { DeleteObject(g_acCatAtlas); g_acCatAtlas = NULL; g_acCatPixels = NULL; }
  if (g_acGirlAtlas) { DeleteObject(g_acGirlAtlas); g_acGirlAtlas = NULL; g_acGirlPixels = NULL; }
}

static DWORD ac_avatar_random(void) {
  if (!g_acAvatarRng) g_acAvatarRng = GetTickCount() | 1;
  g_acAvatarRng = g_acAvatarRng * 1664525u + 1013904223u;
  return g_acAvatarRng >> 8;
}

/* сколько длится петля состояния у нынешнего вида */
static DWORD ac_state_ms(int state) {
  if (g_acAvatarSkin == AC_SKIN_GIRL) return ac_girl_state_ms(state);
  return (DWORD)AC_AVATAR_COLS * kAcAvatarFrameMs[state];
}

static void ac_avatar_ambient(int state, DWORD now) {
  g_acAvatarState = state;
  g_acAvatarFrame = 0;
  g_acAvatarStateAt = now;
  g_acAvatarAmbient = TRUE;
  g_acAvatarTransientUntil = now + ac_state_ms(state) * (state == AC_AV_THINKING ? 2 : 1); /* целые петли */
}

static void ac_avatar_move(void) {
  if (!g_acAvatar || !IsWindowVisible(g_acAvatar)) return;
  RECT wr;
  GetWindowRect(g_acAvatar, &wr);
  int x = wr.left, y = wr.top;
  int dx = g_acAvatarTarget.x - x, dy = g_acAvatarTarget.y - y;
  double distance = sqrt((double)dx * dx + (double)dy * dy);
  if (distance <= 4.0) {
    SetWindowPos(g_acAvatar, HWND_TOPMOST, g_acAvatarTarget.x, g_acAvatarTarget.y,
                 g_acAvatarSize, g_acAvatarSize, SWP_NOACTIVATE);
    g_acAvatarMoving = FALSE;
    if (g_acPerchNext && IsWindow(g_acPerchNext)) { /* дошёл до окна — садится на край */
      RECT r;
      GetWindowRect(g_acPerchNext, &r);
      g_acPerch = g_acPerchNext;
      g_acPerchDx = g_acAvatarTarget.x - r.left;
      g_acLandAt = GetTickCount() | 1;
    }
    g_acPerchNext = NULL;
    g_acAvatarNextMove = GetTickCount() + 20000 + (ac_avatar_random() % 22000);
    return;
  }
  double speed = distance < 40.0 ? 1.0 + distance / 40.0 : 2.0; /* ~60 px/с, к концу пути замедляется */
  int stepX = (int)(dx * speed / distance), stepY = (int)(dy * speed / distance);
  if (!stepX && dx) stepX = dx > 0 ? 1 : -1;
  if (!stepY && dy) stepY = dy > 0 ? 1 : -1;
  SetWindowPos(g_acAvatar, HWND_TOPMOST, x + stepX, y + stepY,
               g_acAvatarSize, g_acAvatarSize, SWP_NOACTIVATE);
}

/* окно, на котором можно сидеть: видимое, не развёрнутое, не наше, не рабочий стол */
static BOOL ac_perch_ok(HWND h) {
  if (!h || !IsWindowVisible(h) || IsIconic(h) || IsZoomed(h)) return FALSE;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid == GetCurrentProcessId()) return FALSE;
  if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return FALSE;
  wchar_t cls[64];
  if (!GetClassNameW(h, cls, 64) || !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
      !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) return FALSE;
  RECT r;
  GetWindowRect(h, &r);
  return r.right - r.left > g_acAvatarSize && r.bottom - r.top > 60;
}

/* верх окна под точкой (x, y) экрана; 0 — нет такого */
static HWND ac_window_below(int x, int y) {
  POINT p = {x, y};
  HWND h = WindowFromPoint(p);
  h = h ? GetAncestor(h, GA_ROOT) : NULL;
  return ac_perch_ok(h) ? h : NULL;
}


/* Окна: едет на своём окне, падает, если опоры нет, иногда перебирается
   на активное окно. Только при включённом «Садиться на окна». */
static void ac_avatar_windows(DWORD now, RECT wr, BOOL idle) {
  int S = g_acAvatarSize, feet = (int)(S * (g_acAvatarSkin == AC_SKIN_ROBOT ? AC_FEET : ac_avatar_feet()));
  if (g_acHeld) return;
  if (g_acPerch) {
    if (!ac_perch_ok(g_acPerch)) { g_acPerch = NULL; g_acFalling = TRUE; g_acFallV = 0.0f; ac_emo(AC_EMO_SURPRISED, 800); }
    else {
      RECT r;
      GetWindowRect(g_acPerch, &r);
      int x = r.left + g_acPerchDx, y = r.top - feet;
      if (x > r.right - S / 2) x = r.right - S / 2;
      if (x < r.left - S / 2) x = r.left - S / 2;
      if (x != wr.left || y != wr.top) SetWindowPos(g_acAvatar, HWND_TOPMOST, x, y, S, S, SWP_NOACTIVATE);
      return;
    }
  }
  if (g_acFalling) {
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(g_acAvatar, MONITOR_DEFAULTTONEAREST), &mi);
    g_acFallV += 1.5f;
    if (g_acFallV > 38.0f) g_acFallV = 38.0f;
    int x = wr.left, y = wr.top + (int)g_acFallV, floorY = mi.rcWork.bottom - feet - 2;
    /* край окна между старыми и новыми «ногами» — встаёт на него */
    for (int py = wr.top + feet + 1; py <= y + feet && py < mi.rcWork.bottom; py += 3) {
      HWND h = ac_window_below(x + S / 2, py);
      RECT r;
      if (h && GetWindowRect(h, &r) && r.top >= wr.top + feet - 2 && r.top <= py) {
        g_acPerch = h;
        g_acPerchDx = x - r.left;
        y = r.top - feet;
        break;
      }
    }
    if (!g_acPerch && y >= floorY) y = floorY;
    if (g_acPerch || y >= floorY) {
      g_acFalling = FALSE;
      g_acLandAt = now | 1;
      if (g_acFallV > 12.0f && !g_acEmo) ac_emo(AC_EMO_DIZZY, 900);
      g_acAvatarHome.x = x;
      g_acAvatarHome.y = y;
    }
    SetWindowPos(g_acAvatar, HWND_TOPMOST, x, y, S, S, SWP_NOACTIVATE);
    return;
  }
  /* в покое иногда перебирается на активное окно */
  if (idle && !g_acAvatarMoving && !ac_mini_open() && ac_due(now, g_acAvatarNextMove) && (ac_avatar_random() % 3) == 0) {
    HWND fg = GetForegroundWindow();
    RECT r;
    if (ac_perch_ok(fg) && GetWindowRect(fg, &r) && r.top - feet > 0) {
      int span = r.right - r.left - S;
      g_acAvatarTarget.x = r.left + (span > 0 ? (int)(ac_avatar_random() % (DWORD)span) : 0);
      g_acAvatarTarget.y = r.top - feet;
      g_acPerchNext = fg;
      g_acAvatarMoving = TRUE;
    }
  }
}

static void ac_avatar_tick(void) {
  DWORD now = GetTickCount();
  int S = g_acAvatarSize;
  if (g_acAvatarAnim && now - g_acAvatarAnimAt >= AC_AVATAR_ANIM_MS) {
    if (g_acAvatarAnim == 2) { g_acAvatarAnim = 0; ac_avatar_hide(); ac_avatar_menu_label(); return; }
    g_acAvatarAnim = 0;
    SetTimer(g_acAvatar, AC_AVATAR_TIMER, AC_AVATAR_TICK, NULL);
  }

  /* взгляд за курсором; курсор рядом — не спать */
  POINT cur;
  RECT wr;
  GetCursorPos(&cur);
  GetWindowRect(g_acAvatar, &wr);
  const short *f = kAcAvatarFace[g_acAvatarState][g_acAvatarFrame];
  float k = (float)S / AC_AVATAR_FRAME / 8.0f;
  float dx = cur.x - (wr.left + (f[0] + f[2]) * 0.5f * k), dy = cur.y - (wr.top + (f[1] + f[3]) * 0.5f * k);
  BOOL nearby = dx * dx + dy * dy < (float)S * S * 1.6f;
  float tx = dx / (fabsf(dx) + S * 0.9f), ty = dy / (fabsf(dy) + S * 0.9f);
  switch (g_acAvatarState) {
  case AC_AV_THINKING: tx = -0.6f + 0.2f * sinf(now / 900.0f); ty = -0.8f; break;
  case AC_AV_FILES: tx = 0.6f * sinf(now / 650.0f); ty = 0.75f; break;
  case AC_AV_SLEEP:
  case AC_AV_ERROR: tx = 0.0f; ty = 0.4f; break;
  }
  g_acAvatarLookX += (tx - g_acAvatarLookX) * 0.16f;
  g_acAvatarLookY += (ty - g_acAvatarLookY) * 0.16f;
  if (nearby) {
    if (g_acAvatarState == AC_AV_SLEEP) {
      ac_avatar_set(AC_AV_IDLE);
      ac_avatar_ambient(AC_AV_WAVE, now);
      if (g_acAvatarSkin == AC_SKIN_GIRL) ac_girl_shot(AGA_WAKE); /* проснулась — потягивается */
      ac_avatar_bubble(L"Ой! Я не спал, я думал", FALSE, 2400);
    }
    g_acAvatarActiveAt = now;
  }

  /* навели курсор — подпрыгивает навстречу и строит рожицу; держат дольше — смущается */
  BOOL over = !g_acHeld && WindowFromPoint(cur) == g_acAvatar;
  if (over && !g_acHover) {
    g_acHover = TRUE;
    g_acHoverAt = now;
    g_acHoverShy = FALSE;
    if (now - g_acHoverOff > 1500 && g_acAvatarState != AC_AV_SLEEP && !g_acEmo) {
      static const int kHi[] = {AC_EMO_SURPRISED, AC_EMO_LOVE, AC_EMO_WINK, AC_EMO_SHY};
      g_acHopAt = now | 1;
      ac_emo(kHi[ac_avatar_random() % 4], 1100);
    }
  } else if (!over && g_acHover) {
    g_acHover = FALSE;
    g_acHoverOff = now;
  }
  if (g_acHover && !g_acHoverShy && now - g_acHoverAt > 2600 && !g_acEmo) {
    g_acHoverShy = TRUE;
    ac_emo(AC_EMO_SHY, 1800);
    if (g_acAvatarSkin == AC_SKIN_GIRL) ac_girl_shot(AGA_PET); /* «гладят» — жмурится, прижимает ушки */
  }

  /* раскачивание: держат — тянет за курсором и отстаёт, отпустили — успокаивается */
  {
    float target = 0.0f;
    if (g_acHeld) {
      int vx = wr.left - g_acLastX;
      g_acLastX = wr.left;
      target = vx * 0.03f;
      if (target > 0.38f) target = 0.38f;
      if (target < -0.38f) target = -0.38f;
      if (now - g_acHeldAt < 700) { if (!g_acEmo) ac_emo(AC_EMO_SURPRISED, 700); }
      else if (fabsf(g_acSwingV) > 0.09f) ac_emo(AC_EMO_DIZZY, 900);
      else if (!g_acEmo) g_acAvatarJoyUntil = (now + 300) | 1;
    }
    g_acSwingV += (target - g_acSwing) * 0.16f;
    g_acSwingV *= 0.84f;
    g_acSwing += g_acSwingV;
  }

  if (g_acAvatarJoyUntil && ac_due(now, g_acAvatarJoyUntil)) g_acAvatarJoyUntil = 0;
  if (ac_due(now, g_acAvatarNextBlink)) {
    g_acAvatarBlinkAt = now | 1;
    g_acAvatarNextBlink = now + ((ac_avatar_random() % 5) ? 2300 + ac_avatar_random() % 4200 : 330);
  }

  BOOL girl = g_acAvatarSkin == AC_SKIN_GIRL; /* у неё радость и огорчение длиннее */
  if (g_acAvatarState == AC_AV_SUCCESS && now - g_acAvatarStateAt > (girl ? 2200u : 1300u)) {
    ac_avatar_set(AC_AV_IDLE);
  } else if (g_acAvatarState == AC_AV_ERROR && now - g_acAvatarStateAt > (girl ? 2800u : 1800u)) {
    ac_avatar_set(AC_AV_IDLE);
    ac_emo(AC_EMO_SAD, 1600); /* после ошибки ещё погрустит */
  } else if (g_acAvatarState == AC_AV_TALKING && ac_due(now, g_acAvatarTalkUntil)) {
    ac_avatar_set(AC_AV_IDLE);
  } else if (g_acAvatarAmbient && ac_due(now, g_acAvatarTransientUntil)) {
    g_acAvatarAmbient = FALSE;
    g_acAvatarTransientUntil = 0;
    g_acAvatarState = AC_AV_IDLE;
    g_acAvatarFrame = 0;
    g_acAvatarStateAt = now;
    ac_avatar_schedule_idle(now);
  }
  BOOL idle = g_acAvatarState == AC_AV_IDLE && !g_acAvatarAmbient && !g_acAvatarBusy;
  if (idle && now - g_acAvatarActiveAt > AC_AVATAR_SLEEP_AFTER) {
    g_acAvatarState = AC_AV_SLEEP;
    g_acAvatarFrame = 0;
    g_acAvatarStateAt = now;
    g_acAvatarMoving = FALSE;
    idle = FALSE;
  }
  if (idle && ac_due(now, g_acAvatarNextIdle)) ac_avatar_ambient((ac_avatar_random() & 1) ? AC_AV_WAVE : AC_AV_THINKING, now);
  /* мелочи в покое: прыжок, подмигнёт, улыбнётся, удивится */
  if (idle && !g_acHover && !g_acHeld && ac_due(now, g_acNextAct)) {
    if (g_acNextAct) {
      switch (ac_avatar_random() % 5) {
      case 0: g_acHopAt = now | 1; break;
      case 1: ac_emo(AC_EMO_WINK, 900); break;
      case 2: ac_emo(AC_EMO_LOVE, 1100); break;
      case 3: g_acHopAt = now | 1; g_acAvatarJoyUntil = (now + 900) | 1; break;
      default: ac_emo(AC_EMO_SURPRISED, 600); break;
      }
    }
    g_acNextAct = now + 9000 + ac_avatar_random() % 12000;
  }
  if (g_acLaughAt && ac_due(now, g_acLaughAt)) { /* анекдот досказан — смеётся */
    g_acLaughAt = 0;
    g_acHopAt = now | 1;
    g_acAvatarJoyUntil = (now + 1500) | 1;
    ac_emo((ac_avatar_random() & 1) ? AC_EMO_LOVE : AC_EMO_WINK, 1300);
  }
  if (g_acFacts && ac_due(now, g_acFactNext)) ac_fact_auto(now);
  if (g_acAvatarWindows) ac_avatar_windows(now, wr, idle);
  BOOL bubble = g_acBubble && IsWindowVisible(g_acBubble);
  if (idle && g_acAvatarWander && !g_acAvatarMoving && !bubble && !ac_mini_open() && !g_acPerch && !g_acFalling &&
      ac_due(now, g_acAvatarNextMove)) {
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromWindow(g_acAvatar, MONITOR_DEFAULTTONEAREST), &mi)) {
      int x = g_acAvatarHome.x - AC_AVATAR_ROAM + (int)(ac_avatar_random() % (2 * AC_AVATAR_ROAM + 1));
      int y = g_acAvatarHome.y - AC_AVATAR_ROAM / 3 + (int)(ac_avatar_random() % (2 * AC_AVATAR_ROAM / 3 + 1));
      if (x > mi.rcWork.right - S) x = mi.rcWork.right - S;
      if (y > mi.rcWork.bottom - S) y = mi.rcWork.bottom - S;
      if (x < mi.rcWork.left) x = mi.rcWork.left;
      if (y < mi.rcWork.top) y = mi.rcWork.top;
      g_acAvatarTarget.x = x;
      g_acAvatarTarget.y = y;
      g_acAvatarMoving = TRUE;
    }
  }
  if (g_acAvatarState == AC_AV_IDLE && g_acAvatarMoving) ac_avatar_move();

  ac_avatar_present();

  if (bubble) {
    if (!g_acBubbleFading && g_acBubbleUntil && ac_due(now, g_acBubbleUntil)) g_acBubbleFading = TRUE;
    if (g_acBubbleFading) {
      if (g_acBubbleAlpha <= 64) { ac_bubble_hide(TRUE); return; }
      g_acBubbleAlpha -= 64;
    } else if (g_acBubbleAlpha < 255) {
      g_acBubbleAlpha = g_acBubbleAlpha > 255 - 85 ? 255 : g_acBubbleAlpha + 85;
    }
    ac_bubble_update();
  }
}
