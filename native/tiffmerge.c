/* ---- Окно «Объединение TIFF / PDF» -------------------------------------------

   Своё окно CursorPad вместо страницы tiff_merger.html: открывается сразу,
   без Edge, и не держит картинки в памяти. Вид — как у страницы: слева
   загрузка и быстрое преобразование, справа страницы документа с
   миниатюрами и кнопки «Сохранить как TIFF / PDF».

   Картинки читает WIC (есть в любой Windows: TIFF, PNG, JPEG, BMP, GIF,
   WebP — если стоит его кодек), пишет — tiff_codec.c. В отличие от
   страницы, которая всё переводила в цвет 72 dpi:
   · ч/б остаётся ч/б (сжатие G4, как у сканеров) — файл в разы меньше;
   · серое и цветное — без потерь (LZW), в PDF — JPEG 92 %;
   · у каждой страницы остаётся её dpi, лист PDF — размером с оригинал;
   · в списке — только путь и миниатюра, пиксели читаются при сохранении
     полосами: хоть сотня листов А0.

   Работа — в отдельном потоке: страницы появляются в списке по мере
   открытия, сохранение идёт с полосой хода и «Отменой». Файлы можно
   перетащить в окно из Проводника (и папку целиком), вставить Ctrl+V. */

#define COBJMACROS
#include <wincodec.h>
#include "tiff_codec.c"

#define WM_TM_PAGE (WM_APP + 40)     /* lParam — TmPage* от потока */
#define WM_TM_PROGRESS (WM_APP + 41) /* ход работы поменялся */
#define WM_TM_DONE (WM_APP + 42)     /* lParam — TmResult* */
#define TIMER_TM_AUTOSCROLL 1

/* WIC: GUID свои — чтобы не тянуть библиотеку uuid */
static const GUID kTmClsidFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
static const GUID kTmIidFactory = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
static const GUID kTmTiff = {0x163bcc30, 0xe2e9, 0x4f0b, {0x96, 0x1d, 0xa3, 0xe9, 0xfd, 0xb7, 0x88, 0xa3}};
static const GUID kTmJpeg = {0x19e4a5aa, 0x5662, 0x4fc5, {0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57}};
static const GUID kTmPng = {0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
/* форматы точек: {6fddc324-4e03-4bfe-b185-3d77768dc9XX} */
static GUID tm_fmt(int xx) {
  GUID g = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, (unsigned char)xx}};
  return g;
}
enum { TMF_IDX1 = 0x01, TMF_IDX2, TMF_IDX4, TMF_IDX8, TMF_BW, TMF_GRAY2, TMF_GRAY4, TMF_GRAY8, TMF_GRAY16 = 0x0b,
       TMF_BGR24 = 0x0c, TMF_BGRA32 = 0x0f };

/* цвета страницы (Tailwind slate / blue / emerald) */
#define TM_BG RGB(0xF8, 0xFA, 0xFC)
#define TM_WHITE RGB(0xFF, 0xFF, 0xFF)
#define TM_S100 RGB(0xF1, 0xF5, 0xF9)
#define TM_S200 RGB(0xE2, 0xE8, 0xF0)
#define TM_S300 RGB(0xCB, 0xD5, 0xE1)
#define TM_S400 RGB(0x94, 0xA3, 0xB8)
#define TM_S500 RGB(0x64, 0x74, 0x8B)
#define TM_S600 RGB(0x47, 0x55, 0x69)
#define TM_S700 RGB(0x33, 0x41, 0x55)
#define TM_S800 RGB(0x1E, 0x29, 0x3B)
#define TM_S900 RGB(0x0F, 0x17, 0x2A)
#define TM_B50 RGB(0xEF, 0xF6, 0xFF)
#define TM_B100 RGB(0xDB, 0xEA, 0xFE)
#define TM_B300 RGB(0x93, 0xC5, 0xFD)
#define TM_B400 RGB(0x60, 0xA5, 0xFA)
#define TM_B500 RGB(0x3B, 0x82, 0xF6)
#define TM_B600 RGB(0x25, 0x63, 0xEB)
#define TM_B700 RGB(0x1D, 0x4E, 0xD8)
#define TM_E50 RGB(0xEC, 0xFD, 0xF5)
#define TM_E100 RGB(0xD1, 0xFA, 0xE5)
#define TM_E200 RGB(0xA7, 0xF3, 0xD0)
#define TM_E500 RGB(0x10, 0xB9, 0x81)
#define TM_E600 RGB(0x05, 0x96, 0x69)
#define TM_E700 RGB(0x04, 0x78, 0x57)
#define TM_R50 RGB(0xFE, 0xF2, 0xF2)
#define TM_R500 RGB(0xEF, 0x44, 0x44)

/* ---- страницы ------------------------------------------------------------------ */

typedef struct {
  wchar_t path[MAX_PATH];
  wchar_t name[MAX_PATH + 24]; /* «файл.tif (стр. 2)» */
  UINT frame;
  int w, h;       /* уже с поворотом по EXIF */
  double dx, dy;  /* dpi */
  int kind;       /* TC_BW / TC_GRAY / TC_RGB */
  int thr;        /* ч/б: темнее этого — чёрная точка */
  int orient;     /* WICBitmapTransformOptions (фото с телефона), 0 — как есть */
  HBITMAP thumb;
  int tw, th;
} TmPage;

static TmPage *g_tmPages;
static int g_tmN, g_tmCap, g_tmSel = -1;

/* ---- работа в потоке: очередь заданий ------------------------------------------ */

enum { TJ_LOAD, TJ_TIFF, TJ_PDF, TJ_EACH_TIFF };

typedef struct TmJob {
  int type;
  wchar_t (*paths)[MAX_PATH]; /* LOAD, EACH_TIFF, а PDF/TIFF из «быстрого» — файлы */
  int npaths;
  TmPage *pages; /* TIFF / PDF из списка: снимок страниц (без миниатюр) */
  int npages;
  wchar_t out[MAX_PATH];
  struct TmJob *next;
} TmJob;

typedef struct {
  int type;
  BOOL ok, cancelled;
  int files, pages;
  ULONGLONG bytes;
  wchar_t out[MAX_PATH];
  wchar_t err[2000]; /* что не вышло — построчно */
} TmResult;

static HWND g_tmWnd, g_tmCanvas;
static CRITICAL_SECTION g_tmLock;
static TmJob *g_tmJobs;
static BOOL g_tmRunning;
static volatile LONG g_tmCancel;
static int g_tmLoadPending; /* заданий на открытие ещё не выполнено */
static BOOL g_tmExporting;  /* идёт сохранение — поверх окна полоса хода */
/* ход работы: пишет поток, читает окно */
static wchar_t g_tmProgTitle[80], g_tmProgText[MAX_PATH + 40];
static int g_tmProgPct; /* 0…1000 */
static DWORD g_tmProgTick;
/* строка под кнопками: что сохранили */
static wchar_t g_tmStatus[MAX_PATH + 80], g_tmStatusPath[MAX_PATH];
static BOOL g_tmStatusFolderOnly;

static void tm_progress(const wchar_t *title, const wchar_t *text, int pct) {
  EnterCriticalSection(&g_tmLock);
  if (title) lstrcpynW(g_tmProgTitle, title, 80);
  if (text) lstrcpynW(g_tmProgText, text, MAX_PATH + 40);
  if (pct >= 0) g_tmProgPct = pct > 1000 ? 1000 : pct;
  LeaveCriticalSection(&g_tmLock);
  DWORD t = GetTickCount();
  if (t - g_tmProgTick > 50 || pct >= 1000 || pct == 0) { /* не чаще 20 раз в секунду */
    g_tmProgTick = t;
    PostMessageW(g_tmWnd, WM_TM_PROGRESS, 0, 0);
  }
}

static void tm_err_add(TmResult *r, const wchar_t *what, const wchar_t *why) {
  size_t n = wcslen(r->err);
  if (n > 1700) return;
  _snwprintf(r->err + n, 2000 - n, L"%s%s — %s", n ? L"\n" : L"", what, why);
  r->err[1999] = 0;
}

static const wchar_t *tm_base(const wchar_t *p) {
  const wchar_t *s = wcsrchr(p, L'\\');
  const wchar_t *s2 = wcsrchr(p, L'/');
  if (s2 > s) s = s2;
  return s ? s + 1 : p;
}

/* HRESULT → понятная причина */
static void tm_why(HRESULT hr, wchar_t *out, int cap) {
  const wchar_t *t = NULL;
  switch ((unsigned)hr) {
  case 0x88982F50: t = L"не картинка, файл повреждён или Windows не знает этот формат"; break;
  case 0x88982F80:
  case 0x88982F81: t = L"Windows не смогла прочитать эту картинку"; break;
  case 0x88982F07:
  case 0x88982F61:
  case 0x88982F62:
  case 0x88982F60: t = L"это не картинка или файл повреждён"; break;
  case 0x80070002:
  case 0x80070003: t = L"файл не найден"; break;
  case 0x80070005: t = L"нет доступа к файлу"; break;
  case 0x80070020: t = L"файл занят другой программой"; break;
  case 0x8007000E: t = L"не хватило памяти"; break;
  case 0x80070070: t = L"на диске нет места"; break;
  case 0x80004004: t = L"отменено"; break;
  }
  if (t) lstrcpynW(out, t, cap);
  else _snwprintf(out, cap, L"ошибка 0x%08X", (unsigned)hr);
  out[cap - 1] = 0;
}

/* ---- WIC: открыть, узнать, прочитать ------------------------------------------ */

static HRESULT tm_open(IWICImagingFactory *f, const wchar_t *path, UINT frame, IWICBitmapDecoder **dec,
                       IWICBitmapFrameDecode **fr) {
  *dec = NULL;
  *fr = NULL;
  HRESULT hr = IWICImagingFactory_CreateDecoderFromFilename(f, path, NULL, GENERIC_READ,
                                                            WICDecodeMetadataCacheOnDemand, dec);
  if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(*dec, frame, fr);
  if (FAILED(hr) && *dec) {
    IWICBitmapDecoder_Release(*dec);
    *dec = NULL;
  }
  return hr;
}

/* кадр с поворотом по EXIF; повёрнутый — сразу в память (иначе WIC вертел бы
   его заново на каждую полосу) */
static HRESULT tm_source(IWICImagingFactory *f, IWICBitmapFrameDecode *fr, int orient, IWICBitmapSource **out) {
  *out = NULL;
  if (!orient) {
    IWICBitmapFrameDecode_AddRef(fr);
    *out = (IWICBitmapSource *)fr;
    return S_OK;
  }
  IWICBitmapFlipRotator *rot = NULL;
  HRESULT hr = IWICImagingFactory_CreateBitmapFlipRotator(f, &rot);
  if (SUCCEEDED(hr)) hr = IWICBitmapFlipRotator_Initialize(rot, (IWICBitmapSource *)fr, (WICBitmapTransformOptions)orient);
  IWICBitmap *bmp = NULL;
  if (SUCCEEDED(hr))
    hr = IWICImagingFactory_CreateBitmapFromSource(f, (IWICBitmapSource *)rot, WICBitmapCacheOnLoad, &bmp);
  if (rot) IWICBitmapFlipRotator_Release(rot);
  if (SUCCEEDED(hr)) *out = (IWICBitmapSource *)bmp;
  return hr;
}

static HRESULT tm_convert(IWICImagingFactory *f, IWICBitmapSource *src, int fmt, IWICBitmapSource **out) {
  *out = NULL;
  IWICFormatConverter *c = NULL;
  GUID g = tm_fmt(fmt);
  HRESULT hr = IWICImagingFactory_CreateFormatConverter(f, &c);
  if (SUCCEEDED(hr))
    hr = IWICFormatConverter_Initialize(c, src, &g, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    if (c) IWICFormatConverter_Release(c);
    return hr;
  }
  *out = (IWICBitmapSource *)c;
  return S_OK;
}

/* ч/б, серое или цветное — по формату точек и палитре */
static int tm_kind(IWICImagingFactory *f, IWICBitmapFrameDecode *fr, int *thr) {
  GUID g;
  *thr = 128;
  if (FAILED(IWICBitmapFrameDecode_GetPixelFormat(fr, &g))) return TC_RGB;
  GUID base = tm_fmt(0);
  if (memcmp(&g, &base, 15)) return TC_RGB; /* не из семейства {6fddc324…}: 48/64 бита, CMYK… */
  int xx = g.Data4[7];
  if (xx == TMF_BW) return TC_BW;
  if (xx == TMF_GRAY2 || xx == TMF_GRAY4 || xx == TMF_GRAY8 || xx == TMF_GRAY16) return TC_GRAY;
  if (xx < TMF_IDX1 || xx > TMF_IDX8) return TC_RGB;
  /* с палитрой: все цвета серые — серое; два цвета — ч/б (порог посередине) */
  int kind = TC_RGB;
  IWICPalette *pal = NULL;
  if (SUCCEEDED(IWICImagingFactory_CreatePalette(f, &pal)) && SUCCEEDED(IWICBitmapFrameDecode_CopyPalette(fr, pal))) {
    WICColor c[256];
    UINT n = 0;
    if (SUCCEEDED(IWICPalette_GetColors(pal, 256, c, &n)) && n > 0) {
      int gray = 1, lo = 255, hi = 0;
      for (UINT i = 0; i < n; i++) {
        int r = (c[i] >> 16) & 255, gg = (c[i] >> 8) & 255, b = c[i] & 255;
        if (r != gg || gg != b) gray = 0;
        if (r < lo) lo = r;
        if (r > hi) hi = r;
      }
      if (gray) {
        kind = xx == TMF_IDX1 && hi > lo ? TC_BW : TC_GRAY;
        if (kind == TC_BW) *thr = (lo + hi + 1) / 2;
      }
    }
  }
  if (pal) IWICPalette_Release(pal);
  return kind;
}

/* поворот фото по EXIF (1…8) → WICBitmapTransformOptions */
static int tm_orient(IWICBitmapDecoder *dec, IWICBitmapFrameDecode *fr) {
  GUID cf;
  if (FAILED(IWICBitmapDecoder_GetContainerFormat(dec, &cf)) || !IsEqualGUID(&cf, &kTmJpeg)) return 0;
  IWICMetadataQueryReader *q = NULL;
  int o = 1;
  if (SUCCEEDED(IWICBitmapFrameDecode_GetMetadataQueryReader(fr, &q))) {
    PROPVARIANT v;
    memset(&v, 0, sizeof(v));
    if (SUCCEEDED(IWICMetadataQueryReader_GetMetadataByName(q, L"/app1/ifd/{ushort=274}", &v)) && v.vt == VT_UI2)
      o = v.uiVal;
    PropVariantClear(&v);
    IWICMetadataQueryReader_Release(q);
  }
  static const int map[9] = {0, 0, 8, 2, 16, 1 | 8, 1, 3 | 8, 3};
  return o >= 1 && o <= 8 ? map[o] : 0;
}

/* Миниатюра: вписанная в 176 точек, фон под прозрачным — белый. src — кадр
   как лежит в файле; w, h — размер уже с поворотом: сначала уменьшаем, потом
   поворачиваем маленькую (фото с телефона не разворачивается в памяти целиком). */
static HBITMAP tm_thumb(IWICImagingFactory *f, IWICBitmapSource *src, int orient, int w, int h, int *tw, int *th) {
  const int M = 176;
  double s = (double)M / (w > h ? w : h);
  if (s > 1) s = 1;
  int a = (int)(w * s + 0.5), b = (int)(h * s + 0.5);
  if (a < 1) a = 1;
  if (b < 1) b = 1;
  IWICBitmapSource *cv = NULL, *out = NULL;
  IWICBitmapScaler *sc = NULL;
  IWICBitmapFlipRotator *rot = NULL;
  HBITMAP hb = NULL;
  UINT sa = orient & 1 ? (UINT)b : (UINT)a, sb = orient & 1 ? (UINT)a : (UINT)b; /* до поворота */
  BYTE *buf = (BYTE *)malloc((size_t)a * b * 4);
  /* Fant — усреднение: тонкие линии чертежа не пропадают; нет его — попроще */
  static const WICBitmapInterpolationMode modes[3] = {WICBitmapInterpolationModeFant, WICBitmapInterpolationModeLinear,
                                                      WICBitmapInterpolationModeNearestNeighbor};
  BOOL ok = buf && SUCCEEDED(tm_convert(f, src, TMF_BGRA32, &cv));
  for (int k = 0; ok && k < 3; k++) {
    if (sc) IWICBitmapScaler_Release(sc);
    sc = NULL;
    ok = SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(f, &sc));
    if (ok && SUCCEEDED(IWICBitmapScaler_Initialize(sc, cv, sa, sb, modes[k]))) {
      out = (IWICBitmapSource *)sc;
      if (orient) {
        if (rot) IWICBitmapFlipRotator_Release(rot);
        rot = NULL;
        if (SUCCEEDED(IWICImagingFactory_CreateBitmapFlipRotator(f, &rot)) &&
            SUCCEEDED(IWICBitmapFlipRotator_Initialize(rot, out, (WICBitmapTransformOptions)orient)))
          out = (IWICBitmapSource *)rot;
      }
      if (SUCCEEDED(IWICBitmapSource_CopyPixels(out, NULL, (UINT)a * 4, (UINT)(a * b * 4), buf))) break;
    }
    if (k == 2) ok = FALSE;
  }
  if (ok) {
    for (int i = 0; i < a * b; i++) {
      BYTE *p = buf + i * 4;
      int al = p[3];
      if (al < 255)
        for (int k = 0; k < 3; k++) p[k] = (BYTE)((p[k] * al + 255 * (255 - al) + 127) / 255);
      p[3] = 255;
    }
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = a;
    bi.bmiHeader.biHeight = -b;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    hb = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hb && bits) memcpy(bits, buf, (size_t)a * b * 4);
    *tw = a;
    *th = b;
  }
  if (rot) IWICBitmapFlipRotator_Release(rot);
  if (sc) IWICBitmapScaler_Release(sc);
  if (cv) IWICBitmapSource_Release(cv);
  free(buf);
  return hb;
}

/* Все страницы файла: у TIFF — каждая, у остальных — первая (у GIF прочие
   кадры — анимация, не страницы). thumbs — делать ли миниатюры. */
typedef void (*TmPageCb)(void *ctx, TmPage *pg);

static HRESULT tm_probe(IWICImagingFactory *f, const wchar_t *path, BOOL thumbs, TmPageCb cb, void *ctx) {
  IWICBitmapDecoder *dec = NULL;
  HRESULT hr = IWICImagingFactory_CreateDecoderFromFilename(f, path, NULL, GENERIC_READ,
                                                            WICDecodeMetadataCacheOnDemand, &dec);
  if (FAILED(hr)) return hr;
  UINT nf = 1;
  GUID cf;
  if (SUCCEEDED(IWICBitmapDecoder_GetContainerFormat(dec, &cf)) && IsEqualGUID(&cf, &kTmTiff))
    if (FAILED(IWICBitmapDecoder_GetFrameCount(dec, &nf)) || nf < 1) nf = 1;
  for (UINT i = 0; i < nf && !g_tmCancel; i++) {
    IWICBitmapFrameDecode *fr = NULL;
    hr = IWICBitmapDecoder_GetFrame(dec, i, &fr);
    if (FAILED(hr)) break;
    TmPage *pg = (TmPage *)calloc(1, sizeof(TmPage));
    if (!pg) {
      IWICBitmapFrameDecode_Release(fr);
      hr = E_OUTOFMEMORY;
      break;
    }
    lstrcpynW(pg->path, path, MAX_PATH);
    if (nf > 1) _snwprintf(pg->name, MAX_PATH + 24, L"%s (стр. %u)", tm_base(path), i + 1);
    else lstrcpynW(pg->name, tm_base(path), MAX_PATH + 24);
    pg->name[MAX_PATH + 23] = 0;
    pg->frame = i;
    pg->kind = tm_kind(f, fr, &pg->thr);
    pg->orient = tm_orient(dec, fr);
    if (FAILED(IWICBitmapFrameDecode_GetResolution(fr, &pg->dx, &pg->dy)) || !(pg->dx >= 10 && pg->dx < 100000))
      pg->dx = 96;
    if (!(pg->dy >= 10 && pg->dy < 100000)) pg->dy = pg->dx;
    if (pg->orient & 1) { /* повёрнуто на 90° — разрешения меняются местами */
      double t = pg->dx;
      pg->dx = pg->dy;
      pg->dy = t;
    }
    UINT w = 0, h = 0;
    hr = IWICBitmapFrameDecode_GetSize(fr, &w, &h);
    if (SUCCEEDED(hr) && (w < 1 || h < 1 || w > 200000 || h > 200000)) hr = 0x88982F60;
    if (SUCCEEDED(hr)) {
      pg->w = (int)(pg->orient & 1 ? h : w); /* повёрнуто на 90° — ширина с высотой меняются */
      pg->h = (int)(pg->orient & 1 ? w : h);
      if (thumbs) pg->thumb = tm_thumb(f, (IWICBitmapSource *)fr, pg->orient, pg->w, pg->h, &pg->tw, &pg->th);
    }
    IWICBitmapFrameDecode_Release(fr);
    if (FAILED(hr)) {
      free(pg);
      break;
    }
    cb(ctx, pg);
  }
  IWICBitmapDecoder_Release(dec);
  return g_tmCancel ? E_ABORT : hr;
}

/* Прочитать страницу полосами и отдать строки: в TcPage (TIFF, ч/б для PDF)
   или в кодировщик JPEG (серое и цветное для PDF). Серое и ч/б WIC отдаёт
   сразу байтом на точку; не умеет (бывает у редких форматов) — читаем в
   цвете и переводим в серое сами. */
static HRESULT tm_pump(IWICImagingFactory *f, const TmPage *pg, TcPage *tp, IWICBitmapFrameEncode *fe, int pi,
                       int pn) {
  IWICBitmapDecoder *dec = NULL;
  IWICBitmapFrameDecode *fr = NULL;
  IWICBitmapSource *src = NULL, *cv = NULL;
  BYTE *buf = NULL, *row = NULL, *out = NULL, *pk = NULL;
  HRESULT hr = tm_open(f, pg->path, pg->frame, &dec, &fr);
  if (SUCCEEDED(hr)) hr = tm_source(f, fr, pg->orient, &src);
  UINT w = 0, h = 0;
  if (SUCCEEDED(hr)) hr = IWICBitmapSource_GetSize(src, &w, &h);
  if (SUCCEEDED(hr) && ((int)w != pg->w || (int)h != pg->h)) hr = 0x88982F60; /* файл подменили после открытия */
  int rgb = pg->kind == TC_RGB;
  int via = rgb ? TMF_BGRA32 : TMF_GRAY8;
  if (SUCCEEDED(hr)) {
    hr = tm_convert(f, src, via, &cv);
    if (FAILED(hr) && !rgb) {
      via = TMF_BGRA32;
      hr = tm_convert(f, src, via, &cv);
    }
  }
  UINT band = w ? (8u << 20) / (w * 4) : 1;
  if (band < 1) band = 1;
  if (band > h) band = h;
  size_t ow = (size_t)w * (rgb ? 3 : 1); /* строка на выход: RGB / BGR или серое */
  if (SUCCEEDED(hr)) {
    buf = (BYTE *)malloc((size_t)w * 4 * band);
    row = (BYTE *)malloc(ow + 8);
    pk = (BYTE *)malloc((size_t)(w + 7) / 8 + 1); /* ч/б строка: бит на точку */
    if (fe) out = (BYTE *)malloc(ow * band);
    if (!buf || !row || !pk || (fe && !out)) hr = E_OUTOFMEMORY;
  }
  for (UINT y = 0; SUCCEEDED(hr) && y < h; y += band) {
    if (g_tmCancel) {
      hr = E_ABORT;
      break;
    }
    UINT rows = h - y < band ? h - y : band;
    UINT stride = w * (via == TMF_BGRA32 ? 4 : 1);
    WICRect rc = {0, (INT)y, (INT)w, (INT)rows};
    hr = IWICBitmapSource_CopyPixels(cv, &rc, stride, stride * rows, buf);
    if (FAILED(hr) && y == 0 && via == TMF_GRAY8) { /* серое не вышло — через цвет */
      IWICBitmapSource_Release(cv);
      cv = NULL;
      via = TMF_BGRA32;
      hr = tm_convert(f, src, via, &cv);
      stride = w * 4;
      if (SUCCEEDED(hr)) hr = IWICBitmapSource_CopyPixels(cv, &rc, stride, stride * rows, buf);
    }
    if (FAILED(hr)) break;
    for (UINT r = 0; r < rows; r++) {
      const BYTE *s = buf + (size_t)stride * r;
      BYTE *d = fe ? out + ow * r : row;
      if (via == TMF_BGRA32) {
        /* прозрачное — на белом, как на бумаге */
        for (UINT x = 0; x < w; x++) {
          const BYTE *p = s + x * 4;
          int a = p[3], b = p[0], g = p[1], rr = p[2];
          if (a < 255) {
            b = (b * a + 255 * (255 - a) + 127) / 255;
            g = (g * a + 255 * (255 - a) + 127) / 255;
            rr = (rr * a + 255 * (255 - a) + 127) / 255;
          }
          if (!rgb) {
            d[x] = (BYTE)((rr * 299 + g * 587 + b * 114 + 500) / 1000);
          } else if (fe) { /* JPEG от WIC — B, G, R */
            d[x * 3] = (BYTE)b;
            d[x * 3 + 1] = (BYTE)g;
            d[x * 3 + 2] = (BYTE)rr;
          } else { /* TIFF — R, G, B */
            d[x * 3] = (BYTE)rr;
            d[x * 3 + 1] = (BYTE)g;
            d[x * 3 + 2] = (BYTE)b;
          }
        }
        s = d;
      } else if (fe) {
        memcpy(d, s, w);
      }
      /* s — строка серого (байт на точку) или цвета */
      if (pg->kind == TC_BW) {
        memset(pk, 0, (w + 7) / 8);
        for (UINT x = 0; x < w; x++)
          if (s[x] < pg->thr) pk[x >> 3] |= (BYTE)(0x80 >> (x & 7));
        tc_page_row(tp, pk);
      } else if (tp) {
        tc_page_row(tp, s);
      }
    }
    if (SUCCEEDED(hr) && fe) hr = IWICBitmapFrameEncode_WritePixels(fe, rows, (UINT)ow, (UINT)(ow * rows), out);
    if (tp && tp->err) hr = E_OUTOFMEMORY;
    tm_progress(NULL, NULL, (int)((pi + (double)(y + rows) / h) * 1000 / pn));
  }
  free(buf);
  free(row);
  free(out);
  free(pk);
  if (cv) IWICBitmapSource_Release(cv);
  if (src) IWICBitmapSource_Release(src);
  if (fr) IWICBitmapFrameDecode_Release(fr);
  if (dec) IWICBitmapDecoder_Release(dec);
  return hr;
}

/* серая или цветная страница → JPEG 92 % в памяти (для PDF) */
static HRESULT tm_jpeg(IWICImagingFactory *f, const TmPage *pg, int pi, int pn, BYTE **out, size_t *n) {
  *out = NULL;
  *n = 0;
  IStream *st = NULL;
  IWICBitmapEncoder *enc = NULL;
  IWICBitmapFrameEncode *fe = NULL;
  IPropertyBag2 *bag = NULL;
  HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &st);
  if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(f, &kTmJpeg, NULL, &enc);
  if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(enc, st, WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(enc, &fe, &bag);
  if (SUCCEEDED(hr) && bag) {
    PROPBAG2 opt;
    memset(&opt, 0, sizeof(opt));
    opt.pstrName = (LPOLESTR)L"ImageQuality";
    VARIANT v;
    memset(&v, 0, sizeof(v));
    v.vt = VT_R4;
    v.fltVal = 0.92f;
    bag->lpVtbl->Write(bag, 1, &opt, &v);
  }
  if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(fe, bag);
  if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetSize(fe, (UINT)pg->w, (UINT)pg->h);
  if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetResolution(fe, pg->dx, pg->dy);
  GUID want = tm_fmt(pg->kind == TC_GRAY ? TMF_GRAY8 : TMF_BGR24), got = want;
  if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetPixelFormat(fe, &got);
  if (SUCCEEDED(hr) && !IsEqualGUID(&got, &want)) hr = 0x88982F50;
  if (SUCCEEDED(hr)) hr = tm_pump(f, pg, NULL, fe, pi, pn);
  if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(fe);
  if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(enc);
  if (SUCCEEDED(hr)) {
    HGLOBAL hg = NULL;
    LARGE_INTEGER zero;
    ULARGE_INTEGER end;
    zero.QuadPart = 0;
    hr = st->lpVtbl->Seek(st, zero, STREAM_SEEK_END, &end);
    if (SUCCEEDED(hr)) hr = GetHGlobalFromStream(st, &hg);
    if (SUCCEEDED(hr)) {
      void *p = GlobalLock(hg);
      *out = p ? (BYTE *)malloc((size_t)end.QuadPart + 1) : NULL;
      if (*out) {
        memcpy(*out, p, (size_t)end.QuadPart);
        *n = (size_t)end.QuadPart;
      } else {
        hr = E_OUTOFMEMORY;
      }
      if (p) GlobalUnlock(hg);
    }
  }
  if (bag) bag->lpVtbl->Release(bag);
  if (fe) IWICBitmapFrameEncode_Release(fe);
  if (enc) IWICBitmapEncoder_Release(enc);
  if (st) st->lpVtbl->Release(st);
  return hr;
}

/* ---- запись в файл ------------------------------------------------------------- */

static int tm_fwrite(void *ctx, const void *d, size_t n) {
  while (n) {
    DWORD part = n > (1u << 30) ? (1u << 30) : (DWORD)n, wr = 0;
    if (!WriteFile((HANDLE)ctx, d, part, &wr, NULL) || wr != part) return 0;
    d = (const char *)d + part;
    n -= part;
  }
  return 1;
}

static int tm_fseek(void *ctx, uint64_t pos) {
  LARGE_INTEGER li;
  li.QuadPart = (LONGLONG)pos;
  return SetFilePointerEx((HANDLE)ctx, li, NULL, FILE_BEGIN) != 0;
}

/* Страницы → один TIFF или PDF. Пишется во временный файл рядом, и только
   целиком готовый встаёт на место: отмена или сбой не оставят полфайла,
   а сохранить поверх одного из исходников можно без вреда. */
static BOOL tm_write_doc(IWICImagingFactory *f, const TmPage *pages, int n, BOOL pdf, const wchar_t *out,
                         TmResult *r) {
  wchar_t tmp[MAX_PATH + 8];
  _snwprintf(tmp, MAX_PATH + 8, L"%s.part", out);
  tmp[MAX_PATH + 7] = 0;
  HANDLE h = CreateFileW(tmp, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    wchar_t why[120];
    tm_why(HRESULT_FROM_WIN32(GetLastError()), why, 120);
    tm_err_add(r, tm_base(out), why);
    return FALSE;
  }
  TcOut o = {tm_fwrite, tm_fseek, h, 0, 0};
  TcTiff tt = {0};
  TcPdf pd = {0};
  if (pdf) tc_pdf_begin(&pd, &o);
  else tc_tiff_begin(&tt, &o, n);
  BOOL ok = TRUE;
  for (int i = 0; i < n && ok; i++) {
    const TmPage *pg = &pages[i];
    wchar_t t[MAX_PATH + 40];
    _snwprintf(t, MAX_PATH + 40, L"Страница %d из %d · %s", i + 1, n, pg->name);
    t[MAX_PATH + 39] = 0;
    tm_progress(NULL, t, i * 1000 / n);
    HRESULT hr;
    if (!pdf || pg->kind == TC_BW) {
      TcPage tp;
      if (!tc_page_begin(&tp, pg->kind, pg->w, pg->h, pg->dx, pg->dy)) hr = E_OUTOFMEMORY;
      else hr = tm_pump(f, pg, &tp, NULL, i, n);
      if (SUCCEEDED(hr) && !tc_page_end(&tp)) hr = E_OUTOFMEMORY;
      if (SUCCEEDED(hr)) {
        if (pdf) tc_pdf_page(&pd, TC_BW, pg->w, pg->h, pg->dx, pg->dy, tp.data.p, tp.data.n);
        else tc_tiff_page(&tt, &tp);
        if (o.err) hr = o.pos > 0xFFFFFF00u && !pdf ? E_OUTOFMEMORY : HRESULT_FROM_WIN32(ERROR_DISK_FULL);
      }
      tc_page_free(&tp);
    } else {
      BYTE *j = NULL;
      size_t jn = 0;
      hr = tm_jpeg(f, pg, i, n, &j, &jn);
      if (SUCCEEDED(hr)) tc_pdf_page(&pd, pg->kind, pg->w, pg->h, pg->dx, pg->dy, j, jn);
      if (SUCCEEDED(hr) && o.err) hr = HRESULT_FROM_WIN32(ERROR_DISK_FULL);
      free(j);
    }
    if (FAILED(hr)) {
      ok = FALSE;
      if (hr == E_ABORT) {
        r->cancelled = TRUE;
      } else {
        wchar_t why[120], what[MAX_PATH + 40];
        tm_why(hr, why, 120);
        if (!pdf && o.pos > 0xFFFFFF00u) lstrcpynW(why, L"TIFF больше 4 ГБ не бывает — сохраните в PDF или частями", 120);
        _snwprintf(what, MAX_PATH + 40, L"Стр. %d (%s)", i + 1, pg->name);
        what[MAX_PATH + 39] = 0;
        tm_err_add(r, what, why);
      }
    }
  }
  if (ok) {
    if (pdf) tc_pdf_end(&pd);
    if (o.err) {
      ok = FALSE;
      tm_err_add(r, tm_base(out), L"не удалось записать (нет места?)");
    }
  } else if (pdf) {
    free(pd.xref);
    free(pd.pages);
  }
  r->bytes = o.pos;
  CloseHandle(h);
  if (ok && !MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
    ok = FALSE;
    wchar_t why[120];
    tm_why(HRESULT_FROM_WIN32(GetLastError()), why, 120);
    tm_err_add(r, tm_base(out), why);
  }
  if (!ok) DeleteFileW(tmp);
  return ok;
}

/* ---- задания ------------------------------------------------------------------- */

typedef struct {
  TmPage *p;
  int n, cap;
} TmList;

static void tm_list_cb(void *ctx, TmPage *pg) {
  TmList *l = (TmList *)ctx;
  if (l->n >= l->cap) {
    int c = l->cap ? l->cap * 2 : 16;
    TmPage *q = (TmPage *)realloc(l->p, sizeof(TmPage) * (size_t)c);
    if (!q) {
      free(pg);
      return;
    }
    l->p = q;
    l->cap = c;
  }
  l->p[l->n++] = *pg;
  free(pg);
}

static void tm_post_page(void *ctx, TmPage *pg) {
  (void)ctx;
  if (!PostMessageW(g_tmWnd, WM_TM_PAGE, 0, (LPARAM)pg)) {
    if (pg->thumb) DeleteObject(pg->thumb);
    free(pg);
  }
}

/* файлы → страницы (для «быстрых» и открытия) с объяснением неудач */
static void tm_probe_all(IWICImagingFactory *f, wchar_t (*paths)[MAX_PATH], int n, BOOL thumbs, TmPageCb cb,
                         void *ctx, TmResult *r, const wchar_t *title) {
  for (int i = 0; i < n && !g_tmCancel; i++) {
    wchar_t t[MAX_PATH + 40];
    _snwprintf(t, MAX_PATH + 40, L"%s (%d из %d)", tm_base(paths[i]), i + 1, n);
    t[MAX_PATH + 39] = 0;
    tm_progress(title, t, thumbs ? i * 1000 / n : -1);
    HRESULT hr = tm_probe(f, paths[i], thumbs, cb, ctx);
    if (FAILED(hr) && hr != E_ABORT) {
      wchar_t why[120];
      tm_why(hr, why, 120);
      tm_err_add(r, tm_base(paths[i]), why);
    } else if (SUCCEEDED(hr)) {
      r->files++;
    }
  }
}

/* имя для «В TIFF (отдельные файлы)»: рядом с исходником, ничего не затирая */
static void tm_free_name(const wchar_t *src, wchar_t *out) {
  wchar_t base[MAX_PATH];
  lstrcpynW(base, src, MAX_PATH);
  wchar_t *dot = wcsrchr(base, L'.');
  if (dot && dot > wcsrchr(base, L'\\')) *dot = 0;
  _snwprintf(out, MAX_PATH, L"%s.tif", base);
  out[MAX_PATH - 1] = 0;
  for (int k = 2; k < 1000 && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES; k++) {
    _snwprintf(out, MAX_PATH, L"%s (%d).tif", base, k);
    out[MAX_PATH - 1] = 0;
  }
}

static void tm_run(IWICImagingFactory *f, TmJob *j, TmResult *r) {
  switch (j->type) {
  case TJ_LOAD:
    tm_probe_all(f, j->paths, j->npaths, TRUE, tm_post_page, NULL, r, L"Открываю");
    r->ok = TRUE;
    break;
  case TJ_TIFF:
  case TJ_PDF: {
    BOOL pdf = j->type == TJ_PDF;
    tm_progress(pdf ? L"Создание PDF" : L"Создание TIFF", L"Подготовка…", 0);
    TmList l = {0};
    const TmPage *pages = j->pages;
    int n = j->npages;
    if (j->paths) { /* «быстрое»: страницы ещё не открыты */
      tm_probe_all(f, j->paths, j->npaths, FALSE, tm_list_cb, &l, r, NULL);
      pages = l.p;
      n = l.n;
    }
    if (g_tmCancel) r->cancelled = TRUE;
    else if (n > 0 && !r->err[0]) r->ok = tm_write_doc(f, pages, n, pdf, j->out, r);
    r->pages = n;
    lstrcpynW(r->out, j->out, MAX_PATH);
    free(l.p);
    break;
  }
  case TJ_EACH_TIFF:
    tm_progress(L"Конвертация в TIFF", L"", 0);
    for (int i = 0; i < j->npaths && !g_tmCancel; i++) {
      TmList l = {0};
      HRESULT hr = tm_probe(f, j->paths[i], FALSE, tm_list_cb, &l);
      wchar_t out[MAX_PATH];
      if (SUCCEEDED(hr) && l.n) {
        tm_free_name(j->paths[i], out);
        wchar_t t[MAX_PATH + 40];
        _snwprintf(t, MAX_PATH + 40, L"%s (%d из %d)", tm_base(j->paths[i]), i + 1, j->npaths);
        t[MAX_PATH + 39] = 0;
        tm_progress(NULL, t, i * 1000 / j->npaths);
        if (tm_write_doc(f, l.p, l.n, FALSE, out, r)) {
          r->files++;
          r->pages += l.n;
          if (!r->out[0]) lstrcpynW(r->out, out, MAX_PATH);
        }
      } else if (hr != E_ABORT) {
        wchar_t why[120];
        tm_why(hr, why, 120);
        tm_err_add(r, tm_base(j->paths[i]), why);
      }
      free(l.p);
    }
    if (g_tmCancel) r->cancelled = TRUE;
    r->ok = r->files > 0;
    break;
  }
}

static DWORD WINAPI tm_worker(LPVOID arg) {
  (void)arg;
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  IWICImagingFactory *f = NULL;
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  for (;;) {
    EnterCriticalSection(&g_tmLock);
    TmJob *j = g_tmJobs;
    if (j) g_tmJobs = j->next;
    else g_tmRunning = FALSE;
    LeaveCriticalSection(&g_tmLock);
    if (!j) break;
    TmResult *r = (TmResult *)calloc(1, sizeof(TmResult));
    if (r) {
      r->type = j->type;
      if (f) tm_run(f, j, r);
      else tm_err_add(r, L"Windows", L"нет компонента WIC для картинок");
      if (!PostMessageW(g_tmWnd, WM_TM_DONE, 0, (LPARAM)r)) free(r);
    }
    free(j->paths);
    free(j->pages);
    free(j);
  }
  if (f) IWICImagingFactory_Release(f);
  CoUninitialize();
  return 0;
}

static void tm_submit(TmJob *j) {
  j->next = NULL;
  BOOL start = FALSE;
  EnterCriticalSection(&g_tmLock);
  TmJob **pp = &g_tmJobs;
  while (*pp) pp = &(*pp)->next;
  *pp = j;
  if (!g_tmRunning) {
    g_tmRunning = TRUE;
    start = TRUE;
  }
  LeaveCriticalSection(&g_tmLock);
  if (start) {
    HANDLE t = CreateThread(NULL, 0, tm_worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else g_tmRunning = FALSE;
  }
}

/* ---- списки файлов ------------------------------------------------------------- */

typedef struct {
  wchar_t (*p)[MAX_PATH];
  int n, cap;
} TmPaths;

static void tm_paths_add(TmPaths *l, const wchar_t *path) {
  if (l->n >= l->cap) {
    int c = l->cap ? l->cap * 2 : 32;
    wchar_t(*q)[MAX_PATH] = realloc(l->p, sizeof(*q) * (size_t)c);
    if (!q) return;
    l->p = q;
    l->cap = c;
  }
  lstrcpynW(l->p[l->n++], path, MAX_PATH);
}

static BOOL tm_is_image_name(const wchar_t *n) {
  const wchar_t *d = wcsrchr(n, L'.');
  if (!d) return FALSE;
  static const wchar_t *const ext[] = {L".tif", L".tiff", L".png", L".jpg", L".jpeg", L".jpe", L".jfif",
                                       L".bmp", L".dib",  L".gif", L".webp"};
  for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++)
    if (!lstrcmpiW(d, ext[i])) return TRUE;
  return FALSE;
}

/* папка — все картинки в ней (без подпапок) */
static void tm_paths_add_any(TmPaths *l, const wchar_t *path) {
  DWORD a = GetFileAttributesW(path);
  if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
    tm_paths_add(l, path);
    return;
  }
  wchar_t mask[MAX_PATH];
  _snwprintf(mask, MAX_PATH, L"%s\\*", path);
  mask[MAX_PATH - 1] = 0;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(mask, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || !tm_is_image_name(fd.cFileName)) continue;
    wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%s\\%s", path, fd.cFileName);
    p[MAX_PATH - 1] = 0;
    tm_paths_add(l, p);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

/* по имени, числа — как числа: «лист 2» раньше «лист 10» */
static int tm_path_cmp(const void *a, const void *b) {
  int r = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, (const wchar_t *)a, -1,
                         (const wchar_t *)b, -1);
  if (!r) r = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, (const wchar_t *)a, -1, (const wchar_t *)b, -1);
  return r - 2;
}

static void tm_paths_sort(TmPaths *l) {
  if (l->n > 1) qsort(l->p, (size_t)l->n, sizeof(*l->p), tm_path_cmp);
}

static TmJob *tm_job_paths(int type, TmPaths *l) {
  TmJob *j = (TmJob *)calloc(1, sizeof(TmJob));
  if (!j) {
    free(l->p);
    return NULL;
  }
  j->type = type;
  j->paths = l->p;
  j->npaths = l->n;
  l->p = NULL;
  l->n = l->cap = 0;
  return j;
}

static void tm_relayout(void);

static void tm_load_paths(TmPaths *l) {
  if (!l->n || g_tmExporting) { /* пока сохраняется — не добавляем: окно под полосой хода */
    free(l->p);
    return;
  }
  tm_paths_sort(l);
  TmJob *j = tm_job_paths(TJ_LOAD, l);
  if (!j) return;
  g_tmLoadPending++;
  InterlockedExchange(&g_tmCancel, 0);
  tm_submit(j);
  tm_relayout();
}

/* окно выбора файлов (несколько сразу) */
static BOOL tm_pick(TmPaths *l, const wchar_t *title) {
  const int CAP = 65536;
  wchar_t *buf = (wchar_t *)calloc(CAP, sizeof(wchar_t));
  if (!buf) return FALSE;
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_tmWnd;
  of.lpstrFilter = L"Изображения (TIFF, PNG, JPG, BMP, GIF, WebP)\0*.tif;*.tiff;*.png;*.jpg;*.jpeg;*.jpe;*.jfif;*.bmp;*.dib;*.gif;*.webp\0"
                   L"Все файлы\0*.*\0";
  of.lpstrFile = buf;
  of.nMaxFile = CAP;
  of.lpstrTitle = title;
  of.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  BOOL ok = GetOpenFileNameW(&of);
  if (ok) {
    const wchar_t *first = buf, *p = buf + wcslen(buf) + 1;
    if (!*p) {
      tm_paths_add(l, first); /* один файл — полный путь */
    } else {
      for (; *p; p += wcslen(p) + 1) { /* папка, потом имена */
        wchar_t full[MAX_PATH];
        _snwprintf(full, MAX_PATH, L"%s\\%s", first, p);
        full[MAX_PATH - 1] = 0;
        tm_paths_add(l, full);
      }
    }
  }
  free(buf);
  return ok && l->n > 0;
}

static BOOL tm_save_as(wchar_t *file, BOOL pdf, const wchar_t *dirFrom) {
  wchar_t dir[MAX_PATH] = L"";
  if (dirFrom) {
    lstrcpynW(dir, dirFrom, MAX_PATH);
    wchar_t *s = wcsrchr(dir, L'\\');
    if (s) *s = 0;
  }
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_tmWnd;
  of.lpstrFilter = pdf ? L"PDF (*.pdf)\0*.pdf\0" : L"TIFF (*.tif)\0*.tif;*.tiff\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrInitialDir = dir[0] ? dir : NULL;
  of.lpstrDefExt = pdf ? L"pdf" : L"tif";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  return GetSaveFileNameW(&of);
}

/* ---- действия ------------------------------------------------------------------ */

static void tm_status(const wchar_t *text, const wchar_t *path, BOOL folderOnly) {
  lstrcpynW(g_tmStatus, text, MAX_PATH + 80);
  lstrcpynW(g_tmStatusPath, path ? path : L"", MAX_PATH);
  g_tmStatusFolderOnly = folderOnly;
}

static void tm_add_files(void) {
  TmPaths l = {0};
  if (tm_pick(&l, L"Добавить страницы")) tm_load_paths(&l);
  else free(l.p);
}

static void tm_start_export(TmJob *j, BOOL pdf) {
  g_tmExporting = TRUE;
  InterlockedExchange(&g_tmCancel, 0);
  lstrcpynW(g_tmProgTitle, pdf ? L"Создание PDF" : L"Создание TIFF", 80);
  g_tmProgText[0] = 0;
  g_tmProgPct = 0;
  tm_submit(j);
  tm_relayout();
}

static void tm_export(BOOL pdf) {
  if (!g_tmN || g_tmExporting || g_tmLoadPending) return;
  wchar_t file[MAX_PATH], base[MAX_PATH];
  lstrcpynW(base, tm_base(g_tmPages[0].path), MAX_PATH);
  wchar_t *dot = wcsrchr(base, L'.');
  if (dot) *dot = 0;
  _snwprintf(file, MAX_PATH, L"%s_объединено", base);
  file[MAX_PATH - 1] = 0;
  if (!tm_save_as(file, pdf, g_tmPages[0].path)) return;
  TmJob *j = (TmJob *)calloc(1, sizeof(TmJob));
  TmPage *snap = (TmPage *)malloc(sizeof(TmPage) * (size_t)g_tmN);
  if (!j || !snap) {
    free(j);
    free(snap);
    return;
  }
  memcpy(snap, g_tmPages, sizeof(TmPage) * (size_t)g_tmN);
  for (int i = 0; i < g_tmN; i++) snap[i].thumb = NULL; /* миниатюры остаются окну */
  j->type = pdf ? TJ_PDF : TJ_TIFF;
  j->pages = snap;
  j->npages = g_tmN;
  lstrcpynW(j->out, file, MAX_PATH);
  tm_start_export(j, pdf);
}

/* «Быстрое преобразование» — мимо списка страниц, как на странице */
static void tm_quick(BOOL pdf) {
  if (g_tmExporting || g_tmLoadPending) return;
  TmPaths l = {0};
  if (!tm_pick(&l, pdf ? L"Файлы для PDF" : L"Файлы для перевода в TIFF")) {
    free(l.p);
    return;
  }
  tm_paths_sort(&l);
  wchar_t file[MAX_PATH] = L"";
  if (pdf) {
    wchar_t base[MAX_PATH];
    lstrcpynW(base, tm_base(l.p[0]), MAX_PATH);
    wchar_t *dot = wcsrchr(base, L'.');
    if (dot) *dot = 0;
    lstrcpynW(file, base, MAX_PATH);
    if (!tm_save_as(file, TRUE, l.p[0])) {
      free(l.p);
      return;
    }
  }
  TmJob *j = tm_job_paths(pdf ? TJ_PDF : TJ_EACH_TIFF, &l);
  if (!j) return;
  lstrcpynW(j->out, file, MAX_PATH);
  tm_start_export(j, pdf);
  if (!pdf) lstrcpynW(g_tmProgTitle, L"Конвертация в TIFF", 80);
}

static void tm_remove(int i) {
  if (i < 0 || i >= g_tmN) return;
  if (g_tmPages[i].thumb) DeleteObject(g_tmPages[i].thumb);
  memmove(&g_tmPages[i], &g_tmPages[i + 1], sizeof(TmPage) * (size_t)(g_tmN - i - 1));
  g_tmN--;
  if (g_tmSel == i) g_tmSel = g_tmN ? (i < g_tmN ? i : g_tmN - 1) : -1;
  else if (g_tmSel > i) g_tmSel--;
  tm_relayout();
}

static void tm_move(int from, int to) { /* to — место в списке после перестановки */
  if (from < 0 || from >= g_tmN || to < 0 || to >= g_tmN || from == to) return;
  TmPage t = g_tmPages[from];
  if (from < to) memmove(&g_tmPages[from], &g_tmPages[from + 1], sizeof(TmPage) * (size_t)(to - from));
  else memmove(&g_tmPages[to + 1], &g_tmPages[to], sizeof(TmPage) * (size_t)(from - to));
  g_tmPages[to] = t;
  g_tmSel = to;
  tm_relayout();
}

static void tm_clear(void) {
  if (!g_tmN) return;
  if (MessageBoxW(g_tmWnd, L"Очистить все страницы?", L"Объединение TIFF / PDF", MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;
  for (int i = 0; i < g_tmN; i++)
    if (g_tmPages[i].thumb) DeleteObject(g_tmPages[i].thumb);
  g_tmN = 0;
  g_tmSel = -1;
  tm_relayout();
}

static void tm_open_file(const wchar_t *path) {
  if (path && path[0]) ShellExecuteW(g_tmWnd, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

static void tm_show_in_folder(const wchar_t *path) {
  if (!path || !path[0]) return;
  wchar_t args[MAX_PATH + 16];
  _snwprintf(args, MAX_PATH + 16, L"/select,\"%s\"", path);
  args[MAX_PATH + 15] = 0;
  ShellExecuteW(g_tmWnd, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
}

/* Ctrl+V: файлы из Проводника или картинка (снимок экрана) */
static void tm_paste(void) {
  if (!OpenClipboard(g_tmWnd)) return;
  TmPaths l = {0};
  HANDLE hd = GetClipboardData(CF_HDROP);
  if (hd) {
    UINT n = DragQueryFileW((HDROP)hd, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < n; i++) {
      wchar_t p[MAX_PATH];
      if (DragQueryFileW((HDROP)hd, i, p, MAX_PATH)) tm_paths_add_any(&l, p);
    }
  }
  HBITMAP hb = !l.n ? (HBITMAP)GetClipboardData(CF_BITMAP) : NULL;
  wchar_t png[MAX_PATH] = L"";
  if (hb && g_dataDir[0]) { /* картинку — в PNG рядом с настройками, дальше как файл */
    wchar_t dir[MAX_PATH];
    _snwprintf(dir, MAX_PATH, L"%s\\вставки", g_dataDir);
    dir[MAX_PATH - 1] = 0;
    CreateDirectoryW(dir, NULL);
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf(png, MAX_PATH, L"%s\\Вставка %02d-%02d-%02d.png", dir, st.wHour, st.wMinute, st.wSecond);
    png[MAX_PATH - 1] = 0;
    IWICImagingFactory *f = NULL;
    IWICBitmap *bmp = NULL;
    IWICStream *s = NULL;
    IWICBitmapEncoder *enc = NULL;
    IWICBitmapFrameEncode *fe = NULL;
    HRESULT hr = CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateBitmapFromHBITMAP(f, hb, NULL, WICBitmapIgnoreAlpha, &bmp);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateStream(f, &s);
    if (SUCCEEDED(hr)) hr = IWICStream_InitializeFromFilename(s, png, GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(f, &kTmPng, NULL, &enc);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(enc, (IStream *)s, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(enc, &fe, NULL);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(fe, NULL);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WriteSource(fe, (IWICBitmapSource *)bmp, NULL);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(fe);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(enc);
    if (fe) IWICBitmapFrameEncode_Release(fe);
    if (enc) IWICBitmapEncoder_Release(enc);
    if (s) IWICStream_Release(s);
    if (bmp) IWICBitmap_Release(bmp);
    if (f) IWICImagingFactory_Release(f);
    if (SUCCEEDED(hr)) tm_paths_add(&l, png);
  }
  CloseClipboard();
  if (l.n) tm_load_paths(&l);
  else free(l.p);
}

/* ---- рисование ----------------------------------------------------------------- */

static float g_tmS = 1.0f, g_tmDpi = 1.0f, g_tmZoom = 1.0f;
static BOOL g_tmTwo = TRUE;
static HFONT g_tf[14];
enum { TF_H1, TF_SUB, TF_SEC, TF_SMALL, TF_SMALLM, TF_TINY, TF_BODYM, TF_BTN, TF_BTNSUB, TF_NUM, TF_ICON, TF_ICONL,
       TF_ICONS, TF_BADGE };
static BOOL g_tmIcons; /* есть шрифт значков Windows 10/11 */
static const wchar_t *g_tmIconFace = L"Segoe MDL2 Assets";

static int g_tmScroll, g_tmContentH;       /* прокрутка всего окна */
static int g_tmListScroll, g_tmListH;      /* прокрутка списка страниц */
static RECT g_tmListRc;                    /* где список (в координатах полотна) */
static int g_tmCardH, g_tmCardGap, g_tmListPad;
static int g_tmHoverId, g_tmHoverArg = -1; /* под мышью */
static int g_tmDragIdx = -1, g_tmDragIns = -1, g_tmDragY0, g_tmThumbGrab = -1;
static BOOL g_tmDragging;
static RECT g_tmCancelRc; /* «Отмена» на полосе хода — в координатах окна */

enum { H_NONE, H_ADD, H_CLEAR, H_QTIFF, H_QPDF, H_UP, H_DOWN, H_CARD, H_EYE, H_DEL, H_TIFF, H_PDF, H_OPEN_OUT,
       H_SHOW_OUT, H_LTHUMB };
typedef struct {
  RECT r;
  int id, arg;
} TmHot;
static TmHot g_tmHot[96];
static int g_tmNHot;
static BOOL g_tmPainting; /* рисуем, а не раскладываем: места для щелчков не трогаем */

static int TS(int v) { return (int)(v * g_tmS + 0.5f); }

static int CALLBACK tm_font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp) {
  (void)lf;
  (void)tm;
  (void)type;
  *(BOOL *)lp = TRUE;
  return 0;
}

static BOOL tm_have_font(const wchar_t *face) {
  LOGFONTW lf;
  memset(&lf, 0, sizeof(lf));
  lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
  BOOL found = FALSE;
  HDC dc = GetDC(NULL);
  EnumFontFamiliesExW(dc, &lf, tm_font_found, (LPARAM)&found, 0);
  ReleaseDC(NULL, dc);
  return found;
}

static void tm_fonts(void) {
  for (int i = 0; i < 14; i++)
    if (g_tf[i]) DeleteObject(g_tf[i]);
  static const struct {
    int px, w, icon;
  } f[14] = {{26, FW_SEMIBOLD, 0}, {13, FW_NORMAL, 0}, {15, FW_SEMIBOLD, 0}, {12, FW_NORMAL, 0}, {12, FW_SEMIBOLD, 0},
             {11, FW_NORMAL, 0},   {14, FW_SEMIBOLD, 0}, {14, FW_BOLD, 0},   {11, FW_NORMAL, 0}, {10, FW_BOLD, 0},
             {16, FW_NORMAL, 1},   {22, FW_NORMAL, 1},   {12, FW_NORMAL, 1}, {12, FW_SEMIBOLD, 0}};
  for (int i = 0; i < 14; i++)
    g_tf[i] = CreateFontW(-TS(f[i].px), 0, 0, 0, f[i].w, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, f[i].icon ? g_tmIconFace : L"Segoe UI");
}

static int tm_text(HDC dc, int font, COLORREF c, const wchar_t *s, RECT r, UINT fl) {
  SelectObject(dc, g_tf[font]);
  SetTextColor(dc, c);
  return DrawTextW(dc, s, -1, &r, fl | DT_NOPREFIX);
}

static int tm_text_w(HDC dc, int font, const wchar_t *s) {
  SelectObject(dc, g_tf[font]);
  SIZE sz;
  GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
  return sz.cx;
}

static int tm_text_h(HDC dc, int font, const wchar_t *s, int w) {
  SelectObject(dc, g_tf[font]);
  RECT r = {0, 0, w, 10000};
  DrawTextW(dc, s, -1, &r, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
  return r.bottom;
}

/* смесь цветов: t = 0 — a, 1 — b */
static COLORREF tm_mix(COLORREF a, COLORREF b, float t) {
  return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5f),
             (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + 0.5f),
             (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5f));
}

static void tm_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int w) {
  HPEN p = CreatePen(PS_SOLID, w, c);
  HGDIOBJ op = SelectObject(dc, p);
  MoveToEx(dc, x1, y1, NULL);
  LineTo(dc, x2, y2);
  SelectObject(dc, op);
  DeleteObject(p);
}

/* значки: из шрифта Windows (Segoe MDL2 / Fluent Icons), а стрелки, крестик и
   глаз — линиями, чтобы были и без шрифта */
enum { G_UP, G_DOWN, G_X, G_EYE, G_TRASH, G_FOLDER, G_UPLOAD, G_BOLT, G_PHOTO, G_DOC, G_INFO, G_LAYERS, G_SHIELD,
       G_GEAR };
static void tm_glyph(HDC dc, int g, RECT r, COLORREF c, int font) {
  int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, s = TS(font == TF_ICONL ? 9 : font == TF_ICONS ? 4 : 6);
  int pw = TS(2) > 1 ? TS(2) : 1;
  switch (g) {
  case G_UP:
  case G_DOWN: {
    int d = g == G_UP ? -1 : 1;
    tm_line(dc, cx, cy - d * s, cx, cy + d * s, c, pw);
    tm_line(dc, cx, cy + d * s, cx - s * 2 / 3, cy + d * s / 3, c, pw);
    tm_line(dc, cx, cy + d * s, cx + s * 2 / 3, cy + d * s / 3, c, pw);
    return;
  }
  case G_X:
    tm_line(dc, cx - s * 2 / 3, cy - s * 2 / 3, cx + s * 2 / 3 + 1, cy + s * 2 / 3 + 1, c, pw);
    tm_line(dc, cx - s * 2 / 3, cy + s * 2 / 3, cx + s * 2 / 3 + 1, cy - s * 2 / 3 - 1, c, pw);
    return;
  case G_EYE:
    if (!g_tmIcons) {
      HPEN p = CreatePen(PS_SOLID, pw, c);
      HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
      Ellipse(dc, cx - s - 1, cy - s * 2 / 3, cx + s + 2, cy + s * 2 / 3 + 1);
      SelectObject(dc, op);
      SelectObject(dc, ob);
      DeleteObject(p);
      HBRUSH b = CreateSolidBrush(c);
      ob = SelectObject(dc, b);
      op = SelectObject(dc, GetStockObject(NULL_PEN));
      Ellipse(dc, cx - s / 3 - 1, cy - s / 3 - 1, cx + s / 3 + 2, cy + s / 3 + 2);
      SelectObject(dc, ob);
      SelectObject(dc, op);
      DeleteObject(b);
      return;
    }
    break;
  case G_UPLOAD:
    if (!g_tmIcons) {
      tm_line(dc, cx, cy + s * 2 / 3, cx, cy - s, c, pw);
      tm_line(dc, cx, cy - s, cx - s * 2 / 3, cy - s / 3, c, pw);
      tm_line(dc, cx, cy - s, cx + s * 2 / 3, cy - s / 3, c, pw);
      tm_line(dc, cx - s, cy + s, cx + s + 1, cy + s, c, pw);
      return;
    }
    break;
  }
  if (!g_tmIcons) return;
  static const wchar_t kGl[] = {0, 0, 0, 0xE890, 0xE74D, 0xE838, 0xE898, 0xE945, 0xE91B, 0xE8A5, 0xE946, 0xE81E, 0xEA18,
                                0xE713};
  wchar_t t[2] = {kGl[g], 0};
  tm_text(dc, font, c, t, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void tm_hot(RECT r, int id, int arg) {
  if (!g_tmPainting && g_tmNHot < 96) {
    g_tmHot[g_tmNHot].r = r;
    g_tmHot[g_tmNHot].id = id;
    g_tmHot[g_tmNHot].arg = arg;
    g_tmNHot++;
  }
}

static BOOL tm_is_hover(int id, int arg) { return g_tmHoverId == id && (arg < 0 || g_tmHoverArg == arg); }

/* «страница / страницы / страниц» */
static const wchar_t *tm_pages_word(int n) {
  int a = n % 100, b = n % 10;
  if (a >= 11 && a <= 14) return L"страниц";
  if (b == 1) return L"страница";
  if (b >= 2 && b <= 4) return L"страницы";
  return L"страниц";
}

/* формат листа по размеру в мм: А4, А3, А4×3… (с допуском, в любой ориентации) */
static const wchar_t *tm_paper(const TmPage *pg) {
  double a = pg->w * 25.4 / pg->dx, b = pg->h * 25.4 / pg->dy;
  if (a > b) {
    double t = a;
    a = b;
    b = t;
  }
  static const struct {
    int s, l;
    const wchar_t *n;
  } k[] = {{841, 1189, L"A0"},   {594, 841, L"A1"},    {420, 594, L"A2"},    {297, 420, L"A3"},    {210, 297, L"A4"},
           {148, 210, L"A5"},    {297, 630, L"A4×3"}, {297, 841, L"A4×4"}, {297, 1051, L"A4×5"}, {420, 891, L"A3×3"},
           {420, 1189, L"A3×4"}, {594, 1261, L"A2×3"}};
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
    double ts = k[i].s * 0.03 > 4 ? k[i].s * 0.03 : 4, tl = k[i].l * 0.03 > 4 ? k[i].l * 0.03 : 4;
    if (a > k[i].s - ts && a < k[i].s + ts && b > k[i].l - tl && b < k[i].l + tl) return k[i].n;
  }
  return NULL;
}

static void tm_page_info(const TmPage *pg, wchar_t *out, int cap) {
  const wchar_t *paper = pg->dx != 96 || pg->dy != 96 ? tm_paper(pg) : NULL; /* 96 — dpi не указан */
  const wchar_t *kind = pg->kind == TC_BW ? L"ч/б" : pg->kind == TC_GRAY ? L"серое" : L"цвет";
  wchar_t dpi[24];
  if ((int)(pg->dx + 0.5) == (int)(pg->dy + 0.5)) _snwprintf(dpi, 24, L"%d dpi", (int)(pg->dx + 0.5));
  else _snwprintf(dpi, 24, L"%d×%d dpi", (int)(pg->dx + 0.5), (int)(pg->dy + 0.5));
  dpi[23] = 0;
  _snwprintf(out, cap, L"%d × %d · %s%s%s · %s", pg->w, pg->h, dpi, paper ? L" · " : L"", paper ? paper : L"", kind);
  out[cap - 1] = 0;
}

/* карточка с белым фоном и рамкой; ring — рамка в 2 точки (выбранная) */
static void tm_card(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border, BOOL ring) {
  if (ring) {
    cc_round(dc, r, rad, border, border);
    RECT in = r;
    InflateRect(&in, -2, -2);
    cc_round(dc, in, rad > 2 ? rad - 2 : rad, fill, fill);
  } else {
    cc_round(dc, r, rad, fill, border);
  }
}

/* кнопка-значок 32×32 */
static void tm_icon_btn(HDC dc, RECT r, int g, int id, int arg, COLORREF hoverBg, COLORREF hoverFg, BOOL enabled,
                        BOOL paint) {
  if (paint) {
    BOOL hov = enabled && tm_is_hover(id, arg);
    if (hov) cc_round(dc, r, TS(12), hoverBg, hoverBg);
    tm_glyph(dc, g, r, !enabled ? TM_S300 : hov ? hoverFg : TM_S400, TF_ICON);
  }
  if (enabled) tm_hot(r, id, arg);
}

/* левая карточка: загрузка и быстрое преобразование; возвращает низ */
static int tm_left(HDC dc, int x, int y, int w, BOOL paint) {
  int pad = TS(24), ix = x + pad, iw = w - pad * 2;
  const wchar_t *tips = L"Перетащите карточку страницы, чтобы поменять порядок · Del — убрать страницу · "
                        L"Ctrl+V — вставить картинку или скопированные файлы";
  /* узкое окно (одна колонка) — место для файлов в одну строку и без
     подсказок, чтобы страницы были видны сразу, без прокрутки */
  BOOL compact = !g_tmTwo;
  int tipsH = compact ? 0 : tm_text_h(dc, TF_TINY, tips, iw) + TS(16);
  BOOL side = iw >= TS(300);
  int dzH = compact ? TS(84) : TS(214), qh = TS(38);
  int h = pad + TS(44) + TS(16) + dzH + TS(16) + 1 + TS(16) + TS(26) + (side ? qh : qh * 2 + TS(8)) + tipsH + pad;
  if (!paint) {
    int cy = y + pad + TS(44) + TS(16);
    tm_hot((RECT){ix, cy, ix + iw, cy + dzH}, H_ADD, -1);
    BOOL qen = !g_tmLoadPending;
    int cw = tm_text_w(dc, TF_SMALLM, L"Очистить всё") + TS(g_tmIcons ? 40 : 24);
    tm_hot((RECT){ix + iw - cw, y + pad, ix + iw, y + pad + TS(30)}, H_CLEAR, -1);
    cy += dzH + TS(16) + 1 + TS(16) + TS(26);
    if (!qen) {
    } else if (side) {
      int bw = (iw - TS(8)) / 2;
      tm_hot((RECT){ix, cy, ix + bw, cy + qh}, H_QTIFF, -1);
      tm_hot((RECT){ix + iw - bw, cy, ix + iw, cy + qh}, H_QPDF, -1);
    } else {
      tm_hot((RECT){ix, cy, ix + iw, cy + qh}, H_QTIFF, -1);
      tm_hot((RECT){ix, cy + qh + TS(8), ix + iw, cy + qh * 2 + TS(8)}, H_QPDF, -1);
    }
    return y + h;
  }
  cc_round(dc, (RECT){x, y, x + w, y + h}, TS(24), TM_WHITE, TM_S200);
  int cy = y + pad;
  /* заголовок */
  int tx = ix;
  if (g_tmIcons) {
    tm_glyph(dc, G_UPLOAD, (RECT){ix, cy, ix + TS(18), cy + TS(20)}, TM_B500, TF_ICONS);
    tx += TS(24);
  }
  tm_text(dc, TF_SEC, TM_S800, L"Загрузка файлов", (RECT){tx, cy, ix + iw - TS(120), cy + TS(20)},
          DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  tm_text(dc, TF_SMALL, TM_S500, L"PNG, JPG, WebP, GIF, BMP, TIFF", (RECT){ix, cy + TS(24), ix + iw - TS(120), cy + TS(42)},
          DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  /* «Очистить всё» */
  {
    BOOL hov = tm_is_hover(H_CLEAR, -1) && g_tmN;
    int cw = tm_text_w(dc, TF_SMALLM, L"Очистить всё") + TS(g_tmIcons ? 40 : 24);
    RECT b = {ix + iw - cw, cy, ix + iw, cy + TS(30)};
    if (hov) cc_round(dc, b, TS(12), TM_R50, TM_R50);
    COLORREF c = !g_tmN ? TM_S300 : hov ? TM_R500 : TM_S500;
    RECT t = b;
    if (g_tmIcons) {
      tm_glyph(dc, G_TRASH, (RECT){b.left + TS(10), b.top, b.left + TS(24), b.bottom}, c, TF_ICONS);
      t.left += TS(28);
    } else {
      t.left += TS(12);
    }
    tm_text(dc, TF_SMALLM, c, L"Очистить всё", t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  cy += TS(44) + TS(16);
  /* место для файлов: пунктирная рамка */
  {
    BOOL hov = tm_is_hover(H_ADD, -1);
    RECT dz = {ix, cy, ix + iw, cy + dzH};
    LOGBRUSH lb = {BS_SOLID, hov ? TM_B400 : TM_S300, 0};
    DWORD dash[2] = {(DWORD)TS(6), (DWORD)TS(4)};
    HPEN p = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT, TS(2) > 1 ? TS(2) : 1, &lb, 2, dash);
    HBRUSH br = CreateSolidBrush(hov ? RGB(0xF3, 0xF7, 0xFE) : TM_BG);
    HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, br);
    RoundRect(dc, dz.left + 1, dz.top + 1, dz.right - 1, dz.bottom - 1, TS(32), TS(32));
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(p);
    DeleteObject(br);
    int bw = tm_text_w(dc, TF_SMALLM, L"Выбрать файлы") + TS(g_tmIcons ? 52 : 32);
    if (compact) { /* значок, надписи, кнопка — в строку */
      RECT ib = {ix + TS(18), cy + (dzH - TS(48)) / 2, ix + TS(66), cy + (dzH + TS(48)) / 2};
      cc_round(dc, ib, TS(16), TM_WHITE, TM_S200);
      tm_glyph(dc, G_UPLOAD, ib, TM_S400, TF_ICONL);
      BOOL btn = iw >= TS(440);
      int tr = btn ? ix + iw - bw - TS(34) : ix + iw - TS(16);
      tm_text(dc, TF_BODYM, TM_S700, L"Перетащите файлы сюда", (RECT){ib.right + TS(16), cy + TS(22), tr, cy + TS(42)},
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
      tm_text(dc, TF_TINY, TM_S400, L"или щёлкните · многостраничные TIFF тоже", (RECT){ib.right + TS(16), cy + TS(46), tr, cy + TS(62)},
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
      if (btn) {
        RECT b = {ix + iw - TS(18) - bw, cy + (dzH - TS(36)) / 2, ix + iw - TS(18), cy + (dzH + TS(36)) / 2};
        cc_round(dc, b, TS(12), hov ? TM_BG : TM_WHITE, TM_S200);
        RECT t = b;
        if (g_tmIcons) {
          tm_glyph(dc, G_FOLDER, (RECT){b.left + TS(14), b.top, b.left + TS(30), b.bottom}, TM_S700, TF_ICONS);
          t.left += TS(38);
        } else {
          t.left += TS(16);
        }
        tm_text(dc, TF_SMALLM, TM_S700, L"Выбрать файлы", t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      }
      goto dz_done;
    }
    int mx = (dz.left + dz.right) / 2, yy = cy + TS(28);
    RECT ib = {mx - TS(24), yy, mx + TS(24), yy + TS(48)};
    cc_round(dc, ib, TS(16), TM_WHITE, TM_S200);
    tm_glyph(dc, G_UPLOAD, ib, TM_S400, TF_ICONL);
    yy += TS(58);
    tm_text(dc, TF_BODYM, TM_S700, L"Перетащите файлы сюда", (RECT){ix, yy, ix + iw, yy + TS(20)},
            DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    yy += TS(22);
    tm_text(dc, TF_SMALL, TM_S500, L"или", (RECT){ix, yy, ix + iw, yy + TS(16)}, DT_CENTER | DT_SINGLELINE);
    yy += TS(24);
    RECT b = {mx - bw / 2, yy, mx + bw / 2, yy + TS(36)};
    cc_round(dc, b, TS(12), hov ? TM_BG : TM_WHITE, TM_S200);
    RECT t = b;
    if (g_tmIcons) {
      tm_glyph(dc, G_FOLDER, (RECT){b.left + TS(14), b.top, b.left + TS(30), b.bottom}, TM_S700, TF_ICONS);
      t.left += TS(38);
    } else {
      t.left += TS(16);
    }
    tm_text(dc, TF_SMALLM, TM_S700, L"Выбрать файлы", t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    yy += TS(48);
    tm_text(dc, TF_TINY, TM_S400, L"Поддерживается объединение многостраничных TIFF", (RECT){ix + TS(8), yy, ix + iw - TS(8), yy + TS(16)},
            DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
dz_done:
  cy += dzH + TS(16);
  cc_fill(dc, (RECT){ix, cy, ix + iw, cy + 1}, TM_S100);
  cy += 1 + TS(16);
  {
    int tx2 = ix;
    if (g_tmIcons) {
      tm_glyph(dc, G_BOLT, (RECT){ix, cy, ix + TS(14), cy + TS(18)}, TM_S600, TF_ICONS);
      tx2 += TS(20);
    }
    tm_text(dc, TF_SMALLM, TM_S600, L"Быстрое преобразование", (RECT){tx2, cy, ix + iw, cy + TS(18)},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  cy += TS(26);
  {
    RECT a, b;
    if (side) {
      int bw = (iw - TS(8)) / 2;
      a = (RECT){ix, cy, ix + bw, cy + qh};
      b = (RECT){ix + iw - bw, cy, ix + iw, cy + qh};
    } else {
      a = (RECT){ix, cy, ix + iw, cy + qh};
      b = (RECT){ix, cy + qh + TS(8), ix + iw, cy + qh * 2 + TS(8)};
    }
    BOOL en = !g_tmExporting && !g_tmLoadPending;
    COLORREF ab = tm_is_hover(H_QTIFF, -1) && en ? TM_B100 : TM_B50;
    COLORREF bb = tm_is_hover(H_QPDF, -1) && en ? TM_E100 : TM_E50;
    cc_round(dc, a, TS(16), ab, ab);
    cc_round(dc, b, TS(16), bb, bb);
    tm_text(dc, TF_SMALLM, en ? TM_B700 : TM_S400, L"В TIFF (отдельные файлы)", a,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    tm_text(dc, TF_SMALLM, en ? TM_E700 : TM_S400, L"В PDF", b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    cy = b.bottom + TS(16);
  }
  if (tipsH) tm_text(dc, TF_TINY, TM_S400, tips, (RECT){ix, cy, ix + iw, cy + tipsH}, DT_LEFT | DT_WORDBREAK);
  return y + h;
}

/* одна карточка страницы в списке */
static void tm_page_card(HDC dc, int i, RECT r, BOOL paint) {
  const TmPage *pg = &g_tmPages[i];
  RECT vis;
  IntersectRect(&vis, &r, &g_tmListRc);
  int bs = TS(32);
  RECT eye = {r.right - TS(12) - bs * 2 - TS(4), (r.top + r.bottom - bs) / 2, 0, 0};
  eye.right = eye.left + bs;
  eye.bottom = eye.top + bs;
  RECT del = {eye.right + TS(4), eye.top, eye.right + TS(4) + bs, eye.bottom};
  if (!paint) {
    tm_hot(vis, H_CARD, i);
    RECT v;
    if (IntersectRect(&v, &eye, &g_tmListRc)) tm_hot(v, H_EYE, i);
    if (IntersectRect(&v, &del, &g_tmListRc)) tm_hot(v, H_DEL, i);
    return;
  }
  BOOL sel = i == g_tmSel, hov = tm_is_hover(H_CARD, i) || tm_is_hover(H_EYE, i) || tm_is_hover(H_DEL, i);
  BOOL dragged = g_tmDragging && i == g_tmDragIdx;
  tm_card(dc, r, TS(16), dragged ? TM_S100 : TM_WHITE, sel ? TM_B400 : hov ? TM_B300 : TM_S200, sel);
  /* миниатюра */
  RECT tb = {r.left + TS(12), r.top + TS(12), r.left + TS(76), r.top + TS(76)};
  cc_round(dc, tb, TS(12), TM_S100, TM_S200);
  if (pg->thumb && pg->tw > 0 && pg->th > 0) {
    int bw = tb.right - tb.left - 4, bh = tb.bottom - tb.top - 4;
    double s = (double)bw / pg->tw < (double)bh / pg->th ? (double)bw / pg->tw : (double)bh / pg->th;
    int dw = (int)(pg->tw * s + 0.5), dh = (int)(pg->th * s + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int dx = tb.left + 2 + (bw - dw) / 2, dy = tb.top + 2 + (bh - dh) / 2;
    HDC m = CreateCompatibleDC(dc);
    HGDIOBJ ob = SelectObject(m, pg->thumb);
    int old = SetStretchBltMode(dc, HALFTONE);
    POINT org;
    SetBrushOrgEx(dc, 0, 0, &org);
    StretchBlt(dc, dx, dy, dw, dh, m, 0, 0, pg->tw, pg->th, SRCCOPY);
    SetBrushOrgEx(dc, org.x, org.y, NULL);
    SetStretchBltMode(dc, old);
    SelectObject(m, ob);
    DeleteDC(m);
  }
  wchar_t num[12];
  _snwprintf(num, 12, L"%d", i + 1);
  int nw = tm_text_w(dc, TF_NUM, num) + TS(10);
  RECT nb = {tb.right - TS(4) - nw, tb.top + TS(4), tb.right - TS(4), tb.top + TS(4) + TS(16)};
  cc_round(dc, nb, TS(6), TM_WHITE, TM_S200);
  tm_text(dc, TF_NUM, TM_S600, num, nb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  /* имя и сведения */
  int tx = tb.right + TS(14), tr = eye.left - TS(8);
  COLORREF ink = dragged ? TM_S400 : TM_S800;
  tm_text(dc, TF_BODYM, ink, pg->name, (RECT){tx, r.top + TS(20), tr, r.top + TS(40)},
          DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  wchar_t info[160];
  tm_page_info(pg, info, 160);
  tm_text(dc, TF_SMALL, TM_S500, info, (RECT){tx, r.top + TS(44), tr, r.top + TS(62)},
          DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  tm_icon_btn(dc, eye, G_EYE, H_EYE, i, TM_B50, TM_B500, TRUE, TRUE);
  tm_icon_btn(dc, del, G_X, H_DEL, i, TM_R50, TM_R500, TRUE, TRUE);
}

/* Правая карточка: заголовок, список страниц (своя прокрутка), сохранение.
   h — сколько она занимает (список растягивается под окно). */
static int tm_right(HDC dc, int x, int y, int w, int h, BOOL paint) {
  int pad = TS(24);
  int hh = TS(64);
  BOOL side = w >= TS(470);
  int bh = TS(60);
  int es = TS(20) + (side ? bh : bh * 2 + TS(12)) + TS(12) + TS(18) + TS(16);
  if (paint) {
    cc_round(dc, (RECT){x, y, x + w, y + h}, TS(24), TM_WHITE, TM_S200);
    /* низ — светло-серый, со скруглением карточки */
    HRGN clip = CreateRoundRectRgn(x + 1, y + 1, x + w, y + h, TS(48), TS(48));
    POINT org;
    GetViewportOrgEx(dc, &org);
    OffsetRgn(clip, org.x, org.y);
    SelectClipRgn(dc, clip);
    cc_fill(dc, (RECT){x, y + h - es, x + w, y + h - 1}, RGB(0xFA, 0xFB, 0xFD));
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
    cc_fill(dc, (RECT){x + 1, y + h - es, x + w - 1, y + h - es + 1}, TM_S100);
    cc_fill(dc, (RECT){x + 1, y + hh, x + w - 1, y + hh + 1}, TM_S100);
    /* заголовок и число страниц */
    int ty = y + TS(20);
    tm_text(dc, TF_SEC, TM_S800, L"Страницы документа", (RECT){x + pad, ty, x + w - TS(100), ty + TS(24)},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int tw = tm_text_w(dc, TF_SEC, L"Страницы документа");
    wchar_t cnt[40];
    _snwprintf(cnt, 40, L"%d %s", g_tmN, tm_pages_word(g_tmN));
    int cw = tm_text_w(dc, TF_BADGE, cnt) + TS(20);
    RECT cb = {x + pad + tw + TS(12), ty + TS(1), x + pad + tw + TS(12) + cw, ty + TS(23)};
    if (cb.right < x + w - TS(90)) {
      cc_round(dc, cb, TS(10), TM_S100, TM_S100);
      tm_text(dc, TF_BADGE, TM_S600, cnt, cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
  /* вверх / вниз */
  {
    int bs = TS(32), by = y + (hh - bs) / 2;
    RECT up = {x + w - pad - bs * 2 - TS(6), by, x + w - pad - bs - TS(6), by + bs};
    RECT dn = {x + w - pad - bs, by, x + w - pad, by + bs};
    BOOL busy = g_tmExporting;
    tm_icon_btn(dc, up, G_UP, H_UP, -1, TM_S100, TM_S600, !busy && g_tmSel > 0, paint);
    tm_icon_btn(dc, dn, G_DOWN, H_DOWN, -1, TM_S100, TM_S600, !busy && g_tmSel >= 0 && g_tmSel < g_tmN - 1, paint);
  }
  /* список */
  g_tmListRc = (RECT){x + 1, y + hh + 1, x + w - 1, y + h - es};
  int vpH = g_tmListRc.bottom - g_tmListRc.top;
  g_tmCardH = TS(88);
  g_tmCardGap = TS(8);
  g_tmListPad = TS(12);
  int loadH = g_tmLoadPending ? TS(52) : 0;
  g_tmListH = g_tmN || loadH ? g_tmListPad * 2 + g_tmN * (g_tmCardH + g_tmCardGap) - (g_tmN ? g_tmCardGap : 0) +
                                   (loadH && g_tmN ? g_tmCardGap : 0) + loadH
                             : 0;
  int maxs = g_tmListH - vpH;
  if (maxs < 0) maxs = 0;
  if (g_tmListScroll > maxs) g_tmListScroll = maxs;
  if (g_tmListScroll < 0) g_tmListScroll = 0;
  int saved = 0;
  if (paint) {
    saved = SaveDC(dc);
    IntersectClipRect(dc, g_tmListRc.left, g_tmListRc.top, g_tmListRc.right, g_tmListRc.bottom);
  }
  int cx = x + g_tmListPad, cw = w - g_tmListPad * 2 - (maxs ? TS(6) : 0);
  for (int i = 0; i < g_tmN; i++) {
    int cy = g_tmListRc.top + g_tmListPad + i * (g_tmCardH + g_tmCardGap) - g_tmListScroll;
    if (cy + g_tmCardH < g_tmListRc.top || cy > g_tmListRc.bottom) continue;
    tm_page_card(dc, i, (RECT){cx, cy, cx + cw, cy + g_tmCardH}, paint);
  }
  if (paint) {
    if (g_tmLoadPending) { /* страницы ещё открываются — строка хода в конце списка */
      int cy = g_tmListRc.top + g_tmListPad + g_tmN * (g_tmCardH + g_tmCardGap) - g_tmListScroll;
      RECT lr = {cx, cy, cx + cw, cy + loadH};
      cc_round(dc, lr, TS(16), TM_BG, TM_S200);
      wchar_t t[MAX_PATH + 60];
      EnterCriticalSection(&g_tmLock);
      _snwprintf(t, MAX_PATH + 60, L"Открываю: %s", g_tmProgText[0] ? g_tmProgText : L"…");
      LeaveCriticalSection(&g_tmLock);
      t[MAX_PATH + 59] = 0;
      RECT tr = lr;
      InflateRect(&tr, -TS(16), 0);
      tm_text(dc, TF_SMALL, TM_S500, t, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    }
    if (g_tmDragging && g_tmDragIns >= 0) { /* куда встанет перетаскиваемая */
      int ly = g_tmListRc.top + g_tmListPad + g_tmDragIns * (g_tmCardH + g_tmCardGap) - g_tmCardGap / 2 - g_tmListScroll;
      cc_round(dc, (RECT){cx, ly - TS(2), cx + cw, ly + TS(2)}, TS(2), TM_B500, TM_B500);
    }
    if (!g_tmN && !g_tmLoadPending) { /* пусто */
      int my = (g_tmListRc.top + g_tmListRc.bottom) / 2 - TS(60), mx = x + w / 2;
      RECT ib = {mx - TS(28), my, mx + TS(28), my + TS(56)};
      cc_round(dc, ib, TS(22), TM_S100, TM_S100);
      if (g_tmIcons) tm_glyph(dc, G_LAYERS, ib, TM_S300, TF_ICONL);
      else { /* два листа */
        cc_round(dc, (RECT){mx - TS(10), my + TS(14), mx + TS(6), my + TS(36)}, TS(2), TM_S100, TM_S300);
        cc_round(dc, (RECT){mx - TS(5), my + TS(19), mx + TS(11), my + TS(41)}, TS(2), TM_S100, TM_S300);
      }
      my += TS(68);
      tm_text(dc, TF_BODYM, TM_S500, L"Добавьте файлы для начала работы", (RECT){x + pad, my, x + w - pad, my + TS(20)},
              DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      my += TS(24);
      tm_text(dc, TF_SMALL, TM_S400, L"Файлы будут автоматически преобразованы в страницы документа",
              (RECT){mx - TS(130), my, mx + TS(130), my + TS(40)}, DT_CENTER | DT_WORDBREAK);
    }
    RestoreDC(dc, saved);
  }
  if (maxs) { /* своя полоса прокрутки списка */
    int track = vpH - TS(8);
    int th = track * vpH / g_tmListH;
    if (th < TS(30)) th = TS(30);
    int ty = g_tmListRc.top + TS(4) + (track - th) * g_tmListScroll / maxs;
    RECT tr = {g_tmListRc.right - TS(9), ty, g_tmListRc.right - TS(4), ty + th};
    if (paint) {
      COLORREF c = tm_is_hover(H_LTHUMB, -1) || g_tmThumbGrab >= 0 ? TM_S400 : TM_S300;
      cc_round(dc, tr, TS(3), c, c);
    } else {
      RECT hit = tr;
      InflateRect(&hit, TS(3), 0);
      tm_hot(hit, H_LTHUMB, -1);
    }
  }
  /* сохранение */
  {
    int ex = x + TS(20), ew = w - TS(40), ey = y + h - es + TS(20);
    RECT a, b;
    if (side) {
      int bw = (ew - TS(12)) / 2;
      a = (RECT){ex, ey, ex + bw, ey + bh};
      b = (RECT){ex + ew - bw, ey, ex + ew, ey + bh};
    } else {
      a = (RECT){ex, ey, ex + ew, ey + bh};
      b = (RECT){ex, ey + bh + TS(12), ex + ew, ey + bh * 2 + TS(12)};
    }
    BOOL en = g_tmN > 0 && !g_tmExporting && !g_tmLoadPending;
    if (en) {
      tm_hot(a, H_TIFF, -1);
      tm_hot(b, H_PDF, -1);
    }
    if (paint) {
      float dim = en ? 0.0f : 0.4f; /* недоступно — бледнее, как opacity-60 на странице */
      for (int k = 0; k < 2; k++) {
        RECT r = k ? b : a;
        BOOL hov = en && tm_is_hover(k ? H_PDF : H_TIFF, -1);
        COLORREF fill = k ? (hov ? TM_E50 : TM_WHITE) : (hov ? RGB(0, 0, 0) : TM_S900);
        COLORREF bd = k ? TM_E200 : fill;
        COLORREF fg = k ? TM_E700 : TM_WHITE, sub = k ? RGB(0x4D, 0xB0, 0x8F) : TM_S400;
        COLORREF badge = k ? TM_E100 : RGB(0x2B, 0x62, 0xE3), bfg = k ? TM_E600 : TM_WHITE;
        COLORREF under = RGB(0xFA, 0xFB, 0xFD);
        fill = tm_mix(fill, under, dim);
        bd = tm_mix(bd, under, dim);
        fg = tm_mix(fg, fill, dim);
        sub = tm_mix(sub, fill, dim);
        badge = tm_mix(badge, fill, dim);
        bfg = tm_mix(bfg, badge, dim);
        cc_round(dc, r, TS(16), fill, bd);
        const wchar_t *t1 = k ? L"Сохранить как PDF" : L"Сохранить как TIFF";
        const wchar_t *t2 = k ? L"Объединённый документ" : L"Многостраничный .tif";
        int gw = TS(32) + TS(12) + (tm_text_w(dc, TF_BTN, t1) > tm_text_w(dc, TF_BTNSUB, t2) ? tm_text_w(dc, TF_BTN, t1)
                                                                                            : tm_text_w(dc, TF_BTNSUB, t2));
        int gx = (r.left + r.right - gw) / 2;
        if (gx < r.left + TS(12)) gx = r.left + TS(12);
        int my = (r.top + r.bottom) / 2;
        RECT bg = {gx, my - TS(16), gx + TS(32), my + TS(16)};
        cc_round(dc, bg, TS(12), badge, badge);
        if (g_tmIcons) tm_glyph(dc, k ? G_DOC : G_PHOTO, bg, bfg, TF_ICON);
        else tm_text(dc, TF_NUM, bfg, k ? L"PDF" : L"TIF", bg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        int tx = gx + TS(44);
        tm_text(dc, TF_BTN, fg, t1, (RECT){tx, my - TS(18), r.right - TS(8), my + TS(2)},
                DT_LEFT | DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS);
        tm_text(dc, TF_BTNSUB, sub, t2, (RECT){tx, my + TS(2), r.right - TS(8), my + TS(18)},
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
      }
    }
    /* строка под кнопками: подсказка или что сохранили (со ссылками) */
    int ly = b.bottom + TS(12);
    RECT lr = {x + pad, ly, x + w - pad, ly + TS(18)};
    if (g_tmStatus[0]) {
      const wchar_t *l1 = g_tmStatusFolderOnly || !g_tmStatusPath[0] ? NULL : L"Открыть";
      const wchar_t *l2 = L"Показать в папке";
      int w0 = tm_text_w(dc, TF_TINY, g_tmStatus), w1 = l1 ? tm_text_w(dc, TF_SMALLM, l1) : 0,
          w2 = g_tmStatusPath[0] ? tm_text_w(dc, TF_SMALLM, l2) : 0;
      int sp = TS(14), total = w0 + (w1 ? sp + w1 : 0) + (w2 ? sp + w2 : 0);
      int lx = (lr.left + lr.right - total) / 2;
      if (lx < lr.left) lx = lr.left;
      int w0v = w0 < lr.right - lr.left - (total - w0) ? w0 : lr.right - lr.left - (total - w0);
      if (w0v < TS(40)) w0v = TS(40);
      if (paint)
        tm_text(dc, TF_TINY, TM_E700, g_tmStatus, (RECT){lx, ly, lx + w0v, ly + TS(18)},
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      lx += w0v + sp;
      if (w1) {
        RECT r1 = {lx, ly, lx + w1, ly + TS(18)};
        if (paint) tm_text(dc, TF_SMALLM, tm_is_hover(H_OPEN_OUT, -1) ? TM_B700 : TM_B600, l1, r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        else tm_hot(r1, H_OPEN_OUT, -1);
        lx += w1 + sp;
      }
      if (w2) {
        RECT r2 = {lx, ly, lx + w2, ly + TS(18)};
        if (paint) tm_text(dc, TF_SMALLM, tm_is_hover(H_SHOW_OUT, -1) ? TM_B700 : TM_B600, l2, r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        else tm_hot(r2, H_SHOW_OUT, -1);
      }
    } else if (paint) {
      const wchar_t *t = L"Ч/б листы — без потерь и компактно (G4), размер листа — по dpi исходника";
      int tw = tm_text_w(dc, TF_TINY, t) + (g_tmIcons ? TS(18) : 0);
      int lx = (lr.left + lr.right - tw) / 2;
      if (lx < lr.left) lx = lr.left;
      if (g_tmIcons) {
        tm_glyph(dc, G_INFO, (RECT){lx, ly, lx + TS(12), ly + TS(18)}, TM_S500, TF_ICONS);
        lx += TS(18);
      }
      tm_text(dc, TF_TINY, TM_S500, t, (RECT){lx, ly, lr.right, ly + TS(18)}, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
  }
  return y + h;
}

/* всё полотно; ch — высота окна (правая карточка тянется под неё) */
static int tm_draw(HDC dc, int cw, int ch, BOOL paint) {
  g_tmPainting = paint;
  if (!paint) g_tmNHot = 0;
  int m = TS(24), gap = TS(24);
  int y = m;
  if (paint) {
    /* шапка: синий квадрат-значок, название, «без интернета» справа */
    RECT ic = {m, y, m + TS(44), y + TS(44)};
    cc_round(dc, ic, TS(16), TM_B600, TM_B600);
    if (g_tmIcons) {
      tm_glyph(dc, G_PHOTO, ic, TM_WHITE, TF_ICONL);
    } else { /* две картинки стопкой */
      cc_round(dc, (RECT){ic.left + TS(11), ic.top + TS(13), ic.left + TS(29), ic.top + TS(29)}, TS(3), TM_B600, TM_WHITE);
      cc_round(dc, (RECT){ic.left + TS(15), ic.top + TS(17), ic.left + TS(33), ic.top + TS(33)}, TS(3), TM_WHITE, TM_WHITE);
    }
    int tx = ic.right + TS(12);
    BOOL pill = cw >= TS(720);
    int pw = pill ? tm_text_w(dc, TF_SMALLM, L"Работает без интернета") + TS(g_tmIcons ? 44 : 26) : 0;
    tm_text(dc, TF_H1, TM_S900, L"TIFF & PDF Toolkit", (RECT){tx, y - TS(4), cw - m - pw - TS(8), y + TS(30)},
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    tm_text(dc, TF_SUB, TM_S500, L"Конвертер и объединитель файлов", (RECT){tx, y + TS(28), cw - m - pw - TS(8), y + TS(46)},
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (pill) {
      RECT pr = {cw - m - pw, y + TS(6), cw - m, y + TS(38)};
      cc_round(dc, pr, TS(12), TM_WHITE, TM_S200);
      RECT t = pr;
      if (g_tmIcons) {
        tm_glyph(dc, G_SHIELD, (RECT){pr.left + TS(12), pr.top, pr.left + TS(26), pr.bottom}, TM_E500, TF_ICONS);
        t.left += TS(32);
      } else {
        t.left += TS(13);
      }
      tm_text(dc, TF_SMALLM, TM_S600, L"Работает без интернета", t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
  }
  y += TS(44) + TS(28);
  int avail = cw - m * 2;
  int minRight = TS(470);
  if (g_tmTwo) {
    int lw = (avail - gap) * 5 / 12, rw = avail - gap - lw;
    int yl = tm_left(dc, m, y, lw, paint);
    int rh = ch - y - m;
    if (rh < minRight) rh = minRight;
    int yr = tm_right(dc, m + lw + gap, y, rw, rh, paint);
    return (yl > yr ? yl : yr) + m;
  }
  int yl = tm_left(dc, m, y, avail, paint) + gap;
  int rh = ch - m * 2;
  if (rh < minRight) rh = minRight;
  return tm_right(dc, m, yl, avail, rh, paint) + m;
}

/* поверх всего — полоса хода сохранения (координаты окна, без прокрутки) */
static void tm_draw_progress(HDC dc, int cw, int ch) {
  /* затемнение: чёрный с прозрачностью */
  HDC m = CreateCompatibleDC(dc);
  HBITMAP px = CreateCompatibleBitmap(dc, 1, 1);
  HGDIOBJ ob = SelectObject(m, px);
  SetPixel(m, 0, 0, RGB(0x0F, 0x17, 0x2A));
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 130, 0};
  AlphaBlend(dc, 0, 0, cw, ch, m, 0, 0, 1, 1, bf);
  SelectObject(m, ob);
  DeleteObject(px);
  DeleteDC(m);
  int w = TS(440);
  if (w > cw - TS(32)) w = cw - TS(32);
  int h = TS(200);
  RECT r = {(cw - w) / 2, (ch - h) / 2, (cw - w) / 2 + w, (ch - h) / 2 + h};
  cc_round(dc, r, TS(24), TM_WHITE, TM_WHITE);
  wchar_t title[80], text[MAX_PATH + 40];
  int pct;
  EnterCriticalSection(&g_tmLock);
  lstrcpyW(title, g_tmProgTitle);
  lstrcpyW(text, g_tmProgText);
  pct = g_tmProgPct;
  LeaveCriticalSection(&g_tmLock);
  int x = r.left + TS(24), iw = w - TS(48), y = r.top + TS(24);
  RECT ic = {x, y, x + TS(36), y + TS(36)};
  cc_round(dc, ic, TS(14), TM_B100, TM_B100);
  if (g_tmIcons) tm_glyph(dc, G_GEAR, ic, TM_B600, TF_ICON);
  else tm_glyph(dc, G_DOWN, ic, TM_B600, TF_ICON);
  tm_text(dc, TF_SEC, TM_S900, title, (RECT){x + TS(48), y - TS(1), x + iw, y + TS(19)}, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  tm_text(dc, TF_SMALL, TM_S500, g_tmCancel ? L"Останавливаю…" : L"Не закрывайте окно — можно свернуть",
          (RECT){x + TS(48), y + TS(19), x + iw, y + TS(37)}, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  y += TS(56);
  RECT bar = {x, y, x + iw, y + TS(8)};
  cc_round(dc, bar, TS(4), TM_S100, TM_S100);
  int fw = (int)((long long)iw * pct / 1000);
  if (fw > TS(8)) cc_round(dc, (RECT){x, y, x + fw, y + TS(8)}, TS(4), TM_B500, TM_B500);
  y += TS(18);
  wchar_t p[16];
  _snwprintf(p, 16, L"%d%%", pct / 10);
  int pw = tm_text_w(dc, TF_SMALLM, p);
  tm_text(dc, TF_SMALL, TM_S500, text, (RECT){x, y, x + iw - pw - TS(10), y + TS(18)},
          DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS);
  tm_text(dc, TF_SMALLM, TM_S700, p, (RECT){x + iw - pw, y, x + iw, y + TS(18)}, DT_RIGHT | DT_SINGLELINE);
  y += TS(36);
  int bw = tm_text_w(dc, TF_SMALLM, L"Отмена") + TS(40);
  g_tmCancelRc = (RECT){r.right - TS(24) - bw, y, r.right - TS(24), y + TS(36)};
  BOOL hov = g_tmHoverId == -2;
  cc_round(dc, g_tmCancelRc, TS(12), hov ? TM_R50 : TM_WHITE, TM_S200);
  tm_text(dc, TF_SMALLM, hov ? TM_R500 : TM_S700, L"Отмена", g_tmCancelRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* ---- полотно: раскладка, прокрутка, мышь, клавиши ------------------------------- */

static void tm_fit(int cw) {
  const float W2 = 980.0f, W1 = 560.0f, MINF = 0.72f;
  float want = g_tmDpi * g_tmZoom, ns;
  if (cw >= W2 * want) {
    g_tmTwo = TRUE;
    ns = want;
  } else if (cw >= W2 * want * MINF) {
    g_tmTwo = TRUE;
    ns = cw / W2;
  } else {
    g_tmTwo = FALSE;
    ns = cw / W1;
    if (ns > want) ns = want;
    if (ns < want * MINF) ns = want * MINF;
  }
  if (ns - g_tmS > 0.004f || g_tmS - ns > 0.004f) {
    g_tmS = ns;
    tm_fonts();
  }
}

static void tm_relayout(void) {
  if (!g_tmCanvas) return;
  RECT rc;
  GetClientRect(g_tmCanvas, &rc);
  tm_fit(rc.right);
  HDC dc = GetDC(g_tmCanvas);
  g_tmContentH = tm_draw(dc, rc.right, rc.bottom, FALSE);
  ReleaseDC(g_tmCanvas, dc);
  int maxs = g_tmContentH - rc.bottom;
  if (maxs < 0) maxs = 0;
  if (g_tmScroll > maxs) g_tmScroll = maxs;
  if (g_tmScroll < 0) g_tmScroll = 0;
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMax = g_tmContentH - 1;
  si.nPage = (UINT)rc.bottom;
  si.nPos = g_tmScroll;
  /* появилась или пропала полоса — окно сузилось, WM_SIZE разложит заново */
  SetScrollInfo(g_tmCanvas, SB_VERT, &si, TRUE);
  InvalidateRect(g_tmCanvas, NULL, FALSE);
}

static void tm_scroll_to(int pos) {
  RECT rc;
  GetClientRect(g_tmCanvas, &rc);
  int maxs = g_tmContentH - rc.bottom;
  if (maxs < 0) maxs = 0;
  if (pos > maxs) pos = maxs;
  if (pos < 0) pos = 0;
  if (pos == g_tmScroll) return;
  g_tmScroll = pos;
  SetScrollPos(g_tmCanvas, SB_VERT, pos, TRUE);
  tm_relayout();
}

static BOOL tm_list_scroll_by(int d) { /* FALSE — список дальше не листается */
  int vpH = g_tmListRc.bottom - g_tmListRc.top, maxs = g_tmListH - vpH;
  if (maxs <= 0) return FALSE;
  int pos = g_tmListScroll + d;
  if (pos < 0) pos = 0;
  if (pos > maxs) pos = maxs;
  if (pos == g_tmListScroll) return FALSE;
  g_tmListScroll = pos;
  tm_relayout();
  return TRUE;
}

/* выбранную — в видимую часть списка */
static void tm_reveal(int i) {
  if (i < 0) return;
  int top = g_tmListPad + i * (g_tmCardH + g_tmCardGap), vpH = g_tmListRc.bottom - g_tmListRc.top;
  if (top - g_tmListPad < g_tmListScroll) g_tmListScroll = top - g_tmListPad;
  else if (top + g_tmCardH + g_tmListPad > g_tmListScroll + vpH) g_tmListScroll = top + g_tmCardH + g_tmListPad - vpH;
  tm_relayout();
}

static int tm_hit(int x, int y, int *arg) { /* координаты окна */
  if (g_tmExporting) {
    POINT p = {x, y};
    *arg = -1;
    return PtInRect(&g_tmCancelRc, p) ? -2 : H_NONE;
  }
  y += g_tmScroll;
  for (int i = g_tmNHot - 1; i >= 0; i--) { /* мелкое (кнопки на карточке) — поверх крупного */
    POINT p = {x, y};
    if (PtInRect(&g_tmHot[i].r, p)) {
      *arg = g_tmHot[i].arg;
      return g_tmHot[i].id;
    }
  }
  *arg = -1;
  return H_NONE;
}

/* куда встанет перетаскиваемая карточка при мыши на высоте y (окна) */
static int tm_drop_slot(int y) {
  int rel = y + g_tmScroll - g_tmListRc.top - g_tmListPad + g_tmListScroll;
  int k = (rel + (g_tmCardH + g_tmCardGap) / 2) / (g_tmCardH + g_tmCardGap);
  if (rel < 0) k = 0;
  if (k > g_tmN) k = g_tmN;
  return k;
}

static void tm_context_menu(int i, int sx, int sy) {
  if (i < 0 || i >= g_tmN) return;
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, 1, L"Открыть");
  AppendMenuW(m, MF_STRING, 2, L"Показать в папке");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING | (i > 0 ? 0 : MF_GRAYED), 3, L"Выше\tCtrl+↑");
  AppendMenuW(m, MF_STRING | (i < g_tmN - 1 ? 0 : MF_GRAYED), 4, L"Ниже\tCtrl+↓");
  AppendMenuW(m, MF_STRING | (i > 0 ? 0 : MF_GRAYED), 5, L"В начало");
  AppendMenuW(m, MF_STRING | (i < g_tmN - 1 ? 0 : MF_GRAYED), 6, L"В конец");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, 7, L"Убрать\tDel");
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, sx, sy, 0, g_tmWnd, NULL);
  DestroyMenu(m);
  wchar_t path[MAX_PATH];
  lstrcpynW(path, g_tmPages[i].path, MAX_PATH);
  switch (cmd) {
  case 1: tm_open_file(path); break;
  case 2: tm_show_in_folder(path); break;
  case 3: tm_move(i, i - 1); break;
  case 4: tm_move(i, i + 1); break;
  case 5: tm_move(i, 0); break;
  case 6: tm_move(i, g_tmN - 1); break;
  case 7: tm_remove(i); break;
  }
}

static void tm_click(int id, int arg) {
  switch (id) {
  case H_ADD: tm_add_files(); break;
  case H_CLEAR: tm_clear(); break;
  case H_QTIFF: tm_quick(FALSE); break;
  case H_QPDF: tm_quick(TRUE); break;
  case H_UP:
    if (g_tmSel > 0) {
      tm_move(g_tmSel, g_tmSel - 1);
      tm_reveal(g_tmSel);
    }
    break;
  case H_DOWN:
    if (g_tmSel >= 0 && g_tmSel < g_tmN - 1) {
      tm_move(g_tmSel, g_tmSel + 1);
      tm_reveal(g_tmSel);
    }
    break;
  case H_EYE:
    if (arg >= 0 && arg < g_tmN) {
      g_tmSel = arg;
      tm_relayout();
      tm_open_file(g_tmPages[arg].path);
    }
    break;
  case H_DEL: tm_remove(arg); break;
  case H_TIFF: tm_export(FALSE); break;
  case H_PDF: tm_export(TRUE); break;
  case H_OPEN_OUT: tm_open_file(g_tmStatusPath); break;
  case H_SHOW_OUT: tm_show_in_folder(g_tmStatusPath); break;
  }
}

static void tm_save_pref(void);

static LRESULT CALLBACK TmCanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    cc_fill(mem, rc, TM_BG);
    SetBkMode(mem, TRANSPARENT);
    SetViewportOrgEx(mem, 0, -g_tmScroll, NULL);
    tm_draw(mem, rc.right, rc.bottom, TRUE);
    SetViewportOrgEx(mem, 0, 0, NULL);
    if (g_tmExporting) tm_draw_progress(mem, rc.right, rc.bottom);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_SIZE:
    tm_relayout();
    return 0;
  case WM_VSCROLL: {
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, &si);
    int pos = g_tmScroll;
    switch (LOWORD(wParam)) {
    case SB_LINEUP: pos -= TS(40); break;
    case SB_LINEDOWN: pos += TS(40); break;
    case SB_PAGEUP: pos -= (int)si.nPage; break;
    case SB_PAGEDOWN: pos += (int)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = g_tmContentH; break;
    }
    tm_scroll_to(pos);
    return 0;
  }
  case WM_MOUSEWHEEL: {
    int d = GET_WHEEL_DELTA_WPARAM(wParam);
    if (GetKeyState(VK_CONTROL) & 0x8000) { /* Ctrl+колесо — масштаб */
      g_tmZoom *= d > 0 ? 1.1f : 1 / 1.1f;
      if (g_tmZoom < 0.6f) g_tmZoom = 0.6f;
      if (g_tmZoom > 1.6f) g_tmZoom = 1.6f;
      tm_relayout();
      tm_save_pref();
      return 0;
    }
    if (g_tmExporting) return 0;
    POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &p);
    p.y += g_tmScroll;
    /* над списком — листаем список, пока он листается; дальше — окно */
    if (PtInRect(&g_tmListRc, p) && tm_list_scroll_by(-d * TS(100) / WHEEL_DELTA)) return 0;
    tm_scroll_to(g_tmScroll - d * TS(60) / WHEEL_DELTA);
    return 0;
  }
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT) {
      if (g_tmDragging) {
        SetCursor(LoadCursorW(NULL, IDC_SIZENS));
        return TRUE;
      }
      POINT p;
      GetCursorPos(&p);
      ScreenToClient(hwnd, &p);
      int arg, id = tm_hit(p.x, p.y, &arg);
      if (id != H_NONE && id != H_CARD && id != H_LTHUMB) {
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
      }
    }
    break;
  case WM_MOUSEMOVE: {
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    if (g_tmThumbGrab >= 0) { /* тянут полосу прокрутки списка */
      int vpH = g_tmListRc.bottom - g_tmListRc.top, maxs = g_tmListH - vpH;
      int track = vpH - TS(8), th = track * vpH / (g_tmListH ? g_tmListH : 1);
      if (th < TS(30)) th = TS(30);
      if (maxs > 0 && track > th) {
        g_tmListScroll = g_tmThumbGrab + (y - g_tmDragY0) * maxs / (track - th);
        tm_relayout();
      }
      return 0;
    }
    if (g_tmDragIdx >= 0 && (wParam & MK_LBUTTON)) {
      if (!g_tmDragging && abs(y - g_tmDragY0) > TS(6)) {
        g_tmDragging = TRUE;
        SetTimer(hwnd, TIMER_TM_AUTOSCROLL, 40, NULL);
      }
      if (g_tmDragging) {
        int k = tm_drop_slot(y);
        if (k != g_tmDragIns) {
          g_tmDragIns = k;
          InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
      }
    }
    int arg, id = tm_hit(x, y, &arg);
    if (id != g_tmHoverId || arg != g_tmHoverArg) {
      g_tmHoverId = id;
      g_tmHoverArg = arg;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    TRACKMOUSEEVENT te = {sizeof(te), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&te);
    return 0;
  }
  case WM_MOUSELEAVE:
    if (g_tmHoverId != H_NONE) {
      g_tmHoverId = H_NONE;
      g_tmHoverArg = -1;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_TIMER:
    if (wParam == TIMER_TM_AUTOSCROLL) { /* тащат карточку к краю списка — список едет сам */
      if (!g_tmDragging) {
        KillTimer(hwnd, TIMER_TM_AUTOSCROLL);
        return 0;
      }
      POINT p;
      GetCursorPos(&p);
      ScreenToClient(hwnd, &p);
      int y = p.y + g_tmScroll, edge = TS(40);
      int d = y < g_tmListRc.top + edge ? -TS(14) : y > g_tmListRc.bottom - edge ? TS(14) : 0;
      if (d && tm_list_scroll_by(d)) {
        g_tmDragIns = tm_drop_slot(p.y);
        InvalidateRect(hwnd, NULL, FALSE);
      }
    }
    return 0;
  case WM_LBUTTONDOWN: {
    SetFocus(hwnd);
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    int arg, id = tm_hit(x, y, &arg);
    if (id == -2) { /* «Отмена» */
      InterlockedExchange(&g_tmCancel, 1);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    if (g_tmExporting) return 0;
    if (id == H_LTHUMB) {
      g_tmThumbGrab = g_tmListScroll;
      g_tmDragY0 = y;
      SetCapture(hwnd);
      return 0;
    }
    if (id == H_CARD) {
      g_tmSel = arg;
      g_tmDragIdx = arg;
      g_tmDragY0 = y;
      g_tmDragIns = -1;
      SetCapture(hwnd);
      tm_relayout();
      return 0;
    }
    if (id != H_NONE) tm_click(id, arg);
    return 0;
  }
  case WM_LBUTTONUP:
    if (g_tmThumbGrab >= 0) {
      g_tmThumbGrab = -1;
      ReleaseCapture();
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    if (g_tmDragIdx >= 0) {
      int from = g_tmDragIdx, ins = g_tmDragIns;
      BOOL dragged = g_tmDragging;
      g_tmDragIdx = -1;
      g_tmDragging = FALSE;
      g_tmDragIns = -1;
      KillTimer(hwnd, TIMER_TM_AUTOSCROLL);
      ReleaseCapture();
      if (dragged && ins >= 0) tm_move(from, ins > from ? ins - 1 : ins);
      else InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_CAPTURECHANGED:
    if ((HWND)lParam != hwnd && (g_tmDragIdx >= 0 || g_tmThumbGrab >= 0)) {
      g_tmDragIdx = -1;
      g_tmDragging = FALSE;
      g_tmDragIns = -1;
      g_tmThumbGrab = -1;
      KillTimer(hwnd, TIMER_TM_AUTOSCROLL);
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_LBUTTONDBLCLK: {
    int arg, id = tm_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &arg);
    if (id == H_CARD && arg >= 0 && arg < g_tmN) tm_open_file(g_tmPages[arg].path);
    else if (id != H_NONE && id != -2 && !g_tmExporting) tm_click(id, arg); /* двойной по кнопке — как одиночный */
    return 0;
  }
  case WM_RBUTTONUP: {
    int arg, id = tm_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &arg);
    if ((id == H_CARD || id == H_EYE || id == H_DEL) && !g_tmExporting) {
      g_tmSel = arg;
      tm_relayout();
      POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ClientToScreen(hwnd, &p);
      tm_context_menu(arg, p.x, p.y);
    }
    return 0;
  }
  case WM_GETDLGCODE:
    return DLGC_WANTARROWS | DLGC_WANTCHARS;
  case WM_KEYDOWN: {
    if (g_tmExporting) {
      if (wParam == VK_ESCAPE) InterlockedExchange(&g_tmCancel, 1);
      return 0;
    }
    BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    switch (wParam) {
    case VK_UP:
    case VK_DOWN: {
      int d = wParam == VK_UP ? -1 : 1;
      if (!g_tmN) break;
      if (ctrl && g_tmSel >= 0) tm_move(g_tmSel, g_tmSel + d);
      else g_tmSel = g_tmSel < 0 ? 0 : g_tmSel + d < 0 ? 0 : g_tmSel + d >= g_tmN ? g_tmN - 1 : g_tmSel + d;
      tm_reveal(g_tmSel);
      break;
    }
    case VK_HOME:
    case VK_END:
      if (!g_tmN) break;
      if (ctrl && g_tmSel >= 0) tm_move(g_tmSel, wParam == VK_HOME ? 0 : g_tmN - 1);
      else g_tmSel = wParam == VK_HOME ? 0 : g_tmN - 1;
      tm_reveal(g_tmSel);
      break;
    case VK_DELETE:
      tm_remove(g_tmSel);
      break;
    case VK_RETURN:
      if (g_tmSel >= 0 && g_tmSel < g_tmN) tm_open_file(g_tmPages[g_tmSel].path);
      break;
    case 'V':
      if (ctrl) tm_paste();
      break;
    case 'O':
      if (ctrl) tm_add_files();
      break;
    case 'S':
      if (ctrl) tm_export((GetKeyState(VK_SHIFT) & 0x8000) != 0); /* Ctrl+S — TIFF, Ctrl+Shift+S — PDF */
      break;
    }
    return 0;
  }
  case WM_DROPFILES: {
    HDROP hd = (HDROP)wParam;
    TmPaths l = {0};
    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < n; i++) {
      wchar_t p[MAX_PATH];
      if (DragQueryFileW(hd, i, p, MAX_PATH)) tm_paths_add_any(&l, p);
    }
    DragFinish(hd);
    tm_load_paths(&l);
    SetForegroundWindow(g_tmWnd);
    return 0;
  }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* масштаб и место окна — между запусками, в tiffmerge.txt */
static void tm_pref_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\tiffmerge.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static void tm_save_pref(void) {
  if (!g_dataDir[0] || !g_tmWnd) return;
  WINDOWPLACEMENT wp;
  wp.length = sizeof(wp);
  if (!GetWindowPlacement(g_tmWnd, &wp)) return;
  RECT r = wp.rcNormalPosition;
  char b[120];
  int n = snprintf(b, sizeof(b), "%d %ld %ld %ld %ld\n", (int)(g_tmZoom * 100 + 0.5f), r.left, r.top, r.right - r.left,
                   r.bottom - r.top);
  wchar_t p[MAX_PATH];
  tm_pref_path(p);
  HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(f, b, (DWORD)n, &w, NULL);
  CloseHandle(f);
}

static BOOL tm_load_pref(RECT *out) {
  if (!g_dataDir[0]) return FALSE;
  wchar_t p[MAX_PATH];
  tm_pref_path(p);
  char *b = NULL;
  DWORD n = 0;
  if (!read_file_bytes(p, &b, &n, 200)) return FALSE;
  int z = 100;
  long x = 0, y = 0, w = 0, h = 0;
  int k = sscanf(b, "%d %ld %ld %ld %ld", &z, &x, &y, &w, &h);
  free(b);
  if (k >= 1 && z >= 60 && z <= 160) g_tmZoom = z / 100.0f;
  if (k < 5 || w < 300 || h < 250) return FALSE;
  RECT r = {x, y, x + w, y + h};
  if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) return FALSE;
  *out = r;
  return TRUE;
}

static void tm_done(TmResult *r) {
  if (r->type == TJ_LOAD) {
    if (g_tmLoadPending > 0) g_tmLoadPending--;
  } else {
    g_tmExporting = FALSE;
    wchar_t t[MAX_PATH + 80];
    if (r->ok && r->type == TJ_EACH_TIFF) {
      _snwprintf(t, MAX_PATH + 80, L"Готово, файлов TIFF: %d — рядом с исходными", r->files);
      t[MAX_PATH + 79] = 0;
      tm_status(t, r->out, TRUE);
    } else if (r->ok) {
      wchar_t sz[32];
      double mb = r->bytes / 1048576.0;
      if (mb < 0.1) _snwprintf(sz, 32, L"%d КБ", (int)((r->bytes + 1023) / 1024));
      else _snwprintf(sz, 32, L"%d,%d МБ", (int)mb, (int)((mb - (int)mb) * 10));
      _snwprintf(t, MAX_PATH + 80, L"Сохранено: %s · %d стр. · %s", tm_base(r->out), r->pages, sz);
      t[MAX_PATH + 79] = 0;
      tm_status(t, r->out, FALSE);
    } else if (r->cancelled) {
      tm_status(L"Сохранение отменено", NULL, FALSE);
    }
  }
  tm_relayout();
  if (r->err[0]) {
    wchar_t msg[2300];
    const wchar_t *head = r->type == TJ_LOAD         ? L"Не удалось открыть:"
                          : r->ok                     ? L"Готово, но не всё:"
                          : r->type == TJ_EACH_TIFF ? L"Не удалось преобразовать:"
                                                      : L"Файл не сохранён:";
    _snwprintf(msg, 2300, L"%s\n\n%s", head, r->err);
    msg[2299] = 0;
    MessageBoxW(g_tmWnd, msg, L"Объединение TIFF / PDF", MB_ICONWARNING);
  }
  free(r);
}

static LRESULT CALLBACK TmProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = (int)(480 * g_tmDpi);
    mm->ptMinTrackSize.y = (int)(420 * g_tmDpi);
    return 0;
  }
  case WM_SIZE:
    if (g_tmCanvas) MoveWindow(g_tmCanvas, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
    return 0;
  case WM_SETFOCUS:
    if (g_tmCanvas) SetFocus(g_tmCanvas);
    return 0;
  case WM_MOUSEWHEEL:
    if (g_tmCanvas) SendMessageW(g_tmCanvas, msg, wParam, lParam);
    return 0;
  case WM_TM_PAGE: {
    TmPage *pg = (TmPage *)lParam;
    if (g_tmN >= g_tmCap) {
      int c = g_tmCap ? g_tmCap * 2 : 32;
      TmPage *q = (TmPage *)realloc(g_tmPages, sizeof(TmPage) * (size_t)c);
      if (!q) {
        if (pg->thumb) DeleteObject(pg->thumb);
        free(pg);
        return 0;
      }
      g_tmPages = q;
      g_tmCap = c;
    }
    g_tmPages[g_tmN++] = *pg;
    free(pg);
    if (g_tmStatus[0]) tm_status(L"", NULL, FALSE); /* новый документ — старая строка «сохранено» ни к чему */
    tm_relayout();
    return 0;
  }
  case WM_TM_PROGRESS:
    if (g_tmCanvas) InvalidateRect(g_tmCanvas, NULL, FALSE);
    return 0;
  case WM_TM_DONE:
    tm_done((TmResult *)lParam);
    return 0;
  case WM_EXITSIZEMOVE:
    tm_save_pref();
    return 0;
  case WM_CLOSE:
    tm_save_pref();
    ShowWindow(hwnd, SW_HIDE); /* прячем: страницы остаются до следующего открытия */
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void tiffmerge_show(void) {
  if (!g_tmWnd) {
    InitializeCriticalSection(&g_tmLock);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); /* для Ctrl+V с картинкой */
    HDC s = GetDC(NULL);
    g_tmDpi = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    g_tmS = g_tmDpi;
    if (s) ReleaseDC(NULL, s);
    if (tm_have_font(L"Segoe Fluent Icons")) {
      g_tmIcons = TRUE;
      g_tmIconFace = L"Segoe Fluent Icons";
    } else {
      g_tmIcons = tm_have_font(L"Segoe MDL2 Assets");
    }
    tm_fonts();
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TmProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CursorPadTiffMerge";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = TmCanvasProc;
    wc.lpszClassName = L"CursorPadTiffMergeCanvas";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int waw = wa.right - wa.left, wah = wa.bottom - wa.top;
    int ww = (int)(1020 * g_tmDpi), wh = (int)(760 * g_tmDpi);
    if (ww > waw * 3 / 4) ww = waw * 3 / 4;
    if (wh > wah * 7 / 8) wh = wah * 7 / 8;
    int wx = wa.left + (waw - ww) / 2, wy = wa.top + (wah - wh) / 2;
    RECT saved;
    if (tm_load_pref(&saved)) {
      wx = saved.left;
      wy = saved.top;
      ww = saved.right - saved.left;
      wh = saved.bottom - saved.top;
    }
    g_tmWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"CursorPadTiffMerge", L"Объединение TIFF / PDF — CursorPad",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wx, wy, ww, wh, NULL, NULL, g_inst, NULL);
    if (!g_tmWnd) return;
    RECT rc;
    GetClientRect(g_tmWnd, &rc);
    g_tmCanvas = CreateWindowExW(WS_EX_ACCEPTFILES, L"CursorPadTiffMergeCanvas", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                 0, 0, rc.right, rc.bottom, g_tmWnd, NULL, g_inst, NULL);
    /* перетаскивание из Проводника работает и у окна, запущенного «от админа» */
    typedef BOOL(WINAPI * ChangeFilterEx)(HWND, UINT, DWORD, void *);
    ChangeFilterEx cf = (ChangeFilterEx)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "ChangeWindowMessageFilterEx");
    if (cf) {
      cf(g_tmCanvas, WM_DROPFILES, 1, NULL);
      cf(g_tmCanvas, 0x0049 /* WM_COPYGLOBALDATA */, 1, NULL);
    }
    tm_relayout();
  }
  ShowWindow(g_tmWnd, IsIconic(g_tmWnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g_tmWnd);
  SetFocus(g_tmCanvas);
}
