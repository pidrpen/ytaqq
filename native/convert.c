/* Explorer Shift-drag -> a real OLE drop target. No injected input, clipboard
   changes or Explorer extensions. Drop always returns COPY, never MOVE. */
#include "convert_core.c"
#define TIMER_CONVERT 1739
#define WM_CONVERT_DONE (WM_APP + 139)
static HWND g_cvWnd;
static BOOL g_cvOle, g_cvRegistered, g_cvWord, g_cvExcel;
static BOOL g_cvDown, g_cvSource, g_cvDismissed;
static POINT g_cvOrigin;
static DWORD g_cvReleased;
static volatile LONG g_cvBusy;
static int g_cvSize = 340, g_cvCount, g_cvFormats[16], g_cvHover = -1;
static wchar_t g_cvPath[MAX_PATH], g_cvLast[MAX_PATH];
#define CV_FILES 128
typedef struct { int count; wchar_t paths[CV_FILES][MAX_PATH]; } CvSelection;
static CvSelection g_cvSelection;
static BOOL cvx_dispatch(const CvSelection *selection, int action);
static void cvx_show(void);
static void cvx_shutdown(void);
static void cv_hide(void) { if (g_cvWnd) ShowWindow(g_cvWnd, SW_HIDE); g_cvHover = -1; }
static void cv_reveal(void) {
  if (!g_cvLast[0]) return;
  wchar_t args[MAX_PATH + 40]; swprintf(args, MAX_PATH + 40, L"/select,\"%ls\"", g_cvLast);
  ShellExecuteW(g_hwnd, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
}
#ifdef CV_RING_TEST
static DWORD WINAPI cv_worker(void *parameter) {
  CvJob *job = (CvJob *)parameter; cv_convert(job);
  if (!PostMessageW(g_hwnd, WM_CONVERT_DONE, 0, (LPARAM)job)) { free(job); InterlockedExchange(&g_cvBusy, 0); }
  return 0;
}
#endif
static void cv_done(CvJob *job) {
  InterlockedExchange(&g_cvBusy, 0);
  if (job->out[0]) {
    ai_log(L"Конвертер: %ls; результат сохранён", job->label[0] ? job->label : kCvFormats[job->format].label);
    lstrcpynW(g_cvLast, job->out, MAX_PATH);
    show_status(job->summary[0] ? job->summary : L"Конвертация готова. Копия сохранена рядом с исходником.");
    upd_balloon(6, L"CursorPad: файл преобразован", job->out);
  } else if (job->error[0]) {
    ai_log(L"Конвертер: %ls", job->error);
    show_status(job->error);
    MessageBoxW(g_hwnd, job->error[0] ? job->error : L"Не удалось преобразовать файл. Исходник сохранён.", L"CursorPad — конвертация", MB_OK | MB_ICONINFORMATION);
  } else if (job->summary[0]) show_status(job->summary);
  free(job);
}
static BOOL cv_explorer_at(POINT point) {
  HWND window = WindowFromPoint(point);
  for (int i = 0; window && i < 12; i++, window = GetParent(window)) {
    wchar_t name[80]; GetClassNameW(window, name, 80);
    if (!wcscmp(name, L"CabinetWClass") || !wcscmp(name, L"ExploreWClass") ||
        !wcscmp(name, L"Progman") || !wcscmp(name, L"WorkerW")) return TRUE;
  }
  /* Explorer's transient drag image may cover the original list view. */
  wchar_t name[80]; HWND foreground = GetForegroundWindow();
  if (foreground && GetClassNameW(GetAncestor(foreground, GA_ROOT), name, 80) &&
      (!wcscmp(name,L"CabinetWClass") || !wcscmp(name,L"ExploreWClass") ||
       !wcscmp(name,L"Progman") || !wcscmp(name,L"WorkerW"))) return TRUE;
  return FALSE;
}
static int cv_hit(POINT point) {
  ScreenToClient(g_cvWnd, &point);
  double x = point.x - g_cvSize / 2.0, y = point.y - g_cvSize / 2.0;
  double radius = sqrt(x*x + y*y);
  if (!g_cvCount || radius < g_cvSize * .23 || radius > g_cvSize * .49) return -1;
  double angle = atan2(y, x) + 1.5707963267948966;
  if (angle < 0) angle += 6.283185307179586;
  return (int)(angle * g_cvCount / 6.283185307179586) % g_cvCount;
}
static void cv_show(POINT point) {
  g_cvPath[0] = 0; g_cvCount = 0; g_cvHover = -1;
  HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {sizeof(info)}; GetMonitorInfoW(monitor, &info);
  int x = point.x - g_cvSize / 2, y = point.y - g_cvSize / 2;
  if (x + g_cvSize > info.rcWork.right) x = info.rcWork.right - g_cvSize;
  if (y + g_cvSize > info.rcWork.bottom) y = info.rcWork.bottom - g_cvSize;
  if (x < info.rcWork.left) x = info.rcWork.left;
  if (y < info.rcWork.top) y = info.rcWork.top;
  SetWindowPos(g_cvWnd, HWND_TOPMOST, x, y, g_cvSize, g_cvSize, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(g_cvWnd, NULL, FALSE);
}
static void cv_tick(void) {
  if (!g_cvRegistered) return;
  POINT point; GetCursorPos(&point);
  BOOL down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  BOOL shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  if (down && !g_cvDown) {
    g_cvOrigin = point; g_cvSource = cv_explorer_at(point); g_cvDismissed = FALSE; g_cvReleased = 0;
  }
  g_cvDown = down;
  if (!down) {
    if (!g_cvReleased) g_cvReleased = GetTickCount();
    /* Let an in-flight OLE Drop arrive before hiding the target. */
    if (GetTickCount() - g_cvReleased > 220) cv_hide();
    return;
  }
  if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { cv_hide(); g_cvDismissed = TRUE; return; }
  if (!shift || g_cvBusy) { cv_hide(); return; }
  if (IsWindowVisible(g_cvWnd)) {
    RECT r; GetWindowRect(g_cvWnd, &r); InflateRect(&r, 45, 45);
    if (!PtInRect(&r, point)) { cv_hide(); g_cvDismissed = TRUE; }
    return;
  }
  if (!g_cvDismissed && g_cvSource &&
      (abs(point.x - g_cvOrigin.x) > GetSystemMetrics(SM_CXDRAG) + 8 ||
       abs(point.y - g_cvOrigin.y) > GetSystemMetrics(SM_CYDRAG) + 8)) cv_show(point);
}
static BOOL cv_data(IDataObject *data, CvSelection *selection) {
  FORMATETC format = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; STGMEDIUM medium;
  memset(&medium, 0, sizeof(medium)); memset(selection,0,sizeof(*selection));
  if (FAILED(data->lpVtbl->GetData(data, &format, &medium))) return FALSE;
  UINT count=medium.tymed==TYMED_HGLOBAL && medium.hGlobal ? DragQueryFileW((HDROP)medium.hGlobal,0xffffffff,NULL,0):0;
  BOOL ok=count>0 && count<=CV_FILES;
  for (UINT i=0;ok && i<count;i++) {
    UINT size=DragQueryFileW((HDROP)medium.hGlobal,i,NULL,0);
    wchar_t *path=selection->paths[i];
    ok=size>0 && size<MAX_PATH && DragQueryFileW((HDROP)medium.hGlobal,i,path,MAX_PATH)==size;
    if (ok) { DWORD attr=GetFileAttributesW(path); ok=attr!=INVALID_FILE_ATTRIBUTES && !(attr&FILE_ATTRIBUTE_DIRECTORY); }
  }
  ReleaseStgMedium(&medium); selection->count=ok?(int)count:0; return ok;
}
static BOOL cvx_can(int action, const wchar_t *path) {
  int image=cv_image_type(path); const wchar_t *e=cv_extension(path);
  BOOL pdf=!_wcsicmp(e,L"pdf"), word=!_wcsicmp(e,L"docx") && g_cvWord, excel=!_wcsicmp(e,L"xlsx") && g_cvExcel;
  if (action==7 || action==15) return TRUE;
  if (action<4 || action==8 || action==9) return image>=0;
  if (action==4) return image>=0 || word || excel;
  if (action==5) return word;
  if (action==6) return excel;
  if (action==14) return pdf || image>=0 || word || excel;
  return pdf;
}
static BOOL cvx_all(const CvSelection *s,int action) { if (!s->count) return FALSE; for(int i=0;i<s->count;i++) if(!cvx_can(action,s->paths[i])) return FALSE; return TRUE; }
static void cv_menu_formats(void) {
  g_cvCount=0;
  if (!g_cvSelection.count) return;
  if (cvx_all(&g_cvSelection,0)) {
    for (int i=0;i<4;i++) if(g_cvSelection.count>1 || cv_image_type(g_cvPath)!=i) g_cvFormats[g_cvCount++]=i;
    g_cvFormats[g_cvCount++]=8; g_cvFormats[g_cvCount++]=9;
  } else if (cvx_all(&g_cvSelection,10)) { for(int i=10;i<=13;i++) g_cvFormats[g_cvCount++]=i; }
  else { for(int i=4;i<=6;i++) if(cvx_all(&g_cvSelection,i)) g_cvFormats[g_cvCount++]=i; if (!g_cvCount && cvx_all(&g_cvSelection,14)) g_cvFormats[g_cvCount++]=14; }
  g_cvFormats[g_cvCount++]=7; g_cvFormats[g_cvCount++]=15;
}
typedef struct { IDropTarget iface; LONG refs; } CvDrop;
static HRESULT STDMETHODCALLTYPE cv_query(IDropTarget *self, REFIID iid, void **out) {
  if (!out) return E_POINTER;
  *out = NULL;
  if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDropTarget)) {
    *out = self; self->lpVtbl->AddRef(self); return S_OK;
  }
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cv_addref(IDropTarget *self) { return (ULONG)InterlockedIncrement(&((CvDrop *)self)->refs); }
static ULONG STDMETHODCALLTYPE cv_release(IDropTarget *self) { return (ULONG)InterlockedDecrement(&((CvDrop *)self)->refs); }
static HRESULT STDMETHODCALLTYPE cv_over(IDropTarget *self, DWORD keys, POINTL position, DWORD *effect) {
  (void)self; POINT point = {position.x, position.y}; int hover = cv_hit(point);
  if (g_cvBusy || !(keys & MK_SHIFT) || !g_cvPath[0] || !(*effect & DROPEFFECT_COPY)) hover = -1;
  if (hover != g_cvHover) { g_cvHover = hover; InvalidateRect(g_cvWnd, NULL, FALSE); }
  *effect = hover >= 0 ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE cv_enter(IDropTarget *self, IDataObject *data, DWORD keys, POINTL position, DWORD *effect) {
  g_cvCount = 0; g_cvPath[0] = 0;
  if (cv_data(data, &g_cvSelection)) { lstrcpynW(g_cvPath,g_cvSelection.paths[0],MAX_PATH); cv_menu_formats(); }
  InvalidateRect(g_cvWnd, NULL, FALSE); return cv_over(self, keys, position, effect);
}
static HRESULT STDMETHODCALLTYPE cv_leave(IDropTarget *self) {
  (void)self; g_cvHover = -1; InvalidateRect(g_cvWnd, NULL, FALSE); return S_OK;
}
static HRESULT STDMETHODCALLTYPE cv_drop(IDropTarget *self, IDataObject *data, DWORD keys, POINTL position, DWORD *effect) {
  cv_over(self, keys, position, effect);
  CvSelection *dropped=(CvSelection*)calloc(1,sizeof(*dropped));
  BOOL accepted=g_cvHover>=0 && dropped && cv_data(data,dropped) && dropped->count==g_cvSelection.count;
  for(int i=0;accepted && i<dropped->count;i++) if(wcscmp(dropped->paths[i],g_cvSelection.paths[i])) accepted=FALSE;
  int target = accepted ? g_cvFormats[g_cvHover] : -1;
  *effect = DROPEFFECT_NONE;
  if (accepted && !g_cvBusy && cvx_dispatch(dropped,target)) *effect=DROPEFFECT_COPY;
  free(dropped);
  g_cvDismissed = TRUE; cv_hide(); return S_OK;
}
static IDropTargetVtbl g_cvVtbl = {cv_query, cv_addref, cv_release, cv_enter, cv_over, cv_leave, cv_drop};
static CvDrop g_cvDrop = {{&g_cvVtbl}, 1};
static LRESULT CALLBACK cv_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  if (message == WM_ERASEBKGND) return 1;
  if (message == WM_PAINT) {
    PAINTSTRUCT ps; HDC screen = BeginPaint(hwnd, &ps), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, g_cvSize, g_cvSize), oldBitmap = SelectObject(dc, bitmap);
    RECT bounds = {0,0,g_cvSize,g_cvSize}; HBRUSH background = CreateSolidBrush(RGB(27,31,40)); FillRect(dc, &bounds, background); DeleteObject(background);
    int count = g_cvCount ? g_cvCount : 4, center = g_cvSize / 2;
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(27,31,40)); HPEN oldPen = SelectObject(dc, pen);
    for (int i = 0; i < count; i++) {
      POINT points[67]; points[0].x = center; points[0].y = center;
      for (int j = 0; j <= 64; j++) {
        double a = -1.5707963267948966 + 6.283185307179586 * (i + j / 64.0) / count;
        points[j+1].x = center + (int)(cos(a) * (center - 3)); points[j+1].y = center + (int)(sin(a) * (center - 3));
      }
      HBRUSH brush = CreateSolidBrush(i == g_cvHover ? RGB(214,153,63) : RGB(48,57,72)), oldBrush = SelectObject(dc, brush);
      Polygon(dc, points, 66); SelectObject(dc, oldBrush); DeleteObject(brush);
    }
    HBRUSH inner = CreateSolidBrush(RGB(27,31,40)), oldBrush = SelectObject(dc, inner);
    int radius = (int)(g_cvSize * .23); Ellipse(dc, center-radius, center-radius, center+radius, center+radius);
    SelectObject(dc, oldBrush); DeleteObject(inner); SelectObject(dc, oldPen); DeleteObject(pen);
    HFONT font = CreateFontW(-MulDiv(g_cvCount>6?12:15, g_cvSize, 340), 0, 0, 0, FW_SEMIBOLD, FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    HFONT oldFont = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < count; i++) {
      double a = -1.5707963267948966 + 6.283185307179586 * (i + .5) / count;
      int x = center + (int)(cos(a) * g_cvSize * .355), y = center + (int)(sin(a) * g_cvSize * .355);
      RECT label = {x-g_cvSize/6,y-24,x+g_cvSize/6,y+24};
      SetTextColor(dc, i == g_cvHover ? RGB(25,28,34) : RGB(241,243,247));
      DrawTextW(dc, g_cvCount ? kCvFormats[g_cvFormats[i]].label : L"·", -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    RECT label = {center-radius+5,center-radius+10,center+radius-5,center+radius-10}; SetTextColor(dc, RGB(210,217,228));
    wchar_t selected[100]; swprintf(selected,100,L"Файлов: %d\nВыберите\nдействие\n\nEsc — отмена",g_cvSelection.count);
    const wchar_t *text = g_cvCount ? selected : g_cvPath[0] ?
      (!_wcsicmp(cv_extension(g_cvPath), L"docx") && !g_cvWord ? L"Нужен\nWord" :
       !_wcsicmp(cv_extension(g_cvPath), L"xlsx") && !g_cvExcel ? L"Нужен\nExcel" : L"Формат не\nподдержан") : L"Перетащите\nфайлы\nс Shift";
    DrawTextW(dc, text, -1, &label, DT_CENTER | DT_WORDBREAK);
    SelectObject(dc, oldFont); DeleteObject(font);
    BitBlt(screen,0,0,g_cvSize,g_cvSize,dc,0,0,SRCCOPY); SelectObject(dc,oldBitmap); DeleteObject(bitmap); DeleteDC(dc); EndPaint(hwnd,&ps); return 0;
  }
  return DefWindowProcW(hwnd,message,wp,lp);
}
static void cv_init(void) {
  HRESULT hr = OleInitialize(NULL);
  if (FAILED(hr)) { ai_log(L"Конвертер: OLE недоступен, 0x%08lx", (unsigned long)hr); return; }
  g_cvOle = TRUE;
  g_cvWord = cv_program(L"Word"); g_cvExcel = cv_program(L"Excel");
  HDC dc = GetDC(g_hwnd); int dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(g_hwnd, dc);
  typedef UINT (WINAPI *DpiForWindow)(HWND);
  DpiForWindow dpiForWindow = (DpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
  if (dpiForWindow) dpi = (int)dpiForWindow(g_hwnd);
  g_cvSize = MulDiv(340, dpi, 96);
  WNDCLASSW klass = {0}; klass.lpfnWndProc = cv_proc; klass.hInstance = g_inst; klass.lpszClassName = L"CursorPadConvertRing"; klass.hCursor = LoadCursorW(NULL,IDC_ARROW);
  if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
  g_cvWnd = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,klass.lpszClassName,L"Конвертация CursorPad",WS_POPUP,0,0,g_cvSize,g_cvSize,g_hwnd,NULL,g_inst,NULL);
  if (!g_cvWnd) return;
  HRGN region = CreateEllipticRgn(0,0,g_cvSize,g_cvSize); if (!SetWindowRgn(g_cvWnd,region,FALSE)) DeleteObject(region);
  hr = RegisterDragDrop(g_cvWnd,&g_cvDrop.iface); g_cvRegistered = SUCCEEDED(hr);
  ai_log(L"Конвертер: OLE 0x%08lx; Word %ls; Excel %ls", (unsigned long)hr, g_cvWord ? L"есть" : L"нет", g_cvExcel ? L"есть" : L"нет");
  if (g_cvRegistered) SetTimer(g_hwnd,TIMER_CONVERT,30,NULL);
}
static void cv_shutdown(void) {
  cvx_shutdown();
  KillTimer(g_hwnd,TIMER_CONVERT);
  if (g_cvRegistered) { RevokeDragDrop(g_cvWnd); g_cvRegistered = FALSE; }
  if (g_cvWnd) { DestroyWindow(g_cvWnd); g_cvWnd = NULL; }
  if (g_cvOle) { OleUninitialize(); g_cvOle = FALSE; }
}
#ifdef CV_RING_TEST
static BOOL cvx_dispatch(const CvSelection *selection,int action) {
  if(action>=7 || selection->count!=1 || InterlockedCompareExchange(&g_cvBusy,1,0)) return FALSE;
  CvJob *job=(CvJob*)calloc(1,sizeof(*job)); if(!job) { g_cvBusy=0; return FALSE; }
  lstrcpynW(job->src,selection->paths[0],MAX_PATH); job->format=action;
  HANDLE thread=CreateThread(NULL,0,cv_worker,job,0,NULL); if(!thread) { free(job);g_cvBusy=0;return FALSE; } CloseHandle(thread); return TRUE;
}
static void cvx_show(void) { }
static void cvx_shutdown(void) { }
#else
#include "convert_workbench.c"
#endif
