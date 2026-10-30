/* Вид кольца Shift-перетаскивания (с 2026.10.24, стили и стекло — с 2026.10.26).
   Своё полупрозрачное окно (UpdateLayeredWindow): плитки — «стекло» поверх
   размытого снимка экрана, по контуру каждой — неяркие точки, у плитки под
   курсором они бегут; плитка окрашивается акцентом и выдвигается наружу.
   В центре — карточка файла, а при наведении — что сделает действие.
   «Ссылка» задержана — снаружи вокруг кольца выезжает дуга из двух сегментов
   («Включая файл» выше, «До файла» ниже), в том же окне, что и кольцо.
   Стиль (kCvStyles: «Матовое стекло», «Лепестки», «Тёмное стекло», «Тонкое
   кольцо»), размытие, точки и анимации выбираются на вкладке «Кастомизация»
   Настроек (g_ringStyle, g_ringBlur, g_ringDots, g_ringAnim).
   Пока проводник не отдал список файлов, по плиткам бежит огонёк.
   Подключается из convert.c после его переменных; рисует только по ним. */
#ifdef CV_RING_TEST
/* проверочные сборки без ui_theme.c и Настроек: светлая тема и стиль по умолчанию */
typedef struct { COLORREF bg, bg2, surface, ink, muted, line, accent; } UiTokens;
static UiTokens g_uiTok = {RGB(255, 255, 255), RGB(243, 244, 246), RGB(255, 255, 255), RGB(17, 24, 39),
                           RGB(107, 114, 128), RGB(226, 229, 233), RGB(37, 99, 235)};
static int g_ringStyle;
static BOOL g_ringBlur = TRUE, g_ringDots = TRUE, g_ringAnim = TRUE;
#endif

#include <commctrl.h> /* ImageList_GetIcon */

#define CV_RING_BASE 360 /* диаметр кольца при 96 dpi */
#define CV_WIN_BASE 460  /* сторона окна: вокруг кольца место под дугу «Ссылки» и тень */
enum { CV_T_IMAGE, CV_T_PDF, CV_T_TEXT, CV_T_ZIP, CV_T_MORE };
/* shell — образец имени для значка из Проводника (кольцо «Создать»); folder — значок папки */
typedef struct { wchar_t icon; const wchar_t *label, *title, *note; int tint; const wchar_t *shell; BOOL folder; } CvRingItem;
/* по номерам действий: 0–3 картинки, 4 PDF, 5 TXT, 6 CSV, 7 ZIP, 8–14 окно параметров, 15 «Ещё»,
   24 — «Ссылка», 25 и 26 — сегменты её дуги вокруг кольца: «Включая файл» и «До файла» (file_link_ui.c),
   27 — пятая плитка кольца «Создать»: бланк «Карта заказа»,
   28 и 29 — та же дуга у преобразований: «Копия рядом» и «Подмена» (исходник удаляется) */
static const CvRingItem kCvRing[30] = {
    {0xEB9F, L"PNG", L"В PNG", L"копия рядом; задержите — подмена", CV_T_IMAGE},
    {0xEB9F, L"JPG", L"В JPG", L"копия рядом; задержите — подмена", CV_T_IMAGE},
    {0xEB9F, L"BMP", L"В BMP", L"копия рядом; задержите — подмена", CV_T_IMAGE},
    {0xEB9F, L"TIFF", L"В TIFF", L"копия рядом; задержите — подмена", CV_T_IMAGE},
    {0xEA90, L"PDF", L"В PDF", L"копия рядом; задержите — подмена", CV_T_PDF},
    {0xE8A5, L"TXT", L"Word → TXT", L"текст рядом с исходником", CV_T_TEXT},
    {0xE80A, L"CSV", L"Excel → CSV", L"первый лист, рядом", CV_T_TEXT},
    {0xE7B8, L"ZIP", L"Собрать ZIP", L"архив рядом с исходником", CV_T_ZIP},
    {0xE740, L"Размер", L"Изменить размер", L"откроет окно параметров", CV_T_IMAGE},
    {0xE73F, L"Сжать", L"Сжать до лимита", L"откроет окно параметров", CV_T_IMAGE},
    {0xE8C8, L"Собрать", L"Объединить PDF", L"сразу, порядок как в выделении", CV_T_PDF},
    {0xE7C3, L"Страницы", L"Извлечь страницы", L"откроет окно параметров", CV_T_PDF},
    {0xE8C6, L"Разделить", L"Разделить PDF", L"откроет окно параметров", CV_T_PDF},
    {0xE7AD, L"Повернуть", L"Повернуть страницы", L"откроет окно параметров", CV_T_PDF},
    {0xE8F1, L"Один PDF", L"Всё в один PDF", L"сразу, порядок как в выделении", CV_T_PDF},
    {0xE712, L"Ещё", L"Все действия", L"окно обработки файлов", CV_T_MORE},
    {0, NULL, NULL, NULL, 0}, {0, NULL, NULL, NULL, 0}, /* 16, 17 — отмена и «показать» истории */
    {0xE8C6, L"Разделить", L"Разделить TIFF", L"каждая страница — свой TIFF", CV_T_IMAGE},
    {0xE7C4, L"Один TIFF", L"Один TIFF", L"сразу, порядок как в выделении", CV_T_IMAGE},
    /* 20–23 — кольцо «Создать» на рабочем столе или в папке Проводника */
    {0xE8B7, L"Папка", L"Новая папка", L"отпустите — появится здесь", CV_T_ZIP, L"Новая папка", TRUE},
    {0xE8A5, L"TXT", L"Текстовый файл", L"отпустите — появится здесь", CV_T_MORE, L"a.txt", FALSE},
    {0xE8A5, L"Word", L"Документ Word", L"отпустите — появится здесь", CV_T_TEXT, L"a.docx", FALSE},
    {0xE80A, L"Excel", L"Книга Excel", L"отпустите — появится здесь", CV_T_TEXT, L"a.xlsx", FALSE},
    {0xE71B, L"Ссылка", L"Ссылка", L"задержите: путь к файлу или к папке", CV_T_ZIP},
    {0xE8A5, L"Включая файл", L"Включая файл", L"путь с именем файла — в буфер", CV_T_ZIP},
    {0xE8B7, L"До файла", L"До файла", L"папка, где лежит файл, — в буфер", CV_T_ZIP},
    {0xE8A5, L"Карта заказа", L"Карта заказа", L"бланк ТИ.21-0025 — появится здесь", CV_T_TEXT, L"a.docx", FALSE},
    {0xE8C8, L"Копия рядом", L"Копия рядом", L"исходник остаётся", CV_T_ZIP},
    {0xE8AB, L"Подмена", L"Подмена", L"старый файл удаляется", CV_T_ZIP}};
static const COLORREF kCvTint[] = {RGB(8, 145, 178), RGB(220, 38, 38), RGB(22, 163, 74), RGB(217, 119, 6),
                                   RGB(100, 116, 139)};

/* ---- стили панели (вкладка «Кастомизация») ---------------------------------
   Размеры — в px при 96 dpi, доли — 0…1. tileA/plateA/hubA — сколько тона
   стекла поверх размытого снимка (0 — чистое размытие). */
#define CV_STYLES 4
typedef struct {
  const wchar_t *name;
  float rinK;            /* внутренний радиус плиток — доля внешнего */
  float hubGap;          /* зазор между центром и плитками */
  float gap, round, pop; /* зазор между плитками, скругление, выдвижение под мышью */
  float inset;           /* отступ плиток от края круга */
  float plateA, tileA, hubA; /* тон стекла: подложка-круг, плитка, центр */
  float blur;            /* радиус размытия снимка */
  BOOL dark;             /* тёмное стекло — свои цвета поверх любой темы */
  float edgeA;           /* кромка плитки */
  float dotA, dotStep, dotR; /* точки по контуру: яркость, шаг, радиус */
  float shadowA;
  float arcGap, arcW, arcRound; /* дуга «Ссылки» снаружи кольца */
  BOOL halo;             /* пунктирная орбита вокруг кольца, пока открыта дуга */
  int appear;            /* 0 — рост от 90 % с волной по плиткам, 1 — «цветение» из центра */
} CvStyle;
static const CvStyle kCvStyles[CV_STYLES] = {
    {L"Матовое стекло", 0.50f, 6, 5, 8, 7, 6, 0.34f, 0.62f, 0.76f, 18, FALSE, 0.85f, 0.55f, 7, 1.1f, 0.22f, 9, 46, 9, FALSE, 0},
    {L"Лепестки", 0.545f, 16, 11, 16, 6, 0, 0.0f, 0.60f, 0.72f, 16, FALSE, 0.90f, 0.55f, 8, 1.1f, 0.30f, 12, 46, 16, FALSE, 1},
    {L"Тёмное стекло", 0.50f, 6, 4, 7, 7, 6, 0.30f, 0.58f, 0.70f, 20, TRUE, 0.22f, 0.75f, 6, 1.0f, 0.45f, 8, 46, 8, FALSE, 0},
    {L"Тонкое кольцо", 0.67f, 6, 2, 5, 14, 2, 0.0f, 0.64f, 0.30f, 14, FALSE, 0.16f, 0.85f, 6, 1.0f, 0.20f, 7, 40, 6, TRUE, 0}};
static const CvStyle *cv_style(void) { return &kCvStyles[g_ringStyle >= 0 && g_ringStyle < CV_STYLES ? g_ringStyle : 0]; }

static int g_cvDpi = 96;
static DWORD g_cvShownAt;   /* начало появления */
static float g_cvLevel[16]; /* подсветка плиток 0…1, плавно к цели */
static BOOL g_cvInHub;      /* курсор в центре — отпускание ничего не делает */
static POINT g_cvAt;        /* левый верхний угол окна на экране */
/* дуга вокруг кольца у плитки g_cvSubTile: у «Ссылки» 0 — «Включая файл» (выше), 1 — «До файла» (ниже);
   у преобразований 0 — «Копия рядом», 1 — «Подмена» */
static BOOL g_cvSubOn;
static int g_cvSubTile = -1, g_cvSubHover = -1; /* g_cvSubTile — плитка, у которой открыта дуга (по умолчанию «Ссылка») */
/* пункт kCvRing сегмента k открытой дуги */
static int cv_sub_item(int k) {
  BOOL link = g_cvSubTile < 0 || g_cvSubTile >= g_cvCount || g_cvFormats[g_cvSubTile] == 24;
  return (link ? 25 : 28) + k;
}
static float g_cvSubLevel[2];
static DWORD g_cvSubShownAt;
static HBITMAP g_cvBmp, g_cvTextBmp;
static BYTE *g_cvPix, *g_cvTextPix; /* premultiplied BGRA; второй — под текст */
static int g_cvBmpSize, g_cvFontSize;
enum { CV_F_LABEL, CV_F_SMALL, CV_F_TITLE, CV_F_BADGE, CV_F_ICON, CV_F_ICON_BIG, CV_F_N };
static HFONT g_cvFont[CV_F_N];

typedef struct { float c, R, rin, rhub, pop, s, gap, round, inset; } CvGeo;
/* size — диаметр кольца; центр — середина окна g_cvWin; scale — анимация появления (попадания при 1) */
static CvGeo cv_geo(int size, float scale) {
  const CvStyle *st = cv_style();
  CvGeo g;
  g.s = (float)size / CV_RING_BASE;
  g.c = (g_cvWin > 0 ? g_cvWin : size * CV_WIN_BASE / CV_RING_BASE) * 0.5f;
  g.R = (size * 0.5f - 22.0f * g.s) * scale;
  g.rin = g.R * st->rinK;
  g.rhub = g.rin - st->hubGap * g.s;
  g.pop = st->pop * g.s;
  g.gap = st->gap * g.s;
  g.round = st->round * g.s;
  g.inset = st->inset * g.s;
  return g;
}

static float cv_clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
static float cv_ease(float t) { t = cv_clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t); }
static COLORREF cv_mix(COLORREF a, COLORREF b, float t) {
  t = cv_clamp01(t);
  return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
             (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}
/* цвет c с покрытием a поверх premultiplied-пикселя */
static void cv_px(BYTE *p, COLORREF c, float a) {
  if (a <= 0.0f) return;
  if (a > 1.0f) a = 1.0f;
  float k = 1.0f - a;
  p[0] = (BYTE)(GetBValue(c) * a + p[0] * k + 0.5f);
  p[1] = (BYTE)(GetGValue(c) * a + p[1] * k + 0.5f);
  p[2] = (BYTE)(GetRValue(c) * a + p[2] * k + 0.5f);
  p[3] = (BYTE)(255.0f * a + p[3] * k + 0.5f);
}
static int cv_luma(COLORREF c) { return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000; }
/* тёмный кадр: тёмное стекло или тёмная тема — тени гуще, точки ярче */
static BOOL cv_dark(void) { return cv_style()->dark || cv_luma(g_uiTok.surface) < 128; }
/* чем писать по акценту: светлый акцент тёмной темы — тёмным, остальные — белым */
static COLORREF cv_on_accent(void) { return cv_luma(g_uiTok.accent) > 150 ? RGB(17, 24, 39) : RGB(255, 255, 255); }
/* палитра кадра: у тёмного стекла своя, остальные — из темы */
typedef struct { COLORREF tint, ink, muted, accent, on, edge; } CvPal;
static CvPal cv_pal(void) {
  const CvStyle *st = cv_style();
  CvPal p;
  if (st->dark) {
    p.tint = RGB(18, 22, 30); p.ink = RGB(243, 244, 246); p.muted = RGB(180, 186, 196);
    p.accent = RGB(96, 165, 250); p.on = RGB(255, 255, 255); p.edge = RGB(255, 255, 255);
  } else {
    p.tint = g_uiTok.surface; p.ink = g_uiTok.ink; p.muted = g_uiTok.muted;
    p.accent = g_uiTok.accent; p.on = cv_on_accent();
    p.edge = RGB(255, 255, 255);
  }
  return p;
}

static int CALLBACK cv_face_cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp) {
  (void)lf; (void)tm; (void)type;
  *(BOOL *)lp = TRUE;
  return 0;
}
static BOOL cv_face(const wchar_t *face) {
  LOGFONTW lf;
  memset(&lf, 0, sizeof(lf));
  lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
  BOOL found = FALSE;
  HDC dc = GetDC(NULL);
  if (dc) { EnumFontFamiliesExW(dc, &lf, cv_face_cb, (LPARAM)&found, 0); ReleaseDC(NULL, dc); }
  return found;
}
/* шрифты под размер кольца; сглаживание без ClearType — у прозрачного окна нет своего фона */
static void cv_fonts(int size) {
  if (g_cvFontSize == size && g_cvFont[CV_F_LABEL]) return;
  for (int i = 0; i < CV_F_N; i++) if (g_cvFont[i]) { DeleteObject(g_cvFont[i]); g_cvFont[i] = NULL; }
  g_cvFontSize = size;
  static int text = -1, icons = -1;
  if (text < 0) text = cv_face(L"Segoe UI Variable Text") ? 1 : 0;
  if (icons < 0) icons = cv_face(L"Segoe Fluent Icons") ? 1 : cv_face(L"Segoe MDL2 Assets") ? 2 : 0;
  const wchar_t *face = text ? L"Segoe UI Variable Text" : L"Segoe UI";
  float s = (float)size / CV_RING_BASE;
  static const int px[CV_F_N] = {12, 11, 15, 10, 20, 30}, weight[CV_F_N] = {FW_SEMIBOLD, FW_NORMAL, FW_SEMIBOLD, FW_BOLD, FW_NORMAL, FW_NORMAL};
  for (int i = 0; i < CV_F_N; i++) {
    BOOL icon = i == CV_F_ICON || i == CV_F_ICON_BIG;
    if (icon && !icons) continue;
    g_cvFont[i] = CreateFontW(-(int)(px[i] * s + 0.5f), 0, 0, 0, weight[i], FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                              icon ? (icons == 1 ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets") : face);
  }
}

/* маска текста из g_cvTextPix (белым по чёрному) — цветом col с покрытием alpha */
static void cv_text_apply(BYTE *pix, int size, RECT r, COLORREF col, float alpha) {
  for (int y = r.top; y < r.bottom; y++)
    for (int x = r.left; x < r.right; x++) {
      const BYTE *t = g_cvTextPix + ((size_t)y * size + x) * 4;
      BYTE m = t[0] > t[1] ? t[0] : t[1];
      if (t[2] > m) m = t[2];
      if (m) cv_px(pix + ((size_t)y * size + x) * 4, col, m / 255.0f * alpha);
    }
}
static BOOL cv_clip(RECT *r, int size) {
  if (r->left < 0) r->left = 0;
  if (r->top < 0) r->top = 0;
  if (r->right > size) r->right = size;
  if (r->bottom > size) r->bottom = size;
  return r->right > r->left && r->bottom > r->top;
}
/* текст цветом col: белым по чёрному в g_cvTextPix, яркость — покрытие */
static void cv_text(BYTE *pix, int size, HDC dc, int font, const wchar_t *s, int n, RECT r, UINT fmt, COLORREF col, float alpha) {
  if (!g_cvFont[font] || !s || !*s || !cv_clip(&r, size)) return;
  for (int y = r.top; y < r.bottom; y++) memset(g_cvTextPix + ((size_t)y * size + r.left) * 4, 0, (size_t)(r.right - r.left) * 4);
  SelectObject(dc, g_cvFont[font]);
  DrawTextW(dc, s, n, &r, fmt | DT_NOPREFIX);
  GdiFlush();
  cv_text_apply(pix, size, r, col, alpha);
}
/* текст вдоль дуги: повёрнут так, что читается по направлению ang (градусы, по часовой на экране),
   центр строки — в (cx, cy) */
static void cv_text_rot(BYTE *pix, int size, HDC dc, int font, const wchar_t *s, float cx, float cy, float ang, COLORREF col, float alpha) {
  if (!g_cvFont[font] || !s || !*s) return;
  LOGFONTW lf;
  if (!GetObjectW(g_cvFont[font], sizeof(lf), &lf)) return;
  int esc = (int)(-ang * 10.0f) % 3600;
  if (esc < 0) esc += 3600;
  lf.lfEscapement = lf.lfOrientation = esc;
  HFONT rot = CreateFontIndirectW(&lf);
  if (!rot) return;
  SelectObject(dc, g_cvFont[font]);
  SIZE ext = {0, 0};
  GetTextExtentPoint32W(dc, s, lstrlenW(s), &ext);
  int hb = (ext.cx + ext.cy) / 2 + 6;
  RECT r = {(int)cx - hb, (int)cy - hb, (int)cx + hb + 1, (int)cy + hb + 1};
  if (cv_clip(&r, size)) {
    for (int y = r.top; y < r.bottom; y++) memset(g_cvTextPix + ((size_t)y * size + r.left) * 4, 0, (size_t)(r.right - r.left) * 4);
    float a = ang * 0.017453292f, ux = sinf(a), uy = -cosf(a); /* «верх» букв */
    float off = ext.cy * 0.36f; /* базовая линия — ниже центра строки */
    HGDIOBJ old = SelectObject(dc, rot);
    UINT align = SetTextAlign(dc, TA_CENTER | TA_BASELINE | TA_NOUPDATECP);
    TextOutW(dc, (int)(cx - ux * off + 0.5f), (int)(cy - uy * off + 0.5f), s, lstrlenW(s));
    SetTextAlign(dc, align);
    SelectObject(dc, old);
    GdiFlush();
    cv_text_apply(pix, size, r, col, alpha);
  }
  DeleteObject(rot);
}
static RECT cv_box(float cx, float cy, float w, float h) {
  RECT r = {(int)(cx - w / 2), (int)(cy - h / 2), (int)(cx + w / 2 + 0.5f), (int)(cy + h / 2 + 0.5f)};
  return r;
}

/* скруглённый прямоугольник со сглаженным краем (значок файла в центре) */
static void cv_rrect(BYTE *pix, int size, float x0, float y0, float x1, float y1, float rad, COLORREF c, float a) {
  float hx = (x1 - x0) / 2, hy = (y1 - y0) / 2, cx = x0 + hx, cy = y0 + hy;
  for (int y = (int)y0 - 1; y <= (int)y1 + 1; y++)
    for (int x = (int)x0 - 1; x <= (int)x1 + 1; x++) {
      if (x < 0 || y < 0 || x >= size || y >= size) continue;
      float qx = fabsf(x + 0.5f - cx) - hx + rad, qy = fabsf(y + 0.5f - cy) - hy + rad;
      float d = sqrtf((qx > 0 ? qx * qx : 0) + (qy > 0 ? qy * qy : 0)) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - rad;
      cv_px(pix + ((size_t)y * size + x) * 4, c, cv_clamp01(0.5f - d) * a);
    }
}
/* кружок со сглаженным краем — точка по контуру плитки */
static void cv_disc(BYTE *pix, int size, float cx, float cy, float rad, COLORREF c, float a) {
  if (a <= 0.003f) return;
  int x0 = (int)(cx - rad - 1), y0 = (int)(cy - rad - 1), x1 = (int)(cx + rad + 1), y1 = (int)(cy + rad + 1);
  for (int y = y0; y <= y1; y++) {
    if (y < 0 || y >= size) continue;
    for (int x = x0; x <= x1; x++) {
      if (x < 0 || x >= size) continue;
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      cv_px(pix + ((size_t)y * size + x) * 4, c, cv_clamp01(0.5f + rad - sqrtf(dx * dx + dy * dy)) * a);
    }
  }
}

/* ---- значки типов файлов — те же, что в Проводнике --------------------------
   Из системного списка значков (SHGetImageList) по расширению: файл не читается
   (SHGFI_USEFILEATTRIBUTES), поэтому быстро и для ещё не созданных файлов
   (меню «Создать на рабочем столе»). Результат — DIB px×px, premultiplied BGRA:
   так его берут и меню (hbmpItem), и прозрачное окно кольца. */
static const GUID kCvIID_ImageList = {0x46eb5926, 0x582e, 0x4017, {0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50}};
static HBITMAP cv_icon_bitmap(const wchar_t *path, BOOL folder, int px, BYTE **pixels) {
  SHFILEINFOW info;
  memset(&info, 0, sizeof(info));
  if (px < 8 || px > 512 ||
      !SHGetFileInfoW(path, folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES))
    return NULL;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = px;
  bi.bmiHeader.biHeight = -px;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  BYTE *bits = NULL;
  HBITMAP bitmap = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
  HDC dc = bitmap ? CreateCompatibleDC(NULL) : NULL;
  if (!dc) {
    if (bitmap) DeleteObject(bitmap);
    return NULL;
  }
  HGDIOBJ old = SelectObject(dc, bitmap);
  /* размер списка — ближайший не меньше; «огромный» список у части типов держит лишь 48 px в углу — тогда 48, растянутый */
  int lists[2] = {px <= 16 ? SHIL_SMALL : px <= 32 ? SHIL_LARGE : px <= 48 ? SHIL_EXTRALARGE : SHIL_JUMBO, SHIL_EXTRALARGE};
  BOOL ok = FALSE;
  for (int attempt = 0; attempt < 2 && !ok; attempt++) {
    memset(bits, 0, (size_t)px * px * 4);
    IUnknown *images = NULL;
    if (FAILED(SHGetImageList(lists[attempt], &kCvIID_ImageList, (void **)&images)) || !images) continue;
    HICON icon = ImageList_GetIcon((HIMAGELIST)images, info.iIcon, ILD_TRANSPARENT);
    images->lpVtbl->Release(images);
    if (!icon) continue;
    DrawIconEx(dc, 0, 0, icon, px, px, 0, NULL, DI_NORMAL);
    GdiFlush();
    DestroyIcon(icon);
    int right = -1, bottom = -1, alpha = 0;
    for (int y = 0; y < px; y++)
      for (int x = 0; x < px; x++)
        if (bits[((size_t)y * px + x) * 4 + 3]) { alpha = 1; if (x > right) right = x; if (y > bottom) bottom = y; }
    if (!alpha) /* старый значок без альфы: непрозрачно там, где есть цвет */
      for (size_t i = 0; i < (size_t)px * px; i++) {
        BYTE *p = bits + i * 4;
        if (p[0] | p[1] | p[2]) { p[3] = 255; alpha = 1; right = px - 1; bottom = px - 1; }
      }
    ok = alpha && (lists[attempt] != SHIL_JUMBO || (right >= px * 6 / 10 && bottom >= px * 6 / 10));
  }
  SelectObject(dc, old);
  DeleteDC(dc);
  if (!ok) {
    DeleteObject(bitmap);
    return NULL;
  }
  if (pixels) *pixels = bits;
  return bitmap;
}
/* премультиплицированный значок поверх кадра, с общей прозрачностью alpha */
static void cv_blit(BYTE *pix, int size, const BYTE *src, int w, int h, int x0, int y0, float alpha) {
  for (int y = 0; y < h; y++) {
    if (y0 + y < 0 || y0 + y >= size) continue;
    for (int x = 0; x < w; x++) {
      if (x0 + x < 0 || x0 + x >= size) continue;
      const BYTE *s = src + ((size_t)y * w + x) * 4;
      if (!s[3]) continue;
      BYTE *d = pix + ((size_t)(y0 + y) * size + x0 + x) * 4;
      float a = s[3] / 255.0f * alpha, k = 1.0f - a;
      for (int c = 0; c < 3; c++) d[c] = (BYTE)(s[c] * alpha + d[c] * k + 0.5f);
      d[3] = (BYTE)(255.0f * a + d[3] * k + 0.5f);
    }
  }
}
static HBITMAP g_cvIconBmp; /* значок центра — до смены расширения или размера */
static BYTE *g_cvIconPix;
static wchar_t g_cvIconExt[16];
static int g_cvIconPx;
static const BYTE *cv_hub_icon(const wchar_t *path, int px) {
  const wchar_t *ext = cv_extension(path);
  if (g_cvIconPx == px && !_wcsicmp(g_cvIconExt, ext)) return g_cvIconPix;
  if (g_cvIconBmp) DeleteObject(g_cvIconBmp);
  g_cvIconPix = NULL;
  g_cvIconBmp = cv_icon_bitmap(path, FALSE, px, &g_cvIconPix);
  lstrcpynW(g_cvIconExt, ext, 16);
  g_cvIconPx = px;
  return g_cvIconBmp ? g_cvIconPix : NULL;
}

/* ---- миниатюра первого файла: сам чертёж или картинка, как в Проводнике ----
   Достаёт IShellItemImageFactory в своём потоке: несжатый TIFF A0 читается
   заметное время, а перетаскивание ждать не должно. Пока миниатюры нет — значок
   типа. Готовая приходит сообщением WM_CV_THUMB окну кольца. */
#define WM_CV_THUMB (WM_APP + 143)
static const GUID kCvIID_ImageFactory = {0xbcc18b79, 0xba16, 0x442f, {0x80, 0xc4, 0x8a, 0x59, 0xc3, 0x0c, 0x46, 0x3b}};
typedef struct { wchar_t path[MAX_PATH]; int px, w, h; BYTE *pixels; } CvThumb;
static CvThumb *g_cvThumb;            /* готовая, premultiplied BGRA w×h */
static wchar_t g_cvThumbWant[MAX_PATH]; /* для какого файла уже просили */
static volatile LONG g_cvThumbBusy;
static DWORD WINAPI cv_thumb_thread(void *parameter) {
  CvThumb *t = (CvThumb *)parameter;
  if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
    IShellItemImageFactory *factory = NULL;
    HBITMAP bitmap = NULL;
    if (SUCCEEDED(SHCreateItemFromParsingName(t->path, NULL, &kCvIID_ImageFactory, (void **)&factory)) && factory) {
      SIZE want = {t->px, t->px};
      factory->lpVtbl->GetImage(factory, want, SIIGBF_THUMBNAILONLY, &bitmap);
      factory->lpVtbl->Release(factory);
    }
    BITMAP info;
    if (bitmap && GetObjectW(bitmap, sizeof(info), &info) && info.bmWidth > 0 && info.bmHeight > 0 && info.bmWidth <= 1024 &&
        info.bmHeight <= 1024 && (t->pixels = (BYTE *)malloc((size_t)info.bmWidth * info.bmHeight * 4)) != NULL) {
      BITMAPINFO bi;
      memset(&bi, 0, sizeof(bi));
      bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
      bi.bmiHeader.biWidth = t->w = info.bmWidth;
      bi.bmiHeader.biHeight = -(t->h = info.bmHeight);
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      HDC dc = GetDC(NULL);
      BOOL read = GetDIBits(dc, bitmap, 0, (UINT)t->h, t->pixels, &bi, DIB_RGB_COLORS) == t->h;
      ReleaseDC(NULL, dc);
      size_t n = (size_t)t->w * t->h;
      BOOL alpha = FALSE, straight = FALSE;
      for (size_t i = 0; read && i < n; i++) {
        BYTE *p = t->pixels + i * 4;
        if (p[3]) alpha = TRUE;
        if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) straight = TRUE;
      }
      for (size_t i = 0; read && i < n; i++) { /* без альфы — непрозрачная; прямая альфа — домножить */
        BYTE *p = t->pixels + i * 4;
        if (!alpha) p[3] = 255;
        else if (straight)
          for (int c = 0; c < 3; c++) p[c] = (BYTE)(p[c] * p[3] / 255);
      }
      /* Shell может отдать квадрат с прозрачными полями — оставить только саму картинку */
      int left = t->w, top = t->h, right = -1, bottom = -1;
      for (int y = 0; read && y < t->h; y++)
        for (int x = 0; x < t->w; x++)
          if (t->pixels[((size_t)y * t->w + x) * 4 + 3]) {
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
          }
      if (read && right >= left && (left > 0 || top > 0 || right < t->w - 1 || bottom < t->h - 1)) {
        int w = right - left + 1, h = bottom - top + 1;
        for (int y = 0; y < h; y++) memmove(t->pixels + (size_t)y * w * 4, t->pixels + ((size_t)(y + top) * t->w + left) * 4, (size_t)w * 4);
        t->w = w, t->h = h;
      }
      if (!read || right < left) {
        free(t->pixels);
        t->pixels = NULL;
      }
    }
    if (bitmap) DeleteObject(bitmap);
    CoUninitialize();
  }
  if (!t->pixels || !g_cvWnd || !PostMessageW(g_cvWnd, WM_CV_THUMB, 0, (LPARAM)t)) {
    free(t->pixels);
    free(t);
  }
  InterlockedExchange(&g_cvThumbBusy, 0);
  return 0;
}
static void cv_thumb_request(const wchar_t *path, int px) {
  if (!path[0] || !_wcsicmp(g_cvThumbWant, path) || InterlockedCompareExchange(&g_cvThumbBusy, 1, 0)) return;
  CvThumb *t = (CvThumb *)calloc(1, sizeof(*t));
  HANDLE thread = NULL;
  if (t) {
    lstrcpynW(t->path, path, MAX_PATH);
    t->px = px;
    thread = CreateThread(NULL, 0, cv_thumb_thread, t, 0, NULL);
  }
  if (thread) {
    CloseHandle(thread);
    lstrcpynW(g_cvThumbWant, path, MAX_PATH);
  } else {
    free(t);
    InterlockedExchange(&g_cvThumbBusy, 0);
  }
}
static void cv_thumb_ready(CvThumb *t) {
  if (g_cvThumb) free(g_cvThumb->pixels), free(g_cvThumb);
  g_cvThumb = t;
}

/* значки плиток «Создать» — по образцу имени и размеру */
static struct { const wchar_t *shell; int px; HBITMAP bmp; BYTE *pix; } g_cvKinds[8];
static const BYTE *cv_kind_icon(const CvRingItem *it, int px) {
  int slot = -1;
  for (int i = 0; i < 8; i++) {
    if (g_cvKinds[i].shell == it->shell && g_cvKinds[i].px == px) return g_cvKinds[i].pix;
    if (slot < 0 && !g_cvKinds[i].bmp) slot = i;
  }
  if (slot < 0) slot = 0;
  if (g_cvKinds[slot].bmp) DeleteObject(g_cvKinds[slot].bmp);
  g_cvKinds[slot].pix = NULL;
  g_cvKinds[slot].bmp = cv_icon_bitmap(it->shell, it->folder, px, &g_cvKinds[slot].pix);
  g_cvKinds[slot].shell = it->shell, g_cvKinds[slot].px = px;
  return g_cvKinds[slot].bmp ? g_cvKinds[slot].pix : NULL;
}
/* центр кольца «Создать»: что появится на столе (значок из Проводника) или подсказка */
static void cv_hub_create(BYTE *pix, int size, HDC dc, CvGeo g, COLORREF ink, COLORREF muted) {
  const CvRingItem *it = g_cvHover >= 0 ? &kCvRing[g_cvFormats[g_cvHover]] : NULL;
  int ipx = (int)(44 * g.s + 0.5f);
  const BYTE *icon = it ? cv_kind_icon(it, ipx) : NULL;
  if (icon) cv_blit(pix, size, icon, ipx, ipx, (int)(g.c - ipx / 2.0f), (int)(g.c - 26 * g.s - ipx / 2.0f), 1.0f);
  else if (g_cvFont[CV_F_ICON_BIG]) {
    wchar_t plus = 0xE710;
    cv_text(pix, size, dc, CV_F_ICON_BIG, &plus, 1, cv_box(g.c, g.c - 26 * g.s, 44 * g.s, 40 * g.s), DT_CENTER | DT_VCENTER | DT_SINGLELINE,
            cv_pal().accent, 1.0f);
  }
  cv_text(pix, size, dc, CV_F_TITLE, it ? it->title : L"Создать", -1, cv_box(g.c, g.c + 8 * g.s, g.rhub * 1.7f, 22 * g.s),
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, ink, 1.0f);
  cv_text(pix, size, dc, CV_F_SMALL, it ? it->note : L"наведите на плитку и отпустите мышь", -1, cv_box(g.c, g.c + 34 * g.s, g.rhub * 1.5f, 32 * g.s),
          DT_CENTER | DT_WORDBREAK, muted, 1.0f);
}

static COLORREF cv_file_tint(const wchar_t *path) {
  const wchar_t *e = cv_extension(path);
  if (cv_image_type(path) >= 0) return kCvTint[CV_T_IMAGE];
  if (!_wcsicmp(e, L"pdf")) return kCvTint[CV_T_PDF];
  if (cv_word_file(path)) return RGB(37, 99, 235);
  if (cv_excel_file(path) || !_wcsicmp(e, L"csv")) return RGB(21, 128, 61);
  return kCvTint[CV_T_MORE];
}

/* карточка в центре: листок с расширением, имя (или число файлов) и подсказка;
   курсор появляется именно здесь, поэтому центр — спокойная карточка, а не «Отмена» */
static void cv_hub_file(BYTE *pix, int size, HDC dc, CvGeo g, COLORREF ink, COLORREF muted) {
  const wchar_t *path = g_cvSelection.count ? g_cvSelection.paths[0] : g_cvPath;
  COLORREF tint = cv_file_tint(path), frame = cv_style()->dark ? RGB(140, 146, 158) : cv_mix(g_uiTok.line, ink, 0.25f);
  int ipx = (int)(48 * g.s + 0.5f);
  const BYTE *icon = cv_hub_icon(path, ipx);
  if (g_cvThumb && !_wcsicmp(g_cvThumb->path, path)) { /* миниатюра — листом с тонкой рамкой и тенью */
    /* размер уже вписан в 58 px при запросе (cv_thumb_request) */
    int w = g_cvThumb->w, h = g_cvThumb->h, x0 = (int)(g.c - w / 2.0f), y0 = (int)(g.c - 6 * g.s) - h; /* низом — над именем */
    if (g_cvSelection.count > 1)
      cv_rrect(pix, size, x0 + 6 * g.s, y0 - 5 * g.s, x0 + w + 6 * g.s, y0 + h - 5 * g.s, 2 * g.s, cv_mix(frame, ink, 0.1f), 1.0f);
    cv_rrect(pix, size, x0 - 1.5f, y0 + 0.5f, x0 + w + 1.5f, y0 + h + 2.5f, 2 * g.s, RGB(0, 0, 0), 0.18f); /* тень */
    cv_rrect(pix, size, x0 - 1, y0 - 1, x0 + w + 1, y0 + h + 1, 1.5f, frame, 1.0f); /* рамка */
    cv_blit(pix, size, g_cvThumb->pixels, w, h, x0, y0, 1.0f);
  } else if (icon) { /* значок, как в Проводнике; несколько файлов — стопкой */
    int x0 = (int)(g.c - ipx / 2.0f), y0 = (int)(g.c - 52 * g.s);
    if (g_cvSelection.count > 1) cv_blit(pix, size, icon, ipx, ipx, x0 + (int)(7 * g.s), y0 - (int)(5 * g.s), 0.45f);
    cv_blit(pix, size, icon, ipx, ipx, x0, y0, 1.0f);
  } else { /* значка нет — листок с расширением */
    float w = 32 * g.s, h = 40 * g.s, x0 = g.c - w / 2, y0 = g.c - 46 * g.s;
    if (g_cvSelection.count > 1) /* стопка: второй листок позади */
      cv_rrect(pix, size, x0 + 5 * g.s, y0 - 4 * g.s, x0 + w + 5 * g.s, y0 + h - 4 * g.s, 5 * g.s, cv_mix(tint, g_uiTok.surface, 0.55f), 1.0f);
    cv_rrect(pix, size, x0, y0, x0 + w, y0 + h, 5 * g.s, tint, 1.0f);
    cv_rrect(pix, size, x0 + w - 11 * g.s, y0, x0 + w, y0 + 11 * g.s, 3 * g.s, cv_mix(tint, RGB(255, 255, 255), 0.45f), 1.0f); /* загнутый угол */
    wchar_t ext[8];
    lstrcpynW(ext, cv_extension(path), 6);
    CharUpperW(ext);
    cv_text(pix, size, dc, CV_F_BADGE, ext[0] ? ext : L"?", -1, cv_box(g.c, y0 + h * 0.62f, w, 16 * g.s),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255), 1.0f);
  }
  wchar_t line[MAX_PATH];
  int k = g_cvSelection.count;
  if (k > 1) {
    int t = k % 100, d = k % 10;
    swprintf(line, MAX_PATH, L"%d %ls", k, t >= 11 && t <= 14 ? L"файлов" : d == 1 ? L"файл" : d >= 2 && d <= 4 ? L"файла" : L"файлов");
  } else {
    const wchar_t *slash = wcsrchr(path, L'\\');
    lstrcpynW(line, slash ? slash + 1 : path, MAX_PATH);
  }
  if (g_cvSkipped && k > 1) wcscat(line, L", без папок");
  cv_text(pix, size, dc, k > 1 ? CV_F_LABEL : CV_F_SMALL, line, -1, cv_box(g.c, g.c + 12 * g.s, g.rhub * 1.55f, 18 * g.s),
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, ink, 1.0f);
  cv_text(pix, size, dc, CV_F_SMALL, g_cvSkipped && k == 1 ? L"папки пропущены" : g_cvInHub ? L"здесь — отмена" : L"Esc — отмена", -1, cv_box(g.c, g.c + 31 * g.s, g.rhub * 1.4f, 16 * g.s),
          DT_CENTER | DT_VCENTER | DT_SINGLELINE, muted, 1.0f);
}

/* значок, заголовок и пояснение в центре */
static void cv_hub_say(BYTE *pix, int size, HDC dc, CvGeo g, wchar_t icon, COLORREF iconInk, const wchar_t *title,
                       const wchar_t *note, COLORREF ink, COLORREF muted) {
  float y = g.c - (icon && g_cvFont[CV_F_ICON_BIG] ? 0 : 18 * g.s);
  if (icon && g_cvFont[CV_F_ICON_BIG])
    cv_text(pix, size, dc, CV_F_ICON_BIG, &icon, 1, cv_box(g.c, g.c - 26 * g.s, 44 * g.s, 40 * g.s),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE, iconInk, 1.0f);
  RECT tr = cv_box(g.c, y + 8 * g.s, g.rhub * 1.7f, 22 * g.s);
  int font = CV_F_TITLE;
  SIZE ext;
  SelectObject(dc, g_cvFont[font]);
  if (GetTextExtentPoint32W(dc, title, lstrlenW(title), &ext) && ext.cx > tr.right - tr.left) font = CV_F_LABEL; /* длинное — помельче */
  cv_text(pix, size, dc, font, title, -1, tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, ink, 1.0f);
  RECT r = cv_box(g.c, y + 34 * g.s, g.rhub * 1.5f, 32 * g.s);
  cv_text(pix, size, dc, CV_F_SMALL, note, -1, r, DT_CENTER | DT_WORDBREAK, muted, 1.0f);
}

/* ---- снимок экрана под кольцом и его размытие («стекло») --------------------
   BitBlt с экрана без CAPTUREBLT не берёт слоистые окна — ни само кольцо, ни
   картинку перетаскивания Проводника. Снимается при показе кольца; окно на
   месте не двигается, так что снимок живёт, пока кольцо открыто. */
static HBITMAP g_cvBackBmp;
static BYTE *g_cvBackPix;
static int g_cvBackSize;
static BOOL g_cvBackOk;
static unsigned g_cvBackGen; /* растёт с каждым снимком — для кэша кадра */
static BYTE *g_cvBlurTmp;
static void cv_blur_pass(const BYTE *src, BYTE *dst, int size, int rad, BOOL vertical) {
  int n = 2 * rad + 1;
  for (int line = 0; line < size; line++) {
    int sum[3] = {0, 0, 0};
    for (int i = -rad; i <= rad; i++) {
      int j = i < 0 ? 0 : i >= size ? size - 1 : i;
      const BYTE *p = src + (vertical ? ((size_t)j * size + line) : ((size_t)line * size + j)) * 4;
      sum[0] += p[0], sum[1] += p[1], sum[2] += p[2];
    }
    for (int i = 0; i < size; i++) {
      BYTE *d = dst + (vertical ? ((size_t)i * size + line) : ((size_t)line * size + i)) * 4;
      d[0] = (BYTE)(sum[0] / n), d[1] = (BYTE)(sum[1] / n), d[2] = (BYTE)(sum[2] / n), d[3] = 255;
      int add = i + rad + 1, rem = i - rad;
      if (add >= size) add = size - 1;
      if (rem < 0) rem = 0;
      const BYTE *pa = src + (vertical ? ((size_t)add * size + line) : ((size_t)line * size + add)) * 4;
      const BYTE *pr = src + (vertical ? ((size_t)rem * size + line) : ((size_t)line * size + rem)) * 4;
      sum[0] += pa[0] - pr[0], sum[1] += pa[1] - pr[1], sum[2] += pa[2] - pr[2];
    }
  }
}
#ifdef CV_RING_TEST
/* стенд без экрана: условный «проводник» — светлое окно со строками на цветном столе */
static void cv_backdrop_fake(int size) {
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      BYTE *p = g_cvBackPix + ((size_t)y * size + x) * 4;
      BOOL win = x < size * 2 / 3;
      if (win) {
        BOOL row = (y / 26) % 2 == 0, text = (x % 90) < 50 && (y % 26) > 8 && (y % 26) < 18;
        int v = row ? 250 : 244;
        p[0] = p[1] = p[2] = (BYTE)(text ? 120 : v);
      } else {
        p[2] = (BYTE)(20 + x * 60 / size), p[1] = (BYTE)(60 + y * 120 / size), p[0] = (BYTE)(120 + x * 100 / size);
      }
      p[3] = 255;
    }
}
#endif
static void cv_backdrop_capture(void) {
  g_cvBackOk = FALSE;
  g_cvBackGen++;
  int size = g_cvWin;
  if (!g_ringBlur || size <= 0) return;
  if (g_cvBackSize != size) {
    if (g_cvBackBmp) DeleteObject(g_cvBackBmp);
    free(g_cvBlurTmp);
    g_cvBackBmp = NULL, g_cvBackPix = NULL, g_cvBlurTmp = NULL, g_cvBackSize = 0;
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    g_cvBackBmp = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&g_cvBackPix, NULL, 0);
    g_cvBlurTmp = (BYTE *)malloc((size_t)size * size * 4);
    if (!g_cvBackBmp || !g_cvBackPix || !g_cvBlurTmp) {
      if (g_cvBackBmp) DeleteObject(g_cvBackBmp);
      free(g_cvBlurTmp);
      g_cvBackBmp = NULL, g_cvBackPix = NULL, g_cvBlurTmp = NULL;
      return;
    }
    g_cvBackSize = size;
  }
  BOOL ok = FALSE;
  if (g_cvWnd) {
    HDC screen = GetDC(NULL), mem = screen ? CreateCompatibleDC(screen) : NULL;
    if (mem) {
      HGDIOBJ old = SelectObject(mem, g_cvBackBmp);
      ok = BitBlt(mem, 0, 0, size, size, screen, g_cvAt.x, g_cvAt.y, SRCCOPY);
      GdiFlush();
      SelectObject(mem, old);
      DeleteDC(mem);
    }
    if (screen) ReleaseDC(NULL, screen);
  }
#ifdef CV_RING_TEST
  if (!g_cvWnd) { cv_backdrop_fake(size); ok = TRUE; }
#endif
  if (!ok) return;
  int rad = (int)(cv_style()->blur * ((float)g_cvSize / CV_RING_BASE) * 0.55f + 0.5f);
  if (rad < 1) rad = 1;
  for (int pass = 0; pass < 2; pass++) {
    cv_blur_pass(g_cvBackPix, g_cvBlurTmp, size, rad, FALSE);
    cv_blur_pass(g_cvBlurTmp, g_cvBackPix, size, rad, TRUE);
  }
  g_cvBackOk = TRUE;
}

/* ---- геометрия плиток и дуги --------------------------------------------- */
/* скругление, которое помещается в узкий сектор (иначе у «Лепестков» при 16 плитках углы сходятся) */
static float cv_round_fit(float round, float rIn, float half, float gap, float s) {
  float max = (2.0f * rIn * sinf(half) - gap) / 2 - 1.5f * s;
  if (round > max) round = max;
  return round < 1.5f * s ? 1.5f * s : round;
}
/* расстояние до скруглённого сектора: r, da — полярные относительно его середины; половина раствора half */
static float cv_sector_sdf(float r, float da, float rIn, float rOut, float half, float gap, float round) {
  float dr = fabsf(r - (rIn + rOut) / 2) - (rOut - rIn) / 2, dt = r * sinf(fabsf(da) - half) + gap / 2;
  float qx = dr + round, qy = dt + round;
  return sqrtf((qx > 0 ? qx * qx : 0) + (qy > 0 ? qy * qy : 0)) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - round;
}
/* внешний и внутренний радиусы плитки i при подсветке l */
static void cv_tile_radii(CvGeo g, float l, float *rIn, float *rOut) {
  *rIn = g.rin + 2.0f * g.s * l;
  *rOut = g.R - g.inset + g.pop * l;
}
/* дуга «Ссылки»: середина сегмента k (радианы, по часовой на экране) и его радиусы при подсветке l */
#define CV_ARC_HALF 0.4363323f /* 25° — сегмент в 50° */
static float cv_arc_mid(int k) {
  float a = -1.5707963f + (g_cvCount > 0 ? g_cvSubTile * 6.2831853f / g_cvCount : 0.0f);
  /* выше тот сегмент, чья середина выше на экране (меньше sin); при равенстве — левый */
  float up = sinf(a - CV_ARC_HALF) <= sinf(a + CV_ARC_HALF) + 1e-4f ? a - CV_ARC_HALF : a + CV_ARC_HALF;
  float down = up == a - CV_ARC_HALF ? a + CV_ARC_HALF : a - CV_ARC_HALF;
  return k == 0 ? up : down;
}
static void cv_arc_radii(CvGeo g, float l, float t, float *r0, float *r1) {
  const CvStyle *st = cv_style();
  float in = g.R + g.pop + st->arcGap * g.s, out = in + (st->arcW + 5.0f * l) * g.s;
  /* выезжает из-под кольца: радиусы растут от края круга к своим */
  *r0 = g.R - 4 * g.s + (in - g.R + 4 * g.s) * t;
  *r1 = g.R + (out - g.R) * t;
}
static float cv_wrap(float a) {
  while (a < -3.1415927f) a += 6.2831853f;
  while (a > 3.1415927f) a -= 6.2831853f;
  return a;
}
#ifdef CV_RING_TEST
/* середина сегмента k дуги — в координатах экрана (для стенда) */
static POINT cv_sub_center(int k) {
  CvGeo g = cv_geo(g_cvSize, 1.0f);
  float r0, r1;
  cv_arc_radii(g, 0.0f, 1.0f, &r0, &r1);
  float a = cv_arc_mid(k), rm = (r0 + r1) / 2;
  POINT p = {g_cvAt.x + (LONG)(g.c + cosf(a) * rm), g_cvAt.y + (LONG)(g.c + sinf(a) * rm)};
  return p;
}
#endif
/* сегмент дуги под точкой экрана: 0 — «Включая файл», 1 — «До файла», -1 — мимо */
static int cv_sub_hit(POINT point) {
  if (!g_cvSubOn || g_cvSubTile < 0 || g_cvSubTile >= g_cvCount) return -1;
  CvGeo g = cv_geo(g_cvSize, 1.0f);
  float x = point.x - g_cvAt.x - g.c, y = point.y - g_cvAt.y - g.c, r = sqrtf(x * x + y * y), r0, r1;
  cv_arc_radii(g, 0.0f, 1.0f, &r0, &r1);
  if (r < r0 - 3 * g.s || r > r1 + 8 * g.s) return -1;
  float a = atan2f(y, x);
  for (int k = 0; k < 2; k++)
    if (fabsf(cv_wrap(a - cv_arc_mid(k))) <= CV_ARC_HALF) return k;
  return -1;
}

/* обход контура скруглённого сектора: точки через ~2 px, замкнуто */
typedef struct { float x, y; } CvPt;
#define CV_OUTLINE_MAX 1024
static int cv_outline_arc(CvPt *out, int n, float cx, float cy, float rad, float from, float to) {
  float sweep = to - from;
  int steps = (int)(fabsf(sweep) * rad / 2.0f) + 1;
  for (int i = 0; i <= steps && n < CV_OUTLINE_MAX; i++) {
    float a = from + sweep * i / steps;
    out[n].x = cx + cosf(a) * rad, out[n].y = cy + sinf(a) * rad;
    n++;
  }
  return n;
}
static int cv_sector_outline(CvPt *out, float cx, float cy, float r0, float r1, float a0, float a1, float gap, float k) {
  float off = gap / 2 + k, R1 = r1 - k, R0 = r0 + k;
  if (R1 <= R0 + 1.0f || R0 <= off + 0.5f) return 0;
  float t0 = sqrtf(R0 * R0 - off * off), t1 = sqrtf(R1 * R1 - off * off);
  float d0 = atan2f(off, t0), d1 = atan2f(off, t1);
  float ac1 = a0 + d1, ac2 = a1 - d1, ac3 = a1 - d0, ac4 = a0 + d0;
  if (ac2 <= ac1 || ac3 <= ac4) return 0;
  float c1x = cx + cosf(ac1) * R1, c1y = cy + sinf(ac1) * R1, c2x = cx + cosf(ac2) * R1, c2y = cy + sinf(ac2) * R1;
  float c3x = cx + cosf(ac3) * R0, c3y = cy + sinf(ac3) * R0, c4x = cx + cosf(ac4) * R0, c4y = cy + sinf(ac4) * R0;
  const float h = 1.5707963f;
  int n = 0;
  n = cv_outline_arc(out, n, cx, cy, r1, ac1, ac2);                 /* внешняя дуга */
  n = cv_outline_arc(out, n, c2x, c2y, k, ac2, a1 + h);             /* угол у c2 */
  n = cv_outline_arc(out, n, c3x, c3y, k, a1 + h, ac3 + 3.1415927f); /* угол у c3 (прямая между ними — сама собой) */
  n = cv_outline_arc(out, n, cx, cy, r0, ac3, ac4);                 /* внутренняя дуга, назад */
  n = cv_outline_arc(out, n, c4x, c4y, k, ac4 + 3.1415927f, a0 + 3 * h); /* угол у c4 */
  n = cv_outline_arc(out, n, c1x, c1y, k, a0 - h, ac1);             /* угол у c1 */
  return n;
}
/* точки через step вдоль замкнутой ломаной, со сдвигом phase */
static void cv_dots_along(BYTE *pix, int size, const CvPt *pts, int n, float step, float phase, float rad, COLORREF col, float alpha) {
  if (n < 2 || step <= 0.5f || alpha <= 0.003f) return;
  float next = phase;
  while (next < 0) next += step;
  float walked = 0;
  for (int i = 0; i < n; i++) {
    const CvPt *a = &pts[i], *b = &pts[(i + 1) % n];
    float dx = b->x - a->x, dy = b->y - a->y, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0001f) continue;
    while (next <= walked + len) {
      float t = (next - walked) / len;
      cv_disc(pix, size, a->x + dx * t, a->y + dy * t, rad, col, alpha);
      next += step;
    }
    walked += len;
  }
}

/* ---- кадр ------------------------------------------------------------------
   Тяжёлая часть (тень, стекло, кромки — попиксельно) считается в g_cvBasePix
   и пересчитывается только когда меняется её состояние (ключ g_cvBaseKey);
   точки, значки, подписи и центр кладутся поверх каждый кадр. */
static BYTE *g_cvBasePix;
static int g_cvBaseSize;
static unsigned g_cvBaseKey;
static unsigned cv_hash(unsigned h, int v) { return (h ^ (unsigned)v) * 16777619u; }

typedef struct { float in[16], sc[16], hub, scale, arc; } CvAnim; /* появление: прозрачность и масштаб плиток, центра, дуги */
static CvAnim cv_anim(DWORD now) {
  const CvStyle *st = cv_style();
  CvAnim a;
  float e = g_cvShownAt && g_ringAnim ? (float)(now - g_cvShownAt) : 1e6f;
  for (int i = 0; i < 16; i++) {
    if (st->appear == 1) { /* цветение: лепестки вырастают из центра друг за другом, с перелётом */
      float t = cv_clamp01((e - i * 28.0f) / 260.0f), u = t - 1.0f;
      float back = 1.0f + 2.70158f * u * u * u + 1.70158f * u * u;
      a.sc[i] = 0.55f + 0.45f * back;
      a.in[i] = cv_clamp01(t * 2.5f);
    } else {
      a.sc[i] = 1.0f;
      a.in[i] = cv_clamp01((e - i * 22.0f) / 110.0f);
    }
  }
  a.hub = cv_ease(e / 150.0f);
  a.scale = st->appear == 1 ? 1.0f : 0.9f + 0.1f * cv_ease(e / 180.0f);
  a.arc = g_cvSubOn ? (g_cvSubShownAt && g_ringAnim ? cv_ease((float)(now - g_cvSubShownAt) / 160.0f) : 1.0f) : 0.0f;
  return a;
}
static BOOL cv_appearing(DWORD now) {
  if (!g_ringAnim) return FALSE;
  if (g_cvShownAt && now - g_cvShownAt < (DWORD)(cv_style()->appear == 1 ? 260 + 16 * 28 : 180 + 16 * 22)) return TRUE;
  if (g_cvSubOn && g_cvSubShownAt && now - g_cvSubShownAt < 180) return TRUE;
  return FALSE;
}

/* стекло, тень и кромки — в base */
static void cv_render_base(BYTE *pix, int size, DWORD now, CvAnim an) {
  const CvStyle *st = cv_style();
  CvPal pal = cv_pal();
  memset(pix, 0, (size_t)size * size * 4);
  CvGeo g = cv_geo(g_cvSize, an.scale);
  BOOL wait = !g_cvCount;
  int n = wait ? 6 : g_cvCount;
  float span = 6.2831853f / n, half = span / 2;
  float round = cv_round_fit(g.round, g.rin, half, g.gap, g.s);
  /* огонёк ожидания: какая плитка горит сейчас (доля с хвостом) — первую секунду чтения */
  float spin = (float)(now % 1200) / 1200.0f * n;
  BOOL reading = g_cvPendingData && now - g_cvEnteredAt <= 1000;
  float shadowA = st->shadowA * (cv_dark() ? 1.6f : 1.0f);
  float arcR0 = 0, arcR1 = 0, arcMid[2] = {0, 0}, arcRound = 0;
  BOOL arc = g_cvSubOn && an.arc > 0.001f && g_cvSubTile >= 0 && g_cvSubTile < g_cvCount;
  if (arc) {
    cv_arc_radii(g, 0.0f, an.arc, &arcR0, &arcR1);
    arcMid[0] = cv_arc_mid(0), arcMid[1] = cv_arc_mid(1);
    arcRound = cv_round_fit(st->arcRound * g.s, arcR0, CV_ARC_HALF, g.gap, g.s);
  }
  float rMax = (arc ? arcR1 + 6 * g.s : g.R + g.pop) + 26.0f * g.s;
  for (int y = 0; y < size; y++) {
    BYTE *row = pix + (size_t)y * size * 4;
    float dy = y + 0.5f - g.c;
    for (int x = 0; x < size; x++) {
      float dx = x + 0.5f - g.c, r = sqrtf(dx * dx + dy * dy);
      if (r > rMax) continue;
      BYTE *p = row + (size_t)x * 4;
      const BYTE *bp = g_cvBackOk ? g_cvBackPix + ((size_t)y * size + x) * 4 : NULL;
      COLORREF back = bp ? RGB(bp[2], bp[1], bp[0]) : pal.tint;
      float a = atan2f(dy, dx), ai = a + 1.5707963f + half;
      while (ai < 0) ai += 6.2831853f;
      while (ai >= 6.2831853f) ai -= 6.2831853f;
      int i = (int)(ai / span) % n;
      float da = ai - (i + 0.5f) * span, l = wait ? 0.0f : g_cvLevel[i];
      float rIn, rOut;
      cv_tile_radii(g, l, &rIn, &rOut);
      float rt = r / an.sc[i]; /* лепесток растёт из центра */
      float dTile = cv_sector_sdf(rt, da, rIn, rOut, half, g.gap, round);
      float tileIn = wait ? an.hub : an.in[i];
      /* тень: у сплошного круга — от круга, у лепестков — от каждой плитки и центра; чуть ниже */
      float sy = dy - 4.0f * g.s, rs = sqrtf(dx * dx + sy * sy), ds;
      if (st->plateA > 0.0f) ds = rs - g.R;
      else {
        float dsh = cv_sector_sdf(rs / an.sc[i], atan2f(sy, dx) + 1.5707963f + half - (i + 0.5f) * span, rIn, rOut, half, g.gap, round);
        float dh = rs - g.rhub;
        ds = dsh < dh ? dsh : dh;
        /* плитка ещё не появилась — без тени */
        if (dsh < dh && tileIn < 0.999f) ds = dh + (dsh - dh) * tileIn;
      }
      if (arc) {
        for (int k = 0; k < 2; k++) {
          float dk = cv_sector_sdf(rs, cv_wrap(atan2f(sy, dx) - arcMid[k]), arcR0, arcR1 + 5.0f * g.s * g_cvSubLevel[k], CV_ARC_HALF, g.gap, arcRound);
          if (dk < ds) ds = dk;
        }
      }
      if (ds > -3.0f) cv_px(p, RGB(0, 0, 0), shadowA * expf(-(ds > 0 ? ds : 0) / (8.0f * g.s)) * cv_clamp01(ds + 3.0f) * an.hub);
      /* подложка-круг */
      if (st->plateA > 0.0f) {
        float dd = r - g.R, cov = cv_clamp01(0.5f - dd);
        if (cov > 0) cv_px(p, cv_mix(back, pal.tint, st->plateA), cov * (bp ? 0.98f : 0.97f) * an.hub);
        float b = 1.0f - fabsf(dd + 0.5f);
        if (b > 0) cv_px(p, pal.edge, b * st->edgeA * 0.8f * an.hub);
      }
      /* плитка */
      float cov = cv_clamp01(0.5f - dTile);
      if (cov > 0 && r > g.rin * 0.8f) {
        COLORREF glass;
        if (wait) {
          float lag = spin - i;
          while (lag < 0) lag += n;
          float glow = lag < 1.0f ? 1.0f : expf(-(lag - 1.0f) * 1.6f);
          glass = cv_mix(cv_mix(back, pal.tint, st->tileA), pal.accent, reading ? 0.12f + 0.4f * glow : 0.0f);
          cv_px(p, glass, cov * 0.85f * tileIn);
        } else {
          glass = cv_mix(cv_mix(back, pal.tint, st->tileA), pal.accent, st->dark ? 0.42f * l : 0.86f * l);
          cv_px(p, glass, cov * (bp ? 1.0f : 0.97f) * tileIn);
        }
        float b = 1.0f - fabsf(dTile + 0.5f);
        if (b > 0) cv_px(p, st->dark && l > 0 ? cv_mix(pal.edge, RGB(147, 197, 253), l) : pal.edge, b * st->edgeA * (st->dark ? 1.0f + 3.0f * l : 1.0f) * tileIn);
      }
      /* центр */
      float dh = r - g.rhub, ch = cv_clamp01(0.5f - dh);
      if (ch > 0) {
        if (st->plateA <= 0.0f || st->hubA > st->plateA) cv_px(p, cv_mix(back, pal.tint, st->hubA), ch * (st->plateA > 0 ? 0.7f : 0.97f) * an.hub);
        float b = 1.0f - fabsf(dh + 0.5f);
        if (b > 0) cv_px(p, pal.edge, b * st->edgeA * 0.7f * an.hub);
      }
      /* дуга «Ссылки» */
      if (arc)
        for (int k = 0; k < 2; k++) {
          float lk = g_cvSubLevel[k];
          float dk = cv_sector_sdf(r, cv_wrap(a - arcMid[k]), arcR0, arcR1 + 5.0f * g.s * lk, CV_ARC_HALF, g.gap, arcRound);
          float ck = cv_clamp01(0.5f - dk);
          if (ck <= 0) continue;
          COLORREF glass = cv_mix(cv_mix(back, pal.tint, st->tileA + 0.02f), pal.accent, st->dark ? 0.42f * lk : 0.86f * lk);
          cv_px(p, glass, ck * (bp ? 1.0f : 0.97f) * an.arc);
          float b = 1.0f - fabsf(dk + 0.5f);
          if (b > 0) cv_px(p, pal.edge, b * st->edgeA * an.arc);
        }
    }
  }
}

/* точки по контуру плитки (или сегмента дуги) */
static void cv_dots_shape(BYTE *pix, int size, CvGeo g, float rIn, float rOut, float mid, float half, float round, float phase, COLORREF col, float alpha) {
  const CvStyle *st = cv_style();
  static CvPt pts[CV_OUTLINE_MAX];
  float ins = 6.0f * g.s * (st->dotStep <= 6 ? 0.85f : 1.0f);
  float k = round - ins / 2;
  if (k < 1.5f * g.s) k = 1.5f * g.s;
  int n = cv_sector_outline(pts, g.c, g.c, rIn + ins, rOut - ins, mid - half, mid + half, g.gap + 2 * ins, k);
  cv_dots_along(pix, size, pts, n, st->dotStep * g.s, phase, st->dotR * g.s, col, alpha);
}

/* весь кадр: база (стекло) из кэша или заново, затем точки, подписи и центр */
static void cv_render(BYTE *pix, int size, HDC dc, DWORD now) {
  const CvStyle *st = cv_style();
  CvPal pal = cv_pal();
  CvAnim an = cv_anim(now);
  BOOL wait = !g_cvCount;
  int n = wait ? 6 : g_cvCount;
  /* ключ состояния базы: что угодно из этого меняет стекло */
  unsigned key = cv_hash(2166136261u, size);
  key = cv_hash(key, g_ringStyle), key = cv_hash(key, g_ringBlur), key = cv_hash(key, (int)g_cvBackGen), key = cv_hash(key, g_cvBackOk);
  key = cv_hash(key, g_cvCount), key = cv_hash(key, g_cvHover), key = cv_hash(key, g_cvSubOn), key = cv_hash(key, g_cvSubTile);
  key = cv_hash(key, (int)g_uiTok.surface), key = cv_hash(key, (int)g_uiTok.accent), key = cv_hash(key, (int)g_uiTok.ink);
  key = cv_hash(key, (int)(an.hub * 512)), key = cv_hash(key, (int)(an.scale * 4096)), key = cv_hash(key, (int)(an.arc * 512));
  for (int i = 0; i < 16; i++) key = cv_hash(key, (int)(g_cvLevel[i] * 256) + (int)(an.in[i] * 256) * 1000 + (int)(an.sc[i] * 2048) * 1000000);
  key = cv_hash(key, (int)(g_cvSubLevel[0] * 256) + (int)(g_cvSubLevel[1] * 256) * 1000);
  if (wait) key = cv_hash(key, (int)((now % 1200) / 30)), key = cv_hash(key, g_cvPendingData && now - g_cvEnteredAt <= 1000);
  if (!g_cvBasePix || g_cvBaseSize != size) {
    free(g_cvBasePix);
    g_cvBasePix = (BYTE *)malloc((size_t)size * size * 4);
    g_cvBaseSize = g_cvBasePix ? size : 0;
    g_cvBaseKey = 0;
  }
  if (g_cvBasePix) {
    if (key != g_cvBaseKey || !key) { cv_render_base(g_cvBasePix, size, now, an); g_cvBaseKey = key ? key : 1; }
    memcpy(pix, g_cvBasePix, (size_t)size * size * 4);
  } else
    cv_render_base(pix, size, now, an);
  CvGeo g = cv_geo(g_cvSize, an.scale);
  float span = 6.2831853f / n, half = span / 2;
  float round = cv_round_fit(g.round, g.rin, half, g.gap, g.s);
  BOOL arc = g_cvSubOn && an.arc > 0.001f && g_cvSubTile >= 0 && g_cvSubTile < g_cvCount;
  float breathe = st->dark && g_ringAnim ? 0.75f + 0.25f * sinf((float)(now % 2600) / 2600.0f * 6.2831853f) : 1.0f;
  float runStep = st->dotStep * g.s, phase = g_ringAnim ? (float)((now - g_cvShownAt) % 1100) / 1100.0f * runStep : 0.0f;
  /* точки по контуру каждой плитки; у плитки под мышью — ярче и бегут */
  if (g_ringDots)
    for (int i = 0; !wait && i < n; i++) {
      float l = g_cvLevel[i], mid = -1.5707963f + i * span, rIn, rOut;
      cv_tile_radii(g, l, &rIn, &rOut);
      COLORREF col = st->dark && l > 0.5f ? RGB(191, 219, 254) : RGB(255, 255, 255);
      float alpha = st->dotA * (0.9f + 0.1f * l) * (l > 0.5f ? 1.0f : breathe) * an.in[i];
      if (an.sc[i] != 1.0f) { rIn *= an.sc[i]; rOut *= an.sc[i]; }
      cv_dots_shape(pix, size, g, rIn, rOut, mid, half, round, l > 0.5f ? -phase : 0.0f, col, alpha);
    }
  if (!dc) return;
  /* подписи плиток: значок над словом, всегда прямо */
  for (int i = 0; !wait && i < n; i++) {
    const CvRingItem *it = &kCvRing[g_cvFormats[i]];
    float l = g_cvLevel[i], a = -1.5707963f + i * span, rIn, rOut;
    cv_tile_radii(g, l, &rIn, &rOut);
    float rm = (rIn + rOut) / 2 * an.sc[i];
    float cx = g.c + cosf(a) * rm, cy = g.c + sinf(a) * rm, w = rm * span * 0.92f;
    if (w > 96 * g.s) w = 96 * g.s;
    BOOL thin = rOut - rIn < 60 * g.s;
    int kpx = (int)((n <= 4 ? 32 : thin ? 20 : 24) * g.s + 0.5f); /* у четырёх больших плиток — крупнее */
    const BYTE *kind = it->shell ? cv_kind_icon(it, kpx) : NULL;
    float alpha = an.in[i];
    COLORREF iconCol = cv_mix(st->dark ? cv_mix(kCvTint[it->tint], RGB(255, 255, 255), 0.3f) : kCvTint[it->tint], pal.on, l);
    if (kind) /* «Создать»: значок из Проводника, как у такого файла на столе */
      cv_blit(pix, size, kind, kpx, kpx, (int)(cx - kpx / 2.0f), (int)(cy - (n <= 4 ? 13 : thin ? 8 : 10) * g.s - kpx / 2.0f), alpha);
    else if (g_cvFont[CV_F_ICON])
      cv_text(pix, size, dc, CV_F_ICON, &it->icon, 1, cv_box(cx, cy - (thin ? 8 : 10) * g.s, 30 * g.s, 26 * g.s),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE, iconCol, alpha);
    RECT lr = cv_box(cx, cy + (it->shell && n <= 4 ? 16 : g_cvFont[CV_F_ICON] ? (thin ? 11 : 12) : 0) * g.s, w, 18 * g.s);
    int font = thin ? CV_F_SMALL : CV_F_LABEL;
    SIZE ext;
    SelectObject(dc, g_cvFont[font]);
    if (GetTextExtentPoint32W(dc, it->label, lstrlenW(it->label), &ext) && ext.cx > lr.right - lr.left) font = CV_F_SMALL; /* тесно — помельче */
    cv_text(pix, size, dc, font, it->label, -1, lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, cv_mix(pal.ink, pal.on, l), alpha);
  }
  /* дуга «Ссылки»: точки, орбита «Тонкого кольца» и подписи вдоль дуги */
  if (arc) {
    float r0, r1;
    cv_arc_radii(g, 0.0f, an.arc, &r0, &r1);
    float arcRound = cv_round_fit(st->arcRound * g.s, r0, CV_ARC_HALF, g.gap, g.s);
    if (st->halo) { /* пунктирная орбита вокруг всего кольца */
      float rh = (r0 + r1) / 2, stepA = 8.0f * g.s / rh;
      for (float a = 0; a < 6.2831853f; a += stepA) cv_disc(pix, size, g.c + cosf(a) * rh, g.c + sinf(a) * rh, st->dotR * g.s, RGB(255, 255, 255), 0.5f * an.arc);
    }
    for (int k = 0; k < 2; k++) {
      float lk = g_cvSubLevel[k], mid = cv_arc_mid(k);
      if (g_ringDots)
        cv_dots_shape(pix, size, g, r0, r1 + 5.0f * g.s * lk, mid, CV_ARC_HALF, arcRound, lk > 0.5f ? -phase : 0.0f,
                      st->dark && lk > 0.5f ? RGB(191, 219, 254) : RGB(255, 255, 255), st->dotA * an.arc);
      float rm = (r0 + r1 + 5.0f * g.s * lk) / 2, cx = g.c + cosf(mid) * rm, cy = g.c + sinf(mid) * rm;
      /* читается вдоль дуги, верх букв наружу; внизу — наоборот, чтобы не вверх ногами */
      float deg = mid * 57.29578f + (sinf(mid) > 0.2f ? -90.0f : 90.0f);
      cv_text_rot(pix, size, dc, CV_F_LABEL, kCvRing[cv_sub_item(k)].label, cx, cy, deg, cv_mix(pal.ink, pal.on, lk), an.arc);
    }
  }
  /* центр */
  COLORREF ink = pal.ink, muted = pal.muted;
  if (wait) {
    if (g_cvRead == CV_READ_FOLDERS) cv_hub_say(pix, size, dc, g, 0xE8B7, kCvTint[CV_T_ZIP], L"Это папки", L"преобразуются только файлы", ink, muted);
    else if (g_cvRead == CV_READ_MANY) cv_hub_say(pix, size, dc, g, 0xE7BA, kCvTint[CV_T_ZIP], L"Больше 128 файлов", L"возьмите поменьше", ink, muted);
    else if (g_cvPendingData && now - g_cvEnteredAt > 1000) /* проводник так и не отдал список */
      cv_hub_say(pix, size, dc, g, 0xE7BA, kCvTint[CV_T_ZIP], L"Не вижу файлов", L"тяните файлы из папки или со стола", ink, muted);
    else cv_hub_say(pix, size, dc, g, 0, 0, g_cvPendingData ? L"Читаю файлы…" : L"Ведите сюда",
                    g_cvPendingData ? L"держите Shift" : L"не отпускайте мышь и Shift", ink, muted);
  } else if (g_cvCreate)
    cv_hub_create(pix, size, dc, g, ink, muted);
  else if (g_cvHover >= 0 || g_cvSubOn) { /* мышь на дуге — в центре кольца её пояснение */
    int tile = g_cvSubTile >= 0 && g_cvSubTile < g_cvCount ? g_cvFormats[g_cvSubTile] : 24;
    int action = g_cvHover >= 0 ? g_cvFormats[g_cvHover] : g_cvSubHover >= 0 && tile == 24 ? 25 + g_cvSubHover : tile;
    const CvRingItem *it = &kCvRing[action];
    BOOL pages = action < 4 && !_wcsicmp(cv_extension(g_cvPath), L"pdf"); /* PDF → картинки */
    const wchar_t *title = it->title, *note = pages ? L"каждая страница, 300 dpi" : it->note;
    wchar_t both[80];
    if (g_cvHover < 0 && g_cvSubHover >= 0 && tile != 24) { /* «Копия рядом» / «Подмена» у преобразования */
      swprintf(both, 80, L"%ls · %ls", it->title, g_cvSubHover ? L"подмена" : L"копия");
      title = both, note = kCvRing[28 + g_cvSubHover].note;
    }
    cv_hub_say(pix, size, dc, g, it->icon, kCvTint[it->tint], title, note, ink, muted);
  } else
    cv_hub_file(pix, size, dc, g, ink, muted);
}

/* плитки ещё доезжают, бегут точки или ждём файлы — нужен следующий кадр */
static BOOL cv_animating(DWORD now) {
  if (cv_appearing(now)) return TRUE;
  if (!g_cvCount) return g_cvPendingData && now - g_cvEnteredAt < 1100; /* огонёк — секунду, потом подпись */
  for (int i = 0; i < g_cvCount; i++)
    if (fabsf(g_cvLevel[i] - (i == g_cvHover || (g_cvSubOn && i == g_cvSubTile) ? 1.0f : 0.0f)) > 0.01f) return TRUE;
  for (int k = 0; k < 2; k++)
    if (fabsf(g_cvSubLevel[k] - (g_cvSubOn && k == g_cvSubHover ? 1.0f : 0.0f)) > 0.01f) return TRUE;
  if (g_ringAnim && g_ringDots) {
    if (cv_style()->dark) return TRUE; /* точки «дышат» */
    if (g_cvHover >= 0 || g_cvSubHover >= 0 || (g_cvSubOn && g_cvSubTile >= 0)) return TRUE; /* бегут по контуру */
  }
  return FALSE;
}
static void cv_step_levels(void) {
  float k = g_ringAnim ? 0.5f : 1.0f;
  for (int i = 0; i < 16; i++) {
    float to = i == g_cvHover || (g_cvSubOn && i == g_cvSubTile) ? 1.0f : 0.0f; /* «Ссылка» горит, пока открыта её дуга */
    g_cvLevel[i] += (to - g_cvLevel[i]) * k;
    if (fabsf(to - g_cvLevel[i]) < 0.01f) g_cvLevel[i] = to;
  }
  for (int j = 0; j < 2; j++) {
    float to = g_cvSubOn && j == g_cvSubHover ? 1.0f : 0.0f;
    g_cvSubLevel[j] += (to - g_cvSubLevel[j]) * k;
    if (fabsf(to - g_cvSubLevel[j]) < 0.01f) g_cvSubLevel[j] = to;
  }
}

/* кадр size×size, BGRA сверху вниз */
static HBITMAP cv_dib(int size, BYTE **pix) {
  BITMAPINFO info;
  memset(&info, 0, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  *pix = NULL;
  HBITMAP bmp = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)pix, NULL, 0);
  if (bmp && !*pix) DeleteObject(bmp), bmp = NULL;
  return bmp;
}
static BOOL cv_buffers(int size) {
  if (g_cvBmp && g_cvBmpSize == size) return TRUE;
  if (g_cvBmp) DeleteObject(g_cvBmp);
  if (g_cvTextBmp) DeleteObject(g_cvTextBmp);
  g_cvBmp = cv_dib(size, &g_cvPix);
  g_cvTextBmp = cv_dib(size, &g_cvTextPix);
  g_cvBmpSize = g_cvBmp && g_cvTextBmp ? size : 0;
  return g_cvBmpSize != 0;
}

/* отрисовать кадр в g_cvPix; окно (если есть) получает его целиком */
static void cv_paint(void) {
  if (g_cvWin <= 0) g_cvWin = g_cvSize * CV_WIN_BASE / CV_RING_BASE;
  int size = g_cvWin;
  if (!cv_buffers(size)) return;
  cv_fonts(g_cvSize);
  HDC screen = GetDC(NULL), tdc = CreateCompatibleDC(screen);
  if (!tdc) { ReleaseDC(NULL, screen); return; }
  HGDIOBJ oldText = SelectObject(tdc, g_cvTextBmp), oldFont = GetCurrentObject(tdc, OBJ_FONT);
  SetBkMode(tdc, TRANSPARENT);
  SetTextColor(tdc, RGB(255, 255, 255));
  DWORD now = GetTickCount();
  cv_render(g_cvPix, size, tdc, now);
  SelectObject(tdc, oldFont);
  SelectObject(tdc, oldText);
  if (g_cvWnd) {
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, g_cvBmp);
    float t = g_cvShownAt && g_ringAnim ? cv_ease((float)(now - g_cvShownAt) / 150.0f) : 1.0f;
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)(255.0f * t), AC_SRC_ALPHA};
    POINT origin = {0, 0};
    SIZE sz = {size, size};
    UpdateLayeredWindow(g_cvWnd, screen, &g_cvAt, &sz, mem, &origin, 0, &blend, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
  }
  DeleteDC(tdc);
  ReleaseDC(NULL, screen);
}

/* dpi монитора под точкой: кольцо может появиться на другом экране */
static int cv_monitor_dpi(POINT point) {
  typedef HRESULT(WINAPI * MonitorDpi)(HMONITOR, int, UINT *, UINT *);
  static MonitorDpi monitorDpi;
  static BOOL looked;
  if (!looked) {
    looked = TRUE;
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) monitorDpi = (MonitorDpi)GetProcAddress(shcore, "GetDpiForMonitor");
  }
  UINT x = 0, y = 0;
  if (monitorDpi && SUCCEEDED(monitorDpi(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), 0, &x, &y)) && x) return (int)x;
  return g_cvDpi;
}

static void cv_ring_free(void) {
  for (int i = 0; i < CV_F_N; i++) if (g_cvFont[i]) { DeleteObject(g_cvFont[i]); g_cvFont[i] = NULL; }
  if (g_cvBmp) DeleteObject(g_cvBmp);
  if (g_cvTextBmp) DeleteObject(g_cvTextBmp);
  g_cvBmp = g_cvTextBmp = NULL;
  g_cvPix = g_cvTextPix = NULL;
  g_cvBmpSize = g_cvFontSize = 0;
  if (g_cvBackBmp) DeleteObject(g_cvBackBmp);
  g_cvBackBmp = NULL, g_cvBackPix = NULL, g_cvBackSize = 0, g_cvBackOk = FALSE;
  free(g_cvBlurTmp), g_cvBlurTmp = NULL;
  free(g_cvBasePix), g_cvBasePix = NULL, g_cvBaseSize = 0, g_cvBaseKey = 0;
  if (g_cvIconBmp) DeleteObject(g_cvIconBmp);
  g_cvIconBmp = NULL, g_cvIconPix = NULL, g_cvIconPx = 0;
  cv_thumb_ready(NULL);
  g_cvThumbWant[0] = 0;
  for (int i = 0; i < 8; i++) {
    if (g_cvKinds[i].bmp) DeleteObject(g_cvKinds[i].bmp);
    g_cvKinds[i].bmp = NULL, g_cvKinds[i].pix = NULL, g_cvKinds[i].shell = NULL;
  }
}
