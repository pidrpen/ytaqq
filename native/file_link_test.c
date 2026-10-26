/* file_link_test.c — «ссылка с файлом» (file_link.c): файл туда и обратно байт в байт,
   сжатие, обрезанная и испорченная ссылка, имя файла. Windows не нужен:
     gcc -O2 -Wall -Werror -o file_link_test file_link_test.c && ./file_link_test */
#include <stdio.h>
#include "file_link.c"

static int g_fail, g_n;

static void check(int ok, const char *what) {
  g_n++;
  if (!ok) {
    g_fail++;
    printf("FAIL: %s\n", what);
  }
}

static unsigned g_seed = 12345;
static unsigned rnd(void) {
  g_seed = g_seed * 1103515245u + 12345u;
  return g_seed >> 16;
}

/* туда и обратно; вернуть длину ссылки */
static size_t roundtrip(const unsigned char *data, size_t n, const char *name, const char *what) {
  size_t chars = 0;
  char *link = flk_make(data, n, name, &chars);
  check(link != NULL, what);
  if (!link) return 0;
  check(strlen(link) == chars && !strncmp(link, FLK_PREFIX, FLK_PREFIX_N), what);
  FlkInfo info;
  unsigned char *back = NULL;
  int st = flk_open(link, chars, &info, &back);
  check(st == FLK_OK && info.size == n && back && (!n || !memcmp(back, data, n)), what);
  free(back);
  free(link);
  return chars;
}

static void test_lz_raw(void) {
  /* сжатие отдельно: повторы, перекрытие, длинные литералы и совпадения */
  static unsigned char src[300000], pack[300000], back[300000];
  for (size_t i = 0; i < sizeof(src); i++) src[i] = (unsigned char)(i < 1000 ? rnd() : i < 70000 ? 'a' : src[i - 997] ^ (i % 5000 == 0));
  size_t pn = flk_lz_pack(src, sizeof(src), pack, sizeof(pack));
  check(pn > 0 && pn < sizeof(src) / 10, "lz: повторы сжимаются");
  check(flk_lz_unpack(pack, pn, back, sizeof(src)) && !memcmp(src, back, sizeof(src)), "lz: распаковка");
  check(!flk_lz_unpack(pack, pn, back, sizeof(src) - 1), "lz: размер не тот — отказ");
  check(!flk_lz_unpack(pack, pn - 1, back, sizeof(src)), "lz: обрезано — отказ");
  /* мусор вместо сжатых данных: не падает и не пишет за край */
  for (int t = 0; t < 2000; t++) {
    unsigned char junk[64];
    for (int k = 0; k < 64; k++) junk[k] = (unsigned char)rnd();
    flk_lz_unpack(junk, (size_t)(rnd() % 64), back, (size_t)(rnd() % 512));
  }
  check(1, "lz: мусор");
}

int main(void) {
  check(flk_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u, "crc32");
  test_lz_raw();

  /* пустой файл, крошечный, «любого формата» — случайные байты не сжимаются */
  roundtrip((const unsigned char *)"", 0, "пусто.txt", "пустой файл");
  roundtrip((const unsigned char *)"x", 1, "a.bin", "1 байт");
  roundtrip((const unsigned char *)"ab", 2, "a.bin", "2 байта");
  roundtrip((const unsigned char *)"abc", 3, "a.bin", "3 байта");
  static unsigned char noise[200001];
  for (size_t i = 0; i < sizeof(noise); i++) noise[i] = (unsigned char)rnd();
  size_t c = roundtrip(noise, sizeof(noise), "скан.jpg", "случайные байты");
  check(c == FLK_PREFIX_N + flk_b64_chars(FLK_HEAD + strlen("скан.jpg") + sizeof(noise)), "несжимаемое лежит как есть");
  for (size_t n = 60; n < 70; n++) roundtrip(noise, n, "x", "граница сжатия");

  /* текст и таблица сжимаются в разы */
  static char csv[400000];
  size_t used = 0;
  for (int r = 0; used + 200 < sizeof(csv); r++)
    used += (size_t)snprintf(csv + used, sizeof(csv) - used, "%d;АДЕ 3424-%03d.01;Кронштейн;Ст3;%d,%d кг\r\n", r, r % 900,
                             r % 17, r % 10);
  c = roundtrip((const unsigned char *)csv, used, "Ведомость.csv", "CSV");
  check(c < used / 2, "CSV в ссылке короче файла");

  /* имя: русское, длинное, с папкой */
  {
    char longName[600] = "", fit[256];
    for (int i = 0; i < 150; i++) strcat(longName, "ж");
    strcat(longName, ".pdf");
    flk_fit_name(longName, fit);
    check(strlen(fit) <= 255 && !strcmp(fit + strlen(fit) - 4, ".pdf") && strlen(fit) % 2 == 0, "длинное имя: укорочено по букве, расширение цело");
    flk_fit_name("C:\\Сканы\\лист 1.tif", fit);
    check(!strcmp(fit, "лист 1.tif"), "имя без папки");
    size_t chars;
    char *link = flk_make((const unsigned char *)"data", 4, longName, &chars);
    FlkInfo info;
    check(link && flk_peek(link, chars, &info) == FLK_OK, "peek");
    flk_fit_name(longName, fit);
    check(link && !strcmp(info.name, fit) && info.size == 4, "имя и размер из ссылки");
    free(link);
  }

  /* ссылка в тексте сообщения, с переносами строк и мусором вокруг */
  {
    size_t chars;
    char *link = flk_make((const unsigned char *)csv, 5000, "часть.csv", &chars);
    char *msg = (char *)malloc(chars * 2 + 200);
    size_t m = (size_t)sprintf(msg, "Иван, 11:08\r\nЛови файл: ");
    for (size_t i = 0; i < chars; i++) {
      msg[m++] = link[i];
      if (i > 20 && i % 76 == 0) msg[m++] = '\n';               /* почта переносит */
      if (i > FLK_PREFIX_N && i % 333 == 0) msg[m++] = (char)0xC2, msg[m++] = (char)0xAD; /* мягкий перенос */
    }
    m += (size_t)sprintf(msg + m, "\r\nspasibo zaranee");
    FlkInfo info;
    unsigned char *back = NULL;
    int st = flk_open(msg, m, &info, &back);
    check(st == FLK_OK && info.size == 5000 && !memcmp(back, csv, 5000) && !strcmp(info.name, "часть.csv"), "ссылка внутри сообщения");
    free(back);

    /* обрезанная: сколько пришло из скольких, имя уже видно */
    st = flk_peek(link, chars - 100, &info);
    check(st == FLK_CUT && info.need == chars - FLK_PREFIX_N && info.chars == info.need - 100 && !strcmp(info.name, "часть.csv"),
          "обрезанная ссылка");
    check(flk_open(link, chars - 1, &info, &back) == FLK_CUT && !back, "обрезанная не открывается");
    check(flk_peek(link, FLK_PREFIX_N + 3, &info) == FLK_CUT, "от ссылки одно начало");

    /* испорченный знак — контрольная сумма */
    char *bad = (char *)malloc(chars + 1);
    memcpy(bad, link, chars + 1);
    bad[chars / 2] = bad[chars / 2] == 'A' ? 'B' : 'A';
    st = flk_open(bad, chars, &info, &back);
    check(st == FLK_BAD && !back, "испорченная ссылка");
    /* приставка большими буквами — тоже наша */
    memcpy(bad, link, chars + 1);
    memcpy(bad, "CursorPad:F1.", FLK_PREFIX_N);
    st = flk_open(bad, chars, &info, &back);
    check(st == FLK_OK, "регистр приставки");
    free(back);
    free(bad);
    free(msg);
    free(link);
  }
  {
    FlkInfo info;
    check(flk_peek("просто текст", strlen("просто текст"), &info) == FLK_NONE, "нет ссылки");
    check(flk_peek("cursorpad:f1.////////////", 25, &info) == FLK_BAD, "чужие флаги");
    check(!flk_make(noise, FLK_MAX + 1, "big.bin", NULL), "больше предела — нет ссылки");
  }

  /* испорченные ссылки наугад: отказ, а не падение (проверять с -fsanitize=address) */
  {
    size_t chars;
    char *link = flk_make((const unsigned char *)csv, 3000, "x.csv", &chars);
    char *bad = (char *)malloc(chars + 1);
    int refused = 0;
    for (int t = 0; t < 3000; t++) {
      memcpy(bad, link, chars + 1);
      for (int k = 0; k < 1 + t % 4; k++) bad[FLK_PREFIX_N + rnd() % (chars - FLK_PREFIX_N)] = kFlkB64[rnd() % 64];
      FlkInfo info;
      unsigned char *back = NULL;
      int st = flk_open(bad, chars - (size_t)(t % 7 == 0 ? rnd() % 40 : 0), &info, &back);
      if (st != FLK_OK) refused++;
      else if (memcmp(back, csv, 3000)) check(0, "испорченная открылась с другим содержимым");
      free(back);
    }
    check(refused > 2900, "испорченные отвергаются");
    free(bad);
    free(link);
  }

  /* имя из чужой ссылки — годное для Windows */
  {
    struct { const char *in, *out; } k[] = {{"..\\..\\Windows\\win.ini", "win.ini"}, {"a/b/c.txt", "c.txt"},
                                             {"что?:*.pdf", "что___.pdf"},          {"отчёт. . ", "отчёт"},
                                             {"CON", "_CON"},                       {"nul.txt", "_nul.txt"},
                                             {"com1.pdf", "_com1.pdf"},             {"COM0.pdf", "COM0.pdf"},
                                             {"CONSOLE.txt", "CONSOLE.txt"},        {"...", "файл"},
                                             {"  лист.tif", "лист.tif"},           {"a\tb\x01.txt", "a_b_.txt"}};
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
      char s[256];
      strcpy(s, k[i].in);
      flk_safe_name(s);
      if (strcmp(s, k[i].out)) printf("  «%s» → «%s», ждали «%s»\n", k[i].in, s, k[i].out);
      check(!strcmp(s, k[i].out), "безопасное имя");
    }
  }

  /* большой файл у предела: и сжимаемый, и нет */
  {
    unsigned char *big = (unsigned char *)malloc(FLK_MAX);
    for (size_t i = 0; i < FLK_MAX; i++) big[i] = (unsigned char)(i % 4096 < 2048 ? rnd() : "чертёж"[i % 6]);
    size_t chars = roundtrip(big, FLK_MAX, "предел.bin", "10 МБ");
    printf("10 МБ (половина сжимается) → %zu знаков\n", chars);
    free(big);
  }
  printf("%s: %d проверок, %d не прошло\n", g_fail ? "FAIL" : "OK", g_n, g_fail);
  return g_fail != 0;
}
