/* ---- Папка под сборку в «Союзе» (с 2026.10.29, правило пользователя) ------------------------------

   ЭСИ сборки (изделие, состав, детали, документы) и её ТП кладутся не в выбранный корень, а в свою
   папку внутри него: «Корень\<Код>\<Код группа>\<номер Наименование>». Код и группа — из обозначения
   головной сборки: буквенный код и первая группа цифр («АДЕ 3422-682.01.01» → код «АДЕ», группа «3422»,
   номер без кода «3422-682.01.01»; «АБВГ.123456.000» → «АБВГ», «123456», «123456.000»). Ничего из этого
   в код не зашито: уровни и имена берутся из того, как устроены соседние папки в выбранном корне
   (список «ПАПКИ» из клиента, g_szmFold): есть ли уровень кода, как названа папка группы («АДЕ 3422»
   или «3422»), пишут ли соседи номер сборки полностью («3422-682.01.01 …») или коротко («3422-682 …»).
   Папка сборки с тем же номером уже есть — берётся она. Группы нет — предлагается создать и её, об
   этом говорится человеку (окно «В Союз…» и карточка ЭСИ в чате показывают «ЭСИ → …», «ТП → …»,
   поле можно поменять). Недостающие уровни создаёт функция пакета 1.0.0.8 по строке
   «СОЗДАТЬ_ПАПКИ<Tab>1» (g_szmMkDirs); пакет старее — предложение не ставится, изделия идут в корень,
   строка состояния говорит об этом. Для ТП — то же правило в дереве папок ТП от выбранной папки ТП. */

typedef struct {
  wchar_t path[SZM_PATH];  /* предлагаемая папка сборки (полный путь) */
  wchar_t group[SZM_PATH]; /* папка группы — найденная или предлагаемая */
  BOOL exists;             /* папка сборки уже есть */
  BOOL groupFound;         /* папка группы нашлась среди соседей */
  int newLevels;           /* сколько уровней придётся создать: 0 — ничего, 1 — папку сборки, 2 — и группу, 3 — и код */
} SzmAsmFolder;

static SzmAsmFolder g_szmProp[2]; /* последнее предложение (изделия, ТП); g_szmRoot/g_szmAuto/g_szmMkDirs — в soyuz_make.c */

static BOOL szm_is_letter(wchar_t c) {
  return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= 0x400 && c <= 0x4ff);
}

/* обозначение → буквенный код, первая группа цифр, номер без кода; FALSE — не разобрать */
static BOOL szm_des_split(const wchar_t *des, wchar_t *code, int ccap, wchar_t *group, int gcap, wchar_t *number,
                          int ncap) {
  code[0] = group[0] = number[0] = 0;
  const wchar_t *p = des ? des : L"";
  while (*p == L' ') p++;
  int k = 0;
  while (szm_is_letter(p[k]) && k < ccap - 1) code[k] = p[k], k++;
  code[k] = 0;
  if (k < 1 || k > 6) return FALSE;
  p += k;
  while (*p == L' ' || *p == L'.' || *p == L'-') p++;
  if (!(*p >= L'0' && *p <= L'9')) return FALSE;
  lstrcpynW(number, p, ncap);
  int g = 0;
  while (*p >= L'0' && *p <= L'9' && g < gcap - 1) group[g++] = *p++;
  group[g] = 0;
  size_t nl = wcslen(number);
  while (nl && number[nl - 1] == L' ') number[--nl] = 0;
  return g >= 2;
}

/* имя папки без пути */
static const wchar_t *szm_leaf(const wchar_t *path) {
  const wchar_t *b = wcsrchr(path, L'\\');
  return b ? b + 1 : path;
}

/* путь — прямой потомок prefix? (prefix\имя без дальнейших «\») */
static BOOL szm_child_of(const wchar_t *path, const wchar_t *prefix) {
  size_t pl = wcslen(prefix);
  if (!pl || _wcsnicmp(path, prefix, pl) || path[pl] != L'\\' || !path[pl + 1]) return FALSE;
  return wcschr(path + pl + 1, L'\\') == NULL;
}

/* первое слово имени (до пробела) */
static void szm_first_word(const wchar_t *name, wchar_t *out, int cap) {
  int k = 0;
  while (name[k] && name[k] != L' ' && k < cap - 1) out[k] = name[k], k++;
  out[k] = 0;
}

/* имя папки — это группа? «АДЕ 3422», «АДЕ3422», «3422», «АДЕ-3422»: без кода и разделителей остаётся ровно группа */
static BOOL szm_is_group_name(const wchar_t *name, const wchar_t *code, const wchar_t *group) {
  const wchar_t *p = name;
  while (*p == L' ') p++;
  size_t cl = wcslen(code);
  if (!_wcsnicmp(p, code, cl) && !szm_is_letter(p[cl])) p += cl;
  while (*p == L' ' || *p == L'.' || *p == L'-' || *p == L'_') p++;
  size_t gl = wcslen(group);
  if (wcsncmp(p, group, gl)) return FALSE;
  p += gl;
  while (*p == L' ') p++;
  return *p == 0;
}

/* Предложение папки сборки. root — выбранная папка; list/n — все пути этого дерева (ПАПКА или ПАПКА_ТП)
   из клиента; des/name — головная сборка. FALSE — обозначение не разобрать или корня нет */
static BOOL szm_asm_folder(const wchar_t *root, wchar_t (*list)[SZM_PATH], int n, const wchar_t *des,
                           const wchar_t *name, SzmAsmFolder *out) {
  memset(out, 0, sizeof(*out));
  wchar_t code[16], group[32], number[120];
  if (!root || !root[0] || !szm_des_split(des, code, 16, group, 32, number, 120)) return FALSE;
  wchar_t rootN[SZM_PATH];
  lstrcpynW(rootN, root, SZM_PATH);
  size_t rl = wcslen(rootN);
  while (rl && (rootN[rl - 1] == L'\\' || rootN[rl - 1] == L' ')) rootN[--rl] = 0;
  /* уровень кода: папка «АДЕ» прямо в корне */
  wchar_t base[SZM_PATH];
  lstrcpynW(base, rootN, SZM_PATH);
  BOOL codeLevel = FALSE;
  for (int i = 0; i < n && !codeLevel; i++)
    if (szm_child_of(list[i], rootN) && !_wcsicmp(szm_leaf(list[i]), code)) lstrcpynW(base, list[i], SZM_PATH), codeLevel = TRUE;
  /* папка группы среди детей base */
  BOOL withCode = FALSE, anyDigits = FALSE; /* как названы соседние группы: «АДЕ 3422» или «3422» */
  for (int i = 0; i < n; i++) {
    if (!szm_child_of(list[i], base)) continue;
    const wchar_t *leaf = szm_leaf(list[i]);
    if (szm_is_group_name(leaf, code, group)) {
      lstrcpynW(out->group, list[i], SZM_PATH);
      out->groupFound = TRUE;
    }
    if (!_wcsnicmp(leaf, code, wcslen(code)) && leaf[wcslen(code)] == L' ') withCode = TRUE;
    if (leaf[0] >= L'0' && leaf[0] <= L'9') anyDigits = TRUE;
  }
  if (!out->groupFound) {
    if (withCode || !anyDigits) _snwprintf(out->group, SZM_PATH, L"%s\\%s %s", base, code, group);
    else _snwprintf(out->group, SZM_PATH, L"%s\\%s", base, group);
    out->group[SZM_PATH - 1] = 0;
  }
  /* номер как у соседей: с «.01.01» или коротко — до первой точки */
  wchar_t shortNum[120];
  lstrcpynW(shortNum, number, 120);
  wchar_t *dot = wcschr(shortNum, L'.');
  if (dot) *dot = 0;
  int full = 0, shortn = 0;
  for (int i = 0; out->groupFound && i < n; i++) {
    if (!szm_child_of(list[i], out->group)) continue;
    const wchar_t *leaf = szm_leaf(list[i]);
    if (!(leaf[0] >= L'0' && leaf[0] <= L'9')) continue;
    wchar_t w[120];
    szm_first_word(leaf, w, 120);
    if (!_wcsicmp(w, number) || !_wcsicmp(w, shortNum)) { /* такая папка уже есть */
      lstrcpynW(out->path, list[i], SZM_PATH);
      out->exists = TRUE;
    }
    if (wcschr(w, L'.')) full++;
    else shortn++;
  }
  if (out->exists) return TRUE;
  const wchar_t *num = full > shortn ? number : shortNum;
  wchar_t nm[200] = L"";
  if (name) {
    lstrcpynW(nm, name, 200);
    for (wchar_t *c = nm; *c; c++)
      if (*c == L'\\' || *c == L'/' || *c == L'\t' || *c == L'"' || *c == L'|' || *c == L'*' || *c == L'?' || *c == L'<' ||
          *c == L'>' || *c == L':')
        *c = L' ';
    size_t l = wcslen(nm);
    while (l && nm[l - 1] == L' ') nm[--l] = 0;
  }
  _snwprintf(out->path, SZM_PATH, L"%s\\%s%s%s", out->group, num, nm[0] ? L" " : L"", nm);
  out->path[SZM_PATH - 1] = 0;
  out->newLevels = out->groupFound ? 1 : 2; /* уровень кода не предлагается: группа ложится в код или в корень */
  (void)codeLevel;
  return TRUE;
}

/* головная сборка описания: первая строка ИЗДЕЛИЕ вида «Сборочные единицы», которая не входит ни в
   один СОСТАВ как входящее; нет сборок — FALSE */
static BOOL szm_head_asm(const wchar_t *text, wchar_t *des, int dcap, wchar_t *name, int ncap) {
  des[0] = name[0] = 0;
  for (const wchar_t *p = text; p && *p;) {
    const wchar_t *e = wcspbrk(p, L"\r\n");
    size_t len = e ? (size_t)(e - p) : wcslen(p);
    if (!wcsncmp(p, L"ИЗДЕЛИЕ\t", 8)) {
      wchar_t f[4][200] = {L"", L"", L"", L""};
      const wchar_t *q = p + 8;
      for (int k = 0; k < 4 && q < p + len; k++) {
        const wchar_t *t = wmemchr(q, L'\t', (size_t)(p + len - q));
        size_t fl = t ? (size_t)(t - q) : (size_t)(p + len - q);
        if (fl > 199) fl = 199;
        memcpy(f[k], q, fl * sizeof(wchar_t));
        f[k][fl] = 0;
        q = t ? t + 1 : p + len;
      }
      if (f[0][0] && wcsstr(f[2], L"борочн")) {
        /* входит ли куда-то как входящее: «СОСТАВ\t<сборка>\t<f[0]>\t» */
        wchar_t needle[220];
        _snwprintf(needle, 220, L"\t%s\t", f[0]);
        BOOL child = FALSE;
        for (const wchar_t *s = text; (s = wcsstr(s, L"СОСТАВ\t")) != NULL && !child; s += 7) {
          const wchar_t *se = wcspbrk(s, L"\r\n");
          const wchar_t *t1 = wcschr(s + 7, L'\t'); /* после сборки — входящее */
          if (t1 && (!se || t1 < se) && !wcsncmp(t1, needle, wcslen(needle))) child = TRUE;
        }
        if (!child) {
          lstrcpynW(des, f[0], dcap);
          lstrcpynW(name, f[1], ncap);
          return TRUE;
        }
      }
    }
    p = e ? e : p + len;
    while (*p == L'\r' || *p == L'\n') p++;
  }
  return FALSE;
}

/* строка «ЭСИ → путь (новая папка…)» для окна и чата */
static void szm_prop_line(const SzmAsmFolder *f, const wchar_t *what, wchar_t *out, int cap) {
  _snwprintf(out, cap, L"%s → %s%s", what, f->path,
             f->exists        ? L" (папка уже есть)"
             : f->newLevels <= 1 ? L" (новая папка)"
                                 : L" (группы не нашлось — будет создана и она)");
  out[cap - 1] = 0;
}

/* Предложение для описания text при корнях folder/tp по спискам клиента: out[0] — изделия, out[1] — ТП
   (out[k].path пусто — предложения нет). *mk — нужна ли строка СОЗДАТЬ_ПАПКИ. FALSE — сборки в описании нет
   или списка папок ещё нет */
static BOOL szm_propose_text(const wchar_t *text, const wchar_t *folder, const wchar_t *tp, SzmAsmFolder out[2], BOOL *mk) {
  memset(out, 0, sizeof(SzmAsmFolder) * 2);
  *mk = FALSE;
  wchar_t des[200], name[200];
  if (!g_szmFold || !szm_head_asm(text, des, 200, name, 200)) return FALSE;
  BOOL any = FALSE;
  if (szm_asm_folder(folder, g_szmFold->path, g_szmFold->n, des, name, &out[0])) any = TRUE;
  if (tp && tp[0] && szm_asm_folder(tp, g_szmFold->tp, g_szmFold->ntp, des, name, &out[1])) any = TRUE;
  *mk = (out[0].path[0] && !out[0].exists) || (out[1].path[0] && !out[1].exists);
  return any;
}

/* окно «В Союз…» (режим текста): предложение — в поля (пока человек их не правил) и в строку состояния */
static void szm_propose(void) {
  if (!g_szmWnd || !g_szmText) return;
  BOOL mk = FALSE;
  wchar_t r0[SZM_PATH], r1[SZM_PATH]; /* корни могут быть ссылками PLM — в пути из списка */
  szm_folder_resolve(g_szmRoot[0], 0, r0, SZM_PATH);
  szm_folder_resolve(g_szmRoot[1], 1, r1, SZM_PATH);
  if (!szm_propose_text(g_szmText, r0, r1, g_szmProp, &mk)) return;
  if (mk && g_szmPkgVer < SZM_VER(1, 0, 0, 8)) { /* старая функция папок не создаёт — изделия в корень */
    g_szmMkDirs = FALSE;
    wchar_t m[700];
    _snwprintf(m, 700, L"Сборке положена своя папка: %s. Пакет «CursorPad — Союз» в клиенте старее 1.0.0.8 и папок не создаёт — "
                       L"изделия лягут в «%s»; создайте папку в клиенте или обновите пакет.",
               g_szmProp[0].path, g_szmRoot[0]);
    m[699] = 0;
    szm_status(m);
    return;
  }
  g_szmMkDirs = mk;
  g_szmSetting = TRUE;
  for (int k = 0; k < 2; k++)
    if (g_szmAuto[k] && g_szmProp[k].path[0]) SetWindowTextW(k ? g_szmTpCombo : g_szmCombo, g_szmProp[k].path);
  g_szmSetting = FALSE;
  wchar_t a[SZM_PATH + 80] = L"", b[SZM_PATH + 80] = L"", m[2 * SZM_PATH + 200];
  if (g_szmProp[0].path[0]) szm_prop_line(&g_szmProp[0], L"ЭСИ", a, SZM_PATH + 80);
  if (g_szmProp[1].path[0]) szm_prop_line(&g_szmProp[1], L"ТП", b, SZM_PATH + 80);
  _snwprintf(m, 2 * SZM_PATH + 200, L"%s%s%s — поля можно поменять.", a, a[0] && b[0] ? L"; " : L"", b);
  m[2 * SZM_PATH + 199] = 0;
  szm_status(m);
}
