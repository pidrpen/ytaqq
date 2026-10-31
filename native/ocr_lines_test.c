/* Проверка отсева мусорных строк ocr_lines.h без Windows:
   gcc -O2 -Wall -Werror -o ocr_lines_test ocr_lines_test.c && ./ocr_lines_test
   Строки TSV выдуманы: так Tesseract видит тёмный плейлист с обложками и значками ⓘ. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ocr_lines.h"

static int g_fail, g_n;

/* TSV по кусочкам: слова одной строки идут подряд, строка — новый номер line */
typedef struct {
  char buf[8192];
  int len;
} Tsv;

static void tsv_start(Tsv *t) {
  t->len = snprintf(t->buf, sizeof(t->buf), "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\t"
                                              "left\ttop\twidth\theight\tconf\ttext\r\n"
                                              "1\t1\t0\t0\t0\t0\t0\t0\t660\t574\t-1\t\r\n"
                                              "2\t1\t1\t0\t0\t0\t16\t12\t630\t540\t-1\t\r\n");
}

/* слово с рамкой: left — слева, ширина 40, высота height */
static void tsv_wordh(Tsv *t, int par, int line, int word, double conf, const char *text, int left, int height) {
  t->len += snprintf(t->buf + t->len, sizeof(t->buf) - (size_t)t->len,
                     "5\t1\t1\t%d\t%d\t%d\t%d\t%d\t40\t%d\t%.6f\t%s\r\n", par, line, word, left, 12 + line * 30,
                     height, conf, text);
}

static void tsv_wordx(Tsv *t, int par, int line, int word, double conf, const char *text, int left) {
  tsv_wordh(t, par, line, word, conf, text, left, 20);
}

/* слова одной строки стоят вплотную: пустота между ними 10 пикселей при высоте 20 */
static void tsv_word(Tsv *t, int par, int line, int word, double conf, const char *text) {
  tsv_wordx(t, par, line, word, conf, text, 16 + word * 50);
}

static void check(const char *name, const Tsv *t, const char *want) {
  char out[8192];
  int n = ocl_filter(t->buf, (size_t)t->len, out, sizeof(out));
  g_n++;
  if (!want) {
    if (n == -1) return;
    g_fail++;
    printf("FAIL %s: ждали -1, вышло %d\n", name, n);
    return;
  }
  if (n >= 0 && (size_t)n == strlen(want) && !memcmp(out, want, (size_t)n)) return;
  g_fail++;
  printf("FAIL %s: вышло %d байт «%.*s», ждали «%s»\n", name, n, n > 0 ? n : 0, out, want);
}

int main(void) {
  Tsv t;

  /* плейлист: заголовок, три строки с обложками слева и ⓘ справа, мусорные строки между ними */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 94.7, "Плейлист:");
  tsv_word(&t, 1, 1, 2, 95.5, "24");
  tsv_word(&t, 1, 1, 3, 95.5, "трека");
  tsv_word(&t, 1, 2, 1, 13.2, "2");                   /* кусок обложки слева */
  tsv_word(&t, 1, 2, 2, 90.4, "mlvine");
  tsv_word(&t, 1, 2, 3, 92.2, "\xE2\x80\x94");         /* тире — в середине строки остаётся */
  tsv_word(&t, 1, 2, 4, 95.8, "Midnight");
  tsv_word(&t, 1, 2, 5, 96.6, "Drive");
  tsv_word(&t, 1, 2, 6, 68.7, "\xC2\xA9");             /* ⓘ, прочитанный как «©» */
  tsv_word(&t, 1, 3, 1, 0.0, "|");                    /* обложка целиком: одни знаки */
  tsv_word(&t, 1, 3, 2, 35.8, "rx");
  tsv_word(&t, 1, 3, 3, 68.7, "\xC2\xA9");
  tsv_word(&t, 1, 4, 1, 20.0, "\xE2\x80\x9A\xC2\xA9.");  /* «‚©.» */
  tsv_word(&t, 1, 4, 2, 25.0, "e=");
  tsv_word(&t, 1, 5, 1, 10.0, "\xC2\xB0");             /* «°» */
  tsv_word(&t, 1, 6, 1, 25.0, "Oe");                  /* две буквы, но уверенности нет */
  tsv_word(&t, 1, 6, 2, 15.0, "\xE2\x80\x9A");
  tsv_word(&t, 1, 7, 1, 59.0, "\xD0\x9C\xD0\xA0\xD0\x97"); /* «МРЗ» русскими буквами */
  tsv_word(&t, 1, 7, 2, 66.4, "3:41");
  tsv_word(&t, 1, 8, 1, 37.8, "a");                   /* кусок обложки */
  tsv_word(&t, 1, 8, 2, 96.3, "\xD0\x9B\xD1\x83\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB9");
  tsv_word(&t, 1, 8, 3, 93.2, "\xD0\x9F\xD0\xB0\xD1\x80\xD0\xBA");
  tsv_word(&t, 1, 8, 4, 80.8, "\xC2\xA9");
  check("плейлист", &t,
        "Плейлист: 24 трека\n"
        "mlvine \xE2\x80\x94 Midnight Drive\n"
        "MP3 3:41\n"
        "Лунный Парк");

  /* обычный текст: ничего не выбрасывается, пустая строка между абзацами */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 96.0, "Итого:");
  tsv_word(&t, 1, 1, 2, 91.0, "8");
  tsv_word(&t, 1, 1, 3, 90.0, "треков,");
  tsv_word(&t, 1, 1, 4, 88.0, "28");
  tsv_word(&t, 1, 1, 5, 87.0, "минут");
  tsv_word(&t, 2, 1, 1, 70.0, "Hollow");
  tsv_word(&t, 2, 1, 2, 65.0, "Pines");
  tsv_word(&t, 2, 1, 3, 62.0, "\xE2\x80\x94");
  tsv_word(&t, 2, 1, 4, 60.0, "Winter");
  tsv_word(&t, 2, 1, 5, 61.0, "Lights");
  check("обычный текст", &t, "Итого: 8 треков, 28 минут\n\nHollow Pines \xE2\x80\x94 Winter Lights");

  /* нормальная строка, где ⓘ прочитан одинокой буквой или «(i)» */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 95.0, "Nova");
  tsv_word(&t, 1, 1, 2, 94.0, "Kite");
  tsv_word(&t, 1, 1, 3, 93.0, "Glass");
  tsv_word(&t, 1, 1, 4, 80.0, "O");
  tsv_word(&t, 1, 2, 1, 96.0, "Echo");
  tsv_word(&t, 1, 2, 2, 96.0, "Harbor");
  tsv_word(&t, 1, 2, 3, 70.0, "(i)");
  check("значок буквой", &t, "Nova Kite Glass\nEcho Harbor");

  /* настоящий © и тире в начале строки остаются, одинокие цифры — тоже */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 93.0, "\xC2\xA9");
  tsv_word(&t, 1, 1, 2, 95.0, "2026");
  tsv_word(&t, 1, 1, 3, 95.0, "Контора");
  tsv_word(&t, 1, 2, 1, 80.0, "\xE2\x80\x94");
  tsv_word(&t, 1, 2, 2, 91.0, "Привет!");
  tsv_word(&t, 1, 3, 1, 92.0, "12");
  tsv_word(&t, 1, 4, 1, 55.0, "7");                  /* цифра с плохой уверенностью — мусор */
  check("настоящие знаки", &t, "\xC2\xA9 2026 Контора\n\xE2\x80\x94 Привет!\n12");

  /* значок ⓘ далеко справа, с уверенностью выше 85, — обрывок; рядом с текстом тот же «©» остаётся */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 96.0, "MP3");
  tsv_word(&t, 1, 1, 2, 90.0, "3:41");
  tsv_wordx(&t, 1, 1, 3, 88.0, "\xC2\xA9", 1830);
  tsv_word(&t, 1, 2, 1, 96.0, "Лунный");
  tsv_word(&t, 1, 2, 2, 90.0, "Парк");
  tsv_wordx(&t, 1, 2, 3, 91.0, "\xC2\xA9", 1830);       /* далеко — убирается и при высокой уверенности */
  tsv_word(&t, 1, 3, 1, 96.0, "Лунный");
  tsv_word(&t, 1, 3, 2, 91.0, "\xC2\xA9");              /* рядом и уверенно — настоящий знак */
  check("значок далеко", &t, "MP3 3:41\nЛунный Парк\nЛунный \xC2\xA9");

  /* кусок обложки слева: одинокая буква, цифра и тире перед названием */
  tsv_start(&t);
  tsv_wordx(&t, 1, 1, 1, 93.0, "-", 48);
  tsv_wordx(&t, 1, 1, 2, 39.0, "7", 114);
  tsv_wordx(&t, 1, 1, 3, 96.0, "Лунный", 240);
  tsv_wordx(&t, 1, 1, 4, 93.0, "Парк", 300);
  tsv_wordx(&t, 1, 2, 1, 75.0, "\xD0\xB3", 54);         /* «г» далеко от названия */
  tsv_wordx(&t, 1, 2, 2, 92.0, "МРЗ", 243);
  tsv_wordx(&t, 1, 2, 3, 94.0, "3:15", 303);
  tsv_word(&t, 1, 3, 1, 75.0, "\xD0\xB2");              /* «в» вплотную к слову — это слово */
  tsv_word(&t, 1, 3, 2, 96.0, "начале");
  tsv_word(&t, 1, 4, 1, 80.0, "\xE2\x84\x96");          /* «№» */
  tsv_word(&t, 1, 4, 2, 95.0, "Наименование");
  tsv_wordh(&t, 1, 5, 1, 72.0, "\xD0\xB0", 66, 42);      /* «а» вдвое выше букв строки, хоть и рядом */
  tsv_wordx(&t, 1, 5, 2, 97.0, "Лунный", 116);
  tsv_wordx(&t, 1, 5, 3, 93.0, "Парк", 166);
  check("обложка слева", &t, "Лунный Парк\nMP3 3:15\nв начале\n\xE2\x84\x96 Наименование\nЛунный Парк");

  /* низкая уверенность на всей строке: выбрасывается; средняя — остаётся */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 30.0, "Berlin");
  tsv_word(&t, 1, 1, 2, 35.0, "Sector");
  tsv_word(&t, 1, 2, 1, 62.0, "Noisy");
  tsv_word(&t, 1, 2, 2, 58.0, "screen");
  check("уверенность", &t, "Noisy screen");

  /* знаков больше, чем букв и цифр, а уверенности нет */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 70.0, "ab");
  tsv_word(&t, 1, 1, 2, 55.0, "#$%&*");
  tsv_word(&t, 1, 1, 3, 70.0, "cd");
  check("одни знаки", &t, "");

  /* код: скобки и «;» читаются уверенно — строки целы; «©» и «|» одни на строке — мусор */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 95.0, "for");
  tsv_word(&t, 1, 1, 2, 94.0, "(int");
  tsv_word(&t, 1, 1, 3, 96.0, "i");
  tsv_word(&t, 1, 1, 4, 0.0, "=");                     /* Tesseract в паре слов не уверен, строка настоящая */
  tsv_word(&t, 1, 1, 5, 0.0, "0;");
  tsv_word(&t, 1, 1, 6, 90.0, "$[1];");
  tsv_word(&t, 1, 1, 7, 90.0, "1++)");
  tsv_word(&t, 1, 1, 8, 92.0, "{");
  tsv_word(&t, 1, 2, 1, 93.0, "if");
  tsv_word(&t, 1, 2, 2, 83.0, "(s[i]");
  tsv_word(&t, 1, 2, 3, 78.0, "==");
  tsv_word(&t, 1, 2, 4, 72.0, "'')");
  tsv_word(&t, 1, 2, 5, 75.0, "n++;");
  tsv_word(&t, 1, 3, 1, 90.0, "x");
  tsv_word(&t, 1, 3, 2, 90.0, "=");
  tsv_word(&t, 1, 3, 3, 90.0, "5;");
  tsv_word(&t, 1, 4, 1, 93.0, "}");
  tsv_word(&t, 1, 5, 1, 93.0, "\xC2\xA9");
  tsv_word(&t, 1, 6, 1, 60.0, "|");
  check("код", &t, "for (int i = 0; $[1]; 1++) {\nif (s[i] == '') n++;\nx = 5;\n}");

  /* имена форматов */
  tsv_start(&t);
  tsv_word(&t, 1, 1, 1, 80.0, "\xD0\xA0\x44\x46");     /* «РDF», русская Р */
  tsv_word(&t, 1, 1, 2, 80.0, "файл");
  tsv_word(&t, 1, 2, 1, 80.0, "\xD0\xBC\xD1\x80\xD0\xB7"); /* «мрз» строчными */
  tsv_word(&t, 1, 2, 2, 80.0, "\xD0\xA1\xD0\xB0\xD0\xBA\xD1\x81");   /* «Сакс» — не формат */
  tsv_word(&t, 1, 3, 1, 90.0, "MP3");
  tsv_word(&t, 1, 3, 2, 90.0, "\xD0\x92\xD0\x9C\xD0\xA0");   /* «ВМР» */
  check("форматы", &t, "PDF файл\nmp3 Сакс\nMP3 BMP");

  /* Windows-переводы строк и заголовок без слов */
  tsv_start(&t);
  check("пустая страница", &t, "");

  /* не TSV: обычный текст, пустой вход — запасной путь */
  {
    Tsv x;
    x.len = snprintf(x.buf, sizeof(x.buf), "Плейлист: 24 трека\nmlvine\n");
    check("не TSV", &x, NULL);
    x.len = 0;
    x.buf[0] = 0;
    check("пусто", &x, NULL);
  }

  /* буфер мал — не верим результату */
  {
    Tsv x;
    char small[8];
    tsv_start(&x);
    tsv_word(&x, 1, 1, 1, 95.0, "Плейлист:");
    tsv_word(&x, 1, 1, 2, 95.0, "очень");
    tsv_word(&x, 1, 1, 3, 95.0, "длинный");
    g_n++;
    if (ocl_filter(x.buf, (size_t)x.len, small, sizeof(small)) != -1) {
      g_fail++;
      printf("FAIL малый буфер\n");
    }
  }

  printf("%d проверок, ошибок: %d\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
