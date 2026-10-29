/* soyuz_win_test.c — проверка окна «В Союз» (soyuz_ui.c) без запуска CursorPad: ни
   хуков клавиатуры, ни буфера обмена (он общий с рабочим столом человека — запись
   подменена), ни данных пользователя. Окно — на отдельном невидимом рабочем столе,
   рисуется настоящим кодом через WM_PRINTCLIENT и сохраняется в PNG.
   База — настоящая, только чтение, вход Windows (у стенда нет пароля SQL): по
   умолчанию домашний «Союз» localhost\SQLEXPRESS, PLM-DATA; другое — CP_SZ_SERVER и
   CP_SZ_DB. Нужны операции в ТП с кодами 0110, 4110, 4210 (как в ТСТ.* дома).
   Вставка в «Союз» здесь не делается: появление операции в ТП подаётся сообщением
   WM_SZ_NEWOP, как его шлёт поток слежения; сам запрос слежения проверяется на базе.
   Из корня репозитория (Git Bash, WinLibs; native/CursorPadOcr.exe — после build.sh):
     (cd native && windres cursorpad.rc -O coff -o ../build/soyuz_win_test.res)
     gcc -O2 -Wall -Werror -Wno-unknown-pragmas -finput-charset=UTF-8 -o build/soyuz_win_test.exe \
       native/soyuz_win_test.c build/soyuz_win_test.res -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 \
       -luuid -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 -lcomdlg32 -lmsimg32 \
       -Wl,--subsystem,windows -municode -static
     build/soyuz_win_test.exe build/soyuz   → снимки *.png и report.txt; код 0 — всё сошлось
     build/soyuz_win_test.exe --live [маршрут.txt] → живое окно у клиента «Союза» (буфер настоящий) */
#define wWinMain cursorpad_wWinMain
#include "cursorpad.c"
#undef wWinMain

static const wchar_t *g_stDir;
static int g_stFail;
static wchar_t g_stReport[12000];
static wchar_t g_stClip[200];
static int g_stClipN;

static void st_check(BOOL ok, const wchar_t *what) {
  size_t n = wcslen(g_stReport);
  _snwprintf(g_stReport + n, 12000 - n, L"%ls %ls\r\n", ok ? L"ok  " : L"FAIL", what);
  if (!ok) g_stFail++;
}

static void st_note(const wchar_t *fmt, ...) {
  size_t n = wcslen(g_stReport);
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_stReport + n, 12000 - n - 3, fmt, ap);
  va_end(ap);
  wcscat(g_stReport, L"\r\n");
}

static BOOL st_clip(const wchar_t *t) {
  lstrcpynW(g_stClip, t, 200);
  g_stClipN++;
  g_ownClip = FALSE;
  return TRUE;
}

static BOOL st_connect(SQLHENV *env, SQLHDBC *dbc, wchar_t *err, int ecap) {
  *env = SQL_NULL_HENV;
  *dbc = SQL_NULL_HDBC;
  const wchar_t *srv = _wgetenv(L"CP_SZ_SERVER"), *db = _wgetenv(L"CP_SZ_DB");
  if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, env) != SQL_SUCCESS) return FALSE;
  SQLSetEnvAttr(*env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
  if (SQLAllocHandle(SQL_HANDLE_DBC, *env, dbc) != SQL_SUCCESS) return FALSE;
  wchar_t conn[600], outc[600];
  SQLSMALLINT outn = 0;
  _snwprintf(conn, 600, L"DRIVER={SQL Server};SERVER=%ls;DATABASE=%ls;Trusted_Connection=Yes;",
             srv ? srv : L"localhost\\SQLEXPRESS", db ? db : L"PLM-DATA");
  if (SQL_SUCCEEDED(SQLDriverConnectW(*dbc, NULL, conn, SQL_NTS, outc, 600, &outn, SQL_DRIVER_NOPROMPT))) return TRUE;
  odbc_err(*dbc, SQL_HANDLE_DBC, err, ecap);
  return FALSE;
}

static void st_pump(DWORD ms) {
  ULONGLONG end = GetTickCount64() + ms;
  do {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
    if (ms) Sleep(15);
  } while (GetTickCount64() < end);
}

static void st_write(const wchar_t *name, const void *data, DWORD len) {
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%ls\\%ls", g_stDir, name);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wr = 0;
  WriteFile(h, data, len, &wr, NULL);
  CloseHandle(h);
}

typedef struct {
  HDC dc;
} StPrint;

static BOOL CALLBACK st_print_child(HWND c, LPARAM lp) {
  StPrint *p = (StPrint *)lp;
  if (!IsWindowVisible(c)) return TRUE;
  RECT r;
  GetWindowRect(c, &r);
  MapWindowPoints(NULL, g_szWnd, (POINT *)&r, 2);
  POINT org;
  SetViewportOrgEx(p->dc, r.left, r.top, &org);
  SendMessageW(c, WM_PRINT, (WPARAM)p->dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
  SetViewportOrgEx(p->dc, org.x, org.y, NULL);
  return TRUE;
}

/* окно «В Союз» с кнопками и списком — в PNG */
static void st_shot_wnd(const wchar_t *name, HWND wnd);
static void st_shot(const wchar_t *name) { st_shot_wnd(name, g_szWnd); }
static void st_shot_wnd(const wchar_t *name, HWND wnd) {
  st_pump(0);
  RECT rc;
  GetClientRect(wnd, &rc);
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
  HBITMAP bmp = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
  if (!bmp) {
    st_check(FALSE, name);
    if (mem) DeleteDC(mem);
    return;
  }
  HGDIOBJ old = SelectObject(mem, bmp);
  if (wnd == g_szWnd) {
    SendMessageW(wnd, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    StPrint p = {mem};
    EnumChildWindows(wnd, st_print_child, (LPARAM)&p);
  } else {
    PrintWindow(wnd, mem, PW_CLIENTONLY | 2 /* PW_RENDERFULLCONTENT */);
  }
  GdiFlush();
  SelectObject(mem, old);
  IWICImagingFactory *f = ac_factory();
  IWICBitmap *wb = NULL;
  BYTE *png = NULL;
  DWORD len = 0;
  if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromHBITMAP(f, bmp, NULL, WICBitmapIgnoreAlpha, &wb)) &&
      ac_png(f, (IWICBitmapSource *)wb, (UINT)w, (UINT)h, &png, &len)) {
    st_write(name, png, len);
    free(png);
  }
  st_check(len > 0, name);
  if (wb) wb->lpVtbl->Release(wb);
  if (f) f->lpVtbl->Release(f);
  DeleteObject(bmp);
  DeleteDC(mem);
}

static int st_find(const wchar_t *op) {
  for (int i = 0; i < g_szR.n; i++)
    if (!wcscmp(g_szR.it[i].op, op)) return i;
  return -1;
}

/* появление операции в ТП — как его подаёт поток слежения */
static void st_newop(long dir, long parent, const wchar_t *num) {
  SzNew *nw = (SzNew *)calloc(1, sizeof(SzNew));
  nw->gen = g_szGen;
  nw->dir = dir;
  nw->parent = parent;
  lstrcpynW(nw->num, num, 16);
  SendMessageW(g_szWnd, WM_SZ_NEWOP, 0, (LPARAM)nw);
  st_pump(500); /* следующая ложится в буфер с паузой */
}

/* маршрут «Удлинителя вала» — как его кладёт в чат окно чертежа */
static void st_route(wchar_t *txt, int cap) {
  TnRoute *tr = (TnRoute *)malloc(sizeof(TnRoute));
  tn_route(L"Удлинитель вала", L"Круг 50 ГОСТ 2590-2006 / 45 ГОСТ 1050-2013",
           L"Ra 3,2\nRa 0,8\nØ30 -0,02\n-0,04\nM12-6H\n2 отв. Ø4+0,03\n1. 30...34 HRC\n"
           L"2. Общие допуски по ГОСТ 30893.2-mK\n3. Покрытие: аэрозольный лак\n",
           tr);
  int n0 = _snwprintf(txt, cap, L"Лист: УВ-01.pdf · АБВГ.711141.001\n");
  tn_text(tr, txt + n0, cap - n0);
  free(tr);
}

/* --live [маршрут.txt]: окно «В Союз» на обычном рабочем столе, рядом с клиентом «Союза»,
   с настоящим буфером, Ctrl+V и «Вставить» — для проверки на домашнем «Союзе», пока
   запущен CursorPad человека (второй CursorPad не запустится). База — та же, что у стенда. */
static int st_live(const wchar_t *routeFile) {
  apply_theme();
  g_szConnect = st_connect;
  static wchar_t txt[9000];
  wchar_t *file = routeFile ? ac_read_utf8(routeFile, 1 << 20) : NULL;
  if (file) lstrcpynW(txt, file, 9000);
  else st_route(txt, 9000);
  free(file);
  if (!sz_start(txt)) return 4;
  SetTimer(NULL, 0, 500, NULL); /* будить цикл: «×» снимает g_szOn без сообщения */
  MSG m;
  while (g_szOn && GetMessageW(&m, NULL, 0, 0) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
  (void)prev, (void)cmd, (void)show;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv || argc < 2) return 2;
  if (!wcscmp(argv[1], L"--live")) {
    g_inst = inst;
    enable_dpi();
    INITCOMMONCONTROLSEX icc0 = {sizeof(icc0), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    return st_live(argc > 2 ? argv[2] : NULL);
  }
  g_stDir = argv[1];
  CreateDirectoryW(g_stDir, NULL);
  HDESK desk = CreateDesktopW(L"CursorPadSoyuzTest", NULL, NULL, 0, GENERIC_ALL, NULL);
  if (!desk || !SetThreadDesktop(desk)) return 3;
  g_inst = inst;
  enable_dpi();
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);
  _snwprintf(g_dataDir, MAX_PATH, L"%ls\\data", g_stDir);
  CreateDirectoryW(g_dataDir, NULL);
  apply_theme(); /* цвета, кисти и шрифты темы по умолчанию */
  g_szConnect = st_connect;
  g_szClipSet = st_clip;
  g_szNoHook = TRUE;

  /* команда в чате */
  st_check(sz_chat_cmd(L"в союз") && sz_chat_cmd(L"Перенеси маршрут в Союз.") && sz_chat_cmd(L"/союз") &&
               sz_chat_cmd(L"маршрут в «Союз»"),
           L"«в союз», «перенеси маршрут в Союз», «/союз» — команда");
  st_check(!sz_chat_cmd(L"есть ли в союзе вал ТСТ.110.001") && !sz_chat_cmd(L"в союзе") &&
               !sz_chat_cmd(L"как в союз добавить операцию, если нет прав на версию ТП"),
           L"«есть ли в союзе…», длинный вопрос — не команда");
  st_check(!sz_start(L"Ответ без маршрута.\nПросто текст 2026 года."), L"без строк «005 Операция.» очередь не начинается");

  /* живой маршрут, как в чате */
  static wchar_t txt[9000];
  st_route(txt, 9000);
  st_check(sz_start(txt) && g_szWnd && g_szR.n == 10, L"маршрут из 10 операций — в очереди, окно открыто");
  st_check(g_szDb == 1, L"база: ищу аналоги");
  wchar_t first[200];
  lstrcpynW(first, g_stClip, 200);
  st_check(!wcscmp(first, L"0110 Заготовительная"), L"до ответа базы в буфере — текст первой операции");
  st_shot(L"1-loading.png");
  for (int t = 0; t < 300 && !g_szLoaded && !g_szNote[0]; t++) st_pump(100);
  st_pump(2500); /* слежение: первый круг */
  st_check(g_szLoaded, L"аналоги из базы пришли");
  st_check(g_szDb == 2, L"слежение за базой идёт");
  for (int i = 0; i < g_szR.n; i++) {
    const SoItem *it = &g_szR.it[i];
    st_note(L"     %ls %ls → %ls %ls · справочная %ld · в ТП %d · аналог %ls %ls · цех «%ls» · оборуд. «%ls» · Тпз %g Тшт %g",
            it->num, it->op, it->code, it->name, it->dir, it->uses, it->guid[0] ? it->guid : L"—", it->from, it->ceh,
            it->equip, it->aTpz, it->aTsht);
  }
  int iZag = st_find(L"Заготовительная"), iTok = st_find(L"Токарная (черновая)"),
      iShl = st_find(L"Круглошлифовальная"), iOkr = st_find(L"Окрасочная"), iKon = st_find(L"Контрольная");
  st_check(iZag == 0 && g_szR.it[0].dir && g_szR.it[0].guid[0], L"заготовительная: справочная и аналог");
  st_check(iTok > 0 && g_szR.it[iTok].guid[0] && !wcscmp(g_szR.it[iTok].code, L"4110"), L"токарная: аналог 4110");
  st_check(iShl > 0 && !wcscmp(g_szR.it[iShl].code, L"4130") && g_szR.it[iShl].guid[0],
           L"круглошлифовальной 4131 в ТП нет — взята ходовая 4130 из запасных");
  st_check(iOkr > 0 && !g_szR.it[iOkr].guid[0], L"окрасочная: аналога нет — пойдёт текстом");
  /* «Создать ТП»: описание для функции пакета «CursorPad — Союз» (soyuz_make.c) */
  {
    st_check(g_szR.mat[0] && g_szR.blank[0], L"из маршрута взяты материал и заготовка детали");
    wchar_t spec[MAX_PATH], note[200];
    _snwprintf(spec, MAX_PATH, L"%ls\\cursorpad-soyuz.txt", g_stDir);
    int n = szm_write_spec(spec, L"Тестовая сборка ЭСИ", L"", g_szR.des, TRUE, note, 200);
    wchar_t *t = NULL;
    HANDLE h = CreateFileW(spec, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      char buf[16000];
      DWORD rd = 0;
      ReadFile(h, buf, sizeof(buf) - 1, &rd, NULL);
      CloseHandle(h);
      buf[rd] = 0;
      t = rd > 3 ? utf8_to_alloc(buf + 3, rd - 3) : NULL;
    }
    st_check(n == 10 && t && wcsstr(t, L"ПАПКА\tТестовая сборка ЭСИ\r\n") &&
                 wcsstr(t, L"ИЗДЕЛИЕ\tАБВГ.711141.001\tУдлинитель вала\tДетали\t"),
             L"описание: папка, изделие с обозначением из «Лист:», 10 операций");
    st_check(t && wcsstr(t, L"ТП\tАБВГ.711141.001\t010\t4110\t31\t16К20, Токарно-винторезный станок\t15\t4\r\n"),
             L"строка ТП токарной: код 4110, участок 31, станок и нормы аналога");
    st_check(t && wcsstr(t, L"\t045\t7300\t\t\t\t\r\n"), L"окрасочная без аналога: код есть, участок и нормы пустые");
    /* окно «Пакет для загрузки в PLM» → «В Союз…»: три листа — в строки описания (package_ui.c) */
    {
      xp_package_free(g_pk);
      pk_init();
      wchar_t **w = xp_row(&g_pk[PK_ITEMS]);
      pk_put(&g_pk[PK_ITEMS], 0, 0, L"ТСТ.120.000");
      pk_put(&g_pk[PK_ITEMS], 0, 1, L"Узел пробный");
      pk_put(&g_pk[PK_ITEMS], 0, 2, L"Сборочные единицы");
      w = xp_row(&g_pk[PK_LINKS]);
      pk_put(&g_pk[PK_LINKS], 0, 0, L"ТСТ.120.000");
      pk_put(&g_pk[PK_LINKS], 0, 1, L"ТСТ.110.004");
      pk_put(&g_pk[PK_LINKS], 0, 2, L"2");
      w = xp_row(&g_pk[PK_OPS]);
      pk_put(&g_pk[PK_OPS], 0, 0, L"ТСТ.110.004");
      pk_put(&g_pk[PK_OPS], 0, 1, L"5");
      pk_put(&g_pk[PK_OPS], 0, 2, L"Токарная");
      pk_put(&g_pk[PK_OPS], 0, 3, L"31 Механический участок 31");
      pk_put(&g_pk[PK_OPS], 0, 4, L"16К20, Токарно-винторезный станок");
      pk_put(&g_pk[PK_OPS], 0, 5, L"15");
      pk_put(&g_pk[PK_OPS], 0, 6, L"4");
      w = xp_row(&g_pk[PK_OPS]);
      pk_put(&g_pk[PK_OPS], 1, 0, L"ТСТ.110.004");
      pk_put(&g_pk[PK_OPS], 1, 1, L"010");
      pk_put(&g_pk[PK_OPS], 1, 2, L"Какая-то неизвестная");
      (void)w;
      wchar_t *pt = NULL, pnote[200];
      PkSent sent;
      int pn = pk_soyuz_text(&pt, pnote, 200, &sent);
      st_check(pn == 1 && pt && wcsstr(pt, L"ИЗДЕЛИЕ\tТСТ.120.000\tУзел пробный\tСборочные единицы\t\t\r\n") &&
                   wcsstr(pt, L"СОСТАВ\tТСТ.120.000\tТСТ.110.004\t2\t\r\n") &&
                   wcsstr(pt, L"ТП\tТСТ.110.004\t5\t4110\t31\t16К20, Токарно-винторезный станок\t15\t4\r\n") &&
                   wcsstr(pnote, L"без кода справочника: 1"),
               L"пакет → описание: изделие, состав, код 4110 по названию «Токарная», участок 31; неизвестная — пропущена");
      if (pt) st_note(L"     %ls", pt);
      free(pt);
      /* с 2026.10.29: строки из «Союза» («Из Союза») не уходят, заготовка и чертёж — уходят */
      pk_soy_add_item(L"ТСТ.120.000", L"Узел пробный", FALSE);
      pk_soy_add_link(L"ТСТ.120.000", L"ТСТ.110.004");
      w = xp_row(&g_pk[PK_BLANKS]);
      pk_put(&g_pk[PK_BLANKS], 0, 0, L"ТСТ.110.004");
      pk_put(&g_pk[PK_BLANKS], 0, 1, L"Круг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013");
      pk_put(&g_pk[PK_BLANKS], 0, 2, L"3,1");
      pk_put(&g_pk[PK_BLANKS], 0, 3, L"D=40;L=265");
      pk_put(&g_pk[PK_ITEMS], 0, 5, L"C:\\Чертежи\\ТСТ.120.000.pdf");
      pt = NULL;
      pk_soyuz_text(&pt, pnote, 200, &sent);
      st_check(pt && !wcsstr(pt, L"ИЗДЕЛИЕ\t") && !wcsstr(pt, L"СОСТАВ\t") &&
                   wcsstr(pt, L"ЗАГОТОВКА\tТСТ.110.004\tКруг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013\t3,1\tD=40;L=265\r\n") &&
                   wcsstr(pt, L"ЧЕРТЕЖ\tТСТ.120.000\tC:\\Чертежи\\ТСТ.120.000.pdf\r\n") && sent.blanks == 1 && sent.draws == 1,
               L"из «Союза» — не уходит; заготовка и чертёж — строками ЗАГОТОВКА и ЧЕРТЕЖ");
      free(pt);
      pk_soy_clear();
      pk_del_row(&g_pk[PK_BLANKS], 0);
      pk_put(&g_pk[PK_ITEMS], 0, 5, NULL);
      /* окно пакета с этими же строками и «В Союз…» → окно папки: справочники и папки — из базы */
      package_show(FALSE);
      szm_dicts_start(g_pkWnd);
      for (int t = 0; t < 100 && !g_szmDicts.na && !g_szmDicts.err[0]; t++) st_pump(100);
      st_check(g_szmDicts.na == 1 && g_szmDicts.ne == 2 && !wcscmp(g_szmDicts.area[0], L"31 Механический участок 31") &&
                   !wcscmp(g_szmDicts.equip[0], L"16К20, Токарно-винторезный станок"),
               L"справочники из базы: участок «31 Механический участок 31», станков 2, первый «16К20, …»");
      g_pkSheet = PK_OPS;
      pk_fill(TRUE);
      pk_edit_begin(0, 2);
      st_check(g_pkEditCtl == g_pkCombo && SendMessageW(g_pkCombo, CB_GETCOUNT, 0, 0) == OPS_1C_N,
               L"правка «Операции» — список из справочника 1С");
      pk_edit_end(FALSE);
      pk_edit_begin(0, 3);
      st_check(g_pkEditCtl == g_pkCombo && SendMessageW(g_pkCombo, CB_GETCOUNT, 0, 0) == 1, L"правка «Участка» — список из базы");
      st_shot_wnd(L"9-package.png", g_pkWnd);
      pk_edit_end(FALSE);
      st_check(ListView_GetItemCount(g_pkList) == 2, L"в таблице листа «Операции» две строки");
      pk_to_soyuz();
      for (int t = 0; t < 100 && SendMessageW(g_szmCombo, CB_GETCOUNT, 0, 0) == 0; t++) st_pump(100);
      int nf = (int)SendMessageW(g_szmCombo, CB_GETCOUNT, 0, 0);
      st_check(g_szmText && wcsstr(g_szmText, L"СОСТАВ\t") && nf >= 5, L"«В Союз…»: окно папки открыто с описанием, папок из базы не меньше 5");
      st_note(L"     папок в списке: %d", nf);
      st_shot_wnd(L"10-soyuz-make.png", g_szmWnd);
      /* мост — из ресурса CursorPad.exe в папку данных (рядом со стендом его нет) */
      {
        wchar_t bp[MAX_PATH];
        int why = szm_bridge(bp, MAX_PATH);
        HRSRC rs = FindResourceW(g_inst, MAKEINTRESOURCEW(312), RT_RCDATA);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        BOOL sz = GetFileAttributesExW(bp, GetFileExInfoStandard, &fa) && rs && fa.nFileSizeLow == SizeofResource(g_inst, rs);
        st_check(why == 0 && wcsstr(bp, L"\\data\\cp-soyuz.exe") && sz,
                 L"мост cp-soyuz.exe достался из ресурса в папку данных, размер как у ресурса");
        st_note(L"     мост: %ls (%lu байт)", bp, (unsigned long)fa.nFileSizeLow);
      }
      ShowWindow(g_szmWnd, SW_HIDE);
      ShowWindow(g_pkWnd, SW_HIDE);
      free(g_szmText);
      g_szmText = NULL;
      xp_package_free(g_pk);
    }
    if (t) st_note(L"     %ls", t);
    free(t);
  }
  st_check(!wcscmp(g_stClip, L"0110 Заготовительная") && g_szTextOnly,
           L"по умолчанию — текст для «Создать операцию» (в сквозной ТП копию не вставить)");
  st_shot(L"2a-text-default.png");
  sz_command(ID_SZ_MODE);
  st_check(!g_szTextOnly && !wcscmp(g_stClip, g_szR.it[0].guid), L"«Аналог» — в буфере GUID аналога");

  /* GUID в буфере — настоящая операция ТП с тем же кодом */
  {
    SQLHENV env;
    SQLHDBC dbc;
    wchar_t err[300] = L"";
    CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * CARD_ROWS);
    if (rows && st_connect(&env, &dbc, err, 300)) {
      wchar_t sql[1400];
      _snwprintf(sql, 1400,
                 L"SELECT TOP 1 o.InfoObjectId, t.NameKey, ISNULL(CAST(cd.ShortText AS NVARCHAR(40)),N''), 0, 0 "
                 L"FROM InfoObjects AS o JOIN Templates AS t ON t.TemplateId=o.TemplateId "
                 L"JOIN InfoObjectAttributes AS l ON l.OwnerId=o.InfoObjectId AND l.Outdated=0 "
                 L"JOIN NameKeys AS nk ON nk.NameKeyId=l.NameKeyId AND nk.Value=N'Operation' "
                 L"OUTER APPLY (SELECT TOP 1 c.ShortText FROM InfoObjectAttributes AS c JOIN NameKeys AS nkc "
                 L"ON nkc.NameKeyId=c.NameKeyId AND nkc.Value=N'Code' WHERE c.OwnerId=l.Link AND c.Outdated=0) AS cd "
                 L"WHERE o.GlobalIdentifier='%ls'",
                 g_szR.it[iTok > 0 ? iTok : 0].guid);
      int n = card_query(dbc, sql, rows, 1, err, 300);
      st_check(n == 1 && !wcscmp(rows[0].s1, L"TSOperation") && sz_code_num(rows[0].s2) == 4110,
               L"GUID токарной — операция ТП (TSOperation) с кодом 4110");
      /* запрос слежения на всей базе: у операций ТП есть справочная и версия */
      n = sz_poll(dbc, 0, 0x7fffffff, NULL, 0, rows, err);
      int withDir = 0, withVer = 0;
      for (int i = 0; i < n; i++) withDir += rows[i].n2 != 0, withVer += rows[i].n3 != 0;
      st_note(L"     слежение: операций ТП %d, со справочной %d, с версией %d %ls", n, withDir, withVer, err);
      st_check(n > 0 && withDir == n && withVer == n, L"запрос слежения: справочная и версия у каждой операции ТП");
      SQLDisconnect(dbc);
      SQLFreeHandle(SQL_HANDLE_DBC, dbc);
      SQLFreeHandle(SQL_HANDLE_ENV, env);
    } else {
      st_check(FALSE, L"нет связи с базой для проверки GUID");
      st_note(L"     %ls", err);
    }
    free(rows);
  }
  st_shot(L"2-guid.png");

  /* вставили первую в ТП (версия 777) — отметка, в буфере следующая */
  long ver = 777;
  st_newop(g_szR.it[0].dir, ver, L"005");
  st_check(g_szR.it[0].state == SO_DONE && !wcscmp(g_szR.it[0].got, L"005") && g_szR.cur == 1 && g_szR.target == ver,
           L"заготовительная встала под № 005 — версия ТП запомнена, дальше токарная");
  st_check(!wcscmp(g_stClip, g_szR.it[1].guid), L"в буфере — GUID следующей");
  st_newop(g_szR.it[1].dir, 999, L"010");
  st_check(g_szR.it[1].state == SO_WAIT, L"операция в другом ТП не считается");
  st_shot(L"3-next.png");

  /* автоввод: сам sz_auto_start здесь не зовётся — клавиши ушли бы на рабочий стол человека,
     где открыт настоящий клиент; проверяется разбор пункта меню и ответ потока окну */
  {
    wchar_t t[80];
    sz_menu_text(L"&Вставить операцию\tCtrl+Shift+V", t, 80);
    st_check(!wcscmp(t, kSzPasteItem), L"пункт меню «&Вставить операцию\\tCtrl+Shift+V» узнаётся");
    wchar_t *m = (wchar_t *)malloc(600 * sizeof(wchar_t));
    lstrcpynW(m, L"Шаг 3: в меню узла «Версия 1» нет пункта «Вставить операцию». Видны: «Создать», «Открыть». "
                 L"Выделен не тот узел, нет прав технолога или клиент изменился после обновления. Меню закрыто, ничего не нажато.",
              600);
    SendMessageW(g_szWnd, WM_SZ_AUTO, 0, (LPARAM)m);
    st_check(!g_szAutoWait && wcsstr(g_szNote, L"Шаг 3") != NULL, L"автоввод не нашёл пункт — окно говорит, на каком шаге");
    st_shot(L"3a-auto-broken.png");
    m = (wchar_t *)malloc(600 * sizeof(wchar_t));
    lstrcpynW(m, L"«Вставить операцию» нажата на «Версия 1» — жду операцию в ТП…", 600);
    SendMessageW(g_szWnd, WM_SZ_AUTO, 1, (LPARAM)m);
    st_check(g_szAutoWait, L"пункт нажат — жду операцию в базе");
    st_newop(g_szR.it[1].dir, ver, L"010");
    st_check(!g_szAutoWait && g_szR.it[1].state == SO_DONE && g_szR.cur == 2, L"токарная встала — ожидание снято, дальше термическая");
  }

  /* «Аналог» ещё раз — та же строка текстом, как для 1С */
  sz_command(ID_SZ_MODE);
  st_pump(50);
  st_check(!wcscmp(g_stClip, L"5000 Термическая обработка"), L"снова текст: в буфере «5000 Термическая обработка», как для 1С");
  st_note(L"     в буфере: «%ls», режим текст %d, строка %d", g_stClip, g_szTextOnly, g_szR.cur);
  st_shot(L"4-text.png");
  sz_command(ID_SZ_MODE);
  /* «Копировать» у поля — в буфере поле, Ctrl+V очередь не двигает; «В буфер» — вернуть */
  sz_command(ID_SZ_COPY + SZF_CODE);
  st_check(!wcscmp(g_stClip, L"5000") && g_szClip == 1, L"«Копировать» у кода — в буфере «5000»");
  st_shot(L"5-field.png");
  sz_command(ID_SZ_PUT);
  st_check(!wcscmp(g_stClip, g_szR.it[2].guid) && g_szClip == 0, L"«В буфер» вернул операцию");

  /* без аналога — текстом; Ctrl+V (как его ловит хук) — следом следующая */
  sz_select(iOkr);
  st_check(g_szR.cur == iOkr && !wcsncmp(g_stClip, L"7300 ", 5), L"окрасочная без аналога — текст «7300 …»");
  st_shot(L"6-no-analog.png");
  sz_paste_tick(g_szWnd, WM_TIMER, TIMER_SZ_PASTE, 0);
  st_check(g_szR.it[iOkr].pasted && g_szR.cur == iKon && !wcscmp(g_stClip, g_szR.it[iKon].guid),
           L"после Ctrl+V текста — контрольная, её GUID в буфере");
  if (g_szR.it[iOkr].dir) {
    st_newop(g_szR.it[iOkr].dir, ver, L"045");
    st_check(g_szR.it[iOkr].state == SO_DONE && g_szR.cur == iKon, L"окрасочная появилась в ТП — отмечена, буфер на месте");
  }

  /* человек скопировал своё */
  sz_foreign_copy();
  st_check(g_szClip == 2, L"скопировано своё — окно говорит, что в буфере не операция");
  st_shot(L"7-foreign.png");
  sz_command(ID_SZ_PUT);

  /* остальные: пропуск и «Дальше» — до конца */
  sz_command(ID_SZ_SKIP);
  for (int k = 0; k < 20 && sz_cur(); k++) sz_command(ID_SZ_NEXT);
  st_check(!sz_cur(), L"всё подано");
  for (int i = 0; i < g_szR.n; i++)
    if (g_szR.it[i].state == SO_WAIT && g_szR.it[i].dir) st_newop(g_szR.it[i].dir, ver, g_szR.it[i].num);
  st_note(L"     осталось %d (без справочной не отметить), пропущено: контрольная", so_left(&g_szR));
  st_shot(L"8-done.png");
  sz_command(ID_SZ_PREV);
  st_check(sz_cur() && g_szR.it[g_szR.cur].state == SO_WAIT, L"«Назад» возвращает неперенесённую");

  sz_stop();
  st_check(!g_szOn && !g_szThread && !IsWindowVisible(g_szWnd), L"«×» — очередь и слежение остановлены");

  st_write(L"report.txt", "\xEF\xBB\xBF", 3);
  {
    int len8 = WideCharToMultiByte(CP_UTF8, 0, g_stReport, -1, NULL, 0, NULL, NULL);
    char *u = (char *)malloc((size_t)len8);
    if (u) {
      WideCharToMultiByte(CP_UTF8, 0, g_stReport, -1, u, len8, NULL, NULL);
      wchar_t path[MAX_PATH];
      _snwprintf(path, MAX_PATH, L"%ls\\report.txt", g_stDir);
      HANDLE h = CreateFileW(path, FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
      DWORD wr = 0;
      if (h != INVALID_HANDLE_VALUE) WriteFile(h, u, (DWORD)strlen(u), &wr, NULL), CloseHandle(h);
      free(u);
    }
  }
  return g_stFail ? 1 : 0;
}
