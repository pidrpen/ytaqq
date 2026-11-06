/* xlsx_read.c — книга Excel (.xlsx, .xlsm) без Excel и без библиотек (с 2026.11.06, John: «форму Excel
   закину в ЭСИ и ТП — и всё проставилось само»). Пара к xlsx_write.c (там — запись, префикс xl_).
   Файл .xlsx — zip: центральный каталог, файлы «store» или «deflate» (inflate — по RFC 1951, как puff.c
   Марка Адлера); листы — по xl/workbook.xml и его связям, строки — xl/sharedStrings.xml.
   Берутся только значения: у формулы — посчитанный Excel результат, как он сохранён в файле.
   Числа — как их показывает русский Excel в буфере: «0,35», «12,3» (без хвостов «12.300000000000001»).
   Перевод строки и Tab внутри ячейки — пробелом; ячейка — без пробелов по краям.
   Старый .xls и книга с паролем — не zip: xr_read скажет об этом (их открывает Excel, см. pk_xlsx_ui.c).
   Windows не нужен: gcc -O2 -Wall -Werror -o xlsx_read_test xlsx_read_test.c && ./xlsx_read_test */

#define XR_MAXROWS 20000
#define XR_MAXCOLS 64
#define XR_MAXSHEETS 64
#define XR_MAXOUT (256u << 20) /* распакованный файл книги — не больше 256 МБ */

typedef struct {
  wchar_t name[100];
  int hidden;        /* скрытый лист (state="hidden" / "veryHidden") */
  int nrows, ncols;  /* до последней непустой строки и столбца */
  wchar_t **cell;    /* nrows * ncols, NULL — пусто */
} XrSheet;

typedef struct {
  int n;
  XrSheet sh[XR_MAXSHEETS];
} XrBook;

static void xr_say(wchar_t *why, int cap, const wchar_t *s) {
  if (!why || cap <= 0) return;
  int k = 0;
  for (; s[k] && k < cap - 1; k++) why[k] = s[k];
  why[k] = 0;
}

static unsigned xr_u16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned long xr_u32(const unsigned char *p) {
  return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

/* ---- inflate ------------------------------------------------------------------------- */

typedef struct {
  const unsigned char *in;
  size_t inlen, inpos;
  unsigned long long bitbuf;
  int bitcnt;
  unsigned char *out;
  size_t outlen, outcap;
  int err;
} XrInf;

typedef struct {
  short count[16];
  short symbol[288];
} XrHuff;

static int xr_bits(XrInf *s, int need) {
  while (s->bitcnt < need) {
    if (s->inpos >= s->inlen) {
      s->err = 1;
      return 0;
    }
    s->bitbuf |= (unsigned long long)s->in[s->inpos++] << s->bitcnt;
    s->bitcnt += 8;
  }
  int v = (int)(s->bitbuf & ((1ull << need) - 1));
  s->bitbuf >>= need;
  s->bitcnt -= need;
  return v;
}

static int xr_room(XrInf *s, size_t n) {
  if (s->outlen + n <= s->outcap) return 1;
  size_t nc = s->outcap ? s->outcap : 65536;
  while (nc < s->outlen + n) nc *= 2;
  if (nc > XR_MAXOUT) {
    if (s->outlen + n > XR_MAXOUT) return 0;
    nc = XR_MAXOUT;
  }
  unsigned char *p = (unsigned char *)realloc(s->out, nc + 1);
  if (!p) return 0;
  s->out = p;
  s->outcap = nc;
  return 1;
}

static int xr_decode(XrInf *s, const XrHuff *h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; len++) {
    code |= xr_bits(s, 1);
    if (s->err) return -1;
    int count = h->count[len];
    if (code - count < first) return h->symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

/* <0 — переподписанный код (ошибка), >0 — неполный, 0 — полный */
static int xr_construct(XrHuff *h, const short *length, int n) {
  for (int len = 0; len < 16; len++) h->count[len] = 0;
  for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
  if (h->count[0] == n) return 0;
  int left = 1;
  for (int len = 1; len < 16; len++) {
    left <<= 1;
    left -= h->count[len];
    if (left < 0) return left;
  }
  short offs[16];
  offs[1] = 0;
  for (int len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
  for (int sym = 0; sym < n; sym++)
    if (length[sym]) h->symbol[offs[length[sym]]++] = (short)sym;
  return left;
}

static const short kXrLBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short kXrLExt[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short kXrDBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                   193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short kXrDExt[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int xr_codes(XrInf *s, const XrHuff *lc, const XrHuff *dc) {
  for (;;) {
    int sym = xr_decode(s, lc);
    if (sym < 0) return -1;
    if (sym < 256) {
      if (!xr_room(s, 1)) return -1;
      s->out[s->outlen++] = (unsigned char)sym;
    } else if (sym == 256) {
      return 0;
    } else {
      sym -= 257;
      if (sym >= 29) return -1;
      int len = kXrLBase[sym] + xr_bits(s, kXrLExt[sym]);
      int ds = xr_decode(s, dc);
      if (ds < 0 || ds >= 30) return -1;
      size_t dist = (size_t)kXrDBase[ds] + (size_t)xr_bits(s, kXrDExt[ds]);
      if (s->err || dist > s->outlen || !xr_room(s, (size_t)len)) return -1;
      for (; len; len--, s->outlen++) s->out[s->outlen] = s->out[s->outlen - dist];
    }
  }
}

static int xr_fixed(XrInf *s) {
  static XrHuff lc, dc;
  static int built;
  if (!built) {
    short l[288];
    int i = 0;
    for (; i < 144; i++) l[i] = 8;
    for (; i < 256; i++) l[i] = 9;
    for (; i < 280; i++) l[i] = 7;
    for (; i < 288; i++) l[i] = 8;
    xr_construct(&lc, l, 288);
    for (i = 0; i < 30; i++) l[i] = 5;
    xr_construct(&dc, l, 30);
    built = 1;
  }
  return xr_codes(s, &lc, &dc);
}

static int xr_dynamic(XrInf *s) {
  static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  short lengths[320];
  XrHuff lc, dc;
  int nlen = xr_bits(s, 5) + 257, ndist = xr_bits(s, 5) + 1, ncode = xr_bits(s, 4) + 4;
  if (s->err || nlen > 286 || ndist > 30) return -1;
  int i = 0;
  for (; i < ncode; i++) lengths[order[i]] = (short)xr_bits(s, 3);
  for (; i < 19; i++) lengths[order[i]] = 0;
  if (s->err || xr_construct(&lc, lengths, 19) != 0) return -1;
  for (i = 0; i < nlen + ndist;) {
    int sym = xr_decode(s, &lc);
    if (sym < 0) return -1;
    if (sym < 16) {
      lengths[i++] = (short)sym;
      continue;
    }
    short len = 0;
    int rep;
    if (sym == 16) {
      if (!i) return -1;
      len = lengths[i - 1];
      rep = 3 + xr_bits(s, 2);
    } else if (sym == 17) {
      rep = 3 + xr_bits(s, 3);
    } else {
      rep = 11 + xr_bits(s, 7);
    }
    if (s->err || i + rep > nlen + ndist) return -1;
    while (rep--) lengths[i++] = len;
  }
  if (!lengths[256]) return -1;
  if (xr_construct(&lc, lengths, nlen) < 0 || xr_construct(&dc, lengths + nlen, ndist) < 0) return -1;
  return xr_codes(s, &lc, &dc);
}

/* сырой deflate → malloc (с нулём в конце); hint — ожидаемый размер */
static unsigned char *xr_inflate(const unsigned char *in, size_t inlen, size_t hint, size_t *outlen) {
  XrInf s;
  memset(&s, 0, sizeof(s));
  s.in = in;
  s.inlen = inlen;
  if (hint && hint < XR_MAXOUT && !xr_room(&s, hint)) return NULL;
  int last = 0, bad = 0;
  while (!last && !bad) {
    last = xr_bits(&s, 1);
    int type = xr_bits(&s, 2);
    if (s.err) break;
    if (type == 0) { /* «store»: до границы байта, LEN, NLEN, байты */
      s.bitbuf = 0;
      s.bitcnt = 0;
      if (s.inpos + 4 > s.inlen) {
        bad = 1;
        break;
      }
      unsigned len = xr_u16(s.in + s.inpos), nlen = xr_u16(s.in + s.inpos + 2);
      s.inpos += 4;
      if ((len ^ 0xFFFFu) != nlen || s.inpos + len > s.inlen || !xr_room(&s, len)) {
        bad = 1;
        break;
      }
      memcpy(s.out + s.outlen, s.in + s.inpos, len);
      s.outlen += len;
      s.inpos += len;
    } else if (type == 1) {
      bad = xr_fixed(&s) != 0;
    } else if (type == 2) {
      bad = xr_dynamic(&s) != 0;
    } else {
      bad = 1;
    }
  }
  if (bad || s.err || !xr_room(&s, 1)) {
    free(s.out);
    return NULL;
  }
  s.out[s.outlen] = 0;
  *outlen = s.outlen;
  return s.out;
}

/* ---- zip --------------------------------------------------------------------------- */

typedef struct {
  const unsigned char *d;
  size_t n, cd, cdEnd;
  unsigned entries;
} XrZip;

static int xr_zip_open(XrZip *z, const unsigned char *d, size_t n) {
  memset(z, 0, sizeof(*z));
  if (!d || n < 22) return 0;
  size_t i = n - 22, stop = n - 22 > 65535 ? n - 22 - 65535 : 0;
  while (xr_u32(d + i) != 0x06054b50UL) {
    if (i == stop) return 0;
    i--;
  }
  size_t size = xr_u32(d + i + 12), off = xr_u32(d + i + 16);
  if (off > n || size > n - off) return 0;
  z->d = d;
  z->n = n;
  z->cd = off;
  z->cdEnd = off + size;
  z->entries = xr_u16(d + i + 10);
  return 1;
}

static int xr_name_eq(const char *a, size_t al, const char *b) {
  if (*b == '/') b++;
  if (al && *a == '/') a++, al--;
  size_t bl = strlen(b);
  if (al != bl) return 0;
  for (size_t k = 0; k < al; k++) {
    char x = a[k], y = b[k];
    if (x == '\\') x = '/';
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return 0;
  }
  return 1;
}

/* файл name из zip (без учёта регистра) → malloc с нулём в конце; NULL — нет или не распаковался */
static unsigned char *xr_zip_get(const XrZip *z, const char *name, size_t *len) {
  size_t p = z->cd;
  *len = 0;
  for (unsigned e = 0; e < z->entries; e++) {
    if (p + 46 > z->cdEnd || xr_u32(z->d + p) != 0x02014b50UL) return NULL;
    unsigned flags = xr_u16(z->d + p + 8), method = xr_u16(z->d + p + 10);
    size_t csize = xr_u32(z->d + p + 20), usize = xr_u32(z->d + p + 24);
    unsigned nl = xr_u16(z->d + p + 28), xl = xr_u16(z->d + p + 30), cl = xr_u16(z->d + p + 32);
    size_t lho = xr_u32(z->d + p + 42);
    if (p + 46 + nl > z->cdEnd) return NULL;
    if (xr_name_eq((const char *)z->d + p + 46, nl, name)) {
      if ((flags & 1) || lho + 30 > z->n || xr_u32(z->d + lho) != 0x04034b50UL) return NULL;
      size_t data = lho + 30 + xr_u16(z->d + lho + 26) + xr_u16(z->d + lho + 28);
      if (data > z->n || csize > z->n - data) return NULL;
      if (method == 0) {
        unsigned char *o = (unsigned char *)malloc(csize + 1);
        if (!o) return NULL;
        memcpy(o, z->d + data, csize);
        o[csize] = 0;
        *len = csize;
        return o;
      }
      if (method == 8) return xr_inflate(z->d + data, csize, usize, len);
      return NULL;
    }
    p += 46 + (size_t)nl + xl + cl;
  }
  return NULL;
}

/* ---- xml ------------------------------------------------------------------------- */

typedef struct {
  const char *start, *after; /* «<» тега и следующий за «>» знак */
  const char *name;          /* местное имя, без «x:» */
  int nlen;
  const char *attr, *attrEnd;
  int close, empty; /* </x>, <x/> */
} XrTag;

static const char *xr_find(const char *p, const char *e, const char *what) {
  size_t wl = strlen(what);
  for (; p + wl <= e; p++)
    if (!memcmp(p, what, wl)) return p;
  return NULL;
}

/* следующий тег с p; объявления, комментарии и CDATA — мимо. NULL — тегов больше нет */
static const char *xr_tag(const char *p, const char *e, XrTag *t) {
  for (;;) {
    const char *lt = p < e ? (const char *)memchr(p, '<', (size_t)(e - p)) : NULL;
    if (!lt || lt + 1 >= e) return NULL;
    if (lt[1] == '?' || lt[1] == '!') {
      const char *end = lt[1] == '?' ? xr_find(lt, e, "?>")
                        : e - lt > 3 && lt[2] == '-' && lt[3] == '-' ? xr_find(lt, e, "-->")
                        : e - lt > 8 && !memcmp(lt, "<![CDATA[", 9) ? xr_find(lt, e, "]]>")
                                                                     : xr_find(lt, e, ">");
      if (!end) return NULL;
      p = end + 1;
      continue;
    }
    const char *q = lt + 1;
    t->close = *q == '/';
    if (t->close) q++;
    const char *ns = q;
    while (q < e && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n' && *q != '>' && *q != '/') q++;
    t->name = ns;
    for (const char *c = ns; c < q; c++)
      if (*c == ':') t->name = c + 1;
    t->nlen = (int)(q - t->name);
    t->attr = q;
    char quote = 0;
    while (q < e) {
      if (quote) {
        if (*q == quote) quote = 0;
      } else if (*q == '"' || *q == '\'') {
        quote = *q;
      } else if (*q == '>') {
        break;
      }
      q++;
    }
    if (q >= e) return NULL;
    t->empty = q > t->attr && q[-1] == '/';
    t->attrEnd = t->empty ? q - 1 : q;
    t->start = lt;
    t->after = q + 1;
    return lt;
  }
}

static int xr_is(const XrTag *t, const char *name) {
  return (size_t)t->nlen == strlen(name) && !memcmp(t->name, name, (size_t)t->nlen);
}

/* значение атрибута по местному имени («id» найдёт и «r:id»); 0 — нет */
static int xr_attr(const XrTag *t, const char *name, const char **v, int *vl) {
  size_t want = strlen(name);
  const char *p = t->attr, *e = t->attrEnd;
  while (p < e) {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    const char *ns = p;
    while (p < e && *p != '=' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    const char *ne = p, *ln = ns;
    for (const char *c = ns; c < ne; c++)
      if (*c == ':') ln = c + 1;
    while (p < e && *p != '=') p++;
    if (p >= e) return 0;
    p++;
    while (p < e && *p != '"' && *p != '\'') p++;
    if (p >= e) return 0;
    char q = *p++;
    const char *vs = p;
    while (p < e && *p != q) p++;
    if ((size_t)(ne - ln) == want && !memcmp(ln, name, want)) {
      *v = vs;
      *vl = (int)(p - vs);
      return 1;
    }
    if (p < e) p++;
  }
  return 0;
}

static int xr_attr_is(const XrTag *t, const char *name, const char *val) {
  const char *v;
  int vl;
  return xr_attr(t, name, &v, &vl) && (size_t)vl == strlen(val) && !memcmp(v, val, (size_t)vl);
}

/* ---- текст: UTF-8, &amp; &#1080;, _x000D_ → wchar_t ---------------------------------------- */

typedef struct {
  wchar_t *p;
  size_t n, cap;
} XrW;

static void xr_wput(XrW *b, unsigned long cp) {
  if (b->n + 3 > b->cap) {
    size_t nc = b->cap ? b->cap * 2 : 64;
    wchar_t *np = (wchar_t *)realloc(b->p, nc * sizeof(wchar_t));
    if (!np) return;
    b->p = np;
    b->cap = nc;
  }
  if (cp < 0x20 || cp == 0x7F) cp = ' '; /* Tab, перевод строки внутри ячейки — пробелом */
  if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
    cp -= 0x10000;
    b->p[b->n++] = (wchar_t)(0xD800 + (cp >> 10));
    b->p[b->n++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
  } else {
    b->p[b->n++] = (wchar_t)cp;
  }
  b->p[b->n] = 0;
}

static int xr_hex(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void xr_wtext(XrW *b, const char *s, const char *e) {
  while (s < e) {
    unsigned char c = (unsigned char)*s;
    if (c == '&') {
      const char *semi = (const char *)memchr(s, ';', (size_t)(e - s));
      if (semi && semi - s <= 10) {
        size_t l = (size_t)(semi - s - 1);
        const char *n = s + 1;
        unsigned long cp = 0;
        int ok = 1;
        if (l == 3 && !memcmp(n, "amp", 3)) cp = '&';
        else if (l == 2 && !memcmp(n, "lt", 2)) cp = '<';
        else if (l == 2 && !memcmp(n, "gt", 2)) cp = '>';
        else if (l == 4 && !memcmp(n, "quot", 4)) cp = '"';
        else if (l == 4 && !memcmp(n, "apos", 4)) cp = '\'';
        else if (l >= 2 && n[0] == '#') {
          int hex = n[1] == 'x' || n[1] == 'X';
          for (const char *d = n + 1 + hex; d < semi && ok; d++) {
            int v = hex ? xr_hex(*d) : (*d >= '0' && *d <= '9' ? *d - '0' : -1);
            if (v < 0) ok = 0;
            else cp = cp * (hex ? 16 : 10) + (unsigned long)v;
          }
          if (cp > 0x10FFFF) ok = 0;
        } else {
          ok = 0;
        }
        if (ok) {
          xr_wput(b, cp);
          s = semi + 1;
          continue;
        }
      }
      xr_wput(b, '&');
      s++;
      continue;
    }
    if (c == '_' && e - s >= 7 && s[1] == 'x' && s[6] == '_') { /* _x000D_ — знак, который xml не держит */
      int h0 = xr_hex(s[2]), h1 = xr_hex(s[3]), h2 = xr_hex(s[4]), h3 = xr_hex(s[5]);
      if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
        xr_wput(b, (unsigned long)(h0 << 12 | h1 << 8 | h2 << 4 | h3));
        s += 7;
        continue;
      }
    }
    unsigned long cp;
    int extra;
    if (c < 0x80) cp = c, extra = 0;
    else if ((c & 0xE0) == 0xC0) cp = c & 0x1F, extra = 1;
    else if ((c & 0xF0) == 0xE0) cp = c & 0x0F, extra = 2;
    else if ((c & 0xF8) == 0xF0) cp = c & 0x07, extra = 3;
    else cp = '?', extra = 0;
    s++;
    for (; extra && s < e && ((unsigned char)*s & 0xC0) == 0x80; extra--, s++) cp = cp << 6 | ((unsigned char)*s & 0x3F);
    if (extra) cp = '?';
    xr_wput(b, cp);
  }
}

/* текст в malloc без пробелов по краям; пусто — NULL */
static wchar_t *xr_wtake(XrW *b) {
  if (!b->p) return NULL;
  size_t s = 0, e = b->n;
  while (s < e && (b->p[s] == L' ' || b->p[s] == 0xA0)) s++;
  while (e > s && (b->p[e - 1] == L' ' || b->p[e - 1] == 0xA0)) e--;
  wchar_t *r = NULL;
  if (e > s && (r = (wchar_t *)malloc((e - s + 1) * sizeof(wchar_t))) != NULL) {
    memcpy(r, b->p + s, (e - s) * sizeof(wchar_t));
    r[e - s] = 0;
  }
  b->n = 0;
  if (b->p) b->p[0] = 0;
  return r;
}

/* текст строки: все <t> подряд (у «богатого» текста их несколько), подсказки чтения <rPh> — мимо;
   p — после открывающего тега, end — имя закрывающего («si», «is»); возвращает место после него */
static const char *xr_runs(const char *p, const char *e, const char *end, XrW *b) {
  XrTag t;
  int ph = 0;
  while (xr_tag(p, e, &t)) {
    p = t.after;
    if (xr_is(&t, end) && t.close) break;
    if (xr_is(&t, "rPh")) ph = !t.close && !t.empty;
    else if (xr_is(&t, "t") && !t.close && !t.empty && !ph) {
      const char *lt = (const char *)memchr(p, '<', (size_t)(e - p));
      if (!lt) lt = e;
      xr_wtext(b, p, lt);
      p = lt;
    }
  }
  return p;
}

/* ---- число ---------------------------------------------------------------------------- */

/* «12.300000000000001» → «12,3», «5» → «5», «1.5E-2» → «0,015»; странное — как есть */
static void xr_num(const char *s, int sl, XrW *b) {
  int i = 0, neg = 0;
  double v = 0;
  if (i < sl && (s[i] == '-' || s[i] == '+')) neg = s[i] == '-', i++;
  int digits = 0, exp10 = 0;
  for (; i < sl && s[i] >= '0' && s[i] <= '9'; i++, digits++) v = v * 10 + (s[i] - '0');
  if (i < sl && s[i] == '.')
    for (i++; i < sl && s[i] >= '0' && s[i] <= '9'; i++, digits++) v = v * 10 + (s[i] - '0'), exp10--;
  if (digits && i < sl && (s[i] == 'e' || s[i] == 'E')) {
    int en = 0, eneg = 0, ed = 0;
    i++;
    if (i < sl && (s[i] == '-' || s[i] == '+')) eneg = s[i] == '-', i++;
    for (; i < sl && s[i] >= '0' && s[i] <= '9' && en < 400; i++, ed++) en = en * 10 + (s[i] - '0');
    if (!ed) digits = 0;
    exp10 += eneg ? -en : en;
  }
  double a = v;
  for (int k = exp10; k > 0; k--) a *= 10;
  for (int k = exp10; k < 0; k++) a /= 10;
  if (!digits || i != sl || a >= 1e15 || (a != 0 && a < 1e-9)) {
    xr_wtext(b, s, s + sl);
    return;
  }
  int intd = 1;
  for (double t = a; t >= 10; t /= 10) intd++;
  int dp = 10 - intd;
  if (dp < 0) dp = 0;
  if (dp > 9) dp = 9;
  double sc = a;
  for (int k = 0; k < dp; k++) sc *= 10;
  unsigned long long m = (unsigned long long)(sc + 0.5), ip = m, fp = 0, p10 = 1;
  for (int k = 0; k < dp; k++) p10 *= 10;
  ip = m / p10;
  fp = m % p10;
  if (neg && m) xr_wput(b, '-');
  char t[32];
  int n = 0;
  do t[n++] = (char)('0' + ip % 10), ip /= 10; while (ip && n < 30);
  while (n) xr_wput(b, (unsigned long)t[--n]);
  if (fp) {
    int w = dp;
    while (fp % 10 == 0) fp /= 10, w--;
    char f[16];
    for (int k = w - 1; k >= 0; k--) f[k] = (char)('0' + fp % 10), fp /= 10;
    xr_wput(b, ',');
    for (int k = 0; k < w; k++) xr_wput(b, (unsigned long)f[k]);
  }
}

/* ---- лист ------------------------------------------------------------------------------ */

typedef struct {
  int r, c;
  wchar_t *v;
} XrCell;

typedef struct {
  XrCell *p;
  size_t n, cap;
} XrCells;

static void xr_cell_add(XrCells *cs, int r, int c, wchar_t *v) {
  if (!v) return;
  if (r < 0 || r >= XR_MAXROWS || c < 0 || c >= XR_MAXCOLS) {
    free(v);
    return;
  }
  if (cs->n == cs->cap) {
    size_t nc = cs->cap ? cs->cap * 2 : 256;
    XrCell *np = (XrCell *)realloc(cs->p, nc * sizeof(XrCell));
    if (!np) {
      free(v);
      return;
    }
    cs->p = np;
    cs->cap = nc;
  }
  cs->p[cs->n].r = r;
  cs->p[cs->n].c = c;
  cs->p[cs->n++].v = v;
}

/* «B12» → столбец 1, строка 11 */
static int xr_ref(const char *v, int vl, int *row, int *col) {
  int i = 0, c = 0, r = 0, cl = 0;
  for (; i < vl && cl < 4 && ((v[i] >= 'A' && v[i] <= 'Z') || (v[i] >= 'a' && v[i] <= 'z')); i++, cl++)
    c = c * 26 + ((v[i] | 0x20) - 'a' + 1);
  for (; i < vl && v[i] >= '0' && v[i] <= '9'; i++)
    if (r < 100000000) r = r * 10 + (v[i] - '0');
  if (!cl || cl > 3 || i != vl) return 0;
  *col = c - 1;
  if (r) *row = r - 1;
  return 1;
}

static int xr_int(const char *v, int vl) {
  int n = 0;
  for (int i = 0; i < vl && v[i] >= '0' && v[i] <= '9' && n < 100000000; i++) n = n * 10 + (v[i] - '0');
  return n;
}

static void xr_sheet_parse(const char *x, const char *e, wchar_t **shared, int nshared, XrSheet *sh) {
  XrCells cs;
  memset(&cs, 0, sizeof(cs));
  XrW b;
  memset(&b, 0, sizeof(b));
  XrTag t;
  const char *p = x;
  int row = -1, col = -1;
  int inData = 0;
  while (xr_tag(p, e, &t)) {
    p = t.after;
    if (xr_is(&t, "sheetData")) {
      inData = !t.close && !t.empty;
      continue;
    }
    if (!inData) continue;
    const char *v;
    int vl;
    if (xr_is(&t, "row") && !t.close) {
      row = xr_attr(&t, "r", &v, &vl) && xr_int(v, vl) > 0 ? xr_int(v, vl) - 1 : row + 1;
      col = -1;
      continue;
    }
    if (!xr_is(&t, "c") || t.close) continue;
    int r = row < 0 ? 0 : row, c = col + 1;
    if (xr_attr(&t, "r", &v, &vl)) xr_ref(v, vl, &r, &c);
    col = c;
    if (t.empty) continue;
    char type[12] = "n";
    if (xr_attr(&t, "t", &v, &vl) && vl < 11) memcpy(type, v, (size_t)vl), type[vl] = 0;
    /* значение: <v>…</v> или строка <is><t>…</t></is>; формула <f> — мимо */
    b.n = 0;
    if (b.p) b.p[0] = 0;
    const char *raw = NULL;
    int rawl = 0;
    XrTag u;
    while (xr_tag(p, e, &u)) {
      p = u.after;
      if (xr_is(&u, "c") && u.close) break;
      if (xr_is(&u, "is") && !u.close && !u.empty) {
        p = xr_runs(p, e, "is", &b);
      } else if (xr_is(&u, "v") && !u.close && !u.empty) {
        const char *lt = (const char *)memchr(p, '<', (size_t)(e - p));
        if (!lt) lt = e;
        raw = p, rawl = (int)(lt - p);
        p = lt;
      }
    }
    if (raw) {
      if (!strcmp(type, "s")) {
        int k = xr_int(raw, rawl);
        if (k >= 0 && k < nshared && shared[k]) {
          for (const wchar_t *s = shared[k]; *s; s++) xr_wput(&b, (unsigned long)(unsigned)*s);
        }
      } else if (!strcmp(type, "n")) {
        while (rawl && (*raw == ' ' || *raw == '\n' || *raw == '\r' || *raw == '\t')) raw++, rawl--;
        while (rawl && (raw[rawl - 1] == ' ' || raw[rawl - 1] == '\n' || raw[rawl - 1] == '\r')) rawl--;
        xr_num(raw, rawl, &b);
      } else { /* str, b, e, d — текстом */
        xr_wtext(&b, raw, raw + rawl);
      }
    }
    xr_cell_add(&cs, r, c, xr_wtake(&b));
  }
  free(b.p);
  /* ячейки — в прямоугольник до последней непустой */
  int nr = 0, nc = 0;
  for (size_t i = 0; i < cs.n; i++) {
    if (cs.p[i].r + 1 > nr) nr = cs.p[i].r + 1;
    if (cs.p[i].c + 1 > nc) nc = cs.p[i].c + 1;
  }
  sh->nrows = sh->ncols = 0;
  sh->cell = nr && nc ? (wchar_t **)calloc((size_t)nr * (size_t)nc, sizeof(wchar_t *)) : NULL;
  if (sh->cell) sh->nrows = nr, sh->ncols = nc;
  for (size_t i = 0; i < cs.n; i++) {
    wchar_t **slot = sh->cell ? &sh->cell[(size_t)cs.p[i].r * (size_t)nc + (size_t)cs.p[i].c] : NULL;
    if (slot && !*slot) *slot = cs.p[i].v;
    else free(cs.p[i].v);
  }
  free(cs.p);
}

/* ---- книга ------------------------------------------------------------------------------ */

static void xr_free(XrBook *b) {
  for (int i = 0; i < b->n; i++) {
    XrSheet *s = &b->sh[i];
    for (size_t k = 0; s->cell && k < (size_t)s->nrows * (size_t)s->ncols; k++) free(s->cell[k]);
    free(s->cell);
    s->cell = NULL;
    s->nrows = s->ncols = 0;
  }
  b->n = 0;
}

static const wchar_t *xr_at(const XrSheet *s, int r, int c) {
  if (!s || !s->cell || r < 0 || c < 0 || r >= s->nrows || c >= s->ncols) return L"";
  const wchar_t *v = s->cell[(size_t)r * (size_t)s->ncols + (size_t)c];
  return v ? v : L"";
}

/* путь листа по связи rid в xl/_rels/workbook.xml.rels: «worksheets/sheet1.xml» → «xl/worksheets/sheet1.xml» */
static int xr_rel_target(const char *rels, size_t rl, const char *rid, int ridl, char *out, int cap) {
  XrTag t;
  const char *p = rels, *e = rels + rl;
  while (xr_tag(p, e, &t)) {
    p = t.after;
    const char *v, *tg;
    int vl, tl;
    if (!xr_is(&t, "Relationship") || t.close || !xr_attr(&t, "Id", &v, &vl) || vl != ridl || memcmp(v, rid, (size_t)vl) ||
        !xr_attr(&t, "Target", &tg, &tl))
      continue;
    int n = 0;
    if (tl && tg[0] == '/') tg++, tl--;
    else {
      const char *pre = "xl/";
      while (*pre && n < cap - 1) out[n++] = *pre++;
    }
    for (int i = 0; i < tl && n < cap - 1; i++) out[n++] = tg[i];
    out[n] = 0;
    return 1;
  }
  return 0;
}

/* Книга из байтов файла: листы по порядку книги. 0 — не прочиталась (в why — почему) */
static int xr_read(const unsigned char *data, size_t len, XrBook *book, wchar_t *why, int cap) {
  memset(book, 0, sizeof(*book));
  if (len >= 8 && data[0] == 0xD0 && data[1] == 0xCF && data[2] == 0x11 && data[3] == 0xE0) {
    xr_say(why, cap, L"это старый формат .xls или книга с паролем — откройте в Excel и сохраните как .xlsx без пароля");
    return 0;
  }
  XrZip z;
  if (!xr_zip_open(&z, data, len)) {
    xr_say(why, cap, L"это не книга Excel .xlsx (файл не zip)");
    return 0;
  }
  size_t wl = 0, rl = 0, sl = 0;
  unsigned char *wb = xr_zip_get(&z, "xl/workbook.xml", &wl);
  if (!wb) {
    xr_say(why, cap, L"в файле нет книги Excel (xl/workbook.xml) — это не .xlsx");
    return 0;
  }
  unsigned char *rels = xr_zip_get(&z, "xl/_rels/workbook.xml.rels", &rl);
  unsigned char *ss = xr_zip_get(&z, "xl/sharedStrings.xml", &sl);
  /* общие строки */
  wchar_t **shared = NULL;
  int nshared = 0, capShared = 0;
  if (ss) {
    XrTag t;
    XrW b;
    memset(&b, 0, sizeof(b));
    const char *p = (const char *)ss, *e = (const char *)ss + sl;
    while (xr_tag(p, e, &t)) {
      p = t.after;
      if (!xr_is(&t, "si") || t.close) continue;
      if (nshared == capShared) {
        int nc = capShared ? capShared * 2 : 256;
        wchar_t **np = (wchar_t **)realloc(shared, (size_t)nc * sizeof(wchar_t *));
        if (!np) break;
        shared = np;
        capShared = nc;
      }
      b.n = 0;
      if (b.p) b.p[0] = 0;
      if (!t.empty) p = xr_runs(p, e, "si", &b);
      shared[nshared++] = xr_wtake(&b);
    }
    free(b.p);
  }
  /* листы книги */
  XrTag t;
  const char *p = (const char *)wb, *e = (const char *)wb + wl;
  int order = 0;
  while (xr_tag(p, e, &t) && book->n < XR_MAXSHEETS) {
    p = t.after;
    if (!xr_is(&t, "sheet") || t.close) continue;
    order++;
    const char *v;
    int vl;
    char path[300] = "";
    if (!(rels && xr_attr(&t, "id", &v, &vl) && xr_rel_target((const char *)rels, rl, v, vl, path, 300)))
      snprintf(path, sizeof(path), "xl/worksheets/sheet%d.xml", order);
    size_t xl = 0;
    unsigned char *x = xr_zip_get(&z, path, &xl);
    if (!x) continue; /* лист-диаграмма или битый — мимо */
    XrSheet *sh = &book->sh[book->n++];
    XrW nb;
    memset(&nb, 0, sizeof(nb));
    if (xr_attr(&t, "name", &v, &vl)) xr_wtext(&nb, v, v + vl);
    wchar_t *nm = xr_wtake(&nb);
    free(nb.p);
    int k = 0;
    for (; nm && nm[k] && k < 99; k++) sh->name[k] = nm[k];
    sh->name[k] = 0;
    free(nm);
    sh->hidden = xr_attr_is(&t, "state", "hidden") || xr_attr_is(&t, "state", "veryHidden");
    xr_sheet_parse((const char *)x, (const char *)x + xl, shared, nshared, sh);
    free(x);
  }
  for (int i = 0; i < nshared; i++) free(shared[i]);
  free(shared);
  free(wb);
  free(rels);
  free(ss);
  if (!book->n) {
    xr_say(why, cap, L"в книге нет листов с ячейками");
    return 0;
  }
  return book->n;
}

/* лист текстом, как его кладёт в буфер Excel: Tab между ячейками, строки — «\n»; ячейка, начатая
   кавычкой, — в кавычках (иначе «Вставить» сочтёт её экранированной). malloc */
static wchar_t *xr_tsv(const XrSheet *s) {
  size_t need = 1;
  for (int r = 0; r < s->nrows; r++) {
    need += 1;
    for (int c = 0; c < s->ncols; c++) need += wcslen(xr_at(s, r, c)) * 2 + 3;
  }
  wchar_t *o = (wchar_t *)malloc(need * sizeof(wchar_t)), *w = o;
  if (!o) return NULL;
  for (int r = 0; r < s->nrows; r++) {
    int last = s->ncols - 1;
    while (last >= 0 && !xr_at(s, r, last)[0]) last--;
    for (int c = 0; c <= last; c++) {
      const wchar_t *v = xr_at(s, r, c);
      if (c) *w++ = L'\t';
      if (v[0] == L'"') {
        *w++ = L'"';
        for (; *v; v++) {
          if (*v == L'"') *w++ = L'"';
          *w++ = *v;
        }
        *w++ = L'"';
      } else {
        size_t l = wcslen(v);
        memcpy(w, v, l * sizeof(wchar_t));
        w += l;
      }
    }
    *w++ = L'\n';
  }
  *w = 0;
  return o;
}
