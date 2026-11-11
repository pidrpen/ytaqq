/* pk_sheets.h — листы пакета окна «ЭСИ и ТП»: их шапки и ширины. Подключают export.c и стенды
   (pk_xlsx_test.c) — шапки одни на всех: по ним загрузка из Excel (pk_xlsx.c) узнаёт листы. */

/* «Заготовки», столбцы «Чертёж (файл)» и «Признак покупки» (с 2026.10.29) — только для
   «В Союз…»: команда PLM ищет листы и столбцы по началу названия, лишние пропускает */
enum { PK_ITEMS, PK_LINKS, PK_OPS, PK_BLANKS, PK_STEPS, PK_NT };
static const wchar_t *const kPkItemsHead[] = {L"Обозначение", L"Наименование", L"Вид изделия", L"Материал",
                                              L"Масса, кг",    L"Чертёж (файл)", L"Признак покупки"};
static const int kPkItemsW[] = {26, 34, 18, 40, 10, 40, 22};
static const wchar_t *const kPkLinksHead[] = {L"Сборка", L"Входящее", L"Кол-во", L"Позиция"};
static const int kPkLinksW[] = {26, 26, 9, 9};
/* «Профессия» (с 2026.10.30, John: «профессии нет в операциях») — последним столбцом: номера прежних не сдвигаются */
static const wchar_t *const kPkOpsHead[] = {L"Обозначение", L"№ опер.", L"Операция", L"Участок",
                                            L"Оборудование", L"Тпз, мин", L"Тшт, мин", L"Профессия"};
static const int kPkOpsW[] = {26, 8, 30, 20, 34, 9, 9, 26};
static const wchar_t *const kPkBlanksHead[] = {L"Деталь", L"Материал заготовки", L"Норма, кг", L"Размеры, мм"};
static const int kPkBlanksW[] = {26, 44, 9, 22};
/* «Переходы» (с 2026.11.11, John: «чтобы в союз с переходами») — переходы операций: деталь и № операции — как на листе
   «Операции», текст перехода, То и Тв в минутах. В «Союз» — строкой ПЕРЕХОД (пакет 1.0.0.15): переход в содержании
   операции, То/Тв — нормы перехода */
static const wchar_t *const kPkStepsHead[] = {L"Обозначение", L"№ опер.", L"№ перех.", L"Содержание перехода",
                                              L"То, мин",     L"Тв, мин"};
static const int kPkStepsW[] = {26, 8, 8, 80, 9, 9};
static const XpSheetDef kPkSheets[PK_NT] = {
    {L"Изделия", XP_N(kPkItemsHead), kPkItemsHead, kPkItemsW},
    {L"Состав", XP_N(kPkLinksHead), kPkLinksHead, kPkLinksW},
    {L"Операции", XP_N(kPkOpsHead), kPkOpsHead, kPkOpsW},
    {L"Заготовки", XP_N(kPkBlanksHead), kPkBlanksHead, kPkBlanksW},
    {L"Переходы", XP_N(kPkStepsHead), kPkStepsHead, kPkStepsW},
};
