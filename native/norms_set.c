/* ---- Настройки → «Нормирование»: пределы и значения по умолчанию режимов резания (с 2026.10.28) ----

   У каждого регулируемого параметра расчёта норм (norms.c, g_nmLim: скорость, подачи, глубина,
   проходы, припуск, обороты, стойкость, партия) — «от», «до» и «по умолчанию». Расчёт берёт
   значения только внутри пределов; значение по умолчанию — запасное, когда своего нет. Пусто в
   «от»/«до» — предела нет, в «по умолчанию» — считает CursorPad. Правка сохраняется сразу — в
   norms.ini в папке данных; «Заводские пределы» — как было. Окно «Режимы» (norms_ui.c) не даёт
   ввести значение вне пределов. */

#define ID_NL_FACTORY 4895
#define ID_NL_OP 4940 /* + номер правила операции (g_nmOps) — класс: Тпз и Тшт, только Тшт, без норм */
#define ID_NL_HEAT 4930 /* + 0 — порог массы, 1 — Тшт до порога, 2 — тяжелее, 3 — зачистка после (норма технолога на ТО) */
#define ID_NL_EDIT 4900 /* + параметр·3 + столбец: 0 — от, 1 — до, 2 — по умолчанию */
#define ID_NL_SAW 5000  /* + группа материала (NMW_…) — производительность пилы, см²/мин */
#define ID_NL_GEAR 5010 /* + 0 — V, 1 — Sкр, 2 — Sрад (зубодолбление) */

static HWND g_nlEdit[NM_LIMS][3];
static HWND g_nlFactory, g_nlOp[NM_OPRULES], g_nlHeat[4], g_nlSaw[NMW_N], g_nlGear[3];
static const wchar_t *const kNlSawKey[NMW_N] = {L"carbon", L"alloy", L"tool", L"stain", L"iron", L"alu", L"copper"};
static const wchar_t *const kNlGearKey[3] = {L"V", L"Skr", L"Srad"};
static double *nl_gear(int c) { return c == 0 ? &g_nmGear.V : c == 1 ? &g_nmGear.Skr : &g_nmGear.Srad; }
static wchar_t g_nlSawTitle[NMW_N][48];
static const wchar_t *const kNlHeatKey[4] = {L"limit", L"light", L"heavy", L"clean"};
static double *nl_heat(int c) {
  return c == 0 ? &g_nmHeat.limit : c == 1 ? &g_nmHeat.light : c == 2 ? &g_nmHeat.heavy : &g_nmHeat.clean;
}
static BOOL g_nlFill; /* поля заполняет код: EN_CHANGE — не правка */
static wchar_t g_nlTitle[NM_LIMS][64], g_nlDesc[NM_LIMS][96]; /* подписи строк: раскладка хранит указатели */
static const wchar_t *const kNlCol[3] = {L"min", L"max", L"def"};
static const wchar_t *const kNlClass[3] = {L"Тпз и Тшт", L"только Тшт", L"без норм"}; /* NMC_FULL, NMC_TSHT, NMC_NONE */
static const wchar_t *const kNlClassKey[3] = {L"full", L"tsht", L"none"};
static WNDPROC g_nlComboProc;

/* колесо над закрытым списком класса — прокрутка Настроек, а не смена класса */
static LRESULT CALLBACK nl_combo_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_MOUSEWHEEL && !SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0)) return SendMessageW(GetParent(h), m, w, l);
  return CallWindowProcW(g_nlComboProc, h, m, w, l);
}

static void nl_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\norms.ini", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

/* заводские пределы, без значений по умолчанию */
static void nm_lim_factory(void) {
  for (int k = 0; k < NM_LIMS; k++)
    g_nmLim[k].min = g_nmLim[k].min0, g_nmLim[k].max = g_nmLim[k].max0, g_nmLim[k].def = NM_NONE;
}

static double *nl_cell(int k, int c) { return c == 0 ? &g_nmLim[k].min : c == 1 ? &g_nmLim[k].max : &g_nmLim[k].def; }

/* norms.ini: [limits] V.min=5 … — нет ключа: заводское, пусто: предела (значения) нет */
static void nl_load(void) {
  nm_lim_factory();
  wchar_t p[MAX_PATH], key[32], v[64];
  nl_path(p);
  for (int k = 0; k < NM_LIMS; k++)
    for (int c = 0; c < 3; c++) {
      _snwprintf(key, 32, L"%s.%s", g_nmLim[k].key, kNlCol[c]);
      key[31] = 0;
      GetPrivateProfileStringW(L"limits", key, L"?", v, 64, p);
      if (!wcscmp(v, L"?")) continue;
      double x;
      const wchar_t *e;
      if (!v[0]) *nl_cell(k, c) = NM_NONE;
      else if (nm_num(v, &x, &e)) *nl_cell(k, c) = x;
    }
  g_nmHeat = (NmHeat)NM_HEAT0;
  for (int c = 0; c < 4; c++) { /* [heat] limit=1 light=2 heavy=3 clean=0.5 — норма технолога на термическую */
    double x;
    const wchar_t *e;
    GetPrivateProfileStringW(L"heat", kNlHeatKey[c], L"", v, 64, p);
    if (v[0] && nm_num(v, &x, &e)) *nl_heat(c) = x;
  }
  static const double saw0[NMW_N] = NM_SAW0;
  for (int c = 0; c < NMW_N; c++) { /* [saw] carbon=30 … — производительность пилы, см²/мин */
    double x;
    const wchar_t *e;
    g_nmSaw[c] = saw0[c];
    GetPrivateProfileStringW(L"saw", kNlSawKey[c], L"", v, 64, p);
    if (v[0] && nm_num(v, &x, &e) && x > 0) g_nmSaw[c] = x;
  }
  g_nmGear = (NmGear)NM_GEAR0;
  for (int c = 0; c < 3; c++) { /* [gear] V=20 Skr=0.25 Srad=0.03 — режимы зубодолбления */
    double x;
    const wchar_t *e;
    GetPrivateProfileStringW(L"gear", kNlGearKey[c], L"", v, 64, p);
    if (v[0] && nm_num(v, &x, &e) && x > 0) *nl_gear(c) = x;
  }
  /* [ops] Сварка=full — Тпз и Тшт, tsht — только Тшт, none — без норм; нет строки — класс по умолчанию;
     до 30.10 там была галочка: «0» — без норм, «1» — по умолчанию */
  for (int k = 0; k < NM_OPRULES; k++) {
    GetPrivateProfileStringW(L"ops", g_nmOps[k].op, L"", v, 64, p);
    g_nmOps[k].cls = !wcscmp(v, L"0") ? NMC_NONE : g_nmOps[k].cls0;
    for (int c = 0; c < 3; c++)
      if (!wcscmp(v, kNlClassKey[c])) g_nmOps[k].cls = c;
  }
}

static void nl_save(void) {
  wchar_t p[MAX_PATH], key[32], v[64];
  nl_path(p);
  HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f != INVALID_HANDLE_VALUE) { /* с меткой UTF-16: дальше WritePrivateProfileStringW пишет юникодом */
    static const BYTE bom[2] = {0xFF, 0xFE};
    DWORD w = 0;
    WriteFile(f, bom, 2, &w, NULL);
    CloseHandle(f);
  }
  for (int k = 0; k < NM_OPRULES; k++) /* только изменённые: новые классы по умолчанию дойдут с обновлением */
    if (g_nmOps[k].cls != g_nmOps[k].cls0) WritePrivateProfileStringW(L"ops", g_nmOps[k].op, kNlClassKey[g_nmOps[k].cls], p);
  for (int c = 0; c < 4; c++) {
    _snwprintf(v, 64, L"%g", *nl_heat(c));
    v[63] = 0;
    WritePrivateProfileStringW(L"heat", kNlHeatKey[c], v, p);
  }
  for (int k = 0; k < NM_LIMS; k++)
    for (int c = 0; c < 3; c++) {
      double x = *nl_cell(k, c);
      _snwprintf(key, 32, L"%s.%s", g_nmLim[k].key, kNlCol[c]);
      key[31] = 0;
      if (nm_na(x)) v[0] = 0;
      else _snwprintf(v, 64, L"%g", x), v[63] = 0;
      WritePrivateProfileStringW(L"limits", key, v, p);
    }
  for (int c = 0; c < NMW_N; c++) {
    _snwprintf(v, 64, L"%g", g_nmSaw[c]);
    v[63] = 0;
    WritePrivateProfileStringW(L"saw", kNlSawKey[c], v, p);
  }
  for (int c = 0; c < 3; c++) {
    _snwprintf(v, 64, L"%g", *nl_gear(c));
    v[63] = 0;
    WritePrivateProfileStringW(L"gear", kNlGearKey[c], v, p);
  }
}

static void nl_refresh(void) {
  g_nlFill = TRUE;
  for (int c = 0; c < 4; c++) {
    wchar_t v[48];
    tn_numstr(*nl_heat(c), v, 48);
    if (g_nlHeat[c]) SetWindowTextW(g_nlHeat[c], v);
  }
  for (int k = 0; k < NM_OPRULES; k++)
    if (g_nlOp[k]) SendMessageW(g_nlOp[k], CB_SETCURSEL, (WPARAM)g_nmOps[k].cls, 0);
  for (int c = 0; c < NMW_N + 3; c++) {
    wchar_t v[48];
    tn_numstr(c < NMW_N ? g_nmSaw[c] : *nl_gear(c - NMW_N), v, 48);
    HWND h = c < NMW_N ? g_nlSaw[c] : g_nlGear[c - NMW_N];
    if (h) SetWindowTextW(h, v);
  }
  for (int k = 0; k < NM_LIMS; k++)
    for (int c = 0; c < 3; c++) {
      wchar_t v[48] = L"";
      if (!nm_na(*nl_cell(k, c))) tn_numstr(*nl_cell(k, c), v, 48);
      if (g_nlEdit[k][c]) SetWindowTextW(g_nlEdit[k][c], v);
    }
  g_nlFill = FALSE;
}

static void nl_create(HWND p) {
  for (int k = 0; k < NM_LIMS; k++) {
    const NmLim *l = &g_nmLim[k];
    _snwprintf(g_nlTitle[k], 64, L"%s%s%s", l->label, l->unit[0] ? L", " : L"", l->unit);
    g_nlTitle[k][63] = 0;
    wchar_t a[32], b[32];
    tn_numstr(l->min0, a, 32);
    tn_numstr(l->max0, b, 32);
    _snwprintf(g_nlDesc[k], 96, L"заводские: от %s до %s", a, b);
    g_nlDesc[k][95] = 0;
    for (int c = 0; c < 3; c++) {
      g_nlEdit[k][c] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_CENTER,
                                       0, -4000, 70, 26, p, (HMENU)(INT_PTR)(ID_NL_EDIT + k * 3 + c), NULL, NULL);
      SendMessageW(g_nlEdit[k][c], WM_SETFONT, (WPARAM)g_fontBody, TRUE);
      SendMessageW(g_nlEdit[k][c], EM_SETLIMITTEXT, 12, 0);
    }
  }
  g_nlFactory = mk_btn(p, L"Заводские пределы", ID_NL_FACTORY);
  for (int c = 0; c < 4; c++) {
    g_nlHeat[c] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_CENTER, 0, -4000,
                                  70, 26, p, (HMENU)(INT_PTR)(ID_NL_HEAT + c), NULL, NULL);
    SendMessageW(g_nlHeat[c], WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    SendMessageW(g_nlHeat[c], EM_SETLIMITTEXT, 12, 0);
  }
  for (int c = 0; c < NMW_N + 3; c++) {
    int id = c < NMW_N ? ID_NL_SAW + c : ID_NL_GEAR + c - NMW_N;
    HWND h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_CENTER, 0, -4000, 70,
                             26, p, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    SendMessageW(h, EM_SETLIMITTEXT, 12, 0);
    if (c < NMW_N) g_nlSaw[c] = h;
    else g_nlGear[c - NMW_N] = h;
  }
  for (int k = 0; k < NM_OPRULES; k++) {
    g_nlOp[k] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, -4000, 150,
                                160, p, (HMENU)(INT_PTR)(ID_NL_OP + k), NULL, NULL);
    SendMessageW(g_nlOp[k], WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    for (int c = 0; c < 3; c++) SendMessageW(g_nlOp[k], CB_ADDSTRING, 0, (LPARAM)kNlClass[c]);
    g_nlComboProc = (WNDPROC)SetWindowLongPtrW(g_nlOp[k], GWLP_WNDPROC, (LONG_PTR)nl_combo_proc);
  }
  SendMessageW(g_nlFactory, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  nl_refresh();
}

/* раскладка вкладки: вступление, строка на параметр (от · до · по умолчанию), сброс */
static void nl_layout(int btnH) {
  sd_group(L"Режимы резания для норм времени");
  sd_card_begin();
  sd_stack_end(sd_stack(NULL, L"Расчёт норм берёт подачу, скорость, глубину и остальное только внутри "
                              L"этих пределов — и по таблицам, и заданное в чате или в окне «Режимы». "
                              L"«По умолчанию» — когда своего значения нет. Пусто — без предела или как "
                              L"считает CursorPad. Сохраняется сразу.") - 12);
  static const int w[3] = {74, 74, 92};
  for (int k = 0; k < NM_LIMS; k++) sd_row(g_nlTitle[k], g_nlDesc[k], g_nlEdit[k], w, 3, btnH);
  sd_card_end();
  sd_card_begin();
  int bw = 190;
  sd_row(L"Заводские пределы", L"вернуть пределы как при установке, без значений по умолчанию", &g_nlFactory, &bw,
         1, btnH);
  sd_card_end();
  sd_group(L"Какие операции нормируются");
  sd_card_begin();
  sd_stack_end(sd_stack(NULL, L"Тпз — только у мехобработки; заготовительным и вспомогательным — только Тшт, "
                              L"контролю — без норм (в ответе ИИ одной строкой). Станочные считаются по ОМН, резка — "
                              L"по нормам резки завода, остальные — оценкой (в ответе «оценка, проверить»), "
                              L"термическая — норма технолога, покрытия — по факту. То же — у окна «ЭСИ и ТП».") - 12);
  static const int cw = 150;
  for (int k = 0; k < NM_OPRULES; k++) /* поле списка — высотой как кнопки (смена шрифта её сбрасывает) */
    if (g_nlOp[k] && SendMessageW(g_nlOp[k], CB_GETITEMHEIGHT, (WPARAM)-1, 0) != btnH - 6)
      SendMessageW(g_nlOp[k], CB_SETITEMHEIGHT, (WPARAM)-1, btnH - 6);
  for (int k = 0; k < NM_OPRULES; k++) sd_row(g_nmOps[k].op, g_nmOps[k].src, &g_nlOp[k], &cw, 1, btnH);
  sd_card_end();
  sd_group(L"Термическая обработка — норма технолога");
  sd_card_begin();
  static const int hw = 92;
  sd_row(L"Порог массы детали, кг", L"масса со штампа чертежа; до порога включительно — «лёгкая»", &g_nlHeat[0], &hw, 1, btnH);
  sd_row(L"Тшт до порога, мин", L"детали до порога массы", &g_nlHeat[1], &hw, 1, btnH);
  sd_row(L"Тшт тяжелее порога, мин", L"и когда масса не прочитана — тогда в ответе «проверить»", &g_nlHeat[2], &hw, 1, btnH);
  sd_row(L"Слесарная после ТО: зачистить, мин", L"маршрут ставит её сразу за термической обработкой", &g_nlHeat[3], &hw,
         1, btnH);
  sd_card_end();
  sd_group(L"Отрезка на пиле — производительность, см²/мин");
  sd_card_begin();
  sd_stack_end(sd_stack(NULL, L"Оценка CursorPad: открытых нормативов на машинное время пилы нет. То = площадь сечения "
                              L"заготовки / производительность; установка, переход, обслуживание и отдых — по ОМН. "
                              L"Подгоните под свои пилы: ⌀150 из нержавейки за 15 мин — это 177/15 ≈ 12.") - 12);
  for (int c = 0; c < NMW_N; c++) {
    tn_cpy(g_nlSawTitle[c], 48, kNmSawName[c]);
    if (g_nlSawTitle[c][0] >= L'а' && g_nlSawTitle[c][0] <= L'я') g_nlSawTitle[c][0] = (wchar_t)(g_nlSawTitle[c][0] - 32);
    sd_row(g_nlSawTitle[c], NULL, &g_nlSaw[c], &hw, 1, btnH);
  }
  sd_card_end();
  sd_group(L"Зубодолбление — режимы");
  sd_card_begin();
  sd_stack_end(sd_stack(NULL, L"Оценка CursorPad для То = 2,25·m/(Sрад·nдв) + π·m·z·i/(Sкр·nдв); Тв, обслуживание, отдых "
                              L"и Тпз — по ОМН. Модуль, число зубьев и ширину венца — в чате: «норма: m 3, z 40, B 30».") - 12);
  sd_row(L"Скорость V, м/мин", L"двойных ходов долбяка — 1000·V/(2·(B + перебег))", &g_nlGear[0], &hw, 1, btnH);
  sd_row(L"Круговая подача Sкр, мм на двойной ход", NULL, &g_nlGear[1], &hw, 1, btnH);
  sd_row(L"Радиальная подача Sрад, мм на двойной ход", L"врезание на высоту зуба 2,25·m", &g_nlGear[2], &hw, 1, btnH);
  sd_card_end();
}

/* подписи столбцов над полями первой строки */
static void nl_paint(HDC hdc, RECT rc) {
  if (g_tab != SEC_NORMS || !g_nlEdit[0][0]) return;
  static const wchar_t *const cap[3] = {L"от", L"до", L"по умолчанию"};
  for (int c = 0; c < 3; c++) {
    RECT wr;
    GetWindowRect(g_nlEdit[0][c], &wr);
    MapWindowPoints(HWND_DESKTOP, g_setHwnd, (POINT *)&wr, 2);
    RECT a = {wr.left, wr.top - 18, wr.right + 40, wr.top - 2};
    draw_set_label(hdc, cap[c], a, rc.bottom);
  }
}

/* правка поля — сразу в пределы и в norms.ini; «Заводские пределы» */
static BOOL nl_command(HWND hwnd, int id, int code) {
  (void)hwnd;
  if (id == ID_NL_FACTORY) {
    nm_lim_factory();
    nl_save();
    nl_refresh();
    return TRUE;
  }
  if (id >= ID_NL_HEAT && id < ID_NL_HEAT + 4) {
    if (code == EN_CHANGE && !g_nlFill) {
      wchar_t v[48];
      double x;
      const wchar_t *e;
      GetWindowTextW(g_nlHeat[id - ID_NL_HEAT], v, 48);
      const wchar_t *t = nm_skipsp(v);
      if (nm_num(t, &x, &e) && !*nm_skipsp(e) && x > 0) { /* пусто и недописанное — не трогаем */
        *nl_heat(id - ID_NL_HEAT) = x;
        nl_save();
      }
    }
    return TRUE;
  }
  if ((id >= ID_NL_SAW && id < ID_NL_SAW + NMW_N) || (id >= ID_NL_GEAR && id < ID_NL_GEAR + 3)) {
    if (code == EN_CHANGE && !g_nlFill) {
      wchar_t v[48];
      double x;
      const wchar_t *e;
      int saw = id < ID_NL_GEAR, c = saw ? id - ID_NL_SAW : id - ID_NL_GEAR;
      GetWindowTextW(saw ? g_nlSaw[c] : g_nlGear[c], v, 48);
      const wchar_t *t = nm_skipsp(v);
      if (nm_num(t, &x, &e) && !*nm_skipsp(e) && x > 0) { /* пусто и недописанное — не трогаем */
        if (saw) g_nmSaw[c] = x;
        else *nl_gear(c) = x;
        nl_save();
      }
    }
    return TRUE;
  }
  if (id >= ID_NL_OP && id < ID_NL_OP + NM_OPRULES) {
    if (code == CBN_SELCHANGE && !g_nlFill) {
      int c = (int)SendMessageW(g_nlOp[id - ID_NL_OP], CB_GETCURSEL, 0, 0);
      if (c >= 0 && c < 3) g_nmOps[id - ID_NL_OP].cls = c, nl_save();
    }
    return TRUE;
  }
  if (id < ID_NL_EDIT || id >= ID_NL_EDIT + NM_LIMS * 3) return FALSE;
  if (code != EN_CHANGE || g_nlFill) return TRUE;
  int k = (id - ID_NL_EDIT) / 3, c = (id - ID_NL_EDIT) % 3;
  wchar_t v[48];
  GetWindowTextW(g_nlEdit[k][c], v, 48);
  double x;
  const wchar_t *e;
  const wchar_t *s = nm_skipsp(v);
  if (!*s) *nl_cell(k, c) = NM_NONE;
  else if (nm_num(s, &x, &e) && !*nm_skipsp(e)) *nl_cell(k, c) = x;
  else return TRUE; /* «0,» и прочее недописанное — ждём */
  nl_save();
  return TRUE;
}
