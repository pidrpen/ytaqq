/* Local Office Automation. Owned instances only; never attach to an open user's
   document. File writes target a CREATE_NEW reservation, never the source.
   This module is also compiled directly by office_test.c. */
#include <oleauto.h>

static VARIANT of_int(long x) { VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = x; return v; }
static VARIANT of_bool(BOOL x) { VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = x ? VARIANT_TRUE : VARIANT_FALSE; return v; }
static VARIANT of_str(const wchar_t *s) { VARIANT v; VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(s); return v; }
/* Formula mode accepts canonical decimal literals as real numbers, independent
   of Windows/Excel locale. Leading-zero IDs and >15-digit values remain text. */
static BOOL of_decimal(const wchar_t *s, double *out) {
  BOOL negative = *s == L'-';
  if (*s == L'-' || *s == L'+') s++;
  if (*s < L'0' || *s > L'9' || (*s == L'0' && s[1] >= L'0' && s[1] <= L'9')) return FALSE;
  double value = 0, scale = 1; int digits = 0;
  while (*s >= L'0' && *s <= L'9') { if (++digits > 15) return FALSE; value = value * 10 + *s++ - L'0'; }
  if (*s == L'.') {
    s++; if (*s < L'0' || *s > L'9') return FALSE;
    while (*s >= L'0' && *s <= L'9') { if (++digits > 15) return FALSE; scale *= 10; value += (*s++ - L'0') / scale; }
  }
  if (*s) return FALSE;
  *out = negative ? -value : value; return TRUE;
}
/* Takes ownership of the input variants. */
static HRESULT of_call(IDispatch *d, const wchar_t *name, WORD flags, VARIANT *out, int n, VARIANT *args) {
  DISPID id, put = DISPID_PROPERTYPUT; LPOLESTR nm = (LPOLESTR)name;
  VARIANT reverse[16], tmp; VariantInit(&tmp);
  HRESULT hr = d ? d->lpVtbl->GetIDsOfNames(d, &IID_NULL, &nm, 1, 0x0409, &id) : E_POINTER;
  if (SUCCEEDED(hr)) {
    for (int i = 0; i < n; i++) reverse[i] = args[n - 1 - i];
    DISPPARAMS dp = {reverse, flags & DISPATCH_PROPERTYPUT ? &put : NULL, (UINT)n, flags & DISPATCH_PROPERTYPUT ? 1u : 0u};
    EXCEPINFO ex; memset(&ex, 0, sizeof(ex)); UINT bad = 0;
    hr = d->lpVtbl->Invoke(d, id, &IID_NULL, 0x0409, flags, &dp, out ? out : &tmp, &ex, &bad);
#ifdef OFFICE_TEST
    if (FAILED(hr)) fwprintf(stderr, L"Office %ls failed: 0x%08lx; %ls\n", name, (unsigned long)hr, ex.bstrDescription ? ex.bstrDescription : L"");
#endif
    SysFreeString(ex.bstrSource); SysFreeString(ex.bstrDescription); SysFreeString(ex.bstrHelpFile);
  }
  for (int i = 0; i < n; i++) VariantClear(&args[i]);
  VariantClear(&tmp); return hr;
}
static IDispatch *of_obj(IDispatch *d, const wchar_t *name, int n, VARIANT *args) {
  VARIANT v; VariantInit(&v);
  WORD flags = !wcscmp(name, L"Open") || !wcscmp(name, L"Add") ? DISPATCH_METHOD : DISPATCH_PROPERTYGET;
  HRESULT hr = of_call(d, name, flags, &v, n, args);
  IDispatch *p = SUCCEEDED(hr) && v.vt == VT_DISPATCH ? v.pdispVal : NULL;
  if (p) v.vt = VT_EMPTY;
  VariantClear(&v); return p;
}
static HRESULT of_put(IDispatch *d, const wchar_t *name, VARIANT v) { return of_call(d, name, DISPATCH_PROPERTYPUT, NULL, 1, &v); }
static long of_number(IDispatch *d, const wchar_t *name) {
  VARIANT v, num; VariantInit(&v); VariantInit(&num); long x = 0;
  if (SUCCEEDED(of_call(d, name, DISPATCH_PROPERTYGET, &v, 0, NULL)) && SUCCEEDED(VariantChangeType(&num, &v, 0, VT_I4))) x = num.lVal;
  VariantClear(&v); VariantClear(&num); return x;
}
static void of_release(IDispatch *d) { if (d) d->lpVtbl->Release(d); }
static BOOL of_excel(const wchar_t *path) { const wchar_t *p = wcsrchr(path, L'.'); return p && !_wcsicmp(p, L".xlsx"); }
static BOOL of_docx(const wchar_t *path) { const wchar_t *p = wcsrchr(path, L'.'); return p && !_wcsicmp(p, L".docx"); }

typedef struct { IDispatch *app, *collection, *doc; BOOL excel, initialized; HRESULT hr; } OfSession;
static void of_end(OfSession *s) {
  if (s->doc) { VARIANT a = of_bool(FALSE); of_call(s->doc, L"Close", DISPATCH_METHOD, NULL, 1, &a); }
  of_release(s->doc); of_release(s->collection);
  if (s->app) of_call(s->app, L"Quit", DISPATCH_METHOD, NULL, 0, NULL);
  of_release(s->app);
  if (s->initialized) CoUninitialize();
}
static BOOL of_begin(OfSession *s, BOOL excel, const wchar_t *path) {
  memset(s, 0, sizeof(*s)); s->excel = excel;
  s->hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (FAILED(s->hr)) return FALSE;
  s->initialized = TRUE; CLSID cls;
  s->hr = CLSIDFromProgID(excel ? L"Excel.Application" : L"Word.Application", &cls);
  if (FAILED(s->hr)) return FALSE;
  s->hr = CoCreateInstance(&cls, NULL, CLSCTX_LOCAL_SERVER, &IID_IDispatch, (void **)&s->app);
  if (FAILED(s->hr)) return FALSE;
  /* Force-disable macros for this owned session; no user Trust Center changes. */
  s->hr = of_put(s->app, L"AutomationSecurity", of_int(3));
  if (FAILED(s->hr)) return FALSE;
  of_put(s->app, L"Visible", of_bool(FALSE));
  of_put(s->app, L"DisplayAlerts", excel ? of_bool(FALSE) : of_int(0));
  if (excel) {
    of_put(s->app, L"EnableEvents", of_bool(FALSE));
    of_put(s->app, L"AskToUpdateLinks", of_bool(FALSE));
  }
  s->collection = of_obj(s->app, excel ? L"Workbooks" : L"Documents", 0, NULL);
  if (!s->collection) return FALSE;
  if (path && path[0]) {
    /* Avoid password/recovery prompts. No source writes, no MRU changes. */
    VARIANT a[13]; int n = excel ? 13 : 4;
    for (int i = 0; i < n; i++) { VariantInit(&a[i]); a[i].vt = VT_ERROR; a[i].scode = DISP_E_PARAMNOTFOUND; }
    a[0] = of_str(path); a[1] = excel ? of_int(0) : of_bool(FALSE); a[2] = of_bool(TRUE);
    if (!excel) a[3] = of_bool(FALSE);
    if (excel) { a[4] = of_str(L""); a[5] = of_str(L""); a[6] = of_bool(TRUE); a[10] = of_bool(FALSE); a[12] = of_bool(FALSE); }
    s->doc = of_obj(s->collection, L"Open", n, a);
  } else s->doc = of_obj(s->collection, L"Add", 0, NULL);
  if (!s->doc) { s->hr = E_FAIL; return FALSE; }
  return TRUE;
}
static IDispatch *of_sheet(OfSession *s, const wchar_t *name) {
  IDispatch *sheets = of_obj(s->doc, L"Worksheets", 0, NULL);
  VARIANT a = name && name[0] ? of_str(name) : of_int(1);
  IDispatch *sheet = of_obj(sheets, L"Item", 1, &a); of_release(sheets); return sheet;
}
static void of_text(const VARIANT *v, wchar_t *out, int cap) {
  VARIANT t; VariantInit(&t); out[0] = 0;
  if (v->vt == VT_ERROR) { swprintf(out, cap, L"[ошибка Excel %ld]", (long)v->scode); return; }
  if (v->vt == VT_EMPTY || v->vt == VT_NULL) return;
  if (SUCCEEDED(VariantChangeTypeEx(&t, (VARIANT *)v, LOCALE_INVARIANT, 0, VT_BSTR))) lstrcpynW(out, t.bstrVal, cap);
  VariantClear(&t);
}
/* Import stored cell values into a bounded Word draft without asking the model
   to retype, invent or rewrite them. All worksheets must fit, otherwise fail
   before staging anything. Formatting/charts/formula evaluation are not copied. */
static inline BOOL of_excel_word_text(const wchar_t *path, wchar_t *out, int cap, wchar_t *why, int whyCap) {
  OfSession s = {0}; BOOL ok = FALSE; out[0] = 0;
  lstrcpynW(why, L"Не удалось полностью перенести значения Excel в Word. План не изменён.", whyCap);
  if (!of_excel(path) || !of_begin(&s, TRUE, path)) {
    swprintf(why, whyCap, L"Не удалось прочитать исходный Excel: %ls; HRESULT 0x%08lx. Word не запланирован.", path, (unsigned long)s.hr);
    of_end(&s); return FALSE;
  }
  IDispatch *sheets = of_obj(s.doc, L"Worksheets", 0, NULL);
  int ns = (int)of_number(sheets, L"Count");
  if (ns < 1 || ns > 30) goto done;
  for (int i = 1; i <= ns; i++) {
    VARIANT index = of_int(i), name, values; VariantInit(&name); VariantInit(&values);
    IDispatch *sh = of_obj(sheets, L"Item", 1, &index), *used = of_obj(sh, L"UsedRange", 0, NULL);
    IDispatch *rs = of_obj(used, L"Rows", 0, NULL), *cs = of_obj(used, L"Columns", 0, NULL);
    int nr = (int)of_number(rs, L"Count"), nc = (int)of_number(cs, L"Count");
    BOOL complete = nr > 0 && nc > 0 && nr <= 100 && nc <= 26 && nr * nc <= 500 &&
      SUCCEEDED(of_call(sh, L"Name", DISPATCH_PROPERTYGET, &name, 0, NULL)) && name.vt == VT_BSTR &&
      SUCCEEDED(of_call(used, L"Value2", DISPATCH_PROPERTYGET, &values, 0, NULL));
    if (complete && wcslen(out) + SysStringLen(name.bstrVal) + 12 < (size_t)cap) {
      wcscat(out, L"Лист: "); wcscat(out, name.bstrVal); wcscat(out, L"\n");
      for (int r = 1; r <= nr && complete; r++) {
        for (int c = 1; c <= nc && complete; c++) {
          VARIANT value, text; VariantInit(&value); VariantInit(&text);
          HRESULT hr;
          if (values.vt & VT_ARRAY) { long at[2] = {r, c}; hr = SafeArrayGetElement(values.parray, at, &value); }
          else hr = VariantCopy(&value, &values);
          if (SUCCEEDED(hr) && value.vt != VT_EMPTY && value.vt != VT_NULL)
            hr = VariantChangeTypeEx(&text, &value, LOCALE_INVARIANT, 0, VT_BSTR);
          const wchar_t *cell = text.vt == VT_BSTR ? text.bstrVal : L"";
          size_t n = text.vt == VT_BSTR ? SysStringLen(text.bstrVal) : 0;
          for (size_t k = 0; k < n; k++) if (cell[k] < 32 && cell[k] != L'\t' && cell[k] != L'\n') hr = E_INVALIDARG;
          if (FAILED(hr) || wcslen(cell) != n || wcslen(out) + n + 2 >= (size_t)cap) complete = FALSE;
          else { wcscat(out, cell); wcscat(out, c == nc ? L"\n" : L"\t"); }
          VariantClear(&text); VariantClear(&value);
        }
      }
    } else complete = FALSE;
    VariantClear(&name); VariantClear(&values);
    of_release(rs); of_release(cs); of_release(used); of_release(sh);
    if (!complete) {
      swprintf(why, whyCap, L"Полный перенос Excel в Word не помещается в текущий лимит: до %d символов на весь документ, до 30 листов, 100 строк, 26 столбцов и 500 ячеек на лист; ошибки ячеек также отклоняются. Частичный документ не создан. Укажите меньший источник.", cap - 1);
      goto done;
    }
  }
  ok = TRUE;
done:
  of_release(sheets); of_end(&s); if (!ok) out[0] = 0; return ok;
}
static void of_col(int c, wchar_t *out) {
  wchar_t reverse[8]; int n = 0;
  do { reverse[n++] = (wchar_t)(L'A' + (c - 1) % 26); c = (c - 1) / 26; } while (c > 0 && n < 7);
  for (int i = 0; i < n; i++) out[i] = reverse[n - 1 - i];
  out[n] = 0;
}
static BOOL of_cell(const wchar_t *p, int *row, int *col) {
  *row = *col = 0; int letters = 0, digits = 0;
  while (*p >= L'A' && *p <= L'Z' && letters++ < 3) *col = *col * 26 + *p++ - L'A' + 1;
  while (*p >= L'0' && *p <= L'9' && digits++ < 7) *row = *row * 10 + *p++ - L'0';
  return !*p && letters && digits && *col <= 16384 && *row > 0 && *row <= 1048576;
}
/* Arithmetic and a small set of local worksheet functions only. No strings,
   external refs, names, links, DDE, add-ins, spill refs or arbitrary functions. */
static BOOL of_formula(const wchar_t *s) {
  if (*s++ != L'=') return FALSE;
  static const wchar_t *const allowed[] = {L"SUM", L"AVERAGE", L"MIN", L"MAX", L"COUNT", L"COUNTA", L"IF", L"IFERROR", L"ROUND", L"ROUNDUP", L"ROUNDDOWN", L"ABS", L"AND", L"OR", L"NOT", L"SUMIF", L"COUNTIF", L"SUMIFS", L"COUNTIFS", L"INDEX", L"MATCH"};
  int depth = 0, tokens = 0;
  while (*s) {
    if (++tokens > 500) return FALSE;
    if (*s >= L'A' && *s <= L'Z') {
      wchar_t token[32]; int n = 0;
      while (*s >= L'A' && *s <= L'Z') { if (n >= 30) return FALSE; token[n++] = *s++; } token[n] = 0;
      if (*s == L'(') {
        BOOL found = FALSE; for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) if (!wcscmp(token, allowed[i])) found = TRUE;
        if (!found) return FALSE;
      } else {
        int row, col; while (*s >= L'0' && *s <= L'9') { if (n >= 30) return FALSE; token[n++] = *s++; } token[n] = 0;
        if (!of_cell(token, &row, &col)) return FALSE;
      }
    } else if (*s == L'(') { depth++; s++; }
    else if (*s == L')') { if (--depth < 0) return FALSE; s++; }
    else if (wcschr(L"0123456789.+-*/^%,: <>=", *s)) s++;
    else return FALSE;
  }
  return tokens > 0 && depth == 0;
}
static BOOL of_table_valid(const wchar_t *text, BOOL formulas, int *rows, int *cols) {
  *rows = *cols = 1; int c = 1; const wchar_t *start = text;
  for (const wchar_t *p = text;; p++) {
    if (*p == L'\t' || *p == L'\n' || !*p) {
      if (formulas && *start == L'=') { wchar_t f[2000]; size_t n = (size_t)(p - start); if (n >= 2000) return FALSE; lstrcpynW(f, start, (int)n + 1); if (!of_formula(f)) return FALSE; }
      start = p + 1;
      if (c > *cols) *cols = c;
      if (*p == L'\t') c++; else if (*p == L'\n') { (*rows)++; c = 1; }
      if (!*p) break;
    } else if (*p < 32) return FALSE;
  }
  return *rows <= 100 && *cols <= 26 && *rows * *cols <= 500;
}
static BOOL of_read(const wchar_t *path, const wchar_t *sheetName, int offset, int columnOffset, wchar_t *out, int cap) {
  OfSession s = {0}; BOOL excel = of_excel(path), ok = FALSE;
  if ((!excel && !of_docx(path)) || offset < 0 || !of_begin(&s, excel, path)) {
    if (excel || of_docx(path)) { swprintf(out, cap, L"Не удалось открыть Office-файл. HRESULT 0x%08lx. Проверьте установку Office, доступ, пароль или окно первого запуска.", (unsigned long)s.hr); of_end(&s); }
    else lstrcpynW(out, L"office_read поддерживает только .xlsx и .docx. PDF, скан, картинку и .txt читай так: {\"tool\":\"app\",\"name\":\"file_read\",\"path\":\"полный_путь\"}.", cap);
    return FALSE;
  }
  if (!excel) {
    IDispatch *content = of_obj(s.doc, L"Content", 0, NULL); VARIANT v; VariantInit(&v);
    if (SUCCEEDED(of_call(content, L"Text", DISPATCH_PROPERTYGET, &v, 0, NULL)) && v.vt == VT_BSTR) {
      int n = (int)SysStringLen(v.bstrVal);
      if (offset <= n) {
        swprintf(out, cap, L"Word: %ls; символов: %d; смещение: %d.\r\n", path, n, offset);
        int head = (int)wcslen(out), room = cap - head - 220;
        if (room > 0) {
          lstrcpynW(out + head, v.bstrVal + offset, room);
          for (wchar_t *p = out + head; *p; p++) if (*p == 11 || *p == L'\r') *p = L'\n';
          if (n - offset >= room) swprintf(out + wcslen(out), cap - wcslen(out), L"\r\nНЕПОЛНЫЙ текст; следующий offset=%d", offset + room - 1);
          else lstrcpynW(out + wcslen(out), L"\r\nДокумент прочитан ПОЛНОСТЬЮ. Чтение завершено; следующий вызов done. Не повторяй office_read того же блока.\r\n", cap - (int)wcslen(out));
        }
        ok = TRUE;
      }
    }
    VariantClear(&v); of_release(content);
  } else {
    IDispatch *sheets = of_obj(s.doc, L"Worksheets", 0, NULL);
    swprintf(out, cap, L"Excel: %ls. Листы: ", path);
    int ns = (int)of_number(sheets, L"Count");
    for (int i = 1; i <= ns && i <= 30; i++) {
      VARIANT a = of_int(i), v; VariantInit(&v); IDispatch *sh = of_obj(sheets, L"Item", 1, &a);
      if (SUCCEEDED(of_call(sh, L"Name", DISPATCH_PROPERTYGET, &v, 0, NULL)) && v.vt == VT_BSTR && wcslen(out) + SysStringLen(v.bstrVal) + 8 < (size_t)cap) { wcscat(out, v.bstrVal); wcscat(out, L"; "); }
      VariantClear(&v); of_release(sh);
    }
    of_release(sheets);
    IDispatch *sh = of_sheet(&s, sheetName), *used = of_obj(sh, L"UsedRange", 0, NULL);
    IDispatch *rs = of_obj(used, L"Rows", 0, NULL), *cs = of_obj(used, L"Columns", 0, NULL);
    int first = (int)of_number(used, L"Row"), nr = (int)of_number(rs, L"Count"), nc = (int)of_number(cs, L"Count"), firstCol = (int)of_number(used, L"Column");
    if (first > 0 && nr > 0 && nc > 0 && offset < nr && columnOffset >= 0 && columnOffset < nc) {
      int count = nr - offset > 20 ? 20 : nr - offset, width = nc - columnOffset > 12 ? 12 : nc - columnOffset;
      firstCol += columnOffset;
      wchar_t col1[8], col2[8], address[64]; of_col(firstCol, col1); of_col(firstCol + width - 1, col2);
      swprintf(address, 64, L"%ls%d:%ls%d", col1, first + offset, col2, first + offset + count - 1);
      VARIANT a = of_str(address); IDispatch *range = of_obj(sh, L"Range", 1, &a);
      VARIANT values; VariantInit(&values);
      if (SUCCEEDED(of_call(range, L"Value2", DISPATCH_PROPERTYGET, &values, 0, NULL))) {
        swprintf(out + wcslen(out), cap - wcslen(out), L"\r\nДиапазон %ls. UsedRange: %d строк × %d столбцов. Значения без принудительного пересчёта; даты могут быть серийными числами.\r\n", address, nr, nc);
        int next = offset; BOOL full = FALSE;
        for (int r = 1; r <= count && !full; r++) {
          for (int c = 1; c <= width; c++) {
            VARIANT v; VariantInit(&v); wchar_t cell[320], col[8];
            if (values.vt & VT_ARRAY) { long index[2] = {r, c}; SafeArrayGetElement(values.parray, index, &v); }
            else VariantCopy(&v, &values);
            of_text(&v, cell, 320); VariantClear(&v);
            if (!cell[0]) continue;
            of_col(firstCol + c - 1, col);
            if (wcslen(out) + wcslen(cell) + 320 >= (size_t)cap) { full = TRUE; break; }
            swprintf(out + wcslen(out), cap - wcslen(out), L"%ls%d: %ls\r\n", col, first + offset + r - 1, cell);
          }
          if (!full) next = offset + r;
        }
        if (full || next < nr) swprintf(out + wcslen(out), cap - wcslen(out), L"НЕПОЛНЫЙ список; повтори office_read с offset=%d.\r\n", next);
        if (columnOffset + width < nc) {
          wchar_t nextCol[8]; of_col(columnOffset + width + 1, nextCol);
          swprintf(out + wcslen(out), cap - wcslen(out), L"ПО СТОЛБЦАМ НЕПОЛНО: следующий блок office_read с cell=%ls1 и тем же offset.\r\n", nextCol);
        }
        else if (!full && next >= nr) lstrcpynW(out + wcslen(out), L"Последняя страница. Чтение этого блока завершено; если других действий не просили, следующий вызов done.\r\n", cap - (int)wcslen(out));
        ok = TRUE;
      }
      VariantClear(&values); of_release(range);
    }
    of_release(rs); of_release(cs); of_release(used); of_release(sh);
  }
  of_end(&s);
  if (!ok) lstrcpynW(out, L"Не удалось прочитать лист/смещение. Проверьте имя листа, offset и доступ к документу.", cap);
  return ok;
}

/* Make a new workbook (or a new copy of src) and fill a bounded TSV block.
   Existing source files and open workbooks are untouched. Formula mode is explicit. */
static BOOL of_write_excel(const wchar_t *src, const wchar_t *dst, const wchar_t *sheetName, const wchar_t *address,
                           const wchar_t *text, BOOL formulas, wchar_t *out, int cap) {
  int row, col, rows, cols;
  if (!of_cell(address[0] ? address : L"A1", &row, &col) || !of_table_valid(text, formulas, &rows, &cols) || row + rows > 1048577 || col + cols > 16385) { lstrcpynW(out, L"Некорректный диапазон, таблица или формула. Допустим блок до 500 ячеек и только локальные формулы из разрешённого набора.", cap); return FALSE; }
  OfSession s; BOOL ok = FALSE; IDispatch *sh = NULL; VARIANT a;
  if (!of_begin(&s, TRUE, src)) { swprintf(out, cap, L"Excel недоступен: 0x%08lx.", (unsigned long)s.hr); of_end(&s); return FALSE; }
  sh = of_sheet(&s, src && src[0] ? sheetName : L"");
  if (!sh) { lstrcpynW(out, L"Указанный лист не найден. Исходник не изменён.", cap); goto done; }
  if ((!src || !src[0]) && FAILED(of_put(sh, L"Name", of_str(sheetName[0] ? sheetName : L"Лист1")))) { lstrcpynW(out, L"Недопустимое имя листа. Новая книга не сохранена.", cap); goto done; }
  const wchar_t *start = text; int rr = 0, cc = 0;
  for (const wchar_t *p = text;; p++) {
    if (*p != L'\t' && *p != L'\n' && *p) continue;
    wchar_t value[2000], column[8], target[32]; int len = (int)(p - start);
    lstrcpynW(value, start, len + 1); of_col(col + cc, column); swprintf(target, 32, L"%ls%d", column, row + rr);
    a = of_str(target); IDispatch *cell = of_obj(sh, L"Range", 1, &a);
    BOOL formula = formulas && value[0] == L'=';
    double number = 0; BOOL numeric = formulas && !formula && of_decimal(value, &number);
    HRESULT hr = of_put(cell, L"NumberFormat", of_str(formula || numeric ? L"General" : L"@"));
    if (SUCCEEDED(hr)) {
      VARIANT content;
      if (numeric) { VariantInit(&content); content.vt = VT_R8; content.dblVal = number; }
      else content = of_str(value);
      hr = of_put(cell, formula ? L"Formula" : L"Value2", content);
    }
    of_release(cell);
    if (FAILED(hr)) { swprintf(out, cap, L"Excel отклонил ячейку %ls: 0x%08lx. Новая книга не сохранена.", target, (unsigned long)hr); goto done; }
    if (!*p) break;
    if (*p == L'\n') { rr++; cc = 0; } else cc++;
    start = p + 1;
  }
  if (formulas) {
    if (FAILED(of_call(s.app, L"CalculateFullRebuild", DISPATCH_METHOD, NULL, 0, NULL))) { lstrcpynW(out, L"Excel не смог пересчитать формулы. Новая книга не сохранена.", cap); goto done; }
    /* Check every written formula cell, independent of the model's claim. */
    rr = cc = 0;
    for (const wchar_t *p = text;; p++) {
      if ((p == text || p[-1] == L'\t' || p[-1] == L'\n') && *p == L'=') {
        wchar_t column[8], target[32]; of_col(col + cc, column); swprintf(target, 32, L"%ls%d", column, row + rr);
        a = of_str(target); IDispatch *cell = of_obj(sh, L"Range", 1, &a); VARIANT v; VariantInit(&v);
        HRESULT hr = of_call(cell, L"Value2", DISPATCH_PROPERTYGET, &v, 0, NULL);
        BOOL error = FAILED(hr) || v.vt == VT_ERROR; VariantClear(&v); of_release(cell);
        if (error) { swprintf(out, cap, L"Формула в %ls вернула ошибку Excel. Книга не сохранена; исправьте формулу.", target); goto done; }
      }
      if (!*p) break;
      if (*p == L'\t') cc++; else if (*p == L'\n') { rr++; cc = 0; }
    }
  }
  /* Reserve the target first. SaveCopyAs replaces only our own reservation. */
  HANDLE reservation = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (reservation == INVALID_HANDLE_VALUE) { lstrcpynW(out, L"Назначение уже занято или недоступно. Исходник не изменён.", cap); goto done; }
  CloseHandle(reservation);
  a = of_str(dst);
  HRESULT hr = src && src[0] ? of_call(s.doc, L"SaveCopyAs", DISPATCH_METHOD, NULL, 1, &a)
                            : of_call(s.doc, L"SaveAs", DISPATCH_METHOD, NULL, 2, (VARIANT[]){a, of_int(51)});
  if (FAILED(hr)) { DeleteFileW(dst); swprintf(out, cap, L"Не удалось сохранить новую книгу: 0x%08lx. Исходник не изменён.", (unsigned long)hr); goto done; }
  swprintf(out, cap, L"Сохранена новая книга: %ls. Записан блок %d × %d. Формулы: %ls. Проверка смысла и контрольных сумм остаётся обязательной.", dst, rows, cols, formulas ? L"пересчитаны Excel, ошибок ячеек нет" : L"выключены, значения записаны текстом");
  ok = TRUE;
done:
  of_release(sh); of_end(&s); return ok;
}

typedef struct { wchar_t **cell; int rows, cols, firstRow, firstCol; } OfTable;
static void of_table_free(OfTable *t) {
  for (int i = 0; i < t->rows * t->cols; i++) free(t->cell[i]);
  free(t->cell); memset(t, 0, sizeof(*t));
}
/* Full-table loader for deterministic batch comparison. Refuse overflow rather
   than compare an unannounced prefix. Limit is 10k rows, 32 columns, 8 MB text. */
static BOOL of_table_load(const wchar_t *path, const wchar_t *sheet, OfTable *t, wchar_t *error, int cap) {
  memset(t, 0, sizeof(*t)); OfSession s;
  if (!of_excel(path) || !of_begin(&s, TRUE, path)) { lstrcpynW(error, L"Не удалось открыть .xlsx через установленный Excel.", cap); if (of_excel(path)) of_end(&s); return FALSE; }
  IDispatch *sh = of_sheet(&s, sheet), *range = of_obj(sh, L"UsedRange", 0, NULL);
  IDispatch *rs = of_obj(range, L"Rows", 0, NULL), *cs = of_obj(range, L"Columns", 0, NULL);
  t->rows = (int)of_number(rs, L"Count"); t->cols = (int)of_number(cs, L"Count");
  t->firstRow = (int)of_number(range, L"Row"); t->firstCol = (int)of_number(range, L"Column");
  BOOL ok = FALSE; VARIANT values; VariantInit(&values); size_t total = 0;
  if (t->rows < 2 || t->rows > 10001 || t->cols < 1 || t->cols > 32 || !t->firstRow) { lstrcpynW(error, L"Нужна таблица с шапкой в первой строке UsedRange, до 10000 строк данных и 32 столбцов. Таблица целиком не прочитана.", cap); t->rows = t->cols = 0; goto done; }
  t->cell = (wchar_t **)calloc((size_t)t->rows * t->cols, sizeof(wchar_t *));
  if (!t->cell || FAILED(of_call(range, L"Value2", DISPATCH_PROPERTYGET, &values, 0, NULL))) goto done;
  for (int r = 0; r < t->rows; r++) for (int c = 0; c < t->cols; c++) {
    VARIANT v; VariantInit(&v); wchar_t text[2001];
    if (values.vt & VT_ARRAY) { long index[2] = {r + 1, c + 1}; if (FAILED(SafeArrayGetElement(values.parray, index, &v))) goto done; }
    else VariantCopy(&v, &values);
    if (v.vt == VT_ERROR || (v.vt == VT_BSTR && SysStringLen(v.bstrVal) >= 2000)) { VariantClear(&v); lstrcpynW(error, L"В таблице ошибка Excel или ячейка длиннее 1999 символов. Сравнение отменено без обрезания данных.", cap); goto done; }
    of_text(&v, text, 2001); VariantClear(&v); size_t n = wcslen(text) + 1;
    total += n * sizeof(wchar_t); if (total > 8 * 1024 * 1024) goto done;
    t->cell[r * t->cols + c] = (wchar_t *)malloc(n * sizeof(wchar_t)); if (!t->cell[r * t->cols + c]) goto done;
    memcpy(t->cell[r * t->cols + c], text, n * sizeof(wchar_t));
  }
  ok = TRUE;
done:
  VariantClear(&values); of_release(rs); of_release(cs); of_release(range); of_release(sh); of_end(&s);
  if (!ok) { if (!error[0]) lstrcpynW(error, L"Не удалось прочитать таблицу целиком в пределах памяти. Ничего не сравнивалось.", cap); if (!t->cell) t->rows = t->cols = 0; of_table_free(t); }
  return ok;
}
