/* norms.c — нормы времени операций маршрута (с 2026.10.24): Тпз и Тшт токарной,
   сверлильной, фрезерной операций и резки лазером/плазмой по таблицам
   общемашиностроительных нормативов (ОМН). Норму считает код, а не модель, и у
   каждой цифры — откуда она: файл, карта, страница, позиция, столбец, или «с
   чертежа», «задано», «по умолчанию». Чего не хватило — в «Уточнить».

   Таблицы нормативов — в репозитории (native/tables/norms), встроены в exe и
   раскладываются в Документы\CursorPad\Нормативы (tables_seed.c): там их правит
   и сверяет человек, правленые обновление не затирает. Windows здесь не нужен:
     gcc -O2 -Wall -Werror -o norms_test norms_test.c && ./norms_test

   Формула (ОМН, серийное производство):
     Тв  = Тус + Тпер·переходов + Тизм·измерений
     Топ = То + Тв·Kтв
     Тшт = Топ·(1 + (аобс + аотл)/100),  Тшк = Тшт + Тпз/партия
   То — по режимам: n = 1000·V/(π·D), берётся ближайшая меньшая ступень станка;
     точение и сверление То = (L + l1)·i/(n·S), фрезерование То = (L + l1)·i/Sм,
     резка То = периметр/v + пробивок·tпроб (v — нормы резки завода, cutting_norms.h).
   Режимы, если не заданы: подача — по таблице (поправки «<оп>.подача.к»), скорость — по
   таблице «<оп>.скорость» или эмпирической формуле (коэффициенты — таблица «<оп>.скорость.ф»):
     V = Cv·D^q/(T^m·t^x·S^y·B^u·z^p)·Kмv·Kv,  Kмv = Kг·(база/σв)^nv (чугун — база/HB),
   у точения нет D, у сверления — t; T — стойкость; фрезерование Sм = Sz·z·n.
   gcc (Linux) — с -lm.

   Файл таблицы — текст, столбцы через табуляцию (UTF-8 или «Текст Юникод» из Excel):
     назначение  токарная.установка     что за таблица; несколько — через «;»
     источник    ОМН … 1974             книга
     ссылка      https://…#Par1593      где текст открыт в сети (якорь карты)
     карта / лист / страница            где в книге (у справочника — «таблица»)
     столбцы     масса, кг              по какому параметру выбирают столбец
     поля        Cv; x; y; m            или: столбцы значений — именованные поля строки
     выбор       ближайшее большее | интерполяция
     ед          мин | % | коэф | мм/об | м/мин | мм/мин | мм
     сверено     да 25.10.2026 Иванов   пока не «да», у цифры пометка «не сверено»
     поз.  способ  как в книге  до 1  до 3  до 5       — шапка таблицы
     1     патрон  В самоцентрирующем патроне  0,15  0,2  0,25
   Столбцы между «поз.» и числовыми — ключи-параметры: слово-код («патрон»; несколько
   через «;»; «*» — любой) или диапазон («до 3», «св. 3 до 5», «3–5», «св. 20»).
   Пустая клетка ключа — как в строке выше (объединённая клетка книги). Значение —
   по правилу ОМН «ближайшее большее»: столбец (строка) с наименьшей границей, не
   меньшей параметра; «—» — значения нет; «0,4–0,5» — берётся середина (так и пишется в
   разборе); «-0,9» — отрицательное (показатель степени). «вид  записи» — перечень (станки,
   материалы): первый ключ — код, дальше поля по именам.

   Параметры (имена ключей и «столбцы»): масса, способ, станок, размер станка,
   размерная группа, материал, марка, вид (черновая/чистовая), D, Dзаг, L, h (припуск
   на сторону), t, i, S, V, n, d, l (глубина отверстия), отверстий, отверстие
   (сквозное/глухое), B, Dф, z, Sм, толщина, периметр, пробивок, переход (вид
   перехода), И (измеряемый размер), квалитет, Ra, угол (резца в плане), перемещение,
   наладка, инструментов, переходов, измерений, деталей, поверхность (прокат/отливка),
   вид подачи, партия, смен (обработки партии), То, Тв, Топ; приёмы при переходе —
   прием, длина перемещения, смен резца, изменений подачи, изменений оборотов;
   измерения — измеритель, отсчёт, допуск, длина измерения, длина в D, поверхность
   измерения, способ размера; доп (дополнительное Тпз списком: «резьба; люнет»); режимы —
   инструмент (твёрдый сплав / быстрорежущая), сплав (Т15К6, ВК6…), σв, HB, T (стойкость), корка
   (без корки / прокат / поковка / отливка), r (радиус при вершине), заточка, глубина в d, фреза,
   Sz, мощность (станка). Чего нет — подходят
   строки со «*» в этом ключе. Таблицы: <оп>.установка, .переход, .измерение, .ктв, .обслуживание,
   .отдых, .пз, .пз.доп (по каждому коду «доп»), .приемы, .измерение.к (периодичность),
   .подача, .подача.к (поправки), .подача.зуб (Sz фрезы), .скорость, .скорость.ф (поля Cv q x y
   u p m), .скорость.nv (поля nv, база, Kг), .скорость.к (поправки Kv), .стойкость, .глубина,
   .припуск, .врезание; резка.пробивка; станки (поля: группа, размер, ряд n,
   ряд S, ряд Sм, мощность); материалы (поля: σв, HB). <оп> — токарная, сверлильная, фрезерная,
   резка; ктв и отдых — и без <оп>. */

#include <math.h>

#define NM_NONE (-1e300) /* нет значения */
#define NM_INF 1e300
#define NM_PI 3.14159265358979323846
#define NM_HOW 24000 /* разбор одной операции (с переходами — длинный) */
#define NM_OPS 24    /* операций маршрута с нормой */
#define NM_OUT 160000 /* текст норм маршрута (подробно — с переходами) */
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
  wchar_t one[160]; /* названия операций справочника бывают длинные (7509 — 93 знака) */
  for (const wchar_t *p = list; p && *p;) {
    const wchar_t *e = wcschr(p, L';');
    int n = e ? (int)(e - p) : (int)wcslen(p);
    if (n > 159) n = 159;
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

/* клетка «0,4–0,5»: книга даёт значение «от – до» */
static int nm_isrange(const wchar_t *s) {
  double v;
  const wchar_t *e;
  if (!nm_num(s, &v, &e)) return 0;
  e = nm_skipsp(e);
  return (*e == L'-' || *e == L'–' || *e == L'—') && nm_num(e + 1, &v, &e);
}

/* значение клетки: «0,15», «12*» (сноска), «-0,9» (показатель степени), «0,4–0,5» — середина;
   «—», «-», «х», пусто — нет */
static double nm_val(const wchar_t *s) {
  double v, w;
  const wchar_t *e;
  s = nm_skipsp(s);
  int neg = (*s == L'-' || *s == 0x2212) && tn_isdig(s[1]);
  if (!nm_num(s + neg, &v, &e)) return NM_NONE;
  const wchar_t *q = nm_skipsp(e);
  if (!neg && (*q == L'-' || *q == L'–' || *q == L'—') && nm_num(q + 1, &w, &e)) v = (v + w) / 2;
  while (*e == L'*' || *e == L' ') e++;
  return *e ? NM_NONE : neg ? -v : v;
}

/* ---- параметры расчёта ------------------------------------------------------------ */

enum { NMO_NONE, NMO_DEF, NMO_CALC, NMO_TAB, NMO_DRAW, NMO_ASK }; /* по возрастанию веса */
static const wchar_t *const kNmOrg[] = {L"", L"по умолчанию", L"расчёт", L"по таблице", L"с чертежа", L"задано"};

#define NM_PARS 80
#define NM_PARS_S 128 /* длина значения параметра */
typedef struct {
  wchar_t name[24];
  wchar_t s[NM_PARS_S]; /* текстом: код («патрон») или число как записано; названия операций — длинные */
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
  if (s) tn_cpy(p->s, NM_PARS_S, s);
  else if (!nm_na(v)) tn_numstr(v, p->s, NM_PARS_S);
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
  const wchar_t *fields; /* «поля Cv; x; y; m» — столбцы значений по именам, строку выбирают ключи */
  const wchar_t *tabl;   /* «таблица 7» — номер таблицы книги (у справочников и методичек вместо карты) */
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
          else if ((t->records && t->nkeys) || nm_eq(cell[j], L"значение") ||
                   (t->fields ? nm_list_has(t->fields, cell[j]) : !t->records && nm_rng(cell[j], &r))) {
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
      else if (nm_eq(k, L"поля")) t->fields = v;
      else if (nm_eq(k, L"таблица")) t->tabl = v;
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
    t->axis = !t->fields && !(t->ncols == 1 && t->cr[0].any);
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
  if (t->tabl && t->tabl[0]) tn_cat(out, cap, tn_isdig(t->tabl[0]) ? L"табл. " : L""), tn_cat(out, cap, t->tabl);
  if (t->card && t->card[0]) tn_cat(out, cap, out[0] ? L" к. " : L"к. "), tn_cat(out, cap, t->card);
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
  if (!t->fields && nm_isrange(r->cell[col])) {
    tn_cat(colTxt, 120, L", середина «");
    tn_cat(colTxt, 120, r->cell[col]);
    tn_cat(colTxt, 120, L"»");
  }
  if (nm_na(v) && !t->fields) {
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

/* поле найденной строки таблицы «поля …» по имени; нет столбца или «—» — NM_NONE */
static double nm_field(const NmHit *h, const wchar_t *name) {
  for (int c = 0; h->t && h->r && c < h->t->ncols; c++)
    if (nm_eq(h->t->colHead[c], name)) return h->r->v[c];
  return NM_NONE;
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

/* EST — оценка, FACT — по факту, TECH — норма технолога (Тшт по массе детали, Настройки) */
enum { NMK_NONE, NMK_TURN, NMK_DRILL, NMK_MILL, NMK_CUT, NMK_EST, NMK_FACT, NMK_TECH, NMK_SAW, NMK_GEAR, NMK_WELD };
/* класс операции — что у неё нормируется (правило технолога 30.10): мехобработка — Тпз и Тшт;
   заготовительные и вспомогательные — только Тшт (Тпз не нужен); контроль — без норм */
enum { NMC_FULL, NMC_TSHT, NMC_NONE };
static const wchar_t *const kNmKind[] = {L"", L"токарная", L"сверлильная", L"фрезерная", L"резка", L"оценка",
                                         L"по факту", L"норма технолога", L"отрезная", L"зубодолбежная", L"сварка"};

#define NM_TRS 16 /* переходов операции, расписанных по одному (токарная, фрезерная) */
typedef struct {
  wchar_t what[96]; /* «Точить Ø45 → Ø40, l 180» */
  double to, tv;    /* То и Тв перехода (Тв — время на переход и измерение, без установки) */
} NmTr;

typedef struct {
  int kind, idx;   /* idx — номер операции в маршруте (с нуля) */
  wchar_t op[128]; /* как в справочнике операций */
  wchar_t sub[24]; /* уточнение маршрута: «черновая», «чистовая», «тонкая», «резьба» */
  double to, tv, top, tsht, tpz, tshk;
  int full;        /* в Тшт есть всё: То, Тв, обслуживание */
  int cls;         /* NMC_FULL — с Тпз; NMC_TSHT — только Тшт (класс операции, Настройки) */
  wchar_t how[NM_HOW];
  NmCtx c;         /* параметры после расчёта — что взято и откуда (окно «Режимы») */
  wchar_t toNeed[200]; /* без чего не посчиталось машинное То */
  NmTr tr[NM_TRS];     /* переходы — разбивка «переход — То — Тв» в подробном ответе */
  int ntr;
} NmOp;

#define NM_USED 48
typedef struct {
  NmOp op[NM_OPS];
  int n;
  const NmTab *used[NM_USED];
  int nused;
  wchar_t need[800];  /* что уточнить — сводно */
  wchar_t miss[800];  /* каких таблиц нет вовсе */
  wchar_t skip[400];  /* какие операции не нормируются пока */
  wchar_t off[400];   /* класс «без норм» (контроль; Настройки) — одной строкой */
  int cutUsed;        /* брали скорость из норм резки завода */
} NmResult;

typedef struct {
  const NmBase *b;
  NmCtx c;
  NmOp *o;
  NmResult *res;
  const wchar_t *pref; /* «токарная» */
  int inTo;            /* считается То: нехватки — и в toNeed операции */
  const TnRoute *r;    /* маршрут и признаки листа — для переходов токарной и фрезерной */
} NmJob;

static void nm_line(NmJob *j, const wchar_t *s) {
  tn_cat(j->o->how, NM_HOW, L"\n   ");
  tn_cat(j->o->how, NM_HOW, s);
}

static void nm_need(NmJob *j, const wchar_t *what) {
  if (!tn_list_has(j->res->need, what)) tn_item(j->res->need, 800, what);
  if (j->inTo && !tn_list_has(j->o->toNeed, what)) tn_item(j->o->toNeed, 200, what);
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

/* ---- пределы и значения по умолчанию регулируемых параметров (Настройки → «Нормирование») ----
   Значение вне пределов — заданное, с чертежа, по таблице или по формуле — берётся на границе, в
   разборе строка «… → … (предел Настроек)». Значение по умолчанию — запасное: там, где своего
   источника нет (нет строки таблицы, нет партии), а у t и проходов — вместо «весь припуск за один
   проход». NM_NONE — предела или значения нет. Заводские пределы широкие: обычный расчёт не трогают. */
typedef struct {
  const wchar_t *par, *key, *label, *unit; /* параметр расчёта, ключ в norms.ini, подпись, единица */
  double min, max, def;
  double min0, max0; /* заводские */
} NmLim;

#define NM_LIMS 9
static NmLim g_nmLim[NM_LIMS] = {
    {L"V", L"V", L"Скорость резания V", L"м/мин", 5, 300, NM_NONE, 5, 300},
    {L"S", L"S", L"Подача S", L"мм/об", 0.02, 2, NM_NONE, 0.02, 2},
    {L"Sz", L"Sz", L"Подача на зуб Sz", L"мм/зуб", 0.01, 0.5, NM_NONE, 0.01, 0.5},
    {L"t", L"t", L"Глубина резания t", L"мм", 0.1, 8, NM_NONE, 0.1, 8},
    {L"i", L"i", L"Число проходов", L"", 1, 10, NM_NONE, 1, 10},
    {L"h", L"h", L"Припуск на сторону h", L"мм", 0.1, 20, NM_NONE, 0.1, 20},
    {L"n", L"n", L"Обороты шпинделя n", L"об/мин", 10, 3000, NM_NONE, 10, 3000},
    {L"T", L"T", L"Стойкость инструмента T", L"мин", 10, 240, NM_NONE, 10, 240},
    {L"партия", L"batch", L"Партия", L"шт", 1, 100000, NM_NONE, 1, 100000},
};

static NmLim *nm_lim(const wchar_t *par) {
  for (int k = 0; k < NM_LIMS; k++)
    if (!wcscmp(g_nmLim[k].par, par)) return &g_nmLim[k];
  return NULL;
}

/* «от 5 до 300 м/мин» */
static void nm_lim_text(const NmLim *l, wchar_t *out, int cap) {
  out[0] = 0;
  if (!nm_na(l->min)) tn_cat(out, cap, L"от "), tn_catnum(out, cap, l->min);
  if (!nm_na(l->max)) tn_cat(out, cap, out[0] ? L" до " : L"до "), tn_catnum(out, cap, l->max);
  if (l->unit[0] && out[0]) tn_cat(out, cap, L" "), tn_cat(out, cap, l->unit);
}

/* значение в пределах Настроек; вне — граница, строка в разборе и пометка у параметра */
static double nm_clamp(NmJob *j, const wchar_t *par, double v) {
  const NmLim *l = nm_lim(par);
  if (!l || nm_na(v)) return v;
  double c = v;
  if (!nm_na(l->min) && c < l->min) c = l->min;
  if (!nm_na(l->max) && c > l->max) c = l->max;
  if (c == v) return v;
  wchar_t s[240], lim[80];
  nm_lim_text(l, lim, 80);
  tn_cpy(s, 240, par);
  tn_cat(s, 240, L" ");
  tn_catnum(s, 240, v);
  tn_cat(s, 240, L" → ");
  tn_catnum(s, 240, c);
  tn_cat(s, 240, L" (предел Настроек «Нормирование»: ");
  tn_cat(s, 240, lim);
  tn_cat(s, 240, L")");
  nm_line(j, s);
  NmPar *p = nm_par(&j->c, par);
  if (p) {
    p->v = c;
    tn_numstr(c, p->s, NM_PARS_S);
    tn_cpy(p->why, 120, L"ограничено пределом Настроек");
  }
  return c;
}

/* значение по умолчанию из Настроек — запасное, когда своего источника нет */
static int nm_lim_def(NmJob *j, const wchar_t *par, double *v) {
  const NmLim *l = nm_lim(par);
  if (!l || nm_na(l->def)) return 0;
  *v = l->def;
  nm_putv(&j->c, par, *v, NMO_DEF, L"Настройки → Нормирование");
  wchar_t s[200];
  s[0] = 0;
  nm_from(&j->c, par, NULL, s, 200);
  nm_line(j, s);
  return 1;
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

/* «x^y» в строку разбора */
static void nm_catpow(wchar_t *s, int cap, double x, double y) {
  tn_catnum(s, cap, x);
  tn_cat(s, cap, L"^");
  tn_catnum(s, cap, y);
}

/* Kмv — поправка на обрабатываемый материал: Kг·(база/σв)^nv, у чугуна (база/HB)^nv; nv, база
   (750 МПа стали, 190 HB серого чугуна) и Kг — таблица «<оп>.скорость.nv». Нет таблицы — 1 */
static double nm_kmv(NmJob *j) {
  wchar_t p[64], s[600];
  nm_purp(j, L"скорость.nv", p, 64);
  if (!nm_has(j->b, p)) return 1;
  const NmPar *mp = nm_parc(&j->c, L"материал");
  const wchar_t *by = mp && nm_eq(mp->s, L"чугун") ? L"HB" : L"σв";
  NmHit h;
  if (!nm_find(j->b, p, &j->c, &h)) {
    tn_cpy(s, 600, L"Kмv не взят (1): ");
    tn_cat(s, 600, h.why);
    nm_line(j, s);
    if (h.need[0]) nm_need(j, h.need);
    return 1;
  }
  nm_use(j->res, h.t);
  double nv = nm_field(&h, L"nv"), base = nm_field(&h, L"база"), kg = nm_field(&h, L"Kг"), x = nm_v(&j->c, by);
  const NmPar *xp = nm_parc(&j->c, by);
  if (nm_na(nv) || nm_na(base) || nm_na(x) || x <= 0) {
    tn_cpy(s, 600, L"Kмv не взят (1): нет ");
    tn_cat(s, 600, nm_na(x) || x <= 0 ? by : L"nv или базы в строке таблицы");
    nm_line(j, s);
    return 1;
  }
  if (xp && xp->org == NMO_DEF) {
    tn_cpy(s, 600, by);
    tn_cat(s, 600, L" материала (для Kмv)");
    nm_need(j, s);
  }
  double k = (nm_na(kg) ? 1 : kg) * pow(base / x, nv);
  tn_cpy(s, 600, L"Kмv = ");
  tn_cat(s, 600, nm_na(kg) ? L"(" : L"Kг·(");
  tn_catnum(s, 600, base);
  tn_cat(s, 600, L"/");
  tn_cat(s, 600, by);
  tn_cat(s, 600, L")^nv = ");
  if (!nm_na(kg)) tn_catnum(s, 600, kg), tn_cat(s, 600, L"·");
  tn_cat(s, 600, L"(");
  tn_catnum(s, 600, base);
  tn_cat(s, 600, L"/");
  tn_catnum(s, 600, x);
  tn_cat(s, 600, L")^");
  tn_catnum(s, 600, nv);
  tn_cat(s, 600, L" = ");
  tn_catnum(s, 600, k);
  tn_cat(s, 600, L" — ");
  nm_from(&j->c, by, NULL, s, 600);
  tn_cat(s, 600, L"; ");
  tn_cat(s, 600, h.why);
  if (nm_na(kg)) tn_cat(s, 600, L"; Kг (группа стали) в таблице нет — 1");
  nm_line(j, s);
  return k;
}

/* скорость резания по эмпирической формуле V = Cv·D^q/(T^m·t^x·S^y·B^u·z^p)·Kмv·Kv; коэффициенты —
   строка таблицы «<оп>.скорость.ф» (поля Cv, q, x, y, u, p, m; нет поля — показателя нет), T —
   задана или по таблице «<оп>.стойкость». S — подача на оборот (у фрезы — на зуб, Sz) */
static int nm_vform(NmJob *j, double D, double S, double *V) {
  wchar_t p[64], s[600];
  double T = nm_v(&j->c, L"T");
  if (nm_na(T)) {
    if (nm_look_pref(j, L"стойкость", L"T (стойкость)", nm_na(nm_lim(L"T")->def), &T)) {
      nm_putv(&j->c, L"T", T, NMO_TAB, L"таблица стойкости");
    } else if (!nm_lim_def(j, L"T", &T)) {
      nm_need(j, L"T — стойкость инструмента, мин");
      return 0;
    }
  } else {
    s[0] = 0;
    nm_from(&j->c, L"T", L" мин", s, 600);
    nm_line(j, s);
  }
  T = nm_clamp(j, L"T", T);
  nm_purp(j, L"скорость.ф", p, 64);
  NmHit h;
  if (!nm_find(j->b, p, &j->c, &h)) {
    tn_cpy(s, 600, L"V по формуле не взята: ");
    tn_cat(s, 600, h.why);
    nm_line(j, s);
    if (h.need[0]) nm_need(j, h.need);
    return 0;
  }
  nm_use(j->res, h.t);
  static const wchar_t *const fn[] = {L"Cv", L"q", L"x", L"y", L"u", L"p", L"m"};
  static const wchar_t *const par[] = {NULL, NULL, L"t", NULL, L"B", L"z", NULL};
  static const wchar_t *const what[] = {NULL, NULL, L"глубина резания t", NULL, L"ширина фрезерования B",
                                        L"число зубьев фрезы z", NULL};
  double f[7];
  tn_cpy(s, 600, L"коэффициенты формулы:");
  for (int i = 0; i < 7; i++) {
    f[i] = nm_field(&h, fn[i]);
    if (nm_na(f[i])) {
      if (i == 0 || i == 6) {
        tn_cpy(s, 600, L"V по формуле не взята: в строке нет ");
        tn_cat(s, 600, fn[i]);
        nm_line(j, s);
        return 0;
      }
      f[i] = 0;
      continue;
    }
    if (i && f[i] == 0) continue;
    tn_cat(s, 600, i > 0 ? L", " : L" ");
    tn_cat(s, 600, fn[i]);
    tn_cat(s, 600, L" ");
    tn_catnum(s, 600, f[i]);
  }
  tn_cat(s, 600, L" — ");
  tn_cat(s, 600, h.why);
  nm_line(j, s);
  double x[7] = {0, D, nm_v(&j->c, L"t"), S, nm_v(&j->c, L"B"), nm_v(&j->c, L"z"), T};
  int ok = 1;
  for (int i = 1; i < 6; i++)
    if (f[i] != 0 && (nm_na(x[i]) || x[i] <= 0)) ok &= par[i] ? nm_req(j, par[i], what[i]) : 0;
  if (!ok || !(S > 0) || !(T > 0) || (f[1] != 0 && !(D > 0))) return 0;
  double kmv = nm_kmv(j), kv = nm_coefs(j, L"скорость.к", L"Kv");
  double num = f[0] * (f[1] != 0 ? pow(D, f[1]) : 1);
  double den = pow(T, f[6]) * (f[2] != 0 ? pow(x[2], f[2]) : 1) * pow(S, f[3]) * (f[4] != 0 ? pow(x[4], f[4]) : 1) *
               (f[5] != 0 ? pow(x[5], f[5]) : 1);
  *V = num / den * kmv * kv;
  tn_cpy(s, 600, L"V = Cv");
  if (f[1] != 0) tn_cat(s, 600, L"·D^q");
  tn_cat(s, 600, L"/(T^m");
  if (f[2] != 0) tn_cat(s, 600, L"·t^x");
  tn_cat(s, 600, L"·S^y");
  if (f[4] != 0) tn_cat(s, 600, L"·B^u");
  if (f[5] != 0) tn_cat(s, 600, L"·z^p");
  tn_cat(s, 600, L")·Kмv·Kv = ");
  tn_catnum(s, 600, f[0]);
  if (f[1] != 0) tn_cat(s, 600, L"·"), nm_catpow(s, 600, D, f[1]);
  tn_cat(s, 600, L"/(");
  nm_catpow(s, 600, T, f[6]);
  if (f[2] != 0) tn_cat(s, 600, L"·"), nm_catpow(s, 600, x[2], f[2]);
  tn_cat(s, 600, L"·");
  nm_catpow(s, 600, S, f[3]);
  if (f[4] != 0) tn_cat(s, 600, L"·"), nm_catpow(s, 600, x[4], f[4]);
  if (f[5] != 0) tn_cat(s, 600, L"·"), nm_catpow(s, 600, x[5], f[5]);
  tn_cat(s, 600, L")·");
  tn_catnum(s, 600, kmv);
  tn_cat(s, 600, L"·");
  tn_catnum(s, 600, kv);
  tn_cat(s, 600, L" = ");
  tn_catnum(s, 600, *V);
  tn_cat(s, 600, L" м/мин");
  nm_line(j, s);
  nm_putv(&j->c, L"V", *V, NMO_CALC, L"по формуле");
  return *V > 0;
}

/* скорость: задана, по таблице «<оп>.скорость» (с поправками) или по формуле, если есть только
   коэффициенты формулы; D — диаметр в формуле (сверла, фрезы), S — подача (у фрезы на зуб) */
static int nm_speed(NmJob *j, double D, double S, double *V) {
  wchar_t s[300], pv[64], pf[64];
  *V = nm_v(&j->c, L"V");
  if (!nm_na(*V)) {
    s[0] = 0;
    nm_from(&j->c, L"V", L" м/мин", s, 300);
    nm_line(j, s);
    *V = nm_clamp(j, L"V", *V);
    return 1;
  }
  nm_purp(j, L"скорость", pv, 64);
  nm_purp(j, L"скорость.ф", pf, 64);
  int ok;
  if (!nm_has(j->b, pv) && nm_has(j->b, pf)) {
    ok = nm_vform(j, D, S, V);
  } else {
    ok = nm_look(j, pv, L"V", nm_na(nm_lim(L"V")->def), V);
    if (ok) *V *= nm_coefs(j, L"скорость.к", L"Kv");
  }
  if (!ok) return nm_lim_def(j, L"V", V);
  *V = nm_clamp(j, L"V", *V);
  return 1;
}

/* подача S и скорость V → обороты n по диаметру D; FALSE — чего-то нет.
   Заданную подачу не трогаем, табличную — с поправками и к ближайшей меньшей ступени станка. */
static int nm_mode(NmJob *j, double D, double *S, double *n) {
  wchar_t s[300];
  double V = NM_NONE, k;
  *S = nm_v(&j->c, L"S");
  if (!nm_na(*S)) {
    s[0] = 0;
    nm_from(&j->c, L"S", L" мм/об", s, 300);
    nm_line(j, s);
    *S = nm_clamp(j, L"S", *S);
  } else if (!nm_look_pref(j, L"подача", L"S", nm_na(nm_lim(L"S")->def), S)) {
    if (!nm_lim_def(j, L"S", S)) return 0;
    *S = nm_clamp(j, L"S", *S);
  } else {
    double ks = nm_coefs(j, L"подача.к", L"Ks");
    if (ks != 1) {
      tn_cpy(s, 300, L"S = ");
      tn_catnum(s, 300, *S), tn_cat(s, 300, L"·"), tn_catnum(s, 300, ks), tn_cat(s, 300, L" = ");
      *S *= ks;
      tn_catnum(s, 300, *S);
      nm_line(j, s);
    }
    *S = nm_clamp(j, L"S", *S);
    tn_cpy(s, 300, L"S по станку ");
    tn_catnum(s, 300, *S);
    *S = nm_machine_step(j, L"ряд S", L"подач", *S, s, 300);
    nm_putv(&j->c, L"S", *S, NMO_TAB, L"по таблице и станку");
    nm_line(j, s);
  }
  if (!nm_speed(j, D, *S, &V)) return 0;
  k = 1000 * V / (NM_PI * D);
  tn_cpy(s, 300, L"n = 1000·V/(π·D) = 1000·");
  tn_catnum(s, 300, V);
  tn_cat(s, 300, L"/(π·");
  tn_catnum(s, 300, D);
  tn_cat(s, 300, L") = ");
  tn_catnum(s, 300, k);
  tn_cat(s, 300, L" об/мин");
  double kc = nm_clamp(j, L"n", k);
  if (kc != k) tn_cat(s, 300, L" → "), tn_catnum(s, 300, kc), tn_cat(s, 300, L" (предел Настроек)");
  *n = nm_machine_step(j, L"ряд n", L"оборотов", kc, s, 300);
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

/* глубина резания t и проходы i: t задана; заданы проходы — t = h/i; по таблице глубины; иначе весь
   припуск за один проход */
static long nm_passes(NmJob *j, double h) {
  double t = nm_v(&j->c, L"t"), ip = nm_v(&j->c, L"i"), d;
  if (!nm_na(ip)) ip = nm_clamp(j, L"i", ip);
  if (!nm_na(t)) {
    t = nm_clamp(j, L"t", t);
  } else if (!nm_na(ip) && ip >= 1) {
    t = h / (double)nm_ceil(ip);
    nm_putv(&j->c, L"t", t, NMO_CALC, L"припуск h/проходов");
  } else {
    if (nm_lim_def(j, L"i", &d) && d >= 1) { /* проходы по умолчанию из Настроек */
      t = h / (double)nm_ceil(d);
      nm_putv(&j->c, L"t", t, NMO_CALC, L"припуск h/проходов");
    } else if (nm_lim_def(j, L"t", &t)) {
    } else if (nm_look_pref(j, L"глубина", L"t", 0, &t)) {
      nm_putv(&j->c, L"t", t, NMO_TAB, L"таблица глубины");
    } else {
      t = h;
      nm_putv(&j->c, L"t", t, NMO_DEF, L"весь припуск за один проход");
    }
    t = nm_clamp(j, L"t", t);
  }
  long i = t > 0 ? nm_ceil(h / t) : 1;
  if (i < 1) i = 1;
  const NmLim *li = nm_lim(L"i");
  if (!nm_na(li->max) && i > (long)li->max) { /* проходов больше предела — глубже каждый, и заданная t */
    wchar_t s[200];
    tn_cpy(s, 200, L"проходов ");
    tn_catnum(s, 200, (double)i);
    i = (long)li->max;
    tn_cat(s, 200, L" → ");
    tn_catnum(s, 200, (double)i);
    tn_cat(s, 200, L" (предел Настроек «Нормирование»), t = h/проходов = ");
    tn_catnum(s, 200, h / (double)i);
    nm_line(j, s);
    NmPar *pt = nm_par(&j->c, L"t");
    if (pt) {
      pt->v = h / (double)i;
      tn_numstr(pt->v, pt->s, NM_PARS_S);
      tn_cpy(pt->why, 120, L"припуск h/проходов — предел проходов Настроек");
    }
  }
  nm_putv(&j->c, L"i", (double)i, NMO_CALC, NULL);
  return i;
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
      if (nm_look_pref(j, L"припуск", L"h", 0, &h)) {
        nm_putv(&j->c, L"h", h, NMO_TAB, L"таблица припусков");
      } else if (!nm_lim_def(j, L"h", &h)) { /* таблицы припусков нет — оценка, чтобы То было */
        h = 0.5;
        nm_putv(&j->c, L"h", h, NMO_DEF, L"оценка: припуск под чистовое точение на сторону — проверить");
      }
    } else if (!nm_na(Dz) && Dz > D) {
      h = (Dz - D) / 2;
      wchar_t w[100];
      tn_cpy(w, 100, L"(Dзаг − D)/2 = (");
      tn_catnum(w, 100, Dz);
      tn_cat(w, 100, L" − ");
      tn_catnum(w, 100, D);
      tn_cat(w, 100, L")/2");
      if (wcsstr(j->o->op, L"чернов") || !wcscmp(j->o->sub, L"черновая")) tn_cat(w, 100, L", припуск под чистовую не вычтен");
      nm_putv(&j->c, L"h", h, NMO_CALC, w);
    } else if (!nm_lim_def(j, L"h", &h)) {
      nm_req(j, L"h", L"припуск h на сторону (или Ø заготовки)");
      return NM_NONE;
    }
  }
  h = nm_clamp(j, L"h", h);
  if (!(h > 0)) {
    nm_line(j, L"припуск h = 0 — точить нечего");
    return NM_NONE;
  }
  long i = nm_passes(j, h);
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

/* фрезерование по режимам: Sz (задана или по таблице «.подача.зуб»), V (задана или по формуле
   с Ø фрезы), n = 1000·V/(π·Dф) к ступени станка, Sм = Sz·z·n к ступени станка */
static int nm_mill_mode(NmJob *j, double *Sm) {
  wchar_t s[300];
  double Dz0 = nm_v(&j->c, L"Dф");
  if (nm_na(nm_v(&j->c, L"z")) && !nm_na(Dz0) && Dz0 > 0) { /* зубья не заданы — оценка по Ø фрезы */
    double zz = (double)(long)(Dz0 / 16 + 0.5);
    nm_putv(&j->c, L"z", zz < 4 ? 4 : zz, NMO_DEF, L"оценка: торцовая фреза со вставками, шаг зубьев ~16 мм — проверить");
  }
  int okD = nm_req(j, L"Dф", L"диаметр фрезы Dф"), okz = nm_req(j, L"z", L"число зубьев фрезы z");
  if (!okD || !okz) return 0;
  double Df = nm_v(&j->c, L"Dф"), z = nm_v(&j->c, L"z"), Sz = nm_v(&j->c, L"Sz"), V, k, n;
  s[0] = 0;
  nm_from(&j->c, L"Dф", L" мм", s, 300), tn_cat(s, 300, L"; ");
  nm_from(&j->c, L"z", NULL, s, 300);
  nm_line(j, s);
  if (nm_na(Sz)) {
    if (nm_look_pref(j, L"подача.зуб", L"Sz", nm_na(nm_lim(L"Sz")->def), &Sz))
      nm_putv(&j->c, L"Sz", Sz, NMO_TAB, L"по таблице");
    else if (!nm_lim_def(j, L"Sz", &Sz))
      return 0;
  } else {
    s[0] = 0;
    nm_from(&j->c, L"Sz", L" мм/зуб", s, 300);
    nm_line(j, s);
  }
  Sz = nm_clamp(j, L"Sz", Sz);
  if (!nm_speed(j, Df, Sz, &V)) return 0;
  k = 1000 * V / (NM_PI * Df);
  tn_cpy(s, 300, L"n = 1000·V/(π·Dф) = 1000·");
  tn_catnum(s, 300, V), tn_cat(s, 300, L"/(π·"), tn_catnum(s, 300, Df), tn_cat(s, 300, L") = ");
  tn_catnum(s, 300, k), tn_cat(s, 300, L" об/мин");
  n = nm_machine_step(j, L"ряд n", L"оборотов", k, s, 300);
  nm_line(j, s);
  k = Sz * z * n;
  tn_cpy(s, 300, L"Sм = Sz·z·n = ");
  tn_catnum(s, 300, Sz), tn_cat(s, 300, L"·"), tn_catnum(s, 300, z), tn_cat(s, 300, L"·"), tn_catnum(s, 300, n);
  tn_cat(s, 300, L" = "), tn_catnum(s, 300, k), tn_cat(s, 300, L" мм/мин");
  *Sm = nm_machine_step(j, L"ряд Sм", L"минутных подач", k, s, 300);
  nm_line(j, s);
  return *Sm > 0;
}

static double nm_to_mill(NmJob *j) {
  wchar_t s[300];
  int okL = nm_req(j, L"L", L"длина фрезерования L");
  int okB = nm_req(j, L"B", L"ширина фрезерования B");
  if (!okL || !okB) return NM_NONE;
  double L = nm_v(&j->c, L"L"), h = nm_v(&j->c, L"h");
  if (nm_na(h)) {
    if (nm_look_pref(j, L"припуск", L"h", 0, &h)) {
      nm_putv(&j->c, L"h", h, NMO_TAB, L"таблица припусков");
    } else if (!nm_lim_def(j, L"h", &h)) { /* таблицы припусков нет — оценка, чтобы То было */
      h = 2;
      nm_putv(&j->c, L"h", h, NMO_DEF, L"оценка: припуск под фрезерование на сторону — проверить");
    }
  }
  h = nm_clamp(j, L"h", h);
  long i = nm_passes(j, h);
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
  } else if (!nm_has(j->b, L"фрезерная.подача") && nm_has(j->b, L"фрезерная.подача.зуб")) {
    if (!nm_mill_mode(j, &Sm)) return NM_NONE;
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
  int m = -1, any = -1;
  /* способ — по операции: «Лазерная резка» — лазер, «Плазменная» — плазма, «Газовая» — газ;
     у «Заготовительной (раскрой)» — первый, у которого в нормах есть скорость */
  wchar_t *fo = tn_fold(j->o->op, 0);
  const wchar_t *want = !fo                      ? NULL
                        : wcsstr(fo, L"лазерн")  ? L"Лазер"
                        : wcsstr(fo, L"плазмен") ? L"Плазма"
                        : wcsstr(fo, L"кислород") || wcsstr(fo, L"газов") ? L"Газовая"
                                                                          : NULL;
  free(fo);
  if (pk && cut_calc(mat, th, P, pk))
    for (int i = 0; i < pk->n && m < 0; i++)
      if (!cut_na(pk->m[i].num[CN_SPEED]) && pk->m[i].num[CN_SPEED] > 0) {
        if (any < 0) any = i;
        if (!want || (pk->m[i].kind && nm_pre(pk->m[i].kind, want))) m = i;
      }
  if (m < 0 && any >= 0) {
    m = any;
    tn_cpy(s, 300, L"в нормах резки завода нет «");
    tn_cat(s, 300, want);
    tn_cat(s, 300, L"» для этой марки и толщины — взят «");
    tn_cat(s, 300, pk->m[m].kind);
    tn_cat(s, 300, L"»; способ резки проверить");
    nm_line(j, s);
  }
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

/* приёмы, связанные с переходом, но не вошедшие в комплекс «время на проход»: смена инструмента,
   изменение подачи и оборотов; ход каретки или стола сверх учтённого в комплексе; выводы сверла */
static double nm_priem_one(NmJob *j, const wchar_t *purpose, const wchar_t *code, double n, const wchar_t *times) {
  wchar_t s[300], lab[80];
  double t;
  nm_put(&j->c, L"прием", code, NM_NONE, NMO_CALC, NULL);
  tn_cpy(lab, 80, L"Тприем «");
  tn_cat(lab, 80, code);
  tn_cat(lab, 80, L"»");
  if (!nm_look(j, purpose, lab, 0, &t)) return 0;
  s[0] = 0;
  if (times) {
    tn_cpy(s, 300, L"   × ");
    nm_from(&j->c, times, NULL, s, 300);
  } else {
    tn_cpy(s, 300, L"   × проходов ");
    tn_catnum(s, 300, n);
  }
  nm_line(j, s);
  return t * n;
}

static double nm_priems(NmJob *j) {
  static const struct {
    int kind;
    const wchar_t *code, *count;
  } k[] = {{NMK_TURN, L"изменение оборотов", L"изменений оборотов"},
           {NMK_TURN, L"изменение подачи", L"изменений подачи"},
           {NMK_TURN, L"смена резца поворотом", L"смен резца"},
           {NMK_DRILL, L"изменение режима", L"изменений режима"},
           {NMK_DRILL, L"установка инструмента", L"смен инструмента"},
           {NMK_MILL, L"изменение оборотов", L"изменений оборотов"},
           {NMK_MILL, L"изменение подачи", L"изменений подачи"}};
  wchar_t p[64], s[300];
  nm_purp(j, L"приемы", p, 64);
  double sum = 0;
  if (nm_has(j->b, p)) {
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
      double n = nm_v(&j->c, k[i].count);
      if (k[i].kind == j->o->kind && !nm_na(n) && n > 0) sum += nm_priem_one(j, p, k[i].code, n, k[i].count);
    }
    /* комплекс «время на проход» учитывает ход до 200 мм (у токарных станков I–II группы — до 50) */
    double L = nm_v(&j->c, L"L"), g = nm_v(&j->c, L"размерная группа"), pass = nm_v(&j->c, L"i");
    double lim = j->o->kind == NMK_TURN && !nm_na(g) && g <= 2 ? 50 : 200;
    const wchar_t *move = j->o->kind == NMK_TURN ? L"перемещение продольное" : j->o->kind == NMK_MILL ? L"перемещение стола" : NULL;
    if (move && !nm_na(L) && L > lim) {
      nm_putv(&j->c, L"длина перемещения", L, NMO_CALC, L"длина обработки L");
      sum += nm_priem_one(j, p, move, nm_na(pass) || pass < 1 ? 1 : pass, NULL);
    }
  }
  /* выводы сверла для удаления стружки — у глубоких отверстий, l > 3d, на каждое отверстие */
  double d = nm_v(&j->c, L"d"), l = nm_v(&j->c, L"l"), holes = nm_v(&j->c, L"отверстий"), t;
  if (j->o->kind == NMK_DRILL && !nm_na(d) && !nm_na(l) && d > 0 && l > 3 * d) {
    nm_purp(j, L"выводы", p, 64);
    nm_putv(&j->c, L"глубина в d", l / d, NMO_CALC, L"l/d");
    if (nm_look(j, p, L"Твыв (выводы сверла)", 0, &t)) {
      if (nm_na(holes) || holes < 1) holes = 1;
      sum += t * holes;
      tn_cpy(s, 300, L"   × ");
      nm_from(&j->c, L"отверстий", NULL, s, 300);
      nm_line(j, s);
    }
  }
  return sum;
}

/* ---- токарная по переходам (с 2026.10.31) ---------------------------------------------
   Каждый торец, ступень, отверстие, черновой и чистовой проход и фаски — свой переход: свои
   режимы (обороты — от своего диаметра), своё То, своё время на переход по карте 20 ОМН и своё
   измерение (карты 43, 44); То и Тв переходов складываются. Ступени — Ø со знаком с листа
   (techno.c, dia), длины ступеней — «ступени Ø30×80; Ø40×100» в вопросе, иначе оценкой: общая
   длина поровну. Деталь переворачивают на другой торец — установок две, вторая по карте
   с коэффициентом 0,8 (примечания к картам 2 и 6). Отверстие из сплошной заготовки — сверлить
   (до Ø25 — в размер, больше — Ø20) и расточить. Чистовой проход — у поверхностей с квалитетом
   до IT11, а если квалитетов на листе нет — у всех при Ra 3,2 и чище. */
enum { NMT_FACE, NMT_OD, NMT_BORE, NMT_DRILL, NMT_CHAMF, NMT_PLANE };
typedef struct {
  int type, fine, it, est, cnt; /* est — длина (у фрезерной — припуск) оценкой; cnt — отверстий или фасок */
  int side;                     /* фрезерная: 0 — плоскость, 1 — кромка вдоль L, 2 — торец вдоль B */
  double D, Dfrom, L, h;        /* Ø после и до перехода (у фрезерной D — измеряемый размер); длина прохода;
                                   припуск на сторону */
  double B;                     /* фрезерная: ширина фрезерования */
} NmTrPlan;
#define NM_HFINE 0.5 /* припуск под чистовой проход на сторону — оценка, пока нет таблицы припусков */
#define NM_HFACE 1.0 /* припуск на подрезку торца — оценка */
#define NM_DRILL0 20 /* сверло под расточку отверстия больше Ø25 — оценка */

static int nm_tr_add(NmTrPlan *pl, int n, int type, int fine, double D, double Dfrom, double L, double h, int it,
                     int est) {
  if (n >= NM_TRS) return n;
  NmTrPlan t = {type, fine, it, est, 1, 0, D, Dfrom, L, h, 0};
  pl[n] = t;
  return n + 1;
}

/* длина ступени Ød из вопроса «ступени Ø30×80; Ø40×100»; NM_NONE — не задана */
static double nm_step_len(const NmCtx *c, double d) {
  const NmPar *p = nm_parc(c, L"ступени");
  if (!p) return NM_NONE;
  for (const wchar_t *q = p->s; *q;) {
    while (*q && !tn_isdig(*q)) q++;
    double a, b;
    const wchar_t *e;
    if (!*q || !nm_num(q, &a, &e)) break;
    q = e;
    while (*q && !tn_isdig(*q) && *q != L';') q++;
    if (!*q || *q == L';' || !nm_num(q, &b, &e)) continue;
    q = e;
    if (fabs(a - d) < 0.01) return b;
  }
  return NM_NONE;
}

/* Ø по убыванию в a[], квалитеты в ai[] */
static void nm_dia_put(double *a, int *ai, int *n, int cap, double v, int it) {
  for (int i = 0; i < *n; i++)
    if (fabs(a[i] - v) < 0.01) return;
  if (*n >= cap) return;
  int k = (*n)++;
  while (k > 0 && a[k - 1] < v) a[k] = a[k - 1], ai[k] = ai[k - 1], k--;
  a[k] = v, ai[k] = it;
}

/* переходы токарной операции; 0 — расписать нечем (нет длины или диаметра — старый расчёт одной
   поверхностью покажет, чего не хватает). note — что взято оценкой */
static int nm_turn_plan(const NmJob *j, NmTrPlan *pl, int *setups, wchar_t *note, int ncap) {
  const NmCtx *c = &j->c;
  const NmOp *o = j->o;
  const TnSigns *s = j->r ? &j->r->s : NULL;
  double L = nm_v(c, L"L"), Dz = nm_v(c, L"Dзаг"), D = nm_v(c, L"D"), Din = nm_v(c, L"Dотв");
  *setups = 1;
  note[0] = 0;
  if (nm_na(L) || L <= 0) return 0;
  /* расписывать — когда Ø прочитаны с листа или ступени заданы в чате; ручной ввод «D 40, L 120»
     без листа — одной поверхностью, как задано */
  if (!(s && s->nDia) && !nm_parc(c, L"ступени") && nm_na(Din)) return 0;
  double od[6], in[3];
  int odIT[6], inIT[3], nod = 0, nin = 0;
  for (int i = 0; s && i < s->nDia; i++) {
    if ((s->diaIn >> i) & 1) nm_dia_put(in, inIT, &nin, 3, s->dia[i], s->diaIT[i]);
    else if (nm_na(Dz) || s->dia[i] <= Dz) nm_dia_put(od, odIT, &nod, 6, s->dia[i], s->diaIT[i]);
  }
  if (!nm_na(D) && nm_parc(c, L"D")->org == NMO_ASK) nod = 0, nm_dia_put(od, odIT, &nod, 6, D, 0);
  if (!nod && !nm_na(D)) nm_dia_put(od, odIT, &nod, 6, D, 0);
  if (!nm_na(Din)) nm_dia_put(in, inIT, &nin, 3, Din, 0);
  if (!nod) return 0;
  if (nm_na(Dz) || Dz < od[0]) Dz = od[0];
  int anyIT = 0;
  for (int i = 0; i < nod; i++) anyIT |= odIT[i] > 0;
  for (int i = 0; i < nin; i++) anyIT |= inIT[i] > 0;
  double ra = s ? s->raMin : 0;
  int rough = wcscmp(o->sub, L"чистовая") && wcscmp(o->sub, L"тонкая"), finish = wcscmp(o->sub, L"черновая");
  /* длины ступеней: заданные, остальное поровну */
  double len[6], known = 0;
  int unknown = 0, est = 0;
  for (int i = 0; i < nod; i++) {
    len[i] = nod == 1 ? L : nm_step_len(c, od[i]);
    if (nm_na(len[i])) unknown++;
    else known += len[i];
  }
  for (int i = 0; i < nod; i++) /* одна незаданная — остаток длины, точно; несколько — поровну, оценка */
    if (nm_na(len[i])) len[i] = L - known > 0 ? (L - known) / unknown : L / nod, est = unknown > 1 || L - known <= 0;
  if (est) tn_item(note, ncap, L"длины ступеней — оценка: общая длина поровну (впишите «ступени Ø30×80; Ø40×100»)");
  int n = 0;
  if (rough) n = nm_tr_add(pl, n, NMT_FACE, 0, Dz, Dz, Dz / 2, NM_HFACE, 0, 0);
  /* наружные: от заготовки к меньшим Ø; проход — по длине всех ступеней не больше этой */
  double prev = Dz, run = L;
  for (int k = 0; k < nod; k++) {
    int precise = (odIT[k] && odIT[k] <= 11) || (!anyIT && ra > 0 && ra <= 3.2);
    double leave = precise && rough ? NM_HFINE : 0, to = od[k] + 2 * leave, h = (prev - to) / 2;
    if (rough && h > 0.05) n = nm_tr_add(pl, n, NMT_OD, 0, to, prev, run, h, 0, est && k > 0);
    prev = to;
    run -= len[k];
    if (run < 1) run = 1;
  }
  /* чистовые — после всех черновых: у поверхностей с квалитетом */
  for (int k = 0; k < nod && finish; k++)
    if ((odIT[k] && odIT[k] <= 11) || (!anyIT && ra > 0 && ra <= 3.2))
      n = nm_tr_add(pl, n, NMT_OD, 1, od[k], od[k] + 2 * NM_HFINE, len[k], NM_NONE, odIT[k], est);
  /* отверстие — одно, самое большое; заготовка сплошная (круг) — сверлить, потом расточить */
  if (nin) {
    double b = in[0];
    int precise = (inIT[0] && inIT[0] <= 11) || (!anyIT && ra > 0 && ra <= 3.2);
    double leave = precise ? NM_HFINE : 0;
    if (rough) {
      double d0 = b <= 25 ? b - 2 * leave : NM_DRILL0;
      if (d0 < 1) d0 = b;
      n = nm_tr_add(pl, n, NMT_DRILL, 0, d0, 0, L, NM_NONE, 0, 0);
      if (b - 2 * leave - d0 > 0.1) n = nm_tr_add(pl, n, NMT_BORE, 0, b - 2 * leave, d0, L, (b - 2 * leave - d0) / 2, 0, 0);
      if (b > 25) tn_item(note, ncap, L"отверстие: сверло Ø20 под расточку — оценка");
    }
    if (finish && precise) n = nm_tr_add(pl, n, NMT_BORE, 1, b, b - 2 * NM_HFINE, L, NM_NONE, inIT[0], 0);
    if (nin > 1) tn_item(note, ncap, L"ступенчатое отверстие — считана одна ступень, проверить");
  }
  /* отверстия по оси, названные моделью («§AX 2 Ø6»): сверлить на токарной, глубина — оценкой 3d */
  if (rough && s && s->holesAx > 0) {
    const wchar_t *q = wcschr(s->ax, L'Ø');
    double d;
    const wchar_t *e;
    if (q && nm_num(q + 1, &d, &e) && d > 0 && n < NM_TRS) {
      n = nm_tr_add(pl, n, NMT_DRILL, 0, d, 0, 3 * d, NM_NONE, 0, 1);
      pl[n - 1].cnt = s->holesAx;
      tn_item(note, ncap, L"глубина отверстий по оси — оценка 3d");
    }
  }
  if (rough) n = nm_tr_add(pl, n, NMT_FACE, 0, Dz, Dz, Dz / 2, NM_HFACE, 0, 0);
  if (finish && s && s->chamfers > 0 && n < NM_TRS) {
    n = nm_tr_add(pl, n, NMT_CHAMF, 0, od[0], od[0], 0, NM_NONE, 0, 0);
    pl[n - 1].cnt = s->chamfers;
  }
  *setups = rough || nod > 1 || nin ? 2 : 1;
  return n;
}

/* вид инструмента перехода — для числа смен резца и инструментов наладки */
static int nm_tr_tool(const NmTrPlan *t) {
  return t->type == NMT_FACE || (t->type == NMT_OD && !t->fine) ? 1
         : t->type == NMT_OD                                    ? 2
         : t->type == NMT_BORE                                  ? 3 + t->fine
         : t->type == NMT_DRILL                                 ? 5
                                                                : 6;
}

static void nm_set(NmCtx *c, const wchar_t *name, const wchar_t *s, double v, const wchar_t *why) {
  NmPar *p = nm_par(c, name);
  if (p) p->org = NMO_NONE;
  nm_put(c, name, s, v, NMO_CALC, why);
}

/* То и Тв (переходы и измерения, без установки) по плану; okTv — все времена на переход нашлись */
static void nm_turn_by_plan(NmJob *j, const NmTrPlan *pl, int n, double *to, double *tv, int *okTv) {
  NmOp *o = j->o;
  wchar_t s[300], what[96];
  const NmPar *kp = nm_parc(&j->c, L"корка");
  wchar_t crust[32];
  tn_cpy(crust, 32, kp ? kp->s : L"прокат");
  double Dz = pl[0].Dfrom;
  int haveTo = 1, mainAt = -1;
  *to = 0, *tv = 0;
  o->ntr = 0;
  /* главный переход — первый черновой проход по наружному Ø: его режимы — режимы операции
     (окно «Режимы», строка «Режимы: …»); правка S или V там переходит на все переходы */
  for (int i = 0; i < n && mainAt < 0; i++)
    if (pl[i].type == NMT_OD && !pl[i].fine) mainAt = i;
  for (int i = 0; i < n && mainAt < 0; i++)
    if (pl[i].type == NMT_OD || pl[i].type == NMT_BORE) mainAt = i;
  static const wchar_t *const kMain[] = {L"S", L"V", L"n", L"t", L"i", L"T"};
  NmPar mainp[6];
  int nmain = 0;
  for (int i = 0; i < n; i++) {
    const NmTrPlan *t = &pl[i];
    NmCtx keep = j->c;
    what[0] = 0;
    if (t->type == NMT_FACE) {
      tn_cpy(what, 96, L"Подрезать торец Ø");
      tn_catnum(what, 96, t->D);
    } else if (t->type == NMT_OD || t->type == NMT_BORE) {
      tn_cpy(what, 96, t->type == NMT_OD ? (t->fine ? L"Точить начисто Ø" : L"Точить Ø") : t->fine ? L"Расточить начисто Ø" : L"Расточить Ø");
      if (!t->fine) tn_catnum(what, 96, t->Dfrom), tn_cat(what, 96, L" → Ø");
      tn_catnum(what, 96, t->D);
      if (t->it) tn_cat(what, 96, L" IT"), tn_catnum(what, 96, t->it);
      tn_cat(what, 96, L", l "), tn_catnum(what, 96, t->L);
      if (t->est) tn_cat(what, 96, L" (оценка)");
    } else if (t->type == NMT_DRILL) {
      tn_cpy(what, 96, L"Сверлить Ø");
      tn_catnum(what, 96, t->D);
      tn_cat(what, 96, L", l "), tn_catnum(what, 96, t->L);
      if (t->cnt > 1) tn_cat(what, 96, L", отверстий "), tn_catnum(what, 96, t->cnt);
    } else {
      tn_cpy(what, 96, L"Снять фаски, ");
      tn_catnum(what, 96, t->cnt);
    }
    tn_cpy(s, 300, L"— переход ");
    tn_catnum(s, 300, i + 1);
    tn_cat(s, 300, L". ");
    tn_cat(s, 300, what);
    nm_line(j, s);
    /* параметры перехода — поверх операции; после перехода всё как было */
    int fine = t->fine, it = t->it ? t->it : 12;
    nm_set(&j->c, L"вид", fine ? L"чистовая" : L"черновая", NM_NONE, NULL);
    nm_set(&j->c, L"корка", fine || (t->type != NMT_FACE && t->Dfrom < Dz) ? L"без корки" : crust, NM_NONE,
           L"по переходу");
    if (fine) nm_put(&j->c, L"r", NULL, 0.8, NMO_DEF, L"оценка: радиус при вершине пластины");
    nm_set(&j->c, L"квалитет", NULL, it, t->it ? L"с чертежа" : L"свободный размер");
    double tto = NM_NONE, tp = 0, ti = 0;
    if (t->type == NMT_DRILL) {
      nm_set(&j->c, L"d", NULL, t->D, L"переход");
      nm_set(&j->c, L"l", NULL, t->L, t->est ? L"оценка" : L"длина детали — насквозь");
      nm_set(&j->c, L"отверстий", NULL, t->cnt, NULL);
      nm_set(&j->c, L"глубина в d", NULL, t->L / t->D, L"l/d");
      nm_set(&j->c, L"инструмент", L"быстрорежущая", NM_NONE, L"сверло Р6М5");
      nm_set(&j->c, L"заточка", L"одинарная", NM_NONE, NULL);
      nm_set(&j->c, L"отверстие", L"сквозное", NM_NONE, NULL);
      const wchar_t *pref = j->pref;
      j->pref = kNmKind[NMK_DRILL]; /* подачи, скорость и врезание сверла — таблицы сверлильной */
      tto = nm_to_drill(j);
      j->pref = pref;
      if (!nm_look_pref(j, L"сверление", L"Тпер", 1, &tp)) *okTv = 0;
      nm_set(&j->c, L"измеритель", L"штангенциркуль", NM_NONE, L"отверстие под расточку");
      nm_set(&j->c, L"И", NULL, t->D, L"Ø отверстия");
    } else if (t->type == NMT_CHAMF) {
      tto = 0;
      nm_line(j, L"То фасок — доли минуты, в норму не идёт");
      nm_set(&j->c, L"переход", L"фаска", NM_NONE, NULL);
      if (nm_look_pref(j, L"переход", L"Тпер", 1, &tp)) {
        tp *= t->cnt;
        tn_cpy(s, 300, L"   × фасок ");
        tn_catnum(s, 300, t->cnt);
        nm_line(j, s);
      } else {
        *okTv = 0;
      }
    } else {
      nm_set(&j->c, L"D", NULL, t->D, L"переход");
      nm_set(&j->c, L"Dзаг", NULL, t->Dfrom, L"Ø до перехода");
      nm_set(&j->c, L"L", NULL, t->L, t->type == NMT_FACE ? L"торец: от Ø до центра" : t->est ? L"оценка" : L"переход");
      if (!nm_na(t->h)) nm_set(&j->c, L"h", NULL, t->h, t->type == NMT_FACE ? L"оценка: припуск на торец" : L"разность Ø перехода");
      /* черновой — проходами не глубже, чем берёт таблица подач у такого Ø (оценка) */
      double tmax = t->type == NMT_BORE || t->D <= 20 ? 2 : t->D <= 40 ? 3 : 5;
      if (!fine && !nm_na(t->h) && t->h > tmax) {
        wchar_t w[80];
        tn_cpy(w, 80, L"оценка: до ");
        tn_catnum(w, 80, tmax);
        tn_cat(w, 80, t->type == NMT_BORE ? L" мм за проход у расточки" : L" мм за проход у такого Ø");
        nm_set(&j->c, L"i", NULL, ceil(t->h / tmax - 1e-9), w);
      }
      tto = nm_to_turn(j);
      nm_set(&j->c, L"переход",
             t->type == NMT_FACE ? L"торец по лимбу" : fine ? L"точение с пробной стружкой" : L"точение по лимбу", NM_NONE,
             NULL);
      nm_set(&j->c, L"И", NULL, t->type == NMT_FACE ? nm_v(&keep, L"L") : t->D, t->type == NMT_FACE ? L"длина детали" : L"Ø перехода");
      if (!nm_look_pref(j, L"переход", L"Тпер", 1, &tp)) *okTv = 0;
      nm_set(&j->c, L"измеритель",
             t->type == NMT_FACE || it > 9 ? L"штангенциркуль" : t->type == NMT_BORE ? L"пробка" : L"скоба", NM_NONE,
             L"по квалитету: до IT9 — скоба (отверстие — пробка), грубее — штангенциркуль");
    }
    int lastFace = t->type == NMT_FACE;
    for (int k = i + 1; k < n && lastFace; k++) lastFace = pl[k].type != NMT_FACE;
    if (t->type == NMT_FACE && !lastFace) {
      nm_line(j, L"длину меряют один раз — после второго торца");
    } else if (t->type == NMT_FACE) { /* длина детали — один раз на деталь, периодичность не берётся */
      nm_set(&j->c, L"длина измерения", NULL, nm_v(&keep, L"L"), NULL);
      nm_look_pref(j, L"измерение", L"Тизм", 0, &ti);
    } else if (t->type != NMT_CHAMF) {
      nm_set(&j->c, L"длина измерения", NULL, t->L, NULL);
      nm_set(&j->c, L"поверхность измерения", L"цилиндрические", NM_NONE, NULL);
      if (nm_look_pref(j, L"измерение", L"Тизм", 0, &ti)) ti *= nm_coefs(j, L"измерение.к", L"Kпер (периодичность)");
    }
    if (i == mainAt)
      for (size_t k = 0; k < sizeof(kMain) / sizeof(kMain[0]); k++) {
        const NmPar *mp = nm_parc(&j->c, kMain[k]);
        if (mp && !nm_na(mp->v)) mainp[nmain++] = *mp;
      }
    if (nm_na(tto)) haveTo = 0;
    else *to += tto;
    *tv += tp + ti;
    if (o->ntr < NM_TRS) {
      NmTr *r = &o->tr[o->ntr++];
      tn_cpy(r->what, 96, what);
      r->to = tto;
      r->tv = tp + ti;
    }
    tn_cpy(s, 300, L"  → То ");
    if (nm_na(tto)) tn_cat(s, 300, L"— (нет)");
    else tn_catnum(s, 300, tto);
    tn_cat(s, 300, L" · Тв ");
    tn_catnum(s, 300, tp + ti);
    tn_cat(s, 300, L" мин");
    nm_line(j, s);
    j->c = keep;
  }
  for (int k = 0; k < nmain; k++) {
    wchar_t w[120];
    tn_cpy(w, 120, L"главный переход ");
    tn_catnum(w, 120, mainAt + 1);
    if (mainp[k].why[0]) tn_cat(w, 120, L": "), tn_cat(w, 120, mainp[k].why);
    nm_put(&j->c, mainp[k].name, mainp[k].s[0] ? mainp[k].s : NULL, mainp[k].v, mainp[k].org, w);
  }
  if (!haveTo) *to = NM_NONE;
}


/* ---- фрезерная по переходам (с 2026.10.31) ----------------------------------------------
   Плоскости (толщина из заготовки толще детали — обе, припуск пополам; толщина та же, но Ra 3,2
   и чище — обе с зачисткой 0,5), кромки вдоль длины (полоса шире детали или контур после резки
   «с припуском под фрезерование») и торцы (после пилы или резки) — каждый своим переходом:
   своя ширина фрезерования, свой припуск, своя торцовая фреза (Dф ≈ 1,375·B, не меньше 50),
   врезание и перебег по формуле 0,5·(Dф − √(Dф² − B²)) + 3 мм. Каждая сторона — своя установка
   в тисках, вторая и дальше — с коэффициентом 0,8 (примечание к карте 9). */
#define NM_HPLASMA 2.5 /* припуск на сторону контура после плазмы под фрезерование — оценка */
#define NM_HSAW 1.5    /* припуск на торец после пилы — оценка */

static int nm_mill_add(NmTrPlan *pl, int n, int side, double L, double B, double h, double D, int est) {
  n = nm_tr_add(pl, n, NMT_PLANE, 0, D, 0, L, h, 0, est);
  pl[n - 1].side = side;
  pl[n - 1].B = B;
  return n;
}

static int nm_mill_plan(const NmJob *j, NmTrPlan *pl, int *setups, wchar_t *note, int ncap) {
  const NmCtx *c = &j->c;
  const TnSigns *s = j->r ? &j->r->s : NULL;
  *setups = 1;
  note[0] = 0;
  if (!s || !s->gL || !s->gB) return 0; /* габариты не с листа — одной плоскостью, как задано */
  double L = s->gL, B = s->gB, S = s->thick ? s->thick : nm_v(c, L"толщина");
  if (B > L) {
    double t = B;
    B = L, L = t;
  }
  /* заготовка: «Полоса 22х110» — толщина и ширина, «Лист 20» — толщина */
  double Sb = NM_NONE, Bb = NM_NONE, a, b;
  const wchar_t *e, *q;
  int strip = 0;
  if ((q = nm_pre(j->r->blank, L"Полоса ")) && nm_num(q, &a, &e)) {
    strip = 1;
    while (*e && !tn_isdig(*e) && (e - q) < 40) e++;
    Sb = a;
    if (nm_num(e, &b, &e)) Sb = a < b ? a : b, Bb = a < b ? b : a;
  } else if ((q = nm_pre(j->r->blank, L"Лист ")) && nm_num(q, &a, &e)) {
    Sb = a;
  }
  int cut = 0; /* контур вырезан с припуском под фрезерование */
  for (int k = 0; k < j->r->n; k++) cut |= wcsstr(j->r->op[k].what, L"с припуском под фрезерование") != NULL;
  int n = 0;
  if (!nm_na(S) && !nm_na(Sb) && Sb - S > 0.2) {
    n = nm_mill_add(pl, n, 0, L, B, (Sb - S) / 2, S, 0);
    n = nm_mill_add(pl, n, 0, L, B, (Sb - S) / 2, S, 0);
  } else if (s->raMin && s->raMin <= 3.2) {
    n = nm_mill_add(pl, n, 0, L, B, 0.5, nm_na(S) ? B : S, 1);
    n = nm_mill_add(pl, n, 0, L, B, 0.5, nm_na(S) ? B : S, 1);
    tn_item(note, ncap, L"плоскости — зачистка 0,5 мм (Ra 3,2 и чище, толщина заготовки как у детали)");
  }
  if (!nm_na(S)) {
    if (!nm_na(Bb) && Bb - B > 0.2) {
      n = nm_mill_add(pl, n, 1, L, S, (Bb - B) / 2, B, 0);
      n = nm_mill_add(pl, n, 1, L, S, (Bb - B) / 2, B, 0);
    } else if (cut) {
      n = nm_mill_add(pl, n, 1, L, S, NM_HPLASMA, B, 1);
      n = nm_mill_add(pl, n, 1, L, S, NM_HPLASMA, B, 1);
    }
    if (cut || strip) {
      n = nm_mill_add(pl, n, 2, B, S, cut ? NM_HPLASMA : NM_HSAW, L, 1);
      n = nm_mill_add(pl, n, 2, B, S, cut ? NM_HPLASMA : NM_HSAW, L, 1);
    }
    if (cut) tn_item(note, ncap, L"припуск контура после резки — оценка 2,5 мм на сторону");
    else if (strip) tn_item(note, ncap, L"припуск на торцы после пилы — оценка 1,5 мм");
  } else if (cut || strip) {
    tn_item(note, ncap, L"толщина детали не прочитана — кромки и торцы не расписаны");
  }
  if (!n) return 0;
  *setups = n;
  return n;
}

/* То и Тв фрезерной по плану: у каждой стороны своя ширина, фреза, врезание и измерение */
static void nm_mill_by_plan(NmJob *j, const NmTrPlan *pl, int n, double *to, double *tv, int *okTv) {
  NmOp *o = j->o;
  wchar_t s[300], what[96];
  int haveTo = 1, mainAt = 0;
  *to = 0, *tv = 0;
  o->ntr = 0;
  static const wchar_t *const kMain[] = {L"Sz", L"V", L"n", L"Sм", L"t", L"i", L"Dф", L"z", L"T"};
  NmPar mainp[9];
  int nmain = 0;
  for (int i = 0; i < n; i++) {
    const NmTrPlan *t = &pl[i];
    NmCtx keep = j->c;
    tn_cpy(what, 96, t->side == 0 ? L"Фрезеровать плоскость " : t->side == 1 ? L"Фрезеровать кромку " : L"Фрезеровать торец ");
    tn_catnum(what, 96, t->L), tn_cat(what, 96, L"×"), tn_catnum(what, 96, t->B);
    tn_cat(what, 96, L", h "), tn_catnum(what, 96, t->h);
    if (t->est) tn_cat(what, 96, L" (оценка)");
    tn_cpy(s, 300, L"— переход ");
    tn_catnum(s, 300, i + 1);
    tn_cat(s, 300, L". ");
    tn_cat(s, 300, what);
    nm_line(j, s);
    nm_set(&j->c, L"L", NULL, t->L, L"переход");
    nm_set(&j->c, L"B", NULL, t->B, t->side ? L"кромка: ширина — толщина детали" : L"переход");
    nm_set(&j->c, L"h", NULL, t->h, t->est ? L"оценка" : L"заготовка толще (шире) детали, пополам");
    if (nm_parc(&keep, L"Dф") == NULL || nm_parc(&keep, L"Dф")->org < NMO_ASK) {
      double Df = ceil(1.375 * t->B);
      nm_set(&j->c, L"Dф", NULL, Df < 50 ? 50 : Df, L"оценка: (1,25–1,5)·B, не меньше 50; впишите свою фрезу");
      if (nm_parc(&keep, L"z") == NULL || nm_parc(&keep, L"z")->org < NMO_ASK) {
        NmPar *zp = nm_par(&j->c, L"z");
        if (zp) zp->v = NM_NONE, zp->org = NMO_NONE;
      }
    }
    double Df = nm_v(&j->c, L"Dф");
    if (!nm_na(Df) && Df > t->B && (nm_parc(&keep, L"врезание") == NULL || nm_parc(&keep, L"врезание")->org < NMO_ASK)) {
      double l1 = 0.5 * (Df - sqrt(Df * Df - t->B * t->B)) + 3;
      nm_set(&j->c, L"врезание", NULL, l1, L"по формуле 0,5·(Dф − √(Dф² − B²)) + 3");
      tn_cpy(s, 300, L"l1 (врезание и перебег) = 0,5·(Dф − √(Dф² − B²)) + 3 = 0,5·(");
      tn_catnum(s, 300, Df), tn_cat(s, 300, L" − √("), tn_catnum(s, 300, Df), tn_cat(s, 300, L"² − "),
          tn_catnum(s, 300, t->B), tn_cat(s, 300, L"²)) + 3 = "), tn_catnum(s, 300, l1), tn_cat(s, 300, L" мм");
      nm_line(j, s);
    }
    double tto = nm_to_mill(j), tp = 0, ti = 0;
    if (!nm_look_pref(j, L"переход", L"Тпер", 1, &tp)) *okTv = 0;
    nm_set(&j->c, L"И", NULL, t->D, t->side == 0 ? L"толщина" : t->side == 1 ? L"ширина" : L"длина");
    nm_set(&j->c, L"длина измерения", NULL, t->L, NULL);
    if (nm_look_pref(j, L"измерение", L"Тизм", 0, &ti)) ti *= nm_coefs(j, L"измерение.к", L"Kпер (периодичность)");
    if (i == mainAt)
      for (size_t k = 0; k < sizeof(kMain) / sizeof(kMain[0]); k++) {
        const NmPar *mp = nm_parc(&j->c, kMain[k]);
        if (mp && !nm_na(mp->v)) mainp[nmain++] = *mp;
      }
    if (nm_na(tto)) haveTo = 0;
    else *to += tto;
    *tv += tp + ti;
    if (o->ntr < NM_TRS) {
      NmTr *r = &o->tr[o->ntr++];
      tn_cpy(r->what, 96, what);
      r->to = tto;
      r->tv = tp + ti;
    }
    tn_cpy(s, 300, L"  → То ");
    if (nm_na(tto)) tn_cat(s, 300, L"— (нет)");
    else tn_catnum(s, 300, tto);
    tn_cat(s, 300, L" · Тв ");
    tn_catnum(s, 300, tp + ti);
    tn_cat(s, 300, L" мин");
    nm_line(j, s);
    j->c = keep;
  }
  for (int k = 0; k < nmain; k++) {
    wchar_t w[120];
    tn_cpy(w, 120, L"главный переход 1");
    if (mainp[k].why[0]) tn_cat(w, 120, L": "), tn_cat(w, 120, mainp[k].why);
    nm_put(&j->c, mainp[k].name, mainp[k].s[0] ? mainp[k].s : NULL, mainp[k].v, mainp[k].org, w);
  }
  if (!haveTo) *to = NM_NONE;
}


/* своих открытых нормативов нет — коэффициенты CursorPad, правятся в Настройках (norms.ini [saw], [gear]):
   производительность ленточной пилы, см²/мин, по группе материала (на заводе ⌀150 из 12Х18Н10Т —
   12–18 мин, отсюда нержавейка ~12; остальное — по соотношению скоростей ленты); режимы зубодолбления */
enum { NMW_CARBON, NMW_ALLOY, NMW_TOOL, NMW_STAIN, NMW_IRON, NMW_ALU, NMW_COPPER, NMW_N };
#define NM_SAW0 {30, 22, 15, 12, 35, 80, 50}
static double g_nmSaw[NMW_N] = NM_SAW0;
static const wchar_t *const kNmSawName[NMW_N] = {L"углеродистая сталь", L"легированная сталь", L"инструментальная сталь",
                                                 L"нержавеющая сталь", L"чугун", L"алюминий", L"медь, латунь, бронза"};
typedef struct {
  double V, Skr, Srad; /* м/мин; мм на двойной ход — круговая и радиальная */
} NmGear;
#define NM_GEAR0 {20, 0.25, 0.03}
static NmGear g_nmGear = NM_GEAR0;

/* ---- отрезка на пиле (с 2026.10.31) -------------------------------------------------------
   То = площадь сечения заготовки / производительность пилы по материалу (оценка CursorPad,
   Настройки); Тв — установка в тисках (карта 9, по аналогии) и переход (карта 33, поз. 118);
   обслуживание — карта 45 (поз. 88), отдых — карта 46; Тпз нет (заготовительная). Сечение — из
   заготовки (сортамент ЭСИ или маршрута): круг, квадрат, шестигранник, полоса, труба, профильная.
   Нет сечения или материала — «проверить», без числа. */

/* площадь сечения заготовки, мм²; NM_NONE — сортамент не узнан (лист — режут не пилой) */
static double nm_blank_area(const wchar_t *blank, wchar_t *why, int cap) {
  double a, b, c;
  const wchar_t *e, *q;
  why[0] = 0;
  static const wchar_t *const sorts[] = {L"Круг ", L"Квадрат ", L"Шестигранник ", L"Полоса ", L"Труба профильная ", L"Труба "};
  for (int k = 0; k < 6; k++) {
    if (!(q = nm_pre(blank, sorts[k])) || !nm_num(q, &a, &e) || a <= 0) continue;
    double v[3] = {a, 0, 0};
    int nv = 1;
    while (nv < 3) {
      while (*e == L' ') e++;
      if (*e != L'х' && *e != L'x' && *e != L'×' && *e != L'Х') break;
      e++;
      if (!nm_num(e, &v[nv], &e) || v[nv] <= 0) break;
      nv++;
    }
    b = v[1], c = v[2];
    double F = NM_NONE;
    if (k == 0) F = NM_PI * a * a / 4, tn_cpy(why, cap, L"круг: π·D²/4");
    else if (k == 1) F = a * a, tn_cpy(why, cap, L"квадрат: a²");
    else if (k == 2) F = 0.866 * a * a, tn_cpy(why, cap, L"шестигранник: 0,866·S²");
    else if (k == 3 && nv >= 2) F = a * b, tn_cpy(why, cap, L"полоса: a·b");
    else if (k == 4 && nv >= 3) F = 2 * (a + b) * c - 4 * c * c, tn_cpy(why, cap, L"профильная труба: 2·(a + b)·s − 4·s²");
    else if (k == 5 && nv >= 2 && 2 * b < a) F = NM_PI / 4 * (a * a - (a - 2 * b) * (a - 2 * b)), tn_cpy(why, cap, L"труба: π/4·(D² − (D − 2s)²)");
    if (!nm_na(F)) return F;
  }
  return NM_NONE;
}

static int nm_saw_group(int mat) {
  switch (mat) {
  case TNM_STEEL: case TNM_STEEL_PLAIN: case TNM_STEEL_CAST: return NMW_CARBON;
  case TNM_STEEL_ALLOY: return NMW_ALLOY;
  case TNM_STEEL_TOOL: return NMW_TOOL;
  case TNM_STEEL_STAIN: return NMW_STAIN;
  case TNM_IRON: return NMW_IRON;
  case TNM_ALU: case TNM_ALU_CAST: return NMW_ALU;
  case TNM_COPPER: case TNM_BRASS: case TNM_BRONZE: return NMW_COPPER;
  default: return -1;
  }
}

/* Тв, Топ, обслуживание и отдых по картам; to — машинное; okTo — То посчитано */
static void nm_omn_tail(NmJob *j, double to, double tv, int okTv, int okTo) {
  NmOp *o = j->o;
  wchar_t s[300];
  o->to = okTo ? to : NM_NONE;
  o->tv = tv;
  double top = (okTo ? to : 0) + tv, aobs = 0, aotl = 0;
  o->top = top;
  tn_cpy(s, 300, L"Топ = То + Тв = ");
  if (okTo) tn_catnum(s, 300, to), tn_cat(s, 300, L" + ");
  else tn_cat(s, 300, L"(нет То) ");
  tn_catnum(s, 300, tv), tn_cat(s, 300, L" = "), tn_catnum(s, 300, top), tn_cat(s, 300, L" мин");
  nm_line(j, s);
  nm_putv(&j->c, L"Топ", top, NMO_CALC, NULL);
  int okObs = nm_look_pref(j, L"обслуживание", L"аобс", 1, &aobs);
  if (!nm_look(j, L"отдых", L"аотл", 0, &aotl)) aotl = 0;
  o->tsht = top * (1 + (aobs + aotl) / 100);
  tn_cpy(s, 300, L"Тшт = Топ·(1 + (аобс + аотл)/100) = ");
  tn_catnum(s, 300, top), tn_cat(s, 300, L"·(1 + ("), tn_catnum(s, 300, aobs), tn_cat(s, 300, L" + ");
  tn_catnum(s, 300, aotl), tn_cat(s, 300, L")/100) = "), tn_catnum(s, 300, o->tsht), tn_cat(s, 300, L" мин");
  nm_line(j, s);
  o->full = okTo && okTv && okObs;
}

static void nm_saw(NmJob *j) {
  NmOp *o = j->o;
  wchar_t s[300], why[80];
  o->to = o->tv = o->top = o->tsht = o->tpz = o->tshk = NM_NONE;
  j->pref = kNmKind[NMK_SAW];
  const wchar_t *blank = j->r ? j->r->blank : L"";
  double F = nm_blank_area(blank, why, 80), to = NM_NONE;
  int g = j->r ? nm_saw_group(j->r->s.mat) : -1, okTo = 0;
  if (nm_na(F)) {
    nm_line(j, L"То — проверить: сечение заготовки не узнано (нужен сортамент: круг, квадрат, шестигранник, полоса, труба)");
    nm_need(j, L"сортамент заготовки");
  } else if (g < 0) {
    nm_line(j, L"То — проверить: материал заготовки не прочитан — производительность пилы не выбрать");
    nm_need(j, L"материал");
  } else {
    double P = g_nmSaw[g];
    to = F / 100 / P;
    okTo = 1;
    tn_cpy(s, 300, L"F = ");
    tn_catnum(s, 300, F / 100);
    tn_cat(s, 300, L" см² (");
    tn_cat(s, 300, why);
    tn_cat(s, 300, L", ");
    tn_cat(s, 300, blank);
    tn_cat(s, 300, L"); П = ");
    tn_catnum(s, 300, P);
    tn_cat(s, 300, L" см²/мин — ");
    tn_cat(s, 300, kNmSawName[g]);
    tn_cat(s, 300, L", оценка CursorPad (Настройки → Нормирование)");
    nm_line(j, s);
    tn_cpy(s, 300, L"То = F/П = ");
    tn_catnum(s, 300, F / 100), tn_cat(s, 300, L"/"), tn_catnum(s, 300, P), tn_cat(s, 300, L" = ");
    tn_catnum(s, 300, to), tn_cat(s, 300, L" мин");
    nm_line(j, s);
  }
  /* Тв: установка в тисках пилы по массе заготовки (масса детали — без отхода), переход */
  double tus = 0, tper = 0;
  nm_put(&j->c, L"способ", L"тиски", NM_NONE, NMO_DEF, L"заготовку зажимают в тисках пилы");
  nm_put(&j->c, L"поверхность", L"прокат", NM_NONE, NMO_DEF, NULL);
  nm_putv(&j->c, L"деталей", 1, NMO_DEF, NULL);
  nm_put(&j->c, L"вид подачи", L"механическая", NM_NONE, NMO_DEF, NULL);
  int okTv = nm_look_pref(j, L"установка", L"Тус", 1, &tus);
  okTv &= nm_look_pref(j, L"переход", L"Тпер", 1, &tper);
  nm_omn_tail(j, to, tus + tper, okTv, okTo);
}

/* ---- зубодолбление (с 2026.10.31) ---------------------------------------------------------
   То = h/(Sрад·nдв) + π·m·z·i/(Sкр·nдв): врезание на высоту зуба h = 2,25·m и обкат на i проходов
   (до m 2,5 — один, больше — два: черновой и чистовой); nдв = 1000·V/(2·(B + перебег)) — двойных ходов долбяка в минуту.
   V, Sкр, Sрад — оценка CursorPad (Настройки); перебег — приложение 2 ОМН. Тв: установка на
   оправке (карта 16), переход (карта 33), совмещение профиля (приложение 6); обслуживание —
   карта 45, отдых — 46, Тпз — карта 68 (наладка + инструмент + пробная обработка). */
static void nm_gear(NmJob *j) {
  NmOp *o = j->o;
  wchar_t s[300];
  o->to = o->tv = o->top = o->tsht = o->tpz = o->tshk = NM_NONE;
  j->pref = kNmKind[NMK_GEAR];
  if (nm_na(nm_v(&j->c, L"B")) && !nm_na(nm_v(&j->c, L"L")))
    nm_putv(&j->c, L"B", nm_v(&j->c, L"L"), NMO_DEF, L"оценка: ширина венца — длина детали");
  /* число зубьев колеса — «zк» с листа; «z» в вопросе у зубодолбёжной — тоже зубья колеса */
  if (nm_na(nm_v(&j->c, L"zк")) && nm_parc(&j->c, L"z") && nm_parc(&j->c, L"z")->org == NMO_ASK)
    nm_putv(&j->c, L"zк", nm_v(&j->c, L"z"), NMO_ASK, L"число зубьев колеса");
  int okm = nm_req(j, L"m", L"модуль зуба m"), okz = nm_req(j, L"zк", L"число зубьев колеса z"),
      okB = nm_req(j, L"B", L"ширина венца B");
  double m = nm_v(&j->c, L"m"), z = nm_v(&j->c, L"zк"), B = nm_v(&j->c, L"B"), to = NM_NONE, lp = 0;
  int okTo = 0;
  if (okm && okz && okB && m > 0 && z > 0 && B > 0) {
    if (!nm_look_pref(j, L"перебег", L"перебег долбяка", 0, &lp)) lp = 0;
    double nd = 1000 * g_nmGear.V / (2 * (B + lp)), h = 2.25 * m;
    double i = m <= 2.5 ? 1 : 2;
    to = h / (g_nmGear.Srad * nd) + NM_PI * m * z * i / (g_nmGear.Skr * nd);
    okTo = 1;
    tn_cpy(s, 300, L"V ");
    tn_catnum(s, 300, g_nmGear.V);
    tn_cat(s, 300, L" м/мин, Sкр ");
    tn_catnum(s, 300, g_nmGear.Skr);
    tn_cat(s, 300, L", Sрад ");
    tn_catnum(s, 300, g_nmGear.Srad);
    tn_cat(s, 300, L" мм/дв. ход — оценка CursorPad (Настройки → Нормирование); проходов ");
    tn_catnum(s, 300, i);
    tn_cat(s, 300, m <= 2.5 ? L" (до m 2,5)" : L" (m больше 2,5 — черновой и чистовой)");
    nm_line(j, s);
    tn_cpy(s, 300, L"nдв = 1000·V/(2·(B + перебег)) = 1000·");
    tn_catnum(s, 300, g_nmGear.V), tn_cat(s, 300, L"/(2·("), tn_catnum(s, 300, B), tn_cat(s, 300, L" + ");
    tn_catnum(s, 300, lp), tn_cat(s, 300, L")) = "), tn_catnum(s, 300, nd), tn_cat(s, 300, L" дв. ходов/мин");
    nm_line(j, s);
    tn_cpy(s, 300, L"То = 2,25·m/(Sрад·nдв) + π·m·z·i/(Sкр·nдв) = ");
    tn_catnum(s, 300, h / (g_nmGear.Srad * nd)), tn_cat(s, 300, L" + ");
    tn_catnum(s, 300, NM_PI * m * z * i / (g_nmGear.Skr * nd)), tn_cat(s, 300, L" = ");
    tn_catnum(s, 300, to), tn_cat(s, 300, L" мин");
    nm_line(j, s);
  }
  double tus = 0, tper = 0, tsov = 0;
  nm_put(&j->c, L"способ", L"оправка", NM_NONE, NMO_DEF, L"на концевой оправке с гайкой");
  nm_put(&j->c, L"вид подачи", L"механическая", NM_NONE, NMO_DEF, NULL);
  nm_put(&j->c, L"переход", L"без прорезки", NM_NONE, NMO_DEF, L"зуб без предварительной прорезки");
  int okTv = nm_look_pref(j, L"установка", L"Тус", 1, &tus);
  okTv &= nm_look_pref(j, L"переход", L"Тпер", 1, &tper);
  if (!nm_look_pref(j, L"совмещение", L"Тсовм (совместить профиль)", 0, &tsov)) tsov = 0;
  nm_omn_tail(j, to, tus + tper + tsov, okTv, okTo);
  /* Тпз: наладка + получение инструмента + пробная обработка */
  if (o->cls == NMC_FULL) {
    static const wchar_t *const parts[] = {L"наладка", L"получение", L"пробная"};
    double pz = 0, x;
    int any = 0;
    for (int k = 0; k < 3; k++) {
      NmCtx keep = j->c;
      nm_put(&j->c, L"часть", parts[k], NM_NONE, NMO_CALC, NULL);
      wchar_t lab[40];
      tn_cpy(lab, 40, L"Тпз (");
      tn_cat(lab, 40, parts[k]);
      tn_cat(lab, 40, L")");
      if (nm_look_pref(j, L"пз", lab, k == 0, &x)) pz += x, any = 1;
      j->c = keep;
    }
    if (any) o->tpz = pz;
  }
  double q = nm_v(&j->c, L"партия");
  if (!nm_na(o->tpz) && !nm_na(q) && q > 0 && o->full) o->tshk = o->tsht + o->tpz / q;
}


/* ---- сварка (с 2026.10.31) ----------------------------------------------------------------
   ОМН на дуговую сварку в среде защитных газов (1989), как в примере 1.18.1 книги:
     Тшт = [(Тнш + Тзач)·L + Тус]·K3·Kсер
   Тнш — неполное штучное на 1 м шва по катету и проволоке (карта 6: углеродистые и
   низколегированные стали, полуавтомат в CO2, нижнее положение; в нём уже основное, вспомогательное
   на шов, обслуживание и отдых), Тзач — зачистка брызг (карта 75), Тус — установка и снятие
   (карта 82), K3 — по длине одного шва (карта 90), Kсер — тип производства (п. 1.14: единичное ×1,3,
   мелкосерийное ×1,2; тип по партии — оценка CursorPad: до 5 — единичное, до 50 — мелкосерийное).
   Длина шва и катет — в чате «норма: шов 200, катет 5, швов 2»; нет — «проверить», числа нет. */
static void nm_weld(NmJob *j) {
  NmOp *o = j->o;
  wchar_t s[300];
  o->to = o->tv = o->top = o->tsht = o->tpz = o->tshk = NM_NONE;
  j->pref = kNmKind[NMK_WELD];
  int okL = nm_req(j, L"Lшва", L"длина шва, мм («шов 200»)"), okk = nm_req(j, L"k", L"катет шва («катет 5»)");
  int mat = j->r ? j->r->s.mat : 0;
  if (mat == TNM_STEEL_STAIN || mat == TNM_ALU || mat == TNM_ALU_CAST || mat == TNM_COPPER || mat == TNM_BRASS ||
      mat == TNM_BRONZE) {
    nm_line(j, L"Тшт — проверить: карты на сварку этого материала (легированные, аргон, алюминий, медь) не перенесены");
    return;
  }
  if (!okL || !okk) {
    nm_line(j, L"Тшт — проверить: нужны длина шва и катет — «норма: шов 200, катет 5»");
    return;
  }
  double L = nm_v(&j->c, L"Lшва") / 1000, cnt = nm_v(&j->c, L"швов"), q = nm_v(&j->c, L"партия");
  if (nm_na(cnt) || cnt < 1) cnt = 1;
  nm_put(&j->c, L"проволока", NULL, 1.2, NMO_DEF, L"полуавтомат, проволока Ø1,2");
  if (nm_na(nm_v(&j->c, L"толщина"))) nm_putv(&j->c, L"толщина", nm_v(&j->c, L"k"), NMO_DEF, L"толщина — как катет");
  nm_put(&j->c, L"зачистка", L"механизированная", NM_NONE, NMO_DEF, L"шлифмашинкой");
  nm_put(&j->c, L"способ", L"стол", NM_NONE, NMO_DEF, L"установка на стол вручную");
  nm_put(&j->c, L"шов", L"прямолинейный", NM_NONE, NMO_DEF, NULL);
  nm_putv(&j->c, L"длина шва", L / cnt, NMO_CALC, L"длина одного шва");
  double tn = 0, tz = 0, tus = 0, k3 = 1;
  int ok = nm_look_pref(j, L"шов", L"Тнш (на 1 м шва)", 1, &tn);
  if (!nm_look_pref(j, L"зачистка", L"Тзач (на 1 м шва)", 0, &tz)) tz = 0;
  ok &= nm_look_pref(j, L"установка", L"Тус", 1, &tus);
  if (!nm_look_pref(j, L"длина", L"K3 (длина шва)", 0, &k3)) k3 = 1;
  double kser = 1;
  const wchar_t *type = L"средне- и крупносерийное";
  if (!nm_na(q) && q <= 5) kser = 1.3, type = L"единичное";
  else if (!nm_na(q) && q <= 50) kser = 1.2, type = L"мелкосерийное";
  else if (nm_na(q)) kser = 1.2, type = L"мелкосерийное (партия не задана)";
  tn_cpy(s, 300, L"Kсер ");
  tn_catnum(s, 300, kser);
  tn_cat(s, 300, L" — производство ");
  tn_cat(s, 300, type);
  tn_cat(s, 300, L" (п. 1.14 ОМН; тип по партии — оценка CursorPad)");
  nm_line(j, s);
  o->to = tn * L;
  o->tv = tz * L + tus;
  o->top = o->to + o->tv;
  o->tsht = ((tn + tz) * L + tus) * k3 * kser;
  tn_cpy(s, 300, L"Тшт = [(Тнш + Тзач)·L + Тус]·K3·Kсер = [(");
  tn_catnum(s, 300, tn), tn_cat(s, 300, L" + "), tn_catnum(s, 300, tz), tn_cat(s, 300, L")·");
  tn_catnum(s, 300, L), tn_cat(s, 300, L" + "), tn_catnum(s, 300, tus), tn_cat(s, 300, L"]·");
  tn_catnum(s, 300, k3), tn_cat(s, 300, L"·"), tn_catnum(s, 300, kser), tn_cat(s, 300, L" = ");
  tn_catnum(s, 300, o->tsht), tn_cat(s, 300, L" мин");
  nm_line(j, s);
  o->full = ok;
}

/* одна операция: То, Тв, Топ, Тшт, Тпз */
static void nm_op_calc(NmJob *j) {
  NmOp *o = j->o;
  wchar_t s[300];
  o->to = o->tv = o->top = o->tsht = o->tpz = o->tshk = NM_NONE;
  j->pref = kNmKind[o->kind];
  /* токарная — по переходам, если есть длина и диаметр; иначе одной поверхностью (покажет, чего нет) */
  NmTrPlan plan[NM_TRS];
  wchar_t pnote[300];
  int setups = 1, np = !j->b->n                ? 0
                       : o->kind == NMK_TURN ? nm_turn_plan(j, plan, &setups, pnote, 300)
                       : o->kind == NMK_MILL ? nm_mill_plan(j, plan, &setups, pnote, 300)
                                             : 0;
  double ptv = 0;
  int pok = 1;
  const NmPar *given = nm_parc(&j->c, L"То");
  int givenTo = given && given->org == NMO_ASK && !nm_na(given->v);
  if (np) {
    tn_cpy(s, 300, L"переходов ");
    tn_catnum(s, 300, np);
    tn_cat(s, 300, L", установок ");
    tn_catnum(s, 300, setups);
    if (pnote[0]) tn_cat(s, 300, L"; оценкой: "), tn_cat(s, 300, pnote);
    nm_line(j, s);
    double pto;
    j->inTo = 1;
    if (o->kind == NMK_TURN) nm_turn_by_plan(j, plan, np, &pto, &ptv, &pok);
    else nm_mill_by_plan(j, plan, np, &pto, &ptv, &pok);
    j->inTo = 0;
    if (!givenTo) o->to = pto, nm_putv(&j->c, L"То", o->to, NMO_CALC, L"сумма переходов");
  }
  if (givenTo) { /* То посчитано технологом (САМ, режимы) */
    o->to = given->v;
    s[0] = 0;
    nm_from(&j->c, L"То", L" мин", s, 300);
    nm_line(j, s);
  } else if (np) {
    tn_cpy(s, 300, L"То = сумма переходов = ");
    if (nm_na(o->to)) tn_cat(s, 300, L"— (у перехода нет То)");
    else tn_catnum(s, 300, o->to), tn_cat(s, 300, L" мин");
    nm_line(j, s);
  } else {
    j->inTo = 1;
    o->to = o->kind == NMK_TURN    ? nm_to_turn(j)
            : o->kind == NMK_DRILL ? nm_to_drill(j)
            : o->kind == NMK_MILL  ? nm_to_mill(j)
                                   : nm_to_cut(j);
    nm_putv(&j->c, L"То", o->to, NMO_CALC, NULL);
    j->inTo = 0;
  }
  if (!j->b->n) return; /* таблиц нет: у резки — только То по нормам резки завода */
  /* вспомогательное */
  double tus = 0, tper = 0, tizm = 0, k;
  int okTv = 1;
  if (o->kind == NMK_CUT) {
    if (!nm_look_pref(j, L"установка", L"Тус", 0, &tus)) tus = 0;
  } else if (np) {
    okTv &= nm_look_pref(j, L"установка", L"Тус", 1, &tus) && pok;
    if (setups > 1) { /* переустановка (другой торец, другая сторона) — по карте с коэффициентом 0,8 */
      double one = tus;
      tus = one * (1 + 0.8 * (setups - 1));
      tn_cpy(s, 300, L"   × установок ");
      tn_catnum(s, 300, setups);
      tn_cat(s, 300, L": ");
      tn_catnum(s, 300, one);
      tn_cat(s, 300, L" + 0,8·");
      tn_catnum(s, 300, one);
      tn_cat(s, 300, L" (переустановка, примечание к карте) = ");
      tn_catnum(s, 300, tus);
      nm_line(j, s);
    }
    /* приёмы при переходе — по плану: смена резца, оборотов и подачи между переходами */
    int chg = 0, nTools = 0, rev = 0, fed = 0, seen[8] = {0};
    for (int i = 0; i < np; i++) {
      /* фрезерная: фреза своя у плоскостей и у кромок с торцами (ширина другая) */
      int tool = o->kind == NMK_MILL ? 1 + (plan[i].side > 0) : nm_tr_tool(&plan[i]);
      if (!seen[tool]) seen[tool] = 1, nTools++;
      if (!i) continue;
      int prevTool = o->kind == NMK_MILL ? 1 + (plan[i - 1].side > 0) : nm_tr_tool(&plan[i - 1]);
      chg += tool != prevTool;
      rev += o->kind == NMK_MILL ? tool != prevTool
                                 : plan[i].type != plan[i - 1].type || fabs(plan[i].Dfrom - plan[i - 1].Dfrom) > 0.1 * plan[i].Dfrom;
      fed += o->kind == NMK_MILL ? plan[i].side != plan[i - 1].side
                                 : plan[i].type != plan[i - 1].type || plan[i].fine != plan[i - 1].fine;
    }
    nm_putv(&j->c, L"смен резца", chg, NMO_CALC, L"по переходам");
    nm_putv(&j->c, L"изменений оборотов", rev, NMO_CALC, L"по переходам: другой Ø или вид");
    nm_putv(&j->c, L"изменений подачи", fed, NMO_CALC, L"по переходам: другой вид или чистовой");
    nm_putv(&j->c, L"инструментов", nTools, NMO_CALC, L"по переходам");
    nm_putv(&j->c, L"переходов", np, NMO_CALC, L"по переходам");
    tper = ptv + nm_priems(j);
    tn_cpy(s, 300, L"Тпер + Тизм по переходам = ");
    tn_catnum(s, 300, ptv);
    tn_cat(s, 300, L" мин");
    nm_line(j, s);
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
  if (nm_na(q)) nm_lim_def(j, L"партия", &q);
  q = nm_clamp(j, L"партия", q);
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
  if (o->cls == NMC_FULL && nm_look_pref(j, L"пз", L"Тпз", o->kind != NMK_CUT, &pz)) { /* только Тшт — без Тпз */
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

static int nm_kind(const wchar_t *op, const wchar_t *sub) {
  if (nm_pre(op, L"Токарная (резьба)") || nm_pre(op, L"Токарная (накатка)") || (sub && !wcscmp(sub, L"резьба")))
    return NMK_NONE;
  if (nm_pre(op, L"Токарная")) return NMK_TURN;
  if (nm_pre(op, L"Сверлильная")) return NMK_DRILL;
  if (nm_pre(op, L"Фрезерная")) return NMK_MILL;
  if (nm_pre(op, L"Термическая резка") || nm_eq(op, L"Раскрой") || nm_pre(op, L"Заготовительная (раскрой)") ||
      nm_pre(op, L"Лазерная резка") || nm_pre(op, L"Плазменная резка") || nm_pre(op, L"Газовая резка"))
    return NMK_CUT;
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
           {L"подача на зуб", L"Sz", 0, 0},    {L"Sz", L"Sz", 0, 1},          {L"диаметр фрезы", L"Dф", 0, 0},
           {L"число зубьев", L"z", 0, 0},      {L"зубьев", L"z", 0, 0},       {L"стойкость", L"T", 0, 0},
           {L"σв", L"σв", 0, 1},               {L"предел прочности", L"σв", 0, 0},
           {L"проход", L"i", 0, 0},
           {L"твердость", L"HB", 0, 0},        {L"HB", L"HB", 0, 1},          {L"НВ", L"HB", 0, 1},
           {L"мощность", L"мощность", 0, 0},   {L"радиус", L"r", 0, 0},       {L"фреза", L"фреза", 1, 0},
           {L"заточка", L"заточка", 1, 0},     {L"сплав", L"сплав", 1, 0},
           {L"длина", L"L", 0, 0},             {L"ширина", L"B", 0, 0},       {L"припуск", L"h", 0, 0},
           {L"глубина резания", L"t", 0, 0},   {L"глубина", L"l", 0, 0},      {L"подача", L"S", 0, 0},
           {L"скорость", L"V", 0, 0},          {L"партия", L"партия", 0, 0},  {L"масса", L"масса", 0, 0},
           {L"пробив", L"пробивок", 0, 0},     {L"вид перехода", L"переход", 2, 0},
           {L"вид подачи", L"вид подачи", 1, 0}, {L"наладка", L"наладка", 2, 0},
           {L"квалитет", L"квалитет", 0, 0},   {L"угол", L"угол", 0, 0},      {L"То", L"То", 0, 1},
           {L"смен резца", L"смен резца", 0, 0}, {L"изменений подачи", L"изменений подачи", 0, 0},
           {L"изменений оборотов", L"изменений оборотов", 0, 0}, {L"смен", L"смен", 0, 0},
           {L"измеритель", L"измеритель", 2, 0}, {L"доп", L"доп", 2, 0},
           {L"модуль", L"m", 0, 0},           {L"m", L"m", 0, 1},            {L"zк", L"zк", 0, 1},
           {L"ступени", L"ступени", 2, 0},
           {L"длина шва", L"Lшва", 0, 0},     {L"шов", L"Lшва", 0, 0},        {L"швов", L"швов", 0, 0},
           {L"катет", L"k", 0, 0},            {L"проволока", L"проволока", 0, 0},    {L"расточка", L"Dотв", 0, 0},  {L"Dотв", L"Dотв", 0, 1},
           {L"переход", L"переходов", 0, 0},
           {L"инструмент", L"инструментов", 0, 0}, {L"измерен", L"измерений", 0, 0},
           {L"отверсти", L"отверстий", 0, 0},  {L"врезание", L"врезание", 0, 0},
           {L"толщина", L"толщина", 0, 0},     {L"станок", L"станок", 1, 0},  {L"способ", L"способ", 2, 0},
           {L"установка", L"способ", 2, 0},    {L"вид", L"вид", 1, 0},        {L"V", L"V", 0, 1},
           {L"L", L"L", 0, 1}, /* «S» не берём: в вопросе «S=6» — толщина листа; подача — словом */
           {L"B", L"B", 0, 1},                 {L"D", L"D", 0, 1},            {L"Ø", L"D", 0, 1},
           {L"d", L"d", 0, 1},                 {L"t", L"t", 0, 1},            {L"h", L"h", 0, 1},
           {L"z", L"z", 0, 1},                 {L"T", L"T", 0, 1},            {L"Т", L"T", 0, 1},
           {L"r", L"r", 0, 1}};
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
        /* «доп резьба; люнет», «ступени Ø30×80; Ø40×100» — список до запятой */
        int list = !wcscmp(k[i].par, L"доп") || !wcscmp(k[i].par, L"ступени");
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
  /* материал режущей части: «Р6М5», «быстрорежущим», «твёрдый сплав», «Т15К6» — и марка сплава */
  if (!nm_parc(c, L"инструмент")) {
    static const struct {
      const wchar_t *w, *tool, *grade;
    } tools[] = {{L"быстрорез", L"быстрорежущая", NULL}, {L"р6м5", L"быстрорежущая", NULL},
                 {L"р18", L"быстрорежущая", NULL},       {L"т15к6", L"твёрдый сплав", L"Т15К6"},
                 {L"т5к10", L"твёрдый сплав", L"Т5К10"}, {L"вк6", L"твёрдый сплав", L"ВК6"},
                 {L"вк8", L"твёрдый сплав", L"ВК8"},     {L"твердосплав", L"твёрдый сплав", NULL},
                 {L"твердый сплав", L"твёрдый сплав", NULL}, {L"твердого сплава", L"твёрдый сплав", NULL}};
    wchar_t *f = tn_fold(text, 0);
    for (size_t i = 0; f && i < sizeof(tools) / sizeof(tools[0]); i++)
      if (wcsstr(f, tools[i].w)) {
        nm_put(c, L"инструмент", tools[i].tool, NM_NONE, NMO_ASK, NULL);
        if (tools[i].grade && !nm_parc(c, L"сплав")) nm_put(c, L"сплав", tools[i].grade, NM_NONE, NMO_ASK, NULL);
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
  /* габариты, проставленные на листе (модель третьего поколения): длина обработки — оценка по общей длине */
  if (s->gL) nm_putv(c, L"L", s->gL, NMO_DRAW, L"общая длина детали с чертежа");
  if (s->gB) nm_putv(c, L"B", s->gB, NMO_DRAW, L"ширина детали с чертежа");
  const wchar_t *kr = nm_pre(r->blank, L"Круг ");
  if (kr && nm_num(kr, &v, &e)) nm_putv(c, L"Dзаг", v, NMO_CALC, L"заготовка — круг");
  if (s->thick) nm_putv(c, L"толщина", s->thick, NMO_DRAW, NULL);
  if (s->holes) nm_putv(c, L"отверстий", s->holes, NMO_DRAW, NULL);
  if (s->holeMin) nm_putv(c, L"d", s->holeMin, NMO_DRAW, L"наименьший Ø из «n отв.»");
  if (s->itMin) nm_putv(c, L"квалитет", s->itMin, NMO_DRAW, NULL);
  if (s->raMin) nm_putv(c, L"Ra", s->raMin, NMO_DRAW, NULL);
  if (s->gearM) nm_putv(c, L"m", s->gearM, NMO_DRAW, L"таблица параметров венца");
  if (s->gearZ) nm_putv(c, L"zк", s->gearZ, NMO_DRAW, L"таблица параметров венца");
}

/* поле перечня материалов по марке — числом; why — откуда (источник перечня) */
static double nm_mat_field(const NmBase *b, const NmCtx *c, const wchar_t *field, wchar_t *why, int cap) {
  const NmPar *mk = nm_parc(c, L"марка");
  const NmTab *t = NULL;
  const wchar_t *v = mk ? nm_rec(b, L"материалы", mk->s, field, &t) : NULL;
  double x = v ? nm_val(v) : NM_NONE;
  if (!nm_na(x) && t) {
    tn_cpy(why, cap, L"перечень материалов");
    if (nm_isrange(v)) tn_cat(why, cap, L", середина «"), tn_cat(why, cap, v), tn_cat(why, cap, L"»");
    tn_cat(why, cap, L": ");
    tn_cat(why, cap, t->src ? t->src : t->file);
  }
  return x;
}

/* для режимов резания: инструмент, свойства материала, состояние поверхности, фреза */
static void nm_ctx_modes(const NmBase *b, const TnRoute *r, const NmOp *o, NmCtx *c) {
  if (o->kind != NMK_TURN && o->kind != NMK_DRILL && o->kind != NMK_MILL) return;
  const NmPar *mp = nm_parc(c, L"материал");
  int steel = mp && (nm_eq(mp->s, L"сталь") || nm_eq(mp->s, L"нержавеющая")), iron = mp && nm_eq(mp->s, L"чугун");
  int drill = o->kind == NMK_DRILL;
  nm_put(c, L"инструмент", drill ? L"быстрорежущая" : L"твёрдый сплав", NM_NONE, NMO_DEF,
         drill ? L"сверло Р6М5" : iron ? L"пластины ВК6" : L"пластины Т15К6");
  const NmPar *ip = nm_parc(c, L"инструмент");
  if (ip && nm_eq(ip->s, L"твёрдый сплав") && (steel || iron))
    nm_put(c, L"сплав", iron ? L"ВК6" : L"Т15К6", NM_NONE, NMO_DEF, iron ? L"по чугуну" : L"по стали");
  wchar_t why[120];
  double sv = nm_mat_field(b, c, L"σв", why, 120), hb;
  if (!nm_na(sv)) nm_putv(c, L"σв", sv, NMO_TAB, why);
  hb = nm_mat_field(b, c, L"HB", why, 120);
  if (!nm_na(hb)) nm_putv(c, L"HB", hb, NMO_TAB, why);
  if (steel && nm_na(nm_v(c, L"σв"))) nm_putv(c, L"σв", 750, NMO_DEF, L"марки нет в перечне — базовая прочность формулы");
  if (steel && nm_na(nm_v(c, L"HB")))
    nm_putv(c, L"HB", (double)(long)(nm_v(c, L"σв") / 3.4 + 0.5), NMO_CALC, L"оценка: σв ≈ 3,4·HB");
  if (iron && nm_na(nm_v(c, L"HB"))) nm_putv(c, L"HB", 190, NMO_DEF, L"базовая твёрдость формулы");
  /* вид: одна операция без «начисто» — снимается весь припуск */
  if (o->kind == NMK_TURN) nm_put(c, L"вид", L"черновая", NM_NONE, NMO_DEF, L"весь припуск");
  const NmPar *vp = nm_parc(c, L"вид");
  int fine = vp && nm_eq(vp->s, L"чистовая");
  if (o->kind != NMK_DRILL) {
    const wchar_t *k = fine                              ? L"без корки"
                       : wcsstr(r->blank, L"Отливка")    ? L"отливка"
                       : wcsstr(r->blank, L"Поковка") || wcsstr(r->blank, L"Штамповка") ? L"поковка"
                                                         : L"прокат";
    nm_put(c, L"корка", k, NM_NONE, fine ? NMO_CALC : NMO_DEF, fine ? L"после черновой" : L"по заготовке");
  }
  if (o->kind == NMK_TURN && fine) nm_putv(c, L"r", 0.8, NMO_DEF, L"оценка: радиус при вершине пластины");
  if (drill) {
    nm_put(c, L"заточка", L"одинарная", NM_NONE, NMO_DEF, L"стандартное сверло без подточки перемычки");
    double d = nm_v(c, L"d"), l = nm_v(c, L"l");
    if (!nm_na(d) && !nm_na(l) && d > 0) nm_putv(c, L"глубина в d", l / d, NMO_CALC, L"l/d");
  }
  if (o->kind == NMK_MILL) {
    nm_put(c, L"фреза", L"торцовая", NM_NONE, NMO_DEF, L"обработка плоскости");
    const NmPar *fp = nm_parc(c, L"фреза");
    double B = nm_v(c, L"B");
    if (fp && nm_eq(fp->s, L"торцовая") && !nm_na(B) && B > 0)
      nm_putv(c, L"Dф", (double)nm_ceil(1.375 * B), NMO_DEF, L"оценка: (1,25–1,5)·B — середина; впишите свою фрезу");
    const NmPar *m = nm_parc(c, L"станок");
    const wchar_t *pw = m ? nm_rec(b, L"станки", m->s, L"мощность", NULL) : NULL;
    if (pw && !nm_na(nm_val(pw))) nm_putv(c, L"мощность", nm_val(pw), NMO_TAB, L"перечень станков");
  }
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
  if (wcsstr(o->op, L"чистов") || wcsstr(o->op, L"тонкая") || !wcscmp(o->sub, L"чистовая") || !wcscmp(o->sub, L"тонкая"))
    nm_put(c, L"вид", L"чистовая", NM_NONE, NMO_CALC, NULL);
  else if (wcsstr(o->op, L"чернов") || !wcscmp(o->sub, L"черновая"))
    nm_put(c, L"вид", L"черновая", NM_NONE, NMO_CALC, NULL);
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
  if (o->kind == NMK_CUT) return;
  /* квалитет не прочитан — свободный размер: для выбора измерителя и периодичности измерений */
  if (nm_na(nm_v(c, L"квалитет"))) nm_putv(c, L"квалитет", 12, NMO_DEF, L"не прочитан — свободный размер");
  per = nm_v(c, L"переходов"), ins = nm_v(c, L"инструментов");
  double it = nm_v(c, L"квалитет"), next = ins > 1 ? ins - 1 : 0, Lm = nm_v(c, L"L"), Dm = nm_v(c, L"D");
  const NmPar *pp = nm_parc(c, L"переход");
  nm_put(c, L"отсчёт", L"0,1", NM_NONE, NMO_DEF, L"штангенциркуль 0,1 мм");
  if (o->kind == NMK_TURN) {
    /* приёмы при переходе — оценка: каждый следующий резец, смена режима на половине переходов */
    nm_putv(c, L"смен резца", next, NMO_DEF, L"оценка: каждый следующий резец");
    double half = (double)(long)((per + 1) / 2);
    nm_putv(c, L"изменений подачи", half, NMO_DEF, L"оценка: на половине переходов");
    nm_putv(c, L"изменений оборотов", half, NMO_DEF, L"оценка: на половине переходов");
    /* контрольные измерения: инструмент по квалитету, размер — Ø, длина — L; периодичность — по способу */
    nm_put(c, L"измеритель", it <= 9 ? L"скоба" : L"штангенциркуль", NM_NONE, NMO_DEF,
           L"по квалитету: до IT9 — скоба, грубее — штангенциркуль");
    nm_put(c, L"поверхность измерения", L"цилиндрические", NM_NONE, NMO_DEF, NULL);
    if (!nm_na(Lm)) nm_putv(c, L"длина измерения", Lm, NMO_CALC, L"длина обработки L");
    if (!nm_na(Lm) && !nm_na(Dm) && Dm > 0) nm_putv(c, L"длина в D", Lm / Dm, NMO_CALC, L"L/D — для калибров");
    nm_put(c, L"способ размера", pp && wcsstr(pp->s, L"на размер") ? L"на размер" : L"лимб", NM_NONE, NMO_CALC,
           L"по виду перехода");
    if (r->s.thrOut > 0) nm_put(c, L"доп", L"резьба", NM_NONE, NMO_DRAW, L"на чертеже наружная резьба");
  } else if (o->kind == NMK_DRILL) {
    nm_putv(c, L"смен инструмента", next, NMO_DEF, L"оценка: каждый следующий инструмент");
    nm_putv(c, L"изменений режима", next, NMO_DEF, L"оценка: при смене инструмента");
    /* отверстие: размер даёт сверло; меряют калибром-пробкой (до IT11) или штангенциркулем */
    if (nm_na(nm_v(c, L"l")) && !nm_na(nm_v(c, L"толщина")))
      nm_putv(c, L"l", nm_v(c, L"толщина"), NMO_CALC, L"сквозные — толщина листа");
    double d = nm_v(c, L"d"), l = nm_v(c, L"l");
    nm_put(c, L"измеритель", it <= 11 ? L"пробка" : L"штангенциркуль", NM_NONE, NMO_DEF,
           L"по квалитету: до IT11 — калибр-пробка, грубее — штангенциркуль");
    nm_put(c, L"поверхность измерения", L"цилиндрические", NM_NONE, NMO_DEF, NULL);
    if (!nm_na(l)) nm_putv(c, L"длина измерения", l, NMO_CALC, L"глубина отверстия");
    if (!nm_na(l) && !nm_na(d) && d > 0) nm_putv(c, L"длина в D", l / d, NMO_CALC, L"l/d — для калибров");
    nm_put(c, L"способ размера", L"инструмент", NM_NONE, NMO_DEF, L"размер отверстия даёт сверло");
  } else if (o->kind == NMK_MILL) {
    nm_putv(c, L"изменений оборотов", next, NMO_DEF, L"оценка: при смене фрезы");
    nm_putv(c, L"изменений подачи", next, NMO_DEF, L"оценка: при смене фрезы");
    /* плоскость: размер неизвестен — по ширине; точность — наименьший допуск с листа или свободный */
    nm_put(c, L"измеритель", L"штангенциркуль", NM_NONE, NMO_DEF, NULL);
    nm_put(c, L"поверхность измерения", L"плоскости", NM_NONE, NMO_DEF, NULL);
    if (!nm_na(nm_v(c, L"B"))) nm_putv(c, L"И", nm_v(c, L"B"), NMO_DEF, L"оценка: ширина фрезерования");
    if (!nm_na(Lm)) nm_putv(c, L"длина измерения", Lm, NMO_CALC, L"длина фрезерования L");
    if (r->s.band > 0) nm_putv(c, L"допуск", r->s.band, NMO_DRAW, L"наименьший допуск с листа");
    else nm_putv(c, L"допуск", 0.5, NMO_DEF, L"свободный размер");
    nm_put(c, L"способ размера", pp && wcsstr(pp->s, L"на размер") ? L"на размер" : L"лимб", NM_NONE, NMO_CALC,
           L"по виду перехода");
  }
  nm_ctx_modes(b, r, o, c);
}

/* вопрос для одной операции: куски «010: подача 0,3» — только операции 010, остальное — всем.
   «норма: L 120, 010: подача 0,3, 015: подача 0,2» — L всем, подачи по операциям; позднее важнее */
static void nm_ask_op(const wchar_t *ask, int num, NmCtx *c) {
  if (!ask) return;
  size_t n = wcslen(ask);
  wchar_t *keep = (wchar_t *)malloc((n + 2) * sizeof(wchar_t));
  if (!keep) return;
  size_t k = 0;
  int mine = 1; /* до первой метки строки — всем */
  for (size_t i = 0; i < n; i++) {
    wchar_t ch = ask[i];
    if (ch == L'\n') {
      mine = 1;
      keep[k++] = ch;
      continue;
    }
    /* метка «NNN:» — три цифры, перед ними начало, пробел или запятая */
    if (tn_isdig(ch) && i + 2 < n && tn_isdig(ask[i + 1]) && tn_isdig(ask[i + 2]) &&
        (i == 0 || ask[i - 1] == L' ' || ask[i - 1] == L',' || ask[i - 1] == L':' || ask[i - 1] == L'\n')) {
      size_t e = i + 3;
      while (e < n && ask[e] == L' ') e++;
      if (e < n && ask[e] == L':' && (i + 3 >= n || !tn_isdig(ask[i + 3]))) {
        int v = (ch - L'0') * 100 + (ask[i + 1] - L'0') * 10 + (ask[i + 2] - L'0');
        mine = v == num;
        if (k) keep[k++] = L','; /* разделить куски: «L 120» не слипнется с «подача 0,3» */
        i = e;
        continue;
      }
    }
    if (mine) keep[k++] = ch;
  }
  keep[k] = 0;
  nm_ask(keep, c);
  free(keep);
}

/* ---- какие операции нормируются и чем (Настройки → «Нормирование», список операций) ----------
   Названия — как в справочнике операций (techno.c берёт их оттуда по номеру). Станочные — по ОМН,
   резка — по нормам резки завода; остальные — оценкой из таблицы «оценка»
   (укрупнённо, не ОМН: Тшт = Тшт + на кг·масса, Тпз), термическая и покрытия — по факту (время
   цикла задаёт их технология). Класс операции (Настройки) — Тпз и Тшт, только Тшт или без норм;
   операции «без норм» в ответе одной строкой. */
enum { NMS_OMN, NMS_CUT, NMS_EST, NMS_FACT, NMS_TECH, NMS_SAW, NMS_GEAR, NMS_WELD };

/* норма технолога на термическую обработку: Тшт по массе детали — до порога одна, тяжелее другая;
   массы нет — как у тяжёлой, с пометкой. Цифры — Настройки → «Нормирование» (norms.ini [heat]) */
typedef struct {
  double limit, light, heavy; /* кг; мин до порога включительно; мин тяжелее */
  double clean;               /* мин — слесарная после ТО: зачистить (techno.c ставит её за термической) */
} NmHeat;
#define NM_HEAT0 {1, 2, 3, 0.5} /* как сказал технолог: до 1 кг — 2 мин, тяжелее — 3, зачистить после — 0,5 */
static NmHeat g_nmHeat = NM_HEAT0;

typedef struct {
  const wchar_t *op; /* как в маршруте; «Токарная» — и черновая, и чистовая */
  int how;
  const wchar_t *src;
  int cls0, cls; /* класс: как при установке и выбранный в Настройках (norms.ini [ops]) */
} NmOpRule;

#define NM_SRC_CUT L"нормы резки завода (скорость), ОМН на газовую резку (обслуживание)"
#define NM_SRC_EST L"оценка CursorPad, не ОМН (таблица «оценка»: Тшт + на кг·масса, Тпз) — проверить"
#define NM_R(op, how, src, cls) {op, how, src, cls, cls}
/* классы — правило технолога: Тпз только у мехобработки (токарные, сверлильные, фрезерные,
   шлифовальные, зубообработка, электроэрозия); заготовительные (резка, отрезная, гибка, штамповка —
   участок заготовки) и вспомогательные (слесарная, разметка, маркирование, сварка, термическая,
   покрытия) — только Тшт; контроль — без норм */
static NmOpRule g_nmOps[] = {
    NM_R(L"Токарная", NMS_OMN, L"ОМН вспомогательного времени (карты 2, 9, 20, 43–47), То — режимы по формулам", NMC_FULL),
    NM_R(L"Сверлильная", NMS_OMN, L"ОМН (карты 9, 10, 25, 43–45, 52), То — режимы по формулам", NMC_FULL),
    NM_R(L"Фрезерная", NMS_OMN, L"ОМН (карты 10, 27, 43–45, 54), То — режимы по формулам", NMC_FULL),
    NM_R(L"Термическая резка лазерная", NMS_CUT, NM_SRC_CUT, NMC_TSHT),
    NM_R(L"Термическая резка плазменно-дуговая", NMS_CUT, NM_SRC_CUT, NMC_TSHT),
    NM_R(L"Термическая резка кислородная", NMS_CUT, NM_SRC_CUT, NMC_TSHT),
    NM_R(L"Раскрой", NMS_CUT, NM_SRC_CUT, NMC_TSHT),
    NM_R(L"Отрезная", NMS_SAW,
         L"ОМН: установка (к. 9), переход (к. 33), обслуживание (к. 45); То — сечение заготовки / производительность "
         L"пилы (оценка CursorPad, Настройки)",
         NMC_TSHT),
    NM_R(L"Токарная (резьба)", NMS_EST, NM_SRC_EST, NMC_FULL),
    NM_R(L"Накатка", NMS_EST, NM_SRC_EST, NMC_FULL),
    NM_R(L"Шлифовальная", NMS_EST, NM_SRC_EST, NMC_FULL),
    NM_R(L"Круглошлифовальная", NMS_EST, NM_SRC_EST, NMC_FULL),
    NM_R(L"Плоскошлифовальная", NMS_EST, NM_SRC_EST, NMC_FULL),
    NM_R(L"3убофрезерная", NMS_EST, NM_SRC_EST, NMC_FULL),
    NM_R(L"Зубодолбежная", NMS_GEAR,
         L"ОМН: установка (к. 16), переход (к. 33), обслуживание (к. 45), Тпз (к. 68); То — по формуле, режимы — "
         L"оценка CursorPad (Настройки)",
         NMC_FULL),
    NM_R(L"Долбежная", NMS_EST, NM_SRC_EST, NMC_FULL),
    NM_R(L"Профильно-вырезная электрофизическая электроэрозионная электроискровая проволочным электродом", NMS_EST,
         NM_SRC_EST, NMC_FULL),
    NM_R(L"Доводочная", NMS_EST, NM_SRC_EST, NMC_FULL),
    NM_R(L"Гибка", NMS_EST, NM_SRC_EST, NMC_TSHT),
    NM_R(L"Штамповка", NMS_EST, NM_SRC_EST, NMC_TSHT),
    NM_R(L"Сварка", NMS_WELD,
         L"ОМН на сварку в защитных газах: по длине шва и катету (карта 6), зачистка (75), установка (82), длина шва (90)",
         NMC_TSHT),
    NM_R(L"Разметка", NMS_EST, NM_SRC_EST, NMC_TSHT),
    NM_R(L"Слесарная", NMS_EST, NM_SRC_EST, NMC_TSHT),
    NM_R(L"Маркирование", NMS_EST, NM_SRC_EST, NMC_TSHT),
    NM_R(L"Контроль", NMS_EST, L"контроль не нормируется: ни Тпз, ни Тшт", NMC_NONE),
    NM_R(L"Термическая обработка", NMS_TECH, L"норма технолога: Тшт по массе детали (до порога и тяжелее — Настройки)",
         NMC_TSHT),
    NM_R(L"Получение покрытий", NMS_FACT, L"по факту: время в ваннах — по технологии покрытия", NMC_TSHT),
    NM_R(L"Цинкование электрохимическое", NMS_FACT, L"по факту: время в ваннах — по технологии покрытия", NMC_TSHT),
    NM_R(L"Получение покрытий органических (лакакрасочных)", NMS_FACT, L"по факту: время сушки — по технологии окраски",
         NMC_TSHT),
};
#define NM_OPRULES ((int)(sizeof(g_nmOps) / sizeof(g_nmOps[0])))

/* правило операции маршрута: по названию целиком, иначе по началу до « (» («Токарная (черновая)») */
static NmOpRule *nm_op_rule(const wchar_t *op) {
  for (int k = 0; k < NM_OPRULES; k++)
    if (nm_eq(g_nmOps[k].op, op)) return &g_nmOps[k];
  for (int k = 0; k < NM_OPRULES; k++) {
    const wchar_t *e = nm_pre(op, g_nmOps[k].op);
    if (e && e[0] == L' ' && e[1] == L'(') return &g_nmOps[k];
  }
  return NULL;
}

/* класс операции: по правилу из списка; нет в списке — станочная или код мехобработки по
   справочнику (41xx–42xx, кроме отрезных 4280…4287) — Тпз и Тшт, «Контрол…» — без норм,
   прочее — только Тшт */
static int nm_op_class(const NmOpRule *rule, const wchar_t *op, int kind, const wchar_t *code) {
  if (rule) return rule->cls;
  if (kind == NMK_TURN || kind == NMK_DRILL || kind == NMK_MILL) return NMC_FULL;
  if (nm_pre(op, L"Контрол")) return NMC_NONE;
  long c = code && code[0] ? wcstol(code, NULL, 10) : 0;
  return c >= 4100 && c <= 4299 && !(c >= 4280 && c <= 4287) ? NMC_FULL : NMC_TSHT;
}

/* название с уточнением маршрута: «Токарная (резьба)» — для правил и строк оценки */
static void nm_op_key(const wchar_t *op, const wchar_t *sub, wchar_t *out, int cap) {
  tn_cpy(out, cap, op);
  if (sub && !wcscmp(sub, L"резьба")) tn_cat(out, cap, L" (резьба)"); /* своё правило и строка оценки */
}

/* оценка операции без таблиц нормативов: строка таблицы «оценка» по названию операции */
static void nm_est(NmJob *j) {
  NmOp *o = j->o;
  wchar_t s[400];
  o->to = o->tv = o->top = o->tsht = o->tpz = o->tshk = NM_NONE;
  wchar_t key[160];
  nm_op_key(o->op, o->sub, key, 160);
  const NmOpRule *rule = nm_op_rule(key);
  if (o->kind == NMK_FACT) {
    nm_line(j, rule ? rule->src : L"по факту");
    return;
  }
  if (o->kind == NMK_TECH && !wcscmp(o->sub, L"зачистка ТО")) { /* слесарная после ТО */
    o->tsht = g_nmHeat.clean;
    tn_cpy(s, 400, L"Тшт ");
    tn_catnum(s, 400, o->tsht);
    tn_cat(s, 400, L" мин — норма технолога: зачистка после термической обработки (Настройки → Нормирование)");
    nm_line(j, s);
    return;
  }
  if (o->kind == NMK_TECH) { /* норма технолога: по массе детали со штампа */
    double m = nm_v(&j->c, L"масса");
    o->tsht = nm_na(m) || m > g_nmHeat.limit ? g_nmHeat.heavy : g_nmHeat.light;
    tn_cpy(s, 400, L"Тшт ");
    tn_catnum(s, 400, o->tsht);
    tn_cat(s, 400, L" мин — норма технолога: ");
    if (nm_na(m)) {
      tn_cat(s, 400, L"масса не прочитана — проверить; взято как для деталей тяжелее ");
      tn_catnum(s, 400, g_nmHeat.limit);
      tn_cat(s, 400, L" кг");
    } else {
      tn_cat(s, 400, L"масса ");
      tn_catnum(s, 400, m);
      tn_cat(s, 400, m > g_nmHeat.limit ? L" кг — тяжелее " : L" кг — до ");
      tn_catnum(s, 400, g_nmHeat.limit);
      tn_cat(s, 400, L" кг включительно");
    }
    tn_cat(s, 400, L" (Настройки → Нормирование)");
    nm_line(j, s);
    return;
  }
  nm_put(&j->c, L"операция", key, NM_NONE, NMO_CALC, NULL);
  NmHit h;
  if (!nm_find(j->b, L"оценка", &j->c, &h)) {
    tn_cpy(s, 400, L"оценки нет: ");
    tn_cat(s, 400, h.why);
    tn_cat(s, 400, L" — добавьте строку в таблицу «оценка» (Документы\\CursorPad\\Нормативы)");
    nm_line(j, s);
    return;
  }
  nm_use(j->res, h.t);
  double a = nm_field(&h, L"Тшт"), k = nm_field(&h, L"на кг"), pz = nm_field(&h, L"Тпз"), m = nm_v(&j->c, L"масса");
  if (nm_na(a)) return;
  if (nm_na(k)) k = 0;
  o->tsht = a + (nm_na(m) ? 0 : k * m);
  o->tpz = nm_na(pz) || o->cls != NMC_FULL ? NM_NONE : pz; /* Тпз — только у мехобработки */
  tn_cpy(s, 400, L"Тшт ≈ ");
  tn_catnum(s, 400, a);
  if (k != 0 && !nm_na(m)) tn_cat(s, 400, L" + "), tn_catnum(s, 400, k), tn_cat(s, 400, L"·"), tn_catnum(s, 400, m);
  tn_cat(s, 400, L" = ");
  tn_catnum(s, 400, o->tsht);
  tn_cat(s, 400, L" мин — оценка, проверить");
  if (k != 0 && nm_na(m)) tn_cat(s, 400, L" (масса не задана — без поправки на массу)");
  tn_cat(s, 400, L"; ");
  tn_cat(s, 400, h.why);
  nm_line(j, s);
  if (!wcscmp(o->sub, L"+зачистка ТО")) { /* к слесарной добавлена зачистка после ТО — её норма сверху */
    o->tsht += g_nmHeat.clean;
    tn_cpy(s, 400, L"+ ");
    tn_catnum(s, 400, g_nmHeat.clean);
    tn_cat(s, 400, L" мин — зачистка после термической обработки, норма технолога; Тшт ≈ ");
    tn_catnum(s, 400, o->tsht);
    tn_cat(s, 400, L" мин");
    nm_line(j, s);
  }
  double q = nm_v(&j->c, L"партия");
  if (!nm_na(o->tpz) && !nm_na(q) && q > 0) o->tshk = o->tsht + o->tpz / q;
}

/* нормы всех операций маршрута; ask — вопрос пользователя (может быть NULL) */
static void nm_route(const NmBase *b, const TnRoute *r, const wchar_t *mass, const wchar_t *ask, NmResult *res) {
  memset(res, 0, sizeof(*res));
  NmJob *j = (NmJob *)malloc(sizeof(NmJob));
  if (!j) return;
  for (int i = 0; i < r->n && res->n < NM_OPS; i++) {
    int kind = nm_kind(r->op[i].op, r->op[i].sub);
    wchar_t key[160];
    nm_op_key(r->op[i].op, r->op[i].sub, key, 160);
    const NmOpRule *rule = nm_op_rule(key);
    int cls = nm_op_class(rule, r->op[i].op, kind, r->op[i].code);
    if (cls == NMC_NONE) { /* без норм (контроль) — ни Тпз, ни Тшт */
      tn_item(res->off, 400, r->op[i].op);
      continue;
    }
    if (!kind)
      kind = !rule                     ? NMK_EST
             : rule->how == NMS_FACT ? NMK_FACT
             : rule->how == NMS_TECH ? NMK_TECH
             : rule->how == NMS_SAW  ? NMK_SAW
             : rule->how == NMS_GEAR ? NMK_GEAR
             : rule->how == NMS_WELD ? NMK_WELD
                                     : NMK_EST;
    if (!wcscmp(r->op[i].sub, L"зачистка ТО")) kind = NMK_TECH; /* слесарная после ТО — норма технолога */
    if (!b->n && kind != NMK_CUT) continue; /* без таблиц — только То резки по нормам завода */
    NmOp *o = &res->op[res->n++];
    memset(o, 0, sizeof(*o));
    o->kind = kind;
    o->idx = i;
    o->cls = cls;
    tn_cpy(o->op, 128, r->op[i].op);
    tn_cpy(o->sub, 24, r->op[i].sub);
    memset(j, 0, sizeof(*j));
    j->b = b;
    j->o = o;
    j->res = res;
    j->r = r;
    nm_ask_op(ask, (i + 1) * 5, &j->c);
    nm_ctx_sheet(r, mass, &j->c);
    if (kind == NMK_EST || kind == NMK_FACT || kind == NMK_TECH) {
      double q;
      if (nm_na(nm_v(&j->c, L"партия"))) nm_lim_def(j, L"партия", &q);
      nm_est(j);
    } else if (kind == NMK_SAW) {
      nm_saw(j);
    } else if (kind == NMK_WELD) {
      double q;
      if (nm_na(nm_v(&j->c, L"партия"))) nm_lim_def(j, L"партия", &q);
      nm_weld(j);
    } else if (kind == NMK_GEAR) {
      double q;
      if (nm_na(nm_v(&j->c, L"партия"))) nm_lim_def(j, L"партия", &q);
      nm_gear(j);
    } else {
      nm_ctx_defaults(b, r, o, &j->c);
      nm_op_calc(j);
    }
    o->c = j->c;
  }
  free(j);
}

/* ---- текст ----------------------------------------------------------------------- */

static void nm_catmin(wchar_t *out, int cap, double v) {
  tn_catnum(out, cap, v);
  tn_cat(out, cap, L" мин");
}

/* итог операции-оценки и «по факту» после «010 Слесарная: »; 1 — написан */
static int nm_head_est(const NmOp *o, wchar_t *out, int cap) {
  if (o->kind != NMK_FACT && o->kind != NMK_TECH && o->kind != NMK_EST) return 0;
  /* строка всегда с «Тпз … ·»: так её узнаёт таблица норм в чате (ai_chat_format.h), «—» — Тпз нет */
  if (nm_na(o->tpz)) tn_cat(out, cap, L"Тпз — · ");
  else tn_cat(out, cap, L"Тпз "), nm_catmin(out, cap, o->tpz), tn_cat(out, cap, L" · ");
  if (o->kind == NMK_FACT) {
    tn_cat(out, cap, L"Тшт — (по факту, не нормируется)");
    return 1;
  }
  if (o->kind == NMK_TECH) {
    tn_cat(out, cap, L"Тшт ");
    nm_catmin(out, cap, o->tsht);
    tn_cat(out, cap, nm_na(nm_v(&o->c, L"масса")) && wcscmp(o->sub, L"зачистка ТО")
                         ? L" (норма технолога; масса не прочитана — проверить)"
                         : L" (норма технолога)");
    return 1;
  }
  if (nm_na(o->tsht)) {
    tn_cat(out, cap, L"Тшт — (оценки нет)");
    return 1;
  }
  tn_cat(out, cap, L"Тшт ≈ ");
  nm_catmin(out, cap, o->tsht);
  if (!nm_na(o->tshk)) tn_cat(out, cap, L" · Тшк ≈ "), nm_catmin(out, cap, o->tshk);
  tn_cat(out, cap, L" (оценка, проверить)");
  return 1;
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
    if (nm_head_est(o, out, cap)) {
      tn_cat(out, cap, o->how);
      continue;
    }
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
    if (o->ntr) { /* разбивка: откуда цифра — переход, его То и Тв */
      tn_cat(out, cap, L"\n   по переходам (То · Тв без установки):");
      for (int k = 0; k < o->ntr; k++) {
        tn_cat(out, cap, L"\n   ");
        tn_catnum(out, cap, k + 1);
        tn_cat(out, cap, L". ");
        tn_cat(out, cap, o->tr[k].what);
        tn_cat(out, cap, L" — То ");
        if (nm_na(o->tr[k].to)) tn_cat(out, cap, L"—");
        else tn_catnum(out, cap, o->tr[k].to);
        tn_cat(out, cap, L" · Тв ");
        tn_catnum(out, cap, o->tr[k].tv);
      }
    }
    tn_cat(out, cap, o->how);
  }
  if (res->skip[0]) tn_cat(out, cap, L"\nПока не нормируются: "), tn_cat(out, cap, res->skip);
  if (res->off[0]) tn_cat(out, cap, L"\nБез норм (Настройки → Нормирование): "), tn_cat(out, cap, res->off);
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

/* «норма подробно», «откуда», «разбор» в последней строке задания — нужен разбор с источниками */
static int nm_want_full(const wchar_t *ask) {
  if (!ask) return 0;
  const wchar_t *last = wcsrchr(ask, L'\n');
  wchar_t *f = tn_fold(last ? last + 1 : ask, 0);
  if (!f) return 0;
  int yes = wcsstr(f, L"подробн") || wcsstr(f, L"откуда") || wcsstr(f, L"разбор") ||
            wcsstr(f, L"источник");
  free(f);
  return yes;
}

/* «Режимы: 010 V 125 · S 0,45 · t 2,5 · i 1; 015 V 20 · S 0,22 — изменить · подробно» — одна строка
   под нормами: режимы, по которым То; слова «изменить» (окно «Режимы») и «подробно» (разбор) чат
   делает ссылками (ai_chat.c). Нет станочных операций — строки нет */
#define NM_MODES_HEAD L"Режимы: "
#define NM_MODES_TAIL L" — изменить · подробно"
static void nm_modes_line(const NmResult *res, wchar_t *out, int cap) {
  wchar_t line[400];
  line[0] = 0;
  for (int i = 0; i < res->n; i++) {
    const NmOp *o = &res->op[i];
    if (o->kind != NMK_TURN && o->kind != NMK_DRILL && o->kind != NMK_MILL) continue; /* режимы — у станочных */
    static const wchar_t *const turn[] = {L"V", L"S", L"t", L"i"}, *const mill[] = {L"V", L"Sz", L"t", L"i"};
    const wchar_t *const *ps = o->kind == NMK_MILL ? mill : turn;
    int np = o->kind == NMK_DRILL ? 2 : 4;
    wchar_t num[8];
    swprintf(num, 8, L"%03d", (o->idx + 1) * 5);
    num[7] = 0;
    if (line[0]) tn_cat(line, 400, L"; ");
    tn_cat(line, 400, num);
    int any = 0;
    for (int k = 0; k < np; k++) {
      double v = nm_v(&o->c, ps[k]);
      if (nm_na(v)) continue;
      tn_cat(line, 400, any ? L" · " : L" ");
      tn_cat(line, 400, ps[k]);
      tn_cat(line, 400, L" ");
      tn_catnum(line, 400, !wcscmp(ps[k], L"V") ? (double)(long)(v + 0.5) : v);
      any = 1;
    }
    if (!any && o->ntr) { /* у каждого перехода свои режимы — они в «подробно» */
      tn_cat(line, 400, L" по переходам (");
      tn_catnum(line, 400, o->ntr);
      tn_cat(line, 400, L")");
    } else if (!any) {
      tn_cat(line, 400, L" не посчитаны");
    }
  }
  if (!line[0]) return;
  tn_cat(out, cap, L"\n" NM_MODES_HEAD);
  tn_cat(out, cap, line);
  tn_cat(out, cap, NM_MODES_TAIL);
}

/* коротко к маршруту: строка на операцию; разбор каждой цифры и источники — nm_text */
static void nm_text_brief(const NmBase *b, const NmResult *res, const wchar_t *dir, wchar_t *out, int cap) {
  if (!b->n || !res->n) { /* таблиц нет или нормировать нечего — как в полном тексте, он короткий */
    nm_text(b, res, dir, out, cap);
    return;
  }
  tn_cat(out, cap, L"\n\nНормы времени (ОМН; проверяет технолог):");

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
    if (nm_head_est(o, out, cap)) continue;
    if (nm_na(o->tsht)) {
      tn_cat(out, cap, L"То ");
      if (nm_na(o->to)) tn_cat(out, cap, L"—");
      else nm_catmin(out, cap, o->to);
      continue;
    }
    tn_cat(out, cap, L"Тпз ");
    if (nm_na(o->tpz)) tn_cat(out, cap, L"—");
    else nm_catmin(out, cap, o->tpz);
    tn_cat(out, cap, L" · Тшт ");
    if (o->full) {
      nm_catmin(out, cap, o->tsht);
      if (!nm_na(o->tshk)) tn_cat(out, cap, L" · Тшк "), nm_catmin(out, cap, o->tshk);
    } else if (nm_na(o->to)) {

      if (o->tsht > 0) tn_cat(out, cap, L"≥ "), nm_catmin(out, cap, o->tsht), tn_cat(out, cap, L" без машинного То");
      else tn_cat(out, cap, L"— нет машинного То");
      if (o->toNeed[0]) tn_cat(out, cap, L" (нужно: "), tn_cat(out, cap, o->toNeed), tn_cat(out, cap, L")");
    } else {
      tn_cat(out, cap, L"≈ ");
      nm_catmin(out, cap, o->tsht);
      tn_cat(out, cap, L" (неполная)");
    }
    /* То посчитано с оценкой припуска или фрезы — коротко, что проверить */
    static const wchar_t *const est[] = {L"h", L"z"}, *const estName[] = {L"припуск", L"зубья фрезы"};
    int first = 1;
    for (int k = 0; k < 2 && !nm_na(o->to); k++) {
      const NmPar *p = nm_parc(&o->c, est[k]);
      if (!p || p->org != NMO_DEF || !nm_pre(p->why, L"оценка")) continue;
      tn_cat(out, cap, first ? L" (" : L", ");
      tn_cat(out, cap, estName[k]);
      first = 0;
    }
    if (!first) tn_cat(out, cap, L" — оценка, проверить)");
  }
  if (res->skip[0]) tn_cat(out, cap, L"\nПока не нормируются: "), tn_cat(out, cap, res->skip);
  if (res->off[0]) tn_cat(out, cap, L"\nБез норм (Настройки → Нормирование): "), tn_cat(out, cap, res->off);
  if (b->err[0]) tn_cat(out, cap, L"\nНе прочитаны таблицы: "), tn_cat(out, cap, b->err);
  nm_modes_line(res, out, cap);
}

/* ---- Windows: папка нормативов (только в сборке CursorPad, где подключён windows.h) ---- */
#ifdef _WINDOWS_

/* Документы\CursorPad\Нормативы — вне AppData: туда кладут и правят руками, в Excel */
static const wchar_t *g_nmDirFix; /* стенд окна «Режимы»: своя папка таблиц, без раскладки встроенных */

static void nm_dir(wchar_t *out) {
  out[0] = 0;
  if (g_nmDirFix) {
    lstrcpynW(out, g_nmDirFix, MAX_PATH);
    return;
  }
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

/* все *.tsv и *.txt папки; FALSE — папки нет (и создать не вышло). Служебные «.имя»
   (.встроено.txt раскладки встроенных таблиц) не таблицы; «имя.tsv.новая» маска с коротким
   именем 8.3 могла бы зацепить — расширение сверяем по длинному имени */
static BOOL nm_base_load(NmBase *b, wchar_t *dir) {
  memset(b, 0, sizeof(*b));
  nm_dir(dir);
#ifdef TABLES_SEED_ONCE /* встроенные таблицы ОМН — в папку, раз за запуск; проверочную CP_NORMS не трогаем */
  BOOL own = !g_nmDirFix;
#ifdef SET_TEST
  const wchar_t *test = _wgetenv(L"CP_NORMS");
  own = own && !(test && test[0]);
#endif
  if (dir[0] && own) tables_seed_once(0, L"norms", dir);
#endif
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
      const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
      if (fd.cFileName[0] == L'.' || !ext || _wcsicmp(ext, masks[m] + 1)) continue;
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

/* текст норм к маршруту (дописать после tn_text) или NULL, если папки нормативов нет; коротко,
   а с «подробно» в последней строке ask — разбор с источниками */
static wchar_t *nm_route_text(const TnRoute *r, const wchar_t *mass, const wchar_t *ask) {
  NmBase b;
  wchar_t dir[MAX_PATH];
  if (!nm_base_load(&b, dir)) return NULL;
  NmResult *res = (NmResult *)malloc(sizeof(NmResult));
  wchar_t *out = (wchar_t *)malloc(sizeof(wchar_t) * NM_OUT);
  if (res && out) {
    out[0] = 0;
    nm_route(&b, r, mass, ask, res);
    if (nm_want_full(ask)) nm_text(&b, res, dir, out, NM_OUT);
    else nm_text_brief(&b, res, dir, out, NM_OUT);
#ifdef TABLES_SEED_ONCE
    tables_note_append(0, out, NM_OUT);
#endif
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
                 L"Нормирование не включено: папки с таблицами нормативов %s нет и создать её не вышло "
                 L"(нет доступа к «Документам»?). Таблицы встроены в CursorPad и кладутся туда сами.",
                 dir);
      t[MAX_PATH + 299] = 0;
    }
  }
  return t;
}

#endif
