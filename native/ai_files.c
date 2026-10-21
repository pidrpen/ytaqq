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
    /* _snwprintf при нехватке места не ставит 0 — длинный путь пропускаем, а не обрезаем */
    if (wcslen(g_acLastDir) + wcslen(fd.cFileName) + 2 > MAX_PATH) continue;
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
  /* _snwprintf при нехватке места не ставит 0, а обрезанное имя — чужой путь.
     Не влезает с запасом на « (99)» — цели нет (пустая строка), действие пропускается */
  if (wcslen(dir) + wcslen(name) + 7 > MAX_PATH) {
    out[0] = 0;
    return;
  }
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
  BOOL all = FALSE;
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
    if (wcsstr(tok, L"эксел") || wcsstr(tok, L"ексел") || wcsstr(tok, L"excel") || !wcscmp(tok, L"xls") ||
        !wcscmp(tok, L"xlsx"))
      ext |= 1;
    else if (!wcscmp(tok, L"ворд") || !wcscmp(tok, L"word") || !wcscmp(tok, L"doc") || !wcscmp(tok, L"docx")) ext |= 2;
    else if (!wcscmp(tok, L"пдф") || !wcscmp(tok, L"pdf")) ext |= 4;
    else if (!wcscmp(tok, L"tif") || !wcscmp(tok, L"tiff") || !wcscmp(tok, L"тиф")) ext |= 8;
    else if (!wcscmp(tok, L"jpg") || !wcscmp(tok, L"jpeg") || !wcscmp(tok, L"джипег")) ext |= 16;
    else if (!wcscmp(tok, L"png") || !wcscmp(tok, L"пнг")) ext |= 32;
    else if (!wcscmp(tok, L"dwg") || !wcscmp(tok, L"двг")) ext |= 64;
    else if (!wcscmp(tok, L"bmp")) ext |= 128;
    else if (!wcsncmp(tok, L"картин", 6)) ext |= 8 | 16 | 32 | 128;
    else if (!wcscmp(tok, L"все") || !wcscmp(tok, L"всё")) all = TRUE;
    else if (!wcscmp(tok, L"файл") || !wcscmp(tok, L"файлы") || !wcscmp(tok, L"файла") || !wcscmp(tok, L"туда") ||
             !wcscmp(tok, L"их"))
      continue;
    else if (nw < 8) lstrcpynW(words[nw++], tok, 64);
  }
  g_afHitN = 0;
  if (!nw && !ext && !all) return 0;
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
               ((ext & 2) && (!wcscmp(e, L".doc") || !wcscmp(e, L".docx"))) || ((ext & 4) && !wcscmp(e, L".pdf")) ||
               ((ext & 8) && (!wcscmp(e, L".tif") || !wcscmp(e, L".tiff"))) ||
               ((ext & 16) && (!wcscmp(e, L".jpg") || !wcscmp(e, L".jpeg"))) || ((ext & 32) && !wcscmp(e, L".png")) ||
               ((ext & 64) && !wcscmp(e, L".dwg")) || ((ext & 128) && !wcscmp(e, L".bmp"));
      ok = t;
    }
    if (ok && wcslen(dir) + wcslen(fd.cFileName) + 2 > MAX_PATH) ok = FALSE; /* не обрезать путь (см. af_unique_target) */
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
  int shown = 0, left = 0;
  for (int i = 0; i < g_afN; i++) {
    if (shown >= 30) {
      left++;
      continue;
    }
    AfItem *it = &g_afItems[i];
    if (it->kind == AF_MKDIR) _snwprintf(s + wcslen(s), 64 + MAX_PATH, L"Создать папку: %s\r\n", it->dst);
    else
      _snwprintf(s + wcslen(s), MAX_PATH * 2 + 64, L"%s: %s\r\n      → %s\r\n",
                 it->kind == AF_COPY ? L"Скопировать" : it->kind == AF_MOVE ? L"Переместить" : L"Переименовать",
                 it->src, it->dst);
    shown++;
  }
  if (left) _snwprintf(s + wcslen(s), 64, L"… и ещё %d\r\n", left);
  ac_push(1, s);
  ac_push(1, L"Ничего ещё не изменено. Над полем ввода кнопки «Применить» и «Отмена».");
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

/* создать папку по пути, который человек написал. Диск смотрит программа, не модель.
   Недостающие папки по дороге тоже создаются. «Применить» не нужно: путь уже назван. */
static void ac_mkdir_now(const wchar_t *dst) {
  wchar_t rd[MAX_PATH], msg[MAX_PATH + 80];
  af_known_folders();
  if (!af_resolve(dst, rd, TRUE) || !af_trusted(rd)) {
    af_say(L"Не понял путь. Напишите его целиком, как в проводнике: C:\\Папка\\новая");
    return;
  }
  size_t n = wcslen(rd);
  while (n > 3 && (rd[n - 1] == L'\\' || rd[n - 1] == L'/')) rd[--n] = 0;
  if (af_is_dir(rd)) {
    _snwprintf(msg, MAX_PATH + 80, L"Эта папка уже есть:\r\n%s", rd);
    af_say(msg);
    return;
  }
  if (af_exists(rd)) {
    af_say(L"По этому пути уже лежит файл, не папка.");
    return;
  }
  wchar_t missing[16][MAX_PATH];
  int nm = 0;
  wchar_t cur[MAX_PATH];
  lstrcpynW(cur, rd, MAX_PATH);
  while (nm < 16 && !af_is_dir(cur)) {
    if (af_exists(cur)) {
      af_say(L"По дороге к папке уже лежит файл. Путь не создать.");
      return;
    }
    lstrcpynW(missing[nm++], cur, MAX_PATH);
    wchar_t par[MAX_PATH];
    af_parent(cur, par);
    if (!par[0] || !_wcsicmp(par, cur)) break;
    lstrcpynW(cur, par, MAX_PATH);
  }
  if (!af_is_dir(cur)) {
    _snwprintf(msg, MAX_PATH + 80, L"Нет такой верхней папки:\r\n%s", cur);
    af_say(msg);
    return;
  }
  for (int i = nm - 1; i >= 0; i--) {
    if (!CreateDirectoryW(missing[i], NULL) && !af_is_dir(missing[i])) {
      _snwprintf(msg, MAX_PATH + 80, L"Не создалась:\r\n%s", missing[i]);
      af_say(msg);
      return;
    }
  }
  af_set_last(rd);
  _snwprintf(msg, MAX_PATH + 80, L"Создал папку:\r\n%s", rd);
  af_say(msg);
}

/* «pdf» или «все tif» — забирать все файлы этого типа, не переспрашивать */
static BOOL ac_all_of_type(const wchar_t *what) {
  wchar_t s[200];
  lstrcpynW(s, what ? what : L"", 200);
  af_lower(s);
  const wchar_t *p = s;
  int words = 0, types = 0;
  while (*p) {
    while (*p == L' ') p++;
    if (!*p) break;
    wchar_t tok[64];
    int n = 0;
    while (*p && *p != L' ' && n < 63) tok[n++] = *p++;
    tok[n] = 0;
    words++;
    if (!wcscmp(tok, L"все") || !wcscmp(tok, L"файл") || !wcscmp(tok, L"файлы") || !wcscmp(tok, L"туда") ||
        !wcscmp(tok, L"pdf") || !wcscmp(tok, L"пдф") || wcsstr(tok, L"эксел") || wcsstr(tok, L"ексел") ||
        wcsstr(tok, L"excel") || !wcscmp(tok, L"xls") || !wcscmp(tok, L"xlsx") || !wcscmp(tok, L"ворд") || !wcscmp(tok, L"word") ||
        !wcscmp(tok, L"doc") || !wcscmp(tok, L"docx") || !wcscmp(tok, L"tif") || !wcscmp(tok, L"tiff") ||
        !wcscmp(tok, L"тиф") || !wcscmp(tok, L"jpg") || !wcscmp(tok, L"jpeg") || !wcscmp(tok, L"джипег") ||
        !wcscmp(tok, L"png") || !wcscmp(tok, L"пнг") || !wcscmp(tok, L"dwg") || !wcscmp(tok, L"двг") ||
        !wcscmp(tok, L"bmp") || !wcsncmp(tok, L"картин", 6))
      types++;
  }
  return words > 0 && words == types;
}

/* создать папку и сложить в неё файлы из src. Перенос начнётся после «Применить». */
static void af_queue_dir(const wchar_t *p);
static void ac_take_plan(const wchar_t *src, const wchar_t *dst, const wchar_t *what, BOOL mv) {
  wchar_t msg[600];
  af_clear();
  if (!src || !src[0] || !af_is_dir(src)) {
    af_say(L"Не вижу, откуда брать файлы.");
    return;
  }
  if (!what || !what[0]) {
    af_say(L"Какие файлы положить? Например: все pdf или 8930.");
    return;
  }
  if (!af_is_dir(dst)) {
    wchar_t par[MAX_PATH];
    af_parent(dst, par);
    if (!af_is_dir(par)) {
      af_say(L"Папки, куда класть, нет.");
      return;
    }
    af_queue_dir(dst);
  }
  int n = af_match(src, what);
  if (!n) {
    wchar_t list[700] = L"";
    af_list_dir(src, list, 700);
    _snwprintf(msg, 600, L"В папке нет таких файлов («%s»). Что там есть:%s", what, list[0] ? list : L"\r\nпусто");
    msg[599] = 0; /* список длиннее msg: _snwprintf тогда не ставит 0 */
    af_say(msg);
    return;
  }
  if (n > 1 && !ac_all_of_type(what) && !wcsstr(g_acCmdText, L"все")) {
    wchar_t list[700] = L"";
    for (int i = 0; i < n && wcslen(list) < 550; i++) {
      wcsncat(list, L"\r\n• ", 699 - wcslen(list));
      wcsncat(list, wcsrchr(g_afHits[i], L'\\') + 1, 699 - wcslen(list));
    }
    _snwprintf(msg, 600, L"Подходит несколько:%s\r\nНапишите «все» или уточните имя.", list);
    msg[599] = 0;
    af_say(msg);
    return;
  }
  for (int i = 0; i < n; i++) {
    const wchar_t *base = wcsrchr(g_afHits[i], L'\\');
    wchar_t target[MAX_PATH];
    base = base ? base + 1 : g_afHits[i];
    af_unique_target(dst, base, target);
    if (!target[0] || !_wcsicmp(g_afHits[i], target)) continue;
    af_add(mv ? AF_MOVE : AF_COPY, g_afHits[i], target);
  }
  if (!g_afN) {
    af_say(L"Эти файлы уже в этой папке.");
    return;
  }
  const wchar_t *leaf = wcsrchr(dst, L'\\');
  leaf = leaf ? leaf + 1 : dst;
  _snwprintf(msg, 600, L"Папка «%s» и %s файлов: %d. Кнопка «Отправить» стала «Применить».", leaf,
             mv ? L"перенос" : L"копии", n);
  ac_push(1, msg);
  af_show_plan();
}

static void af_queue_dir(const wchar_t *p) {
  if (af_is_dir(p)) return;
  for (int i = 0; i < g_afN; i++)
    if (g_afItems[i].kind == AF_MKDIR && !_wcsicmp(g_afItems[i].dst, p)) return;
  af_add(AF_MKDIR, L"", p);
}

/* папка формата: PDF, Excel, Word, TIFF… */
static void af_ext_folder(const wchar_t *file, wchar_t *out) {
  const wchar_t *dot = wcsrchr(file, L'.');
  wchar_t e[16];
  if (!dot || dot == file || !dot[1]) {
    lstrcpynW(out, L"Без типа", 32);
    return;
  }
  lstrcpynW(e, dot, 16);
  CharLowerBuffW(e, (DWORD)wcslen(e));
  if (!wcscmp(e, L".pdf")) lstrcpynW(out, L"PDF", 32);
  else if (!wcscmp(e, L".xls") || !wcscmp(e, L".xlsx") || !wcscmp(e, L".xlsm")) lstrcpynW(out, L"Excel", 32);
  else if (!wcscmp(e, L".doc") || !wcscmp(e, L".docx")) lstrcpynW(out, L"Word", 32);
  else if (!wcscmp(e, L".tif") || !wcscmp(e, L".tiff")) lstrcpynW(out, L"TIFF", 32);
  else if (!wcscmp(e, L".jpg") || !wcscmp(e, L".jpeg")) lstrcpynW(out, L"JPG", 32);
  else if (!wcscmp(e, L".png")) lstrcpynW(out, L"PNG", 32);
  else if (!wcscmp(e, L".bmp")) lstrcpynW(out, L"BMP", 32);
  else if (!wcscmp(e, L".gif")) lstrcpynW(out, L"GIF", 32);
  else if (!wcscmp(e, L".dwg")) lstrcpynW(out, L"DWG", 32);
  else if (!wcscmp(e, L".dxf")) lstrcpynW(out, L"DXF", 32);
  else if (!wcscmp(e, L".txt")) lstrcpynW(out, L"TXT", 32);
  else if (!wcscmp(e, L".zip") || !wcscmp(e, L".rar") || !wcscmp(e, L".7z")) lstrcpynW(out, L"Архивы", 32);
  else {
    lstrcpynW(out, e + 1, 12);
    CharUpperBuffW(out, (DWORD)wcslen(out));
  }
}

/* папка имени: имя файла без расширения, без знаков, которые Windows не пускает в имя */
static void af_name_folder(const wchar_t *file, wchar_t *out) {
  wchar_t s[180];
  lstrcpynW(s, file, 180);
  wchar_t *dot = wcsrchr(s, L'.');
  if (dot && dot != s) *dot = 0;
  int n = 0;
  for (wchar_t *p = s; *p && n < 80; p++) out[n++] = wcschr(L"\\/:*?\"<>|", *p) ? L'_' : *p;
  while (n && (out[n - 1] == L' ' || out[n - 1] == L'.')) n--;
  int b = 0;
  while (b < n && (out[b] == L' ' || out[b] == L'.')) b++;
  if (b) {
    n -= b;
    memmove(out, out + b, (size_t)n * sizeof(wchar_t));
  }
  out[n] = 0;
  if (!out[0]) lstrcpynW(out, L"Без имени", 80);
}

/* mode: 1 имя, 2 формат, 3 имя\формат. Файлы только верхнего уровня папки.
   Ничего не двигается, пока не нажата «Применить». */
static void ac_group_plan(const wchar_t *user, int mode) {
  wchar_t srcIn[MAX_PATH] = L"", dstIn[MAX_PATH] = L"";
  wchar_t src[MAX_PATH], dst[MAX_PATH], msg[400];
  BOOL have = ac_pick_path(user, srcIn);
  const wchar_t *rest = NULL;
  if (have) {
    const wchar_t *at = wcsstr(user, srcIn);
    if (at) rest = at + wcslen(srcIn);
  }
  af_known_folders();
  af_clear();
  if (have) {
    if (!af_resolve(srcIn, src, FALSE) || !af_is_dir(src) || !af_trusted(src)) {
      af_say(L"Не нашёл папку, которую раскладывать. Напишите путь, как в проводнике.");
      return;
    }
  } else if (g_acLastDir[0] && af_is_dir(g_acLastDir)) {
    lstrcpynW(src, g_acLastDir, MAX_PATH);
  } else {
    af_say(L"Напишите путь к папке целиком. Например: разложи по форматам C:\\Папка");
    return;
  }
  if (rest && ac_pick_path(rest, dstIn)) {
    if (!af_resolve(dstIn, dst, TRUE) || !af_trusted(dst)) {
      af_say(L"Не понял, в какую папку складывать.");
      return;
    }
  } else
    lstrcpynW(dst, src, MAX_PATH);
  wchar_t low[500];
  lstrcpynW(low, user, 500);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  BOOL mv = wcsstr(low, L"скопир") == NULL;
  wchar_t par[MAX_PATH];
  if (!af_is_dir(dst)) {
    af_parent(dst, par);
    if (!af_is_dir(par)) {
      af_say(L"Папки назначения нет, и папки выше тоже.");
      return;
    }
    af_queue_dir(dst);
  }
  wchar_t pat[MAX_PATH];
  _snwprintf(pat, MAX_PATH, L"%s\\*", src);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    af_say(L"Папку не открыть.");
    return;
  }
  int files = 0, skip = 0;
  BOOL more = FALSE;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (!wcscmp(fd.cFileName, L"desktop.ini") || !_wcsicmp(fd.cFileName, L"Thumbs.db")) continue;
    if (files >= 400) {
      more = TRUE;
      break;
    }
    wchar_t kind[32], stem[88], dir[MAX_PATH], full[MAX_PATH], target[MAX_PATH];
    af_ext_folder(fd.cFileName, kind);
    af_name_folder(fd.cFileName, stem);
    /* путь длиннее MAX_PATH — файл не трогаем: _snwprintf не поставил бы 0 */
    if (wcslen(dst) + wcslen(stem) + wcslen(kind) + 3 > MAX_PATH || wcslen(src) + wcslen(fd.cFileName) + 2 > MAX_PATH) {
      skip++;
      continue;
    }
    if (mode == 1) _snwprintf(dir, MAX_PATH, L"%s\\%s", dst, stem);
    else if (mode == 2) _snwprintf(dir, MAX_PATH, L"%s\\%s", dst, kind);
    else _snwprintf(dir, MAX_PATH, L"%s\\%s\\%s", dst, stem, kind);
    _snwprintf(full, MAX_PATH, L"%s\\%s", src, fd.cFileName);
    af_unique_target(dir, fd.cFileName, target);
    if (!target[0] || !_wcsicmp(full, target)) {
      skip++;
      continue;
    }
    if (mode == 3) {
      wchar_t mid[MAX_PATH];
      _snwprintf(mid, MAX_PATH, L"%s\\%s", dst, stem);
      af_queue_dir(mid);
    }
    af_queue_dir(dir);
    af_add(mv ? AF_MOVE : AF_COPY, full, target);
    files++;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  if (!files) {
    af_clear();
    af_say(skip ? L"Файлы уже лежат по таким папкам." : L"В папке нет файлов, которые можно разложить.");
    return;
  }
  const wchar_t *how = mode == 3 ? L"по имени и формату" : mode == 1 ? L"по имени файла" : L"по формату";
  _snwprintf(msg, 400, L"Разложить %s (%s). Файлов: %d%s. Кнопка «Отправить» стала «Применить».", how,
             mv ? L"перенос" : L"копии", files, more ? L", дальше ещё есть" : L"");
  ac_push(1, msg);
  af_show_plan();
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
    ac_mkdir_now(dst[0] ? dst : g_acCmdText);
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
      msg[1199] = 0; /* длинный список: _snwprintf не ставит 0 */
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
      msg[1199] = 0;
      af_say(msg);
      return;
    }
    if (mkd) af_add(AF_MKDIR, L"", rd);
    for (int i = 0; i < n; i++) {
      wchar_t target[MAX_PATH];
      af_unique_target(rd, wcsrchr(g_afHits[i], L'\\') + 1, target);
      if (target[0]) af_add(mv ? AF_MOVE : AF_COPY, g_afHits[i], target);
    }
    af_show_plan();
    return;
  }
  if (!wcscmp(op, L"rename")) {
    if (!af_resolve(src, rs, FALSE) || !af_is_dir(rs) || !af_trusted(rs)) {
      af_say(L"Не понял, в какой папке файл. Назовите её полностью, как в проводнике.");
      return;
    }
    if (!af_valid_name(nw) || wcslen(rs) + wcslen(nw) + 2 > MAX_PATH) {
      af_say(L"Новое имя пустое, слишком длинное или содержит недопустимые знаки.");
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
  if (g_agReview) { ag_apply_review(); return; }
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
  ac_avatar_set(bad ? AC_AV_ERROR : AC_AV_SUCCESS);
}

static void ac_files_cancel(void) {
  if (!g_acPlanOn) return;
  if (g_agReview) ag_stop();
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

#ifdef SET_TEST
/* проверочная сборка: --airoute папка файл — фраза из файла (UTF-8); пишет папка\\airoute.txt: 1 — файловая команда, 0 — чат */
static void ai_route_test(wchar_t **argv) {
  wchar_t out[MAX_PATH], q[400] = L"";
  _snwprintf(out, MAX_PATH, L"%s\\airoute.txt", argv[2]);
  FILE *in = _wfopen(argv[3], L"r, ccs=UTF-8");
  if (in) {
    fgetws(q, 400, in);
    fclose(in);
  }
  FILE *f = _wfopen(out, L"w");
  if (!f) return;
  fprintf(f, "%d\n", ac_is_file_cmd(q) ? 1 : 0);
  fclose(f);
}
#endif
