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
  BitBlt(dc,0,0,w,h,screen,r.left,r.top,SRCCOPY);SelectObject(dc,old);
  BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
  DWORD size=(DWORD)w*h*4;BYTE *pixels=(BYTE*)malloc(size);GetDIBits(dc,bitmap,0,h,pixels,&info,DIB_RGB_COLORS);
  BITMAPFILEHEADER header={0};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+size;
  FILE *file=_wfopen(path,L"wb");if(file){fwrite(&header,sizeof(header),1,file);fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,file);fwrite(pixels,size,1,file);fclose(file);}
  free(pixels);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(NULL,screen);
}
static int cvx_lab(HINSTANCE instance) {
  int argc;LPWSTR *args=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc!=2)return 2;
  GetFullPathNameW(args[1],MAX_PATH,g_dataDir,NULL);LocalFree(args);g_inst=instance;
  INITCOMMONCONTROLSEX common={sizeof(common),ICC_STANDARD_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&common);
  WNDCLASSW k={0};k.hInstance=instance;k.lpfnWndProc=cvx_lab_proc;k.lpszClassName=L"CursorPadWorkbenchTest";RegisterClassW(&k);
  g_hwnd=CreateWindowW(k.lpszClassName,L"test",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,instance,NULL);
  cv_init();cvx_show();if(!cvx_lab_wait())return 3;
  g_cvxSelection.count=2;swprintf(g_cvxSelection.paths[0],MAX_PATH,L"%ls\\inputs\\one.png",g_dataDir);swprintf(g_cvxSelection.paths[1],MAX_PATH,L"%ls\\inputs\\Фото.png",g_dataDir);cvx_list();
  wchar_t out[MAX_PATH];swprintf(out,MAX_PATH,L"%ls\\out",g_dataDir);SetWindowTextW(g_cvxDir,out);
  SendMessageW(g_cvxAction,CB_SETCURSEL,1,0);cvx_run();if(!cvx_lab_wait()||!cvx_lab_exists(L"one.jpg")||!cvx_lab_exists(L"Фото.jpg"))return 4;
  SendMessageW(g_cvxAction,CB_SETCURSEL,8,0);SendMessageW(g_cvxMode,CB_SETCURSEL,1,0);SetWindowTextW(g_cvxSize,L"50");cvx_run();if(!cvx_lab_wait()||!cvx_lab_exists(L"one-размер.png"))return 5;
  SendMessageW(g_cvxAction,CB_SETCURSEL,7,0);cvx_run();if(!cvx_lab_wait()||!cvx_lab_exists(L"Пакет для отправки.zip"))return 6;
  SendMessageW(g_cvxHistory,LB_SETCURSEL,0,0);cvx_history(16);if(!cvx_lab_wait()||cvx_lab_exists(L"Пакет для отправки.zip"))return 7;
  swprintf(g_cvxSelection.paths[1],MAX_PATH,L"%ls\\inputs\\pages.pdf",g_dataDir);cvx_list();
  SendMessageW(g_cvxAction,CB_SETCURSEL,14,0);cvx_run();if(!cvx_lab_wait()||!cvx_lab_exists(L"Пакет чертежей.pdf"))return 8;
  wchar_t screenshot[MAX_PATH];swprintf(screenshot,MAX_PATH,L"%ls\\workbench.bmp",g_dataDir);cvx_controls();cvx_lab_capture(screenshot);
  cv_shutdown();DestroyWindow(g_hwnd);
  swprintf(out,MAX_PATH,L"%ls\\passed.txt",g_dataDir);aq_write(out,"PASS: native controls -> JSON request -> pinned Python helper -> batch JPEG/resize/ZIP/PDF packet -> history -> guarded undo; no API or Office");
  return 0;
}
