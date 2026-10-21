/* ai_avatar.c — настольный аватар мини-ИИ. Робот смоделирован в Blender
   (native/avatar/make_avatar.py), кадры состояний запечены в атлас
   ai_avatar_atlas.png и вшиты в EXE ресурсом 380. Глаза и рот на тёмном
   экране-лице программа рисует сама поверх кадра по точкам из ai_avatar_face.h:
   робот моргает, следит за курсором и меняет выражение. Пузырь рядом с ним
   показывает, чем ИИ занят фоном: строку состояния чата, шаг файловой задачи,
   начало ответа. Всё рисуется в окнах с попиксельной прозрачностью. */
#include "ai_avatar_face.h"
#define IDR_AI_AVATAR_ATLAS 380
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

static wchar_t g_acBubbleText[200];
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

static BOOL ac_avatar_load(void) {
  if (g_acAvatarAtlas) return TRUE;
  HRSRC resource = FindResourceW(g_inst, MAKEINTRESOURCEW(IDR_AI_AVATAR_ATLAS), RT_RCDATA);
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
      if (SUCCEEDED(converter->lpVtbl->GetSize(converter, &width, &height)) &&
          width == AC_AVATAR_FRAME * AC_AVATAR_COLS && height == AC_AVATAR_FRAME * AC_AVATAR_ROWS) {
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
          g_acAvatarAtlas = bitmap;
          g_acAvatarPixels = (BYTE *)pixels;
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
  g_acAvatarState = state;
  g_acAvatarFrame = 0;
  g_acAvatarStateAt = now;
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

/* билинейная выборка одного канала атласа: строки r0/r1, столбцы x0/x1, доли 0..65535 */
static unsigned ac_bilerp(size_t r0, size_t r1, int x0, int x1, int wx, int wy, int c) {
  const BYTE *p = g_acAvatarPixels;
  unsigned top = ((unsigned)p[(r0 + x0) * 4 + c] * (65536 - wx) + (unsigned)p[(r0 + x1) * 4 + c] * wx) >> 16;
  unsigned bottom = ((unsigned)p[(r1 + x0) * 4 + c] * (65536 - wx) + (unsigned)p[(r1 + x1) * 4 + c] * wx) >> 16;
  return (top * (65536 - wy) + bottom * wy) >> 16;
}

/* Глаза и рот поверх кадра: выражение по состоянию, моргание, взгляд за курсором. */
static void ac_avatar_face(DWORD now, float zoom) {
  int S = g_acAvatarSize, state = g_acAvatarState;
  const short *fa = kAcAvatarFace[state][g_acAvatarFrame], *fb = kAcAvatarFace[state][g_acAvatarNext];
  float f[6], m = g_acAvatarMix / 256.0f;
  for (int i = 0; i < 6; i++) f[i] = fa[i] + (fb[i] - fa[i]) * m;
  float k = (float)S / AC_AVATAR_FRAME / 8.0f, pvx = S * 0.5f, pvy = S * 0.78f;
  float lx = pvx + (f[0] * k - pvx) * zoom, ly = pvy + (f[1] * k - pvy) * zoom;
  float rx = pvx + (f[2] * k - pvx) * zoom, ry = pvy + (f[3] * k - pvy) * zoom;
  float mx = pvx + (f[4] * k - pvx) * zoom, my = pvy + (f[5] * k - pvy) * zoom;
  float ux = rx - lx, uy = ry - ly, d = sqrtf(ux * ux + uy * uy);
  if (d < 4.0f) return;
  AcFaceAxes ax = {ux / d, uy / d, -uy / d, ux / d};
  float lookA = g_acAvatarLookX * d * 0.17f, lookB = g_acAvatarLookY * d * 0.11f;
  COLORREF mood = kAcAvatarMood[state];
  COLORREF core = ac_mix(mood, RGB(255, 255, 255), 0.55f);
  float glowR = d * 0.10f + 0.6f, w = d * 0.27f, h = d * 0.50f, line = d * 0.065f + 0.45f;
  BOOL joy = g_acAvatarJoyUntil || state == AC_AV_SUCCESS || state == AC_AV_WAVE;
  float pts[8];
  for (int side = -1; side <= 1; side += 2) {
    float c0[2];
    ac_face_pt(&ax, side < 0 ? lx : rx, side < 0 ? ly : ry, lookA, lookB, c0);
    float cx = c0[0], cy = c0[1];
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
  if (state == AC_AV_TALKING) {
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
  float thick = n == 1 || state == AC_AV_TALKING ? mr : line * 0.8f;
  ac_paint_stroke(g_acAvatarFramePixels, S, S, pts, n, thick, mouth, mood, mg, state == AC_AV_SLEEP ? 0.6f : 0.85f);
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

  DWORD now = GetTickCount();
  BYTE alpha;
  float zoom = ac_avatar_zoom(now, &alpha);
  ac_avatar_frame(now);
  int S = g_acAvatarSize;
  float scale = (float)AC_AVATAR_FRAME / S, inv = 1.0f / zoom, pvx = S * 0.5f, pvy = S * 0.78f;
  int frameX = g_acAvatarFrame * AC_AVATAR_FRAME, nextX = g_acAvatarNext * AC_AVATAR_FRAME;
  unsigned mix = (unsigned)g_acAvatarMix;
  int frameY = g_acAvatarState * AC_AVATAR_FRAME;
  int atlasW = AC_AVATAR_FRAME * AC_AVATAR_COLS;
  for (int y = 0; y < S; y++) {
    /* точка кадра под пикселем окна; при призыве кадр масштабируется вокруг (pvx, pvy) */
    float sy = (pvy + (y + 0.5f - pvy) * inv) * scale - 0.5f;
    if (sy <= -1.0f || sy >= AC_AVATAR_FRAME) {
      memset(g_acAvatarFramePixels + (size_t)y * S * 4, 0, (size_t)S * 4);
      continue;
    }
    if (sy < 0.0f) sy = 0.0f;
    int y0 = (int)sy;
    int wy = (int)((sy - y0) * 65535.0f);
    if (y0 >= AC_AVATAR_FRAME - 1) { y0 = AC_AVATAR_FRAME - 1; wy = 0; }
    int y1 = y0 + (wy ? 1 : 0);
    for (int x = 0; x < S; x++) {
      float sx = (pvx + (x + 0.5f - pvx) * inv) * scale - 0.5f;
      if (sx <= -1.0f || sx >= AC_AVATAR_FRAME) {
        memset(g_acAvatarFramePixels + ((size_t)y * S + x) * 4, 0, 4);
        continue;
      }
      if (sx < 0.0f) sx = 0.0f;
      int x0 = (int)sx;
      int wx = (int)((sx - x0) * 65535.0f);
      if (x0 >= AC_AVATAR_FRAME - 1) { x0 = AC_AVATAR_FRAME - 1; wx = 0; }
      int x1 = x0 + (wx ? 1 : 0);
      size_t r0 = (size_t)(frameY + y0) * atlasW, r1 = (size_t)(frameY + y1) * atlasW;
      BYTE *dst = g_acAvatarFramePixels + (((size_t)y * S + x) * 4);
      for (int c = 0; c < 4; c++) {
        unsigned v = ac_bilerp(r0, r1, frameX + x0, frameX + x1, wx, wy, c);
        if (mix) v = (v * (256 - mix) + ac_bilerp(r0, r1, nextX + x0, nextX + x1, wx, wy, c) * mix) >> 8;
        dst[c] = (BYTE)v;
      }
    }
  }
  ac_avatar_face(now, zoom);

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
  RECT tr = {0, 0, maxW, 0};
  DrawTextW(dc, g_acBubbleText, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int textW = tr.right > maxW ? maxW : tr.right, textH = tr.bottom;
  if (textH > tm.tmHeight * 3) textH = tm.tmHeight * 3;
  BOOL dots = g_acAvatarBusy && g_acBubbleThought;
  int pad = fontPx * 3 / 4 + 2, dotsW = dots ? fontPx * 2 : 0;
  int bw = textW + pad * 2 + dotsW, bh = textH + pad * 2;
  int margin = 10, tail = fontPx + 4;
  int W = bw + margin * 2, H = bh + margin * 2 + tail;

  /* над головой; у верхнего края экрана — под аватаром */
  RECT ar;
  GetWindowRect(g_acAvatar, &ar);
  const short *f = kAcAvatarFace[g_acAvatarState][g_acAvatarFrame];
  float k = (float)S / AC_AVATAR_FRAME / 8.0f;
  float eyeD = sqrtf((float)(f[2] - f[0]) * (f[2] - f[0]) + (float)(f[3] - f[1]) * (f[3] - f[1])) * k;
  int headX = ar.left + (int)((f[0] + f[2]) * 0.5f * k);
  int headTop = ar.top + (int)((f[1] + f[3]) * 0.5f * k - eyeD * 1.5f);
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
        if (dist > 0.0f) ac_px_over(dst, RGB(0, 0, 0), 0.32f * expf(-dist / 3.5f));
        ac_px_over(dst, RGB(17, 21, 38), 0.94f * (0.5f - dist));
        float band = 1.3f - fabsf(dist + 0.9f);
        if (band > 0.0f)
          ac_px_over(dst, ac_mix(RGB(123, 92, 255), RGB(56, 225, 255), (fx - bx) / (bw > 1 ? bw : 1)), band * 0.95f);
      }
    }
    RECT tr2 = {(int)bx + pad, (int)by + pad, (int)bx + pad + textW, (int)by + pad + textH};
    ac_text(pix, W, H, dc, g_acBubbleText, -1, tr2, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS,
            RGB(236, 240, 255));
    /* ИИ ещё работает: три точки бегут волной */
    if (dots) {
      for (int i = 0; i < 3; i++) {
        float ph = 0.5f + 0.5f * sinf(now / 170.0f - i * 1.1f);
        float dot[2] = {bx + bw - pad - dotsW + (i + 0.5f) * dotsW / 3.0f, cy - ph * fontPx * 0.12f};
        ac_paint_stroke(pix, W, H, dot, 1, fontPx * (0.12f + 0.06f * ph),
                        ac_mix(RGB(123, 92, 255), RGB(56, 225, 255), i / 2.0f), RGB(56, 225, 255), 1.5f,
                        0.55f + 0.45f * ph);
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
  case WM_LBUTTONUP: ac_bubble_hide(FALSE); aichat_show(); return 0;
  case WM_RBUTTONUP: ac_bubble_hide(FALSE); return 0;
  case WM_PAINT: ValidateRect(hwnd, NULL); return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* Показать реплику рядом с аватаром. ms = 0 — держать, пока ИИ занят. */
static void ac_avatar_bubble(const wchar_t *text, BOOL thought, DWORD ms) {
  if (!g_acAvatarBubbles || !g_acAvatar || !IsWindowVisible(g_acAvatar) || g_acAvatarAnim == 2 || !text) return;
  if (ac_mini_open()) return; /* мини-чат открыт рядом — там то же самое */
  int n = 0;
  BOOL space = FALSE;
  for (const wchar_t *s = text; *s && n < 196; s++) {
    wchar_t c = *s;
    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    if (c == L'*' || c == L'#' || c == L'`') continue; /* разметка ответа */
    if (c == L' ' && (space || !n)) continue;
    space = c == L' ';
    g_acBubbleText[n++] = c;
  }
  while (n && g_acBubbleText[n - 1] == L' ') n--;
  if (n > 120) {
    n = 118;
    while (n > 80 && g_acBubbleText[n] != L' ') n--;
    g_acBubbleText[n++] = L'…';
  }
  g_acBubbleText[n] = 0;
  if (!n) return;
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

enum { ACM_HEAD, ACM_CHAT, ACM_SIZE, ACM_BUBBLES, ACM_WANDER, ACM_SEP, ACM_DISMISS, ACM_N };
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
static BOOL ac_menu_pick(int i) { return i == ACM_CHAT || i == ACM_SIZE || i == ACM_BUBBLES || i == ACM_WANDER || i == ACM_DISMISS; }

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
  static const int kRowH[ACM_N] = {56, 38, 62, 38, 38, 9, 38};
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
  ac_rrect(pix, W, H, x0, cy - h / 2, x1, cy + h / 2, h / 2, on ? RGB(56, 200, 255) : RGB(58, 64, 96), 1.0f);
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
    RECT nr = {row.right - ac_mpx(110), row.top, row.right - ac_mpx(12), row.top + height};
    SelectObject(dc, g_acMenuSmall);
    ac_text(pix, W, H, dc, note, -1, nr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, RGB(140, 150, 190));
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
  /* стекло: тень, тёмная заливка, кромка от фиолетового к бирюзовому */
  float m = (float)ac_mpx(14), r = (float)ac_mpx(14);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      float d = ac_rrect_sd(x + 0.5f, y + 0.5f, m, m, W - m, H - m, r);
      BYTE *dst = pix + ((size_t)y * W + x) * 4;
      if (d > 0.0f) ac_px_over(dst, RGB(0, 0, 0), 0.38f * expf(-d / (m * 0.4f)));
      ac_px_over(dst, RGB(17, 21, 38), 0.97f * (0.5f - d));
      float band = 1.3f - fabsf(d + 0.9f);
      if (band > 0.0f) ac_px_over(dst, ac_mix(RGB(123, 92, 255), RGB(56, 225, 255), (float)x / W), band * 0.9f);
    }
  }
  if (g_acMenuHot >= 0 && ac_menu_pick(g_acMenuHot)) {
    RECT hr = g_acMenuRow[g_acMenuHot];
    ac_rrect(pix, W, H, (float)hr.left, hr.top + 1.0f, (float)hr.right, hr.bottom - 1.0f, (float)ac_mpx(9),
             g_acMenuHot == ACM_DISMISS ? RGB(72, 32, 50) : RGB(36, 43, 74), 1.0f);
  }
  COLORREF ink = RGB(236, 240, 255), cyan = RGB(56, 225, 255);
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
  ac_text(pix, W, H, dc, ac_menu_state(), -1, st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, RGB(140, 150, 190));
  float rule[4] = {(float)hd.left + ac_mpx(8), hd.bottom - 0.5f, (float)hd.right - ac_mpx(8), hd.bottom - 0.5f};
  ac_paint_stroke(pix, W, H, rule, 2, 0.5f, RGB(46, 54, 88), RGB(46, 54, 88), 0.0f, 1.0f);
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
  ac_paint_stroke(pix, W, H, track, 2, (float)ac_mpx(2), RGB(58, 64, 96), RGB(58, 64, 96), 0.0f, 1.0f);
  ac_paint_stroke(pix, W, H, fill, 2, (float)ac_mpx(2), cyan, cyan, (float)ac_mpx(2), 1.0f);
  ac_paint_stroke(pix, W, H, knob, 1, (float)ac_mpx(7), RGB(255, 255, 255), cyan,
                  (float)ac_mpx(g_acMenuDrag ? 5 : 3), 1.0f);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_BUBBLES], 0xE8F2, L"Мысли ИИ рядом", NULL, ink, cyan, lineH);
  ac_menu_switch(pix, W, H, g_acMenuRow[ACM_BUBBLES], g_acAvatarBubbles);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_WANDER], 0xE805, L"Гулять рядом с местом", NULL, ink, cyan, lineH);
  ac_menu_switch(pix, W, H, g_acMenuRow[ACM_WANDER], g_acAvatarWander);
  RECT sp = g_acMenuRow[ACM_SEP];
  float sep[4] = {(float)sp.left + ac_mpx(8), (sp.top + sp.bottom) * 0.5f, (float)sp.right - ac_mpx(8),
                  (sp.top + sp.bottom) * 0.5f};
  ac_paint_stroke(pix, W, H, sep, 2, 0.5f, RGB(46, 54, 88), RGB(46, 54, 88), 0.0f, 1.0f);
  ac_menu_line(pix, W, H, dc, g_acMenuRow[ACM_DISMISS], 0xE711, L"Отозвать помощника", NULL, RGB(255, 128, 146),
               RGB(255, 107, 129), lineH);
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
  case ACM_BUBBLES:
    g_acAvatarBubbles = !g_acAvatarBubbles;
    if (!g_acAvatarBubbles) ac_bubble_hide(TRUE);
    ac_avatar_save_settings();
    ac_menu_render();
    break;
  case ACM_WANDER:
    g_acAvatarWander = !g_acAvatarWander;
    g_acAvatarMoving = FALSE;
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

/* щелчок без перетаскивания — робот радуется */
static void ac_avatar_poke(void) {
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
  if (g_acAvatarState == AC_AV_IDLE || g_acAvatarState == AC_AV_SLEEP) {
    ac_avatar_set(AC_AV_IDLE);
    g_acAvatarState = AC_AV_WAVE;
    g_acAvatarFrame = 0;
    g_acAvatarStateAt = now;
    g_acAvatarAmbient = TRUE;
    g_acAvatarTransientUntil = now + (DWORD)AC_AVATAR_COLS * kAcAvatarFrameMs[AC_AV_WAVE];
  }
  if (!g_acAvatarBusy)
    ac_avatar_bubble(kPhrases[ac_avatar_random() % (sizeof(kPhrases) / sizeof(kPhrases[0]))], FALSE, 2600);
  ac_avatar_present();
}

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
  case WM_LBUTTONDBLCLK: aichat_show(); return 0;
  case WM_ENTERSIZEMOVE: GetWindowRect(hwnd, &g_acAvatarDragFrom); g_acAvatarMoving = FALSE; return 0;
  case WM_EXITSIZEMOVE: {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    if (abs(rc.left - g_acAvatarDragFrom.left) + abs(rc.top - g_acAvatarDragFrom.top) < 4) ac_avatar_poke();
    else { g_acAvatarHome.x = rc.left; g_acAvatarHome.y = rc.top; }
    ac_avatar_save_settings();
    return 0;
  }
  case WM_MOVE:
    if (g_acBubble && IsWindowVisible(g_acBubble)) ac_bubble_update();
    if (!g_acAvatarFollowing) ac_mini_follow(); /* мини-чат едет следом */
    break;
  case WM_TIMER:
    if (wParam == AC_AVATAR_TIMER) { ac_avatar_tick(); return 0; }
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

static void ac_avatar_destroy(void) {
  if (g_acBubble) { DestroyWindow(g_acBubble); g_acBubble = NULL; }
  if (g_acBubbleBitmap) { DeleteObject(g_acBubbleBitmap); g_acBubbleBitmap = NULL; g_acBubblePixels = NULL; }
  if (g_acBubbleFont) { DeleteObject(g_acBubbleFont); g_acBubbleFont = NULL; }
  if (g_acAvatar) { KillTimer(g_acAvatar, AC_AVATAR_TIMER); DestroyWindow(g_acAvatar); g_acAvatar = NULL; }
  if (g_acAvatarFrameBitmap) { DeleteObject(g_acAvatarFrameBitmap); g_acAvatarFrameBitmap = NULL; }
  if (g_acAvatarAtlas) { DeleteObject(g_acAvatarAtlas); g_acAvatarAtlas = NULL; g_acAvatarPixels = NULL; }
}

static DWORD ac_avatar_random(void) {
  if (!g_acAvatarRng) g_acAvatarRng = GetTickCount() | 1;
  g_acAvatarRng = g_acAvatarRng * 1664525u + 1013904223u;
  return g_acAvatarRng >> 8;
}

static void ac_avatar_ambient(int state, DWORD now) {
  g_acAvatarState = state;
  g_acAvatarFrame = 0;
  g_acAvatarStateAt = now;
  g_acAvatarAmbient = TRUE;
  g_acAvatarTransientUntil = now + (DWORD)AC_AVATAR_COLS * kAcAvatarFrameMs[state] * (state == AC_AV_THINKING ? 2 : 1); /* целые петли */
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
      ac_avatar_bubble(L"Ой! Я не спал, я думал", FALSE, 2400);
    }
    g_acAvatarActiveAt = now;
  }

  if (g_acAvatarJoyUntil && ac_due(now, g_acAvatarJoyUntil)) g_acAvatarJoyUntil = 0;
  if (ac_due(now, g_acAvatarNextBlink)) {
    g_acAvatarBlinkAt = now | 1;
    g_acAvatarNextBlink = now + ((ac_avatar_random() % 5) ? 2300 + ac_avatar_random() % 4200 : 330);
  }

  if (g_acAvatarState == AC_AV_SUCCESS && now - g_acAvatarStateAt > 1300) {
    ac_avatar_set(AC_AV_IDLE);
  } else if (g_acAvatarState == AC_AV_ERROR && now - g_acAvatarStateAt > 1800) {
    ac_avatar_set(AC_AV_IDLE);
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
  BOOL bubble = g_acBubble && IsWindowVisible(g_acBubble);
  if (idle && g_acAvatarWander && !g_acAvatarMoving && !bubble && !ac_mini_open() && ac_due(now, g_acAvatarNextMove)) {
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
