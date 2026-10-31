/* ---- Действия из чата для ЭСИ и «Союза» (с 2026.10.29) ---------------------------------------

   Не один большой сценарий, а отдельные действия, каждое — из чата по ключевым словам (как
   sz_chat_cmd, nm_want_full); модель не нужна, работу делает код. Короткий ответ в чат и
   строка-кнопки под ним (ссылки — тот же EN_LINK → WM_AC_LINK, что у маршрута).

     «загрузи ЭСИ по чертежам C:\…\папка» (или пути к файлам, в кавычках или через «;») —
         листы читает очередь окна «ЭСИ и ТП» (pk_stamp.c, окно создаётся скрытым): штамп,
         СБ — документом к сборке, спецификация — составом, детали — маршрут с операциями;
         участок и станок — по таблице приоритетов, Тпз/Тшт — norms.c, «Транспортирование» —
         при смене участка. По окончании — карточка: изделия, состав, операции, что не
         прочитано, и «Действия: В Союз · Только ЭСИ · Только ТП · Проверить подробно · Отмена»;
     «В Союз» — описание отдаётся мостом в клиент с последней папкой (нет папки — окно папки),
         отчёт клиента — строкой в чат; «Только ЭСИ» / «Только ТП» — то же по отдельности (2026.10.30); «Проверить подробно» — окно «ЭСИ и ТП» с этими
         строками; «Отмена» — листы очищаются;
     «выгрузи МВ по ТСТ.100.000» / «маршрутная ведомость ТСТ.100.000» — окно ведомости, сбор
         сразу (route_ui.c);
     «приоритет участков», «поставь участки» — таблица «операция → участок» (soyuz_prio.c).
   Запасной путь без чата — те же окна: «Ещё → ЭСИ и ТП в Союз», первая кнопка «Чертежи…». */

static const wchar_t *const kAcEsiLinks[] = {L"Проверить подробно", L"Отмена", L"Только ЭСИ", L"Только ТП"}; /* WM_AC_LINK 21…24; «В Союз» — 10 */
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

/* строка карточки «Куда ляжет: ЭСИ → …; ТП → …» по описанию окна и последним папкам; FALSE — сборки нет */
static BOOL esi_folders_line(wchar_t *out, int cap) {
  out[0] = 0;
  wchar_t *text = NULL, note[200];
  PkSent sent;
  memset(&sent, 0, sizeof(sent));
  pk_soyuz_text(&text, note, 200, &sent, PK_SEND_ALL);
  if (!text) return FALSE;
  wchar_t des[200], nm[200];
  BOOL has = szm_head_asm(text, des, 200, nm, 200);
  if (has) {
    SzmPrefs *pf = (SzmPrefs *)calloc(1, sizeof(SzmPrefs));
    wchar_t folder[SZM_PATH] = L"", tp[SZM_PATH] = L"";
    if (pf) szm_pref_load(pf), lstrcpynW(folder, pf->cur[0], SZM_PATH), lstrcpynW(tp, pf->cur[1], SZM_PATH), free(pf);
    SzmAsmFolder pr[2];
    BOOL mk = FALSE;
    if (!g_szmFold || !folder[0])
      _snwprintf(out, cap, L"Куда ляжет: сборке %s положена своя папка в выбранной папке «Союза» — «В Союз» откроет окно с предложением.", des);
    else if (szm_propose_text(text, folder, tp, pr, &mk)) {
      wchar_t a[SZM_PATH + 80] = L"", b[SZM_PATH + 80] = L"";
      if (pr[0].path[0]) szm_prop_line(&pr[0], L"ЭСИ", a, SZM_PATH + 80);
      if (pr[1].path[0]) szm_prop_line(&pr[1], L"ТП", b, SZM_PATH + 80);
      _snwprintf(out, cap, L"Куда ляжет: %s%s%s%s", a, a[0] && b[0] ? L"; " : L"", b,
                 mk && g_szmPkgVer < SZM_VER(1, 0, 0, 8) ? L" — но пакет в клиенте старее 1.0.0.8 и папок не создаёт" : L" — поменять можно в «Проверить подробно» → «В Союз…»");
    }
    out[cap - 1] = 0;
  }
  free(text);
  return out[0] != 0;
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
  { /* куда ляжет: папка сборки по правилу (soyuz_asmfolder.c) от последних выбранных папок */
    wchar_t fl[2 * SZM_PATH + 200];
    if (esi_folders_line(fl, 2 * SZM_PATH + 200)) l += _snwprintf(t + l, 1800 - l, L"\n%s", fl);
  }
  if (g_pksNoDes || noDes)
    l += _snwprintf(t + l, 1800 - l, L"\nБез обозначения: %d — штамп не прочитался, впишите в «Проверить подробно».", noDes ? noDes : g_pksNoDes);
  if (g_pksDoubt[0]) l += _snwprintf(t + l, 1800 - l, L"\nПроверьте обозначения: %ls.", g_pksDoubt);
  if (g_pksOpsNo) l += _snwprintf(t + l, 1800 - l, L"\nБез маршрута: %d (лист не прочитался или это не чертёж детали).", g_pksOpsNo);
  if (!op->n && det) l += _snwprintf(t + l, 1800 - l, L"\nОпераций нет: листы деталей не дали маршрута.");
  if (l < 1800 - 160)
    _snwprintf(t + l, 1800 - l, L"\n\nДействия:   В Союз   ·   Только ЭСИ   ·   Только ТП   ·   Проверить подробно   ·   Отмена");
  t[1799] = 0;
  ac_push(1, t);
  ac_status(L"ЭСИ готово — «В Союз» (или только ЭСИ, только ТП), «Проверить подробно» или «Отмена» под ответом");
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
  static const wchar_t *const words[] = {L"В Союз", L"Только ЭСИ", L"Только ТП", L"Проверить подробно", L"Отмена"};
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    memset(&ft, 0, sizeof(ft));
    ft.chrg.cpMin = at;
    ft.chrg.cpMax = at + 140;
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

/* «В Союз» под карточкой: описание из листов — мостом в клиент с последней папкой; mode — всё, только ЭСИ
   или только ТП (PK_SEND_*, «Только ЭСИ» / «Только ТП» под карточкой) */
static void esi_send(int mode) {
  if (!g_pk[PK_ITEMS].n && !g_pk[PK_OPS].n && !g_pk[PK_LINKS].n) {
    ac_push(1, L"В «Союз» нечего отправлять: листы «ЭСИ и ТП» пусты.");
    return;
  }
  pk_check(TRUE);
  wchar_t *text = NULL, note[200];
  PkSent sent;
  int n = pk_soyuz_text(&text, note, 200, &sent, mode);
  if (!text || !text[0]) {
    free(text);
    ac_push(1, mode == PK_SEND_ESI  ? L"Только ЭСИ — нечего отправлять: нет новых изделий, строк состава и чертежей."
               : mode == PK_SEND_TP ? L"Только ТП — нечего отправлять: нет операций с кодом справочника и заготовок."
                                    : L"В «Союз» нечего отправлять: нет ни изделий, ни операций с кодом справочника.");
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
  wchar_t label[240]; /* заголовок окна «В Союз…», если придётся его открыть */
  if (mode == PK_SEND_ESI)
    _snwprintf(label, 240, L"Только ЭСИ: изделий %d, состав %d, чертежей %d", sent.items, sent.links, sent.draws);
  else if (mode == PK_SEND_TP)
    _snwprintf(label, 240, L"Только ТП (к изделиям в «Союзе»): операций %d, заготовок %d", n, sent.blanks);
  else
    _snwprintf(label, 240, L"В «Союз»: изделий %d, состав %d, операций %d", sent.items, g_pk[PK_LINKS].n, n);
  label[239] = 0;
  if (wcsstr(text, L"ИЗДЕЛИЕ\t") && !folder[0]) { /* папки ещё не было — спросить в окне */
    szm_open_text(text, label);
    free(text);
    ac_push(1, L"Папку «Союза» для новых изделий ещё не выбирали — выберите в открывшемся окне (или вставьте ссылку "
                L"«копировать ссылку в PLM» на папку) и нажмите «Создать».");
    return;
  }
  /* сборке — своя папка (soyuz_asmfolder.c): списка папок ещё нет — показать предложение в окне */
  wchar_t des[200], nm[200];
  BOOL hasAsm = szm_head_asm(text, des, 200, nm, 200);
  if (hasAsm && folder[0] && !g_szmFold) {
    szm_open_text(text, label);
    free(text);
    ac_push(1, L"Сборке положена своя папка в «Союзе» — проверьте предложение в открывшемся окне (папки спрашиваются у клиента) "
                L"и нажмите «Создать».");
    return;
  }
  wchar_t root0[SZM_PATH], root1[SZM_PATH];
  lstrcpynW(root0, folder, SZM_PATH);
  lstrcpynW(root1, tp, SZM_PATH);
  SzmAsmFolder pr[2];
  BOOL mk = FALSE, used = FALSE;
  if (hasAsm && szm_propose_text(text, folder, tp, pr, &mk)) {
    if (mk && g_szmPkgVer < SZM_VER(1, 0, 0, 8)) {
      ac_push(1, L"Пакет «CursorPad — Союз» в клиенте старее 1.0.0.8 — папку под сборку он не создаст, изделия лягут в выбранную папку.");
      mk = FALSE;
    } else {
      if (pr[0].path[0]) lstrcpynW(folder, pr[0].path, SZM_PATH), used = TRUE;
      if (pr[1].path[0]) lstrcpynW(tp, pr[1].path, SZM_PATH), used = TRUE;
    }
  }
  g_szmMkDirs = mk;
  wchar_t msg[500];
  BOOL ok = szm_send_text(text, folder, tp, g_acWnd, msg, 500);
  g_szmMkDirs = FALSE;
  if (ok && used) szm_pref_save(root0, root1); /* последними остаются корни, не папка сборки */
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
   «АБВГ.123456.001», «ТСТ.100.000», «АДЕ 3422-682.01.01» (ac_is_des: 2–4 буквы, точка или пробел,
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
      /* «АДЕ 3422-682.01.01»: буквы, пробел, цифры — слово с пробелом склеиваем */
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

/* ---- «дерево ТП <обозначение>» (с 31.10): как устроен ТП в базе — только чтение (SELECT) ---------------------------
   Для сверки с заводом: ТП, сделанный руками, и ТП, созданный функцией пакета, — «ТП → версия → операции» и
   «вариант в версии → операции в варианте», у каких объектов есть нормы (Тпз/Тшт), профессии, станки, участок,
   способ комплектования ТП. ТП ищется по обозначению (атрибут Designation) или по имени «<обозначение> […]».
   Ответ — в чат (WM_AC_TREE из потока). */
#define WM_AC_TREE (WM_APP + 89)

/* обозначение в тексте: слово или «буквы цифры…» из двух слов, как ac_is_des; FALSE — нет */
static BOOL tree_des_of(const wchar_t *a, wchar_t *out, int cap) {
  out[0] = 0;
  wchar_t buf[600];
  lstrcpynW(buf, a, 600);
  for (wchar_t *c = buf; *c; c++)
    if (*c == 0xAB || *c == 0xBB || *c == L'"' || *c == L',' || *c == L';' || *c == L'?') *c = L' ';
  wchar_t *w[60];
  int n = 0;
  for (wchar_t *c = buf; *c && n < 60;) {
    while (*c == L' ' || *c == L'\t') *c++ = 0;
    if (!*c) break;
    w[n++] = c;
    while (*c && *c != L' ' && *c != L'\t') c++;
  }
  for (int i = 0; i < n; i++) {
    size_t l = wcslen(w[i]);
    while (l && (w[i][l - 1] == L'.' || w[i][l - 1] == L':')) w[i][--l] = 0;
    if (ac_is_des(w[i])) {
      lstrcpynW(out, w[i], cap);
      return TRUE;
    }
    if (i + 1 < n) {
      wchar_t two[200];
      _snwprintf(two, 200, L"%s %s", w[i], w[i + 1]);
      two[199] = 0;
      size_t l2 = wcslen(two);
      while (l2 && (two[l2 - 1] == L'.' || two[l2 - 1] == L':')) two[--l2] = 0;
      if (ac_is_des(two)) {
        lstrcpynW(out, two, cap);
        return TRUE;
      }
    }
  }
  return FALSE;
}

/* «дерево ТП …», «дерево техпроцесса …», «как устроен ТП …» */
static BOOL tree_wanted(const wchar_t *a) {
  wchar_t low[2000];
  esi_low(a, low, 2000);
  if (wcslen(low) > 300) return FALSE;
  BOOL tp = tn_word(low, L"тп") || wcsstr(low, L"техпроцесс") || wcsstr(low, L"технологическ");
  return tp && (wcsstr(low, L"дерев") || wcsstr(low, L"как устроен") || wcsstr(low, L"структур"));
}

static const wchar_t *tree_kind_ui(const wchar_t *key) {
  if (!wcscmp(key, L"TSVersion")) return L"Версия ТП";
  if (!wcscmp(key, L"TSVVariant")) return L"Вариант ТП в версии";
  if (!wcscmp(key, L"TSVariant")) return L"Вариант ТП";
  if (!wcscmp(key, L"TSOperation")) return L"операция";
  if (!wcscmp(key, L"TSOperationVariant")) return L"операция в варианте";
  return key;
}

/* операции под parent: строка на каждую и итог «нормы a из n, профессия b, станок c, участок d» */
static void tree_ops(ShareBuf *b, const CardRow *rows, int n, long parent, const wchar_t *indent) {
  int cnt = 0, fn = 0, fc = 0, fe = 0, fa = 0;
  for (int i = 0; i < n; i++) {
    if (rows[i].n2 != parent) continue;
    if (wcscmp(rows[i].s1, L"TSOperation") && wcscmp(rows[i].s1, L"TSOperationVariant")) continue;
    int fl = (int)(rows[i].n3 % 100);
    cnt++;
    fn += (fl & 1) != 0, fc += (fl & 2) != 0, fe += (fl & 4) != 0, fa += (fl & 8) != 0;
    wchar_t num[40] = L"", code[40] = L"";
    const wchar_t *p = rows[i].s2, *bar = wcschr(p, L'|');
    if (bar) {
      lstrcpynW(num, p, (int)(bar - p) + 1 < 40 ? (int)(bar - p) + 1 : 40);
      const wchar_t *bar2 = wcschr(bar + 1, L'|');
      if (bar2) lstrcpynW(code, bar + 1, (int)(bar2 - bar) < 40 ? (int)(bar2 - bar) : 40);
    }
    if (cnt <= 30)
      sb_add(b, L"%s%s %s · нормы %s · профессия %s · станок %s · участок %s\n", indent, num[0] ? num : L"—", code[0] ? code : L"—",
             fl & 1 ? L"есть" : L"нет", fl & 2 ? L"есть" : L"нет", fl & 4 ? L"есть" : L"нет", fl & 8 ? L"есть" : L"нет");
  }
  if (cnt > 30) sb_add(b, L"%s… ещё %d\n", indent, cnt - 30);
  if (cnt) sb_add(b, L"%sитого %d: с нормами %d, с профессией %d, со станком %d, с участком %d\n", indent, cnt, fn, fc, fe, fa);
  else sb_add(b, L"%sопераций нет\n", indent);
}

/* текст дерева ТП des (malloc; NULL — память) — в потоке и на стенде */
static wchar_t *tree_build(const wchar_t *des) {
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  wchar_t d[200];
  int k = 0;
  for (const wchar_t *c = des; *c && k < 199; c++)
    if (*c != L'\'') d[k++] = *c; /* в SQL — без кавычек */
  d[k] = 0;
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[300] = L"";
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * CARD_ROWS);
  wchar_t *sql = (wchar_t *)malloc(sizeof(wchar_t) * 6000);
  if (!rows || !sql || !g_szConnect(&env, &dbc, err, 300)) {
    sb_add(&b, L"Дерево ТП «%s»: база «Союза» недоступна%s%s.", d, err[0] ? L" — " : L"", err);
    goto done;
  }
  g_qTimeout = 30;
  _snwprintf(sql, 6000,
             L"WITH r AS (SELECT ts.InfoObjectId AS Id, 0 AS L FROM InfoObjects AS ts WITH(NOLOCK) "
             L"JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=ts.TemplateId AND t.NameKey=N'TechnologicalStructure' "
             L"WHERE ts.Erased=0 AND (LEFT(CAST(ts.Name AS NVARCHAR(400)), %d)=N'%s [' OR CAST(ts.Name AS NVARCHAR(400))=N'%s' "
             L"OR EXISTS (SELECT 1 FROM InfoObjectAttributes AS a WITH(NOLOCK) JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId "
             L"AND nk.Value=N'Designation' WHERE a.OwnerId=ts.InfoObjectId AND a.Outdated=0 AND CAST(a.ShortText AS NVARCHAR(200))=N'%s')) "
             L"UNION ALL SELECT c.InfoObjectId, r.L+1 FROM InfoObjects AS c WITH(NOLOCK) JOIN r ON c.ParentId=r.Id WHERE c.Erased=0 AND r.L<3) "
             L"SELECT TOP 800 r.Id, t.NameKey, ISNULL(nr.S,N'') + N'|' + ISNULL(cd.S,N'') + N'|' + ISNULL(CAST(o.Name AS NVARCHAR(160)),N''), "
             L"ISNULL(o.ParentId,0), r.L*100 "
             L"+ CASE WHEN EXISTS (SELECT 1 FROM InfoObjectAttributes AS x WITH(NOLOCK) JOIN NameKeys AS y WITH(NOLOCK) ON y.NameKeyId=x.NameKeyId "
             L"AND y.Value IN (N'SetupTime',N'TimePerPiece') WHERE x.OwnerId=r.Id AND x.Outdated=0) THEN 1 ELSE 0 END "
             L"+ CASE WHEN EXISTS (SELECT 1 FROM InfoObjectAttributes AS x WITH(NOLOCK) JOIN NameKeys AS y WITH(NOLOCK) ON y.NameKeyId=x.NameKeyId "
             L"AND y.Value IN (N'Craft',N'CraftList') WHERE x.OwnerId=r.Id AND x.Outdated=0) THEN 2 ELSE 0 END "
             L"+ CASE WHEN EXISTS (SELECT 1 FROM InfoObjectAttributes AS x WITH(NOLOCK) JOIN NameKeys AS y WITH(NOLOCK) ON y.NameKeyId=x.NameKeyId "
             L"AND y.Value IN (N'Equipment',N'EquipmentList') WHERE x.OwnerId=r.Id AND x.Outdated=0) THEN 4 ELSE 0 END "
             L"+ CASE WHEN EXISTS (SELECT 1 FROM InfoObjectAttributes AS x WITH(NOLOCK) JOIN NameKeys AS y WITH(NOLOCK) ON y.NameKeyId=x.NameKeyId "
             L"AND y.Value=N'Area' WHERE x.OwnerId=r.Id AND x.Outdated=0 AND ISNULL(x.Link,0)<>0) THEN 8 ELSE 0 END "
             L"FROM r JOIN InfoObjects AS o WITH(NOLOCK) ON o.InfoObjectId=r.Id JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId "
             L"OUTER APPLY (SELECT TOP 1 CAST(a.ShortText AS NVARCHAR(20)) AS S FROM InfoObjectAttributes AS a WITH(NOLOCK) JOIN NameKeys AS nk WITH(NOLOCK) "
             L"ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'Number' WHERE a.OwnerId=r.Id AND a.Outdated=0) AS nr "
             L"OUTER APPLY (SELECT TOP 1 CAST(c.ShortText AS NVARCHAR(20)) AS S FROM InfoObjectAttributes AS l WITH(NOLOCK) JOIN NameKeys AS nkl WITH(NOLOCK) "
             L"ON nkl.NameKeyId=l.NameKeyId AND nkl.Value=N'Operation' JOIN InfoObjectAttributes AS c WITH(NOLOCK) ON c.OwnerId=l.Link AND c.Outdated=0 "
             L"JOIN NameKeys AS nkc WITH(NOLOCK) ON nkc.NameKeyId=c.NameKeyId AND nkc.Value=N'Code' WHERE l.OwnerId=r.Id AND l.Outdated=0) AS cd "
             L"ORDER BY r.L, o.ParentId, nr.S, r.Id",
             (int)wcslen(d) + 2, d, d, d);
  sql[5999] = 0;
  int n = card_query(dbc, sql, rows, CARD_ROWS, err, 300);
  if (n < 0) {
    sb_add(&b, L"Дерево ТП «%s»: запрос не прошёл — %s", d, err);
  } else if (n == 0) {
    sb_add(&b, L"Дерево ТП «%s»: в базе такого ТП нет (ни по обозначению, ни по имени).", d);
  } else {
    int roots = 0;
    for (int i = 0; i < n; i++) roots += rows[i].n3 / 100 == 0;
    sb_add(&b, L"Дерево ТП «%s» — только чтение, в базе ничего не меняется. ТП: %d\n", d, roots);
    for (int i = 0; i < n; i++) {
      if (rows[i].n3 / 100 != 0) continue;
      const wchar_t *nm = wcsrchr(rows[i].s2, L'|');
      sb_add(&b, L"\nТП «%s» (объект %ld)\n", nm ? nm + 1 : rows[i].s2, rows[i].n1);
      for (int j = 0; j < n; j++) { /* версии и варианты ТП */
        if (rows[j].n2 != rows[i].n1) continue;
        const wchar_t *nmj = wcsrchr(rows[j].s2, L'|');
        sb_add(&b, L"  %s «%s» (объект %ld)\n", tree_kind_ui(rows[j].s1), nmj ? nmj + 1 : L"", rows[j].n1);
        if (!wcscmp(rows[j].s1, L"TSVersion")) {
          tree_ops(&b, rows, n, rows[j].n1, L"    ");
          for (int q = 0; q < n; q++) /* вариант в версии и его операции */
            if (rows[q].n2 == rows[j].n1 && !wcscmp(rows[q].s1, L"TSVVariant")) {
              const wchar_t *nmq = wcsrchr(rows[q].s2, L'|');
              sb_add(&b, L"    %s «%s» (объект %ld)\n", tree_kind_ui(rows[q].s1), nmq ? nmq + 1 : L"", rows[q].n1);
              tree_ops(&b, rows, n, rows[q].n1, L"      ");
            }
        }
      }
    }
    /* способ комплектования корня */
    _snwprintf(sql, 6000,
               L"SELECT TOP 20 a.OwnerId, ISNULL(CAST(nv.NameUI AS NVARCHAR(200)),N''), ISNULL(nv.NameKey,N''), 0, 0 FROM InfoObjectAttributes AS a WITH(NOLOCK) "
               L"JOIN NameKeys AS nk WITH(NOLOCK) ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'KindOfStorageItems' "
               L"JOIN NamedValues AS nv WITH(NOLOCK) ON nv.NamedValueId=a.Link WHERE a.Outdated=0 AND a.OwnerId=%ld",
               rows[0].n1);
    CardRow k1[20];
    int nk = card_query(dbc, sql, k1, 20, err, 300);
    if (nk > 0) sb_add(&b, L"\nСпособ комплектования ТП: «%s» (%s)\n", k1[0].s1, k1[0].s2);
  }
done:
  if (dbc != SQL_NULL_HDBC) SQLDisconnect(dbc), SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  if (env != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, env);
  free(rows);
  free(sql);
  return b.w;
}

static DWORD WINAPI tree_thread(LPVOID arg) {
  wchar_t *des = (wchar_t *)arg;
  wchar_t *t = tree_build(des);
  free(des);
  if (!g_acWnd || !PostMessageW(g_acWnd, WM_AC_TREE, 0, (LPARAM)t)) free(t);
  return 0;
}

/* команда чата; TRUE — забрана */
static BOOL tree_chat_cmd(const wchar_t *a) {
  if (!tree_wanted(a)) return FALSE;
  ac_push(0, a);
  wchar_t des[200];
  if (!tree_des_of(a, des, 200)) {
    ac_push(1, L"Напишите обозначение детали: «дерево ТП АБВГ.123456.001» — покажу, как устроен её ТП в «Союзе» (только чтение).");
    return TRUE;
  }
  wchar_t *d = _wcsdup(des);
  HANDLE th = d ? CreateThread(NULL, 0, tree_thread, d, 0, NULL) : NULL;
  if (th) CloseHandle(th);
  else free(d);
  wchar_t m[300];
  _snwprintf(m, 300, L"Читаю дерево ТП «%s» в базе «Союза» — только чтение…", des);
  m[299] = 0;
  ac_status(m);
  return TRUE;
}

/* ответ потока */
static void tree_report(wchar_t *t) {
  ac_push(1, t ? t : L"Дерево ТП: ответа нет.");
  ac_status(L"Дерево ТП прочитано — в базе ничего не менялось");
  free(t);
}
