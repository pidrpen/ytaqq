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
#include "convert.c"
typedef struct { IDataObject iface; const wchar_t *path; BOOL multiple; } FakeData;
static HRESULT STDMETHODCALLTYPE data_query(IDataObject *self, REFIID iid, void **out) { (void)iid; *out = self; return S_OK; }
static ULONG STDMETHODCALLTYPE data_ref(IDataObject *self) { (void)self; return 1; }
static HRESULT STDMETHODCALLTYPE data_get(IDataObject *self, FORMATETC *format, STGMEDIUM *medium) {
  FakeData *data = (FakeData *)self;
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
static void capture(const wchar_t *path) {
  HDC dc = GetDC(g_cvWnd), memory = CreateCompatibleDC(dc);
  HBITMAP bitmap = CreateCompatibleBitmap(dc,g_cvSize,g_cvSize), old = SelectObject(memory,bitmap);
  BitBlt(memory,0,0,g_cvSize,g_cvSize,dc,0,0,SRCCOPY); SelectObject(memory,old);
  BITMAPINFO info = {0}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = g_cvSize;
  info.bmiHeader.biHeight = -g_cvSize; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
  DWORD size = (DWORD)g_cvSize*g_cvSize*4; BYTE *pixels = (BYTE *)malloc(size);
  GetDIBits(memory,bitmap,0,g_cvSize,pixels,&info,DIB_RGB_COLORS);
  BITMAPFILEHEADER header = {0}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader); header.bfSize=header.bfOffBits+size;
  FILE *file = _wfopen(path,L"wb"); if (file) { fwrite(&header,sizeof(header),1,file); fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,file); fwrite(pixels,size,1,file); fclose(file); }
  free(pixels); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(g_cvWnd,dc);
}
int wmain(int argc, wchar_t **argv) {
  if (argc != 3) return 2;
  g_inst = GetModuleHandleW(NULL); WNDCLASSW klass={0}; klass.hInstance=g_inst; klass.lpfnWndProc=test_proc; klass.lpszClassName=L"ConvertTestOwner";
  RegisterClassW(&klass); g_hwnd=CreateWindowW(klass.lpszClassName,L"test",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,g_inst,NULL);
  cv_init(); if (!g_cvRegistered) return 3;
  wchar_t path[MAX_PATH]; GetFullPathNameW(argv[1],MAX_PATH,path,NULL);
  FakeData data={{&data_vtable},path,FALSE}; POINT show={450,300}; cv_show(show); UpdateWindow(g_cvWnd);
  RECT rect; GetWindowRect(g_cvWnd,&rect); POINTL center={rect.left+g_cvSize/2,rect.top+g_cvSize/2};
  DWORD effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;
  cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,center,&effect);
  if (effect != DROPEFFECT_NONE || g_cvCount != 3) return 4;
  POINTL sector={center.x+(LONG)(g_cvSize*.30),center.y-(LONG)(g_cvSize*.17)};
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_over(&g_cvDrop.iface,0,sector,&effect); if (effect != DROPEFFECT_NONE) return 5;
  data.multiple=TRUE; effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;
  cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,sector,&effect); if (effect != DROPEFFECT_NONE || g_cvCount) return 6;
  data.multiple=FALSE; effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;
  cv_enter(&g_cvDrop.iface,&data.iface,MK_SHIFT,sector,&effect); if (effect != DROPEFFECT_COPY) return 7;
  UpdateWindow(g_cvWnd); capture(argv[2]);
  effect=DROPEFFECT_COPY|DROPEFFECT_MOVE; cv_drop(&g_cvDrop.iface,&data.iface,MK_SHIFT,sector,&effect);
  if (effect != DROPEFFECT_COPY) return 8;
  DWORD started=GetTickCount();
  while (g_cvBusy && GetTickCount()-started < 10000) {
    MSG msg; while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(10);
  }
  BOOL ok=!g_cvBusy && g_cvLast[0] && GetFileAttributesW(g_cvLast)!=INVALID_FILE_ATTRIBUTES;
  cv_shutdown(); DestroyWindow(g_hwnd);
  puts(ok ? "PASS: OLE registration, CF_HDROP, center cancellation, Shift, multiple-file rejection, COPY effect, threaded conversion" : "FAIL: conversion");
  return ok ? 0 : 9;
}
