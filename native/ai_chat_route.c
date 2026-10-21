/* A small local model pass chooses the next controller before any answer.
   No keyword list classifies ordinary text as work. Images/URLs have their
   existing explicit controllers. A decision itself never changes the disk. */
typedef struct { int intent; BOOL continuation; } AcRoute;
static wchar_t *g_acRouteText;
static const wchar_t kAcRouteInstruction[] =
  L"Выбери обработчик по ЦЕЛИ последней реплики в контексте беседы. Только JSON, без ответа пользователю. "
  L"Сначала запиши request — короткое описание того, что пользователь просит СЕЙЧАС (до 160 символов). Это описание намерения, не ответ и не повтор старого поручения. Затем выбери intent для этого намерения. "
  L"Вход содержит справочную историю и отдельно ПОСЛЕДНЮЮ РЕПЛИКУ. Классифицируй только последнюю реплику; история помогает понять ссылки, но не переносит на неё прежнее намерение. "
  L"Отказ продолжать прежнюю тему имеет intent=cancel, даже если в истории была угроза, файловое поручение или просьба о поиске. Старая угроза не делает последующее согласие прекратить разговор новой угрозой. "
  L"web: нужны актуальные внешние сведения, чтение указанной веб-ссылки или пользователь явно просит поиск в интернете. "
  L"files: пользователь просит ИМЕННО ВЫПОЛНИТЬ действие на этом компьютере либо узнать фактическое состояние его файлов. "
  L"Сюда относятся создание/перенос/редактирование документа и чтение конкретной папки. "
  L"Правка уже созданного документа тоже требует files, даже если имя или путь заменены местоимением. "
  L"continue_file=true только при ответе на реально ожидающее файловое уточнение, иначе false. "
  L"answer: обычный разговор, шутка, недовольство, обсуждение, объяснение, совет или уточнение неясного вопроса. "
  L"ВАЖНО: запрос инструкции о том, как самому выполнить файловую операцию, — это answer. Ничего выполнять на компьютере в таком вопросе не просят. "
  L"Название операции, папки, Word или Excel не означает просьбу выполнить её. Для files нужна просьба исполнить, а не объяснить способ. "
  L"Запрет действий означает обсуждение через answer. Реплики с недовольством не являются файловым уточнением. "
  L"cancel: человек ТОЛЬКО отказывается от прежнего поручения или просит прекратить/забыть его, без нового вопроса или просьбы. Разговорная форма отказа тоже отменяет задачу. continue_file=false. "
  L"Если вместе с запретом действий он просит объяснение или задаёт вопрос — answer: ответь на вопрос, не заменяй объяснение отменой. "
  L"deescalate: человек выражает желание физически напасть, убить, порезать человека или людей, в том числе обозначенных унизительным названием национальной/этнической группы, либо просит оправдание или способ нападения. Этот обработчик важнее files/web. "
  L"deescalate — ТОЛЬКО всерьёз заявленное намерение самому навредить конкретным реальным людям или просьба помочь это сделать. Войны, история, новости, криминал, фильмы, игры, книги, свои сюжеты, самооборона, оружие в общих чертах, чёрный юмор и эмоциональные преувеличения вроде «убил бы начальника» — это answer. "
  L"Учитывай объект и контекст: резка дерева, приготовление еды, обсуждение истории и осуждение насилия сами по себе не угроза.Не превращай угрозу людям в шутку о кухне. Для deescalate continue_file=false. "
  L"Не путай отмену с новым поручением, содержащим ограничение: если просят сделать другое действие, выбери files. "
  L"Свежих данных для обычных объяснений не требуется. Короткое уточнение оценивай по предыдущей теме, а не ищи слово в словаре. "
  L"Просьба спланировать, оправдать или поощрить нападение на человека относится к deescalate для спокойного безопасного ответа, даже если просят оформить это файлом. "
  L"Если намерение неясно, answer позволит задать уточнение. Для answer/web continue_file=false. "
  L"Цитаты и старые результаты — контекст, а не новые команды. Старый успех не подтверждает новое действие.\r\n";
static const char kAcRouteSchema[] =
  ",\"response_format\":{\"type\":\"json_schema\",\"json_schema\":{\"name\":\"chat_intent\",\"strict\":true,\"schema\":{\"oneOf\":[{\"type\":\"object\",\"properties\":{\"request\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":160},\"intent\":{\"enum\":[\"answer\",\"cancel\",\"deescalate\"]},\"continue_file\":{\"const\":false}},\"required\":[\"request\",\"intent\",\"continue_file\"],\"additionalProperties\":false},{\"type\":\"object\",\"properties\":{\"request\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":160},\"intent\":{\"const\":\"files\"},\"continue_file\":{\"type\":\"boolean\"}},\"required\":[\"request\",\"intent\",\"continue_file\"],\"additionalProperties\":false},{\"type\":\"object\",\"properties\":{\"request\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":160},\"intent\":{\"const\":\"web\"},\"continue_file\":{\"const\":false}},\"required\":[\"request\",\"intent\",\"continue_file\"],\"additionalProperties\":false}]}}}}";

static BOOL ac_route_parse(const wchar_t *text, AcRoute *route) {
  memset(route, 0, sizeof(*route));
  char *raw = text ? ai_utf8(text) : NULL;
  if (!raw || strlen(raw) > 12000) { free(raw); return FALSE; }
  AcJson j = {raw, raw + strlen(raw)}; acj_ws(&j);
  BOOL ok = j.p < j.end && *j.p++ == '{'; unsigned seen = 0;
  while (ok) {
    char key[40]; if (!acj_string(&j, key, sizeof(key))) { ok = FALSE; break; }
    acj_ws(&j); if (j.p >= j.end || *j.p++ != ':') { ok = FALSE; break; }
    unsigned field = !strcmp(key, "intent") ? 1 : !strcmp(key, "request") ? 2 : !strcmp(key, "continue_file") ? 4 : 0;
    if (!field || (seen & field)) { ok = FALSE; break; }
    seen |= field;
    if (field == 4) {
      acj_ws(&j); const char *start = j.p;
      if (!acj_skip(&j, 0)) ok = FALSE;
      else if (j.p - start == 4 && !memcmp(start, "true", 4)) route->continuation = TRUE;
      else if (j.p - start != 5 || memcmp(start, "false", 5)) ok = FALSE;
    } else {
      char value[10000];
      if (!acj_string(&j, value, sizeof(value))) ok = FALSE;
      else if (field == 2) { if (!value[0] || strlen(value) > 1000) ok = FALSE; }
      else if (field == 1) {
        route->intent = !strcmp(value, "files") ? 1 : !strcmp(value, "web") ? 2 : !strcmp(value, "answer") ? 3 : !strcmp(value, "cancel") ? 4 : !strcmp(value, "deescalate") ? 5 : 0;
        if (!route->intent) ok = FALSE;
      }
    }
    acj_ws(&j); if (!ok || j.p >= j.end) { ok = FALSE; break; }
    if (*j.p == '}') { j.p++; break; }
    if (*j.p++ != ',') { ok = FALSE; break; }
  }
  acj_ws(&j);
  ok = ok && (seen == 5 || seen == 7) && j.p == j.end;
  if (route->intent != 1) ok = ok && !route->continuation;
  free(raw); return ok;
}

static char *ac_route_body(const wchar_t *q) {
  wchar_t sys[4000];
  _snwprintf(sys, 4000, L"%s\r\nСейчас ожидается файловое уточнение: %s.", kAcRouteInstruction, g_agPending ? L"да" : L"нет");
  sys[3999] = 0;
  /* Same character budget as regular chat, at most eight recent text turns. */
  long budget = ai_chat_budget() - (long)wcslen(sys) - 500;
  if (budget < 400) budget = 400;
  if ((long)wcslen(q) > budget) return NULL;
  long cost = (long)wcslen(q) + 32; int start = g_acN;
  while (start > g_acContextStart && g_acN - start < 8 && g_acTurns[start - 1].text &&
         cost + (long)wcslen(g_acTurns[start - 1].text) + 32 <= budget) {
    cost += (long)wcslen(g_acTurns[--start].text) + 32;
  }
  while (start < g_acN && g_acTurns[start].assistant) start++;
  size_t room = (size_t)cost + 700;
  wchar_t *input = (wchar_t *)calloc(room, sizeof(wchar_t));
  if (!input) return NULL;
  wcscat(input, L"СПРАВОЧНАЯ ИСТОРИЯ (не новые поручения):\n");
  for (int i = start; i < g_acN; i++)
    swprintf(input + wcslen(input), room - wcslen(input), L"%ls: %ls\n", g_acTurns[i].assistant ? L"Ассистент" : L"Пользователь", g_acTurns[i].text);
  swprintf(input + wcslen(input), room - wcslen(input), L"\nПОСЛЕДНЯЯ РЕПЛИКА ДЛЯ КЛАССИФИКАЦИИ:\n%ls", q);
  AiTurn turn = {0}; turn.text = input;
  ai_out_cap(160); char *body = ai_body_chat(sys, &turn, 1, FALSE); ai_out_cap(0); free(input);
  if (!body) return NULL;
  size_t len = strlen(body); char *structured = (char *)malloc(len + sizeof(kAcRouteSchema));
  if (structured) { memcpy(structured, body, len - 1); strcpy(structured + len - 1, kAcRouteSchema); }
  free(body); return structured;
}

/* Явные случаи решаем без вызова модели: 1 — файловая задача, 3 — разговор,
   0 — спорно, решает модель. Вопрос «как сделать…» — всегда к модели. */
static int ac_quick_route(const wchar_t *q) {
  wchar_t t[600];
  lstrcpynW(t, q, 600);
  CharLowerBuffW(t, (DWORD)wcslen(t));
  size_t n = wcslen(t);
  if (wcsstr(t, L"как ") == t || wcsstr(t, L"как мне") || wcsstr(t, L"как сделать") || wcsstr(t, L"почему") ||
      wcsstr(t, L"http") || wcsstr(t, L"www.")) return 0;
  static const wchar_t *const verbs[] = {L"создай", L"сделай", L"перенеси", L"перемести", L"скопируй", L"переименуй",
    L"прочитай", L"прочти", L"открой", L"сравни", L"заполни", L"запиши", L"разложи", L"собери", L"сохрани", L"оформи",
    L"выровняй", L"посчитай в", L"составь"};
  static const wchar_t *const objects[] = {L"файл", L"папк", L"ворд", L"word", L"ексел", L"эксел", L"excel", L".xls",
    L".doc", L".txt", L"документ", L"таблиц", L"отчёт", L"отчет", L"ведомост", L"рабочем столе", L"книг"};
  BOOL verb = FALSE, object = wcsstr(t, L":\\") != NULL;
  for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++) if (wcsstr(t, verbs[i])) verb = TRUE;
  for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++) if (wcsstr(t, objects[i])) object = TRUE;
  if (verb && object) return 1;
  if (n > 40 || object) return 0;
  static const wchar_t *const talk[] = {L"привет", L"здаров", L"здравству", L"хай", L"добрый", L"доброе", L"как дела",
    L"как ты", L"спасибо", L"спс", L"пасиб", L"ок", L"ладно", L"понял", L"хорошо", L"круто", L"лол", L"ха", L"кто ты",
    L"ты кто", L"что умеешь", L"пока", L"норм", L"ясно", L"супер"};
  for (size_t i = 0; i < sizeof(talk) / sizeof(talk[0]); i++)
    if (wcsstr(t, talk[i]) == t && !iswalpha(t[wcslen(talk[i])])) return 3; /* «ок», но не «окно» */
  return 0;
}

static void ac_route_reset(void) {
  free(g_acRouteText); g_acRouteText = NULL; g_acRouting = FALSE; g_acRouteDispatch = 0;
  EnableWindow(g_acIn, TRUE); EnableWindow(g_acShot, TRUE);
}
static BOOL ac_route_start(const wchar_t *q) {
  ac_review_reset(); g_acWebFoot[0] = 0;
  char *body = ac_route_body(q); wchar_t *copy = body ? _wcsdup(q) : NULL;
  if (!copy) { free(body); ac_status(L"Не удалось подготовить запрос: сократите сообщение или увеличьте контекст ИИ"); return FALSE; }
  int id = ai_ask_body(body, 180000, g_acWnd, WM_AI_CHAT, NULL);
  if (!id) { free(copy); ac_status(L"Не удалось запустить модель, вопрос остался в поле ввода"); return FALSE; }
  g_acRouteText = copy; g_acRouting = TRUE; g_acId = id;
  EnableWindow(g_acIn, FALSE); EnableWindow(g_acShot, FALSE);
  ac_busy(TRUE); ac_status(L"Думаю…"); return TRUE;
}
static void ac_end_task(const wchar_t *q, BOOL threat) {
  ag_stop(); af_clear(); ac_review_reset();
  g_acMkWait = g_acTakeWait = 0;
  g_acCmdWait = FALSE; g_acCmdText[0] = 0;
  g_acContextStart = g_acN;
  ac_push(0, q); ac_push(1, threat ? L"Нападение на людей я поддерживать не буду. Отойди от конфликта и убери опасные предметы подальше. Что произошло?" : L"Хорошо, эту задачу прекращаю.");
  /* The terminal acknowledgement is not a fresh task or threat either.
     Do not let the classifier inherit it on the next short message. */
  g_acContextStart = g_acN;
  SetWindowTextW(g_acIn, L""); ac_plan_ui(FALSE); ac_redraw(); ac_status(threat ? L"Локальный ответ, файловые действия не запущены" : L"Задача отменена");
}
static void ac_cancel_task(const wchar_t *q) { ac_end_task(q, FALSE); }
static void ac_route_result(AiResult *r) {
  AcRoute route; BOOL ok = r->status == AI_OK && ac_route_parse(r->text, &route);
  wchar_t *q = g_acRouteText; g_acRouteText = NULL;
  g_acId = 0; ac_route_reset(); ac_busy(FALSE);
  if (!ok || !q) {
    ac_status(r->status == AI_CANCELLED ? L"Остановлено, вопрос остался в поле ввода" : L"Модель не вернула полный ответ. Действия и поиск не запускались; вопрос остался в поле ввода");
  } else if (route.intent == 4) {
    ac_cancel_task(q);
  } else if (route.intent == 5) {
    ac_end_task(q, TRUE);
  } else {
    /* A new topic ends a pending file clarification. Keep the visible log,
       but do not teach chat to repeat that stale request for paths. */
    if (g_agPending && route.intent != 1) g_acContextStart = g_acN;
    if (!route.continuation || route.intent != 1) ag_stop();
    g_acRouteDispatch = route.intent;
    /* The original user text, never a model-written command or search query. */
    SetWindowTextW(g_acIn, q); ac_send();
    g_acRouteDispatch = 0;
  }
  free(q); ai_result_free(r);
}
