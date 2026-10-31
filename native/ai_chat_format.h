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
  ac_rtf_add(b, "{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}{\\f2 Consolas;}}"
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
/* Ответ «маршрут по чертежу» (techno.c, norms.c, std_files.c) узнаётся по строкам и
   рисуется с оформлением: операция — номер и название жирно, «почему» (после « — ») серым;
   нормы — колонками «операция | Тпз | Тшт»; разделы — жирно, служебное — мелко и серо.
   Прочий текст чата — как был. История для модели не меняется. */
enum { ACR_NONE, ACR_OP, ACR_NORM, ACR_HEAD, ACR_META, ACR_LABEL };
#define AC_RTF_BODY "\\pard\\li120\\ri120\\sa60\\sl260\\slmult1 "
static const wchar_t *ac_route_pre(const wchar_t *p, const wchar_t *e, const wchar_t *pre) {
  size_t n = wcslen(pre);
  return (size_t)(e - p) >= n && !wcsncmp(p, pre, n) ? p + n : NULL;
}
static int ac_route_kind(const wchar_t *p, const wchar_t *e, const wchar_t **mark) {
  *mark = NULL;
  if (e - p > 6 && p[0] >= L'0' && p[0] <= L'9' && p[1] >= L'0' && p[1] <= L'9' && p[2] >= L'0' && p[2] <= L'9' &&
      p[3] == L' ' && p[4] >= 0x410 && p[4] <= 0x42F) {
    for (const wchar_t *q = p + 4; q + 1 < e && q - p < 64; q++) {
      if (q[0] == L'.' && q[1] == L' ') return *mark = q + 1, ACR_OP;
      if (q[0] == L':' && q[1] == L' ' && (ac_route_pre(q + 2, e, L"Тпз ") || ac_route_pre(q + 2, e, L"То ")))
        return *mark = q, ACR_NORM;
    }
    return ACR_NONE;
  }
  static const wchar_t *const head[] = {L"Маршрут обработки", L"Нормы времени", L"Справочник стандартов",
                                        L"⚠ ИИ ВЫКЛЮЧЕН"};
  static const wchar_t *const meta[] = {L"Лист прочитан", L"Черновик по Tesseract", L"Полная Тшт",
                                        L"Разбор каждой цифры"};
  static const wchar_t *const label[] = {L"Деталь:", L"Материал:", L"Заготовка:", L"Прочитано на листе:",
                                         L"Уточнить:", L"Пока не нормируются:"};
  for (size_t i = 0; i < sizeof(head) / sizeof(head[0]); i++)
    if (ac_route_pre(p, e, head[i])) return ACR_HEAD;
  for (size_t i = 0; i < sizeof(meta) / sizeof(meta[0]); i++)
    if (ac_route_pre(p, e, meta[i])) return ACR_META;
  for (size_t i = 0; i < sizeof(label) / sizeof(label[0]); i++)
    if ((*mark = ac_route_pre(p, e, label[i])) != NULL) return ACR_LABEL;
  return ACR_NONE;
}
static void ac_rtf_span(AcRtf *b, const wchar_t *p, const wchar_t *e) {
  while (p < e) ac_rtf_char(b, *p++);
}
/* строка [p, e) вида kind; «·» между Тпз и Тшт — в табуляцию */
static void ac_route_line(AcRtf *b, int kind, const wchar_t *p, const wchar_t *e, const wchar_t *mark) {
  if (kind == ACR_OP) {
    const wchar_t *why = mark;
    while (why + 3 <= e && wcsncmp(why, L" — ", 3)) why++;
    if (why + 3 > e) why = e;
    ac_rtf_add(b, "\\b ");
    ac_rtf_span(b, p, mark);
    ac_rtf_add(b, "\\b0 ");
    ac_rtf_span(b, mark, why);
    if (why < e) ac_rtf_add(b, "\\cf3 "), ac_rtf_span(b, why, e), ac_rtf_add(b, "\\cf1 ");
  } else if (kind == ACR_NORM) {
    ac_rtf_add(b, "\\pard\\li120\\ri120\\sa20\\sl260\\slmult1\\tx4300\\tx6300 \\b ");
    ac_rtf_span(b, p, mark);
    ac_rtf_add(b, "\\b0\\tab ");
    for (const wchar_t *q = mark + 2; q < e;)
      if (q + 3 <= e && !wcsncmp(q, L" · ", 3)) ac_rtf_add(b, "\\tab "), q += 3;
      else ac_rtf_char(b, *q++);
  } else if (kind == ACR_HEAD) {
    ac_rtf_add(b, "\\b ");
    ac_rtf_span(b, p, e);
    ac_rtf_add(b, "\\b0 ");
  } else if (kind == ACR_META) {
    ac_rtf_add(b, "\\cf3\\fs20 ");
    ac_rtf_span(b, p, e);
    ac_rtf_add(b, "\\cf1\\fs22 ");
  } else {
    ac_rtf_add(b, "\\b ");
    ac_rtf_span(b, p, mark);
    ac_rtf_add(b, "\\b0 ");
    ac_rtf_span(b, mark, e);
  }
}
/* ---- нормы таблицей (с 2026.10.30, вид «Чертёж») -------------------------------------
   Строки норм norms.c («010 Токарная черновая: Тпз 18 мин · Тшт 12,4 мин (неполная)») идут
   подряд — они рисуются таблицей № | операция | Тпз, мин | Тшт, мин | пометка. Числа берутся
   ровно как их написал расчёт (с «≈», «—»), ничего не пересчитывается и не досочиняется;
   участка в расчёте норм нет — и столбца нет. Таблица — строки верхнего уровня: пузырь ответа
   (ac_rtf_bubble) перед ней закрывается и после неё открывается снова, по меткам
   \cpnormbeg / \cpnormend, которые до RichEdit не доходят. Ширина — g_acNormTw (твипы). */
static int g_acNormTw = 9000;

/* «Тпз 18 мин», «Тшт ≈ 12,4 мин» в строке [p, e): значение между «name » и « мин» (или « · », конец) */
static BOOL ac_norm_val(const wchar_t *p, const wchar_t *e, const wchar_t *name, const wchar_t **vb,
                        const wchar_t **ve, const wchar_t **after) {
  size_t n = wcslen(name);
  for (const wchar_t *q = p; q + n <= e; q++) {
    if (wcsncmp(q, name, n)) continue;
    if (q > p && q[-1] != L' ') continue;
    const wchar_t *v = q + n, *w = v;
    while (w < e && !(w + 4 <= e && !wcsncmp(w, L" мин", 4)) && !(w + 3 <= e && !wcsncmp(w, L" · ", 3)) &&
           *w != L'(')
      w++;
    while (w > v && w[-1] == L' ') w--;
    *vb = v, *ve = w;
    *after = w + 4 <= e && !wcsncmp(w, L" мин", 4) ? w + 4 : w;
    return TRUE;
  }
  return FALSE;
}

static void ac_norm_borders(AcRtf *b, BOOL head) {
  ac_rtf_add(b, head ? "\\clbrdrt\\brdrs\\brdrw20\\brdrcf6\\clbrdrb\\brdrs\\brdrw20\\brdrcf6"
                     : "\\clbrdrb\\brdrs\\brdrw10\\brdrcf6");
  ac_rtf_add(b, "\\clbrdrl\\brdrs\\brdrw10\\brdrcf6\\clbrdrr\\brdrs\\brdrw10\\brdrcf6");
  if (head) ac_rtf_add(b, "\\clcbpat8");
}

static void ac_norm_row_def(AcRtf *b, BOOL head) {
  static const int pct[5] = {9, 42, 57, 72, 100}; /* правые края столбцов, % ширины */
  char t[96];
  const int left = 320; /* вровень с текстом ответа: отступ пузыря и абзаца */
  snprintf(t, sizeof(t), "\\trowd\\trgaph80\\trleft%d\\trpaddl90\\trpaddr90\\trpaddfl3\\trpaddfr3%s ", left,
           head ? "\\trhdr" : "");
  ac_rtf_add(b, t);
  for (int i = 0; i < 5; i++) {
    ac_norm_borders(b, head);
    snprintf(t, sizeof(t), "\\cellx%d ", left + (g_acNormTw - left - 200) * pct[i] / 100);
    ac_rtf_add(b, t);
  }
}

static void ac_norm_cell(AcRtf *b, const wchar_t *p, const wchar_t *e, BOOL mono, BOOL right) {
  ac_rtf_add(b, right ? "\\pard\\intbl\\qr\\sa0\\sb0 " : "\\pard\\intbl\\ql\\sa0\\sb0 ");
  ac_rtf_add(b, mono ? "\\f2\\fs20 " : "\\f0\\fs21 "); /* \f2 — тот же Consolas, но без подсветки кода (\f1) */
  while (p < e && *p == L' ') p++;
  while (e > p && (e[-1] == L' ' || e[-1] == L'·')) e--;
  ac_rtf_span(b, p, e);
  ac_rtf_add(b, "\\f0\\fs22\\cell ");
}

static void ac_norm_head(AcRtf *b) {
  static const wchar_t *const h[5] = {L"№", L"ОПЕРАЦИЯ", L"ТПЗ, МИН", L"ТШТ, МИН", L"ПОМЕТКА"};
  ac_norm_row_def(b, TRUE);
  for (int i = 0; i < 5; i++) {
    ac_rtf_add(b, i == 2 || i == 3 ? "\\pard\\intbl\\qr\\sa0\\sb0\\cf3\\fs16 " : "\\pard\\intbl\\ql\\sa0\\sb0\\cf3\\fs16 ");
    ac_rtf_span(b, h[i], h[i] + wcslen(h[i]));
    ac_rtf_add(b, "\\cf1\\fs22\\cell ");
  }
  ac_rtf_add(b, "\\row ");
}

/* одна строка норм [p, e), mark — «: » после названия */
static void ac_norm_row(AcRtf *b, const wchar_t *p, const wchar_t *e, const wchar_t *mark) {
  const wchar_t *r = mark + 2, *pzb = NULL, *pze = NULL, *shb = NULL, *she = NULL, *a1 = r, *a2 = r;
  BOOL pz = ac_norm_val(r, e, L"Тпз ", &pzb, &pze, &a1);
  BOOL sh = ac_norm_val(r, e, L"Тшт ", &shb, &she, &a2);
  ac_norm_row_def(b, FALSE);
  ac_norm_cell(b, p, p + 3, TRUE, FALSE);   /* 010 */
  ac_norm_cell(b, p + 4, mark, FALSE, FALSE); /* название */
  if (pz) ac_norm_cell(b, pzb, pze, TRUE, TRUE);
  else ac_norm_cell(b, r, r, TRUE, TRUE);
  if (sh) ac_norm_cell(b, shb, she, TRUE, TRUE);
  else ac_norm_cell(b, r, r, TRUE, TRUE);
  /* пометка: что осталось после чисел — «(неполная)», «Тшк 3 мин», «по факту», «То 2,5 мин» */
  const wchar_t *rest = sh ? a2 : pz ? a1 : r;
  if (!pz && !sh) rest = r;
  ac_norm_cell(b, rest, e, FALSE, FALSE);
  ac_rtf_add(b, "\\row ");
}

static void ac_rtf_message(AcRtf *b, const wchar_t *text, BOOL assistant, BOOL picture) {
  ac_rtf_add(b, assistant ? "\\pard\\sb180\\sa80\\cf2\\b\\fs22 " : "\\pard\\sb180\\sa80\\cf3\\b\\fs22 ");
  const wchar_t *label = assistant ? L"ИИ" : L"Вы";
  for (const wchar_t *p = label; *p; p++) ac_rtf_char(b, *p);
  ac_rtf_add(b, "\\b0\\cf1\\par" AC_RTF_BODY "\\f0\\fs22 ");
  if (picture) {
    for (const wchar_t *p = L"[Прикреплена картинка]"; *p; p++) ac_rtf_char(b, *p);
    ac_rtf_add(b, "\\par ");
  }
  BOOL bold = FALSE, code = FALSE, fenced = FALSE, lineStart = TRUE, heading = FALSE, tabs = FALSE;
  for (const wchar_t *p = text; *p; ) {
    if (*p == L'\r') { p++; continue; }
    if (assistant && lineStart && !fenced && !code && !bold && *p != L'\n') { /* строки маршрута и норм */
      const wchar_t *e = p, *mark;
      while (*e && *e != L'\n' && *e != L'\r') e++;
      int kind = ac_route_kind(p, e, &mark);
      if (tabs && kind != ACR_NORM) ac_rtf_add(b, AC_RTF_BODY), tabs = FALSE; /* после колонок норм — обычный абзац */
      if (kind == ACR_NORM && !wcsncmp(mark, L": Тпз ", 6)) { /* нормы подряд — таблицей */
        ac_rtf_add(b, "\\cpnormbeg ");
        ac_norm_head(b);
        for (;;) {
          ac_norm_row(b, p, e, mark);
          p = e;
          while (*p == L'\r' || *p == L'\n') p++;
          const wchar_t *e2 = p;
          while (*e2 && *e2 != L'\n' && *e2 != L'\r') e2++;
          if (!*p || ac_route_kind(p, e2, &mark) != ACR_NORM) break;
          e = e2;
        }
        ac_rtf_add(b, "\\cpnormend " AC_RTF_BODY "\\f0\\fs22 ");
        lineStart = TRUE;
        continue;
      }
      if (kind) {
        ac_route_line(b, kind, p, e, mark);
        tabs = tabs || kind == ACR_NORM;
        p = e;
        lineStart = FALSE;
        continue;
      }
    }
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
