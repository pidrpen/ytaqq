/* Observe -> choose tool -> observe -> review. This is application code shipped
   in CursorPad.exe, independent of downloaded GGUF files and llama.cpp. */
#include <wctype.h>
#include "ai_agent_fs.c"
#include "ai_app_tools.c"
static AgFs g_agFs;
static int g_agSteps, g_agErrors;
static wchar_t g_agNotes[AG_TEXT], g_agObservation[6000];
static wchar_t g_agReadout[100000];
typedef struct {
  AgFs fs;
  wchar_t json[6000], observation[6000];
  int state, applying;
  LONG generation;
  volatile LONG cancel;
  HWND notify;
} AgToolJob;
static AgToolJob *g_agTool;
static LONG g_agGeneration;
static BOOL g_agApplying;

#include "ai_agent_prompt.h"

#include "ai_agent_route.h"
static BOOL ag_wants(const wchar_t *text) {
  if (g_acLastDir[0]) {
    wchar_t q[400]; lstrcpynW(q, text, 400); CharLowerBuffW(q, (DWORD)wcslen(q));
    if (wcsstr(q, L"что там") || wcsstr(q, L"что в ней") || wcsstr(q, L"а там") || wcsstr(q, L"сколько там")) return TRUE;
  }
  return g_agPending || ag_file_intent(text) || ac_is_file_cmd(text);
}

static void ag_note(const wchar_t *tool, const wchar_t *observation) {
  size_t n = wcslen(g_agNotes);
  size_t need = wcslen(tool) + wcslen(observation) + 40;
  if (need >= AG_TEXT) return;
  if (n + need >= AG_TEXT) {
    size_t drop = n + need - AG_TEXT + 1;
    while (drop < n && g_agNotes[drop] != L'\n') drop++;
    memmove(g_agNotes, g_agNotes + drop, (n - drop + 1) * sizeof(wchar_t));
    n -= drop;
  }
  swprintf(g_agNotes + n, AG_TEXT - n, L"\nВызов: %ls\nРезультат: %ls\n", tool, observation);
}

static void ag_stop(void) {
  if (g_agTool) InterlockedExchange(&g_agTool->cancel, 1);
  g_agTool = NULL;
  g_agGeneration++;
  g_agActive = g_agReview = g_agPending = FALSE;
  g_agFs.count = 0;
}

static BOOL ag_request(void) {
  if (++g_agSteps > 16) {
    ag_stop(); ac_busy(FALSE);
    af_say(L"Достигнут предел 16 шагов. Ничего не изменено. Уточните или сократите задачу.");
    return FALSE;
  }
  int budget = ai_chat_budget() - (int)wcslen(kAgSystem) - 1000;
  if (budget < 1500) {
    ag_stop(); af_say(L"Для составных задач установите контекст 16384 на вкладке ИИ."); return FALSE;
  }
  int cap = 24000;
  wchar_t *prompt = (wchar_t *)calloc(cap, sizeof(wchar_t));
  if (!prompt) { ag_stop(); return FALSE; }
  swprintf(prompt, cap,
    L"Задача пользователя: %ls\nПапки: @desktop=%ls; @downloads=%ls; @documents=%ls; @last=%ls.\n"
    L"Шаг %d из 16. Действий в плане: %d.\n",
    g_agFs.goal, g_agFs.desktop, g_agFs.downloads, g_agFs.documents, g_agFs.last, g_agSteps, g_agFs.count);
  for (int i = 0; i < g_agFs.count && i < 12 && wcslen(prompt) + MAX_PATH * 2 + 80 < (size_t)cap; i++) {
    AgAction *a = &g_agFs.plan[i];
    swprintf(prompt + wcslen(prompt), cap - wcslen(prompt), L"В плане: %ls %ls -> %ls\n",
      a->kind == AG_DIR ? L"mkdir" : a->kind == AG_COPY ? L"copy" : a->kind == AG_MOVE ? L"move" : a->kind == AG_OPEN ? L"open" : a->kind == AG_EXCEL ? L"excel_write" : (a->kind == AG_DOCX || a->kind == AG_PLAIN || a->kind == AG_XLSX) ? L"write" : L"rename", a->src, a->dst);
  }
  if (g_agFs.count > 12) wcscat(prompt, L"Остальные действия той же группы скрыты для экономии контекста; ориентируйся на результаты инструментов.\n");
  /* Keep the newest observations within the selected model's context. */
  int room = budget - (int)wcslen(prompt);
  if (room > cap - (int)wcslen(prompt) - 1) room = cap - (int)wcslen(prompt) - 1;
  if (room > 0) {
    size_t n = wcslen(g_agNotes);
    const wchar_t *start = g_agNotes + (n > (size_t)room ? n - room : 0);
    wcsncat(prompt, start, room);
  }
  AiTurn turn = {0, prompt, NULL, 0};
  ai_out_cap(512);
  g_acId = ai_ask_files_async(kAgSystem, &turn, 1, g_acWnd, WM_AI_CHAT);
  ai_out_cap(0); free(prompt);
  if (!g_acId) { ag_stop(); ac_busy(FALSE); af_say(L"Не удалось запустить следующий шаг ИИ. Диск не изменён."); return FALSE; }
  ac_busy(TRUE);
  wchar_t status[160]; swprintf(status, 160, L"Проверяю файлы и составляю план · шаг %d из 16 · Стоп — отменить", g_agSteps);
  ac_status(status); return TRUE;
}

static BOOL ag_start(const wchar_t *text) {
  wchar_t goal[4000];
  if (g_agPending) {
    if (wcslen(g_agFs.goal) + wcslen(text) + 40 >= 4000) {
      ag_stop(); af_say(L"Уточнений слишком много. Напишите задачу заново одним сообщением."); return TRUE;
    }
    swprintf(goal, 4000, L"%ls\nУточнение пользователя: %ls", g_agFs.goal, text);
  } else {
    if (wcslen(text) >= 4000) { af_say(L"Сократите файловую задачу до 4000 символов."); return TRUE; }
    lstrcpynW(goal, text, 4000);
  }
  ag_fs_init(&g_agFs, g_dataDir, g_acLastDir);
  lstrcpynW(g_agFs.goal, goal, 4000);
  g_agNotes[0] = g_agReadout[0] = 0; g_agSteps = g_agErrors = 0;
  g_agGeneration++;
  g_agPending = g_agReview = FALSE; g_agActive = TRUE;
  ac_push(0, text); ac_redraw();
  ag_request(); return TRUE;
}

static void ag_preview(void) {
  size_t cap = (size_t)g_agFs.count * (MAX_PATH * 2 + 2400) + 512;
  wchar_t *out = (wchar_t *)calloc(cap, sizeof(wchar_t));
  if (!out) { ag_stop(); af_say(L"Не хватило памяти для плана. Диск не изменён."); return; }
  swprintf(out, cap, L"План действий: %d. Диск пока не изменён.\r\n", g_agFs.count);
  for (int i = 0; i < g_agFs.count; i++) {
    AgAction *a = &g_agFs.plan[i];
    const wchar_t *verb = a->kind == AG_DIR ? L"Создать папку" : a->kind == AG_COPY ? L"Копировать" : a->kind == AG_MOVE ? L"Переместить" : a->kind == AG_OPEN ? L"Открыть в проводнике" : a->kind == AG_DOCX ? L"Создать Word-документ" : a->kind == AG_XLSX ? L"Создать пустую книгу Excel" : a->kind == AG_EXCEL ? L"Заполнить новую книгу/копию Excel" : a->kind == AG_PLAIN ? L"Создать текстовый файл" : L"Переименовать";
    swprintf(out + wcslen(out), cap - wcslen(out), L"%ls: %ls%ls%ls\r\n", verb, a->src, a->src[0] ? L" → " : L"", a->dst);
    if (a->kind == AG_EXCEL || a->kind == AG_DOCX || a->kind == AG_PLAIN) {
      swprintf(out + wcslen(out), cap - wcslen(out), L"%ls%ls%ls\r\nСодержимое: %ls\r\n", a->kind == AG_EXCEL ? L"Лист: " : L"", a->sheet, a->formulas ? L"; ФОРМУЛЫ ВКЛЮЧЕНЫ" : L"", a->text[0] ? a->text : L"(пусто)");
    }
  }
  ac_push(1, out); free(out);
  ac_push(1, L"Проверьте весь план. «Применить» выполнит его, «Отмена» оставит файлы на месте.");
  g_agReview = TRUE; g_acPlanOn = TRUE; ac_plan_ui(TRUE); ac_redraw();
}

static void ag_tool_ready(void *result) {
  AgToolJob *job = (AgToolJob *)result;
  if (job->generation != g_agGeneration || !g_agActive) { free(job); return; }
  g_agTool = NULL;
  if (job->applying) {
    if (job->state == 1 && job->fs.count) {
      af_set_last(job->fs.last);
    }
    lstrcpynW(g_agObservation, job->observation, 6000);
    free(job); g_agApplying = FALSE;
    ag_stop(); ac_busy(FALSE); af_say(g_agObservation); ac_status(L""); return;
  }
  g_agFs = job->fs; g_agFs.cancel = NULL;
  int state = job->state;
  lstrcpynW(g_agObservation, job->observation, 6000);
  ag_note(job->json, job->observation);
  AgJson parsed;
  if (state == 0 && ag_json(job->json, &parsed)) {
    const wchar_t *tool = ag_get(&parsed, L"tool");
    if (!wcscmp(tool, L"list") || !wcscmp(tool, L"find") || !wcscmp(tool, L"read") || !wcscmp(tool, L"office_read") || !wcscmp(tool, L"compare_materials") || !wcscmp(tool, L"app")) {
      wchar_t actual[MAX_PATH];
      const wchar_t *input = ag_get(&parsed, L"path"); if (!input[0]) input = ag_get(&parsed, L"src");
      if (ag_path(&g_agFs, input, actual) && ag_dir(actual)) {
        af_set_last(actual); lstrcpynW(g_agFs.last, actual, MAX_PATH);
      }
      if (wcslen(g_agReadout) + wcslen(job->observation) + 4 >= 100000) g_agReadout[0] = 0;
      wcscat(g_agReadout, job->observation); wcscat(g_agReadout, L"\r\n\r\n");
    }
  }
  free(job); ac_busy(FALSE);
  if (state == 1) {
    g_agActive = FALSE;
    if (g_agFs.count) ag_preview();
    else { af_say(g_agReadout[0] ? g_agReadout : L"План пуст. Ничего не изменено."); }
    ac_status(L""); return;
  }
  if (state == 2) {
    g_agActive = FALSE; g_agPending = TRUE; g_agFs.count = 0;
    af_say(g_agObservation); ac_status(L"Ответьте на уточнение в поле ввода"); return;
  }
  if (state < 0 && ++g_agErrors >= 3) {
    ag_stop(); af_say(L"ИИ трижды предложил некорректное действие. Диск не изменён. Уточните задачу и папки."); return;
  }
  ag_request();
}

static DWORD WINAPI ag_tool_thread(LPVOID param) {
  AgToolJob *job = (AgToolJob *)param;
  job->fs.cancel = &job->cancel;
  job->state = job->applying ? (ag_apply(&job->fs, job->observation, 6000) ? 1 : -1)
                             : ag_tool(&job->fs, job->json, job->observation, 6000);
  if (!PostMessageW(job->notify, WM_AG_TOOL, 0, (LPARAM)job)) free(job);
  return 0;
}

static void ag_result(AiResult *r) {
  g_acId = 0;
  if (r->status != AI_OK) {
    ag_stop(); ac_busy(FALSE);
    af_say(r->status == AI_CANCELLED ? L"Остановлено. Диск не изменён." : ai_status_text(r->status)); return;
  }
  AgJson appJson;
  if (ag_json(r->text, &appJson) && !wcscmp(ag_get(&appJson, L"tool"), L"app")) {
    int state = ag_app_call(&appJson, g_agObservation, 6000);
    ag_note(r->text, g_agObservation);
    if (state == 0 && wcslen(g_agReadout) + wcslen(g_agObservation) + 4 < 100000) {
      wcscat(g_agReadout, g_agObservation); wcscat(g_agReadout, L"\r\n\r\n");
    }
    if (state < 0 && ++g_agErrors >= 3) { ag_stop(); ac_busy(FALSE); af_say(L"Не удалось выбрать доступную команду CursorPad. Ничего не изменено."); return; }
    ag_request(); return;
  }
  AgToolJob *job = (AgToolJob *)calloc(1, sizeof(AgToolJob));
  if (!job) { ag_stop(); ac_busy(FALSE); af_say(L"Не хватило памяти. Диск не изменён."); return; }
  job->fs = g_agFs; job->generation = g_agGeneration; job->notify = g_acWnd;
  lstrcpynW(job->json, r->text, 6000);
  g_agTool = job;
  HANDLE thread = CreateThread(NULL, 0, ag_tool_thread, job, 0, NULL);
  if (thread) CloseHandle(thread);
  else {
    g_agTool = NULL; free(job); ag_stop(); ac_busy(FALSE); af_say(L"Не удалось запустить проверку файлов. Диск не изменён.");
  }
}

static void ag_cancel_work(void) {
  if (g_agApplying && g_agTool) {
    InterlockedExchange(&g_agTool->cancel, 1);
    ac_status(L"Останавливаю выполнение после текущей операции…"); return;
  }
  ag_stop(); ac_busy(FALSE); af_say(L"Остановлено. Диск не изменён.");
}

static void ag_apply_review(void) {
  AgToolJob *job = (AgToolJob *)calloc(1, sizeof(AgToolJob));
  if (!job) { af_say(L"Не хватило памяти для выполнения. План сохранён."); return; }
  job->fs = g_agFs; job->generation = g_agGeneration; job->notify = g_acWnd; job->applying = 1;
  g_agTool = job;
  HANDLE thread = CreateThread(NULL, 0, ag_tool_thread, job, 0, NULL);
  if (!thread) { g_agTool = NULL; free(job); af_say(L"Не удалось начать выполнение. План сохранён."); return; }
  CloseHandle(thread);
  g_agApplying = g_agActive = TRUE; g_agReview = FALSE;
  g_acPlanOn = FALSE; ac_plan_ui(FALSE); ac_busy(TRUE);
  ac_status(L"Выполняю план… Стоп — остановить после текущей операции");
}

static BOOL ag_can_clear(void) {
  if (!g_agApplying) return TRUE;
  ac_status(L"Сначала дождитесь выполнения или нажмите Стоп."); return FALSE;
}

#ifdef AGENT_LAB
/* Isolated GUI harness: no main app startup, hooks, updates or user settings.
   Build with -DAGENT_LAB; it is absent from the release executable. */
static int ag_lab(HINSTANCE inst, const wchar_t *scratch) {
  g_inst = inst;
  lstrcpynW(g_dataDir, scratch, MAX_PATH);
  CreateDirectoryW(scratch, NULL);
  g_aiOn = TRUE; g_aiCtx = 16384; g_aiUiThread = GetCurrentThreadId();
  lstrcpynW(g_aiExtHost, L"127.0.0.1", 64); g_aiExtPort = 39285;
  g_fontUi = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
  g_fontBody = g_fontUi;
  aichat_show();
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return 0;
}
#endif
