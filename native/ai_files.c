/* ai_files.c — команды для файлов из окна «Спросить ИИ» (подключается в конце ai_chat.c).
   Модель переводит фразу в одну операцию: раскладка сканов (sort), создать папку
   (mkdir), скопировать или переместить по имени (copy, move), переименовать (rename).
   Программа сама проверяет JSON, папки и файлы, показывает план «было → станет»
   и ничего не делает, пока не нажата «Применить». Удаления нет. Перезаписи нет:
   при совпадении имени новому файлу добавляется «(2)». */

typedef enum { AF_MKDIR, AF_COPY, AF_MOVE, AF_RENAME } AfKind;
typedef struct {
  AfKind kind;
  wchar_t src[MAX_PATH], dst[MAX_PATH];
} AfItem;

static AfItem *g_afItems;
static int g_afN, g_afCap;
static wchar_t g_acLastDir[MAX_PATH]; /* последняя папка, с которой работали: «из неё», «в неё» */
static wchar_t g_acKnown[40][MAX_PATH];  /* папки, которые модель может назвать */
static int g_acKnownN;
static wchar_t g_afHits[200][MAX_PATH];  /* найденные файлы */
static int g_afHitN;
static wchar_t g_acInstr[16000];

/* навык «Файлы»: правила, по которым модель собирает одну команду.
   Программа потом сама проверяет пути и показывает план. */
static const wchar_t kAcSkill[] =
    L"\nНавык «Файлы». Одна операция за ответ.\n"
    L"Поля JSON: op, src, dst, name, new, mode, question. Лишние поля не пиши.\n"
    L"op бывает только: copy, move, rename, mkdir, sort, ask.\n"
    L"\nПапки.\n"
    L"1. Полный путь пользователь написал сам (диск и двоеточие или путь с \\ в начале) — перепиши его дословно. "
    L"В JSON один обратный слэш пишется двумя: C:\\\\Сканы.\n"
    L"2. Папку назвали по имени («сканы», «архив», «загрузки», «тп») — так и пиши, без выдуманного диска. "
    L"Программа найдёт её в списке известных папок ниже.\n"
    L"3. Не выдумывай папку, которой нет ни в фразе, ни в списке известных.\n"
    L"4. Пустой src \"\" — «отсюда», «из неё», «из той же», если папка уже была. Пустой src без известной папки — это ask.\n"
    L"5. Известных папок нет — без полного пути из фразы отвечай ask и попроси путь, как в проводнике.\n"
    L"6. dst у copy, move и sort — всегда папка, не файл. Имя файла в dst не пиши.\n"
    L"7. Полный путь к файлу разбирай: src — папка, name — имя. "
    L"«C:\\Сканы\\830.pdf в D:\\Архив» → src C:\\\\Сканы, name 830.pdf, dst D:\\\\Архив.\n"
    L"\nИмя файла, поле name.\n"
    L"8. Только слова из просьбы. Расширение не дописывай, если его не произнесли.\n"
    L"9. Тип, не часть имени: эксель, excel, xls, xlsx — Excel; ворд, word, doc, docx — Word; пдф, pdf — PDF. "
    L"В name оставь слово типа («пдф», «эксель»), остальные слова — что ищут в имени.\n"
    L"10. Слова «все», «файл», «файлы», «файла» в name не пиши. «все пдф» → name \"пдф\".\n"
    L"11. Каждое оставшееся слово должно найтись в имени. «чертёж 830» — и «чертёж», и «830». Регистр не важен.\n"
    L"12. tiff, tif, jpg, jpeg, png, dwg, bmp — не особый тип. Если их назвали, оставь слово в name: «tiff 830».\n"
    L"13. Ищутся только файлы, не папки.\n"
    L"14. Нашлось несколько, а человек сказал «все» — всё равно одна команда copy или move, не список. "
    L"Не сказал «все» — тоже отдай команду: переспросит уже программа.\n"
    L"15. Имени нет («закинь это», «скопируй файл») — ask, спроси какое имя.\n"
    L"\nmove или copy.\n"
    L"16. move, если есть: перемести, перенеси, закинь, переложи, положи, скинь, отправь.\n"
    L"17. copy, если есть: скопируй, копия, продублируй, продублировать. Глагола нет — copy.\n"
    L"18. «закинь» — это move, не раскладка по штампу.\n"
    L"\nsort — разложить сканы по штампу. Не папка на каждое обозначение.\n"
    L"19. Только если просят разложить сканы, по штампу или по обозначению из рамки. "
    L"Обычное «перемести файлы» без штампа — move, не sort.\n"
    L"20. src — папка со сканами, dst — куда. Обе уже должны существовать. "
    L"Полный путь из фразы пиши дословно. Короткое имя — только если оно есть в списке известных папок. "
    L"Нет обоих путей — ask, попроси два пути как в проводнике. Новую папку для sort не выдумывай.\n"
    L"21. Программа сама читает штамп. Имя файла станет «обозначение наименование». "
    L"Папки не по обозначению, а по типу листа: Чертежи, Спецификации, ТП, Прочее. "
    L"Штамп не прочитан — папка «Не распознано», имя старое. "
    L"Берутся pdf, tif, tiff, png, jpg, jpeg, bmp. Excel, Word и dwg так не раскладываются.\n"
    L"22. mode \"move\" только если в фразе есть «перемести» или «перенеси». Иначе \"copy\".\n"
    L"23. «Каждое обозначение в свою папку» программа не делает. Ответь ask и скажи, что умеет: "
    L"имя файла по обозначению и подпапки Чертежи, Спецификации, ТП, Прочее.\n"
    L"\nmkdir — «создай папку», «сделай папку», «новая папка».\n"
    L"24. dst — путь новой папки. «папку Архив в C:\\Работа» → dst C:\\\\Работа\\\\Архив. "
    L"«папку ТП в сканах» → dst сканы\\\\ТП.\n"
    L"25. Просьба положить файлы в папку — это copy или move, не mkdir. Папку, если её нет, создаст программа.\n"
    L"\nrename.\n"
    L"26. name — как найти старый файл, new — новое имя целиком. Расширение сохрани, если его не просили сменить.\n"
    L"27. src — папка файла или \"\". В new нельзя знаки \\ / : * ? \" < > |\n"
    L"28. Один файл. Имя размытое («переименуй чертёж») без номера, когда их много, — ask.\n"
    L"\nask.\n"
    L"29. Удалить, стереть, очистить, в корзину, отформатировать — нельзя. "
    L"question: удалять не умею; переместить, скопировать или переименовать?\n"
    L"30. Два разных дела в одной фразе («создай папку и скопируй») — ask, какое сделать первым.\n"
    L"31. Неясно, что сделать или с каким файлом — ask, один короткий вопрос по-русски.\n"
    L"32. Не пиши, что уже сделано. Не придумывай файлы, которых не называли.\n";

/* примеры «фраза → JSON» для модели: few-shot вместо дообучения */
static const wchar_t *const kAcFewShot[] = {
    L"закинь эксель Аде в папку сканы → {\"op\":\"move\",\"src\":\"\",\"name\":\"эксель Аде\",\"dst\":\"сканы\"}",
    L"перемести все пдф из загрузок в архив → {\"op\":\"move\",\"src\":\"загрузки\",\"name\":\"пдф\",\"dst\":\"архив\"}",
    L"скопируй чертёж 830 в папку сканы → {\"op\":\"copy\",\"src\":\"\",\"name\":\"чертёж 830\",\"dst\":\"сканы\"}",
    L"переложи ворд ТУ в тп → {\"op\":\"move\",\"src\":\"\",\"name\":\"ворд ТУ\",\"dst\":\"тп\"}",
    L"скопируй все xls из сканов в архив → {\"op\":\"copy\",\"src\":\"сканы\",\"name\":\"xls\",\"dst\":\"архив\"}",
    L"положи спецификацию 830 в спец → {\"op\":\"move\",\"src\":\"\",\"name\":\"спецификация 830\",\"dst\":\"спец\"}",
    L"скинь все эксели АДЕ в архив → {\"op\":\"move\",\"src\":\"\",\"name\":\"эксель АДЕ\",\"dst\":\"архив\"}",
    L"отправь пдф 3422 из сканов в тп → {\"op\":\"move\",\"src\":\"сканы\",\"name\":\"пдф 3422\",\"dst\":\"тп\"}",
    L"сделай копию ворд маршрута в загрузки → {\"op\":\"copy\",\"src\":\"\",\"name\":\"ворд маршрут\",\"dst\":\"загрузки\"}",
    L"продублируй чертёж АДЕ.830 в архив → {\"op\":\"copy\",\"src\":\"\",\"name\":\"чертёж АДЕ.830\",\"dst\":\"архив\"}",
    L"отсюда все файлы 682 в папку сканы → {\"op\":\"copy\",\"src\":\"\",\"name\":\"682\",\"dst\":\"сканы\"}",
    L"из неё перенеси tiff 830 в архив → {\"op\":\"move\",\"src\":\"\",\"name\":\"tiff 830\",\"dst\":\"архив\"}",
    L"скопируй jpg штамп в спец → {\"op\":\"copy\",\"src\":\"\",\"name\":\"jpg штамп\",\"dst\":\"спец\"}",
    L"закинь dwg корпус в папку модели → {\"op\":\"move\",\"src\":\"\",\"name\":\"dwg корпус\",\"dst\":\"модели\"}",
    L"скопируй C:\\Сканы\\830.pdf в D:\\Архив → {\"op\":\"copy\",\"src\":\"C:\\\\Сканы\",\"name\":\"830.pdf\",\"dst\":\"D:\\\\Архив\"}",
    L"перемести D:\\Вход\\АДЕ 3422-682.xlsx в C:\\Работа\\ТП → {\"op\":\"move\",\"src\":\"D:\\\\Вход\",\"name\":\"АДЕ 3422-682.xlsx\",\"dst\":\"C:\\\\Работа\\\\ТП\"}",
    L"переименуй отчёт.docx в отчёт 2026.docx → {\"op\":\"rename\",\"src\":\"\",\"name\":\"отчёт.docx\",\"new\":\"отчёт 2026.docx\"}",
    L"переименуй Спецификация 830.xls в Спец 830.xls → {\"op\":\"rename\",\"src\":\"\",\"name\":\"Спецификация 830.xls\",\"new\":\"Спец 830.xls\"}",
    L"в сканах переименуй чертёж 830.pdf в 830-сб.pdf → {\"op\":\"rename\",\"src\":\"сканы\",\"name\":\"чертёж 830.pdf\",\"new\":\"830-сб.pdf\"}",
    L"переименуй ворд ТУ в ТУ 2026.docx → {\"op\":\"rename\",\"src\":\"\",\"name\":\"ворд ТУ\",\"new\":\"ТУ 2026.docx\"}",
    L"создай папку Архив 2026 в C:\\Работа → {\"op\":\"mkdir\",\"dst\":\"C:\\\\Работа\\\\Архив 2026\"}",
    L"создай папку ТП в сканах → {\"op\":\"mkdir\",\"dst\":\"сканы\\\\ТП\"}",
    L"сделай папку Спец → {\"op\":\"mkdir\",\"dst\":\"Спец\"}",
    L"разложи сканы из C:\\Сканы по штампам в C:\\Архив → {\"op\":\"sort\",\"src\":\"C:\\\\Сканы\",\"dst\":\"C:\\\\Архив\",\"mode\":\"copy\"}",
    L"разложи сканы по обозначениям из D:\\Вход в D:\\Архив → {\"op\":\"sort\",\"src\":\"D:\\\\Вход\",\"dst\":\"D:\\\\Архив\",\"mode\":\"copy\"}",
    L"перемести сканы из C:\\Сканы по штампам в C:\\Архив → {\"op\":\"sort\",\"src\":\"C:\\\\Сканы\",\"dst\":\"C:\\\\Архив\",\"mode\":\"move\"}",
    L"разложи сканы по штампу в архив → {\"op\":\"ask\",\"question\":\"Откуда сканы и куда класть? Напишите оба пути, как в проводнике.\"}",
    L"каждое обозначение в свою папку → {\"op\":\"ask\",\"question\":\"Отдельную папку на обозначение не делаю. Могу назвать файл «обозначение наименование» и разложить по папкам Чертежи, Спецификации, ТП, Прочее. Куда?\"}",
    L"удали все файлы из сканов → {\"op\":\"ask\",\"question\":\"Удалять не умею. Переместить, скопировать или переименовать?\"}",
    L"очисти папку архив → {\"op\":\"ask\",\"question\":\"Удалять не умею. Переместить файлы в другую папку?\"}",
    L"сделай что-нибудь с файлами → {\"op\":\"ask\",\"question\":\"Что сделать и с какими файлами?\"}",
    L"закинь это в архив → {\"op\":\"ask\",\"question\":\"Какой файл закинуть в архив?\"}",
    L"создай папку ТП и скопируй туда чертёж 830 → {\"op\":\"ask\",\"question\":\"Сначала создать папку ТП или скопировать чертёж 830?\"}",
    L"закинь АДЕ в папку которой нет → {\"op\":\"move\",\"src\":\"\",\"name\":\"АДЕ\",\"dst\":\"Новая папка\"}",
};

static void af_set_last(const wchar_t *dir) { lstrcpynW(g_acLastDir, dir, MAX_PATH); }

static void af_clear(void) {
  g_afN = 0;
  g_acPlanOn = FALSE;
}

static void af_add(AfKind k, const wchar_t *src, const wchar_t *dst) {
  if (g_afN == g_afCap) {
    int cap = g_afCap ? g_afCap * 2 : 16;
    AfItem *t = (AfItem *)realloc(g_afItems, cap * sizeof(AfItem));
    if (!t) return;
    g_afItems = t;
    g_afCap = cap;
  }
  AfItem *it = &g_afItems[g_afN++];
  it->kind = k;
  lstrcpynW(it->src, src, MAX_PATH);
  lstrcpynW(it->dst, dst, MAX_PATH);
}

static BOOL af_exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static BOOL af_is_dir(const wchar_t *p) {
  DWORD a = GetFileAttributesW(p);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void af_parent(const wchar_t *p, wchar_t *out) {
  lstrcpynW(out, p, MAX_PATH);
  wchar_t *s = wcsrchr(out, L'\\');
  if (s) *s = 0;
}

/* папки, которые модель может назвать: последняя использованная и её подпапки */
static void af_known_folders(void) {
  g_acKnownN = 0;
  if (!g_acLastDir[0]) return;
  lstrcpynW(g_acKnown[g_acKnownN++], g_acLastDir, MAX_PATH);
  wchar_t pat[MAX_PATH];
  _snwprintf(pat, MAX_PATH, L"%s\\*", g_acLastDir);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
      continue;
    if (g_acKnownN >= 40) break;
    _snwprintf(g_acKnown[g_acKnownN++], MAX_PATH, L"%s\\%s", g_acLastDir, fd.cFileName);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

static BOOL af_known(const wchar_t *p) {
  for (int k = 0; k < g_acKnownN; k++)
    if (!_wcsicmp(g_acKnown[k], p)) return TRUE;
  return FALSE;
}

/* путь можно трогать, если пользователь его назвал или он из списка известных папок */
static BOOL af_trusted(const wchar_t *p) {
  if (ac_named(p) || af_known(p)) return TRUE;
  wchar_t par[MAX_PATH];
  af_parent(p, par);
  return af_known(par);
}

/* путь от модели: полный — как есть; имя папки — по списку известных; пусто — последняя папка.
   allowNew: новую папку можно назвать от последней папки. */
static BOOL af_resolve(const wchar_t *in, wchar_t *out, BOOL allowNew) {
  if (!in[0]) {
    if (!g_acLastDir[0]) return FALSE;
    lstrcpynW(out, g_acLastDir, MAX_PATH);
    return TRUE;
  }
  if (wcschr(in, L':') || in[0] == L'\\') {
    lstrcpynW(out, in, MAX_PATH);
    return TRUE;
  }
  wchar_t first[MAX_PATH];
  lstrcpynW(first, in, MAX_PATH);
  wchar_t *rest = wcschr(first, L'\\');
  if (rest) *rest++ = 0;
  const wchar_t *hit = NULL;
  int n = 0;
  for (int k = 0; k < g_acKnownN; k++) {
    const wchar_t *base = wcsrchr(g_acKnown[k], L'\\');
    base = base ? base + 1 : g_acKnown[k];
    if (!_wcsicmp(base, first)) {
      hit = g_acKnown[k];
      n++;
    }
  }
  if (n == 1) {
    if (rest) _snwprintf(out, MAX_PATH, L"%s\\%s", hit, rest);
    else lstrcpynW(out, hit, MAX_PATH);
    return TRUE;
  }
  if (allowNew && g_acLastDir[0]) {
    _snwprintf(out, MAX_PATH, L"%s\\%s", g_acLastDir, in);
    return TRUE;
  }
  return FALSE;
}

/* имя файла с (2), (3)… — чтобы не перезаписать чужой файл */
static void af_unique_target(const wchar_t *dir, const wchar_t *name, wchar_t *out) {
  _snwprintf(out, MAX_PATH, L"%s\\%s", dir, name);
  for (int k = 2; af_exists(out) && k < 100; k++) {
    wchar_t base[MAX_PATH], ext[64] = L"";
    lstrcpynW(base, name, MAX_PATH);
    wchar_t *dot = wcsrchr(base, L'.');
    if (dot) {
      lstrcpynW(ext, dot, 64);
      *dot = 0;
    }
    _snwprintf(out, MAX_PATH, L"%s\\%s (%d)%s", dir, base, k, ext);
  }
}

static void af_lower(wchar_t *s) { CharLowerBuffW(s, (DWORD)wcslen(s)); }

/* файлы папки по словам из имени: все слова должны быть в имени; «эксель»/«ворд»/«пдф» — тип */
static int af_match(const wchar_t *dir, const wchar_t *name) {
  wchar_t words[8][64];
  int nw = 0, ext = 0;
  wchar_t lname[200];
  lstrcpynW(lname, name, 200);
  af_lower(lname);
  const wchar_t *p = lname;
  while (*p) {
    while (*p == L' ') p++;
    if (!*p) break;
    wchar_t tok[64];
    int n = 0;
    while (*p && *p != L' ' && n < 63) tok[n++] = *p++;
    tok[n] = 0;
    if (!wcscmp(tok, L"эксель") || !wcscmp(tok, L"excel") || !wcscmp(tok, L"xls") || !wcscmp(tok, L"xlsx")) ext |= 1;
    else if (!wcscmp(tok, L"ворд") || !wcscmp(tok, L"word") || !wcscmp(tok, L"doc") || !wcscmp(tok, L"docx")) ext |= 2;
    else if (!wcscmp(tok, L"пдф") || !wcscmp(tok, L"pdf")) ext |= 4;
    else if (!wcscmp(tok, L"все") || !wcscmp(tok, L"файл") || !wcscmp(tok, L"файлы") || !wcscmp(tok, L"файла")) continue;
    else if (nw < 8) lstrcpynW(words[nw++], tok, 64);
  }
  g_afHitN = 0;
  if (!nw && !ext) return 0;
  wchar_t pat[MAX_PATH];
  _snwprintf(pat, MAX_PATH, L"%s\\*", dir);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    wchar_t lf[MAX_PATH];
    lstrcpynW(lf, fd.cFileName, MAX_PATH);
    af_lower(lf);
    BOOL ok = TRUE;
    for (int i = 0; i < nw && ok; i++) ok = wcsstr(lf, words[i]) != NULL;
    const wchar_t *e = wcsrchr(lf, L'.');
    if (ext && !e) ok = FALSE;
    if (ok && ext) {
      BOOL t = ((ext & 1) && (!wcscmp(e, L".xls") || !wcscmp(e, L".xlsx"))) ||
               ((ext & 2) && (!wcscmp(e, L".doc") || !wcscmp(e, L".docx"))) || ((ext & 4) && !wcscmp(e, L".pdf"));
      ok = t;
    }
    if (ok && g_afHitN < 200) _snwprintf(g_afHits[g_afHitN++], MAX_PATH, L"%s\\%s", dir, fd.cFileName);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return g_afHitN;
}

/* список файлов папки для ответа «что там есть» */
static void af_list_dir(const wchar_t *dir, wchar_t *out, int cap) {
  out[0] = 0;
  wchar_t pat[MAX_PATH];
  _snwprintf(pat, MAX_PATH, L"%s\\*", dir);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  int n = 0;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (++n > 15) break;
    wcsncat(out, L"\r\n• ", cap - wcslen(out) - 1);
    wcsncat(out, fd.cFileName, cap - wcslen(out) - 1);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

static BOOL af_valid_name(const wchar_t *n) {
  if (!n[0] || wcspbrk(n, L"\\/:*?\"<>|")) return FALSE;
  return TRUE;
}

/* показать план: «было → станет», кнопки «Применить» и «Отмена» */
static void af_show_plan(void) {
  size_t cap = (size_t)g_afN * (MAX_PATH * 3 + 64) + 256;
  wchar_t *s = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!s) return;
  s[0] = 0;
  for (int i = 0; i < g_afN; i++) {
    AfItem *it = &g_afItems[i];
    if (it->kind == AF_MKDIR) _snwprintf(s + wcslen(s), 64 + MAX_PATH, L"Создать папку: %s\r\n", it->dst);
    else
      _snwprintf(s + wcslen(s), MAX_PATH * 2 + 64, L"%s: %s\r\n      → %s\r\n",
                 it->kind == AF_COPY ? L"Скопировать" : it->kind == AF_MOVE ? L"Переместить" : L"Переименовать",
                 it->src, it->dst);
  }
  ac_push(1, s);
  ac_push(1, L"Ничего ещё не изменено. Нажмите «Применить» или «Отмена».");
  free(s);
  g_acPlanOn = TRUE;
  ac_plan_ui(TRUE);
  ac_redraw();
}

/* запрос без плана: вопрос или отказ */
static void af_say(const wchar_t *msg) {
  ac_push(1, msg);
  ac_redraw();
}

/* JSON от модели: проверки здесь, перед любым действием */
static void ac_files_run(const wchar_t *j) {
  wchar_t op[16] = L"", src[MAX_PATH] = L"", dst[MAX_PATH] = L"", name[200] = L"", nw[200] = L"";
  wchar_t rs[MAX_PATH], rd[MAX_PATH], msg[1200];
  ac_json_field(j, L"op", op, 16);
  ac_json_field(j, L"src", src, MAX_PATH);
  ac_json_field(j, L"dst", dst, MAX_PATH);
  ac_json_field(j, L"name", name, 200);
  ac_json_field(j, L"new", nw, 200);
  af_clear();
  if (!wcscmp(op, L"mkdir")) {
    if (!af_resolve(dst, rd, TRUE) || !af_trusted(rd) || af_exists(rd)) {
      af_say(L"Напишите полный путь новой папки, как в проводнике, и проверьте, что её ещё нет.");
      return;
    }
    wchar_t par[MAX_PATH];
    af_parent(rd, par);
    if (!af_is_dir(par)) {
      af_say(L"Папка, в которой должна быть новая, не найдена.");
      return;
    }
    af_add(AF_MKDIR, L"", rd);
    af_show_plan();
    return;
  }
  if (!wcscmp(op, L"copy") || !wcscmp(op, L"move")) {
    BOOL mv = !wcscmp(op, L"move");
    if (!af_resolve(src, rs, FALSE) || !af_is_dir(rs) || !af_trusted(rs)) {
      af_say(L"Не понял, из какой папки брать файлы. Назовите её полностью, как в проводнике.");
      return;
    }
    if (!af_resolve(dst, rd, TRUE) || !af_trusted(rd)) {
      af_say(L"Не понял, в какую папку положить. Назовите её полностью, как в проводнике.");
      return;
    }
    if (!_wcsicmp(rs, rd)) {
      af_say(L"Файлы уже в этой папке.");
      return;
    }
    if (af_exists(rd) && !af_is_dir(rd)) {
      af_say(L"Под этим путём уже есть файл, а не папка.");
      return;
    }
    BOOL mkd = !af_exists(rd);
    wchar_t par[MAX_PATH];
    af_parent(rd, par);
    if (mkd && !af_is_dir(par)) {
      af_say(L"Папка назначения не найдена, и её родительская папка тоже.");
      return;
    }
    int n = af_match(rs, name);
    if (!n) {
      wchar_t list[1200] = L"";
      af_list_dir(rs, list, 1200);
      _snwprintf(msg, 1200, L"Не нашёл файл «%s» в %s. Что там есть:%s\r\nУточните имя.", name, rs, list);
      af_say(msg);
      return;
    }
    if (n > 1 && !wcsstr(g_acCmdText, L"все")) {
      wchar_t list[1200] = L"";
      for (int i = 0; i < n && wcslen(list) < 1000; i++) {
        wcsncat(list, L"\r\n• ", 1199 - wcslen(list));
        wcsncat(list, wcsrchr(g_afHits[i], L'\\') + 1, 1199 - wcslen(list));
      }
      _snwprintf(msg, 1200, L"Нашлось несколько файлов:%s\r\nУточните имя или напишите «все».", list);
      af_say(msg);
      return;
    }
    if (mkd) af_add(AF_MKDIR, L"", rd);
    for (int i = 0; i < n; i++) {
      wchar_t target[MAX_PATH];
      af_unique_target(rd, wcsrchr(g_afHits[i], L'\\') + 1, target);
      af_add(mv ? AF_MOVE : AF_COPY, g_afHits[i], target);
    }
    af_show_plan();
    return;
  }
  if (!wcscmp(op, L"rename")) {
    if (!af_resolve(src, rs, FALSE) || !af_is_dir(rs) || !af_trusted(rs)) {
      af_say(L"Не понял, в какой папке файл. Назовите её полностью, как в проводнике.");
      return;
    }
    if (!af_valid_name(nw)) {
      af_say(L"Новое имя пустое или содержит недопустимые знаки.");
      return;
    }
    int n = af_match(rs, name);
    if (n != 1) {
      af_say(n ? L"Под это имя подходит несколько файлов. Уточните имя." : L"Не нашёл такой файл. Уточните имя.");
      return;
    }
    wchar_t target[MAX_PATH];
    _snwprintf(target, MAX_PATH, L"%s\\%s", rs, nw);
    if (af_exists(target)) {
      af_say(L"Файл с таким новым именем уже есть.");
      return;
    }
    af_add(AF_RENAME, g_afHits[0], target);
    af_show_plan();
    return;
  }
  af_say(L"Не понял команду. Напишите, что сделать и с какими файлами.");
}

/* «Применить»: операции по порядку; перезаписи и удаления нет */
static void ac_files_apply(void) {
  if (!g_acPlanOn) return;
  int ok = 0, bad = 0;
  wchar_t last[MAX_PATH] = L"";
  for (int i = 0; i < g_afN; i++) {
    AfItem *it = &g_afItems[i];
    BOOL r = FALSE;
    switch (it->kind) {
    case AF_MKDIR: r = CreateDirectoryW(it->dst, NULL); break;
    case AF_COPY: r = CopyFileW(it->src, it->dst, TRUE); break;
    case AF_MOVE:
    case AF_RENAME: r = MoveFileW(it->src, it->dst); break;
    }
    if (r) {
      ok++;
      if (it->kind == AF_MKDIR) lstrcpynW(last, it->dst, MAX_PATH);
      else af_parent(it->dst, last);
    } else
      bad++;
  }
  if (last[0]) af_set_last(last);
  af_clear();
  ac_plan_ui(FALSE);
  wchar_t msg[200];
  _snwprintf(msg, 200, L"Готово: %d. Не вышло: %d%s", ok, bad,
             bad ? L" (возможно, файл открыт в другой программе)" : L"");
  af_say(msg);
}

static void ac_files_cancel(void) {
  if (!g_acPlanOn) return;
  af_clear();
  ac_plan_ui(FALSE);
  af_say(L"Отменено: ничего не изменено.");
}

/* инструкция модели: навык, известные папки и примеры. Длина — по окну контекста,
   иначе длинный навык не влезает в «Слабый» уровень с малым контекстом. */
static void af_cat(int lim, const wchar_t *s) {
  size_t n = wcslen(g_acInstr);
  if (!s || (int)n >= lim - 1) return;
  wcsncat(g_acInstr, s, (size_t)(lim - 1 - (int)n));
}

static const wchar_t *ac_cmd_instruction(void) {
  int lim = 12000;
  int ctx = ai_ctx_size();
  if (ctx >= 2048) {
    lim = (ctx - 700) * 2;
    if (lim > 12000) lim = 12000;
    if (lim < 1800) lim = 1800;
  }
  af_known_folders();
  g_acInstr[0] = 0;
  af_cat(lim, AC_CMD_SYS);
  af_cat(lim, kAcSkill);
  af_cat(lim, L"\nИзвестные папки:");
  if (!g_acKnownN) af_cat(lim, L" нет, пишите полный путь");
  for (int k = 0; k < g_acKnownN; k++) {
    af_cat(lim, L"\n");
    af_cat(lim, g_acKnown[k]);
  }
  af_cat(lim, L"\nПримеры:");
  for (size_t k = 0; k < sizeof(kAcFewShot) / sizeof(kAcFewShot[0]); k++) {
    af_cat(lim, L"\n");
    af_cat(lim, kAcFewShot[k]);
  }
  return g_acInstr;
}

#ifdef SET_TEST
/* проверочная сборка: --aifiles папка «текст команды» JSON [apply] — план, при apply сразу «Применить».
   Что вышло — в папке/aifiles.txt */
static void ai_files_test(int argc, wchar_t **argv) {
  af_set_last(argv[2]);
  lstrcpynW(g_acCmdText, argv[3], 2000);
  af_known_folders();
  ac_files_run(argv[4]);
  if (argc >= 6 && !wcscmp(argv[5], L"apply")) ac_files_apply();
  wchar_t out[MAX_PATH];
  _snwprintf(out, MAX_PATH, L"%s\\aifiles.txt", argv[2]);
  FILE *f = _wfopen(out, L"w, ccs=UTF-8");
  if (!f) return;
  for (int i = 0; i < g_acN; i++) fwprintf(f, L"%ls\r\n", g_acTurns[i].text);
  fclose(f);
}
#endif
