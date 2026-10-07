/* ---- Запись простого .xlsx ---------------------------------------------------

   Листы со строками текстом или числом, жирная шапка с переносом, рамки,
   ширины столбцов. xl_save — один лист (маршрутная ведомость), xl_save_book —
   несколько (выгрузка из PLM, export.c). Файл .xlsx — это zip с несколькими xml внутри; пишем его
   сами, без сжатия (метод «store»): Excel такие открывает, а библиотек не
   нужно. Строки — «inlineStr», без таблицы общих строк. */

typedef struct {
  unsigned char *p;
  size_t n, cap;
} XlBuf;

static void xl_put(XlBuf *b, const void *d, size_t n) {
  if (!n) return;
  if (b->n + n > b->cap) {
    size_t nc = b->cap ? b->cap * 2 : 65536;
    while (nc < b->n + n) nc *= 2;
    unsigned char *np = (unsigned char *)realloc(b->p, nc);
    if (!np) return;
    b->p = np;
    b->cap = nc;
  }
  memcpy(b->p + b->n, d, n);
  b->n += n;
}
static void xl_puts(XlBuf *b, const char *s) { xl_put(b, s, strlen(s)); }
static void xl_u16(XlBuf *b, unsigned v) {
  unsigned char c[2] = {(unsigned char)(v & 255), (unsigned char)(v >> 8)};
  xl_put(b, c, 2);
}
static void xl_u32(XlBuf *b, unsigned long v) {
  unsigned char c[4] = {(unsigned char)(v & 255), (unsigned char)((v >> 8) & 255), (unsigned char)((v >> 16) & 255),
                        (unsigned char)((v >> 24) & 255)};
  xl_put(b, c, 4);
}

/* текст ячейки: UTF-8 и без знаков, ломающих xml */
static void xl_text(XlBuf *b, const wchar_t *s) {
  for (; s && *s; s++) {
    wchar_t c = *s;
    if (c == L'&') xl_puts(b, "&amp;");
    else if (c == L'<') xl_puts(b, "&lt;");
    else if (c == L'>') xl_puts(b, "&gt;");
    else if (c == L'"') xl_puts(b, "&quot;");
    else if (c < 0x20 && c != L'\t' && c != L'\n') xl_puts(b, " ");
    else {
      char u[8];
      int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1, u, 8, NULL, NULL);
      /* суррогатная пара: вторую половину берём вместе с первой */
      if (c >= 0xD800 && c <= 0xDBFF && s[1]) {
        n = WideCharToMultiByte(CP_UTF8, 0, s, 2, u, 8, NULL, NULL);
        s++;
      }
      if (n > 0) xl_put(b, u, (size_t)n);
    }
  }
}

static unsigned long xl_crc32(const unsigned char *p, size_t n) {
  static unsigned long t[256];
  static int ready;
  if (!ready) {
    for (unsigned long i = 0; i < 256; i++) {
      unsigned long c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    ready = 1;
  }
  unsigned long c = 0xFFFFFFFFUL;
  for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
  return c ^ 0xFFFFFFFFUL;
}

typedef struct {
  const char *name;
  XlBuf data;
  unsigned long crc, off;
} XlEntry;

/* zip без сжатия из готовых частей */
static BOOL xl_zip_write(const wchar_t *path, XlEntry *e, int n) {
  XlBuf z = {0};
  for (int i = 0; i < n; i++) {
    e[i].crc = xl_crc32(e[i].data.p, e[i].data.n);
    e[i].off = (unsigned long)z.n;
    size_t nl = strlen(e[i].name);
    xl_u32(&z, 0x04034b50UL);
    xl_u16(&z, 20);
    xl_u16(&z, 0x0800); /* имена в UTF-8 */
    xl_u16(&z, 0);      /* без сжатия */
    xl_u16(&z, 0);
    xl_u16(&z, 0x21); /* 1980-01-01 */
    xl_u32(&z, e[i].crc);
    xl_u32(&z, (unsigned long)e[i].data.n);
    xl_u32(&z, (unsigned long)e[i].data.n);
    xl_u16(&z, (unsigned)nl);
    xl_u16(&z, 0);
    xl_put(&z, e[i].name, nl);
    xl_put(&z, e[i].data.p, e[i].data.n);
  }
  unsigned long cd = (unsigned long)z.n;
  for (int i = 0; i < n; i++) {
    size_t nl = strlen(e[i].name);
    xl_u32(&z, 0x02014b50UL);
    xl_u16(&z, 20);
    xl_u16(&z, 20);
    xl_u16(&z, 0x0800);
    xl_u16(&z, 0);
    xl_u16(&z, 0);
    xl_u16(&z, 0x21);
    xl_u32(&z, e[i].crc);
    xl_u32(&z, (unsigned long)e[i].data.n);
    xl_u32(&z, (unsigned long)e[i].data.n);
    xl_u16(&z, (unsigned)nl);
    xl_u16(&z, 0);
    xl_u16(&z, 0);
    xl_u16(&z, 0);
    xl_u16(&z, 0);
    xl_u32(&z, 0);
    xl_u32(&z, e[i].off);
    xl_put(&z, e[i].name, nl);
  }
  unsigned long cdn = (unsigned long)z.n - cd;
  xl_u32(&z, 0x06054b50UL);
  xl_u16(&z, 0);
  xl_u16(&z, 0);
  xl_u16(&z, (unsigned)n);
  xl_u16(&z, (unsigned)n);
  xl_u32(&z, cdn);
  xl_u32(&z, cd);
  xl_u16(&z, 0);
  BOOL ok = FALSE;
  if (z.p) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
      DWORD w = 0;
      ok = WriteFile(f, z.p, (DWORD)z.n, &w, NULL) && w == (DWORD)z.n;
      CloseHandle(f);
      if (!ok) DeleteFileW(path);
    }
  }
  free(z.p);
  return ok;
}

/* Ячейка листа: текст или число. Число — если строка целиком число (запятая
   или точка): тогда Excel сможет по нему считать. */
typedef struct {
  int ncols;
  const wchar_t *const *head;
  const int *width; /* в символах */
  /* колонтитулы в кодах Excel («&L&18текст&R&24текст»), NULL — нет */
  const wchar_t *header, *footer;
  /* столбец-произведение: prodCol (номер + 1, 0 — нет) = prodA × prodB
     формулой Excel — поменяли число в файле, пересчиталось само */
  int prodCol, prodA, prodB;
  int zoom; /* масштаб «Разметки страницы», %; 0 — 70 */
  /* простой лист данных (выгрузка): шрифт 11, текст слева, высота строки —
     своя у каждой, фильтр на шапке, печать A4 в ширину листа. 0 — как у
     маршрутной ведомости: шрифт 14 под печать A3 */
  int plain;
} XlSheet;

/* лист книги для xl_save_book */
typedef struct {
  const wchar_t *name; /* имя ярлыка: до 31 знака, без []:*?/\ */
  const wchar_t *title;
  const XlSheet *sh;
  int nrows;
  const wchar_t *(*cell)(void *, int, int);
  void *ctx;
} XlBookSheet;

static BOOL xl_is_num(const wchar_t *s, double *v) {
  if (!s || !*s) return FALSE;
  wchar_t t[64];
  lstrcpynW(t, s, 64);
  for (wchar_t *c = t; *c; c++) {
    if (*c == L',') *c = L'.';
    if (!(iswdigit(*c) || *c == L'.' || *c == L'-')) return FALSE;
  }
  wchar_t *end = NULL;
  *v = wcstod(t, &end);
  return end && !*end && end != t;
}

static void xl_colname(int c, char *out) {
  if (c < 26) {
    out[0] = (char)('A' + c);
    out[1] = 0;
  } else {
    out[0] = (char)('A' + c / 26 - 1);
    out[1] = (char)('A' + c % 26);
    out[2] = 0;
  }
}

/* Сколько строк займёт текст в столбце шириной w символов при переносе по
   словам (и по «\n»). Кириллица шире цифры, по которой Excel меряет ширину, —
   считаем в строке на символ меньше. */
static int xl_lines(const wchar_t *t, int w) {
  if (!t || !*t) return 1;
  int cap = w > 3 ? w - 1 : 2, lines = 1, col = 0;
  while (*t) {
    if (*t == L'\n') {
      lines++;
      col = 0;
      t++;
      continue;
    }
    int len = 0;
    while (t[len] && t[len] != L' ' && t[len] != L'\n') len++;
    if (len) {
      if (col && col + 1 + len > cap) { /* слово на новую строку */
        lines++;
        col = 0;
      }
      if (col) col++;
      while (len > cap) { /* слово длиннее строки — Excel рвёт его */
        lines++;
        len -= cap;
      }
      col += len;
      t += len;
      while (*t && *t != L' ' && *t != L'\n') t++;
    }
    while (*t == L' ') t++;
  }
  return lines;
}

/* xml одного листа. cell(row, col) — текст ячейки строки данных; title —
   заголовок над шапкой, NULL или пусто — без него: шапка в первой строке.
   Стили — из xl_styles: 1/2 — шапка и ячейка ведомости (шрифт 14), 3 —
   заголовок, 4/5 — шапка и ячейка простого листа (шрифт 11, слева). */
static void xl_sheet_xml(XlBuf *s, const wchar_t *title, const XlSheet *sh, int nrows,
                         const wchar_t *(*cell)(void *, int, int), void *ctx) {
  BOOL hasTitle = title && title[0];
  int hr = hasTitle ? 2 : 1; /* строка шапки */
  const char *sHead = sh->plain ? "4" : "1", *sCell = sh->plain ? "5" : "2";
  xl_puts(s, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
             "<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>"
             "<sheetViews>");
  char tmp[200];
  BOOL hf = (sh->header && sh->header[0]) || (sh->footer && sh->footer[0]);
  if (hf) {
    /* есть колонтитулы — открывается сразу «Разметкой страницы»: листы как
       на бумаге, колонтитулы видны (с 2026.09.23.57). Закрепить шапку в
       этом виде Excel не даёт — она и так повторяется на каждом листе */
    snprintf(tmp, sizeof(tmp), "<sheetView view=\"pageLayout\" zoomScalePageLayoutView=\"%d\" workbookViewId=\"0\"/>",
             sh->zoom >= 10 && sh->zoom <= 400 ? sh->zoom : 70);
    xl_puts(s, tmp);
  } else {
    xl_puts(s, "<sheetView workbookViewId=\"0\">");
    snprintf(tmp, sizeof(tmp),
             "<pane ySplit=\"%d\" topLeftCell=\"A%d\" activePane=\"bottomLeft\" state=\"frozen\"/>", hr, hr + 1);
    xl_puts(s, tmp);
    xl_puts(s, "</sheetView>");
  }
  xl_puts(s, "</sheetViews><cols>");
  for (int c = 0; c < sh->ncols; c++) {
    snprintf(tmp, sizeof(tmp), "<col min=\"%d\" max=\"%d\" width=\"%d\" customWidth=\"1\"/>", c + 1, c + 1,
             sh->width[c]);
    xl_puts(s, tmp);
  }
  xl_puts(s, "</cols><sheetData>");
  /* строка 1 — заголовок (если есть), затем шапка, дальше данные */
  if (hasTitle) {
    xl_puts(s, "<row r=\"1\" ht=\"28\" customHeight=\"1\"><c r=\"A1\" s=\"3\" t=\"inlineStr\"><is><t>");
    xl_text(s, title);
    xl_puts(s, "</t></is></c></row>");
  }
  snprintf(tmp, sizeof(tmp), "<row r=\"%d\" ht=\"%d\" customHeight=\"1\">", hr, sh->plain ? 32 : 60);
  xl_puts(s, tmp);
  for (int c = 0; c < sh->ncols; c++) {
    char cn[4];
    xl_colname(c, cn);
    snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"%s\" t=\"inlineStr\"><is><t>", cn, hr, sHead);
    xl_puts(s, tmp);
    xl_text(s, sh->head[c]);
    xl_puts(s, "</t></is></c>");
  }
  xl_puts(s, "</row>");
  /* Ведомость: строки одной высоты — по самой высокой (сколько строк текста
     в ней при переносе), шрифт 14 — 17 пт на строку и 4 на поля (с
     2026.09.23.61; раньше высоту подбирал Excel у каждой строки свою).
     Простой лист: у каждой строки своя высота, шрифт 11 — 15 пт на строку;
     ширина столбца мерится цифрой шрифта 14, а 11-го в неё входит больше */
  int maxLines = 1;
  if (!sh->plain) {
    for (int r = 0; r < nrows; r++)
      for (int c = 0; c < sh->ncols; c++) {
        int l = xl_lines(cell(ctx, r, c), sh->width[c]);
        if (l > maxLines) maxLines = l;
      }
    if (maxLines > 12) maxLines = 12;
  }
  for (int r = 0; r < nrows; r++) {
    int rr = r + hr + 1;
    double rowHt = maxLines * 17.0 + 4.0;
    if (sh->plain) {
      int lines = 1;
      for (int c = 0; c < sh->ncols; c++) {
        int l = xl_lines(cell(ctx, r, c), sh->width[c] * 5 / 4);
        if (l > lines) lines = l;
      }
      if (lines > 8) lines = 8;
      rowHt = lines * 15.0 + 3.0;
    }
    snprintf(tmp, sizeof(tmp), "<row r=\"%d\" ht=\"%.1f\" customHeight=\"1\">", rr, rowHt);
    xl_puts(s, tmp);
    for (int c = 0; c < sh->ncols; c++) {
      const wchar_t *v = cell(ctx, r, c);
      char cn[4];
      xl_colname(c, cn);
      double d, a, b;
      if (sh->prodCol == c + 1 && xl_is_num(cell(ctx, r, sh->prodA), &a) && xl_is_num(cell(ctx, r, sh->prodB), &b)) {
        char ca[4], cb[4];
        xl_colname(sh->prodA, ca);
        xl_colname(sh->prodB, cb);
        snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"%s\"><f>%s%d*%s%d</f><v>%.10g</v></c>", cn, rr, sCell, ca, rr,
                 cb, rr, a * b);
        xl_puts(s, tmp);
      } else if (xl_is_num(v, &d)) {
        snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"%s\"><v>%.10g</v></c>", cn, rr, sCell, d);
        xl_puts(s, tmp);
      } else {
        snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"%s\" t=\"inlineStr\"><is><t xml:space=\"preserve\">", cn,
                 rr, sCell);
        xl_puts(s, tmp);
        xl_text(s, v ? v : L"");
        xl_puts(s, "</t></is></c>");
      }
    }
    xl_puts(s, "</row>");
  }
  /* заголовок — объединён на всю ширину таблицы, по центру */
  char last[4];
  xl_colname(sh->ncols > 0 ? sh->ncols - 1 : 0, last);
  xl_puts(s, "</sheetData>");
  if (sh->plain && nrows > 0) { /* фильтр на шапке: отобрать по детали, участку… */
    snprintf(tmp, sizeof(tmp), "<autoFilter ref=\"A%d:%s%d\"/>", hr, last, hr + nrows);
    xl_puts(s, tmp);
  }
  if (hasTitle) {
    snprintf(tmp, sizeof(tmp), "<mergeCells count=\"1\"><mergeCell ref=\"A1:%s1\"/></mergeCells>", last);
    xl_puts(s, tmp);
  }
  if (sh->plain) {
    /* простой лист: A4 (paperSize 9) альбомный, в ширину одной страницы */
    xl_puts(s, "<pageMargins left=\"0.25\" right=\"0.25\" top=\"0.4\" bottom=\"0.4\" header=\"0.2\" footer=\"0.2\"/>"
               "<pageSetup paperSize=\"9\" orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>"
               "</worksheet>");
    return;
  }
  /* печать: A3 (paperSize 8) альбомный, все столбцы — в ширину одной
     страницы (fitToWidth 1), в высоту — сколько выйдет (fitToHeight 0),
     поля по бокам узкие, таблица по центру листа. Есть колонтитулы — поля
     сверху и снизу шире (с 2026.09.23.54: было 1 см, и таблица подходила
     вплотную к колонтитулу в две строки 18-м шрифтом): колонтитул с 0,8 см
     от края, таблица — с 2,5 см. Колонтитулы ужимаются вместе с таблицей
     при вписывании в ширину, как в шаблоне ведомости (в 2026.09.23.54 было
     scaleWithDoc 0 — на печати выходили огромными рядом с таблицей) */
  xl_puts(s, "<printOptions horizontalCentered=\"1\"/>");
  xl_puts(s, hf ? "<pageMargins left=\"0.25\" right=\"0.25\" top=\"1\" bottom=\"1\" header=\"0.3\" "
                  "footer=\"0.3\"/>"
                : "<pageMargins left=\"0.25\" right=\"0.25\" top=\"0.4\" bottom=\"0.4\" header=\"0.2\" "
                  "footer=\"0.2\"/>");
  /* Масштаб вписывания — сразу в файле, как в шаблоне ведомости (scale="51"):
     без него Excel до первого предпросмотра печати показывает «Разметку
     страницы» неверно — пустоты по бокам (так было до 2026.09.23.60). Ширина
     столбца в пикселях при 100% ≈ символы × 10 + 5 (цифра Arial 14 — 10
     точек); ширина листа A3 альбомного без полей — (16,54 − 0,5) дюйма × 96. */
  long px = 0;
  for (int c = 0; c < sh->ncols; c++) px += sh->width[c] * 10 + 5;
  int scale = px > 0 ? (int)((16.54 - 0.5) * 96 * 100 / px) : 100;
  if (scale > 100) scale = 100;
  if (scale < 10) scale = 10;
  snprintf(tmp, sizeof(tmp),
           "<pageSetup paperSize=\"8\" scale=\"%d\" orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>",
           scale);
  xl_puts(s, tmp);
  if (hf) {
    xl_puts(s, "<headerFooter>");
    if (sh->header && sh->header[0]) {
      xl_puts(s, "<oddHeader>");
      xl_text(s, sh->header);
      xl_puts(s, "</oddHeader>");
    }
    if (sh->footer && sh->footer[0]) {
      xl_puts(s, "<oddFooter>");
      xl_text(s, sh->footer);
      xl_puts(s, "</oddFooter>");
    }
    xl_puts(s, "</headerFooter>");
  }
  xl_puts(s, "</worksheet>");
}

#define XL_MAX_SHEETS 12

/* Книга из нескольких листов. Ярлыки — как названы, печатаемая шапка
   (заголовок и строка столбцов) повторяется на каждой странице каждого листа. */
static BOOL xl_save_book(const wchar_t *path, const XlBookSheet *bs, int n) {
  if (n < 1 || n > XL_MAX_SHEETS) return FALSE;
  XlEntry e[5 + XL_MAX_SHEETS];
  memset(e, 0, sizeof(e));
  char tmp[400];
  e[0].name = "[Content_Types].xml";
  xl_puts(&e[0].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
          "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
          "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
          "<Override PartName=\"/xl/workbook.xml\" "
          "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>");
  for (int i = 0; i < n; i++) {
    snprintf(tmp, sizeof(tmp),
             "<Override PartName=\"/xl/worksheets/sheet%d.xml\" "
             "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>",
             i + 1);
    xl_puts(&e[0].data, tmp);
  }
  xl_puts(&e[0].data, "<Override PartName=\"/xl/styles.xml\" "
                      "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
                      "</Types>");
  e[1].name = "_rels/.rels";
  xl_puts(&e[1].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
          "<Relationship Id=\"rId1\" "
          "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
          "Target=\"xl/workbook.xml\"/></Relationships>");
  e[2].name = "xl/workbook.xml";
  XlBuf *w = &e[2].data;
  xl_puts(w, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
             "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>");
  for (int i = 0; i < n; i++) {
    /* имя ярлыка: без запрещённых знаков, до 31 символа */
    wchar_t nm[32];
    lstrcpynW(nm, bs[i].name && bs[i].name[0] ? bs[i].name : L"Лист", 32);
    for (wchar_t *c = nm; *c; c++)
      if (wcschr(L"[]:*?/\\'", *c)) *c = L' ';
    xl_puts(w, "<sheet name=\"");
    xl_text(w, nm);
    snprintf(tmp, sizeof(tmp), "\" sheetId=\"%d\" r:id=\"rId%d\"/>", i + 1, i + 1);
    xl_puts(w, tmp);
  }
  xl_puts(w, "</sheets><definedNames>");
  for (int i = 0; i < n; i++) {
    snprintf(tmp, sizeof(tmp), "<definedName name=\"_xlnm.Print_Titles\" localSheetId=\"%d\">'", i);
    xl_puts(w, tmp);
    wchar_t nm[32];
    lstrcpynW(nm, bs[i].name && bs[i].name[0] ? bs[i].name : L"Лист", 32);
    for (wchar_t *c = nm; *c; c++)
      if (wcschr(L"[]:*?/\\'", *c)) *c = L' ';
    xl_text(w, nm);
    snprintf(tmp, sizeof(tmp), "'!$1:$%d</definedName>", bs[i].title && bs[i].title[0] ? 2 : 1);
    xl_puts(w, tmp);
  }
  xl_puts(w, "</definedNames><calcPr fullCalcOnLoad=\"1\"/></workbook>");
  e[3].name = "xl/_rels/workbook.xml.rels";
  XlBuf *rl = &e[3].data;
  xl_puts(rl, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
  for (int i = 0; i < n; i++) {
    snprintf(tmp, sizeof(tmp),
             "<Relationship Id=\"rId%d\" "
             "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
             "Target=\"worksheets/sheet%d.xml\"/>",
             i + 1, i + 1);
    xl_puts(rl, tmp);
  }
  snprintf(tmp, sizeof(tmp),
           "<Relationship Id=\"rId%d\" "
           "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
           "Target=\"styles.xml\"/></Relationships>",
           n + 1);
  xl_puts(rl, tmp);
  /* стили: 0 — обычный, 1 — шапка (жирный, серый фон, перенос, рамка),
     2 — ячейка с рамкой и переносом, 3 — заголовок листа (жирный крупнее).
     Шрифт 14, всё по центру (с 2026.09.23.51 — под печать ведомости).
     4, 5 — шапка и ячейка простого листа выгрузки: шрифт 11, ячейка слева
     и сверху (с 2026.09.23.78) */
  e[4].name = "xl/styles.xml";
  xl_puts(&e[4].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
          "<fonts count=\"5\"><font><sz val=\"14\"/><name val=\"Arial\"/></font>"
          "<font><b/><sz val=\"14\"/><name val=\"Arial\"/></font>"
          "<font><b/><sz val=\"16\"/><name val=\"Arial\"/></font>"
          "<font><sz val=\"11\"/><name val=\"Arial\"/></font>"
          "<font><b/><sz val=\"11\"/><name val=\"Arial\"/></font></fonts>"
          "<fills count=\"3\"><fill><patternFill patternType=\"none\"/></fill>"
          "<fill><patternFill patternType=\"gray125\"/></fill>"
          "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE7E6E6\"/><bgColor indexed=\"64\"/>"
          "</patternFill></fill></fills>"
          "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
          "<border><left style=\"thin\"/><right style=\"thin\"/><top style=\"thin\"/><bottom style=\"thin\"/>"
          "<diagonal/></border></borders>"
          "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
          "<cellXfs count=\"6\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
          "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" "
          "applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" "
          "wrapText=\"1\"/></xf>"
          "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" "
          "applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
          "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" "
          "applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
          "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" "
          "applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" "
          "wrapText=\"1\"/></xf>"
          "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyBorder=\"1\" "
          "applyAlignment=\"1\"><alignment horizontal=\"left\" vertical=\"top\" wrapText=\"1\"/></xf></cellXfs>"
          "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
          "</styleSheet>");
  static char sheetNames[XL_MAX_SHEETS][32];
  for (int i = 0; i < n; i++) {
    snprintf(sheetNames[i], sizeof(sheetNames[i]), "xl/worksheets/sheet%d.xml", i + 1);
    e[5 + i].name = sheetNames[i];
    xl_sheet_xml(&e[5 + i].data, bs[i].title, bs[i].sh, bs[i].nrows, bs[i].cell, bs[i].ctx);
  }
  int ne = 5 + n;
  BOOL ok = TRUE;
  for (int i = 0; i < ne; i++)
    if (!e[i].data.p) ok = FALSE;
  if (ok) ok = xl_zip_write(path, e, ne);
  for (int i = 0; i < ne; i++) free(e[i].data.p);
  return ok;
}

/* один лист «Лист1» — маршрутная ведомость */
static BOOL xl_save(const wchar_t *path, const wchar_t *title, const XlSheet *sh, int nrows,
                    const wchar_t *(*cell)(void *, int, int), void *ctx) {
  XlBookSheet one = {L"Лист1", title, sh, nrows, cell, ctx};
  return xl_save_book(path, &one, 1);
}
