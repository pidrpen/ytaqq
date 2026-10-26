/* ---- «Ссылка с файлом»: кольцо Shift и буфер обмена (с 2026.10.26) ---------------

   Shift-перетаскивание одного файла (convert.c) → плитка «Ссылка»: файл
   любого формата, до 10 МБ, сжимается и кладётся в буфер обмена текстом
   «cursorpad:f1.…» (упаковка и проверка — file_link.c). Ссылку вставляют в
   письмо или сообщение.

   У получателя CursorPad замечает скопированную ссылку и показывает
   всплывашку у часов: щелчок — «Сохранить как» (в «Загрузки»), сохранённый
   файл выделяется в проводнике. То же — «Ещё → Сохранить файл из ссылки».
   Файлу ставится отметка «получен извне» (Zone.Identifier), как вложению из
   почты: Word откроет его в защищённом просмотре, exe Windows проверит
   перед запуском. Сам файл программа не открывает. */
#include "file_link.c"

#define FLK_TITLE L"CursorPad — ссылка с файлом"

/* «512 Б», «12 КБ», «1,2 МБ» */
static void flk_size_text(unsigned long long b, wchar_t *out, int cap) {
  if (b < 1024) _snwprintf(out, cap, L"%u Б", (unsigned)b);
  else if (b < 1024 * 1024) _snwprintf(out, cap, L"%u КБ", (unsigned)((b + 1023) / 1024));
  else {
    unsigned t = (unsigned)((b * 10 + 512 * 1024) / (1024 * 1024));
    _snwprintf(out, cap, L"%u,%u МБ", t / 10, t % 10);
  }
  out[cap - 1] = 0;
}

/* «1 634 210» */
static void flk_count_text(size_t n, wchar_t *out, int cap) {
  wchar_t d[32];
  int k = _snwprintf(d, 32, L"%lu", (unsigned long)n), o = 0;
  for (int i = 0; i < k && o < cap - 2; i++) {
    if (i && (k - i) % 3 == 0) out[o++] = L' ';
    out[o++] = d[i];
  }
  out[o] = 0;
}

/* имя для сообщения: длинное — с многоточием */
static void flk_short_name(const wchar_t *name, wchar_t *out, int cap) {
  if ((int)wcslen(name) < cap && wcslen(name) <= 60) lstrcpynW(out, name, cap);
  else _snwprintf(out, cap, L"%.40ls…%ls", name, name + wcslen(name) - 15);
  out[cap - 1] = 0;
}

/* «cursorpad:f1.» в начале длинного текста (регистр не важен); -1 — нет */
static int flk_findw(const wchar_t *s, size_t n) {
  for (size_t i = 0; i + FLK_PREFIX_N <= n; i++) {
    size_t k = 0;
    while (k < FLK_PREFIX_N && s[i + k] < 0x80 && ((char)s[i + k] | (FLK_PREFIX[k] >= 'a' ? 0x20 : 0)) == FLK_PREFIX[k]) k++;
    if (k == FLK_PREFIX_N) return (int)i;
  }
  return -1;
}

static BOOL flk_clip_is_link(const wchar_t *s) { return flk_findw(s, wcslen(s)) >= 0; }

/* текст буфера обмена — в байты для file_link.c: ASCII как есть, прочее — 0x80
   (такой знак ссылка пропускает). scan — где искать начало ссылки: у каждого
   копирования смотрим только начало, чтобы не гонять мегабайты чужого текста */
static char *flk_clip_bytes(size_t scan, size_t *n) {
  *n = 0;
  if (!OpenClipboard(g_hwnd)) return NULL;
  char *out = NULL;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  const wchar_t *p = h ? (const wchar_t *)GlobalLock(h) : NULL;
  if (p) {
    size_t cap = GlobalSize(h) / sizeof(wchar_t), len = 0;
    while (len < cap && p[len]) len++;
    if (flk_findw(p, len < scan ? len : scan) >= 0 && (out = (char *)malloc(len + 1)) != NULL) {
      for (size_t i = 0; i < len; i++) out[i] = p[i] < 0x80 ? (char)p[i] : (char)0x80;
      out[len] = 0;
      *n = len;
    }
    GlobalUnlock(h);
  }
  CloseClipboard();
  return out;
}

static void flk_name_w(const FlkInfo *in, wchar_t *out, int cap) {
  if (!in->name[0] || !MultiByteToWideChar(CP_UTF8, 0, in->name, -1, out, cap)) lstrcpynW(out, L"файл", cap);
}

/* почему ссылка не открылась — словами */
static void flk_problem(int st, const FlkInfo *in, wchar_t *out, int cap) {
  wchar_t nm[300], sn[80], got[32], need[32];
  flk_name_w(in, nm, 300);
  flk_short_name(nm, sn, 80);
  flk_count_text(in->chars, got, 32);
  flk_count_text(in->need, need, 32);
  if (st == FLK_NONE)
    lstrcpynW(out, L"В буфере обмена нет ссылки с файлом. Скопируйте её целиком — она начинается с «cursorpad:f1.».", cap);
  else if (st == FLK_CUT && in->need)
    _snwprintf(out, cap,
               L"Ссылка на «%ls» обрезана: пришло %ls из %ls знаков.\n\nСкопируйте сообщение целиком. Если мессенджер "
               L"разбил ссылку на части или обрезал её, попросите прислать её письмом.",
               in->name[0] ? sn : L"файл", got, need);
  else if (st == FLK_CUT)
    lstrcpynW(out, L"От ссылки с файлом пришло только начало. Скопируйте сообщение целиком.", cap);
  else if (st == FLK_MEM)
    lstrcpynW(out, L"Не хватило памяти, чтобы достать файл из ссылки.", cap);
  else
    lstrcpynW(out, L"Ссылка повреждена: файл из неё не сходится с исходным. Попросите прислать ссылку заново.", cap);
  out[cap - 1] = 0;
}

/* плитка «Ссылка» в кольце Shift: файл → ссылка в буфере обмена */
static void flk_send(const wchar_t *path) {
  const wchar_t *slash = wcsrchr(path, L'\\');
  const wchar_t *name = slash ? slash + 1 : path;
  wchar_t sn[80], sz[32], msg[700];
  flk_short_name(name, sn, 80);
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    _snwprintf(msg, 700, L"Файл «%ls» не открылся.", sn);
    msg[699] = 0;
    MessageBoxW(g_hwnd, msg, FLK_TITLE, MB_OK | MB_ICONINFORMATION);
    return;
  }
  unsigned long long size = (unsigned long long)fa.nFileSizeHigh << 32 | fa.nFileSizeLow;
  flk_size_text(size, sz, 32);
  if (size > FLK_MAX) {
    _snwprintf(msg, 700,
               L"«%ls» — %ls, а в ссылку помещается файл до 10 МБ: ссылку длиннее не примут ни письмо, ни "
               L"мессенджер.\n\nТакой файл лучше передать через общую папку.",
               sn, sz);
    msg[699] = 0;
    MessageBoxW(g_hwnd, msg, FLK_TITLE, MB_OK | MB_ICONINFORMATION);
    return;
  }
  HCURSOR was = SetCursor(LoadCursorW(NULL, IDC_WAIT));
  unsigned char *data = (unsigned char *)malloc(size ? (size_t)size : 1);
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
  size_t done = 0;
  if (data && f != INVALID_HANDLE_VALUE) {
    DWORD got = 0;
    while (done < size && ReadFile(f, data + done, (DWORD)(size - done), &got, NULL) && got) done += got;
  }
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  char name8[1024];
  if (!WideCharToMultiByte(CP_UTF8, 0, name, -1, name8, (int)sizeof(name8), NULL, NULL)) strcpy(name8, "файл");
  size_t chars = 0;
  char *link = data && done == size ? flk_make(data, (size_t)size, name8, &chars) : NULL;
  free(data);
  wchar_t *w = link ? (wchar_t *)malloc((chars + 1) * sizeof(wchar_t)) : NULL;
  if (w)
    for (size_t i = 0; i <= chars; i++) w[i] = (unsigned char)link[i];
  BOOL ok = w && clipboard_set(w);
  free(w);
  free(link);
  SetCursor(was);
  if (!ok) {
    _snwprintf(msg, 700, done == size ? L"Не удалось положить ссылку на «%ls» в буфер обмена: не хватило памяти?"
                                      : L"Файл «%ls» не прочитался — может, он открыт в другой программе с запретом чтения.",
               sn);
    msg[699] = 0;
    MessageBoxW(g_hwnd, msg, FLK_TITLE, MB_OK | MB_ICONINFORMATION);
    return;
  }
  wchar_t cnt[32];
  flk_count_text(chars, cnt, 32);
  ai_log(L"Ссылка с файлом: %ls, %llu байт → %lu знаков", path, size, (unsigned long)chars);
  _snwprintf(msg, 700, L"Ссылка с файлом в буфере: %ls знаков", cnt);
  msg[699] = 0;
  show_status(msg);
  /* в мессенджере сообщение обычно короче нескольких тысяч знаков — сказать заранее */
  _snwprintf(msg, 700, L"«%ls», %ls → %ls знаков. Вставьте её в письмо или сообщение (Ctrl+V).%ls", sn, sz, cnt,
             chars > 4000 ? L" Длинную ссылку мессенджер может обрезать — тогда письмом." : L"");
  msg[699] = 0;
  upd_balloon(9, L"Ссылка с файлом скопирована", msg);
}

/* «Загрузки», если есть, иначе рабочий стол */
static void flk_save_dir(wchar_t *dir) {
  const GUID downloads = {0x374de290, 0x123f, 0x4565, {0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b}};
  PWSTR p = NULL;
  dir[0] = 0;
  if (SUCCEEDED(SHGetKnownFolderPath(&downloads, 0, NULL, &p))) {
    lstrcpynW(dir, p, MAX_PATH);
    CoTaskMemFree(p);
  }
  if (!dir[0] || GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES)
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, dir);
}

/* файл из ссылки в буфере обмена — «Сохранить как», потом показать в проводнике */
static void flk_receive(void) {
  size_t n = 0;
  char *t = flk_clip_bytes((size_t)-1, &n);
  FlkInfo in;
  unsigned char *data = NULL;
  memset(&in, 0, sizeof(in));
  HCURSOR was = SetCursor(LoadCursorW(NULL, IDC_WAIT));
  int st = t ? flk_open(t, n, &in, &data) : FLK_NONE;
  SetCursor(was);
  free(t);
  if (st != FLK_OK) {
    wchar_t msg[700];
    flk_problem(st, &in, msg, 700);
    MessageBoxW(g_hwnd, msg, FLK_TITLE, MB_OK | MB_ICONINFORMATION);
    return;
  }
  char safe[256];
  lstrcpynA(safe, in.name, 256);
  flk_safe_name(safe);
  wchar_t file[MAX_PATH], dir[MAX_PATH];
  if (!MultiByteToWideChar(CP_UTF8, 0, safe, -1, file, MAX_PATH)) lstrcpynW(file, L"файл", MAX_PATH);
  flk_save_dir(dir);
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_hwnd;
  of.lpstrFilter = L"Все файлы\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrInitialDir = dir;
  of.lpstrTitle = L"Сохранить файл из ссылки";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
  SetForegroundWindow(g_hwnd); /* щелчок по всплывашке — окно сохранения поверх, а не за проводником */
  if (!GetSaveFileNameW(&of)) {
    free(data);
    return;
  }
  HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  DWORD put = 0;
  BOOL ok = f != INVALID_HANDLE_VALUE && (!in.size || (WriteFile(f, data, in.size, &put, NULL) && put == in.size));
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  free(data);
  if (!ok) {
    if (f != INVALID_HANDLE_VALUE) DeleteFileW(file); /* недописанный не оставляем */
    wchar_t msg[MAX_PATH + 120];
    _snwprintf(msg, MAX_PATH + 120, L"Не удалось записать файл:\n%ls\n\nВыберите другую папку.", file);
    msg[MAX_PATH + 119] = 0;
    MessageBoxW(g_hwnd, msg, FLK_TITLE, MB_OK | MB_ICONINFORMATION);
    return;
  }
  /* отметка «получен извне» — как у вложения из почты; на флешке без NTFS не встанет, и ладно */
  wchar_t ads[MAX_PATH + 24];
  _snwprintf(ads, MAX_PATH + 24, L"%ls:Zone.Identifier", file);
  ads[MAX_PATH + 23] = 0;
  HANDLE z = CreateFileW(ads, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (z != INVALID_HANDLE_VALUE) {
    static const char zone[] = "[ZoneTransfer]\r\nZoneId=3\r\n";
    DWORD w = 0;
    WriteFile(z, zone, sizeof(zone) - 1, &w, NULL);
    CloseHandle(z);
  }
  ai_log(L"Ссылка с файлом: сохранён %ls, %lu байт", file, (unsigned long)in.size);
  show_in_explorer(file);
  show_status(L"Файл из ссылки сохранён");
}

/* скопировали чужую ссылку — всплывашка «сохранить» (своя — g_ownClip, не сюда) */
static void flk_on_copy(void) {
  size_t n = 0;
  char *t = flk_clip_bytes(4096, &n);
  if (!t) return;
  FlkInfo in;
  int st = flk_peek(t, n, &in);
  free(t);
  if (st != FLK_OK && st != FLK_CUT) return; /* не похоже на ссылку — молчим */
  wchar_t nm[300], sn[80], sz[32], msg[300];
  flk_name_w(&in, nm, 300);
  flk_short_name(nm, sn, 80);
  flk_size_text(in.size, sz, 32);
  if (st == FLK_OK) {
    _snwprintf(msg, 300, L"«%ls», %ls. Щёлкните здесь, чтобы сохранить.", sn, sz);
    msg[299] = 0;
    show_status(L"В буфере ссылка с файлом — «Ещё → Сохранить файл из ссылки»");
    upd_balloon(10, L"Скопирована ссылка с файлом", msg);
  } else {
    flk_problem(st, &in, msg, 300);
    upd_balloon(4, L"Ссылка с файлом обрезана", msg);
  }
}

#ifdef SET_TEST
/* --linktest файл отчёт: ссылка через настоящий буфер обмена туда и обратно,
   всплывашка «сохранить» и пропуск в истории копий; --linksave — то же с «Сохранить как» */
static void flk_test(const wchar_t *path, const wchar_t *report, BOOL save) {
  flk_send(path);
  size_t n = 0, fn = 0;
  char *t = flk_clip_bytes((size_t)-1, &n), *file = NULL;
  FlkInfo in;
  unsigned char *data = NULL;
  memset(&in, 0, sizeof(in));
  int st = t ? flk_open(t, n, &in, &data) : FLK_NONE;
  DWORD fz = 0;
  BOOL same = read_file_bytes(path, &file, &fz, FLK_MAX + 1) && (fn = fz) == in.size && data && !memcmp(file, data, fn);
  wchar_t head[400] = {0};
  clipboard_text(head, 400);
  g_balloonKind = 0;
  flk_on_copy();
  int kind = g_balloonKind;
  if (save) flk_receive();
  char rep[1200];
  int k = snprintf(rep, sizeof(rep), "st=%d chars=%lu size=%lu packed=%d name=%s same=%d balloon=%d tray=%d histskip=%d\r\n", st,
                   (unsigned long)n, (unsigned long)in.size, in.packed, in.name, same, kind, g_trayAdded, flk_clip_is_link(head));
  write_all(report, rep, (DWORD)k);
  free(t);
  free(file);
  free(data);
}
#endif
