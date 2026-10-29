/* pk_paste.c — «Вставить» в окне «ЭСИ и ТП» (package_ui.c, с 2026.10.29): что бы ни
   скопировали, строки сами ложатся по листам Изделия, Состав, Операции, Заготовки.
   Узнаёт по порядку:
     1. строки описания для «Союза» (ИЗДЕЛИЕ / СОСТАВ / ЗАГОТОВКА / ТП через Tab) — как есть;
     2. таблицу с шапкой из Excel или Word, CSV через «;»: столбцы находятся по шапке
        в любом порядке («Обозн.», «Кол.», «Поз.», «№ опер.», «Станок», «Тшт, ч»…).
        Есть операция или нормы времени — Операции; норма расхода или размеры — Заготовки;
        количество, сборка или позиция — спецификация: Изделия и Состав; иначе — Изделия;
     3. маршрут из чата ИИ («Лист: … · обозначение», «Деталь:», «Материал:», «005 Токарная. …»,
        строки норм) — изделие и операции с кодами справочника;
     4. операции словами: «005 Токарная 16К20 Тпз 12 Тшт 3,5», «010 Сверлильная, уч. 31»;
     5. список: «1 АБВГ.123456.002 Корпус 2 шт», «Болт М6х20 ГОСТ 7798-70 — 8 шт».
   Таблица без шапки — по содержимому (обозначение, наименование, количество); не понял —
   по порядку столбцов открытого листа, как было раньше.
   Спецификация: строки разделов («Детали», «Стандартные изделия»…) дают вид изделия,
   документация и материалы пропускаются (материал детали — столбец «Материал» или лист
   «Заготовки»: состав «Союз» заводит в штуках). У стандартных и прочих изделий без
   обозначения обозначением становится наименование — так их и заводят в «Союзе».
   Изделие, у которого есть только обозначение, идёт лишь в состав: строка «Изделия» без
   наименования стёрла бы наименование, уже заведённое в «Союзе» (функция его выравнивает).
   Windows не нужен: gcc -O2 -Wall -Werror -o pk_paste_test pk_paste_test.c && ./pk_paste_test
   Подключается после search_route.c (шаблон обозначения), ops_1c.h, techno.c и soyuz_ops.c
   (маршрут, коды операций); листы PK_* — из export.c (стенд объявляет их сам). */

#define PP_COLS 7 /* столбцов у самого широкого листа — «Операции» */
#define PP_CELL 400
#define PP_ROWS 3000
#define PP_MAXC 24

/* строка в лист sheet: cells[PP_COLS] в порядке столбцов листа, пустое — L"" */
typedef void (*PpPut)(void *ctx, int sheet, const wchar_t *const *cells);

typedef struct {
  int sheet;               /* открытый лист: туда — таблица, которую не поняли */
  const wchar_t *assembly; /* сборка для состава, если её нет в тексте (выделенная в окне); L"" — нет */
  const wchar_t *part;     /* деталь для операций и заготовок, если её нет в тексте */
  PpPut put;
  void *ctx;
} PpIn;

/* как поняли текст — для строки состояния окна */
enum { PPH_NONE, PPH_SPEC, PPH_HEAD, PPH_GUESS, PPH_ROUTE, PPH_OPS, PPH_LIST, PPH_PLAIN };

typedef struct {
  int how;
  int rows[4];               /* строк по листам PK_ITEMS…PK_BLANKS */
  int docs, mats;            /* пропущено строк спецификации: документация, материалы */
  int noParent, noPart;      /* строк состава без сборки; операций и заготовок без детали */
  wchar_t assembly[PP_CELL]; /* сборка, к которой пошёл состав */
  wchar_t part[PP_CELL];     /* деталь, к которой пошли операции или заготовки */
  wchar_t cols[240];         /* узнанные столбцы: «обозначение, наименование, кол-во» */
} PpOut;

/* ---- строки ------------------------------------------------------------------------- */

static int pp_has(const wchar_t *s, const wchar_t *w) { return wcsstr(s, w) != NULL; }
static int pp_pre(const wchar_t *s, const wchar_t *w) { return !wcsncmp(s, w, wcslen(w)); }
static int pp_upper_cyr(wchar_t c) { return (c >= L'А' && c <= L'Я') || c == L'Ё'; }

/* пробелы, «\r» и неразрывный пробел по краям — прочь (в том же буфере) */
static wchar_t *pp_trim(wchar_t *s) {
  while (*s == L' ' || *s == L'\r' || *s == 0xA0 || *s == L'\t') s++;
  size_t n = wcslen(s);
  while (n && (s[n - 1] == L' ' || s[n - 1] == L'\r' || s[n - 1] == 0xA0 || s[n - 1] == L'\t')) s[--n] = 0;
  return s;
}

/* края наименования: тире, точки, двоеточия и прочие разделители — прочь */
static void pp_clean(wchar_t *s) {
  size_t n = wcslen(s), b = 0;
  while (b < n && wcschr(L" \t-–—:;,.=|•*", s[b])) b++;
  while (n > b && wcschr(L" \t-–—:;,=|•*", s[n - 1])) n--;
  memmove(s, s + b, (n - b) * sizeof(wchar_t));
  s[n - b] = 0;
}

static void pp_ncpy(wchar_t *d, int cap, const wchar_t *s, int len) {
  int k = 0;
  while (k < len && s[k] && k < cap - 1) d[k] = s[k], k++;
  d[k] = 0;
}

/* «5» → «005»: номер операции — три цифры */
static void pp_num3(long v, wchar_t *out, int cap) {
  wchar_t t[16];
  int k = 0;
  do t[k++] = (wchar_t)(L'0' + v % 10), v /= 10; while (v && k < 15);
  while (k < 3) t[k++] = L'0';
  int o = 0;
  while (k && o < cap - 1) out[o++] = t[--k];
  out[o] = 0;
}

/* первое число ячейки («2 шт», «0,5 кг», «= 12 мин») × mul → «2», «0,5», «720»; нет числа —
   0 и ячейка как есть (проверка в окне её покажет) */
static int pp_num(const wchar_t *s, double mul, wchar_t *out, int cap) {
  const wchar_t *p = s;
  while (*p == L' ' || *p == L'=' || *p == L'≈' || *p == L'~' || *p == L':') p++;
  double v;
  int len, sep;
  if (!tn_num(p, &v, &len, &sep)) {
    tn_cpy(out, cap, s);
    return 0;
  }
  tn_numstr(v * mul, out, cap);
  return 1;
}

/* число ли вся ячейка («2», «0,5», «2 шт», «8шт.») — для таблиц без шапки и списков */
static int pp_is_qty(const wchar_t *s) {
  double v;
  int len, sep;
  if (!tn_num(s, &v, &len, &sep)) return 0;
  const wchar_t *p = s;
  while (tn_isdig(*p) || *p == L',' || *p == L'.') p++;
  while (*p == L' ') p++;
  return !*p || !wcscmp(p, L"шт") || !wcscmp(p, L"шт.") || !wcscmp(p, L"Шт") || !wcscmp(p, L"Шт.");
}

/* ---- обозначение --------------------------------------------------------------------- */

/* обозначение в тексте по шаблону штампа: [*b, *e) в s; текст — как написан (латиница
   не меняется на кириллицу: «Союз» ищет обозначение дословно) */
static int pp_find_des(const wchar_t *s, int *b, int *e) {
  wchar_t u[SR_Q];
  int n = 0;
  for (; s[n] && n < SR_Q - 1; n++) u[n] = sr_upper(s[n]);
  u[n] = 0;
  if (n >= SR_Q - 1) return 0;
  return sr_find_des(s, u, b, e);
}

/* вся ячейка — обозначение (с кодом впереди: «АДЕ 3424-123.01») */
static int pp_cell_des(const wchar_t *s) {
  wchar_t p[SR_Q];
  sr_prep(s, p, SR_Q);
  int b, e;
  return p[0] && pp_find_des(p, &b, &e) && b <= 1 && e >= (int)wcslen(p) - 1;
}

/* документ спецификации: «АБВГ.123456.000СБ», «… Сборочный чертеж», «Спецификация» */
static int pp_is_doc(const wchar_t *des, const wchar_t *name) {
  wchar_t n[120];
  so_low(name, n, 120);
  static const wchar_t *const kDoc[] = {L"сборочный чертеж", L"спецификация", L"чертеж общего вида", L"габаритный чертеж",
                                        L"монтажный чертеж", L"схема", L"пояснительная записка", L"ведомость",
                                        L"руководство", L"паспорт", L"технические условия", L"электронная модель",
                                        L"теоретический чертеж", L"упаковочный чертеж"};
  for (size_t i = 0; i < sizeof(kDoc) / sizeof(kDoc[0]); i++)
    if (pp_pre(n, kDoc[i])) return 1;
  /* код документа сразу за цифрами обозначения или через пробел: СБ, ГЧ, ВП, ПЗ, Э3… */
  static const wchar_t *const kCode[] = {L"СБ", L"ГЧ", L"МЧ", L"ВП", L"ВС", L"ПЗ", L"ТУ", L"ПС", L"РЭ", L"ЭМ", L"ИМ",
                                         L"Э1", L"Э2", L"Э3", L"Э4", L"Э5", L"Э6", L"Э7", L"УЧ", L"ТЧ"};
  int e = (int)wcslen(des);
  while (e > 0 && des[e - 1] == L' ') e--;
  for (size_t i = 0; i < sizeof(kCode) / sizeof(kCode[0]); i++) {
    int k = e - (int)wcslen(kCode[i]);
    if (k > 0 && !wcsncmp(des + k, kCode[i], wcslen(kCode[i])) && (tn_isdig(des[k - 1]) || des[k - 1] == L' '))
      return 1;
  }
  return 0;
}

/* ---- чертёж, брошенный в окно (pk_stamp.c): обозначение и наименование ------------------ */

/* код документа — прочь: из конца обозначения («АБВГ.123456.000 СБ», «…000СБ») и начала
   наименования («СБ Редуктор» из имени файла), «Сборочный чертеж» — из конца наименования
   (Tesseract берёт графу целиком). 1 — чертёж сборки (СБ, ВО, ГЧ, МЧ, МЭ, УЧ), 2 — другой
   документ изделия (схема, ПЗ…), 0 — кода нет: чертёж детали. code (если не NULL, 8 знаков) — какой
   код снят: «СБ»; только фразой «Сборочный чертеж» — её код */
static int pp_draw_doc_code(wchar_t *des, wchar_t *name, wchar_t *code) {
  static const wchar_t *const kCode[] = {L"СБ", L"ВО", L"ГЧ", L"МЧ", L"МЭ", L"УЧ", /* сборка — первые 6 */
                                         L"ВП", L"ВС", L"ПЗ", L"ТУ", L"ПС", L"РЭ", L"ЭМ", L"ИМ", L"Э1", L"Э2",
                                         L"Э3", L"Э4", L"Э5", L"Э6", L"Э7", L"ТЧ"};
  static const wchar_t *const kPhrase[] = {L"сборочный чертеж", L"чертеж общего вида", L"габаритный чертеж",
                                           L"монтажный чертеж", L"упаковочный чертеж"};
  static const wchar_t *const kPhraseCode[] = {L"СБ", L"ВО", L"ГЧ", L"МЧ", L"УЧ"};
  int doc = 0;
  if (code) code[0] = 0;
  wchar_t *d = pp_trim(des);
  if (d != des) memmove(des, d, (wcslen(d) + 1) * sizeof(wchar_t));
  int e = (int)wcslen(des);
  for (size_t i = 0; i < sizeof(kCode) / sizeof(kCode[0]) && !doc; i++) {
    int k = e - (int)wcslen(kCode[i]);
    if (k > 0 && !wcscmp(des + k, kCode[i]) && (tn_isdig(des[k - 1]) || des[k - 1] == L' ' || des[k - 1] == L'-')) {
      while (k > 0 && (des[k - 1] == L' ' || des[k - 1] == L'-')) k--;
      des[k] = 0;
      doc = i < 6 ? 1 : 2;
      if (code) tn_cpy(code, 8, kCode[i]);
    }
  }
  wchar_t *n = pp_trim(name);
  if (n != name) memmove(name, n, (wcslen(n) + 1) * sizeof(wchar_t));
  for (size_t i = 0; i < sizeof(kCode) / sizeof(kCode[0]); i++) {
    size_t l = wcslen(kCode[i]);
    if (!wcsncmp(name, kCode[i], l) && (name[l] == L' ' || name[l] == L'-' || !name[l])) {
      memmove(name, name + l, (wcslen(name + l) + 1) * sizeof(wchar_t));
      if (!doc) doc = i < 6 ? 1 : 2;
      if (code && !code[0]) tn_cpy(code, 8, kCode[i]);
      break;
    }
  }
  wchar_t low[PP_CELL];
  int L = 0;
  for (; name[L] && L < PP_CELL - 1; L++) low[L] = so_low1(name[L]);
  low[L] = 0;
  while (L && (low[L - 1] == L' ' || low[L - 1] == L'.')) L--;
  for (size_t i = 0; i < sizeof(kPhrase) / sizeof(kPhrase[0]); i++) {
    int k = L - (int)wcslen(kPhrase[i]);
    if (k >= 0 && !wcsncmp(low + k, kPhrase[i], wcslen(kPhrase[i])) && (!k || low[k - 1] == L' ')) {
      while (k > 0 && (name[k - 1] == L' ' || name[k - 1] == L'.' || name[k - 1] == L',')) k--;
      name[k] = 0;
      doc = 1;
      if (code && !code[0]) tn_cpy(code, 8, kPhraseCode[i]);
      break;
    }
  }
  pp_clean(name);
  return doc;
}

#define pp_draw_doc(des, name) pp_draw_doc_code(des, name, NULL)

/* наименование документа сборки в разделе «Документация» — по коду: «СБ» → «Сборочный чертеж» */
static const wchar_t *pp_doc_title(const wchar_t *code) {
  static const wchar_t *const kC[] = {L"СБ", L"ВО", L"ГЧ", L"МЧ", L"МЭ", L"УЧ"};
  static const wchar_t *const kT[] = {L"Сборочный чертеж",  L"Чертеж общего вида",      L"Габаритный чертеж",
                                      L"Монтажный чертеж",  L"Электромонтажный чертеж", L"Упаковочный чертеж"};
  for (size_t i = 0; i < sizeof(kC) / sizeof(kC[0]); i++)
    if (!wcscmp(code, kC[i])) return kT[i];
  return L"";
}

/* обозначение и наименование из имени файла: «АБВГ.123456.002 Корпус.pdf», «Корпус_АБВГ.123456.002.tif»,
   «АБВГ.123456.000 СБ Редуктор.pdf» — когда штамп не прочитался (или файл КОМПАС, DWG).
   0 — обозначения в имени нет; *doc и code — как у pp_draw_doc_code */
static int pp_file_title_code(const wchar_t *path, wchar_t *des, wchar_t *name, int *doc, wchar_t *code) {
  const wchar_t *b = path;
  for (const wchar_t *p = path; *p; p++)
    if (*p == L'\\' || *p == L'/') b = p + 1;
  wchar_t s[SR_Q];
  int n = 0;
  for (; b[n] && n < SR_Q - 1; n++) s[n] = b[n] == L'_' ? L' ' : b[n];
  s[n] = 0;
  wchar_t *dot = wcsrchr(s, L'.'); /* расширение — только если в нём есть буква: «.002» — часть обозначения */
  if (dot && wcslen(dot) <= 6) {
    int letter = 0;
    for (wchar_t *q = dot + 1; *q; q++)
      if (!tn_isdig(*q)) letter = 1;
    if (letter) *dot = 0;
  }
  des[0] = name[0] = 0;
  *doc = 0;
  if (code) code[0] = 0;
  wchar_t p[SR_Q];
  sr_prep(s, p, SR_Q);
  int db, de;
  if (!p[0] || !pp_find_des(p, &db, &de)) return 0;
  pp_ncpy(des, PP_CELL, p + db, de - db);
  tn_cpy(name, PP_CELL, p + de);
  pp_clean(name);
  if (!name[0] && db > 0) { /* «Корпус АБВГ.123456.002» — наименование впереди */
    pp_ncpy(name, PP_CELL, p, db);
    pp_clean(name);
  }
  *doc = pp_draw_doc_code(des, name, code);
  return des[0] != 0;
}

#define pp_file_title(path, des, name, doc) pp_file_title_code(path, des, name, doc, NULL)

/* наименование стандартного (ГОСТ, ОСТ, DIN, ISO) или покупного (ТУ) изделия: 1 — стандартное,
   2 — прочее, 0 — ни то, ни другое */
static int pp_std_name(const wchar_t *name) {
  static const wchar_t *const kStd[] = {L"ГОСТ", L"ОСТ", L"DIN", L"ISO", L"ИСО", L"СТП", L"СТО"};
  for (size_t i = 0; i < sizeof(kStd) / sizeof(kStd[0]); i++)
    if (tn_word(name, kStd[i])) return 1;
  return tn_word(name, L"ТУ") ? 2 : 0;
}

/* ---- вид изделия --------------------------------------------------------------------- */

/* Виды так, как они названы в «Союзе» (NameUI шаблонов ProductClasses\MachineryProduct):
   по ним функция «Союза» и команда «Загрузить ЭСИ из CursorPad» ищут вид дословно —
   «Деталь» в единственном числе не нашлась бы, и сборка завелась бы деталью. */
static const wchar_t *const kPpKinds[] = {L"Сборочные единицы", L"Детали",          L"Детали БЧ",
                                          L"Стандартные изделия", L"Прочие изделия", L"Материалы",
                                          L"Комплекты",          L"Комплексы",       L"Комплекты разборки",
                                          L"Документация"}; /* документ сборки в её составе: «…СБ Сборочный чертеж» */
#define PP_KINDS ((int)(sizeof(kPpKinds) / sizeof(kPpKinds[0])))

/* вид как в «Союзе»: «Деталь», «СЕ», «сборка», «Стандартное изделие» → «Детали»,
   «Сборочные единицы», «Стандартные изделия»; незнакомое — как есть; 1 — поправлено */
static int pp_kind(const wchar_t *in, wchar_t *out, int cap) {
  wchar_t k[80];
  so_low(in, k, 80);
  const wchar_t *v = NULL;
  if (!k[0]) v = L"";
  else if (pp_pre(k, L"сборочн") || pp_pre(k, L"сборка") || pp_pre(k, L"сборки") || !wcscmp(k, L"се") ||
           !wcscmp(k, L"сб") || pp_pre(k, L"узел"))
    v = kPpKinds[0];
  else if (pp_has(k, L"бч") || pp_has(k, L"без чертеж")) v = kPpKinds[2];
  else if (pp_pre(k, L"детал")) v = kPpKinds[1];
  else if (pp_pre(k, L"стандарт") || !wcscmp(k, L"сти")) v = kPpKinds[3];
  else if (pp_pre(k, L"проч") || pp_pre(k, L"покупн")) v = kPpKinds[4];
  else if (pp_pre(k, L"материал")) v = kPpKinds[5];
  else if (pp_pre(k, L"комплекты разборки")) v = kPpKinds[8];
  else if (pp_pre(k, L"комплект")) v = kPpKinds[6];
  else if (pp_pre(k, L"комплекс")) v = kPpKinds[7];
  else if (pp_pre(k, L"документ")) v = kPpKinds[9];
  if (!v) {
    tn_cpy(out, cap, in);
    return 0;
  }
  int changed = wcscmp(in, v) != 0;
  tn_cpy(out, cap, v);
  return changed;
}

/* вид, который заводится без наименования, — по одному обозначению */
static int pp_kind_noname(const wchar_t *kind) { return !wcscmp(kind, kPpKinds[3]) || !wcscmp(kind, kPpKinds[4]); }

/* строка раздела спецификации: вид в sect; *skip — 1 документация, 2 материалы; 0 — не раздел */
static int pp_section(const wchar_t *cell, wchar_t *sect, int cap, int *skip) {
  wchar_t k[80];
  so_low(cell, k, 80);
  if (!wcscmp(k, L"документация")) {
    sect[0] = 0, *skip = 1;
    return 1;
  }
  static const wchar_t *const kSect[] = {L"комплексы", L"сборочные единицы", L"детали", L"стандартные изделия",
                                         L"прочие изделия", L"материалы", L"комплекты"};
  for (size_t i = 0; i < sizeof(kSect) / sizeof(kSect[0]); i++)
    if (!wcscmp(k, kSect[i])) {
      pp_kind(kSect[i], sect, cap);
      *skip = i == 5 ? 2 : 0;
      return 1;
    }
  return 0;
}

/* ---- размеры заготовки --------------------------------------------------------------- */

/* «Ø40x265», «D40 L265», «10х280х440», «S=10; B=280; L=440» → «D=40;L=265», «S=10;B=280;L=440»
   (так их ждёт функция «Союза»: D, L, S, B, H в мм через «;»); не понял — как есть, 0 */
static int pp_sizes(const wchar_t *in, wchar_t *out, int cap) {
  wchar_t key[6];
  double val[6];
  int n = 0, nokey = 0;
  wchar_t pend = 0;
  const wchar_t *p = in;
  while (*p && n < 6) {
    wchar_t c = *p;
    wchar_t k = 0;
    if (c == L'Ø' || c == L'ø' || c == L'⌀' || c == L'D' || c == L'd' || c == L'Д' || c == L'д') k = L'D';
    else if (c == L'L' || c == L'l' || c == L'Л' || c == L'л') k = L'L';
    else if (c == L'S' || c == L's' || c == L'Т' || c == L'т') k = L'S';
    else if (c == L'B' || c == L'b' || c == L'В' || c == L'в' || c == L'Ш' || c == L'ш') k = L'B';
    else if (c == L'H' || c == L'h' || c == L'Н' || c == L'н') k = L'H';
    if (k && !tn_isletter(p[1])) {
      pend = k;
      p++;
      continue;
    }
    if (tn_isletter(c) && !(c == L'x' || c == L'х' || c == L'X' || c == L'Х')) {
      tn_cpy(out, cap, in); /* слово — не размеры */
      return 0;
    }
    double v;
    int len, sep;
    if (tn_num(p, &v, &len, &sep)) {
      key[n] = pend;
      val[n++] = v;
      if (!pend) nokey++;
      pend = 0;
      p += len;
      continue;
    }
    p++;
  }
  if (!n) {
    tn_cpy(out, cap, in);
    return 0;
  }
  if (nokey) { /* без букв: «Ø40x265» — диаметр и длина, «10х280х440» — толщина, ширина, длина */
    if (n == 2 && key[0] == L'D' && !key[1]) key[1] = L'L';
    else if (n == 3 && nokey == 3) key[0] = L'S', key[1] = L'B', key[2] = L'L';
    else if (n == 2 && nokey == 2 && (pp_has(in, L"Ø") || pp_has(in, L"⌀"))) key[0] = L'D', key[1] = L'L';
    for (int i = 0; i < n; i++)
      if (!key[i]) {
        tn_cpy(out, cap, in);
        return 0;
      }
  }
  out[0] = 0;
  for (int i = 0; i < n; i++) {
    wchar_t t[40] = {key[i], L'=', 0};
    tn_catnum(t, 40, val[i]);
    if (i) tn_cat(out, cap, L";");
    tn_cat(out, cap, t);
  }
  return 1;
}

/* размеры уже в виде «D=40;L=265» — то, что примет функция «Союза»; пусто — тоже да */
static int pp_sizes_ok(const wchar_t *s) {
  for (const wchar_t *p = s; *p;) {
    while (*p == L' ' || *p == L';') p++;
    if (!*p) break;
    if (!wcschr(L"DLSBHdlsbh", *p)) return 0;
    p++;
    while (*p == L' ') p++;
    if (*p++ != L'=') return 0;
    while (*p == L' ') p++;
    double v;
    int len, sep;
    if (!tn_num(p, &v, &len, &sep)) return 0;
    p += len;
    while (*p == L' ') p++;
    if (*p && *p != L';') return 0;
  }
  return 1;
}

/* ---- вывод --------------------------------------------------------------------------- */

typedef struct {
  const PpIn *in;
  PpOut *out;
  wchar_t sect[40]; /* вид из последней строки раздела спецификации */
  int skip;         /* 1 — раздел «Документация», 2 — «Материалы» */
  const wchar_t *asmb; /* сборка состава: из текста (asmbBuf) или выделенная в окне */
  wchar_t asmbBuf[PP_CELL];
  int link;         /* строки спецификации идут и в состав */
  long lastNum;     /* последний номер операции — для строк без номера */
  wchar_t lastPart[PP_CELL];
} PpCtx;

static void pp_emit(PpCtx *x, int sheet, const wchar_t *c0, const wchar_t *c1, const wchar_t *c2, const wchar_t *c3,
                    const wchar_t *c4, const wchar_t *c5, const wchar_t *c6) {
  const wchar_t *c[PP_COLS] = {c0 ? c0 : L"", c1 ? c1 : L"", c2 ? c2 : L"", c3 ? c3 : L"",
                               c4 ? c4 : L"", c5 ? c5 : L"", c6 ? c6 : L""};
  if (x->in->put) x->in->put(x->in->ctx, sheet, c);
  x->out->rows[sheet]++;
}

/* одна позиция спецификации или списка: изделие и строка состава */
static void pp_spec_row(PpCtx *x, const wchar_t *des, const wchar_t *name, const wchar_t *kind, const wchar_t *mat,
                        const wchar_t *mass, const wchar_t *parent, const wchar_t *qty, const wchar_t *pos) {
  wchar_t d[PP_CELL], n[PP_CELL], k[80], q[40], m[40];
  tn_cpy(d, PP_CELL, des);
  tn_cpy(n, PP_CELL, name);
  pp_clean(d);
  pp_clean(n);
  if (!d[0] && !qty[0] && !parent[0] && pp_section(n, x->sect, 40, &x->skip)) return;
  if (!d[0] && !n[0]) return;
  if (x->skip == 1 || pp_is_doc(d, n)) {
    x->out->docs++;
    return;
  }
  if (x->skip == 2) {
    x->out->mats++;
    return;
  }
  pp_kind(kind[0] ? kind : x->sect, k, 80);
  if (!d[0]) { /* стандартное или прочее изделие: обозначение — его наименование */
    int s = pp_std_name(n);
    if (!k[0] && s) tn_cpy(k, 80, kPpKinds[s == 1 ? 3 : 4]);
    if (pp_kind_noname(k) || s) {
      tn_cpy(d, PP_CELL, n);
      n[0] = 0;
    }
  }
  q[0] = m[0] = 0;
  if (qty[0] && !pp_num(qty, 1, q, 40) && !wcspbrk(qty, L"0123456789")) q[0] = 0; /* «—» — без количества */
  if (mass[0] && !pp_num(mass, 1, m, 40) && !wcspbrk(mass, L"0123456789")) m[0] = 0;
  if (n[0] || pp_kind_noname(k) || !d[0])
    pp_emit(x, PK_ITEMS, d, n, k, mat, m, NULL, NULL);
  const wchar_t *par = parent[0] ? parent : x->asmb;
  if (x->link && d[0]) {
    if (!par[0]) x->out->noParent++;
    else if (!x->out->assembly[0]) tn_cpy(x->out->assembly, PP_CELL, par);
    pp_emit(x, PK_LINKS, par, d, q, pos, NULL, NULL, NULL);
  }
}

/* код операции по названию: «Токарная» → «4110 Токарная» (правила маршрута, потом
   справочник 1С); не нашлось — название как есть */
static void pp_op_text(const wchar_t *name, const wchar_t *code, wchar_t *out, int cap) {
  wchar_t nm[120];
  tn_cpy(nm, 120, name);
  pp_clean(nm);
  if (tn_isdig(nm[0])) { /* уже с кодом: «4110 Токарная»; один код «4110» — название из справочника 1С */
    int d = 0;
    while (tn_isdig(nm[d])) d++;
    tn_cpy(out, cap, nm);
    if (nm[d] || d > 4) return;
    wchar_t num[8] = L"0000";
    for (int i = 0; i < d; i++) num[4 - d + i] = nm[i];
    for (int i = 0; i < OPS_1C_N; i++)
      if (!wcscmp(kOps1c[i].num, num)) {
        tn_cpy(out, cap, kOps1c[i].name);
        return;
      }
    return;
  }
  if (code && code[0]) {
    tn_cpy(out, cap, code);
    if (nm[0]) tn_cat(out, cap, L" "), tn_cat(out, cap, nm);
    return;
  }
  SoItem it;
  memset(&it, 0, sizeof(it));
  tn_cpy(it.op, 64, nm);
  if (so_map(&it)) {
    so_clip_text(&it, out, cap);
    return;
  }
  wchar_t key[120];
  so_low(nm, key, 120);
  wchar_t *br = wcschr(key, L'(');
  if (br) {
    *br = 0;
    so_low(key, key, 120);
  }
  for (int i = 0; i < OPS_1C_N; i++)
    if (!wcscmp(kOps1c[i].key, key)) {
      tn_cpy(out, cap, kOps1c[i].name);
      return;
    }
  tn_cpy(out, cap, nm);
}

/* строка операции; номер — свой или следующий за прошлым (через 5) */
static void pp_op_row(PpCtx *x, const wchar_t *part, const wchar_t *num, const wchar_t *op, const wchar_t *area,
                      const wchar_t *equip, const wchar_t *tpz, const wchar_t *tsht) {
  wchar_t nb[16];
  if (wcscmp(part, x->lastPart)) x->lastNum = 0, tn_cpy(x->lastPart, PP_CELL, part);
  long v = 0;
  const wchar_t *p = num;
  while (*p == L' ') p++;
  int d = 0;
  while (tn_isdig(p[d])) v = v * 10 + (p[d] - L'0'), d++;
  if (d && d <= 4) x->lastNum = v;
  else v = x->lastNum = x->lastNum + 5;
  pp_num3(v, nb, 16);
  if (!part[0]) x->out->noPart++;
  else if (!x->out->part[0]) tn_cpy(x->out->part, PP_CELL, part);
  pp_emit(x, PK_OPS, part, nb, op, area, equip, tpz, tsht);
}

/* ---- таблица ------------------------------------------------------------------------- */

typedef struct {
  int n;
  wchar_t *c[PP_MAXC];
} PpRow;

/* Строки и ячейки: sep — Tab или «;»; ячейку с переводом строки, табуляцией или кавычкой
   Excel берёт в кавычки ("" внутри — сама кавычка). sep 0 — строка целиком одна ячейка.
   Режет s на месте; ячейки — без пробелов по краям. */
static int pp_split(wchar_t *s, wchar_t sep, PpRow *rows, int max) {
  int n = 0;
  wchar_t *p = s;
  while (*p && n < max) {
    PpRow *r = &rows[n];
    r->n = 0;
    for (;;) {
      wchar_t *cell = p, *w = p;
      if (sep && *p == L'"') {
        p++;
        while (*p) {
          if (*p == L'"') {
            if (p[1] == L'"') {
              *w++ = L'"';
              p += 2;
              continue;
            }
            p++;
            break;
          }
          *w++ = *p == L'\n' ? L' ' : *p; /* перевод строки внутри ячейки — пробелом */
          p++;
        }
        while (*p && *p != sep && *p != L'\n') p++;
      } else {
        while (*p && *p != sep && *p != L'\n') *w++ = *p++;
      }
      wchar_t end = *p;
      *w = 0;
      if (r->n < PP_MAXC) r->c[r->n++] = pp_trim(cell);
      if (end) p++;
      if (!sep || end != sep) break;
    }
    n++;
  }
  return n;
}

static int pp_row_empty(const PpRow *r) {
  for (int i = 0; i < r->n; i++)
    if (r->c[i][0]) return 0;
  return 1;
}

static const wchar_t *pp_at(const PpRow *r, int col) { return col >= 0 && col < r->n ? r->c[col] : L""; }

/* поля шапки */
enum { PF_NONE, PF_SKIP, PF_DES, PF_NAME, PF_KIND, PF_MAT, PF_MASS, PF_PARENT, PF_QTY, PF_POS, PF_NUM, PF_OPNUM,
       PF_OP, PF_OPCODE, PF_AREA, PF_EQUIP, PF_TPZ, PF_TSHT, PF_NORM, PF_SIZES, PF_N };
static const wchar_t *const kPpField[PF_N] = {L"",        L"",        L"обозначение", L"наименование", L"вид",
                                              L"материал", L"масса",  L"сборка",      L"кол-во",       L"позиция",
                                              L"№",       L"№ опер.", L"операция",    L"код операции", L"участок",
                                              L"оборудование", L"Тпз", L"Тшт",       L"норма расхода", L"размеры"};

/* шапка столбца → поле; *hours — нормы в часах («Тшт, ч») */
static int pp_field(const wchar_t *cell, int *hours) {
  wchar_t h[120];
  so_low(cell, h, 120);
  *hours = 0;
  if (!h[0] || wcslen(h) > 40) return PF_NONE; /* шапка — короткая */
  if (pp_pre(h, L"формат") || pp_pre(h, L"зона") || pp_pre(h, L"примеч") || pp_has(h, L"п/п") || pp_pre(h, L"ед.") ||
      pp_pre(h, L"ед ") || !wcscmp(h, L"ед") || pp_pre(h, L"код 1с") || pp_has(h, L"содерж") || pp_has(h, L"переход"))
    return PF_SKIP;
  if (pp_has(h, L"код") && pp_has(h, L"опер")) return PF_OPCODE;
  if ((pp_pre(h, L"№") || pp_pre(h, L"номер") || pp_pre(h, L"no")) && pp_has(h, L"опер")) return PF_OPNUM;
  if (pp_has(h, L"опер")) return PF_OP;
  int tp = pp_has(h, L"тпз") || pp_has(h, L"т пз") || pp_has(h, L"т.пз") || pp_has(h, L"т п.з") || pp_has(h, L"подготов");
  int ts = pp_has(h, L"тшт") || pp_has(h, L"т шт") || pp_has(h, L"т.шт") || pp_has(h, L"штучн");
  if (tp || ts) {
    *hours = (pp_has(h, L" ч") && !pp_has(h, L" чел")) || pp_has(h, L"(ч") || pp_has(h, L"час");
    return tp ? PF_TPZ : PF_TSHT;
  }
  if (pp_has(h, L"норм") || pp_has(h, L"расход") || (pp_has(h, L"масс") && pp_has(h, L"заготов"))) return PF_NORM;
  if (pp_has(h, L"размер") || pp_has(h, L"габарит")) return PF_SIZES;
  if (pp_has(h, L"сортамент") || pp_has(h, L"материал") || pp_pre(h, L"марка")) return PF_MAT;
  if (pp_pre(h, L"масса") || pp_pre(h, L"вес")) return PF_MASS;
  if (pp_has(h, L"участ") || pp_pre(h, L"цех") || pp_has(h, L"подраздел")) return PF_AREA;
  if (pp_has(h, L"оборуд") || pp_has(h, L"станок") || pp_has(h, L"станк") || pp_pre(h, L"модель")) return PF_EQUIP;
  if (pp_has(h, L"наимен") || pp_has(h, L"назван")) return PF_NAME;
  if (pp_pre(h, L"вид") || pp_has(h, L"раздел") || pp_pre(h, L"тип") || pp_pre(h, L"класс")) return PF_KIND;
  if (pp_has(h, L"входящ") || pp_has(h, L"комплектующ")) return PF_DES;
  if (pp_has(h, L"сборк") || pp_has(h, L"сборочн") || pp_has(h, L"куда вход") || pp_has(h, L"входит в") ||
      pp_has(h, L"родител") || pp_has(h, L"головн"))
    return PF_PARENT;
  if (pp_has(h, L"обозн") || pp_has(h, L"децимал") || pp_has(h, L"шифр") || pp_pre(h, L"деталь") ||
      pp_pre(h, L"изделие") || pp_pre(h, L"чертеж"))
    return PF_DES;
  if (pp_pre(h, L"кол") || !wcscmp(h, L"шт") || pp_pre(h, L"к-во")) return PF_QTY;
  if (pp_pre(h, L"поз") || pp_pre(h, L"№ поз")) return PF_POS;
  if (!wcscmp(h, L"№") || !wcscmp(h, L"n") || !wcscmp(h, L"no") || !wcscmp(h, L"номер")) return PF_NUM;
  return PF_NONE;
}

/* Строка шапки: не меньше двух узнанных столбцов, и узнано больше половины непустых.
   col[поле] — номер столбца (-1 — нет), hours — нормы этого столбца в часах. */
static int pp_head(const PpRow *r, int *col, int *hours) {
  int rec = 0, real = 0, ne = 0;
  int f[PP_MAXC], hr[PP_MAXC];
  for (int i = 0; i < r->n; i++) {
    f[i] = PF_NONE, hr[i] = 0;
    if (!r->c[i][0]) continue;
    ne++;
    if (pp_cell_des(r->c[i])) return 0; /* обозначение — это данные */
    f[i] = pp_field(r->c[i], &hr[i]);
    rec += f[i] != PF_NONE;
    real += f[i] > PF_SKIP;
  }
  if (ne < 2 || rec < 2 || !real || rec * 2 <= ne) return 0;
  for (int k = 0; k < PF_N; k++) col[k] = -1;
  for (int i = 0; i < r->n; i++)
    if (f[i] > PF_SKIP && col[f[i]] < 0) {
      col[f[i]] = i;
      hours[i] = hr[i];
    }
  /* «№» — номер операции рядом с операциями, иначе позиция */
  if (col[PF_NUM] >= 0) {
    int ops = col[PF_OP] >= 0 || col[PF_OPCODE] >= 0 || col[PF_TPZ] >= 0 || col[PF_TSHT] >= 0;
    int *to = ops ? &col[PF_OPNUM] : &col[PF_POS];
    if (*to < 0) *to = col[PF_NUM];
  }
  return 1;
}

static void pp_cols_note(PpOut *o, const int *col) {
  o->cols[0] = 0;
  for (int k = PF_DES; k < PF_N; k++)
    if (col[k] >= 0 && k != PF_NUM) {
      if (o->cols[0]) tn_cat(o->cols, 240, L", ");
      tn_cat(o->cols, 240, kPpField[k]);
    }
}

/* строка над шапкой: «Спецификация АБВГ.123456.000 Редуктор» — обозначение и наименование */
static int pp_title(const PpRow *r, wchar_t *des, wchar_t *name) {
  wchar_t s[SR_Q] = L"";
  for (int i = 0; i < r->n; i++)
    if (r->c[i][0]) {
      if (s[0]) tn_cat(s, SR_Q, L" ");
      tn_cat(s, SR_Q, r->c[i]);
    }
  wchar_t p[SR_Q];
  sr_prep(s, p, SR_Q);
  int b, e;
  if (!pp_find_des(p, &b, &e)) return 0;
  pp_ncpy(des, PP_CELL, p + b, e - b);
  tn_cpy(name, PP_CELL, p + e);
  pp_clean(name);
  return 1;
}

/* таблица с шапкой в строке h */
static void pp_table_head(PpCtx *x, PpRow *rows, int n, int h, const int *col, const int *hours) {
  const PpIn *in = x->in;
  wchar_t tdes[PP_CELL] = L"", tname[PP_CELL] = L"";
  for (int i = 0; i < h; i++)
    if (!tdes[0]) pp_title(&rows[i], tdes, tname);
  int ops = col[PF_OP] >= 0 || col[PF_OPCODE] >= 0 || col[PF_TPZ] >= 0 || col[PF_TSHT] >= 0 || col[PF_OPNUM] >= 0;
  int blanks = !ops && (col[PF_NORM] >= 0 || col[PF_SIZES] >= 0);
  int spec = !ops && !blanks &&
             (col[PF_QTY] >= 0 || col[PF_PARENT] >= 0 || col[PF_POS] >= 0 || in->sheet == PK_LINKS);
  pp_cols_note(x->out, col);
  if (ops) {
    int opCol = col[PF_OP] >= 0 ? col[PF_OP] : col[PF_NAME]; /* «№ | Наименование | Станок» — маршрут */
    int nameCol = col[PF_OP] >= 0 ? col[PF_NAME] : -1;
    double kz = col[PF_TPZ] >= 0 && hours[col[PF_TPZ]] ? 60 : 1; /* нормы в часах — в минуты */
    double ks = col[PF_TSHT] >= 0 && hours[col[PF_TSHT]] ? 60 : 1;
    for (int i = h + 1; i < n; i++) {
      const PpRow *r = &rows[i];
      if (pp_row_empty(r)) continue;
      const wchar_t *des = pp_at(r, col[PF_DES]);
      if (!des[0]) des = pp_at(r, col[PF_PARENT]);
      if (!des[0]) des = tdes[0] ? tdes : in->part;
      wchar_t op[PP_CELL], tz[40] = L"", tsh[40] = L"";
      pp_op_text(pp_at(r, opCol), pp_at(r, col[PF_OPCODE]), op, PP_CELL);
      if (!op[0]) continue;
      if (pp_at(r, col[PF_TPZ])[0]) pp_num(pp_at(r, col[PF_TPZ]), kz, tz, 40);
      if (pp_at(r, col[PF_TSHT])[0]) pp_num(pp_at(r, col[PF_TSHT]), ks, tsh, 40);
      if (nameCol >= 0 && pp_at(r, nameCol)[0] && pp_at(r, col[PF_DES])[0])
        pp_spec_row(x, des, pp_at(r, nameCol), pp_at(r, col[PF_KIND]), pp_at(r, col[PF_MAT]), pp_at(r, col[PF_MASS]),
                    L"", L"", L"");
      pp_op_row(x, des, pp_at(r, col[PF_OPNUM]), op, pp_at(r, col[PF_AREA]), pp_at(r, col[PF_EQUIP]), tz, tsh);
    }
    return;
  }
  if (blanks) {
    for (int i = h + 1; i < n; i++) {
      const PpRow *r = &rows[i];
      if (pp_row_empty(r)) continue;
      const wchar_t *des = pp_at(r, col[PF_DES]);
      if (!des[0]) des = tdes[0] ? tdes : in->part;
      const wchar_t *mat = pp_at(r, col[PF_MAT]), *norm = pp_at(r, col[PF_NORM]), *sz = pp_at(r, col[PF_SIZES]);
      if (!mat[0] && !norm[0] && !sz[0]) continue;
      if (pp_at(r, col[PF_NAME])[0] && pp_at(r, col[PF_DES])[0])
        pp_spec_row(x, des, pp_at(r, col[PF_NAME]), pp_at(r, col[PF_KIND]), mat, pp_at(r, col[PF_MASS]), L"", L"",
                    L"");
      wchar_t nb[40] = L"", sb[PP_CELL] = L"";
      if (norm[0]) pp_num(norm, 1, nb, 40);
      if (sz[0]) pp_sizes(sz, sb, PP_CELL);
      if (!des[0]) x->out->noPart++;
      else if (!x->out->part[0]) tn_cpy(x->out->part, PP_CELL, des);
      pp_emit(x, PK_BLANKS, des, mat, nb, sb, NULL, NULL, NULL);
    }
    return;
  }
  /* спецификация или изделия; сборка — над шапкой, иначе выделенная в окне */
  x->link = spec;
  if (spec && tdes[0] && col[PF_PARENT] < 0) {
    tn_cpy(x->asmbBuf, PP_CELL, tdes);
    x->asmb = x->asmbBuf;
    if (tname[0]) pp_emit(x, PK_ITEMS, tdes, tname, kPpKinds[0], NULL, NULL, NULL, NULL);
  }
  for (int i = h + 1; i < n; i++) {
    const PpRow *r = &rows[i];
    if (pp_row_empty(r)) continue;
    const wchar_t *des = pp_at(r, col[PF_DES]), *name = pp_at(r, col[PF_NAME]);
    if (col[PF_DES] < 0 && col[PF_NAME] < 0) continue;
    /* строка раздела — одно слово в любом столбце («Детали» иногда в «Обозначении») */
    int ne = 0;
    const wchar_t *only = L"";
    for (int c = 0; c < r->n; c++)
      if (r->c[c][0]) ne++, only = r->c[c];
    if (ne == 1 && pp_section(only, x->sect, 40, &x->skip)) continue;
    pp_spec_row(x, des, name, pp_at(r, col[PF_KIND]), pp_at(r, col[PF_MAT]), pp_at(r, col[PF_MASS]),
                pp_at(r, col[PF_PARENT]), pp_at(r, col[PF_QTY]), pp_at(r, col[PF_POS]));
  }
}

/* Таблица без шапки: столбцы по содержимому — где обозначения, где слова, где количество.
   Только для изделий и состава; не вышло — 0. */
static int pp_table_guess(PpCtx *x, PpRow *rows, int n) {
  int cnt[PP_MAXC][3]; /* обозначение, число, слова */
  memset(cnt, 0, sizeof(cnt));
  int nr = 0, maxc = 0;
  for (int i = 0; i < n && nr < 300; i++) {
    const PpRow *r = &rows[i];
    if (pp_row_empty(r)) continue;
    nr++;
    if (r->n > maxc) maxc = r->n;
    for (int c = 0; c < r->n; c++) {
      const wchar_t *v = r->c[c];
      if (!v[0]) continue;
      if (pp_cell_des(v)) cnt[c][0]++;
      else if (pp_is_qty(v)) cnt[c][1]++;
      else if (pp_has(v, L"ГОСТ") || pp_has(v, L"ОСТ ")) cnt[c][0]++, cnt[c][2]++; /* стандартное — без обозначения */
      else {
        int letters = 0;
        for (const wchar_t *p = v; *p; p++) letters += tn_isletter(*p);
        if (letters >= 3) cnt[c][2]++;
      }
    }
  }
  if (nr < 1 || maxc < 2) return 0;
  int desC = -1, nameC = -1, qtyC = -1, posC = -1;
  for (int c = 0; c < maxc; c++)
    if (cnt[c][0] * 10 >= nr * 4 && (desC < 0 || cnt[c][0] > cnt[desC][0])) desC = c;
  if (desC < 0) return 0;
  for (int c = 0; c < maxc; c++)
    if (c != desC && cnt[c][2] * 2 >= nr && nameC < 0) nameC = c;
  for (int c = desC + 1; c < maxc; c++)
    if (c != nameC && cnt[c][1] * 2 >= nr && qtyC < 0) qtyC = c;
  for (int c = 0; c < desC; c++)
    if (c != nameC && cnt[c][1] * 2 >= nr) posC = c;
  if (nameC < 0 && qtyC < 0) return 0;
  x->link = qtyC >= 0 || x->in->sheet == PK_LINKS;
  int col[PF_N];
  for (int k = 0; k < PF_N; k++) col[k] = -1;
  col[PF_DES] = desC, col[PF_NAME] = nameC, col[PF_QTY] = qtyC, col[PF_POS] = posC;
  pp_cols_note(x->out, col);
  for (int i = 0; i < n; i++) {
    const PpRow *r = &rows[i];
    if (pp_row_empty(r)) continue;
    const wchar_t *des = pp_at(r, desC), *name = pp_at(r, nameC);
    if (!pp_cell_des(des) && !name[0] && des[0]) name = des, des = L""; /* «Болт … ГОСТ …» в столбце обозначений */
    else if (!pp_cell_des(des) && des[0] && pp_std_name(des) && !pp_std_name(name)) name = des, des = L"";
    int ne = 0;
    const wchar_t *only = L"";
    for (int c = 0; c < r->n; c++)
      if (r->c[c][0]) ne++, only = r->c[c];
    if (ne == 1 && pp_section(only, x->sect, 40, &x->skip)) continue;
    pp_spec_row(x, des, name, L"", L"", L"", L"", pp_at(r, qtyC), pp_at(r, posC));
  }
  return 1;
}

/* строки описания для «Союза»: ИЗДЕЛИЕ, СОСТАВ, ЗАГОТОВКА, ТП, ЧЕРТЕЖ (ПАПКА — выбирают в окне) */
static int pp_desc_kind(const wchar_t *c0) {
  if (!wcscmp(c0, L"ИЗДЕЛИЕ")) return 1;
  if (!wcscmp(c0, L"СОСТАВ")) return 2;
  if (!wcscmp(c0, L"ЗАГОТОВКА")) return 3;
  if (!wcscmp(c0, L"ТП")) return 4;
  if (!wcscmp(c0, L"ПАПКА") || !wcscmp(c0, L"ПАПКА_ТП")) return 5;
  if (!wcscmp(c0, L"ЧЕРТЕЖ")) return 6;
  return 0;
}

static void pp_desc(PpCtx *x, PpRow *rows, int n) {
  for (int i = 0; i < n; i++) {
    const PpRow *r = &rows[i];
    int k = r->n ? pp_desc_kind(r->c[0]) : 0;
    const wchar_t *a = pp_at(r, 1), *b = pp_at(r, 2), *c = pp_at(r, 3), *d = pp_at(r, 4), *e = pp_at(r, 5);
    if (k == 1) {
      wchar_t kind[80];
      pp_kind(c, kind, 80);
      pp_emit(x, PK_ITEMS, a, b, kind, d, e, NULL, pp_at(r, 6)); /* шестое — признак покупки */
    } else if (k == 2) {
      if (!x->out->assembly[0]) tn_cpy(x->out->assembly, PP_CELL, a);
      pp_emit(x, PK_LINKS, a, b, c, d, NULL, NULL, NULL);
    } else if (k == 3) {
      pp_emit(x, PK_BLANKS, a, b, c, d, NULL, NULL, NULL);
    } else if (k == 4) {
      wchar_t op[PP_CELL];
      pp_op_text(c, NULL, op, PP_CELL);
      pp_op_row(x, a, b, op, d, e, pp_at(r, 6), pp_at(r, 7));
    } else if (k == 6 && a[0] && b[0]) { /* чертёж — столбцом «Чертёж (файл)» к изделию */
      pp_emit(x, PK_ITEMS, a, NULL, NULL, NULL, NULL, b, NULL);
    }
  }
}

/* ---- текст без таблицы --------------------------------------------------------------- */

typedef struct {
  wchar_t num[8], op[120], area[40], equip[120], tpz[24], tsht[24];
} PpOp;

/* конец слова: пробел, Tab, «;» или запятая, если она не внутри числа («3,5») */
static int pp_wend(const wchar_t *q) {
  if (!*q || *q == L' ' || *q == L'\t' || *q == L';') return 1;
  return *q == L',' && !(q[-1] >= L'0' && q[-1] <= L'9' && tn_isdig(q[1]));
}

static int pp_is_model(const wchar_t *w, int len) { /* «16К20», «2Н135», «DMU50»: буквы и цифры вместе */
  int dig = 0, let = 0;
  for (int i = 0; i < len; i++) dig += tn_isdig(w[i]), let += tn_isletter(w[i]);
  return dig && let && len >= 3;
}

/* Операция словами: «005 Токарная 16К20 Тпз 12 Тшт 3,5», «010 Сверлильная, уч. 31, станок 2Н135».
   0 — строка не операция (нет номера или за номером не название операции). */
static int pp_op_line(const wchar_t *s, PpOp *o) {
  memset(o, 0, sizeof(*o));
  while (*s == L' ' || *s == L'\t' || *s == L'-' || *s == L'•' || *s == L'*') s++;
  int d = 0;
  long v = 0;
  while (tn_isdig(s[d])) v = v * 10 + (s[d] - L'0'), d++;
  if (d < 1 || d > 4 || (s[d] != L' ' && s[d] != L'.' && s[d] != L'\t')) return 0;
  const wchar_t *p = s + d;
  while (*p == L' ' || *p == L'.' || *p == L'\t') p++;
  if (!pp_upper_cyr(*p)) return 0;
  /* название — слова без цифр до первого ключевого слова, запятой или точки */
  static const wchar_t *const kStop[] = {L"тпз", L"тшт", L"то", L"тв", L"уч", L"уч.", L"участок", L"цех", L"станок",
                                         L"оборудование", L"на", L"мин", L"мин.", L"—", L"-", L"–"};
  wchar_t name[120] = L"";
  const wchar_t *q = p;
  for (;;) {
    while (*q == L' ') q++;
    const wchar_t *w = q;
    while (*q && *q != L' ' && *q != L',' && *q != L';' && *q != L'\t') q++;
    int len = (int)(q - w);
    if (!len) break;
    wchar_t lw[40];
    pp_ncpy(lw, 40, w, len);
    so_low(lw, lw, 40);
    int stop = 0, dig = 0;
    for (size_t i = 0; i < sizeof(kStop) / sizeof(kStop[0]) && !stop; i++) stop = !wcscmp(lw, kStop[i]);
    for (int i = 0; i < len; i++) dig |= tn_isdig(w[i]);
    if (stop || dig || pp_pre(lw, L"тпз") || pp_pre(lw, L"тшт")) {
      q = w;
      break;
    }
    if (name[0]) tn_cat(name, 120, L" ");
    int k = (int)wcslen(name);
    pp_ncpy(name + k, 120 - k, w, len);
    if (w[len - 1] == L'.' || *q == L',' || *q == L';') {
      if (*q == L',' || *q == L';') q++;
      break;
    }
  }
  pp_clean(name);
  if (wcslen(name) < 4) return 0;
  /* известная операция или прилагательное «…ая» — иначе это не операция («1 Корпус …») */
  wchar_t op[PP_CELL];
  pp_op_text(name, NULL, op, PP_CELL);
  wchar_t first[40];
  int fl = 0;
  while (name[fl] && name[fl] != L' ' && fl < 39) first[fl] = name[fl], fl++;
  first[fl] = 0;
  if (!tn_isdig(op[0]) && !(fl >= 5 && first[fl - 1] == L'я' && first[fl - 2] == L'а')) return 0;
  tn_cpy(o->op, 120, op);
  pp_num3(v, o->num, 8);
  /* остальное: нормы, участок, оборудование */
  const wchar_t *r = q;
  wchar_t want = 0; /* что ждёт следующее слово: z — Тпз, s — Тшт, a — участок, e — оборудование */
  while (*r) {
    while (*r == L' ' || *r == L',' || *r == L';' || *r == L'\t' || *r == L'=' || *r == L':') r++;
    const wchar_t *w = r;
    while (!pp_wend(r) || r == w) {
      if (!*r) break;
      r++;
    }
    int len = (int)(r - w);
    if (!len) break;
    wchar_t lw[60];
    pp_ncpy(lw, 60, w, len);
    so_low(lw, lw, 60);
    if (!wcscmp(lw, L"—") || !wcscmp(lw, L"–")) break; /* дальше — причина */
    double num;
    int nl, sep;
    const wchar_t *val = NULL;
    wchar_t key = 0;
    if (pp_pre(lw, L"тпз")) key = L'z', val = w + 3;
    else if (pp_pre(lw, L"тшт")) key = L's', val = w + 3;
    if (key) {
      while (*val == L'=' || *val == L':' || *val == L'≈') val++;
      if (tn_num(val, &num, &nl, &sep)) tn_numstr(num, key == L'z' ? o->tpz : o->tsht, 24);
      else want = key;
      continue;
    }
    if (want == L'z' || want == L's') {
      const wchar_t *vv = w;
      while (*vv == L'≈' || *vv == L'~') vv++;
      if (tn_num(vv, &num, &nl, &sep)) tn_numstr(num, want == L'z' ? o->tpz : o->tsht, 24);
      want = 0;
      continue;
    }
    if (!wcscmp(lw, L"уч") || !wcscmp(lw, L"уч.") || !wcscmp(lw, L"участок")) {
      want = L'a';
      continue;
    }
    if (pp_pre(lw, L"уч.") && tn_isdig(w[3])) { /* «уч.31» */
      int k = 0;
      while (tn_isdig(w[3 + k]) && k < 39) o->area[k] = w[3 + k], k++;
      o->area[k] = 0;
      continue;
    }
    if (want == L'a') {
      int k = 0;
      while (k < len && tn_isdig(w[k]) && k < 39) o->area[k] = w[k], k++;
      o->area[k] = 0;
      want = 0;
      continue;
    }
    if (!wcscmp(lw, L"станок") || !wcscmp(lw, L"оборудование") || !wcscmp(lw, L"на")) {
      want = L'e';
      continue;
    }
    if (!wcscmp(lw, L"цех")) {
      want = L'c'; /* номер цеха — не участок, пропускаем */
      continue;
    }
    if (want == L'c') {
      want = 0;
      continue;
    }
    if ((want == L'e' || pp_is_model(w, len)) && !o->equip[0]) pp_ncpy(o->equip, 120, w, len);
    want = 0;
  }
  return 1;
}

/* маршрут из чата: «Лист: …», «Деталь: …», «005 Токарная. …» — разбирает soyuz_ops.c */
static int pp_route(PpCtx *x, const wchar_t *text) {
  SoRoute *r = (SoRoute *)malloc(sizeof(SoRoute));
  if (!r) return 0;
  int n = so_parse(text, r);
  if (!n) {
    free(r);
    return 0;
  }
  const wchar_t *part = r->des[0] ? r->des : x->in->part;
  if (r->des[0] && r->name[0]) { /* материал — как его заводит «Создать ТП»: «заготовка / марка» */
    wchar_t mat[420];
    if (r->blank[0] && r->mat[0]) {
      tn_cpy(mat, 420, r->blank);
      tn_cat(mat, 420, L" / ");
      tn_cat(mat, 420, r->mat);
    } else tn_cpy(mat, 420, r->mat);
    pp_emit(x, PK_ITEMS, r->des, r->name, kPpKinds[1], mat, NULL, NULL, NULL);
  }
  for (int i = 0; i < r->n; i++) {
    const SoItem *it = &r->it[i];
    wchar_t op[PP_CELL], tz[24] = L"", ts[24] = L"";
    so_clip_text(it, op, PP_CELL);
    if (it->tpz >= 0) tn_numstr(it->tpz, tz, 24);
    if (it->tsht >= 0) tn_numstr(it->tsht, ts, 24);
    pp_op_row(x, part, it->num, op, NULL, NULL, tz, ts);
  }
  free(r);
  return n;
}

/* пробелы схлопнуты, тире — как есть: «7798-70 — 8 шт» не должно склеиться в «7798-70-8» */
static int pp_squeeze(const wchar_t *in, wchar_t *line, int cap) {
  int L = 0;
  for (const wchar_t *q = in; *q && L < cap - 1; q++) {
    wchar_t c = *q == L'\t' || *q == 0xA0 ? L' ' : *q;
    if (c != L' ' || (L && line[L - 1] != L' ')) line[L++] = c;
  }
  while (L && line[L - 1] == L' ') L--;
  line[L] = 0;
  return L;
}

/* количество в конце строки списка: «2», «— 8 шт», «x2»; возвращает, где кончается
   остальное (L — количества нет) */
static int pp_tail_qty(const wchar_t *line, int from, int L, wchar_t *qty, int cap) {
  qty[0] = 0;
  int t = L, ws = L;
  for (int pass = 0; pass < 2; pass++) {
    ws = t;
    while (ws > from && line[ws - 1] != L' ') ws--;
    if (pass == 0 && !wcsncmp(line + ws, L"шт", 2) && (t - ws == 2 || (t - ws == 3 && line[ws + 2] == L'.'))) {
      t = ws;
      while (t > from && line[t - 1] == L' ') t--;
      continue;
    }
    break;
  }
  wchar_t last[40];
  pp_ncpy(last, 40, line + ws, t - ws);
  const wchar_t *lq = last;
  if (*lq == L'x' || *lq == L'х' || *lq == L'×') lq++;
  if (ws <= from || !pp_is_qty(lq)) return L;
  pp_num(lq, 1, qty, cap);
  return ws;
}

/* Список словами: «1 АБВГ.123456.002 Корпус 2 шт», «Болт М6х20 ГОСТ 7798-70 — 8 шт»,
   строки разделов; первая строка с обозначением и словом «сборка» или «спецификация» — сборка. */
static int pp_list(PpCtx *x, PpRow *rows, int n) {
  int got = 0, title = 0;
  /* в состав — если открыт лист «Состав» или хоть у одной строки есть количество */
  x->link = x->in->sheet == PK_LINKS;
  for (int i = 0; i < n && !x->link; i++)
    if (rows[i].n && rows[i].c[0][0]) {
      wchar_t line[SR_Q], q[40];
      int L = pp_squeeze(rows[i].c[0], line, SR_Q);
      x->link = pp_tail_qty(line, 0, L, q, 40) < L;
    }
  for (int i = 0; i < n; i++) {
    if (!rows[i].n || !rows[i].c[0][0]) continue;
    wchar_t line[SR_Q];
    int L = pp_squeeze(rows[i].c[0], line, SR_Q);
    wchar_t p[SR_Q], low[SR_Q];
    sr_prep(line, p, SR_Q);
    so_low(p, low, SR_Q);
    int b, e, has = pp_find_des(p, &b, &e);
    if (!got && !title && has && (pp_has(low, L"сборк") || pp_has(low, L"специфик") || pp_has(low, L"состав"))) {
      wchar_t name[PP_CELL];
      pp_ncpy(x->asmbBuf, PP_CELL, p + b, e - b);
      tn_cpy(name, PP_CELL, p + e);
      pp_clean(name);
      if (name[0]) pp_emit(x, PK_ITEMS, x->asmbBuf, name, kPpKinds[0], NULL, NULL, NULL, NULL);
      x->asmb = x->asmbBuf;
      title = 1;
      continue;
    }
    if (pp_section(line, x->sect, 40, &x->skip)) continue;
    /* позиция впереди («1 АБВГ…»), количество в конце («2», «— 8 шт», «x2») */
    int from = 0, to = L;
    wchar_t pos[16] = L"", qty[40] = L"";
    int d = 0;
    while (tn_isdig(line[d])) d++;
    if (d && d <= 3 && line[d] == L' ' && line[d + 1] && !tn_isdig(line[d + 1])) {
      pp_ncpy(pos, 16, line, d);
      from = d + 1;
    }
    to = pp_tail_qty(line, from, L, qty, 40);
    wchar_t mid[SR_Q];
    pp_ncpy(mid, SR_Q, line + from, to - from);
    pp_clean(mid);
    sr_prep(mid, p, SR_Q);
    has = pp_find_des(p, &b, &e);
    wchar_t des[PP_CELL] = L"", name[PP_CELL] = L"";
    if (has) {
      pp_ncpy(des, PP_CELL, p + b, e - b);
      tn_cpy(name, PP_CELL, p + e);
      pp_clean(name);
      if (!name[0]) pp_ncpy(name, PP_CELL, p, b), pp_clean(name);
    } else {
      tn_cpy(name, PP_CELL, mid);
      int letters = 0;
      for (const wchar_t *q = name; *q; q++) letters += tn_isletter(*q);
      if (letters < 3 || (!pp_std_name(name) && !x->sect[0])) continue; /* не позиция */
    }
    pp_spec_row(x, des, name, L"", L"", L"", L"", qty, pos);
    got++;
  }
  return got || title;
}

/* ---- разбор целиком ------------------------------------------------------------------ */

static int pp_parse(const wchar_t *text, const PpIn *in, PpOut *out) {
  memset(out, 0, sizeof(*out));
  PpIn inx = *in;
  if (!inx.assembly) inx.assembly = L"";
  if (!inx.part) inx.part = L"";
  in = &inx;
  PpCtx x;
  memset(&x, 0, sizeof(x));
  x.in = in;
  x.out = out;
  x.asmb = in->assembly;
  size_t len = text ? wcslen(text) : 0;
  if (!len) return 0;
  wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
  PpRow *rows = (PpRow *)calloc(PP_ROWS, sizeof(PpRow));
  if (!buf || !rows) {
    free(buf);
    free(rows);
    return 0;
  }
  /* чем делить: Tab — Excel и Word; «;» — CSV, если он в большинстве строк */
  int lines = 0, tabs = 0, semis = 0;
  for (const wchar_t *p = text; *p;) {
    const wchar_t *e = wcschr(p, L'\n');
    size_t l = e ? (size_t)(e - p) : wcslen(p);
    int t = 0, s = 0, any = 0;
    for (size_t i = 0; i < l; i++) t |= p[i] == L'\t', s |= p[i] == L';', any |= p[i] != L' ' && p[i] != L'\r';
    lines += any, tabs += t, semis += s;
    p = e ? e + 1 : p + l;
  }
  int route = pp_has(text, L"Маршрут обработки") || pp_has(text, L"\nЛист: ") || pp_pre(text, L"Лист: ") ||
              (pp_has(text, L"Деталь: ") && pp_has(text, L"Материал: "));
  wchar_t sep = tabs ? L'\t' : (!route && semis * 10 >= lines * 6 && semis >= 1 && lines >= 1) ? L';' : 0;
  wcscpy(buf, text);
  int n = pp_split(buf, sep, rows, PP_ROWS);
  if (sep) {
    /* описание для «Союза» */
    int desc = 0, ne = 0;
    for (int i = 0; i < n; i++)
      if (!pp_row_empty(&rows[i]) && rows[i].c[0][0] != L'#') ne++, desc += pp_desc_kind(rows[i].c[0]) != 0;
    int col[PF_N], hours[PP_MAXC] = {0}, h = -1;
    if (desc && desc * 2 >= ne) {
      out->how = PPH_SPEC;
      pp_desc(&x, rows, n);
    } else {
      for (int i = 0, seen = 0; i < n && seen < 6 && h < 0; i++) {
        if (pp_row_empty(&rows[i])) continue;
        seen++;
        if (pp_head(&rows[i], col, hours)) h = i;
      }
      if (h >= 0) {
        out->how = PPH_HEAD;
        pp_table_head(&x, rows, n, h, col, hours);
      } else if ((in->sheet == PK_ITEMS || in->sheet == PK_LINKS) && pp_table_guess(&x, rows, n)) {
        out->how = PPH_GUESS;
      } else {
        out->how = PPH_PLAIN; /* по порядку столбцов открытого листа */
        for (int i = 0; i < n; i++) {
          const PpRow *r = &rows[i];
          if (pp_row_empty(r)) continue;
          pp_emit(&x, in->sheet, pp_at(r, 0), pp_at(r, 1), pp_at(r, 2), pp_at(r, 3), pp_at(r, 4), pp_at(r, 5),
                  pp_at(r, 6));
        }
      }
    }
  } else if (route && pp_route(&x, text)) {
    out->how = PPH_ROUTE;
  } else {
    /* строки операций — если их больше половины; иначе список позиций */
    int ops = 0, ne = 0;
    PpOp o;
    for (int i = 0; i < n; i++)
      if (rows[i].n && rows[i].c[0][0]) ne++, ops += pp_op_line(rows[i].c[0], &o);
    if (ops && ops * 2 >= ne) {
      out->how = PPH_OPS;
      for (int i = 0; i < n; i++)
        if (rows[i].n && pp_op_line(rows[i].c[0], &o))
          pp_op_row(&x, in->part, o.num, o.op, o.area, o.equip, o.tpz, o.tsht);
    } else if (pp_list(&x, rows, n)) {
      out->how = PPH_LIST;
    }
  }
  free(buf);
  free(rows);
  int total = 0;
  for (int k = 0; k < 4; k++) total += out->rows[k];
  return total;
}
