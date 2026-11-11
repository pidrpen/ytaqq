/* pk_xlsx_ui.c — «Из Excel…» окна «ЭСИ и ТП»: книга (pk_xlsx.c) → листы окна (с 2026.11.06, John: «форму
   Excel закину в ЭСИ и ТП — и всё проставилось само»). Книгу выбирают кнопкой «Из Excel…» вверху окна или
   бросают мышью в окно (pk_stamp_drop отдаёт сюда .xlsx, .xlsm, .xls). Excel не нужен: .xlsx читает
   xlsx_read.c; только старый .xls (и книгу с паролем) открывает Excel — сохраняет во временный .xlsx.
   В окне уже есть строки — спросить: заменить их или добавить. Строка, которая уже есть (то же
   обозначение; в составе — та же пара; в операциях — деталь и номер), не дублируется: значения из книги
   её дополняют и правят (строки из «Союза» — только пустые ячейки). Потом — как после «Вставить»:
   признак покупки, станок, транспортирования, профессия, нормы (pk_changed).
   «Ещё ▾ → Шаблон Excel…» — пустой шаблон (RCDATA 315, make_esi_template.py): листы пакета со списками
   видов и операций 1С и «Как заполнять». Подключается после ai_file_ask.c (Excel — ai_office.c) и до
   pk_stamp.c (бросок книги мышью). */

#define PK_XLS_TPL_RES 315 /* esi_tp_shablon.xlsx в exe */

typedef struct {
  int added[PK_NT], merged;
} PkxCtx;

static void pkx_put_row(void *ctx, int sheet, const wchar_t *const *c, int n) {
  PkxCtx *x = (PkxCtx *)ctx;
  if (sheet < 0 || sheet >= PK_NT) return;
  XpTable *t = &g_pk[sheet];
  int key2 = sheet == PK_LINKS || sheet == PK_OPS ? 1 : -1;
  int r = -1;
  if (sheet == PK_STEPS) { /* переход — деталь, № операции и № перехода: тот же — новое заменяет */
    for (int q = 0; q < t->n && r < 0 && n > 2; q++)
      if (!_wcsicmp(xp_cell(t, q, 0), c[0]) && !wcscmp(xp_cell(t, q, 1), c[1]) && !wcscmp(xp_cell(t, q, 2), c[2])) r = q;
  } else if (c[0][0] && (key2 < 0 || c[key2][0]))
    r = pk_find_row(t, c[0], key2, key2 >= 0 ? c[key2] : L"");
  if (r >= 0) {
    BOOL soy = pk_row_from_soyuz(sheet, r);
    for (int k = 1; k < t->ncols && k < n; k++)
      if (c[k][0] && (!soy || !xp_cell(t, r, k)[0])) pk_put(t, r, k, c[k]);
    x->merged++;
    return;
  }
  if (!xp_row(t)) return;
  for (int k = 0; k < t->ncols && k < n; k++) pk_put(t, t->n - 1, k, c[k]);
  x->added[sheet]++;
}

/* книга ли (по расширению) — для броска мышью */
static BOOL pkx_is_book(const wchar_t *p) {
  const wchar_t *d = wcsrchr(p, L'.');
  return d && (!lstrcmpiW(d, L".xlsx") || !lstrcmpiW(d, L".xlsm") || !lstrcmpiW(d, L".xls"));
}

/* файл целиком; открыт в Excel — всё равно читается (Excel держит его на запись) */
static unsigned char *pkx_read_file(const wchar_t *path, size_t *len) {
  *len = 0;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return NULL;
  LARGE_INTEGER sz;
  unsigned char *d = NULL;
  if (GetFileSizeEx(f, &sz) && sz.QuadPart > 0 && sz.QuadPart < (100ll << 20) &&
      (d = (unsigned char *)malloc((size_t)sz.QuadPart + 1)) != NULL) {
    DWORD got = 0;
    if (ReadFile(f, d, (DWORD)sz.QuadPart, &got, NULL) && got == (DWORD)sz.QuadPart) {
      *len = got;
    } else {
      free(d);
      d = NULL;
    }
  }
  CloseHandle(f);
  return d;
}

/* старый .xls (или книга с паролем) — Excel сохраняет её во временный .xlsx */
static BOOL pkx_xls_to_xlsx(const wchar_t *src, wchar_t *tmp, wchar_t *why, int cap) {
  wchar_t dir[MAX_PATH];
  GetTempPathW(MAX_PATH, dir);
  _snwprintf(tmp, MAX_PATH, L"%lscursorpad-book-%lu-%lu.xlsx", dir, GetCurrentProcessId(), GetTickCount());
  tmp[MAX_PATH - 1] = 0;
  OfSession s;
  BOOL ok = FALSE;
  if (of_begin(&s, TRUE, src)) {
    VARIANT a[] = {of_str(tmp), of_int(51)}; /* xlOpenXMLWorkbook */
    s.hr = of_call(s.doc, L"SaveAs", DISPATCH_METHOD, NULL, 2, a);
    ok = SUCCEEDED(s.hr);
  }
  HRESULT hr = s.hr;
  of_end(&s);
  ok = ok && GetFileAttributesW(tmp) != INVALID_FILE_ATTRIBUTES;
  if (!ok) {
    _snwprintf(why, cap,
               L"старый .xls (или книгу с паролем) открывает только Excel, а он не открыл её (0x%08lx) — сохраните "
               L"книгу в Excel как .xlsx без пароля",
               (unsigned long)hr);
    why[cap - 1] = 0;
  }
  return ok;
}

/* «Операции» — по деталям (в порядке, как они встретились) и по номерам: транспортирования pk_changed дописывает
   в конец таблицы, а при «Все детали» список показывает строки, как они лежат — «010 Транспортирование» стояло
   бы под последней деталью, а не между 005 и 015 своей */
static int *g_pkxGrp;
static int pkx_ops_cmp(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  if (g_pkxGrp[x] != g_pkxGrp[y]) return g_pkxGrp[x] < g_pkxGrp[y] ? -1 : 1;
  int kx = pk_op_key(x), ky = pk_op_key(y);
  if (kx != ky) return kx < ky ? -1 : 1;
  return x < y ? -1 : x > y;
}

static void pkx_ops_order(void) {
  XpTable *t = &g_pk[PK_OPS];
  int n = t->n, nc = t->ncols;
  if (n < 2 || nc < 1) return;
  int *grp = (int *)malloc(sizeof(int) * (size_t)n), *idx = (int *)malloc(sizeof(int) * (size_t)n);
  wchar_t **cp = (wchar_t **)malloc(sizeof(wchar_t *) * (size_t)n * (size_t)nc);
  if (grp && idx && cp) {
    for (int r = 0; r < n; r++) {
      grp[r] = r, idx[r] = r;
      for (int q = 0; q < r; q++)
        if (grp[q] == q && !_wcsicmp(xp_cell(t, q, 0), xp_cell(t, r, 0))) {
          grp[r] = q;
          break;
        }
    }
    g_pkxGrp = grp;
    qsort(idx, (size_t)n, sizeof(int), pkx_ops_cmp);
    g_pkxGrp = NULL;
    for (int i = 0; i < n; i++)
      memcpy(cp + (size_t)i * (size_t)nc, t->cell + (size_t)idx[i] * (size_t)nc, sizeof(wchar_t *) * (size_t)nc);
    memcpy(t->cell, cp, sizeof(wchar_t *) * (size_t)n * (size_t)nc);
    for (int i = 0; g_pkQEdit >= 0 && i < n; i++) /* строка быстрого ТП в правке — за ней */
      if (idx[i] == g_pkQEdit) {
        g_pkQEdit = i;
        break;
      }
  }
  free(grp);
  free(idx);
  free(cp);
}

/* Книга → листы окна. ask — в окне уже есть строки: спросить «заменить или добавить» (вторая книга одного
   броска — добавляется молча). FALSE — ничего не легло */
static BOOL pk_xlsx_load(const wchar_t *path, BOOL ask) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  pk_init();
  const wchar_t *name = tm_base(path);
  wchar_t why[300] = L"", tmp[MAX_PATH] = L"";
  size_t len = 0;
  unsigned char *d = pkx_read_file(path, &len);
  if (!d) {
    pk_status(L"Книга «%s» не открылась — её нет, она пустая или больше 100 МБ", name);
    return FALSE;
  }
  if (len >= 4 && d[0] == 0xD0 && d[1] == 0xCF && d[2] == 0x11 && d[3] == 0xE0) { /* .xls или с паролем */
    free(d);
    d = NULL;
    pk_status(L"«%s» — старый формат .xls: открываю через Excel…", name);
    if (g_pkWnd) UpdateWindow(g_pkWnd);
    HCURSOR was = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    if (pkx_xls_to_xlsx(path, tmp, why, 300)) d = pkx_read_file(tmp, &len);
    SetCursor(was);
    if (tmp[0]) DeleteFileW(tmp);
    if (!d) {
      pk_status(L"«%s»: %s", name, why[0] ? why : L"не прочиталась");
      return FALSE;
    }
  }
  XrBook *b = (XrBook *)calloc(1, sizeof(XrBook));
  if (!b || !xr_read(d, len, b, why, 300)) {
    free(d);
    free(b);
    pk_status(L"«%s» не прочиталась: %s", name, why[0] ? why : L"мало памяти");
    return FALSE;
  }
  free(d);
  int have = g_pk[PK_ITEMS].n + g_pk[PK_LINKS].n + g_pk[PK_OPS].n + g_pk[PK_BLANKS].n + g_pk[PK_STEPS].n;
  if (have && ask && !g_pkNoBox) {
    wchar_t q[400];
    _snwprintf(q, 400,
               L"В листах окна уже есть строки (%d).\n\nДа — заменить их строками из книги «%s».\nНет — добавить к ним "
               L"(строки с теми же обозначениями дополнятся).\nОтмена — не загружать.",
               have, name);
    q[399] = 0;
    int a = MessageBoxW(g_pkWnd, q, L"Из Excel — ЭСИ и ТП", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (a == IDCANCEL) {
      xr_free(b);
      free(b);
      pk_status(L"Загрузка «%s» отменена", name);
      return FALSE;
    }
    if (a == IDYES) pk_reset();
  }
  wchar_t dir[MAX_PATH], asmb[200], part[200];
  lstrcpynW(dir, path, MAX_PATH);
  wchar_t *sl = wcsrchr(dir, L'\\');
  if (sl) *sl = 0;
  else dir[0] = 0;
  pk_target(asmb, part);
  PkxCtx x;
  memset(&x, 0, sizeof(x));
  PkxOut o;
  int n = pkx_apply(b, dir, asmb, part, pkx_put_row, &x, &o);
  xr_free(b);
  free(b);
  if (!n) {
    if (o.passed[0])
      pk_status(L"Из «%s» ничего не легло: в листах %s нет шапки, которую CursorPad узнаёт («Обозначение», «Наименование», "
                L"«Кол.», «Операция»…) — пришлите форму, или возьмите «Ещё ▾ → Шаблон Excel»", name, o.passed);
    else
      pk_status(L"В книге «%s» нет строк — заполните листы шаблона (Изделия, Состав, Операции, Заготовки)", name);
    return FALSE;
  }
  int show = PK_ITEMS; /* лист, куда легло больше всего (изделия — вперёд) */
  if (!o.rows[PK_ITEMS])
    for (int k = 0; k < PK_NT; k++)
      if (o.rows[k] > o.rows[show]) show = k;
  BOOL cols = show != g_pkSheet;
  g_pkSheet = show;
  pk_fill(cols);
  pk_changed();
  if (o.rows[PK_OPS]) {
    pkx_ops_order();
    if (g_pkSheet == PK_OPS) pk_fill(FALSE);
    pk_draft_save();
  }
  wchar_t more[300] = L"";
  if (x.merged) _snwprintf(more, 300, L"; уже были — дополнено %d", x.merged);
  size_t ml = wcslen(more);
  if (o.skipped && ml < 250) _snwprintf(more + ml, 300 - ml, L"; без обозначения или операции — мимо %d", o.skipped);
  ml = wcslen(more);
  if (o.passed[0] && ml < 250) _snwprintf(more + ml, 300 - ml, L"; не понял листы: %s", o.passed);
  more[299] = 0;
  pk_status(L"Из Excel «%s»: изделий %d, состав %d, операций %d, заготовок %d, переходов %d%s; листы: %s — дальше «Проверить»",
            name, x.added[PK_ITEMS], x.added[PK_LINKS], x.added[PK_OPS], x.added[PK_BLANKS], x.added[PK_STEPS], more, o.used);
  return TRUE;
}

/* «Вставить» на листе «Переходы» (с 2026.11.11): таблица из буфера (Excel, Word) — первая строка с шапкой листа
   («№ опер.», «Содержание перехода»…) — по ней; без шапки — по порядку столбцов листа (деталь, № опер., № перех.,
   текст, То, Тв), два-три столбца — № опер., текст[, То]. Пустые деталь и № операции — как строкой выше (первая —
   выбранная в окне деталь), пустой № перехода — следующий. Сколько строк легло; msg — что сказать */
static int pk_steps_paste(const wchar_t *text, const wchar_t *part, wchar_t *msg, int cap) {
  int lines = 0, maxc = 1;
  for (const wchar_t *p = text; *p;) {
    const wchar_t *e = wcschr(p, L'\n');
    size_t l = e ? (size_t)(e - p) : wcslen(p);
    int c = 1;
    for (size_t k = 0; k < l; k++) c += p[k] == L'\t';
    if (l && !(l == 1 && p[0] == L'\r')) lines++, maxc = c > maxc ? c : maxc;
    p += l + (e ? 1 : 0);
  }
  if (!lines || maxc < 2) {
    lstrcpynW(msg, L"Переходы: нужна таблица — хотя бы «№ опер.» и «Содержание перехода» (или все столбцы листа)", cap);
    return 0;
  }
  XrSheet s;
  memset(&s, 0, sizeof(s));
  s.ncols = maxc > PKX_COLS ? maxc : PKX_COLS;
  s.nrows = lines + 1;
  s.cell = (wchar_t **)calloc((size_t)s.nrows * (size_t)s.ncols, sizeof(wchar_t *));
  if (!s.cell) {
    lstrcpynW(msg, L"Мало памяти", cap);
    return 0;
  }
  for (int h = 0; h < kPkSheets[PK_STEPS].ncols; h++) s.cell[h] = _wcsdup(kPkSheets[PK_STEPS].head[h]); /* строка 0 — шапка */
  int r = 1;
  for (const wchar_t *p = text; *p && r < s.nrows;) {
    const wchar_t *e = wcschr(p, L'\n');
    size_t l = e ? (size_t)(e - p) : wcslen(p);
    if (l && p[l - 1] == L'\r') l--;
    if (l) {
      int c = 0;
      for (size_t a = 0; a <= l && c < s.ncols;) {
        size_t b = a;
        while (b < l && p[b] != L'\t') b++;
        size_t ca = a, cb = b;
        if (cb > ca + 1 && p[ca] == L'"' && p[cb - 1] == L'"') ca++, cb--; /* ячейка Excel с переводом строки — в кавычках */
        wchar_t *v = (wchar_t *)calloc(cb - ca + 1, sizeof(wchar_t));
        if (v) {
          for (size_t q = 0; q < cb - ca; q++) v[q] = p[ca + q] == L'\r' || p[ca + q] == L'\n' ? L' ' : p[ca + q];
          s.cell[(size_t)r * (size_t)s.ncols + (size_t)c] = v;
        }
        c++;
        a = b + 1;
      }
      r++;
    }
    p += (e ? (size_t)(e - p) + 1 : wcslen(p));
  }
  int col[PKX_COLS], hrow = 0;
  if (pkx_head(&s, 1, PK_STEPS, col) > 0) hrow = 1; /* своя шапка в первой строке */
  else {
    for (int h = 0; h < PKX_COLS; h++) col[h] = h < kPkSheets[PK_STEPS].ncols ? h : -1;
    if (maxc <= 3) /* № опер., текст[, То] */
      col[0] = -1, col[1] = 0, col[2] = -1, col[3] = 1, col[4] = maxc > 2 ? 2 : -1, col[5] = -1;
  }
  PkxCtx x;
  memset(&x, 0, sizeof(x));
  PkxOut o;
  memset(&o, 0, sizeof(o));
  int n = pkx_sheet_tpl(&s, PK_STEPS, hrow, col, NULL, part, pkx_put_row, &x, &o);
  for (size_t i = 0; i < (size_t)s.nrows * (size_t)s.ncols; i++) free(s.cell[i]);
  free(s.cell);
  if (!n)
    _snwprintf(msg, cap, L"Переходы не легли: в строках нет детали (выберите её в окне), номера операции или текста "
                         L"(мимо %d)", o.skipped);
  else
    _snwprintf(msg, cap, L"Вставлено переходов: %d%s%s", x.added[PK_STEPS],
               x.merged ? L", заменено прежних" : L"", o.skipped ? L"; строки без операции или текста — мимо" : L"");
  msg[cap - 1] = 0;
  return n;
}

/* «Из Excel…» */
static void pk_xlsx_pick(void) {
  wchar_t file[MAX_PATH] = L"";
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_pkWnd;
  of.lpstrFilter = L"Книга Excel (*.xlsx, *.xlsm, *.xls)\0*.xlsx;*.xlsm;*.xls\0Все файлы\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrTitle = L"Из Excel — шаблон «ЭСИ и ТП» или своя таблица с шапкой";
  of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&of)) pk_xlsx_load(file, TRUE);
}

/* «Ещё ▾ → Шаблон Excel…»: пустой шаблон — сохранить и открыть */
static void pk_xlsx_template(void) {
  wchar_t file[MAX_PATH] = L"ЭСИ и ТП — шаблон.xlsx";
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_pkWnd;
  of.lpstrFilter = L"Книга Excel (*.xlsx)\0*.xlsx\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"xlsx";
  of.lpstrTitle = L"Шаблон Excel для «ЭСИ и ТП» — куда сохранить";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  if (!extract_rcdata(PK_XLS_TPL_RES, file)) {
    MessageBoxW(g_pkWnd, L"Не удалось записать файл — он не открыт сейчас в Excel?", L"Шаблон Excel", MB_ICONWARNING);
    return;
  }
  pk_status(L"Шаблон сохранён: %s — заполните листы и загрузите кнопкой «Из Excel…» (или бросьте книгу в окно)", file);
  ShellExecuteW(g_pkWnd, L"open", file, NULL, NULL, SW_SHOWNORMAL);
}
