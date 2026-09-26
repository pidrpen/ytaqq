/* ---- Окно «Расчёт краски» ----------------------------------------------------

   Перенос «Краска — расчёт.xlsx» (pidrpen/wowdroch) в CursorPad — как с
   расчётом резки: своё окно, открывается сразу, Excel не нужен. Формулы —
   paint_calc.c (сверены с файлом на его примерах и на 60 случайных деталях:
   площадь, расход каждого слоя и итоги по материалам — до последнего знака).

   Слева — деталь: профиль (круг, лист, труба, квадратная труба), размеры,
   длина, количество, «торцы» и «внутри», покрытие (эмаль / лак, грунтовка,
   клей — материал и число слоёв). Справа сразу видно площадь и расход этой
   детали; «Добавить в список» (или Enter) — в список деталей, под ним итог
   по материалам, как на листе «Итого». «Копировать для Excel» кладёт в буфер
   таблицу теми же столбцами, что в файле, — вставляется в Excel как есть.
   Ниже — нормы расхода (лист «Нормы»): можно поменять норму, добавить или
   убрать материал; расчёт тут же пересчитывается.

   Вид — как у расчёта резки (бежевый фон, карточки, крупные цифры). Всё, что
   ввели, и нормы хранятся в paint.txt рядом с заметками: после закрытия и
   после обновления программы список на месте. */

#include "paint_calc.c"

#define ID_PN_DESC 720
#define ID_PN_D1 721
#define ID_PN_D2 722
#define ID_PN_LEN 723
#define ID_PN_QTY 724
#define ID_PN_MAT0 725 /* 725..727 — материал слоя */
#define ID_PN_LAY0 728 /* 728..730 — слоёв */
#define ID_PN_NEWNAME 731
#define ID_PN_NEWNORM 732
#define ID_PN_NORM0 740 /* 740..759 — нормы */
#define TIMER_PN_COPIED 1
#define WM_PN_ADD (WM_APP + 60) /* Enter в поле — добавить деталь */

#define PN_MAXMAT 20
#define PN_MAXPART 300

typedef struct {
  wchar_t name[PN_NAME];
  double norm; /* кг/м² за один слой */
} PnMat;

/* лист «Нормы» файла */
static const PnMat kPnDefMats[] = {
    {L"Эмаль ПФ-115(к)", 0.27}, {L"Эмаль ПФ-231", 0.13}, {L"Эмаль ПФ-115(ж)", 0.15}, {L"Эмаль ПФ-118", 0.10},
    {L"Лак ЛБС", 0.12},         {L"Эмаль ПФ-223", 0.27}, {L"Грунтовка ФЛ-03К", 0.09}, {L"Клей К300-61", 0.35},
};
static const wchar_t *const kPnProf[PN_NPROF] = {L"Круг", L"Лист", L"Труба", L"Кв. труба"};
static const wchar_t *const kPnCoat[PN_NCOAT] = {L"Эмаль / лак", L"Грунтовка", L"Клей"};

static PnMat g_pnMat[PN_MAXMAT];
static int g_pnNMat;
static PnPart g_pnCur;
static PnPart *g_pnList;
static int g_pnN;
static BOOL g_pnCopied;

static HWND g_pnWnd, g_pnCanvas;
static HWND g_pnDesc, g_pnD1, g_pnD2, g_pnLen, g_pnQty, g_pnMatCb[PN_NCOAT], g_pnLay[PN_NCOAT];
static HWND g_pnNorm[PN_MAXMAT], g_pnNewName, g_pnNewNorm;
static BOOL g_pnFilling; /* сами пишем в поля — EN_CHANGE не считать вводом */
static BOOL g_pnPlace;   /* проход раскладки: двигать поля */
static WNDPROC g_pnOldEdit, g_pnOldCombo;

static int g_pnScroll, g_pnContentH;
static float g_pnS = 1.0f, g_pnDpi = 1.0f, g_pnZoom = 1.0f;
static BOOL g_pnTwo = TRUE;
static HFONT g_pf[10];
static HBRUSH g_pnSurfBrush;

typedef struct {
  RECT r;
  int what;
} PnHot;
#define PN_MAXHOT (PN_MAXPART + PN_MAXMAT + 40)
static PnHot g_pnHot[PN_MAXHOT];
static int g_pnNHot;
/* что где: 1..4 — профиль, 20 — торцы, 21 — внутри, 30 — добавить, 31 — копировать,
   32 — очистить список, 40 — добавить материал, 41 — исходные нормы,
   100+i — убрать деталь i, 1000+j — убрать материал j */

static int PS(int v) { return (int)(v * g_pnS + 0.5f); }

static void pn_fonts(void) {
  for (int i = 0; i < 10; i++)
    if (g_pf[i]) DeleteObject(g_pf[i]);
  struct {
    int pt, w;
    const wchar_t *face;
  } f[10] = {{15, FW_BOLD, L"Segoe UI"},  {10, FW_NORMAL, L"Segoe UI"}, {13, FW_BOLD, L"Segoe UI"},
             {9, FW_BOLD, L"Segoe UI"},   {10, FW_NORMAL, L"Segoe UI"}, {17, FW_HEAVY, L"Segoe UI"},
             {10, FW_BOLD, L"Segoe UI"},  {8, FW_NORMAL, L"Consolas"},  {9, FW_SEMIBOLD, L"Segoe UI"},
             {12, FW_SEMIBOLD, L"Segoe UI"}};
  for (int i = 0; i < 10; i++)
    g_pf[i] = CreateFontW(-PS(f[i].pt * 96 / 72), 0, 0, 0, f[i].w, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, f[i].face);
}

static int pn_text(HDC dc, int font, COLORREF c, const wchar_t *s, RECT r, UINT fl, BOOL paint) {
  SelectObject(dc, g_pf[font]);
  RECT m = r;
  DrawTextW(dc, s, -1, &m, fl | DT_CALCRECT | DT_NOPREFIX);
  if (paint) {
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fl | DT_NOPREFIX);
  }
  return m.bottom - m.top;
}

static int pn_text_w(HDC dc, int font, const wchar_t *s) {
  SelectObject(dc, g_pf[font]);
  SIZE sz;
  GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
  return sz.cx;
}

static void pn_hot(RECT r, int what) {
  if (g_pnNHot < PN_MAXHOT) {
    g_pnHot[g_pnNHot].r = r;
    g_pnHot[g_pnNHot].what = what;
    g_pnNHot++;
  }
}

/* ---- числа ---------------------------------------------------------------------- */

static double pn_parse_str(const wchar_t *s0) {
  wchar_t t[64];
  lstrcpynW(t, s0, 64);
  for (wchar_t *c = t; *c; c++)
    if (*c == L',') *c = L'.';
  wchar_t *end = NULL;
  double v = wcstod(t, &end);
  if (end == t || v != v) return 0;
  return v;
}

static double pn_parse(HWND e) {
  wchar_t t[64];
  GetWindowTextW(e, t, 64);
  return pn_parse_str(t);
}

/* для полей и для Excel: без пробелов в тысячах, запятая, лишние нули долой */
static void pn_num(double v, int dec, wchar_t *out, int cap) {
  _snwprintf(out, cap, L"%.*f", dec, v);
  out[cap - 1] = 0;
  wchar_t *dot = wcschr(out, L'.');
  if (dot) {
    *dot = L',';
    size_t n = wcslen(out);
    while (n > 0 && out[n - 1] == L'0') out[--n] = 0;
    if (n > 0 && out[n - 1] == L',') out[--n] = 0;
  }
  if (!wcscmp(out, L"-0")) lstrcpynW(out, L"0", cap);
}

/* кг и м²: мелкое — точнее, чтобы не было «0» у маленькой детали */
static void pn_fmt(double v, wchar_t *out, int cap) { cut_fmt(v, v < 0.1 ? 4 : 3, out, cap); }

/* ---- материалы ------------------------------------------------------------------- */

static int pn_find_mat(const wchar_t *name) {
  if (!name || !name[0]) return -1;
  for (int j = 0; j < g_pnNMat; j++)
    if (!wcscmp(g_pnMat[j].name, name)) return j;
  return -1;
}

static void pn_default_mats(void) {
  g_pnNMat = (int)(sizeof(kPnDefMats) / sizeof(kPnDefMats[0]));
  memcpy(g_pnMat, kPnDefMats, sizeof(kPnDefMats));
}

static void pn_default_cur(void) {
  memset(&g_pnCur, 0, sizeof(g_pnCur));
  g_pnCur.prof = PN_ROUND;
  g_pnCur.qty = 1;
  lstrcpynW(g_pnCur.mat[0], L"Эмаль ПФ-115(к)", PN_NAME);
  lstrcpynW(g_pnCur.mat[1], L"Грунтовка ФЛ-03К", PN_NAME);
  for (int k = 0; k < PN_NCOAT; k++) g_pnCur.lay[k] = 1;
}

/* слой детали: кг; *state — 0 не выбран, 1 посчитан, 2 материала нет в нормах */
static double pn_layer(const PnPart *p, int k, int *state) {
  *state = 0;
  if (!p->mat[k][0]) return 0;
  int j = pn_find_mat(p->mat[k]);
  if (j < 0) {
    *state = 2;
    return 0;
  }
  double v = pn_kg(p, k, g_pnMat[j].norm);
  if (v < 0) return 0;
  *state = 1;
  return v;
}

/* «Круг Ø40, L=1200 — 3 шт» */
static void pn_describe(const PnPart *p, wchar_t *out, int cap) {
  wchar_t a[32], b[32], l[32], q[48] = L"";
  cut_fmt(p->d1, 2, a, 32);
  cut_fmt(p->d2, 2, b, 32);
  cut_fmt(p->len, 1, l, 32);
  if (p->qty > 1) _snwprintf(q, 48, L" — %d шт", p->qty);
  switch (p->prof) {
  case PN_ROUND: _snwprintf(out, cap, L"Круг Ø%s, L=%s%s", a, l, q); break;
  case PN_SHEET:
    if (p->d2 > 0) _snwprintf(out, cap, L"Лист %s×%s, L=%s%s", a, b, l, q);
    else _snwprintf(out, cap, L"Лист %s, L=%s%s", a, l, q);
    break;
  case PN_PIPE:
    if (p->d2 > 0) _snwprintf(out, cap, L"Труба Ø%s×%s, L=%s%s", a, b, l, q);
    else _snwprintf(out, cap, L"Труба Ø%s, L=%s%s", a, l, q);
    break;
  default: _snwprintf(out, cap, L"Кв. труба %s×%s, L=%s%s", a, b, l, q); break;
  }
  out[cap - 1] = 0;
}

/* ---- хранение: paint.txt (UTF-8, строки через табуляцию) ------------------------ */

static void pn_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\paint.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static void pn_clean(const wchar_t *s, wchar_t *out, int cap) {
  lstrcpynW(out, s, cap);
  for (wchar_t *c = out; *c; c++)
    if (*c == L'\t' || *c == L'\r' || *c == L'\n') *c = L' ';
}

static void pn_part_line(ShareBuf *b, const wchar_t *tag, const PnPart *p) {
  wchar_t m[PN_NCOAT][PN_NAME], d[80];
  for (int k = 0; k < PN_NCOAT; k++) pn_clean(p->mat[k], m[k], PN_NAME);
  pn_clean(p->desc, d, 80);
  sb_add(b, L"%s\t%d\t%.10g\t%.10g\t%.10g\t%d\t%d\t%d\t%s\t%d\t%s\t%d\t%s\t%d\t%s\n", tag, p->prof, p->d1, p->d2,
         p->len, p->qty, p->ends, p->inside, m[0], p->lay[0], m[1], p->lay[1], m[2], p->lay[2], d);
}

static void pn_save(void) {
  if (!g_dataDir[0]) return;
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  RECT r = {0, 0, 0, 0};
  if (g_pnWnd) {
    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    if (GetWindowPlacement(g_pnWnd, &wp)) r = wp.rcNormalPosition;
  }
  sb_add(&b, L"view\t%d\t%ld\t%ld\t%ld\t%ld\n", (int)(g_pnZoom * 100 + 0.5f), r.left, r.top, r.right - r.left,
         r.bottom - r.top);
  for (int j = 0; j < g_pnNMat; j++) {
    wchar_t n[PN_NAME];
    pn_clean(g_pnMat[j].name, n, PN_NAME);
    sb_add(&b, L"norm\t%s\t%.10g\n", n, g_pnMat[j].norm);
  }
  pn_part_line(&b, L"cur", &g_pnCur);
  for (int i = 0; i < g_pnN; i++) pn_part_line(&b, L"item", &g_pnList[i]);
  wchar_t p[MAX_PATH];
  pn_path(p);
  if (b.w) share_write_ex(p, b.w, TRUE); /* целиком через временный: при сбое старый файл цел */
  free(b.w);
}

static BOOL pn_read_part(wchar_t **f, int n, PnPart *p) {
  if (n < 14) return FALSE;
  memset(p, 0, sizeof(*p));
  p->prof = _wtoi(f[1]);
  if (p->prof < 0 || p->prof >= PN_NPROF) p->prof = PN_ROUND;
  p->d1 = pn_parse_str(f[2]);
  p->d2 = pn_parse_str(f[3]);
  p->len = pn_parse_str(f[4]);
  p->qty = _wtoi(f[5]);
  p->ends = _wtoi(f[6]) != 0;
  p->inside = _wtoi(f[7]) != 0;
  for (int k = 0; k < PN_NCOAT; k++) {
    lstrcpynW(p->mat[k], f[8 + k * 2], PN_NAME);
    p->lay[k] = _wtoi(f[9 + k * 2]);
  }
  if (n > 14) lstrcpynW(p->desc, f[14], 80);
  return TRUE;
}

/* view — масштаб и место окна; FALSE — места нет или оно не на этом экране */
static BOOL pn_load(RECT *place) {
  pn_default_mats();
  pn_default_cur();
  g_pnN = 0;
  if (!g_dataDir[0]) return FALSE;
  wchar_t p[MAX_PATH];
  pn_path(p);
  wchar_t *t = share_read(p);
  if (!t) return FALSE;
  BOOL havePlace = FALSE, normsSeen = FALSE;
  wchar_t *pp = t, *line;
  while ((line = share_next_line(&pp)) != NULL) {
    wchar_t *f[20];
    int n = share_split(line, f, 20);
    if (n < 1) continue;
    if (!wcscmp(f[0], L"view") && n >= 6) {
      int z = _wtoi(f[1]);
      if (z >= 60 && z <= 160) g_pnZoom = z / 100.0f;
      long x = _wtol(f[2]), y = _wtol(f[3]), w = _wtol(f[4]), h = _wtol(f[5]);
      RECT r = {x, y, x + w, y + h};
      if (w >= 300 && h >= 250 && MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
        *place = r;
        havePlace = TRUE;
      }
    } else if (!wcscmp(f[0], L"norm") && n >= 3) {
      if (!normsSeen) { /* свои нормы есть — исходные не нужны */
        normsSeen = TRUE;
        g_pnNMat = 0;
      }
      if (g_pnNMat < PN_MAXMAT && f[1][0] && pn_find_mat(f[1]) < 0) {
        lstrcpynW(g_pnMat[g_pnNMat].name, f[1], PN_NAME);
        g_pnMat[g_pnNMat].norm = pn_parse_str(f[2]);
        g_pnNMat++;
      }
    } else if (!wcscmp(f[0], L"cur")) {
      pn_read_part(f, n, &g_pnCur);
    } else if (!wcscmp(f[0], L"item") && g_pnN < PN_MAXPART) {
      if (pn_read_part(f, n, &g_pnList[g_pnN])) g_pnN++;
    }
  }
  free(t);
  return havePlace;
}

/* ---- поля ------------------------------------------------------------------------ */

static void pn_set_num(HWND e, double v) {
  wchar_t t[40] = L"";
  if (v > 0) pn_num(v, 6, t, 40);
  SetWindowTextW(e, t);
}

static void pn_fill_combo(int k) {
  HWND c = g_pnMatCb[k];
  SendMessageW(c, CB_RESETCONTENT, 0, 0);
  SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"— нет —");
  int sel = 0;
  for (int j = 0; j < g_pnNMat; j++) {
    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)g_pnMat[j].name);
    if (!wcscmp(g_pnMat[j].name, g_pnCur.mat[k])) sel = j + 1;
  }
  if (g_pnCur.mat[k][0] && !sel) { /* материала больше нет в нормах — слой снимается */
    g_pnCur.mat[k][0] = 0;
  }
  SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
}

static void pn_to_fields(void) {
  g_pnFilling = TRUE;
  SetWindowTextW(g_pnDesc, g_pnCur.desc);
  pn_set_num(g_pnD1, g_pnCur.d1);
  pn_set_num(g_pnD2, g_pnCur.d2);
  pn_set_num(g_pnLen, g_pnCur.len);
  pn_set_num(g_pnQty, g_pnCur.qty);
  for (int k = 0; k < PN_NCOAT; k++) {
    pn_fill_combo(k);
    pn_set_num(g_pnLay[k], g_pnCur.lay[k]);
  }
  g_pnFilling = FALSE;
}

static void pn_from_fields(void) {
  GetWindowTextW(g_pnDesc, g_pnCur.desc, 80);
  g_pnCur.d1 = pn_parse(g_pnD1);
  g_pnCur.d2 = g_pnCur.prof == PN_ROUND ? 0 : pn_parse(g_pnD2);
  g_pnCur.len = pn_parse(g_pnLen);
  double q = pn_parse(g_pnQty);
  g_pnCur.qty = q >= 1 ? (int)(q + 0.5) : 0;
  for (int k = 0; k < PN_NCOAT; k++) {
    int s = (int)SendMessageW(g_pnMatCb[k], CB_GETCURSEL, 0, 0);
    if (s >= 1 && s <= g_pnNMat) lstrcpynW(g_pnCur.mat[k], g_pnMat[s - 1].name, PN_NAME);
    else g_pnCur.mat[k][0] = 0;
    double l = pn_parse(g_pnLay[k]);
    g_pnCur.lay[k] = l >= 1 ? (int)(l + 0.5) : 0;
  }
}

/* поля норм — по одному на материал */
static LRESULT CALLBACK PnFieldProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static HWND pn_edit(const wchar_t *text, int id) {
  HWND e = CreateWindowExW(0, L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 26,
                           g_pnCanvas, (HMENU)(INT_PTR)id, g_inst, NULL);
  SendMessageW(e, WM_SETFONT, (WPARAM)g_pf[9], FALSE);
  g_pnOldEdit = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)PnFieldProc);
  return e;
}

static void pn_make_norm_fields(void) {
  for (int j = 0; j < PN_MAXMAT; j++)
    if (g_pnNorm[j]) {
      DestroyWindow(g_pnNorm[j]);
      g_pnNorm[j] = NULL;
    }
  g_pnFilling = TRUE;
  for (int j = 0; j < g_pnNMat; j++) {
    wchar_t t[32];
    pn_num(g_pnMat[j].norm, 4, t, 32);
    g_pnNorm[j] = pn_edit(t, ID_PN_NORM0 + j);
  }
  g_pnFilling = FALSE;
}

/* ---- рисование ---------------------------------------------------------------- */

/* поле ввода: подпись и рамка, само поле — поверх рамки */
static int pn_field(HDC dc, int x, int w, int y, const wchar_t *label, HWND ctl, BOOL paint) {
  y += pn_text(dc, CF_LABEL, CC_MUTED, label, (RECT){x, y, x + w, y + 60}, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS,
               paint) + PS(6);
  RECT r = {x, y, x + w, y + PS(40)};
  if (paint) {
    BOOL foc = GetFocus() == ctl;
    cc_round(dc, r, PS(9), CC_SURF, foc ? CC_PRIM : CC_BORDER);
  }
  if (g_pnPlace && ctl) {
    MoveWindow(ctl, r.left + PS(10), r.top + PS(8) - g_pnScroll, w - PS(20), PS(24), TRUE);
    ShowWindow(ctl, SW_SHOWNA);
  }
  return y + PS(40);
}

static int pn_button(HDC dc, const wchar_t *t, int x, int y, BOOL prim, int what, BOOL paint) {
  int w = pn_text_w(dc, CF_BOLD, t) + PS(32), h = PS(38);
  RECT r = {x, y, x + w, y + h};
  if (paint) {
    cc_round(dc, r, PS(10), prim ? CC_PRIM : CC_SURF, prim ? CC_PRIM : CC_BORDER);
    pn_text(dc, CF_BOLD, prim ? RGB(0xFF, 0xFF, 0xFF) : CC_INK, t, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, TRUE);
  }
  pn_hot(r, what);
  return w;
}

static void pn_check(HDC dc, int x, int y, const wchar_t *t, BOOL on, int what, BOOL paint, int *outW) {
  int bs = PS(20);
  int tw = pn_text_w(dc, CF_BODY, t);
  RECT hit = {x, y, x + bs + PS(8) + tw, y + PS(26)};
  if (paint) {
    RECT b = {x, y + PS(3), x + bs, y + PS(3) + bs};
    cc_round(dc, b, PS(5), on ? CC_PRIM : CC_SURF, on ? CC_PRIM : CC_BORDER);
    if (on) { /* галочка */
      HPEN pen = CreatePen(PS_SOLID, PS(2) < 2 ? 2 : PS(2), RGB(0xFF, 0xFF, 0xFF));
      HGDIOBJ op = SelectObject(dc, pen);
      MoveToEx(dc, b.left + bs * 5 / 20, b.top + bs * 10 / 20, NULL);
      LineTo(dc, b.left + bs * 8 / 20, b.top + bs * 14 / 20);
      LineTo(dc, b.left + bs * 15 / 20, b.top + bs * 6 / 20);
      SelectObject(dc, op);
      DeleteObject(pen);
    }
    pn_text(dc, CF_BODY, CC_INK, t, (RECT){x + bs + PS(8), y, hit.right + 4, y + PS(26)},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE, TRUE);
  }
  pn_hot(hit, what);
  *outW = hit.right - hit.left;
}

static const wchar_t *pn_formula(int prof) {
  switch (prof) {
  case PN_ROUND: return L"Площадь: π·D·L, с торцами ещё 2·πD²/4";
  case PN_SHEET: return L"Площадь: 2·Ш·L (обе стороны), с кромками ещё 2·(Ш+L)·Т";
  case PN_PIPE: return L"Площадь: π·D·L; внутри ещё π·(D−2s)·L; торцы — кольца";
  default: return L"Площадь: 4·a·L; внутри ещё 4·(a−2s)·L; торцы — рамки";
  }
}

/* левая колонка: деталь и покрытие */
static int pn_left_body(HDC dc, int x, int w, int y, BOOL paint) {
  y += pn_text(dc, CF_TITLE, CC_INK, L"Какая деталь?", (RECT){x, y, x + w, y + 60}, DT_LEFT | DT_SINGLELINE, paint) +
       PS(4);
  y += pn_text(dc, CF_SUB, CC_MUTED, L"Размеры в мм — площадь и расход считаются сразу",
               (RECT){x, y, x + w, y + 200}, DT_LEFT | DT_WORDBREAK, paint) + PS(16);
  /* профиль — четыре «пилюли» */
  y += pn_text(dc, CF_LABEL, CC_MUTED, L"Профиль", (RECT){x, y, x + w, y + 40}, DT_LEFT | DT_SINGLELINE, paint) +
       PS(6);
  int gap = PS(6), pw = (w - gap * 3) / 4, ph = PS(36);
  for (int i = 0; i < PN_NPROF; i++) {
    RECT r = {x + i * (pw + gap), y, x + i * (pw + gap) + pw, y + ph};
    BOOL on = g_pnCur.prof == i;
    if (paint) {
      cc_round(dc, r, ph / 2, on ? CC_PRIM : CC_PILL, on ? CC_PRIM : CC_BORDER);
      pn_text(dc, CF_SMALLB, on ? RGB(0xFF, 0xFF, 0xFF) : CC_MUTED, kPnProf[i], r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE, TRUE);
    }
    pn_hot(r, 1 + i);
  }
  y += ph + PS(8);
  y += pn_text(dc, CF_SMALLB, CC_FAINT, pn_formula(g_pnCur.prof), (RECT){x, y, x + w, y + 200},
               DT_LEFT | DT_WORDBREAK, paint) + PS(14);
  y = pn_field(dc, x, w, y, L"Обозначение (можно пусто)", g_pnDesc, paint) + PS(12);
  int hw = (w - PS(12)) / 2;
  static const wchar_t *const l1[PN_NPROF] = {L"Диаметр, мм", L"Ширина, мм", L"Наружный Ø, мм", L"Сторона, мм"};
  static const wchar_t *const l2[PN_NPROF] = {L"", L"Толщина, мм", L"Стенка, мм", L"Стенка, мм"};
  if (g_pnCur.prof == PN_ROUND) {
    y = pn_field(dc, x, w, y, l1[0], g_pnD1, paint) + PS(12);
    if (g_pnPlace) ShowWindow(g_pnD2, SW_HIDE);
  } else {
    pn_field(dc, x, hw, y, l1[g_pnCur.prof], g_pnD1, paint);
    y = pn_field(dc, x + hw + PS(12), hw, y, l2[g_pnCur.prof], g_pnD2, paint) + PS(12);
  }
  pn_field(dc, x, hw, y, L"Длина, мм", g_pnLen, paint);
  y = pn_field(dc, x + hw + PS(12), hw, y, L"Кол-во, шт", g_pnQty, paint) + PS(14);
  int cw = 0;
  pn_check(dc, x, y, g_pnCur.prof == PN_SHEET ? L"Кромки (торцы)" : L"Торцы", g_pnCur.ends, 20, paint, &cw);
  if (g_pnCur.prof == PN_PIPE || g_pnCur.prof == PN_SQPIPE)
    pn_check(dc, x + cw + PS(24), y, L"Внутри", g_pnCur.inside, 21, paint, &cw);
  y += PS(26) + PS(20);
  /* покрытие */
  if (paint) cc_fill(dc, (RECT){x, y, x + w, y + 1}, CC_BORDER);
  y += PS(14);
  y += pn_text(dc, CF_TITLE, CC_INK, L"Покрытие", (RECT){x, y, x + w, y + 60}, DT_LEFT | DT_SINGLELINE, paint) +
       PS(10);
  int lw = PS(78);
  for (int k = 0; k < PN_NCOAT; k++) {
    int cbw = w - lw - PS(10);
    pn_text(dc, CF_LABEL, CC_MUTED, kPnCoat[k], (RECT){x, y, x + cbw, y + 40}, DT_LEFT | DT_SINGLELINE, paint);
    y += pn_text(dc, CF_LABEL, CC_MUTED, L"Слоёв", (RECT){x + cbw + PS(10), y, x + w, y + 40},
                 DT_LEFT | DT_SINGLELINE, paint) + PS(6);
    if (g_pnPlace) MoveWindow(g_pnMatCb[k], x, y - g_pnScroll, cbw, PS(300), TRUE);
    RECT lr = {x + cbw + PS(10), y, x + w, y + PS(40)};
    if (paint) cc_round(dc, lr, PS(9), CC_SURF, GetFocus() == g_pnLay[k] ? CC_PRIM : CC_BORDER);
    if (g_pnPlace)
      MoveWindow(g_pnLay[k], lr.left + PS(10), lr.top + PS(8) - g_pnScroll, lr.right - lr.left - PS(20), PS(24), TRUE);
    y += PS(40) + PS(12);
  }
  return y;
}

/* правая колонка, карточка 1: эта деталь */
static int pn_cur_body(HDC dc, int x, int w, int y, BOOL paint) {
  y += pn_text(dc, CF_TITLE, CC_INK, L"Эта деталь", (RECT){x, y, x + w, y + 60}, DT_LEFT | DT_SINGLELINE, paint) +
       PS(12);
  double a1 = pn_area1(&g_pnCur);
  if (a1 < 0) {
    const wchar_t *why = a1 < -1.5 ? L"Стенка толще половины размера — проверьте числа"
                                   : (g_pnCur.prof == PN_ROUND ? L"Введите диаметр и длину" : L"Введите размер и длину");
    y += pn_text(dc, CF_BODY, a1 < -1.5 ? CC_WARN : CC_MUTED, why, (RECT){x, y, x + w, y + 200},
                 DT_LEFT | DT_WORDBREAK, paint) + PS(8);
    return y;
  }
  double at = pn_area_total(&g_pnCur);
  int qty = g_pnCur.qty > 0 ? g_pnCur.qty : 1;
  /* две плашки: площадь 1 шт и всего */
  int gap = PS(12), cw = (w - gap) / 2, ch = PS(84);
  for (int i = 0; i < 2; i++) {
    RECT r = {x + i * (cw + gap), y, x + i * (cw + gap) + cw, y + ch};
    wchar_t cap[48], big[48], v[40];
    if (i == 0) lstrcpynW(cap, L"Площадь 1 шт", 48);
    else _snwprintf(cap, 48, L"Всего, %d шт", qty);
    pn_fmt(i == 0 ? a1 : at, v, 40);
    _snwprintf(big, 48, L"%s м²", v);
    if (paint) {
      cc_round(dc, r, PS(12), i == 1 ? CC_TIMEBG : RGB(0xF7, 0xF4, 0xEE), CC_BORDER);
      pn_text(dc, CF_LABEL, CC_MUTED, cap, (RECT){r.left + PS(14), r.top + PS(12), r.right - PS(8), r.top + PS(34)},
              DT_LEFT | DT_SINGLELINE, TRUE);
      pn_text(dc, CF_BIG, i == 1 ? CC_PRIM : CC_INK, big,
              (RECT){r.left + PS(14), r.top + PS(36), r.right - PS(8), r.bottom - PS(8)},
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, TRUE);
    }
  }
  y += ch + PS(14);
  /* расход по слоям */
  int any = 0;
  for (int k = 0; k < PN_NCOAT; k++) {
    int st;
    double kg = pn_layer(&g_pnCur, k, &st);
    if (st == 0) continue;
    any++;
    int j = pn_find_mat(g_pnCur.mat[k]);
    wchar_t left[160], right[48], n[32], v[40];
    if (st == 2) {
      _snwprintf(left, 160, L"%s", g_pnCur.mat[k]);
      lstrcpynW(right, L"нет нормы", 48);
    } else {
      pn_num(g_pnMat[j].norm, 4, n, 32);
      int lay = g_pnCur.lay[k] > 0 ? g_pnCur.lay[k] : 1;
      _snwprintf(left, 160, L"%s   ·   %s кг/м² × %d сл.", g_pnCur.mat[k], n, lay);
      pn_fmt(kg, v, 40);
      _snwprintf(right, 48, L"%s кг", v);
    }
    int rw = pn_text_w(dc, CF_BOLD, right) + PS(4);
    if (paint) cc_fill(dc, (RECT){x, y, x + w, y + 1}, CC_BORDER);
    y += PS(10);
    pn_text(dc, CF_BODY, CC_INK, left, (RECT){x, y, x + w - rw - PS(8), y + PS(24)},
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, paint);
    y += pn_text(dc, CF_BOLD, st == 2 ? CC_WARN : CC_INK, right, (RECT){x + w - rw, y, x + w, y + PS(24)},
                 DT_RIGHT | DT_SINGLELINE, paint) + PS(10);
  }
  if (!any)
    y += pn_text(dc, CF_BODY, CC_MUTED, L"Покрытие не выбрано — только площадь", (RECT){x, y, x + w, y + 60},
                 DT_LEFT | DT_SINGLELINE, paint) + PS(10);
  y += PS(8);
  int bw = pn_button(dc, L"Добавить в список", x, y, TRUE, 30, paint);
  pn_text(dc, CF_SMALLB, CC_FAINT, L"или Enter в любом поле", (RECT){x + bw + PS(12), y, x + w, y + PS(38)},
          DT_LEFT | DT_VCENTER | DT_SINGLELINE, paint);
  return y + PS(38);
}

/* итог по материалам: сумма всех слоёв с этим материалом, как лист «Итого» */
static int pn_totals(double *kg, double *area) {
  int nbad = 0;
  *area = 0;
  for (int j = 0; j < g_pnNMat; j++) kg[j] = 0;
  for (int i = 0; i < g_pnN; i++) {
    double a = pn_area_total(&g_pnList[i]);
    if (a > 0) *area += a;
    for (int k = 0; k < PN_NCOAT; k++) {
      int st;
      double v = pn_layer(&g_pnList[i], k, &st);
      if (st == 1) kg[pn_find_mat(g_pnList[i].mat[k])] += v;
      if (st == 2) nbad++;
    }
  }
  return nbad;
}

/* карточка 2: список деталей и итог */
static int pn_list_body(HDC dc, int x, int w, int y, BOOL paint) {
  wchar_t t[96];
  _snwprintf(t, 96, L"Список деталей (%d)", g_pnN);
  y += pn_text(dc, CF_TITLE, CC_INK, t, (RECT){x, y, x + w, y + 60}, DT_LEFT | DT_SINGLELINE, paint) + PS(10);
  if (!g_pnN) {
    y += pn_text(dc, CF_BODY, CC_MUTED,
                 L"Добавленные детали появятся здесь, а под ними — сколько всего каждого материала.",
                 (RECT){x, y, x + w, y + 200}, DT_LEFT | DT_WORDBREAK, paint) + PS(6);
    return y;
  }
  for (int i = 0; i < g_pnN; i++) {
    const PnPart *p = &g_pnList[i];
    wchar_t d[200], nm[120], ar[48], v[40];
    pn_describe(p, nm, 120);
    if (p->desc[0]) _snwprintf(d, 200, L"%d. %s — %s", i + 1, p->desc, nm);
    else _snwprintf(d, 200, L"%d. %s", i + 1, nm);
    double a = pn_area_total(p);
    if (a >= 0) {
      pn_fmt(a, v, 40);
      _snwprintf(ar, 48, L"%s м²", v);
    } else {
      lstrcpynW(ar, L"нет размеров", 48);
    }
    /* что ушло: «Эмаль ПФ-115(к) 0,041 кг · Грунтовка ФЛ-03К 0,014 кг» */
    wchar_t mats[400] = L"";
    for (int k = 0; k < PN_NCOAT; k++) {
      int st;
      double kg = pn_layer(p, k, &st);
      if (st == 0) continue;
      wchar_t one[140];
      if (st == 2) _snwprintf(one, 140, L"%s — нет нормы", p->mat[k]);
      else {
        pn_fmt(kg, v, 40);
        int lay = p->lay[k] > 0 ? p->lay[k] : 1;
        if (lay > 1) _snwprintf(one, 140, L"%s ×%d сл. %s кг", p->mat[k], lay, v);
        else _snwprintf(one, 140, L"%s %s кг", p->mat[k], v);
      }
      if (mats[0]) wcscat(mats, L"  ·  ");
      wcsncat(mats, one, 400 - wcslen(mats) - 1);
    }
    if (!mats[0]) lstrcpynW(mats, L"без покрытия", 400);
    if (paint) cc_fill(dc, (RECT){x, y, x + w, y + 1}, CC_BORDER);
    y += PS(8);
    int xw = PS(26), aw = pn_text_w(dc, CF_BOLD, ar) + PS(4);
    RECT del = {x + w - xw, y, x + w, y + PS(24)};
    if (paint) pn_text(dc, CF_BOLD, CC_FAINT, L"×", del, DT_CENTER | DT_VCENTER | DT_SINGLELINE, TRUE);
    pn_hot(del, 100 + i);
    pn_text(dc, CF_BOLD, CC_INK, ar, (RECT){x + w - xw - PS(6) - aw, y, x + w - xw - PS(6), y + PS(24)},
            DT_RIGHT | DT_SINGLELINE, paint);
    y += pn_text(dc, CF_BODY, CC_INK, d, (RECT){x, y, x + w - xw - aw - PS(16), y + PS(24)},
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, paint) + PS(2);
    y += pn_text(dc, CF_SMALLB, CC_MUTED, mats, (RECT){x, y, x + w - xw - PS(8), y + 400}, DT_LEFT | DT_WORDBREAK,
                 paint) + PS(8);
  }
  /* итог */
  double kg[PN_MAXMAT], area;
  int nbad = pn_totals(kg, &area);
  y += PS(10);
  RECT box = {x, y, x + w, y};
  int by = y + PS(14), bx = x + PS(16), bw = w - PS(32);
  int yy = by;
  yy += pn_text(dc, CF_TITLE, CC_PRIM, L"Итого по материалам", (RECT){bx, yy, bx + bw, yy + 60},
                DT_LEFT | DT_SINGLELINE, FALSE) + PS(10);
  double all = 0;
  int rows = 0;
  for (int j = 0; j < g_pnNMat; j++)
    if (kg[j] > 0) {
      all += kg[j];
      rows++;
    }
  yy += (rows + 2) * PS(28) + PS(8) + (nbad ? PS(28) : 0);
  box.bottom = yy + PS(10);
  if (paint) {
    cc_round(dc, box, PS(12), CC_TIMEBG, CC_BORDER);
    yy = by;
    yy += pn_text(dc, CF_TITLE, CC_PRIM, L"Итого по материалам", (RECT){bx, yy, bx + bw, yy + 60},
                  DT_LEFT | DT_SINGLELINE, TRUE) + PS(10);
    for (int j = 0; j < g_pnNMat; j++) {
      if (!(kg[j] > 0)) continue;
      wchar_t v[40], r[48];
      pn_fmt(kg[j], v, 40);
      _snwprintf(r, 48, L"%s кг", v);
      pn_text(dc, CF_BODY, CC_INK, g_pnMat[j].name, (RECT){bx, yy, bx + bw - PS(120), yy + PS(28)},
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, TRUE);
      pn_text(dc, CF_BOLD, CC_INK, r, (RECT){bx, yy, bx + bw, yy + PS(28)}, DT_RIGHT | DT_VCENTER | DT_SINGLELINE,
              TRUE);
      yy += PS(28);
    }
    cc_fill(dc, (RECT){bx, yy + PS(2), bx + bw, yy + PS(3)}, CC_BORDER);
    yy += PS(8);
    wchar_t v[40], r[64];
    pn_fmt(all, v, 40);
    _snwprintf(r, 64, L"%s кг", v);
    pn_text(dc, CF_BOLD, CC_INK, L"Всего материалов", (RECT){bx, yy, bx + bw, yy + PS(28)},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE, TRUE);
    pn_text(dc, CF_BOLD, CC_PRIM, r, (RECT){bx, yy, bx + bw, yy + PS(28)}, DT_RIGHT | DT_VCENTER | DT_SINGLELINE,
            TRUE);
    yy += PS(28);
    pn_fmt(area, v, 40);
    _snwprintf(r, 64, L"%s м²", v);
    pn_text(dc, CF_BODY, CC_MUTED, L"Площадь всех деталей", (RECT){bx, yy, bx + bw, yy + PS(28)},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE, TRUE);
    pn_text(dc, CF_BODY, CC_MUTED, r, (RECT){bx, yy, bx + bw, yy + PS(28)}, DT_RIGHT | DT_VCENTER | DT_SINGLELINE,
            TRUE);
    yy += PS(28);
    if (nbad) {
      _snwprintf(r, 64, L"Без нормы (не в итоге): %d", nbad);
      pn_text(dc, CF_SMALLB, CC_WARN, r, (RECT){bx, yy, bx + bw, yy + PS(28)}, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
              TRUE);
    }
  }
  y = box.bottom + PS(14);
  int bw1 = pn_button(dc, g_pnCopied ? L"Скопировано ✓" : L"Копировать для Excel", x, y, FALSE, 31, paint);
  pn_button(dc, L"Очистить список", x + bw1 + PS(10), y, FALSE, 32, paint);
  return y + PS(38);
}

/* карточка 3: нормы */
static int pn_norm_body(HDC dc, int x, int w, int y, BOOL paint) {
  y += pn_text(dc, CF_TITLE, CC_INK, L"Нормы расхода", (RECT){x, y, x + w, y + 60}, DT_LEFT | DT_SINGLELINE, paint) +
       PS(4);
  y += pn_text(dc, CF_SUB, CC_MUTED,
               L"Кг на 1 м² за один слой. Поменяете — всё выше пересчитается само. Исходные — из «Краска — расчёт.xlsx».",
               (RECT){x, y, x + w, y + 200}, DT_LEFT | DT_WORDBREAK, paint) + PS(12);
  int fw = PS(110), xw = PS(26);
  for (int j = 0; j < g_pnNMat; j++) {
    RECT fr = {x + w - xw - PS(8) - fw, y, x + w - xw - PS(8), y + PS(36)};
    pn_text(dc, CF_BODY, CC_INK, g_pnMat[j].name, (RECT){x, y, fr.left - PS(10), y + PS(36)},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, paint);
    if (paint) cc_round(dc, fr, PS(8), CC_SURF, GetFocus() == g_pnNorm[j] ? CC_PRIM : CC_BORDER);
    if (g_pnPlace && g_pnNorm[j])
      MoveWindow(g_pnNorm[j], fr.left + PS(8), fr.top + PS(6) - g_pnScroll, fw - PS(16), PS(24), TRUE);
    RECT del = {x + w - xw, y, x + w, y + PS(36)};
    if (paint) pn_text(dc, CF_BOLD, CC_FAINT, L"×", del, DT_CENTER | DT_VCENTER | DT_SINGLELINE, TRUE);
    pn_hot(del, 1000 + j);
    y += PS(36) + PS(6);
  }
  y += PS(8);
  /* новый материал */
  if (g_pnNMat < PN_MAXMAT) {
    y += pn_text(dc, CF_LABEL, CC_MUTED, L"Новый материал и его норма", (RECT){x, y, x + w, y + 40},
                 DT_LEFT | DT_SINGLELINE, paint) + PS(6);
    int bw = pn_text_w(dc, CF_BOLD, L"Добавить") + PS(32);
    RECT nr = {x, y, x + w - fw - bw - PS(20), y + PS(38)};
    RECT vr = {nr.right + PS(10), y, nr.right + PS(10) + fw, y + PS(38)};
    if (paint) {
      cc_round(dc, nr, PS(8), CC_SURF, GetFocus() == g_pnNewName ? CC_PRIM : CC_BORDER);
      cc_round(dc, vr, PS(8), CC_SURF, GetFocus() == g_pnNewNorm ? CC_PRIM : CC_BORDER);
    }
    if (g_pnPlace) {
      MoveWindow(g_pnNewName, nr.left + PS(8), nr.top + PS(7) - g_pnScroll, nr.right - nr.left - PS(16), PS(24), TRUE);
      MoveWindow(g_pnNewNorm, vr.left + PS(8), vr.top + PS(7) - g_pnScroll, fw - PS(16), PS(24), TRUE);
      ShowWindow(g_pnNewName, SW_SHOWNA);
      ShowWindow(g_pnNewNorm, SW_SHOWNA);
    }
    pn_button(dc, L"Добавить", vr.right + PS(10), y, FALSE, 40, paint);
    y += PS(38) + PS(14);
  } else if (g_pnPlace) {
    ShowWindow(g_pnNewName, SW_HIDE);
    ShowWindow(g_pnNewNorm, SW_HIDE);
  }
  const wchar_t *lnk = L"Вернуть исходные нормы";
  int lw = pn_text_w(dc, CF_SMALLB, lnk);
  RECT lr = {x, y, x + lw, y + PS(22)};
  pn_text(dc, CF_SMALLB, CC_PRIM, lnk, lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, paint);
  pn_hot(lr, 41);
  return y + PS(22);
}

/* карточка: сперва меряем, потом фон, потом содержимое */
typedef int (*PnBody)(HDC, int, int, int, BOOL);
static int pn_card(HDC dc, int x, int w, int y, PnBody body, BOOL paint) {
  int pad = PS(24);
  if (paint) {
    int save = g_pnNHot;
    BOOL place = g_pnPlace;
    g_pnPlace = FALSE;
    int h = body(dc, x + pad, w - pad * 2, y + pad, FALSE) + pad;
    g_pnPlace = place;
    g_pnNHot = save;
    cc_round(dc, (RECT){x, y, x + w, h}, PS(16), CC_SURF, CC_BORDER);
  }
  return body(dc, x + pad, w - pad * 2, y + pad, paint) + pad;
}

static int pn_draw(HDC dc, int cw, BOOL paint) {
  g_pnNHot = 0;
  int hh = PS(78);
  if (paint) {
    cc_fill(dc, (RECT){0, 0, cw, hh}, RGB(0xF6, 0xF3, 0xED));
    cc_fill(dc, (RECT){0, hh - 1, cw, hh}, CC_BORDER);
    RECT ic = {PS(24), PS(16), PS(24) + PS(44), PS(16) + PS(44)};
    cc_round(dc, ic, PS(12), CC_PRIM, CC_PRIM);
    /* значок: банка с краской и капля */
    RECT can = {ic.left + PS(12), ic.top + PS(16), ic.right - PS(12), ic.bottom - PS(9)};
    cc_round(dc, can, PS(3), RGB(0xFF, 0xFF, 0xFF), RGB(0xFF, 0xFF, 0xFF));
    cc_fill(dc, (RECT){can.left - PS(2), can.top - PS(4), can.right + PS(2), can.top - PS(1)}, RGB(0xFF, 0xFF, 0xFF));
    cc_fill(dc, (RECT){can.left + PS(3), can.top + PS(4), can.right - PS(3), can.top + PS(10)}, CC_PRIM);
    pn_text(dc, CF_H1, CC_INK, L"Расчёт краски", (RECT){ic.right + PS(14), PS(14), cw, PS(44)},
            DT_LEFT | DT_SINGLELINE, TRUE);
    pn_text(dc, CF_SUB, CC_MUTED, L"Эмаль, грунтовка и клей по площади детали",
            (RECT){ic.right + PS(14), PS(42), cw, PS(66)}, DT_LEFT | DT_SINGLELINE, TRUE);
  }
  int y = hh + PS(24), m = PS(24), gap = PS(20), avail = cw - m * 2;
  if (g_pnTwo) {
    int lw = PS(400), rx = m + lw + gap, rw = avail - lw - gap;
    int yl = pn_card(dc, m, lw, y, pn_left_body, paint);
    int yr = pn_card(dc, rx, rw, y, pn_cur_body, paint) + PS(18);
    yr = pn_card(dc, rx, rw, yr, pn_list_body, paint) + PS(18);
    yr = pn_card(dc, rx, rw, yr, pn_norm_body, paint);
    return (yl > yr ? yl : yr) + PS(24);
  }
  y = pn_card(dc, m, avail, y, pn_left_body, paint) + PS(18);
  y = pn_card(dc, m, avail, y, pn_cur_body, paint) + PS(18);
  y = pn_card(dc, m, avail, y, pn_list_body, paint) + PS(18);
  return pn_card(dc, m, avail, y, pn_norm_body, paint) + PS(24);
}

/* ---- раскладка, прокрутка (как у расчёта резки) ------------------------------------ */

static void pn_fit(int cw) {
  const float W2 = 1150.0f, W1 = 620.0f, MINF = 0.72f;
  float want = g_pnDpi * g_pnZoom, ns;
  if (cw >= W2 * want) {
    g_pnTwo = TRUE;
    ns = want;
  } else if (cw >= W2 * want * MINF) {
    g_pnTwo = TRUE;
    ns = cw / W2;
  } else {
    g_pnTwo = FALSE;
    ns = cw / W1;
    if (ns > want) ns = want;
    if (ns < want * MINF) ns = want * MINF;
  }
  if (ns - g_pnS > 0.004f || g_pnS - ns > 0.004f) {
    g_pnS = ns;
    pn_fonts();
    HWND all[] = {g_pnDesc, g_pnD1, g_pnD2, g_pnLen, g_pnQty, g_pnLay[0], g_pnLay[1], g_pnLay[2],
                  g_pnNewName, g_pnNewNorm, g_pnMatCb[0], g_pnMatCb[1], g_pnMatCb[2]};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
      if (all[i]) SendMessageW(all[i], WM_SETFONT, (WPARAM)g_pf[9], FALSE);
    for (int j = 0; j < g_pnNMat; j++)
      if (g_pnNorm[j]) SendMessageW(g_pnNorm[j], WM_SETFONT, (WPARAM)g_pf[9], FALSE);
    for (int k = 0; k < PN_NCOAT; k++)
      if (g_pnMatCb[k]) {
        SendMessageW(g_pnMatCb[k], CB_SETITEMHEIGHT, (WPARAM)-1, PS(32));
        SendMessageW(g_pnMatCb[k], CB_SETITEMHEIGHT, 0, PS(26));
      }
  }
}

static void pn_relayout(void) {
  if (!g_pnCanvas) return;
  RECT rc;
  GetClientRect(g_pnCanvas, &rc);
  pn_fit(rc.right);
  HDC dc = GetDC(g_pnCanvas);
  g_pnContentH = pn_draw(dc, rc.right, FALSE);
  ReleaseDC(g_pnCanvas, dc);
  int maxs = g_pnContentH - rc.bottom;
  if (maxs < 0) maxs = 0;
  if (g_pnScroll > maxs) g_pnScroll = maxs;
  if (g_pnScroll < 0) g_pnScroll = 0;
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMax = g_pnContentH - 1;
  si.nPage = (UINT)rc.bottom;
  si.nPos = g_pnScroll;
  SetScrollInfo(g_pnCanvas, SB_VERT, &si, TRUE);
  GetClientRect(g_pnCanvas, &rc); /* полоса прокрутки могла появиться или пропасть */
  dc = GetDC(g_pnCanvas);
  g_pnPlace = TRUE;
  g_pnContentH = pn_draw(dc, rc.right, FALSE);
  g_pnPlace = FALSE;
  ReleaseDC(g_pnCanvas, dc);
  RedrawWindow(g_pnCanvas, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void pn_scroll_to(int pos) {
  RECT rc;
  GetClientRect(g_pnCanvas, &rc);
  int maxs = g_pnContentH - rc.bottom;
  if (maxs < 0) maxs = 0;
  if (pos > maxs) pos = maxs;
  if (pos < 0) pos = 0;
  if (pos == g_pnScroll) return;
  g_pnScroll = pos;
  SetScrollPos(g_pnCanvas, SB_VERT, pos, TRUE);
  HDC dc = GetDC(g_pnCanvas);
  g_pnPlace = TRUE;
  pn_draw(dc, rc.right, FALSE);
  g_pnPlace = FALSE;
  ReleaseDC(g_pnCanvas, dc);
  RedrawWindow(g_pnCanvas, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static int pn_hit(int x, int y) {
  POINT p = {x, y + g_pnScroll};
  for (int i = g_pnNHot - 1; i >= 0; i--)
    if (PtInRect(&g_pnHot[i].r, p)) return g_pnHot[i].what;
  return 0;
}

/* ---- действия ---------------------------------------------------------------------- */

static void pn_changed(void) {
  pn_relayout();
  pn_save();
}

static void pn_add(void) {
  pn_from_fields();
  if (pn_area1(&g_pnCur) < 0) {
    MessageBeep(MB_ICONWARNING);
    SetFocus(g_pnCur.d1 > 0 ? g_pnLen : g_pnD1);
    return;
  }
  if (g_pnN >= PN_MAXPART) {
    MessageBoxW(g_pnWnd, L"В списке уже 300 деталей — скопируйте его и очистите.", L"Расчёт краски", MB_ICONWARNING);
    return;
  }
  g_pnList[g_pnN++] = g_pnCur;
  /* дальше обычно похожая деталь: профиль, размеры и покрытие остаются —
     поправить, что отличается; обозначение — заново */
  g_pnCur.desc[0] = 0;
  g_pnFilling = TRUE;
  SetWindowTextW(g_pnDesc, L"");
  g_pnFilling = FALSE;
  pn_changed();
  SetFocus(g_pnDesc);
  SendMessageW(g_pnD1, EM_SETSEL, 0, -1);
}

/* таблица теми же столбцами, что лист «Расчёт», и итог — как лист «Итого» */
static void pn_copy(void) {
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  sb_add(&b, L"№\tОбозначение / наименование\tПрофиль\tРазмер 1, мм\tРазмер 2, мм\tДлина, мм\tКол-во, шт\t"
             L"Торцы\tВнутри\tПлощадь 1 шт, м²\tПлощадь всего, м²\tЭмаль / лак\tСлоёв\tЭмаль, кг\t"
             L"Грунтовка\tСлоёв\tГрунтовка, кг\tКлей\tСлоёв\tКлей, кг\r\n");
  static const wchar_t *const prof[PN_NPROF] = {L"Круг", L"Лист", L"Труба", L"Квадратная труба"};
  int n = g_pnN ? g_pnN : 1;
  for (int i = 0; i < n; i++) {
    const PnPart *p = g_pnN ? &g_pnList[i] : &g_pnCur;
    wchar_t d[80], a[32], s2[32], l[32], a1[32], at[32];
    pn_clean(p->desc[0] ? p->desc : L"", d, 80);
    pn_num(p->d1, 6, a, 32);
    if (p->d2 > 0) pn_num(p->d2, 6, s2, 32);
    else s2[0] = 0;
    pn_num(p->len, 6, l, 32);
    double v1 = pn_area1(p), vt = pn_area_total(p);
    if (v1 >= 0) {
      pn_num(v1, 6, a1, 32);
      pn_num(vt, 6, at, 32);
    } else {
      a1[0] = at[0] = 0;
    }
    sb_add(&b, L"%d\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s", i + 1, d, prof[p->prof], a, s2, l,
           p->qty > 0 ? p->qty : 1, p->ends ? L"да" : L"нет", p->inside ? L"да" : L"нет", a1, at);
    for (int k = 0; k < PN_NCOAT; k++) {
      int st;
      double kg = pn_layer(p, k, &st);
      wchar_t v[32] = L"";
      if (st == 1) pn_num(kg, 6, v, 32);
      if (p->mat[k][0]) sb_add(&b, L"\t%s\t%d\t%s", p->mat[k], p->lay[k] > 0 ? p->lay[k] : 1, v);
      else sb_add(&b, L"\t\t\t");
    }
    sb_add(&b, L"\r\n");
  }
  if (g_pnN) {
    double kg[PN_MAXMAT], area, all = 0;
    pn_totals(kg, &area);
    sb_add(&b, L"\r\nИтого по материалам\r\nМатериал\tРасход, кг\r\n");
    for (int j = 0; j < g_pnNMat; j++)
      if (kg[j] > 0) {
        wchar_t v[32];
        pn_num(kg[j], 6, v, 32);
        sb_add(&b, L"%s\t%s\r\n", g_pnMat[j].name, v);
        all += kg[j];
      }
    wchar_t v[32];
    pn_num(all, 6, v, 32);
    sb_add(&b, L"Всего\t%s\r\n", v);
  }
  if (b.w && clipboard_set(b.w)) {
    g_pnCopied = TRUE;
    SetTimer(g_pnWnd, TIMER_PN_COPIED, 1500, NULL);
    pn_relayout();
  }
  free(b.w);
}

static int pn_mat_uses(const wchar_t *name) {
  int n = 0;
  for (int i = 0; i < g_pnN; i++)
    for (int k = 0; k < PN_NCOAT; k++)
      if (!wcscmp(g_pnList[i].mat[k], name)) n++;
  return n;
}

static void pn_mats_changed(void) {
  pn_make_norm_fields();
  g_pnFilling = TRUE;
  for (int k = 0; k < PN_NCOAT; k++) pn_fill_combo(k);
  g_pnFilling = FALSE;
  pn_changed();
}

static void pn_del_mat(int j) {
  if (j < 0 || j >= g_pnNMat) return;
  int uses = pn_mat_uses(g_pnMat[j].name);
  wchar_t q[300];
  if (uses)
    _snwprintf(q, 300, L"«%s» выбран в списке деталей (%d раз). Без нормы эти слои не посчитаются. Убрать всё равно?",
               g_pnMat[j].name, uses);
  else
    _snwprintf(q, 300, L"Убрать «%s» из норм?", g_pnMat[j].name);
  q[299] = 0;
  if (MessageBoxW(g_pnWnd, q, L"Расчёт краски", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  memmove(&g_pnMat[j], &g_pnMat[j + 1], sizeof(PnMat) * (size_t)(g_pnNMat - j - 1));
  g_pnNMat--;
  pn_mats_changed();
}

static void pn_add_mat(void) {
  wchar_t name[PN_NAME];
  GetWindowTextW(g_pnNewName, name, PN_NAME);
  pn_clean(name, name, PN_NAME);
  wchar_t *s = name;
  while (*s == L' ') s++;
  size_t l = wcslen(s);
  while (l > 0 && s[l - 1] == L' ') s[--l] = 0;
  double v = pn_parse(g_pnNewNorm);
  if (!s[0]) {
    SetFocus(g_pnNewName);
    return;
  }
  if (!(v > 0)) {
    SetFocus(g_pnNewNorm);
    MessageBeep(MB_ICONWARNING);
    return;
  }
  if (pn_find_mat(s) >= 0) {
    MessageBoxW(g_pnWnd, L"Такой материал уже есть — поменяйте его норму в списке выше.", L"Расчёт краски",
                MB_ICONINFORMATION);
    return;
  }
  if (g_pnNMat >= PN_MAXMAT) return;
  lstrcpynW(g_pnMat[g_pnNMat].name, s, PN_NAME);
  g_pnMat[g_pnNMat].norm = v;
  g_pnNMat++;
  SetWindowTextW(g_pnNewName, L"");
  SetWindowTextW(g_pnNewNorm, L"");
  pn_mats_changed();
}

static void pn_reset_norms(void) {
  if (MessageBoxW(g_pnWnd, L"Вернуть нормы как в «Краска — расчёт.xlsx»? Свои изменения норм пропадут.",
                  L"Расчёт краски", MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;
  pn_default_mats();
  pn_mats_changed();
}

/* ---- поля: Enter — добавить, Tab — дальше, колесо — листать ---------------------- */

static HWND pn_tab_next(HWND cur, BOOL back) {
  HWND order[16 + PN_MAXMAT];
  int n = 0;
  order[n++] = g_pnDesc;
  order[n++] = g_pnD1;
  if (g_pnCur.prof != PN_ROUND) order[n++] = g_pnD2;
  order[n++] = g_pnLen;
  order[n++] = g_pnQty;
  for (int k = 0; k < PN_NCOAT; k++) {
    order[n++] = g_pnMatCb[k];
    order[n++] = g_pnLay[k];
  }
  for (int j = 0; j < g_pnNMat; j++) order[n++] = g_pnNorm[j];
  if (g_pnNMat < PN_MAXMAT) {
    order[n++] = g_pnNewName;
    order[n++] = g_pnNewNorm;
  }
  for (int i = 0; i < n; i++)
    if (order[i] == cur) return order[(i + (back ? n - 1 : 1)) % n];
  return order[0];
}

/* поле ушло за край видимого — подвинуть полотно */
static void pn_scroll_into_view(HWND e) {
  RECT r, c;
  GetWindowRect(e, &r);
  MapWindowPoints(NULL, g_pnCanvas, (POINT *)&r, 2);
  GetClientRect(g_pnCanvas, &c);
  if (r.top < PS(10)) pn_scroll_to(g_pnScroll + r.top - PS(60));
  else if (r.bottom > c.bottom - PS(10)) pn_scroll_to(g_pnScroll + r.bottom - c.bottom + PS(60));
}

static LRESULT CALLBACK PnFieldProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  wchar_t cls[16] = L"";
  GetClassNameW(hwnd, cls, 16);
  BOOL combo = !_wcsicmp(cls, L"ComboBox");
  WNDPROC old = combo ? g_pnOldCombo : g_pnOldEdit;
  if (msg == WM_KEYDOWN && wParam == VK_TAB) {
    HWND nx = pn_tab_next(hwnd, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
    SetFocus(nx);
    pn_scroll_into_view(nx);
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_RETURN && !(combo && SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0))) {
    int id = GetDlgCtrlID(hwnd);
    if (id == ID_PN_NEWNAME || id == ID_PN_NEWNORM) pn_add_mat();
    else if (id < ID_PN_NORM0) PostMessageW(g_pnCanvas, WM_PN_ADD, 0, 0);
    return 0;
  }
  if (msg == WM_CHAR && (wParam == L'\r' || wParam == L'\t' || wParam == L'\n')) return 0; /* без писка */
  if (msg == WM_CHAR && wParam == 1 && !combo) {                                             /* Ctrl+A */
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return 0;
  }
  if (msg == WM_MOUSEWHEEL && !(combo && SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0))) {
    /* колесо над полем листает окно, а не меняет выбор в списке */
    SendMessageW(g_pnCanvas, msg, wParam, lParam);
    return 0;
  }
  return CallWindowProcW(old, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK PnCanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    cc_fill(mem, rc, CC_BG);
    SetBkMode(mem, TRANSPARENT);
    SetViewportOrgEx(mem, 0, -g_pnScroll, NULL);
    pn_draw(mem, rc.right, TRUE);
    SetViewportOrgEx(mem, 0, 0, NULL);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_SIZE:
    pn_relayout();
    return 0;
  case WM_VSCROLL: {
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, &si);
    int pos = g_pnScroll;
    switch (LOWORD(wParam)) {
    case SB_LINEUP: pos -= PS(40); break;
    case SB_LINEDOWN: pos += PS(40); break;
    case SB_PAGEUP: pos -= (int)si.nPage; break;
    case SB_PAGEDOWN: pos += (int)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = g_pnContentH; break;
    }
    pn_scroll_to(pos);
    return 0;
  }
  case WM_MOUSEWHEEL:
    if (GetKeyState(VK_CONTROL) & 0x8000) {
      g_pnZoom *= GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.1f : 1 / 1.1f;
      if (g_pnZoom < 0.6f) g_pnZoom = 0.6f;
      if (g_pnZoom > 1.6f) g_pnZoom = 1.6f;
      pn_relayout();
      pn_save();
      return 0;
    }
    pn_scroll_to(g_pnScroll - GET_WHEEL_DELTA_WPARAM(wParam) * PS(60) / WHEEL_DELTA);
    return 0;
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT) {
      POINT p;
      GetCursorPos(&p);
      ScreenToClient(hwnd, &p);
      if (pn_hit(p.x, p.y)) {
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
      }
    }
    break;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK: { /* два быстрых щелчка по галочке — два переключения, а не один */
    int w = pn_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    SetFocus(hwnd);
    if (w >= 1 && w <= PN_NPROF) {
      pn_from_fields();
      g_pnCur.prof = w - 1;
      if (g_pnCur.prof == PN_ROUND) SetWindowTextW(g_pnD2, L"");
      pn_changed();
      SetFocus(g_pnD1);
    } else if (w == 20 || w == 21) {
      pn_from_fields();
      if (w == 20) g_pnCur.ends = !g_pnCur.ends;
      else g_pnCur.inside = !g_pnCur.inside;
      pn_changed();
    } else if (w == 30) {
      pn_add();
    } else if (w == 31) {
      pn_copy();
    } else if (w == 32) {
      if (g_pnN && MessageBoxW(g_pnWnd, L"Очистить список деталей?", L"Расчёт краски", MB_YESNO | MB_ICONQUESTION) ==
                       IDYES) {
        g_pnN = 0;
        pn_changed();
      }
    } else if (w == 40) {
      pn_add_mat();
    } else if (w == 41) {
      pn_reset_norms();
    } else if (w >= 100 && w < 100 + PN_MAXPART) {
      int i = w - 100;
      if (i < g_pnN) {
        memmove(&g_pnList[i], &g_pnList[i + 1], sizeof(PnPart) * (size_t)(g_pnN - i - 1));
        g_pnN--;
        pn_changed();
      }
    } else if (w >= 1000 && w < 1000 + PN_MAXMAT) {
      pn_del_mat(w - 1000);
    }
    return 0;
  }
  case WM_PN_ADD:
    pn_add();
    return 0;
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORLISTBOX: {
    HDC dc = (HDC)wParam;
    SetTextColor(dc, CC_INK);
    SetBkColor(dc, CC_SURF);
    return (LRESULT)g_pnSurfBrush;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam), code = HIWORD(wParam);
    if (g_pnFilling) return 0;
    if (code == EN_SETFOCUS) {
      PostMessageW((HWND)lParam, EM_SETSEL, 0, -1); /* число целиком — новое набирается поверх */
      InvalidateRect(hwnd, NULL, FALSE);
    }
    if (code == EN_KILLFOCUS) InvalidateRect(hwnd, NULL, FALSE);
    if (code == EN_CHANGE && id >= ID_PN_NORM0 && id < ID_PN_NORM0 + PN_MAXMAT) {
      int j = id - ID_PN_NORM0;
      if (j < g_pnNMat) {
        g_pnMat[j].norm = pn_parse((HWND)lParam);
        pn_changed();
      }
    } else if (code == EN_CHANGE && id >= ID_PN_DESC && id <= ID_PN_LAY0 + PN_NCOAT - 1) {
      pn_from_fields();
      pn_changed();
    }
    if (code == CBN_SELCHANGE && id >= ID_PN_MAT0 && id < ID_PN_MAT0 + PN_NCOAT) {
      pn_from_fields();
      pn_changed();
    }
    return 0;
  }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK PnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = (int)(480 * g_pnDpi);
    mm->ptMinTrackSize.y = (int)(380 * g_pnDpi);
    return 0;
  }
  case WM_SIZE:
    if (g_pnCanvas) MoveWindow(g_pnCanvas, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
    return 0;
  case WM_MOUSEWHEEL:
    if (g_pnCanvas) SendMessageW(g_pnCanvas, msg, wParam, lParam);
    return 0;
  case WM_TIMER:
    if (wParam == TIMER_PN_COPIED) {
      KillTimer(hwnd, TIMER_PN_COPIED);
      g_pnCopied = FALSE;
      pn_relayout();
    }
    return 0;
  case WM_EXITSIZEMOVE:
    pn_save();
    return 0;
  case WM_CLOSE:
    pn_save();
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void paint_show(void) {
  if (!g_pnWnd) {
    if (!g_pnList) g_pnList = (PnPart *)calloc(PN_MAXPART, sizeof(PnPart));
    if (!g_pnList) return;
    HDC s = GetDC(NULL);
    g_pnDpi = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    g_pnS = g_pnDpi;
    if (s) ReleaseDC(NULL, s);
    RECT saved;
    BOOL havePlace = pn_load(&saved);
    pn_fonts();
    g_pnSurfBrush = CreateSolidBrush(CC_SURF);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PnProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CursorPadPaint";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = PnCanvasProc;
    wc.lpszClassName = L"CursorPadPaintCanvas";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int waw = wa.right - wa.left, wah = wa.bottom - wa.top;
    int ww = (int)(1060 * g_pnDpi), wh = (int)(820 * g_pnDpi);
    if (ww > waw * 3 / 4) ww = waw * 3 / 4;
    if (wh > wah * 7 / 8) wh = wah * 7 / 8;
    int wx = wa.left + (waw - ww) / 2, wy = wa.top + (wah - wh) / 2;
    if (havePlace) {
      wx = saved.left;
      wy = saved.top;
      ww = saved.right - saved.left;
      wh = saved.bottom - saved.top;
    }
    g_pnWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"CursorPadPaint", L"Расчёт краски — CursorPad",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wx, wy, ww, wh, NULL, NULL, g_inst, NULL);
    if (!g_pnWnd) return;
    RECT rc;
    GetClientRect(g_pnWnd, &rc);
    g_pnCanvas = CreateWindowExW(0, L"CursorPadPaintCanvas", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                                 0, 0, rc.right, rc.bottom, g_pnWnd, NULL, g_inst, NULL);
    g_pnDesc = pn_edit(L"", ID_PN_DESC);
    SendMessageW(g_pnDesc, EM_SETLIMITTEXT, 78, 0);
    g_pnD1 = pn_edit(L"", ID_PN_D1);
    g_pnD2 = pn_edit(L"", ID_PN_D2);
    g_pnLen = pn_edit(L"", ID_PN_LEN);
    g_pnQty = pn_edit(L"", ID_PN_QTY);
    for (int k = 0; k < PN_NCOAT; k++) {
      g_pnMatCb[k] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     0, 0, 100, 300, g_pnCanvas, (HMENU)(INT_PTR)(ID_PN_MAT0 + k), g_inst, NULL);
      SendMessageW(g_pnMatCb[k], WM_SETFONT, (WPARAM)g_pf[9], FALSE);
      SendMessageW(g_pnMatCb[k], CB_SETITEMHEIGHT, (WPARAM)-1, PS(32));
      SendMessageW(g_pnMatCb[k], CB_SETITEMHEIGHT, 0, PS(26));
      g_pnOldCombo = (WNDPROC)SetWindowLongPtrW(g_pnMatCb[k], GWLP_WNDPROC, (LONG_PTR)PnFieldProc);
      g_pnLay[k] = pn_edit(L"", ID_PN_LAY0 + k);
    }
    g_pnNewName = pn_edit(L"", ID_PN_NEWNAME);
    SendMessageW(g_pnNewName, EM_SETLIMITTEXT, PN_NAME - 2, 0);
    g_pnNewNorm = pn_edit(L"", ID_PN_NEWNORM);
    pn_make_norm_fields();
    pn_to_fields();
    pn_relayout();
  }
  ShowWindow(g_pnWnd, IsIconic(g_pnWnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g_pnWnd);
  SetFocus(g_pnD1);
}
