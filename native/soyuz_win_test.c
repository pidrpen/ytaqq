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
#define ST_REPORT_CAP 60000 /* отчёт стенда; переполнение 12000 затирало соседние переменные (30.10) */
static wchar_t g_stReport[ST_REPORT_CAP];
static wchar_t g_stClip[200];
static int g_stClipN;

static void st_check(BOOL ok, const wchar_t *what) {
  size_t n = wcslen(g_stReport);
  if (!ok) g_stFail++;
  if (n + 8 >= ST_REPORT_CAP) return; /* отчёт полон — дальше не писать, не затирать соседей */
  _snwprintf(g_stReport + n, ST_REPORT_CAP - n, L"%ls %ls\r\n", ok ? L"ok  " : L"FAIL", what);
  g_stReport[ST_REPORT_CAP - 1] = 0;
}

static void st_note(const wchar_t *fmt, ...) {
  size_t n = wcslen(g_stReport);
  va_list ap;
  va_start(ap, fmt);
  if (n + 8 >= ST_REPORT_CAP) {
    va_end(ap);
    return;
  }
  _vsnwprintf(g_stReport + n, ST_REPORT_CAP - n - 3, fmt, ap);
  g_stReport[ST_REPORT_CAP - 4] = 0;
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

/* участок операции по таблице приоритетов (строка группы — статический буфер, берём сразу) */
static wchar_t g_stGrp[8][80]; /* восемь буферов по кругу — чтобы несколько ответов жили в одной st_note */
static int g_stGrpI;
static const wchar_t *st_grp_area(const wchar_t *op, const wchar_t *eq) {
  const SzPrio *p = szp_find_eq(op, eq);
  wchar_t *out = g_stGrp[g_stGrpI++ & 7];
  lstrcpynW(out, p ? p->area[0] : L"-", 80);
  return out;
}
static BOOL st_grp(const wchar_t *op, const wchar_t *eq, const wchar_t *want) { return !wcscmp(st_grp_area(op, eq), want); }

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
  g_pkNoBox = TRUE; /* окна «Проверить» ждали бы человека */
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
  st_check(sz_start(txt) && g_szWnd && g_szR.n >= 9, L"маршрут из 9+ операций (названия справочника; их число меняется с правилами технолога) — в очереди, окно открыто");
  st_note(L"     операций в маршруте: %d", g_szR.n);
  st_check(g_szDb == 1, L"база: ищу аналоги");
  wchar_t first[200];
  lstrcpynW(first, g_stClip, 200);
  st_check(!wcscmp(first, L"4280 Отрезная"), L"до ответа базы в буфере — текст первой операции (заготовка — «Отрезная» 4280)");
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
  int iZag = st_find(L"Отрезная"), iTok = st_find(L"Токарная"), /* первая из двух токарных */
      iShl = st_find(L"Круглошлифовальная"), iOkr = st_find(L"Получение покрытий органических (лакакрасочных)"),
      iKon = st_find(L"Контроль");
  st_check(iZag == 0 && g_szR.it[0].dir && !wcscmp(g_szR.it[0].code, L"4280") && !g_szR.it[0].guid[0],
           L"отрезная первой: справочная 4280 есть, аналога в ТП дома нет — пойдёт текстом");
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
    st_check(n >= 9 && t && wcsstr(t, L"ПАПКА\tТестовая сборка ЭСИ\r\n") &&
                 wcsstr(t, L"ИЗДЕЛИЕ\tАБВГ.711141.001\tУдлинитель вала\tДетали\t"),
             L"описание: папка, изделие с обозначением из «Лист:», 9 операций и транспортирования между участками групп");
    st_check(t && wcsstr(t, L"\t4110\t12\t16К20, Токарно-винторезный станок\t15\t4\r\n"),
             L"строка ТП токарной: код 4110, участок 12 (группа МЕХОБРАБОТКА по умолчанию важнее аналога на 31), станок и нормы аналога");
    st_check(t && wcsstr(t, L"\t7300\t\t\t\t\r\n") && wcsstr(note, L"без участка — проверить") && wcsstr(note, L" 7300"),
             L"окрасочная без аналога и без группы: код есть, участок и нормы пустые, в отчёте «без участка — проверить»");
    /* таблица приоритетов технолога (soyuz_prio.c) важнее аналога: 4110 → участок 32, станок 1К62 */
    {
      wchar_t pp[MAX_PATH];
      szp_path(pp);
      HANDLE hp = CreateFileW(pp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hp != INVALID_HANDLE_VALUE) {
        DWORD wr;
        WriteFile(hp, "\xEF\xBB\xBF", 3, &wr, NULL);
        const char *body = "# проверка\r\n\xD0\x9C\xD0\x95\xD0\xA5\xD0\x9E\xD0\x91\xD0\xA0\xD0\x90\xD0\x91\xD0\x9E\xD0\xA2\xD0\x9A\xD0\x90\t12\r\n4110\t32\t1\xD0\x9A""62\r\n\xD0\xA1\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBB\xD0\xB8\xD0\xBB\xD1\x8C\xD0\xBD\xD0\xB0\xD1\x8F\t31\t2\xD0\x9D""135\r\n";
        WriteFile(hp, body, (DWORD)strlen(body), &wr, NULL);
        CloseHandle(hp);
      }
      g_szpLoaded = FALSE;
      const SzPrio *pr = szp_find(L"4110 Токарная");
      const SzPrio *pm = szp_find(L"4130 Шлифовальная");
      st_check(pr && pr->n == 1 && !wcscmp(pr->area[0], L"32") && !wcscmp(pr->equip[0], L"1К62") &&
                   szp_find(L"Сверлильная (отв.)") && szp_find(L"4210") && !szp_find(L"7300") && pm &&
                   !wcscmp(pm->area[0], L"32") && !pm->equip[0][0] && !szp_find(L"0200 Контроль"),
               L"таблица приоритетов: 4110 → 32/1К62, «Сверлильная» и 4210 — одна строка; шлифовальной — 32 (группа ШЛИФОВАЛЬНЫЕ), контролю — ничего");
      { /* группы участков дописались в файл человека (в нём была одна МЕХОБРАБОТКА), его строки целы */
        wchar_t *pt = share_read(pp);
        st_check(pt && wcsstr(pt, L"\nЗАГОТОВКА\t31\t") && wcsstr(pt, L"\nЧПУ\t32\t") && wcsstr(pt, L"\nСБОРКА\t12\t") &&
                     wcsstr(pt, L"МЕХОБРАБОТКА\t12\r\n4110\t32\t1К62") && !wcsstr(pt, L"\nМЕХОБРАБОТКА\t12\t#"),
                 L"группы участков дописаны в конец таблицы технолога, его МЕХОБРАБОТКА и строки операций не тронуты");
        free(pt);
        /* строка группы — один статический буфер (szp_find_eq), поэтому участок сравнивается сразу */
        st_note(L"     группы: 4280 «%ls», 2129 «%ls», 4260 «%ls», 4152 «%ls», 4233 «%ls», 4260+ЧПУ «%ls», 8809 «%ls», 9171 «%ls»",
                st_grp_area(L"4280 Отрезная", NULL), st_grp_area(L"2129 Гибка", NULL), st_grp_area(L"4260 Фрезерная", NULL),
                st_grp_area(L"4152 Зубодолбежная", NULL), st_grp_area(L"4233 Токарная с ЧПУ", NULL),
                st_grp_area(L"4260 Фрезерная", L"6Р13Ф3 с ЧПУ"), st_grp_area(L"8809 Слесарно-сборочная", NULL),
                st_grp_area(L"9171 Термическая резка лазерная", NULL));
        st_check(st_grp(L"4280 Отрезная", NULL, L"31") && st_grp(L"2129 Гибка", NULL, L"31") && st_grp(L"4260 Фрезерная", NULL, L"12") &&
                     st_grp(L"4152 Зубодолбежная", NULL, L"32") && st_grp(L"4233 Токарная с ЧПУ", NULL, L"32") &&
                     st_grp(L"4260 Фрезерная", L"6Р13Ф3 с ЧПУ", L"32") && st_grp(L"8809 Слесарно-сборочная", NULL, L"12") &&
                     st_grp(L"9171 Термическая резка лазерная", NULL, L"31") && !szp_find(L"9000 Сварка") &&
                     !szp_find(L"7300 Получение покрытий"),
                 L"группы: заготовка и гибка → 31; зубодолбёжная, ЧПУ (по операции и по станку) → 32; фрезерная и сборка → 12; "
                 L"сварке и покрытиям участка нет");
      }
      int n2 = szm_write_spec(spec, L"Тестовая сборка ЭСИ", L"", g_szR.des, TRUE, note, 200);
      wchar_t *t2 = NULL;
      HANDLE h2 = CreateFileW(spec, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
      if (h2 != INVALID_HANDLE_VALUE) {
        char buf2[16000];
        DWORD rd2 = 0;
        ReadFile(h2, buf2, sizeof(buf2) - 1, &rd2, NULL);
        CloseHandle(h2);
        t2 = rd2 > 3 ? utf8_to_alloc(buf2 + 3, rd2 - 3) : NULL;
      }
      int ntr = 0;
      for (const wchar_t *q = t2; q && (q = wcsstr(q, L"\t0401\t")) != NULL; q += 6) ntr++;
      st_check(n2 > 10 && t2 && wcsstr(t2, L"\t005\t4280\t31\t\t\t\r\n") && wcsstr(t2, L"\t010\t0401\t32\t\t\t\r\n") &&
                   wcsstr(t2, L"\t015\t4110\t32\t1К62\t15\t4\r\n") && wcsstr(t2, L"\t4210\t31\t2Н135\t10\t4\r\n") &&
                   wcsstr(t2, L"\t0401\t32\t\t\t\r\n") && wcsstr(t2, L"\t4130\t32\t") && ntr >= 3 &&
                   wcsstr(note, L"без участка — проверить") && wcsstr(note, L" 7300") && !wcsstr(note, L" 4280"),
               L"«Создать ТП»: отрезная → 31 (ЗАГОТОВКА), приоритет перекрыл аналог (4110 → 32/1К62, 4210 → 31), шлифовальная → 32 "
               L"(ШЛИФОВАЛЬНЫЕ); «0401 Транспортирование» перед каждой сменой участка, номера по порядку");
      st_note(L"     транспортирований в описании: %d", ntr);
      free(t2);
    }
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
      st_check(pn == 1 && pt && wcsstr(pt, L"ИЗДЕЛИЕ\tТСТ.120.000\tУзел пробный\tСборочные единицы\t\t\t") &&
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
      /* пустая операция: участок и станок — из таблицы приоритетов, Тпз — по таблицам ОМН (pk_norms.c) */
      {
        wchar_t **w3 = xp_row(&g_pk[PK_OPS]);
        (void)w3;
        pk_put(&g_pk[PK_OPS], 2, 0, L"ТСТ.110.004");
        pk_put(&g_pk[PK_OPS], 2, 1, L"015");
        pk_put(&g_pk[PK_OPS], 2, 2, L"4210 Сверлильная");
        int pf = pk_equip_fill();
        st_check(pf >= 1 && !wcscmp(xp_cell(&g_pk[PK_OPS], 2, 3), L"31") && !wcscmp(xp_cell(&g_pk[PK_OPS], 2, 4), L"2Н135"),
                 L"пустой сверлильной участок 31 и станок 2Н135 — из таблицы приоритетов");
        int nf = pk_norms_fill(TRUE);
        st_check(nf >= 1 && xp_cell(&g_pk[PK_OPS], 2, 5)[0], L"у сверлильной без норм появился Тпз по таблицам ОМН");
        st_note(L"     нормы сверлильной: Тпз «%ls», Тшт «%ls»", xp_cell(&g_pk[PK_OPS], 2, 5), xp_cell(&g_pk[PK_OPS], 2, 6));
        /* смена участка и конец маршрута: фрезерная без участка — на 12 (МЕХОБРАБОТКА), деталь входит в
           ТСТ.120.000, у которой первая операция на «5 Сборочный участок» */
        wchar_t **w4 = xp_row(&g_pk[PK_OPS]);
        (void)w4;
        pk_put(&g_pk[PK_OPS], 3, 0, L"ТСТ.110.004");
        pk_put(&g_pk[PK_OPS], 3, 1, L"020");
        pk_put(&g_pk[PK_OPS], 3, 2, L"4260 Фрезерная");
        w4 = xp_row(&g_pk[PK_OPS]);
        pk_put(&g_pk[PK_OPS], 4, 0, L"ТСТ.120.000");
        pk_put(&g_pk[PK_OPS], 4, 1, L"005");
        pk_put(&g_pk[PK_OPS], 4, 2, L"8800 Слесарно-сборочная");
        pk_put(&g_pk[PK_OPS], 4, 3, L"5 Сборочный участок");
        pk_equip_fill();
        int trn = pk_transport_fill();
        XpTable *ot = &g_pk[PK_OPS];
        int nTr = 0, lastTr = -1, frez = -1;
        for (int r = 0; r < ot->n; r++)
          if (!_wcsicmp(xp_cell(ot, r, 0), L"ТСТ.110.004")) {
            if (pk_is_transport(xp_cell(ot, r, 2))) nTr++, lastTr = r;
            if (!wcsncmp(xp_cell(ot, r, 2), L"4260", 4)) frez = r;
          }
        st_check(trn == 2 && nTr == 2 && frez >= 0 && !wcscmp(xp_cell(ot, frez, 3), L"12") && lastTr >= 0 &&
                     !wcscmp(xp_cell(ot, lastTr, 3), L"5 Сборочный участок") && !wcscmp(xp_cell(ot, lastTr, 1), L"030"),
                 L"транспортирование: перед фрезерной на 12 и в конце на «5 Сборочный участок» (№ 030), фрезерная на 12 по МЕХОБРАБОТКЕ");
        st_check(pk_transport_fill() == 0, L"повторный вызов транспортирований не добавляет");
        { /* примеры: вал — отрезная, фрезерная, круглошлифовальная (31 → 12 → 32); сварная сборка — отрезная,
             слесарно-сборочная (31 → 12); участки — от групп таблицы, транспортирование — при каждой смене */
          int b0 = ot->n;
          xp_row(ot), pk_put(ot, b0, 0, L"ТСТ.140.001"), pk_put(ot, b0, 1, L"005"), pk_put(ot, b0, 2, L"4280 Отрезная");
          xp_row(ot), pk_put(ot, b0 + 1, 0, L"ТСТ.140.001"), pk_put(ot, b0 + 1, 1, L"010"), pk_put(ot, b0 + 1, 2, L"4260 Фрезерная");
          xp_row(ot), pk_put(ot, b0 + 2, 0, L"ТСТ.140.001"), pk_put(ot, b0 + 2, 1, L"015"), pk_put(ot, b0 + 2, 2, L"4131 Круглошлифовальная");
          xp_row(ot), pk_put(ot, b0 + 3, 0, L"ТСТ.140.000"), pk_put(ot, b0 + 3, 1, L"005"), pk_put(ot, b0 + 3, 2, L"4280 Отрезная");
          xp_row(ot), pk_put(ot, b0 + 4, 0, L"ТСТ.140.000"), pk_put(ot, b0 + 4, 1, L"010"), pk_put(ot, b0 + 4, 2, L"8809 Слесарно-сборочная");
          pk_equip_fill();
          int trn2 = pk_transport_fill();
          int a31 = 0, a12 = 0, a32 = 0, tr2 = 0, rowsVal = 0, rowsAsm = 0;
          for (int r = b0; r < ot->n; r++) {
            const wchar_t *des = xp_cell(ot, r, 0), *op = xp_cell(ot, r, 2), *ar = xp_cell(ot, r, 3);
            BOOL val = !wcscmp(des, L"ТСТ.140.001");
            rowsVal += val, rowsAsm += !val;
            if (pk_is_transport(op)) tr2++;
            else if (!wcsncmp(ar, L"31", 2)) a31++;
            else if (!wcsncmp(ar, L"12", 2)) a12++;
            else if (!wcsncmp(ar, L"32", 2)) a32++;
          }
          st_check(trn2 == 3 && a31 == 2 && a12 == 2 && a32 == 1 && tr2 == 3 && rowsVal == 5 && rowsAsm == 3,
                   L"вал 31 → 12 → 32 и сварная сборка 31 → 12: участки от групп, транспортирование перед каждой сменой (3)");
          st_note(L"     строк вала %d (операций 3 + транспортирований 2), сборки %d (2 + 1); участки 31/12/32: %d/%d/%d", rowsVal, rowsAsm, a31, a12, a32);
          while (ot->n > b0) pk_del_row(ot, ot->n - 1); /* чужие строки — прочь, дальше стенд считает семь строк */
        }
        for (int r = 0; r < ot->n; r++)
          if (!_wcsicmp(xp_cell(ot, r, 0), L"ТСТ.110.004"))
            st_note(L"     %ls %ls · %ls · %ls", xp_cell(ot, r, 1), xp_cell(ot, r, 2), xp_cell(ot, r, 3), xp_cell(ot, r, 4));
        wchar_t pp2[MAX_PATH];
        szp_path(pp2);
        DeleteFileW(pp2);
        g_szpLoaded = FALSE;
      }
      /* ссылка PLM на папку (soyuz_make.c): разбор, поиск по номеру в списке, подстановка в описание */
      {
        wchar_t kind = 0;
        long id = 0;
        st_check(szm_link_parse(L"pmsz-plm:um-splmsrv[a4484722]:4450/C.18122", &kind, &id) && kind == L'C' && id == 18122 &&
                     szm_link_parse(L"C.468", &kind, &id) && kind == L'C' && id == 468 &&
                     szm_link_parse(L"#468", &kind, &id) && kind == L'C' && id == 468 &&
                     szm_link_parse(L"pmsz-plm:PLM:4450/IO.5794", &kind, &id) && kind == L'I' && id == 5794 &&
                     !szm_link_parse(L"31", &kind, &id) && !szm_link_parse(L"Тестовая сборка ЭСИ", &kind, &id) &&
                     !szm_link_parse(L"Корневая\\Подпапка", &kind, &id),
                 L"ссылка PLM: pmsz-plm…/C.18122, C.468, #468 — папка; IO.5794 — объект; «31» и пути — не ссылки");
        SzmFolders *fl = (SzmFolders *)calloc(1, sizeof(SzmFolders));
        fl->path = (wchar_t (*)[SZM_PATH])calloc(2, sizeof(*fl->path));
        fl->pathId = (long *)calloc(2, sizeof(long));
        fl->tp = (wchar_t (*)[SZM_PATH])calloc(1, sizeof(*fl->tp));
        fl->tpId = (long *)calloc(1, sizeof(long));
        lstrcpynW(fl->path[0], L"Тестовая сборка ЭСИ", SZM_PATH);
        fl->pathId[0] = 468;
        lstrcpynW(fl->path[1], L"Конструкторская подготовка производства\\Заказ 1", SZM_PATH);
        fl->pathId[1] = 999;
        fl->n = 2;
        g_szmFold = fl;
        wchar_t r1[SZM_PATH], r2[SZM_PATH], r3[SZM_PATH];
        szm_folder_resolve(L"pmsz-plm:PLM:4450/C.468", 0, r1, SZM_PATH);
        szm_folder_resolve(L"#999", 0, r2, SZM_PATH);
        szm_folder_resolve(L"C.12345", 0, r3, SZM_PATH);
        st_check(!wcscmp(r1, L"Тестовая сборка ЭСИ") && !wcscmp(r2, L"Конструкторская подготовка производства\\Заказ 1") &&
                     !wcscmp(r3, L"C.12345"),
                 L"ссылка → путь из списка по номеру; неизвестный номер уходит функции как C.12345");
        g_szmFold = NULL;
        free(fl->path), free(fl->pathId), free(fl->tp), free(fl->tpId), free(fl);
      }
      /* отправка частями (szm_split_spec): ЭСИ отдельно, ТП по 3 детали; одна деталь — целиком */
      {
        wchar_t sp[MAX_PATH];
        _snwprintf(sp, MAX_PATH, L"%ls\\cursorpad-split.txt", g_stDir);
        wchar_t big[4000] = L"# проверка\r\nПАПКА\tТестовая сборка ЭСИ\r\nПАПКА_ТП\tДля основных ТП\r\nИЗДЕЛИЕ\tТСТ.130.000\tУзел\tСборочные единицы\t\t\r\n";
        for (int k = 1; k <= 5; k++) {
          wchar_t line[200];
          _snwprintf(line, 200, L"ТП\tТСТ.130.00%d\t005\t4110\t31\t\t15\t4\r\nТП\tТСТ.130.00%d\t010\t0200\t31\t\t\t1\r\n", k, k);
          wcscat(big, line);
        }
        szm_write_part(sp, big);
        wchar_t *parts[20];
        int np = szm_split_spec(sp, parts, 20);
        st_check(np == 3 && wcsstr(parts[0], L"ИЗДЕЛИЕ\tТСТ.130.000") && wcsstr(parts[0], L"ПАПКА_ТП\t") && !wcsstr(parts[0], L"ТП\tТСТ.130.001") &&
                     wcsstr(parts[1], L"ПАПКА_ТП\tДля основных ТП\r\n") && wcsstr(parts[1], L"ТП\tТСТ.130.001\t005") &&
                     wcsstr(parts[1], L"ТП\tТСТ.130.003\t010") && !wcsstr(parts[1], L"ТСТ.130.004") &&
                     wcsstr(parts[2], L"ТП\tТСТ.130.004") && wcsstr(parts[2], L"ТП\tТСТ.130.005\t010\t0200"),
                 L"описание на 5 деталей делится: ЭСИ + папка ТП, потом ТП по 3 и по 2 детали с папкой ТП в каждой части");
        for (int i = 0; i < np; i++) free(parts[i]);
        szm_write_part(sp, L"ПАПКА_ТП\tДля основных ТП\r\nТП\tТСТ.130.001\t005\t4110\t31\t\t15\t4\r\n");
        st_check(szm_split_spec(sp, parts, 20) == 0, L"одна деталь без ЭСИ — одним вызовом, частей нет");
        DeleteFileW(sp);
        { /* папка под сборку (soyuz_asmfolder.c): код и группа из обозначения, имя как у соседей; обозначения выдуманные */
          wchar_t code[16], grp[32], num[120];
          st_check(szm_des_split(L"АДЕ 3422-682.01.01", code, 16, grp, 32, num, 120) && !wcscmp(code, L"АДЕ") && !wcscmp(grp, L"3422") &&
                       !wcscmp(num, L"3422-682.01.01") && szm_des_split(L"АБВГ.123456.000", code, 16, grp, 32, num, 120) &&
                       !wcscmp(code, L"АБВГ") && !wcscmp(grp, L"123456") && !wcscmp(num, L"123456.000") &&
                       !szm_des_split(L"Рама", code, 16, grp, 32, num, 120) && !szm_des_split(L"12345", code, 16, grp, 32, num, 120),
                   L"обозначение → код, группа, номер без кода; слово и число — не обозначение");
          static wchar_t lst[8][SZM_PATH] = {L"Испытания", L"Испытания\\АДЕ", L"Испытания\\АДЕ\\АДЕ 3422",
                                             L"Испытания\\АДЕ\\АДЕ 3422\\3422-901 Приспособление", L"Испытания\\АДЕ\\АДЕ 3422\\3422-902 Кондуктор",
                                             L"Испытания\\АДЕ\\АДЕ 3425", L"Испытания\\Прочее",
                                             L"Испытания\\АДЕ\\АДЕ 3422\\3422-901 Приспособление\\Старое"};
          SzmAsmFolder f;
          BOOL ok1 = szm_asm_folder(L"Испытания", lst, 8, L"АДЕ 3422-682.01.01", L"Стенд проверочный", &f);
          st_check(ok1 && !wcscmp(f.path, L"Испытания\\АДЕ\\АДЕ 3422\\3422-682 Стенд проверочный") && f.groupFound && !f.exists && f.newLevels == 1,
                   L"группа найдена, номер коротко как у соседей — новая папка сборки в группе");
          st_note(L"     предложение: %ls", f.path);
          BOOL ok2 = szm_asm_folder(L"Испытания\\", lst, 8, L"АДЕ 3422-901.00.00.00", L"Приспособление", &f);
          st_check(ok2 && f.exists && !wcscmp(f.path, L"Испытания\\АДЕ\\АДЕ 3422\\3422-901 Приспособление"),
                   L"папка сборки с тем же номером уже есть — берётся она (корень с хвостовым «\\»)");
          BOOL ok3 = szm_asm_folder(L"Испытания", lst, 8, L"АДЕ 9999-5.00.00", L"Новая оснастка", &f);
          st_check(ok3 && !f.groupFound && !wcscmp(f.group, L"Испытания\\АДЕ\\АДЕ 9999") &&
                       !wcscmp(f.path, L"Испытания\\АДЕ\\АДЕ 9999\\9999-5 Новая оснастка") && f.newLevels == 2,
                   L"группы нет — предлагается создать её в папке кода, названную как соседние группы");
          static wchar_t lst2[3][SZM_PATH] = {L"Корень", L"Корень\\123456", L"Корень\\123456\\123456.001 Корпус"};
          BOOL ok4 = szm_asm_folder(L"Корень", lst2, 3, L"АБВГ.123456.000", L"Редуктор", &f);
          st_check(ok4 && f.groupFound && !wcscmp(f.path, L"Корень\\123456\\123456.000 Редуктор") && f.newLevels == 1,
                   L"уровня кода нет, группа названа одними цифрами, соседи с полным номером — так же");
          wchar_t hd[200], hn[200];
          st_check(szm_head_asm(L"ИЗДЕЛИЕ\tАБВГ.123456.100\tУзел\tСборочные единицы\t\t\r\nИЗДЕЛИЕ\tАБВГ.123456.000\tРедуктор\tСборочные единицы\t\t\r\n"
                                L"СОСТАВ\tАБВГ.123456.000\tАБВГ.123456.100\t1\r\n",
                                hd, 200, hn, 200) &&
                       !wcscmp(hd, L"АБВГ.123456.000") && !wcscmp(hn, L"Редуктор") &&
                       !szm_head_asm(L"ИЗДЕЛИЕ\tАБВГ.123456.001\tВал\tДетали\t\t\r\n", hd, 200, hn, 200),
                   L"головная сборка описания — та, что никуда не входит; одни детали — сборки нет");
          { /* описание из окна при новой папке — со строкой СОЗДАТЬ_ПАПКИ */
            wchar_t *keep = g_szmText;
            g_szmText = _wcsdup(L"ИЗДЕЛИЕ\tАБВГ.123456.000\tРедуктор\tСборочные единицы\t\t\r\nТП\tАБВГ.123456.001\t005\t4110\t31\t\t15\t4\r\n");
            wchar_t sp2[MAX_PATH];
            _snwprintf(sp2, MAX_PATH, L"%ls\\cursorpad-mkdir.txt", g_stDir);
            int dr = 0;
            g_szmMkDirs = TRUE;
            int nw = szm_write_text(sp2, L"Корень\\123456\\123456.000 Редуктор", L"", &dr);
            g_szmMkDirs = FALSE;
            wchar_t *wt = szm_read_spec(sp2);
            st_check(nw == 1 && wt && wcsstr(wt, L"ПАПКА\tКорень\\123456\\123456.000 Редуктор\r\nСОЗДАТЬ_ПАПКИ\t1\r\n"),
                     L"описание с новой папкой сборки несёт «СОЗДАТЬ_ПАПКИ 1» сразу после ПАПКА");
            free(wt);
            DeleteFileW(sp2);
            free(g_szmText);
            g_szmText = keep;
          }
        }
        /* пакет 1.0.0.7: версия из строки «ПАКЕТ», ход работы из файла .progress */
        g_szmPkgVer = 0;
        st_check(szm_ver_num(L"1.0.0.7") == SZM_VER(1, 0, 0, 7) && szm_ver_num(L"1.0.0.10") > szm_ver_num(L"1.0.0.7") &&
                     szm_ver_num(L"2.0") > szm_ver_num(L"1.9.9.9") && !szm_ver_num(L"ВЕРСИЯ 3") &&
                     szm_pkg_line(L"ПАКЕТ\t1.0.0.7\r\nВЕРСИЯ\t3\r\nПАПКА\tx\r\n") == SZM_VER(1, 0, 0, 7) &&
                     g_szmPkgVer == SZM_VER(1, 0, 0, 7) && !szm_pkg_line(L"ВЕРСИЯ\t3\r\nПАПКА\tx\r\n") &&
                     g_szmPkgVer == SZM_VER(1, 0, 0, 7),
                 L"версия пакета: «ПАКЕТ\t1.0.0.7» в ответе запоминается, старый ответ без строки её не сбрасывает");
        {
          wchar_t pp[MAX_PATH], pt[400] = L"";
          _snwprintf(pp, MAX_PATH, L"%ls\\cursorpad-split.txt.progress", g_stDir);
          st_check(!szm_progress_text(pp, pt, 400), L"файла хода нет — текста нет");
          szm_write_part(pp, L"12\t40\tТП ТСТ.130.004");
          BOOL okp = szm_progress_text(pp, pt, 400);
          st_check(okp && !wcscmp(pt, L"12 из 40: ТП ТСТ.130.004"), L"ход работы из файла: «12 из 40: ТП …»");
          szm_write_part(pp, L"0\t0\tищу обозначения в «Союзе»: 7");
          st_check(szm_progress_text(pp, pt, 400) && !wcscmp(pt, L"ищу обозначения в «Союзе»: 7"), L"ход без счёта — одной строкой");
          DeleteFileW(pp);
        }
        g_szmPkgVer = 0;
      }
      /* действия из чата (ai_esi.c): команды узнаёт код, пути — из текста */
      {
        wchar_t cmd[600];
        _snwprintf(cmd, 600, L"загрузи ЭСИ по чертежам «%ls» и в Союз", g_stDir);
        { /* в папке стенда гарантированно есть картинка: снимки окон могут не успеть (или не сняться на невидимом столе) */
          wchar_t smp[MAX_PATH];
          _snwprintf(smp, MAX_PATH, L"%ls\\0-sample.png", g_stDir);
          HANDLE hs = CreateFileW(smp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
          if (hs != INVALID_HANDLE_VALUE) {
            DWORD wrs;
            WriteFile(hs, "\x89PNG\r\n\x1a\n", 8, &wrs, NULL);
            CloseHandle(hs);
          }
        }
        TmPaths pl = {0};
        int np = esi_paths(cmd, &pl);
        st_check(esi_chat_cmd(L"загрузи ЭСИ по чертежам C:\\Чертежи") && esi_chat_cmd(L"Заведи эси в Союз по этим чертежам") &&
                     esi_chat_cmd(L"чертежи в союз") && !esi_chat_cmd(L"выгрузи ЭСИ из союза") && !esi_chat_cmd(L"в союз") &&
                     !esi_chat_cmd(L"есть ли в союзе ТСТ.110.004") && !rt_chat_cmd(L"норма материала по ведомости") &&
                     !prio_chat_cmd(L"какой участок у токарной"),
                 L"чат: «загрузи ЭСИ по чертежам» узнаётся, «выгрузи», «в союз», сверка и вопросы — нет");
        { /* правило «СБ» (пользователь 29.10): «321-23-сб32» — спецификация, «321-23-сб32СБ» — сборочный чертёж той же сборки */
          wchar_t d2[PP_CELL], n2[PP_CELL], c2[8];
          int doc1 = -1, doc2 = -1, doc3 = -1;
          BOOL f1 = pp_file_title_code(L"Z:\\x\\321-23-сб32 Рама.pdf", d2, n2, &doc1, c2);
          BOOL s1 = f1 && doc1 == 0 && !wcscmp(d2, L"321-23-сб32") && !wcscmp(n2, L"Рама");
          BOOL f2 = pp_file_title_code(L"Z:\\x\\321-23-сб32СБ Рама.pdf", d2, n2, &doc2, c2);
          BOOL s2 = f2 && doc2 == 1 && !wcscmp(d2, L"321-23-сб32") && !wcscmp(c2, L"СБ");
          BOOL f3 = pp_file_title_code(L"Z:\\x\\321-23-сб32СБ-1.tif", d2, n2, &doc3, c2);
          BOOL s3 = f3 && doc3 == 1 && !wcscmp(d2, L"321-23-сб32") && !n2[0];
          st_check(s1 && s2 && s3, L"«СБ»: «321-23-сб32» — не сборочный чертёж (спецификация/сборка), «321-23-сб32СБ» и «…СБ-1» — СБ сборки 321-23-сб32");
        }
        st_check(ac_is_des(L"АБВГ.711141.001") && ac_is_des(L"ТСТ.100.000") && ac_is_des(L"АДЕ 3422-682.01.01") &&
                     ac_is_des(L"АБВГ.123456.000СБ") && !ac_is_des(L"2026-10-29") && !ac_is_des(L"ГОСТ 2590-2006") &&
                     !ac_is_des(L"16К20,") && !ac_is_des(L"4110") && !ac_is_des(L"Токарная") && ac_is_path_start(L"C:\\x") &&
                     !ac_is_path_start(L"ТСТ.100.000"),
                 L"кликабельный чат: обозначения узнаются, даты, ГОСТы, коды и слова — нет; путь — по «X:\\»");
        st_check(np == 1 && pl.n >= 1 && wcsstr(pl.p[0], L".png"), L"чат: путь к папке в кавычках найден, её картинки собраны в список");
        st_note(L"     файлов из папки стенда: %d (папка «%ls»)", pl.n, g_stDir);
        free(pl.p);
        { /* тот же путь с хвостовым «\\» и с прямыми слэшами — как его вставляют из Проводника или терминала */
          wchar_t alt[600], fwd[MAX_PATH];
          lstrcpynW(fwd, g_stDir, MAX_PATH);
          for (wchar_t *c = fwd; *c; c++)
            if (*c == L'\\') *c = L'/';
          _snwprintf(alt, 600, L"по чертежам \"%ls\\\" и %ls в Союз", g_stDir, fwd);
          TmPaths pl2 = {0};
          int np2 = esi_paths(alt, &pl2);
          st_check(np2 == 2 && pl2.n >= 2, L"чат: путь с хвостовым «\\» в кавычках и путь с прямыми слэшами без кавычек — оба найдены");
          st_note(L"     найдено путей %d, файлов %d", np2, pl2.n);
          free(pl2.p);
        }
      }
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
      { /* «Ещё»: один пункт «ЭСИ и ТП в Союз» (пункт «Загрузить ЭСИ в Союз» убран 30.10); в окне «Чертежи…» — первая слева */
        int nEsi = 0, nOld = 0;
        for (size_t i = 0; i < sizeof(kMore) / sizeof(kMore[0]); i++) {
          if (kMore[i].text && !wcscmp(kMore[i].text, L"ЭСИ и ТП в Союз")) nEsi++;
          if (kMore[i].text && (wcsstr(kMore[i].text, L"Загрузить ЭСИ") || wcsstr(kMore[i].text, L"пакет или сразу"))) nOld++;
        }
        RECT rd, rp;
        GetWindowRect(GetDlgItem(g_pkWnd, ID_PK_DRAW), &rd);
        GetWindowRect(GetDlgItem(g_pkWnd, ID_PK_PASTE), &rp);
        st_check(nEsi == 1 && nOld == 0 && rd.left < rp.left && IsWindowVisible(GetDlgItem(g_pkWnd, ID_PK_DRAW)),
                 L"меню «Ещё»: один пункт «ЭСИ и ТП в Союз», второго нет; в окне «Чертежи…» — первая кнопка подвала");
        RECT ra, rs;
        GetWindowRect(GetDlgItem(g_pkWnd, ID_PK_ADD), &ra);
        GetWindowRect(GetDlgItem(g_pkWnd, ID_PK_ASM), &rs);
        st_check(IsWindowVisible(GetDlgItem(g_pkWnd, ID_PK_ADD)) && IsWindowVisible(GetDlgItem(g_pkWnd, ID_PK_ASM)) &&
                     rd.left < ra.left && ra.left < rs.left && rs.left < rp.left,
                 L"«+ Строка» и «+ Сборка» — видимые кнопки сразу за «Чертежи…», до «Вставить»");
      }
      { /* списки по набору (pk_combo_filter): «ток» → токарные первыми, «411» → коды с 411, участок — по номеру и слову */
        st_check(pk_match_rank(L"4110 Токарная", L"ток") == 0 && pk_match_rank(L"4110 Токарная", L"411") == 0 &&
                     pk_match_rank(L"12 Механический участок 12", L"12") == 0 && pk_match_rank(L"12 Механический участок 12", L"мех") == 0 &&
                     pk_match_rank(L"12 Механический участок 12", L"участ") == 1 &&
                     pk_match_rank(L"Цех 3 · 31 Сварочный", L"31") == 1 && pk_match_rank(L"4233 Токарная с ЧПУ", L"чпу") == 1 &&
                     pk_match_rank(L"4130 Шлифовальная", L"фова") == 2 && pk_match_rank(L"0200 Контроль", L"ток") == -1,
                 L"подбор по набору: с начала кода или названия, с начала слова, вхождение; без регистра");
        SetWindowTextW(g_pkQOp, L"ток");
        int nOp = pk_combo_filter(g_pkQOp, 2, FALSE);
        wchar_t f0[300] = L"", fl[300] = L"";
        SendMessageW(g_pkQOp, CB_GETLBTEXT, 0, (LPARAM)f0);
        int cnt = (int)SendMessageW(g_pkQOp, CB_GETCOUNT, 0, 0);
        if (cnt > 0) SendMessageW(g_pkQOp, CB_GETLBTEXT, (WPARAM)(cnt - 1), (LPARAM)fl);
        wchar_t tx[300];
        GetWindowTextW(g_pkQOp, tx, 300);
        st_check(nOp > 0 && nOp < OPS_1C_N && wcsstr(f0, L"Токарн") && f0[0] >= L'0' && f0[0] <= L'9' && !wcscmp(tx, L"ток"),
                 L"«ток» в поле «Операция» — первая строка токарная, весь справочник не показан, набранное в поле");
        st_note(L"     «ток»: подходящих %d из %d, первая «%ls», последняя «%ls»", nOp, OPS_1C_N, f0, fl);
        SetWindowTextW(g_pkQOp, L"411");
        pk_combo_filter(g_pkQOp, 2, FALSE);
        SendMessageW(g_pkQOp, CB_GETLBTEXT, 0, (LPARAM)f0);
        st_check(!wcsncmp(f0, L"411", 3), L"«411» — первыми коды с 411");
        SetWindowTextW(g_pkQOp, L"");
        pk_combo_filter(g_pkQOp, 2, FALSE);
        st_check((int)SendMessageW(g_pkQOp, CB_GETCOUNT, 0, 0) == OPS_1C_N, L"поле пустое — снова весь справочник");
        /* участок: в домашней базе — «31 Механический участок 31»; «12» там нет — «нет в справочнике», набранное остаётся */
        SetWindowTextW(g_pkQArea, L"31");
        int nA = pk_combo_filter(g_pkQArea, 3, FALSE);
        SendMessageW(g_pkQArea, CB_GETLBTEXT, 0, (LPARAM)f0);
        BOOL a31 = nA >= 1 && !wcsncmp(f0, L"31 ", 3);
        SetWindowTextW(g_pkQArea, L"мех");
        pk_combo_filter(g_pkQArea, 3, FALSE);
        SendMessageW(g_pkQArea, CB_GETLBTEXT, 0, (LPARAM)fl);
        BOOL aMex = wcsstr(fl, L"Механ") != NULL;
        SetWindowTextW(g_pkQArea, L"12");
        int n12 = pk_combo_filter(g_pkQArea, 3, FALSE);
        wchar_t none[300] = L"", typed[300] = L"";
        SendMessageW(g_pkQArea, CB_GETLBTEXT, 0, (LPARAM)none);
        GetWindowTextW(g_pkQArea, typed, 300);
        wchar_t fixd[300];
        lstrcpynW(fixd, kPkNone, 300);
        pk_none_fix(g_pkQArea, fixd, 300);
        st_check(a31 && aMex && n12 == 0 && !wcscmp(none, kPkNone) && !wcscmp(typed, L"12") && !wcscmp(fixd, L"12"),
                 L"участок: «31» и «мех» — 31 Механический; «12» (в домашней базе нет) — «нет в справочнике», набранное «12» не теряется");
        SetWindowTextW(g_pkQArea, L"");
        { /* участок 12 — на выдуманном справочнике: «12» → «12 …» первым, раньше «Цех 3 · 12 …» */
          SzmDicts keep = g_szmDicts;
          static wchar_t ar[3][200] = {L"31 Механический участок 31", L"Цех 3 · 12 Сборка", L"12 Механический участок 12"};
          g_szmDicts.area = ar;
          g_szmDicts.na = 3;
          SetWindowTextW(g_pkQArea, L"12");
          int k12 = pk_combo_filter(g_pkQArea, 3, FALSE);
          wchar_t a0[200] = L"", a1[200] = L"";
          SendMessageW(g_pkQArea, CB_GETLBTEXT, 0, (LPARAM)a0);
          SendMessageW(g_pkQArea, CB_GETLBTEXT, 1, (LPARAM)a1);
          st_check(k12 == 2 && !wcscmp(a0, L"12 Механический участок 12") && !wcscmp(a1, L"Цех 3 · 12 Сборка"),
                   L"«12» в участке — участок 12 первым, потом «Цех 3 · 12 …»; 31 не показан");
          { /* участок справочника по вписанному (pk_area_dict): код, «<номер> » в начале, не «120», номер словом, название */
            static wchar_t ar2[3][200] = {L"31 Механический участок 31", L"12 Инструментальный участок", L"120 Склад заготовок"};
            static wchar_t cd2[3][40] = {L"31", L"12", L"120"};
            g_szmDicts.area = ar2;
            g_szmDicts.areaCode = cd2;
            g_szmDicts.na = 3;
            wchar_t f1[200], f2[200], f3[200], f4[200], f5[200];
            BOOL h1 = pk_area_dict(L"12", f1, 200), h2 = pk_area_dict(L"120", f2, 200), h3 = pk_area_dict(L"инструментальный участок", f3, 200),
                 h4 = pk_area_dict(L"12 Инструментальный участок", f4, 200), h5 = pk_area_dict(L"1", f5, 200);
            st_check(h1 && !wcscmp(f1, L"12 Инструментальный участок") && h2 && !wcscmp(f2, L"120 Склад заготовок") && !h3 &&
                         h4 && !wcscmp(f4, L"12 Инструментальный участок") && !h5 && !pk_area_dict(L"77", f5, 200),
                     L"участок по вписанному: «12» → «12 Инструментальный участок» (не «120 …»), «120» → склад, «1» и «77» — нет");
            /* «Проверить» до отправки: участка «77» нет в справочнике — предупреждение с текстом; строка — не из «Союза»
               (строки из «Союза» не проверяются), вернули участок — про «77» ни слова */
            XpTable *ot2 = &g_pk[PK_OPS];
            int r77 = -1;
            for (int r = 0; r < ot2->n && r77 < 0; r++)
              if (!pk_row_from_soyuz(PK_OPS, r) && !wcscmp(xp_cell(ot2, r, 2), L"4260 Фрезерная")) r77 = r;
            wchar_t wasA[200] = L"";
            if (r77 >= 0) lstrcpynW(wasA, xp_cell(ot2, r77, 3), 200);
            pk_check(TRUE);
            BOOL before77 = g_pkLastCheck && wcsstr(g_pkLastCheck, L"«77»");
            if (r77 >= 0) pk_put(ot2, r77, 3, L"77");
            pk_check(TRUE);
            BOOL after77 = g_pkLastCheck && wcsstr(g_pkLastCheck, L"Участка") && wcsstr(g_pkLastCheck, L"«77»") &&
                           wcsstr(g_pkLastCheck, L"Приоритетах операций");
            int warn77 = g_pkSendWarn;
            if (r77 >= 0) pk_put(ot2, r77, 3, wasA);
            pk_check(TRUE);
            BOOL gone77 = !(g_pkLastCheck && wcsstr(g_pkLastCheck, L"«77»"));
            st_check(r77 >= 0 && !before77 && after77 && warn77 >= 1 && gone77,
                     L"«Проверить» до отправки: «Участка «77» нет в справочнике «Союза»… впишите свой в «Приоритетах операций»»; вернули участок — про «77» ни слова");
            g_szmDicts.areaCode = keep.areaCode;
          }
          g_szmDicts = keep;
          SetWindowTextW(g_pkQArea, L"");
          pk_combo_filter(g_pkQArea, 3, FALSE);
        }
        /* ячейка «Операция»: набор «ток» — список раскрыт и по набору, Enter — первая подходящая */
        wchar_t was[300];
        lstrcpynW(was, xp_cell(&g_pk[PK_OPS], 0, 2), 300);
        pk_edit_begin(0, 2);
        COMBOBOXINFO ci;
        memset(&ci, 0, sizeof(ci));
        ci.cbSize = sizeof(ci);
        GetComboBoxInfo(g_pkCombo, &ci);
        SetWindowTextW(g_pkCombo, L"то");
        SendMessageW(ci.hwndItem, EM_SETSEL, 2, 2);
        SendMessageW(ci.hwndItem, WM_CHAR, L'к', 0); /* как с клавиатуры: фильтр по «ток» */
        BOOL dropped = SendMessageW(g_pkCombo, CB_GETDROPPEDSTATE, 0, 0) != 0;
        wchar_t c0[300] = L"";
        SendMessageW(g_pkCombo, CB_GETLBTEXT, 0, (LPARAM)c0);
        int ccnt = (int)SendMessageW(g_pkCombo, CB_GETCOUNT, 0, 0);
        SendMessageW(ci.hwndItem, WM_KEYDOWN, VK_RETURN, 0);
        wchar_t got[300];
        lstrcpynW(got, xp_cell(&g_pk[PK_OPS], 0, 2), 300);
        if (g_pkEditRow >= 0) pk_edit_end(FALSE); /* Enter ушёл на следующую строку — без правки */
        st_check(dropped && ccnt < OPS_1C_N && wcsstr(c0, L"Токарн") && !wcscmp(got, c0),
                 L"ячейка «Операция»: набор «ток» — список раскрыт и по набору, Enter — первая подходящая в ячейку");
        st_note(L"     ячейка: раскрыт %d, строк %d, первая «%ls», в ячейке «%ls»", dropped, ccnt, c0, got);
        pk_put(&g_pk[PK_OPS], 0, 2, was); /* дальше стенд ждёт прежнюю операцию */
        if (g_pkSheet == PK_OPS) pk_fill(FALSE);
      }
      pk_edit_end(FALSE);
      /* строки таблицы дописывает очередь окна (справочники из базы → pk_equip_fill → pk_fill): ждём её, не таймер */
      for (int t = 0; t < 50 && ListView_GetItemCount(g_pkList) != 7; t++) st_pump(100);
      st_check(ListView_GetItemCount(g_pkList) == 7, L"в таблице листа «Операции» семь строк (с транспортированиями и сборкой)");
      st_note(L"     строк в таблице «Операции»: %d, в данных %d", ListView_GetItemCount(g_pkList), g_pk[PK_OPS].n);
      { /* деталь из 8 операций по чертежу (как на снимке пользователя 30.10; обозначение выдуманное): участок у
           каждой, станок у токарной, транспортирование сразу, номера шагом 5 — и после удаления и правки участка */
        XpTable *ot = &g_pk[PK_OPS];
        static const wchar_t *const ops8[8] = {L"4280 Отрезная",  L"4110 Токарная",  L"5000 Термическая обработка", L"0108 Слесарная",
                                              L"4110 Токарная",  L"0108 Слесарная", L"0180 Маркирование",         L"0200 Контроль"};
        for (int i = 0; i < 8; i++) {
          wchar_t num[8];
          _snwprintf(num, 8, L"%03d", (i + 1) * 5);
          xp_row(ot);
          ot = &g_pk[PK_OPS];
          pk_put(ot, ot->n - 1, 0, L"ТСТ.150.001");
          pk_put(ot, ot->n - 1, 1, num);
          pk_put(ot, ot->n - 1, 2, ops8[i]);
        }
        pk_changed(); /* как после чтения чертежа (pk_stamp.c) */
        ot = &g_pk[PK_OPS];
        int rows = 0, noArea = 0, tr = 0, tok = 0, tokEq = 0, seqOk = 1, mark = -1;
        int nums[40], nn = 0;
        for (int r = 0; r < ot->n; r++) {
          if (wcscmp(xp_cell(ot, r, 0), L"ТСТ.150.001")) continue;
          rows++;
          const wchar_t *o = xp_cell(ot, r, 2);
          if (!xp_cell(ot, r, 3)[0]) noArea++;
          if (pk_is_transport(o)) tr++;
          if (!wcscmp(o, L"4110 Токарная")) tok++, tokEq += xp_cell(ot, r, 4)[0] != 0;
          if (!wcscmp(o, L"0180 Маркирование")) mark = r;
          if (nn < 40) nums[nn++] = _wtoi(xp_cell(ot, r, 1));
        }
        for (int i = 0; i < nn; i++) { /* номера — 5, 10, … без пропусков и повторов */
          int seen = 0;
          for (int k = 0; k < nn; k++) seen += nums[k] == (i + 1) * 5;
          if (seen != 1) seqOk = 0;
        }
        st_note(L"     деталь из 8 операций: строк %d (транспортирований %d), без участка %d, токарных со станком %d из %d, маркирование — «%ls»",
                rows, tr, noArea, tokEq, tok, mark >= 0 ? xp_cell(ot, mark, 3) : L"-");
        st_check(rows == 8 + tr && tr >= 2 && noArea == 0 && tok == 2 && tokEq == 2 && seqOk && mark >= 0 && xp_cell(ot, mark, 3)[0],
                 L"8 операций по чертежу: участок у каждой (маркирование — как у предыдущей), станок у токарных, "
                 L"«Транспортирование» сразу при смене участка, номера 005… шагом 5");
        /* «Проверить»: маркирование — «как у предыдущей», в списке */
        BOOL markSrc = mark >= 0 && pk_area_sourced(xp_cell(ot, mark, 2), xp_cell(ot, mark, 4));
        st_check(!markSrc, L"маркирование без участка в таблице и в «Союзе» — помечено «участок как у предыдущей — проверить»");
        /* удалить термообработку (была 015) — номера заново 005…, лишних транспортирований нет */
        for (int r = 0; r < ot->n; r++)
          if (!wcscmp(xp_cell(ot, r, 0), L"ТСТ.150.001") && !wcscmp(xp_cell(ot, r, 2), L"5000 Термическая обработка")) {
            pk_del_row(ot, r);
            break;
          }
        pk_changed(); /* как «Удалить» (pk_delete_selected) */
        ot = &g_pk[PK_OPS];
        int rows2 = 0, tr2 = 0;
        nn = 0;
        for (int r = 0; r < ot->n; r++)
          if (!wcscmp(xp_cell(ot, r, 0), L"ТСТ.150.001")) {
            rows2++;
            tr2 += pk_is_transport(xp_cell(ot, r, 2));
            if (nn < 40) nums[nn++] = _wtoi(xp_cell(ot, r, 1));
          }
        int seq2 = 1;
        for (int i = 0; i < nn; i++) {
          int seen = 0;
          for (int k = 0; k < nn; k++) seen += nums[k] == (i + 1) * 5;
          if (seen != 1) seq2 = 0;
        }
        st_check(rows2 == 7 + tr2 && seq2, L"после удаления операции номера детали снова 005… шагом 5 без пропусков");
        st_note(L"     после удаления: строк %d (транспортирований %d), последний номер %03d", rows2, tr2, rows2 * 5);
        /* вторая токарная — на участок слесарной: транспортирования вокруг неё стали лишними — убраны */
        int tok2 = -1, cntTok = 0;
        for (int r = 0; r < ot->n; r++)
          if (!wcscmp(xp_cell(ot, r, 0), L"ТСТ.150.001") && !wcscmp(xp_cell(ot, r, 2), L"4110 Токарная") && ++cntTok == 2) tok2 = r;
        wchar_t slesArea[200] = L"";
        for (int r = 0; r < ot->n && !slesArea[0]; r++)
          if (!wcscmp(xp_cell(ot, r, 0), L"ТСТ.150.001") && !wcscmp(xp_cell(ot, r, 2), L"0108 Слесарная")) lstrcpynW(slesArea, xp_cell(ot, r, 3), 200);
        if (tok2 >= 0) pk_put(ot, tok2, 3, slesArea);
        pk_changed(); /* как правка ячейки «Участок» */
        ot = &g_pk[PK_OPS];
        int tr3 = 0;
        for (int r = 0; r < ot->n; r++) tr3 += !wcscmp(xp_cell(ot, r, 0), L"ТСТ.150.001") && pk_is_transport(xp_cell(ot, r, 2));
        st_check(tok2 >= 0 && slesArea[0] && tr3 == tr2 - 2, L"участок токарной сменили на участок соседних — два транспортирования вокруг неё убраны");
        st_note(L"     транспортирований: было %d, после правки участка %d", tr2, tr3);
        /* деталь — прочь: дальше стенд ждёт прежние семь строк */
        for (int r = ot->n - 1; r >= 0; r--)
          if (!wcscmp(xp_cell(ot, r, 0), L"ТСТ.150.001")) pk_del_row(ot, r);
        pk_changed();
        if (g_pkSheet == PK_OPS) pk_fill(FALSE);
        st_check(g_pk[PK_OPS].n == 7, L"чужая деталь убрана — на листе снова семь строк");
        /* поле правки не остаётся поверх таблицы, когда список перестраивается */
        pk_edit_begin(0, 3);
        BOOL shown = g_pkCombo && IsWindowVisible(g_pkCombo);
        pk_fill(FALSE);
        st_check(shown && !IsWindowVisible(g_pkCombo) && g_pkEditRow < 0,
                 L"список перестроили во время правки ячейки — правка закончена, поле не висит над шапкой");
      }
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
  st_check(!wcscmp(g_stClip, L"4280 Отрезная") && g_szTextOnly,
           L"по умолчанию — текст для «Создать операцию» (в сквозной ТП копию не вставить)");
  st_shot(L"2a-text-default.png");
  sz_command(ID_SZ_MODE);
  st_check(!g_szTextOnly && !wcscmp(g_stClip, L"4280 Отрезная") && g_szR.it[iTok].guid[0],
           L"«Аналог»: у отрезной аналога нет — остаётся текст, у токарной GUID есть");

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
      /* дерево ТП в базе: каждая операция (TSOperation) — дочерняя версии ТП (TSVersion), не варианта;
         «Способ комплектования» ТП — на варианте ТП в версии (OnTSVVariant), как ставит пакет 1.0.0.7 */
      n = card_query(dbc,
                     L"SELECT COUNT(*), N'', N'', SUM(CASE WHEN tp.NameKey=N'TSVersion' THEN 1 ELSE 0 END), 0 "
                     L"FROM InfoObjects AS o JOIN Templates AS t ON t.TemplateId=o.TemplateId AND t.NameKey=N'TSOperation' "
                     L"JOIN InfoObjects AS p ON p.InfoObjectId=o.ParentId JOIN Templates AS tp ON tp.TemplateId=p.TemplateId "
                     L"WHERE o.Erased=0",
                     rows, 1, err, 300);
      st_check(n == 1 && rows[0].n1 > 0 && rows[0].n2 == rows[0].n1,
               L"операции ТП в базе — дочерние версии ТП (TSVersion), не варианта: ParentId каждой — версия");
      n = card_query(dbc,
                     L"SELECT COUNT(*), N'', N'', SUM(CASE WHEN nv.NameKey=N'OnTSVVariant' THEN 1 ELSE 0 END), "
                     L"SUM(CASE WHEN nv.NameKey=N'OnTSOperationVariant' THEN 1 ELSE 0 END) "
                     L"FROM InfoObjects AS o JOIN Templates AS t ON t.TemplateId=o.TemplateId AND t.NameKey=N'TechnologicalStructure' "
                     L"JOIN InfoObjectAttributes AS a ON a.OwnerId=o.InfoObjectId AND a.Outdated=0 "
                     L"JOIN NameKeys AS nk ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'KindOfStorageItems' "
                     L"JOIN NamedValues AS nv ON nv.NamedValueId=a.Link WHERE o.Erased=0",
                     rows, 1, err, 300);
      if (n == 1)
        st_note(L"     способ комплектования ТП в базе: всего %ld, на варианте ТП в версии %ld, на операции в варианте %ld "
                L"(старый пакет) — после установки 1.0.0.7 новые ТП должны идти в первый столбец",
                rows[0].n1, rows[0].n2, rows[0].n3);

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
    st_newop(g_szR.it[iOkr].dir, ver, g_szR.it[iOkr].num); /* номер окрасочной — какой есть в маршруте */
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
