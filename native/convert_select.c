/* Выделение Проводника на миг нажатия Shift (с 2026.10.24).

   Shift + щелчок по файлу в Проводнике выделяет диапазон: от первого
   выделенного до того, за который взялись. Кто сначала зажимает Shift, а
   потом тянет из середины выделения, получает выделение, урезанное «до
   середины», — и в кольцо приходят не те файлы. Поэтому, как только Shift
   нажат над Проводником или рабочим столом (кнопка мыши ещё отпущена),
   отдельный поток спрашивает у окна его выделение через Shell
   (IShellWindows → IShellBrowser → IFolderView), ничего в окне не меняя.
   Если потом пришедший при перетаскивании список — часть запомненного
   (тянули при том же нажатом Shift и из того же окна), кольцо берёт
   запомненное целиком. Тянуть, а уже потом жать Shift, — по-прежнему можно.
   Подключается из convert.c после CvSelection и cv_data. */
#include <exdisp.h>

static const GUID kCvCLSID_ShellWindows = {0x9ba05972, 0xf6a8, 0x11cf, {0xa4, 0x42, 0x00, 0xa0, 0xc9, 0x0a, 0x8f, 0x39}};
static const GUID kCvIID_ShellWindows = {0x85cb6900, 0x4d95, 0x11cf, {0x96, 0x0c, 0x00, 0x80, 0xc7, 0xf4, 0xee, 0x85}};
static const GUID kCvSID_TopLevelBrowser = {0x4c96be40, 0x915c, 0x11cf, {0x99, 0xd3, 0x00, 0xaa, 0x00, 0x4a, 0xe8, 0x37}};
static const GUID kCvIID_ServiceProvider = {0x6d5140c1, 0x7436, 0x11ce, {0x80, 0x34, 0x00, 0xaa, 0x00, 0x60, 0x09, 0xfa}};
static const GUID kCvIID_ShellBrowser = {0x000214e2, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kCvIID_FolderView = {0xcde725b0, 0xccc9, 0x4519, {0x91, 0x7e, 0x32, 0x5d, 0x72, 0xfa, 0xb4, 0xce}};
static const GUID kCvIID_ShellItemArray = {0xb63ea76d, 0x1f85, 0x456f, {0xa1, 0x9c, 0x48, 0x15, 0x9e, 0xfa, 0x85, 0x8b}};
static const GUID kCvIID_WebBrowserApp = {0x0002df05, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static SRWLOCK g_cvSnapLock = SRWLOCK_INIT;
static CvSelection g_cvSnap;        /* запомненное выделение, только файлы и папки как есть */
static BOOL g_cvSnapDesktop;        /* снято с рабочего стола */
static HWND g_cvSnapWnd;            /* окно Проводника, с которого снято */
static DWORD g_cvSnapPress;         /* к какому нажатию Shift относится (его GetTickCount) */
static volatile LONG g_cvSnapBusy;  /* поток уже спрашивает */
static DWORD g_cvShiftPress;        /* когда нажат текущий Shift; 0 — отпущен */
static DWORD g_cvDownPress;         /* Shift, с которым нажата кнопка мыши; 0 — без него */
static HWND g_cvDownRoot;           /* окно под мышью в миг нажатия */
static BOOL g_cvWide;               /* в g_cvSelection — запомненное, а не пришедшее */
static CvSelection g_cvDragged;     /* что пришло при перетаскивании — с ним сверяется сброс */

static BOOL cv_desktop_class(HWND root) {
  wchar_t name[40];
  return root && GetClassNameW(root, name, 40) && (!wcscmp(name, L"Progman") || !wcscmp(name, L"WorkerW"));
}
static BOOL cv_explorer_class(HWND root) {
  wchar_t name[40];
  return root && GetClassNameW(root, name, 40) && (!wcscmp(name, L"CabinetWClass") || !wcscmp(name, L"ExploreWClass"));
}

/* выделение открытой папки; FALSE — пусто, больше CV_FILES или есть не-файлы (Корзина, «Этот компьютер») */
static BOOL cv_view_selection(IShellBrowser *browser, CvSelection *out) {
  IShellView *view = NULL;
  IFolderView *folder = NULL;
  IShellItemArray *items = NULL;
  BOOL ok = FALSE;
  memset(out, 0, sizeof(*out));
  if (SUCCEEDED(browser->lpVtbl->QueryActiveShellView(browser, &view)) && view &&
      SUCCEEDED(view->lpVtbl->QueryInterface(view, &kCvIID_FolderView, (void **)&folder)) && folder &&
      SUCCEEDED(folder->lpVtbl->Items(folder, SVGIO_SELECTION | SVGIO_FLAG_VIEWORDER, &kCvIID_ShellItemArray, (void **)&items)) &&
      items) {
    DWORD n = 0;
    ok = SUCCEEDED(items->lpVtbl->GetCount(items, &n)) && n > 0 && n <= CV_FILES;
    for (DWORD i = 0; ok && i < n; i++) {
      IShellItem *item = NULL;
      wchar_t *path = NULL;
      ok = SUCCEEDED(items->lpVtbl->GetItemAt(items, i, &item)) && item &&
           SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path && wcslen(path) < MAX_PATH;
      if (ok) lstrcpynW(out->paths[out->count++], path, MAX_PATH);
      if (path) CoTaskMemFree(path);
      if (item) item->lpVtbl->Release(item);
    }
  }
  if (items) items->lpVtbl->Release(items);
  if (folder) folder->lpVtbl->Release(folder);
  if (view) view->lpVtbl->Release(view);
  return ok;
}

static IShellBrowser *cv_dispatch_browser(IDispatch *disp) {
  IServiceProvider *services = NULL;
  IShellBrowser *browser = NULL;
  if (disp && SUCCEEDED(disp->lpVtbl->QueryInterface(disp, &kCvIID_ServiceProvider, (void **)&services)) && services) {
    services->lpVtbl->QueryService(services, &kCvSID_TopLevelBrowser, &kCvIID_ShellBrowser, (void **)&browser);
    services->lpVtbl->Release(services);
  }
  return browser;
}

/* окно Проводника (его видимая вкладка) или рабочий стол */
static IShellBrowser *cv_find_browser(IShellWindows *windows, HWND root, BOOL desktop) {
  IShellBrowser *found = NULL;
  if (desktop) {
    VARIANT empty;
    VariantInit(&empty);
    long hwnd = 0;
    IDispatch *disp = NULL;
    if (SUCCEEDED(windows->lpVtbl->FindWindowSW(windows, &empty, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp)) && disp) {
      found = cv_dispatch_browser(disp);
      disp->lpVtbl->Release(disp);
    }
    return found;
  }
  /* в Windows 11 у окна несколько вкладок; видимая — первая ShellTabWindowClass */
  HWND tab = FindWindowExW(root, NULL, L"ShellTabWindowClass", NULL);
  long count = 0;
  windows->lpVtbl->get_Count(windows, &count);
  for (long i = 0; i < count && !found; i++) {
    VARIANT index;
    VariantInit(&index);
    index.vt = VT_I4;
    index.lVal = i;
    IDispatch *disp = NULL;
    IWebBrowserApp *app = NULL;
    SHANDLE_PTR handle = 0;
    if (SUCCEEDED(windows->lpVtbl->Item(windows, index, &disp)) && disp &&
        SUCCEEDED(disp->lpVtbl->QueryInterface(disp, &kCvIID_WebBrowserApp, (void **)&app)) && app &&
        SUCCEEDED(app->lpVtbl->get_HWND(app, &handle)) && (HWND)handle == root) {
      IShellBrowser *browser = cv_dispatch_browser(disp);
      HWND window = NULL;
      if (browser && (!tab || (SUCCEEDED(browser->lpVtbl->GetWindow(browser, &window)) && window == tab))) found = browser;
      else if (browser) browser->lpVtbl->Release(browser);
    }
    if (app) app->lpVtbl->Release(app);
    if (disp) disp->lpVtbl->Release(disp);
  }
  return found;
}

typedef struct { HWND root; BOOL desktop; DWORD press; } CvSnapJob;
static DWORD WINAPI cv_snap_thread(void *parameter) {
  CvSnapJob job = *(CvSnapJob *)parameter;
  free(parameter);
  CvSelection *found = (CvSelection *)calloc(1, sizeof(*found));
  BOOL ok = FALSE;
  if (found && SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
    IShellWindows *windows = NULL;
    if (SUCCEEDED(CoCreateInstance(&kCvCLSID_ShellWindows, NULL, CLSCTX_ALL, &kCvIID_ShellWindows, (void **)&windows)) && windows) {
      IShellBrowser *browser = cv_find_browser(windows, job.root, job.desktop);
      if (browser) {
        ok = cv_view_selection(browser, found);
        browser->lpVtbl->Release(browser);
      }
      windows->lpVtbl->Release(windows);
    }
    CoUninitialize();
  }
  if (ok) {
    AcquireSRWLockExclusive(&g_cvSnapLock);
    g_cvSnap = *found;
    g_cvSnapWnd = job.root;
    g_cvSnapDesktop = job.desktop;
    g_cvSnapPress = job.press;
    ReleaseSRWLockExclusive(&g_cvSnapLock);
  }
  free(found);
  InterlockedExchange(&g_cvSnapBusy, 0);
  return 0;
}

/* Shift только что нажат, мышь отпущена: запомнить выделение окна под рукой */
static void cv_snap_start(DWORD press) {
  HWND root = GetAncestor(GetForegroundWindow(), GA_ROOT);
  BOOL desktop = cv_desktop_class(root);
  if ((!desktop && !cv_explorer_class(root)) || InterlockedCompareExchange(&g_cvSnapBusy, 1, 0)) return;
  CvSnapJob *job = (CvSnapJob *)malloc(sizeof(*job));
  HANDLE thread = NULL;
  if (job) {
    job->root = root, job->desktop = desktop, job->press = press;
    thread = CreateThread(NULL, 0, cv_snap_thread, job, 0, NULL);
  }
  if (thread) CloseHandle(thread);
  else {
    free(job);
    InterlockedExchange(&g_cvSnapBusy, 0);
  }
}

/* слежение за Shift и мышью из cv_tick: нажатие Shift и миг нажатия кнопки */
static void cv_snap_track(BOOL down, BOOL shift, POINT point) {
  if (!shift) g_cvShiftPress = 0;
  else if (!g_cvShiftPress) {
    g_cvShiftPress = GetTickCount() | 1;
    if (!down) cv_snap_start(g_cvShiftPress);
  }
  if (down && !g_cvDown) {
    g_cvDownPress = g_cvShiftPress;
    g_cvDownRoot = GetAncestor(WindowFromPoint(point), GA_ROOT);
  }
}

/* список от Проводника урезан Shift-щелчком — взять запомненное выделение целиком.
   Только если тянули при том же нажатом Shift, из того же окна, и пришедшее — часть запомненного. */
static void cv_snap_apply(void) {
  g_cvWide = FALSE;
  if (!g_cvDownPress || !g_cvSelection.count) return;
  AcquireSRWLockShared(&g_cvSnapLock);
  BOOL same = g_cvSnapPress == g_cvDownPress && g_cvSnap.count > g_cvSelection.count + g_cvSkipped &&
              (g_cvSnapDesktop ? cv_desktop_class(g_cvDownRoot) : g_cvSnapWnd == g_cvDownRoot);
  for (int i = 0; same && i < g_cvSelection.count; i++) {
    BOOL inside = FALSE;
    for (int j = 0; !inside && j < g_cvSnap.count; j++) inside = !_wcsicmp(g_cvSelection.paths[i], g_cvSnap.paths[j]);
    same = inside;
  }
  if (same) {
    int skipped = g_cvSkipped;
    g_cvDragged = g_cvSelection;
    memset(&g_cvSelection, 0, sizeof(g_cvSelection));
    g_cvSkipped = 0;
    for (int j = 0; j < g_cvSnap.count; j++) { /* как cv_data: папки пропускаются */
      DWORD attr = GetFileAttributesW(g_cvSnap.paths[j]);
      if (attr == INVALID_FILE_ATTRIBUTES) continue;
      if (attr & FILE_ATTRIBUTE_DIRECTORY) g_cvSkipped++;
      else lstrcpynW(g_cvSelection.paths[g_cvSelection.count++], g_cvSnap.paths[j], MAX_PATH);
    }
    g_cvWide = g_cvSelection.count > 0;
    if (!g_cvWide) g_cvSelection = g_cvDragged, g_cvSkipped = skipped;
  }
  ReleaseSRWLockShared(&g_cvSnapLock);
}
