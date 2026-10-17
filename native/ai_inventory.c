/* Folder inventories are derived from real filesystem entries, never generated
   by the language model. Inline string cells cannot execute filename formulas. */
#define AG_INV_MAX 10000
typedef struct { wchar_t name[MAX_PATH]; WIN32_FILE_ATTRIBUTE_DATA info; BOOL planned; } AgInvEntry;
typedef struct { char *p; size_t n, cap; BOOL failed; } AgInvXml;
static BOOL ag_direct_child(const wchar_t *path, const wchar_t *folder) {
  size_t n = wcslen(folder);
  return ag_under(path, folder) && path[n] == L'\\' && path[n + 1] && !wcschr(path + n + 1, L'\\');
}
static int ag_inv_order(const void *a, const void *b) {
  return CompareStringOrdinal(((const AgInvEntry *)a)->name, -1, ((const AgInvEntry *)b)->name, -1, TRUE) - CSTR_EQUAL;
}
static BOOL ag_inv_entries(AgFs *fs, const wchar_t *folder, const wchar_t *report,
                           BOOL future, AgInvEntry **entries, int *count) {
  *entries = NULL; *count = 0;
  AgInvEntry *items = (AgInvEntry *)calloc(AG_INV_MAX, sizeof(*items));
  if (!items) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
  wchar_t mask[MAX_PATH]; BOOL ok = TRUE;
  if (wcslen(folder) + 3 >= MAX_PATH) { free(items); SetLastError(ERROR_FILENAME_EXCED_RANGE); return FALSE; }
  swprintf(mask, MAX_PATH, L"%ls\\*", folder);
  WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(mask, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && !(future && ag_planned_dir(fs, folder) && error == ERROR_PATH_NOT_FOUND)) ok = FALSE;
  } else {
    do {
      if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
      const wchar_t *reportName = wcsrchr(report, L'\\');
      if (reportName && ag_direct_child(report, folder) && !_wcsicmp(fd.cFileName, reportName + 1)) continue;
      if (*count >= AG_INV_MAX || (fs->cancel && InterlockedCompareExchange(fs->cancel, 0, 0))) { ok = FALSE; SetLastError(ERROR_MORE_DATA); break; }
      AgInvEntry *e = &items[(*count)++]; lstrcpynW(e->name, fd.cFileName, MAX_PATH);
      e->info.dwFileAttributes = fd.dwFileAttributes; e->info.nFileSizeHigh = fd.nFileSizeHigh; e->info.nFileSizeLow = fd.nFileSizeLow;
      e->info.ftLastWriteTime = fd.ftLastWriteTime;
    } while (FindNextFileW(h, &fd));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = FALSE;
    DWORD error = GetLastError(); FindClose(h); SetLastError(error);
  }
  for (int i = 0; future && ok && i < fs->count; i++) {
    const AgAction *a = &fs->plan[i];
    if ((a->kind == AG_MOVE || a->kind == AG_RENAME || a->kind == AG_MOVE_DIR) && ag_direct_child(a->src, folder)) {
      const wchar_t *name = wcsrchr(a->src, L'\\') + 1;
      for (int k = 0; k < *count; k++) if (!_wcsicmp(items[k].name, name)) { memmove(items + k, items + k + 1, (size_t)(--*count - k) * sizeof(*items)); break; }
    }
    if (a->kind == AG_OPEN || a->kind == AG_INVENTORY || !ag_direct_child(a->dst, folder) || !_wcsicmp(a->dst, report)) continue;
    const wchar_t *name = wcsrchr(a->dst, L'\\') + 1; BOOL exists = FALSE;
    for (int k = 0; k < *count; k++) if (!_wcsicmp(items[k].name, name)) exists = TRUE;
    if (exists) continue;
    if (*count >= AG_INV_MAX) { ok = FALSE; SetLastError(ERROR_MORE_DATA); break; }
    AgInvEntry *e = &items[(*count)++]; lstrcpynW(e->name, name, MAX_PATH); e->info = a->before;
    if (a->kind == AG_DIR || a->kind == AG_MOVE_DIR) e->info.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    e->planned = TRUE;
  }
  if (!ok) { free(items); return FALSE; }
  qsort(items, *count, sizeof(*items), ag_inv_order); *entries = items; return TRUE;
}
static void ag_inv_append(AgInvXml *b, const char *s) {
  if (b->failed) return;
  size_t n = strlen(s);
  if (b->n + n + 1 > 32u * 1024u * 1024u) { b->failed = TRUE; return; }
  if (b->n + n + 1 > b->cap) {
    size_t cap = b->cap ? b->cap * 2 : 8192;
    while (cap < b->n + n + 1) cap *= 2;
    char *p = (char *)realloc(b->p, cap); if (!p) { b->failed = TRUE; return; }
    b->p = p; b->cap = cap;
  }
  memcpy(b->p + b->n, s, n + 1); b->n += n;
}
static void ag_inv_text(AgInvXml *b, const wchar_t *s) {
  char utf8[MAX_PATH * 8 + 8];
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, utf8, sizeof(utf8), NULL, NULL)) { b->failed = TRUE; return; }
  for (const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
    if (*p == '&') ag_inv_append(b, "&amp;");
    else if (*p == '<') ag_inv_append(b, "&lt;");
    else if (*p == '>') ag_inv_append(b, "&gt;");
    else if (*p >= 32 || *p == '\t' || *p == '\n') { char c[2] = {(char)*p, 0}; ag_inv_append(b, c); }
  }
}
static void ag_inv_cell(AgInvXml *b, char col, int row, const wchar_t *s) {
  char tag[100]; snprintf(tag, sizeof(tag), "<c r=\"%c%d\" t=\"inlineStr\"><is><t xml:space=\"preserve\">", col, row);
  ag_inv_append(b, tag); ag_inv_text(b, s); ag_inv_append(b, "</t></is></c>");
}
static BOOL ag_inventory_write(AgFs *fs, const AgAction *action, int *listed) {
  AgInvEntry *items; int count;
  if (!ag_inv_entries(fs, action->text, action->dst, FALSE, &items, &count)) return FALSE;
  AgInvXml b = {0};
  ag_inv_append(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"2\" topLeftCell=\"A3\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><cols><col min=\"1\" max=\"1\" width=\"45\" customWidth=\"1\"/><col min=\"2\" max=\"4\" width=\"20\" customWidth=\"1\"/><col min=\"5\" max=\"5\" width=\"80\" customWidth=\"1\"/><col min=\"6\" max=\"6\" width=\"24\" customWidth=\"1\"/></cols><sheetData><row r=\"1\">");
  ag_inv_cell(&b, 'A', 1, L"Содержимое папки после выполнения плана; без самой книги. Подпапки не обходились.");
  ag_inv_append(&b, "</row><row r=\"2\">");
  const wchar_t *heads[] = {L"Имя", L"Тип", L"Расширение", L"Размер, байт", L"Путь", L"Изменён, UTC"};
  for (int c = 0; c < 6; c++) ag_inv_cell(&b, (char)('A' + c), 2, heads[c]);
  ag_inv_append(&b, "</row>");
  for (int i = 0; i < count && !b.failed; i++) {
    if (fs->cancel && InterlockedCompareExchange(fs->cancel, 0, 0)) { b.failed = TRUE; break; }
    AgInvEntry *e = &items[i]; BOOL dir = (e->info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    wchar_t size[32] = L"", path[MAX_PATH * 2 + 2], stamp[40] = L""; SYSTEMTIME st;
    if (!dir) swprintf(size, 32, L"%llu", ((unsigned long long)e->info.nFileSizeHigh << 32) | e->info.nFileSizeLow);
    swprintf(path, MAX_PATH * 2 + 2, L"%ls\\%ls", action->text, e->name);
    if (FileTimeToSystemTime(&e->info.ftLastWriteTime, &st)) swprintf(stamp, 40, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const wchar_t *ext = wcsrchr(e->name, L'.');
    const wchar_t *values[] = {e->name, (e->info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? L"Ссылка/облачный объект" : dir ? L"Папка" : L"Файл", dir || !ext ? L"" : ext, size, path, stamp};
    char tag[40]; snprintf(tag, sizeof(tag), "<row r=\"%d\">", i + 3); ag_inv_append(&b, tag);
    for (int c = 0; c < 6; c++) ag_inv_cell(&b, (char)('A' + c), i + 3, values[c]);
    ag_inv_append(&b, "</row>");
  }
  char end[100]; snprintf(end, sizeof(end), "</sheetData><autoFilter ref=\"A2:F%d\"/></worksheet>", count + 2); ag_inv_append(&b, end);
  BOOL ok = !b.failed && ag_write_document(action->dst, L"", AG_XLSX, b.p);
  free(b.p); free(items); if (ok) *listed = count; else if (b.failed) SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return ok;
}
static BOOL ag_order_inventories(AgFs *fs) {
  int n = 0; for (int i = 0; i < fs->count; i++) if (fs->plan[i].kind == AG_INVENTORY) n++;
  if (!n) return TRUE;
  AgAction *ordered = (AgAction *)malloc((size_t)fs->count * sizeof(*ordered));
  if (!ordered) return FALSE;
  int k = 0;
  for (int i = 0; i < fs->count; i++) if (fs->plan[i].kind != AG_INVENTORY) ordered[k++] = fs->plan[i];
  for (int i = 0; i < fs->count; i++) if (fs->plan[i].kind == AG_INVENTORY) ordered[k++] = fs->plan[i];
  memcpy(fs->plan, ordered, (size_t)fs->count * sizeof(*ordered)); free(ordered); return TRUE;
}
static BOOL ag_inventory_preview_counts(AgFs *fs) {
  for (int i = 0; i < fs->count; i++) if (fs->plan[i].kind == AG_INVENTORY) {
    AgInvEntry *items; int count;
    if (!ag_inv_entries(fs, fs->plan[i].text, fs->plan[i].dst, TRUE, &items, &count)) return FALSE;
    free(items); swprintf(fs->plan[i].cell, 16, L"%d", count);
  }
  return TRUE;
}
