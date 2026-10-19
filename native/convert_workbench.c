/* Batch controls, ordered PDF packets and persistent result history. */
#include <commdlg.h>
#define WM_CVX_DONE (WM_APP+140)
enum { CX_FILES=7600,CX_ADD,CX_REMOVE,CX_CLEAR,CX_UP,CX_DOWN,CX_ACTION,CX_FORMAT,CX_MODE,CX_SIZE,CX_KB,CX_PAGES,CX_ROTATE,CX_DIR,CX_BROWSE,CX_RUN,CX_CANCEL,CX_HISTORY,CX_REFRESH,CX_UNDO,CX_REVEAL };
static const wchar_t *kCvxActions[]={L"Картинки → PNG",L"Картинки → JPG",L"Картинки → BMP",L"Картинки → TIFF",L"Каждый файл → PDF",L"Word → TXT",L"Excel → CSV (лист 1)",L"Собрать ZIP для отправки",L"Изменить размер картинок",L"Сжать картинки до лимита",L"Объединить PDF",L"Извлечь страницы PDF",L"Разделить PDF по страницам",L"Повернуть страницы PDF",L"Пакет чертежей в один PDF"};
static const char *kCvxOps[]={"convert","convert","convert","convert","to_pdf","convert","convert","zip","resize","compress","merge","extract","split","rotate","packet","history","undo","reveal"};
typedef struct {
  CvJob result; CvSelection files; int action,format,mode,size,kb,rotation;
  wchar_t pages[256],directory[MAX_PATH],entry[80],run[MAX_PATH]; char *report;
} CvxJob;
static HWND g_cvxWnd,g_cvxFiles,g_cvxAction,g_cvxFormat,g_cvxMode,g_cvxSize,g_cvxKb,g_cvxPages,g_cvxRotate,g_cvxDir,g_cvxHistory,g_cvxStatus,g_cvxProgress;
static CvSelection g_cvxSelection;
static wchar_t g_cvxIds[200][80],g_cvxCancelPath[MAX_PATH];
static int g_cvxHistoryCount;
static volatile LONG g_cvxCancelled;
typedef struct { char *text; size_t used,cap; BOOL ok; } CvxJson;
static void cx_raw(CvxJson *b,const char *text) {
  if(!b->ok)return;
  if(!text){b->ok=FALSE;return;}
  size_t n=strlen(text);
  if(b->used+n+1>b->cap) { size_t cap=(b->used+n+4096)*2; char *p=(char*)realloc(b->text,cap); if(!p){b->ok=FALSE;return;} b->text=p;b->cap=cap; }
  memcpy(b->text+b->used,text,n+1);b->used+=n;
}
static void cx_string(CvxJson *b,const wchar_t *value) { char *s=ai_json_str(value);if(s){cx_raw(b,s);free(s);}else b->ok=FALSE; }
static void cx_field(CvxJson *b,const char *name,const wchar_t *value) { cx_raw(b,",\"");cx_raw(b,name);cx_raw(b,"\":");cx_string(b,value); }
static void cx_text(char *report,const char *field,wchar_t *out,int cap) { const char *p=report?json_find_string(report,field):NULL;wchar_t *s=p?ai_json_unstr(p):NULL;lstrcpynW(out,s?s:L"",cap);free(s); }
static void cvx_controls(void) {
  if(!g_cvxWnd)return;
  for(int id=CX_ADD;id<=CX_REVEAL;id++) { HWND h=GetDlgItem(g_cvxWnd,id);if(h)EnableWindow(h,!g_cvBusy); }
  EnableWindow(GetDlgItem(g_cvxWnd,CX_CANCEL),g_cvBusy);
  SendMessageW(g_cvxProgress,PBM_SETMARQUEE,g_cvBusy,40);
  ShowWindow(g_cvxProgress,g_cvBusy?SW_SHOW:SW_HIDE);
  int action=(int)SendMessageW(g_cvxAction,CB_GETCURSEL,0,0);
  if(!g_cvBusy) {
    EnableWindow(g_cvxFormat,action==8);EnableWindow(g_cvxMode,action==8);EnableWindow(g_cvxSize,action==8);
    EnableWindow(g_cvxKb,action==9);EnableWindow(g_cvxPages,action>=11&&action<=13);EnableWindow(g_cvxRotate,action==13);
  }
}
static void cvx_hint(void) {
  int action=(int)SendMessageW(g_cvxAction,CB_GETCURSEL,0,0);
  const wchar_t *hint=action==9?L"Для заданного лимита JPG могут уменьшиться качество и размеры. Исходники сохраняются.":
    action==8?L"Пропорции сохраняются. Примеры: ширина 1920/1024 пикселя или масштаб 50%.":
    action==14?L"PDF, картинки и документы Office — в указанном порядке. Для DOCX/XLSX нужны Word/Excel.":
    action==10?L"PDF объединяются в порядке списка, с сохранением текста и векторной графики.":
    action==12?L"Каждая выбранная страница станет отдельным PDF. Пустое поле — все страницы.":
    action==13?L"Поворот применяется к выбранным страницам; остальные страницы остаются в документе.":
    action==7?L"ZIP сохраняет все выбранные файлы. Совпадающие имена внутри архива получают номер.":
    action==6?L"CSV сохраняет первый лист книги в UTF-8. Для экспорта нужен установленный Excel.":
    L"Результаты сохраняются новыми файлами. Исходники не изменяются; операции доступны в истории.";
  SetWindowTextW(g_cvxStatus,hint);
}
static void cvx_list(void) {
  SendMessageW(g_cvxFiles,LB_RESETCONTENT,0,0);
  for(int i=0;i<g_cvxSelection.count;i++)SendMessageW(g_cvxFiles,LB_ADDSTRING,0,(LPARAM)g_cvxSelection.paths[i]);
}
static void cvx_history_update(char *report) {
  const char *a=json_find_string(report,"history_ids"),*b=json_find_string(report,"history_labels");
  if(!a||!b)return;
  wchar_t *ids=ai_json_unstr(a),*labels=ai_json_unstr(b); if(!ids||!labels){free(ids);free(labels);return;}
  SendMessageW(g_cvxHistory,LB_RESETCONTENT,0,0);g_cvxHistoryCount=0;
  wchar_t *id=ids,*label=labels;
  while(*id&&*label&&g_cvxHistoryCount<200) {
    wchar_t *next=wcsstr(id,L"\n"),*lnext=wcsstr(label,L"\n");if(next)*next++=0;if(lnext)*lnext++=0;
    lstrcpynW(g_cvxIds[g_cvxHistoryCount++],id,80);SendMessageW(g_cvxHistory,LB_ADDSTRING,0,(LPARAM)label);
    if(!next||!lnext)break;
    id=next;label=lnext;
  }
  free(ids);free(labels);
}
static DWORD WINAPI cvx_worker(void *parameter) {
  CvxJob *job=(CvxJob*)parameter;CvJob *r=&job->result;AiJob control={0};
  DWORD deadline=GetTickCount()+300000;wchar_t root[MAX_PATH],python[MAX_PATH],script[MAX_PATH],input[MAX_PATH],output[MAX_PATH],history[MAX_PATH],cancel[MAX_PATH];
  char *request=NULL; CvxJson files={NULL,0,0,TRUE},prepared={NULL,0,0,TRUE},wire={NULL,0,0,TRUE};
  wchar_t scratch[CV_FILES][MAX_PATH];int scratchCount=0,filesCount=0,preparedCount=0;
  int state=aq_prepare(&control,deadline,root);
  if(state!=AI_OK){lstrcpynW(r->error,L"Не удалось подготовить локальные инструменты. Проверьте журнал ИИ.",400);goto done;}
  swprintf(python,MAX_PATH,L"%ls\\python.exe",root);swprintf(script,MAX_PATH,L"%ls\\convert_files.py",root);
  swprintf(input,MAX_PATH,L"%ls\\request.json",job->run);swprintf(output,MAX_PATH,L"%ls\\response.json",job->run);
  swprintf(cancel,MAX_PATH,L"%ls\\cancel",job->run);swprintf(history,MAX_PATH,L"%ls\\conversions\\history.json",g_dataDir);
  cx_raw(&files,"[");cx_raw(&prepared,"[");
  for(int i=0;i<job->files.count;i++) {
    if(g_cvxCancelled){lstrcpynW(r->error,L"Операция отменена. Исходники сохранены.",400);goto done;}
    const wchar_t *source=job->files.paths[i];BOOL office=!_wcsicmp(cv_extension(source),L"docx")||!_wcsicmp(cv_extension(source),L"xlsx");
    if(office && (job->action==4||job->action==5||job->action==6||job->action==14)) {
      CvJob converted={0};lstrcpynW(converted.src,source,MAX_PATH);converted.format=job->action==14?4:job->action;
      swprintf(scratch[scratchCount],MAX_PATH,L"%ls\\office-%d.%ls",job->run,i,kCvFormats[converted.format].ext);
      wchar_t *temp=scratch[scratchCount++];HANDLE f=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
      if(f==INVALID_HANDLE_VALUE){lstrcpynW(r->error,L"Не удалось создать временный файл для документа.",400);goto done;}CloseHandle(f);
      if(!cv_office(&converted,temp)){lstrcpynW(r->error,converted.error,400);goto done;}
      if(job->action==14){if(filesCount++)cx_raw(&files,",");cx_string(&files,temp);}
      else {
        if(preparedCount++)cx_raw(&prepared,",");
        cx_raw(&prepared,"{\"path\":");cx_string(&prepared,temp);cx_field(&prepared,"original",source);
        wchar_t name[MAX_PATH];const wchar_t *base=wcsrchr(source,L'\\');lstrcpynW(name,base?base+1:source,MAX_PATH);wchar_t *dot=wcsrchr(name,L'.');if(dot)*dot=0;
        size_t used=wcslen(name);swprintf(name+used,MAX_PATH-used,L".%ls",kCvFormats[converted.format].ext);cx_field(&prepared,"name",name);cx_raw(&prepared,"}");
      }
    } else {if(filesCount++)cx_raw(&files,",");cx_string(&files,source);}
  }
  cx_raw(&files,"]");cx_raw(&prepared,"]");cx_raw(&wire,"{\"op\":\"");cx_raw(&wire,kCvxOps[job->action]);cx_raw(&wire,"\"");
  cx_field(&wire,"history",history);cx_field(&wire,"cancel",cancel);cx_field(&wire,"entry_id",job->entry);cx_field(&wire,"pages",job->pages);
  cx_field(&wire,"output_dir",job->directory);if(job->files.count)cx_field(&wire,"anchor",job->files.paths[0]);
  cx_raw(&wire,",\"files\":");cx_raw(&wire,files.text);cx_raw(&wire,",\"prepared\":");cx_raw(&wire,prepared.text);
  char options[240];snprintf(options,sizeof(options),",\"format\":\"%ls\",\"resize_mode\":\"%s\",\"width\":%d,\"percent\":%d,\"max_bytes\":%lld,\"rotation\":%d}",
      kCvFormats[job->format].ext,job->mode==1?"percent":"width",job->size,job->size,(long long)job->kb*1024,job->rotation);
  cx_raw(&wire,options);
  if(!files.ok||!prepared.ok||!wire.ok||!aq_write(input,wire.text)){lstrcpynW(r->error,L"Не удалось записать задание конвертера.",400);goto done;}
  if(g_cvxCancelled)aq_write(cancel,"");
  wchar_t command[MAX_PATH*5];swprintf(command,MAX_PATH*5,L"\"%ls\" -I -B \"%ls\" --request \"%ls\" --response \"%ls\"",python,script,input,output);
  state=aq_process(python,command,root,&control,deadline);
  if(state!=AI_OK){lstrcpynW(r->error,state==AI_TIMEOUT?L"Превышено время обработки пакета. Исходники сохранены.":L"Не удалось выполнить локальные файловые инструменты.",400);goto done;}
  job->report=aq_read(output);
  if(!job->report || !json_find_string(job->report,"summary")){lstrcpynW(r->error,L"Конвертер не вернул отчёт.",400);goto done;}
  cx_text(job->report,"error",r->error,400);cx_text(job->report,"summary",r->summary,1600);cx_text(job->report,"first_output",r->out,MAX_PATH);
done:
  free(control.text);free(request);free(files.text);free(prepared.text);free(wire.text);
  for(int i=0;i<scratchCount;i++)DeleteFileW(scratch[i]);
  swprintf(input,MAX_PATH,L"%ls\\request.json",job->run);swprintf(output,MAX_PATH,L"%ls\\response.json",job->run);swprintf(cancel,MAX_PATH,L"%ls\\cancel",job->run);
  DeleteFileW(input);DeleteFileW(output);DeleteFileW(cancel);RemoveDirectoryW(job->run);
  if(!PostMessageW(g_hwnd,WM_CVX_DONE,0,(LPARAM)job)){free(job->report);free(job);InterlockedExchange(&g_cvBusy,0);}
  return 0;
}
static BOOL cvx_start(CvxJob *job) {
  if(InterlockedCompareExchange(&g_cvBusy,1,0)){free(job);return FALSE;}
  g_cvxCancelled=0;g_cvxCancelPath[0]=0;
  if(wcslen(g_dataDir)>MAX_PATH-80){free(job);g_cvBusy=0;return FALSE;}
  wchar_t dir[MAX_PATH];swprintf(dir,MAX_PATH,L"%ls\\conversions",g_dataDir);CreateDirectoryW(dir,NULL);
  BOOL made=FALSE;
  for(int i=0;i<100;i++){swprintf(job->run,MAX_PATH,L"%ls\\run-%lu-%lu-%d",dir,GetCurrentProcessId(),GetTickCount(),i);if(CreateDirectoryW(job->run,NULL)){made=TRUE;break;}}
  if(!made){free(job);g_cvBusy=0;return FALSE;}
  swprintf(g_cvxCancelPath,MAX_PATH,L"%ls\\cancel",job->run);
  lstrcpynW(job->result.label,job->action<15?kCvxActions[job->action]:L"История",80);
  HANDLE thread=CreateThread(NULL,0,cvx_worker,job,0,NULL);
  if(!thread){RemoveDirectoryW(job->run);free(job);g_cvBusy=0;return FALSE;}CloseHandle(thread);
  if(job->action!=15){if(g_cvxStatus)SetWindowTextW(g_cvxStatus,L"Обработка… Исходники сохраняются. Отмена — после текущего файла.");show_status(L"Обрабатываю файлы…");}
  cvx_controls();return TRUE;
}
static void cvx_history(int action) {
  CvxJob *job=(CvxJob*)calloc(1,sizeof(*job));if(!job)return;job->action=action;job->format=0;
  if(action!=15){int index=(int)SendMessageW(g_cvxHistory,LB_GETCURSEL,0,0);if(index<0||index>=g_cvxHistoryCount){free(job);return;}lstrcpynW(job->entry,g_cvxIds[index],80);}
  cvx_start(job);
}
static void cvx_done(CvxJob *job) {
  if(g_cvxWnd&&job->report)cvx_history_update(job->report);
  if(g_cvxStatus&&job->action!=15)SetWindowTextW(g_cvxStatus,job->result.error[0]?job->result.error:job->result.summary);
  int action=job->action;
  if(action==15)job->result.summary[0]=0;
  if(action==17&&job->result.out[0]){lstrcpynW(g_cvLast,job->result.out,MAX_PATH);cv_reveal();job->result.out[0]=0;}
  free(job->report);job->report=NULL;cv_done(&job->result);cvx_controls();
  if(g_cvxWnd && action!=15 && action!=17)cvx_history(15);
}
static HWND cx_control(const wchar_t *kind,const wchar_t *text,DWORD style,int id) {
  if(wcscmp(kind,L"STATIC")&&wcscmp(kind,PROGRESS_CLASSW))style|=WS_TABSTOP;
  HWND h=CreateWindowExW(!wcscmp(kind,L"EDIT")||!wcscmp(kind,L"LISTBOX")?WS_EX_CLIENTEDGE:0,kind,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,g_cvxWnd,(HMENU)(INT_PTR)id,g_inst,NULL);
  SendMessageW(h,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return h;
}
static void cx_label(const wchar_t *text,int id) { cx_control(L"STATIC",text,0,id); }
static void cx_place(int id,int x,int y,int width,int height) { MoveWindow(GetDlgItem(g_cvxWnd,id),x,y,width,height,TRUE); }
static void cvx_layout(void) {
  if(!g_cvxWnd||!g_cvxFiles)return;
  RECT r;GetClientRect(g_cvxWnd,&r);int w=r.right,h=r.bottom,left=(w-48)*58/100,right=left+32,rw=w-right-16,history=h-228;
  cx_place(7700,16,12,left,22);cx_place(CX_FILES,16,38,left,history-92);
  int buttons[]={CX_ADD,CX_REMOVE,CX_CLEAR,CX_UP,CX_DOWN};for(int i=0;i<5;i++)cx_place(buttons[i],16+i*(left/5),history-46,left/5-4,28);
  cx_place(7701,right,12,rw,22);cx_place(CX_ACTION,right,38,rw,360);
  cx_place(7702,right,73,rw,20);cx_place(CX_FORMAT,right,94,rw,140);
  cx_place(CX_MODE,right,128,rw/2-3,120);cx_place(CX_SIZE,right+rw/2+3,128,rw/2-3,140);
  cx_place(7703,right,163,rw/2,20);cx_place(CX_KB,right+rw/2,158,rw/2,140);
  cx_place(7704,right,197,rw,20);cx_place(CX_PAGES,right,219,rw*2/3-4,26);cx_place(CX_ROTATE,right+rw*2/3,219,rw/3,120);
  cx_place(7705,right,255,rw,20);cx_place(CX_DIR,right,277,rw-65,26);cx_place(CX_BROWSE,right+rw-60,277,60,26);
  cx_place(CX_RUN,right,315,rw/2-4,32);cx_place(CX_CANCEL,right+rw/2+4,315,rw/2-4,32);
  cx_place(7707,right,354,rw,16);
  cx_place(7706,16,history,w-32,22);cx_place(CX_HISTORY,16,history+24,w-190,132);
  cx_place(CX_REFRESH,w-164,history+24,148,28);cx_place(CX_REVEAL,w-164,history+60,148,28);cx_place(CX_UNDO,w-164,history+96,148,42);
  cx_place(7708,16,h-60,w-32,50);
}
static void cvx_add(const wchar_t *path) {
  if(g_cvxSelection.count>=CV_FILES)return;
  DWORD attr=GetFileAttributesW(path);if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_DIRECTORY)||wcslen(path)>=MAX_PATH)return;
  for(int i=0;i<g_cvxSelection.count;i++)if(!_wcsicmp(path,g_cvxSelection.paths[i]))return;
  lstrcpynW(g_cvxSelection.paths[g_cvxSelection.count++],path,MAX_PATH);
}
static void cvx_pick(void) {
  wchar_t *buffer=(wchar_t*)calloc(32768,sizeof(wchar_t));if(!buffer)return;
  OPENFILENAMEW of={sizeof(of)};of.hwndOwner=g_cvxWnd;of.lpstrFile=buffer;of.nMaxFile=32768;of.lpstrFilter=L"Все файлы\0*.*\0";of.Flags=OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST;
  if(GetOpenFileNameW(&of)) {
    wchar_t *p=buffer+wcslen(buffer)+1;
    if(!*p)cvx_add(buffer);
    else while(*p){wchar_t path[MAX_PATH];if(wcslen(buffer)+wcslen(p)+2<MAX_PATH){swprintf(path,MAX_PATH,L"%ls\\%ls",buffer,p);cvx_add(path);}p+=wcslen(p)+1;}
    cvx_list();
  }
  free(buffer);
}
static void cvx_run(void) {
  int action=(int)SendMessageW(g_cvxAction,CB_GETCURSEL,0,0);
  if(action<0||!cvx_all(&g_cvxSelection,action)){SetWindowTextW(g_cvxStatus,L"Выберите файлы, подходящие для действия. Для Word/Excel нужен установленный Office.");return;}
  CvxJob *job=(CvxJob*)calloc(1,sizeof(*job));if(!job)return;job->files=g_cvxSelection;job->action=action;
  job->format=action<4?action:action==9?1:(int)SendMessageW(g_cvxFormat,CB_GETCURSEL,0,0);if(job->format<0)job->format=0;
  job->mode=(int)SendMessageW(g_cvxMode,CB_GETCURSEL,0,0);job->size=(int)GetDlgItemInt(g_cvxWnd,CX_SIZE,NULL,FALSE);job->kb=(int)GetDlgItemInt(g_cvxWnd,CX_KB,NULL,FALSE);
  job->rotation=((int)SendMessageW(g_cvxRotate,CB_GETCURSEL,0,0)+1)*90;
  GetWindowTextW(g_cvxPages,job->pages,256);GetWindowTextW(g_cvxDir,job->directory,MAX_PATH);
  cvx_start(job);
}
static LRESULT CALLBACK cvx_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
  if(msg==WM_SIZE){cvx_layout();return 0;}
  if(msg==WM_CLOSE){ShowWindow(hwnd,SW_HIDE);return 0;}
  if(msg==WM_DROPFILES){HDROP drop=(HDROP)wp;UINT count=DragQueryFileW(drop,0xffffffff,NULL,0);for(UINT i=0;i<count;i++){wchar_t path[MAX_PATH];if(DragQueryFileW(drop,i,NULL,0)<MAX_PATH&&DragQueryFileW(drop,i,path,MAX_PATH))cvx_add(path);}DragFinish(drop);cvx_list();return 0;}
  if(msg==WM_COMMAND){int id=LOWORD(wp);
    if(id==CX_ADD)cvx_pick();
    else if(id==CX_CLEAR){g_cvxSelection.count=0;cvx_list();}
    else if(id==CX_REMOVE||id==CX_UP||id==CX_DOWN){int at=(int)SendMessageW(g_cvxFiles,LB_GETCURSEL,0,0);if(at>=0&&at<g_cvxSelection.count){int next=id==CX_UP?at-1:at+1;if(id==CX_REMOVE){memmove(g_cvxSelection.paths[at],g_cvxSelection.paths[at+1],(g_cvxSelection.count-at-1)*sizeof(g_cvxSelection.paths[0]));g_cvxSelection.count--;}else if(next>=0&&next<g_cvxSelection.count){wchar_t swap[MAX_PATH];memcpy(swap,g_cvxSelection.paths[at],sizeof(swap));memcpy(g_cvxSelection.paths[at],g_cvxSelection.paths[next],sizeof(swap));memcpy(g_cvxSelection.paths[next],swap,sizeof(swap));at=next;}cvx_list();SendMessageW(g_cvxFiles,LB_SETCURSEL,at,0);}}
    else if(id==CX_BROWSE){wchar_t path[MAX_PATH];g_pickFolderTitle=L"Папка результатов конвертации";if(pick_folder(hwnd,path,MAX_PATH))SetWindowTextW(g_cvxDir,path);}
    else if(id==CX_RUN)cvx_run();
    else if(id==CX_CANCEL){g_cvxCancelled=1;if(g_cvxCancelPath[0])aq_write(g_cvxCancelPath,"");SetWindowTextW(g_cvxStatus,L"Отмена запрошена. Дождитесь окончания текущего файла.");}
    else if(id==CX_REFRESH)cvx_history(15);else if(id==CX_UNDO)cvx_history(16);else if(id==CX_REVEAL||(id==CX_HISTORY&&HIWORD(wp)==LBN_DBLCLK))cvx_history(17);
    else if(id==CX_ACTION&&HIWORD(wp)==CBN_SELCHANGE){cvx_controls();cvx_hint();}
    else if(id==CX_MODE&&HIWORD(wp)==CBN_SELCHANGE)SetWindowTextW(g_cvxSize,SendMessageW(g_cvxMode,CB_GETCURSEL,0,0)==1?L"50":L"1920");
    return 0;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
}
static void cvx_show(void) {
  if(!g_cvxWnd){
    WNDCLASSW k={0};k.hInstance=g_inst;k.lpfnWndProc=cvx_proc;k.lpszClassName=L"CursorPadFileWorkbench";k.hCursor=LoadCursorW(NULL,IDC_ARROW);k.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&k);
    MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(g_hwnd,MONITOR_DEFAULTTONEAREST),&mi);int width=min(1020,mi.rcWork.right-mi.rcWork.left-32),height=min(780,mi.rcWork.bottom-mi.rcWork.top-32);
    g_cvxWnd=CreateWindowExW(WS_EX_APPWINDOW,k.lpszClassName,L"Файлы: конвертер, PDF и история — CursorPad",WS_OVERLAPPEDWINDOW,mi.rcWork.left+16,mi.rcWork.top+16,width,height,g_hwnd,NULL,g_inst,NULL);
    if(!g_cvxWnd)return;
    g_cvxFiles=cx_control(L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_HSCROLL,CX_FILES);
    SendMessageW(g_cvxFiles,LB_SETHORIZONTALEXTENT,1800,0);
    const wchar_t *buttons[]={L"Добавить…",L"Убрать",L"Очистить",L"Выше",L"Ниже"};for(int i=0;i<5;i++)cx_control(L"BUTTON",buttons[i],BS_PUSHBUTTON,CX_ADD+i);
    g_cvxAction=cx_control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,CX_ACTION);for(int i=0;i<15;i++)SendMessageW(g_cvxAction,CB_ADDSTRING,0,(LPARAM)kCvxActions[i]);SendMessageW(g_cvxAction,CB_SETCURSEL,7,0);
    g_cvxFormat=cx_control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,CX_FORMAT);for(int i=0;i<4;i++)SendMessageW(g_cvxFormat,CB_ADDSTRING,0,(LPARAM)kCvFormats[i].label);SendMessageW(g_cvxFormat,CB_SETCURSEL,0,0);
    g_cvxMode=cx_control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,CX_MODE);SendMessageW(g_cvxMode,CB_ADDSTRING,0,(LPARAM)L"Ширина, пиксели");SendMessageW(g_cvxMode,CB_ADDSTRING,0,(LPARAM)L"Масштаб, %");SendMessageW(g_cvxMode,CB_SETCURSEL,0,0);
    g_cvxSize=cx_control(L"COMBOBOX",L"",CBS_DROPDOWN|CBS_AUTOHSCROLL,CX_SIZE);const wchar_t *sizes[]={L"1920",L"1024",L"50"};for(int i=0;i<3;i++)SendMessageW(g_cvxSize,CB_ADDSTRING,0,(LPARAM)sizes[i]);SetWindowTextW(g_cvxSize,L"1920");
    g_cvxKb=cx_control(L"COMBOBOX",L"",CBS_DROPDOWN|CBS_AUTOHSCROLL,CX_KB);const wchar_t *limits[]={L"100",L"500",L"1024",L"2048"};for(int i=0;i<4;i++)SendMessageW(g_cvxKb,CB_ADDSTRING,0,(LPARAM)limits[i]);SetWindowTextW(g_cvxKb,L"1024");
    g_cvxPages=cx_control(L"EDIT",L"",ES_AUTOHSCROLL,CX_PAGES);SendMessageW(g_cvxPages,EM_SETLIMITTEXT,254,0);
    g_cvxRotate=cx_control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,CX_ROTATE);const wchar_t *angles[]={L"90°",L"180°",L"270°"};for(int i=0;i<3;i++)SendMessageW(g_cvxRotate,CB_ADDSTRING,0,(LPARAM)angles[i]);SendMessageW(g_cvxRotate,CB_SETCURSEL,0,0);
    g_cvxDir=cx_control(L"EDIT",L"",ES_AUTOHSCROLL,CX_DIR);SendMessageW(g_cvxDir,EM_SETLIMITTEXT,MAX_PATH-1,0);cx_control(L"BUTTON",L"Папка…",BS_PUSHBUTTON,CX_BROWSE);
    cx_control(L"BUTTON",L"Выполнить",BS_DEFPUSHBUTTON,CX_RUN);cx_control(L"BUTTON",L"Отмена",BS_PUSHBUTTON,CX_CANCEL);
    g_cvxHistory=cx_control(L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_HSCROLL,CX_HISTORY);cx_control(L"BUTTON",L"Обновить",BS_PUSHBUTTON,CX_REFRESH);cx_control(L"BUTTON",L"Показать в папке",BS_PUSHBUTTON,CX_REVEAL);cx_control(L"BUTTON",L"Отменить результат",BS_PUSHBUTTON,CX_UNDO);
    SendMessageW(g_cvxHistory,LB_SETHORIZONTALEXTENT,2000,0);
    cx_label(L"Файлы и порядок страниц в PDF-пакете",7700);cx_label(L"Действие",7701);cx_label(L"Формат после изменения размера",7702);cx_label(L"Лимит JPG, КБ",7703);cx_label(L"Страницы: 1-3,5 (пусто — все)",7704);cx_label(L"Папка результата (пусто — рядом)",7705);cx_label(L"История · отмена удаляет только неизменённые созданные копии",7706);
    g_cvxProgress=cx_control(PROGRESS_CLASSW,L"",PBS_MARQUEE,7707);g_cvxStatus=cx_control(L"STATIC",L"Добавьте файлы. Для PDF-пакета задайте порядок кнопками Выше/Ниже.",0,7708);
    DragAcceptFiles(g_cvxWnd,TRUE);cvx_layout();cvx_list();
  }
  ShowWindow(g_cvxWnd,SW_SHOWNORMAL);SetForegroundWindow(g_cvxWnd);cvx_controls();if(!g_cvBusy)cvx_history(15);
}
static BOOL cvx_dispatch(const CvSelection *selection,int action) {
  if(action>=8){cvx_show();g_cvxSelection=*selection;cvx_list();int chosen=action==15?(cvx_all(selection,10)?10:cvx_all(selection,8)?8:cvx_all(selection,14)?14:7):action;SendMessageW(g_cvxAction,CB_SETCURSEL,chosen,0);cvx_controls();cvx_hint();return TRUE;}
  CvxJob *job=(CvxJob*)calloc(1,sizeof(*job));if(!job)return FALSE;job->files=*selection;job->action=action;job->format=action<4?action:0;job->kb=1024;job->size=1920;job->rotation=90;return cvx_start(job);
}
static void cvx_shutdown(void) {if(g_cvxWnd){DestroyWindow(g_cvxWnd);g_cvxWnd=NULL;}}
