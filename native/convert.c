/* Explorer Shift-drag -> a real OLE drop target. No injected input, clipboard
   changes or Explorer extensions. Drop always returns COPY, never MOVE. */
#include "convert_core.c"
#ifndef CV_RING_TEST
#include "desktop_create.c"
#endif
#define TIMER_CONVERT 1739
#define WM_CONVERT_DONE (WM_APP + 139)
#define WM_CV_CREATE (WM_APP + 141) /* wParam — что создать на рабочем столе: 1 папка, 2 TXT, 3 Word, 4 Excel */
#define WM_CV_TIFF_MERGE (WM_APP + 142) /* lParam — CvSelection*: в окно «Объединение TIFF / PDF» */
static HWND g_cvWnd;
static BOOL g_cvOle, g_cvRegistered, g_cvWord, g_cvExcel;
static BOOL g_cvDown, g_cvSource, g_cvDismissed;
static BOOL g_cvDesktopGesture,g_cvDesktopDragged;
static BOOL g_cvCreate; /* кольцо «Создать» на пустом месте рабочего стола, без перетаскивания файлов */
static POINT g_cvOrigin;
static DWORD g_cvReleased;
static volatile LONG g_cvBusy;
static int g_cvSize = 360, g_cvCount, g_cvFormats[20], g_cvHover = -1;
static wchar_t g_cvPath[MAX_PATH], g_cvLast[MAX_PATH];
#define CV_FILES 128
typedef struct { int count; wchar_t paths[CV_FILES][MAX_PATH]; } CvSelection;
static CvSelection g_cvSelection;
static IDataObject *g_cvPendingData;
static DWORD g_cvDataAttempt, g_cvEnteredAt;
/* что пришло от проводника: ждём, файлы, одни папки, больше CV_FILES, не прочиталось */
enum { CV_READ_WAIT, CV_READ_OK, CV_READ_FOLDERS, CV_READ_MANY, CV_READ_FAILED };
static int g_cvRead, g_cvSkipped; /* g_cvSkipped — сколько папок пропущено */
static BOOL cvx_dispatch(const CvSelection *selection, int action);
static void cvx_show(void);
static void cvx_shutdown(void);
static void cv_pending_clear(void) { if(g_cvPendingData){g_cvPendingData->lpVtbl->Release(g_cvPendingData);g_cvPendingData=NULL;} }
#include "convert_ring.c" /* вид кольца: прозрачное окно, плитки, центр */
#include "convert_select.c" /* выделение Проводника на миг нажатия Shift */
static void cv_redraw(void) { if(g_cvWnd&&IsWindowVisible(g_cvWnd))cv_paint(); }
static void cv_hide(void) { cv_pending_clear();if (g_cvWnd) ShowWindow(g_cvWnd, SW_HIDE); g_cvHover = -1;g_cvInHub=FALSE;g_cvCreate=FALSE; }
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
/* номер плитки под точкой экрана; -1 — центр (отмена) или мимо. Плитка 0 — сверху */
static int cv_hit(POINT point) {
  CvGeo g = cv_geo(g_cvSize, 1.0f);
  double x = point.x - g_cvAt.x - g.c, y = point.y - g_cvAt.y - g.c;
  double radius = sqrt(x*x + y*y);
  g_cvInHub = g_cvCount && radius < g.rin;
  if (!g_cvCount || radius < g.rin || radius > g.R + g.pop + 6 * g.s) return -1;
  double angle = atan2(y, x) + 1.5707963267948966 + 3.141592653589793 / g_cvCount;
  while (angle < 0) angle += 6.283185307179586;
  return (int)(angle * g_cvCount / 6.283185307179586) % g_cvCount;
}
/* create — кольцо «Создать на рабочем столе» (папка, TXT, Word, Excel); иначе ждёт файлы от Проводника */
static void cv_show(POINT point, BOOL create) {
  cv_pending_clear();g_cvSelection.count=0;g_cvPath[0] = 0; g_cvCount = 0; g_cvHover = -1;g_cvInHub=FALSE;g_cvRead=CV_READ_WAIT;g_cvSkipped=0;
  memset(g_cvLevel,0,sizeof(g_cvLevel));
  g_cvCreate=create;
  for (int i=0;create && i<4;i++) g_cvFormats[g_cvCount++]=20+i;
  g_cvDpi = cv_monitor_dpi(point); g_cvSize = MulDiv(CV_RING_BASE, g_cvDpi, 96);
  HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {sizeof(info)}; GetMonitorInfoW(monitor, &info);
  int x = point.x - g_cvSize / 2, y = point.y - g_cvSize / 2;
  if (x + g_cvSize > info.rcWork.right) x = info.rcWork.right - g_cvSize;
  if (y + g_cvSize > info.rcWork.bottom) y = info.rcWork.bottom - g_cvSize;
  if (x < info.rcWork.left) x = info.rcWork.left;
  if (y < info.rcWork.top) y = info.rcWork.top;
  g_cvAt.x = x; g_cvAt.y = y; g_cvShownAt = GetTickCount() | 1;
  cv_paint();
  SetWindowPos(g_cvWnd, HWND_TOPMOST, x, y, g_cvSize, g_cvSize, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
static void cv_gesture(POINT point,BOOL down,BOOL shift,BOOL escape,BOOL source,BOOL emptyDesktop) {
  if (down && !g_cvDown) {
    g_cvOrigin = point; g_cvSource = source; g_cvDismissed = FALSE; g_cvReleased = 0;
    g_cvDesktopGesture=shift&&emptyDesktop;g_cvDesktopDragged=FALSE;
  }
  g_cvDown = down;
  if (!down) {
    /* рабочий стол: отпустили на плитке, держа Shift, — создать; в центре или без Shift — ничего */
    if(g_cvCreate&&!g_cvDismissed&&shift&&!escape&&g_cvHover>=0) PostMessageW(g_cvWnd,WM_CV_CREATE,(WPARAM)(g_cvFormats[g_cvHover]-19),0);
    if(g_cvCreate) cv_hide(); /* здесь нет сброса OLE, ждать нечего */
    g_cvDesktopGesture=FALSE;g_cvDesktopDragged=FALSE;
    if (!g_cvReleased) g_cvReleased = GetTickCount();
    /* Let an in-flight OLE Drop arrive before hiding the target. */
    if (GetTickCount() - g_cvReleased > 220) cv_hide();
    return;
  }
  if (escape) { cv_hide(); g_cvDismissed = TRUE;g_cvDesktopGesture=FALSE;g_cvDesktopDragged=FALSE;return; }
  if (!shift || g_cvBusy) { cv_hide();g_cvDesktopGesture=FALSE;g_cvDesktopDragged=FALSE;return; }
  if(g_cvDesktopGesture) { /* пустое место стола: протянули — кольцо «Создать» у курсора */
    if(!g_cvDesktopDragged) {
      if(abs(point.x-g_cvOrigin.x)>GetSystemMetrics(SM_CXDRAG)+8||abs(point.y-g_cvOrigin.y)>GetSystemMetrics(SM_CYDRAG)+8){g_cvDesktopDragged=TRUE;cv_show(point,TRUE);}
    } else if(g_cvCreate && IsWindowVisible(g_cvWnd)) {
      RECT r; GetWindowRect(g_cvWnd, &r); InflateRect(&r, 45, 45);
      if (!PtInRect(&r, point)) { cv_hide(); g_cvDismissed = TRUE; return; }
      BOOL hub=g_cvInHub; int hover=cv_hit(point); /* мышь у стола, а не у кольца, — наведение по таймеру */
      if(hover!=g_cvHover||hub!=g_cvInHub){g_cvHover=hover;cv_redraw();}
    }
    return;
  }
  if (IsWindowVisible(g_cvWnd)) {
    RECT r; GetWindowRect(g_cvWnd, &r); InflateRect(&r, 45, 45);
    if (!PtInRect(&r, point)) { cv_hide(); g_cvDismissed = TRUE; }
    return;
  }
  if (!g_cvDismissed && g_cvSource &&
      (abs(point.x - g_cvOrigin.x) > GetSystemMetrics(SM_CXDRAG) + 8 ||
       abs(point.y - g_cvOrigin.y) > GetSystemMetrics(SM_CYDRAG) + 8)) cv_show(point,FALSE);
}
static void cv_tick(void) {
  if(!g_cvRegistered)return;
  /* появление, подсветка плиток и огонёк ожидания — кадрами этого же таймера */
  if(IsWindowVisible(g_cvWnd)&&cv_animating(GetTickCount())){cv_step_levels();cv_paint();}
  POINT point;GetCursorPos(&point);BOOL down=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
  BOOL shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,empty=FALSE;
#ifndef CV_RING_TEST
  if(shift&&down&&!g_cvDown)empty=dc_empty_desktop(point);
#endif
  BOOL source=down&&!g_cvDown?cv_explorer_at(point):FALSE;
  cv_snap_track(down,shift,point);
  cv_gesture(point,down,shift,(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0,source,empty);
}
/* Файлы из перетаскивания. Папки пропускаются: Shift+щелчок в проводнике
   выделяет диапазон, и в него легко попадает папка — раньше из-за неё
   отказывала вся подборка, а кольцо молча ждало файлов. */
static int cv_data(IDataObject *data, CvSelection *selection, int *skipped) {
  FORMATETC format = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; STGMEDIUM medium;
  memset(&medium, 0, sizeof(medium)); memset(selection,0,sizeof(*selection)); if(skipped)*skipped=0;
  if (FAILED(data->lpVtbl->GetData(data, &format, &medium))) return CV_READ_FAILED;
  UINT count=medium.tymed==TYMED_HGLOBAL && medium.hGlobal ? DragQueryFileW((HDROP)medium.hGlobal,0xffffffff,NULL,0):0;
  BOOL ok=count>0, many=FALSE; int kept=0, folders=0;
  for (UINT i=0;ok && i<count;i++) {
    UINT size=DragQueryFileW((HDROP)medium.hGlobal,i,NULL,0); wchar_t path[MAX_PATH];
    ok=size>0 && size<MAX_PATH && DragQueryFileW((HDROP)medium.hGlobal,i,path,MAX_PATH)==size;
    DWORD attr=ok?GetFileAttributesW(path):INVALID_FILE_ATTRIBUTES; ok=attr!=INVALID_FILE_ATTRIBUTES;
    if (!ok) break;
    if (attr&FILE_ATTRIBUTE_DIRECTORY) { folders++; continue; }
    if (kept==CV_FILES) { many=TRUE; break; }
    lstrcpynW(selection->paths[kept++],path,MAX_PATH);
  }
  ReleaseStgMedium(&medium); selection->count=ok && !many?kept:0; if(skipped)*skipped=folders;
  return !ok ? CV_READ_FAILED : many ? CV_READ_MANY : kept ? CV_READ_OK : CV_READ_FOLDERS;
}
static BOOL cvx_can(int action, const wchar_t *path) {
  int image=cv_image_type(path); const wchar_t *e=cv_extension(path);
  BOOL pdf=!_wcsicmp(e,L"pdf"), word=cv_word_file(path) && g_cvWord, excel=cv_excel_file(path) && g_cvExcel;
  if (action==7 || action==15) return TRUE;
  if (action<4) return image>=0 || pdf; /* PDF → картинки: по странице, 300 dpi */
  if (action==18) return image==3;      /* разделить многостраничный TIFF */
  if (action==19) return image>=0;      /* один TIFF — в окне объединения */
  if (action==8 || action==9) return image>=0;
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
  BOOL pdfs=cvx_all(&g_cvSelection,10);
  if (cvx_all(&g_cvSelection,0)) { /* картинки и PDF — в картинки; у PDF без BMP, он для чертежей не нужен */
    /* BMP в кольце не нужен (чертежам и сканам — PNG/TIFF); остался в окне «Ещё» */
    /* формат, в котором уже все файлы, не предлагается */
    for (int i=0;i<4;i++) {
      BOOL same=TRUE;for(int k=0;same && k<g_cvSelection.count;k++) same=cv_image_type(g_cvSelection.paths[k])==i;
      if(i!=2 && !same) g_cvFormats[g_cvCount++]=i;
    }
    /* сканы чаще всего нужны в PDF: каждый отдельно, а несколько — ещё и одним файлом */
    if(cvx_all(&g_cvSelection,4)) g_cvFormats[g_cvCount++]=4;
    if(g_cvSelection.count>1 && !pdfs && cvx_all(&g_cvSelection,14)) g_cvFormats[g_cvCount++]=14;
    if(g_cvSelection.count>1 && cvx_all(&g_cvSelection,19)) g_cvFormats[g_cvCount++]=19;
    if(cvx_all(&g_cvSelection,18)) g_cvFormats[g_cvCount++]=18;
    if(cvx_all(&g_cvSelection,8)) { g_cvFormats[g_cvCount++]=8; g_cvFormats[g_cvCount++]=9; }
    if(pdfs) for(int i=10;i<=13;i++) g_cvFormats[g_cvCount++]=i;
  } else if (pdfs) { for(int i=10;i<=13;i++) g_cvFormats[g_cvCount++]=i; }
  else { for(int i=4;i<=6;i++) if(cvx_all(&g_cvSelection,i)) g_cvFormats[g_cvCount++]=i; if (!g_cvCount && cvx_all(&g_cvSelection,14)) g_cvFormats[g_cvCount++]=14; }
  g_cvFormats[g_cvCount++]=7; g_cvFormats[g_cvCount++]=15;
}
static void cv_read_pending(void) {
  if(!g_cvPendingData)return;
  g_cvDataAttempt=GetTickCount();
  g_cvRead=cv_data(g_cvPendingData,&g_cvSelection,&g_cvSkipped);
  if(g_cvRead==CV_READ_OK){
    cv_snap_apply();lstrcpynW(g_cvPath,g_cvSelection.paths[0],MAX_PATH);cv_menu_formats();
    cv_thumb_request(g_cvPath,MulDiv(58,g_cvSize,CV_RING_BASE)); /* миниатюра в центр — своим потоком */
  }
  /* не прочиталось — повторим в DragOver: проводник может отдать список позже */
  if(g_cvRead!=CV_READ_FAILED)cv_pending_clear();
  cv_redraw();
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
  (void)self;
  if(g_cvPendingData&&GetTickCount()-g_cvDataAttempt>=60)cv_read_pending();
  POINT point = {position.x, position.y}; BOOL hub = g_cvInHub; int hover = cv_hit(point);
  if (g_cvBusy || !(keys & MK_SHIFT) || !g_cvPath[0] || !(*effect & DROPEFFECT_COPY)) { hover = -1; g_cvInHub = FALSE; }
  if (hover != g_cvHover || hub != g_cvInHub) { g_cvHover = hover; cv_redraw(); }
  *effect = hover >= 0 ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE cv_enter(IDropTarget *self, IDataObject *data, DWORD keys, POINTL position, DWORD *effect) {
  cv_pending_clear();g_cvCount = 0; g_cvPath[0] = 0;g_cvSelection.count=0;g_cvRead=CV_READ_WAIT;g_cvSkipped=0;g_cvEnteredAt=GetTickCount();g_cvWide=FALSE;
  if(data){g_cvPendingData=data;data->lpVtbl->AddRef(data);cv_read_pending();}
  cv_redraw();return cv_over(self, keys, position, effect);
}
static HRESULT STDMETHODCALLTYPE cv_leave(IDropTarget *self) {
  (void)self;cv_pending_clear();g_cvHover = -1;g_cvInHub=FALSE;cv_redraw();return S_OK;
}
static HRESULT STDMETHODCALLTYPE cv_drop(IDropTarget *self, IDataObject *data, DWORD keys, POINTL position, DWORD *effect) {
  cv_over(self, keys, position, effect);
  CvSelection *dropped=(CvSelection*)calloc(1,sizeof(*dropped));
  /* бросили то же, что принесли; если кольцо взяло запомненное выделение — сверка с принесённым */
  const CvSelection *brought=g_cvWide?&g_cvDragged:&g_cvSelection;
  BOOL accepted=g_cvHover>=0 && dropped && cv_data(data,dropped,NULL)==CV_READ_OK && dropped->count==brought->count;
  for(int i=0;accepted && i<dropped->count;i++) if(wcscmp(dropped->paths[i],brought->paths[i])) accepted=FALSE;
  if(accepted) *dropped=g_cvSelection;
  int target = accepted ? g_cvFormats[g_cvHover] : -1;
  *effect = DROPEFFECT_NONE;
  if (accepted && !g_cvBusy && cvx_dispatch(dropped,target)) *effect=DROPEFFECT_COPY;
  free(dropped);
  g_cvDismissed = TRUE; cv_hide(); return S_OK;
}
static IDropTargetVtbl g_cvVtbl = {cv_query, cv_addref, cv_release, cv_enter, cv_over, cv_leave, cv_drop};
static CvDrop g_cvDrop = {{&g_cvVtbl}, 1};
static LRESULT CALLBACK cv_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
#ifndef CV_RING_TEST
  if(message==WM_CV_CREATE){dc_create((int)wp);return 0;}
  if(message==WM_CV_TIFF_MERGE){ /* уже после сброса: окно с вопросом не держит Проводник */
    CvSelection *s=(CvSelection*)lp;wchar_t name[MAX_PATH];const wchar_t *slash=wcsrchr(s->paths[0],L'\\');
    lstrcpynW(name,slash?slash+1:s->paths[0],MAX_PATH);wchar_t *dot=wcsrchr(name,L'.');if(dot)*dot=0;
    tm_open_pack(s->paths,s->count,name);free(s);return 0;
  }
#endif
  if (message == WM_CV_THUMB) { cv_thumb_ready((CvThumb *)lp); cv_redraw(); return 0; }
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
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
  g_cvDpi = dpi; g_cvSize = MulDiv(CV_RING_BASE, dpi, 96);
  WNDCLASSW klass = {0}; klass.lpfnWndProc = cv_proc; klass.hInstance = g_inst; klass.lpszClassName = L"CursorPadConvertRing"; klass.hCursor = LoadCursorW(NULL,IDC_ARROW);
  if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
  /* прозрачное окно: форма и тень — из альфы кадра, мимо круга мышь проходит насквозь */
  g_cvWnd = CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,klass.lpszClassName,L"Конвертация CursorPad",WS_POPUP,0,0,g_cvSize,g_cvSize,g_hwnd,NULL,g_inst,NULL);
  if (!g_cvWnd) return;
  hr = RegisterDragDrop(g_cvWnd,&g_cvDrop.iface); g_cvRegistered = SUCCEEDED(hr);
  ai_log(L"Конвертер: OLE 0x%08lx; Word %ls; Excel %ls", (unsigned long)hr, g_cvWord ? L"есть" : L"нет", g_cvExcel ? L"есть" : L"нет");
  if (g_cvRegistered) SetTimer(g_hwnd,TIMER_CONVERT,30,NULL);
}
static void cv_shutdown(void) {
  cv_pending_clear();
  cvx_shutdown();
  KillTimer(g_hwnd,TIMER_CONVERT);
  if (g_cvRegistered) { RevokeDragDrop(g_cvWnd); g_cvRegistered = FALSE; }
  if (g_cvWnd) { DestroyWindow(g_cvWnd); g_cvWnd = NULL; }
  cv_ring_free();
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
