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

   Нормы — как на листе «Нормы» того файла; их можно менять в окне.

   С 2026.09.23.44 — ещё профили, которых в файле не было (формулы по
   периметру сечения, скругления проката не учитываются — чуть больше
   настоящей площади, в запас):
       квадрат           4·a·L            + торцы 2·a²
       шестигранник      2·√3·S·L         + торцы √3·S²          (S — под ключ)
       уголок a×b×t      2·(a+b)·L        + торцы 2·t·(a+b−t)    (b пусто — равнополочный)
       швеллер, двутавр  (2h + 4b − 2s)·L + торцы 2·(2·b·t + (h−2t)·s)
       своя площадь      площадь 1 шт, м² — как ввели */

#include <math.h>

/* номера — как в paint.txt: первые четыре с 2026.09.23.43, не переставлять */
enum { PN_ROUND, PN_SHEET, PN_PIPE, PN_SQPIPE, PN_BAR, PN_HEX, PN_ANGLE, PN_CHANNEL, PN_BEAM, PN_AREA, PN_NPROF };

#define PN_NCOAT 3 /* эмаль / лак, грунтовка, клей — столбцы L, O, R файла */
#define PN_NAME 64

typedef struct {
  int prof;
  double d1, d2, len; /* мм; d2 — толщина листа или стенка трубы, 0 — не задано */
  double d3, d4;      /* уголок: d3 — вторая полка; швеллер, двутавр: d1 h, d2 b, d3 s, d4 t */
  int qty;            /* 0 — как 1 */
  int ends, inside;   /* «Торцы (да/нет)», «Внутри (да/нет)» */
  wchar_t mat[PN_NCOAT][PN_NAME]; /* пусто — этот слой не нужен */
  int lay[PN_NCOAT];              /* 0 — как 1 */
  wchar_t desc[80];               /* «Обозначение / наименование», можно пусто */
} PnPart;

/* площадь одной детали, м²; < 0 — не хватает размеров или они невозможны */
static double pn_area1(const PnPart *p) {
  double D = p->d1, L = p->len, s = p->d2;
  if (p->prof == PN_AREA) return D > 0 ? D : -1; /* своя площадь — уже в м² */
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
  case PN_BAR:
    a = 4 * D * L + (p->ends ? 2 * D * D : 0);
    break;
  case PN_HEX:
    a = 2 * sqrt(3.0) * D * L + (p->ends ? sqrt(3.0) * D * D : 0);
    break;
  case PN_ANGLE: {
    double b = p->d3 > 0 ? p->d3 : D, t = s;
    if (t >= D || t >= b) return -2; /* толщина больше самой полки */
    a = 2 * (D + b) * L + (p->ends && t > 0 ? 2 * t * (D + b - t) : 0);
    break;
  }
  case PN_CHANNEL:
  case PN_BEAM: {
    double h = D, b = s, w = p->d3, t = p->d4;
    if (!(b > 0)) return -1;
    if (w < 0 || t < 0 || w >= b || 2 * t >= h) return -2;
    a = (2 * h + 4 * b - 2 * w) * L + (p->ends && t > 0 && w > 0 ? 2 * (2 * b * t + (h - 2 * t) * w) : 0);
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
