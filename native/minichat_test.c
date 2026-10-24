/* minichat_test.c — проверка мини-чата у помощника (ai_minichat.c) без запуска
   CursorPad: ни хуков, ни обновлений, ни данных пользователя. Окна создаются на
   отдельном невидимом рабочем столе, рисуются настоящим кодом через WM_PRINTCLIENT
   и сохраняются в PNG; беседа выдуманная, модель не вызывается. Заодно проверяет
   «Новую беседу»: черту в ai_chat.txt, её чтение, «Вернуть» и команду «/новая»,
   и что остановленный вопрос возвращается в поле мини-чата.
   Из корня репозитория (Git Bash, WinLibs; native/CursorPadOcr.exe — после build.sh):
     (cd native && windres cursorpad.rc -O coff -o ../build/minichat_test.res)
     gcc -O2 -Wall -Werror -Wno-unknown-pragmas -finput-charset=UTF-8 -o build/minichat_test.exe \
       native/minichat_test.c build/minichat_test.res -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 \
       -luuid -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 -lcomdlg32 -lmsimg32 \
       -Wl,--subsystem,windows -municode -static
     build/minichat_test.exe build/minichat   → снимки *.png и report.txt; код 0 — всё сошлось */
#define wWinMain cursorpad_wWinMain
#include "cursorpad.c"
#undef wWinMain

static const wchar_t *g_mctDir;
static int g_mctFail;
static wchar_t g_mctReport[8000];

static void mct_check(BOOL ok, const wchar_t *what) {
  size_t n = wcslen(g_mctReport);
  _snwprintf(g_mctReport + n, 8000 - n, L"%ls %ls\r\n", ok ? L"ok  " : L"FAIL", what);
  if (!ok) g_mctFail++;
}

static void mct_pump(void) {
  MSG m;
  while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}

static void mct_write(const wchar_t *name, const void *data, DWORD len) {
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%ls\\%ls", g_mctDir, name);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wr = 0;
  WriteFile(h, data, len, &wr, NULL);
  CloseHandle(h);
}

/* окно мини-чата с полем ввода — в PNG */
static void mct_shot(const wchar_t *name) {
  mct_pump();
  RECT rc;
  GetClientRect(g_amWnd, &rc);
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
    mct_check(FALSE, name);
    if (mem) DeleteDC(mem);
    return;
  }
  HGDIOBJ old = SelectObject(mem, bmp);
  SendMessageW(g_amWnd, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
  RECT er;
  GetWindowRect(g_amIn, &er);
  MapWindowPoints(NULL, g_amWnd, (POINT *)&er, 2);
  POINT org;
  SetViewportOrgEx(mem, er.left, er.top, &org);
  SendMessageW(g_amIn, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
  SetViewportOrgEx(mem, org.x, org.y, NULL);
  GdiFlush();
  SelectObject(mem, old);
  IWICImagingFactory *f = ac_factory();
  IWICBitmap *wb = NULL;
  BYTE *png = NULL;
  DWORD len = 0;
  if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromHBITMAP(f, bmp, NULL, WICBitmapIgnoreAlpha, &wb)) &&
      ac_png(f, (IWICBitmapSource *)wb, (UINT)w, (UINT)h, &png, &len)) {
    mct_write(name, png, len);
    free(png);
  }
  mct_check(len > 0, name);
  if (wb) wb->lpVtbl->Release(wb);
  if (f) f->lpVtbl->Release(f);
  DeleteObject(bmp);
  DeleteDC(mem);
}

/* миниатюра «чертежа» вместо настоящей картинки: модель с картинками не нужна */
static void mct_fake_image(void) {
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = 80;
  bi.bmiHeader.biHeight = -56;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  BYTE *bits = NULL;
  HDC mem = CreateCompatibleDC(NULL);
  HBITMAP hb = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
  if (!hb) {
    DeleteDC(mem);
    return;
  }
  HGDIOBJ old = SelectObject(mem, hb);
  RECT all = {0, 0, 80, 56};
  FillRect(mem, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
  SelectObject(mem, GetStockObject(BLACK_PEN));
  SelectObject(mem, GetStockObject(NULL_BRUSH));
  Rectangle(mem, 2, 2, 78, 54);
  Rectangle(mem, 12, 18, 56, 34);
  Ellipse(mem, 56, 20, 68, 32);
  MoveToEx(mem, 6, 26, NULL);
  LineTo(mem, 74, 26);
  Rectangle(mem, 50, 42, 78, 54);
  GdiFlush();
  for (int i = 0; i < 80 * 56; i++) bits[i * 4 + 3] = 255; /* у WIC-миниатюры альфа полная */
  SelectObject(mem, old);
  DeleteDC(mem);
  g_acThumbBmp = hb;
  g_acImg = (BYTE *)malloc(16);
  g_acImgLen = 16;
  ac_relayout();
}

static BOOL mct_file_has(const wchar_t *path, const wchar_t *what) {
  wchar_t *t = ac_read_utf8(path, 1 << 20);
  BOOL ok = t && wcsstr(t, what);
  free(t);
  return ok;
}

/* есть ли слова в ленте полного чата (RichEdit) */
static BOOL mct_log_has(const wchar_t *what) {
  int n = GetWindowTextLengthW(g_acLog);
  wchar_t *t = n > 0 ? (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t)) : NULL;
  if (t) GetWindowTextW(g_acLog, t, n + 1);
  BOOL ok = t && wcsstr(t, what);
  free(t);
  return ok;
}

static BOOL mct_text_is(HWND h, const wchar_t *what) {
  wchar_t t[200] = L"";
  GetWindowTextW(h, t, 200);
  return !wcscmp(t, what);
}

/* лента полного чата — в PNG (RichEdit рисует себя по WM_PRINT) */
static void mct_log_shot(const wchar_t *name) {
  mct_pump();
  RECT rc;
  GetClientRect(g_acLog, &rc);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = rc.right;
  bi.bmiHeader.biHeight = -rc.bottom;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HDC mem = CreateCompatibleDC(NULL);
  HBITMAP bmp = mem && rc.right > 0 && rc.bottom > 0 ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
  DWORD len = 0;
  if (bmp) {
    HGDIOBJ old = SelectObject(mem, bmp);
    SendMessageW(g_acLog, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_ERASEBKGND);
    GdiFlush();
    SelectObject(mem, old);
    IWICImagingFactory *f = ac_factory();
    IWICBitmap *wb = NULL;
    BYTE *png = NULL;
    if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromHBITMAP(f, bmp, NULL, WICBitmapIgnoreAlpha, &wb)) &&
        ac_png(f, (IWICBitmapSource *)wb, (UINT)rc.right, (UINT)rc.bottom, &png, &len)) {
      mct_write(name, png, len);
      free(png);
    }
    if (wb) wb->lpVtbl->Release(wb);
    if (f) f->lpVtbl->Release(f);
    DeleteObject(bmp);
  }
  if (mem) DeleteDC(mem);
  mct_check(len > 0, name);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
  (void)prev;
  (void)cmd;
  (void)show;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv || argc < 2) return 2;
  g_mctDir = argv[1];
  CreateDirectoryW(g_mctDir, NULL);
  /* свой рабочий стол: окна проверки не мелькают у человека на экране */
  HDESK desk = CreateDesktopW(L"CursorPadMiniChatTest", NULL, NULL, 0, GENERIC_ALL, NULL);
  if (!desk || !SetThreadDesktop(desk)) return 3;
  g_inst = inst;
  enable_dpi();
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);
  _snwprintf(g_dataDir, MAX_PATH, L"%ls\\data", g_mctDir);
  CreateDirectoryW(g_dataDir, NULL);
  wchar_t hist[MAX_PATH], ini[MAX_PATH];
  ac_hist_path(hist);
  DeleteFileW(hist);
  _snwprintf(ini, MAX_PATH, L"%ls\\ai_avatar.ini", g_dataDir); /* помощника не звать: проверяем окна чата */
  WritePrivateProfileStringW(L"Avatar", L"Visible", L"0", ini);
  g_aiUiThread = GetCurrentThreadId();
  g_fontUi = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
  g_fontBody = g_fontUi;

  ac_mini_show();
  mct_check(g_amWnd && g_acWnd, L"мини-чат и окно беседы созданы");
  if (!g_amWnd) return 4;
  ac_status(L"");
  mct_shot(L"1-empty.png");

  /* длинный ответ: целиком, лента — к его началу, полоса прокрутки */
  ac_push(0, L"Посмотри чертёж вала и скажи, как его обрабатывать");
  ac_push(1, L"## Маршрут обработки вала\n\n"
             L"1. **Заготовка** — прокат круглый Ø65 мм, сталь 40Х ГОСТ 4543, длина 412 мм с припуском на подрезку торцов.\n"
             L"2. **Токарная черновая** — подрезать торцы в размер 405 мм, зацентровать, точить ступени с припуском 1,5 мм на сторону.\n"
             L"3. **Термообработка** — улучшение до 269…302 HB, после неё проверить твёрдость на двух шейках.\n"
             L"4. **Токарная чистовая** — точить шейки под подшипники Ø40k6 с припуском 0,3 мм под шлифование, фаски 1×45°, канавки для выхода шлифовального круга.\n"
             L"5. **Фрезерная** — шпоночный паз 12N9 на длине 56 мм, глубина 5 мм, симметричность 0,03 относительно оси.\n"
             L"6. **Сверлильная** — резьбовое отверстие М10-7Н в торце на глубину 22 мм под рым-болт.\n"
             L"7. **Шлифовальная** — шейки Ø40k6, Ra 0,8; биение относительно оси центров не более 0,016 мм.\n"
             L"8. **Контроль** — размеры по чертежу, шероховатость, биение, маркировка обозначения и номера плавки.\n\n"
             L"Что не прочиталось на листе — допуск на длину ступени Ø52 и покрытие: уточните, и я поправлю маршрут. "
             L"Если вал идёт партией, черновую токарную и фрезерную лучше вынести на станки с ЧПУ — время на установку сократится примерно вдвое.");
  ac_redraw();
  mct_check(g_amMaxScroll > 0, L"длинный ответ не влез — есть прокрутка");
  mct_check(g_amScroll > 0, L"лента встала к началу ответа, а не к хвосту");
  int last = g_amCount - 1;
  mct_check(last >= 0 && g_amItems[last].kind == AM_AI && wcsstr(g_amItems[last].text, L"примерно вдвое") != NULL,
            L"ответ показан целиком, до последних слов");
  mct_shot(L"2-long-answer-top.png");
  am_scroll_by(-100000);
  mct_shot(L"3-long-answer-bottom.png");

  /* ИИ думает: «Стоп» вместо «отправить»; остановили — вопрос вернулся в поле мини-чата */
  ac_push(0, L"А какие допуски на шейки?"); /* как ac_send, но без модели и поиска */
  g_acId = 1;
  ac_busy(TRUE);
  ac_status(L"Думаю… первый ответ после запуска модели может грузиться до минуты");
  SetWindowTextW(g_amIn, L"");
  mct_check(am_busy(), L"пока ИИ думает, мини-чат занят");
  mct_shot(L"4-busy-stop.png");
  g_acId = 0;
  ac_busy(FALSE);
  ac_unsend();
  ac_redraw();
  ac_status(L"Остановлено, вопрос вернулся в поле ввода");
  mct_pump();
  wchar_t back[100] = L"";
  GetWindowTextW(g_amIn, back, 100);
  mct_check(!wcscmp(back, L"А какие допуски на шейки?") && !GetWindowTextLengthW(g_acIn),
            L"остановленный вопрос вернулся в поле мини-чата");
  mct_shot(L"5-stopped.png");

  /* картинка к вопросу */
  SetWindowTextW(g_amIn, L"");
  mct_fake_image();
  ac_status(L"Картинка прикреплена к следующему вопросу. Клик по миниатюре — убрать.");
  mct_check(g_amImg.bottom > g_amImg.top, L"полоса картинки над полем ввода");
  mct_shot(L"6-image.png");

  /* план файловой задачи */
  ac_drop_img();
  ac_push(0, L"Разложи чертежи с рабочего стола по папкам");
  ac_push(1, L"План: 14 файлов PDF → C:\\Работа\\Чертежи\\Валы, 6 файлов → C:\\Работа\\Чертежи\\Корпуса. Ничего ещё не изменено.");
  g_acPlanOn = TRUE;
  ac_plan_ui(TRUE);
  ac_status(L"");
  mct_shot(L"7-plan.png");
  g_acPlanOn = FALSE;
  ac_plan_ui(FALSE);

  /* «Новая беседа»: черта в файле, «Вернуть», чтение черты, «/новая» */
  int before = g_acN;
  mct_check(ac_new_topic() && g_acNewAt == before && g_acContextStart == before, L"новая беседа: ИИ не видит прежних реплик");
  mct_check(mct_file_has(hist, L"2\t") && mct_file_has(hist, L"примерно вдвое"), L"ai_chat.txt хранит прежние реплики и черту");
  mct_check(mct_log_has(L"Прежняя сохранена — вернуть · показать") && !mct_log_has(L"примерно вдвое"),
            L"полный чат после ⟳ — тоже с чистого листа, прежнее скрыто строкой «вернуть · показать»");
  mct_check(ac_new_topic_undo_ready(), L"до нового вопроса беседу можно вернуть");
  mct_shot(L"8-new-topic.png");
  mct_log_shot(L"10-full-new-topic.png");
  ac_log_link_do(1); /* «показать» */
  mct_check(mct_log_has(L"примерно вдвое") && mct_log_has(L"новая беседа: что выше"),
            L"«показать» — в полном чате вся история с чертой");
  mct_log_shot(L"11-full-shown.png");
  ac_log_link_do(2); /* «скрыть» */
  mct_check(!mct_log_has(L"примерно вдвое"), L"«скрыть» — снова только новая беседа");
  ac_new_topic_undo();
  mct_check(g_acNewAt == 0 && !ac_new_topic_undo_ready(), L"«Вернуть» вернуло прежнюю беседу");
  mct_shot(L"9-undo.png");
  SetWindowTextW(g_acIn, L"/новая");
  ac_send();
  mct_check(g_acNewAt == before && !GetWindowTextLengthW(g_acIn), L"команда «/новая» начинает новую беседу");
  ac_push(0, L"Новый вопрос");
  ac_push(1, L"Новый ответ");
  ac_clear_turns();
  g_acHistLoaded = FALSE;
  ac_hist_load();
  mct_check(g_acN == before + 2 && g_acNewAt == before && g_acContextStart >= before,
            L"после перезапуска черта на месте: ИИ помнит только новую беседу");
  for (int i = 0; i < AM_TURNS; i++) ac_push(i & 1, i & 1 ? L"Ответ" : L"Вопрос");
  ac_redraw();
  mct_check(g_amCount > 0 && g_amItems[0].kind == AM_MORE && g_amItems[1].turn == g_acN - AM_TURNS,
            L"длинная беседа: в ленте последние реплики, о ранних — строка сверху");

  /* «Новая беседа» полного чата — то же, что ⟳: обе ленты чистые, ничего не стёрто */
  SendMessageW(g_acWnd, WM_COMMAND, MAKEWPARAM(ID_AC_CLEAR, BN_CLICKED), (LPARAM)g_acClear);
  mct_pump();
  mct_check(g_acNewAt == g_acN && g_amCount == 1 && g_amItems[0].kind == AM_HINT && !mct_log_has(L"Новый ответ") &&
                mct_file_has(hist, L"Новый ответ"),
            L"кнопка полного чата: обе ленты с чистого листа, история на месте");

  /* одно окно на беседу: ⤢ закрывает мини-чат, черновик и картинка переезжают — и обратно */
  SetWindowTextW(g_amIn, L"черновик вопроса");
  mct_fake_image();
  aichat_show_full();
  mct_pump();
  mct_check(!IsWindowVisible(g_amWnd) && IsWindowVisible(g_acWnd) && mct_text_is(g_acIn, L"черновик вопроса") &&
                !GetWindowTextLengthW(g_amIn) && IsWindowVisible(g_acThumb),
            L"⤢: мини-чат закрыт, черновик и картинка — в полном чате");
  ac_mini_show();
  mct_pump();
  mct_check(IsWindowVisible(g_amWnd) && !IsWindowVisible(g_acWnd) && mct_text_is(g_amIn, L"черновик вопроса") &&
                !GetWindowTextLengthW(g_acIn) && g_amImg.bottom > g_amImg.top,
            L"щелчок по помощнику: полный чат спрятан, черновик и картинка — в мини-чате");
  ac_drop_img();
  SetWindowTextW(g_amIn, L"");

  /* маршрут по чертежу: черновик, потом ответ на его месте — мини-чат показывает ответ */
  ac_push(0, L"Маршрут по чертежу");
  g_acTpSeq = 7;
  AcTpDone *draft = (AcTpDone *)calloc(1, sizeof(AcTpDone));
  draft->text = _wcsdup(L"Черновик по Tesseract: 005 Заготовительная, 010 Токарная.");
  draft->seq = 7;
  ac_tp_done(TRUE, draft);
  int n0 = g_acN;
  AcTpDone *done = (AcTpDone *)calloc(1, sizeof(AcTpDone));
  done->text = _wcsdup(L"Маршрут: 005 Заготовительная, 010 Токарная черновая, 015 Термическая, 020 Токарная "
                       L"чистовая, 025 Фрезерная, 030 Шлифовальная, 035 Контрольная. Дочитано моделью.");
  done->seq = 7;
  ac_tp_done(FALSE, done);
  mct_pump();
  last = g_amCount - 1;
  mct_check(g_acN == n0 && last >= 0 && wcsstr(g_amItems[last].text, L"Дочитано моделью") &&
                !wcsstr(g_amItems[last].text, L"Черновик") && g_amSeenText == g_acTurns[g_acN - 1].text &&
                mct_log_has(L"Дочитано моделью") && !mct_log_has(L"Черновик по Tesseract"),
            L"ответ на маршрут встал на место черновика — и в мини-чате, и в полном");
  mct_shot(L"12-route-answer.png");

  char out[24000];
  int k = WideCharToMultiByte(CP_UTF8, 0, g_mctReport, -1, out, sizeof(out), NULL, NULL);
  mct_write(L"report.txt", out, k > 0 ? (DWORD)k - 1 : 0);
  LocalFree(argv);
  return g_mctFail ? 1 : 0;
}
