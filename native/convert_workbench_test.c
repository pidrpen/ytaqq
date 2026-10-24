/* Included only by -DCVX_LAB builds. Isolated profile and verified runtime are
   supplied by test_workbench.py; no real user files, AI requests or Office. */
static BOOL g_cvxLabFailed;
static LRESULT CALLBACK cvx_lab_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
  if(msg==WM_CVX_DONE) {
    CvxJob *job=(CvxJob*)lp;
    if(job->result.error[0]) {
      g_cvxLabFailed=TRUE;wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\error.txt",g_dataDir);
      char *text=ai_utf8(job->result.error);if(text){aq_write(path,text);free(text);}job->result.error[0]=0;
    }
    cvx_done(job);return 0;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
}
static BOOL cvx_lab_wait(void) {
  DWORD started=GetTickCount();
  while(g_cvBusy&&GetTickCount()-started<60000){MSG m;while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}
  return !g_cvBusy&&!g_cvxLabFailed;
}
static BOOL cvx_lab_exists(const wchar_t *name) {wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\out\\%ls",g_dataDir,name);return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;}
static void cvx_lab_capture(const wchar_t *path) {
  RedrawWindow(g_cvxWnd,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);Sleep(80);
  RECT r;GetWindowRect(g_cvxWnd,&r);int w=r.right-r.left,h=r.bottom-r.top;
  HDC screen=GetDC(NULL),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h),old=SelectObject(dc,bitmap);
  /* Render only this test window, even if another app covers the desktop. */
  if(!PrintWindow(g_cvxWnd,dc,2))SendMessageW(g_cvxWnd,WM_PRINT,(WPARAM)dc,PRF_NONCLIENT|PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
  SelectObject(dc,old);
  BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
  DWORD size=(DWORD)w*h*4;BYTE *pixels=(BYTE*)malloc(size);GetDIBits(dc,bitmap,0,h,pixels,&info,DIB_RGB_COLORS);
  BITMAPFILEHEADER header={0};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+size;
  FILE *file=_wfopen(path,L"wb");if(file){fwrite(&header,sizeof(header),1,file);fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,file);fwrite(pixels,size,1,file);fclose(file);}
  free(pixels);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(NULL,screen);
}
static int cvx_lab(HINSTANCE instance) {
  int argc;LPWSTR *args=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc!=2)return 2;
  GetFullPathNameW(args[1],MAX_PATH,g_dataDir,NULL);LocalFree(args);g_inst=instance;
  INITCOMMONCONTROLSEX common={sizeof(common),ICC_STANDARD_CLASSES|ICC_PROGRESS_CLASS|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
  WNDCLASSW k={0};k.hInstance=instance;k.lpfnWndProc=cvx_lab_proc;k.lpszClassName=L"CursorPadWorkbenchTest";RegisterClassW(&k);
  g_hwnd=CreateWindowW(k.lpszClassName,L"test",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,instance,NULL);
  cv_init();cvx_show();if(!cvx_lab_wait())return 3;
  if(g_cvxHistoryOpen||IsWindowVisible(g_cvxHistory)||IsWindowEnabled(GetDlgItem(g_cvxWnd,CX_RUN)))return 20;
  if(IsWindowVisible(g_cvxSize)||IsWindowVisible(g_cvxKb)||IsWindowVisible(g_cvxPages))return 21;
  g_cvxSelection.count=2;swprintf(g_cvxSelection.paths[0],MAX_PATH,L"%ls\\inputs\\one.png",g_dataDir);swprintf(g_cvxSelection.paths[1],MAX_PATH,L"%ls\\inputs\\Фото.png",g_dataDir);cvx_list();
  wchar_t out[MAX_PATH];swprintf(out,MAX_PATH,L"%ls\\out",g_dataDir);SetWindowTextW(g_cvxDir,out);
  SendMessageW(g_cvxAction,CB_SETCURSEL,1,0);cvx_controls();MSG enter={0};enter.hwnd=g_cvxAction;enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;if(!IsDialogMessageW(g_cvxWnd,&enter))return 29;if(!cvx_lab_wait()||!cvx_lab_exists(L"one.jpg")||!cvx_lab_exists(L"Фото.jpg"))return 4;
  SendMessageW(g_cvxAction,CB_SETCURSEL,8,0);cvx_controls();cvx_hint();
  if(!IsWindowVisible(g_cvxSize)||!IsWindowVisible(g_cvxFormat)||IsWindowVisible(g_cvxKb)||IsWindowVisible(g_cvxPages))return 22;
  SetWindowTextW(g_cvxSize,L"abc");cvx_run();if(g_cvBusy)return 23;
  wchar_t message[600];GetWindowTextW(g_cvxStatus,message,600);if(!wcsstr(message,L"ширину"))return 24;
  SendMessageW(g_cvxMode,CB_SETCURSEL,1,0);SetWindowTextW(g_cvxSize,L"50");cvx_run();if(!cvx_lab_wait()||!cvx_lab_exists(L"one-размер.png"))return 5;
  SendMessageW(g_cvxAction,CB_SETCURSEL,7,0);cvx_run();if(!cvx_lab_wait()||!cvx_lab_exists(L"one.zip"))return 6;
  cvx_toggle_history();if(!cvx_lab_wait()||!IsWindowVisible(g_cvxHistory))return 25;
  SendMessageW(g_cvxHistory,LB_SETCURSEL,0,0);cvx_history(16);if(!cvx_lab_wait()||cvx_lab_exists(L"one.zip"))return 7;
  swprintf(g_cvxSelection.paths[1],MAX_PATH,L"%ls\\inputs\\pages.pdf",g_dataDir);cvx_list();
  SendMessageW(g_cvxAction,CB_SETCURSEL,14,0);cvx_run();if(!cvx_lab_wait()||!cvx_lab_exists(L"Пакет чертежей.pdf"))return 8;
  cvx_toggle_history();if(g_cvxHistoryOpen||IsWindowVisible(g_cvxHistory))return 26;
  wchar_t screenshot[MAX_PATH];swprintf(screenshot,MAX_PATH,L"%ls\\workbench.bmp",g_dataDir);cvx_controls();cvx_hint();cvx_lab_capture(screenshot);
  g_cvxSelection.count=1;cvx_list();
  /* Capture the exact resize scenario from the user's screenshot. */
  SendMessageW(g_cvxAction,CB_SETCURSEL,8,0);cvx_controls();cvx_hint();
  swprintf(screenshot,MAX_PATH,L"%ls\\resize.bmp",g_dataDir);cvx_lab_capture(screenshot);
  for(int action=0;action<15;action++) {
    SendMessageW(g_cvxAction,CB_SETCURSEL,action,0);cvx_controls();
    if(IsWindowVisible(g_cvxSize)!=(action==8)||IsWindowVisible(g_cvxKb)!=(action==9)||IsWindowVisible(g_cvxPages)!=(action>=11&&action<=13))return 27;
  }
  SendMessageW(g_cvxAction,CB_SETCURSEL,8,0);cvx_controls();
  RECT window;GetWindowRect(g_cvxWnd,&window);RECT scaled={window.left,window.top,window.left+1175,window.top+700};
  SendMessageW(g_cvxWnd,WM_DPICHANGED,MAKEWPARAM(120,120),(LPARAM)&scaled);
  RECT client,button;GetClientRect(g_cvxWnd,&client);GetWindowRect(GetDlgItem(g_cvxWnd,CX_RUN),&button);MapWindowPoints(NULL,g_cvxWnd,(POINT*)&button,2);
  if(button.bottom>client.bottom||button.right>client.right||g_cvxDpi!=120)return 28;
  swprintf(screenshot,MAX_PATH,L"%ls\\scale125.bmp",g_dataDir);cvx_lab_capture(screenshot);
  RECT larger={window.left,window.top,window.left+1410,window.top+840};
  SendMessageW(g_cvxWnd,WM_DPICHANGED,MAKEWPARAM(144,144),(LPARAM)&larger);cvx_toggle_history();if(!cvx_lab_wait())return 33;
  GetClientRect(g_cvxWnd,&client);GetWindowRect(g_cvxHistory,&button);MapWindowPoints(NULL,g_cvxWnd,(POINT*)&button,2);if(button.bottom>client.bottom-cx_px(40))return 34;
  cvx_toggle_history();
  cx_font(96);SetWindowPos(g_cvxWnd,NULL,window.left,window.top,800,540,SWP_NOZORDER|SWP_NOACTIVATE);cvx_controls();
  GetClientRect(g_cvxWnd,&client);GetWindowRect(GetDlgItem(g_cvxWnd,CX_RUN),&button);MapWindowPoints(NULL,g_cvxWnd,(POINT*)&button,2);
  if(button.bottom>client.bottom||button.right>client.right)return 30;
  g_cvBusy=1;cvx_controls();if(IsWindowEnabled(GetDlgItem(g_cvxWnd,CX_RUN))||IsWindowEnabled(GetDlgItem(g_cvxWnd,CX_ADD))||!IsWindowEnabled(GetDlgItem(g_cvxWnd,CX_CANCEL)))return 31;g_cvBusy=0;cvx_controls();
  SendMessageW(g_cvxWnd,WM_COMMAND,IDCANCEL,0);if(IsWindowVisible(g_cvxWnd))return 32;
  /* The exact native creator writes into this isolated profile, never Desktop. */
  wchar_t desktop[MAX_PATH],created[MAX_PATH];swprintf(desktop,MAX_PATH,L"%ls\\desktop",g_dataDir);if(!CreateDirectoryW(desktop,NULL))return 40;
  for(int kind=1;kind<=4;kind++) {
    if(!dc_create_at(desktop,kind,created)||!dc_create_at(desktop,kind,created)||!wcsstr(created,L" (2)"))return 41;
  }
  if(dc_create_at(desktop,0,created)||created[0])return 42;
  if(!dc_empty_role(ROLE_SYSTEM_LIST,CHILDID_SELF,TRUE)||dc_empty_role(ROLE_SYSTEM_LISTITEM,1,TRUE)||dc_empty_role(ROLE_SYSTEM_LIST,1,TRUE))return 43;
  HWND probe=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,k.lpszClassName,L"CursorPad desktop test",WS_POPUP|WS_VISIBLE,50,50,300,300,NULL,NULL,instance,NULL);
  HWND list=CreateWindowW(WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_NOCOLUMNHEADER,0,0,300,300,probe,NULL,instance,NULL);
  LVCOLUMNW column={0};column.mask=LVCF_WIDTH;column.cx=280;SendMessageW(list,LVM_INSERTCOLUMNW,0,(LPARAM)&column);
  LVITEMW item={0};item.mask=LVIF_TEXT;item.pszText=L"Тестовый файл";SendMessageW(list,LVM_INSERTITEMW,0,(LPARAM)&item);
  UpdateWindow(probe);POINT blank={200,200};ClientToScreen(list,&blank);
  if(!dc_empty_accessible(blank,TRUE)||dc_empty_desktop(blank))return 44;
  RECT row={LVIR_BOUNDS,0,0,0};SendMessageW(list,LVM_GETITEMRECT,0,(LPARAM)&row);POINT icon={10,(row.top+row.bottom)/2};ClientToScreen(list,&icon);
  if(dc_empty_accessible(icon,TRUE))return 45;
  DestroyWindow(probe);
  POINT start={500,400},end={550,430};MSG gesture;
  /* пустое место стола: протянули с Shift — кольцо «Создать» у курсора, отпустили на плитке Word — создать Word */
  cv_gesture(start,TRUE,TRUE,FALSE,TRUE,TRUE);cv_gesture(end,TRUE,TRUE,FALSE,FALSE,FALSE);
  if(!IsWindowVisible(g_cvWnd)||!g_cvCreate||!g_cvDesktopDragged)return 46;
  CvGeo ring=cv_geo(g_cvSize,1.0f);POINT word={g_cvAt.x+(LONG)ring.c,g_cvAt.y+(LONG)(ring.c+(ring.rin+ring.R)/2)};
  cv_gesture(word,TRUE,TRUE,FALSE,FALSE,FALSE);cv_gesture(word,FALSE,TRUE,FALSE,FALSE,FALSE);
  if(!PeekMessageW(&gesture,g_cvWnd,WM_CV_CREATE,WM_CV_CREATE,PM_REMOVE)||gesture.wParam!=3||IsWindowVisible(g_cvWnd))return 47;
  cv_gesture(word,FALSE,TRUE,FALSE,FALSE,TRUE);
  if(PeekMessageW(&gesture,g_cvWnd,WM_CV_CREATE,WM_CV_CREATE,PM_REMOVE))return 48;
  /* без Shift, с Esc или в центре — ничего не создаётся */
  cv_gesture(start,TRUE,TRUE,FALSE,TRUE,TRUE);cv_gesture(end,TRUE,TRUE,FALSE,FALSE,FALSE);cv_gesture(word,TRUE,TRUE,FALSE,FALSE,FALSE);cv_gesture(word,FALSE,FALSE,FALSE,FALSE,TRUE);
  if(PeekMessageW(&gesture,g_cvWnd,WM_CV_CREATE,WM_CV_CREATE,PM_REMOVE))return 49;
  cv_gesture(start,TRUE,TRUE,FALSE,TRUE,TRUE);cv_gesture(end,TRUE,TRUE,FALSE,FALSE,FALSE);cv_gesture(word,TRUE,TRUE,FALSE,FALSE,FALSE);cv_gesture(word,FALSE,TRUE,TRUE,FALSE,TRUE);
  if(PeekMessageW(&gesture,g_cvWnd,WM_CV_CREATE,WM_CV_CREATE,PM_REMOVE))return 50;
  cv_gesture(start,TRUE,TRUE,FALSE,TRUE,TRUE);cv_gesture(end,TRUE,TRUE,FALSE,FALSE,FALSE);cv_gesture(end,FALSE,TRUE,FALSE,FALSE,FALSE);
  if(PeekMessageW(&gesture,g_cvWnd,WM_CV_CREATE,WM_CV_CREATE,PM_REMOVE)||IsWindowVisible(g_cvWnd))return 52;
  cv_gesture(start,TRUE,TRUE,FALSE,TRUE,FALSE);cv_gesture(end,TRUE,TRUE,FALSE,FALSE,FALSE);
  if(!IsWindowVisible(g_cvWnd)||g_cvDesktopGesture||g_cvCreate)return 51;
  cv_hide();cv_gesture(end,FALSE,FALSE,FALSE,FALSE,FALSE);
  cv_shutdown();DestroyWindow(g_hwnd);
  swprintf(out,MAX_PATH,L"%ls\\passed.txt",g_dataDir);aq_write(out,"PASS: native controls -> JSON request -> pinned Python helper -> batch JPEG/resize/ZIP/PDF packet -> history -> guarded undo; no API or Office");
  return 0;
}
