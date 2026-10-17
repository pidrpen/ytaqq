/* App commands execute on the UI thread. Only this registry is callable by the
   model: no command lines, SQL, function addresses or arbitrary Windows calls. */
typedef struct { const wchar_t *name, *description; void (*show)(void); } AgCapability;
static const AgCapability kAgCapabilities[] = {
  {L"open_match", L"Открыть сопоставление позиций Excel с данными СОЮЗ; выбор данных и запуск видны пользователю", match_show},
  {L"open_export", L"Открыть штатную выгрузку из PLM/СОЮЗ", export_show},
  {L"open_route", L"Открыть маршрутную ведомость из PLM", route_show},
  {L"open_drawings", L"Открыть распознавание чертежей и существующую раскладку по папкам", drawrec_show},
  {L"open_paint", L"Открыть расчёт краски", paint_show},
  {L"open_cutting", L"Открыть расчёт резки и газов", cutting_show},
};
static int ag_app_call(const AgJson *j, wchar_t *out, int cap) {
  const wchar_t *name = ag_get(j, L"name");
  if (!wcscmp(name, L"capabilities")) {
    lstrcpynW(out, L"Доступны filesystem (локальный Filesystem MCP, только list/info/read), excel_write (XlsxWriter для новой книги, mode=table для шапки/фильтра; формулы и копии через установленный Excel), office_read (.docx/.xlsx через Office), compare_materials (ВСЕ строки двух локальных .xlsx, одна может быть выгрузкой СОЮЗ).\r\napp name=plm_status — только состояние реально загруженной выгрузки.\r\n", cap);
    for (size_t i = 0; i < sizeof(kAgCapabilities) / sizeof(kAgCapabilities[0]); i++)
      swprintf(out + wcslen(out), cap - wcslen(out), L"app name=%ls — %ls.\r\n", kAgCapabilities[i].name, kAgCapabilities[i].description);
    wcscat(out, L"Открытие окна не означает выполнение расчёта или сравнения. Прямой доступ к химическому составу материалов СОЮЗ ещё не настроен.\r\n");
    return 0;
  }
  if (!wcscmp(name, L"plm_status")) {
    if (g_xpBusy) { lstrcpynW(out, L"Выгрузка PLM сейчас выполняется; результаты ещё не готовы. Не используй промежуточные строки.", cap); return 0; }
    if (!g_xpJob) { lstrcpynW(out, L"В этой сессии выгрузка из СОЮЗ/PLM не загружена. Для сравнения нужен реальный файл выгрузки .xlsx либо окно app open_export. Данные не получены; не выдумывай их.", cap); return 0; }
    swprintf(out, cap, L"В текущей сессии есть выгрузка PLM: состав — %d строк, материалы операций — %d строк, заготовки — %d строк. Для пакетного сравнения сохраните выгрузку в .xlsx и передайте путь compare_materials. Само сравнение пока не выполнено.", g_xpJob->t[XP_COMP].n, g_xpJob->t[XP_MAT].n, g_xpJob->t[XP_PF].n); return 0;
  }
  for (size_t i = 0; i < sizeof(kAgCapabilities) / sizeof(kAgCapabilities[0]); i++) {
    if (wcscmp(name, kAgCapabilities[i].name)) continue;
    kAgCapabilities[i].show();
    swprintf(out, cap, L"Открыт инструмент CursorPad: %ls. Это только открытие окна. Данные не прочитаны и операция не выполнена автоматически.", kAgCapabilities[i].description); return 0;
  }
  lstrcpynW(out, L"Неизвестная команда CursorPad. Используй app name=capabilities; произвольные команды запрещены.", cap); return -1;
}
