/* search_history.c — история поиска и избранное запросов (с 2026.10.23).
   Каждый запрос, который уходит в поиск (start_lookup, pad_extra.c), попадает
   сюда. Ctrl+H в поле поиска открывает список в «Находках»: сверху избранное
   («* »), ниже недавние («· »). Двойной щелчок или Enter по строке — искать
   снова; Ctrl+D — в избранное или обратно; Del — убрать из истории. Ctrl+D в
   самом поле поиска делает то же с набранным текстом.
   Файл %LOCALAPPDATA%\CursorPad\search_history.txt: строка на запрос, как в
   store_text.h — «1<TAB>"запрос"» избранное, «0<TAB>…» обычный; свежие сверху.
   Избранное не вытесняется; всего хранится QH_MAX записей. */
#define QH_MAX 200 /* всего записей, избранное тоже в счёт */
#define QH_Q 400   /* длина запроса, как в поле поиска */
#define QH_LINE 412 /* строка списка: метка из двух знаков и запрос */

/* метки строк: ASCII и «·» (есть в любом шрифте окна, в отличие от звёздочек-символов) */
static const wchar_t kQhFav[] = L"* ";
static const wchar_t kQhPlain[] = L"· ";

typedef struct {
  wchar_t q[QH_Q];
  BOOL fav;
} QhEntry;
static QhEntry g_qh[QH_MAX];
static int g_qhN;
static BOOL g_qhLoaded;

static void qh_load(void);

static void qh_path(wchar_t *out) {
  if (g_dataDir[0]) _snwprintf(out, MAX_PATH, L"%s\\search_history.txt", g_dataDir);
  else out[0] = 0;
}

static int qh_index(const wchar_t *q) {
  qh_load();
  for (int i = 0; i < g_qhN; i++)
    if (!lstrcmpiW(g_qh[i].q, q)) return i;
  return -1;
}

static void qh_remove_at(int i) {
  memmove(&g_qh[i], &g_qh[i + 1], (size_t)(g_qhN - i - 1) * sizeof(QhEntry));
  g_qhN--;
}

static void qh_save(void) {
  wchar_t path[MAX_PATH];
  StBuf b = {0};
  qh_path(path);
  if (!path[0]) return;
  for (int i = 0; i < g_qhN; i++) {
    st_char(&b, g_qh[i].fav ? L'1' : L'0');
    st_char(&b, L'\t');
    st_quoted(&b, g_qh[i].q);
    st_char(&b, L'\n');
  }
  if (b.n) ac_write_utf8(path, b.s);
  else DeleteFileW(path);
  free(b.s);
}

/* читается один раз; до этого ничего не пишем, иначе пустой список стёр бы файл */
static void qh_load(void) {
  wchar_t path[MAX_PATH];
  if (g_qhLoaded) return;
  g_qhLoaded = TRUE;
  qh_path(path);
  wchar_t *text = path[0] ? ac_read_utf8(path, 1u << 20) : NULL;
  if (!text) return;
  for (const wchar_t *p = text; *p && g_qhN < QH_MAX; p = st_next(p)) {
    wchar_t tag = 0;
    wchar_t *q = st_record(p, &tag);
    if (q && q[0] && qh_index(q) < 0) {
      lstrcpynW(g_qh[g_qhN].q, q, QH_Q);
      g_qh[g_qhN].fav = tag == L'1';
      g_qhN++;
    }
    free(q);
  }
  free(text);
}

/* без краёв и без переводов строк внутри: строка списка должна остаться строкой */
static void qh_trim(const wchar_t *in, wchar_t *out) {
  while (*in == L' ' || *in == L'\t' || *in == L'\r' || *in == L'\n') in++;
  lstrcpynW(out, in, QH_Q);
  for (wchar_t *c = out; *c; c++)
    if (*c == L'\t' || *c == L'\r' || *c == L'\n') *c = L' ';
  size_t n = wcslen(out);
  while (n && out[n - 1] == L' ') out[--n] = 0;
}

/* запрос уходит в поиск: наверх списка; у избранного метка сохраняется */
static void qh_add(const wchar_t *raw) {
  wchar_t q[QH_Q];
  qh_load();
  qh_trim(raw, q);
  if (!q[0]) return;
  int i = qh_index(q);
  QhEntry e;
  memset(&e, 0, sizeof(e));
  if (i >= 0) {
    e = g_qh[i];
    qh_remove_at(i);
  } else if (g_qhN == QH_MAX) {
    int k = g_qhN - 1;
    while (k >= 0 && g_qh[k].fav) k--;
    if (k < 0) return; /* все записи — избранное: новому места нет */
    qh_remove_at(k);
  }
  lstrcpynW(e.q, q, QH_Q);
  memmove(&g_qh[1], &g_qh[0], (size_t)g_qhN * sizeof(QhEntry));
  g_qh[0] = e;
  g_qhN++;
  qh_save();
}

/* Ctrl+D: 1 — теперь в избранном, 0 — уже не в нём, -1 — нечего было менять */
static int qh_toggle(const wchar_t *raw) {
  wchar_t q[QH_Q];
  qh_load();
  qh_trim(raw, q);
  if (!q[0]) return -1;
  int i = qh_index(q);
  if (i < 0) {
    qh_add(q);
    i = qh_index(q);
    if (i < 0) return -1;
  }
  g_qh[i].fav = !g_qh[i].fav;
  qh_save();
  return g_qh[i].fav ? 1 : 0;
}

static void qh_forget(const wchar_t *q) {
  int i = qh_index(q);
  if (i < 0) return;
  qh_remove_at(i);
  qh_save();
}

/* текст списка для «Находок»: избранное, потом недавние, внизу подсказка */
static wchar_t *qh_text(void) {
  StBuf b = {0};
  int favs = 0, plain = 0;
  qh_load();
  for (int i = 0; i < g_qhN; i++) {
    if (g_qh[i].fav) favs++;
    else plain++;
  }
  if (!g_qhN) st_cat(&b, L"Пока пусто: каждый запрос, который вы ищете, появится здесь.\r\n");
  if (favs) {
    st_cat(&b, L"Избранное\r\n");
    for (int i = 0; i < g_qhN; i++)
      if (g_qh[i].fav) {
        st_cat(&b, kQhFav);
        st_cat(&b, g_qh[i].q);
        st_cat(&b, L"\r\n");
      }
    st_cat(&b, L"\r\n");
  }
  if (plain) {
    st_cat(&b, L"Недавние\r\n");
    for (int i = 0; i < g_qhN; i++)
      if (!g_qh[i].fav) {
        st_cat(&b, kQhPlain);
        st_cat(&b, g_qh[i].q);
        st_cat(&b, L"\r\n");
      }
  }
  st_cat(&b, L"\r\nДвойной щелчок или Enter — искать снова\r\n");
  st_cat(&b, L"Ctrl+D — в избранное или обратно, Del — убрать из истории\r\n");
  return b.s;
}

/* показать список в «Находках» (Ctrl+H в поле поиска) */
static void qh_show(void) {
  wchar_t *text = qh_text();
  g_plmCount = 0;
  g_resultFiles = FALSE;
  g_ansTitle = L"История поиска";
  g_ansPlain = TRUE;
  show_answer_text(text);
  g_qhView = TRUE;
  free(text);
  if (!g_qhN) show_status(L"История пуста: запросы появятся после первого поиска");
  else show_status(L"История: Enter или двойной щелчок — искать, Ctrl+D — избранное, Del — убрать");
}

/* строка списка, где стоит курсор: из неё — сам запрос (без метки) */
static BOOL qh_line_query(HWND edit, wchar_t *q) {
  wchar_t buf[QH_LINE];
  int line = (int)SendMessageW(edit, EM_LINEFROMCHAR, (WPARAM)-1, 0);
  *(WORD *)buf = QH_LINE - 1;
  int n = (int)SendMessageW(edit, EM_GETLINE, (WPARAM)line, (LPARAM)buf);
  if (n <= 0) return FALSE;
  buf[n] = 0;
  while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L' ')) buf[--n] = 0;
  if (wcsncmp(buf, kQhFav, 2) != 0 && wcsncmp(buf, kQhPlain, 2) != 0) return FALSE;
  lstrcpynW(q, buf + 2, QH_Q);
  return q[0] != 0;
}

/* клавиши в списке «Находок»: Enter — искать, Ctrl+D — избранное, Del — убрать */
static void qh_view_key(HWND edit, WPARAM key) {
  wchar_t q[QH_Q];
  if (!qh_line_query(edit, q)) {
    show_status(L"Здесь не запрос: выберите строку с запросом");
    return;
  }
  if (key == VK_DELETE) {
    qh_forget(q);
    qh_show();
    show_status(L"Убрано из истории");
    return;
  }
  if (key == L'D') {
    int fav = qh_toggle(q);
    qh_show();
    show_status(fav == 1 ? L"Запрос в избранном" : L"Запрос убран из избранного");
    return;
  }
  /* Enter или двойной щелчок: тот же поиск, что и с набранным текстом */
  if (g_searchEdit) SetWindowTextW(g_searchEdit, q);
  start_lookup(q);
}

/* Ctrl+D в поле поиска: набранный запрос — в избранное или обратно */
static void qh_fav_edit(void) {
  wchar_t q[QH_Q] = {0};
  if (g_searchEdit) GetWindowTextW(g_searchEdit, q, QH_Q);
  int fav = qh_toggle(q);
  if (fav < 0) show_status(L"Сначала наберите запрос, потом Ctrl+D");
  else show_status(fav == 1 ? L"Запрос в избранном · Ctrl+H — список" : L"Запрос убран из избранного");
}
