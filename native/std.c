/* std.c — справочник стандартных изделий и материалов (с 2026.10.24): размеры
   крепежа, сортамент, марки сталей и их аналоги по ГОСТ, DIN, ISO. Таблицы
   набраны из стандартов и лежат только у пользователя (по умолчанию
   %USERPROFILE%\CursorPad-std, файлы *.tsv в ней и в подпапках). В репозитории
   их нет: он публичный, а тексты DIN и ISO платные. Числа ИИ не придумывает —
   их находит этот код и подписывает, откуда они: стандарт, таблица, страница и
   сверил ли их человек. Разбор и поиск — без Windows (std_test.c под Linux):
     gcc -O2 -Wall -Werror -o std_test std_test.c && ./std_test
   Чтение папки с диска — std_files.c; страница сверки для человека — std_review.py.

   Файл — текст UTF-8 (или «Текст Юникод» и «Текст с разделителями табуляции»
   из Excel), столбцы через TAB:
     #стандарт	ГОСТ 7798-70
     #название	Болты с шестигранной головкой класса точности В
     #таблица	1 — Размеры болтов, мм
     #источник	адрес страницы или PDF, лист
     #вид	болт; винт с шестигранной головкой
     #ключ	d                    — по какому столбцу ищут («все» — по любому); без строки — первый
     #масса	Масса 1 м, кг        — столбец погонной массы: масса заготовки по длине
     #проверено	да, 2026-10-25     — сверено человеком с источником; иначе — с оговоркой
     #сверка	адрес второго источника — совпало 15 из 15 строк   (сверка кодом по второму источнику)
     #примечание	…
     d	P	S	e	k
     M12	1,75	18	19,9	7,5
   Первая строка без # — шапка, дальше строки таблицы; ключ заполняется в каждой.
   Строка # после строк таблицы начинает следующую таблицу того же стандарта:
   #стандарт, #название, #вид и #источник переходят к ней, остальное — нет
   (каждую таблицу сверяют отдельно). «##…» и «# …» — комментарии. */
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GS_COLS 40
#define GS_KEYS 8
#define GS_REFS 8
#define GS_TOKS 24
#define GS_WORDS 32

/* растущая строка (без store_text.h: тот тянет функции, ненужные тесту) */
typedef struct {
  wchar_t *s;
  size_t n, cap;
} GsBuf;

static void gs_cat(GsBuf *b, const wchar_t *s) {
  size_t k = wcslen(s);
  if (b->n + k + 1 > b->cap) {
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->n + k + 1) cap *= 2;
    wchar_t *t = (wchar_t *)realloc(b->s, cap * sizeof(wchar_t));
    if (!t) return;
    b->s = t;
    b->cap = cap;
  }
  memcpy(b->s + b->n, s, (k + 1) * sizeof(wchar_t));
  b->n += k;
}

static void gs_ch(GsBuf *b, wchar_t c) {
  wchar_t t[2] = {c, 0};
  gs_cat(b, t);
}

typedef struct {
  const wchar_t *file; /* откуда прочитана — для предупреждений */
  int line;            /* строка шапки */
  wchar_t std[80];     /* «ГОСТ 7798-70» */
  wchar_t title[240];  /* название стандарта */
  wchar_t table[240];  /* «1 — Размеры болтов, мм» */
  wchar_t src[400];    /* где набрано */
  wchar_t kind[240];   /* виды через «;»: ищутся по первому слову */
  wchar_t checked[160];
  wchar_t cross[400];  /* «#сверка»: чем и как сверено со вторым источником */
  wchar_t note[800];
  wchar_t keyName[160], massName[80];
  int keyCol[GS_KEYS], nKey, keyAll;
  int massCol;         /* -1 — нет */
  int ncol, nrow, capRow;
  wchar_t **cell;      /* (nrow + 1) × ncol, строка 0 — шапка */
} GsTable;

typedef struct {
  GsTable *t;
  int n, cap;
  wchar_t **own; /* тексты файлов и пути: ячейки — указатели в них */
  int nOwn, capOwn;
  int files;
  GsBuf warn;    /* «файл, строка N: что не так» */
} GsDb;

/* ---- строки ---------------------------------------------------------------------- */

static void gs_cpy(wchar_t *d, int cap, const wchar_t *s) {
  int n = 0;
  while (s && s[n] && n < cap - 1) d[n] = s[n], n++;
  d[n] = 0;
}

static void gs_int(GsBuf *b, long v) {
  wchar_t t[24];
  int n = 23;
  t[n] = 0;
  int neg = v < 0;
  if (neg) v = -v;
  do t[--n] = (wchar_t)(L'0' + v % 10), v /= 10; while (v && n > 1);
  if (neg) t[--n] = L'-';
  gs_cat(b, t + n);
}

/* число с запятой, до 3 знаков после неё: 1,849 · 15,4 · 50 */
static void gs_num(GsBuf *b, double v) {
  long m = (long)(v * 1000 + (v < 0 ? -0.5 : 0.5));
  if (m < 0) gs_ch(b, L'-'), m = -m;
  gs_int(b, m / 1000);
  long f = m % 1000;
  if (!f) return;
  gs_ch(b, L',');
  for (long d = 100; f && d; d /= 10) gs_ch(b, (wchar_t)(L'0' + f / d)), f %= d;
}

static int gs_isdig(wchar_t c) { return c >= L'0' && c <= L'9'; }
static int gs_space(wchar_t c) { return c == L' ' || c == L'\t' || c == 0xA0; }

/* сравнение без регистра, ё и латиницы, похожей на кириллицу: «M12» = «М12»,
   «40X» = «40Х»; «×» и «*» — «х», запятая — точка (1,75 = 1.75) */
static wchar_t gs_fold1(wchar_t c) {
  if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
  else if (c >= L'А' && c <= L'Я') return (wchar_t)(c + 32);
  switch (c) {
  case L'a': return L'а';
  case L'b': return L'в';
  case L'c': return L'с';
  case L'e': return L'е';
  case L'h': return L'н';
  case L'k': return L'к';
  case L'm': return L'м';
  case L'o': return L'о';
  case L'p': return L'р';
  case L't': return L'т';
  case L'x': return L'х';
  case L'y': return L'у';
  case L'Ё': case L'ё': return L'е';
  case L'×': case L'*': return L'х';
  case L',': return L'.';
  }
  return c;
}

static void gs_fold(const wchar_t *s, wchar_t *out, int cap) {
  int n = 0;
  while (s && *s && n < cap - 1) out[n++] = gs_fold1(*s++);
  out[n] = 0;
}

/* число целиком: «1,75», «50», «-0,5»; иначе 0 */
static int gs_number(const wchar_t *s, double *v) {
  while (gs_space(*s)) s++;
  int neg = *s == L'-' || *s == L'−';
  if (neg) s++;
  if (!gs_isdig(*s)) return 0;
  double x = 0;
  while (gs_isdig(*s)) x = x * 10 + (*s++ - L'0');
  if ((*s == L',' || *s == L'.') && gs_isdig(s[1])) {
    double k = 0.1;
    for (s++; gs_isdig(*s); s++) x += (*s - L'0') * k, k /= 10;
  }
  while (gs_space(*s)) s++;
  if (*s) return 0;
  *v = neg ? -x : x;
  return 1;
}

/* одно значение равно другому: числа — по величине, прочее — сложенным текстом */
static int gs_same(const wchar_t *a, int an, const wchar_t *b) {
  wchar_t x[64], y[64];
  if (an <= 0 || an > 63) return 0;
  for (int i = 0; i < an; i++) x[i] = a[i];
  x[an] = 0;
  double u, v;
  if (gs_number(x, &u) && gs_number(b, &v)) return u - v < 1e-9 && v - u < 1e-9;
  gs_fold(x, x, 64);
  gs_fold(b, y, 64);
  return !wcscmp(x, y);
}

/* «Ст3» — это и «Ст3сп», «Ст3пс», «Ст3кп»: степень раскисления не меняет марку */
static int gs_deox(const wchar_t *cell, int n, const wchar_t *key) {
  static const wchar_t *const kSuf[] = {L"сп", L"пс", L"кп"};
  int k = (int)wcslen(key);
  if (n != k + 2 || !k || !gs_isdig(key[k - 1])) return 0;
  for (size_t i = 0; i < sizeof(kSuf) / sizeof(kSuf[0]); i++) {
    wchar_t w[48];
    if (k > 40) return 0;
    wcscpy(w, key);
    wcscat(w, kSuf[i]);
    if (gs_same(cell, n, w)) return 1;
  }
  return 0;
}

/* граница частей ячейки: ; / ( ) пробел и запятая — кроме десятичной («1,75») */
static int gs_part_sep(const wchar_t *cell, const wchar_t *p) {
  if (*p == L',') return !(p > cell && gs_isdig(p[-1]) && gs_isdig(p[1]));
  return wcschr(L";/()", *p) || gs_space(*p);
}

/* ячейка совпадает с ключом: целиком или одной из частей «M12; M12x1,25»,
   «41Cr4 (1.7035)», «50, 50А» */
static int gs_cell_has(const wchar_t *cell, const wchar_t *key) {
  const wchar_t *p = cell;
  int whole = (int)wcslen(cell);
  if (gs_same(cell, whole, key) || gs_deox(cell, whole, key)) return 1;
  while (*p) {
    while (*p && gs_part_sep(cell, p)) p++;
    const wchar_t *b = p;
    while (*p && !gs_part_sep(cell, p)) p++;
    if (p > b && (int)(p - b) < whole && (gs_same(b, (int)(p - b), key) || gs_deox(b, (int)(p - b), key))) return 1;
  }
  return 0;
}

/* Запасные формы ключа, если целиком не нашёлся: «50-В» → «50» (класс
   точности круга), «М12-6g» → «М12», «М12х60» → «М12» (длина болта). Марку
   «12Х18Н10Т» не режем: после «х» у неё буквы, а у размера — только цифры. */
static int gs_tok_alt(const wchar_t *tok, wchar_t alt[2][40]) {
  int n = 0;
  const wchar_t *d = wcschr(tok, L'-');
  if (d && d > tok && d - tok < 40) {
    for (int i = 0; i < d - tok; i++) alt[n][i] = tok[i];
    alt[n][d - tok] = 0;
    n++;
  }
  wchar_t f[40];
  gs_fold(tok, f, 40);
  const wchar_t *x = wcschr(f, L'х');
  if (x && x > f && f[0] == L'м' && gs_isdig(f[1]) && gs_isdig(x[1]) && x - f < 40) {
    for (int i = 0; i < x - f; i++) alt[n][i] = tok[i];
    alt[n][x - f] = 0;
    n++;
  }
  return n;
}

/* «Масса 1 м, кг» под «масса»: начало шапки до запятой, пробела или скобки */
static int gs_head_is(const wchar_t *head, const wchar_t *name) {
  wchar_t h[160], n[160];
  gs_fold(head, h, 160);
  gs_fold(name, n, 160);
  size_t k = wcslen(n);
  if (!k || wcsncmp(h, n, k)) return 0;
  return !h[k] || h[k] == L'.' || h[k] == L' ' || h[k] == L'(';
}

/* ---- обозначения стандартов: «ГОСТ 7798-70», «DIN EN ISO 4017», «ГОСТ Р 52627» --------- */

typedef struct {
  wchar_t org[24];  /* последнее слово: «гост», «гост р», «din», «iso» */
  wchar_t num[24];  /* «7798», «30893.2» */
} GsRef;

/* слово организации; 0 — не оно */
static int gs_org(const wchar_t *w, int n, wchar_t *org) {
  static const wchar_t *const kOrg[][2] = {{L"гост", L"гост"}, {L"gost", L"гост"}, {L"ост", L"ост"},
                                           {L"din", L"din"},   {L"iso", L"iso"},   {L"исо", L"iso"},
                                           {L"en", L"en"},     {L"iec", L"iec"},   {L"мэк", L"iec"}};
  wchar_t f[16];
  if (n <= 0 || n > 15) return 0;
  for (int i = 0; i < n; i++) f[i] = w[i];
  f[n] = 0;
  gs_fold(f, f, 16);
  for (size_t i = 0; i < sizeof(kOrg) / sizeof(kOrg[0]); i++) {
    wchar_t g[16];
    gs_fold(kOrg[i][0], g, 16);
    if (!wcscmp(f, g)) {
      gs_cpy(org, 24, kOrg[i][1]);
      return 1;
    }
  }
  return 0;
}

/* Обозначение с позиции p (на начале слова). Возвращает, сколько символов
   занято, 0 — здесь не обозначение. */
static int gs_ref_at(const wchar_t *p, GsRef *r) {
  const wchar_t *q = p;
  int any = 0;
  r->org[0] = r->num[0] = 0;
  for (;;) {
    const wchar_t *w = q;
    while (*q && !gs_space(*q) && !gs_isdig(*q) && !wcschr(L",;:()«»\"/", *q)) q++;
    wchar_t org[24];
    if (q > w && gs_org(w, (int)(q - w), org)) {
      any = 1;
      gs_cpy(r->org, 24, org);
    } else if (any && q - w == 1 && !wcscmp(r->org, L"гост") && (*w == L'Р' || *w == L'р' || *w == L'R')) {
      gs_cpy(r->org, 24, L"гост р");
    } else {
      q = w;
      break;
    }
    while (gs_space(*q)) q++;
    if (gs_isdig(*q)) break;
  }
  if (!any || !gs_isdig(*q)) return 0;
  int n = 0;
  while (gs_isdig(*q) && n < 20) r->num[n++] = *q++;
  if (*q == L'.' && gs_isdig(q[1])) { /* часть: 30893.2 */
    r->num[n++] = *q++;
    while (gs_isdig(*q) && n < 23) r->num[n++] = *q++;
  }
  r->num[n] = 0;
  while (*q == L'-' || *q == L'–' || gs_isdig(*q)) q++; /* год не сравнивается */
  return (int)(q - p);
}

static int gs_ref_eq(const GsRef *a, const GsRef *b) {
  if (wcscmp(a->org, b->org)) return 0;
  if (!wcscmp(a->num, b->num)) return 1;
  /* «ГОСТ 30893» — любая часть «30893.1», «30893.2» */
  size_t na = wcslen(a->num), nb = wcslen(b->num);
  if (na < nb && !wcschr(a->num, L'.')) return !wcsncmp(a->num, b->num, na) && b->num[na] == L'.';
  if (nb < na && !wcschr(b->num, L'.')) return !wcsncmp(a->num, b->num, nb) && a->num[nb] == L'.';
  return 0;
}

/* есть ли в тексте обозначение r */
static int gs_text_has_ref(const wchar_t *s, const GsRef *r) {
  for (const wchar_t *p = s; *p; p++) {
    if (p > s && !gs_space(p[-1]) && !wcschr(L",;:()«»\"/", p[-1])) continue;
    GsRef t;
    int k = gs_ref_at(p, &t);
    if (k && gs_ref_eq(&t, r)) return 1;
    if (k) p += k - 1;
  }
  return 0;
}

/* ---- разбор файла ------------------------------------------------------------------ */

static void gs_warn(GsDb *db, const wchar_t *file, int line, const wchar_t *what) {
  if (db->warn.n > 6000) return;
  gs_cat(&db->warn, file ? file : L"?");
  gs_cat(&db->warn, L", строка ");
  gs_int(&db->warn, line);
  gs_cat(&db->warn, L": ");
  gs_cat(&db->warn, what);
  gs_ch(&db->warn, L'\n');
}

static int gs_keep(GsDb *db, wchar_t *p) {
  if (!p) return 0;
  if (db->nOwn == db->capOwn) {
    int cap = db->capOwn ? db->capOwn * 2 : 16;
    wchar_t **t = (wchar_t **)realloc(db->own, (size_t)cap * sizeof(wchar_t *));
    if (!t) return 0;
    db->own = t;
    db->capOwn = cap;
  }
  db->own[db->nOwn++] = p;
  return 1;
}

static void gs_free(GsDb *db) {
  for (int i = 0; i < db->n; i++) free(db->t[i].cell);
  for (int i = 0; i < db->nOwn; i++) free(db->own[i]);
  free(db->t);
  free(db->own);
  free(db->warn.s);
  memset(db, 0, sizeof(*db));
}

/* ячейка без пробелов по краям и без кавычек Excel: "a ""b""" → a "b" */
static wchar_t *gs_trim(wchar_t *s) {
  while (gs_space(*s)) s++;
  size_t n = wcslen(s);
  while (n && gs_space(s[n - 1])) s[--n] = 0;
  if (n >= 2 && s[0] == L'"' && s[n - 1] == L'"') {
    s[n - 1] = 0;
    s++;
    wchar_t *w = s;
    for (wchar_t *r = s; *r; r++) {
      if (r[0] == L'"' && r[1] == L'"') r++;
      *w++ = *r;
    }
    *w = 0;
  }
  return s;
}

static int gs_row_add(GsTable *t, wchar_t **cells, int n) {
  if (t->nrow + 1 >= t->capRow) {
    int cap = t->capRow ? t->capRow * 2 : 32;
    wchar_t **c = (wchar_t **)realloc(t->cell, (size_t)cap * (size_t)t->ncol * sizeof(wchar_t *));
    if (!c) return 0;
    t->cell = c;
    t->capRow = cap;
  }
  wchar_t **row = t->cell + (size_t)(t->nrow + 1) * (size_t)t->ncol;
  for (int i = 0; i < t->ncol; i++) row[i] = i < n ? cells[i] : (wchar_t *)L"";
  t->nrow++;
  return 1;
}

static int gs_col_find(const GsTable *t, const wchar_t *name) {
  for (int c = 0; c < t->ncol; c++)
    if (gs_head_is(t->cell[c], name)) return c;
  return -1;
}

/* таблица дочитана: проверить и взять в справочник */
static void gs_finish(GsDb *db, GsTable *t) {
  if (!t->ncol) return;
  const wchar_t *why = !t->std[0] ? L"нет строки #стандарт — таблица пропущена"
                       : !t->nrow ? L"в таблице только шапка — пропущена"
                                  : NULL;
  if (why) {
    gs_warn(db, t->file, t->line, why);
    free(t->cell);
    t->cell = NULL;
    return;
  }
  wchar_t f[160];
  gs_fold(t->keyName, f, 160);
  t->keyAll = !wcscmp(f, L"все") || !wcscmp(f, L"любой");
  t->nKey = 0;
  if (!t->keyAll && t->keyName[0]) {
    wchar_t one[80];
    for (const wchar_t *p = t->keyName; *p && t->nKey < GS_KEYS;) {
      int n = 0;
      while (*p == L';' || gs_space(*p)) p++;
      while (*p && *p != L';' && n < 79) one[n++] = *p++;
      while (n && gs_space(one[n - 1])) n--;
      one[n] = 0;
      if (!n) continue;
      int c = gs_col_find(t, one);
      if (c < 0) {
        wchar_t w[200] = L"нет столбца ключа «";
        wcsncat(w, one, 120);
        wcscat(w, L"» — ищу по первому");
        gs_warn(db, t->file, t->line, w);
      } else {
        t->keyCol[t->nKey++] = c;
      }
    }
  }
  if (!t->keyAll && !t->nKey) t->keyCol[t->nKey++] = 0;
  t->massCol = -1;
  if (t->massName[0] && (t->massCol = gs_col_find(t, t->massName)) < 0)
    gs_warn(db, t->file, t->line, L"нет столбца из строки #масса");
  if (db->n == db->cap) {
    int cap = db->cap ? db->cap * 2 : 16;
    GsTable *n = (GsTable *)realloc(db->t, (size_t)cap * sizeof(GsTable));
    if (!n) {
      free(t->cell);
      t->cell = NULL;
      return;
    }
    db->t = n;
    db->cap = cap;
  }
  db->t[db->n++] = *t;
  t->cell = NULL;
}

/* Текст одного файла (справочник забирает его и освобождает в gs_free). */
static void gs_parse(GsDb *db, wchar_t *text, const wchar_t *file) {
  if (!text) return;
  if (!gs_keep(db, text)) {
    free(text);
    return;
  }
  wchar_t *name = (wchar_t *)malloc((wcslen(file ? file : L"") + 1) * sizeof(wchar_t));
  if (name && gs_keep(db, name)) wcscpy(name, file ? file : L"");
  else name = (wchar_t *)L"";
  db->files++;
  GsTable cur;
  memset(&cur, 0, sizeof(cur));
  cur.file = name;
  wchar_t *p = text;
  if (*p == 0xFEFF) p++;
  for (int line = 1; p && *p; line++) {
    wchar_t *e = wcschr(p, L'\n');
    if (e) *e = 0;
    size_t len = wcslen(p);
    if (len && p[len - 1] == L'\r') p[--len] = 0;
    wchar_t *s = p;
    p = e ? e + 1 : NULL;
    while (gs_space(*s)) s++;
    if (!*s || (s[0] == L'#' && (s[1] == L'#' || gs_space(s[1]) || !s[1]))) continue; /* «## …», «# …» */
    if (s[0] == L'#') {
      if (cur.ncol) { /* после строк таблицы — следующая таблица того же стандарта */
        GsTable next;
        memset(&next, 0, sizeof(next));
        next.file = name;
        memcpy(next.std, cur.std, sizeof(cur.std));
        memcpy(next.title, cur.title, sizeof(cur.title));
        memcpy(next.kind, cur.kind, sizeof(cur.kind));
        memcpy(next.src, cur.src, sizeof(cur.src));
        gs_finish(db, &cur);
        cur = next;
      }
      wchar_t *k = ++s;
      while (*s && !gs_space(*s) && *s != L':') s++;
      wchar_t key[32];
      int kn = (int)(s - k) < 31 ? (int)(s - k) : 31;
      for (int i = 0; i < kn; i++) key[i] = k[i];
      key[kn] = 0;
      gs_fold(key, key, 32);
      while (gs_space(*s) || *s == L':') s++;
      for (wchar_t *t = s; *t; t++) /* Excel добивает строку пустыми столбцами */
        if (*t == L'\t') *t = L' ';
      s = gs_trim(s);
      struct { const wchar_t *k; wchar_t *v; int cap; } kf[] = {
          {L"стандарт", cur.std, 80},       {L"название", cur.title, 240},  {L"таблица", cur.table, 240},
          {L"источник", cur.src, 400},      {L"вид", cur.kind, 240},        {L"ключ", cur.keyName, 160},
          {L"масса", cur.massName, 80},     {L"проверено", cur.checked, 160}, {L"примечание", cur.note, 800},
          {L"сверка", cur.cross, 400}};
      int known = 0;
      for (size_t i = 0; i < sizeof(kf) / sizeof(kf[0]); i++) {
        wchar_t fk[32];
        gs_fold(kf[i].k, fk, 32);
        if (wcscmp(key, fk)) continue;
        known = 1;
        if (kf[i].v == cur.note && cur.note[0]) { /* несколько примечаний — подряд */
          wcsncat(cur.note, L" ", 799 - wcslen(cur.note));
          wcsncat(cur.note, s, 799 - wcslen(cur.note));
        } else {
          gs_cpy(kf[i].v, kf[i].cap, s);
        }
      }
      if (!known) gs_warn(db, name, line, L"неизвестная строка # — пропущена");
      continue;
    }
    wchar_t *cells[GS_COLS];
    int n = 0, extra = 0;
    for (wchar_t *c = s;;) {
      wchar_t *tab = wcschr(c, L'\t');
      if (tab) *tab = 0;
      if (n < GS_COLS) cells[n++] = gs_trim(c);
      else if (*gs_trim(c)) extra = 1;
      if (!tab) break;
      c = tab + 1;
    }
    while (n > 1 && !cells[n - 1][0]) n--; /* пустые столбцы справа (Excel) */
    if (!cur.ncol) {
      cur.ncol = n;
      cur.line = line;
      cur.nrow = -1; /* шапка — строка 0 */
      if (!gs_row_add(&cur, cells, n)) break;
      continue;
    }
    if (n > cur.ncol || extra) gs_warn(db, name, line, L"ячеек больше, чем в шапке — лишние не взяты");
    if (!cells[0][0] && cur.nKey == 0 && !cur.keyName[0]) gs_warn(db, name, line, L"пустой ключ (первый столбец)");
    if (!gs_row_add(&cur, cells, n)) break;
  }
  gs_finish(db, &cur);
}

/* ---- запрос ------------------------------------------------------------------------- */

typedef struct {
  GsRef ref[GS_REFS];
  int nref, refSeg[GS_REFS];
  wchar_t tok[GS_TOKS][40];     /* ключи: слова с цифрой, как написаны */
  int ntok, tokSeg[GS_TOKS], tokWord[GS_TOKS]; /* tokWord — индекс слова перед ключом, -1 */
  wchar_t word[GS_WORDS][32];   /* прочие слова, сложенные */
  int nword, wordSeg[GS_WORDS];
  double len;                   /* длина заготовки, мм; 0 — не названа */
} GsQuery;

static int gs_sep(const wchar_t *p, const wchar_t *start) {
  wchar_t c = *p;
  if (gs_space(c) || c == L'\n' || c == L'\r') return 1;
  if (wcschr(L";?!()[]«»\"':=/\\|", c)) return 1;
  if (c == L',' || c == L'.') /* 1,75 и 30893.2 — одно слово */
    return !(p > start && gs_isdig(p[-1]) && gs_isdig(p[1]));
  if (c == L'—' || c == L'–') return 1;
  return 0;
}

static void gs_query(const wchar_t *q, GsQuery *Q) {
  memset(Q, 0, sizeof(*Q));
  int seg = 0, lastWord = -1, lenNext = 0;
  for (const wchar_t *p = q; *p;) {
    if (gs_sep(p, q)) {
      if (wcschr(L"/;|\n", *p)) seg++, lastWord = -1;
      p++;
      continue;
    }
    if (*p == L'Ø' || *p == L'ø' || *p == L'⌀') { p++; continue; }
    GsRef r;
    int k = gs_ref_at(p, &r);
    if (k) {
      if (Q->nref < GS_REFS) Q->refSeg[Q->nref] = seg, Q->ref[Q->nref++] = r;
      p += k;
      continue;
    }
    const wchar_t *b = p;
    while (*p && !gs_sep(p, q)) p++;
    int n = (int)(p - b), dig = 0;
    for (int i = 0; i < n; i++) dig |= gs_isdig(b[i]);
    wchar_t w[40];
    int wn = n < 39 ? n : 39;
    for (int i = 0; i < wn; i++) w[i] = b[i];
    w[wn] = 0;
    if (dig) {
      double v;
      if (lenNext && gs_number(w, &v)) { /* «L=120», «длиной 120 мм» */
        Q->len = v;
        lenNext = 0;
        continue;
      }
      lenNext = 0;
      if (Q->ntok < GS_TOKS) {
        gs_cpy(Q->tok[Q->ntok], 40, w);
        Q->tokSeg[Q->ntok] = seg;
        Q->tokWord[Q->ntok++] = lastWord;
      }
      continue;
    }
    wchar_t f[40];
    gs_fold(w, f, 40);
    lenNext = !wcscmp(f, L"l") || !wcsncmp(f, L"длин", 4);
    if (lenNext || !wcscmp(f, L"мм")) continue;
    if (Q->nword < GS_WORDS) {
      gs_cpy(Q->word[Q->nword], 32, f);
      Q->wordSeg[Q->nword] = seg;
      lastWord = Q->nword++;
    }
  }
}

/* слово запроса — вид таблицы: общее начало хотя бы на букву короче слова
   («гайки» — «гайка», «стали» — «сталь», «болта» — «болт») */
static int gs_kind_word(const wchar_t *kind, const wchar_t *fw) {
  int lw = (int)wcslen(fw);
  if (lw < 3) return 0;
  for (const wchar_t *p = kind; p && *p;) {
    while (*p == L';' || *p == L',' || gs_space(*p)) p++;
    wchar_t stem[32];
    int n = 0;
    while (*p && *p != L';' && *p != L',' && !gs_space(*p) && n < 31) stem[n++] = *p++;
    stem[n] = 0;
    while (*p && *p != L';' && *p != L',') p++;
    if (n < 3) continue;
    gs_fold(stem, stem, 32);
    int c = 0;
    while (stem[c] && fw[c] && stem[c] == fw[c]) c++;
    int m = n < lw ? n : lw;
    if (c >= 3 && c >= m - 1) return 1;
  }
  return 0;
}

static int gs_table_ref(const GsTable *t, const GsRef *r) {
  GsRef tr;
  return gs_ref_at(t->std, &tr) && gs_ref_eq(&tr, r);
}

/* ---- поиск --------------------------------------------------------------------------- */

typedef struct {
  const GsTable *t;
  int row;   /* 1…nrow; 0 — сводка таблицы (стандарт назван, размер — нет) */
  int close;  /* не точное совпадение, а соседняя строка по числовому ключу */
  int tok;   /* какой ключ запроса, -1 — по обозначению в ячейке */
  int score;
} GsHit;

static const wchar_t *gs_cell(const GsTable *t, int row, int col) {
  if (!t || row < 0 || row > t->nrow || col < 0 || col >= t->ncol) return L"";
  return t->cell[(size_t)row * (size_t)t->ncol + (size_t)col];
}

static int gs_key_match1(const GsTable *t, int row, const wchar_t *tok) {
  wchar_t **r = t->cell + (size_t)row * (size_t)t->ncol;
  if (t->keyAll) {
    for (int c = 0; c < t->ncol; c++)
      if (gs_cell_has(r[c], tok)) return 1;
    return 0;
  }
  for (int i = 0; i < t->nKey; i++)
    if (gs_cell_has(r[t->keyCol[i]], tok)) return 1;
  return 0;
}

/* 1 — ключ как есть, 2 — по запасной форме («50-В» → 50) */
static int gs_key_match(const GsTable *t, int row, const wchar_t *tok) {
  if (gs_key_match1(t, row, tok)) return 1;
  wchar_t alt[2][40];
  int n = gs_tok_alt(tok, alt);
  for (int i = 0; i < n; i++)
    if (gs_key_match1(t, row, alt[i])) return 2;
  return 0;
}

/* Изделия и материалы, которые бывают в справочнике: слово запроса из этого
   списка — вид, даже если таблиц такого вида ещё нет («гайка М12» без гаек —
   «нет», а не болт М12). */
static const wchar_t *const kGsKinds =
    L"болт; винт; гайка; шайба; шпилька; шплинт; шпонка; штифт; заклёпка; кольцо; подшипник; манжета; "
    L"круг; пруток; квадрат; шестигранник; лист; полоса; лента; труба; уголок; швеллер; двутавр; проволока; "
    L"сталь; чугун; бронза; латунь; медь; алюминий; сплав; марка; аналог; замена";

static int gs_is_kind(const GsDb *db, const wchar_t *fw) {
  if (gs_kind_word(kGsKinds, fw)) return 1;
  for (int i = 0; i < db->n; i++)
    if (gs_kind_word(db->t[i].kind, fw)) return 1;
  return 0;
}

/* можно ли этот ключ искать в этой таблице: «круг 50 / сталь 45» — 50 для
   круга, 45 для стали; ключ в части с обозначением — для того стандарта */
static int gs_tok_for(const GsQuery *Q, int k, const GsTable *t, const int *refTab) {
  int refs = 0, mine = 0;
  for (int i = 0; i < Q->nref; i++) {
    if (Q->refSeg[i] != Q->tokSeg[k]) continue;
    refs = 1;
    if (refTab[i]) mine = 1;
  }
  if (refs) return mine;
  int w = Q->tokWord[k];
  if (w >= 0 && Q->wordSeg[w] == Q->tokSeg[k]) {
    /* слово перед ключом — чей-то вид: тогда ключ только для таблиц этого вида */
    if (gs_kind_word(t->kind, Q->word[w])) return 1;
    return -1; /* решит вызывающий: вид ли это вообще */
  }
  return 1;
}

static int gs_score(const GsQuery *Q, const GsTable *t) {
  wchar_t f[1200];
  GsBuf b = {0};
  gs_cat(&b, t->title);
  gs_ch(&b, L' ');
  gs_cat(&b, t->table);
  gs_ch(&b, L' ');
  for (int c = 0; c < t->ncol; c++) gs_cat(&b, t->cell[c]), gs_ch(&b, L' ');
  gs_fold(b.s ? b.s : L"", f, 1200);
  free(b.s);
  int s = 0;
  for (int i = 0; i < Q->nword; i++) {
    wchar_t stem[8];
    if (wcslen(Q->word[i]) < 4) continue;
    gs_cpy(stem, 6, Q->word[i]); /* «химический» и «хим.», «механические» и «механ» */
    if (wcsstr(f, stem)) s++;
  }
  return s;
}

static int gs_hit_cmp(const void *a, const void *b) {
  const GsHit *x = (const GsHit *)a, *y = (const GsHit *)b;
  if (x->score != y->score) return y->score - x->score;
  if (x->t != y->t) return x->t < y->t ? -1 : 1;
  return x->row - y->row;
}

static int gs_add(GsHit *hit, int n, int max, const GsTable *t, int row, int close, int tok, int score) {
  for (int i = 0; i < n; i++)
    if (hit[i].t == t && hit[i].row == row) return n;
  if (n >= max) return n;
  hit[n].t = t;
  hit[n].row = row;
  hit[n].close = close;
  hit[n].tok = tok;
  hit[n].score = score;
  return n + 1;
}

/* Найти строки по запросу: «болт М12 ГОСТ 7798», «гайка M16 DIN 934», «40Х»,
   «круг 52 L=120», «аналог DIN 933». Возвращает число находок в hit. */
static int gs_find(const GsDb *db, const wchar_t *q, GsHit *hit, int max, GsQuery *Q) {
  gs_query(q, Q);
  int n = 0, anyKind = 0, anyDirect = 0;
  for (int w = 0; w < Q->nword && !anyKind; w++) anyKind = gs_is_kind(db, Q->word[w]);
  for (int i = 0; i < db->n && !anyDirect; i++)
    for (int r = 0; r < Q->nref; r++) anyDirect |= gs_table_ref(&db->t[i], &Q->ref[r]);
  for (int i = 0; i < db->n; i++) {
    const GsTable *t = &db->t[i];
    int refTab[GS_REFS] = {0}, direct = 0, kind = 0;
    for (int r = 0; r < Q->nref; r++)
      if ((refTab[r] = gs_table_ref(t, &Q->ref[r]))) direct = 1;
    for (int w = 0; w < Q->nword; w++)
      if (gs_kind_word(t->kind, Q->word[w])) kind = 1;
    /* таблица соответствий (обозначения в ячейках) — когда о соответствии и
       спросили («аналог DIN 933») или своей таблицы у стандарта нет */
    int cellRef = t->keyAll && Q->nref && (kind || !anyDirect);
    if (Q->nref ? !direct && !cellRef : anyKind && !kind) continue;
    int score = (direct ? 100 : 0) + (kind ? 10 : 0) + gs_score(Q, t);
    int usable[GS_TOKS], nUse = 0, found = 0;
    for (int k = 0; k < Q->ntok; k++) {
      int u = gs_tok_for(Q, k, t, refTab);
      if (u < 0) u = !gs_is_kind(db, Q->word[Q->tokWord[k]]); /* слово перед ключом — другой вид? */
      if ((usable[k] = u)) nUse++;
    }
    /* по каждому ключу — строки, где он как есть; нет таких — по запасной форме:
       «М12х1,25» — строка мелкого шага, если она есть, иначе строка М12 */
    for (int k = 0; k < Q->ntok; k++) {
      if (!usable[k]) continue;
      for (int pass = 1, got = 0; pass <= 2 && !got; pass++)
        for (int row = 1; row <= t->nrow; row++)
          if (gs_key_match(t, row, Q->tok[k]) == pass) {
            n = gs_add(hit, n, max, t, row, 0, k, score);
            got = found = 1;
          }
    }
    for (int row = 1; row <= t->nrow; row++) {
      if (cellRef && !direct) {
        wchar_t **r = t->cell + (size_t)row * (size_t)t->ncol;
        for (int c = 0; c < t->ncol; c++) {
          int has = 0;
          for (int x = 0; x < Q->nref && !has; x++) has = gs_text_has_ref(r[c], &Q->ref[x]);
          if (has) {
            n = gs_add(hit, n, max, t, row, 0, -1, score);
            found = 1;
            break;
          }
        }
      }
    }
    if (found || !(direct || kind)) continue;
    if (!nUse) {
      if (direct) n = gs_add(hit, n, max, t, 0, 0, -1, score);
      continue;
    }
    /* точного нет — у сортамента (есть погонная масса) соседние размеры:
       «круг 51» → 50 и 52; у марок и прочего («сталь 41», «болт М13») — что в
       таблице есть: марка 40 — не «ближайшая» к 41 */
    int close = 0, first = -1;
    for (int k = 0; k < Q->ntok && !close; k++) {
      double v, x;
      if (!usable[k]) continue;
      if (first < 0) first = k;
      if (t->keyAll || t->massCol < 0 || !gs_number(Q->tok[k], &v)) continue;
      int lo = 0, hi = 0;
      double dlo = 0, dhi = 0;
      for (int row = 1; row <= t->nrow; row++) {
        if (!gs_number(gs_cell(t, row, t->keyCol[0]), &x)) continue;
        if (x < v && (!lo || x > dlo)) lo = row, dlo = x;
        if (x > v && (!hi || x < dhi)) hi = row, dhi = x;
      }
      if (lo) n = gs_add(hit, n, max, t, lo, 1, k, score);
      if (hi) n = gs_add(hit, n, max, t, hi, 1, k, score);
      close = lo || hi;
    }
    if (!close) n = gs_add(hit, n, max, t, 0, 0, first, score);
  }
  /* ключ нашёлся где-то точно — соседи и сводки по нему из других таблиц не
     нужны: «сталь 40Х» — строка ГОСТ 4543, без «40Х нет» из ГОСТ 1050 */
  int exact[GS_TOKS] = {0}, m = 0;
  for (int i = 0; i < n; i++)
    if (hit[i].row && !hit[i].close && hit[i].tok >= 0) exact[hit[i].tok] = 1;
  for (int i = 0; i < n; i++)
    if (!((hit[i].close || !hit[i].row) && hit[i].tok >= 0 && exact[hit[i].tok])) hit[m++] = hit[i];
  qsort(hit, (size_t)m, sizeof(GsHit), gs_hit_cmp);
  return m;
}

static int gs_is_checked(const GsTable *t) {
  wchar_t f[8];
  gs_fold(t->checked, f, 8);
  return !wcsncmp(f, L"да", 2);
}

/* Подпись таблицы: «ГОСТ 7798-70, табл. 1 — Размеры, мм (сверено)» — в подвал ответа */
static void gs_cite(const GsTable *t, GsBuf *b) {
  gs_cat(b, t->std);
  if (t->table[0]) gs_cat(b, L", табл. "), gs_cat(b, t->table);
  gs_cat(b, gs_is_checked(t) ? L" — сверено" : L" — НЕ сверено человеком");
}

/* Найденное — текстом для человека и для модели: по таблицам, с источником. */
static void gs_text(const GsQuery *Q, const GsHit *hit, int n, GsBuf *b) {
  for (int i = 0; i < n; i++) {
    const GsTable *t = hit[i].t;
    if (!i || hit[i - 1].t != t) {
      if (i) gs_ch(b, L'\n');
      gs_cat(b, t->std);
      if (t->title[0]) gs_cat(b, L" «"), gs_cat(b, t->title), gs_cat(b, L"»");
      if (t->table[0]) gs_cat(b, L", табл. "), gs_cat(b, t->table);
      gs_cat(b, L"\nИсточник: ");
      gs_cat(b, t->src[0] ? t->src : L"не указан");
      if (gs_is_checked(t)) gs_cat(b, L". Сверено человеком: "), gs_cat(b, t->checked);
      else gs_cat(b, L". НЕ СВЕРЕНО человеком с источником — числа давать с оговоркой");
      if (t->cross[0]) gs_cat(b, L"\nСверка по второму источнику: "), gs_cat(b, t->cross);
      gs_ch(b, L'\n');
      int nearTok = -1;
      for (int j = i; j < n && hit[j].t == t; j++)
        if (hit[j].close) nearTok = hit[j].tok;
      if (nearTok >= 0) {
        gs_cat(b, L"«");
        gs_cat(b, Q->tok[nearTok]);
        gs_cat(b, L"» в таблице нет, ближайшие строки:\n");
      }
    }
    if (!hit[i].row) {
      gs_cat(b, L"  ");
      if (hit[i].tok >= 0) gs_cat(b, L"«"), gs_cat(b, Q->tok[hit[i].tok]), gs_cat(b, L"» в таблице нет. ");
      gs_cat(b, L"Строк в таблице: ");
      gs_int(b, t->nrow);
      gs_cat(b, L"; ");
      gs_cat(b, gs_cell(t, 0, t->keyAll ? 0 : t->keyCol[0]));
      gs_cat(b, L" от ");
      gs_cat(b, gs_cell(t, 1, t->keyAll ? 0 : t->keyCol[0]));
      gs_cat(b, L" до ");
      gs_cat(b, gs_cell(t, t->nrow, t->keyAll ? 0 : t->keyCol[0]));
      gs_cat(b, L". Столбцы: ");
      for (int c = 0; c < t->ncol; c++) gs_cat(b, c ? L"; " : L""), gs_cat(b, gs_cell(t, 0, c));
      gs_cat(b, hit[i].tok >= 0 ? L".\n" : L". Нужен размер или марка — назовите.\n");
    } else {
      gs_cat(b, L"  ");
      for (int c = 0, first = 1; c < t->ncol; c++) {
        const wchar_t *v = gs_cell(t, hit[i].row, c);
        if (!v[0]) continue;
        if (!first) gs_cat(b, L"; ");
        first = 0;
        gs_cat(b, gs_cell(t, 0, c));
        gs_cat(b, L": ");
        gs_cat(b, v);
      }
      gs_ch(b, L'\n');
      double m;
      if (Q->len > 0 && t->massCol >= 0 && gs_number(gs_cell(t, hit[i].row, t->massCol), &m)) {
        gs_cat(b, L"  Масса заготовки L = ");
        gs_num(b, Q->len);
        gs_cat(b, L" мм: ");
        gs_num(b, m);
        gs_cat(b, L" кг/м × ");
        gs_num(b, Q->len / 1000);
        gs_cat(b, L" м = ");
        gs_num(b, m * Q->len / 1000);
        gs_cat(b, L" кг (по столбцу «");
        gs_cat(b, gs_cell(t, 0, t->massCol));
        gs_cat(b, L"»; без припусков на отрезку)\n");
      }
    }
    if (t->note[0] && (i == n - 1 || hit[i + 1].t != t)) gs_cat(b, L"  Примечание: "), gs_cat(b, t->note), gs_ch(b, L'\n');
  }
}

/* Вопрос к справочнику? Назван стандарт («ГОСТ 7798», «DIN 933»), изделие или
   сортамент с размером сразу за словом («болт М12», «круг 50»), марка со словом
   о свойствах («свойства 40Х», «чем заменить 20Х») или одни марки и размеры без
   слов («40Х», «М12»). Ошибиться не страшно: не нашлось — чат отвечает как обычно. */
static int gs_wanted(const wchar_t *q) {
  static const wchar_t *const kProps =
      L"свойства; состав; химсостав; твёрдость; прочность; предел; аналог; замена; заменитель; марка; "
      L"термообработка; закалка; отпуск; плотность; применение; расшифровка; размеры; масса; вес";
  GsQuery Q;
  gs_query(q, &Q);
  if (Q.nref) return 1;
  int grade = 0;
  for (int k = 0; k < Q.ntok; k++) {
    int w = Q.tokWord[k];
    if (w >= 0 && Q.wordSeg[w] == Q.tokSeg[k] && gs_kind_word(kGsKinds, Q.word[w])) return 1;
    for (const wchar_t *p = Q.tok[k]; *p && !grade; p++)
      grade = !gs_isdig(*p) && *p != L',' && *p != L'.' && *p != L'-' && *p != L'+';
  }
  if (!grade) return 0;
  if (!Q.nword) return 1;
  for (int w = 0; w < Q.nword; w++)
    if (gs_kind_word(kProps, Q.word[w])) return 1;
  return 0;
}
