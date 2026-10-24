/* Вид кольца Shift-перетаскивания (с 2026.10.24). Своё полупрозрачное окно
   (UpdateLayeredWindow): сглаженные края, мягкая тень, цвета темы g_uiTok.
   Секторы — плитки с зазорами, у каждой значок и короткая подпись; плитка
   под курсором окрашивается акцентом и выдвигается наружу. В центре —
   карточка файла, а при наведении — что сделает действие; центр — отмена.
   Пока проводник не отдал список файлов, по плиткам бежит огонёк.
   Подключается из convert.c после его переменных; рисует только по ним. */
#ifdef CV_RING_TEST
/* проверочные сборки без ui_theme.c: светлая тема по умолчанию */
typedef struct { COLORREF bg, bg2, surface, ink, muted, line, accent; } UiTokens;
static UiTokens g_uiTok = {RGB(255, 255, 255), RGB(243, 244, 246), RGB(255, 255, 255), RGB(17, 24, 39),
                           RGB(107, 114, 128), RGB(226, 229, 233), RGB(37, 99, 235)};
#endif

#define CV_RING_BASE 360 /* сторона окна при 96 dpi; круг — без полей под тень */
enum { CV_T_IMAGE, CV_T_PDF, CV_T_TEXT, CV_T_ZIP, CV_T_MORE };
typedef struct { wchar_t icon; const wchar_t *label, *title, *note; int tint; } CvRingItem;
/* по номерам действий: 0–3 картинки, 4 PDF, 5 TXT, 6 CSV, 7 ZIP, 8–14 окно параметров, 15 «Ещё» */
static const CvRingItem kCvRing[16] = {
    {0xEB9F, L"PNG", L"В PNG", L"копия рядом с исходником", CV_T_IMAGE},
    {0xEB9F, L"JPG", L"В JPG", L"копия рядом с исходником", CV_T_IMAGE},
    {0xEB9F, L"BMP", L"В BMP", L"копия рядом с исходником", CV_T_IMAGE},
    {0xEB9F, L"TIFF", L"В TIFF", L"копия рядом с исходником", CV_T_IMAGE},
    {0xEA90, L"PDF", L"В PDF", L"копия рядом с исходником", CV_T_PDF},
    {0xE8A5, L"TXT", L"Word → TXT", L"текст рядом с исходником", CV_T_TEXT},
    {0xE80A, L"CSV", L"Excel → CSV", L"первый лист, рядом", CV_T_TEXT},
    {0xE7B8, L"ZIP", L"Собрать ZIP", L"архив рядом с исходником", CV_T_ZIP},
    {0xE740, L"Размер", L"Изменить размер", L"откроет окно параметров", CV_T_IMAGE},
    {0xE73F, L"Сжать", L"Сжать до лимита", L"откроет окно параметров", CV_T_IMAGE},
    {0xE8C8, L"Собрать", L"Объединить PDF", L"откроет окно параметров", CV_T_PDF},
    {0xE7C3, L"Страницы", L"Извлечь страницы", L"откроет окно параметров", CV_T_PDF},
    {0xE8C6, L"Разделить", L"Разделить PDF", L"откроет окно параметров", CV_T_PDF},
    {0xE7AD, L"Повернуть", L"Повернуть страницы", L"откроет окно параметров", CV_T_PDF},
    {0xE8F1, L"Один PDF", L"Всё в один PDF", L"порядок — в окне параметров", CV_T_PDF},
    {0xE712, L"Ещё", L"Все действия", L"окно обработки файлов", CV_T_MORE}};
static const COLORREF kCvTint[] = {RGB(8, 145, 178), RGB(220, 38, 38), RGB(22, 163, 74), RGB(217, 119, 6),
                                   RGB(100, 116, 139)};

static int g_cvDpi = 96;
static DWORD g_cvShownAt;   /* начало появления: 150 мс от 92 % к полному */
static float g_cvLevel[16]; /* подсветка плиток 0…1, плавно к цели */
static BOOL g_cvInHub;      /* курсор в центре — отпускание ничего не делает */
static POINT g_cvAt;        /* левый верхний угол окна на экране */
static HBITMAP g_cvBmp, g_cvTextBmp;
static BYTE *g_cvPix, *g_cvTextPix; /* premultiplied BGRA; второй — под текст */
static int g_cvBmpSize, g_cvFontSize;
enum { CV_F_LABEL, CV_F_SMALL, CV_F_TITLE, CV_F_BADGE, CV_F_ICON, CV_F_ICON_BIG, CV_F_N };
static HFONT g_cvFont[CV_F_N];

typedef struct { float c, R, rin, rhub, pop, s; } CvGeo;
/* scale — анимация появления; попадания считаются при scale = 1 */
static CvGeo cv_geo(int size, float scale) {
  CvGeo g;
  g.s = (float)size / CV_RING_BASE;
  g.c = size * 0.5f;
  g.R = (g.c - 22.0f * g.s) * scale;
  g.rin = g.R * 0.5f;
  g.rhub = g.rin - 6.0f * g.s;
  g.pop = 7.0f * g.s;
  return g;
}

static float cv_clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
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
static BOOL cv_dark(void) { return cv_luma(g_uiTok.surface) < 128; }
/* чем писать по акценту: светлый акцент тёмной темы — тёмным, остальные — белым */
static COLORREF cv_on_accent(void) { return cv_luma(g_uiTok.accent) > 150 ? RGB(17, 24, 39) : RGB(255, 255, 255); }

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
/* шрифты под размер окна; сглаживание без ClearType — у прозрачного окна нет своего фона */
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

/* текст цветом col: белым по чёрному в g_cvTextPix, яркость — покрытие */
static void cv_text(BYTE *pix, int size, HDC dc, int font, const wchar_t *s, int n, RECT r, UINT fmt, COLORREF col, float alpha) {
  if (!g_cvFont[font] || !s || !*s) return;
  if (r.left < 0) r.left = 0;
  if (r.top < 0) r.top = 0;
  if (r.right > size) r.right = size;
  if (r.bottom > size) r.bottom = size;
  if (r.right <= r.left || r.bottom <= r.top) return;
  for (int y = r.top; y < r.bottom; y++) memset(g_cvTextPix + ((size_t)y * size + r.left) * 4, 0, (size_t)(r.right - r.left) * 4);
  SelectObject(dc, g_cvFont[font]);
  DrawTextW(dc, s, n, &r, fmt | DT_NOPREFIX);
  GdiFlush();
  for (int y = r.top; y < r.bottom; y++)
    for (int x = r.left; x < r.right; x++) {
      const BYTE *t = g_cvTextPix + ((size_t)y * size + x) * 4;
      BYTE m = t[0] > t[1] ? t[0] : t[1];
      if (t[2] > m) m = t[2];
      if (m) cv_px(pix + ((size_t)y * size + x) * 4, col, m / 255.0f * alpha);
    }
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
  COLORREF tint = cv_file_tint(path);
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

/* весь кадр: тень, круг, плитки, центр. now — для огонька ожидания */
static void cv_render(BYTE *pix, int size, HDC dc, DWORD now) {
  memset(pix, 0, (size_t)size * size * 4);
  float t = g_cvShownAt ? cv_clamp01((float)(now - g_cvShownAt) / 150.0f) : 1.0f;
  float e = 1.0f - (1.0f - t) * (1.0f - t);
  CvGeo g = cv_geo(size, 0.92f + 0.08f * e);
  BOOL dark = cv_dark(), wait = !g_cvCount;
  COLORREF surface = g_uiTok.surface, ink = g_uiTok.ink, muted = g_uiTok.muted, accent = g_uiTok.accent;
  COLORREF tile = cv_mix(surface, ink, dark ? 0.12f : 0.06f), line = cv_mix(g_uiTok.line, ink, dark ? 0.25f : 0.12f);
  int n = wait ? 6 : g_cvCount;
  float span = 6.2831853f / n, half = span / 2, gap = 5.0f * g.s, round = 8.0f * g.s;
  /* огонёк ожидания: какая плитка горит сейчас (доля с хвостом) — первую секунду чтения */
  float spin = (float)(now % 1200) / 1200.0f * n;
  BOOL reading = g_cvPendingData && now - g_cvEnteredAt <= 1000;
  for (int y = 0; y < size; y++) {
    BYTE *row = pix + (size_t)y * size * 4;
    float dy = y + 0.5f - g.c;
    for (int x = 0; x < size; x++) {
      BYTE *p = row + (size_t)x * 4;
      float dx = x + 0.5f - g.c, r = sqrtf(dx * dx + dy * dy);
      /* тень чуть ниже круга */
      float sy = dy - 4.0f * g.s, ds = sqrtf(dx * dx + sy * sy) - g.R;
      if (ds > -3.0f) cv_px(p, RGB(0, 0, 0), (dark ? 0.42f : 0.20f) * expf(-(ds > 0 ? ds : 0) / (8.0f * g.s)) * cv_clamp01(ds + 3.0f));
      float dd = r - g.R;
      if (dd < 1.0f) cv_px(p, surface, cv_clamp01(0.5f - dd) * 0.97f);
      float b = 1.0f - fabsf(dd + 0.5f);
      if (b > 0) cv_px(p, line, b * 0.9f);
      if (r > g.rin - 2 && r < g.R + g.pop + 2) {
        float a = atan2f(dy, dx) + 1.5707963f + half;
        while (a < 0) a += 6.2831853f;
        while (a >= 6.2831853f) a -= 6.2831853f;
        int i = (int)(a / span) % n;
        float da = a - (i + 0.5f) * span, l = wait ? 0.0f : g_cvLevel[i];
        float rIn = g.rin + 2.0f * g.s * l, rOut = g.R - 6.0f * g.s + g.pop * l;
        float dr = fabsf(r - (rIn + rOut) / 2) - (rOut - rIn) / 2, dt = r * sinf(fabsf(da) - half) + gap / 2;
        float qx = dr + round, qy = dt + round;
        float d = sqrtf((qx > 0 ? qx * qx : 0) + (qy > 0 ? qy * qy : 0)) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - round;
        float cov = cv_clamp01(0.5f - d);
        if (cov > 0) {
          if (wait) {
            float lag = spin - i;
            while (lag < 0) lag += n;
            float glow = lag < 1.0f ? 1.0f : expf(-(lag - 1.0f) * 1.6f);
            cv_px(p, cv_mix(tile, accent, reading ? 0.15f + 0.45f * glow : 0.0f), cov * 0.8f);
          } else {
            cv_px(p, cv_mix(tile, accent, l), cov);
          }
        }
      }
      float dh = fabsf(r - g.rhub);
      if (dh < 1.0f) cv_px(p, line, (1.0f - dh) * 0.6f);
    }
  }
  if (!dc) return;
  /* подписи плиток: значок над словом, всегда прямо */
  for (int i = 0; !wait && i < n; i++) {
    const CvRingItem *it = &kCvRing[g_cvFormats[i]];
    float l = g_cvLevel[i], a = -1.5707963f + i * span;
    float rm = (g.rin + 2.0f * g.s * l + g.R - 6.0f * g.s + g.pop * l) / 2;
    float cx = g.c + cosf(a) * rm, cy = g.c + sinf(a) * rm, w = rm * span * 0.92f;
    if (w > 96 * g.s) w = 96 * g.s;
    COLORREF on = cv_on_accent();
    if (g_cvFont[CV_F_ICON])
      cv_text(pix, size, dc, CV_F_ICON, &it->icon, 1, cv_box(cx, cy - 10 * g.s, 30 * g.s, 26 * g.s),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE, cv_mix(kCvTint[it->tint], on, l), 1.0f);
    cv_text(pix, size, dc, CV_F_LABEL, it->label, -1, cv_box(cx, cy + (g_cvFont[CV_F_ICON] ? 12 : 0) * g.s, w, 18 * g.s),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, cv_mix(ink, on, l), 1.0f);
  }
  /* центр */
  if (wait) {
    if (g_cvRead == CV_READ_FOLDERS) cv_hub_say(pix, size, dc, g, 0xE8B7, kCvTint[CV_T_ZIP], L"Это папки", L"преобразуются только файлы", ink, muted);
    else if (g_cvRead == CV_READ_MANY) cv_hub_say(pix, size, dc, g, 0xE7BA, kCvTint[CV_T_ZIP], L"Больше 128 файлов", L"возьмите поменьше", ink, muted);
    else if (g_cvPendingData && !reading) /* проводник так и не отдал список */
      cv_hub_say(pix, size, dc, g, 0xE7BA, kCvTint[CV_T_ZIP], L"Не вижу файлов", L"тяните файлы из папки или со стола", ink, muted);
    else cv_hub_say(pix, size, dc, g, 0, 0, g_cvPendingData ? L"Читаю файлы…" : L"Ведите сюда",
                    g_cvPendingData ? L"держите Shift" : L"не отпускайте мышь и Shift", ink, muted);
  } else if (g_cvHover >= 0) {
    const CvRingItem *it = &kCvRing[g_cvFormats[g_cvHover]];
    cv_hub_say(pix, size, dc, g, it->icon, kCvTint[it->tint], it->title, it->note, ink, muted);
  } else
    cv_hub_file(pix, size, dc, g, ink, muted);
}

/* плитки ещё доезжают или ждём файлы — нужен следующий кадр */
static BOOL cv_animating(DWORD now) {
  if (g_cvShownAt && now - g_cvShownAt < 170) return TRUE;
  if (!g_cvCount) return g_cvPendingData && now - g_cvEnteredAt < 1100; /* огонёк — секунду, потом подпись */
  for (int i = 0; i < g_cvCount; i++)
    if (fabsf(g_cvLevel[i] - (i == g_cvHover ? 1.0f : 0.0f)) > 0.01f) return TRUE;
  return FALSE;
}
static void cv_step_levels(void) {
  for (int i = 0; i < 16; i++) {
    float to = i == g_cvHover ? 1.0f : 0.0f;
    g_cvLevel[i] += (to - g_cvLevel[i]) * 0.5f;
    if (fabsf(to - g_cvLevel[i]) < 0.01f) g_cvLevel[i] = to;
  }
}

static BOOL cv_buffers(int size) {
  if (g_cvBmp && g_cvBmpSize == size) return TRUE;
  if (g_cvBmp) DeleteObject(g_cvBmp);
  if (g_cvTextBmp) DeleteObject(g_cvTextBmp);
  g_cvBmp = g_cvTextBmp = NULL;
  g_cvPix = g_cvTextPix = NULL;
  g_cvBmpSize = 0;
  BITMAPINFO info;
  memset(&info, 0, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  g_cvBmp = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&g_cvPix, NULL, 0);
  g_cvTextBmp = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&g_cvTextPix, NULL, 0);
  if (!g_cvBmp || !g_cvTextBmp || !g_cvPix || !g_cvTextPix) return FALSE;
  g_cvBmpSize = size;
  return TRUE;
}

/* отрисовать кадр в g_cvPix; окно (если есть) получает его целиком */
static void cv_paint(void) {
  int size = g_cvSize;
  if (!cv_buffers(size)) return;
  cv_fonts(size);
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
    float t = g_cvShownAt ? cv_clamp01((float)(now - g_cvShownAt) / 150.0f) : 1.0f;
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)(255.0f * (1.0f - (1.0f - t) * (1.0f - t))), AC_SRC_ALPHA};
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
}
