/* avatar_life_test.c — стенд помощника мини-ИИ (в CI не входит): режим «Факты и анекдоты»
   без модели, облики и их кадры, меню и пузыри. Как design_shots_test.c: окна создаются
   на отдельном невидимом рабочем столе и рисуются настоящим кодом, данные — в папке стенда
   (SHGetFolderPath подменён), системные курсоры и горячие клавиши не трогаются, MessageBox
   не показывается. Модель не вызывается: ответ «модели» подкладывается сообщением WM_AC_FACT.
   Из корня репозитория (Git Bash, WinLibs; native/CursorPadOcr.exe — после build.sh):
     (cd native && windres cursorpad.rc -O coff -o ../build/avatar_life_test.res)
     gcc -O2 -Wall -Werror -Wno-unknown-pragmas -finput-charset=UTF-8 -o build/avatar_life_test.exe \
       native/avatar_life_test.c build/avatar_life_test.res -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 \
       -luuid -lcomctl32 -lwinhttp -ladvapi32 -lodbc32 -lcrypt32 -lcomdlg32 -lmsimg32 \
       -Wl,--subsystem,windows -municode -static
     build/avatar_life_test.exe build/avatar_life   → снимки *.png и report.txt (строки FAIL — ошибки) */
#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>

static wchar_t g_alRoot[MAX_PATH];
static int WINAPI al_msgbox(HWND h, LPCWSTR t, LPCWSTR c, UINT f) { (void)h; (void)t; (void)c; (void)f; return IDOK; }
static BOOL WINAPI al_hotkey(HWND h, int id, UINT m, UINT vk) {
  (void)h; (void)id; (void)m; (void)vk;
  SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);
  return FALSE;
}
static BOOL WINAPI al_setcur(HCURSOR c, DWORD id) { (void)c; (void)id; return TRUE; }
static BOOL WINAPI al_spi(UINT a, UINT b, PVOID c, UINT d) {
  if (a == SPI_SETCURSORS) return TRUE;
  return SystemParametersInfoW(a, b, c, d);
}
static HRESULT WINAPI al_folder(HWND h, int csidl, HANDLE tok, DWORD fl, LPWSTR out) {
  (void)h; (void)csidl; (void)tok; (void)fl;
  lstrcpynW(out, g_alRoot, MAX_PATH);
  return S_OK;
}
/* человек «за компьютером»: настоящий ввод ночью не идёт, а помощник молчит, когда никого нет */
static DWORD g_alInputAt;
static BOOL WINAPI al_lastinput(PLASTINPUTINFO li) {
  li->dwTime = g_alInputAt ? g_alInputAt : GetTickCount();
  return TRUE;
}
#define MessageBoxW(h, t, c, f) al_msgbox(h, t, c, f)
#define RegisterHotKey(h, i, m, v) al_hotkey(h, i, m, v)
#define SetSystemCursor(c, i) al_setcur(c, i)
#define SystemParametersInfoW(a, b, c, d) al_spi(a, b, c, d)
#define SHGetFolderPathW(h, c, t, f, o) al_folder(h, c, t, f, o)
#define GetLastInputInfo(li) al_lastinput(li)
/* курсор стенда: на невидимом рабочем столе SetCursorPos не действует */
static POINT g_alCursor = {5, 5};
static BOOL WINAPI al_cursor(LPPOINT p) {
  *p = g_alCursor;
  return TRUE;
}
#define GetCursorPos(p) al_cursor(p)
#define wWinMain cursorpad_wWinMain
#include "cursorpad.c"
#undef wWinMain

static const wchar_t *g_alDir;
static wchar_t g_alReport[32000];
static int g_alFails;

static void al_log(const wchar_t *line) {
  size_t n = wcslen(g_alReport);
  _snwprintf(g_alReport + n, 32000 - n, L"%ls\r\n", line);
}

static void al_check(BOOL ok, const wchar_t *what) {
  wchar_t line[400];
  _snwprintf(line, 400, L"%ls %ls", ok ? L"ok  " : L"FAIL", what);
  al_log(line);
  if (!ok) g_alFails++;
}

static void al_pump(int ms) {
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

static void al_write(const wchar_t *name, const void *data, DWORD len) {
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%ls\\%ls", g_alDir, name);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wr = 0;
  WriteFile(h, data, len, &wr, NULL);
  CloseHandle(h);
}

static const GUID kAlBgr = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0e}};

/* premultiplied BGRA поверх цвета bg — в PNG */
static void al_png(const wchar_t *name, const BYTE *pm, int W, int H, COLORREF bg) {
  BYTE *o = (BYTE *)malloc((size_t)W * H * 4);
  if (!o) return;
  for (size_t i = 0; i < (size_t)W * H; i++) {
    const BYTE *s = pm + i * 4;
    int ia = 255 - s[3];
    o[i * 4] = (BYTE)(s[0] + GetBValue(bg) * ia / 255);
    o[i * 4 + 1] = (BYTE)(s[1] + GetGValue(bg) * ia / 255);
    o[i * 4 + 2] = (BYTE)(s[2] + GetRValue(bg) * ia / 255);
    o[i * 4 + 3] = 255;
  }
  IWICImagingFactory *f = ac_factory();
  IWICBitmap *wb = NULL;
  BYTE *png = NULL;
  DWORD len = 0;
  if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromMemory(f, (UINT)W, (UINT)H, &kAlBgr, (UINT)W * 4, (UINT)W * H * 4, o, &wb)) &&
      ac_png(f, (IWICBitmapSource *)wb, (UINT)W, (UINT)H, &png, &len)) {
    al_write(name, png, len);
    free(png);
  }
  wchar_t line[300];
  _snwprintf(line, 300, L"%ls снимок %ls %d×%d", len ? L"ok  " : L"FAIL", name, W, H);
  al_log(line);
  if (!len) g_alFails++;
  if (wb) wb->lpVtbl->Release(wb);
  if (f) f->lpVtbl->Release(f);
  free(o);
}

/* пузырь сейчас — реплика из встроенного списка? номер или −1 */
static int al_bubble_item(void) {
  for (int i = 0; i < FJ_N; i++)
    if (!wcscmp(g_acBubbleText, kFjItems[i].text)) return i;
  return -1;
}

static BOOL al_bubble_shown(void) { return g_acBubble && IsWindowVisible(g_acBubble); }

/* «ответ модели» на запрос id */
static void al_model_answer(int id, int status, const wchar_t *text) {
  AiResult *r = (AiResult *)calloc(1, sizeof(AiResult));
  r->id = id;
  r->status = status;
  r->text = _wcsdup(text);
  SendMessageW(g_acAvatar, WM_AC_FACT, 0, (LPARAM)r);
}

static void al_facts(void) {
  wchar_t line[400];
  /* включить в меню: переключатель, запись в ai_avatar.ini, подсказка в пузыре */
  ac_avatar_menu(g_acAvatar);
  al_pump(200);
  ac_menu_activate(ACM_FACTS);
  al_pump(200);
  if (g_acMenuPix) al_png(L"menu.png", g_acMenuPix, g_acMenuBmpW, g_acMenuBmpH, RGB(242, 242, 242));
  ac_menu_close();
  wchar_t ini[MAX_PATH];
  _snwprintf(ini, MAX_PATH, L"%ls\\ai_avatar.ini", g_dataDir);
  al_check(g_acFacts && GetPrivateProfileIntW(L"Avatar", L"Facts", 0, ini) == 1, L"факты: включены и записаны в ai_avatar.ini");
  al_check(al_bubble_shown() && g_acBubbleStory && wcsstr(g_acBubbleText, L"15 минут"), L"факты: подсказка при включении");
  al_check(!ai_warm(), L"ИИ выключен — модель не трогается");

  /* щелчок → через время двойного щелчка факт или анекдот из списка, без повторов */
  int seen[64], nSeen = 0, repeats = 0, kinds[2] = {0, 0}, run = 0, maxRun = 0, prevKind = -1;
  for (int k = 0; k < 40; k++) {
    SendMessageW(g_acAvatar, WM_TIMER, AC_FACT_TIMER, 0);
    int i = al_bubble_item();
    if (i < 0) { al_check(FALSE, L"факты: щелчок — пузырь не из списка"); break; }
    for (int j = nSeen - FJ_RECENT < 0 ? 0 : nSeen - FJ_RECENT; j < nSeen; j++)
      if (seen[j] == i) repeats++;
    if (nSeen < 64) seen[nSeen++] = i;
    kinds[kFjItems[i].joke]++;
    run = kFjItems[i].joke == prevKind ? run + 1 : 1;
    prevKind = kFjItems[i].joke;
    if (run > maxRun) maxRun = run;
    if (k == 0) {
      al_pump(120);
      ac_bubble_update();
      if (g_acBubblePixels) al_png(kFjItems[i].joke ? L"bubble-joke.png" : L"bubble-fact.png", g_acBubblePixels,
                                   g_acBubbleW, g_acBubbleH, RGB(242, 242, 242));
    }
  }
  _snwprintf(line, 400, L"факты: 40 щелчков — %d фактов, %d анекдотов, повторов среди последних %d: %d, подряд одного рода не больше %d",
             kinds[0], kinds[1], FJ_RECENT, repeats, maxRun);
  al_check(nSeen == 40 && !repeats && maxRun <= 2 && kinds[0] && kinds[1], line);
  al_check(g_acAvatarState == AC_AV_TELL, L"факты: помощник «рассказывает» («кстати!»)");
  /* длинный анекдот — снимок пузыря целиком */
  for (int i = 0; i < FJ_N; i++)
    if (kFjItems[i].joke && wcslen(kFjItems[i].text) > 150) {
      ac_fact_say(kFjItems[i].text, 1);
      al_pump(120);
      ac_bubble_update();
      if (g_acBubblePixels) al_png(L"bubble-long.png", g_acBubblePixels, g_acBubbleW, g_acBubbleH, RGB(34, 36, 42));
      break;
    }

  /* одиночный щелчок ставит ожидание, двойной его снимает и факт не говорит */
  ac_bubble_hide(TRUE);
  GetWindowRect(g_acAvatar, &g_acAvatarDragFrom);
  SendMessageW(g_acAvatar, WM_EXITSIZEMOVE, 0, 0);
  SendMessageW(g_acAvatar, WM_LBUTTONDBLCLK, 0, 0);
  ac_mini_close();
  if (g_acWnd) ShowWindow(g_acWnd, SW_HIDE);
  ac_bubble_hide(TRUE);
  wcscpy(g_acBubbleText, L"-");
  al_pump(GetDoubleClickTime() + 300);
  al_check(al_bubble_item() < 0, L"факты: двойной щелчок открывает чат, факт не рассказан");
  /* одиночный — рассказан */
  GetWindowRect(g_acAvatar, &g_acAvatarDragFrom);
  SendMessageW(g_acAvatar, WM_EXITSIZEMOVE, 0, 0);
  al_pump(GetDoubleClickTime() + 300);
  al_check(al_bubble_item() >= 0, L"факты: одиночный щелчок — факт или анекдот");

  /* ответ модели: подпись и разметка снимаются, повтор и негодное — строкой из списка */
  g_acFactId = 9001;
  g_acFactKind = 0;
  al_model_answer(9001, AI_OK, L"**Факт:** первые станки с ЧПУ управлялись перфолентой.");
  al_check(!wcscmp(g_acBubbleText, L"первые станки с ЧПУ управлялись перфолентой.") && !g_acFactId,
           L"факты: ответ модели почищен и показан");
  g_acFactId = 9002;
  al_model_answer(9002, AI_OK, L"Факт — первые станки с ЧПУ управлялись перфолентой.");
  al_check(al_bubble_item() >= 0 && !kFjItems[al_bubble_item()].joke, L"факты: повтор модели — строка из списка");
  g_acFactId = 9003;
  g_acFactKind = 1;
  al_model_answer(9003, AI_OK, L"Sure! Here is a joke about lathes and engineers.");
  al_check(al_bubble_item() >= 0 && kFjItems[al_bubble_item()].joke, L"факты: ответ по-английски — анекдот из списка");
  g_acFactId = 9004;
  wcscpy(g_acBubbleText, L"-");
  al_model_answer(9004, AI_CANCELLED, L"отменено");
  al_check(!wcscmp(g_acBubbleText, L"-") && !g_acFactId, L"факты: отменённый запрос — молчит");
  g_acFactId = 9005;
  al_model_answer(4242, AI_OK, L"Чужой ответ, пришедший после отмены запроса, не показывается.");
  al_check(!wcscmp(g_acBubbleText, L"-") && g_acFactId == 9005, L"факты: чужой ответ не показывается");
  g_acFactId = 9005;
  ac_avatar_busy(TRUE); /* человек спросил в чате — запрос факта снимается */
  al_check(!g_acFactId, L"факты: вопрос в чате снимает запрос факта");
  ac_avatar_busy(FALSE);

  /* сам раз в 15 минут: человек за компьютером — рассказывает; нет его — ждёт минуту */
  ac_bubble_hide(TRUE);
  al_pump(50);
  DWORD now = GetTickCount();
  g_alInputAt = now - 10 * 60 * 1000;
  g_acFactNext = now;
  wcscpy(g_acBubbleText, L"-");
  ac_avatar_tick();
  al_check(al_bubble_item() < 0 && (LONG)(g_acFactNext - now) >= 50000, L"факты: никого нет — отложено на минуту");
  g_alInputAt = 0;
  g_acFactNext = GetTickCount();
  ac_avatar_tick();
  _snwprintf(line, 400, L"факты: сам рассказал «%.40ls…», следующий через %d мин", g_acBubbleText,
             (int)((LONG)(g_acFactNext - GetTickCount()) / 60000 + 1));
  al_check(al_bubble_item() >= 0 && (LONG)(g_acFactNext - GetTickCount()) > 14 * 60 * 1000, line);
  /* занят чатом — не рассказывает */
  ac_bubble_hide(TRUE);
  al_pump(50);
  g_acAvatarBusy = TRUE;
  g_acFactNext = GetTickCount();
  wcscpy(g_acBubbleText, L"-");
  ac_avatar_tick();
  al_check(!wcscmp(g_acBubbleText, L"-"), L"факты: ИИ занят — сам не рассказывает");
  g_acAvatarBusy = FALSE;
  /* выключить */
  ac_menu_activate(ACM_FACTS);
  al_check(!g_acFacts && GetPrivateProfileIntW(L"Avatar", L"Facts", 1, ini) == 0, L"факты: выключены и записаны");
  ac_menu_close();
  ac_bubble_hide(TRUE);
}

/* облики: перебор в меню по кругу, запись в ai_avatar.ini, кадр в окне, время кадра */
static void al_skins(void) {
  wchar_t ini[MAX_PATH], line[400], value[32];
  _snwprintf(ini, MAX_PATH, L"%ls\\ai_avatar.ini", g_dataDir);
  ac_avatar_menu(g_acAvatar);
  al_pump(150);
  int start = g_acAvatarSkin;
  for (int k = 0; k < AC_SKIN_N; k++) {
    ac_menu_activate(ACM_SKIN);
    al_pump(120);
    GetPrivateProfileStringW(L"Avatar", L"Skin", L"", value, 32, ini);
    _snwprintf(line, 400, L"облик: «%ls», в ai_avatar.ini Skin=%ls", kAcSkinName[g_acAvatarSkin], value);
    al_check(g_acAvatarSkin == (start + k + 1) % AC_SKIN_N && !wcscmp(value, kAcSkinKey[g_acAvatarSkin]), line);
    if (g_acAvatarSkin == AC_SKIN_GIRL && g_acMenuPix)
      al_png(L"menu-catgirl.png", g_acMenuPix, g_acMenuBmpW, g_acMenuBmpH, RGB(242, 242, 242));
  }
  ac_menu_close();
  /* кошко-девочка в окне: машет после переодевания, потом покой; крупная и обычная */
  ac_avatar_set_skin(AC_SKIN_GIRL);
  al_check(g_agShot == AGA_WAVE, L"кошко-девочка: переоделась — машет");
  al_pump(600);
  ac_avatar_present();
  if (g_acAvatarFramePixels) al_png(L"window-catgirl-wave.png", g_acAvatarFramePixels, g_acAvatarSize, g_acAvatarSize, RGB(242, 242, 242));
  int sizes[2] = {AC_AVATAR_MAX, AC_AVATAR_DEFAULT_SIZE};
  for (int k = 0; k < 2; k++) {
    ac_avatar_apply_scale(sizes[k]);
    g_agShot = -1;
    al_pump(400);
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    BYTE alpha;
    for (int i = 0; i < 60; i++) ac_avatar_render(GetTickCount() + i * 33, &alpha);
    QueryPerformanceCounter(&t1);
    double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart / 60.0;
    _snwprintf(line, 400, L"кошко-девочка %d px: уровень атласа %d, кадр %.2f мс", sizes[k], g_acGirlLevel, ms);
    al_check(ms < 12.0 && g_acGirlLevel == (k ? 1 : 0), line);
    ac_avatar_present();
    _snwprintf(line, 400, L"window-catgirl-%d.png", sizes[k]);
    if (g_acAvatarFramePixels) al_png(line, g_acAvatarFramePixels, g_acAvatarSize, g_acAvatarSize, RGB(34, 36, 42));
  }
  /* щелчок — касание; разбудили — потягивается */
  ac_avatar_poke_ex(FALSE);
  al_check(g_agShot == AGA_TAP, L"кошко-девочка: щелчок — касание");
  g_acAvatarState = AC_AV_SLEEP;
  g_acAvatarActiveAt = GetTickCount();
  {
    RECT wr;
    GetWindowRect(g_acAvatar, &wr);
    g_alCursor.x = (wr.left + wr.right) / 2;
    g_alCursor.y = wr.top + 10;
  }
  ac_avatar_tick();
  al_check(g_acAvatarState == AC_AV_WAKE && kAgFromState[AC_AV_WAKE] == AGA_WAKE,
           L"кошко-девочка: курсор рядом — проснулась и потягивается");
  g_alCursor.x = g_alCursor.y = 5;
  /* пузырь над головой, а не сбоку */
  ac_avatar_bubble(L"Привет! Я теперь кошко-девочка.", FALSE, 5000);
  al_pump(100);
  RECT br, ar;
  GetWindowRect(g_acBubble, &br);
  GetWindowRect(g_acAvatar, &ar);
  _snwprintf(line, 400, L"кошко-девочка: пузырь %ld..%ld над головой окна %ld..%ld (низ пузыря %ld, верх окна %ld)", br.left, br.right,
             ar.left, ar.right, br.bottom, ar.top);
  al_check(br.bottom <= ar.top + g_acAvatarSize / 4 && br.left < (ar.left + ar.right) / 2 && br.right > (ar.left + ar.right) / 2, line);
  ac_bubble_hide(TRUE);
}

/* робот: переход между строками атласа, новые действия, ходьба */
static void al_robot(void) {
  wchar_t line[400];
  ac_avatar_set_skin(AC_SKIN_ROBOT);
  ac_avatar_apply_scale(AC_AVATAR_DEFAULT_SIZE);
  g_acHopAt = g_acLandAt = 0;
  ac_avatar_set(AC_AV_IDLE);
  ac_avatar_present();
  ac_avatar_set(AC_AV_THINKING); /* рисует сразу — с этого мгновения прежний кадр тает */
  DWORD now = GetTickCount();
  unsigned mid = ac_fade_mix(now + 100);
  _snwprintf(line, 400, L"робот: смена состояния — прежний кадр тает (доля %u из 256 через 100 мс), через 300 мс перехода нет", mid);
  al_check(mid > 20 && mid < 240 && ac_fade_mix(now + 300) == 0, line);
  /* факт — «кстати», анекдот — потом смеётся, потом обратно в покой */
  g_acAvatarBusy = FALSE;
  for (int i = 0; i < FJ_N; i++)
    if (kFjItems[i].joke) { ac_fact_say(kFjItems[i].text, 1); break; }
  al_check(g_acAvatarState == AC_AV_TELL && g_acLaughAt, L"робот: рассказывает анекдот — «кстати», смех назначен");
  g_acAvatarTalkUntil = g_acLaughAt = GetTickCount();
  ac_avatar_tick();
  al_check(g_acAvatarState == AC_AV_LAUGH, L"робот: анекдот досказан — смеётся");
  ac_avatar_present();
  if (g_acAvatarFramePixels) al_png(L"robot-laugh.png", g_acAvatarFramePixels, g_acAvatarSize, g_acAvatarSize, RGB(242, 242, 242));
  g_acLaughUntil = GetTickCount();
  ac_avatar_tick();
  al_check(g_acAvatarState == AC_AV_IDLE, L"робот: отсмеялся — покой");
  ac_bubble_hide(TRUE);
  /* спал — курсор рядом — потягивается один раз и в покой */
  g_acAvatarState = AC_AV_SLEEP;
  RECT wr;
  GetWindowRect(g_acAvatar, &wr);
  g_alCursor.x = (wr.left + wr.right) / 2;
  g_alCursor.y = wr.top + 10;
  ac_avatar_tick();
  al_check(g_acAvatarState == AC_AV_WAKE, L"робот: курсор рядом — проснулся и потягивается");
  g_acAvatarStateAt = GetTickCount() - ac_state_ms(AC_AV_WAKE) - 50;
  ac_avatar_tick();
  al_check(g_acAvatarState == AC_AV_IDLE, L"робот: потянулся — покой");
  g_alCursor.x = g_alCursor.y = 5;
  ac_bubble_hide(TRUE);
  /* идёт вправо: разгон с места и наклон по ходу */
  g_acAvatarTarget.x = wr.left + 120;
  g_acAvatarTarget.y = wr.top;
  g_acAvatarMoving = TRUE;
  g_acMoveAt = 0;
  ac_avatar_move();
  GetWindowRect(g_acAvatar, &wr);
  int first = wr.left;
  g_acMoveAt = GetTickCount() - 1000;
  ac_avatar_move();
  GetWindowRect(g_acAvatar, &wr);
  AcXf xf;
  ac_avatar_pose(GetTickCount(), 1.0f, &xf);
  _snwprintf(line, 400, L"робот: идёт вправо — шаг с места %d px, разогнавшись %d px, наклон %.3f",
             first - (int)(g_acAvatarTarget.x - 120), wr.left - first, atan2f(xf.s, xf.c));
  al_check(g_acMoveDir > 0 && wr.left - first >= first - (int)(g_acAvatarTarget.x - 120) && atan2f(xf.s, xf.c) > 0.02f, line);
  g_acAvatarMoving = FALSE;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
  (void)prev;
  (void)cmd;
  (void)show;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv || argc < 2) return 2;
  g_alDir = argv[1];
  CreateDirectoryW(g_alDir, NULL);
  _snwprintf(g_alRoot, MAX_PATH, L"%ls\\appdata", g_alDir);
  CreateDirectoryW(g_alRoot, NULL);
  /* свой рабочий стол: окна стенда не мелькают у человека на экране */
  HDESK desk = CreateDesktopW(L"CursorPadAvatarLife", NULL, NULL, 0, GENERIC_ALL, NULL);
  if (!desk || !SetThreadDesktop(desk)) return 3;
  OleInitialize(NULL);
  g_inst = inst;
  enable_dpi();
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&icc);
  {
    wchar_t folder[MAX_PATH], path[MAX_PATH];
    _snwprintf(folder, MAX_PATH, L"%ls\\CursorPad", g_alRoot);
    CreateDirectoryW(folder, NULL);
    _snwprintf(path, MAX_PATH, L"%ls\\cursor.txt", folder);
    const char *pref = "system 180 250 ai 0 0\n";
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(h, pref, (DWORD)strlen(pref), &wr, NULL); CloseHandle(h); }
    _snwprintf(path, MAX_PATH, L"%ls\\ai.txt", folder); /* ИИ выключен: стенд модель не зовёт */
    const char *ai = "on 0\r\n";
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(h, ai, (DWORD)strlen(ai), &wr, NULL); CloseHandle(h); }
    _snwprintf(path, MAX_PATH, L"%ls\\ai_avatar.ini", folder);
    DeleteFileW(path);
    WritePrivateProfileStringW(L"Avatar", L"Visible", L"0", path);
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
  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"CursorPadWindow", L"CursorPad",
                              WS_POPUP | WS_CLIPCHILDREN, 320, 330, WND_W, WND_H, NULL, NULL, inst, NULL);
  if (!hwnd) return 1;
  apply_dpi(hwnd);
  g_aiUiThread = GetCurrentThreadId();
  KillTimer(hwnd, TIMER_FOLLOW);
  g_aiOn = FALSE;
  al_pump(200);

  ac_avatar_summon();
  al_pump(900);
  al_check(ac_avatar_here(), L"помощник призван");
  al_facts();
  al_skins();
  al_robot();

  char out[96000];
  int k = WideCharToMultiByte(CP_UTF8, 0, g_alReport, -1, out, sizeof(out), NULL, NULL);
  al_write(L"report.txt", out, k > 0 ? (DWORD)k - 1 : 0);
  LocalFree(argv);
  return g_alFails ? 10 : 0;
}
