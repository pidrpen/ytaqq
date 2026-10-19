/* QwenPaw runs in its own bundled Python; no system installation or shell
   commands from the model. All mutations still go through native tools. */
#include "agent_pack.h"
#include "addons_pack.h"
static SRWLOCK g_aqPackLock = SRWLOCK_INIT;
static wchar_t g_aqReady[MAX_PATH];
static BOOL aq_cancelled(AiJob *j) { return j->cancel || (j->externalCancel && *j->externalCancel); }

static BOOL aq_hash_expected(const wchar_t *path, const char *expected) {
  HCRYPTPROV prov = 0; HCRYPTHASH hash = 0;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  BYTE buf[65536], sum[32]; DWORD got, size = 32; BOOL ok = FALSE;
  if (CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
      CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
    ok = TRUE;
    for (;;) {
      if (!ReadFile(f, buf, sizeof(buf), &got, NULL)) { ok = FALSE; break; }
      if (!got) break;
      if (!CryptHashData(hash, buf, got, 0)) { ok = FALSE; break; }
    }
    if (!CryptGetHashParam(hash, HP_HASHVAL, sum, &size, 0) || size != 32) ok = FALSE;
  }
  char hex[65] = {0};
  if (ok) for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", sum[i]);
  if (hash) CryptDestroyHash(hash);
  if (prov) CryptReleaseContext(prov, 0);
  CloseHandle(f);
  return ok && !strcmp(hex, expected);
}

static BOOL aq_write(const wchar_t *path, const char *text) {
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  DWORD n = (DWORD)strlen(text), done = 0;
  BOOL ok = WriteFile(f, text, n, &done, NULL) && n == done;
  CloseHandle(f); return ok;
}

static char *aq_read(const wchar_t *path) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return NULL;
  DWORD n = GetFileSize(f, NULL), done = 0;
  char *text = n < 262144 ? (char *)malloc((size_t)n + 1) : NULL;
  if (text && (!ReadFile(f, text, n, &done, NULL) || n != done)) { free(text); text = NULL; }
  if (text) text[n] = 0;
  CloseHandle(f); return text;
}

/* Child processes are suspended until assigned to a private kill-on-close job.
   Stop/timeout/app exit kills ONLY this child tree, never the user's Python. */
static int aq_process(const wchar_t *exe, wchar_t *cmd, const wchar_t *cwd, AiJob *j, DWORD deadline) {
  STARTUPINFOW si = {0}; PROCESS_INFORMATION pi = {0}; si.cb = sizeof(si);
  HANDLE group = CreateJobObjectW(NULL, NULL);
  if (!group) { ai_log(L"ИИ: этап=создание группы процесса; Windows %lu", GetLastError()); return AI_ERROR; }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim = {0};
  lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(group, JobObjectExtendedLimitInformation, &lim, sizeof(lim)) ||
      !CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, cwd, &si, &pi)) {
    DWORD error = GetLastError();
    ai_log(L"ИИ: этап=запуск помощника; Windows %lu", error);
    CloseHandle(group); return AI_ERROR;
  }
  if (!AssignProcessToJobObject(group, pi.hProcess)) {
    ai_log(L"ИИ: этап=назначение группы процесса; Windows %lu", GetLastError());
    TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(group); return AI_ERROR;
  }
  ResumeThread(pi.hThread); CloseHandle(pi.hThread);
  int status = AI_OK;
  for (;;) {
    if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) break;
    if (aq_cancelled(j)) { status = AI_CANCELLED; break; }
    if ((LONG)(deadline - GetTickCount()) <= 0) { status = AI_TIMEOUT; break; }
  }
  DWORD code = 1;
  if (status == AI_OK && (!GetExitCodeProcess(pi.hProcess, &code) || code)) {
    ai_log(L"ИИ: этап=завершение помощника; код выхода %lu", code); status = AI_ERROR;
  }
  if (status == AI_TIMEOUT) ai_log(L"ИИ: этап=ожидание помощника; время ожидания истекло");
  CloseHandle(group); CloseHandle(pi.hProcess); return status;
}

static BOOL aq_download(AiJob *j, const wchar_t *dest, DWORD deadline, const wchar_t *file, DWORD packSize, const char *sha) {
  /* Archive name is content-addressed and compiled into this version. */
  static const wchar_t *hosts[] = {L"raw.githubusercontent.com", L"github.com", L"github.com"};
  for (int attempt = 0; attempt < 3 && !aq_cancelled(j); attempt++) {
    wchar_t path[220];
    if (attempt == 2) swprintf(path, 220, L"/pidrpen/ytaqq/releases/download/v%ls/%ls", APP_VERSION_STR, file);
    else swprintf(path, 220, attempt ? L"/pidrpen/ytaqq/raw/main/public/%ls" : L"/pidrpen/ytaqq/main/public/%ls", file);
    HINTERNET ses = WinHttpOpen(L"CursorPad-QwenPaw", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!ses) continue;
    WinHttpSetTimeouts(ses, 5000, 5000, 10000, 10000);
    HINTERNET con = WinHttpConnect(ses, hosts[attempt], INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    DWORD total = 0, code = 0; BOOL ok = FALSE;
    HANDLE f = INVALID_HANDLE_VALUE;
    if (req) {
      InterlockedExchangePointer(&j->req, req);
      if (WinHttpSendRequest(req, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(req, NULL)) {
        DWORD size = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &size, NULL);
        if (code == 200) f = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
          BYTE buf[65536]; DWORD got = 0, wrote = 0; ok = TRUE;
          for (;;) {
            if (aq_cancelled(j) || (LONG)(deadline - GetTickCount()) <= 0 ||
                !WinHttpReadData(req, buf, sizeof(buf), &got)) { ok = FALSE; break; }
            if (!got) break;
            if (total + got > packSize || !WriteFile(f, buf, got, &wrote, NULL) || wrote != got) { ok = FALSE; break; }
            total += got;
          }
          CloseHandle(f);
        }
      }
      HINTERNET mine = InterlockedExchangePointer(&j->req, NULL);
      if (mine) WinHttpCloseHandle(mine);
    }
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    if (ok && total == packSize && aq_hash_expected(dest, sha)) return TRUE;
    DeleteFileW(dest);
  }
  return FALSE;
}

static int aq_prepare_pack(AiJob *j, DWORD deadline, wchar_t root[MAX_PATH], BOOL tools, const wchar_t *python) {
  const wchar_t *file = tools ? AT_PACK_FILE : AQ_PACK_FILE;
  const char *sha = tools ? AT_PACK_SHA : AQ_PACK_SHA;
  DWORD packSize = tools ? AT_PACK_SIZE : AQ_PACK_SIZE;
  if (aq_cancelled(j)) return AI_CANCELLED;
  while (!TryAcquireSRWLockExclusive(&g_aqPackLock)) {
    if (aq_cancelled(j)) return AI_CANCELLED;
    if ((LONG)(deadline - GetTickCount()) <= 0) return AI_TIMEOUT;
    Sleep(50);
  }
  int status = AI_ERROR;
  wchar_t parent[MAX_PATH], pack[MAX_PATH], marker[MAX_PATH], py[MAX_PATH], script[MAX_PATH];
    if (wcslen(g_dataDir) > 130) goto done;
  swprintf(parent, MAX_PATH, L"%ls\\%ls", g_dataDir, tools ? L"tools" : L"agent"); CreateDirectoryW(parent, NULL);
  swprintf(root, MAX_PATH, L"%ls\\%hs", parent, sha);
  swprintf(marker, MAX_PATH, L"%ls\\ready.txt", root);
  if (tools) lstrcpynW(py, python, MAX_PATH);
  else swprintf(py, MAX_PATH, L"%ls\\python.exe", root);
  swprintf(script, MAX_PATH, L"%ls\\%ls", root, tools ? L"executor.py" : L"bridge.py");
  char *ready = aq_read(marker);
  BOOL installed = ready && !strcmp(ready, sha) && GetFileAttributesW(py) != INVALID_FILE_ATTRIBUTES &&
                   GetFileAttributesW(script) != INVALID_FILE_ATTRIBUTES;
  if (installed && tools) {
    wchar_t required[MAX_PATH];
    const wchar_t *names[] = {L"node.exe", L"filesystem.cjs", L"lib\\xlsxwriter\\__init__.py"};
    for (int i = 0; i < 3; i++) { swprintf(required, MAX_PATH, L"%ls\\%ls", root, names[i]); if (GetFileAttributesW(required) == INVALID_FILE_ATTRIBUTES) installed = FALSE; }
  }
  free(ready);
  if (installed) { status = AI_OK; goto done; }
  ai_log(L"ИИ: подготавливаю локальный комплект %ls · %hs", tools ? L"XlsxWriter / Filesystem MCP" : L"QwenPaw", sha);
  wchar_t self[MAX_PATH]; GetModuleFileNameW(NULL, self, MAX_PATH);
  wchar_t *slash = wcsrchr(self, L'\\'); if (slash) *slash = 0;
  swprintf(pack, MAX_PATH, L"%ls\\%ls", self, file);
  if (!aq_hash_expected(pack, sha)) {
    swprintf(pack, MAX_PATH, L"%ls\\%ls", parent, file);
    if (!aq_hash_expected(pack, sha) && !aq_download(j, pack, deadline, file, packSize, sha)) {
      j->text = _wcsdup(L"Не удалось получить локальный комплект ИИ из GitHub. Нужен интернет для первой подготовки или архив комплекта рядом с EXE; скачанные модели сохранены.");
      goto done;
    }
  }
  /* The complete archive was verified before extraction. Isolated version
     directory; no update writes to ai/models or another installed agent. */
  CreateDirectoryW(root, NULL);
  wchar_t sys[MAX_PATH], exe[MAX_PATH], cmd[MAX_PATH * 4];
  GetSystemDirectoryW(sys, MAX_PATH);
  swprintf(exe, MAX_PATH, L"%ls\\tar.exe", sys);
  swprintf(cmd, MAX_PATH * 4, L"\"%ls\" -xf \"%ls\" -C \"%ls\"", exe, pack, root);
  status = aq_process(exe, cmd, parent, j, deadline);
  if (status != AI_OK) goto done;
  swprintf(cmd, MAX_PATH * 4, L"\"%ls\" -I -B \"%ls\" --self-test", py, script);
  status = aq_process(py, cmd, root, j, deadline);
  if (status == AI_OK && !aq_write(marker, sha)) status = AI_ERROR;
done:
  if (status == AI_OK && !tools) lstrcpynW(g_aqReady, root, MAX_PATH);
  ReleaseSRWLockExclusive(&g_aqPackLock);
  return aq_cancelled(j) ? AI_CANCELLED : status;
}
static int aq_prepare(AiJob *j, DWORD deadline, wchar_t root[MAX_PATH]) {
  return aq_prepare_pack(j, deadline, root, FALSE, L"");
}
static int at_prepare(AiJob *j, DWORD deadline, wchar_t root[MAX_PATH], wchar_t py[MAX_PATH]) {
  wchar_t core[MAX_PATH];
  int status = aq_prepare(j, deadline, core);
  if (status != AI_OK) return status;
  swprintf(py, MAX_PATH, L"%ls\\python.exe", core);
  return aq_prepare_pack(j, deadline, root, TRUE, py);
}

#ifdef QWEN_TEST
static const wchar_t *g_aqTestBridge;
#endif
static int aq_run(AiJob *j, DWORD deadline, int port) {
  DWORD started = GetTickCount();
  ai_log(L"Вызов ИИ: источник=%ls; режим=%ls; этап=подготовка", j->provider ? L"Z.ai api.z.ai" : L"QwenPaw 127.0.0.1", j->agentTask ? L"инструменты" : L"ответ");
  /* Current product guarantee is offline/local. Do not silently send agent
     context to an external configured host. Ordinary chat keeps its behavior. */
  if (!j->provider && ai_ext() && wcscmp(g_aiExtHost, L"127.0.0.1") && wcscmp(g_aiExtHost, L"localhost")) {
    j->text = _wcsdup(L"QwenPaw работает с локальным сервером модели. Выберите скачанную модель или сервер 127.0.0.1."); return AI_ERROR;
  }
  wchar_t root[MAX_PATH] = {0};
  int status = aq_prepare(j, deadline, root);
  if (status != AI_OK) return status;
  wchar_t work[MAX_PATH], input[MAX_PATH], output[MAX_PATH], key[MAX_PATH], state[MAX_PATH], py[MAX_PATH], script[MAX_PATH];
  swprintf(work, MAX_PATH, L"%ls\\agent\\tasks", g_dataDir); CreateDirectoryW(work, NULL);
  swprintf(input, MAX_PATH, L"%ls\\%lu-%d.in.json", work, GetCurrentProcessId(), j->id);
  swprintf(output, MAX_PATH, L"%ls\\%lu-%d.out.json", work, GetCurrentProcessId(), j->id);
  swprintf(key, MAX_PATH, L"%ls\\%lu-%d.key", work, GetCurrentProcessId(), j->id);
  swprintf(state, MAX_PATH, L"%ls\\%lu-%ld.state.json", work, GetCurrentProcessId(), j->agentTask);
  swprintf(py, MAX_PATH, L"%ls\\python.exe", root);
  swprintf(script, MAX_PATH, L"%ls\\bridge.py", root);
#ifdef QWEN_TEST
  if (g_aqTestBridge) lstrcpynW(script, g_aqTestBridge, MAX_PATH);
#endif
  if (!aq_write(input, j->body) || (!j->provider && !aq_write(key, ai_ext() ? "" : g_aiKey))) { status = AI_ERROR; goto cleanup; }
  wchar_t cmd[MAX_PATH * 8];
  if (j->provider) {
    wchar_t protectedKey[MAX_PATH]; ai_zai_key_path(protectedKey);
    swprintf(cmd, MAX_PATH * 8, L"\"%ls\" -I -B \"%ls\" --request \"%ls\" --response \"%ls\" --state \"%ls\" --provider zai --protected-key \"%ls\" --endpoint https://api.z.ai/api/paas/v4/chat/completions --timeout 120 %ls", py, script, input, output, state, protectedKey, j->agentTask ? L"" : L"--chat");
  } else swprintf(cmd, MAX_PATH * 8, L"\"%ls\" -I -B \"%ls\" --request \"%ls\" --response \"%ls\" --state \"%ls\" --key-file \"%ls\" --endpoint http://127.0.0.1:%d/v1/chat/completions --timeout 120", py, script, input, output, state, key, port);
  status = aq_process(py, cmd, root, j, deadline);
  if (status == AI_OK) {
    char *response = aq_read(output);
    const char *field = response ? json_find_string(response, j->agentTask ? "action" : "text") : NULL;
    if (field) j->text = ai_json_unstr(field);
    else {
      field = response ? json_find_string(response, "error") : NULL;
      if (field) j->text = ai_json_unstr(field);
      status = AI_ERROR;
    }
    free(response);
    if (!j->text) status = AI_ERROR;
  }
  if (status == AI_OK) ai_log(L"%ls: %ls", j->provider ? L"Z.ai" : L"QwenPaw", j->agentTask ? L"инструмент выбран" : L"ответ получен");
  if (status != AI_CANCELLED) ai_log(L"Вызов ИИ: источник=%ls; результат=%ls; %lu мс", j->provider ? L"Z.ai" : L"QwenPaw", status == AI_OK ? L"успешно" : L"ошибка", GetTickCount() - started);
cleanup:
  DeleteFileW(input); DeleteFileW(output); DeleteFileW(key);
  if (status != AI_OK && status != AI_CANCELLED && !j->text)
    j->text = _wcsdup(L"Не удалось запустить комплект QwenPaw. Проверьте журнал ИИ; пользовательские файлы не изменены.");
  return status;
}

static AiJob g_aqWarm;
static volatile LONG g_aqWarmActive;
static DWORD WINAPI aq_warm_thread(LPVOID unused) {
  (void)unused;
  wchar_t root[MAX_PATH];
  int status = aq_prepare(&g_aqWarm, GetTickCount() + 600000, root);
  if (status == AI_OK) { wchar_t py[MAX_PATH]; status = at_prepare(&g_aqWarm, GetTickCount() + 600000, root, py); }
  ai_log(status == AI_OK ? L"QwenPaw, XlsxWriter и Filesystem MCP готовы к локальной работе." : L"ИИ: фоновая подготовка не завершена; повторится при запросе.");
  free(g_aqWarm.text); g_aqWarm.text = NULL;
  InterlockedExchange(&g_aqWarmActive, 0); return 0;
}
static void aq_warm_start(void) {
  if (InterlockedCompareExchange(&g_aqWarmActive, 1, 0)) return;
  memset(&g_aqWarm, 0, sizeof(g_aqWarm));
  HANDLE th = CreateThread(NULL, 0, aq_warm_thread, NULL, 0, NULL);
  if (th) CloseHandle(th); else InterlockedExchange(&g_aqWarmActive, 0);
}
static void aq_warm_cancel(void) {
  InterlockedExchange(&g_aqWarm.cancel, 1);
  HINTERNET req = InterlockedExchangePointer(&g_aqWarm.req, NULL);
  if (req) WinHttpCloseHandle(req);
}
