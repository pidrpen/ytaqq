/* OLE-to-conversion integration probe and a real rendered preview. */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "ai_office.c"
static HWND g_hwnd;
static HINSTANCE g_inst;
static void ai_log(const wchar_t *text, ...) { (void)text; }
static void show_status(const wchar_t *text) { (void)text; }
static void upd_balloon(int kind, const wchar_t *title, const wchar_t *text) { (void)kind; (void)title; (void)text; }
#define CV_RING_TEST
#include "convert.c"
typedef struct { IDataObject iface; const wchar_t *path; BOOL multiple; int pending; } FakeData;
static HRESULT STDMETHODCALLTYPE data_query(IDataObject *self, REFIID iid, void **out) { (void)iid; *out = self; return S_OK; }
static ULONG STDMETHODCALLTYPE data_ref(IDataObject *self) { (void)self; return 1; }
static HRESULT STDMETHODCALLTYPE data_get(IDataObject *self, FORMATETC *format, STGMEDIUM *medium) {
  FakeData *data = (FakeData *)self;
  if(data->pending>0){data->pending--;return E_PENDING;}
  if (format->cfFormat != CF_HDROP || !(format->tymed & TYMED_HGLOBAL)) return DV_E_FORMATETC;
  size_t length = wcslen(data->path) + 1, count = data->multiple ? 2 : 1;
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + (length * count + 1) * sizeof(wchar_t));
  DROPFILES *drop = (DROPFILES *)GlobalLock(memory); if (!drop) return E_OUTOFMEMORY;
  drop->pFiles = sizeof(*drop); drop->fWide = TRUE;
  wchar_t *paths = (wchar_t *)((BYTE *)drop + sizeof(*drop));
  for (size_t i = 0; i < count; i++) memcpy(paths + i * length, data->path, length * sizeof(wchar_t));
  GlobalUnlock(memory); memset(medium, 0, sizeof(*medium)); medium->tymed = TYMED_HGLOBAL; medium->hGlobal = memory;
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE data_here(IDataObject *s, FORMATETC *f, STGMEDIUM *m) { (void)s;(void)f;(void)m; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE data_check(IDataObject *s, FORMATETC *f) { (void)s; return f->cfFormat == CF_HDROP ? S_OK : DV_E_FORMATETC; }
static HRESULT STDMETHODCALLTYPE data_canonical(IDataObject *s, FORMATETC *f, FORMATETC *o) { (void)s;(void)f;(void)o; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE data_set(IDataObject *s, FORMATETC *f, STGMEDIUM *m, BOOL r) { (void)s;(void)f;(void)m;(void)r; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE data_enum(IDataObject *s, DWORD d, IEnumFORMATETC **e) { (void)s;(void)d;(void)e; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE data_advise(IDataObject *s, FORMATETC *f, DWORD d, IAdviseSink *a, DWORD *c) { (void)s;(void)f;(void)d;(void)a;(void)c; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE data_unadvise(IDataObject *s, DWORD d) { (void)s;(void)d; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE data_enumadvise(IDataObject *s, IEnumSTATDATA **e) { (void)s;(void)e; return E_NOTIMPL; }
static IDataObjectVtbl data_vtable = {data_query,data_ref,data_ref,data_get,data_here,data_check,data_canonical,data_set,data_enum,data_advise,data_unadvise,data_enumadvise};
static LRESULT CALLBACK test_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_CONVERT_DONE) {
    CvJob *job = (CvJob *)lp;
    if (!job->out[0]) { fwprintf(stderr,L"%ls\n",job->error); free(job); InterlockedExchange(&g_cvBusy,0); }
    else cv_done(job);
    return 0;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
}
/* кадр w×h (premultiplied) поверх фона окна проводника — в BMP */
static void capture_pix(const wchar_t *path, const BYTE *pix, int w, int h) {
  if (!pix || w <= 0 || h <= 0) return;
  BITMAPINFOHEADER info = {0}; info.biSize = sizeof(info); info.biWidth = w; info.biHeight = -h; info.biPlanes = 1; info.biBitCount = 32;
  DWORD bytes = (DWORD)w*h*4; BYTE *pixels = (BYTE *)malloc(bytes); if (!pixels) return;
  COLORREF back = g_uiTok.surface == RGB(255,255,255) ? RGB(243,243,243) : RGB(25,25,25);
  for (DWORD i = 0; i < bytes; i += 4) {
    int a = pix[i+3];
    pixels[i] = (BYTE)(pix[i] + GetBValue(back)*(255-a)/255); pixels[i+1] = (BYTE)(pix[i+1] + GetGValue(back)*(255-a)/255);
    pixels[i+2] = (BYTE)(pix[i+2] + GetRValue(back)*(255-a)/255); pixels[i+3] = 255;
  }
  BITMAPFILEHEADER header = {0}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(info); header.bfSize=header.bfOffBits+bytes;
  FILE *file = _wfopen(path,L"wb"); if (file) { fwrite(&header,sizeof(header),1,file); fwrite(&info,sizeof(info),1,file); fwrite(pixels,bytes,1,file); fclose(file); }
  free(pixels);
}
static void capture(const wchar_t *path) { capture_pix(path, g_cvPix, g_cvBmpSize, g_cvBmpSize); }
/* --preview папка [dpi]: состояния кольца в светлой и тёмной теме, без окон и OLE */
static void preview_shot(const wchar_t *dir, const wchar_t *name, int theme) {
  wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls\\%ls-%ls.bmp", dir, name, theme ? L"dark" : L"light");
  g_cvShownAt = 0; cv_paint(); capture(path);
}
static void preview_select(const wchar_t *const *paths, int count) {
  g_cvSelection.count = count; for (int i = 0; i < count; i++) lstrcpynW(g_cvSelection.paths[i], paths[i], MAX_PATH);
  lstrcpynW(g_cvPath, paths[0], MAX_PATH); cv_menu_formats(); memset(g_cvLevel, 0, sizeof(g_cvLevel)); g_cvHover = -1; g_cvInHub = FALSE;
}
static int preview(const wchar_t *dir, int dpi) {
  static const UiTokens themes[2] = {{RGB(255,255,255),RGB(243,244,246),RGB(255,255,255),RGB(17,24,39),RGB(107,114,128),RGB(226,229,233),RGB(37,99,235)},
                                     {RGB(32,33,36),RGB(40,42,46),RGB(32,33,36),RGB(232,234,237),RGB(154,160,166),RGB(60,64,67),RGB(138,180,248)}};
  static const wchar_t *image[] = {L"C:\\Чертежи\\Вал РНЕ.20.031.png"}, *pdfs[] = {L"C:\\a\\Лист 1.pdf", L"C:\\a\\Лист 2.pdf", L"C:\\a\\Лист 3.pdf"},
                       *word[] = {L"C:\\a\\Пояснительная записка.docx"}, *other[] = {L"C:\\a\\Отчёт за квартал.mp4"},
                       *images[] = {L"C:\\a\\1.jpg", L"C:\\a\\2.png", L"C:\\a\\3.tif", L"C:\\a\\4.bmp", L"C:\\a\\5.jpg"};
  g_cvWord = g_cvExcel = TRUE; g_cvSize = MulDiv(CV_RING_BASE, dpi, 96); g_cvWin = MulDiv(CV_WIN_BASE, dpi, 96);
  for (int t = 0; t < 2; t++) {
    g_uiTok = themes[t]; g_ringStyle = 0; cv_backdrop_capture(); /* без экрана — условный проводник под стеклом */
    g_cvCount = 0; g_cvSelection.count = 0; g_cvPath[0] = 0; g_cvPendingData = NULL; g_cvHover = -1; g_cvInHub = FALSE;
    preview_shot(dir, L"1-wait", t);
    preview_select(image, 1); preview_shot(dir, L"2-image", t);
    g_cvHover = 0; g_cvLevel[0] = 1; preview_shot(dir, L"3-image-hover", t);
    preview_select(images, 5); g_cvHover = 4; g_cvLevel[4] = 1; preview_shot(dir, L"4-images-resize", t);
    preview_select(pdfs, 3); g_cvHover = 2; g_cvLevel[2] = 1; preview_shot(dir, L"5-pdf-hover", t);
    { static const wchar_t *tiffs[] = {L"C:\\a\\Лист 1.tif", L"C:\\a\\Лист 2.tif", L"C:\\a\\Лист 3.tif"};
      preview_select(tiffs, 3); for (int i = 0; i < g_cvCount; i++) if (g_cvFormats[i] == 19) g_cvHover = i, g_cvLevel[i] = 1;
      preview_shot(dir, L"5b-tiffs", t); }
    preview_select(word, 1); g_cvInHub = TRUE; preview_shot(dir, L"6-word-center", t);
    preview_select(other, 1); preview_shot(dir, L"7-other", t);
    for (int i = 0; i < g_cvCount; i++) if (g_cvFormats[i] == 24) g_cvHover = i, g_cvLevel[i] = 1; /* «Ссылка» */
    preview_shot(dir, L"7b-other-link", t);
    /* задержались на «Ссылке» — вокруг кольца её дуга; мышь на верхнем сегменте, потом на нижнем */
    g_cvHover = -1; memset(g_cvLevel, 0, sizeof(g_cvLevel)); g_cvLevel[g_cvSubTile] = 1;
    g_cvSubOn = TRUE; g_cvSubShownAt = 0; g_cvSubHover = 0; g_cvSubLevel[0] = 1; g_cvSubLevel[1] = 0; preview_shot(dir, L"7c-link-file", t);
    g_cvSubHover = 1; g_cvSubLevel[0] = 0; g_cvSubLevel[1] = 1; preview_shot(dir, L"7d-link-folder", t);
    g_cvSubOn = FALSE; g_cvSubHover = -1; memset(g_cvSubLevel, 0, sizeof(g_cvSubLevel));
    g_cvCreate = TRUE; g_cvCount = 5; for (int i = 0; i < 4; i++) g_cvFormats[i] = 20 + i;
    g_cvFormats[4] = 27;
    g_cvHover = -1; memset(g_cvLevel, 0, sizeof(g_cvLevel)); preview_shot(dir, L"11-desktop", t);
    g_cvHover = 2; g_cvLevel[2] = 1; preview_shot(dir, L"12-desktop-word", t);
    g_cvLevel[2] = 0; g_cvHover = 4; g_cvLevel[4] = 1; preview_shot(dir, L"12b-desktop-karta", t); g_cvCreate = FALSE;
    /* ожидание файлов: огонёк, затем объяснение; одни папки */
    g_cvCount = 0; g_cvSelection.count = 0; g_cvPath[0] = 0; g_cvHover = -1; g_cvInHub = FALSE; g_cvRead = CV_READ_FAILED;
    g_cvPendingData = (IDataObject *)1; g_cvEnteredAt = GetTickCount(); preview_shot(dir, L"8-reading", t);
    g_cvEnteredAt = GetTickCount() - 2000; preview_shot(dir, L"9-unreadable", t);
    g_cvPendingData = NULL; g_cvRead = CV_READ_FOLDERS; preview_shot(dir, L"10-folders", t); g_cvRead = CV_READ_WAIT;
  }
  /* остальные стили панели (вкладка «Кастомизация»): покой, наведение, дуга «Ссылки» — светлая тема */
  g_uiTok = themes[0];
  for (int st = 1; st < CV_STYLES; st++) {
    static const wchar_t *const kStyle[CV_STYLES] = {L"", L"-petals", L"-dark-glass", L"-thin"};
    wchar_t name[64];
    g_ringStyle = st; cv_backdrop_capture();
    preview_select(image, 1); swprintf(name, 64, L"2-image%ls", kStyle[st]); preview_shot(dir, name, 0);
    g_cvHover = 2; g_cvLevel[2] = 1; swprintf(name, 64, L"3-image-hover%ls", kStyle[st]); preview_shot(dir, name, 0);
    g_cvHover = -1; memset(g_cvLevel, 0, sizeof(g_cvLevel)); g_cvLevel[g_cvSubTile] = 1;
    g_cvSubOn = TRUE; g_cvSubShownAt = 0; g_cvSubHover = 0; g_cvSubLevel[0] = 1; swprintf(name, 64, L"7c-link-file%ls", kStyle[st]); preview_shot(dir, name, 0);
    g_cvSubOn = FALSE; g_cvSubHover = -1; memset(g_cvSubLevel, 0, sizeof(g_cvSubLevel)); memset(g_cvLevel, 0, sizeof(g_cvLevel));
  }
  g_ringStyle = 0;
  return 0;
}
int wmain(int argc, wchar_t **argv) {
  if (argc >= 3 && !wcscmp(argv[1], L"--preview")) return preview(argv[2], argc > 3 ? _wtoi(argv[3]) : 96);
  if (argc != 3) return 2;
  g_inst = GetModuleHandleW(NULL); WNDCLASSW klass={0}; klass.hInstance=g_inst; klass.lpfnWndProc=test_proc; klass.lpszClassName=L"ConvertTestOwner";
  RegisterClassW(&klass); g_hwnd=CreateWindowW(klass.lpszClassName,L"test",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,g_inst,NULL);
  cv_init(); if (!g_cvRegistered) return 3;
  wchar_t path[MAX_PATH]; GetFullPathNameW(argv[1],MAX_PATH,path,NULL);
  FakeData data={{&data_vtable},path,FALSE,1}; POINT show={450,300}; cv_show(show,FALSE);
  if(GetUpdateRect(g_cvWnd,NULL,FALSE))return 10;
  CvGeo geo = cv_geo(g_cvSize, 1.0f); POINTL center={g_cvAt.x+(LONG)geo.c,g_cvAt.y+(LONG)geo.c};
  DWORD effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;
  cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,center,&effect);
  if(g_cvCount||!g_cvPendingData)return 11;
  Sleep(110);effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;cv_over(&g_cvDrop.iface,MK_SHIFT,center,&effect);
  if (effect != DROPEFFECT_NONE || g_cvCount < 3) return 4;
  if(g_cvPendingData||GetUpdateRect(g_cvWnd,NULL,FALSE))return 12;
  /* середина плитки 0 — прямо над центром */
  POINTL sector={center.x,center.y-(LONG)((geo.rin+geo.R)/2)};
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_over(&g_cvDrop.iface,0,sector,&effect); if (effect != DROPEFFECT_NONE) return 5;
  data.multiple=TRUE; effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;
  cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,sector,&effect); if (g_cvSelection.count != 2 || !g_cvCount) return 6;
  /* одна папка — понятный отказ без вечного ожидания; на плитку не бросить */
  wchar_t folder[MAX_PATH]; lstrcpynW(folder,path,MAX_PATH); wchar_t *slash=wcsrchr(folder,L'\\'); if(slash)*slash=0;
  FakeData dir={{&data_vtable},folder,FALSE,0}; effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;
  cv_enter(&g_cvDrop.iface,&dir.iface,MK_SHIFT,sector,&effect);
  if (g_cvRead != CV_READ_FOLDERS || g_cvCount || g_cvPendingData || effect != DROPEFFECT_NONE) return 14;
  /* Shift+щелчок урезал выделение Проводника: при том же нажатии Shift кольцо берёт запомненное целиком */
  g_cvSnap.count=2; lstrcpynW(g_cvSnap.paths[0],path,MAX_PATH); lstrcpynW(g_cvSnap.paths[1],path,MAX_PATH);
  g_cvSnapPress=g_cvDownPress=5; g_cvSnapWnd=g_cvDownRoot=(HWND)1; g_cvSnapDesktop=FALSE; effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;
  data.multiple=FALSE; /* принесён один файл из двух выделенных */
  cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,sector,&effect);
  if (!g_cvWide || g_cvSelection.count != 2 || g_cvDragged.count != 1) return 15;
  g_cvDownPress=6; effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; /* другое нажатие Shift — как пришло */
  cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,sector,&effect);
  if (g_cvWide || g_cvSelection.count != 1) return 16;
  g_cvDownPress=0;
  data.multiple=FALSE; effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;
  cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,sector,&effect); if (effect != DROPEFFECT_COPY) return 7;
  if(GetUpdateRect(g_cvWnd,NULL,FALSE))return 13;
  /* миниатюра первого файла приходит своим потоком (WM_CV_THUMB) */
  for (DWORD started=GetTickCount(); !g_cvThumb && GetTickCount()-started<5000;) {
    MSG msg; while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(10);
  }
  if (!g_cvThumb || _wcsicmp(g_cvThumb->path,path)) return 17;
  int hover=g_cvHover; g_cvHover=-1; memset(g_cvLevel,0,sizeof(g_cvLevel)); cv_paint(); /* снимок — с карточкой файла в центре */
  capture(argv[2]); g_cvHover=hover;
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_drop(&g_cvDrop.iface,&data.iface,MK_SHIFT,sector,&effect);
  if (effect != DROPEFFECT_COPY) return 8;
  DWORD started=GetTickCount();
  while (g_cvBusy && GetTickCount()-started < 10000) {
    MSG msg; while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(10);
  }
  BOOL ok=!g_cvBusy && g_cvLast[0] && GetFileAttributesW(g_cvLast)!=INVALID_FILE_ATTRIBUTES;
  /* рабочий стол: Shift и протянуть по пустому месту — кольцо «Создать»; отпустить на плитке Word — создать Word */
  cv_hide(); g_cvDown=FALSE; POINT p0={600,400}, p1={640,400};
  cv_gesture(p0,TRUE,TRUE,FALSE,FALSE,TRUE); cv_gesture(p1,TRUE,TRUE,FALSE,FALSE,FALSE);
  if (!IsWindowVisible(g_cvWnd) || !g_cvCreate || g_cvCount!=5) return 18;
  /* середина плитки i: плитка 0 сверху, дальше по часовой */
  CvGeo ring=cv_geo(g_cvSize,1.0f); double band=(ring.rin+ring.R)/2, a=-1.5707963267948966+2*6.283185307179586/g_cvCount;
  POINT word={g_cvAt.x+(LONG)(ring.c+cos(a)*band),g_cvAt.y+(LONG)(ring.c+sin(a)*band)};
  cv_gesture(word,TRUE,TRUE,FALSE,FALSE,FALSE); if (g_cvHover!=2) return 19;
  cv_gesture(word,FALSE,TRUE,FALSE,FALSE,FALSE);
  MSG made; if (!PeekMessageW(&made,g_cvWnd,WM_CV_CREATE,WM_CV_CREATE,PM_REMOVE) || made.wParam!=3 || IsWindowVisible(g_cvWnd)) return 20;
  /* пятая плитка — бланк «Карта заказа» */
  cv_gesture(p0,TRUE,TRUE,FALSE,FALSE,TRUE); cv_gesture(p1,TRUE,TRUE,FALSE,FALSE,FALSE);
  a=-1.5707963267948966+4*6.283185307179586/g_cvCount; POINT karta={g_cvAt.x+(LONG)(ring.c+cos(a)*band),g_cvAt.y+(LONG)(ring.c+sin(a)*band)};
  cv_gesture(karta,TRUE,TRUE,FALSE,FALSE,FALSE); if (g_cvHover!=4) return 30;
  cv_gesture(karta,FALSE,TRUE,FALSE,FALSE,FALSE);
  if (!PeekMessageW(&made,g_cvWnd,WM_CV_CREATE,WM_CV_CREATE,PM_REMOVE) || made.wParam!=5 || IsWindowVisible(g_cvWnd)) return 31;
  /* отпустили в центре — ничего не создаётся, кольцо уходит само */
  cv_gesture(p0,TRUE,TRUE,FALSE,FALSE,TRUE); cv_gesture(p1,TRUE,TRUE,FALSE,FALSE,FALSE); cv_gesture(p1,FALSE,TRUE,FALSE,FALSE,FALSE);
  if (PeekMessageW(&made,g_cvWnd,WM_CV_CREATE,WM_CV_CREATE,PM_REMOVE) || IsWindowVisible(g_cvWnd)) return 21;
  /* «Ссылка»: 250 мс на плитке — вокруг кольца её дуга; центр кольца её убирает;
     с дуги назад на «Ссылку» — остаётся; отпустили на нижнем сегменте — «До файла» (26) */
  POINT at={450,300}; cv_show(at,FALSE); g_cvDismissed=FALSE;
  CvGeo lg=cv_geo(g_cvSize,1.0f); POINTL mid={g_cvAt.x+(LONG)lg.c,g_cvAt.y+(LONG)lg.c};
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,mid,&effect);
  if (g_cvSubTile<0 || g_cvSubTile>=g_cvCount || g_cvFormats[g_cvSubTile]!=24) return 22;
  float la=-1.5707963f+g_cvSubTile*6.2831853f/g_cvCount, lr=(lg.rin+lg.R)/2;
  POINTL tile={mid.x+(LONG)(cosf(la)*lr),mid.y+(LONG)(sinf(la)*lr)};
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_over(&g_cvDrop.iface,MK_SHIFT,tile,&effect);
  if (g_cvHover!=g_cvSubTile || g_cvSubOn) return 23;
  g_cvLinkSince=(GetTickCount()-300)|1; cv_link_dwell();
  int ringCount=g_cvCount;
  if (!g_cvSubOn || !IsWindowVisible(g_cvWnd) || g_cvFormats[g_cvSubTile]!=24) return 24;
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_over(&g_cvDrop.iface,MK_SHIFT,mid,&effect); /* центр кольца — дуга убирается */
  if (g_cvSubOn || !IsWindowVisible(g_cvWnd)) return 25;
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_over(&g_cvDrop.iface,MK_SHIFT,tile,&effect);
  g_cvLinkSince=(GetTickCount()-300)|1; cv_link_dwell(); if (!g_cvSubOn) return 26;
  POINT lowAt=cv_sub_center(1); POINTL low={lowAt.x,lowAt.y}; /* нижний сегмент — «До файла» */
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_over(&g_cvDrop.iface,MK_SHIFT,low,&effect);
  if (g_cvSubHover!=1 || g_cvHover!=-1 || effect!=DROPEFFECT_COPY || !g_cvSubOn) return 27;
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_over(&g_cvDrop.iface,MK_SHIFT,tile,&effect); /* назад на «Ссылку» — дуга остаётся */
  if (!g_cvSubOn || g_cvCount!=ringCount || g_cvHover!=g_cvSubTile || g_cvSubHover!=-1) return 28;
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_over(&g_cvDrop.iface,MK_SHIFT,low,&effect);
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_drop(&g_cvDrop.iface,&data.iface,MK_SHIFT,low,&effect);
  if (g_cvTestLink!=26 || effect!=DROPEFFECT_COPY || IsWindowVisible(g_cvWnd)) return 29;
  cv_shutdown(); DestroyWindow(g_hwnd);
  puts(ok ? "PASS: OLE registration, delayed CF_HDROP retry without re-click, immediate paint, center cancellation, Shift, multiple-file selection, folder-only selection, Shift-click range kept, desktop create ring, COPY effect, threaded conversion, link arc around the ring" : "FAIL: conversion");
  return ok ? 0 : 9;
}
