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

/* cell(row, col) — текст ячейки строки данных; title — заголовок над шапкой */
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
  e[2].name = "xl/workbook.xml";
  xl_puts(&e[2].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
          "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
          "<sheets><sheet name=\"Лист1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");
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
     2 — ячейка с рамкой и переносом, 3 — заголовок листа (жирный крупнее) */
  e[4].name = "xl/styles.xml";
  xl_puts(&e[4].data,
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
          "<fonts count=\"3\"><font><sz val=\"10\"/><name val=\"Arial\"/></font>"
          "<font><b/><sz val=\"10\"/><name val=\"Arial\"/></font>"
          "<font><b/><sz val=\"12\"/><name val=\"Arial\"/></font></fonts>"
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
          "applyAlignment=\"1\"><alignment vertical=\"top\" wrapText=\"1\"/></xf>"
          "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/></cellXfs>"
          "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
          "</styleSheet>");
  e[5].name = "xl/worksheets/sheet1.xml";
  XlBuf *s = &e[5].data;
  xl_puts(s, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
             "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"2\" topLeftCell=\"A3\" "
             "activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><cols>");
  char tmp[160];
  for (int c = 0; c < sh->ncols; c++) {
    snprintf(tmp, sizeof(tmp), "<col min=\"%d\" max=\"%d\" width=\"%d\" customWidth=\"1\"/>", c + 1, c + 1,
             sh->width[c]);
    xl_puts(s, tmp);
  }
  xl_puts(s, "</cols><sheetData>");
  /* строка 1 — заголовок, строка 2 — шапка, дальше данные */
  xl_puts(s, "<row r=\"1\"><c r=\"A1\" s=\"3\" t=\"inlineStr\"><is><t>");
  xl_text(s, title);
  xl_puts(s, "</t></is></c></row><row r=\"2\" ht=\"40\" customHeight=\"1\">");
  for (int c = 0; c < sh->ncols; c++) {
    char cn[4];
    xl_colname(c, cn);
    snprintf(tmp, sizeof(tmp), "<c r=\"%s2\" s=\"1\" t=\"inlineStr\"><is><t>", cn);
    xl_puts(s, tmp);
    xl_text(s, sh->head[c]);
    xl_puts(s, "</t></is></c>");
  }
  xl_puts(s, "</row>");
  for (int r = 0; r < nrows; r++) {
    snprintf(tmp, sizeof(tmp), "<row r=\"%d\">", r + 3);
    xl_puts(s, tmp);
    for (int c = 0; c < sh->ncols; c++) {
      const wchar_t *v = cell(ctx, r, c);
      char cn[4];
      xl_colname(c, cn);
      double d;
      if (xl_is_num(v, &d)) {
        snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"2\"><v>%.10g</v></c>", cn, r + 3, d);
        xl_puts(s, tmp);
      } else {
        snprintf(tmp, sizeof(tmp), "<c r=\"%s%d\" s=\"2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">", cn,
                 r + 3);
        xl_puts(s, tmp);
        xl_text(s, v ? v : L"");
        xl_puts(s, "</t></is></c>");
      }
    }
    xl_puts(s, "</row>");
  }
  xl_puts(s, "</sheetData><pageSetup orientation=\"landscape\"/></worksheet>");
  BOOL ok = TRUE;
  for (int i = 0; i < 6; i++)
    if (!e[i].data.p) ok = FALSE;
  if (ok) ok = xl_zip_write(path, e, 6);
  for (int i = 0; i < 6; i++) free(e[i].data.p);
  return ok;
}
