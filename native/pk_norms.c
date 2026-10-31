/* ---- Нормы Тпз и Тшт операций окна «ЭСИ и ТП» (с 2026.10.29) --------------------------------

   Операции, добавленные строкой быстрого ТП, «Вставить» или ИИ по чертежу, оставались без
   норм. Теперь пустые Тпз/Тшт листа «Операции» считает тот же norms.c, что и маршрут по
   чертежу: операции детали собираются в TnRoute по порядку номеров, масса — из «Изделий»,
   таблицы ОМН — из Документы\CursorPad\Нормативы. Тпз берётся из таблиц по виду операции и
   только у мехобработки (класс операции — Настройки → «Нормирование»), у контроля норм нет;
   Тшт без размеров и режимов не посчитать — остаётся пустым (как «— нет машинного То» в
   маршруте). Что уже вписано, не трогается. Файл идёт после norms.c (package_ui.c — раньше). */

static int pk_norms_fill(BOOL quiet) {
  XpTable *t = &g_pk[PK_OPS];
  int need = 0;
  for (int r = 0; r < t->n; r++)
    if (xp_cell(t, r, 2)[0] && (!xp_cell(t, r, 5)[0] || !xp_cell(t, r, 6)[0])) need++;
  if (!need) return 0;
  NmBase b;
  wchar_t dir[MAX_PATH];
  if (!nm_base_load(&b, dir)) {
    if (!quiet) pk_status(L"Нормы не посчитать: нет таблиц нормативов (%s)", dir);
    return 0;
  }
  TnRoute *r = (TnRoute *)calloc(1, sizeof(TnRoute));
  NmResult *res = (NmResult *)malloc(sizeof(NmResult));
  int filled = 0;
  if (r && res) {
    for (int s = 0; s < t->n; s++) {
      const wchar_t *des = xp_cell(t, s, 0);
      if (!des[0]) continue;
      int seen = 0;
      for (int q = 0; q < s && !seen; q++) seen = !_wcsicmp(xp_cell(t, q, 0), des);
      if (seen) continue;
      /* операции детали — в маршрут по порядку номеров; rowOf — где каждая в таблице */
      int rowOf[24], order[24], n = 0;
      for (int k = 0; k < t->n && n < 24; k++)
        if (!_wcsicmp(xp_cell(t, k, 0), des) && xp_cell(t, k, 2)[0]) order[n++] = k;
      for (int i = 1; i < n; i++)
        for (int k = i; k > 0 && _wtoi(xp_cell(t, order[k - 1], 1)) > _wtoi(xp_cell(t, order[k], 1)); k--) {
          int tmp = order[k];
          order[k] = order[k - 1];
          order[k - 1] = tmp;
        }
      memset(r, 0, sizeof(*r));
      /* заготовка детали «Круг 40 ГОСТ 2590-2006 / Сталь 45 ГОСТ 1050-2013» — сечение и материал для отрезки */
      for (int k = 0; k < g_pk[PK_BLANKS].n; k++)
        if (!_wcsicmp(xp_cell(&g_pk[PK_BLANKS], k, 0), des)) {
          const wchar_t *bl = xp_cell(&g_pk[PK_BLANKS], k, 1), *sl = wcsstr(bl, L" / ");
          tn_signs(L"", bl, L"", &r->s);
          int len = sl ? (int)(sl - bl) : (int)wcslen(bl);
          if (len > TN_TXT - 1) len = TN_TXT - 1;
          wcsncpy(r->blank, bl, (size_t)len);
          r->blank[len] = 0;
          break;
        }
      for (int i = 0; i < n; i++) {
        const wchar_t *op = xp_cell(t, order[i], 2);
        while (*op == L' ') op++;
        int pre = op_num_prefix(op); /* «4110 Токарная» → «Токарная», код — для класса операции */
        if (pre && op[3] >= L'0' && op[3] <= L'9') tn_cpy(r->op[r->n].code, 5, op); /* четыре цифры кода */
        op += pre;
        tn_cpy(r->op[r->n].op, 48, op);
        rowOf[r->n++] = order[i];
      }
      const wchar_t *mass = L"";
      for (int k = 0; k < g_pk[PK_ITEMS].n; k++)
        if (!_wcsicmp(xp_cell(&g_pk[PK_ITEMS], k, 0), des)) {
          mass = xp_cell(&g_pk[PK_ITEMS], k, 4);
          break;
        }
      nm_route(&b, r, mass, NULL, res);
      for (int k = 0; k < res->n; k++) {
        const NmOp *o = &res->op[k];
        if (o->idx < 0 || o->idx >= r->n) continue;
        int row = rowOf[o->idx];
        wchar_t v[24];
        BOOL put = FALSE;
        if (!xp_cell(t, row, 5)[0] && !nm_na(o->tpz) && o->tpz > 0) {
          tn_numstr(o->tpz, v, 24);
          pk_put(t, row, 5, v);
          put = TRUE;
        }
        if (!xp_cell(t, row, 6)[0] && !nm_na(o->tsht) && o->tsht > 0) {
          tn_numstr(o->tsht, v, 24);
          pk_put(t, row, 6, v);
          put = TRUE;
        }
        if (put) {
          filled++;
          if (g_pkSheet == PK_OPS && g_pkList) pk_set_item(row);
        }
      }
    }
  }
  free(r);
  free(res);
  nm_base_free(&b);
  if (filled && !quiet) pk_status(L"Нормы по таблицам ОМН: Тпз/Тшт у %d операций (Тшт — только где есть машинное время)", filled);
  return filled;
}
