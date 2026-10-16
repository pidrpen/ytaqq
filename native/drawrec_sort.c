/* ---- «Разложить по папкам» (окно «Распознать чертёж», этап 2) -------------------

   Файлы, прочитанные в «Распознать чертёж», получают имя по штампу —
   «обозначение наименование.tif» — и раскладываются в папку назначения, по
   подпапкам типа листа (Чертежи, Спецификации, ТП, Прочее). Сначала всегда
   предпросмотр «было → станет»: ничего не трогается, пока не нажато
   «Применить». По умолчанию файлы копируются, исходные остаются на месте;
   перемещение — галочкой. Чужие файлы не перезаписываются: занятое имя
   получает « (2)». Каждое применение пишет журнал в папку назначения
   («журнал раскладки …txt») и его можно «Отменить»: копии удаляются,
   перемещённые возвращаются на место. Файл без обозначения не
   переименовывается — уходит в «Не распознано» со своим именем. */

#define ID_DRS_DIR 940
#define ID_DRS_PICK 941
#define ID_DRS_MOVE 942
#define ID_DRS_BYTYPE 943
#define ID_DRS_APPLY 944
#define ID_DRS_UNDO 945

typedef struct {
  wchar_t src[MAX_PATH], dst[MAX_PATH];
  wchar_t note[160];
  BOOL skip;  /* уже на месте или файла нет */
  BOOL doubt; /* обозначение подсвечено жёлтым */
} DrsRow;

typedef struct {
  wchar_t src[MAX_PATH], dst[MAX_PATH];
  BOOL moved;
} DrsOp;

static HWND g_drsWnd, g_drsList, g_drsDir, g_drsMove, g_drsByType;
static DrsRow *g_drsRows;
static int g_drsN;
static DrsOp *g_drsOps; /* последнее «Применить» — для «Отменить» */
static int g_drsOpsN;
static wchar_t g_drsLog[MAX_PATH];
static wchar_t g_drsStatus[300];

static const wchar_t *const kDrsTypeDir[DRT_N] = {L"Чертежи", L"Спецификации", L"ТП", L"Прочее"};

static void drs_status(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(g_drsStatus, 300, fmt, ap);
  va_end(ap);
  g_drsStatus[299] = 0;
  if (g_drsWnd) InvalidateRect(g_drsWnd, NULL, FALSE);
}

static void drs_dir_of(const wchar_t *path, wchar_t *dir) {
  lstrcpynW(dir, path, MAX_PATH);
  wchar_t *s = wcsrchr(dir, L'\\'), *f = wcsrchr(dir, L'/');
  if (f > s) s = f;
  if (s) *s = 0;
  else dir[0] = 0;
}

/* имя по штампу: «обозначение наименование», без запрещённых знаков, не длиннее 150 */
static void drs_name(const DrPage *pg, wchar_t *out, int cap) {
  wchar_t t[2 * DR_CELL + 2];
  if (pg->f[DF_NAME][0]) _snwprintf(t, 2 * DR_CELL + 2, L"%s %s", pg->f[DF_DES], pg->f[DF_NAME]);
  else lstrcpynW(t, pg->f[DF_DES], 2 * DR_CELL + 2);
  t[2 * DR_CELL + 1] = 0;
  if (cap > 151) cap = 151;
  dr_safe_name(t, out, cap);
}

static BOOL drs_taken(const wchar_t *p, int upto) {
  if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return TRUE;
  for (int i = 0; i < upto; i++)
    if (!g_drsRows[i].skip && !_wcsicmp(g_drsRows[i].dst, p)) return TRUE;
  return FALSE;
}

/* предпросмотр: по файлу — строка; имя и тип — по первой странице с обозначением */
static void drs_plan(void) {
  wchar_t root[MAX_PATH];
  GetWindowTextW(g_drsDir, root, MAX_PATH);
  dr_trim(root);
  size_t rl = wcslen(root);
  while (rl > 3 && (root[rl - 1] == L'\\' || root[rl - 1] == L'/')) root[--rl] = 0;
  BOOL byType = SendMessageW(g_drsByType, BM_GETCHECK, 0, 0) == BST_CHECKED;
  free(g_drsRows);
  g_drsRows = g_drn ? (DrsRow *)calloc((size_t)g_drn, sizeof(DrsRow)) : NULL;
  g_drsN = 0;
  for (int i = 0; i < g_drn && g_drsRows; i++) {
    const wchar_t *path = g_dri[i].pg.path;
    BOOL seen = FALSE;
    for (int k = 0; k < g_drsN && !seen; k++) seen = !_wcsicmp(g_drsRows[k].src, path);
    if (seen) continue;
    const DrPage *best = NULL;
    int pages = 0, des = 0;
    for (int k = i; k < g_drn; k++) {
      const DrPage *q = &g_dri[k].pg;
      if (_wcsicmp(q->path, path)) continue;
      pages++;
      if (q->done && q->f[DF_DES][0]) {
        des++;
        if (!best) best = q;
      }
    }
    DrsRow *r = &g_drsRows[g_drsN++];
    lstrcpynW(r->src, path, MAX_PATH);
    const wchar_t *base = tm_base(path), *ext = wcsrchr(base, L'.');
    if (!ext) ext = L"";
    wchar_t dir[MAX_PATH], name[160];
    if (!g_dri[i].pg.done) {
      r->skip = TRUE;
      lstrcpynW(r->note, L"ещё не распознан", 160);
      continue;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
      r->skip = TRUE;
      lstrcpynW(r->note, L"файла уже нет на месте", 160);
      continue;
    }
    if (best) {
      drs_name(best, name, 160);
      _snwprintf(dir, MAX_PATH, L"%s%s%s", root, byType ? L"\\" : L"", byType ? kDrsTypeDir[best->type] : L"");
      r->doubt = best->doubt[DF_DES];
      if (r->doubt) lstrcpynW(r->note, L"обозначение под сомнением — проверьте или поправьте в таблице", 160);
      else if (pages > 1 && des > 1) _snwprintf(r->note, 160, L"листов в файле: %d — имя по первому", pages);
    } else {
      lstrcpynW(name, base, ext[0] ? (int)(ext - base) + 1 : 160);
      _snwprintf(dir, MAX_PATH, L"%s\\Не распознано", root);
      lstrcpynW(r->note, L"обозначение не найдено — имя прежнее", 160);
    }
    dir[MAX_PATH - 1] = 0;
    wchar_t lext[16];
    lstrcpynW(lext, ext, 16);
    CharLowerBuffW(lext, (DWORD)wcslen(lext));
    _snwprintf(r->dst, MAX_PATH, L"%s\\%s%s", dir, name, lext);
    r->dst[MAX_PATH - 1] = 0;
    if (!_wcsicmp(r->dst, r->src)) {
      r->skip = TRUE;
      lstrcpynW(r->note, L"уже на месте с этим именем", 160);
      continue;
    }
    for (int n = 2; drs_taken(r->dst, g_drsN - 1) && n < 1000; n++) {
      _snwprintf(r->dst, MAX_PATH, L"%s\\%s (%d)%s", dir, name, n, lext);
      r->dst[MAX_PATH - 1] = 0;
    }
  }
  /* в список */
  SendMessageW(g_drsList, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(g_drsList);
  int todo = 0;
  for (int i = 0; i < g_drsN; i++) {
    DrsRow *r = &g_drsRows[i];
    wchar_t dir[MAX_PATH], rel[MAX_PATH];
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = i;
    it.pszText = (wchar_t *)tm_base(r->src);
    int idx = (int)SendMessageW(g_drsList, LVM_INSERTITEMW, 0, (LPARAM)&it);
    drs_dir_of(r->dst, dir);
    const wchar_t *sub = dir;
    if (!_wcsnicmp(dir, root, rl) && (dir[rl] == L'\\' || !dir[rl])) sub = dir[rl] ? dir + rl + 1 : L"(в саму папку)";
    lstrcpynW(rel, sub, MAX_PATH);
    const wchar_t *cells[3] = {r->skip ? L"—" : tm_base(r->dst), r->skip ? L"" : rel, r->note};
    for (int c = 0; c < 3; c++) {
      LVITEMW si;
      memset(&si, 0, sizeof(si));
      si.iSubItem = c + 1;
      si.pszText = (wchar_t *)cells[c];
      SendMessageW(g_drsList, LVM_SETITEMTEXTW, (WPARAM)idx, (LPARAM)&si);
    }
    if (!r->skip) todo++;
  }
  SendMessageW(g_drsList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_drsList, NULL, TRUE);
  BOOL move = SendMessageW(g_drsMove, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (!root[0]) drs_status(L"Укажите папку назначения");
  else if (!todo) drs_status(L"Раскладывать нечего");
  else
    drs_status(L"Будет %s файлов: %d. Пока ничего не тронуто — проверьте и нажмите «Применить»",
               move ? L"перемещено" : L"скопировано", todo);
}

/* папка с родителями */
static BOOL drs_mkdirs(const wchar_t *dir) {
  wchar_t t[MAX_PATH];
  lstrcpynW(t, dir, MAX_PATH);
  for (wchar_t *p = t + 3; *p; p++)
    if (*p == L'\\') {
      *p = 0;
      CreateDirectoryW(t, NULL);
      *p = L'\\';
    }
  CreateDirectoryW(t, NULL);
  DWORD a = GetFileAttributesW(t);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void drs_apply(void) {
  if (!g_drsN) return;
  BOOL move = SendMessageW(g_drsMove, BM_GETCHECK, 0, 0) == BST_CHECKED;
  free(g_drsOps);
  g_drsOps = (DrsOp *)calloc((size_t)g_drsN, sizeof(DrsOp));
  g_drsOpsN = 0;
  if (!g_drsOps) return;
  ShareBuf log;
  memset(&log, 0, sizeof(log));
  SYSTEMTIME st;
  GetLocalTime(&st);
  sb_add(&log, L"CursorPad · Распознать чертёж · раскладка %02d.%02d.%04d %02d:%02d\r\n%s\r\n\r\n", st.wDay, st.wMonth,
         st.wYear, st.wHour, st.wMinute, move ? L"перемещение" : L"копирование (исходные файлы на месте)");
  int ok = 0, bad = 0;
  wchar_t root[MAX_PATH] = L"";
  for (int i = 0; i < g_drsN; i++) {
    DrsRow *r = &g_drsRows[i];
    if (r->skip) continue;
    wchar_t dir[MAX_PATH];
    drs_dir_of(r->dst, dir);
    if (!root[0]) GetWindowTextW(g_drsDir, root, MAX_PATH);
    BOOL done = FALSE;
    /* занято с момента предпросмотра — следующий свободный номер */
    if (drs_mkdirs(dir) && GetFileAttributesW(r->dst) != INVALID_FILE_ATTRIBUTES) {
      wchar_t base[MAX_PATH], ext[16] = L"";
      lstrcpynW(base, r->dst, MAX_PATH);
      wchar_t *d = wcsrchr(base, L'.');
      if (d && d > wcsrchr(base, L'\\')) lstrcpynW(ext, d, 16), *d = 0;
      for (int n = 2; GetFileAttributesW(r->dst) != INVALID_FILE_ATTRIBUTES && n < 1000; n++)
        _snwprintf(r->dst, MAX_PATH, L"%s (%d)%s", base, n, ext);
    }
    done = move ? MoveFileExW(r->src, r->dst, MOVEFILE_COPY_ALLOWED) : CopyFileW(r->src, r->dst, TRUE);
    if (done) {
      DrsOp *o = &g_drsOps[g_drsOpsN++];
      lstrcpynW(o->src, r->src, MAX_PATH);
      lstrcpynW(o->dst, r->dst, MAX_PATH);
      o->moved = move;
      sb_add(&log, L"%s\t%s\t%s\r\n", move ? L"перемещён" : L"скопирован", r->src, r->dst);
      if (move) /* таблица дальше смотрит на новое место */
        for (int k = 0; k < g_drn; k++)
          if (!_wcsicmp(g_dri[k].pg.path, r->src)) lstrcpynW(g_dri[k].pg.path, r->dst, MAX_PATH);
      lstrcpynW(r->note, move ? L"перемещён" : L"скопирован", 160);
      r->skip = TRUE;
      ok++;
    } else {
      wchar_t why[120];
      FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), 0, why, 120,
                     NULL);
      dr_trim(why);
      _snwprintf(r->note, 160, L"не получилось: %s", why);
      sb_add(&log, L"ошибка\t%s\t%s\t%s\r\n", r->src, r->dst, why);
      bad++;
    }
    ListView_SetItemText(g_drsList, i, 3, r->note);
  }
  g_drsLog[0] = 0;
  if (ok && root[0]) {
    _snwprintf(g_drsLog, MAX_PATH, L"%s\\журнал раскладки %04d-%02d-%02d %02d-%02d-%02d.txt", root, st.wYear, st.wMonth,
               st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_drsLog[MAX_PATH - 1] = 0;
    if (log.w && !dr_write_utf8(g_drsLog, log.w)) g_drsLog[0] = 0;
  }
  free(log.w);
  EnableWindow(GetDlgItem(g_drsWnd, ID_DRS_UNDO), g_drsOpsN > 0);
  if (g_drWnd) InvalidateRect(g_drList, NULL, FALSE);
  if (bad) drs_status(L"%s: %d, не получилось: %d (причина — в строке)", move ? L"Перемещено" : L"Скопировано", ok, bad);
  else drs_status(L"%s: %d. Журнал — в папке назначения. Передумали — «Отменить»", move ? L"Перемещено" : L"Скопировано", ok);
#ifndef SET_TEST
  if (ok && root[0]) tm_open_file(root);
#endif
}

static void drs_undo(void) {
  int ok = 0, bad = 0;
  for (int i = g_drsOpsN - 1; i >= 0; i--) {
    DrsOp *o = &g_drsOps[i];
    BOOL done;
    if (o->moved) {
      done = MoveFileExW(o->dst, o->src, MOVEFILE_COPY_ALLOWED); /* исходное место занято — не затираем */
      if (done)
        for (int k = 0; k < g_drn; k++)
          if (!_wcsicmp(g_dri[k].pg.path, o->dst)) lstrcpynW(g_dri[k].pg.path, o->src, MAX_PATH);
    } else {
      done = DeleteFileW(o->dst);
    }
    if (done) ok++;
    else bad++;
    wchar_t dir[MAX_PATH];
    drs_dir_of(o->dst, dir);
    RemoveDirectoryW(dir); /* пустая подпапка, созданная раскладкой; непустую Windows не удалит */
  }
  g_drsOpsN = 0;
  EnableWindow(GetDlgItem(g_drsWnd, ID_DRS_UNDO), FALSE);
  if (g_drsLog[0]) {
    ShareBuf b;
    memset(&b, 0, sizeof(b));
    wchar_t *old = share_read(g_drsLog);
    sb_add(&b, L"%s\r\nОТМЕНЕНО: возвращено %d, не получилось %d\r\n", old ? old : L"", ok, bad);
    free(old);
    if (b.w) dr_write_utf8(g_drsLog, b.w);
    free(b.w);
  }
  drs_plan();
  drs_status(bad ? L"Отменено: %d, не получилось: %d (файл открыт или исходное место занято)"
                 : L"Отменено: всё вернулось как было (%d)",
             ok, bad);
}

static void drs_layout(void) {
  RECT rc;
  GetClientRect(g_drsWnd, &rc);
  int pad = DRS(14), y = PANEL_TITLE_H + DRS(26);
  place_panel_close(g_drsWnd);
  MoveWindow(g_drsDir, pad, y, rc.right - pad * 2 - DRS(120), DRS(26), TRUE);
  MoveWindow(GetDlgItem(g_drsWnd, ID_DRS_PICK), rc.right - pad - DRS(110), y - DRS(2), DRS(110), DRS(30), TRUE);
  MoveWindow(g_drsByType, pad, y + DRS(36), DRS(330), DRS(22), TRUE);
  MoveWindow(g_drsMove, pad + DRS(340), y + DRS(36), DRS(380), DRS(22), TRUE);
  int ly = y + DRS(96), by = rc.bottom - pad - DRS(30);
  MoveWindow(g_drsList, pad, ly, rc.right - pad * 2, by - DRS(10) - ly, TRUE);
  MoveWindow(GetDlgItem(g_drsWnd, ID_DRS_APPLY), pad, by, DRS(150), DRS(30), TRUE);
  MoveWindow(GetDlgItem(g_drsWnd, ID_DRS_UNDO), pad + DRS(158), by, DRS(130), DRS(30), TRUE);
}

static void drs_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, bg_brush(FALSE));
  draw_panel_header(hwnd, hdc, L"Разложить по папкам");
  SetBkMode(hdc, TRANSPARENT);
  int pad = DRS(14), y = PANEL_TITLE_H + DRS(6);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  SetTextColor(hdc, COL_MUTED);
  RECT a = {pad, y, rc.right - pad, y + DRS(18)};
  DrawTextW(hdc, L"Куда: папка назначения. Имя файла — «обозначение наименование» из штампа", -1, &a,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  RECT st = {pad, PANEL_TITLE_H + DRS(92), rc.right - pad, PANEL_TITLE_H + DRS(116)};
  DrawTextW(hdc, g_drsStatus, -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT drs_list_draw(NMLVCUSTOMDRAW *cd) {
  switch (cd->nmcd.dwDrawStage) {
  case CDDS_PREPAINT:
    return CDRF_NOTIFYITEMDRAW;
  case CDDS_ITEMPREPAINT: {
    int r = (int)cd->nmcd.dwItemSpec;
    cd->clrTextBk = ListView_GetBkColor(g_drsList);
    cd->clrText = GetSysColor(COLOR_WINDOWTEXT);
    if (r >= 0 && r < g_drsN) {
      if (g_drsRows[r].doubt && !g_drsRows[r].skip) cd->clrTextBk = RGB(0xFF, 0xF0, 0xA8);
      if (g_drsRows[r].skip) cd->clrText = COL_MUTED;
    }
    return CDRF_NEWFONT;
  }
  }
  return CDRF_DODEFAULT;
}

static LRESULT CALLBACK DrsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    drs_paint(hwnd, mem);
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
    mm->ptMinTrackSize.x = DRS(700);
    mm->ptMinTrackSize.y = DRS(360);
    return 0;
  }
  case WM_SIZE:
    drs_layout();
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_NOTIFY: {
    NMHDR *nh = (NMHDR *)lParam;
    if (nh->hwndFrom == g_drsList && nh->code == NM_CUSTOMDRAW) return drs_list_draw((NMLVCUSTOMDRAW *)lParam);
    break;
  }
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)bg_brush(FALSE);
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_PANEL_MIN) {
      ShowWindow(hwnd, SW_MINIMIZE);
      return 0;
    }
    if (id == ID_PANEL_CLOSE) ShowWindow(hwnd, SW_HIDE);
    if (id == ID_DRS_PICK) {
      wchar_t d[MAX_PATH];
      g_pickFolderTitle = L"Куда разложить файлы";
      if (pick_folder(hwnd, d, MAX_PATH)) {
        SetWindowTextW(g_drsDir, d);
        drs_plan();
      }
    }
    if (id == ID_DRS_DIR && HIWORD(wParam) == EN_KILLFOCUS) drs_plan();
    if ((id == ID_DRS_MOVE || id == ID_DRS_BYTYPE) && HIWORD(wParam) == BN_CLICKED) drs_plan();
    if (id == ID_DRS_APPLY) drs_apply();
    if (id == ID_DRS_UNDO) drs_undo();
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void drs_show(void) {
  BOOL any = FALSE;
  for (int i = 0; i < g_drn && !any; i++) any = g_dri[i].pg.done;
  if (!any) {
    dr_status(L"Сначала распознайте файлы — раскладка по их штампам");
    return;
  }
  if (!g_drsWnd) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DrsProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadDrawSort";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    RECT wr;
    GetWindowRect(g_drWnd, &wr);
    int w = DRS(1000), h = DRS(560);
    g_drsWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadDrawSort", L"Разложить по папкам — CursorPad",
                               WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                               wr.left + DRS(40), wr.top + DRS(40), w, h, NULL, NULL, g_inst, NULL);
    if (!g_drsWnd) return;
    round_corners(g_drsWnd);
    mk_btn(g_drsWnd, L"×", ID_PANEL_CLOSE);
    mk_btn(g_drsWnd, L"–", ID_PANEL_MIN);
    g_drsDir = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                               10, 10, g_drsWnd, (HMENU)(INT_PTR)ID_DRS_DIR, g_inst, NULL);
    if (g_fontBody) SendMessageW(g_drsDir, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    mk_btn(g_drsWnd, L"Выбрать…", ID_DRS_PICK);
    g_drsByType = CreateWindowExW(0, L"BUTTON", L"По подпапкам: Чертежи, Спецификации, ТП, Прочее",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10, 10, g_drsWnd,
                                  (HMENU)(INT_PTR)ID_DRS_BYTYPE, g_inst, NULL);
    g_drsMove = CreateWindowExW(0, L"BUTTON", L"Перемещать, а не копировать",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10, 10, g_drsWnd,
                                (HMENU)(INT_PTR)ID_DRS_MOVE, g_inst, NULL);
    if (g_fontUi) SendMessageW(g_drsByType, WM_SETFONT, (WPARAM)g_fontUi, FALSE);
    if (g_fontUi) SendMessageW(g_drsMove, WM_SETFONT, (WPARAM)g_fontUi, FALSE);
    SendMessageW(g_drsByType, BM_SETCHECK, BST_CHECKED, 0);
    mk_btn(g_drsWnd, L"Применить", ID_DRS_APPLY);
    mk_btn(g_drsWnd, L"Отменить", ID_DRS_UNDO);
    EnableWindow(GetDlgItem(g_drsWnd, ID_DRS_UNDO), FALSE);
    g_drsList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
                                0, 0, 10, 10, g_drsWnd, NULL, g_inst, NULL);
    SendMessageW(g_drsList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    if (g_fontBody) SendMessageW(g_drsList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    static const wchar_t *const head[4] = {L"Было", L"Станет", L"Папка", L"Заметка"};
    static const int wd[4] = {200, 300, 120, 330};
    for (int c = 0; c < 4; c++) {
      LVCOLUMNW col;
      memset(&col, 0, sizeof(col));
      col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
      col.pszText = (wchar_t *)head[c];
      col.cx = DRS(wd[c]);
      col.iSubItem = c;
      SendMessageW(g_drsList, LVM_INSERTCOLUMNW, (WPARAM)c, (LPARAM)&col);
    }
    drs_layout();
  }
  /* папка по умолчанию — «Разложено» рядом с первым файлом */
  wchar_t cur[MAX_PATH];
  GetWindowTextW(g_drsDir, cur, MAX_PATH);
  if (!cur[0]) {
    wchar_t d[MAX_PATH], def[MAX_PATH];
    drs_dir_of(g_dri[0].pg.path, d);
    _snwprintf(def, MAX_PATH, L"%s\\Разложено", d);
    def[MAX_PATH - 1] = 0;
    SetWindowTextW(g_drsDir, def);
  }
  drs_plan();
  ShowWindow(g_drsWnd, IsIconic(g_drsWnd) ? SW_RESTORE : SW_SHOWNORMAL);
  SetForegroundWindow(g_drsWnd);
}

#ifdef SET_TEST
/* проверочная сборка: --drawsort снимок папка файл… — распознать, предпросмотр
   (снимок.plan.bmp), применить копированием (снимок.done.bmp), отменить; что
   вышло — снимок.txt */
static void drs_test(int argc, wchar_t **argv) {
  if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);
  drawrec_show();
  TmPaths l = {0};
  for (int i = 4; i < argc; i++) tm_paths_add(&l, argv[i]);
  dr_add_paths(&l);
  ULONGLONG end = GetTickCount64() + 600000;
  dr_ui_pump(500);
  while ((g_drTodo > 0 || g_drRunning) && GetTickCount64() < end) dr_ui_pump(200);
  drs_show();
  SetWindowTextW(g_drsDir, argv[3]);
  drs_plan();
  const wchar_t *out = argv[2];
  dr_ui_snap_wnd(g_drsWnd, out, L"plan");
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  for (int i = 0; i < g_drsN; i++)
    sb_add(&b, L"plan\t%s\t%s\t%s\r\n", tm_base(g_drsRows[i].src), g_drsRows[i].skip ? L"—" : g_drsRows[i].dst,
           g_drsRows[i].note);
  drs_apply();
  dr_ui_snap_wnd(g_drsWnd, out, L"done");
  for (int i = 0; i < g_drsOpsN; i++)
    sb_add(&b, L"done\t%s\t%d\r\n", g_drsOps[i].dst, GetFileAttributesW(g_drsOps[i].dst) != INVALID_FILE_ATTRIBUTES);
  sb_add(&b, L"log\t%s\t%d\r\n", g_drsLog, GetFileAttributesW(g_drsLog) != INVALID_FILE_ATTRIBUTES);
  DrsOp keep[64];
  int nk = g_drsOpsN < 64 ? g_drsOpsN : 64;
  memcpy(keep, g_drsOps, sizeof(DrsOp) * (size_t)nk);
  drs_undo();
  for (int i = 0; i < nk; i++)
    sb_add(&b, L"undo\t%s\t%d\t%s\t%d\r\n", keep[i].dst, GetFileAttributesW(keep[i].dst) != INVALID_FILE_ATTRIBUTES,
           keep[i].src, GetFileAttributesW(keep[i].src) != INVALID_FILE_ATTRIBUTES);
  sb_add(&b, L"status\t%s\r\n", g_drsStatus);
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s.txt", out);
  if (b.w) dr_write_utf8(p, b.w);
  free(b.w);
  ExitProcess(0);
}
#endif
