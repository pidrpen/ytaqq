/* Test-only entry: no hooks, startup, updater, settings or user model changes. */
static int aq_test(void) {
  int argc = 0; LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv || (argc != 5 && argc != 7)) { if (argv) LocalFree(argv); return 2; }
  lstrcpynW(g_dataDir, argv[1], MAX_PATH); CreateDirectoryW(g_dataDir, NULL);
  AiJob job = {0}; job.id = 1; job.agentTask = 1;
  job.body = aq_read(argv[2]);
  wchar_t root[MAX_PATH];
  int port = _wtoi(argv[4]);
  int status, testedSlot = 0;
  if (port == -2 && argc == 7 && job.body) {
    g_aiOn = TRUE; g_aiCtx = 2048; g_aiGpu = TRUE;
    lstrcpynW(g_aiModel, argv[5], MAX_PATH); lstrcpynW(g_aiSrvExe, argv[6], MAX_PATH);
    wchar_t aiDir[MAX_PATH]; ai_dir(aiDir, NULL); CreateDirectoryW(aiDir, NULL);
    job.timeout = 180000;
    ai_job_run(&job); status = job.status; testedSlot = g_aiSlotCtx;
    ai_shutdown();
  } else status = !port ? aq_prepare(&job, GetTickCount() + 600000, root) :
    job.body ? aq_run(&job, GetTickCount() + 600000, port) : AI_ERROR;
  char *text = ai_json_str(job.text ? job.text : ai_status_text(status));
  char *result = (char *)malloc((text ? strlen(text) : 0) + 100);
  if (result) {
    sprintf(result, "{\"status\":%d,\"configured_context\":%d,\"slot_context\":%d,\"text\":%s}", status, g_aiCtx, testedSlot ? testedSlot : g_aiSlotCtx, text ? text : "null");
    aq_write(argv[3], result);
  }
  free(result); free(text); free(job.body); free(job.text); LocalFree(argv);
  return status == AI_OK ? 0 : 1;
}
