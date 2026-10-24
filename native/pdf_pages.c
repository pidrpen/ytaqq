/* CursorPadOcr.exe --pdf <in.pdf> <папка> <dpi> — страницы PDF в PNG.

   Рисует встроенный в Windows 10/11 Windows.Data.Pdf: своих библиотек нет,
   а отдельный процесс (как и у распознавания) не даёт сбою отрисовки
   задеть CursorPad. Файл берётся потоком CreateRandomAccessStreamOnFile из
   shcore.dll, без StorageFile. Подключается из ocr_module.c: интерфейсы
   CpUnk / CpAsyncOp / CpAsyncInfo и factory() — оттуда.

   Пишет p0001.png, p0002.png… и на каждую страницу строку stdout
   «p0001.png 300» — имя и фактический dpi: огромный лист уменьшается до
   CP_PDF_MAX_PIXELS, чтобы отрисовка не съела память (A0 при 300 dpi —
   139 Мп, выйдет ~250 dpi). Коды: 0 — готово, 1 — ошибка (причина в stderr).
   Порядок методов и IID — из windows.data.pdf.idl / windows.storage.streams.idl
   (Windows SDK 10.0.26100). */

#include <math.h>

#define CP_PDF_MAX_PIXELS 100000000.0

typedef struct { float Width, Height; } CpSize;

typedef struct CpPdfStatics CpPdfStatics;
struct CpPdfStaticsVtbl {
  struct CpUnkVtbl base;
  void *LoadFromFileAsync, *LoadFromFileWithPasswordAsync;
  HRESULT(STDMETHODCALLTYPE *LoadFromStreamAsync)(CpPdfStatics *, CpUnk *, CpUnk **);
  void *LoadFromStreamWithPasswordAsync;
};
struct CpPdfStatics {
  struct CpPdfStaticsVtbl *v;
};

typedef struct CpPdfDoc CpPdfDoc;
struct CpPdfDocVtbl {
  struct CpUnkVtbl base;
  HRESULT(STDMETHODCALLTYPE *GetPage)(CpPdfDoc *, UINT32, CpUnk **);
  HRESULT(STDMETHODCALLTYPE *get_PageCount)(CpPdfDoc *, UINT32 *);
  void *get_IsPasswordProtected;
};
struct CpPdfDoc {
  struct CpPdfDocVtbl *v;
};

typedef struct CpPdfPage CpPdfPage;
struct CpPdfPageVtbl {
  struct CpUnkVtbl base;
  void *RenderToStreamAsync;
  HRESULT(STDMETHODCALLTYPE *RenderWithOptionsToStreamAsync)(CpPdfPage *, CpUnk *, CpUnk *, CpUnk **);
  void *PreparePageAsync, *get_Index;
  HRESULT(STDMETHODCALLTYPE *get_Size)(CpPdfPage *, CpSize *); /* в DIP, 1/96 дюйма, с учётом поворота */
  void *get_Dimensions, *get_Rotation, *get_PreferredZoom;
};
struct CpPdfPage {
  struct CpPdfPageVtbl *v;
};

typedef struct CpPdfOptions CpPdfOptions;
struct CpPdfOptionsVtbl {
  struct CpUnkVtbl base;
  void *get_SourceRect, *put_SourceRect, *get_DestinationWidth;
  HRESULT(STDMETHODCALLTYPE *put_DestinationWidth)(CpPdfOptions *, UINT32);
  void *get_DestinationHeight;
  HRESULT(STDMETHODCALLTYPE *put_DestinationHeight)(CpPdfOptions *, UINT32);
  void *get_BackgroundColor;
  /* Windows.UI.Color {A, R, G, B} — 4 байта по значению, на x64 тем же регистром, что UINT32 */
  HRESULT(STDMETHODCALLTYPE *put_BackgroundColor)(CpPdfOptions *, UINT32);
  void *get_IsIgnoringHighContrast;
  HRESULT(STDMETHODCALLTYPE *put_IsIgnoringHighContrast)(CpPdfOptions *, BOOLEAN);
  void *get_BitmapEncoderId, *put_BitmapEncoderId;
};
struct CpPdfOptions {
  struct CpPdfOptionsVtbl *v;
};

/* IAsyncAction::GetResults без результата; IClosable::Close */
typedef struct CpAsyncAction CpAsyncAction;
struct CpAsyncActionVtbl {
  struct CpUnkVtbl base;
  void *put_Completed, *get_Completed;
  HRESULT(STDMETHODCALLTYPE *GetResults)(CpAsyncAction *);
};
struct CpAsyncAction {
  struct CpAsyncActionVtbl *v;
};
typedef struct CpClosable CpClosable;
struct CpClosableVtbl {
  struct CpUnkVtbl base;
  HRESULT(STDMETHODCALLTYPE *Close)(CpClosable *);
};
struct CpClosable {
  struct CpClosableVtbl *v;
};

static const GUID kIID_PdfStatics = {0x433a0b5f, 0xc007, 0x4788, {0x90, 0xf2, 0x08, 0x14, 0x3d, 0x92, 0x25, 0x99}};
static const GUID kIID_PdfOptions = {0x3c98056f, 0xb7cf, 0x4c29, {0x9a, 0x04, 0x52, 0xd9, 0x02, 0x67, 0xf4, 0x25}};
static const GUID kIID_RandomStream = {0x905a0fe1, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};
static const GUID kIID_Closable = {0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e}};

/* дождаться асинхронной операции; result != NULL — IAsyncOperation<T>, иначе IAsyncAction */
static HRESULT cp_finish(CpUnk *op, DWORD ms, CpUnk **result) {
  CpAsyncInfo *info = NULL;
  if (!op || FAILED(op->v->QueryInterface(op, &kIID_AsyncInfo, (void **)&info))) return E_FAIL;
  int status = AsyncStatus_Started;
  for (DWORD waited = 0; waited < ms; waited += 10) {
    if (FAILED(info->v->get_Status(info, &status)) || status != AsyncStatus_Started) break;
    Sleep(10);
  }
  HRESULT hr = E_FAIL;
  if (status == AsyncStatus_Completed)
    hr = result ? ((CpAsyncOp *)op)->v->GetResults((CpAsyncOp *)op, result) : ((CpAsyncAction *)op)->v->GetResults((CpAsyncAction *)op);
  info->v->Close(info);
  CP_RELEASE(info);
  return hr;
}

typedef HRESULT(WINAPI *CpStreamOnFile)(PCWSTR, DWORD, REFIID, void **);

/* одна страница в файл PNG; *dpi уменьшается, если лист огромный */
static BOOL cp_pdf_page(CpPdfDoc *doc, UINT32 index, CpPdfOptions *options, CpStreamOnFile open, const wchar_t *path, double *dpi) {
  CpPdfPage *page = NULL;
  CpUnk *out = NULL, *op = NULL;
  BOOL ok = FALSE;
  CpSize size = {0, 0};
  if (FAILED(doc->v->GetPage(doc, index, (CpUnk **)&page)) || !page || FAILED(page->v->get_Size(page, &size)) ||
      size.Width <= 0 || size.Height <= 0)
    goto done;
  double scale = *dpi / 96.0, w = size.Width * scale, h = size.Height * scale;
  if (w * h > CP_PDF_MAX_PIXELS) {
    double k = sqrt(CP_PDF_MAX_PIXELS / (w * h));
    w *= k, h *= k, *dpi *= k;
  }
  options->v->put_DestinationWidth(options, (UINT32)(w + 0.5));
  options->v->put_DestinationHeight(options, (UINT32)(h + 0.5));
  /* поток поверх пустого файла: CREATE_NEW — чужой файл не перезаписывается */
  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) goto done;
  CloseHandle(file);
  if (FAILED(open(path, 1 /* FileAccessMode.ReadWrite */, &kIID_RandomStream, (void **)&out)) || !out) goto done;
  if (SUCCEEDED(page->v->RenderWithOptionsToStreamAsync(page, out, (CpUnk *)options, &op)))
    ok = SUCCEEDED(cp_finish(op, 300000, NULL));
  CpClosable *close = NULL;
  if (SUCCEEDED(out->v->QueryInterface(out, &kIID_Closable, (void **)&close))) {
    close->v->Close(close);
    CP_RELEASE(close);
  }
done:
  CP_RELEASE(op);
  CP_RELEASE(out);
  CP_RELEASE(page);
  if (!ok) DeleteFileW(path);
  return ok;
}

static int pdf_pages(const wchar_t *pdf, const wchar_t *folder, int dpi) {
  if (dpi < 50 || dpi > 1200) {
    note("dpi must be 50..1200\n");
    return 1;
  }
  HMODULE shcore = LoadLibraryW(L"shcore.dll");
  CpStreamOnFile open = shcore ? (CpStreamOnFile)(void *)GetProcAddress(shcore, "CreateRandomAccessStreamOnFile") : NULL;
  if (!open) {
    note("CreateRandomAccessStreamOnFile unavailable\n");
    return 1;
  }
  HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
  if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
    note("windows runtime unavailable\n");
    return 1;
  }
  int rc = 1;
  CpUnk *input = NULL, *op = NULL, *optionsRaw = NULL;
  CpPdfStatics *statics = NULL;
  CpPdfDoc *doc = NULL;
  CpPdfOptions *options = NULL;
  HSTRING name = NULL;
  HSTRING_HEADER header;
  const wchar_t *cls = L"Windows.Data.Pdf.PdfPageRenderOptions";
  UINT32 count = 0;
  if (FAILED(open(pdf, 0 /* FileAccessMode.Read */, &kIID_RandomStream, (void **)&input)) || !input) {
    note("cannot open pdf file\n");
    goto done;
  }
  if (FAILED(factory(L"Windows.Data.Pdf.PdfDocument", &kIID_PdfStatics, (void **)&statics)) ||
      FAILED(statics->v->LoadFromStreamAsync(statics, input, &op)) || FAILED(cp_finish(op, 120000, (CpUnk **)&doc)) || !doc) {
    note("cannot read pdf (damaged or password protected)\n");
    goto done;
  }
  if (FAILED(WindowsCreateStringReference(cls, (UINT32)wcslen(cls), &header, &name)) ||
      FAILED(RoActivateInstance(name, (IInspectable **)&optionsRaw)) || !optionsRaw ||
      FAILED(optionsRaw->v->QueryInterface(optionsRaw, &kIID_PdfOptions, (void **)&options))) {
    note("pdf render options unavailable\n");
    goto done;
  }
  options->v->put_BackgroundColor(options, 0xffffffffu); /* белый лист, а не прозрачный */
  options->v->put_IsIgnoringHighContrast(options, TRUE);  /* контрастная тема Windows не красит чертёж */
  if (FAILED(doc->v->get_PageCount(doc, &count)) || !count) {
    note("pdf has no pages\n");
    goto done;
  }
  rc = 0;
  for (UINT32 i = 0; i < count && !rc; i++) {
    wchar_t path[MAX_PATH];
    double used = dpi;
    if (_snwprintf(path, MAX_PATH, L"%ls\\p%04u.png", folder, i + 1) < 0 || !cp_pdf_page(doc, i, options, open, path, &used)) {
      note("page render failed\n");
      rc = 1;
      break;
    }
    printf("p%04u.png %.3f\n", i + 1, used);
    fflush(stdout);
  }
done:
  CP_RELEASE(options);
  CP_RELEASE(optionsRaw);
  CP_RELEASE(doc);
  CP_RELEASE(op);
  CP_RELEASE(statics);
  CP_RELEASE(input);
  RoUninitialize();
  return rc;
}
