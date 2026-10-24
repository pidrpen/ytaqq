/* Local conversion only. The source is read-only; publish with MoveFile (no
   replacement) from a private, reserved sibling temporary file. */
typedef struct { const wchar_t *ext, *label; } CvFormat;
static const CvFormat kCvFormats[] = {{L"png", L"PNG"}, {L"jpg", L"JPG"}, {L"bmp", L"BMP"},
    {L"tif", L"TIFF"}, {L"pdf", L"PDF"}, {L"txt", L"TXT"}, {L"csv", L"CSV · лист 1"},
    {L"zip",L"ZIP"},{L"png",L"Размер"},{L"jpg",L"Сжать"},{L"pdf",L"Собрать PDF"},
    {L"pdf",L"Страницы"},{L"pdf",L"Разделить"},{L"pdf",L"Повернуть"},{L"pdf",L"PDF-пакет"},{L"",L"Ещё…"}};
typedef struct { wchar_t src[MAX_PATH], out[MAX_PATH], error[400], summary[1600], label[80]; int format; HRESULT hr; } CvJob;

static const wchar_t *cv_extension(const wchar_t *path) {
  const wchar_t *slash = wcsrchr(path, L'\\'), *dot = wcsrchr(path, L'.');
  return dot && (!slash || dot > slash) ? dot + 1 : L"";
}
static int cv_image_type(const wchar_t *path) {
  const wchar_t *e = cv_extension(path);
  if (!_wcsicmp(e, L"png")) return 0;
  if (!_wcsicmp(e, L"jpg") || !_wcsicmp(e, L"jpeg")) return 1;
  if (!_wcsicmp(e, L"bmp")) return 2;
  if (!_wcsicmp(e, L"tif") || !_wcsicmp(e, L"tiff")) return 3;
  return -1;
}
/* что открывает Word / Excel: на заводе много старых .doc и .xls, не только .docx/.xlsx */
static BOOL cv_word_file(const wchar_t *path) {
  static const wchar_t *const ext[] = {L"docx", L"doc", L"docm", L"rtf", L"odt"};
  for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) if (!_wcsicmp(cv_extension(path), ext[i])) return TRUE;
  return FALSE;
}
static BOOL cv_excel_file(const wchar_t *path) {
  static const wchar_t *const ext[] = {L"xlsx", L"xls", L"xlsm", L"xlsb", L"ods"};
  for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) if (!_wcsicmp(cv_extension(path), ext[i])) return TRUE;
  return FALSE;
}
static int cv_formats(const wchar_t *path, BOOL word, BOOL excel, int formats[7]) {
  int image = cv_image_type(path), count = 0;
  if (image >= 0) { for (int i = 0; i < 4; i++) if (i != image) formats[count++] = i; }
  else if (cv_word_file(path) && word) { formats[count++] = 4; formats[count++] = 5; }
  else if (cv_excel_file(path) && excel) { formats[count++] = 4; formats[count++] = 6; }
  return count;
}
static BOOL cv_program(const wchar_t *name) {
  HKEY key; wchar_t path[80]; swprintf(path, 80, L"%ls.Application\\CLSID", name);
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return FALSE;
  RegCloseKey(key); return TRUE;
}
static BOOL cv_image(CvJob *job, const wchar_t *temp) {
  HMODULE dll = LoadLibraryW(L"gdiplus.dll");
  if (!dll) return FALSE;
  typedef struct { UINT version; void *callback; BOOL suppressThread, suppressCodecs; } Start;
  typedef int (WINAPI *Startup)(ULONG_PTR *, const Start *, void *);
  typedef void (WINAPI *Shutdown)(ULONG_PTR);
  typedef int (WINAPI *Load)(const WCHAR *, void **);
  typedef int (WINAPI *Dispose)(void *);
  typedef int (WINAPI *Dimension)(void *, UINT *);
  typedef int (WINAPI *Frames)(void *, const GUID *, UINT *);
  typedef int (WINAPI *Save)(void *, const WCHAR *, const CLSID *, const void *);
  typedef int (WINAPI *Bitmap)(int, int, int, int, BYTE *, void **);
  typedef int (WINAPI *Graphics)(void *, void **);
  typedef int (WINAPI *Clear)(void *, DWORD);
  typedef int (WINAPI *Draw)(void *, void *, int, int, int, int);
  Startup start = (Startup)GetProcAddress(dll, "GdiplusStartup");
  Shutdown stop = (Shutdown)GetProcAddress(dll, "GdiplusShutdown");
  Load load = (Load)GetProcAddress(dll, "GdipLoadImageFromFile");
  Dispose dispose = (Dispose)GetProcAddress(dll, "GdipDisposeImage");
  Dimension width = (Dimension)GetProcAddress(dll, "GdipGetImageWidth"), height = (Dimension)GetProcAddress(dll, "GdipGetImageHeight");
  Frames frames = (Frames)GetProcAddress(dll, "GdipImageGetFrameCount");
  Save save = (Save)GetProcAddress(dll, "GdipSaveImageToFile");
  Bitmap bitmap = (Bitmap)GetProcAddress(dll, "GdipCreateBitmapFromScan0");
  Graphics graphics = (Graphics)GetProcAddress(dll, "GdipGetImageGraphicsContext");
  Clear clear = (Clear)GetProcAddress(dll, "GdipGraphicsClear");
  Draw draw = (Draw)GetProcAddress(dll, "GdipDrawImageRectI");
  Dispose deleteGraphics = (Dispose)GetProcAddress(dll, "GdipDeleteGraphics");
  BOOL ok = FALSE; ULONG_PTR token = 0; void *image = NULL, *flat = NULL, *dc = NULL;
  int status = 1; Start input = {1, NULL, FALSE, FALSE};
  static const GUID pageId = {0x7462dc86, 0x6180, 0x4c7e, {0x8e,0x3f,0xee,0x73,0x33,0xa7,0xa4,0x83}};
  if (!start || !stop || !load || !dispose || !width || !height || !frames || !save || !bitmap || !graphics || !clear || !draw || !deleteGraphics) goto done;
  if ((status = start(&token, &input, NULL)) || (status = load(job->src, &image))) goto done;
  UINT pages = 1, w = 0, h = 0;
  if (cv_image_type(job->src) == 3 && !frames(image, &pageId, &pages) && pages > 1) {
    lstrcpynW(job->error, L"Многостраничный TIFF пока не поддерживается. Исходный файл сохранён.", 400); goto done;
  }
  if ((status = width(image, &w)) || (status = height(image, &h)) || !w || !h || (uint64_t)w * h > 100000000) goto done;
  /* JPEG has no alpha: composite onto white instead of producing black areas. */
  if (job->format == 1) {
    if ((status = bitmap((int)w, (int)h, 0, 0x26200a, NULL, &flat)) ||
        (status = graphics(flat, &dc)) || (status = clear(dc, 0xffffffff)) ||
        (status = draw(dc, image, 0, 0, (int)w, (int)h))) goto done;
    deleteGraphics(dc); dc = NULL;
  }
  CLSID encoder = {0x557cf400, 0x1a04, 0x11d3, {0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
  static const DWORD ids[] = {0x557cf406,0x557cf401,0x557cf400,0x557cf405};
  encoder.Data1 = ids[job->format];
  status = save(flat ? flat : image, temp, &encoder, NULL); ok = status == 0;
done:
  if (dc && deleteGraphics) deleteGraphics(dc);
  if (flat && dispose) dispose(flat);
  if (image && dispose) dispose(image);
  if (token && stop) stop(token);
  FreeLibrary(dll);
  if (!ok && !job->error[0]) swprintf(job->error, 400, L"Не удалось преобразовать изображение (код %d). Проверьте формат и доступ к файлу.", status);
  return ok;
}
static BOOL cv_office_open(OfSession *s, BOOL excel, const wchar_t *path) {
  if (!of_begin(s, excel, NULL)) return FALSE;
  /* Explicit empty passwords and hidden/no-repair flags prevent Office from
     waiting for a password or encoding dialog in the background. */
  of_call(s->doc, L"Close", DISPATCH_METHOD, NULL, 1, (VARIANT[]){of_bool(FALSE)});
  of_release(s->doc); s->doc = NULL;
  VARIANT args[15], result; int n = excel ? 13 : 15;
  for (int i = 0; i < n; i++) { VariantInit(&args[i]); args[i].vt = VT_ERROR; args[i].scode = DISP_E_PARAMNOTFOUND; }
  args[0] = of_str(path); args[1] = excel ? of_int(0) : of_bool(FALSE); args[2] = of_bool(TRUE);
  if (!excel) args[3] = of_bool(FALSE);
  args[4] = of_str(L""); args[5] = of_str(L""); args[6] = of_bool(TRUE);
  if (excel) { args[10] = of_bool(FALSE); args[12] = of_bool(FALSE); }
  else { args[6] = of_bool(FALSE); args[7] = of_str(L""); args[8] = of_str(L""); args[11] = of_bool(FALSE); args[12] = of_bool(FALSE); args[14] = of_bool(TRUE); }
  VariantInit(&result);
  s->hr = of_call(s->collection,L"Open",DISPATCH_METHOD,&result,n,args);
  if (SUCCEEDED(s->hr) && result.vt == VT_DISPATCH && result.pdispVal) { s->doc=result.pdispVal; result.vt=VT_EMPTY; }
  VariantClear(&result); return s->doc != NULL;
}
static BOOL cv_office(CvJob *job, const wchar_t *temp) {
  OfSession s; BOOL excel = cv_excel_file(job->src), ok = FALSE;
  if (!cv_office_open(&s, excel, job->src)) goto done;
  if (job->format == 4) {
    VARIANT args[] = {excel ? of_int(0) : of_str(temp), excel ? of_str(temp) : of_int(17)};
    s.hr = of_call(s.doc, L"ExportAsFixedFormat", DISPATCH_METHOD, NULL, 2, args);
    ok = SUCCEEDED(s.hr);
  } else if (job->format == 6) {
    IDispatch *sheet = of_sheet(&s, NULL);
    VARIANT args[] = {of_str(temp), of_int(62)}; /* xlCSVUTF8, first worksheet only. */
    s.hr = of_call(sheet, L"SaveAs", DISPATCH_METHOD, NULL, 2, args); of_release(sheet);
    ok = SUCCEEDED(s.hr);
  } else {
    IDispatch *content = of_obj(s.doc, L"Content", 0, NULL); VARIANT value; VariantInit(&value);
    s.hr = of_call(content, L"Text", DISPATCH_PROPERTYGET, &value, 0, NULL); of_release(content);
    if (SUCCEEDED(s.hr) && value.vt == VT_BSTR) {
      UINT length = SysStringLen(value.bstrVal);
      if (length <= 16000000) {
        for (UINT i = 0; i < length; i++) { if (value.bstrVal[i] == L'\r') value.bstrVal[i] = L'\n'; else if (value.bstrVal[i] == 7) value.bstrVal[i] = L'\t'; }
        int size = length ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.bstrVal, (int)length, NULL, 0, NULL, NULL) : 0;
        char *utf8 = (char *)malloc((size_t)size + 3);
        if (utf8 && (!length || size)) {
          memcpy(utf8, "\xef\xbb\xbf", 3);
          if (size) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.bstrVal, (int)length, utf8 + 3, size, NULL, NULL);
          HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); DWORD written = 0;
          if (file != INVALID_HANDLE_VALUE) { ok = WriteFile(file, utf8, (DWORD)size + 3, &written, NULL) && written == (DWORD)size + 3; CloseHandle(file); }
        }
        free(utf8);
      }
    }
    VariantClear(&value);
  }
done:
  job->hr = s.hr; of_end(&s);
  if (!ok) swprintf(job->error, 400, L"Не удалось преобразовать документ (Office 0x%08lx). Нужен установленный %ls; файл не должен требовать пароль. Исходник сохранён.", (unsigned long)job->hr, excel ? L"Excel" : L"Word");
  return ok;
}
static inline BOOL cv_convert(CvJob *job) {
  int available[7], n = cv_formats(job->src, TRUE, TRUE, available); BOOL supported = FALSE;
  for (int i = 0; i < n; i++) if (available[i] == job->format) supported = TRUE;
  DWORD attr = GetFileAttributesW(job->src);
  if (!supported || attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    lstrcpynW(job->error, L"Этот файл или выбранное преобразование не поддерживается.", 400); return FALSE;
  }
  const wchar_t *slash = wcsrchr(job->src, L'\\');
  if (!slash || slash - job->src > MAX_PATH - 80) { lstrcpynW(job->error, L"Слишком длинный путь. Переместите файл в папку с более коротким именем.", 400); return FALSE; }
  wchar_t dir[MAX_PATH], base[MAX_PATH], temp[MAX_PATH];
  int directory = (int)(slash - job->src) + 1; memcpy(dir, job->src, (size_t)directory * sizeof(wchar_t)); dir[directory] = 0;
  lstrcpynW(base, slash + 1, MAX_PATH); wchar_t *dot = wcsrchr(base, L'.'); if (dot) *dot = 0;
  if (directory + wcslen(base) + 20 >= MAX_PATH) { lstrcpynW(job->error, L"Слишком длинное имя файла для результата конвертации.", 400); return FALSE; }
  BOOL reserved = FALSE;
  for (int i = 0; i < 100; i++) {
    swprintf(temp, MAX_PATH, L"%ls.CursorPad-%lu-%lu-%d.%ls", dir, GetCurrentProcessId(), GetTickCount(), i, kCvFormats[job->format].ext);
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); reserved = TRUE; break; }
    if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
  }
  if (!reserved) { swprintf(job->error, 400, L"Нет доступа для записи результата рядом с исходником (Windows %lu).", GetLastError()); return FALSE; }
  BOOL ok = job->format < 4 ? cv_image(job, temp) : cv_office(job, temp);
  if (ok) {
    WIN32_FILE_ATTRIBUTE_DATA result;
    if (!GetFileAttributesExW(temp, GetFileExInfoStandard, &result) || (!result.nFileSizeLow && !result.nFileSizeHigh)) {
      ok = FALSE; lstrcpynW(job->error, L"Конвертер не создал содержимое результата. Исходник сохранён.", 400);
    }
  }
  if (ok) {
    ok = FALSE;
    for (int i = 1; i <= 999; i++) {
      if (i == 1) swprintf(job->out, MAX_PATH, L"%ls%ls.%ls", dir, base, kCvFormats[job->format].ext);
      else swprintf(job->out, MAX_PATH, L"%ls%ls (%d).%ls", dir, base, i, kCvFormats[job->format].ext);
      if (MoveFileW(temp, job->out)) { ok = TRUE; break; }
      if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    if (!ok) swprintf(job->error, 400, L"Не удалось сохранить результат (Windows %lu). Существующие файлы не заменены.", GetLastError());
  }
  if (!ok) { DeleteFileW(temp); job->out[0] = 0; }
  return ok;
}
