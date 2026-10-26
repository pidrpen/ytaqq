/* file_ask.c — «Спросить ИИ»: прочитать файл и перевести его в другой формат
   по просьбе словами, без F6 и без окна обработки файлов (с 2026.10.26).

     «прочитай C:\Сканы\письмо.pdf»            — текст в чат (скан — Tesseract)
     «переведи C:\Работа\Отчёт.docx в pdf»     — копия рядом с исходником
     «сделай из C:\Сканы\лист.tif ворд»        — распознать и сохранить Word
     «а теперь в txt», «покажи в проводнике»   — тот же файл, без пути

   Здесь только разбор просьбы и выбор пути — без Windows, проверяет
   file_ask_test.c в CI. Выполняет ai_file_ask.c: сканы читает тот же
   Tesseract, что «Выделить и прочитать» (F6) и «Распознать чертёж»,
   конвертирует тот же исполнитель, что кольцо Shift-перетаскивания и окно
   «Обработка файлов». Чего они не умеют, того и здесь нет: Excel и таблицы
   из сканов собирает помощник с инструментами (FQA_AGENT), остальное —
   честный отказ со списком того, что можно. */

enum { FQ_NONE, FQ_PDF, FQ_TXT, FQ_CSV, FQ_PNG, FQ_JPG, FQ_BMP, FQ_TIF, FQ_DOCX, FQ_XLSX, FQ_ZIP, FQ_N };
static const wchar_t *const kFqName[FQ_N] = {L"", L"PDF", L"TXT", L"CSV", L"PNG", L"JPG", L"BMP", L"TIFF",
                                             L"Word", L"Excel", L"ZIP"};

/* что за файл — по расширению */
enum { FQK_OTHER, FQK_IMAGE, FQK_PDF, FQK_WORD, FQK_EXCEL, FQK_TEXT };

/* что просят */
enum { FQW_NO, FQW_READ, FQW_CONVERT, FQW_REVEAL };

/* чем выполнять */
enum {
  FQA_NONE,  /* так не умеем — ответ со списком, что можно */
  FQA_SAME,  /* файл уже в этом формате */
  FQA_TEXT,  /* прочитать и показать текст в чате */
  FQA_SAVE,  /* прочитать (скан — распознать) и записать .txt / .docx */
  FQA_BENCH, /* исполнитель окна «Обработка файлов», действие — *bench */
  FQA_AGENT  /* составная задача — помощнику с инструментами */
};

typedef struct {
  int want;    /* FQW_… */
  int target;  /* FQ_…; FQ_NONE — просто прочитать */
  int desktop; /* «на рабочий стол» — результат туда, а не рядом с исходником */
  int drawing; /* «чертёж», «штамп» — скан читать как чертёж: графы штампа и спецификация */
} FqAsk;

static wchar_t fq_low(wchar_t c) {
  if (c >= L'A' && c <= L'Z') return (wchar_t)(c + 32);
  if (c >= 0x410 && c <= 0x42F) return (wchar_t)(c + 32);
  if (c == 0x401 || c == 0x451) return 0x435; /* ё → е */
  return c;
}
static int fq_letter(wchar_t c) {
  c = fq_low(c);
  return (c >= L'a' && c <= L'z') || (c >= 0x430 && c <= 0x44F) || (c >= L'0' && c <= L'9');
}

static int fq_ext_is(const wchar_t *path, const wchar_t *const *list, int n) {
  const wchar_t *slash = wcsrchr(path, L'\\'), *dot = wcsrchr(path, L'.');
  if (!dot || (slash && dot < slash)) return 0;
  dot++;
  for (int i = 0; i < n; i++) {
    const wchar_t *a = dot, *b = list[i];
    while (*a && *b && fq_low(*a) == *b) a++, b++;
    if (!*a && !*b) return 1;
  }
  return 0;
}
#define FQ_EXT(path, ...) fq_ext_is(path, (const wchar_t *const[]){__VA_ARGS__}, \
                                    (int)(sizeof((const wchar_t *const[]){__VA_ARGS__}) / sizeof(wchar_t *)))

static int fq_kind(const wchar_t *path) {
  if (FQ_EXT(path, L"png", L"jpg", L"jpeg", L"bmp", L"tif", L"tiff", L"gif")) return FQK_IMAGE;
  if (FQ_EXT(path, L"pdf")) return FQK_PDF;
  if (FQ_EXT(path, L"docx", L"doc", L"docm", L"rtf", L"odt")) return FQK_WORD;
  if (FQ_EXT(path, L"xlsx", L"xls", L"xlsm", L"xlsb", L"ods")) return FQK_EXCEL;
  if (FQ_EXT(path, L"txt", L"csv", L"tsv", L"log", L"md")) return FQK_TEXT;
  return FQK_OTHER;
}

/* у картинки — какой из форматов FQ_PNG…FQ_TIF; иначе FQ_NONE */
static int fq_image_format(const wchar_t *path) {
  if (FQ_EXT(path, L"png")) return FQ_PNG;
  if (FQ_EXT(path, L"jpg", L"jpeg")) return FQ_JPG;
  if (FQ_EXT(path, L"bmp")) return FQ_BMP;
  if (FQ_EXT(path, L"tif", L"tiff")) return FQ_TIF;
  return FQ_NONE;
}

/* Путь в тексте: «C:\…» или «\\сервер\…». Имя с пробелами укорачивается по
   словам с конца, пока is() не узнает файл (dir=0) или папку (dir=1).
   [*b, *e) — где путь стоит в тексте; from — откуда искать. */
typedef int (*FqExists)(const wchar_t *path, int dir);
static int fq_find_path(const wchar_t *q, int from, int dir, FqExists is, int *b, int *e) {
  for (const wchar_t *p = q + from; *p; p++) {
    int drive = fq_letter(p[0]) && !(p[0] >= L'0' && p[0] <= L'9') && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/');
    int unc = p[0] == L'\\' && p[1] == L'\\' && fq_letter(p[2]);
    if ((!drive && !unc) || (p > q && fq_letter(p[-1]))) continue;
    wchar_t buf[260];
    int n = 0;
    while (p[n] && p[n] != L'\n' && p[n] != L'\r' && p[n] != L'"' && p[n] != 0xAB && p[n] != 0xBB && n < 259)
      buf[n] = p[n] == L'/' ? L'\\' : p[n], n++;
    buf[n] = 0;
    while (n > 3) {
      while (n > 3 && (wcschr(L" .,;:!?)", buf[n - 1]) || (dir && buf[n - 1] == L'\\'))) buf[--n] = 0;
      if (is(buf, dir)) {
        *b = (int)(p - q);
        *e = *b + n;
        return 1;
      }
      while (n > 3 && buf[n - 1] != L' ') n--;
      buf[n] = 0;
    }
  }
  return 0;
}

/* текст в нижнем регистре; пути (span — пары начало/конец) — знаком 1: их
   расширения и папки («D:\PDF») — не цель, а «из» перед путём — про путь */
static void fq_fold(const wchar_t *q, const int *span, int nspan, wchar_t *out, int cap) {
  int i = 0;
  for (; q[i] && i < cap - 1; i++) {
    int in = 0;
    for (int k = 0; k + 1 < nspan * 2 && !in; k += 2) in = i >= span[k] && i < span[k + 1];
    out[i] = in ? 1 : fq_low(q[i]);
  }
  out[i] = 0;
}

static int fq_has(const wchar_t *f, const wchar_t *w) { return wcsstr(f, w) != NULL; }

static int fq_any(const wchar_t *f, const wchar_t *const *list, int n) {
  for (int i = 0; i < n; i++)
    if (fq_has(f, list[i])) return 1;
  return 0;
}
#define FQ_ANY(f, ...) fq_any(f, (const wchar_t *const[]){__VA_ARGS__}, \
                              (int)(sizeof((const wchar_t *const[]){__VA_ARGS__}) / sizeof(wchar_t *)))

/* слово с начала: «ворд» в «ворде», но не в «пароворд»; end — и конец слова */
static const wchar_t *fq_word_at(const wchar_t *f, const wchar_t *w, int end, const wchar_t *from) {
  size_t n = wcslen(w);
  for (const wchar_t *p = from; (p = wcsstr(p, w)) != NULL; p++)
    if ((p == f || !fq_letter(p[-1])) && (!end || !fq_letter(p[n]))) return p;
  return NULL;
}

/* слов в [b, e) */
static int fq_count(const wchar_t *f, int b, int e) {
  int n = 0;
  for (int i = b; i < e; i++)
    if (fq_letter(f[i]) && (i == 0 || !fq_letter(f[i - 1]))) n++;
  return n;
}

typedef struct {
  const wchar_t *w;
  int target;
  int end;  /* слово целиком: «doc» не в «docs», «tif» не в «tiff» — у них свои строки */
  int into; /* только после «в …»: «прочитай картинку», «прочитай таблицу» — это про исходник */
} FqWord;
static const FqWord kFqWords[] = {
    {L"pdf", FQ_PDF, 0, 0},        {L"пдф", FQ_PDF, 0, 0},      {L"txt", FQ_TXT, 1, 0},
    {L"тхт", FQ_TXT, 1, 0},        {L"текстов", FQ_TXT, 0, 1},  {L"текст", FQ_TXT, 1, 1},
    {L"csv", FQ_CSV, 1, 0},        {L"цсв", FQ_CSV, 1, 0},      {L"png", FQ_PNG, 1, 0},
    {L"пнг", FQ_PNG, 0, 0},        {L"jpg", FQ_JPG, 1, 0},      {L"jpeg", FQ_JPG, 1, 0},
    {L"джпг", FQ_JPG, 0, 0},       {L"жпг", FQ_JPG, 0, 0},      {L"джипег", FQ_JPG, 0, 0},
    {L"bmp", FQ_BMP, 1, 0},        {L"бмп", FQ_BMP, 0, 0},      {L"tif", FQ_TIF, 1, 0},
    {L"tiff", FQ_TIF, 1, 0},       {L"тиф", FQ_TIF, 0, 0},      {L"word", FQ_DOCX, 0, 0},
    {L"ворд", FQ_DOCX, 0, 0},      {L"docx", FQ_DOCX, 1, 0},    {L"doc", FQ_DOCX, 1, 0},
    {L"excel", FQ_XLSX, 0, 0},     {L"эксел", FQ_XLSX, 0, 0},   {L"ексел", FQ_XLSX, 0, 0},
    {L"xlsx", FQ_XLSX, 1, 0},      {L"xls", FQ_XLSX, 1, 0},     {L"таблиц", FQ_XLSX, 0, 1},
    {L"zip", FQ_ZIP, 1, 0},        {L"зип", FQ_ZIP, 1, 0},      {L"картинк", FQ_PNG, 0, 1},
    {L"изображени", FQ_PNG, 0, 1},
};

/* Предлог перед словом на позиции p: «в », «во », «как », «на », «to »,
   «в формат » — 1 (цель); «из », «с » — 2 (это исходник); иначе 0. Между
   предлогом и словом — не больше одного слова: «в новый ворд». */
static int fq_before(const wchar_t *f, int p) {
  static const wchar_t *const kInto[] = {L"в ", L"во ", L"как ", L"на ", L"to ", L"формат "};
  static const wchar_t *const kFrom[] = {L"из ", L"с ", L"со ", L"от "};
  for (int k = 0; k < 2; k++) {
    const wchar_t *const *list = k ? kFrom : kInto;
    int n = k ? (int)(sizeof(kFrom) / sizeof(kFrom[0])) : (int)(sizeof(kInto) / sizeof(kInto[0]));
    for (int i = 0; i < n; i++) {
      int len = (int)wcslen(list[i]);
      for (int s = p - len; s >= 0 && s >= p - len - 24; s--) {
        if (wcsncmp(f + s, list[i], (size_t)len) || (s > 0 && fq_letter(f[s - 1]))) continue;
        if (wmemchr(f + s + len, 1, (size_t)(p - s - len))) continue; /* «из C:\… ворд»: «из» — про путь */
        if (fq_count(f, s + len, p) <= 1) return k ? 2 : 1;
      }
    }
  }
  return 0;
}

/* Цель: упоминание формата с предлогом «в …» (последнее), иначе последнее
   без «из …». Возвращает FQ_…; *into — нашлась ли цель с предлогом. */
static int fq_target(const wchar_t *f, int *into) {
  int best = FQ_NONE, bestPos = -1, bestInto = 0;
  for (size_t i = 0; i < sizeof(kFqWords) / sizeof(kFqWords[0]); i++) {
    for (const wchar_t *p = f; (p = fq_word_at(f, kFqWords[i].w, kFqWords[i].end, p)) != NULL; p++) {
      int pos = (int)(p - f), pre = fq_before(f, pos);
      if (pre == 2 || (kFqWords[i].into && pre != 1)) continue;
      if (pre > bestInto || (pre == bestInto && pos > bestPos)) {
        best = kFqWords[i].target;
        bestPos = pos;
        bestInto = pre;
      }
    }
  }
  *into = bestInto;
  return best;
}

/* без пути — о том же файле: «переведи его в pdf», «а теперь в txt» */
static int fq_refers(const wchar_t *f) {
  return FQ_ANY(f, L"его", L"ее ", L"её", L"этот", L"это ", L"этого", L"эту", L"тот же", L"того же", L"тоже",
                L"файл", L"результат", L"ещё раз", L"еще раз", L"снова", L"теперь", L"а в ", L"и в ");
}

/* Просьба про файл. span — пути в тексте: [0], [1] — исходник (файл), [2],
   [3] — папка для результата (если nspan 2); nspan 0 — пути нет. last — в
   беседе последним ответом был этот же помощник с файлом («а теперь в pdf»);
   out — у того ответа есть результат («покажи в проводнике»). */
static void fq_parse(const wchar_t *q, const int *span, int nspan, int last, int out, FqAsk *a) {
  memset(a, 0, sizeof(*a));
  wchar_t f[2000];
  fq_fold(q, span, nspan, f, 2000);
  int path = nspan > 0;
  if (!path && !last) return;
  /* другие помощники: сверка с Союзом, маршрут и нормы по чертежу, переносы */
  if (FQ_ANY(f, L"союз", L"plm", L"плм", L"маршрут", L"техпроц", L"норм", L"перенеси", L"перемести", L"скопируй",
             L"переименуй", L"удали", L"разложи", L"создай папк")) return;
  /* «как перевести pdf в word?» — вопрос, как сделать самому */
  const wchar_t *s = f;
  while (*s == L' ' || *s == 1) s++;
  if (!wcsncmp(s, L"как ", 4) || !wcsncmp(s, L"почему", 6)) return;
  /* за чтением ещё и вопрос по содержимому — помощнику: он прочитает
     file_read и ответит по тексту, а не вывалит весь файл */
  if (FQ_ANY(f, L"посчитай", L"подсчитай", L"сколько", L"сумм", L"сравни", L"найди", L"сверь", L"проверь",
             L"заполни", L"перескажи", L"кратко", L"вкратце", L"суть", L"о чем", L"про что", L"объясни", L"ответь",
             L"выдели", L"выбери", L"английск", L"немецк", L"китайск", L"язык", L"на русск", L"столбц", L"колонк",
             L"какой", L"какая", L"какие", L"какое", L"каком", L"кто ", L"когда", L"где указан", L"есть ли",
             L"исправь", L"дополни", L"добавь")) return;
  a->desktop = fq_has(f, L"на рабочий стол");
  a->drawing = FQ_ANY(f, L"чертеж", L"штамп", L"спецификац", L"основн надпис");
  int into = 0, target = fq_target(f, &into);
  int read = FQ_ANY(f, L"прочитай", L"прочти", L"прочесть", L"прочитать", L"почитай", L"распознай", L"распознать",
                    L"распознавани", L"что написано", L"что там написано", L"что в файле", L"что внутри",
                    L"покажи текст", L"покажи содержимое", L"текст из", L"текст с ", L"вытащи текст",
                    L"достань текст", L"извлеки текст", L"выпиши текст", L"перепиши текст", L"считай текст", L"ocr",
                    L"оцр");
  int conv = FQ_ANY(f, L"переведи", L"перевести", L"переводи", L"перегони", L"конверт", L"преобраз", L"сохрани",
                    L"сохранить", L"сделай", L"сделать", L"переделай", L"экспорт", L"запакуй", L"заархивируй",
                    L"упакуй", L"нужен в", L"нужна в", L"нужно в", L"хочу в");
  if (FQ_ANY(f, L"запакуй", L"заархивируй", L"упакуй")) target = FQ_ZIP, into = 1;
  /* «прочитай pdf C:\…» — это про сам файл; цель только с «в …» */
  if (read && !conv && !into) target = FQ_NONE;
  /* «переведи на английский» — перевод текста, а не формат */
  if (fq_has(f, L"переведи на ") && target == FQ_NONE) return;
  int words = fq_count(f, 0, (int)wcslen(f));
  if (!path) {
    if (out && !conv && FQ_ANY(f, L"проводник", L"покажи файл", L"открой папк", L"покажи папк", L"где файл",
                               L"где он", L"где лежит", L"покажи результат", L"открой результат")) {
      a->want = FQW_REVEAL;
      return;
    }
    /* без пути — только явная ссылка на тот же файл или короткое «сохрани в ворд» */
    if (!fq_refers(f) && !(words <= 5 && target != FQ_NONE)) return;
  }
  if (target != FQ_NONE && (conv || read || (into && words <= 6))) {
    a->want = FQW_CONVERT;
    a->target = target;
    return;
  }
  if (read && target == FQ_NONE) a->want = FQW_READ;
}

/* Чем выполнять: вид файла kind (FQK_…), формат исходника-картинки own
   (fq_image_format), цель target. *bench — номер действия окна «Обработка
   файлов» (kCvxActions): 0–3 в PNG/JPG/BMP/TIFF, 4 в PDF, 5 Word → TXT,
   6 Excel → CSV, 7 ZIP. */
static int fq_route(int kind, int own, int target, int *bench) {
  *bench = -1;
  if (target == FQ_NONE) return kind == FQK_OTHER ? FQA_NONE : FQA_TEXT;
  if (target == FQ_ZIP) return *bench = 7, FQA_BENCH;
  if (target == FQ_XLSX) return kind == FQK_EXCEL ? FQA_SAME : kind == FQK_OTHER ? FQA_NONE : FQA_AGENT;
  switch (kind) {
  case FQK_IMAGE:
    if (target >= FQ_PNG && target <= FQ_TIF) return target == own ? FQA_SAME : (*bench = target - FQ_PNG, FQA_BENCH);
    if (target == FQ_PDF) return *bench = 4, FQA_BENCH;
    if (target == FQ_TXT || target == FQ_DOCX) return FQA_SAVE;
    return FQA_NONE;
  case FQK_PDF:
    if (target >= FQ_PNG && target <= FQ_TIF) return *bench = target - FQ_PNG, FQA_BENCH;
    if (target == FQ_TXT || target == FQ_DOCX) return FQA_SAVE;
    return target == FQ_PDF ? FQA_SAME : FQA_NONE;
  case FQK_WORD:
    if (target == FQ_PDF) return *bench = 4, FQA_BENCH;
    if (target == FQ_TXT) return *bench = 5, FQA_BENCH;
    return target == FQ_DOCX ? FQA_SAME : FQA_NONE;
  case FQK_EXCEL:
    if (target == FQ_PDF) return *bench = 4, FQA_BENCH;
    if (target == FQ_CSV) return *bench = 6, FQA_BENCH;
    if (target == FQ_TXT) return FQA_SAVE; /* лист — текстом с табуляциями */
    if (target == FQ_DOCX) return FQA_AGENT; /* Excel → Word — write src=… у помощника */
    return FQA_NONE;
  case FQK_TEXT:
    if (target == FQ_DOCX) return FQA_SAVE;
    return target == FQ_TXT || target == FQ_CSV ? FQA_SAME : FQA_NONE;
  }
  return FQA_NONE;
}

/* что можно сделать с файлом такого вида — для отказа */
static const wchar_t *fq_can(int kind) {
  switch (kind) {
  case FQK_IMAGE: return L"прочитать текст (распознать), перевести в PNG, JPG, BMP, TIFF или PDF, распознать в TXT или Word";
  case FQK_PDF: return L"прочитать текст (распознать), перевести страницы в PNG, JPG, BMP или TIFF, распознать в TXT или Word";
  case FQK_WORD: return L"прочитать текст, перевести в PDF или TXT";
  case FQK_EXCEL: return L"прочитать первый лист, перевести в PDF, CSV или TXT; Word из Excel — «сделай Word из …» помощнику";
  case FQK_TEXT: return L"прочитать, сохранить в Word";
  }
  return L"читаю и перевожу картинки и сканы (PNG, JPG, TIFF, BMP), PDF, Word, Excel, TXT и CSV";
}
