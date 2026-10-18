/* The native host, not the model, selects this fixed executable and operation.
   Uses the same cancellable private child job as QwenPaw. No shell commands. */
static LONG g_atSerial;
static BOOL at_call(const wchar_t *op, const wchar_t *path, const wchar_t *text,
                    const wchar_t *sheet, const wchar_t *cell, const wchar_t *mode,
                    const wchar_t *offset, volatile LONG *cancel, wchar_t *out, int cap) {
  AiJob job = {0}; job.externalCancel = cancel;
  wchar_t root[MAX_PATH], py[MAX_PATH], work[MAX_PATH], input[MAX_PATH], output[MAX_PATH], script[MAX_PATH];
  DWORD deadline = GetTickCount() + 600000;
  int status = at_prepare(&job, deadline, root, py);
  BOOL ok = FALSE;
  if (status != AI_OK) { lstrcpynW(out, job.text ? job.text : L"Локальные инструменты не подготовлены. Нужен комплект CursorPad-tools рядом с EXE или интернет для его первой загрузки.", cap); free(job.text); return FALSE; }
  swprintf(work, MAX_PATH, L"%ls\\tools\\tasks", g_dataDir); CreateDirectoryW(work, NULL);
  LONG serial = InterlockedIncrement(&g_atSerial);
  swprintf(input, MAX_PATH, L"%ls\\%lu-%ld.in.json", work, GetCurrentProcessId(), serial);
  swprintf(output, MAX_PATH, L"%ls\\%lu-%ld.out.json", work, GetCurrentProcessId(), serial);
  swprintf(script, MAX_PATH, L"%ls\\executor.py", root);
  const wchar_t *values[] = {op, path, text, sheet, cell, mode, offset};
  char *quoted[7] = {0}; size_t size = 200;
  for (int i = 0; i < 7; i++) { quoted[i] = ai_json_str(values[i]); if (!quoted[i]) goto done; size += strlen(quoted[i]); }
  char *body = (char *)malloc(size);
  if (!body) goto done;
  snprintf(body, size, "{\"op\":%s,\"path\":%s,\"text\":%s,\"sheet\":%s,\"cell\":%s,\"mode\":%s,\"offset\":%s}", quoted[0], quoted[1], quoted[2], quoted[3], quoted[4], quoted[5], quoted[6]);
  ok = aq_write(input, body); free(body);
  if (!ok) goto done;
  wchar_t cmd[MAX_PATH * 8];
  swprintf(cmd, MAX_PATH * 8, L"\"%ls\" -I -B \"%ls\" --request \"%ls\" --response \"%ls\"", py, script, input, output);
  status = aq_process(py, cmd, root, &job, GetTickCount() + 60000);
  ok = FALSE;
  if (status == AI_OK) {
    char *response = aq_read(output);
    const char *field = response ? json_find_string(response, "text") : NULL;
    if (field) { wchar_t *value = ai_json_unstr(field); if (value) { lstrcpynW(out, value, cap); free(value); ok = TRUE; } }
    else {
      field = response ? json_find_string(response, "error") : NULL;
      if (field) { wchar_t *value = ai_json_unstr(field); if (value) { swprintf(out, cap, L"Локальный инструмент: %ls", value); free(value); } }
    }
    free(response);
  } else lstrcpynW(out, status == AI_CANCELLED ? L"Локальный инструмент остановлен." : L"Локальный инструмент не завершился за 60 секунд. Проверьте результат перед повтором.", cap);
done:
  for (int i = 0; i < 7; i++) free(quoted[i]);
  DeleteFileW(input); DeleteFileW(output); free(job.text);
  if (!ok && !out[0]) lstrcpynW(out, L"Не удалось вызвать локальный инструмент.", cap);
  return ok;
}
