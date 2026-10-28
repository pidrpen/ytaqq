/* design_shots_test.c — снимки окон CursorPad для разбора оформления (стенд, в CI не входит).
   Как minichat_test.c: окна создаются на отдельном невидимом рабочем столе и рисуются
   настоящим кодом, беседа выдуманная, модель не вызывается. Данные — в папке стенда
   (SHGetFolderPath подменён), системные курсоры и горячие клавиши не трогаются,
   MessageBox не показывается. Слоистые окна (помощник, его меню и пузырь) кладутся
   на светлый и тёмный фон; блокнот — ещё и с его прозрачностью «пока следует за курсором».
   Из корня репозитория (Git Bash, WinLibs; native/CursorPadOcr.exe — после build.sh):
     (cd native && windres cursorpad.rc -O coff -o ../build/design_shots_test.res)
     gcc -O2 -Wall -Werror -Wno-unknown-pragmas -finput-charset=UTF-8 -o build/design_shots_test.exe \
       native/design_shots_test.c build/design_shots_test.res -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 \
       -luuid -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 -lcomdlg32 -lmsimg32 \
       -Wl,--subsystem,windows -municode -static
     build/design_shots_test.exe build/design_shots [all]   → снимки *.png и report.txt */
#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>

static wchar_t g_dstRoot[MAX_PATH];
static int WINAPI dst_msgbox(HWND h, LPCWSTR t, LPCWSTR c, UINT f) { (void)h; (void)t; (void)c; (void)f; return IDOK; }
static BOOL WINAPI dst_hotkey(HWND h, int id, UINT m, UINT vk) {
  (void)h; (void)id; (void)m; (void)vk;
  SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);
  return FALSE;
}
static BOOL WINAPI dst_setcur(HCURSOR c, DWORD id) { (void)c; (void)id; return TRUE; }
static BOOL WINAPI dst_spi(UINT a, UINT b, PVOID c, UINT d) {
  if (a == SPI_SETCURSORS) return TRUE;
  return SystemParametersInfoW(a, b, c, d);
}
static HRESULT WINAPI dst_folder(HWND h, int csidl, HANDLE tok, DWORD fl, LPWSTR out) {
  (void)h; (void)csidl; (void)tok; (void)fl;
  lstrcpynW(out, g_dstRoot, MAX_PATH);
  return S_OK;
}
#define MessageBoxW(h, t, c, f) dst_msgbox(h, t, c, f)
#define RegisterHotKey(h, i, m, v) dst_hotkey(h, i, m, v)
#define SetSystemCursor(c, i) dst_setcur(c, i)
#define SystemParametersInfoW(a, b, c, d) dst_spi(a, b, c, d)
#define SHGetFolderPathW(h, c, t, f, o) dst_folder(h, c, t, f, o)
#define wWinMain cursorpad_wWinMain
#include "cursorpad.c"
#undef wWinMain

static const wchar_t *g_dstDir;
static wchar_t g_dstReport[16000];

static void dst_log(const wchar_t *line) {
  size_t n = wcslen(g_dstReport);
  _snwprintf(g_dstReport + n, 16000 - n, L"%ls\r\n", line);
}

static void dst_pump(int ms) {
  DWORD until = GetTickCount() + (DWORD)ms;
  for (;;) {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
    if ((LONG)(GetTickCount() - until) >= 0) break;
    Sleep(5);
  }
}

static void dst_write(const wchar_t *name, const void *data, DWORD len) {
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%ls\\%ls", g_dstDir, name);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wr = 0;
  WriteFile(h, data, len, &wr, NULL);
  CloseHandle(h);
}

/* GUID_WICPixelFormat32bppBGR: без wincodecsdk его нет в ссылке (как в ai_chat.c) */
static const GUID kDstBgr = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0e}};

/* BGRA-буфер W×H (альфа не нужна) — в PNG */
static void dst_png(const wchar_t *name, const BYTE *bgra, int W, int H) {
  IWICImagingFactory *f = ac_factory();
  IWICBitmap *wb = NULL;
  BYTE *png = NULL;
  DWORD len = 0;
  if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromMemory(f, (UINT)W, (UINT)H, &kDstBgr, (UINT)W * 4,
                                                       (UINT)W * H * 4, (BYTE *)bgra, &wb)) &&
      ac_png(f, (IWICBitmapSource *)wb, (UINT)W, (UINT)H, &png, &len)) {
    dst_write(name, png, len);
    free(png);
  }
  wchar_t line[300];
  _snwprintf(line, 300, L"%ls %ls %d×%d", len ? L"ok  " : L"FAIL", name, W, H);
  dst_log(line);
  if (wb) wb->lpVtbl->Release(wb);
  if (f) f->lpVtbl->Release(f);
}

/* premultiplied BGRA поверх цвета фона */
static BYTE *dst_over(const BYTE *pm, int W, int H, COLORREF bg) {
  BYTE *out = (BYTE *)malloc((size_t)W * H * 4);
  if (!out) return NULL;
  BYTE b = GetBValue(bg), g = GetGValue(bg), r = GetRValue(bg);
  for (size_t i = 0; i < (size_t)W * H; i++) {
    const BYTE *s = pm + i * 4;
    BYTE *d = out + i * 4;
    int ia = 255 - s[3];
    d[0] = (BYTE)(s[0] + b * ia / 255);
    d[1] = (BYTE)(s[1] + g * ia / 255);
    d[2] = (BYTE)(s[2] + r * ia / 255);
    d[3] = 255;
  }
  return out;
}

static const COLORREF kLight = RGB(242, 242, 242), kDark = RGB(34, 36, 42);

static void dst_png_both(const wchar_t *base, const BYTE *pm, int W, int H) {
  wchar_t name[200];
  BYTE *o = dst_over(pm, W, H, kLight);
  if (o) { _snwprintf(name, 200, L"%ls-light.png", base); dst_png(name, o, W, H); free(o); }
  o = dst_over(pm, W, H, kDark);
  if (o) { _snwprintf(name, 200, L"%ls-dark.png", base); dst_png(name, o, W, H); free(o); }
}

/* окно целиком (с дочерними) — в BGRA-буфер; PrintWindow с полным содержимым, иначе WM_PRINT */
static BYTE *dst_grab(HWND w, int *W, int *H) {
  if (!w) return NULL;
  RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  dst_pump(60);
  RECT r;
  GetWindowRect(w, &r);
  int cw = r.right - r.left, ch = r.bottom - r.top;
  if (cw <= 0 || ch <= 0) return NULL;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = cw;
  bi.bmiHeader.biHeight = -ch;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HDC mem = CreateCompatibleDC(NULL);
  HBITMAP bmp = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
  if (!bmp) { if (mem) DeleteDC(mem); return NULL; }
  HGDIOBJ old = SelectObject(mem, bmp);
  RECT all = {0, 0, cw, ch};
  FillRect(mem, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
  BOOL ok = PrintWindow(w, mem, 2 /* PW_RENDERFULLCONTENT */);
  GdiFlush();
  /* пусто (один цвет) — окно не отрисовано композитором скрытого стола: просим само */
  BOOL blank = TRUE;
  const DWORD *px = (const DWORD *)bits;
  for (size_t i = 1; i < (size_t)cw * ch && blank; i += 7)
    if ((px[i] & 0xFFFFFF) != (px[0] & 0xFFFFFF)) blank = FALSE;
  if (!ok || blank) {
    SendMessageW(w, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND | PRF_NONCLIENT | PRF_OWNED);
    GdiFlush();
    dst_log(ok ? L"  (PrintWindow дал пустое — WM_PRINT)" : L"  (PrintWindow не удался — WM_PRINT)");
  }
  SelectObject(mem, old);
  BYTE *out = (BYTE *)malloc((size_t)cw * ch * 4);
  if (out) memcpy(out, bits, (size_t)cw * ch * 4);
  DeleteObject(bmp);
  DeleteDC(mem);
  *W = cw;
  *H = ch;
  return out;
}

static void dst_shot(HWND w, const wchar_t *name) {
  int W, H;
  BYTE *b = dst_grab(w, &W, &H);
  if (!b) { wchar_t line[200]; _snwprintf(line, 200, L"FAIL %ls (нет окна)", name); dst_log(line); return; }
  dst_png(name, b, W, H);
  free(b);
}

/* снимок окна с его прозрачностью (alpha 0..255) на светлом и тёмном фоне */
static void dst_shot_alpha(HWND w, const wchar_t *base, int alpha) {
  int W, H;
  BYTE *b = dst_grab(w, &W, &H);
  if (!b) return;
  for (size_t i = 0; i < (size_t)W * H; i++) {
    BYTE *p = b + i * 4;
    p[0] = (BYTE)(p[0] * alpha / 255);
    p[1] = (BYTE)(p[1] * alpha / 255);
    p[2] = (BYTE)(p[2] * alpha / 255);
    p[3] = (BYTE)alpha;
  }
  dst_png_both(base, b, W, H);
  free(b);
}

/* эффект при нажатии kind: кадры frames[] полосой на светлом и тёмном (как visual_test.c) */
static void dst_fx_strip(int kind, const wchar_t *base, const int *frames, int n) {
  const int S = 170;
  BYTE *pm = (BYTE *)calloc((size_t)S * n * S, 4);
  if (!pm) return;
  for (int k = 0; k < n; k++) {
    FxWin f;
    memset(&f, 0, sizeof(f));
    f.kind = kind;
    f.W = f.H = S;
    f.bits = (BYTE *)calloc((size_t)S * S, 4);
    if (!f.bits) break;
    g_fxScale = 1.0f;
    g_fxSeed = 777;
    fx_seed_new(&f, kind, S);
    for (f.frame = 0; f.frame < frames[k]; f.frame++) fx_step_new(&f);
    fx_paint_new(&f);
    for (int y = 0; y < S; y++) memcpy(pm + ((size_t)y * S * n + (size_t)k * S) * 4, f.bits + (size_t)y * S * 4, (size_t)S * 4);
    free(f.bits);
  }
  dst_png_both(base, pm, S * n, S);
  free(pm);
}

static void dst_settings(int tab, const wchar_t *name) {
  g_tab = tab;
  if (!IsWindowVisible(g_setHwnd)) toggle_settings();
  place_settings();
  dst_pump(200);
  dst_shot(g_setHwnd, name);
}

/* «Современный» набор (cursor_kit.c): все 16 видов стиля st рядами 32, 48 и 64 точки
   на светлом и тёмном; плюс 32 точки, увеличенные вчетверо без сглаживания, — видно,
   чёткий ли край */
static void dst_kit_gallery(const wchar_t *base) {
  static const int sizes[3] = {32, 48, 64};
  const int cell = 80, W = cell * ST_SLOTS, H = cell * 3;
  BYTE *pm = (BYTE *)calloc((size_t)W * H, 4);
  BYTE *img = (BYTE *)malloc(64 * 64 * 4);
  if (!pm || !img) { free(pm); free(img); return; }
  for (int r = 0; r < 3; r++)
    for (int i = 0; i < ST_SLOTS; i++) {
      int N = sizes[r];
      kit_draw(i, 2, N, img);
      int ox = i * cell + (cell - N) / 2, oy = r * cell + (cell - N) / 2;
      for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
          const BYTE *sp = img + ((size_t)y * N + x) * 4;
          BYTE *d = pm + ((size_t)(oy + y) * W + ox + x) * 4;
          d[0] = (BYTE)(sp[0] * sp[3] / 255), d[1] = (BYTE)(sp[1] * sp[3] / 255), d[2] = (BYTE)(sp[2] * sp[3] / 255);
          d[3] = sp[3];
        }
    }
  dst_png_both(base, pm, W, H);
  free(pm);
  /* увеличение: 8 видов по 32 точки, ×4 */
  const int Z = 4, N = 32, zc = N * Z + 8, ZW = zc * 8, ZH = zc;
  static const int zs[8] = {0, 2, 1, 14, 3, 4, 6, 10};
  BYTE *zp = (BYTE *)calloc((size_t)ZW * ZH, 4);
  if (zp) {
    for (int i = 0; i < 8; i++) {
      kit_draw(zs[i], 2, N, img);
      for (int y = 0; y < N * Z; y++)
        for (int x = 0; x < N * Z; x++) {
          const BYTE *sp = img + ((size_t)(y / Z) * N + x / Z) * 4;
          BYTE *d = zp + ((size_t)(4 + y) * ZW + i * zc + 4 + x) * 4;
          d[0] = (BYTE)(sp[0] * sp[3] / 255), d[1] = (BYTE)(sp[1] * sp[3] / 255), d[2] = (BYTE)(sp[2] * sp[3] / 255);
          d[3] = sp[3];
        }
    }
    wchar_t name[200];
    _snwprintf(name, 200, L"%ls-zoom", base);
    dst_png_both(name, zp, ZW, ZH);
    free(zp);
  }
  free(img);
}

/* все стили и главные сочетания переключателей; карточка «Курсор» в Настройках */
static void dst_kit_all(void) {
  static const wchar_t *const kSt[KIT_STYLES] = {L"svetly", L"temny", L"akcent", L"steklo"};
  g_kitOutline = g_kitShadow = g_kitThemeAcc = TRUE;
  for (int s = 0; s < KIT_STYLES; s++) {
    g_kitStyle = s;
    wchar_t name[200];
    _snwprintf(name, 200, L"kit-%ls", kSt[s]);
    dst_kit_gallery(name);
  }
  g_kitStyle = 0;
  g_kitOutline = FALSE;
  g_kitShadow = FALSE;
  dst_kit_gallery(L"kit-svetly-bare"); /* без обводки и тени */
  g_kitOutline = g_kitShadow = TRUE;
  g_kitThemeAcc = FALSE;
  dst_kit_gallery(L"kit-svetly-blue"); /* значки синим, не цветом темы */
  g_kitThemeAcc = TRUE;
  { /* «Волна»: четыре момента */
    static const int frames[4] = {2, 7, 14, 24};
    dst_fx_strip(17, L"fx-wave", frames, 4);
  }
  /* карточка: набор стоит, в нескольких темах */
  set_skin(SKIN_KIT);
  { /* крутящиеся виды — из своих .ani: файл записан и Windows его читает */
    for (int sl = 14; sl <= 15; sl++) {
      HCURSOR c = kit_ani_load(sl);
      wchar_t path[MAX_PATH], line[400];
      kit_ani_path(sl, path);
      WIN32_FILE_ATTRIBUTE_DATA fa;
      DWORD sz = GetFileAttributesExW(path, GetFileExInfoStandard, &fa) ? fa.nFileSizeLow : 0;
      _snwprintf(line, 400, L"%ls kit_%ls.ani: %lu байт, LoadImage %ls", c ? L"ok  " : L"FAIL", kRias[sl].name,
                 (unsigned long)sz, c ? L"читает" : L"не читает");
      dst_log(line);
      if (c) DestroyCursor(c);
    }
  }
  static const int th[5] = {0, 5, 6, 7, 8};
  static const wchar_t *const thn[5] = {L"obychnaya", L"steklo", L"grafit", L"sinka", L"tseh"};
  for (int i = 0; i < 5; i++) {
    set_theme(th[i]);
    dst_pump(150);
    wchar_t name[200];
    _snwprintf(name, 200, L"kit-card-%ls.png", thn[i]);
    dst_settings(2, name);
    _snwprintf(name, 200, L"kit-cursortab-%ls.png", thn[i]);
    dst_settings(0, name);
  }
  if (IsWindowVisible(g_setHwnd)) toggle_settings();
  set_theme(0);
}

static void dst_chat_fill(void) {
  if (g_acN) return;
  ac_push(0, L"Посмотри чертёж вала и скажи, как его обрабатывать");
  ac_push(1, L"## Маршрут обработки вала\n\n"
             L"1. **Заготовка** — прокат круглый Ø65 мм, сталь 40Х ГОСТ 4543, длина 412 мм с припуском на подрезку торцов.\n"
             L"2. **Токарная черновая** — подрезать торцы в размер 405 мм, зацентровать, точить ступени с припуском 1,5 мм на сторону.\n"
             L"3. **Термообработка** — улучшение до 269…302 HB, после неё проверить твёрдость на двух шейках.\n"
             L"4. **Токарная чистовая** — точить шейки под подшипники Ø40k6 с припуском 0,3 мм под шлифование, фаски 1×45°.\n"
             L"5. **Фрезерная** — шпоночный паз 12N9 на длине 56 мм, глубина 5 мм, симметричность 0,03 относительно оси.\n"
             L"6. **Шлифовальная** — шейки Ø40k6, Ra 0,8; биение относительно оси центров не более 0,016 мм.\n\n"
             L"Что не прочиталось на листе — допуск на длину ступени Ø52 и покрытие: уточните, и я поправлю маршрут. "
             L"Если вал идёт партией, черновую токарную и фрезерную лучше вынести на станки с ЧПУ.");
  ac_push(0, L"А какие нормы времени по этому маршруту?");
  ac_push(1, L"Нормы по ОМН (таблицы токарной и фрезерной обработки):\n"
             L"010 Токарная черновая: Тпз 18 мин, Тшт 12,4 мин\n"
             L"020 Токарная чистовая: Тпз 16 мин, Тшт 9,8 мин\n"
             L"025 Фрезерная: Тпз 14 мин, Тшт 6,1 мин\n"
             L"030 Шлифовальная: Тпз 20 мин, Тшт 7,3 мин\n\n"
             L"Источник — ОМН, карты 12, 31 и 44. Для партии 50 шт. подготовительно-заключительное время делится на партию.");
  ac_redraw();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
  (void)prev;
  (void)cmd;
  (void)show;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv || argc < 2) return 2;
  g_dstDir = argv[1];
  BOOL all = argc >= 3 && !wcscmp(argv[2], L"all");
  CreateDirectoryW(g_dstDir, NULL);
  _snwprintf(g_dstRoot, MAX_PATH, L"%ls\\appdata", g_dstDir);
  CreateDirectoryW(g_dstRoot, NULL);
  /* свой рабочий стол: окна стенда не мелькают у человека на экране */
  HDESK desk = CreateDesktopW(L"CursorPadDesignShots", NULL, NULL, 0, GENERIC_ALL, NULL);
  if (!desk || !SetThreadDesktop(desk)) return 3;
  OleInitialize(NULL);
  g_inst = inst;
  enable_dpi();
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&icc);
  /* настройки стенда: курсор Windows (системные курсоры не трогать), помощника не звать при старте */
  {
    wchar_t folder[MAX_PATH], path[MAX_PATH];
    _snwprintf(folder, MAX_PATH, L"%ls\\CursorPad", g_dstRoot);
    CreateDirectoryW(folder, NULL);
    _snwprintf(path, MAX_PATH, L"%ls\\cursor.txt", folder);
    const char *pref = "system 180 250 ai 0 0\n";
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(h, pref, (DWORD)strlen(pref), &wr, NULL); CloseHandle(h); }
    _snwprintf(path, MAX_PATH, L"%ls\\ai_avatar.ini", folder);
    WritePrivateProfileStringW(L"Avatar", L"Visible", L"0", path);
    _snwprintf(path, MAX_PATH, L"%ls\\notes.txt", folder);
    const char *notes = "Вал 40Х — уточнить допуск на длину ступени Ø52\r\n"
                        "Заявка на резку: лист 10 мм, 3 шт., до пятницы\r\n"
                        "Позвонить в ОГТ насчёт нормы на шлифовку\r\n";
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(h, notes, (DWORD)strlen(notes), &wr, NULL); CloseHandle(h); }
  }

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(COL_PAPER);
  wc.lpszClassName = L"CursorPadWindow";
  if (!RegisterClassExW(&wc)) return 1;
  SetCursorPos(300, 300);
  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"CursorPadWindow", L"CursorPad",
                              WS_POPUP | WS_CLIPCHILDREN, 320, 330, WND_W, WND_H, NULL, NULL, inst, NULL);
  if (!hwnd) return 1;
  apply_dpi(hwnd);
  g_aiUiThread = GetCurrentThreadId();
  SetWindowPos(hwnd, HWND_TOPMOST, 320, 330, g_ww, g_hh, SWP_NOACTIVATE);
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd);
  KillTimer(hwnd, TIMER_FOLLOW); /* не ездить за курсором стенда */
  dst_pump(300);
  if (g_follow) toggle_follow(); /* закреплено: кнопки живые, как при работе в окне */
  dst_pump(100);

  if (argc >= 3 && !wcscmp(argv[2], L"kit")) { /* только «Современный» набор и его карточка */
    dst_kit_all();
    char rep[48000];
    int kk = WideCharToMultiByte(CP_UTF8, 0, g_dstReport, -1, rep, sizeof(rep), NULL, NULL);
    dst_write(L"report.txt", rep, kk > 0 ? (DWORD)kk - 1 : 0);
    LocalFree(argv);
    return 0;
  }
  static const wchar_t *const kThemeFile[THEME_COUNT] = {L"obychnaya", L"bumaga", L"myagkaya", L"chertezh", L"rytsarskaya",
                                                         L"steklo", L"grafit", L"sinka", L"tseh"};
  for (int t = 0; t < THEME_COUNT; t++) {
    if (t && !all) break;
    set_theme(t);
    dst_pump(200);
    wchar_t name[200];
    const wchar_t *tn = kThemeFile[t];
    /* блокнот: режим заметок и режим поиска; «следует за курсором» — с прозрачностью 180 */
    set_pad_mode(0);
    dst_pump(100);
    _snwprintf(name, 200, L"%ls-pad-notes.png", tn);
    dst_shot(hwnd, name);
    _snwprintf(name, 200, L"%ls-pad-notes-follow", tn);
    dst_shot_alpha(hwnd, name, g_alphaFollow);
    set_pad_mode(1);
    dst_pump(100);
    _snwprintf(name, 200, L"%ls-pad-search.png", tn);
    dst_shot(hwnd, name);
    set_pad_mode(0);
    /* Настройки: все вкладки; на «Курсоре» выбрана «Тяночка» (системные курсоры стенд не трогает) */
    if (!t) set_skin(SKIN_K8);
    for (int s = 0; s < SEC_COUNT; s++) {
      if (!t || s == 0 || s == 2) {
        _snwprintf(name, 200, L"%ls-settings-%d-%ls.png", tn, s, kSecNames[s]);
        dst_settings(s, name);
      }
    }
    if (IsWindowVisible(g_setHwnd)) toggle_settings();
    /* окна из окна курсора и клавиш: ответ поиска, карточка PLM, распознанный текст, горячие клавиши */
    if (!t) {
      show_answer_text(L"Вал 40Х — сталь конструкционная легированная, ГОСТ 4543-2016.\r\n"
                       L"Улучшение: закалка 860 °C, отпуск 500 °C, 269…302 HB.\r\n"
                       L"Заменители: 45Х, 38ХА, 40ХН, 40ХС.");
      dst_pump(200);
      dst_shot(g_answer, L"obychnaya-answer.png");
      if (g_answer) ShowWindow(g_answer, SW_HIDE);
      show_card_text(L"АБВГ.715221.004  Вал\r\n"
                     L"Материал      Круг 65 ГОСТ 2590-2006 / 40Х ГОСТ 4543-2016\r\n"
                     L"Масса         3,42 кг\r\n"
                     L"Применяемость АБВГ.301234.001 СБ (2 шт.)\r\n"
                     L"Изменение     Изв. 1247 от 12.09.2026");
      dst_pump(200);
      dst_shot(g_card, L"obychnaya-card.png");
      if (g_card) ShowWindow(g_card, SW_HIDE);
      show_ocr_text(L"Неуказанные предельные отклонения размеров: H14, h14, ±IT14/2.\r\n"
                    L"Покрытие Хим.Окс.прм. Твёрдость 269…302 HB.\r\n"
                    L"* Размер для справок.");
      dst_pump(200);
      dst_shot(g_ocrWnd, L"obychnaya-ocr.png");
      if (g_ocrWnd) ShowWindow(g_ocrWnd, SW_HIDE);
      hk_show();
      dst_pump(200);
      dst_shot(g_hkWnd, L"obychnaya-hotkeys.png");
      if (g_hkWnd) ShowWindow(g_hkWnd, SW_HIDE);
    }
    /* чат с мини-ИИ: полный и мини-чат у помощника */
    aichat_show_full();
    dst_chat_fill();
    ac_status(L"Enter — отправить · Shift+Enter — новая строка");
    dst_pump(300);
    _snwprintf(name, 200, L"%ls-chat.png", tn);
    dst_shot(g_acWnd, name);
    ac_mini_show();
    dst_pump(300);
    _snwprintf(name, 200, L"%ls-minichat.png", tn);
    dst_shot(g_amWnd, name);
    if (!t) { /* ИИ думает: «Стоп» в обоих окнах, точки в ленте */
      g_acId = 1;
      ac_busy(TRUE);
      ac_status(L"Думаю… первый ответ после запуска модели может грузиться до минуты");
      dst_pump(300);
      dst_shot(g_amWnd, L"obychnaya-minichat-busy.png");
      aichat_show_full();
      dst_pump(300);
      dst_shot(g_acWnd, L"obychnaya-chat-busy.png");
      g_acId = 0;
      ac_busy(FALSE);
      ac_status(L"");
    }
    ac_mini_close();
    if (g_acWnd) ShowWindow(g_acWnd, SW_HIDE);
  }
  set_theme(0);
  dst_pump(100);

  /* помощник: кадры робота и кошки, меню, пузыри — слоистые окна поверх светлого и тёмного */
  ac_avatar_summon();
  dst_pump(900);
  for (int skin = 0; skin < 2; skin++) {
    ac_avatar_set_skin(skin ? AC_SKIN_CAT : AC_SKIN_ROBOT);
    g_acHopAt = 0;
    dst_pump(500);
    ac_avatar_present();
    if (g_acAvatarFramePixels)
      dst_png_both(skin ? L"avatar-cat" : L"avatar-robot", g_acAvatarFramePixels, g_acAvatarSize, g_acAvatarSize);
  }
  ac_avatar_set_skin(AC_SKIN_ROBOT);
  dst_pump(300);
  ac_avatar_menu(g_acAvatar);
  dst_pump(300);
  if (g_acMenuPix) dst_png_both(L"avatar-menu", g_acMenuPix, g_acMenuBmpW, g_acMenuBmpH);
  ac_menu_close();
  dst_pump(100);
  g_acAvatarBusy = TRUE;
  ac_avatar_bubble(L"Читаю «Ведомость материалов.xlsx»", TRUE, 0);
  dst_pump(300);
  ac_bubble_update();
  if (g_acBubblePixels) dst_png_both(L"avatar-bubble-thought", g_acBubblePixels, g_acBubbleW, g_acBubbleH);
  g_acAvatarBusy = FALSE;
  ac_avatar_bubble(L"Маршрут готов: 6 операций, нормы по ОМН посчитаны. Открыть чат?", FALSE, 60000);
  dst_pump(300);
  ac_bubble_update();
  if (g_acBubblePixels) dst_png_both(L"avatar-bubble-say", g_acBubblePixels, g_acBubbleW, g_acBubbleH);

  { /* эффект «Лепестки» (kind 16): четыре момента */
    static const int frames[4] = {3, 10, 22, 38};
    dst_fx_strip(16, L"fx-petals", frames, 4);
  }

  char out[48000];
  int k = WideCharToMultiByte(CP_UTF8, 0, g_dstReport, -1, out, sizeof(out), NULL, NULL);
  dst_write(L"report.txt", out, k > 0 ? (DWORD)k - 1 : 0);
  LocalFree(argv);
  return 0;
}
