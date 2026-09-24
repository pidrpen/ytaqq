/* ---- Запись TIFF и PDF для окна «Объединение TIFF / PDF» ---------------------

   Без Windows: чистый C, проверяется на Linux (декодер libtiff в Pillow
   читает то, что здесь написано). Читает картинки WIC (tiffmerge.c), а
   пишет — этот файл, чтобы результат был одинаковым на любом ПК:

   · ч/б страница (чертёж, скан) — CCITT Group 4, как у факсов и сканеров:
     без потерь и в десятки раз меньше; в PDF тот же поток идёт как есть;
   · серая и цветная — LZW с предсказанием по строке (без потерь), в PDF —
     JPEG (его делает WIC, здесь он только вкладывается);
   · у каждой страницы остаётся своё разрешение (dpi), и лист PDF — того же
     размера, что бумага у исходника.

   Страница подаётся строками: целиком в памяти лежит только сжатое. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- растущий буфер -------------------------------------------------------- */

typedef struct {
  unsigned char *p;
  size_t n, cap;
  int err; /* не хватило памяти */
} TcBuf;

static void tc_put(TcBuf *b, const void *d, size_t n) {
  if (b->err) return;
  if (b->n + n > b->cap) {
    size_t c = b->cap ? b->cap : 65536;
    while (c < b->n + n) c *= 2;
    unsigned char *q = (unsigned char *)realloc(b->p, c);
    if (!q) {
      b->err = 1;
      return;
    }
    b->p = q;
    b->cap = c;
  }
  memcpy(b->p + b->n, d, n);
  b->n += n;
}

static void tc_free(TcBuf *b) {
  free(b->p);
  memset(b, 0, sizeof(*b));
}

/* биты — старший вперёд (FillOrder 1: так ждут и TIFF, и PDF) */
typedef struct {
  TcBuf *b;
  uint32_t acc;
  int nb;
} TcBits;

static void tc_bits(TcBits *w, uint32_t code, int len) {
  while (len > 0) {
    int take = len > 16 ? 16 : len;
    len -= take;
    w->acc = (w->acc << take) | ((code >> len) & ((1u << take) - 1));
    w->nb += take;
    while (w->nb >= 8) {
      unsigned char c = (unsigned char)(w->acc >> (w->nb - 8));
      tc_put(w->b, &c, 1);
      w->nb -= 8;
    }
    w->acc &= (1u << w->nb) - 1;
  }
}

static void tc_bits_flush(TcBits *w) {
  if (w->nb) tc_bits(w, 0, 8 - w->nb);
}

/* ---- CCITT Group 4 (T.6) ---------------------------------------------------- */

/* коды длин серий (T.4): {код, число бит} */
typedef struct {
  uint16_t code;
  uint8_t len;
} TcCode;

static const TcCode kWhiteTerm[64] = {
    {0x35, 8}, {0x07, 6}, {0x07, 4}, {0x08, 4}, {0x0B, 4}, {0x0C, 4}, {0x0E, 4}, {0x0F, 4}, {0x13, 5}, {0x14, 5},
    {0x07, 5}, {0x08, 5}, {0x08, 6}, {0x03, 6}, {0x34, 6}, {0x35, 6}, {0x2A, 6}, {0x2B, 6}, {0x27, 7}, {0x0C, 7},
    {0x08, 7}, {0x17, 7}, {0x03, 7}, {0x04, 7}, {0x28, 7}, {0x2B, 7}, {0x13, 7}, {0x24, 7}, {0x18, 7}, {0x02, 8},
    {0x03, 8}, {0x1A, 8}, {0x1B, 8}, {0x12, 8}, {0x13, 8}, {0x14, 8}, {0x15, 8}, {0x16, 8}, {0x17, 8}, {0x28, 8},
    {0x29, 8}, {0x2A, 8}, {0x2B, 8}, {0x2C, 8}, {0x2D, 8}, {0x04, 8}, {0x05, 8}, {0x0A, 8}, {0x0B, 8}, {0x52, 8},
    {0x53, 8}, {0x54, 8}, {0x55, 8}, {0x24, 8}, {0x25, 8}, {0x58, 8}, {0x59, 8}, {0x5A, 8}, {0x5B, 8}, {0x4A, 8},
    {0x4B, 8}, {0x32, 8}, {0x33, 8}, {0x34, 8}};

static const TcCode kBlackTerm[64] = {
    {0x37, 10}, {0x02, 3},  {0x03, 2},  {0x02, 2},  {0x03, 3},  {0x03, 4},  {0x02, 4},  {0x03, 5},  {0x05, 6},
    {0x04, 6},  {0x04, 7},  {0x05, 7},  {0x07, 7},  {0x04, 8},  {0x07, 8},  {0x18, 9},  {0x17, 10}, {0x18, 10},
    {0x08, 10}, {0x67, 11}, {0x68, 11}, {0x6C, 11}, {0x37, 11}, {0x28, 11}, {0x17, 11}, {0x18, 11}, {0xCA, 12},
    {0xCB, 12}, {0xCC, 12}, {0xCD, 12}, {0x68, 12}, {0x69, 12}, {0x6A, 12}, {0x6B, 12}, {0xD2, 12}, {0xD3, 12},
    {0xD4, 12}, {0xD5, 12}, {0xD6, 12}, {0xD7, 12}, {0x6C, 12}, {0x6D, 12}, {0xDA, 12}, {0xDB, 12}, {0x54, 12},
    {0x55, 12}, {0x56, 12}, {0x57, 12}, {0x64, 12}, {0x65, 12}, {0x52, 12}, {0x53, 12}, {0x24, 12}, {0x37, 12},
    {0x38, 12}, {0x27, 12}, {0x28, 12}, {0x58, 12}, {0x59, 12}, {0x2B, 12}, {0x2C, 12}, {0x5A, 12}, {0x66, 12},
    {0x67, 12}};

/* 64, 128 … 1728 */
static const TcCode kWhiteMake[27] = {
    {0x1B, 5}, {0x12, 5}, {0x17, 6}, {0x37, 7}, {0x36, 8}, {0x37, 8}, {0x64, 8}, {0x65, 8}, {0x68, 8},
    {0x67, 8}, {0xCC, 9}, {0xCD, 9}, {0xD2, 9}, {0xD3, 9}, {0xD4, 9}, {0xD5, 9}, {0xD6, 9}, {0xD7, 9},
    {0xD8, 9}, {0xD9, 9}, {0xDA, 9}, {0xDB, 9}, {0x98, 9}, {0x99, 9}, {0x9A, 9}, {0x18, 6}, {0x9B, 9}};

static const TcCode kBlackMake[27] = {
    {0x0F, 10}, {0xC8, 12}, {0xC9, 12}, {0x5B, 12}, {0x33, 12}, {0x34, 12}, {0x35, 12}, {0x6C, 13}, {0x6D, 13},
    {0x4A, 13}, {0x4B, 13}, {0x4C, 13}, {0x4D, 13}, {0x72, 13}, {0x73, 13}, {0x74, 13}, {0x75, 13}, {0x76, 13},
    {0x77, 13}, {0x52, 13}, {0x53, 13}, {0x54, 13}, {0x55, 13}, {0x5A, 13}, {0x5B, 13}, {0x64, 13}, {0x65, 13}};

/* 1792, 1856 … 2560 — общие для белых и чёрных */
static const TcCode kExtMake[13] = {{0x08, 11}, {0x0C, 11}, {0x0D, 11}, {0x12, 12}, {0x13, 12}, {0x14, 12}, {0x15, 12},
                                    {0x16, 12}, {0x17, 12}, {0x1C, 12}, {0x1D, 12}, {0x1E, 12}, {0x1F, 12}};

static void tc_run(TcBits *w, int run, int black) {
  while (run >= 2560) {
    tc_bits(w, kExtMake[12].code, kExtMake[12].len);
    run -= 2560;
  }
  if (run >= 64) {
    int k = run / 64; /* 1..40 */
    const TcCode *c = k <= 27 ? &(black ? kBlackMake : kWhiteMake)[k - 1] : &kExtMake[k - 28];
    tc_bits(w, c->code, c->len);
    run -= k * 64;
  }
  const TcCode *t = &(black ? kBlackTerm : kWhiteTerm)[run];
  tc_bits(w, t->code, t->len);
}

typedef struct {
  TcBits bw;
  int w;
  int *ref, *cur; /* где цвет меняется: сначала на чёрный, потом на белый…; в конце — w, w */
} TcG4;

static int tc_g4_init(TcG4 *g, TcBuf *out, int w) {
  memset(g, 0, sizeof(*g));
  g->bw.b = out;
  g->w = w;
  g->ref = (int *)malloc(sizeof(int) * (size_t)(w + 4));
  g->cur = (int *)malloc(sizeof(int) * (size_t)(w + 4));
  if (!g->ref || !g->cur) {
    free(g->ref);
    free(g->cur);
    g->ref = g->cur = NULL;
    return 0;
  }
  g->ref[0] = g->ref[1] = g->ref[2] = w; /* над первой строкой — белая */
  return 1;
}

/* строка: 1 бит на точку, старший бит — левая точка, 1 = чёрная */
static void tc_g4_row(TcG4 *g, const unsigned char *row) {
  int w = g->w, n = 0, color = 0;
  for (int x = 0; x < w; x++) {
    int px = (row[x >> 3] >> (7 - (x & 7))) & 1;
    if (px != color) {
      g->cur[n++] = x;
      color = px;
    }
  }
  g->cur[n] = g->cur[n + 1] = g->cur[n + 2] = w;
  int a0 = -1, ca = 0; /* ca — цвет a0: 0 белый, 1 чёрный */
  int ia = 0, kr = 0;  /* ia — индекс a1 в cur; kr — первый в ref правее a0 */
  while (a0 < w) {
    while (g->ref[kr] <= a0 && g->ref[kr] < w) kr++;
    int kb = kr;
    if ((kb & 1) != ca) kb++; /* b1 — перемена на цвет, обратный a0 */
    int b1 = g->ref[kb] < w ? g->ref[kb] : w;
    int b2 = b1 < w ? g->ref[kb + 1] : w;
    while (g->cur[ia] <= a0 && g->cur[ia] < w) ia++;
    int a1 = g->cur[ia];
    if (b2 < a1) { /* проход */
      tc_bits(&g->bw, 1, 4);
      a0 = b2;
    } else if (a1 - b1 <= 3 && b1 - a1 <= 3) { /* по вертикали */
      static const TcCode kV[7] = {{0x02, 7}, {0x02, 6}, {0x02, 3}, {0x01, 1}, {0x03, 3}, {0x03, 6}, {0x03, 7}};
      const TcCode *c = &kV[a1 - b1 + 3];
      tc_bits(&g->bw, c->code, c->len);
      a0 = a1;
      ca ^= 1;
    } else { /* по горизонтали: две серии */
      int a2 = a1 < w ? g->cur[ia + 1] : w;
      tc_bits(&g->bw, 1, 3);
      tc_run(&g->bw, a1 - (a0 < 0 ? 0 : a0), ca);
      tc_run(&g->bw, a2 - a1, ca ^ 1);
      a0 = a2;
    }
  }
  int *t = g->ref;
  g->ref = g->cur;
  g->cur = t;
}

static void tc_g4_end(TcG4 *g) {
  tc_bits(&g->bw, 1, 12); /* EOFB */
  tc_bits(&g->bw, 1, 12);
  tc_bits_flush(&g->bw);
  free(g->ref);
  free(g->cur);
  g->ref = g->cur = NULL;
}

/* ---- LZW (как в TIFF: 9–12 бит, «ранняя» смена ширины) ---------------------- */

#define TC_LZW_HASH 9001 /* простое, больше 4096 */

typedef struct {
  TcBits bw;
  int bits, next, prefix; /* prefix: текущая цепочка, -1 — пусто */
  int32_t key[TC_LZW_HASH];
  int16_t val[TC_LZW_HASH];
} TcLzw;

static void tc_lzw_reset(TcLzw *z) {
  for (int i = 0; i < TC_LZW_HASH; i++) z->key[i] = -1;
  z->bits = 9;
  z->next = 258;
}

static void tc_lzw_init(TcLzw *z, TcBuf *out) {
  memset(&z->bw, 0, sizeof(z->bw));
  z->bw.b = out;
  tc_lzw_reset(z);
  tc_bits(&z->bw, 256, 9); /* Clear */
  z->prefix = -1;
}

/* выдали код — в таблице стало на одну цепочку больше */
static void tc_lzw_grow(TcLzw *z) {
  z->next++;
  if (z->next == 4094) { /* таблица полна — начинаем заново */
    tc_bits(&z->bw, 256, z->bits);
    tc_lzw_reset(z);
  } else if (z->next > (1 << z->bits) - 1 && z->bits < 12) {
    z->bits++;
  }
}

static void tc_lzw_data(TcLzw *z, const unsigned char *d, size_t n) {
  for (size_t i = 0; i < n; i++) {
    int c = d[i];
    if (z->prefix < 0) {
      z->prefix = c;
      continue;
    }
    int32_t k = (z->prefix << 8) | c;
    unsigned h = (unsigned)k % TC_LZW_HASH;
    while (z->key[h] >= 0 && z->key[h] != k) h = h + 1 == TC_LZW_HASH ? 0 : h + 1;
    if (z->key[h] == k) {
      z->prefix = z->val[h];
      continue;
    }
    tc_bits(&z->bw, (uint32_t)z->prefix, z->bits);
    z->key[h] = k;
    z->val[h] = (int16_t)z->next;
    tc_lzw_grow(z);
    z->prefix = c;
  }
}

static void tc_lzw_end(TcLzw *z) {
  if (z->prefix >= 0) {
    tc_bits(&z->bw, (uint32_t)z->prefix, z->bits);
    /* читатель на этом коде тоже добавит цепочку — и может расшириться до EOI */
    z->next++;
    if (z->next == 4094) {
      tc_bits(&z->bw, 256, z->bits);
      z->bits = 9;
    } else if (z->next > (1 << z->bits) - 1 && z->bits < 12) {
      z->bits++;
    }
  }
  tc_bits(&z->bw, 257, z->bits); /* EOI */
  tc_bits_flush(&z->bw);
}

/* ---- страница в TIFF ---------------------------------------------------------- */

enum { TC_BW = 0, TC_GRAY = 1, TC_RGB = 2 };

/* Сжатая страница. Строки подаются по одной (tc_page_row) в «сыром» виде:
   ч/б — 1 бит на точку (1 = чёрная), серая — байт, цветная — R, G, B. */
typedef struct {
  int kind, w, h;
  double dpix, dpiy;
  int rowsPerStrip, row;
  size_t rowBytes;
  TcBuf data;                /* все полосы подряд */
  uint32_t *stripOff, *stripLen; /* смещения полос внутри data */
  int nstrips;
  unsigned char *tmp; /* строка для предсказания */
  TcG4 g4;
  TcLzw *lzw;
  int err;
} TcPage;

static int tc_page_begin(TcPage *p, int kind, int w, int h, double dpix, double dpiy) {
  memset(p, 0, sizeof(*p));
  p->kind = kind;
  p->w = w;
  p->h = h;
  p->dpix = dpix;
  p->dpiy = dpiy;
  p->rowBytes = kind == TC_BW ? (size_t)(w + 7) / 8 : (size_t)w * (kind == TC_RGB ? 3 : 1);
  if (kind == TC_BW) {
    p->rowsPerStrip = h; /* G4 — одной полосой, как у сканеров */
  } else {
    /* LZW — полосами около 64 КБ: так советует стандарт TIFF */
    p->rowsPerStrip = (int)(65536 / p->rowBytes);
    if (p->rowsPerStrip < 1) p->rowsPerStrip = 1;
    if (p->rowsPerStrip > h) p->rowsPerStrip = h;
  }
  p->nstrips = (h + p->rowsPerStrip - 1) / p->rowsPerStrip;
  p->stripOff = (uint32_t *)calloc((size_t)p->nstrips, sizeof(uint32_t));
  p->stripLen = (uint32_t *)calloc((size_t)p->nstrips, sizeof(uint32_t));
  p->tmp = (unsigned char *)malloc(p->rowBytes);
  if (kind != TC_BW) p->lzw = (TcLzw *)malloc(sizeof(TcLzw));
  if (!p->stripOff || !p->stripLen || !p->tmp || (kind != TC_BW && !p->lzw) ||
      (kind == TC_BW && !tc_g4_init(&p->g4, &p->data, w))) {
    p->err = 1;
    return 0;
  }
  return 1;
}

static void tc_page_row(TcPage *p, const unsigned char *row) {
  if (p->err || p->row >= p->h) return;
  if (p->kind == TC_BW) {
    tc_g4_row(&p->g4, row);
  } else {
    int s = p->row / p->rowsPerStrip;
    if (p->row % p->rowsPerStrip == 0) {
      p->stripOff[s] = (uint32_t)p->data.n;
      tc_lzw_init(p->lzw, &p->data);
    }
    /* предсказание по строке (Predictor 2): пишем разницу с соседом слева */
    int spp = p->kind == TC_RGB ? 3 : 1;
    size_t n = p->rowBytes;
    for (size_t i = 0; i < n; i++) p->tmp[i] = (unsigned char)(row[i] - (i >= (size_t)spp ? row[i - spp] : 0));
    tc_lzw_data(p->lzw, p->tmp, n);
    if (p->row % p->rowsPerStrip == p->rowsPerStrip - 1 || p->row == p->h - 1) {
      tc_lzw_end(p->lzw);
      p->stripLen[s] = (uint32_t)(p->data.n - p->stripOff[s]);
    }
  }
  p->row++;
  if (p->data.err) p->err = 1;
}

static int tc_page_end(TcPage *p) {
  if (!p->err && p->kind == TC_BW) {
    tc_g4_end(&p->g4);
    p->stripOff[0] = 0;
    p->stripLen[0] = (uint32_t)p->data.n;
  }
  if (p->data.err || p->row != p->h) p->err = 1;
  free(p->tmp);
  p->tmp = NULL;
  free(p->lzw);
  p->lzw = NULL;
  if (p->g4.ref) {
    free(p->g4.ref);
    free(p->g4.cur);
    p->g4.ref = p->g4.cur = NULL;
  }
  return !p->err;
}

static void tc_page_free(TcPage *p) {
  tc_free(&p->data);
  free(p->stripOff);
  free(p->stripLen);
  free(p->tmp);
  free(p->lzw);
  if (p->g4.ref) {
    free(p->g4.ref);
    free(p->g4.cur);
  }
  memset(p, 0, sizeof(*p));
}

/* ---- куда пишем: файл (Windows — HANDLE, проверка — FILE*) ------------------- */

typedef struct {
  int (*write)(void *ctx, const void *d, size_t n);
  int (*seek)(void *ctx, uint64_t pos);
  void *ctx;
  uint64_t pos;
  int err;
} TcOut;

static void tc_out(TcOut *o, const void *d, size_t n) {
  if (o->err || !n) return;
  if (!o->write(o->ctx, d, n)) o->err = 1;
  o->pos += n;
}

static void tc_outs(TcOut *o, const char *s) { tc_out(o, s, strlen(s)); }

static void tc_le16(unsigned char *b, unsigned v) {
  b[0] = (unsigned char)v;
  b[1] = (unsigned char)(v >> 8);
}

static void tc_le32(unsigned char *b, uint32_t v) {
  b[0] = (unsigned char)v;
  b[1] = (unsigned char)(v >> 8);
  b[2] = (unsigned char)(v >> 16);
  b[3] = (unsigned char)(v >> 24);
}

/* многостраничный TIFF: заголовок, потом страницы по одной */
typedef struct {
  TcOut *o;
  uint64_t nextPtr; /* где записать смещение следующего IFD */
  int page, total;
} TcTiff;

static void tc_tiff_begin(TcTiff *t, TcOut *o, int total) {
  t->o = o;
  t->page = 0;
  t->total = total;
  unsigned char h[8] = {'I', 'I', 42, 0, 0, 0, 0, 0};
  t->nextPtr = o->pos + 4;
  tc_out(o, h, 8);
}

/* dpi → дробь для XResolution: до сотых */
static void tc_rational(unsigned char *b, double dpi) {
  if (!(dpi > 0) || dpi > 100000) dpi = 96;
  tc_le32(b, (uint32_t)(dpi * 100 + 0.5));
  tc_le32(b + 4, 100);
}

static int tc_tiff_page(TcTiff *t, const TcPage *p) {
  TcOut *o = t->o;
  if (o->err) return 0;
  if (o->pos & 1) tc_out(o, "", 1); /* всё по чётным адресам */
  uint64_t dataAt = o->pos;
  tc_out(o, p->data.p, p->data.n);
  if (o->pos & 1) tc_out(o, "", 1);
  /* доп. данные тегов: смещения и длины полос, BitsPerSample, разрешение */
  uint64_t extraAt = o->pos;
  TcBuf ex = {0};
  uint32_t offsOff = 0, lensOff = 0, bpsOff = 0, xresOff, yresOff;
  unsigned char b[8];
  if (p->nstrips > 1) {
    offsOff = (uint32_t)(extraAt + ex.n);
    for (int i = 0; i < p->nstrips; i++) {
      tc_le32(b, (uint32_t)(dataAt + p->stripOff[i]));
      tc_put(&ex, b, 4);
    }
    lensOff = (uint32_t)(extraAt + ex.n);
    for (int i = 0; i < p->nstrips; i++) {
      tc_le32(b, p->stripLen[i]);
      tc_put(&ex, b, 4);
    }
  }
  if (p->kind == TC_RGB) {
    bpsOff = (uint32_t)(extraAt + ex.n);
    tc_le16(b, 8);
    tc_le16(b + 2, 8);
    tc_le16(b + 4, 8);
    tc_le16(b + 6, 0);
    tc_put(&ex, b, 8);
  }
  xresOff = (uint32_t)(extraAt + ex.n);
  tc_rational(b, p->dpix);
  tc_put(&ex, b, 8);
  yresOff = (uint32_t)(extraAt + ex.n);
  tc_rational(b, p->dpiy);
  tc_put(&ex, b, 8);
  if (ex.err) {
    tc_free(&ex);
    return 0;
  }
  tc_out(o, ex.p, ex.n);
  tc_free(&ex);
  uint64_t ifdAt = o->pos;
  if (ifdAt > 0xFFFFFF00u) { /* обычный TIFF не больше 4 ГБ */
    o->err = 1;
    return 0;
  }
  /* теги — строго по возрастанию номера */
  unsigned char e[20][12];
  int n = 0;
#define TAG(id, type, count, val)                                                                                      \
  do {                                                                                                                 \
    tc_le16(e[n], id);                                                                                                 \
    tc_le16(e[n] + 2, type);                                                                                           \
    tc_le32(e[n] + 4, count);                                                                                          \
    if ((type) == 3 && (count) == 1) {                                                                                 \
      tc_le16(e[n] + 8, (unsigned)(val));                                                                              \
      tc_le16(e[n] + 10, 0);                                                                                           \
    } else {                                                                                                           \
      tc_le32(e[n] + 8, (uint32_t)(val));                                                                              \
    }                                                                                                                  \
    n++;                                                                                                               \
  } while (0)
  TAG(254, 4, 1, t->total > 1 ? 2 : 0); /* NewSubfileType: страница документа */
  TAG(256, 4, 1, p->w);
  TAG(257, 4, 1, p->h);
  if (p->kind == TC_RGB) TAG(258, 3, 3, bpsOff);
  else TAG(258, 3, 1, p->kind == TC_BW ? 1 : 8);
  TAG(259, 3, 1, p->kind == TC_BW ? 4 : 5);                            /* G4 / LZW */
  TAG(262, 3, 1, p->kind == TC_BW ? 0 : p->kind == TC_GRAY ? 1 : 2); /* белый — 0 / чёрный — 0 / RGB */
  TAG(273, 4, p->nstrips, p->nstrips > 1 ? offsOff : (uint32_t)dataAt);
  TAG(277, 3, 1, p->kind == TC_RGB ? 3 : 1);
  TAG(278, 4, 1, p->rowsPerStrip);
  TAG(279, 4, p->nstrips, p->nstrips > 1 ? lensOff : p->stripLen[0]);
  TAG(282, 5, 1, xresOff);
  TAG(283, 5, 1, yresOff);
  TAG(284, 3, 1, 1);
  if (p->kind == TC_BW) TAG(293, 4, 1, 0); /* T6Options */
  TAG(296, 3, 1, 2);                        /* дюймы */
  if (t->total > 1) {                       /* PageNumber: номер, всего */
    tc_le16(e[n], 297);
    tc_le16(e[n] + 2, 3);
    tc_le32(e[n] + 4, 2);
    tc_le16(e[n] + 8, (unsigned)t->page);
    tc_le16(e[n] + 10, (unsigned)t->total);
    n++;
  }
  if (p->kind != TC_BW) TAG(317, 3, 1, 2); /* Predictor: по строке */
#undef TAG
  unsigned char cnt[2], nx[4] = {0, 0, 0, 0};
  tc_le16(cnt, (unsigned)n);
  tc_out(o, cnt, 2);
  tc_out(o, e, (size_t)n * 12);
  uint64_t myNext = o->pos;
  tc_out(o, nx, 4);
  /* прошлый IFD (или заголовок) — ссылкой на этот */
  uint64_t end = o->pos;
  unsigned char at[4];
  tc_le32(at, (uint32_t)ifdAt);
  if (!o->err && o->seek(o->ctx, t->nextPtr) && o->write(o->ctx, at, 4) && o->seek(o->ctx, end)) {
    t->nextPtr = myNext;
  } else {
    o->err = 1;
  }
  t->page++;
  return !o->err;
}

/* ---- PDF ----------------------------------------------------------------------- */

typedef struct {
  TcOut *o;
  uint64_t *xref; /* смещения объектов 1…nobj */
  int nobj, cap;
  int *pages;     /* номера объектов страниц */
  int npages, pcap;
} TcPdf;

static int tc_pdf_obj(TcPdf *d) { /* начать очередной объект: запомнить, где он */
  if (d->nobj + 1 >= d->cap) {
    int c = d->cap ? d->cap * 2 : 64;
    uint64_t *x = (uint64_t *)realloc(d->xref, sizeof(uint64_t) * (size_t)c);
    if (!x) {
      d->o->err = 1;
      return 0;
    }
    d->xref = x;
    d->cap = c;
  }
  d->nobj++;
  d->xref[d->nobj] = d->o->pos;
  char h[32];
  snprintf(h, sizeof(h), "%d 0 obj\n", d->nobj);
  tc_outs(d->o, h);
  return d->nobj;
}

static void tc_pdf_begin(TcPdf *d, TcOut *o) {
  memset(d, 0, sizeof(*d));
  d->o = o;
  tc_outs(o, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
  /* 1 — каталог, 2 — список страниц (он пишется в конце) */
  tc_pdf_obj(d);
  tc_outs(o, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  d->nobj = 2;
  if (d->cap > 2) d->xref[2] = 0;
}

/* число с точкой и без лишних нулей — без printf("%f"): тому важен язык системы */
static void tc_num(char *s, size_t cap, double v) {
  long long c = (long long)(v * 1000 + (v < 0 ? -0.5 : 0.5));
  const char *sign = c < 0 ? "-" : "";
  if (c < 0) c = -c;
  if (c % 1000 == 0) snprintf(s, cap, "%s%lld", sign, c / 1000);
  else snprintf(s, cap, "%s%lld.%03lld", sign, c / 1000, c % 1000);
  char *z = s + strlen(s) - 1;
  if (strchr(s, '.')) {
    while (*z == '0') *z-- = 0;
    if (*z == '.') *z = 0;
  }
}

/* Страница с одной картинкой во весь лист. Лист — в пунктах (1/72 дюйма):
   точки ÷ dpi × 72. kind: TC_BW — data это поток G4 (как из tc_page), иначе
   data — JPEG (TC_GRAY — одноканальный). */
static int tc_pdf_page(TcPdf *d, int kind, int w, int h, double dpix, double dpiy, const unsigned char *data,
                       size_t n) {
  TcOut *o = d->o;
  if (!(dpix > 0) || dpix > 100000) dpix = 96;
  if (!(dpiy > 0) || dpiy > 100000) dpiy = 96;
  double pw = w * 72.0 / dpix, ph = h * 72.0 / dpiy;
  /* Acrobat не открывает листы больше 200 дюймов — такие уменьшаем целиком */
  double big = pw > ph ? pw : ph;
  if (big > 14400) {
    pw = pw * 14400 / big;
    ph = ph * 14400 / big;
  }
  char a[40], b[40], hd[400];
  int img = tc_pdf_obj(d);
  if (kind == TC_BW)
    snprintf(hd, sizeof(hd),
             "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray /BitsPerComponent 1 "
             "/Filter /CCITTFaxDecode /DecodeParms << /K -1 /Columns %d /Rows %d >> /Length %lu >>\nstream\n",
             w, h, w, h, (unsigned long)n);
  else
    snprintf(hd, sizeof(hd),
             "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /%s /BitsPerComponent 8 "
             "/Filter /DCTDecode /Length %lu >>\nstream\n",
             w, h, kind == TC_GRAY ? "DeviceGray" : "DeviceRGB", (unsigned long)n);
  tc_outs(o, hd);
  tc_out(o, data, n);
  tc_outs(o, "\nendstream\nendobj\n");
  tc_num(a, sizeof(a), pw);
  tc_num(b, sizeof(b), ph);
  char cs[200];
  snprintf(cs, sizeof(cs), "q %s 0 0 %s 0 0 cm /Im0 Do Q\n", a, b);
  int cont = tc_pdf_obj(d);
  snprintf(hd, sizeof(hd), "<< /Length %lu >>\nstream\n", (unsigned long)strlen(cs));
  tc_outs(o, hd);
  tc_outs(o, cs);
  tc_outs(o, "endstream\nendobj\n");
  int pg = tc_pdf_obj(d);
  snprintf(hd, sizeof(hd),
           "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %s %s] /Resources << /XObject << /Im0 %d 0 R >> >> "
           "/Contents %d 0 R >>\nendobj\n",
           a, b, img, cont);
  tc_outs(o, hd);
  if (d->npages >= d->pcap) {
    int c = d->pcap ? d->pcap * 2 : 32;
    int *q = (int *)realloc(d->pages, sizeof(int) * (size_t)c);
    if (!q) {
      o->err = 1;
      return 0;
    }
    d->pages = q;
    d->pcap = c;
  }
  d->pages[d->npages++] = pg;
  return !o->err;
}

static int tc_pdf_end(TcPdf *d) {
  TcOut *o = d->o;
  if (d->cap > 2) d->xref[2] = o->pos;
  tc_outs(o, "2 0 obj\n<< /Type /Pages /Kids [");
  for (int i = 0; i < d->npages; i++) {
    char s[24];
    snprintf(s, sizeof(s), "%s%d 0 R", i ? " " : "", d->pages[i]);
    tc_outs(o, s);
  }
  char s[160];
  snprintf(s, sizeof(s), "] /Count %d >>\nendobj\n", d->npages);
  tc_outs(o, s);
  uint64_t xr = o->pos;
  snprintf(s, sizeof(s), "xref\n0 %d\n0000000000 65535 f \n", d->nobj + 1);
  tc_outs(o, s);
  for (int i = 1; i <= d->nobj; i++) {
    snprintf(s, sizeof(s), "%010llu 00000 n \n", (unsigned long long)d->xref[i]);
    tc_outs(o, s);
  }
  snprintf(s, sizeof(s), "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%llu\n%%%%EOF\n", d->nobj + 1,
           (unsigned long long)xr);
  tc_outs(o, s);
  free(d->xref);
  free(d->pages);
  d->xref = NULL;
  d->pages = NULL;
  return !o->err;
}
