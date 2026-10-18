/* Deterministic material-name matching over ALL rows, not an LLM sample.
   Source and reference are local .xlsx files; the latter may be a PLM export. */
#include "match.h"
static int ag_material_column(const OfTable *t, const wchar_t *wanted) {
  int found = -1;
  const wchar_t *defaults[] = {L"Материал", L"Наименование материала", L"Наименование"};
  int tries = wanted[0] ? 1 : 3;
  for (int k = 0; k < tries; k++) {
    const wchar_t *header = wanted[0] ? wanted : defaults[k];
    for (int c = 0; c < t->cols; c++) {
      wchar_t s[2000]; lstrcpynW(s, t->cell[c], 2000);
      size_t n = wcslen(s); while (n && s[n - 1] == L' ') s[--n] = 0;
      if (!_wcsicmp(s, header)) { if (found >= 0) return -1; found = c; }
    }
    if (found >= 0) return found;
  }
  return -1;
}
static BOOL ag_compare_materials(const wchar_t *src, const wchar_t *ref, const wchar_t *sheet,
                                 const wchar_t *refSheet, const wchar_t *column, int offset,
                                 volatile LONG *cancel, wchar_t *out, int cap) {
  OfTable a, b; wchar_t error[1000] = L""; out[0] = 0;
  if (!of_table_load(src, sheet, &a, error, 1000)) { lstrcpynW(out, error, cap); return FALSE; }
  if (!of_table_load(ref, refSheet, &b, error, 1000)) { of_table_free(&a); lstrcpynW(out, error, cap); return FALSE; }
  BOOL ok = FALSE; MtRules *rules = NULL; MtIndex *ix = NULL;
  int *map = NULL, *best = NULL, *score = NULL, *second = NULL;
  int ac = ag_material_column(&a, column), bc = ag_material_column(&b, column);
  if (ac < 0 || bc < 0) { lstrcpynW(out, L"Не найден однозначный столбец Материал/Наименование материала/Наименование. Укажите одинаковый заголовок через pattern или подготовьте таблицы с явной шапкой.", cap); goto done; }
  map = (int *)calloc(b.rows, sizeof(int)); best = (int *)calloc(a.rows, sizeof(int)); score = (int *)calloc(a.rows, sizeof(int)); second = (int *)calloc(a.rows, sizeof(int));
  rules = mt_rules_parse(NULL); ix = mt_index_new(rules);
  if (!map || !best || !score || !second || !ix) goto done;
  int nb = 0, empty = 0, found = 0, missing = 0, ambiguous = 0;
  for (int r = 1; r < b.rows; r++) if (b.cell[r * b.cols + bc][0]) { map[nb++] = r; mt_index_add(ix, L"", b.cell[r * b.cols + bc]); }
  mt_index_build(ix);
  for (int r = 1; r < a.rows; r++) {
    if (cancel && InterlockedCompareExchange(cancel, 0, 0)) { lstrcpynW(out, L"Сравнение остановлено пользователем. Файлы не изменены.", cap); goto done; }
    const wchar_t *name = a.cell[r * a.cols + ac]; best[r] = -1;
    if (!name[0]) { empty++; continue; }
    MtHit hits[MT_MAX_HITS]; int n = mt_find(ix, L"", name, hits, MT_MAX_HITS);
    if (n) { best[r] = map[hits[0].item]; score[r] = hits[0].score; second[r] = n > 1 ? hits[1].score : 0; found++; if (n > 1 && hits[0].score - hits[1].score < 5) ambiguous++; }
    else missing++;
  }
  swprintf(out, cap, L"Сравнение названий материалов: %ls ↔ %ls.\r\nПрочитаны ВСЕ строки: %d и %d; столбцы: %ls и %ls. С кандидатами: %d; без кандидата: %d; неоднозначных: %d; пустых строк: %d.\r\nЭто сходство позиций, не проверка химического состава или взаимозаменяемости. Год ГОСТ движок может не различать; проверьте стандарт, марку и размер.\r\n", src, ref, a.rows - 1, b.rows - 1, a.cell[ac], b.cell[bc], found, missing, ambiguous, empty);
  int shown = 0, next = offset;
  for (int r = offset + 1; r < a.rows; r++) {
    const wchar_t *name = a.cell[r * a.cols + ac], *candidate = best[r] < 0 ? L"(нет кандидата)" : b.cell[best[r] * b.cols + bc];
    if (shown >= 8 || wcslen(out) + wcslen(name) + wcslen(candidate) + 240 >= (size_t)cap) break;
    wchar_t ca[8], cb[8], referenceCell[24]; of_col(a.firstCol + ac, ca); of_col(b.firstCol + bc, cb);
    if (best[r] < 0) lstrcpynW(referenceCell, L"—", 24);
    else swprintf(referenceCell, 24, L"%ls%d", cb, b.firstRow + best[r]);
    swprintf(out + wcslen(out), cap - wcslen(out), L"%ls%d: %ls → %ls: %ls; сходство %d/100; второй кандидат %d/100%ls\r\n", ca, a.firstRow + r, name, referenceCell, candidate, score[r], second[r], second[r] && score[r] - second[r] < 5 ? L"; НЕОДНОЗНАЧНО" : L"");
    shown++; next = r;
  }
  if (next < a.rows - 1) swprintf(out + wcslen(out), cap - wcslen(out), L"Показана ЧАСТЬ результатов. Следующая страница compare_materials с offset=%d. Данные полностью обработаны, файлы не изменены.", next);
  else lstrcpynW(out + wcslen(out), L"Последняя страница. Сравнение завершено, следующий вызов done. Не повторяй compare_materials с теми же параметрами. Файлы не изменены.", cap - (int)wcslen(out));
  ok = TRUE;
done:
  mt_index_free(ix); mt_rules_free(rules); free(map); free(best); free(score); free(second); of_table_free(&a); of_table_free(&b);
  if (!ok && !out[0]) lstrcpynW(out, L"Недостаточно памяти для сравнения всех строк. Файлы не изменены.", cap);
  return ok;
}
