/* ai_plm_check.c — «Спросить ИИ»: сверка с PLM «Союз» (с 2026.10.23).
   «есть ли в Союзе АДЕ 3424-123.01», «сверь с союзом список из буфера»,
   «сравни C:\…\Спецификация.xlsx с PLM», картинка чертежа + «проверь в Союзе».
   Обозначения и наименования берёт код (plm_check.c), Союз читает исполнитель
   запросов (ai_app_tools.c → share.c), итог — готовым списком: что есть, чего
   нет, где наименование другое. Модель не нужна и итог не пересказывает, так
   что сверка работает и без загруженной модели. Союз только читается.
   Подключается из ai_chat.c после ai_agent.c. */

struct AcPcJob {
  wchar_t *text;          /* вопрос (chat) или текст буфера */
  BOOL chat;
  wchar_t path[MAX_PATH]; /* файл со списком или скан */
  BYTE *png;              /* картинка из чата — чертёж */
  DWORD pngLen;
  wchar_t src[300];
  volatile LONG cancel;
  wchar_t *report;
};
static struct AcPcJob *g_acPc;

static BOOL ac_pc_busy(void) { return g_acPc != NULL; }

/* Путь к файлу в тексте: «C:\…» или «\\сервер\…». Имена с пробелами — путь
   укорачивается по словам с конца, пока файл не найдётся. */
static BOOL ac_pc_path(const wchar_t *q, wchar_t *out) {
  for (const wchar_t *p = q; *p; p++) {
    BOOL drive = iswalpha(p[0]) && p[1] == L':' && p[2] == L'\\';
    BOOL unc = p[0] == L'\\' && p[1] == L'\\' && iswalnum(p[2]);
    if ((!drive && !unc) || (p > q && iswalnum(p[-1]))) continue;
    wchar_t buf[MAX_PATH];
    int n = 0;
    while (p[n] && p[n] != L'\n' && p[n] != L'\r' && p[n] != L'"' && p[n] != 0xAB && p[n] != 0xBB && n < MAX_PATH - 1)
      buf[n] = p[n], n++;
    buf[n] = 0;
    while (n > 3) {
      while (n > 3 && wcschr(L" .,;:!?)", buf[n - 1])) buf[--n] = 0;
      DWORD a = GetFileAttributesW(buf);
      if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        lstrcpynW(out, buf, MAX_PATH);
        return TRUE;
      }
      while (n > 3 && buf[n - 1] != L' ') n--;
      buf[n] = 0;
    }
  }
  return FALSE;
}

/* Просьба сверить с Союзом. Вопрос про одно изделие с материалом, ТП или
   составом — это карточка (помощник с plm_card), а сравнение материалов и
   выгрузок — свои окна и compare_materials. */
static BOOL ac_pc_wanted(const wchar_t *q) {
  wchar_t low[2000];
  lstrcpynW(low, q, 2000);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  if (!wcsstr(low, L"союз") && !wcsstr(low, L"plm") && !wcsstr(low, L"плм")) return FALSE;
  static const wchar_t *const ask[] = {L"есть ли", L"есть в ", L"нет ли", L"нет в ", L"налич", L"сравн",
                                       L"свер", L"провер", L"имеется", L"имеются", L"заведен", L"отсутств",
                                       L"совпада", L"расхожд", L"чего нет", L"каких нет", L"есть такое",
                                       L"есть такой", L"есть такая"};
  BOOL want = FALSE;
  for (size_t i = 0; i < sizeof(ask) / sizeof(ask[0]) && !want; i++) want = wcsstr(low, ask[i]) != NULL;
  if (!want) return FALSE;
  static const wchar_t *const other[] = {L"материал", L"выгрузк", L"химсостав", L"маршрут"};
  for (size_t i = 0; i < sizeof(other) / sizeof(other[0]); i++)
    if (wcsstr(low, other[i])) return FALSE;
  wchar_t path[MAX_PATH];
  if (g_acImg || g_acImgFull || wcsstr(low, L"буфер") || wcsstr(low, L"скопир") || ac_pc_path(q, path)) return TRUE;
  wchar_t p[SR_Q], u[SR_Q];
  sr_prep(q, p, SR_Q);
  int i = 0;
  for (; p[i]; i++) u[i] = sr_upper(p[i]);
  u[i] = 0;
  int sb[4], se[4], des = pc_spans(p, u, sb, se, 4);
  /* без обозначения весь вопрос — поиск по наименованию; «проверь связь с
     Союзом» — не о наименовании, это к модели */
  static const wchar_t *const sys[] = {L"подключ", L"связ", L"соедин", L"логин", L"парол", L"настро", L"доступ",
                                       L"работа", L"сервер", L"ошибк", L"обновл", L"верси", L"лиценз", L"программ",
                                       L"клиент", L"раздав", L"коллег"};
  for (size_t k = 0; k < sizeof(sys) / sizeof(sys[0]) && !des; k++)
    if (wcsstr(low, sys[k])) return FALSE;
  static const wchar_t *const card[] = {L"заготов", L"техпроц", L"операц", L"входит", L"входимост", L"масс",
                                        L"состав", L"чертёж", L"чертеж", L"скан"};
  BOOL detail = FALSE;
  for (size_t k = 0; k < sizeof(card) / sizeof(card[0]) && !detail; k++) detail = wcsstr(low, card[k]) != NULL;
  /* про одно изделие подробности — к карточке; список — всё равно сверка */
  return !detail || des >= 2;
}

static void ac_pc_progress(int done, int total, void *ctx) {
  (void)ctx;
  wchar_t *t = (wchar_t *)malloc(sizeof(wchar_t) * 160);
  if (!t) return;
  swprintf(t, 160, L"Сверяю с PLM «Союз»: %d из %d · только чтение · Стоп — остановить", done, total);
  if (!g_acWnd || !PostMessageW(g_acWnd, WM_AC_PC, 1, (LPARAM)t)) free(t);
}

static DWORD WINAPI ac_pc_thread(LPVOID arg) {
  struct AcPcJob *j = (struct AcPcJob *)arg;
  PcList l = {(PcItem *)calloc(PC_MAX, sizeof(PcItem)), 0, 0, 0};
  wchar_t err[600] = L"", why[600] = L"";
  if (!l.it) {
    j->report = _wcsdup(L"Не хватило памяти для сверки.");
  } else {
    BOOL from = TRUE;
    if (j->path[0]) {
      from = ag_pc_from_file(&l, j->path, &j->cancel, j->src, 300, err, 600);
    } else if (j->png) {
      wchar_t tmp[MAX_PATH], path[MAX_PATH];
      GetTempPathW(MAX_PATH, tmp);
      _snwprintf(path, MAX_PATH, L"%scursorpad-plmcheck-%lu-%lu.png", tmp, GetCurrentThreadId(), GetTickCount());
      path[MAX_PATH - 1] = 0;
      HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
      DWORD wr = 0;
      BOOL ok = h != INVALID_HANDLE_VALUE && WriteFile(h, j->png, j->pngLen, &wr, NULL) && wr == j->pngLen;
      if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
      lstrcpynW(j->src, L"картинка из чата (штамп и спецификация)", 300);
      if (ok) ag_pc_drawing(&l, path, &j->cancel, err, 600);
      else lstrcpynW(err, L"Картинку не удалось сохранить для распознавания.", 600);
      DeleteFileW(path);
      from = l.n > 0;
    } else if (j->text) {
      pc_parse(&l, j->text, j->chat);
    }
    if (l.n) ag_pc_query(&l, &j->cancel, ac_pc_progress, j, why, 600);
    if (!l.n && !from && err[0]) {
      size_t cap = wcslen(err) + 80;
      j->report = (wchar_t *)malloc(cap * sizeof(wchar_t));
      if (j->report) swprintf(j->report, cap, L"Список для сверки с Союзом не получен: %ls", err);
    } else {
      j->report = pc_report(&l, j->src, why[0] ? why : err);
    }
    free(l.it);
  }
  if (!g_acWnd || !PostMessageW(g_acWnd, WM_AC_PC, 0, (LPARAM)j)) {
    free(j->text);
    free(j->png);
    free(j->report);
    free(j);
  }
  return 0;
}

static BOOL ac_pc_start(const wchar_t *a) {
  struct AcPcJob *j = (struct AcPcJob *)calloc(1, sizeof(struct AcPcJob));
  if (!j) return FALSE;
  wchar_t low[2000];
  lstrcpynW(low, a, 2000);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  const BYTE *img = g_acImgFull ? g_acImgFull : g_acImg;
  DWORD imgLen = g_acImgFull ? g_acImgFullLen : g_acImgLen;
  if (ac_pc_path(a, j->path)) {
    /* подпись — в потоке, по виду файла */
  } else if (img) {
    j->png = (BYTE *)malloc(imgLen);
    if (!j->png) {
      free(j);
      return FALSE;
    }
    memcpy(j->png, img, imgLen);
    j->pngLen = imgLen;
  } else if (wcsstr(low, L"буфер") || wcsstr(low, L"скопир")) {
    j->text = mtw_clip_text();
    lstrcpynW(j->src, L"буфер обмена", 300);
    if (!j->text || !j->text[0]) {
      free(j->text);
      free(j);
      ac_push(0, a);
      ac_push(1, L"В буфере обмена нет текста. Скопируйте ячейки Excel (обозначение и наименование) или список и повторите.");
      ac_redraw();
      return TRUE;
    }
  } else {
    j->text = _wcsdup(a);
    j->chat = TRUE;
    lstrcpynW(j->src, L"", 300);
    if (!j->text) {
      free(j);
      return FALSE;
    }
  }
  ac_push(0, a);
  if (g_acImg && g_acN) { /* миниатюра вопроса — как у маршрута по чертежу */
    g_acTurns[g_acN - 1].png = g_acImg;
    g_acTurns[g_acN - 1].pngLen = g_acImgLen;
    g_acImg = NULL;
    g_acImgLen = 0;
  }
  free(g_acImgFull);
  g_acImgFull = NULL;
  g_acImgFullLen = 0;
  ac_relayout();
  HANDLE th = CreateThread(NULL, 0, ac_pc_thread, j, 0, NULL);
  if (!th) {
    free(j->text);
    free(j->png);
    free(j);
    ac_push(1, L"Не вышло запустить сверку с Союзом.");
    ac_redraw();
    return TRUE;
  }
  CloseHandle(th);
  g_acPc = j;
  ac_redraw();
  ac_busy(TRUE);
  ac_status(L"Сверяю с PLM «Союз» — только чтение · Стоп — остановить");
  return TRUE;
}

/* wp 1 — строка состояния (free), 0 — сверка закончилась */
static void ac_pc_ready(WPARAM wp, LPARAM lp) {
  if (wp == 1) {
    wchar_t *t = (wchar_t *)lp;
    if (g_acPc && t) ac_status(t);
    free(t);
    return;
  }
  struct AcPcJob *j = (struct AcPcJob *)lp;
  if (!j) return;
  if (j == g_acPc) g_acPc = NULL;
  ac_busy(FALSE);
  if (j->report) {
    ac_push(1, j->report);
    /* в облачный Z.ai данные Союза без разрешения не уходят и потом, с беседой */
    if (g_aiProvider && !g_aiZaiPlm) g_acContextStart = g_acN;
  }
  ac_redraw();
  ac_status(j->cancel ? L"Сверка остановлена — непроверенные позиции помечены"
                      : L"Сверка с PLM «Союз» — только чтение, ничего не изменено");
  free(j->text);
  free(j->png);
  free(j->report);
  free(j);
}

static BOOL ac_pc_cancel(void) {
  if (!g_acPc) return FALSE;
  InterlockedExchange(&g_acPc->cancel, 1);
  ac_status(L"Останавливаю сверку после текущего запроса…");
  return TRUE;
}
