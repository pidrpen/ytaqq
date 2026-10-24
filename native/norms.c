/* norms.c — нормы времени операций маршрута (с 2026.10.24): Тпз и Тшт токарной,
   сверлильной, фрезерной операций и резки лазером/плазмой по таблицам
   общемашиностроительных нормативов (ОМН). Норму считает код, а не модель, и у
   каждой цифры — откуда она: файл, карта, страница, позиция, столбец, или «с
   чертежа», «задано», «по умолчанию». Чего не хватило — в «Уточнить».

   Таблицы нормативов могут быть под авторским правом: они только у пользователя,
   в Документы\CursorPad\Нормативы (переносит человек или ИИ, сверяет человек).
   В репозитории — код и выдуманные таблицы тестов. Windows здесь не нужен:
     gcc -O2 -Wall -Werror -o norms_test norms_test.c && ./norms_test

   Формула (ОМН, серийное производство):
     Тв  = Тус + Тпер·переходов + Тизм·измерений
     Топ = То + Тв·Kтв
     Тшт = Топ·(1 + (аобс + аотл)/100),  Тшк = Тшт + Тпз/партия
   То — по режимам: n = 1000·V/(π·D), берётся ближайшая меньшая ступень станка;
     точение и сверление То = (L + l1)·i/(n·S), фрезерование То = (L + l1)·i/Sм,
     резка То = периметр/v + пробивок·tпроб (v — нормы резки завода, cutting_norms.h).

   Файл таблицы — текст, столбцы через табуляцию (UTF-8 или «Текст Юникод» из Excel):
     назначение  токарная.установка     что за таблица; несколько — через «;»
     источник    ОМН … 1974             книга
     ссылка      https://…#Par1593      где текст открыт в сети (якорь карты)
     карта / лист / страница            где в книге
     столбцы     масса, кг              по какому параметру выбирают столбец
     выбор       ближайшее большее | интерполяция
     ед          мин | % | коэф | мм/об | м/мин | мм/мин | мм
     сверено     да 25.10.2026 Иванов   пока не «да», у цифры пометка «не сверено»
     поз.  способ  как в книге  до 1  до 3  до 5       — шапка таблицы
     1     патрон  В самоцентрирующем патроне  0,15  0,2  0,25
   Столбцы между «поз.» и числовыми — ключи-параметры: слово-код («патрон»; несколько
   через «;»; «*» — любой) или диапазон («до 3», «св. 3 до 5», «3–5», «св. 20»).
   Пустая клетка ключа — как в строке выше (объединённая клетка книги). Значение —
   по правилу ОМН «ближайшее большее»: столбец (строка) с наименьшей границей, не
   меньшей параметра; «—» — значения нет. «вид  записи» — перечень (станки): первый
   ключ — код, дальше поля по именам.

   Параметры (имена ключей и «столбцы»): масса, способ, станок, размер станка,
   размерная группа, материал, марка, вид (черновая/чистовая), D, Dзаг, L, h (припуск
   на сторону), t, i, S, V, n, d, l (глубина отверстия), отверстий, отверстие
   (сквозное/глухое), B, Dф, z, Sм, толщина, периметр, пробивок, переход (вид
   перехода), И (измеряемый размер), квалитет, Ra, угол (резца в плане), перемещение,
   наладка, инструментов, переходов, измерений, деталей, поверхность (прокат/отливка),
   вид подачи, партия, смен (обработки партии), То, Тв, Топ; приёмы при переходе —
   прием, длина перемещения, смен резца, изменений подачи, изменений оборотов;
   измерения — измеритель, отсчёт, допуск, длина измерения, длина в D, поверхность
   измерения, способ размера; доп (дополнительное Тпз списком: «резьба; люнет»). Чего нет — подходят
   строки со «*» в этом ключе. Таблицы: <оп>.установка, .переход, .измерение, .ктв, .обслуживание,
   .отдых, .пз, .пз.доп (по каждому коду «доп»), .приемы, .измерение.к (периодичность),
   .подача, .скорость, .скорость.к (поправки), .глубина,
   .припуск, .врезание; резка.пробивка; станки (поля: группа, размер, ряд n,
   ряд S, ряд Sм). <оп> — токарная, сверлильная, фрезерная, резка; ктв и отдых —
   и без <оп>. */

#define NM_NONE (-1e300) /* нет значения */
#define NM_INF 1e300
#define NM_PI 3.14159265358979323846
static int nm_na(double v) { return v < -1e299; }

/* ---- строки и числа ------------------------------------------------------------ */

static wchar_t nm_fold1(wchar_t c) {
  if (c >= L'А' && c <= L'Я') return (wchar_t)(c + 32);
  if (c == L'Ё' || c == L'ё') return L'е';
  /* «16K20» латиницей — тот же станок; строчные h, t, n, d, l — параметры, их не трогаем */
  switch (c) {
  case L'A': case L'a': return L'а';
  case L'B': return L'в';
  case L'C': case L'c': return L'с';
  case L'E': case L'e': return L'е';
  case L'H': return L'н';
  case L'K': case L'k': return L'к';
  case L'M': return L'м';
  case L'O': case L'o': return L'о';
  case L'P': case L'p': return L'р';
  case L'T': return L'т';
  case L'X': case L'x': return L'х';
  case L'y': return L'у';
  }
  return c; /* остальная латиница как есть: D и d, L и l — разные параметры */
}

static const wchar_t *nm_skipsp(const wchar_t *s) {
  while (*s == L' ' || *s == 0xA0) s++;
  return s;
}

/* равны без учёта регистра русских букв, ё и пробелов по краям */
static int nm_eq(const wchar_t *a, const wchar_t *b) {
  a = nm_skipsp(a), b = nm_skipsp(b);
  while (*a && *b && nm_fold1(*a) == nm_fold1(*b)) a++, b++;
  return !*nm_skipsp(a) && !*nm_skipsp(b);
}

/* s начинается с w — тогда конец совпадения, иначе NULL */
static const wchar_t *nm_pre(const wchar_t *s, const wchar_t *w) {
  while (*w && nm_fold1(*s) == nm_fold1(*w)) s++, w++;
  return *w ? NULL : s;
}

/* есть ли code среди «a; b; c» */
static int nm_list_has(const wchar_t *list, const wchar_t *code) {
  wchar_t one[80];
  for (const wchar_t *p = list; p && *p;) {
    const wchar_t *e = wcschr(p, L';');
    int n = e ? (int)(e - p) : (int)wcslen(p);
    if (n > 79) n = 79;
    wcsncpy(one, p, (size_t)n);
    one[n] = 0;
    if (nm_eq(one, code)) return 1;
    p = e ? e + 1 : NULL;
  }
  return 0;
}

/* число с позиции p: «0,15», «12.5», «,5» нет; *end — за числом. Целое из цифр,
   делённое на 10^k, — ровно то же число, что «0.35» в коде: таблицы сравниваются точно */
static int nm_num(const wchar_t *p, double *v, const wchar_t **end) {
  p = nm_skipsp(p);
  if (!tn_isdig(*p)) return 0;
  long long m = 0;
  double div = 1;
  int dig = 0;
  while (tn_isdig(*p)) {
    if (dig < 17) m = m * 10 + (*p - L'0'), dig++;
    else div /= 10; /* лишние цифры целой части — порядок сохраняем */
    p++;
  }
  if ((*p == L',' || *p == L'.') && tn_isdig(p[1])) {
    p++;
    while (tn_isdig(*p)) {
      if (dig < 17) m = m * 10 + (*p - L'0'), dig++, div *= 10;
      p++;
    }
  }
  *v = (double)m / div;
  if (end) *end = p;
  return 1;
}

static long nm_ceil(double x) {
  long i = (long)x;
  return (double)i < x - 1e-9 ? i + 1 : i;
}

/* ---- диапазоны: «до 3», «св. 3 до 5», «3–5», «св. 20», «3» (= до 3), «*» ---------- */

typedef struct {
  double lo, hi;
  int loIn, any; /* loIn — граница lo входит; any — подходит всё */
} NmRng;

/* после числа — конец, пробел или разделитель диапазона: «16К20» — не число */
static int nm_numend(const wchar_t *e) {
  return !*e || *e == L' ' || *e == 0xA0 || *e == L'-' || *e == L'–' || *e == L'—' || *e == L'…' || *e == L'.' ||
         *e == L'*';
}

static int nm_rng(const wchar_t *s, NmRng *r) {
  r->lo = -NM_INF, r->hi = NM_INF, r->loIn = 0, r->any = 0;
  s = nm_skipsp(s);
  if (!*s) return 0;
  if (nm_eq(s, L"*") || nm_eq(s, L"любой") || nm_eq(s, L"любая") || nm_eq(s, L"любое")) return r->any = 1;
  const wchar_t *p, *e;
  double x, y;
  if ((p = nm_pre(s, L"до")) && (*p == L' ' || tn_isdig(*p))) {
    if (!nm_num(p, &x, &e) || !nm_numend(e)) return 0;
    r->hi = x;
    return 1;
  }
  p = nm_pre(s, L"свыше");
  if (!p) p = nm_pre(s, L"св.");
  if (!p) p = nm_pre(s, L"св ");
  if (!p) p = nm_pre(s, L"более");
  if (!p) p = nm_pre(s, L"больше");
  if (p) {
    if (!nm_num(p, &x, &e) || !nm_numend(e)) return 0;
    r->lo = x;
    e = nm_skipsp(e);
    const wchar_t *q = nm_pre(e, L"до");
    if (q && nm_num(q, &y, &e) && nm_numend(e)) r->hi = y;
    return 1;
  }
  if (!nm_num(s, &x, &e) || !nm_numend(e)) return 0;
  e = nm_skipsp(e);
  if (*e == L'-' || *e == L'–' || *e == L'—' || *e == L'…' || (e[0] == L'.' && e[1] == L'.')) {
    while (*e == L'-' || *e == L'–' || *e == L'—' || *e == L'…' || *e == L'.') e++;
    if (nm_num(e, &y, &e) && nm_numend(e)) {
      r->lo = x, r->loIn = 1, r->hi = y;
      return 1;
    }
    return 0;
  }
  r->hi = x;
  return 1;
}

static int nm_in(const NmRng *r, double v) {
  if (r->any) return 1;
  if (r->loIn ? v < r->lo - 1e-9 : v <= r->lo + 1e-9) return 0;
  return v <= r->hi + 1e-9;
}

/* значение клетки: «0,15», «12*» (сноска); «—», «-», «х», пусто — нет */
static double nm_val(const wchar_t *s) {
  double v;
  const wchar_t *e;
  if (!nm_num(s, &v, &e)) return NM_NONE;
  while (*e == L'*' || *e == L' ') e++;
  return *e ? NM_NONE : v;
}

/* ---- параметры расчёта ------------------------------------------------------------ */

enum { NMO_NONE, NMO_DEF, NMO_CALC, NMO_TAB, NMO_DRAW, NMO_ASK }; /* по возрастанию веса */
static const wchar_t *const kNmOrg[] = {L"", L"по умолчанию", L"расчёт", L"по таблице", L"с чертежа", L"задано"};

#define NM_PARS 56
typedef struct {
  wchar_t name[24];
  wchar_t s[64];   /* текстом: код («патрон») или число как записано */
  double v;        /* NM_NONE — не число */
  int org;
  wchar_t why[120]; /* откуда, если не очевидно: «заготовка Круг 45» */
} NmPar;

typedef struct {
  NmPar p[NM_PARS];
  int n;
} NmCtx;

static NmPar *nm_par(NmCtx *c, const wchar_t *name) {
  for (int i = 0; i < c->n; i++)
    if (!wcscmp(c->p[i].name, name) || nm_eq(c->p[i].name, name)) return &c->p[i];
  return NULL;
}

static const NmPar *nm_parc(const NmCtx *c, const wchar_t *name) { return nm_par((NmCtx *)c, name); }

/* записать, если такого нет или новое весомее (заданное в вопросе не перебить чертежом) */
static NmPar *nm_put(NmCtx *c, const wchar_t *name, const wchar_t *s, double v, int org, const wchar_t *why) {
  NmPar *p = nm_par(c, name);
  if (p && p->org > org) return p;
  if (!p) {
    if (c->n >= NM_PARS) return NULL;
    p = &c->p[c->n++];
  }
  memset(p, 0, sizeof(*p));
  tn_cpy(p->name, 24, name);
  if (s) tn_cpy(p->s, 64, s);
  else if (!nm_na(v)) tn_numstr(v, p->s, 64);
  p->v = v;
  p->org = org;
  if (why) tn_cpy(p->why, 120, why);
  return p;
}

static void nm_putv(NmCtx *c, const wchar_t *name, double v, int org, const wchar_t *why) {
  if (!nm_na(v)) nm_put(c, name, NULL, v, org, why);
}

static double nm_v(const NmCtx *c, const wchar_t *name) {
  const NmPar *p = nm_parc(c, name);
  return p ? p->v : NM_NONE;
}

/* ---- таблицы ---------------------------------------------------------------------- */

#define NM_KEYS 4
#define NM_VCOLS 24
#define NM_CELLS (NM_KEYS + NM_VCOLS + 4)

typedef struct {
  const wchar_t *pos, *book;
  const wchar_t *key[NM_KEYS];
  NmRng kr[NM_KEYS];
  const wchar_t *cell[NM_VCOLS];
  double v[NM_VCOLS];
} NmRow;

typedef struct {
  wchar_t *buf;     /* текст файла, порезанный на клетки: всё ниже указывает в него */
  wchar_t file[80];
  const wchar_t *purpose, *src, *url, *card, *sheet, *page, *note, *unit, *checked, *colName;
  int interp, records, isChecked;
  int nkeys, keyNum[NM_KEYS];
  const wchar_t *keyName[NM_KEYS];
  int ncols, axis; /* axis — столбец выбирают по параметру colName */
  const wchar_t *colHead[NM_VCOLS];
  NmRng cr[NM_VCOLS];
  NmRow *row;
  int nrows;
} NmTab;

typedef struct {
  NmTab *t;
  int n, cap;
  wchar_t err[1200]; /* какие файлы не прочитались и почему */
} NmBase;

static void nm_tab_free(NmTab *t) {
  free(t->buf);
  free(t->row);
  memset(t, 0, sizeof(*t));
}

static void nm_base_free(NmBase *b) {
  for (int i = 0; i < b->n; i++) nm_tab_free(&b->t[i]);
  free(b->t);
  memset(b, 0, sizeof(*b));
}

/* обрезать пробелы и кавычки Excel («"a ""b"""» → «a "b"») — на месте */
static wchar_t *nm_unq(wchar_t *s) {
  while (*s == L' ' || *s == 0xA0) s++;
  size_t n = wcslen(s);
  while (n && (s[n - 1] == L' ' || s[n - 1] == 0xA0)) s[--n] = 0;
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

static int nm_cells(wchar_t *line, wchar_t **cell, int max) {
  int n = 0;
  for (wchar_t *p = line;;) {
    wchar_t *e = wcschr(p, L'\t');
    if (e) *e = 0;
    if (n < max) cell[n++] = nm_unq(p);
    if (!e) break;
    p = e + 1;
  }
  while (n && !cell[n - 1][0]) n--;
  return n;
}

/* имя параметра из шапки: «масса, кг» → «масса» (на месте) */
static const wchar_t *nm_cut_name(wchar_t *s) {
  wchar_t *e = s;
  while (*e && *e != L',' && *e != L'(') e++;
  *e = 0;
  return nm_unq(s);
}

static void nm_err(NmBase *b, const wchar_t *file, const wchar_t *what) {
  wchar_t t[240];
  tn_cpy(t, 240, file);
  tn_cat(t, 240, L": ");
  tn_cat(t, 240, what);
  tn_item(b->err, 1200, t);
}

/* разобрать текст файла; text переходит таблице (или освобождается при ошибке) */
static int nm_base_add(NmBase *b, wchar_t *text, const wchar_t *file) {
  if (!text) return 0;
  if (b->n >= b->cap) {
    int cap = b->cap ? b->cap * 2 : 16;
    NmTab *nt = (NmTab *)realloc(b->t, (size_t)cap * sizeof(NmTab));
    if (!nt) return free(text), 0;
    b->t = nt, b->cap = cap;
  }
  NmTab *t = &b->t[b->n];
  memset(t, 0, sizeof(*t));
  t->buf = text;
  tn_cpy(t->file, 80, file);
  int capRows = 0, head = 0, bookCol = -1, keyCol[NM_KEYS] = {0}, valCol[NM_VCOLS] = {0};
  const wchar_t *vid = NULL, *pick = NULL;
  wchar_t *cell[NM_CELLS];
  wchar_t *p = text;
  if (*p == 0xFEFF) p++;
  const wchar_t *bad = NULL;
  while (*p && !bad) {
    wchar_t *e = wcschr(p, L'\n'), *line = p;
    if (e) *e = 0;
    p = e ? e + 1 : p + wcslen(p);
    size_t ln = wcslen(line);
    if (ln && line[ln - 1] == L'\r') line[--ln] = 0;
    if (!line[0] || line[0] == L'#') continue;
    int n = nm_cells(line, cell, NM_CELLS);
    if (!n) continue;
    if (!head) {
      if (nm_pre(cell[0], L"поз")) { /* шапка таблицы */
        head = 1;
        for (int j = 1; j < n; j++) {
          NmRng r;
          if (nm_eq(cell[j], L"как в книге") || nm_eq(cell[j], L"в книге") || nm_eq(cell[j], L"наименование"))
            bookCol = j;
          else if ((t->records && t->nkeys) || nm_eq(cell[j], L"значение") || (!t->records && nm_rng(cell[j], &r))) {
            if (t->ncols >= NM_VCOLS) { bad = L"больше 24 столбцов значений"; break; }
            valCol[t->ncols] = j;
            t->colHead[t->ncols] = cell[j];
            if (!t->records && !nm_rng(cell[j], &t->cr[t->ncols])) t->cr[t->ncols].any = 1;
            t->ncols++;
          } else if (t->ncols) {
            bad = L"ключевой столбец после столбцов значений";
          } else {
            if (t->nkeys >= NM_KEYS) { bad = L"больше 4 ключевых столбцов"; break; }
            keyCol[t->nkeys] = j;
            t->keyName[t->nkeys++] = nm_cut_name(cell[j]);
          }
        }
        continue;
      }
      if (n < 2) continue;
      const wchar_t *k = cell[0], *v = cell[1];
      if (nm_eq(k, L"назначение")) t->purpose = v;
      else if (nm_eq(k, L"источник")) t->src = v;
      else if (nm_eq(k, L"ссылка")) t->url = v;
      else if (nm_eq(k, L"карта")) t->card = v;
      else if (nm_eq(k, L"лист")) t->sheet = v;
      else if (nm_eq(k, L"страница") || nm_eq(k, L"стр.") || nm_eq(k, L"стр")) t->page = v;
      else if (nm_eq(k, L"примечание")) t->note = v;
      else if (nm_eq(k, L"ед") || nm_eq(k, L"ед.") || nm_eq(k, L"единица")) t->unit = v;
      else if (nm_eq(k, L"сверено")) t->checked = v;
      else if (nm_eq(k, L"столбцы")) t->colName = nm_cut_name(cell[1]);
      else if (nm_eq(k, L"выбор")) pick = v;
      else if (nm_eq(k, L"вид")) vid = v, t->records = nm_eq(v, L"записи");
      continue;
    }
    /* строка таблицы */
    if (t->nrows >= capRows) {
      capRows = capRows ? capRows * 2 : 32;
      NmRow *nr = (NmRow *)realloc(t->row, (size_t)capRows * sizeof(NmRow));
      if (!nr) { bad = L"нет памяти"; break; }
      t->row = nr;
    }
    NmRow *r = &t->row[t->nrows];
    memset(r, 0, sizeof(*r));
    r->pos = cell[0];
    r->book = bookCol > 0 && bookCol < n ? cell[bookCol] : L"";
    int any = 0;
    for (int k = 0; k < t->nkeys; k++) {
      r->key[k] = keyCol[k] < n ? cell[keyCol[k]] : L"";
      if (!r->key[k][0] && t->nrows) r->key[k] = t->row[t->nrows - 1].key[k]; /* объединённая клетка */
    }
    for (int c = 0; c < t->ncols; c++) {
      r->cell[c] = valCol[c] < n ? cell[valCol[c]] : L"";
      r->v[c] = nm_val(r->cell[c]);
      any |= r->cell[c][0] != 0;
    }
    if (any || t->nkeys) t->nrows++;
  }
  (void)vid;
  if (!bad && !t->purpose) bad = L"нет строки «назначение»";
  if (!bad && !head) bad = L"нет шапки таблицы (строка «поз.»)";
  if (!bad && !t->ncols) bad = L"нет столбцов значений";
  if (!bad && !t->nrows) bad = L"нет строк";
  if (!bad && t->records && !t->nkeys) bad = L"у перечня нет столбца-кода";
  if (!bad && !t->records) {
    t->axis = !(t->ncols == 1 && t->cr[0].any);
    if (t->axis && !t->colName) bad = L"нет строки «столбцы» — по какому параметру выбирать столбец";
    for (int c = 0; c < t->ncols && !bad; c++)
      if (t->cr[c].any && t->axis) bad = L"столбец значений без диапазона в шапке";
  }
  if (bad) {
    nm_err(b, file, bad);
    nm_tab_free(t);
    return 0;
  }
  /* ключ числовой, если все его клетки — диапазоны */
  for (int k = 0; k < t->nkeys; k++) {
    int num = 1, seen = 0;
    for (int i = 0; i < t->nrows && num; i++) {
      NmRng r;
      if (!t->row[i].key[k][0]) continue;
      if (!nm_rng(t->row[i].key[k], &r)) num = 0;
      else seen += !r.any;
    }
    t->keyNum[k] = num && seen;
    for (int i = 0; i < t->nrows; i++)
      if (!nm_rng(t->row[i].key[k], &t->row[i].kr[k])) t->row[i].kr[k].any = !t->row[i].key[k][0] || !t->keyNum[k];
  }
  t->interp = pick && nm_pre(pick, L"интерпол");
  t->isChecked = t->checked && nm_pre(nm_skipsp(t->checked), L"да") != NULL;
  b->n++;
  return 1;
}

/* таблица с этим назначением? */
static int nm_tab_is(const NmTab *t, const wchar_t *purpose) { return nm_list_has(t->purpose, purpose); }

static int nm_has(const NmBase *b, const wchar_t *purpose) {
  for (int i = 0; i < b->n; i++)
    if (!b->t[i].records && nm_tab_is(&b->t[i], purpose)) return 1;
  return 0;
}

/* ---- выбор значения ---------------------------------------------------------------- */

typedef struct {
  int ok, notab;
  double v;
  const NmTab *t;
  const NmRow *r;
  int col;
  wchar_t why[360]; /* найдено — по чему и где; нет — чего не хватило */
  wchar_t need[40]; /* какого параметра нет */
} NmHit;

/* «к. 2 л. 1 с. 46 поз. 3» */
static void nm_where(const NmTab *t, const NmRow *r, wchar_t *out, int cap) {
  out[0] = 0;
  if (t->card && t->card[0]) tn_cat(out, cap, L"к. "), tn_cat(out, cap, t->card);
  if (t->sheet && t->sheet[0]) tn_cat(out, cap, out[0] ? L" л. " : L"л. "), tn_cat(out, cap, t->sheet);
  if (t->page && t->page[0]) tn_cat(out, cap, out[0] ? L" с. " : L"с. "), tn_cat(out, cap, t->page);
  if (!out[0]) tn_cpy(out, cap, t->file);
  if (r && r->pos && r->pos[0]) tn_cat(out, cap, L" поз. "), tn_cat(out, cap, r->pos);
}

/* «масса 2,4» — параметр, как его взяли */
static void nm_said(const NmPar *p, wchar_t *out, int cap) {
  tn_cat(out, cap, p->name);
  tn_cat(out, cap, L" ");
  tn_cat(out, cap, p->s);
}

static int nm_text_match(const wchar_t *cell, const NmPar *p) {
  NmRng r;
  if (nm_rng(cell, &r) && r.any) return 1;
  return nm_list_has(cell, p->s);
}

static int nm_try(const NmTab *t, const NmCtx *c, NmHit *h) {
  memset(h, 0, sizeof(*h));
  h->t = t;
  const NmPar *kp[NM_KEYS];
  int asNum[NM_KEYS]; /* сравнивать диапазоном; марка «45» — словом, хоть клетки и похожи на числа */
  for (int k = 0; k < t->nkeys; k++) {
    kp[k] = nm_parc(c, t->keyName[k]);
    asNum[k] = kp[k] && t->keyNum[k] && !nm_na(kp[k]->v);
    if (kp[k] && t->keyNum[k] && nm_na(kp[k]->v) && !kp[k]->s[0]) kp[k] = NULL;
  }
  /* параметра нет — подходят только строки со «*» в этом ключе; не нашлось — просим параметр */
  int best = -1, miss = -1;
  for (int i = 0; i < t->nrows; i++) {
    const NmRow *r = &t->row[i];
    int ok = 1;
    for (int k = 0; k < t->nkeys && ok; k++) {
      if (!kp[k]) {
        ok = r->kr[k].any && (t->keyNum[k] || nm_eq(r->key[k], L"*") || !r->key[k][0]);
        if (!ok && miss < 0) miss = k;
        continue;
      }
      ok = asNum[k] ? nm_in(&r->kr[k], kp[k]->v) : nm_text_match(r->key[k], kp[k]);
    }
    if (!ok) continue;
    if (best < 0) {
      best = i;
      continue;
    }
    /* ближайшее большее: у кого граница меньше — по ключам слева направо */
    for (int k = 0; k < t->nkeys; k++) {
      if (!asNum[k]) continue;
      double a = r->kr[k].any ? NM_INF : r->kr[k].hi, b = t->row[best].kr[k].any ? NM_INF : t->row[best].kr[k].hi;
      if (a < b - 1e-9) {
        best = i;
        break;
      }
      if (a > b + 1e-9) break;
    }
  }
  if (best < 0 && miss >= 0) {
    tn_cpy(h->need, 40, t->keyName[miss]);
    tn_cpy(h->why, 360, L"нужен параметр «");
    tn_cat(h->why, 360, t->keyName[miss]);
    tn_cat(h->why, 360, L"»");
    return 0;
  }
  wchar_t by[200] = {0};
  for (int k = 0; k < t->nkeys; k++) {
    if (!kp[k]) continue;
    if (by[0]) tn_cat(by, 200, L", ");
    nm_said(kp[k], by, 200);
  }
  if (best < 0) {
    tn_cpy(h->why, 360, L"в таблице нет строки для: ");
    tn_cat(h->why, 360, by);
    return 0;
  }
  const NmRow *r = &t->row[best];
  h->r = r;
  wchar_t where[80], colTxt[120] = {0};
  nm_where(t, r, where, 80);
  int col = 0;
  double v = r->v[0];
  if (t->axis) {
    const NmPar *cp = nm_parc(c, t->colName);
    if (!cp || nm_na(cp->v)) {
      tn_cpy(h->need, 40, t->colName);
      tn_cpy(h->why, 360, L"нужен параметр «");
      tn_cat(h->why, 360, t->colName);
      tn_cat(h->why, 360, L"»");
      return 0;
    }
    if (by[0]) tn_cat(by, 200, L", ");
    nm_said(cp, by, 200);
    col = -1;
    for (int j = 0; j < t->ncols; j++)
      if (nm_in(&t->cr[j], cp->v) && (col < 0 || t->cr[j].hi < t->cr[col].hi)) col = j;
    if (col < 0) {
      tn_cpy(h->why, 360, cp->name);
      tn_cat(h->why, 360, L" ");
      tn_cat(h->why, 360, cp->s);
      tn_cat(h->why, 360, L" — за пределами таблицы ");
      tn_cat(h->why, 360, where);
      return 0;
    }
    v = r->v[col];
    tn_cpy(colTxt, 120, L", «");
    tn_cat(colTxt, 120, t->colHead[col]);
    tn_cat(colTxt, 120, L"»");
    /* интерполяция — между соседними границами, если параметр не на границе */
    if (t->interp && col > 0 && !nm_na(v) && !nm_na(r->v[col - 1]) && t->cr[col - 1].hi < NM_INF &&
        t->cr[col].hi < NM_INF && cp->v < t->cr[col].hi - 1e-9 && cp->v > t->cr[col - 1].hi) {
      double a = r->v[col - 1], b2 = r->v[col], x0 = t->cr[col - 1].hi, x1 = t->cr[col].hi;
      v = a + (b2 - a) * (cp->v - x0) / (x1 - x0);
      tn_cpy(colTxt, 120, L", между «");
      tn_cat(colTxt, 120, t->colHead[col - 1]);
      tn_cat(colTxt, 120, L"» и «");
      tn_cat(colTxt, 120, t->colHead[col]);
      tn_cat(colTxt, 120, L"»");
    }
  }
  h->col = col;
  if (nm_na(v)) {
    tn_cpy(h->why, 360, L"в клетке прочерк: ");
    tn_cat(h->why, 360, where);
    tn_cat(h->why, 360, colTxt);
    return 0;
  }
  h->v = v;
  h->ok = 1;
  tn_cpy(h->why, 360, by);
  if (by[0]) tn_cat(h->why, 360, L" → ");
  tn_cat(h->why, 360, where);
  if (r->book && r->book[0]) {
    wchar_t bk[64];
    tn_cpy(bk, 64, r->book);
    tn_cat(h->why, 360, L" «");
    tn_cat(h->why, 360, bk);
    tn_cat(h->why, 360, L"»");
  }
  tn_cat(h->why, 360, colTxt);
  if (!t->isChecked) tn_cat(h->why, 360, L", не сверено");
  return 1;
}

static int nm_find(const NmBase *b, const wchar_t *purpose, const NmCtx *c, NmHit *h) {
  NmHit x, first;
  int seen = 0, needSeen = 0;
  memset(&first, 0, sizeof(first));
  for (int i = 0; i < b->n; i++) {
    const NmTab *t = &b->t[i];
    if (t->records || !nm_tab_is(t, purpose)) continue;
    if (nm_try(t, c, &x)) {
      *h = x;
      return 1;
    }
    if (!seen++ || (x.need[0] && !needSeen)) first = x, needSeen = x.need[0] != 0;
  }
  if (!seen) {
    memset(h, 0, sizeof(*h));
    h->notab = 1;
    tn_cpy(h->why, 360, L"нет таблицы «");
    tn_cat(h->why, 360, purpose);
    tn_cat(h->why, 360, L"»");
    return 0;
  }
  *h = first;
  return 0;
}

/* перечень (станки): строка с кодом code; поле field — клетка или NULL */
static const wchar_t *nm_rec(const NmBase *b, const wchar_t *purpose, const wchar_t *code, const wchar_t *field,
                             const NmTab **tab) {
  for (int i = 0; i < b->n; i++) {
    const NmTab *t = &b->t[i];
    if (!t->records || !nm_tab_is(t, purpose)) continue;
    for (int r = 0; r < t->nrows; r++) {
      if (!nm_list_has(t->row[r].key[0], code)) continue;
      for (int c = 0; c < t->ncols; c++)
        if (nm_eq(t->colHead[c], field)) {
          if (tab) *tab = t;
          return t->row[r].cell[c][0] ? t->row[r].cell[c] : NULL;
        }
      return NULL;
    }
  }
  return NULL;
}

/* первый в перечне, у кого поле field содержит value: станок группы «токарная» */
static const wchar_t *nm_rec_first(const NmBase *b, const wchar_t *purpose, const wchar_t *field,
                                   const wchar_t *value) {
  for (int i = 0; i < b->n; i++) {
    const NmTab *t = &b->t[i];
    if (!t->records || !nm_tab_is(t, purpose)) continue;
    for (int c = 0; c < t->ncols; c++)
      if (nm_eq(t->colHead[c], field))
        for (int r = 0; r < t->nrows; r++)
          if (nm_list_has(t->row[r].cell[c], value)) return t->row[r].key[0];
  }
  return NULL;
}

/* ближайшая меньшая ступень из ряда «12,5 16 20 25»: *low — ниже наименьшей */
static double nm_step(const wchar_t *row, double x, int *low) {
  double best = NM_NONE, min = NM_NONE, v;
  const wchar_t *p = row, *e;
  *low = 0;
  while (p && *p) {
    if (nm_num(p, &v, &e)) {
      if (v <= x * (1 + 1e-9) && (nm_na(best) || v > best)) best = v;
      if (nm_na(min) || v < min) min = v;
      p = e;
    } else {
      p++;
    }
  }
  if (nm_na(best) && !nm_na(min)) best = min, *low = 1;
  return best;
}

/* ---- расчёт операции -------------------------------------------------------------- */

enum { NMK_NONE, NMK_TURN, NMK_DRILL, NMK_MILL, NMK_CUT };
static const wchar_t *const kNmKind[] = {L"", L"токарная", L"сверлильная", L"фрезерная", L"резка"};

typedef struct {
  int kind, idx;   /* idx — номер операции в маршруте (с нуля) */
  wchar_t op[48];
  double to, tv, top, tsht, tpz, tshk;
  int full;        /* в Тшт есть всё: То, Тв, обслуживание */
  wchar_t how[4000];
} NmOp;

#define NM_USED 48
typedef struct {
  NmOp op[12];
  int n;
  const NmTab *used[NM_USED];
  int nused;
  wchar_t need[800];  /* что уточнить — сводно */
  wchar_t miss[800];  /* каких таблиц нет вовсе */
  wchar_t skip[400];  /* какие операции не нормируются пока */
  int cutUsed;        /* брали скорость из норм резки завода */
} NmResult;

typedef struct {
  const NmBase *b;
  NmCtx c;
  NmOp *o;
  NmResult *res;
  const wchar_t *pref; /* «токарная» */
} NmJob;

static void nm_line(NmJob *j, const wchar_t *s) {
  tn_cat(j->o->how, 4000, L"\n   ");
  tn_cat(j->o->how, 4000, s);
}

static void nm_need(NmJob *j, const wchar_t *what) {
  if (!tn_list_has(j->res->need, what)) tn_item(j->res->need, 800, what);
}

static void nm_use(NmResult *res, const NmTab *t) {
  for (int i = 0; i < res->nused; i++)
    if (res->used[i] == t) return;
  if (res->nused < NM_USED) res->used[res->nused++] = t;
}

static void nm_unit(const NmTab *t, wchar_t *out, int cap) {
  out[0] = 0;
  if (!t || !t->unit || !t->unit[0] || nm_eq(t->unit, L"коэф")) return;
  tn_cpy(out, cap, t->unit[0] == L'%' ? L"" : L" ");
  tn_cat(out, cap, t->unit);
}

/* «<пр>.<что>» */
static void nm_purp(const NmJob *j, const wchar_t *what, wchar_t *out, int cap) {
  tn_cpy(out, cap, j->pref);
  tn_cat(out, cap, L".");
  tn_cat(out, cap, what);
}

/* найти и записать строку разбора «Тус 0,22 мин — масса 2,4 → к. 2 …»; нет таблицы
   вовсе — в «Нет таблиц» (если required); не подошла — строка и параметр в «Уточнить» */
static int nm_look(NmJob *j, const wchar_t *purpose, const wchar_t *label, int required, double *v) {
  NmHit h;
  wchar_t s[600], u[16];
  if (nm_find(j->b, purpose, &j->c, &h)) {
    nm_use(j->res, h.t);
    *v = h.v;
    nm_unit(h.t, u, 16);
    tn_cpy(s, 600, label);
    tn_cat(s, 600, h.t->unit && nm_eq(h.t->unit, L"коэф") ? L" ×" : L" ");
    tn_catnum(s, 600, h.v);
    tn_cat(s, 600, u);
    tn_cat(s, 600, L" — ");
    tn_cat(s, 600, h.why);
    nm_line(j, s);
    return 1;
  }
  if (h.notab) {
    if (required) {
      if (!tn_list_has(j->res->miss, purpose)) tn_item(j->res->miss, 800, purpose);
      tn_cpy(s, 600, label);
      tn_cat(s, 600, L": нет таблицы «");
      tn_cat(s, 600, purpose);
      tn_cat(s, 600, L"»");
      nm_line(j, s);
    }
    return 0;
  }
  tn_cpy(s, 600, label);
  tn_cat(s, 600, L": ");
  tn_cat(s, 600, h.why);
  nm_line(j, s);
  if (h.need[0]) nm_need(j, h.need);
  return 0;
}

static int nm_look_pref(NmJob *j, const wchar_t *what, const wchar_t *label, int required, double *v) {
  wchar_t p[64];
  nm_purp(j, what, p, 64);
  return nm_look(j, p, label, required, v);
}

/* «L 120 (задано)» — откуда взят параметр */
static void nm_from(const NmCtx *c, const wchar_t *name, const wchar_t *unit, wchar_t *out, int cap) {
  const NmPar *p = nm_parc(c, name);
  if (!p) return;
  tn_cat(out, cap, p->name);
  tn_cat(out, cap, L" ");
  tn_cat(out, cap, p->s);
  if (unit) tn_cat(out, cap, unit);
  tn_cat(out, cap, L" (");
  tn_cat(out, cap, kNmOrg[p->org]);
  if (p->why[0]) tn_cat(out, cap, L": "), tn_cat(out, cap, p->why);
  tn_cat(out, cap, L")");
}

/* параметр обязателен: нет — строка «нужно …» и пункт «Уточнить» */
static int nm_req(NmJob *j, const wchar_t *name, const wchar_t *what) {
  if (!nm_na(nm_v(&j->c, name))) return 1;
  wchar_t s[160];
  tn_cpy(s, 160, L"нет «");
  tn_cat(s, 160, name);
  tn_cat(s, 160, L"» — ");
  tn_cat(s, 160, what);
  nm_line(j, s);
  nm_need(j, what);
  return 0;
}

/* произведение поправок: каждая таблица «<оп>.<what>», что подходит (Kv, периодичность измерений) */
static double nm_coefs(NmJob *j, const wchar_t *what, const wchar_t *label) {
  wchar_t p[64], s[400];
  double k = 1;
  nm_purp(j, what, p, 64);
  for (int i = 0; i < j->b->n; i++) {
    const NmTab *t = &j->b->t[i];
    NmHit h;
    if (t->records || !nm_tab_is(t, p)) continue;
    if (nm_try(t, &j->c, &h)) {
      nm_use(j->res, t);
      k *= h.v;
      tn_cpy(s, 400, label);
      tn_cat(s, 400, L" ×");
      tn_catnum(s, 400, h.v);
      tn_cat(s, 400, L" — ");
      tn_cat(s, 400, h.why);
    } else {
      tn_cpy(s, 400, label);
      tn_cat(s, 400, L" не взят: ");
      tn_cat(s, 400, h.why);
      if (h.need[0]) nm_need(j, h.need);
    }
    nm_line(j, s);
  }
  return k;
}

/* ступень станка из ряда поля field («ряд n»); нет ряда — берётся расчётное */
static double nm_machine_step(NmJob *j, const wchar_t *field, const wchar_t *what, double x, wchar_t *s, int cap) {
  const NmPar *m = nm_parc(&j->c, L"станок");
  const NmTab *t = NULL;
  const wchar_t *row = m ? nm_rec(j->b, L"станки", m->s, field, &t) : NULL;
  if (!row) {
    tn_cat(s, cap, L" (ступени станка ");
    tn_cat(s, cap, what);
    tn_cat(s, cap, L" не заданы — расчётное)");
    return x;
  }
  int low;
  double st = nm_step(row, x, &low);
  nm_use(j->res, t);
  tn_cat(s, cap, L" → ");
  tn_catnum(s, cap, st);
  tn_cat(s, cap, low ? L" (наименьшая ступень " : L" (ступень ");
  tn_cat(s, cap, m->s);
  tn_cat(s, cap, t->isChecked ? L")" : L", не сверено)");
  return st;
}

/* подача S и скорость V → обороты n по диаметру D; FALSE — чего-то нет.
   Заданную подачу не трогаем, табличную — к ближайшей меньшей ступени станка. */
static int nm_mode(NmJob *j, double D, double *S, double *n) {
  wchar_t s[300];
  double V = NM_NONE, k;
  *S = nm_v(&j->c, L"S");
  if (!nm_na(*S)) {
    s[0] = 0;
    nm_from(&j->c, L"S", L" мм/об", s, 300);
    nm_line(j, s);
  } else {
    if (!nm_look_pref(j, L"подача", L"S", 1, S)) return 0;
    tn_cpy(s, 300, L"S по станку ");
    tn_catnum(s, 300, *S);
    *S = nm_machine_step(j, L"ряд S", L"подач", *S, s, 300);
    nm_putv(&j->c, L"S", *S, NMO_TAB, L"по таблице и станку");
    nm_line(j, s);
  }
  V = nm_v(&j->c, L"V");
  if (!nm_na(V)) {
    tn_cpy(s, 300, L"");
    nm_from(&j->c, L"V", L" м/мин", s, 300);
    nm_line(j, s);
  } else if (!nm_look_pref(j, L"скорость", L"V", 1, &V)) {
    return 0;
  } else {
    V *= nm_coefs(j, L"скорость.к", L"Kv");
  }
  k = 1000 * V / (NM_PI * D);
  tn_cpy(s, 300, L"n = 1000·V/(π·D) = 1000·");
  tn_catnum(s, 300, V);
  tn_cat(s, 300, L"/(π·");
  tn_catnum(s, 300, D);
  tn_cat(s, 300, L") = ");
  tn_catnum(s, 300, k);
  tn_cat(s, 300, L" об/мин");
  *n = nm_machine_step(j, L"ряд n", L"оборотов", k, s, 300);
  nm_line(j, s);
  return *n > 0 && *S > 0;
}

/* врезание и перебег l1: задано, по таблице или не учтено */
static double nm_l1(NmJob *j) {
  double l1 = nm_v(&j->c, L"врезание");
  if (!nm_na(l1)) return l1;
  if (nm_look_pref(j, L"врезание", L"l1 (врезание и перебег)", 0, &l1)) return l1;
  nm_line(j, L"l1 (врезание и перебег) не учтено — нет таблицы и не задано");
  return 0;
}

static double nm_to_turn(NmJob *j) {
  wchar_t s[300];
  int fine = nm_eq(nm_parc(&j->c, L"вид") ? nm_parc(&j->c, L"вид")->s : L"", L"чистовая");
  int okD = nm_req(j, L"D", L"диаметр обработки D");
  int okL = nm_req(j, L"L", L"длина обработки L");
  if (!okD || !okL) return NM_NONE;
  double D = nm_v(&j->c, L"D"), L = nm_v(&j->c, L"L"), Dz = nm_v(&j->c, L"Dзаг"), h = nm_v(&j->c, L"h");
  if (nm_na(h)) {
    if (fine) {
      if (!nm_look_pref(j, L"припуск", L"h", 1, &h)) {
        nm_need(j, L"припуск h под чистовую");
        return NM_NONE;
      }
      nm_putv(&j->c, L"h", h, NMO_TAB, L"таблица припусков");
    } else if (!nm_na(Dz) && Dz > D) {
      h = (Dz - D) / 2;
      wchar_t w[100];
      tn_cpy(w, 100, L"(Dзаг − D)/2 = (");
      tn_catnum(w, 100, Dz);
      tn_cat(w, 100, L" − ");
      tn_catnum(w, 100, D);
      tn_cat(w, 100, L")/2");
      if (wcsstr(j->o->op, L"чернов")) tn_cat(w, 100, L", припуск под чистовую не вычтен");
      nm_putv(&j->c, L"h", h, NMO_CALC, w);
    } else {
      nm_req(j, L"h", L"припуск h на сторону (или Ø заготовки)");
      return NM_NONE;
    }
  }
  if (!(h > 0)) {
    nm_line(j, L"припуск h = 0 — точить нечего");
    return NM_NONE;
  }
  double t = nm_v(&j->c, L"t");
  if (nm_na(t)) {
    if (nm_look_pref(j, L"глубина", L"t", 0, &t)) {
      nm_putv(&j->c, L"t", t, NMO_TAB, L"таблица глубины");
    } else {
      t = h;
      nm_putv(&j->c, L"t", t, NMO_DEF, L"весь припуск за один проход");
    }
  }
  long i = t > 0 ? nm_ceil(h / t) : 1;
  if (i < 1) i = 1;
  nm_putv(&j->c, L"i", (double)i, NMO_CALC, NULL);
  s[0] = 0;
  nm_from(&j->c, L"D", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"L", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"h", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"t", L" мм", s, 300);
  tn_cat(s, 300, L" → проходов i = ");
  tn_catnum(s, 300, (double)i);
  nm_line(j, s);
  double S, n, Dn = !nm_na(Dz) && Dz > D && !fine ? Dz : D;
  if (!nm_mode(j, Dn, &S, &n)) return NM_NONE;
  double l1 = nm_l1(j), to = (L + l1) * (double)i / (n * S);
  tn_cpy(s, 300, L"То = (L + l1)·i/(n·S) = (");
  tn_catnum(s, 300, L), tn_cat(s, 300, L" + "), tn_catnum(s, 300, l1), tn_cat(s, 300, L")·");
  tn_catnum(s, 300, (double)i), tn_cat(s, 300, L"/("), tn_catnum(s, 300, n), tn_cat(s, 300, L"·");
  tn_catnum(s, 300, S), tn_cat(s, 300, L") = "), tn_catnum(s, 300, to), tn_cat(s, 300, L" мин");
  nm_line(j, s);
  return to;
}

static double nm_to_drill(NmJob *j) {
  wchar_t s[300];
  if (nm_na(nm_v(&j->c, L"l")) && !nm_na(nm_v(&j->c, L"толщина")))
    nm_putv(&j->c, L"l", nm_v(&j->c, L"толщина"), NMO_CALC, L"сквозные — толщина листа");
  int okd = nm_req(j, L"d", L"диаметр отверстия d");
  int okl = nm_req(j, L"l", L"глубина отверстия");
  if (!okd || !okl) return NM_NONE;
  if (nm_na(nm_v(&j->c, L"отверстий"))) nm_putv(&j->c, L"отверстий", 1, NMO_DEF, NULL);
  double d = nm_v(&j->c, L"d"), l = nm_v(&j->c, L"l"), k = nm_v(&j->c, L"отверстий");
  s[0] = 0;
  nm_from(&j->c, L"отверстий", NULL, s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"d", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"l", L" мм", s, 300);
  nm_line(j, s);
  double S, n;
  if (!nm_mode(j, d, &S, &n)) return NM_NONE;
  double l1 = nm_l1(j), to = k * (l + l1) / (n * S);
  tn_cpy(s, 300, L"То = отверстий·(l + l1)/(n·S) = ");
  tn_catnum(s, 300, k), tn_cat(s, 300, L"·("), tn_catnum(s, 300, l), tn_cat(s, 300, L" + ");
  tn_catnum(s, 300, l1), tn_cat(s, 300, L")/("), tn_catnum(s, 300, n), tn_cat(s, 300, L"·");
  tn_catnum(s, 300, S), tn_cat(s, 300, L") = "), tn_catnum(s, 300, to), tn_cat(s, 300, L" мин");
  nm_line(j, s);
  return to;
}

static double nm_to_mill(NmJob *j) {
  wchar_t s[300];
  int okL = nm_req(j, L"L", L"длина фрезерования L");
  int okB = nm_req(j, L"B", L"ширина фрезерования B");
  if (!okL || !okB) return NM_NONE;
  double L = nm_v(&j->c, L"L"), h = nm_v(&j->c, L"h");
  if (nm_na(h)) {
    if (!nm_look_pref(j, L"припуск", L"h", 1, &h)) {
      nm_need(j, L"припуск h");
      return NM_NONE;
    }
    nm_putv(&j->c, L"h", h, NMO_TAB, L"таблица припусков");
  }
  double t = nm_v(&j->c, L"t");
  if (nm_na(t)) {
    if (nm_look_pref(j, L"глубина", L"t", 0, &t)) {
      nm_putv(&j->c, L"t", t, NMO_TAB, L"таблица глубины");
    } else {
      t = h;
      nm_putv(&j->c, L"t", t, NMO_DEF, L"весь припуск за один проход");
    }
  }
  long i = t > 0 ? nm_ceil(h / t) : 1;
  if (i < 1) i = 1;
  nm_putv(&j->c, L"i", (double)i, NMO_CALC, NULL);
  s[0] = 0;
  nm_from(&j->c, L"L", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"B", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"h", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"t", L" мм", s, 300);
  tn_cat(s, 300, L" → проходов i = ");
  tn_catnum(s, 300, (double)i);
  nm_line(j, s);
  double Sm = nm_v(&j->c, L"Sм");
  if (!nm_na(Sm)) {
    s[0] = 0;
    nm_from(&j->c, L"Sм", L" мм/мин", s, 300);
    nm_line(j, s);
  } else {
    if (!nm_look_pref(j, L"подача", L"Sм", 1, &Sm)) return NM_NONE;
    tn_cpy(s, 300, L"Sм по станку ");
    tn_catnum(s, 300, Sm);
    Sm = nm_machine_step(j, L"ряд Sм", L"минутных подач", Sm, s, 300);
    nm_line(j, s);
  }
  if (!(Sm > 0)) return NM_NONE;
  double l1 = nm_l1(j), to = (L + l1) * (double)i / Sm;
  tn_cpy(s, 300, L"То = (L + l1)·i/Sм = (");
  tn_catnum(s, 300, L), tn_cat(s, 300, L" + "), tn_catnum(s, 300, l1), tn_cat(s, 300, L")·");
  tn_catnum(s, 300, (double)i), tn_cat(s, 300, L"/"), tn_catnum(s, 300, Sm), tn_cat(s, 300, L" = ");
  tn_catnum(s, 300, to), tn_cat(s, 300, L" мин");
  nm_line(j, s);
  return to;
}

/* марка с чертежа → материал норм резки: «45» → «ст45», «Ст3сп» → «ст3» */
static const wchar_t *nm_cut_mat(const wchar_t *grade) {
  wchar_t g[48], t[52];
  if (!grade || !grade[0]) return NULL;
  for (int pass = 0; pass < 2; pass++) {
    tn_cpy(g, 48, grade);
    if (pass) { /* без степени раскисления: Ст3сп, 08кп */
      size_t n = wcslen(g);
      if (n > 2 && (nm_eq(g + n - 2, L"сп") || nm_eq(g + n - 2, L"пс") || nm_eq(g + n - 2, L"кп"))) g[n - 2] = 0;
      else continue;
    }
    for (int i = 0; i < CUT_NMATS; i++)
      if (nm_eq(kCutMaterials[i], g)) return kCutMaterials[i];
    const wchar_t *body = nm_pre(g, L"ст") ? g + 2 : g;
    tn_cpy(t, 52, L"ст");
    tn_cat(t, 52, body);
    for (int i = 0; i < CUT_NMATS; i++)
      if (nm_eq(kCutMaterials[i], t)) return kCutMaterials[i];
  }
  return NULL;
}

static double nm_to_cut(NmJob *j) {
  wchar_t s[300];
  int okP = nm_req(j, L"периметр", L"длина реза (контур и отверстия), мм");
  int okT = nm_req(j, L"толщина", L"толщина листа");
  const NmPar *gp = nm_parc(&j->c, L"марка");
  const wchar_t *mat = nm_cut_mat(gp ? gp->s : NULL);
  if (!mat) {
    tn_cpy(s, 300, L"нет норм резки для марки «");
    tn_cat(s, 300, gp ? gp->s : L"");
    tn_cat(s, 300, L"» (нормы резки завода)");
    nm_line(j, s);
    nm_need(j, L"марка материала из норм резки");
  }
  if (!okP || !okT || !mat) return NM_NONE;
  double P = nm_v(&j->c, L"периметр"), th = nm_v(&j->c, L"толщина");
  CutPack *pk = (CutPack *)malloc(sizeof(CutPack));
  int m = -1;
  if (pk && cut_calc(mat, th, P, pk))
    for (int i = 0; i < pk->n && m < 0; i++)
      if (!cut_na(pk->m[i].num[CN_SPEED]) && pk->m[i].num[CN_SPEED] > 0) m = i;
  if (m < 0) {
    free(pk);
    tn_cpy(s, 300, L"нет скорости резки: ");
    tn_cat(s, 300, mat);
    tn_cat(s, 300, L" S=");
    tn_catnum(s, 300, th);
    nm_line(j, s);
    return NM_NONE;
  }
  CutMethod cmv = pk->m[m];
  const CutMethod *cm = &cmv;
  free(pk);
  double v = cm->num[CN_SPEED];
  j->res->cutUsed = 1;
  s[0] = 0;
  nm_from(&j->c, L"периметр", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"толщина", L" мм", s, 300);
  nm_line(j, s);
  tn_cpy(s, 300, L"v ");
  tn_catnum(s, 300, v);
  tn_cat(s, 300, L" м/мин — нормы резки завода: ");
  tn_cat(s, 300, cm->kind);
  tn_cat(s, 300, L", ");
  tn_cat(s, 300, mat);
  if (cm->mode == 0 || cm->mode == 1) tn_cat(s, 300, L" S="), tn_catnum(s, 300, cm->from);
  else tn_cat(s, 300, L" между S="), tn_catnum(s, 300, cm->from), tn_cat(s, 300, L" и "), tn_catnum(s, 300, cm->to);
  if (cm->mode == 2) tn_cat(s, 300, L" (вне таблицы — по крайним)");
  nm_line(j, s);
  double to = P / 1000 / v, tp = 0, k = nm_v(&j->c, L"пробивок");
  if (nm_na(k)) {
    double holes = nm_v(&j->c, L"отверстий");
    k = (nm_na(holes) ? 0 : holes) + 1;
    nm_putv(&j->c, L"пробивок", k, NMO_CALC, L"отверстия + наружный контур");
  }
  int pierce = nm_look(j, L"резка.пробивка", L"tпроб", 0, &tp);
  tn_cpy(s, 300, L"То = периметр/v");
  if (pierce) tn_cat(s, 300, L" + пробивок·tпроб");
  tn_cat(s, 300, L" = ");
  tn_catnum(s, 300, P / 1000), tn_cat(s, 300, L"/"), tn_catnum(s, 300, v);
  if (pierce) tn_cat(s, 300, L" + "), tn_catnum(s, 300, k), tn_cat(s, 300, L"·"), tn_catnum(s, 300, tp), to += k * tp;
  tn_cat(s, 300, L" = ");
  tn_catnum(s, 300, to);
  tn_cat(s, 300, L" мин");
  if (!pierce) tn_cat(s, 300, L" (пробивки не учтены — нет таблицы «резка.пробивка»)");
  nm_line(j, s);
  return to;
}

/* приёмы, связанные с переходом, но не вошедшие в комплекс «время на проход»: смена резца,
   изменение подачи и оборотов; перемещение каретки на длину сверх учтённой в комплексе */
static double nm_priems(NmJob *j) {
  static const struct {
    int kind;
    const wchar_t *code, *count;
  } k[] = {{NMK_TURN, L"изменение оборотов", L"изменений оборотов"},
           {NMK_TURN, L"изменение подачи", L"изменений подачи"},
           {NMK_TURN, L"смена резца поворотом", L"смен резца"}};
  wchar_t p[64], s[300], lab[80];
  nm_purp(j, L"приемы", p, 64);
  if (!nm_has(j->b, p)) return 0;
  double sum = 0, t;
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
    double n = nm_v(&j->c, k[i].count);
    if (k[i].kind != j->o->kind || nm_na(n) || n <= 0) continue;
    nm_put(&j->c, L"прием", k[i].code, NM_NONE, NMO_CALC, NULL);
    tn_cpy(lab, 80, L"Тприем «");
    tn_cat(lab, 80, k[i].code);
    tn_cat(lab, 80, L"»");
    if (nm_look(j, p, lab, 0, &t)) {
      sum += t * n;
      tn_cpy(s, 300, L"   × ");
      nm_from(&j->c, k[i].count, NULL, s, 300);
      nm_line(j, s);
    }
  }
  /* комплекс «время на проход» учитывает ход каретки до 200 мм (у станков I–II группы — до 50) */
  double L = nm_v(&j->c, L"L"), g = nm_v(&j->c, L"размерная группа"), pass = nm_v(&j->c, L"i");
  if (j->o->kind == NMK_TURN && !nm_na(L) && L > (!nm_na(g) && g <= 2 ? 50 : 200)) {
    nm_put(&j->c, L"прием", L"перемещение продольное", NM_NONE, NMO_CALC, NULL);
    nm_putv(&j->c, L"длина перемещения", L, NMO_CALC, L"длина обработки L");
    if (nm_look(j, p, L"Тприем «перемещение продольное»", 0, &t)) {
      if (nm_na(pass) || pass < 1) pass = 1;
      sum += t * pass;
      tn_cpy(s, 300, L"   × проходов ");
      tn_catnum(s, 300, pass);
      nm_line(j, s);
    }
  }
  return sum;
}

/* одна операция: То, Тв, Топ, Тшт, Тпз */
static void nm_op_calc(NmJob *j) {
  NmOp *o = j->o;
  wchar_t s[300];
  o->to = o->tv = o->top = o->tsht = o->tpz = o->tshk = NM_NONE;
  j->pref = kNmKind[o->kind];
  const NmPar *given = nm_parc(&j->c, L"То");
  if (given && given->org == NMO_ASK && !nm_na(given->v)) { /* То посчитано технологом (САМ, режимы) */
    o->to = given->v;
    s[0] = 0;
    nm_from(&j->c, L"То", L" мин", s, 300);
    nm_line(j, s);
  } else {
    o->to = o->kind == NMK_TURN    ? nm_to_turn(j)
            : o->kind == NMK_DRILL ? nm_to_drill(j)
            : o->kind == NMK_MILL  ? nm_to_mill(j)
                                   : nm_to_cut(j);
    nm_putv(&j->c, L"То", o->to, NMO_CALC, NULL);
  }
  if (!j->b->n) return; /* таблиц нет: у резки — только То по нормам резки завода */
  /* вспомогательное */
  double tus = 0, tper = 0, tizm = 0, k;
  int okTv = 1;
  if (o->kind == NMK_CUT) {
    if (!nm_look_pref(j, L"установка", L"Тус", 0, &tus)) tus = 0;
  } else {
    okTv &= nm_look_pref(j, L"установка", L"Тус", 1, &tus);
    if (nm_look_pref(j, L"переход", L"Тпер", 1, &tper)) {
      k = nm_v(&j->c, L"переходов");
      tper *= k;
      tn_cpy(s, 300, L"   × ");
      nm_from(&j->c, L"переходов", NULL, s, 300);
      nm_line(j, s);
    } else {
      okTv = 0;
    }
    tper += nm_priems(j);
    if (nm_look_pref(j, L"измерение", L"Тизм", 0, &tizm)) {
      tizm *= nm_coefs(j, L"измерение.к", L"Kпер (периодичность)") * nm_v(&j->c, L"измерений");
      tn_cpy(s, 300, L"   × ");
      nm_from(&j->c, L"измерений", NULL, s, 300);
      nm_line(j, s);
    }
  }
  o->tv = tus + tper + tizm;
  nm_putv(&j->c, L"Тв", o->tv, NMO_CALC, NULL);
  /* Kтв — по серийности: продолжительность обработки партии в сменах (ОМН, карта 1). Тшт ещё
     нет, поэтому смены — оценкой (То + Тв)·партия/480; без партии Kтв не берётся */
  double ktv = 1, to0 = nm_na(o->to) ? 0 : o->to, q = nm_v(&j->c, L"партия");
  if (nm_na(nm_v(&j->c, L"смен")) && !nm_na(q) && q > 0) {
    double sm = (to0 + o->tv) * q / 480;
    nm_putv(&j->c, L"смен", sm, NMO_CALC, L"(То + Тв)·партия/480 — оценка");
    tn_cpy(s, 300, L"смен на партию = (То + Тв)·партия/480 = (");
    tn_catnum(s, 300, to0), tn_cat(s, 300, L" + "), tn_catnum(s, 300, o->tv), tn_cat(s, 300, L")·");
    tn_catnum(s, 300, q), tn_cat(s, 300, L"/480 = "), tn_catnum(s, 300, sm);
    nm_line(j, s);
  }
  wchar_t kp[64];
  nm_purp(j, L"ктв", kp, 64);
  if (o->kind == NMK_CUT || !(o->tv > 0)) {
    /* Kтв — из нормативов станочных работ; у резки его нет, без Тв поправлять нечего */
  } else if (nm_na(nm_v(&j->c, L"смен")) && (nm_has(j->b, kp) || nm_has(j->b, L"ктв"))) {
    nm_line(j, L"Kтв не взят — нужна партия: коэффициент зависит от продолжительности обработки партии");
    nm_need(j, L"партия");
  } else if (!nm_look(j, kp, L"Kтв", 0, &ktv) && !nm_look(j, L"ктв", L"Kтв", 0, &ktv)) {
    ktv = 1;
  }
  double top = to0 + o->tv * ktv;
  nm_putv(&j->c, L"Топ", top, NMO_CALC, NULL);
  tn_cpy(s, 300, L"Топ = То + Тв");
  if (ktv != 1) tn_cat(s, 300, L"·Kтв");
  tn_cat(s, 300, L" = ");
  if (nm_na(o->to)) tn_cat(s, 300, L"(нет То) ");
  else tn_catnum(s, 300, o->to), tn_cat(s, 300, L" + ");
  tn_catnum(s, 300, o->tv);
  if (ktv != 1) tn_cat(s, 300, L"·"), tn_catnum(s, 300, ktv);
  tn_cat(s, 300, L" = ");
  tn_catnum(s, 300, top);
  tn_cat(s, 300, L" мин");
  nm_line(j, s);
  /* обслуживание и отдых, % от оперативного */
  double aobs = 0, aotl = 0;
  int okObs = nm_look_pref(j, L"обслуживание", L"аобс", 1, &aobs);
  if (!nm_look_pref(j, L"отдых", L"аотл", 0, &aotl) && !nm_look(j, L"отдых", L"аотл", 0, &aotl)) aotl = 0;
  double tsht = top * (1 + (aobs + aotl) / 100);
  tn_cpy(s, 300, L"Тшт = Топ·(1 + (аобс + аотл)/100) = ");
  tn_catnum(s, 300, top), tn_cat(s, 300, L"·(1 + ("), tn_catnum(s, 300, aobs), tn_cat(s, 300, L" + ");
  tn_catnum(s, 300, aotl), tn_cat(s, 300, L")/100) = "), tn_catnum(s, 300, tsht), tn_cat(s, 300, L" мин");
  nm_line(j, s);
  o->top = top;
  o->tsht = tsht;
  o->full = !nm_na(o->to) && okTv && okObs;
  /* подготовительно-заключительное */
  double pz = 0, pzx = 0;
  if (nm_look_pref(j, L"пз", L"Тпз", o->kind != NMK_CUT, &pz)) {
    /* дополнительное Тпз — по каждому коду из «доп»: «резьба; люнет» */
    const NmPar *dp = nm_parc(&j->c, L"доп");
    wchar_t list[64], one[64], lab[80];
    tn_cpy(list, 64, dp ? dp->s : L"");
    for (const wchar_t *p = list; *p;) {
      const wchar_t *e = wcschr(p, L';');
      int n = e ? (int)(e - p) : (int)wcslen(p);
      if (n > 63) n = 63;
      wcsncpy(one, p, (size_t)n);
      one[n] = 0;
      p = e ? e + 1 : p + n;
      const wchar_t *w = nm_skipsp(one);
      if (!*w) continue;
      NmCtx keep = j->c;
      nm_put(&j->c, L"доп", w, NM_NONE, NMO_ASK, NULL);
      tn_cpy(lab, 80, L"Тпз доп. («");
      tn_cat(lab, 80, w);
      tn_cat(lab, 80, L"»)");
      if (nm_look_pref(j, L"пз.доп", lab, 0, &pzx)) pz += pzx;
      j->c = keep;
    }
    o->tpz = pz;
  }
  if (!nm_na(o->tpz) && !nm_na(q) && q > 0 && o->full) {
    o->tshk = o->tsht + o->tpz / q;
    tn_cpy(s, 300, L"Тшк = Тшт + Тпз/партия = ");
    tn_catnum(s, 300, o->tsht), tn_cat(s, 300, L" + "), tn_catnum(s, 300, o->tpz), tn_cat(s, 300, L"/");
    tn_catnum(s, 300, q), tn_cat(s, 300, L" = "), tn_catnum(s, 300, o->tshk), tn_cat(s, 300, L" мин");
    nm_line(j, s);
  }
}

/* ---- вход: маршрут, штамп, вопрос ------------------------------------------------- */

static int nm_kind(const wchar_t *op) {
  if (nm_pre(op, L"Токарная (резьба)") || nm_pre(op, L"Токарная (накатка)")) return NMK_NONE;
  if (nm_pre(op, L"Токарная")) return NMK_TURN;
  if (nm_pre(op, L"Сверлильная")) return NMK_DRILL;
  if (nm_pre(op, L"Фрезерная")) return NMK_MILL;
  if (nm_pre(op, L"Заготовительная (раскрой)")) return NMK_CUT;
  return NMK_NONE;
}

/* «в центрах» → «центры» — коды способов установки в таблицах */
static void nm_way_code(const wchar_t *s, wchar_t *out, int cap) {
  static const struct {
    const wchar_t *stem, *code;
  } k[] = {{L"центр", L"центры"},   {L"патрон", L"патрон"}, {L"тиск", L"тиски"},         {L"стол", L"стол"},
           {L"оправк", L"оправка"}, {L"кондуктор", L"кондуктор"}, {L"приспособ", L"приспособление"}};
  wchar_t buf[64];
  const wchar_t *p = nm_skipsp(s), *q;
  if ((q = nm_pre(p, L"в ")) || (q = nm_pre(p, L"на "))) p = q;
  tn_cpy(buf, 64, p);
  /* первое слово — к коду, остальное как есть: «патроне пневматический» → «патрон пневматический» */
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
    if (nm_pre(buf, k[i].stem)) {
      const wchar_t *rest = buf;
      while (*rest && *rest != L' ') rest++;
      tn_cpy(out, cap, k[i].code);
      tn_cat(out, cap, rest);
      return;
    }
  tn_cpy(out, cap, buf);
}

/* числа и слова из вопроса: «норма: L 120, партия 50, станок 16К20, в центрах» */
static void nm_ask(const wchar_t *text, NmCtx *c) {
  static const struct {
    const wchar_t *w, *par;
    int word, exact; /* word: 1 — значение одним словом, 2 — до запятой; exact — с учётом регистра */
  } k[] = {{L"длина реза", L"периметр", 0, 0}, {L"периметр", L"периметр", 0, 0}, {L"контур", L"периметр", 0, 0},
           {L"минутная подача", L"Sм", 0, 0},  {L"Dзаг", L"Dзаг", 0, 1},      {L"Dф", L"Dф", 0, 1},
           {L"длина", L"L", 0, 0},             {L"ширина", L"B", 0, 0},       {L"припуск", L"h", 0, 0},
           {L"глубина резания", L"t", 0, 0},   {L"глубина", L"l", 0, 0},      {L"подача", L"S", 0, 0},
           {L"скорость", L"V", 0, 0},          {L"партия", L"партия", 0, 0},  {L"масса", L"масса", 0, 0},
           {L"пробив", L"пробивок", 0, 0},     {L"вид перехода", L"переход", 2, 0},
           {L"вид подачи", L"вид подачи", 1, 0}, {L"наладка", L"наладка", 2, 0},
           {L"квалитет", L"квалитет", 0, 0},   {L"угол", L"угол", 0, 0},      {L"То", L"То", 0, 1},
           {L"смен резца", L"смен резца", 0, 0}, {L"изменений подачи", L"изменений подачи", 0, 0},
           {L"изменений оборотов", L"изменений оборотов", 0, 0}, {L"смен", L"смен", 0, 0},
           {L"измеритель", L"измеритель", 2, 0}, {L"доп", L"доп", 2, 0},
           {L"переход", L"переходов", 0, 0},
           {L"инструмент", L"инструментов", 0, 0}, {L"измерен", L"измерений", 0, 0},
           {L"отверсти", L"отверстий", 0, 0},  {L"врезание", L"врезание", 0, 0},
           {L"толщина", L"толщина", 0, 0},     {L"станок", L"станок", 1, 0},  {L"способ", L"способ", 2, 0},
           {L"установка", L"способ", 2, 0},    {L"вид", L"вид", 1, 0},        {L"V", L"V", 0, 1},
           {L"L", L"L", 0, 1}, /* «S» не берём: в вопросе «S=6» — толщина листа; подача — словом */
           {L"B", L"B", 0, 1},                 {L"D", L"D", 0, 1},            {L"Ø", L"D", 0, 1},
           {L"d", L"d", 0, 1},                 {L"t", L"t", 0, 1},            {L"h", L"h", 0, 1},
           {L"z", L"z", 0, 1}};
  if (!text) return;
  for (const wchar_t *p = text; *p; p++) {
    if (p > text && (tn_isletter(p[-1]) || tn_isdig(p[-1]))) continue;
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
      const wchar_t *e;
      if (k[i].exact) {
        size_t n = wcslen(k[i].w);
        if (wcsncmp(p, k[i].w, n) || tn_isletter(p[n])) continue;
        /* одна буква — только «L 120», «L=120»: «h11», «d9» — поля допусков, не параметры */
        if (n == 1 && p[0] != L'Ø' && p[1] != L' ' && p[1] != L'=' && p[1] != L':') continue;
        e = p + n;
      } else {
        if (!(e = nm_pre(p, k[i].w))) continue;
        while (tn_isletter(*e)) e++; /* «переходов», «партией» */
      }
      while (*e == L' ' || *e == L'=' || *e == L':' || *e == L'-' || *e == L'—') e++;
      if (k[i].word) {
        wchar_t w[64];
        int n = 0;
        int list = !wcscmp(k[i].par, L"доп"); /* «доп резьба; люнет» — список до запятой */
        while (*e && *e != L',' && (list || *e != L';') && *e != L'\n' && !(k[i].word == 1 && *e == L' ') && n < 63)
          w[n++] = *e++;
        while (n && w[n - 1] == L' ') n--;
        w[n] = 0;
        if (!n) continue;
        if (!wcscmp(k[i].par, L"способ")) nm_way_code(w, w, 64);
        nm_put(c, k[i].par, w, NM_NONE, NMO_ASK, NULL);
      } else {
        double v;
        const wchar_t *e2;
        if (!nm_num(e, &v, &e2)) continue;
        nm_put(c, k[i].par, NULL, v, NMO_ASK, NULL);
        e = e2;
      }
      p = e - 1;
      break;
    }
  }
  /* «в центрах», «в патроне» без слова «способ» */
  if (!nm_parc(c, L"способ")) {
    static const wchar_t *const ways[] = {L"в центрах", L"в патроне", L"в тисках", L"на столе", L"на оправке",
                                          L"в кондукторе"};
    wchar_t *f = tn_fold(text, 0);
    for (size_t i = 0; f && i < sizeof(ways) / sizeof(ways[0]); i++)
      if (wcsstr(f, ways[i])) {
        wchar_t w[32];
        nm_way_code(ways[i], w, 32);
        nm_put(c, L"способ", w, NM_NONE, NMO_ASK, NULL);
        break;
      }
    free(f);
  }
}

static const wchar_t *nm_mat_code(int m) {
  switch (m) {
  case TNM_STEEL: case TNM_STEEL_PLAIN: case TNM_STEEL_ALLOY: case TNM_STEEL_TOOL: case TNM_STEEL_CAST: return L"сталь";
  case TNM_STEEL_STAIN: return L"нержавеющая";
  case TNM_IRON: return L"чугун";
  case TNM_ALU: case TNM_ALU_CAST: return L"алюминий";
  case TNM_COPPER: return L"медь";
  case TNM_BRASS: return L"латунь";
  case TNM_BRONZE: return L"бронза";
  case TNM_PLASTIC: return L"пластмасса";
  }
  return NULL;
}

/* общее для всех операций: что прочитано на листе */
static void nm_ctx_sheet(const TnRoute *r, const wchar_t *mass, NmCtx *c) {
  const TnSigns *s = &r->s;
  double v;
  const wchar_t *e;
  if (mass && nm_num(mass, &v, &e) && v > 0) nm_putv(c, L"масса", v, NMO_DRAW, L"штамп");
  const wchar_t *mc = nm_mat_code(s->mat);
  if (mc) nm_put(c, L"материал", mc, NM_NONE, NMO_DRAW, NULL);
  if (s->grade[0]) nm_put(c, L"марка", s->grade, NM_NONE, NMO_DRAW, NULL);
  if (s->dmax) nm_putv(c, L"D", s->dmax, NMO_DRAW, L"наибольший Ø с чертежа");
  const wchar_t *kr = nm_pre(r->blank, L"Круг ");
  if (kr && nm_num(kr, &v, &e)) nm_putv(c, L"Dзаг", v, NMO_CALC, L"заготовка — круг");
  if (s->thick) nm_putv(c, L"толщина", s->thick, NMO_DRAW, NULL);
  if (s->holes) nm_putv(c, L"отверстий", s->holes, NMO_DRAW, NULL);
  if (s->holeMin) nm_putv(c, L"d", s->holeMin, NMO_DRAW, L"наименьший Ø из «n отв.»");
  if (s->itMin) nm_putv(c, L"квалитет", s->itMin, NMO_DRAW, NULL);
  if (s->raMin) nm_putv(c, L"Ra", s->raMin, NMO_DRAW, NULL);
}

/* то, чего на листе нет, — по умолчанию (в разборе так и написано) */
static void nm_ctx_defaults(const NmBase *b, const TnRoute *r, NmOp *o, NmCtx *c) {
  const wchar_t *k = kNmKind[o->kind];
  if (!nm_parc(c, L"станок") && o->kind != NMK_CUT) {
    const wchar_t *m = nm_rec_first(b, L"станки", L"группа", k);
    if (m) nm_put(c, L"станок", m, NM_NONE, NMO_DEF, L"первый станок группы в перечне");
  }
  const NmPar *m = nm_parc(c, L"станок");
  const NmTab *t = NULL;
  const wchar_t *sz = m ? nm_rec(b, L"станки", m->s, L"размер", &t) : NULL;
  double v;
  const wchar_t *e;
  if (sz && nm_num(sz, &v, &e)) nm_putv(c, L"размер станка", v, NMO_TAB, L"перечень станков");
  const wchar_t *gr = m ? nm_rec(b, L"станки", m->s, L"размерная группа", NULL) : NULL;
  if (gr && nm_num(gr, &v, &e)) nm_putv(c, L"размерная группа", v, NMO_TAB, L"перечень станков");
  /* ключи карт ОМН, которых на листе нет: вид перехода и наладки, подача, размер измерения */
  static const struct {
    int kind;
    const wchar_t *per, *nal;
  } kDef[] = {{NMK_TURN, L"точение по лимбу", L"цилиндрические"},
              {NMK_DRILL, L"сверление по разметке", L"отверстия"},
              {NMK_MILL, L"фрезерование по лимбу", L"плоскости"}};
  for (size_t i = 0; i < sizeof(kDef) / sizeof(kDef[0]); i++)
    if (kDef[i].kind == o->kind) {
      nm_put(c, L"переход", kDef[i].per, NM_NONE, NMO_DEF, L"вид перехода");
      nm_put(c, L"наладка", kDef[i].nal, NM_NONE, NMO_DEF, L"вид наладки");
    }
  nm_put(c, L"вид подачи", L"механическая", NM_NONE, NMO_DEF, NULL);
  nm_putv(c, L"деталей", 1, NMO_DEF, L"одновременно устанавливаемых");
  nm_put(c, L"поверхность", wcsstr(r->blank, L"Отливка") ? L"отливка" : L"прокат", NM_NONE, NMO_CALC,
         L"по заготовке");
  if (o->kind == NMK_TURN) {
    nm_putv(c, L"угол", 90, NMO_DEF, L"резец в плане, проходной упорный");
    if (!nm_na(nm_v(c, L"D"))) nm_putv(c, L"И", nm_v(c, L"D"), NMO_CALC, L"измеряемый размер — Ø обработки");
  }
  if (o->kind == NMK_DRILL) {
    if (!nm_na(nm_v(c, L"d"))) nm_putv(c, L"И", nm_v(c, L"d"), NMO_CALC, L"измеряемый размер — Ø отверстия");
    nm_putv(c, L"перемещение", 0, NMO_DEF, L"вертикально-сверлильный: инструмент не перемещается");
    nm_put(c, L"отверстие", L"сквозное", NM_NONE, NMO_DEF, NULL);
  }
  /* вид — для таблиц режимов с черновой и чистовой; у одной операции «начисто» его нет */
  if (wcsstr(o->op, L"чистов") || wcsstr(o->op, L"тонкая")) nm_put(c, L"вид", L"чистовая", NM_NONE, NMO_CALC, NULL);
  else if (wcsstr(o->op, L"чернов")) nm_put(c, L"вид", L"черновая", NM_NONE, NMO_CALC, NULL);
  if (!nm_parc(c, L"способ")) {
    double D = nm_v(c, L"D"), L = nm_v(c, L"L");
    if (o->kind == NMK_TURN) {
      int center = !nm_na(D) && !nm_na(L) && D > 0 && L / D > 5;
      nm_put(c, L"способ", center ? L"центры" : L"патрон", NM_NONE, NMO_DEF, center ? L"L/D > 5" : NULL);
    } else if (o->kind == NMK_DRILL || o->kind == NMK_MILL) {
      nm_put(c, L"способ", r->cls == TN_BODY ? L"стол" : L"тиски", NM_NONE, NMO_DEF, NULL);
    }
  }
  /* переходы и инструменты — оценка по виду операции */
  double per = 1, ins = 1;
  const wchar_t *why = L"оценка";
  if (o->kind == NMK_TURN) per = 3, ins = 2, why = L"оценка: 2 торца и поверхность";
  if (o->kind == NMK_DRILL) {
    double h = nm_v(c, L"отверстий");
    per = nm_na(h) || h < 1 ? 1 : h, why = L"по числу отверстий";
  }
  nm_putv(c, L"переходов", per, NMO_DEF, why);
  nm_putv(c, L"инструментов", ins, NMO_DEF, L"оценка");
  nm_putv(c, L"измерений", per, NMO_DEF, L"по числу переходов");
  if (o->kind != NMK_TURN) return;
  /* приёмы при переходе — оценка: каждый следующий резец, смена режима на половине переходов */
  per = nm_v(c, L"переходов"), ins = nm_v(c, L"инструментов");
  nm_putv(c, L"смен резца", ins > 1 ? ins - 1 : 0, NMO_DEF, L"оценка: каждый следующий резец");
  double half = (double)(long)((per + 1) / 2);
  nm_putv(c, L"изменений подачи", half, NMO_DEF, L"оценка: на половине переходов");
  nm_putv(c, L"изменений оборотов", half, NMO_DEF, L"оценка: на половине переходов");
  /* контрольные измерения: инструмент по квалитету, размер — Ø, длина — L; периодичность — по способу */
  double it = nm_v(c, L"квалитет");
  nm_put(c, L"измеритель", !nm_na(it) && it <= 9 ? L"скоба" : L"штангенциркуль", NM_NONE, NMO_DEF,
         L"по квалитету: до IT9 — скоба, грубее — штангенциркуль");
  nm_put(c, L"отсчёт", L"0,1", NM_NONE, NMO_DEF, L"штангенциркуль 0,1 мм");
  nm_put(c, L"поверхность измерения", L"цилиндрические", NM_NONE, NMO_DEF, NULL);
  double Lm = nm_v(c, L"L"), Dm = nm_v(c, L"D");
  if (!nm_na(Lm)) nm_putv(c, L"длина измерения", Lm, NMO_CALC, L"длина обработки L");
  if (!nm_na(Lm) && !nm_na(Dm) && Dm > 0) nm_putv(c, L"длина в D", Lm / Dm, NMO_CALC, L"L/D — для калибров");
  const NmPar *pp = nm_parc(c, L"переход");
  nm_put(c, L"способ размера", pp && wcsstr(pp->s, L"на размер") ? L"на размер" : L"лимб", NM_NONE, NMO_CALC,
         L"по виду перехода");
  if (r->s.thrOut > 0) nm_put(c, L"доп", L"резьба", NM_NONE, NMO_DRAW, L"на чертеже наружная резьба");
}

/* нормы всех операций маршрута; ask — вопрос пользователя (может быть NULL) */
static void nm_route(const NmBase *b, const TnRoute *r, const wchar_t *mass, const wchar_t *ask, NmResult *res) {
  memset(res, 0, sizeof(*res));
  NmJob *j = (NmJob *)malloc(sizeof(NmJob));
  if (!j) return;
  for (int i = 0; i < r->n && res->n < 12; i++) {
    int kind = nm_kind(r->op[i].op);
    if (!kind || (!b->n && kind != NMK_CUT)) { /* без таблиц станочную не посчитать */
      if (b->n && nm_pre(r->op[i].op, L"Токарная (")) tn_item(res->skip, 400, r->op[i].op);
      continue;
    }
    NmOp *o = &res->op[res->n++];
    memset(o, 0, sizeof(*o));
    o->kind = kind;
    o->idx = i;
    tn_cpy(o->op, 48, r->op[i].op);
    memset(j, 0, sizeof(*j));
    j->b = b;
    j->o = o;
    j->res = res;
    nm_ask(ask, &j->c);
    nm_ctx_sheet(r, mass, &j->c);
    nm_ctx_defaults(b, r, o, &j->c);
    nm_op_calc(j);
  }
  free(j);
}

/* ---- текст ----------------------------------------------------------------------- */

static void nm_catmin(wchar_t *out, int cap, double v) {
  tn_catnum(out, cap, v);
  tn_cat(out, cap, L" мин");
}

static void nm_text(const NmBase *b, const NmResult *res, const wchar_t *dir, wchar_t *out, int cap) {
  tn_cat(out, cap, L"\n\nНормы времени — считает код по таблицам нормативов, проверяет технолог");
  if (!b->n) {
    tn_cat(out, cap, L"\nТаблиц нормативов ещё нет");
    if (dir && dir[0]) tn_cat(out, cap, L" (папка "), tn_cat(out, cap, dir), tn_cat(out, cap, L")");
    tn_cat(out, cap, res->n ? L": у резки — только То по нормам резки завода." : L".");
  } else if (!res->n) {
    tn_cat(out, cap, L"\nВ маршруте нет операций, которые умеем нормировать (токарная, сверлильная, фрезерная, резка).");
  }
  for (int i = 0; i < res->n; i++) {
    const NmOp *o = &res->op[i];
    wchar_t num[8];
    int v = (o->idx + 1) * 5;
    num[0] = L'0' + v / 100, num[1] = L'0' + v / 10 % 10, num[2] = L'0' + v % 10, num[3] = 0;
    tn_cat(out, cap, L"\n");
    tn_cat(out, cap, num);
    tn_cat(out, cap, L" ");
    tn_cat(out, cap, o->op);
    tn_cat(out, cap, L": ");
    if (nm_na(o->tsht)) { /* таблиц нет вовсе — только То резки */
      tn_cat(out, cap, L"То ");
      if (nm_na(o->to)) tn_cat(out, cap, L"—");
      else nm_catmin(out, cap, o->to);
    } else {
      tn_cat(out, cap, L"Тпз ");
      if (nm_na(o->tpz)) tn_cat(out, cap, L"—");
      else nm_catmin(out, cap, o->tpz);
      tn_cat(out, cap, L" · Тшт ");
      if (o->full) {
        nm_catmin(out, cap, o->tsht);
        if (!nm_na(o->tshk)) tn_cat(out, cap, L" · Тшк "), nm_catmin(out, cap, o->tshk);
      } else if (nm_na(o->to)) {
        tn_cat(out, cap, L"— (нет То");
        if (o->tsht > 0) tn_cat(out, cap, L"; без него ≥ "), nm_catmin(out, cap, o->tsht);
        tn_cat(out, cap, L")");
      } else {
        tn_cat(out, cap, L"≈ ");
        nm_catmin(out, cap, o->tsht);
        tn_cat(out, cap, L" (неполная)");
      }
    }
    tn_cat(out, cap, o->how);
  }
  if (res->skip[0]) tn_cat(out, cap, L"\nПока не нормируются: "), tn_cat(out, cap, res->skip);
  if (res->miss[0]) tn_cat(out, cap, L"\nНет таблиц: "), tn_cat(out, cap, res->miss);
  if (res->nused) {
    tn_cat(out, cap, L"\nИсточники:");
    for (int i = 0; i < res->nused; i++) {
      const NmTab *t = res->used[i];
      wchar_t w[80];
      nm_where(t, NULL, w, 80);
      tn_cat(out, cap, L"\n • ");
      if (wcscmp(w, t->file)) tn_cat(out, cap, w), tn_cat(out, cap, L" — "); /* без карты — имя файла ниже */
      tn_cat(out, cap, t->purpose);
      if (t->src && t->src[0]) tn_cat(out, cap, L" — "), tn_cat(out, cap, t->src);
      if (t->url && t->url[0]) tn_cat(out, cap, L" — "), tn_cat(out, cap, t->url);
      tn_cat(out, cap, L" (");
      tn_cat(out, cap, t->file);
      tn_cat(out, cap, t->isChecked ? L", сверено" : L", не сверено");
      const wchar_t *when = t->isChecked ? nm_skipsp(nm_skipsp(t->checked) + 2) : L"";
      if (*when) tn_cat(out, cap, L" "), tn_cat(out, cap, when);
      tn_cat(out, cap, L")");
    }
  }
  if (res->cutUsed) tn_cat(out, cap, L"\n • скорость резки — нормы резки завода (окно «Расчёт резки и газов»)");
  if (b->err[0]) tn_cat(out, cap, L"\nНе прочитаны: "), tn_cat(out, cap, b->err);
  if (res->need[0]) {
    tn_cat(out, cap, L"\nДля нормы уточнить: ");
    tn_cat(out, cap, res->need);
    tn_cat(out, cap, L". Напишите в чате «норма:» и значения, например: «норма: L 120, глубина 20, партия 50, станок 16К20».");
  }
}

/* ---- Windows: папка нормативов (только в сборке CursorPad, где подключён windows.h) ---- */
#ifdef _WINDOWS_

/* Документы\CursorPad\Нормативы — вне AppData: туда кладут и правят руками, в Excel */
static void nm_dir(wchar_t *out) {
  out[0] = 0;
#ifdef SET_TEST
  const wchar_t *test = _wgetenv(L"CP_NORMS"); /* проверочная сборка — своя папка, не Документы */
  if (test && test[0]) {
    lstrcpynW(out, test, MAX_PATH);
    return;
  }
#endif
  wchar_t docs[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docs))) {
    _snwprintf(out, MAX_PATH, L"%s\\CursorPad\\Нормативы", docs);
    out[MAX_PATH - 1] = 0;
  }
}

/* файл → текст: UTF-16 с BOM («Текст Юникод» Excel) или UTF-8 */
static wchar_t *nm_read_file(const wchar_t *path) {
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return NULL;
  DWORD n = GetFileSize(h, NULL), rd = 0;
  char *raw = n != INVALID_FILE_SIZE && n < 4 * 1024 * 1024 ? (char *)malloc(n + 2) : NULL;
  BOOL ok = raw && ReadFile(h, raw, n, &rd, NULL) && rd == n;
  CloseHandle(h);
  if (!ok) return free(raw), NULL;
  raw[n] = raw[n + 1] = 0;
  wchar_t *w = NULL;
  if (n >= 2 && (BYTE)raw[0] == 0xFF && (BYTE)raw[1] == 0xFE) {
    w = (wchar_t *)malloc(n + 2);
    if (w) memcpy(w, raw + 2, n - 2), w[(n - 2) / 2] = 0;
  } else {
    int skip = n >= 3 && (BYTE)raw[0] == 0xEF && (BYTE)raw[1] == 0xBB && (BYTE)raw[2] == 0xBF ? 3 : 0;
    int m = MultiByteToWideChar(CP_UTF8, 0, raw + skip, (int)n - skip, NULL, 0);
    w = (wchar_t *)malloc(((size_t)m + 1) * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, raw + skip, (int)n - skip, w, m), w[m] = 0;
  }
  free(raw);
  return w;
}

/* все *.tsv и *.txt папки; FALSE — папки нет (нормирование не включено) */
static BOOL nm_base_load(NmBase *b, wchar_t *dir) {
  memset(b, 0, sizeof(*b));
  nm_dir(dir);
  DWORD a = dir[0] ? GetFileAttributesW(dir) : INVALID_FILE_ATTRIBUTES;
  if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return FALSE;
  static const wchar_t *const masks[] = {L"*.tsv", L"*.txt"};
  for (int m = 0; m < 2; m++) {
    wchar_t pat[MAX_PATH], path[MAX_PATH];
    _snwprintf(pat, MAX_PATH, L"%s\\%s", dir, masks[m]);
    pat[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) continue;
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      if (!_wcsicmp(fd.cFileName, L"README.txt") || !_wcsicmp(fd.cFileName, L"ПРОЧТИ.txt")) continue;
      _snwprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
      path[MAX_PATH - 1] = 0;
      wchar_t *text = nm_read_file(path);
      if (text) nm_base_add(b, text, fd.cFileName);
      else nm_err(b, fd.cFileName, L"не открылся");
    } while (FindNextFileW(f, &fd));
    FindClose(f);
  }
  return TRUE;
}

/* текст норм к маршруту (дописать после tn_text) или NULL, если папки нормативов нет */
static wchar_t *nm_route_text(const TnRoute *r, const wchar_t *mass, const wchar_t *ask) {
  NmBase b;
  wchar_t dir[MAX_PATH];
  if (!nm_base_load(&b, dir)) return NULL;
  NmResult *res = (NmResult *)malloc(sizeof(NmResult));
  wchar_t *out = (wchar_t *)malloc(sizeof(wchar_t) * 24000);
  if (res && out) {
    out[0] = 0;
    nm_route(&b, r, mass, ask, res);
    nm_text(&b, res, dir, out, 24000);
  } else {
    free(out);
    out = NULL;
  }
  free(res);
  nm_base_free(&b);
  return out;
}

/* текст маршрута txt (malloc) + нормы; возвращает новый указатель (или тот же) */
static wchar_t *nm_append(wchar_t *txt, const TnRoute *r, const wchar_t *mass, const wchar_t *ask) {
  wchar_t *nm = txt ? nm_route_text(r, mass, ask) : NULL;
  if (!nm) return txt;
  size_t a = wcslen(txt), n = wcslen(nm);
  wchar_t *o = (wchar_t *)realloc(txt, (a + n + 1) * sizeof(wchar_t));
  if (o) memcpy(o + a, nm, (n + 1) * sizeof(wchar_t));
  free(nm);
  return o ? o : txt;
}

/* последний маршрут — чтобы в чате досчитать нормы: «норма: L 120, партия 50».
   Только в потоке окон: маршрут из рабочего потока приходит с WM_AC_TP. */
typedef struct {
  TnRoute r;
  wchar_t mass[64], ask[600]; /* масса со штампа; что уже задано в вопросах */
} NmLast;
static NmLast *g_nmLast;

static NmLast *nm_last_new(const TnRoute *r, const wchar_t *mass, const wchar_t *ask) {
  NmLast *l = (NmLast *)calloc(1, sizeof(NmLast));
  if (!l) return NULL;
  l->r = *r;
  if (mass) lstrcpynW(l->mass, mass, 64);
  if (ask) lstrcpynW(l->ask, ask, 600);
  return l;
}

static void nm_last_keep(NmLast *l) {
  if (!l) return;
  free(g_nmLast);
  g_nmLast = l;
}

/* вопрос о нормах: «норма: L 120», «нормы для партии 50», «Тшт?» — но не «норма расхода» */
static BOOL nm_ask_wanted(const wchar_t *q) {
  wchar_t *f = tn_fold(q, 0);
  if (!f) return FALSE;
  const wchar_t *p = nm_skipsp(f);
  BOOL word = (!wcsncmp(p, L"норма", 5) || !wcsncmp(p, L"нормы", 5) || !wcsncmp(p, L"норму", 5)) &&
              !tn_isletter(p[5]); /* «нормально?» — не про нормы */
  BOOL yes = (word || !wcsncmp(p, L"нормир", 6) || tn_word(f, L"тшт") || tn_word(f, L"тпз") ||
              wcsstr(f, L"нормы времени")) &&
             !wcsstr(f, L"расход");
  free(f);
  return yes;
}

/* досчитать нормы последнего маршрута с новыми значениями; текст — free() */
static wchar_t *nm_follow_text(const wchar_t *q) {
  if (!g_nmLast) return NULL;
  size_t n = wcslen(g_nmLast->ask) + wcslen(q) + 2;
  wchar_t *ask = (wchar_t *)malloc(n * sizeof(wchar_t)); /* новое — после прежнего: оно и важнее */
  if (!ask) return NULL;
  _snwprintf(ask, n, L"%s\n%s", g_nmLast->ask, q);
  ask[n - 1] = 0;
  wchar_t *t = nm_route_text(&g_nmLast->r, g_nmLast->mass, ask);
  size_t len = wcslen(ask);
  if (t) lstrcpynW(g_nmLast->ask, ask + (len > 599 ? len - 599 : 0), 600); /* длинно — старое отрезаем */
  free(ask);
  if (!t) {
    wchar_t dir[MAX_PATH];
    nm_dir(dir);
    t = (wchar_t *)malloc(sizeof(wchar_t) * (MAX_PATH + 300));
    if (t) {
      _snwprintf(t, MAX_PATH + 300,
                 L"Нормирование не включено: нет папки с таблицами нормативов %s. Создайте её и положите "
                 L"таблицы — тогда к маршруту добавятся Тпз и Тшт с разбором, откуда каждая цифра.",
                 dir);
      t[MAX_PATH + 299] = 0;
    }
  }
  return t;
}

#endif
