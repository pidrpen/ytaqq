/* Explicit cloud opt-in. Credential is DPAPI-protected for this Windows user;
   it never enters ai.txt, diagnostics, command lines or task JSON. */
#include <wincrypt.h>
static int g_aiProvider; /* 0 local, 1 Z.ai standard API */
static char g_aiZaiModel[64] = "glm-4.5-flash";
static void ai_zai_key_path(wchar_t *out) { swprintf(out, MAX_PATH, L"%ls\\zai-key.dat", g_dataDir); }
static BOOL ai_zai_model_valid(const char *model) {
  return !strcmp(model, "glm-4.5-flash") || !strcmp(model, "glm-4.7-flash") || !strcmp(model, "glm-4.6v-flash");
}
static BOOL ai_zai_has_key(void) {
  wchar_t path[MAX_PATH]; ai_zai_key_path(path);
  DWORD a = GetFileAttributesW(path); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static BOOL ai_zai_store_key(const wchar_t *key) {
  char plain[1025]; size_t n = wcslen(key);
  if (!n || n > 1024) return FALSE;
  for (size_t i = 0; i < n; i++) if (key[i] <= 32 || key[i] >= 127) return FALSE;
  for (size_t i = 0; i < n; i++) plain[i] = (char)key[i];
  plain[n] = 0;
  static const char salt[] = "CursorPad-ZAI-v1";
  DATA_BLOB input = {(DWORD)n, (BYTE *)plain}, entropy = {sizeof(salt) - 1, (BYTE *)salt}, sealed = {0};
  BOOL ok = CryptProtectData(&input, L"CursorPad Z.ai", &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &sealed);
  SecureZeroMemory(plain, sizeof(plain));
  if (!ok) return FALSE;
  wchar_t path[MAX_PATH], tmp[MAX_PATH]; ai_zai_key_path(path);
  swprintf(tmp, MAX_PATH, L"%ls\\zai-key-%lu.tmp", g_dataDir, GetCurrentProcessId());
  HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); DWORD written = 0;
  ok = h != INVALID_HANDLE_VALUE;
  if (ok) { ok = WriteFile(h, sealed.pbData, sealed.cbData, &written, NULL) && written == sealed.cbData && FlushFileBuffers(h); CloseHandle(h); }
  if (ok) ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (!ok) DeleteFileW(tmp);
  LocalFree(sealed.pbData); return ok;
}
static BOOL ai_zai_vision(void) {
  return !strcmp(g_aiZaiModel, "glm-4.6v-flash");
}
static const char *ai_selected_model(void) { return g_aiProvider ? g_aiZaiModel : g_aiExtModel[0] ? g_aiExtModel : "local"; }
