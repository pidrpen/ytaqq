/* Test-only entry: no hooks, startup, updater, settings or user model changes. */
#define CURSORPAD_EMBEDDED_PROBE
#include "agent_probe.c"
static int aq_test(void) {
  int argc = 0; LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv && argc == 2) { int code = ag_probe_main(argc, argv); LocalFree(argv); return code; }
  if (!argv || (argc != 5 && argc != 7)) { if (argv) LocalFree(argv); return 2; }
  lstrcpynW(g_dataDir, argv[1], MAX_PATH); CreateDirectoryW(g_dataDir, NULL);
  AiJob job = {0}; job.id = 1; job.agentTask = 1;
  job.body = aq_read(argv[2]);
  wchar_t root[MAX_PATH];
  int port = _wtoi(argv[4]);
  int status, testedSlot = 0;
  if (port == -3 && argc == 7 && job.body) {
    AgFs *fs = (AgFs *)calloc(1, sizeof(AgFs));
    wchar_t *action = ai_wide(job.body, -1), observation[6000] = L"";
    if (!fs || !action) { free(fs); free(action); LocalFree(argv); free(job.body); return 3; }
    ag_fs_init(fs, g_dataDir, argv[5]);
    lstrcpynW(fs->desktop, argv[5], MAX_PATH); fs->downloads[0] = fs->documents[0] = 0;
    fs->backend = at_call; fs->cancel = &job.cancel;
    int state = ag_tool(fs, action, observation, 6000);
    if (state >= 0 && fs->count && !wcscmp(argv[6], L"apply")) state = ag_apply(fs, observation, 6000) ? 1 : -1;
    char *observed = ai_json_str(observation), *result = (char *)malloc((observed ? strlen(observed) : 0) + 150);
    if (result) { sprintf(result, "{\"state\":%d,\"count\":%d,\"observation\":%s}", state, fs->count, observed ? observed : "null"); aq_write(argv[3], result); }
    free(result); free(observed); free(fs); free(action); free(job.body); LocalFree(argv);
    return state >= 0 ? 0 : 1;
  } else if (port == -2 && argc == 7 && job.body) {
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
