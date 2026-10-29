/* ---- Действия из чата для ЭСИ и «Союза» (с 2026.10.29) ---------------------------------------

   Не один большой сценарий, а отдельные действия, каждое — из чата по ключевым словам (как
   sz_chat_cmd, nm_want_full); модель не нужна, работу делает код. Короткий ответ в чат и
   строка-кнопки под ним (ссылки — тот же EN_LINK → WM_AC_LINK, что у маршрута).

     «загрузи ЭСИ по чертежам C:\…\папка» (или пути к файлам, в кавычках или через «;») —
         листы читает очередь окна «ЭСИ и ТП» (pk_stamp.c, окно создаётся скрытым): штамп,
         СБ — документом к сборке, спецификация — составом, детали — маршрут с операциями;
         участок и станок — по таблице приоритетов, Тпз/Тшт — norms.c, «Транспортирование» —
         при смене участка. По окончании — карточка: изделия, состав, операции, что не
         прочитано, и «Действия: В Союз · Проверить подробно · Отмена»;
     «В Союз» — описание отдаётся мостом в клиент с последней папкой (нет папки — окно папки),
         отчёт клиента — строкой в чат; «Проверить подробно» — окно «ЭСИ и ТП» с этими
         строками; «Отмена» — листы очищаются;
     «выгрузи МВ по ТСТ.100.000» / «маршрутная ведомость ТСТ.100.000» — окно ведомости, сбор
         сразу (route_ui.c);
     «приоритет участков», «поставь участки» — таблица «операция → участок» (soyuz_prio.c).
   Запасной путь без чата — те же окна и пункт «Загрузить ЭСИ в Союз» в меню «Ещё». */

static const wchar_t *const kAcEsiLinks[] = {L"Проверить подробно", L"Отмена"}; /* WM_AC_LINK 21, 22; «В Союз» — 10 */
#define AC_ESI_HEAD L"ЭСИ по чертежам"

static void esi_low(const wchar_t *a, wchar_t *low, int cap) {
  lstrcpynW(low, a, cap);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  for (wchar_t *c = low; *c; c++)
    if (*c == L'ё') *c = L'е';
}

/* «загрузи эси …», «эси в союз по чертежам», «заведи эси …» */
static BOOL esi_chat_cmd(const wchar_t *a) {
  wchar_t low[2000];
  esi_low(a, low, 2000);
  if (wcslen(low) > 1500) return FALSE;
  BOOL esi = wcsstr(low, L"эси") != NULL;
  BOOL verb = wcsstr(low, L"загруз") || wcsstr(low, L"завед") || wcsstr(low, L"созда") || wcsstr(low, L"в союз");
  BOOL draw = wcsstr(low, L"чертеж") != NULL;
  if (wcsstr(low, L"выгруз") || wcsstr(low, L"ведомост")) return FALSE;
  return (esi && verb) || (draw && wcsstr(low, L"в союз"));
}

/* пути из текста: «C:\…», в кавычках или до «;»/конца строки; папки раскрываются в pks_add_path */
static int esi_paths(const wchar_t *a, TmPaths *l) {
  int n = 0;
  for (const wchar_t *p = a; *p;) {
    if (!(((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) &&
        !(p[0] == L'\\' && p[1] == L'\\')) {
      p++;
      continue;
    }
    wchar_t q = p > a ? p[-1] : 0; /* открывающая кавычка — до парной */
    wchar_t close = q == L'«' ? L'»' : q == L'"' ? L'"' : q == L'\'' ? L'\'' : 0;
    const wchar_t *e = p;
    while (*e && *e != L'\r' && *e != L'\n' && *e != L';' && *e != L'\t' && (close ? *e != close : TRUE)) e++;
    wchar_t path[MAX_PATH];
    int len = (int)(e - p);
    if (len >= MAX_PATH) len = MAX_PATH - 1;
    memcpy(path, p, (size_t)len * sizeof(wchar_t));
    path[len] = 0;
    while (len && (path[len - 1] == L' ' || path[len - 1] == L'.' || path[len - 1] == L',' || path[len - 1] == L')')) path[--len] = 0;
    if (!close) { /* без кавычек: отрезаем хвост-слова после файла, если путь так не существует */
      while (len && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        wchar_t *sp = wcsrchr(path, L' ');
        if (!sp) break;
        *sp = 0;
        len = (int)wcslen(path);
      }
    }
    if (len && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
      pks_add_path(l, path);
      n++;
    }
    p = e > p ? e : p + 1;
  }
  return n;
}

/* ---- «загрузи ЭСИ по чертежам» ---- */
static void esi_start(const wchar_t *a) {
  TmPaths l = {0};
  esi_paths(a, &l);
  if (!l.n) {
    free(l.p);
    ac_push(1, L"Чтобы загрузить ЭСИ, укажите чертежи: путь к папке или к файлам (PDF, TIF, PNG), например\n"
                L"«загрузи ЭСИ по чертежам C:\\Чертежи\\Редуктор». Сборочный чертёж (…СБ) ляжет документом к сборке, "
                L"спецификация даст состав, по чертежам деталей составятся операции.");
    return;
  }
  pk_create();                /* окно «ЭСИ и ТП» — скрытое: туда ложатся строки */
  if (g_pkEditRow >= 0) pk_edit_end(FALSE);
  pk_stamp_forget();          /* недочитанное прежнее — мимо */
  xp_package_free(g_pk);      /* чистый лист: башка и так кипит */
  pk_init();
  pk_soy_clear();
  if (g_pkWnd) pk_fill(TRUE);
  pks_ops_pref();
  g_pkAiOps = TRUE;           /* операции сразу по чертежам деталей */
  g_esiChat = TRUE;
  int n = l.n;
  pks_enqueue(&l, -1, FALSE);
  wchar_t m[300];
  _snwprintf(m, 300, L"Читаю %d %ls%ls: штампы, спецификации и листы деталей. Как закончу — покажу здесь карточку с кнопками.", n,
             n == 1 ? L"лист" : n < 5 ? L"листа" : L"листов", dr_vl_ok() ? L" (мини-ИИ с картинками)" : L" (Tesseract)");
  m[299] = 0;
  ac_push(1, m);
  ac_status(L"ЭСИ: читаю чертежи…");
}

/* очередь чтения кончилась (pk_stamp_done, последний файл) — карточка в чат */
static void esi_card(void) {
  g_esiChat = FALSE;
  pk_check(TRUE); /* транспортирование, нормы, названия как в 1С */
  XpTable *it = &g_pk[PK_ITEMS], *ln = &g_pk[PK_LINKS], *op = &g_pk[PK_OPS], *bl = &g_pk[PK_BLANKS];
  int asm_ = 0, det = 0, doc = 0, noDes = 0, tr = 0, parts = 0;
  for (int r = 0; r < it->n; r++) {
    const wchar_t *k = xp_cell(it, r, 2);
    if (!xp_cell(it, r, 0)[0]) noDes++;
    if (wcsstr(k, L"борочн")) asm_++;
    else if (!wcscmp(k, L"Документация")) doc++;
    else if (!wcscmp(k, L"Детали")) det++;
  }
  for (int r = 0; r < op->n; r++) {
    if (pk_is_transport(xp_cell(op, r, 2))) tr++;
    int seen = 0;
    for (int q = 0; q < r && !seen; q++) seen = !_wcsicmp(xp_cell(op, q, 0), xp_cell(op, r, 0));
    if (!seen) parts++;
  }
  wchar_t t[1800];
  int l = _snwprintf(t, 1800,
                     AC_ESI_HEAD L" готово: изделий %d (сборок %d, деталей %d, документов %d), состав — %d строк, "
                     L"операций %d у %d деталей (транспортирований %d), заготовок %d.",
                     it->n, asm_, det, doc, ln->n, op->n, parts, tr, bl->n);
  if (g_pksNoDes || noDes)
    l += _snwprintf(t + l, 1800 - l, L"\nБез обозначения: %d — штамп не прочитался, впишите в «Проверить подробно».", noDes ? noDes : g_pksNoDes);
  if (g_pksDoubt[0]) l += _snwprintf(t + l, 1800 - l, L"\nПроверьте обозначения: %ls.", g_pksDoubt);
  if (g_pksOpsNo) l += _snwprintf(t + l, 1800 - l, L"\nБез маршрута: %d (лист не прочитался или это не чертёж детали).", g_pksOpsNo);
  if (!op->n && det) l += _snwprintf(t + l, 1800 - l, L"\nОпераций нет: листы деталей не дали маршрута.");
  if (l < 1800 - 120)
    _snwprintf(t + l, 1800 - l, L"\n\nДействия:   В Союз   ·   Проверить подробно   ·   Отмена");
  t[1799] = 0;
  ac_push(1, t);
  ac_status(L"ЭСИ готово — «В Союз», «Проверить подробно» или «Отмена» под ответом");
}

/* последний ответ с карточкой ЭСИ — позже последнего маршрута? тогда «В Союз» под ним — её */
static int ac_esi_turn(int from) {
  for (int i = g_acN - 1; i >= from && i >= 0; i--)
    if (g_acTurns[i].assistant && g_acTurns[i].text && !wcsncmp(g_acTurns[i].text, AC_ESI_HEAD, wcslen(AC_ESI_HEAD))) return i;
  return -1;
}
static BOOL ac_esi_last(void) { return ac_esi_turn(0) > ac_route_turn(0); }

/* слова карточки — ссылками (строка «Действия:» — последняя в ленте) */
static void ac_esi_links(void) {
  if (!ac_esi_last()) return;
  FINDTEXTEXW ft;
  memset(&ft, 0, sizeof(ft));
  ft.chrg.cpMin = GetWindowTextLengthW(g_acLog);
  ft.chrg.cpMax = 0;
  ft.lpstrText = L"Действия:";
  if (SendMessageW(g_acLog, EM_FINDTEXTEXW, 0, (LPARAM)&ft) < 0) return;
  LONG at = ft.chrgText.cpMax;
  static const wchar_t *const words[] = {L"В Союз", L"Проверить подробно", L"Отмена"};
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    memset(&ft, 0, sizeof(ft));
    ft.chrg.cpMin = at;
    ft.chrg.cpMax = at + 90;
    ft.lpstrText = words[i];
    if (SendMessageW(g_acLog, EM_FINDTEXTEXW, FR_DOWN | FR_MATCHCASE, (LPARAM)&ft) < 0) continue;
    SendMessageW(g_acLog, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_LINK;
    cf.dwEffects = CFE_LINK;
    SendMessageW(g_acLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
  }
}

/* «В Союз» под карточкой: описание из листов — мостом в клиент с последней папкой */
static void esi_send(void) {
  if (!g_pk[PK_ITEMS].n && !g_pk[PK_OPS].n && !g_pk[PK_LINKS].n) {
    ac_push(1, L"В «Союз» нечего отправлять: листы «ЭСИ и ТП» пусты.");
    return;
  }
  pk_check(TRUE);
  wchar_t *text = NULL, note[200];
  PkSent sent;
  int n = pk_soyuz_text(&text, note, 200, &sent);
  if (!text || !text[0]) {
    free(text);
    ac_push(1, L"В «Союз» нечего отправлять: нет ни изделий, ни операций с кодом справочника.");
    return;
  }
  SzmPrefs *pf = (SzmPrefs *)calloc(1, sizeof(SzmPrefs));
  wchar_t folder[SZM_PATH] = L"", tp[SZM_PATH] = L"";
  if (pf) {
    szm_pref_load(pf);
    lstrcpynW(folder, pf->cur[0], SZM_PATH);
    lstrcpynW(tp, pf->cur[1], SZM_PATH);
    free(pf);
  }
  if (wcsstr(text, L"ИЗДЕЛИЕ\t") && !folder[0]) { /* папки ещё не было — спросить в окне */
    wchar_t label[240];
    _snwprintf(label, 240, L"В «Союз»: изделий %d, состав %d, операций %d", sent.items, g_pk[PK_LINKS].n, n);
    label[239] = 0;
    szm_open_text(text, label);
    free(text);
    ac_push(1, L"Папку «Союза» для новых изделий ещё не выбирали — выберите в открывшемся окне (или вставьте ссылку "
                L"«копировать ссылку в PLM» на папку) и нажмите «Создать».");
    return;
  }
  wchar_t msg[500];
  BOOL ok = szm_send_text(text, folder, tp, g_acWnd, msg, 500);
  free(text);
  ac_push(1, msg);
  if (ok) ac_status(L"Передаю в «Союз»…");
}

/* ответ клиента (WM_SZM_DONE в окне чата): первая строка и ошибки — в чат */
static void esi_report(wchar_t *rep) {
  if (!rep) return;
  wchar_t out[1600];
  int l = 0;
  wchar_t *p = rep;
  int lines = 0, errs = 0;
  while (*p && l < 1500) {
    wchar_t *e = wcspbrk(p, L"\r\n");
    size_t len = e ? (size_t)(e - p) : wcslen(p);
    BOOL first = lines == 0, err = wcsstr(p, L"ОШИБКА") == p || (len > 3 && p[0] == L' ' && p[1] == L' ' && p[2] == L'!');
    if (len && (first || (err && errs < 6))) {
      if (!first) errs++;
      if (len > 300) len = 300;
      l += _snwprintf(out + l, 1600 - l, L"%s%.*s", l ? L"\n" : L"В «Союз»: ", (int)len, p);
    }
    if (len) lines++;
    if (!e) break;
    p = e;
    while (*p == L'\r' || *p == L'\n') p++;
  }
  if (!l) lstrcpynW(out, L"В «Союз»: клиент ничего не ответил.", 1600);
  else if (lines > 1) l += _snwprintf(out + l, 1600 - l, L"\n(строк отчёта %d — целиком в «Проверить подробно» → «В Союз…»)", lines);
  out[1599] = 0;
  ac_push(1, out);
  ac_status(L"«Союз» ответил");
  free(rep);
}

static void esi_detail(void) { package_show(FALSE); }

static void esi_cancel(void) {
  if (g_pkEditRow >= 0) pk_edit_end(FALSE);
  pk_stamp_forget();
  xp_package_free(g_pk);
  pk_init();
  pk_soy_clear();
  pk_draft_save();
  if (g_pkWnd) pk_fill(TRUE);
  ac_push(1, L"Отменено: листы «ЭСИ и ТП» очищены, в «Союз» ничего не ушло.");
}

/* ---- маршрутная ведомость и приоритеты участков — из чата ---- */

/* «выгрузи мв по ТСТ.100.000», «маршрутная ведомость ТСТ.100.000», «сделай ведомость на …» */
static BOOL rt_chat_cmd(const wchar_t *a) {
  wchar_t low[2000];
  esi_low(a, low, 2000);
  if (wcslen(low) > 400) return FALSE;
  BOOL mv = wcsstr(low, L"ведомост") != NULL || tn_word(low, L"мв");
  if (!mv || wcsstr(low, L"норм") || wcsstr(low, L"материал")) return FALSE;
  if (!(wcsstr(low, L"выгруз") || wcsstr(low, L"сдела") || wcsstr(low, L"собер") || wcsstr(low, L"маршрутн") ||
        wcsstr(low, L"постро") || wcsstr(low, L"открой")))
    return FALSE;
  /* обозначение — слово с цифрами и точкой/дефисом; иначе окно пустое */
  wchar_t des[200] = L"";
  static const wchar_t kSep[] = L" \t\r\n,\"()\u00ab\u00bb"; /* пробелы, запятые, кавычки и скобки */
  for (const wchar_t *p = a; *p && !des[0];) { /* слово с цифрами и точкой/дефисом — обозначение */
    while (*p && wcschr(kSep, *p)) p++;
    const wchar_t *e = p;
    while (*e && !wcschr(kSep, *e)) e++;
    if (e > p && e - p < 200) {
      wchar_t w[200];
      lstrcpynW(w, p, (int)(e - p) + 1);
      if (rt_looks_des(w)) lstrcpynW(des, w, 200);
    }
    p = e;
  }
  route_show();
  if (des[0]) {
    SetWindowTextW(g_rtDes, des);
    rt_start();
    wchar_t m[300];
    _snwprintf(m, 300, L"Маршрутная ведомость по %s собирается в своём окне: состав, заготовки, нормы и маршрут по участкам; "
                       L"потом «Сохранить в Excel…».",
               des);
    m[299] = 0;
    ac_push(1, m);
  } else
    ac_push(1, L"Окно маршрутной ведомости открыто — впишите обозначение сборки или детали и нажмите «Собрать».");
  return TRUE;
}

/* «приоритет участков», «поставь участки», «участки по умолчанию» */
static BOOL prio_chat_cmd(const wchar_t *a) {
  wchar_t low[2000];
  esi_low(a, low, 2000);
  if (wcslen(low) > 300) return FALSE;
  BOOL uch = wcsstr(low, L"участ") != NULL;
  if (!uch) return FALSE;
  if (!(wcsstr(low, L"приоритет") || wcsstr(low, L"постав") || wcsstr(low, L"по умолчан") || wcsstr(low, L"таблиц") ||
        wcsstr(low, L"мехобработ")))
    return FALSE;
  szp_edit(g_acWnd);
  ac_push(1, L"Таблица «операция → участок, станок» открыта: строка — код или название операции, затем пары участок и "
              L"станок по предпочтению; «МЕХОБРАБОТКА<Tab>12» — все операции резанием на участок 12. Сохраните — и окно "
              L"«ЭСИ и ТП» с «Создать ТП» будут брать её первой.");
  return TRUE;
}

/* ---- кликабельный чат (с 2026.10.29): обозначения изделий и пути — ссылками ----------------------
   После каждой отрисовки ленты (ac_text_links, последние AC_LINK_SCAN знаков): обозначение вида
   «АБВГ.123456.001», «ТСТ.100.000», «АДЕ 3424-1073.00.00.01» (ac_is_des: 2–4 буквы, точка или пробел,
   цифры, хотя бы одна точка; даты и «ГОСТ 2590-2006» — нет) и путь «C:\…» или «\\сервер\…» получают
   CFE_LINK. Щелчок (ac_text_link_click из ac_log_link): путь — открыть в Проводнике; обозначение —
   меню у курсора: «Есть ли в Союзе», «Куда входит», «Маршрутная ведомость», «Загрузить ЭСИ» (WM_AC_LINK 40…43,
   обозначение — в g_acLinkDes). Тот же EN_LINK → WM_AC_LINK, что у строк-кнопок. */
#define AC_LINK_SCAN 30000
static wchar_t g_acLinkDes[200];

static BOOL ac_is_letter(wchar_t c) {
  return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= 0x400 && c <= 0x4ff);
}

/* обозначение изделия: 2–4 буквы, «.» или пробел, цифра, дальше цифры/точки/дефисы, хотя бы одна точка после букв */
static BOOL ac_is_des(const wchar_t *t) {
  int nl = 0;
  while (ac_is_letter(t[nl])) nl++;
  if (nl < 2 || nl > 4) return FALSE;
  const wchar_t *p = t + nl;
  if (*p != L'.' && *p != L' ') return FALSE;
  p++;
  if (!(*p >= L'0' && *p <= L'9')) return FALSE;
  int digits = 0, dots = 0;
  for (; *p; p++) {
    if (*p >= L'0' && *p <= L'9') digits++;
    else if (*p == L'.') dots++;
    else if (*p == L'-') {
    } else if (ac_is_letter(*p) && !p[1]) { /* «…СБ» — последняя буква */
    } else if (ac_is_letter(*p) && ac_is_letter(p[1]) && !p[2]) p++;
    else return FALSE;
  }
  return digits >= 5 && dots >= 1;
}

static BOOL ac_is_path_start(const wchar_t *p) {
  return (((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) && p[1] == L':' && p[2] == L'\\') ||
         (p[0] == L'\\' && p[1] == L'\\' && ac_is_letter(p[2]));
}

static void ac_mark_link(LONG a, LONG b) {
  CHARRANGE cr = {a, b};
  SendMessageW(g_acLog, EM_EXSETSEL, 0, (LPARAM)&cr);
  CHARFORMAT2W cf;
  memset(&cf, 0, sizeof(cf));
  cf.cbSize = sizeof(cf);
  cf.dwMask = CFM_LINK;
  cf.dwEffects = CFE_LINK;
  SendMessageW(g_acLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void ac_text_links(void) {
  LONG len = GetWindowTextLengthW(g_acLog);
  LONG from = len > AC_LINK_SCAN ? len - AC_LINK_SCAN : 0;
  wchar_t *t = (wchar_t *)malloc(((size_t)(len - from) + 2) * sizeof(wchar_t));
  if (!t) return;
  TEXTRANGEW tr = {{from, len}, t};
  LONG got = (LONG)SendMessageW(g_acLog, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
  if (got <= 0) {
    free(t);
    return;
  }
  t[got] = 0;
  static const wchar_t kStop[] = L" \t\r\n,;«»\"'()[]{}<>|";
  for (LONG i = 0; i < got;) {
    if (i > 0 && !wcschr(kStop, t[i - 1])) { /* начало слова */
      i++;
      continue;
    }
    LONG j = i;
    if (ac_is_path_start(t + i)) { /* путь — до пробела/кавычки, но с пробелами внутри, пока файл существует */
      while (j < got && t[j] != L'\r' && t[j] != L'\n' && t[j] != L'"' && t[j] != L'\u00bb' && t[j] != L'\t' && t[j] != L';' &&
             t[j] != L'|')
        j++;
      while (j > i && (t[j - 1] == L' ' || t[j - 1] == L'.' || t[j - 1] == L',' || t[j - 1] == L')')) j--;
      wchar_t path[MAX_PATH];
      LONG n = j - i < MAX_PATH - 1 ? j - i : MAX_PATH - 1;
      memcpy(path, t + i, (size_t)n * sizeof(wchar_t));
      path[n] = 0;
      while (n && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) { /* хвост-слова после пути */
        wchar_t *sp = wcsrchr(path, L' ');
        if (!sp) {
          n = 0;
          break;
        }
        *sp = 0;
        n = (LONG)wcslen(path);
      }
      if (n > 3) ac_mark_link(from + i, from + i + n);
      i = j > i ? j : i + 1;
      continue;
    }
    while (j < got && !wcschr(kStop, t[j]) && j - i < 60) j++;
    if (j - i >= 7 && j - i < 60) {
      wchar_t w[64];
      memcpy(w, t + i, (size_t)(j - i) * sizeof(wchar_t));
      w[j - i] = 0;
      /* «АДЕ 3424-1073.00.00.01»: буквы, пробел, цифры — слово с пробелом склеиваем */
      if (ac_is_des(w)) ac_mark_link(from + i, from + j);
      else if (j < got && t[j] == L' ' && ac_is_letter(t[i]) && !(w[0] >= L'0' && w[0] <= L'9')) {
        LONG k = j + 1, nl = 0;
        while (nl < (LONG)wcslen(w) && ac_is_letter(w[nl])) nl++;
        if (nl == (LONG)wcslen(w) && nl >= 2 && nl <= 4 && t[k] >= L'0' && t[k] <= L'9') {
          while (k < got && !wcschr(kStop, t[k]) && k - i < 60) k++;
          wchar_t w2[64];
          memcpy(w2, t + i, (size_t)(k - i) * sizeof(wchar_t));
          w2[k - i] = 0;
          if (ac_is_des(w2)) {
            ac_mark_link(from + i, from + k);
            j = k;
          }
        }
      }
    }
    i = j > i ? j : i + 1;
  }
  free(t);
}

/* щёлкнули по ссылке в ленте: путь — открыть; обозначение — меню действий. FALSE — не наше */
static BOOL ac_text_link_click(const wchar_t *t) {
  if (ac_is_path_start(t)) {
    if (GetFileAttributesW(t) != INVALID_FILE_ATTRIBUTES) ShellExecuteW(g_acWnd, L"open", t, NULL, NULL, SW_SHOWNORMAL);
    else ac_status(L"Такого файла или папки уже нет");
    return TRUE;
  }
  if (!ac_is_des(t)) return FALSE;
  lstrcpynW(g_acLinkDes, t, 200);
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, 40, L"Есть ли в «Союзе» — карточка");
  AppendMenuW(m, MF_STRING, 41, L"Куда входит");
  AppendMenuW(m, MF_STRING, 42, L"Маршрутная ведомость");
  AppendMenuW(m, MF_STRING, 43, L"Загрузить ЭСИ из «Союза» в окно «ЭСИ и ТП»");
  POINT pt;
  GetCursorPos(&pt);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, pt.x, pt.y, 0, g_acWnd, NULL);
  DestroyMenu(m);
  if (cmd) PostMessageW(g_acWnd, WM_AC_LINK, (WPARAM)cmd, 0);
  return TRUE;
}

/* WM_AC_LINK 40…43 — действие над обозначением из ленты */
static void ac_des_action(int which) {
  const wchar_t *des = g_acLinkDes;
  if (!des[0]) return;
  wchar_t cmd[260];
  switch (which) {
  case 40:
    _snwprintf(cmd, 260, L"есть ли в Союзе %s", des);
    cmd[259] = 0;
    ac_route_say(cmd);
    break;
  case 41:
    _snwprintf(cmd, 260, L"куда входит %s", des);
    cmd[259] = 0;
    ac_route_say(cmd);
    break;
  case 42:
    route_show();
    SetWindowTextW(g_rtDes, des);
    rt_start();
    break;
  case 43:
    package_show(FALSE);
    SetWindowTextW(g_pkEsiEdit, des);
    pk_esi_start();
    break;
  }
}
