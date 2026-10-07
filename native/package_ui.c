/* ---- Окно «Пакет для загрузки в PLM» --------------------------------------------

   Сюда вбивается всё, что нужно команде PLM «Загрузить ЭСИ из CursorPad»
   (pidrpen/cursor): три листа — Изделия, Состав, Операции (столбцы — kPkSheets
   в export.c). Двойной щелчок или F2 — править ячейку (Enter — дальше вниз,
   Tab — вправо, Esc — отмена); «+ Строка», Delete — удалить выделенные;
   Ctrl+V или «Вставить из Excel» — строки из буфера (скопированные в Excel),
   столбцы по порядку; «Из выгрузки» — изделия, состав и операции из окна
   «Выгрузка из PLM» (похожее изделие как образец). «Проверить» — что входящие
   есть в изделиях, операции есть в справочнике 1С (названия сам приводит к
   виду 1С «4110 Токарная»), номера операций «005». «Сохранить пакет…» — xlsx,
   который и загружают в PLM. Черновик помнится между запусками (package.txt
   рядом с заметками). */

#define ID_PK_TAB 300 /* +лист: 300…302 */
#define ID_PK_ADD 304
#define ID_PK_DEL 305
#define ID_PK_PASTE 306
#define ID_PK_FROMXP 307
#define ID_PK_CHECK 308
#define ID_PK_SAVE 309
#define ID_PK_CLEAR 310

static HWND g_pkWnd, g_pkList, g_pkEdit;
static XpTable g_pk[PK_NT];
static int g_pkSheet;             /* какой лист в таблице */
static int g_pkEditRow = -1, g_pkEditCol = -1;
static wchar_t g_pkStatus[300];
static float g_pkS = 1.0f;
static BOOL g_pkLoaded;

static int PKS(int v) { return (int)(v * g_pkS + 0.5f); }

static void pk_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_pkStatus, 300, fmt, ap);
  va_end(ap);
  g_pkStatus[299] = 0;
  if (g_pkWnd) {
    RECT rc;
    GetClientRect(g_pkWnd, &rc);
    RECT st = {0, PANEL_TITLE_H + PKS(44), rc.right, PANEL_TITLE_H + PKS(70)};
    InvalidateRect(g_pkWnd, &st, FALSE);
  }
}

/* ячейка: пусто — NULL (xp_set пустое не пишет, а здесь его надо и стирать) */
static void pk_put(XpTable *t, int r, int c, const wchar_t *v) {
  wchar_t **cell = &t->cell[(size_t)r * (size_t)t->ncols + (size_t)c];
  free(*cell);
  *cell = NULL;
  if (!v) return;
  while (*v == L' ') v++;
  size_t l = wcslen(v);
  while (l && (v[l - 1] == L' ' || v[l - 1] == L'\r')) l--;
  if (!l) return;
  *cell = (wchar_t *)malloc((l + 1) * sizeof(wchar_t));
  if (*cell) {
    memcpy(*cell, v, l * sizeof(wchar_t));
    (*cell)[l] = 0;
  }
}

static void pk_del_row(XpTable *t, int r) {
  if (r < 0 || r >= t->n) return;
  for (int c = 0; c < t->ncols; c++) free(t->cell[(size_t)r * (size_t)t->ncols + (size_t)c]);
  memmove(t->cell + (size_t)r * (size_t)t->ncols, t->cell + (size_t)(r + 1) * (size_t)t->ncols,
          sizeof(wchar_t *) * (size_t)(t->n - r - 1) * (size_t)t->ncols);
  t->n--;
}

static void pk_init(void) {
  for (int k = 0; k < PK_NT; k++)
    if (!g_pk[k].ncols) g_pk[k].ncols = kPkSheets[k].ncols;
}

/* ---- черновик: package.txt, строки «лист\tячейки…» ---- */
static void pk_draft_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\package.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static void pk_draft_save(void) {
  if (!g_dataDir[0]) return;
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  sb_add(&b, L"pkg\t1\n");
  for (int k = 0; k < PK_NT; k++)
    for (int r = 0; r < g_pk[k].n; r++) {
      sb_add(&b, L"%d", k);
      for (int c = 0; c < g_pk[k].ncols; c++) sb_field(&b, xp_cell(&g_pk[k], r, c));
      sb_add(&b, L"\n");
    }
  wchar_t p[MAX_PATH];
  pk_draft_path(p);
  if (b.w) share_write_ex(p, b.w, TRUE);
  free(b.w);
}

static void pk_draft_load(void) {
  pk_init();
  if (!g_dataDir[0]) return;
  wchar_t p[MAX_PATH];
  pk_draft_path(p);
  wchar_t *t = share_read(p);
  if (!t) return;
  wchar_t *pp = t, *line;
  while ((line = share_next_line(&pp)) != NULL) {
    wchar_t *f[12];
    int n = share_split(line, f, 12);
    if (n < 2 || f[0][0] < L'0' || f[0][0] > L'2' || f[0][1]) continue;
    int k = f[0][0] - L'0';
    wchar_t **w = xp_row(&g_pk[k]);
    if (!w) break;
    for (int c = 0; c < g_pk[k].ncols && c + 1 < n; c++) pk_put(&g_pk[k], g_pk[k].n - 1, c, f[c + 1]);
  }
  free(t);
}

/* ---- таблица окна ---- */
static void pk_tabs(void) {
  for (int k = 0; k < PK_NT; k++) {
    wchar_t s[60];
    _snwprintf(s, 60, L"%s%s · %d", k == g_pkSheet ? L"● " : L"", kPkSheets[k].name, g_pk[k].n);
    SetWindowTextW(GetDlgItem(g_pkWnd, ID_PK_TAB + k), s);
  }
}

static void pk_set_item(int r) {
  XpTable *t = &g_pk[g_pkSheet];
  for (int c = 0; c < t->ncols; c++) {
    LVITEMW si;
    memset(&si, 0, sizeof(si));
    si.iSubItem = c;
    si.pszText = (wchar_t *)xp_cell(t, r, c);
    SendMessageW(g_pkList, LVM_SETITEMTEXTW, (WPARAM)r, (LPARAM)&si);
  }
}

static void pk_fill(BOOL columns) {
  XpTable *t = &g_pk[g_pkSheet];
  SendMessageW(g_pkList, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(g_pkList);
  if (columns) {
    while (SendMessageW(g_pkList, LVM_DELETECOLUMN, 0, 0)) {
    }
    for (int c = 0; c < t->ncols; c++) {
      LVCOLUMNW col;
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      col.pszText = (wchar_t *)kPkSheets[g_pkSheet].head[c];
      col.cx = PKS(kPkSheets[g_pkSheet].width[c] * 7 + 14);
      col.iSubItem = c;
      SendMessageW(g_pkList, LVM_INSERTCOLUMNW, (WPARAM)c, (LPARAM)&col);
    }
  }
  for (int r = 0; r < t->n; r++) {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = r;
    it.pszText = (wchar_t *)xp_cell(t, r, 0);
    SendMessageW(g_pkList, LVM_INSERTITEMW, 0, (LPARAM)&it);
    pk_set_item(r);
  }
  SendMessageW(g_pkList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_pkList, NULL, TRUE);
  pk_tabs();
}

static void pk_changed(void) {
  pk_tabs();
  pk_draft_save();
}

/* ---- правка ячейки поверх таблицы ---- */
static void pk_edit_end(BOOL commit);
static void pk_edit_begin(int r, int c);

static LRESULT CALLBACK PkEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (msg == WM_KEYDOWN) {
    if (wParam == VK_ESCAPE) {
      pk_edit_end(FALSE);
      return 0;
    }
    if (wParam == VK_RETURN || wParam == VK_TAB) {
      int r = g_pkEditRow, c = g_pkEditCol;
      BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      pk_edit_end(TRUE);
      XpTable *t = &g_pk[g_pkSheet];
      if (wParam == VK_TAB) c = shift ? c - 1 : c + 1;
      else r++;
      if (c >= t->ncols) c = 0, r++;
      if (c < 0) c = t->ncols - 1, r--;
      if (r >= 0 && r < t->n) pk_edit_begin(r, c);
      return 0;
    }
  }
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\t' || wParam == 27)) return 0;
  if (msg == WM_KILLFOCUS) {
    LRESULT res = CallWindowProcW(old, hwnd, msg, wParam, lParam);
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    return res;
  }
  return CallWindowProcW(old, hwnd, msg, wParam, lParam);
}

static void pk_edit_begin(int r, int c) {
  XpTable *t = &g_pk[g_pkSheet];
  if (r < 0 || r >= t->n || c < 0 || c >= t->ncols) return;
  RECT rc;
  ListView_EnsureVisible(g_pkList, r, FALSE);
  if (c == 0) {
    ListView_GetItemRect(g_pkList, r, &rc, LVIR_LABEL);
  } else {
    rc.top = c;
    rc.left = LVIR_BOUNDS;
    SendMessageW(g_pkList, LVM_GETSUBITEMRECT, (WPARAM)r, (LPARAM)&rc);
  }
  if (!g_pkEdit) {
    g_pkEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, g_pkList, NULL,
                               g_inst, NULL);
    if (g_fontBody) SendMessageW(g_pkEdit, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    SetWindowLongPtrW(g_pkEdit, GWLP_USERDATA,
                      (LONG_PTR)SetWindowLongPtrW(g_pkEdit, GWLP_WNDPROC, (LONG_PTR)PkEditProc));
  }
  g_pkEditRow = r;
  g_pkEditCol = c;
  SetWindowTextW(g_pkEdit, xp_cell(t, r, c));
  MoveWindow(g_pkEdit, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top + 2, TRUE);
  ShowWindow(g_pkEdit, SW_SHOW);
  SetFocus(g_pkEdit);
  SendMessageW(g_pkEdit, EM_SETSEL, 0, -1);
}

static void pk_edit_end(BOOL commit) {
  int r = g_pkEditRow, c = g_pkEditCol;
  g_pkEditRow = g_pkEditCol = -1; /* до ShowWindow: уход фокуса не должен сохранить второй раз */
  if (!g_pkEdit) return;
  if (commit && r >= 0 && r < g_pk[g_pkSheet].n) {
    wchar_t v[600];
    GetWindowTextW(g_pkEdit, v, 600);
    pk_put(&g_pk[g_pkSheet], r, c, v);
    pk_set_item(r);
    pk_changed();
  }
  BOOL had = GetFocus() == g_pkEdit; /* ушли щелчком на кнопку — фокус ей, а не таблице */
  ShowWindow(g_pkEdit, SW_HIDE);
  if (had) SetFocus(g_pkList);
}

/* ---- действия ---- */
static void pk_add_row(void) {
  XpTable *t = &g_pk[g_pkSheet];
  if (!xp_row(t)) return;
  /* в составе и операциях — сборка/обозначение как у строки выше: вбивать меньше */
  if (g_pkSheet != PK_ITEMS && t->n > 1) pk_put(t, t->n - 1, 0, xp_cell(t, t->n - 2, 0));
  pk_fill(FALSE);
  pk_changed();
  pk_edit_begin(t->n - 1, g_pkSheet != PK_ITEMS && t->n > 1 ? 1 : 0);
}

static void pk_delete_selected(void) {
  XpTable *t = &g_pk[g_pkSheet];
  int removed = 0;
  for (int r = t->n - 1; r >= 0; r--)
    if (ListView_GetItemState(g_pkList, r, LVIS_SELECTED) & LVIS_SELECTED) pk_del_row(t, r), removed++;
  if (!removed) {
    pk_status(L"Выделите строки, которые удалить");
    return;
  }
  pk_fill(FALSE);
  pk_changed();
  pk_status(L"Удалено строк: %d", removed);
}

/* строки из буфера: скопированные из Excel — табуляция между столбцами */
static void pk_paste(void) {
  if (!OpenClipboard(g_pkWnd)) {
    pk_status(L"Буфер занят другой программой");
    return;
  }
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  wchar_t *text = NULL;
  if (h) {
    const wchar_t *src = (const wchar_t *)GlobalLock(h);
    if (src) {
      text = _wcsdup(src);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  if (!text) {
    pk_status(L"В буфере нет текста — скопируйте строки в Excel");
    return;
  }
  XpTable *t = &g_pk[g_pkSheet];
  int added = 0;
  wchar_t *p = text;
  while (p && *p) {
    wchar_t *nl = wcschr(p, L'\n');
    if (nl) *nl = 0;
    wchar_t *f[16];
    int n = 0;
    f[n++] = p;
    for (wchar_t *q = p; *q && n < 16; q++)
      if (*q == L'\t') *q = 0, f[n++] = q + 1;
    BOOL empty = TRUE;
    for (int i = 0; i < n; i++) {
      size_t l = wcslen(f[i]);
      if (l && f[i][l - 1] == L'\r') f[i][l - 1] = 0;
      if (f[i][0] && f[i][0] != L' ') empty = FALSE;
    }
    /* строка шапки (как у листа) — мимо */
    BOOL head = !_wcsnicmp(f[0], kPkSheets[g_pkSheet].head[0], 5);
    if (!empty && !head && xp_row(t)) {
      for (int c = 0; c < t->ncols && c < n; c++) pk_put(t, t->n - 1, c, f[c]);
      added++;
    }
    p = nl ? nl + 1 : NULL;
  }
  free(text);
  pk_fill(FALSE);
  pk_changed();
  pk_status(added ? L"Вставлено строк: %d — проверьте столбцы" : L"Строк в буфере не нашлось", added);
}

static void pk_from_export(void) {
  if (!g_xpJob || !g_xpJob->t[XP_COMP].n) {
    pk_status(L"Сначала соберите похожее изделие в «Выгрузке из PLM» (лист «Состав (ЭСИ)», лучше и «Операции»)");
    return;
  }
  int was = g_pk[PK_ITEMS].n + g_pk[PK_LINKS].n + g_pk[PK_OPS].n;
  if (was) {
    int a = MessageBoxW(g_pkWnd, L"В пакете уже есть строки.\n\nДа — заменить их выгрузкой, Нет — добавить к ним.",
                        L"Пакет для загрузки", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (a == IDCANCEL) return;
    if (a == IDYES) xp_package_free(g_pk);
  }
  pk_init();
  xp_package(g_xpJob, g_pk); /* добавляет без повторов */
  pk_fill(FALSE);
  pk_changed();
  pk_status(L"Из выгрузки %s: изделий %d, состав %d, операций %d — поменяйте обозначения и названия", g_xpJob->rt->des,
            g_pk[PK_ITEMS].n, g_pk[PK_LINKS].n, g_pk[PK_OPS].n);
}

static BOOL pk_has_item(const wchar_t *des) {
  for (int r = 0; r < g_pk[PK_ITEMS].n; r++)
    if (!_wcsicmp(xp_cell(&g_pk[PK_ITEMS], r, 0), des)) return TRUE;
  return FALSE;
}

/* Проверка (и что можно — правит сама): FALSE — есть ошибки */
static BOOL pk_check(BOOL quiet) {
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  int errs = 0, fixes = 0;
  XpTable *it = &g_pk[PK_ITEMS], *ln = &g_pk[PK_LINKS], *op = &g_pk[PK_OPS];
  if (!it->n) sb_add(&b, L"• Лист «Изделия» пуст\n"), errs++;
  for (int r = 0; r < it->n; r++) {
    if (!xp_cell(it, r, 0)[0]) sb_add(&b, L"• Изделия, строка %d: нет обозначения\n", r + 1), errs++;
    for (int q = 0; q < r; q++)
      if (xp_cell(it, r, 0)[0] && !_wcsicmp(xp_cell(it, r, 0), xp_cell(it, q, 0))) {
        sb_add(&b, L"• Изделия: «%s» дважды (строки %d и %d)\n", xp_cell(it, r, 0), q + 1, r + 1);
        errs++;
        break;
      }
  }
  for (int r = 0; r < ln->n; r++) {
    const wchar_t *a = xp_cell(ln, r, 0), *c = xp_cell(ln, r, 1);
    if (!a[0] || !c[0]) sb_add(&b, L"• Состав, строка %d: нужны и сборка, и входящее\n", r + 1), errs++;
    else {
      if (!pk_has_item(a)) sb_add(&b, L"• Состав, строка %d: сборки «%s» нет в «Изделиях»\n", r + 1, a), errs++;
      if (!pk_has_item(c)) sb_add(&b, L"• Состав, строка %d: входящего «%s» нет в «Изделиях»\n", r + 1, c), errs++;
      if (!_wcsicmp(a, c)) sb_add(&b, L"• Состав, строка %d: изделие входит само в себя\n", r + 1), errs++;
    }
    double v;
    if (xp_cell(ln, r, 2)[0] && !rt_num(xp_cell(ln, r, 2), &v))
      sb_add(&b, L"• Состав, строка %d: количество «%s» — не число\n", r + 1, xp_cell(ln, r, 2)), errs++;
  }
  for (int r = 0; r < op->n; r++) {
    const wchar_t *des = xp_cell(op, r, 0);
    if (des[0] && !pk_has_item(des)) sb_add(&b, L"• Операции, строка %d: изделия «%s» нет в «Изделиях»\n", r + 1, des), errs++;
    /* номер — «005» */
    const wchar_t *num = xp_cell(op, r, 1);
    if (num[0]) {
      wchar_t *end = NULL;
      long n = wcstol(num, &end, 10);
      if (end && !*end && n > 0 && wcslen(num) < 3) {
        wchar_t nb[16];
        _snwprintf(nb, 16, L"%03ld", n);
        pk_put(op, r, 1, nb);
        fixes++;
      }
    }
    /* операция — как в 1С: по ней команда PLM ищет операцию справочника */
    const wchar_t *o = xp_cell(op, r, 2);
    if (!o[0]) {
      sb_add(&b, L"• Операции, строка %d: нет операции\n", r + 1);
      errs++;
      continue;
    }
    wchar_t t1c[PLM_COL1];
    BOOL found = op1c_text(NULL, NULL, NULL, o, t1c, PLM_COL1);
    if (found && wcscmp(t1c, o)) {
      pk_put(op, r, 2, t1c);
      fixes++;
    } else if (!found) {
      sb_add(&b, L"• Операции, строка %d: «%s» нет в справочнике 1С — PLM поищет по названию\n", r + 1, o);
    }
    double v;
    for (int c = 5; c <= 6; c++)
      if (xp_cell(op, r, c)[0] && !rt_num(xp_cell(op, r, c), &v))
        sb_add(&b, L"• Операции, строка %d: «%s» — не число\n", r + 1, xp_cell(op, r, c)), errs++;
  }
  if (fixes) {
    pk_fill(FALSE);
    pk_changed();
  }
  if (b.w && b.w[0] && !quiet) {
    wchar_t *msg = b.w;
    if (wcslen(msg) > 3000) msg[3000] = 0, wcscat(msg, L"\n…");
    MessageBoxW(g_pkWnd, msg, errs ? L"Пакет: есть ошибки" : L"Пакет: замечания", errs ? MB_ICONWARNING : MB_ICONINFORMATION);
  }
  free(b.w);
  if (errs) pk_status(L"Ошибок: %d%s", errs, fixes ? L" (что можно — поправлено)" : L"");
  else pk_status(L"Пакет в порядке%s: изделий %d, состав %d, операций %d", fixes ? L", названия операций — как в 1С" : L"",
                 it->n, ln->n, op->n);
  return errs == 0;
}

static void pk_save(void) {
  if (g_pkEditRow >= 0) pk_edit_end(TRUE);
  if (!pk_check(TRUE) &&
      MessageBoxW(g_pkWnd, L"В пакете есть ошибки (см. «Проверить»). Всё равно сохранить?", L"Пакет для загрузки",
                  MB_YESNO | MB_ICONWARNING) != IDYES)
    return;
  wchar_t file[MAX_PATH];
  const wchar_t *root = g_pk[PK_ITEMS].n ? xp_cell(&g_pk[PK_ITEMS], 0, 0) : L"";
  _snwprintf(file, MAX_PATH - 6, L"Пакет для PLM %s", root);
  file[MAX_PATH - 7] = 0;
  for (wchar_t *p = file; *p; p++)
    if (wcschr(L"\\/:*?\"<>|", *p)) *p = L'_';
  wcscat(file, L".xlsx");
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_pkWnd;
  of.lpstrFilter = L"Книга Excel (*.xlsx)\0*.xlsx\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"xlsx";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  XlSheet sh[PK_NT];
  XlBookSheet bs[PK_NT];
  for (int k = 0; k < PK_NT; k++) {
    memset(&sh[k], 0, sizeof(sh[k]));
    sh[k].ncols = kPkSheets[k].ncols;
    sh[k].head = kPkSheets[k].head;
    sh[k].width = kPkSheets[k].width;
    sh[k].plain = 1;
    bs[k].name = kPkSheets[k].name;
    bs[k].title = NULL; /* шапка — в первой строке: так её ищет команда PLM */
    bs[k].sh = &sh[k];
    bs[k].nrows = g_pk[k].n;
    bs[k].cell = xp_cell;
    bs[k].ctx = &g_pk[k];
  }
  if (!xl_save_book(file, bs, PK_NT)) {
    MessageBoxW(g_pkWnd, L"Не удалось записать файл — он не открыт сейчас в Excel?", L"Пакет для загрузки",
                MB_ICONWARNING);
    return;
  }
  pk_status(L"Сохранено: %s — в PLM: «Загрузить ЭСИ из CursorPad»", file);
}

static void pk_clear(void) {
  if (MessageBoxW(g_pkWnd, L"Очистить все три листа пакета?", L"Пакет для загрузки", MB_YESNO | MB_ICONQUESTION) !=
      IDYES)
    return;
  xp_package_free(g_pk);
  pk_init();
  pk_fill(FALSE);
  pk_changed();
  pk_status(L"Пакет пуст");
}

/* ---- окно ---- */
static void pk_layout(void) {
  RECT rc;
  GetClientRect(g_pkWnd, &rc);
  int pad = PKS(14), y = PANEL_TITLE_H + PKS(8), h = PKS(30);
  place_panel_close(g_pkWnd);
  for (int k = 0; k < PK_NT; k++) MoveWindow(GetDlgItem(g_pkWnd, ID_PK_TAB + k), pad + k * PKS(150), y, PKS(142), h, TRUE);
  MoveWindow(GetDlgItem(g_pkWnd, ID_PK_FROMXP), rc.right - pad - PKS(180), y, PKS(180), h, TRUE);
  int ly = PANEL_TITLE_H + PKS(74), by = rc.bottom - pad - PKS(30);
  MoveWindow(g_pkList, pad, ly, rc.right - pad * 2, by - PKS(10) - ly, TRUE);
  static const int ids[] = {ID_PK_ADD, ID_PK_DEL, ID_PK_PASTE, ID_PK_CHECK, ID_PK_SAVE, ID_PK_CLEAR};
  static const int w[] = {100, 100, 150, 100, 170, 100};
  int x = pad;
  for (int i = 0; i < 6; i++) {
    if (ids[i] == ID_PK_CLEAR) x = rc.right - pad - PKS(w[i]);
    MoveWindow(GetDlgItem(g_pkWnd, ids[i]), x, by, PKS(w[i]), PKS(30), TRUE);
    x += PKS(w[i] + 8);
  }
}

static void pk_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Пакет для загрузки в PLM");
  SetBkMode(hdc, TRANSPARENT);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  int pad = PKS(14);
  RECT st = {pad, PANEL_TITLE_H + PKS(44), rc.right - pad, PANEL_TITLE_H + PKS(70)};
  DrawTextW(hdc,
            g_pkStatus[0] ? g_pkStatus
                          : L"Двойной щелчок или F2 — править, Enter — вниз, Tab — вправо, Ctrl+V — строки из Excel, "
                            L"Delete — удалить. В PLM: «Загрузить ЭСИ из CursorPad».",
            -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static WNDPROC g_pkListOld;
static LRESULT CALLBACK PkListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN) {
    BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (wParam == VK_F2 || wParam == VK_RETURN) {
      int r = ListView_GetNextItem(hwnd, -1, LVNI_FOCUSED);
      if (r >= 0) pk_edit_begin(r, 0);
      return 0;
    }
    if (wParam == VK_DELETE) {
      pk_delete_selected();
      return 0;
    }
    if (wParam == VK_INSERT) {
      pk_add_row();
      return 0;
    }
    if (ctrl && wParam == 'V') {
      pk_paste();
      return 0;
    }
    if (ctrl && wParam == 'A') {
      ListView_SetItemState(hwnd, -1, LVIS_SELECTED, LVIS_SELECTED);
      return 0;
    }
  }
  return CallWindowProcW(g_pkListOld, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK PkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    pk_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = PKS(820);
    mm->ptMinTrackSize.y = PKS(360);
    return 0;
  }
  case WM_SIZE:
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    pk_layout();
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_NOTIFY: {
    NMHDR *nh = (NMHDR *)lParam;
    if (nh->hwndFrom == g_pkList && nh->code == NM_DBLCLK) {
      NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lParam;
      if (ia->iItem >= 0) pk_edit_begin(ia->iItem, ia->iSubItem);
      else pk_add_row(); /* по пустому месту — новая строка */
      return 0;
    }
    if (nh->hwndFrom == g_pkList && nh->code == LVN_BEGINSCROLL && g_pkEditRow >= 0) pk_edit_end(TRUE);
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_PANEL_CLOSE) {
      if (g_pkEditRow >= 0) pk_edit_end(TRUE);
      ShowWindow(hwnd, SW_HIDE);
    }
    if (id >= ID_PK_TAB && id < ID_PK_TAB + PK_NT) {
      if (g_pkEditRow >= 0) pk_edit_end(TRUE);
      g_pkSheet = id - ID_PK_TAB;
      pk_fill(TRUE);
    }
    if (id == ID_PK_ADD) pk_add_row();
    if (id == ID_PK_DEL) pk_delete_selected();
    if (id == ID_PK_PASTE) pk_paste();
    if (id == ID_PK_FROMXP) pk_from_export();
    if (id == ID_PK_CHECK) pk_check(FALSE);
    if (id == ID_PK_SAVE) pk_save();
    if (id == ID_PK_CLEAR) pk_clear();
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_CLOSE:
    if (g_pkEditRow >= 0) pk_edit_end(TRUE);
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* fill — сразу заполнить из собранной выгрузки (кнопка в окне выгрузки) */
static void package_show(BOOL fill) {
  if (!g_pkWnd) {
    HDC s = GetDC(NULL);
    g_pkS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    if (s) ReleaseDC(NULL, s);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PkProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadPackage";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = PKS(1100), h = PKS(640);
    if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
    if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
    g_pkWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadPackage", L"Пакет для загрузки в PLM — CursorPad",
                              WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                              wa.left + (wa.right - wa.left - w) / 2 + PKS(30), wa.top + (wa.bottom - wa.top - h) / 2 + PKS(30),
                              w, h, NULL, NULL, g_inst, NULL);
    if (!g_pkWnd) return;
    round_corners(g_pkWnd);
    mk_btn(g_pkWnd, L"×", ID_PANEL_CLOSE);
    for (int k = 0; k < PK_NT; k++) mk_btn(g_pkWnd, kPkSheets[k].name, ID_PK_TAB + k);
    mk_btn(g_pkWnd, L"Из выгрузки…", ID_PK_FROMXP);
    mk_btn(g_pkWnd, L"+ Строка", ID_PK_ADD);
    mk_btn(g_pkWnd, L"Удалить", ID_PK_DEL);
    mk_btn(g_pkWnd, L"Вставить из Excel", ID_PK_PASTE);
    mk_btn(g_pkWnd, L"Проверить", ID_PK_CHECK);
    mk_btn(g_pkWnd, L"Сохранить пакет…", ID_PK_SAVE);
    mk_btn(g_pkWnd, L"Очистить", ID_PK_CLEAR);
    g_pkList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT |
                                                       LVS_SHOWSELALWAYS | WS_CLIPCHILDREN,
                               0, 0, 10, 10, g_pkWnd, NULL, g_inst, NULL);
    SendMessageW(g_pkList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    if (g_fontBody) SendMessageW(g_pkList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    g_pkListOld = (WNDPROC)SetWindowLongPtrW(g_pkList, GWLP_WNDPROC, (LONG_PTR)PkListProc);
    if (!g_pkLoaded) {
      g_pkLoaded = TRUE;
      pk_draft_load();
    }
    pk_fill(TRUE);
    pk_layout();
  }
  ShowWindow(g_pkWnd, IsIconic(g_pkWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_pkWnd);
  SetFocus(g_pkList);
  if (fill) pk_from_export();
}
