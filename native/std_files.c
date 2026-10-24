/* std_files.c — справочник стандартов (std.c) с диска этого компьютера: папка
   из ai.txt «std_dir» (g_gsDir, ai_local.c) или %USERPROFILE%\CursorPad-std,
   все *.tsv в ней и в подпапках. Вне OneDrive и AppData — как CursorPad-train:
   правки человека не прячутся виртуализацией MSIX. Встроенные таблицы туда
   раскладывает tables_seed.c; его «имя.tsv.новая» и «.встроено.txt» этот
   загрузчик не читает (не *.tsv). Подключается после std.c и techno.c;
   Windows нужен только здесь. */

static void gs_dir(wchar_t *out) {
  if (g_gsDir[0]) {
    lstrcpynW(out, g_gsDir, MAX_PATH);
    return;
  }
  wchar_t home[MAX_PATH];
  DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
  if (!n || n >= MAX_PATH) {
    out[0] = 0;
    return;
  }
  _snwprintf(out, MAX_PATH, L"%s\\CursorPad-std", home);
  out[MAX_PATH - 1] = 0;
}

/* UTF-8 (с BOM или без), UTF-16 LE с BOM («Текст Юникод» Excel) или ANSI 1251
   («Текст с разделителями табуляции» Excel) */
static wchar_t *gs_read(const wchar_t *path) {
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return NULL;
  LARGE_INTEGER sz;
  char *buf = NULL;
  DWORD got = 0;
  if (GetFileSizeEx(h, &sz) && sz.QuadPart < 8 * 1024 * 1024 && (buf = (char *)malloc((size_t)sz.QuadPart + 2)))
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL)) got = 0;
  CloseHandle(h);
  if (!buf) return NULL;
  wchar_t *w = NULL;
  if (got >= 2 && (BYTE)buf[0] == 0xFF && (BYTE)buf[1] == 0xFE) {
    int n = (int)(got - 2) / 2;
    if ((w = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t)))) {
      memcpy(w, buf + 2, (size_t)n * sizeof(wchar_t));
      w[n] = 0;
    }
  } else {
    const char *s = buf;
    int len = (int)got;
    if (len >= 3 && (BYTE)s[0] == 0xEF && (BYTE)s[1] == 0xBB && (BYTE)s[2] == 0xBF) s += 3, len -= 3;
    UINT cp = CP_UTF8;
    int n = len ? MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s, len, NULL, 0) : 0;
    if (len && !n) cp = 1251, n = MultiByteToWideChar(cp, 0, s, len, NULL, 0);
    if ((w = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t)))) {
      if (n) MultiByteToWideChar(cp, 0, s, len, w, n);
      w[n] = 0;
    }
  }
  free(buf);
  return w;
}

/* все *.tsv папки и подпапок (кроме pdf и скрытых), не глубже 4 уровней */
static void gs_load_tree(GsDb *db, const wchar_t *dir, int depth) {
  wchar_t mask[MAX_PATH];
  _snwprintf(mask, MAX_PATH, L"%s\\*", dir);
  mask[MAX_PATH - 1] = 0;
  WIN32_FIND_DATAW fd;
  HANDLE f = FindFirstFileW(mask, &fd);
  if (f == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.cFileName[0] == L'.' || (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_REPARSE_POINT))) continue;
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
    path[MAX_PATH - 1] = 0;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (depth < 4 && lstrcmpiW(fd.cFileName, L"pdf")) gs_load_tree(db, path, depth + 1);
      continue;
    }
    size_t n = wcslen(fd.cFileName);
    if (n < 5 || lstrcmpiW(fd.cFileName + n - 4, L".tsv") || db->files >= 2000) continue;
    wchar_t *text = gs_read(path);
    if (text) gs_parse(db, text, path);
    else gs_warn(db, path, 0, L"не прочитан");
  } while (FindNextFileW(f, &fd));
  FindClose(f);
}

/* Справочник читается заново при каждом вопросе: десятки небольших файлов —
   миллисекунды, зато правки в Excel видны сразу и общего состояния между
   потоками (чат, инструменты агента, окно чертежа) нет. gs_free — после. */
static void gs_open(GsDb *db) {
  memset(db, 0, sizeof(*db));
  wchar_t dir[MAX_PATH];
  gs_dir(dir);
#ifdef TABLES_SEED_ONCE /* встроенные таблицы — в папку, раз за запуск (после ai_load: std_dir уже прочитан) */
  if (dir[0]) tables_seed_once(1, L"std", dir);
#endif
  if (dir[0]) gs_load_tree(db, dir, 0);
}

/* Ответ кода на вопрос: найденные строки с источником; *cite — подписи таблиц
   для подвала (free). NULL — справочника нет или ничего не нашлось; тогда в
   why — почему (для модели и человека). */
static wchar_t *gs_answer(const wchar_t *q, wchar_t **cite, wchar_t *why, int cap) {
  GsDb db;
  gs_open(&db);
  *cite = NULL;
  why[0] = 0;
  wchar_t dir[MAX_PATH];
  gs_dir(dir);
  if (!db.n) {
    _snwprintf(why, cap, L"Справочник стандартов пуст: в папке %s нет таблиц *.tsv%s", dir,
               db.files ? L" (файлы есть, но таблицы не разобраны)" : L"");
    why[cap - 1] = 0;
    gs_free(&db);
    return NULL;
  }
  GsHit *hit = (GsHit *)calloc(40, sizeof(GsHit));
  GsQuery *Q = (GsQuery *)calloc(1, sizeof(GsQuery));
  int n = hit && Q ? gs_find(&db, q, hit, 40, Q) : 0;
  wchar_t *out = NULL;
  if (n) {
    GsBuf b = {0}, c = {0};
    gs_text(Q, hit, n, &b);
    for (int i = 0; i < n; i++) {
      if (i && hit[i - 1].t == hit[i].t) continue;
      if (c.n) gs_cat(&c, L"; ");
      gs_cite(hit[i].t, &c);
    }
#ifdef TABLES_SEED_ONCE
    wchar_t note[760] = {0};
    tables_note_append(1, note, 760);
    if (note[0]) gs_cat(&b, note);
#endif
    out = b.s;
    *cite = c.s;
  } else {
    _snwprintf(why, cap, L"В справочнике стандартов (%d табл., папка %s) этого нет", db.n, dir);
    why[cap - 1] = 0;
  }
  free(hit);
  free(Q);
  gs_free(&db);
  return out;
}

/* значение к маршруту: «σв 600 Н/мм²», «P ≤ 0,030» — из шапки «имя, единица» */
static void gs_brief_cell(GsBuf *b, const wchar_t *head, const wchar_t *v) {
  const wchar_t *comma = wcsstr(head, L", ");
  wchar_t name[80];
  lstrcpynW(name, head, comma && comma - head < 79 ? (int)(comma - head) + 1 : 80);
  const wchar_t *unit = comma ? comma + 2 : L"";
  gs_cat(b, name);
  if (!wcscmp(unit, L"не более")) gs_cat(b, L" ≤ "), gs_cat(b, v);
  else if (!wcscmp(unit, L"не менее")) gs_cat(b, L" ≥ "), gs_cat(b, v);
  else {
    gs_cat(b, L" ");
    gs_cat(b, v);
    if (unit[0]) gs_cat(b, L" "), gs_cat(b, unit);
  }
}

/* строка таблицы к маршруту — одной строкой: ключ, масса 1 м, затем остальное, не больше 5 значений;
   полный ответ с источником, сверкой и примечанием — в чате (gs_text) */
static void gs_brief_row(const GsHit *h, GsBuf *b) {
  const GsTable *t = h->t;
  gs_cat(b, L"\n• ");
  gs_cat(b, t->std);
  wchar_t what[120] = {0};
  if (t->massCol >= 0) lstrcpynW(what, L"сортамент", 120);
  else { /* «4 — Механические свойства (М1) на…» → «механические свойства» */
    const wchar_t *p = wcsstr(t->table, L"— ");
    p = p ? p + 2 : t->table;
    int k = 0;
    while (p[k] && p[k] != L'(' && p[k] != L',' && p[k] != L';' && k < 119) k++;
    while (k && p[k - 1] == L' ') k--;
    lstrcpynW(what, p, k + 1);
    CharLowerBuffW(what, 1);
  }
  if (what[0]) gs_cat(b, L", "), gs_cat(b, what);
  gs_cat(b, h->close ? L" (ближайший): " : L": ");
  int order[64], n = 0;
  for (int i = 0; i < t->nKey && n < 64; i++) order[n++] = t->keyAll ? 0 : t->keyCol[i];
  if (t->massCol >= 0 && n < 64) order[n++] = t->massCol;
  for (int c = 0; c < t->ncol && n < 64; c++) {
    int seen = 0;
    for (int i = 0; i < n; i++) seen |= order[i] == c;
    if (!seen) order[n++] = c;
  }
  for (int i = 0, k = 0; i < n && k < 5; i++) {
    const wchar_t *v = gs_cell(t, h->row, order[i]);
    if (!v[0] || !wcscmp(v, L"-") || wcslen(v) > 40) continue;
    if (k++) gs_cat(b, L"; ");
    gs_brief_cell(b, gs_cell(t, 0, order[i]), v);
  }
  if (!gs_is_checked(t)) gs_cat(b, L" (не сверено)");
}

/* К маршруту по чертежу (techno.c): заготовка и марка — по справочнику, коротко:
   не больше двух таблиц (сортамент, механика; химсостав — в чате), строкой на
   таблицу. «Круг 51» — нет в сортаменте, ближайший. Пустой справочник или ничего
   не нашлось — ничего не дописывается. */
static void gs_route_note(const TnRoute *r, wchar_t *out, int cap) {
  const TnSigns *s = &r->s;
  wchar_t q[600] = {0};
  if (r->blankRolled && r->blank[0]) tn_cpy(q, 600, r->blank);
  if (s->grade[0]) {
    tn_cat(q, 600, q[0] ? L"\n" : L"");
    if (!s->matStd[0]) tn_cat(q, 600, L"марка ");
    tn_cat(q, 600, s->grade);
    if (s->matStd[0]) tn_cat(q, 600, L" "), tn_cat(q, 600, s->matStd);
  }
  int used = (int)wcslen(out);
  if (!q[0] || cap - used < 200) return;
  GsDb db;
  gs_open(&db);
  GsHit *hit = (GsHit *)calloc(16, sizeof(GsHit));
  GsQuery *Q = (GsQuery *)calloc(1, sizeof(GsQuery));
  int n = hit && Q && db.n ? gs_find(&db, q, hit, 16, Q) : 0, m = 0;
  /* по строке таблицы, не больше двух таблиц: сводка без размера («Круг ГОСТ 2590 или
     поковка») и химсостав маршруту не нужны — их спросят в чате */
  for (int i = 0; i < n && m < 2; i++) {
    if (!hit[i].row || (m && hit[m - 1].t == hit[i].t)) continue;
    wchar_t *f = tn_fold(hit[i].t->table, 0);
    BOOL chem = f && wcsstr(f, L"химическ");
    free(f);
    if (!chem) hit[m++] = hit[i];
  }
  if (m) {
    GsBuf b = {0};
    gs_cat(&b, L"\n\nСправочник стандартов (подробно — спросите в чате, например «сталь 45 ГОСТ 1050»):");
    for (int i = 0; i < m; i++) gs_brief_row(&hit[i], &b);
    if (b.s) lstrcpynW(out + used, b.s, cap - used);
    free(b.s);
  }
  free(hit);
  free(Q);
  gs_free(&db);
}
