#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <winhttp.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sql.h>
#include <sqlext.h>
#include <wincrypt.h>

#ifndef WS_EX_NOACTIVATE
#define WS_EX_NOACTIVATE 0x08000000L
#endif
#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031D
#endif

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "winhttp")
#pragma comment(lib, "advapi32")
#pragma comment(lib, "odbc32")
#pragma comment(lib, "crypt32")

#define ID_PIN 101
#define ID_CLOSE 102
#define ID_EDIT 103
#define ID_CUR_K2 104
#define ID_CUR_K3 105
#define ID_CUR_K4 115 /* «Фея» */
#define ID_CUR_K5 120 /* «Дракон» */
#define ID_CUR_K6 121 /* «Чёрный кот» */
#define ID_CUR_K7 136 /* «Риас» */
#define ID_CUR_K8 137 /* «Тяночка» — аниме-девочка, make_anime.py */
#define ID_FX_BASE 400    /* 400..416: эффект при нажатии в Настройках (до 2026.10.14 — 160…) */
#define FX_OPTIONS 19    /* сколько вариантов в «Эффект при нажатии» */
#define ID_CHK_TESS 176   /* точное распознавание (Tesseract) */
#define ID_SEC_BASE 440   /* 440..445: вкладки Настроек */
#define SEC_COUNT 7
#define SEC_AI 5          /* вкладка «ИИ» (ai_ui.c) */
#define SEC_NORMS 6       /* вкладка «Нормирование» (norms_set.c) */
#define ID_CUR_ST_BASE 460 /* 460..466: персонажи «Ворон»…«Баклажан» (styles.c) */
#define ST_COUNT 7
#define ST_RAZREZ 3         /* «Разрез» — клинок вместо стрелки */
#define ST_KNIGHT ST_COUNT  /* рыцарь (Мечник) рисуется кодом, но кнопки стиля у него нет */
#define ST_SKIN0 7          /* g_skin первого из них; 7…13 */
#define SKIN_K8 (ST_SKIN0 + ST_COUNT) /* g_skin «Тяночки» (k8): после стилей, 14 */
#define SKIN_KIT (SKIN_K8 + 1)        /* «Современный» (cursor_kit.c), 15; ключ «m1» в cursor.txt */
#define ID_CUR_KIT 600      /* кнопка «Современный» на вкладке «Курсор» */
#define ID_KIT_BASE 601     /* 601..604: стиль набора — карточка «Курсор» на вкладке «Кастомизация» */
#define ID_KIT_SZ 605       /* 605..607: размер — обычный, крупнее, крупный */
#define ID_KIT_OUT 608      /* обводка с ореолом */
#define ID_KIT_SHADOW 609   /* тень */
#define ID_KIT_ACC 610      /* значки цветом темы */
#define ID_KIT_ANIM 611     /* ожидание крутится */
#define ID_KIT_FX 612       /* своя «Волна» при нажатии */
#define ID_KIT_PREVIEW 613  /* живой просмотр набора */
#define ID_PREVIEW 470      /* окошко просмотра курсора и эффекта */
#define ID_IDLE_BASE 480    /* 480..484: бой при бездействии — выкл, 30 с, 1, 2, 5 мин */
#define IDLE_OPTIONS 5
#define ID_IDLE_SHOW 485    /* «Показать бой сейчас» */
#define ID_IDLE_SZ 486      /* 486..488: бойцы мелкие, средние, крупные */
#define IDLE_SIZES 3
#define ID_IDLE_WATCH 489   /* «Режим просмотра»: бой идёт и при работе мышью */
#define ID_RING_BASE 490    /* 490..493: стиль панели Shift (вкладка «Кастомизация», convert_ring.c) */
#define RING_STYLES 4
#define ID_RING_BLUR 494    /* размытие снимка экрана под плитками */
#define ID_RING_DOTS 495    /* точки по контуру плиток */
#define ID_RING_ANIM 496    /* плавные анимации кольца */
#define ID_MIN 106
#define ID_OCR 107
#define ID_ALPHA_BG 108
#define ID_ALPHA_FG 109
#define ID_SETTINGS 110
#define ID_SYS_CUR 111
#define ID_SEARCH_EDIT 112
#define ID_SEARCH_GO 113
#define ID_ASK_TAB 114
#define ID_NARDY_BTN 190 /* «Нарды» — в Настройках */
#define ID_NOTES_TAB 191 /* «Блокнот»: режим заметок */
#define ID_MORE_BTN 193  /* «Ещё» (⋯): инструменты и действия, см. tools.c */
#define ID_ND_LIST 180
#define ID_ND_INVITE 181
#define ID_ND_ACCEPT 182
#define ID_ND_DECLINE 183
#define ID_ND_ROLL 184
#define ID_ND_UNDO 185
#define ID_ND_DONE 186
#define ID_ND_RESIGN 187
#define ID_ND_BACK 188
#define ID_ND_CANCEL 189
#define ID_CLIP 131
#define ID_CLIPCLR 153
#define ID_CLIPHIST 199 /* «▾» у буфера копии: пять последних копирований */
#define ID_MSG_LIST 210 /* окна сообщений коллегам, см. msg.c */
#define ID_MSG_TEXT 211
#define ID_MSG_TTL0 212 /* 212..214 — 30 с / 1 мин / 5 мин */
#define ID_MSG_SEND 215
#define ID_MSG_REPLY 216
#define ID_MSG_DISMISS 217
#define ID_MSG_CLEAR 218 /* «Очистить переписку» в чате */
#define ID_RT_BUILD 220 /* маршрутная ведомость, см. route.c */
#define ID_RT_SAVE 221
#define ID_RT_COPY 222
#define ID_RT_LOG 223
#define ID_RT_DUMP 224 /* «Выгрузка для проверки» */
#define ID_RT_TPMAT 225 /* галочка «Материалы из ТП» */
#define ID_SH_LIST 230 /* шашки, см. shashki.c */
#define ID_SH_INVITE 231
#define ID_SH_ACCEPT 232
#define ID_SH_DECLINE 233
#define ID_SH_DRAW 234
#define ID_SH_DRAWOK 235
#define ID_SH_RESIGN 236
#define ID_SH_BACK 237
#define ID_SH_CANCEL 238
#define ID_SHASHKI_BTN 239 /* «Шашки» — в Настройках, рядом с «Нардами» */
#define ID_MB_LIST 340 /* морской бой, см. morskoy.c */
#define ID_MB_INVITE 341
#define ID_MB_ACCEPT 342
#define ID_MB_DECLINE 343
#define ID_MB_CANCEL 344
#define ID_MB_SHUFFLE 345
#define ID_MB_READY 346
#define ID_MB_RESIGN 347
#define ID_MB_BACK 348
#define ID_MORSKOY_BTN 349 /* «Морской бой» — в Настройках, рядом с «Шашками» */
#define ID_SX_LIST 350 /* шахматы, см. shahmaty.c */
#define ID_SX_INVITE 351
#define ID_SX_ACCEPT 352
#define ID_SX_DECLINE 353
#define ID_SX_DRAW 354
#define ID_SX_DRAWOK 355
#define ID_SX_RESIGN 356
#define ID_SX_BACK 357
#define ID_SX_CANCEL 358
#define ID_SHAHMATY_BTN 359 /* «Шахматы» — в Настройках, рядом с «Морским боем» */
#define ID_HOTKEYS 240     /* «Горячие клавиши…» — в Настройках */
#define ID_ANSPIN 154
#define TIMER_FOLLOW 1
#define TIMER_SAVE 2
#define TIMER_PASTE 3
#define TIMER_STATUS 4
#define TIMER_FILES_PLAN 10 /* раз в минуту: не пора ли полный обход папки */
#define TIMER_IDLE_FIGHT 40 /* раз в секунду: не пора ли начать бой при бездействии */
#define ID_FILES_NOTIFY 157
#define ID_FILES_STAT 138 /* «Индекс: —» и его полоса хода: раньше 136/137, а 136 — это ID_CUR_K7 «Риас» в том же окне */
#define ID_FILES_BAR 139
#define ID_WATCH_EDIT 158
#define ID_WATCH_BROWSE 159
#define ID_DES_CODES 172 /* коды своих обозначений: распознавание правит букву кода */
#define ID_SHARE_EDIT 155
#define ID_SHARE_BROWSE 156
#define ID_SHARE_SERVE 169
#define HOTKEY_TOGGLE 1
#define HOTKEY_SNIP_BASE 10
#define HOTKEY_CURSOR 2
#define HOTKEY_OCR 3
#define HOTKEY_MIN 4
#define HOTKEY_SEARCH 5
#define HOTKEY_LAYOUT 6
/* функции с горячими клавишами (hotkeys.c): клавиши можно сменить или убрать */
enum { HK_TOGGLE, HK_MIN, HK_SEARCH, HK_OCR, HK_CURSOR, HK_LAYOUT, HK_SNIP, HK_COUNT };
static wchar_t g_hkName[HK_COUNT][40]; /* на чём работает сейчас: «F6», «Ctrl+Alt+R»; пусто — клавиши нет */
#define SNIP_COUNT 9
#define CUR_FRAMES 8
#define WM_TRAY (WM_APP + 1)
#define TRAY_UID 1
#define OCR_NORMAL_ID 32512
#define OCR_IBEAM_ID 32513
#define OCR_WAIT_ID 32514
#define OCR_CROSS_ID 32515
#define OCR_UP_ID 32516
#define OCR_SIZENWSE_ID 32642
#define OCR_SIZENESW_ID 32643
#define OCR_SIZEWE_ID 32644
#define OCR_SIZENS_ID 32645
#define OCR_SIZEALL_ID 32646
#define OCR_NO_ID 32648
#define OCR_HAND_ID 32649
#define OCR_APPSTARTING_ID 32650
#define OCR_HELP_ID 32651
#define OCR_PIN_ID 32671
#define OCR_PERSON_ID 32672

#define WND_W 312
#define WND_H 250
#define TITLE_H 44
#define MODE_H 38 /* ряд под шапкой: «Блокнот | Поиск», ⋯ и ⚙ (с 2026.09.23.41) */
#define FOOT_H 22 /* внизу — строка подсказки */
#define CLIP_H 26
#define PAD 12
#define GUTTER 26
#define SET_W 600 /* слева разделы, справа страница (с 2026.10.21; было 356 — шесть вкладок в ряд) */
#define SET_FOOT_H 34 /* внизу окна Настроек — подвал с версией, прокрутка под него не заходит */
/* полная высота содержимого Настроек считается при раскладке (g_setContentH):
   она зависит от того, какие разделы раскрыты; окно прокручивается, если выше экрана */
#define ASK_W 312
#define ASK_H 224 /* room for the drawn header */
#define ID_THEME_BASE 140
#define THEME_COUNT 9

static COLORREF COL_PAPER = RGB(255, 255, 255);
static COLORREF COL_PAPER_DARK = RGB(243, 244, 246);
static COLORREF COL_INK = RGB(17, 24, 39);
static COLORREF COL_MUTED = RGB(107, 114, 128);
static COLORREF COL_SAGE = RGB(37, 99, 235);
static COLORREF COL_LINE = RGB(226, 229, 233);

/* Тема — не только цвет. Меняются шрифт, форма кнопок, вид шапок и
   углы окон: с одной палитрой темы выглядели перекрашенной одной и той же
   программой. Тёмные убраны. */
#define BTN_SOFT 0  /* мягкая заливка и тонкая рамка */
#define BTN_LINE 1  /* только контур, как на чертеже */
#define BTN_TONE 2  /* цветная подложка без рамки */
#define BTN_DOTS 4  /* стекло: мягкая заливка и пунктир из точек по контуру (как у плиток панели Shift) */
#define HEAD_BAND 0  /* шапка полосой */
#define HEAD_RULE 1  /* шапка на фоне, под ней линия */
#define HEAD_PLAIN 2 /* без полосы и линии, заголовок цветом */
#define BTN_KNIGHT 3  /* стальные пластины с заклёпками, сургучные печати */
#define HEAD_BANNER 3 /* алое знамя с раздвоенными концами */
/* цвета рыцарской темы, которых нет в палитре остальных */
#define KN_GOLD RGB(201, 162, 39)
#define KN_GOLD_LT RGB(242, 214, 128)
#define KN_GOLD_DK RGB(128, 96, 18)
#define KN_CRIMSON RGB(139, 30, 30)
#define KN_CRIMSON_DK RGB(88, 14, 14)
#define KN_STEEL_HI RGB(222, 221, 212)
#define KN_STEEL_LO RGB(152, 150, 140)
#define KN_STEEL_BD RGB(62, 56, 46)
#define KN_RIVET RGB(88, 80, 66)

typedef struct {
  COLORREF paper, dark, ink, muted, sage;
  const wchar_t *name;
  const wchar_t *face;     /* кнопки и текст */
  const wchar_t *faceAlt;  /* если основного в системе нет */
  const wchar_t *faceHead; /* «CursorPad» и крупные заголовки */
  const wchar_t *faceSmall;
  int radius;              /* скругление кнопок: 0 — прямые, -1 — «таблетка» */
  int btn, head;
  int corners;             /* углы окон: 1 прямые, 2 круглые, 3 чуть скруглённые */
  BOOL caps;               /* заголовки панелей прописными */
  BOOL ornate;             /* фактура, рамки, герб и свои названия окон */
} PadTheme;

static const PadTheme kThemes[THEME_COUNT] = {
    /* привычный вид — как было до сих пор */
    {RGB(255, 255, 255), RGB(243, 244, 246), RGB(17, 24, 39), RGB(107, 114, 128), RGB(37, 99, 235),
     L"Обычная", L"Segoe UI Variable Text", L"Segoe UI", L"Segoe UI Variable Display",
     L"Segoe UI Variable Small", 8, BTN_SOFT, HEAD_BAND, 2, FALSE},
    /* тёплая бумага, шрифт с засечками, линейка под шапкой */
    {RGB(251, 248, 241), RGB(241, 234, 219), RGB(43, 33, 24), RGB(125, 110, 95), RGB(154, 91, 19),
     L"Бумага", L"Georgia", L"Cambria", L"Georgia", L"Georgia", 3, BTN_SOFT, HEAD_RULE, 3,
     FALSE},
    /* округлое и мягкое: кнопки-таблетки без рамок */
    {RGB(247, 250, 249), RGB(234, 243, 240), RGB(15, 31, 28), RGB(95, 122, 116), RGB(13, 148, 136),
     L"Мягкая", L"Segoe UI Variable Text", L"Segoe UI", L"Segoe UI Variable Display",
     L"Segoe UI Variable Small", -1, BTN_TONE, HEAD_PLAIN, 2, FALSE},
    /* чертёжный лист: прямые углы, контуры, узкий технический шрифт,
       заголовки прописными — как основная надпись. С 2026.10.30 — вид по умолчанию
       (вариант А «Чертёж» макета): чёрные линии, тёплый серый вторым фоном и акцент
       «красным карандашом»; главное окно и меню «Ещё» рисуются по-своему (chertezh_on) */
    {RGB(255, 255, 255), RGB(241, 239, 232), RGB(22, 24, 29), RGB(75, 79, 87), RGB(180, 56, 10),
     L"Чертёж", L"Bahnschrift", L"Segoe UI", L"Bahnschrift", L"Bahnschrift", 0, BTN_LINE,
     HEAD_RULE, 1, TRUE},
    /* рыцарская: пергамент с фактурой, алые знамёна с золотом, стальные
       пластины вместо кнопок, готический шрифт заголовков, щит у названия */
    {RGB(241, 228, 195), RGB(228, 211, 168), RGB(43, 27, 14), RGB(118, 88, 54), KN_CRIMSON,
     L"Рыцарская", L"Palatino Linotype", L"Georgia", L"Old English Text MT",
     L"Palatino Linotype", 2, BTN_KNIGHT, HEAD_BANNER, 1, FALSE, TRUE},
    /* с 2026.10.26 — четыре темы с вкладки «Кастомизация»; с 2026.10.29 переделаны:
       спокойные цельные палитры, у тёмных — тёмные полосы прокрутки, без пунктира и узкого шрифта */
    /* стекло: светлая холодная бумага с лёгкой голубизной, крупное скругление, приглушённый
       сине-стальной акцент; кнопки — мягкая заливка (пунктир из точек на каждой кнопке убран) */
    {RGB(246, 248, 251), RGB(235, 239, 245), RGB(26, 32, 44), RGB(90, 100, 118), RGB(62, 104, 178),
     L"Стекло", L"Segoe UI Variable Text", L"Segoe UI", L"Segoe UI Variable Display",
     L"Segoe UI Variable Small", 12, BTN_SOFT, HEAD_PLAIN, 2, FALSE, FALSE},
    /* графит: нейтральная тёмная, без синевы в сером; акцент — спокойный голубой; заголовок окна тёмный (DWM) */
    {RGB(32, 33, 36), RGB(41, 43, 47), RGB(228, 230, 233), RGB(150, 155, 163), RGB(122, 162, 224),
     L"Графит", L"Segoe UI Variable Text", L"Segoe UI", L"Segoe UI Variable Display",
     L"Segoe UI Variable Small", 6, BTN_SOFT, HEAD_RULE, 3, FALSE, FALSE},
    /* синька: чертёж наоборот — светлые линии по приглушённой синей бумаге (не яркий ультрамарин),
       прямые углы, прописные; акцент — мягкий голубой вместо кислотного циана */
    {RGB(27, 42, 62), RGB(34, 51, 74), RGB(226, 234, 243), RGB(150, 170, 194), RGB(132, 186, 222),
     L"Синька", L"Bahnschrift", L"Segoe UI", L"Bahnschrift", L"Bahnschrift", 0, BTN_LINE,
     HEAD_RULE, 1, TRUE, FALSE},
    /* цех: тёплая тёмная сталь и приглушённый янтарь; шрифт Bahnschrift обычной ширины
       (узкий SemiCondensed мелко читался), шапка полосой */
    {RGB(36, 36, 37), RGB(45, 45, 47), RGB(234, 232, 227), RGB(163, 159, 151), RGB(214, 154, 66),
     L"Цех", L"Bahnschrift", L"Segoe UI", L"Bahnschrift SemiBold", L"Bahnschrift", 4, BTN_SOFT,
     HEAD_BAND, 3, TRUE, FALSE},
};

static COLORREF blend_rgb(COLORREF a, COLORREF b, int t) {
  if (t < 0) t = 0;
  if (t > 256) t = 256;
  return RGB((GetRValue(a) * (256 - t) + GetRValue(b) * t) / 256,
             (GetGValue(a) * (256 - t) + GetGValue(b) * t) / 256,
             (GetBValue(a) * (256 - t) + GetBValue(b) * t) / 256);
}

static HWND g_hwnd;
static HWND g_edit;
static HWND g_clipEdit;
static HWND g_clipClr;
static HWND g_clipHistBtn;
/* пять последних копирований (только в памяти: в буфер копируют и пароли) */
#define CLIP_HIST 5
static wchar_t g_clipHist[CLIP_HIST][400];
static int g_clipHistN;
static HWND g_pin;
static HWND g_close;
static HWND g_btnK2;
static HWND g_btnK3;
static HWND g_btnK4;
static HWND g_btnK5;
static HWND g_btnK6;
static HWND g_btnK7;
static HWND g_btnK8;
static HWND g_btnFx[FX_OPTIONS]; /* эффект при нажатии: как у курсора, нет, звёздочки, пламя, царапины, разрез */
static HWND g_btnSt[ST_COUNT];       /* стили, нарисованные кодом */
static HWND g_btnSec[SEC_COUNT];     /* вкладки Настроек; индекс — что на вкладке, не место */
static HWND g_preview;               /* просмотр курсора и эффекта (styles.c) */
static int g_tab;                    /* открытая вкладка Настроек (что на ней, 0…SEC_COUNT-1) */
static int g_tabOrder[SEC_COUNT] = {0, 1, 2, 3, 4, 5, 6}; /* слева направо: какая вкладка на каком месте */
static HWND g_chkTess;
static HWND g_min;
static HWND g_ocr;
static HWND g_btnUp;
static HWND g_btnHk; /* «Горячие клавиши…» — окно hotkeys.c */
static HWND g_btnTheme[THEME_COUNT];
#define THEME_CHERTEZH 3 /* «Чертёж» — вид по умолчанию с 2026.10.30 */
static int g_theme = THEME_CHERTEZH;
/* главное окно и меню «Ещё» как чертёжный лист: ячейки, номера строк, штамп внизу */
static BOOL chertezh_on(void) { return g_theme == THEME_CHERTEZH; }
static HWND g_tbBg;
static HWND g_tbFg;
static HWND g_btnIdle[IDLE_OPTIONS]; /* бой при бездействии: через сколько */
static HWND g_btnIdleShow;           /* «Показать бой сейчас» */
static HWND g_btnIdleSz[IDLE_SIZES];  /* размер бойцов */
static HWND g_btnIdleWatch;           /* режим просмотра */
/* панель Shift (кольцо перетаскивания, convert_ring.c): стиль и переключатели — вкладка «Кастомизация» */
static HWND g_btnRing[RING_STYLES], g_btnRingBlur, g_btnRingDots, g_btnRingAnim;
static int g_ringStyle; /* 0 Матовое стекло, 1 Лепестки, 2 Тёмное стекло, 3 Тонкое кольцо */
static BOOL g_ringBlur = TRUE, g_ringDots = TRUE, g_ringAnim = TRUE;
/* «Современный» набор курсоров (cursor_kit.c): кнопка на «Курсоре» и карточка «Курсор» на «Кастомизации» */
#define KIT_STYLES 4
#define KIT_SIZES 3
static HWND g_btnKit, g_btnKitSt[KIT_STYLES], g_btnKitSz[KIT_SIZES], g_btnKitOut, g_btnKitShadow, g_btnKitAcc,
    g_btnKitAnim, g_btnKitFx;
static HWND g_kitPreview;
static int g_kitStyle; /* 0 Светлый, 1 Тёмный, 2 Акцент, 3 Стекло */
static int g_kitSize;  /* 0 ×1, 1 ×1,25, 2 ×1,5 */
static BOOL g_kitOutline = TRUE, g_kitShadow = TRUE, g_kitThemeAcc = TRUE, g_kitAnim = TRUE, g_kitFx = TRUE;
/* окно Настроек тянется мышью за край (с 2026.10.29): выбранные ширина и высота, точки
   экрана; 0 — по умолчанию (SET_W и SET_H_DEF). Высота — предел: вкладка короче — окно ниже */
static int g_setUserW, g_setUserH;
static HWND g_btnSet;
static HWND g_btnAsk;
static HWND g_btnNd; /* кнопка «Нарды» в Настройках */
static HWND g_btnSh; /* и «Шашки» рядом с ней */
static HWND g_btnMb; /* и «Морской бой» */
static HWND g_btnSx; /* и «Шахматы» */
static HWND g_btnNotes; /* «Блокнот» внизу окна */
static HWND g_btnMore;  /* «Ещё» внизу окна: страницы из giriaja-hall */
/* Режим окна: 0 — блокнот (заметки), 1 — поиск (поле, «Спросить» и где
   искать — прямо в окне, без отдельного окна «Поиск»). Запоминается. */
static int g_padMode;
static HWND g_setHwnd;
static HWND g_askHwnd;
static HWND g_btnSys;
static HWND g_searchEdit;
static HWND g_searchGo;
static BOOL g_askOpen = FALSE;
static WNDPROC g_oldSearch;
static HWND g_btnAi;
static HWND g_btnPlm;
static HWND g_plmServer;
static HWND g_plmDb;
static HWND g_plmUser;
static HWND g_plmPass;
static HWND g_btnFiles;
static HWND g_filesRootEdit;
static HWND g_btnIdx;
static HWND g_filesStat;
static HFONT g_fontMono;
static HFONT g_fontMonoSm; /* «Чертёж»: номера строк, штамп, версия и клавиши — моноширинным */
/* Карточка теперь в своём окне: находки не затираются, и можно
   смотреть список и карточку рядом. У каждого окна своё место, размер
   и масштаб текста. */
static HWND g_card, g_cardEdit;
static int g_cardPt = 10;
static int g_cardX, g_cardY;
static HFONT g_cardFontZoom;
static HWND g_filesBar;
static HWND g_chkAuto;
static HWND g_chkAnsPin;
static HWND g_chkNotify; /* «Сообщать о новых и изменённых файлах» */
static HWND g_answer;
static HWND g_answerList;
static HWND g_pick;
static HANDLE g_mutex;
/* программа закрывается (в том числе ради обновления): фоновые потоки больше
   ничего не забирают из общей папки — показать это уже некому, а новая
   версия заберёт сама */
static volatile LONG g_quitting;
static WNDPROC g_oldEdit;
static BOOL g_ownClip = FALSE;
static HFONT g_fontUi;
static HFONT g_fontBody;
static HFONT g_fontSmall;
static HFONT g_fontDisplay;
static HBRUSH g_paper;
static HBRUSH g_paperDark;
static NOTIFYICONDATAW g_nid;
static BOOL g_follow = TRUE;
static BOOL g_dirty = FALSE;
static BOOL g_notesTruncated = FALSE; /* loaded file was bigger than the box */
static int g_ansW = 0, g_ansH = 0;   /* запомненный размер окна находок */
static int g_cardW = 0, g_cardH = 0; /* у карточки он свой: она длиннее списка */
static int g_ansPt = 10;             /* масштаб текста ответа, Ctrl+колесо */
static BOOL g_ansKeepPos;            /* окно ответа: держать на месте, а не у курсора */
static int g_ansX = 0, g_ansY = 0;   /* где его оставили */
static const wchar_t *g_ansTitle; /* set when the panel shows something other than hits */
static BOOL g_ansPlain; /* просто текст (отчёт, «что нового»): внизу только «Закрыть» */
static BOOL g_trayAdded = FALSE;
static BOOL g_hidden = FALSE;
/* Слежение за курсором опрашивалось 100 раз в секунду всегда — даже когда
   окно закреплено, спрятано или мышь просто стоит. На ноутбуке это заметно
   по батарее. Частота теперь подстраивается под то, что происходит. */
static UINT g_followMs = 10;
static int g_followIdle;
static POINT g_lastCur;
static BOOL g_overBot; /* курсор на помощнике мини-ИИ: блокнот спрятан, чтобы не заслонять робота */

static void follow_rate(HWND h, UINT ms) {
  if (ms == g_followMs) return;
  g_followMs = ms;
  SetTimer(h, TIMER_FOLLOW, ms, NULL);
}
static double g_x, g_y;
static int g_ww = WND_W, g_hh = WND_H;
static int g_offx = 22, g_offy = 28;
static wchar_t g_notesPath[MAX_PATH];
static wchar_t g_prefPath[MAX_PATH];
static wchar_t g_dataDir[MAX_PATH];
static wchar_t g_imgDir[MAX_PATH];
static wchar_t g_status[160];
static wchar_t g_ocrNote[160]; /* why recognition produced nothing */
/* Распознанный текст показывался в окне находок и затирал выжимку.
   Теперь у него своё окно — и текст в нём можно править перед поиском:
   распознавание путает «О» с нулём чаще, чем хотелось бы. */
static HWND g_ocrWnd, g_ocrEdit;
static int g_ocrX, g_ocrY, g_ocrW, g_ocrH;
static int g_ocrPt = 11;
static HFONT g_ocrFontZoom;
static BOOL g_statusOn = FALSE;
static HINSTANCE g_inst;
/* значок программы (с 2026.10.31): cursorpad.ico из make_icon.py, ICON 1 в cursorpad.rc —
   у exe и ярлыка его показывает Проводник; окнам — классам (hIcon/hIconSm) и WM_SETICON
   при показе (ui_chrome_once), трею — малый. Каждый размер грузится один раз */
#define IDI_CURSORPAD 1
static HICON app_icon(BOOL small) {
  static HICON big, sm;
  HICON *h = small ? &sm : &big;
  if (!*h) {
    int cx = GetSystemMetrics(small ? SM_CXSMICON : SM_CXICON), cy = GetSystemMetrics(small ? SM_CYSMICON : SM_CYICON);
    *h = (HICON)LoadImageW(g_inst ? g_inst : GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_CURSORPAD), IMAGE_ICON, cx, cy,
                           LR_DEFAULTCOLOR);
    if (!*h) *h = LoadIconW(NULL, IDI_APPLICATION);
  }
  return *h;
}
static int g_skin = 1; /* 0 system, 1 sword, 2 gauntlet, 3 fairy, 4 dragon, 5 black cat, 6 rias,
                          7…13 нарисованные кодом стили (styles.c), последний — «Баклажан» */
static BOOL g_sparkle = TRUE; /* у «Феи» — звёздочки при нажатии */
static BOOL g_flame = TRUE;   /* у «Дракона» — пламя при нажатии */
static int g_catFx = 1;       /* у «Чёрного кота» при нажатии: 0 ничего, 1 царапины, 2 звёздочки как у Феи */
/* эффект при нажатии, выбранный отдельно от курсора (меню → «Эффект при нажатии»):
   -1 — как у выбранного курсора, как раньше; 0 нет; 1 звёздочки; 2 пламя; 3 царапины;
   4 разрез (красный, в духе Сукуны; изредка большой разрез и раскол экрана);
   5 кольца; 6 конфетти; 7 молния; 8 сердечки.
   Так любой курсор можно сочетать с любым эффектом. */
static int g_fxPick = -1;
/* бой Годжо и Сукуны (idle_fight.c), если мышь и клавиатуру не трогали
   столько секунд; 0 — выключен */
static int g_idleSec = 60;
static const int kIdleSec[IDLE_OPTIONS] = {0, 30, 60, 120, 300};
/* рост бойцов: 0 — мелкие, 1 — средние (десятая часть высоты монитора), 2 — крупные */
static int g_idleSize = 1;
/* режим просмотра: бой идёт поверх экрана всё время и не прячется от мыши и клавиш
   (окно боя пропускает щелчки — работать он не мешает); 0 — обычный бой при бездействии */
static int g_idleWatch = 0;
static int g_balloonKind; /* куда ведёт щелчок по всплывашке: 0 — изменения в папке, 1 — нарды, 5 — шашки, 7 — морской бой, 8 — шахматы */
static HCURSOR g_staticCur[5];
static HCURSOR g_ibeamCur[5];
static HCURSOR g_handCur[5];
static HCURSOR g_helpCur[5];
static HCURSOR g_noCur[5];
static HCURSOR g_crossCur[5];
static HCURSOR g_sizeweCur[5];
static HCURSOR g_sizensCur[5];
static HCURSOR g_nwseCur[5];
static HCURSOR g_neswCur[5];
static HCURSOR g_allCur[5];
static HCURSOR g_upCur[5];
static HCURSOR g_pinCur[5];
static HCURSOR g_personCur[5];
static int g_alphaFollow = 180;
static int g_alphaPinned = 250;
static BOOL g_cursorOn = FALSE;
static int g_engine = 3; /* 0 wiki, 1 ddg, 2 yandex, 3 mini-ai, 4 plm, 5 files */
static wchar_t g_filesRoot[MAX_PATH];
static wchar_t g_plmHost[96] = L"um-splmsrv";
static wchar_t g_sqlHost[96] = L"UM-SQLSRV";
static wchar_t g_plmPort[16] = L"4450";
static wchar_t g_plmDatabase[96] = L"";
static wchar_t g_sqlUser[96] = L"";
static wchar_t g_sqlPass[128] = L"";
static wchar_t g_szBridge[MAX_PATH]; /* plm.txt: soyuz_bridge — мост cp-soyuz.exe (soyuz_make.c) */
/* общая папка отдела: через неё коллеги без логина получают PLM (share.c) */
static wchar_t g_shareRoot[MAX_PATH];
static BOOL g_shareServe; /* раздавать PLM коллегам через этот компьютер */
static HWND g_shareEdit, g_chkServe;
#define PLM_ROWS 200  /* result rows held for the list view */
#define PLM_LINK 4096 /* сетевые пути бывают длиннее тысячи знаков */
#define PLM_COL1 260
#define PLM_COL2 640
/* Находки одного поиска целиком. Поток поиска (и раздача PLM коллегам)
   собирает их в своей копии в куче; окно находок показывает g_plmView, и его
   меняет только поток окон — целиком, по WM_SEARCH_DONE (search_done_take).
   Раньше поток поиска писал прямо в строки, которые окно в это время рисовало
   и сортировало: «рваные» строки и не та строка под щелчком. */
typedef struct {
  int count;
  BOOL files; /* строки — файлы архива, а не объекты PLM */
  wchar_t lastLink[PLM_LINK];
  wchar_t links[PLM_ROWS][PLM_LINK];
  wchar_t esi[PLM_ROWS][PLM_COL1];
  wchar_t tp[PLM_ROWS][PLM_COL2];
  long ids[PLM_ROWS];    /* InfoObjectId каждой строки — для карточки */
  int tmpl[PLM_ROWS];    /* шаблон строки: по нему видно, где ТП */
  long tpId[PLM_ROWS];   /* id техпроцесса, сведённого в эту строку */
  long realId[PLM_ROWS]; /* собственный id строки: у ТП он свой, а открывается родитель */
  wchar_t pf[PLM_ROWS][PLM_COL1]; /* заготовки изделия строки (ProductPreformsCard) */
} PlmRows;
static PlmRows g_plmView; /* что в окне находок — только для потока окон */
#define g_plmCount g_plmView.count
#define g_resultFiles g_plmView.files
#define g_plmLastLink g_plmView.lastLink
#define g_plmLinks g_plmView.links
#define g_plmEsi g_plmView.esi
#define g_plmTp g_plmView.tp
#define g_plmIds g_plmView.ids
#define g_plmTmpl g_plmView.tmpl
#define g_plmTpId g_plmView.tpId
#define g_plmRealId g_plmView.realId
#define g_plmPf g_plmView.pf
static BOOL g_autostart = FALSE;
static void show_status(const wchar_t *text);
static void toggle_settings(void);
static void save_cursor_pref(void);
static void set_skin(int skin);
static void fx_sync(void); /* fairy_fx.c: звёздочки у «Феи», пламя у «Дракона» */
static void nardy_show(void); /* nardy.c: короткие нарды через общую папку */
static void shashki_show(void); /* shashki.c: русские шашки — так же */
static void morskoy_show(void); /* morskoy.c: морской бой — так же */
static void shahmaty_show(void); /* shahmaty.c: шахматы — так же */
static void start_lookup(const wchar_t *q);
static BOOL clipboard_text(wchar_t *out, int n);
static void search_web(const wchar_t *q);
static void start_ocr_pick(void);
static void mark_cursor_dirty(BOOL on);
static void restore_if_stale_lock(void);
static void apply_scheme_slots(void);
static void update_engine_buttons(void);
static BOOL ensure_single_instance(void);
static void close_other_windows(HWND main);
static LONG WINAPI on_crash(EXCEPTION_POINTERS *ex);
static BOOL autostart_get(void);
static void autostart_set(BOOL on);
static void create_answer(HWND owner);
static void create_card(HWND owner);
static void create_ocr(HWND owner);
static void show_ocr_text(const wchar_t *text);
static void show_answer_text(const wchar_t *text);
static void note_margins(void);

#include "ui_theme.c" /* общий вид: палитра, рисование, хром окон, анимации */

static void apply_dpi(HWND hwnd) {
  UINT dpi = 96;
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32) {
    typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
    GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
    if (fn) dpi = fn(hwnd);
  }
  g_ww = MulDiv(WND_W, (int)dpi, 96);
  g_hh = MulDiv(WND_H, (int)dpi, 96);
}

static void get_work_area(POINT pt, RECT *wa) {
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);
  if (mon && GetMonitorInfoW(mon, &mi)) {
    *wa = mi.rcWork;
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, wa, 0);
  }
}

static void clamp_to_work(int *x, int *y, POINT cursor) {
  RECT wa;
  get_work_area(cursor, &wa);
  int tx = *x;
  int ty = *y;
  if (tx + g_ww > wa.right) tx = cursor.x - g_ww - 16;
  if (ty + g_hh > wa.bottom) ty = cursor.y - g_hh - 16;
  if (tx < wa.left) tx = wa.left + 8;
  if (ty < wa.top) ty = wa.top + 8;
  if (tx + g_ww > wa.right) tx = wa.right - g_ww - 8;
  if (ty + g_hh > wa.bottom) ty = wa.bottom - g_hh - 8;
  *x = tx;
  *y = ty;
}

static void set_clickthrough(BOOL enable) {
  LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
  if (enable) ex |= WS_EX_TRANSPARENT;
  else ex &= ~WS_EX_TRANSPARENT;
  SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
  SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void set_alpha(BYTE a) {
  SetLayeredWindowAttributes(g_hwnd, 0, a, LWA_ALPHA);
}

static void update_pin_label(void) {
  SetWindowTextW(g_pin, g_follow ? L"Закрепить" : L"Следовать");
}

static void notes_path(void) {
  wchar_t dir[MAX_PATH];
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, dir))) {
    GetTempPathW(MAX_PATH, dir);
  }
  wchar_t folder[MAX_PATH];
  _snwprintf(folder, MAX_PATH, L"%s\\CursorPad", dir);
  CreateDirectoryW(folder, NULL);
  lstrcpynW(g_dataDir, folder, MAX_PATH);
  _snwprintf(g_imgDir, MAX_PATH, L"%s\\img", folder);
  CreateDirectoryW(g_imgDir, NULL);
  _snwprintf(g_notesPath, MAX_PATH, L"%s\\notes.txt", folder);
  _snwprintf(g_prefPath, MAX_PATH, L"%s\\cursor.txt", folder);
}

static void restore_system_cursor(void) {
  SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, 0);
  g_cursorOn = FALSE;
  mark_cursor_dirty(FALSE);
}

static void exe_dir(wchar_t *out, int n) {
  GetModuleFileNameW(NULL, out, n);
  wchar_t *slash = wcsrchr(out, L'\\');
  if (slash) *slash = 0;
}

static HCURSOR load_cur_file(const wchar_t *rel) {
  wchar_t path[MAX_PATH];
  if (g_dataDir[0]) {
    _snwprintf(path, MAX_PATH, L"%s\\%s", g_dataDir, rel);
    HCURSOR c = (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE);
    if (c) return c;
  }
  wchar_t dir[MAX_PATH];
  exe_dir(dir, MAX_PATH);
  _snwprintf(path, MAX_PATH, L"%s\\%s", dir, rel);
  return (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE);
}

static BOOL extract_rcdata(int id, const wchar_t *path) {
  HRSRC rs = FindResourceW(g_inst, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (!rs) return FALSE;
  HGLOBAL hg = LoadResource(g_inst, rs);
  if (!hg) return FALSE;
  DWORD sz = SizeofResource(g_inst, rs);
  const void *p = LockResource(hg);
  if (!p || !sz) return FALSE;
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  DWORD w = 0;
  WriteFile(f, p, sz, &w, NULL);
  CloseHandle(f);
  return w == sz;
}

/* «Риас» (make_rias.py): анимирован весь набор, каждый вид — свой .ani,
   в exe — RCDATA 360… в этом порядке. Пиксель-арт, размеры 32 / 48 / 64. */
static const struct {
  const wchar_t *name;
  int ocr;
} kRias[] = {{L"static", OCR_NORMAL_ID},     {L"ibeam", OCR_IBEAM_ID},     {L"hand", OCR_HAND_ID},
             {L"help", OCR_HELP_ID},         {L"no", OCR_NO_ID},           {L"cross", OCR_CROSS_ID},
             {L"sizewe", OCR_SIZEWE_ID},     {L"sizens", OCR_SIZENS_ID},   {L"sizenwse", OCR_SIZENWSE_ID},
             {L"sizenesw", OCR_SIZENESW_ID}, {L"sizeall", OCR_SIZEALL_ID}, {L"up", OCR_UP_ID},
             {L"pin", OCR_PIN_ID},           {L"person", OCR_PERSON_ID},   {L"wait", OCR_WAIT_ID},
             {L"app", OCR_APPSTARTING_ID}};
#define RIAS_SLOTS ((int)(sizeof(kRias) / sizeof(kRias[0])))

static void extract_payloads(void) {
  if (!g_dataDir[0]) return;
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH, L"%s\\k2_wait.ani", g_dataDir);
  extract_rcdata(300, path);
  _snwprintf(path, MAX_PATH, L"%s\\k3_wait.ani", g_dataDir);
  extract_rcdata(301, path);
  _snwprintf(path, MAX_PATH, L"%s\\ocr.ps1", g_dataDir);
  extract_rcdata(302, path);
  _snwprintf(path, MAX_PATH, L"%s\\CursorPadOcr.exe", g_dataDir);
  extract_rcdata(305, path);
  _snwprintf(path, MAX_PATH, L"%s\\k2_app.ani", g_dataDir);
  extract_rcdata(303, path);
  _snwprintf(path, MAX_PATH, L"%s\\k3_app.ani", g_dataDir);
  extract_rcdata(304, path);
  _snwprintf(path, MAX_PATH, L"%s\\k4_wait.ani", g_dataDir);
  extract_rcdata(306, path);
  _snwprintf(path, MAX_PATH, L"%s\\k4_app.ani", g_dataDir);
  extract_rcdata(307, path);
  _snwprintf(path, MAX_PATH, L"%s\\k5_wait.ani", g_dataDir);
  extract_rcdata(308, path);
  _snwprintf(path, MAX_PATH, L"%s\\k5_app.ani", g_dataDir);
  extract_rcdata(309, path);
  _snwprintf(path, MAX_PATH, L"%s\\k6_wait.ani", g_dataDir);
  extract_rcdata(310, path);
  _snwprintf(path, MAX_PATH, L"%s\\k6_app.ani", g_dataDir);
  extract_rcdata(311, path);
  for (int i = 0; i < RIAS_SLOTS; i++) {
    _snwprintf(path, MAX_PATH, L"%s\\k7_%s.ani", g_dataDir, kRias[i].name);
    extract_rcdata(360 + i, path);
    _snwprintf(path, MAX_PATH, L"%s\\k8_%s.ani", g_dataDir, kRias[i].name); /* «Тяночка» — те же виды, RCDATA 390… */
    extract_rcdata(390 + i, path);
  }
}

static void set_slot(HCURSOR src, int ocr) {
  if (!src) return;
  HCURSOR c = CopyCursor(src);
  if (c) SetSystemCursor(c, ocr);
}

static HCURSOR load_res_or_file(HINSTANCE inst, int id, const wchar_t *rel) {
  HCURSOR c = NULL;
  if (inst && id) c = LoadCursorW(inst, MAKEINTRESOURCEW(id));
  if (!c) c = load_cur_file(rel);
  return c;
}

/* Каждый вид «Риас» (tag k7) и «Тяночки» (k8) — прямо из .ani, нужного размера
   (32 / 48 / 64 по настройке указателя). Без CopyCursor, как у остальных наборов:
   копия анимированного курсора остаётся одним неподвижным кадром. */
static void ani_apply(const wchar_t *tag) {
  int sz = GetSystemMetrics(SM_CXCURSOR);
  if (sz < 32) sz = 32;
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  exe_dir(dir, MAX_PATH);
  for (int i = 0; i < RIAS_SLOTS; i++) {
    HCURSOR c = NULL;
    if (g_dataDir[0]) {
      _snwprintf(path, MAX_PATH, L"%s\\%s_%s.ani", g_dataDir, tag, kRias[i].name);
      c = (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, sz, sz, LR_LOADFROMFILE);
    }
    if (!c) {
      _snwprintf(path, MAX_PATH, L"%s\\cursors\\%s_%s.ani", dir, tag, kRias[i].name);
      c = (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, sz, sz, LR_LOADFROMFILE);
    }
    if (c) SetSystemCursor(c, kRias[i].ocr);
  }
}

static void style_apply(int st); /* styles.c */
static void settings_trackbars(void); /* ползунки прозрачности Настроек — заново при смене темы */
static void kit_apply(void);     /* cursor_kit.c */

static void apply_scheme_slots(void) {
  if (g_skin == SKIN_KIT) {
    kit_apply();
    g_cursorOn = TRUE;
    mark_cursor_dirty(TRUE);
    return;
  }
  if (g_skin >= ST_SKIN0 && g_skin < ST_SKIN0 + ST_COUNT) {
    style_apply(g_skin - ST_SKIN0);
    g_cursorOn = TRUE;
    mark_cursor_dirty(TRUE);
    return;
  }
  if (g_skin == 1) { /* Мечник с 2026.10.14.6 — рыцарь, нарисованный кодом (styles.c) */
    style_apply(ST_KNIGHT);
    g_cursorOn = TRUE;
    mark_cursor_dirty(TRUE);
    return;
  }
  if (g_skin == 6 || g_skin == SKIN_K8) {
    ani_apply(g_skin == SKIN_K8 ? L"k8" : L"k7");
    g_cursorOn = TRUE;
    mark_cursor_dirty(TRUE);
    return;
  }
  if (g_skin <= 0 || g_skin > 5) return;
  int s = g_skin - 1;
  set_slot(g_staticCur[s], OCR_NORMAL_ID);
  set_slot(g_ibeamCur[s], OCR_IBEAM_ID);
  set_slot(g_handCur[s], OCR_HAND_ID);
  set_slot(g_helpCur[s], OCR_HELP_ID);
  set_slot(g_noCur[s], OCR_NO_ID);
  set_slot(g_crossCur[s], OCR_CROSS_ID);
  set_slot(g_sizeweCur[s], OCR_SIZEWE_ID);
  set_slot(g_sizensCur[s], OCR_SIZENS_ID);
  set_slot(g_nwseCur[s], OCR_SIZENWSE_ID);
  set_slot(g_neswCur[s], OCR_SIZENESW_ID);
  set_slot(g_allCur[s], OCR_SIZEALL_ID);
  set_slot(g_upCur[s], OCR_UP_ID);
  set_slot(g_pinCur[s], OCR_PIN_ID);
  set_slot(g_personCur[s], OCR_PERSON_ID);
  static const wchar_t *kWait[5] = {L"k2_wait.ani", L"k3_wait.ani", L"k4_wait.ani", L"k5_wait.ani",
                                    L"k6_wait.ani"};
  static const wchar_t *kApp[5] = {L"k2_app.ani", L"k3_app.ani", L"k4_app.ani", L"k5_app.ani",
                                   L"k6_app.ani"};
  const wchar_t *wait = kWait[s];
  const wchar_t *app = kApp[s];
  wchar_t alt[64];
  HCURSOR wait1 = load_cur_file(wait);
  _snwprintf(alt, 64, L"cursors\\%s", wait);
  if (!wait1) wait1 = load_cur_file(alt);
  if (wait1) SetSystemCursor(wait1, OCR_WAIT_ID);
  HCURSOR app1 = load_cur_file(app);
  _snwprintf(alt, 64, L"cursors\\%s", app);
  if (!app1) app1 = load_cur_file(alt);
  if (app1) SetSystemCursor(app1, OCR_APPSTARTING_ID);
  else {
    HCURSOR wait2 = load_cur_file(wait);
    if (wait2) SetSystemCursor(wait2, OCR_APPSTARTING_ID);
  }
  g_cursorOn = TRUE;
  mark_cursor_dirty(TRUE);
}

static void install_scheme_cursors(void) {
  apply_scheme_slots();
}

static void update_theme_buttons(void) {
  for (int i = 0; i < THEME_COUNT; i++) {
    if (!g_btnTheme[i]) continue;
    wchar_t t[40];
    _snwprintf(t, 40, L"%s%s", g_theme == i ? L"● " : L"", kThemes[i].name);
    SetWindowTextW(g_btnTheme[i], t);
  }
}

static HFONT make_font(const wchar_t *face, int px, int weight);
static void layout_children(void);
static void layout_settings(void);
static void layout_ask(void);
static void layout_answer(void);
static void layout_card(void);
static void layout_ocr(void);
static void theme_zoom_fonts_reset(void);
static void round_corners(HWND hwnd);
static void theme_fit_windows(void);
static void ac_retheme(void); /* ai_chat.c: цвета чата при смене темы */
/* шрифт текста текущей темы: по нему строят свои шрифты окна с масштабом */
static wchar_t g_faceBody[LF_FACESIZE] = L"Segoe UI";

static int CALLBACK font_seen_cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp) {
  (void)lf;
  (void)tm;
  (void)type;
  *(BOOL *)lp = TRUE;
  return 0;
}

/* CreateFont никогда не отказывает: нет такого шрифта — молча подставит
   какой-нибудь свой. Поэтому запасной шрифт выбираем сами, спросив
   у системы, есть ли он. На Windows 10 нет Segoe UI Variable, и до сих пор
   там вместо него выходил случайный шрифт, а не Segoe UI. */
static BOOL font_exists(const wchar_t *face) {
  if (!face || !face[0]) return FALSE;
  LOGFONTW lf;
  memset(&lf, 0, sizeof(lf));
  lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
  BOOL seen = FALSE;
  HDC dc = GetDC(NULL);
  if (dc) {
    EnumFontFamiliesExW(dc, &lf, font_seen_cb, (LPARAM)&seen, 0);
    ReleaseDC(NULL, dc);
  }
  return seen;
}

static const wchar_t *pick_face(const wchar_t *want, const wchar_t *spare) {
  if (font_exists(want)) return want;
  if (font_exists(spare)) return spare;
  return L"Segoe UI";
}

typedef struct {
  HFONT from[4], to[4];
} FontMap;

/* Каждому элементу — новый шрифт той же роли, что и был: кнопка остаётся
   кнопкой, текст текстом. Шрифты со своим масштабом (карточка, находки)
   сюда не попадают — их перестраивают сами окна. */
static BOOL CALLBACK remap_font_cb(HWND h, LPARAM lp) {
  const FontMap *m = (const FontMap *)lp;
  HFONT cur = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
  if (!cur) return TRUE;
  for (int i = 0; i < 4; i++) {
    if (m->from[i] && cur == m->from[i]) {
      SendMessageW(h, WM_SETFONT, (WPARAM)m->to[i], TRUE);
      break;
    }
  }
  return TRUE;
}

static void theme_fonts(void) {
  const PadTheme *t = &kThemes[g_theme];
  const wchar_t *face = pick_face(t->face, t->faceAlt);
  const wchar_t *head = pick_face(t->faceHead, face);
  const wchar_t *small = pick_face(t->faceSmall, face);
  lstrcpynW(g_faceBody, face, LF_FACESIZE);
  FontMap m;
  m.from[0] = g_fontDisplay;
  m.from[1] = g_fontUi;
  m.from[2] = g_fontBody;
  m.from[3] = g_fontSmall;
  g_fontDisplay = make_font(head, 13, FW_SEMIBOLD);
  g_fontUi = make_font(face, 9, FW_SEMIBOLD);
  g_fontBody = make_font(face, 10, FW_NORMAL);
  g_fontSmall = make_font(small, 8, FW_NORMAL);
  m.to[0] = g_fontDisplay;
  m.to[1] = g_fontUi;
  m.to[2] = g_fontBody;
  m.to[3] = g_fontSmall;
  HWND tops[6] = {g_hwnd, g_setHwnd, g_askHwnd, g_answer, g_card, g_ocrWnd};
  for (int i = 0; i < 6; i++)
    if (tops[i]) EnumChildWindows(tops[i], remap_font_cb, (LPARAM)&m);
  /* старые шрифты удаляем только после замены — иначе элемент
     успеет отрисоваться удалённым */
  for (int i = 0; i < 4; i++)
    if (m.from[i] && m.from[i] != m.to[i]) DeleteObject(m.from[i]);
  theme_zoom_fonts_reset();
}

/* ---- рыцарская тема: материалы и украшения ---------------------------- */
/* Пергамент рисуется самой программой, а не грузится картинкой: плитка
   128×128 из крупных пятен и мелкого зерна. Пятна считаются по замкнутой
   сетке, поэтому плитки стыкуются без шва. */
static HBRUSH g_texPaper, g_texDark;
static HBITMAP g_texPaperBm, g_texDarkBm;

static float tex_smooth(float t) {
  return t * t * (3.0f - 2.0f * t);
}

static HBRUSH make_parchment(COLORREF base, unsigned seed, HBITMAP *keep) {
  enum { N = 128 };
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = N;
  bi.bmiHeader.biHeight = -N;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HBITMAP bm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bm || !bits) return NULL;
  unsigned st = seed;
  float g1[8][8], g2[16][16];
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      st = st * 1103515245u + 12345u;
      g1[y][x] = (float)((st >> 16) & 1023) / 511.5f - 1.0f;
    }
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      st = st * 1103515245u + 12345u;
      g2[y][x] = (float)((st >> 16) & 1023) / 511.5f - 1.0f;
    }
  unsigned char *px = (unsigned char *)bits;
  for (int y = 0; y < N; y++) {
    for (int x = 0; x < N; x++) {
      float fx = x / 16.0f, fy = y / 16.0f;
      int ix = (int)fx, iy = (int)fy;
      float tx = tex_smooth(fx - ix), ty = tex_smooth(fy - iy);
      float a = g1[iy % 8][ix % 8], b = g1[iy % 8][(ix + 1) % 8];
      float c = g1[(iy + 1) % 8][ix % 8], d = g1[(iy + 1) % 8][(ix + 1) % 8];
      float v1 = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
      fx = x / 8.0f;
      fy = y / 8.0f;
      ix = (int)fx;
      iy = (int)fy;
      tx = tex_smooth(fx - ix);
      ty = tex_smooth(fy - iy);
      a = g2[iy % 16][ix % 16];
      b = g2[iy % 16][(ix + 1) % 16];
      c = g2[(iy + 1) % 16][ix % 16];
      d = g2[(iy + 1) % 16][(ix + 1) % 16];
      float v2 = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
      st = st * 1103515245u + 12345u;
      int grain = (int)((st >> 16) % 9) - 4;
      float shade = v1 * 9.0f + v2 * 4.0f + (float)grain;
      int r = (int)(GetRValue(base) + shade);
      int gg = (int)(GetGValue(base) + shade * 0.95f);
      int bb = (int)(GetBValue(base) + shade * 0.8f);
      unsigned char *q = px + ((size_t)y * N + x) * 4;
      q[0] = (unsigned char)(bb < 0 ? 0 : (bb > 255 ? 255 : bb));
      q[1] = (unsigned char)(gg < 0 ? 0 : (gg > 255 ? 255 : gg));
      q[2] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
      q[3] = 255;
    }
  }
  HBRUSH br = CreatePatternBrush(bm);
  if (!br) {
    DeleteObject(bm);
    return NULL;
  }
  *keep = bm;
  return br;
}

static void knight_textures(BOOL on) {
  if (g_texPaper) DeleteObject(g_texPaper);
  if (g_texDark) DeleteObject(g_texDark);
  if (g_texPaperBm) DeleteObject(g_texPaperBm);
  if (g_texDarkBm) DeleteObject(g_texDarkBm);
  g_texPaper = g_texDark = NULL;
  g_texPaperBm = g_texDarkBm = NULL;
  if (!on) return;
  g_texPaper = make_parchment(COL_PAPER, 20260923u, &g_texPaperBm);
  g_texDark = make_parchment(COL_PAPER_DARK, 1415u, &g_texDarkBm);
}

/* Фон окна: у рыцарской темы — пергамент, у остальных — ровный цвет.
   Поля ввода остаются ровными: под каждой буквой в них рисуется сплошной
   прямоугольник, и на фактуре текст стал бы пятнистым. */
static HBRUSH bg_brush(BOOL dark) {
  if (dark) return g_texDark ? g_texDark : g_paperDark;
  return g_texPaper ? g_texPaper : g_paper;
}

static BOOL knight_on(void) {
  return g_theme >= 0 && g_theme < THEME_COUNT && kThemes[g_theme].ornate;
}

/* Заголовки окон в рыцарской теме звучат по-своему. Надписи на
   кнопках не трогаем: «Открыть файл» должен оставаться понятным. */
static const wchar_t *knight_title(const wchar_t *t) {
  static const wchar_t *map[][2] = {
      {L"Настройки", L"Арсенал"},
      {L"Поиск", L"Дозор"},
      {L"Находки", L"Добыча"},
      {L"Найденные файлы", L"Найденные свитки"},
      {L"Карточка", L"Грамота"},
      {L"Атрибуты объекта", L"Родословная"},
      {L"Распознанный текст", L"Свиток"},
      {L"Отчёт об обновлении", L"Весть от гонца"},
      {L"Изменения в папке", L"Вести из хранилища"},
  };
  for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    if (wcscmp(t, map[i][0]) == 0) return map[i][1];
  return t;
}

static void fill_vgrad(HDC dc, RECT r, COLORREF top, COLORREF bottom) {
  int h = r.bottom - r.top;
  if (h <= 0) return;
  for (int y = 0; y < h; y++) {
    RECT line = {r.left, r.top + y, r.right, r.top + y + 1};
    HBRUSH b = CreateSolidBrush(blend_rgb(top, bottom, h > 1 ? y * 256 / (h - 1) : 0));
    FillRect(dc, &line, b);
    DeleteObject(b);
  }
}

static void frame_rect(HDC dc, RECT r, COLORREF c, int w) {
  HBRUSH b = CreateSolidBrush(c);
  RECT e;
  e = (RECT){r.left, r.top, r.right, r.top + w};
  FillRect(dc, &e, b);
  e = (RECT){r.left, r.bottom - w, r.right, r.bottom};
  FillRect(dc, &e, b);
  e = (RECT){r.left, r.top, r.left + w, r.bottom};
  FillRect(dc, &e, b);
  e = (RECT){r.right - w, r.top, r.right, r.bottom};
  FillRect(dc, &e, b);
  DeleteObject(b);
}

static void draw_poly(HDC dc, const POINT *pt, int n, COLORREF fill, COLORREF edge, int w) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN pn = CreatePen(PS_SOLID, w, edge);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pn);
  Polygon(dc, pt, n);
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(b);
  DeleteObject(pn);
}

static void draw_dot(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF edge) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN pn = CreatePen(PS_SOLID, 1, edge);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pn);
  Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(b);
  DeleteObject(pn);
}

/* Двойная золотая рамка по краю окна с ромбами на углах — как на
   виньетке рукописи. */
static void draw_knight_frame(HDC dc, RECT rc) {
  RECT o = {rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2};
  frame_rect(dc, o, KN_GOLD, 2);
  RECT in = {rc.left + 6, rc.top + 6, rc.right - 6, rc.bottom - 6};
  frame_rect(dc, in, KN_GOLD_DK, 1);
  int cx[4] = {o.left + 1, o.right - 2, o.left + 1, o.right - 2};
  int cy[4] = {o.top + 1, o.top + 1, o.bottom - 2, o.bottom - 2};
  for (int i = 0; i < 4; i++) {
    POINT d[4] = {{cx[i], cy[i] - 5}, {cx[i] + 5, cy[i]}, {cx[i], cy[i] + 5}, {cx[i] - 5, cy[i]}};
    draw_poly(dc, d, 4, KN_GOLD, KN_GOLD_DK, 1);
  }
}

/* Щит: алое поле, золотая кайма, золотой крест. Кайма — из точек
   геральдического «испанского» щита: прямой верх и заострённый низ. */
static void draw_shield(HDC dc, int x, int y, int w, int h) {
  POINT p[9] = {{x, y},
                {x + w, y},
                {x + w, y + h * 45 / 100},
                {x + w - w / 10, y + h * 70 / 100},
                {x + w * 7 / 10, y + h * 88 / 100},
                {x + w / 2, y + h},
                {x + w * 3 / 10, y + h * 88 / 100},
                {x + w / 10, y + h * 70 / 100},
                {x, y + h * 45 / 100}};
  draw_poly(dc, p, 9, KN_CRIMSON, KN_GOLD, 2);
  HBRUSH g = CreateSolidBrush(KN_GOLD);
  RECT v = {x + w / 2 - 1, y + h / 6, x + w / 2 + 2, y + h * 5 / 6};
  RECT hz = {x + w / 5, y + h * 36 / 100, x + w - w / 5, y + h * 36 / 100 + 3};
  FillRect(dc, &v, g);
  FillRect(dc, &hz, g);
  DeleteObject(g);
}

/* Знамя с раздвоенными концами и золотой каймой, заголовок — по центру. */
static void draw_banner(HDC dc, RECT r, const wchar_t *title) {
  int ym = (r.top + r.bottom) / 2, notch = (r.bottom - r.top) / 3;
  POINT p[6] = {{r.left, r.top},  {r.right, r.top},         {r.right - notch, ym},
                {r.right, r.bottom}, {r.left, r.bottom}, {r.left + notch, ym}};
  draw_poly(dc, p, 6, KN_CRIMSON, KN_GOLD, 2);
  HBRUSH d = CreateSolidBrush(KN_CRIMSON_DK);
  RECT sh = {r.left + notch + 2, r.bottom - 5, r.right - notch - 2, r.bottom - 3};
  FillRect(dc, &sh, d);
  DeleteObject(d);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, KN_GOLD_LT);
  if (g_fontDisplay) SelectObject(dc, g_fontDisplay);
  RECT t = {r.left + notch + 4, r.top, r.right - notch - 4, r.bottom};
  DrawTextW(dc, title, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* «Чертёж»: тень под окном блокнота (CS_DROPSHADOW) — слоистому окну DWM тени не даёт,
   а без неё лист на белом фоне держится только рамкой. Действует со следующего показа */
static void pad_shadow_sync(HWND h) {
  if (!h) return;
  LONG_PTR cs = GetClassLongPtrW(h, GCL_STYLE);
  LONG_PTR want = chertezh_on() ? (cs | CS_DROPSHADOW) : (cs & ~(LONG_PTR)CS_DROPSHADOW);
  if (want != cs) SetClassLongPtrW(h, GCL_STYLE, want);
}

static void apply_theme(void) {
  if (g_theme < 0 || g_theme >= THEME_COUNT) g_theme = 0;
  COL_PAPER = kThemes[g_theme].paper;
  COL_PAPER_DARK = kThemes[g_theme].dark;
  COL_INK = kThemes[g_theme].ink;
  COL_MUTED = kThemes[g_theme].muted;
  COL_SAGE = kThemes[g_theme].sage;
  COL_LINE = blend_rgb(COL_INK, COL_PAPER, 224);
  ui_sync_tokens();
  pad_shadow_sync(g_hwnd);
  ui_brush_retire(g_paper, g_paperDark); /* классы окон модулей ещё помнят эти кисти */
  if (g_paper) DeleteObject(g_paper);
  if (g_paperDark) DeleteObject(g_paperDark);
  g_paper = CreateSolidBrush(COL_PAPER);
  g_paperDark = CreateSolidBrush(COL_PAPER_DARK);
  /* у рыцарской темы фон окон — пергамент, у остальных ровный цвет */
  knight_textures(kThemes[g_theme].ornate);
  {
    HWND lite[4] = {g_hwnd, g_answer, g_card, g_ocrWnd};
    for (int i = 0; i < 4; i++) {
      if (!lite[i]) continue;
      SetClassLongPtrW(lite[i], GCLP_HBRBACKGROUND, (LONG_PTR)bg_brush(FALSE));
      InvalidateRect(lite[i], NULL, TRUE);
    }
    HWND dark[2] = {g_setHwnd, g_askHwnd};
    for (int i = 0; i < 2; i++) {
      if (!dark[i]) continue;
      SetClassLongPtrW(dark[i], GCLP_HBRBACKGROUND, (LONG_PTR)bg_brush(TRUE));
      InvalidateRect(dark[i], NULL, TRUE);
    }
  }
  /* список находок перекрашиваем явно: свои цвета он запомнил при создании
     и сам со сменой темы не менялся */
  if (g_answerList) {
    SendMessageW(g_answerList, LVM_SETBKCOLOR, 0, (LPARAM)COL_PAPER);
    SendMessageW(g_answerList, LVM_SETTEXTBKCOLOR, 0, (LPARAM)COL_PAPER);
    SendMessageW(g_answerList, LVM_SETTEXTCOLOR, 0, (LPARAM)COL_INK);
    InvalidateRect(g_answerList, NULL, TRUE);
  }
  if (g_edit) InvalidateRect(g_edit, NULL, TRUE);
  if (g_clipEdit) InvalidateRect(g_clipEdit, NULL, TRUE);
  update_theme_buttons();
  theme_fonts();
  HWND tops[6] = {g_hwnd, g_setHwnd, g_askHwnd, g_answer, g_card, g_ocrWnd};
  for (int i = 0; i < 6; i++)
    if (tops[i]) round_corners(tops[i]);
  /* шрифт другой — у надписей другая ширина, кнопки перемеряем */
  if (g_hwnd) layout_children();
  if (g_setHwnd) {
    settings_trackbars(); /* новые ползунки — с кистью новой темы */
    layout_settings();
  }
  if (g_askHwnd) layout_ask();
  if (g_answer) layout_answer();
  if (g_card) layout_card();
  if (g_ocrWnd) layout_ocr();
  theme_fit_windows();
  for (int i = 0; i < 6; i++)
    if (tops[i]) RedrawWindow(tops[i], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  /* окна модулей (расчёты, PLM, чат ИИ…): хром Windows 11, рамки полей */
  ui_theme_changed();
  ac_retheme();
}

static void kit_theme_changed(void); /* cursor_kit.c: значки «Современного» — цветом новой темы */

static void set_theme(int t) {
  if (t < 0 || t >= THEME_COUNT) return;
  g_theme = t;
  apply_theme();
  save_cursor_pref();
  kit_theme_changed();
}

static void apply_alpha_now(void) {
  int a = g_follow ? g_alphaFollow : g_alphaPinned;
  if (a < 40) a = 40;
  if (a > 255) a = 255;
  set_alpha((BYTE)a);
}

static void load_cursor_pref(void) {
  HANDLE h = CreateFileW(g_prefPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  char buf[600];
  DWORD n = 0;
  ReadFile(h, buf, 599, &n, NULL);
  CloseHandle(h);
  buf[n] = 0;
  char skin[16] = {0};
  char eng[16] = {0};
  int bg = g_alphaFollow, fg = g_alphaPinned, autoOn = -1, theme = 0, aw = 0, ah = 0;
  int cw = 0, ch = 0, pt = 0, keep = 0, ax = 0, ay = 0;
  /* три последних — про окно карточки; в старых файлах их нет,
     sscanf просто их не заполнит и останутся нули */
  int cx = 0, cy = 0, cpt = 0;
  int ox = 0, oy = 0, ow = 0, oh = 0, opt = 0;
  int sparkle = -1; /* нет в файле — искорки включены */
  int padMode = 0;
  int flame = -1; /* нет в файле — пламя включено */
  int catFx = -1; /* нет в файле — царапины */
  int fxPick = -1; /* нет в файле — эффект как у курсора */
  int tab = -1, order = -1; /* нет в файле — первая вкладка, порядок как задуман */
  int idle = -1; /* нет в файле — бой через минуту */
  int idleSz = -1; /* нет в файле — средние бойцы */
  int idleWatch = 0; /* нет в файле — режим просмотра выключен */
  int ringStyle = -1, ringBlur = -1, ringDots = -1, ringAnim = -1; /* панель Shift; нет в файле — стекло, всё включено */
  /* «Современный» набор: стиль, размер, обводка, тень, цвет темы, анимация, эффект; нет в файле — светлый, всё включено */
  int kSt = -1, kSz = -1, kOut = -1, kSh = -1, kAcc = -1, kAn = -1, kFx = -1;
  int setW = 0, setH = 0; /* размер окна Настроек; нет в файле — по умолчанию */
  int look = 0; /* 1 — вид «Чертёж» уже предложен; нет в файле (до 2026.10.30) — включить его один раз */
  sscanf(buf, "%15s %d %d %15s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
              " %d %d %d %d %d %d %d %d %d %d", skin,
         &bg, &fg, eng, &autoOn, &theme, &aw, &ah, &cw, &ch, &pt, &keep, &ax, &ay, &cx, &cy, &cpt,
         &ox, &oy, &ow, &oh, &opt, &sparkle, &padMode, &flame, &catFx, &fxPick, &tab, &order, &idle, &idleSz, &idleWatch,
         &ringStyle, &ringBlur, &ringDots, &ringAnim, &kSt, &kSz, &kOut, &kSh, &kAcc, &kAn, &kFx, &setW, &setH, &look);
  if (setW >= 360 && setW <= 8000) g_setUserW = setW;
  if (setH >= 240 && setH <= 8000) g_setUserH = setH;
  if (kSt >= 0 && kSt < KIT_STYLES) g_kitStyle = kSt;
  if (kSz >= 0 && kSz < KIT_SIZES) g_kitSize = kSz;
  if (kOut == 0 || kOut == 1) g_kitOutline = kOut == 1;
  if (kSh == 0 || kSh == 1) g_kitShadow = kSh == 1;
  if (kAcc == 0 || kAcc == 1) g_kitThemeAcc = kAcc == 1;
  if (kAn == 0 || kAn == 1) g_kitAnim = kAn == 1;
  if (kFx == 0 || kFx == 1) g_kitFx = kFx == 1;
  if (ringStyle >= 0 && ringStyle < RING_STYLES) g_ringStyle = ringStyle;
  if (ringBlur == 0 || ringBlur == 1) g_ringBlur = ringBlur == 1;
  if (ringDots == 0 || ringDots == 1) g_ringDots = ringDots == 1;
  if (ringAnim == 0 || ringAnim == 1) g_ringAnim = ringAnim == 1;
  g_idleWatch = idleWatch == 1;
  if (idleSz >= 0 && idleSz < IDLE_SIZES) g_idleSize = idleSz;
  for (int i = 0; i < IDLE_OPTIONS; i++)
    if (idle == kIdleSec[i]) g_idleSec = idle;
  /* до 2026.10.14.4 на месте вкладки было, какие разделы раскрыты: там вкладка
     окажется случайной, но допустимой */
  if (tab >= 0 && tab < SEC_COUNT) g_tab = tab;
  if (order >= 0) { /* порядок — цифрами: 210345 — вторая, первая, нулевая…;
                       до 2026.10.16.4 вкладок было пять — «ИИ» тогда встаёт последней */
    for (int cnt = SEC_COUNT; cnt >= SEC_COUNT - 1; cnt--) {
      int o[SEC_COUNT], seen = 0, ok = 1, v = order;
      for (int i = cnt - 1; i >= 0; i--, v /= 10) {
        o[i] = v % 10;
        if (o[i] >= cnt || (seen >> o[i]) & 1) ok = 0;
        else seen |= 1 << o[i];
      }
      if (cnt < SEC_COUNT) o[cnt] = SEC_COUNT - 1;
      if (ok && v == 0) {
        memcpy(g_tabOrder, o, sizeof(o));
        break;
      }
    }
  }
  g_sparkle = sparkle != 0;
  g_flame = flame != 0;
  g_catFx = catFx == 0 ? 0 : (catFx == 2 ? 2 : 1);
  g_fxPick = fxPick >= 0 && fxPick <= 17 ? fxPick : -1;
  g_padMode = padMode == 1 ? 1 : 0;
  g_cardX = cx;
  g_cardY = cy;
  if (cpt >= 7 && cpt <= 22) g_cardPt = cpt;
  g_ocrX = ox;
  g_ocrY = oy;
  if (ow >= 320 && ow <= 4000) g_ocrW = ow;
  if (oh >= 160 && oh <= 3000) g_ocrH = oh;
  if (opt >= 7 && opt <= 22) g_ocrPt = opt;
  g_ansKeepPos = keep == 1;
  g_ansX = ax;
  g_ansY = ay;
  if (aw >= 320 && aw <= 4000) g_ansW = aw;
  if (ah >= 200 && ah <= 3000) g_ansH = ah;
  if (cw >= 320 && cw <= 4000) g_cardW = cw;
  if (ch >= 200 && ch <= 3000) g_cardH = ch;
  if (pt >= 7 && pt <= 22) g_ansPt = pt;
  /* t1…t9 — t4 и t5 (Огонёк, Череп) убраны и становятся Вороном,
     t1…t3 — Ворон, Осьминог, Скорпион, t6…t8 — Разрез, Сукуна, Годжо, t9 — Баклажан.
     tb — удалённый курсор, остаётся Баклажан. */
  static const signed char kTSkin[9] = {0, 1, 2, 0, 0, 3, 4, 5, 6};
  if (skin[0] == 'm' && skin[1] == '1') g_skin = SKIN_KIT;
  else if (skin[0] == 't' && skin[1] == 'b') g_skin = ST_SKIN0 + 6;
  else if (skin[0] == 't' && skin[1] >= '1' && skin[1] <= '9') g_skin = ST_SKIN0 + kTSkin[skin[1] - '1'];
  else if (skin[0] == 'k' && skin[1] == '8') g_skin = SKIN_K8;
  else if (skin[0] == 'k' && skin[1] == '7') g_skin = 6;
  else if (skin[0] == 'k' && skin[1] == '6') g_skin = 5;
  else if (skin[0] == 'k' && skin[1] == '5') g_skin = 4;
  else if (skin[0] == 'k' && skin[1] == '4') g_skin = 3;
  else if (skin[0] == 'k' && skin[1] == '3') g_skin = 2;
  else if (skin[0] == 's') g_skin = 0;
  else g_skin = 1;
  if (bg >= 40 && bg <= 255) g_alphaFollow = bg;
  if (fg >= 40 && fg <= 255) g_alphaPinned = fg;
  if (eng[0] == 'p') g_engine = 4;
  else if (eng[0] == 'f') g_engine = 5;
  else g_engine = 3;
  if (autoOn == 0 || autoOn == 1) g_autostart = autoOn ? TRUE : FALSE;
  else g_autostart = autostart_get();
  if (theme >= 0 && theme < THEME_COUNT) g_theme = theme;
  /* пользователь выбрал вариант «Чертёж» — при первом запуске новой версии он и встаёт;
     дальше тема — какую выберут в Настройках («Кастомизация» → «Тема») */
  if (look != 1) g_theme = THEME_CHERTEZH;
}

static void save_cursor_pref(void) {
  static const char *const kSt[ST_COUNT] = {"t1", "t2", "t3", "t6", "t7", "t8", "t9"};
  const char *v = g_skin >= ST_SKIN0 && g_skin < ST_SKIN0 + ST_COUNT ? kSt[g_skin - ST_SKIN0]
                  : g_skin == SKIN_K8 ? "k8"
                  : g_skin == SKIN_KIT ? "m1"
                  : g_skin == 6 ? "k7"
                  : g_skin == 5 ? "k6"
                  : g_skin == 4 ? "k5"
                  : g_skin == 3 ? "k4" : (g_skin == 2 ? "k3" : (g_skin == 0 ? "system" : "k2"));
  const char *e = g_engine == 4 ? "plm" : (g_engine == 5 ? "files" : "ai");
  char buf[480];
  int order = 0;
  for (int i = 0; i < SEC_COUNT; i++) order = order * 10 + g_tabOrder[i];
  snprintf(buf, sizeof(buf), "%s %d %d %s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
                             " %d %d %d %d %d %d %d %d %d 1\n",
           v, g_alphaFollow, g_alphaPinned, e, g_autostart ? 1 : 0, g_theme, g_ansW, g_ansH,
           g_cardW, g_cardH, g_ansPt, g_ansKeepPos ? 1 : 0, g_ansX, g_ansY, g_cardX, g_cardY,
           g_cardPt, g_ocrX, g_ocrY, g_ocrW, g_ocrH, g_ocrPt, g_sparkle ? 1 : 0, g_padMode,
           g_flame ? 1 : 0, g_catFx, g_fxPick, g_tab, order, g_idleSec, g_idleSize, g_idleWatch,
           g_ringStyle, g_ringBlur ? 1 : 0, g_ringDots ? 1 : 0, g_ringAnim ? 1 : 0, g_kitStyle, g_kitSize,
           g_kitOutline ? 1 : 0, g_kitShadow ? 1 : 0, g_kitThemeAcc ? 1 : 0, g_kitAnim ? 1 : 0, g_kitFx ? 1 : 0, g_setUserW, g_setUserH);
  HANDLE h = CreateFileW(g_prefPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(h, buf, (DWORD)strlen(buf), &w, NULL);
  CloseHandle(h);
}

/* кнопки эффекта в Настройках: порядок совпадает с ID_FX_BASE */
static const wchar_t *const kFxNames[FX_OPTIONS] = {L"Как у курсора", L"Нет",    L"Звёздочки", L"Пламя",
                                                    L"Царапины",      L"Разрез", L"Кольца",    L"Конфетти",
                                                    L"Молния",        L"Сердечки", L"Рассечение", L"Пурпур",
                                                    L"Перья",         L"Чернила",  L"Яд",       L"Бесконечность",
                                                    L"Пятна",         L"Лепестки",  L"Волна"};
static const int kFxPick[FX_OPTIONS] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
static const wchar_t *const kStyleNames[ST_COUNT] = {L"Ворон", L"Осьминог", L"Скорпион", L"Разрез", L"Сукуна", L"Годжо",
                                                    L"Баклажан"};

static void update_fx_buttons(void) {
  for (int i = 0; i < FX_OPTIONS; i++) {
    if (!g_btnFx[i]) continue;
    wchar_t t[32];
    _snwprintf(t, 32, L"%s%s", g_fxPick == kFxPick[i] ? L"● " : L"", kFxNames[i]);
    SetWindowTextW(g_btnFx[i], t);
  }
}

/* имя набора курсоров: для кнопок, строки состояния и просмотра */
static const wchar_t *skin_name(int skin) {
  static const wchar_t *const kOld[7] = {L"Windows", L"Мечник", L"Рукавица", L"Фея", L"Дракон", L"Чёрный кот", L"Риас"};
  if (skin >= ST_SKIN0 && skin < ST_SKIN0 + ST_COUNT) return kStyleNames[skin - ST_SKIN0];
  if (skin == SKIN_K8) return L"Тяночка";
  if (skin == SKIN_KIT) return L"Современный";
  return skin >= 0 && skin < 7 ? kOld[skin] : L"Мечник";
}

static void set_mark(HWND b, BOOL on, const wchar_t *name) {
  if (!b) return;
  wchar_t t[40];
  _snwprintf(t, 40, L"%s%s", on ? L"● " : L"", name);
  SetWindowTextW(b, t);
}

static void update_kit_buttons(void); /* ниже, у карточки «Курсор» */

static void update_cursor_buttons(void) {
  set_mark(g_btnK2, g_skin == 1, L"Мечник");
  set_mark(g_btnK3, g_skin == 2, L"Рукавица");
  set_mark(g_btnK4, g_skin == 3, L"Фея");
  set_mark(g_btnK5, g_skin == 4, L"Дракон");
  set_mark(g_btnK6, g_skin == 5, L"Чёрный кот");
  set_mark(g_btnK7, g_skin == 6, L"Риас");
  set_mark(g_btnK8, g_skin == SKIN_K8, L"Тяночка");
  set_mark(g_btnKit, g_skin == SKIN_KIT, L"Современный");
  for (int i = 0; i < ST_COUNT; i++) set_mark(g_btnSt[i], g_skin == ST_SKIN0 + i, kStyleNames[i]);
  set_mark(g_btnSys, g_skin == 0, L"Курсор Windows");
  update_fx_buttons();
  update_kit_buttons();
}

static void set_skin(int skin) {
  g_skin = skin;
  save_cursor_pref();
  update_cursor_buttons();
  if (skin == 0) restore_system_cursor();
  else install_scheme_cursors();
  fx_sync();
  _snwprintf(g_status, 160, L"Курсор: %s", skin_name(skin));
  g_statusOn = TRUE;
  if (g_hwnd) {
    SetTimer(g_hwnd, TIMER_STATUS, 1600, NULL);
    InvalidateRect(g_hwnd, NULL, FALSE);
  }
}

/* эффект при нажатии отдельно от курсора: имена для строки состояния */
static const wchar_t *fx_pick_name(int pick) {
  static const wchar_t *const kNames[18] = {L"нет",   L"звёздочки", L"пламя",  L"царапины", L"разрез",
                                           L"кольца", L"конфетти", L"молния", L"сердечки", L"рассечение",
                                           L"пурпур", L"перья",    L"чернила", L"яд",     L"бесконечность",
                                           L"пятна",  L"лепестки", L"волна"};
  return pick >= 0 && pick < 18 ? kNames[pick] : L"как у курсора";
}

/* то же с большой буквы — для окошка просмотра (там всегда сам эффект) */
static const wchar_t *fx_pick_label(int kind) {
  return kind > 0 && kind < FX_OPTIONS - 1 ? kFxNames[kind + 1] : L"без эффекта";
}

static void set_fx_pick(int pick) {
  g_fxPick = pick;
  save_cursor_pref();
  fx_sync();
  update_fx_buttons();
  wchar_t msg[160];
  _snwprintf(msg, 160, L"Эффект при нажатии: %s", fx_pick_name(pick));
  show_status(msg);
}

static void cycle_skin(void) {
  int next = g_skin + 1;
  if (next > SKIN_KIT) next = 0;
  set_skin(next);
}

/* кнопка какого курсора в Настройках; -1 — не кнопка курсора */
static int settings_skin_of_id(int id) {
  if (id == ID_CUR_K2) return 1;
  if (id == ID_CUR_K3) return 2;
  if (id == ID_CUR_K4) return 3;
  if (id == ID_CUR_K5) return 4;
  if (id == ID_CUR_K6) return 5;
  if (id == ID_CUR_K7) return 6;
  if (id == ID_CUR_K8) return SKIN_K8;
  if (id == ID_CUR_KIT) return SKIN_KIT;
  if (id == ID_SYS_CUR) return 0;
  if (id >= ID_CUR_ST_BASE && id < ID_CUR_ST_BASE + ST_COUNT) return ST_SKIN0 + id - ID_CUR_ST_BASE;
  return -1;
}

/* «Фея» чуть крупнее прочих: 1,25 размера системного курсора (40 точек при
   100 %, 50 при 125 %, 60 при 150 %). Её .cur несут ровно эти размеры.
   До 2026.09.23.13 было полтора размера — вышло крупно. */
static HCURSOR load_big_cursor(HINSTANCE inst, int id, const wchar_t *rel) {
  int sz = GetSystemMetrics(SM_CXCURSOR) * 5 / 4;
  if (sz < 32) sz = 40;
  HCURSOR c = NULL;
  if (inst && id) c = (HCURSOR)LoadImageW(inst, MAKEINTRESOURCEW(id), IMAGE_CURSOR, sz, sz, 0);
  if (!c) {
    wchar_t path[MAX_PATH], dir[MAX_PATH];
    exe_dir(dir, MAX_PATH);
    _snwprintf(path, MAX_PATH, L"%s\\%s", dir, rel);
    c = (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, sz, sz, LR_LOADFROMFILE);
  }
  if (!c) c = load_res_or_file(inst, id, rel);
  return c;
}

static void load_cursor_frames(HINSTANCE inst) {
  g_staticCur[0] = load_res_or_file(inst, 200, L"cursors\\k2_static.cur");
  g_staticCur[1] = load_res_or_file(inst, 210, L"cursors\\k3_static.cur");
  g_ibeamCur[0] = load_res_or_file(inst, 220, L"cursors\\k2_ibeam.cur");
  g_ibeamCur[1] = load_res_or_file(inst, 221, L"cursors\\k3_ibeam.cur");
  g_handCur[0] = load_res_or_file(inst, 230, L"cursors\\k2_hand.cur");
  g_handCur[1] = load_res_or_file(inst, 231, L"cursors\\k3_hand.cur");
  g_helpCur[0] = load_res_or_file(inst, 232, L"cursors\\k2_help.cur");
  g_helpCur[1] = load_res_or_file(inst, 233, L"cursors\\k3_help.cur");
  g_noCur[0] = load_res_or_file(inst, 234, L"cursors\\k2_no.cur");
  g_noCur[1] = load_res_or_file(inst, 235, L"cursors\\k3_no.cur");
  g_crossCur[0] = load_res_or_file(inst, 236, L"cursors\\k2_cross.cur");
  g_crossCur[1] = load_res_or_file(inst, 237, L"cursors\\k3_cross.cur");
  g_sizeweCur[0] = load_res_or_file(inst, 240, L"cursors\\k2_sizewe.cur");
  g_sizeweCur[1] = load_res_or_file(inst, 241, L"cursors\\k3_sizewe.cur");
  g_sizensCur[0] = load_res_or_file(inst, 242, L"cursors\\k2_sizens.cur");
  g_sizensCur[1] = load_res_or_file(inst, 243, L"cursors\\k3_sizens.cur");
  g_nwseCur[0] = load_res_or_file(inst, 244, L"cursors\\k2_sizenwse.cur");
  g_nwseCur[1] = load_res_or_file(inst, 245, L"cursors\\k3_sizenwse.cur");
  g_neswCur[0] = load_res_or_file(inst, 246, L"cursors\\k2_sizenesw.cur");
  g_neswCur[1] = load_res_or_file(inst, 247, L"cursors\\k3_sizenesw.cur");
  g_allCur[0] = load_res_or_file(inst, 248, L"cursors\\k2_sizeall.cur");
  g_allCur[1] = load_res_or_file(inst, 249, L"cursors\\k3_sizeall.cur");
  g_upCur[0] = load_res_or_file(inst, 250, L"cursors\\k2_up.cur");
  g_upCur[1] = load_res_or_file(inst, 251, L"cursors\\k3_up.cur");
  g_pinCur[0] = load_res_or_file(inst, 252, L"cursors\\k2_pin.cur");
  g_pinCur[1] = load_res_or_file(inst, 253, L"cursors\\k3_pin.cur");
  g_personCur[0] = load_res_or_file(inst, 254, L"cursors\\k2_person.cur");
  g_personCur[1] = load_res_or_file(inst, 255, L"cursors\\k3_person.cur");
  g_staticCur[2] = load_big_cursor(inst, 260, L"cursors\\k4_static.cur");
  g_ibeamCur[2] = load_big_cursor(inst, 261, L"cursors\\k4_ibeam.cur");
  g_handCur[2] = load_big_cursor(inst, 262, L"cursors\\k4_hand.cur");
  g_helpCur[2] = load_big_cursor(inst, 263, L"cursors\\k4_help.cur");
  g_noCur[2] = load_big_cursor(inst, 264, L"cursors\\k4_no.cur");
  g_crossCur[2] = load_big_cursor(inst, 265, L"cursors\\k4_cross.cur");
  g_sizeweCur[2] = load_big_cursor(inst, 266, L"cursors\\k4_sizewe.cur");
  g_sizensCur[2] = load_big_cursor(inst, 267, L"cursors\\k4_sizens.cur");
  g_nwseCur[2] = load_big_cursor(inst, 268, L"cursors\\k4_sizenwse.cur");
  g_neswCur[2] = load_big_cursor(inst, 269, L"cursors\\k4_sizenesw.cur");
  g_allCur[2] = load_big_cursor(inst, 270, L"cursors\\k4_sizeall.cur");
  g_upCur[2] = load_big_cursor(inst, 271, L"cursors\\k4_up.cur");
  g_pinCur[2] = load_big_cursor(inst, 272, L"cursors\\k4_pin.cur");
  g_personCur[2] = load_big_cursor(inst, 273, L"cursors\\k4_person.cur");
  /* «Дракон» — обычного размера, как Мечник и Рукавица */
  g_staticCur[3] = load_res_or_file(inst, 280, L"cursors\\k5_static.cur");
  g_ibeamCur[3] = load_res_or_file(inst, 281, L"cursors\\k5_ibeam.cur");
  g_handCur[3] = load_res_or_file(inst, 282, L"cursors\\k5_hand.cur");
  g_helpCur[3] = load_res_or_file(inst, 283, L"cursors\\k5_help.cur");
  g_noCur[3] = load_res_or_file(inst, 284, L"cursors\\k5_no.cur");
  g_crossCur[3] = load_res_or_file(inst, 285, L"cursors\\k5_cross.cur");
  g_sizeweCur[3] = load_res_or_file(inst, 286, L"cursors\\k5_sizewe.cur");
  g_sizensCur[3] = load_res_or_file(inst, 287, L"cursors\\k5_sizens.cur");
  g_nwseCur[3] = load_res_or_file(inst, 288, L"cursors\\k5_sizenwse.cur");
  g_neswCur[3] = load_res_or_file(inst, 289, L"cursors\\k5_sizenesw.cur");
  g_allCur[3] = load_res_or_file(inst, 290, L"cursors\\k5_sizeall.cur");
  g_upCur[3] = load_res_or_file(inst, 291, L"cursors\\k5_up.cur");
  g_pinCur[3] = load_res_or_file(inst, 292, L"cursors\\k5_pin.cur");
  g_personCur[3] = load_res_or_file(inst, 293, L"cursors\\k5_person.cur");
  /* «Чёрный кот» — обычного размера, как Мечник, Рукавица и Дракон */
  g_staticCur[4] = load_res_or_file(inst, 340, L"cursors\\k6_static.cur");
  g_ibeamCur[4] = load_res_or_file(inst, 341, L"cursors\\k6_ibeam.cur");
  g_handCur[4] = load_res_or_file(inst, 342, L"cursors\\k6_hand.cur");
  g_helpCur[4] = load_res_or_file(inst, 343, L"cursors\\k6_help.cur");
  g_noCur[4] = load_res_or_file(inst, 344, L"cursors\\k6_no.cur");
  g_crossCur[4] = load_res_or_file(inst, 345, L"cursors\\k6_cross.cur");
  g_sizeweCur[4] = load_res_or_file(inst, 346, L"cursors\\k6_sizewe.cur");
  g_sizensCur[4] = load_res_or_file(inst, 347, L"cursors\\k6_sizens.cur");
  g_nwseCur[4] = load_res_or_file(inst, 348, L"cursors\\k6_sizenwse.cur");
  g_neswCur[4] = load_res_or_file(inst, 349, L"cursors\\k6_sizenesw.cur");
  g_allCur[4] = load_res_or_file(inst, 350, L"cursors\\k6_sizeall.cur");
  g_upCur[4] = load_res_or_file(inst, 351, L"cursors\\k6_up.cur");
  g_pinCur[4] = load_res_or_file(inst, 352, L"cursors\\k6_pin.cur");
  g_personCur[4] = load_res_or_file(inst, 353, L"cursors\\k6_person.cur");
}

static void free_one(HCURSOR *c) {
  if (*c) {
    DestroyCursor(*c);
    *c = NULL;
  }
}

static void free_cursor_frames(void) {
  restore_system_cursor();
  for (int s = 0; s < 5; s++) {
    free_one(&g_staticCur[s]);
    free_one(&g_ibeamCur[s]);
    free_one(&g_handCur[s]);
    free_one(&g_helpCur[s]);
    free_one(&g_noCur[s]);
    free_one(&g_crossCur[s]);
    free_one(&g_sizeweCur[s]);
    free_one(&g_sizensCur[s]);
    free_one(&g_nwseCur[s]);
    free_one(&g_neswCur[s]);
    free_one(&g_allCur[s]);
    free_one(&g_upCur[s]);
    free_one(&g_pinCur[s]);
    free_one(&g_personCur[s]);
  }
}

static void load_notes(void) {
  HANDLE h = CreateFileW(g_notesPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    SetWindowTextW(g_edit,
                   L"Первая фраза для вставки\r\n"
                   L"Вторая фраза\r\n"
                   L"Третья фраза\r\n"
                   L"Четвёртая фраза\r\n"
                   L"Пятая фраза\r\n");
    g_dirty = TRUE;
    return;
  }
  DWORD sz = GetFileSize(h, NULL);
  if (sz == INVALID_FILE_SIZE || sz == 0) {
    CloseHandle(h);
    return;
  }
  char *utf8 = (char *)malloc(sz + 1);
  if (!utf8) {
    CloseHandle(h);
    return;
  }
  DWORD read = 0;
  ReadFile(h, utf8, sz, &read, NULL);
  utf8[read] = 0;
  CloseHandle(h);
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, read, NULL, 0);
  wchar_t *w = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
  if (w) {
    MultiByteToWideChar(CP_UTF8, 0, utf8, read, w, wlen);
    w[wlen] = 0;
    SetWindowTextW(g_edit, w);
    /* the edit control caps what it accepts; saving a truncated copy back
       would destroy the rest of an oversized file */
    if (GetWindowTextLengthW(g_edit) < wlen) {
      g_notesTruncated = TRUE;
      show_status(L"Файл заметок слишком велик — правки не сохраняются");
    }
    free(w);
  }
  free(utf8);
  g_dirty = FALSE;
}

static void save_notes(void) {
  if (!g_edit || g_notesTruncated) return;
  int len = GetWindowTextLengthW(g_edit);
  wchar_t *w = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
  if (!w) return;
  GetWindowTextW(g_edit, w, len + 1);
  int nbytes = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *utf8 = (char *)malloc(nbytes);
  if (!utf8) {
    free(w);
    return;
  }
  WideCharToMultiByte(CP_UTF8, 0, w, -1, utf8, nbytes, NULL, NULL);
  /* write beside the real file and swap it in: overwriting in place leaves a
     window where a crash or a power cut finds an empty notes.txt */
  wchar_t tmp[MAX_PATH];
  _snwprintf(tmp, MAX_PATH, L"%s.tmp", g_notesPath);
  HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD written = 0, want = nbytes > 0 ? (DWORD)nbytes - 1 : 0;
    BOOL ok = want == 0 || (WriteFile(h, utf8, want, &written, NULL) && written == want);
    if (ok) ok = FlushFileBuffers(h);
    CloseHandle(h);
    if (ok && MoveFileExW(tmp, g_notesPath, MOVEFILE_REPLACE_EXISTING)) g_dirty = FALSE;
    else DeleteFileW(tmp);
  }
  free(utf8);
  free(w);
}

static void layout_children(void) {
  if (!g_edit) return;
  int dpi = 96;
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32) {
    typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
    GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
    if (fn && g_hwnd) dpi = (int)fn(g_hwnd);
  }
  int th = MulDiv(TITLE_H, dpi, 96);
  int fh = MulDiv(FOOT_H, dpi, 96);
  int pad = MulDiv(PAD, dpi, 96);
  int gut = MulDiv(GUTTER, dpi, 96);
  int btnH = MulDiv(24, dpi, 96);
  int closeW = MulDiv(28, dpi, 96);
  int pinW = MulDiv(88, dpi, 96);
  int gap = MulDiv(4, dpi, 96);
  RECT rc;
  GetClientRect(g_hwnd, &rc);
  int cw = rc.right - rc.left;
  int ch = rc.bottom - rc.top;
  MoveWindow(g_close, cw - pad - closeW, (th - btnH) / 2, closeW, btnH, TRUE);
  if (g_min)
    MoveWindow(g_min, cw - pad - closeW - gap - closeW, (th - btnH) / 2, closeW, btnH, TRUE);
  (void)pinW;
  MoveWindow(g_pin, cw - pad - closeW * 2 - gap * 2 - btnH - gap, (th - btnH) / 2, btnH + gap, btnH, TRUE);

  /* ряд режимов: «Блокнот | Поиск» — переключатель слева, ⋯ и ⚙ справа */
  int mh = MulDiv(MODE_H, dpi, 96);
  {
    int segH = MulDiv(28, dpi, 96), in = MulDiv(3, dpi, 96), tabW = MulDiv(84, dpi, 96);
    int my = th + (mh - segH) / 2;
    if (g_btnNotes) MoveWindow(g_btnNotes, pad + in, my, tabW, segH, TRUE);
    if (g_btnAsk) MoveWindow(g_btnAsk, pad + in + tabW, my, tabW, segH, TRUE);
    int ib = MulDiv(30, dpi, 96), iy = th + (mh - ib) / 2;
    if (g_btnSet) MoveWindow(g_btnSet, cw - pad - ib, iy, ib, ib, TRUE);
    if (g_btnMore) MoveWindow(g_btnMore, cw - pad - ib * 2 - gap, iy, ib, ib, TRUE);
  }
  th += mh; /* дальше всё — под рядом режимов */
  /* высота шапки — для подложки кнопок и бегущей линии (ui_theme.c) */
  SetPropW(g_hwnd, L"CpUiHead", (HANDLE)(INT_PTR)th);

  int clipH = MulDiv(CLIP_H, dpi, 96);
  int histW = MulDiv(26, dpi, 96);
  if (g_clipEdit) MoveWindow(g_clipEdit, gut, th, cw - gut - pad - histW - gap, clipH, TRUE);
  if (g_clipHistBtn) MoveWindow(g_clipHistBtn, cw - pad - histW, th, histW, clipH, TRUE);
  MoveWindow(g_edit, gut, th + clipH, cw - gut - pad, ch - th - clipH - fh, TRUE);
  note_margins();
  /* режим поиска: поле со «Спросить», ниже — где искать, три кнопки в ряд */
  {
    BOOL srch = g_padMode == 1;
    ShowWindow(g_edit, srch ? SW_HIDE : SW_SHOW);
    HWND sc[5] = {g_searchEdit, g_searchGo, g_btnAi, g_btnPlm, g_btnFiles};
    for (int k = 0; k < 5; k++)
      if (sc[k]) ShowWindow(sc[k], srch ? SW_SHOW : SW_HIDE);
    if (srch) {
      /* поле и «Найти», под ним «Где:» и три ярлыка по ширине надписи */
      int sy = th + clipH + MulDiv(10, dpi, 96), sh = MulDiv(30, dpi, 96);
      int goW = MulDiv(72, dpi, 96), left = gut, right = cw - pad;
      if (g_searchEdit) MoveWindow(g_searchEdit, left, sy, right - left - goW - gap, sh, TRUE);
      if (g_searchGo) MoveWindow(g_searchGo, right - goW, sy, goW, sh, TRUE);
      sy += sh + MulDiv(10, dpi, 96);
      int chH = MulDiv(26, dpi, 96), cx = left + MulDiv(34, dpi, 96); /* место под «Где:» */
      HWND chips[3] = {g_btnAi, g_btnPlm, g_btnFiles};
      HDC dc = GetDC(g_hwnd);
      HGDIOBJ of = (dc && g_fontUi) ? SelectObject(dc, g_fontUi) : NULL;
      for (int k = 0; k < 3; k++) {
        if (!chips[k]) continue;
        wchar_t t[64];
        GetWindowTextW(chips[k], t, 64);
        const wchar_t *lbl = (t[0] == 0x25CF && t[1] == L' ') ? t + 2 : t;
        SIZE sz = {60, 0};
        if (dc) GetTextExtentPoint32W(dc, lbl, (int)wcslen(lbl), &sz);
        int w = sz.cx + MulDiv(24, dpi, 96);
        MoveWindow(chips[k], cx, sy, w, chH, TRUE);
        cx += w + gap;
      }
      if (of) SelectObject(dc, of);
      if (dc) ReleaseDC(g_hwnd, dc);
    }
  }
}

/* углы окна берутся из темы (у «Чертежа» прямые, у остальных круглые);
   с 2026.10.21 — весь хром Windows 11: рамка и заголовок, см. ui_theme.c */
static void round_corners(HWND hwnd) {
  ui_window_chrome(hwnd);
}

static void apply_follow_state(void) {
  set_clickthrough(g_follow);
  apply_alpha_now();
  EnableWindow(g_edit, !g_follow);
  if (g_clipEdit) EnableWindow(g_clipEdit, !g_follow);
  if (g_clipClr) EnableWindow(g_clipClr, !g_follow);
  if (g_clipHistBtn) EnableWindow(g_clipHistBtn, !g_follow);
  EnableWindow(g_pin, !g_follow);
  EnableWindow(g_close, !g_follow);
  EnableWindow(g_min, !g_follow);
  EnableWindow(g_btnSet, !g_follow);
  if (g_btnAsk) EnableWindow(g_btnAsk, !g_follow);
  if (g_btnNotes) EnableWindow(g_btnNotes, !g_follow);
  if (g_btnMore) EnableWindow(g_btnMore, !g_follow);
  if (g_searchEdit) EnableWindow(g_searchEdit, !g_follow);
  if (g_searchGo) EnableWindow(g_searchGo, !g_follow);
  if (g_ocr) EnableWindow(g_ocr, !g_follow);
  if (g_btnUp) EnableWindow(g_btnUp, !g_follow);
  for (int i = 0; i < THEME_COUNT; i++)
    if (g_btnTheme[i]) EnableWindow(g_btnTheme[i], !g_follow);
  if (g_btnK2) EnableWindow(g_btnK2, !g_follow);
  if (g_btnK3) EnableWindow(g_btnK3, !g_follow);
  if (g_btnK4) EnableWindow(g_btnK4, !g_follow);
  if (g_btnK5) EnableWindow(g_btnK5, !g_follow);
  if (g_btnK6) EnableWindow(g_btnK6, !g_follow);
  if (g_btnK7) EnableWindow(g_btnK7, !g_follow);
  if (g_btnK8) EnableWindow(g_btnK8, !g_follow);
  if (g_btnKit) EnableWindow(g_btnKit, !g_follow);
  if (g_tbBg) EnableWindow(g_tbBg, !g_follow);
  if (g_tbFg) EnableWindow(g_tbFg, !g_follow);
  if (g_btnSys) EnableWindow(g_btnSys, !g_follow);
  for (int i = 0; i < ST_COUNT; i++)
    if (g_btnSt[i]) EnableWindow(g_btnSt[i], !g_follow);
  if (g_btnAi) EnableWindow(g_btnAi, !g_follow);
  if (g_btnPlm) EnableWindow(g_btnPlm, !g_follow);
  if (g_btnFiles) EnableWindow(g_btnFiles, !g_follow);
  if (g_filesRootEdit) EnableWindow(g_filesRootEdit, !g_follow);
  if (g_btnIdx) EnableWindow(g_btnIdx, !g_follow);
  if (g_plmServer) EnableWindow(g_plmServer, !g_follow);
  if (g_plmDb) EnableWindow(g_plmDb, !g_follow);
  if (g_plmUser) EnableWindow(g_plmUser, !g_follow);
  if (g_plmPass) EnableWindow(g_plmPass, !g_follow);
  if (g_chkAuto) EnableWindow(g_chkAuto, !g_follow);
  if (g_chkAnsPin) EnableWindow(g_chkAnsPin, !g_follow);
  update_pin_label();
  if (g_follow && g_setHwnd) ShowWindow(g_setHwnd, SW_HIDE);
  if (g_follow && g_askHwnd) ShowWindow(g_askHwnd, SW_HIDE);
  if (g_follow) {
    g_askOpen = FALSE;
    if (g_btnAsk) SetWindowTextW(g_btnAsk, L"Поиск");
  }
  update_pin_label();
  if (!g_follow && !g_hidden) {
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetFocus(g_edit);
  }
  InvalidateRect(g_hwnd, NULL, TRUE);
}

static void toggle_follow(void) {
  g_follow = !g_follow;
  apply_follow_state();
}


static wchar_t *nth_nonempty_line(int n) {
  if (n < 1 || !g_edit) return NULL;
  int len = GetWindowTextLengthW(g_edit);
  wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
  if (!buf) return NULL;
  GetWindowTextW(g_edit, buf, len + 1);
  int seen = 0;
  wchar_t *p = buf;
  wchar_t *out = NULL;
  while (*p) {
    wchar_t *start = p;
    while (*p && *p != L'\n' && *p != L'\r') p++;
    wchar_t *end = p;
    if (*p == L'\r') p++;
    if (*p == L'\n') p++;
    if (end == start) continue;
    seen++;
    if (seen != n) continue;
    size_t nch = (size_t)(end - start);
    out = (wchar_t *)malloc((nch + 1) * sizeof(wchar_t));
    if (out) {
      memcpy(out, start, nch * sizeof(wchar_t));
      out[nch] = 0;
    }
    break;
  }
  free(buf);
  return out;
}

static void show_status(const wchar_t *text);
static BOOL clipboard_set(const wchar_t *text) {
  size_t nch = wcslen(text);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (nch + 1) * sizeof(wchar_t));
  if (!mem) return FALSE;
  wchar_t *locked = (wchar_t *)GlobalLock(mem);
  if (!locked) {
    GlobalFree(mem);
    return FALSE;
  }
  memcpy(locked, text, (nch + 1) * sizeof(wchar_t));
  GlobalUnlock(mem);
  if (!OpenClipboard(g_hwnd)) {
    GlobalFree(mem);
    return FALSE;
  }
  EmptyClipboard();
  g_ownClip = TRUE;
  if (!SetClipboardData(CF_UNICODETEXT, mem)) {
    CloseClipboard();
    GlobalFree(mem);
    return FALSE;
  }
  CloseClipboard();
  return TRUE;
}

static void url_encode_utf8(const wchar_t *src, char *out, int cap) {
  char utf8[2400];
  int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, utf8, (int)sizeof(utf8) - 1, NULL, NULL);
  if (n <= 0) {
    out[0] = 0;
    return;
  }
  utf8[n] = 0;
  int o = 0;
  for (int i = 0; utf8[i] && o + 4 < cap; i++) {
    unsigned char c = (unsigned char)utf8[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out[o++] = (char)c;
    } else if (c == ' ') {
      out[o++] = '+';
    } else {
      static const char *hex = "0123456789ABCDEF";
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 15];
    }
  }
  out[o] = 0;
}

static void flatten_clip_line(wchar_t *s) {
  for (wchar_t *p = s; *p; p++) {
    if (*p == L'\r' || *p == L'\n' || *p == L'\t') *p = L' ';
  }
}

static void grab_last_copy(void) {
  if (g_ownClip) {
    g_ownClip = FALSE;
    return;
  }
  wchar_t q[400] = {0};
  if (!clipboard_text(q, 400)) return;
  flatten_clip_line(q);
  if (!q[0]) return;
  /* в начало списка; уже был — поднимаем наверх, а не дублируем */
  int at = g_clipHistN < CLIP_HIST ? g_clipHistN : CLIP_HIST - 1;
  for (int k = 0; k < g_clipHistN; k++)
    if (!wcscmp(g_clipHist[k], q)) {
      at = k;
      break;
    }
  if (at == g_clipHistN) g_clipHistN++;
  memmove(g_clipHist[1], g_clipHist[0], sizeof(g_clipHist[0]) * (size_t)at);
  lstrcpynW(g_clipHist[0], q, 400);
  if (g_clipEdit) SetWindowTextW(g_clipEdit, q);
}

/* «▾» у буфера копии: список пяти последних; выбранное — снова в буфер Windows */
static void clip_hist_menu(void) {
  if (!g_clipHistN) {
    show_status(L"Пока ничего не копировали");
    return;
  }
  HMENU m = CreatePopupMenu();
  for (int k = 0; k < g_clipHistN; k++) {
    wchar_t t[96];
    _snwprintf(t, 96, L"&%d   %.70s%s", k + 1, g_clipHist[k], wcslen(g_clipHist[k]) > 70 ? L"…" : L"");
    t[95] = 0;
    AppendMenuW(m, MF_STRING | (k == 0 ? MF_DEFAULT : 0), 1 + k, t);
  }
  RECT r;
  GetWindowRect(g_clipHistBtn ? g_clipHistBtn : g_clipEdit, &r);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom, 0, g_hwnd, NULL);
  DestroyMenu(m);
  if (cmd < 1 || cmd > g_clipHistN) return;
  wchar_t q[400];
  lstrcpynW(q, g_clipHist[cmd - 1], 400);
  if (!clipboard_set(q)) {
    show_status(L"Буфер занят другой программой");
    return;
  }
  /* выбранное — наверх списка и в поле */
  memmove(g_clipHist[1], g_clipHist[0], sizeof(g_clipHist[0]) * (size_t)(cmd - 1));
  lstrcpynW(g_clipHist[0], q, 400);
  if (g_clipEdit) SetWindowTextW(g_clipEdit, q);
  wchar_t msg[120];
  _snwprintf(msg, 120, L"В буфере: %.60s", q);
  msg[119] = 0;
  show_status(msg);
}

static void search_clip_buf(void) {
  wchar_t q[400] = {0};
  if (g_clipEdit) GetWindowTextW(g_clipEdit, q, 400);
  if (!q[0]) clipboard_text(q, 400);
  flatten_clip_line(q);
  if (!q[0]) {
    show_status(L"Буфер пуст — скопируйте текст");
    return;
  }
  search_web(q);
}

static void search_web(const wchar_t *q) {
  start_lookup(q);
}

static BOOL clipboard_text(wchar_t *out, int n) {
  if (!OpenClipboard(g_hwnd)) return FALSE;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  BOOL ok = FALSE;
  if (h) {
    wchar_t *p = (wchar_t *)GlobalLock(h);
    if (p) {
      lstrcpynW(out, p, n);
      GlobalUnlock(h);
      ok = out[0] != 0;
    }
  }
  CloseClipboard();
  return ok;
}

static void search_clipboard_or_edit(void) {
  wchar_t q[400] = {0};
  if (g_searchEdit) {
    GetWindowTextW(g_searchEdit, q, 400);
  }
  if (!q[0]) clipboard_text(q, 400);
  if (!q[0] && g_edit) {
    DWORD s = 0, e = 0;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (e > s) {
      int len = GetWindowTextLengthW(g_edit);
      wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
      if (buf) {
        GetWindowTextW(g_edit, buf, len + 1);
        int nch = (int)(e - s);
        if (nch > 399) nch = 399;
        memcpy(q, buf + s, nch * sizeof(wchar_t));
        q[nch] = 0;
        free(buf);
      }
    }
  }
  if (!q[0]) {
    show_status(L"Нечего искать — скопируйте текст");
    return;
  }
  search_web(q);
}

static int parse_img_id(const wchar_t *line) {
  if (!line) return 0;
  while (*line == L' ' || *line == L'\t') line++;
  if (wcsncmp(line, L"IMG:", 4) != 0) return 0;
  int id = _wtoi(line + 4);
  return id > 0 ? id : 0;
}

static void img_path(int id, wchar_t *out, int n) {
  _snwprintf(out, n, L"%s\\%d.bmp", g_imgDir, id);
}

static int next_img_id(void) {
  for (int i = 1; i < 400; i++) {
    wchar_t p[MAX_PATH];
    img_path(i, p, MAX_PATH);
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return i;
  }
  return (int)(GetTickCount() % 9000 + 1);
}

static BOOL save_clipboard_image(int id) {
  if (!OpenClipboard(g_hwnd)) return FALSE;
  HANDLE hdib = GetClipboardData(CF_DIB);
  BOOL ok = FALSE;
  if (hdib) {
    DWORD sz = (DWORD)GlobalSize(hdib);
    BITMAPINFOHEADER *bi = (BITMAPINFOHEADER *)GlobalLock(hdib);
    if (bi && sz > sizeof(BITMAPINFOHEADER)) {
      BITMAPFILEHEADER fh;
      memset(&fh, 0, sizeof(fh));
      fh.bfType = 0x4D42;
      DWORD pal = 0;
      if (bi->biBitCount <= 8) pal = (bi->biClrUsed ? bi->biClrUsed : (1u << bi->biBitCount)) * 4;
      fh.bfOffBits = (DWORD)(sizeof(fh) + bi->biSize + pal);
      if (bi->biSizeImage) fh.bfOffBits = (DWORD)(sizeof(fh) + sz - bi->biSizeImage);
      fh.bfSize = (DWORD)(sizeof(fh) + sz);
      wchar_t path[MAX_PATH];
      img_path(id, path, MAX_PATH);
      HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
      if (f != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(f, &fh, sizeof(fh), &w, NULL);
        WriteFile(f, bi, sz, &w, NULL);
        CloseHandle(f);
        ok = TRUE;
      }
      GlobalUnlock(hdib);
    } else if (bi) GlobalUnlock(hdib);
  }
  CloseClipboard();
  return ok;
}

static BOOL clipboard_set_image(int id) {
  wchar_t path[MAX_PATH];
  img_path(id, path, MAX_PATH);
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return FALSE;
  DWORD sz = GetFileSize(f, NULL);
  if (sz == INVALID_FILE_SIZE || sz <= sizeof(BITMAPFILEHEADER)) {
    CloseHandle(f);
    return FALSE;
  }
  char *buf = (char *)malloc(sz);
  if (!buf) {
    CloseHandle(f);
    return FALSE;
  }
  DWORD r = 0;
  ReadFile(f, buf, sz, &r, NULL);
  CloseHandle(f);
  BITMAPFILEHEADER *fh = (BITMAPFILEHEADER *)buf;
  if (fh->bfType != 0x4D42) {
    free(buf);
    return FALSE;
  }
  DWORD dib = r - sizeof(BITMAPFILEHEADER);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, dib);
  if (!mem) {
    free(buf);
    return FALSE;
  }
  void *p = GlobalLock(mem);
  memcpy(p, buf + sizeof(BITMAPFILEHEADER), dib);
  GlobalUnlock(mem);
  free(buf);
  if (!OpenClipboard(g_hwnd)) {
    GlobalFree(mem);
    return FALSE;
  }
  EmptyClipboard();
  g_ownClip = TRUE;
  if (!SetClipboardData(CF_DIB, mem)) {
    CloseClipboard();
    GlobalFree(mem);
    return FALSE;
  }
  CloseClipboard();
  return TRUE;
}

static BOOL replace_current_line(const wchar_t *text) {
  if (!g_edit) return FALSE;
  int line = (int)SendMessageW(g_edit, EM_LINEFROMCHAR, (WPARAM)-1, 0);
  int idx = (int)SendMessageW(g_edit, EM_LINEINDEX, (WPARAM)line, 0);
  int linelen = (int)SendMessageW(g_edit, EM_LINELENGTH, (WPARAM)idx, 0);
  int end = idx + linelen;
  int total = GetWindowTextLengthW(g_edit);
  if (end < total) {
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)end, (LPARAM)end + 2);
    SendMessageW(g_edit, EM_GETSEL, 0, 0);
  }
  SendMessageW(g_edit, EM_SETSEL, (WPARAM)idx, (LPARAM)end);
  SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)text);
  g_dirty = TRUE;
  InvalidateRect(g_hwnd, NULL, FALSE);
  return TRUE;
}

static BOOL try_paste_image(void) {
  if (!OpenClipboard(g_hwnd)) return FALSE;
  BOOL has = IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_BITMAP);
  CloseClipboard();
  if (!has) return FALSE;
  int id = next_img_id();
  if (!save_clipboard_image(id)) return FALSE;
  wchar_t line[32];
  _snwprintf(line, 32, L"IMG:%d", id);
  replace_current_line(line);
  show_status(L"Картинка в строке (миниатюра)");
  return TRUE;
}

static void draw_gutter_thumbs(HDC hdc, int dpi, int th, int fh) {
  if (!g_edit) return;
  RECT rc;
  GetClientRect(g_hwnd, &rc);
  int gut = MulDiv(GUTTER, dpi, 96);
  int clipH = MulDiv(CLIP_H, dpi, 96);
  int lineH = MulDiv(20, dpi, 96);
  int first = (int)SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
  int count = (int)SendMessageW(g_edit, EM_GETLINECOUNT, 0, 0);
  int vis = (rc.bottom - th - clipH - fh) / lineH + 1;
  for (int i = 0; i < vis; i++) {
    int ln = first + i;
    if (ln >= count) break;
    wchar_t buf[256];
    ((WORD *)buf)[0] = 255;
    int n = (int)SendMessageW(g_edit, EM_GETLINE, (WPARAM)ln, (LPARAM)buf);
    if (n < 0) n = 0;
    if (n > 254) n = 254;
    buf[n] = 0;
    int id = parse_img_id(buf);
    if (!id) continue;
    wchar_t path[MAX_PATH];
    img_path(id, path, MAX_PATH);
    HBITMAP bm = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    if (!bm) continue;
    BITMAP info;
    GetObject(bm, sizeof(info), &info);
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, bm);
    int y = th + clipH + i * lineH + 2;
    int sz = gut - 6;
    StretchBlt(hdc, 3, y, sz, sz, mem, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bm);
  }
}

/* «Чертёж» (с 2026.10.30): поле номеров слева от заметок — номер строки (01, 02…, как Ctrl+1…9),
   вертикальная черта по краю поля и черта под каждой строкой, вровень с чертой в самом поле */
static int pad_note_slots(void) {
  int len = g_edit ? GetWindowTextLengthW(g_edit) : 0, n = 0;
  if (len <= 0) return 0;
  wchar_t *buf = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
  if (!buf) return 0;
  GetWindowTextW(g_edit, buf, len + 1);
  for (int i = 0; i < len;) {
    int st = i;
    while (i < len && buf[i] != L'\n' && buf[i] != L'\r') i++;
    if (i > st) n++;
    while (i < len && (buf[i] == L'\r' || buf[i] == L'\n')) i++;
  }
  free(buf);
  return n;
}

static void draw_chertezh_gutter(HDC hdc, int dpi) {
  if (!g_edit || !IsWindowVisible(g_edit)) return;
  RECT er;
  GetWindowRect(g_edit, &er);
  MapWindowPoints(HWND_DESKTOP, g_hwnd, (POINT *)&er, 2);
  int gut = MulDiv(GUTTER, dpi, 96);
  HBRUSH ink = CreateSolidBrush(COL_INK);
  RECT v = {gut - 4, er.top, gut - 3, er.bottom};
  FillRect(hdc, &v, ink);
  DeleteObject(ink);
  int len = GetWindowTextLengthW(g_edit);
  if (len <= 0) return;
  wchar_t *buf = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
  if (!buf) return;
  GetWindowTextW(g_edit, buf, len + 1);
  HFONT ef = (HFONT)SendMessageW(g_edit, WM_GETFONT, 0, 0);
  HDC edc = GetDC(g_edit);
  int lh = 18;
  if (edc) {
    HGDIOBJ of = ef ? SelectObject(edc, ef) : NULL;
    TEXTMETRICW tm;
    GetTextMetricsW(edc, &tm);
    lh = tm.tmHeight;
    if (of) SelectObject(edc, of);
    ReleaseDC(g_edit, edc);
  }
  HBRUSH rule = CreateSolidBrush(blend_rgb(COL_LINE, COL_PAPER, 40));
  if (g_fontMonoSm) SelectObject(hdc, g_fontMonoSm);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, COL_MUTED);
  int slot = 0, eh = er.bottom - er.top;
  for (int i = 0; i < len;) {
    int st = i;
    while (i < len && buf[i] != L'\n' && buf[i] != L'\r') i++;
    int en = i;
    while (i < len && (buf[i] == L'\r' || buf[i] == L'\n')) i++;
    if (en == st) continue;
    slot++;
    LRESULT p1 = SendMessageW(g_edit, EM_POSFROMCHAR, (WPARAM)st, 0);
    LRESULT p2 = SendMessageW(g_edit, EM_POSFROMCHAR, (WPARAM)(en - 1), 0);
    if (p1 == -1 || p2 == -1) continue;
    int y1 = (short)HIWORD(p1), y2 = (short)HIWORD(p2);
    if (y1 > eh) break;
    if (y1 < 0 || y1 + lh > eh) continue;
    if (y2 + lh <= eh) {
      RECT r = {0, er.top + y2 + lh - 1, gut - 4, er.top + y2 + lh};
      FillRect(hdc, &r, rule);
    }
    {
      wchar_t line[64];
      int n = en - st < 63 ? en - st : 63;
      memcpy(line, buf + st, (size_t)n * sizeof(wchar_t));
      line[n] = 0;
      if (parse_img_id(line)) continue; /* у картинки в поле — её миниатюра */
    }
    wchar_t d[8];
    _snwprintf(d, 8, L"%02d", slot);
    RECT nr = {0, er.top + y1, gut - 7, er.top + y1 + lh};
    DrawTextW(hdc, d, -1, &nr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  }
  DeleteObject(rule);
  free(buf);
}

/* штамп внизу окна, как основная надпись: режим | строк | окно | подсказка — ячейками */
static void draw_chertezh_stamp(HDC hdc, RECT rc, int dpi, int fh, const wchar_t *hint) {
  int top = rc.bottom - fh, x = MulDiv(8, dpi, 96), pad = MulDiv(7, dpi, 96);
  wchar_t cell[3][32];
  lstrcpynW(cell[0], g_padMode == 1 ? L"ПОИСК" : L"БЛОКНОТ", 32);
  _snwprintf(cell[1], 32, L"%d СТР.", pad_note_slots());
  lstrcpynW(cell[2], g_follow ? L"ЗА КУРСОРОМ" : L"ЗАКРЕПЛЕНО", 32);
  HBRUSH ink = CreateSolidBrush(COL_INK);
  SetBkMode(hdc, TRANSPARENT);
  for (int i = 0; i < 3; i++) {
    if (i == 1 && g_padMode == 1) continue; /* в поиске строки не считаем */
    if (g_fontMonoSm) SelectObject(hdc, g_fontMonoSm);
    SetTextColor(hdc, COL_INK);
    SIZE sz = {40, 10};
    GetTextExtentPoint32W(hdc, cell[i], (int)wcslen(cell[i]), &sz);
    RECT t = {x, top, x + sz.cx, rc.bottom};
    DrawTextW(hdc, cell[i], -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += sz.cx + pad;
    RECT sep = {x, top + 2, x + 1, rc.bottom};
    FillRect(hdc, &sep, ink);
    x += pad;
  }
  DeleteObject(ink);
  if (hint && hint[0]) {
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, COL_MUTED);
    RECT t = {x, top, rc.right - MulDiv(8, dpi, 96), rc.bottom};
    /* не влезает — короче по смыслу, а не обрывом посреди слова: до « · » и до « — » */
    wchar_t sh[128];
    lstrcpynW(sh, hint, 128);
    for (;;) {
      SIZE sz = {0, 0};
      GetTextExtentPoint32W(hdc, sh, (int)wcslen(sh), &sz);
      if (sz.cx <= t.right - t.left) break;
      wchar_t *cut = wcsstr(sh, L" · ");
      wchar_t *last = NULL;
      for (wchar_t *q = cut; q; q = wcsstr(q + 3, L" · ")) last = q;
      if (last) {
        *last = 0; /* последняя часть долой: «Enter — найти · пусто — ищу скопированное» → «Enter — найти» */
        continue;
      }
      break;
    }
    DrawTextW(hdc, sh, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
}

static void send_paste(void) {
  INPUT in[4];
  memset(in, 0, sizeof(in));
  in[0].type = INPUT_KEYBOARD;
  in[0].ki.wVk = VK_CONTROL;
  in[1].type = INPUT_KEYBOARD;
  in[1].ki.wVk = 'V';
  in[2].type = INPUT_KEYBOARD;
  in[2].ki.wVk = 'V';
  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
  in[3].type = INPUT_KEYBOARD;
  in[3].ki.wVk = VK_CONTROL;
  in[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, in, sizeof(INPUT));
}

static BOOL is_our_foreground(void) {
  HWND fg = GetForegroundWindow();
  if (!fg) return FALSE;
  HWND root = GetAncestor(fg, GA_ROOT);
  return fg == g_hwnd || root == g_hwnd || fg == g_setHwnd || root == g_setHwnd ||
         fg == g_askHwnd || root == g_askHwnd;
}

static void show_status(const wchar_t *text) {
  lstrcpynW(g_status, text, 160);
  g_statusOn = TRUE;
  InvalidateRect(g_hwnd, NULL, FALSE);
  SetTimer(g_hwnd, TIMER_STATUS, 2200, NULL);
}

static void hide_to_tray(void) {
  g_hidden = TRUE;
  if (g_setHwnd) ShowWindow(g_setHwnd, SW_HIDE);
  if (g_askHwnd) ShowWindow(g_askHwnd, SW_HIDE);
  g_askOpen = FALSE;
  if (g_btnAsk) SetWindowTextW(g_btnAsk, L"Поиск");
  ShowWindow(g_hwnd, SW_HIDE);
  {
    wchar_t st[64];
    if (g_hkName[HK_MIN][0]) _snwprintf(st, 64, L"Свёрнуто · %s", g_hkName[HK_MIN]);
    else lstrcpynW(st, L"Свёрнуто — вернуть через значок у часов", 64);
    st[63] = 0;
    show_status(st);
  }
}

static void restore_from_tray(void) {
  g_hidden = FALSE;
  ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
  SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void toggle_hidden(void) {
  if (g_hidden) restore_from_tray();
  else hide_to_tray();
}

static void append_notes(const wchar_t *text) {
  if (!g_edit || !text || !text[0]) return;
  int len = GetWindowTextLengthW(g_edit);
  wchar_t *buf = (wchar_t *)malloc((len + wcslen(text) + 8) * sizeof(wchar_t));
  if (!buf) return;
  GetWindowTextW(g_edit, buf, len + 1);
  wcscat(buf, L"\r\n");
  wcscat(buf, text);
  SetWindowTextW(g_edit, buf);
  free(buf);
  g_dirty = TRUE;
}

static void draw_pad_button(const DRAWITEMSTRUCT *dis);
static HWND mk_btn(HWND parent, const wchar_t *text, int id);
/* panels draw their own title bar instead of wearing the system one */
#define PANEL_TITLE_H 36
#define ID_PANEL_CLOSE 150 /* keep clear of ID_THEME_BASE..+THEME_COUNT */
#define ID_PANEL_MIN 195   /* «–» в шапке окон с кнопкой на панели задач */
static void draw_panel_header(HWND hwnd, HDC hdc, const wchar_t *title);
static LRESULT panel_hittest(HWND hwnd, LPARAM lParam);
static void place_panel_close(HWND hwnd);
static void round_corners(HWND hwnd);
/* рамка окна-инструмента (с 2026.10.21): светлая шапка с пояснением, страница
   темнее, содержимое на карточке, подвал с кнопками — главная справа */
#define TOOL_FOOT_H 52
static void tool_frame_init(HWND hwnd);
static void tool_frame_paint(HWND hwnd, HDC hdc, const wchar_t *title, const wchar_t *desc, RECT card, int footTop);

#include "pad_extra.c"
#include "fairy_fx.c"
#include "styles.c" /* курсоры, нарисованные кодом, и просмотр в Настройках */
#include "cursor_kit.c" /* «Современный» набор: стили и переключатели карточки «Курсор» */
#include "idle_fight.c" /* бой Годжо и Сукуны, пока мышь не трогают */

/* бой при бездействии в Настройках: через сколько, ● у выбранного */
static const wchar_t *const kIdleNames[IDLE_OPTIONS] = {L"Выкл", L"30 с", L"1 мин", L"2 мин", L"5 мин"};

static const wchar_t *const kIdleSzNames[IDLE_SIZES] = {L"Мелкие", L"Средние", L"Крупные"};

static void update_idle_buttons(void) {
  for (int i = 0; i < IDLE_OPTIONS; i++) set_mark(g_btnIdle[i], g_idleSec == kIdleSec[i], kIdleNames[i]);
  for (int i = 0; i < IDLE_SIZES; i++) set_mark(g_btnIdleSz[i], g_idleSize == i, kIdleSzNames[i]);
  /* состояние — словами: включён ли и как выключить */
  if (g_btnIdleWatch)
    SetWindowTextW(g_btnIdleWatch, g_idleWatch ? L"● Режим просмотра включён · выключить"
                                               : L"Режим просмотра: бой и при работе мышью");
}

/* панель Shift: стиль и переключатели, ● у выбранного и включённого */
static const wchar_t *const kRingNames[RING_STYLES] = {L"Матовое стекло", L"Лепестки", L"Тёмное стекло", L"Тонкое кольцо"};
/* карточка «Курсор»: ● у выбранного стиля и размера и у включённого */
static void update_kit_buttons(void) {
  for (int i = 0; i < KIT_STYLES; i++) set_mark(g_btnKitSt[i], g_skin == SKIN_KIT && g_kitStyle == i, kKitNames[i]);
  for (int i = 0; i < KIT_SIZES; i++) set_mark(g_btnKitSz[i], g_kitSize == i, kKitSzNames[i]);
  set_mark(g_btnKitOut, g_kitOutline, L"Обводка");
  set_mark(g_btnKitShadow, g_kitShadow, L"Тень");
  set_mark(g_btnKitAcc, g_kitThemeAcc, L"Цвет темы");
  set_mark(g_btnKitAnim, g_kitAnim, L"Анимация");
  set_mark(g_btnKitFx, g_kitFx, L"Эффект нажатия");
  if (g_kitPreview) InvalidateRect(g_kitPreview, NULL, FALSE);
}

/* что-то в карточке сменилось: запомнить и, если набор стоит, поставить заново */
static void kit_changed(BOOL relayout) {
  save_cursor_pref();
  update_kit_buttons();
  if (g_skin == SKIN_KIT) install_scheme_cursors();
  fx_sync();
  if (relayout && g_setHwnd) layout_settings(); /* размер сменился — окошко просмотра выше или ниже */
}

static void kit_theme_changed(void) {
  if (g_kitThemeAcc && g_skin == SKIN_KIT) install_scheme_cursors();
  if (g_kitPreview) InvalidateRect(g_kitPreview, NULL, FALSE);
}

static void update_ring_buttons(void) {
  for (int i = 0; i < RING_STYLES; i++) set_mark(g_btnRing[i], g_ringStyle == i, kRingNames[i]);
  set_mark(g_btnRingBlur, g_ringBlur, L"Размытие под плитками");
  set_mark(g_btnRingDots, g_ringDots, L"Точки по контуру");
  set_mark(g_btnRingAnim, g_ringAnim, L"Плавные анимации");
}

static void set_idle_watch(int on) {
  idle_fight_watch(on);
  save_cursor_pref();
  update_idle_buttons();
  show_status(on ? L"Режим просмотра включён. Выключить: эта же кнопка, меню значка или зажать Esc на секунду"
                 : L"Режим просмотра выключен");
}

static void set_idle_sec(int sec) {
  g_idleSec = sec;
  if (sec <= 0 && g_idleWatch) idle_fight_watch(0); /* «Выкл» — бой выключен совсем, и режим просмотра тоже */
  save_cursor_pref();
  update_idle_buttons();
  if (sec <= 0) idle_fight_stop(FALSE);
  wchar_t msg[96];
  if (sec <= 0) _snwprintf(msg, 96, L"Бой при бездействии выключен");
  else if (sec < 60) _snwprintf(msg, 96, L"Бой — если мышь не трогать %d с", sec);
  else _snwprintf(msg, 96, L"Бой — если мышь не трогать %d мин", sec / 60);
  show_status(msg);
}

/* с 2026.10.21 скругления сглажены (ui_theme.c) — у всех тем и во всех
   окнах, что рисуют через эту функцию */
static void fill_round_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF border, int rad) {
  ui_round_fill(hdc, rc, rad < 1 ? 1 : rad, fill, border);
}

static void draw_panel_header(HWND hwnd, HDC hdc, const wchar_t *title) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  const PadTheme *th = &kThemes[g_theme];
  /* высота шапки — для подложки кнопок в ней (ui_theme.c) */
  SetPropW(hwnd, L"CpUiHead", (HANDLE)(INT_PTR)PANEL_TITLE_H);
  if (th->head == HEAD_BANNER) {
    /* рамка по краю окна и знамя в шапке; если справа есть крестик,
       знамя кончается до него */
    draw_knight_frame(hdc, rc);
    int right = GetDlgItem(hwnd, ID_PANEL_CLOSE) ? rc.right - 46 : rc.right - 14;
    if (GetDlgItem(hwnd, ID_PANEL_MIN)) right -= 30;
    RECT bn = {14, 7, right, PANEL_TITLE_H - 3};
    draw_banner(hdc, bn, knight_title(title));
    return;
  }
  RECT hd = {0, 0, rc.right, PANEL_TITLE_H};
  COLORREF titleCol = COL_INK;
  if (th->head == HEAD_BAND) {
    FillRect(hdc, &hd, g_paperDark);
    RECT rule = {14, PANEL_TITLE_H - 1, rc.right - 14, PANEL_TITLE_H};
    HBRUSH line = CreateSolidBrush(COL_LINE);
    FillRect(hdc, &rule, line);
    DeleteObject(line);
  } else {
    /* шапка на фоне самого окна, без отдельной полосы */
    HBRUSH bg = (HBRUSH)GetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND);
    FillRect(hdc, &hd, bg ? bg : g_paper);
    if (th->head == HEAD_RULE) {
      /* линейка под шапкой, у «Чертежа» жирная — как рамка листа */
      int w = th->caps ? 2 : 1;
      RECT rule = {14, PANEL_TITLE_H - w, rc.right - 14, PANEL_TITLE_H};
      HBRUSH line = CreateSolidBrush(blend_rgb(COL_INK, COL_PAPER, th->caps ? 40 : 150));
      FillRect(hdc, &rule, line);
      DeleteObject(line);
    } else {
      titleCol = COL_SAGE;
    }
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, titleCol);
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  RECT t = {16, 0, rc.right - 44, PANEL_TITLE_H};
  if (th->caps) {
    wchar_t up[128];
    lstrcpynW(up, title, 128);
    CharUpperBuffW(up, (DWORD)wcslen(up));
    int oldExtra = SetTextCharacterExtra(hdc, 1);
    DrawTextW(hdc, up, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SetTextCharacterExtra(hdc, oldExtra);
  } else {
    DrawTextW(hdc, title, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
}

/* the header doubles as the drag handle, the way the pad's own title does */
static LRESULT panel_hittest(HWND hwnd, LPARAM lParam) {
  /* a resizable panel must keep its edges: ask the default handler first and
     only claim the hit when it is not one of the sizing borders */
  LRESULT edge = DefWindowProcW(hwnd, WM_NCHITTEST, 0, lParam);
  if (edge >= HTLEFT && edge <= HTBOTTOMRIGHT) return edge;
  POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
  ScreenToClient(hwnd, &pt);
  if (pt.y >= 0 && pt.y < PANEL_TITLE_H) {
    HWND child = ChildWindowFromPoint(hwnd, pt);
    if (!child || child == hwnd) return HTCAPTION;
  }
  return HTCLIENT;
}

static void place_panel_close(HWND hwnd) {
  HWND b = GetDlgItem(hwnd, ID_PANEL_CLOSE);
  if (!b) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  MoveWindow(b, rc.right - 14 - 26, (PANEL_TITLE_H - 24) / 2, 26, 24, TRUE);
  HWND m = GetDlgItem(hwnd, ID_PANEL_MIN);
  if (m) MoveWindow(m, rc.right - 14 - 26 - 4 - 26, (PANEL_TITLE_H - 24) / 2, 26, 24, TRUE);
}

/* ---- рамка окна-инструмента (с 2026.10.21) ---------------------------------
   Роли поверхностей, как в приложениях Windows 11: шапка — светлая полоса с
   названием и серым пояснением, под ней страница чуть темнее, содержимое — на
   светлой карточке, внизу — светлая полоса с кнопками (главная — справа,
   остальные — слева). Окно само решает, где карточка и где начинается подвал
   (обычно rc.bottom - TOOL_FOOT_H). У рыцарской темы — прежний вид. */
static void tool_frame_init(HWND hwnd) {
  SetPropW(hwnd, L"CpUiHead", (HANDLE)(INT_PTR)-1); /* шапка светлая — без полосы темы */
  SetPropW(hwnd, L"CpUiBg", (HANDLE)(INT_PTR)1);    /* все кнопки — на светлых поверхностях */
}

static void tool_frame_paint(HWND hwnd, HDC hdc, const wchar_t *title, const wchar_t *desc, RECT card, int footTop) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  if (knight_on()) {
    FillRect(hdc, &rc, bg_brush(FALSE));
    draw_panel_header(hwnd, hdc, title);
    return;
  }
  COLORREF hair = blend_rgb(COL_LINE, COL_INK, 24);
  HBRUSH line = CreateSolidBrush(hair);
  FillRect(hdc, &rc, bg_brush(TRUE)); /* страница */
  RECT head = {0, 0, rc.right, PANEL_TITLE_H};
  FillRect(hdc, &head, bg_brush(FALSE));
  RECT hr = {0, PANEL_TITLE_H - 1, rc.right, PANEL_TITLE_H};
  FillRect(hdc, &hr, line);
  SetBkMode(hdc, TRANSPARENT);
  int right = rc.right - 50 - (GetDlgItem(hwnd, ID_PANEL_MIN) ? 30 : 0);
  RECT t = {16, 0, right, PANEL_TITLE_H};
  if (g_fontUi) SelectObject(hdc, g_fontUi);
  SetTextColor(hdc, COL_INK);
  SIZE sz = {0, 0};
  GetTextExtentPoint32W(hdc, title, (int)wcslen(title), &sz);
  DrawTextW(hdc, title, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (desc && desc[0] && 16 + sz.cx + 16 < right) {
    RECT d = {16 + sz.cx + 14, 0, right, PANEL_TITLE_H};
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, COL_MUTED);
    DrawTextW(hdc, desc, -1, &d, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
  if (card.right > card.left && card.bottom > card.top)
    ui_round_fill(hdc, card, kThemes[g_theme].radius == 0 ? 0 : 8, COL_PAPER, hair);
  if (footTop > PANEL_TITLE_H && footTop < rc.bottom) {
    RECT f = {0, footTop, rc.right, rc.bottom};
    FillRect(hdc, &f, bg_brush(FALSE));
    RECT fl = {0, footTop, rc.right, footTop + 1};
    FillRect(hdc, &fl, line);
  }
  DeleteObject(line);
}

/* Кнопка рыцарской темы. Обычная — стальная пластина с заклёпками по
   углам; главная — алая с золотой каймой; выбранная — золотая. Закрыть и
   свернуть — круглые сургучные печати. Нажатая пластина вдавливается: свет
   и тень меняются местами. */
static void draw_knight_button(HWND item, HDC dc, RECT rc, const wchar_t *label, BOOL press,
                               BOOL disab, BOOL on, BOOL primary, BOOL quiet) {
  int w = rc.right - rc.left, h = rc.bottom - rc.top;
  HWND parent = GetParent(item);
  HBRUSH under = parent ? (HBRUSH)GetClassLongPtrW(parent, GCLP_HBRBACKGROUND) : NULL;
  if (quiet) {
    if (parent) {
      RECT ir;
      GetWindowRect(item, &ir);
      MapWindowPoints(HWND_DESKTOP, parent, (POINT *)&ir, 2);
      SetBrushOrgEx(dc, -ir.left, -ir.top, NULL);
    }
    FillRect(dc, &rc, under ? under : g_paper);
    int d = (w < h ? w : h) - 2;
    int cx = rc.left + w / 2, cy = rc.top + h / 2;
    COLORREF ring = disab ? blend_rgb(KN_GOLD, COL_PAPER, 150) : KN_GOLD;
    COLORREF wax = disab ? blend_rgb(KN_CRIMSON, COL_PAPER, 150)
                         : (press ? KN_CRIMSON_DK : KN_CRIMSON);
    draw_dot(dc, cx, cy, d / 2, ring, KN_GOLD_DK);
    draw_dot(dc, cx, cy, d / 2 - 2, wax, KN_CRIMSON_DK);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disab ? COL_MUTED : KN_GOLD_LT);
    if (g_fontUi) SelectObject(dc, g_fontUi);
    DrawTextW(dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  COLORREF top, bot, edge, fg;
  if (disab) {
    top = blend_rgb(KN_STEEL_HI, COL_PAPER, 170);
    bot = blend_rgb(KN_STEEL_LO, COL_PAPER, 170);
    edge = blend_rgb(KN_STEEL_BD, COL_PAPER, 160);
    fg = COL_MUTED;
  } else if (on) {
    top = KN_GOLD_LT;
    bot = KN_GOLD;
    edge = KN_GOLD_DK;
    fg = RGB(43, 27, 14);
  } else if (primary) {
    top = RGB(170, 46, 46);
    bot = KN_CRIMSON_DK;
    edge = KN_GOLD;
    fg = KN_GOLD_LT;
  } else {
    top = KN_STEEL_HI;
    bot = KN_STEEL_LO;
    edge = KN_STEEL_BD;
    fg = RGB(30, 24, 18);
  }
  if (press && !disab) {
    COLORREF x = top;
    top = bot;
    bot = x;
  }
  fill_vgrad(dc, rc, top, bot);
  frame_rect(dc, rc, edge, (primary && !on && !disab) ? 2 : 1);
  if (!disab) {
    /* блик по верхнему краю — пластина выглядит выпуклой */
    RECT hl = {rc.left + 2, rc.top + 2, rc.right - 2, rc.top + 3};
    HBRUSH b = CreateSolidBrush(blend_rgb(press ? bot : top, RGB(255, 255, 255), 110));
    FillRect(dc, &hl, b);
    DeleteObject(b);
  }
  int inset = 5;
  SIZE lsz = {0, 0};
  if (g_fontUi) SelectObject(dc, g_fontUi);
  GetTextExtentPoint32W(dc, label, (int)wcslen(label), &lsz);
  /* заклёпки — только если они не съедают место под надпись: на узкой
     кнопке понятная надпись важнее украшения */
  if (!disab && h >= 20 && w >= lsz.cx + 26) {
    COLORREF rv = (primary && !on) ? KN_GOLD : KN_RIVET;
    COLORREF rvEdge = blend_rgb(rv, RGB(0, 0, 0), 110);
    int ox = 5, oy = h >= 26 ? 6 : 5;
    draw_dot(dc, rc.left + ox, rc.top + oy, 2, rv, rvEdge);
    draw_dot(dc, rc.right - ox - 1, rc.top + oy, 2, rv, rvEdge);
    draw_dot(dc, rc.left + ox, rc.bottom - oy - 1, 2, rv, rvEdge);
    draw_dot(dc, rc.right - ox - 1, rc.bottom - oy - 1, 2, rv, rvEdge);
    inset = 11;
  }
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, fg);
  if (g_fontUi) SelectObject(dc, g_fontUi);
  RECT tr = {rc.left + inset, rc.top, rc.right - inset, rc.bottom};
  if (press && !disab) OffsetRect(&tr, 0, 1);
  DrawTextW(dc, label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* стрелка вниз на кнопке «▾» у буфера: рисуем сами — знака ▾ нет не во
   всех шрифтах тем */
static void draw_down_arrow(HDC dc, RECT rc, COLORREF c) {
  int h = rc.bottom - rc.top, w = h * 2 / 5, hh = h / 5;
  if (w < 6) w = 6;
  if (hh < 3) hh = 3;
  int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
  POINT p[3] = {{cx - w / 2, cy - hh / 2}, {cx + w / 2 + 1, cy - hh / 2}, {cx, cy + hh / 2 + 1}};
  HBRUSH b = CreateSolidBrush(c);
  HPEN pn = CreatePen(PS_SOLID, 1, c);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pn);
  Polygon(dc, p, 3);
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(b);
  DeleteObject(pn);
}

/* подложка переключателя «Блокнот | Поиск» — чуть темнее шапки */
static COLORREF seg_fill_color(void) {
  return blend_rgb(COL_PAPER_DARK, COL_INK, 18);
}

/* значки кнопок шапки (с 2026.09.23.41): булавка, ⋯, шестерёнка — линиями,
   чтобы были в любой теме и без шрифта значков */
enum { GL_PIN, GL_DOTS, GL_GEAR };
static void draw_ui_glyph(HDC dc, RECT rc, int kind, COLORREF c) {
  int w = rc.right - rc.left, h = rc.bottom - rc.top;
  int s = (w < h ? w : h) * 7 / 10; /* рисунок — большая часть кнопки */
  int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
  int pw = s >= 18 ? 2 : 1;
  HPEN pn = CreatePen(PS_SOLID, pw, c);
  HBRUSH br = CreateSolidBrush(c);
  HGDIOBJ op = SelectObject(dc, pn), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
  if (kind == GL_PIN) { /* канцелярская кнопка: шляпка, ножка, площадка, игла */
    int u = s / 8 > 1 ? s / 8 : 1, top = cy - s / 2;
    SelectObject(dc, br);
    Ellipse(dc, cx - u * 2 - 1, top, cx + u * 2 + 2, top + u * 3 + 1);          /* шляпка */
    Rectangle(dc, cx - u, top + u * 2, cx + u + 1, top + u * 5);                /* ножка */
    RoundRect(dc, cx - u * 3, top + u * 5 - 1, cx + u * 3 + 1, top + u * 6, 2, 2); /* площадка */
    MoveToEx(dc, cx, top + u * 6, NULL);                                         /* игла */
    LineTo(dc, cx, cy + s / 2 + 1);
  } else if (kind == GL_DOTS) {
    SelectObject(dc, br);
    int r = s / 8 > 1 ? s / 8 : 1;
    for (int k = -1; k <= 1; k++) {
      int x = cx + k * (s / 3 + 1);
      Ellipse(dc, x - r, cy - r, x + r + 1, cy + r + 1);
    }
  } else { /* шестерёнка: толстое кольцо и восемь коротких зубцов вплотную */
    int r = s * 5 / 18, r1 = r, r2 = s * 9 / 20;
    HPEN ring = CreatePen(PS_SOLID, pw + 1, c);
    SelectObject(dc, ring);
    Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(dc, pn);
    DeleteObject(ring);
    static const int dx[8] = {100, 71, 0, -71, -100, -71, 0, 71}, dy[8] = {0, 71, 100, 71, 0, -71, -100, -71};
    HPEN pn2 = CreatePen(PS_SOLID, pw + 1, c);
    SelectObject(dc, pn2);
    for (int k = 0; k < 8; k++) {
      MoveToEx(dc, cx + dx[k] * r1 / 100, cy + dy[k] * r1 / 100, NULL);
      LineTo(dc, cx + dx[k] * r2 / 100, cy + dy[k] * r2 / 100);
    }
    SelectObject(dc, pn);
    DeleteObject(pn2);
  }
  SelectObject(dc, op);
  SelectObject(dc, ob);
  DeleteObject(pn);
  DeleteObject(br);
}

static int ui_glyph_of(int id) {
  return id == ID_PIN ? GL_PIN : id == ID_MORE_BTN ? GL_DOTS : id == ID_SETTINGS ? GL_GEAR : -1;
}

/* Кнопка (кроме рыцарской темы): под ней — фон родителя (углы сливаются с
   окном), форма сглажена, фокус клавиатуры — кольцо цвета акцента; всё
   рисуется в памяти и кладётся одним разом, без мигания. icon — знак из
   Segoe Fluent Icons; нет шрифта значков — рисунок линиями (glyph) или
   надпись. */
static void pad_btn_paint(const DRAWITEMSTRUCT *dis, COLORREF fill, COLORREF bd, int rad, COLORREF fg,
                          const wchar_t *label, int glyph, wchar_t icon) {
  HWND btn = dis->hwndItem, parent = GetParent(btn);
  RECT pr;
  GetWindowRect(btn, &pr);
  MapWindowPoints(HWND_DESKTOP, parent, (POINT *)&pr, 2);
  UiCanvas cv;
  BOOL ok = ui_canvas_begin(&cv, dis->hDC, pr, dis->rcItem.left, dis->rcItem.top, FALSE);
  HDC dc = ok ? cv.dc : dis->hDC;
  RECT r = ok ? pr : dis->rcItem;
  if (ok) {
    ui_underlay(&cv, parent, pr);
    UiShape s;
    memset(&s, 0, sizeof(s));
    s.r = (float)rad;
    if (fill == (COLORREF)-1) { /* без своей подложки: bd — оттенок, в старшем байте — сколько его */
      s.fill = bd & 0xFFFFFF;
      s.fillA = (int)((DWORD)bd >> 24);
    } else {
      s.fill = fill;
      s.fillA = 255;
      if (bd != fill) {
        s.border = bd;
        s.borderA = 255;
        s.bw = 1.0f;
      }
    }
    ui_shape(&cv, r, &s);
    if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
      UiShape f;
      memset(&f, 0, sizeof(f));
      f.r = (float)(rad > 2 ? rad - 2 : rad);
      f.border = fill == COL_SAGE ? COL_PAPER : COL_SAGE;
      f.borderA = 230;
      f.bw = 1.5f;
      RECT fr = r;
      InflateRect(&fr, -2, -2);
      ui_shape(&cv, fr, &f);
    }
  } else if (fill != (COLORREF)-1) {
    fill_round_rect(dc, r, fill, bd, rad);
  }
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, fg);
  int side =(r.right - r.left) < (r.bottom - r.top) ? r.right - r.left : r.bottom - r.top;
  HFONT ifont = icon ? ui_icon_font(side * (glyph == -3 ? 38 : 50) / 100) : NULL;
  if (ifont) {
    HGDIOBJ of = SelectObject(dc, ifont);
    DrawTextW(dc, &icon, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);
  } else if (glyph == -2) {
    draw_down_arrow(dc, r, fg);
  } else if (glyph >= 0) {
    draw_ui_glyph(dc, r, glyph, fg);
  } else {
    HGDIOBJ of = g_fontUi ? SelectObject(dc, g_fontUi) : NULL;
    DrawTextW(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (of) SelectObject(dc, of);
  }
  if (ok) ui_canvas_end(&cv, dis->hDC);
}

/* значки разделов Настроек (Segoe Fluent Icons / MDL2), по номеру раздела */
static const wchar_t kSecIcons[SEC_COUNT] = {0xE8B0, 0xE945, 0xE790, 0xE721, 0xE712, 0xE99A, 0xE916};

/* раздел Настроек — строка левой панели, как в «Параметрах» Windows 11:
   значок и название; открытый — на чуть более тёмной подложке с полоской
   цвета акцента слева, под мышью подложка проступает плавно */
static void draw_settings_tab(const DRAWITEMSTRUCT *dis, int sec) {
  RECT rc = dis->rcItem;
  BOOL open = sec == g_tab, press = (dis->itemState & ODS_SELECTED) != 0;
  wchar_t t[32] = {0};
  GetWindowTextW(dis->hwndItem, t, 32);
  HWND btn = dis->hwndItem, parent = GetParent(btn);
  RECT pr;
  GetWindowRect(btn, &pr);
  MapWindowPoints(HWND_DESKTOP, parent, (POINT *)&pr, 2);
  UiCanvas cv;
  if (!ui_canvas_begin(&cv, dis->hDC, pr, rc.left, rc.top, FALSE)) return;
  SetBrushOrgEx(cv.dc, -cv.lx, -cv.ly, NULL);
  FillRect(cv.dc, &pr, bg_brush(TRUE)); /* подложка панели разделов */
  int hot = ui_hover_level(btn);
  int shade = open ? 20 : hot * 12 / 255;
  if (press) shade += 10;
  UiShape s;
  memset(&s, 0, sizeof(s));
  s.r = (float)ui_px(btn, 6);
  if (shade > 0) {
    s.fill = blend_rgb(COL_PAPER_DARK, COL_INK, shade);
    s.fillA = 255;
    ui_shape(&cv, pr, &s);
  }
  int h = pr.bottom - pr.top, x = pr.left + ui_px(btn, 12);
  if (open) { /* полоска акцента */
    int ih = h * 45 / 100;
    RECT bar = {pr.left + ui_px(btn, 1), pr.top + (h - ih) / 2, pr.left + ui_px(btn, 4), pr.top + (h - ih) / 2 + ih};
    s.fill = COL_SAGE;
    s.r = -1;
    ui_shape(&cv, bar, &s);
  }
  SetTextColor(cv.dc, COL_INK);
  HFONT ifont = ui_icon_font(ui_px(btn, 16));
  if (ifont) {
    HGDIOBJ of = SelectObject(cv.dc, ifont);
    RECT ir = {x, pr.top, x + ui_px(btn, 18), pr.bottom};
    DrawTextW(cv.dc, &kSecIcons[sec], 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(cv.dc, of);
    x += ui_px(btn, 30);
  }
  HGDIOBJ of = g_fontBody ? SelectObject(cv.dc, g_fontBody) : NULL;
  RECT tr = {x, pr.top, pr.right - ui_px(btn, 6), pr.bottom};
  DrawTextW(cv.dc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (of) SelectObject(cv.dc, of);
  if ((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT)) {
    UiShape f;
    memset(&f, 0, sizeof(f));
    f.r = (float)ui_px(btn, 6);
    f.border = COL_SAGE;
    f.borderA = 230;
    f.bw = 1.5f;
    ui_shape(&cv, pr, &f);
  }
  ui_canvas_end(&cv, dis->hDC);
}

static void draw_pad_button(const DRAWITEMSTRUCT *dis) {
  if (!dis || dis->CtlType != ODT_BUTTON) return;
  RECT rc = dis->rcItem;
  BOOL press = (dis->itemState & ODS_SELECTED) != 0;
  BOOL disab = (dis->itemState & ODS_DISABLED) != 0;
  int id = (int)dis->CtlID;
  ui_btn_track(dis->hwndItem); /* подсветка под мышью — у каждой кнопки */
  if (id >= ID_SEC_BASE && id < ID_SEC_BASE + SEC_COUNT) {
    draw_settings_tab(dis, id - ID_SEC_BASE);
    return;
  }
  wchar_t t[96] = {0};
  GetWindowTextW(dis->hwndItem, t, 96);
  BOOL themeBtn = id >= ID_THEME_BASE && id < ID_THEME_BASE + THEME_COUNT;
  int ti = themeBtn ? id - ID_THEME_BASE : -1;
  BOOL on = t[0] == 0x25CF || (themeBtn && g_theme == ti);
  BOOL primary = id == ID_SEARCH_GO || id == ID_UPDATE || (id == ID_PIN && !g_follow) ||
                 id == ID_ANS_OPEN || id == ID_ANS_OPENTP || (id == ID_ASK_TAB && g_padMode == 1) || (id == ID_NOTES_TAB && g_padMode == 0) ||
                 id == ID_ND_INVITE || id == ID_ND_ACCEPT || id == ID_ND_ROLL || id == ID_ND_DONE ||
                 id == ID_SH_INVITE || id == ID_SH_ACCEPT || id == ID_SH_DRAWOK ||
                 id == ID_MB_INVITE || id == ID_MB_ACCEPT || id == ID_MB_READY ||
                 id == ID_SX_INVITE || id == ID_SX_ACCEPT || id == ID_SX_DRAWOK ||
                 id == ID_ANS_DRAW || id == ID_ANS_SHOW || /* чертёж нашёлся — кнопки синие */
                 id == ID_MSG_SEND || id == ID_MSG_REPLY || id == ID_RT_BUILD || id == ID_RT_SAVE ||
                 id == 4714 || /* «Применить» в окне «Спросить ИИ» */
                 id == 4703 || /* и «Отправить» там же */
                 id == 4722 || id == 4725 || /* «Сохранить…» в «Что помнит ИИ» (ai_memory.c) */
                 id == 4842 ||               /* «Дальше» в окне «В Союз» (soyuz_ui.c) */
                 id == 4882 ||               /* «Применить» в окне «Режимы» (norms_ui.c) */
                 id == ID_XP_SAVE || id == ID_PK_SOYUZ || id == 315; /* главное действие в подвале окон PLM; 315 — «Чертежи…» окна «ЭСИ и ТП» */
  BOOL quiet = id == ID_CLOSE || id == ID_MIN || id == ID_PANEL_CLOSE || id == ID_PANEL_MIN;
  COLORREF fill, fg, bd;
  /* рыцарская тема рисует кнопки целиком по-своему; кнопка выбора
     этой темы в Настройках — тоже, чтобы было видно, что получится */
  if ((themeBtn && ti >= 0 && kThemes[ti].btn == BTN_KNIGHT) ||
      (!themeBtn && kThemes[g_theme].btn == BTN_KNIGHT)) {
    const wchar_t *kl = themeBtn ? kThemes[ti].name
                                 : ((t[0] == 0x25CF && t[1] == L' ') ? t + 2 : t);
    BOOL tab = id == ID_NOTES_TAB || id == ID_ASK_TAB;
    BOOL tabOn = (id == ID_NOTES_TAB && g_padMode == 0) || (id == ID_ASK_TAB && g_padMode == 1);
    int gl = ui_glyph_of(id);
    draw_knight_button(dis->hwndItem, dis->hDC, rc, (id == ID_CLIPHIST || gl >= 0) ? L"" : kl, press, disab,
                       tab ? tabOn : on, (primary && !tab) || themeBtn, quiet);
    if (id == ID_CLIPHIST) draw_down_arrow(dis->hDC, rc, COL_INK);
    if (gl >= 0) draw_ui_glyph(dis->hDC, rc, gl, COL_INK);
    return;
  }
  /* «Блокнот | Поиск» — половинки одного переключателя: выбранная — светлая
     «таблетка» на тёмной подложке, другая — просто надпись */
  if ((id == ID_NOTES_TAB || id == ID_ASK_TAB) && chertezh_on()) {
    /* «Чертёж»: две ячейки — выбранная на белом с чертой акцента снизу, другая на сером */
    BOOL act = (id == ID_NOTES_TAB && g_padMode == 0) || (id == ID_ASK_TAB && g_padMode == 1);
    HBRUSH fb = act ? g_paper : g_paperDark;
    FillRect(dis->hDC, &rc, fb);
    if (act) {
      HBRUSH ab = CreateSolidBrush(COL_SAGE);
      RECT bar = {rc.left, rc.bottom - 3, rc.right, rc.bottom};
      FillRect(dis->hDC, &bar, ab);
      DeleteObject(ab);
    }
    wchar_t up[32];
    lstrcpynW(up, t, 32);
    CharUpperBuffW(up, (DWORD)wcslen(up));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, disab ? COL_MUTED : act ? COL_INK : COL_MUTED);
    if (g_fontUi) SelectObject(dis->hDC, g_fontUi);
    int ox = SetTextCharacterExtra(dis->hDC, 1);
    DrawTextW(dis->hDC, up, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SetTextCharacterExtra(dis->hDC, ox);
    return;
  }
  if (id == ID_NOTES_TAB || id == ID_ASK_TAB) {
    BOOL act = (id == ID_NOTES_TAB && g_padMode == 0) || (id == ID_ASK_TAB && g_padMode == 1);
    HBRUSH sb = CreateSolidBrush(seg_fill_color());
    FillRect(dis->hDC, &rc, sb);
    DeleteObject(sb);
    int tr = kThemes[g_theme].radius, hh = rc.bottom - rc.top;
    int rad = tr < 0 ? hh / 2 : tr == 0 ? 0 : tr;
    if (act) fill_round_rect(dis->hDC, rc, press ? COL_PAPER_DARK : COL_PAPER, COL_LINE, rad);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, disab ? COL_MUTED : act ? COL_INK : blend_rgb(COL_INK, COL_PAPER, 110));
    if (g_fontUi) SelectObject(dis->hDC, g_fontUi);
    DrawTextW(dis->hDC, t, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return;
  }
  if (themeBtn && ti >= 0) {
    const PadTheme *th = &kThemes[ti];
    if (on) {
      fill = th->sage;
      fg = th->paper;
      bd = th->ink;
    } else {
      fill = th->paper;
      fg = th->ink;
      bd = th->sage;
    }
    if (press) fill = blend_rgb(fill, th->ink, 36);
    if (disab) {
      fill = blend_rgb(fill, COL_PAPER_DARK, 80);
      fg = th->muted;
    }
  } else {
    int style = kThemes[g_theme].btn;
    if (disab) {
      fill = blend_rgb(COL_PAPER, COL_PAPER_DARK, 160);
      fg = COL_MUTED;
      bd = style == BTN_TONE ? fill : COL_LINE;
    } else if (style == BTN_LINE) {
      /* чертёж: контур вместо заливки; главная кнопка — контур и
         надпись цветом, выбранная — залита, иначе её не отличить */
      if (on) {
        fill = press ? blend_rgb(COL_SAGE, COL_INK, 48) : COL_SAGE;
        fg = COL_PAPER;
        bd = fill;
      } else if (primary) {
        fill = press ? blend_rgb(COL_PAPER, COL_SAGE, 40) : COL_PAPER;
        fg = COL_SAGE;
        bd = COL_SAGE;
      } else {
        fill = press ? blend_rgb(COL_PAPER, COL_INK, 30) : COL_PAPER;
        fg = COL_INK;
        bd = blend_rgb(COL_INK, COL_PAPER, 110);
      }
    } else if (style == BTN_TONE) {
      /* мягкая: подложка цвета темы, рамок нет совсем */
      if (on || primary) {
        fill = press ? blend_rgb(COL_SAGE, COL_INK, 48) : COL_SAGE;
        fg = COL_PAPER;
      } else {
        fill = blend_rgb(COL_PAPER, COL_SAGE, press ? 70 : 34);
        fg = blend_rgb(COL_INK, COL_SAGE, 60);
      }
      bd = fill;
    } else if (on || primary) {
      fill = press ? blend_rgb(COL_SAGE, COL_INK, 48) : COL_SAGE;
      fg = COL_PAPER;
      bd = fill;
    } else if (quiet) {
      fill = press ? COL_PAPER : COL_PAPER_DARK;
      fg = COL_INK;
      bd = COL_LINE;
    } else {
      fill = press ? blend_rgb(COL_PAPER_DARK, COL_INK, 30) : COL_PAPER;
      fg = COL_INK;
      bd = COL_LINE;
    }
  }
  /* тёмные темы («Графит», «Синька», «Цех»): выбранное и включённое — подкрашенная подложка
     с каймой акцента, а не сплошная заливка: ряд включённых переключателей на тёмном горел,
     как вывеска. Главные кнопки («Найти») остаются залитыми — она одна */
  if (on && !primary && !themeBtn && !disab && ui_theme_dark()) {
    fill = blend_rgb(COL_PAPER, COL_SAGE, press ? 96 : 66);
    fg = COL_INK;
    bd = COL_SAGE;
  }
  int hot = disab ? 0 : ui_hover_level(dis->hwndItem);
  int gl = ui_glyph_of(id);
  /* значки — шрифтом Segoe Fluent Icons, если он есть (иначе линиями) */
  wchar_t icon = id == ID_CLIPHIST ? 0xE70D /* ChevronDown */
                 : gl == GL_PIN    ? (g_follow ? 0xE718 : 0xE840) /* Pin / Pinned */
                 : gl == GL_DOTS   ? 0xE712
                 : gl == GL_GEAR   ? 0xE713
                                   : 0;
  if (quiet) {
    /* «закрыть» и «свернуть» — как в заголовке Windows 11: без подложки,
       под мышью проступает серая, у «закрыть» — красная */
    BOOL closeBtn = id == ID_CLOSE || id == ID_PANEL_CLOSE;
    if ((t[0] == 0x00D7 || t[0] == 0x2013 || t[0] == L'-') && t[1] == 0) icon = closeBtn ? 0xE8BB : 0xE921;
    int lvl = press ? 255 : hot;
    COLORREF tint = closeBtn ? RGB(196, 43, 28) : COL_INK;
    int amt = closeBtn ? lvl : lvl * (press ? 34 : 22) / 255;
    pad_btn_paint(dis, (COLORREF)-1, (COLORREF)(tint | ((DWORD)amt << 24)), kThemes[g_theme].radius == 0 ? 0 : 6,
                  closeBtn && lvl > 128 ? RGB(255, 255, 255) : (disab ? COL_MUTED : COL_INK), t, -3, icon);
    return;
  }
  /* под мышью — чуть темнее (с 2026.10.21), плавно за 0,1 с */
  if (hot && !press) {
    BOOL filled = on || primary;
    fill = blend_rgb(fill, COL_INK, hot * (filled ? 26 : 14) / 255);
    if (!filled) bd = blend_rgb(bd, COL_INK, hot * 40 / 255);
  }
  int h = rc.bottom - rc.top;
  /* скругление задаёт тема; кнопки выбора темы рисуются формой своей
     темы — сразу видно, что получится */
  int tr = kThemes[themeBtn && ti >= 0 ? ti : g_theme].radius;
  int rad;
  if (tr < 0) rad = h / 2;
  else if (tr == 0) rad = 0;
  else rad = h >= 26 ? tr : (tr > 2 ? tr - 2 : tr);
  const wchar_t *label = t;
  if (themeBtn) label = kThemes[ti].name;
  else if (t[0] == 0x25CF && t[1] == L' ') label = t + 2;
  pad_btn_paint(dis, fill, bd, rad, fg, label, id == ID_CLIPHIST ? -2 : gl, icon);
  /* «Стекло»: по контуру кнопки — пунктир из точек, как у плиток панели Shift */
  if (kThemes[themeBtn && ti >= 0 ? ti : g_theme].btn == BTN_DOTS && !disab) {
    LOGBRUSH lb = {BS_SOLID, (on || primary) ? blend_rgb(fg, fill, 70) : blend_rgb(COL_SAGE, fill, 150), 0};
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_DOT | PS_ENDCAP_ROUND | PS_JOIN_ROUND, 2, &lb, 0, NULL);
    if (pen) {
      HGDIOBJ oldPen = SelectObject(dis->hDC, pen), oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
      int in = 4, rr = rad > in ? (rad - in) * 2 : 2;
      RoundRect(dis->hDC, rc.left + in, rc.top + in, rc.right - in, rc.bottom - in, rr, rr);
      SelectObject(dis->hDC, oldBrush);
      SelectObject(dis->hDC, oldPen);
      DeleteObject(pen);
    }
  }
}

static HWND mk_btn(HWND parent, const wchar_t *text, int id) {
  HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 80, 26,
                           parent, (HMENU)(INT_PTR)id, NULL, NULL);
  if (b && g_fontUi) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  /* крестик в шапке — значит, у окна шапка панели: её высоту знают сразу,
     ещё до первой отрисовки (подложка кнопок в шапке-полосе, ui_theme.c) */
  if (b && parent && (id == ID_PANEL_CLOSE || id == ID_PANEL_MIN) && !GetPropW(parent, L"CpUiHead"))
    SetPropW(parent, L"CpUiHead", (HANDLE)(INT_PTR)PANEL_TITLE_H);
  if (b) ui_btn_track(b);
  return b;
}

static void run_ocr_test(void) {
  start_ocr_pick();
}

/* Сброс скопированного: буфер обмена чистится, строка поиска пустеет.
   Нужен, чтобы случайно не вставить в чужой документ то, что копировал
   час назад, и чтобы видеть, что буфер действительно пуст. */
static void clear_copied(void) {
  BOOL ok = FALSE;
  for (int tries = 0; tries < 5 && !ok; tries++) {
    if (OpenClipboard(g_hwnd)) {
      ok = EmptyClipboard();
      CloseClipboard();
    } else {
      Sleep(30);
    }
  }
  if (g_clipEdit) SetWindowTextW(g_clipEdit, L"");
  g_clipHistN = 0; /* и пять последних копирований */
  /* «Сброс» заодно останавливает перенос в 1С — отдельная кнопка не нужна */
  onec_stop(NULL);
  /* и очищает весь блокнот. Спрашиваем: заметки пропадут разом. Прежние
     заметки остаются в notes_before_reset.txt — если нажали зря. */
  BOOL cleared = FALSE;
  if (g_edit && GetWindowTextLengthW(g_edit) > 0) {
    if (MessageBoxW(g_hwnd, L"Очистить весь блокнот?\n\nПрежние заметки сохранятся в notes_before_reset.txt.",
                    L"Сброс", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES) {
      save_notes(); /* последние правки — в файл, потом копия */
      wchar_t bak[MAX_PATH];
      lstrcpynW(bak, g_notesPath, MAX_PATH);
      wchar_t *slash = wcsrchr(bak, L'\\');
      if (slash) {
        lstrcpynW(slash + 1, L"notes_before_reset.txt", (int)(MAX_PATH - (slash + 1 - bak)));
        CopyFileW(g_notesPath, bak, FALSE);
      }
      SetWindowTextW(g_edit, L"");
      g_dirty = TRUE;
      save_notes();
      /* рамки и номера строк рисуются поверх поля — стереть и их */
      RedrawWindow(g_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
      cleared = TRUE;
    }
  }
  show_status(cleared ? L"Блокнот очищен" : (ok ? L"Буфер очищен" : L"Буфер занят другой программой"));
}

static void paste_line(int n) {
  wchar_t *line = nth_nonempty_line(n);
  if (!line) {
    wchar_t msg[64];
    _snwprintf(msg, 64, L"Строки %d нет", n);
    show_status(msg);
    return;
  }
  BOOL ok;
  int img = parse_img_id(line);
  if (img) ok = clipboard_set_image(img);
  else ok = clipboard_set(line);
  if (!ok) {
    free(line);
    show_status(L"Не удалось скопировать");
    return;
  }
  wchar_t msg[160];
  wchar_t pre[40]; /* «Ctrl+» или своё, как назначили: «Alt+1…9» → «Alt+» */
  lstrcpynW(pre, g_hkName[HK_SNIP], 40);
  wchar_t *dig = wcsstr(pre, L"1…9");
  if (dig) *dig = 0;
  if (img) _snwprintf(msg, 160, L"%s%d  ·  картинка", pre, n);
  else _snwprintf(msg, 160, L"%s%d  ·  в буфере", pre, n);
  show_status(msg);
  if (!is_our_foreground()) {
    SetTimer(g_hwnd, TIMER_PASTE, 40, NULL);
  }
  free(line);
}

/* hotkeys.c */
static void tray_tip_text(wchar_t *out, int n);
static void hk_with(const wchar_t *text, int f, wchar_t *out, int n);
static void hk_register_all(HWND hwnd);
static void hk_unregister_all(HWND hwnd);
static void hk_show(void);

static void add_tray(HWND hwnd) {
  memset(&g_nid, 0, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = TRAY_UID;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAY;
  g_nid.hIcon = app_icon(TRUE);
  tray_tip_text(g_nid.szTip, 128);
  g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void msg_compose_show(void); /* msg.c */
static void tray_menu(HWND hwnd) {
  POINT pt;
  GetCursorPos(&pt);
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1, g_follow ? L"Закрепить" : L"Следовать за курсором");
  wchar_t mMin[64], mOcr[64], mCur[64];
  hk_with(g_hidden ? L"Показать окно" : L"Свернуть", HK_MIN, mMin, 64);
  hk_with(L"Выделить и прочитать", HK_OCR, mOcr, 64);
  hk_with(L"Следующий курсор", HK_CURSOR, mCur, 64);
  AppendMenuW(menu, MF_STRING, 14, mMin);
  AppendMenuW(menu, MF_STRING, 16, L"Настройки");
  AppendMenuW(menu, MF_STRING, 15, mOcr);
  AppendMenuW(menu, MF_STRING, 17, L"Проверить обновления");
  /* с 2026.09.23.75 — выключено: сама программа себя не обновляет (update.c) */
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 22, L"   обновляться самостоятельно — пока не работает");
  AppendMenuW(menu, MF_STRING, 18, L"Что нового в папке");
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 23, L"Чат с коллегами — пока не работает");
  AppendMenuW(menu, MF_STRING, 21, L"Нарды");
  AppendMenuW(menu, MF_STRING, 24, L"Шашки");
  AppendMenuW(menu, MF_STRING, 26, L"Морской бой");
  AppendMenuW(menu, MF_STRING, 27, L"Шахматы");
  AppendMenuW(menu, MF_STRING | (g_idleWatch ? MF_CHECKED : 0), 25, L"Бой Годжо и Сукуны: смотреть, не пряча");
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuW(menu, MF_STRING, 13, mCur);
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuW(menu, MF_STRING, 2, L"Открыть файл заметок");
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuW(menu, MF_STRING, 3, L"Выход");
  SetForegroundWindow(hwnd);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
  DestroyMenu(menu);
  if (cmd == 1) toggle_follow();
  else if (cmd == 2) {
    save_notes();
    ShellExecuteW(NULL, L"open", g_notesPath, NULL, NULL, SW_SHOWNORMAL);
  } else if (cmd == 3) {
    DestroyWindow(hwnd);
  } else if (cmd == 13) cycle_skin();
  else if (cmd == 14) {
    if (g_hidden) restore_from_tray();
    else hide_to_tray();
  } else if (cmd == 15) run_ocr_test();
  else if (cmd == 17) start_update();
  else if (cmd == 23 && 0) msg_compose_show(); /* чат выключен с 2026.09.23.75 */
  else if (cmd == 22 && !UPD_AUTO_DISABLED) {
    BOOL on = upd_auto_off();
    upd_auto_set(on);
    show_status(on ? L"Обновления ставятся сами" : L"Обновления — только по кнопке");
  }
  else if (cmd == 18) files_show_changes();
  else if (cmd == 21) nardy_show();
  else if (cmd == 24) shashki_show();
  else if (cmd == 26) morskoy_show();
  else if (cmd == 27) shahmaty_show();
  else if (cmd == 25) set_idle_watch(!g_idleWatch);
  else if (cmd == 16) {
    if (g_follow) toggle_follow();
    toggle_settings();
  }
}

static HFONT make_font(const wchar_t *face, int px, int weight) {
  HDC hdc = GetDC(NULL);
  int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
  ReleaseDC(NULL, hdc);
  int h = -MulDiv(px, dpi, 72);
  return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE, face);
}

/* Ctrl+1…9 копируют первые девять непустых строк заметок. Какая строка под
   каким номером — приходилось держать в голове и пересчитывать после каждой
   правки. Теперь они обведены и пронумерованы прямо в тексте.
   С 2026.09.23.40 у каждой непустой строки справа ещё и кнопка «копировать»
   (два листка): щёлкнул — строка в буфере, кнопка на миг зеленеет. Под
   кнопки у поля оставлено поле справа (note_margins). */
typedef struct {
  RECT r;          /* кнопка, в координатах поля */
  int start, end;  /* строка в тексте поля */
} NoteCopyBtn;
static NoteCopyBtn g_noteBtn[96];
static int g_noteBtnN;
static int g_noteFlash = -1; /* какая кнопка только что скопировала — зелёная */
static int g_noteHot = -1;   /* над какой мышь */
#define TIMER_NOTE_FLASH 77  /* таймер у самого поля */

/* ширина правого поля под номер и кнопку — от высоты строки */
static int note_gutter(HWND ed) {
  HDC dc = GetDC(ed);
  if (!dc) return 40;
  HFONT ef = (HFONT)SendMessageW(ed, WM_GETFONT, 0, 0);
  HGDIOBJ of = ef ? SelectObject(dc, ef) : NULL;
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  if (of) SelectObject(dc, of);
  ReleaseDC(ed, dc);
  return tm.tmHeight + 22; /* кнопка в высоту строки + номер */
}

static void note_margins(void) {
  if (!g_edit) return;
  SendMessageW(g_edit, EM_SETMARGINS, EC_RIGHTMARGIN, MAKELONG(0, note_gutter(g_edit)));
}

static void draw_slot_marks(HWND ed) {
  g_noteBtnN = 0;
  if (!ed) return;
  int len = GetWindowTextLengthW(ed);
  if (len <= 0) return;
  wchar_t *buf = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
  if (!buf) return;
  GetWindowTextW(ed, buf, len + 1);
  HDC dc = GetDC(ed);
  if (!dc) {
    free(buf);
    return;
  }
  RECT cl;
  GetClientRect(ed, &cl);
  HFONT ef = (HFONT)SendMessageW(ed, WM_GETFONT, 0, 0);
  HFONT prevFont = ef ? (HFONT)SelectObject(dc, ef) : NULL;
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int lh = tm.tmHeight;
  int bw = lh;                       /* кнопка — квадрат в высоту строки */
  int numW = 18;                     /* место под номер */
  int bx = cl.right - bw - 2;        /* кнопка у самого края */
  int textRight = bx - numW - 2;     /* дальше текста рамки не заходят */
  HPEN pen = CreatePen(PS_SOLID, 1, blend_rgb(COL_SAGE, COL_PAPER, 205));
  HPEN glyph = CreatePen(PS_SOLID, 1, blend_rgb(COL_SAGE, COL_PAPER, 110));
  HPEN glyphOk = CreatePen(PS_SOLID, 2, RGB(0x1F, 0x9D, 0x55));
  HGDIOBJ prevPen = SelectObject(dc, pen);
  HGDIOBJ prevBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  int prevBk = SetBkMode(dc, TRANSPARENT);
  COLORREF prevCol = SetTextColor(dc, blend_rgb(COL_SAGE, COL_PAPER, 140));

  int slot = 0, i = 0;
  while (i < len) {
    int start = i;
    while (i < len && buf[i] != L'\n' && buf[i] != L'\r') i++;
    int end = i;
    if (i < len && buf[i] == L'\r') i++;
    if (i < len && buf[i] == L'\n') i++;
    if (end == start) continue; /* пустые строки Ctrl+N пропускает */
    slot++;
    LRESULT p1 = SendMessageW(ed, EM_POSFROMCHAR, (WPARAM)start, 0);
    LRESULT p2 = SendMessageW(ed, EM_POSFROMCHAR, (WPARAM)(end - 1), 0);
    if (p1 == -1 || p2 == -1) continue; /* строка прокручена за край */
    int x1 = (short)LOWORD(p1), y1 = (short)HIWORD(p1);
    int y2 = (short)HIWORD(p2);
    if (y1 > cl.bottom) break; /* дальше — ниже края поля */
    /* только целиком видимые: обрезанная снизу рамка налезает на кнопки */
    if (y1 < 0 || y1 + lh > cl.bottom) continue;
    if (chertezh_on() && y2 + lh <= cl.bottom) { /* «Чертёж»: черта под строкой, номер — слева, в поле окна */
      HPEN rule = CreatePen(PS_SOLID, 1, blend_rgb(COL_LINE, COL_PAPER, 40));
      HGDIOBJ opr = SelectObject(dc, rule);
      MoveToEx(dc, 0, y2 + lh - 1, NULL);
      LineTo(dc, cl.right, y2 + lh - 1);
      SelectObject(dc, opr);
      DeleteObject(rule);
    }
    if (!chertezh_on() && slot <= 9 && g_hkName[HK_SNIP][0] && y2 + lh + 2 <= cl.bottom) { /* номер для Ctrl+1…9 (рамок с .41 нет — вид «A») */
      (void)x1;
      (void)textRight;
      wchar_t d[4];
      _snwprintf(d, 4, L"%d", slot);
      RECT nr = {bx - numW - 2, y1 - 1, bx - 3, y1 + lh};
      DrawTextW(dc, d, -1, &nr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    /* кнопка «копировать»: два листка; скопировала — зелёная галочка */
    if (g_noteBtnN < 96) {
      int k = g_noteBtnN++;
      RECT br = {bx, y1, bx + bw, y1 + lh};
      g_noteBtn[k].r = br;
      g_noteBtn[k].start = start;
      g_noteBtn[k].end = end;
      /* подложка кнопки — всегда видна, над мышью темнее, скопировала — зелёная */
      COLORREF bgc = k == g_noteFlash ? RGB(0xDC, 0xF5, 0xE3)
                     : k == g_noteHot ? blend_rgb(COL_SAGE, COL_PAPER, 215)
                                      : blend_rgb(COL_SAGE, COL_PAPER, 238);
      HBRUSH hb = CreateSolidBrush(bgc);
      HGDIOBJ ob = SelectObject(dc, hb);
      HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
      if (chertezh_on()) Rectangle(dc, br.left, br.top, br.right + 1, br.bottom + 1); /* «Чертёж» — прямые углы */
      else RoundRect(dc, br.left, br.top, br.right + 1, br.bottom + 1, 6, 6);
      SelectObject(dc, op);
      SelectObject(dc, ob);
      DeleteObject(hb);
      int gs = lh - 6; /* рисунок: квадрат внутри кнопки */
      if (gs < 8) gs = 8;
      int gx = br.left + (bw - gs) / 2, gy = br.top + (lh - gs) / 2;
      if (k == g_noteFlash) { /* галочка */
        SelectObject(dc, glyphOk);
        MoveToEx(dc, gx + gs / 6, gy + gs / 2, NULL);
        LineTo(dc, gx + gs * 2 / 5, gy + gs * 3 / 4);
        LineTo(dc, gx + gs * 5 / 6, gy + gs / 5);
      } else { /* два листка: задний справа сверху, передний слева снизу */
        int sw = gs * 2 / 3;
        SelectObject(dc, glyph);
        HBRUSH pb = CreateSolidBrush(bgc);
        HGDIOBJ ob2 = SelectObject(dc, pb);
        Rectangle(dc, gx + gs - sw, gy, gx + gs, gy + sw);
        Rectangle(dc, gx, gy + gs - sw, gx + sw, gy + gs);
        SelectObject(dc, ob2);
        DeleteObject(pb);
      }
    }
  }

  SetTextColor(dc, prevCol);
  SetBkMode(dc, prevBk);
  SelectObject(dc, prevBrush);
  SelectObject(dc, prevPen);
  DeleteObject(pen);
  DeleteObject(glyph);
  DeleteObject(glyphOk);
  if (prevFont) SelectObject(dc, prevFont);
  ReleaseDC(ed, dc);
  free(buf);
}

static int note_btn_at(int x, int y) {
  POINT p = {x, y};
  for (int k = 0; k < g_noteBtnN; k++)
    if (PtInRect(&g_noteBtn[k].r, p)) return k;
  return -1;
}

/* щелчок по кнопке у строки: строка — в буфер (картинка — картинкой) */
static void note_copy_btn(HWND ed, int k) {
  int len = GetWindowTextLengthW(ed);
  NoteCopyBtn b = g_noteBtn[k];
  if (b.start < 0 || b.end > len || b.end <= b.start) return;
  wchar_t *buf = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
  if (!buf) return;
  GetWindowTextW(ed, buf, len + 1);
  buf[b.end] = 0;
  const wchar_t *line = buf + b.start;
  int img = parse_img_id(line);
  BOOL ok = img ? clipboard_set_image(img) : clipboard_set(line);
  if (ok) {
    wchar_t msg[160];
    if (img) lstrcpynW(msg, L"Картинка скопирована", 160);
    else _snwprintf(msg, 160, L"Скопировано: %.60s", line);
    msg[159] = 0;
    show_status(msg);
    g_noteFlash = k;
    SetTimer(ed, TIMER_NOTE_FLASH, 900, NULL);
    InvalidateRect(ed, NULL, FALSE);
  } else {
    show_status(L"Буфер занят другой программой");
  }
  free(buf);
}

static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_PASTE) {
    if (try_paste_image()) return 0;
  }
  if (msg == WM_KEYDOWN && wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
    if (try_paste_image()) return 0;
  }
  if (msg == WM_MOUSEWHEEL || msg == WM_VSCROLL) {
    LRESULT r = CallWindowProcW(g_oldEdit, hwnd, msg, wParam, lParam);
    InvalidateRect(g_hwnd, NULL, FALSE);
    InvalidateRect(hwnd, NULL, TRUE); /* рамки уехали вместе с текстом */
    return r;
  }
  if (msg == WM_PAINT) {
    LRESULT r = CallWindowProcW(g_oldEdit, hwnd, msg, wParam, lParam);
    draw_slot_marks(hwnd);
    return r;
  }
  /* кнопки «копировать» у строк: поле их не знает — ловим мышь раньше него */
  if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
    int k = note_btn_at(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    if (k >= 0) {
      if (msg == WM_LBUTTONDOWN) note_copy_btn(hwnd, k);
      return 0; /* курсор в тексте не прыгает */
    }
  }
  if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(hwnd, &p);
    if (note_btn_at(p.x, p.y) >= 0) {
      SetCursor(LoadCursorW(NULL, IDC_HAND));
      return TRUE;
    }
  }
  if (msg == WM_MOUSEMOVE) {
    int k = note_btn_at(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    if (k != g_noteHot) {
      g_noteHot = k;
      InvalidateRect(hwnd, NULL, FALSE);
      TRACKMOUSEEVENT te = {sizeof(te), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&te);
    }
  }
  if (msg == WM_MOUSELEAVE && g_noteHot >= 0) {
    g_noteHot = -1;
    InvalidateRect(hwnd, NULL, FALSE);
  }
  if (msg == WM_TIMER && wParam == TIMER_NOTE_FLASH) {
    KillTimer(hwnd, TIMER_NOTE_FLASH);
    g_noteFlash = -1;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }
  if (msg == WM_SETFONT) { /* шрифт сбрасывает поля — вернуть место под кнопки */
    LRESULT r = CallWindowProcW(g_oldEdit, hwnd, msg, wParam, lParam);
    note_margins();
    return r;
  }
  return CallWindowProcW(g_oldEdit, hwnd, msg, wParam, lParam);
}

static void qh_show(void);
static void qh_fav_edit(void);

static LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    search_clipboard_or_edit();
    return 0;
  }
  /* Ctrl+H — история поиска, Ctrl+D — избранное (search_history.c); коды 8 и 4 от
     этих сочетаний в поле не пишем, иначе Ctrl+H сотрёт символ */
  if (msg == WM_KEYDOWN && (wParam == L'H' || wParam == L'D') && (GetKeyState(VK_CONTROL) & 0x8000)) {
    if (wParam == L'H') qh_show();
    else qh_fav_edit();
    return 0;
  }
  if (msg == WM_CHAR && (wParam == 8 || wParam == 4) && (GetKeyState(VK_CONTROL) & 0x8000)) return 0;
  return CallWindowProcW(g_oldSearch ? g_oldSearch : DefWindowProcW, hwnd, msg, wParam, lParam);
}

/* Spelled out rather than linked from libuuid: pulling that in for two
   values cost 400 KB of exe, which is a lot for one button. */
static const GUID kCLSID_FileOpenDialog = {0xdc1c5a9c, 0xe88a, 0x4dde,
                                           {0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7}};
static const GUID kIID_FileOpenDialog = {0xd57c7288, 0xd4ad, 0x4768,
                                         {0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60}};

/* Typing a network path by hand is the easiest thing in the program to get
   wrong, and a typo just yields an empty index with no explanation. */
static const wchar_t *g_pickFolderTitle; /* заголовок окна выбора; NULL — «для поиска файлов» */
static BOOL pick_folder(HWND owner, wchar_t *out, int cap) {
  out[0] = 0;
  const wchar_t *title = g_pickFolderTitle ? g_pickFolderTitle : L"Папка для поиска файлов";
  g_pickFolderTitle = NULL;
  HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  BOOL ok = FALSE;
  IFileOpenDialog *dlg = NULL;
  if (SUCCEEDED(CoCreateInstance(&kCLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                 &kIID_FileOpenDialog, (void **)&dlg))) {
    DWORD opts = 0;
    dlg->lpVtbl->GetOptions(dlg, &opts);
    dlg->lpVtbl->SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                     FOS_PATHMUSTEXIST);
    dlg->lpVtbl->SetTitle(dlg, title);
    if (SUCCEEDED(dlg->lpVtbl->Show(dlg, owner))) {
      IShellItem *item = NULL;
      if (SUCCEEDED(dlg->lpVtbl->GetResult(dlg, &item)) && item) {
        PWSTR path = NULL;
        if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path) {
          lstrcpynW(out, path, cap);
          CoTaskMemFree(path);
          ok = out[0] != 0;
        }
        item->lpVtbl->Release(item);
      }
    }
    dlg->lpVtbl->Release(dlg);
  } else {
    /* pre-Vista shells, and anything that refuses the modern dialog */
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (id) {
      if (SHGetPathFromIDListW(id, out)) ok = out[0] != 0;
      CoTaskMemFree(id);
    }
  }
  if (init == S_OK || init == S_FALSE) CoUninitialize();
  return ok;
}

/* Строка «следить за папкой» имеет смысл, только пока включены уведомления. */
static void watch_enable(void) {
  if (g_filesWatchEdit) EnableWindow(g_filesWatchEdit, g_filesNotify);
  HWND br = g_setHwnd ? GetDlgItem(g_setHwnd, ID_WATCH_BROWSE) : NULL;
  if (br) EnableWindow(br, g_filesNotify);
}

/* Папка для полного обхода по датам и уведомлений. Пусто — вся папка архива. */
static void files_apply_watch(void) {
  if (!g_filesWatchEdit) return;
  wchar_t w[MAX_PATH];
  GetWindowTextW(g_filesWatchEdit, w, MAX_PATH);
  wchar_t *p = w;
  while (*p == L' ') p++;
  size_t n = wcslen(p);
  while (n > 0 && (p[n - 1] == L' ' || p[n - 1] == L'\\')) p[--n] = 0;
  if (p[0] && !path_under(p, g_filesRoot)) {
    SetWindowTextW(g_filesWatchEdit, g_filesWatch);
    show_status(L"Нужна папка внутри архива");
    return;
  }
  if (wcscmp(p, g_filesWatch) == 0) return;
  lstrcpynW(g_filesWatch, p, MAX_PATH);
  SetWindowTextW(g_filesWatchEdit, g_filesWatch);
  save_files_pref();
  show_status(g_filesWatch[0] ? L"Слежу за выбранной папкой" : L"Слежу за всей папкой архива");
}

/* Подсказка в пустом поле кодов обозначений: какие взялись сами по архиву (после нового индекса — заново) */
static void des_codes_cue(void) {
  static int s_at = -2;
  if (!g_desCodesEdit || s_at == g_filesN) return;
  s_at = g_filesN;
  wchar_t c[SR_CODES][8], t[96];
  int n = files_des_codes_auto(c, SR_CODES);
  int l = _snwprintf(t, 96, n ? L"сами по архиву: " : L"сами по архиву — пока не нашлись");
  for (int i = 0; i < n && l > 0 && l < 80; i++) l += _snwprintf(t + l, 96 - l, i ? L", %s" : L"%s", c[i]);
  t[95] = 0;
  SendMessageW(g_desCodesEdit, 0x1501, TRUE, (LPARAM)t);
}

/* Коды обозначений из поля: как разобрались, через запятую; пусто — снова сами по архиву */
static void des_codes_apply(void) {
  if (!g_desCodesEdit) return;
  wchar_t w[64], c[SR_CODES][8];
  GetWindowTextW(g_desCodesEdit, w, 64);
  int n = sr_codes_parse(w, c, SR_CODES), l = 0;
  w[0] = 0;
  for (int i = 0; i < n; i++) l += _snwprintf(w + l, 64 - l, i ? L", %s" : L"%s", c[i]);
  SetWindowTextW(g_desCodesEdit, w);
  if (!wcscmp(w, g_desCodesUser)) return;
  lstrcpynW(g_desCodesUser, w, 64);
  save_files_pref();
  show_status(w[0] ? L"Коды обозначений — вписанные" : L"Коды обозначений — сами по архиву");
}

static void share_apply_root(void) {
  if (!g_shareEdit) return;
  wchar_t w[MAX_PATH];
  GetWindowTextW(g_shareEdit, w, MAX_PATH);
  wchar_t *p = w;
  while (*p == L' ') p++;
  wchar_t old[MAX_PATH];
  share_root_copy(old);
  share_root_set(p);
  wchar_t now[MAX_PATH];
  share_root_copy(now);
  if (wcscmp(old, now) == 0) return;
  SetWindowTextW(g_shareEdit, now);
  if (!now[0] && g_shareServe) {
    g_shareServe = FALSE;
    if (g_chkServe) SendMessageW(g_chkServe, BM_SETCHECK, BST_UNCHECKED, 0);
  }
  save_plm_pref();
  if (!now[0]) show_status(L"Общая папка убрана");
  else if (!g_sqlUser[0] || !g_sqlPass[0]) show_status(L"PLM буду спрашивать у коллег");
  else show_status(L"Общая папка задана");
}

/* Настройки прокручиваются: всё расставляется в полной высоте g_setContentH, а окно
   показывает часть. Шапка и подвал не двигаются; то, что уехало под шапку или
   под подвал, обрезается регионом, а подписи не рисуются вне полосы. */
static int g_setScroll; /* сколько точек содержимого уехало вверх */
#define MOVE_SET(h, x, yy, w, hh, rp) MoveWindow((h), (x), (yy) - g_setScroll, (w), (hh), (rp))

static void clip_settings_children(void) {
  RECT rc;
  GetClientRect(g_setHwnd, &rc);
  int top = PANEL_TITLE_H, bot = rc.bottom - SET_FOOT_H;
  for (HWND h = GetWindow(g_setHwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
    if (GetDlgCtrlID(h) == ID_PANEL_CLOSE) continue;
    RECT wr;
    GetWindowRect(h, &wr);
    MapWindowPoints(HWND_DESKTOP, g_setHwnd, (POINT *)&wr, 2);
    /* то, что не помещается в полосе между шапкой и подвалом, прячем целиком:
       частично видимый элемент наезжал бы на шапку или подвал */
    BOOL inBand = wr.top >= top && wr.bottom <= bot;
    BOOL hidden = GetPropW(h, L"CpScrollHide") != NULL;
    BOOL shown = (GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE) != 0;
    if (inBand && hidden) {
      RemovePropW(h, L"CpScrollHide");
      ShowWindow(h, SW_SHOW);
    } else if (!inBand && !hidden && shown) {
      SetPropW(h, L"CpScrollHide", (HANDLE)1);
      ShowWindow(h, SW_HIDE);
    }
  }
}

static void draw_set_label(HDC hdc, const wchar_t *text, RECT r, int clientBottom) {
  if (r.top < PANEL_TITLE_H || r.bottom > clientBottom - SET_FOOT_H) return;
  DrawTextW(hdc, text, -1, &r, DT_LEFT | DT_SINGLELINE);
}

/* Настройки — разделами (с 2026.10.14.4 вкладками, с 2026.10.21 столбиком
   слева, как «Параметры» Windows 11): видна одна страница. Порядок разделов
   можно менять правым щелчком по разделу, он запоминается (g_tabOrder), как и
   открытый раздел. Элементы других разделов уезжают далеко за край — их
   прячет clip_settings_children, как и всё, что ушло под шапку при прокрутке. */
static const wchar_t *const kSecNames[SEC_COUNT] = {L"Курсор", L"Эффект", L"Кастомизация", L"Поиск", L"Ещё", L"ИИ", L"Нормирование"};
#define SET_SEC_H 28
static int g_setContentH = 480; /* высота содержимого: по открытой вкладке (settings_fit_height подгоняет окно) */
static BOOL g_layOpen;          /* раскладывается открытая вкладка */

static void set_place(HWND h, int x, int y, int w, int hh) {
  if (!h) return;
  if (g_layOpen) MOVE_SET(h, x, y, w, hh, TRUE);
  else MoveWindow(h, x, -4000, w, hh, FALSE);
}

/* кнопки сеткой по cols в ряд; последний ряд растягивается на всю ширину.
   Возвращает, сколько места по высоте заняли */
static int set_grid(HWND *b, int n, int cols, int x, int y, int w, int bh, int gap) {
  int rows = (n + cols - 1) / cols;
  for (int i = 0; i < n; i++) {
    int row = i / cols, col = i % cols;
    int inRow = row == rows - 1 ? n - row * cols : cols;
    int bw = (w - gap * (inRow - 1)) / inRow;
    int bx = x + col * (bw + gap);
    if (col == inRow - 1) bw = x + w - bx; /* без щели справа от округления */
    set_place(b[i], bx, y + row * (bh + gap), bw, bh);
  }
  return rows * (bh + gap);
}

#include "ai_ui.c" /* вкладка «ИИ» */
static void nl_create(HWND p); /* вкладка «Нормирование» — norms_set.c, после norms.c */
static void nl_layout(int btnH);
static void nl_paint(HDC hdc, RECT rc);
static BOOL nl_command(HWND hwnd, int id, int code);
static void nl_load(void);

/* ---- Настройки как «Параметры» Windows 11 (с 2026.10.21) ------------------
   Слева — разделы со значками (порядок — правым щелчком, как раньше), справа —
   страница раздела: заголовок, над каждой карточкой — название группы; в
   карточке — строки: название и серое пояснение слева, элемент справа, а ряд
   вариантов — под ними; между строками — тонкие линии. Элементы те же, что
   были (те же ID и порядок обхода клавишей Tab), меняется только раскладка.
   Подписи, карточки и линии запоминаются при раскладке (g_setDeco) и
   рисуются в WM_PAINT; прокручивается только страница, разделы стоят. */
#define SET_RAIL_W 172 /* панель разделов */
#define SET_NAV_H 36   /* строка раздела */
#define SET_PAGE_PAD 20
#define SET_CARD_PAD 14
enum { SD_PAGE, SD_GROUP, SD_TITLE, SD_DESC, SD_CARD, SD_SEP };
typedef struct {
  RECT r; /* в координатах содержимого (до прокрутки) */
  int kind;
  const wchar_t *text;
} SetDeco;
#define SET_DECO_N 256 /* «Нормирование» со списком операций — около 140 */
static SetDeco g_setDeco[SET_DECO_N];
static int g_setDecoN;
static int g_sy;              /* куда класть дальше по высоте */
static int g_sx0, g_sx1;      /* края карточек */
static int g_cardTop, g_cardRows;
static HDC g_setMeasure;      /* мерить подписи при раскладке */
static wchar_t g_setVerDesc[64];

static void sd_add(int kind, int l, int t, int r, int b, const wchar_t *text) {
  if (!g_layOpen || g_setDecoN >= SET_DECO_N) return;
  SetDeco *d = &g_setDeco[g_setDecoN++];
  SetRect(&d->r, l, t, r, b);
  d->kind = kind;
  d->text = text;
}

/* высота текста в ширину w (с переносом) */
static int sd_text_h(HFONT f, const wchar_t *t, int w) {
  if (!t || !t[0] || !g_setMeasure) return 0;
  HGDIOBJ of = f ? SelectObject(g_setMeasure, f) : NULL;
  RECT r = {0, 0, w > 20 ? w : 20, 0};
  DrawTextW(g_setMeasure, t, -1, &r, DT_LEFT | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
  if (of) SelectObject(g_setMeasure, of);
  return r.bottom;
}

static void sd_page(const wchar_t *title) {
  int h = sd_text_h(g_fontDisplay, title, g_sx1 - g_sx0) + 4;
  sd_add(SD_PAGE, g_sx0, g_sy, g_sx1, g_sy + h, title);
  g_sy += h + 12;
}

static void sd_group(const wchar_t *head) {
  int h = sd_text_h(g_fontUi, head, g_sx1 - g_sx0);
  sd_add(SD_GROUP, g_sx0 + 2, g_sy, g_sx1, g_sy + h, head);
  g_sy += h + 6;
}

static void sd_card_begin(void) {
  g_cardTop = g_sy;
  g_cardRows = 0;
}

static void sd_card_end(void) {
  sd_add(SD_CARD, g_sx0, g_cardTop, g_sx1, g_sy, NULL);
  g_sy += 20;
}

/* новая строка карточки: между строками — тонкая линия */
static void sd_row_start(void) {
  if (g_cardRows++) sd_add(SD_SEP, g_sx0 + 1, g_sy, g_sx1 - 1, g_sy + 1, NULL);
}

/* название и пояснение во всю ширину; возвращает, где ставить элементы */
static int sd_stack(const wchar_t *title, const wchar_t *desc) {
  sd_row_start();
  int x0 = g_sx0 + SET_CARD_PAD, x1 = g_sx1 - SET_CARD_PAD, y = g_sy + 12;
  if (title) {
    int h = sd_text_h(g_fontBody, title, x1 - x0);
    sd_add(SD_TITLE, x0, y, x1, y + h, title);
    y += h;
  }
  if (desc) {
    int h = sd_text_h(g_fontSmall, desc, x1 - x0);
    sd_add(SD_DESC, x0, y + 1, x1, y + 1 + h, desc);
    y += h + 1;
  }
  return y + ((title || desc) ? 8 : 0);
}
static void sd_stack_end(int y) { g_sy = y + 12; }

/* строка: название и пояснение слева, n элементов справа шириной w[i] */
static void sd_row(const wchar_t *title, const wchar_t *desc, HWND *ctl, const int *w, int n, int ctlH) {
  sd_row_start();
  int x0 = g_sx0 + SET_CARD_PAD, x1 = g_sx1 - SET_CARD_PAD, cw = 0;
  for (int i = 0; i < n; i++) cw += w[i] + (i ? 8 : 0);
  int tw = x1 - x0 - cw - 16;
  int th = sd_text_h(g_fontBody, title, tw), dh = sd_text_h(g_fontSmall, desc, tw);
  int textH = th + (dh ? dh + 1 : 0);
  int rowH = (textH > ctlH ? textH : ctlH) + 24;
  int ty = g_sy + (rowH - textH) / 2;
  if (title) sd_add(SD_TITLE, x0, ty, x0 + tw, ty + th, title);
  if (desc) sd_add(SD_DESC, x0, ty + th + 1, x0 + tw, ty + th + 1 + dh, desc);
  int cx = x1 - cw, cy = g_sy + (rowH - ctlH) / 2;
  for (int i = 0; i < n; i++) {
    set_place(ctl[i], cx, cy, w[i], ctlH);
    cx += w[i] + 8;
  }
  g_sy += rowH;
}

/* строка из одного элемента во всю ширину (галочка, длинная кнопка) */
static void sd_full(HWND ctl, int h) {
  sd_row_start();
  set_place(ctl, g_sx0 + SET_CARD_PAD, g_sy + 8, g_sx1 - g_sx0 - 2 * SET_CARD_PAD, h);
  g_sy += h + 16;
}

static void layout_settings(void) {
  if (!g_setHwnd) return;
  RECT rc;
  GetClientRect(g_setHwnd, &rc);
  int maxScroll = g_setContentH - rc.bottom;
  if (maxScroll < 0) maxScroll = 0;
  if (g_setScroll > maxScroll) g_setScroll = maxScroll;
  if (g_setScroll < 0) g_setScroll = 0;
  int gap = 8, btnH = 30, lab = 22, browseW = 92;
  int yEnd = 0;
  place_panel_close(g_setHwnd);
  { /* разделы — столбиком слева, в выбранном порядке; не прокручиваются */
    for (int pos = 0; pos < SEC_COUNT; pos++) {
      HWND b = g_btnSec[g_tabOrder[pos]];
      if (b) MoveWindow(b, 8, PANEL_TITLE_H + 10 + pos * (SET_NAV_H + 2), SET_RAIL_W - 16, SET_NAV_H, TRUE);
    }
  }
  g_sx0 = SET_RAIL_W + 4;
  g_sx1 = rc.right - SET_PAGE_PAD;
  if (g_sx1 < g_sx0 + 240) g_sx1 = g_sx0 + 240;
  int x0 = g_sx0 + SET_CARD_PAD, x1 = g_sx1 - SET_CARD_PAD, iw = x1 - x0;
  g_setDecoN = 0;
  g_setMeasure = GetDC(g_setHwnd);
  for (int sec = 0; sec < SEC_COUNT; sec++) {
    g_layOpen = sec == g_tab;
    g_sy = PANEL_TITLE_H + 16;
    sd_page(kSecNames[sec]);
    switch (sec) {
    case 0: /* курсоры с просмотром */
    case 1: { /* эффекты с тем же просмотром */
      sd_card_begin();
      int y = sd_stack(NULL, NULL);
      if (g_layOpen) set_place(g_preview, x0, y, iw, 150);
      sd_stack_end(y + 150);
      sd_card_end();
      sd_card_begin();
      if (sec == 0) {
        HWND sk[9 + ST_COUNT] = {g_btnKit, g_btnK2, g_btnK3, g_btnK4, g_btnK5, g_btnK6, g_btnK7, g_btnK8};
        for (int i = 0; i < ST_COUNT; i++) sk[8 + i] = g_btnSt[i];
        sk[8 + ST_COUNT] = g_btnSys;
        y = sd_stack(L"Набор", L"«Современный» настраивается ниже; «Курсор Windows» — обычные стрелки");
        y += set_grid(sk, 9 + ST_COUNT, 3, x0, y, iw, btnH, gap) - gap;
      } else {
        y = sd_stack(L"Эффект при нажатии", L"Любой курсор можно сочетать с любым эффектом");
        y += set_grid(g_btnFx, FX_OPTIONS, 3, x0, y, iw, btnH, gap) - gap;
      }
      sd_stack_end(y);
      sd_card_end();
      if (sec == 0) { /* «Современный»: стиль, размер и переключатели — здесь, рядом с остальными наборами */
        sd_group(L"Современный");
        sd_card_begin();
        {
          int ph = kit_preview_h();
          y = sd_stack(NULL, NULL);
          set_place(g_kitPreview, x0, y, iw, ph);
          sd_stack_end(y + ph);
        }
        y = sd_stack(L"Стиль курсора", L"Новый набор, нарисован кодом: чёткий на любом размере, виден на светлом и тёмном");
        y += set_grid(g_btnKitSt, KIT_STYLES, 2, x0, y, iw, btnH, gap) - gap;
        sd_stack_end(y);
        y = sd_stack(L"Размер", NULL);
        y += set_grid(g_btnKitSz, KIT_SIZES, KIT_SIZES, x0, y, iw, btnH, gap) - gap;
        sd_stack_end(y);
        {
          HWND c[5] = {g_btnKitOut, g_btnKitShadow, g_btnKitAcc, g_btnKitAnim, g_btnKitFx};
          y = sd_stack(L"Эффекты", L"Двойная кайма, мягкая тень, значки цветом темы, крутится «Ожидание», своя «Волна» при нажатии");
          y += set_grid(c, 5, 3, x0, y, iw, btnH, gap) - gap;
          sd_stack_end(y);
        }
        sd_card_end();
      }
      break;
    }
    case 2: { /* тема, прозрачность, бой при бездействии */
      sd_group(L"Оформление");
      sd_card_begin();
      int y = sd_stack(L"Тема", L"Меняет шрифт, форму кнопок, шапки и углы окон");
      y += set_grid(g_btnTheme, THEME_COUNT, 3, x0, y, iw, btnH, gap) - gap;
      sd_stack_end(y);
      sd_card_end();
      sd_group(L"Панель Shift");
      sd_card_begin();
      y = sd_stack(L"Стиль кольца", L"Shift-перетаскивание файлов: стекло поверх экрана, точки по контуру, дуга «Ссылки»");
      y += set_grid(g_btnRing, RING_STYLES, 2, x0, y, iw, btnH, gap) - gap;
      sd_stack_end(y);
      {
        HWND c[3] = {g_btnRingBlur, g_btnRingDots, g_btnRingAnim};
        y = sd_stack(L"Эффекты", L"Размытие снимка экрана под плитками, точки по контуру, плавное появление и наведение");
        y += set_grid(c, 3, 3, x0, y, iw, btnH, gap) - gap;
        sd_stack_end(y);
      }
      sd_card_end();
      sd_group(L"Прозрачность окна");
      sd_card_begin();
      y = sd_stack(L"Пока окно следует за курсором", NULL);
      set_place(g_tbBg, x0 - 4, y - 4, iw + 8, btnH);
      sd_stack_end(y + btnH - 8);
      y = sd_stack(L"Когда окно закреплено", NULL);
      set_place(g_tbFg, x0 - 4, y - 4, iw + 8, btnH);
      sd_stack_end(y + btnH - 8);
      sd_card_end();
      sd_group(L"Бой Годжо и Сукуны");
      sd_card_begin();
      y = sd_stack(L"Начинать, если мышь не трогать", NULL);
      y += set_grid(g_btnIdle, IDLE_OPTIONS, IDLE_OPTIONS, x0, y, iw, btnH, gap) - gap;
      sd_stack_end(y);
      y = sd_stack(L"Размер бойцов", NULL);
      y += set_grid(g_btnIdleSz, IDLE_SIZES, IDLE_SIZES, x0, y, iw, btnH, gap) - gap;
      sd_stack_end(y);
      {
        HWND c[1] = {g_btnIdleShow};
        int w[1] = {176};
        sd_row(L"Показать бой", L"Сразу, не дожидаясь бездействия", c, w, 1, btnH);
      }
      y = sd_stack(L"Режим просмотра", L"Бой идёт и при работе мышью; щелчки проходят сквозь него");
      set_place(g_btnIdleWatch, x0, y, iw, btnH);
      sd_stack_end(y + btnH);
      sd_card_end();
      break;
    }
    case 3: { /* где искать файлы и вход в PLM */
      sd_group(L"Файлы");
      sd_card_begin();
      int y = sd_stack(L"Папка с файлами", L"Сетевая папка, где искать файлы по имени");
      set_place(g_filesRootEdit, x0, y, iw - browseW - gap, btnH);
      set_place(GetDlgItem(g_setHwnd, ID_FILES_BROWSE), x1 - browseW, y, browseW, btnH);
      sd_stack_end(y + btnH);
      y = sd_stack(L"Индекс файлов", NULL);
      set_place(g_filesStat, x0, y + 4, iw - 150 - gap, btnH - 6);
      set_place(g_filesBar, x0, y + btnH, iw - 150 - gap, 6);
      set_place(g_btnIdx, x1 - 150, y, 150, btnH);
      sd_stack_end(y + btnH + 6);
      sd_full(g_chkNotify, btnH);
      y = sd_stack(L"Следить за изменениями", L"Папка внутри архива; пусто — вся папка");
      set_place(g_filesWatchEdit, x0, y, iw - browseW - gap, btnH);
      set_place(GetDlgItem(g_setHwnd, ID_WATCH_BROWSE), x1 - browseW, y, browseW, btnH);
      sd_stack_end(y + btnH);
      y = sd_stack(L"Коды обозначений", L"Буквы перед номером своих чертежей; пусто — сами по архиву");
      set_place(g_desCodesEdit, x0, y, iw, btnH);
      sd_stack_end(y + btnH);
      sd_card_end();
      sd_group(L"PLM");
      sd_card_begin();
      y = sd_stack(L"Вход в базу PLM", L"Сервер SQL, база, логин и пароль");
      int half = (iw - gap) / 2;
      set_place(g_plmServer, x0, y, half, btnH);
      set_place(g_plmDb, x0 + half + gap, y, iw - half - gap, btnH);
      y += btnH + gap;
      set_place(g_plmUser, x0, y, half, btnH);
      set_place(g_plmPass, x0 + half + gap, y, iw - half - gap, btnH);
      sd_stack_end(y + btnH);
      sd_card_end();
      break;
    }
    case SEC_AI: { /* локальный ИИ: свои подписи над элементами (ai_ui_paint) */
      sd_group(L"Мини-ИИ");
      sd_card_begin();
      int y = sd_stack(NULL, NULL);
      y = ai_ui_layout(x0, y, iw, x1, btnH, gap, lab, browseW);
      sd_stack_end(y - gap);
      sd_card_end();
      break;
    }
    case SEC_NORMS: /* пределы режимов резания для норм (norms_set.c) */
      nl_layout(btnH);
      break;
    default: { /* общая папка (через неё идут и игры), распознавание, программа */
      sd_group(L"Общая папка отдела");
      sd_card_begin();
      int y = sd_stack(L"Общая папка", L"Через неё коллеги получают PLM и играют: нарды, шашки, морской бой, шахматы");
      set_place(g_shareEdit, x0, y, iw - browseW - gap, btnH);
      set_place(GetDlgItem(g_setHwnd, ID_SHARE_BROWSE), x1 - browseW, y, browseW, btnH);
      sd_stack_end(y + btnH);
      sd_full(g_chkServe, btnH);
      {
        /* четыре игры — рядом не помещаются с подписью: подпись сверху, кнопки во всю ширину, по две */
        HWND c[4] = {g_btnNd, g_btnSh, g_btnMb, g_btnSx};
        int gy = sd_stack(L"Игры с коллегами", L"Партия идёт через общую папку");
        sd_stack_end(gy + set_grid(c, 4, 2, x0, gy, iw, btnH, gap) - gap);
      }
      sd_card_end();
      sd_group(L"Распознавание текста");
      sd_card_begin();
      sd_full(g_chkTess, btnH);
      {
        HWND c[1] = {g_ocr};
        int w[1] = {196};
        sd_row(L"Прочитать текст с экрана", L"Выделите область — текст откроется в своём окне", c, w, 1, btnH);
      }
      sd_card_end();
      sd_group(L"Программа");
      sd_card_begin();
      {
        HWND c[1] = {g_btnHk};
        int w[1] = {178};
        sd_row(L"Клавиши", L"Назначить, сменить или убрать горячие клавиши", c, w, 1, btnH);
      }
      sd_full(g_chkAuto, btnH);
      sd_full(g_chkAnsPin, btnH);
      {
        _snwprintf(g_setVerDesc, 64, L"Сейчас — %s", APP_VERSION_STR);
        g_setVerDesc[63] = 0;
        HWND c[1] = {g_btnUp};
        int w[1] = {180};
        sd_row(L"Обновления", g_setVerDesc, c, w, 1, btnH);
      }
      sd_card_end();
      break;
    }
    }
    if (sec == g_tab) yEnd = g_sy; /* высота — по открытой вкладке (до 2026.10.26 — по самой длинной) */
  }
  if (g_setMeasure) ReleaseDC(g_setHwnd, g_setMeasure);
  g_setMeasure = NULL;
  g_layOpen = TRUE;
  if (g_tab > 1 && g_preview) MoveWindow(g_preview, x0, -4000, iw, 150, FALSE);
  int railH = PANEL_TITLE_H + 10 + SEC_COUNT * (SET_NAV_H + 2) + 12;
  if (yEnd < railH) yEnd = railH;
  int contentH = yEnd + SET_FOOT_H;
  if (contentH != g_setContentH) {
    g_setContentH = contentH;
    int newMax = contentH - rc.bottom;
    if (newMax < 0) newMax = 0;
    if (g_setScroll > newMax) { /* окно стало ниже — прокрутка сжимается, разложить заново */
      g_setScroll = newMax;
      layout_settings();
      return;
    }
  }
  clip_settings_children();
  preview_wake();
  kit_preview_wake();
}

/* подписи, карточки и линии страницы (раскладка — layout_settings) */
static void settings_paint_page(HDC hdc, RECT rc) {
  int save = SaveDC(hdc);
  IntersectClipRect(hdc, SET_RAIL_W, PANEL_TITLE_H, rc.right, rc.bottom - SET_FOOT_H);
  SetBkMode(hdc, TRANSPARENT);
  COLORREF hair = blend_rgb(COL_LINE, COL_INK, 24);
  for (int pass = 0; pass < 2; pass++) /* сначала карточки, потом всё, что на них */
    for (int i = 0; i < g_setDecoN; i++) {
      const SetDeco *d = &g_setDeco[i];
      RECT r = d->r;
      OffsetRect(&r, 0, -g_setScroll);
      if (r.bottom < PANEL_TITLE_H || r.top > rc.bottom - SET_FOOT_H) continue;
      if ((d->kind == SD_CARD) != (pass == 0)) continue;
      switch (d->kind) {
      case SD_CARD:
        if (knight_on()) {
          RECT f = r;
          SetBrushOrgEx(hdc, 0, 0, NULL);
          FillRect(hdc, &f, bg_brush(FALSE));
          frame_rect(hdc, f, KN_GOLD_DK, 1);
        } else {
          ui_round_fill(hdc, r, kThemes[g_theme].radius == 0 ? 0 : 8, COL_PAPER, hair);
        }
        break;
      case SD_SEP: {
        HBRUSH b = CreateSolidBrush(blend_rgb(COL_LINE, COL_PAPER, 60));
        FillRect(hdc, &r, b);
        DeleteObject(b);
        break;
      }
      case SD_PAGE:
      case SD_GROUP:
      case SD_TITLE:
      case SD_DESC: {
        HFONT f = d->kind == SD_PAGE ? g_fontDisplay : d->kind == SD_GROUP ? g_fontUi : d->kind == SD_TITLE ? g_fontBody : g_fontSmall;
        if (f) SelectObject(hdc, f);
        SetTextColor(hdc, d->kind == SD_DESC ? COL_MUTED : COL_INK);
        DrawTextW(hdc, d->text, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        break;
      }
      }
    }
  RestoreDC(hdc, save);
}

static void scroll_settings(int pos) {
  RECT rc;
  GetClientRect(g_setHwnd, &rc);
  int maxScroll = g_setContentH - rc.bottom;
  if (maxScroll < 0) maxScroll = 0;
  if (pos < 0) pos = 0;
  if (pos > maxScroll) pos = maxScroll;
  if (pos == g_setScroll) return;
  g_setScroll = pos;
  layout_settings();
  /* двигаются и прячутся дочерние окна — перерисовать всё вместе с ними, иначе
     на старых местах остаются их следы */
  RedrawWindow(g_setHwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

/* высота окна Настроек — по содержимому: раскрыли раздел — окно выросло (до
   высоты экрана, дальше прокрутка), свернули — сжалось. Верх остаётся на месте,
   пока окно не упрётся в низ экрана */
#define SET_H_DEF 680 /* предел высоты Настроек по умолчанию (при 100 %); выше — прокрутка */
static BOOL g_setSizing;   /* окно тянут за край — не подгонять под вкладку посреди движения */
static RECT g_setSizeFrom; /* каким было окно, когда начали тянуть */

/* С 2026.10.29 окно не растёт под длинную вкладку: высота по вкладке, но не выше
   выбранной (тянется за край, запоминается; по умолчанию SET_H_DEF), дальше —
   прокрутка колесом и полосой справа. Ширина — выбранная. Верх остаётся на месте,
   пока окно не упрётся в низ экрана */
static void settings_fit_height(void) {
  if (!g_setHwnd || !IsWindowVisible(g_setHwnd) || g_setSizing) return;
  RECT wr, cr, wa;
  GetWindowRect(g_setHwnd, &wr);
  GetClientRect(g_setHwnd, &cr);
  POINT pt = {wr.left + 8, wr.top + 8};
  get_work_area(pt, &wa);
  int frame = (wr.bottom - wr.top) - cr.bottom; /* рамка, за которую тянут */
  int cap = g_setUserH > 0 ? g_setUserH : MulDiv(SET_H_DEF, ui_dpi(g_setHwnd), 96);
  int h = g_setContentH + frame, maxh = wa.bottom - wa.top - 16;
  if (maxh < 240) maxh = 240;
  if (h > cap) h = cap;
  if (h > maxh) h = maxh;
  if (h < 240) h = 240;
  int w = g_setUserW > 0 ? g_setUserW : wr.right - wr.left, maxw = wa.right - wa.left - 16;
  if (w > maxw) w = maxw;
  int x = wr.left, y = wr.top;
  if (x + w > wa.right - 4) x = wa.right - w - 8;
  if (x < wa.left + 4) x = wa.left + 8;
  if (y + h > wa.bottom - 4) y = wa.bottom - h - 8;
  if (y < wa.top + 4) y = wa.top + 8;
  if (h == wr.bottom - wr.top && w == wr.right - wr.left && y == wr.top && x == wr.left) return;
  SetWindowPos(g_setHwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

/* полоса прокрутки справа: где ползунок; FALSE — прокручивать нечего */
static BOOL settings_thumb(RECT rc, RECT *th, int *bandH, int *thumbH, int *maxScroll) {
  int bandTop = PANEL_TITLE_H, bandBot = rc.bottom - SET_FOOT_H;
  if (g_setContentH <= rc.bottom || bandBot <= bandTop) return FALSE;
  *bandH = bandBot - bandTop;
  *maxScroll = g_setContentH - rc.bottom;
  *thumbH = *bandH * rc.bottom / g_setContentH;
  if (*thumbH < 28) *thumbH = 28;
  if (*thumbH > *bandH) *thumbH = *bandH;
  int y = bandTop + (*bandH - *thumbH) * g_setScroll / *maxScroll;
  SetRect(th, rc.right - 10, y, rc.right - 3, y + *thumbH);
  return TRUE;
}
static int g_setDragFrom = -1, g_setDragY; /* ползунок тянут: прокрутка и точка, где взяли */

static void settings_relayout(void) {
  for (int i = 0; i < SEC_COUNT; i++)
    if (g_btnSec[i]) InvalidateRect(g_btnSec[i], NULL, FALSE);
  layout_settings();
  settings_fit_height();
  layout_settings(); /* высота окна могла смениться — прокрутка вместе с ней */
  RedrawWindow(g_setHwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void select_tab(int sec) {
  g_tab = sec;
  g_setScroll = 0;
  save_cursor_pref();
  settings_relayout();
}

/* правый щелчок по разделу: сдвинуть его выше или ниже */
static void tab_menu(int sec, int x, int y) {
  int pos = 0;
  while (pos < SEC_COUNT && g_tabOrder[pos] != sec) pos++;
  if (pos >= SEC_COUNT) return;
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING | (pos == 0 ? MF_GRAYED : 0), 1, L"↑ Сдвинуть выше");
  AppendMenuW(m, MF_STRING | (pos == SEC_COUNT - 1 ? MF_GRAYED : 0), 2, L"↓ Сдвинуть ниже");
  AppendMenuW(m, MF_STRING | (pos == 0 ? MF_GRAYED : 0), 3, L"Сделать первым");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, 4, L"Порядок как был");
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, g_setHwnd, NULL);
  DestroyMenu(m);
  if (cmd == 1 || cmd == 2) {
    int to = cmd == 1 ? pos - 1 : pos + 1;
    g_tabOrder[pos] = g_tabOrder[to];
    g_tabOrder[to] = sec;
  } else if (cmd == 3) {
    memmove(g_tabOrder + 1, g_tabOrder, sizeof(int) * pos);
    g_tabOrder[0] = sec;
  } else if (cmd == 4) {
    for (int i = 0; i < SEC_COUNT; i++) g_tabOrder[i] = i;
  } else {
    return;
  }
  save_cursor_pref();
  settings_relayout();
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1; /* фон рисует WM_PAINT целиком */
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC pdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    /* в памяти и одним разом: карточки при прокрутке не мигают */
    HDC hdc = CreateCompatibleDC(pdc);
    HBITMAP pbm = hdc ? CreateCompatibleBitmap(pdc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1) : NULL;
    HGDIOBJ pold = NULL;
    if (hdc && pbm) {
      pold = SelectObject(hdc, pbm);
    } else {
      if (hdc) DeleteDC(hdc);
      if (pbm) DeleteObject(pbm);
      hdc = pdc;
    }
    FillRect(hdc, &rc, bg_brush(TRUE)); /* фон страницы и панели разделов */
    draw_panel_header(hwnd, hdc, L"Настройки");
    settings_paint_page(hdc, rc);
    SetBkMode(hdc, TRANSPARENT);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, COL_MUTED);
    ai_ui_paint(hdc, rc); /* подписи над элементами раздела «ИИ» */
    nl_paint(hdc, rc);    /* и столбцов раздела «Нормирование» */
    {
      RECT ver = {16, rc.bottom - 28, rc.right - 16, rc.bottom - 10};
      wchar_t vs[64];
      _snwprintf(vs, 64, L"%s", APP_VERSION_STR); /* «CursorPad» не влезал рядом с подсказкой */
      DrawTextW(hdc, vs, -1, &ver, DT_LEFT | DT_SINGLELINE);
      DrawTextW(hdc, L"правый щелчок по разделу — порядок разделов", -1, &ver, DT_RIGHT | DT_SINGLELINE);
    }
    {
      /* полоска прокрутки справа: видно, что страница выше окна */
      RECT th;
      int bandH, thumbH, maxScroll;
      if (settings_thumb(rc, &th, &bandH, &thumbH, &maxScroll)) { /* тянется мышью, щелчок мимо — на страницу */
        COLORREF c = blend_rgb(COL_MUTED, COL_PAPER_DARK, g_setDragFrom >= 0 ? 20 : 80);
        fill_round_rect(hdc, th, c, c, 3);
      }
    }
    if (hdc != pdc) {
      BitBlt(pdc, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
      SelectObject(hdc, pold);
      DeleteObject(pbm);
      DeleteDC(hdc);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLOREDIT: {
    /* поля — на светлой карточке, рамку рисует ui_theme.c */
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_CTLCOLORSTATIC: {
    /* галочки, ползунки и строки состояния стоят на карточках */
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_MUTED);
    /* на пергаменте галочки и ползунки не должны стоять ровными плашками */
    if (knight_on()) {
      SetBkMode(hdc, TRANSPARENT);
      /* узор пергамента считаем от угла родителя, а не самого элемента —
         иначе за ползунком виден сдвинутый прямоугольник */
      RECT cr;
      GetWindowRect((HWND)lParam, &cr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&cr, 2);
      SetBrushOrgEx(hdc, -cr.left, -cr.top, NULL);
      return (LRESULT)bg_brush(FALSE);
    }
    return (LRESULT)g_paper;
  }
  case WM_NOTIFY: { /* ползунки прозрачности рисуются в цветах темы (ui_theme.c) */
    LRESULT res = 0;
    if (ui_trackbar_draw((const NMHDR *)lParam, &res)) return res;
    break;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_CONTEXTMENU: {
    int id = GetDlgCtrlID((HWND)wParam);
    if (id >= ID_SEC_BASE && id < ID_SEC_BASE + SEC_COUNT) {
      POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);
      tab_menu(id - ID_SEC_BASE, pt.x, pt.y);
      return 0;
    }
    break;
  }
  case WM_MOUSEWHEEL:
    scroll_settings(g_setScroll - GET_WHEEL_DELTA_WPARAM(wParam) * 48 / WHEEL_DELTA);
    return 0;
  case WM_AI_UI:
    ai_ui_refresh();
    return 0;
  case WM_AI_TEST:
    ai_ui_test_done((AiResult *)lParam);
    return 0;
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_PANEL_CLOSE) {
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    }
    if (ai_ui_command(hwnd, LOWORD(wParam), HIWORD(wParam))) return 0;
    if (nl_command(hwnd, LOWORD(wParam), HIWORD(wParam))) return 0;
    if (LOWORD(wParam) == ID_FILES_BROWSE) {
      wchar_t picked[MAX_PATH];
      if (pick_folder(hwnd, picked, MAX_PATH)) {
        if (g_filesRootEdit) SetWindowTextW(g_filesRootEdit, picked);
        files_apply_root(TRUE);
      }
      return 0;
    }
    if (LOWORD(wParam) >= ID_SEC_BASE && LOWORD(wParam) < ID_SEC_BASE + SEC_COUNT) {
      select_tab(LOWORD(wParam) - ID_SEC_BASE);
      return 0;
    }
    if (settings_skin_of_id(LOWORD(wParam)) >= 0) set_skin(settings_skin_of_id(LOWORD(wParam)));
    if (LOWORD(wParam) >= ID_FX_BASE && LOWORD(wParam) < ID_FX_BASE + FX_OPTIONS)
      set_fx_pick(kFxPick[LOWORD(wParam) - ID_FX_BASE]);
    if (LOWORD(wParam) == ID_CHK_TESS && g_chkTess) {
      g_ocrWinOnly = SendMessageW(g_chkTess, BM_GETCHECK, 0, 0) != BST_CHECKED;
      ocr_engine_save();
      show_status(g_ocrWinOnly ? L"Распознаёт Windows OCR" : L"Распознаёт Tesseract (точнее)");
    }
    if (LOWORD(wParam) == ID_OCR) run_ocr_test();
    if (LOWORD(wParam) == ID_UPDATE) start_update();
    if (LOWORD(wParam) == ID_HOTKEYS && HIWORD(wParam) == BN_CLICKED) hk_show();
    if (LOWORD(wParam) >= ID_THEME_BASE && LOWORD(wParam) < ID_THEME_BASE + THEME_COUNT)
      set_theme(LOWORD(wParam) - ID_THEME_BASE);
    if (LOWORD(wParam) >= ID_RING_BASE && LOWORD(wParam) < ID_RING_BASE + RING_STYLES) {
      g_ringStyle = LOWORD(wParam) - ID_RING_BASE;
      save_cursor_pref();
      update_ring_buttons();
      wchar_t msg[96];
      _snwprintf(msg, 96, L"Панель Shift: %s. Зажмите Shift и потяните файл из Проводника", kRingNames[g_ringStyle]);
      show_status(msg);
    }
    if (LOWORD(wParam) == ID_RING_BLUR || LOWORD(wParam) == ID_RING_DOTS || LOWORD(wParam) == ID_RING_ANIM) {
      if (LOWORD(wParam) == ID_RING_BLUR) g_ringBlur = !g_ringBlur;
      else if (LOWORD(wParam) == ID_RING_DOTS) g_ringDots = !g_ringDots;
      else g_ringAnim = !g_ringAnim;
      save_cursor_pref();
      update_ring_buttons();
    }
    if (LOWORD(wParam) >= ID_KIT_BASE && LOWORD(wParam) < ID_KIT_BASE + KIT_STYLES) {
      /* стиль — сразу и набор: выбрать стиль и не поставить его было бы странно */
      g_kitStyle = LOWORD(wParam) - ID_KIT_BASE;
      if (g_skin != SKIN_KIT) set_skin(SKIN_KIT);
      kit_changed(FALSE);
      wchar_t msg[96];
      _snwprintf(msg, 96, L"Курсор: Современный · %s", kKitNames[g_kitStyle]);
      show_status(msg);
    }
    if (LOWORD(wParam) >= ID_KIT_SZ && LOWORD(wParam) < ID_KIT_SZ + KIT_SIZES) {
      g_kitSize = LOWORD(wParam) - ID_KIT_SZ;
      kit_changed(TRUE);
    }
    if (LOWORD(wParam) >= ID_KIT_OUT && LOWORD(wParam) <= ID_KIT_FX) {
      int id = LOWORD(wParam);
      if (id == ID_KIT_OUT) g_kitOutline = !g_kitOutline;
      else if (id == ID_KIT_SHADOW) g_kitShadow = !g_kitShadow;
      else if (id == ID_KIT_ACC) g_kitThemeAcc = !g_kitThemeAcc;
      else if (id == ID_KIT_ANIM) g_kitAnim = !g_kitAnim;
      else g_kitFx = !g_kitFx;
      kit_changed(FALSE);
      if (id == ID_KIT_FX && g_kitFx && g_fxPick >= 0) /* свой выбор во вкладке «Эффект» важнее */
        show_status(L"«Волна» будет, если во вкладке «Эффект» выбрать «Как у курсора»");
    }
    if (LOWORD(wParam) >= ID_IDLE_BASE && LOWORD(wParam) < ID_IDLE_BASE + IDLE_OPTIONS)
      set_idle_sec(kIdleSec[LOWORD(wParam) - ID_IDLE_BASE]);
    if (LOWORD(wParam) >= ID_IDLE_SZ && LOWORD(wParam) < ID_IDLE_SZ + IDLE_SIZES) {
      g_idleSize = LOWORD(wParam) - ID_IDLE_SZ;
      save_cursor_pref();
      update_idle_buttons();
      show_status(g_idleSize == 0 ? L"Бойцы мелкие" : (g_idleSize == 1 ? L"Бойцы средние" : L"Бойцы крупные"));
    }
    if (LOWORD(wParam) == ID_IDLE_WATCH) set_idle_watch(!g_idleWatch);
    if (LOWORD(wParam) == ID_IDLE_SHOW) {
      idle_fight_demo();
      /* сколько мониторов видит программа: если бой не уходит на другие, по этой строке понятно почему */
      wchar_t msg[128];
      _snwprintf(msg, 128, L"Бой идёт, пока не тронете мышь. Мониторов: %d, весь экран %d×%d",
                 GetSystemMetrics(SM_CMONITORS), GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
      show_status(msg);
    }
    if (LOWORD(wParam) == ID_ENG_PLM) {
      g_engine = 4;
      save_cursor_pref();
      update_engine_buttons();
    }
    if (LOWORD(wParam) == ID_ENG_FILES) {
      g_engine = 5;
      save_cursor_pref();
      files_apply_root(FALSE);
      update_engine_buttons();
    }
    if (LOWORD(wParam) == ID_FILES_REFRESH) {
      /* the same button stops a walk that is already running */
      if (InterlockedCompareExchange(&g_filesBusy, 0, 0)) files_stop_index();
      else files_apply_root(TRUE);
    }
    if (LOWORD(wParam) == ID_SHARE_BROWSE) {
      wchar_t picked[MAX_PATH];
      if (pick_folder(hwnd, picked, MAX_PATH)) {
        if (g_shareEdit) SetWindowTextW(g_shareEdit, picked);
        share_apply_root();
      }
      return 0;
    }
    if (LOWORD(wParam) == ID_SHARE_EDIT && HIWORD(wParam) == EN_KILLFOCUS) share_apply_root();
    if (LOWORD(wParam) == ID_NARDY_BTN) {
      nardy_show();
      return 0;
    }
    if (LOWORD(wParam) == ID_SHASHKI_BTN) {
      shashki_show();
      return 0;
    }
    if (LOWORD(wParam) == ID_MORSKOY_BTN) {
      morskoy_show();
      return 0;
    }
    if (LOWORD(wParam) == ID_SHAHMATY_BTN) {
      shahmaty_show();
      return 0;
    }
    if (LOWORD(wParam) == ID_SHARE_SERVE) {
      save_plm_pref(); /* логин могли вписать только что */
      BOOL want = SendMessageW(g_chkServe, BM_GETCHECK, 0, 0) == BST_CHECKED;
      if (want && (!g_sqlUser[0] || !g_sqlPass[0])) {
        want = FALSE;
        SendMessageW(g_chkServe, BM_SETCHECK, BST_UNCHECKED, 0);
        show_status(L"Сначала логин и пароль SQL");
      } else if (want && !g_shareRoot[0]) {
        want = FALSE;
        SendMessageW(g_chkServe, BM_SETCHECK, BST_UNCHECKED, 0);
        show_status(L"Сначала общая папка");
      } else {
        show_status(want ? L"Раздаю PLM коллегам" : L"PLM коллегам больше не раздаю");
      }
      g_shareServe = want;
      save_plm_pref();
    }
    if (LOWORD(wParam) == ID_WATCH_BROWSE) {
      wchar_t picked[MAX_PATH];
      if (pick_folder(hwnd, picked, MAX_PATH)) {
        if (g_filesWatchEdit) SetWindowTextW(g_filesWatchEdit, picked);
        files_apply_watch();
      }
      return 0;
    }
    if (LOWORD(wParam) == ID_WATCH_EDIT && HIWORD(wParam) == EN_KILLFOCUS) files_apply_watch();
    if (LOWORD(wParam) == ID_DES_CODES && HIWORD(wParam) == EN_KILLFOCUS) des_codes_apply();
    if (LOWORD(wParam) == ID_FILES_NOTIFY) {
      g_filesNotify = (SendMessageW(g_chkNotify, BM_GETCHECK, 0, 0) == BST_CHECKED);
      watch_enable();
      save_files_pref();
      show_status(g_filesNotify ? L"Буду сообщать о новых файлах в папке"
                                : L"Уведомления о файлах выключены");
    }
    if (LOWORD(wParam) == ID_ANSPIN) {
      g_ansKeepPos = (SendMessageW(g_chkAnsPin, BM_GETCHECK, 0, 0) == BST_CHECKED);
      save_cursor_pref();
      show_status(g_ansKeepPos ? L"Окно находок остаётся на месте"
                               : L"Окно находок у курсора");
    }
    if (LOWORD(wParam) == ID_AUTOSTART) {
      g_autostart = (SendMessageW(g_chkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);
      autostart_set(g_autostart);
      save_cursor_pref();
    }
    if (LOWORD(wParam) == ID_FILES_ROOT && HIWORD(wParam) == EN_KILLFOCUS)
      files_apply_root(FALSE);
    /* С 2026.09.19.11 эти поля никто не читал: сервер, логин и пароль
       брались из plm.txt, оставшегося от старой версии, а введённое
       в Настройках пропадало. На новом компьютере войти было нельзя вообще. */
    if ((LOWORD(wParam) == ID_PLM_SERVER || LOWORD(wParam) == ID_PLM_DB ||
         LOWORD(wParam) == ID_PLM_USER || LOWORD(wParam) == ID_PLM_PASS) &&
        HIWORD(wParam) == EN_KILLFOCUS)
      save_plm_pref();
    return 0;
  case WM_HSCROLL:
    if ((HWND)lParam == g_tbBg) {
      g_alphaFollow = (int)SendMessageW(g_tbBg, TBM_GETPOS, 0, 0);
      save_cursor_pref();
      apply_alpha_now();
    } else if ((HWND)lParam == g_tbFg) {
      g_alphaPinned = (int)SendMessageW(g_tbFg, TBM_GETPOS, 0, 0);
      save_cursor_pref();
      apply_alpha_now();
    }
    return 0;
  case WM_SIZE:
    layout_settings();
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_GETMINMAXINFO: {
    MINMAXINFO *mm = (MINMAXINFO *)lParam;
    mm->ptMinTrackSize.x = ui_px(hwnd, 460);
    mm->ptMinTrackSize.y = ui_px(hwnd, 300);
    return 0;
  }
  case WM_ENTERSIZEMOVE:
    g_setSizing = TRUE;
    GetWindowRect(hwnd, &g_setSizeFrom);
    return 0;
  case WM_EXITSIZEMOVE: { /* тянули за край — запомнить; просто переносили — ничего */
    g_setSizing = FALSE;
    RECT wr;
    GetWindowRect(hwnd, &wr);
    BOOL dw = wr.right - wr.left != g_setSizeFrom.right - g_setSizeFrom.left;
    BOOL dh = wr.bottom - wr.top != g_setSizeFrom.bottom - g_setSizeFrom.top;
    if (dw) g_setUserW = wr.right - wr.left;
    if (dh) g_setUserH = wr.bottom - wr.top;
    if (dw || dh) {
      save_cursor_pref();
      settings_relayout();
    }
    return 0;
  }
  case WM_LBUTTONDOWN: { /* полоса прокрутки: тянуть ползунок или листать щелчком мимо */
    RECT rc, th;
    int bandH, thumbH, maxScroll;
    GetClientRect(hwnd, &rc);
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (pt.x < rc.right - 16 || !settings_thumb(rc, &th, &bandH, &thumbH, &maxScroll)) break;
    if (pt.y >= th.top && pt.y < th.bottom) {
      g_setDragFrom = g_setScroll;
      g_setDragY = pt.y;
      SetCapture(hwnd);
      InvalidateRect(hwnd, &th, FALSE);
    } else if (pt.y >= PANEL_TITLE_H && pt.y < rc.bottom - SET_FOOT_H) {
      int page = rc.bottom - PANEL_TITLE_H - SET_FOOT_H - 40;
      scroll_settings(g_setScroll + (pt.y < th.top ? -page : page));
    }
    return 0;
  }
  case WM_MOUSEMOVE:
    if (g_setDragFrom >= 0 && GetCapture() == hwnd) {
      RECT rc, th;
      int bandH, thumbH, maxScroll;
      GetClientRect(hwnd, &rc);
      if (settings_thumb(rc, &th, &bandH, &thumbH, &maxScroll) && bandH > thumbH)
        scroll_settings(g_setDragFrom + (GET_Y_LPARAM(lParam) - g_setDragY) * maxScroll / (bandH - thumbH));
      return 0;
    }
    break;
  case WM_LBUTTONUP:
    if (g_setDragFrom >= 0) ReleaseCapture();
    return 0;
  case WM_CAPTURECHANGED:
    if (g_setDragFrom >= 0) {
      g_setDragFrom = -1;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void place_popup(HWND win, int wantW, int wantH, BOOL prefer_left) {
  if (!win || !g_hwnd) return;
  RECT rc, wa;
  GetWindowRect(g_hwnd, &rc);
  POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
  get_work_area(pt, &wa);
  UINT dpi = 96;
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32) {
    typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
    GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
    if (fn) dpi = fn(win);
  }
  int w = MulDiv(wantW, (int)dpi, 96);
  int h = MulDiv(wantH, (int)dpi, 96);
  int maxh = wa.bottom - wa.top - 16;
  if (maxh < 160) maxh = 160;
  if (h > maxh) h = maxh;
  int x = prefer_left ? rc.left - w - 8 : rc.right + 8;
  int y = rc.top;
  if (x < wa.left + 4 || x + w > wa.right - 4)
    x = prefer_left ? rc.right + 8 : rc.left - w - 8;
  if (x < wa.left + 4) x = wa.left + 8;
  if (x + w > wa.right - 4) x = wa.right - w - 8;
  if (y + h > wa.bottom - 4) y = wa.bottom - h - 8;
  if (y < wa.top + 4) y = wa.top + 8;
  SetWindowPos(win, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

static void place_settings(void) {
  if (!g_setHwnd) return;
  /* ширина — выбранная (в точках экрана, place_popup ждёт 96 dpi); высоту поправит settings_fit_height */
  int dpi = ui_dpi(g_setHwnd);
  place_popup(g_setHwnd, g_setUserW > 0 ? MulDiv(g_setUserW, 96, dpi) : SET_W, g_setContentH, FALSE);
  layout_settings();
  settings_fit_height();
}

static void refresh_search_cue(void) {
  if (!g_searchEdit) return;
  /* подсказка короче поля: «обозначение, название или вопрос» обрезалось на «во…» */
  const wchar_t *c = g_engineForce == 4   ? L"найти в PLM"
                     : g_engineForce == 5 ? L"имя файла"
                     : g_engineForce == 3 ? L"спросить мини-ИИ"
                                          : L"обозначение, имя, вопрос";
  SendMessageW(g_searchEdit, 0x1501, TRUE, (LPARAM)c);
}

static void layout_ask(void) {
  /* поле и кнопки поиска теперь в окне курсора — их расставляет layout_children */
  if (g_askHwnd) place_panel_close(g_askHwnd);
  layout_children();
}

static void place_ask(void) {
  if (!g_askHwnd) return;
  place_popup(g_askHwnd, ASK_W, ASK_H, TRUE);
  layout_ask();
}

static void toggle_settings(void) {
  if (!g_setHwnd) return;
  if (IsWindowVisible(g_setHwnd)) {
    ShowWindow(g_setHwnd, SW_HIDE);
    return;
  }
  SetLayeredWindowAttributes(g_setHwnd, 0, 255, LWA_ALPHA);
  place_settings();
}

static void sync_ask_btn(void) {
  g_askOpen = g_askHwnd && IsWindowVisible(g_askHwnd);
  if (g_btnAsk) InvalidateRect(g_btnAsk, NULL, FALSE);
  if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

static void toggle_ask(void);
/* «Спросить» и выбор, где искать — из окна курсора (режим «Поиск») */
static void ask_command(int id) {
  if (id == ID_SEARCH_GO) search_clipboard_or_edit();
  /* ярлык — «здесь, один раз»; без него место выбирается по запросу */
  if (id == ID_ENG_AI || id == ID_ENG_PLM || id == ID_ENG_FILES)
    engine_chip(id == ID_ENG_AI ? 3 : (id == ID_ENG_PLM ? 4 : 5));
}

static void set_pad_mode(int mode) {
  g_padMode = mode ? 1 : 0;
  save_cursor_pref();
  layout_children();
  if (g_btnAsk) InvalidateRect(g_btnAsk, NULL, FALSE);
  if (g_btnNotes) InvalidateRect(g_btnNotes, NULL, FALSE);
  if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
  if (!g_follow) SetFocus(g_padMode ? g_searchEdit : g_edit);
}

static LRESULT CALLBACK AskProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, bg_brush(TRUE));
    draw_panel_header(hwnd, hdc, L"Поиск");
    SetBkMode(hdc, TRANSPARENT);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, COL_MUTED);
    if (g_btnAi) {
      RECT wr;
      GetWindowRect(g_btnAi, &wr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&wr, 2);
      RECT a = {14, wr.top - 18, rc.right - 14, wr.top - 2};
      DrawTextW(hdc, L"где искать", -1, &a, DT_LEFT | DT_SINGLELINE);
    }
    {
      RECT hint = {14, rc.bottom - 26, rc.right - 14, rc.bottom - 8};
      wchar_t ht[96];
      if (g_hkName[HK_SEARCH][0]) _snwprintf(ht, 96, L"Enter — спросить · %s по буферу копии", g_hkName[HK_SEARCH]);
      else lstrcpynW(ht, L"Enter — спросить", 96);
      ht[95] = 0;
      DrawTextW(hdc, ht, -1, &hint, DT_LEFT | DT_SINGLELINE);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_CTLCOLORSTATIC: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER_DARK);
    SetTextColor(hdc, COL_MUTED);
    /* на пергаменте галочки и ползунки не должны стоять ровными плашками */
    if (knight_on()) {
      SetBkMode(hdc, TRANSPARENT);
      /* узор пергамента считаем от угла родителя, а не самого элемента —
         иначе за ползунком виден сдвинутый прямоугольник */
      RECT cr;
      GetWindowRect((HWND)lParam, &cr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&cr, 2);
      SetBrushOrgEx(hdc, -cr.left, -cr.top, NULL);
      return (LRESULT)bg_brush(TRUE);
    }
    return (LRESULT)g_paperDark;
  }
  case WM_NCHITTEST:
    return panel_hittest(hwnd, lParam);
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_PANEL_CLOSE) {
      toggle_ask();
      return 0;
    }
    ask_command(LOWORD(wParam));
    return 0;
  case WM_SIZE:
    layout_ask();
    return 0;
  case WM_CLOSE:
    ShowWindow(hwnd, SW_HIDE);
    sync_ask_btn();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void toggle_ask(void) {
  if (!g_askHwnd) return;
  if (IsWindowVisible(g_askHwnd)) {
    ShowWindow(g_askHwnd, SW_HIDE);
    sync_ask_btn();
    return;
  }
  SetLayeredWindowAttributes(g_askHwnd, 0, 255, LWA_ALPHA);
  place_ask();
  sync_ask_btn();
  if (!g_follow && g_searchEdit) SetFocus(g_searchEdit);
}

static void create_ask(HWND owner) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = AskProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = g_paperDark;
  wc.lpszClassName = L"CursorPadAsk";
  RegisterClassExW(&wc);
  g_askHwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"CursorPadAsk",
      L"Поиск", WS_POPUP | WS_CLIPCHILDREN, 0, 0,
      ASK_W, ASK_H, owner, NULL, g_inst, NULL);
  mk_btn(g_askHwnd, L"×", ID_PANEL_CLOSE);
  round_corners(g_askHwnd);
  /* поле и кнопки поиска живут в самом окне курсора (режим «Поиск»);
     отдельное окно «Поиск» осталось пустой оболочкой и не показывается */
  g_searchEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                 0, 0, 160, 26, owner, (HMENU)(INT_PTR)ID_SEARCH_EDIT, NULL, NULL);
  g_searchGo = mk_btn(owner, L"Найти", ID_SEARCH_GO);
  g_btnAi = mk_btn(owner, L"Мини-ИИ", ID_ENG_AI);
  g_btnPlm = mk_btn(owner, L"PLM", ID_ENG_PLM);
  g_btnFiles = mk_btn(owner, L"Файлы", ID_ENG_FILES);
  {
    HWND sc[4] = {g_searchGo, g_btnAi, g_btnPlm, g_btnFiles};
    for (int k = 0; k < 4; k++)
      if (sc[k]) ShowWindow(sc[k], SW_HIDE);
  }
  if (g_searchEdit) {
    SendMessageW(g_searchEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    g_oldSearch = (WNDPROC)SetWindowLongPtrW(g_searchEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
    refresh_search_cue();
  }
  if (g_searchGo) SendMessageW(g_searchGo, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_btnAi) SendMessageW(g_btnAi, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_btnPlm) SendMessageW(g_btnPlm, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_btnFiles) SendMessageW(g_btnFiles, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  update_engine_buttons();
  layout_ask();
}

/* ползунки прозрачности. Ползунок со стилями запоминает кисть фона, а при смене
   темы прежняя кисть удаляется — в тёмных темах под ним оставалась белая плашка.
   Поэтому при смене темы оба создаются заново на тех же местах (apply_theme) */
static void settings_trackbars(void) {
  if (!g_setHwnd) return;
  HWND *tb[2] = {&g_tbBg, &g_tbFg};
  static const int id[2] = {ID_ALPHA_BG, ID_ALPHA_FG};
  int pos[2] = {g_alphaFollow, g_alphaPinned};
  for (int i = 0; i < 2; i++) {
    RECT r = {0, 0, 120, 28};
    BOOL vis = TRUE, hide = FALSE;
    if (*tb[i]) {
      GetWindowRect(*tb[i], &r);
      MapWindowPoints(HWND_DESKTOP, g_setHwnd, (POINT *)&r, 2);
      vis = (GetWindowLongW(*tb[i], GWL_STYLE) & WS_VISIBLE) != 0;
      hide = GetPropW(*tb[i], L"CpScrollHide") != NULL; /* спрятан прокруткой — пусть знает об этом и новый */
      RemovePropW(*tb[i], L"CpScrollHide");
      DestroyWindow(*tb[i]);
    }
    *tb[i] = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | (vis ? WS_VISIBLE : 0) | TBS_NOTICKS | TBS_TOOLTIPS,
                             r.left, r.top, r.right - r.left, r.bottom - r.top, g_setHwnd, (HMENU)(INT_PTR)id[i], NULL, NULL);
    if (!*tb[i]) continue;
    if (hide) SetPropW(*tb[i], L"CpScrollHide", (HANDLE)1);
    SendMessageW(*tb[i], TBM_SETRANGEMIN, FALSE, 40);
    SendMessageW(*tb[i], TBM_SETRANGEMAX, FALSE, 255);
    SendMessageW(*tb[i], TBM_SETPOS, TRUE, pos[i]);
    EnableWindow(*tb[i], !g_follow);
  }
}

static void create_settings(HWND owner) {
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = SettingsProc;
  wc.hInstance = g_inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = g_paperDark;
  wc.lpszClassName = L"CursorPadSettings";
  RegisterClassExW(&wc);
  g_setHwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"CursorPadSettings",
      L"Настройки", WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN, 0, 0,
      SET_W, g_setContentH, owner, NULL, g_inst, NULL);
  mk_btn(g_setHwnd, L"×", ID_PANEL_CLOSE);
  round_corners(g_setHwnd);
  /* кнопки страницы стоят на светлых карточках — под ними светлый фон (ui_theme.c) */
  SetPropW(g_setHwnd, L"CpUiBg", (HANDLE)(INT_PTR)1);
  for (int i = 0; i < SEC_COUNT; i++) g_btnSec[i] = mk_btn(g_setHwnd, kSecNames[i], ID_SEC_BASE + i);
  g_preview = preview_create(g_setHwnd, ID_PREVIEW);
  g_filesRootEdit = CreateWindowExW(0, L"EDIT", g_filesRoot,
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    0, 0, 200, 26, g_setHwnd, (HMENU)(INT_PTR)ID_FILES_ROOT, NULL, NULL);
  mk_btn(g_setHwnd, L"Обзор…", ID_FILES_BROWSE);
  g_btnIdx = mk_btn(g_setHwnd, L"Обновить JSON", ID_FILES_REFRESH);
  g_filesStat = CreateWindowExW(0, L"STATIC", L"Индекс: —",
                                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS, 0, 0, 200, 22, g_setHwnd,
                                (HMENU)(INT_PTR)ID_FILES_STAT, NULL, NULL);
  g_filesBar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 0, 0, 200, 6,
                               g_setHwnd, (HMENU)(INT_PTR)ID_FILES_BAR, NULL, NULL);
  g_chkNotify = CreateWindowExW(0, L"BUTTON", L"Сообщать о новых и изменённых файлах",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26,
                                g_setHwnd, (HMENU)(INT_PTR)ID_FILES_NOTIFY, NULL, NULL);
  g_filesWatchEdit = CreateWindowExW(0, L"EDIT", g_filesWatch,
                                     WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                     0, 0, 200, 26, g_setHwnd, (HMENU)(INT_PTR)ID_WATCH_EDIT, NULL, NULL);
  mk_btn(g_setHwnd, L"Обзор…", ID_WATCH_BROWSE);
  g_desCodesEdit = CreateWindowExW(0, L"EDIT", g_desCodesUser, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 200, 26,
                                   g_setHwnd, (HMENU)(INT_PTR)ID_DES_CODES, NULL, NULL);
  g_plmServer = CreateWindowExW(0, L"EDIT", g_sqlHost,
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 120, 26, g_setHwnd, (HMENU)(INT_PTR)ID_PLM_SERVER, NULL, NULL);
  g_plmDb = CreateWindowExW(0, L"EDIT", g_plmDatabase[0] ? g_plmDatabase : L"",
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            0, 0, 120, 26, g_setHwnd, (HMENU)(INT_PTR)ID_PLM_DB, NULL, NULL);
  g_plmUser = CreateWindowExW(0, L"EDIT", g_sqlUser,
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                              0, 0, 120, 26, g_setHwnd, (HMENU)(INT_PTR)ID_PLM_USER, NULL, NULL);
  g_plmPass = CreateWindowExW(0, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
                              0, 0, 120, 26, g_setHwnd, (HMENU)(INT_PTR)ID_PLM_PASS, NULL, NULL);
  g_shareEdit = CreateWindowExW(0, L"EDIT", g_shareRoot, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 200, 26, g_setHwnd, (HMENU)(INT_PTR)ID_SHARE_EDIT, NULL, NULL);
  mk_btn(g_setHwnd, L"Обзор…", ID_SHARE_BROWSE);
  g_chkServe = CreateWindowExW(0, L"BUTTON", L"Раздавать PLM коллегам через меня",
                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26,
                               g_setHwnd, (HMENU)(INT_PTR)ID_SHARE_SERVE, NULL, NULL);
  g_btnNd = mk_btn(g_setHwnd, L"Нарды", ID_NARDY_BTN);
  g_btnSh = mk_btn(g_setHwnd, L"Шашки", ID_SHASHKI_BTN);
  g_btnMb = mk_btn(g_setHwnd, L"Морской бой", ID_MORSKOY_BTN);
  g_btnSx = mk_btn(g_setHwnd, L"Шахматы", ID_SHAHMATY_BTN);
  g_btnK2 = mk_btn(g_setHwnd, L"Мечник", ID_CUR_K2);
  g_btnK3 = mk_btn(g_setHwnd, L"Рукавица", ID_CUR_K3);
  g_btnK4 = mk_btn(g_setHwnd, L"Фея", ID_CUR_K4);
  g_btnK5 = mk_btn(g_setHwnd, L"Дракон", ID_CUR_K5);
  g_btnK6 = mk_btn(g_setHwnd, L"Чёрный кот", ID_CUR_K6);
  g_btnK7 = mk_btn(g_setHwnd, L"Риас", ID_CUR_K7);
  g_btnK8 = mk_btn(g_setHwnd, L"Тяночка", ID_CUR_K8);
  g_btnKit = mk_btn(g_setHwnd, L"Современный", ID_CUR_KIT);
  for (int i = 0; i < ST_COUNT; i++) g_btnSt[i] = mk_btn(g_setHwnd, kStyleNames[i], ID_CUR_ST_BASE + i);
  g_btnSys = mk_btn(g_setHwnd, L"Курсор Windows", ID_SYS_CUR);
  for (int i = 0; i < FX_OPTIONS; i++) g_btnFx[i] = mk_btn(g_setHwnd, kFxNames[i], ID_FX_BASE + i);
  g_chkTess = CreateWindowExW(0, L"BUTTON", L"точное распознавание (Tesseract)",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26,
                              g_setHwnd, (HMENU)(INT_PTR)ID_CHK_TESS, NULL, NULL);
  g_chkAuto = CreateWindowExW(0, L"BUTTON", L"Автозапуск с Windows",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 200, 26,
                              g_setHwnd, (HMENU)(INT_PTR)ID_AUTOSTART, NULL, NULL);
  g_chkAnsPin = CreateWindowExW(0, L"BUTTON", L"Окно находок оставлять на месте",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26,
                                g_setHwnd, (HMENU)(INT_PTR)ID_ANSPIN, NULL, NULL);
  g_ocr = mk_btn(g_setHwnd, L"Выделить и прочитать (F6)", ID_OCR);
  g_btnHk = mk_btn(g_setHwnd, L"Горячие клавиши…", ID_HOTKEYS);
  g_btnUp = mk_btn(g_setHwnd, L"Проверить обновления", ID_UPDATE);
  for (int i = 0; i < THEME_COUNT; i++)
    g_btnTheme[i] = mk_btn(g_setHwnd, kThemes[i].name, ID_THEME_BASE + i);
  for (int i = 0; i < IDLE_OPTIONS; i++) g_btnIdle[i] = mk_btn(g_setHwnd, kIdleNames[i], ID_IDLE_BASE + i);
  g_btnIdleShow = mk_btn(g_setHwnd, L"Показать бой сейчас", ID_IDLE_SHOW);
  for (int i = 0; i < IDLE_SIZES; i++) g_btnIdleSz[i] = mk_btn(g_setHwnd, kIdleSzNames[i], ID_IDLE_SZ + i);
  g_btnIdleWatch = mk_btn(g_setHwnd, L"Режим просмотра", ID_IDLE_WATCH);
  for (int i = 0; i < RING_STYLES; i++) g_btnRing[i] = mk_btn(g_setHwnd, kRingNames[i], ID_RING_BASE + i);
  g_btnRingBlur = mk_btn(g_setHwnd, L"Размытие под плитками", ID_RING_BLUR);
  g_btnRingDots = mk_btn(g_setHwnd, L"Точки по контуру", ID_RING_DOTS);
  g_btnRingAnim = mk_btn(g_setHwnd, L"Плавные анимации", ID_RING_ANIM);
  update_ring_buttons();
  g_kitPreview = kit_preview_create(g_setHwnd, ID_KIT_PREVIEW);
  for (int i = 0; i < KIT_STYLES; i++) g_btnKitSt[i] = mk_btn(g_setHwnd, kKitNames[i], ID_KIT_BASE + i);
  for (int i = 0; i < KIT_SIZES; i++) g_btnKitSz[i] = mk_btn(g_setHwnd, kKitSzNames[i], ID_KIT_SZ + i);
  g_btnKitOut = mk_btn(g_setHwnd, L"Обводка", ID_KIT_OUT);
  g_btnKitShadow = mk_btn(g_setHwnd, L"Тень", ID_KIT_SHADOW);
  g_btnKitAcc = mk_btn(g_setHwnd, L"Цвет темы", ID_KIT_ACC);
  g_btnKitAnim = mk_btn(g_setHwnd, L"Анимация", ID_KIT_ANIM);
  g_btnKitFx = mk_btn(g_setHwnd, L"Эффект нажатия", ID_KIT_FX);
  update_kit_buttons();
  settings_trackbars();
  SendMessageW(g_btnK2, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnK3, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnK4, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnK5, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnK6, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnK7, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnK8, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnKit, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnSys, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_ocr, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_btnUp) SendMessageW(g_btnUp, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_btnHk) SendMessageW(g_btnHk, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_filesRootEdit) {
    SendMessageW(g_filesRootEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    SendMessageW(g_filesRootEdit, 0x1501, TRUE, (LPARAM)L"папка сети \\\\server\\share");
  }
  if (g_btnIdx) SendMessageW(g_btnIdx, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_filesStat) SendMessageW(g_filesStat, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
  files_refresh_status();
  if (g_plmServer) SendMessageW(g_plmServer, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  if (g_plmDb) SendMessageW(g_plmDb, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  if (g_plmUser) SendMessageW(g_plmUser, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  if (g_plmPass) SendMessageW(g_plmPass, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
  if (g_plmServer) SendMessageW(g_plmServer, 0x1501, TRUE, (LPARAM)L"SQL UM-SQLSRV");
  if (g_plmDb) SendMessageW(g_plmDb, 0x1501, TRUE, (LPARAM)L"база SQL");
  if (g_plmUser) SendMessageW(g_plmUser, 0x1501, TRUE, (LPARAM)L"логин SQL");
  if (g_plmPass) SendMessageW(g_plmPass, 0x1501, TRUE, (LPARAM)L"пароль SQL");
  if (g_sqlPass[0] && g_plmPass) SetWindowTextW(g_plmPass, L"********");
  if (g_chkAuto) SendMessageW(g_chkAuto, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  /* вторая галочка шрифта не получала и рисовалась системным — и теме не подчинялась */
  if (g_chkAnsPin) SendMessageW(g_chkAnsPin, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_chkAuto) SendMessageW(g_chkAuto, BM_SETCHECK, g_autostart ? BST_CHECKED : BST_UNCHECKED, 0);
  if (g_chkTess) {
    ocr_engine_load();
    SendMessageW(g_chkTess, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_chkTess, BM_SETCHECK, g_ocrWinOnly ? BST_UNCHECKED : BST_CHECKED, 0);
    EnableWindow(g_chkTess, tess_ready());
  }
  if (g_chkAnsPin)
    SendMessageW(g_chkAnsPin, BM_SETCHECK, g_ansKeepPos ? BST_CHECKED : BST_UNCHECKED, 0);
  ai_ui_create(g_setHwnd);
  nl_create(g_setHwnd);
  if (g_chkNotify) {
    SendMessageW(g_chkNotify, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_chkNotify, BM_SETCHECK, g_filesNotify ? BST_CHECKED : BST_UNCHECKED, 0);
  }
  if (g_shareEdit) {
    SendMessageW(g_shareEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    SendMessageW(g_shareEdit, 0x1501, TRUE, (LPARAM)L"не задана — обмена нет");
  }
  if (g_chkServe) {
    SendMessageW(g_chkServe, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_chkServe, BM_SETCHECK, g_shareServe ? BST_CHECKED : BST_UNCHECKED, 0);
  }
  if (g_filesWatchEdit) {
    SendMessageW(g_filesWatchEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    SendMessageW(g_filesWatchEdit, 0x1501, TRUE, (LPARAM)L"следить: вся папка архива");
  }
  if (g_desCodesEdit) {
    SendMessageW(g_desCodesEdit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    des_codes_cue();
  }
  watch_enable();
  update_engine_buttons();
  update_cursor_buttons(); /* отметка ● у выбранного: в том числе у новых стилей */
  update_idle_buttons();
  layout_settings();
}

#include "cutting.c"
#include "paint.c" /* расчёт краски — после резки: общие кирпичики рисования */
#include "tiffmerge.c"
#include "tiffsort.c"
#include "drawrec.c"    /* распознать чертёж: штамп, спецификация, тип листа */
#include "techno.c"     /* маршрут обработки по чертежу: правила технолога */
#include "tables_seed.c"  /* встроенные таблицы (ОМН, стандарты) — в папку пользователя, с обновлением */
#include "norms.c"      /* нормы времени операций маршрута по таблицам ОМН (после резки и маршрута) */
#include "pk_norms.c"  /* нормы Тпз/Тшт операций окна «ЭСИ и ТП» тем же norms.c */
#include "norms_set.c"  /* Настройки → «Нормирование»: пределы режимов резания */
#include "cut_ask.c"    /* расход на резку по вопросу словами: чат и инструмент агента cut_calc */
#include "std.c"        /* справочник ГОСТ/DIN/ISO: разбор таблиц и поиск */
#include "std_files.c"  /* его таблицы с диска пользователя (CursorPad-std) */
#include "soyuz_ops.c"  /* маршрут → операции ТП «Союза»: разбор, коды справочника, сверка */
#include "pk_paste.c"    /* «Вставить» окна «ЭСИ и ТП»: что бы ни скопировали — строки по листам */
#include "pk_paste_ui.c" /* его раскладка по листам окна (package_ui.c) */
#include "soyuz_ui.c"   /* окно «В Союз»: буфер по операции, аналоги и слежение в базе */
#include "soyuz_auto.c" /* «Вставить сам»: пункт меню клиента через UI Automation */
#include "soyuz_make.c" /* «Создать ТП»: деталь и ТП функцией пакета «CursorPad — Союз» через мост */
#include "soyuz_asmfolder.c" /* папка под сборку: код и группа из обозначения, имя как у соседних папок */
#include "drawrec_ui.c" /* окно «Распознать чертёж» */
#include "drawrec_sort.c" /* «Разложить по папкам»: имена по штампу, копия, журнал, отмена */
#include "ai_chat.c" /* «Спросить ИИ»: чат с локальной моделью, после окон чертежа */
#include "norms_ui.c" /* окно «Режимы»: параметры норм по операциям, правка с пересчётом (после чата) */
#include "search_history.c" /* история поиска и избранное: Ctrl+H, Ctrl+D (после чата: та же запись файлов) */
#include "ai_memory.c" /* «Что помнит ИИ»: тетрадь «запомни» и записная книжка — рядом с чатом */
#include "game_net.c" /* связь игр с коллегами: приглашения, файлы партии, копии ходов */
#include "nardy.c"
#include "shashki.c" /* после нард: общий список «кто в сети» и мелочи работы с папкой */
#include "morskoy.c" /* морской бой — на той же связи */
#include "shahmaty.c" /* шахматы — тоже */
#include "hotkeys.c"    /* F8, F6, Pause… — какие назначены в Настройках */
#include "layout_fix.c" /* Pause: «ghbdtn» → «привет» */
#include "msg.c"        /* исчезающие сообщения коллегам — через ту же папку, что нарды */
#include "convert.c" /* Shift-drag conversion ring; real OLE drop target */
#include "file_link_ui.c" /* плитка «Ссылка» кольца Shift: путь к файлу или к папке — в буфер */
#include "ai_file_ask.c" /* «прочитай / переведи файл» в чате: Tesseract и исполнитель конвертера — после convert.c */
#include "pk_stamp.c" /* чертежи мышью в окно «ЭСИ и ТП»: штамп → строка, сборка → состав (после ai_file_ask.c: PDF) */
#include "ai_esi.c"   /* действия из чата: «загрузи ЭСИ по чертежам», «выгрузи МВ», «приоритет участков» */
#include "tools.c" /* после нардов: автообновление смотрит, не открыты ли они */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE: {
    g_hwnd = hwnd;
    ui_init(); /* оформление: плавное появление окон, подсветка, хром Windows 11 */
    apply_dpi(hwnd);
    g_paper = CreateSolidBrush(COL_PAPER);
    g_paperDark = CreateSolidBrush(COL_PAPER_DARK);
    g_fontDisplay = make_font(L"Segoe UI Variable Display", 13, FW_SEMIBOLD);
    if (!g_fontDisplay) g_fontDisplay = make_font(L"Segoe UI", 13, FW_SEMIBOLD);
    g_fontUi = make_font(L"Segoe UI Variable Text", 9, FW_SEMIBOLD);
    if (!g_fontUi) g_fontUi = make_font(L"Segoe UI", 9, FW_SEMIBOLD);
    g_fontBody = make_font(L"Segoe UI Variable Text", 10, FW_NORMAL);
    if (!g_fontBody) g_fontBody = make_font(L"Segoe UI", 10, FW_NORMAL);
    /* карточка PLM печатается колонками — на пропорциональном шрифте они
       разъезжаются, поэтому для неё держим моноширинный */
    g_fontMono = make_font(L"Consolas", 10, FW_NORMAL);
    if (!g_fontMono) g_fontMono = make_font(L"Courier New", 10, FW_NORMAL);
    g_fontMonoSm = make_font(font_exists(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas", 8, FW_NORMAL);
    g_fontSmall = make_font(L"Segoe UI Variable Small", 8, FW_NORMAL);
    if (!g_fontSmall) g_fontSmall = make_font(L"Segoe UI", 8, FW_NORMAL);

    pad_shadow_sync(hwnd); /* до первого показа: тень «Чертежа» сразу */
    g_pin = mk_btn(hwnd, L"Закрепить", ID_PIN);
    /* «Сброс» с 2026.09.23.41 — пункт «Очистить блокнот…» в меню «Ещё» */
    g_clipHistBtn = mk_btn(hwnd, L"▾", ID_CLIPHIST);
    g_close = mk_btn(hwnd, L"×", ID_CLOSE);
    g_min = mk_btn(hwnd, L"–", ID_MIN);
    g_edit = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
                                 ES_WANTRETURN | WS_VSCROLL,
                             0, 0, 100, 100, hwnd, (HMENU)(INT_PTR)ID_EDIT, NULL, NULL);
    g_clipEdit = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 0, 0, 100, 24, hwnd, (HMENU)(INT_PTR)ID_CLIP, NULL, NULL);
    g_btnAsk = mk_btn(hwnd, L"Поиск", ID_ASK_TAB);
    g_btnNotes = mk_btn(hwnd, L"Блокнот", ID_NOTES_TAB);
    g_btnMore = mk_btn(hwnd, L"Ещё", ID_MORE_BTN);       /* рисуется значком ⋯ */
    g_btnSet = mk_btn(hwnd, L"Настройки", ID_SETTINGS); /* рисуется значком ⚙ */
    SendMessageW(g_pin, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    if (g_clipClr) SendMessageW(g_clipClr, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    if (g_clipHistBtn) SendMessageW(g_clipHistBtn, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_close, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_min, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    if (g_clipEdit) {
      SendMessageW(g_clipEdit, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
      SendMessageW(g_clipEdit, 0x1501, TRUE, (LPARAM)L"буфер копии"); /* с клавишей — hk_register_all */
    }
    SendMessageW(g_btnSet, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    if (g_btnAsk) SendMessageW(g_btnAsk, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    if (g_btnNotes) SendMessageW(g_btnNotes, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    if (g_btnMore) SendMessageW(g_btnMore, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_edit, EM_SETLIMITTEXT, 200000, 0);
    g_oldEdit = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    notes_path();
    cleanup_old_bins();
    restore_if_stale_lock();
    extract_payloads();
    load_notes();
    load_cursor_pref();
    load_plm_pref();
    nl_load(); /* пределы режимов для норм (norms.ini) */
    share_start();
    nardy_start(); /* «я в сети» для нард и приглашения — пока задана общая папка */
    shashki_start(); /* приглашения и партии в шашки — там же */
    morskoy_start(); /* и в морской бой */
    shahmaty_start(); /* и в шахматы */
    /* чат с 2026.09.23.75 выключен (пока не работает): его поток каждую
       секунду-три читал и удалял файлы в общей папке — после .73 сработал
       «Анализ поведения» Kaspersky */
    if (0) msg_start();
    load_files_pref();
    if (g_autostart) autostart_set(TRUE);
    create_settings(hwnd);
    create_ask(hwnd);
    layout_children(); /* поле поиска создано — расставить по режиму окна */
    create_answer(hwnd);
    create_card(hwnd);
    create_ocr(hwnd);
    apply_theme();
    SendMessageW(g_tbBg, TBM_SETPOS, TRUE, g_alphaFollow);
    SendMessageW(g_tbFg, TBM_SETPOS, TRUE, g_alphaPinned);
    load_cursor_frames(g_inst);
    update_cursor_buttons();
    round_corners(hwnd);
    layout_children();
    cv_init();
    SetTimer(hwnd, TIMER_FOLLOW, 10, NULL);
    tools_cleanup_old(); /* страницы и Edge прежних версий */
    if (!UPD_AUTO_DISABLED) SetTimer(hwnd, TIMER_UPD_AUTO, 90000, NULL); /* обновление — само, через полторы минуты */
    SetTimer(hwnd, TIMER_UPD_CONFIRM, 45000, NULL); /* проработали 45 с — версия годная */
    SetTimer(hwnd, TIMER_UPD_NOTE, 8000, NULL);     /* не было ли отката */
    SetTimer(hwnd, TIMER_SAVE, 2000, NULL);
    SetTimer(hwnd, TIMER_CURSOR_KEEP, 4000, NULL);
    /* раз в пять минут спрашиваем, не исполнился ли индексу час: так час
       считается от последнего обхода, а не от запуска программы */
    SetTimer(hwnd, TIMER_FILES, 300000, NULL);
    SetTimer(hwnd, TIMER_FILES_PLAN, 60000, NULL);
    SetTimer(hwnd, TIMER_IDLE_FIGHT, 1000, NULL);
    if (g_filesRoot[0]) files_start_index(FALSE);
    add_tray(hwnd);
    /* F8, F9, F3, F6, F7, Pause, Ctrl+1…9 — или какие назначены в Настройках */
    hk_register_all(hwnd);
    if (g_hkBusy[HK_TOGGLE] || (!g_hkOwn[HK_TOGGLE] && !g_hkName[HK_TOGGLE][0])) {
      MessageBoxW(hwnd,
                  L"Не удалось зарегистрировать горячую клавишу закрепления: её держит другая программа. "
                  L"Закрепляйте окно кнопкой или выберите другую клавишу: Настройки → «Горячие клавиши».",
                  L"CursorPad", MB_OK | MB_ICONINFORMATION);
    }
    AddClipboardFormatListener(hwnd);
    apply_follow_state();
    if (g_skin != 0) install_scheme_cursors();
    fx_sync();
    ac_avatar_startup();
    return 0;
  }
  case WM_SIZE:
    layout_children();
    return 0;
  case WM_MEASUREITEM: /* меню «Ещё» рисуется само */
    if (((MEASUREITEMSTRUCT *)lParam)->CtlType == ODT_MENU) {
      tools_menu_measure((MEASUREITEMSTRUCT *)lParam);
      return TRUE;
    }
    break;
  case WM_DRAWITEM:
    if (((const DRAWITEMSTRUCT *)lParam)->CtlType == ODT_MENU) tools_menu_draw((const DRAWITEMSTRUCT *)lParam);
    else draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC pdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    /* рисуем в памяти и кладём разом (с 2026.10.21): тёмная шапка «Модерна»
       не мигает светлым при перерисовке */
    HDC hdc = CreateCompatibleDC(pdc);
    HBITMAP pbm = hdc ? CreateCompatibleBitmap(pdc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1) : NULL;
    HGDIOBJ pold = NULL;
    if (hdc && pbm) {
      pold = SelectObject(hdc, pbm);
    } else {
      if (hdc) DeleteDC(hdc);
      if (pbm) DeleteObject(pbm);
      hdc = pdc;
    }
    FillRect(hdc, &rc, bg_brush(FALSE));
    int dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
      typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
      GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
      if (fn) dpi = (int)fn(hwnd);
    }
    int th = MulDiv(TITLE_H, dpi, 96);
    int mh = MulDiv(MODE_H, dpi, 96);
    int fh = MulDiv(FOOT_H, dpi, 96);
    int th0 = th; /* сама шапка — название и версия */
    th += mh;     /* полоса шапки — вместе с рядом режимов */
    RECT title = {0, 0, rc.right, th};
    RECT foot = {0, rc.bottom - fh, rc.right, rc.bottom};
    const PadTheme *pth = &kThemes[g_theme];
    /* шапка и низ блокнота — в том же стиле, что шапки остальных окон */
    if (pth->head == HEAD_BANNER) {
      /* рыцарская: шапка и низ — тёмный пергамент под двойной золотой линией */
      FillRect(hdc, &title, bg_brush(TRUE));
      FillRect(hdc, &foot, bg_brush(TRUE));
      HBRUSH gold = CreateSolidBrush(KN_GOLD), dk = CreateSolidBrush(KN_GOLD_DK);
      RECT r1 = {0, th - 4, rc.right, th - 2}, r2 = {0, th - 1, rc.right, th};
      RECT f1 = {0, rc.bottom - fh, rc.right, rc.bottom - fh + 1};
      RECT f2 = {0, rc.bottom - fh + 2, rc.right, rc.bottom - fh + 4};
      FillRect(hdc, &r1, gold);
      FillRect(hdc, &r2, dk);
      FillRect(hdc, &f1, dk);
      FillRect(hdc, &f2, gold);
      DeleteObject(gold);
      DeleteObject(dk);
    } else if (pth->head == HEAD_BAND) {
      FillRect(hdc, &title, g_paperDark);
      FillRect(hdc, &foot, g_paperDark);
    }
    if (pth->head != HEAD_PLAIN && pth->head != HEAD_BANNER) {
      int w = (pth->head == HEAD_RULE && pth->caps) ? 2 : 1;
      RECT rule = {0, th - w, rc.right, th};
      RECT frule = {0, rc.bottom - fh, rc.right, rc.bottom - fh + w};
      HBRUSH line = CreateSolidBrush(pth->head == HEAD_BAND
                                         ? COL_LINE
                                         : blend_rgb(COL_INK, COL_PAPER, pth->caps ? 40 : 150));
      FillRect(hdc, &rule, line);
      FillRect(hdc, &frule, line);
      DeleteObject(line);
    }

    SetBkMode(hdc, TRANSPARENT);
    int padx = MulDiv(PAD, dpi, 96);
    RECT ttext = {padx + MulDiv(6, dpi, 96), MulDiv(4, dpi, 96), rc.right / 2, th0 - MulDiv(14, dpi, 96)};
    RECT vtext = {padx + MulDiv(6, dpi, 96), th0 - MulDiv(18, dpi, 96), rc.right / 2, th0 - 2};
    /* подложка переключателя «Блокнот | Поиск»; у «Чертежа» — рамка ячеек и черты шапки */
    if (g_btnNotes && g_btnAsk && chertezh_on()) {
      RECT a, b;
      GetWindowRect(g_btnNotes, &a);
      GetWindowRect(g_btnAsk, &b);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&a, 2);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&b, 2);
      HBRUSH ink = CreateSolidBrush(COL_INK);
      RECT fr = {a.left - 1, a.top - 1, b.right + 1, b.bottom + 1};
      FrameRect(hdc, &fr, ink);
      RECT mid = {a.right, a.top, a.right + 1, a.bottom};
      FillRect(hdc, &mid, ink);
      RECT r0 = {0, th0 - 1, rc.right, th0}; /* шапка | ряд режимов */
      FillRect(hdc, &r0, ink);
      DeleteObject(ink);
      int clipH = MulDiv(CLIP_H, dpi, 96);
      HBRUSH hl = CreateSolidBrush(blend_rgb(COL_LINE, COL_INK, 60));
      RECT r1 = {0, th + clipH, rc.right, th + clipH + 1}; /* под буфером копии */
      FillRect(hdc, &r1, hl);
      DeleteObject(hl);
    } else if (g_btnNotes && g_btnAsk && !pth->ornate) {
      RECT a, b;
      GetWindowRect(g_btnNotes, &a);
      GetWindowRect(g_btnAsk, &b);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&a, 2);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&b, 2);
      int in = MulDiv(3, dpi, 96);
      RECT seg = {a.left - in, a.top - in, b.right + in, b.bottom + in};
      int tr = kThemes[g_theme].radius;
      int rad = tr < 0 ? (seg.bottom - seg.top) / 2 : tr == 0 ? 0 : tr + 1;
      fill_round_rect(hdc, seg, seg_fill_color(), seg_fill_color(), rad);
    }
    /* строка подсказки внизу */
    {
      if (g_fontSmall) SelectObject(hdc, g_fontSmall);
      SetTextColor(hdc, COL_MUTED);
      RECT ft = {padx + MulDiv(6, dpi, 96), rc.bottom - fh, rc.right - padx, rc.bottom};
      wchar_t ht[128];
      if (g_padMode == 1) {
        hk_with(L"Enter — найти · пусто — ищу скопированное", HK_SEARCH, ht, 128);
      } else {
        /* клавиши — какие назначены; убранные не упоминаем */
        ht[0] = 0;
        if (g_hkName[HK_SNIP][0]) _snwprintf(ht, 128, L"%s — вставить строку", g_hkName[HK_SNIP]);
        if (g_hkName[HK_TOGGLE][0]) {
          size_t l = wcslen(ht);
          _snwprintf(ht + l, 128 - l, L"%s%s — закрепить", l ? L" · " : L"", g_hkName[HK_TOGGLE]);
        }
        ht[127] = 0;
      }
      if (chertezh_on()) draw_chertezh_stamp(hdc, rc, dpi, fh, ht);
      else DrawTextW(hdc, ht, -1, &ft, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (g_statusOn) {
      SetTextColor(hdc, COL_INK);
      if (g_fontUi) SelectObject(hdc, g_fontUi);
      RECT st = {padx + MulDiv(6, dpi, 96), 0, rc.right / 2, th0};
      DrawTextW(hdc, g_status, -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
      SetTextColor(hdc, COL_INK);
      if (pth->ornate) {
        /* щит перед названием, само название — алым готическим */
        int sw = MulDiv(18, dpi, 96), sh = MulDiv(22, dpi, 96);
        draw_shield(hdc, padx + MulDiv(4, dpi, 96), (th - sh) / 2, sw, sh);
        ttext.left += sw + MulDiv(6, dpi, 96);
        vtext.left += sw + MulDiv(6, dpi, 96);
        SetTextColor(hdc, KN_CRIMSON);
      }
      if (g_fontDisplay) SelectObject(hdc, g_fontDisplay);
      if (chertezh_on()) { /* «Чертёж»: прописными вразрядку, версия — моноширинным */
        int ox = SetTextCharacterExtra(hdc, MulDiv(2, dpi, 96));
        DrawTextW(hdc, L"CURSORPAD", -1, &ttext, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
        SetTextCharacterExtra(hdc, ox);
      } else {
        DrawTextW(hdc, L"CursorPad", -1, &ttext, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
      }
      SetTextColor(hdc, COL_MUTED);
      if (g_fontSmall) SelectObject(hdc, chertezh_on() && g_fontMonoSm ? g_fontMonoSm : g_fontSmall);
      DrawTextW(hdc, APP_VERSION_STR, -1, &vtext, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    if (g_padMode == 0) {
      if (chertezh_on()) draw_chertezh_gutter(hdc, dpi);
      draw_gutter_thumbs(hdc, dpi, th, fh);
    } else if (g_btnAi) {
      /* режим поиска: «Где:» перед ярлыками */
      RECT wr;
      GetWindowRect(g_btnAi, &wr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&wr, 2);
      if (g_fontSmall) SelectObject(hdc, g_fontSmall);
      SetTextColor(hdc, COL_MUTED);
      RECT a = {MulDiv(GUTTER, dpi, 96), wr.top, wr.left - 2, wr.bottom};
      DrawTextW(hdc, L"Где:", -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* с 2026.10.21 цветной полосы по левому краю нет: цвет акцента — только у
       главной кнопки, выбранного и фокуса; край окна даёт рамка Windows */
    if (pth->ornate) draw_knight_frame(hdc, rc);
    ui_sheet_frame(hdc, rc, hwnd); /* «Чертёж»: рамка листа по периметру, иначе окно сливалось с белым */
    if (hdc != pdc) {
      BitBlt(pdc, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
      SelectObject(hdc, pold);
      DeleteObject(pbm);
      DeleteDC(hdc);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    if ((HWND)lParam == g_clipEdit || (HWND)lParam == g_searchEdit) {
      SetBkColor(hdc, COL_PAPER_DARK);
      SetTextColor(hdc, COL_INK);
      return (LRESULT)g_paperDark;
    }
    SetBkColor(hdc, COL_PAPER);
    SetTextColor(hdc, COL_INK);
    return (LRESULT)g_paper;
  }
  case WM_CTLCOLORBTN: {
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, COL_PAPER_DARK);
    return (LRESULT)g_paperDark;
  }
  case WM_COMMAND:
    if (LOWORD(wParam) == ID_PIN) toggle_follow();
    if (LOWORD(wParam) == ID_CLIPCLR) clear_copied();
    if (LOWORD(wParam) == ID_CLIPHIST) clip_hist_menu();
    if (LOWORD(wParam) == ID_CLOSE) DestroyWindow(hwnd);
    if (LOWORD(wParam) == ID_MIN) toggle_hidden();
    if (LOWORD(wParam) == ID_SETTINGS) toggle_settings();
    if (LOWORD(wParam) == ID_ASK_TAB) set_pad_mode(1);
    if (LOWORD(wParam) == ID_NOTES_TAB) set_pad_mode(0);
    if (LOWORD(wParam) == ID_MORE_BTN && g_btnMore) tools_menu(hwnd, g_btnMore);
    if (LOWORD(wParam) == ID_SEARCH_GO || LOWORD(wParam) == ID_ENG_AI || LOWORD(wParam) == ID_ENG_PLM ||
        LOWORD(wParam) == ID_ENG_FILES)
      ask_command(LOWORD(wParam));
    if (LOWORD(wParam) == ID_EDIT && HIWORD(wParam) == EN_CHANGE) {
      g_dirty = TRUE;
      InvalidateRect(hwnd, NULL, FALSE);
      if (g_edit) InvalidateRect(g_edit, NULL, TRUE);
    }
    return 0;
  case WM_HOTKEY:
    if (wParam == HOTKEY_TOGGLE) toggle_follow();
    else if (wParam == HOTKEY_CURSOR) cycle_skin();
    else if (wParam == HOTKEY_OCR) run_ocr_test();
    else if (wParam == HOTKEY_MIN) toggle_hidden();
    else if (wParam == HOTKEY_SEARCH) search_clip_buf();
    else if (wParam == HOTKEY_LAYOUT) layout_fix();
    else if (wParam >= HOTKEY_SNIP_BASE && wParam < HOTKEY_SNIP_BASE + SNIP_COUNT)
      paste_line((int)(wParam - HOTKEY_SNIP_BASE + 1));
    return 0;
  case WM_CVX_DONE:
    cvx_done((CvxJob *)lParam); return 0;
  case WM_CONVERT_DONE:
    cv_done((CvJob *)lParam); return 0;
  case WM_TIMER:
    if (wParam == TIMER_CONVERT) { cv_tick(); return 0; }
    if (wParam == TIMER_LF_RESTORE) {
      layout_fix_timer();
      return 0;
    }
    if (wParam == TIMER_UPD_AUTO) {
      SetTimer(hwnd, TIMER_UPD_AUTO, 3 * 3600 * 1000, NULL); /* дальше — раз в три часа */
      start_update_auto();
      return 0;
    }
    if (wParam == TIMER_UPD_IDLE) {
      upd_idle_tick();
      return 0;
    }
    if (wParam == TIMER_UPD_CONFIRM) {
      KillTimer(hwnd, TIMER_UPD_CONFIRM);
      if (upd_trial_confirm()) { /* только что обновились — сказать, что нового */
        wchar_t notes[3000], head[200], title[64];
        upd_own_notes(notes, 3000, FALSE);
        upd_notes_head(notes, head, 200, 3);
        _snwprintf(title, 64, L"CursorPad обновлён до %s", APP_VERSION_STR);
        title[63] = 0;
        upd_balloon(3, title, head[0] ? head : L"Нажмите, чтобы посмотреть, что нового");
      }
      return 0;
    }
    if (wParam == TIMER_UPD_NOTE) {
      KillTimer(hwnd, TIMER_UPD_NOTE);
      wchar_t p[MAX_PATH];
      char b[64];
      upd_data_file(p, L"update-rollback.txt");
      if (g_dataDir[0] && upd_read_text(p, b, sizeof(b))) {
        DeleteFileW(p);
        long v = strtol(b, NULL, 10);
        wchar_t t[256];
        _snwprintf(t, 256,
                   L"Новая версия (%ld) не запустилась — вернул прежнюю, %s. Сама она больше не ставится.", v,
                   APP_VERSION_STR);
        t[255] = 0;
        upd_balloon(4, L"CursorPad: обновление откатено", t);
      }
      return 0;
    }
    if (wParam == TIMER_FOLLOW) {
      if (!g_follow || g_hidden) {
        /* двигать нечего — редкий тик только чтобы заметить, когда снова
           включат слежение */
        follow_rate(hwnd, 200);
      } else {
        POINT p;
        GetCursorPos(&p);
        /* курсор на помощнике мини-ИИ — блокнот прячется, а не заслоняет робота;
           едет дальше невидимым и появляется уже у курсора */
        BOOL overBot = ac_avatar_under(p);
        if (overBot != g_overBot) {
          g_overBot = overBot;
          ShowWindow(g_hwnd, overBot ? SW_HIDE : SW_SHOWNOACTIVATE);
        }
        int tx = p.x + g_offx;
        int ty = p.y + g_offy;
        clamp_to_work(&tx, &ty, p);
        double dx = tx - g_x, dy = ty - g_y;
        BOOL moved = p.x != g_lastCur.x || p.y != g_lastCur.y || fabs(dx) > 0.5 ||
                     fabs(dy) > 0.5;
        g_lastCur = p;
        if (moved) {
          g_followIdle = 0;
          follow_rate(hwnd, 10);
          g_x += dx * 0.28;
          g_y += dy * 0.28;
          int nx = (int)lround(g_x);
          int ny = (int)lround(g_y);
          SetWindowPos(g_hwnd, HWND_TOPMOST, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
          if (g_setHwnd && IsWindowVisible(g_setHwnd)) place_settings();
          if (g_askHwnd && IsWindowVisible(g_askHwnd)) place_ask();
        } else if (++g_followIdle > 20) {
          /* мышь стоит: 20 опросов в секунду вместо 100. Стронется — на
             следующем тике вернёмся к плавным 10 мс */
          follow_rate(hwnd, 50);
        }
      }
    }
    if (wParam == TIMER_SAVE && g_dirty) save_notes();
    if (wParam == TIMER_STATUS) {
      KillTimer(hwnd, TIMER_STATUS);
      g_statusOn = FALSE;
      InvalidateRect(hwnd, NULL, FALSE);
      /* пока идёт перенос в 1С, строка про него не гаснет */
      if (g_1cOn) onec_show();
    }
    if (wParam == TIMER_PASTE) {
      /* строки на Alt+1…9 или с Shift: пока их держат, наш Ctrl+V был бы
         другим сочетанием — ждём, пока отпустят, не дольше полутора секунд */
      static int pasteWaits;
      if (((GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_LWIN) |
            GetAsyncKeyState(VK_RWIN)) & 0x8000) && pasteWaits++ < 37)
        return 0;
      pasteWaits = 0;
      KillTimer(hwnd, TIMER_PASTE);
      send_paste();
    }
    if (wParam == TIMER_CURSOR_KEEP && g_skin > 0) {
      /* у «Риас» курсор анимированный: заново поставленный начинает мультик
         с первого кадра. Раз в 4 с это видно, раз в минуту — нет. Копия .ani
         — один кадр, поэтому набор ставится без CopyCursor. Сброс курсоров
         самой Windows ловит WM_SETTINGCHANGE — там сразу. */
      static int keepTicks;
      int slow = g_skin == 6 || g_skin == SKIN_K8 || (g_skin == SKIN_KIT && g_kitAnim);
      if (!slow || ++keepTicks % 15 == 0) apply_scheme_slots();
    }
    if (wParam == TIMER_FILES) files_start_index(FALSE);
    if (wParam == TIMER_FILES_TICK) files_refresh_status();
    if (wParam == TIMER_FILES_PLAN) files_plan_tick();
    if (wParam == TIMER_IDLE_FIGHT) idle_fight_tick();
    return 0;
  case WM_SEARCH_DONE: {
    wchar_t *text = search_done_take((SearchDone *)lParam); /* строки находок — в окно */
    if (text) {
      /* PLM строк не дал — чертежи, найденные пока он искал, оставляем ниже */
      if (g_plmCount == 0 && g_quickDraw && g_quickDraw[0]) {
        size_t n = wcslen(text) + wcslen(g_quickDraw) + 120;
        wchar_t *both = (wchar_t *)malloc(n * sizeof(wchar_t));
        if (both) {
          _snwprintf(both, n, L"%s\r\n\r\nЧертежи в архиве (двойной щелчок по строке открывает):\r\n%s", text,
                     g_quickDraw);
          both[n - 1] = 0;
          free(text);
          text = both;
        }
      }
      route_done((int)wParam);
      show_answer_text(text);
      free(text);
      ans_draw_from_text(g_quickDraw); /* PLM строк не дал — кнопки чертежа по первому из архива */
    }
    pf_start_async(); /* находки уже на экране — заготовки к ним досчитаются следом */
    return 0;
  }
  case WM_PF_DONE:
    pf_apply((PfJob *)lParam);
    return 0;
  /* карточка приходит отдельно: ей своё окно, находки остаются на месте */
  case WM_CARD_DONE: {
    wchar_t *text = (wchar_t *)lParam;
    if (text) {
      card_ops_commit();
      show_card_text(text);
      free(text);
    }
    return 0;
  }
  case WM_FILES_DONE: {
    KillTimer(hwnd, TIMER_FILES_TICK);
    if (lParam) files_full_done();
    files_refresh_status();
    des_codes_cue(); /* новый индекс — коды по архиву могли смениться */
    wchar_t m[160];
    if (wParam)
      _snwprintf(m, 160, L"JSON обновлён: %d файлов", (int)wParam);
    else
      lstrcpynW(m, L"JSON пуст — папка недоступна?", 160);
    show_status(m);
    return 0;
  }
  case WM_UPDATE_DONE:
    on_update_done((int)wParam);
    return 0;
  case WM_OCR_DONE: {
    wchar_t *text = (wchar_t *)lParam;
    if (text && text[0]) {
      /* «б» в «сб» обозначения распознавание отдаёт шестёркой: поправка — в окне, а с ним в буфере,
         блокноте и поиске; что поправлено — в строке состояния, как поправка поиска */
      wchar_t first[60];
      int sb = dr_text_fix_sb(text, first, 60);
      /* только в своё окно: в блокнот — кнопкой «В блокнот», если нужно */
      show_ocr_text(text);
      wchar_t s[200];
      if (sb)
        _snwprintf(s, 200, L"Распознано (%s) — с поправкой «6» → «б» в «сб»: %s%s", g_ocrBy[0] ? g_ocrBy : L"OCR",
                   first, sb > 1 ? L" и ещё" : L"");
      else
        _snwprintf(s, 200, L"Распознано (%s) — текст в отдельном окне", g_ocrBy[0] ? g_ocrBy : L"OCR");
      show_status(s);
    } else {
      show_status(g_ocrNote[0] ? g_ocrNote : L"Текст не распознан");
    }
    free(text);
    return 0;
  }
  case WM_SHOW_PAD:
    if (g_hidden) restore_from_tray();
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    return 0;
  case WM_SETTINGCHANGE:
  case WM_DISPLAYCHANGE:
    if (g_skin > 0) apply_scheme_slots();
    return 0;
  case WM_CLIPBOARDUPDATE:
    if (lf_quiet()) return 0; /* это исправление раскладки гоняет буфер — не копирование человека */
    /* своё мы помечаем флажком; без него — скопировал человек */
    if (!g_ownClip) onec_foreign_copy(), sz_foreign_copy();
    grab_last_copy();
    return 0;
  case WM_NCHITTEST: {
    LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
    if (hit != HTCLIENT) return hit;
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd, &pt);
    int dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
      typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
      GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
      if (fn) dpi = (int)fn(hwnd);
    }
    int th = MulDiv(TITLE_H, dpi, 96) + MulDiv(MODE_H, dpi, 96);
    if (!g_follow && pt.y < th) {
      HWND child = ChildWindowFromPoint(hwnd, pt);
      if (child == hwnd || child == NULL) return HTCAPTION;
    }
    return HTCLIENT;
  }
  case WM_FILES_CHANGES:
    files_on_changes((FileChanges *)lParam);
    return 0;
  case WM_TRAY:
    /* щелчок по всплывашке «Новое в папке» */
    if (lParam == NIN_BALLOONUSERCLICK) {
      if (g_balloonKind == 1) nardy_show();
      else if (g_balloonKind == 5) shashki_show();
      else if (g_balloonKind == 7) morskoy_show();
      else if (g_balloonKind == 8) shahmaty_show();
      else if (g_balloonKind == 6) cv_reveal();
      else if (g_balloonKind == 9) { /* «ссылка на файл скопирована» — сказано всё во всплывашке */ }
      else if (g_balloonKind == 2) { /* «вышла новая версия» — поставить сейчас */
        if (g_updReady && g_updPath[0]) {
          KillTimer(hwnd, TIMER_UPD_IDLE);
          g_updReady = FALSE;
          apply_update(g_updPath);
        }
      } else if (g_balloonKind == 3) { /* «обновлён» — показать, что нового */
        static wchar_t notes[3000];
        upd_own_notes(notes, 3000, TRUE);
        g_resultFiles = FALSE;
        g_plmCount = 0;
        g_ansTitle = L"Что нового";
        g_ansPlain = TRUE;
        show_answer_text(notes[0] ? notes : L"Описания нет.");
      } else if (g_balloonKind == 4) {
        /* откат — сказано всё во всплывашке */
      } else files_show_changes();
    }
    if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) tray_menu(hwnd);
    if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
      if (g_hidden) restore_from_tray();
      else toggle_follow();
    }
    return 0;
  case WM_CLOSE:
    if (g_cvBusy) { show_status(L"Дождитесь завершения конвертации файла."); return 0; }
    DestroyWindow(hwnd);
    return 0;
  case WM_NARDY_POLL:
    nd_on_poll((NdPoll *)lParam);
    msg_refresh_list(); /* «кто в сети» — и для сообщений */
    shashki_online_changed(); /* и для шашек */
    morskoy_online_changed(); /* и для морского боя */
    shahmaty_online_changed(); /* и для шахмат */
    return 0;
  case WM_SHASHKI_POLL:
    sh_on_poll((GnPoll *)lParam);
    return 0;
  case WM_NARDY_GAME:
    nd_on_game((GnPoll *)lParam);
    return 0;
  case WM_MORSKOY_POLL:
    mb_on_poll((GnPoll *)lParam);
    return 0;
  case WM_SHAHMATY_POLL:
    sx_on_poll((GnPoll *)lParam);
    return 0;
  case WM_MSG_IN:
    msg_show_in((MsgIn *)lParam);
    return 0;
  case WM_MSG_SENT:
    msg_on_sent((wchar_t *)lParam);
    return 0;
  case WM_FAIRY_CLICK:
    fx_burst((int)(LONG_PTR)wParam, (int)(LONG_PTR)lParam);
    return 0;
  case WM_DESTROY:
    cv_shutdown();
    InterlockedExchange(&g_quitting, 1);
    close_other_windows(hwnd);
    ac_avatar_destroy();
    KillTimer(hwnd, TIMER_IDLE_FIGHT);
    idle_fight_stop(TRUE);
    fx_shutdown();
    styles_free();
    kit_free();
    kpv_free();
    share_stop();
    ai_shutdown();
    save_notes();
    save_cursor_pref(); /* keeps the results panel's size across restarts */
    if (g_oldEdit && g_edit)
      SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)g_oldEdit);
    if (g_oldSearch && g_searchEdit)
      SetWindowLongPtrW(g_searchEdit, GWLP_WNDPROC, (LONG_PTR)g_oldSearch);
    if (g_setHwnd) DestroyWindow(g_setHwnd);
    if (g_askHwnd) DestroyWindow(g_askHwnd);
    if (g_answer) DestroyWindow(g_answer);
    if (g_card) DestroyWindow(g_card);
    if (g_ocrWnd) DestroyWindow(g_ocrWnd);
    if (g_pick) DestroyWindow(g_pick);
    restore_system_cursor();
    KillTimer(hwnd, TIMER_FOLLOW);
    KillTimer(hwnd, TIMER_SAVE);
    KillTimer(hwnd, TIMER_CURSOR_KEEP);
    KillTimer(hwnd, TIMER_FILES);
    KillTimer(hwnd, TIMER_FILES_PLAN);
    KillTimer(hwnd, TIMER_FILES_TICK);
    files_wait_idle(2000); /* a walk over a slow share must not outlive us */
    save_files_pref();
    files_clear();
    files_on_changes(NULL);
    hk_unregister_all(hwnd);
    RemoveClipboardFormatListener(hwnd);
    if (g_trayAdded) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    free_cursor_frames();
    if (g_fontUi) DeleteObject(g_fontUi);
    if (g_fontBody) DeleteObject(g_fontBody);
    if (g_fontSmall) DeleteObject(g_fontSmall);
    if (g_fontDisplay) DeleteObject(g_fontDisplay);
    if (g_paper) DeleteObject(g_paper);
    if (g_paperDark) DeleteObject(g_paperDark);
    if (g_mutex) CloseHandle(g_mutex);
    g_mutex = NULL;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* Все прочие окна программы — шашки, нарды, чат, окна «Ещё», горячие
   клавиши — закрыть вместе с главным. Раньше они оставались: при обновлении
   прежняя программа ещё до полутора минут ждёт знака от новой, и её окна
   висели на экране и на панели задач, не отвечая, — открывалась «прошлая
   сессия» (2026.09.23.74). */
static HWND g_closeList[64];
static int g_closeN;

static BOOL CALLBACK close_collect(HWND w, LPARAM lp) {
  wchar_t cls[32];
  if (w == (HWND)lp || !GetClassNameW(w, cls, 32)) return TRUE;
  if (!wcscmp(cls, L"IME") || !wcsncmp(cls, L"MSCTFIME", 8)) return TRUE; /* служебные окна ввода — не наши */
  if (g_closeN < 64) g_closeList[g_closeN++] = w;
  return TRUE;
}

static void close_other_windows(HWND main) {
  g_closeN = 0;
  EnumThreadWindows(GetCurrentThreadId(), close_collect, (LPARAM)main);
  for (int i = 0; i < g_closeN; i++)
    if (IsWindow(g_closeList[i])) ShowWindow(g_closeList[i], SW_HIDE);
  for (int i = 0; i < g_closeN; i++)
    if (IsWindow(g_closeList[i])) DestroyWindow(g_closeList[i]);
}

static void enable_dpi(void) {
  HMODULE user32 = LoadLibraryW(L"user32.dll");
  if (!user32) return;
  typedef BOOL(WINAPI *SetProcessDPIAwareFn)(void);
  typedef BOOL(WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
  SetProcessDpiAwarenessContextFn ctx =
      (SetProcessDpiAwarenessContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
  if (ctx) {
    ctx((HANDLE)(intptr_t)-4); /* PER_MONITOR_AWARE_V2 */
  } else {
    SetProcessDPIAwareFn fn =
        (SetProcessDPIAwareFn)GetProcAddress(user32, "SetProcessDPIAware");
    if (fn) fn();
  }
  FreeLibrary(user32);
}

#ifdef VISUAL_TEST
#include "visual_test.c"
#endif
#ifdef QWEN_TEST
#include "qwen_test.c"
#endif
#ifdef CVX_LAB
#include "convert_workbench_test.c"
#endif
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
#ifdef CVX_LAB
  return cvx_lab(inst);
#endif
#ifdef QWEN_TEST
  return aq_test();
#endif
#ifdef VISUAL_TEST
  { int n; LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &n);
    if (args && n == 3 && !wcscmp(args[1], L"--render")) { int code = visual_test(args[2]); LocalFree(args); return code; }
    if (args) LocalFree(args);
    return 2;
  }
#endif
#ifdef AGENT_LAB
  {
    int n = 0; LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &n);
    if (args && n >= 3 && !wcscmp(args[1], L"--agent-lab")) {
      int code = ag_lab(inst, args[2]); LocalFree(args); return code;
    }
    if (args) LocalFree(args);
  }
#endif
  (void)prev;
  (void)cmd;
  (void)show;
  g_inst = inst;
  SetUnhandledExceptionFilter(on_crash);
  {
    /* одноразовый исполнитель запроса коллеги (share.c): без окна и без
       проверки «уже запущено» — основная программа как раз запущена */
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 4 && wcscmp(argv[1], L"--plm-serve") == 0) {
      int rc = share_serve_child(argv[2], argv[3]);
      LocalFree(argv);
      return rc;
    }
#ifdef SET_TEST /* кадры помощника полосой — без окон, поэтому до проверки «уже запущено»:
                   иначе проверочная сборка будит уже работающий CursorPad */
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--avatarshot")) ac_avatar_shot(argv[2]);
#endif
#ifdef TESS_SELFTEST /* только проверочная сборка: пакет и чтение Tesseract без окна */
    if (argv && argc >= 6 && wcscmp(argv[1], L"--tess-selftest") == 0) {
      /* <папка данных> <общая папка> <картинка.bmp> <отчёт.txt> */
      lstrcpynW(g_dataDir, argv[2], MAX_PATH);
      lstrcpynW(g_shareRoot, argv[3], MAX_PATH);
      BOOL synced = tess_pack_sync(TRUE);
      wchar_t helper[MAX_PATH];
      exe_dir(helper, MAX_PATH);
      wcsncat(helper, L"\\CursorPadOcr.exe", MAX_PATH - wcslen(helper) - 1);
      wchar_t *t = tess_ocr(argv[4], helper);
      wchar_t *rep = (wchar_t *)calloc(16000, sizeof(wchar_t));
      _snwprintf(rep, 16000, L"%s\r\n\r\nsync=%d note=%s\r\n--- OCR ---\r\n%s\r\n", g_updLog, synced,
                 g_tessNote, t ? t : L"(NULL)");
      char *u = (char *)malloc(64000);
      int k = WideCharToMultiByte(CP_UTF8, 0, rep, -1, u, 64000, NULL, NULL);
      write_all(argv[5], u, k > 0 ? (DWORD)k - 1 : 0);
      LocalFree(argv);
      return t ? 0 : 1;
    }
#endif
    if (argv) LocalFree(argv);
  }
  clear_runas_layer();
#ifdef UPD_CRASH /* только проверочная сборка: «новая версия, которая падает при запуске» */
  *(volatile int *)0 = 1;
#endif
  if (!ensure_single_instance()) return 0;
  notes_path(); /* папка данных — для проверки ниже */
  if (upd_trial_on_start()) { /* новая версия трижды не проработала и 45 секунд — вернули прежнюю */
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    if (g_mutex) CloseHandle(g_mutex);
    g_mutex = NULL;
    launch_open(self);
    return 0;
  }
  enable_dpi();
  aq_warm_start(); /* Prepare update-delivered agent/skills without blocking UI. */

  INITCOMMONCONTROLSEX icc;
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
  InitCommonControlsEx(&icc);

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(COL_PAPER);
  wc.lpszClassName = L"CursorPadWindow";
  wc.hIcon = app_icon(FALSE);
  wc.hIconSm = app_icon(TRUE);
  if (!RegisterClassExW(&wc)) return 1;

  POINT p;
  GetCursorPos(&p);
  int x = p.x + 22;
  int y = p.y + 28;
  g_x = x;
  g_y = y;

  HWND hwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
      L"CursorPadWindow", L"CursorPad",
      WS_POPUP | WS_CLIPCHILDREN,
      x, y, WND_W, WND_H,
      NULL, NULL, inst, NULL);
  if (!hwnd) return 1;

  apply_dpi(hwnd);
  SetWindowPos(hwnd, HWND_TOPMOST, (int)g_x, (int)g_y, g_ww, g_hh, SWP_NOACTIVATE);
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd);
#ifdef UPD_TEST /* только проверочная сборка: --upd-apply файл версия — поставить как обновление */
  {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--upd-apply")) {
      g_updRemoteNum = wcstol(argv[3], NULL, 10);
      apply_update(argv[2]);
    }
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--upd-sha")) {
      char h[65];
      g_updSha[0] = 0;
      upd_file_sha256(argv[2], h);
      wchar_t p[MAX_PATH];
      upd_data_file(p, L"sha-test.txt");
      write_all(p, h, (DWORD)strlen(h));
    }
    /* --upd-verify version.txt отчёт — разобрать version.txt: отпечаток и подпись выпуска */
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--upd-verify")) {
      char *t = NULL, line[160] = "unreadable";
      DWORD n = 0;
      if (read_file_bytes(argv[2], &t, &n, 256 * 1024)) {
        upd_parse_extra(t);
        snprintf(line, sizeof(line), "sha=%s sig=%d", g_updSha[0] ? g_updSha : "-", g_updSigOk);
        free(t);
      }
      write_all(argv[3], line, (DWORD)strlen(line));
      ExitProcess(0);
    }
    /* --tess-share данные общая отчёт — поставить Tesseract только из общей папки */
    if (argv && argc >= 5 && !wcscmp(argv[1], L"--tess-share")) {
      lstrcpynW(g_dataDir, argv[2], MAX_PATH);
      lstrcpynW(g_shareRoot, argv[3], MAX_PATH);
      g_updLog[0] = 0;
      BOOL synced = tess_pack_sync(FALSE);
      wchar_t dir[MAX_PATH];
      BOOL trusted = tess_find(dir);
      wchar_t rep[8000];
      _snwprintf(rep, 8000, L"sync=%d trusted=%d\r\n%s", synced, trusted, g_updLog);
      rep[7999] = 0;
      char u[24000];
      int k = WideCharToMultiByte(CP_UTF8, 0, rep, -1, u, sizeof(u), NULL, NULL);
      write_all(argv[4], u, k > 0 ? (DWORD)k - 1 : 0);
      ExitProcess(0);
    }
    if (argv) LocalFree(argv);
  }
#endif
#ifdef SET_TEST /* только проверочная сборка: --settings разделы [кнопка [прокрутка]] — открыть
                   Настройки с этими раскрытыми разделами (битами), навести мышь на кнопку с этим id */
  {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--gallery")) st_gallery(argv[2]);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--slash")) st_slash_strip(argv[2]);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--slashbench")) st_slash_bench(argv[2]);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--idlefight")) if_test_shots(argv[2]);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--xptest")) export_test(argv[2]);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--drawrec")) dr_test(argc, argv);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--drspecjson")) dr_spec_json_test(argv[2], argv[3]);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--pkread")) pks_test(argc, argv); /* путь окна «ЭСИ и ТП» без окна */
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--rename-by-stamp")) rs_test(argc, argv);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--fqread")) fq_test(argc, argv);
    if (argv && argc >= 4 && (!wcscmp(argv[1], L"--linktest") || !wcscmp(argv[1], L"--foldertest"))) flk_test(argc, argv);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--drawui")) dr_ui_test(argc, argv);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--techno")) dr_techno_test(argc, argv);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--tpchat")) ac_tp_test(argv[2], argv[3]);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--drawsort")) drs_test(argc, argv);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--mttest")) match_test(argv[2], argv[3], argc >= 5 ? argv[4] : NULL);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--idlebench")) if_test_bench(argv[2]);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--idlelog")) if_test_hplog(argv[2]);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--idlestage")) if_test_stage(argv[2], 0);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--idlestage3")) if_test_stage(argv[2], 1);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--figsheet")) if_test_figs(argv[2]);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--figbench")) if_test_figbench(argv[2]);
    if (argv && argc >= 2 && !wcscmp(argv[1], L"--idledemo")) if_launch(2); /* бой без остановки по вводу */
    if (argv && argc >= 2 && !wcscmp(argv[1], L"--idlewatch")) set_idle_watch(1); /* режим просмотра, как кнопкой */
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--aitest")) ai_selftest(argv[2], argc >= 4 ? argv[3] : NULL);
    if (argv && argc >= 5 && !wcscmp(argv[1], L"--aifiles")) ai_files_test(argc, argv);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--airoute")) ai_route_test(argv);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--aidl")) ai_dltest(argv[2], (int)wcstol(argv[3], NULL, 10), argc >= 5);
    if (argv && argc >= 4 && !wcscmp(argv[1], L"--aistart")) ai_starttest(argv[2], argv[3]);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--panel")) { /* проверка шапок окон: 1 карточка, 2 текст, 3 выгрузка, 4 маршрут, 5 нарды, 6 шашки, 7 пакет, 8 морской бой, 9 шахматы */
      int n = (int)wcstol(argv[2], NULL, 10);
      if (n >= 5 && g_follow) toggle_follow(); /* игры: окошко не бегает за мышью и не закрывает доску */
      if (n == 1) { create_card(NULL); show_card_text(L"проверка"); }
      if (n == 2) { create_ocr(NULL); show_ocr_text(L"проверка"); }
      if (n == 3) export_show();
      if (n == 4) route_show();
      if (n == 5) nardy_show();
      if (n == 6) shashki_show();
      if (n == 7) package_show(FALSE), pk_stamp_test_drop(); /* CP_PKDROP — бросок чертежей без мыши */
      if (n == 8) morskoy_show();
      if (n == 9) shahmaty_show();
    }
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--settings")) {
      g_tab = (int)wcstol(argv[2], NULL, 10) % SEC_COUNT;
      if (g_follow) toggle_follow(); /* как «Настройки» в меню значка */
      toggle_settings();
      if (argc >= 5) scroll_settings((int)wcstol(argv[4], NULL, 10));
      HWND b = argc >= 4 ? GetDlgItem(g_setHwnd, (int)wcstol(argv[3], NULL, 10)) : NULL;
      if (b) {
        RECT r;
        GetWindowRect(b, &r);
        SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
      }
    }
    if (argv) LocalFree(argv);
  }
#endif

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    /* Esc закрывает Настройки, «Карточку» и «Распознанный текст», где бы ни стоял фокус */
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
      HWND root = GetAncestor(msg.hwnd, GA_ROOT);
      if (root && (root == g_setHwnd || root == g_card || root == g_ocrWnd) && IsWindowVisible(root)) {
        ShowWindow(root, SW_HIDE);
        continue;
      }
    }
    if (g_cvxWnd && IsWindowVisible(g_cvxWnd) && IsDialogMessageW(g_cvxWnd, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  finish_update_launch();
  return (int)msg.wParam;
}
