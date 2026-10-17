/* Test-only entry: no hooks, startup, updater, settings or user model changes. */
#define CURSORPAD_EMBEDDED_PROBE
#include "agent_probe.c"
static LRESULT CALLBACK aq_hidden_chat(HWND h, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_AI_CHAT) {
    AiResult *r = (AiResult *)l;
    if (g_acReviewStage == 1 && r && r->text) {
      wchar_t path[MAX_PATH]; _snwprintf(path, MAX_PATH, L"%s\\draft.txt", g_dataDir);
      char *text = ai_utf8(r->text); if (text) aq_write(path, text); free(text);
    }
    ac_result(r); return 0;
  }
  if (msg == WM_AC_WEB) { ac_web_ready((AcWebDone *)l); return 0; }
  return DefWindowProcW(h, msg, w, l);
}
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
  if (port == -20 && argc == 7 && job.body) {
    wchar_t *goal = ai_wide(job.body, -1), before[65], after[65];
    lstrcpynW(g_agLastDoc, argv[5], MAX_PATH);
    BOOL ok = goal && ag_word_hash(argv[5], before) && ag_start(goal);
    ok = ok && g_agReview && g_acPlanOn && g_agFs.count == 1 && g_agFs.plan[0].kind == AG_WORD_FORMAT && !g_acId;
    ok = ok && ag_word_hash(argv[5], after) && !wcscmp(before, after);
    ag_stop(); g_acPlanOn = FALSE; ac_clear_turns(); free(goal);
    status = ok ? AI_OK : AI_ERROR;
    job.text = _wcsdup(ok ? L"PASS: actual edit request -> native format plan for last Word, no model/search, original unchanged" : L"FAIL");
  } else if (port == -19 && argc == 7 && job.body) {
    wchar_t *text = ai_wide(job.body, -1); status = AI_ERROR;
    if (text) {
      ag_author_layout(text); wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls\\Authored.docx", g_dataDir);
      if (ag_write_new(path, text, AG_DOCX)) { job.text = _wcsdup(text); status = AI_OK; }
      free(text);
    }
  } else if (port == -17 && argc == 7 && job.body) {
    g_aiOn = TRUE; g_aiCtx = 4096; g_aiGpu = TRUE;
    lstrcpynW(g_aiModel, argv[5], MAX_PATH); lstrcpynW(g_aiSrvExe, argv[6], MAX_PATH);
    wchar_t *goal = ai_wide(job.body, -1), content[2000], error[1000];
    BOOL ok = goal && ag_author(goal, L"1. Токарная обработка\n2. Гравировка\n3. Фрезерование\n4. Установка патронов\n5. Измерение размеров", content, 2000, &job.cancel, error, 1000);
    job.text = _wcsdup(ok ? content : error); status = ok ? AI_OK : AI_ERROR;
    if (ok) {
      wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls\\Authored.docx", g_dataDir);
      if (!ag_write_new(path, content, AG_DOCX)) status = AI_ERROR;
    }
    free(goal); ai_shutdown();
  } else if (port == -18 && argc == 7 && job.body) {
    BOOL ok = !ag_file_intent(L"какой вес\\рост") && !ag_file_intent(L"предположи") && !ag_file_intent(L"это по картинке ранее вопрос уточняющий");
    ac_push(0, L"как бы ты описал этого человека");
    BYTE *pixel = (BYTE *)malloc(1); if (pixel) pixel[0] = 0;
    if (g_acN) { g_acTurns[g_acN - 1].png = pixel; g_acTurns[g_acN - 1].pngLen = 1; }
    ac_push(1, L"Описание"); ac_push(0, L"какой вес\\рост");
    ok = ok && ac_picture_context(L"какой вес\\рост") && !ac_web_use(L"какой вес\\рост", TRUE);
    ac_push(1, L"Нет масштаба"); ac_push(0, L"предположи");
    ok = ok && ac_picture_context(L"предположи") && !ac_web_use(L"предположи", TRUE);
    ac_push(1, L"Ответ"); ac_push(0, L"постарайся");
    ok = ok && ac_picture_context(L"постарайся") && !ac_web_use(L"постарайся", TRUE) && !ac_picture_context(L"погода завтра");
    ac_clear_turns(); status = ok ? AI_OK : AI_ERROR;
    job.text = _wcsdup(ok ? L"PASS: weight/backslash not a path; photo follow-ups keep picture and skip search; new topic isolated" : L"FAIL");
  } else if (port == -15 && argc == 7 && job.body) {
    wchar_t query[500];
    BOOL ok = ac_review_query(L"MATERIAL: 40Х\nCHECK_WEB: обработка стали 40Х шлифование\nЧерновик", query);
    ok = ok && !ac_review_query(L"MATERIAL: UNKNOWN\nCHECK_WEB: обработка UNKNOWN\nЧерновик", query);
    ok = ok && !ac_review_query(L"MATERIAL: unknown\nCHECK_WEB: обработка unknown\nЧерновик", query);
    ok = ok && !ac_review_query(L"MATERIAL: 40Х\nCHECK_WEB: HTTPS://example.org/40Х\nЧерновик", query);
    ok = ok && !ac_review_query(L"MATERIAL: 40Х\nCHECK_WEB: обработка стали 45\nЧерновик", query);
    ok = ok && !ac_review_query(L"MATERIAL: 40Х\nCHECK_WEB: NONE\nЧерновик", query);
    ac_push(0, L"Вопрос"); g_acReviewStage = 3; g_acReviewDraft = _wcsdup(L"draft");
    ac_review_finish(L"{\"material\":\"40Х\",\"requirements\":\"не разобраны\",\"operations\":[\"Точение\",\"Фрезерование\",\"Контроль\"],\"unknown\":\"https://invented.example/stock и HTTP://invented.example/price\"}");
    const wchar_t *answer = ac_last_role(1);
    ok = ok && answer && !wcsstr(answer, L"invented") && !g_acReviewStage && !g_acReviewDraft;
    wchar_t formatted[9000];
    ok = ok && !ac_review_format(L"обрыв ответа", formatted);
    ok = ok && !ac_review_format(L"{\"material\":\"40Х\",\"requirements\":\"?\",\"operations\":[\"Точение\",\"Точение\"],\"unknown\":\"?\"}", formatted);
    ac_clear_turns(); g_acReviewStage = 2; g_acReviewDraft = _wcsdup(L"draft"); ac_web_cancel();
    ok = ok && !g_acReviewStage && !g_acReviewDraft && !g_acWebOn;
    status = ok ? AI_OK : AI_ERROR;
    job.text = _wcsdup(ok ? L"PASS: bounded review protocol, unknown/mismatched material and model URLs rejected, output links removed, cancel reset" : L"FAIL");
  } else if (port == -12 && argc == 7 && job.body) {
    BOOL ok = !ac_web_use(L"ы", FALSE) && !ac_web_use(L"Привет!", FALSE) && !ac_web_use(L"спасибо", FALSE);
    ok = ok && ac_web_use(L"какая погода завтра в спб", FALSE);
    ok = ok && ac_web_use(L"привет, какая погода завтра", FALSE);
    ok = ok && !ac_web_use(L"напиши тех процесс, и че тут за материал", TRUE);
    ok = ok && ac_web_use(L"найди в интернете режим для этого материала", TRUE);
    ok = ok && ac_web_use(L"https://docs.python.org/3/ сравни с картинкой", TRUE);
    ok = ok && !ac_web_use(L"без интернета напиши техпроцесс", TRUE) && !ac_web_use(L"не ищи в интернете, объясни формулу", FALSE);
    ac_push(0, L"разбери чертёж");
    BYTE *pixel = (BYTE *)malloc(1); if (pixel) pixel[0] = 0;
    if (g_acN) { g_acTurns[g_acN - 1].png = pixel; g_acTurns[g_acN - 1].pngLen = 1; }
    ok = ok && ac_picture_context(L"разбери чертёж");
    ac_push(1, L"Ответ"); ac_push(0, L"а размеры у этого вала");
    ok = ok && ac_picture_context(L"а размеры у этого вала") && !ac_picture_context(L"погода завтра");
    AiTurn image = {0}; image.text = L"напиши тех процесс, и че тут за материал"; image.png = pixel; image.pngLen = 1;
    wchar_t sys[14000]; ac_sys_build(image.text, NULL, sys, 14000);
    char *body = ai_body_chat(sys, &image, 1, FALSE);
    ok = ok && body && strstr(body, "image_url") && strstr(body, "data:image/png;base64,");
    ok = ok && wcsstr(sys, L"предварительный маршрут") && wcsstr(sys, L"не удалось разобрать");
    free(body); ac_clear_turns();
    status = ok ? AI_OK : AI_ERROR;
    job.text = _wcsdup(ok ? L"PASS: short-chat local; image default local; explicit web allowed; image context and image payload preserved; drawing prompt" : L"FAIL");
  } else if (port == -11 && argc == 7 && job.body) {
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
  } else if ((port == -4 || port == -13 || port == -14 || port == -16) && argc == 7 && job.body) {
    /* Real chat image body + managed tier selection + production launcher.
       The caller supplies an isolated profile containing model hard links. */
    g_aiOn = TRUE; g_aiTier = 1; g_aiCtx = 4096; g_aiGpu = TRUE; ai_out_cap(200);
    if (port == -13 || port == -14 || port == -16) { ai_model_file(1, 0, g_aiModel); ai_out_cap(700); }
    if (port == -14 || port == -16) g_aiCtx = 8192;
    lstrcpynW(g_aiSrvExe, argv[6], MAX_PATH);
    job.agentTask = 0; job.timeout = 180000;
    wchar_t model[MAX_PATH], mm[MAX_PATH];
    if (!ai_pick_model(model, mm) || !ai_vision_ready()) status = AI_NO_VISION;
    else {
      DWORD imageSize = (DWORD)ai_file_size(argv[5]);
      char *raw = imageSize && imageSize <= 16 * 1024 * 1024 ? (char *)malloc(imageSize) : NULL;
      FILE *imageFile = raw ? _wfopen(argv[5], L"rb") : NULL;
      if (!imageFile || fread(raw, 1, imageSize, imageFile) != imageSize) { free(raw); raw = NULL; }
      if (imageFile) fclose(imageFile);
      wchar_t *question = ai_wide(job.body, -1);
      if (!raw || !question) status = AI_ERROR;
      else if (port == -14 || port == -16) {
        WNDCLASSW wc = {0}; wc.lpfnWndProc = aq_hidden_chat; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"CursorPadHiddenReviewTest";
        RegisterClassW(&wc);
        g_acWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
        ac_push(0, question);
        if (g_acN) { g_acTurns[g_acN - 1].png = (BYTE *)raw; g_acTurns[g_acN - 1].pngLen = imageSize; raw = NULL; }
        g_acReviewStage = 1;
        if (port == -16) {
          AiResult *draft = (AiResult *)calloc(1, sizeof(AiResult));
          if (draft) {
            g_acId = draft->id = 123; draft->status = AI_OK;
            draft->text = _wcsdup(L"MATERIAL: 40Х\nCHECK_WEB: механическая обработка стали 40Х вал точение шлифование\nЧерновик: материал 40Х. Ошибочное предположение: масса 12,5 является диаметром. Нужна справочная проверка выбора обработки; режимы не подтверждены.");
            ac_result(draft);
          }
        } else ac_finish_ask(NULL, NULL);
        DWORD deadline = GetTickCount() + 360000;
        while ((g_acId || g_acWebOn) && (LONG)(deadline - GetTickCount()) > 0) {
          MSG message;
          while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
          Sleep(10);
        }
        const wchar_t *answer = ac_last_role(1);
        if (answer) job.text = _wcsdup(answer);
        status = answer && !g_acId && !g_acWebOn ? AI_OK : AI_ERROR;
        if (g_acId) ai_cancel(g_acId);
        ac_web_cancel(); ac_clear_turns(); DestroyWindow(g_acWnd); g_acWnd = NULL;
      }
      else {
        AiTurn turn = {0}; turn.text = question;
        turn.png = (BYTE *)raw; turn.pngLen = (DWORD)ai_file_size(argv[5]);
        wchar_t sys[14000];
        if (port == -13) ac_sys_build(question, NULL, sys, 14000);
        char *body = ai_body_chat(port == -13 ? sys : L"Ответь на вопрос по приложенной картинке. Кратко, по-русски.", &turn, 1, FALSE);
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
