/* ---- окно «Режимы»: параметры норм по операциям маршрута, правка с пересчётом (с 2026.10.28) ----

   Под маршрутом в чате — «Режимы», под нормами в ответе — «изменить» в строке «Режимы: …». Слева операции маршрута с Тшт, справа параметры
   выбранной: значение (правится) и откуда оно — с чертежа, по таблице, расчёт, по
   умолчанию, задано. Под ними То, Тв, Тшт, Тпз, Тшк и чего не хватило для То. Правка —
   пересчёт через полсекунды или по Enter: nm_route с вопросом «010: L 120, подача 0,3»
   после прежнего вопроса беседы; стёртое поле — снова как считает CursorPad. «Применить» —
   нормы в ответе беседы обновляются на месте (без новых сообщений), правки запоминает вопрос
   беседы — «норма: …» в чате дальше считает с ними.
   Расчёт и формулы — norms.c; таблицы читаются один раз при открытии окна. */

#define ID_NU_LIST 4880
#define ID_NU_RESET 4881
#define ID_NU_CHAT 4882 /* «Применить» — главная кнопка подвала, синяя (cursorpad.c, draw_pad_button) */
#define ID_NU_EDIT 4890 /* + строка */
#define TIMER_NU_CALC 4885
#define NU_ROWS 14
#define NU_OPS NM_OPS

typedef struct {
  const wchar_t *par, *label, *ask; /* параметр norms.c, подпись, слово вопроса («» — значение само слово) */
} NuField;

static const NuField kNuTurn[] = {{L"L", L"Длина L, мм", L"L"},
                                  {L"D", L"Диаметр D, мм", L"D"},
                                  {L"Dзаг", L"Ø заготовки, мм", L"Dзаг"},
                                  {L"h", L"Припуск h, мм", L"припуск"},
                                  {L"t", L"Глубина t, мм", L"глубина резания"},
                                  {L"i", L"Проходов", L"проходов"},
                                  {L"S", L"Подача S, мм/об", L"подача"},
                                  {L"V", L"Скорость V, м/мин", L"скорость"},
                                  {L"T", L"Стойкость T, мин", L"стойкость"},
                                  {L"инструмент", L"Инструмент", L""},
                                  {L"σв", L"σв, МПа", L"σв"},
                                  {L"станок", L"Станок", L"станок"},
                                  {L"партия", L"Партия, шт", L"партия"},
                                  {L"То", L"То своё, мин", L"То"}};
static const NuField kNuDrill[] = {{L"отверстий", L"Отверстий", L"отверстий"},
                                   {L"d", L"Ø отверстия d, мм", L"d"},
                                   {L"l", L"Глубина l, мм", L"глубина"},
                                   {L"S", L"Подача S, мм/об", L"подача"},
                                   {L"V", L"Скорость V, м/мин", L"скорость"},
                                   {L"T", L"Стойкость T, мин", L"стойкость"},
                                   {L"инструмент", L"Инструмент", L""},
                                   {L"σв", L"σв, МПа", L"σв"},
                                   {L"станок", L"Станок", L"станок"},
                                   {L"партия", L"Партия, шт", L"партия"},
                                   {L"То", L"То своё, мин", L"То"}};
static const NuField kNuMill[] = {{L"L", L"Длина L, мм", L"L"},
                                  {L"B", L"Ширина B, мм", L"B"},
                                  {L"h", L"Припуск h, мм", L"припуск"},
                                  {L"t", L"Глубина t, мм", L"глубина резания"},
                                  {L"i", L"Проходов", L"проходов"},
                                  {L"Dф", L"Ø фрезы, мм", L"Dф"},
                                  {L"z", L"Зубьев z", L"z"},
                                  {L"Sz", L"Подача Sz, мм/зуб", L"Sz"},
                                  {L"V", L"Скорость V, м/мин", L"скорость"},
                                  {L"T", L"Стойкость T, мин", L"стойкость"},
                                  {L"σв", L"σв, МПа", L"σв"},
                                  {L"станок", L"Станок", L"станок"},
                                  {L"партия", L"Партия, шт", L"партия"},
                                  {L"То", L"То своё, мин", L"То"}};
static const NuField kNuCut[] = {{L"периметр", L"Периметр, мм", L"периметр"},
                                 {L"толщина", L"Толщина, мм", L"толщина"},
                                 {L"пробивок", L"Пробивок", L"пробивок"},
                                 {L"партия", L"Партия, шт", L"партия"},
                                 {L"То", L"То своё, мин", L"То"}};
static const NuField kNuHB = {L"HB", L"Твёрдость HB", L"HB"}; /* у чугуна вместо σв */

static HWND g_nuWnd, g_nuList, g_nuEdit[NU_ROWS];
static WNDPROC g_nuEditProc;
static NmBase g_nuBase;
static BOOL g_nuBaseOk;
static NmResult *g_nuRes;
static const NmLast *g_nuFor;                /* маршрут, к которому правки (сменился в чате — правки сброшены) */
static int g_nuCur;                          /* выбранная операция — номер в g_nuRes */
static BOOL g_nuFill;                        /* поля заполняет код: EN_CHANGE — не правка */
static wchar_t g_nuOv[NU_OPS][NU_ROWS][48];  /* правки по операции и строке */
static wchar_t g_nuBatch[48];                /* партия — общая для всех операций */
static wchar_t g_nuDir[MAX_PATH];
static float g_nuS = 1.0f;

static int NUS(int v) { return (int)(v * g_nuS + 0.5f); }

static const NuField *nu_fields(const NmOp *o, int *n) {
  switch (o ? o->kind : NMK_NONE) {
  case NMK_TURN: *n = (int)(sizeof(kNuTurn) / sizeof(kNuTurn[0])); return kNuTurn;
  case NMK_DRILL: *n = (int)(sizeof(kNuDrill) / sizeof(kNuDrill[0])); return kNuDrill;
  case NMK_MILL: *n = (int)(sizeof(kNuMill) / sizeof(kNuMill[0])); return kNuMill;
  case NMK_CUT: *n = (int)(sizeof(kNuCut) / sizeof(kNuCut[0])); return kNuCut;
  }
  *n = 0;
  return NULL;
}

/* строка i операции o: у чугуна вместо σв — HB */
static const NuField *nu_field(const NmOp *o, int i) {
  int n;
  const NuField *f = nu_fields(o, &n);
  if (!f || i < 0 || i >= n) return NULL;
  const NmPar *m = nm_parc(&o->c, L"материал");
  if (!wcscmp(f[i].par, L"σв") && m && nm_eq(m->s, L"чугун")) return &kNuHB;
  return &f[i];
}

static const NmOp *nu_op(int k) { return g_nuRes && k >= 0 && k < g_nuRes->n ? &g_nuRes->op[k] : NULL; }

static int nu_num(const NmOp *o) { return (o->idx + 1) * 5; }

/* правка строки: партия — общая */
static wchar_t *nu_ov(int k, int i) {
  const NuField *f = nu_field(nu_op(k), i);
  if (f && !wcscmp(f->par, L"партия")) return g_nuBatch;
  return k >= 0 && k < NU_OPS && i >= 0 && i < NU_ROWS ? g_nuOv[k][i] : NULL;
}

/* правки в вопрос norms.c: общее «партия 50» — первым, дальше по операциям «010: L 120, подача 0,3»;
   oneLine — одной строкой для чата (кусок операции идёт до следующего номера), иначе строками */
static void nu_overrides(wchar_t *out, int cap, BOOL oneLine) {
  out[0] = 0;
  if (g_nuBatch[0]) tn_cat(out, cap, L"партия "), tn_cat(out, cap, g_nuBatch);
  for (int k = 0; g_nuRes && k < g_nuRes->n && k < NU_OPS; k++) {
    const NmOp *o = nu_op(k);
    int n, first = 1;
    nu_fields(o, &n);
    for (int i = 0; i < n && i < NU_ROWS; i++) {
      const NuField *f = nu_field(o, i);
      if (!f || !wcscmp(f->par, L"партия") || !g_nuOv[k][i][0]) continue;
      if (first) {
        wchar_t num[8];
        _snwprintf(num, 8, L"%03d: ", nu_num(o));
        num[7] = 0;
        if (out[0]) tn_cat(out, cap, oneLine ? L", " : L"\n");
        tn_cat(out, cap, num);
        first = 0;
      } else {
        tn_cat(out, cap, L", ");
      }
      if (f->ask[0]) tn_cat(out, cap, f->ask), tn_cat(out, cap, L" ");
      tn_cat(out, cap, g_nuOv[k][i]);
    }
  }
}

static void nu_reset_all(void) {
  memset(g_nuOv, 0, sizeof(g_nuOv));
  g_nuBatch[0] = 0;
}

static void nu_refresh(void);

/* пересчитать нормы маршрута с правками окна */
static void nu_calc(void) {
  if (!g_nuWnd || !g_nuRes || !g_nuBaseOk) return;
  if (!g_nmLast) {
    g_nuRes->n = 0;
    nu_refresh();
    return;
  }
  if (g_nmLast != g_nuFor) { /* в чате новый маршрут — правки были к прежнему */
    g_nuFor = g_nmLast;
    g_nuCur = 0;
    nu_reset_all();
  }
  wchar_t ov[1500];
  nu_overrides(ov, 1500, FALSE);
  size_t n = wcslen(g_nmLast->ask) + wcslen(ov) + 4;
  wchar_t *ask = (wchar_t *)malloc(n * sizeof(wchar_t));
  if (!ask) return;
  _snwprintf(ask, n, L"%s\n%s", g_nmLast->ask, ov);
  ask[n - 1] = 0;
  nm_route(&g_nuBase, &g_nmLast->r, g_nmLast->mass, ask, g_nuRes);
  free(ask);
  if (g_nuCur >= g_nuRes->n) g_nuCur = g_nuRes->n ? g_nuRes->n - 1 : 0;
  nu_refresh();
}

/* «010 Токарная · Тшт 2,91 мин» — строка списка операций */
static void nu_op_line(const NmOp *o, wchar_t *out, int cap) {
  _snwprintf(out, cap, L"%03d %s", nu_num(o), o->op);
  out[cap - 1] = 0;
  if (o->kind == NMK_FACT) tn_cat(out, cap, L" · по факту");
  else if (o->kind == NMK_EST && nm_na(o->tsht)) tn_cat(out, cap, L" · оценки нет");
  else if (o->kind == NMK_EST) tn_cat(out, cap, L" · Тшт ≈ "), nm_catmin(out, cap, o->tsht), tn_cat(out, cap, L" (оценка)");
  else if (nm_na(o->to)) tn_cat(out, cap, L" · нет То");
  else if (!nm_na(o->tsht)) tn_cat(out, cap, o->full ? L" · Тшт " : L" · Тшт ≈ "), nm_catmin(out, cap, o->tsht);
  else tn_cat(out, cap, L" · То "), nm_catmin(out, cap, o->to);
}

static void nu_refresh(void) {
  if (!g_nuWnd) return;
  g_nuFill = TRUE;
  SendMessageW(g_nuList, LB_RESETCONTENT, 0, 0);
  for (int k = 0; g_nuRes && k < g_nuRes->n; k++) {
    wchar_t t[160];
    nu_op_line(&g_nuRes->op[k], t, 160);
    SendMessageW(g_nuList, LB_ADDSTRING, 0, (LPARAM)t);
  }
  SendMessageW(g_nuList, LB_SETCURSEL, (WPARAM)g_nuCur, 0);
  const NmOp *o = nu_op(g_nuCur);
  int n;
  nu_fields(o, &n);
  HWND focus = GetFocus();
  for (int i = 0; i < NU_ROWS; i++) {
    const NuField *f = i < n ? nu_field(o, i) : NULL;
    ShowWindow(g_nuEdit[i], f ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (!f || g_nuEdit[i] == focus) continue; /* в поле печатают — не мешать */
    const wchar_t *ov = nu_ov(g_nuCur, i);
    const NmPar *p = nm_parc(&o->c, f->par);
    SetWindowTextW(g_nuEdit[i], ov && ov[0] ? ov : p ? p->s : L"");
  }
  g_nuFill = FALSE;
  InvalidateRect(g_nuWnd, NULL, FALSE);
}

#define NU_ROW 28
static int nu_list_w(void) { return NUS(250); }
static int nu_rows_top(void) { return PANEL_TITLE_H + NUS(64); }

static void nu_layout(void) {
  if (!g_nuWnd) return;
  RECT rc;
  GetClientRect(g_nuWnd, &rc);
  place_panel_close(g_nuWnd);
  int pad = NUS(22), foot = rc.bottom - NUS(TOOL_FOOT_H), by = foot + (NUS(TOOL_FOOT_H) - NUS(30)) / 2;
  int top = PANEL_TITLE_H + NUS(22);
  MoveWindow(g_nuList, pad, top, nu_list_w(), foot - NUS(14) - top, TRUE);
  int x = pad + nu_list_w() + NUS(18) + NUS(150), y = nu_rows_top();
  for (int i = 0; i < NU_ROWS; i++, y += NUS(NU_ROW))
    MoveWindow(g_nuEdit[i], x, y + NUS(2), NUS(130), NUS(NU_ROW) - NUS(5), TRUE);
  MoveWindow(GetDlgItem(g_nuWnd, ID_NU_CHAT), rc.right - NUS(16) - NUS(160), by, NUS(160), NUS(30), TRUE);
  MoveWindow(GetDlgItem(g_nuWnd, ID_NU_RESET), rc.right - NUS(16) - NUS(160) - NUS(8) - NUS(130), by, NUS(130), NUS(30),
             TRUE);
  InvalidateRect(g_nuWnd, NULL, FALSE);
}

/* «по таблице: …», «расчёт: …», «задано» — откуда значение */
static void nu_origin(const NmPar *p, wchar_t *out, int cap) {
  out[0] = 0;
  if (!p) {
    tn_cpy(out, cap, L"—");
    return;
  }
  tn_cpy(out, cap, kNmOrg[p->org]);
  if (p->why[0]) tn_cat(out, cap, L": "), tn_cat(out, cap, p->why);
}

static void nu_paint(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int foot = rc.bottom - NUS(TOOL_FOOT_H);
  RECT card = {NUS(10), PANEL_TITLE_H + NUS(10), rc.right - NUS(10), foot - NUS(6)};
  const NmOp *o = nu_op(g_nuCur);
  wchar_t desc[200], t[600];
  _snwprintf(desc, 200, L"последний маршрут в чате%s%s · операций с нормой: %d",
             g_nmLast && g_nmLast->r.s.grade[0] ? L" · материал " : L"", g_nmLast ? g_nmLast->r.s.grade : L"",
             g_nuRes ? g_nuRes->n : 0);
  desc[199] = 0;
  tool_frame_paint(hwnd, hdc, L"Режимы и нормы", desc, card, foot);
  SetBkMode(hdc, TRANSPARENT);
  int pad = NUS(22), x0 = pad + nu_list_w() + NUS(18), right = rc.right - pad, y = PANEL_TITLE_H + NUS(22);
  if (!g_nuBaseOk || !g_nmLast || !o) {
    if (g_fontUi) SelectObject(hdc, g_fontUi);
    SetTextColor(hdc, COL_MUTED);
    if (!g_nuBaseOk)
      _snwprintf(t, 600, L"Таблиц нормативов нет: папка %s не найдена.", g_nuDir);
    else if (!g_nmLast)
      lstrcpynW(t, L"Сначала постройте маршрут по чертежу — в окне чертежа или в чате.", 600);
    else
      lstrcpynW(t, L"В маршруте нет операций, которые нормируются (токарная, сверлильная, фрезерная, резка).", 600);
    t[599] = 0;
    RECT m = {x0, y, right, foot - NUS(12)};
    DrawTextW(hdc, t, -1, &m, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    return;
  }
  /* операция крупно */
  if (g_fontDisplay) SelectObject(hdc, g_fontDisplay);
  SetTextColor(hdc, COL_INK);
  _snwprintf(t, 600, L"%03d %s", nu_num(o), o->op);
  t[599] = 0;
  RECT a = {x0, y, right, y + NUS(30)};
  DrawTextW(hdc, t, -1, &a, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  /* строки: подпись · поле (окно EDIT) · откуда */
  int n;
  nu_fields(o, &n);
  y = nu_rows_top();
  int ex = x0 + NUS(150), ox = ex + NUS(130) + NUS(12);
  for (int i = 0; i < n && i < NU_ROWS; i++, y += NUS(NU_ROW)) {
    const NuField *f = nu_field(o, i);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, COL_MUTED);
    RECT l = {x0, y, ex - NUS(8), y + NUS(NU_ROW) - NUS(3)};
    DrawTextW(hdc, f->label, -1, &l, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    const wchar_t *ov = nu_ov(g_nuCur, i);
    nu_origin(nm_parc(&o->c, f->par), t, 600);
    if (ov && ov[0]) SetTextColor(hdc, COL_SAGE), lstrcpynW(t, L"ваше — пересчитано с ним", 600);
    RECT r = {ox, y, right, y + NUS(NU_ROW) - NUS(3)};
    DrawTextW(hdc, t, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
  /* итог операции */
  y += NUS(10);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  t[0] = 0;
  tn_cat(t, 600, L"То ");
  if (nm_na(o->to)) tn_cat(t, 600, L"—");
  else nm_catmin(t, 600, o->to);
  if (!nm_na(o->tv)) tn_cat(t, 600, L" · Тв "), nm_catmin(t, 600, o->tv);
  if (!nm_na(o->tsht)) tn_cat(t, 600, o->full ? L" · Тшт " : L" · Тшт ≈ "), nm_catmin(t, 600, o->tsht);
  if (!nm_na(o->tpz)) tn_cat(t, 600, L" · Тпз "), nm_catmin(t, 600, o->tpz);
  if (!nm_na(o->tshk)) tn_cat(t, 600, L" · Тшк "), nm_catmin(t, 600, o->tshk);
  RECT res = {x0, y, right, y + NUS(26)};
  DrawTextW(hdc, t, -1, &res, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  y += NUS(28);
  if (g_fontSmall) SelectObject(hdc, g_fontSmall);
  if (o->toNeed[0]) {
    SetTextColor(hdc, RGB(185, 74, 34));
    _snwprintf(t, 600, L"Для машинного То нужно: %s", o->toNeed);
  } else {
    SetTextColor(hdc, COL_MUTED);
    lstrcpynW(t, L"Сотрите значение — CursorPad посчитает его сам. «Применить» — нормы в ответе чата обновятся на месте.", 600);
  }
  t[599] = 0;
  RECT h = {x0, y, right, foot - NUS(12)};
  DrawTextW(hdc, t, -1, &h, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX | DT_EDITCONTROL);
}

/* нормы в последнем ответе беседы — на месте: от «Нормы времени (ОМН…» до строки «Режимы: …»
   включительно встаёт короткий текст норм с правками окна; новых сообщений в ленте нет. Правки —
   в вопрос беседы (g_nmLast), чтобы и «норма: …» дальше считала с ними. FALSE — ответа с нормами нет */
static BOOL nu_apply(void) {
  if (!g_nuRes || !g_nmLast || !g_nuBaseOk) return FALSE;
  int at = -1;
  wchar_t *p = NULL;
  for (int i = g_acN - 1; i >= 0 && at < 0; i--)
    if (g_acTurns[i].assistant && g_acTurns[i].text && (p = wcsstr(g_acTurns[i].text, L"\n\nНормы времени (ОМН")))
      at = i;
  if (at < 0) return FALSE;
  wchar_t *old = g_acTurns[at].text, *end = old + wcslen(old), *m = wcsstr(p, L"\n" NM_MODES_HEAD);
  if (m) {
    wchar_t *nl = wcschr(m + 1, L'\n');
    if (nl) end = nl;
  }
  wchar_t *nm = (wchar_t *)malloc(sizeof(wchar_t) * NM_OUT);
  if (!nm) return FALSE;
  nm[0] = 0;
  nm_text_brief(&g_nuBase, g_nuRes, g_nuDir, nm, NM_OUT);
  size_t a = (size_t)(p - old), n = wcslen(nm), z = wcslen(end);
  wchar_t *t = (wchar_t *)malloc((a + n + z + 1) * sizeof(wchar_t));
  if (!t) {
    free(nm);
    return FALSE;
  }
  memcpy(t, old, a * sizeof(wchar_t));
  memcpy(t + a, nm, n * sizeof(wchar_t));
  memcpy(t + a + n, end, (z + 1) * sizeof(wchar_t));
  free(nm);
  free(old);
  g_acTurns[at].text = t;
  /* правки — в вопрос беседы; длинно — старое отрезаем, как nm_follow_text */
  wchar_t ov[1500], ask[2200];
  nu_overrides(ov, 1500, FALSE);
  if (ov[0]) {
    _snwprintf(ask, 2200, L"%s\n%s", g_nmLast->ask, ov);
    ask[2199] = 0;
    size_t len = wcslen(ask);
    lstrcpynW(g_nmLast->ask, ask + (len > 599 ? len - 599 : 0), 600);
  }
  nu_reset_all();
  g_acRendered = 0;
  if (g_acWnd) ac_redraw();
  ac_hist_save();
  return TRUE;
}

/* «Применить»: нормы в ответе беседы — на месте; ответа с нормами нет — «норма: …» в чат или в буфер */
static void nu_to_chat(void) {
  if (nu_apply()) {
    nu_calc();
    return;
  }
  wchar_t ov[1500], q[1600];
  nu_overrides(ov, 1500, TRUE);
  _snwprintf(q, 1600, L"норма%s%s", ov[0] ? L": " : L"", ov);
  q[1599] = 0;
  if (g_acWnd && g_acIn) {
    ac_route_say(q);
    nu_reset_all(); /* правки теперь в вопросе беседы */
    nu_calc();
  } else if (clipboard_set(q)) {
    MessageBeep(MB_OK);
  }
}

static LRESULT CALLBACK nu_edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_KEYDOWN && wp == VK_RETURN) {
    KillTimer(g_nuWnd, TIMER_NU_CALC);
    nu_calc();
    return 0;
  }
  if (msg == WM_KEYDOWN && wp == VK_TAB) { /* к следующему видимому полю */
    int i = GetDlgCtrlID(h) - ID_NU_EDIT, step = GetKeyState(VK_SHIFT) < 0 ? NU_ROWS - 1 : 1;
    for (int k = 1; k <= NU_ROWS; k++) {
      HWND e = g_nuEdit[(i + k * step) % NU_ROWS];
      if (IsWindowVisible(e)) {
        SetFocus(e);
        SendMessageW(e, EM_SETSEL, 0, -1);
        break;
      }
    }
    return 0;
  }
  if (msg == WM_CHAR && (wp == L'\r' || wp == L'\t')) return 0; /* без звука */
  return CallWindowProcW(g_nuEditProc, h, msg, wp, lp);
}

static LRESULT CALLBACK NuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    nu_paint(hwnd, mem);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_PRINTCLIENT: /* снимок окна (стенд) */
    nu_paint(hwnd, (HDC)wParam);
    return 0;
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = NUS(820);
    mm->ptMinTrackSize.y = NUS(640);
    return 0;
  }
  case WM_SIZE:
    nu_layout();
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_TIMER:
    if (wParam == TIMER_NU_CALC) {
      KillTimer(hwnd, TIMER_NU_CALC);
      nu_calc();
    }
    return 0;
  case WM_COMMAND: {
    int id = LOWORD(wParam);
    if (id == ID_NU_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
      int i = (int)SendMessageW(g_nuList, LB_GETCURSEL, 0, 0);
      if (i >= 0 && i != g_nuCur) g_nuCur = i, nu_refresh();
      return 0;
    }
    if (id >= ID_NU_EDIT && id < ID_NU_EDIT + NU_ROWS && HIWORD(wParam) == EN_CHANGE) {
      if (g_nuFill) return 0;
      wchar_t *ov = nu_ov(g_nuCur, id - ID_NU_EDIT), v[48];
      if (!ov) return 0;
      GetWindowTextW((HWND)lParam, v, 48);
      const wchar_t *s = nm_skipsp(v);
      size_t n = wcslen(s);
      while (n && (s[n - 1] == L' ' || s[n - 1] == 0xA0)) n--;
      lstrcpynW(ov, s, (int)n + 1); /* пусто — правки нет, снова как считает CursorPad */
      SetTimer(hwnd, TIMER_NU_CALC, 500, NULL);
      return 0;
    }
    if (id == ID_NU_RESET) {
      nu_reset_all();
      SetFocus(g_nuList);
      nu_calc();
    } else if (id == ID_NU_CHAT) {
      nu_to_chat();
    } else if (id == ID_PANEL_CLOSE) {
      DestroyWindow(hwnd);
    }
    return 0;
  }
  case WM_KEYDOWN:
    if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    KillTimer(hwnd, TIMER_NU_CALC);
    g_nuWnd = NULL;
    free(g_nuRes);
    g_nuRes = NULL;
    if (g_nuBaseOk) nm_base_free(&g_nuBase);
    g_nuBaseOk = FALSE;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* открыть окно у последнего маршрута беседы (g_nmLast) — кнопка «Режимы» под маршрутом в чате */
static void nu_open(void) {
  if (g_nuWnd) {
    nu_calc();
    ShowWindow(g_nuWnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_nuWnd);
    return;
  }
  HDC s = GetDC(NULL);
  g_nuS = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
  if (s) ReleaseDC(NULL, s);
  g_nuRes = (NmResult *)calloc(1, sizeof(NmResult));
  if (!g_nuRes) return;
  g_nuBaseOk = nm_base_load(&g_nuBase, g_nuDir);
  g_nuFor = NULL;
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = NuProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = g_paper;
  wc.lpszClassName = L"CursorPadNorms";
  RegisterClassExW(&wc);
  int w = NUS(880), h = NUS(660);
  HMONITOR mon = MonitorFromWindow(g_acWnd ? g_acWnd : g_hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(mon, &mi);
  int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2, y = mi.rcWork.top + NUS(60);
  g_nuWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CursorPadNorms", L"Режимы и нормы — CursorPad",
                            WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU, x, y, w, h, NULL, NULL, g_inst,
                            NULL);
  if (!g_nuWnd) {
    free(g_nuRes);
    g_nuRes = NULL;
    if (g_nuBaseOk) nm_base_free(&g_nuBase);
    g_nuBaseOk = FALSE;
    return;
  }
  round_corners(g_nuWnd);
  mk_btn(g_nuWnd, L"×", ID_PANEL_CLOSE);
  tool_frame_init(g_nuWnd);
  mk_btn(g_nuWnd, L"Сбросить правки", ID_NU_RESET);
  mk_btn(g_nuWnd, L"Применить", ID_NU_CHAT);
  g_nuList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                             0, 0, 10, 10, g_nuWnd, (HMENU)(INT_PTR)ID_NU_LIST, g_inst, NULL);
  if (g_fontBody) SendMessageW(g_nuList, WM_SETFONT, (WPARAM)g_fontBody, FALSE);
  for (int i = 0; i < NU_ROWS; i++) {
    g_nuEdit[i] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, g_nuWnd,
                                  (HMENU)(INT_PTR)(ID_NU_EDIT + i), g_inst, NULL);
    if (g_fontBody) SendMessageW(g_nuEdit[i], WM_SETFONT, (WPARAM)g_fontBody, FALSE);
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(g_nuEdit[i], GWLP_WNDPROC, (LONG_PTR)nu_edit_proc);
    if (!g_nuEditProc) g_nuEditProc = old;
  }
  nu_layout();
  nu_calc();
  ShowWindow(g_nuWnd, SW_SHOWNORMAL);
  SetForegroundWindow(g_nuWnd);
}

/* «режимы», «режимы резания», «изменить режимы» — открыть окно (команда чата, без модели) */
static BOOL nu_chat_cmd(const wchar_t *a) {
  wchar_t *f = tn_fold(a, 0);
  if (!f) return FALSE;
  wchar_t *p = f;
  while (*p == L' ') p++;
  size_t n = wcslen(p);
  while (n && (p[n - 1] == L' ' || p[n - 1] == L'.' || p[n - 1] == L'!')) p[--n] = 0;
  BOOL yes = !wcscmp(p, L"режимы") || !wcscmp(p, L"режимы резания") || !wcscmp(p, L"изменить режимы") ||
             !wcscmp(p, L"режимы и нормы");
  free(f);
  return yes;
}
