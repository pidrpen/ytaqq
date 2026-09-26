/* ---- Расчёт краски: формулы -------------------------------------------------

   Перенос файла «Краска — расчёт.xlsx» (pidrpen/wowdroch) — формулы те же,
   ячейка в ячейку (сверено на примерах из файла, см. README):

     площадь 1 шт, м² (размеры в мм, итог / 1 000 000):
       круг              π·D·L            + торцы 2·π·D²/4
       лист              2·Ш·L            + торцы (кромки) 2·(Ш+L)·Т
       труба             π·D·L            + внутри π·(D−2s)·L + торцы 2·π/4·(D²−(D−2s)²)
       квадратная труба  4·a·L            + внутри 4·(a−2s)·L + торцы 2·(a²−(a−2s)²)
     площадь всего  = площадь 1 шт × кол-во (пусто — 1)
     расход, кг     = площадь всего × норма (кг/м² за слой) × слоёв (пусто — 1)

   Нормы — как на листе «Нормы» того файла; их можно менять в окне. */

#include <math.h>

enum { PN_ROUND, PN_SHEET, PN_PIPE, PN_SQPIPE, PN_NPROF };

#define PN_NCOAT 3 /* эмаль / лак, грунтовка, клей — столбцы L, O, R файла */
#define PN_NAME 64

typedef struct {
  int prof;
  double d1, d2, len; /* мм; d2 — толщина листа или стенка трубы, 0 — не задано */
  int qty;            /* 0 — как 1 */
  int ends, inside;   /* «Торцы (да/нет)», «Внутри (да/нет)» */
  wchar_t mat[PN_NCOAT][PN_NAME]; /* пусто — этот слой не нужен */
  int lay[PN_NCOAT];              /* 0 — как 1 */
  wchar_t desc[80];               /* «Обозначение / наименование», можно пусто */
} PnPart;

/* площадь одной детали, м²; < 0 — не хватает размеров или они невозможны */
static double pn_area1(const PnPart *p) {
  double D = p->d1, L = p->len, s = p->d2;
  if (!(D > 0) || !(L > 0)) return -1;
  if (s < 0) return -1;
  double a;
  switch (p->prof) {
  case PN_ROUND:
    a = M_PI * D * L + (p->ends ? 2 * M_PI * D * D / 4 : 0);
    break;
  case PN_SHEET:
    a = 2 * D * L + (p->ends ? 2 * (D + L) * s : 0);
    break;
  case PN_PIPE: {
    double di = D - 2 * s;
    if (di < 0) return -2; /* стенка толще половины диаметра */
    a = M_PI * D * L + (p->inside ? M_PI * di * L : 0) + (p->ends ? 2 * M_PI / 4 * (D * D - di * di) : 0);
    break;
  }
  case PN_SQPIPE: {
    double ai = D - 2 * s;
    if (ai < 0) return -2;
    a = 4 * D * L + (p->inside ? 4 * ai * L : 0) + (p->ends ? 2 * (D * D - ai * ai) : 0);
    break;
  }
  default:
    return -1;
  }
  return a / 1000000.0;
}

static double pn_area_total(const PnPart *p) {
  double a = pn_area1(p);
  return a < 0 ? a : a * (p->qty > 0 ? p->qty : 1);
}

/* расход одного слоя покрытия (k — 0..2), кг; < 0 — слой не выбран */
static double pn_kg(const PnPart *p, int k, double norm) {
  if (!p->mat[k][0]) return -1;
  double a = pn_area_total(p);
  if (a < 0) return -1;
  return a * norm * (p->lay[k] > 0 ? p->lay[k] : 1);
}
