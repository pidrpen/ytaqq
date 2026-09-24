/* ---- Окно «Расчёт резки и газов» --------------------------------------------

   Своё окно CursorPad вместо страницы cutting-calculator.html: открывается
   сразу, без Edge. Расчёт — cutting_calc.c (сверен со страницей: текст
   «Копировать» и CSV совпадают символ в символ на 25 тысячах вариантов).

   Вид — как у страницы (с 2026.09.23.35): бежевый фон, карточки со
   скруглёнными углами, крупные цифры, цветные метки технологий, таблицы
   расхода. Всё рисуется само (GDI) на одном полотне с прокруткой; поля ввода
   и выбор материала — настоящие, поверх полотна. Широкое окно — две колонки
   (слева ввод и станок, справа итог), узкое — одна под другой.

   Окно поверх всех, со «свернуть/развернуть», растягивается. Крестик его
   прячет: при следующем открытии в нём то же, что вводили. */

#include <commdlg.h> /* окно «Сохранить как» для CSV */
#include "cutting_calc.c"

#define ID_CUT_MAT 700
#define ID_CUT_THICK 701
#define ID_CUT_LEN 702
#define TIMER_CUT_COPIED 1

/* цвета страницы */
#define CC_BG RGB(0xF4, 0xF1, 0xEA)
#define CC_SURF RGB(0xFF, 0xFC, 0xF7)
#define CC_PRIM RGB(0x1F, 0x6B, 0x5A)
#define CC_BORDER RGB(0xD4, 0xCD, 0xC0)
#define CC_MUTED RGB(0x5C, 0x56, 0x4C)
#define CC_FAINT RGB(0x8A, 0x83, 0x78)
#define CC_INK RGB(0x1C, 0x1A, 0x16)
#define CC_HEAD RGB(0xEF, 0xEB, 0xE3)   /* шапка таблиц */
#define CC_PILL RGB(0xEB, 0xE6, 0xDC)   /* быстрые длины */
#define CC_TIMEBG RGB(0xE4, 0xE8, 0xE0) /* карточка «Время резки» */
#define CC_WARN RGB(0x9A, 0x6B, 0x12)

static HWND g_cutWnd, g_cutCanvas, g_cutMat, g_cutThick, g_cutLen;
static CutPack g_cutPack;
static BOOL g_cutHave, g_cutCopied;
static wchar_t g_cutHint[400];
static const int kCutQuick[6] = {1000, 2500, 5000, 10000, 25000, 50000};
static int g_cutScroll, g_cutContentH;
static float g_cutS = 1.0f;    /* итоговый масштаб рисования: экран × Ctrl+колесо × подгонка под окно */
static float g_cutDpi = 1.0f;  /* масштаб экрана Windows (100 % — 1.0) */
static float g_cutZoom = 1.0f; /* свой, Ctrl+колесо */
static BOOL g_cutTwo = TRUE;   /* две колонки или одна под другой */
static HFONT g_cf[10]; /* шрифты окна, см. cut_fonts */
enum { CF_H1, CF_SUB, CF_TITLE, CF_LABEL, CF_BODY, CF_BIG, CF_BOLD, CF_MONO, CF_SMALLB, CF_INPUT };
static HBRUSH g_cutSurfBrush;

/* что где лежит на полотне — для щелчков (быстрые длины, «Копировать», «CSV») */
typedef struct {
  RECT r;
  int what; /* 1..6 — быстрые длины, 10 — копировать, 11 — CSV */
} CutHot;
static CutHot g_cutHot[16];
static int g_cutNHot;
static RECT g_cutThickBox, g_cutLenBox; /* рамки полей (в координатах полотна без прокрутки) */

static int CS(int v) { return (int)(v * g_cutS + 0.5f); }

static void cut_fonts(void) {
  for (int i = 0; i < 10; i++)
    if (g_cf[i]) DeleteObject(g_cf[i]);
  struct {
    int pt, w;
    const wchar_t *face;
  } f[10] = {{15, FW_BOLD, L"Segoe UI"},     {10, FW_NORMAL, L"Segoe UI"}, {13, FW_BOLD, L"Segoe UI"},
             {9, FW_BOLD, L"Segoe UI"},      {10, FW_NORMAL, L"Segoe UI"}, {17, FW_HEAVY, L"Segoe UI"},
             {10, FW_BOLD, L"Segoe UI"},     {8, FW_NORMAL, L"Consolas"},  {9, FW_SEMIBOLD, L"Segoe UI"},
             {13, FW_SEMIBOLD, L"Segoe UI"}};
  for (int i = 0; i < 10; i++)
    g_cf[i] = CreateFontW(-CS(f[i].pt * 96 / 72), 0, 0, 0, f[i].w, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, f[i].face);
}

/* как parseFloat(value.replace(',', '.')) || 0 */
static double cut_parse(HWND e) {
  wchar_t t[64];
  GetWindowTextW(e, t, 64);
  wchar_t *c = wcschr(t, L',');
  if (c) *c = L'.';
  wchar_t *end = NULL;
  double v = wcstod(t, &end);
  if (end == t || v != v) return 0;
  return v;
}

static const wchar_t *cut_cur_material(void) {
  int i = (int)SendMessageW(g_cutMat, CB_GETCURSEL, 0, 0);
  return (i >= 0 && i < CUT_NMATS) ? kCutMaterials[i] : NULL;
}

/* ---- рисование: простые кирпичики ------------------------------------------ */

static void cc_round(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
  RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(b);
  DeleteObject(p);
}

static void cc_fill(HDC dc, RECT r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

/* текст в прямоугольник; возвращает высоту (с переносом, если DT_WORDBREAK) */
static int cc_text(HDC dc, int font, COLORREF c, const wchar_t *s, RECT r, UINT fl, BOOL paint) {
  SelectObject(dc, g_cf[font]);
  RECT m = r;
  DrawTextW(dc, s, -1, &m, fl | DT_CALCRECT | DT_NOPREFIX);
  if (paint) {
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fl | DT_NOPREFIX);
  }
  return m.bottom - m.top;
}

static int cc_text_w(HDC dc, int font, const wchar_t *s) {
  SelectObject(dc, g_cf[font]);
  SIZE sz;
  GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
  return sz.cx;
}

/* метка технологии: цвета как у badge-laser / badge-plasma / … на странице */
static void cc_badge_colors(const wchar_t *kind, COLORREF *bg, COLORREF *fg, COLORREF *bd) {
  if (!wcscmp(kind, L"Лазер")) {
    *bg = RGB(0xE1, 0xEE, 0xE9);
    *fg = CC_PRIM;
    *bd = RGB(0xB9, 0xD4, 0xCB);
  } else if (!wcscmp(kind, L"Плазма")) {
    *bg = RGB(0xF8, 0xE6, 0xDB);
    *fg = RGB(0xC4, 0x5C, 0x26);
    *bd = RGB(0xEE, 0xC9, 0xB3);
  } else if (!wcscmp(kind, L"Газовая резка")) {
    *bg = RGB(0xF4, 0xEA, 0xD2);
    *fg = CC_WARN;
    *bd = RGB(0xE3, 0xD1, 0xA6);
  } else {
    *bg = CC_HEAD;
    *fg = CC_MUTED;
    *bd = CC_BORDER;
  }
}

static int cc_badge(HDC dc, const wchar_t *text, const wchar_t *kind, int x, int y, BOOL paint) {
  COLORREF bg, fg, bd;
  cc_badge_colors(kind, &bg, &fg, &bd);
  int w = cc_text_w(dc, CF_SMALLB, text) + CS(20), h = CS(24);
  if (paint) {
    RECT r = {x, y, x + w, y + h};
    cc_round(dc, r, h / 2, bg, bd);
    cc_text(dc, CF_SMALLB, fg, text, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, TRUE);
  }
  return w;
}

static void cc_hot(RECT r, int what) {
  if (g_cutNHot < 16) {
    g_cutHot[g_cutNHot].r = r;
    g_cutHot[g_cutNHot].what = what;
    g_cutNHot++;
  }
}

/* белая кнопка с рамкой (Копировать, CSV) */
static int cc_button(HDC dc, const wchar_t *t, int right, int y, int what, BOOL paint) {
  int w = cc_text_w(dc, CF_BOLD, t) + CS(32), h = CS(38);
  RECT r = {right - w, y, right, y + h};
  if (paint) {
    cc_round(dc, r, CS(10), CC_SURF, CC_BORDER);
    cc_text(dc, CF_BOLD, CC_INK, t, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, TRUE);
  }
  cc_hot(r, what);
  return w;
}

/* ---- колонка ввода ----------------------------------------------------------- */

typedef struct {
  int x, w, y;
  BOOL paint;
  HDC dc;
} CutCol;

static void cut_field(CutCol *c, const wchar_t *label, HWND ctl, RECT *box, int h) {
  c->y += cc_text(c->dc, CF_LABEL, CC_MUTED, label, (RECT){c->x, c->y, c->x + c->w, c->y + 100},
                  DT_LEFT | DT_SINGLELINE, c->paint) + CS(8);
  RECT r = {c->x, c->y, c->x + c->w, c->y + h};
  if (box) {
    *box = r;
    if (c->paint) {
      BOOL foc = GetFocus() == ctl;
      cc_round(c->dc, r, CS(10), CC_SURF, foc ? CC_PRIM : CC_BORDER);
      if (foc) { /* второй контур — как «кольцо» фокуса на странице */
        RECT r2 = r;
        InflateRect(&r2, 1, 1);
        HPEN p = CreatePen(PS_SOLID, 1, RGB(0xA9, 0xCB, 0xC1));
        HGDIOBJ op = SelectObject(c->dc, p), ob = SelectObject(c->dc, GetStockObject(NULL_BRUSH));
        RoundRect(c->dc, r2.left, r2.top, r2.right, r2.bottom, CS(22), CS(22));
        SelectObject(c->dc, op);
        SelectObject(c->dc, ob);
        DeleteObject(p);
      }
    }
  }
  c->y += h;
}

/* левая колонка: «Что режем?» и «Станок»; y — снизу колонки */
static int cut_left(HDC dc, int x0, int w, int y0, BOOL paint) {
  int pad = CS(24);
  CutCol c = {x0 + pad, w - pad * 2, y0 + pad, paint, dc};
  int top = y0;
  /* карточка ввода — сначала меряем, потом рисуем фон и содержимое */
  for (int pass = paint ? 0 : 1; pass < 2; pass++) {
    c.y = y0 + pad;
    c.paint = pass == 1 && paint;
    if (pass == 0) c.paint = FALSE;
    c.y += cc_text(dc, CF_TITLE, CC_INK, L"Что режем?", (RECT){c.x, c.y, c.x + c.w, c.y + 100},
                   DT_LEFT | DT_SINGLELINE, c.paint) + CS(6);
    c.y += cc_text(dc, CF_SUB, CC_MUTED,
                   L"Любая толщина — расход каждой технологии посчитаем по нормам, без шаблонов",
                   (RECT){c.x, c.y, c.x + c.w, c.y + 200}, DT_LEFT | DT_WORDBREAK, c.paint) + CS(18);
    /* материал: настоящий список поверх полотна */
    c.y += cc_text(dc, CF_LABEL, CC_MUTED, L"Материал", (RECT){c.x, c.y, c.x + c.w, c.y + 100},
                   DT_LEFT | DT_SINGLELINE, c.paint) + CS(8);
    /* поля двигаем только при раскладке (не во время рисования) — и с перерисовкой,
       иначе после прокрутки в них остаются куски старой картинки */
    if (pass == 1 && !paint)
      MoveWindow(g_cutMat, c.x, c.y - g_cutScroll, c.w, CS(300), TRUE);
    c.y += CS(40) + CS(18);
    cut_field(&c, L"Толщина, мм", g_cutThick, &g_cutThickBox, CS(48));
    if (pass == 1 && !paint)
      MoveWindow(g_cutThick, g_cutThickBox.left + CS(14), g_cutThickBox.top + CS(10) - g_cutScroll,
                 g_cutThickBox.right - g_cutThickBox.left - CS(28), CS(28), TRUE);
    c.y += CS(6);
    c.y += cc_text(dc, CF_SMALLB, CC_FAINT, g_cutHint, (RECT){c.x, c.y, c.x + c.w, c.y + 200},
                   DT_LEFT | DT_WORDBREAK, c.paint) + CS(18);
    cut_field(&c, L"Длина реза, мм", g_cutLen, &g_cutLenBox, CS(48));
    if (pass == 1 && !paint)
      MoveWindow(g_cutLen, g_cutLenBox.left + CS(14), g_cutLenBox.top + CS(10) - g_cutScroll,
                 g_cutLenBox.right - g_cutLenBox.left - CS(28), CS(28), TRUE);
    c.y += CS(12);
    /* быстрые длины — «пилюли», сколько влезет в ряд */
    int px = c.x, ph = CS(30), gap = CS(8);
    for (int i = 0; i < 6; i++) {
      wchar_t n[16], t[24];
      cut_fmt(kCutQuick[i], 0, n, 16);
      _snwprintf(t, 24, L"%s мм", n);
      int pw = cc_text_w(dc, CF_SMALLB, t) + CS(28);
      if (px + pw > c.x + c.w) {
        px = c.x;
        c.y += ph + gap;
      }
      RECT r = {px, c.y, px + pw, c.y + ph};
      if (c.paint) {
        cc_round(dc, r, ph / 2, CC_PILL, CC_BORDER);
        cc_text(dc, CF_SMALLB, CC_MUTED, t, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, TRUE);
      }
      if (pass == 1) cc_hot(r, 1 + i);
      px += pw + gap;
    }
    c.y += ph + pad;
    if (pass == 0 && paint) { /* фон карточки — под содержимым */
      RECT card = {x0, top, x0 + w, c.y};
      cc_round(dc, card, CS(16), CC_SURF, CC_BORDER);
    }
  }
  int y = c.y + CS(18);
  /* «Станок» — если есть что считать */
  if (g_cutHave) {
    const CutMethod *p = &g_cutPack.m[0];
    wchar_t name[300], badge[40], sp[40], tm[40];
    if (g_cutPack.n > 1) {
      name[0] = 0;
      for (int i = 0; i < g_cutPack.n; i++) {
        if (i) wcscat(name, L" · ");
        wcscat(name, g_cutPack.m[i].kind);
      }
      _snwprintf(badge, 40, L"%d способа", g_cutPack.n);
    } else {
      lstrcpynW(name, p->str[CS_EQUIP] && p->str[CS_EQUIP][0] ? p->str[CS_EQUIP] : L"Не указано", 300);
      lstrcpynW(badge, p->kind, 40);
    }
    if (!cut_na(p->num[CN_SPEED])) {
      cut_fmt(p->num[CN_SPEED], 2, sp, 40);
      wcscat(sp, L" м/мин");
    } else {
      lstrcpynW(sp, L"—", 40);
    }
    cut_fmt_time(cut_na(p->timeMin) ? 0 : p->timeMin, tm, 40);
    for (int pass = paint ? 0 : 1; pass < 2; pass++) {
      BOOL pt = pass == 1 && paint;
      int cy = y + pad, x = x0 + pad, iw = w - pad * 2;
      int bw = cc_badge(dc, badge, p->kind, 0, 0, FALSE);
      cc_badge(dc, badge, p->kind, x + iw - bw, cy - CS(2), pt);
      cy += cc_text(dc, CF_LABEL, CC_MUTED, L"Станок", (RECT){x, cy, x + iw - bw - CS(8), cy + 50},
                    DT_LEFT | DT_SINGLELINE, pt) + CS(8);
      cy += cc_text(dc, CF_TITLE, CC_INK, name, (RECT){x, cy, x + iw, cy + 200}, DT_LEFT | DT_WORDBREAK, pt) +
            CS(14);
      if (pt) cc_fill(dc, (RECT){x, cy, x + iw, cy + 1}, CC_BORDER);
      cy += CS(16);
      int half = iw / 2;
      cc_text(dc, CF_LABEL, CC_MUTED, L"Скорость", (RECT){x, cy, x + half, cy + 40}, DT_LEFT | DT_SINGLELINE, pt);
      cc_text(dc, CF_LABEL, CC_MUTED, L"Время", (RECT){x + half, cy, x + iw, cy + 40}, DT_LEFT | DT_SINGLELINE,
              pt);
      cy += CS(22);
      cc_text(dc, CF_TITLE, CC_INK, sp, (RECT){x, cy, x + half, cy + 40}, DT_LEFT | DT_SINGLELINE, pt);
      cy += cc_text(dc, CF_TITLE, CC_INK, tm, (RECT){x + half, cy, x + iw, cy + 40}, DT_LEFT | DT_SINGLELINE, pt);
      if (p->incomplete) {
        cy += CS(14);
        RECT wb = {x, cy, x + iw, cy + CS(56)};
        if (pt) {
          cc_round(dc, wb, CS(10), RGB(0xF3, 0xEA, 0xD6), RGB(0xE3, 0xD1, 0xA6));
          RECT t = wb;
          InflateRect(&t, -CS(12), -CS(8));
          cc_text(dc, CF_SMALLB, CC_WARN, L"Для этой позиции нормы в таблице не заполнены.", t,
                  DT_LEFT | DT_WORDBREAK | DT_VCENTER, TRUE);
        }
        cy += CS(56);
      }
      cy += pad;
      if (pass == 0 && paint) cc_round(dc, (RECT){x0, y, x0 + w, cy}, CS(16), RGB(0xF7, 0xF4, 0xEE), CC_BORDER);
      if (pass == 1) y = cy;
    }
  }
  return y;
}

/* ---- колонка итога ---------------------------------------------------------- */

static int cut_stat_card(HDC dc, RECT r, const wchar_t *cap, const wchar_t *big, const wchar_t *sub, BOOL hl,
                         COLORREF border, BOOL paint) {
  if (paint) {
    cc_round(dc, r, CS(16), hl ? CC_TIMEBG : CC_SURF, border);
    int x = r.left + CS(18), y = r.top + CS(18), rr = r.right - CS(12);
    y += cc_text(dc, CF_LABEL, CC_MUTED, cap, (RECT){x, y, rr, y + 40}, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS,
                 TRUE) + CS(8);
    y += cc_text(dc, CF_BIG, hl ? CC_PRIM : CC_INK, big, (RECT){x, y, rr, y + 60},
                 DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, TRUE) + CS(6);
    if (sub) cc_text(dc, CF_SMALLB, CC_MUTED, sub, (RECT){x, y, rr, y + 40}, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, TRUE);
  }
  return CS(124);
}

/* одна строка таблицы: позиция (+ деталь мелко), норма, расход */
static int cut_row(HDC dc, int x, int w, int y, const CutLine *l, BOOL paint) {
  int cw = CS(150), padx = CS(16);
  int lx = x + padx, lw = w - padx * 2 - cw * 2;
  int h = CS(18);
  int hl = cc_text(dc, CF_BOLD, CC_INK, l->label, (RECT){lx, 0, lx + lw, 200}, DT_LEFT | DT_WORDBREAK, FALSE);
  int hd = l->detail && l->detail[0]
               ? cc_text(dc, CF_MONO, CC_FAINT, l->detail, (RECT){lx, 0, lx + lw, 200}, DT_LEFT | DT_WORDBREAK, FALSE)
               : 0;
  int textH = hl + (hd ? CS(4) + hd : 0);
  int numH = l->group == 1 ? CS(48) : CS(22);
  int rowH = (textH > numH ? textH : numH) + h * 2;
  if (!paint) return rowH;
  int ty = y + (rowH - textH) / 2;
  cc_text(dc, CF_BOLD, CC_INK, l->label, (RECT){lx, ty, lx + lw, ty + hl}, DT_LEFT | DT_WORDBREAK, TRUE);
  if (hd)
    cc_text(dc, CF_MONO, CC_FAINT, l->detail, (RECT){lx, ty + hl + CS(4), lx + lw, ty + hl + CS(4) + hd},
            DT_LEFT | DT_WORDBREAK, TRUE);
  int c1 = x + w - padx - cw * 2, c2 = x + w - padx - cw;
  wchar_t a[64], b[64], t[80];
  if (l->group == 1) {
    int ny = y + (rowH - CS(48)) / 2;
    cut_fmt(l->ratePerMVol, 4, a, 64);
    _snwprintf(t, 80, L"%s м³", a);
    cc_text(dc, CF_BOLD, CC_MUTED, t, (RECT){c1, ny, c1 + cw, ny + CS(24)}, DT_RIGHT | DT_SINGLELINE, TRUE);
    cut_fmt(l->ratePerM, 4, a, 64);
    _snwprintf(t, 80, L"%s кг", a);
    cc_text(dc, CF_BOLD, CC_MUTED, t, (RECT){c1, ny + CS(24), c1 + cw, ny + CS(48)}, DT_RIGHT | DT_SINGLELINE, TRUE);
    cut_fmt(l->quantityVol, 4, b, 64);
    _snwprintf(t, 80, L"%s м³", b);
    cc_text(dc, CF_TITLE, CC_PRIM, t, (RECT){c2, ny, c2 + cw, ny + CS(24)}, DT_RIGHT | DT_SINGLELINE, TRUE);
    cut_fmt(l->quantity, 4, b, 64);
    _snwprintf(t, 80, L"%s кг", b);
    cc_text(dc, CF_BOLD, CC_INK, t, (RECT){c2, ny + CS(24), c2 + cw, ny + CS(48)}, DT_RIGHT | DT_SINGLELINE, TRUE);
  } else {
    int ny = y + (rowH - CS(22)) / 2;
    cut_fmt(l->ratePerM, 4, a, 64);
    _snwprintf(t, 80, L"%s %s", a, l->unit);
    cc_text(dc, CF_BODY, CC_MUTED, t, (RECT){c1, ny, c1 + cw, ny + CS(22)}, DT_RIGHT | DT_SINGLELINE, TRUE);
    cut_fmt(l->quantity, 4, b, 64);
    _snwprintf(t, 80, L"%s %s", b, l->unit);
    cc_text(dc, CF_BODY, CC_INK, t, (RECT){c2, ny, c2 + cw, ny + CS(22)}, DT_RIGHT | DT_SINGLELINE, TRUE);
  }
  return rowH;
}

/* таблица одной группы (оснастка / газы / прочее) */
static int cut_table(HDC dc, int x, int w, int y, const CutMethod *m, int g, BOOL paint) {
  int hh = CS(44), top = y, cw = CS(150), padx = CS(16);
  int total = hh;
  for (int k = 0; k < m->nlines; k++)
    if (m->lines[k].group == g) total += cut_row(dc, x, w, 0, &m->lines[k], FALSE) + 1;
  if (!paint) return total;
  RECT box = {x, top, x + w, top + total};
  cc_round(dc, box, CS(12), CC_SURF, CC_BORDER);
  /* шапка — тем же скруглением сверху: заливаем внутри рамки */
  HRGN clip = CreateRoundRectRgn(box.left + 1, box.top + 1, box.right, box.bottom, CS(24), CS(24));
  POINT org; /* обрезка — в координатах устройства, а полотно сдвинуто на прокрутку */
  GetViewportOrgEx(dc, &org);
  OffsetRgn(clip, org.x, org.y);
  SelectClipRgn(dc, clip);
  cc_fill(dc, (RECT){x, top, x + w, top + hh}, CC_HEAD);
  cc_fill(dc, (RECT){x, top + hh, x + w, top + hh + 1}, CC_BORDER);
  cc_text(dc, CF_LABEL, CC_MUTED, L"Позиция", (RECT){x + padx, top, x + w, top + hh}, DT_LEFT | DT_VCENTER | DT_SINGLELINE, TRUE);
  cc_text(dc, CF_LABEL, CC_MUTED, L"Норма/м", (RECT){x + w - padx - cw * 2, top, x + w - padx - cw, top + hh},
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE, TRUE);
  cc_text(dc, CF_LABEL, CC_MUTED, L"Расход", (RECT){x + w - padx - cw, top, x + w - padx, top + hh},
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE, TRUE);
  int ry = top + hh + 1, first = 1;
  for (int k = 0; k < m->nlines; k++) {
    if (m->lines[k].group != g) continue;
    if (!first) cc_fill(dc, (RECT){x, ry - 1, x + w, ry}, RGB(0xE6, 0xE0, 0xD5));
    first = 0;
    ry += cut_row(dc, x, w, ry, &m->lines[k], TRUE) + 1;
  }
  SelectClipRgn(dc, NULL);
  DeleteObject(clip);
  return total;
}

/* блок технологии: метка, станок, режим, скорость/время, таблицы */
static int cut_method(HDC dc, int x, int w, int y0, const CutMethod *m, BOOL paint) {
  int pad = CS(18), y = y0 + pad, ix = x + pad, iw = w - pad * 2;
  wchar_t ml[200], sp[40], tm[40];
  cut_mode_label(m, ml, 200);
  if (!cut_na(m->num[CN_SPEED])) {
    cut_fmt(m->num[CN_SPEED], 2, sp, 40);
    wcscat(sp, L" м/мин");
  } else {
    lstrcpynW(sp, L"—", 40);
  }
  cut_fmt_time(cut_na(m->timeMin) ? 0 : m->timeMin, tm, 40);
  for (int pass = paint ? 0 : 1; pass < 2; pass++) {
    BOOL pt = pass == 1 && paint;
    y = y0 + pad;
    /* справа — скорость и время, слева всё остальное */
    int rw = CS(210), lw = iw - rw;
    int bw = cc_badge(dc, m->kind, m->kind, ix, y, pt);
    const wchar_t *eq = m->str[CS_EQUIP] && m->str[CS_EQUIP][0] ? m->str[CS_EQUIP] : L"Не указано";
    int eh = cc_text(dc, CF_BOLD, CC_INK, eq, (RECT){ix + bw + CS(10), y + CS(2), ix + lw, y + 200},
                     DT_LEFT | DT_WORDBREAK, pt);
    int ly = y + (eh > CS(24) ? eh : CS(24)) + CS(8);
    ly += cc_text(dc, CF_SMALLB, CC_FAINT, ml, (RECT){ix, ly, ix + lw, ly + 200}, DT_LEFT | DT_WORDBREAK, pt);
    int rx = ix + lw + CS(10), half = (iw - lw - CS(10)) / 2;
    cc_text(dc, CF_LABEL, CC_MUTED, L"Скорость", (RECT){rx, y, rx + half, y + 30}, DT_LEFT | DT_SINGLELINE, pt);
    cc_text(dc, CF_LABEL, CC_MUTED, L"Время", (RECT){rx + half, y, rx + half * 2, y + 30}, DT_LEFT | DT_SINGLELINE, pt);
    cc_text(dc, CF_BOLD, CC_INK, sp, (RECT){rx, y + CS(22), rx + half, y + CS(48)}, DT_LEFT | DT_SINGLELINE, pt);
    cc_text(dc, CF_BOLD, CC_PRIM, tm, (RECT){rx + half, y + CS(22), rx + half * 2, y + CS(48)}, DT_LEFT | DT_SINGLELINE, pt);
    y = (ly > y + CS(48) ? ly : y + CS(48)) + CS(8);
    if (m->incomplete) {
      RECT wb = {ix, y, ix + iw, y + CS(40)};
      if (pt) {
        cc_round(dc, wb, CS(10), RGB(0xF3, 0xEA, 0xD6), RGB(0xE3, 0xD1, 0xA6));
        cc_text(dc, CF_SMALLB, CC_WARN, L"   Для этой позиции нормы в таблице не заполнены.", wb,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, TRUE);
      }
      y += CS(48);
    }
    if (!m->nlines) {
      y += CS(8) + cc_text(dc, CF_BODY, CC_MUTED, L"Нет заполненных норм расхода", (RECT){ix, y + CS(8), ix + iw, y + 60},
                           DT_LEFT | DT_SINGLELINE, pt);
    }
    static const wchar_t *const titles[3] = {L"Оснастка и расходники", L"Газы", L"Вспомогательные"};
    for (int g = 0; g < 3; g++) {
      int cnt = 0;
      double vol = 0, kg = 0;
      for (int k = 0; k < m->nlines; k++)
        if (m->lines[k].group == g) {
          cnt++;
          vol += m->lines[k].quantityVol;
          kg += m->lines[k].quantity;
        }
      if (!cnt) continue;
      wchar_t t[160];
      if (g == 1) {
        wchar_t a[48], b[48];
        cut_fmt(vol, 4, a, 48);
        cut_fmt(kg, 4, b, 48);
        _snwprintf(t, 160, L"Газы · итого %s м³ / %s кг", a, b);
      } else {
        lstrcpynW(t, titles[g], 160);
      }
      y += CS(14);
      y += cc_text(dc, CF_LABEL, CC_MUTED, t, (RECT){ix, y, ix + iw, y + 40}, DT_LEFT | DT_SINGLELINE, pt) + CS(10);
      y += cut_table(dc, ix, iw, y, m, g, pt);
    }
    y += pad;
    if (pass == 0 && paint) cc_round(dc, (RECT){x, y0, x + w, y}, CS(14), CC_SURF, CC_BORDER);
  }
  return y - y0;
}

static int cut_right(HDC dc, int x, int w, int y0, BOOL paint) {
  int y = y0, gap = CS(14);
  if (g_cutHave) {
    /* карточки-итоги */
    const CutPack *p = &g_cutPack;
    if (p->n == 1) {
      const CutMethod *m = &p->m[0];
      int cw = (w - gap * 2) / 3, tool = 0, gas = 0;
      for (int k = 0; k < m->nlines; k++) {
        if (m->lines[k].group == 0) tool++;
        if (m->lines[k].group == 1) gas++;
      }
      wchar_t sp[40], tm[40], cnt[16], sub[60];
      if (!cut_na(m->num[CN_SPEED])) {
        cut_fmt(m->num[CN_SPEED], 2, sp, 40);
        wcscat(sp, L" м/мин");
      } else {
        lstrcpynW(sp, L"—", 40);
      }
      cut_fmt_time(cut_na(m->timeMin) ? 0 : m->timeMin, tm, 40);
      _snwprintf(cnt, 16, L"%d", m->nlines);
      _snwprintf(sub, 60, L"оснастка %d · газы %d", tool, gas);
      cut_stat_card(dc, (RECT){x, y, x + cw, y + CS(124)}, L"Тип резки", m->kind, sp, FALSE, CC_BORDER, paint);
      cut_stat_card(dc, (RECT){x + cw + gap, y, x + cw * 2 + gap, y + CS(124)}, L"Время резки", tm, NULL, TRUE,
                    RGB(0xB9, 0xD4, 0xCB), paint);
      cut_stat_card(dc, (RECT){x + (cw + gap) * 2, y, x + w, y + CS(124)}, L"Позиций расхода", cnt, sub, FALSE,
                    CC_BORDER, paint);
    } else {
      int cw = (w - gap * (p->n - 1)) / p->n;
      for (int i = 0; i < p->n; i++) {
        const CutMethod *m = &p->m[i];
        wchar_t sp[40], tm[40];
        if (!cut_na(m->num[CN_SPEED])) {
          cut_fmt(m->num[CN_SPEED], 2, sp, 40);
          wcscat(sp, L" м/мин");
        } else {
          lstrcpynW(sp, L"нет скорости", 40);
        }
        cut_fmt_time(cut_na(m->timeMin) ? 0 : m->timeMin, tm, 40);
        COLORREF bg, fg, bd;
        cc_badge_colors(m->kind, &bg, &fg, &bd);
        int xx = x + (cw + gap) * i;
        cut_stat_card(dc, (RECT){xx, y, i == p->n - 1 ? x + w : xx + cw, y + CS(124)}, m->kind, tm, sp, FALSE, bd,
                      paint);
      }
    }
    y += CS(124) + CS(18);
  }
  /* большая карточка «Что понадобится» */
  int top = y, pad = CS(22);
  for (int pass = paint ? 0 : 1; pass < 2; pass++) {
    BOOL pt = pass == 1 && paint;
    y = top + pad;
    int ix = x + pad, iw = w - pad * 2;
    int bw = 0;
    if (g_cutHave) {
      bw = cc_button(dc, L"CSV", ix + iw, y, 11, pass == 1 && paint);
      bw += CS(10) + cc_button(dc, g_cutCopied ? L"Скопировано" : L"Копировать", ix + iw - bw - CS(10), y, 10,
                               pass == 1 && paint);
      if (pass == 0) g_cutNHot -= 2; /* кнопки учтём один раз — на втором проходе */
    }
    y += cc_text(dc, CF_TITLE, CC_INK, L"Что понадобится", (RECT){ix, y, ix + iw - bw - CS(10), y + 40},
                 DT_LEFT | DT_SINGLELINE, pt) + CS(6);
    wchar_t sub[200] = L"";
    if (g_cutHave) {
      wchar_t t[40], L[40];
      cut_fmt(g_cutPack.thickness, 2, t, 40);
      cut_fmt(g_cutPack.lengthMm, 0, L, 40);
      if (g_cutPack.n > 1)
        _snwprintf(sub, 200, L"%s · %s мм · %s мм реза · %d технологии", g_cutPack.material, t, L, g_cutPack.n);
      else
        _snwprintf(sub, 200, L"%s · %s мм · %s мм реза · расчёт по нормам", g_cutPack.material, t, L);
    }
    y += cc_text(dc, CF_SUB, CC_MUTED, sub[0] ? sub : L" ", (RECT){ix, y, ix + iw - bw - CS(10), y + 60},
                 DT_LEFT | DT_WORDBREAK, pt);
    y += CS(18);
    if (pt) cc_fill(dc, (RECT){x, y, x + w, y + 1}, CC_BORDER);
    y += CS(20);
    if (!g_cutHave) {
      y += CS(20) + cc_text(dc, CF_SUB, CC_MUTED,
                            L"Укажите толщину и длину реза — расход каждой технологии посчитается сам",
                            (RECT){ix, y + CS(20), ix + iw, y + 200}, DT_CENTER | DT_WORDBREAK, pt);
      y += CS(36);
    } else {
      for (int i = 0; i < g_cutPack.n; i++) y += cut_method(dc, ix, iw, y, &g_cutPack.m[i], pt) + CS(14);
    }
    y += pad - CS(14);
    if (pass == 0 && paint) cc_round(dc, (RECT){x, top, x + w, y}, CS(16), CC_SURF, CC_BORDER);
  }
  y += CS(16);
  cc_text(dc, CF_SMALLB, CC_FAINT, L"Нормы приложения 2 · интерполяция по толщине · газы в м³ и кг",
          (RECT){x, y, x + w, y + 30}, DT_CENTER | DT_SINGLELINE, paint);
  return y + CS(30);
}

/* всё полотно: шапка, колонки; возвращает высоту содержимого */
static int cut_draw(HDC dc, int cw, BOOL paint) {
  g_cutNHot = 0;
  int y = 0;
  /* шапка как у страницы: квадрат-значок, название, подзаголовок */
  int hh = CS(78);
  if (paint) {
    cc_fill(dc, (RECT){0, 0, cw, hh}, RGB(0xF6, 0xF3, 0xED));
    cc_fill(dc, (RECT){0, hh - 1, cw, hh}, CC_BORDER);
    RECT ic = {CS(24), CS(16), CS(24) + CS(44), CS(16) + CS(44)};
    cc_round(dc, ic, CS(12), CC_PRIM, CC_PRIM);
    /* калькулятор на значке: рамка и кнопки */
    RECT sc = {ic.left + CS(14), ic.top + CS(10), ic.right - CS(14), ic.bottom - CS(10)};
    cc_round(dc, sc, CS(3), CC_PRIM, RGB(0xFF, 0xFF, 0xFF));
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 2; j++)
        cc_fill(dc, (RECT){sc.left + CS(3) + j * CS(6), sc.top + CS(8) + i * CS(5), sc.left + CS(6) + j * CS(6),
                           sc.top + CS(10) + i * CS(5)},
                RGB(0xFF, 0xFF, 0xFF));
    cc_fill(dc, (RECT){sc.left + CS(3), sc.top + CS(3), sc.right - CS(3), sc.top + CS(5)}, RGB(0xFF, 0xFF, 0xFF));
    cc_text(dc, CF_H1, CC_INK, L"Расчётник резки", (RECT){ic.right + CS(14), CS(14), cw, CS(44)},
            DT_LEFT | DT_SINGLELINE, TRUE);
    cc_text(dc, CF_SUB, CC_MUTED, L"Сколько уйдёт расходников на ваш рез", (RECT){ic.right + CS(14), CS(42), cw, CS(66)},
            DT_LEFT | DT_SINGLELINE, TRUE);
  }
  y = hh + CS(24);
  int m = CS(24), gap = CS(20);
  int avail = cw - m * 2;
  if (g_cutTwo) { /* две колонки */
    int lw = CS(380);
    int yl = cut_left(dc, m, lw, y, paint);
    int yr = cut_right(dc, m + lw + gap, avail - lw - gap, y, paint);
    return (yl > yr ? yl : yr) + CS(24);
  }
  int yl = cut_left(dc, m, avail, y, paint);
  return cut_right(dc, m, avail, yl, paint) + CS(24);
}

/* ---- полотно: рисование с прокруткой, щелчки ---------------------------------- */

/* Подгонка под ширину окна. Всё окно нарисовано в «макетных» пикселях:
   две колонки — 1150 в ширину, одна — 620. Окно уже макета — всё
   уменьшается целиком, в тех же пропорциях (шрифты вместе с рамками и
   отступами), но не мельче 72 %; дальше двум колонкам тесно — встают одна
   под другой. Так размер букв и раскладка не расходятся ни при каком окне. */
static void cut_fit(int cw) {
  const float W2 = 1150.0f, W1 = 620.0f, MINF = 0.72f;
  float want = g_cutDpi * g_cutZoom, ns; /* сколько хочется: экран × Ctrl+колесо */
  if (cw >= W2 * want) { /* две колонки влезают как есть */
    g_cutTwo = TRUE;
    ns = want;
  } else if (cw >= W2 * want * MINF) { /* влезают, если чуть уменьшить */
    g_cutTwo = TRUE;
    ns = cw / W2;
  } else { /* одна колонка; не влезает — тоже уменьшаем, но не мельче 72 % */
    g_cutTwo = FALSE;
    ns = cw / W1;
    if (ns > want) ns = want;
    if (ns < want * MINF) ns = want * MINF;
  }
  if (ns - g_cutS > 0.004f || g_cutS - ns > 0.004f) {
    g_cutS = ns;
    cut_fonts();
    if (g_cutMat) {
      SendMessageW(g_cutMat, WM_SETFONT, (WPARAM)g_cf[CF_INPUT], FALSE);
      SendMessageW(g_cutThick, WM_SETFONT, (WPARAM)g_cf[CF_INPUT], FALSE);
      SendMessageW(g_cutLen, WM_SETFONT, (WPARAM)g_cf[CF_INPUT], FALSE);
      SendMessageW(g_cutMat, CB_SETITEMHEIGHT, (WPARAM)-1, CS(32));
      SendMessageW(g_cutMat, CB_SETITEMHEIGHT, 0, CS(26));
    }
  }
}

static void cut_save_pref(void);
static void cut_relayout(void) {
  if (!g_cutCanvas) return;
  RECT rc;
  GetClientRect(g_cutCanvas, &rc);
  cut_fit(rc.right);
  HDC dc = GetDC(g_cutCanvas);
  g_cutContentH = cut_draw(dc, rc.right, FALSE);
  /* поля ввода расставляет второй проход cut_left — он идёт и при измерении */
  ReleaseDC(g_cutCanvas, dc);
  int maxs = g_cutContentH - rc.bottom;
  if (maxs < 0) maxs = 0;
  if (g_cutScroll > maxs) g_cutScroll = maxs;
  if (g_cutScroll < 0) g_cutScroll = 0;
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin = 0;
  si.nMax = g_cutContentH - 1;
  si.nPage = (UINT)rc.bottom;
  si.nPos = g_cutScroll;
  SetScrollInfo(g_cutCanvas, SB_VERT, &si, TRUE);
  /* ещё раз — уже с поправленной прокруткой, чтобы поля встали на место */
  dc = GetDC(g_cutCanvas);
  cut_draw(dc, rc.right, FALSE);
  ReleaseDC(g_cutCanvas, dc);
  RedrawWindow(g_cutCanvas, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void cut_recalc(void) {
  const wchar_t *mat = cut_cur_material();
  cut_hint(mat ? mat : L"", g_cutHint, 400);
  g_cutHave = cut_calc(mat, cut_parse(g_cutThick), cut_parse(g_cutLen), &g_cutPack);
  cut_relayout();
}

static void cut_to_clipboard(void) {
  if (!g_cutHave) return;
  wchar_t *t = (wchar_t *)malloc(40000 * sizeof(wchar_t));
  if (!t) return;
  cut_copy_text(&g_cutPack, t, 20000);
  /* в Windows строки делит \r\n — иначе Блокнот склеит всё в одну */
  size_t n = wcslen(t), extra = 0;
  for (size_t i = 0; i < n; i++)
    if (t[i] == L'\n') extra++;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (n + extra + 1) * sizeof(wchar_t));
  if (h) {
    wchar_t *d = (wchar_t *)GlobalLock(h);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
      if (t[i] == L'\n') d[k++] = L'\r';
      d[k++] = t[i];
    }
    d[k] = 0;
    GlobalUnlock(h);
    if (OpenClipboard(g_cutWnd)) {
      EmptyClipboard();
      if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
      CloseClipboard();
      g_cutCopied = TRUE;
      SetTimer(g_cutWnd, TIMER_CUT_COPIED, 1500, NULL);
      cut_relayout();
    } else {
      GlobalFree(h);
    }
  }
  free(t);
}

static void cut_save_csv(void) {
  if (!g_cutHave) return;
  wchar_t file[MAX_PATH], t[40], L[40];
  cut_js_num(g_cutPack.thickness, t, 40);
  cut_js_num(g_cutPack.lengthMm, L, 40);
  _snwprintf(file, MAX_PATH, L"raschet-rezki-%s-%smm-%smm.csv", g_cutPack.material, t, L);
  file[MAX_PATH - 1] = 0;
  for (wchar_t *p = file; *p; p++) /* «АДО, АД1М» и прочее — без знаков, запрещённых в имени */
    if (wcschr(L"\\/:*?\"<>|", *p)) *p = L'_';
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = g_cutWnd;
  of.lpstrFilter = L"CSV (*.csv)\0*.csv\0Все файлы\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"csv";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of)) return;
  wchar_t *w = (wchar_t *)malloc(60000 * sizeof(wchar_t));
  if (!w) return;
  cut_csv_text(&g_cutPack, w, 60000);
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *u = n > 0 ? (char *)malloc((size_t)n + 3) : NULL;
  if (u) {
    u[0] = (char)0xEF; /* BOM, как у страницы: Excel тогда видит UTF-8 */
    u[1] = (char)0xBB;
    u[2] = (char)0xBF;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u + 3, n, NULL, NULL);
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wr = 0;
    BOOL ok = f != INVALID_HANDLE_VALUE && WriteFile(f, u, (DWORD)(n + 2), &wr, NULL);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) MessageBoxW(g_cutWnd, L"Не удалось записать файл.", L"Расчёт резки", MB_ICONWARNING);
    free(u);
  }
  free(w);
}

static void cut_scroll_to(int pos) {
  RECT rc;
  GetClientRect(g_cutCanvas, &rc);
  int maxs = g_cutContentH - rc.bottom;
  if (maxs < 0) maxs = 0;
  if (pos > maxs) pos = maxs;
  if (pos < 0) pos = 0;
  if (pos == g_cutScroll) return;
  g_cutScroll = pos;
  SetScrollPos(g_cutCanvas, SB_VERT, pos, TRUE);
  HDC dc = GetDC(g_cutCanvas);
  cut_draw(dc, rc.right, FALSE); /* поля переезжают вместе с полотном */
  ReleaseDC(g_cutCanvas, dc);
  /* и полотно, и поля на нём — целиком: иначе в полях остаются следы прокрутки */
  RedrawWindow(g_cutCanvas, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static int cut_hit(int x, int y) {
  y += g_cutScroll;
  for (int i = 0; i < g_cutNHot; i++) {
    POINT p = {x, y};
    if (PtInRect(&g_cutHot[i].r, p)) return g_cutHot[i].what;
  }
  return 0;
}

static LRESULT CALLBACK CutCanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    /* в памяти, потом одним махом — без мигания; полотно сдвинуто на прокрутку */
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    cc_fill(mem, rc, CC_BG);
    SetBkMode(mem, TRANSPARENT);
    SetViewportOrgEx(mem, 0, -g_cutScroll, NULL);
    cut_draw(mem, rc.right, TRUE);
    SetViewportOrgEx(mem, 0, 0, NULL);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_SIZE:
    cut_relayout();
    return 0;
  case WM_VSCROLL: {
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, &si);
    int pos = g_cutScroll;
    switch (LOWORD(wParam)) {
    case SB_LINEUP: pos -= CS(40); break;
    case SB_LINEDOWN: pos += CS(40); break;
    case SB_PAGEUP: pos -= (int)si.nPage; break;
    case SB_PAGEDOWN: pos += (int)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = g_cutContentH; break;
    }
    cut_scroll_to(pos);
    return 0;
  }
  case WM_MOUSEWHEEL:
    if (GetKeyState(VK_CONTROL) & 0x8000) { /* Ctrl+колесо — свой масштаб, как в браузере */
      g_cutZoom *= GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.1f : 1 / 1.1f;
      if (g_cutZoom < 0.6f) g_cutZoom = 0.6f;
      if (g_cutZoom > 1.6f) g_cutZoom = 1.6f;
      cut_relayout();
      cut_save_pref();
      return 0;
    }
    cut_scroll_to(g_cutScroll - GET_WHEEL_DELTA_WPARAM(wParam) * CS(60) / WHEEL_DELTA);
    return 0;
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT) {
      POINT p;
      GetCursorPos(&p);
      ScreenToClient(hwnd, &p);
      if (cut_hit(p.x, p.y)) {
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
      }
    }
    break;
  case WM_LBUTTONDOWN: {
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    int w = cut_hit(x, y);
    if (w >= 1 && w <= 6) {
      wchar_t v[16];
      _snwprintf(v, 16, L"%d", kCutQuick[w - 1]);
      SetWindowTextW(g_cutLen, v); /* EN_CHANGE пересчитает */
    } else if (w == 10) {
      cut_to_clipboard();
    } else if (w == 11) {
      cut_save_csv();
    } else {
      /* щелчок в рамке поля, мимо самого поля, — тоже в поле */
      POINT p = {x, y + g_cutScroll};
      if (PtInRect(&g_cutThickBox, p)) SetFocus(g_cutThick);
      else if (PtInRect(&g_cutLenBox, p)) SetFocus(g_cutLen);
      else SetFocus(hwnd);
    }
    return 0;
  }
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORLISTBOX: {
    HDC dc = (HDC)wParam;
    SetTextColor(dc, CC_INK);
    SetBkColor(dc, CC_SURF);
    return (LRESULT)g_cutSurfBrush;
  }
  case WM_COMMAND: {
    int id = LOWORD(wParam), code = HIWORD(wParam);
    if ((id == ID_CUT_THICK || id == ID_CUT_LEN) && code == EN_CHANGE) cut_recalc();
    /* зашли в поле — число выделено целиком: новое набирается поверх, а не дописывается */
    if ((id == ID_CUT_THICK || id == ID_CUT_LEN) && code == EN_SETFOCUS) {
      PostMessageW((HWND)lParam, EM_SETSEL, 0, -1);
      InvalidateRect(hwnd, NULL, FALSE); /* рамка поля — цветом «в фокусе» */
    }
    if ((id == ID_CUT_THICK || id == ID_CUT_LEN) && code == EN_KILLFOCUS) InvalidateRect(hwnd, NULL, FALSE);
    if (id == ID_CUT_MAT && code == CBN_SELCHANGE) cut_recalc();
    return 0;
  }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* масштаб (Ctrl+колесо) и место окна — между запусками, в cutting.txt */
static void cut_pref_path(wchar_t *p) {
  _snwprintf(p, MAX_PATH, L"%s\\cutting.txt", g_dataDir);
  p[MAX_PATH - 1] = 0;
}

static void cut_save_pref(void) {
  if (!g_dataDir[0] || !g_cutWnd) return;
  WINDOWPLACEMENT wp;
  wp.length = sizeof(wp);
  if (!GetWindowPlacement(g_cutWnd, &wp)) return;
  RECT r = wp.rcNormalPosition;
  char b[120];
  int n = snprintf(b, sizeof(b), "%d %ld %ld %ld %ld\n", (int)(g_cutZoom * 100 + 0.5f), r.left, r.top,
                   r.right - r.left, r.bottom - r.top);
  wchar_t p[MAX_PATH];
  cut_pref_path(p);
  HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(f, b, (DWORD)n, &w, NULL);
  CloseHandle(f);
}

/* FALSE — сохранённого нет или оно не на этом экране */
static BOOL cut_load_pref(RECT *out) {
  if (!g_dataDir[0]) return FALSE;
  wchar_t p[MAX_PATH];
  cut_pref_path(p);
  char *b = NULL;
  DWORD n = 0;
  if (!read_file_bytes(p, &b, &n, 200)) return FALSE;
  int z = 100;
  long x = 0, y = 0, w = 0, h = 0;
  int k = sscanf(b, "%d %ld %ld %ld %ld", &z, &x, &y, &w, &h);
  free(b);
  if (k >= 1 && z >= 60 && z <= 160) g_cutZoom = z / 100.0f;
  if (k < 5 || w < 300 || h < 250) return FALSE;
  RECT r = {x, y, x + w, y + h};
  /* окно должно хоть частью попасть на какой-нибудь монитор */
  if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) return FALSE;
  *out = r;
  return TRUE;
}

static LRESULT CALLBACK CutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = (int)(480 * g_cutDpi);
    mm->ptMinTrackSize.y = (int)(380 * g_cutDpi);
    return 0;
  }
  case WM_SIZE:
    if (g_cutCanvas) MoveWindow(g_cutCanvas, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
    return 0;
  case WM_MOUSEWHEEL: /* колесо над полями — тоже листает */
    if (g_cutCanvas) SendMessageW(g_cutCanvas, msg, wParam, lParam);
    return 0;
  case WM_TIMER:
    if (wParam == TIMER_CUT_COPIED) {
      KillTimer(hwnd, TIMER_CUT_COPIED);
      g_cutCopied = FALSE;
      cut_relayout();
    }
    return 0;
  case WM_EXITSIZEMOVE:
    cut_save_pref();
    return 0;
  case WM_CLOSE:
    cut_save_pref();
    ShowWindow(hwnd, SW_HIDE); /* прячем: в следующий раз — с тем же, что вводили */
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void cutting_show(void) {
  if (!g_cutWnd) {
    HDC s = GetDC(NULL);
    g_cutDpi = s ? GetDeviceCaps(s, LOGPIXELSX) / 96.0f : 1.0f;
    g_cutS = g_cutDpi;
    if (s) ReleaseDC(NULL, s);
    cut_fonts();
    g_cutSurfBrush = CreateSolidBrush(CC_SURF);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CutProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CursorPadCutting";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = CutCanvasProc;
    wc.lpszClassName = L"CursorPadCuttingCanvas";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    /* не во весь экран: около трёх четвертей ширины и чуть меньше высоты;
       содержимое само подгонится под то, что вышло (cut_fit) */
    int waw = wa.right - wa.left, wah = wa.bottom - wa.top;
    int ww = (int)(1060 * g_cutDpi), wh = (int)(800 * g_cutDpi);
    if (ww > waw * 3 / 4) ww = waw * 3 / 4;
    if (wh > wah * 7 / 8) wh = wah * 7 / 8;
    int wx = wa.left + (waw - ww) / 2, wy = wa.top + (wah - wh) / 2;
    RECT saved;
    if (cut_load_pref(&saved)) { /* как оставили в прошлый раз */
      wx = saved.left;
      wy = saved.top;
      ww = saved.right - saved.left;
      wh = saved.bottom - saved.top;
    }
    /* поверх всех окон, со «свернуть» и «развернуть» */
    g_cutWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"CursorPadCutting", L"Расчёт резки и газов — CursorPad",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wx, wy, ww, wh, NULL, NULL, g_inst, NULL);
    if (!g_cutWnd) return;
    RECT rc;
    GetClientRect(g_cutWnd, &rc);
    g_cutCanvas = CreateWindowExW(0, L"CursorPadCuttingCanvas", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                                  0, 0, rc.right, rc.bottom, g_cutWnd, NULL, g_inst, NULL);
    g_cutMat = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                               0, 0, 100, 300, g_cutCanvas, (HMENU)(INT_PTR)ID_CUT_MAT, g_inst, NULL);
    g_cutThick = CreateWindowExW(0, L"EDIT", L"10", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 26,
                                 g_cutCanvas, (HMENU)(INT_PTR)ID_CUT_THICK, g_inst, NULL);
    g_cutLen = CreateWindowExW(0, L"EDIT", L"10000", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 26,
                               g_cutCanvas, (HMENU)(INT_PTR)ID_CUT_LEN, g_inst, NULL);
    SendMessageW(g_cutMat, WM_SETFONT, (WPARAM)g_cf[CF_INPUT], TRUE);
    SendMessageW(g_cutThick, WM_SETFONT, (WPARAM)g_cf[CF_INPUT], TRUE);
    SendMessageW(g_cutLen, WM_SETFONT, (WPARAM)g_cf[CF_INPUT], TRUE);
    for (int i = 0; i < CUT_NMATS; i++) SendMessageW(g_cutMat, CB_ADDSTRING, 0, (LPARAM)kCutMaterials[i]);
    int def = 0;
    for (int i = 0; i < CUT_NMATS; i++)
      if (!wcscmp(kCutMaterials[i], L"ст3")) def = i;
    SendMessageW(g_cutMat, CB_SETCURSEL, (WPARAM)def, 0);
    SendMessageW(g_cutMat, CB_SETITEMHEIGHT, (WPARAM)-1, CS(32)); /* сам список — высотой как поля */
    cut_recalc();
  }
  ShowWindow(g_cutWnd, IsIconic(g_cutWnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(g_cutWnd);
  SetFocus(g_cutThick);
}
