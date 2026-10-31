/* ai_minichat.c — мини-чат у помощника (с 2026.10.21.3). Подключается из
   ai_chat.c после окна чата: беседа та же (g_acTurns, ac_send), ответы приходят
   в окно чата, даже скрытое, — полный чат (⤢) покажет её целиком.

   Пока помощник на столе, «Спросить ИИ» открывает маленькое тёмное окно у его
   головы: реплики пузырями — целиком, без обрезки; окно растёт с беседой до
   ~60 % высоты экрана, дальше лента листается колесом, полосой справа и
   PageUp/PageDown, новый длинный ответ встаёт началом к верху ленты. Правая
   кнопка по пузырю — «Копировать». Поле ввода, скрепка (картинка из файла,
   снимок области экрана, из буфера) и «отправить» — пока ИИ думает, на её месте
   «Стоп». Картинку можно и вставить Ctrl+V, и перетащить в окно; другой файл —
   в поле ляжет его путь. ⟳ — новая беседа: ИИ забывает прежние реплики, они
   остаются в полном чате, а пока новый вопрос не задан — «Вернуть». План
   файловой задачи — «Применить» / «Отмена» прямо здесь. × или Esc — закрыть.
   Окно ездит за роботом; за шапку его тянут — робот идёт следом.
   С 2026.10.26 окно в цветах и шрифтах темы, как полный чат (раньше — всегда
   тёмно-синее, при «Бумаге» и «Рыцарской» чужое); ответы с разметкой: жирное,
   заголовки, списки, строки маршрута и норм; ширину тянут за левый или правый
   край (запоминается в ai_avatar.ini), состояние в шапке целиком — в подсказке. */

#define WM_AM_SEND (WM_APP + 76) /* Enter в поле ввода */
#define WM_AM_SYNC (WM_APP + 79) /* вопрос не дошёл — вернуть его в поле мини-чата */
#define ID_AM_IN 4801
#define AM_TIMER 0xA181          /* точки «думает…» */
#define AM_TURNS 40              /* реплик в ленте; раньше — в полном чате */
enum { AM_NONE, AM_NEW, AM_FULL, AM_CLOSE, AM_ATTACH, AM_SEND, AM_IMGDEL, AM_UNDO, AM_APPLY, AM_CANCEL, AM_BTNS };
/* у ответа до четырёх строк: заголовок и текст размышления, заголовок и текст шагов */
#define AM_ITEMS (AM_TURNS * 5 + 8)
enum { AM_USER, AM_AI, AM_DOTS, AM_HINT, AM_MORE, AM_THINK_HEAD, AM_STEPS_HEAD, AM_NOTE_BODY };
enum { AM_MENU_FILE = 1, AM_MENU_SHOT, AM_MENU_PASTE, AM_MENU_DROP, AM_MENU_COPY };

/* Цвета — из темы (cursorpad.c, ui_theme.c), те же, что у полного чата: окно и ответ —
   бумага, вопрос — бумага потемнее, акцент темы — у «отправить», черты ответа и ссылок */
static COLORREF am_bg(void) { return COL_PAPER; }
static COLORREF am_bg2(void) { return COL_PAPER_DARK; }
static COLORREF am_ink(void) { return COL_INK; }
static COLORREF am_muted(void) { return COL_MUTED; }
static COLORREF am_accent(void) { return COL_SAGE; }
static COLORREF am_line(void) { return ui_hairline(); }
static COLORREF am_hover(void) { return blend_rgb(COL_PAPER, COL_INK, 20); }
/* текст и значок на акценте: тёмный на светлом акценте, иначе белый */
static COLORREF am_on_accent(void) {
  COLORREF a = COL_SAGE;
  return (GetRValue(a) * 299 + GetGValue(a) * 587 + GetBValue(a) * 114) / 1000 > 150 ? COL_INK : RGB(255, 255, 255);
}
#define AM_RED RGB(196, 43, 28)
#define AM_W_MIN 340 /* ширина окна, единицы 96 dpi */
#define AM_W_MAX 760
#define AM_W_DEFAULT 460
#define AM_EDGE 6    /* за столько точек от края окно тянут в ширину */

typedef struct {
  int kind;
  int turn;            /* реплика в g_acTurns; -1 — подсказка или точки */
  RECT r;              /* в координатах ленты: от её верха */
  const wchar_t *text; /* в g_amText */
} AmItem;

static HWND g_amWnd, g_amIn, g_amTip;
static WNDPROC g_amInOld;
static HFONT g_amFont, g_amBold, g_amSmall;
static HBRUSH g_amBgBr, g_amFieldBr;
static int g_amK = 100, g_amScroll, g_amHot, g_amContentH, g_amCount, g_amMaxScroll, g_amTop;
static int g_amSeenN = -1, g_amDragY, g_amDragTop;
static BOOL g_amTrack, g_amPlacing, g_amLeft, g_amDrag, g_amCueImg;
static int g_amWidth = AM_W_DEFAULT; /* ширина окна в единицах 96 dpi; в ai_avatar.ini — [MiniChat] Width */
static int g_amSizing, g_amSizeX0, g_amSizeW0; /* тянут край: -1 левый, 1 правый; откуда и с какой ширины */
static BOOL g_amFace; /* беседу ведут здесь, полный чат скрыт: его поле ввода — только для пересылки */
static const wchar_t *g_amSeenText; /* последняя реплика, какой её видели: черновик маршрута сменился ответом */
static POINT g_amGap; /* робот относительно угла окна у его стороны */
static RECT g_amMsgs, g_amField, g_amImg, g_amBtn[AM_BTNS];
static AmItem g_amItems[AM_ITEMS];
static wchar_t *g_amText; /* тексты пузырей подряд, по нулю на каждый */
static size_t g_amTextCap;
static wchar_t g_amStatus[200];

static void aichat_show_full(void);
static void am_layout(void);
static void ac_mini_show(void);

static int am_px(int v) { return v * g_amK / 100; }
/* скругление пузырей и поля: у «Чертежа» (кнопки с прямыми углами) — почти прямое */
static float am_rad(int px) {
  return kThemes[g_theme >= 0 && g_theme < THEME_COUNT ? g_theme : 0].radius == 0 ? (float)am_px(3) : (float)am_px(px);
}
static BOOL am_busy(void) { return g_acId || g_agActive || g_acWebOn || ac_pc_busy() || ac_fq_busy(); }
static int am_gutter(void) { return am_px(10); } /* полоса прокрутки справа от пузырей */

static BOOL ac_mini_open(void) { return g_amWnd && IsWindowVisible(g_amWnd); }
static void ac_mini_close(void) {
  if (g_amWnd) ShowWindow(g_amWnd, SW_HIDE);
}

/* реплика для мини-чата: без разметки Markdown и повторов пробелов; out не короче s */
static void am_clean(const wchar_t *s, wchar_t *out) {
  int n = 0;
  BOOL space = FALSE;
  for (; *s; s++) {
    wchar_t c = *s;
    if (c == L'*' || c == L'`' || c == L'#' || c == L'\r') continue;
    if (c == L'\t') c = L' ';
    if (c == L' ' && space) continue;
    space = c == L' ';
    out[n++] = c;
  }
  while (n && (out[n - 1] == L' ' || out[n - 1] == L'\n')) n--;
  out[n] = 0;
}

/* первая реплика ленты: после «Новой беседы», не больше AM_TURNS последних */
static int am_from(void) { return g_acNewAt < g_acN ? g_acNewAt : g_acN; }
static int am_first(void) { return g_acN - am_from() > AM_TURNS ? g_acN - AM_TURNS : am_from(); }

/* «Размышление ►» или «Шаги ►» над ответом; раскрыт — и текст блоком под заголовком.
   Щелчок по заголовку — am_note_at / ac_show_toggle. y — куда встаёт следующая строка ленты */
static void am_note(HDC dc, int width, int turn, int head, const wchar_t *body, BOOL open, wchar_t **w, int *y) {
  int padX = am_px(10), padY = am_px(7), gap = am_px(8);
  AmItem *it = &g_amItems[g_amCount++];
  it->kind = head;
  it->turn = turn;
  if (head == AM_THINK_HEAD) it->text = open ? L"Размышление ▼" : L"Размышление ►";
  else it->text = open ? L"Шаги ▼" : L"Шаги ►";
  SetRect(&it->r, 0, *y, width, *y + am_px(22));
  *y = it->r.bottom + am_px(2);
  if (!open) return;
  AmItem *b = &g_amItems[g_amCount++];
  b->kind = AM_NOTE_BODY;
  b->turn = turn;
  b->text = *w;
  am_clean(body, *w);
  *w += wcslen(*w) + 1;
  int maxW = (int)(width * 0.94f) - 2 * padX;
  RECT tr = {0, 0, maxW, 0};
  SelectObject(dc, g_amSmall);
  DrawTextW(dc, b->text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
  SetRect(&b->r, 0, *y, (int)(width * 0.94f), *y + tr.bottom + 2 * padY);
  *y = b->r.bottom + gap;
}

/* ответ — как есть, без \r и хвостовых пробелов: разметку разбирает am_rich */
static void am_raw(const wchar_t *s, wchar_t *out) {
  int n = 0;
  for (; *s; s++)
    if (*s != L'\r') out[n++] = *s == L'\t' ? L' ' : *s;
  while (n && (out[n - 1] == L' ' || out[n - 1] == L'\n')) n--;
  out[n] = 0;
}

/* ---- разметка ответа в пузыре (с 2026.10.26) -----------------------------
   Раньше разметка вычищалась (am_clean), и маршрут читался простынёй. Теперь, как
   в полном чате: **жирное**, заголовки #, списки, строки операций и норм маршрута
   (ac_route_kind из ai_chat_format.h) — название жирно, «почему» и времена серым.
   Перенос по словам свой: DrawText не умеет менять шрифт внутри строки. Один и тот
   же проход и меряет (draw = FALSE), и рисует. */
typedef struct {
  HDC dc;
  int x0, y, width;     /* левый край, текущая строка, ширина поля */
  int lineX, maxW;      /* заполнено в строке; самая длинная строка — ширина пузыря */
  int lineH, headH, spaceW;
  BOOL draw;
} AmRich;

static int am_font_h(HDC dc, HFONT f) {
  TEXTMETRICW tm;
  HGDIOBJ old = SelectObject(dc, f);
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  return tm.tmHeight + tm.tmExternalLeading;
}

static void am_rich_init(AmRich *r, HDC dc, int x0, int y0, int width, BOOL draw) {
  memset(r, 0, sizeof(*r));
  r->dc = dc;
  r->x0 = x0;
  r->y = y0;
  r->width = width > 40 ? width : 40;
  r->draw = draw;
  r->lineH = am_font_h(dc, g_amFont);
  r->headH = am_font_h(dc, g_amBold);
  SIZE sp = {4, 0};
  SelectObject(dc, g_amFont);
  GetTextExtentPoint32W(dc, L" ", 1, &sp);
  r->spaceW = sp.cx;
  SetBkMode(dc, TRANSPARENT);
}

/* кусок слова шрифтом f: не влезает — со следующей строки; шире строки — по буквам */
static void am_rich_piece(AmRich *r, const wchar_t *p, int n, HFONT f, COLORREF col, int indent, int lineH) {
  if (n <= 0) return;
  SelectObject(r->dc, f);
  SIZE sz = {0, 0};
  GetTextExtentPoint32W(r->dc, p, n, &sz);
  if (r->lineX > indent && r->lineX + sz.cx > r->width) {
    r->y += lineH;
    r->lineX = indent;
  }
  if (r->lineX + sz.cx > r->width && n > 1) {
    int k = n - 1;
    for (; k > 1; k--) {
      GetTextExtentPoint32W(r->dc, p, k, &sz);
      if (r->lineX + sz.cx <= r->width) break;
    }
    am_rich_piece(r, p, k, f, col, indent, lineH);
    r->y += lineH;
    r->lineX = indent;
    am_rich_piece(r, p + k, n - k, f, col, indent, lineH);
    return;
  }
  if (r->draw) {
    SetTextColor(r->dc, col);
    TextOutW(r->dc, r->x0 + r->lineX, r->y, p, n);
  }
  r->lineX += sz.cx;
  if (r->lineX > r->maxW) r->maxW = r->lineX;
}

/* текст [p, e) одним шрифтом и цветом, слово за словом; inl — разбирать **жирное** и `код` */
static void am_rich_run(AmRich *r, const wchar_t *p, const wchar_t *e, BOOL bold, COLORREF col, int indent, int lineH,
                        BOOL inl) {
  BOOL space = FALSE;
  while (p < e) {
    if (*p == L' ') {
      space = TRUE;
      p++;
      continue;
    }
    if (inl && p + 1 < e && p[0] == L'*' && p[1] == L'*') { /* жирное — до парной ** в этой же строке */
      const wchar_t *close = NULL;
      for (const wchar_t *q = p + 2; q + 1 < e; q++)
        if (q[0] == L'*' && q[1] == L'*') { close = q; break; }
      if (bold || close) {
        bold = !bold;
        p += 2;
        continue;
      }
    }
    if (inl && *p == L'`') { /* код — тем же шрифтом, маркер прячем */
      p++;
      continue;
    }
    const wchar_t *q = p;
    while (q < e && *q != L' ' && !(inl && ((q + 1 < e && q[0] == L'*' && q[1] == L'*') || *q == L'`'))) q++;
    if (q == p) { p++; continue; }
    HFONT f = bold ? g_amBold : g_amFont;
    if (space && r->lineX > indent) { /* пробел перед словом; вместе не влезают — слово с новой строки */
      SelectObject(r->dc, f);
      SIZE sz = {0, 0};
      GetTextExtentPoint32W(r->dc, p, (int)(q - p), &sz);
      if (r->lineX + r->spaceW + sz.cx <= r->width) r->lineX += r->spaceW;
      else {
        r->y += lineH;
        r->lineX = indent;
      }
    }
    am_rich_piece(r, p, (int)(q - p), f, col, indent, lineH);
    space = FALSE;
    p = q;
  }
}

/* одна строка ответа: заголовок, пункт списка, строка маршрута или обычный текст */
static void am_rich_line(AmRich *r, const wchar_t *p, const wchar_t *e, BOOL fenced) {
  int lineH = r->lineH, indent = 0;
  r->lineX = 0;
  if (fenced) {
    am_rich_run(r, p, e, FALSE, am_ink(), 0, lineH, FALSE);
    r->y += lineH;
    return;
  }
  const wchar_t *q = p;
  while (q < e && *q == L'#') q++;
  if (q > p && q < e && *q == L' ') { /* заголовок — жирно */
    lineH = r->headH;
    am_rich_run(r, q + 1, e, TRUE, am_ink(), 0, lineH, TRUE);
    r->y += lineH;
    return;
  }
  if (e - p > 2 && (p[0] == L'-' || p[0] == L'*' || p[0] == 0x2022) && p[1] == L' ') { /* пункт списка */
    am_rich_piece(r, L"\x2022 ", 2, g_amFont, am_ink(), 0, lineH);
    indent = r->lineX;
    am_rich_run(r, p + 2, e, FALSE, am_ink(), indent, lineH, TRUE);
    r->y += lineH;
    return;
  }
  q = p;
  while (q < e && *q >= L'0' && *q <= L'9') q++;
  if (q > p && q - p <= 2 && q + 1 < e && q[0] == L'.' && q[1] == L' ') { /* «1. » — висячий отступ */
    am_rich_piece(r, p, (int)(q + 2 - p), g_amFont, am_ink(), 0, lineH);
    indent = r->lineX;
    am_rich_run(r, q + 2, e, FALSE, am_ink(), indent, lineH, TRUE);
    r->y += lineH;
    return;
  }
  const wchar_t *mark;
  int kind = ac_route_kind(p, e, &mark);
  if (kind == ACR_OP) { /* «010 Токарная.» жирно, «почему» после « — » серым */
    const wchar_t *why = mark;
    while (why + 3 <= e && wcsncmp(why, L" — ", 3)) why++;
    if (why + 3 > e) why = e;
    am_rich_run(r, p, mark, TRUE, am_ink(), 0, lineH, FALSE);
    am_rich_run(r, mark, why, FALSE, am_ink(), 0, lineH, FALSE);
    am_rich_run(r, why, e, FALSE, am_muted(), 0, lineH, FALSE);
  } else if (kind == ACR_NORM) { /* «010 Токарная:» жирно, времена серым */
    am_rich_run(r, p, mark, TRUE, am_ink(), 0, lineH, FALSE);
    am_rich_run(r, mark + 1, e, FALSE, am_muted(), 0, lineH, FALSE);
  } else if (kind == ACR_HEAD) {
    am_rich_run(r, p, e, TRUE, am_ink(), 0, lineH, FALSE);
  } else if (kind == ACR_META) {
    am_rich_run(r, p, e, FALSE, am_muted(), 0, lineH, FALSE);
  } else if (kind == ACR_LABEL) {
    am_rich_run(r, p, mark, TRUE, am_ink(), 0, lineH, FALSE);
    am_rich_run(r, mark, e, FALSE, am_ink(), 0, lineH, FALSE);
  } else {
    am_rich_run(r, p, e, FALSE, am_ink(), 0, lineH, TRUE);
  }
  r->y += lineH;
}

/* ---- нормы таблицей (с 2026.10.30, как в полном чате) ---------------------------------
   Строки norms.c подряд — таблица: № | операция | Тпз | Тшт; пометка («(неполная)», «Тшк …»)
   — серой строкой под названием. Числа — ровно как в строке расчёта (ac_norm_val). */
#define AM_NORM_MAX 40
static HFONT g_amMono; /* цифры норм — моноширинным, по высоте шрифта пузыря */
static HFONT am_mono(void) {
  if (g_amMono) return g_amMono;
  LOGFONTW lf;
  if (!g_amFont || !GetObjectW(g_amFont, sizeof(lf), &lf)) return g_amFont;
  lf.lfWeight = FW_NORMAL;
  lstrcpynW(lf.lfFaceName, font_exists(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas", LF_FACESIZE);
  g_amMono = CreateFontIndirectW(&lf);
  return g_amMono ? g_amMono : g_amFont;
}

static void am_norm_text(AmRich *r, HFONT f, COLORREF col, const wchar_t *p, const wchar_t *e, int x0, int x1, int y,
                         BOOL right) {
  while (p < e && *p == L' ') p++;
  while (e > p && (e[-1] == L' ' || e[-1] == 0x00B7)) e--;
  if (!r->draw || e <= p) return;
  SelectObject(r->dc, f);
  SetTextColor(r->dc, col);
  RECT tr = {r->x0 + x0, y, r->x0 + x1, y + r->lineH};
  DrawTextW(r->dc, p, (int)(e - p), &tr, (right ? DT_RIGHT : DT_LEFT) | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void am_norm_line(AmRich *r, int y, BOOL strong) {
  if (!r->draw) return;
  HBRUSH b = CreateSolidBrush(strong ? am_muted() : ui_hairline());
  RECT l = {r->x0, y, r->x0 + r->width, y + 1};
  FillRect(r->dc, &l, b);
  DeleteObject(b);
}

/* n строк норм: p[i]..e[i], mark[i] — «: » после названия */
static void am_rich_norms(AmRich *r, const wchar_t **p, const wchar_t **e, const wchar_t **mark, int n) {
  int w = r->width, gap = am_px(6), numW = am_px(30), tW = am_px(64);
  int xOp = numW + gap, xPz = w - 2 * tW - gap, xSh = w - tW;
  int rowH = r->lineH + am_px(4);
  r->y += am_px(2);
  am_norm_text(r, g_amFont, am_muted(), L"№", L"№" + 1, 0, numW, r->y, FALSE);
  am_norm_text(r, g_amFont, am_muted(), L"Операция", L"Операция" + 8, xOp, xPz - gap, r->y, FALSE);
  am_norm_text(r, g_amFont, am_muted(), L"Тпз, мин", L"Тпз, мин" + 8, xPz, xPz + tW, r->y, TRUE);
  am_norm_text(r, g_amFont, am_muted(), L"Тшт, мин", L"Тшт, мин" + 8, xSh, w, r->y, TRUE);
  r->y += rowH;
  am_norm_line(r, r->y - am_px(2), TRUE);
  for (int i = 0; i < n; i++) {
    const wchar_t *rs = mark[i] + 2, *pzb, *pze, *shb, *she, *a1 = rs, *a2 = rs;
    BOOL pz = ac_norm_val(rs, e[i], L"Тпз ", &pzb, &pze, &a1);
    BOOL sh = ac_norm_val(rs, e[i], L"Тшт ", &shb, &she, &a2);
    int y = r->y + am_px(1);
    am_norm_text(r, am_mono(), am_muted(), p[i], p[i] + 3, 0, numW, y, FALSE);
    am_norm_text(r, g_amBold, am_ink(), p[i] + 4, mark[i], xOp, xPz - gap, y, FALSE);
    if (pz) am_norm_text(r, am_mono(), am_ink(), pzb, pze, xPz, xPz + tW, y, TRUE);
    if (sh) am_norm_text(r, am_mono(), am_ink(), shb, she, xSh, w, y, TRUE);
    r->y += rowH;
    const wchar_t *rest = sh ? a2 : pz ? a1 : rs;
    while (rest < e[i] && (*rest == L' ' || *rest == 0x00B7)) rest++;
    if (rest < e[i]) { /* пометка — серым под названием */
      am_norm_text(r, g_amFont, am_muted(), rest, e[i], xOp, w, r->y - am_px(3), FALSE);
      r->y += r->lineH;
    }
    am_norm_line(r, r->y - am_px(2), FALSE);
  }
  if (r->maxW < w) r->maxW = w;
  r->y += am_px(4);
}

/* весь ответ: строки через \n, пустая строка — отступ абзаца, ``` — без разбора */
static void am_rich_text(AmRich *r, const wchar_t *s) {
  BOOL fenced = FALSE, gap = FALSE;
  for (const wchar_t *p = s; *p;) {
    const wchar_t *e = p;
    while (*e && *e != L'\n') e++;
    const wchar_t *t = p;
    while (t < e && *t == L' ') t++;
    const wchar_t *mk;
    if (!fenced && ac_route_kind(t, e, &mk) == ACR_NORM && !wcsncmp(mk, L": Тпз ", 6)) { /* нормы подряд — таблицей */
      const wchar_t *np[AM_NORM_MAX], *ne[AM_NORM_MAX], *nm[AM_NORM_MAX];
      int n = 0;
      for (;;) {
        np[n] = t, ne[n] = e, nm[n] = mk, n++;
        p = *e ? e + 1 : e;
        if (!*p || n == AM_NORM_MAX) break;
        const wchar_t *e2 = p, *t2 = p;
        while (*e2 && *e2 != L'\n') e2++;
        while (t2 < e2 && *t2 == L' ') t2++;
        if (ac_route_kind(t2, e2, &mk) != ACR_NORM) break;
        t = t2, e = e2;
      }
      if (gap) r->y += r->lineH / 2;
      gap = FALSE;
      am_rich_norms(r, np, ne, nm, n);
      continue;
    }
    if (e - t >= 3 && t[0] == L'`' && t[1] == L'`' && t[2] == L'`') fenced = !fenced;
    else if (t == e) gap = r->y > 0; /* пустая строка — полстроки, но не в начале */
    else {
      if (gap) r->y += r->lineH / 2;
      gap = FALSE;
      am_rich_line(r, t, e, fenced);
    }
    p = *e ? e + 1 : e;
  }
}

/* реплики пузырями: ширина, высота, место в ленте. Тексты целиком — в g_amText. */
static void am_build(HDC dc, int width) {
  int padX = am_px(10), padY = am_px(7), gap = am_px(8), y = 0;
  int first = am_first(), more = first - am_from();
  g_amCount = 0;
  g_amContentH = 0;
  size_t need = 600;
  for (int i = first; i < g_acN; i++) {
    need += (g_acTurns[i].text ? wcslen(g_acTurns[i].text) : 0) + 16;
    need += (g_acTurns[i].think ? wcslen(g_acTurns[i].think) : 0) + (g_acTurns[i].steps ? wcslen(g_acTurns[i].steps) : 0) + 32;
  }
  if (need > g_amTextCap) {
    wchar_t *t = (wchar_t *)realloc(g_amText, need * sizeof(wchar_t));
    if (!t) return;
    g_amText = t;
    g_amTextCap = need;
  }
  wchar_t *w = g_amText;
  SelectObject(dc, g_amFont);
  if (more > 0) { /* ранние реплики этой беседы — в полном чате */
    AmItem *it = &g_amItems[g_amCount++];
    it->kind = AM_MORE;
    it->turn = -1;
    it->text = w;
    w += swprintf(w, 80, L"Ранние реплики этой беседы (%d) — в полном чате ⤢", more) + 1;
    SetRect(&it->r, 0, 0, width, am_px(22));
    y = it->r.bottom + gap;
  }
  if (first == g_acN && !am_busy()) {
    AmItem *it = &g_amItems[g_amCount++];
    BOOL fresh = g_acNewAt && g_acNewAt == g_acN;
    it->kind = AM_HINT;
    it->turn = -1;
    it->text = w;
    lstrcpynW(w, fresh ? L"Новая беседа: что было раньше, ИИ не помнит. Прежняя беседа сохранена — в полном чате ⤢ "
                         L"её можно показать."
                       : L"Спросите что-нибудь — отвечу здесь. Картинку прикрепите скрепкой, Ctrl+V или "
                         L"перетащите в окно. ⟳ — новая беседа, ⤢ — полный чат.", 300);
    w += wcslen(w) + 1;
    RECT tr = {0, 0, width - 2 * padX, 0};
    DrawTextW(dc, it->text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_CENTER);
    int h = tr.bottom + 2 * padY;
    if (fresh && ac_new_topic_undo_ready()) h += am_px(26); /* «Вернуть прежнюю беседу» */
    SetRect(&it->r, 0, y, width, y + h);
    y = it->r.bottom + gap;
  }
  for (int i = first; i < g_acN && g_amCount < AM_ITEMS - 6; i++) {
    if (g_acTurns[i].assistant && g_acTurns[i].think) am_note(dc, width, i, AM_THINK_HEAD, g_acTurns[i].think, g_acShowThink, &w, &y);
    if (g_acTurns[i].assistant && g_acTurns[i].steps) am_note(dc, width, i, AM_STEPS_HEAD, g_acTurns[i].steps, g_acShowSteps, &w, &y);
    AmItem *it = &g_amItems[g_amCount++];
    it->kind = g_acTurns[i].assistant ? AM_AI : AM_USER;
    it->turn = i;
    it->text = w;
    if (g_acTurns[i].png) w += swprintf(w, 16, L"[картинка] ");
    if (it->kind == AM_AI) am_raw(g_acTurns[i].text ? g_acTurns[i].text : L"", w); /* разметку рисует am_rich */
    else am_clean(g_acTurns[i].text ? g_acTurns[i].text : L"", w);
    w += wcslen(w) + 1;
    int maxW = (int)(width * (it->kind == AM_AI ? 0.94f : 0.80f)) - 2 * padX;
    RECT tr = {0, 0, maxW, 0};
    if (it->kind == AM_AI) {
      AmRich rt;
      am_rich_init(&rt, dc, 0, 0, maxW, FALSE);
      am_rich_text(&rt, it->text);
      tr.right = rt.maxW;
      tr.bottom = rt.y;
      SelectObject(dc, g_amFont);
    } else {
      DrawTextW(dc, it->text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    }
    int bw = tr.right + 2 * padX, bh = tr.bottom + 2 * padY;
    int x = it->kind == AM_AI ? 0 : width - bw;
    SetRect(&it->r, x, y, x + bw, y + bh);
    y += bh + gap;
  }
  if (am_busy()) { /* ИИ ещё работает — пузырь с бегущими точками */
    AmItem *it = &g_amItems[g_amCount++];
    it->kind = AM_DOTS;
    it->turn = -1;
    it->text = L"";
    SetRect(&it->r, 0, y, am_px(58), y + am_px(30));
    y = it->r.bottom + gap;
  }
  g_amContentH = y > gap ? y - gap : 0;
}

/* сколько ленты не влезло; прокрутка (от низа) — в этих пределах */
static void am_clamp(void) {
  int areaH = g_amMsgs.bottom - g_amMsgs.top;
  g_amMaxScroll = g_amContentH > areaH ? g_amContentH - areaH : 0;
  if (g_amScroll > g_amMaxScroll) g_amScroll = g_amMaxScroll;
  if (g_amScroll < 0) g_amScroll = 0;
}

/* Новая реплика: вопрос — лента вниз; длинный ответ — его начало к верху ленты,
   а не последние строки: читают сверху. */
static void am_anchor_last(void) {
  int areaH = g_amMsgs.bottom - g_amMsgs.top;
  g_amScroll = 0;
  for (int i = g_amCount - 1; i >= 0; i--) {
    const AmItem *it = &g_amItems[i];
    if (it->turn < 0) continue;
    if (it->kind == AM_AI && it->turn == g_acN - 1 && it->r.bottom - it->r.top > areaH)
      g_amScroll = g_amContentH - it->r.top - areaH;
    break;
  }
  am_clamp();
}

static void am_scroll_by(int delta) {
  g_amScroll += delta;
  am_clamp();
  if (g_amWnd) InvalidateRect(g_amWnd, NULL, FALSE);
}

/* ползунок полосы прокрутки, координаты окна; FALSE — всё и так видно */
static BOOL am_thumb(RECT *th) {
  int areaH = g_amMsgs.bottom - g_amMsgs.top;
  if (g_amMaxScroll <= 0 || areaH <= 0 || g_amContentH <= 0) return FALSE;
  int h = MulDiv(areaH, areaH, g_amContentH);
  if (h < am_px(24)) h = am_px(24);
  if (h > areaH) h = areaH;
  int top = g_amMsgs.top + MulDiv(g_amMaxScroll - g_amScroll, areaH - h, g_amMaxScroll);
  SetRect(th, g_amMsgs.right - am_px(5), top, g_amMsgs.right - am_px(1), top + h);
  return TRUE;
}

/* скруглённая фигура со сглаженным краем (ui_theme.c) */
static void am_shape(HDC dc, RECT r, float rad, COLORREF fill, COLORREF border, float bw) {
  RECT box = r;
  InflateRect(&box, 1, 1);
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, dc, box, box.left, box.top, TRUE)) return;
  UiShape s;
  memset(&s, 0, sizeof(s));
  s.r = rad;
  s.fill = fill;
  s.fillA = 255;
  if (bw > 0.0f) {
    s.border = border;
    s.borderA = 255;
    s.bw = bw;
  }
  ui_shape(&cv, r, &s);
  ui_canvas_end(&cv, dc);
}

static void am_icon(HDC dc, RECT r, wchar_t glyph, const wchar_t *fallback, COLORREF col, int px) {
  HFONT f = ui_icon_font(am_px(px));
  SetTextColor(dc, col);
  HGDIOBJ of = SelectObject(dc, f ? f : g_amBold);
  if (f) DrawTextW(dc, &glyph, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  else DrawTextW(dc, fallback, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, of);
}

/* миниатюра ждущей картинки (g_acThumbBmp, до 80 px) — вписать в box на белом */
static void am_thumb_draw(HDC dc, RECT box) {
  BITMAP bm;
  if (!g_acThumbBmp || !GetObjectW(g_acThumbBmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return;
  am_shape(dc, box, (float)am_px(5), RGB(255, 255, 255), 0, 0.0f);
  InflateRect(&box, -am_px(2), -am_px(2));
  int bw = box.right - box.left, bh = box.bottom - box.top;
  int w = bw, h = MulDiv(bm.bmHeight, bw, bm.bmWidth);
  if (h > bh) {
    h = bh;
    w = MulDiv(bm.bmWidth, bh, bm.bmHeight);
  }
  HDC src = CreateCompatibleDC(dc);
  if (!src) return;
  HGDIOBJ old = SelectObject(src, g_acThumbBmp);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  AlphaBlend(dc, box.left + (bw - w) / 2, box.top + (bh - h) / 2, w > 0 ? w : 1, h > 0 ? h : 1, src, 0, 0,
             bm.bmWidth, bm.bmHeight, bf);
  SelectObject(src, old);
  DeleteDC(src);
}

/* всё окно — в pdc (WM_PAINT или WM_PRINTCLIENT: снимок окна) */
static void am_draw(HWND hwnd, HDC pdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  HDC dc = CreateCompatibleDC(pdc);
  HBITMAP bm = dc ? CreateCompatibleBitmap(pdc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1) : NULL;
  if (!dc || !bm) {
    if (bm) DeleteObject(bm);
    if (dc) DeleteDC(dc);
    return;
  }
  HGDIOBJ ob = SelectObject(dc, bm);
  FillRect(dc, &rc, g_amBgBr);
  SetBkMode(dc, TRANSPARENT);
  COLORREF ink = am_ink(), muted = am_muted(), accent = am_accent(), line = am_line();
  /* шапка: огонёк настроения, имя, чем занят; ⟳, ⤢ и × справа; под ней — линия, как у чата */
  int head = am_px(42);
  RECT dot = {am_px(14), head / 2 - am_px(5), am_px(24), head / 2 + am_px(5)};
  COLORREF mood = kAcAvatarMood[g_acAvatarState];
  am_shape(dc, dot, -1.0f, mood, mood, 0.0f);
  SelectObject(dc, g_amBold);
  SetTextColor(dc, ink);
  RECT tt = {am_px(32), 0, am_px(120), head};
  DrawTextW(dc, L"Мини-ИИ", -1, &tt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SIZE nameSz = {0, 0};
  GetTextExtentPoint32W(dc, L"Мини-ИИ", 7, &nameSz);
  const wchar_t *sub = g_amStatus[0] ? g_amStatus : am_busy() ? L"думает…"
                       : g_aiProvider            ? L"Z.ai API"
                                                 : L"на этом компьютере";
  SelectObject(dc, g_amSmall);
  SetTextColor(dc, am_busy() ? accent : muted);
  RECT st = {am_px(32) + nameSz.cx + am_px(8), 0, g_amBtn[AM_NEW].left - am_px(4), head};
  DrawTextW(dc, sub, -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  BOOL canNew = g_acNewAt != g_acN; /* сбрасывать нечего — значок приглушён */
  for (int b = AM_NEW; b <= AM_CLOSE; b++) {
    BOOL hot = g_amHot == b;
    if (hot) am_shape(dc, g_amBtn[b], 6.0f, b == AM_CLOSE ? AM_RED : am_hover(), 0, 0.0f);
    wchar_t glyph = b == AM_NEW ? 0xE72C : b == AM_FULL ? 0xE740 : 0xE8BB;
    const wchar_t *fb = b == AM_NEW ? L"↻" : b == AM_FULL ? L"⤢" : L"×";
    COLORREF ic = hot && b == AM_CLOSE ? RGB(255, 255, 255) : b == AM_NEW && !canNew ? muted : ink;
    am_icon(dc, g_amBtn[b], glyph, fb, ic, 11);
  }
  {
    RECT rule = {0, head - 1, rc.right, head};
    HBRUSH hb = CreateSolidBrush(line);
    FillRect(dc, &rule, hb);
    DeleteObject(hb);
  }
  /* лента: новое — внизу, колесом и полосой справа — к старому */
  am_build(dc, g_amMsgs.right - g_amMsgs.left - am_gutter());
  am_clamp();
  int top = g_amMsgs.bottom - g_amContentH + g_amScroll;
  g_amTop = top;
  SetRectEmpty(&g_amBtn[AM_UNDO]);
  int saved = SaveDC(dc);
  IntersectClipRect(dc, g_amMsgs.left, g_amMsgs.top, g_amMsgs.right, g_amMsgs.bottom);
  for (int i = 0; i < g_amCount; i++) {
    AmItem *it = &g_amItems[i];
    RECT r = it->r;
    OffsetRect(&r, g_amMsgs.left, top);
    if (r.bottom < g_amMsgs.top || r.top > g_amMsgs.bottom) continue;
    if (it->kind == AM_MORE) {
      SelectObject(dc, g_amSmall);
      SetTextColor(dc, muted);
      DrawTextW(dc, it->text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
      continue;
    }
    if (it->kind == AM_THINK_HEAD || it->kind == AM_STEPS_HEAD) { /* заголовок блока: мелкой строкой, как ссылка */
      SelectObject(dc, g_amSmall);
      SetTextColor(dc, accent);
      RECT hr = r;
      hr.left += am_px(4);
      DrawTextW(dc, it->text, -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      continue;
    }
    if (it->kind == AM_NOTE_BODY) { /* раскрытый блок: тексты размышления и шагов — приглушённо, на бумаге потемнее */
      am_shape(dc, r, am_rad(10), am_bg2(), 0, 0.0f);
      SelectObject(dc, g_amSmall);
      SetTextColor(dc, muted);
      RECT tr = r;
      InflateRect(&tr, -am_px(10), -am_px(7));
      DrawTextW(dc, it->text, -1, &tr, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
      continue;
    }
    if (it->kind == AM_HINT) {
      SelectObject(dc, g_amFont);
      SetTextColor(dc, muted);
      RECT tr = r;
      InflateRect(&tr, -am_px(10), -am_px(7));
      if (g_acNewAt && g_acNewAt == g_acN && ac_new_topic_undo_ready()) { /* ссылка «Вернуть» под подсказкой */
        RECT link = {tr.left, tr.bottom - am_px(22), tr.right, tr.bottom};
        tr.bottom = link.top;
        SetTextColor(dc, g_amHot == AM_UNDO ? blend_rgb(accent, ink, 80) : accent);
        DrawTextW(dc, L"Вернуть прежнюю беседу", -1, &link, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SIZE ls = {0, 0};
        GetTextExtentPoint32W(dc, L"Вернуть прежнюю беседу", 22, &ls);
        int cx = (link.left + link.right) / 2;
        SetRect(&g_amBtn[AM_UNDO], cx - ls.cx / 2 - am_px(6), link.top, cx + ls.cx / 2 + am_px(6), link.bottom);
        SetTextColor(dc, muted);
      }
      DrawTextW(dc, it->text, -1, &tr, DT_WORDBREAK | DT_NOPREFIX | DT_CENTER);
      continue;
    }
    if (it->kind == AM_DOTS) {
      am_shape(dc, r, am_rad(12), am_bg2(), 0, 0.0f);
      DWORD now = GetTickCount();
      for (int d = 0; d < 3; d++) {
        float ph = 0.5f + 0.5f * sinf(now / 170.0f - d * 1.1f);
        int cx = r.left + am_px(17) + d * am_px(12), cy = (r.top + r.bottom) / 2 - (int)(ph * am_px(3));
        int rr = am_px(3) + (ph > 0.5f ? 1 : 0);
        RECT dd = {cx - rr, cy - rr, cx + rr, cy + rr};
        am_shape(dc, dd, -1.0f, blend_rgb(accent, am_bg(), (int)(90.0f * (1.0f - ph))), 0, 0.0f);
      }
      continue;
    }
    BOOL ai = it->kind == AM_AI;
    /* ответ — карточка на бумаге с чертой акцента слева, вопрос — на бумаге потемнее: как в полном чате */
    if (ai) am_shape(dc, r, am_rad(12), am_bg(), line, 1.0f);
    else am_shape(dc, r, am_rad(12), am_bg2(), 0, 0.0f);
    RECT tr = r;
    InflateRect(&tr, -am_px(10), -am_px(7));
    if (ai) {
      RECT bar = {r.left + am_px(3), r.top + am_px(8), r.left + am_px(5), r.bottom - am_px(8)};
      am_shape(dc, bar, 1.0f, accent, 0, 0.0f);
      tr.left += am_px(4);
      AmRich rt;
      am_rich_init(&rt, dc, tr.left, tr.top, tr.right - tr.left, TRUE);
      am_rich_text(&rt, it->text);
      continue;
    }
    SelectObject(dc, g_amFont);
    SetTextColor(dc, ink);
    DrawTextW(dc, it->text, -1, &tr, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
  }
  RestoreDC(dc, saved);
  /* лента длиннее окна — полоса прокрутки справа */
  RECT th;
  if (am_thumb(&th)) am_shape(dc, th, (float)am_px(2), g_amDrag ? accent : blend_rgb(COL_LINE, ink, 70), 0, 0.0f);
  /* план файловой задачи ждёт решения */
  if (g_acPlanOn) {
    SelectObject(dc, g_amSmall);
    SetTextColor(dc, muted);
    RECT pt = {g_amMsgs.left + am_px(4), g_amBtn[AM_APPLY].top, g_amBtn[AM_APPLY].left - am_px(6), g_amBtn[AM_APPLY].bottom};
    DrawTextW(dc, L"План готов — проверьте", -1, &pt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    am_shape(dc, g_amBtn[AM_APPLY], am_rad(8), g_amHot == AM_APPLY ? blend_rgb(accent, ink, 40) : accent, 0, 0.0f);
    am_shape(dc, g_amBtn[AM_CANCEL], am_rad(8), g_amHot == AM_CANCEL ? am_hover() : am_bg(), line, 1.0f);
    SelectObject(dc, g_amFont);
    SetTextColor(dc, am_on_accent());
    DrawTextW(dc, L"Применить", -1, &g_amBtn[AM_APPLY], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, ink);
    DrawTextW(dc, L"Отмена", -1, &g_amBtn[AM_CANCEL], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  /* картинка к следующему вопросу: миниатюра, подпись, × — убрать */
  if (g_acImg) {
    am_shape(dc, g_amImg, am_rad(10), am_bg2(), line, 1.0f);
    RECT box = {g_amImg.left + am_px(6), g_amImg.top + am_px(5), g_amImg.left + am_px(6) + (g_amImg.bottom - g_amImg.top) - am_px(10),
                g_amImg.bottom - am_px(5)};
    am_thumb_draw(dc, box);
    RECT tx = {box.right + am_px(10), g_amImg.top, g_amBtn[AM_IMGDEL].left - am_px(4), g_amImg.bottom};
    SelectObject(dc, g_amFont);
    SetTextColor(dc, ink);
    DrawTextW(dc, L"Картинка к следующему вопросу", -1, &tx, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (g_amHot == AM_IMGDEL) am_shape(dc, g_amBtn[AM_IMGDEL], 6.0f, am_hover(), 0, 0.0f);
    am_icon(dc, g_amBtn[AM_IMGDEL], 0xE711, L"×", muted, 11);
  }
  /* поле ввода, скрепка и «отправить» / «Стоп»; поле — как карточка ввода чата: в фокусе рамка акцентом */
  BOOL focus = GetFocus() == g_amIn;
  am_shape(dc, g_amField, am_rad(14), am_bg(), focus ? accent : line, focus ? 1.5f : 1.0f);
  if (g_amHot == AM_ATTACH) am_shape(dc, g_amBtn[AM_ATTACH], -1.0f, am_hover(), 0, 0.0f);
  am_icon(dc, g_amBtn[AM_ATTACH], 0xE723, L"+", g_acImg ? accent : muted, 13);
  if (am_busy()) { /* ИИ думает — красноватая «Стоп» со словом, её не спутать с «отправить» */
    am_shape(dc, g_amBtn[AM_SEND], -1.0f, blend_rgb(am_bg(), AM_RED, g_amHot == AM_SEND ? 60 : 30), AM_RED, 1.0f);
    RECT ic = g_amBtn[AM_SEND], tx = g_amBtn[AM_SEND];
    ic.right = ic.left + am_px(30);
    tx.left = ic.right - am_px(4);
    am_icon(dc, ic, 0xE71A, L"■", AM_RED, 11);
    SelectObject(dc, g_amBold);
    SetTextColor(dc, AM_RED);
    DrawTextW(dc, L"Стоп", -1, &tx, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  } else {
    am_shape(dc, g_amBtn[AM_SEND], -1.0f, g_amHot == AM_SEND ? blend_rgb(accent, ink, 40) : accent, 0, 0.0f);
    am_icon(dc, g_amBtn[AM_SEND], 0xE724, L"➤", am_on_accent(), 13);
  }
  ui_sheet_frame(dc, rc, hwnd); /* «Чертёж»: рамка листа */
  BitBlt(pdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
  SelectObject(dc, ob);
  DeleteObject(bm);
  DeleteDC(dc);
}

static void am_paint(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC pdc = BeginPaint(hwnd, &ps);
  am_draw(hwnd, pdc);
  EndPaint(hwnd, &ps);
}

/* Низ окна — на уровне плеч робота; окно выше экрана не уходит. */
static void am_place(void) {
  if (!g_amWnd || !g_acAvatar || !IsWindowVisible(g_acAvatar)) return;
  RECT ar, cur;
  GetWindowRect(g_acAvatar, &ar);
  GetWindowRect(g_amWnd, &cur);
  MONITORINFO mi = {sizeof(mi)};
  if (!GetMonitorInfoW(MonitorFromWindow(g_acAvatar, MONITOR_DEFAULTTONEAREST), &mi)) return;
  RECT wa = mi.rcWork;
  int w = cur.right - cur.left, h = cur.bottom - cur.top, S = ar.right - ar.left;
  int tuck = S / 5, gap = am_px(2); /* у окна робота прозрачные поля — встаём к самому телу */
  g_amLeft = (ar.left + ar.right) / 2 > (wa.left + wa.right) / 2;
  int x = g_amLeft ? ar.left + tuck - gap - w : ar.right - tuck + gap;
  if (x < wa.left || x + w > wa.right) {
    g_amLeft = !g_amLeft;
    x = g_amLeft ? ar.left + tuck - gap - w : ar.right - tuck + gap;
  }
  if (x + w > wa.right) x = wa.right - w;
  if (x < wa.left) x = wa.left;
  int y = ar.top + S * 45 / 100 - h;
  if (y + h > wa.bottom) y = wa.bottom - h;
  if (y < wa.top) y = wa.top;
  g_amGap.x = ar.left - (g_amLeft ? x + w : x);
  g_amGap.y = ar.top - (y + h);
  g_amPlacing = TRUE;
  SetWindowPos(g_amWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
  g_amPlacing = FALSE;
}

/* подсказки к значкам — по прямоугольникам кнопок; AM_TIP_STATUS — шапка: состояние целиком */
#define AM_TIP_STATUS AM_BTNS
static void am_tips(void) {
  if (!g_amTip) return;
  for (int b = AM_NEW; b <= AM_TIP_STATUS; b++) {
    TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = g_amWnd;
    ti.uId = (UINT_PTR)b;
    if (b == AM_TIP_STATUS) SetRect(&ti.rect, am_px(8), 0, g_amBtn[AM_NEW].left - am_px(4), am_px(42));
    else if (b > AM_IMGDEL) continue;
    else ti.rect = g_amBtn[b];
    SendMessageW(g_amTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
  }
}

static const wchar_t *am_tip_text(int b) {
  switch (b) {
  case AM_TIP_STATUS: /* в шапке строка состояния обрезается — здесь целиком */
    return g_amStatus[0] ? g_amStatus : am_busy() ? L"ИИ думает" : NULL;
  case AM_NEW:
    return g_acNewAt == g_acN ? L"Новая беседа — сейчас и так новая"
                              : L"Новая беседа: ИИ забудет прежние реплики. Они останутся в полном чате, "
                                L"а до нового вопроса их можно вернуть";
  case AM_FULL: return L"Полный чат со всей историей";
  case AM_CLOSE: return L"Закрыть (Esc). Ответ, если ИИ думает, всё равно придёт";
  case AM_ATTACH: return L"Картинка к вопросу: из файла, снимок области экрана или из буфера (Ctrl+V). Можно и перетащить в окно";
  case AM_SEND: return am_busy() ? L"Остановить ответ ИИ" : L"Отправить (Enter). Shift+Enter — новая строка";
  case AM_IMGDEL: return L"Убрать картинку";
  }
  return L"";
}

/* Высота — по содержимому: лента от 70 px до ~60 % высоты экрана, дальше листается. */
static void am_layout(void) {
  if (!g_amWnd) return;
  int W = am_px(g_amWidth), pad = am_px(10), head = am_px(42), field = am_px(52), act = g_acPlanOn ? am_px(42) : 0;
  int img = g_acImg ? am_px(54) : 0;
  HDC dc = GetDC(g_amWnd);
  if (dc) {
    am_build(dc, W - 2 * pad - am_gutter());
    ReleaseDC(g_amWnd, dc);
  }
  MONITORINFO mi = {sizeof(mi)};
  HWND ref = g_acAvatar && IsWindowVisible(g_acAvatar) ? g_acAvatar : g_amWnd;
  int workH = GetMonitorInfoW(MonitorFromWindow(ref, MONITOR_DEFAULTTONEAREST), &mi) ? mi.rcWork.bottom - mi.rcWork.top
                                                                                       : am_px(800);
  int maxH = workH * 62 / 100 - (head + am_px(12) + act + img + field + pad);
  if (maxH > am_px(520)) maxH = am_px(520);
  if (maxH < am_px(140)) maxH = am_px(140);
  int msgsH = g_amContentH + am_px(8);
  if (msgsH < am_px(70)) msgsH = am_px(70);
  if (msgsH > maxH) msgsH = maxH;
  SetRect(&g_amMsgs, pad, head + am_px(6), W - pad, head + am_px(6) + msgsH);
  int y = g_amMsgs.bottom + am_px(6);
  memset(g_amBtn, 0, sizeof(g_amBtn));
  SetRect(&g_amBtn[AM_CLOSE], W - am_px(8) - am_px(30), (head - am_px(28)) / 2, W - am_px(8), (head + am_px(28)) / 2);
  SetRect(&g_amBtn[AM_FULL], g_amBtn[AM_CLOSE].left - am_px(32), g_amBtn[AM_CLOSE].top, g_amBtn[AM_CLOSE].left - am_px(2),
          g_amBtn[AM_CLOSE].bottom);
  SetRect(&g_amBtn[AM_NEW], g_amBtn[AM_FULL].left - am_px(32), g_amBtn[AM_CLOSE].top, g_amBtn[AM_FULL].left - am_px(2),
          g_amBtn[AM_CLOSE].bottom);
  if (act) {
    SetRect(&g_amBtn[AM_CANCEL], W - pad - am_px(82), y + am_px(4), W - pad, y + act - am_px(6));
    SetRect(&g_amBtn[AM_APPLY], g_amBtn[AM_CANCEL].left - am_px(8) - am_px(104), y + am_px(4),
            g_amBtn[AM_CANCEL].left - am_px(8), y + act - am_px(6));
    y += act;
  }
  SetRectEmpty(&g_amImg);
  if (img) {
    SetRect(&g_amImg, pad, y, W - pad, y + img - am_px(6));
    int bd = am_px(28);
    SetRect(&g_amBtn[AM_IMGDEL], g_amImg.right - am_px(8) - bd, (g_amImg.top + g_amImg.bottom - bd) / 2,
            g_amImg.right - am_px(8), (g_amImg.top + g_amImg.bottom + bd) / 2);
    y += img;
  }
  SetRect(&g_amField, pad, y, W - pad, y + field);
  int sd = am_px(34), sw = am_busy() ? am_px(82) : sd, ad = am_px(30);
  int cy = (g_amField.top + g_amField.bottom) / 2;
  SetRect(&g_amBtn[AM_SEND], g_amField.right - am_px(9) - sw, cy - sd / 2, g_amField.right - am_px(9), cy + sd / 2);
  SetRect(&g_amBtn[AM_ATTACH], g_amBtn[AM_SEND].left - am_px(4) - ad, cy - ad / 2, g_amBtn[AM_SEND].left - am_px(4),
          cy + ad / 2);
  MoveWindow(g_amIn, g_amField.left + am_px(14), g_amField.top + am_px(9),
             g_amBtn[AM_ATTACH].left - am_px(6) - (g_amField.left + am_px(14)), field - am_px(18), TRUE);
  int H = g_amField.bottom + pad;
  RECT cur;
  GetWindowRect(g_amWnd, &cur);
  if (cur.right - cur.left != W || cur.bottom - cur.top != H) {
    g_amPlacing = TRUE;
    SetWindowPos(g_amWnd, NULL, 0, 0, W, H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    g_amPlacing = FALSE;
    am_place(); /* низ — снова у робота */
  }
  if (g_amCueImg != (g_acImg != NULL)) { /* с картинкой — спросить, что с ней сделать */
    g_amCueImg = g_acImg != NULL;
    SendMessageW(g_amIn, EM_SETCUEBANNER, FALSE,
                 (LPARAM)(g_amCueImg ? L"Что сделать с картинкой? Enter — отправить" : L"Спросите… Enter — отправить"));
  }
  am_clamp();
  const wchar_t *lastText = g_acN ? g_acTurns[g_acN - 1].text : NULL;
  if (g_acN != g_amSeenN || lastText != g_amSeenText) { /* новая реплика или ответ встал на место черновика */
    if (g_acN >= g_amSeenN) am_anchor_last();
    g_amSeenN = g_acN;
    g_amSeenText = lastText;
  }
  am_tips();
  InvalidateRect(g_amWnd, NULL, FALSE);
}

static void am_send(void) {
  if (am_busy()) return;
  int n = GetWindowTextLengthW(g_amIn);
  if (n <= 0) return;
  wchar_t *t = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
  if (!t) return;
  GetWindowTextW(g_amIn, t, n + 1);
  BOOL empty = TRUE;
  for (wchar_t *c = t; *c; c++)
    if (!iswspace(*c)) empty = FALSE;
  if (!empty) {
    if (!g_acWnd) ac_create();
    if (g_acWnd) {
      SetWindowTextW(g_acIn, t);
      SetWindowTextW(g_amIn, L"");
      ac_send(); /* дальше — как из полного чата: модель, поиск, файловые задачи */
      PostMessageW(g_amWnd, WM_AM_SYNC, 0, 0);
    }
  }
  free(t);
  am_layout();
}

/* Черновик вопроса — один на оба окна: при смене окна он переезжает из поля
   from в поле to (там уже что-то есть — дописывается строкой ниже). Поле чата
   выключено, пока ИИ разбирает вопрос, — тогда ничего не трогаем. */
static void am_move_draft(HWND from, HWND to) {
  if (!from || !to || !IsWindowEnabled(from) || !IsWindowEnabled(to)) return;
  int n = GetWindowTextLengthW(from), m = GetWindowTextLengthW(to);
  if (n <= 0) return;
  wchar_t *t = (wchar_t *)malloc(((size_t)n + m + 3) * sizeof(wchar_t));
  if (!t) return;
  int k = 0;
  if (m > 0) {
    GetWindowTextW(to, t, m + 1);
    k = (int)wcslen(t);
    t[k++] = L'\r';
    t[k++] = L'\n';
  }
  GetWindowTextW(from, t + k, n + 1);
  SetWindowTextW(to, t);
  SetWindowTextW(from, L"");
  int end = GetWindowTextLengthW(to);
  SendMessageW(to, EM_SETSEL, (WPARAM)end, (LPARAM)end);
  free(t);
}

/* Мини-чат — лицо беседы, а вопрос вернулся в поле скрытого чата («Стоп», ошибка
   модели, «сначала план»): забираем его сюда, когда чат доделает своё. */
static void am_take_back(void) {
  if (!g_amFace || am_busy()) return;
  am_move_draft(g_acIn, g_amIn);
}

/* «Стоп», «Применить», «Отмена» — те же кнопки полного чата */
static void am_chat_command(int id, HWND btn) {
  if (g_acWnd) SendMessageW(g_acWnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)btn);
}

/* картинка ли это по расширению — иначе в поле ляжет путь (для файловых задач) */
static BOOL am_is_image(const wchar_t *path) {
  static const wchar_t *const kExt[] = {L".png", L".jpg", L".jpeg", L".jpe", L".bmp", L".dib", L".gif",
                                        L".tif", L".tiff", L".webp", L".ico", L".heic"};
  const wchar_t *dot = wcsrchr(path, L'.');
  if (!dot || wcschr(dot, L'\\')) return FALSE;
  for (size_t i = 0; i < sizeof(kExt) / sizeof(kExt[0]); i++)
    if (!_wcsicmp(dot, kExt[i])) return TRUE;
  return FALSE;
}

/* Файлы перетащили в окно или вставили из Проводника: первая картинка
   прикрепляется к вопросу, пути остальных файлов ложатся в поле ввода edit. */
static void am_files_to(HDROP hd, HWND edit) {
  UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
  BOOL took = FALSE;
  for (UINT i = 0; i < n; i++) {
    wchar_t p[MAX_PATH] = L"";
    if (!DragQueryFileW(hd, i, p, MAX_PATH)) continue;
    if (!took && am_is_image(p)) {
      took = TRUE;
      ac_from_file(p);
      continue;
    }
    if (am_is_image(p)) continue; /* к вопросу — одна картинка */
    if (GetWindowTextLengthW(edit) > 0) SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)L" ");
    SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)p);
  }
  SetFocus(edit);
}

static void am_paste_files(HWND owner, HWND edit) {
  if (!OpenClipboard(owner)) return;
  HDROP hd = (HDROP)GetClipboardData(CF_HDROP);
  if (hd) am_files_to(hd, edit); /* буфер владеет данными — DragFinish не нужен */
  CloseClipboard();
}

static void am_pick_file(HWND owner) {
  if (!ac_vision_check()) return;
  wchar_t f[MAX_PATH] = L"";
  OPENFILENAMEW of;
  memset(&of, 0, sizeof(of));
  of.lStructSize = sizeof(of);
  of.hwndOwner = owner;
  of.lpstrFilter = L"Картинки\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0Все файлы\0*.*\0";
  of.lpstrFile = f;
  of.nMaxFile = MAX_PATH;
  of.lpstrTitle = L"Картинка к вопросу";
  of.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&of)) ac_from_file(f);
}

/* снимок области: мини-чат прячется, после выбора — снова у робота */
static void am_shot_done(RECT r) {
  ac_mini_show();
  ac_shot_grab(r);
}

static void am_shot(void) {
  if (!ac_vision_check()) return;
  ac_mini_close();
  Sleep(150);
  g_pickDone = am_shot_done;
  start_ocr_pick();
}

/* меню скрепки над точкой at (экран): картинка из файла, снимок области, из буфера.
   Одно на оба окна (с 2026.10.26 скрепка есть и в полном чате): mini — чьё поле ввода
   и как прятаться ради снимка экрана */
static void am_attach_menu_at(HWND owner, POINT at, BOOL mini) {
  HMENU m = CreatePopupMenu();
  if (!m) return;
  BOOL clip = IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_HDROP);
  AppendMenuW(m, MF_STRING, AM_MENU_FILE, L"Картинка из файла…");
  AppendMenuW(m, MF_STRING, AM_MENU_SHOT, L"Снимок области экрана");
  AppendMenuW(m, MF_STRING | (clip ? 0 : MF_GRAYED), AM_MENU_PASTE, L"Вставить из буфера\tCtrl+V");
  if (g_acImg) {
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, AM_MENU_DROP, L"Убрать картинку");
  }
  int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, at.x, at.y, 0,
                                owner, NULL);
  DestroyMenu(m);
  switch (cmd) {
  case AM_MENU_FILE: am_pick_file(owner); break;
  case AM_MENU_SHOT:
    if (mini) am_shot();
    else ac_shot();
    break;
  case AM_MENU_PASTE:
    if (IsClipboardFormatAvailable(CF_BITMAP)) ac_paste_img();
    else am_paste_files(owner, mini ? g_amIn : g_acIn);
    break;
  case AM_MENU_DROP: ac_drop_img(); break;
  }
}

static void am_attach_menu(void) {
  POINT p = {g_amBtn[AM_ATTACH].left, g_amBtn[AM_ATTACH].top};
  ClientToScreen(g_amWnd, &p);
  am_attach_menu_at(g_amWnd, p, TRUE);
}

/* край окна под точкой x: -1 левый, 1 правый, 0 — нет (ширину тянут за край) */
static int am_edge(int x) {
  RECT rc;
  GetClientRect(g_amWnd, &rc);
  return x < am_px(AM_EDGE) ? -1 : x >= rc.right - am_px(AM_EDGE) ? 1 : 0;
}

static void am_save_width(void) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH], value[32];
  _snwprintf(path, MAX_PATH, L"%s\\ai_avatar.ini", g_dataDir);
  swprintf(value, 32, L"%d", g_amWidth);
  WritePrivateProfileStringW(L"MiniChat", L"Width", value, path);
}

/* правая кнопка по пузырю — «Копировать» его текст целиком, с разметкой */
static void am_copy_menu(int x, int y) {
  POINT p = {x, y};
  if (!PtInRect(&g_amMsgs, p)) return;
  for (int i = 0; i < g_amCount; i++) {
    RECT r = g_amItems[i].r;
    OffsetRect(&r, g_amMsgs.left, g_amTop);
    int turn = g_amItems[i].turn;
    if (turn < 0 || turn >= g_acN || !PtInRect(&r, p)) continue;
    HMENU m = CreatePopupMenu();
    if (!m) return;
    AppendMenuW(m, MF_STRING, AM_MENU_COPY, g_acTurns[turn].assistant ? L"Копировать ответ" : L"Копировать вопрос");
    ClientToScreen(g_amWnd, &p);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, g_amWnd, NULL);
    DestroyMenu(m);
    if (cmd == AM_MENU_COPY && g_acTurns[turn].text) {
      ac_clip(g_acTurns[turn].text);
      ac_status(L"Скопировано");
    }
    return;
  }
}

static int am_hit(int x, int y) {
  POINT p = {x, y};
  for (int b = AM_NEW; b < AM_BTNS; b++) {
    if ((b == AM_APPLY || b == AM_CANCEL) && !g_acPlanOn) continue;
    if (b == AM_IMGDEL && !g_acImg) continue;
    if (b == AM_UNDO && (!ac_new_topic_undo_ready() || !PtInRect(&g_amMsgs, p))) continue;
    if (PtInRect(&g_amBtn[b], p)) return b;
  }
  return AM_NONE;
}

/* заголовок «Размышление ►» или «Шаги ►» под курсором; NULL — нет */
static const AmItem *am_note_at(int x, int y) {
  POINT p = {x, y};
  for (int i = 0; i < g_amCount; i++) {
    const AmItem *it = &g_amItems[i];
    if (it->kind != AM_THINK_HEAD && it->kind != AM_STEPS_HEAD) continue;
    RECT r = it->r;
    OffsetRect(&r, g_amMsgs.left, g_amTop);
    if (PtInRect(&r, p) && PtInRect(&g_amMsgs, p)) return it;
  }
  return NULL;
}

static LRESULT CALLBACK am_in_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  if (m == WM_KEYDOWN && w == VK_RETURN && !shift) {
    PostMessageW(g_amWnd, WM_AM_SEND, 0, 0);
    return 0;
  }
  if (m == WM_CHAR && (w == L'\r' || w == L'\n') && !shift) return 0;
  if (m == WM_KEYDOWN && w == VK_ESCAPE) {
    ac_mini_close();
    return 0;
  }
  if (m == WM_CHAR && w == 27) return 0;
  if (m == WM_CHAR && w == 1) { /* Ctrl+A — выделить всё */
    SendMessageW(h, EM_SETSEL, 0, -1);
    return 0;
  }
  /* Ctrl+V: в буфере картинка (снимок экрана) или файлы из Проводника, а не текст */
  if (m == WM_KEYDOWN && w == 'V' && ctrl && !IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    if (IsClipboardFormatAvailable(CF_BITMAP)) {
      ac_paste_img();
      return 0;
    }
    if (IsClipboardFormatAvailable(CF_HDROP)) {
      am_paste_files(g_amWnd, g_amIn);
      return 0;
    }
  }
  if (m == WM_KEYDOWN && (w == VK_PRIOR || w == VK_NEXT) && !ctrl) { /* листать ленту */
    int page = g_amMsgs.bottom - g_amMsgs.top - am_px(24);
    am_scroll_by(w == VK_PRIOR ? page : -page);
    return 0;
  }
  if (m == WM_SETFOCUS || m == WM_KILLFOCUS) InvalidateRect(g_amWnd, &g_amField, FALSE);
  return CallWindowProcW(g_amInOld, h, m, w, l);
}

static LRESULT CALLBACK AmProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_NCHITTEST: {
    POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &p);
    if (am_edge(p.x)) return HTCLIENT; /* у края — тянуть ширину, не окно */
    if (p.y < am_px(42) && am_hit(p.x, p.y) == AM_NONE) return HTCAPTION; /* за шапку — тянуть */
    return HTCLIENT;
  }
  case WM_NCMOUSEMOVE:
  case WM_MOUSEMOVE: {
    POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (msg == WM_NCMOUSEMOVE) ScreenToClient(hwnd, &p);
    if (g_amSizing && msg == WM_MOUSEMOVE) { /* тянут край: ширина в единицах 96 dpi */
      POINT s = p;
      ClientToScreen(hwnd, &s);
      int w = g_amSizeW0 + (s.x - g_amSizeX0) * g_amSizing * 100 / (g_amK > 0 ? g_amK : 100);
      if (w < AM_W_MIN) w = AM_W_MIN;
      if (w > AM_W_MAX) w = AM_W_MAX;
      if (w != g_amWidth) {
        g_amWidth = w;
        am_layout();
      }
      return 0;
    }
    if (g_amDrag && msg == WM_MOUSEMOVE) { /* тянут ползунок полосы прокрутки */
      RECT th;
      if (am_thumb(&th)) {
        int span = (g_amMsgs.bottom - g_amMsgs.top) - (th.bottom - th.top);
        int topOff = g_amDragTop + (span > 0 ? MulDiv(p.y - g_amDragY, g_amMaxScroll, span) : 0);
        g_amScroll = g_amMaxScroll - topOff;
        am_clamp();
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }
    if (!g_amTrack) {
      TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0};
      g_amTrack = TrackMouseEvent(&t);
    }
    int hot = am_hit(p.x, p.y);
    if (hot != g_amHot) {
      g_amHot = hot;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  }
  case WM_MOUSELEAVE:
    g_amTrack = FALSE;
    if (g_amHot) {
      g_amHot = AM_NONE;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_SETCURSOR: {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(hwnd, &p);
    if (LOWORD(lParam) == HTCLIENT && (g_amSizing || am_edge(p.x))) {
      SetCursor(LoadCursorW(NULL, IDC_SIZEWE));
      return TRUE;
    }
    if (LOWORD(lParam) == HTCLIENT && (am_hit(p.x, p.y) != AM_NONE || am_note_at(p.x, p.y))) {
      SetCursor(LoadCursorW(NULL, IDC_HAND));
      return TRUE;
    }
    break;
  }
  case WM_LBUTTONDOWN: { /* край — ширина; полоса прокрутки: ползунок тянут, мимо — на страницу */
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    if (am_edge(x)) {
      POINT s = {x, y};
      ClientToScreen(hwnd, &s);
      g_amSizing = am_edge(x);
      g_amSizeX0 = s.x;
      g_amSizeW0 = g_amWidth;
      SetCapture(hwnd);
      return 0;
    }
    RECT th;
    if (am_thumb(&th) && x >= g_amMsgs.right - am_px(12) && x < g_amMsgs.right + am_px(6) && y >= g_amMsgs.top &&
        y < g_amMsgs.bottom) {
      if (y >= th.top && y < th.bottom) {
        g_amDrag = TRUE;
        g_amDragY = y;
        g_amDragTop = g_amMaxScroll - g_amScroll;
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
      } else {
        int page = g_amMsgs.bottom - g_amMsgs.top - am_px(24);
        am_scroll_by(y < th.top ? page : -page);
      }
      return 0;
    }
    break;
  }
  case WM_CAPTURECHANGED:
    if (g_amSizing) {
      g_amSizing = 0;
      am_save_width();
    }
    if (g_amDrag) {
      g_amDrag = FALSE;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;
  case WM_LBUTTONUP:
    if (g_amDrag || g_amSizing) {
      ReleaseCapture();
      return 0;
    }
    {
      const AmItem *note = am_note_at(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      if (note) { /* раскрыть или свернуть блок */
        ac_show_toggle(note->kind == AM_STEPS_HEAD);
        return 0;
      }
    }
    switch (am_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
    case AM_NEW: ac_new_topic(); break;
    case AM_UNDO: ac_new_topic_undo(); break;
    case AM_FULL: aichat_show_full(); break; /* мини-чат закроется, черновик переедет */
    case AM_CLOSE: ac_mini_close(); break;
    case AM_ATTACH: am_attach_menu(); break;
    case AM_IMGDEL: ac_drop_img(); break;
    case AM_SEND:
      if (am_busy()) am_chat_command(ID_AC_STOP, g_acStop);
      else am_send();
      break;
    case AM_APPLY: am_chat_command(ID_AC_APPLY, g_acApply); break;
    case AM_CANCEL: am_chat_command(ID_AC_CANCEL, g_acCancel); break;
    default: SetFocus(g_amIn); break;
    }
    return 0;
  case WM_RBUTTONUP:
    am_copy_menu(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    return 0;
  case WM_MOUSEWHEEL:
    am_scroll_by(GET_WHEEL_DELTA_WPARAM(wParam) * am_px(40) / WHEEL_DELTA);
    return 0;
  case WM_DROPFILES:
    am_files_to((HDROP)wParam, g_amIn);
    DragFinish((HDROP)wParam);
    return 0;
  case WM_NOTIFY: {
    NMHDR *nm = (NMHDR *)lParam;
    if (nm && nm->hwndFrom == g_amTip && nm->code == TTN_GETDISPINFOW) {
      ((NMTTDISPINFOW *)lParam)->lpszText = (wchar_t *)am_tip_text((int)nm->idFrom);
      return 0;
    }
    break;
  }
  case WM_MOVE:
    if (!g_amPlacing && IsWindowVisible(hwnd)) { /* окно потянули — робот идёт следом */
      RECT r;
      GetWindowRect(hwnd, &r);
      ac_avatar_follow_panel((g_amLeft ? r.right : r.left) + g_amGap.x, r.bottom + g_amGap.y);
    }
    break;
  case WM_EXITSIZEMOVE: ac_avatar_save_settings(); break;
  case WM_ACTIVATE:
    if (LOWORD(wParam) != WA_INACTIVE) SetFocus(g_amIn);
    return 0;
  case WM_AM_SEND: am_send(); return 0;
  case WM_AM_SYNC: am_take_back(); return 0;
  case WM_TIMER:
    if (wParam == AM_TIMER) {
      if (!am_busy() || !IsWindowVisible(hwnd)) KillTimer(hwnd, AM_TIMER);
      InvalidateRect(hwnd, &g_amMsgs, FALSE);
      return 0;
    }
    break;
  case WM_CTLCOLOREDIT:
    SetBkColor((HDC)wParam, am_bg());
    SetTextColor((HDC)wParam, am_ink());
    return (LRESULT)g_amFieldBr;
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: am_paint(hwnd); return 0;
  case WM_PRINTCLIENT: am_draw(hwnd, (HDC)wParam); return 0;
  case WM_CLOSE: ac_mini_close(); return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* беседа, картинка или строка состояния сменились — перерисовать (и подогнать высоту) */
static void ac_mini_refresh(void) {
  if (!ac_mini_open()) return;
  am_layout();
  if (am_busy()) SetTimer(g_amWnd, AM_TIMER, 120, NULL);
  if (g_amFace) PostMessageW(g_amWnd, WM_AM_SYNC, 0, 0); /* после того, как чат доделает своё */
}

/* строка состояния чата (ac_status) — в шапку мини-чата, без подсказок про «Стоп —»
   и миниатюру полного чата: здесь свои кнопки */
static void ac_mini_status(const wchar_t *t) {
  g_amStatus[0] = 0;
  if (t && t[0]) {
    lstrcpynW(g_amStatus, t, 200);
    wchar_t *cut = wcsstr(g_amStatus, L"Стоп —");
    if (cut && cut != g_amStatus) *cut = 0;
    if ((cut = wcsstr(g_amStatus, L"Клик по миниатюре")) != NULL) *cut = 0;
    for (size_t n = wcslen(g_amStatus); n && wcschr(L" ·—.,", g_amStatus[n - 1]); n--) g_amStatus[n - 1] = 0;
    if (!wcsncmp(g_amStatus, L"Enter", 5)) g_amStatus[0] = 0; /* подсказка полного чата — здесь лишняя */
  }
  if (ac_mini_open()) InvalidateRect(g_amWnd, NULL, FALSE);
}

static void ac_mini_follow(void) {
  if (ac_mini_open() && !g_amPlacing) am_place();
}

/* помощника отозвали — мини-чат закрывается (беседа остаётся в полном чате) */
static void ac_mini_sync(void) {
  if (!ac_avatar_here()) ac_mini_close();
}

/* шрифты — гарнитура темы (g_faceBody, как у окон блокнота), размеры свои */
static void am_fonts(void) {
  HFONT old[3] = {g_amFont, g_amBold, g_amSmall};
  g_amFont = CreateFontW(-am_px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, g_faceBody);
  g_amBold = CreateFontW(-am_px(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, g_faceBody);
  g_amSmall = CreateFontW(-am_px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, g_faceBody);
  if (g_amIn) SendMessageW(g_amIn, WM_SETFONT, (WPARAM)g_amFont, TRUE);
  for (int i = 0; i < 3; i++)
    if (old[i]) DeleteObject(old[i]);
  if (g_amMono) { /* цифры норм — под новый размер, при первом рисовании */
    DeleteObject(g_amMono);
    g_amMono = NULL;
  }
}

/* кисти и кромка окна — в цветах темы; углы — как у окон темы (ui_theme.c) */
static void am_chrome(void) {
  if (g_amBgBr) DeleteObject(g_amBgBr);
  if (g_amFieldBr) DeleteObject(g_amFieldBr);
  g_amBgBr = CreateSolidBrush(am_bg());
  g_amFieldBr = CreateSolidBrush(am_bg());
  if (!g_amWnd) return;
  ui_dwm_set(g_amWnd, UI_DWMWA_CORNER, (DWORD)kThemes[g_theme >= 0 && g_theme < THEME_COUNT ? g_theme : 0].corners);
  ui_dwm_set(g_amWnd, 34, am_line()); /* DWMWA_BORDER_COLOR: тонкая кромка в цвет линий темы */
}

/* тема сменилась (apply_theme → ac_retheme) */
static void am_retheme(void) {
  if (!g_amWnd) return;
  am_fonts();
  am_chrome();
  am_layout();
  InvalidateRect(g_amWnd, NULL, TRUE);
}

static void ac_mini_show(void) {
  if (!g_acWnd) ac_create(); /* беседа живёт в окне чата, оно может оставаться скрытым */
  if (!g_acWnd) return;
  if (!g_amWnd) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = AmProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"CursorPadMiniChat";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    g_amK = MulDiv(100, ui_dpi(NULL), 96);
    am_fonts();
    if (g_dataDir[0]) { /* ширина, которую вытянули в прошлый раз */
      wchar_t path[MAX_PATH];
      _snwprintf(path, MAX_PATH, L"%s\\ai_avatar.ini", g_dataDir);
      g_amWidth = GetPrivateProfileIntW(L"MiniChat", L"Width", AM_W_DEFAULT, path);
      if (g_amWidth < AM_W_MIN) g_amWidth = AM_W_MIN;
      if (g_amWidth > AM_W_MAX) g_amWidth = AM_W_MAX;
    }
    g_amWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_ACCEPTFILES, L"CursorPadMiniChat", L"Мини-ИИ",
                              WS_POPUP | WS_CLIPCHILDREN, 0, 0, am_px(g_amWidth), am_px(240), NULL, NULL, g_inst, NULL);
    if (!g_amWnd) return;
    am_chrome();
    g_amIn = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                             0, 0, 10, 10, g_amWnd, (HMENU)(INT_PTR)ID_AM_IN, g_inst, NULL);
    SendMessageW(g_amIn, WM_SETFONT, (WPARAM)g_amFont, TRUE);
    SendMessageW(g_amIn, EM_SETCUEBANNER, FALSE, (LPARAM)L"Спросите… Enter — отправить");
    g_amInOld = (WNDPROC)SetWindowLongPtrW(g_amIn, GWLP_WNDPROC, (LONG_PTR)am_in_proc);
    g_amTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_amWnd, NULL, g_inst, NULL);
    if (g_amTip) {
      SendMessageW(g_amTip, TTM_SETMAXTIPWIDTH, 0, am_px(300));
      for (int b = AM_NEW; b <= AM_TIP_STATUS; b++) {
        if (b > AM_IMGDEL && b != AM_TIP_STATUS) continue;
        TOOLINFOW ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = g_amWnd;
        ti.uId = (UINT_PTR)b;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(g_amTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
      }
    }
  }
  BOOL was = IsWindowVisible(g_amWnd);
  g_amSeenN = -1; /* открыли — последний длинный ответ с начала */
  /* у беседы одно окно: полный чат прячется, его черновик переезжает сюда */
  if (IsWindowVisible(g_acWnd)) ShowWindow(g_acWnd, SW_HIDE);
  g_amFace = TRUE;
  am_move_draft(g_acIn, g_amIn);
  am_layout(); /* размер и место — до показа, чтобы не мигнуло в углу экрана */
  am_place();
  ShowWindow(g_amWnd, SW_SHOWNOACTIVATE);
  SetForegroundWindow(g_amWnd);
  SetFocus(g_amIn);
  ac_bubble_hide(TRUE);
  if (!was && !am_busy()) ac_avatar_set(AC_AV_LISTENING);
  if (am_busy()) SetTimer(g_amWnd, AM_TIMER, 120, NULL);
  if (!am_busy() && !ai_ready()) ac_status(ai_status_text(g_aiOn ? AI_NO_MODEL : AI_OFF));
}
