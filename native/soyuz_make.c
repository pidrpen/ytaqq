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

   Папку «Союза» выбирает человек, и для изделий, и для ТП. Списки (с 2026.10.29) — из
   самого клиента «Союза»: мост с описанием «ПАПКИ» — функция ничего не создаёт, а
   отвечает папками хранилища так, как их видит вошедший, то есть из той базы, с которой
   клиент работает сейчас. Старая функция («ПАПКИ» не знает) — из базы SQL (только SELECT
   по Containers): папки людей и поддерево у «Для основных ТП». Путь можно вписать руками
   («Корневая\Подпапка»); последние папки помнятся в soyuz.txt. Папок CursorPad не создаёт:
   нет такой — функция откажет и скажет, какой части пути не нашлось. ТП по маршруту
   ложится к изделию из поля «ТП к изделию» (по обозначению — к уже заведённому ЭСИ). */

#define WM_SZM_FOLDERS (WM_APP + 95) /* lParam — SzmFolders*, освобождает окно */
#define WM_SZM_DONE (WM_APP + 96)    /* lParam — wchar_t* отчёт, освобождает окно */
#define ID_SZM_FOLDER 4870
#define ID_SZM_NEW 4871
#define ID_SZM_OK 4872
#define ID_SZM_CANCEL 4873
#define ID_SZM_TPFOLDER 4874
#define ID_SZM_DES 4875
#define SZM_PATH 300
#define SZM_MAX 2000

typedef struct {
  LONG gen;
  BOOL client;  /* из клиента «Союза» (функция «ПАПКИ»), иначе — из базы SQL */
  int old;      /* клиент ответил, но функция старая: «ПАПКИ» не знает */
  int n, ntp;
  wchar_t (*path)[SZM_PATH]; /* папки для изделий */
  wchar_t (*tp)[SZM_PATH];   /* папки для ТП */
  wchar_t err[300];
} SzmFolders;

static HWND g_szmWnd, g_szmCombo, g_szmTpCombo, g_szmDes, g_szmChk, g_szmStatus, g_szmLog;
static volatile LONG g_szmBusy, g_szmGen, g_szmListBusy;
static int g_szmPkgNew;   /* функция в клиенте: 1 — новая (папки, чертежи), -1 — старая, 0 — не знаем */
static BOOL g_szmClientList; /* в списках — папки из клиента: SQL их не заменяет */
static BOOL g_szmSqlAsked;    /* папки читаются и из базы SQL (есть вход) */
static wchar_t *g_szmText;      /* описание из окна пакета (ИЗДЕЛИЕ, СОСТАВ, ТП без ПАПКА); NULL — по маршруту (g_szR) */
static wchar_t g_szmLabel[240]; /* подпись окна в режиме текста */
static BOOL log_window(HWND *pw, HWND owner, const wchar_t *title, int w, int h); /* route_ui.c */

static void szm_pref_path(wchar_t *p) { _snwprintf(p, MAX_PATH, L"%s\\soyuz.txt", g_dataDir), p[MAX_PATH - 1] = 0; }

/* последние папки — в soyuz.txt (UTF-8): первая строка — для изделий, вторая — для ТП, дальше —
   недавние, свежие сверху: «И<TAB>путь» и «Т<TAB>путь» (они встают в начало списков окна) */
#define SZM_RECENT 6
typedef struct {
  wchar_t cur[2][SZM_PATH];
  wchar_t rec[2][SZM_RECENT][SZM_PATH];
  int nrec[2];
} SzmPrefs;

static void szm_pref_load(SzmPrefs *pf) {
  memset(pf, 0, sizeof(*pf));
  wchar_t p[MAX_PATH];
  szm_pref_path(p);
  HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  enum { CAP = 16384 };
  char *buf = (char *)malloc(CAP);
  wchar_t *w = (wchar_t *)malloc(CAP * sizeof(wchar_t));
  DWORD n = 0;
  if (buf && w) ReadFile(h, buf, CAP - 1, &n, NULL);
  CloseHandle(h);
  if (buf && w) {
    buf[n] = 0;
    char *s = buf;
    if (n >= 3 && (unsigned char)s[0] == 0xEF) s += 3;
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, CAP)) w[0] = 0;
    w[CAP - 1] = 0;
    wchar_t *ln = w;
    for (int line = 0; ln && *ln; line++) {
      wchar_t *e = wcspbrk(ln, L"\r\n");
      wchar_t c = e ? *e : 0;
      if (e) *e++ = 0;
      if (line < 2) lstrcpynW(pf->cur[line], ln, SZM_PATH);
      else {
        int k = ln[0] == L'И' ? 0 : ln[0] == L'Т' ? 1 : -1;
        if (k >= 0 && ln[1] == L'\t' && ln[2] && pf->nrec[k] < SZM_RECENT) lstrcpynW(pf->rec[k][pf->nrec[k]++], ln + 2, SZM_PATH);
      }
      if (c == L'\r' && *e == L'\n') e++;
      ln = e;
    }
  }
  free(buf);
  free(w);
}

/* новая папка — первой в недавних, повтор (без регистра) уходит; старый файл (две строки) — его
   папка тоже в недавние */
static void szm_pref_push(SzmPrefs *pf, int k, const wchar_t *path) {
  wchar_t keep[SZM_RECENT + 1][SZM_PATH];
  int n = 0;
  if (path[0]) lstrcpynW(keep[n++], path, SZM_PATH);
  for (int i = -1; i < pf->nrec[k] && n < SZM_RECENT; i++) {
    const wchar_t *old = i < 0 ? pf->cur[k] : pf->rec[k][i];
    if (!old[0] || (i < 0 && pf->nrec[k])) continue;
    BOOL dup = FALSE;
    for (int j = 0; j < n && !dup; j++) dup = !_wcsicmp(keep[j], old);
    if (!dup) lstrcpynW(keep[n++], old, SZM_PATH);
  }
  for (int i = 0; i < n; i++) lstrcpynW(pf->rec[k][i], keep[i], SZM_PATH);
  pf->nrec[k] = n;
  if (path[0]) lstrcpynW(pf->cur[k], path, SZM_PATH);
}

static void szm_pref_save(const wchar_t *folder, const wchar_t *tpFolder) {
  SzmPrefs *pf = (SzmPrefs *)malloc(sizeof(SzmPrefs));
  enum { CAP = 16384 };
  wchar_t *line = (wchar_t *)malloc(CAP * sizeof(wchar_t));
  char *u = (char *)malloc(CAP * 3);
  if (!pf || !line || !u) {
    free(pf), free(line), free(u);
    return;
  }
  szm_pref_load(pf);
  szm_pref_push(pf, 0, folder);
  szm_pref_push(pf, 1, tpFolder);
  int at = _snwprintf(line, CAP, L"%s\r\n%s\r\n", folder, tpFolder);
  for (int k = 0; k < 2; k++)
    for (int i = 0; at > 0 && at < CAP && i < pf->nrec[k]; i++) {
      int m = _snwprintf(line + at, CAP - at, L"%s\t%s\r\n", k ? L"Т" : L"И", pf->rec[k][i]);
      at = m < 0 ? -1 : at + m;
    }
  line[CAP - 1] = 0;
  int n = at > 0 ? WideCharToMultiByte(CP_UTF8, 0, line, -1, u, CAP * 3, NULL, NULL) : 0;
  wchar_t p[MAX_PATH];
  szm_pref_path(p);
  HANDLE h = n > 0 ? CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
  if (h != INVALID_HANDLE_VALUE) {
    DWORD wr;
    WriteFile(h, "\xEF\xBB\xBF", 3, &wr, NULL);
    WriteFile(h, u, (DWORD)strlen(u), &wr, NULL);
    CloseHandle(h);
  }
  free(pf), free(line), free(u);
}

/* список папок окна: недавние сверху (из пришедшего списка — если он есть), за ними остальные */
static void szm_fill_combo(HWND cb, const SzmPrefs *pf, int k, wchar_t (*a)[SZM_PATH], int n) {
  wchar_t cur[SZM_PATH];
  GetWindowTextW(cb, cur, SZM_PATH);
  SendMessageW(cb, CB_RESETCONTENT, 0, 0);
  for (int r = 0; r < pf->nrec[k]; r++) {
    BOOL in = n <= 0;
    for (int i = 0; i < n && !in; i++) in = !_wcsicmp(a[i], pf->rec[k][r]);
    if (in) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)pf->rec[k][r]);
  }
  for (int i = 0; a && i < n; i++)
    if (SendMessageW(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)a[i]) == CB_ERR) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)a[i]);
  SetWindowTextW(cb, cur);
}

/* Мост: soyuz_bridge из plm.txt; иначе cp-soyuz.exe рядом с CursorPad.exe; иначе — из самого
   CursorPad.exe (RCDATA 312, native/cp-soyuz.exe; исходник native/cp-soyuz.cs и pidrpen/slack)
   в папку данных, заново — если там другой размер (обновился CursorPad) */
/* 0 — мост найден (путь в out); 1 — путь из plm.txt не существует; 2 — в этой сборке
   CursorPad нет ресурса моста; 3 — не удалось положить мост в папку данных (нет места,
   запрет записи или антивирус тут же удалил файл). Зовётся только по кнопке «Создать». */
static int szm_bridge(wchar_t *out, int cap) {
  if (g_szBridge[0]) {
    lstrcpynW(out, g_szBridge, cap);
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES ? 0 : 1;
  }
  GetModuleFileNameW(NULL, out, (DWORD)cap);
  wchar_t *b = wcsrchr(out, L'\\');
  if (b) b[1] = 0;
  wcsncat(out, L"cp-soyuz.exe", (size_t)cap - wcslen(out) - 1);
  if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return 0;
  HRSRC rs = FindResourceW(g_inst, MAKEINTRESOURCEW(312), RT_RCDATA);
  DWORD want = rs ? SizeofResource(g_inst, rs) : 0;
  if (!want) return 2;
  if (!g_dataDir[0]) return 3;
  _snwprintf(out, cap, L"%s\\cp-soyuz.exe", g_dataDir);
  out[cap - 1] = 0;
  WIN32_FILE_ATTRIBUTE_DATA fa;
  BOOL have = GetFileAttributesExW(out, GetFileExInfoStandard, &fa) && fa.nFileSizeLow == want;
  if (!have) have = extract_rcdata(312, out) && GetFileAttributesExW(out, GetFileExInfoStandard, &fa) &&
                    fa.nFileSizeLow == want;
  return have ? 0 : 3;
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

/* описание для функции: ПАПКА, ПАПКА_ТП, ИЗДЕЛИЕ (если просили), ТП по строкам маршрута с кодом —
   к изделию partDes (поле «ТП к изделию»: по умолчанию обозначение из маршрута, можно — уже
   заведённое ЭСИ). Возвращает число операций; текст предупреждений — в note */
static int szm_write_spec(const wchar_t *path, const wchar_t *folder, const wchar_t *tpFolder, const wchar_t *partDes,
                          BOOL newPart, wchar_t *note, int ncap) {
  note[0] = 0;
  static wchar_t txt[16000];
  txt[0] = 0;
  wchar_t des[200] = L"", name[200] = L"", fold[SZM_PATH] = L"", tpf[SZM_PATH] = L"", mat[420] = L"";
  szm_tsv(des, 200, partDes); /* значения полей — без табуляций и переводов строк */
  szm_tsv(name, 200, g_szR.name);
  szm_tsv(fold, SZM_PATH, folder);
  szm_tsv(tpf, SZM_PATH, tpFolder);
  if (g_szR.blank[0] && g_szR.mat[0]) _snwprintf(mat, 420, L"%s / %s", g_szR.blank, g_szR.mat);
  else lstrcpynW(mat, g_szR.mat, 420);
  size_t l = 0;
  l += (size_t)_snwprintf(txt + l, 16000 - l, L"# CursorPad → «Союз»: %s %s\r\n", des, name);
  if (tpf[0]) l += (size_t)_snwprintf(txt + l, 16000 - l, L"ПАПКА_ТП\t%s\r\n", tpf);
  /* без наименования строку изделия не шлём: у заведённого функция стёрла бы наименование */
  if (newPart && name[0]) {
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
  /* папки ТП: поддерево контейнера, в котором лежит «Для основных ТП» (TPStructures — туда
     функция кладёт ТП, если папку не выбрали) */
  int tpRoot = -1;
  for (int i = 0; i < nc && tpRoot < 0; i++)
    if (!_wcsicmp(c[i].name, L"Для основных ТП"))
      for (int k = 0; k < nc; k++)
        if (c[k].id == c[i].parent) tpRoot = k;
  f->path = (wchar_t (*)[SZM_PATH])calloc(SZM_MAX, sizeof(*f->path));
  f->tp = (wchar_t (*)[SZM_PATH])calloc(SZM_MAX, sizeof(*f->tp));
  for (int i = 0; i < nc && f->path && f->tp; i++) {
    if (!c[i].name[0]) continue;
    BOOL inTp = FALSE;
    wchar_t p[SZM_PATH];
    lstrcpynW(p, c[i].name, SZM_PATH);
    if (i == tpRoot) inTp = TRUE;
    long par = c[i].parent;
    for (int depth = 0; par && depth < 12; depth++) {
      int k = 0;
      while (k < nc && c[k].id != par) k++;
      if (k == nc) break;
      if (k == tpRoot) inTp = TRUE;
      wchar_t q[SZM_PATH];
      _snwprintf(q, SZM_PATH, L"%s\\%s", c[k].name, p);
      q[SZM_PATH - 1] = 0;
      lstrcpynW(p, q, SZM_PATH);
      par = c[k].parent;
    }
    if (c[i].folderLike && f->n < SZM_MAX) lstrcpynW(f->path[f->n++], p, SZM_PATH);
    if (inTp && f->ntp < SZM_MAX) lstrcpynW(f->tp[f->ntp++], p, SZM_PATH);
  }
  /* по алфавиту — вложенные рядом с родителем */
  for (int pass = 0; pass < 2; pass++) {
    wchar_t(*a)[SZM_PATH] = pass ? f->tp : f->path;
    int n = pass ? f->ntp : f->n;
    for (int i = 1; a && i < n; i++)
      for (int k = i; k > 0 && _wcsicmp(a[k - 1], a[k]) > 0; k--) {
        wchar_t t[SZM_PATH];
        memcpy(t, a[k], sizeof(t));
        memcpy(a[k], a[k - 1], sizeof(t));
        memcpy(a[k - 1], t, sizeof(t));
      }
  }
  SQLDisconnect(dbc);
done:
  if (dbc != SQL_NULL_HDBC) SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  if (env != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, env);
  free(rows);
  free(c);
  if (!g_szmWnd || !PostMessageW(g_szmWnd, WM_SZM_FOLDERS, 0, (LPARAM)f)) {
    free(f->path);
    free(f->tp);
    free(f);
  }
  return 0;
}

/* Папки — из самого клиента «Союза»: мост с описанием «ПАПКИ». Функция ничего не создаёт, а
   отвечает строками «ПАПКА\t…» и «ПАПКА_ТП\t…» — как папки видит вошедший, из той базы, с
   которой клиент сейчас работает. Старая функция «ПАПКИ» не знает («непонятная строка
   описания») — тогда остаётся список из базы SQL. */
typedef struct {
  SzmFolders *f;
  wchar_t cmd[MAX_PATH * 2 + 8];
} SzmList;

static DWORD WINAPI szm_client_folders_thread(LPVOID arg) {
  SzmList *L = (SzmList *)arg;
  SzmFolders *f = L->f;
  f->client = TRUE;
  char *out = NULL, err[200] = "";
  DWORD outn = 0, code = (DWORD)-1;
  BOOL ok = run_capture(L->cmd, &out, &outn, &code, err, 200);
  wchar_t *text = ok ? utf8_to_alloc(out, outn) : NULL;
  free(out);
  free(L);
  if (!ok) _snwprintf(f->err, 300, L"мост не запустился (%hs)", err);
  else if (code == 3) lstrcpynW(f->err, L"клиент «Союза» не запущен", 300);
  else if (text && wcsstr(text, L"непонятная строка описания")) f->old = 1;
  else if (text) {
    f->path = (wchar_t (*)[SZM_PATH])calloc(SZM_MAX, sizeof(*f->path));
    f->tp = (wchar_t (*)[SZM_PATH])calloc(SZM_MAX, sizeof(*f->tp));
    wchar_t *p = text, *line;
    while (f->path && f->tp && (line = share_next_line(&p)) != NULL) {
      if (!wcsncmp(line, L"ПАПКА\t", 6) && f->n < SZM_MAX) lstrcpynW(f->path[f->n++], line + 6, SZM_PATH);
      else if (!wcsncmp(line, L"ПАПКА_ТП\t", 9) && f->ntp < SZM_MAX) lstrcpynW(f->tp[f->ntp++], line + 9, SZM_PATH);
      else if (!wcsncmp(line, L"ОШИБКА", 6)) lstrcpynW(f->err, line, 300);
    }
    if (!f->n && !f->err[0]) lstrcpynW(f->err, L"клиент не прислал папок", 300);
  } else lstrcpynW(f->err, L"пустой ответ клиента", 300);
  free(text);
  InterlockedExchange(&g_szmListBusy, 0);
  if (!g_szmWnd || !PostMessageW(g_szmWnd, WM_SZM_FOLDERS, 0, (LPARAM)f)) {
    free(f->path);
    free(f->tp);
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
  static const wchar_t *const kq[3] = {
      L"SELECT TOP 900 o.InfoObjectId, ISNULL(CAST(cd.ShortText AS NVARCHAR(40)),N''), CAST(o.Name AS NVARCHAR(200)), 0, 0 "
      L"FROM InfoObjects AS o WITH(NOLOCK) JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId AND t.NameKey=N'Area' "
      L"OUTER APPLY (SELECT TOP 1 c.ShortText FROM InfoObjectAttributes AS c WITH(NOLOCK) JOIN NameKeys AS nk WITH(NOLOCK) "
      L"ON nk.NameKeyId=c.NameKeyId AND nk.Value=N'Code' WHERE c.OwnerId=o.InfoObjectId AND c.Outdated=0) AS cd "
      L"WHERE o.Erased=0 ORDER BY cd.ShortText, o.Name",
      L"SELECT TOP 900 o.InfoObjectId, ISNULL(CAST(md.ShortText AS NVARCHAR(80)),N''), CAST(o.Name AS NVARCHAR(200)), 0, 0 "
      L"FROM InfoObjects AS o WITH(NOLOCK) JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=o.TemplateId AND t.NameKey=N'Equipment' "
      L"OUTER APPLY (SELECT TOP 1 c.ShortText FROM InfoObjectAttributes AS c WITH(NOLOCK) JOIN NameKeys AS nk WITH(NOLOCK) "
      L"ON nk.NameKeyId=c.NameKeyId AND nk.Value=N'EquipmentModel' WHERE c.OwnerId=o.InfoObjectId AND c.Outdated=0) AS md "
      L"WHERE o.Erased=0 ORDER BY md.ShortText, o.Name",
      /* «Признак покупки» (PurchasedKind у конфигурации изделия, перечисление): какие значения есть в базе */
      L"SELECT TOP 40 0, N'', CAST(nv.NameUI AS NVARCHAR(200)), 0, 0 "
      L"FROM InfoObjectAttributes AS a WITH(NOLOCK) JOIN NameKeys AS nk WITH(NOLOCK) "
      L"ON nk.NameKeyId=a.NameKeyId AND nk.Value=N'PurchasedKind' "
      L"JOIN NamedValues AS nv WITH(NOLOCK) ON nv.NamedValueId=a.Link "
      L"WHERE a.Outdated=0 AND a.DataType=11 GROUP BY CAST(nv.NameUI AS NVARCHAR(200)) ORDER BY 3"};
  for (int q = 0; q < 3; q++) {
    int n = card_query(dbc, kq[q], rows, CARD_ROWS, err, 300);
    if (n < 0 && q == 2) break; /* признака нет в этой базе — списки участков и станков остаются */
    if (n < 0) {
      lstrcpynW(d->err, err, 300);
      break;
    }
    size_t cnt = (size_t)(n > 0 ? n : 1);
    wchar_t(*arr)[200] = (wchar_t(*)[200])calloc(cnt, sizeof(*arr));
    long *ids = q < 2 ? (long *)calloc(cnt, sizeof(long)) : NULL;
    wchar_t(*codes)[40] = q == 0 ? (wchar_t(*)[40])calloc(cnt, sizeof(*codes)) : NULL;
    if (!arr || (q < 2 && !ids) || (q == 0 && !codes)) {
      free(arr), free(ids), free(codes);
      break;
    }
    int k = 0;
    for (int i = 0; i < n; i++) {
      const wchar_t *a = rows[i].s1, *b = rows[i].s2;
      /* имя объекта бывает уже с кодом/моделью впереди — не удваивать */
      if (a[0] && !wcsncmp(b, a, wcslen(a))) lstrcpynW(arr[k], b, 200);
      else if (a[0]) _snwprintf(arr[k], 200, q ? L"%s, %s" : L"%s %s", a, b), arr[k][199] = 0;
      else lstrcpynW(arr[k], b, 200);
      if (!arr[k][0]) continue;
      if (ids) ids[k] = rows[i].n1;
      if (codes) lstrcpynW(codes[k], a, 40);
      k++;
    }
    if (q == 0) d->area = arr, d->areaId = ids, d->areaCode = codes, d->na = k;
    else if (q == 1) d->equip = arr, d->equipId = ids, d->ne = k;
    else d->buy = arr, d->nb = k;
  }
  /* какой станок ставят на операцию с этим кодом на этом участке: типовые операции и свежие
     30000 операций ТП, по каждой паре участок+код — два самых частых станка. Долго или не вышло —
     без подбора, руками */
  if (!d->err[0] && d->ne) {
    g_qTimeout = 40;
    static const wchar_t kFit[] =
        L"SELECT TOP 900 x.A, x.V, N'', x.E, x.S FROM (SELECT g.A, g.V, g.E, g.S, "
        L"ROW_NUMBER() OVER (PARTITION BY g.A, g.V ORDER BY g.S DESC) AS rn FROM ("
        L"SELECT ISNULL(ar.L,0) AS A, cd.V AS V, eq.L AS E, SUM(CASE WHEN o.T=1 THEN 1000 ELSE 1 END) AS S "
        L"FROM (SELECT TOP 30000 io.InfoObjectId AS Id, CASE WHEN t.NameKey=N'TypesOperation' THEN 1 ELSE 0 END AS T "
        L"FROM InfoObjects AS io WITH(NOLOCK) JOIN Templates AS t WITH(NOLOCK) ON t.TemplateId=io.TemplateId "
        L"AND t.NameKey IN (N'TSOperation',N'TypesOperation') WHERE io.Erased=0 ORDER BY T DESC, io.DateModified DESC) AS o "
        L"CROSS APPLY (SELECT TOP 1 l.Link AS D FROM InfoObjectAttributes AS l WITH(NOLOCK) JOIN NameKeys AS nkl WITH(NOLOCK) "
        L"ON nkl.NameKeyId=l.NameKeyId AND nkl.Value=N'Operation' WHERE l.OwnerId=o.Id AND l.Outdated=0 "
        L"AND ISNULL(l.Link,0)<>0 AND ISNULL(l.CollectionElementId,0)=0) AS dl "
        L"CROSS APPLY (SELECT TOP 1 COALESCE(NULLIF(LTRIM(RTRIM(CAST(c.ShortText AS NVARCHAR(40)))),N''), "
        L"CONVERT(NVARCHAR(40), c.IntegerNumber), CONVERT(NVARCHAR(40), c.LongNumber)) AS V "
        L"FROM InfoObjectAttributes AS c WITH(NOLOCK) JOIN NameKeys AS nkc WITH(NOLOCK) ON nkc.NameKeyId=c.NameKeyId "
        L"AND nkc.Value=N'Code' WHERE c.OwnerId=dl.D AND c.Outdated=0 AND ISNULL(c.CollectionElementId,0)=0) AS cd "
        L"OUTER APPLY (SELECT TOP 1 a.Link AS L FROM InfoObjectAttributes AS a WITH(NOLOCK) JOIN NameKeys AS nka WITH(NOLOCK) "
        L"ON nka.NameKeyId=a.NameKeyId AND nka.Value=N'ActualOperation' WHERE a.OwnerId=o.Id AND a.Outdated=0 "
        L"AND ISNULL(a.Link,0)<>0) AS ao "
        L"OUTER APPLY (SELECT TOP 1 a.Link AS L FROM InfoObjectAttributes AS a WITH(NOLOCK) JOIN NameKeys AS nka WITH(NOLOCK) "
        L"ON nka.NameKeyId=a.NameKeyId AND nka.Value=N'Area' WHERE a.OwnerId IN (o.Id, ISNULL(ao.L,0)) AND a.Outdated=0 "
        L"AND a.DataType=6 AND ISNULL(a.Link,0)<>0) AS ar "
        L"CROSS APPLY (SELECT DISTINCT e.Link AS L FROM InfoObjectAttributes AS e WITH(NOLOCK) JOIN NameKeys AS nke WITH(NOLOCK) "
        L"ON nke.NameKeyId=e.NameKeyId AND nke.Value=N'Equipment' WHERE e.OwnerId IN (o.Id, ISNULL(ao.L,0)) "
        L"AND e.Outdated=0 AND e.DataType=6 AND ISNULL(e.Link,0)<>0) AS eq "
        L"WHERE cd.V IS NOT NULL GROUP BY ISNULL(ar.L,0), cd.V, eq.L) AS g) AS x WHERE x.rn<=2 ORDER BY x.S DESC";
    int n = card_query(dbc, kFit, rows, CARD_ROWS, err, 300);
    if (n > 0 && (d->fit = (SzmFit *)calloc((size_t)n, sizeof(SzmFit))) != NULL)
      for (int i = 0; i < n; i++) {
        const wchar_t *v = rows[i].s1;
        while (*v == L' ') v++;
        if (*v < L'0' || *v > L'9') continue;
        SzmFit *f = &d->fit[d->nf++];
        f->area = rows[i].n1;
        f->code = wcstol(v, NULL, 10);
        f->equip = rows[i].n2;
        f->score = rows[i].n3;
      }
  }
  SQLDisconnect(dbc);
done:
  if (dbc != SQL_NULL_HDBC) SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  if (env != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, env);
  free(rows);
  if (d && (!notify || !IsWindow(notify) || !PostMessageW(notify, WM_SZM_DICTS, 0, (LPARAM)d))) {
    free(d->area);
    free(d->equip);
    free(d->areaId);
    free(d->equipId);
    free(d->areaCode);
    free(d->buy);
    free(d->fit);
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
  size_t extra = 800 + (text ? wcslen(text) : 0);
  wchar_t *rep = (wchar_t *)malloc(extra * sizeof(wchar_t));
  if (rep) {
    const wchar_t *head = !ok                ? L"Мост cp-soyuz.exe не запустился — его мог удалить или заблокировать антивирус"
                          : code == 0        ? L"Готово: функция «Союза» отработала"
                          : code == 1        ? L"«Союз» отказал — см. отчёт"
                          : code == 3        ? L"Клиент «Союза» не найден: запустите его и войдите"
                          : code == 2        ? L"Мост не получил описание (внутренняя ошибка CursorPad)"
                                             : L"Мост завершился с ошибкой — возможно, в «Союзе» нет пакета «CursorPad — Союз» "
                                               L"или версия клиента другая";
    const wchar_t *manual = (!ok || (code != 0 && code != 1 && code != 3))
                                ? L"Ручной путь: «ЭСИ и ТП» → «Сохранить пакет…», в «Союзе» — «Загрузить ЭСИ из CursorPad».\r\n"
                                : L"";
    _snwprintf(rep, extra, L"%s (код %ld).\r\n%hs%s%s%s\r\n", head, (long)code, err, err[0] ? L"\r\n" : L"", manual,
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

/* Описание из окна «ЭСИ и ТП»: ПАПКА (если есть изделия), ПАПКА_ТП (если выбрана) + его строки.
   Строки ЧЕРТЕЖ — только новой функции: старая не знает их и не сделала бы ничего (откажет
   на первой же непонятной строке); их число — в *dropped. Возвращает число строк ТП. */
static int szm_write_text(const wchar_t *path, const wchar_t *folder, const wchar_t *tpFolder, int *dropped) {
  const wchar_t *t = g_szmText ? g_szmText : L"";
  *dropped = 0;
  size_t cap = wcslen(t) + SZM_PATH * 2 + 96;
  wchar_t *txt = (wchar_t *)malloc(cap * sizeof(wchar_t));
  if (!txt) return -1;
  wchar_t fold[SZM_PATH] = L"", tpf[SZM_PATH] = L"";
  szm_tsv(fold, SZM_PATH, folder);
  szm_tsv(tpf, SZM_PATH, tpFolder);
  BOOL items = wcsstr(t, L"ИЗДЕЛИЕ\t") != NULL;
  int l = _snwprintf(txt, cap, L"# CursorPad → «Союз»: окно «ЭСИ и ТП»\r\n%s%s%s%s%s%s", items && fold[0] ? L"ПАПКА\t" : L"",
                     items ? fold : L"", items && fold[0] ? L"\r\n" : L"", tpf[0] ? L"ПАПКА_ТП\t" : L"", tpf,
                     tpf[0] ? L"\r\n" : L"");
  if (l < 0) l = 0;
  for (const wchar_t *p = t; *p;) { /* строки — по одной: ЧЕРТЕЖ старой функции не отдаём */
    const wchar_t *e = wcschr(p, L'\n');
    size_t n = e ? (size_t)(e - p) + 1 : wcslen(p);
    if (g_szmPkgNew != 1 && !wcsncmp(p, L"ЧЕРТЕЖ\t", 7)) (*dropped)++;
    else if ((size_t)l + n < cap) memcpy(txt + l, p, n * sizeof(wchar_t)), l += (int)n;
    p += n;
  }
  txt[l] = 0;
  int n = 0;
  for (const wchar_t *p = txt; (p = wcsstr(p, L"\nТП\t")) != NULL; p += 4) n++;
  int len8 = WideCharToMultiByte(CP_UTF8, 0, txt, -1, NULL, 0, NULL, NULL);
  char *u = (char *)malloc((size_t)len8);
  if (!u) {
    free(txt);
    return -1;
  }
  WideCharToMultiByte(CP_UTF8, 0, txt, -1, u, len8, NULL, NULL);
  /* есть ли что делать, кроме папок: изделия, состав, заготовки, ТП, чертежи */
  BOOL work = wcsstr(txt, L"ИЗДЕЛИЕ\t") || wcsstr(txt, L"СОСТАВ\t") || wcsstr(txt, L"ЗАГОТОВКА\t") || n ||
              wcsstr(txt, L"ЧЕРТЕЖ\t");
  free(txt);
  if (!work) {
    free(u);
    return -2;
  }
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD wr;
    WriteFile(h, "\xEF\xBB\xBF", 3, &wr, NULL);
    WriteFile(h, u, (DWORD)(len8 - 1), &wr, NULL);
    CloseHandle(h);
  } else n = -1;
  free(u);
  return n;
}

static void szm_go(void) {
  if (g_szmBusy || (!g_szmText && !g_szR.n)) return;
  if (g_szmListBusy) {
    szm_status(L"Подождите пару секунд: клиент «Союза» присылает папки.");
    return;
  }
  wchar_t folder[SZM_PATH], tpFolder[SZM_PATH], partDes[200] = L"";
  GetWindowTextW(g_szmCombo, folder, SZM_PATH);
  GetWindowTextW(g_szmTpCombo, tpFolder, SZM_PATH);
  BOOL textMode = g_szmText != NULL;
  BOOL newPart = textMode ? wcsstr(g_szmText, L"ИЗДЕЛИЕ\t") != NULL : SendMessageW(g_szmChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (!textMode) {
    GetWindowTextW(g_szmDes, partDes, 200);
    wchar_t *d = partDes;
    while (*d == L' ') d++;
    memmove(partDes, d, (wcslen(d) + 1) * sizeof(wchar_t));
    size_t dl = wcslen(partDes);
    while (dl && partDes[dl - 1] == L' ') partDes[--dl] = 0;
    if (!partDes[0]) {
      szm_status(L"Впишите обозначение изделия, к которому привязать ТП (уже заведённое в «Союзе» или новое).");
      SetFocus(g_szmDes);
      return;
    }
    /* к другому, уже заведённому изделию — только ТП: его наименование не трогаем */
    if (_wcsicmp(partDes, g_szR.des)) newPart = FALSE;
  }
  for (int k = 0; k < 2; k++) {
    wchar_t *f = k ? tpFolder : folder;
    size_t l = wcslen(f);
    while (l && (f[l - 1] == L' ' || f[l - 1] == L'\\')) f[--l] = 0;
  }
  if (newPart && !folder[0]) {
    szm_status(textMode ? L"Укажите папку «Союза», куда создать изделия." : L"Укажите папку «Союза», куда создать деталь.");
    return;
  }
  wchar_t bridge[MAX_PATH];
  int why = szm_bridge(bridge, MAX_PATH);
  if (why) {
    wchar_t m[500];
    _snwprintf(m, 500, L"%s Ручной путь есть: «ЭСИ и ТП» → «Сохранить пакет…», в «Союзе» — «Загрузить ЭСИ из CursorPad».",
               why == 1 ? L"В plm.txt указан мост soyuz_bridge, но такого файла нет."
               : why == 2 ? L"В этой сборке CursorPad нет моста cp-soyuz.exe (ресурс не собран)."
                          : L"Не удалось положить мост cp-soyuz.exe в папку данных — нет места, запрет записи или его "
                            L"удалил антивирус (добавьте папку данных CursorPad в исключения).");
    m[499] = 0;
    szm_status(m);
    return;
  }
  wchar_t spec[MAX_PATH], note[200] = L"";
  _snwprintf(spec, MAX_PATH, L"%s\\cursorpad-soyuz.txt", g_dataDir);
  int dropped = 0;
  int n = textMode ? szm_write_text(spec, folder, tpFolder, &dropped)
                   : szm_write_spec(spec, folder, tpFolder, partDes, newPart, note, 200);
  if (n == -2) {
    szm_status(dropped ? L"Остались одни чертежи, а функция «Союза» в клиенте старая — привязывать их не умеет. Обновите "
                         L"пакет «CursorPad — Союз» (см. «Что нужно?»)."
                       : L"В «Союз» подать нечего.");
    return;
  }
  if (n < 0) {
    szm_status(L"Не удалось записать описание в папку данных.");
    return;
  }
  if (n == 0 && !newPart && !textMode) {
    szm_status(L"Ни одной операции с кодом справочника — в ТП подать нечего.");
    return;
  }
  if (dropped) _snwprintf(note, 200, L"чертежи (%d) не идут: функция «Союза» старая", dropped);
  szm_pref_save(folder, tpFolder);
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
  wchar_t m[400];
  _snwprintf(m, 400, L"Передаю в «Союз»: операций %d%s%s%s%s… Ответ клиента — в отчёте.", n, !textMode ? L" к " : L"",
             !textMode ? partDes : L"", note[0] ? L"; " : L"", note);
  m[399] = 0;
  szm_status(m);
  EnableWindow(GetDlgItem(g_szmWnd, ID_SZM_OK), FALSE);
}

/* ---- окно ------------------------------------------------------------------------- */

/* сверху вниз: папка изделий, папка ТП, (по маршруту) «ТП к изделию» и галочка, строка состояния;
   подписи над полями рисует WM_PAINT по тем же g_szmLabelY */
static int g_szmLabelY[3];

static void szm_layout(void) {
  RECT rc;
  GetClientRect(g_szmWnd, &rc);
  int pad = SZS(16), w = rc.right - pad * 2, y = SZS(12);
  BOOL route = g_szmText == NULL;
  g_szmLabelY[0] = y;
  MoveWindow(g_szmCombo, pad, y + SZS(26), w, SZS(200), TRUE);
  y += SZS(60);
  g_szmLabelY[1] = y;
  MoveWindow(g_szmTpCombo, pad, y + SZS(26), w, SZS(200), TRUE);
  y += SZS(60);
  g_szmLabelY[2] = route ? y : -1;
  if (route) {
    MoveWindow(g_szmDes, pad, y + SZS(26), w, SZS(26), TRUE);
    y += SZS(60);
    MoveWindow(g_szmChk, pad, y, w, SZS(24), TRUE);
    y += SZS(30);
  }
  MoveWindow(g_szmStatus, pad, y, w, SZS(48), TRUE);
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
    wchar_t t[3][300];
    if (g_szmText) _snwprintf(t[0], 300, L"%s — папка «Союза» для новых изделий:", g_szmLabel[0] ? g_szmLabel : L"Из «ЭСИ и ТП»");
    else _snwprintf(t[0], 300, L"Папка «Союза» для детали %s %s (если создавать):", g_szR.des, g_szR.name);
    lstrcpynW(t[1], L"Папка для ТП (пусто — «Для основных ТП»):", 300);
    lstrcpynW(t[2], L"ТП — к изделию (обозначение; можно уже заведённое в «Союзе» ЭСИ):", 300);
    for (int i = 0; i < 3; i++) {
      if (g_szmLabelY[i] < 0) continue;
      t[i][299] = 0;
      RECT a = {SZS(16), g_szmLabelY[i], rc.right - SZS(16), g_szmLabelY[i] + SZS(24)};
      DrawTextW(hdc, t[i], -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, (HWND)lParam == g_szmStatus ? COL_MUTED : COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_CTLCOLOREDIT: /* поля и выпадающие списки — в цветах темы (в тёмных темах были белыми) */
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
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
      if (f->client && !f->err[0] && !f->old) g_szmPkgNew = 1;
      if (f->client && f->old) g_szmPkgNew = -1;
      /* папки из клиента — та база, с которой он работает: SQL (он может прийти позже) их не заменяет */
      BOOL use = f->client ? f->n > 0 : !g_szmClientList;
      if (use) {
        if (f->client) g_szmClientList = TRUE;
        SzmPrefs *pf = (SzmPrefs *)malloc(sizeof(SzmPrefs));
        if (pf) {
          szm_pref_load(pf);
          szm_fill_combo(g_szmCombo, pf, 0, f->path, f->n);
          szm_fill_combo(g_szmTpCombo, pf, 1, f->tp, f->ntp);
          free(pf);
        }
      }
      wchar_t m[500];
      if (f->client && use)
        _snwprintf(m, 500, L"Папки — из клиента «Союза» (база, с которой он работает): для изделий %d, для ТП %d.", f->n, f->ntp);
      else if (f->client && f->old)
        _snwprintf(m, 500, L"Функция «Союза» в клиенте старая — папки из базы SQL %s (%s), они могут быть не из той базы, что "
                           L"у клиента. Обновите пакет «CursorPad — Союз».",
                   g_plmDatabase[0] ? g_plmDatabase : L"по умолчанию", g_sqlHost);
      else if (f->client && !g_szmSqlAsked) /* клиент не ответил, а базы SQL нет */
        _snwprintf(m, 500, L"Из клиента «Союза» папки не получены (%s) — впишите путь «Корневая\\Подпапка» руками.", f->err);
      else if (f->client) m[0] = 0; /* клиент не ответил: остаётся то, что прочитали из SQL */
      else if (g_szmClientList) m[0] = 0;
      else if (f->err[0])
        _snwprintf(m, 500, L"Список папок из базы не прочитан (%s) — впишите путь руками или запустите клиент «Союза».", f->err);
      else
        _snwprintf(m, 500, L"Папки из базы SQL %s (%s): для изделий %d, для ТП %d. Список из самого клиента — когда он "
                           L"запущен и пакет «CursorPad — Союз» новый.",
                   g_plmDatabase[0] ? g_plmDatabase : L"по умолчанию", g_sqlHost, f->n, f->ntp);
      m[499] = 0;
      if (m[0]) szm_status(m);
    }
    if (f) free(f->path), free(f->tp);
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
  BOOL route = g_szmText == NULL;
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
    g_szmWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadSoyuzMake", L"Создать в «Союзе» — CursorPad",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, SZS(680), SZS(300), g_szWnd, NULL, g_inst, NULL);
    if (!g_szmWnd) return;
    for (int k = 0; k < 2; k++) {
      HWND cb = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWN |
                                                         CBS_AUTOHSCROLL,
                                0, 0, 10, 10, g_szmWnd, (HMENU)(INT_PTR)(k ? ID_SZM_TPFOLDER : ID_SZM_FOLDER), g_inst, NULL);
      if (k) g_szmTpCombo = cb;
      else g_szmCombo = cb;
    }
    g_szmDes = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10,
                               10, g_szmWnd, (HMENU)(INT_PTR)ID_SZM_DES, g_inst, NULL);
    g_szmChk = CreateWindowExW(0, L"BUTTON", L"Создать и саму деталь, если её ещё нет в «Союзе»",
                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, g_szmWnd,
                               (HMENU)(INT_PTR)ID_SZM_NEW, g_inst, NULL);
    g_szmStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, g_szmWnd,
                                  NULL, g_inst, NULL);
    HWND ctl[5] = {g_szmCombo, g_szmTpCombo, g_szmDes, g_szmChk, g_szmStatus};
    for (int i = 0; i < 5; i++)
      if (g_fontBody) SendMessageW(ctl[i], WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    SendMessageW(g_szmChk, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_szmCombo, CB_SETDROPPEDWIDTH, (WPARAM)SZS(640), 0);
    SendMessageW(g_szmTpCombo, CB_SETDROPPEDWIDTH, (WPARAM)SZS(640), 0);
    mk_btn(g_szmWnd, L"Создать", ID_SZM_OK);
    mk_btn(g_szmWnd, L"Закрыть", ID_SZM_CANCEL);
  }
  /* высота — по режиму: по маршруту есть ещё «ТП к изделию» и галочка */
  int w = SZS(680), h = route ? SZS(372) : SZS(282), x = 0, y = 0;
  RECT pr;
  HWND beside = g_szWnd && IsWindowVisible(g_szWnd) ? g_szWnd : g_pkWnd; /* у окна «В Союз» или у окна пакета */
  if (beside && GetWindowRect(beside, &pr)) {
    x = pr.left - w - 8 > 0 ? pr.left - w - 8 : pr.left + SZS(40);
    y = pr.top + SZS(80);
  }
  SetWindowPos(g_szmWnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  ShowWindow(g_szmDes, route ? SW_SHOW : SW_HIDE);
  ShowWindow(g_szmChk, route ? SW_SHOW : SW_HIDE); /* в режиме текста изделия уже перечислены в описании */
  szm_layout();
  SzmPrefs *pf = (SzmPrefs *)malloc(sizeof(SzmPrefs));
  if (pf) { /* последняя папка — в поле, недавние — в списке, пока не пришёл список из «Союза» */
    szm_pref_load(pf);
    SetWindowTextW(g_szmCombo, pf->cur[0]);
    SetWindowTextW(g_szmTpCombo, pf->cur[1]);
    szm_fill_combo(g_szmCombo, pf, 0, NULL, 0);
    szm_fill_combo(g_szmTpCombo, pf, 1, NULL, 0);
    free(pf);
  }
  if (route) SetWindowTextW(g_szmDes, g_szR.des);
  SetWindowTextW(g_szmWnd, route ? L"Создать ТП в «Союзе» — CursorPad" : L"В «Союз» из «ЭСИ и ТП» — CursorPad");
  EnableWindow(GetDlgItem(g_szmWnd, ID_SZM_OK), !g_szmBusy);
  InvalidateRect(g_szmWnd, NULL, TRUE);
  ShowWindow(g_szmWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_szmWnd);
  /* папки: из клиента «Союза» (та база, с которой он работает) и, если есть вход, из базы SQL —
     пока клиент отвечает или если функция в нём старая */
  LONG gen = InterlockedIncrement(&g_szmGen);
  g_szmClientList = FALSE;
  g_szmSqlAsked = (g_sqlUser[0] && g_sqlPass[0]) || g_szConnect != plm_connect;
  if (g_szmSqlAsked) {
    SzmFolders *f = (SzmFolders *)calloc(1, sizeof(SzmFolders));
    if (f) {
      f->gen = gen;
      HANDLE th = CreateThread(NULL, 0, szm_folders_thread, f, 0, NULL);
      if (th) CloseHandle(th);
      else free(f);
    }
  }
  wchar_t bridge[MAX_PATH];
  SzmList *L = NULL;
  if (g_dataDir[0] && !g_szmBusy && !g_szmListBusy && szm_bridge(bridge, MAX_PATH) == 0 &&
      (L = (SzmList *)calloc(1, sizeof(SzmList))) != NULL && (L->f = (SzmFolders *)calloc(1, sizeof(SzmFolders))) != NULL) {
    wchar_t spec[MAX_PATH];
    _snwprintf(spec, MAX_PATH, L"%s\\cursorpad-folders.txt", g_dataDir);
    HANDLE h = CreateFileW(spec, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      static const char kAsk[] = "\xEF\xBB\xBF# CursorPad: папки хранилища — ничего не создавать\r\n\xD0\x9F\xD0\x90\xD0\x9F"
                                 "\xD0\x9A\xD0\x98\r\n"; /* «ПАПКИ» */
      DWORD wr;
      WriteFile(h, kAsk, (DWORD)(sizeof(kAsk) - 1), &wr, NULL);
      CloseHandle(h);
      L->f->gen = gen;
      _snwprintf(L->cmd, sizeof(L->cmd) / sizeof(wchar_t), L"\"%s\" \"%s\"", bridge, spec);
      InterlockedExchange(&g_szmListBusy, 1);
      HANDLE th = CreateThread(NULL, 0, szm_client_folders_thread, L, 0, NULL);
      if (th) CloseHandle(th), L = NULL;
      else InterlockedExchange(&g_szmListBusy, 0);
    }
  }
  if (L) free(L->f), free(L);
  szm_status(g_szmListBusy ? L"Спрашиваю папки у клиента «Союза»…"
             : g_szmSqlAsked ? L"Читаю папки хранилища из базы…"
                             : L"Нет входа в базу и клиент «Союза» не спрошен — впишите путь папки «Корневая\\Подпапка» руками.");
}
