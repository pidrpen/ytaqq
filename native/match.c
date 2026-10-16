/* ---- Нечёткое сопоставление позиций: движок (интерфейс — match.h) -----------------

   Устройство:
     1. Строка режется на слова и числа (mt_tokenize): регистр, ё, тире, знаки и «№»
        отброшены, латинские двойники букв заменены кириллицей, слова приведены к
        словарю (сокращения), у длинных обрезано окончание. Число — отдельное
        слово: 18 и 28 никогда не «почти равны». После слова ГОСТ/ТУ/ОСТ… идёт код
        стандарта (отдельный вид слова), а за ним год — год в счёт не идёт.
     2. Позиции набора раскладываются по словарю слов; запрос находит кандидатов
        по общим словам (с опечатками тоже — слово ищется в словаре нечётко) и по
        совпавшему обозначению.
     3. Каждая пара оценивается: сходство обозначений (убраны пробелы, точки,
        тире, нули слева), покрытие слов запроса словами кандидата с поправкой на
        числа и стандарт, символьные триграммы для слитного написания.
   Только правила и расстояние между строками, модели ИИ тут нет. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "match.h"

#define MT_MAXTOK 48
#define MT_MAXSRC 1000
#define MT_MAXCAND 120
enum { MT_WORD, MT_NUM, MT_STDW, MT_STDC, MT_YEAR };

typedef struct {
  const wchar_t *s;
  int len, vid;
  unsigned char kind, mod, dlen;
} MtTok;

/* ---- хеш-таблица строка → указатель -------------------------------------------- */
typedef struct {
  wchar_t **k;
  int *kl;
  void **v;
  int cap, n;
} MtMap;

static wchar_t *mt_wdup(const wchar_t *s, int len) {
  wchar_t *d = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
  if (!d) return NULL;
  wmemcpy(d, s, (size_t)len);
  d[len] = 0;
  return d;
}

static unsigned mt_hash(const wchar_t *s, int len) {
  unsigned h = 2166136261u;
  for (int i = 0; i < len; i++) h = (h ^ (unsigned)s[i]) * 16777619u;
  return h;
}

static void mt_map_free(MtMap *m) {
  for (int i = 0; i < m->cap; i++) free(m->k[i]);
  free(m->k);
  free(m->kl);
  free(m->v);
  memset(m, 0, sizeof(*m));
}

static void mt_map_grow(MtMap *m) {
  MtMap o = *m;
  int cap = o.cap ? o.cap * 2 : 256;
  m->k = (wchar_t **)calloc((size_t)cap, sizeof(wchar_t *));
  m->kl = (int *)calloc((size_t)cap, sizeof(int));
  m->v = (void **)calloc((size_t)cap, sizeof(void *));
  m->cap = cap;
  for (int i = 0; i < o.cap; i++) {
    if (!o.k[i]) continue;
    unsigned h = mt_hash(o.k[i], o.kl[i]) & (unsigned)(cap - 1);
    while (m->k[h]) h = (h + 1) & (unsigned)(cap - 1);
    m->k[h] = o.k[i];
    m->kl[h] = o.kl[i];
    m->v[h] = o.v[i];
  }
  free(o.k);
  free(o.kl);
  free(o.v);
}

/* слот значения; без create — NULL, если ключа нет */
static void **mt_map_get(MtMap *m, const wchar_t *s, int len, int create) {
  if (create && (m->n + 1) * 2 > m->cap) mt_map_grow(m);
  if (!m->cap) return NULL;
  unsigned mask = (unsigned)(m->cap - 1), h = mt_hash(s, len) & mask;
  while (m->k[h]) {
    if (m->kl[h] == len && !wmemcmp(m->k[h], s, (size_t)len)) return &m->v[h];
    h = (h + 1) & mask;
  }
  if (!create) return NULL;
  m->k[h] = mt_wdup(s, len);
  m->kl[h] = len;
  m->v[h] = NULL;
  m->n++;
  return &m->v[h];
}

/* ---- буквы ---------------------------------------------------------------------- */
static wchar_t mt_fold(wchar_t c) {
  if (c >= 'A' && c <= 'Z') return (wchar_t)(c + 32);
  if (c >= 0x410 && c <= 0x42F) return (wchar_t)(c + 32);
  if (c == 0x401 || c == 0x451) return 0x435; /* Ё, ё → е */
  if (c == 0xD7) return 'x';                  /* × */
  return c;
}

static int mt_isdig(wchar_t c) { return c >= '0' && c <= '9'; }
static int mt_iscyr(wchar_t c) { return c >= 0x430 && c <= 0x44F; }
static int mt_islat(wchar_t c) { return c >= 'a' && c <= 'z'; }
static int mt_isletter(wchar_t c) { return mt_iscyr(c) || mt_islat(c); }

/* латинская буква, похожая на русскую: a b c e h k m o p t x y */
static wchar_t mt_lookalike(wchar_t c) {
  switch (c) {
  case 'a': return 0x430;
  case 'b': return 0x432;
  case 'c': return 0x441;
  case 'e': return 0x435;
  case 'h': return 0x43D;
  case 'k': return 0x43A;
  case 'm': return 0x43C;
  case 'o': return 0x43E;
  case 'p': return 0x440;
  case 't': return 0x442;
  case 'x': return 0x445;
  case 'y': return 0x443;
  }
  return 0;
}

/* слово: свёрнутые буквы, латинские двойники — кириллицей, если в слове есть
   кириллица или оно целиком из двойников; возвращает длину */
static int mt_word_key(const wchar_t *run, int len, wchar_t *out) {
  int cyr = 0, other = 0;
  for (int i = 0; i < len; i++) {
    wchar_t c = mt_fold(run[i]);
    if (mt_iscyr(c)) cyr = 1;
    else if (!mt_lookalike(c)) other = 1;
  }
  int map = cyr || !other;
  for (int i = 0; i < len; i++) {
    wchar_t c = mt_fold(run[i]);
    wchar_t l = map ? mt_lookalike(c) : 0;
    out[i] = l ? l : c;
  }
  return len;
}

/* убрать окончание у длинного русского слова */
static int mt_stem(wchar_t *w, int len) {
  static const wchar_t *const suf[] = {L"ыми", L"ими", L"ого", L"его", L"ому", L"ему", L"ая", L"яя", L"ое", L"ее",
                                       L"ые", L"ие", L"ый", L"ий", L"ой", L"ых", L"их", L"ом", L"ем", L"ою",
                                       L"ею", L"ам", L"ям", L"ов", L"ев", L"ей", L"а",  L"я",  L"у",  L"ю",
                                       L"ы",  L"и",  L"е",  L"о"};
  if (len < 5 || !mt_iscyr(w[0])) return len;
  int best = 0;
  for (size_t i = 0; i < sizeof(suf) / sizeof(suf[0]); i++) {
    int sl = (int)wcslen(suf[i]);
    if (sl > best && len - sl >= 4 && !wmemcmp(w + len - sl, suf[i], (size_t)sl)) best = sl;
  }
  return len - best;
}

/* ---- правила --------------------------------------------------------------------- */
struct MtRules {
  MtMap syn; /* слово → основное слово (wchar_t*) */
  MtMap ign; /* слова, которые не считаются */
  MtMap std; /* слова-стандарты: гост, ту, ост… */
  int t[3];
  wchar_t **own;
  int nown, capown;
};

static void mt_own(MtRules *r, wchar_t *p) {
  if (r->nown == r->capown) {
    r->capown = r->capown ? r->capown * 2 : 64;
    r->own = (wchar_t **)realloc(r->own, (size_t)r->capown * sizeof(wchar_t *));
  }
  r->own[r->nown++] = p;
}

void mt_rules_free(MtRules *r) {
  if (!r) return;
  mt_map_free(&r->syn);
  mt_map_free(&r->ign);
  mt_map_free(&r->std);
  for (int i = 0; i < r->nown; i++) free(r->own[i]);
  free(r->own);
  free(r);
}

int mt_rules_threshold(const MtRules *r, int which) {
  static const int def[3] = {97, 60, 35};
  if (which < 0 || which > 2) return 0;
  return r && r->t[which] > 0 ? r->t[which] : def[which];
}

/* разбить строку правила на слова-ключи (буквы), вызвать f для каждого */
static int mt_rule_words(const wchar_t *s, int len, wchar_t out[][40], int max) {
  int n = 0, i = 0;
  while (i < len && n < max) {
    while (i < len && !mt_isletter(mt_fold(s[i]))) i++;
    int j = i;
    while (j < len && mt_isletter(mt_fold(s[j]))) j++;
    if (j > i && j - i < 39) {
      int l = mt_word_key(s + i, j - i, out[n]);
      out[n][l] = 0;
      n++;
    }
    i = j;
  }
  return n;
}

MtRules *mt_rules_parse(const wchar_t *text) {
  MtRules *r = (MtRules *)calloc(1, sizeof(MtRules));
  if (!r) return NULL;
  int sec = 0; /* 1 синонимы, 2 игнорировать, 3 стандарты, 4 пороги */
  const wchar_t *p = text ? text : L"";
  if (*p == 0xFEFF) p++;
  while (*p) {
    const wchar_t *e = p;
    while (*e && *e != '\n') e++;
    int len = (int)(e - p);
    while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
    while (len > 0 && (*p == ' ' || *p == '\t')) p++, len--;
    if (len > 0 && *p != '#' && *p != ';') {
      if (*p == '[') {
        wchar_t nm[40][40];
        int n = mt_rule_words(p, len, nm, 1);
        sec = 0;
        if (n) {
          if (!wcsncmp(nm[0], L"синоним", 7)) sec = 1;
          else if (!wcsncmp(nm[0], L"игнор", 5)) sec = 2;
          else if (!wcsncmp(nm[0], L"стандарт", 8)) sec = 3;
          else if (!wcsncmp(nm[0], L"порог", 5)) sec = 4;
        }
      } else if (sec == 1 || sec == 4) {
        const wchar_t *eq = p;
        while (eq < p + len && *eq != '=') eq++;
        if (eq < p + len) {
          if (sec == 1) {
            wchar_t w[40][40];
            int nl = mt_rule_words(p, (int)(eq - p), w, 1);
            int nr = 0;
            wchar_t rw[32][40];
            nr = mt_rule_words(eq + 1, (int)(p + len - eq - 1), rw, 32);
            if (nl == 1) {
              wchar_t *canon = mt_wdup(w[0], (int)wcslen(w[0]));
              mt_own(r, canon);
              for (int i = 0; i < nr; i++) {
                void **s = mt_map_get(&r->syn, rw[i], (int)wcslen(rw[i]), 1);
                if (s) *s = canon;
              }
              if (!wcscmp(w[0], L"гост") || !wcscmp(w[0], L"ту") || !wcscmp(w[0], L"ост") || !wcscmp(w[0], L"din") ||
                  !wcscmp(w[0], L"iso")) {
                void **s = mt_map_get(&r->std, canon, (int)wcslen(canon), 1);
                if (s) *s = canon;
              }
            }
          } else {
            wchar_t k[40][40];
            int nk = mt_rule_words(p, (int)(eq - p), k, 1);
            int v = (int)wcstol(eq + 1, NULL, 10);
            if (nk) {
              if (!wcsncmp(k[0], L"точно", 5)) r->t[MT_T_EXACT] = v;
              else if (!wcsncmp(k[0], L"вероятно", 8)) r->t[MT_T_LIKELY] = v;
              else if (!wcsncmp(k[0], L"показывать", 10)) r->t[MT_T_SHOW] = v;
            }
          }
        }
      } else if (sec == 2 || sec == 3) {
        wchar_t w[40][40];
        int n = mt_rule_words(p, len, w, 40);
        for (int i = 0; i < n; i++) {
          MtMap *m = sec == 2 ? &r->ign : &r->std;
          void **s = mt_map_get(m, w[i], (int)wcslen(w[i]), 1);
          if (s) *s = (void *)1;
        }
      }
    }
    p = *e ? e + 1 : e;
  }
  return r;
}

/* ---- разбор строки на слова ---------------------------------------------------- */
/* desmode: обозначение — «830.001» не десятичная дробь, «2.5» дробь */
static int mt_tokenize(const MtRules *r, const wchar_t *src, int desmode, MtTok *out, int maxt, wchar_t *pool) {
  wchar_t f[MT_MAXSRC + 1];
  int n = 0;
  for (; src && src[n] && n < MT_MAXSRC; n++) {
    wchar_t c = mt_fold(src[n]);
    if (c == 0xD8 || c == 0xF8 || c == 0x2300) c = ' '; /* Ø ø ⌀ */
    f[n] = c;
  }
  f[n] = 0;
  int nt = 0, pp = 0, i = 0;
  while (i < n && nt < maxt) {
    wchar_t c = f[i];
    if (mt_isdig(c)) {
      int a = i;
      while (i < n && mt_isdig(f[i])) i++;
      int ie = i, fa = 0, fb = 0;
      if (i + 1 < n && (f[i] == ',' || f[i] == '.') && mt_isdig(f[i + 1])) {
        int j = i + 1;
        while (j < n && mt_isdig(f[j])) j++;
        int dl = ie - a, fl = j - (i + 1);
        if (f[i] == ',' || !desmode || (dl <= 2 && fl <= 2)) {
          fa = i + 1;
          fb = j;
          i = j;
        }
      }
      /* число: нули слева и справа от запятой не считаются */
      int s = a;
      while (s < ie - 1 && f[s] == '0') s++;
      wchar_t *t = pool + pp;
      int l = 0;
      for (int k = s; k < ie; k++) t[l++] = f[k];
      if (fa) {
        int fe = fb;
        while (fe > fa && f[fe - 1] == '0') fe--;
        if (fe > fa) {
          t[l++] = '.';
          for (int k = fa; k < fe; k++) t[l++] = f[k];
        }
      }
      t[l] = 0;
      pp += l + 1;
      MtTok *k = &out[nt++];
      k->s = t;
      k->len = l;
      k->vid = -1;
      k->kind = MT_NUM;
      k->mod = 0;
      k->dlen = (unsigned char)(ie - a > 255 ? 255 : ie - a);
    } else if (mt_isletter(c)) {
      int a = i;
      while (i < n && mt_isletter(f[i])) i++;
      wchar_t w[64];
      int l = i - a > 63 ? 63 : i - a;
      mt_word_key(f + a, l, w);
      w[l] = 0;
      if (r && mt_map_get((MtMap *)&r->ign, w, l, 0)) continue;
      MtTok *k = &out[nt++];
      k->vid = -1;
      k->kind = MT_WORD;
      k->mod = 0;
      k->dlen = 0;
      void **sy = r ? mt_map_get((MtMap *)&r->syn, w, l, 0) : NULL;
      if (sy && *sy) {
        k->s = (const wchar_t *)*sy;
        k->len = (int)wcslen(k->s);
        k->mod = (unsigned char)(wcscmp(k->s, w) ? 1 : 0);
      } else {
        l = mt_stem(w, l);
        wchar_t *t = pool + pp;
        wmemcpy(t, w, (size_t)l);
        t[l] = 0;
        pp += l + 1;
        k->s = t;
        k->len = l;
      }
      if (r && mt_map_get((MtMap *)&r->std, k->s, k->len, 0)) k->kind = MT_STDW;
    } else {
      i++;
    }
  }
  /* «2 х 1500 х 3000» — х между числами не слово */
  int w = 0;
  for (int k = 0; k < nt; k++) {
    if (out[k].kind == MT_WORD && out[k].len == 1 && out[k].s[0] == 0x445 && k > 0 && k + 1 < nt &&
        out[k - 1].kind == MT_NUM && out[k + 1].kind == MT_NUM)
      continue;
    out[w++] = out[k];
  }
  nt = w;
  /* код стандарта и год */
  for (int k = 0; k < nt; k++) {
    if (out[k].kind != MT_STDW) continue;
    int j = k + 1;
    if (j < nt && out[j].kind == MT_WORD && out[j].len == 1 && out[j].s[0] == 0x440) j++; /* ГОСТ Р */
    if (j < nt && out[j].kind == MT_NUM) {
      out[j].kind = MT_STDC;
      if (j + 1 < nt && out[j + 1].kind == MT_NUM && out[j + 1].len <= 4) {
        int dl = out[j + 1].dlen;
        if (dl == 2 || (dl == 4 && (out[j + 1].s[0] == '1' || out[j + 1].s[0] == '2') && out[j + 1].len == 4))
          out[j + 1].kind = MT_YEAR;
      }
    }
  }
  return nt;
}

/* обозначение без пробелов, знаков и нулей слева; возвращает длину */
static int mt_compact(const wchar_t *src, wchar_t *out, int cap) {
  wchar_t f[MT_MAXSRC + 1];
  int n = 0, cyr = 0, other = 0;
  for (; src && src[n] && n < MT_MAXSRC; n++) {
    f[n] = mt_fold(src[n]);
    if (mt_iscyr(f[n])) cyr = 1;
    else if (mt_islat(f[n]) && !mt_lookalike(f[n])) other = 1;
  }
  int map = cyr || !other, l = 0;
  for (int i = 0; i < n && l < cap - 1; i++) {
    wchar_t c = f[i];
    if (mt_isdig(c)) {
      int skip = c == '0' && (i == 0 || !mt_isdig(f[i - 1])); /* нули в начале числа */
      while (skip && i + 1 < n && f[i] == '0' && mt_isdig(f[i + 1])) i++;
      if (skip) c = f[i];
      out[l++] = c;
    } else if (mt_isletter(c)) {
      wchar_t m = map ? mt_lookalike(c) : 0;
      out[l++] = m ? m : c;
    }
  }
  out[l] = 0;
  return l;
}

/* расстояние Дамерау — Левенштейна; больше maxd — maxd + 1 */
static int mt_dl(const wchar_t *a, int la, const wchar_t *b, int lb, int maxd) {
  if (la - lb > maxd || lb - la > maxd) return maxd + 1;
  if (la > 250 || lb > 250) return maxd + 1;
  int r0[260], r1[260], r2[260];
  int *p2 = r0, *p1 = r1, *cur = r2;
  for (int j = 0; j <= lb; j++) p1[j] = j;
  for (int i = 1; i <= la; i++) {
    cur[0] = i;
    int rowmin = cur[0];
    for (int j = 1; j <= lb; j++) {
      int cost = a[i - 1] == b[j - 1] ? 0 : 1;
      int v = p1[j - 1] + cost;
      if (p1[j] + 1 < v) v = p1[j] + 1;
      if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1;
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] && p2[j - 2] + 1 < v) v = p2[j - 2] + 1;
      cur[j] = v;
      if (v < rowmin) rowmin = v;
    }
    if (rowmin > maxd) return maxd + 1;
    int *t = p2;
    p2 = p1;
    p1 = cur;
    cur = t;
  }
  return p1[lb];
}

/* ---- индекс ---------------------------------------------------------------------- */
typedef struct {
  MtTok *d, *n, *all;
  int nd, nn, nall;
  wchar_t *dcomp, *acomp;
  int dlen, alen;
  uint32_t *tri;
  int ntri;
} MtDoc;

typedef struct {
  int nex;
  int *vid;
  float *sim;
} MtExp;

struct MtIndex {
  const MtRules *r;
  MtMap vmap;
  wchar_t **vstr;
  int *vlen, *vdf;
  unsigned char *vkind;
  uint64_t *vmask;
  int nv, capv;
  MtDoc *doc;
  int n, cap;
  int *pstart, *plist;
  MtMap dmap, amap;
  int *dnext, *anext;
  MtMap xmap;
  float *acc;
  int *touched;
};

MtIndex *mt_index_new(const MtRules *r) {
  MtIndex *ix = (MtIndex *)calloc(1, sizeof(MtIndex));
  if (ix) ix->r = r;
  return ix;
}

static uint64_t mt_mask(const wchar_t *s, int len) {
  uint64_t m = 0;
  for (int i = 0; i < len; i++) m |= (uint64_t)1 << ((unsigned)s[i] & 63);
  return m;
}

static int mt_popcount(uint64_t x) {
  int c = 0;
  while (x) {
    x &= x - 1;
    c++;
  }
  return c;
}

static int mt_intern(MtIndex *ix, const MtTok *t) {
  void **s = mt_map_get(&ix->vmap, t->s, t->len, 1);
  if (!s) return -1;
  if (*s) {
    int id = (int)(intptr_t)*s - 1;
    ix->vdf[id]++;
    return id;
  }
  if (ix->nv == ix->capv) {
    ix->capv = ix->capv ? ix->capv * 2 : 1024;
    ix->vstr = (wchar_t **)realloc(ix->vstr, (size_t)ix->capv * sizeof(wchar_t *));
    ix->vlen = (int *)realloc(ix->vlen, (size_t)ix->capv * sizeof(int));
    ix->vdf = (int *)realloc(ix->vdf, (size_t)ix->capv * sizeof(int));
    ix->vkind = (unsigned char *)realloc(ix->vkind, (size_t)ix->capv);
    ix->vmask = (uint64_t *)realloc(ix->vmask, (size_t)ix->capv * sizeof(uint64_t));
  }
  int id = ix->nv++;
  ix->vstr[id] = mt_wdup(t->s, t->len);
  ix->vlen[id] = t->len;
  ix->vdf[id] = 1;
  ix->vkind[id] = t->kind;
  ix->vmask[id] = mt_mask(t->s, t->len);
  *s = (void *)(intptr_t)(id + 1);
  return id;
}

static int mt_u32cmp(const void *a, const void *b) {
  uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
  return x < y ? -1 : x > y;
}

static int mt_trigrams(const wchar_t *c, int len, uint32_t **out) {
  if (len < 3) {
    *out = NULL;
    return 0;
  }
  int n = len - 2;
  uint32_t *t = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
  for (int i = 0; i < n; i++) t[i] = ((uint32_t)c[i] * 31u + (uint32_t)c[i + 1]) * 31u + (uint32_t)c[i + 2];
  qsort(t, (size_t)n, sizeof(uint32_t), mt_u32cmp);
  *out = t;
  return n;
}

static double mt_dice(const uint32_t *a, int na, const uint32_t *b, int nb) {
  if (!na || !nb) return 0;
  int i = 0, j = 0, c = 0;
  while (i < na && j < nb) {
    if (a[i] == b[j]) c++, i++, j++;
    else if (a[i] < b[j]) i++;
    else j++;
  }
  return 2.0 * c / (na + nb);
}

static wchar_t *mt_join(const wchar_t *a, const wchar_t *b) {
  size_t la = a ? wcslen(a) : 0, lb = b ? wcslen(b) : 0;
  wchar_t *s = (wchar_t *)malloc((la + lb + 2) * sizeof(wchar_t));
  if (!s) return NULL;
  if (la) wmemcpy(s, a, la);
  s[la] = ' ';
  if (lb) wmemcpy(s + la + 1, b, lb);
  s[la + 1 + lb] = 0;
  return s;
}

int mt_index_add(MtIndex *ix, const wchar_t *des, const wchar_t *name) {
  if (ix->n == ix->cap) {
    ix->cap = ix->cap ? ix->cap * 2 : 1024;
    ix->doc = (MtDoc *)realloc(ix->doc, (size_t)ix->cap * sizeof(MtDoc));
  }
  MtDoc *d = &ix->doc[ix->n];
  memset(d, 0, sizeof(*d));
  wchar_t *pool = (wchar_t *)malloc((size_t)MT_MAXSRC * 4 * sizeof(wchar_t) + 4096);
  MtTok td[MT_MAXTOK], tn[MT_MAXTOK];
  if (!pool) return -1;
  d->nd = mt_tokenize(ix->r, des, 1, td, MT_MAXTOK, pool);
  d->nn = mt_tokenize(ix->r, name, 0, tn, MT_MAXTOK, pool + MT_MAXSRC * 2);
  d->d = (MtTok *)malloc((size_t)(d->nd + 1) * sizeof(MtTok));
  d->n = (MtTok *)malloc((size_t)(d->nn + 1) * sizeof(MtTok));
  d->all = (MtTok *)malloc((size_t)(d->nd + d->nn + 1) * sizeof(MtTok));
  for (int i = 0; i < d->nd; i++) {
    td[i].vid = mt_intern(ix, &td[i]);
    td[i].s = td[i].vid >= 0 ? ix->vstr[td[i].vid] : td[i].s;
    d->d[i] = td[i];
  }
  for (int i = 0; i < d->nn; i++) {
    tn[i].vid = mt_intern(ix, &tn[i]);
    tn[i].s = tn[i].vid >= 0 ? ix->vstr[tn[i].vid] : tn[i].s;
    d->n[i] = tn[i];
  }
  memcpy(d->all, d->d, (size_t)d->nd * sizeof(MtTok));
  memcpy(d->all + d->nd, d->n, (size_t)d->nn * sizeof(MtTok));
  d->nall = d->nd + d->nn;
  free(pool);
  wchar_t c[256];
  d->dlen = mt_compact(des, c, 256);
  d->dcomp = d->dlen ? mt_wdup(c, d->dlen) : NULL;
  wchar_t *j = mt_join(des, name);
  d->alen = j ? mt_compact(j, c, 256) : 0;
  free(j);
  d->acomp = d->alen ? mt_wdup(c, d->alen) : NULL;
  d->ntri = d->alen ? mt_trigrams(d->acomp, d->alen, &d->tri) : 0;
  return ix->n++;
}

void mt_index_build(MtIndex *ix) {
  int *cnt = (int *)calloc((size_t)ix->nv + 1, sizeof(int));
  ix->pstart = (int *)calloc((size_t)ix->nv + 2, sizeof(int));
  for (int i = 0; i < ix->n; i++) {
    MtDoc *d = &ix->doc[i];
    for (int k = 0; k < d->nall; k++) {
      int v = d->all[k].vid, dup = 0;
      if (v < 0) continue;
      for (int m = 0; m < k; m++) if (d->all[m].vid == v) dup = 1;
      if (!dup) cnt[v]++;
    }
  }
  for (int v = 0; v < ix->nv; v++) ix->pstart[v + 1] = ix->pstart[v] + cnt[v];
  ix->plist = (int *)malloc((size_t)(ix->pstart[ix->nv] + 1) * sizeof(int));
  memset(cnt, 0, (size_t)ix->nv * sizeof(int));
  for (int i = 0; i < ix->n; i++) {
    MtDoc *d = &ix->doc[i];
    for (int k = 0; k < d->nall; k++) {
      int v = d->all[k].vid, dup = 0;
      if (v < 0) continue;
      for (int m = 0; m < k; m++) if (d->all[m].vid == v) dup = 1;
      if (!dup) ix->plist[ix->pstart[v] + cnt[v]++] = i;
    }
  }
  free(cnt);
  ix->dnext = (int *)malloc((size_t)(ix->n + 1) * sizeof(int));
  ix->anext = (int *)malloc((size_t)(ix->n + 1) * sizeof(int));
  for (int i = 0; i < ix->n; i++) {
    MtDoc *d = &ix->doc[i];
    ix->dnext[i] = ix->anext[i] = -1;
    if (d->dcomp) {
      void **s = mt_map_get(&ix->dmap, d->dcomp, d->dlen, 1);
      ix->dnext[i] = *s ? (int)(intptr_t)*s - 1 : -1;
      *s = (void *)(intptr_t)(i + 1);
    }
    if (d->acomp) {
      void **s = mt_map_get(&ix->amap, d->acomp, d->alen, 1);
      ix->anext[i] = *s ? (int)(intptr_t)*s - 1 : -1;
      *s = (void *)(intptr_t)(i + 1);
    }
  }
  ix->acc = (float *)calloc((size_t)ix->n + 1, sizeof(float));
  ix->touched = (int *)malloc((size_t)(ix->n + 1) * sizeof(int));
}

void mt_index_free(MtIndex *ix) {
  if (!ix) return;
  for (int i = 0; i < ix->n; i++) {
    MtDoc *d = &ix->doc[i];
    free(d->d);
    free(d->n);
    free(d->all);
    free(d->dcomp);
    free(d->acomp);
    free(d->tri);
  }
  free(ix->doc);
  for (int v = 0; v < ix->nv; v++) free(ix->vstr[v]);
  free(ix->vstr);
  free(ix->vlen);
  free(ix->vdf);
  free(ix->vkind);
  free(ix->vmask);
  free(ix->pstart);
  free(ix->plist);
  free(ix->dnext);
  free(ix->anext);
  free(ix->acc);
  free(ix->touched);
  mt_map_free(&ix->vmap);
  mt_map_free(&ix->dmap);
  mt_map_free(&ix->amap);
  for (int i = 0; i < ix->xmap.cap; i++) {
    MtExp *e = (MtExp *)ix->xmap.v[i];
    if (e) {
      free(e->vid);
      free(e->sim);
      free(e);
    }
  }
  mt_map_free(&ix->xmap);
  free(ix);
}

/* ---- сходство слов и пар ---------------------------------------------------------- */
static double mt_tsim(const MtTok *a, const MtTok *b, unsigned *fl) {
  int same = a->len == b->len && !wmemcmp(a->s, b->s, (size_t)a->len);
  int numa = a->kind == MT_NUM || a->kind == MT_STDC || a->kind == MT_YEAR;
  int numb = b->kind == MT_NUM || b->kind == MT_STDC || b->kind == MT_YEAR;
  if (same) {
    if (a->mod || b->mod) *fl |= MTF_ABBR;
    return 1.0;
  }
  if (numa || numb) return 0;
  int mn = a->len < b->len ? a->len : b->len, mx = a->len < b->len ? b->len : a->len;
  if (mn < 4) return 0;
  const MtTok *sh = a->len < b->len ? a : b, *lo = a->len < b->len ? b : a;
  if (!wmemcmp(sh->s, lo->s, (size_t)mn)) {
    *fl |= MTF_ABBR;
    return 0.86;
  }
  int maxd = mx <= 5 ? 1 : mx <= 9 ? 2 : 3;
  int d = mt_dl(a->s, a->len, b->s, b->len, maxd);
  if (d > maxd) return 0;
  *fl |= MTF_TYPO;
  return 1.0 - (double)d / mx;
}

static double mt_weight(const MtTok *t) {
  switch (t->kind) {
  case MT_NUM: return 1.3;
  case MT_STDW: return 0.3;
  case MT_STDC: return 1.0;
  case MT_YEAR: return 0;
  }
  return t->len >= 3 ? 1.0 : t->len == 2 ? 0.6 : 0.4;
}

typedef struct {
  double covq, covc;
  unsigned fl;
  int sizeMiss, stdMiss, candHasStd, noWord;
  wchar_t miss[24];
} MtCov;

/* какая доля слов запроса есть у кандидата и какая доля слов кандидата объяснена */
static void mt_cover(const MtTok *q, int nq, const MtTok *c, int nc, MtCov *o) {
  char uq[MT_MAXTOK] = {0}, uc[MT_MAXTOK] = {0};
  double msum = 0, wq = 0, wc = 0, mc = 0;
  int words = 0, wordHit = 0;
  memset(o, 0, sizeof(*o));
  for (int i = 0; i < nq; i++) {
    wq += mt_weight(&q[i]);
    if (q[i].kind == MT_WORD && q[i].len >= 3) words++;
  }
  for (int j = 0; j < nc; j++) {
    wc += mt_weight(&c[j]);
    if (c[j].kind == MT_STDC) o->candHasStd = 1;
  }
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < nq; i++) {
      if (uq[i] || q[i].kind == MT_YEAR) continue;
      double best = 0;
      int bj = -1;
      unsigned bf = 0;
      for (int j = 0; j < nc; j++) {
        if (uc[j] || c[j].kind == MT_YEAR) continue;
        unsigned f = 0;
        double s = pass == 0 ? (q[i].len == c[j].len && !wmemcmp(q[i].s, c[j].s, (size_t)q[i].len) ? 1.0 : 0)
                             : mt_tsim(&q[i], &c[j], &f);
        if (pass == 0 && s > 0) mt_tsim(&q[i], &c[j], &f);
        if (s > best) best = s, bj = j, bf = f;
      }
      if (bj >= 0) {
        uq[i] = uc[bj] = 1;
        if (q[i].kind == MT_WORD && q[i].len >= 3) wordHit++;
        msum += mt_weight(&q[i]) * best;
        mc += mt_weight(&c[bj]) * best;
        o->fl |= bf;
      }
    }
  }
  for (int i = 0; i < nq; i++) {
    if (uq[i] || q[i].kind == MT_YEAR) continue;
    if (q[i].kind == MT_NUM) {
      o->sizeMiss++;
      if (!o->miss[0]) {
        int l = q[i].len < 23 ? q[i].len : 23;
        wmemcpy(o->miss, q[i].s, (size_t)l);
        o->miss[l] = 0;
      }
    } else if (q[i].kind == MT_STDC) {
      o->stdMiss++;
    }
  }
  o->noWord = words > 0 && wordHit == 0; /* ни одного слова: совпали одни числа — не то */
  o->covq = wq > 0 ? msum / wq : 0;
  o->covc = wc > 0 ? mc / wc : 0;
}

static double mt_cov_score(const MtCov *o) {
  double s = 0.82 * o->covq + 0.18 * o->covc;
  if (o->sizeMiss) s *= 0.45;
  if (o->stdMiss && o->candHasStd) s *= 0.8;
  if (o->noWord) s *= 0.5;
  return s;
}

typedef struct {
  double s;
  unsigned fl;
  wchar_t miss[24];
} MtRes;

static void mt_fold_flags(MtRes *res, const MtCov *c) {
  res->fl |= c->fl;
  if (c->sizeMiss) {
    res->fl |= MTF_SIZE_DIFF;
    if (!res->miss[0]) wcscpy(res->miss, c->miss);
  }
  if (c->stdMiss && c->candHasStd) res->fl |= MTF_STD_DIFF;
  if (c->covq < 0.999) res->fl |= MTF_PARTIAL;
}

static void mt_score(const MtDoc *q, const MtDoc *c, MtRes *res) {
  memset(res, 0, sizeof(*res));
  MtCov cf;
  mt_cover(q->all, q->nall, c->all, c->nall, &cf);
  double F = mt_cov_score(&cf);
  if (q->alen >= 6 && c->alen >= 6) {
    double dc = 0.85 * mt_dice(q->tri, q->ntri, c->tri, c->ntri);
    if (cf.sizeMiss) dc *= 0.45;
    if (cf.stdMiss && cf.candHasStd) dc *= 0.8;
    if (cf.noWord) dc *= 0.5;
    if (dc > F) F = dc;
  }
  int hasD = q->dlen > 0 && c->dlen > 0, hasN = q->nn > 0 && c->nn > 0;
  double D = -1, N = -1;
  MtRes tmp;
  memset(&tmp, 0, sizeof(tmp));
  if (hasD) {
    if (q->dlen == c->dlen && !wmemcmp(q->dcomp, c->dcomp, (size_t)q->dlen)) {
      D = 1;
      res->fl |= MTF_DES_EXACT;
    } else {
      int mx = q->dlen > c->dlen ? q->dlen : c->dlen;
      int d = mt_dl(q->dcomp, q->dlen, c->dcomp, c->dlen, mx);
      D = 0.75 * (1.0 - (double)d / mx);
      if (hasN) res->fl |= MTF_DES_DIFF;
    }
  }
  if (hasN) {
    MtCov cn;
    mt_cover(q->n, q->nn, c->n, c->nn, &cn);
    N = mt_cov_score(&cn);
    mt_fold_flags(&tmp, &cn);
    if (cn.covq > 0.999 && cn.covc > 0.999 && !(cn.fl & MTF_TYPO)) res->fl |= MTF_NAME_EXACT;
  }
  double s;
  if (hasD && hasN) {
    s = D == 1 ? 0.6 + 0.4 * N : N * (0.3 + 0.7 * D);
    if (0.97 * F > s) s = 0.97 * F;
  } else {
    s = F;
    if (hasD && !hasN && D == 1) s = 1.0;
  }
  res->s = s > 1 ? 1 : s;
  mt_fold_flags(res, &cf);
  res->fl |= tmp.fl & (MTF_SIZE_DIFF | MTF_STD_DIFF);
  if (!res->miss[0]) wcscpy(res->miss, tmp.miss);
  if (res->fl & MTF_DES_EXACT) res->fl &= ~MTF_DES_DIFF;
}

/* ---- поиск --------------------------------------------------------------------- */
static MtExp *mt_expand(MtIndex *ix, const MtTok *t) {
  void **s = mt_map_get(&ix->xmap, t->s, t->len, 1);
  if (!s) return NULL;
  if (*s) return (MtExp *)*s;
  MtExp *e = (MtExp *)calloc(1, sizeof(MtExp));
  int cap = 0;
  void **ex = mt_map_get(&ix->vmap, t->s, t->len, 0);
  if (ex && *ex) {
    cap = 1;
    e->vid = (int *)malloc(sizeof(int));
    e->sim = (float *)malloc(sizeof(float));
    e->vid[0] = (int)(intptr_t)*ex - 1;
    e->sim[0] = 1;
    e->nex = 1;
  } else if (t->kind == MT_WORD && t->len >= 4) {
    uint64_t qm = mt_mask(t->s, t->len);
    int maxd = t->len <= 5 ? 1 : t->len <= 9 ? 2 : 3;
    for (int v = 0; v < ix->nv && e->nex < 24; v++) {
      if (ix->vkind[v] != MT_WORD) continue;
      int lv = ix->vlen[v];
      if (lv < 4 || lv - t->len > maxd + 3 || t->len - lv > maxd + 3) continue;
      uint64_t vm = ix->vmask[v];
      if (mt_popcount(qm & ~vm) > maxd + 2 || mt_popcount(vm & ~qm) > maxd + 2) {
        /* обрезанное слово: запрос — начало словарного — проверим ниже */
        if (!(lv > t->len && !wmemcmp(ix->vstr[v], t->s, (size_t)t->len))) continue;
      }
      MtTok vt;
      memset(&vt, 0, sizeof(vt));
      vt.s = ix->vstr[v];
      vt.len = lv;
      vt.kind = MT_WORD;
      unsigned fl = 0;
      double sim = mt_tsim(t, &vt, &fl);
      if (sim < 0.7) continue;
      if (e->nex == cap) {
        cap = cap ? cap * 2 : 8;
        e->vid = (int *)realloc(e->vid, (size_t)cap * sizeof(int));
        e->sim = (float *)realloc(e->sim, (size_t)cap * sizeof(float));
      }
      e->vid[e->nex] = v;
      e->sim[e->nex++] = (float)sim;
    }
  }
  *s = e;
  return e;
}

typedef struct {
  int item;
  float acc;
} MtCand;

static int mt_cand_cmp(const void *a, const void *b) {
  const MtCand *x = (const MtCand *)a, *y = (const MtCand *)b;
  if (x->acc != y->acc) return x->acc < y->acc ? 1 : -1;
  return x->item - y->item;
}

int mt_find(MtIndex *ix, const wchar_t *des, const wchar_t *name, MtHit *out, int max) {
  if (!ix || !ix->acc || ix->n == 0 || max <= 0) return 0;
  wchar_t *pool = (wchar_t *)malloc((size_t)MT_MAXSRC * 4 * sizeof(wchar_t) + 4096);
  MtTok td[MT_MAXTOK], tn[MT_MAXTOK], ta[MT_MAXTOK * 2];
  MtDoc q;
  if (!pool) return 0;
  memset(&q, 0, sizeof(q));
  q.nd = mt_tokenize(ix->r, des, 1, td, MT_MAXTOK, pool);
  q.nn = mt_tokenize(ix->r, name, 0, tn, MT_MAXTOK, pool + MT_MAXSRC * 2);
  memcpy(ta, td, (size_t)q.nd * sizeof(MtTok));
  memcpy(ta + q.nd, tn, (size_t)q.nn * sizeof(MtTok));
  q.d = td;
  q.n = tn;
  q.all = ta;
  q.nall = q.nd + q.nn;
  wchar_t c[256];
  q.dlen = mt_compact(des, c, 256);
  q.dcomp = q.dlen ? mt_wdup(c, q.dlen) : NULL;
  wchar_t *j = mt_join(des, name);
  q.alen = j ? mt_compact(j, c, 256) : 0;
  free(j);
  q.acomp = q.alen ? mt_wdup(c, q.alen) : NULL;
  q.ntri = q.alen ? mt_trigrams(q.acomp, q.alen, &q.tri) : 0;

  int nt = 0;
  /* слова запроса от редких к частым */
  const MtTok *order[MT_MAXTOK * 2];
  float wgt[MT_MAXTOK * 2];
  int dfq[MT_MAXTOK * 2];
  for (int i = 0; i < q.nall; i++) {
    const MtTok *t = &ta[i];
    if (t->kind == MT_YEAR || t->kind == MT_STDW || (t->kind == MT_WORD && t->len < 3)) continue;
    void **ex = mt_map_get(&ix->vmap, t->s, t->len, 0);
    order[nt] = t;
    wgt[nt] = (float)mt_weight(t);
    dfq[nt] = ex && *ex ? ix->vdf[(int)(intptr_t)*ex - 1] : 0;
    nt++;
  }
  int ntouch = 0, used = 0;
  for (int pass = 0; pass < nt; pass++) {
    int bi = -1;
    for (int i = 0; i < nt; i++)
      if (order[i] && (bi < 0 || dfq[i] < dfq[bi])) bi = i;
    if (bi < 0) break;
    const MtTok *t = order[bi];
    float w = wgt[bi];
    order[bi] = NULL;
    MtExp *e = mt_expand(ix, t);
    if (!e) continue;
    for (int k = 0; k < e->nex; k++) {
      int v = e->vid[k];
      int a = ix->pstart[v], b = ix->pstart[v + 1];
      if (b - a > 5000 && used) continue;
      used = 1;
      for (int m = a; m < b; m++) {
        int it = ix->plist[m];
        if (ix->acc[it] == 0) ix->touched[ntouch++] = it;
        ix->acc[it] += w * e->sim[k];
      }
    }
  }
  if (q.dcomp) {
    void **s = mt_map_get(&ix->dmap, q.dcomp, q.dlen, 0);
    for (int it = s && *s ? (int)(intptr_t)*s - 1 : -1; it >= 0; it = ix->dnext[it]) {
      if (ix->acc[it] == 0) ix->touched[ntouch++] = it;
      ix->acc[it] += 100;
    }
  }
  if (q.acomp) {
    void **s = mt_map_get(&ix->amap, q.acomp, q.alen, 0);
    for (int it = s && *s ? (int)(intptr_t)*s - 1 : -1; it >= 0; it = ix->anext[it]) {
      if (ix->acc[it] == 0) ix->touched[ntouch++] = it;
      ix->acc[it] += 50;
    }
  }
  int nc = ntouch < MT_MAXCAND ? ntouch : MT_MAXCAND;
  MtCand *cand = (MtCand *)malloc((size_t)(ntouch + 1) * sizeof(MtCand));
  for (int i = 0; i < ntouch; i++) {
    cand[i].item = ix->touched[i];
    cand[i].acc = ix->acc[ix->touched[i]];
    ix->acc[ix->touched[i]] = 0;
  }
  if (ntouch > nc) qsort(cand, (size_t)ntouch, sizeof(MtCand), mt_cand_cmp);
  int nh = 0;
  double best[MT_MAX_HITS * 4];
  if (max > MT_MAX_HITS * 4) max = MT_MAX_HITS * 4;
  for (int i = 0; i < nc; i++) {
    MtRes r;
    const MtDoc *cd = &ix->doc[cand[i].item];
    mt_score(&q, cd, &r);
    /* при равной оценке — позиция покороче */
    double key = r.s - 0.0005 * (cd->nall > 20 ? 20 : cd->nall);
    int pos = nh;
    while (pos > 0 && best[pos - 1] < key) pos--;
    if (pos >= max) continue;
    if (nh < max) nh++;
    for (int m = nh - 1; m > pos; m--) {
      out[m] = out[m - 1];
      best[m] = best[m - 1];
    }
    out[pos].item = cand[i].item;
    out[pos].score = (int)(r.s * 100 + 0.5);
    out[pos].flags = r.fl;
    wcscpy(out[pos].miss, r.miss);
    best[pos] = key;
  }
  free(cand);
  free(q.dcomp);
  free(q.acomp);
  free(q.tri);
  free(pool);
  return nh;
}

void mt_note(const MtHit *h, wchar_t *out, int cap) {
  wchar_t b[200];
  b[0] = 0;
#define ADD(s)                                \
  do {                                        \
    if (b[0]) wcscat(b, L"; ");               \
    wcscat(b, s);                             \
  } while (0)
  if (h->flags & MTF_DES_EXACT) ADD(L"обозначение совпало");
  if (h->flags & MTF_DES_DIFF) ADD(L"обозначение другое");
  if ((h->flags & MTF_NAME_EXACT) && !(h->flags & MTF_DES_EXACT)) ADD(L"наименование совпало");
  if (h->flags & MTF_SIZE_DIFF) {
    wchar_t t[60] = L"числа не сошлись: ";
    wcscat(t, h->miss);
    wcscat(t, L" нет");
    ADD(t);
  }
  if (h->flags & MTF_STD_DIFF) ADD(L"другой стандарт");
  if (h->flags & MTF_TYPO) ADD(L"опечатка исправлена");
  if (h->flags & MTF_ABBR) ADD(L"сокращение");
  if ((h->flags & MTF_PARTIAL) && !(h->flags & MTF_SIZE_DIFF)) ADD(L"часть слов не найдена");
#undef ADD
  int l = (int)wcslen(b);
  if (l >= cap) l = cap - 1;
  wmemcpy(out, b, (size_t)l);
  out[l] = 0;
}

/* ---- точка расширения для локального ИИ (см. match.h): сейчас заглушка ------------ */
static MtAiRankFn g_mtAiRank;

void mt_set_ai_ranker(MtAiRankFn f) { g_mtAiRank = f; }

int mt_ai_wanted(const MtHit *h, int n, const MtRules *r) {
  int ex = mt_rules_threshold(r, MT_T_EXACT), lk = mt_rules_threshold(r, MT_T_LIKELY);
  if (n <= 0 || h[0].score >= ex) return 0;
  if (h[0].score < lk - 10) return 0;
  if (h[0].score < lk + 10) return 1;
  return n > 1 && h[0].score - h[1].score <= 6;
}

int mt_ai_rank_candidates(const wchar_t *des, const wchar_t *name, MtAiCand *cand, int n, int *order) {
  if (!g_mtAiRank || n <= 0) return 0;
  return g_mtAiRank(des, name, cand, n, order);
}
