/* Bounded JSON reader for search responses. No evaluation or script engine. */
typedef struct { const char *p, *end; } AcJson;
static void acj_ws(AcJson *j) {
  while (j->p < j->end && (*j->p == ' ' || *j->p == '\r' || *j->p == '\n' || *j->p == '\t')) j->p++;
}
static BOOL acj_string(AcJson *j, char *out, size_t cap) {
  acj_ws(j); if (j->p >= j->end || *j->p++ != '"') return FALSE;
  size_t n = 0; BOOL fit = TRUE;
  while (j->p < j->end) {
    unsigned char c = (unsigned char)*j->p++;
    if (c == '"') { if (out && cap) out[n] = 0; return fit; }
    if (c < 32) return FALSE;
    char bytes[8]; int count = 1; bytes[0] = (char)c;
    if (c == '\\') {
      if (j->p >= j->end) return FALSE;
      c = (unsigned char)*j->p++;
      if (c == 'u') {
        unsigned v = 0;
        for (int k = 0; k < 4; k++) { if (j->p >= j->end) return FALSE; int d = acw_hex(*j->p++); if (d < 0) return FALSE; v = (v << 4) | d; }
        wchar_t w[2]; int wn = 1; w[0] = (wchar_t)v;
        if (v >= 0xd800 && v <= 0xdbff) {
          if (j->end - j->p < 6 || j->p[0] != '\\' || j->p[1] != 'u') return FALSE;
          j->p += 2; v = 0;
          for (int k = 0; k < 4; k++) { int d = acw_hex(*j->p++); if (d < 0) return FALSE; v = (v << 4) | d; }
          if (v < 0xdc00 || v > 0xdfff) return FALSE;
          w[1] = (wchar_t)v; wn = 2;
        } else if ((v >= 0xdc00 && v <= 0xdfff) || v == 0) return FALSE;
        count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, wn, bytes, 8, NULL, NULL);
        if (!count) return FALSE;
      } else {
        const char *esc = "\"\\/bfnrt", *decoded = "\"\\/\b\f\n\r\t", *p = strchr(esc, c);
        if (!p) return FALSE;
        bytes[0] = decoded[p - esc];
      }
    }
    if (out) {
      if (n + (size_t)count < cap) { memcpy(out + n, bytes, (size_t)count); n += count; }
      else fit = FALSE;
    }
  }
  return FALSE;
}
static BOOL acj_skip(AcJson *j, int depth) {
  acj_ws(j); if (depth > 24 || j->p >= j->end) return FALSE;
  if (*j->p == '"') return acj_string(j, NULL, 0);
  if (*j->p == '{' || *j->p == '[') {
    char open = *j->p++, close = open == '{' ? '}' : ']'; acj_ws(j);
    if (j->p < j->end && *j->p == close) { j->p++; return TRUE; }
    for (;;) {
      if (open == '{') { if (!acj_string(j, NULL, 0)) return FALSE; acj_ws(j); if (j->p >= j->end || *j->p++ != ':') return FALSE; }
      if (!acj_skip(j, depth + 1)) return FALSE;
      acj_ws(j); if (j->p >= j->end) return FALSE;
      if (*j->p == close) { j->p++; return TRUE; }
      if (*j->p++ != ',') return FALSE;
    }
  }
  const char *start = j->p;
  while (j->p < j->end && !strchr(" \t\n\r,]}", *j->p)) j->p++;
  size_t n = (size_t)(j->p - start);
  if ((n == 4 && (!memcmp(start, "true", 4) || !memcmp(start, "null", 4))) || (n == 5 && !memcmp(start, "false", 5))) return TRUE;
  if (!n || n >= 80 || !strchr("-0123456789", *start)) return FALSE;
  char num[80], *end; memcpy(num, start, n); num[n] = 0; strtod(num, &end);
  return end == num + n;
}
static BOOL acj_field(const char *start, const char *end, const char *name, const char **value, const char **stop) {
  AcJson j = {start, end}; acj_ws(&j); if (j.p >= end || *j.p++ != '{') return FALSE;
  for (;;) {
    acj_ws(&j); if (j.p >= end || *j.p == '}') return FALSE;
    char key[100]; if (!acj_string(&j, key, sizeof(key))) return FALSE;
    acj_ws(&j); if (j.p >= end || *j.p++ != ':') return FALSE;
    acj_ws(&j); const char *p = j.p; if (!acj_skip(&j, 0)) return FALSE;
    if (!strcmp(key, name)) { *value = p; *stop = j.p; return TRUE; }
    acj_ws(&j); if (j.p >= end || *j.p++ != ',') return FALSE;
  }
}
static BOOL acj_text(const char *start, const char *end, const char *name, char *out, size_t cap) {
  const char *p, *e; if (!acj_field(start, end, name, &p, &e)) return FALSE;
  AcJson j = {p, e}; return acj_string(&j, out, cap);
}
