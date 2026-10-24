/* Empty desktop Shift gesture. Native, offline creation; no model or Office. */
#include <oleacc.h>
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
/* выбор из кольца «Создать» (convert.c): 1 папка, 2 TXT, 3 Word, 4 Excel. Меню больше нет —
   оно висело, пока на него не наведёшь; кольцо уходит само, как только отпущена мышь. */
static void dc_create(int choice) {
  if(choice<1||choice>4)return;
  wchar_t desktop[MAX_PATH],path[MAX_PATH],message[600];
  if(FAILED(SHGetFolderPathW(NULL,CSIDL_DESKTOPDIRECTORY,NULL,SHGFP_TYPE_CURRENT,desktop))) {
    show_status(L"Не удалось найти папку рабочего стола.");return;
  }
  if(!dc_create_at(desktop,choice,path)) {
    swprintf(message,600,L"Не удалось создать файл или папку. Ошибка Windows %lu.",GetLastError());show_status(message);return;
  }
  SHChangeNotify(choice==1?SHCNE_MKDIR:SHCNE_CREATE,SHCNF_PATHW|SHCNF_FLUSHNOWAIT,path,NULL);
  swprintf(message,600,L"Создано: %ls",path);show_status(message);upd_balloon(6,L"CursorPad: создано на рабочем столе",path);
}
