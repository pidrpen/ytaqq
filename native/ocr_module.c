/* CursorPad OCR companion — picture in, text out.

   Deliberately a separate process: recognition reaches into system codecs and
   graphics, which behave differently from machine to machine. Today that work
   happens inside PowerShell, so a crash there never touches the notepad. This
   keeps that isolation while dropping the PowerShell dependency, which
   corporate policy often restricts.

   Called as:  CursorPadOcr.exe <bmp-path> [lang]
               CursorPadOcr.exe --prep <in.bmp> <out.bmp>   (picture for Tesseract)
               CursorPadOcr.exe --pdf <in.pdf> <folder> <dpi> (PDF pages to PNG, pdf_pages.c)
   Writes UTF-8 text to stdout, one recognised line per output line.
   Exit codes: 0 text, 2 no recognition engine, 3 nothing recognised, 1 error.

   The WinRT interfaces below are declared by hand: mingw-w64 ships the
   plumbing (HSTRING, activation, async) but no headers for Windows.Media.Ocr.
   Method order and IIDs are taken from the published Windows metadata, not
   from memory — the order of ISoftwareBitmapStatics and IOcrEngineStatics in
   particular is not what it intuitively looks like. */

#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ASYNC_STATUS_DEFINED
typedef enum { AsyncStatus_Started = 0, AsyncStatus_Completed = 1,
               AsyncStatus_Canceled = 2, AsyncStatus_Error = 3 } CpAsyncStatus;
#endif

#define BITMAP_PIXEL_FORMAT_BGRA8 87
#define BITMAP_ALPHA_MODE_PREMULTIPLIED 0

/* ---- hand-declared WinRT interfaces ------------------------------------- */

typedef struct CpUnk CpUnk; /* anything we only pass along */
struct CpUnkVtbl {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(void *);
  ULONG(STDMETHODCALLTYPE *Release)(void *);
  void *GetIids, *GetRuntimeClassName, *GetTrustLevel;
};
struct CpUnk {
  struct CpUnkVtbl *v;
};
#define CP_RELEASE(p) do { if (p) { ((CpUnk *)(p))->v->Release(p); (p) = NULL; } } while (0)

/* Windows.Security.Cryptography.CryptographicBuffer */
typedef struct CpBufStatics CpBufStatics;
struct CpBufStaticsVtbl {
  struct CpUnkVtbl base;
  void *Compare, *GenerateRandom, *GenerateRandomNumber;
  HRESULT(STDMETHODCALLTYPE *CreateFromByteArray)(CpBufStatics *, UINT32, BYTE *, CpUnk **);
};
struct CpBufStatics {
  struct CpBufStaticsVtbl *v;
};

/* Windows.Globalization.Language */
typedef struct CpLangFactory CpLangFactory;
struct CpLangFactoryVtbl {
  struct CpUnkVtbl base;
  HRESULT(STDMETHODCALLTYPE *CreateLanguage)(CpLangFactory *, HSTRING, CpUnk **);
};
struct CpLangFactory {
  struct CpLangFactoryVtbl *v;
};

/* Windows.Graphics.Imaging.SoftwareBitmap statics.
   Order: Copy, Convert, ConvertWithAlpha, CreateCopyFromBuffer, ... */
typedef struct CpBmpStatics CpBmpStatics;
struct CpBmpStaticsVtbl {
  struct CpUnkVtbl base;
  void *Copy, *Convert, *ConvertWithAlpha;
  HRESULT(STDMETHODCALLTYPE *CreateCopyFromBuffer)(CpBmpStatics *, CpUnk *, int, INT32, INT32,
                                                   CpUnk **);
};
struct CpBmpStatics {
  struct CpBmpStaticsVtbl *v;
};

/* Windows.Media.Ocr.OcrEngine statics.
   Order: MaxImageDimension, AvailableRecognizerLanguages, IsLanguageSupported,
   TryCreateFromLanguage, TryCreateFromUserProfileLanguages. */
typedef struct CpOcrStatics CpOcrStatics;
struct CpOcrStaticsVtbl {
  struct CpUnkVtbl base;
  HRESULT(STDMETHODCALLTYPE *get_MaxImageDimension)(CpOcrStatics *, UINT32 *);
  HRESULT(STDMETHODCALLTYPE *get_AvailableRecognizerLanguages)(CpOcrStatics *, CpUnk **);
  void *IsLanguageSupported;
  HRESULT(STDMETHODCALLTYPE *TryCreateFromLanguage)(CpOcrStatics *, CpUnk *, CpUnk **);
  HRESULT(STDMETHODCALLTYPE *TryCreateFromUserProfileLanguages)(CpOcrStatics *, CpUnk **);
};
struct CpOcrStatics {
  struct CpOcrStaticsVtbl *v;
};

typedef struct CpOcrEngine CpOcrEngine;
struct CpOcrEngineVtbl {
  struct CpUnkVtbl base;
  HRESULT(STDMETHODCALLTYPE *RecognizeAsync)(CpOcrEngine *, CpUnk *, CpUnk **);
  void *get_RecognizerLanguage;
};
struct CpOcrEngine {
  struct CpOcrEngineVtbl *v;
};

typedef struct CpOcrResult CpOcrResult;
struct CpOcrResultVtbl {
  struct CpUnkVtbl base;
  HRESULT(STDMETHODCALLTYPE *get_Lines)(CpOcrResult *, CpUnk **);
  void *get_TextAngle;
  HRESULT(STDMETHODCALLTYPE *get_Text)(CpOcrResult *, HSTRING *);
};
struct CpOcrResult {
  struct CpOcrResultVtbl *v;
};

typedef struct CpOcrLine CpOcrLine;
struct CpOcrLineVtbl {
  struct CpUnkVtbl base;
  void *get_Words;
  HRESULT(STDMETHODCALLTYPE *get_Text)(CpOcrLine *, HSTRING *);
};
struct CpOcrLine {
  struct CpOcrLineVtbl *v;
};

/* IVectorView<T> and IAsyncOperation<T> share one layout for every T, so the
   pointer we are handed can be used directly without querying an interface. */
typedef struct CpVectorView CpVectorView;
struct CpVectorViewVtbl {
  struct CpUnkVtbl base;
  HRESULT(STDMETHODCALLTYPE *GetAt)(CpVectorView *, UINT32, CpUnk **);
  HRESULT(STDMETHODCALLTYPE *get_Size)(CpVectorView *, UINT32 *);
  void *IndexOf, *GetMany;
};
struct CpVectorView {
  struct CpVectorViewVtbl *v;
};

typedef struct CpAsyncOp CpAsyncOp;
struct CpAsyncOpVtbl {
  struct CpUnkVtbl base;
  void *put_Completed, *get_Completed;
  HRESULT(STDMETHODCALLTYPE *GetResults)(CpAsyncOp *, CpUnk **);
};
struct CpAsyncOp {
  struct CpAsyncOpVtbl *v;
};

typedef struct CpAsyncInfo CpAsyncInfo;
struct CpAsyncInfoVtbl {
  struct CpUnkVtbl base;
  void *get_Id;
  HRESULT(STDMETHODCALLTYPE *get_Status)(CpAsyncInfo *, int *);
  void *get_ErrorCode, *Cancel;
  HRESULT(STDMETHODCALLTYPE *Close)(CpAsyncInfo *);
};
struct CpAsyncInfo {
  struct CpAsyncInfoVtbl *v;
};

static const GUID kIID_AsyncInfo = {0x00000036, 0x0000, 0x0000,
                                    {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kIID_BufStatics = {0x320b7e22, 0x3cb0, 0x4cdf,
                                     {0x86, 0x63, 0x1d, 0x28, 0x91, 0x00, 0x65, 0xeb}};
static const GUID kIID_LangFactory = {0x9b0252ac, 0x0c27, 0x44f8,
                                      {0xb7, 0x92, 0x97, 0x93, 0xfb, 0x66, 0xc6, 0x3e}};
static const GUID kIID_BmpStatics = {0xdf0385db, 0x672f, 0x4a9d,
                                     {0x80, 0x6e, 0xc2, 0x44, 0x2f, 0x34, 0x3e, 0x86}};
static const GUID kIID_OcrStatics = {0x5bffa85a, 0x3384, 0x3540,
                                     {0x99, 0x40, 0x69, 0x91, 0x20, 0xd4, 0x28, 0xa8}};

/* ---- picture in ---------------------------------------------------------- */

typedef struct {
  int w, h;
  BYTE *bgra; /* top-down BGRA8 */
} Pix;

static BOOL load_bmp24(const wchar_t *path, Pix *out) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  BITMAPFILEHEADER fh;
  BITMAPINFOHEADER ih;
  DWORD got = 0;
  if (!ReadFile(f, &fh, sizeof(fh), &got, NULL) || got != sizeof(fh) ||
      !ReadFile(f, &ih, sizeof(ih), &got, NULL) || got != sizeof(ih) ||
      fh.bfType != 0x4D42 || ih.biBitCount != 24 || ih.biWidth <= 0 || ih.biHeight == 0) {
    CloseHandle(f);
    return FALSE;
  }
  int w = ih.biWidth, h = ih.biHeight < 0 ? -ih.biHeight : ih.biHeight;
  BOOL bottom_up = ih.biHeight > 0;
  int row = (w * 3 + 3) & ~3;
  BYTE *raw = (BYTE *)malloc((size_t)row * h);
  if (!raw) {
    CloseHandle(f);
    return FALSE;
  }
  SetFilePointer(f, (LONG)fh.bfOffBits, NULL, FILE_BEGIN);
  BOOL ok = ReadFile(f, raw, (DWORD)((size_t)row * h), &got, NULL) && got == (DWORD)((size_t)row * h);
  CloseHandle(f);
  if (!ok) {
    free(raw);
    return FALSE;
  }
  out->w = w;
  out->h = h;
  out->bgra = (BYTE *)malloc((size_t)w * h * 4);
  if (!out->bgra) {
    free(raw);
    return FALSE;
  }
  for (int y = 0; y < h; y++) {
    const BYTE *src = raw + (size_t)(bottom_up ? (h - 1 - y) : y) * row;
    BYTE *dst = out->bgra + (size_t)y * w * 4;
    for (int x = 0; x < w; x++) {
      dst[x * 4 + 0] = src[x * 3 + 0];
      dst[x * 4 + 1] = src[x * 3 + 1];
      dst[x * 4 + 2] = src[x * 3 + 2];
      dst[x * 4 + 3] = 255;
    }
  }
  free(raw);
  return TRUE;
}

/* ---- тёмный фон и картинки ---------------------------------------------------
   Tesseract и Windows OCR ждут тёмный текст на светлом. На тёмной теме текст
   светлый — кадр переворачивается. А обложки, аватарки и большие значки
   (плейлисты, чаты, списки файлов) они читают как буквы: из пёстрой картинки
   выходят строки вроде «‚©. e=». Такие картинки закрашиваются цветом фона до
   распознавания; что просочится — отсеет ocr_lines.h по уверенности. */

#define PIC_B 4     /* блок сетки, пикселей */
#define PIC_MIN 28  /* картинка — не меньше стольких пикселей с каждой стороны */
#define PIC_MAX 400 /* крупнее — часть самого снимка: на ней может быть настоящий текст */
#define PIC_RECTS 64

static int px_gray(const BYTE *s) { return (s[0] * 29 + s[1] * 150 + s[2] * 77) >> 8; }

static int px_sat(const BYTE *s) {
  int mx = s[0] > s[1] ? s[0] : s[1], mn = s[0] < s[1] ? s[0] : s[1];
  if (s[2] > mx) mx = s[2];
  if (s[2] < mn) mn = s[2];
  return mx - mn;
}

static int px_bucket(const BYTE *s) { return ((s[2] >> 4) << 8) | ((s[1] >> 4) << 4) | (s[0] >> 4); }

/* Фон — самый частый цвет кадра (по 4 бита на канал), берётся его среднее. */
static void dominant_bg(const Pix *p, BYTE bg[4]) {
  static unsigned cnt[4096];
  static unsigned long long sum[4096][3];
  memset(cnt, 0, sizeof(cnt));
  memset(sum, 0, sizeof(sum));
  for (size_t i = 0; i < (size_t)p->w * p->h; i++) {
    const BYTE *s = p->bgra + i * 4;
    int k = px_bucket(s);
    cnt[k]++;
    for (int c = 0; c < 3; c++) sum[k][c] += s[c];
  }
  int best = 0;
  for (int k = 1; k < 4096; k++)
    if (cnt[k] > cnt[best]) best = k;
  for (int c = 0; c < 3; c++) bg[c] = cnt[best] ? (BYTE)(sum[best][c] / cnt[best]) : 0;
  bg[3] = 255;
}

typedef struct {
  int x0, y0, x1, y1; /* [x0, x1) × [y0, y1) */
} PicRect;

/* Ищет почти квадратные пёстрые области — обложки, аватарки — и закрашивает их в g
   (серое кадра) цветом фона. Сетка из блоков PIC_B×PIC_B: блок «картинный», если
   почти все его пиксели далеки от фона или в среднем сильно насыщены (цветной
   текст и ClearType-кайма сюда не попадают: у них строки штрихов с просветами).
   Связная область блоков берётся, только если она не меньше PIC_MIN, не больше
   PIC_MAX, почти квадратная и почти сплошная, не одноцветная плашка (там
   обычно подпись) и заметно цветная. Возвращает, сколько картинок закрашено. */
static int paint_pictures(const Pix *p, BYTE *g, const BYTE bg[4]) {
  int bw = (p->w + PIC_B - 1) / PIC_B, bh = (p->h + PIC_B - 1) / PIC_B;
  if (p->w < PIC_MIN || p->h < PIC_MIN) return 0;
  size_t nb = (size_t)bw * bh;
  BYTE *mk = (BYTE *)calloc(nb, 1), *dl = (BYTE *)calloc(nb, 1);
  int *st = (int *)malloc(nb * sizeof(int));
  PicRect rc[PIC_RECTS];
  int nrc = 0;
  long long area = 0;
  if (!mk || !dl || !st) {
    free(mk);
    free(dl);
    free(st);
    return 0;
  }

  for (int by = 0; by < bh; by++)
    for (int bx = 0; bx < bw; bx++) {
      int x1 = (bx + 1) * PIC_B < p->w ? (bx + 1) * PIC_B : p->w;
      int y1 = (by + 1) * PIC_B < p->h ? (by + 1) * PIC_B : p->h;
      int n = 0, away = 0, sat = 0;
      for (int y = by * PIC_B; y < y1; y++)
        for (int x = bx * PIC_B; x < x1; x++) {
          const BYTE *s = p->bgra + ((size_t)y * p->w + x) * 4;
          int d0 = abs(s[0] - bg[0]), d1 = abs(s[1] - bg[1]), d2 = abs(s[2] - bg[2]);
          if (d0 > 18 || d1 > 18 || d2 > 18) away++;
          sat += px_sat(s);
          n++;
        }
      if (away * 10 >= n * 9 || sat >= n * 55) mk[(size_t)by * bw + bx] = 1;
    }
  /* зазор в один блок не разрывает картинку: в обложке бывают места, близкие к фону */
  for (int by = 0; by < bh; by++)
    for (int bx = 0; bx < bw; bx++)
      if (mk[(size_t)by * bw + bx])
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int yy = by + dy, xx = bx + dx;
            if (yy >= 0 && yy < bh && xx >= 0 && xx < bw) dl[(size_t)yy * bw + xx] = 1;
          }

  for (int s0 = 0; s0 < (int)nb; s0++) {
    if (!dl[s0]) continue;
    int top = 0, cnt = 0, mx0 = bw, my0 = bh, mx1 = -1, my1 = -1;
    st[top++] = s0;
    dl[s0] = 0;
    while (top) {
      int i = st[--top], x = i % bw, y = i / bw;
      if (mk[i]) {
        cnt++;
        if (x < mx0) mx0 = x;
        if (x > mx1) mx1 = x;
        if (y < my0) my0 = y;
        if (y > my1) my1 = y;
      }
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          int yy = y + dy, xx = x + dx;
          if (yy < 0 || yy >= bh || xx < 0 || xx >= bw || !dl[(size_t)yy * bw + xx]) continue;
          dl[(size_t)yy * bw + xx] = 0;
          st[top++] = yy * bw + xx;
        }
    }
    if (cnt == 0) continue;
    int pw = (mx1 - mx0 + 1) * PIC_B, ph = (my1 - my0 + 1) * PIC_B;
    if (pw < PIC_MIN || ph < PIC_MIN || pw > PIC_MAX || ph > PIC_MAX) continue;
    if (pw * 10 > ph * 18 || ph * 10 > pw * 18) continue; /* вытянутая — подсветка, строка */
    if (cnt * 100 < (mx1 - mx0 + 1) * (my1 - my0 + 1) * 45) continue; /* сплошь просветы — не картинка */

    /* внутри: не плашка одного цвета и не серая */
    static long hist[4096];
    memset(hist, 0, sizeof(hist));
    int x0 = mx0 * PIC_B, y0 = my0 * PIC_B;
    int x1 = (mx1 + 1) * PIC_B < p->w ? (mx1 + 1) * PIC_B : p->w;
    int y1 = (my1 + 1) * PIC_B < p->h ? (my1 + 1) * PIC_B : p->h;
    long tot = 0, colourful = 0, top1 = 0;
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) {
        const BYTE *s = p->bgra + ((size_t)y * p->w + x) * 4;
        int k = px_bucket(s);
        if (++hist[k] > top1) top1 = hist[k];
        if (px_sat(s) >= 30) colourful++;
        tot++;
      }
    if (top1 * 100 >= tot * 75 || colourful * 100 < tot * 25) continue;

    if (nrc < PIC_RECTS) {
      rc[nrc].x0 = x0 > 2 ? x0 - 2 : 0;
      rc[nrc].y0 = y0 > 2 ? y0 - 2 : 0;
      rc[nrc].x1 = x1 + 2 < p->w ? x1 + 2 : p->w;
      rc[nrc].y1 = y1 + 2 < p->h ? y1 + 2 : p->h;
      area += (long long)(rc[nrc].x1 - rc[nrc].x0) * (rc[nrc].y1 - rc[nrc].y0);
      nrc++;
    }
  }
  free(mk);
  free(dl);
  free(st);

  /* больше половины кадра «картинки» — это не обложки, а сам снимок: не трогаем */
  if (area * 2 > (long long)p->w * p->h) return 0;
  int bgg = px_gray(bg);
  for (int r = 0; r < nrc; r++)
    for (int y = rc[r].y0; y < rc[r].y1; y++)
      memset(g + (size_t)y * p->w + rc[r].x0, bgg, (size_t)(rc[r].x1 - rc[r].x0));
  return nrc;
}

/* Screen text recognises far better after a clean upscale and a contrast
   stretch than it does at native resolution. Тёмный кадр (медиана яркости ниже
   100, фон тёмный) переворачивается, картинки закрашиваются фоном — см. выше. */
static BOOL enhance(Pix *p, int scale) {
  int w = p->w * scale, h = p->h * scale;
  if ((long long)w * h > 40000000LL) { /* already big enough: без увеличения */
    scale = 1;
    w = p->w;
    h = p->h;
  }
  BYTE *dst = (BYTE *)malloc((size_t)w * h * 4);
  size_t np = (size_t)p->w * p->h;
  BYTE *gray = (BYTE *)malloc(np);
  if (!dst || !gray) {
    free(dst);
    free(gray);
    return FALSE;
  }

  unsigned hist[256];
  memset(hist, 0, sizeof(hist));
  for (size_t i = 0; i < np; i++) {
    gray[i] = (BYTE)px_gray(p->bgra + i * 4);
    hist[gray[i]]++;
  }
  int med = 0;
  for (size_t acc = 0; med < 255 && (acc += hist[med]) * 2 < np; med++) {}
  BYTE bg[4];
  dominant_bg(p, bg);
  int dark = med < 100 && px_gray(bg) < 128;
  paint_pictures(p, gray, bg);
  if (dark)
    for (size_t i = 0; i < np; i++) gray[i] = (BYTE)(255 - gray[i]);

  int lo = 255, hi = 0;
  for (size_t i = 0; i < np; i++) {
    int g = gray[i];
    if (g < lo) lo = g;
    if (g > hi) hi = g;
  }
  int span = hi - lo;
  if (span < 24) span = 24;

  for (int y = 0; y < h; y++) {
    /* bilinear: nearest-neighbour leaves jagged strokes that OCR reads worse */
    float sy = (y + 0.5f) / scale - 0.5f;
    int y0 = (int)sy;
    if (y0 < 0) y0 = 0;
    int y1 = y0 + 1 < p->h ? y0 + 1 : y0;
    float fy = sy - y0;
    if (fy < 0) fy = 0;
    for (int x = 0; x < w; x++) {
      float sx = (x + 0.5f) / scale - 0.5f;
      int x0 = (int)sx;
      if (x0 < 0) x0 = 0;
      int x1 = x0 + 1 < p->w ? x0 + 1 : x0;
      float fx = sx - x0;
      if (fx < 0) fx = 0;
      float acc = 0;
      const int xs[2] = {x0, x1}, ys[2] = {y0, y1};
      const float wx[2] = {1 - fx, fx}, wy[2] = {1 - fy, fy};
      for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) acc += gray[(size_t)ys[j] * p->w + xs[i]] * wx[i] * wy[j];
      int g = (int)((acc - lo) * 255.0f / span + 0.5f);
      if (g < 0) g = 0;
      if (g > 255) g = 255;
      BYTE *d = dst + ((size_t)y * w + x) * 4;
      d[0] = d[1] = d[2] = (BYTE)g;
      d[3] = 255;
    }
  }
  free(gray);
  free(p->bgra);
  p->bgra = dst;
  p->w = w;
  p->h = h;
  return TRUE;
}

/* Diagnostics: what the recogniser actually sees after preparation. */
static void save_gray_bmp(const wchar_t *path, const Pix *p) {
  int row = (p->w * 3 + 3) & ~3;
  BYTE *out = (BYTE *)calloc((size_t)row * p->h, 1);
  if (!out) return;
  for (int y = 0; y < p->h; y++) {
    const BYTE *src = p->bgra + (size_t)y * p->w * 4;
    BYTE *dst = out + (size_t)(p->h - 1 - y) * row;
    for (int x = 0; x < p->w; x++) {
      dst[x * 3 + 0] = src[x * 4 + 0];
      dst[x * 3 + 1] = src[x * 4 + 1];
      dst[x * 3 + 2] = src[x * 4 + 2];
    }
  }
  BITMAPFILEHEADER fh;
  BITMAPINFOHEADER ih;
  memset(&fh, 0, sizeof(fh));
  memset(&ih, 0, sizeof(ih));
  ih.biSize = sizeof(ih);
  ih.biWidth = p->w;
  ih.biHeight = p->h;
  ih.biPlanes = 1;
  ih.biBitCount = 24;
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(ih);
  fh.bfSize = fh.bfOffBits + (DWORD)((size_t)row * p->h);
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD w = 0;
    WriteFile(f, &fh, sizeof(fh), &w, NULL);
    WriteFile(f, &ih, sizeof(ih), &w, NULL);
    WriteFile(f, out, (DWORD)((size_t)row * p->h), &w, NULL);
    CloseHandle(f);
  }
  free(out);
}

/* ---- text out ------------------------------------------------------------ */

static void emit_utf8(const wchar_t *ws, UINT32 len) {
  if (!ws || !len) return;
  int n = WideCharToMultiByte(CP_UTF8, 0, ws, (int)len, NULL, 0, NULL, NULL);
  if (n <= 0) return;
  char *b = (char *)malloc((size_t)n);
  if (!b) return;
  WideCharToMultiByte(CP_UTF8, 0, ws, (int)len, b, n, NULL, NULL);
  DWORD wrote = 0;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), b, (DWORD)n, &wrote, NULL);
  free(b);
}

static void emit_line_break(void) {
  DWORD wrote = 0;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\r\n", 2, &wrote, NULL);
}

static void note(const char *msg) {
  DWORD wrote = 0;
  WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)strlen(msg), &wrote, NULL);
}

static HRESULT factory(const wchar_t *cls, const GUID *iid, void **out) {
  HSTRING h = NULL;
  HSTRING_HEADER hdr;
  HRESULT hr = WindowsCreateStringReference(cls, (UINT32)wcslen(cls), &hdr, &h);
  if (FAILED(hr)) return hr;
  return RoGetActivationFactory(h, iid, out);
}

/* ---- the chain ----------------------------------------------------------- */

static CpUnk *make_bitmap(const Pix *p) {
  CpBufStatics *bufs = NULL;
  CpBmpStatics *bmps = NULL;
  CpUnk *buf = NULL, *bmp = NULL;
  if (FAILED(factory(L"Windows.Security.Cryptography.CryptographicBuffer", &kIID_BufStatics,
                     (void **)&bufs)))
    return NULL;
  if (SUCCEEDED(bufs->v->CreateFromByteArray(bufs, (UINT32)((size_t)p->w * p->h * 4), p->bgra,
                                             &buf)) &&
      SUCCEEDED(factory(L"Windows.Graphics.Imaging.SoftwareBitmap", &kIID_BmpStatics,
                        (void **)&bmps))) {
    if (FAILED(bmps->v->CreateCopyFromBuffer(bmps, buf, BITMAP_PIXEL_FORMAT_BGRA8, p->w, p->h,
                                             &bmp)))
      bmp = NULL;
  }
  CP_RELEASE(buf);
  CP_RELEASE(bufs);
  CP_RELEASE(bmps);
  return bmp;
}

static CpOcrEngine *make_engine(const wchar_t *lang, int *have_any, int *no_component) {
  CpOcrStatics *st = NULL;
  CpOcrEngine *eng = NULL;
  *have_any = 0;
  *no_component = 0;
  if (FAILED(factory(L"Windows.Media.Ocr.OcrEngine", &kIID_OcrStatics, (void **)&st))) {
    *no_component = 1;
    return NULL;
  }

  /* knowing whether any language pack exists lets the caller say why, instead
     of the blank "не распознано" the old script produced */
  CpUnk *langs = NULL;
  if (SUCCEEDED(st->v->get_AvailableRecognizerLanguages(st, &langs)) && langs) {
    UINT32 n = 0;
    CpVectorView *lv = (CpVectorView *)langs;
    if (SUCCEEDED(lv->v->get_Size(lv, &n))) *have_any = n > 0;
    CP_RELEASE(langs);
  }

  if (lang && lang[0]) {
    CpLangFactory *lf = NULL;
    if (SUCCEEDED(factory(L"Windows.Globalization.Language", &kIID_LangFactory, (void **)&lf))) {
      HSTRING tag = NULL;
      HSTRING_HEADER hdr;
      CpUnk *lobj = NULL;
      if (SUCCEEDED(WindowsCreateStringReference(lang, (UINT32)wcslen(lang), &hdr, &tag)) &&
          SUCCEEDED(lf->v->CreateLanguage(lf, tag, &lobj)) && lobj) {
        CpUnk *e = NULL;
        if (SUCCEEDED(st->v->TryCreateFromLanguage(st, lobj, &e))) eng = (CpOcrEngine *)e;
        CP_RELEASE(lobj);
      }
      CP_RELEASE(lf);
    }
  }
  if (!eng) {
    CpUnk *e = NULL;
    if (SUCCEEDED(st->v->TryCreateFromUserProfileLanguages(st, &e))) eng = (CpOcrEngine *)e;
  }
  CP_RELEASE(st);
  return eng;
}

static CpOcrResult *recognise(CpOcrEngine *eng, CpUnk *bmp) {
  CpUnk *op = NULL;
  if (FAILED(eng->v->RecognizeAsync(eng, bmp, &op)) || !op) return NULL;
  CpAsyncInfo *info = NULL;
  if (FAILED(((CpUnk *)op)->v->QueryInterface(op, &kIID_AsyncInfo, (void **)&info))) {
    CP_RELEASE(op);
    return NULL;
  }
  int status = AsyncStatus_Started;
  for (int i = 0; i < 1200; i++) { /* up to ~30 s, polled */
    if (FAILED(info->v->get_Status(info, &status))) break;
    if (status != AsyncStatus_Started) break;
    Sleep(25);
  }
  CpOcrResult *res = NULL;
  if (status == AsyncStatus_Completed) {
    CpUnk *r = NULL;
    if (SUCCEEDED(((CpAsyncOp *)op)->v->GetResults((CpAsyncOp *)op, &r))) res = (CpOcrResult *)r;
  }
  info->v->Close(info);
  CP_RELEASE(info);
  CP_RELEASE(op);
  return res;
}

static int emit_result(CpOcrResult *res) {
  CpUnk *lines = NULL;
  int printed = 0;
  if (SUCCEEDED(res->v->get_Lines(res, &lines)) && lines) {
    CpVectorView *v = (CpVectorView *)lines;
    UINT32 n = 0;
    v->v->get_Size(v, &n);
    for (UINT32 i = 0; i < n; i++) {
      CpUnk *ln = NULL;
      if (FAILED(v->v->GetAt(v, i, &ln)) || !ln) continue;
      HSTRING t = NULL;
      if (SUCCEEDED(((CpOcrLine *)ln)->v->get_Text((CpOcrLine *)ln, &t)) && t) {
        UINT32 len = 0;
        const wchar_t *s = WindowsGetStringRawBuffer(t, &len);
        if (len) {
          if (printed) emit_line_break();
          emit_utf8(s, len);
          printed++;
        }
        WindowsDeleteString(t);
      }
      CP_RELEASE(ln);
    }
    CP_RELEASE(lines);
  }
  if (!printed) { /* fall back to the flat text property */
    HSTRING t = NULL;
    if (SUCCEEDED(res->v->get_Text(res, &t)) && t) {
      UINT32 len = 0;
      const wchar_t *s = WindowsGetStringRawBuffer(t, &len);
      if (len) {
        emit_utf8(s, len);
        printed = 1;
      }
      WindowsDeleteString(t);
    }
  }
  return printed;
}

/* --prep: only the preparation step, for Tesseract. It reads screen text as
   badly at native size as Windows OCR does, so it gets the same picture. */
static int prep_only(const wchar_t *in, const wchar_t *out) {
  Pix pix;
  memset(&pix, 0, sizeof(pix));
  if (!load_bmp24(in, &pix)) {
    note("cannot read bitmap\n");
    return 1;
  }
  int ok = enhance(&pix, pix.w < 700 ? 3 : 2);
  if (ok) save_gray_bmp(out, &pix);
  free(pix.bgra);
  if (!ok) note("out of memory\n");
  return ok ? 0 : 1;
}

#include "pdf_pages.c" /* --pdf: страницы PDF для конвертера файлов */

int wmain(int argc, wchar_t **argv) {
  if (argc == 4 && !wcscmp(argv[1], L"--prep")) return prep_only(argv[2], argv[3]);
  if (argc == 5 && !wcscmp(argv[1], L"--pdf")) return pdf_pages(argv[2], argv[3], _wtoi(argv[4]));
  if (argc < 2) {
    note("usage: CursorPadOcr.exe <bmp> [lang] | --prep <in.bmp> <out.bmp> | --pdf <in.pdf> <folder> <dpi>\n");
    return 1;
  }
  const wchar_t *lang = argc > 2 ? argv[2] : L"ru";

  Pix pix;
  memset(&pix, 0, sizeof(pix));
  if (!load_bmp24(argv[1], &pix)) {
    note("cannot read bitmap\n");
    return 1;
  }
  int scale = pix.w < 700 ? 3 : 2;
  const wchar_t *dump = argc > 3 ? argv[3] : NULL;
  if (!enhance(&pix, scale)) {
    free(pix.bgra);
    note("out of memory\n");
    return 1;
  }

  if (dump) save_gray_bmp(dump, &pix);

  HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
  if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
    free(pix.bgra);
    note("windows runtime unavailable\n");
    return 1;
  }

  int rc = 1, have_any = 0, no_component = 0;
  CpOcrEngine *eng = make_engine(lang, &have_any, &no_component);
  if (!eng) {
    /* the caller turns each of these into a sentence the user can act on */
    note(no_component ? "ocr component missing\n"
                      : (have_any ? "no engine for this language\n"
                                  : "no recognition languages installed\n"));
    rc = 2;
  } else {
    CpUnk *bmp = make_bitmap(&pix);
    if (!bmp) {
      note("cannot build bitmap\n");
      rc = 1;
    } else {
      CpOcrResult *res = recognise(eng, bmp);
      if (!res) {
        note("recognition failed\n");
        rc = 1;
      } else {
        rc = emit_result(res) ? 0 : 3;
        CP_RELEASE(res);
      }
      CP_RELEASE(bmp);
    }
    CP_RELEASE(eng);
  }
  free(pix.bgra);
  RoUninitialize();
  return rc;
}
