/* search_route.c — поиск без ярлыков (с 2026.10.22). Одно поле само решает,
   где искать: мини-ИИ, PLM или архив файлов. Подключается из pad_extra.c перед
   files.c. Windows здесь не нужен — правила проверяются на Linux:
     gcc -O2 -Wall -Werror -o search_route_test search_route_test.c && ./search_route_test

   Правила по порядку, решает первое подходящее:
     0. префикс: «?» — мини-ИИ, «#» — PLM, «/» — файлы;
     1. путь, маска или расширение файла — архив;
     2. «привет», «спасибо» и прочая болтовня — мини-ИИ;
     3. обозначение (шаблон штампа) или его кусок — PLM и архив, ищется само
        обозначение без остальных слов; со словом «чертёж» или «файл» — архив;
     4. вопрос или просьба, а также пять слов и больше — мини-ИИ;
     5. «чертёж», «скан», «файл» — архив;
     6. материал, сортамент, стандарт — PLM;
     7. остальное — PLM и архив по наименованию.
   Ярлык под полем поиска отменяет правила на один поиск. Пусто там, куда
   решили сами (или PLM без связи), — тот же запрос идёт в другое место
   (compose_answer в pad_extra.c).

   Слова запроса ищутся в любом порядке. Регистр, «ё» и латиница, похожая на
   кириллицу («ADE» и «АДЕ»), при сравнении не различаются; пробел, случайно
   вставленный у точки или дефиса между цифрами («3422 - 682»), убирается. */

/* ---- шаблон обозначения (общий со штампом, drawrec.c) ----------------------- */

static int dr_isupper_cyr(wchar_t c) { return (c >= L'А' && c <= L'Я') || c == L'Ё'; }
static int dr_isdig(wchar_t c) { return c >= L'0' && c <= L'9'; }

static int dr_digits(const wchar_t *s, int i, int mn, int mx) {
  int j = i;
  while (dr_isdig(s[j]) && j - i < mx) j++;
  if (j - i < mn || dr_isdig(s[j])) return -1;
  return j;
}

/* ядро обозначения с позиции i: «3422-682.01.01.02» или «АБВГ.123456.001»;
   возвращает конец или -1 */
static int dr_des_core(const wchar_t *s, int i) {
  int j = dr_digits(s, i, 3, 5);
  if (j > 0 && s[j] == L'-') {
    int k = dr_digits(s, j + 1, 1, 5), dots = 0;
    if (k < 0) return -1;
    if (s[k] == L'-') { /* «830-01-сб115», «830-01-77» */
      int q = k + 1;
      if ((s[q] == L'с' || s[q] == L'С') && (s[q + 1] == L'б' || s[q + 1] == L'Б')) q += 2;
      int e = dr_digits(s, q, 1, 4);
      if (e > 0) return e;
    }
    while (s[k] == L'.' && dr_isdig(s[k + 1])) {
      int q = dr_digits(s, k + 1, 1, 3);
      if (q < 0) break;
      k = q;
      dots++;
    }
    return dots ? k : -1; /* без точек — это номер ГОСТ («14637-89»), не обозначение */
  }
  int l = 0;
  while (dr_isupper_cyr(s[i + l]) && l < 5) l++;
  /* классификационное: 4 буквы . 6 цифр . 3 цифры */
  if (l == 4 && s[i + 4] == L'.') {
    int k = dr_digits(s, i + 5, 6, 6);
    if (k > 0 && s[k] == L'.') return dr_digits(s, k + 1, 3, 3);
  }
  /* документ ведомости: «АДЕ.830.МК», «АДЕ.830.01.ЗК» */
  if (l >= 2 && s[i + l] == L'.') {
    int k = dr_digits(s, i + l + 1, 2, 6);
    while (k > 0 && s[k] == L'.' && dr_isdig(s[k + 1])) k = dr_digits(s, k + 1, 1, 4);
    if (k > 0 && s[k] == L'.' && dr_isupper_cyr(s[k + 1])) {
      int q = k + 1;
      while (dr_isupper_cyr(s[q]) || dr_isdig(s[q])) q++;
      if (q - k - 1 <= 4) return q;
    }
  }
  return -1;
}

/* «б» из «СБ» в обозначении, прочитанная как цифра «6»: «321-23-с632» → «321-23-сб32», «….000С6» →
   «….000СБ». По «СБ» сборочный чертёж отличают от спецификации, а «с632» и шаблону не подходит.
   Правится, только если выходит обозначение: за дефисом — когда с «сб» сходится ядро
   (dr_des_core), в хвосте-шифре — когда перед «С6» стоит ядро целиком. Сколько поправлено */
static int dr_des_fix_sb(wchar_t *s) {
  int n = 0;
  for (int i = 1; s[i] && s[i + 1]; i++) {
    if ((s[i] != L'с' && s[i] != L'С') || s[i + 1] != L'6') continue;
    wchar_t x = s[i + 2];
    int b = i;
    if (s[i - 1] == L'-' && dr_isdig(x)) { /* «321-23-с632» */
      while (b > 0 && (dr_isdig(s[b - 1]) || s[b - 1] == L'-' || s[b - 1] == L'.')) b--;
      s[i + 1] = s[i] == L'с' ? L'б' : L'Б';
      if (dr_des_core(s, b) > i + 2) n++;
      else s[i + 1] = L'6';
      continue;
    }
    if (x && x != L'.' && x != L' ') continue;
    int e = i - (s[i - 1] == L' '); /* «….000С6», «….000 С6» */
    b = e;
    while (b > 0 && (dr_isdig(s[b - 1]) || s[b - 1] == L'-' || s[b - 1] == L'.' || dr_isupper_cyr(s[b - 1]))) b--;
    if (e > 0 && dr_isdig(s[e - 1]) && dr_des_core(s, b) == e) {
      s[i] = L'С', s[i + 1] = L'Б';
      n++;
    }
  }
  return n;
}

/* ---- запрос и слова ---------------------------------------------------------- */

enum { SR_AI = 3, SR_PLM = 4, SR_FILES = 5 }; /* те же номера, что у g_engine */
/* почему выбрано именно это место — для строки состояния */
enum { SR_WHY_CHIP, SR_WHY_PREFIX, SR_WHY_PATH, SR_WHY_TALK, SR_WHY_DES, SR_WHY_ASK, SR_WHY_FILE, SR_WHY_MAT,
       SR_WHY_NAME };

#define SR_Q 400
#define SR_WORDS 8 /* слова сверх восьмого в отбор не идут */
#define SR_WORD 120

typedef struct {
  int engine, why;
  wchar_t q[SR_Q]; /* что искать: обозначение без лишних слов или запрос как есть */
} SrRoute;

typedef struct {
  int n;
  wchar_t w[SR_WORDS][SR_WORD];
} SrWords;

/* буква для сравнения: строчная, «ё» как «е», латиница, похожая на кириллицу, — кириллица */
static wchar_t sr_fold(wchar_t c) {
  static const wchar_t lat[] = L"abcehkmoptxy", cyr[] = L"авсенкмортху";
  if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
  else if (c >= 0x410 && c <= 0x42F) c = (wchar_t)(c + 32);
  if (c == 0x401 || c == 0x451) return 0x435;
  if (c >= L'a' && c <= L'z') {
    const wchar_t *p = wcschr(lat, c);
    if (p) return cyr[p - lat];
  }
  return c;
}

/* заглавная кириллица для шаблона обозначения: «аде», «ADE» → «АДЕ» */
static wchar_t sr_upper(wchar_t c) {
  wchar_t f = sr_fold(c);
  return f >= 0x430 && f <= 0x44F ? (wchar_t)(f - 32) : c;
}

static int sr_space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xA0; }
static int sr_alnum(wchar_t c) {
  wchar_t f = sr_fold(c);
  return dr_isdig(c) || (f >= L'a' && f <= L'z') || (f >= 0x430 && f <= 0x44F);
}
static int sr_joiner(wchar_t c) { return c == L'.' || c == L'-'; }

static void sr_copy(wchar_t *out, const wchar_t *in, int cap) {
  int n = 0;
  while (in[n] && n < cap - 1) out[n] = in[n], n++;
  out[n] = 0;
}

/* запрос к одному виду: пробелы схлопнуты, длинные тире — дефис, пробел между
   цифрой и точкой или дефисом, за которыми снова цифра, убран («3422 - 682») */
static void sr_prep(const wchar_t *in, wchar_t *out, int cap) {
  wchar_t t[SR_Q];
  int n = 0;
  for (; *in && n < SR_Q - 1; in++) {
    wchar_t c = *in == 0x2013 || *in == 0x2014 ? L'-' : *in;
    if (!sr_space(c)) t[n++] = c;
    else if (n && t[n - 1] != L' ') t[n++] = L' ';
  }
  while (n && t[n - 1] == L' ') n--;
  t[n] = 0;
  int o = 0;
  for (int i = 0; t[i] && o < cap - 1; i++) {
    if (t[i] == L' ' && o > 0) {
      wchar_t a = out[o - 1], a2 = o > 1 ? out[o - 2] : 0, b = t[i + 1];
      wchar_t b2 = b ? (t[i + 2] == L' ' ? t[i + 3] : t[i + 2]) : 0;
      if ((dr_isdig(a) && sr_joiner(b) && dr_isdig(b2)) || (sr_joiner(a) && dr_isdig(a2) && dr_isdig(b))) continue;
    }
    out[o++] = t[i];
  }
  out[o] = 0;
}

/* слова через пробел; fold — сразу в виде для сравнения */
static void sr_split(const wchar_t *s, SrWords *w, int fold) {
  w->n = 0;
  while (*s) {
    while (*s == L' ') s++;
    if (!*s) break;
    int k = 0;
    for (; *s && *s != L' '; s++)
      if (k < SR_WORD - 1 && w->n < SR_WORDS) w->w[w->n][k++] = fold ? sr_fold(*s) : *s;
    if (w->n < SR_WORDS) w->w[w->n++][k] = 0;
  }
}

/* слова запроса для сравнения и весь запрос одной строкой (тоже сложенный);
   «*» и «?» — «что угодно», их просто нет: «*.pdf» ищет «.pdf» */
static void sr_words(const wchar_t *q, SrWords *w, wchar_t *whole, int cap) {
  wchar_t p[SR_Q];
  sr_prep(q, p, SR_Q);
  int o = 0;
  for (int i = 0; p[i]; i++)
    if (p[i] != L'*' && p[i] != L'?' && !(p[i] == L' ' && (!o || p[o - 1] == L' '))) p[o++] = p[i];
  while (o && p[o - 1] == L' ') o--;
  p[o] = 0;
  sr_split(p, w, 1);
  int i = 0;
  for (; p[i] && i < cap - 1; i++) whole[i] = sr_fold(p[i]);
  whole[i] = 0;
}

/* needle уже сложен (sr_fold), hay — как есть */
static int sr_has(const wchar_t *hay, const wchar_t *needle) {
  if (!needle[0]) return 1;
  if (!hay) return 0;
  for (; *hay; hay++) {
    int k = 0;
    while (needle[k] && hay[k] && sr_fold(hay[k]) == needle[k]) k++;
    if (!needle[k]) return 1;
  }
  return 0;
}

/* слово из s длиной len — это lit (без учёта регистра и похожих букв) */
static int sr_is(const wchar_t *s, int len, const wchar_t *lit) {
  int k = 0;
  for (; k < len && lit[k]; k++)
    if (sr_fold(s[k]) != sr_fold(lit[k])) return 0;
  return k == len && !lit[k];
}

static int sr_in(const wchar_t *s, int len, const wchar_t *const *list, int n) {
  for (int i = 0; i < n; i++)
    if (sr_is(s, len, list[i])) return 1;
  return 0;
}

#define SR_IN(s, len, list) sr_in(s, len, list, (int)(sizeof(list) / sizeof(list[0])))

/* слова для отбора в базе: как набраны, без повторов и без слов из одних
   знаков («/», «—»), не больше max */
static void sr_db_words(const wchar_t *q, SrWords *w, int max) {
  wchar_t p[SR_Q];
  SrWords all;
  sr_prep(q, p, SR_Q);
  sr_split(p, &all, 0);
  w->n = 0;
  for (int i = 0; i < all.n && w->n < max; i++) {
    int len = (int)wcslen(all.w[i]), ok = 0, dup = 0;
    for (int k = 0; k < len; k++) ok |= sr_alnum(all.w[i][k]);
    for (int j = 0; j < w->n; j++) dup |= sr_is(all.w[i], len, w->w[j]);
    if (ok && !dup) memcpy(w->w[w->n++], all.w[i], sizeof(all.w[i]));
  }
}

/* длина имени без расширения: у «.tif» или «.m3d» после точки есть буква,
   а «.02» в обозначении — не расширение */
static int sr_stem_len(const wchar_t *name) {
  int n = (int)wcslen(name), d = n;
  while (d > 0 && name[d - 1] != L'.') d--;
  if (d == 0 || n - d < 1 || n - d > 5) return n;
  for (int i = d; i < n; i++)
    if (!dr_isdig(name[i])) return d - 1;
  return n;
}

/* Насколько файл подходит под запрос; -1 — не все слова нашлись. Слово ищется
   в имени, а если там нет — в папке (dir NULL — только в имени). Имя целиком
   (без расширения) выше начала имени, начало — выше слов вразброс, слова
   в имени — выше слов в папке. */
static int sr_score(const wchar_t *name, const wchar_t *dir, const SrWords *w, const wchar_t *whole) {
  int inName = 0;
  for (int i = 0; i < w->n; i++) {
    if (sr_has(name, w->w[i])) inName++;
    else if (!dir || !sr_has(dir, w->w[i])) return -1;
  }
  int s = inName * 10;
  if (whole && whole[0]) {
    int k = 0;
    while (whole[k] && name[k] && sr_fold(name[k]) == whole[k]) k++;
    if (!whole[k]) s += k == sr_stem_len(name) ? 100 : 50;
  }
  return s;
}

/* ---- где искать ------------------------------------------------------------- */

static const wchar_t *const kSrExt[] = {L".tif", L".tiff", L".pdf", L".dwg", L".dxf", L".cdw", L".frw", L".spw",
                                        L".m3d", L".a3d", L".doc", L".docx", L".xls", L".xlsx", L".xlsm", L".txt",
                                        L".csv", L".rtf", L".jpg", L".jpeg", L".png", L".bmp", L".stp", L".step",
                                        L".igs", L".iges", L".zip", L".rar", L".7z"};
static const wchar_t *const kSrTalk[] = {L"привет", L"здаров", L"здорово", L"здравствуй", L"здравствуйте", L"хай",
                                         L"спасибо", L"спс", L"пасиб", L"ок", L"окей", L"ладно", L"понял", L"хорошо",
                                         L"круто", L"лол", L"пока", L"норм", L"ясно", L"супер", L"ага", L"угу"};
static const wchar_t *const kSrAsk[] = {
    L"как", L"почему", L"зачем", L"отчего", L"что", L"чем", L"чего", L"какой", L"какая", L"какое", L"какие",
    L"каков", L"какова", L"сколько", L"кто", L"когда", L"можно", L"можешь", L"расскажи", L"объясни", L"поясни",
    L"расшифруй", L"напиши", L"переведи", L"посчитай", L"рассчитай", L"вычисли", L"реши", L"сделай", L"создай",
    L"сравни", L"помоги", L"подскажи", L"скажи", L"опиши", L"придумай", L"составь", L"дай", L"давай", L"перенеси",
    L"перемести", L"скопируй", L"переименуй", L"прочитай", L"прочти", L"заполни", L"запиши", L"разложи", L"собери",
    L"сохрани", L"оформи", L"выровняй", L"нарисуй", L"проверь", L"исправь"};
/* слова «как искать», а не «что»: в запрос не идут */
static const wchar_t *const kSrStop[] = {L"найди", L"найти", L"поищи", L"поиск", L"ищи", L"искать", L"покажи",
                                         L"открой", L"где", L"мне", L"пожалуйста", L"плз", L"в", L"во", L"по", L"у",
                                         L"для", L"plm", L"плм", L"архив", L"архиве", L"база", L"базе"};
static const wchar_t *const kSrFile[] = {L"чертёж", L"чертежи", L"чертежа", L"чертежей", L"скан", L"сканы",
                                         L"скана", L"файл", L"файлы", L"файла", L"tif", L"tiff", L"pdf"};
static const wchar_t *const kSrStd[] = {L"гост", L"ост", L"ту", L"стп", L"iso", L"din", L"en", L"р"};
/* заглавные слова перед номером, которые не буквенный код изделия */
static const wchar_t *const kSrNotCode[] = {L"тп", L"эси", L"дсе"};
static const wchar_t *const kSrMat[] = {L"стал", L"лист", L"круг", L"труб", L"уголок", L"уголк", L"швеллер",
                                        L"двутавр", L"полос", L"лент", L"проволок", L"шестигран", L"квадрат",
                                        L"прут", L"арматур", L"профил", L"латун", L"бронз", L"медь", L"алюмин",
                                        L"материал", L"сортамент"};

/* слово начинается с одной из основ */
static int sr_stem_in(const wchar_t *s, int len, const wchar_t *const *list, int n) {
  for (int i = 0; i < n; i++) {
    int k = 0;
    while (list[i][k] && k < len && sr_fold(s[k]) == sr_fold(list[i][k])) k++;
    if (!list[i][k]) return 1;
  }
  return 0;
}

/* марка: «09Г2С», «40Х», «12Х18Н10Т», «Ст3сп», «АМг6», «Д16Т» — цифры и буквы
   вперемешку, без точек и дефисов */
static int sr_grade(const wchar_t *s, int len) {
  int dig = 0, let = 0;
  if (len < 2 || len > 12) return 0;
  for (int i = 0; i < len; i++) {
    wchar_t f = sr_fold(s[i]);
    if (dr_isdig(s[i])) dig = 1;
    else if (f >= 0x430 && f <= 0x44F) let = 1;
    else return 0;
  }
  return dig && let;
}

static int sr_material(const wchar_t *s, int len) {
  return SR_IN(s, len, kSrStd) || sr_stem_in(s, len, kSrMat, (int)(sizeof(kSrMat) / sizeof(kSrMat[0]))) ||
         sr_grade(s, len);
}

/* кусок обозначения: группы цифр через точку или дефис, хоть одна — от трёх
   цифр («682.01», «3422-682»). Не он: дробь «2.5» и «100.5», дата «22.10.2026». */
static int sr_part_des(const wchar_t *t, int len) {
  int groups = 0, big = 0, dots = 0, cur = 0, glen[3] = {0, 0, 0};
  if (len < 3 || !dr_isdig(t[0]) || !dr_isdig(t[len - 1])) return 0;
  for (int i = 0; i <= len; i++) {
    if (i < len && dr_isdig(t[i])) {
      cur++;
      continue;
    }
    if (i < len && !sr_joiner(t[i])) return 0;
    if (!cur) return 0;
    if (groups < 3) glen[groups] = cur;
    groups++;
    if (cur >= 3) big = 1;
    if (i < len && t[i] == L'.') dots++;
    cur = 0;
  }
  if (groups < 2 || !big) return 0;
  if (groups == 2 && dots == 1 && glen[1] < 2) return 0;
  if (groups == 3 && dots == 2 && glen[0] <= 2 && glen[1] <= 2 && (glen[2] == 2 || glen[2] == 4)) return 0;
  return 1;
}

/* слово перед позицией i — «ГОСТ», «ТУ», «ОСТ»…: за ним номер стандарта */
static int sr_after_std(const wchar_t *u, int i) {
  int e = i;
  while (e > 0 && u[e - 1] == L' ') e--;
  int b = e;
  while (b > 0 && u[b - 1] != L' ') b--;
  return e > b && SR_IN(u + b, e - b, kSrStd);
}

/* буквенный код перед обозначением («АДЕ 3422-…»): только набранный заглавными,
   иначе «где 682.01» дало бы код «ГДЕ» */
static int sr_code_before(const wchar_t *p, const wchar_t *u, int b) {
  if (b < 3 || u[b - 1] != L' ') return b;
  int q = b - 1;
  while (q > 0 && dr_isupper_cyr(u[q - 1])) q--;
  if (b - 1 - q < 2 || b - 1 - q > 5 || (q > 0 && u[q - 1] != L' ')) return b;
  for (int k = q; k < b - 1; k++)
    if (p[k] != u[k] && !(p[k] >= L'A' && p[k] <= L'Z')) return b;
  if (SR_IN(u + q, b - 1 - q, kSrStd) || SR_IN(u + q, b - 1 - q, kSrStop) || SR_IN(u + q, b - 1 - q, kSrNotCode))
    return b;
  return q;
}

/* обозначение в запросе: [b, e) в u; 0 — нет. Сначала полное по шаблону
   штампа, с исполнением «-01» сзади, потом кусок. */
static int sr_find_des(const wchar_t *p, const wchar_t *u, int *b0, int *e0) {
  for (int i = 0; u[i]; i++) {
    if (!dr_isdig(u[i]) && !dr_isupper_cyr(u[i])) continue;
    if (i > 0 && (dr_isdig(u[i - 1]) || dr_isupper_cyr(u[i - 1]) || sr_joiner(u[i - 1]))) continue;
    int e = dr_des_core(u, i);
    if (e < 0 || sr_after_std(u, i)) continue;
    if (u[e] == L'-' && dr_isdig(u[e + 1])) {
      int q = dr_digits(u, e + 1, 2, 3);
      if (q > 0) e = q;
    }
    *b0 = sr_code_before(p, u, i);
    *e0 = e;
    return 1;
  }
  for (int i = 0; u[i];) {
    while (u[i] == L' ') i++;
    int j = i;
    while (u[j] && u[j] != L' ') j++;
    /* скобки и кавычки вокруг («(682.01)», «682.01,») — не часть обозначения */
    int a = i, z = j;
    while (a < z && !dr_isdig(u[a]) && !dr_isupper_cyr(u[a])) a++;
    while (z > a && !dr_isdig(u[z - 1]) && !dr_isupper_cyr(u[z - 1])) z--;
    if (z > a && (a == i || !sr_joiner(u[a - 1])) && sr_part_des(u + a, z - a) && !sr_after_std(u, i)) {
      *b0 = a == i ? sr_code_before(p, u, i) : a;
      *e0 = z;
      return 1;
    }
    i = j;
  }
  return 0;
}

/* запрос без служебных слов (и без слов «файла», если files) — в r->q;
   возвращает, сколько слов осталось */
static int sr_strip(const wchar_t *p, int files, wchar_t *out, int cap) {
  int n = 0, words = 0;
  out[0] = 0;
  for (int i = 0; p[i];) {
    while (p[i] == L' ') i++;
    int j = i;
    while (p[j] && p[j] != L' ') j++;
    int skip = SR_IN(p + i, j - i, kSrStop) || (files && SR_IN(p + i, j - i, kSrFile));
    if (j > i && !skip) {
      if (n && n < cap - 1) out[n++] = L' ';
      for (int k = i; k < j && n < cap - 1; k++) out[n++] = p[k];
      out[n] = 0;
      words++;
    }
    i = j;
  }
  return words;
}

static void sr_route(const wchar_t *in, SrRoute *r) {
  wchar_t p[SR_Q], u[SR_Q], rest[SR_Q];
  SrWords w;
  memset(r, 0, sizeof(*r));
  while (sr_space(*in)) in++;
  int pre = *in == L'?' ? SR_AI : *in == L'#' ? SR_PLM : *in == L'/' ? SR_FILES : 0;
  sr_prep(pre ? in + 1 : in, p, SR_Q);
  sr_copy(r->q, p, SR_Q);
  r->engine = SR_PLM;
  r->why = SR_WHY_NAME;
  /* 0. префикс */
  if (pre) {
    r->engine = pre;
    r->why = SR_WHY_PREFIX;
    return;
  }
  if (!p[0]) return;
  sr_split(p, &w, 0);
  int fileWord = 0, mat = 0;
  for (int i = 0; i < w.n; i++) {
    int len = (int)wcslen(w.w[i]);
    if (SR_IN(w.w[i], len, kSrFile)) fileWord = 1;
    if (sr_material(w.w[i], len)) mat = 1;
  }
  /* 1. путь, маска, расширение; «/» не путь — «труба 1/2», «Лист … / Ст3сп» */
  int path = wcschr(p, L'\\') || wcschr(p, L'*');
  for (int i = 0; i < w.n && !path; i++) {
    int len = (int)wcslen(w.w[i]);
    for (size_t k = 0; k < sizeof(kSrExt) / sizeof(kSrExt[0]) && !path; k++) {
      int el = (int)wcslen(kSrExt[k]);
      path = len > el && sr_is(w.w[i] + len - el, el, kSrExt[k]);
    }
  }
  if (path) {
    r->engine = SR_FILES;
    r->why = SR_WHY_PATH;
    return;
  }
  /* 2. болтовня */
  if (w.n <= 3 && SR_IN(w.w[0], (int)wcslen(w.w[0]), kSrTalk)) {
    r->engine = SR_AI;
    r->why = SR_WHY_TALK;
    return;
  }
  /* 3. обозначение: ищется само, остальные слова только мешают */
  int i = 0;
  for (; p[i]; i++) u[i] = sr_upper(p[i]);
  u[i] = 0;
  int b, e;
  if (w.n < 8 && sr_find_des(p, u, &b, &e)) {
    int n = 0;
    for (int k = b; k < e && n < SR_Q - 1; k++) r->q[n++] = u[k];
    r->q[n] = 0;
    r->engine = fileWord ? SR_FILES : SR_PLM;
    r->why = SR_WHY_DES;
    return;
  }
  /* 4. вопрос или просьба; длинная строка из спецификации с маркой — не вопрос */
  int words = sr_strip(p, 0, rest, SR_Q);
  if (p[wcslen(p) - 1] == L'?' || SR_IN(w.w[0], (int)wcslen(w.w[0]), kSrAsk) || (words >= 5 && !mat)) {
    r->engine = SR_AI;
    r->why = SR_WHY_ASK;
    return;
  }
  /* 5. файл */
  if (fileWord) {
    if (sr_strip(p, 1, rest, SR_Q)) sr_copy(r->q, rest, SR_Q);
    r->engine = SR_FILES;
    r->why = SR_WHY_FILE;
    return;
  }
  /* 6. материал — как набрано: «лист 3» без «3» — уже другой лист */
  if (mat) {
    r->why = SR_WHY_MAT;
    return;
  }
  /* 7. наименование */
  if (words) sr_copy(r->q, rest, SR_Q);
}
