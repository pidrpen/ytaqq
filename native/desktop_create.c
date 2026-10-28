/* Empty desktop Shift gesture. Native, offline creation; no model or Office. */
#include <oleacc.h>
#include <exdisp.h>
#ifndef SVSI_POSITIONITEM
#define SVSI_POSITIONITEM 0x80
#endif
static BOOL dc_empty_role(LONG role,LONG child,BOOL list) {
  return child==CHILDID_SELF&&(list?role==ROLE_SYSTEM_LIST:role==ROLE_SYSTEM_CLIENT||role==ROLE_SYSTEM_PANE);
}
static BOOL dc_empty_accessible(POINT point,BOOL list) {
  /* MSAA distinguishes a desktop icon from list whitespace without remote
     process writes, clipboard changes, injected input or Explorer extensions. */
  typedef HRESULT (WINAPI *FromPoint)(POINT,IAccessible**,VARIANT*);
  HMODULE library=LoadLibraryW(L"oleacc.dll");if(!library)return FALSE;
  FromPoint fromPoint=(FromPoint)GetProcAddress(library,"AccessibleObjectFromPoint");
  IAccessible *object=NULL;VARIANT child,role;VariantInit(&child);VariantInit(&role);BOOL empty=FALSE;
  if(fromPoint&&SUCCEEDED(fromPoint(point,&object,&child))&&object&&child.vt==VT_I4&&
     SUCCEEDED(object->lpVtbl->get_accRole(object,child,&role))&&role.vt==VT_I4)
    empty=dc_empty_role(role.lVal,child.lVal,list);
  VariantClear(&role);VariantClear(&child);if(object)object->lpVtbl->Release(object);FreeLibrary(library);return empty;
}
static BOOL dc_empty_desktop(POINT point) {
  HWND hit=WindowFromPoint(point);if(!hit)return FALSE;
  HWND root=GetAncestor(hit,GA_ROOT);wchar_t name[80];
  if(!GetClassNameW(root,name,80)||(wcscmp(name,L"Progman")&&wcscmp(name,L"WorkerW")))return FALSE;
  GetClassNameW(hit,name,80);BOOL list=!wcscmp(name,L"SysListView32");
  if(!list&&hit!=root)return FALSE;
  return dc_empty_accessible(point,list);
}
static BOOL dc_create_at(const wchar_t *directory,int kind,wchar_t *out) {
  out[0]=0;
  if(kind<1||kind>4){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
  DWORD attributes=GetFileAttributesW(directory);
  if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)){SetLastError(ERROR_PATH_NOT_FOUND);return FALSE;}
  if(wcslen(directory)>MAX_PATH-64){SetLastError(ERROR_FILENAME_EXCED_RANGE);return FALSE;}
  const wchar_t *names[]={L"Новая папка",L"Новый текст",L"Новый документ",L"Новая таблица"};
  const wchar_t *extensions[]={L"",L".txt",L".docx",L".xlsx"};
  for(int number=1;number<=9999;number++) {
    if(number==1)swprintf(out,MAX_PATH,L"%ls\\%ls%ls",directory,names[kind-1],extensions[kind-1]);
    else swprintf(out,MAX_PATH,L"%ls\\%ls (%d)%ls",directory,names[kind-1],number,extensions[kind-1]);
    BOOL ok=kind==1?CreateDirectoryW(out,NULL):ag_write_new(out,L"",kind==2?AG_PLAIN:kind==3?AG_DOCX:AG_XLSX);
    if(ok)return TRUE;
    DWORD error=GetLastError();if(error!=ERROR_FILE_EXISTS&&error!=ERROR_ALREADY_EXISTS){out[0]=0;SetLastError(error);return FALSE;}
  }
  out[0]=0;SetLastError(ERROR_FILE_EXISTS);return FALSE;
}
/* Новый значок Проводник кладёт в первое свободное место сетки — то есть в левый верхний угол.
   Поэтому после создания значок ставится туда, где кольцо было вызвано (IShellWindows → IFolderView,
   как в convert_select.c). Если у рабочего стола включено «Упорядочить автоматически», Проводник
   сам оставит значок на его месте в порядке. */
static const GUID kDcCLSID_ShellWindows = {0x9ba05972, 0xf6a8, 0x11cf, {0xa4, 0x42, 0x00, 0xa0, 0xc9, 0x0a, 0x8f, 0x39}};
static const GUID kDcIID_ShellWindows = {0x85cb6900, 0x4d95, 0x11cf, {0x96, 0x0c, 0x00, 0x80, 0xc7, 0xf4, 0xee, 0x85}};
static const GUID kDcSID_TopLevelBrowser = {0x4c96be40, 0x915c, 0x11cf, {0x99, 0xd3, 0x00, 0xaa, 0x00, 0x4a, 0xe8, 0x37}};
static const GUID kDcIID_ServiceProvider = {0x6d5140c1, 0x7436, 0x11ce, {0x80, 0x34, 0x00, 0xaa, 0x00, 0x60, 0x09, 0xfa}};
static const GUID kDcIID_ShellBrowser = {0x000214e2, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kDcIID_FolderView = {0xcde725b0, 0xccc9, 0x4519, {0x91, 0x7e, 0x32, 0x5d, 0x72, 0xfa, 0xb4, 0xce}};
static BOOL dc_place_once(PCUITEMID_CHILD child,POINT at) {
  BOOL ok=FALSE;IShellWindows *windows=NULL;IDispatch *disp=NULL;IServiceProvider *services=NULL;
  IShellBrowser *browser=NULL;IShellView *view=NULL;IFolderView *folder=NULL;
  if(SUCCEEDED(CoCreateInstance(&kDcCLSID_ShellWindows,NULL,CLSCTX_ALL,&kDcIID_ShellWindows,(void**)&windows))&&windows) {
    VARIANT empty;VariantInit(&empty);long hwnd=0;
    if(SUCCEEDED(windows->lpVtbl->FindWindowSW(windows,&empty,&empty,SWC_DESKTOP,&hwnd,SWFO_NEEDDISPATCH,&disp))&&disp&&
       SUCCEEDED(disp->lpVtbl->QueryInterface(disp,&kDcIID_ServiceProvider,(void**)&services))&&services&&
       SUCCEEDED(services->lpVtbl->QueryService(services,&kDcSID_TopLevelBrowser,&kDcIID_ShellBrowser,(void**)&browser))&&browser&&
       SUCCEEDED(browser->lpVtbl->QueryActiveShellView(browser,&view))&&view&&
       SUCCEEDED(view->lpVtbl->QueryInterface(view,&kDcIID_FolderView,(void**)&folder))&&folder) {
      HWND defView=NULL;view->lpVtbl->GetWindow(view,&defView);
      HWND list=defView?FindWindowExW(defView,NULL,L"SysListView32",NULL):NULL;
      POINT cell=at;ScreenToClient(list?list:defView,&cell);
      if(list) { /* значок кладут за левый верхний угол клетки — сдвинуть так, чтобы клетка была под курсором */
        DWORD spacing=(DWORD)SendMessageW(list,LVM_GETITEMSPACING,FALSE,0);
        cell.x-=(int)LOWORD(spacing)/2;cell.y-=(int)HIWORD(spacing)/2;
      }
      if(cell.x<0)cell.x=0;
      if(cell.y<0)cell.y=0;
      ok=SUCCEEDED(folder->lpVtbl->SelectAndPositionItems(folder,1,&child,&cell,SVSI_SELECT|SVSI_DESELECTOTHERS|SVSI_ENSUREVISIBLE|SVSI_POSITIONITEM));
    }
  }
  if(folder)folder->lpVtbl->Release(folder);
  if(view)view->lpVtbl->Release(view);
  if(browser)browser->lpVtbl->Release(browser);
  if(services)services->lpVtbl->Release(services);
  if(disp)disp->lpVtbl->Release(disp);
  if(windows)windows->lpVtbl->Release(windows);
  return ok;
}
typedef struct { wchar_t path[MAX_PATH]; POINT at; } DcPlace;
static DWORD WINAPI dc_place_thread(void *parameter) {
  DcPlace job=*(DcPlace*)parameter;free(parameter);
  if(FAILED(CoInitializeEx(NULL,COINIT_APARTMENTTHREADED)))return 0;
  PIDLIST_ABSOLUTE full=NULL;
  if(SUCCEEDED(SHParseDisplayName(job.path,NULL,&full,0,NULL))&&full) {
    PCUITEMID_CHILD child=ILFindLastID(full);
    for(int attempt=0;attempt<12;attempt++) { /* значок появляется в окне не сразу после уведомления */
      Sleep(attempt?150:100);
      if(dc_place_once(child,job.at))break;
    }
    CoTaskMemFree(full);
  }
  CoUninitialize();return 0;
}
static void dc_place(const wchar_t *path,POINT at) {
  DcPlace *job=(DcPlace*)malloc(sizeof(*job));if(!job)return;
  lstrcpynW(job->path,path,MAX_PATH);job->at=at;
  HANDLE thread=CreateThread(NULL,0,dc_place_thread,job,0,NULL);
  if(thread)CloseHandle(thread);else free(job);
}
/* выбор из кольца «Создать» (convert.c): 1 папка, 2 TXT, 3 Word, 4 Excel. Меню больше нет —
   оно висело, пока на него не наведёшь; кольцо уходит само, как только отпущена мышь. */
static void dc_create(int choice,POINT at) {
  if(choice<1||choice>4)return;
  wchar_t desktop[MAX_PATH],path[MAX_PATH],message[600];
  if(FAILED(SHGetFolderPathW(NULL,CSIDL_DESKTOPDIRECTORY,NULL,SHGFP_TYPE_CURRENT,desktop))) {
    show_status(L"Не удалось найти папку рабочего стола.");return;
  }
  if(!dc_create_at(desktop,choice,path)) {
    swprintf(message,600,L"Не удалось создать файл или папку. Ошибка Windows %lu.",GetLastError());show_status(message);return;
  }
  SHChangeNotify(choice==1?SHCNE_MKDIR:SHCNE_CREATE,SHCNF_PATHW|SHCNF_FLUSHNOWAIT,path,NULL);
  dc_place(path,at);
  swprintf(message,600,L"Создано: %ls",path);show_status(message);upd_balloon(6,L"CursorPad: создано на рабочем столе",path);
}
