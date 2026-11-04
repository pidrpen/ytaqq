/* ai_ocr.c — вторая локальная модель для чертежей (с 2026.11.4): Xiaomi-OCR-0 (Qwen3.5-0.8B,
   xiaomi-research, Apache 2.0) со своим адаптером чертежей. Обозначение читают обе модели — она и «Средний»:
   совпало — верно, разошлись — в графе её вариант (на заводских листах 10 из 10 против 9), «проверьте» с обоими
   (drawrec.c, dr_vl_des). Строки спецификации она читает в разы быстрее на процессоре (~27 с против ~128 с), но в
   окне «ЭСИ и ТП» путала раздел и портила наименования — пока их читает «Средний»; наименование, материал, лист
   для ТП и чат — тоже он (ai_local.c).
   Своя модель — свой llama-server: поднимается на первом вопросе по чертежу и гаснет сам после минуты без
   вопросов; на видеокарте, если на ней «Средний», не упал — повтор на процессоре. Нет файлов, выключено в
   Настройках или сервер не поднялся — всё читает «Средний», как раньше. Файлы — ai\models\xiaomi-ocr-0:
   модель и проектор с huggingface.co (ревизия закреплена, sha256 сверяется), адаптер — из репозитория
   CursorPad (как адаптеры «Среднего»); качаются сами в фоне, когда «Средний» запускается.
   Подключается после ai_local.c: берёт его скачивание, сеть, HTTP и журнал. */

#define AI_OCR_DIR L"xiaomi-ocr-0"
#define AI_OCR_PORT0 (AI_PORT0 + 50)
#define AI_OCR_IDLE_MS 60000
static const AiFile kAiOcrFiles[2] = {
    {L"prithivMLmods/Xiaomi-OCR-0-GGUF", L"7bbfa072e0f3d70ac722e26fbccef5b9f64b7121", L"Xiaomi-OCR-0.Q6_K.gguf",
     629744384ULL, "6da9896bd738852ec01d463612e463656f43b68e6cfbe09d3b0a3ed6f4ad9fd3"},
    {L"prithivMLmods/Xiaomi-OCR-0-GGUF", L"7bbfa072e0f3d70ac722e26fbccef5b9f64b7121", L"Xiaomi-OCR-0.mmproj-bf16.gguf",
     207346624ULL, "634d6bc95781ebf4a3f06570a59859fa5082d488db540fd474bc6b4061aec90e"}};
/* адаптер — в репозитории CursorPad; пустой коммит — не закреплён, не качается */
static const AiFile kAiOcrLora = {L"pidrpen/ytaqq", L"", L"models/draw-lora-xiaomi-ocr-0.gguf", 12793248ULL,
                                  "17552978a723d2f353c1f16fbba4011baf87bf2930d05b5761cb6f590594486f"};

static SRWLOCK g_ocrLock = SRWLOCK_INIT; /* запуск и остановка сервера */
static CRITICAL_SECTION g_ocrAskCs;      /* вопросы по одному: у сервера одна ячейка */
static volatile LONG g_ocrCsInit, g_ocrDlTried, g_ocrDlBusy, g_ocrIdleOn, g_ocrBusy;
static HANDLE g_ocrProc, g_ocrJob;
static int g_ocrPort;
static char g_ocrKey[40];
static volatile DWORD g_ocrLast; /* GetTickCount последнего вопроса */
static BOOL g_ocrGaveUp;         /* не поднялся в этом сеансе — дальше всё «Среднему», без новых попыток */
static BOOL g_ocrOnCpu;          /* видеокарта не потянула — на процессоре до конца сеанса */

/* 0, 1 — модель и проектор, 2 — адаптер */
static void ai_ocr_file(int i, wchar_t *out) {
  const wchar_t *f = i < 2 ? kAiOcrFiles[i].file : wcsrchr(kAiOcrLora.file, L'/') + 1;
  _snwprintf(out, MAX_PATH, L"%s\\ai\\models\\%s\\%s", g_dataDir, AI_OCR_DIR, f);
  out[MAX_PATH - 1] = 0;
}

/* скачано: модель и проектор — размер и отметка .ok (sha256 сверен при скачивании), адаптер — размер */
static BOOL ai_ocr_have(void) {
  wchar_t p[MAX_PATH], ok[MAX_PATH + 4];
  for (int i = 0; i < 2; i++) {
    ai_ocr_file(i, p);
    _snwprintf(ok, MAX_PATH + 4, L"%s.ok", p);
    if (ai_file_size(p) != kAiOcrFiles[i].size || !ai_file_exists(ok)) return FALSE;
  }
  ai_ocr_file(2, p);
  return ai_file_size(p) == kAiOcrLora.size;
}

/* читать ли ею: в Настройках не выключено, локальный ИИ включён (с чужим сервером или Z.ai — нет) */
static BOOL ai_ocr_on(void) { return g_aiOcrPref != 0 && g_aiOn && !g_aiProvider && !g_ocrGaveUp; }

static DWORD WINAPI ai_ocr_dl_thread(LPVOID p) {
  (void)p;
  HINTERNET ses = ai_net_open();
  wchar_t d[MAX_PATH], f[MAX_PATH], path[600], what[120], ok[MAX_PATH + 4];
  ai_dir(d, L"models");
  CreateDirectoryW(d, NULL);
  ai_dir(d, L"models\\" AI_OCR_DIR);
  CreateDirectoryW(d, NULL);
  BOOL fine = ses != NULL;
  for (int i = 0; fine && i < 2; i++) {
    ai_ocr_file(i, f);
    _snwprintf(ok, MAX_PATH + 4, L"%s.ok", f);
    if (ai_file_size(f) == kAiOcrFiles[i].size && ai_file_exists(ok)) continue;
    _snwprintf(path, 600, L"/%s/resolve/%s/%s", kAiOcrFiles[i].repo, kAiOcrFiles[i].rev, kAiOcrFiles[i].file);
    _snwprintf(what, 120, L"Xiaomi-OCR-0 (часть %d из 2)", i + 1);
    ai_log(L"Модель чтения чертежей: https://huggingface.co%s → %s", path, f);
    fine = ai_fetch(ses, L"huggingface.co", path, f, kAiOcrFiles[i].size, kAiOcrFiles[i].sha, what);
    if (fine) {
      HANDLE h = CreateFileW(ok, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, kAiOcrFiles[i].sha, 64, &w, NULL);
        CloseHandle(h);
      }
    }
  }
  ai_ocr_file(2, f);
  if (fine && kAiOcrLora.rev[0] && ai_file_size(f) != kAiOcrLora.size) {
    AiLoraPin pin = {&kAiOcrLora, L"Адаптер чертежей к Xiaomi-OCR-0"};
    wchar_t why[600], seen[400] = L"";
    lstrcpynW(pin.dest, f, MAX_PATH);
    fine = ai_lora_get(ses, &pin, why, 600, seen);
    if (!fine) ai_log(L"Адаптер к Xiaomi-OCR-0 не скачался (%s) — чертежи читает «Средний»", why);
  }
  if (!ses) ai_log(L"Xiaomi-OCR-0 не скачать: нет WinHTTP (код %lu)", GetLastError());
  else WinHttpCloseHandle(ses);
  ai_log(fine && ai_ocr_have() ? L"Xiaomi-OCR-0 готова: обозначения и спецификации — ею, остальное — «Средний»"
                               : L"Xiaomi-OCR-0 не скачалась — чертежи читает «Средний»; снова — при следующем запуске");
  InterlockedExchange(&g_ocrDlBusy, 0);
  return 0;
}

/* скачать в фоне, если нужно: раз за запуск программы (зовут запуск «Среднего» и галочка в Настройках) */
static void ai_ocr_kick(void) {
  if (g_aiOcrPref == 0 || !g_dataDir[0] || ai_ocr_have() || InterlockedExchange(&g_ocrDlTried, 1)) return;
  InterlockedExchange(&g_ocrDlBusy, 1);
  HANDLE t = CreateThread(NULL, 0, ai_ocr_dl_thread, NULL, 0, NULL);
  if (t) CloseHandle(t);
  else InterlockedExchange(&g_ocrDlBusy, 0);
}

static void ai_ocr_stop_locked(void) {
  if (!g_ocrProc) return;
  TerminateProcess(g_ocrProc, 0);
  WaitForSingleObject(g_ocrProc, 3000);
  CloseHandle(g_ocrProc);
  g_ocrProc = NULL;
  g_ocrPort = 0;
}

static void ai_ocr_stop(void) {
  AcquireSRWLockExclusive(&g_ocrLock);
  if (g_ocrProc) ai_log(L"Xiaomi-OCR-0: сервер остановлен");
  ai_ocr_stop_locked();
  ReleaseSRWLockExclusive(&g_ocrLock);
}

/* минута без вопросов — сервер прочь: память нужна «Среднему» и программам человека */
static DWORD WINAPI ai_ocr_idle_thread(LPVOID p) {
  (void)p;
  for (;;) {
    Sleep(5000);
    if (g_ocrProc && !g_ocrBusy && GetTickCount() - g_ocrLast > AI_OCR_IDLE_MS) ai_ocr_stop();
  }
  return 0;
}

static BOOL ai_ocr_alive(void) { return g_ocrProc && WaitForSingleObject(g_ocrProc, 0) == WAIT_TIMEOUT && g_ocrPort; }

/* поднять сервер (под g_ocrLock); TRUE — отвечает на /health */
static BOOL ai_ocr_start_locked(void) {
  if (ai_ocr_alive()) return TRUE;
  ai_ocr_stop_locked();
  wchar_t exe[MAX_PATH], model[MAX_PATH], mm[MAX_PATH], lora[MAX_PATH], log[MAX_PATH], d[MAX_PATH];
  BOOL gpu = g_aiGpu && !g_ocrOnCpu;
  if (!ai_server_exe(gpu, exe) && !(gpu && (gpu = FALSE, ai_server_exe(FALSE, exe)))) {
    ai_log(L"Xiaomi-OCR-0: нет llama-server.exe — чертежи читает «Средний»");
    return FALSE;
  }
  ai_ocr_file(0, model), ai_ocr_file(1, mm), ai_ocr_file(2, lora);
  ai_short_path(model), ai_short_path(mm), ai_short_path(lora);
  ai_dir(d, NULL);
  _snwprintf(log, MAX_PATH, L"%s\\server-ocr.log", d);
  if (!g_ocrJob) {
    g_ocrJob = CreateJobObjectW(NULL, NULL);
    if (g_ocrJob) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
      memset(&li, 0, sizeof(li));
      li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(g_ocrJob, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }
  }
  { /* свой ключ: чужие программы не спросят и эту модель */
    HCRYPTPROV prov = 0;
    BYTE r[16] = {0};
    if (CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
      CryptGenRandom(prov, sizeof(r), r);
      CryptReleaseContext(prov, 0);
    } else {
      LARGE_INTEGER q;
      QueryPerformanceCounter(&q);
      memcpy(r, &q, sizeof(q));
    }
    for (int i = 0; i < 16; i++) snprintf(g_ocrKey + i * 2, 3, "%02x", r[i]);
  }
  for (int attempt = 0; attempt < 4; attempt++) {
    int port = AI_OCR_PORT0 + attempt * 3;
    wchar_t cmd[MAX_PATH * 4 + 400], wkey[40];
    MultiByteToWideChar(CP_UTF8, 0, g_ocrKey, -1, wkey, 40);
    /* как у «Среднего»: размышление выключено, картинка — от 1024 токенов; адаптер — на всё: модель только для
       чертежей. Одна ячейка, 8192 токенов — страница спецификации с ответом помещается */
    _snwprintf(cmd, MAX_PATH * 4 + 400,
               L"\"%s\" -m \"%s\" --mmproj \"%s\"%s --lora \"%s\" --host 127.0.0.1 --port %d --api-key %s -c 8192 -np 1 "
               L"-t %d -ngl %s --jinja --chat-template-kwargs \"{\\\"enable_thinking\\\":false}\" --image-min-tokens 1024",
               exe, model, mm, gpu ? L"" : L" --no-mmproj-offload", lora, port, wkey, ai_cores(), gpu ? L"auto" : L"0");
    cmd[MAX_PATH * 4 + 399] = 0;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE lf = CreateFileW(log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (lf != INVALID_HANDLE_VALUE) si.dwFlags = STARTF_USESTDHANDLES, si.hStdOutput = si.hStdError = lf;
    wchar_t exeDir[MAX_PATH];
    lstrcpynW(exeDir, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exeDir, L'\\');
    if (sl) *sl = 0;
    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, lf != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL,
                             exeDir, &si, &pi);
    if (lf != INVALID_HANDLE_VALUE) CloseHandle(lf);
    if (!ok) {
      ai_log(L"Xiaomi-OCR-0: llama-server не запустился (код %lu) — чертежи читает «Средний»", GetLastError());
      return FALSE;
    }
    if (g_ocrJob) AssignProcessToJobObject(g_ocrJob, pi.hProcess);
    SetPriorityClass(pi.hProcess, BELOW_NORMAL_PRIORITY_CLASS);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    g_ocrProc = pi.hProcess;
    g_ocrPort = port;
    ai_log(L"Xiaomi-OCR-0: запуск сервера, порт %d, %s", port, gpu ? L"видеокарта" : L"процессор");
    DWORD t0 = GetTickCount();
    BOOL up = FALSE, died = FALSE;
    while (!up && !died && GetTickCount() - t0 < 120000) {
      died = WaitForSingleObject(g_ocrProc, 300) == WAIT_OBJECT_0;
      up = !died && ai_http(L"GET", port, L"/health", NULL, g_ocrKey, 2000, NULL, NULL, NULL) == 200;
    }
    if (up) {
      ai_log(L"Xiaomi-OCR-0 готова за %.1f с", (GetTickCount() - t0) / 1000.0);
      if (!InterlockedExchange(&g_ocrIdleOn, 1)) {
        HANDLE t = CreateThread(NULL, 0, ai_ocr_idle_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
      }
      return TRUE;
    }
    ai_ocr_stop_locked();
    if (died && gpu) { /* заводской ПК: Vulkan падает — тогда процессор, до конца сеанса */
      gpu = FALSE, g_ocrOnCpu = TRUE;
      ai_log(L"Xiaomi-OCR-0: на видеокарте не поднялась — повтор на процессоре");
      if (!ai_server_exe(FALSE, exe)) ai_server_exe(TRUE, exe);
      continue;
    }
    if (!died) break; /* не ответила за 2 мин — не тот случай, чтобы пробовать порты */
  }
  ai_log(L"Xiaomi-OCR-0 не поднялась (журнал: %s) — до конца сеанса чертежи читает «Средний»", log);
  g_ocrGaveUp = TRUE;
  return FALSE;
}

/* вопрос по картинке чертежа второй модели — как ai_ask_image_sync_ex, но без потока: ответ целиком.
   AI_NO_MODEL — её нет, она выключена или не поднялась: спрашивать «Средний» */
static int ai_ocr_ask(const wchar_t *question, const wchar_t *imagePath, const char *schema, int maxTokens,
                      DWORD timeoutMs, wchar_t **answer) {
  *answer = NULL;
  if (GetCurrentThreadId() == g_aiUiThread) return AI_BUSY;
  if (!ai_ocr_on() || !ai_ocr_have()) return AI_NO_MODEL;
  if (!InterlockedExchange(&g_ocrCsInit, 1)) InitializeCriticalSection(&g_ocrAskCs);
  HANDLE h = CreateFileW(imagePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return AI_ERROR;
  DWORD sz = GetFileSize(h, NULL), got = 0;
  BYTE *data = sz && sz < 32u * 1024 * 1024 ? (BYTE *)malloc(sz) : NULL;
  BOOL rd = data && ReadFile(h, data, sz, &got, NULL) && got == sz;
  CloseHandle(h);
  if (!rd) {
    free(data);
    return AI_ERROR;
  }
  const wchar_t *ext = wcsrchr(imagePath, L'.');
  const char *mime = ext && !_wcsicmp(ext, L".bmp") ? "image/bmp" : ext && (!_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg")) ? "image/jpeg" : "image/png";
  char *b64 = ai_b64(data, sz), *q = ai_json_str(question);
  free(data);
  size_t n = (b64 ? strlen(b64) : 0) + (q ? strlen(q) : 0) + (schema ? strlen(schema) : 0) + 600;
  char *body = b64 && q ? (char *)malloc(n) : NULL;
  if (body) {
    int k = snprintf(body, n,
                     "{\"model\":\"ocr\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"image_url\",\"image_url\":"
                     "{\"url\":\"data:%s;base64,%s\"}},{\"type\":\"text\",\"text\":%s}]}],\"temperature\":0,\"max_tokens\":%d,"
                     "\"stream\":false,\"cache_prompt\":false,\"chat_template_kwargs\":{\"enable_thinking\":false}",
                     mime, b64, q, maxTokens);
    if (schema)
      k += snprintf(body + k, n - k, ",\"response_format\":{\"type\":\"json_schema\",\"json_schema\":{\"name\":\"r\",\"strict\":true,\"schema\":%s}}", schema);
    snprintf(body + k, n - k, "}");
  }
  free(b64);
  free(q);
  if (!body) return AI_BUSY;
  int rc = AI_ERROR;
  EnterCriticalSection(&g_ocrAskCs);
  InterlockedIncrement(&g_ocrBusy);
  AcquireSRWLockExclusive(&g_ocrLock);
  BOOL up = ai_ocr_start_locked();
  int port = g_ocrPort;
  ReleaseSRWLockExclusive(&g_ocrLock);
  if (up) {
    char *resp = NULL;
    DWORD st = ai_http(L"POST", port, L"/v1/chat/completions", body, g_ocrKey, timeoutMs ? timeoutMs : 240000, NULL, NULL, &resp);
    const char *c = st == 200 && resp ? json_find_string(resp, "content") : NULL;
    *answer = c ? ai_json_unstr(c) : NULL;
    rc = *answer ? AI_OK : AI_ERROR;
    if (!*answer) ai_log(L"Xiaomi-OCR-0: ответа нет (HTTP %lu) — спрошу «Средний»", st);
    free(resp);
  } else {
    rc = AI_NO_MODEL;
  }
  g_ocrLast = GetTickCount();
  InterlockedDecrement(&g_ocrBusy);
  LeaveCriticalSection(&g_ocrAskCs);
  free(body);
  return rc;
}
