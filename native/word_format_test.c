#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "ai_agent_fs.c"
#include "ai_agent_route.h"
static AgFs fs;
static wchar_t message[6000];
#define CHECK(x) do { if (!(x)) { char u[24000]; WideCharToMultiByte(CP_UTF8,0,message,-1,u,sizeof(u),NULL,NULL); fprintf(stderr,"FAIL %d: %s\n%s\n",__LINE__,#x,u); return 1; } } while(0)
static BOOL reject_author(const wchar_t *goal, const wchar_t *draft, wchar_t *out, int n, volatile LONG *cancel, wchar_t *error, int cap) {
  (void)goal;(void)draft;(void)out;(void)n;(void)cancel; lstrcpynW(error,L"Rejected test draft",cap); return FALSE;
}
int wmain(int argc, wchar_t **argv) {
  CHECK(argc == 3); wchar_t root[MAX_PATH], path[MAX_PATH], baseline[MAX_PATH], hash[65], after[65], backup[MAX_PATH];
  CHECK(GetFullPathNameW(argv[1],MAX_PATH,root,NULL) && CreateDirectoryW(root,NULL));
  swprintf(path,MAX_PATH,L"%ls\\Проверка.docx",root); swprintf(baseline,MAX_PATH,L"%ls\\baseline.docx",root);
  CHECK(CopyFileW(argv[2],path,TRUE) && CopyFileW(path,baseline,TRUE));
  ag_fs_init(&fs,L"C:\\protected-data",L""); lstrcpynW(fs.desktop,root,MAX_PATH); fs.downloads[0]=fs.documents[0]=0;
  lstrcpynW(fs.lastDoc,path,MAX_PATH); lstrcpynW(fs.goal,L"отредактируй тот файл чтобы выравнивание было по центру",4000);
  CHECK(!ag_file_intent(L"какой вес\\рост")); CHECK(!ag_file_intent(L"это по картинке ранее вопрос уточняющий"));
  CHECK(ag_file_intent(L"отредактируй тот файл чтобы выравнивание было по центру"));
  CHECK(ag_file_intent(L"прочитай C:\\Папка\\файл.txt"));
  CHECK(ag_word_hash(path,hash));
  CHECK(ag_tool(&fs,L"{\"tool\":\"word_format\",\"path\":\"@document\",\"mode\":\"right\"}",message,6000)==-1 && !fs.count);
  CHECK(ag_tool(&fs,L"{\"tool\":\"write\",\"path\":\"@document\",\"text\":\"Выравнивание по центру\"}",message,6000)==-1 && !fs.count);
  CHECK(ag_tool(&fs,L"{\"tool\":\"word_format\",\"path\":\"@document\",\"mode\":\"center\"}",message,6000)==0 && fs.count==1);
  lstrcpynW(backup,fs.plan[0].text,MAX_PATH);
  CHECK(ag_word_hash(path,after) && !wcscmp(hash,after) && GetFileAttributesW(backup)==INVALID_FILE_ATTRIBUTES);
  HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL); CHECK(f!=INVALID_HANDLE_VALUE);
  DWORD done; CHECK(WriteFile(f,"X",1,&done,NULL)); CHECK(SetFileTime(f,NULL,NULL,&fs.plan[0].before.ftLastWriteTime)); CloseHandle(f);
  CHECK(!ag_apply(&fs,message,6000) && GetFileAttributesW(backup)==INVALID_FILE_ATTRIBUTES);
  CHECK(CopyFileW(baseline,path,FALSE)); fs.count=0;
  CHECK(ag_tool(&fs,L"{\"tool\":\"word_format\",\"path\":\"@document\",\"mode\":\"center\"}",message,6000)==0);
  lstrcpynW(backup,fs.plan[0].text,MAX_PATH);
  CHECK(ag_apply(&fs,message,6000)); CHECK(ag_word_hash(backup,after) && !wcscmp(hash,after));
  CHECK(!wcscmp(fs.lastDoc,path));
  fs.count=0; fs.author=reject_author; lstrcpynW(fs.goal,L"создай ворд с содержимым",4000);
  CHECK(ag_tool(&fs,L"{\"tool\":\"write\",\"path\":\"@desktop\\\\Rejected.docx\",\"text\":\"bad draft\"}",message,6000)==-1 && !fs.count);
  puts("PASS: real Word formatting, unchanged-before-apply, SHA256 change rejection, exact original backup, last document, rejected author, routing");
  return 0;
}
