/* ---- Настройки → «Нормирование»: пределы и значения по умолчанию режимов резания (с 2026.10.28) ----

   У каждого регулируемого параметра расчёта норм (norms.c, g_nmLim: скорость, подачи, глубина,
   проходы, припуск, обороты, стойкость, партия) — «от», «до» и «по умолчанию». Расчёт берёт
   значения только внутри пределов; значение по умолчанию — запасное, когда своего нет. Пусто в
   «от»/«до» — предела нет, в «по умолчанию» — считает CursorPad. Правка сохраняется сразу — в
   norms.ini в папке данных; «Заводские пределы» — как было. Окно «Режимы» (norms_ui.c) не даёт
   ввести значение вне пределов. */

#define ID_NL_FACTORY 4895
#define ID_NL_OP 4940 /* + номер правила операции (g_nmOps) — галочка «нормировать» */
#define ID_NL_HEAT 4930 /* + 0 — порог массы, 1 — Тшт до порога, 2 — тяжелее, 3 — зачистка после (норма технолога на ТО) */
#define ID_NL_EDIT 4900 /* + параметр·3 + столбец: 0 — от, 1 — до, 2 — по умолчанию */

static HWND g_nlEdit[NM_LIMS][3];
static HWND g_nlFactory, g_nlOp[NM_OPRULES], g_nlHeat[4];
static const wchar_t *const kNlHeatKey[4] = {L"limit", L"light", L"heavy", L"clean"};
static double *nl_heat(int c) {
  return c == 0 ? &g_nmHeat.limit : c == 1 ? &g_nmHeat.light : c == 2 ? &g_nmHeat.heavy : &g_nmHeat.clean;
}
static BOOL g_nlFill; /* поля заполняет код: EN_CHANGE — не правка */
static wchar_t g_nlTitle[NM_LIMS][64], g_nlDesc[NM_LIMS][96]; /* подписи строк: раскладка хранит указатели */
static const wchar_t *const kNlCol[3] = {L"min", L"max", L"def"};

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
  for (int k = 0; k < NM_OPRULES; k++) { /* [ops] Токарная=1 — нормировать; 0 — нет */
    GetPrivateProfileStringW(L"ops", g_nmOps[k].op, L"1", v, 64, p);
    g_nmOps[k].off = !wcscmp(v, L"0");
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
  for (int k = 0; k < NM_OPRULES; k++) WritePrivateProfileStringW(L"ops", g_nmOps[k].op, g_nmOps[k].off ? L"0" : L"1", p);
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
}

static void nl_refresh(void) {
  g_nlFill = TRUE;
  for (int c = 0; c < 4; c++) {
    wchar_t v[48];
    tn_numstr(*nl_heat(c), v, 48);
    if (g_nlHeat[c]) SetWindowTextW(g_nlHeat[c], v);
  }
  for (int k = 0; k < NM_OPRULES; k++)
    if (g_nlOp[k]) SendMessageW(g_nlOp[k], BM_SETCHECK, g_nmOps[k].off ? BST_UNCHECKED : BST_CHECKED, 0);
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
  for (int k = 0; k < NM_OPRULES; k++) {
    g_nlOp[k] = CreateWindowExW(0, L"BUTTON", L"нормировать", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, -4000, 130, 26, p,
                                (HMENU)(INT_PTR)(ID_NL_OP + k), NULL, NULL);
    SendMessageW(g_nlOp[k], WM_SETFONT, (WPARAM)g_fontUi, TRUE);
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
  sd_stack_end(sd_stack(NULL, L"Станочные — по ОМН, резка — по нормам резки завода, остальные — оценкой (в ответе "
                              L"«оценка, проверить»), термическая и покрытия — по факту. Снятая галочка — операция не "
                              L"нормируется, в ответе ИИ она одной строкой «не нормируются».") - 12);
  static const int cw = 130;
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
  if (id >= ID_NL_OP && id < ID_NL_OP + NM_OPRULES) {
    if (code == BN_CLICKED && !g_nlFill) {
      g_nmOps[id - ID_NL_OP].off = SendMessageW(g_nlOp[id - ID_NL_OP], BM_GETCHECK, 0, 0) != BST_CHECKED;
      nl_save();
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
