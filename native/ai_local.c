/* ai_local.c — локальный ИИ: llama.cpp (llama-server.exe) на 127.0.0.1.
   Интерфейс и правила — в ai.h. Здесь: настройки (ai.txt в папке данных),
   какие модели качать, запуск и остановка сервера, запросы к нему и
   скачивание модели по кнопке. Окно настроек — ai_ui.c.

   Почему отдельный процесс, а не библиотека внутри exe: llama.cpp — это C++
   и десятки мегабайт под разные процессоры и видеокарты; в exe он утяжелил бы
   каждое обновление, а упавшая модель уронила бы весь CursorPad вместе с
   курсором. Сервер запускается с CREATE_NO_WINDOW в задании Windows (job),
   которое убивает его вместе с CursorPad, и гасится после 10 минут без
   запросов, чтобы не держать 2–5 ГБ памяти. */
#include "ai.h"

#define AI_IDLE_STOP_MS (10 * 60 * 1000)
#ifndef AI_PORT0 /* isolated native integration tests may choose another port */
#define AI_PORT0 39281
#endif
#define WM_AI_UI (WM_APP + 70)   /* в окно Настроек: обновить вкладку «ИИ» */
#define WM_AI_TEST (WM_APP + 71) /* в окно Настроек: ответ кнопки «Проверить» */

/* --- что качать ------------------------------------------------------------
   Модели — с huggingface.co, ревизия закреплена: файл по этой ссылке не
   подменить, а размер и sha256 сверяются после скачивания. Все — Apache 2.0. */
typedef struct {
  const wchar_t *repo, *rev, *file;
  ULONGLONG size;
  const char *sha;
} AiFile;

typedef struct {
  const wchar_t *button; /* на кнопке уровня */
  const wchar_t *model;  /* название модели для людей */
  const wchar_t *dir;    /* подпапка в ai\models */
  const wchar_t *hint;   /* для какого компьютера */
  int nf;                /* файлов модели; mmproj — последний, если vision */
  BOOL vision;
  AiFile f[2];
} AiTier;

#define AI_TIERS 4
static const AiTier kAiTiers[AI_TIERS] = {
    {L"Слабый", L"Qwen2.5 1.5B", L"qwen2.5-1.5b", L"8 ГБ памяти, без видеокарты", 1, FALSE,
     {{L"Qwen/Qwen2.5-1.5B-Instruct-GGUF", L"91cad51170dc346986eccefdc2dd33a9da36ead9",
       L"qwen2.5-1.5b-instruct-q4_k_m.gguf", 1117320736ULL,
       "6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e"}}},
    {L"Средний", L"Qwen3.5 4B · текст и картинки", L"qwen3.5-4b", L"16 ГБ памяти; текст и картинки, Vulkan или процессор", 2, TRUE,
     {{L"unsloth/Qwen3.5-4B-GGUF", L"e87f176479d0855a907a41277aca2f8ee7a09523",
       L"Qwen3.5-4B-Q4_K_M.gguf", 2740937888ULL,
       "00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4"},
      {L"unsloth/Qwen3.5-4B-GGUF", L"e87f176479d0855a907a41277aca2f8ee7a09523",
       L"mmproj-F16.gguf", 672423616ULL,
       "cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864"}}},
    {L"Мощный", L"Qwen3 8B · текст", L"qwen3-8b", L"Только текст; 16–32 ГБ памяти, для полного GPU — от 8 ГБ VRAM", 1, FALSE,
     {{L"Qwen/Qwen3-8B-GGUF", L"7c41481f57cb95916b40956ab2f0b139b296d974",
       L"Qwen3-8B-Q4_K_M.gguf", 5027783488ULL,
       "d98cdcbd03e17ce47681435b5150e34c1417f50b5c0019dd560e4882c5745785"}}},
    {L"С картинками", L"Qwen3-VL 4B", L"qwen3-vl-4b", L"16 ГБ; читает и картинки, медленнее", 2, TRUE,
     {{L"unsloth/Qwen3-VL-4B-Instruct-GGUF", L"00c00da0690c4b14b5539b02c4ea5d7c9102b35e",
       L"Qwen3-VL-4B-Instruct-Q4_K_M.gguf", 2497282336ULL,
       "d4dcd426bfba75752a312b266b80fec8136fbaca13c62d93b7ac41fa67f0492b"},
      {L"unsloth/Qwen3-VL-4B-Instruct-GGUF", L"00c00da0690c4b14b5539b02c4ea5d7c9102b35e",
       L"mmproj-F16.gguf", 836180640ULL,
       "1b9f4e92f0fbda14d7d7b58baed86039b8a980fe503d9d6a9393f25c0028f1fc"}}},
};

/* Адаптер чертежей к «Среднему» (дообучен читать штамп и лист, native/train) — в репозитории
   CursorPad, коммит закреплён, размер и sha256 сверяются. Качается сам, в фоне, и включается
   при следующем запуске модели, только для чертежей (draw-lora*.gguf, ai_server_ensure). */
#define AI_DRAW_LORA_TIER 1
static const AiFile kAiDrawLora = {L"pidrpen/ytaqq", L"8ecdf3739eaa1647b638e9e4cf6b520b101ab543",
                                   L"models/draw-lora-qwen3.5-4b.gguf", 42485376ULL,
                                   "1772624ec0b9115d88671971e2bb4e073a59bacd560549f48fa28bd98250e4d5"};
static void ai_lora_kick(const wchar_t *model);

/* --- настройки ------------------------------------------------------------ */
static volatile LONG g_aiOn;         /* по умолчанию выключен; виден рабочим потокам */
static int g_aiTier = 1;             /* «Средний» */
static int g_aiCtx = 8192;           /* окно контекста модели, токенов; на каждую из двух ячеек сервера — половина */
static BOOL g_aiGpu;                 /* llama.cpp со сборкой Vulkan и слоями на видеокарте */
static wchar_t g_aiModel[MAX_PATH];  /* своя модель .gguf; пусто — модель уровня */
static wchar_t g_aiSrvExe[MAX_PATH]; /* llama-server.exe, указанный руками; пусто — скачанный */
static wchar_t g_aiExtHost[64];      /* свой сервер (Ollama…): адрес; пусто — свой llama-server */
static BOOL g_aiDrawLora;            /* свой llama-server запущен с адаптером для чертежей (draw-lora*.gguf) */
static int g_aiExtPort;
static char g_aiExtModel[64];        /* имя модели для чужого сервера */
static DWORD g_aiUiThread;           /* поток интерфейса — синхронные вызовы из него запрещены */
#include "ai_zai.c"

/* журнал ИИ: что качали, что запускали, чем кончилось — для «Скопировать отчёт» */
static wchar_t g_aiLog[12000];
static SRWLOCK g_aiLogLock = SRWLOCK_INIT;

static void ai_log(const wchar_t *fmt, ...) {
  wchar_t line[1200];
  SYSTEMTIME t;
  GetLocalTime(&t);
  int n = _snwprintf(line, 1200, L"%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(line + n, 1200 - n - 3, fmt, ap);
  va_end(ap);
  line[1196] = 0;
  wcscat(line, L"\r\n");
  AcquireSRWLockExclusive(&g_aiLogLock);
  size_t have = wcslen(g_aiLog), add = wcslen(line), cap = sizeof(g_aiLog) / sizeof(g_aiLog[0]);
  if (have + add + 1 > cap) { /* старое — вон, свежее важнее */
    size_t drop = have + add + 1 - cap + cap / 4;
    if (drop > have) drop = have;
    memmove(g_aiLog, g_aiLog + drop, (have - drop + 1) * sizeof(wchar_t));
  }
  wcscat(g_aiLog, line);
  ReleaseSRWLockExclusive(&g_aiLogLock);
}

static void ai_dir(wchar_t *out, const wchar_t *sub) {
  if (sub && sub[0]) _snwprintf(out, MAX_PATH, L"%s\\ai\\%s", g_dataDir, sub);
  else _snwprintf(out, MAX_PATH, L"%s\\ai", g_dataDir);
  out[MAX_PATH - 1] = 0;
}

static void ai_model_file(int tier, int i, wchar_t *out) {
  _snwprintf(out, MAX_PATH, L"%s\\ai\\models\\%s\\%s", g_dataDir, kAiTiers[tier].dir, kAiTiers[tier].f[i].file);
  out[MAX_PATH - 1] = 0;
}

static BOOL ai_file_exists(const wchar_t *p) {
  DWORD a = GetFileAttributesW(p);
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static ULONGLONG ai_file_size(const wchar_t *p) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(p, GetFileExInfoStandard, &d)) return 0;
  return ((ULONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

/* файл модели уровня скачан и проверен: рядом лежит отметка .ok (sha256 считали
   при скачивании; пересчитывать 2,5 ГБ на каждом запуске — десятки секунд) */
static BOOL ai_tier_file_ok(int tier, int i) {
  wchar_t p[MAX_PATH], ok[MAX_PATH + 4];
  ai_model_file(tier, i, p);
  _snwprintf(ok, MAX_PATH + 4, L"%s.ok", p);
  return ai_file_size(p) == kAiTiers[tier].f[i].size && ai_file_exists(ok);
}

static BOOL ai_tier_ready(int tier) {
  for (int i = 0; i < kAiTiers[tier].nf; i++)
    if (!ai_tier_file_ok(tier, i)) return FALSE;
  return TRUE;
}

/* llama-server.exe: путь записан в ai\llama\server.txt (или ai\llama-vk\…) после распаковки */
static BOOL ai_server_exe(BOOL gpu, wchar_t *out) {
  wchar_t d[MAX_PATH], p[MAX_PATH];
  if (g_aiSrvExe[0] && ai_file_exists(g_aiSrvExe)) {
    lstrcpynW(out, g_aiSrvExe, MAX_PATH);
    return TRUE;
  }
  ai_dir(d, gpu ? L"llama-vk" : L"llama");
  _snwprintf(p, MAX_PATH, L"%s\\llama-server.exe", d);
  if (ai_file_exists(p)) {
    lstrcpynW(out, p, MAX_PATH);
    return TRUE;
  }
  /* в архиве он мог лежать во вложенной папке */
  WIN32_FIND_DATAW fd;
  wchar_t mask[MAX_PATH];
  _snwprintf(mask, MAX_PATH, L"%s\\*", d);
  HANDLE h = FindFirstFileW(mask, &fd);
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  BOOL found = FALSE;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
    _snwprintf(p, MAX_PATH, L"%s\\%s\\llama-server.exe", d, fd.cFileName);
    if (ai_file_exists(p)) {
      lstrcpynW(out, p, MAX_PATH);
      found = TRUE;
    }
  } while (!found && FindNextFileW(h, &fd));
  FindClose(h);
  return found;
}

/* файл по маске рядом с моделью (в той же папке) — в out полный путь */
static BOOL ai_find_near(const wchar_t *model, const wchar_t *mask, wchar_t *out) {
  out[0] = 0;
  wchar_t m[MAX_PATH];
  lstrcpynW(m, model, MAX_PATH);
  wchar_t *sl = wcsrchr(m, L'\\');
  if (!sl || (size_t)(sl - m) + wcslen(mask) + 2 >= MAX_PATH) return FALSE;
  wcscpy(sl + 1, mask);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(m, &fd);
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  *sl = 0;
  _snwprintf(out, MAX_PATH, L"%s\\%s", m, fd.cFileName);
  out[MAX_PATH - 1] = 0;
  FindClose(h);
  return TRUE;
}

/* какую модель запускать: своя или уровня; mm — проектор картинок или пусто */
static BOOL ai_pick_model(wchar_t *model, wchar_t *mm) {
  mm[0] = 0;
  if (g_aiModel[0]) {
    if (!ai_file_exists(g_aiModel)) return FALSE;
    lstrcpynW(model, g_aiModel, MAX_PATH);
    ai_find_near(g_aiModel, L"mmproj*.gguf", mm); /* рядом mmproj — модель с картинками */
    return TRUE;
  }
  if (!ai_tier_ready(g_aiTier)) return FALSE;
  ai_model_file(g_aiTier, 0, model);
  if (kAiTiers[g_aiTier].vision) ai_model_file(g_aiTier, kAiTiers[g_aiTier].nf - 1, mm);
  return TRUE;
}

static BOOL ai_ext(void) { return g_aiExtHost[0] && g_aiExtPort > 0; }
static BOOL ai_qwen35(const wchar_t *model) {
  wchar_t lower[MAX_PATH]; lstrcpynW(lower, model, MAX_PATH); CharLowerBuffW(lower, (DWORD)wcslen(lower));
  return (!g_aiModel[0] && g_aiTier == 1) || wcsstr(lower, L"qwen3.5") || wcsstr(lower, L"qwen3_5");
}
static BOOL ai_qwen_thinking(const wchar_t *model) {
  wchar_t lower[MAX_PATH]; lstrcpynW(lower, model, MAX_PATH); CharLowerBuffW(lower, (DWORD)wcslen(lower));
  return ai_qwen35(model) || wcsstr(lower, L"qwen3-") || wcsstr(lower, L"qwen3_");
}

BOOL ai_ready(void) {
  if (!g_aiOn) return FALSE;
  if (g_aiProvider) return ai_zai_has_key() && ai_zai_model_valid(g_aiZaiModel);
  if (ai_ext()) return TRUE;
  wchar_t exe[MAX_PATH], m[MAX_PATH], mm[MAX_PATH];
  return (ai_server_exe(g_aiGpu, exe) || ai_server_exe(!g_aiGpu, exe)) && ai_pick_model(m, mm);
}

const wchar_t *ai_status_text(int st) {
  switch (st) {
  case AI_OK: return L"готово";
  case AI_OFF: return L"ИИ выключен (Настройки → ИИ)";
  case AI_NO_MODEL: return g_aiProvider ? L"Z.ai: добавьте API-ключ в Настройки → ИИ" : L"модель не скачана (Настройки → ИИ → Скачать)";
  case AI_NO_SERVER: return L"llama-server не запустился";
  case AI_TIMEOUT: return L"ИИ не успел ответить";
  case AI_CANCELLED: return L"отменено";
  case AI_BUSY: return L"нельзя ждать ИИ в потоке окон";
  case AI_NO_VISION: return g_aiProvider ? L"Для картинок выберите в Z.ai модель с изображениями, например glm-4.6v-flash" : L"нужна модель с картинками: «Средний» или «С картинками»";
  default: return L"ошибка ИИ";
  }
}

/* характер помощника в чате (ai_chat_policy.h): 0 обычный, 1 южный, 2 интеллигент, 3 неуверенный, 4 матерный,
   5 прапорщик, 6 старый мастер, 7 зумер */
#define AI_PERSONAS 8
static int g_aiPersona;
/* Z.ai может получать данные PLM «Союз» и архива из инструментов ИИ — только
   если человек сам разрешил на вкладке «ИИ»; локальной модели можно всегда */
static BOOL g_aiZaiPlm;
/* папка справочника стандартов (std_files.c): «std_dir» в ai.txt; пусто — %USERPROFILE%\CursorPad-std */
static wchar_t g_gsDir[MAX_PATH];

/* ai.txt: строки «ключ значение», UTF-8 */
static void ai_save(void) {
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s\\ai.txt", g_dataDir);
  char model[MAX_PATH * 3], buf[4096];
  WideCharToMultiByte(CP_UTF8, 0, g_aiModel, -1, model, sizeof(model), NULL, NULL);
  int n = snprintf(buf, sizeof(buf), "on %d\r\ntier %d\r\nctx %d\r\ngpu %d\r\nmodel %s\r\n", g_aiOn ? 1 : 0, g_aiTier, g_aiCtx,
                   g_aiGpu ? 1 : 0, model);
  n += snprintf(buf + n, sizeof(buf) - n, "provider %d\r\nzai_model %s\r\npersona %d\r\nzai_plm %d\r\n", g_aiProvider, g_aiZaiModel,
                g_aiPersona, g_aiZaiPlm ? 1 : 0);
  if (g_aiSrvExe[0]) {
    char se[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, g_aiSrvExe, -1, se, sizeof(se), NULL, NULL);
    n += snprintf(buf + n, sizeof(buf) - n, "serverexe %s\r\n", se);
  }
  if (g_gsDir[0]) {
    char sd[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, g_gsDir, -1, sd, sizeof(sd), NULL, NULL);
    n += snprintf(buf + n, sizeof(buf) - n, "std_dir %s\r\n", sd);
  }
  if (ai_ext()) {
    char host[64];
    WideCharToMultiByte(CP_UTF8, 0, g_aiExtHost, -1, host, sizeof(host), NULL, NULL);
    n += snprintf(buf + n, sizeof(buf) - n, "server %s %d %s\r\n", host, g_aiExtPort, g_aiExtModel);
  }
  HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD w;
  WriteFile(h, buf, (DWORD)n, &w, NULL);
  CloseHandle(h);
}

static void ai_load(void) {
  g_aiUiThread = GetCurrentThreadId();
  BOOL migratedZai = FALSE;
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s\\ai.txt", g_dataDir);
  HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  char buf[4096];
  DWORD got = 0;
  ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
  CloseHandle(h);
  buf[got] = 0;
  for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    int v;
    if (sscanf(line, "on %d", &v) == 1) g_aiOn = v == 1;
    else if (sscanf(line, "provider %d", &v) == 1 && (v == 0 || v == 1)) g_aiProvider = v;
    else if (sscanf(line, "persona %d", &v) == 1 && v >= 0 && v < AI_PERSONAS) g_aiPersona = v;
    else if (sscanf(line, "zai_plm %d", &v) == 1) g_aiZaiPlm = v == 1;
    else if (!strncmp(line, "zai_model ", 10)) {
      migratedZai = !ai_zai_model_valid(line + 10);
      lstrcpynA(g_aiZaiModel, migratedZai ? "glm-4.5-flash" : line + 10, 64);
    }
    else if (sscanf(line, "tier %d", &v) == 1 && v >= 0 && v < AI_TIERS) g_aiTier = v;
    else if (sscanf(line, "ctx %d", &v) == 1 && v >= 2048 && v <= 16384) g_aiCtx = v;
    else if (sscanf(line, "gpu %d", &v) == 1) g_aiGpu = v == 1;
    else if (!strncmp(line, "model ", 6)) MultiByteToWideChar(CP_UTF8, 0, line + 6, -1, g_aiModel, MAX_PATH);
    else if (!strncmp(line, "serverexe ", 10)) MultiByteToWideChar(CP_UTF8, 0, line + 10, -1, g_aiSrvExe, MAX_PATH);
    else if (!strncmp(line, "std_dir ", 8)) MultiByteToWideChar(CP_UTF8, 0, line + 8, -1, g_gsDir, MAX_PATH);
    else if (!strncmp(line, "server ", 7)) {
      /* свой сервер с OpenAI-совместимым API: «server 127.0.0.1 11434 qwen3:4b» —
         Ollama или llama-server, запущенный руками. Только адреса этого компьютера:
         данные не должны уходить с него. */
      char host[64] = "", model[64] = "";
      int port = 0;
      if (sscanf(line + 7, "%63s %d %63s", host, &port, model) >= 2 &&
          (!strcmp(host, "127.0.0.1") || !strcmp(host, "localhost")) && port > 0 && port < 65536) {
        MultiByteToWideChar(CP_UTF8, 0, host, -1, g_aiExtHost, 64);
        g_aiExtPort = port;
        lstrcpynA(g_aiExtModel, model, sizeof(g_aiExtModel));
      }
    }
  }
  if (migratedZai) ai_save();
}

/* --- строки: UTF-8, JSON ------------------------------------------------------ */
static char *ai_utf8(const wchar_t *w) {
  if (!w) w = L"";
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *s = (char *)malloc(n > 0 ? n : 1);
  if (!s) return NULL;
  if (n <= 0) s[0] = 0;
  else WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  return s;
}

static wchar_t *ai_wide(const char *s, int len) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
  wchar_t *w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
  if (!w) return NULL;
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, len, w, n);
  w[n > 0 ? n : 0] = 0;
  return w;
}

/* строка JSON в кавычках из wchar_t; malloc */
static char *ai_json_str(const wchar_t *w) {
  char *u = ai_utf8(w);
  if (!u) return NULL;
  size_t n = strlen(u);
  char *o = (char *)malloc(n * 6 + 3), *q = o;
  if (!o) {
    free(u);
    return NULL;
  }
  *q++ = '"';
  for (const unsigned char *p = (const unsigned char *)u; *p; p++) {
    if (*p == '"' || *p == '\\') {
      *q++ = '\\';
      *q++ = (char)*p;
    } else if (*p == '\n') {
      *q++ = '\\';
      *q++ = 'n';
    } else if (*p == '\r') {
      *q++ = '\\';
      *q++ = 'r';
    } else if (*p == '\t') {
      *q++ = '\\';
      *q++ = 't';
    } else if (*p < 0x20) {
      q += sprintf(q, "\\u%04x", *p);
    } else {
      *q++ = (char)*p;
    }
  }
  *q++ = '"';
  *q = 0;
  free(u);
  return o;
}

static int ai_hex4(const char *p) {
  int v = 0;
  for (int i = 0; i < 4; i++) {
    char c = p[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return -1;
  }
  return v;
}

/* p — сразу после открывающей кавычки; строка JSON → wchar_t (malloc) */
static wchar_t *ai_json_unstr(const char *p) {
  size_t cap = strlen(p) + 1;
  char *u = (char *)malloc(cap), *q = u;
  if (!u) return NULL;
  while (*p && *p != '"') {
    if (*p != '\\') {
      *q++ = *p++;
      continue;
    }
    p++;
    char c = *p ? *p++ : 0;
    if (c == 'n') *q++ = '\n';
    else if (c == 'r') *q++ = '\r';
    else if (c == 't') *q++ = '\t';
    else if (c == 'b' || c == 'f') *q++ = ' ';
    else if (c == 'u') {
      int cp = ai_hex4(p);
      if (cp < 0) break;
      p += 4;
      if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
        int lo = ai_hex4(p + 2);
        if (lo >= 0xDC00 && lo < 0xE000) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          p += 6;
        }
      }
      if (cp < 0x80) *q++ = (char)cp;
      else if (cp < 0x800) {
        *q++ = (char)(0xC0 | cp >> 6);
        *q++ = (char)(0x80 | (cp & 63));
      } else if (cp < 0x10000) {
        *q++ = (char)(0xE0 | cp >> 12);
        *q++ = (char)(0x80 | (cp >> 6 & 63));
        *q++ = (char)(0x80 | (cp & 63));
      } else {
        *q++ = (char)(0xF0 | cp >> 18);
        *q++ = (char)(0x80 | (cp >> 12 & 63));
        *q++ = (char)(0x80 | (cp >> 6 & 63));
        *q++ = (char)(0x80 | (cp & 63));
      }
    } else if (c) *q++ = c;
  }
  *q = 0;
  wchar_t *w = ai_wide(u, -1);
  free(u);
  return w;
}

/* ответ модели: убрать рассуждения <think>…</think> и пробелы по краям */
static void ai_tidy(wchar_t *t) {
  wchar_t *e = wcsstr(t, L"</think>");
  if (e && wcsstr(t, L"<think>")) memmove(t, e + 8, (wcslen(e + 8) + 1) * sizeof(wchar_t));
  wchar_t *s = t;
  while (*s && iswspace(*s)) s++;
  if (s != t) memmove(t, s, (wcslen(s) + 1) * sizeof(wchar_t));
  size_t n = wcslen(t);
  while (n && iswspace(t[n - 1])) t[--n] = 0;
}

static char *ai_b64(const BYTE *d, DWORD n) {
  static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *o = (char *)malloc((size_t)(n + 2) / 3 * 4 + 1), *q = o;
  if (!o) return NULL;
  for (DWORD i = 0; i < n; i += 3) {
    DWORD v = (DWORD)d[i] << 16 | (i + 1 < n ? (DWORD)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    *q++ = k[v >> 18 & 63];
    *q++ = k[v >> 12 & 63];
    *q++ = i + 1 < n ? k[v >> 6 & 63] : '=';
    *q++ = i + 2 < n ? k[v & 63] : '=';
  }
  *q = 0;
  return o;
}

/* --- сервер ----------------------------------------------------------------- */
static SRWLOCK g_aiSrvLock = SRWLOCK_INIT;
static HANDLE g_aiProc, g_aiJob;
static int g_aiPort;
static char g_aiKey[40];
static volatile LONG g_aiActive;    /* запросов сейчас */
static volatile DWORD g_aiLastUse;  /* GetTickCount последнего ответа */
static BOOL g_aiWatchOn;
static wchar_t g_aiSrvNote[200];    /* почему не запустился — для «Проверить» */
static wchar_t g_aiRunNote[200];    /* как пришлось запустить (без видеокарты, малый контекст) */
static int g_aiSlotCtx;             /* контекст ячейки у идущего сервера; 0 — как в настройках */
static volatile LONG g_aiSrvUsers;  /* запросов, которые сейчас идут к поднятому серверу */
static int g_aiSrvWaiters, g_aiSrvWant; /* под замком: сколько ждут перезапуска и какое окно им нужно */
static int g_aiOutCap = 1024; /* длина ответа, токенов */

void ai_out_cap(int n) { g_aiOutCap = (n >= 64 && n <= 1024) ? n : 1024; }

/* простой HTTP к 127.0.0.1. reqSlot — куда положить ручку запроса, чтобы
   другой поток мог оборвать его, закрыв её. Возвращает код HTTP или 0. */
static DWORD ai_http(const wchar_t *verb, int port, const wchar_t *path, const char *body, const char *key,
                     DWORD timeoutMs, void *volatile *reqSlot, volatile LONG *abort, char **out) {
  if (out) *out = NULL;
  DWORD status = 0;
  HINTERNET ses = WinHttpOpen(L"CursorPad-AI", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return 0;
  int t = timeoutMs > 0x7fffffff ? 0x7fffffff : (int)timeoutMs;
  WinHttpSetTimeouts(ses, 2000, 2000, t, t);
  HINTERNET con = WinHttpConnect(ses, ai_ext() ? g_aiExtHost : L"127.0.0.1", (INTERNET_PORT)port, 0);
  HINTERNET req = con ? WinHttpOpenRequest(con, verb, path, NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
                      : NULL;
  if (req) {
    if (reqSlot) InterlockedExchangePointer((void *volatile *)reqSlot, req);
    /* без keep-alive: после ответа потоком сервер закрывает соединение, а WinHTTP
       (в Wine — точно) пытался отправить следующий запрос в уже закрытое */
    wchar_t hdr[200] = L"Content-Type: application/json\r\nConnection: close\r\n";
    if (key && key[0]) {
      wchar_t wk[48];
      MultiByteToWideChar(CP_UTF8, 0, key, -1, wk, 48);
      _snwprintf(hdr + wcslen(hdr), 120, L"Authorization: Bearer %s\r\n", wk);
    }
    DWORD bn = body ? (DWORD)strlen(body) : 0;
    if (WinHttpSendRequest(req, hdr, (DWORD)-1L, (LPVOID)body, bn, bn, 0) && WinHttpReceiveResponse(req, NULL)) {
      DWORD sl = sizeof(status);
      WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sl,
                          WINHTTP_NO_HEADER_INDEX);
      size_t cap = 65536, n = 0;
      char *buf = out ? (char *)malloc(cap) : NULL;
      for (;;) {
        DWORD avail = 0, got = 0;
        /* отмена: бросаем чтение и закрываем соединение — llama-server, не сумев
           отправить следующее слово, сам прекращает генерацию */
        if (abort && *abort) {
          status = 0;
          break;
        }
        if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
        if (!buf) { /* тело не нужно — дочитать впустую */
          char tmp[4096];
          if (!WinHttpReadData(req, tmp, avail < sizeof(tmp) ? avail : sizeof(tmp), &got) || !got) break;
          continue;
        }
        if (n + avail + 1 > cap) {
          while (n + avail + 1 > cap) cap *= 2;
          char *nb = (char *)realloc(buf, cap);
          if (!nb) break;
          buf = nb;
        }
        if (!WinHttpReadData(req, buf + n, avail, &got) || !got) break;
        n += got;
      }
      if (buf) {
        buf[n] = 0;
        *out = buf;
      }
    } else {
      status = 0;
    }
    /* ручку закрывает тот, кто первым забрал её из слота: мы или отмена */
    HINTERNET mine = reqSlot ? InterlockedExchangePointer((void *volatile *)reqSlot, NULL) : req;
    if (mine) WinHttpCloseHandle(mine);
  }
  if (con) WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  return status;
}

static void ai_server_stop_locked(void) {
  if (g_aiProc) {
    TerminateProcess(g_aiProc, 0);
    WaitForSingleObject(g_aiProc, 3000);
    CloseHandle(g_aiProc);
    g_aiProc = NULL;
  }
  g_aiPort = 0;
}

/* настройки сменили, пока сервер запускался: следующий запрос перезапустит его */
static volatile LONG g_aiSrvStale;

static void ai_server_stop(void) {
  /* Поток окон не ждёт замка: ai_server_ensure держит его, пока грузится модель
     (до срока запроса — минуты), а в этом потоке идут окна и хук мыши.
     Замок занят — только пометка «устарел», сервер погасит следующий запрос. */
  if (GetCurrentThreadId() == g_aiUiThread) {
    if (!TryAcquireSRWLockExclusive(&g_aiSrvLock)) {
      InterlockedExchange(&g_aiSrvStale, 1);
      return;
    }
  } else
    AcquireSRWLockExclusive(&g_aiSrvLock);
  ai_server_stop_locked();
  ReleaseSRWLockExclusive(&g_aiSrvLock);
}

/* сторож: гасит сервер после 10 минут без запросов или если ИИ выключили */
static DWORD WINAPI ai_watch_thread(LPVOID p) {
  (void)p;
  for (;;) {
    Sleep(15000);
    AcquireSRWLockExclusive(&g_aiSrvLock);
    BOOL idle = g_aiActive == 0 && (GetTickCount() - g_aiLastUse > AI_IDLE_STOP_MS || !g_aiOn);
    BOOL dead = !g_aiProc || WaitForSingleObject(g_aiProc, 0) == WAIT_OBJECT_0;
    if (idle || dead) {
      ai_server_stop_locked();
      g_aiWatchOn = FALSE;
      ReleaseSRWLockExclusive(&g_aiSrvLock);
      return 0;
    }
    ReleaseSRWLockExclusive(&g_aiSrvLock);
  }
}

static int ai_cores(void) {
  /* потоков столько, сколько физических ядер: на гиперпотоках llama.cpp не быстрее */
  DWORD len = 0;
  GetLogicalProcessorInformation(NULL, &len);
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(len ? len : 1);
  int cores = 0;
  if (info && GetLogicalProcessorInformation(info, &len))
    for (DWORD i = 0; i < len / sizeof(*info); i++)
      if (info[i].Relationship == RelationProcessorCore) cores++;
  free(info);
  if (cores <= 0) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    cores = (int)si.dwNumberOfProcessors / 2;
  }
  return cores < 1 ? 1 : (cores > 16 ? 16 : cores);
}

/* последняя непустая строка журнала сервера — почему он не поднялся */
static void ai_log_tail(const wchar_t *log, wchar_t *out, int cap) {
  out[0] = 0;
  HANDLE h = CreateFileW(log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  char buf[4096];
  DWORD sz = GetFileSize(h, NULL), got = 0;
  if (sz > sizeof(buf) - 1) SetFilePointer(h, (LONG)(sz - (sizeof(buf) - 1)), NULL, FILE_BEGIN);
  ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
  CloseHandle(h);
  buf[got] = 0;
  while (got && (buf[got - 1] == '\n' || buf[got - 1] == '\r' || buf[got - 1] == ' ')) buf[--got] = 0;
  char *ln = strrchr(buf, '\n');
  ln = ln ? ln + 1 : buf;
  MultiByteToWideChar(CP_UTF8, 0, ln, -1, out, cap);
  out[cap - 1] = 0;
}

/* порт занят другой программой */
static BOOL strstr_log_port(const wchar_t *log) {
  wchar_t t[200];
  ai_log_tail(log, t, 200);
  return wcsstr(t, L"bind") || wcsstr(t, L"address already in use");
}

/* почему llama-server не загрузил модель — по его журналу */
enum { AI_LF_OTHER, AI_LF_VRAM, AI_LF_RAM, AI_LF_ARCH, AI_LF_FILE };
static int ai_load_fail(const wchar_t *log) {
  HANDLE h = CreateFileW(log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return AI_LF_OTHER;
  static char b[32768];
  DWORD sz = GetFileSize(h, NULL), got = 0;
  if (sz > sizeof(b) - 1) SetFilePointer(h, (LONG)(sz - (sizeof(b) - 1)), NULL, FILE_BEGIN);
  ReadFile(h, b, sizeof(b) - 1, &got, NULL);
  CloseHandle(h);
  b[got] = 0;
  for (DWORD i = 0; i < got; i++)
    if (b[i] >= 'A' && b[i] <= 'Z') b[i] += 32;
  if (strstr(b, "unknown model architecture") || strstr(b, "unknown architecture")) return AI_LF_ARCH;
  if (strstr(b, "outofdevicememory") || strstr(b, "out of device memory") || strstr(b, "cudamalloc failed") ||
      strstr(b, "unable to allocate vulkan") || strstr(b, "unable to allocate cuda") ||
      (strstr(b, "failed to allocate") && (strstr(b, "vulkan") || strstr(b, "cuda"))))
    return AI_LF_VRAM;
  if (strstr(b, "failed to allocate") || strstr(b, "out of memory") || strstr(b, "bad_alloc") ||
      strstr(b, "not enough memory") || strstr(b, "failed to mmap") || strstr(b, "unable to allocate"))
    return AI_LF_RAM;
  if (strstr(b, "invalid magic") || strstr(b, "not within the file bounds") || strstr(b, "failed to read") ||
      strstr(b, "failed to open") || strstr(b, "corrupted") || strstr(b, "failed to load model from"))
    return AI_LF_FILE;
  return AI_LF_OTHER;
}

/* памяти: свободно сейчас и всего, МБ */
static void ai_mem_mb(int *avail, int *total) {
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  *avail = *total = 0;
  if (GlobalMemoryStatusEx(&ms)) {
    *avail = (int)(ms.ullAvailPhys >> 20);
    *total = (int)(ms.ullTotalPhys >> 20);
  }
}

static void ai_short_path(wchar_t *p) {
  BOOL ascii = TRUE;
  for (const wchar_t *c = p; *c; c++)
    if (*c > 127) ascii = FALSE;
  if (ascii) return;
  wchar_t s[MAX_PATH];
  DWORD n = GetShortPathNameW(p, s, MAX_PATH);
  if (n && n < MAX_PATH) {
    for (const wchar_t *c = s; *c; c++)
      if (*c > 127) {
        ai_log(L"Путь с русскими буквами и без короткого имени: %s — llama.cpp может его не открыть", p);
        break;
      }
    lstrcpynW(p, s, MAX_PATH);
  }
}

/* поднять сервер, если он не идёт. Ждёт загрузки модели до deadline.
   *held — запрос пошёл к своему серверу и записан в g_aiSrvUsers: по концу
   запроса его оттуда снимают (ai_job_run). */
static int ai_server_ensure(DWORD deadline, volatile LONG *cancel, int minimumContext, BOOL *held) {
  *held = FALSE;
  if (!g_aiOn) return AI_OFF;
  if (ai_ext()) return AI_OK;
  AcquireSRWLockExclusive(&g_aiSrvLock);
  /* Серверу нужно другое окно (агенту — 16384) или новые настройки, а через него
     ещё идут ответы: раньше его гасили сразу, и соседний ответ чата или
     распознавания чертежа обрывался ошибкой. Теперь ждём их конца, а новые
     запросы встают за ждущим — иначе ответы один за другим не дали бы ему дождаться. */
  BOOL waiting = FALSE;
  for (int spin = 0;; spin++) {
    int st = !g_aiOn                                               ? AI_OFF
             : cancel && *cancel                                   ? AI_CANCELLED
             : spin && (LONG)(GetTickCount() - deadline) >= 0      ? AI_TIMEOUT
                                                                   : AI_OK;
    if (st != AI_OK) {
      if (st == AI_TIMEOUT) lstrcpynW(g_aiSrvNote, L"сервер занят другими ответами — не дождался их конца", 200);
      if (waiting && --g_aiSrvWaiters == 0) g_aiSrvWant = 0;
      ReleaseSRWLockExclusive(&g_aiSrvLock);
      return st;
    }
    BOOL alive = g_aiProc && WaitForSingleObject(g_aiProc, 0) == WAIT_TIMEOUT && g_aiPort;
    if (alive && g_aiSlotCtx >= minimumContext && !g_aiSrvStale) {
      if (waiting || !g_aiSrvWaiters) {
        if (waiting && --g_aiSrvWaiters == 0) g_aiSrvWant = 0;
        InterlockedIncrement(&g_aiSrvUsers);
        *held = TRUE;
        ReleaseSRWLockExclusive(&g_aiSrvLock);
        return AI_OK;
      }
    } else if (!alive || !g_aiSrvUsers) {
      break; /* гасить можно: под ним никто не идёт */
    } else if (!waiting) {
      waiting = TRUE;
      g_aiSrvWaiters++;
      ai_log(L"Перезапуск сервера ждёт конца идущих ответов (%ld)", (long)g_aiSrvUsers);
    }
    if (waiting && minimumContext > g_aiSrvWant) g_aiSrvWant = minimumContext;
    ReleaseSRWLockExclusive(&g_aiSrvLock);
    Sleep(200);
    AcquireSRWLockExclusive(&g_aiSrvLock);
  }
  /* окно — самое большое из нужных ждущим: иначе следующий перезапустил бы снова */
  int need = minimumContext > g_aiSrvWant ? minimumContext : g_aiSrvWant;
  if (waiting && --g_aiSrvWaiters == 0) g_aiSrvWant = 0;
  ai_server_stop_locked();
  InterlockedExchange(&g_aiSrvStale, 0); /* запускаем с текущими настройками */
  wchar_t exe[MAX_PATH], model[MAX_PATH], mm[MAX_PATH], log[MAX_PATH], dir[MAX_PATH];
  BOOL gpu = g_aiGpu && ai_server_exe(TRUE, exe);
  if (!gpu && !ai_server_exe(FALSE, exe) && !ai_server_exe(TRUE, exe)) {
    lstrcpynW(g_aiSrvNote, L"нет llama-server.exe — нажмите «Скачать»", 200);
    ai_log(L"Запуск: нет llama-server.exe в %s\\ai\\llama", g_dataDir);
    ReleaseSRWLockExclusive(&g_aiSrvLock);
    return AI_NO_MODEL;
  }
  if (!ai_pick_model(model, mm)) {
    lstrcpynW(g_aiSrvNote, L"нет модели — нажмите «Скачать»", 200);
    ai_log(L"Запуск: нет модели (%s)", g_aiModel[0] ? g_aiModel : kAiTiers[g_aiTier].model);
    ReleaseSRWLockExclusive(&g_aiSrvLock);
    return AI_NO_MODEL;
  }
  if (!g_aiModel[0] && g_aiTier == AI_DRAW_LORA_TIER) ai_lora_kick(model);
  if (!g_aiJob) {
    g_aiJob = CreateJobObjectW(NULL, NULL);
    if (g_aiJob) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
      memset(&li, 0, sizeof(li));
      li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(g_aiJob, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }
  }
  { /* ключ на этот запуск: чужие программы на компьютере не будут спрашивать нашу модель */
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
    for (int i = 0; i < 16; i++) snprintf(g_aiKey + i * 2, 3, "%02x", r[i]);
  }
  ai_dir(dir, NULL);
  _snwprintf(log, MAX_PATH, L"%s\\server.log", dir);
  /* llama.cpp читает пути из командной строки не всегда как Юникод: русское имя
     пользователя в C:\\Users\\Иван\\… ломает -m. Короткое имя 8.3 — латиница */
  ai_short_path(model);
  if (mm[0]) ai_short_path(mm);
  int rc = AI_NO_SERVER;
  g_aiSrvNote[0] = g_aiRunNote[0] = 0;
  /* если модель не влезла, пробуем проще: без видеокарты, затем с малым контекстом */
  int ngl = gpu ? 99 : 0, ctx = g_aiCtx * 2, np = 2, fail = -1;
  if (need) { ctx = g_aiCtx > need ? g_aiCtx : need; np = 1; }
  {
    int av, tot;
    ai_mem_mb(&av, &tot);
    ai_log(L"Память: свободно %d МБ из %d, модель %d МБ, контекст %d", av, tot, (int)(ai_file_size(model) >> 20), ctx);
  }
  for (int attempt = 0; attempt < 6 && rc != AI_OK; attempt++) {
    int port = AI_PORT0 + (attempt % 4) * 7;
    wchar_t cmd[MAX_PATH * 4 + 300], wkey[40];
    MultiByteToWideChar(CP_UTF8, 0, g_aiKey, -1, wkey, 40);
    int n = _snwprintf(cmd, MAX_PATH * 4 + 300,
                       L"\"%s\" -m \"%s\" --host 127.0.0.1 --port %d --api-key %s -c %d -np %d -t %d -ngl %d",
                       exe, model, port, wkey, ctx, np, ai_cores(), ngl);
    if (mm[0] && n > 0) n += _snwprintf(cmd + n, MAX_PATH * 4 + 300 - n, L" --mmproj \"%s\"", mm);
    /* Qwen3 / Qwen3.5 default to thinking. Tool JSON has a small output budget, so
       reasoning must not consume that budget before the action is returned.
       Server default also covers QwenPaw's auxiliary checklist/audit calls. */
    if (ai_qwen_thinking(model) && n > 0)
      n += _snwprintf(cmd + n, MAX_PATH * 4 + 300 - n, L" --jinja --chat-template-kwargs \"{\\\"enable_thinking\\\":false}\"");
    /* The vision loader recommends at least 1024 image tokens for details.
       This affects attached images only; text requests keep their budget. */
    if (ai_qwen35(model) && mm[0] && n > 0)
      n += _snwprintf(cmd + n, MAX_PATH * 4 + 300 - n, L" --image-min-tokens 1024");
    /* дообучение: адаптер LoRA рядом с моделью подключается сам. lora*.gguf — к своей модели,
       на все ответы (обучена на своих документах); draw-lora*.gguf — к любой, только для
       чтения чертежей (native/train): в остальных запросах вес адаптера 0, чат прежний */
    wchar_t lora[MAX_PATH];
    g_aiDrawLora = FALSE;
    if (g_aiModel[0] && n > 0 && ai_find_near(g_aiModel, L"lora*.gguf", lora)) {
      ai_short_path(lora);
      _snwprintf(cmd + n, MAX_PATH * 4 + 300 - n, L" --lora \"%s\"", lora);
    } else if (n > 0 && (ai_find_near(model, L"draw-lora2*.gguf", lora) || ai_find_near(model, L"draw-lora*.gguf", lora))) {
      /* второе поколение (знает конструктивные элементы) — первым: drawrec.c спрашивает по нему */
      ai_short_path(lora);
      _snwprintf(cmd + n, MAX_PATH * 4 + 300 - n, L" --lora \"%s\" --lora-init-without-apply", lora);
      g_aiDrawLora = TRUE;
    }
    cmd[MAX_PATH * 4 + 299] = 0;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE lf = CreateFileW(log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (lf != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = lf;
      si.hStdError = lf;
      si.hStdInput = NULL;
    }
    wchar_t exeDir[MAX_PATH];
    lstrcpynW(exeDir, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exeDir, L'\\');
    if (sl) *sl = 0;
    ai_log(L"Запуск локального llama-server; порт %d", port); /* g_aiPort здесь уже 0: сервер только что остановлен */
    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, lf != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             NULL, exeDir, &si, &pi);
    if (lf != INVALID_HANDLE_VALUE) CloseHandle(lf);
    if (!ok) {
      DWORD e = GetLastError();
      _snwprintf(g_aiSrvNote, 200,
                 e == ERROR_ACCESS_DENIED || e == 225 /* ERROR_VIRUS_INFECTED */
                     ? L"Windows не дал запустить llama-server.exe (код %lu) — антивирус?"
                     : L"llama-server.exe не запустился (код %lu)",
                 e);
      ai_log(L"%s", g_aiSrvNote);
      break;
    }
    if (g_aiJob) AssignProcessToJobObject(g_aiJob, pi.hProcess);
    SetPriorityClass(pi.hProcess, BELOW_NORMAL_PRIORITY_CLASS); /* мышь и окна важнее */
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    g_aiProc = pi.hProcess;
    g_aiPort = port;
    /* ждём /health: 503, пока грузится модель, 200 — готов */
    for (;;) {
      if (WaitForSingleObject(g_aiProc, 300) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(g_aiProc, &code);
        ai_log_tail(log, g_aiSrvNote, 200);
        ai_log(L"llama-server завершился (код 0x%08lX): %s", code, g_aiSrvNote);
        if (code == 0xC000001D) lstrcpynW(g_aiSrvNote, L"процессор не тянет эту сборку llama.cpp (нет AVX2?)", 200);
        if (code == 0xC0000135) lstrcpynW(g_aiSrvNote, L"llama-server не нашёл свои DLL — распакуйте архив целиком", 200);
        ai_server_stop_locked();
        if (code == 0xC000001D || code == 0xC0000135) {
          attempt = 99;
          break;
        }
        if (strstr_log_port(log)) break; /* порт занят — тот же запуск на другом порту */
        fail = ai_load_fail(log);
        ai_log(L"Причина по журналу: %s", fail == AI_LF_VRAM ? L"не хватило памяти видеокарты"
                                         : fail == AI_LF_RAM  ? L"не хватило оперативной памяти"
                                         : fail == AI_LF_ARCH ? L"llama.cpp не знает эту модель"
                                         : fail == AI_LF_FILE ? L"файл модели не читается"
                                                              : L"не ясна");
        if (fail == AI_LF_ARCH || fail == AI_LF_FILE) {
          attempt = 99;
          break;
        }
        if (ngl) {
          ngl = 0;
          lstrcpynW(g_aiRunNote, L"видеокарте не хватило памяти, модель идёт на процессоре", 200);
          ai_log(L"Повтор без видеокарты (-ngl 0)");
        } else if (ctx > 2048 && !need) {
          ctx = 2048;
          np = 1;
          lstrcpynW(g_aiRunNote, L"памяти мало, контекст урезан до 2048", 200);
          ai_log(L"Повтор с малым контекстом (-c 2048 -np 1)");
        } else {
          attempt = 99;
        }
        break;
      }
      if (!g_aiOn || (cancel && *cancel) || (LONG)(GetTickCount() - deadline) >= 0) {
        rc = !g_aiOn ? AI_OFF : cancel && *cancel ? AI_CANCELLED : AI_TIMEOUT;
        if (rc == AI_TIMEOUT) lstrcpynW(g_aiSrvNote, L"модель грузится слишком долго", 200);
        ai_server_stop_locked();
        attempt = 99;
        break;
      }
      if (ai_http(L"GET", port, L"/health", NULL, NULL, 1500, NULL, NULL, NULL) == 200) {
        ai_log(L"llama-server готов, порт %d", port);
        /* адаптер чертежей: по умолчанию вес 0 (запрос без поля lora берёт общий вес),
           1 — только в запросах с чертежом (ai_ask_image_sync_ex) */
        if (g_aiDrawLora &&
            ai_http(L"POST", port, L"/lora-adapters", "[{\"id\":0,\"scale\":0.0}]", g_aiKey, 5000, NULL, NULL, NULL) != 200)
          ai_log(L"Адаптер чертежей не отключился для остальных запросов");
        g_aiSlotCtx = ctx / np;
        rc = AI_OK;
        break;
      }
    }
  }
  if (rc != AI_OK && fail >= 0 && !(cancel && *cancel)) {
    int av, tot;
    ai_mem_mb(&av, &tot);
    int need = (int)(ai_file_size(model) >> 20) + 700;
    if (fail == AI_LF_ARCH)
      lstrcpynW(g_aiSrvNote, L"эта llama.cpp старая для модели — поставьте свежую кнопкой «llama.cpp…» или «Скачать»", 200);
    else if (fail == AI_LF_FILE)
      lstrcpynW(g_aiSrvNote, L"файл модели не читается (испорчен или не та модель) — удалите его и нажмите «Скачать»", 200);
    else if (fail == AI_LF_RAM || need > av)
      _snwprintf(g_aiSrvNote, 200,
                 L"не хватает памяти: свободно %d МБ, модели нужно около %d МБ — закройте лишние программы или возьмите %s%s%s",
                 av, need, !g_aiModel[0] && g_aiTier > 0 ? L"уровень «" : L"модель поменьше",
                 !g_aiModel[0] && g_aiTier > 0 ? kAiTiers[g_aiTier >= 2 ? 1 : 0].button : L"",
                 !g_aiModel[0] && g_aiTier > 0 ? L"»" : L"");
    else if (!g_aiSrvNote[0])
      lstrcpynW(g_aiSrvNote, L"llama-server не загрузил модель", 200);
    g_aiSrvNote[199] = 0;
    ai_log(L"Итог: %s", g_aiSrvNote);
  }
  if (rc == AI_OK) {
    InterlockedIncrement(&g_aiSrvUsers);
    *held = TRUE;
    g_aiLastUse = GetTickCount();
    if (!g_aiWatchOn) {
      HANDLE th = CreateThread(NULL, 0, ai_watch_thread, NULL, 0, NULL);
      if (th) {
        g_aiWatchOn = TRUE;
        CloseHandle(th);
      }
    }
  }
  ReleaseSRWLockExclusive(&g_aiSrvLock);
  return rc;
}

static void aq_warm_cancel(void);
void ai_shutdown(void) {
  ai_disable();
}

/* --- запросы ------------------------------------------------------------------ */
typedef struct AiJob {
  int id;
  int provider;
  LONG agentTask;
  char *body;
  DWORD timeout;
  HWND notify;
  UINT msg;
  void *user;
  volatile LONG cancel;
  volatile LONG *externalCancel;
  void *volatile req; /* открытый запрос — закрыть, чтобы оборвать */
  int status;
  wchar_t *text;
  DWORD ms;
  volatile LONG refs;
  struct AiJob *next;
} AiJob;

static SRWLOCK g_aiJobLock = SRWLOCK_INIT;
static AiJob *g_aiJobs;
static volatile LONG g_aiNextId;

static char *ai_body(const wchar_t *instruction, const char *userContent, int maxTokens) {
  char *sys = ai_json_str(instruction);
  if (!sys || !userContent) {
    free(sys);
    return NULL;
  }
  size_t n = strlen(sys) + strlen(userContent) + 400 + strlen(g_aiExtModel);
  char *b = (char *)malloc(n);
  if (b)
    snprintf(b, n,
             "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":%s},"
             "{\"role\":\"user\",\"content\":%s}],\"temperature\":0.2,\"max_tokens\":%d,"
             "\"stream\":true,\"cache_prompt\":true,\"chat_template_kwargs\":{\"enable_thinking\":false}}",
             ai_selected_model(), sys, userContent, maxTokens);
  free(sys);
  return b;
}

/* ответ потоком (SSE): строки «data: {…"delta":{"content":"кусок"}…}» — склеить куски */
static wchar_t *ai_sse_text(const char *s) {
  size_t cap = 256, n = 0;
  wchar_t *acc = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!acc) return NULL;
  acc[0] = 0;
  for (const char *p = strstr(s, "data:"); p; p = strstr(p + 5, "data:")) {
    const char *eol = strchr(p, '\n');
    size_t ln = eol ? (size_t)(eol - p) : strlen(p);
    char *line = (char *)malloc(ln + 1);
    if (!line) break;
    memcpy(line, p, ln);
    line[ln] = 0;
    const char *c = json_find_string(line, "content");
    wchar_t *piece = c ? ai_json_unstr(c) : NULL;
    free(line);
    if (!piece) continue;
    size_t pl = wcslen(piece);
    if (n + pl + 1 > cap) {
      while (n + pl + 1 > cap) cap *= 2;
      wchar_t *na = (wchar_t *)realloc(acc, cap * sizeof(wchar_t));
      if (!na) {
        free(piece);
        break;
      }
      acc = na;
    }
    wmemcpy(acc + n, piece, pl + 1);
    n += pl;
    free(piece);
  }
  return acc;
}

#include "ai_qwen.c"

static void ai_job_run(AiJob *j) {
  DWORD t0 = GetTickCount(), deadline = t0 + j->timeout;
  InterlockedIncrement(&g_aiActive);
  BOOL held = FALSE;
  int st = j->provider != g_aiProvider ? AI_CANCELLED : j->provider ? (!g_aiOn ? AI_OFF : AI_OK) : ai_server_ensure(deadline, &j->cancel, j->agentTask ? 16384 : 0, &held);
  if (st == AI_NO_SERVER || st == AI_NO_MODEL) {
    AcquireSRWLockShared(&g_aiSrvLock);
    j->text = _wcsdup(g_aiSrvNote[0] ? g_aiSrvNote : ai_status_text(st));
    ReleaseSRWLockShared(&g_aiSrvLock);
  }
  if (st == AI_OK && (j->agentTask || j->provider)) {
    st = aq_run(j, deadline, ai_ext() ? g_aiExtPort : g_aiPort);
    if (st == AI_OK && !j->agentTask && j->text) ai_tidy(j->text);
    g_aiLastUse = GetTickCount();
  } else if (st == AI_OK) {
    DWORD left = deadline - GetTickCount();
    if ((LONG)left < 1000) left = 1000;
    char *resp = NULL;
    int port = ai_ext() ? g_aiExtPort : g_aiPort;
    DWORD code = 0;
    for (int tr = 0; tr < 2 && !code && !j->cancel; tr++) { /* сразу не ушло — ещё раз, по новому соединению */
      DWORD t1 = GetTickCount();
      code = ai_http(L"POST", port, L"/v1/chat/completions", j->body, ai_ext() ? NULL : g_aiKey, left, &j->req,
                     &j->cancel, &resp);
      if (code || GetTickCount() - t1 > 1000) break;
      free(resp);
      resp = NULL;
    }
    if (j->cancel) st = AI_CANCELLED;
    else if (!code) st = GetTickCount() - t0 >= j->timeout - 500 ? AI_TIMEOUT : AI_NO_SERVER;
    else {
      if (code == 200 && resp && strstr(resp, "data:")) j->text = ai_sse_text(resp);
      else {
        const char *c = resp ? json_find_string(resp, code == 200 ? "content" : "message") : NULL;
        if (c) j->text = ai_json_unstr(c);
      }
      if (code == 200 && j->text) {
        ai_tidy(j->text);
        st = AI_OK;
      } else {
        st = AI_ERROR;
        if (!j->text) {
          wchar_t m[64];
          _snwprintf(m, 64, L"сервер ИИ ответил кодом %lu", code);
          j->text = _wcsdup(m);
        }
      }
    }
    free(resp);
    g_aiLastUse = GetTickCount();
  }
  if (held) InterlockedDecrement(&g_aiSrvUsers);
  InterlockedDecrement(&g_aiActive);
  j->status = st;
  j->ms = GetTickCount() - t0;
  if (!j->text) j->text = _wcsdup(ai_status_text(st));
  if (st != AI_OK && st != AI_CANCELLED) ai_log(L"Запрос: %s (%lu мс)", j->text, j->ms);
}

/* У запроса два хозяина: поток, который ходит к серверу, и тот, кто ждёт ответа.
   Ждущий не зависит от того, оборвёт ли WinHTTP запрос по закрытию ручки или по
   таймауту (в Wine — не обрывает): по отмене или сроку он бросает запрос и
   отвечает сразу, а ходящий поток доделает своё и освободит job сам — кто
   последний, тот и освобождает. */
static void ai_job_release(AiJob *j) {
  AcquireSRWLockExclusive(&g_aiJobLock);
  BOOL last = InterlockedDecrement(&j->refs) == 0;
  if (last)
    for (AiJob **pp = &g_aiJobs; *pp; pp = &(*pp)->next)
      if (*pp == j) {
        *pp = j->next;
        break;
      }
  ReleaseSRWLockExclusive(&g_aiJobLock);
  if (last) {
    free(j->body);
    free(j->text);
    free(j);
  }
}

static DWORD WINAPI ai_worker(LPVOID p) {
  AiJob *j = (AiJob *)p;
  ai_job_run(j);
  ai_job_release(j);
  return 0;
}

static AiJob *ai_job_new(char *body, DWORD timeoutMs) {
  AiJob *j = (AiJob *)calloc(1, sizeof(AiJob));
  if (!j) {
    free(body);
    return NULL;
  }
  j->id = (int)InterlockedIncrement(&g_aiNextId);
  j->provider = g_aiProvider;
  j->body = body;
  j->timeout = timeoutMs ? timeoutMs : 120000;
  j->refs = 1; /* ждущий */
  AcquireSRWLockExclusive(&g_aiJobLock);
  j->next = g_aiJobs;
  g_aiJobs = j;
  ReleaseSRWLockExclusive(&g_aiJobLock);
  return j;
}

void ai_cancel(int id) {
  AcquireSRWLockExclusive(&g_aiJobLock);
  for (AiJob *j = g_aiJobs; j; j = j->next)
    if (j->id == id) {
      j->cancel = 1;
      HINTERNET h = InterlockedExchangePointer(&j->req, NULL);
      if (h) WinHttpCloseHandle(h);
    }
  ReleaseSRWLockExclusive(&g_aiJobLock);
}

void ai_disable(void) {
  InterlockedExchange(&g_aiOn, FALSE);
  aq_warm_cancel();
  AcquireSRWLockExclusive(&g_aiJobLock);
  for (AiJob *j = g_aiJobs; j; j = j->next) {
    InterlockedExchange(&j->cancel, 1);
    HINTERNET h = InterlockedExchangePointer(&j->req, NULL);
    if (h) WinHttpCloseHandle(h);
  }
  ReleaseSRWLockExclusive(&g_aiJobLock);
  ai_server_stop(); /* only our owned process; external servers are untouched */
}

/* запустить ходящий поток и ждать: ответа, отмены (j->cancel или ext) или срока.
   Ответ — в *text (malloc). Освобождает ссылку ждущего. */
static int ai_wait(AiJob *j, volatile LONG *ext, wchar_t **text, DWORD *ms) {
  *text = NULL;
  DWORD t0 = GetTickCount();
  InterlockedIncrement(&j->refs);
  HANDLE th = CreateThread(NULL, 0, ai_worker, j, 0, NULL);
  if (!th) {
    InterlockedDecrement(&j->refs);
    ai_job_release(j);
    return AI_BUSY;
  }
  int st;
  for (;;) {
    if (WaitForSingleObject(th, 100) == WAIT_OBJECT_0) {
      st = j->status;
      *text = j->text;
      j->text = NULL;
      break;
    }
    BOOL stop = j->cancel || (ext && *ext);
    if (stop || GetTickCount() - t0 >= j->timeout) {
      st = stop ? AI_CANCELLED : AI_TIMEOUT;
      ai_cancel(j->id); /* на Windows закрытая ручка обрывает и генерацию на сервере */
      *text = _wcsdup(ai_status_text(st));
      break;
    }
  }
  CloseHandle(th);
  if (ms) *ms = GetTickCount() - t0;
  ai_job_release(j);
  return st;
}

void ai_result_free(AiResult *r) {
  if (!r) return;
  free(r->text);
  free(r);
}

static int ai_precheck(void) {
  if (!g_aiOn) return AI_OFF;
  if (!ai_ready()) return AI_NO_MODEL;
  return AI_OK;
}

typedef struct {
  AiJob *j;
  int id;
  HWND notify;
  UINT msg;
  void *user;
} AiAsync;

static DWORD WINAPI ai_async_thread(LPVOID p) {
  AiAsync *a = (AiAsync *)p;
  AiResult *r = (AiResult *)calloc(1, sizeof(AiResult));
  wchar_t *text = NULL;
  DWORD ms = 0;
  int st = ai_wait(a->j, NULL, &text, &ms);
  if (r) {
    r->id = a->id;
    r->status = st;
    r->text = text ? text : _wcsdup(ai_status_text(st));
    r->user = a->user;
    r->ms = ms;
    if (!PostMessageW(a->notify, a->msg, 0, (LPARAM)r)) ai_result_free(r); /* окно закрыто */
  } else {
    free(text);
  }
  free(a);
  return 0;
}

static int ai_ask_body(char *body, DWORD timeoutMs, HWND notify, UINT msg, void *user);

int ai_ask_async(const wchar_t *instruction, const wchar_t *text, DWORD timeoutMs, HWND notify, UINT msg,
                 void *user) {
  if (ai_precheck() != AI_OK || !notify) return 0;
  char *u = ai_json_str(text);
  char *body = ai_body(instruction, u, 1024);
  free(u);
  if (!body) return 0;
  return ai_ask_body(body, timeoutMs, notify, msg, user);
}

/* беседа: system, затем реплики; тело запроса — как у ai_body */
/* вопрос с картинкой: текст и image_url с PNG в data-адресе */
static char *ai_content_img(const wchar_t *text, const BYTE *png, DWORD len) {
  char *tx = ai_json_str(text);
  char *b64 = ai_b64(png, len);
  if (!tx || !b64) {
    free(tx);
    free(b64);
    return NULL;
  }
  size_t n = strlen(tx) + strlen(b64) + 200;
  char *c = (char *)malloc(n);
  if (c)
    snprintf(c, n,
             "[{\"type\":\"text\",\"text\":%s},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,%s\"}}]",
             tx, b64);
  free(tx);
  free(b64);
  return c;
}

/* стоит ли рядом с моделью адаптер чертежей второго поколения (draw-lora2…): он обучен называть
   конструктивные элементы, и drawrec.c спрашивает про них только тогда */
BOOL ai_draw_elements(void) {
  if (g_aiProvider || ai_ext()) return FALSE;
  wchar_t m[MAX_PATH], mm[MAX_PATH], f[MAX_PATH];
  return ai_pick_model(m, mm) && ai_find_near(m, L"draw-lora2*.gguf", f);
}

BOOL ai_vision_ready(void) {
  if (g_aiProvider) return ai_zai_vision();
  if (ai_ext()) return TRUE;
  wchar_t m[MAX_PATH], mm[MAX_PATH];
  return ai_pick_model(m, mm) && mm[0] != 0;
}

/* картинка весит как ~3000 символов истории: модель тратит на неё много контекста */
static long ai_turn_cost(const AiTurn *t) { return (long)wcslen(t->text) + (t->png ? 3000 : 0); }

#include "ai_agent_schema.h"
static char *ai_body_chat(const wchar_t *instruction, const AiTurn *t, int n, BOOL fileTools) {
  char *sys = ai_json_str(instruction);
  char **js = (char **)calloc(n > 0 ? n : 1, sizeof(char *));
  if (!sys || !js) {
    free(sys);
    free(js);
    return NULL;
  }
  size_t total = strlen(sys) + strlen(g_aiExtModel) + 400 + (fileTools ? strlen(kAiAgentSchema) : 0);
  for (int i = 0; i < n; i++) {
    js[i] = t[i].png ? ai_content_img(t[i].text, t[i].png, t[i].pngLen) : ai_json_str(t[i].text);
    if (!js[i]) break;
    total += strlen(js[i]) + 64;
  }
  char *b = NULL;
  if (js[n - 1]) b = (char *)malloc(total);
  if (b) {
    size_t k = snprintf(b, total, "{\"model\":\"%s\",\"messages\":[{\"role\":\"system\",\"content\":%s}",
                        ai_selected_model(), sys);
    for (int i = 0; i < n; i++)
      k += snprintf(b + k, total - k, ",{\"role\":\"%s\",\"content\":%s}", t[i].assistant ? "assistant" : "user",
                    js[i]);
    snprintf(b + k, total - k,
             "],\"temperature\":0.2,\"max_tokens\":%d,\"stream\":true,\"cache_prompt\":true,\"chat_template_kwargs\":{\"enable_thinking\":false}%s}", g_aiOutCap,
             fileTools ? kAiAgentSchema : "");
  }
  for (int i = 0; i < n; i++) free(js[i]);
  free(js);
  free(sys);
  return b;
}

int ai_chat_budget(void) { return ((g_aiProvider ? g_aiCtx : g_aiSlotCtx ? g_aiSlotCtx : g_aiCtx) - 1024 - 300) * 2; } /* ≈ 2 символа на токен: запас для русского */
int ai_agent_budget(void) { return (16384 - 1024 - 300) * 2; }
int ai_ctx_size(void) { return g_aiCtx; }

static int ai_ask_chat_kind(const wchar_t *instruction, const AiTurn *turns, int n, DWORD timeoutMs, HWND notify,
                            UINT msg, void *user, BOOL fileTools) {
  if (ai_precheck() != AI_OK || !notify || !turns || n < 1 || !turns[n - 1].text) return 0;
  /* роль system не входит в бюджет реплик: длинная тетрадь иначе вытесняет вопрос из окна */
  long budget = ai_chat_budget();
  long sysCost = instruction ? (long)wcslen(instruction) : 0;
  if (sysCost > 200) {
    budget -= sysCost - 200;
    if (budget < 400) budget = 400;
  }
  long used = ai_turn_cost(&turns[n - 1]);
  if (used > budget) return 0;
  int s = n - 1;
  while (s > 0 && used + ai_turn_cost(&turns[s - 1]) <= budget) used += ai_turn_cost(&turns[--s]);
  while (s < n - 1 && turns[s].assistant) s++; /* модель начинает с вопроса */
  char *body = ai_body_chat(instruction, turns + s, n - s, fileTools);
  if (!body) return 0;
  return ai_ask_body(body, timeoutMs, notify, msg, user);
}

int ai_ask_chat_async(const wchar_t *instruction, const AiTurn *turns, int n, DWORD timeoutMs, HWND notify,
                      UINT msg, void *user) {
  return ai_ask_chat_kind(instruction, turns, n, timeoutMs, notify, msg, user, FALSE);
}

int ai_ask_files_async(const wchar_t *instruction, const AiTurn *turns, int n, HWND notify, UINT msg,
                       LONG task, const wchar_t *goal, const wchar_t *plan,
                       const wchar_t *observation, int observationState, int planCount) {
  if (ai_precheck() != AI_OK || !notify || n != 1 || !turns || !turns[0].text || task <= 0) return 0;
  char *body = ai_body_chat(instruction, turns, n, TRUE), *g = ai_json_str(goal), *p = ai_json_str(plan);
  char *o = ai_json_str(observation);
  if (!body || !g || !p || !o) { free(body); free(g); free(p); free(o); return 0; }
  size_t size = strlen(body) + strlen(g) + strlen(p) + strlen(o) + 200;
  char *wire = (char *)malloc(size);
  if (wire) snprintf(wire, size, "{\"cursorpad\":{\"goal\":%s,\"plan\":%s,\"plan_count\":%d,\"observation\":%s,\"observation_state\":%d},%s", g, p, planCount, o, observationState, body + 1);
  free(body); free(g); free(p); free(o);
  AiAsync *a = (AiAsync *)calloc(1, sizeof(AiAsync));
  AiJob *j = a && wire ? ai_job_new(wire, 600000) : NULL;
  if (!j) { if (!a || !wire) free(wire); free(a); return 0; }
  j->agentTask = task;
  a->j = j; a->id = j->id; a->notify = notify; a->msg = msg;
  int id = j->id;
  HANDLE th = CreateThread(NULL, 0, ai_async_thread, a, 0, NULL);
  if (!th) { ai_job_release(j); free(a); return 0; }
  CloseHandle(th); return id;
}

static int ai_ask_body(char *body, DWORD timeoutMs, HWND notify, UINT msg, void *user) {
  AiAsync *a = (AiAsync *)calloc(1, sizeof(AiAsync));
  AiJob *j = a ? ai_job_new(body, timeoutMs) : NULL;
  if (!j) {
    if (!a) free(body);
    free(a);
    return 0;
  }
  int id = j->id;
  a->j = j;
  a->id = id;
  a->notify = notify;
  a->msg = msg;
  a->user = user;
  HANDLE th = CreateThread(NULL, 0, ai_async_thread, a, 0, NULL);
  if (!th) {
    ai_job_release(j);
    free(a);
    return 0;
  }
  CloseHandle(th); /* a и j теперь у потока — не трогать */
  return id;
}

/* синхронно: свой поток на запрос, а этот ждёт и следит за флагом отмены */
static int ai_run_sync(char *body, DWORD timeoutMs, volatile LONG *cancel, wchar_t **answer) {
  if (answer) *answer = NULL;
  AiJob *j = ai_job_new(body, timeoutMs);
  if (!j) return AI_BUSY;
  wchar_t *text = NULL;
  int st = ai_wait(j, cancel, &text, NULL);
  if (answer) *answer = text;
  else free(text);
  return st;
}

int ai_ask_sync(const wchar_t *instruction, const wchar_t *text, DWORD timeoutMs, volatile LONG *cancel,
                wchar_t **answer) {
  if (answer) *answer = NULL;
  if (GetCurrentThreadId() == g_aiUiThread) return AI_BUSY;
  int pre = ai_precheck();
  if (pre != AI_OK) return pre;
  char *u = ai_json_str(text);
  char *body = ai_body(instruction, u, 1024);
  free(u);
  if (!body) return AI_BUSY;
  return ai_run_sync(body, timeoutMs, cancel, answer);
}

int ai_ask_image_sync(const wchar_t *instruction, const wchar_t *imagePath, DWORD timeoutMs, volatile LONG *cancel,
                      wchar_t **answer) {
  if (answer) *answer = NULL;
  if (GetCurrentThreadId() == g_aiUiThread) return AI_BUSY;
  int pre = ai_precheck();
  if (pre != AI_OK) return pre;
  if (!ai_vision_ready()) return AI_NO_VISION;
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
  const char *mime = ext && (!_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg")) ? "image/jpeg"
                     : ext && !_wcsicmp(ext, L".bmp")                             ? "image/bmp"
                                                                                  : "image/png";
  char *b64 = ai_b64(data, sz);
  free(data);
  if (!b64) return AI_BUSY;
  size_t n = strlen(b64) + 200;
  char *content = (char *)malloc(n);
  if (content)
    snprintf(content, n, "[{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:%s;base64,%s\"}}]", mime, b64);
  free(b64);
  char *body = ai_body(instruction, content, 1024);
  free(content);
  if (!body) return AI_BUSY;
  return ai_run_sync(body, timeoutMs ? timeoutMs : 300000, cancel, answer);
}

/* Картинка и вопрос в одной реплике (так модели с картинками читают точнее, чем
   с вопросом в system), температура 0; schema — JSON-схема ответа или NULL. */
int ai_ask_image_sync_ex(const wchar_t *question, const wchar_t *imagePath, const char *schema, int maxTokens,
                         DWORD timeoutMs, volatile LONG *cancel, wchar_t **answer) {
  if (answer) *answer = NULL;
  if (GetCurrentThreadId() == g_aiUiThread) return AI_BUSY;
  int pre = ai_precheck();
  if (pre != AI_OK) return pre;
  if (!ai_vision_ready()) return AI_NO_VISION;
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
  size_t n = (b64 ? strlen(b64) : 0) + (q ? strlen(q) : 0) + (schema ? strlen(schema) : 0) + strlen(g_aiExtModel) + 600;
  char *body = b64 && q ? (char *)malloc(n) : NULL;
  if (body) {
    int k = snprintf(body, n,
                     "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"image_url\",\"image_url\":"
                     "{\"url\":\"data:%s;base64,%s\"}},{\"type\":\"text\",\"text\":%s}]}],\"temperature\":0,\"max_tokens\":%d,"
                     "\"stream\":true,\"cache_prompt\":false,\"chat_template_kwargs\":{\"enable_thinking\":false}",
                     ai_selected_model(), mime, b64, q, maxTokens);
    if (schema)
      k += snprintf(body + k, n - k, ",\"response_format\":{\"type\":\"json_schema\",\"json_schema\":{\"name\":\"r\",\"strict\":true,\"schema\":%s}}", schema);
    if (g_aiDrawLora && !ai_ext()) k += snprintf(body + k, n - k, ",\"lora\":[{\"id\":0,\"scale\":1.0}]"); /* картинки — только чертежи */
    snprintf(body + k, n - k, "}");
  }
  free(b64);
  free(q);
  if (!body) return AI_BUSY;
  return ai_run_sync(body, timeoutMs ? timeoutMs : 300000, cancel, answer);
}

/* --- точки расширения ------------------------------------------------------- */
BOOL ai_refine_field(const wchar_t *field, const wchar_t *raw, const wchar_t *context, wchar_t *out, int cap,
                     DWORD timeoutMs) {
  if (!out || cap < 2 || !raw || !raw[0]) return FALSE;
  size_t n = wcslen(raw) + (context ? wcslen(context) : 0) + wcslen(field ? field : L"") + 200;
  wchar_t *q = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (!q) return FALSE;
  if (context && context[0])
    _snwprintf(q, n, L"%s (распознано): %s\nРядом на листе: %s", field ? field : L"Значение", raw, context);
  else _snwprintf(q, n, L"%s (распознано): %s", field ? field : L"Значение", raw);
  q[n - 1] = 0;
  wchar_t *a = NULL;
  int st = ai_ask_sync(
      L"Ты исправляешь ошибки автоматического распознавания текста в конструкторской документации завода "
      L"(ЕСКД, ГОСТ 2.201, обозначения материалов и сортамента по ГОСТ). Типичные ошибки: буква О вместо "
      L"цифры 0, З вместо 3, латинские буквы вместо русских, лишние или потерянные пробелы, точки и дефисы. "
      L"Ничего не добавляй и не выдумывай. Если ошибок нет — повтори значение как есть. "
      L"Ответь только исправленным значением, одной строкой, без кавычек и пояснений.",
      q, timeoutMs ? timeoutMs : 60000, NULL, &a);
  free(q);
  BOOL ok = FALSE;
  if (st == AI_OK && a) {
    wchar_t *v = a;
    size_t l = wcslen(v);
    if (l >= 2 && (v[0] == L'«' || v[0] == L'"') && (v[l - 1] == L'»' || v[l - 1] == L'"')) {
      v[l - 1] = 0;
      v++;
    }
    const wchar_t *colon = wcschr(v, L':');
    if (colon && field && !_wcsnicmp(v, field, wcslen(field))) v = (wchar_t *)colon + 1; /* «Материал: …» */
    while (*v == L' ') v++;
    l = wcslen(v);
    /* модель повторила название поля, ответила пустым или написала рассказ — не берём */
    ok = l > 0 && !wcschr(v, L'\n') && (!field || _wcsicmp(v, field)) && l <= wcslen(raw) * 2 + 10 &&
         l * 2 + 2 >= wcslen(raw);
    if (ok) lstrcpynW(out, v, cap);
  }
  free(a);
  return ok;
}

int ai_rank_candidates(const wchar_t *query, const wchar_t *const *cand, int n, int *order, DWORD timeoutMs) {
  if (!query || !cand || !order || n <= 0) return 0;
  if (n > 40) n = 40;
  size_t len = wcslen(query) + 100;
  for (int i = 0; i < n; i++) len += (cand[i] ? wcslen(cand[i]) : 0) + 12;
  wchar_t *q = (wchar_t *)malloc(len * sizeof(wchar_t));
  if (!q) return 0;
  size_t at = (size_t)_snwprintf(q, len, L"Ищем: %s\nКандидаты:\n", query);
  for (int i = 0; i < n && at < len; i++) at += (size_t)_snwprintf(q + at, len - at, L"%d. %s\n", i + 1, cand[i] ? cand[i] : L"");
  q[len - 1] = 0;
  wchar_t *a = NULL;
  int st = ai_ask_sync(
      L"Ты сопоставляешь позиции заводской номенклатуры (обозначения по ЕСКД, наименования, материалы по ГОСТ). "
      L"Выбери кандидатов, которые обозначают то же самое, что искомое, от самого подходящего. "
      L"Ответь только номерами через запятую, например: 3, 1. Если ни один не подходит — ответь 0.",
      q, timeoutMs ? timeoutMs : 60000, NULL, &a);
  free(q);
  int got = 0;
  if (st == AI_OK && a) {
    BOOL seen[41] = {0};
    for (wchar_t *p = a; *p && got < n;) {
      if (*p >= L'0' && *p <= L'9') {
        int v = (int)wcstol(p, &p, 10);
        if (v >= 1 && v <= n && !seen[v]) {
          seen[v] = TRUE;
          order[got++] = v - 1;
        }
      } else {
        p++;
      }
    }
  }
  free(a);
  return got;
}

/* --- скачивание ------------------------------------------------------------- */
typedef struct {
  volatile LONG active, cancel;
  volatile LONGLONG done, total; /* байт всего по плану */
  wchar_t msg[240];              /* что сейчас / чем кончилось */
  BOOL failed;
} AiDl;
static AiDl g_aiDl;
static HWND g_aiDlNotify; /* окно Настроек — ему WM_AI_UI */

static void ai_dl_say(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_aiDl.msg, 240, fmt, ap);
  va_end(ap);
  g_aiDl.msg[239] = 0;
  ai_log(L"%s", g_aiDl.msg);
  if (g_aiDlNotify) PostMessageW(g_aiDlNotify, WM_AI_UI, 0, 0);
}

static HINTERNET ai_net_open(void) {
  HINTERNET ses = WinHttpOpen(L"CursorPad/" APP_VERSION_STR L" (+https://github.com/pidrpen/ytaqq)",
                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses)
    ses = WinHttpOpen(L"CursorPad/" APP_VERSION_STR, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                      WINHTTP_NO_PROXY_BYPASS, 0);
  if (ses) {
    http_tune(ses);
    WinHttpSetTimeouts(ses, 10000, 15000, 30000, 60000);
  }
  return ses;
}

/* HTTPS GET; from > 0 — докачать с этого байта. В файл (дописывает) или в память */
static DWORD ai_net_get(HINTERNET ses, const wchar_t *host, const wchar_t *path, ULONGLONG from, HANDLE file,
                        char **mem, ULONGLONG memMax) {
  if (mem) *mem = NULL;
  DWORD status = 0;
  HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE)
                      : NULL;
  if (req) {
    wchar_t hdr[160] = L"Accept: */*\r\nAccept-Encoding: identity\r\n";
    if (from) _snwprintf(hdr + wcslen(hdr), 100, L"Range: bytes=%llu-\r\n", from);
    if (wcsstr(host, L"api.github.com")) wcscat(hdr, L"Accept: application/vnd.github+json\r\n");
    DWORD lvl = WINHTTP_AUTOLOGON_SECURITY_LEVEL_LOW; /* прокси завода — от имени пользователя Windows */
    WinHttpSetOption(req, WINHTTP_OPTION_AUTOLOGON_POLICY, &lvl, sizeof(lvl));
    if (!(WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
          WinHttpReceiveResponse(req, NULL))) {
      DWORD e = GetLastError();
      const wchar_t *why = wh_reason(e);
      ai_log(L"%s%s → %s (код %lu)", host, path, why ? why : L"нет ответа", e);
    } else {
      DWORD sl = sizeof(status);
      WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sl,
                          WINHTTP_NO_HEADER_INDEX);
      if (status != 200 && status != 206) ai_log(L"%s%s → HTTP %lu", host, path, status);
      if (status == 200 || status == 206) {
        size_t cap = mem ? 1 << 16 : 0, n = 0;
        char *buf = mem ? (char *)malloc(cap) : NULL;
        BYTE *chunk = (BYTE *)malloc(1 << 16);
        for (; chunk;) {
          if (g_aiDl.cancel) {
            status = 0;
            break;
          }
          DWORD got = 0;
          if (!WinHttpReadData(req, chunk, 1 << 16, &got)) {
            status = 0; /* обрыв — докачаем со следующей попытки */
            break;
          }
          if (!got) break;
          if (file) {
            DWORD w = 0;
            if (!WriteFile(file, chunk, got, &w, NULL) || w != got) {
              status = 1; /* диск — не сеть */
              break;
            }
            InterlockedExchangeAdd64(&g_aiDl.done, got);
          } else if (buf) {
            if (n + got + 1 > cap) {
              while (n + got + 1 > cap) cap *= 2;
              if (cap > memMax) {
                status = 0;
                break;
              }
              char *nb = (char *)realloc(buf, cap);
              if (!nb) break;
              buf = nb;
            }
            memcpy(buf + n, chunk, got);
            n += got;
          }
        }
        free(chunk);
        if (buf) {
          buf[n] = 0;
          if (status == 200 || status == 206) *mem = buf;
          else free(buf);
        }
      }
    }
  }
  if (req) WinHttpCloseHandle(req);
  if (con) WinHttpCloseHandle(con);
  return status;
}

static BOOL ai_sha256(const wchar_t *path, char out[65]) {
  out[0] = 0;
  HCRYPTPROV prov = 0;
  HCRYPTHASH h = 0;
  BOOL ok = FALSE;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  BYTE *buf = (BYTE *)malloc(1 << 20);
  if (buf && CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
      CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) {
    DWORD got = 0;
    ok = TRUE;
    while (ReadFile(f, buf, 1 << 20, &got, NULL) && got) {
      if (g_aiDl.cancel || !CryptHashData(h, buf, got, 0)) {
        ok = FALSE;
        break;
      }
    }
    BYTE d[32];
    DWORD dn = 32;
    if (ok && CryptGetHashParam(h, HP_HASHVAL, d, &dn, 0) && dn == 32)
      for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
    else ok = FALSE;
  }
  if (h) CryptDestroyHash(h);
  if (prov) CryptReleaseContext(prov, 0);
  free(buf);
  CloseHandle(f);
  return ok;
}

/* разобрать https://host/path */
static BOOL ai_split_url(const char *url, wchar_t *host, int hcap, wchar_t *path, int pcap) {
  if (strncmp(url, "https://", 8)) return FALSE;
  const char *h = url + 8, *sl = strchr(h, '/');
  if (!sl || sl - h >= hcap) return FALSE;
  char hb[128];
  memcpy(hb, h, sl - h);
  hb[sl - h] = 0;
  MultiByteToWideChar(CP_UTF8, 0, hb, -1, host, hcap);
  MultiByteToWideChar(CP_UTF8, 0, sl, -1, path, pcap);
  return TRUE;
}

/* скачать файл: .part с докачкой, размер, sha256, потом на место */
static BOOL ai_fetch(HINTERNET ses, const wchar_t *host, const wchar_t *path, const wchar_t *dest, ULONGLONG size,
                     const char *sha, const wchar_t *what) {
  wchar_t part[MAX_PATH + 8];
  _snwprintf(part, MAX_PATH + 8, L"%s.part", dest);
  for (int attempt = 0; attempt < 6; attempt++) {
    if (g_aiDl.cancel) return FALSE;
    ULONGLONG have = ai_file_size(part);
    if (have > size) {
      DeleteFileW(part);
      have = 0;
    }
    if (have < size) {
      HANDLE f = CreateFileW(part, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (f == INVALID_HANDLE_VALUE) {
        ai_dl_say(L"Не создать файл %s (код %lu)", part, GetLastError());
        return FALSE;
      }
      LARGE_INTEGER z = {{0}};
      SetFilePointerEx(f, z, NULL, FILE_END);
      ai_dl_say(attempt ? L"%s: докачиваю…" : L"%s: качаю…", what);
      DWORD st = ai_net_get(ses, host, path, have, f, NULL, 0);
      CloseHandle(f);
      if (st == 200 && have) { /* сервер не умеет докачку и отдал файл с начала — дописали лишнее */
        InterlockedExchangeAdd64(&g_aiDl.done, -(LONGLONG)ai_file_size(part));
        DeleteFileW(part);
        continue;
      }
      if (st == 1) {
        ai_dl_say(L"%s: не записать на диск — место кончилось?", what);
        return FALSE;
      }
      if (st != 200 && st != 206) {
        if (g_aiDl.cancel) return FALSE;
        if (st >= 400) {
          ai_dl_say(L"%s: сервер ответил %lu", what, st);
          return FALSE;
        }
        Sleep(2000u << (attempt < 4 ? attempt : 4)); /* сеть моргнула — ещё раз с того же места */
        continue;
      }
    }
    if (ai_file_size(part) != size) continue;
    ai_dl_say(L"%s: проверяю контрольную сумму…", what);
    char got[65];
    if (!ai_sha256(part, got)) return FALSE;
    if (_stricmp(got, sha)) {
      DeleteFileW(part);
      ai_dl_say(L"%s: контрольная сумма не сошлась, файл удалён. Попробуйте ещё раз.", what);
      return FALSE;
    }
    DeleteFileW(dest);
    if (!MoveFileW(part, dest)) {
      ai_dl_say(L"%s: не переименовать (код %lu)", what, GetLastError());
      return FALSE;
    }
    return TRUE;
  }
  ai_dl_say(L"%s: не скачалось — нет связи?", what);
  return FALSE;
}

static volatile LONG g_aiLoraTried;
static wchar_t g_aiLoraDest[MAX_PATH];

static DWORD WINAPI ai_lora_thread(LPVOID p) {
  (void)p;
  const AiFile *a = &kAiDrawLora;
  static const wchar_t *kHost[2] = {L"raw.githubusercontent.com", L"github.com"};
  wchar_t part[MAX_PATH + 8], path[400];
  _snwprintf(part, MAX_PATH + 8, L"%s.part", g_aiLoraDest);
  HINTERNET ses = ai_net_open();
  for (int i = 0; ses && i < 2; i++) {
    _snwprintf(path, 400, i ? L"/%s/raw/%s/%s" : L"/%s/%s/%s", a->repo, a->rev, a->file);
    HANDLE f = CreateFileW(part, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) break;
    DWORD st = ai_net_get(ses, kHost[i], path, 0, f, NULL, 0);
    CloseHandle(f);
    ULONGLONG got = ai_file_size(part);
    InterlockedExchangeAdd64(&g_aiDl.done, -(LONGLONG)got); /* счётчик — только для «Скачать» */
    char sha[65];
    if (st == 200 && got == a->size && ai_sha256(part, sha) && !_stricmp(sha, a->sha) &&
        MoveFileExW(part, g_aiLoraDest, MOVEFILE_REPLACE_EXISTING)) {
      ai_log(L"Адаптер чертежей скачан: %s — включится при следующем запуске модели", g_aiLoraDest);
      WinHttpCloseHandle(ses);
      return 0;
    }
    ai_log(L"Адаптер чертежей: https://%s%s — ответ %lu, %llu байт, не подошёл", kHost[i], path, st, got);
  }
  DeleteFileW(part);
  if (ses) WinHttpCloseHandle(ses);
  return 0;
}

/* модель уровня есть, а адаптера чертежей рядом нет — скачать в фоне, раз за запуск программы */
static void ai_lora_kick(const wchar_t *model) {
  const wchar_t *name = wcsrchr(kAiDrawLora.file, L'/');
  wchar_t dest[MAX_PATH];
  lstrcpynW(dest, model, MAX_PATH);
  wchar_t *sl = wcsrchr(dest, L'\\');
  if (!name || !sl || (size_t)(sl - dest) + wcslen(name) + 1 >= MAX_PATH) return;
  wcscpy(sl + 1, name + 1);
  /* закреплённого адаптера ещё нет (прежнее поколение рядом не в счёт) — скачать */
  if (ai_file_size(dest) == kAiDrawLora.size || InterlockedExchange(&g_aiLoraTried, 1)) return;
  lstrcpynW(g_aiLoraDest, dest, MAX_PATH);
  HANDLE t = CreateThread(NULL, 0, ai_lora_thread, NULL, 0, NULL);
  if (t) CloseHandle(t);
}

/* последний выпуск llama.cpp: ссылка на архив, размер и sha256 из GitHub */
typedef struct {
  char url[512];
  ULONGLONG size;
  char sha[65];
  char tag[48];
} AiAsset;

static BOOL ai_has(const char *s, int n, const char *w) {
  int wl = (int)strlen(w);
  for (int i = 0; i + wl <= n; i++)
    if (!memcmp(s + i, w, wl)) return TRUE;
  return FALSE;
}

/* имя архива llama.cpp для Windows x64 с нужным движком: «llama-b8000-bin-win-cpu-x64.zip»,
   в старых выпусках «…-bin-win-avx2-x64.zip», для видеокарты «…-bin-win-vulkan-x64.zip».
   Сверяем по частям, а не целиком: имена у llama.cpp меняются от выпуска к выпуску */
static BOOL ai_llama_name_ok(const char *name, int nlen, BOOL gpu) {
  char n[256];
  if (nlen <= 0 || nlen >= (int)sizeof(n)) return FALSE;
  for (int i = 0; i < nlen; i++) n[i] = (char)tolower((unsigned char)name[i]);
  n[nlen] = 0;
  if (strncmp(n, "llama-", 6) || !strstr(n, "bin-win") || !strstr(n, "x64") || nlen < 4 ||
      strcmp(n + nlen - 4, ".zip"))
    return FALSE;
  return gpu ? strstr(n, "vulkan") != NULL : (strstr(n, "-cpu-") || strstr(n, "-avx2-")) != 0;
}

/* последний подходящий архив в ответе API (один выпуск или список выпусков, новые первыми).
   Поля файла: name, …, size, digest, …, browser_download_url — size и digest берём
   последние перед ссылкой */
static BOOL ai_parse_llama(const char *js, BOOL gpu, AiAsset *a, char *names, int ncap) {
  memset(a, 0, sizeof(*a));
  if (names && ncap) names[0] = 0;
  const char *prev = js, *u;
  while ((u = strstr(prev, "\"browser_download_url\""))) {
    const char *q = strchr(u + 22, '"');
    const char *end = q ? strchr(q + 1, '"') : NULL;
    if (!end) break;
    const char *sl = q + 1, *s2;
    while ((s2 = memchr(sl, '/', end - sl))) sl = s2 + 1;
    int nlen = (int)(end - sl);
    if (names && ai_has(sl, nlen, "win") && (int)strlen(names) + nlen + 3 < ncap) { /* для отчёта */
      strncat(names, sl, nlen);
      strcat(names, "; ");
    }
    if (ai_llama_name_ok(sl, nlen, gpu) && end - q - 1 < (int)sizeof(a->url)) {
      memcpy(a->url, q + 1, end - q - 1);
      a->url[end - q - 1] = 0;
      char *seg = (char *)malloc(u - prev + 1);
      if (seg) {
        memcpy(seg, prev, u - prev);
        seg[u - prev] = 0;
        const char *sz = NULL, *dg = NULL, *t;
        for (t = seg; (t = strstr(t, "\"size\"")); t += 6) sz = t;
        for (t = seg; (t = strstr(t, "\"digest\"")); t += 8) dg = t;
        if (sz) {
          const char *c = strchr(sz + 6, ':');
          if (c) a->size = _strtoui64(c + 1, NULL, 10);
        }
        if (dg) {
          const char *d = json_find_string(dg, "digest");
          if (d && !strncmp(d, "sha256:", 7)) sscanf(d + 7, "%64[0-9a-fA-F]", a->sha);
        }
        free(seg);
      }
      /* тег выпуска — из ссылки …/download/<тег>/… */
      const char *dl = strstr(a->url, "/download/");
      if (dl) sscanf(dl + 10, "%47[^/]", a->tag);
      return TRUE;
    }
    prev = end;
  }
  return FALSE;
}

static BOOL ai_find_llama(HINTERNET ses, BOOL gpu, AiAsset *a) {
  static const wchar_t *kApi[2] = {L"/repos/ggml-org/llama.cpp/releases/latest",
                                   L"/repos/ggml-org/llama.cpp/releases?per_page=8"};
  for (int k = 0; k < 2; k++) {
    char *js = NULL;
    DWORD st = ai_net_get(ses, L"api.github.com", kApi[k], 0, NULL, &js, 16u << 20);
    if (st != 200 || !js) {
      free(js);
      continue;
    }
    char names[600];
    BOOL ok = ai_parse_llama(js, gpu, a, names, sizeof(names));
    if (!ok) {
      char head[160];
      int i = 0;
      for (; js[i] && i < 159; i++) head[i] = js[i] < 32 ? ' ' : js[i];
      head[i] = 0;
      ai_log(L"GitHub %s: %u байт, нужного архива нет. Файлы для win: %hs. Начало ответа: %hs", kApi[k],
             (unsigned)strlen(js), names[0] ? names : "нет", head);
    }
    free(js);
    if (!ok) continue;
    ai_log(L"llama.cpp %hs: %hs, %llu байт, sha256 %hs", a->tag, a->url, a->size, a->sha[0] ? a->sha : "нет");
    if (!a->size || strlen(a->sha) != 64) {
      ai_dl_say(L"llama.cpp: GitHub не дал размер или контрольную сумму архива");
      return FALSE;
    }
    return TRUE;
  }
  ai_dl_say(L"llama.cpp не нашёлся на GitHub. Скачайте архив вручную и укажите его кнопкой «llama.cpp…»");
  return FALSE;
}

/* распаковать zip встроенным в Windows 10/11 tar.exe (или PowerShell) */
static BOOL ai_unzip(const wchar_t *zip, const wchar_t *dir) {
  wchar_t sys[MAX_PATH], cmd[MAX_PATH * 3 + 200];
  GetSystemDirectoryW(sys, MAX_PATH);
  CreateDirectoryW(dir, NULL);
  for (int k = 0; k < 2; k++) {
    if (k == 0) _snwprintf(cmd, MAX_PATH * 3 + 200, L"\"%s\\tar.exe\" -xf \"%s\" -C \"%s\"", sys, zip, dir);
    else
      _snwprintf(cmd, MAX_PATH * 3 + 200,
                 L"\"%s\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive -Command "
                 L"\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
                 sys, zip, dir);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) continue;
    WaitForSingleObject(pi.hProcess, 180000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ai_log(L"Распаковка %s: код %lu", k ? L"PowerShell" : L"tar.exe", code);
    if (code == 0) return TRUE;
  }
  return FALSE;
}

static BOOL ai_free_space(const wchar_t *dir, ULONGLONG need, ULONGLONG *have) {
  ULARGE_INTEGER fr;
  if (!GetDiskFreeSpaceExW(dir, &fr, NULL, NULL)) return TRUE; /* не узнать — пусть пробует */
  *have = fr.QuadPart;
  return fr.QuadPart >= need + (300ull << 20);
}

static void ai_mkdirs(void) {
  wchar_t d[MAX_PATH];
  ai_dir(d, NULL);
  CreateDirectoryW(d, NULL);
  ai_dir(d, L"models");
  CreateDirectoryW(d, NULL);
  _snwprintf(d, MAX_PATH, L"%s\\ai\\models\\%s", g_dataDir, kAiTiers[g_aiTier].dir);
  CreateDirectoryW(d, NULL);
}

/* скачать и распаковать llama.cpp (для видеокарты — сборку Vulkan) */
static BOOL ai_get_llama(HINTERNET ses, BOOL gpu) {
  AiAsset la;
  wchar_t exe[MAX_PATH], host[128], path[512], zip[MAX_PATH], dir[MAX_PATH];
  ai_dl_say(gpu ? L"Ищу llama.cpp для видеокарты…" : L"Ищу llama.cpp…");
  if (!ai_find_llama(ses, gpu, &la)) return FALSE;
  if (!ai_split_url(la.url, host, 128, path, 512)) {
    ai_dl_say(L"llama.cpp: странная ссылка на архив");
    return FALSE;
  }
  ai_dir(dir, gpu ? L"llama-vk" : L"llama");
  ai_dir(zip, gpu ? L"llama-vk.zip" : L"llama.zip");
  InterlockedExchangeAdd64(&g_aiDl.total, (LONGLONG)la.size);
  if (!ai_fetch(ses, host, path, zip, la.size, la.sha, L"llama.cpp")) return FALSE;
  ai_dl_say(L"Распаковываю llama.cpp…");
  if (!ai_unzip(zip, dir) || !ai_server_exe(gpu, exe)) {
    ai_dl_say(L"Не распаковался архив llama.cpp — распакуйте %s в %s вручную", zip, dir);
    return FALSE;
  }
  ai_log(L"llama.cpp %hs распакован: %s", la.tag, exe);
  DeleteFileW(zip);
  return TRUE;
}

static DWORD WINAPI ai_dl_thread(LPVOID p) {
  (void)p;
  int tier = g_aiTier;
  BOOL gpu = g_aiGpu;
  const AiTier *t = &kAiTiers[tier];
  g_aiDl.failed = TRUE;
  ai_mkdirs();
  HINTERNET ses = ai_net_open();
  if (!ses) {
    ai_dl_say(L"Нет WinHTTP (код %lu)", GetLastError());
    goto out;
  }
  /* план: что ещё не скачано */
  ULONGLONG need = 0;
  BOOL needModel[2] = {FALSE, FALSE};
  for (int i = 0; i < t->nf; i++) {
    if (ai_tier_file_ok(tier, i)) continue;
    wchar_t f[MAX_PATH], part[MAX_PATH + 8];
    ai_model_file(tier, i, f);
    _snwprintf(part, MAX_PATH + 8, L"%s.part", f);
    ULONGLONG have = ai_file_size(part);
    needModel[i] = TRUE;
    need += t->f[i].size;
    if (have <= t->f[i].size) InterlockedExchangeAdd64(&g_aiDl.done, (LONGLONG)have);
  }
  g_aiDl.total = need + g_aiDl.done;
  {
    wchar_t d[MAX_PATH];
    ULONGLONG have = 0, want = need + (200ull << 20); /* и на llama.cpp */
    ai_dir(d, NULL);
    if (!ai_free_space(d, want, &have)) {
      ai_dl_say(L"Мало места на диске: нужно %.1f ГБ, свободно %.1f ГБ", (want + (300ull << 20)) / 1e9, have / 1e9);
      goto out;
    }
  }
  BOOL llamaFail = FALSE;
  /* сначала llama.cpp: он маленький, и если не скачается — видно сразу, а не после 2 ГБ модели */
  {
    wchar_t exe[MAX_PATH];
    /* First download of a newer architecture refreshes the managed engine too.
       An old existing EXE is not evidence that it supports qwen35 / Vulkan GDN.
       A manually selected engine is retained and tested by normal startup. */
    if (!ai_server_exe(gpu, exe) || ((tier == 1 || tier == 2) && (needModel[0] || needModel[1]) && !g_aiSrvExe[0])) {
      if (!g_aiSrvExe[0]) ai_server_stop(); /* release only our managed engine/DLLs before updating */
      BOOL ok = ai_get_llama(ses, gpu);
      if (!ok && gpu && !g_aiDl.cancel && !ai_server_exe(FALSE, exe)) {
        ai_log(L"Сборка для видеокарты не скачалась — беру сборку для процессора");
        ok = ai_get_llama(ses, FALSE);
      }
      llamaFail = !ok && !ai_server_exe(FALSE, exe);
      if (g_aiDl.cancel) goto out;
    }
  }
  for (int i = 0; i < t->nf; i++) {
    if (!needModel[i]) continue;
    wchar_t f[MAX_PATH], path[600], what[120];
    ai_model_file(tier, i, f);
    _snwprintf(path, 600, L"/%s/resolve/%s/%s", t->f[i].repo, t->f[i].rev, t->f[i].file);
    _snwprintf(what, 120, t->nf > 1 ? L"%s (часть %d из %d)" : L"%s", t->model, i + 1, t->nf);
    ai_log(L"Модель: https://huggingface.co%s → %s", path, f);
    if (!ai_fetch(ses, L"huggingface.co", path, f, t->f[i].size, t->f[i].sha, what)) goto out;
    wchar_t ok[MAX_PATH + 4];
    _snwprintf(ok, MAX_PATH + 4, L"%s.ok", f);
    HANDLE h = CreateFileW(ok, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      DWORD w;
      WriteFile(h, t->f[i].sha, 64, &w, NULL);
      CloseHandle(h);
    }
  }
  if (llamaFail) {
    ai_dl_say(L"Модель %s скачана, а llama.cpp — нет. Скачайте llama-…-bin-win-cpu-x64.zip с "
              L"github.com/ggml-org/llama.cpp/releases и укажите его кнопкой «llama.cpp…»", t->model);
    goto out;
  }
  ai_dl_say(L"Модель сохранена. Подготавливаю QwenPaw, XlsxWriter и Filesystem MCP…");
  {
    AiJob job = {0}; job.externalCancel = &g_aiDl.cancel;
    wchar_t root[MAX_PATH];
    wchar_t python[MAX_PATH];
    int status = at_prepare(&job, GetTickCount() + 600000, root, python);
    if (status != AI_OK) {
      ai_dl_say(L"Модель сохранена, но локальные инструменты ещё не готовы. %s", job.text ? job.text : L"Повторите скачивание при подключённом интернете.");
      free(job.text); goto out;
    }
    free(job.text);
  }
  g_aiDl.failed = FALSE;
  ai_dl_say(L"Готово: %s, QwenPaw и локальные инструменты подготовлены. Включите ИИ и нажмите «Проверить».", t->model);
out:
  if (ses) WinHttpCloseHandle(ses);
  if (g_aiDl.cancel) ai_dl_say(L"Скачивание остановлено. Скачанное сохранено — продолжится с того же места.");
  InterlockedExchange(&g_aiDl.active, 0);
  if (g_aiDlNotify) PostMessageW(g_aiDlNotify, WM_AI_UI, 0, 0);
  return 0;
}

/* кнопка «Скачать»: второй раз — отменить */
static void ai_download_toggle(HWND notify) {
  if (g_aiDl.active) {
    InterlockedExchange(&g_aiDl.cancel, 1);
    return;
  }
  if (!g_dataDir[0]) return;
  g_aiDlNotify = notify;
  g_aiDl.cancel = 0;
  g_aiDl.done = 0;
  g_aiDl.total = 0;
  g_aiDl.failed = FALSE;
  g_aiDl.msg[0] = 0;
  InterlockedExchange(&g_aiDl.active, 1);
  HANDLE th = CreateThread(NULL, 0, ai_dl_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else InterlockedExchange(&g_aiDl.active, 0);
}

#ifdef SET_TEST
/* проверочная сборка: --aitest отчёт.txt [модель.gguf] — спросить, поправить поле,
   выбрать кандидата, отменить и дождаться таймаута; всё в отчёт (UTF-8) */
static wchar_t g_aiTestOut[MAX_PATH];
static char g_aiTestRep[16000];
static volatile LONG g_aiTestFlag;

static void ai_rep(const char *fmt, ...) {
  size_t n = strlen(g_aiTestRep);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_aiTestRep + n, sizeof(g_aiTestRep) - n, fmt, ap);
  va_end(ap);
  HANDLE h = CreateFileW(g_aiTestOut, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD w;
    WriteFile(h, g_aiTestRep, (DWORD)strlen(g_aiTestRep), &w, NULL);
    CloseHandle(h);
  }
}

static DWORD WINAPI ai_flag_later(LPVOID p) {
  Sleep((DWORD)(INT_PTR)p);
  InterlockedExchange(&g_aiTestFlag, 1);
  return 0;
}

static DWORD WINAPI ai_selftest_thread(LPVOID p) {
  (void)p;
  wchar_t *a = NULL;
  char u[2000];
  DWORD t0 = GetTickCount();
  int st = ai_ask_sync(L"Отвечай кратко, по-русски.", L"Расшифруй обозначение «Круг 20 ГОСТ 2590-2006» одной строкой.",
                       600000, NULL, &a);
  WideCharToMultiByte(CP_UTF8, 0, a ? a : L"(null)", -1, u, sizeof(u), NULL, NULL);
  ai_rep("ask: st=%d ms=%lu port=%d\n  %s\n", st, GetTickCount() - t0, g_aiPort, u);
  free(a);
  wchar_t out[200];
  t0 = GetTickCount();
  BOOL ok = ai_refine_field(L"Материал", L"Круг 2О ГОСТ 259О-2ОО6 / 45 ГОСТ 1О5О-2О13", L"", out, 200, 120000);
  WideCharToMultiByte(CP_UTF8, 0, ok ? out : L"-", -1, u, sizeof(u), NULL, NULL);
  ai_rep("refine: ok=%d ms=%lu\n  %s\n", ok, GetTickCount() - t0, u);
  const wchar_t *cand[] = {L"Лист 2 ГОСТ 19903-2015", L"Шестигранник 20 ГОСТ 2879-2006",
                           L"Круг 20-В ГОСТ 2590-2006 / 45-2ГП ГОСТ 1050-2013", L"Круг 22 ГОСТ 2590-2006 / Ст3"};
  int ord[4] = {-1, -1, -1, -1};
  t0 = GetTickCount();
  int n = ai_rank_candidates(L"Пруток круглый d20 сталь 45", cand, 4, ord, 120000);
  ai_rep("rank: n=%d ms=%lu order=%d,%d,%d,%d\n", n, GetTickCount() - t0, ord[0], ord[1], ord[2], ord[3]);
  /* отмена посреди длинного ответа */
  g_aiTestFlag = 0;
  HANDLE th = CreateThread(NULL, 0, ai_flag_later, (LPVOID)(INT_PTR)1500, 0, NULL);
  if (th) CloseHandle(th);
  t0 = GetTickCount();
  st = ai_ask_sync(L"Пиши подробно.", L"Расскажи историю ГОСТ на 2000 слов.", 600000, &g_aiTestFlag, &a);
  ai_rep("cancel: st=%d (ждём %d) ms=%lu\n", st, AI_CANCELLED, GetTickCount() - t0);
  free(a);
  t0 = GetTickCount();
  st = ai_ask_sync(L"Пиши подробно.", L"Расскажи историю ГОСТ на 2000 слов.", 2500, NULL, &a);
  ai_rep("timeout: st=%d (ждём %d) ms=%lu\n", st, AI_TIMEOUT, GetTickCount() - t0);
  free(a);
  t0 = GetTickCount();
  st = ai_ask_sync(L"Отвечай одним словом.", L"Столица России?", 120000, NULL, &a);
  WideCharToMultiByte(CP_UTF8, 0, a ? a : L"(null)", -1, u, sizeof(u), NULL, NULL);
  ai_rep("after abandoned: st=%d ms=%lu %s\n", st, GetTickCount() - t0, u);
  free(a);
  st = ai_ask_image_sync(L"Что на картинке?", L"C:\\nope.png", 1000, NULL, &a);
  ai_rep("image without vision: st=%d (ждём %d)\n", st, AI_NO_VISION);
  free(a);
  ai_rep("server alive before shutdown: %d\n", g_aiProc && WaitForSingleObject(g_aiProc, 0) == WAIT_TIMEOUT);
  ai_shutdown();
  ai_rep("server alive after shutdown: %d\nDONE\n", g_aiProc != NULL);
  return 0;
}

/* --aidl отчёт.txt уровень [gpu]: скачать как кнопкой и записать журнал */
static DWORD WINAPI ai_dltest_thread(LPVOID p) {
  (void)p;
  ai_dl_thread(NULL);
  char u[30000];
  AcquireSRWLockShared(&g_aiLogLock);
  WideCharToMultiByte(CP_UTF8, 0, g_aiLog, -1, u, sizeof(u), NULL, NULL);
  ReleaseSRWLockShared(&g_aiLogLock);
  ai_rep("%s\nfailed=%d\nDONE\n", u, g_aiDl.failed);
  return 0;
}

static void ai_dltest(const wchar_t *out, int tier, BOOL gpu) {
  lstrcpynW(g_aiTestOut, out, MAX_PATH);
  g_aiTier = tier >= 0 && tier < AI_TIERS ? tier : 0;
  g_aiGpu = gpu;
  g_aiDl.active = 1;
  HANDLE th = CreateThread(NULL, 0, ai_dltest_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
}

static void ai_selftest(const wchar_t *out, const wchar_t *model) {
  lstrcpynW(g_aiTestOut, out, MAX_PATH);
  g_aiOn = TRUE;
  if (model) lstrcpynW(g_aiModel, model, MAX_PATH);
  wchar_t *a = NULL;
  int st = ai_ask_sync(L"x", L"y", 1000, NULL, &a); /* из потока окон — нельзя */
  free(a);
  ai_rep("ui-thread sync: st=%d (ждём %d) ready=%d\n", st, AI_BUSY, ai_ready());
  HANDLE th = CreateThread(NULL, 0, ai_selftest_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
}
#endif
