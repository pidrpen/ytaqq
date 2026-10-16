/* ---- Нечёткое сопоставление позиций (обозначение + наименование) ----------------

   Движок без окон и без Windows: на вход строки, на выход ближайшие позиции из
   набора и оценка похожести 0…100. Им пользуется окно «Сопоставить с Союзом»
   (match_ui.c), позже — «умный поиск» в окне «Находки».

   Как пользоваться:
     MtRules *r = mt_rules_parse(текст_файла_правил);   // NULL — только встроенное
     MtIndex *ix = mt_index_new(r);
     for (...) mt_index_add(ix, обозначение, наименование);  // номер позиции = порядок
     mt_index_build(ix);
     MtHit hit[MT_MAX_HITS];
     int n = mt_find(ix, обозначение, наименование, hit, MT_MAX_HITS);  // лучшие первыми
     mt_note(&hit[0], текст, 80);                              // «что не так», по-русски
   Правила (сокращения, пороги, слова, которые не считаются) — в файле
   match_rules.txt, его можно править и дополнять; движок читает его текстом.

   Что учитывается: регистр, лишние пробелы и знаки, любые тире и «№», латиница
   вместо кириллицы (A, B, C, E, H, K, M, O, P, T, X, Y), ё/е, порядок слов,
   опечатки (одна на короткое слово, две-три на длинное), сокращения из словаря и
   обрезанные слова («шестигр»), окончания, числа отдельно (18 не равно 28,
   18,0 равно 18), код стандарта (ГОСТ 2590) отдельно от года (-88, -2006). */
#ifndef MATCH_H
#define MATCH_H

#include <wchar.h>

#define MT_MAX_HITS 5

/* пометки, почему оценка такая */
enum {
  MTF_DES_EXACT = 1,   /* обозначения совпали буква в букву (после чистки) */
  MTF_DES_DIFF = 2,    /* у обеих есть обозначение, но оно другое */
  MTF_NAME_EXACT = 4,  /* наименования совпали */
  MTF_TYPO = 8,        /* исправлена опечатка */
  MTF_ABBR = 16,       /* сошлось через сокращение или обрезанное слово */
  MTF_SIZE_DIFF = 32,  /* число из запроса не найдено у кандидата */
  MTF_STD_DIFF = 64,   /* другой стандарт (ГОСТ, ТУ…) */
  MTF_PARTIAL = 128    /* найдена только часть слов запроса */
};

typedef struct {
  int item;           /* номер позиции в наборе (порядок добавления) */
  int score;          /* 0…100 */
  unsigned flags;     /* MTF_… */
  wchar_t miss[24];   /* первое число запроса, которого нет у кандидата */
} MtHit;

typedef struct MtRules MtRules;
typedef struct MtIndex MtIndex;

enum { MT_T_EXACT, MT_T_LIKELY, MT_T_SHOW };

MtRules *mt_rules_parse(const wchar_t *text); /* text может быть NULL */
void mt_rules_free(MtRules *r);
int mt_rules_threshold(const MtRules *r, int which); /* точно (97), вероятно (60), показывать (35) */

MtIndex *mt_index_new(const MtRules *r); /* r должен жить, пока жив индекс */
int mt_index_add(MtIndex *ix, const wchar_t *des, const wchar_t *name); /* номер позиции */
void mt_index_build(MtIndex *ix);
int mt_find(MtIndex *ix, const wchar_t *des, const wchar_t *name, MtHit *out, int max);
void mt_index_free(MtIndex *ix);

void mt_note(const MtHit *h, wchar_t *out, int cap);

/* ---- точка расширения: локальный ИИ как «помощник для неоднозначных позиций» ------
   Окно зовёт mt_ai_rank_candidates, когда правила не уверены (лучший кандидат не
   «точный» и рядом есть почти равный, или оценка на границе «вероятно»). Пока
   помощник не подключён, функция ничего не меняет и возвращает 0. Подключить
   локальную модель позже — mt_set_ai_ranker(функция): она получает позицию и
   кандидатов (текст, оценка правил), заполняет order — порядок кандидатов от
   лучшего к худшему — и при желании правит score; возвращает 1. Ничего не уходит
   из программы: помощник работает на этом компьютере. */
typedef struct {
  const wchar_t *des, *name; /* кандидат из набора */
  int score;                 /* оценка правил; помощник может поправить */
  unsigned flags;
} MtAiCand;
typedef int (*MtAiRankFn)(const wchar_t *des, const wchar_t *name, MtAiCand *cand, int n, int *order);
void mt_set_ai_ranker(MtAiRankFn f);
int mt_ai_wanted(const MtHit *h, int n, const MtRules *r); /* 1 — позиция неоднозначна */
int mt_ai_rank_candidates(const wchar_t *des, const wchar_t *name, MtAiCand *cand, int n, int *order);

#endif
