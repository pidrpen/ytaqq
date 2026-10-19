/* Verify Office dispatch parameters and output publication without requiring
   Microsoft Office. These mocks do not establish real Office compatibility. */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <oleauto.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
typedef struct { IDispatch *app, *collection, *doc; BOOL excel, initialized; HRESULT hr; } OfSession;
static BOOL test_excel, test_closed, test_fail;
static VARIANT of_int(long n) { VARIANT v; VariantInit(&v); v.vt=VT_I4; v.lVal=n; return v; }
static VARIANT of_bool(BOOL n) { VARIANT v; VariantInit(&v); v.vt=VT_BOOL; v.boolVal=n?VARIANT_TRUE:VARIANT_FALSE; return v; }
static VARIANT of_str(const wchar_t *s) { VARIANT v; VariantInit(&v); v.vt=VT_BSTR; v.bstrVal=SysAllocString(s); return v; }
static BOOL of_begin(OfSession *s, BOOL excel, const wchar_t *path) {
  assert(path == NULL); memset(s,0,sizeof(*s)); s->doc=(IDispatch*)3; s->collection=(IDispatch*)2; s->hr=S_OK; test_excel=excel; test_closed=FALSE; return TRUE;
}
static void of_release(IDispatch *d) { (void)d; }
static void of_end(OfSession *s) { (void)s; test_closed=TRUE; }
static IDispatch *of_sheet(OfSession *s, const wchar_t *name) { (void)s; assert(name==NULL); return (IDispatch*)5; }
static IDispatch *of_obj(IDispatch *d, const wchar_t *name, int n, VARIANT *args) { (void)d;(void)n;(void)args; assert(!wcscmp(name,L"Content")); return (IDispatch*)4; }
static HRESULT of_call(IDispatch *d, const wchar_t *name, WORD flags, VARIANT *out, int n, VARIANT *args) {
  (void)flags;
  if (!wcscmp(name,L"Open")) {
    assert(n==(test_excel?13:15) && args[2].vt==VT_BOOL && args[2].boolVal==VARIANT_TRUE);
    assert(args[4].vt==VT_BSTR && !args[4].bstrVal[0]);
    if (test_excel) assert(args[1].vt==VT_I4 && args[1].lVal==0);
    else { assert(args[3].boolVal==VARIANT_FALSE && args[11].boolVal==VARIANT_FALSE && args[14].boolVal==VARIANT_TRUE); }
    if (out) { VariantInit(out); if (!test_fail) { out->vt=VT_DISPATCH; out->pdispVal=(IDispatch*)6; } }
  } else if (!wcscmp(name,L"ExportAsFixedFormat") || !wcscmp(name,L"SaveAs")) {
    assert(n==2); const wchar_t *path;
    if (!wcscmp(name,L"SaveAs")) { assert(d==(IDispatch*)5 && args[1].lVal==62); path=args[0].bstrVal; }
    else if (test_excel) { assert(args[0].lVal==0 && args[1].vt==VT_BSTR); path=args[1].bstrVal; }
    else { assert(args[0].vt==VT_BSTR && args[1].lVal==17); path=args[0].bstrVal; }
    if (!test_fail) { HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,TRUNCATE_EXISTING,0,NULL); assert(f!=INVALID_HANDLE_VALUE); DWORD wrote; WriteFile(f,"mock export",11,&wrote,NULL); CloseHandle(f); }
  } else if (!wcscmp(name,L"Text")) { assert(out); *out=of_str(L"Привет\rМир"); }
  else assert(!wcscmp(name,L"Close"));
  for (int i=0;i<n;i++) VariantClear(&args[i]);
  return test_fail?E_FAIL:S_OK;
}
#include "convert_core.c"
int wmain(int argc, wchar_t **argv) {
  if (argc!=2) return 2;
  wchar_t dir[MAX_PATH]; GetFullPathNameW(argv[1],MAX_PATH,dir,NULL); CreateDirectoryW(dir,NULL);
  for (int i=0;i<4;i++) {
    CvJob job={0}; job.format=i==0||i==2?4:i==1?5:6;
    swprintf(job.src,MAX_PATH,L"%ls\\source-%d.%ls",dir,i,i<2?L"docx":L"xlsx");
    HANDLE f=CreateFileW(job.src,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL); assert(f!=INVALID_HANDLE_VALUE);
    DWORD wrote; WriteFile(f,"source",6,&wrote,NULL); CloseHandle(f);
    assert(cv_convert(&job) && test_closed && job.out[0]);
    char text[128]={0}; f=CreateFileW(job.src,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); DWORD got; ReadFile(f,text,127,&got,NULL); CloseHandle(f); assert(got==6 && !memcmp(text,"source",6));
    if (i==1) {
      f=CreateFileW(job.out,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); ReadFile(f,text,127,&got,NULL); CloseHandle(f);
      const char expected[]="\xef\xbb\xbfПривет\nМир"; assert(got==sizeof(expected)-1 && !memcmp(text,expected,got));
    }
    DeleteFileW(job.out);
    test_fail=TRUE; assert(!cv_convert(&job) && !job.out[0] && job.error[0] && test_closed); test_fail=FALSE;
    DeleteFileW(job.src);
  }
  RemoveDirectoryW(dir);
  puts("PASS: Office read-only/no-password-dialog flags, Word/Excel PDF arguments, UTF-8 TXT, first-sheet CSV, source preservation and failed-output cleanup (mock Office)");
  return 0;
}
