/* file_ask_test.c — «прочитай / переведи файл» в чате (file_ask.c): путь в тексте,
   что просят, в какой формат и чем выполнять. Windows не нужен:
     gcc -O2 -Wall -Werror -o file_ask_test file_ask_test.c && ./file_ask_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "file_ask.c"

static int g_n, g_fail;
static void out8(const wchar_t *w) {
  for (; w && *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}

/* «диск»: какие файлы и папки есть */
static const wchar_t *const kFiles[] = {
    L"C:\\Сканы\\письмо.pdf", L"C:\\Сканы\\лист 1.tif", L"C:\\Работа\\Отчёт за май.docx", L"C:\\Работа\\Ведомость.xlsx",
    L"\\\\srv\\archive\\АДЕ 3424-123.01.tif", L"C:\\Работа\\заметки.txt", L"C:\\Фото\\снимок.png", L"D:\\PDF\\старый.doc",
    L"C:\\Работа\\модель.stl"};
static const wchar_t *const kDirs[] = {L"C:\\Сканы", L"C:\\Работа", L"D:\\Готово", L"D:\\PDF"};
static int fake_exists(const wchar_t *p, int dir) {
  const wchar_t *const *l = dir ? kDirs : kFiles;
  int n = dir ? (int)(sizeof(kDirs) / sizeof(kDirs[0])) : (int)(sizeof(kFiles) / sizeof(kFiles[0]));
  for (int i = 0; i < n; i++)
    if (!wcscmp(p, l[i])) return 1;
  return 0;
}

/* разбор как в чате: путь-исходник, за ним — папка результата */
static void ask(const wchar_t *q, int last, int out, FqAsk *a, wchar_t *src, wchar_t *dst) {
  int span[4], n = 0;
  src[0] = dst[0] = 0;
  if (fq_find_path(q, 0, 0, fake_exists, &span[0], &span[1])) {
    n = 1;
    wcsncpy(src, q + span[0], (size_t)(span[1] - span[0]));
    src[span[1] - span[0]] = 0;
    for (wchar_t *c = src; *c; c++)
      if (*c == L'/') *c = L'\\';
    if (fq_find_path(q, span[1], 1, fake_exists, &span[2], &span[3])) {
      n = 2;
      wcsncpy(dst, q + span[2], (size_t)(span[3] - span[2]));
      dst[span[3] - span[2]] = 0;
    }
  }
  fq_parse(q, span, n, last, out, a);
}

static const char *kWant[] = {"нет", "прочитать", "перевести", "показать"};

static void one(const wchar_t *q, int last, int want, int target, const wchar_t *src) {
  FqAsk a;
  wchar_t s[300], d[300];
  ask(q, last, last, &a, s, d);
  g_n++;
  int bad = a.want != want || (want == FQW_CONVERT && a.target != target) || (src && wcscmp(s, src));
  if (bad) {
    g_fail++;
    printf("ОШИБКА «");
    out8(q);
    printf("»: %s → %s, формат ", kWant[want], kWant[a.want]);
    out8(kFqName[target]);
    printf(" → ");
    out8(kFqName[a.target]);
    printf(", путь «");
    out8(s);
    printf("»\n");
  }
}

static void route(const wchar_t *path, int target, int want, int bench) {
  int b, r = fq_route(fq_kind(path), fq_image_format(path), target, &b);
  g_n++;
  if (r != want || (want == FQA_BENCH && b != bench)) {
    g_fail++;
    printf("ОШИБКА путь ");
    out8(path);
    printf(" → ");
    out8(kFqName[target]);
    printf(": %d/%d, ждали %d/%d\n", r, b, want, bench);
  }
}

int main(void) {
  /* прочитать */
  one(L"прочитай C:\\Сканы\\письмо.pdf", 0, FQW_READ, 0, L"C:\\Сканы\\письмо.pdf");
  one(L"Прочитай файл C:\\Сканы\\лист 1.tif пожалуйста", 0, FQW_READ, 0, L"C:\\Сканы\\лист 1.tif");
  one(L"распознай текст из «C:\\Фото\\снимок.png»", 0, FQW_READ, 0, L"C:\\Фото\\снимок.png");
  one(L"что написано в C:\\Сканы\\письмо.pdf?", 0, FQW_READ, 0, NULL);
  one(L"прочитай pdf C:\\Сканы\\письмо.pdf", 0, FQW_READ, 0, NULL);
  one(L"прочитай картинку C:\\Фото\\снимок.png", 0, FQW_READ, 0, NULL);
  one(L"прочитай таблицу C:\\Работа\\Ведомость.xlsx", 0, FQW_READ, 0, NULL);
  one(L"прочитай текстовый файл C:\\Работа\\заметки.txt", 0, FQW_READ, 0, NULL);
  one(L"прочти \\\\srv\\archive\\АДЕ 3424-123.01.tif", 0, FQW_READ, 0, L"\\\\srv\\archive\\АДЕ 3424-123.01.tif");
  one(L"прочитай C:/Работа/Отчёт за май.docx", 0, FQW_READ, 0, L"C:\\Работа\\Отчёт за май.docx");

  /* перевести */
  one(L"переведи C:\\Работа\\Отчёт за май.docx в pdf", 0, FQW_CONVERT, FQ_PDF, L"C:\\Работа\\Отчёт за май.docx");
  one(L"конвертируй C:\\Работа\\Ведомость.xlsx в CSV", 0, FQW_CONVERT, FQ_CSV, NULL);
  one(L"сделай из C:\\Сканы\\письмо.pdf ворд", 0, FQW_CONVERT, FQ_DOCX, NULL);
  one(L"сделай ворд из C:\\Сканы\\письмо.pdf", 0, FQW_CONVERT, FQ_DOCX, NULL);
  one(L"распознай C:\\Сканы\\лист 1.tif в word", 0, FQW_CONVERT, FQ_DOCX, NULL);
  one(L"распознай скан C:\\Сканы\\лист 1.tif в текст", 0, FQW_CONVERT, FQ_TXT, NULL);
  one(L"переведи pdf C:\\Сканы\\письмо.pdf в ворд", 0, FQW_CONVERT, FQ_DOCX, NULL);
  one(L"из pdf в jpg: C:\\Сканы\\письмо.pdf", 0, FQW_CONVERT, FQ_JPG, NULL);
  one(L"C:\\Фото\\снимок.png в jpg", 0, FQW_CONVERT, FQ_JPG, NULL);
  one(L"сохрани C:\\Фото\\снимок.png как TIFF", 0, FQW_CONVERT, FQ_TIF, NULL);
  one(L"переведи C:\\Сканы\\письмо.pdf в картинки", 0, FQW_CONVERT, FQ_PNG, NULL);
  one(L"мне нужен C:\\Работа\\Отчёт за май.docx в пдф", 0, FQW_CONVERT, FQ_PDF, NULL);
  one(L"запакуй C:\\Работа\\Ведомость.xlsx", 0, FQW_CONVERT, FQ_ZIP, NULL);
  one(L"переведи C:\\Сканы\\письмо.pdf в таблицу", 0, FQW_CONVERT, FQ_XLSX, NULL);
  one(L"переведи D:\\PDF\\старый.doc в pdf", 0, FQW_CONVERT, FQ_PDF, L"D:\\PDF\\старый.doc");

  /* папка результата и рабочий стол */
  {
    FqAsk a;
    wchar_t s[300], d[300];
    ask(L"переведи C:\\Работа\\Отчёт за май.docx в pdf и положи в D:\\Готово", 0, 0, &a, s, d);
    g_n++;
    if (a.want != FQW_CONVERT || a.target != FQ_PDF || wcscmp(d, L"D:\\Готово")) g_fail++, printf("ОШИБКА папка результата\n");
    ask(L"переведи C:\\Работа\\Отчёт за май.docx в pdf на рабочий стол", 0, 0, &a, s, d);
    g_n++;
    if (a.want != FQW_CONVERT || a.target != FQ_PDF || !a.desktop) g_fail++, printf("ОШИБКА рабочий стол\n");
    /* папка D:\PDF — не цель «pdf» */
    ask(L"переведи C:\\Фото\\снимок.png в jpg в D:\\PDF", 0, 0, &a, s, d);
    g_n++;
    if (a.target != FQ_JPG || wcscmp(d, L"D:\\PDF")) g_fail++, printf("ОШИБКА папка D:\\PDF\n");
    ask(L"прочитай чертёж C:\\Сканы\\лист 1.tif", 0, 0, &a, s, d);
    g_n++;
    if (a.want != FQW_READ || !a.drawing) g_fail++, printf("ОШИБКА чертёж\n");
  }

  /* продолжение о том же файле */
  one(L"а теперь в pdf", 1, FQW_CONVERT, FQ_PDF, NULL);
  one(L"сохрани в ворд", 1, FQW_CONVERT, FQ_DOCX, NULL);
  one(L"переведи его в txt", 1, FQW_CONVERT, FQ_TXT, NULL);
  one(L"прочитай его ещё раз", 1, FQW_READ, 0, NULL);
  one(L"покажи в проводнике", 1, FQW_REVEAL, 0, NULL);
  one(L"где файл?", 1, FQW_REVEAL, 0, NULL);
  one(L"прочитай мне стих про осень", 1, FQW_NO, 0, NULL);
  one(L"сохрани в ворд", 0, FQW_NO, 0, NULL);
  one(L"а теперь в pdf", 0, FQW_NO, 0, NULL);

  /* не это: другим помощникам и модели */
  one(L"как перевести C:\\Сканы\\письмо.pdf в word?", 0, FQW_NO, 0, NULL);
  one(L"прочитай C:\\Сканы\\письмо.pdf и скажи, какой там материал", 0, FQW_NO, 0, NULL);
  one(L"прочитай C:\\Работа\\Ведомость.xlsx и посчитай сумму", 0, FQW_NO, 0, NULL);
  one(L"сверь C:\\Работа\\Ведомость.xlsx с союзом", 0, FQW_NO, 0, NULL);
  one(L"скопируй C:\\Работа\\Ведомость.xlsx в D:\\Готово", 0, FQW_NO, 0, NULL);
  one(L"переведи C:\\Работа\\заметки.txt на английский", 0, FQW_NO, 0, NULL);
  one(L"маршрут по чертежу C:\\Сканы\\лист 1.tif", 0, FQW_NO, 0, NULL);
  one(L"открой C:\\Сканы\\письмо.pdf", 0, FQW_NO, 0, NULL);
  one(L"переведи в pdf", 0, FQW_NO, 0, NULL);
  one(L"прочитай C:\\Сканы\\нет такого.pdf", 0, FQW_NO, 0, NULL);
  one(L"сделай копию C:\\Сканы\\письмо.pdf", 0, FQW_NO, 0, NULL);
  one(L"переведи текст на английский", 1, FQW_NO, 0, NULL);

  /* чем выполнять */
  route(L"C:\\a.pdf", FQ_NONE, FQA_TEXT, -1);
  route(L"C:\\a.stl", FQ_NONE, FQA_NONE, -1);
  route(L"C:\\a.pdf", FQ_DOCX, FQA_SAVE, -1);
  route(L"C:\\a.pdf", FQ_TXT, FQA_SAVE, -1);
  route(L"C:\\a.pdf", FQ_JPG, FQA_BENCH, 1);
  route(L"C:\\a.pdf", FQ_PDF, FQA_SAME, -1);
  route(L"C:\\a.TIF", FQ_PDF, FQA_BENCH, 4);
  route(L"C:\\a.tif", FQ_TIF, FQA_SAME, -1);
  route(L"C:\\a.tiff", FQ_PNG, FQA_BENCH, 0);
  route(L"C:\\a.jpeg", FQ_BMP, FQA_BENCH, 2);
  route(L"C:\\a.png", FQ_DOCX, FQA_SAVE, -1);
  route(L"C:\\a.docx", FQ_PDF, FQA_BENCH, 4);
  route(L"C:\\a.rtf", FQ_TXT, FQA_BENCH, 5);
  route(L"C:\\a.doc", FQ_DOCX, FQA_SAME, -1);
  route(L"C:\\a.docx", FQ_PNG, FQA_NONE, -1);
  route(L"C:\\a.xls", FQ_CSV, FQA_BENCH, 6);
  route(L"C:\\a.xlsx", FQ_PDF, FQA_BENCH, 4);
  route(L"C:\\a.xlsx", FQ_TXT, FQA_SAVE, -1);
  route(L"C:\\a.xlsx", FQ_DOCX, FQA_AGENT, -1);
  route(L"C:\\a.pdf", FQ_XLSX, FQA_AGENT, -1);
  route(L"C:\\a.xlsx", FQ_XLSX, FQA_SAME, -1);
  route(L"C:\\a.txt", FQ_DOCX, FQA_SAVE, -1);
  route(L"C:\\a.csv", FQ_PDF, FQA_NONE, -1);
  route(L"C:\\a.stl", FQ_ZIP, FQA_BENCH, 7);
  route(L"C:\\папка.v2\\a", FQ_PDF, FQA_NONE, -1);
  g_n++;
  if (!wcsstr(fq_can(FQK_PDF), L"Word") || !wcsstr(fq_can(FQK_OTHER), L"PDF")) g_fail++, printf("ОШИБКА fq_can\n");

  printf("file_ask: %d проверок, ошибок %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
