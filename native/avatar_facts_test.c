/* avatar_facts_test.c — факты и анекдоты помощника (ai_avatar_facts.h): разбор ответа
   модели, выбор из списка без повторов. Без Win32. */
#include <stdio.h>
#include <stdlib.h>
#include "ai_avatar_facts.h"

static int fails;

static void fail(const char *what, int no) {
  fails++;
  printf("FAIL %s %d\n", what, no);
}

/* clean: in → ожидаемая реплика или NULL (не годится) */
static void check_clean(int no, const wchar_t *in, const wchar_t *want) {
  wchar_t buf[1024];
  wcsncpy(buf, in, 1023);
  buf[1023] = 0;
  int ok = fj_clean(buf);
  if (want ? !ok || wcscmp(buf, want) : ok) fail("clean", no);
}

int main(void) {
  check_clean(1, L"Сталь 45 содержит около 0,45 % углерода.", L"Сталь 45 содержит около 0,45 % углерода.");
  check_clean(2, L"Факт: сверло затачивают под углом 118 градусов.", L"сверло затачивают под углом 118 градусов.");
  check_clean(3, L"**Анекдот:**\n\n«Технолог пришёл в столовую и спросил техпроцесс борща.»",
              L"Технолог пришёл в столовую и спросил техпроцесс борща.");
  check_clean(4, L"<think>\nхм\n</think>\n\nКонечно! Вот факт: резьба М10 имеет шаг 1,5 мм.", L"резьба М10 имеет шаг 1,5 мм.");
  check_clean(5, L"Фактически резец изнашивается быстрее на нержавейке.", L"Фактически резец изнашивается быстрее на нержавейке.");
  check_clean(6, L"", NULL);
  check_clean(7, L"Коротко.", NULL);
  check_clean(8, L"Here is a fun fact about machining: chips carry most of the heat.", NULL);
  check_clean(9, L"<think>Оборвалось по лимиту", NULL);
  check_clean(10, L"Извините, я не могу придумать анекдот на эту тему.", NULL);
  check_clean(11, L"Смотрите https://example.com — там всё про резцы и пластины.", NULL);
  check_clean(12, L"{\"fact\": \"резец из твёрдого сплава держит температуру\"}", NULL);
  {
    /* длинное режется по концу фразы */
    wchar_t in[700] = L"Первая фраза про токарный станок довольно длинная, но вполне понятная. ";
    while (wcslen(in) < 600) wcscat(in, L"Ещё одна фраза про фрезу и про шпиндель станка. ");
    wchar_t buf[700];
    wcscpy(buf, in);
    if (!fj_clean(buf) || wcslen(buf) > FJ_MAX || buf[wcslen(buf) - 1] != L'.') fail("long", 13);
    /* длинное без конца фразы — не годится */
    wchar_t run[700];
    int k = 0;
    while (k < 600) { run[k++] = L'а'; if (k % 7 == 0) run[k++] = L' '; }
    run[k] = 0;
    if (fj_clean(run)) fail("long", 14);
  }
  check_clean(15, L"\"Кавычки «внутри» оставляем, если есть\"", L"\"Кавычки «внутри» оставляем, если есть\"");

  /* отпечаток не зависит от регистра, ё и знаков */
  if (fj_key(L"Сталь 45, ёлка!") != fj_key(L"сталь 45 елка")) fail("key", 1);
  if (fj_key(L"Сталь 45") == fj_key(L"Сталь 40Х")) fail("key", 2);
  if (fj_key(L"!!!")) fail("key", 3);

  /* в списке нет повторов, у каждой строки нормальная длина и есть оба рода */
  int jokes = 0;
  for (int i = 0; i < FJ_N; i++) {
    jokes += kFjItems[i].joke;
    size_t n = wcslen(kFjItems[i].text);
    if (n < 20 || n > FJ_MAX) fail("item length", i);
    wchar_t buf[FJ_MAX + 1];
    wcscpy(buf, kFjItems[i].text);
    if (!fj_clean(buf) || wcscmp(buf, kFjItems[i].text)) fail("item clean", i);
    for (int j = 0; j < i; j++)
      if (fj_key(kFjItems[i].text) == fj_key(kFjItems[j].text)) fail("item dup", i);
  }
  if (jokes < 20 || FJ_N - jokes < 30) fail("kinds", jokes);

  /* выбор: среди последних FJ_RECENT повторов нет, род не идёт трижды подряд */
  FjState st;
  memset(&st, 0, sizeof(st));
  unsigned r = 12345u, prevKey = 0;
  int run = 0, prevKind = -1;
  for (int step = 0; step < 400; step++) {
    r = r * 1664525u + 1013904223u;
    int kind = fj_kind(&st, r);
    run = kind == prevKind ? run + 1 : 1;
    prevKind = kind;
    if (run > 2) fail("kind run", step);
    int i = fj_pick(&st, kind, r >> 8);
    if (kFjItems[i].joke != kind) fail("pick kind", step);
    unsigned key = fj_key(kFjItems[i].text);
    if (key == prevKey) fail("pick repeat", step);
    if (fj_seen(&st, key)) fail("pick recent", step);
    prevKey = key;
    fj_note(&st, kFjItems[i].text, kind);
  }
  /* ответ модели, совпавший с недавним, узнаётся */
  if (!fj_seen(&st, fj_key(kFjItems[fj_pick(&st, -1, 7)].text)) && st.nRecent < FJ_RECENT) fail("seen", 1);

  /* запрос к модели: тема подставлена, род учтён */
  wchar_t q[300];
  fj_question(1, 3, q, 300);
  if (!wcsstr(q, L"анекдот") || !wcsstr(q, kFjTopics[3])) fail("question", 1);
  fj_question(0, -1, q, 300);
  if (!wcsstr(q, L"факт") || !wcsstr(q, kFjTopics[FJ_TOPICS - 1])) fail("question", 2);
  if (!wcsstr(fj_instruction(), L"по-русски")) fail("instruction", 1);

  printf(fails ? "avatar_facts_test: ошибок %d\n" : "avatar_facts_test: все проверки прошли\n", fails);
  return fails ? 1 : 0;
}
