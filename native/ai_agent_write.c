/* New UTF-8 files and minimal valid OOXML documents. No Office dependency,
   no macros, no external relationships, and CREATE_NEW prevents replacement. */
static DWORD ag_crc(const char *s, size_t n) {
  DWORD c = 0xffffffffUL;
  for (size_t i = 0; i < n; i++) {
    c ^= (BYTE)s[i];
    for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xedb88320UL : c >> 1;
  }
  return c ^ 0xffffffffUL;
}
static void ag_le16(BYTE *p, unsigned v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static void ag_le32(BYTE *p, DWORD v) { for (int i = 0; i < 4; i++) p[i] = (BYTE)(v >> (i * 8)); }
static BOOL ag_bytes(HANDLE h, const void *p, DWORD n) {
  DWORD written = 0; return !n || (WriteFile(h, p, n, &written, NULL) && written == n);
}
static BOOL ag_write_document(const wchar_t *path, const wchar_t *text, AgKind kind, const char *sheetXml) {
  BOOL docx = kind == AG_DOCX, xlsx = kind == AG_XLSX;
  if (xlsx && text[0]) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
  char utf8[8001], xml[18000];
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, utf8, sizeof(utf8), NULL, NULL);
  if (!n) return FALSE;
  if (docx) {
    strcpy(xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body><w:p><w:r><w:t xml:space=\"preserve\">");
    size_t at = strlen(xml);
    for (int i = 0; i < n - 1; i++) {
      const char *escaped = NULL;
      if (utf8[i] == '&') escaped = "&amp;";
      else if (utf8[i] == '<') escaped = "&lt;";
      else if (utf8[i] == '>') escaped = "&gt;";
      else if (utf8[i] == '\n') escaped = "</w:t></w:r></w:p><w:p><w:r><w:t xml:space=\"preserve\">";
      else if (utf8[i] == '\t') escaped = "</w:t><w:tab/><w:t xml:space=\"preserve\">";
      else if ((BYTE)utf8[i] < 32) continue;
      size_t len = escaped ? strlen(escaped) : 1;
      if (at + len + 400 >= sizeof(xml)) return FALSE;
      memcpy(xml + at, escaped ? escaped : utf8 + i, len); at += len;
    }
    strcpy(xml + at, "</w:t></w:r></w:p><w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\"/></w:sectPr></w:body></w:document>");
  }
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return FALSE;
  BOOL ok = TRUE;
  if (!docx && !xlsx) ok = ag_bytes(h, utf8, (DWORD)n - 1);
  else {
    const char *names[5] = {"[Content_Types].xml", "_rels/.rels", "word/document.xml"};
    const char *data[5] = {
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>",
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>", xml};
    int count = docx ? 3 : 5;
    if (xlsx) {
      names[2] = "xl/workbook.xml"; names[3] = "xl/_rels/workbook.xml.rels"; names[4] = "xl/worksheets/sheet1.xml";
      data[0] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/><Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/></Types>";
      data[1] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>";
      data[2] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"Лист1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
      data[3] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>";
      data[4] = sheetXml ? sheetXml : "<?xml version=\"1.0\" encoding=\"UTF-8\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData/></worksheet>";
    }
    DWORD offsets[5], crcs[5], pos = 0;
    for (int i = 0; i < count && ok; i++) {
      DWORD size = (DWORD)strlen(data[i]), len = (DWORD)strlen(names[i]);
      offsets[i] = pos; crcs[i] = ag_crc(data[i], size);
      BYTE head[30] = {0}; ag_le32(head, 0x04034b50); ag_le16(head + 4, 20); ag_le16(head + 6, 0x800);
      ag_le16(head + 12, 0x21); ag_le32(head + 14, crcs[i]); ag_le32(head + 18, size); ag_le32(head + 22, size); ag_le16(head + 26, len);
      ok = ag_bytes(h, head, 30) && ag_bytes(h, names[i], len) && ag_bytes(h, data[i], size); pos += 30 + len + size;
    }
    DWORD cd = pos;
    for (int i = 0; i < count && ok; i++) {
      DWORD size = (DWORD)strlen(data[i]), len = (DWORD)strlen(names[i]);
      BYTE head[46] = {0}; ag_le32(head, 0x02014b50); ag_le16(head + 4, 20); ag_le16(head + 6, 20);
      ag_le16(head + 8, 0x800); ag_le16(head + 14, 0x21); ag_le32(head + 16, crcs[i]);
      ag_le32(head + 20, size); ag_le32(head + 24, size); ag_le16(head + 28, len); ag_le32(head + 42, offsets[i]);
      ok = ag_bytes(h, head, 46) && ag_bytes(h, names[i], len); pos += 46 + len;
    }
    BYTE end[22] = {0}; ag_le32(end, 0x06054b50); ag_le16(end + 8, count); ag_le16(end + 10, count); ag_le32(end + 12, pos - cd); ag_le32(end + 16, cd);
    ok = ok && ag_bytes(h, end, 22);
  }
  DWORD error = GetLastError(); CloseHandle(h);
  if (!ok) { DeleteFileW(path); SetLastError(error); }
  return ok;
}
static BOOL ag_write_new(const wchar_t *path, const wchar_t *text, AgKind kind) {
  return ag_write_document(path, text, kind, NULL);
}
