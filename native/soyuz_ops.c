/* soyuz_ops.c — маршрут по чертежу → операции ТП в «Союзе» (с 2026.10.24).
   В «Союзе» со стороны завода ничего не меняется: операции встают в ТП только
   обычным клиентом под логином технолога, а CursorPad лишь подаёт, что вставить,
   и читает базу (логин cursorpad, только чтение), встала ли операция.

   Два способа, оба штатные (конфигурация PMSZ.PLMSOYUZ.CFG.MFG.UNI.STD):
     • GUID. «Скопировать операцию» кладёт в буфер текстом GlobalIdentifier
       операции, «Вставить операцию» (правая кнопка на версии ТП, варианте или
       операции) по этому GUID делает её копию со следующим номером и сразу
       сохраняет. CursorPad кладёт GUID операции-аналога с тем же кодом
       справочника — типовой (TypesOperation) или из готового ТП завода;
     • текст, как для 1С: «4110 Токарная», «4210 Сверлильная»… — когда аналога
       в базе нет; операцию человек создаёт сам и вставляет текст Ctrl+V.
   Встала ли операция, видно по базе: новая TSOperation с той же ссылкой
   Operation — значит, строка маршрута перенесена (so_match).

   Здесь — то, что не зависит от Windows и проверяется на Linux:
     gcc -O2 -Wall -Werror -o soyuz_ops_test soyuz_ops_test.c && ./soyuz_ops_test
   Нужен techno.c (tn_cat, tn_cpy, tn_num) — в cursorpad.c он включён раньше. */

#define SO_MAX 40

enum { SO_WAIT, SO_DONE, SO_SKIP };

typedef struct {
  wchar_t num[8];     /* номер в маршруте: «005» */
  wchar_t op[128];    /* как в маршруте: «Токарная», «Термическая резка лазерная» (названия справочника) */
  wchar_t what[300];  /* что делать — из маршрута */
  wchar_t code[8];    /* код справочника операций (Code): «4110»; пусто — не сопоставлена */
  wchar_t name[120];  /* название в справочнике: «Токарная» */
  wchar_t alt[64];    /* другие подходящие коды: «0201» — технологу проверить */
  double tpz, tsht;   /* нормы из маршрута (norms.c), мин; < 0 — нет */
  /* из базы (soyuz_ui.c): */
  long dir;           /* справочная операция (InfoObjectId); 0 — кода нет в справочнике */
  wchar_t guid[40];   /* операция-аналог: её GUID ложится в буфер; пусто — только текст */
  wchar_t from[200];  /* откуда аналог: «ТП ТСТ.100.001 · 020 Токарная» */
  wchar_t ceh[120];   /* цех и участок аналога */
  wchar_t equip[120]; /* оборудование аналога */
  double aTpz, aTsht; /* нормы аналога, мин; < 0 — нет */
  int uses;           /* сколько раз операция с этим кодом встречается в ТП завода */
  int state;          /* SO_… */
  int pasted;         /* текст вставлен Ctrl+V или «Дальше» — ждём, когда операция появится в ТП */
  wchar_t got[16];    /* номер, под которым операция встала в ТП */
} SoItem;

typedef struct {
  wchar_t des[80];   /* обозначение детали — из «Лист: файл · обозначение» */
  wchar_t name[120]; /* наименование — из «Деталь: …» */
  wchar_t mat[200];   /* материал — из «Материал: марка ГОСТ — класс» (для создания детали, soyuz_make.c) */
  wchar_t blank[200]; /* заготовка — из «Заготовка: Круг 50 ГОСТ 2590-2006 (…)» */
  SoItem it[SO_MAX];
  int n;
  int cur;           /* строка, которая сейчас в буфере */
  long target;       /* версия ТП, куда вставляют: узнаётся по первой вставке */
} SoRoute;

/* ---- строки --------------------------------------------------------------------- */

static wchar_t so_low1(wchar_t c) {
  if (c >= L'А' && c <= L'Я') return (wchar_t)(c + 32);
  if (c >= L'A' && c <= L'Z') return (wchar_t)(c + 32);
  if (c == L'Ё' || c == L'ё') return L'е';
  return c;
}

/* строчные, ё→е, без лишних пробелов по краям и точки в конце */
static void so_low(const wchar_t *s, wchar_t *out, int cap) {
  int k = 0;
  while (*s == L' ') s++;
  for (; *s && k < cap - 1; s++) out[k++] = so_low1(*s);
  while (k && (out[k - 1] == L' ' || out[k - 1] == L'.')) k--;
  out[k] = 0;
}

static int so_starts(const wchar_t *s, const wchar_t *pre) { return !wcsncmp(s, pre, wcslen(pre)); }

static int so_has(const wchar_t *low, const wchar_t *w) { return wcsstr(low, w) != NULL; }

/* ---- сопоставление со справочником операций ---------------------------------------- */

/* Операции маршрута (techno.c) → код справочника операций PLM — тот же номер,
   что в 1С (ops_1c.h). Первым — код по умолчанию, в alt — другие подходящие:
   какой из них берут на заводе, решает частота в ТП (soyuz_ui.c). */
typedef struct {
  const wchar_t *op; /* название в маршруте, строчными, без уточнения в скобках */
  const wchar_t *code, *name, *alt;
} SoMap;

static const SoMap kSoMap[] = {
    {L"токарная", L"4110", L"Токарная", L""},
    {L"сверлильная", L"4210", L"Сверлильная", L""},
    {L"фрезерная", L"4260", L"Фрезерная", L""},
    {L"зубофрезерная", L"4153", L"Зубофрезерная", L""},
    {L"шлифовальная", L"4130", L"Шлифовальная", L"4131, 4133"},
    {L"круглошлифовальная", L"4131", L"Круглошлифовальная", L"4130"},
    {L"плоскошлифовальная", L"4133", L"Плоскошлифовальная", L"4130"},
    {L"долбежная", L"4175", L"Долбежная", L""},
    {L"доводочная", L"4194", L"Доводочная", L""},
    {L"отрезная", L"4280", L"Отрезная", L"2101"},
    {L"заготовительная", L"0110", L"Заготовительная", L"4280"},
    /* маршрут называет операции как справочник (techno.c, tn_opc); прежние названия — выше, для старых
       ответов в чате. Скобки названия отрезаются — «получение покрытий органических» */
    {L"термическая резка лазерная", L"9171", L"Термическая резка лазерная", L"0104"},
    {L"термическая резка плазменно-дуговая", L"9176", L"Термическая резка плазменно-дуговая", L"0104"},
    {L"термическая резка кислородная", L"9172", L"Термическая резка кислородная", L"0104"},
    {L"раскрой", L"0104", L"Раскрой", L"0110"},
    {L"разметка", L"0101", L"Разметка", L"0108"},
    {L"накатка", L"2153", L"Накатка", L"4110"},
    {L"3убофрезерная", L"4153", L"3убофрезерная", L""},
    {L"профильно-вырезная электрофизическая электроэрозионная электроискровая проволочным электродом", L"7509",
     L"Профильно-вырезная электрофизическая электроэрозионная", L"7501"},
    {L"обработка резанием", L"4100", L"Обработка резанием", L""},
    {L"гибка", L"2129", L"Гибка", L""},
    {L"штамповка", L"2170", L"Штамповка", L""},
    {L"сварка", L"9000", L"Сварка", L""},
    {L"маркирование", L"0180", L"Маркирование", L""},
    {L"контроль", L"0200", L"Контроль", L"0201"},
    {L"термическая обработка", L"5000", L"Термическая обработка", L"5030, 5050"},
    {L"получение покрытий", L"7100", L"Получение покрытий", L""},
    {L"цинкование электрохимическое", L"7174", L"Цинкование электрохимическое", L"7100"},
    {L"получение покрытий органических", L"7300", L"Получение покрытий органических (лакокрасочных)", L"7360"},
    /* заготовка из листа — операция по способу резки (techno.c, tn_cut_op): свой код и участок */
    {L"лазерная резка", L"9171", L"Термическая резка лазерная", L"0104"},
    {L"плазменная резка", L"9176", L"Термическая резка плазменно-дуговая", L"0104"},
    {L"газовая резка", L"9172", L"Термическая резка кислородная", L"0104"},
    {L"слесарная", L"0108", L"Слесарная", L""},
    {L"термическая", L"5000", L"Термическая обработка", L"5030, 5050"},
    {L"контрольная", L"0200", L"Контроль", L"0201"},
    {L"маркировочная", L"0180", L"Маркирование", L""},
    {L"гибочная", L"2129", L"Гибка", L""},
    {L"штамповочная", L"2170", L"Штамповка", L""},
    {L"сварочная", L"9000", L"Сварка", L""},
    {L"электроэрозионная", L"7509", L"Профильно-вырезная электрофизическая электроэрозионная", L"7501"},
    {L"механическая обработка", L"4100", L"Обработка резанием", L""},
    {L"гальваническая", L"7100", L"Получение покрытий", L""},
    {L"анодирование", L"7100", L"Получение покрытий", L""},
    {L"окрасочная", L"7300", L"Получение покрытий органических (лакокрасочных)", L"7360"},
};

static void so_set(SoItem *it, const wchar_t *code, const wchar_t *name, const wchar_t *alt) {
  tn_cpy(it->code, 8, code);
  tn_cpy(it->name, 120, name);
  tn_cpy(it->alt, 64, alt);
}

/* код справочника по операции маршрута; FALSE — сопоставить нечем (только текст) */
static int so_map(SoItem *it) {
  wchar_t op[128], base[128], what[300];
  so_low(it->op, op, 128);
  so_low(it->what, what, 300);
  tn_cpy(base, 128, op);
  wchar_t *br = wcschr(base, L'(');
  if (br) {
    *br = 0;
    so_low(base, base, 128);
  }
  it->code[0] = it->name[0] = it->alt[0] = 0;
  /* уточнение в скобках меняет операцию справочника */
  if (!wcscmp(op, L"слесарная (разметка)")) {
    so_set(it, L"0101", L"Разметка", L"0108");
    return 1;
  }
  if (!wcscmp(base, L"заготовительная") && br) { /* «(раскрой)» — чем режут, сказано в «что» */
    if (so_has(what, L"лазер")) so_set(it, L"9171", L"Термическая резка лазерная", L"0104");
    else if (so_has(what, L"плазм")) so_set(it, L"9176", L"Термическая резка плазменно-дуговая", L"0104");
    else if (so_has(what, L"газ") || so_has(what, L"кислород"))
      so_set(it, L"9172", L"Термическая резка кислородная", L"0104");
    else so_set(it, L"0104", L"Раскрой", L"0110");
    return 1;
  }
  if (!wcscmp(base, L"токарная") && so_has(op, L"резьба")) {
    so_set(it, L"4110", L"Токарная", L"4105");
    return 1;
  }
  if (!wcscmp(base, L"токарная") && so_has(op, L"накатка")) {
    so_set(it, L"4110", L"Токарная", L"2153");
    return 1;
  }
  int zinc = so_has(what, L"цинк"); /* или обозначение по ГОСТ 9.306: «Ц9.хр», «Ц12» */
  for (const wchar_t *q = what; *q && !zinc; q++)
    zinc = *q == L'ц' && tn_isdig(q[1]) && (q == what || !tn_isletter(q[-1]));
  if (!wcscmp(base, L"гальваническая") && zinc) {
    so_set(it, L"7174", L"Цинкование электрохимическое", L"7100");
    return 1;
  }
  for (size_t i = 0; i < sizeof(kSoMap) / sizeof(kSoMap[0]); i++)
    if (!wcscmp(base, kSoMap[i].op)) {
      so_set(it, kSoMap[i].code, kSoMap[i].name, kSoMap[i].alt);
      return 1;
    }
  return 0;
}

/* ---- разбор маршрута из текста ------------------------------------------------------- */

/* «005 Токарная. Точить… — причина»: длина номера (2–4 цифры) с пробелом; 0 — не строка операции */
static int so_num_len(const wchar_t *s) {
  int d = 0;
  while (tn_isdig(s[d])) d++;
  if (d < 2 || d > 4 || s[d] != L' ') return 0;
  return d;
}

/* число после слова: «Тпз 12,5 мин», «Тшт ≈ 3,4»; < 0 — нет («Тпз —») */
static double so_after(const wchar_t *s, const wchar_t *w) {
  const wchar_t *p = wcsstr(s, w);
  if (!p) return -1;
  p += wcslen(w);
  while (*p == L' ' || *p == L'≈' || *p == L'~' || *p == L'=') p++;
  double v;
  int len, sep;
  return tn_num(p, &v, &len, &sep) ? v : -1;
}

/* строка нормы (norms.c): «010 Токарная: То 2,1 мин · Тпз 12 мин · Тшт 3,4 мин …» */
static void so_norm_line(SoRoute *r, const wchar_t *num, const wchar_t *rest) {
  for (int i = 0; i < r->n; i++) {
    if (wcscmp(r->it[i].num, num)) continue;
    double pz = so_after(rest, L"Тпз"), sh = so_after(rest, L"Тшт");
    if (pz >= 0) r->it[i].tpz = pz;
    if (sh >= 0) r->it[i].tsht = sh;
    return;
  }
}

/* Текст маршрута (tn_text, как он лёг в чат, — его могли и поправить) → строки
   очереди. Берутся строки «NNN Название. что делать — причина»; «NNN Название:»
   — нормы этой операции. Возвращает число операций. */
static int so_parse(const wchar_t *text, SoRoute *r) {
  memset(r, 0, sizeof(*r));
  const wchar_t *p = text ? text : L"";
  wchar_t line[900];
  while (*p) {
    int k = 0;
    while (*p && *p != L'\n' && k < 899) line[k++] = *p++;
    while (*p && *p != L'\n') p++;
    if (*p == L'\n') p++;
    while (k && (line[k - 1] == L'\r' || line[k - 1] == L' ')) k--;
    line[k] = 0;
    wchar_t *s = line;
    while (*s == L' ' || *s == L'\t' || *s == L'-' || *s == L'•' || *s == L'*') s++;
    if (so_starts(s, L"Лист: ")) { /* «Лист: файл.pdf · АБВГ.123456.001» */
      wchar_t *dot = NULL;
      for (wchar_t *q = s; (q = wcsstr(q, L" · ")) != NULL; q += 3) dot = q;
      if (dot) tn_cpy(r->des, 80, dot + 3);
      continue;
    }
    if (so_starts(s, L"Материал: ")) { /* «Материал: 45 ГОСТ 1050-2013 — сталь» */
      tn_cpy(r->mat, 200, s + 10);
      wchar_t *dash = wcsstr(r->mat, L" — ");
      if (dash) *dash = 0;
      if (so_starts(r->mat, L"не прочитан")) r->mat[0] = 0;
      continue;
    }
    if (so_starts(s, L"Заготовка: ")) { /* «Заготовка: Круг 50 ГОСТ 2590-2006 (указана на чертеже)» */
      tn_cpy(r->blank, 200, s + 11);
      wchar_t *br = wcsstr(r->blank, L" (");
      if (br) *br = 0;
      if (so_starts(r->blank, L"Готовая деталь")) r->blank[0] = 0;
      continue;
    }
    if (so_starts(s, L"Деталь: ")) { /* «Деталь: Вал — тело вращения (…)» */
      tn_cpy(r->name, 120, s + 8);
      wchar_t *dash = wcsstr(r->name, L" — ");
      if (dash) *dash = 0;
      if (r->name[0] == L'(') r->name[0] = 0; /* «(наименование не прочитано)» */
      continue;
    }
    int d = so_num_len(s);
    if (!d) continue;
    wchar_t num[8];
    tn_cpy(num, d + 1 < 8 ? d + 1 : 8, s);
    const wchar_t *nm = s + d + 1;
    /* название операции — с заглавной: «2026 г.» или «12 отв.» — не операция */
    if (!((*nm >= L'А' && *nm <= L'Я') || *nm == L'Ё')) continue;
    const wchar_t *end = nm;
    while (*end && *end != L'.' && *end != L':' && !(end[0] == L' ' && end[1] == L'—')) end++;
    if (end - nm < 4) continue;
    if (*end == L':') {
      so_norm_line(r, num, end + 1);
      continue;
    }
    if (r->n >= SO_MAX) continue;
    SoItem *it = &r->it[r->n];
    memset(it, 0, sizeof(*it));
    tn_cpy(it->num, 8, num);
    int nl = (int)(end - nm) + 1;
    tn_cpy(it->op, nl < 64 ? nl : 64, nm);
    const wchar_t *w = *end == L'.' ? end + 1 : end;
    while (*w == L' ') w++;
    tn_cpy(it->what, 300, w);
    wchar_t *why = wcsstr(it->what, L" — ");
    if (why) *why = 0;
    it->tpz = it->tsht = it->aTpz = it->aTsht = -1;
    so_map(it);
    r->n++;
  }
  return r->n;
}

/* ---- очередь ---------------------------------------------------------------------------- */

/* что положить в буфер текстом — как для 1С: «4110 Токарная»; без кода — название маршрута */
static void so_clip_text(const SoItem *it, wchar_t *out, int cap) {
  out[0] = 0;
  if (it->code[0]) {
    tn_cat(out, cap, it->code);
    tn_cat(out, cap, L" ");
    tn_cat(out, cap, it->name[0] ? it->name : it->op);
  } else {
    wchar_t base[64];
    tn_cpy(base, 64, it->op);
    wchar_t *br = wcschr(base, L'(');
    if (br) *br = 0;
    int n = (int)wcslen(base);
    while (n && base[n - 1] == L' ') base[--n] = 0;
    tn_cat(out, cap, base);
  }
}

/* первая неперенесённая и ещё не поданная строка начиная с from; -1 — нет такой */
static int so_next_wait(const SoRoute *r, int from) {
  for (int i = from < 0 ? 0 : from; i < r->n; i++)
    if (r->it[i].state == SO_WAIT && !r->it[i].pasted) return i;
  return -1;
}

/* в буфер — следующая после cur, а если дальше нет — первая с начала; -1 — всё подано */
static int so_advance(SoRoute *r) {
  int nx = so_next_wait(r, r->cur + 1);
  r->cur = nx >= 0 ? nx : so_next_wait(r, 0);
  return r->cur;
}

static int so_left(const SoRoute *r) {
  int k = 0;
  for (int i = 0; i < r->n; i++) k += r->it[i].state == SO_WAIT;
  return k;
}

/* В ТП появилась новая операция: dir — её справочная операция, parent — версия ТП,
   num — номер, под которым встала. Какая строка маршрута перенесена: первая уже
   поданная текстом (её вставили раньше), потом та, что сейчас в буфере, потом
   первая ждущая с тем же кодом (вставили не по порядку). Чужие версии после
   первой вставки не считаются: это другой ТП. Возвращает индекс строки или -1. */
static int so_match(SoRoute *r, long dir, long parent, const wchar_t *num) {
  if (!dir) return -1;
  if (r->target && parent != r->target) return -1;
  int hit = -1;
  for (int i = 0; i < r->n && hit < 0; i++)
    if (r->it[i].state == SO_WAIT && r->it[i].pasted && r->it[i].dir == dir) hit = i;
  if (hit < 0 && r->cur >= 0 && r->cur < r->n && r->it[r->cur].state == SO_WAIT && r->it[r->cur].dir == dir)
    hit = r->cur;
  for (int i = 0; i < r->n && hit < 0; i++)
    if (r->it[i].state == SO_WAIT && r->it[i].dir == dir) hit = i;
  if (hit < 0) return -1;
  r->it[hit].state = SO_DONE;
  tn_cpy(r->it[hit].got, 16, num ? num : L"");
  if (!r->target) r->target = parent;
  if (hit == r->cur) so_advance(r);
  return hit;
}

/* «мин» без лишних нулей: 12 · 3,4 · 0,25; < 0 — «—» */
static void so_min(double v, wchar_t *out, int cap) {
  if (v < 0) {
    tn_cpy(out, cap, L"—");
    return;
  }
  tn_numstr(v, out, cap);
  tn_cat(out, cap, L" мин");
}
