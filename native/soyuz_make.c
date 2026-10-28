/* ---- «Создать ТП»: деталь и техпроцесс по маршруту — функцией пакета «CursorPad — Союз» (с 2026.10.28) ----

   В базу CursorPad по-прежнему не пишет. Описание (cursorpad-soyuz.txt в папке данных:
   ПАПКА, ИЗДЕЛИЕ, ТП — строки через Tab) отдаётся мосту cp-soyuz.exe (рядом с
   CursorPad.exe, иначе soyuz_bridge в plm.txt). Мост через клиентскую службу «Союза»
   (PLMClientHelperService) зовёт в уже запущенном клиенте скриптовую функцию
   CursorPadCreate из пакета «CursorPad — Союз»: та штатным API создаёт изделие (если
   его нет), ТП с версией, вариантом и операциями — код справочника, участок, оборудование,
   Тпз и Тшт — и вносит ТП в карточку изделия. Всё под правами того, кто вошёл в клиент;
   чего нет в справочниках (материал, участок, станок), функция не досоздаёт, а пишет в
   отчёт. Пакет и мост — вне репозитория (soyuz-sql\cursorpad-pkg), ставит администратор.

   Папку «Союза» выбирает человек: список папок хранилища читается из базы (только
   SELECT по Containers) в выпадающий список, можно вписать путь руками
   («Корневая\Подпапка»); последняя папка запоминается в soyuz.txt. Папок CursorPad не
   создаёт: нет такой — функция откажет и скажет, какой части пути не нашлось. */

#define WM_SZM_FOLDERS (WM_APP + 95) /* lParam — SzmFolders*, освобождает окно */
#define WM_SZM_DONE (WM_APP + 96)    /* lParam — wchar_t* отчёт, освобождает окно */
#define ID_SZM_FOLDER 4870
#define ID_SZM_NEW 4871
#define ID_SZM_OK 4872
#define ID_SZM_CANCEL 4873
#define SZM_PATH 300
#define SZM_MAX 2000

typedef struct {
  LONG gen;
  int n;
  wchar_t (*path)[SZM_PATH];
  wchar_t err[300];
} SzmFolders;

static HWND g_szmWnd, g_szmCombo, g_szmChk, g_szmStatus, g_szmLog;
static volatile LONG g_szmBusy, g_szmGen;
static wchar_t *g_szmText;      /* описание из окна пакета (ИЗДЕЛИЕ, СОСТАВ, ТП без ПАПКА); NULL — по маршруту (g_szR) */
static wchar_t g_szmLabel[240]; /* подпись окна в режиме текста */
static BOOL log_window(HWND *pw, HWND owner, const wchar_t *title, int w, int h); /* route_ui.c */

static void szm_pref_path(wchar_t *p) { _snwprintf(p, MAX_PATH, L"%s\\soyuz.txt", g_dataDir), p[MAX_PATH - 1] = 0; }

/* последняя папка — из soyuz.txt (UTF-8, первая строка) */
static void szm_pref_load(wchar_t *folder, int cap) {
  folder[0] = 0;
  wchar_t p[MAX_PATH];
  szm_pref_path(p);
  HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  char buf[1200];
  DWORD n = 0;
  ReadFile(h, buf, sizeof(buf) - 1, &n, NULL);
  CloseHandle(h);
  buf[n] = 0;
  char *s = buf;
  if (n >= 3 && (unsigned char)s[0] == 0xEF) s += 3;
  char *e = strpbrk(s, "\r\n");
  if (e) *e = 0;
  MultiByteToWideChar(CP_UTF8, 0, s, -1, folder, cap);
}

static void szm_pref_save(const wchar_t *folder) {
  wchar_t p[MAX_PATH];
  szm_pref_path(p);
  char u[1200];
  int n = WideCharToMultiByte(CP_UTF8, 0, folder, -1, u, 1196, NULL, NULL);
  if (n <= 0) return;
  strcpy(u + n - 1, "\r\n");
  HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wr;
  WriteFile(h, "\xEF\xBB\xBF", 3, &wr, NULL);
  WriteFile(h, u, (DWORD)strlen(u), &wr, NULL);
  CloseHandle(h);
}

/* мост: soyuz_bridge из plm.txt, иначе cp-soyuz.exe рядом с CursorPad.exe */
static BOOL szm_bridge(wchar_t *out, int cap) {
  if (g_szBridge[0]) lstrcpynW(out, g_szBridge, cap);
  else {
    GetModuleFileNameW(NULL, out, (DWORD)cap);
    wchar_t *b = wcsrchr(out, L'\\');
    if (b) b[1] = 0;
    wcsncat(out, L"cp-soyuz.exe", (size_t)cap - wcslen(out) - 1);
  }
  return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

static void szm_status(const wchar_t *t) {
  if (g_szmStatus) SetWindowTextW(g_szmStatus, t);
}

/* код участка из «Цех 3 · 31 Механический участок 31» — число в начале участка */
static void szm_area_code(const wchar_t *ceh, wchar_t *out, int cap) {
  out[0] = 0;
  const wchar_t *p = wcsstr(ceh, L" · ");
  p = p ? p + 3 : ceh;
  if (p == ceh && !wcsstr(ceh, L" · ")) { /* только цех или только участок — берём число, если оно есть */
    while (*p && !(*p >= L'0' && *p <= L'9')) p++;
  }
  int k = 0;
  while (*p >= L'0' && *p <= L'9' && k < cap - 1) out[k++] = *p++;
  out[k] = 0;
}

static void szm_tsv(wchar_t *dst, int cap, const wchar_t *s) { /* табуляции и переводы строк в поле — пробелом */
  size_t l = wcslen(dst);
  for (; *s && l < (size_t)cap - 1; s++) dst[l++] = (*s == L'\t' || *s == L'\r' || *s == L'\n') ? L' ' : *s;
  dst[l] = 0;
}

/* описание для функции: ПАПКА, ИЗДЕЛИЕ (если просили), ТП по строкам маршрута с кодом.
   Возвращает число операций; текст предупреждений — в note */
static int szm_write_spec(const wchar_t *path, const wchar_t *folder, BOOL newPart, wchar_t *note, int ncap) {
  note[0] = 0;
  static wchar_t txt[16000];
  txt[0] = 0;
  wchar_t des[200] = L"", name[200] = L"", fold[SZM_PATH] = L"", mat[420] = L"";
  szm_tsv(des, 200, g_szR.des); /* значения полей — без табуляций и переводов строк */
  szm_tsv(name, 200, g_szR.name);
  szm_tsv(fold, SZM_PATH, folder);
  if (g_szR.blank[0] && g_szR.mat[0]) _snwprintf(mat, 420, L"%s / %s", g_szR.blank, g_szR.mat);
  else lstrcpynW(mat, g_szR.mat, 420);
  size_t l = 0;
  l += (size_t)_snwprintf(txt + l, 16000 - l, L"# CursorPad → «Союз»: %s %s\r\n", des, name);
  if (newPart) {
    l += (size_t)_snwprintf(txt + l, 16000 - l, L"ПАПКА\t%s\r\n", fold);
    l += (size_t)_snwprintf(txt + l, 16000 - l, L"ИЗДЕЛИЕ\t%s\t%s\tДетали\t%s\t\r\n", des, name, mat);
  }
  int n = 0, skipped = 0;
  for (int i = 0; i < g_szR.n; i++) {
    const SoItem *it = &g_szR.it[i];
    if (it->state == SO_SKIP) continue;
    if (!it->code[0]) {
      skipped++;
      continue;
    }
    wchar_t area[16], tpz[32] = L"", tsht[32] = L"";
    szm_area_code(it->ceh, area, 16);
    double pz = it->tpz >= 0 ? it->tpz : it->aTpz, sh = it->tsht >= 0 ? it->tsht : it->aTsht;
    if (pz > 0) tn_numstr(pz, tpz, 32);
    if (sh > 0) tn_numstr(sh, tsht, 32);
    for (wchar_t *c = tpz; *c; c++)
      if (*c == L',') *c = L'.';
    for (wchar_t *c = tsht; *c; c++)
      if (*c == L',') *c = L'.';
    wchar_t eq[200] = L"";
    szm_tsv(eq, 200, it->equip);
    l = wcslen(txt);
    if (l + 400 < 16000)
      _snwprintf(txt + l, 16000 - l, L"ТП\t%s\t%s\t%s\t%s\t%s\t%s\t%s\r\n", des, it->num, it->code, area, eq, tpz, tsht);
    n++;
  }
  txt[15999] = 0;
  if (skipped) _snwprintf(note, ncap, L"без кода справочника, в ТП не идут: %d", skipped);
  int len8 = WideCharToMultiByte(CP_UTF8, 0, txt, -1, NULL, 0, NULL, NULL);
  char *u = (char *)malloc((size_t)len8);
  if (!u) return 0;
  WideCharToMultiByte(CP_UTF8, 0, txt, -1, u, len8, NULL, NULL);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    free(u);
    return -1;
  }
  DWORD wr;
  WriteFile(h, "\xEF\xBB\xBF", 3, &wr, NULL);
  WriteFile(h, u, (DWORD)(len8 - 1), &wr, NULL);
  CloseHandle(h);
  free(u);
  return n;
}

/* ---- папки хранилища — из базы ------------------------------------------------------ */

typedef struct {
  long id, parent;
  int folderLike;
  wchar_t name[200], tpl[48];
} SzmCont;

static DWORD WINAPI szm_folders_thread(LPVOID arg) {
  SzmFolders *f = (SzmFolders *)arg;
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[300] = L"";
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * CARD_ROWS);
  SzmCont *c = (SzmCont *)calloc(SZM_MAX, sizeof(SzmCont));
  int nc = 0;
  if (!rows || !c) goto done;
  g_qTimeout = 20;
  if (!g_szConnect(&env, &dbc, err, 300)) {
    lstrcpynW(f->err, err, 300);
    goto done;
  }
  for (int from = 0; nc < SZM_MAX;) { /* все папки хранилища: путь строится по родителям */
    wchar_t sql[700];
    _snwprintf(sql, 700,
               L"SELECT TOP %d c.ContainerId, CAST(c.Name AS NVARCHAR(200)), ISNULL(t.NameKey,N''), ISNULL(c.ParentId,0), 0 "
               L"FROM Containers AS c WITH(NOLOCK) LEFT JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=c.TemplateId "
               L"WHERE c.Erased=0 AND c.ContainerId>%d ORDER BY c.ContainerId",
               CARD_ROWS, from);
    int n = card_query(dbc, sql, rows, CARD_ROWS, err, 300);
    if (n < 0) {
      lstrcpynW(f->err, err, 300);
      break;
    }
    for (int i = 0; i < n && nc < SZM_MAX; i++) {
      c[nc].id = rows[i].n1;
      c[nc].parent = rows[i].n2;
      lstrcpynW(c[nc].tpl, rows[i].s2, 48);
      lstrcpynW(c[nc].name, rows[i].s1, 200);
      nc++;
    }
    if (n < CARD_ROWS) break;
    from = (int)rows[n - 1].n1;
  }
  /* Папки людей: шаблон «Папка» (Folder) в дереве, корень которого — тоже Folder или
     «Конструкторская подготовка производства» (DesignerMainFolder); сам этот корень — тоже.
     Служебные деревья (НСИ, справочники, карточки, документация) — мимо: туда изделия не кладут. */
  for (int i = 0; i < nc; i++) {
    int r = i;
    for (int depth = 0; c[r].parent && depth < 12; depth++) {
      int k = 0;
      while (k < nc && c[k].id != c[r].parent) k++;
      if (k == nc) break;
      r = k;
    }
    BOOL rootOk = !wcscmp(c[r].tpl, L"Folder") || !wcscmp(c[r].tpl, L"DesignerMainFolder");
    c[i].folderLike = rootOk && (!wcscmp(c[i].tpl, L"Folder") || !wcscmp(c[i].tpl, L"DesignerMainFolder"));
  }
  f->path = (wchar_t (*)[SZM_PATH])calloc(SZM_MAX, sizeof(*f->path));
  for (int i = 0; i < nc && f->path && f->n < SZM_MAX; i++) {
    if (!c[i].folderLike || !c[i].name[0]) continue;
    wchar_t p[SZM_PATH];
    lstrcpynW(p, c[i].name, SZM_PATH);
    long par = c[i].parent;
    for (int depth = 0; par && depth < 12; depth++) {
      int k = 0;
      while (k < nc && c[k].id != par) k++;
      if (k == nc) break;
      wchar_t q[SZM_PATH];
      _snwprintf(q, SZM_PATH, L"%s\\%s", c[k].name, p);
      q[SZM_PATH - 1] = 0;
      lstrcpynW(p, q, SZM_PATH);
      par = c[k].parent;
    }
    lstrcpynW(f->path[f->n++], p, SZM_PATH);
  }
  /* по алфавиту — вложенные рядом с родителем */
  for (int i = 1; i < f->n; i++)
    for (int k = i; k > 0 && _wcsicmp(f->path[k - 1], f->path[k]) > 0; k--) {
      wchar_t t[SZM_PATH];
      memcpy(t, f->path[k], sizeof(t));
      memcpy(f->path[k], f->path[k - 1], sizeof(t));
      memcpy(f->path[k - 1], t, sizeof(t));
    }
  SQLDisconnect(dbc);
done:
  if (dbc != SQL_NULL_HDBC) SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  if (env != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, env);
  free(rows);
  free(c);
  if (!g_szmWnd || !PostMessageW(g_szmWnd, WM_SZM_FOLDERS, 0, (LPARAM)f)) {
    free(f->path);
    free(f);
  }
  return 0;
}

/* ---- участки и станки из базы — для списков окна пакета (package_ui.c) ------------------ */

typedef struct {
  HWND notify;
} SzmDictsJob;

static DWORD WINAPI szm_dicts_thread(LPVOID arg) {
  SzmDictsJob *jb = (SzmDictsJob *)arg;
  HWND notify = jb->notify;
  free(jb);
  SzmDicts *d = (SzmDicts *)calloc(1, sizeof(SzmDicts));
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  wchar_t err[300] = L"";
  CardRow *rows = (CardRow *)malloc(sizeof(CardRow) * CARD_ROWS);
  if (!d || !rows) goto done;
  g_qTimeout = 20;
  if (!g_szConnect(&env, &dbc, err, 300)) {
    lstrcpynW(d->err, err, 300);
    goto done;
  }
  /* участок: Code и Name — «31 Механический участок 31»; станок: EquipmentModel и Name — как
     ToString() объекта в клиенте: «16К20, Токарно-винторезный станок» (так его ищет функция) */
  static const wchar_t *const kq[2] = {
      L"SELECT TOP 900 o.InfoObjectId, ISNULL(CAST(cd.ShortText AS NVARCHAR(40)),N''), CAST(o.Name AS NVARCHAR(200)), 0, 0 "
      L"FROM InfoObjects AS o WITH(NOLOCK) JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId AND t.NameKey=N'Area' "
      L"OUTER APPLY (SELECT TOP 1 c.ShortText FROM InfoObjectAttributes AS c WITH(NOLOCK) JOIN NameKeys AS nk WITH(NOLOCK) "
      L"ON nk.NameKeyId=c.NameKeyId AND nk.Value=N'Code' WHERE c.OwnerId=o.InfoObjectId AND c.Outdated=0) AS cd "
      L"WHERE o.Erased=0 ORDER BY cd.ShortText, o.Name",
      L"SELECT TOP 900 o.InfoObjectId, ISNULL(CAST(md.ShortText AS NVARCHAR(80)),N''), CAST(o.Name AS NVARCHAR(200)), 0, 0 "
      L"FROM InfoObjects AS o WITH(NOLOCK) JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId AND t.NameKey=N'Equipment' "
      L"OUTER APPLY (SELECT TOP 1 c.ShortText FROM InfoObjectAttributes AS c WITH(NOLOCK) JOIN NameKeys AS nk WITH(NOLOCK) "
      L"ON nk.NameKeyId=c.NameKeyId AND nk.Value=N'EquipmentModel' WHERE c.OwnerId=o.InfoObjectId AND c.Outdated=0) AS md "
      L"WHERE o.Erased=0 ORDER BY md.ShortText, o.Name"};
  for (int q = 0; q < 2; q++) {
    int n = card_query(dbc, kq[q], rows, CARD_ROWS, err, 300);
    if (n < 0) {
      lstrcpynW(d->err, err, 300);
      break;
    }
    wchar_t(*arr)[200] = (wchar_t(*)[200])calloc((size_t)(n > 0 ? n : 1), sizeof(*arr));
    if (!arr) break;
    int k = 0;
    for (int i = 0; i < n; i++) {
      const wchar_t *a = rows[i].s1, *b = rows[i].s2;
      /* имя объекта бывает уже с кодом/моделью впереди — не удваивать */
      if (a[0] && !wcsncmp(b, a, wcslen(a))) lstrcpynW(arr[k], b, 200);
      else if (a[0]) _snwprintf(arr[k], 200, q ? L"%s, %s" : L"%s %s", a, b), arr[k][199] = 0;
      else lstrcpynW(arr[k], b, 200);
      if (arr[k][0]) k++;
    }
    if (q == 0) d->area = arr, d->na = k;
    else d->equip = arr, d->ne = k;
  }
  SQLDisconnect(dbc);
done:
  if (dbc != SQL_NULL_HDBC) SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  if (env != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, env);
  free(rows);
  if (d && (!notify || !IsWindow(notify) || !PostMessageW(notify, WM_SZM_DICTS, 0, (LPARAM)d))) {
    free(d->area);
    free(d->equip);
    free(d);
  }
  return 0;
}

static void szm_dicts_start(HWND notify) {
  SzmDictsJob *jb = (SzmDictsJob *)calloc(1, sizeof(SzmDictsJob));
  if (!jb) return;
  jb->notify = notify;
  HANDLE th = CreateThread(NULL, 0, szm_dicts_thread, jb, 0, NULL);
  if (th) CloseHandle(th);
  else free(jb);
}

/* ---- запуск моста ----------------------------------------------------------------- */

typedef struct {
  wchar_t cmd[MAX_PATH * 2 + 8];
} SzmRun;

static DWORD WINAPI szm_run_thread(LPVOID arg) {
  SzmRun *r = (SzmRun *)arg;
  char *out = NULL, err[200] = "";
  DWORD outn = 0, code = (DWORD)-1;
  BOOL ok = run_capture(r->cmd, &out, &outn, &code, err, 200);
  wchar_t *text = ok ? utf8_to_alloc(out, outn) : NULL;
  size_t extra = 600 + (text ? wcslen(text) : 0);
  wchar_t *rep = (wchar_t *)malloc(extra * sizeof(wchar_t));
  if (rep) {
    const wchar_t *head = !ok                ? L"Мост cp-soyuz.exe не запустился"
                          : code == 0        ? L"Готово: функция «Союза» отработала"
                          : code == 1        ? L"«Союз» отказал — см. отчёт"
                          : code == 3        ? L"Клиент «Союза» не найден: запустите его и войдите"
                                             : L"Мост завершился с ошибкой";
    _snwprintf(rep, extra, L"%s (код %ld).\r\n%hs%s%s\r\n", head, (long)code, err, err[0] ? L"\r\n" : L"",
               text ? text : L"");
    rep[extra - 1] = 0;
  }
  free(text);
  free(out);
  free(r);
  InterlockedExchange(&g_szmBusy, 0);
  if (!g_szmWnd || !PostMessageW(g_szmWnd, WM_SZM_DONE, 0, (LPARAM)rep)) free(rep);
  return 0;
}

/* описание из окна пакета: ПАПКА (если есть изделия) + его строки */
static int szm_write_text(const wchar_t *path, const wchar_t *folder) {
  const wchar_t *t = g_szmText ? g_szmText : L"";
  size_t cap = wcslen(t) + SZM_PATH + 64;
  wchar_t *txt = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!txt) return -1;
  wchar_t fold[SZM_PATH] = L"";
  szm_tsv(fold, SZM_PATH, folder);
  _snwprintf(txt, cap, L"# CursorPad → «Союз»: окно «Пакет для загрузки в PLM»\r\n%s%s%s%s", fold[0] ? L"ПАПКА\t" : L"",
             fold, fold[0] ? L"\r\n" : L"", t);
  txt[cap - 1] = 0;
  int n = 0;
  for (const wchar_t *p = t; (p = wcsstr(p, L"ТП\t")) != NULL; p += 3) n++;
  int len8 = WideCharToMultiByte(CP_UTF8, 0, txt, -1, NULL, 0, NULL, NULL);
  char *u = (char *)malloc((size_t)len8);
  if (!u) {
    free(txt);
    return -1;
  }
  WideCharToMultiByte(CP_UTF8, 0, txt, -1, u, len8, NULL, NULL);
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD wr;
    WriteFile(h, "\xEF\xBB\xBF", 3, &wr, NULL);
    WriteFile(h, u, (DWORD)(len8 - 1), &wr, NULL);
    CloseHandle(h);
  } else n = -1;
  free(u);
  free(txt);
  return n;
}

static void szm_go(void) {
  if (g_szmBusy || (!g_szmText && !g_szR.n)) return;
  wchar_t folder[SZM_PATH];
  GetWindowTextW(g_szmCombo, folder, SZM_PATH);
  BOOL textMode = g_szmText != NULL;
  BOOL newPart = textMode ? wcsstr(g_szmText, L"ИЗДЕЛИЕ\t") != NULL : SendMessageW(g_szmChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (!textMode && !g_szR.des[0]) {
    szm_status(L"В маршруте нет обозначения детали («Лист: … · обозначение») — создать нечего.");
    return;
  }
  size_t l = wcslen(folder);
  while (l && (folder[l - 1] == L' ' || folder[l - 1] == L'\\')) folder[--l] = 0;
  if (newPart && !folder[0]) {
    szm_status(textMode ? L"Укажите папку «Союза», куда создать изделия." : L"Укажите папку «Союза», куда создать деталь.");
    return;
  }
  wchar_t bridge[MAX_PATH];
  if (!szm_bridge(bridge, MAX_PATH)) {
    wchar_t m[400];
    _snwprintf(m, 400, L"Нет моста %s — положите cp-soyuz.exe рядом с CursorPad.exe или укажите soyuz_bridge в plm.txt.",
               bridge);
    m[399] = 0;
    szm_status(m);
    return;
  }
  wchar_t spec[MAX_PATH], note[200] = L"";
  _snwprintf(spec, MAX_PATH, L"%s\\cursorpad-soyuz.txt", g_dataDir);
  int n = textMode ? szm_write_text(spec, folder) : szm_write_spec(spec, folder, newPart, note, 200);
  if (n < 0) {
    szm_status(L"Не удалось записать описание в папку данных.");
    return;
  }
  if (n == 0 && !newPart && !(textMode && wcsstr(g_szmText, L"СОСТАВ\t"))) {
    szm_status(L"Ни одной операции с кодом справочника — в ТП подать нечего.");
    return;
  }
  if (folder[0]) szm_pref_save(folder);
  SzmRun *r = (SzmRun *)calloc(1, sizeof(SzmRun));
  if (!r) return;
  _snwprintf(r->cmd, sizeof(r->cmd) / sizeof(wchar_t), L"\"%s\" \"%s\"", bridge, spec);
  InterlockedExchange(&g_szmBusy, 1);
  HANDLE th = CreateThread(NULL, 0, szm_run_thread, r, 0, NULL);
  if (!th) {
    free(r);
    InterlockedExchange(&g_szmBusy, 0);
    return;
  }
  CloseHandle(th);
  wchar_t m[300];
  _snwprintf(m, 300, L"Передаю в «Союз»: операций %d%s%s… Ответ клиента — в отчёте.", n, note[0] ? L"; " : L"", note);
  m[299] = 0;
  szm_status(m);
  EnableWindow(GetDlgItem(g_szmWnd, ID_SZM_OK), FALSE);
}

/* ---- окно ------------------------------------------------------------------------- */

static void szm_layout(void) {
  RECT rc;
  GetClientRect(g_szmWnd, &rc);
  int pad = SZS(16), y = SZS(40);
  MoveWindow(g_szmCombo, pad, y, rc.right - pad * 2, SZS(200), TRUE);
  y += SZS(34);
  MoveWindow(g_szmChk, pad, y, rc.right - pad * 2, SZS(24), TRUE);
  y += SZS(30);
  MoveWindow(g_szmStatus, pad, y, rc.right - pad * 2, SZS(44), TRUE);
  int by = rc.bottom - SZS(42), bw = SZS(120);
  MoveWindow(GetDlgItem(g_szmWnd, ID_SZM_OK), rc.right - pad - bw * 2 - SZS(8), by, bw, SZS(30), TRUE);
  MoveWindow(GetDlgItem(g_szmWnd, ID_SZM_CANCEL), rc.right - pad - bw, by, bw, SZS(30), TRUE);
}

static LRESULT CALLBACK SzmProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, g_paper);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COL_INK);
    if (g_fontUi) SelectObject(hdc, g_fontUi);
    wchar_t t[300];
    if (g_szmText) lstrcpynW(t, g_szmLabel[0] ? g_szmLabel : L"Папка «Союза» для новых изделий:", 300);
    else _snwprintf(t, 300, L"Папка «Союза» для детали %s %s:", g_szR.des, g_szR.name);
    t[299] = 0;
    RECT a = {SZS(16), SZS(12), rc.right - SZS(16), SZS(38)};
    DrawTextW(hdc, t, -1, &a, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, (HWND)lParam == g_szmStatus ? COL_MUTED : COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_SIZE:
    szm_layout();
    return 0;
  case WM_SZM_FOLDERS: {
    SzmFolders *f = (SzmFolders *)lParam;
    if (f && f->gen == g_szmGen && g_szmCombo) {
      wchar_t cur[SZM_PATH];
      GetWindowTextW(g_szmCombo, cur, SZM_PATH);
      SendMessageW(g_szmCombo, CB_RESETCONTENT, 0, 0);
      for (int i = 0; i < f->n; i++) SendMessageW(g_szmCombo, CB_ADDSTRING, 0, (LPARAM)f->path[i]);
      SetWindowTextW(g_szmCombo, cur);
      wchar_t m[400];
      if (f->err[0]) _snwprintf(m, 400, L"Список папок из базы не прочитан (%s) — впишите путь руками.", f->err);
      else _snwprintf(m, 400, L"Папок в хранилище: %d. Выберите или впишите путь «Корневая\\Подпапка».", f->n);
      m[399] = 0;
      szm_status(m);
    }
    if (f) free(f->path);
    free(f);
    return 0;
  }
  case WM_SZM_DONE: {
    wchar_t *rep = (wchar_t *)lParam;
    EnableWindow(GetDlgItem(hwnd, ID_SZM_OK), TRUE);
    if (rep) {
      wchar_t first[200];
      lstrcpynW(first, rep, 200);
      wchar_t *nl = wcspbrk(first, L"\r\n");
      if (nl) *nl = 0;
      szm_status(first);
      if (log_window(&g_szmLog, hwnd, L"Создание в «Союзе» — отчёт", SZS(760), SZS(480))) {
        SetWindowTextW(g_szmLog, rep);
        ShowWindow(g_szmLog, SW_SHOWNORMAL);
      }
    }
    free(rep);
    return 0;
  }
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case ID_SZM_OK: szm_go(); return 0;
    case ID_SZM_CANCEL: ShowWindow(hwnd, SW_HIDE); return 0;
    }
    return 0;
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) ShowWindow(hwnd, SW_HIDE);
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void szm_show(void);

/* кнопка «Создать ТП» окна «В Союз» — по маршруту */
static void szm_open(void) {
  if (!g_szR.n) return;
  free(g_szmText);
  g_szmText = NULL;
  szm_show();
}

/* «В Союз…» окна «Пакет для загрузки в PLM» — готовые строки ИЗДЕЛИЕ/СОСТАВ/ТП (копируются) */
static void szm_open_text(const wchar_t *text, const wchar_t *label) {
  if (!text || !text[0]) return;
  free(g_szmText);
  g_szmText = _wcsdup(text);
  lstrcpynW(g_szmLabel, label ? label : L"", 240);
  szm_show();
}

static void szm_show(void) {
  if (!g_szmWnd) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SzmProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_paper;
    wc.lpszClassName = L"CursorPadSoyuzMake";
    RegisterClassExW(&wc);
    int w = SZS(560), h = SZS(230), x = 0, y = 0;
    RECT pr;
    if (g_szWnd && GetWindowRect(g_szWnd, &pr)) x = pr.left - w - 8 > 0 ? pr.left - w - 8 : pr.left, y = pr.top + SZS(80);
    g_szmWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadSoyuzMake", L"Создать в «Союзе» — CursorPad",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, g_szWnd, NULL, g_inst, NULL);
    if (!g_szmWnd) return;
    g_szmCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWN |
                                                           CBS_AUTOHSCROLL,
                                 0, 0, 10, 10, g_szmWnd, (HMENU)(INT_PTR)ID_SZM_FOLDER, g_inst, NULL);
    g_szmChk = CreateWindowExW(0, L"BUTTON", L"Создать и саму деталь, если её ещё нет в «Союзе»",
                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, g_szmWnd,
                               (HMENU)(INT_PTR)ID_SZM_NEW, g_inst, NULL);
    g_szmStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, g_szmWnd,
                                  NULL, g_inst, NULL);
    HWND ctl[3] = {g_szmCombo, g_szmChk, g_szmStatus};
    for (int i = 0; i < 3; i++)
      if (g_fontBody) SendMessageW(ctl[i], WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    SendMessageW(g_szmChk, BM_SETCHECK, BST_CHECKED, 0);
    mk_btn(g_szmWnd, L"Создать", ID_SZM_OK);
    mk_btn(g_szmWnd, L"Закрыть", ID_SZM_CANCEL);
    szm_layout();
  }
  wchar_t last[SZM_PATH];
  szm_pref_load(last, SZM_PATH);
  SetWindowTextW(g_szmCombo, last);
  ShowWindow(g_szmChk, g_szmText ? SW_HIDE : SW_SHOW); /* в режиме текста изделия уже перечислены в описании */
  SetWindowTextW(g_szmWnd, g_szmText ? L"В «Союз» из пакета — CursorPad" : L"Создать в «Союзе» — CursorPad");
  EnableWindow(GetDlgItem(g_szmWnd, ID_SZM_OK), !g_szmBusy);
  InvalidateRect(g_szmWnd, NULL, TRUE);
  ShowWindow(g_szmWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_szmWnd);
  /* папки — из базы, если есть свой вход; без него — только руками */
  if ((g_sqlUser[0] && g_sqlPass[0]) || g_szConnect != plm_connect) {
    SzmFolders *f = (SzmFolders *)calloc(1, sizeof(SzmFolders));
    if (f) {
      f->gen = InterlockedIncrement(&g_szmGen);
      HANDLE th = CreateThread(NULL, 0, szm_folders_thread, f, 0, NULL);
      if (th) CloseHandle(th);
      else free(f);
      szm_status(L"Читаю папки хранилища из базы…");
    }
  } else {
    szm_status(L"Нет входа в базу — впишите путь папки «Корневая\\Подпапка» руками.");
  }
}
