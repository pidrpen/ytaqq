/* CursorPad OCR companion — picture in, text out.

   Deliberately a separate process: recognition reaches into system codecs and
   graphics, which behave differently from machine to machine. Today that work
   happens inside PowerShell, so a crash there never touches the notepad. This
   keeps that isolation while dropping the PowerShell dependency, which
   corporate policy often restricts.

   Called as:  CursorPadOcr.exe <bmp-path> [lang]
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

/* Screen text recognises far better after a clean upscale and a contrast
   stretch than it does at native resolution. */
static BOOL enhance(Pix *p, int scale) {
  int w = p->w * scale, h = p->h * scale;
  if ((long long)w * h > 40000000LL) return TRUE; /* already big enough */
  BYTE *dst = (BYTE *)malloc((size_t)w * h * 4);
  if (!dst) return FALSE;

  int lo = 255, hi = 0;
  for (int i = 0; i < p->w * p->h; i++) {
    const BYTE *s = p->bgra + (size_t)i * 4;
    int g = (s[0] * 29 + s[1] * 150 + s[2] * 77) >> 8;
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
        for (int i = 0; i < 2; i++) {
          const BYTE *s = p->bgra + ((size_t)ys[j] * p->w + xs[i]) * 4;
          int g = (s[0] * 29 + s[1] * 150 + s[2] * 77) >> 8;
          acc += g * wx[i] * wy[j];
        }
      int g = (int)((acc - lo) * 255.0f / span + 0.5f);
      if (g < 0) g = 0;
      if (g > 255) g = 255;
      BYTE *d = dst + ((size_t)y * w + x) * 4;
      d[0] = d[1] = d[2] = (BYTE)g;
      d[3] = 255;
    }
  }
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

int wmain(int argc, wchar_t **argv) {
  if (argc < 2) {
    note("usage: CursorPadOcr.exe <bmp> [lang]\n");
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
