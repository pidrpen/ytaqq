/* ---- «Ссылка» в кольце Shift: ссылка, открывающая файл, или путь до него (с 2026.10.26) ----

   Shift-перетаскивание файлов (convert.c) → плитка «Ссылка»; задержите на ней
   мышь — кольцо сменится двумя полукругами:
     «Включая файл» — ссылка, щелчок по которой сразу открывает файл: в буфер
       «HTML Format» — <a href="file:///…">имя файла</a> (Outlook, Word, чаты
       в браузере вставят её кликабельной) и тот же file:///… текстом;
     «До файла» — только путь текстом, по одному на строку.
   Отпустить прямо на «Ссылке» — то же, что «Включая файл».
   Файл на подключённом сетевом диске (Z:\…) — путь сетевой, \\сервер\папка\…:
   у коллеги буква диска может быть другой. Переводит mpr.dll
   (WNetGetUniversalNameW), она загружается только здесь.
   В 2026.10.26.5 плитка зашивала содержимое файла в текст на тысячи знаков —
   убрано: просили путь до файла, а «сам файл» — это ссылка, которая его открывает. */
#include <winnetwk.h>

/* Z:\… на подключённом сетевом диске → \\сервер\папка\…; иначе как есть */
static void flk_net_path(const wchar_t *path, wchar_t *out, int cap) {
  lstrcpynW(out, path, cap);
  if (!path[0] || path[1] != L':') return;
  wchar_t root[4] = {path[0], L':', L'\\', 0};
  if (GetDriveTypeW(root) != DRIVE_REMOTE) return;
  typedef DWORD(WINAPI * UniversalFn)(LPCWSTR, DWORD, LPVOID, LPDWORD);
  static UniversalFn fn;
  static BOOL tried;
  if (!tried) {
    tried = TRUE;
    HMODULE mpr = LoadLibraryW(L"mpr.dll");
    if (mpr) fn = (UniversalFn)GetProcAddress(mpr, "WNetGetUniversalNameW");
  }
  if (!fn) return;
  union {
    UNIVERSAL_NAME_INFOW info;
    BYTE raw[sizeof(UNIVERSAL_NAME_INFOW) + 2048 * sizeof(wchar_t)];
  } buf;
  DWORD size = sizeof(buf);
  if (fn(path, UNIVERSAL_NAME_INFO_LEVEL, &buf, &size) == NO_ERROR && buf.info.lpUniversalName &&
      buf.info.lpUniversalName[0])
    lstrcpynW(out, buf.info.lpUniversalName, cap);
}

static HGLOBAL flk_global(const void *p, size_t n) {
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n);
  void *at = h ? GlobalLock(h) : NULL;
  if (!at) {
    if (h) GlobalFree(h);
    return NULL;
  }
  if (p) memcpy(at, p, n);
  GlobalUnlock(h);
  return h;
}

/* путь → file:///C:/…, \\сервер\папка\… → file://сервер/папка/…; кириллица как есть
   (%XX Windows прочла бы в кодировке ANSI), кодируются пробел и знаки, на которых рвётся ссылка */
static void flk_url(const wchar_t *path, wchar_t *out, int cap) {
  const wchar_t *pre = L"file:///", *p = path;
  if (p[0] == L'\\' && p[1] == L'\\') pre = L"file://", p += 2;
  int n = 0;
  for (; *pre && n < cap - 1; pre++) out[n++] = *pre;
  for (; *p && n < cap - 4; p++) {
    wchar_t c = *p == L'\\' ? L'/' : *p;
    if (wcschr(L" %#?`{}^[]", c)) n += _snwprintf(out + n, cap - n, L"%%%02X", (unsigned)c);
    else out[n++] = c;
  }
  out[n] = 0;
}

/* дописать текст в HTML: &<>" — сущностями */
static size_t flk_html_put(wchar_t *d, const wchar_t *s) {
  size_t n = 0;
  for (; *s; s++) {
    const wchar_t *e = *s == L'&' ? L"&amp;" : *s == L'<' ? L"&lt;" : *s == L'>' ? L"&gt;" : *s == L'"' ? L"&quot;" : NULL;
    if (e) wcscpy(d + n, e), n += wcslen(e);
    else d[n++] = *s;
  }
  d[n] = 0;
  return n;
}

/* «HTML Format»: заголовок со смещениями в байтах UTF-8, фрагмент между пометками */
static HGLOBAL flk_html(const wchar_t *frag) {
  static const char head[] = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\nStartFragment:%010d\r\nEndFragment:%010d\r\n",
                    pre[] = "<html><body>\r\n<!--StartFragment-->", post[] = "<!--EndFragment-->\r\n</body></html>";
  int fl = WideCharToMultiByte(CP_UTF8, 0, frag, -1, NULL, 0, NULL, NULL); /* с нулём */
  if (fl <= 0) return NULL;
  int hl = snprintf(NULL, 0, head, 0, 0, 0, 0), sf = hl + (int)strlen(pre), ef = sf + fl - 1, eh = ef + (int)strlen(post);
  char *buf = (char *)malloc((size_t)eh + 1);
  if (!buf) return NULL;
  snprintf(buf, (size_t)hl + 1, head, hl, eh, sf, ef);
  memcpy(buf + hl, pre, strlen(pre));
  WideCharToMultiByte(CP_UTF8, 0, frag, -1, buf + sf, fl, NULL, NULL);
  memcpy(buf + ef, post, strlen(post) + 1);
  HGLOBAL h = flk_global(buf, (size_t)eh + 1);
  free(buf);
  return h;
}

/* «Ссылка»: asLink — ссылки, открывающие файлы («Включая файл»), иначе пути («До файла») */
static void flk_send(const CvSelection *s, BOOL asLink) {
  if (!s || s->count < 1) return;
  enum { URL_CAP = 3100, FRAG_PER = URL_CAP * 6 + MAX_PATH * 6 + 40 };
  wchar_t *text = (wchar_t *)calloc((size_t)s->count * (URL_CAP + 2) + 1, sizeof(wchar_t));
  wchar_t *frag = asLink ? (wchar_t *)calloc((size_t)s->count * FRAG_PER + 1, sizeof(wchar_t)) : NULL;
  if (!text || (asLink && !frag)) {
    free(text);
    free(frag);
    return;
  }
  BOOL local = FALSE;
  wchar_t first[1024] = L"";
  size_t tl = 0, fl = 0;
  for (int i = 0; i < s->count; i++) {
    wchar_t net[1024], url[URL_CAP];
    flk_net_path(s->paths[i], net, 1024);
    if (!i) lstrcpynW(first, net, 1024);
    if (net[0] != L'\\') local = TRUE; /* по такому пути у коллеги файл не откроется */
    if (asLink) flk_url(net, url, URL_CAP);
    if (i) text[tl++] = L'\r', text[tl++] = L'\n';
    wcscpy(text + tl, asLink ? url : net);
    tl += wcslen(text + tl);
    if (asLink) {
      const wchar_t *name = wcsrchr(net, L'\\');
      name = name ? name + 1 : net;
      if (i) wcscpy(frag + fl, L"<br>\r\n"), fl += 6;
      wcscpy(frag + fl, L"<a href=\""), fl += 9;
      fl += flk_html_put(frag + fl, url);
      wcscpy(frag + fl, L"\">"), fl += 2;
      fl += flk_html_put(frag + fl, name);
      wcscpy(frag + fl, L"</a>"), fl += 4;
    }
  }
  HGLOBAL hText = flk_global(text, (tl + 1) * sizeof(wchar_t)), hHtml = asLink ? flk_html(frag) : NULL;
  free(text);
  free(frag);
  BOOL ok = hText && (!asLink || hHtml) && OpenClipboard(g_hwnd);
  if (ok) {
    EmptyClipboard();
    g_ownClip = TRUE;
    /* что взял буфер — его; не взял — освобождаем сами */
    if (SetClipboardData(CF_UNICODETEXT, hText)) hText = NULL;
    else ok = FALSE;
    if (asLink && SetClipboardData(RegisterClipboardFormatW(L"HTML Format"), hHtml)) hHtml = NULL;
    CloseClipboard();
  }
  if (hText) GlobalFree(hText);
  if (hHtml) GlobalFree(hHtml);
  if (!ok) {
    show_status(L"Буфер обмена занят — попробуйте ещё раз");
    return;
  }
  ai_log(L"%ls: %d шт., %ls", asLink ? L"Ссылка на файл" : L"Путь до файла", s->count, first);
  wchar_t shown[140], msg[300];
  if (wcslen(first) < 120) lstrcpynW(shown, first, 140);
  else _snwprintf(shown, 140, L"…%ls", first + wcslen(first) - 110);
  shown[139] = 0;
  if (s->count > 1) _snwprintf(msg, 300, L"Файлов: %d, первый — %ls", s->count, shown);
  else lstrcpynW(msg, shown, 300);
  size_t m = wcslen(msg);
  if (asLink)
    _snwprintf(msg + m, 300 - m, L"\nВставьте в письмо или чат — щелчок по ссылке откроет файл.%ls",
               local ? L" Файл на этом ПК: у коллеги не откроется, нужна сетевая папка." : L"");
  else
    _snwprintf(msg + m, 300 - m, L"\nCtrl+V вставит путь.%ls",
               local ? L" Файл на этом ПК: коллега по такому пути его не откроет." : L"");
  msg[299] = 0;
  if (asLink) show_status(s->count > 1 ? L"В буфере ссылки на файлы" : L"В буфере ссылка на файл");
  else show_status(s->count > 1 ? L"В буфере пути до файлов" : L"В буфере путь до файла");
  upd_balloon(9, asLink ? L"Ссылка на файл скопирована" : L"Путь до файла скопирован", msg);
}

#ifdef SET_TEST
/* --linktest / --pathtest отчёт файл [файл…]: что после «Включая файл» / «До файла» лежит
   в буфере обмена; строки ссылок переводятся обратно в путь (PathCreateFromUrlW) */
static void flk_test(int argc, LPWSTR *argv) {
  static CvSelection s;
  s.count = 0;
  for (int i = 3; i < argc && s.count < CV_FILES; i++) GetFullPathNameW(argv[i], MAX_PATH, s.paths[s.count++], NULL);
  flk_send(&s, !wcscmp(argv[1], L"--linktest"));
  typedef HRESULT(WINAPI * FromUrlFn)(LPCWSTR, LPWSTR, DWORD *, DWORD);
  HMODULE shl = LoadLibraryW(L"shlwapi.dll");
  FromUrlFn fromUrl = shl ? (FromUrlFn)GetProcAddress(shl, "PathCreateFromUrlW") : NULL;
  wchar_t *rep = (wchar_t *)calloc(40000, sizeof(wchar_t));
  int n = 0;
  if (rep && OpenClipboard(g_hwnd)) {
    n += _snwprintf(rep + n, 40000 - n, L"hdrop=%d\r\n", IsClipboardFormatAvailable(CF_HDROP));
    HANDLE ht = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *t = ht ? (const wchar_t *)GlobalLock(ht) : NULL;
    n += _snwprintf(rep + n, 40000 - n, L"text=[%ls]\r\n", t ? t : L"(нет)");
    for (const wchar_t *line = t; line && *line && fromUrl;) {
      const wchar_t *end = wcschr(line, L'\r');
      size_t len = end ? (size_t)(end - line) : wcslen(line);
      wchar_t url[3200], back[MAX_PATH] = L"";
      lstrcpynW(url, line, (int)(len < 3199 ? len + 1 : 3200));
      DWORD bn = MAX_PATH;
      if (!wcsncmp(url, L"file:", 5)) {
        HRESULT hr = fromUrl(url, back, &bn, 0);
        int same = 0;
        for (int k = 0; k < s.count; k++) same |= !lstrcmpiW(back, s.paths[k]);
        n += _snwprintf(rep + n, 40000 - n, L"back=%ls hr=%lx same=%d\r\n", back, (unsigned long)hr, same);
      }
      line = end ? end + 2 : NULL;
    }
    if (t) GlobalUnlock(ht);
    HANDLE hh = GetClipboardData(RegisterClipboardFormatW(L"HTML Format"));
    const char *h = hh ? (const char *)GlobalLock(hh) : NULL;
    if (h) {
      const char *sf = strstr(h, "StartFragment:"), *ef = strstr(h, "EndFragment:"), *sh = strstr(h, "StartHTML:"),
                 *eh = strstr(h, "EndHTML:");
      int a = sf ? atoi(sf + 14) : 0, b = ef ? atoi(ef + 12) : 0, c = sh ? atoi(sh + 10) : 0, d = eh ? atoi(eh + 8) : 0;
      int okOffsets = a > 0 && b >= a && (size_t)d == strlen(h) && !strncmp(h + c, "<html>", 6) &&
                      !strncmp(h + a - 20, "<!--StartFragment-->", 20) && !strncmp(h + b, "<!--EndFragment-->", 18);
      wchar_t frag[8000] = L"";
      MultiByteToWideChar(CP_UTF8, 0, h + a, b - a, frag, 7999);
      n += _snwprintf(rep + n, 40000 - n, L"html=[%ls] offsets=%d\r\n", frag, okOffsets);
      GlobalUnlock(hh);
    } else n += _snwprintf(rep + n, 40000 - n, L"html=(нет)\r\n");
    n += _snwprintf(rep + n, 40000 - n, L"own=%d\r\n", g_ownClip);
    CloseClipboard();
  }
  char *u = (char *)malloc(120000);
  int k = rep && u ? WideCharToMultiByte(CP_UTF8, 0, rep, -1, u, 120000, NULL, NULL) : 0;
  if (k > 0) write_all(argv[2], u, (DWORD)k - 1);
  free(u);
  free(rep);
}
#endif
