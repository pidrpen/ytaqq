/* ---- Окно со встроенным Edge (WebView2) для страниц из «Ещё» ----------------

   Страница открывается в окне самого CursorPad, а не во вкладке браузера.
   Движок — WebView2: это Edge, встроенный в Windows 10/11. Загрузчик
   WebView2Loader.dll (из официального пакета Microsoft.Web.WebView2,
   лицензия BSD — native/webview2/LICENSE.txt) вшит в exe (RCDATA 323) и
   выкладывается рядом с данными.

   Полный WebView2.h — 69 тысяч строк; здесь только то, что вызываем,
   с тем же порядком методов (снято с WebView2.h 1.0.4191.47). Слоты,
   которые не нужны, — пустые указатели того же размера.

   Нет среды WebView2 на ПК (или что-то не поднялось) — webview_open вернёт
   FALSE, и tools.c откроет страницу отдельным окном Edge без вкладок. */

typedef struct WvEnv WvEnv;
typedef struct WvCtl WvCtl;
typedef struct WvCore WvCore;
typedef struct WvEnvDone WvEnvDone;
typedef struct WvCtlDone WvCtlDone;

typedef struct {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(WvEnv *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(WvEnv *);
  ULONG(STDMETHODCALLTYPE *Release)(WvEnv *);
  HRESULT(STDMETHODCALLTYPE *CreateCoreWebView2Controller)(WvEnv *, HWND, WvCtlDone *);
} WvEnvVtbl;
struct WvEnv {
  const WvEnvVtbl *lpVtbl;
};

typedef struct {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(WvCtl *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(WvCtl *);
  ULONG(STDMETHODCALLTYPE *Release)(WvCtl *);
  void *get_IsVisible;
  HRESULT(STDMETHODCALLTYPE *put_IsVisible)(WvCtl *, BOOL);
  void *get_Bounds;
  HRESULT(STDMETHODCALLTYPE *put_Bounds)(WvCtl *, RECT);
  void *slots7_11[5]; /* ZoomFactor…SetBoundsAndZoomFactor */
  HRESULT(STDMETHODCALLTYPE *MoveFocus)(WvCtl *, int);
  void *slots13_23[11]; /* события фокуса, клавиш, ParentWindow… */
  HRESULT(STDMETHODCALLTYPE *Close)(WvCtl *);
  HRESULT(STDMETHODCALLTYPE *get_CoreWebView2)(WvCtl *, WvCore **);
} WvCtlVtbl;
struct WvCtl {
  const WvCtlVtbl *lpVtbl;
};

typedef struct {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(WvCore *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(WvCore *);
  ULONG(STDMETHODCALLTYPE *Release)(WvCore *);
  void *get_Settings;
  void *get_Source;
  HRESULT(STDMETHODCALLTYPE *Navigate)(WvCore *, LPCWSTR);
} WvCoreVtbl;
struct WvCore {
  const WvCoreVtbl *lpVtbl;
};

/* обработчики «среда готова» / «окно готово» — свои COM-объекты */
typedef struct {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(WvEnvDone *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(WvEnvDone *);
  ULONG(STDMETHODCALLTYPE *Release)(WvEnvDone *);
  HRESULT(STDMETHODCALLTYPE *Invoke)(WvEnvDone *, HRESULT, WvEnv *);
} WvEnvDoneVtbl;
struct WvEnvDone {
  const WvEnvDoneVtbl *lpVtbl;
};

typedef struct {
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(WvCtlDone *, REFIID, void **);
  ULONG(STDMETHODCALLTYPE *AddRef)(WvCtlDone *);
  ULONG(STDMETHODCALLTYPE *Release)(WvCtlDone *);
  HRESULT(STDMETHODCALLTYPE *Invoke)(WvCtlDone *, HRESULT, WvCtl *);
} WvCtlDoneVtbl;
struct WvCtlDone {
  const WvCtlDoneVtbl *lpVtbl;
  HWND hwnd; /* окно, для которого просили */
};

/* свой IID_IUnknown: из libuuid он тянет лишнее (см. pick_folder) */
static const IID kIID_Unknown = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID kIID_EnvDone = {0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}};
static const IID kIID_CtlDone = {0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}};

typedef HRESULT(STDAPICALLTYPE *WvCreateEnvFn)(PCWSTR, PCWSTR, void *, WvEnvDone *);
typedef HRESULT(STDAPICALLTYPE *WvVersionFn)(PCWSTR, LPWSTR *);

static WvCreateEnvFn g_wvCreate;
static WvEnv *g_wvEnv;
static BOOL g_wvEnvPending, g_wvBroken;

/* окна, которые ждут среду: она поднимается один раз и асинхронно */
#define WV_MAX 8
typedef struct {
  HWND hwnd;
  WvCtl *ctl;
  WvCore *core;
  wchar_t url[MAX_PATH * 3];
  WvCtlDone done;
  ULONGLONG t0; /* когда попросили открыть — для журнала */
} WvWin;

/* Журнал: сколько занял каждый шаг (%LOCALAPPDATA%\CursorPad\webview2\log.txt).
   По нему видно, что именно медленное: запуск движка или само окно. */
static ULONGLONG g_wvEnvT0;
static void wv_log(const wchar_t *fmt, ...) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH], line[400];
  _snwprintf(path, MAX_PATH, L"%s\\webview2\\log.txt", g_dataDir);
  path[MAX_PATH - 1] = 0;
  SYSTEMTIME t;
  GetLocalTime(&t);
  int n = _snwprintf(line, 400, L"%02d.%02d %02d:%02d:%02d.%03d  ", t.wDay, t.wMonth, t.wHour, t.wMinute,
                     t.wSecond, t.wMilliseconds);
  va_list ap;
  va_start(ap, fmt);
  if (n > 0) _vsnwprintf(line + n, 400 - n - 3, fmt, ap);
  va_end(ap);
  line[396] = 0;
  wcscat(line, L"\r\n");
  char u[1200];
  int k = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, (int)sizeof(u), NULL, NULL);
  HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  if (k > 1) WriteFile(f, u, (DWORD)(k - 1), &w, NULL);
  CloseHandle(f);
}
static WvWin g_wv[WV_MAX];

static WvWin *wv_find(HWND h) {
  for (int i = 0; i < WV_MAX; i++)
    if (g_wv[i].hwnd == h) return &g_wv[i];
  return NULL;
}

static HRESULT STDMETHODCALLTYPE wv_qi_env(WvEnvDone *t, REFIID r, void **o) {
  if (IsEqualIID(r, &kIID_Unknown) || IsEqualIID(r, &kIID_EnvDone)) {
    *o = t;
    return S_OK;
  }
  *o = NULL;
  return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE wv_qi_ctl(WvCtlDone *t, REFIID r, void **o) {
  if (IsEqualIID(r, &kIID_Unknown) || IsEqualIID(r, &kIID_CtlDone)) {
    *o = t;
    return S_OK;
  }
  *o = NULL;
  return E_NOINTERFACE;
}
/* объекты живут всё время программы — счёт ссылок не нужен */
static ULONG STDMETHODCALLTYPE wv_ref_env(WvEnvDone *t) {
  (void)t;
  return 1;
}
static ULONG STDMETHODCALLTYPE wv_ref_ctl(WvCtlDone *t) {
  (void)t;
  return 1;
}

static void wv_fallback(WvWin *w);

static HRESULT STDMETHODCALLTYPE wv_ctl_done(WvCtlDone *t, HRESULT hr, WvCtl *ctl) {
  if (!t->hwnd) return S_OK; /* окно закрыли, пока движок поднимался */
  WvWin *w = wv_find(t->hwnd);
  if (!w) return S_OK; /* окно уже закрыли */
  if (FAILED(hr) || !ctl) {
    wv_log(L"окно движка не создалось (0x%08lx) — открываю в Edge", (unsigned long)hr);
    wv_fallback(w);
    return S_OK;
  }
  wv_log(L"окно движка готово через %llu мс после нажатия", GetTickCount64() - w->t0);
  ctl->lpVtbl->AddRef(ctl);
  w->ctl = ctl;
  RECT rc;
  GetClientRect(w->hwnd, &rc);
  ctl->lpVtbl->put_Bounds(ctl, rc);
  ctl->lpVtbl->put_IsVisible(ctl, TRUE);
  if (SUCCEEDED(ctl->lpVtbl->get_CoreWebView2(ctl, &w->core)) && w->core)
    w->core->lpVtbl->Navigate(w->core, w->url);
  ctl->lpVtbl->MoveFocus(ctl, 0 /* COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC */);
  return S_OK;
}

static const WvCtlDoneVtbl kCtlDoneVtbl = {wv_qi_ctl, wv_ref_ctl, wv_ref_ctl, wv_ctl_done};

static void wv_attach(WvWin *w) {
  w->done.lpVtbl = &kCtlDoneVtbl;
  w->done.hwnd = w->hwnd;
  if (FAILED(g_wvEnv->lpVtbl->CreateCoreWebView2Controller(g_wvEnv, w->hwnd, &w->done))) wv_fallback(w);
}

static HRESULT STDMETHODCALLTYPE wv_env_done(WvEnvDone *t, HRESULT hr, WvEnv *env) {
  (void)t;
  g_wvEnvPending = FALSE;
  wv_log(L"движок Edge %s за %llu мс", SUCCEEDED(hr) && env ? L"запущен" : L"НЕ запустился",
         GetTickCount64() - g_wvEnvT0);
  if (FAILED(hr) || !env) {
    g_wvBroken = TRUE; /* дальше сразу открываем по-запасному */
    for (int i = 0; i < WV_MAX; i++)
      if (g_wv[i].hwnd) wv_fallback(&g_wv[i]);
    return S_OK;
  }
  env->lpVtbl->AddRef(env);
  g_wvEnv = env;
  for (int i = 0; i < WV_MAX; i++)
    if (g_wv[i].hwnd && !g_wv[i].ctl) wv_attach(&g_wv[i]);
  return S_OK;
}

static const WvEnvDoneVtbl kEnvDoneVtbl = {wv_qi_env, wv_ref_env, wv_ref_env, wv_env_done};
static WvEnvDone g_wvEnvDone = {&kEnvDoneVtbl};

/* загрузчик из exe рядом с данными + есть ли на ПК сама среда WebView2 */
static BOOL wv_ready(void) {
  if (g_wvBroken || !g_dataDir[0]) return FALSE;
  if (g_wvCreate) return TRUE;
  wchar_t dir[MAX_PATH], dll[MAX_PATH];
  _snwprintf(dir, MAX_PATH, L"%s\\webview2", g_dataDir);
  dir[MAX_PATH - 1] = 0;
  CreateDirectoryW(dir, NULL);
  _snwprintf(dll, MAX_PATH, L"%s\\WebView2Loader.dll", dir);
  dll[MAX_PATH - 1] = 0;
  extract_rcdata(323, dll); /* занята (уже загружена) — берём ту, что есть */
  HMODULE m = LoadLibraryW(dll);
  if (!m) {
    g_wvBroken = TRUE;
    return FALSE;
  }
  WvVersionFn ver = (WvVersionFn)(void *)GetProcAddress(m, "GetAvailableCoreWebView2BrowserVersionString");
  WvCreateEnvFn cr = (WvCreateEnvFn)(void *)GetProcAddress(m, "CreateCoreWebView2EnvironmentWithOptions");
  LPWSTR v = NULL;
  if (!ver || !cr || FAILED(ver(NULL, &v)) || !v) {
    if (v) CoTaskMemFree(v);
    g_wvBroken = TRUE; /* среды WebView2 нет — не ждать её каждый раз */
    return FALSE;
  }
  CoTaskMemFree(v);
  g_wvCreate = cr;
  return TRUE;
}

static LRESULT CALLBACK WvProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WvWin *w = wv_find(hwnd);
  switch (msg) {
  case WM_SIZE:
    if (w && w->ctl) {
      RECT rc;
      GetClientRect(hwnd, &rc);
      w->ctl->lpVtbl->put_Bounds(w->ctl, rc);
    }
    return 0;
  case WM_SETFOCUS:
    if (w && w->ctl) w->ctl->lpVtbl->MoveFocus(w->ctl, 0);
    return 0;
  case WM_PAINT: {
    /* пока движок поднимается, окно не пустое: видно, что оно не зависло */
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    if (!w || !w->ctl) {
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(120, 120, 120));
      if (g_fontUi) SelectObject(dc, g_fontUi);
      DrawTextW(dc, L"Открываю…", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_CLOSE:
    /* не закрываем, а прячем: во второй раз окно откроется мгновенно и с
       тем, что в нём уже ввели */
    if (w && w->ctl) {
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    }
    break;
  case WM_DESTROY:
    free(GetPropW(hwnd, L"CursorPadWvPath"));
    RemovePropW(hwnd, L"CursorPadWvPath");
    if (w) {
      if (w->core) w->core->lpVtbl->Release(w->core);
      if (w->ctl) {
        w->ctl->lpVtbl->Close(w->ctl);
        w->ctl->lpVtbl->Release(w->ctl);
      }
      memset(w, 0, sizeof(*w));
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* путь к файлу → file:///C:/…, с пробелами и % в имени */
static void wv_file_url(const wchar_t *path, wchar_t *out, int cap) {
  int l = _snwprintf(out, cap, L"file:///");
  for (const wchar_t *p = path; *p && l < cap - 4; p++) {
    if (*p == L'\\') out[l++] = L'/';
    else if (*p == L' ') l += _snwprintf(out + l, cap - l, L"%%20");
    else if (*p == L'%') l += _snwprintf(out + l, cap - l, L"%%25");
    else if (*p == L'#') l += _snwprintf(out + l, cap - l, L"%%23");
    else out[l++] = *p;
  }
  out[l < cap ? l : cap - 1] = 0;
}

/* запасной путь: отдельное окно Edge без вкладок, иначе браузер по умолчанию */
static void wv_open_outside(const wchar_t *path) {
  wchar_t url[MAX_PATH * 3], args[MAX_PATH * 3 + 16];
  wv_file_url(path, url, MAX_PATH * 3);
  _snwprintf(args, MAX_PATH * 3 + 16, L"--app=\"%s\"", url);
  args[MAX_PATH * 3 + 15] = 0;
  if ((INT_PTR)ShellExecuteW(NULL, L"open", L"msedge", args, NULL, SW_SHOWNORMAL) > 32) return;
  if ((INT_PTR)ShellExecuteW(NULL, L"open", L"chrome", args, NULL, SW_SHOWNORMAL) > 32) return;
  if ((INT_PTR)ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL) <= 32)
    show_status(L"Нет браузера для HTML");
}

static void wv_fallback(WvWin *w) {
  /* из file:///C:/… обратно в путь не нужно: храним и путь в заголовке */
  wchar_t path[MAX_PATH];
  path[0] = 0;
  if (w->hwnd) {
    wchar_t *stored = (wchar_t *)GetPropW(w->hwnd, L"CursorPadWvPath");
    if (stored) lstrcpynW(path, stored, MAX_PATH);
    HWND h = w->hwnd;
    w->hwnd = NULL;
    w->done.hwnd = NULL;
    DestroyWindow(h); /* WM_DESTROY сам освободит сохранённый путь */
  }
  memset(w, 0, sizeof(*w));
  if (path[0]) wv_open_outside(path);
}

static BOOL wv_start_env(void) {
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  /* На рабочей сети движок Edge на старте ищет прокси (WPAD) и спрашивает
     SmartScreen и обновления — и ждёт ответа по многу секунд. Страницы у нас
     локальные, сеть им не нужна: всё это выключаем (параметры движка
     задаются этой переменной — так документировано у WebView2). */
  SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                          L"--no-proxy-server --disable-background-networking --disable-component-update "
                          L"--disable-features=msSmartScreenProtection,msEdgeSmartScreen");
  g_wvEnvT0 = GetTickCount64();
  wv_log(L"запускаю движок Edge");
  wchar_t udf[MAX_PATH];
  _snwprintf(udf, MAX_PATH, L"%s\\webview2\\data", g_dataDir);
  udf[MAX_PATH - 1] = 0;
  g_wvEnvPending = TRUE;
  if (FAILED(g_wvCreate(NULL, udf, NULL, &g_wvEnvDone))) {
    g_wvEnvPending = FALSE;
    g_wvBroken = TRUE;
    return FALSE;
  }
  return TRUE;
}

/* Открыть страницу в окне CursorPad. FALSE — WebView2 нет, открывайте иначе. */
static BOOL webview_open(const wchar_t *path, const wchar_t *title) {
  if (!wv_ready()) return FALSE;
  wchar_t url[MAX_PATH * 3];
  wv_file_url(path, url, MAX_PATH * 3);
  for (int i = 0; i < WV_MAX; i++) /* эта страница уже открывалась — показать то же окно */
    if (g_wv[i].hwnd && g_wv[i].ctl && !wcscmp(g_wv[i].url, url)) {
      ShowWindow(g_wv[i].hwnd, IsIconic(g_wv[i].hwnd) ? SW_RESTORE : SW_SHOW);
      SetForegroundWindow(g_wv[i].hwnd);
      wv_log(L"%s: уже открыта — показал сразу", title);
      return TRUE;
    }
  WvWin *w = wv_find(NULL);
  if (!w) return FALSE; /* восемь окон открыто — хватит, пусть откроется в Edge */
  static BOOL reg;
  if (!reg) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WvProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CursorPadWebView";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    reg = RegisterClassExW(&wc) != 0;
  }
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int ww = (wa.right - wa.left) * 3 / 4, wh = (wa.bottom - wa.top) * 7 / 8;
  if (ww > 1280) ww = 1280;
  wchar_t cap[160];
  _snwprintf(cap, 160, L"%s — CursorPad", title);
  cap[159] = 0;
  HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"CursorPadWebView", cap, WS_OVERLAPPEDWINDOW,
                           wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2,
                           ww, wh, NULL, NULL, g_inst, NULL);
  if (!h) return FALSE;
  memset(w, 0, sizeof(*w));
  w->hwnd = h;
  w->t0 = GetTickCount64();
  lstrcpynW(w->url, url, MAX_PATH * 3);
  wv_log(L"%s: открываю (движок %s)", title, g_wvEnv ? L"уже запущен" : (g_wvEnvPending ? L"запускается" : L"ещё не запущен"));
  SetPropW(h, L"CursorPadWvPath", _wcsdup(path));
  ShowWindow(h, SW_SHOWNORMAL);
  SetForegroundWindow(h);
  if (g_wvEnv) wv_attach(w);
  else if (!g_wvEnvPending && !wv_start_env()) wv_fallback(w);
  return TRUE;
}

/* Движок Edge поднимается небыстро. Запускаем его заранее: через 20 секунд
   после старта программы (таймер в cursorpad.c) и ещё раз — если к тому
   времени не вышло — как только нажали «Ещё». */
static void webview_prewarm(void) {
  if (g_wvEnv || g_wvEnvPending || !wv_ready()) return;
  wv_start_env();
}
