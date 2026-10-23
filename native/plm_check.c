/* plm_check.c — сверка списка с PLM «Союз» (с 2026.10.23): есть ли там
   обозначение и то ли у него наименование. Список приходит из сообщения чата,
   буфера, файла или со штампа и спецификации чертежа; здесь — разбор списка,
   сравнение наименований и итог для чата. Windows не нужен, проверяется на Linux:
     gcc -O2 -Wall -Werror -o plm_check_test plm_check_test.c && ./plm_check_test
   Подключается после search_route.c: обозначение ищется тем же шаблоном штампа
   (sr_find_des). Сам PLM читают ai_app_tools.c и share.c (запрос «check») —
   только чтение, в Союз ничего не пишется.

   Позиция — обозначение (буквенный код отдельно от ядра «3424-123.01») и
   наименование, если оно было рядом. Ищется в PLM ядро: код в именах объектов
   пишут не везде. Итог по каждой позиции:
     есть и наименование совпадает (регистр, «ё», знаки и пробелы не в счёт);
     записано иначе — те же слова в другом порядке или с сокращением;
     наименование другое;
     точного обозначения нет, есть похожие — исполнение «-01», другой код, дочерние;
     нет в Союзе; не проверено — связи нет или не успели. */

#define PC_MAX 200 /* позиций в одной сверке */
#define PC_DES 120
#define PC_NAME 240
#define PC_ROWS 6 /* строк PLM на позицию */
#define PC_CODE 16

/* итог позиции; PC_NONE занят в wingdi.h — отсюда PCS_ */
enum { PCS_TODO, PCS_OK, PCS_NEAR, PCS_DIFF, PCS_SIMILAR, PCS_NONE, PCS_FAIL };

typedef struct {
  long id;
  int tmpl, same; /* same — 2 то же обозначение, 1 похожее, 0 другое */
  wchar_t des[PC_DES], name[PC_NAME];
} PcRow;

typedef struct {
  wchar_t des[PC_DES];   /* как в списке, код заглавными: «АДЕ 3424-123.01»; пусто — позиция по наименованию */
  wchar_t core[PC_DES];  /* без кода и пробелов — так ищется в PLM: «3424-123.01» */
  wchar_t code[PC_CODE]; /* «АДЕ» или пусто */
  wchar_t name[PC_NAME]; /* наименование из списка; пусто — не указано */
  int line;              /* строка источника, с 1 */
  int doubt;             /* прочитано со скана неуверенно */
  int state, best, nrows;
  PcRow rows[PC_ROWS];
} PcItem;

typedef struct {
  PcItem *it; /* PC_MAX позиций, выделяет вызывающий */
  int n, dup, cut;
} PcList;

/* ---- строки ------------------------------------------------------------------ */

static void pc_ncpy(wchar_t *d, int cap, const wchar_t *s, int len) {
  int n = 0;
  while (n < len && s[n] && n < cap - 1) d[n] = s[n], n++;
  d[n] = 0;
}

static int pc_letter(wchar_t c) {
  wchar_t f = sr_fold(c);
  return (f >= L'a' && f <= L'z') || (f >= 0x430 && f <= 0x44F);
}

static int pc_letters(const wchar_t *s) {
  int n = 0;
  for (; s && *s; s++) n += pc_letter(*s);
  return n;
}

/* знак на краю наименования — не часть его */
static int pc_edge(wchar_t c) {
  return sr_space(c) || wcschr(L",;:-.?!*/\\|«»\"'()[]<>\x2013\x2014\x2022\x2116", c) != NULL;
}

static void pc_trim(wchar_t *s) {
  int n = (int)wcslen(s), b = 0;
  while (b < n && pc_edge(s[b])) b++;
  while (n > b && pc_edge(s[n - 1])) n--;
  /* закрывающая скобка у «Болт (М12)» — своя, её не трогаем */
  if (n > b && s[n] == L')' && wmemchr(s + b, L'(', (size_t)(n - b))) n++;
  memmove(s, s + b, (size_t)(n - b) * sizeof(wchar_t));
  s[n - b] = 0;
}

/* одинаковы ли строки без учёта регистра, «ё» и похожей латиницы */
static int pc_same_fold(const wchar_t *a, const wchar_t *b) {
  for (; *a && *b; a++, b++)
    if (sr_fold(*a) != sr_fold(*b)) return 0;
  return !*a && !*b;
}

/* ---- обозначение ------------------------------------------------------------- */

/* код и ядро: «АДЕ 3424-123.01» → «АДЕ» и «3424-123.01»; «АДЕ.830.МК» — всё ядро */
static void pc_split_des(const wchar_t *des, wchar_t *code, wchar_t *core) {
  code[0] = core[0] = 0;
  const wchar_t *sp = wcschr(des, L' ');
  const wchar_t *from = des;
  if (sp && sp - des < PC_CODE && sp > des) {
    int letters = 1;
    for (const wchar_t *q = des; q < sp; q++)
      if (!dr_isupper_cyr(*q) && !(*q >= L'A' && *q <= L'Z')) letters = 0;
    if (letters && dr_isdig(sp[1])) {
      pc_ncpy(code, PC_CODE, des, (int)(sp - des));
      from = sp + 1;
    }
  }
  int n = 0;
  for (; *from && n < PC_DES - 1; from++)
    if (!sr_space(*from)) core[n++] = sr_upper(*from);
  core[n] = 0;
}

/* Обозначение в строке PLM. Шаблон штампа находит его и в «…ТП», и в имени
   с наименованием в скобках; не нашёл — всё до скобки или двоеточия. */
static void pc_find_des(const wchar_t *s, wchar_t *des, int cap) {
  wchar_t p[SR_Q], u[SR_Q];
  sr_prep(s, p, SR_Q);
  int i = 0;
  for (; p[i]; i++) u[i] = sr_upper(p[i]);
  u[i] = 0;
  int b, e;
  if (sr_find_des(p, u, &b, &e)) {
    pc_ncpy(des, cap, u + b, e - b);
    return;
  }
  int n = 0;
  while (p[n] && !wcschr(L"[(:<", p[n])) n++;
  pc_ncpy(des, cap, p, n);
  pc_trim(des);
}

/* Та же ли позиция в строке PLM: 2 — ядро совпало (и код, если он есть у
   обеих), 1 — похожая (другой код, исполнение, дочерняя или родитель), 0 — другая */
static int pc_des_cmp(const wchar_t *code, const wchar_t *core, const wchar_t *rowDes) {
  wchar_t d[PC_DES], rc[PC_CODE], rk[PC_DES];
  pc_find_des(rowDes, d, PC_DES);
  pc_split_des(d, rc, rk);
  if (!core[0] || !rk[0]) return 0;
  size_t a = wcslen(core), b = wcslen(rk);
  if (pc_same_fold(core, rk)) return code[0] && rc[0] && !pc_same_fold(code, rc) ? 1 : 2;
  /* «3424-123.01» и «3424-123.01-01» или «3424-123.01.02» — одно семейство */
  const wchar_t *lng = a > b ? core : rk, *shr = a > b ? rk : core;
  size_t sl = a > b ? b : a;
  for (size_t k = 0; k < sl; k++)
    if (sr_fold(lng[k]) != sr_fold(shr[k])) return 0;
  return lng[sl] == L'.' || lng[sl] == L'-' ? 1 : 0;
}

/* ---- наименование ------------------------------------------------------------ */

#define PC_WORDS 16
#define PC_WLEN 40

/* слова для сравнения; предлоги «с», «в», «на» не в счёт — их и в вопросе
   чата нет (pc_clean_name), и в Союзе пишут то с ними, то без */
static int pc_words(const wchar_t *s, wchar_t w[][PC_WLEN], int max) {
  int n = 0;
  while (s && *s && n < max) {
    while (*s && !sr_alnum(*s)) s++;
    if (!*s) break;
    int k = 0, dig = 0;
    for (; *s && sr_alnum(*s); s++) {
      dig |= dr_isdig(*s);
      if (k < PC_WLEN - 1) w[n][k++] = sr_fold(*s);
    }
    w[n][k] = 0;
    if (k > 2 || dig) n++;
  }
  return n;
}

/* слово то же: целиком, сокращение («кронш» — «кронштейн») или другое окончание
   («опорный» — «опорная»). Числа — только целиком: 12 не 120. */
static int pc_word_like(const wchar_t *a, const wchar_t *b) {
  if (!wcscmp(a, b)) return 1;
  if (dr_isdig(a[0]) || dr_isdig(b[0])) return 0;
  size_t la = wcslen(a), lb = wcslen(b), k = 0;
  while (a[k] && a[k] == b[k]) k++;
  size_t sh = la < lb ? la : lb, lg = la < lb ? lb : la;
  if (k == sh && sh >= 3) return 1;
  return sh >= 5 && k >= 4 && k + 3 >= lg;
}

/* 2 — совпадает, 1 — те же слова иначе (порядок, сокращения, окончания), 0 — другое */
static int pc_name_cmp(const wchar_t *a, const wchar_t *b) {
  wchar_t wa[PC_WORDS][PC_WLEN], wb[PC_WORDS][PC_WLEN];
  int na = pc_words(a, wa, PC_WORDS), nb = pc_words(b, wb, PC_WORDS);
  if (!na || !nb) return 0;
  int same = na == nb;
  for (int i = 0; i < na && same; i++) same = !wcscmp(wa[i], wb[i]);
  if (same) return 2;
  for (int pass = 0; pass < 2; pass++) {
    wchar_t(*x)[PC_WLEN] = pass ? wb : wa, (*y)[PC_WLEN] = pass ? wa : wb;
    int nx = pass ? nb : na, ny = pass ? na : nb;
    for (int i = 0; i < nx; i++) {
      int hit = 0;
      for (int j = 0; j < ny && !hit; j++) hit = pc_word_like(x[i], y[j]);
      if (!hit) return 0;
    }
  }
  return 1;
}

/* служебные слова вопроса в чате — не наименование: «есть ли в союзе …» */
static const wchar_t *const kPcAsk[] = {
    L"есть", L"ли", L"нет", L"нету", L"в", L"во", L"на", L"из", L"с", L"со", L"по", L"у", L"и", L"или", L"а",
    L"к", L"о", L"об", L"же", L"союз", L"союзе", L"союза", L"союзу", L"plm", L"плм", L"пдм", L"база", L"базе",
    L"базу", L"бд", L"проверь", L"проверить", L"проверьте", L"проверка", L"проверим", L"сверь", L"сверить",
    L"сверьте", L"сверка", L"сравни", L"сравнить", L"сравните", L"наличие", L"наличия", L"обозначение",
    L"обозначения", L"обозначением", L"наименование", L"наименования", L"наименованием", L"название",
    L"названием", L"называется", L"найди", L"найти", L"глянь", L"посмотри", L"такое", L"такой", L"такая",
    L"такие", L"там", L"пожалуйста", L"мне", L"подскажи", L"скажи", L"это", L"что", L"совпадает",
    L"совпадают", L"заведено", L"заведена", L"заведен", L"заведены", L"имеется", L"имеются", L"правильно",
    L"верно", L"список", L"списком", L"следующие", L"эти", L"этих", L"вообще", L"ещё", L"еще", L"тоже",
    L"также", L"точно", L"чего", L"каких", L"какие", L"отсутствует", L"отсутствуют"};
/* слова перед обозначением, которые говорят, что это, а не как называется */
static const wchar_t *const kPcKind[] = {L"деталь", L"детали", L"сборка", L"сборку", L"сборки", L"сборочная",
                                         L"единица", L"изделие", L"изделия", L"узел", L"чертёж", L"чертеж",
                                         L"позиция", L"позицию", L"позиции", L"номер"};

/* Наименование из куска текста: края, «2 шт.» в конце, а в чате — слова
   вопроса (kind — и «деталь», «сборка»: так говорят перед обозначением). */
static void pc_clean_name(const wchar_t *in, int len, int chat, int kind, wchar_t *out) {
  wchar_t t[PC_NAME];
  pc_ncpy(t, PC_NAME, in, len);
  /* в чате наименование часто в кавычках — тогда только оно */
  if (chat) {
    wchar_t *q1 = wcschr(t, 0xAB), *q2 = q1 ? wcschr(q1 + 1, 0xBB) : NULL;
    if (!q1) q1 = wcschr(t, L'"'), q2 = q1 ? wcschr(q1 + 1, L'"') : NULL;
    if (q1 && q2 && q2 > q1 + 1) {
      pc_ncpy(out, PC_NAME, q1 + 1, (int)(q2 - q1 - 1));
      pc_trim(out);
      return;
    }
  }
  out[0] = 0;
  int o = 0;
  for (int i = 0; t[i];) {
    while (t[i] && sr_space(t[i])) i++;
    int j = i;
    while (t[j] && !sr_space(t[j])) j++;
    if (j == i) break;
    /* «ли?», «союзе,» — знаки на краях слова не мешают узнать его */
    int a = i, z = j;
    while (a < z && pc_edge(t[a])) a++;
    while (z > a && pc_edge(t[z - 1])) z--;
    int skip = chat && z > a &&
               (sr_in(t + a, z - a, kPcAsk, (int)(sizeof(kPcAsk) / sizeof(kPcAsk[0]))) ||
                (kind && sr_in(t + a, z - a, kPcKind, (int)(sizeof(kPcKind) / sizeof(kPcKind[0])))));
    if (!skip) {
      if (o && o < PC_NAME - 1) out[o++] = L' ';
      for (int k = i; k < j && o < PC_NAME - 1; k++) out[o++] = t[k];
      out[o] = 0;
    }
    i = j;
  }
  pc_trim(out);
  /* «… 2 шт.» — количество, не наименование */
  int n = (int)wcslen(out);
  if (n > 3 && sr_fold(out[n - 1]) == L'т' && sr_fold(out[n - 2]) == L'ш') {
    int k = n - 2;
    while (k > 0 && out[k - 1] == L' ') k--;
    int d = k;
    while (d > 0 && dr_isdig(out[d - 1])) d--;
    if (d < k && (d == 0 || out[d - 1] == L' ')) {
      out[d] = 0;
      pc_trim(out);
    }
  }
  if (pc_letters(out) < 2) out[0] = 0;
}

/* ---- список ------------------------------------------------------------------ */

/* Позиция по уже найденному обозначению (заглавными, как его отдал шаблон).
   Повтор той же позиции не добавляется; у повтора может быть наименование,
   которого не было у первой. */
static PcItem *pc_add(PcList *l, const wchar_t *des, const wchar_t *name, int line) {
  wchar_t code[PC_CODE], core[PC_DES];
  pc_split_des(des, code, core);
  if (!core[0]) return NULL;
  for (int i = 0; i < l->n; i++) {
    PcItem *o = &l->it[i];
    if (!o->des[0] || !pc_same_fold(o->core, core) || !pc_same_fold(o->code, code)) continue;
    if (!o->name[0] && name && name[0]) pc_ncpy(o->name, PC_NAME, name, PC_NAME);
    l->dup++;
    return NULL;
  }
  if (l->n >= PC_MAX) {
    l->cut++;
    return NULL;
  }
  PcItem *it = &l->it[l->n++];
  memset(it, 0, sizeof(*it));
  pc_ncpy(it->des, PC_DES, des, PC_DES);
  wcscpy(it->code, code);
  wcscpy(it->core, core);
  if (name) pc_ncpy(it->name, PC_NAME, name, PC_NAME);
  it->line = line;
  it->best = -1;
  return it;
}

/* позиция только по наименованию (обозначения в вопросе нет) */
static PcItem *pc_add_name(PcList *l, const wchar_t *name, int line) {
  if (l->n >= PC_MAX || !name || pc_letters(name) < 3) return NULL;
  PcItem *it = &l->it[l->n++];
  memset(it, 0, sizeof(*it));
  pc_ncpy(it->name, PC_NAME, name, PC_NAME);
  it->line = line;
  it->best = -1;
  return it;
}

/* «АДЕ3424-123.01» — код прилип к числу (так пишут наспех и так читает
   Tesseract): пробел между заглавным кодом из 2–5 букв и цифрой. Только для
   текста, где шаблон без этого обозначения не нашёл. */
static int pc_unglue(const wchar_t *p, wchar_t *out, int cap) {
  int o = 0, changed = 0;
  for (int i = 0; p[i] && o < cap - 2; i++) {
    out[o++] = p[i];
    if (!dr_isdig(p[i + 1]) || !dr_isupper_cyr(p[i])) continue;
    int b = i;
    while (b > 0 && dr_isupper_cyr(p[b - 1])) b--;
    if (i - b + 1 >= 2 && i - b + 1 <= 5 && (b == 0 || !sr_alnum(p[b - 1]))) {
      out[o++] = L' ';
      changed = 1;
    }
  }
  out[o] = 0;
  return changed;
}

/* Обозначение из текста поля (графа штампа, столбец спецификации): то, что
   нашёл шаблон; не нашёл — 0. */
static int pc_add_field(PcList *l, const wchar_t *desText, const wchar_t *name, int line, int doubt) {
  wchar_t des[PC_DES];
  wchar_t p[SR_Q], u[SR_Q], g[SR_Q];
  sr_prep(desText, p, SR_Q);
  int b, e;
  for (int pass = 0; pass < 2; pass++) {
    if (pass && !pc_unglue(p, g, SR_Q)) return 0;
    if (pass) wcscpy(p, g);
    int i = 0;
    for (; p[i]; i++) u[i] = sr_upper(p[i]);
    u[i] = 0;
    if (sr_find_des(p, u, &b, &e)) break;
    if (pass) return 0;
  }
  pc_ncpy(des, PC_DES, u + b, e - b);
  wchar_t nm[PC_NAME];
  pc_ncpy(nm, PC_NAME, name ? name : L"", PC_NAME);
  pc_trim(nm);
  PcItem *it = pc_add(l, des, nm, line);
  if (it) it->doubt = doubt;
  return 1;
}

#define PC_CELLS 40
#define PC_SPANS 48

/* одна ячейка: обозначения [b, e) по шаблону штампа, слева направо */
static int pc_spans(const wchar_t *p, const wchar_t *u, int *sb, int *se, int max) {
  int n = 0, off = 0;
  while (n < max && p[off]) {
    int b, e;
    if (!sr_find_des(p + off, u + off, &b, &e)) break;
    sb[n] = off + b;
    se[n] = off + e;
    if (se[n] <= off) break;
    off = se[n++];
  }
  return n;
}

/* Одна строка списка (или кусок длинной строки без табуляций). */
static void pc_parse_line(PcList *l, const wchar_t *s, int len, int line, int chat) {
  wchar_t(*cells)[SR_Q] = (wchar_t(*)[SR_Q])malloc(sizeof(wchar_t) * SR_Q * PC_CELLS);
  if (!cells) return;
  int nc = 0, tabs = 0, semis = 0;
  for (int i = 0; i < len; i++) tabs += s[i] == L'\t', semis += s[i] == L';';
  wchar_t sep = tabs ? L'\t' : (!chat && semis) ? L';' : 0;
  for (int i = 0; i <= len && nc < PC_CELLS;) {
    int j = i;
    while (j < len && s[j] != sep) j++;
    wchar_t raw[SR_Q];
    pc_ncpy(raw, SR_Q, s + i, j - i);
    sr_prep(raw, cells[nc], SR_Q);
    nc++;
    if (!sep) break;
    i = j + 1;
  }
  int desCell[PC_CELLS];
  PcItem *noName[PC_SPANS * 2];
  int noNameCell[PC_SPANS * 2], nn = 0;
  for (int c = 0; c < nc; c++) {
    wchar_t *p = cells[c], u[SR_Q];
    int i = 0;
    for (; p[i]; i++) u[i] = sr_upper(p[i]);
    u[i] = 0;
    int sb[PC_SPANS], se[PC_SPANS];
    int k = pc_spans(p, u, sb, se, PC_SPANS);
    if (!k) {
      wchar_t g[SR_Q];
      if (pc_unglue(p, g, SR_Q)) {
        for (i = 0; g[i]; i++) u[i] = sr_upper(g[i]);
        u[i] = 0;
        k = pc_spans(g, u, sb, se, PC_SPANS);
        if (k) wcscpy(p, g);
      }
    }
    desCell[c] = k;
    for (int m = 0; m < k; m++) {
      wchar_t des[PC_DES], name[PC_NAME];
      pc_ncpy(des, PC_DES, u + sb[m], se[m] - sb[m]);
      int to = m + 1 < k ? sb[m + 1] : (int)wcslen(p);
      pc_clean_name(p + se[m], to - se[m], chat, 0, name);
      if (!name[0] && m == 0) pc_clean_name(p, sb[0], chat, 1, name);
      PcItem *it = pc_add(l, des, name, line);
      if (it && !name[0] && nn < PC_SPANS * 2) noName[nn] = it, noNameCell[nn++] = c;
    }
  }
  /* наименование в соседней ячейке: сначала правее, потом левее */
  for (int q = 0; q < nn; q++) {
    int c0 = noNameCell[q];
    for (int dir = 1; dir >= -1 && !noName[q]->name[0]; dir -= 2)
      for (int c = c0 + dir; c >= 0 && c < nc && !desCell[c]; c += dir) {
        if (pc_letters(cells[c]) < 3) continue;
        wchar_t name[PC_NAME];
        pc_clean_name(cells[c], (int)wcslen(cells[c]), chat, 0, name);
        if (name[0]) {
          wcscpy(noName[q]->name, name);
          break;
        }
      }
  }
  free(cells);
}

/* Список из текста: строки, ячейки через табуляцию (буфер Excel) или «;»
   (CSV). В строке ищутся обозначения; наименование — текст за обозначением
   до следующего, а нет его — ближайшая ячейка со словами. chat — текст
   вопроса: из наименования убираются слова вопроса, а без единого обозначения
   весь вопрос — поиск по наименованию. Длинная строка без табуляций
   («АДЕ … Кронштейн, АДЕ … Планка, …») режется по «, » и «; ». Возвращает
   число позиций. */
static int pc_parse(PcList *l, const wchar_t *text, int chat) {
  int line = 0;
  const wchar_t *s = text;
  while (s && *s) {
    const wchar_t *nl = wcschr(s, L'\n');
    int len = nl ? (int)(nl - s) : (int)wcslen(s);
    line++;
    int tabs = 0;
    for (int i = 0; i < len; i++) tabs += s[i] == L'\t';
    for (int at = 0; at < len;) {
      int n = len - at;
      if (!tabs && n > SR_Q - 40) {
        int cut = 0;
        for (int i = at + SR_Q - 40; i > at + 20 && !cut; i--)
          if ((s[i - 1] == L',' || s[i - 1] == L';') && s[i] == L' ') cut = i;
        for (int i = at + SR_Q - 40; i > at + 20 && !cut; i--)
          if (s[i] == L' ') cut = i;
        n = (cut ? cut : at + SR_Q - 40) - at;
      }
      pc_parse_line(l, s + at, tabs ? len : n, line, chat);
      if (tabs) break;
      at += n;
    }
    s = nl ? nl + 1 : NULL;
  }
  if (chat && !l->n && text) {
    wchar_t p[SR_Q], name[PC_NAME];
    sr_prep(text, p, SR_Q);
    pc_clean_name(p, (int)wcslen(p), 1, 1, name);
    pc_add_name(l, name, 1);
  }
  return l->n;
}

/* ---- строки PLM и решение ------------------------------------------------------ */

/* наименование без обозначения впереди: «АДЕ 3424-123.01 Кронштейн» → «Кронштейн» */
static void pc_strip_des(wchar_t *s, const wchar_t *des) {
  wchar_t code[PC_CODE], core[PC_DES], d[PC_DES], c2[PC_CODE], k2[PC_DES];
  pc_split_des(des, code, core);
  pc_find_des(s, d, PC_DES);
  pc_split_des(d, c2, k2);
  if (!core[0] || !pc_same_fold(core, k2)) return;
  /* обозначение стоит в начале — отрезаем его со всеми пробелами внутри */
  int i = 0, k = 0;
  const wchar_t *want = d;
  while (s[i] && want[k]) {
    if (sr_space(s[i])) {
      i++;
      continue;
    }
    if (sr_space(want[k])) {
      k++;
      continue;
    }
    if (sr_upper(s[i]) != want[k]) return;
    i++, k++;
  }
  if (want[k]) return;
  memmove(s, s + i, (wcslen(s + i) + 1) * sizeof(wchar_t));
  pc_trim(s);
}

/* Строка PLM: имя объекта raw («АДЕ 3424-123.01 [Кронштейн]»), атрибуты
   Designation и Name (могут быть пустыми). Наименование — атрибут Name, нет
   его — текст в квадратных скобках, нет и их — остаток имени за обозначением. */
static void pc_row_set(PcRow *r, long id, int tmpl, const wchar_t *raw, const wchar_t *desAttr,
                       const wchar_t *nameAttr) {
  memset(r, 0, sizeof(*r));
  r->id = id;
  r->tmpl = tmpl;
  if (!raw) raw = L"";
  if (desAttr && desAttr[0]) pc_ncpy(r->des, PC_DES, desAttr, PC_DES), pc_trim(r->des);
  else pc_find_des(raw, r->des, PC_DES);
  wchar_t c[PC_NAME];
  for (int k = 0; k < 3 && !r->name[0]; k++) {
    c[0] = 0;
    if (k == 0 && nameAttr) pc_ncpy(c, PC_NAME, nameAttr, PC_NAME);
    if (k == 1) {
      const wchar_t *o = wcschr(raw, L'['), *z = o ? wcschr(o, L']') : NULL;
      if (o && z) pc_ncpy(c, PC_NAME, o + 1, (int)(z - o - 1));
    }
    if (k == 2) {
      int n = 0;
      while (raw[n] && !wcschr(L"[(:<", raw[n])) n++;
      pc_ncpy(c, PC_NAME, raw, n);
    }
    pc_strip_des(c, r->des);
    pc_trim(c);
    if (pc_letters(c) >= 2) wcscpy(r->name, c);
  }
}

/* Строка в позицию: сначала строки с тем же обозначением, потом похожие;
   лишние сверх PC_ROWS отбрасываются. По наименованию — все. */
static void pc_row_add(PcItem *it, long id, int tmpl, const wchar_t *raw, const wchar_t *desAttr,
                       const wchar_t *nameAttr) {
  PcRow r;
  pc_row_set(&r, id, tmpl, raw, desAttr, nameAttr);
  if (it->des[0]) {
    r.same = pc_des_cmp(it->code, it->core, r.des);
  } else {
    r.same = it->name[0] && r.name[0] ? pc_name_cmp(it->name, r.name) : 0;
    if (r.same == 0 && it->name[0]) {
      /* по наименованию: строки, где все слова вопроса нашлись в имени */
      wchar_t w[PC_WORDS][PC_WLEN], v[PC_WORDS][PC_WLEN];
      int nw = pc_words(it->name, w, PC_WORDS), nv = pc_words(r.name[0] ? r.name : raw, v, PC_WORDS), all = nw > 0;
      for (int i = 0; i < nw && all; i++) {
        int hit = 0;
        for (int j = 0; j < nv && !hit; j++) hit = pc_word_like(w[i], v[j]);
        all = hit;
      }
      r.same = all;
    }
  }
  for (int i = 0; i < it->nrows; i++)
    if (it->rows[i].id == id && id) return;
  int at = it->nrows;
  while (at > 0 && it->rows[at - 1].same < r.same) at--;
  if (at >= PC_ROWS) return;
  int last = it->nrows < PC_ROWS ? it->nrows : PC_ROWS - 1;
  memmove(&it->rows[at + 1], &it->rows[at], (size_t)(last - at) * sizeof(PcRow));
  it->rows[at] = r;
  if (it->nrows < PC_ROWS) it->nrows++;
}

/* Итог позиции по её строкам. Строк с тем же обозначением бывает несколько
   (изделие, его конфигурация, версии) — берётся та, чьё наименование ближе. */
static void pc_judge(PcItem *it) {
  if (it->state == PCS_FAIL) return;
  int best = -1, bs = -1;
  for (int i = 0; i < it->nrows; i++) {
    const PcRow *r = &it->rows[i];
    if (it->des[0] ? r->same != 2 : r->same < 1) continue;
    int s = 10 + (r->tmpl == 1767) + (r->name[0] ? 2 : 0);
    if (it->name[0] && r->name[0]) s += 4 * pc_name_cmp(it->name, r->name);
    if (s > bs) bs = s, best = i;
  }
  it->best = best;
  if (best >= 0) {
    const PcRow *r = &it->rows[best];
    int m = !it->des[0] || !it->name[0] ? 2 : r->name[0] ? pc_name_cmp(it->name, r->name) : 0;
    it->state = m == 2 ? PCS_OK : m == 1 ? PCS_NEAR : PCS_DIFF;
    return;
  }
  it->state = it->nrows && (it->des[0] ? it->rows[0].same == 1 : 1) ? PCS_SIMILAR : PCS_NONE;
  if (it->state == PCS_SIMILAR) it->best = 0;
}

/* ---- итог для чата ------------------------------------------------------------- */

typedef struct {
  wchar_t *w;
  size_t n, cap;
} PcBuf;

static void pc_put(PcBuf *b, const wchar_t *s) {
  size_t l = s ? wcslen(s) : 0;
  if (!l) return;
  if (b->n + l + 1 > b->cap) {
    size_t c = b->cap ? b->cap : 4096;
    while (c < b->n + l + 1) c *= 2;
    wchar_t *w = (wchar_t *)realloc(b->w, c * sizeof(wchar_t));
    if (!w) return;
    b->w = w;
    b->cap = c;
  }
  memcpy(b->w + b->n, s, l * sizeof(wchar_t));
  b->n += l;
  b->w[b->n] = 0;
}

static void pc_num(PcBuf *b, long v) {
  wchar_t t[24], d[24];
  int k = 0, n = 0;
  if (v < 0) t[n++] = L'-', v = -v;
  do d[k++] = (wchar_t)(L'0' + v % 10), v /= 10;
  while (v && k < 20);
  while (k) t[n++] = d[--k];
  t[n] = 0;
  pc_put(b, t);
}

static void pc_quote(PcBuf *b, const wchar_t *s) {
  pc_put(b, L"«");
  pc_put(b, s);
  pc_put(b, L"»");
}

static void pc_io(PcBuf *b, const PcRow *r) {
  if (!r->id) return;
  pc_put(b, L" (IO.");
  pc_num(b, r->id);
  pc_put(b, L")");
}

/* позиция одной строкой списка */
static void pc_line(PcBuf *b, const PcItem *it) {
  const PcRow *r = it->best >= 0 ? &it->rows[it->best] : NULL;
  pc_put(b, L"- ");
  if (it->des[0]) {
    pc_put(b, L"**");
    pc_put(b, it->des);
    pc_put(b, L"**");
    if (it->doubt) pc_put(b, L" (прочитано со скана неуверенно)");
  } else {
    pc_quote(b, it->name);
  }
  switch (it->state) {
  case PCS_OK:
    if (!it->des[0]) {
      pc_put(b, L" — в Союзе: ");
      for (int i = 0, k = 0; i < it->nrows && k < 5; i++) {
        if (it->rows[i].same < 1) continue;
        if (k++) pc_put(b, L"; ");
        pc_put(b, it->rows[i].des);
        if (it->rows[i].name[0]) pc_put(b, L" "), pc_quote(b, it->rows[i].name);
        pc_io(b, &it->rows[i]);
      }
    } else if (it->name[0]) {
      pc_put(b, L" ");
      pc_put(b, it->name);
      pc_io(b, r);
    } else {
      pc_put(b, L" — в Союзе ");
      if (r->name[0]) pc_quote(b, r->name);
      else pc_put(b, L"без наименования");
      pc_io(b, r);
    }
    break;
  case PCS_NEAR:
  case PCS_DIFF:
    pc_put(b, L" — в списке ");
    pc_quote(b, it->name);
    pc_put(b, L", в Союзе ");
    if (r->name[0]) pc_quote(b, r->name);
    else pc_put(b, L"наименования нет");
    pc_io(b, r);
    break;
  case PCS_SIMILAR:
    if (it->name[0] && it->des[0]) pc_put(b, L" "), pc_put(b, it->name);
    pc_put(b, it->des[0] ? L" — в Союзе есть похожие: " : L" — точно такого наименования нет, похожие: ");
    for (int i = 0, k = 0; i < it->nrows && k < 3; i++) {
      if (it->des[0] && it->rows[i].same < 1) continue;
      if (k++) pc_put(b, L"; ");
      pc_put(b, it->rows[i].des[0] ? it->rows[i].des : L"(без обозначения)");
      if (it->rows[i].name[0]) pc_put(b, L" "), pc_quote(b, it->rows[i].name);
      pc_io(b, &it->rows[i]);
    }
    break;
  case PCS_NONE:
    if (it->name[0] && it->des[0]) pc_put(b, L" "), pc_put(b, it->name);
    break;
  default:
    if (it->name[0] && it->des[0]) pc_put(b, L" "), pc_put(b, it->name);
    break;
  }
  pc_put(b, L"\n");
}

/* одна позиция — предложением, а не списком */
static void pc_one(PcBuf *b, const PcItem *it) {
  const PcRow *r = it->best >= 0 ? &it->rows[it->best] : NULL;
  if (!it->des[0]) {
    pc_put(b, it->state == PCS_OK ? L"В Союзе есть " : L"В Союзе нет изделия с наименованием ");
    pc_quote(b, it->name);
    pc_put(b, it->state == PCS_OK ? L":\n" : it->state == PCS_SIMILAR ? L". Похожие:\n" : L".\n");
    for (int i = 0, k = 0; i < it->nrows && k < 8 && it->state != PCS_NONE; i++) {
      if (it->state == PCS_OK && it->rows[i].same < 1) continue;
      k++;
      pc_put(b, L"- ");
      pc_put(b, it->rows[i].des[0] ? it->rows[i].des : L"(без обозначения)");
      if (it->rows[i].name[0]) pc_put(b, L" "), pc_quote(b, it->rows[i].name);
      pc_io(b, &it->rows[i]);
      pc_put(b, L"\n");
    }
    return;
  }
  pc_put(b, L"**");
  pc_put(b, it->des);
  pc_put(b, L"**");
  if (it->doubt) pc_put(b, L" (прочитано со скана неуверенно)");
  switch (it->state) {
  case PCS_OK:
    pc_put(b, L" есть в Союзе");
    if (r->name[0]) pc_put(b, L": "), pc_quote(b, r->name);
    pc_io(b, r);
    pc_put(b, it->name[0] ? L" — наименование совпадает.\n" : L".\n");
    break;
  case PCS_NEAR:
  case PCS_DIFF:
    pc_put(b, it->state == PCS_NEAR ? L" есть в Союзе, наименование записано иначе: в Союзе "
                                   : L" есть в Союзе, но наименование другое: в Союзе ");
    if (r->name[0]) pc_quote(b, r->name);
    else pc_put(b, L"его нет");
    pc_put(b, L", у вас ");
    pc_quote(b, it->name);
    pc_io(b, r);
    pc_put(b, L".\n");
    break;
  case PCS_SIMILAR:
    pc_put(b, L" — точно такого обозначения в Союзе нет. Похожие:\n");
    for (int i = 0, k = 0; i < it->nrows && k < 5; i++) {
      if (it->rows[i].same < 1) continue;
      k++;
      pc_put(b, L"- ");
      pc_put(b, it->rows[i].des);
      if (it->rows[i].name[0]) pc_put(b, L" "), pc_quote(b, it->rows[i].name);
      pc_io(b, &it->rows[i]);
      pc_put(b, L"\n");
    }
    break;
  default:
    pc_put(b, L" в Союзе нет.\n");
    break;
  }
}

static const wchar_t *const kPcHead[] = {L"", L"Есть, наименование совпадает", L"Наименование записано иначе",
                                         L"Наименование другое", L"Точного обозначения нет, есть похожие",
                                         L"Нет в Союзе", L"Не проверено"};

/* Итог: сначала то, с чем разбираться, — нет, другое наименование, похожие;
   совпавшие — в конце. src — откуда список; why — почему что-то не проверено. */
static wchar_t *pc_report(const PcList *l, const wchar_t *src, const wchar_t *why) {
  PcBuf b = {0};
  int cnt[7] = {0};
  for (int i = 0; i < l->n; i++) cnt[l->it[i].state]++;
  if (l->n == 0) {
    pc_put(&b, L"Не нашлось ни одного обозначения");
    if (src && src[0]) pc_put(&b, L" ("), pc_put(&b, src), pc_put(&b, L")");
    pc_put(&b, L". Обозначение ищется по шаблону штампа: «АДЕ 3424-123.01», «3424-123.01», «АБВГ.123456.001».");
    return b.w;
  }
  if (cnt[PCS_FAIL] == l->n) {
    pc_put(&b, L"Сверить с PLM «Союз» не вышло");
    if (why && why[0]) pc_put(&b, L": "), pc_put(&b, why);
    pc_put(&b, l->n > 1 ? L"\nПозиций в списке: " : L"\nПозиция: ");
    if (l->n > 1) pc_num(&b, l->n), pc_put(&b, L", первая — ");
    pc_put(&b, l->it[0].des[0] ? l->it[0].des : l->it[0].name);
    pc_put(&b, L". Ничего не изменено.");
    return b.w;
  }
  if (l->n == 1 && l->it[0].state != PCS_FAIL) {
    pc_one(&b, &l->it[0]);
    int miss = l->it[0].state == PCS_SIMILAR || l->it[0].state == PCS_NONE;
    pc_put(&b, miss ? L"\nИскал в PLM «" : L"\n");
    if (miss && l->it[0].des[0])
      pc_put(&b, l->it[0].core), pc_put(&b, L"» в имени изделия и в атрибуте «Обозначение». ");
    else if (miss)
      pc_put(&b, l->it[0].name), pc_put(&b, L"» по словам наименования. ");
    pc_put(&b, L"Союз только читался — в нём ничего не изменено.");
    return b.w;
  }
  pc_put(&b, L"## Сверка с PLM «Союз»");
  if (l->n > 1) pc_put(&b, L": "), pc_num(&b, l->n), pc_put(&b, L" поз.");
  pc_put(&b, L"\n");
  static const int order[] = {PCS_NONE, PCS_DIFF, PCS_SIMILAR, PCS_NEAR, PCS_FAIL, PCS_OK};
  static const wchar_t *const brief[] = {L"", L"совпадает", L"записано иначе", L"другое наименование",
                                         L"похожие", L"нет в Союзе", L"не проверено"};
  if (l->n > 1) {
    int first = 1;
    pc_put(&b, L"Итого: ");
    for (int k = 5; k >= 0; k--) {
      int s = order[k];
      if (!cnt[s]) continue;
      if (!first) pc_put(&b, L" · ");
      first = 0;
      pc_put(&b, brief[s]);
      pc_put(&b, L" — ");
      pc_num(&b, cnt[s]);
    }
    pc_put(&b, L".\n");
  }
  for (int k = 0; k < 6; k++) {
    int s = order[k];
    if (!cnt[s]) continue;
    pc_put(&b, L"\n**");
    pc_put(&b, kPcHead[s]);
    if (s == PCS_FAIL && why && why[0]) pc_put(&b, L" — "), pc_put(&b, why);
    pc_put(&b, L"** (");
    pc_num(&b, cnt[s]);
    pc_put(&b, L"):\n");
    int shown = 0, limit = s == PCS_OK ? 40 : 80;
    for (int i = 0; i < l->n; i++) {
      if (l->it[i].state != s) continue;
      if (shown++ >= limit) continue;
      pc_line(&b, &l->it[i]);
    }
    if (shown > limit) pc_put(&b, L"- … и ещё "), pc_num(&b, shown - limit), pc_put(&b, L"\n");
  }
  pc_put(&b, L"\n");
  if (src && src[0]) pc_put(&b, L"Источник: "), pc_put(&b, src), pc_put(&b, L". ");
  if (l->dup) pc_put(&b, L"Повторы не считались: "), pc_num(&b, l->dup), pc_put(&b, L". ");
  if (l->cut) pc_put(&b, L"Сверх "), pc_num(&b, PC_MAX), pc_put(&b, L" позиций не сверялось: "), pc_num(&b, l->cut),
      pc_put(&b, L". ");
  pc_put(&b, L"Союз только читался — в нём ничего не изменено.");
  return b.w;
}
