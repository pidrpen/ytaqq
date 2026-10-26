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

#include <commctrl.h> /* ImageList_GetIcon */

#define CV_RING_BASE 360 /* сторона окна при 96 dpi; круг — без полей под тень */
enum { CV_T_IMAGE, CV_T_PDF, CV_T_TEXT, CV_T_ZIP, CV_T_MORE };
/* shell — образец имени для значка из Проводника (кольцо «Создать»); folder — значок папки */
typedef struct { wchar_t icon; const wchar_t *label, *title, *note; int tint; const wchar_t *shell; BOOL folder; } CvRingItem;
/* по номерам действий: 0–3 картинки, 4 PDF, 5 TXT, 6 CSV, 7 ZIP, 8–14 окно параметров, 15 «Ещё»,
   24 — «Ссылка», 25 и 26 — её полукруги: «Включая файл» и «До файла» (file_link_ui.c) */
static const CvRingItem kCvRing[27] = {
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
    {0xE712, L"Ещё", L"Все действия", L"окно обработки файлов", CV_T_MORE},
    {0, NULL, NULL, NULL, 0}, {0, NULL, NULL, NULL, 0}, /* 16, 17 — отмена и «показать» истории */
    {0xE8C6, L"Разделить", L"Разделить TIFF", L"каждая страница — свой TIFF", CV_T_IMAGE},
    {0xE7C4, L"Один TIFF", L"Один TIFF", L"порядок — в окне объединения", CV_T_IMAGE},
    /* 20–23 — кольцо «Создать на рабочем столе» */
    {0xE8B7, L"Папка", L"Новая папка", L"отпустите — появится на столе", CV_T_ZIP, L"Новая папка", TRUE},
    {0xE8A5, L"TXT", L"Текстовый файл", L"отпустите — появится на столе", CV_T_MORE, L"a.txt", FALSE},
    {0xE8A5, L"Word", L"Документ Word", L"отпустите — появится на столе", CV_T_TEXT, L"a.docx", FALSE},
    {0xE80A, L"Excel", L"Книга Excel", L"отпустите — появится на столе", CV_T_TEXT, L"a.xlsx", FALSE},
    {0xE71B, L"Ссылка", L"Ссылка", L"задержите: ссылка или путь", CV_T_ZIP},
    {0xE8A5, L"Включая файл", L"Включая файл", L"ссылка — щелчок откроет файл", CV_T_ZIP},
    {0xE71B, L"До файла", L"До файла", L"только путь — в буфер", CV_T_ZIP}};
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
            g_uiTok.accent, 1.0f);
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
  COLORREF tint = cv_file_tint(path);
  int ipx = (int)(48 * g.s + 0.5f);
  const BYTE *icon = cv_hub_icon(path, ipx);
  if (g_cvThumb && !_wcsicmp(g_cvThumb->path, path)) { /* миниатюра — листом с тонкой рамкой и тенью */
    /* размер уже вписан в 58 px при запросе (cv_thumb_request) */
    int w = g_cvThumb->w, h = g_cvThumb->h, x0 = (int)(g.c - w / 2.0f), y0 = (int)(g.c - 6 * g.s) - h; /* низом — над именем */
    if (g_cvSelection.count > 1)
      cv_rrect(pix, size, x0 + 6 * g.s, y0 - 5 * g.s, x0 + w + 6 * g.s, y0 + h - 5 * g.s, 2 * g.s, cv_mix(g_uiTok.line, ink, 0.15f), 1.0f);
    cv_rrect(pix, size, x0 - 1.5f, y0 + 0.5f, x0 + w + 1.5f, y0 + h + 2.5f, 2 * g.s, RGB(0, 0, 0), 0.18f); /* тень */
    cv_rrect(pix, size, x0 - 1, y0 - 1, x0 + w + 1, y0 + h + 1, 1.5f, cv_mix(g_uiTok.line, ink, 0.25f), 1.0f); /* рамка */
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
    int kpx = (int)((n <= 4 ? 32 : 24) * g.s + 0.5f); /* у четырёх больших плиток — крупнее */
    const BYTE *kind = it->shell ? cv_kind_icon(it, kpx) : NULL;
    if (kind) /* «Создать»: значок из Проводника, как у такого файла на столе */
      cv_blit(pix, size, kind, kpx, kpx, (int)(cx - kpx / 2.0f), (int)(cy - (n <= 4 ? 13 : 10) * g.s - kpx / 2.0f), 1.0f);
    else if (g_cvFont[CV_F_ICON])
      cv_text(pix, size, dc, CV_F_ICON, &it->icon, 1, cv_box(cx, cy - 10 * g.s, 30 * g.s, 26 * g.s),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE, cv_mix(kCvTint[it->tint], on, l), 1.0f);
    RECT lr = cv_box(cx, cy + (it->shell && n <= 4 ? 16 : g_cvFont[CV_F_ICON] ? 12 : 0) * g.s, w, 18 * g.s);
    int font = CV_F_LABEL;
    SIZE ext;
    SelectObject(dc, g_cvFont[font]);
    if (GetTextExtentPoint32W(dc, it->label, lstrlenW(it->label), &ext) && ext.cx > lr.right - lr.left) font = CV_F_SMALL; /* тесно — помельче */
    cv_text(pix, size, dc, font, it->label, -1, lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, cv_mix(ink, on, l), 1.0f);
  }
  /* центр */
  if (wait) {
    if (g_cvRead == CV_READ_FOLDERS) cv_hub_say(pix, size, dc, g, 0xE8B7, kCvTint[CV_T_ZIP], L"Это папки", L"преобразуются только файлы", ink, muted);
    else if (g_cvRead == CV_READ_MANY) cv_hub_say(pix, size, dc, g, 0xE7BA, kCvTint[CV_T_ZIP], L"Больше 128 файлов", L"возьмите поменьше", ink, muted);
    else if (g_cvPendingData && !reading) /* проводник так и не отдал список */
      cv_hub_say(pix, size, dc, g, 0xE7BA, kCvTint[CV_T_ZIP], L"Не вижу файлов", L"тяните файлы из папки или со стола", ink, muted);
    else cv_hub_say(pix, size, dc, g, 0, 0, g_cvPendingData ? L"Читаю файлы…" : L"Ведите сюда",
                    g_cvPendingData ? L"держите Shift" : L"не отпускайте мышь и Shift", ink, muted);
  } else if (g_cvCreate)
    cv_hub_create(pix, size, dc, g, ink, muted);
  else if (g_cvHover >= 0) {
    const CvRingItem *it = &kCvRing[g_cvFormats[g_cvHover]];
    BOOL pages = g_cvFormats[g_cvHover] < 4 && !_wcsicmp(cv_extension(g_cvPath), L"pdf"); /* PDF → картинки */
    cv_hub_say(pix, size, dc, g, it->icon, kCvTint[it->tint], it->title, pages ? L"каждая страница, 300 dpi" : it->note, ink, muted);
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
  if (g_cvIconBmp) DeleteObject(g_cvIconBmp);
  g_cvIconBmp = NULL, g_cvIconPix = NULL, g_cvIconPx = 0;
  cv_thumb_ready(NULL);
  g_cvThumbWant[0] = 0;
  for (int i = 0; i < 8; i++) {
    if (g_cvKinds[i].bmp) DeleteObject(g_cvKinds[i].bmp);
    g_cvKinds[i].bmp = NULL, g_cvKinds[i].pix = NULL, g_cvKinds[i].shell = NULL;
  }
}
