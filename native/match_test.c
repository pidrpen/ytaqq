/* Проверка движка match.c без Windows: gcc -O2 -Wall -Werror -o match_test match_test.c
   ./match_test правила.txt союз.tsv запросы.tsv — столбцы: обозначение<TAB>наименование */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "match.c"

static wchar_t *u8(const char *s, size_t n) {
  wchar_t *w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
  size_t o = 0;
  for (size_t i = 0; i < n;) {
    unsigned char c = (unsigned char)s[i];
    unsigned cp;
    int l = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    cp = l == 1 ? c : l == 2 ? c & 0x1F : l == 3 ? c & 0x0F : c & 7;
    for (int k = 1; k < l && i + k < n; k++) cp = cp << 6 | ((unsigned char)s[i + k] & 0x3F);
    w[o++] = (wchar_t)cp;
    i += l;
  }
  w[o] = 0;
  return w;
}
static char *slurp(const char *p, size_t *n) {
  FILE *f = fopen(p, "rb");
  if (!f) { perror(p); exit(1); }
  fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  char *b = (char *)malloc(*n + 1);
  if (fread(b, 1, *n, f) != *n) exit(1);
  fclose(f);
  return b;
}
static void out8(const wchar_t *w) {
  for (; *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}
typedef struct { wchar_t *a, *b; } Row;
static int rows(const char *path, Row **out) {
  size_t n; char *t = slurp(path, &n);
  int cnt = 0; Row *r = (Row *)calloc(200000, sizeof(Row));
  char *p = t, *end = t + n;
  while (p < end) {
    char *e = memchr(p, '\n', (size_t)(end - p)); if (!e) e = end;
    char *l = e; if (l > p && l[-1] == '\r') l--;
    char *tab = memchr(p, '\t', (size_t)(l - p));
    if (l > p) {
      r[cnt].a = u8(p, (size_t)((tab ? tab : l) - p));
      r[cnt].b = tab ? u8(tab + 1, (size_t)(l - tab - 1)) : u8("", 0);
      cnt++;
    }
    p = e + 1;
  }
  *out = r; return cnt;
}
int main(int argc, char **argv) {
  if (argc < 4) return 2;
  size_t n; char *rt = slurp(argv[1], &n);
  wchar_t *rw = u8(rt, n);
  MtRules *r = mt_rules_parse(rw);
  Row *S, *Q; int ns = rows(argv[2], &S), nq = rows(argv[3], &Q);
  MtIndex *ix = mt_index_new(r);
  for (int i = 0; i < ns; i++) mt_index_add(ix, S[i].a, S[i].b);
  mt_index_build(ix);
  for (int i = 0; i < nq; i++) {
    MtHit h[MT_MAX_HITS];
    int k = mt_find(ix, Q[i].a, Q[i].b, h, MT_MAX_HITS);
    out8(Q[i].a); printf(" | "); out8(Q[i].b); printf("\n");
    for (int j = 0; j < k && j < (argc > 4 ? 5 : 2); j++) {
      wchar_t note[100]; mt_note(&h[j], note, 100);
      printf("   %3d%%  ", h[j].score); out8(S[h[j].item].a); printf(" | "); out8(S[h[j].item].b);
      printf("   ["); out8(note); printf("]\n");
    }
    if (!k) printf("   —\n");
  }
  return 0;
}
