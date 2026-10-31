/* ---- Отсев мусорных строк после Tesseract: чистая логика, без Win32 --------------
   Проверка: native/ocr_lines_test.c (gcc -O2 -Wall -Werror -o ocr_lines_test ocr_lines_test.c).
   Подключается из tess.c.

   Tesseract с `-c tessedit_create_tsv=1` отдаёт таблицу: уровень, блок, абзац,
   строка, слово, рамка, уверенность 0–100, текст. По ней строки собираются
   заново, и то, что Tesseract выдумал из картинок (обложки, аватарки, значки
   ⓘ), уходит:
   - слово-обрывок на краю строки: одни знаки («©», «°», «‚») с уверенностью
     ниже 90, обычные («|», «=») — ниже 70; не больше трёх знаков с уверенностью
     ниже 50 («st», «Z»); одинокая буква или «(i)» — справа с уверенностью ниже
     85, слева ниже 60, а если от остальной строки её отделяет пустота шире трёх
     высот строки или она вдвое выше букв строки — ниже 90 (значок ⓘ у правого
     края, кусок обложки у левого);
   - строка, где после этого не осталось двух уверенных букв или цифр, а средняя
     уверенность низкая; где знаков больше, чем букв и цифр, или нет ни одной
     пары букв подряд, при средней уверенности ниже 70 и без двух уверенных слов
     (строки кода с «;» и скобками остаются); одни знаки, кроме скобок и «;»
     кода («}» уверенно — настоящая строка, «©» — нет).
   Обычный текст остаётся как был: слова через пробел, строки через \n, пустая
   строка между абзацами. Дефис, тире, «№» и «§» на краю строки — не обрывок
   (если за ними не идёт обрывок: «- 7 Название» теряет и «-», и «7»).
   Заодно «MP3», прочитанное русской моделью как «МРЗ», возвращается в латиницу:
   только слова, которые после замены похожих русских букв совпали с известным
   именем формата файла (MP3, PDF, DOCX…). */

#include <stddef.h>
#include <string.h>

#define OCL_WORDS 256 /* слов в одной строке; больше — строка режется */

typedef struct {
  const char *t; /* UTF-8, не оканчивается нулём */
  int n;         /* байт */
  float conf;
  int left, width, height; /* рамка слова на картинке, пикселей */
  int cp;      /* знаков (кодовых точек) */
  int alnum;   /* из них букв и цифр */
  int run;     /* самая длинная подряд идущая череда букв и цифр */
  int digits;  /* цифр */
  int ascii;   /* все знаки — из первых 128 (скобки и «;» кода — да, «©» и «°» — нет) */
} OclWord;

/* следующий знак UTF-8; плохой байт — как U+FFFD длиной 1 */
static unsigned ocl_next(const char *s, int n, int *len) {
  unsigned char c = (unsigned char)s[0];
  *len = 1;
  if (c < 0x80) return c;
  if (c >= 0xC2 && c < 0xE0 && n >= 2 && ((unsigned char)s[1] & 0xC0) == 0x80) {
    *len = 2;
    return ((c & 0x1Fu) << 6) | ((unsigned char)s[1] & 0x3Fu);
  }
  if (c >= 0xE0 && c < 0xF0 && n >= 3 && ((unsigned char)s[1] & 0xC0) == 0x80 &&
      ((unsigned char)s[2] & 0xC0) == 0x80) {
    *len = 3;
    return ((c & 0x0Fu) << 12) | (((unsigned char)s[1] & 0x3Fu) << 6) | ((unsigned char)s[2] & 0x3Fu);
  }
  if (c >= 0xF0 && c < 0xF5 && n >= 4 && ((unsigned char)s[1] & 0xC0) == 0x80 &&
      ((unsigned char)s[2] & 0xC0) == 0x80 && ((unsigned char)s[3] & 0xC0) == 0x80) {
    *len = 4;
    return ((c & 0x07u) << 18) | (((unsigned char)s[1] & 0x3Fu) << 12) |
           (((unsigned char)s[2] & 0x3Fu) << 6) | ((unsigned char)s[3] & 0x3Fu);
  }
  return 0xFFFD;
}

static int ocl_is_digit(unsigned c) { return c >= '0' && c <= '9'; }

/* буква или цифра: латиница, кириллица, греческие, латиница с диакритикой */
static int ocl_is_alnum(unsigned c) {
  if (c < 0x80) return ocl_is_digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;
  if (c >= 0x370 && c <= 0x3FF) return c != 0x37E && c != 0x387;
  if (c >= 0x400 && c <= 0x52F) return !(c >= 0x482 && c <= 0x489);
  return 0;
}

static void ocl_stats(OclWord *w) {
  int run = 0;
  w->cp = w->alnum = w->run = w->digits = 0;
  w->ascii = 1;
  for (int i = 0; i < w->n;) {
    int l;
    unsigned c = ocl_next(w->t + i, w->n - i, &l);
    i += l;
    w->cp++;
    if (c >= 0x80) w->ascii = 0;
    if (ocl_is_alnum(c)) {
      w->alnum++;
      if (ocl_is_digit(c)) w->digits++;
      if (++run > w->run) w->run = run;
    } else {
      run = 0;
    }
  }
}

/* «12.5» → 12.5; без библиотечного strtod, чтобы не зависеть от локали */
static float ocl_conf(const char *s, const char *e) {
  float v = 0, f = 0.1f;
  int neg = 0, frac = 0, any = 0;
  if (s < e && *s == '-') {
    neg = 1;
    s++;
  }
  for (; s < e; s++) {
    if (*s == '.') {
      frac = 1;
    } else if (*s >= '0' && *s <= '9') {
      any = 1;
      if (frac) {
        v += (float)(*s - '0') * f;
        f *= 0.1f;
      } else {
        v = v * 10 + (float)(*s - '0');
      }
    } else {
      break;
    }
  }
  return any ? (neg ? -v : v) : -1.0f;
}

static int ocl_int(const char *s, const char *e) {
  int v = 0;
  for (; s < e && *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
  return v;
}

/* тире, маркер, «№» и «§» на краю строки — настоящие («— привет», «• пункт», «№ п/п») */
static int ocl_keeps_edge(const OclWord *w) {
  if (w->cp != 1) return 0;
  int l;
  unsigned c = ocl_next(w->t, w->n, &l);
  return c == '-' || c == 0x2013 || c == 0x2014 || c == 0x2022 || c == 0xB7 || c == 0x2116 || c == 0xA7;
}

/* Обрывок на краю строки: значок, кусок обложки. gap — пустота между словом и
   остальной строкой, lh — высота строки (оба в пикселях картинки). */
static int ocl_junk_edge(const OclWord *w, int trailing, int gap, int lh) {
  /* далеко от остальной строки или вдвое выше букв строки — не буква, а кусок картинки */
  int far_off = lh > 0 && (gap > 3 * lh || w->height * 2 > lh * 3);
  if (w->alnum == 0) {
    if (ocl_keeps_edge(w)) return 0;
    /* «;» и скобки кода уверенно читаются, «©» и «°» с обложек — нет */
    return w->conf < (w->ascii ? 70.0f : 90.0f) || (lh > 0 && gap > 5 * lh) ||
           (lh > 0 && w->height * 2 > lh * 3 && w->conf < 90.0f);
  }
  if (w->cp <= 3 && w->conf < 50.0f) return 1;
  if (lh > 0 && w->height > 2 * lh && w->conf < 70.0f) return 1; /* вдвое выше букв строки */
  if (w->alnum <= 1 && w->cp <= 3 && !w->digits &&
      (w->conf < (trailing ? 85.0f : 60.0f) || (far_off && w->conf < 90.0f)))
    return 1;
  return w->run < 2 && w->conf < 60.0f;
}

/* ---- имена форматов: «МРЗ» → «MP3» -------------------------------------------- */

/* русская буква, похожая на латинскую (или цифру); 0 — нет такой */
static char ocl_latin(unsigned c) {
  switch (c) {
    case 0x410: return 'A'; case 0x412: return 'B'; case 0x415: return 'E'; case 0x41A: return 'K';
    case 0x41C: return 'M'; case 0x41D: return 'H'; case 0x41E: return 'O'; case 0x420: return 'P';
    case 0x421: return 'C'; case 0x422: return 'T'; case 0x425: return 'X'; case 0x423: return 'Y';
    case 0x430: return 'A'; case 0x435: return 'E'; case 0x43E: return 'O'; case 0x440: return 'P';
    case 0x441: return 'C'; case 0x445: return 'X'; case 0x443: return 'Y';
    case 0x43A: return 'K'; case 0x43C: return 'M'; case 0x442: return 'T'; /* малые прописные */
    case 0x417: case 0x437: return '3'; /* З */
    case 0x427: case 0x447: return '4'; /* Ч */
    default: return 0;
  }
}

static const char *const kOclFormats[] = {
    "MP3", "MP4", "M4A", "AAC", "OGG", "WAV", "WMA", "FLAC", "PDF", "DOC", "DOCX", "XLS", "XLSX",
    "PPT", "PPTX", "TXT", "CSV", "PNG", "JPG", "JPEG", "GIF", "BMP", "TIFF", "ZIP", "RAR", "AVI",
    "MKV", "MOV", "SVG", "XML", "HTML", "JSON"};

/* Слово из 3–5 знаков, где есть русские двойники латиницы и которое после
   замены совпало с форматом, — пишет формат в out (с нулём) и отдаёт 1.
   Регистр как у слова: есть заглавная — заглавными, иначе строчными. */
static int ocl_format(const OclWord *w, char *out) {
  char m[8];
  int k = 0, cyr = 0, upper = 0;
  if (w->cp < 3 || w->cp > 5) return 0;
  for (int i = 0; i < w->n;) {
    int l;
    unsigned c = ocl_next(w->t + i, w->n - i, &l);
    i += l;
    char a = 0;
    if (c < 0x80) {
      a = (char)c;
      if (a >= 'A' && a <= 'Z') upper = 1;
      if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
    } else {
      a = ocl_latin(c);
      if (!a) return 0;
      cyr = 1;
      if (c >= 0x410 && c <= 0x42F) upper = 1;
    }
    if (!(a >= 'A' && a <= 'Z') && !(a >= '0' && a <= '9')) return 0;
    if (k >= 6) return 0;
    m[k++] = a;
  }
  m[k] = 0;
  if (!cyr) return 0; /* латиница и так верна */
  for (size_t f = 0; f < sizeof(kOclFormats) / sizeof(kOclFormats[0]); f++) {
    if (strcmp(m, kOclFormats[f])) continue;
    for (int i = 0; i <= k; i++)
      out[i] = (char)(upper || !(m[i] >= 'A' && m[i] <= 'Z') ? m[i] : m[i] - 'A' + 'a');
    return 1;
  }
  return 0;
}

/* ---- сборка строк ------------------------------------------------------------- */

typedef struct {
  char *o;
  size_t cap, len;
  int lines;
  long lastPar; /* блок и абзац последней взятой строки */
  int full;     /* не влезло в буфер — результату не верить */
} OclOut;

static void ocl_put(OclOut *o, const char *s, size_t n) {
  if (o->len + n + 1 > o->cap) {
    o->full = 1;
    return;
  }
  memcpy(o->o + o->len, s, n);
  o->len += n;
}

/* пустота между словами i и i+1 */
static int ocl_gap(const OclWord *w, int i) { return w[i + 1].left - (w[i].left + w[i].width); }

/* Высота букв строки: медиана по словам с двумя буквами и уверенностью от 60 (не меньше
   одного такого слова — иначе по всем словам с буквами). 0 — нечем мерить. */
static int ocl_line_height(const OclWord *w, int n) {
  int h[OCL_WORDS], k = 0;
  for (int pass = 0; pass < 2 && !k; pass++)
    for (int i = 0; i < n; i++)
      if (w[i].alnum >= 2 && (pass || w[i].conf >= 60.0f)) {
        int j = k++;
        while (j > 0 && h[j - 1] > w[i].height) {
          h[j] = h[j - 1];
          j--;
        }
        h[j] = w[i].height;
      }
  return k ? h[k / 2] : 0;
}

/* Решает судьбу одной строки: срезает обрывки с краёв, потом берёт или бросает целиком. */
static void ocl_line(OclWord *w, int n, long par, OclOut *o) {
  int a = 0, b = n, lh = ocl_line_height(w, n);
  for (;;) { /* слева: обрывок, а перед обрывком знак — тоже обрывок («= A Название») */
    int gap = b - a > 1 ? ocl_gap(w, a) : 0;
    if (b - a >= 1 && ocl_junk_edge(&w[a], 0, gap, lh)) a++;
    else if (b - a >= 2 && w[a].alnum == 0 &&
             ocl_junk_edge(&w[a + 1], 0, b - a > 2 ? ocl_gap(w, a + 1) : 0, lh))
      a++;
    else break;
  }
  for (;;) {
    int gap = b - a > 1 ? ocl_gap(w, b - 2) : 0;
    if (b - a >= 1 && ocl_junk_edge(&w[b - 1], 1, gap, lh)) b--;
    else if (b - a >= 2 && w[b - 1].alnum == 0 &&
             ocl_junk_edge(&w[b - 2], 1, b - a > 2 ? ocl_gap(w, b - 3) : 0, lh))
      b--;
    else break;
  }
  if (b <= a) return;

  /* mean — средняя уверенность по буквам и цифрам, all — по всем знакам */
  int alnum = 0, cps = 0, good = 0, sym = 0, digits = 0, run2 = 0, strong = 0, ascii = 1;
  double csum = 0, call = 0;
  for (int i = a; i < b; i++) {
    alnum += w[i].alnum;
    cps += w[i].cp;
    digits += w[i].digits;
    sym += w[i].cp - w[i].alnum;
    csum += (double)w[i].conf * w[i].alnum;
    call += (double)w[i].conf * w[i].cp;
    if (w[i].conf >= 50.0f) good += w[i].alnum;
    if (w[i].run >= 2) run2 = 1;
    if (w[i].run >= 2 && w[i].conf >= 80.0f) strong++;
    if (!w[i].ascii) ascii = 0;
  }
  double mean = alnum ? csum / alnum : 0, all = call / cps;
  if (!alnum) { /* «}» и «);» кода — настоящие; «©», «°», «‚» — обрывки */
    if (!ascii || all < 90.0) return;
  } else if (digits == alnum) { /* номер, время, сумма: короткое, но настоящее */
    if (mean < 70.0) return;
  } else {
    if (mean < 40.0 || good < 2) return;
    if (good < 3 && mean < 85.0) return;
    /* знаков больше букв или ни одной пары букв подряд — только если Tesseract в себе
       не уверен; две уверенных слова с буквами (строка кода) делают её настоящей */
    if (strong < 2 && (sym > alnum || !run2) && all < 70.0) return;
  }

  if (o->lines && par != o->lastPar) ocl_put(o, "\n", 1);
  if (o->lines) ocl_put(o, "\n", 1);
  for (int i = a; i < b; i++) {
    char fixed[8];
    if (i > a) ocl_put(o, " ", 1);
    if (ocl_format(&w[i], fixed)) ocl_put(o, fixed, strlen(fixed));
    else ocl_put(o, w[i].t, (size_t)w[i].n);
  }
  o->lastPar = par;
  o->lines++;
}

/* Разбирает TSV Tesseract (tsv, n байт) и пишет в out (cap байт) отфильтрованный
   текст. Возвращает число байт текста (0 — ничего настоящего не осталось), -1 —
   это не TSV или буфер мал: тогда берут обычный вывод Tesseract. */
static int ocl_filter(const char *tsv, size_t n, char *out, size_t cap) {
  static const char kHead[] = "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num";
  OclWord w[OCL_WORDS];
  OclOut o;
  int nw = 0, started = 0;
  long key = -1, par = 0;
  const char *p = tsv, *end = tsv + n;
  o.o = out;
  o.cap = cap;
  o.len = 0;
  o.lines = 0;
  o.lastPar = -1;
  o.full = 0;
  if (!tsv || !out || cap < 2 || n < sizeof(kHead) || strncmp(tsv, kHead, sizeof(kHead) - 1)) return -1;

  while (p < end) {
    const char *e = memchr(p, '\n', (size_t)(end - p));
    const char *f[12];
    int nf = 0;
    if (!e) e = end;
    const char *le = e;
    if (le > p && le[-1] == '\r') le--;
    if (p != tsv) { /* первая строка — заголовок */
      f[nf++] = p;
      for (const char *q = p; q < le && nf < 12; q++)
        if (*q == '\t') f[nf++] = q + 1;
      if (nf == 12 && f[0][0] == '5' && f[0][1] == '\t') { /* уровень 5 — слово */
        long blk = ocl_int(f[2], f[3] - 1), par_n = ocl_int(f[3], f[4] - 1), ln = ocl_int(f[4], f[5] - 1);
        long k = (blk * 1000 + par_n) * 100000L + ln;
        OclWord cur;
        cur.t = f[11];
        cur.n = (int)(le - f[11]);
        cur.conf = ocl_conf(f[10], f[11] - 1);
        cur.left = ocl_int(f[6], f[7] - 1);
        cur.width = ocl_int(f[8], f[9] - 1);
        cur.height = ocl_int(f[9], f[10] - 1);
        while (cur.n > 0 && (cur.t[cur.n - 1] == ' ' || cur.t[cur.n - 1] == '\t')) cur.n--;
        while (cur.n > 0 && cur.t[0] == ' ') {
          cur.t++;
          cur.n--;
        }
        if (k != key || nw == OCL_WORDS) {
          if (nw) ocl_line(w, nw, par, &o);
          nw = 0;
          key = k;
          par = blk * 1000 + par_n;
        }
        if (cur.n > 0 && cur.conf >= 0) {
          ocl_stats(&cur);
          w[nw++] = cur;
        }
        started = 1;
      }
    }
    p = e < end ? e + 1 : end;
  }
  if (nw) ocl_line(w, nw, par, &o);
  if (o.full) return -1;
  if (!started) return 0;
  out[o.len] = 0;
  return (int)o.len;
}
