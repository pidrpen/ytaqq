/* ai_file_ask.c — «Спросить ИИ»: прочитать файл и перевести его в другой
   формат по просьбе словами, без F6 (с 2026.10.26). Что просят и чем
   выполнять — file_ask.c (без Windows, file_ask_test.c); здесь чтение и запись.

   • Скан, картинка, PDF — Tesseract, как у F6 и «Распознать чертёж», только с
     файла, а не с экрана. Страница выпрямляется, приводится примерно к 300 dpi
     и читается целиком (psm 3: колонки и абзацы Tesseract ищет сам). PDF
     сначала рисует CursorPadOcr.exe --pdf (Windows.Data.Pdf), 300 dpi.
     «Чертёж», «штамп» в просьбе — лист читается как в окне «Распознать
     чертёж»: графы штампа и строки спецификации.
   • Word — через Word (таблицы — с табуляциями), Excel — первый лист через
     Excel (CSV), TXT/CSV — как есть.
   • Перевести — исполнитель окна «Обработка файлов» (convert_files.py и
     Office), тот же, что у кольца Shift-перетаскивания: новая копия, исходник
     не меняется, операция — в истории того окна («Удалить копии» отменяет).
   • TXT и Word из скана пишутся здесь: UTF-8 с BOM и простой .docx без Office.

   Модель не нужна. Исходный файл только читается. Результат — рядом с
   исходником; папка только для чтения (архив на сервере) — на рабочий стол,
   и ответ об этом говорит. Excel из скана и Word из Excel — у помощника с
   инструментами (FQA_AGENT): чтение для него — инструмент file_read
   (ai_app_tools.c), тот же fq_read_text.
   Подключается из cursorpad.c после convert.c (нужен cvx_exec); чат знает
   ac_fq_* по объявлениям в ai_chat.c. */
#include "file_ask.c"

#define FQ_PAGES 30   /* страниц скана или PDF за один раз */
#define FQ_SHOW 12000 /* знаков текста в чат; целиком — «сохрани в txt» */

typedef void (*FqProgress)(int done, int total, void *ctx);

/* ---- чтение ------------------------------------------------------------------ */

static void fq_note(FqProgress cb, void *ctx, int done, int total) {
  if (cb) cb(done, total, ctx);
}

/* слова страницы page из TSV → строки; новый абзац Tesseract — пустая строка.
   Одиночные «|» и «_» — остатки линий таблиц, не текст. */
static void fq_words_text(const DrWords *ws, int page, ShareBuf *b) {
  int last = -1;
  for (int i = 0; i < ws->n; i++) {
    const DrWord *w = &ws->w[i];
    if (w->page != page || !w->t[0] || (!w->t[1] && (w->t[0] == L'|' || w->t[0] == L'_'))) continue;
    if (last >= 0) sb_add(b, w->line == last ? L" " : w->line / 100 != last / 100 ? L"\r\n\r\n" : L"\r\n");
    sb_add(b, L"%ls", w->t);
    last = w->line;
  }
}

/* к ~300 dpi по длинной стороне: больше 4200 точек — уменьшить (A3, A1),
   меньше 1700 (снимок экрана) — увеличить вдвое, как F6 перед Tesseract */
static BOOL fq_scale(DrImg *m) {
  int lng = m->w > m->h ? m->w : m->h;
  DrImg n;
  if (lng > 4200) {
    int s = (lng + 4199) / 4200;
    if (!dr_img_new(&n, m->w / s, m->h / s)) return FALSE;
    for (int y = 0; y < n.h; y++)
      for (int x = 0; x < n.w; x++) {
        int sum = 0;
        for (int dy = 0; dy < s; dy++)
          for (int dx = 0; dx < s; dx++) sum += m->px[(size_t)(y * s + dy) * m->w + x * s + dx];
        n.px[(size_t)y * n.w + x] = (BYTE)(sum / (s * s));
      }
  } else if (lng < 1700) {
    if (!dr_img_new(&n, m->w * 2, m->h * 2)) return FALSE;
    for (int y = 0; y < n.h; y++) {
      int y0 = y / 2, y1 = y0 + (y & 1) < m->h ? y0 + (y & 1) : y0;
      for (int x = 0; x < n.w; x++) {
        int x0 = x / 2, x1 = x0 + (x & 1) < m->w ? x0 + (x & 1) : x0;
        int sum = m->px[(size_t)y0 * m->w + x0] + m->px[(size_t)y0 * m->w + x1] + m->px[(size_t)y1 * m->w + x0] +
                  m->px[(size_t)y1 * m->w + x1];
        n.px[(size_t)y * n.w + x] = (BYTE)((sum + 2) / 4);
      }
    }
  } else {
    return TRUE;
  }
  dr_img_free(m);
  *m = n;
  return TRUE;
}

/* одна страница-картинка → текст */
static BOOL fq_ocr_page(IWICImagingFactory *f, const wchar_t *path, UINT frame, ShareBuf *b, wchar_t *why, int cap) {
  DrImg m;
  double dpi = 96;
  HRESULT hr = dr_read(f, path, frame, &m, &dpi);
  if (FAILED(hr)) {
    wchar_t w[120];
    tm_why(hr, w, 120);
    _snwprintf(why, cap, L"не открылся: %ls", w);
    why[cap - 1] = 0;
    return FALSE;
  }
  int thr = dr_otsu(&m);
  dr_deskew(&m, dr_skew(&m, thr, FALSE), dr_skew(&m, thr, TRUE));
  DrWords ws;
  memset(&ws, 0, sizeof(ws));
  BOOL ok = fq_scale(&m) && dr_tess(&m, 1, 3, &ws, why, cap);
  if (ok) fq_words_text(&ws, 1, b);
  dr_words_free(&ws);
  dr_img_free(&m);
  return ok;
}

/* лист чертежа: графы штампа и спецификация — как в окне «Распознать чертёж» */
static void fq_drawing_page(IWICImagingFactory *f, const wchar_t *path, UINT frame, ShareBuf *b) {
  DrPage pg;
  memset(&pg, 0, sizeof(pg));
  lstrcpynW(pg.path, path, MAX_PATH);
  pg.frame = frame;
  dr_recognize(f, &pg);
  sb_add(b, L"%ls", kDrType[pg.type]);
  if (pg.fmt[0]) sb_add(b, L", формат %ls", pg.fmt);
  sb_add(b, L"\r\n");
  for (int k = 0; k < DF_N; k++)
    if (pg.f[k][0]) sb_add(b, L"%ls: %ls%ls\r\n", kDrField[k], pg.f[k], pg.doubt[k] ? L" (проверить)" : L"");
  if (pg.nrows) {
    sb_add(b, L"\r\nСпецификация:\r\n");
    for (int c = 0; c < DS_N; c++) sb_add(b, c ? L"\t%ls" : L"%ls", kDrSpecCol[c]);
    for (int r = 0; r < pg.nrows; r++) {
      sb_add(b, L"\r\n");
      for (int c = 0; c < DS_N; c++)
        sb_add(b, L"%ls%ls%ls", c ? L"\t" : L"", pg.rows[r].c[c], pg.rows[r].doubt[c] ? L" (?)" : L"");
    }
    sb_add(b, L"\r\n");
  }
  if (pg.note[0]) sb_add(b, L"Заметка: %ls\r\n", pg.note);
  dr_page_free(&pg);
}

/* PDF → p0001.png… в новой временной папке dir: CursorPadOcr.exe --pdf, 300 dpi.
   Возвращает число страниц; 0 — не вышло (в why — почему). */
static int fq_pdf_render(const wchar_t *pdf, wchar_t *dir, wchar_t *why, int cap) {
  wchar_t helper[MAX_PATH], tmp[MAX_PATH], cmd[MAX_PATH * 3 + 40];
  _snwprintf(helper, MAX_PATH, L"%ls\\CursorPadOcr.exe", g_dataDir);
  helper[MAX_PATH - 1] = 0;
  if (GetFileAttributesW(helper) == INVALID_FILE_ATTRIBUTES) {
    lstrcpynW(why, L"нет помощника распознавания CursorPadOcr.exe — перезапустите CursorPad", cap);
    return 0;
  }
  GetTempPathW(MAX_PATH, tmp);
  _snwprintf(dir, MAX_PATH, L"%lscursorpad-read-%lu-%lu", tmp, GetCurrentThreadId(), GetTickCount());
  dir[MAX_PATH - 1] = 0;
  if (!CreateDirectoryW(dir, NULL)) {
    lstrcpynW(why, L"не создать временную папку для страниц PDF", cap);
    return 0;
  }
  _snwprintf(cmd, MAX_PATH * 3 + 40, L"\"%ls\" --pdf \"%ls\" \"%ls\" 300", helper, pdf, dir);
  cmd[MAX_PATH * 3 + 39] = 0;
  char *out = NULL, err[256] = {0};
  DWORD n = 0, code = 0;
  BOOL ran = run_capture(cmd, &out, &n, &code, err, 256);
  free(out);
  int pages = 0;
  for (;; pages++) {
    wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%ls\\p%04d.png", dir, pages + 1);
    p[MAX_PATH - 1] = 0;
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) break;
  }
  if (!pages) {
    wchar_t e[256] = L"";
    MultiByteToWideChar(CP_UTF8, 0, err, -1, e, 256);
    e[255] = 0;
    for (wchar_t *c = e; *c; c++)
      if (*c == L'\r' || *c == L'\n') *c = L' ';
    _snwprintf(why, cap, L"PDF не открылся%ls%ls", !ran ? L" (помощник не запустился)" : e[0] ? L": " : L"", e);
    why[cap - 1] = 0;
  }
  return pages;
}

/* CSV из Excel (запятые, кавычки) → строки с табуляциями; перевод строки внутри ячейки — пробел */
static wchar_t *fq_csv_tsv(wchar_t *csv) {
  if (!csv) return NULL;
  size_t n = wcslen(csv), o = 0;
  wchar_t *t = (wchar_t *)malloc((n * 2 + 1) * sizeof(wchar_t));
  if (!t) return csv;
  const wchar_t *p = csv;
  if (*p == 0xFEFF) p++;
  BOOL quoted = FALSE;
  for (; *p; p++) {
    if (quoted) {
      if (*p == L'"' && p[1] == L'"') t[o++] = L'"', p++;
      else if (*p == L'"') quoted = FALSE;
      else t[o++] = *p == L'\r' || *p == L'\n' || *p == L'\t' ? L' ' : *p;
    } else if (*p == L'"') {
      quoted = TRUE;
    } else if (*p == L',') {
      t[o++] = L'\t';
    } else if (*p == L'\n') {
      while (o && t[o - 1] == L'\t') o--; /* пустые ячейки в конце строки */
      t[o++] = L'\r';
      t[o++] = L'\n';
    } else if (*p != L'\r') {
      t[o++] = *p;
    }
  }
  while (o && (t[o - 1] == L'\t' || t[o - 1] == L'\r' || t[o - 1] == L'\n')) o--;
  t[o] = 0;
  free(csv);
  return t;
}

/* первый лист книги — через Excel в CSV во временный файл (как «Excel → CSV») */
static wchar_t *fq_excel_text(const wchar_t *path, wchar_t *why, int cap) {
  CvJob job;
  memset(&job, 0, sizeof(job));
  lstrcpynW(job.src, path, MAX_PATH);
  job.format = 6;
  wchar_t tmp[MAX_PATH], csv[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  _snwprintf(csv, MAX_PATH, L"%lscursorpad-read-%lu-%lu.csv", tmp, GetCurrentThreadId(), GetTickCount());
  csv[MAX_PATH - 1] = 0;
  HANDLE h = CreateFileW(csv, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    lstrcpynW(why, L"не создать временный файл для листа Excel", cap);
    return NULL;
  }
  CloseHandle(h);
  wchar_t *text = NULL;
  if (cv_office(&job, csv)) text = fq_csv_tsv(mtw_read_file(csv));
  else lstrcpynW(why, job.error, cap);
  DeleteFileW(csv);
  return text;
}

/* Текст файла. Скан, картинка, PDF — Tesseract (страниц не больше FQ_PAGES;
   страницы со второй начинаются с «\f— стр. N —»), drawing — как чертёж: штамп
   и спецификация. Word и Excel — через Office, TXT/CSV — как есть. NULL — не
   прочитан, в why — почему. *pages — прочитано страниц, *pagesAll — всего. */
static wchar_t *fq_read_text(const wchar_t *path, BOOL drawing, volatile LONG *cancel, FqProgress cb, void *ctx,
                             int *pages, int *pagesAll, wchar_t *why, int cap) {
  *pages = *pagesAll = 0;
  why[0] = 0;
  DWORD at = GetFileAttributesW(path);
  if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY)) {
    lstrcpynW(why, L"файла нет или это папка", cap);
    return NULL;
  }
  int kind = fq_kind(path);
  if (kind == FQK_WORD) return ag_pc_docx(path, why, cap);
  if (kind == FQK_EXCEL) return fq_excel_text(path, why, cap);
  if (kind == FQK_TEXT) {
    wchar_t *t = mtw_read_file(path);
    if (!t) lstrcpynW(why, L"файл не прочитался (занят или слишком большой)", cap);
    return t;
  }
  if (kind != FQK_IMAGE && kind != FQK_PDF) {
    lstrcpynW(why, fq_can(FQK_OTHER), cap);
    return NULL;
  }
  /* страницы: у PDF — нарисованные PNG, у TIFF — кадры, у остальных — одна */
  wchar_t tmp[MAX_PATH] = L"";
  DrPages list;
  memset(&list, 0, sizeof(list));
  HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  IWICImagingFactory *f = NULL;
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  if (!f) {
    lstrcpynW(why, L"Windows не открыл картинку (нет WIC)", cap);
  } else if (kind == FQK_PDF) {
    fq_note(cb, ctx, 0, 0);
    *pagesAll = fq_pdf_render(path, tmp, why, cap);
  } else {
    HRESULT hr = tm_probe(f, path, FALSE, dr_pages_cb, &list);
    *pagesAll = list.n;
    if (!list.n) {
      wchar_t w[120];
      tm_why(hr, w, 120);
      _snwprintf(why, cap, L"не открылся: %ls", w);
      why[cap - 1] = 0;
    }
  }
  for (int i = 0; f && i < *pagesAll && i < FQ_PAGES && !(cancel && *cancel); i++) {
    wchar_t page[MAX_PATH];
    UINT frame = 0;
    if (kind == FQK_PDF) {
      _snwprintf(page, MAX_PATH, L"%ls\\p%04d.png", tmp, i + 1);
      page[MAX_PATH - 1] = 0;
    } else {
      lstrcpynW(page, list.p[i].path, MAX_PATH);
      frame = list.p[i].frame;
    }
    fq_note(cb, ctx, i + 1, *pagesAll < FQ_PAGES ? *pagesAll : FQ_PAGES);
    if (i) sb_add(&b, L"\r\n\r\n\f— стр. %d —\r\n", i + 1);
    if (drawing) {
      fq_drawing_page(f, page, frame, &b);
    } else {
      wchar_t w[300] = L"";
      if (!fq_ocr_page(f, page, frame, &b, w, 300)) {
        if (!i) { /* первая страница не прочиталась — дальше то же самое */
          lstrcpynW(why, w, cap);
          break;
        }
        sb_add(&b, L"(страница не прочиталась: %ls)", w);
      }
    }
    (*pages)++;
  }
  free(list.p);
  if (f) IWICImagingFactory_Release(f);
  if (SUCCEEDED(co)) CoUninitialize();
  if (tmp[0]) dr_rmdir(tmp);
  if (!*pages) {
    free(b.w);
    if (!why[0]) lstrcpynW(why, cancel && *cancel ? L"остановлено" : L"страниц не нашлось", cap);
    return NULL;
  }
  if (!b.w) {
    lstrcpynW(why, L"не хватило памяти", cap);
    return NULL;
  }
  return b.w;
}

/* скан, картинка или PDF — текст распознан, в нём бывают ошибки */
static BOOL fq_is_scan(const wchar_t *path) {
  int k = fq_kind(path);
  return k == FQK_IMAGE || k == FQK_PDF;
}

/* убрать метки страниц «\f» — для чата, TXT и помощника */
static void fq_unpage(wchar_t *t) {
  wchar_t *o = t;
  for (const wchar_t *p = t; p && *p; p++)
    if (*p != L'\f') *o++ = *p;
  if (o) *o = 0;
}

/* ---- запись ------------------------------------------------------------------ */

typedef struct {
  char *s;
  size_t n, cap;
  BOOL ok;
} FqXml;
static void fqx_add(FqXml *x, const char *t, size_t len) {
  if (!x->ok) return;
  if (x->n + len + 1 > x->cap) {
    size_t cap = (x->n + len + 1) * 2 + 4096;
    char *s = (char *)realloc(x->s, cap);
    if (!s) {
      x->ok = FALSE;
      return;
    }
    x->s = s;
    x->cap = cap;
  }
  memcpy(x->s + x->n, t, len);
  x->n += len;
  x->s[x->n] = 0;
}
#define FQX(x, lit) fqx_add(x, lit, sizeof(lit) - 1)

/* Word без Office: абзац на строку, табуляции, «\f» — разрыв страницы; A4 */
static BOOL fq_write_docx(const wchar_t *path, const wchar_t *text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  char *u = n > 0 ? (char *)malloc((size_t)n) : NULL;
  if (!u) return FALSE;
  WideCharToMultiByte(CP_UTF8, 0, text, -1, u, n, NULL, NULL);
  FqXml x = {NULL, 0, 0, TRUE};
  FQX(&x, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/"
          "wordprocessingml/2006/main\"><w:body><w:p><w:r><w:t xml:space=\"preserve\">");
  for (int i = 0; i < n - 1 && x.ok; i++) {
    char c = u[i];
    if (c == '\r') continue;
    if (c == '\n') FQX(&x, "</w:t></w:r></w:p><w:p><w:r><w:t xml:space=\"preserve\">");
    else if (c == '\t') FQX(&x, "</w:t><w:tab/><w:t xml:space=\"preserve\">");
    else if (c == '\f') FQX(&x, "</w:t><w:br w:type=\"page\"/><w:t xml:space=\"preserve\">");
    else if (c == '&') FQX(&x, "&amp;");
    else if (c == '<') FQX(&x, "&lt;");
    else if (c == '>') FQX(&x, "&gt;");
    else if ((BYTE)c >= 32) fqx_add(&x, &c, 1);
  }
  FQX(&x, "</w:t></w:r></w:p><w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1134\" "
          "w:right=\"850\" w:bottom=\"1134\" w:left=\"1418\"/></w:sectPr></w:body></w:document>");
  free(u);
  BOOL ok = x.ok && ag_write_document(path, L"", AG_DOCX, x.s);
  DWORD e = GetLastError();
  free(x.s);
  SetLastError(e);
  return ok;
}

/* TXT: UTF-8 с BOM, переводы строк Windows; CREATE_NEW — чужое не заменяется */
static BOOL fq_write_txt(const wchar_t *path, const wchar_t *text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  char *u = n > 0 ? (char *)malloc((size_t)n + 3) : NULL;
  if (!u) return FALSE;
  memcpy(u, "\xef\xbb\xbf", 3);
  WideCharToMultiByte(CP_UTF8, 0, text, -1, u + 3, n, NULL, NULL);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD e = GetLastError();
    free(u);
    SetLastError(e);
    return FALSE;
  }
  DWORD wr = 0, len = (DWORD)(n - 1 + 3);
  BOOL ok = WriteFile(h, u, len, &wr, NULL) && wr == len;
  DWORD e = GetLastError();
  CloseHandle(h);
  if (!ok) DeleteFileW(path);
  free(u);
  SetLastError(e);
  return ok;
}

/* можно ли писать в папку: пробный файл, удаляется сам */
static BOOL fq_dir_writable(const wchar_t *dir) {
  wchar_t probe[MAX_PATH];
  _snwprintf(probe, MAX_PATH, L"%ls\\.cursorpad-probe-%lu-%lu", dir, GetCurrentProcessId(), GetTickCount());
  probe[MAX_PATH - 1] = 0;
  HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, NULL);
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  CloseHandle(h);
  return TRUE;
}

/* ---- чат ------------------------------------------------------------------- */

struct AcFqJob {
  wchar_t path[MAX_PATH]; /* исходник */
  wchar_t dir[MAX_PATH];  /* куда писать; пусто — рядом с исходником */
  BOOL moved;             /* папка исходника только для чтения — пишем на рабочий стол */
  FqAsk ask;
  int kind, route, bench;
  volatile LONG cancel;
  CvxJob *cvx;            /* перевод исполнителем «Обработки файлов» */
  wchar_t *text;          /* прочитанное */
  BOOL cached;            /* текст взят из прошлого чтения этого же файла */
  int pages, pagesAll;
  wchar_t out[MAX_PATH];  /* записанный файл */
  wchar_t why[600];
};
static struct AcFqJob *g_acFq;
/* о каком файле был последний ответ этого помощника — «а теперь в pdf» */
static wchar_t g_fqLast[MAX_PATH], g_fqLastOut[MAX_PATH];
static int g_fqTurn = -1;
/* прочитанный текст: «прочитай», потом «сохрани в ворд» — без второго распознавания */
static wchar_t *g_fqText, g_fqTextPath[MAX_PATH];
static BOOL g_fqTextDrawing;
static FILETIME g_fqTextTime;

static BOOL ac_fq_busy(void) { return g_acFq != NULL; }

static void ac_fq_progress(int done, int total, void *ctx) {
  struct AcFqJob *j = (struct AcFqJob *)ctx;
  wchar_t *t = (wchar_t *)malloc(sizeof(wchar_t) * 200);
  if (!t) return;
  const wchar_t *base = wcsrchr(j->path, L'\\');
  base = base ? base + 1 : j->path;
  if (!total) swprintf(t, 200, L"Готовлю страницы PDF «%.60ls»… · Стоп — остановить", base);
  else swprintf(t, 200, L"Распознаю «%.60ls»: страница %d из %d · Стоп — остановить", base, done, total);
  if (!g_acWnd || !PostMessageW(g_acWnd, WM_AC_FQ, 1, (LPARAM)t)) free(t);
}

static BOOL fq_file_time(const wchar_t *path, FILETIME *t) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return FALSE;
  *t = d.ftLastWriteTime;
  return TRUE;
}

/* записать прочитанное в .txt / .docx: «<имя>.txt», занято — «<имя> (2).txt» */
static void fq_save(struct AcFqJob *j) {
  const wchar_t *ext = j->ask.target == FQ_DOCX ? L"docx" : L"txt";
  wchar_t dir[MAX_PATH], base[MAX_PATH];
  const wchar_t *slash = wcsrchr(j->path, L'\\');
  lstrcpynW(base, slash ? slash + 1 : j->path, MAX_PATH);
  wchar_t *dot = wcsrchr(base, L'.');
  if (dot) *dot = 0;
  if (j->dir[0]) lstrcpynW(dir, j->dir, MAX_PATH);
  else {
    lstrcpynW(dir, j->path, MAX_PATH);
    wchar_t *s = wcsrchr(dir, L'\\');
    if (s) *s = 0;
  }
  wchar_t *text = j->text;
  if (j->ask.target != FQ_DOCX) { /* в TXT меток страниц не нужно — строка «— стр. N —» остаётся */
    text = _wcsdup(j->text);
    if (!text) {
      lstrcpynW(j->why, L"не хватило памяти", 600);
      return;
    }
    fq_unpage(text);
  }
  BOOL ok = FALSE;
  DWORD e = 0;
  for (int i = 1; i <= 999 && !ok; i++) {
    if (i == 1) _snwprintf(j->out, MAX_PATH, L"%ls\\%ls.%ls", dir, base, ext);
    else _snwprintf(j->out, MAX_PATH, L"%ls\\%ls (%d).%ls", dir, base, i, ext);
    j->out[MAX_PATH - 1] = 0;
    ok = j->ask.target == FQ_DOCX ? fq_write_docx(j->out, text) : fq_write_txt(j->out, text);
    e = GetLastError();
    if (!ok && e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS) break;
  }
  if (text != j->text) free(text);
  if (!ok) {
    swprintf(j->why, 600, L"не удалось записать %ls в %ls (Windows %lu)", ext, dir, e);
    j->out[0] = 0;
  }
}

static DWORD WINAPI ac_fq_thread(LPVOID arg) {
  struct AcFqJob *j = (struct AcFqJob *)arg;
  if (j->route == FQA_BENCH || j->route == FQA_SAVE) {
    /* куда писать: названная папка, рабочий стол или рядом; рядом нельзя — на рабочий стол */
    wchar_t src[MAX_PATH];
    lstrcpynW(src, j->path, MAX_PATH);
    wchar_t *s = wcsrchr(src, L'\\');
    if (s) *s = 0;
    if (!j->dir[0] && (j->ask.desktop || !fq_dir_writable(src))) {
      if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, j->dir)))
        j->moved = !j->ask.desktop;
      else j->dir[0] = 0;
    }
  }
  if (j->route == FQA_BENCH) {
    lstrcpynW(j->cvx->directory, j->dir, MAX_PATH);
    cvx_exec(j->cvx);
  } else {
    if (!j->text)
      j->text = fq_read_text(j->path, j->ask.drawing, &j->cancel, ac_fq_progress, j, &j->pages, &j->pagesAll, j->why, 600);
    if (j->text && j->route == FQA_SAVE && !j->cancel) fq_save(j);
  }
  if (!g_acWnd || !PostMessageW(g_acWnd, WM_AC_FQ, 0, (LPARAM)j)) {
    /* окна нет — итог некому показать; конвертер отпустить */
    if (j->cvx) {
      free(j->cvx->report);
      free(j->cvx);
      InterlockedExchange(&g_cvBusy, 0);
    }
    free(j->text);
    free(j);
  }
  return 0;
}

static const wchar_t *fq_kind_name(int kind) {
  switch (kind) {
  case FQK_IMAGE: return L"картинка";
  case FQK_PDF: return L"PDF";
  case FQK_WORD: return L"Word";
  case FQK_EXCEL: return L"Excel";
  case FQK_TEXT: return L"текстовый файл";
  }
  return L"файл этого типа";
}

/* ответ без работы: так не умею или уже в этом формате */
static void ac_fq_say(const wchar_t *a, const wchar_t *path, const wchar_t *text) {
  ag_stop();
  ac_push(0, a);
  ac_push(1, text);
  lstrcpynW(g_fqLast, path, MAX_PATH);
  g_fqLastOut[0] = 0;
  g_fqTurn = g_acN - 1;
  ac_redraw();
  ac_avatar_answer(text);
  ac_status(L"Файлы — на этом компьютере, без модели");
}

static int fq_exists(const wchar_t *p, int dir) {
  DWORD a = GetFileAttributesW(p);
  return a != INVALID_FILE_ATTRIBUTES && (dir ? (a & FILE_ATTRIBUTE_DIRECTORY) != 0 : !(a & FILE_ATTRIBUTE_DIRECTORY));
}

static void fq_copy_span(const wchar_t *a, int b, int e, wchar_t *out) {
  int n = e - b < MAX_PATH - 1 ? e - b : MAX_PATH - 1;
  for (int i = 0; i < n; i++) out[i] = a[b + i] == L'/' ? L'\\' : a[b + i];
  out[n] = 0;
}

/* Просьба прочитать или перевести файл. TRUE — взяли (работа пошла или ответ
   уже в чате); FALSE — не это, вопрос идёт дальше (Excel из скана — помощнику). */
static BOOL ac_fq_start(const wchar_t *a) {
  if (g_acImg || g_acImgFull) return FALSE; /* с картинкой в поле — это вопрос о ней */
  int span[4], n = 0;
  if (fq_find_path(a, 0, 0, fq_exists, &span[0], &span[1])) {
    n = 1;
    if (fq_find_path(a, span[1], 1, fq_exists, &span[2], &span[3])) n = 2;
  }
  BOOL last = !n && g_fqLast[0] && g_fqTurn == g_acN - 1 && g_fqTurn >= g_acContextStart && fq_exists(g_fqLast, 0);
  FqAsk ask;
  fq_parse(a, span, n, last, last && g_fqLastOut[0] && fq_exists(g_fqLastOut, 0), &ask);
  if (ask.want == FQW_NO) return FALSE;
  wchar_t path[MAX_PATH], dir[MAX_PATH] = L"";
  if (n) fq_copy_span(a, span[0], span[1], path);
  else lstrcpynW(path, g_fqLast, MAX_PATH);
  if (n == 2) fq_copy_span(a, span[2], span[3], dir);
  const wchar_t *base = wcsrchr(path, L'\\');
  base = base ? base + 1 : path;
  wchar_t msg[1200];
  if (ask.want == FQW_REVEAL) {
    lstrcpynW(g_cvLast, g_fqLastOut, MAX_PATH);
    cv_reveal();
    swprintf(msg, 1200, L"Открыл Проводник на файле `%ls`.", g_fqLastOut);
    wchar_t keep[MAX_PATH];
    lstrcpynW(keep, g_fqLastOut, MAX_PATH);
    ac_fq_say(a, path, msg);
    lstrcpynW(g_fqLastOut, keep, MAX_PATH);
    return TRUE;
  }
  int kind = fq_kind(path), bench = -1;
  int route = fq_route(kind, fq_image_format(path), ask.want == FQW_READ ? FQ_NONE : ask.target, &bench);
  if (route == FQA_AGENT) {
    /* таблица из скана, Word из Excel — помощник с инструментами: file_read,
       excel_write, write src=…; без модели он не работает — так и сказать */
    if (ai_ready()) {
      g_acRouteDispatch = 1;
      return FALSE;
    }
    swprintf(msg, 1200, L"%ls из файла «%ls» собирает помощник с моделью: он читает файл и раскладывает данные по ячейкам. "
                        L"Сейчас модель не загружена — включите её на вкладке «ИИ». Без модели с этим файлом могу: %ls.",
             ask.target == FQ_XLSX ? L"Таблицу Excel" : L"Word", base, fq_can(kind));
    ac_fq_say(a, path, msg);
    return TRUE;
  }
  if (route == FQA_NONE || route == FQA_SAME) {
    if (route == FQA_SAME)
      swprintf(msg, 1200, L"«%ls» — уже %ls. С ним могу: %ls.", base, kFqName[ask.target], fq_can(kind));
    else if (kind == FQK_OTHER)
      swprintf(msg, 1200, L"Файлы «%ls» не читаю. Я %ls.", base, fq_can(kind));
    else
      swprintf(msg, 1200, L"%ls → %ls не умею. С файлом «%ls» (%ls) могу: %ls.", fq_kind_name(kind), kFqName[ask.target], base,
               fq_kind_name(kind), fq_can(kind));
    ac_fq_say(a, path, msg);
    return TRUE;
  }
  struct AcFqJob *j = (struct AcFqJob *)calloc(1, sizeof(struct AcFqJob));
  if (!j) return FALSE;
  lstrcpynW(j->path, path, MAX_PATH);
  lstrcpynW(j->dir, dir, MAX_PATH);
  j->ask = ask;
  j->kind = kind;
  j->route = route;
  j->bench = bench;
  if (route == FQA_BENCH) {
    j->cvx = (CvxJob *)calloc(1, sizeof(CvxJob));
    if (!j->cvx) {
      free(j);
      return FALSE;
    }
    CvxJob *c = j->cvx;
    c->files.count = 1;
    lstrcpynW(c->files.paths[0], path, MAX_PATH);
    c->action = bench;
    c->format = bench < 4 ? bench : 0;
    c->kb = 1024;
    c->size = 1920;
    c->rotation = 90;
    lstrcpynW(c->result.label, kCvxActions[bench], 80);
    if (!cvx_claim(c)) {
      free(c);
      free(j);
      ac_fq_say(a, path, L"Конвертер сейчас занят: идёт операция в окне «Обработка файлов» или из кольца Shift-перетаскивания. Повторите, когда она закончится.");
      return TRUE;
    }
  } else {
    /* тот же файл, не менялся, читали так же — текст уже есть */
    FILETIME t;
    if (g_fqText && !_wcsicmp(g_fqTextPath, path) && g_fqTextDrawing == ask.drawing && fq_file_time(path, &t) &&
        !CompareFileTime(&t, &g_fqTextTime)) {
      j->text = _wcsdup(g_fqText);
      j->cached = j->text != NULL;
    }
  }
  ag_stop();
  ac_push(0, a);
  HANDLE th = CreateThread(NULL, 0, ac_fq_thread, j, 0, NULL);
  if (!th) {
    if (j->cvx) {
      RemoveDirectoryW(j->cvx->run);
      free(j->cvx);
      InterlockedExchange(&g_cvBusy, 0);
    }
    free(j->text);
    free(j);
    ac_push(1, L"Не вышло запустить чтение файла.");
    ac_redraw();
    return TRUE;
  }
  CloseHandle(th);
  g_acFq = j;
  ac_redraw();
  ac_busy(TRUE);
  ac_avatar_set(AC_AV_FILES);
  if (route == FQA_BENCH)
    swprintf(msg, 1200, L"Перевожу «%.60ls» в %ls — исходник не меняется · Стоп — остановить", base, kFqName[ask.target]);
  else
    swprintf(msg, 1200, L"Читаю «%.60ls» — только чтение · Стоп — остановить", base);
  ac_status(msg);
  return TRUE;
}

/* текст в ответ: кодом, как есть (OCR путает разметку чата), не длиннее cap знаков */
static void fq_show(ShareBuf *b, const wchar_t *text, int cap) {
  size_t len = wcslen(text);
  int show = len > (size_t)cap ? cap : (int)len;
  sb_add(b, L"\n```text\n%.*ls%ls\n```\n", show, text, len > (size_t)cap ? L"\n…" : L"");
}

/* wp 1 — строка состояния из потока (free); 0 — готово */
static void ac_fq_ready(WPARAM wp, LPARAM lp) {
  if (wp == 1) {
    wchar_t *t = (wchar_t *)lp;
    if (g_acFq && t) ac_status(t);
    free(t);
    return;
  }
  struct AcFqJob *j = (struct AcFqJob *)lp;
  if (!j) return;
  if (j == g_acFq) g_acFq = NULL;
  ac_busy(FALSE);
  const wchar_t *base = wcsrchr(j->path, L'\\');
  base = base ? base + 1 : j->path;
  BOOL scan = j->kind == FQK_IMAGE || j->kind == FQK_PDF;
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  BOOL ok = FALSE;
  if (j->cvx) {
    CvJob *r = &j->cvx->result;
    InterlockedExchange(&g_cvBusy, 0);
    cvx_controls();
    if (g_cvxWnd && g_cvxHistoryOpen) cvx_history(15);
    if (r->error[0] || !r->out[0]) {
      sb_add(&b, L"Не перевёл «%ls» в %ls: %ls Исходник не изменён.", base, kFqName[j->ask.target],
             r->error[0] ? r->error : L"конвертер не вернул результат.");
    } else {
      ok = TRUE;
      lstrcpynW(j->out, r->out, MAX_PATH);
      lstrcpynW(g_cvLast, r->out, MAX_PATH);
      sb_add(&b, L"Готово: «%ls» → %ls.\n\n`%ls`", base, kFqName[j->ask.target], r->out);
      if (r->summary[0]) sb_add(&b, L"\n\n%ls", r->summary);
      if (j->moved) sb_add(&b, L"\n\nВ папку исходника записать нельзя — копия на рабочем столе.");
      sb_add(&b, L"\n\nИсходник не изменён. Копию можно удалить в окне «Обработка файлов» → «История и отмена копий». «Покажи в проводнике» — открыть папку.");
    }
  } else if (!j->text) {
    sb_add(&b, L"Не прочитал «%ls»: %ls.", base, j->why[0] ? j->why : L"неизвестная ошибка");
  } else {
    ok = TRUE;
    wchar_t *show = _wcsdup(j->text);
    if (show) fq_unpage(show);
    const wchar_t *how = j->kind == FQK_WORD ? L"через Word" : j->kind == FQK_EXCEL ? L"первый лист, через Excel" :
                         j->kind == FQK_TEXT ? L"как есть" : j->ask.drawing ? L"как чертёж: штамп и спецификация" :
                         L"распознал Tesseract";
    if (j->route == FQA_SAVE) {
      if (j->out[0]) {
        lstrcpynW(g_cvLast, j->out, MAX_PATH);
        sb_add(&b, L"Готово: «%ls» → %ls (%ls).\n\n`%ls`", base, kFqName[j->ask.target], how, j->out);
        if (j->moved) sb_add(&b, L"\n\nВ папку исходника записать нельзя — файл на рабочем столе.");
      } else if (j->cancel) {
        ok = FALSE;
        sb_add(&b, L"Остановлено — %ls не записан.", kFqName[j->ask.target]);
      } else {
        ok = FALSE;
        sb_add(&b, L"Прочитал «%ls», но %ls.", base, j->why[0] ? j->why : L"файл не записался");
      }
    } else {
      sb_add(&b, L"«%ls» — %ls", base, how);
      if (scan && j->pagesAll > 1) sb_add(&b, L", страниц %d", j->pagesAll);
      sb_add(&b, L":");
    }
    if (scan && j->pagesAll > j->pages)
      sb_add(&b, L"\n\nПрочитаны первые %d страниц из %d.", j->pages, j->pagesAll);
    if (show && show[0]) {
      int cap = j->route == FQA_SAVE ? 1500 : FQ_SHOW;
      if (j->route == FQA_SAVE) sb_add(&b, L"\n\nНачало:");
      fq_show(&b, show, cap);
      if (j->route != FQA_SAVE && wcslen(show) > (size_t)cap)
        sb_add(&b, L"\nПоказано начало — %d из %d знаков. Целиком: «сохрани в txt» или «в ворд».", cap, (int)wcslen(show));
    } else {
      sb_add(&b, L"\n\nТекста не нашлось.");
    }
    if (scan && !j->ask.drawing)
      sb_add(&b, L"\nРаспознавание может путать О и 0, З и 3 — обозначения и числа сверяйте со сканом.");
    if (j->route != FQA_SAVE)
      sb_add(&b, L"\nДальше можно: «сохрани в ворд», «в txt»%ls.", scan ? L", «переведи в pdf»" : L"");
    free(show);
    /* запомнить прочитанное — «сохрани в ворд» не распознаёт заново */
    if (!j->cancel || j->cached) {
      free(g_fqText);
      g_fqText = j->text;
      j->text = NULL;
      lstrcpynW(g_fqTextPath, j->path, MAX_PATH);
      g_fqTextDrawing = j->ask.drawing;
      if (!fq_file_time(j->path, &g_fqTextTime)) memset(&g_fqTextTime, 0, sizeof(g_fqTextTime));
    }
  }
  if (j->cancel) sb_add(&b, L"\n\nОстановлено — прочитано, что успел.");
  if (b.w) {
    ac_push(1, b.w);
    ac_avatar_answer(b.w);
  }
  free(b.w);
  lstrcpynW(g_fqLast, j->path, MAX_PATH);
  lstrcpynW(g_fqLastOut, j->out, MAX_PATH);
  g_fqTurn = g_acN - 1;
  ac_avatar_set(ok ? AC_AV_SUCCESS : AC_AV_ERROR);
  ac_redraw();
  ac_status(ok ? (j->out[0] ? L"Файл сохранён — исходник не изменён" : L"Файл прочитан на этом компьютере, без модели")
               : L"Файл не прочитан или не переведён — причина в ответе");
  if (j->cvx) free(j->cvx->report);
  free(j->cvx);
  free(j->text);
  free(j);
}

static BOOL ac_fq_cancel(void) {
  if (!g_acFq) return FALSE;
  InterlockedExchange(&g_acFq->cancel, 1);
  if (g_acFq->cvx) {
    g_cvxCancelled = 1;
    if (g_cvxCancelPath[0]) aq_write(g_cvxCancelPath, "");
  }
  ac_status(L"Останавливаю после текущей страницы…");
  return TRUE;
}

#ifdef SET_TEST
/* проверочная сборка: --fqread отчёт.txt файл [чертёж] — прочитать файл, как
   «прочитай …» в чате; прочитанное — ещё и в отчёт.txt.docx (запись Word) */
static void fq_test(int argc, wchar_t **argv) {
  int pages = 0, all = 0;
  wchar_t why[600] = L"";
  wchar_t *t = fq_read_text(argv[3], argc >= 5, NULL, NULL, NULL, &pages, &all, why, 600);
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  sb_add(&b, L"pages=%d of %d\r\nwhy=%ls\r\n---\r\n%ls", pages, all, why, t ? t : L"");
  if (b.w) share_write_ex(argv[2], b.w, TRUE);
  if (t) {
    wchar_t doc[MAX_PATH];
    _snwprintf(doc, MAX_PATH, L"%ls.docx", argv[2]);
    doc[MAX_PATH - 1] = 0;
    DeleteFileW(doc);
    fq_write_docx(doc, t);
  }
  free(b.w);
  free(t);
  ExitProcess(0);
}
#endif
