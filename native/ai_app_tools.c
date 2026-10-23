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

/* Один инструмент данных. 0 — наблюдение для модели; *got — в нём настоящие
   данные PLM или архива (а не отказ или «не нашлось»). */
static int ag_data_call(const AgJson *j, wchar_t *out, int cap, volatile LONG *cancel, BOOL *got) {
  const wchar_t *name = ag_get(j, L"name");
  const wchar_t *text = ag_get(j, L"text");
  *got = FALSE;
  if (!ag_data_tool(name)) {
    lstrcpynW(out, L"Неизвестная команда CursorPad. Используй app name=capabilities; произвольные команды запрещены.", cap);
    return -1;
  }
  if (g_aiProvider && !g_aiZaiPlm) {
    lstrcpynW(out, L"Данные PLM и архива не передаются в облачный Z.ai: разрешения нет (Настройки → ИИ → «Z.ai может читать PLM «Союз» и архив»). Скажи об этом пользователю; данных нет, не выдумывай их.", cap);
    return 0;
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
