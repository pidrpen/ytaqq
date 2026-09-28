/* ---- Запись простого .xlsx ---------------------------------------------------

   Один лист, строки текстом или числом, жирная шапка с переносом, рамки,
   ширины столбцов. Файл .xlsx — это zip с несколькими xml внутри; пишем его
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
} XlSheet;

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

/* cell(row, col) — текст ячейки строки данных; title — заголовок над шапкой,
   NULL или пусто — без него: шапка в первой строке */
static BOOL xl_save(const wchar_t *path, const wchar_t *title, const XlSheet *sh, int nrows,
                    const wchar_t *(*cell)(void *, int, int), void *ctx) {
  XlEntry e[6];
  memset(e, 0, sizeof(e));
  e[0].name = "[Content_Types].xml";
  xl_puts(&e[0].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
          "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
          "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
          "<Override PartName=\"/xl/workbook.xml\" "
          "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
          "<Override PartName=\"/xl/worksheets/sheet1.xml\" "
          "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
          "<Override PartName=\"/xl/styles.xml\" "
          "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
          "</Types>");
  e[1].name = "_rels/.rels";
  xl_puts(&e[1].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
          "<Relationship Id=\"rId1\" "
          "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
          "Target=\"xl/workbook.xml\"/></Relationships>");
  BOOL hasTitle = title && title[0];
  int hr = hasTitle ? 2 : 1; /* строка шапки */
  e[2].name = "xl/workbook.xml";
  xl_puts(&e[2].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
          "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
          "<sheets><sheet name=\"Лист1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
          /* заголовок и шапка печатаются на каждом листе */
          "<definedNames><definedName name=\"_xlnm.Print_Titles\" localSheetId=\"0\">"
          "'Лист1'!$1:$");
  xl_puts(&e[2].data, hasTitle ? "2" : "1");
  xl_puts(&e[2].data, "</definedName></definedNames></workbook>");
  e[3].name = "xl/_rels/workbook.xml.rels";
  xl_puts(&e[3].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
          "<Relationship Id=\"rId1\" "
          "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
          "Target=\"worksheets/sheet1.xml\"/>"
          "<Relationship Id=\"rId2\" "
          "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
          "Target=\"styles.xml\"/></Relationships>");
  /* стили: 0 — обычный, 1 — шапка (жирный, серый фон, перенос, рамка),
     2 — ячейка с рамкой и переносом, 3 — заголовок листа (жирный крупнее).
     Шрифт 14, всё по центру (с 2026.09.23.51 — под печать ведомости) */
  e[4].name = "xl/styles.xml";
  xl_puts(&e[4].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
          "<fonts count=\"3\"><font><sz val=\"14\"/><name val=\"Arial\"/></font>"
          "<font><b/><sz val=\"14\"/><name val=\"Arial\"/></font>"
          "<font><b/><sz val=\"16\"/><name val=\"Arial\"/></font></fonts>"
          "<fills count=\"3\"><fill><patternFill patternType=\"none\"/></fill>"
          "<fill><patternFill patternType=\"gray125\"/></fill>"
          "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE7E6E6\"/><bgColor indexed=\"64\"/>"
          "</patternFill></fill></fills>"
          "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
          "<border><left style=\"thin\"/><right style=\"thin\"/><top style=\"thin\"/><bottom style=\"thin\"/>"
          "<diagonal/></border></borders>"
          "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
          "<cellXfs count=\"4\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
          "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" "
          "applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" "
          "wrapText=\"1\"/></xf>"
          "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" "
          "applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
          "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" "
          "applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf></cellXfs>"
          "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
          "</styleSheet>");
  e[5].name = "xl/worksheets/sheet1.xml";
  XlBuf *s = &e[5].data;
  xl_puts(s, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
             "<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>"
             "<sheetViews><sheetView workbookViewId=\"0\">");
  char tmp[160];
  snprintf(tmp, sizeof(tmp),
           "<pane ySplit=\"%d\" topLeftCell=\"A%d\" activePane=\"bottomLeft\" state=\"frozen\"/>", hr, hr + 1);
  xl_puts(s, tmp);
  xl_puts(s, "</sheetView></sheetViews><cols>");
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
  snprintf(tmp, sizeof(tmp), "<row r=\"%d\" ht=\"60\" customHeight=\"1\">", hr);
  xl_puts(s, tmp);
  for (int c = 0; c < sh->ncols; c++) {
    char cn[4];
    xl_colname(c, cn);
    snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"1\" t=\"inlineStr\"><is><t>", cn, hr);
    xl_puts(s, tmp);
    xl_text(s, sh->head[c]);
    xl_puts(s, "</t></is></c>");
  }
  xl_puts(s, "</row>");
  for (int r = 0; r < nrows; r++) {
    int rr = r + hr + 1;
    snprintf(tmp, sizeof(tmp), "<row r=\"%d\">", rr);
    xl_puts(s, tmp);
    for (int c = 0; c < sh->ncols; c++) {
      const wchar_t *v = cell(ctx, r, c);
      char cn[4];
      xl_colname(c, cn);
      double d;
      if (xl_is_num(v, &d)) {
        snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"2\"><v>%.10g</v></c>", cn, rr, d);
        xl_puts(s, tmp);
      } else {
        snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">", cn,
                 rr);
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
  if (hasTitle) {
    snprintf(tmp, sizeof(tmp), "<mergeCells count=\"1\"><mergeCell ref=\"A1:%s1\"/></mergeCells>", last);
    xl_puts(s, tmp);
  }
  /* печать: A3 (paperSize 8) альбомный, все столбцы — в ширину одной
     страницы (fitToWidth 1), в высоту — сколько выйдет (fitToHeight 0),
     поля по бокам узкие, таблица по центру листа. Есть колонтитулы — поля
     сверху и снизу шире (с 2026.09.23.54: было 1 см, и таблица подходила
     вплотную к колонтитулу в две строки 18-м шрифтом): колонтитул с 0,8 см
     от края, таблица — с 3,3 см; сами колонтитулы не ужимаются вместе с
     таблицей (scaleWithDoc 0) — 18 и 24 на бумаге такие и есть */
  BOOL hf = (sh->header && sh->header[0]) || (sh->footer && sh->footer[0]);
  xl_puts(s, "<printOptions horizontalCentered=\"1\"/>");
  xl_puts(s, hf ? "<pageMargins left=\"0.25\" right=\"0.25\" top=\"1.3\" bottom=\"1.3\" header=\"0.3\" "
                  "footer=\"0.3\"/>"
                : "<pageMargins left=\"0.25\" right=\"0.25\" top=\"0.4\" bottom=\"0.4\" header=\"0.2\" "
                  "footer=\"0.2\"/>");
  xl_puts(s, "<pageSetup paperSize=\"8\" orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>");
  if (hf) {
    xl_puts(s, "<headerFooter scaleWithDoc=\"0\">");
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
  BOOL ok = TRUE;
  for (int i = 0; i < 6; i++)
    if (!e[i].data.p) ok = FALSE;
  if (ok) ok = xl_zip_write(path, e, 6);
  for (int i = 0; i < 6; i++) free(e[i].data.p);
  return ok;
}
