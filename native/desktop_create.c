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
static HBITMAP cv_icon_bitmap(const wchar_t *path, BOOL folder, int px, BYTE **pixels); /* convert_ring.c */
static int cv_monitor_dpi(POINT point);
static void dc_menu(POINT point) {
  HMENU menu=CreatePopupMenu();if(!menu)return;
  AppendMenuW(menu,MF_STRING|MF_DISABLED,0,L"Создать на рабочем столе");AppendMenuW(menu,MF_SEPARATOR,0,NULL);
  AppendMenuW(menu,MF_STRING,1,L"Папка");AppendMenuW(menu,MF_STRING,2,L"Текстовый файл (.txt)");
  AppendMenuW(menu,MF_STRING,3,L"Документ Word (.docx)");AppendMenuW(menu,MF_STRING,4,L"Книга Excel (.xlsx)");
  /* у каждого пункта — значок, как у такого файла в Проводнике (Word, Excel, Блокнот, папка) */
  static const wchar_t *const kinds[]={L"Новая папка",L"a.txt",L"a.docx",L"a.xlsx"};HBITMAP icons[4]={0};
  int px=MulDiv(16,cv_monitor_dpi(point),96);
  for(int i=0;i<4;i++){
    icons[i]=cv_icon_bitmap(kinds[i],i==0,px,NULL);if(!icons[i])continue;
    MENUITEMINFOW item={sizeof(item)};item.fMask=MIIM_BITMAP;item.hbmpItem=icons[i];SetMenuItemInfoW(menu,i+1,FALSE,&item);
  }
  SetForegroundWindow(g_hwnd);
  int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,g_hwnd,NULL);
  DestroyMenu(menu);for(int i=0;i<4;i++)if(icons[i])DeleteObject(icons[i]);
  PostMessageW(g_hwnd,WM_NULL,0,0);if(!choice)return;
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
