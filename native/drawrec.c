/* ---- Распознать чертёж: штамп, спецификация, тип листа ----------------------

   Ядро окна «Распознать чертёж» (окно — drawrec_ui.c). На входе страница
   скана (TIFF, PNG, JPG… — читает WIC, как «Объединение TIFF»), на выходе
   поля основной надписи, строки спецификации и тип листа. Читает тот же
   Tesseract, что «Выделить и прочитать» (tess.c); нет его — не читает ничего:
   Windows OCR для таблиц слишком неточен.

   Как. Страница переводится в серое, выпрямляется (перекос скана до ±3°),
   на ней ищутся длинные линии. Правый нижний угол рамки — где сходятся
   нижняя и правая длинные линии. Штамп по ГОСТ 2.104 всегда шириной 185 мм,
   поэтому масштаб берётся не из dpi файла (его часто нет, а у снимка экрана
   он и вовсе ни при чём), а из самой длинной линии штампа, упёртой в правую
   рамку. Высоту штампа даёт вертикаль в 65 мм от его левого края (граница
   граф «Изм./Лист/№ докум.» и обозначения): 55 мм — форма 1 (чертёж),
   40 — форма 2 (первый лист спецификации или текстового документа),
   15 — форма 2а (последующие листы). Графы вырезаются по размерам ГОСТ,
   линии в вырезках стираются, и все вырезки одной страницы читаются одним
   запуском Tesseract (список картинок → TSV со словами, рамками и
   уверенностью).

   Спецификация (ГОСТ 2.106) — над штампом формы 2/2а, столбцы 6, 6, 8, 70,
   63, 10, 22 мм. Строки — по найденным линиям, нет их — через 8 мм. Слово
   попадает в ячейку по своему центру. Строки разделов («Детали»,
   «Стандартные изделия»…) становятся столбцом «Раздел», продолжение
   наименования на следующей строке приклеивается к своей позиции.

   Нет штампа — страница читается целиком (уменьшенной): «маршрутная карта»,
   ГОСТ 3.1118 и т. п. — это ТП, обозначение ищется в тексте.

   Точным полям модель не доверяем: обозначение проверяется по шаблону
   («АДЕ 3422-682.01.01.02», «АБВГ.123456.001», с исполнением и шифром
   документа), масса и листы — числа, масштаб — «1:2»; что не сходится или
   прочитано с малой уверенностью, помечается «проверить» и подсвечивается. */

#define DR_MAXSIDE 9000 /* больше — страница уменьшается: A0 в 300 dpi не нужен целиком */

enum { DRT_DRAW, DRT_SPEC, DRT_TP, DRT_OTHER, DRT_N };
static const wchar_t *const kDrType[DRT_N] = {L"Чертёж", L"Спецификация", L"ТП", L"Прочее"};

enum { DF_DES, DF_NAME, DF_MAT, DF_MASS, DF_SCALE, DF_SHEET, DF_SHEETS, DF_N };
static const wchar_t *const kDrField[DF_N] = {L"Обозначение", L"Наименование", L"Материал", L"Масса",
                                              L"Масштаб", L"Лист", L"Листов"};

enum { DS_SECT, DS_FMT, DS_ZONE, DS_POS, DS_DES, DS_NAME, DS_QTY, DS_NOTE, DS_N };
static const wchar_t *const kDrSpecCol[DS_N] = {L"Раздел", L"Формат", L"Зона", L"Поз.", L"Обозначение",
                                                L"Наименование", L"Кол.", L"Примечание"};

#define DR_CELL 260
typedef struct {
  wchar_t c[DS_N][DR_CELL];
  BYTE doubt[DS_N];
} DrSpecRow;

typedef struct {
  wchar_t path[MAX_PATH];
  UINT frame, nframes;
  BOOL done;          /* распознано (или не вышло — тогда note) */
  int type;           /* DRT_… */
  int form;           /* 1 — форма 1, 2 — форма 2, 3 — форма 2а, 0 — штампа нет */
  wchar_t f[DF_N][DR_CELL];
  BYTE doubt[DF_N];
  wchar_t fmt[16];    /* формат листа: A4, A3, A4×3… */
  wchar_t note[300];  /* что не так — в столбец «Заметки» */
  DrSpecRow *rows;
  int nrows;
  int rowsBy;         /* чем прочитаны строки спецификации: 1 — Tesseract, 2 — модель, 0 — нет (карточка ЭСИ) */
  wchar_t *text;      /* весь лист (ТТ, Ra, допуски, резьбы) — для маршрута, techno.c; NULL — не читали */
  /* то же, прочитанное локальной моделью с картинками (drawrec_vl): графы штампа и ТТ */
  wchar_t vlName[DR_CELL], vlMat[DR_CELL];
  wchar_t vlDes[DR_CELL]; /* обозначение, прочитанное моделью по графе (только vlStamp: rename_by_stamp) */
  wchar_t *vlText;    /* ТТ, «Развертка», «n отв.»; NULL — модель не читала */
  BOOL wantText;      /* прочитать и весь лист (кнопка «Маршрут обработки») */
  BOOL vlLater;       /* модель — вторым проходом (dr_vl_later): черновик по Tesseract сразу */
  int vlStamp;        /* rename_by_stamp: 1 — модель читает только графу «Обозначение», 2 — и «Наименование»; лист — нет */
  BOOL stampOnly;     /* нужен только штамп (rename_by_stamp): строки спецификации модель не читает — это минуты */
  double kmm;         /* точек на мм — по штампу или dpi файла; 0 — неизвестно (угол листа для модели) */
  void *vlKeep;       /* лист и штамп для второго прохода (DrVlKeep); NULL — нет */
  double secs;
} DrPage;

typedef struct {
  int w, h;
  BYTE *px; /* серое, 0 — чёрное */
} DrImg;

static void dr_trim(wchar_t *s);

/* Помощник для сомнительных полей штампа — локальный ИИ (ai.h), только если он
   включён в Настройках. Зовётся из рабочего потока распознавания; в облако
   ничего не уходит. Нет ответа — остаётся то, что нашли правила. */
static BOOL dr_ai_refine_field(const wchar_t *field, const wchar_t *text, const wchar_t *hint, wchar_t *out,
                               int cap) {
  if (!ai_ready() || !text[0]) return FALSE;
  if (!ai_refine_field(field, text, hint, out, cap, 20000)) return FALSE;
  dr_trim(out);
  return out[0] != 0;
}

/* ---- картинка ------------------------------------------------------------------ */

static BOOL dr_img_new(DrImg *m, int w, int h) {
  m->w = w;
  m->h = h;
  m->px = (BYTE *)malloc((size_t)w * (size_t)h);
  if (m->px) memset(m->px, 255, (size_t)w * (size_t)h);
  return m->px != NULL;
}

static void dr_img_free(DrImg *m) {
  free(m->px);
  m->px = NULL;
  m->w = m->h = 0;
}

/* страница файла → серое; dpi — из файла (96 — «не знаю») */
static HRESULT dr_read(IWICImagingFactory *f, const wchar_t *path, UINT frame, DrImg *out, double *dpi) {
  memset(out, 0, sizeof(*out));
  IWICBitmapDecoder *dec = NULL;
  IWICBitmapFrameDecode *fr = NULL;
  IWICBitmapSource *src = NULL, *cv = NULL;
  IWICBitmapScaler *sc = NULL;
  HRESULT hr = tm_open(f, path, frame, &dec, &fr);
  if (FAILED(hr)) return hr;
  int orient = tm_orient(dec, fr);
  double dx = 96, dy = 96;
  if (FAILED(IWICBitmapFrameDecode_GetResolution(fr, &dx, &dy)) || !(dx >= 10 && dx < 100000)) dx = 96;
  *dpi = dx;
  hr = tm_source(f, fr, orient, &src);
  UINT w = 0, h = 0;
  if (SUCCEEDED(hr)) hr = IWICBitmapSource_GetSize(src, &w, &h);
  if (SUCCEEDED(hr) && (w < 32 || h < 32 || w > 200000 || h > 200000)) hr = 0x88982F60;
  /* в серое WIC умеет не из всех форматов (Wine — не из ч/б), в цвет — не из
     всех серых: пробуем по очереди, проверяя первой строкой */
  static const int kFmt[3] = {TMF_GRAY8, TMF_BGR24, TMF_BGRA32};
  int bpp = 1;
  for (int i = 0; SUCCEEDED(hr) && i < 3; i++) {
    if (FAILED(tm_convert(f, src, kFmt[i], &cv))) continue;
    BYTE probe[4 * 64];
    UINT pw = w < 64 ? w : 64;
    WICRect r = {0, 0, (INT)pw, 1};
    bpp = i == 0 ? 1 : i == 1 ? 3 : 4;
    if (SUCCEEDED(IWICBitmapSource_CopyPixels(cv, &r, pw * bpp, pw * bpp, probe))) break;
    IWICBitmapSource_Release(cv);
    cv = NULL;
  }
  if (SUCCEEDED(hr) && !cv) hr = 0x88982F81;
  BOOL bgr = bpp > 1;
  IWICBitmapSource *use = cv;
  if (SUCCEEDED(hr) && (w > DR_MAXSIDE || h > DR_MAXSIDE)) {
    double s = (double)DR_MAXSIDE / (w > h ? w : h);
    UINT nw = (UINT)(w * s), nh = (UINT)(h * s);
    hr = IWICImagingFactory_CreateBitmapScaler(f, &sc);
    if (SUCCEEDED(hr)) hr = IWICBitmapScaler_Initialize(sc, cv, nw, nh, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(hr)) {
      use = (IWICBitmapSource *)sc;
      w = nw;
      h = nh;
      *dpi = dx * s;
    }
  }
  if (SUCCEEDED(hr) && !dr_img_new(out, (int)w, (int)h)) hr = E_OUTOFMEMORY;
  /* полосами: у больших TIFF одним куском WIC может и не справиться */
  BYTE *strip = bgr && SUCCEEDED(hr) ? (BYTE *)malloc((size_t)w * bpp * 64) : NULL;
  if (bgr && SUCCEEDED(hr) && !strip) hr = E_OUTOFMEMORY;
  for (UINT y = 0; SUCCEEDED(hr) && y < h; y += bgr ? 64 : 512) {
    UINT n = h - y < (bgr ? 64u : 512u) ? h - y : (bgr ? 64u : 512u);
    WICRect r = {0, (INT)y, (INT)w, (INT)n};
    if (!bgr) {
      hr = IWICBitmapSource_CopyPixels(use, &r, w, w * n, out->px + (size_t)y * w);
      continue;
    }
    hr = IWICBitmapSource_CopyPixels(use, &r, w * bpp, w * bpp * n, strip);
    for (size_t i = 0; SUCCEEDED(hr) && i < (size_t)w * n; i++) {
      const BYTE *q = strip + i * bpp;
      out->px[(size_t)y * w + i] = (BYTE)((q[0] * 29 + q[1] * 150 + q[2] * 77) >> 8);
    }
  }
  free(strip);
  if (sc) IWICBitmapScaler_Release(sc);
  if (cv) IWICBitmapSource_Release(cv);
  if (src) IWICBitmapSource_Release(src);
  IWICBitmapFrameDecode_Release(fr);
  IWICBitmapDecoder_Release(dec);
  if (FAILED(hr)) dr_img_free(out);
  return hr;
}

/* порог Оцу: между фоном и чернилами */
static int dr_otsu(const DrImg *m) {
  double hist[256] = {0};
  size_t n = (size_t)m->w * (size_t)m->h, step = n > 4000000 ? 3 : 1;
  double tot = 0;
  for (size_t i = 0; i < n; i += step) hist[m->px[i]]++, tot++;
  double sum = 0;
  for (int i = 0; i < 256; i++) sum += i * hist[i];
  double wb = 0, sb = 0, best = -1;
  int thr = 128;
  for (int t = 0; t < 256; t++) {
    wb += hist[t];
    if (wb == 0) continue;
    double wf = tot - wb;
    if (wf == 0) break;
    sb += t * hist[t];
    double mb = sb / wb, mf = (sum - sb) / wf, v = wb * wf * (mb - mf) * (mb - mf);
    if (v > best) best = v, thr = t;
  }
  if (thr < 80) thr = 80;
  if (thr > 210) thr = 210;
  return thr;
}

/* Перекос. У скана он бывает разным по горизонтали и по вертикали (лист
   повело в сканере), поэтому меряем отдельно: vert = FALSE — по длинным
   горизонтальным отрезкам (линии таблиц, рамка), TRUE — по вертикальным.
   Угол — тот, при котором их точки собираются в самые острые пики. */
static double dr_skew(const DrImg *m, int thr, BOOL vert) {
  int W = vert ? m->h : m->w, H = vert ? m->w : m->h; /* вдоль и поперёк линий */
  int s = (m->w > m->h ? m->w : m->h) / 1400 + 1;
  int mn = m->w < m->h ? m->w : m->h, runMin = mn / 25 > 16 ? mn / 25 : 16; /* длиннее букв */
  int cap = 300000;
  int *pts = (int *)malloc(sizeof(int) * 2 * (size_t)cap);
  if (!pts) return 0;
  int n = 0;
  for (int v = 0; v < H && n < cap; v += s) {
    int start = -1;
    for (int u = 0; u <= W; u++) {
      BOOL d = u < W && (vert ? m->px[(size_t)u * m->w + v] : m->px[(size_t)v * m->w + u]) < thr;
      if (d) {
        if (start < 0) start = u;
        continue;
      }
      if (start >= 0 && u - start >= runMin)
        for (int q = start; q < u && n < cap; q += s) pts[2 * n] = q / s, pts[2 * n + 1] = v / s, n++;
      start = -1;
    }
  }
  int w = W / s, h = H / s;
  if (n < 200) {
    free(pts);
    return 0;
  }
  int nb = h + w;
  double *bins = (double *)malloc(sizeof(double) * (size_t)nb);
  double bestA = 0, bestS = -1;
  if (bins) {
    for (int pass = 0; pass < 2; pass++) {
      double a0 = pass ? bestA - 0.1 : -3.0, a1 = pass ? bestA + 0.1 : 3.0, st = pass ? 0.01 : 0.1;
      for (double a = a0; a <= a1 + 1e-9; a += st) {
        double tt = tan(a * 3.14159265358979 / 180.0);
        memset(bins, 0, sizeof(double) * (size_t)nb);
        for (int i = 0; i < n; i++) {
          int yy = (int)(pts[2 * i + 1] + pts[2 * i] * tt + w / 2 + 0.5);
          if (yy >= 0 && yy < nb) bins[yy]++;
        }
        double sc = 0;
        for (int i = 0; i < nb; i++) sc += bins[i] * bins[i];
        if (sc > bestS) bestS = sc, bestA = a;
      }
    }
  }
  free(bins);
  free(pts);
  return bestA;
}

/* выпрямить: горизонтали — сдвигом по y, вертикали — по x (углы из dr_skew) */
static BOOL dr_deskew(DrImg *m, double ah, double av) {
  if (fabs(ah) < 0.03 && fabs(av) < 0.03) return TRUE;
  DrImg o;
  if (!dr_img_new(&o, m->w, m->h)) return FALSE;
  double th = tan(ah * 3.14159265358979 / 180.0), tv = tan(av * 3.14159265358979 / 180.0);
  double cx = m->w / 2.0, cy = m->h / 2.0;
  for (int y = 0; y < m->h; y++) {
    BYTE *row = o.px + (size_t)y * m->w;
    double xo = 0 - (y - cy) * tv, yo = y + cx * th;
    for (int x = 0; x < m->w; x++, xo += 1, yo -= th) {
      int ix = (int)floor(xo), iy = (int)floor(yo);
      if (ix < 0 || iy < 0 || ix >= m->w - 1 || iy >= m->h - 1) continue;
      double fx = xo - ix, fy = yo - iy;
      const BYTE *p = m->px + (size_t)iy * m->w + ix;
      double v = (p[0] * (1 - fx) + p[1] * fx) * (1 - fy) + (p[m->w] * (1 - fx) + p[m->w + 1] * fx) * fy;
      row[x] = (BYTE)(v + 0.5);
    }
  }
  free(m->px);
  m->px = o.px;
  return TRUE;
}

/* ---- линии --------------------------------------------------------------------- */

typedef struct {
  int y, x0, x1;
} DrRun;

typedef struct {
  DrImg *m;
  int thr;
  BYTE *hm, *vm; /* точки длинных горизонталей и вертикалей */
  DrRun *hr;
  int nhr, caphr;
  int *rowc, *colc; /* сколько точек линий в строке / столбце */
} DrLines;

static void dr_lines_free(DrLines *L) {
  free(L->hm);
  free(L->vm);
  free(L->hr);
  free(L->rowc);
  free(L->colc);
  memset(L, 0, sizeof(*L));
}

static BOOL dr_hrun_add(DrLines *L, int y, int x0, int x1) {
  if (L->nhr >= L->caphr) {
    int c = L->caphr ? L->caphr * 2 : 4096;
    DrRun *q = (DrRun *)realloc(L->hr, sizeof(DrRun) * (size_t)c);
    if (!q) return FALSE;
    L->hr = q;
    L->caphr = c;
  }
  L->hr[L->nhr].y = y, L->hr[L->nhr].x0 = x0, L->hr[L->nhr].x1 = x1, L->nhr++;
  return TRUE;
}

/* Линии на скане бывают рваными: разрывы до ~1 мм прощаются, но в отрезке
   должно быть не меньше 60 % чернил — иначе это строка текста, а не линия. */
static BOOL dr_lines(DrImg *m, int thr, DrLines *L) {
  memset(L, 0, sizeof(*L));
  L->m = m;
  L->thr = thr;
  size_t n = (size_t)m->w * (size_t)m->h;
  L->hm = (BYTE *)calloc(n, 1);
  L->vm = (BYTE *)calloc(n, 1);
  L->rowc = (int *)calloc((size_t)m->h, sizeof(int));
  L->colc = (int *)calloc((size_t)m->w, sizeof(int));
  if (!L->hm || !L->vm || !L->rowc || !L->colc) return FALSE;
  int mn = m->w < m->h ? m->w : m->h;
  int minLen = mn / 45 > 20 ? mn / 45 : 20, gap = mn / 250 + 1;
  for (int y = 0; y < m->h; y++) {
    const BYTE *row = m->px + (size_t)y * m->w;
    int start = -1, last = -1, ink = 0;
    for (int x = 0; x <= m->w; x++) {
      BOOL dark = x < m->w && row[x] < thr;
      if (dark && (start < 0 || x - last <= gap + 1)) {
        if (start < 0) start = x, ink = 0;
        last = x;
        ink++;
        continue;
      }
      int len = last - start + 1;
      if (start >= 0 && len >= minLen && ink * 10 >= len * 6) {
        memset(L->hm + (size_t)y * m->w + start, 1, (size_t)len);
        L->rowc[y] += len;
        if (!dr_hrun_add(L, y, start, last)) return FALSE;
      }
      start = dark ? x : -1;
      last = dark ? x : -1;
      ink = dark ? 1 : 0;
    }
  }
  for (int x = 0; x < m->w; x++) {
    int start = -1, last = -1, ink = 0;
    for (int y = 0; y <= m->h; y++) {
      BOOL dark = y < m->h && m->px[(size_t)y * m->w + x] < thr;
      if (dark && (start < 0 || y - last <= gap + 1)) {
        if (start < 0) start = y, ink = 0;
        last = y;
        ink++;
        continue;
      }
      int len = last - start + 1;
      if (start >= 0 && len >= minLen && ink * 10 >= len * 6) {
        for (int k = start; k <= last; k++) L->vm[(size_t)k * m->w + x] = 1;
        L->colc[x] += len;
      }
      start = dark ? y : -1;
      last = dark ? y : -1;
      ink = dark ? 1 : 0;
    }
  }
  return TRUE;
}

/* от yb вверх по вертикали около xc (±hw): докуда она тянется. Начало должно
   быть рядом с yb (в пределах tol); разрывы до gap точек прощаются. -1 — нет */
static int dr_vup(const DrLines *L, int xc, int hw, int yb, int tol, int gap) {
  const DrImg *m = L->m;
  int x0 = xc - hw < 0 ? 0 : xc - hw, x1 = xc + hw >= m->w ? m->w - 1 : xc + hw;
  if (x0 > x1) return -1;
  int top = -1, miss = 0;
  int y = yb + tol >= m->h ? m->h - 1 : yb + tol;
  for (; y >= 0; y--) {
    BOOL hit = FALSE;
    for (int x = x0; x <= x1 && !hit; x++) hit = L->vm[(size_t)y * m->w + x];
    if (hit) {
      top = y;
      miss = 0;
    } else if (top < 0) {
      if (y < yb - tol) return -1;
    } else if (++miss > gap) {
      break;
    }
  }
  return top;
}

/* есть ли горизонталь около y (±tol), закрывающая frac отрезка [x0, x1] */
static BOOL dr_hline(const DrLines *L, int y, int tol, int x0, int x1, double frac) {
  const DrImg *m = L->m;
  if (x0 < 0) x0 = 0;
  if (x1 >= m->w) x1 = m->w - 1;
  if (x1 <= x0) return FALSE;
  int cov = 0;
  for (int x = x0; x <= x1; x++) {
    BOOL hit = FALSE;
    for (int yy = y - tol; yy <= y + tol && !hit; yy++)
      if (yy >= 0 && yy < m->h) hit = L->hm[(size_t)yy * m->w + x];
    cov += hit;
  }
  return cov >= frac * (x1 - x0 + 1);
}

/* ---- геометрия штампа ------------------------------------------------------------ */

typedef struct {
  int rx, by;   /* внутренний правый нижний угол рамки */
  double k;     /* точек на мм */
  int sl, st;   /* левый верх штампа */
  int form;     /* 1, 2, 3 (2а) или 0 */
  double hmm;   /* высота штампа, мм */
  int fit;      /* сколько вертикалей левой части (0, 17, 40, 55, 65 мм) сошлось с масштабом: из 5 */
} DrStamp;

/* есть ли точка горизонтали в столбце x в строках y±r */
static BOOL dr_hband(const DrLines *L, int y, int x, int r) {
  for (int q = y - r; q <= y + r; q++)
    if (q >= 0 && q < L->m->h && L->hm[(size_t)q * L->m->w + x]) return TRUE;
  return FALSE;
}

static int dr_cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* Сколько вертикалей левой части штампа — 0, 17, 40, 55, 65 мм от его левого края — стоит на месте при левом
   крае sl и масштабе k: в нижних строках (Н.контр., Утв.) они есть у всех форм. Допуск ~1 мм: масштаб, ошибочный
   на 5 % (копия в 95 %), уводит 55 и 65 мм на 3 мм — такой не сойдётся */
static int dr_stamp_fit(const DrLines *L, int sl, int by, double k) {
  static const int X[5] = {0, 17, 40, 55, 65};
  const DrImg *m = L->m;
  int y0 = by - (int)(9 * k), y1 = by - (int)(1.5 * k), tol = (int)(1.0 * k) + 2, fit = 0;
  if (y0 < 0 || y1 <= y0) return 0;
  for (int i = 0; i < 5; i++) {
    int xc = sl + (int)(X[i] * k + 0.5);
    BOOL ok = FALSE;
    for (int x = xc - tol; x <= xc + tol && !ok; x++) {
      if (x < 0 || x >= m->w) continue;
      int cov = 0;
      for (int y = y0; y < y1; y++) cov += L->vm[(size_t)y * m->w + x];
      ok = cov >= 0.7 * (y1 - y0);
    }
    fit += ok;
  }
  return fit;
}

/* штамп при заданных нижней и правой линиях рамки (уже стоим на линии) */
static BOOL dr_stamp_at(const DrLines *L, double dpi, int by, int rx, DrStamp *s, wchar_t *why, int cap) {
  const DrImg *m = L->m;
  memset(s, 0, sizeof(*s));
  int r0 = (m->w < m->h ? m->w : m->h) / 700 + 1;
  /* внутренний край толстой линии */
  while (by > 0 && L->rowc[by - 1] >= 0.12 * m->w) by--;
  while (rx > 0 && L->colc[rx - 1] >= 0.12 * m->h) rx--;
  s->rx = rx;
  s->by = by;
  /* ширина штампа: горизонтали, упёртые в правую рамку; самая «весомая»
     длина (сколько строк × длина) — это 185 мм. Линия скана рвётся на
     куски — идём от рамки влево по точкам линий, прощая разрывы до ~2 мм. */
  int tolT = 4 + m->w / 250, gapW = m->w / 120 + 2;
  int ylo = by - (int)(0.36 * m->h);
  if (ylo < 0) ylo = 0;
  int *len = (int *)malloc(sizeof(int) * (size_t)(by - ylo + 1));
  if (!len) return FALSE;
  int nl = 0, maxl = 0;
  /* полоса ±r0 строк: остаток перекоса уводит тонкую линию на соседние строки */
#define DR_HB(yy, xx) dr_hband(L, yy, xx, r0)
  for (int y = by - 2 - r0; y >= ylo; y--) {
    int x = rx + 2 < m->w ? rx + 2 : m->w - 1, lastHit = -1;
    if (!L->hm[(size_t)y * m->w + (rx - 1 > 0 ? rx - 1 : 0)] && !L->hm[(size_t)y * m->w + (rx - tolT / 2 > 0 ? rx - tolT / 2 : 0)])
      continue; /* сама строка у рамки пустая — не линия */
    for (; x >= rx - tolT && x >= 0; x--)
      if (DR_HB(y, x)) {
        lastHit = x;
        break;
      }
    if (lastHit < 0) continue;
    for (x = lastHit; x >= 0 && lastHit - x <= gapW; x--)
      if (DR_HB(y, x)) lastHit = x;
    int l = rx - lastHit;
    if (l > maxl) maxl = l;
    if (l < m->w / 9 || by - y > 0.36 * l) continue;
    len[nl++] = l;
  }
#undef DR_HB
  BOOL dpiOk = ts_dpi_known(dpi) && dpi >= 100 && dpi <= 1200;
  if (!nl && !dpiOk) {
    free(len);
    _snwprintf(why, cap, L"нет линий штампа у правой рамки");
    return FALSE;
  }
  int Wm = 0;
  if (nl) {
    qsort(len, (size_t)nl, sizeof(int), dr_cmp_int);
    double bestV = -1;
    int bestL = 0;
    for (int i = 0; i < nl; i++) {
      int cnt = 0;
      for (int j = 0; j < nl; j++)
        if (abs(len[j] - len[i]) <= len[i] / 50 + 2) cnt++;
      double v = (double)cnt * len[i];
      if (v > bestV) bestV = v, bestL = len[i];
    }
    /* медиана своей группы */
    int g0 = -1, g1 = -1;
    for (int i = 0; i < nl; i++)
      if (abs(len[i] - bestL) <= bestL / 50 + 2) {
        if (g0 < 0) g0 = i;
        g1 = i;
      }
    Wm = len[(g0 + g1) / 2];
  }
  free(len);
  /* ширина: по dpi скана — если он настоящий; по линиям — если лист копировали с масштабом: копии в 94–95 %
     и уменьшенные форматы (A1 и A2 на A3 — 50 и 71 %, A3 на A4 — 71 %; dpi у такого файла — от меньшего листа).
     Линия у правой рамки бывает и шире штампа (ведомости), поэтому по линиям — только уже, чем по dpi. Какой
     масштаб верный, решают вертикали левой части штампа (dr_stamp_fit), а не первая вертикаль около 65 мм:
     её неверный масштаб тоже находит — и штамп выходил выше («форма 2») или не находился вовсе */
  int Wd = dpiOk ? (int)(185 * dpi / 25.4 + 0.5) : 0, Wc[2], nw = 0;
  if (Wd) Wc[nw++] = Wd;
  if (Wm && (!Wd || (Wm < Wd * 0.98 && Wm > Wd * 0.3))) Wc[nw++] = Wm;
  int top = -1, fit = -1;
  for (int i = 0; i < nw; i++) {
    double kk = Wc[i] / 185.0;
    int sl = rx - Wc[i];
    /* высота: вертикаль в 65 мм от левого края штампа */
    /* ±5 мм: старые бланки и покоробленный скан расходятся с ГОСТ на 2–3 мм */
    int x65 = sl + (int)(65 * kk + 0.5), hw = (int)(5 * kk) + 1, tol = (int)(2 * kk) + 2;
    int t = dr_vup(L, x65, hw, by, tol, (int)(1.2 * kk) + 2), f = t >= 0 ? dr_stamp_fit(L, sl, by, kk) : -1;
    if (t >= 0 && f > fit) top = t, fit = f, s->k = kk, s->sl = sl;
  }
  s->fit = fit;
  if (top < 0) {
    _snwprintf(why, cap, L"не найдена граница граф штампа");
    return FALSE;
  }
  s->hmm = (by - top) / s->k;
  if (s->hmm >= 47 && s->hmm <= 63) s->form = 1;
  else if (s->hmm >= 32 && s->hmm < 47) s->form = 2;
  else if (s->hmm >= 10 && s->hmm <= 22) s->form = 3;
  if (!s->form) {
    _snwprintf(why, cap, L"штамп непонятной высоты (%.0f мм)", s->hmm);
    return FALSE;
  }
  static const double kH[4] = {0, 55, 40, 15};
  s->st = by - (int)(kH[s->form] * s->k + 0.5);
  return TRUE;
}

/* Рамка листа: самые нижняя и правая длинные линии. На копиях под рамкой и
   справа бывает ещё край копии — поэтому до трёх кандидатов с каждой стороны
   (от края внутрь), и штамп ищется у каждой пары. */
static int dr_frame_lines(const DrLines *L, BOOL rows, int out[3]) {
  const DrImg *m = L->m;
  int n = 0, len = rows ? m->h : m->w, across = rows ? m->w : m->h;
  const int *cnt = rows ? L->rowc : L->colc;
  int r = (m->w < m->h ? m->w : m->h) / 700 + 1, skip = len / 200 + 2;
  for (double frac = 0.3; frac > 0.12 && !n; frac -= 0.15)
    for (int i = len - 1 - r; i >= len * 6 / 10 && n < 3; i--) {
      int sum = 0;
      for (int q = i - r; q <= i + r; q++) sum += cnt[q];
      if (sum < frac * across) continue;
      int b = i; /* окно нашло линию где-то рядом — встаём на саму линию */
      for (int q = i - r; q <= i + r; q++)
        if (q >= 0 && q < len && cnt[q] > cnt[b]) b = q;
      out[n++] = b;
      i = b - skip;
    }
  return n;
}

static BOOL dr_find_stamp(const DrLines *L, double dpi, DrStamp *s, wchar_t *why, int cap) {
  memset(s, 0, sizeof(*s));
  int byc[3], rxc[3];
  int nb = dr_frame_lines(L, TRUE, byc), nr = dr_frame_lines(L, FALSE, rxc);
  if (!nb || !nr) {
    _snwprintf(why, cap, L"рамка листа не найдена");
    return FALSE;
  }
  /* низ — первая снаружи нижняя линия, у которой штамп нашёлся (у линии обреза копии вверх нет вертикали, а
     внутренние линии штампа лежат выше рамки); правый край при этом низе — где вертикали левой части сошлись
     лучше: на копиях за рамкой отпечатан обрез листа, и штамп от него сдвинут на 3–5 мм */
  DrStamp t;
  BOOL got = FALSE;
  wchar_t w[200];
  for (int i = 0; i < nb && !got; i++)
    for (int j = 0; j < nr; j++)
      if (dr_stamp_at(L, dpi, byc[i], rxc[j], &t, w, 200) && (!got || t.fit > s->fit)) *s = t, got = TRUE;
      else if (!got) lstrcpynW(why, w, cap);
  return got;
}

/* ---- вырезки и Tesseract ------------------------------------------------------- */

typedef struct {
  double x0, y0, x1, y1; /* мм от левого верха штампа */
  int field;
} DrBox;

/* графы — по ГОСТ, мм от левого верха штампа; края потом прижимаются к
   найденным линиям (старые бланки расходятся с ГОСТ на 2–3 мм) */
static const DrBox kDrForm1[] = {
    {65, 0, 185, 15, DF_DES},      {65, 15, 135, 40, DF_NAME},     {65, 40, 135, 55, DF_MAT},
    {150, 20, 167, 35, DF_MASS},   {167, 20, 185, 35, DF_SCALE},   {135, 35, 155, 40, DF_SHEET},
    {155, 35, 185, 40, DF_SHEETS},
};
static const DrBox kDrForm2[] = {
    {65, 0, 185, 15, DF_DES},
    {65, 15, 135, 40, DF_NAME},
    {150, 20, 165, 25, DF_SHEET},
    {165, 20, 185, 25, DF_SHEETS},
};
static const DrBox kDrForm2a[] = {
    {65, 0, 175, 15, DF_DES},
    {175, 7, 185, 15, DF_SHEET},
};

/* край графы x — к вертикали поблизости (в строках y0…y1), если она есть */
static int dr_snap_x(const DrLines *L, int x, int y0, int y1, int rad) {
  const DrImg *m = L->m;
  int best = x, bestN = (y1 - y0) * 85 / 100; /* во всю высоту графы: буквы так не умеют */
  for (int c = x - rad; c <= x + rad; c++) {
    if (c < 0 || c >= m->w) continue;
    int n = 0;
    for (int y = y0; y < y1; y++)
      if (y >= 0 && y < m->h) n += L->vm[(size_t)y * m->w + c];
    if (n > bestN || (n == bestN && n > 0 && abs(c - x) < abs(best - x))) bestN = n, best = c;
  }
  return best;
}

static int dr_snap_y(const DrLines *L, int y, int x0, int x1, int rad) {
  const DrImg *m = L->m;
  int best = y, bestN = (x1 - x0) * 85 / 100;
  for (int r = y - rad; r <= y + rad; r++) {
    if (r < 0 || r >= m->h) continue;
    int n = 0;
    for (int x = x0; x < x1; x++)
      if (x >= 0 && x < m->w) n += L->hm[(size_t)r * m->w + x];
    if (n > bestN || (n == bestN && n > 0 && abs(r - y) < abs(best - y))) bestN = n, best = r;
  }
  return best;
}

/* графа → прямоугольник вырезки в точках: прижать к линиям, отступить внутрь */
static void dr_box_rect(const DrLines *L, const DrStamp *s, const DrBox *b, int *rx0, int *ry0, int *rx1, int *ry1) {
  double k = s->k;
  int x0 = s->sl + (int)(b->x0 * k), x1 = s->sl + (int)(b->x1 * k);
  int y0 = s->st + (int)(b->y0 * k), y1 = s->st + (int)(b->y1 * k);
  /* искать линию не дальше четверти графы: узкая «Лист» не должна уехать на соседнюю */
  int radx = (int)(4 * k), rady = (int)((b->y1 - b->y0) * k / 4);
  if (radx > (x1 - x0) / 4) radx = (x1 - x0) / 4;
  if (rady > radx) rady = radx;
  int mx = (int)(0.4 * k), my = (int)(0.4 * k);
  int nx0 = dr_snap_x(L, x0, y0 + my, y1 - my, radx), nx1 = dr_snap_x(L, x1, y0 + my, y1 - my, radx);
  int ny0 = dr_snap_y(L, y0, x0 + mx, x1 - mx, rady), ny1 = dr_snap_y(L, y1, x0 + mx, x1 - mx, rady);
  int in = (int)(0.35 * k) + 1; /* остатки линий у края стирает dr_crop */
  *rx0 = nx0 + in;
  *rx1 = nx1 - in;
  *ry0 = ny0 + in;
  *ry1 = ny1 - in;
}

/* вырезка из страницы: линии длиннее lineMin (точек) стираются, мелкое —
   увеличивается до ~12 точек на мм (Tesseract любит буквы от 25 точек) */
/* выпрямить курсив: строка y сдвигается вправо на t·y, ширина растёт на t·h, фон белый. Точка (x, y) после —
   это (x − t·y, y) до. Шрифт ГОСТ 2.304 тип Б наклонён на 15°: Tesseract на нём теряет точки обозначения
   («3487.00.00.01» → «3487000001») и путает «1» с «7», «Д» с «М»; выпрямленный читает почти без ошибок */
static BOOL dr_img_shear(DrImg *im, double t) {
  DrImg o;
  if (!dr_img_new(&o, im->w + (int)(t * im->h) + 1, im->h)) return FALSE;
  memset(o.px, 255, (size_t)o.w * o.h);
  for (int y = 0; y < im->h; y++) memcpy(o.px + (size_t)y * o.w + (int)(t * y + 0.5), im->px + (size_t)y * im->w, (size_t)im->w);
  dr_img_free(im);
  *im = o;
  return TRUE;
}

static BOOL dr_crop(const DrImg *m, int thr, int x0, int y0, int x1, int y1, double k, DrImg *out) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > m->w) x1 = m->w;
  if (y1 > m->h) y1 = m->h;
  int w = x1 - x0, h = y1 - y0;
  if (w < 4 || h < 4) return FALSE;
  double up = k < 9 ? 11.8 / k : 1.0;
  int pad = 12;
  int ow = (int)(w * up) + 2 * pad, oh = (int)(h * up) + 2 * pad;
  if (!dr_img_new(out, ow, oh)) return FALSE;
  DrImg tmp;
  if (!dr_img_new(&tmp, w, h)) {
    dr_img_free(out);
    return FALSE;
  }
  for (int y = 0; y < h; y++) memcpy(tmp.px + (size_t)y * w, m->px + (size_t)(y0 + y) * m->w + x0, (size_t)w);
  /* стереть линии: горизонтали длиннее 9 мм, вертикали длиннее 8,5 мм
     (буквы — до 7 мм); подчёркивание и дробь в материале — тоже линии */
  int lh = (int)(9 * k), lv = (int)(8.5 * k), gap = (int)(0.3 * k) + 1;
  /* у края вырезки — остатки рамки графы: там линией считаем и короткое, от 60 % */
  int bd = (int)(1.2 * k) + 1, lhb = w * 6 / 10, lvb = h * 6 / 10;
  if (lhb > lh) lhb = lh;
  if (lvb > lv) lvb = lv;
  BYTE *kill = (BYTE *)calloc((size_t)w * h, 1);
  if (kill) {
    for (int y = 0; y < h; y++) {
      int s0 = -1, last = -1;
      for (int x = 0; x <= w; x++) {
        BOOL d = x < w && tmp.px[(size_t)y * w + x] < thr;
        if (d && (s0 < 0 || x - last <= gap + 1)) {
          if (s0 < 0) s0 = x;
          last = x;
          continue;
        }
        if (s0 >= 0 && last - s0 + 1 >= (y < bd || y >= h - bd ? lhb : lh)) memset(kill + (size_t)y * w + s0, 1, (size_t)(last - s0 + 1));
        s0 = d ? x : -1;
        last = d ? x : -1;
      }
    }
    for (int x = 0; x < w; x++) {
      int s0 = -1, last = -1;
      for (int y = 0; y <= h; y++) {
        BOOL d = y < h && tmp.px[(size_t)y * w + x] < thr;
        if (d && (s0 < 0 || y - last <= gap + 1)) {
          if (s0 < 0) s0 = y;
          last = y;
          continue;
        }
        if (s0 >= 0 && last - s0 + 1 >= (x < bd || x >= w - bd ? lvb : lv))
          for (int q = s0; q <= last; q++) kill[(size_t)q * w + x] = 1;
        s0 = d ? y : -1;
        last = d ? y : -1;
      }
    }
    /* край линии на скане серый и светлее порога — стираем и по точке-две вокруг */
    int dl = (int)(0.15 * k) + 1;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if (kill[(size_t)y * w + x] == 1)
          for (int dy = -dl; dy <= dl; dy++)
            for (int dx = -dl; dx <= dl; dx++) {
              int yy = y + dy, xx = x + dx;
              if (yy >= 0 && yy < h && xx >= 0 && xx < w && !kill[(size_t)yy * w + xx]) kill[(size_t)yy * w + xx] = 2;
            }
    for (size_t i = 0; i < (size_t)w * h; i++)
      if (kill[i]) tmp.px[i] = 255;
    /* пыль: одиночные тёмные точки (меньше 0,2 мм) */
    int dust = (int)(0.2 * k);
    if (dust >= 2)
      for (int y = 1; y < h - 1; y++)
        for (int x = 1; x < w - 1; x++) {
          size_t i = (size_t)y * w + x;
          if (tmp.px[i] >= thr) continue;
          int nb = (tmp.px[i - 1] < thr) + (tmp.px[i + 1] < thr) + (tmp.px[i - w] < thr) + (tmp.px[i + w] < thr);
          if (!nb) tmp.px[i] = 255;
        }
    free(kill);
  }
  for (int y = 0; y < oh - 2 * pad; y++) {
    double sy = (y + 0.5) / up - 0.5;
    int iy = (int)floor(sy);
    double fy = sy - iy;
    if (iy < 0) iy = 0, fy = 0;
    if (iy >= h - 1) iy = h - 2 < 0 ? 0 : h - 2, fy = h > 1 ? 1 : 0;
    BYTE *row = out->px + (size_t)(y + pad) * ow + pad;
    for (int x = 0; x < ow - 2 * pad; x++) {
      double sx = (x + 0.5) / up - 0.5;
      int ix = (int)floor(sx);
      double fx = sx - ix;
      if (ix < 0) ix = 0, fx = 0;
      if (ix >= w - 1) ix = w - 2 < 0 ? 0 : w - 2, fx = w > 1 ? 1 : 0;
      const BYTE *p = tmp.px + (size_t)iy * w + ix;
      double v = up == 1.0 ? p[0]
                           : (p[0] * (1 - fx) + p[w > 1 ? 1 : 0] * fx) * (1 - fy) +
                                 (p[h > 1 ? w : 0] * (1 - fx) + p[(h > 1 ? w : 0) + (w > 1 ? 1 : 0)] * fx) * fy;
      row[x] = (BYTE)(v + 0.5);
    }
  }
  dr_img_free(&tmp);
  return TRUE;
}

/* 8-битный BMP с серой палитрой; dpi 300 — чтобы Tesseract не гадал */
static BOOL dr_save_bmp(const DrImg *m, const wchar_t *path) {
  int stride = (m->w + 3) & ~3;
  DWORD imgSize = (DWORD)stride * (DWORD)m->h;
  BITMAPFILEHEADER fh;
  BITMAPINFOHEADER ih;
  memset(&fh, 0, sizeof(fh));
  memset(&ih, 0, sizeof(ih));
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(ih) + 1024;
  fh.bfSize = fh.bfOffBits + imgSize;
  ih.biSize = sizeof(ih);
  ih.biWidth = m->w;
  ih.biHeight = m->h;
  ih.biPlanes = 1;
  ih.biBitCount = 8;
  ih.biSizeImage = imgSize;
  ih.biXPelsPerMeter = ih.biYPelsPerMeter = 11811;
  ih.biClrUsed = 256;
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  DWORD wr;
  BOOL ok = WriteFile(f, &fh, sizeof(fh), &wr, NULL) && WriteFile(f, &ih, sizeof(ih), &wr, NULL);
  BYTE pal[1024];
  for (int i = 0; i < 256; i++) pal[i * 4] = pal[i * 4 + 1] = pal[i * 4 + 2] = (BYTE)i, pal[i * 4 + 3] = 0;
  ok = ok && WriteFile(f, pal, 1024, &wr, NULL);
  BYTE *row = (BYTE *)calloc((size_t)stride, 1);
  for (int y = m->h - 1; ok && row && y >= 0; y--) {
    memcpy(row, m->px + (size_t)y * m->w, (size_t)m->w);
    ok = WriteFile(f, row, (DWORD)stride, &wr, NULL);
  }
  free(row);
  CloseHandle(f);
  return ok && row;
}

typedef struct {
  int page, line; /* вырезка (с 1) и строка в ней: блок·абзац·строка */
  int x, y, w, h; /* в точках вырезки */
  int conf;
  wchar_t t[64];
} DrWord;

typedef struct {
  DrWord *w;
  int n, cap;
} DrWords;

static void dr_words_free(DrWords *ws) {
  free(ws->w);
  memset(ws, 0, sizeof(*ws));
}

/* TSV Tesseract: level page block par line word left top width height conf text */
static void dr_parse_tsv(const char *tsv, DrWords *ws) {
  const char *p = tsv;
  while (p && *p) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    char line[600];
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    if (len && line[len - 1] == '\r') line[--len] = 0;
    p = nl ? nl + 1 : NULL;
    char *f[12];
    int n = 0;
    f[n++] = line;
    for (char *q = line; *q && n < 12; q++)
      if (*q == '\t') *q = 0, f[n++] = q + 1;
    if (n < 12 || strcmp(f[0], "5")) continue;
    const char *txt = f[11];
    while (*txt == ' ') txt++;
    if (!*txt) continue;
    if (ws->n >= ws->cap) {
      int c = ws->cap ? ws->cap * 2 : 256;
      DrWord *q = (DrWord *)realloc(ws->w, sizeof(DrWord) * (size_t)c);
      if (!q) return;
      ws->w = q;
      ws->cap = c;
    }
    DrWord *w = &ws->w[ws->n++];
    w->page = atoi(f[1]);
    w->line = atoi(f[2]) * 10000 + atoi(f[3]) * 100 + atoi(f[4]);
    w->x = atoi(f[6]);
    w->y = atoi(f[7]);
    w->w = atoi(f[8]);
    w->h = atoi(f[9]);
    w->conf = (int)atof(f[10]);
    if (!MultiByteToWideChar(CP_UTF8, 0, txt, -1, w->t, 64)) w->t[0] = 0;
    w->t[63] = 0;
  }
}

/* рабочая папка для вырезок: в папке Tesseract (пути без русских букв),
   нельзя — во временной */
static BOOL dr_workdir(const wchar_t *tdir, wchar_t *abs, wchar_t *rel) {
  _snwprintf(rel, 40, L"drw-%lu-%lu", GetCurrentThreadId(), GetTickCount());
  _snwprintf(abs, MAX_PATH, L"%s\\%s", tdir, rel);
  if (CreateDirectoryW(abs, NULL)) return TRUE;
  wchar_t tmp[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  _snwprintf(abs, MAX_PATH, L"%s%s", tmp, rel);
  lstrcpynW(rel, abs, MAX_PATH);
  return CreateDirectoryW(abs, NULL);
}

static void dr_rmdir(const wchar_t *dir) {
  if (getenv("CP_DRDEBUG")) return; /* проверка: вырезки оставить */
  wchar_t mask[MAX_PATH];
  _snwprintf(mask, MAX_PATH, L"%s\\*", dir);
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(mask, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      wchar_t p[MAX_PATH];
      _snwprintf(p, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
      DeleteFileW(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryW(dir);
}

/* Вырезки → слова. Все одним запуском: список файлов на входе, TSV на выходе.
   psm — как Tesseract делит вырезку (6 — блок, 3 — сам ищет колонки). */
static BOOL dr_tess(DrImg *crops, int n, int psm, DrWords *ws, wchar_t *why, int cap) {
  wchar_t tdir[MAX_PATH];
  ocr_engine_load();
  if (!tess_find(tdir)) {
    _snwprintf(why, cap, L"нет Tesseract — «Проверить обновления» докачает его");
    return FALSE;
  }
  wchar_t wabs[MAX_PATH], wrel[MAX_PATH];
  if (!dr_workdir(tdir, wabs, wrel)) {
    _snwprintf(why, cap, L"не создать папку для вырезок");
    return FALSE;
  }
  char list[16384];
  int ll = 0;
  for (int i = 0; i < n; i++) {
    wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%s\\c%02d.bmp", wabs, i);
    if (!dr_save_bmp(&crops[i], p)) continue;
    _snwprintf(p, MAX_PATH, L"%s\\c%02d.bmp", wrel, i);
    char u[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, p, -1, u, sizeof(u), NULL, NULL);
    ll += _snprintf(list + ll, sizeof(list) - ll - 1, "%s\n", u);
  }
  list[ll] = 0;
  wchar_t lp[MAX_PATH];
  _snwprintf(lp, MAX_PATH, L"%s\\list.txt", wabs);
  HANDLE lf = CreateFileW(lp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (lf != INVALID_HANDLE_VALUE) {
    DWORD wr;
    WriteFile(lf, list, (DWORD)ll, &wr, NULL);
    CloseHandle(lf);
  }
  wchar_t langs[24] = L"rus", td[MAX_PATH], cmd[1400];
  _snwprintf(td, MAX_PATH, L"%s\\tessdata\\eng.traineddata", tdir);
  if (GetFileAttributesW(td) != INVALID_FILE_ATTRIBUTES) lstrcpyW(langs, L"rus+eng");
  _snwprintf(cmd, 1400, L"\"%s\\tesseract.exe\" \"%s\\list.txt\" stdout --tessdata-dir tessdata -l %s --psm %d -c tessedit_create_tsv=1",
             tdir, wrel, langs, psm);
  char *out = NULL, err[256] = {0};
  DWORD on = 0, code = 0;
  BOOL ran = run_capture_in(cmd, tdir, &out, &on, &code, err, 256);
  dr_rmdir(wabs);
  if (!ran || code != 0 || !out) {
    free(out);
    _snwprintf(why, cap, L"Tesseract не сработал (код %lu)", code);
    return FALSE;
  }
  dr_parse_tsv(out, ws);
  free(out);
  return TRUE;
}

/* текст вырезки page: слова по строкам, строки через sep; *minConf — худшее слово */
static void dr_page_text(const DrWords *ws, int page, const wchar_t *sep, wchar_t *out, int cap, int *minConf) {
  out[0] = 0;
  *minConf = 100;
  int n = 0, lastLine = -1;
  for (int i = 0; i < ws->n; i++) {
    const DrWord *w = &ws->w[i];
    if (w->page != page) continue;
    /* мусор от остатков линий: одиночные «|», «_», «—» */
    if (!w->t[1] && wcschr(L"|_—-–=~'\"`.,:;!", w->t[0])) continue;
    if (w->conf < *minConf) *minConf = w->conf;
    const wchar_t *s = n == 0 ? L"" : w->line != lastLine ? sep : L" ";
    n += _snwprintf(out + n, cap - n, L"%s%s", s, w->t);
    if (n >= cap - 1) {
      out[cap - 1] = 0;
      break;
    }
    lastLine = w->line;
  }
  if (!out[0]) *minConf = 100;
}

/* ---- проверка полей ---------------------------------------------------------------- */

/* шаблон обозначения (dr_des_core и его буквы-цифры) — в search_route.c: по нему же
   поле поиска узнаёт обозначение в запросе */

/* короткое обозначение без точек «3422-682» — только за буквенным кодом («АДЕ 3422-682»):
   без кода это номер ГОСТ; code — начало буквенного кода, «ГОСТ 1050-13» и «ОСТ …» не берём */
static int dr_des_short(const wchar_t *s, int code, int i) {
  static const wchar_t *const std[] = {L"ГОСТ ", L"ОСТ ", L"СТБ ", L"СТО ", L"СТП ", L"ТУ "};
  for (int k = 0; k < (int)(sizeof(std) / sizeof(std[0])); k++)
    if (!wcsncmp(s + code, std[k], wcslen(std[k]))) return -1;
  int j = dr_digits(s, i, 4, 4);
  if (j < 0 || s[j] != L'-') return -1;
  int k = dr_digits(s, j + 1, 2, 4);
  return k > 0 && s[k] != L'.' && s[k] != L'-' ? k : -1;
}

/* всё обозначение целиком: [буквы ]ядро[-исп][ шифр] */
static BOOL dr_des_ok(const wchar_t *s) {
  int i = 0;
  int l = 0;
  while (dr_isupper_cyr(s[l])) l++;
  if (l >= 2 && l <= 5 && s[l] == L' ') i = l + 1;
  int e = dr_des_core(s, i);
  if (e < 0 && i) e = dr_des_short(s, 0, i);
  if (e < 0) return FALSE;
  if (s[e] == L'-' && dr_isdig(s[e + 1])) {
    int q = dr_digits(s, e + 1, 2, 3);
    if (q < 0) return FALSE;
    e = q;
  }
  if (s[e] == L' ') e++;
  int sl = 0;
  while (dr_isupper_cyr(s[e + sl]) || (sl > 0 && dr_isdig(s[e + sl]))) sl++;
  if (sl > 4) return FALSE;
  return s[e + sl] == 0;
}

/* Латиница, похожая на кириллицу, — в буквенных кусках; цифровые куски —
   через ocr_fix_query (О→0, З→3…); пробелы схлопнуть */
static void dr_norm_des(const wchar_t *in, wchar_t *out, int cap) {
  static const wchar_t lat[] = L"ABCEHKMOPTXaceopxy", cyr[] = L"АВСЕНКМОРТХасеорху";
  wchar_t t[DR_CELL];
  int n = 0;
  BOOL sp = FALSE;
  for (int i = 0; in[i] && n < DR_CELL - 1; i++) {
    wchar_t c = in[i];
    if (c == L',') c = L'.';
    if (c == L'—' || c == L'–') c = L'-';
    if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
      sp = n > 0;
      continue;
    }
    if (sp) t[n++] = L' ', sp = FALSE;
    t[n++] = c;
  }
  t[n] = 0;
  /* латиница в обозначении — это кириллица («MK» → «МК»); кусок без цифр —
     буквенный код, он прописными */
  for (int k = 0; t[k]; k++) {
    const wchar_t *q = wcschr(lat, t[k]);
    if (q) t[k] = cyr[q - lat];
  }
  /* буквенный код, прилипший к цифрам: «АДЕ3422-682.01.01» → «АДЕ 3422-682.01.01» */
  for (int i = 0; t[i]; i++) {
    if (i > 0 && t[i - 1] != L' ') continue;
    int l = 0;
    while (dr_isupper_cyr(t[i + l]) && l < 6) l++;
    if (l < 2 || l > 5 || dr_digits(t, i + l, 3, 5) < 0 || t[dr_digits(t, i + l, 3, 5)] != L'-' || n + 1 >= DR_CELL)
      continue;
    memmove(t + i + l + 1, t + i + l, (size_t)(n - i - l + 1) * sizeof(wchar_t));
    t[i + l] = L' ';
    n++;
  }
  for (int i = 0; t[i];) {
    int j = i;
    BOOL dig = FALSE;
    while (t[j] && t[j] != L' ') dig |= dr_isdig(t[j]), j++;
    if (!dig && j - i <= 5)
      for (int k = i; k < j; k++)
        if (t[k] >= L'а' && t[k] <= L'я') t[k] = (wchar_t)(t[k] - 0x20);
    i = t[j] ? j + 1 : j;
  }
  /* «сб» в «830-01-сб115» — буквы, хоть и рядом с цифрами: прячем от исправления */
  for (int i = 0; t[i] && t[i + 1]; i++)
    if ((t[i] == L'с' || t[i] == L'С') && (t[i + 1] == L'б' || t[i + 1] == L'Б') && i > 0 && t[i - 1] == L'-')
      t[i] = t[i] == L'с' ? 0xE000 : 0xE001, t[i + 1] = t[i + 1] == L'б' ? 0xE002 : 0xE003;
  ocr_fix_query(t, out, cap);
  for (int i = 0; out[i]; i++)
    if (out[i] >= 0xE000 && out[i] <= 0xE003) out[i] = L"сСбБ"[out[i] - 0xE000];
  /* «б» в «СБ» распознавание (и Tesseract, и модель) часто отдаёт цифрой: «321-23-с632» */
  if (wcsstr(out, L"с6") || wcsstr(out, L"С6")) {
    wchar_t was[DR_CELL];
    lstrcpynW(was, out, DR_CELL);
    if (dr_des_fix_sb(out)) ai_log(L"Обозначение: «%s» → «%s» — «б» в «СБ» было прочитано как «6»", was, out);
  }
  /* код документа «ЗК» после точки: «.3 К», «.3К» → «.ЗК» */
  for (int i = 1; out[i]; i++)
    if (out[i - 1] == L'.' && (out[i] == L'3' || out[i] == L'З')) {
      int j = i + 1 + (out[i + 1] == L' ');
      if (out[j] == L'К' && (!out[j + 1] || out[j + 1] == L' ')) {
        out[i] = L'З';
        memmove(out + i + 1, out + j, (wcslen(out + j) + 1) * sizeof(wchar_t));
      }
    }
  /* «СБ» в хвосте, прочитанное «Сb», «(Б», «СЬ» */
  int l = (int)wcslen(out), k = l;
  if (l >= 3 && dr_isdig(out[l - 3]) && wcschr(L"Сс(", out[l - 2]) && wcschr(L"bБЬь", out[l - 1]))
    out[l - 2] = L'С', out[l - 1] = L'Б';
  /* хвост из букв, прилипший к цифрам: «…01.02СБ» → «…01.02 СБ» */
  while (k > 0 && dr_isupper_cyr(out[k - 1])) k--;
  if (k > 0 && k < l && l - k <= 3 && dr_isdig(out[k - 1]) && l + 1 < cap) {
    memmove(out + k + 1, out + k, (size_t)(l - k + 1) * sizeof(wchar_t));
    out[k] = L' ';
  }
  /* свой буквенный код, прочитанный не так (Tesseract на курсиве: «Д» → «Л»): к своему — из Настроек или
     взявшемуся сам по именам архива */
  wchar_t codes[SR_CODES][8];
  int nc = files_des_codes(codes, SR_CODES);
  /* код — первое слово из букв перед номером, хоть бы перед ним и был мусор («¢ АNЕ 3426-…») */
  for (int i = 0; nc && out[i]; i++) {
    if (i && out[i - 1] != L' ') continue;
    int l = 0;
    while (l < 6 && (dr_isupper_cyr(out[i + l]) || (out[i + l] >= L'A' && out[i + l] <= L'Z'))) l++;
    if (l < 2 || !(dr_isdig(out[i + l]) || (out[i + l] == L' ' && dr_isdig(out[i + l + 1])))) continue;
    wchar_t was[DR_CELL];
    lstrcpynW(was, out, DR_CELL);
    if (dr_des_fix_code(out + i, cap - i, codes, nc)) ai_log(L"Обозначение: «%s» → «%s» — буквенный код по своим", was, out);
    break;
  }
}

/* найти обозначение в свободном тексте (ТП, «Прочее») */
static BOOL dr_find_des(const wchar_t *text, wchar_t *out, int cap) {
  wchar_t fx[4096];
  dr_norm_des(text, fx, 4096);
  for (int i = 0; fx[i]; i++) {
    if (!dr_isdig(fx[i]) && !dr_isupper_cyr(fx[i])) continue;
    if (i > 0 && (dr_isdig(fx[i - 1]) || dr_isupper_cyr(fx[i - 1]) || fx[i - 1] == L'.')) continue;
    int e = dr_des_core(fx, i);
    /* «АДЕ 3422-682»: короткое — только за буквенным кодом */
    if (e < 0 && i >= 3 && fx[i - 1] == L' ' && dr_isupper_cyr(fx[i - 2])) {
      int q = i - 1;
      while (q > 0 && dr_isupper_cyr(fx[q - 1])) q--;
      if (i - 1 - q >= 2 && i - 1 - q <= 5 && (q == 0 || fx[q - 1] == L' ')) e = dr_des_short(fx, q, i);
    }
    if (e < 0) continue;
    int b = i;
    /* буквенный код перед ним */
    if (b >= 3 && fx[b - 1] == L' ') {
      int q = b - 1;
      while (q > 0 && dr_isupper_cyr(fx[q - 1])) q--;
      if (b - 1 - q >= 2 && b - 1 - q <= 5 && (q == 0 || fx[q - 1] == L' ')) b = q;
    }
    if (fx[e] == L'-' && dr_isdig(fx[e + 1])) {
      int q = dr_digits(fx, e + 1, 2, 3);
      if (q > 0) e = q;
    }
    int n = e - b < cap - 1 ? e - b : cap - 1;
    memcpy(out, fx + b, (size_t)n * sizeof(wchar_t));
    out[n] = 0;
    return TRUE;
  }
  out[0] = 0;
  return FALSE;
}

/* число из ячейки: «0,85», «12» — цифры с одной запятой; '.' → ',' */
static BOOL dr_number(const wchar_t *in, wchar_t *out, int cap, BOOL frac) {
  int n = 0;
  BOOL comma = FALSE, dig = FALSE;
  for (int i = 0; in[i] && n < cap - 1; i++) {
    wchar_t c = ocr_digitize(in[i]);
    if (dr_isdig(c)) {
      out[n++] = c;
      dig = TRUE;
    } else if ((c == L',' || c == L'.') && frac && dig && !comma) {
      out[n++] = L',';
      comma = TRUE;
    } else if (c == L' ' && !dig) {
      continue;
    } else if (dig) {
      break;
    }
  }
  if (n && out[n - 1] == L',') n--;
  out[n] = 0;
  return dig;
}

/* масса в граммах «192.4 г» (бывает у мелких деталей) → килограммы «0,1924»;
   raw — ячейка как прочитана, num — число из неё */
static void dr_mass_kg(const wchar_t *raw, wchar_t *num, int cap) {
  int d = -1;
  for (int i = 0; raw[i]; i++)
    if (dr_isdig(ocr_digitize(raw[i]))) d = i;
  if (d < 0 || !num[0]) return;
  const wchar_t *u = raw + d + 1;
  while (*u == L' ') u++;
  if (!(*u == L'г' || *u == L'Г') || (u[1] >= L'а' && u[1] <= L'я') || (u[1] >= L'А' && u[1] <= L'Я')) return;
  wchar_t b[32];
  lstrcpynW(b, num, 32);
  wchar_t *c = wcschr(b, L',');
  if (c) *c = L'.';
  double kg = wcstod(b, NULL) / 1000;
  if (kg <= 0) return;
  _snwprintf(num, cap, L"%.6f", kg);
  num[cap - 1] = 0;
  int l = (int)wcslen(num);
  while (l > 1 && num[l - 1] == L'0') num[--l] = 0;
  if (l > 0 && num[l - 1] == L'.') num[--l] = 0;
  c = wcschr(num, L'.');
  if (c) *c = L',';
}

/* «Лист 2», «Листов 12» → «2», «12»: слово отбросить, взять цифры */
static void dr_sheetnum(wchar_t *s) {
  wchar_t d[16];
  /* машинописная «I» и «О» — тоже цифры, но только в отдельном слове из них */
  for (int i = 0; s[i];) {
    int j = i;
    BOOL digitish = TRUE;
    while (s[j] && s[j] != L' ') digitish &= dr_isdig(s[j]) || ocr_digitize(s[j]) != s[j], j++;
    if (digitish)
      for (int q = i; q < j; q++) s[q] = ocr_digitize(s[q]);
    i = s[j] ? j + 1 : j;
  }
  const wchar_t *p = s;
  while (*p && !dr_isdig(*p)) p++;
  if (!*p) {
    s[0] = 0;
    return;
  }
  dr_number(p, d, 16, FALSE);
  lstrcpyW(s, d);
}

static void dr_trim(wchar_t *s) {
  /* по краям — остатки линий и рамок: «|», «_», «>»… */
  size_t l = wcslen(s), a = 0;
  while (a < l && wcschr(L" |_>~=—–-.,:;'\"`", s[a])) a++;
  while (l > a && wcschr(L" |_>~=—–-,:;'\"`", s[l - 1])) l--;
  memmove(s, s + a, (l - a) * sizeof(wchar_t));
  s[l - a] = 0;
}

/* формат листа в спецификации: «А3», «А4х3», «БЧ» (латиница, З вместо 3) */
static void dr_norm_fmt(wchar_t *s) {
  wchar_t t[DR_CELL];
  int n = 0;
  /* «без чертежа»: «Б\Ч», «б|ч», «Б/Ч», «Б14» (палочка и Ч, прочитанные цифрами) — «БЧ» */
  if (s[0] == L'Б' || s[0] == L'б') {
    int l = (int)wcslen(s);
    while (l > 1 && s[l - 1] == L' ') l--;
    wchar_t e = s[l - 1];
    BOOL mid = TRUE;
    for (int i = 1; i < l - 1; i++) mid &= wcschr(L"\\|/1lI ", s[i]) != NULL;
    if (l >= 2 && l <= 4 && mid && (e == L'Ч' || e == L'ч' || e == L'4')) {
      lstrcpyW(s, L"БЧ");
      return;
    }
  }
  for (int i = 0; s[i] && n < DR_CELL - 1; i++) {
    wchar_t c = s[i];
    if (c == L' ') continue;
    if (c == L'A' || c == L'a' || c == L'а') c = L'А';
    else if (c == L'x' || c == L'X' || c == L'*' || c == L'Х') c = L'х';
    else if (n > 0 && (c == L'З' || c == L'з' || c == L'Z' || c == L'z')) c = L'3';
    else if (n > 0) c = ocr_digitize(c);
    if (c == L'ч') c = L'Ч';
    t[n++] = c;
  }
  t[n] = 0;
  lstrcpyW(s, t);
}

/* формат листа: А0…А4, можно с кратностью «А4х3» */
static BOOL dr_fmt_ok(const wchar_t *s) {
  if (!wcscmp(s, L"БЧ")) return TRUE;
  if (s[0] != L'А' || s[1] < L'0' || s[1] > L'4') return FALSE;
  if (!s[2]) return TRUE;
  if (s[2] != L'х' || !dr_isdig(s[3])) return FALSE;
  for (int i = 3; s[i]; i++)
    if (!dr_isdig(s[i])) return FALSE;
  return TRUE;
}

/* резьба: «Мб» — это «М6» */
static void dr_fix_thread(wchar_t *s) {
  for (int i = 0; s[i]; i++)
    if ((s[i] == L'М' || s[i] == L'M') && (i == 0 || s[i - 1] == L' ') && (s[i + 1] == L'б' || s[i + 1] == L'b') &&
        (s[i + 2] == 0 || s[i + 2] == L' ' || s[i + 2] == L'-' || s[i + 2] == L'х' || s[i + 2] == L'x'))
      s[i] = L'М', s[i + 1] = L'6';
  /* поле допуска «М6-6gх20»: Tesseract читает g как 9 — «М6-69х20» */
  for (int i = 0; s[i]; i++) {
    if (s[i] != L'М' || (i && s[i - 1] != L' ') || !dr_isdig(s[i + 1])) continue;
    int j = i + 1;
    while (dr_isdig(s[j]) || s[j] == L',' || s[j] == L'.') j++;
    if (s[j] == L'-' && dr_isdig(s[j + 1]) && s[j + 2] == L'9' && (s[j + 3] == L'х' || s[j + 3] == L'x'))
      s[j + 2] = L'g';
  }
}

/* Слово латиницей, но все его буквы похожи на русские («Cm3cn» — это
   «Ст3сп», «M6» — «М6»): в русском поле — в кириллицу. Слова с другими
   латинскими буквами («AISI 304», «gx20») не трогаем. */
static void dr_fix_latin(wchar_t *s) {
  static const wchar_t lat[] = L"ABCEHKMOPTXaceopxymnuk", cyr[] = L"АВСЕНКМОРТХасеорхутпик";
  BOOL cyrSeen = FALSE;
  for (const wchar_t *p = s; *p; p++)
    if (*p >= 0x400 && *p <= 0x4FF) cyrSeen = TRUE;
  if (!cyrSeen) return;
  for (int i = 0; s[i];) {
    int j = i;
    BOOL lat1 = FALSE, other = FALSE;
    while (s[j] && s[j] != L' ') {
      wchar_t c = s[j];
      if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) {
        if (wcschr(lat, c)) lat1 = TRUE;
        else other = TRUE;
      }
      j++;
    }
    if (lat1 && !other)
      for (int k = i; k < j; k++) {
        const wchar_t *q = wcschr(lat, s[k]);
        if (q && (s[k] < L'0' || s[k] > L'9')) s[k] = cyr[q - lat];
      }
    i = s[j] ? j + 1 : j;
  }
}

/* Курсив ГОСТ: заглавную «С» в начале слова Tesseract читает «(» — «(ухарь», «(борочный чертеж». Скобка без
   пары в том же слове перед строчной буквой — это «С» */
static void dr_fix_paren_c(wchar_t *s) {
  for (int i = 0; s[i]; i++) {
    if (s[i] != L'(' || (i && s[i - 1] != L' ') || s[i + 1] < L'а' || s[i + 1] > L'я') continue;
    int j = i + 1;
    while (s[j] && s[j] != L' ' && s[j] != L')') j++;
    if (s[j] != L')') s[i] = L'С';
  }
}

/* Графа «Наименование», прочитанная Tesseract, — мусор: слова соседних граф штампа («№ докум.», «Подп.»,
   «Разраб.», «Масштаб»…) или кириллица вперемешку с латинскими словами, у которых нет русской пары
   («Гриспосодление SYOOTONOLXHOE», «fama Сидоренко Welz»: курсив ГОСТ, смещённая графа). DIN, ISO, AISI — свои */
static BOOL dr_stamp_junk(const wchar_t *s) {
  static const wchar_t *const kStampW[] = {L"докум", L"подп", L"разраб", L"пров", L"контр", L"утв", L"лит",
                                           L"масса", L"масштаб", L"изм", L"листов", L"дата"};
  static const wchar_t *const kLatOk[] = {L"DIN", L"ISO", L"AISI", L"ASTM", L"SAE", L"EN"};
  BOOL cyr = FALSE, lat = FALSE;
  for (const wchar_t *p = s; *p;) {
    while (*p == L' ') p++;
    wchar_t w[32];
    int l = 0, nl = 0;
    while (*p && *p != L' ') {
      if (l < 31) w[l++] = *p;
      nl += (*p >= L'A' && *p <= L'Z') || (*p >= L'a' && *p <= L'z');
      cyr |= *p >= 0x400 && *p <= 0x4FF;
      p++;
    }
    w[l] = 0;
    if (wcschr(w, L'№')) return TRUE;
    BOOL ok = FALSE;
    for (size_t i = 0; i < sizeof(kLatOk) / sizeof(kLatOk[0]); i++) ok |= !wcscmp(w, kLatOk[i]);
    if (nl >= 3 && !ok) lat = TRUE;
    wchar_t low[32];
    lstrcpynW(low, w[0] == L'(' ? w + 1 : w, 32);
    CharLowerBuffW(low, (DWORD)wcslen(low));
    for (int e = (int)wcslen(low); e > 0 && (low[e - 1] == L'.' || low[e - 1] == L','); e--) low[e - 1] = 0;
    for (size_t i = 0; i < sizeof(kStampW) / sizeof(kStampW[0]); i++)
      if (!wcscmp(low, kStampW[i]) || (wcslen(kStampW[i]) >= 5 && !wcsncmp(low, kStampW[i], wcslen(kStampW[i])) &&
                                       wcslen(low) <= wcslen(kStampW[i]) + 2))
        return TRUE;
  }
  return cyr && lat;
}

static void dr_field_set(DrPage *pg, int f, const wchar_t *raw, int conf) {
  wchar_t v[DR_CELL];
  lstrcpynW(v, raw, DR_CELL);
  dr_trim(v);
  BOOL bad = conf < 70;
  wchar_t t[DR_CELL];
  switch (f) {
  case DF_DES:
    dr_norm_des(v, t, DR_CELL);
    lstrcpyW(v, t);
    bad = !dr_des_ok(v);
    if (bad && dr_find_des(v, t, DR_CELL) && dr_des_ok(t)) lstrcpyW(v, t); /* хвост чужих граф — прочь, подсветка остаётся */
    if (!bad && conf < 50) bad = TRUE;
    break;
  case DF_MASS:
    if (v[0] && wcscmp(v, L"-") && wcscmp(v, L"—")) {
      bad = !dr_number(v, t, DR_CELL, TRUE) || bad;
      dr_mass_kg(v, t, DR_CELL);
      lstrcpyW(v, t);
    } else {
      v[0] = 0, bad = FALSE;
    }
    break;
  case DF_SCALE: {
    dr_norm_des(v, t, DR_CELL);
    wchar_t *c = wcschr(t, L':');
    bad = !(c && c > t && dr_isdig(c[-1]) && dr_isdig(c[1]));
    if (!v[0]) bad = FALSE;
    lstrcpyW(v, t);
    break;
  }
  case DF_SHEET:
  case DF_SHEETS:
    dr_sheetnum(v);
    bad = FALSE; /* пустой «Лист» у однолистового — по ГОСТ */
    break;
  default:
    dr_fix_paren_c(v), dr_fix_latin(v);
    if (f == DF_NAME && dr_stamp_junk(v)) v[0] = 0, bad = TRUE; /* не вписывать: пусто и «проверьте» */
    else if (!v[0]) bad = FALSE;
    break;
  }
  if (bad) {
    wchar_t ai[DR_CELL];
    if (dr_ai_refine_field(kDrField[f], v,
                           f == DF_DES ? L"Графа штампа чертежа по ГОСТ 2.104, распознана Tesseract. "
                                         L"Пример обозначения: АДЕ 3422-682.01.01.02"
                                       : L"Графа штампа чертежа по ГОСТ 2.104, распознана Tesseract.",
                           ai, DR_CELL)) {
      /* обозначение принимаем, только если сходится с шаблоном; остальное — подсветка остаётся */
      if (f != DF_DES || dr_des_ok(ai)) {
        lstrcpyW(v, ai);
        if (f == DF_DES) bad = FALSE;
      }
    }
  }
  lstrcpynW(pg->f[f], v, DR_CELL);
  pg->doubt[f] = (BYTE)bad;
}

/* ---- спецификация ------------------------------------------------------------------ */

static const int kDrSpecX[8] = {0, 6, 12, 20, 90, 153, 163, 185}; /* мм от левого края */
#define DR_SPEC_SLANT 0.2 /* выпрямление курсива перед Tesseract (dr_img_shear): 0,2–0,27 читаются одинаково */

static int dr_section(const wchar_t *s) {
  static const wchar_t *const sect[] = {L"докум", L"компле", L"сборо", L"детал", L"станд", L"прочи", L"матер"};
  wchar_t low[16];
  lstrcpynW(low, s, 16);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  for (size_t i = 0; i < sizeof(sect) / sizeof(sect[0]); i++)
    if (!wcsncmp(low, sect[i], wcslen(sect[i]))) return (int)i;
  return -1;
}

/* графа строки спецификации к виду ГОСТ: обозначение и формат — по шаблону, позиция и количество —
   числом, в наименовании латиница и резьбы поправлены. conf — уверенность Tesseract (модель — 100);
   сомнительное — подсветить */
static void dr_spec_cell(DrSpecRow *o, int c, const wchar_t *in, int conf) {
  wchar_t t[DR_CELL], d[DR_CELL];
  lstrcpynW(t, in, DR_CELL);
  BOOL bad = t[0] && conf < 70;
  if (t[0] && c == DS_DES) {
    dr_norm_des(t, d, DR_CELL);
    lstrcpyW(t, d);
    bad = !dr_des_ok(t) || conf < 80; /* «АДЕ»→«АМЕ» шаблон не ловит */
  }
  if (t[0] && c == DS_FMT) {
    dr_norm_fmt(t);
    bad = !dr_fmt_ok(t);
  }
  if (c == DS_NAME) dr_fix_paren_c(t);
  if (c == DS_NAME || c == DS_NOTE) dr_fix_latin(t), dr_fix_thread(t);
  if (t[0] && (c == DS_POS || c == DS_QTY)) {
    if (dr_number(t, d, DR_CELL, c == DS_QTY)) lstrcpyW(t, d);
    else bad = TRUE;
  }
  lstrcpynW(o->c[c], t, DR_CELL);
  o->doubt[c] = (BYTE)bad;
}

/* «Резерв» в наименовании — место под позицию, не изделие: в состав не идёт */
static BOOL dr_spec_reserve(const wchar_t *name) {
  wchar_t low[8];
  lstrcpynW(low, name, 8);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  return !wcsncmp(low, L"резерв", 6);
}

/* правок (вставка, удаление, замена буквы) от a до b; строки до 31 знака */
static int dr_lev(const wchar_t *a, int la, const wchar_t *b, int lb) {
  int d[32];
  if (la > 31 || lb > 31) return 99;
  for (int j = 0; j <= lb; j++) d[j] = j;
  for (int i = 1; i <= la; i++) {
    int prev = d[0];
    d[0] = i;
    for (int j = 1; j <= lb; j++) {
      int t = d[j], v = prev + (a[i - 1] != b[j - 1]);
      if (d[j] + 1 < v) v = d[j] + 1;
      if (d[j - 1] + 1 < v) v = d[j - 1] + 1;
      d[j] = v, prev = t;
    }
  }
  return d[lb];
}

static const wchar_t *const kDrSectTitle[] = {L"Документация", L"Комплексы", L"Сборочные единицы", L"Детали",
                                              L"Стандартные изделия", L"Прочие изделия", L"Материалы", L"Комплекты"};
static const int kDrSectId[] = {0, 1, 2, 3, 4, 5, 6, 1}; /* номер раздела, как у dr_section */

/* Заголовок раздела в словах Tesseract с w: курсив ГОСТ теряет и путает первую букву («(борочные»,
   «(тандбартные изделия») — слово сверяется с названием по ГОСТ 2.106 с допуском в четверть букв (не
   больше двух), у двухсловного — и второе, если оно есть. Ответ — номер в kDrSectTitle, *end — где
   заголовок кончился; -1 — не заголовок */
static int dr_section_ocr(const wchar_t *w, const wchar_t **end) {
  while (*w && !IsCharAlphaW(*w)) w++;
  for (size_t i = 0; i < sizeof(kDrSectTitle) / sizeof(kDrSectTitle[0]); i++) {
    const wchar_t *t = kDrSectTitle[i], *p = w;
    BOOL ok = TRUE;
    for (int word = 0; ok && *t; word++) {
      wchar_t a[32], b[32];
      int la = 0, lb = 0;
      while (*p && !IsCharAlphaW(*p) && word) p++; /* второе слово — через пробел и скобки */
      while (*p && *p != L' ' && la < 31) a[la++] = *p++;
      while (*t && *t != L' ' && lb < 31) b[lb++] = *t++;
      while (*t == L' ') t++;
      a[la] = b[lb] = 0;
      if (word && !la) break; /* второго слова нет — хватит первого */
      CharLowerBuffW(a, (DWORD)la), CharLowerBuffW(b, (DWORD)lb);
      while (la && !IsCharAlphaW(a[la - 1])) a[--la] = 0;
      int tol = lb / 4 < 2 ? lb / 4 : 2;
      ok = dr_lev(a, la, b, lb) <= tol;
    }
    if (ok) {
      if (end) *end = p;
      return (int)i;
    }
  }
  return -1;
}

/* Заголовок раздела, склеенный Tesseract со строкой («Кальцо i (тандбартные изделия»): двухсловный — или
   после скобки — в середине наименования. Найден — вырезается из name, ответ — номер в kDrSectTitle; -1 — нет */
static int dr_spec_cut_head(wchar_t *name) {
  for (wchar_t *p = name; (p = wcschr(p, L' ')) != NULL; p++) {
    const wchar_t *end;
    int i = dr_section_ocr(p + 1, &end);
    if (i < 0) continue;
    BOOL paren = p[1] == L'(', two = FALSE; /* двухсловный — если в тексте оба слова */
    for (const wchar_t *q = p + 1; q < end && !two; q++) two = *q == L' ' && wcschr(kDrSectTitle[i], L' ') != NULL;
    if (!paren && !two) continue;
    memmove(p, end, (wcslen(end) + 1) * sizeof(wchar_t));
    dr_trim(name);
    return i;
  }
  return -1;
}

static void dr_vl_spec(const DrImg *m, int x0, int y0, int x1, int y1, DrPage *pg);

/* Сетка таблицы по её же вертикалям: столбцы ГОСТ 2.106 (0, 6, 12, 20, 90, 153, 163, 185 мм) подгоняются по
   масштабу и сдвигу к длинным вертикалям над штампом, низ таблицы — линия во всю ширину у верха штампа. На
   заводских копиях вокруг рамки отпечатан обрез листа, и штамп находится со сдвигом и в масштабе по dpi (копия
   в 95 %): сетка от штампа промахивалась на 4–8 мм, и Tesseract не давал ни одной строки. g — штамп с краями и
   масштабом таблицы; FALSE — не подогналось (g как был) */
static BOOL dr_spec_grid(const DrImg *m, const DrLines *L, DrStamp *g) {
  static const int X[8] = {0, 6, 12, 20, 90, 153, 163, 185};
  double k = g->k;
  int y0 = g->st - (int)(60 * k), y1 = g->st - (int)(4 * k);
  int x0 = g->sl - (int)(25 * k), x1 = g->rx + (int)(8 * k);
  if (y0 < 0) y0 = 0;
  if (x0 < 0) x0 = 0;
  if (x1 > m->w - 1) x1 = m->w - 1;
  if (y1 - y0 < (int)(20 * k)) return FALSE;
  int cx[96], nc = 0, run0 = -1;
  for (int x = x0; x <= x1 + 1; x++) { /* длинные вертикали полосы: 80 % её высоты */
    int cov = 0;
    if (x <= x1)
      for (int y = y0; y < y1; y++) cov += L->vm[(size_t)y * m->w + x];
    BOOL on = x <= x1 && cov >= 0.8 * (y1 - y0);
    if (on && run0 < 0) run0 = x;
    if (!on && run0 >= 0) {
      if (nc < 96) cx[nc++] = (run0 + x - 1) / 2;
      run0 = -1;
    }
  }
  int best = 0, bl = 0;
  double bk = 0;
  for (int i = 0; i < nc; i++)
    for (int j = i + 1; j < nc; j++) {
      double kk = (cx[j] - cx[i]) / 185.0;
      if (kk < 0.85 * k || kk > 1.05 * k) continue;
      int score = 0, tol = (int)(1.2 * kk) + 1;
      for (int c = 0; c < 8; c++) {
        int want = cx[i] + (int)(X[c] * kk + 0.5);
        for (int q = 0; q < nc; q++)
          if (abs(cx[q] - want) <= tol) {
            score++;
            break;
          }
      }
      if (score > best) best = score, bl = cx[i], bk = kk;
    }
  if (best < 6) return FALSE;
  g->sl = bl;
  g->rx = bl + (int)(185 * bk + 0.5);
  g->k = bk;
  /* низ таблицы — самая нижняя линия во всю ширину около верха штампа (внутренние линии штампа короче) */
  int lo = g->st - (int)(6 * k), hi = g->st + (int)(12 * k);
  if (hi > m->h - 2) hi = m->h - 2;
  for (int y = hi; y >= lo; y--)
    if (dr_hline(L, y, 1, g->sl, g->rx, 0.9)) {
      g->st = y;
      break;
    }
  return TRUE;
}

/* Точки в обозначениях строк — по соседним строкам того же листа. Tesseract теряет мелкие точки шрифта ГОСТ
   («3487.00.00.01» → «34870000.01»), а у строк одной спецификации номер устроен одинаково (4 цифры, затем
   группы): разбивка, которая встретилась у строк чаще всех, — образец, и строка с тем же числом цифр после
   дефиса разбивается по нему. Заодно — апострофы, которые Tesseract видит в хвостах «7» */
static void dr_spec_fix_des(DrPage *pg) {
  int pat[16][6], cnt[16], np = 0;
  for (int r = 0; r < pg->nrows; r++) { /* образцы: длины групп после дефиса у правильных обозначений */
    wchar_t *d = pg->rows[r].c[DS_DES], *q;
    for (q = d; *q; q++)
      if (*q == L'\'' || *q == L'’') memmove(q, q + 1, wcslen(q) * sizeof(wchar_t)), q--;
    const wchar_t *h = wcschr(d, L'-');
    if (!h || dr_des_core(d, (int)(h - d) - 4 < 0 ? 0 : (int)(h - d) - 4) < 0) continue;
    int g[6] = {0}, ng = 0;
    for (const wchar_t *c = h + 1; *c && ng < 6; c++) {
      if (dr_isdig(*c)) g[ng]++;
      else if (*c == L'.' && g[ng]) ng++;
      else break;
    }
    if (g[ng]) ng++;
    if (ng < 2) continue;
    int k = 0;
    while (k < np && memcmp(pat[k], g, sizeof(g))) k++;
    if (k == np && np < 16) memcpy(pat[np], g, sizeof(g)), cnt[np++] = 0;
    if (k < np) cnt[k]++;
  }
  int best = -1;
  for (int k = 0; k < np; k++)
    if (best < 0 || cnt[k] > cnt[best]) best = k;
  /* образца нет (правильных строк меньше двух) — по числу цифр: 10 — 4.2.2.2, 8 — 4.2.2, 6 — 4.2 */
  static const int kFall[3][6] = {{4, 2, 2, 2}, {4, 2, 2}, {4, 2}};
  BOOL own = best >= 0 && cnt[best] >= 2;
  for (int r = 0; r < pg->nrows; r++) {
    wchar_t *d = pg->rows[r].c[DS_DES], *h = wcschr(d, L'-');
    if (!h) continue;
    wchar_t dig[32];
    int n = 0;
    const wchar_t *c = h + 1;
    for (; *c && (dr_isdig(*c) || *c == L'.') && n < 31; c++)
      if (dr_isdig(*c)) dig[n++] = *c;
    const int *pt = own ? pat[best] : n == 10 ? kFall[0] : n == 8 ? kFall[1] : n == 6 ? kFall[2] : NULL;
    int want = 0;
    for (int i = 0; pt && i < 6; i++) want += pt[i];
    if (!pt || n != want) continue;
    wchar_t out[DR_CELL];
    int o = (int)(h - d) + 1, k = 0;
    memcpy(out, d, (size_t)o * sizeof(wchar_t));
    for (int gi = 0; gi < 6 && pt[gi]; gi++) {
      if (gi) out[o++] = L'.';
      for (int j = 0; j < pt[gi]; j++) out[o++] = dig[k++];
    }
    _snwprintf(out + o, DR_CELL - o, L"%s", c);
    out[DR_CELL - 1] = 0;
    if (wcscmp(out, d)) {
      wchar_t t[DR_CELL];
      dr_norm_des(out, t, DR_CELL); /* с точками сходится ядро — теперь и хвост «С6» становится «СБ» */
      lstrcpynW(d, t, DR_CELL);
      pg->rows[r].doubt[DS_DES] = !dr_des_ok(d);
    }
  }
  /* буквенный код, потерянный одной строкой: у соседей «КОД 1234-…», у неё «1234-…» с тем же номером изделия */
  for (int r = 0; r < pg->nrows; r++) {
    wchar_t *d = pg->rows[r].c[DS_DES];
    const wchar_t *h = wcschr(d, L'-'), *code = NULL;
    if (!dr_isdig(d[0]) || !h || h - d < 3 || h - d > 5) continue;
    int len = (int)(h - d) + 1, cl = 0, same = 0;
    for (int q = 0; q < pg->nrows && same >= 0; q++) {
      const wchar_t *e = pg->rows[q].c[DS_DES];
      int l = 0;
      while (l < 6 && dr_isupper_cyr(e[l])) l++;
      if (l < 2 || l > 5 || e[l] != L' ' || wcsncmp(e + l + 1, d, (size_t)len)) continue;
      if (code && (cl != l || wcsncmp(code, e, (size_t)l))) same = -1; /* у соседей разные коды — не угадать */
      else code = e, cl = l, same++;
    }
    int n = (int)wcslen(d);
    if (same < 2 || n + cl + 1 >= DR_CELL) continue;
    memmove(d + cl + 1, d, (size_t)(n + 1) * sizeof(wchar_t));
    memcpy(d, code, (size_t)cl * sizeof(wchar_t));
    d[cl] = L' ';
  }
}

/* строки спецификации Tesseract по сетке таблицы; FALSE — таблица не нашлась */
static BOOL dr_spec(DrImg *m, int thr, const DrLines *L, const DrStamp *s0, DrPage *pg) {
  DrStamp g = *s0, *s = &g; /* края и масштаб — таблицы (dr_spec_grid), а не штампа, если подогналось */
  dr_spec_grid(m, L, &g);
  double k = s->k;
  int x20 = s->sl + (int)(20 * k + 0.5);
  int top = dr_vup(L, x20, (int)(1.5 * k) + 1, s->st, (int)(2 * k) + 2, (int)(1.5 * k) + 2);
  if (top < 0 || (s->st - top) / k < 30) {
    lstrcpynW(pg->note, L"таблица спецификации не найдена", 300);
    return FALSE;
  }
  /* низ шапки: линия во всю ширину около 15 мм от верха таблицы */
  int body = top + (int)(15 * k + 0.5);
  for (int d = 0; d <= (int)(3 * k); d++) {
    if (dr_hline(L, body + d, 1, s->sl, s->rx, 0.8)) {
      body += d;
      break;
    }
    if (dr_hline(L, body - d, 1, s->sl, s->rx, 0.8)) {
      body -= d;
      break;
    }
  }
  /* строки: линии во всю ширину между шапкой и штампом */
  int ys[200], ny = 0;
  ys[ny++] = body;
  for (int y = body + (int)(4 * k); y < s->st - (int)(4 * k) && ny < 198; y++) {
    int cov = 0;
    for (int x = s->sl; x < s->rx; x += 2) cov += L->hm[(size_t)y * m->w + x];
    if (cov * 2 >= 0.6 * (s->rx - s->sl)) {
      int y2 = y;
      while (y2 + 1 < s->st) {
        int c2 = 0;
        for (int x = s->sl; x < s->rx; x += 2) c2 += L->hm[(size_t)(y2 + 1) * m->w + x];
        if (c2 * 2 < 0.6 * (s->rx - s->sl)) break;
        y2++;
      }
      ys[ny++] = (y + y2) / 2;
      y = y2 + (int)(3 * k);
    }
  }
  int nrowsExp = (int)((s->st - body) / (8 * k) + 0.5);
  if (ny - 1 < nrowsExp / 2) { /* линий строк нет — через 8 мм */
    ny = 0;
    for (int i = 0; i <= nrowsExp && ny < 199; i++) ys[ny++] = body + (int)(i * 8 * k + 0.5);
  }
  ys[ny++] = s->st;
  int nr = ny - 1;
  /* тело таблицы — одной вырезкой: слово — в ячейку по центру */
  int cx0 = s->sl + (int)(0.6 * k), cx1 = s->rx - (int)(0.6 * k);
  int cy0 = body + (int)(0.6 * k), cy1 = s->st - (int)(0.6 * k);
  /* курсив ГОСТ (тип Б, 15°) Tesseract читает лучше выпрямленным, прямой шрифт — как есть: обе вырезки в
     одном запуске, берётся та, где слова увереннее (заводские копии — курсив, бланки из программ — прямой) */
  DrImg crop[2];
  if (!dr_crop(m, thr, cx0, cy0, cx1, cy1, k, &crop[0])) return FALSE;
  int nc = 1;
  if (dr_img_new(&crop[1], crop[0].w, crop[0].h)) {
    memcpy(crop[1].px, crop[0].px, (size_t)crop[0].w * crop[0].h);
    if (dr_img_shear(&crop[1], DR_SPEC_SLANT)) nc = 2;
    else dr_img_free(&crop[1]);
  }
  double up = k < 9 ? 11.8 / k : 1.0;
  DrWords ws;
  memset(&ws, 0, sizeof(ws));
  wchar_t why[200] = {0};
  BOOL ok = dr_tess(crop, nc, 6, &ws, why, 200);
  for (int i = 0; i < nc; i++) dr_img_free(&crop[i]);
  double csum[3] = {0};
  int ccnt[3] = {0};
  for (int i = 0; ok && i < ws.n; i++)
    if (ws.w[i].t[1] && ws.w[i].page >= 1 && ws.w[i].page <= 2) csum[ws.w[i].page] += ws.w[i].conf, ccnt[ws.w[i].page]++;
  int pick = nc == 2 && ccnt[2] && (!ccnt[1] || csum[2] / ccnt[2] > csum[1] / ccnt[1]) ? 2 : 1;
  double slant = pick == 2 ? DR_SPEC_SLANT : 0;
  if (!ok) {
    lstrcpynW(pg->note, why, 300);
    return FALSE;
  }
  /* сетка: текст и худшая уверенность каждой ячейки */
  wchar_t(*cell)[DS_N][DR_CELL] = calloc((size_t)nr, sizeof(*cell));
  int(*conf)[DS_N] = calloc((size_t)nr, sizeof(*conf));
  if (!cell || !conf) {
    free(cell);
    free(conf);
    dr_words_free(&ws);
    return FALSE;
  }
  for (int r = 0; r < nr; r++)
    for (int c = 0; c < DS_N; c++) conf[r][c] = 100;
  /* слова по порядку чтения: строка Tesseract, потом x */
  for (int i = 0; i < ws.n; i++) {
    const DrWord *w = &ws.w[i];
    if (w->page != pick) continue;
    if (!w->t[1] && wcschr(L"|_—-–=~'\"`.,:;!", w->t[0])) continue;
    double wy = w->y + w->h / 2.0, px = cx0 + (w->x + w->w / 2.0 - slant * wy - 12) / up, py = cy0 + (wy - 12) / up;
    double mmx = (px - s->sl) / k;
    int col = -1;
    for (int c = 0; c < 7; c++)
      if (mmx >= kDrSpecX[c] && mmx < kDrSpecX[c + 1]) col = c;
    /* остатки линий и пыль; одинокую цифру в «Поз.» и «Кол.» Tesseract часто
       читает верно, но не уверен — берём, графа будет подсвечена */
    BOOL lone = !w->t[1] && dr_isdig(w->t[0]) && (col + 1 == DS_POS || col + 1 == DS_QTY);
    if (w->conf < (lone ? 10 : 40) || w->h < 1.6 * k * up) continue;
    int row = -1;
    for (int r = 0; r < nr; r++)
      if (py >= ys[r] && py < ys[r + 1]) row = r;
    if (col < 0 || row < 0) continue;
    int dc = col + 1; /* DS_FMT… — после «Раздела» */
    wchar_t *dst = cell[row][dc];
    size_t l = wcslen(dst);
    _snwprintf(dst + l, DR_CELL - l, L"%s%s", l ? L" " : L"", w->t);
    if (w->conf < conf[row][dc]) conf[row][dc] = w->conf;
  }
  dr_words_free(&ws);
  /* строки → позиции: разделы, продолжения наименования */
  wchar_t sect[DR_CELL] = {0};
  int lastRow = -1; /* последняя строка сетки с текстом */
  pg->rows = (DrSpecRow *)calloc((size_t)nr, sizeof(DrSpecRow));
  pg->nrows = 0;
  for (int r = 0; r < nr && pg->rows; r++) {
    BOOL any = FALSE, onlyName = TRUE;
    for (int c = DS_FMT; c < DS_N; c++) {
      dr_trim(cell[r][c]);
      if (cell[r][c][0]) {
        any = TRUE;
        if (c != DS_NAME) onlyName = FALSE;
      }
    }
    if (any) lastRow = r;
    if (!any || (!cell[r][DS_DES][0] && dr_spec_reserve(cell[r][DS_NAME]))) continue; /* «Резерв» — не продолжение наименования */
    const wchar_t *hend;
    int hs = onlyName ? dr_section_ocr(cell[r][DS_NAME], &hend) : -1;
    if (hs >= 0 && !*hend) { /* заголовок раздела — название по ГОСТ, а не как прочиталось («(борочные») */
      lstrcpynW(sect, kDrSectTitle[hs], DR_CELL);
      continue;
    }
    hs = dr_spec_cut_head(cell[r][DS_NAME]); /* заголовок, склеенный со строкой под ним: строка — уже в нём */
    if (hs >= 0) lstrcpynW(sect, kDrSectTitle[hs], DR_CELL);
    if (onlyName && pg->nrows > 0) { /* продолжение наименования */
      DrSpecRow *p = &pg->rows[pg->nrows - 1];
      size_t l = wcslen(p->c[DS_NAME]);
      _snwprintf(p->c[DS_NAME] + l, DR_CELL - l, L" %s", cell[r][DS_NAME]);
      if (conf[r][DS_NAME] < 70) p->doubt[DS_NAME] = 1;
      continue;
    }
    if (!cell[r][DS_NAME][0] && !cell[r][DS_DES][0] && !cell[r][DS_QTY][0]) continue; /* от строки — один мусор */
    DrSpecRow *o = &pg->rows[pg->nrows++];
    lstrcpynW(o->c[DS_SECT], sect, DR_CELL);
    o->doubt[DS_SECT] = !sect[0];
    for (int c = DS_FMT; c < DS_N; c++) dr_spec_cell(o, c, cell[r][c], conf[r][c]);
    static const int kNum[2] = {DS_POS, DS_QTY};
    for (int q = 0; q < 2; q++) { /* «д» вместо цифры — не число: пусто, «проверьте» */
      wchar_t t[DR_CELL];
      int c = kNum[q];
      if (o->c[c][0] && !dr_number(o->c[c], t, DR_CELL, c == DS_QTY)) o->c[c][0] = 0, o->doubt[c] = 1;
    }
  }
  for (int r = 0; r < pg->nrows; r++) {
    wchar_t *nm = pg->rows[r].c[DS_NAME];
    dr_fix_paren_c(nm), dr_fix_latin(nm), dr_fix_thread(nm);
  }
  dr_spec_fix_des(pg);
  pg->rowsBy = pg->nrows ? 1 : 0;
  free(cell);
  free(conf);
  /* модели — с полем слева и над шапкой, как в замере 31.10: по тесной вырезке (ровно таблица) она на заводском
     листе зациклилась до предела токенов */
  int vy = top - (int)(8 * k), vx = s->sl - (int)(15 * k), vb = s->st;
  BOOL fill = FALSE; /* только заполненная часть таблицы (до строки под последней с текстом): замер скорости */
#ifdef SET_TEST
  fill = getenv("CP_SPECFILL") != NULL;
#endif
  if (fill && lastRow >= 0 && lastRow + 2 < ny) vb = ys[lastRow + 2];
  dr_vl_spec(m, vx < 0 ? 0 : vx, vy < 0 ? 0 : vy, s->rx + (int)(2 * k), vb, pg);
  return TRUE;
}

/* Шапка таблицы спецификации словами: «Обозначение», «Наименование» и «Поз.», «Кол.» или «Примечание» в
   полосе 70 мм сверху. Для копий, где столбцы не на своих местах: вокруг рамки отпечатан обрез листа, и
   штамп находится со сдвигом и в масштабе по dpi (заводские копии 95 %) — dr_spec их не узнаёт, а модели
   сетка не нужна. В *top — верх шапки (точки листа) */
static BOOL dr_spec_header(const DrImg *m, int thr, const DrStamp *s, double k, int *top) {
  int x0 = s->sl - (int)(15 * k), y1 = (int)(70 * k);
  if (x0 < 0) x0 = 0;
  if (y1 > s->st) y1 = s->st;
  DrImg crop;
  if (!dr_crop(m, thr, x0, 0, s->rx, y1, k, &crop)) return FALSE;
  double up = k < 9 ? 11.8 / k : 1.0;
  DrWords ws;
  memset(&ws, 0, sizeof(ws));
  wchar_t why[200] = {0};
  BOOL ok = dr_tess(&crop, 1, 6, &ws, why, 200);
  dr_img_free(&crop);
  int des = -1, name = 0;
  for (int i = 0; ok && i < ws.n; i++) {
    wchar_t low[64];
    lstrcpynW(low, ws.w[i].t, 64);
    CharLowerBuffW(low, (DWORD)wcslen(low));
    if (wcsstr(low, L"обознач") && des < 0) des = (int)((ws.w[i].y - 12) / up);
    /* «Поз.», «Кол.», «Примечание» в узких повёрнутых графах Tesseract обычно не видит, а «Н» читает
       как «Р» («Раименование») — хватает «Обозначения» и одного из остальных */
    if (wcsstr(low, L"именов") || wcsstr(low, L"поз") || wcsstr(low, L"кол") || wcsstr(low, L"примеч")) name = 1;
  }
  dr_words_free(&ws);
  if (des < 0 || !name) return FALSE;
  *top = des - (int)(8 * k) < 0 ? 0 : des - (int)(8 * k);
  return TRUE;
}

#ifdef SET_TEST
static double g_drAng, g_drAngV;
/* проверка: уменьшенная страница, линии красным/синим, штамп зелёным */
static void dr_debug_img(const DrLines *L, const DrStamp *s, const DrPage *pg) {
  const DrImg *m = L->m;
  int sc = m->w / 1500 + 1, w = m->w / sc, h = m->h / sc, stride = (w * 3 + 3) & ~3;
  BYTE *b = (BYTE *)calloc((size_t)stride * h, 1);
  if (!b) return;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      size_t i = (size_t)y * sc * m->w + (size_t)x * sc;
      BYTE *q = b + (size_t)(h - 1 - y) * stride + x * 3;
      int g = m->px[i] / 2 + 127;
      q[0] = q[1] = q[2] = (BYTE)g;
      BOOL hh = FALSE, vv = FALSE;
      for (int dy = 0; dy < sc; dy++)
        for (int dx = 0; dx < sc; dx++) {
          size_t j = (size_t)(y * sc + dy) * m->w + x * sc + dx;
          hh |= L->hm[j];
          vv |= L->vm[j];
        }
      if (hh) q[2] = 255, q[1] = q[0] = 0;
      if (vv) q[0] = 255, q[1] = q[2] = 0;
      if (s && ((abs(y * sc - s->st) < sc * 2 && x * sc >= s->sl && x * sc <= s->rx) ||
                (abs(x * sc - s->sl) < sc * 2 && y * sc >= s->st && y * sc <= s->by)))
        q[1] = 255, q[0] = q[2] = 0;
    }
  wchar_t p[MAX_PATH];
  _snwprintf(p, MAX_PATH, L"%s.dbg%u.bmp", pg->path, pg->frame);
  BITMAPFILEHEADER fh = {0x4D42, 0, 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
  BITMAPINFOHEADER ih;
  memset(&ih, 0, sizeof(ih));
  ih.biSize = sizeof(ih);
  ih.biWidth = w;
  ih.biHeight = h;
  ih.biPlanes = 1;
  ih.biBitCount = 24;
  fh.bfSize = fh.bfOffBits + stride * h;
  HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD wr;
    WriteFile(f, &fh, sizeof(fh), &wr, NULL);
    WriteFile(f, &ih, sizeof(ih), &wr, NULL);
    WriteFile(f, b, (DWORD)(stride * h), &wr, NULL);
    CloseHandle(f);
  }
  free(b);
}
#endif

/* ---- страница целиком (штампа нет): тип и обозначение по тексту --------------- */

static BOOL dr_whole_text(const DrImg *m, wchar_t *text, int cap, int *conf, wchar_t *why, int wcap) {
  int lng = m->w > m->h ? m->w : m->h;
  int s = lng > 2600 ? (lng + 2599) / 2600 : 1;
  DrImg sm;
  if (!dr_img_new(&sm, m->w / s, m->h / s)) return FALSE;
  for (int y = 0; y < sm.h; y++)
    for (int x = 0; x < sm.w; x++) {
      int sum = 0;
      for (int dy = 0; dy < s; dy++)
        for (int dx = 0; dx < s; dx++) sum += m->px[(size_t)(y * s + dy) * m->w + x * s + dx];
      sm.px[(size_t)y * sm.w + x] = (BYTE)(sum / (s * s));
    }
  DrWords ws;
  memset(&ws, 0, sizeof(ws));
  BOOL ok = dr_tess(&sm, 1, 3, &ws, why, wcap);
  dr_img_free(&sm);
  if (ok) dr_page_text(&ws, 1, L"\n", text, cap, conf);
  dr_words_free(&ws);
  return ok;
}

/* Текст всего листа для маршрута: размеры, резьбы и Ra на чертеже часто стоят
   вертикально, поэтому лист читается ещё повёрнутым на 90° в обе стороны.
   psm 11 — разрозненный текст: надписи чертежа не складываются в абзацы. */
static BOOL g_drText; /* читать ли весь лист у чертежей (маршрут); пакетное распознавание — нет */

static BOOL dr_img_rot(const DrImg *m, BOOL cw, DrImg *out) {
  if (!dr_img_new(out, m->h, m->w)) return FALSE;
  for (int y = 0; y < m->h; y++)
    for (int x = 0; x < m->w; x++)
      out->px[cw ? (size_t)x * out->w + (m->h - 1 - y) : (size_t)(m->w - 1 - x) * out->w + y] =
          m->px[(size_t)y * m->w + x];
  return TRUE;
}

static BOOL dr_sheet_text(const DrImg *m, wchar_t *text, int cap, wchar_t *why, int wcap) {
  int lng = m->w > m->h ? m->w : m->h;
  int s = lng > 3300 ? (lng + 3299) / 3300 : 1;
  DrImg im[3];
  memset(im, 0, sizeof(im));
  BOOL ok = dr_img_new(&im[0], m->w / s, m->h / s);
  if (ok) {
    for (int y = 0; y < im[0].h; y++)
      for (int x = 0; x < im[0].w; x++) {
        int sum = 0;
        for (int dy = 0; dy < s; dy++)
          for (int dx = 0; dx < s; dx++) sum += m->px[(size_t)(y * s + dy) * m->w + x * s + dx];
        im[0].px[(size_t)y * im[0].w + x] = (BYTE)(sum / (s * s));
      }
    ok = dr_img_rot(&im[0], TRUE, &im[1]) && dr_img_rot(&im[0], FALSE, &im[2]);
  }
  DrWords ws;
  memset(&ws, 0, sizeof(ws));
  ok = ok && dr_tess(im, 3, 11, &ws, why, wcap);
  for (int i = 0; i < 3; i++) dr_img_free(&im[i]);
  text[0] = 0;
  for (int pgn = 1, n = 0; ok && pgn <= 3 && n < cap - 2; pgn++) {
    int conf;
    if (n) text[n++] = L'\n', text[n] = 0;
    dr_page_text(&ws, pgn, L"\n", text + n, cap - n, &conf);
    n += (int)wcslen(text + n);
  }
  dr_words_free(&ws);
  return ok;
}

static BOOL dr_has(const wchar_t *low, const wchar_t *w) { return wcsstr(low, w) != NULL; }

/* ---- одна страница ----------------------------------------------------------------- */

/* лист и штамп, отложенные для второго прохода модели (DrPage.vlLater) */
typedef struct {
  DrImg m;
  DrStamp s;
  BOOL hasStamp;
} DrVlKeep;

static void dr_page_free(DrPage *pg) {
  if (pg->vlKeep) {
    dr_img_free(&((DrVlKeep *)pg->vlKeep)->m);
    free(pg->vlKeep);
    pg->vlKeep = NULL;
  }
  free(pg->rows);
  pg->rows = NULL;
  pg->nrows = 0;
  free(pg->text);
  pg->text = NULL;
  free(pg->vlText);
  pg->vlText = NULL;
  pg->vlName[0] = pg->vlMat[0] = pg->vlDes[0] = 0;
}

/* ---- чтение моделью с картинками (только локальной: чертежи завода не уходят в сеть) ----
   Tesseract на чертёжном шрифте и почерке ошибается; модель с картинками читает
   наименование и материал из отдельных увеличенных граф штампа и пункты ТТ с поля
   (замер на 37 заводских листах: маршрут целиком 12 → 27). Ответы — только как
   дополнительный текст для правил techno.c: числа и операции модель не решает. */
static const char kDrVlSheetSchema[] =
    "{\"type\":\"object\",\"properties\":{\"tt\":{\"type\":\"array\",\"maxItems\":10,\"items\":{\"type\":\"string\","
    "\"maxLength\":220}},\"razvertka\":{\"type\":\"boolean\"},\"holes\":{\"type\":\"array\",\"maxItems\":6,\"items\":"
    "{\"type\":\"string\",\"maxLength\":40}}},\"required\":[\"tt\",\"razvertka\",\"holes\"],\"additionalProperties\":false}";

static BOOL dr_vl_ok(void) {
#ifdef SET_TEST
  if (!getenv("CP_DRVL")) return FALSE; /* проверочная сборка зовёт модель только по просьбе */
#endif
  return g_aiOn && !g_aiProvider && ai_vision_ready();
}

#ifdef SET_TEST
/* дообучение (native/train): --drawrec с CP_DRVLDUMP=папка кладёт туда картинки и вопросы
   ровно такими, какими их получает модель, и прочитанное Tesseract — модель не зовут */
static wchar_t g_drVlDump[MAX_PATH], g_drVlBase[MAX_PATH];
#endif

static BOOL dr_vl_want(void) {
#ifdef SET_TEST
  if (g_drVlDump[0]) return TRUE;
#endif
  return dr_vl_ok();
}

/* часть картинки [x0,x1)×[y0,y1), увеличенная в zoom раз (1 или 2) или уменьшенная до maxSide */
static BOOL dr_img_part(const DrImg *m, int x0, int y0, int x1, int y1, int zoom, int maxSide, DrImg *out) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > m->w) x1 = m->w;
  if (y1 > m->h) y1 = m->h;
  int w = x1 - x0, h = y1 - y0;
  if (w < 8 || h < 8) return FALSE;
  int s = 1;
  if (maxSide && (w > maxSide || h > maxSide)) s = ((w > h ? w : h) + maxSide - 1) / maxSide;
  if (!dr_img_new(out, w / s * zoom, h / s * zoom)) return FALSE;
  for (int y = 0; y < out->h; y++)
    for (int x = 0; x < out->w; x++) {
      int sum = 0, sx = x0 + x / zoom * s, sy = y0 + y / zoom * s;
      for (int dy = 0; dy < s; dy++)
        for (int dx = 0; dx < s; dx++) sum += m->px[(size_t)(sy + dy) * m->w + sx + dx];
      out->px[(size_t)y * out->w + x] = (BYTE)(sum / (s * s));
    }
  return TRUE;
}

/* картинку — во временный BMP и спросить модель; ответ — free(). tag — имя вопроса для выгрузки;
   «gab» — вопрос адаптеру габаритов (ai_ask_image_gab_sync), остальные — адаптеру чертежей */
static wchar_t *dr_vl_ask(const DrImg *im, const wchar_t *tag, const wchar_t *question, const char *schema,
                          int maxTokens) {
  wchar_t tmp[MAX_PATH], p[MAX_PATH];
#ifdef SET_TEST
  if (g_drVlDump[0]) {
    _snwprintf(p, MAX_PATH, L"%s\\%s.%s.bmp", g_drVlDump, g_drVlBase, tag);
    p[MAX_PATH - 1] = 0;
    dr_save_bmp(im, p);
    _snwprintf(p, MAX_PATH, L"%s\\%s.prompt.txt", g_drVlDump, tag);
    share_write_ex(p, question, TRUE);
    if (schema) {
      wchar_t ws[4096];
      MultiByteToWideChar(CP_UTF8, 0, schema, -1, ws, 4096);
      _snwprintf(p, MAX_PATH, L"%s\\%s.schema.json", g_drVlDump, tag);
      share_write_ex(p, ws, TRUE);
    }
    _snwprintf(p, MAX_PATH, L"%s\\%s.max_tokens.txt", g_drVlDump, tag);
    _snwprintf(tmp, MAX_PATH, L"%d", maxTokens);
    share_write_ex(p, tmp, TRUE);
    if (!dr_vl_ok()) return NULL;
  }
#endif
  GetTempPathW(MAX_PATH, tmp);
  static LONG seq;
  _snwprintf(p, MAX_PATH, L"%scursorpad-vl-%lu-%ld.bmp", tmp, GetCurrentThreadId(), InterlockedIncrement(&seq));
  p[MAX_PATH - 1] = 0;
  wchar_t *ans = NULL;
  BOOL gab = !wcscmp(tag, L"gab");
  if (dr_save_bmp(im, p) && (gab ? ai_ask_image_gab_sync(question, p, schema, maxTokens, 240000, NULL, &ans)
                                 : ai_ask_image_sync_ex(question, p, schema, maxTokens, 240000, NULL, &ans)) != AI_OK) {
    free(ans);
    ans = NULL;
  }
  DeleteFileW(p);
  return ans;
}

/* первая строка ответа, без кавычек и пробелов по краям */
static void dr_vl_line(const wchar_t *ans, wchar_t *out, int cap) {
  out[0] = 0;
  if (!ans) return;
  while (*ans == L' ' || *ans == L'\n' || *ans == L'\r' || *ans == L'"' || *ans == L'«') ans++;
  int n = 0;
  while (ans[n] && ans[n] != L'\n' && ans[n] != L'\r' && n < cap - 1) out[n] = ans[n], n++;
  out[n] = 0;
  while (n && (out[n - 1] == L' ' || out[n - 1] == L'"' || out[n - 1] == L'»')) out[--n] = 0;
}

/* строки массива key из JSON-ответа — в out через перевод строки */
static void dr_json_strings(const wchar_t *j, const wchar_t *key, wchar_t *out, int cap) {
  wchar_t k[40];
  _snwprintf(k, 40, L"\"%s\"", key);
  k[39] = 0;
  const wchar_t *p = j ? wcsstr(j, k) : NULL;
  if (!p || !(p = wcschr(p + wcslen(k), L'['))) return;
  int n = (int)wcslen(out);
  for (p++; *p && *p != L']';) {
    if (*p != L'"') {
      p++;
      continue;
    }
    if (n && n < cap - 1) out[n++] = L'\n';
    for (p++; *p && *p != L'"' && n < cap - 1; p++) {
      if (*p == L'\\' && p[1]) {
        p++;
        out[n++] = *p == L'n' ? L' ' : *p == L't' ? L' ' : *p;
      } else {
        out[n++] = *p;
      }
    }
    if (*p == L'"') p++;
  }
  out[n] = 0;
}

/* вопрос по листу целиком; с элементами — для адаптера, который их знает (native/train) */
#define DR_VL_SHEET_HEAD                                                                                              \
  L"Машиностроительный чертёж детали. Ответь JSON:\ntt — пункты технических требований (пронумерованный текст над " \
  L"основной надписью), каждый дословно; если их нет — пустой список;\nrazvertka — true, если на поле чертежа есть " \
  L"надпись «Развертка» или «Линии гиба»;\nholes — надписи про отверстия вида «4 отв. Ø13», «2 отв. M8-7H», «3 отв. " \
  L"Ø9», как написано; если таких надписей нет — пустой список.\n"
#define DR_VL_SHEET_TAIL L"Ничего не придумывай. Основную надпись (штамп) не переписывай."
static const wchar_t kDrVlSheetQ[] = DR_VL_SHEET_HEAD DR_VL_SHEET_TAIL;
static const wchar_t kDrVlSheetQEl[] =
    DR_VL_SHEET_HEAD L"elements — конструктивные элементы, видные на видах, разрезах и сечениях: type — из списка, size — "
                     L"размер, как подписан (или пусто), count — сколько таких. «отверстие» и «резьбовое отверстие» — не "
                     L"по оси детали (крепёжные, поперечные); резьба по оси — «резьба наружная» или «резьба внутренняя»; "
                     L"фаски и осевые отверстия не перечисляй; если элементов нет — пустой список.\n" DR_VL_SHEET_TAIL;
/* те же типы — в native/train (ELEMENT_TYPES) и в разборе ниже */
static const char kDrVlSheetSchemaEl[] =
    "{\"type\":\"object\",\"properties\":{\"tt\":{\"type\":\"array\",\"maxItems\":10,\"items\":{\"type\":\"string\","
    "\"maxLength\":220}},\"razvertka\":{\"type\":\"boolean\"},\"holes\":{\"type\":\"array\",\"maxItems\":6,\"items\":"
    "{\"type\":\"string\",\"maxLength\":40}},\"elements\":{\"type\":\"array\",\"maxItems\":12,\"items\":{\"type\":"
    "\"object\",\"properties\":{\"type\":{\"type\":\"string\",\"enum\":[\"шпоночный паз\",\"шпоночный паз в "
    "отверстии\",\"паз\",\"лыска\",\"грани под ключ\",\"шлицы\",\"шлицы в отверстии\",\"зубья\",\"резьба наружная\","
    "\"резьба внутренняя\",\"резьбовое отверстие\",\"отверстие\",\"канавка\",\"рифление\",\"выдавка\",\"сварной "
    "шов\"]},\"size\":{\"type\":\"string\",\"maxLength\":30},\"count\":{\"type\":\"integer\",\"minimum\":1,"
    "\"maximum\":99}},\"required\":[\"type\",\"size\",\"count\"],\"additionalProperties\":false}}},\"required\":"
    "[\"tt\",\"razvertka\",\"holes\",\"elements\"],\"additionalProperties\":false}";
/* третье поколение (draw-lora3…): ещё габариты (только проставленные на листе) и где отверстие у тела
   вращения — по оси (на токарной), поперёк оси или в торце не по оси (на сверлильной); уступы и
   карманы деталей из листа, которые потом фрезеруют */
static const wchar_t kDrVlSheetQEl3[] =
    DR_VL_SHEET_HEAD L"gabarit — габаритные размеры, только если такой размер проставлен на листе: d — наибольший диаметр, "
                     L"l — общая длина детали, b — ширина, s — толщина (у детали из листа); чего нет на листе — null, "
                     L"цепочки размеров не складывай.\nelements — конструктивные элементы, видные на видах, разрезах и "
                     L"сечениях: type — из списка, size — размер, как подписан (нет подписи — пусто), count — сколько таких, "
                     L"where — у отверстий тела вращения: «по оси» (центральные, центровые), «поперёк оси» (поперечные и "
                     L"радиальные), «в торце не по оси» (по окружности фланца); у остальных — пусто. Резьба по оси — "
                     L"«резьба наружная» или «резьба внутренняя»; «уступ» и «карман» — фрезерованные, у плоских и "
                     L"корпусных деталей; фаски не перечисляй; если элементов нет — пустой список.\n" DR_VL_SHEET_TAIL;
static const char kDrVlSheetSchemaEl3[] =
    "{\"type\":\"object\",\"properties\":{\"tt\":{\"type\":\"array\",\"maxItems\":10,\"items\":{\"type\":\"string\","
    "\"maxLength\":220}},\"razvertka\":{\"type\":\"boolean\"},\"holes\":{\"type\":\"array\",\"maxItems\":6,\"items\":"
    "{\"type\":\"string\",\"maxLength\":40}},\"gabarit\":{\"type\":\"object\",\"properties\":{\"d\":{\"type\":[\"number\","
    "\"null\"]},\"l\":{\"type\":[\"number\",\"null\"]},\"b\":{\"type\":[\"number\",\"null\"]},\"s\":{\"type\":[\"number\","
    "\"null\"]}},\"required\":[\"d\",\"l\",\"b\",\"s\"],\"additionalProperties\":false},\"elements\":{\"type\":\"array\","
    "\"maxItems\":12,\"items\":{\"type\":\"object\",\"properties\":{\"type\":{\"type\":\"string\",\"enum\":[\"шпоночный "
    "паз\",\"шпоночный паз в отверстии\",\"паз\",\"лыска\",\"грани под ключ\",\"шлицы\",\"шлицы в отверстии\",\"зубья\","
    "\"резьба наружная\",\"резьба внутренняя\",\"резьбовое отверстие\",\"отверстие\",\"канавка\",\"рифление\",\"выдавка\","
    "\"сварной шов\",\"уступ\",\"карман\"]},\"size\":{\"type\":\"string\",\"maxLength\":30},\"count\":{\"type\":"
    "\"integer\",\"minimum\":1,\"maximum\":99},\"where\":{\"type\":\"string\",\"enum\":[\"\",\"по оси\",\"поперёк оси\","
    "\"в торце не по оси\"]}},\"required\":[\"type\",\"size\",\"count\",\"where\"],\"additionalProperties\":false}}},"
    "\"required\":[\"tt\",\"razvertka\",\"holes\",\"gabarit\",\"elements\"],\"additionalProperties\":false}";

/* строковое поле key объекта, начиная с p (до конца объекта end): значение без кавычек */
static void dr_json_field(const wchar_t *p, const wchar_t *end, const wchar_t *key, wchar_t *out, int cap) {
  out[0] = 0;
  wchar_t k[24];
  _snwprintf(k, 24, L"\"%s\"", key);
  k[23] = 0;
  const wchar_t *q = wcsstr(p, k);
  if (!q || q >= end) return;
  q += wcslen(k);
  while (*q == L' ' || *q == L':') q++;
  int n = 0;
  if (*q == L'"') {
    for (q++; *q && *q != L'"' && n < cap - 1; q++) {
      if (*q == L'\\' && q[1]) q++;
      out[n++] = *q;
    }
  } else {
    while ((*q >= L'0' && *q <= L'9') && n < cap - 1) out[n++] = *q++;
  }
  out[n] = 0;
}

/* габариты из JSON-ответа (третье поколение) — строкой «§GB d=62 l=180» для techno.c; null и 0 — нет.
   Пустая «§GB» — модель спросили, а на листе габаритов нет: techno.c попросит уточнить длину */
static void dr_json_gabarit(const wchar_t *j, wchar_t *out, int cap) {
  const wchar_t *p = j ? wcsstr(j, L"\"gabarit\"") : NULL, *end = p ? wcschr(p, L'}') : NULL;
  if (!end) return;
  wchar_t line[96] = L"§GB";
  static const wchar_t *const kKey[] = {L"d", L"l", L"b", L"s"};
  for (int i = 0; i < 4; i++) {
    wchar_t k[8];
    _snwprintf(k, 8, L"\"%s\"", kKey[i]);
    const wchar_t *q = wcsstr(p, k);
    if (!q || q >= end) continue;
    for (q += 3; *q == L' ' || *q == L':'; q++) {}
    double v = wcstod(q, NULL);
    if (v > 0 && v < 100000) {
      int n = (int)wcslen(line);
      _snwprintf(line + n, 96 - n, L" %s=%g", kKey[i], v);
    }
  }
  line[95] = 0;
  int len = (int)wcslen(out);
  if (len + (int)wcslen(line) + 2 < cap) _snwprintf(out + len, cap - len, L"\n%s", line);
}

/* элементы из JSON-ответа — строками для правил techno.c: «§EL код» для фрезерной, долбёжной,
   штамповки, накатки и сварки; отверстия — «n отв. размер», по оси тела вращения — «§AX n размер»
   (их сверлят на токарной), резьбы по оси — сам размер */
static void dr_json_elements(const wchar_t *j, wchar_t *out, int cap) {
  static const struct {
    const wchar_t *type, *code;
  } kEl[] = {{L"шпоночный паз в отверстии", L"keyin"}, {L"шпоночный паз", L"key"}, {L"паз", L"slot"},
             {L"лыска", L"flat"}, {L"грани под ключ", L"hex"}, {L"шлицы в отверстии", L"splinein"},
             {L"шлицы", L"spline"}, {L"зубья", L"gear"}, {L"рифление", L"knurl"}, {L"выдавка", L"emboss"},
             {L"сварной шов", L"weld"}, {L"уступ", L"step"}, {L"карман", L"pocket"}};
  const wchar_t *p = j ? wcsstr(j, L"\"elements\"") : NULL;
  while (p && (p = wcsstr(p, L"\"type\"")) != NULL) {
    const wchar_t *end = wcschr(p, L'}');
    if (!end) break;
    wchar_t type[40], size[40], count[8], where[24], line[96];
    dr_json_field(p, end, L"type", type, 40);
    dr_json_field(p, end, L"size", size, 40);
    dr_json_field(p, end, L"count", count, 8);
    dr_json_field(p, end, L"where", where, 24);
    int n = _wtoi(count);
    if (n < 1) n = 1;
    line[0] = 0;
    for (size_t i = 0; i < sizeof(kEl) / sizeof(kEl[0]) && !line[0]; i++)
      if (!wcscmp(type, kEl[i].type)) _snwprintf(line, 96, L"§EL %s", kEl[i].code);
    if (!line[0] && (!wcscmp(type, L"отверстие") || !wcscmp(type, L"резьбовое отверстие")))
      _snwprintf(line, 96, !wcscmp(where, L"по оси") ? L"§AX %d %s" : where[0] ? L"%d отв. %s (%s)" : L"%d отв. %s", n,
                 size, where);
    if (!line[0] && !wcsncmp(type, L"резьба", 6) && size[0]) lstrcpynW(line, size, 96);
    line[95] = 0;
    int len = (int)wcslen(out);
    if (line[0] && len + (int)wcslen(line) + 2 < cap) _snwprintf(out + len, cap - len, L"\n%s", line);
    p = end;
  }
}

/* графа «Обозначение» 65…185 × 0…15 мм от верха штампа высотой hmm (форма 1 — 55, форма 2 — 40) — моделью:
   для переименования по штампу и состава ЭСИ. Tesseract на этой графе путает буквы (АДЕ → АМЕ) и теряет
   хвост, а имя файла и состав должны быть точными. Выгрузка (native/train) — всегда: по ней считают «СБ» */
static void dr_vl_des(const DrImg *m, const DrStamp *s, DrPage *pg, int hmm) {
  DrImg im;
  if (!dr_img_part(m, s->sl + (int)(64 * s->k), s->by - (int)((hmm + 1) * s->k), s->sl + (int)(186 * s->k),
                   s->by - (int)((hmm - 16) * s->k), 2, 0, &im))
    return;
  wchar_t *a = dr_vl_ask(&im, L"des", L"На картинке — графа «Обозначение» основной надписи чертежа. Перепиши обозначение точно, "
                                      L"как написано: буквы, цифры, точки и дефисы, одной строкой. Только текст.", NULL, 40);
  dr_vl_line(a, pg->vlDes, DR_CELL);
  free(a);
  dr_img_free(&im);
}

/* графа «Наименование» моделью: 65…135 мм по ширине, от 15 мм под верхом штампа высотой hmm до низа графы
   (форма 1 — 25 мм над материалом, форма 2 — до низа штампа) */
static void dr_vl_name(const DrImg *m, const DrStamp *s, DrPage *pg, int hmm) {
  DrImg im;
  int y0 = s->by - (int)((hmm - 14) * s->k), y1 = hmm == 55 ? s->by - (int)(14 * s->k) : s->by + (int)(1 * s->k);
  if (!dr_img_part(m, s->sl + (int)(64 * s->k), y0, s->sl + (int)(136 * s->k), y1, 2, 0, &im)) return;
  wchar_t *a = dr_vl_ask(&im, L"name", L"На картинке — графа «Наименование» основной надписи чертежа. Перепиши текст точно, как "
                                       L"написано, одной строкой. Только текст.", NULL, 80);
  dr_vl_line(a, pg->vlName, DR_CELL);
  free(a);
  dr_img_free(&im);
}

/* ключи по-русски: с «des»/«name» модель путала графы — в обозначение писала наименование («Сухарь», «Ребро») */
static const wchar_t kDrVlCornerQ[] =
    L"На картинке — правый нижний угол листа чертежа с основной надписью (штампом) по ГОСТ 2.104. Перепиши из неё "
    L"точно, как написано: обозначение — код документа из букв и цифр с точками и дефисом (крупно, в верхней графе, "
    L"например «АБВГ.123456.001» или «КЛМ 1234-567.01.02»), наименование — название изделия словами (графа под "
    L"обозначением), материал (дробью — слово слева, потом верх дроби, « / », низ дроби), масса. Чего нет — пустая "
    L"строка. Ничего не придумывай.";
static const char kDrVlCornerSchema[] =
    "{\"type\":\"object\",\"properties\":{\"обозначение\":{\"type\":\"string\"},\"наименование\":{\"type\":\"string\"},"
    "\"материал\":{\"type\":\"string\"},\"масса\":{\"type\":\"string\"}},\"required\":[\"обозначение\",\"наименование\","
    "\"материал\",\"масса\"],\"additionalProperties\":false}";

/* Штамп по линиям не найден (копия с обрезом листа, рамка не та, лист без рамки) — графы читает модель по углу
   листа: правые 215 × нижние 80 мм по dpi файла (штамп 185 × 55 мм с запасом на рамку и копию в 95 %), без dpi —
   доли листа. Одним вопросом — обозначение, наименование, материал, масса; пустые графы штампа — из ответа */
static void dr_vl_corner(const DrImg *m, DrPage *pg) {
  int x0, y0;
  if (pg->kmm > 2) x0 = m->w - (int)(215 * pg->kmm), y0 = m->h - (int)(80 * pg->kmm);
  else if (m->w > m->h) x0 = m->w * 45 / 100, y0 = m->h * 65 / 100; /* альбомный: штамп в правом нижнем углу */
  else x0 = m->w / 4, y0 = m->h * 72 / 100;                           /* книжный A4: штамп внизу почти во всю ширину */
  DrImg im;
  if (!dr_img_part(m, x0, y0, m->w, m->h, 1, 1600, &im)) return;
  wchar_t *a = dr_vl_ask(&im, L"corner", kDrVlCornerQ, kDrVlCornerSchema, 200);
  dr_img_free(&im);
  if (!a) return;
  const wchar_t *end = a + wcslen(a);
  wchar_t v[DR_CELL], t[DR_CELL];
  dr_json_field(a, end, L"обозначение", pg->vlDes, DR_CELL);
  dr_json_field(a, end, L"наименование", pg->vlName, DR_CELL);
  dr_json_field(a, end, L"материал", pg->vlMat, DR_CELL);
  dr_json_field(a, end, L"масса", v, DR_CELL);
  free(a);
  ai_log(L"Штамп по линиям не найден — графы прочитала модель по углу листа: «%s»", pg->vlDes);
  if (dr_find_des(pg->vlDes, t, DR_CELL) && dr_des_ok(t)) {
    if (!pg->f[DF_DES][0] || pg->doubt[DF_DES]) lstrcpynW(pg->f[DF_DES], t, DR_CELL), pg->doubt[DF_DES] = 0;
    if (pg->type == DRT_OTHER) pg->type = DRT_DRAW; /* штамп с обозначением есть — это чертёж */
  }
  if (!pg->f[DF_NAME][0] && pg->vlName[0]) lstrcpynW(pg->f[DF_NAME], pg->vlName, DR_CELL);
  if (!pg->f[DF_MAT][0] && pg->vlMat[0]) lstrcpynW(pg->f[DF_MAT], pg->vlMat, DR_CELL), pg->doubt[DF_MAT] = 0;
  if (!pg->f[DF_MASS][0] && v[0] && dr_number(v, t, DR_CELL, TRUE)) {
    dr_mass_kg(v, t, DR_CELL);
    lstrcpynW(pg->f[DF_MASS], t, DR_CELL), pg->doubt[DF_MASS] = 0;
  }
}

static BOOL dr_vl_dumping(void) {
#ifdef SET_TEST
  return g_drVlDump[0] != 0;
#else
  return FALSE;
#endif
}

/* sheet — спросить и весь лист (ТТ, отверстия, элементы, габариты); FALSE — только графы штампа */
static void dr_vl_read(const DrImg *m, const DrStamp *s, DrPage *pg, BOOL sheet) {
  DrImg im;
  wchar_t *a;
  if (s) { /* графы штампа формы 1: наименование 65…135 × 15…40 мм, материал 65…135 × 40…55 мм от верха */
    int x0 = s->sl + (int)(64 * s->k), x1 = s->sl + (int)(136 * s->k);
    if (pg->vlStamp || dr_vl_dumping()) dr_vl_des(m, s, pg, 55);
    if (pg->vlStamp != 1) dr_vl_name(m, s, pg, 55);
    /* материал имени не нужен; маршруту по листу (ЭСИ и ТП: штамп и операции сразу) — нужен */
    if ((!pg->vlStamp || pg->wantText) && dr_img_part(m, x0, s->by - (int)(16 * s->k), x1, s->by + (int)(1 * s->k), 2, 0, &im)) {
      a = dr_vl_ask(&im, L"mat", L"На картинке — графа «Материал» основной надписи чертежа. Перепиши точно, как написано. Если "
                         L"материал записан дробью — слово слева, потом верх дроби, « / », низ дроби, например «Лист 8 "
                         L"ГОСТ 19903-15 / 20 ГОСТ 1577-93». Только текст одной строкой.", NULL, 80);
      dr_vl_line(a, pg->vlMat, DR_CELL);
      free(a);
      dr_img_free(&im);
    }
  } else {
    dr_vl_corner(m, pg); /* линии штампа не найдены — всё равно модель, по углу листа, а не пусто */
  }
  wchar_t t[DR_CELL];
  if (s && (pg->vlStamp || dr_vl_dumping()) && !(dr_find_des(pg->vlDes, t, DR_CELL) && dr_des_ok(t)))
    dr_vl_corner(m, pg); /* штамп найден со сдвигом (обрез листа на копии) — графа мимо: по углу листа */
  if (sheet && dr_img_part(m, 0, 0, m->w, m->h, 1, 1600, &im)) {
    /* конструктивные элементы и габариты спрашиваем, только если стоит адаптер, обученный их
       называть (draw-lora2… — элементы, draw-lora3… — и габариты): без обучения модель их выдумывает */
    int gen = ai_draw_gen();
#ifdef SET_TEST
    const char *ge = getenv("CP_DREL"); /* выгрузка — по вопросу последнего поколения; CP_DREL=2 — по второму */
    if (ge || g_drVlDump[0]) gen = ge && atoi(ge) ? atoi(ge) : 3;
#endif
    BOOL el = gen >= 2;
    a = dr_vl_ask(&im, L"sheet", gen >= 3 ? kDrVlSheetQEl3 : el ? kDrVlSheetQEl : kDrVlSheetQ,
                  gen >= 3 ? kDrVlSheetSchemaEl3 : el ? kDrVlSheetSchemaEl : kDrVlSheetSchema,
                  gen >= 3 ? 1400 : el ? 1200 : 900);
    if (a) {
      pg->vlText = (wchar_t *)calloc(4096, sizeof(wchar_t));
      if (pg->vlText) {
        dr_json_strings(a, L"tt", pg->vlText, 4096);
        if (wcsstr(a, L"\"razvertka\":true") || wcsstr(a, L"\"razvertka\": true"))
          lstrcpynW(pg->vlText + wcslen(pg->vlText), L"\nРазвертка", 4096 - (int)wcslen(pg->vlText));
        dr_json_strings(a, L"holes", pg->vlText, 4096);
        if (gen >= 3) dr_json_gabarit(a, pg->vlText, 4096);
        if (el) dr_json_elements(a, pg->vlText, 4096);
      }
      free(a);
    }
    /* габариты отдельно: адаптер второго поколения читает лист, адаптер габаритов (v2.5) отвечает
       на вопрос третьего поколения, и из его ответа берутся только габариты. Замер 29.10 на 22
       отложенных листах: всё как у v2, габариты верно 38 из 50 (у v2 их не было) */
    if (gen == 2 && pg->vlText && ai_draw_gab()) {
      a = dr_vl_ask(&im, L"gab", kDrVlSheetQEl3, kDrVlSheetSchemaEl3, 1400);
      if (a) dr_json_gabarit(a, pg->vlText, 4096);
      free(a);
    }
    dr_img_free(&im);
  }
}

/* ---- спецификация моделью -------------------------------------------------------------
   Таблица целиком (от верха таблицы до штампа, во всю ширину) — строки JSON;
   раздел — у каждой строки, перенос наименования — одной строкой. Ответ ложится в те же DrSpecRow,
   что и строки Tesseract: состав сборки (pk_stamp.c) берёт их как есть. С 31.10 — в работе: модель готова —
   строки её, строки Tesseract — только запасом (модели нет или она ничего не вернула). Замер на заводских
   копиях: модель 45 из 61 строки целиком, Tesseract по сетке — 20; на стенде — CP_SPECVL с CP_DRVL */
static const wchar_t kDrVlSpecQ[] =
    L"На картинке — таблица спецификации (ГОСТ 2.106) с графами «Формат», «Зона», «Поз.», «Обозначение», "
    L"«Наименование», «Кол.», «Примечание». Перепиши все заполненные строки сверху вниз точно, как написано, "
    L"ничего не придумывая. Заголовок раздела («Документация», «Сборочные единицы», «Детали», «Стандартные "
    L"изделия», «Прочие изделия», «Материалы», «Комплекты») — не строка: впиши его в sect у каждой строки под ним; "
    L"строки выше первого заголовка — sect пустой. Наименование, перенесённое на следующую строку таблицы, — "
    L"в той же строке через пробел. Пустые строки и «Резерв» пропусти. Пустая графа — пустая строка.";
static const char kDrVlSpecSchema[] =
    "{\"type\":\"object\",\"properties\":{\"rows\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{"
    "\"sect\":{\"type\":\"string\",\"enum\":[\"\",\"Документация\",\"Комплексы\",\"Сборочные единицы\",\"Детали\","
    "\"Стандартные изделия\",\"Прочие изделия\",\"Материалы\",\"Комплекты\"]},\"fmt\":{\"type\":\"string\"},"
    "\"zone\":{\"type\":\"string\"},\"pos\":{\"type\":\"string\"},\"des\":{\"type\":\"string\"},"
    "\"name\":{\"type\":\"string\"},\"qty\":{\"type\":\"string\"},\"note\":{\"type\":\"string\"}},"
    "\"required\":[\"sect\",\"fmt\",\"zone\",\"pos\",\"des\",\"name\",\"qty\",\"note\"],\"additionalProperties\":false}}},"
    "\"required\":[\"rows\"],\"additionalProperties\":false}";
#ifdef SET_TEST
/* замер скорости (CP_SPECFMT=2): ответ короче — раздел один раз на группу строк, без «Формата» и «Зоны» */
static const wchar_t kDrVlSpecQ2[] =
    L"На картинке — таблица спецификации (ГОСТ 2.106). Перепиши все заполненные строки сверху вниз точно, как "
    L"написано, ничего не придумывая, сгруппировав по разделам: заголовок раздела («Документация», «Сборочные "
    L"единицы», «Детали», «Стандартные изделия», «Прочие изделия», «Материалы», «Комплекты») — sect группы, строки "
    L"под ним — её rows; строки выше первого заголовка — группа с пустым sect. В строке: pos — «Поз.», des — "
    L"«Обозначение», name — «Наименование», qty — «Кол.», note — «Примечание». Наименование, перенесённое на "
    L"следующую строку таблицы, — в той же строке через пробел. Пустые строки и «Резерв» пропусти.";
static const char kDrVlSpecSchema2[] =
    "{\"type\":\"object\",\"properties\":{\"sections\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{"
    "\"sect\":{\"type\":\"string\",\"enum\":[\"\",\"Документация\",\"Комплексы\",\"Сборочные единицы\",\"Детали\","
    "\"Стандартные изделия\",\"Прочие изделия\",\"Материалы\",\"Комплекты\"]},\"rows\":{\"type\":\"array\",\"items\":{"
    "\"type\":\"object\",\"properties\":{\"pos\":{\"type\":\"string\"},\"des\":{\"type\":\"string\"},\"name\":{\"type\":"
    "\"string\"},\"qty\":{\"type\":\"string\"},\"note\":{\"type\":\"string\"}},\"required\":[\"pos\",\"des\",\"name\","
    "\"qty\",\"note\"],\"additionalProperties\":false}}},\"required\":[\"sect\",\"rows\"],\"additionalProperties\":false}}},"
    "\"required\":[\"sections\"],\"additionalProperties\":false}";
#endif
/* таблица плотная: при 1600 лист 200 dpi ужимался вдвое — буквы 3,5 мм в 14 точек. До 3000 по длинной стороне
   лист идёт как есть, остальное llama.cpp ужимает сама до своих 4096 токенов картинки (models.json) */
#define DR_SPEC_SIDE 3000
static const wchar_t *const kDrSpecKey[DS_N] = {L"sect", L"fmt", L"zone", L"pos", L"des", L"name", L"qty", L"note"};

/* конец JSON-объекта с p (p — на «{»), строки в кавычках — мимо */
static const wchar_t *dr_json_obj_end(const wchar_t *p) {
  int depth = 0;
  for (BOOL str = FALSE; *p; p++) {
    if (str) {
      if (*p == L'\\' && p[1]) p++;
      else if (*p == L'"') str = FALSE;
    } else if (*p == L'"') str = TRUE;
    else if (*p == L'{') depth++;
    else if (*p == L'}' && --depth == 0) return p;
  }
  return NULL;
}

/* строка спецификации — документ изделия: код документа в конце обозначения («…00СБ», «… Э3») или «Сборочный
   чертеж», «Схема…», «Пояснительная записка» в начале наименования */
static BOOL dr_spec_doc_row(const DrSpecRow *o) {
  static const wchar_t *const kCode[] = {L"СБ", L"ВО", L"ГЧ", L"МЧ", L"МЭ", L"УЧ", L"ПЗ", L"ТУ", L"ПС", L"РЭ", L"Э3", L"ТЧ", L"ВП"};
  static const wchar_t *const kPhrase[] = {L"сборочный черт", L"чертеж общего", L"габаритный черт", L"монтажный черт",
                                           L"схема ", L"пояснительная"};
  const wchar_t *d = o->c[DS_DES];
  int e = (int)wcslen(d);
  for (size_t i = 0; i < sizeof(kCode) / sizeof(kCode[0]); i++) {
    int k = e - (int)wcslen(kCode[i]);
    if (k > 0 && !wcscmp(d + k, kCode[i]) && (dr_isdig(d[k - 1]) || d[k - 1] == L' ')) return TRUE;
  }
  wchar_t low[24];
  lstrcpynW(low, o->c[DS_NAME], 24);
  CharLowerBuffW(low, (DWORD)wcslen(low));
  for (size_t i = 0; i < sizeof(kPhrase) / sizeof(kPhrase[0]); i++)
    if (!wcsncmp(low, kPhrase[i], wcslen(kPhrase[i]))) return TRUE;
  return FALSE;
}

/* строка — продолжение наименования строкой материала («Лист Б-ПН-0-20 ГОСТ 19903-74 / Ст3пс5…»): ни позиции, ни
   количества, ни формата, а в «Обозначении» слово без цифр */
static BOOL dr_spec_mat_line(const DrSpecRow *o) {
  if (o->c[DS_POS][0] || o->c[DS_QTY][0] || o->c[DS_FMT][0] || !o->c[DS_DES][0]) return FALSE;
  for (const wchar_t *q = o->c[DS_DES]; *q; q++)
    if (dr_isdig(*q)) return FALSE;
  return TRUE;
}

/* строки спецификации из ответа модели — вместо прочитанных Tesseract; FALSE — строк нет */
static BOOL dr_json_spec(const wchar_t *j, DrPage *pg) {
  const wchar_t *p = j ? wcsstr(j, L"\"rows\"") : NULL;
  if (!p || !(p = wcschr(p, L'['))) return FALSE;
  int cap = 0, n = 0;
  wchar_t sect[DR_CELL] = {0};
  for (const wchar_t *q = p; (q = wcschr(q, L'{')) != NULL; q++) cap++;
  DrSpecRow *rows = cap ? (DrSpecRow *)calloc((size_t)cap, sizeof(DrSpecRow)) : NULL;
  for (const wchar_t *q = p; rows && n < cap && (q = wcschr(q, L'{')) != NULL;) {
    const wchar_t *end = dr_json_obj_end(q);
    if (!end) break;
    wchar_t v[DR_CELL];
    DrSpecRow *o = &rows[n];
    BOOL any = FALSE;
    for (int c = 0; c < DS_N; c++) {
      dr_json_field(q, end, kDrSpecKey[c], v, DR_CELL);
      int l = (int)wcslen(v), b = 0; /* только пробелы: dr_trim срезал бы и кавычки «"А"» — у модели это не рамка */
      while (b < l && v[b] == L' ') b++;
      while (l > b && v[l - 1] == L' ') l--;
      memmove(v, v + b, (size_t)(l - b) * sizeof(wchar_t));
      v[l - b] = 0;
      if (c == DS_SECT) lstrcpynW(o->c[c], v, DR_CELL);
      else dr_spec_cell(o, c, v, 100), any |= v[0] != 0;
    }
    /* как у строк Tesseract (dr_spec): строка из одного заголовка раздела — раздел для строк под ней (модель,
       не обученная этому, пишет его отдельной строкой — в «Наименование» или в sect), строка из одного
       наименования — перенос к предыдущей (материал дробью под «Планкой»), «Резерв» и пустые — прочь */
    BOOL onlyName = TRUE;
    for (int c = DS_FMT; c < DS_N; c++)
      if (c != DS_NAME && o->c[c][0]) onlyName = FALSE;
    const wchar_t *head = !onlyName ? NULL : o->c[DS_NAME][0] ? o->c[DS_NAME] : o->c[DS_SECT];
    if (head && head[0] && dr_section(head) >= 0) {
      /* заголовок после первой строки своего раздела (модель так ошибается): строка, записанная в
         «Документацию», хотя не документ, — к этому разделу */
      if (n > 0 && rows[n - 1].doubt[DS_SECT] && dr_section(rows[n - 1].c[DS_SECT]) == 0 && dr_section(head) != 0)
        lstrcpynW(rows[n - 1].c[DS_SECT], head, DR_CELL), rows[n - 1].doubt[DS_SECT] = 0;
      lstrcpynW(sect, head, DR_CELL);
    } else if (!any || (!o->c[DS_DES][0] && dr_spec_reserve(o->c[DS_NAME]))) {
    } else if (onlyName && n > 0) {
      size_t l = wcslen(rows[n - 1].c[DS_NAME]);
      _snwprintf(rows[n - 1].c[DS_NAME] + l, DR_CELL - l, L"%s%s", l ? L" " : L"", o->c[DS_NAME]);
    } else if (n > 0 && dr_spec_mat_line(o)) { /* материал дробью под «Планкой»: «Лист» модель кладёт в обозначение */
      size_t l = wcslen(rows[n - 1].c[DS_NAME]);
      _snwprintf(rows[n - 1].c[DS_NAME] + l, DR_CELL - l, L"%s%s %s", l ? L" " : L"", o->c[DS_DES], o->c[DS_NAME]);
      dr_trim(rows[n - 1].c[DS_NAME]);
    } else {
      BOOL doc = dr_spec_doc_row(o);
      if (doc) lstrcpynW(o->c[DS_SECT], L"Документация", DR_CELL); /* документ — по содержанию, где бы ни был заголовок */
      else if (!o->c[DS_SECT][0]) lstrcpynW(o->c[DS_SECT], sect, DR_CELL);
      else if (dr_section(o->c[DS_SECT]) >= 0) lstrcpynW(sect, o->c[DS_SECT], DR_CELL);
      if (!doc && dr_section(o->c[DS_SECT]) == 0) o->doubt[DS_SECT] = 1; /* в «Документации», а не документ */
      wchar_t low[16];
      lstrcpynW(low, o->c[DS_NAME], 16);
      CharLowerBuffW(low, (DWORD)wcslen(low));
      int e = (int)wcslen(o->c[DS_DES]);
      if (!wcsncmp(low, L"сборочный черт", 14) && (e < 2 || wcscmp(o->c[DS_DES] + e - 2, L"СБ")))
        o->doubt[DS_DES] = 1; /* сборочный чертёж без «СБ» в конце: рукописное «СБ» модель читает «.05» */
      n++;
      q = end + 1;
      continue;
    }
    memset(o, 0, sizeof(*o));
    q = end + 1;
  }
  if (!n) {
    free(rows);
    return FALSE;
  }
  free(pg->rows);
  pg->rows = rows;
  pg->nrows = n;
  pg->rowsBy = 2;
  return TRUE;
}

static void dr_vl_spec(const DrImg *m, int x0, int y0, int x1, int y1, DrPage *pg) {
#ifdef SET_TEST
  BOOL want = g_drVlDump[0] || getenv("CP_SPECVL");
#else
  BOOL want = TRUE;
#endif
  DrImg im;
  int side = DR_SPEC_SIDE;
#ifdef SET_TEST
  if (getenv("CP_SPECSIDE")) side = atoi(getenv("CP_SPECSIDE")); /* замер скорости: картинка меньше — токенов меньше */
#endif
  if (!want || pg->stampOnly || !dr_vl_want() || !dr_img_part(m, x0, y0, x1, y1, 1, side, &im)) return;
#ifdef SET_TEST
  if (g_drVlDump[0]) { /* строки Tesseract — тем же JSON, что у модели: замер «как сейчас» (eval_model.py) */
    size_t cap = 64 + (size_t)pg->nrows * DS_N * (DR_CELL * 2 + 16), n = 0;
    wchar_t *js = (wchar_t *)malloc(cap * sizeof(wchar_t)), p[MAX_PATH];
    if (js) {
      n += _snwprintf(js + n, cap - n, L"{\"rows\":[");
      for (int r = 0; r < pg->nrows; r++) {
        n += _snwprintf(js + n, cap - n, L"%s{", r ? L"," : L"");
        for (int c = 0; c < DS_N; c++) {
          n += _snwprintf(js + n, cap - n, L"%s\"%s\":\"", c ? L"," : L"", kDrSpecKey[c]);
          for (const wchar_t *v = pg->rows[r].c[c]; *v && n + 3 < cap; v++) {
            if (*v == L'"' || *v == L'\\') js[n++] = L'\\';
            js[n++] = *v < 32 ? L' ' : *v;
          }
          n += _snwprintf(js + n, cap - n, L"\"");
        }
        n += _snwprintf(js + n, cap - n, L"}");
      }
      _snwprintf(js + n, cap - n, L"]}");
      js[cap - 1] = 0;
      _snwprintf(p, MAX_PATH, L"%s\\%s.spec_tess.json", g_drVlDump, g_drVlBase);
      p[MAX_PATH - 1] = 0;
      share_write_ex(p, js, TRUE);
      free(js);
    }
  }
#endif
  const wchar_t *q = kDrVlSpecQ;
  const char *sch = kDrVlSpecSchema;
#ifdef SET_TEST
  if (getenv("CP_SPECFMT")) q = kDrVlSpecQ2, sch = kDrVlSpecSchema2; /* разбор такого ответа — только в замере (cpvl) */
#endif
  wchar_t *a = dr_vl_ask(&im, L"spec", q, sch, 2500);
  /* ответ оборван на пределе (лист — не больше 30 строк, это ~1700 токенов): модель зациклилась — строки
     Tesseract, если есть, с «проверьте» лучше обрывка */
  BOOL cut = a && !wcschr(a, L']');
  if (cut && pg->nrows) ai_log(L"Спецификация: ответ модели оборван (зациклилась) — строки Tesseract (%d)", pg->nrows);
  else if (a && dr_json_spec(a, pg)) ai_log(L"Спецификация: строки прочитала модель (%d)", pg->nrows);
  free(a);
  dr_img_free(&im);
}

/* Строки, прочитанные одним Tesseract (модели нет или она не ответила): «Сборочный чертеж» без номера —
   Tesseract берёт от него только код («АNЕ») — обозначением самой сборки с «СБ» и под вопросом, в
   «Документацию»; обозначение без цифр — под вопросом: всё это ляжет в состав с «проверьте» */
static void dr_spec_tess_rows(DrPage *pg) {
  wchar_t own[DR_CELL] = L"", v[DR_CELL];
  if (pg->vlDes[0] && dr_find_des(pg->vlDes, v, DR_CELL) && dr_des_ok(v)) lstrcpynW(own, v, DR_CELL);
  else if (pg->f[DF_DES][0] && !pg->doubt[DF_DES] && dr_des_ok(pg->f[DF_DES])) lstrcpynW(own, pg->f[DF_DES], DR_CELL);
  else if (dr_find_des(tm_base(pg->path), v, DR_CELL) && dr_des_ok(v)) { /* штамп не прочитан — имя файла сборки */
    int e = (int)wcslen(v);
    if (e > 2 && !wcscmp(v + e - 2, L"СБ")) v[e - 2] = 0, dr_trim(v);
    lstrcpynW(own, v, DR_CELL);
  }
  for (int r = 0; r < pg->nrows; r++) {
    DrSpecRow *o = &pg->rows[r];
    wchar_t *d = o->c[DS_DES], low[24];
    BOOL dig = FALSE;
    for (const wchar_t *q = d; *q; q++) dig |= dr_isdig(*q);
    if (d[0] && !dig) o->doubt[DS_DES] = 1;
    int sc = dr_section(o->c[DS_SECT]);
    if (!d[0] && (sc == 2 || sc == 3)) o->doubt[DS_DES] = 1; /* у сборочной единицы и детали обозначение есть всегда */
    const wchar_t *n = o->c[DS_NAME];
    while (*n && !IsCharAlphaW(*n)) n++;
    lstrcpynW(low, n, 24);
    CharLowerBuffW(low, (DWORD)wcslen(low));
    if (!wcsstr(low, L"борочный черт")) continue; /* «(борочный чертех»: курсив теряет «С» и путает «ж» */
    lstrcpynW(o->c[DS_SECT], L"Документация", DR_CELL);
    lstrcpynW(o->c[DS_NAME], L"Сборочный чертеж", DR_CELL);
    o->doubt[DS_SECT] = 0;
    if (!dig && own[0]) _snwprintf(d, DR_CELL, L"%sСБ", own), d[DR_CELL - 1] = 0, o->doubt[DS_DES] = 1;
  }
}

static void dr_recognize(IWICImagingFactory *f, DrPage *pg) {
  ULONGLONG t0 = GetTickCount64();
  dr_page_free(pg);
  memset(pg->f, 0, sizeof(pg->f));
  memset(pg->doubt, 0, sizeof(pg->doubt));
  pg->note[0] = pg->fmt[0] = 0;
  pg->type = DRT_OTHER;
  pg->form = 0;
  pg->rowsBy = 0;
  files_des_codes_dir(pg->path); /* свои коды обозначений — и по именам файлов рядом с листом */
  DrImg m;
  double dpi = 96;
  HRESULT hr = dr_read(f, pg->path, pg->frame, &m, &dpi);
  if (FAILED(hr)) {
    wchar_t why[120];
    tm_why(hr, why, 120);
    _snwprintf(pg->note, 300, L"не открылся: %s (0x%08X)", why, (unsigned)hr);
    pg->done = TRUE;
    return;
  }
  int thr = dr_otsu(&m);
  double ang = dr_skew(&m, thr, FALSE), angv = dr_skew(&m, thr, TRUE);
  dr_deskew(&m, ang, angv);
#ifdef SET_TEST
  g_drAng = ang, g_drAngV = angv;
#endif
  DrLines L;
  DrStamp s;
  wchar_t why[200] = {0};
  BOOL stamp = dr_lines(&m, thr, &L) && dr_find_stamp(&L, dpi, &s, why, 200);
#ifdef SET_TEST
  if (getenv("CP_DRDEBUG")) dr_debug_img(&L, stamp ? &s : NULL, pg);
  if (stamp && getenv("CP_DRSTAMP")) { /* проверка чтения: штамп целиком — в папку CP_DRSTAMP */
    DrImg st;
    int pad = (int)(2 * s.k);
    if (dr_img_new(&st, s.rx - s.sl + 2 * pad, s.by - s.st + 2 * pad)) {
      for (int y = 0; y < st.h; y++)
        for (int x = 0; x < st.w; x++) {
          int sx = s.sl - pad + x, sy = s.st - pad + y;
          st.px[(size_t)y * st.w + x] = sx >= 0 && sx < m.w && sy >= 0 && sy < m.h ? m.px[(size_t)sy * m.w + sx] : 255;
        }
      wchar_t sp[MAX_PATH], dir[MAX_PATH];
      MultiByteToWideChar(CP_UTF8, 0, getenv("CP_DRSTAMP"), -1, dir, MAX_PATH);
      _snwprintf(sp, MAX_PATH, L"%s\\%s.stamp.bmp", dir, tm_base(pg->path));
      dr_save_bmp(&st, sp);
      dr_img_free(&st);
    }
  }
#endif
  double k = stamp ? s.k : (ts_dpi_known(dpi) ? dpi / 25.4 : 0);
  pg->kmm = k;
  if (stamp) {
    pg->form = s.form;
    const DrBox *bx = s.form == 1 ? kDrForm1 : s.form == 2 ? kDrForm2 : kDrForm2a;
    int nb = s.form == 1 ? XP_N(kDrForm1) : s.form == 2 ? XP_N(kDrForm2) : XP_N(kDrForm2a);
    DrImg crops[8];
    int fld[8], n = 0;
    for (int i = 0; i < nb; i++) {
      const DrBox *b = &bx[i];
      int x0, y0, x1, y1;
      dr_box_rect(&L, &s, b, &x0, &y0, &x1, &y1);
      if (dr_crop(&m, thr, x0, y0, x1, y1, k, &crops[n])) fld[n++] = b->field;
    }
    DrWords ws;
    memset(&ws, 0, sizeof(ws));
    if (dr_tess(crops, n, 6, &ws, why, 200)) {
      for (int i = 0; i < n; i++) {
        wchar_t t[DR_CELL];
        int conf;
        dr_page_text(&ws, i + 1, fld[i] == DF_MAT ? L" / " : L" ", t, DR_CELL, &conf);
        dr_field_set(pg, fld[i], t, conf);
      }
    } else {
      lstrcpynW(pg->note, why, 300);
    }
    dr_words_free(&ws);
    for (int i = 0; i < n; i++) dr_img_free(&crops[i]);
    if (s.form == 1) {
      pg->type = DRT_DRAW;
    } else {
      /* столбцы спецификации над штампом: 6, 12, 20, 90, 153, 163 мм */
      int cols = 0;
      for (int c = 1; c < 7; c++) {
        int top = dr_vup(&L, s.sl + (int)(kDrSpecX[c] * k + 0.5), (int)(1.5 * k) + 1, s.st, (int)(2 * k) + 2,
                         (int)(1.5 * k) + 2);
        if (top >= 0 && (s.st - top) / k >= 20) cols++;
      }
      int head = 0;
      BOOL byHead = cols < 4 && dr_spec_header(&m, thr, &s, k, &head); /* столбцы от штампа мимо, а шапка есть */
      if (cols >= 4 || byHead) {
        pg->type = DRT_SPEC;
        if (!dr_spec(&m, thr, &L, &s, pg)) { /* сетка не подогналась: строки — только моделью */
          int y0 = byHead ? head : s.st - (int)(250 * k); /* без шапки — от верха рамки A4 */
          dr_vl_spec(&m, s.sl - (int)(15 * k), y0 < 0 ? 0 : y0, s.rx, s.st, pg);
          if (pg->rowsBy == 2) pg->note[0] = 0;
          else lstrcpynW(pg->note, L"таблица спецификации не разобрана — строки не прочитаны", 300);
        }
        /* обозначение спецификации — обозначение самой сборки (без «СБ»), наименование — её же: моделью, как у
           чертежа (Tesseract на курсиве заводских копий даёт «Гриспосодление SYOOTONOLXHOE») */
        if (s.form == 2 && (pg->vlStamp || dr_vl_dumping()) && dr_vl_want()) {
          dr_vl_des(&m, &s, pg, 40);
          if (pg->vlStamp != 1) dr_vl_name(&m, &s, pg, 40);
        }
        if (pg->rowsBy == 1) dr_spec_tess_rows(pg);
      }
    }
  } else {
    /* штампа нет: читаем лист целиком (уменьшенным) */
    wchar_t *text = (wchar_t *)malloc(sizeof(wchar_t) * 16384);
    int conf = 0;
    wchar_t why2[200] = {0};
    if (text && dr_whole_text(&m, text, 16384, &conf, why2, 200)) {
      wchar_t *low = _wcsdup(text);
      if (low) {
        CharLowerBuffW(low, (DWORD)wcslen(low));
        if (dr_has(low, L"маршрутн") || dr_has(low, L"3.1118") || dr_has(low, L"3.1105") ||
            dr_has(low, L"3.1404") || dr_has(low, L"операционн") || dr_has(low, L"технологическ"))
          pg->type = DRT_TP;
        free(low);
      }
      wchar_t des[DR_CELL];
      if (dr_find_des(text, des, DR_CELL)) {
        lstrcpyW(pg->f[DF_DES], des);
        pg->doubt[DF_DES] = !dr_des_ok(des);
      }
      if (pg->type != DRT_TP) _snwprintf(pg->note, 300, L"штамп не найден (%s)", why);
    } else {
      _snwprintf(pg->note, 300, L"штамп не найден (%s); %s", why, why2);
    }
    free(text);
  }
  if ((g_drText || pg->wantText) && (pg->type == DRT_DRAW || pg->type == DRT_OTHER)) {
    pg->text = (wchar_t *)malloc(sizeof(wchar_t) * 32768);
    wchar_t why3[200] = {0};
    if (pg->text && !dr_sheet_text(&m, pg->text, 32768, why3, 200)) {
      free(pg->text);
      pg->text = NULL;
    }
    if (dr_vl_want() && pg->vlLater) { /* черновик — сейчас, модель — dr_vl_later: лист забираем себе */
      DrVlKeep *k = (DrVlKeep *)calloc(1, sizeof(DrVlKeep));
      if (k) {
        k->m = m;
        m.px = NULL;
        k->hasStamp = stamp && s.form == 1;
        if (k->hasStamp) k->s = s;
        pg->vlKeep = k;
      } else
        dr_vl_read(&m, stamp && s.form == 1 ? &s : NULL, pg, TRUE);
    } else if (dr_vl_want())
      dr_vl_read(&m, stamp && s.form == 1 ? &s : NULL, pg, TRUE);
  } else if (pg->vlStamp && dr_vl_want() && (pg->type == DRT_DRAW || pg->type == DRT_OTHER))
    dr_vl_read(&m, stamp && s.form == 1 ? &s : NULL, pg, FALSE); /* штамп моделью, без вопросов по листу; не найден — по углу */
  if (!pg->f[DF_DES][0] && pg->type != DRT_OTHER) pg->doubt[DF_DES] = 1;
  /* формат листа: по масштабу штампа, без него — по dpi файла */
  if (k > 0) {
    double a = m.w / k, b = m.h / k;
    const wchar_t *p = ts_paper(a < b ? a : b, a < b ? b : a, 8);
    if (p) lstrcpynW(pg->fmt, p, 16);
  }
  dr_lines_free(&L);
  dr_img_free(&m);
  pg->secs = (GetTickCount64() - t0) / 1000.0;
  pg->done = TRUE;
}

/* второй проход: отложенный лист читает модель (ТТ, штамп, элементы); FALSE — откладывать было нечего */
static BOOL dr_vl_later(DrPage *pg) {
  DrVlKeep *k = (DrVlKeep *)pg->vlKeep;
  if (!k) return FALSE;
  ULONGLONG t0 = GetTickCount64();
  dr_vl_read(&k->m, k->hasStamp ? &k->s : NULL, pg, TRUE);
  dr_img_free(&k->m);
  free(k);
  pg->vlKeep = NULL;
  pg->secs += (GetTickCount64() - t0) / 1000.0;
  return TRUE;
}

/* ---- страницы файлов --------------------------------------------------------------- */

typedef struct {
  DrPage *p;
  int n, cap;
} DrPages;

static void dr_pages_cb(void *ctx, TmPage *tp) {
  DrPages *l = (DrPages *)ctx;
  if (l->n >= l->cap) {
    int c = l->cap ? l->cap * 2 : 16;
    DrPage *q = (DrPage *)realloc(l->p, sizeof(DrPage) * (size_t)c);
    if (!q) {
      if (tp->thumb) DeleteObject(tp->thumb);
      free(tp);
      return;
    }
    l->p = q;
    l->cap = c;
  }
  DrPage *pg = &l->p[l->n++];
  memset(pg, 0, sizeof(*pg));
  lstrcpynW(pg->path, tp->path, MAX_PATH);
  pg->frame = tp->frame;
  if (tp->thumb) DeleteObject(tp->thumb);
  free(tp);
}

#ifdef SET_TEST
/* --drspecjson ответ.json выход.txt — ответ модели по спецификации (UTF-8) → строки DrSpecRow, как их
   получит состав ЭСИ: графа = значение, «?» — подсвечена */
static void dr_spec_json_test(const wchar_t *in, const wchar_t *out) {
  char *raw = NULL;
  DWORD n = 0;
  DrPage pg;
  memset(&pg, 0, sizeof(pg));
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  if (read_file_bytes(in, &raw, &n, 1 << 20)) {
    int wn = MultiByteToWideChar(CP_UTF8, 0, raw, (int)n, NULL, 0);
    wchar_t *w = (wchar_t *)calloc((size_t)wn + 1, sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, raw, (int)n, w, wn);
    BOOL ok = w && dr_json_spec(w, &pg);
    sb_add(&b, L"ok=%d rows=%d by=%d\n", ok, pg.nrows, pg.rowsBy);
    for (int r = 0; r < pg.nrows; r++) {
      for (int c = 0; c < DS_N; c++)
        sb_add(&b, L"%s%s%s=%s", c ? L" | " : L"", pg.rows[r].doubt[c] ? L"?" : L"", kDrSpecKey[c], pg.rows[r].c[c]);
      sb_add(&b, L"\n");
    }
    free(w);
    free(raw);
  }
  share_write_ex(out, b.w ? b.w : L"", TRUE);
  free(b.w);
  free(pg.rows);
  ExitProcess(0);
}

/* проверочная сборка: --drawrec выход.txt файл… — распознать и записать итог */
static void dr_test(int argc, wchar_t **argv) {
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  IWICImagingFactory *f = NULL;
  CoCreateInstance(&kTmClsidFactory, NULL, CLSCTX_INPROC_SERVER, &kTmIidFactory, (void **)&f);
  DrPages l;
  memset(&l, 0, sizeof(l));
  for (int i = 3; i < argc && f; i++) tm_probe(f, argv[i], FALSE, dr_pages_cb, &l);
  g_drText = getenv("CP_DRTEXT") != NULL; /* и текст всего листа */
  if (getenv("CP_DRVLDUMP")) {
    MultiByteToWideChar(CP_UTF8, 0, getenv("CP_DRVLDUMP"), -1, g_drVlDump, MAX_PATH);
    CreateDirectoryW(g_drVlDump, NULL);
    g_drText = TRUE;
  }
  ShareBuf b;
  memset(&b, 0, sizeof(b));
  for (int i = 0; i < l.n; i++) {
    DrPage *pg = &l.p[i];
    _snwprintf(g_drVlBase, MAX_PATH, pg->frame ? L"%s.p%u" : L"%s", tm_base(pg->path), pg->frame + 1);
    g_drVlBase[MAX_PATH - 1] = 0;
    dr_recognize(f, pg);
    if (g_drVlDump[0]) { /* прочитанное Tesseract: наименование, материал, дальше текст листа */
      size_t n = 2 * DR_CELL + (pg->text ? wcslen(pg->text) : 0) + 4;
      wchar_t *t = (wchar_t *)malloc(n * sizeof(wchar_t)), p[MAX_PATH];
      if (t) {
        _snwprintf(t, n, L"%s\n%s\n%s", pg->f[DF_NAME], pg->f[DF_MAT], pg->text ? pg->text : L"");
        t[n - 1] = 0;
        _snwprintf(p, MAX_PATH, L"%s\\%s.tess.txt", g_drVlDump, g_drVlBase);
        p[MAX_PATH - 1] = 0;
        share_write_ex(p, t, TRUE);
        free(t);
      }
    }
    sb_add(&b, L"== %s стр.%u · %s · форма %d · %s · %.1f с · %.2f° %.2f°%s%s\n", tm_base(pg->path), pg->frame + 1,
           kDrType[pg->type], pg->form, pg->fmt, pg->secs, g_drAng, g_drAngV, pg->note[0] ? L" · " : L"", pg->note);
    for (int k = 0; k < DF_N; k++)
      if (pg->f[k][0] || pg->doubt[k]) sb_add(&b, L"  %s%s: %s\n", pg->doubt[k] ? L"? " : L"", kDrField[k], pg->f[k]);
    if (pg->text) sb_add(&b, L"  -- лист --\n%s\n  -- конец --\n", pg->text);
    for (int r = 0; r < pg->nrows; r++) {
      sb_add(&b, L"  |");
      for (int c = 0; c < DS_N; c++) sb_add(&b, L" %s%s |", pg->rows[r].doubt[c] ? L"?" : L"", pg->rows[r].c[c]);
      sb_add(&b, L"\n");
    }
  }
  if (b.w) share_write_ex(argv[2], b.w, TRUE);
  free(b.w);
  for (int i = 0; i < l.n; i++) dr_page_free(&l.p[i]);
  free(l.p);
  if (f) IWICImagingFactory_Release(f);
  ExitProcess(0);
}
#endif
