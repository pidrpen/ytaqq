/* ai.h — локальный ИИ CursorPad (с 2026.10.16.1).

   Всё на этом компьютере: модель GGUF крутит llama.cpp (llama-server.exe),
   который CursorPad сам запускает на 127.0.0.1 и гасит, когда ИИ долго не
   нужен или программа закрывается. В интернет ничего не уходит; сеть нужна
   только один раз — скачать модель и llama.cpp по кнопке в Настройках, вкладка
   «ИИ». По умолчанию ИИ выключен.

   Как пользоваться из другой части программы:
   - ai_ready() — можно ли спрашивать (включён и модель скачана). Нет — работайте
     по своим правилам, как без ИИ.
   - Из потока интерфейса — только ai_ask_async: ответ придёт сообщением в окно.
     Синхронные ai_ask_sync, ai_refine_field, ai_rank_candidates из потока
     интерфейса сразу возвращают AI_BUSY/FALSE/0: там же крутится хук мыши, и
     поток нельзя держать секундами.
   - Первый запрос после простоя ждёт загрузки модели (секунд 5–30 на CPU).
   Строки — UTF-16 (wchar_t), как во всей программе. */
#ifndef CURSORPAD_AI_H
#define CURSORPAD_AI_H

enum {
  AI_OK = 0,
  AI_OFF,       /* выключен в Настройках */
  AI_NO_MODEL,  /* модель или llama-server.exe не скачаны */
  AI_NO_SERVER, /* llama-server не запустился */
  AI_TIMEOUT,
  AI_CANCELLED,
  AI_ERROR,     /* сервер ответил ошибкой или ответ не разобрать */
  AI_BUSY,      /* синхронный вызов из потока интерфейса или нет памяти */
  AI_NO_VISION  /* картинку спросили, а модель только текстовая */
};

/* ответ ai_ask_async: lParam сообщения; освободить ai_result_free */
typedef struct AiResult {
  int id;        /* что вернул ai_ask_async */
  int status;    /* AI_OK… */
  wchar_t *text; /* ответ (AI_OK) или пояснение ошибки; не NULL */
  void *user;    /* что передали в ai_ask_async */
  DWORD ms;      /* сколько ушло */
} AiResult;

BOOL ai_ready(void);
const wchar_t *ai_status_text(int status); /* «ИИ выключен», «нет модели»… */

/* Спросить в фоне. instruction — что сделать (роль system), text — с чем.
   Ответ — PostMessage(notify, msg, 0, (LPARAM)AiResult*). timeoutMs — на всё,
   включая загрузку модели; 0 — 120 с. Возвращает id (>0) или 0, если не
   спросить (ИИ выключен…): тогда сообщения не будет. */
int ai_ask_async(const wchar_t *instruction, const wchar_t *text, DWORD timeoutMs, HWND notify, UINT msg,
                 void *user);
void ai_cancel(int id); /* ответ всё равно придёт — со статусом AI_CANCELLED */
void ai_result_free(AiResult *r);

/* Спросить и дождаться — только из рабочих потоков. *answer — malloc, освободить
   free(); при ошибке там пояснение (или NULL). cancel — необязательный флаг
   отмены: стал не 0 — запрос обрывается. */
int ai_ask_sync(const wchar_t *instruction, const wchar_t *text, DWORD timeoutMs, volatile LONG *cancel,
                wchar_t **answer);
/* то же с картинкой (PNG/JPG/BMP-файл); нужна модель уровня «С картинками» */
int ai_ask_image_sync(const wchar_t *instruction, const wchar_t *imagePath, DWORD timeoutMs,
                      volatile LONG *cancel, wchar_t **answer);

/* Точки расширения для правил (распознавание чертежа, сопоставление с Союзом).
   Обе — из рабочего потока; ИИ выключен или не ответил — FALSE / 0, правила
   оставляют своё.

   ai_refine_field: field — что за поле («Обозначение», «Наименование»,
   «Материал»…), raw — что нашли правила (может быть с ошибками распознавания),
   context — окружающий текст или пусто. В out — исправленное значение одной
   строкой; TRUE, если ИИ ответил. */
BOOL ai_refine_field(const wchar_t *field, const wchar_t *raw, const wchar_t *context, wchar_t *out, int cap,
                     DWORD timeoutMs);
/* ai_rank_candidates: какие из n кандидатов (до 40) лучше подходят к query.
   В order — индексы кандидатов от лучшего; возвращает, сколько записано
   (не больше n, неподходящих ИИ может не назвать). 0 — оставить свой порядок. */
int ai_rank_candidates(const wchar_t *query, const wchar_t *const *cand, int n, int *order, DWORD timeoutMs);

void ai_shutdown(void); /* при выходе: погасить llama-server */

#endif
