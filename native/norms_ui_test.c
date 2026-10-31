/* norms_ui_test.c — проверка окна «Режимы» (norms_ui.c) без запуска CursorPad: ни хуков, ни
   обновлений, ни данных пользователя. Окно создаётся на отдельном невидимом рабочем столе,
   рисуется настоящим кодом через WM_PRINTCLIENT и сохраняется в PNG. Маршрут — выдуманный
   вал по правилам techno.c, таблицы — native/tables/norms репозитория (только чтение).
   Из корня репозитория (Git Bash, WinLibs; native/CursorPadOcr.exe — после build.sh):
     (cd native && windres cursorpad.rc -O coff -o ../build/norms_ui_test.res)
     gcc -O2 -Wall -Werror -Wno-unknown-pragmas -finput-charset=UTF-8 -o build/norms_ui_test.exe \
       native/norms_ui_test.c build/norms_ui_test.res -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 \
       -luuid -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 -lcomdlg32 -lmsimg32 \
       -Wl,--subsystem,windows -municode -static
     build/norms_ui_test.exe build/norms_ui native/tables/norms   → снимки *.png и report.txt;
     код 0 — всё сошлось */
#define wWinMain cursorpad_wWinMain
#include "cursorpad.c"
#undef wWinMain

static const wchar_t *g_nutDir;
static int g_nutFail;
static wchar_t g_nutReport[12000];

static void nut_check(BOOL ok, const wchar_t *what) {
  size_t n = wcslen(g_nutReport);
  _snwprintf(g_nutReport + n, 12000 - n, L"%ls %ls\r\n", ok ? L"ok  " : L"FAIL", what);
  if (!ok) g_nutFail++;
}

static void nut_pump(void) {
  MSG m;
  while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}

static void nut_write(const wchar_t *name, const void *data, DWORD len) {
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%ls\\%ls", g_nutDir, name);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wr = 0;
  WriteFile(h, data, len, &wr, NULL);
  CloseHandle(h);
}

/* дочерние окна (поля, список, кнопки) — поверх нарисованного окна */
static BOOL CALLBACK nut_child(HWND h, LPARAM lp) {
  if (!IsWindowVisible(h)) return TRUE;
  HDC mem = (HDC)lp;
  RECT r;
  GetWindowRect(h, &r);
  MapWindowPoints(NULL, g_nuWnd, (POINT *)&r, 2);
  POINT org;
  SetViewportOrgEx(mem, r.left, r.top, &org);
  SendMessageW(h, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
  SetViewportOrgEx(mem, org.x, org.y, NULL);
  return TRUE;
}

static void nut_shot(const wchar_t *name) {
  nut_pump();
  RECT rc;
  GetClientRect(g_nuWnd, &rc);
  int w = rc.right, h = rc.bottom;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HDC mem = CreateCompatibleDC(NULL);
  HBITMAP bmp = mem && w > 0 && h > 0 ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
  DWORD len = 0;
  if (bmp) {
    HGDIOBJ old = SelectObject(mem, bmp);
    SendMessageW(g_nuWnd, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    EnumChildWindows(g_nuWnd, nut_child, (LPARAM)mem);
    GdiFlush();
    SelectObject(mem, old);
    IWICImagingFactory *f = ac_factory();
    IWICBitmap *wb = NULL;
    BYTE *png = NULL;
    if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromHBITMAP(f, bmp, NULL, WICBitmapIgnoreAlpha, &wb)) &&
        ac_png(f, (IWICBitmapSource *)wb, (UINT)w, (UINT)h, &png, &len)) {
      nut_write(name, png, len);
      free(png);
    }
    if (wb) wb->lpVtbl->Release(wb);
    if (f) f->lpVtbl->Release(f);
    DeleteObject(bmp);
  }
  if (mem) DeleteDC(mem);
  nut_check(len > 0, name);
}

/* номер строки параметра par у операции k в окне */
static int nut_row(int k, const wchar_t *par) {
  int n;
  const NmOp *o = nu_op(k);
  nu_fields(o, &n);
  for (int i = 0; i < n; i++) {
    const NuField *f = nu_field(o, i);
    if (f && !wcscmp(f->par, par)) return i;
  }
  return -1;
}

/* как человек: выбрать операцию, вписать значение, пересчёт по таймеру */
static void nut_type(int k, const wchar_t *par, const wchar_t *v) {
  if (k != g_nuCur) {
    SendMessageW(g_nuList, LB_SETCURSEL, (WPARAM)k, 0);
    SendMessageW(g_nuWnd, WM_COMMAND, MAKEWPARAM(ID_NU_LIST, LBN_SELCHANGE), (LPARAM)g_nuList);
  }
  int i = nut_row(k, par);
  if (i < 0) {
    nut_check(FALSE, L"нет строки параметра в окне");
    return;
  }
  SetFocus(g_nuEdit[i]);
  SetWindowTextW(g_nuEdit[i], v); /* EN_CHANGE — правка и таймер пересчёта */
  SendMessageW(g_nuWnd, WM_TIMER, TIMER_NU_CALC, 0);
  SetFocus(g_nuList);
}

static double nut_v(int k, const wchar_t *par) {
  const NmOp *o = nu_op(k);
  return o ? nm_v(&o->c, par) : NM_NONE;
}

static int kT = -1, kD = -1; /* токарная и сверлильная в списке окна */

static BOOL nut_near(double a, double b) { return !nm_na(a) && a > b - 1e-6 && a < b + 1e-6; }

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
  (void)prev;
  (void)cmd;
  (void)show;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv || argc < 3) return 2;
  g_nutDir = argv[1];
  CreateDirectoryW(g_nutDir, NULL);
  /* свой рабочий стол: окно проверки не мелькает у человека на экране */
  HDESK desk = CreateDesktopW(L"CursorPadNormsTest", NULL, NULL, 0, GENERIC_ALL, NULL);
  if (!desk || !SetThreadDesktop(desk)) return 3;
  g_inst = inst;
  enable_dpi();
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  _snwprintf(g_dataDir, MAX_PATH, L"%ls\\data", g_nutDir);
  CreateDirectoryW(g_dataDir, NULL);
  apply_theme();
  g_fontUi = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
  g_fontBody = g_fontUi;
  g_fontSmall = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
  g_fontDisplay = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
  g_nmDirFix = argv[2]; /* таблицы репозитория, без раскладки в «Документы» */

  /* без маршрута — окно подсказывает, откуда он берётся */
  nu_open();
  nut_check(g_nuWnd != NULL, L"окно открылось и без маршрута");
  nut_check(g_nuBaseOk && g_nuBase.n > 40 && !g_nuBase.err[0], L"таблицы нормативов прочитаны без ошибок");
  nut_shot(L"0-no-route.png");
  DestroyWindow(g_nuWnd);
  nut_check(!g_nuWnd && !g_nuRes, L"закрытие освобождает расчёт");

  static TnRoute r;
  tn_route(L"Вал", L"Сталь 45 ГОСТ 1050-2013", L"Ø40h9 Ra 3,2 фаска 1×45° 2 отв. Ø6", &r);
  nm_last_keep(nm_last_new(&r, L"1,8", NULL));
  nu_open();
  for (int i = 0; g_nuRes && i < g_nuRes->n; i++) {
    if (g_nuRes->op[i].kind == NMK_TURN && kT < 0) kT = i;
    if (g_nuRes->op[i].kind == NMK_DRILL && kD < 0) kD = i;
  }
  nut_check(g_nuRes && g_nuRes->n == r.n - 1 && wcsstr(g_nuRes->off, L"Контроль") && kT >= 0 && kD >= 0,
            L"в окне — операции маршрута, кроме контроля (без норм), среди них токарная и сверлильная");
  nut_check(g_nuRes->op[0].kind == NMK_SAW && !nm_na(g_nuRes->op[0].tsht), L"005 Отрезная — на пиле, с Тшт");
  nut_check(nm_na(g_nuRes->op[kT].to) && wcsstr(g_nuRes->op[kT].toNeed, L"длина обработки L") != NULL,
            L"без длины То нет — окно говорит, что нужна L");
  nut_check(nut_near(nut_v(kT, L"σв"), 600), L"σв стали 45 — 600 из перечня материалов");
  nut_shot(L"1-opened.png");

  /* L — и То считается по формулам */
  nut_type(kT, L"L", L"120");
  const NmOp *o = nu_op(kT);
  double to1 = o ? o->to : NM_NONE;
  nut_check(!nm_na(to1) && o->full, L"L 120 — То по режимам и полная Тшт");
  const NmPar *pv = o ? nm_parc(&o->c, L"V") : NULL;
  nut_check(pv && pv->org == NMO_CALC && pv->v > 50 && pv->v < 300, L"V — по формуле, в разумных пределах");
  wchar_t t[64] = L"";
  GetWindowTextW(g_nuEdit[nut_row(kT, L"V")], t, 64);
  nut_check(t[0] && wcscmp(t, pv ? pv->s : L"") == 0, L"поле V показывает расчётную скорость");
  nut_shot(L"2-length.png");

  /* своя подача — скорость и То пересчитаны с ней */
  nut_type(kT, L"S", L"0,3");
  o = nu_op(kT);
  const NmPar *ps = o ? nm_parc(&o->c, L"S") : NULL;
  nut_check(ps && ps->org == NMO_ASK && nut_near(ps->v, 0.3), L"подача 0,3 — «задано»");
  nut_check(o && !nm_na(o->to) && fabs(o->to - to1) > 1e-6, L"То пересчитано с новой подачей");
  nut_shot(L"3-feed.png");

  /* сверлильная: глубина и партия (партия — общая) */
  nut_type(kD, L"l", L"12");
  nut_type(kD, L"партия", L"50");
  nut_check(!nm_na(nu_op(kD)->to), L"020: глубина 12 — То сверления");
  nut_check(nut_near(nut_v(kT, L"партия"), 50) && nut_near(nut_v(kD, L"партия"), 50), L"партия — обеим операциям");
  nut_check(!nm_na(nu_op(kT)->tshk) && !nm_na(nu_op(kD)->tshk), L"с партией — Тшк у обеих");
  nut_shot(L"4-drill.png");

  wchar_t line[400];
  nu_overrides(line, 400, TRUE);
  nut_check(!wcscmp(line, L"партия 50, 010: L 120, подача 0,3, 020: глубина 12"), L"строка для чата");
  wchar_t txt[400];
  _snwprintf(txt, 400, L"   строка: %ls\r\n", line);
  wcscat(g_nutReport, txt);

  /* стёртое поле — снова как считает CursorPad */
  nut_type(kT, L"S", L"");
  ps = nm_parc(&nu_op(kT)->c, L"S");
  nut_check(ps && ps->org == NMO_TAB, L"подача стёрта — снова по таблице");

  /* сброс — всё как было, То снова нет */
  SendMessageW(g_nuWnd, WM_COMMAND, MAKEWPARAM(ID_NU_RESET, BN_CLICKED), 0);
  nut_check(nm_na(nu_op(kT)->to) && !g_nuBatch[0], L"«Сбросить правки» — как до правок");
  nut_shot(L"5-reset.png");

  /* «Применить»: нормы в ответе беседы — на месте, без новых сообщений; правки — в вопрос беседы */
  ac_push(0, L"Маршрут по чертежу вала");
  ac_push(1, L"Маршрут обработки — черновик по правилам, проверяет технолог\n010 Токарная.\n015 Сверлильная."
             L"\n\nНормы времени (ОМН; проверяет технолог):\n010 Токарная: Тпз 14 мин · Тшт ≥ 1 мин без машинного То"
             L"\nРежимы: 010 не посчитаны; 015 не посчитаны — изменить · подробно\n\nПосле норм — своё.");
  nut_type(kT, L"L", L"120");
  nut_type(kT, L"S", L"0,3");
  nut_type(kD, L"l", L"12");
  nut_type(kD, L"партия", L"50");
  int turns = g_acN;
  SendMessageW(g_nuWnd, WM_COMMAND, MAKEWPARAM(ID_NU_CHAT, BN_CLICKED), 0);
  const wchar_t *ans = g_acN ? g_acTurns[g_acN - 1].text : L"";
  nut_check(g_acN == turns, L"«Применить» — без новых сообщений в ленте");
  nut_check(wcsstr(ans, L"Маршрут обработки — черновик") == ans && wcsstr(ans, L"\n015 Сверлильная.\n\nНормы времени (ОМН") &&
                wcsstr(ans, L"\n\nПосле норм — своё.") && !wcsstr(ans, L"не посчитаны"),
            L"нормы в ответе заменены на месте, маршрут и хвост целы");
  nut_check(wcsstr(ans, L"\nРежимы: 010 V ") && wcsstr(ans, L"020 V ") && wcsstr(ans, L" — изменить · подробно"),
            L"в ответе — строка режимов обеих операций");
  nut_check(wcsstr(g_nmLast->ask, L"010: L 120") && wcsstr(g_nmLast->ask, L"020: глубина 12") && !g_nuBatch[0],
            L"правки — в вопросе беседы, в окне сброшены");
  nut_check(!nm_na(nu_op(kT)->to) && nut_near(nut_v(kD, L"партия"), 50), L"окно после «Применить» считает то же");
  _snwprintf(txt, 400, L"   ответ: %ls\r\n", wcsstr(ans, L"Режимы:") ? wcsstr(ans, L"Режимы:") : L"?");
  wcscat(g_nutReport, txt);

  DestroyWindow(g_nuWnd);
  nut_pump();
  nut_check(!g_nuWnd, L"окно закрыто");
  /* отчёт */
  int n8 = WideCharToMultiByte(CP_UTF8, 0, g_nutReport, -1, NULL, 0, NULL, NULL);
  char *u8 = (char *)malloc((size_t)n8);
  if (u8) {
    WideCharToMultiByte(CP_UTF8, 0, g_nutReport, -1, u8, n8, NULL, NULL);
    nut_write(L"report.txt", u8, (DWORD)(n8 - 1));
    free(u8);
  }
  CloseDesktop(desk);
  return g_nutFail ? 1 : 0;
}
