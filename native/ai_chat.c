/* ai_chat.c — окно «Спросить ИИ» (пункт «Ещё»): чат с локальной моделью (ai.h).
   Беседа помнит реплики этого окна: модели уходят последние, что влезают в
   окно контекста (вкладка «ИИ»). Enter — отправить, Shift+Enter — новая строка.
   Ответ приходит сообщением WM_AI_CHAT с AiResult*. Беседа живёт в памяти окна. */

/* GUID форматов WIC: без библиотеки wincodecsdk их нет в ссылке */
static const GUID kAcPngFormat = {0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
static const GUID kAcBgra = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f}};
#define WM_AI_CHAT (WM_APP + 72) /* lParam — AiResult*, освободить ai_result_free */
#define WM_AC_SEND (WM_APP + 73) /* из поля ввода: отправить */
#define ID_AC_LOG 4701
#define ID_AC_IN 4702
#define ID_AC_SEND 4703
#define ID_AC_STOP 4704
#define ID_AC_CLEAR 4705
#define ID_AC_COPY 4706
#define ID_AC_SET 4707
#define ID_AC_STAT 4708
#define ID_AC_TITLE 4709
#define ID_AC_CMD 4710
#define ID_AC_SHOT 4711
#define ID_AC_THUMB 4712
#define AC_CMD_SYS                                                                                             \
  L"Ты разбираешь команду для программы CursorPad и переводишь её в ОДНУ операцию. Ответь ОДНОЙ строкой JSON "   \
  L"без текста вокруг. Операции: sort — {\"op\":\"sort\",\"src\":папка со сканами,\"dst\":папка результата,"  \
  L"\"mode\":\"copy\"} (раскладка сканов по штампу); mkdir — {\"op\":\"mkdir\",\"dst\":путь новой папки}; "   \
  L"copy и move — {\"op\":\"move\",\"src\":папка,\"name\":часть имени файла,\"dst\":папка}; rename — "         \
  L"{\"op\":\"rename\",\"src\":папка,\"name\":имя файла,\"new\":новое имя}; ask — {\"op\":\"ask\",\"question\":"\
  L"вопрос по-русски}. Папку пиши из списка известных или полный путь, который назвал пользователь; пустой src "    \
  L"значит последнюю папку. move — только если пользователь пишет «перемести», «перенеси», «закинь» или «переложи»; "  \
  L"иначе copy. Имя файла — только слова из просьбы, ничего не додумывай. Удалять нельзя: ответь ask. "           \
  L"Непонятно — ask, а не угадывай."
#define AC_SYS L"Ты помощник в программе CursorPad. Отвечай по-русски, по делу и кратко, если не просят иначе."

typedef struct {
  int assistant;
  wchar_t *text;
  BYTE *png; /* картинка к вопросу, PNG; NULL — без неё */
  DWORD pngLen;
} AcTurn;
static HWND g_acWnd, g_acLog, g_acIn, g_acSend, g_acStop, g_acClear, g_acCopy, g_acSet, g_acStat, g_acTitle, g_acCmdChk,
    g_acShot, g_acThumb;
static BYTE *g_acImg;      /* картинка, ждёт отправки */
static DWORD g_acImgLen;
static HBITMAP g_acThumbBmp; /* миниатюра ждущей картинки (и той, что не ушла) */
static void ac_relayout(void);
static void ac_layout(int W, int H);
static BOOL g_acPlanOn; /* план «было → станет» ждёт «Применить» */
static void ac_plan_ui(BOOL on);
static void ac_files_run(const wchar_t *j);
static void ac_files_apply(void);
static void ac_files_cancel(void);
static const wchar_t *ac_cmd_instruction(void);
static void af_set_last(const wchar_t *dir);
static void af_clear(void);
static BOOL g_acCmdWait;      /* ответ на команду для файлов, а не чат */
static wchar_t g_acCmdText[2000];
static wchar_t g_acSortDst[MAX_PATH];
static BOOL g_acSortMove;
static WNDPROC g_acEditOld;
static AcTurn *g_acTurns;
static int g_acN, g_acCap;
static int g_acId; /* запрос в пути, 0 — нет */

static void ac_push(int assistant, const wchar_t *text) {
  if (g_acN == g_acCap) {
    int cap = g_acCap ? g_acCap * 2 : 16;
    AcTurn *t = (AcTurn *)realloc(g_acTurns, cap * sizeof(AcTurn));
    if (!t) return;
    g_acTurns = t;
    g_acCap = cap;
  }
  wchar_t *c = (wchar_t *)malloc((wcslen(text) + 1) * sizeof(wchar_t));
  if (!c) return;
  wcscpy(c, text);
  g_acTurns[g_acN].assistant = assistant;
  g_acTurns[g_acN].text = c;
  g_acTurns[g_acN].png = NULL;
  g_acTurns[g_acN].pngLen = 0;
  g_acN++;
}

/* снять последнюю реплику и вернуть её текст в поле ввода (вопрос не дошёл) */
/* вопрос не дошёл: текст и картинка возвращаются в поле ввода */
static void ac_unsend(void) {
  if (!g_acN) return;
  AcTurn *t = &g_acTurns[g_acN - 1];
  SetWindowTextW(g_acIn, t->text);
  if (t->png) {
    if (g_acImg) free(t->png);
    else {
      g_acImg = t->png;
      g_acImgLen = t->pngLen;
      ac_relayout();
    }
  }
  free(t->text);
  g_acN--;
}

static void ac_clear_turns(void) {
  for (int i = 0; i < g_acN; i++) {
    free(g_acTurns[i].text);
    free(g_acTurns[i].png);
  }
  g_acN = 0;
}

static void ac_redraw(void) {
  size_t cap = 64;
  for (int i = 0; i < g_acN; i++) cap += wcslen(g_acTurns[i].text) + 32;
  wchar_t *s = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!s) return;
  s[0] = 0;
  for (int i = 0; i < g_acN; i++) {
    wcscat(s, g_acTurns[i].assistant ? L"ИИ:\r\n" : L"Вы:\r\n");
    if (g_acTurns[i].png) wcscat(s, L"[картинка] ");
    wcscat(s, g_acTurns[i].text);
    wcscat(s, L"\r\n\r\n");
  }
  SetWindowTextW(g_acLog, s);
  int len = GetWindowTextLengthW(g_acLog);
  SendMessageW(g_acLog, EM_SETSEL, len, len);
  SendMessageW(g_acLog, EM_SCROLLCARET, 0, 0);
  free(s);
}

static void ac_status(const wchar_t *t) { SetWindowTextW(g_acStat, t ? t : L""); }

static void ac_busy(BOOL on) {
  EnableWindow(g_acSend, !on);
  EnableWindow(g_acStop, on);
}

/* план ждёт ответа: кнопки «Отправить» и «Стоп» становятся «Применить» и «Отмена» */
static void ac_plan_ui(BOOL on) {
  SetWindowTextW(g_acSend, on ? L"Применить" : L"Отправить");
  SetWindowTextW(g_acStop, on ? L"Отмена" : L"Стоп");
  if (on) {
    EnableWindow(g_acSend, TRUE);
    EnableWindow(g_acStop, TRUE);
  } else
    ac_busy(g_acId != 0);
}

/* «ИИ выключен» / «нет модели» — ведём на вкладку «ИИ» Настроек */
static void ac_open_settings(void) {
  select_tab(SEC_AI);
  if (g_setHwnd && !IsWindowVisible(g_setHwnd)) toggle_settings();
}

static void ac_clip(const wchar_t *s) {
  if (!OpenClipboard(g_acWnd)) return;
  EmptyClipboard();
  size_t bytes = (wcslen(s) + 1) * sizeof(wchar_t);
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (g) {
    memcpy(GlobalLock(g), s, bytes);
    GlobalUnlock(g);
    if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
  }
  CloseClipboard();
}

static void ac_send(void) {
  if (g_acId) return;
  if (g_acPlanOn) {
    ac_status(L"Сначала «Применить» или «Отмена» для плана");
    return;
  }
  int len = GetWindowTextLengthW(g_acIn);
  wchar_t *in = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
  if (!in) return;
  GetWindowTextW(g_acIn, in, len + 1);
  wchar_t *a = in;
  while (*a == L' ' || *a == L'\r' || *a == L'\n' || *a == L'\t') a++;
  size_t n = wcslen(a);
  while (n && (a[n - 1] == L' ' || a[n - 1] == L'\r' || a[n - 1] == L'\n' || a[n - 1] == L'\t')) a[--n] = 0;
  if (!n) {
    free(in);
    return;
  }
  if (!ai_ready()) {
    ac_status(ai_status_text(g_aiOn ? AI_NO_MODEL : AI_OFF));
    ac_open_settings();
    free(in);
    return;
  }
  if ((int)n > ai_chat_budget()) {
    ac_status(L"Сообщение длиннее окна контекста. Сократите его или увеличьте контекст на вкладке «ИИ».");
    free(in);
    return;
  }
  BOOL cmd = SendMessageW(g_acCmdChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (cmd && g_acImg) {
    ac_status(L"В командах для файлов картинки не нужны. Уберите картинку или снимите галочку.");
    free(in);
    return;
  }
  ac_push(0, a);
  if (g_acImg && g_acN) {
    g_acTurns[g_acN - 1].png = g_acImg; /* пока только ссылка: отдадим, если запрос пойдёт */
    g_acTurns[g_acN - 1].pngLen = g_acImgLen;
  }
  AiTurn *t = (AiTurn *)malloc(g_acN * sizeof(AiTurn));
  int id = 0;
  if (t) {
    for (int i = 0; i < g_acN; i++) {
      t[i].assistant = g_acTurns[i].assistant;
      t[i].text = g_acTurns[i].text;
      t[i].png = g_acTurns[i].png;
      t[i].pngLen = g_acTurns[i].pngLen;
    }
    /* команда разбирается без истории: ответ — только JSON */
    int from = cmd ? g_acN - 1 : 0;
    id = ai_ask_chat_async(cmd ? ac_cmd_instruction() : AC_SYS, t + from, g_acN - from, 180000, g_acWnd, WM_AI_CHAT, NULL);
    free(t);
  }
  if (!id) {
    if (g_acN) g_acTurns[g_acN - 1].png = NULL; /* картинка осталась ждать */
    ac_unsend();
    ac_status(L"Не вышло спросить модель, попробуйте ещё раз");
    free(in);
    return;
  }
  g_acId = id;
  g_acCmdWait = cmd;
  if (g_acImg) { /* ушла вместе с вопросом */
    g_acImg = NULL;
    g_acImgLen = 0;
    ac_relayout();
  }
  if (cmd) lstrcpynW(g_acCmdText, a, 2000);
  SetWindowTextW(g_acIn, L"");
  ac_redraw();
  ac_busy(TRUE);
  ac_status(L"Думаю… первый ответ после запуска модели может грузиться до минуты");
  free(in);
}

/* поле "key" из JSON-строки ответа модели; \\ и \" раскрываются */
static BOOL ac_json_field(const wchar_t *j, const wchar_t *key, wchar_t *out, int cap) {
  wchar_t pat[64];
  _snwprintf(pat, 64, L"\"%s\"", key);
  const wchar_t *p = wcsstr(j, pat);
  if (!p) return FALSE;
  p = wcschr(p + wcslen(pat), L'"');
  if (!p) return FALSE;
  int n = 0;
  for (p++; *p && *p != L'"' && n < cap - 1; p++) {
    if (*p == L'\\' && p[1]) p++;
    out[n++] = *p;
  }
  out[n] = 0;
  return TRUE;
}

static BOOL ac_dir_exists(const wchar_t *p) {
  DWORD a = GetFileAttributesW(p);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* путь назван в тексте пользователя — модель не может подсунуть свою папку */
static BOOL ac_named(const wchar_t *path) {
  wchar_t t[MAX_PATH];
  lstrcpynW(t, path, MAX_PATH);
  size_t n = wcslen(t);
  while (n > 3 && (t[n - 1] == L'\\' || t[n - 1] == L'/')) t[--n] = 0;
  return wcsstr(g_acCmdText, t) != NULL;
}

/* запуск раскладки: сканы папки идут в «Распознать чертёж», потом предпросмотр */
static void ac_sort_ready(void) {
  drs_show();
  if (!g_drsWnd) {
    ac_push(1, L"Нечего раскладывать: ни один скан не распознан.");
    ac_redraw();
    return;
  }
  SetWindowTextW(g_drsDir, g_acSortDst);
  SendMessageW(g_drsMove, BM_SETCHECK, g_acSortMove ? BST_CHECKED : BST_UNCHECKED, 0);
  drs_plan();
  InvalidateRect(g_drsWnd, NULL, FALSE);
  ac_push(1, L"Предпросмотр в окне «Разложить по папкам». Ничего не изменится, пока вы не нажмёте «Применить».");
  ac_redraw();
  ac_status(L"");
}

static void ac_sort_start(const wchar_t *src, const wchar_t *dst, BOOL move) {
  TmPaths l = {0};
  wchar_t pat[MAX_PATH];
  _snwprintf(pat, MAX_PATH, L"%s\\*", src);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
      if (!ext) continue;
      if (!_wcsicmp(ext, L".pdf") || !_wcsicmp(ext, L".tif") || !_wcsicmp(ext, L".tiff") || !_wcsicmp(ext, L".png") ||
          !_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg") || !_wcsicmp(ext, L".bmp")) {
        wchar_t full[MAX_PATH];
        _snwprintf(full, MAX_PATH, L"%s\\%s", src, fd.cFileName);
        tm_paths_add(&l, full);
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  if (!l.n) {
    free(l.p);
    ac_push(1, L"В этой папке нет сканов (pdf, tif, png, jpg, bmp).");
    ac_redraw();
    return;
  }
  drawrec_show();
  af_set_last(dst);
  lstrcpynW(g_acSortDst, dst, MAX_PATH);
  g_acSortMove = move;
  g_drSortHook = ac_sort_ready;
  dr_add_paths(&l);
  ac_push(1, L"Распознаю сканы из папки. Когда закончу, покажу предпросмотр.");
  ac_redraw();
}

/* ответ модели на команду: проверки здесь, а не в модели */
static void ac_cmd_run(const wchar_t *j) {
  wchar_t op[32] = L"", src[MAX_PATH] = L"", dst[MAX_PATH] = L"", mode[16] = L"", q[300] = L"";
  ac_json_field(j, L"op", op, 32);
  if (!wcscmp(op, L"sort") && ac_json_field(j, L"src", src, MAX_PATH) && ac_json_field(j, L"dst", dst, MAX_PATH)) {
    ac_json_field(j, L"mode", mode, 16);
    if (!ac_named(src) || !ac_named(dst) || !ac_dir_exists(src) || !ac_dir_exists(dst)) {
      ac_push(1, L"Не нашёл такие папки. Напишите путь точно, как он открыт в проводнике.");
      return;
    }
    /* перемещать — только если пользователь так и написал */
    BOOL move = !wcscmp(mode, L"move") && (wcsstr(g_acCmdText, L"перемест") || wcsstr(g_acCmdText, L"перенес"));
    ac_sort_start(src, dst, move);
    return;
  }
  if (!wcscmp(op, L"ask") && ac_json_field(j, L"question", q, 300)) {
    ac_push(1, q);
    return;
  }
  ac_files_run(j);
}

static void ac_relayout(void) {
  if (!g_acWnd) return;
  RECT rc;
  GetClientRect(g_acWnd, &rc);
  ac_layout(rc.right, rc.bottom);
}

static IWICImagingFactory *ac_factory(void) {
  IWICImagingFactory *f = NULL;
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  return f;
}

/* картинка по большей стороне не больше maxSide, 32 бита BGRA */
static IWICBitmapSource *ac_scaled(IWICImagingFactory *f, IWICBitmapSource *src, UINT maxSide, UINT *ow, UINT *oh) {
  UINT w = 0, h = 0;
  if (FAILED(src->lpVtbl->GetSize(src, &w, &h)) || !w || !h) return NULL;
  if (w > maxSide || h > maxSide) {
    if (w >= h) {
      h = (UINT)((UINT64)h * maxSide / w);
      w = maxSide;
    } else {
      w = (UINT)((UINT64)w * maxSide / h);
      h = maxSide;
    }
  }
  if (!w) w = 1;
  if (!h) h = 1;
  IWICBitmapScaler *sc = NULL;
  IWICFormatConverter *cv = NULL;
  if (FAILED(f->lpVtbl->CreateBitmapScaler(f, &sc))) return NULL;
  sc->lpVtbl->Initialize(sc, src, w, h, WICBitmapInterpolationModeFant);
  if (FAILED(f->lpVtbl->CreateFormatConverter(f, &cv))) {
    sc->lpVtbl->Release(sc);
    return NULL;
  }
  cv->lpVtbl->Initialize(cv, (IWICBitmapSource *)sc, &kAcBgra, WICBitmapDitherTypeNone, NULL, 0.0,
                         WICBitmapPaletteTypeCustom);
  sc->lpVtbl->Release(sc);
  *ow = w;
  *oh = h;
  return (IWICBitmapSource *)cv;
}

static BOOL ac_png(IWICImagingFactory *f, IWICBitmapSource *src, UINT w, UINT h, BYTE **out, DWORD *outLen) {
  IStream *st = NULL;
  IWICBitmapEncoder *enc = NULL;
  IWICBitmapFrameEncode *fr = NULL;
  IPropertyBag2 *pb = NULL;
  BOOL ok = FALSE;
  if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &st))) return FALSE;
  if (SUCCEEDED(f->lpVtbl->CreateEncoder(f, &kAcPngFormat, NULL, &enc)) &&
      SUCCEEDED(enc->lpVtbl->Initialize(enc, st, WICBitmapEncoderNoCache)) &&
      SUCCEEDED(enc->lpVtbl->CreateNewFrame(enc, &fr, &pb)) && SUCCEEDED(fr->lpVtbl->Initialize(fr, pb)) &&
      SUCCEEDED(fr->lpVtbl->SetSize(fr, w, h))) {
    WICPixelFormatGUID pf = kAcBgra;
    if (SUCCEEDED(fr->lpVtbl->SetPixelFormat(fr, &pf)) && SUCCEEDED(fr->lpVtbl->WriteSource(fr, src, NULL)) &&
        SUCCEEDED(fr->lpVtbl->Commit(fr)) && SUCCEEDED(enc->lpVtbl->Commit(enc))) {
      LARGE_INTEGER zero = {{0, 0}};
      ULARGE_INTEGER pos = {{0, 0}};
      HGLOBAL g = NULL;
      st->lpVtbl->Seek(st, zero, STREAM_SEEK_CUR, &pos); /* конец записанного */
      if (SUCCEEDED(GetHGlobalFromStream(st, &g))) {
        BYTE *lk = (BYTE *)GlobalLock(g);
        BYTE *p = (BYTE *)malloc((size_t)pos.QuadPart);
        if (p && lk) {
          memcpy(p, lk, (size_t)pos.QuadPart);
          *out = p;
          *outLen = (DWORD)pos.QuadPart;
          ok = TRUE;
        } else
          free(p);
        GlobalUnlock(g);
      }
    }
  }
  if (pb) pb->lpVtbl->Release(pb);
  if (fr) fr->lpVtbl->Release(fr);
  if (enc) enc->lpVtbl->Release(enc);
  st->lpVtbl->Release(st);
  return ok;
}

/* картинка прикрепляется к следующему вопросу: PNG до 768 px и миниатюра 80 px */
static void ac_take(IWICImagingFactory *f, IWICBitmapSource *src) {
  if (!ai_vision_ready()) {
    ac_status(L"Картинки читает модель «С картинками». Скачайте её на вкладке «ИИ».");
    ac_open_settings();
    return;
  }
  UINT w = 0, h = 0, tw = 0, th = 0;
  IWICBitmapSource *big = ac_scaled(f, src, 768, &w, &h);
  BYTE *png = NULL;
  DWORD pl = 0;
  if (!big || !ac_png(f, big, w, h, &png, &pl)) {
    if (big) big->lpVtbl->Release(big);
    ac_status(L"Не вышло прочитать картинку");
    return;
  }
  HBITMAP hb = NULL;
  IWICBitmapSource *small = ac_scaled(f, big, 80, &tw, &th);
  if (small) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)tw;
    bi.bmiHeader.biHeight = -(LONG)th;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HDC dc = GetDC(NULL);
    hb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (hb && bits) small->lpVtbl->CopyPixels(small, NULL, tw * 4, tw * 4 * th, (BYTE *)bits);
    small->lpVtbl->Release(small);
  }
  big->lpVtbl->Release(big);
  free(g_acImg);
  g_acImg = png;
  g_acImgLen = pl;
  if (g_acThumbBmp) DeleteObject(g_acThumbBmp);
  g_acThumbBmp = hb;
  SendMessageW(g_acThumb, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hb);
  ac_relayout();
  ac_status(L"Картинка прикреплена к следующему вопросу. Клик по миниатюре — убрать.");
}

static void ac_drop_img(void) {
  free(g_acImg);
  g_acImg = NULL;
  g_acImgLen = 0;
  ac_relayout();
  ac_status(L"Картинка убрана");
}

static void ac_from_file(const wchar_t *path) {
  IWICImagingFactory *f = ac_factory();
  IWICBitmapDecoder *dec = NULL;
  if (!f || FAILED(f->lpVtbl->CreateDecoderFromFilename(f, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) {
    ac_status(L"Это не картинка (нужны PNG, JPG, BMP, TIFF)");
  } else {
    IWICBitmapFrameDecode *fr = NULL;
    if (SUCCEEDED(dec->lpVtbl->GetFrame(dec, 0, &fr))) ac_take(f, (IWICBitmapSource *)fr);
    if (fr) fr->lpVtbl->Release(fr);
  }
  if (dec) dec->lpVtbl->Release(dec);
  if (f) f->lpVtbl->Release(f);
}

/* Ctrl+V: картинка из буфера (скриншот) */
static void ac_paste_img(void) {
  if (!OpenClipboard(g_acWnd)) return;
  HBITMAP clip = (HBITMAP)GetClipboardData(CF_BITMAP);
  IWICImagingFactory *f = clip ? ac_factory() : NULL;
  IWICBitmap *bmp = NULL;
  if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromHBITMAP(f, clip, NULL, WICBitmapUseAlpha, &bmp)))
    ac_take(f, (IWICBitmapSource *)bmp);
  if (bmp) bmp->lpVtbl->Release(bmp);
  if (f) f->lpVtbl->Release(f);
  CloseClipboard();
}

/* «С экрана»: область мышью, как в «Распознать чертёж» */
static void ac_shot_done(RECT r) {
  ShowWindow(g_acWnd, SW_SHOWNORMAL);
  int w = r.right - r.left, h = r.bottom - r.top;
  if (w < 20 || h < 20) {
    ac_status(L"Выделено слишком мало — обведите картинку целиком");
    return;
  }
  HDC s = GetDC(NULL);
  HDC m = CreateCompatibleDC(s);
  HBITMAP hb = CreateCompatibleBitmap(s, w, h);
  HGDIOBJ old = SelectObject(m, hb);
  BitBlt(m, 0, 0, w, h, s, r.left, r.top, SRCCOPY);
  SelectObject(m, old);
  DeleteDC(m);
  ReleaseDC(NULL, s);
  IWICImagingFactory *f = ac_factory();
  IWICBitmap *bmp = NULL;
  if (f && SUCCEEDED(f->lpVtbl->CreateBitmapFromHBITMAP(f, hb, NULL, WICBitmapUseAlpha, &bmp)))
    ac_take(f, (IWICBitmapSource *)bmp);
  if (bmp) bmp->lpVtbl->Release(bmp);
  if (f) f->lpVtbl->Release(f);
  DeleteObject(hb);
}

static void ac_shot(void) {
  if (!ai_vision_ready()) {
    ac_status(L"Картинки читает модель «С картинками». Скачайте её на вкладке «ИИ».");
    ac_open_settings();
    return;
  }
  ShowWindow(g_acWnd, SW_HIDE);
  Sleep(150);
  g_pickDone = ac_shot_done;
  start_ocr_pick();
}

static void ac_result(AiResult *r) {
  if (!r) return;
  if (r->id != g_acId) {
    ai_result_free(r);
    return;
  }
  g_acId = 0;
  ac_busy(FALSE);
  BOOL cmd = g_acCmdWait;
  g_acCmdWait = FALSE;
  if (r->status == AI_OK && cmd) {
    ac_cmd_run(r->text);
    ac_redraw();
  } else if (r->status == AI_OK) {
    ac_push(1, r->text);
    ac_redraw();
    ac_status(L"");
  } else {
    ac_unsend();
    ac_redraw();
    ac_status(r->status == AI_CANCELLED ? L"Остановлено, вопрос вернулся в поле ввода"
                                        : (r->text && r->text[0] ? r->text : ai_status_text(r->status)));
  }
  ai_result_free(r);
}

static void ac_clear(void) {
  if (g_acPlanOn) af_clear();
  ac_plan_ui(FALSE);
  if (g_acId) ai_cancel(g_acId);
  g_acId = 0;
  ac_busy(FALSE);
  ac_clear_turns();
  free(g_acImg);
  g_acImg = NULL;
  g_acImgLen = 0;
  ac_relayout();
  SetWindowTextW(g_acLog, L"");
  ac_status(L"");
}

static void ac_copy_answer(void) {
  for (int i = g_acN - 1; i >= 0; i--)
    if (g_acTurns[i].assistant) {
      ac_clip(g_acTurns[i].text);
      ac_status(L"Ответ скопирован");
      return;
    }
  ac_status(L"Ответа ещё нет");
}

/* поле ввода: Enter — отправить, Shift+Enter — новая строка; Ctrl+A, Esc */
static LRESULT CALLBACK AcEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (msg == WM_KEYDOWN && wParam == VK_RETURN && !shift) {
    if (hwnd == g_acIn) PostMessageW(g_acWnd, WM_AC_SEND, 0, 0);
    return 0;
  }
  if (msg == WM_CHAR && wParam == L'\r' && !shift) return 0;
  if (msg == WM_KEYDOWN && wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) && hwnd == g_acIn &&
      IsClipboardFormatAvailable(CF_BITMAP) && !IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    ac_paste_img();
    return 0;
  }
  if (msg == WM_CHAR && wParam == 1) {
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    ShowWindow(g_acWnd, SW_HIDE);
    return 0;
  }
  return CallWindowProcW(g_acEditOld, hwnd, msg, wParam, lParam);
}

static void ac_layout(int W, int H) {
  int pad = 12, gap = 8, btnH = 30, inH = 72, statH = 20;
  int btnY = H - pad - btnH;
  int inY = btnY - gap - inH;
  int thumbH = g_acImg ? 84 : 0;
  int cmdY = inY - 4 - 22 - thumbH;
  int stY = cmdY - 4 - statH;
  int logY = 46;
  int logH = stY - 6 - logY;
  if (logH < 40) logH = 40;
  MoveWindow(g_acTitle, pad, 10, W - 2 * pad - 70, 24, TRUE);
  MoveWindow(GetDlgItem(g_acWnd, ID_PANEL_MIN), W - pad - 66, 8, 30, 26, TRUE);
  MoveWindow(GetDlgItem(g_acWnd, ID_PANEL_CLOSE), W - pad - 30, 8, 30, 26, TRUE);
  MoveWindow(g_acLog, pad, logY, W - 2 * pad, logH, TRUE);
  MoveWindow(g_acStat, pad, stY, W - 2 * pad, statH, TRUE);
  MoveWindow(g_acCmdChk, pad, cmdY, W - 2 * pad - 100, 22, TRUE);
  MoveWindow(g_acShot, W - pad - 96, cmdY, 96, 22, TRUE);
  MoveWindow(g_acThumb, pad, inY - 84, 80, 80, TRUE);
  ShowWindow(g_acThumb, g_acImg ? SW_SHOW : SW_HIDE);
  MoveWindow(g_acIn, pad, inY, W - 2 * pad, inH, TRUE);
  HWND row[5] = {g_acSend, g_acStop, g_acClear, g_acCopy, g_acSet};
  int bw = (W - 2 * pad - 4 * gap) / 5;
  for (int i = 0; i < 5; i++) MoveWindow(row[i], pad + i * (bw + gap), btnY, bw, btnH, TRUE);
}

static LRESULT CALLBACK AcProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_SIZE:
    ac_layout(LOWORD(lParam), HIWORD(lParam));
    return 0;
  case WM_DROPFILES: {
    wchar_t p[MAX_PATH] = L"";
    HDROP hd = (HDROP)wParam;
    if (DragQueryFileW(hd, 0, p, MAX_PATH)) ac_from_file(p);
    DragFinish(hd);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_AC_THUMB && HIWORD(wParam) == STN_CLICKED) {
      ac_drop_img();
      return 0;
    }
    if (HIWORD(wParam) != BN_CLICKED) break;
    switch (LOWORD(wParam)) {
    case ID_AC_SHOT:
      ac_shot();
      break;
    case ID_PANEL_CLOSE:
      ShowWindow(hwnd, SW_HIDE);
      break;
    case ID_PANEL_MIN:
      ShowWindow(hwnd, SW_MINIMIZE);
      break;
    case ID_AC_SEND:
      if (g_acPlanOn) ac_files_apply();
      else ac_send();
      break;
    case ID_AC_STOP:
      if (g_acPlanOn) ac_files_cancel();
      else if (g_acId) ai_cancel(g_acId);
      break;
    case ID_AC_CLEAR:
      ac_clear();
      break;
    case ID_AC_COPY:
      ac_copy_answer();
      break;
    case ID_AC_SET:
      ac_open_settings();
      break;
    }
    return 0;
  case WM_AC_SEND:
    ac_send();
    return 0;
  case WM_AI_CHAT:
    ac_result((AiResult *)lParam);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ac_create(void) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = AcProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = L"CursorPadAiChat";
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  RegisterClassExW(&wc);
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int w = 600, h = 640;
  if (w > wa.right - wa.left - 40) w = wa.right - wa.left - 40;
  if (h > wa.bottom - wa.top - 40) h = wa.bottom - wa.top - 40;
  g_acWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CursorPadAiChat", L"Спросить ИИ — CursorPad",
                            WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX,
                            wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                            NULL, NULL, g_inst, NULL);
  if (!g_acWnd) return;
  round_corners(g_acWnd);
  mk_btn(g_acWnd, L"×", ID_PANEL_CLOSE);
  mk_btn(g_acWnd, L"–", ID_PANEL_MIN);
  g_acTitle = CreateWindowExW(0, L"STATIC", L"Спросить ИИ · всё на этом компьютере, без интернета", WS_CHILD | WS_VISIBLE | SS_LEFT,
                              0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_TITLE, g_inst, NULL);
  g_acLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                            0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_LOG, g_inst, NULL);
  g_acIn = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                           0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_IN, g_inst, NULL);
  g_acStat = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, g_acWnd,
                             (HMENU)(INT_PTR)ID_AC_STAT, g_inst, NULL);
  g_acSend = mk_btn(g_acWnd, L"Отправить", ID_AC_SEND);
  g_acStop = mk_btn(g_acWnd, L"Стоп", ID_AC_STOP);
  g_acClear = mk_btn(g_acWnd, L"Очистить", ID_AC_CLEAR);
  g_acCopy = mk_btn(g_acWnd, L"Копировать", ID_AC_COPY);
  g_acSet = mk_btn(g_acWnd, L"Настройки", ID_AC_SET);
  g_acCmdChk = CreateWindowExW(0, L"BUTTON", L"Команда для файлов: разложить папку по штампам", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                               0, 0, 10, 10, g_acWnd, (HMENU)(INT_PTR)ID_AC_CMD, g_inst, NULL);
  g_acShot = mk_btn(g_acWnd, L"С экрана", ID_AC_SHOT);
  g_acThumb = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_BITMAP | SS_NOTIFY, 0, 0, 10, 10, g_acWnd,
                              (HMENU)(INT_PTR)ID_AC_THUMB, g_inst, NULL);
  DragAcceptFiles(g_acWnd, TRUE);
  for (HWND c = GetWindow(g_acWnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    if (c != g_acSend && c != g_acStop && c != g_acClear && c != g_acCopy && c != g_acSet && c != g_acCmdChk && c != g_acShot) SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_acLog, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  SendMessageW(g_acIn, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  g_acEditOld = (WNDPROC)SetWindowLongPtrW(g_acIn, GWLP_WNDPROC, (LONG_PTR)AcEditProc);
  SetWindowLongPtrW(g_acLog, GWLP_WNDPROC, (LONG_PTR)AcEditProc);
  RECT rc;
  GetClientRect(g_acWnd, &rc);
  ac_layout(rc.right, rc.bottom);
}

/* пункт «Спросить ИИ» в «Ещё» */
static void aichat_show(void) {
  if (!g_acWnd) ac_create();
  if (!g_acWnd) return;
  ShowWindow(g_acWnd, IsIconic(g_acWnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g_acWnd);
  SetFocus(g_acIn);
  if (!g_acId) {
    if (!ai_ready()) ac_status(ai_status_text(g_aiOn ? AI_NO_MODEL : AI_OFF));
    else ac_status(L"Enter — отправить, Shift+Enter — новая строка. Модель помнит эту беседу.");
  }
}

/* поиск с вопросом (мини-ИИ): открыть чат и сразу спросить */
static void aichat_ask(const wchar_t *q) {
  aichat_show();
  if (!g_acWnd) return;
  SendMessageW(g_acCmdChk, BM_SETCHECK, BST_UNCHECKED, 0); /* вопрос из поиска — не команда */
  SetWindowTextW(g_acIn, q);
  ac_send();
}

#include "ai_files.c" /* команды для файлов: копировать, переместить, переименовать, создать папку */
