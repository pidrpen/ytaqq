/* file_link.c — «ссылка с файлом»: сам файл внутри текста ссылки (с 2026.10.26).
   Чистая логика без Win32 — её проверяет file_link_test.c (в CI); кольцо Shift,
   буфер обмена и «Сохранить как» — в file_link_ui.c.

   Ссылка — «cursorpad:f1.» и base64 от блока (числа — младшим байтом вперёд):
     флаги (1) · длина блока (4) · размер файла (4) · CRC32 файла (4) ·
     длина имени (1) · имя UTF-8 · данные
   Флаг 1 — данные сжаты (LZ ниже). PDF, JPG, DOCX уже сжаты сами — такие
   лежат как есть, а TXT, CSV, BMP, несжатый TIFF становятся в разы короче.
   Длина блока нужна, чтобы обрезанную ссылку («пришло N из M знаков») не
   путать с испорченной. Пробелы, переносы строк и не-ASCII (мягкий перенос,
   пробел нулевой ширины) внутри ссылки пропускаются: почта и мессенджеры
   переносят длинное слово. Алфавит обычный, «+/», а не «-_»: «__» в
   мессенджерах становится курсивом и пропадает. */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FLK_PREFIX "cursorpad:f1."
#define FLK_PREFIX_N 13
#define FLK_MAX (10u * 1024u * 1024u) /* файл больше — ссылкой не отдаём */
#define FLK_HEAD 14                   /* блок без имени и данных */
#define FLK_PACKED 1

enum { FLK_OK, FLK_NONE, FLK_CUT, FLK_BAD, FLK_MEM };

typedef struct {
  char name[256];  /* как прислали, UTF-8; перед записью на диск — flk_safe_name */
  uint32_t size, crc, total;
  int packed;
  size_t chars, need; /* знаков ссылки нашлось / нужно */
} FlkInfo;

/* ---- CRC32 (как у ZIP) ----------------------------------------------------- */

static uint32_t flk_crc32(const unsigned char *p, size_t n) {
  static uint32_t table[256];
  static int ready;
  if (!ready) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = 1;
  }
  uint32_t c = 0xFFFFFFFFu;
  while (n--) c = table[(c ^ *p++) & 255] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

/* ---- сжатие ------------------------------------------------------------------

   LZ77 со словарём 64 КБ, раскладка как у LZ4: байт-жетон (старшие 4 бита —
   сколько литералов, младшие — длина совпадения − 4; 15 — «дальше байтами по
   255»), литералы, смещение (2 байта), продолжение длины совпадения. Последняя
   последовательность — только литералы: распаковщик знает размер файла.
   Совпадения ищутся по цепочке хэшей (до 32 кандидатов). */

#define FLK_MIN 4
#define FLK_WIN 65535
#define FLK_HBITS 16
#define FLK_DEPTH 32

static uint32_t flk_hash4(const unsigned char *p) {
  uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
  return (v * 2654435761u) >> (32 - FLK_HBITS);
}

/* длина «15 и дальше байтами»: v — сверх 15 */
static int flk_put_len(unsigned char *out, size_t *o, size_t cap, size_t v) {
  for (;;) {
    if (*o >= cap) return 0;
    if (v < 255) {
      out[(*o)++] = (unsigned char)v;
      return 1;
    }
    out[(*o)++] = 255;
    v -= 255;
  }
}

static int flk_put_seq(unsigned char *out, size_t *o, size_t cap, const unsigned char *lit, size_t nlit,
                       size_t off, size_t mlen) {
  size_t mcode = mlen ? mlen - FLK_MIN : 0;
  if (*o >= cap) return 0;
  out[(*o)++] = (unsigned char)((nlit < 15 ? nlit : 15) << 4 | (mcode < 15 ? mcode : 15));
  if (nlit >= 15 && !flk_put_len(out, o, cap, nlit - 15)) return 0;
  if (nlit > cap - *o) return 0;
  memcpy(out + *o, lit, nlit);
  *o += nlit;
  if (!mlen) return 1;
  if (cap - *o < 2) return 0;
  out[(*o)++] = (unsigned char)(off & 255);
  out[(*o)++] = (unsigned char)(off >> 8);
  return mcode < 15 || flk_put_len(out, o, cap, mcode - 15);
}

/* сжать src в out (не больше cap байт); 0 — не влезло (сжимать незачем) */
static size_t flk_lz_pack(const unsigned char *src, size_t n, unsigned char *out, size_t cap) {
  int32_t *head = (int32_t *)malloc(sizeof(int32_t) << FLK_HBITS);
  int32_t *prev = (int32_t *)malloc(sizeof(int32_t) * (FLK_WIN + 1));
  size_t o = 0, anchor = 0, i = 0;
  int ok = head && prev;
  if (ok) {
    memset(head, 0xFF, sizeof(int32_t) << FLK_HBITS);
    while (ok && n >= FLK_MIN && i + FLK_MIN <= n) {
      uint32_t h = flk_hash4(src + i);
      int32_t cand = head[h];
      size_t best = 0, bestAt = 0;
      for (int d = 0; d < FLK_DEPTH && cand >= 0 && i - (size_t)cand <= FLK_WIN; d++) {
        const unsigned char *a = src + cand, *b = src + i;
        size_t len = 0, most = n - i;
        while (len < most && a[len] == b[len]) len++;
        if (len > best) {
          best = len;
          bestAt = (size_t)cand;
          if (len == most) break;
        }
        int32_t next = prev[cand & FLK_WIN];
        if (next >= cand) break; /* ячейку уже занял более новый — цепочка кончилась */
        cand = next;
      }
      prev[i & FLK_WIN] = head[h];
      head[h] = (int32_t)i;
      if (best < FLK_MIN) {
        i++;
        continue;
      }
      ok = flk_put_seq(out, &o, cap, src + anchor, i - anchor, i - bestAt, best);
      /* позиции внутри совпадения — тоже в словарь: следующие совпадения длиннее */
      for (size_t k = i + 1; k < i + best && k + FLK_MIN <= n; k++) {
        uint32_t hk = flk_hash4(src + k);
        prev[k & FLK_WIN] = head[hk];
        head[hk] = (int32_t)k;
      }
      i += best;
      anchor = i;
    }
    if (ok && anchor < n) ok = flk_put_seq(out, &o, cap, src + anchor, n - anchor, 0, 0);
  }
  free(head);
  free(prev);
  return ok ? o : 0;
}

/* распаковать ровно dn байт; 0 — данные испорчены */
static int flk_lz_unpack(const unsigned char *src, size_t sn, unsigned char *dst, size_t dn) {
  size_t ip = 0, op = 0;
  while (ip < sn) {
    unsigned tok = src[ip++];
    size_t lit = tok >> 4, b;
    if (lit == 15) do {
        if (ip >= sn) return 0;
        b = src[ip++];
        lit += b;
      } while (b == 255);
    if (lit > sn - ip || lit > dn - op) return 0;
    memcpy(dst + op, src + ip, lit);
    ip += lit;
    op += lit;
    if (ip == sn) break; /* последняя — одни литералы */
    if (sn - ip < 2) return 0;
    size_t off = (size_t)src[ip] | (size_t)src[ip + 1] << 8, mlen = (tok & 15) + FLK_MIN;
    ip += 2;
    if ((tok & 15) == 15) do {
        if (ip >= sn) return 0;
        b = src[ip++];
        mlen += b;
      } while (b == 255);
    if (!off || off > op || mlen > dn - op) return 0;
    for (size_t k = 0; k < mlen; k++, op++) dst[op] = dst[op - off]; /* может перекрываться */
  }
  return op == dn;
}

/* ---- base64 ---------------------------------------------------------------- */

static const char kFlkB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* знак base64 → 0…63; -1 — пропустить (пробел, перенос, не-ASCII); -2 — конец ссылки */
static int flk_b64v(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62; /* «-_» — тоже: вдруг ссылку переписали под URL */
  if (c == '/' || c == '_') return 63;
  if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c >= 0x80) return -1;
  return -2;
}

static size_t flk_b64_chars(size_t bytes) { return bytes / 3 * 4 + (bytes % 3 ? bytes % 3 + 1 : 0); }

/* до want знаков ссылки с p (не дальше end) — в out (want*3/4 байт); вернуть, сколько нашлось */
static size_t flk_b64_read(const char *p, const char *end, size_t want, unsigned char *out) {
  size_t got = 0, o = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (; p < end && got < want; p++) {
    int v = flk_b64v((unsigned char)*p);
    if (v == -1) continue;
    if (v < 0) break;
    got++;
    if (!out) continue;
    acc = acc << 6 | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[o++] = (unsigned char)(acc >> bits);
    }
  }
  return got;
}

/* ---- имя файла ----------------------------------------------------------------- */

/* без папки и не длиннее 255 байт UTF-8; длинное укорачивается до расширения */
static void flk_fit_name(const char *in, char out[256]) {
  const char *base = in;
  for (const char *p = in; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;
  size_t n = strlen(base);
  if (n <= 255) {
    memcpy(out, base, n + 1);
    return;
  }
  const char *dot = strrchr(base, '.');
  size_t ext = dot && strlen(dot) <= 32 ? strlen(dot) : 0;
  size_t keep = 255 - ext;
  while (keep && ((unsigned char)base[keep] & 0xC0) == 0x80) keep--; /* не резать букву пополам */
  memcpy(out, base, keep);
  memcpy(out + keep, base + n - ext, ext);
  out[keep + ext] = 0;
}

/* имя из чужой ссылки — годное для Windows: без папок, запрещённых знаков,
   точек и пробелов на конце и без имён устройств (CON, NUL, COM1…) */
static void flk_safe_name(char *s) {
  char *base = s;
  for (char *p = s; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;
  if (base != s) memmove(s, base, strlen(base) + 1);
  for (char *p = s; *p; p++)
    if ((unsigned char)*p < 32 || strchr("<>:\"|?*", *p)) *p = '_';
  size_t n = strlen(s);
  while (n && (s[n - 1] == '.' || s[n - 1] == ' ')) s[--n] = 0;
  size_t lead = 0;
  while (s[lead] == ' ') lead++;
  if (lead) memmove(s, s + lead, strlen(s + lead) + 1);
  if (!s[0]) {
    strcpy(s, "файл");
    return;
  }
  static const char *const dev[] = {"CON", "PRN", "AUX", "NUL", "COM", "LPT"};
  size_t stem = strcspn(s, ".");
  for (int k = 0; k < 6; k++) {
    int digit = k >= 4;
    if (stem != 3u + digit) continue;
    int same = 1;
    for (int j = 0; j < 3; j++)
      if ((s[j] & ~0x20) != dev[k][j]) same = 0;
    if (same && (!digit || (s[3] >= '1' && s[3] <= '9'))) {
      if (strlen(s) + 2 > 256) s[254] = 0;
      memmove(s + 1, s, strlen(s) + 1);
      s[0] = '_';
      return;
    }
  }
}

/* ---- ссылка ------------------------------------------------------------------- */

static void flk_put32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}
static uint32_t flk_get32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* ссылка на файл (malloc, ASCII); NULL — больше FLK_MAX или нет памяти.
   *chars — длина ссылки в знаках */
static char *flk_make(const unsigned char *data, size_t n, const char *name, size_t *chars) {
  if (n > FLK_MAX) return NULL;
  char nm[256];
  flk_fit_name(name ? name : "", nm);
  size_t nl = strlen(nm), head = FLK_HEAD + nl;
  /* сжатое берём, только если вышло заметно короче (хотя бы на 3 %) */
  size_t cap = n - n / 32;
  unsigned char *blob = (unsigned char *)malloc(head + n + 1);
  if (!blob) return NULL;
  size_t packed = n >= 64 ? flk_lz_pack(data, n, blob + head, cap) : 0;
  if (!packed && n) memcpy(blob + head, data, n);
  size_t total = head + (packed ? packed : n);
  blob[0] = packed ? FLK_PACKED : 0;
  flk_put32(blob + 1, (uint32_t)total);
  flk_put32(blob + 5, (uint32_t)n);
  flk_put32(blob + 9, flk_crc32(data, n));
  blob[13] = (unsigned char)nl;
  memcpy(blob + FLK_HEAD, nm, nl);
  size_t len = FLK_PREFIX_N + flk_b64_chars(total);
  char *out = (char *)malloc(len + 1);
  if (out) {
    memcpy(out, FLK_PREFIX, FLK_PREFIX_N);
    char *o = out + FLK_PREFIX_N;
    size_t i = 0;
    for (; i + 3 <= total; i += 3) {
      uint32_t v = (uint32_t)blob[i] << 16 | (uint32_t)blob[i + 1] << 8 | blob[i + 2];
      *o++ = kFlkB64[v >> 18], *o++ = kFlkB64[v >> 12 & 63], *o++ = kFlkB64[v >> 6 & 63], *o++ = kFlkB64[v & 63];
    }
    if (total - i == 1) {
      *o++ = kFlkB64[blob[i] >> 2], *o++ = kFlkB64[(blob[i] & 3) << 4];
    } else if (total - i == 2) {
      uint32_t v = (uint32_t)blob[i] << 8 | blob[i + 1];
      *o++ = kFlkB64[v >> 10], *o++ = kFlkB64[v >> 4 & 63], *o++ = kFlkB64[(v & 15) << 2];
    }
    *o = 0;
    if (chars) *chars = len;
  }
  free(blob);
  return out;
}

/* начало ссылки в тексте (регистр приставки не важен) или NULL */
static const char *flk_find(const char *text, size_t n) {
  for (size_t i = 0; i + FLK_PREFIX_N <= n; i++) {
    size_t k = 0;
    while (k < FLK_PREFIX_N && (text[i + k] | (FLK_PREFIX[k] >= 'a' ? 0x20 : 0)) == FLK_PREFIX[k]) k++;
    if (k == FLK_PREFIX_N) return text + i;
  }
  return NULL;
}

/* что в ссылке: имя, размер, хватает ли знаков. FLK_CUT — обрезана (имя, если
   дошло, уже в info); FLK_BAD — не наша или испорчена */
static int flk_peek(const char *text, size_t n, FlkInfo *info) {
  memset(info, 0, sizeof(*info));
  const char *s = flk_find(text, n), *end = text + n;
  if (!s) return FLK_NONE;
  s += FLK_PREFIX_N;
  unsigned char h[FLK_HEAD + 255 + 3];
  size_t got = flk_b64_read(s, end, 8, h);
  if (got < 8) {
    info->chars = got;
    return FLK_CUT;
  }
  info->packed = h[0] & FLK_PACKED;
  info->total = flk_get32(h + 1);
  if ((h[0] & ~FLK_PACKED) || info->total < FLK_HEAD || info->total > FLK_HEAD + 255 + FLK_MAX)
    return FLK_BAD;
  info->need = flk_b64_chars(info->total);
  info->chars = flk_b64_read(s, end, info->need, NULL);
  size_t headChars = flk_b64_chars(FLK_HEAD);
  if (info->chars >= headChars) {
    flk_b64_read(s, end, headChars, h);
    size_t nl = h[13];
    info->size = flk_get32(h + 5);
    info->crc = flk_get32(h + 9);
    if (FLK_HEAD + nl > info->total || info->size > FLK_MAX ||
        (!info->packed && info->total != FLK_HEAD + nl + info->size))
      return FLK_BAD;
    size_t nameChars = flk_b64_chars(FLK_HEAD + nl);
    if (info->chars >= nameChars) {
      flk_b64_read(s, end, nameChars, h);
      memcpy(info->name, h + FLK_HEAD, nl);
      info->name[nl] = 0;
    }
  }
  return info->chars < info->need ? FLK_CUT : FLK_OK;
}

/* файл из ссылки: *data — malloc на info->size байт (хотя бы 1) */
static int flk_open(const char *text, size_t n, FlkInfo *info, unsigned char **data) {
  *data = NULL;
  int st = flk_peek(text, n, info);
  if (st != FLK_OK) return st;
  const char *s = flk_find(text, n) + FLK_PREFIX_N;
  unsigned char *blob = (unsigned char *)malloc(info->total + 3);
  unsigned char *out = (unsigned char *)malloc(info->size ? info->size : 1);
  if (!blob || !out) {
    free(blob);
    free(out);
    return FLK_MEM;
  }
  flk_b64_read(s, text + n, info->need, blob);
  size_t at = FLK_HEAD + blob[13], body = info->total - at;
  int ok = info->packed ? flk_lz_unpack(blob + at, body, out, info->size) : body == info->size;
  if (ok && !info->packed) memcpy(out, blob + at, body);
  free(blob);
  if (!ok || flk_crc32(out, info->size) != info->crc) {
    free(out);
    return FLK_BAD;
  }
  *data = out;
  return FLK_OK;
}
