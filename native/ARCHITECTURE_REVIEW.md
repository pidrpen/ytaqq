# CursorPad (native) — обзор архитектуры и корректности

Дата: 2026-10-21. Объект — только `native/` (сборка в `public/CursorPad.exe`).
Веб-часть (`src/`, `server/`, `package.json`) не рассматривалась.
Номера строк — на момент проверки; файлы параллельно правят другие агенты, строки могут сдвинуться.

---

## 1. Архитектура

### 1.1. Сборка: unity build

Один модуль трансляции — `cursorpad.c`; всё остальное подключается через `#include "*.c"`.
Отсюда два следствия: все `static`-глобали видны всем модулям ниже по тексту
(связность по глобалям очень высокая), а порядок `#include` — это порядок объявлений.

```
cursorpad.c
├─ pad_extra.c ── files.c, update.c (version.h), ai_local.c (ai.h, ai_zai.c, ai_qwen.c → agent_pack.h, addons_pack.h;
│                 ai_agent_schema.h), mini_ai.c, web_lookup.c, ops_1c.h, route.c (xlsx_write.c), export.c, share.c,
│                 route_ui.c, export_ui.c, package_ui.c, match_ui.c (match.c), tess.c
├─ fairy_fx.c, styles.c (gojo_sprite.h), idle_fight.c (fight_art.c)
├─ ai_ui.c                      вкладка «ИИ» в Настройках
├─ cutting.c (cutting_calc.c, cutting_norms.h), paint.c (paint_calc.c)
├─ tiffmerge.c (tiff_codec.c), tiffsort.c, drawrec.c, drawrec_ui.c, drawrec_sort.c
├─ ai_chat.c ── ai_chat_format.h, ai_chat_policy.h, ai_avatar.c (ai_avatar_face.h), ai_web.c (ai_web_json.h),
│               ai_chat_route.c, ai_files.c, ai_agent.c ── ai_addons.c, ai_agent_fs.c (ai_agent_write.c,
│               ai_office.c, ai_word_format.c, ai_compare.c → match.h, ai_inventory.c), ai_app_tools.c,
│               ai_document_author.c, ai_agent_prompt.h, ai_agent_route.h
├─ nardy.c (nardy_rules.c), shashki.c (shashki_rules.c), hotkeys.c, layout_fix.c, msg.c, tools.c
└─ visual_test.c / qwen_test.c — только в тестовых сборках
```

Отдельно собираются тесты: `match_test.c`, `chat_format_test.c`, `word_format_test.c`, `office_test.c`,
`agent_files_test.c`, `agent_probe.c`; `ai_web.c` с `-DAC_WEB_TEST`; `qwen_test.c` с `-DQWEN_TEST`.

### 1.2. Карта модулей (кратко)

| Область | Файлы | Что делает |
|---|---|---|
| Ядро окна, курсоры, блокнот | cursorpad.c, styles.c, fairy_fx.c, idle_fight.c | главное окно, следование за курсором, темы, эффекты клика (хук WH_MOUSE_LL), бой при бездействии |
| Поиск PLM/файлы, карточки, 1С | pad_extra.c, files.c, route*.c, export*.c, package_ui.c, ops_1c.h | ODBC к SQL PLM, индекс архива (files.json), ведомости, выгрузки, перенос в 1С (хук WH_KEYBOARD_LL) |
| Общая папка | share.c, msg.c, nardy.c, shashki.c, tess.c | раздача PLM коллегам, сообщения, игры, пакет Tesseract — всё через файлы в сетевой папке |
| OCR / чертежи / TIFF | pad_extra.c (OCR), tess.c, ocr_module.c (CursorPadOcr.exe), drawrec*.c, tiff*.c | распознавание, раскладка сканов, склейка/сортировка TIFF |
| Обновление | update.c, tools.c | проверка/скачивание exe с GitHub/CDN, откат, чистка |
| ИИ: движок | ai_local.c, ai_zai.c, ai_qwen.c, ai.h | llama-server.exe на 127.0.0.1 (job-объект), Z.ai через комплект Python, загрузка моделей |
| ИИ: чат | ai_chat.c, ai_chat_route.c, ai_web.c, ai_chat_format.h, ai_avatar.c | окно «Спросить ИИ», маршрутизация, поиск Tavily, RTF-лента, аватар |
| ИИ: агент | ai_agent*.c, ai_office.c, ai_word_format.c, ai_inventory.c, ai_compare.c, ai_files.c | план файловых действий, проверка путей, «Применить» |
| Прочее | hotkeys.c, layout_fix.c, cutting.c, paint.c, match*.c, mini_ai.c, web_lookup.c | горячие клавиши, исправление раскладки, расчёты, сопоставление номенклатуры |

### 1.3. Глобальное состояние

Почти всё — файловые `static`-глобали: окна (`g_hwnd`, `g_setHwnd`, `g_answer`, `g_card`, `g_acWnd`…),
результаты поиска (`g_plmCount`, `g_plmEsi[]`, `g_plmIds[]`, `g_resultFiles`), настройки ИИ (`g_aiOn`, `g_aiTier`,
`g_aiCtx`, `g_aiModel`, `g_aiGpu`, `g_aiProvider`), беседа (`g_acTurns`, `g_acN`, `g_acId`), агент
(`g_agFs` — ~2,7 МБ: 512 действий × ~5,4 КБ), индекс файлов (`g_idx` под `g_filesCs`).
Замки есть точечно: `g_aiSrvLock`, `g_aiJobLock`, `g_aiLogLock` (SRW), `g_filesCs`, `g_shareCs`, `g_msgCs`,
`g_drLock`, `nd_lock`, `g_aqPackLock`. Большинство глобалей UI замков не имеют и рассчитаны на один поток окон.

### 1.4. Модель потоков

Поток окон один (в нём же работают хуки WH_MOUSE_LL / WH_KEYBOARD_LL — поэтому его нельзя блокировать
дольше ~300 мс). Рабочие потоки создаются `CreateThread` и возвращают результат **почти всегда через
`PostMessageW(hwnd, WM_APP+n, 0, (LPARAM)heap_ptr)`**; получатель освобождает память, при неудачном
`PostMessage` освобождает отправитель. Это хороший, последовательно выдержанный шаблон.

| Поток | Файл | Обратно в UI |
|---|---|---|
| ai_worker / ai_async_thread / ai_watch_thread / ai_dl_thread | ai_local.c | WM_AI_CHAT (AiResult*), WM_AI_UI, WM_AI_TEST |
| aq_warm_thread | ai_qwen.c | — (журнал) |
| ag_tool_thread | ai_agent.c | WM_AG_TOOL (AgToolJob*) |
| ac_web_thread | ai_chat.c | WM_AC_WEB (AcWebDone*) |
| ai_ui_unzip_thread | ai_ui.c | WM_AI_UI (**пишет g_aiSrvExe и ai.txt сам**) |
| search_thread, card_thread, pf_thread, sel_draw_thread, ocr_thread | pad_extra.c | WM_SEARCH_DONE, WM_CARD_DONE, WM_PF_DONE, WM_SEL_DRAW, WM_OCR_DONE (**search_thread пишет g_plm* напрямую**) |
| files_index_thread, files_walk_worker | files.c | WM_FILES_DONE, WM_FILES_CHANGES (индекс — под CS) |
| update_thread | update.c | WM_UPDATE_DONE |
| share_serve_thread, msg_thread, nd_thread, sh_thread, tess_share_thread | share.c, msg.c, nardy.c, shashki.c, tess.c | WM_MSG_IN/SENT, WM_NARDY_POLL, WM_SHASHKI_POLL |
| dr_worker, tm_worker, mtw_thread, rt_thread/rt_pool_worker, xp_thread/xp_worker | drawrec_ui.c, tiffmerge.c, match_ui.c, route*.c, export*.c | свои WM_* окнам модулей |
| if_thread | idle_fight.c | своё окно и свой цикл сообщений |
| tools_cleanup_thread | tools.c | — |

### 1.5. Идентификаторы

**WM_APP+n** (все `#define`, встроенных `WM_APP+` нет): 1 TRAY · 8 SEARCH_DONE · 9 OCR_DONE · 10 SHOW_PAD ·
11 FILES_DONE · 12 UPDATE_DONE · 13 SEL_DRAW · 14 CARD_DONE · 15 FILES_CHANGES · 16 PF_DONE · 17 FAIRY_CLICK ·
18 NARDY_POLL · 19 MSG_IN · 20 MSG_SENT · 40–42 TM_* · 60 PN_ADD · 61–62 RT_* · 63 SHASHKI_POLL · 64–65 XP_* ·
70 AI_UI · 71 AI_TEST · 72 AI_CHAT · 73 AC_SEND · 74 AG_TOOL · 75 AC_WEB · 120 MT_DONE · 140–141 DR_*.
Пересечений нет.

**Таймеры на g_hwnd**: 1 FOLLOW, 2 SAVE, 3 PASTE, 4 STATUS, 6 CURSOR_KEEP, 8 FILES, 9 FILES_TICK, 10 FILES_PLAN,
21 1C, 30–33 UPD_*, 34 LF_RESTORE, 40 IDLE_FIGHT — пересечений нет. Прочие таймеры (id 1 в cutting/paint/tiffmerge/
msg-toast/styles/fairy, 291, 898, 932, 77 на поле заметок, 0xA171 у аватара) висят на своих окнах — конфликтов нет.

**ID элементов**: два совпадения значений — 152 (`ID_FILES_BROWSE` в Настройках и `ID_ANS_CARD` в «Находках»),
154 (`ID_ANSPIN` в Настройках и `ID_ANS_DRAW` в «Находках»). Окна разные, сейчас безвредно, но общая
`draw_pad_button` (cursorpad.c ~2508) красит по `id` без учёта окна, и там же зашит литерал `4714` (`ID_AC_APPLY`).
Диапазоны `*_BASE + i` (темы 140–144, эффекты 400–416, вкладки 440–445, стили 460–466, бездействие 480–484,
горячие клавиши 241–247/251–257, краска 736–739/740–759, пакет 300–303) не налезают на соседей.

**Ресурсы (cursorpad.rc)**: CURSOR 200–281 (+k7 как RCDATA 360–375), RCDATA 300–311, 302 ocr.ps1,
305 CursorPadOcr.exe, 330 whatsnew.txt, 346 match_rules.txt, 380 атлас аватара. Дублей нет.

### 1.6. Конвейер ИИ

```
Окно «Спросить ИИ» (AcProc) ── Enter/«Отправить» ──▶ ac_send (ai_chat.c)
  ├─ «запомни…» → ai_learn.txt (тетрадь), без модели
  ├─ картинка/вопрос по картинке → сразу ac_finish_ask (зрение)
  └─ иначе ac_route_start (ai_chat_route.c): JSON-схема {request, intent, continue_file}
        ai_ask_body → ai_async_thread → ai_wait → ai_worker → ai_job_run
             локально: ai_server_ensure (llama-server, замок g_aiSrvLock) → POST /v1/chat/completions (SSE)
             Z.ai / агент: aq_run → python.exe bridge.py (комплект QwenPaw, свой job-объект)
        WM_AI_CHAT → ac_result → ac_route_result → g_acRouteDispatch → ac_send ещё раз:
          answer     → ac_finish_ask: системная роль = политика + дата + карточка mini_ai + тетрадь (+ веб)
          web        → ac_web_thread (Tavily / чтение ссылки, только публичные хосты, без редиректов)
                       → WM_AC_WEB → ac_finish_ask
          cancel/deescalate → ac_end_task (локальный ответ, без действий)
          files      → ag_start (ai_agent.c):
               ag_request → ai_ask_files_async (goal/plan/observation + kAiAgentSchema, ≤16 шагов)
               → WM_AI_CHAT → ag_result → app-инструмент сразу, иначе AgToolJob → ag_tool_thread
               → ag_tool (ai_agent_fs.c): ag_path (канонизация, области @desktop/@downloads/@documents/@last/@new,
                 запрет системных папок, папки данных, ссылок/junction, ADS) → действие только ЗАНОСИТСЯ в план
               → WM_AG_TOOL → ag_tool_ready → следующий шаг | done → ag_preview | ask → уточнение
          «Применить» → ac_files_apply → ag_apply_review → ag_tool_thread(applying) → ag_preflight (повторная
               проверка путей, отпечатков, размеров) → ag_apply (CREATE_NEW / FAIL_IF_EXISTS, без перезаписи)
               → WM_AG_TOOL → итог в чат
```
Старая ветка «навыка Файлы» (`ac_is_file_cmd`, `ac_mkdir_handle`, `ac_xfer_handle`, `ac_group_plan`, `ac_cmd_run`,
ai_files.c) достижима только если `ai_ready()` стал ложным между маршрутизацией и разбором — по сути мёртвый код.

---

## 2. Находки

Серьёзность: **В** — высокая, **С** — средняя, **Н** — низкая.

### 2.1. Исправлено

| # | Где | Серь. | Что было не так | Исправление |
|---|---|---|---|---|
| F1 | ai_local.c:547–562, 689–695 (`ai_server_stop`, `ai_server_ensure`) | **В** | `ai_server_ensure` держит `g_aiSrvLock` эксклюзивно всё время загрузки модели (до срока запроса: 120 с для чата, 600 с для агента). Смена контекста, уровня, GPU, пути модели, провайдера, выбор llama.cpp в Настройках вызывали `ai_server_stop()` в потоке окон → **программа зависала до конца загрузки**, а хуки WH_MOUSE_LL/WH_KEYBOARD_LL этого потока переставали отвечать (лаги мыши, Windows снимает хук). | В потоке окон `TryAcquireSRWLockExclusive`; замок занят — флаг `g_aiSrvStale`, сервер перезапустит следующий запрос с новыми настройками. Рабочие потоки (скачивание) ждут, как раньше. |
| F2 | files.c:167–197, 244–262, 962–981 (`idx_dir`, `idx_merge`, `wq_push_owned`) | С | Два-три `realloc` подряд; если первый удался, а второй нет — функция выходила, не сохранив новый указатель: старый блок уже освобождён `realloc`, структура держит висячий указатель → use-after-free / двойное освобождение в `idx_free` (при нехватке памяти на большом архиве). | Удавшийся `realloc` сохраняется сразу. |
| F3 | ai_agent_fs.c:1126–1146 (инструмент `read`) | С | Читаются первые 4096 байт и декодируются с `MB_ERR_INVALID_CHARS`. Если 4096-й байт попадает внутрь UTF-8-буквы (русский текст — примерно каждый второй файл > 4 КБ), валидный файл отклонялся сообщением «Действие отклонено…», и модель зацикливалась. Подтверждено пробником: до правки state=-1, после — 0. | Неполная последняя буква отрезается перед декодированием. |
| F4 | ai_files.c:156–172, 229–247, 300–308, 479, 490, 632–645, 712, 723, 729, 742; ai_chat.c:653–661 | С | `_snwprintf` в этой сборке (UCRT) **не ставит 0 при переполнении** (проверено пробником). Пути «папка\имя» из каталогов и от модели склеивались в `MAX_PATH` без проверки: незавершённые строки читались дальше буфера (`af_exists`, `wcslen` в `ac_push`), а обрезанный путь мог уйти в план «Применить» как другое имя. Сообщения со списком файлов (`msg[600]`/`msg[1200]`) переполнялись уже при ~15 файлах с длинными именами. | Длинные пути пропускаются/отклоняются до склейки (`af_unique_target` → пустая цель, действие не добавляется), сообщениям — явный 0 в конце. |
| F5 | ai_chat.c:1893–1894, 1919 (`ac_paste_img`, `ac_shot_done`) | С | Снимок экрана и `CF_BITMAP` конвертировались с `WICBitmapUseAlpha`; у GDI-снимка альфа — нули, и PNG, уходящий модели, был **полностью прозрачным** (пробник: BGRA 50,100,200, a=0). llama.cpp альфу отбрасывает, но любой потребитель, учитывающий прозрачность (облако Z.ai, просмотрщики), видит пустую картинку. | `WICBitmapIgnoreAlpha`. |
| F6 | ai_chat.c:1853–1860 (`ac_take`) | Н | STATIC с 32-битной картинкой с альфой хранит свою копию и возвращает её из `STM_SETIMAGE`; возврат игнорировался → утечка GDI-битмапа на каждую прикреплённую картинку (после F5 — на каждую). | Возвращённая копия удаляется. |
| F7 | ai_word_format.c:60–62 (`ag_word_apply`) | Н | Два ранних отказа (нет `\`, путь > ~205 знаков) возвращали FALSE, не заполнив `out`; `ag_apply` показывал неинициализированный `detail[1600]` через `lstrcpynW(out, detail, 6000)` — мусор и чтение за буфером. | Сообщение задаётся до ранних выходов. |
| F8 | ai_local.c:790 | Н | Журнал запуска llama-server писал `g_aiPort` (уже 0) вместо выбранного порта — сбивало разбор «Скопировать отчёт». | `port`. |

### 2.2. Не исправлено (риск или правка слишком большая)

| # | Где | Серь. | Суть |
|---|---|---|---|
| R1 | tess.c:296–434 (`tess_pack_sync` из общей папки), pad_extra.c:5998 (`tess_share_sync_async`) | **В** | Пакет Tesseract ставится из `<общая папка>\CursorPad-PLM\tesseract`, а sha256 сверяется с `pack.txt` **из той же папки** — самоподписанно. Любой с правом записи в общую папку подкладывает свой `tesseract.exe` с «правильным» `pack.txt` (версия выше) → он ставится в `%LOCALAPPDATA%\CursorPad\tesseract` и запускается при следующем F6 у всех коллег. Нужны зашитые в exe отпечатки (как `agent_pack.h`) или проверка `pack.txt` только с GitHub. **Исправлено в 2026.10.21.2:** `pack.txt` закреплён в exe (`tess_pack.h`), папка пакета сверяется целиком перед запуском. |
| R2 | update.c:995–1008 (`upd_sha_ok`) | С | Отпечаток exe берётся из того же `version.txt`, что и сам файл (те же зеркала); нет подписи. Пустой `sha256` в `version.txt` → принимается любой PE. Целостность есть, подлинности нет. **Исправлено в 2026.10.21.2:** `version.txt` подписан RSA-3072 (`update_sign.py`, ключ вне репозитория), exe без отпечатка или верной подписи не ставится (RELEASE_SIGNING.md). В 2026.10.22.2 подпись снималась; **с 2026.10.22.3 снова обязательна:** выпуск собирается в GitHub Actions (`build-release.yml`) и подписывается только после одобрения владельца в окружении `release`, ключ — секрет этого окружения. |
| R3 | ai_local.c:689 (`ai_server_ensure`) | С | Если агенту нужно окно 16384, а сервер поднят с меньшей ячейкой, сервер убивается `TerminateProcess` даже при идущих запросах (`g_aiActive>0`): параллельный запрос чата/распознавания чертежа обрывается ошибкой. **Исправлено (2026-10-23):** запросы, идущие к серверу, считаются (`g_aiSrvUsers`); перезапуск ради большего окна или новых настроек ждёт их конца (до срока своего запроса, отмена работает), новые запросы встают за ждущим, окно берётся наибольшее из нужных. Упавший сервер перезапускается сразу. |
| R4 | ai_local.c:791 и pad_extra.c:5900 | С | Оба `CreateProcessW` с `bInheritHandles=TRUE` без `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. Если llama-server стартует в момент, когда открыт канал OCR-помощника, он наследует его конец — `run_capture_in` не дождётся EOF до выхода llama-server (до 10 минут). |
| R5 | pad_extra.c:5872–5941 (`run_capture_in`) | С | `ReadFile` по каналу без тайм-аута; 30-секундный `WaitForSingleObject` стоит после чтения. Зависший помощник OCR (или его потомок, держащий канал) вешает поток OCR навсегда. Временный файл `%TEMP%\cursorpad-ocr.bmp` один на все запуски. **Исправлено (2026-10-23):** каналы читаются без блокировки (`pipe_take`: только то, что уже пришло; stdout и stderr по очереди), помощник с потомками — в задании `KILL_ON_JOB_CLOSE`, через 5 мин (`RUN_CAPTURE_MS`) снимается целиком; потомок, унёсший канал, больше не держит поток — вышел помощник, дочитали и всё. Снимок OCR — свой файл на каждый запуск. |
| R6 | pad_extra.c:4111–4115, 1122, 1316; files.c:1652, 1796; share.c:762, 795 | С | `search_thread` пишет UI-глобали (`g_plmCount`, `g_plmEsi[]`, `g_plmIds[]`, `g_resultFiles`) из рабочего потока, пока окно находок может их читать. Строки фиксированной длины — не падает, но возможны «рваные» строки и неверный выбор строки. Правильно — собрать результат в куче и передать в WM_SEARCH_DONE. |
| R7 | ai_ui.c:287, 367–372, 418, 451, 477–478, 488 | Н–С | Настройки ИИ (`g_aiModel`, `g_aiTier`, `g_aiCtx`, `g_aiGpu`, `g_aiSrvExe`) меняются в UI и читаются рабочим потоком в `ai_server_ensure` без замка; `ai_ui_unzip_thread` сам пишет `g_aiSrvExe` и `ai.txt` параллельно с `ai_save()` из UI (CREATE_ALWAYS, share 0 → один из них молча не записывается). После F1 при выборе архива llama.cpp во время загрузки модели распаковка может не перезаписать занятые DLL (сообщит «Не распаковался»). |
| R8 | ai_local.c:1480 (`ai_dl_say`), 730 (`g_aiKey`) | Н | `g_aiDl.msg` пишется рабочим потоком, читается UI без замка (рваный текст). Ключ `g_aiKey` перегенерируется при перезапуске сервера, пока другие запросы читают его в `ai_http`. |
| R9 | ai_agent_fs.c:182–254 (`ag_path`) | Н | `GetFullPathNameW` не раскрывает короткие имена 8.3: `C:\PROGRA~1\…` или `…\Local\CURSOR~1\…` обходят запрет Program Files / папки данных, если пользователь назвал корень (например `C:\`). Смягчено ручным «Применить», где виден путь. Лечится `GetLongPathNameW` для существующей части пути. |
| R10 | ai_local.c:1800–1806 (`ai_unzip`, PowerShell) | Н | Пути в `Expand-Archive -LiteralPath '%s'` не экранируются: апостроф в профиле (`C:\Users\O'Brien`) ломает команду (запасной путь после tar.exe). |
| R11 | Весь код: 821 вызов `_snwprintf` | С (системно) | Без явного `buf[N-1]=0` строка при переполнении не завершается. Большинство мест безопасны по длинам или ставят 0 сами; исправлены места с путями из каталогов/модели (F4). Остальное — кандидат на обёртку. |
| R12 | layout_fix.c:389–396; ai_chat.c:1933 (`Sleep(150)`) | Н | В потоке окон `Sleep` до 1 с (ожидание отпускания модификаторов) — при включённом эффекте клика хук мыши этого потока не отвечает. |
| R13 | ai_chat.c:1740, ai_avatar.c:89 | Н | `CoInitializeEx` при каждом вызове без парного `CoUninitialize` (счётчик растёт; вреда немного, но это несбалансированно). |
| R14 | cutting.c:1009, paint.c:1987 | Н | Кисть фона создаётся при каждом создании окна и не удаляется — утечка, если окно уничтожают и создают снова. (Код отрисовки — зона редизайна.) |
| R15 | pad_extra.c:11/21, cursorpad.c:116 / pad_extra.c:22; cursorpad.c ~2508 | Н | Совпадающие ID 152/154 и литерал 4714 в общей `draw_pad_button` (см. 1.5). |
| R16 | native/test_qwen_contract.py:63–64 ↔ native/agent/bridge.py:318, 358 | С | Тест подаёт рецензенту `{'covered':…,'missing':…}`, а `bridge.py` теперь требует схему `{'checks':{…}}` → `jsonschema.ValidationError`. Устаревший фикстур (или регресс протокола) — тест падает на текущем коде. |

### 2.3. Аватар (ai_avatar.c — зона ведущего агента, не правил)

| # | Где | Серь. | Суть |
|---|---|---|---|
| A1 | ai_avatar.c:850–851 | Н | `GetPrivateProfileIntW` возвращает 0 для отрицательных значений — место на мониторе слева/сверху от основного теряется при перезапуске. Читать строкой + `wcstol`. |
| A2 | ai_avatar.c:966, 975, 977, 993, 995, 1023 | Н | Сроки сравниваются как `now >= t`; через 49,7 сут. работы `GetTickCount` переполняется и моргание/прогулки/пузыри замирают. Нужно `(LONG)(now - t) >= 0`. |
| A3 | ai_avatar.c:803, 995–1009 | С (UX) | `WM_NCHITTEST → HTCAPTION` по всему квадрату: окно поверх всех перехватывает клики того, что под ним, и само переезжает (±140 px от «дома») — может накрыть чужую кнопку; клик по нему начинает перетаскивание. Стоит пропускать клики вне силуэта (`HTTRANSPARENT` по альфе кадра). |
| A4 | ai_avatar.c:873, 732–737, 1020 | Н | Таймер 80 мс и полная перерисовка кадра (ресэмплинг до 320² пикселей + `UpdateLayeredWindow`) идут в потоке окон постоянно, в том числе когда аватар скрыт (`ac_avatar_hide` таймер не останавливает) — лишние пробуждения CPU и конкуренция с хуком мыши. |
| A5 | ai_avatar.c:843, 864–865 | Н | Место по умолчанию считается от рабочей области основного монитора (`SPI_GETWORKAREA`), а прогулки — от монитора окна; при смене конфигурации мониторов «дом» может оказаться вне экрана до следующего перетаскивания. |
| A6 | атлас 2400×1800 BGRA | Н | DIB-секция атласа ~17 МБ живёт всё время работы программы. |

---

## 3. Рекомендуемые рефакторинги (не выполнялись), по убыванию пользы

1. **Доверие к общей папке** (R1): зашить отпечатки пакета Tesseract в exe на этапе сборки
   (генератор по образцу `build_agent_pack.py`) или брать из общей папки только файлы, отпечатки которых
   получены с GitHub; то же правило — для любого исполняемого кода из сетевой папки.
2. **Поток окон никогда не ждёт рабочих**: перевести остальные места, где UI берёт замки/ждёт процессы
   (`files_wait_idle` при выходе, ожидания в layout_fix), на флаги + сообщения, как в F1.
3. **Результаты рабочих потоков — только в куче через PostMessage** (R6, R7, R8): `search_thread`,
   `plm_lookup`, `files_search`, `share_lookup` возвращают структуру с массивами строк; UI атомарно подменяет
   свои `g_plm*`. Настройки ИИ — снимок структуры под `g_aiSrvLock`.
4. **Безопасная склейка строк** (R11): одна обёртка `fmt_path(out, cap, …)` → BOOL, проверяющая длину и всегда
   завершающая строку; заменить ею `_snwprintf` в путях. Альтернатива — `swprintf` (терминирует) с проверкой кода.
5. **Запуск дочерних процессов** (R4, R5): общая функция с `STARTUPINFOEX` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`,
   job-объектом и чтением каналов с тайм-аутом (overlapped/отдельный поток), уникальные временные файлы.
6. **Сервер модели**: не убивать сервер при `g_aiActive>0` (R3) — дождаться окончания запросов или держать
   один сервер с `-c 16384 -np 1` и короткими запросами чата.
7. **Разрезать unity build** хотя бы на ИИ-подсистему с явным интерфейсом (`ai.h` уже есть) — сейчас любой
   модуль может трогать `g_acTurns`, `g_agFs`, `g_plm*`; это и есть источник гонок R6/R7.
8. **Удалить мёртвую ветку старого навыка «Файлы»** (ai_files.c: `ac_files_run`, `ac_take_plan`, `ac_group_plan`,
   `ac_mkdir_handle`, `ac_xfer_handle`, `ac_cmd_run`) или явно подключить её к маршрутизатору — сейчас она
   почти недостижима, но поддерживается и содержит отдельные правила безопасности путей.
9. **Подпись обновлений** (R2): Authenticode или собственная подпись `version.txt` открытым ключом в exe.
10. **ID элементов**: развести 152/154, убрать литерал 4714 из `draw_pad_button` (R15).

---

## 4. Проверка

Сборка в отдельную папку (scratchpad), `public/` и `build.ps1` не трогались:

- `windres` + `gcc -O2 -Wall -Werror … cursorpad.c` — **OK**, без предупреждений (до и после правок).
- `chat_format_test.c` — **PASS** (RichEdit, Markdown, экранирование).
- `agent_files_test.c` (+ `match.c`) — **PASS**: 1386 проверок, перенос каталогов, junction/ACL отклоняются, межтомовый перенос.
- `office_test.c` (+ `match.c`) — **PASS** (реальный Excel: пересчёт, ведущие нули, без перезаписи).
- `word_format_test.c` (+ `match.c`, тестовый .docx) — **PASS** (реальный Word, отпечатки, резервная копия) — проверяет F7.
- `match_test.c` на малом наборе — работает, ожидаемые совпадения (100 % / 97 %).
- Пробник инструмента `read` (UTF-8 на границе 4096 байт): до правки отказ, после — чтение (F3).
- Python: `test_ai_web.py` (зонд `-DAC_WEB_TEST`) — **PASS**; `test_inventory_engine.py` (зонд `agent_probe.c`) — **PASS**;
  `test_zai_schema.py` и `test_agent_coverage.py` (под Python комплекта QwenPaw) — **PASS**;
  `test_qwen_contract.py` — **FAIL** (R16, не связано с правками C).
- Не запускались (нужны модель/llama-server, сеть, QWEN_TEST-сборка или Office-сценарии с моделью):
  `test_addons.py`, `test_addons_boundary.py`, `test_agent_inventory.py`, `test_agent_model.py`, `test_chat_intent.py`,
  `test_excel_plan.py`, `test_qwen_agent.py`, `test_qwen_vision.py`, `test_word_import.py`, `test_zai.py`, `qwen_test.c`.
