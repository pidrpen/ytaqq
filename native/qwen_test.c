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
  int testedOff = 0, testedStopped = 0, testedQueue = -1;
  wchar_t testedWeb[AC_WEB_CAP] = L"", testedNote[360] = L"";
  if (port == -11 && argc == 7 && job.body) {
    ac_push(0, L"https://www.ozon.ru/product/test-123/ что по ссылке");
    ac_push(1, L"Подшипник 999. https://unrelated.example/wrong цена 1499");
    ac_push(0, L"какие характеристики у этого подшипника");
    wchar_t *resolved = ac_web_question(ac_last_role(0));
    BOOL ok = resolved && wcsstr(resolved, L"https://www.ozon.ru/product/test-123/") && !wcsstr(resolved, L"unrelated") && !wcsstr(resolved, L"1499");
    free(resolved);
    ac_push(1, L"Ответ"); ac_push(0, L"а размеры у него");
    resolved = ac_web_question(ac_last_role(0));
    ok = ok && resolved && wcsstr(resolved, L"https://www.ozon.ru/product/test-123/"); free(resolved);
    resolved = ac_web_question(L"новости Петербурга");
    ok = ok && resolved && !wcsstr(resolved, L"https://"); free(resolved);
    resolved = ac_web_question(L"а у этого https://docs.python.org/3/");
    ok = ok && resolved && !wcsstr(resolved, L"ozon"); free(resolved);
    ac_push(0, L"расскажи про погоду"); ac_push(1, L"Ответ"); ac_push(0, L"а какая у него температура");
    resolved = ac_web_question(ac_last_role(0));
    ok = ok && resolved && !wcsstr(resolved, L"ozon"); free(resolved);
    status = ok ? AI_OK : AI_ERROR;
    job.text = _wcsdup(ok ? L"PASS: user-link follow-ups, assistant ignored, new topic/new link isolated" : L"FAIL");
    ac_clear_turns();
  } else if (port == -10 && argc == 7 && job.body) {
    /* Real link failure -> actual chat handler, with no model or user profile. */
    wchar_t *question = ai_wide(job.body, -1); AcWebInfo info = {0};
    if (question && !ac_web_find(question, testedWeb, AC_WEB_CAP, &info) && !wcsncmp(info.note, L"Прямая ссылка:", 14)) {
      ac_push(0, question);
      ac_finish_ask(_wcsdup(testedWeb), info.note);
      const wchar_t *answer = ac_last_role(1);
      if (answer) job.text = _wcsdup(answer);
      status = answer && wcsstr(answer, L"Не удалось прочитать") && !g_acId && !g_aiProc ? AI_OK : AI_ERROR;
      ac_clear_turns();
    } else status = AI_ERROR;
    lstrcpynW(testedNote, info.note, 360); free(question);
  } else if (port == -3 && argc == 7 && job.body) {
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
  } else if ((port == -6 || port == -9) && argc == 7 && job.body) {
    /* Actual native HTTP search -> production system prompt -> local Qwen. */
    g_aiOn = TRUE; g_aiCtx = 4096; g_aiGpu = TRUE; ai_out_cap(128);
    lstrcpynW(g_aiModel, argv[5], MAX_PATH); lstrcpynW(g_aiSrvExe, argv[6], MAX_PATH);
    job.agentTask = 0; job.timeout = 180000;
    char cachedQuery[2000];
    BOOL cached = port == -9 && acj_text(job.body, job.body + strlen(job.body), "query", cachedQuery, sizeof(cachedQuery));
    wchar_t *question = ai_wide(cached ? cachedQuery : job.body, -1), sys[14000]; AcWebInfo info = {0};
    BOOL found = question && (cached ? acw_tavily_parse(job.body, (DWORD)strlen(job.body), question, testedWeb, AC_WEB_CAP, &info)
                                    : ac_web_find(question, testedWeb, AC_WEB_CAP, &info));
    if (found) {
      ac_web_read_pages(question, testedWeb, AC_WEB_CAP, &info);
      ac_sys_build(question, testedWeb, sys, 14000);
      AiTurn turn = {0}; turn.text = question;
      char *body = ai_body_chat(sys, &turn, 1, FALSE);
      free(job.body); job.body = body;
      if (body) { ai_job_run(&job); status = job.status; testedSlot = g_aiSlotCtx; }
      else status = AI_ERROR;
    } else status = AI_ERROR;
    lstrcpynW(testedNote, info.note, 360);
    if (status == AI_OK && job.text) {
      ac_web_foot(testedWeb, testedNote); ac_push_with_foot(job.text);
      wchar_t *shown = _wcsdup(ac_last_role(1));
      if (shown) { free(job.text); job.text = shown; }
      ac_clear_turns();
      HANDLE process = NULL;
      if (g_aiProc) DuplicateHandle(GetCurrentProcess(), g_aiProc, GetCurrentProcess(), &process, SYNCHRONIZE, FALSE, 0);
      AiJob *queued = ai_job_new(ai_body(L"Ответь кратко", "\"привет\"", 16), 2000);
      testedOff = ai_ui_command(NULL, ID_AI_OFF, BN_CLICKED) && !g_aiOn;
      testedStopped = process && WaitForSingleObject(process, 3000) == WAIT_OBJECT_0 && !g_aiProc;
      if (process) CloseHandle(process);
      if (queued) { ai_job_run(queued); testedQueue = queued->status; ai_job_release(queued); }
    }
    free(question); ai_shutdown();
  } else if (port == -4 && argc == 7 && job.body) {
    /* Real chat image body + managed tier selection + production launcher.
       The caller supplies an isolated profile containing model hard links. */
    g_aiOn = TRUE; g_aiTier = 1; g_aiCtx = 4096; g_aiGpu = TRUE; ai_out_cap(200);
    lstrcpynW(g_aiSrvExe, argv[6], MAX_PATH);
    job.agentTask = 0; job.timeout = 180000;
    wchar_t model[MAX_PATH], mm[MAX_PATH];
    if (!ai_pick_model(model, mm) || !ai_vision_ready()) status = AI_NO_VISION;
    else {
      char *raw = aq_read(argv[5]);
      wchar_t *question = ai_wide(job.body, -1);
      if (!raw || !question) status = AI_ERROR;
      else {
        AiTurn turn = {0}; turn.text = question;
        turn.png = (BYTE *)raw; turn.pngLen = (DWORD)ai_file_size(argv[5]);
        char *body = ai_body_chat(L"Ответь на вопрос по приложенной картинке. Кратко, по-русски.", &turn, 1, FALSE);
        free(job.body); job.body = body;
        if (body) { ai_job_run(&job); status = job.status; testedSlot = g_aiSlotCtx; }
        else status = AI_ERROR;
      }
      free(raw); free(question);
    }
    ai_shutdown();
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
  char *web = ai_json_str(testedWeb), *note = ai_json_str(testedNote);
  char *result = (char *)malloc((text ? strlen(text) : 0) + (web ? strlen(web) : 0) + (note ? strlen(note) : 0) + 400);
  if (result) {
    sprintf(result, "{\"status\":%d,\"configured_context\":%d,\"slot_context\":%d,\"text\":%s,\"web\":%s,\"search_note\":%s,\"off_button\":%d,\"server_stopped\":%d,\"queued_after_off_status\":%d}", status, g_aiCtx, testedSlot ? testedSlot : g_aiSlotCtx, text ? text : "null", web ? web : "null", note ? note : "null", testedOff, testedStopped, testedQueue);
    aq_write(argv[3], result);
  }
  free(result); free(web); free(note); free(text); free(job.body); free(job.text); LocalFree(argv);
  return status == AI_OK ? 0 : 1;
}
