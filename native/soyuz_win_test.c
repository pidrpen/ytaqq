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
     build/soyuz_win_test.exe build/soyuz   → снимки *.png и report.txt; код 0 — всё сошлось */
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
static void st_shot(const wchar_t *name) {
  st_pump(0);
  RECT rc;
  GetClientRect(g_szWnd, &rc);
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
  SendMessageW(g_szWnd, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
  StPrint p = {mem};
  EnumChildWindows(g_szWnd, st_print_child, (LPARAM)&p);
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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
  (void)prev, (void)cmd, (void)show;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv || argc < 2) return 2;
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
  TnRoute *tr = (TnRoute *)malloc(sizeof(TnRoute));
  static wchar_t txt[9000];
  tn_route(L"Удлинитель вала", L"Круг 50 ГОСТ 2590-2006 / 45 ГОСТ 1050-2013",
           L"Ra 3,2\nRa 0,8\nØ30 -0,02\n-0,04\nM12-6H\n2 отв. Ø4+0,03\n1. 30...34 HRC\n"
           L"2. Общие допуски по ГОСТ 30893.2-mK\n3. Покрытие: аэрозольный лак\n",
           tr);
  int n0 = _snwprintf(txt, 9000, L"Лист: УВ-01.pdf · АБВГ.711141.001\n");
  tn_text(tr, txt + n0, 9000 - n0);
  free(tr);
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
  st_check(!wcscmp(g_stClip, g_szR.it[0].guid), L"пришёл аналог — в буфере его GUID");

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

  /* «Текст» — та же строка текстом, как для 1С */
  sz_command(ID_SZ_MODE);
  st_pump(50);
  st_check(!wcscmp(g_stClip, L"4110 Токарная"), L"«Текст»: в буфере «4110 Токарная»");
  st_note(L"     в буфере: «%ls», режим текст %d, строка %d", g_stClip, g_szTextOnly, g_szR.cur);
  st_shot(L"4-text.png");
  sz_command(ID_SZ_MODE);
  /* «Копировать» у поля — в буфере поле, Ctrl+V очередь не двигает; «В буфер» — вернуть */
  sz_command(ID_SZ_COPY + SZF_CODE);
  st_check(!wcscmp(g_stClip, L"4110") && g_szClip == 1, L"«Копировать» у кода — в буфере «4110»");
  st_shot(L"5-field.png");
  sz_command(ID_SZ_PUT);
  st_check(!wcscmp(g_stClip, g_szR.it[1].guid) && g_szClip == 0, L"«В буфер» вернул операцию");

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
