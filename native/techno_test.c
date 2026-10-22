/* Проверка правил techno.c без Windows:
   gcc -O2 -Wall -Werror -o techno_test techno_test.c && ./techno_test   (-v — показать маршруты)
   Тексты — как их отдаёт распознавание листа: со штампом и без, с ошибками
   Tesseract на курсиве ГОСТ 2.304 («Ra 08», «Mi2-6H», «2 отб.», «руг»). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "techno.c"

static void out8(const wchar_t *w) {
  for (; *w; w++) {
    unsigned c = (unsigned)*w;
    if (c < 0x80) putchar((int)c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}

static int g_fail, g_n, g_verbose;

/* ops — названия операций по порядку через «|»; no — каких быть не должно */
static void check(const char *title, const wchar_t *name, const wchar_t *mat, const wchar_t *text, int cls,
                  const wchar_t *blank, const wchar_t *grade, const wchar_t *ops, const wchar_t *no,
                  const wchar_t *check) {
  TnRoute r;
  tn_route(name, mat, text, &r);
  g_n++;
  wchar_t got[600] = {0};
  for (int i = 0; i < r.n; i++) tn_cat(got, 600, i ? L"|" : L""), tn_cat(got, 600, r.op[i].op);
  int ok = r.cls == cls && (!blank || wcsstr(r.blank, blank)) && (!grade || !wcscmp(r.s.grade, grade)) &&
           !wcscmp(got, ops) && (!check || wcsstr(r.check, check));
  for (const wchar_t *p = no; ok && p && *p;) {
    const wchar_t *e = wcschr(p, L'|');
    wchar_t one[48] = {0};
    wcsncpy(one, p, e ? (size_t)(e - p) : wcslen(p));
    for (int i = 0; i < r.n; i++)
      if (!wcscmp(r.op[i].op, one)) ok = 0;
    p = e ? e + 1 : NULL;
  }
  if (!ok) {
    g_fail++;
    printf("FAIL %s: вид %d (ждали %d), заготовка «", title, r.cls, cls);
    out8(r.blank);
    printf("», марка «");
    out8(r.s.grade);
    printf("»\n  операции: ");
    out8(got);
    printf("\n  ждали:    ");
    out8(ops);
    printf("\n  уточнить: ");
    out8(r.check);
    printf("\n");
  }
  if (g_verbose || !ok) {
    static wchar_t txt[8000];
    tn_text(&r, txt, 8000);
    printf("---- %s\n", title);
    out8(txt);
    printf("\n\n");
  }
}

int main(int argc, char **argv) {
  g_verbose = argc > 1 && !strcmp(argv[1], "-v");

  check("удлинитель вала, штамп прочитан", L"Удлинитель вала", L"Круг 50 ГОСТ 2590-2006 / 45 ГОСТ 1050-2013",
        L"Ra 3,2\nRa 0,8\nØ30 -0,02\n-0,04\nM12-6H\n2 отв. Ø4+0,03\n1. 30...34 HRC\n"
        L"2. Общие допуски по ГОСТ 30893.2-mK\n3. Покрытие: аэрозольный лак\n",
        TN_ROT, L"Круг 50 ГОСТ 2590-2006", L"45",
        L"Заготовительная|Токарная (черновая)|Термическая|Токарная (чистовая)|Токарная (резьба)|Сверлильная|"
        L"Круглошлифовальная|Слесарная|Окрасочная|Контрольная",
        NULL, NULL);

  check("удлинитель вала, только текст листа (Tesseract)", L"", L"",
        L"Ra 3,2 (/)\n220\nRa 08\n2,6x45°\n1. 30...34 HRC\n2. Общие допуски по ГОСТ 30893.2-mK\n"
        L"3. Покрытие Аэрозольный лак бесцветный\nУдлинитель Вала\n50 ГОСТ 2590-2006\nруг\n45 ГОСТ 1050-2013\n"
        L"МИ Mi2-6H\n0,02\nD302\n0,04\n2 отб.\n",
        TN_ROT, L"Круг 50 ГОСТ 2590-2006", L"45",
        L"Заготовительная|Токарная (черновая)|Термическая|Токарная (чистовая)|Токарная (резьба)|Сверлильная|"
        L"Круглошлифовальная|Слесарная|Окрасочная|Контрольная",
        NULL, L"без запятой");

  check("пластина из листа", L"Пластина", L"Лист 08кп ГОСТ 16523-97 S=6 мм",
        L"4отв. Ø20\nR10\n1.* Размеры для справок.\n2. Общие допуски по ГОСТ 30893.1: H14, h14\n"
        L"4. Покрытие: грунт ГФ-021.\n",
        TN_FLAT, L"S=6", L"08кп", L"Заготовительная (раскрой)|Слесарная|Окрасочная|Контрольная",
        L"Сверлильная|Токарная", NULL);

  check("втулка 20Х с цементацией", L"Втулка направляющая", L"Сталь 20Х ГОСТ 4543-71",
        L"Ø80\nØ28H7\nØ40k6\nRa 0,8\nRa 0,3\n3 отв. Ø9\n2х45°\n"
        L"2. Цементировать на глубину h=0,8...1,2 мм до твердости 57...61 HRC\n"
        L"3. Неуказанные предельные отклонения размеров отверстий H14, валов h14, остальные ±IT14/2\n",
        TN_ROT, L"Круг 85 ГОСТ 2590", L"20Х",
        L"Заготовительная|Токарная (черновая)|Токарная (чистовая)|Сверлильная|Термическая|Круглошлифовальная|"
        L"Слесарная|Контрольная",
        NULL, NULL);

  check("ось с лыской и цинком", L"Ось блока", L"Сталь 45 ГОСТ 1050-88",
        L"M10x1-7H\nØ40e9\nRa 12,5\nØ6\n1. H14, h14, ±IT14/2\n2. Покрытие Ц9.хр\nлыска 5\n", TN_ROT,
        L"Круг 43 ГОСТ 2590", L"45",
        L"Заготовительная|Токарная|Токарная (резьба)|Фрезерная|Слесарная|Гальваническая|Контрольная",
        L"Термическая|Круглошлифовальная", NULL);

  check("корпус чугунный", L"Корпус", L"СЧ20 ГОСТ 1412-85", L"4 отв. M8-7H\nRa 1,6\nRa 6,3\n", TN_BODY,
        L"Отливка", L"СЧ20", L"Фрезерная|Сверлильная|Слесарная|Контрольная", L"Заготовительная|Круглошлифовальная",
        NULL);

  check("просьба словами в чате", L"", L"",
        L"вал, сталь 40Х ГОСТ 4543, закалка 45...50 HRC, Ra 0,4, шпоночный паз", TN_ROT, L"Круг ГОСТ 2590",
        L"40Х",
        L"Заготовительная|Токарная (черновая)|Токарная (чистовая)|Фрезерная|Термическая|Круглошлифовальная|"
        L"Слесарная|Контрольная",
        NULL, L"размер прутка");

  check("латунная крышка по признакам", L"", L"",
        L"M30x2\nTrt6x2\n3х45\n2x45 °\n#22\n#64\nKosluka корпуса\n63 ГОСТ 15527-70\nRa 63\n", TN_ROT,
        L"Пруток ГОСТ 2060", L"Л63", L"Заготовительная|Токарная|Токарная (резьба)|Слесарная|Контрольная", NULL,
        L"косвенным");

  check("ничего не понятно", L"", L"", L"абв\n12\n", TN_UNK, NULL, NULL,
        L"Механическая обработка|Слесарная|Контрольная", NULL, L"вид детали не определён");

  /* признаки по отдельности */
  TnSigns s;
  tn_signs(L"", L"", L"МИ Mi2-6H\nM30x2\nTrt6x2\nM1:1\nMotip\nМасса\n4 отв. M8\n", &s);
  g_n++;
  if (wcscmp(s.thr, L"M12-6H; M30x2; Tr16x2; M8") || s.thrIn != 1 || s.holes != 4) {
    g_fail++;
    printf("FAIL резьбы: «");
    out8(s.thr);
    printf("» внутр. %d, отв. %d\n", s.thrIn, s.holes);
  }
  tn_signs(L"", L"", L"Ra 08\nRa 6,3\nPa 32\nКа 1,6\nRaZ0\n", &s);
  g_n++;
  if (s.raMin != 0.8 || !s.raDoubt || s.nRa != 5) g_fail++, printf("FAIL Ra: %g сомн. %d n %d\n", s.raMin, s.raDoubt, s.nRa);
  tn_signs(L"", L"", L"1 35.40 HRC\n", &s);
  g_n++;
  if (s.hrcLo != 35 || s.hrcHi != 40) g_fail++, printf("FAIL HRC: %d…%d\n", s.hrcLo, s.hrcHi);
  tn_signs(L"", L"", L"Масса 0,49\n-0070\n0.06мм2\n0,5х45°\n2. Общие допуски по ГОСТ 30893.1: 0,02\n", &s);
  g_n++;
  if (s.band < 0.069 || s.band > 0.071) g_fail++, printf("FAIL допуск: %g\n", s.band);
  tn_signs(L"", L"", L"Сталь 12Х18Н10Т ГОСТ 5632-2014\n", &s);
  g_n++;
  if (s.mat != TNM_STEEL_STAIN || wcscmp(s.grade, L"12Х18Н10Т")) g_fail++, printf("FAIL нержавейка: %d\n", s.mat);
  tn_signs(L"", L"", L"остальные ТТ по СТБ1014-95\nпостоянно 1050\n", &s);
  g_n++;
  if (s.mat || s.matStd[0]) g_fail++, printf("FAIL «ост» внутри слова принят за ГОСТ\n");

  tn_signs(L"", L"", L"Покраска\nАэрозольный лак бесцветный\nГальваническое покрытие\n", &s);
  g_n++;
  if (wcscmp(s.coat, L"лак") || s.coatKind != TNC_PAINT) g_fail++, printf("FAIL покрытие из таблицы\n");

  printf("%d проверок, %d ошибок\n", g_n, g_fail);
  return g_fail != 0;
}
