/* App commands execute on the UI thread. Only this registry is callable by the
   model: no command lines, SQL, function addresses or arbitrary Windows calls. */
typedef struct { const wchar_t *name, *description; void (*show)(void); } AgCapability;
static const AgCapability kAgCapabilities[] = {
  {L"open_match", L"Открыть сопоставление позиций Excel с данными СОЮЗ; выбор данных и запуск видны пользователю", match_show},
  {L"open_export", L"Открыть штатную выгрузку из PLM/СОЮЗ", export_show},
  {L"open_route", L"Открыть маршрутную ведомость из PLM", route_show},
  {L"open_drawings", L"Открыть распознавание чертежей и существующую раскладку по папкам", drawrec_show},
  {L"open_paint", L"Открыть расчёт краски", paint_show},
  {L"open_cutting", L"Открыть расчёт резки и газов", cutting_show},
};
/* Данные PLM «Союз» и архива чертежей (с 2026.10.23) — только чтение. ИИ ищет
   и читает настоящие карточки сам, а не отвечает по памяти. Поиск и карточка
   идут тем же путём, что у коллеги без логина (share.c): запрос исполняет
   отдельный процесс «--plm-serve» — свой, если логин SQL вписан, иначе у
   раздающего коллеги. Окно находок и открытая карточка поэтому не трогаются.
   Вызываются из потока инструментов (ag_tool_thread), не из окна. */
typedef struct { const wchar_t *name, *description; } AgDataTool;
static const AgDataTool kAgDataTools[] = {
  {L"plm_search", L"найти в PLM «Союз» изделия, детали и техпроцессы по обозначению или словам наименования; text — запрос; ответ — строки с IO-номерами"},
  {L"plm_card", L"карточка изделия из PLM: заготовка и материал, основной ТП с операциями, техсостав (состав сборки), файлы в архиве; text — обозначение или IO.номер из plm_search"},
  {L"plm_where_used", L"куда изделие входит по техсоставу (входимость); text — обозначение или IO.номер"},
  {L"archive_find", L"найти файлы и сканы чертежей в архиве (индекс files.json) по обозначению или словам; text — запрос; ответ — полные пути"},
  {L"plm_check", L"сверить с PLM «Союз» список обозначений и наименований: что есть, чего нет, где наименование другое; text — сам список (строка на позицию) или «буфер»; либо path — файл .xlsx/.docx/.csv/.txt или скан чертежа .tif/.png/.jpg (штамп и спецификация); ответ — готовый итог"},
  {L"std_find", L"справочник стандартов на этом компьютере: размеры крепежа, сортамент и масса 1 м (с L= — масса заготовки), марки сталей, аналоги DIN/ISO↔ГОСТ; text — «болт М12 ГОСТ 7798», «круг 50 L=120», «40Х»; ответ — строки таблиц с источником"},
};
static BOOL ag_data_tool(const wchar_t *name) {
  for (size_t i = 0; i < sizeof(kAgDataTools) / sizeof(kAgDataTools[0]); i++)
    if (!wcscmp(name, kAgDataTools[i].name)) return TRUE;
  return FALSE;
}

/* Запрос «kind\t…» исполнителю share.c. Возвращает ответ CPANS1 (освобождает
   вызывающий) или NULL — тогда в err объяснение для модели. */
static wchar_t *ag_plm_ask(const wchar_t *body, int waitSec, volatile LONG *cancel, wchar_t *err, int cap) {
  if (share_client_on()) return share_ask(body, L"-ai", waitSec, err, cap);
  if (!g_sqlUser[0] || !g_sqlPass[0]) {
    lstrcpynW(err, L"PLM не настроен: в Настройках нет логина SQL и общей папки с раздающим коллегой. Данные PLM не получены; не выдумывай их, скажи об этом.", cap);
    return NULL;
  }
  static volatile LONG seq;
  wchar_t dir[MAX_PATH], req[MAX_PATH], ans[MAX_PATH], exe[MAX_PATH];
  _snwprintf(dir, MAX_PATH, L"%s\\ai_plm", g_dataDir);
  dir[MAX_PATH - 1] = 0;
  CreateDirectoryW(dir, NULL);
  LONG n = InterlockedIncrement(&seq);
  _snwprintf(req, MAX_PATH, L"%s\\%lu-%ld.req", dir, GetCurrentProcessId(), n);
  _snwprintf(ans, MAX_PATH, L"%s\\%lu-%ld.ans", dir, GetCurrentProcessId(), n);
  req[MAX_PATH - 1] = ans[MAX_PATH - 1] = 0;
  ShareBuf b = {0};
  sb_add(&b, L"CPREQ1\n%s\nfrom\tИИ CursorPad\n", body);
  BOOL sent = b.w && share_write(req, b.w);
  free(b.w);
  if (!sent) {
    swprintf(err, cap, L"Не удалось записать запрос к PLM в %ls. Данные не получены.", dir);
    return NULL;
  }
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  wchar_t cmd[MAX_PATH * 3 + 32];
  _snwprintf(cmd, MAX_PATH * 3 + 32, L"\"%s\" --plm-serve \"%s\" \"%s\"", exe, req, ans);
  cmd[MAX_PATH * 3 + 31] = 0;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  memset(&pi, 0, sizeof(pi));
  if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    DeleteFileW(req);
    lstrcpynW(err, L"Не удалось запустить запрос к PLM. Данные не получены.", cap);
    return NULL;
  }
  CloseHandle(pi.hThread);
  BOOL stopped = FALSE, late = TRUE;
  for (int t = 0; t < waitSec * 5; t++) {
    if (cancel && *cancel) {
      stopped = TRUE;
      break;
    }
    if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) {
      late = FALSE;
      break;
    }
  }
  if (stopped || late) TerminateProcess(pi.hProcess, 2);
  CloseHandle(pi.hProcess);
  wchar_t *a = stopped || late ? NULL : share_read(ans);
  DeleteFileW(req);
  DeleteFileW(ans);
  if (a && wcsncmp(a, L"CPANS1", 6)) {
    free(a);
    a = NULL;
  }
  if (!a)
    swprintf(err, cap, L"%ls Данные PLM не получены; не выдумывай их.",
             stopped ? L"Запрос к PLM остановлен." : late ? L"PLM не ответил вовремя (сервер занят?)." : L"PLM вернул пустой ответ.");
  return a;
}

/* текст ответа исполнителя — всё после строки «text» */
static const wchar_t *ag_plm_text(const wchar_t *a) {
  const wchar_t *t = wcsstr(a, L"\ntext\n");
  return t ? t + 6 : L"";
}

typedef struct {
  long id;
  int tmpl;
  wchar_t esi[PLM_COL1], tp[PLM_COL1];
} AgPlmRow;

/* Поиск — те же строки «row», что получает коллега (share_lookup) */
static int ag_plm_search(const wchar_t *q, AgPlmRow *rows, int max, volatile LONG *cancel, wchar_t *err, int cap) {
  wchar_t body[480], qq[400];
  lstrcpynW(qq, q, 400);
  for (wchar_t *c = qq; *c; c++)
    if (*c == L'\t' || *c == L'\r' || *c == L'\n') *c = L' ';
  _snwprintf(body, 480, L"kind\tsearch\nq\t%s", qq);
  body[479] = 0;
  wchar_t *a = ag_plm_ask(body, 60, cancel, err, cap);
  if (!a) return -1;
  int n = 0;
  wchar_t *p = a, *line;
  while ((line = share_next_line(&p)) != NULL) {
    if (!wcscmp(line, L"text")) break;
    wchar_t *f[8];
    int k = share_split(line, f, 8);
    if (wcscmp(f[0], L"row") || k < 7 || n >= max) continue;
    rows[n].id = wcstol(f[1], NULL, 10);
    rows[n].tmpl = (int)wcstol(f[2], NULL, 10);
    lstrcpynW(rows[n].esi, f[5], PLM_COL1);
    lstrcpynW(rows[n].tp, f[6], PLM_COL1);
    n++;
  }
  if (!n) {
    /* у исполнителя текст объясняет почему: нет связи, нет такого */
    wchar_t why[400];
    lstrcpynW(why, p ? p : L"", 400);
    for (wchar_t *c = why; *c; c++)
      if (*c == L'\r' || *c == L'\n') *c = L' ';
    swprintf(err, cap, L"PLM: по «%ls» ничего не нашлось. %ls", qq, why);
  }
  free(a);
  return n;
}

static void ag_plm_rows_text(const wchar_t *q, const AgPlmRow *rows, int n, wchar_t *out, int cap) {
  swprintf(out, cap, L"PLM «Союз», поиск «%ls»: строк %d%ls.\r\n", q, n, n > 15 ? L", показаны первые 15" : L"");
  for (int i = 0; i < n && i < 15; i++) {
    size_t at = wcslen(out);
    swprintf(out + at, cap - at, L"%d. %ls%ls%ls · IO.%ld\r\n", i + 1, rows[i].esi, rows[i].tp[0] ? L" · ТП: " : L"",
             rows[i].tp, rows[i].id);
  }
  size_t at = wcslen(out);
  swprintf(out + at, cap - at, L"Подробно: app name=plm_card text=IO.номер.");
}

/* Какой объект открыть: IO.номер, номер или обозначение. По обозначению —
   строка с тем же голым обозначением (plm_core_des), единственная строка,
   иначе список на выбор (в out, *listed, возвращает 0). */
static long ag_plm_pick(const wchar_t *text, volatile LONG *cancel, wchar_t *out, int cap, BOOL *listed) {
  const wchar_t *s = text;
  while (*s == L' ') s++;
  if (!_wcsnicmp(s, L"IO.", 3)) s += 3;
  wchar_t *end = NULL;
  long id = wcstol(s, &end, 10);
  if (id > 0 && end && !*end) return id;
  AgPlmRow *rows = (AgPlmRow *)calloc(40, sizeof(AgPlmRow));
  if (!rows) return 0;
  int n = ag_plm_search(text, rows, 40, cancel, out, cap);
  long pick = 0;
  if (n > 0) {
    wchar_t want[PLM_COL1], have[PLM_COL1];
    plm_core_des(text, want, PLM_COL1);
    for (int i = 0; i < n && !pick; i++) {
      plm_core_des(rows[i].esi, have, PLM_COL1);
      if (want[0] && !_wcsicmp(want, have) && rows[i].id) pick = rows[i].id;
    }
    if (!pick && n == 1) pick = rows[0].id;
    if (!pick) {
      *listed = TRUE;
      ag_plm_rows_text(text, rows, n, out, cap);
      size_t at = wcslen(out);
      swprintf(out + at, cap - at, L" Точного совпадения обозначения нет: выбери строку и вызови plm_card с её IO.номером.");
    }
  }
  free(rows);
  return pick;
}

/* Карточка — как в окне «Карточка» (plm_card), verbose — «Все данные» */
static wchar_t *ag_plm_card(long id, BOOL verbose, volatile LONG *cancel, wchar_t *err, int cap) {
  wchar_t body[120];
  _snwprintf(body, 120, L"kind\tcard\nid\t%ld\nverbose\t%d", id, verbose ? 1 : 0);
  return ag_plm_ask(body, 100, cancel, err, cap);
}

/* Длинную карточку сокращаем в середине: наверху обозначение, заготовка и ТП,
   внизу техсостав и файлы в архиве — длиннее всего список операций. */
static void ag_fit(const wchar_t *head, const wchar_t *text, wchar_t *out, int cap) {
  int room = cap - (int)wcslen(head) - 80;
  int len = (int)wcslen(text);
  if (room < 200) room = 200;
  if (len <= room) {
    swprintf(out, cap, L"%ls%ls", head, text);
    return;
  }
  int first = room * 2 / 5, last = room - first;
  swprintf(out, cap, L"%ls%.*ls\r\n… (середина сокращена) …\r\n%ls", head, first, text, text + len - last);
}

static int ag_archive_find(const wchar_t *text, wchar_t *out, int cap) {
  files_lock_init();
  if (!g_filesRoot[0]) {
    lstrcpynW(out, L"Архив не настроен: в Настройках не указана папка файлов. Файлы не искались.", cap);
    return 0;
  }
  files_lock();
  if (g_filesN == 0) files_load_idx();
  files_unlock();
  if (g_filesN == 0) {
    lstrcpynW(out, L"Индекс архива пуст: нужно «Обновить JSON» в Настройках. Файлы не искались.", cap);
    return 0;
  }
  /* обозначение ищется само, без буквенного кода: в именах сканов его нет */
  SrRoute r;
  sr_route(text, &r);
  wchar_t q[SR_Q];
  if (r.why == SR_WHY_DES) plm_core_des(r.q, q, SR_Q);
  else lstrcpynW(q, text, SR_Q);
  FileHit hits[25];
  int n = 0, total = 0;
  BOOL inDirs = FALSE;
  files_lock();
  FileIdx *ix = g_idx;
  if (ix) {
    n = files_find(q, TRUE, hits, 25, &total);
    if (!n) {
      n = files_find(q, FALSE, hits, 25, &total);
      inDirs = n > 0;
    }
  }
  swprintf(out, cap, L"Архив (%d файлов в индексе), поиск «%ls»: найдено %d%ls%ls.\r\n", g_filesN, q, total,
           total > n ? L", показаны лучшие" : L"", inDirs ? L" (слова нашлись в именах папок)" : L"");
  for (int k = 0; k < n && ix; k++) {
    const FileEnt *e = &ix->ent[hits[k].i];
    const wchar_t *dir = e->dir < ix->dirsN ? ix->dirs[e->dir] : L"";
    size_t at = wcslen(out);
    if (at + wcslen(dir) + wcslen(e->name) + 8 >= (size_t)cap) break;
    swprintf(out + at, cap - at, L"%ls\\%ls\r\n", dir, e->name);
  }
  files_unlock();
  return n;
}

/* ---- сверка с Союзом (plm_check.c) -----------------------------------------------
   Позиции уходят пачками по 20 запросом «check»: исполнитель (свой процесс или
   раздающий коллега) держит одно соединение на пачку. Коллега со старой версией
   «check» не знает — тогда каждая позиция обычным поиском, как plm_search. */
#define AG_PC_PACK 20

typedef void (*AgPcProgress)(int done, int total, void *ctx);

/* Причина для человека: подсказки модели («не выдумывай их») ему не нужны */
static void ag_pc_user_err(wchar_t *why) {
  static const wchar_t *const tail[] = {L" Данные PLM не получены", L" Данные не получены", L"; не выдумывай"};
  for (size_t i = 0; i < sizeof(tail) / sizeof(tail[0]); i++) {
    wchar_t *t = wcsstr(why, tail[i]);
    if (t) *t = 0;
  }
}

/* Причина из ответа исполнителя одной строкой («Не удалось подключиться…») */
static void ag_pc_why(const wchar_t *text, wchar_t *why, int cap) {
  while (*text == L'P' || *text == L'L' || *text == L'M' || *text == L'\r' || *text == L'\n') text++;
  lstrcpynW(why, text, cap);
  for (wchar_t *c = why; *c; c++)
    if (*c == L'\r' || *c == L'\n') *c = L' ';
}

/* Позиция обычным поиском (наименование всегда так, обозначение — у старого
   коллеги). FALSE — PLM не ответил. */
static BOOL ag_pc_search_one(PcItem *it, volatile LONG *cancel, wchar_t *why, int whyCap) {
  AgPlmRow *rows = (AgPlmRow *)calloc(40, sizeof(AgPlmRow));
  if (!rows) return FALSE;
  wchar_t err[600] = L"";
  int n = ag_plm_search(it->des[0] ? it->core : it->name, rows, 40, cancel, err, 600);
  if (n < 0 || (!n && (wcsstr(err, L"подключиться") || wcsstr(err, L"не выполнился") || wcsstr(err, L"не настроен") ||
                       wcsstr(err, L"никто не раздаёт") || wcsstr(err, L"не забрал") || wcsstr(err, L"не пришёл")))) {
    lstrcpynW(why, err, whyCap);
    free(rows);
    return FALSE;
  }
  for (int i = 0; i < n; i++) pc_row_add(it, rows[i].id, rows[i].tmpl, rows[i].esi, L"", L"");
  free(rows);
  pc_judge(it);
  return TRUE;
}

/* Сверить все позиции списка. why — почему что-то осталось непроверенным. */
static void ag_pc_query(PcList *l, volatile LONG *cancel, AgPcProgress progress, void *ctx, wchar_t *why, int whyCap) {
  int done = 0, legacy = 0, stuck = 0;
  why[0] = 0;
  wchar_t *body = (wchar_t *)malloc(sizeof(wchar_t) * (AG_PC_PACK * (PC_DES + 4) + 64));
  if (!body) {
    lstrcpynW(why, L"не хватило памяти", whyCap);
    return;
  }
  for (;;) {
    if (cancel && *cancel) {
      lstrcpynW(why, L"остановлено", whyCap);
      break;
    }
    int pack[AG_PC_PACK], np = 0;
    for (int i = 0; i < l->n && np < AG_PC_PACK; i++)
      if (l->it[i].state == PCS_TODO) pack[np++] = i;
    if (!np) break;
    /* по наименованию и у старого коллеги — по одной */
    if (legacy || !l->it[pack[0]].des[0]) {
      PcItem *it = &l->it[pack[0]];
      if (!ag_pc_search_one(it, cancel, why, whyCap)) {
        /* связи нет — дальше спрашивать бесполезно */
        for (int i = 0; i < l->n; i++)
          if (l->it[i].state == PCS_TODO) l->it[i].state = PCS_FAIL;
        break;
      }
      done++;
      if (progress) progress(done, l->n, ctx);
      continue;
    }
    /* пачка — только позиции с обозначением */
    int k = 0;
    for (int i = 0; i < np; i++)
      if (l->it[pack[i]].des[0]) pack[k++] = pack[i];
    np = k;
    size_t at = (size_t)swprintf(body, 64, L"kind\tcheck\n");
    for (int i = 0; i < np; i++) {
      wcscpy(body + at, L"d\t");
      wcscat(body + at, l->it[pack[i]].core);
      wcscat(body + at, L"\n");
      at += wcslen(body + at);
    }
    wchar_t err[600] = L"";
    wchar_t *a = ag_plm_ask(body, 95, cancel, err, 600);
    if (!a) {
      lstrcpynW(why, err, whyCap);
      for (int i = 0; i < l->n; i++)
        if (l->it[i].state == PCS_TODO) l->it[i].state = PCS_FAIL;
      break;
    }
    BOOL got[AG_PC_PACK] = {0}, any = FALSE;
    wchar_t *p = a, *line;
    while ((line = share_next_line(&p)) != NULL) {
      if (!wcscmp(line, L"text")) break;
      wchar_t *f[8];
      int nf = share_split(line, f, 8);
      int kk = nf > 1 ? (int)wcstol(f[1], NULL, 10) : -1;
      if (kk < 0 || kk >= np) continue;
      PcItem *it = &l->it[pack[kk]];
      if (!wcscmp(f[0], L"ck") && nf >= 7)
        pc_row_add(it, wcstol(f[2], NULL, 10), (int)wcstol(f[3], NULL, 10), f[4], f[5], f[6]);
      else if (!wcscmp(f[0], L"cke") && nf >= 3) {
        it->state = PCS_FAIL;
        if (!why[0]) lstrcpynW(why, f[2], whyCap);
      } else if (!wcscmp(f[0], L"ckd")) {
        got[kk] = any = TRUE;
      }
    }
    if (!any) {
      const wchar_t *t = p ? p : L"";
      BOOL old = wcsstr(t, L"Непонятный запрос") != NULL; /* раздающий коллега на старой версии */
      if (!old) ag_pc_why(t, why, whyCap);
      free(a);
      if (old) {
        legacy = 1;
        continue;
      }
      if (!why[0]) lstrcpynW(why, L"PLM не ответил", whyCap);
      for (int i = 0; i < l->n; i++)
        if (l->it[i].state == PCS_TODO) l->it[i].state = PCS_FAIL;
      break;
    }
    free(a);
    int now = 0;
    for (int i = 0; i < np; i++) {
      PcItem *it = &l->it[pack[i]];
      if (got[i] && it->state != PCS_FAIL) pc_judge(it), now++;
      else if (got[i]) now++;
    }
    done += now;
    /* не успели всю пачку — остаток уйдёт следующей; дважды без толку — хватит */
    stuck = now ? 0 : stuck + 1;
    if (stuck >= 2) {
      lstrcpynW(why, L"PLM отвечает слишком долго", whyCap);
      for (int i = 0; i < l->n; i++)
        if (l->it[i].state == PCS_TODO) l->it[i].state = PCS_FAIL;
      break;
    }
    if (progress) progress(done, l->n, ctx);
  }
  if (cancel && *cancel)
    for (int i = 0; i < l->n; i++)
      if (l->it[i].state == PCS_TODO) l->it[i].state = PCS_FAIL;
  ag_pc_user_err(why);
  free(body);
}

static BOOL ag_pc_ext(const wchar_t *path, const wchar_t *const *list, int n) {
  const wchar_t *d = wcsrchr(path, L'.');
  for (int i = 0; d && i < n; i++)
    if (!lstrcmpiW(d, list[i])) return TRUE;
  return FALSE;
}
static const wchar_t *const kAgPcImg[] = {L".tif", L".tiff", L".png", L".jpg", L".jpeg", L".bmp", L".gif"};
static const wchar_t *const kAgPcText[] = {L".txt", L".csv", L".tsv"};

/* Текст Word вместе с таблицами. В тексте документа конец ячейки и конец
   строки таблицы — одинаковый знак, и пустую ячейку от конца строки не
   отличить. Поэтому таблицы в открытой только для чтения копии сначала
   переводятся в текст с табуляциями (ConvertToText) — файл не сохраняется. */
static wchar_t *ag_pc_docx(const wchar_t *path, wchar_t *err, int errCap) {
  OfSession s;
  if (!of_begin(&s, FALSE, path)) {
    swprintf(err, errCap, L"Word не открыл файл (0x%08lx): нужен установленный Office.", (unsigned long)s.hr);
    of_end(&s);
    return NULL;
  }
  IDispatch *tables = of_obj(s.doc, L"Tables", 0, NULL);
  int nt = tables ? (int)of_number(tables, L"Count") : 0;
  for (int i = nt; i >= 1 && i > nt - 60; i--) {
    VARIANT a = of_int(i), sep = of_int(1); /* wdSeparateByTabs */
    IDispatch *t = of_obj(tables, L"Item", 1, &a);
    if (t) of_call(t, L"ConvertToText", DISPATCH_METHOD, NULL, 1, &sep);
    of_release(t);
  }
  of_release(tables);
  IDispatch *content = of_obj(s.doc, L"Content", 0, NULL);
  VARIANT v;
  VariantInit(&v);
  wchar_t *out = NULL;
  if (SUCCEEDED(of_call(content, L"Text", DISPATCH_PROPERTYGET, &v, 0, NULL)) && v.vt == VT_BSTR) {
    size_t n = SysStringLen(v.bstrVal), o = 0;
    out = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    for (size_t i = 0; out && i < n; i++) {
      wchar_t c = v.bstrVal[i];
      if (c == L'\r' && i + 1 < n && v.bstrVal[i + 1] == 7) i++; /* остаток разметки ячейки */
      out[o++] = c == L'\r' || c == 11 ? L'\n' : c == 7 ? L'\t' : c;
    }
    if (out) out[o] = 0;
  } else {
    lstrcpynW(err, L"Word не отдал текст документа.", errCap);
  }
  VariantClear(&v);
  of_release(content);
  of_end(&s);
  return out;
}

/* Скан (TIFF, PNG, JPG…): штамп и спецификация каждой страницы — как в окне
   «Распознать чертёж». Обозначение и наименование — из граф, не из модели. */
static int ag_pc_drawing(PcList *l, const wchar_t *path, volatile LONG *cancel, wchar_t *err, int errCap) {
  HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  IWICImagingFactory *f = NULL;
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  DrPages pages;
  memset(&pages, 0, sizeof(pages));
  int found = 0, read = 0;
  if (!f) {
    lstrcpynW(err, L"Windows не открыл картинку (нет WIC).", errCap);
  } else {
    HRESULT hr = tm_probe(f, path, FALSE, dr_pages_cb, &pages);
    if (FAILED(hr) || !pages.n) {
      wchar_t w[120];
      tm_why(hr, w, 120);
      swprintf(err, errCap, L"Скан не открылся: %ls.", w);
    }
    for (int i = 0; i < pages.n && i < 12 && !(cancel && *cancel); i++) {
      DrPage *pg = &pages.p[i];
      dr_recognize(f, pg);
      read++;
      if (pg->f[DF_DES][0])
        found += pc_add_field(l, pg->f[DF_DES], pg->f[DF_NAME], i + 1, pg->doubt[DF_DES] || pg->doubt[DF_NAME]);
      for (int r = 0; r < pg->nrows; r++)
        found += pc_add_field(l, pg->rows[r].c[DS_DES], pg->rows[r].c[DS_NAME], i + 1,
                              pg->rows[r].doubt[DS_DES] || pg->rows[r].doubt[DS_NAME]);
      if (!found && pg->note[0] && !err[0]) swprintf(err, errCap, L"Лист %d: %ls.", i + 1, pg->note);
    }
    if (pages.n > 12 && errCap > 0 && !err[0]) lstrcpynW(err, L"Прочитаны первые 12 страниц.", errCap);
  }
  for (int i = 0; i < pages.n; i++) dr_page_free(&pages.p[i]);
  free(pages.p);
  if (f) IWICImagingFactory_Release(f);
  if (SUCCEEDED(co)) CoUninitialize();
  if (read && !found && !err[0]) lstrcpynW(err, L"На скане не прочиталось ни одного обозначения (штамп или спецификация).", errCap);
  return found;
}

/* Позиции из файла: таблица Excel, Word, CSV/TXT или скан чертежа. src — подпись
   «откуда» для итога; FALSE — файл не прочитан (в err — почему). */
static BOOL ag_pc_from_file(PcList *l, const wchar_t *path, volatile LONG *cancel, wchar_t *src, int srcCap,
                            wchar_t *err, int errCap) {
  const wchar_t *base = wcsrchr(path, L'\\');
  base = base ? base + 1 : path;
  DWORD at = GetFileAttributesW(path);
  if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY)) {
    swprintf(err, errCap, L"Файла %ls нет или это папка.", path);
    return FALSE;
  }
  if (ag_pc_ext(path, kAgPcImg, (int)(sizeof(kAgPcImg) / sizeof(kAgPcImg[0])))) {
    swprintf(src, srcCap, L"скан %ls (штамп и спецификация)", base);
    ag_pc_drawing(l, path, cancel, err, errCap);
    return l->n > 0;
  }
  wchar_t *text = NULL;
  if (of_excel(path)) {
    OfTable t;
    if (of_table_load(path, NULL, &t, err, errCap)) {
      size_t cap = 1;
      for (int i = 0; i < t.rows * t.cols; i++) cap += wcslen(t.cell[i]) + 1;
      text = (wchar_t *)malloc(cap * sizeof(wchar_t));
      if (text) {
        size_t o = 0;
        for (int r = 0; r < t.rows; r++)
          for (int c = 0; c < t.cols; c++) {
            const wchar_t *v = t.cell[r * t.cols + c];
            for (; *v; v++) text[o++] = *v == L'\t' || *v == L'\n' || *v == L'\r' ? L' ' : *v;
            text[o++] = c + 1 < t.cols ? L'\t' : L'\n';
          }
        text[o] = 0;
      }
      of_table_free(&t);
    }
    swprintf(src, srcCap, L"Excel %ls, первый лист", base);
  } else if (of_docx(path)) {
    text = ag_pc_docx(path, err, errCap);
    swprintf(src, srcCap, L"Word %ls", base);
  } else if (ag_pc_ext(path, kAgPcText, (int)(sizeof(kAgPcText) / sizeof(kAgPcText[0])))) {
    text = mtw_read_file(path);
    swprintf(src, srcCap, L"файл %ls", base);
    if (!text) swprintf(err, errCap, L"Файл %ls не прочитался.", base);
  } else {
    lstrcpynW(err, L"Сверяю списки из .xlsx, .docx, .csv, .txt и сканы .tif/.png/.jpg. PDF и .xls пока нет — сохраните лист в TIFF или таблицу в .xlsx/CSV.", errCap);
    return FALSE;
  }
  if (!text) return FALSE;
  pc_parse(l, text, 0);
  free(text);
  return TRUE;
}

/* plm_check для модели: итог целиком — в *report (его показывают как ответ),
   модели — коротко сколько чего. */
static int ag_plm_check(const AgJson *j, AgFs *fs, wchar_t *out, int cap, volatile LONG *cancel, wchar_t **report) {
  const wchar_t *text = ag_get(j, L"text"), *path = ag_get(j, L"path");
  PcList l = {(PcItem *)calloc(PC_MAX, sizeof(PcItem)), 0, 0, 0};
  if (!l.it) return -1;
  wchar_t src[400] = L"", err[600] = L"", why[600] = L"";
  BOOL ok = TRUE;
  if (path[0]) {
    wchar_t actual[MAX_PATH];
    if (!fs || !ag_path(fs, path, actual)) lstrcpynW(actual, path, MAX_PATH);
    ok = ag_pc_from_file(&l, actual, cancel, src, 400, err, 600);
  } else {
    wchar_t low[40];
    lstrcpynW(low, text, 40);
    CharLowerBuffW(low, (DWORD)wcslen(low));
    if (wcsstr(low, L"буфер") && wcslen(text) < 40) {
      wchar_t *clip = mtw_clip_text();
      if (clip) pc_parse(&l, clip, 0);
      else lstrcpynW(err, L"В буфере обмена нет текста.", 600);
      ok = clip != NULL;
      free(clip);
      lstrcpynW(src, L"буфер обмена", 400);
    } else {
      pc_parse(&l, text, 1);
      lstrcpynW(src, L"список из задачи", 400);
    }
  }
  if (!ok || !l.n) {
    swprintf(out, cap, L"plm_check: %ls", err[0] ? err : L"в списке нет ни одного обозначения (шаблон штампа: «АДЕ 3424-123.01»).");
    free(l.it);
    return ok ? 0 : -1;
  }
  ag_pc_query(&l, cancel, NULL, NULL, why, 600);
  *report = pc_report(&l, src, why[0] ? why : err);
  int cnt[7] = {0};
  for (int i = 0; i < l.n; i++) cnt[l.it[i].state]++;
  swprintf(out, cap, L"Сверка с PLM «Союз» выполнена (только чтение): позиций %d — совпадает %d, наименование иначе %d, другое %d, похожие %d, нет %d, не проверено %d. Итог показан пользователю целиком.",
           l.n, cnt[PCS_OK], cnt[PCS_NEAR], cnt[PCS_DIFF], cnt[PCS_SIMILAR], cnt[PCS_NONE], cnt[PCS_FAIL]);
  free(l.it);
  return 0;
}

/* Один инструмент данных. 0 — наблюдение для модели; *got — в нём настоящие
   данные PLM или архива (а не отказ или «не нашлось»). */
static int ag_data_call(const AgJson *j, AgFs *fs, wchar_t *out, int cap, volatile LONG *cancel, BOOL *got,
                        wchar_t **report) {
  const wchar_t *name = ag_get(j, L"name");
  const wchar_t *text = ag_get(j, L"text");
  *got = FALSE;
  if (!ag_data_tool(name)) {
    lstrcpynW(out, L"Неизвестная команда CursorPad. Используй app name=capabilities; произвольные команды запрещены.", cap);
    return -1;
  }
  if (!wcscmp(name, L"std_find")) { /* таблицы стандартов — не данные завода: и для Z.ai */
    if (!text[0]) {
      lstrcpynW(out, L"std_find: нужен text — изделие, размер, марка или стандарт: «болт М12 ГОСТ 7798», «40Х».", cap);
      return -1;
    }
    wchar_t *cite = NULL, why[600];
    wchar_t *t = gs_answer(text, &cite, why, 600);
    if (t) ag_fit(L"Справочник стандартов на этом компьютере (только чтение; числа — из таблиц, бери их дословно):\r\n", t, out, cap);
    else swprintf(out, cap, L"%ls. Чисел из справочника нет — не называй размеры, состав и свойства по памяти, скажи об этом.", why);
    *got = t != NULL;
    free(t);
    free(cite);
    return 0;
  }
  if (g_aiProvider && !g_aiZaiPlm) {
    lstrcpynW(out, L"Данные PLM и архива не передаются в облачный Z.ai: разрешения нет (Настройки → ИИ → «Z.ai может читать PLM «Союз» и архив»). Скажи об этом пользователю; данных нет, не выдумывай их.", cap);
    return 0;
  }
  if (!wcscmp(name, L"plm_check")) {
    if (!text[0] && !ag_get(j, L"path")[0]) {
      lstrcpynW(out, L"plm_check: нужен text — список обозначений (или «буфер») — либо path — файл со списком или скан.", cap);
      return -1;
    }
    int r = ag_plm_check(j, fs, out, cap, cancel, report);
    *got = r == 0 && *report;
    return r;
  }
  if (!text[0]) {
    swprintf(out, cap, L"%ls: нужен text — обозначение или слова запроса.", name);
    return -1;
  }
  if (!wcscmp(name, L"archive_find")) {
    *got = ag_archive_find(text, out, cap) > 0;
    return 0;
  }
  if (!wcscmp(name, L"plm_search")) {
    AgPlmRow *rows = (AgPlmRow *)calloc(40, sizeof(AgPlmRow));
    if (!rows) return -1;
    int n = ag_plm_search(text, rows, 40, cancel, out, cap);
    if (n > 0) ag_plm_rows_text(text, rows, n, out, cap);
    free(rows);
    *got = n > 0;
    return 0;
  }
  long id = ag_plm_pick(text, cancel, out, cap, got);
  if (!id) return 0;
  BOOL used = !wcscmp(name, L"plm_where_used");
  wchar_t *a = ag_plm_card(id, used, cancel, out, cap);
  if (!a) return 0;
  const wchar_t *t = ag_plm_text(a);
  wchar_t head[200];
  if (used) {
    /* в «Все данные» входимость — свой раздел; остальное модели не нужно */
    const wchar_t *from = wcsstr(t, L"ВХОДИМОСТЬ ПО ТЕХСОСТАВУ");
    if (!from) {
      swprintf(out, cap, L"Входимость IO.%ld: раздела нет в ответе PLM.\r\n%.600ls", id, t);
    } else {
      size_t len = wcslen(from);
      const wchar_t *stops[] = {L"\r\nЗАГОТОВК", L"\r\nТЕХСОСТАВ", L"\r\nФАЙЛЫ В АРХИВЕ", L"\r\n── время"};
      for (size_t k = 0; k < sizeof(stops) / sizeof(stops[0]); k++) {
        const wchar_t *e = wcsstr(from, stops[k]);
        if (e && (size_t)(e - from) < len) len = (size_t)(e - from);
      }
      swprintf(head, 200, L"PLM «Союз», входимость IO.%ld:\r\n", id);
      wchar_t *part = (wchar_t *)calloc(len + 1, sizeof(wchar_t));
      if (part) {
        memcpy(part, from, len * sizeof(wchar_t));
        ag_fit(head, part, out, cap);
        free(part);
      }
      *got = TRUE;
    }
  } else {
    swprintf(head, 200, L"Карточка PLM «Союз» IO.%ld (только чтение):\r\n", id);
    ag_fit(head, t, out, cap);
    *got = TRUE;
  }
  free(a);
  return 0;
}

static int ag_app_call(const AgJson *j, wchar_t *out, int cap) {
  const wchar_t *name = ag_get(j, L"name");
  if (!wcscmp(name, L"capabilities")) {
    lstrcpynW(out, L"Доступны filesystem (локальный Filesystem MCP, только list/info/read), excel_write (XlsxWriter для новой книги, mode=table для шапки/фильтра; формулы и копии через установленный Excel), office_read (.docx/.xlsx через Office), compare_materials (ВСЕ строки двух локальных .xlsx, одна может быть выгрузкой СОЮЗ).\r\napp name=plm_status — только состояние реально загруженной выгрузки.\r\n", cap);
    for (size_t i = 0; i < sizeof(kAgCapabilities) / sizeof(kAgCapabilities[0]); i++)
      swprintf(out + wcslen(out), cap - wcslen(out), L"app name=%ls — %ls.\r\n", kAgCapabilities[i].name, kAgCapabilities[i].description);
    for (size_t i = 0; i < sizeof(kAgDataTools) / sizeof(kAgDataTools[0]); i++)
      swprintf(out + wcslen(out), cap - wcslen(out), L"app name=%ls text=… — только чтение: %ls.\r\n", kAgDataTools[i].name, kAgDataTools[i].description);
    wcscat(out, L"Открытие окна не означает выполнение расчёта или сравнения. Прямой доступ к химическому составу материалов СОЮЗ ещё не настроен.\r\n");
    return 0;
  }
  if (!wcscmp(name, L"plm_status")) {
    if (g_xpBusy) { lstrcpynW(out, L"Выгрузка PLM сейчас выполняется; результаты ещё не готовы. Не используй промежуточные строки.", cap); return 0; }
    if (!g_xpJob) { lstrcpynW(out, L"В этой сессии выгрузка из СОЮЗ/PLM не загружена. Для сравнения нужен реальный файл выгрузки .xlsx либо окно app open_export. Данные не получены; не выдумывай их.", cap); return 0; }
    swprintf(out, cap, L"В текущей сессии есть выгрузка PLM: состав — %d строк, материалы операций — %d строк, заготовки — %d строк. Для пакетного сравнения сохраните выгрузку в .xlsx и передайте путь compare_materials. Само сравнение пока не выполнено.", g_xpJob->t[XP_COMP].n, g_xpJob->t[XP_MAT].n, g_xpJob->t[XP_PF].n); return 0;
  }
  for (size_t i = 0; i < sizeof(kAgCapabilities) / sizeof(kAgCapabilities[0]); i++) {
    if (wcscmp(name, kAgCapabilities[i].name)) continue;
    kAgCapabilities[i].show();
    swprintf(out, cap, L"Открыт инструмент CursorPad: %ls. Это только открытие окна. Данные не прочитаны и операция не выполнена автоматически.", kAgCapabilities[i].description); return 0;
  }
  lstrcpynW(out, L"Неизвестная команда CursorPad. Используй app name=capabilities; произвольные команды запрещены.", cap); return -1;
}
