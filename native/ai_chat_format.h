/* Display-only Markdown subset. History sent to the model stays untouched.
   Every character is escaped before entering RTF, including paths and braces. */
typedef struct { char *text; size_t size, cap; BOOL failed; } AcRtf;
static void ac_rtf_add(AcRtf *b, const char *text) {
  if (b->failed) return;
  size_t n = strlen(text);
  if (b->size + n + 1 > 64u * 1024u * 1024u) { b->failed = TRUE; return; }
  if (b->size + n + 1 > b->cap) {
    size_t cap = b->cap ? b->cap * 2 : 4096;
    while (cap < b->size + n + 1) cap *= 2;
    char *p = (char *)realloc(b->text, cap);
    if (!p) { b->failed = TRUE; return; }
    b->text = p; b->cap = cap;
  }
  memcpy(b->text + b->size, text, n + 1); b->size += n;
}
static void ac_rtf_char(AcRtf *b, wchar_t c) {
  char value[32];
  if (c == L'\\' || c == L'{' || c == L'}') snprintf(value, sizeof(value), "\\%c", (char)c);
  else if (c == L'\t') strcpy(value, "\\tab ");
  else if (c >= 32 && c < 127) { value[0] = (char)c; value[1] = 0; }
  else if (c >= 32) snprintf(value, sizeof(value), "\\u%d?", (int)(short)c);
  else return;
  ac_rtf_add(b, value);
}
static void ac_rtf_begin(AcRtf *b) {
  ac_rtf_add(b, "{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}}"
                "{\\colortbl;\\red30\\green41\\blue59;\\red37\\green99\\blue150;\\red100\\green116\\blue139;}"
                "\\viewkind4\\f0\\fs22\\cf1 ");
}
/* A numbered heading accidentally glued to prose is split only when followed
   by **bold**. Decimal dimensions, versions and C:\\ paths are never split. */
static BOOL ac_inline_heading(const wchar_t *start, const wchar_t *p) {
  if (p == start || *p < L'1' || *p > L'9' || (p[-1] >= L'0' && p[-1] <= L'9')) return FALSE;
  const wchar_t *q = p;
  for (int i = 0; i < 3 && *q >= L'0' && *q <= L'9'; i++) q++;
  if (*q++ != L'.') return FALSE;
  while (*q == L' ') q++;
  return q[0] == L'*' && q[1] == L'*';
}
static void ac_rtf_message(AcRtf *b, const wchar_t *text, BOOL assistant, BOOL picture) {
  ac_rtf_add(b, assistant ? "\\pard\\sb180\\sa80\\cf2\\b\\fs22 " : "\\pard\\sb180\\sa80\\cf3\\b\\fs22 ");
  const wchar_t *label = assistant ? L"ИИ" : L"Вы";
  for (const wchar_t *p = label; *p; p++) ac_rtf_char(b, *p);
  ac_rtf_add(b, "\\b0\\cf1\\par\\pard\\li120\\ri120\\sa60\\sl260\\slmult1\\f0\\fs22 ");
  if (picture) {
    for (const wchar_t *p = L"[Прикреплена картинка]"; *p; p++) ac_rtf_char(b, *p);
    ac_rtf_add(b, "\\par ");
  }
  BOOL bold = FALSE, code = FALSE, fenced = FALSE, lineStart = TRUE, heading = FALSE;
  for (const wchar_t *p = text; *p; ) {
    if (*p == L'\r') { p++; continue; }
    if (assistant && lineStart && !code && p[0] == L'`' && p[1] == L'`' && p[2] == L'`') {
      fenced = !fenced;
      while (*p && *p != L'\n') p++;
      if (*p) p++;
      ac_rtf_add(b, fenced ? "\\f1\\fs20 " : "\\f0\\fs22 ");
      lineStart = TRUE; continue;
    }
    if (*p == L'\n') {
      if (fenced) { ac_rtf_add(b, "\\line "); p++; lineStart = TRUE; continue; }
      if (heading) { ac_rtf_add(b, "\\b0\\fs22 "); bold = FALSE; heading = FALSE; }
      int breaks = 0;
      while (*p == L'\n' || *p == L'\r') { if (*p == L'\n') breaks++; p++; }
      ac_rtf_add(b, breaks > 1 ? "\\par\\sb100 " : "\\par\\sb0 "); lineStart = TRUE; continue;
    }
    if (assistant && !lineStart && !fenced && !code && !bold && ac_inline_heading(text, p)) {
      ac_rtf_add(b, "\\par "); lineStart = TRUE;
    }
    if (assistant && lineStart && !fenced && *p == L'#') {
      const wchar_t *q = p; while (*q == L'#') q++;
      if (*q == L' ') { p = q + 1; heading = bold = TRUE; ac_rtf_add(b, "\\b\\fs24 "); }
    }
    if (assistant && lineStart && !fenced && (p[0] == L'-' || p[0] == L'*') && p[1] == L' ') {
      ac_rtf_char(b, 0x2022); ac_rtf_add(b, " "); p += 2;
    }
    lineStart = FALSE;
    if (assistant && !fenced && !code && p[0] == L'*' && p[1] == L'*' && (bold || wcsstr(p + 2, L"**"))) {
      bold = !bold; ac_rtf_add(b, bold ? "\\b " : "\\b0 "); p += 2; continue;
    }
    if (assistant && !fenced && *p == L'`' && (code || wcschr(p + 1, L'`'))) {
      code = !code; ac_rtf_add(b, code ? "\\f1\\fs20 " : "\\f0\\fs22 "); p++; continue;
    }
    ac_rtf_char(b, *p++);
  }
  ac_rtf_add(b, "\\b0\\f0\\fs22\\par ");
}
