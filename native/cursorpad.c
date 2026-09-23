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
#define ID_MIN 106
#define ID_OCR 107
#define ID_ALPHA_BG 108
#define ID_ALPHA_FG 109
#define ID_SETTINGS 110
#define ID_SYS_CUR 111
#define ID_SEARCH_EDIT 112
#define ID_SEARCH_GO 113
#define ID_ASK_TAB 114
#define ID_CLIP 131
#define ID_CLIPCLR 153
#define ID_ANSPIN 154
#define TIMER_FOLLOW 1
#define TIMER_SAVE 2
#define TIMER_PASTE 3
#define TIMER_STATUS 4
#define TIMER_FILES_PLAN 10 /* раз в минуту: не пора ли полный обход папки */
#define ID_FILES_NOTIFY 157
#define ID_WATCH_EDIT 158
#define ID_WATCH_BROWSE 159
#define ID_SHARE_EDIT 155
#define ID_SHARE_BROWSE 156
#define ID_SHARE_SERVE 169
#define HOTKEY_TOGGLE 1
#define HOTKEY_SNIP_BASE 10
#define HOTKEY_CURSOR 2
#define HOTKEY_OCR 3
#define HOTKEY_MIN 4
#define HOTKEY_SEARCH 5
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
#define WND_H 228
#define TITLE_H 44
#define FOOT_H 36
#define CLIP_H 26
#define PAD 12
#define GUTTER 26
#define SET_W 312
#define SET_H 935  /* темы в два ряда: четыре и рыцарская под ними */
#define ASK_W 312
#define ASK_H 224 /* room for the drawn header */
#define ID_THEME_BASE 140
#define THEME_COUNT 5

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
       заголовки прописными — как основная надпись */
    {RGB(255, 255, 255), RGB(244, 245, 247), RGB(20, 24, 31), RGB(100, 108, 120), RGB(30, 58, 95),
     L"Чертёж", L"Bahnschrift", L"Segoe UI", L"Bahnschrift", L"Bahnschrift", 0, BTN_LINE,
     HEAD_RULE, 1, TRUE},
    /* рыцарская: пергамент с фактурой, алые знамёна с золотом, стальные
       пластины вместо кнопок, готический шрифт заголовков, щит у названия */
    {RGB(241, 228, 195), RGB(228, 211, 168), RGB(43, 27, 14), RGB(118, 88, 54), KN_CRIMSON,
     L"Рыцарская", L"Palatino Linotype", L"Georgia", L"Old English Text MT",
     L"Palatino Linotype", 2, BTN_KNIGHT, HEAD_BANNER, 1, FALSE, TRUE},
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
static HWND g_pin;
static HWND g_close;
static HWND g_btnK2;
static HWND g_btnK3;
static HWND g_min;
static HWND g_ocr;
static HWND g_btnUp;
static HWND g_btnTheme[THEME_COUNT];
static int g_theme = 0;
static HWND g_tbBg;
static HWND g_tbFg;
static HWND g_btnSet;
static HWND g_btnAsk;
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
static BOOL g_trayAdded = FALSE;
static BOOL g_hidden = FALSE;
/* Слежение за курсором опрашивалось 100 раз в секунду всегда — даже когда
   окно закреплено, спрятано или мышь просто стоит. На ноутбуке это заметно
   по батарее. Частота теперь подстраивается под то, что происходит. */
static UINT g_followMs = 10;
static int g_followIdle;
static POINT g_lastCur;

static void follow_rate(HWND h, UINT ms) {
  if (ms == g_followMs) return;
  g_followMs = ms;
  SetTimer(h, TIMER_FOLLOW, ms, NULL);
}
static double g_x, g_y;
static int g_ww = WND_W, g_hh = WND_H;
static int g_offx = 22, g_offy = 28;
static UINT g_hotkeyVk = VK_F8;
static wchar_t g_hotkeyName[32] = L"F8";
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
static int g_skin = 1; /* 0 system, 1 sword, 2 gauntlet */
static HCURSOR g_staticCur[2];
static HCURSOR g_ibeamCur[2];
static HCURSOR g_handCur[2];
static HCURSOR g_helpCur[2];
static HCURSOR g_noCur[2];
static HCURSOR g_crossCur[2];
static HCURSOR g_sizeweCur[2];
static HCURSOR g_sizensCur[2];
static HCURSOR g_nwseCur[2];
static HCURSOR g_neswCur[2];
static HCURSOR g_allCur[2];
static HCURSOR g_upCur[2];
static HCURSOR g_pinCur[2];
static HCURSOR g_personCur[2];
static int g_alphaFollow = 180;
static int g_alphaPinned = 250;
static BOOL g_cursorOn = FALSE;
static int g_engine = 3; /* 0 wiki, 1 ddg, 2 yandex, 3 mini-ai, 4 plm, 5 files */
static wchar_t g_filesRoot[MAX_PATH];
static BOOL g_resultFiles = FALSE;
static wchar_t g_plmHost[96] = L"um-splmsrv";
static wchar_t g_sqlHost[96] = L"UM-SQLSRV";
static wchar_t g_plmPort[16] = L"4450";
static wchar_t g_plmDatabase[96] = L"";
static wchar_t g_sqlUser[96] = L"";
static wchar_t g_sqlPass[128] = L"";
/* общая папка отдела: через неё коллеги без логина получают PLM (share.c) */
static wchar_t g_shareRoot[MAX_PATH];
static BOOL g_shareServe; /* раздавать PLM коллегам через этот компьютер */
static HWND g_shareEdit, g_chkServe;
#define PLM_ROWS 200  /* result rows held for the list view */
#define PLM_LINK 4096 /* сетевые пути бывают длиннее тысячи знаков */
#define PLM_COL1 260
#define PLM_COL2 640
static wchar_t g_plmLastLink[PLM_LINK];
static wchar_t g_plmLinks[PLM_ROWS][PLM_LINK];
static wchar_t g_plmEsi[PLM_ROWS][PLM_COL1];
static wchar_t g_plmTp[PLM_ROWS][PLM_COL2];
static long g_plmIds[PLM_ROWS];  /* InfoObjectId каждой строки — для карточки */
static int g_plmTmpl[PLM_ROWS];  /* шаблон строки: по нему видно, где ТП */
static long g_plmTpId[PLM_ROWS];  /* id техпроцесса, сведённого в эту строку */
static long g_plmRealId[PLM_ROWS]; /* собственный id строки: у ТП он свой, а открывается родитель */
static int g_plmCount = 0;
static BOOL g_autostart = FALSE;
static void show_status(const wchar_t *text);
static void toggle_settings(void);
static void save_cursor_pref(void);
static void set_skin(int skin);
static void start_lookup(const wchar_t *q);
static BOOL clipboard_text(wchar_t *out, int n);
static void search_web(const wchar_t *q);
static void start_ocr_pick(void);
static void mark_cursor_dirty(BOOL on);
static void restore_if_stale_lock(void);
static void apply_scheme_slots(void);
static void update_engine_buttons(void);
static BOOL ensure_single_instance(void);
static LONG WINAPI on_crash(EXCEPTION_POINTERS *ex);
static BOOL autostart_get(void);
static void autostart_set(BOOL on);
static void create_answer(HWND owner);
static void create_card(HWND owner);
static void create_ocr(HWND owner);
static void show_ocr_text(const wchar_t *text);
static void show_answer_text(const wchar_t *text);

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

static void apply_scheme_slots(void) {
  if (g_skin <= 0 || g_skin > 2) return;
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
  const wchar_t *wait = s == 1 ? L"k3_wait.ani" : L"k2_wait.ani";
  const wchar_t *app = s == 1 ? L"k3_app.ani" : L"k2_app.ani";
  HCURSOR wait1 = load_cur_file(wait);
  if (!wait1) wait1 = load_cur_file(s == 1 ? L"cursors\\k3_wait.ani" : L"cursors\\k2_wait.ani");
  if (wait1) SetSystemCursor(wait1, OCR_WAIT_ID);
  HCURSOR app1 = load_cur_file(app);
  if (!app1) app1 = load_cur_file(s == 1 ? L"cursors\\k3_app.ani" : L"cursors\\k2_app.ani");
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

static void apply_theme(void) {
  if (g_theme < 0 || g_theme >= THEME_COUNT) g_theme = 0;
  COL_PAPER = kThemes[g_theme].paper;
  COL_PAPER_DARK = kThemes[g_theme].dark;
  COL_INK = kThemes[g_theme].ink;
  COL_MUTED = kThemes[g_theme].muted;
  COL_SAGE = kThemes[g_theme].sage;
  COL_LINE = blend_rgb(COL_INK, COL_PAPER, 224);
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
  if (g_setHwnd) layout_settings();
  if (g_askHwnd) layout_ask();
  if (g_answer) layout_answer();
  if (g_card) layout_card();
  if (g_ocrWnd) layout_ocr();
  theme_fit_windows();
  for (int i = 0; i < 6; i++)
    if (tops[i]) RedrawWindow(tops[i], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static void set_theme(int t) {
  if (t < 0 || t >= THEME_COUNT) return;
  g_theme = t;
  apply_theme();
  save_cursor_pref();
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
  char buf[320];
  DWORD n = 0;
  ReadFile(h, buf, 319, &n, NULL);
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
  sscanf(buf, "%15s %d %d %15s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", skin, &bg,
         &fg, eng, &autoOn, &theme, &aw, &ah, &cw, &ch, &pt, &keep, &ax, &ay, &cx, &cy, &cpt, &ox,
         &oy, &ow, &oh, &opt);
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
  if (skin[0] == 'k' && skin[1] == '3') g_skin = 2;
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
}

static void save_cursor_pref(void) {
  const char *v = g_skin == 2 ? "k3" : (g_skin == 0 ? "system" : "k2");
  const char *e = g_engine == 4 ? "plm" : (g_engine == 5 ? "files" : "ai");
  char buf[280];
  snprintf(buf, sizeof(buf), "%s %d %d %s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
           v, g_alphaFollow, g_alphaPinned, e, g_autostart ? 1 : 0, g_theme, g_ansW, g_ansH,
           g_cardW, g_cardH, g_ansPt, g_ansKeepPos ? 1 : 0, g_ansX, g_ansY, g_cardX, g_cardY,
           g_cardPt, g_ocrX, g_ocrY, g_ocrW, g_ocrH, g_ocrPt);
  HANDLE h = CreateFileW(g_prefPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(h, buf, (DWORD)strlen(buf), &w, NULL);
  CloseHandle(h);
}

static void update_cursor_buttons(void) {
  if (g_btnK2)
    SetWindowTextW(g_btnK2, g_skin == 1 ? L"● Мечник" : L"Мечник");
  if (g_btnK3)
    SetWindowTextW(g_btnK3, g_skin == 2 ? L"● Рукавица" : L"Рукавица");
}

static void set_skin(int skin) {
  g_skin = skin;
  save_cursor_pref();
  update_cursor_buttons();
  if (skin == 0) restore_system_cursor();
  else install_scheme_cursors();
  _snwprintf(g_status, 160, L"Курсор: %s",
             skin == 2 ? L"Рукавица" : (skin == 0 ? L"Windows" : L"Мечник"));
  g_statusOn = TRUE;
  if (g_hwnd) {
    SetTimer(g_hwnd, TIMER_STATUS, 1600, NULL);
    InvalidateRect(g_hwnd, NULL, FALSE);
  }
}

static void cycle_skin(void) {
  int next = g_skin + 1;
  if (next > 2) next = 0;
  set_skin(next);
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
}

static void free_one(HCURSOR *c) {
  if (*c) {
    DestroyCursor(*c);
    *c = NULL;
  }
}

static void free_cursor_frames(void) {
  restore_system_cursor();
  for (int s = 0; s < 2; s++) {
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
  MoveWindow(g_pin, cw - pad - closeW * 2 - gap * 2 - pinW, (th - btnH) / 2, pinW, btnH, TRUE);

  int clipH = MulDiv(CLIP_H, dpi, 96);
  int clrW = MulDiv(56, dpi, 96);
  if (g_clipEdit) MoveWindow(g_clipEdit, gut, th, cw - gut - pad - clrW - gap, clipH, TRUE);
  if (g_clipClr) MoveWindow(g_clipClr, cw - pad - clrW, th, clrW, clipH, TRUE);
  MoveWindow(g_edit, gut, th + clipH, cw - gut - pad, ch - th - clipH - fh, TRUE);
  int half = (cw - pad * 2 - gap) / 2;
  int fy = ch - fh + (fh - btnH) / 2;
  if (g_btnAsk) MoveWindow(g_btnAsk, pad, fy, half, btnH, TRUE);
  if (g_btnSet) MoveWindow(g_btnSet, pad + half + gap, fy, half, btnH, TRUE);
}

static void round_corners(HWND hwnd) {
  HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (!dwm) return;
  typedef HRESULT(WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
  DwmSetWindowAttributeFn fn =
      (DwmSetWindowAttributeFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
  if (fn) {
    /* углы окна берутся из темы: у «Чертежа» прямые, у остальных круглые */
    int pref = (g_theme >= 0 && g_theme < THEME_COUNT) ? kThemes[g_theme].corners : 2;
    fn(hwnd, 33, &pref, sizeof(pref));
  }
  FreeLibrary(dwm);
}

static void apply_follow_state(void) {
  set_clickthrough(g_follow);
  apply_alpha_now();
  EnableWindow(g_edit, !g_follow);
  if (g_clipEdit) EnableWindow(g_clipEdit, !g_follow);
  if (g_clipClr) EnableWindow(g_clipClr, !g_follow);
  EnableWindow(g_pin, !g_follow);
  EnableWindow(g_close, !g_follow);
  EnableWindow(g_min, !g_follow);
  EnableWindow(g_btnSet, !g_follow);
  if (g_btnAsk) EnableWindow(g_btnAsk, !g_follow);
  if (g_searchEdit) EnableWindow(g_searchEdit, !g_follow);
  if (g_searchGo) EnableWindow(g_searchGo, !g_follow);
  if (g_ocr) EnableWindow(g_ocr, !g_follow);
  if (g_btnUp) EnableWindow(g_btnUp, !g_follow);
  for (int i = 0; i < THEME_COUNT; i++)
    if (g_btnTheme[i]) EnableWindow(g_btnTheme[i], !g_follow);
  if (g_btnK2) EnableWindow(g_btnK2, !g_follow);
  if (g_btnK3) EnableWindow(g_btnK3, !g_follow);
  if (g_tbBg) EnableWindow(g_tbBg, !g_follow);
  if (g_tbFg) EnableWindow(g_tbFg, !g_follow);
  if (g_btnSys) EnableWindow(g_btnSys, !g_follow);
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

static const UINT kHotkeys[] = {VK_F8, VK_PAUSE, VK_SCROLL};
static const wchar_t *kHotkeyNames[] = {L"F8", L"Pause", L"Scroll Lock"};

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
  if (g_clipEdit) SetWindowTextW(g_clipEdit, q);
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
  show_status(L"Свёрнуто · F9");
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
static void draw_panel_header(HWND hwnd, HDC hdc, const wchar_t *title);
static LRESULT panel_hittest(HWND hwnd, LPARAM lParam);
static void place_panel_close(HWND hwnd);
static void round_corners(HWND hwnd);

#include "pad_extra.c"

static void fill_round_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF border, int rad) {
  int d = rad * 2;
  if (d < 2) d = 2;
  HBRUSH br = CreateSolidBrush(fill);
  HPEN pn = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ obr = SelectObject(hdc, br);
  HGDIOBJ opn = SelectObject(hdc, pn);
  RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, d, d);
  SelectObject(hdc, obr);
  SelectObject(hdc, opn);
  DeleteObject(br);
  DeleteObject(pn);
}

static void draw_panel_header(HWND hwnd, HDC hdc, const wchar_t *title) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  const PadTheme *th = &kThemes[g_theme];
  if (th->head == HEAD_BANNER) {
    /* рамка по краю окна и знамя в шапке; если справа есть крестик,
       знамя кончается до него */
    draw_knight_frame(hdc, rc);
    int right = GetDlgItem(hwnd, ID_PANEL_CLOSE) ? rc.right - 46 : rc.right - 14;
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

static void draw_pad_button(const DRAWITEMSTRUCT *dis) {
  if (!dis || dis->CtlType != ODT_BUTTON) return;
  RECT rc = dis->rcItem;
  BOOL press = (dis->itemState & ODS_SELECTED) != 0;
  BOOL disab = (dis->itemState & ODS_DISABLED) != 0;
  int id = (int)dis->CtlID;
  wchar_t t[96] = {0};
  GetWindowTextW(dis->hwndItem, t, 96);
  BOOL themeBtn = id >= ID_THEME_BASE && id < ID_THEME_BASE + THEME_COUNT;
  int ti = themeBtn ? id - ID_THEME_BASE : -1;
  BOOL on = t[0] == 0x25CF || (themeBtn && g_theme == ti);
  BOOL primary = id == ID_SETTINGS || id == ID_SEARCH_GO || id == ID_UPDATE || id == ID_PIN ||
                 id == ID_ANS_OPEN || id == ID_ANS_OPENTP || (id == ID_ASK_TAB && g_askOpen);
  BOOL quiet = id == ID_CLOSE || id == ID_MIN || id == ID_PANEL_CLOSE;
  COLORREF fill, fg, bd;
  /* рыцарская тема рисует кнопки целиком по-своему; кнопка выбора
     этой темы в Настройках — тоже, чтобы было видно, что получится */
  if ((themeBtn && ti >= 0 && kThemes[ti].btn == BTN_KNIGHT) ||
      (!themeBtn && kThemes[g_theme].btn == BTN_KNIGHT)) {
    const wchar_t *kl = themeBtn ? kThemes[ti].name
                                 : ((t[0] == 0x25CF && t[1] == L' ') ? t + 2 : t);
    draw_knight_button(dis->hwndItem, dis->hDC, rc, kl, press, disab, on, primary || themeBtn,
                       quiet);
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
  int h = rc.bottom - rc.top;
  /* скругление задаёт тема; кнопки выбора темы рисуются формой своей
     темы — сразу видно, что получится */
  int tr = kThemes[themeBtn && ti >= 0 ? ti : g_theme].radius;
  int rad;
  if (tr < 0) rad = h / 2;
  else if (tr == 0) rad = 0;
  else rad = quiet ? (h / 2) : (h >= 26 ? tr : (tr > 2 ? tr - 2 : tr));
  fill_round_rect(dis->hDC, rc, fill, bd, rad);
  SetBkMode(dis->hDC, TRANSPARENT);
  SetTextColor(dis->hDC, fg);
  if (g_fontUi) SelectObject(dis->hDC, g_fontUi);
  const wchar_t *label = t;
  if (themeBtn) label = kThemes[ti].name;
  else if (t[0] == 0x25CF && t[1] == L' ') label = t + 2;
  DrawTextW(dis->hDC, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static HWND mk_btn(HWND parent, const wchar_t *text, int id) {
  HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 80, 26,
                           parent, (HMENU)(INT_PTR)id, NULL, NULL);
  if (b && g_fontUi) SendMessageW(b, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
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
  /* «Сброс» заодно останавливает перенос в 1С — отдельная кнопка не нужна */
  onec_stop(NULL);
  show_status(ok ? L"Буфер очищен" : L"Буфер занят другой программой");
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
  if (img) _snwprintf(msg, 160, L"Ctrl+%d  ·  картинка", n);
  else _snwprintf(msg, 160, L"Ctrl+%d  ·  в буфере", n);
  show_status(msg);
  if (!is_our_foreground()) {
    SetTimer(g_hwnd, TIMER_PASTE, 40, NULL);
  }
  free(line);
}

static void register_snip_hotkeys(HWND hwnd) {
  for (int i = 0; i < SNIP_COUNT; i++) {
    RegisterHotKey(hwnd, HOTKEY_SNIP_BASE + i, MOD_CONTROL | MOD_NOREPEAT, (UINT)('1' + i));
  }
}

static void unregister_snip_hotkeys(HWND hwnd) {
  for (int i = 0; i < SNIP_COUNT; i++) {
    UnregisterHotKey(hwnd, HOTKEY_SNIP_BASE + i);
  }
}

static BOOL register_toggle_hotkey(HWND hwnd) {
  for (int i = 0; i < 3; i++) {
    if (RegisterHotKey(hwnd, HOTKEY_TOGGLE, MOD_NOREPEAT, kHotkeys[i])) {
      g_hotkeyVk = kHotkeys[i];
      lstrcpynW(g_hotkeyName, kHotkeyNames[i], 32);
      return TRUE;
    }
  }
  return FALSE;
}

static void add_tray(HWND hwnd) {
  memset(&g_nid, 0, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = TRAY_UID;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAY;
  g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  _snwprintf(g_nid.szTip, 128, L"CursorPad %s  ·  %s закрепить", APP_VERSION_STR, g_hotkeyName);
  g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void tray_menu(HWND hwnd) {
  POINT pt;
  GetCursorPos(&pt);
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1, g_follow ? L"Закрепить" : L"Следовать за курсором");
  AppendMenuW(menu, MF_STRING, 14, g_hidden ? L"Показать окно (F9)" : L"Свернуть (F9)");
  AppendMenuW(menu, MF_STRING, 16, L"Настройки");
  AppendMenuW(menu, MF_STRING, 15, L"Выделить и прочитать (F6)");
  AppendMenuW(menu, MF_STRING, 17, L"Обновить с GitHub");
  AppendMenuW(menu, MF_STRING, 18, L"Что нового в папке");
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuW(menu, MF_STRING | (g_skin == 1 ? MF_CHECKED : 0), 10, L"Курсор: Мечник");
  AppendMenuW(menu, MF_STRING | (g_skin == 2 ? MF_CHECKED : 0), 11, L"Курсор: Рукавица");
  AppendMenuW(menu, MF_STRING | (g_skin == 0 ? MF_CHECKED : 0), 12, L"Курсор: обычный Windows");
  AppendMenuW(menu, MF_STRING, 13, L"Следующий курсор (F7)");
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
  } else if (cmd == 10) set_skin(1);
  else if (cmd == 11) set_skin(2);
  else if (cmd == 12) set_skin(0);
  else if (cmd == 13) cycle_skin();
  else if (cmd == 14) {
    if (g_hidden) restore_from_tray();
    else hide_to_tray();
  } else if (cmd == 15) run_ocr_test();
  else if (cmd == 17) start_update();
  else if (cmd == 18) files_show_changes();
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
   правки. Теперь они обведены и пронумерованы прямо в тексте. */
static void draw_slot_marks(HWND ed) {
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
  int gutter = 16;
  HPEN pen = CreatePen(PS_SOLID, 1, blend_rgb(COL_SAGE, COL_PAPER, 205));
  HGDIOBJ prevPen = SelectObject(dc, pen);
  HGDIOBJ prevBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  int prevBk = SetBkMode(dc, TRANSPARENT);
  COLORREF prevCol = SetTextColor(dc, blend_rgb(COL_SAGE, COL_PAPER, 140));

  int slot = 0, i = 0;
  while (i < len && slot < 9) {
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
    /* только целиком видимые: обрезанная снизу рамка налезает на кнопки */
    if (y1 < 0 || y2 + lh + 2 > cl.bottom) continue;
    int right;
    if (y1 == y2) {
      SIZE sz;
      sz.cx = 0;
      GetTextExtentPoint32W(dc, buf + start, end - start, &sz);
      right = x1 + sz.cx + 4;
    } else {
      right = cl.right - gutter - 4; /* строка перенеслась — рамка во всю ширину */
    }
    if (right > cl.right - gutter - 4) right = cl.right - gutter - 4;
    if (right <= x1 + 6) right = x1 + 6;
    RoundRect(dc, x1 - 4, y1 - 1, right, y2 + lh + 1, 7, 7);
    wchar_t d[4];
    _snwprintf(d, 4, L"%d", slot);
    RECT nr;
    nr.left = cl.right - gutter;
    nr.right = cl.right - 2;
    nr.top = y1 - 1;
    nr.bottom = y1 + lh;
    DrawTextW(dc, d, -1, &nr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  }

  SetTextColor(dc, prevCol);
  SetBkMode(dc, prevBk);
  SelectObject(dc, prevBrush);
  SelectObject(dc, prevPen);
  DeleteObject(pen);
  if (prevFont) SelectObject(dc, prevFont);
  ReleaseDC(ed, dc);
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
  return CallWindowProcW(g_oldEdit, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    search_clipboard_or_edit();
    return 0;
  }
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
static BOOL pick_folder(HWND owner, wchar_t *out, int cap) {
  out[0] = 0;
  HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  BOOL ok = FALSE;
  IFileOpenDialog *dlg = NULL;
  if (SUCCEEDED(CoCreateInstance(&kCLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                 &kIID_FileOpenDialog, (void **)&dlg))) {
    DWORD opts = 0;
    dlg->lpVtbl->GetOptions(dlg, &opts);
    dlg->lpVtbl->SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                     FOS_PATHMUSTEXIST);
    dlg->lpVtbl->SetTitle(dlg, L"Папка для поиска файлов");
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
    bi.lpszTitle = L"Папка для поиска файлов";
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

static void layout_settings(void) {
  if (!g_setHwnd) return;
  RECT rc;
  GetClientRect(g_setHwnd, &rc);
  int pad = 14, gap = 7, btnH = 28, y = PANEL_TITLE_H + 26;
  int cw = rc.right - pad;
  int half = (cw - pad - gap) / 2;
  place_panel_close(g_setHwnd);
  {
    int browseW = 78;
    if (g_filesRootEdit)
      MoveWindow(g_filesRootEdit, pad, y, cw - pad - browseW - gap, btnH, TRUE);
    HWND br = GetDlgItem(g_setHwnd, ID_FILES_BROWSE);
    if (br) MoveWindow(br, cw - browseW, y, browseW, btnH, TRUE);
  }
  y += btnH + gap;
  if (g_btnIdx) MoveWindow(g_btnIdx, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap;
  if (g_filesStat) MoveWindow(g_filesStat, pad, y, cw - pad, btnH, TRUE);
  if (g_filesBar) MoveWindow(g_filesBar, pad, y + btnH - 8, cw - pad, 6, TRUE);
  y += btnH + gap;
  if (g_chkNotify) MoveWindow(g_chkNotify, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap;
  {
    int browseW = 78;
    if (g_filesWatchEdit)
      MoveWindow(g_filesWatchEdit, pad, y, cw - pad - browseW - gap, btnH, TRUE);
    HWND br = GetDlgItem(g_setHwnd, ID_WATCH_BROWSE);
    if (br) MoveWindow(br, cw - browseW, y, browseW, btnH, TRUE);
  }
  y += btnH + gap;
  if (g_plmServer) MoveWindow(g_plmServer, pad, y, half, btnH, TRUE);
  if (g_plmDb) MoveWindow(g_plmDb, pad + half + gap, y, half, btnH, TRUE);
  y += btnH + gap;
  if (g_plmUser) MoveWindow(g_plmUser, pad, y, half, btnH, TRUE);
  if (g_plmPass) MoveWindow(g_plmPass, pad + half + gap, y, half, btnH, TRUE);
  y += btnH + gap + 22;
  {
    int browseW = 78;
    if (g_shareEdit) MoveWindow(g_shareEdit, pad, y, cw - pad - browseW - gap, btnH, TRUE);
    HWND br = GetDlgItem(g_setHwnd, ID_SHARE_BROWSE);
    if (br) MoveWindow(br, cw - browseW, y, browseW, btnH, TRUE);
  }
  y += btnH + gap;
  if (g_chkServe) MoveWindow(g_chkServe, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap + 22;
  if (g_btnK2) MoveWindow(g_btnK2, pad, y, half, btnH, TRUE);
  if (g_btnK3) MoveWindow(g_btnK3, pad + half + gap, y, half, btnH, TRUE);
  y += btnH + gap;
  if (g_btnSys) MoveWindow(g_btnSys, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap;
  if (g_chkAuto) MoveWindow(g_chkAuto, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap;
  if (g_chkAnsPin) MoveWindow(g_chkAnsPin, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap + 22;
  if (g_tbBg) MoveWindow(g_tbBg, pad, y, cw - pad, btnH, TRUE);
  y += btnH + 24;
  if (g_tbFg) MoveWindow(g_tbFg, pad, y, cw - pad, btnH, TRUE);
  y += btnH + 22;
  if (g_ocr) MoveWindow(g_ocr, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap;
  if (g_btnUp) MoveWindow(g_btnUp, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap + 18;
  {
    int cols = 4;
    int tw = (cw - pad - gap * (cols - 1)) / cols;
    for (int i = 0; i < THEME_COUNT; i++) {
      if (!g_btnTheme[i]) continue;
      int row = i / cols;
      int col = i % cols;
      /* пятая, рыцарская, — отдельным рядом во всю ширину */
      int bw = (row > 0 && THEME_COUNT - cols == 1) ? cw - pad : tw;
      MoveWindow(g_btnTheme[i], pad + col * (tw + gap), y + row * (btnH + gap), bw, btnH, TRUE);
    }
  }
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, bg_brush(TRUE));
    draw_panel_header(hwnd, hdc, L"Настройки");
    SetBkMode(hdc, TRANSPARENT);
    if (g_fontSmall) SelectObject(hdc, g_fontSmall);
    SetTextColor(hdc, COL_MUTED);
    {
      RECT cap = {14, PANEL_TITLE_H + 6, rc.right - 14, PANEL_TITLE_H + 24};
      DrawTextW(hdc, L"PLM и файлы", -1, &cap, DT_LEFT | DT_SINGLELINE);
    }
    if (g_shareEdit) {
      RECT wr;
      GetWindowRect(g_shareEdit, &wr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&wr, 2);
      RECT a = {14, wr.top - 18, rc.right - 14, wr.top - 2};
      DrawTextW(hdc, L"общая папка — PLM для коллег", -1, &a, DT_LEFT | DT_SINGLELINE);
    }
    if (g_btnK2) {
      RECT wr;
      GetWindowRect(g_btnK2, &wr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&wr, 2);
      RECT a = {14, wr.top - 18, rc.right - 14, wr.top - 2};
      DrawTextW(hdc, L"курсор", -1, &a, DT_LEFT | DT_SINGLELINE);
    }
    if (g_tbBg) {
      RECT wr;
      GetWindowRect(g_tbBg, &wr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&wr, 2);
      RECT a = {14, wr.top - 18, rc.right - 14, wr.top - 2};
      DrawTextW(hdc, L"прозрачность следования", -1, &a, DT_LEFT | DT_SINGLELINE);
    }
    if (g_tbFg) {
      RECT wr;
      GetWindowRect(g_tbFg, &wr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&wr, 2);
      RECT b = {14, wr.top - 18, rc.right - 14, wr.top - 2};
      DrawTextW(hdc, L"прозрачность закреплённого окна", -1, &b, DT_LEFT | DT_SINGLELINE);
    }
    if (g_btnTheme[0]) {
      RECT wr;
      GetWindowRect(g_btnTheme[0], &wr);
      MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&wr, 2);
      RECT c = {14, wr.top - 18, rc.right - 14, wr.top - 2};
      DrawTextW(hdc, L"тема", -1, &c, DT_LEFT | DT_SINGLELINE);
    }
    {
      RECT ver = {14, rc.bottom - 28, rc.right - 14, rc.bottom - 10};
      wchar_t vs[64];
      _snwprintf(vs, 64, L"CursorPad  %s", APP_VERSION_STR);
      DrawTextW(hdc, vs, -1, &ver, DT_LEFT | DT_SINGLELINE);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_CTLCOLOREDIT: {
    /* fields read as filled surfaces against the panel, no sunken border */
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
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    }
    if (LOWORD(wParam) == ID_FILES_BROWSE) {
      wchar_t picked[MAX_PATH];
      if (pick_folder(hwnd, picked, MAX_PATH)) {
        if (g_filesRootEdit) SetWindowTextW(g_filesRootEdit, picked);
        files_apply_root(TRUE);
      }
      return 0;
    }
    if (LOWORD(wParam) == ID_CUR_K2) set_skin(1);
    if (LOWORD(wParam) == ID_CUR_K3) set_skin(2);
    if (LOWORD(wParam) == ID_SYS_CUR) set_skin(0);
    if (LOWORD(wParam) == ID_OCR) run_ocr_test();
    if (LOWORD(wParam) == ID_UPDATE) start_update();
    if (LOWORD(wParam) >= ID_THEME_BASE && LOWORD(wParam) < ID_THEME_BASE + THEME_COUNT)
      set_theme(LOWORD(wParam) - ID_THEME_BASE);
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
  place_popup(g_setHwnd, SET_W, SET_H, FALSE);
  layout_settings();
}

static void refresh_search_cue(void) {
  if (!g_searchEdit) return;
  const wchar_t *c = g_engine == 4 ? L"найти в PLM" :
                     (g_engine == 5 ? L"имя файла" : L"спросить мини-ИИ");
  SendMessageW(g_searchEdit, 0x1501, TRUE, (LPARAM)c);
}

static void layout_ask(void) {
  if (!g_askHwnd) return;
  RECT rc;
  GetClientRect(g_askHwnd, &rc);
  int pad = 14, gap = 7, btnH = 28, y = PANEL_TITLE_H + 12;
  int cw = rc.right - pad;
  int goW = 88;
  place_panel_close(g_askHwnd);
  if (g_searchEdit) MoveWindow(g_searchEdit, pad, y, cw - pad - goW - gap, btnH, TRUE);
  if (g_searchGo) MoveWindow(g_searchGo, cw - goW, y, goW, btnH, TRUE);
  y += btnH + gap + 20;
  if (g_btnAi) MoveWindow(g_btnAi, pad, y, cw - pad, btnH, TRUE);
  y += btnH + gap;
  int half = (cw - pad - gap) / 2;
  if (g_btnPlm) MoveWindow(g_btnPlm, pad, y, half, btnH, TRUE);
  if (g_btnFiles) MoveWindow(g_btnFiles, pad + half + gap, y, half, btnH, TRUE);
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
  if (g_btnAsk) SetWindowTextW(g_btnAsk, g_askOpen ? L"● Поиск" : L"Поиск");
  if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

static void toggle_ask(void);
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
      DrawTextW(hdc, L"Enter — спросить · F3 по буферу копии", -1, &hint,
                DT_LEFT | DT_SINGLELINE);
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
    if (LOWORD(wParam) == ID_SEARCH_GO) search_clipboard_or_edit();
    if (LOWORD(wParam) == ID_ENG_AI) {
      g_engine = 3;
      save_cursor_pref();
      update_engine_buttons();
      refresh_search_cue();
    }
    if (LOWORD(wParam) == ID_ENG_PLM) {
      g_engine = 4;
      save_cursor_pref();
      update_engine_buttons();
      refresh_search_cue();
    }
    if (LOWORD(wParam) == ID_ENG_FILES) {
      g_engine = 5;
      save_cursor_pref();
      files_apply_root(FALSE);
      update_engine_buttons();
      refresh_search_cue();
    }
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
  g_searchEdit = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 0, 0, 160, 26, g_askHwnd, (HMENU)(INT_PTR)ID_SEARCH_EDIT, NULL, NULL);
  g_searchGo = mk_btn(g_askHwnd, L"Спросить", ID_SEARCH_GO);
  g_btnAi = mk_btn(g_askHwnd, L"Мини-ИИ", ID_ENG_AI);
  g_btnPlm = mk_btn(g_askHwnd, L"PLM", ID_ENG_PLM);
  g_btnFiles = mk_btn(g_askHwnd, L"Файлы", ID_ENG_FILES);
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
      L"Настройки", WS_POPUP | WS_CLIPCHILDREN, 0, 0,
      SET_W, SET_H, owner, NULL, g_inst, NULL);
  mk_btn(g_setHwnd, L"×", ID_PANEL_CLOSE);
  round_corners(g_setHwnd);
  g_filesRootEdit = CreateWindowExW(0, L"EDIT", g_filesRoot,
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    0, 0, 200, 26, g_setHwnd, (HMENU)(INT_PTR)ID_FILES_ROOT, NULL, NULL);
  mk_btn(g_setHwnd, L"Обзор…", ID_FILES_BROWSE);
  g_btnIdx = mk_btn(g_setHwnd, L"Обновить JSON", ID_FILES_REFRESH);
  g_filesStat = CreateWindowExW(0, L"STATIC", L"Индекс: —",
                                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS, 0, 0, 200, 22, g_setHwnd,
                                (HMENU)(INT_PTR)136, NULL, NULL);
  g_filesBar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 0, 0, 200, 6,
                               g_setHwnd, (HMENU)(INT_PTR)137, NULL, NULL);
  g_chkNotify = CreateWindowExW(0, L"BUTTON", L"Сообщать о новых и изменённых файлах",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26,
                                g_setHwnd, (HMENU)(INT_PTR)ID_FILES_NOTIFY, NULL, NULL);
  g_filesWatchEdit = CreateWindowExW(0, L"EDIT", g_filesWatch,
                                     WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                     0, 0, 200, 26, g_setHwnd, (HMENU)(INT_PTR)ID_WATCH_EDIT, NULL, NULL);
  mk_btn(g_setHwnd, L"Обзор…", ID_WATCH_BROWSE);
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
  g_btnK2 = mk_btn(g_setHwnd, L"Мечник", ID_CUR_K2);
  g_btnK3 = mk_btn(g_setHwnd, L"Рукавица", ID_CUR_K3);
  g_btnSys = mk_btn(g_setHwnd, L"Курсор Windows", ID_SYS_CUR);
  g_chkAuto = CreateWindowExW(0, L"BUTTON", L"Автозапуск с Windows",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 200, 26,
                              g_setHwnd, (HMENU)(INT_PTR)ID_AUTOSTART, NULL, NULL);
  g_chkAnsPin = CreateWindowExW(0, L"BUTTON", L"Окно находок оставлять на месте",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 240, 26,
                                g_setHwnd, (HMENU)(INT_PTR)ID_ANSPIN, NULL, NULL);
  g_ocr = mk_btn(g_setHwnd, L"Выделить и прочитать (F6)", ID_OCR);
  g_btnUp = mk_btn(g_setHwnd, L"Обновить с GitHub", ID_UPDATE);
  for (int i = 0; i < THEME_COUNT; i++)
    g_btnTheme[i] = mk_btn(g_setHwnd, kThemes[i].name, ID_THEME_BASE + i);
  g_tbBg = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_TOOLTIPS,
                           0, 0, 120, 28, g_setHwnd, (HMENU)(INT_PTR)ID_ALPHA_BG, NULL, NULL);
  g_tbFg = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_TOOLTIPS,
                           0, 0, 120, 28, g_setHwnd, (HMENU)(INT_PTR)ID_ALPHA_FG, NULL, NULL);
  SendMessageW(g_tbBg, TBM_SETRANGEMIN, FALSE, 40);
  SendMessageW(g_tbBg, TBM_SETRANGEMAX, FALSE, 255);
  SendMessageW(g_tbBg, TBM_SETPOS, TRUE, g_alphaFollow);
  SendMessageW(g_tbFg, TBM_SETRANGEMIN, FALSE, 40);
  SendMessageW(g_tbFg, TBM_SETRANGEMAX, FALSE, 255);
  SendMessageW(g_tbFg, TBM_SETPOS, TRUE, g_alphaPinned);
  SendMessageW(g_btnK2, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnK3, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_btnSys, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  SendMessageW(g_ocr, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
  if (g_btnUp) SendMessageW(g_btnUp, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
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
  if (g_chkAnsPin)
    SendMessageW(g_chkAnsPin, BM_SETCHECK, g_ansKeepPos ? BST_CHECKED : BST_UNCHECKED, 0);
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
  watch_enable();
  update_engine_buttons();
  layout_settings();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE: {
    g_hwnd = hwnd;
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
    g_fontSmall = make_font(L"Segoe UI Variable Small", 8, FW_NORMAL);
    if (!g_fontSmall) g_fontSmall = make_font(L"Segoe UI", 8, FW_NORMAL);

    g_pin = mk_btn(hwnd, L"Закрепить", ID_PIN);
    g_clipClr = mk_btn(hwnd, L"Сброс", ID_CLIPCLR);
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
    g_btnSet = mk_btn(hwnd, L"Настройки", ID_SETTINGS);
    SendMessageW(g_pin, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    if (g_clipClr) SendMessageW(g_clipClr, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_close, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_min, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    if (g_clipEdit) {
      SendMessageW(g_clipEdit, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
      SendMessageW(g_clipEdit, 0x1501, TRUE, (LPARAM)L"буфер копии · F3");
    }
    SendMessageW(g_btnSet, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    if (g_btnAsk) SendMessageW(g_btnAsk, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g_edit, EM_SETLIMITTEXT, 200000, 0);
    g_oldEdit = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    notes_path();
    cleanup_old_bins();
    restore_if_stale_lock();
    extract_payloads();
    load_notes();
    load_cursor_pref();
    load_plm_pref();
    share_start();
    load_files_pref();
    if (g_autostart) autostart_set(TRUE);
    create_settings(hwnd);
    create_ask(hwnd);
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
    SetTimer(hwnd, TIMER_FOLLOW, 10, NULL);
    SetTimer(hwnd, TIMER_SAVE, 2000, NULL);
    SetTimer(hwnd, TIMER_CURSOR_KEEP, 4000, NULL);
    /* раз в пять минут спрашиваем, не исполнился ли индексу час: так час
       считается от последнего обхода, а не от запуска программы */
    SetTimer(hwnd, TIMER_FILES, 300000, NULL);
    SetTimer(hwnd, TIMER_FILES_PLAN, 60000, NULL);
    if (g_filesRoot[0]) files_start_index(FALSE);
    if (!register_toggle_hotkey(hwnd)) {
      MessageBoxW(hwnd,
                  L"Не удалось зарегистрировать горячую клавишу. "
                  L"Закрепляйте окно кнопкой, когда оно не следует за курсором.",
                  L"CursorPad", MB_OK | MB_ICONINFORMATION);
    }
    RegisterHotKey(hwnd, HOTKEY_CURSOR, MOD_NOREPEAT, VK_F7);
    RegisterHotKey(hwnd, HOTKEY_OCR, MOD_NOREPEAT, VK_F6);
    RegisterHotKey(hwnd, HOTKEY_MIN, MOD_NOREPEAT, VK_F9);
    RegisterHotKey(hwnd, HOTKEY_SEARCH, MOD_NOREPEAT, VK_F3);
    register_snip_hotkeys(hwnd);
    add_tray(hwnd);
    AddClipboardFormatListener(hwnd);
    apply_follow_state();
    if (g_skin != 0) install_scheme_cursors();
    return 0;
  }
  case WM_SIZE:
    layout_children();
    return 0;
  case WM_DRAWITEM:
    draw_pad_button((const DRAWITEMSTRUCT *)lParam);
    return TRUE;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, bg_brush(FALSE));
    int dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
      typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
      GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
      if (fn) dpi = (int)fn(hwnd);
    }
    int th = MulDiv(TITLE_H, dpi, 96);
    int fh = MulDiv(FOOT_H, dpi, 96);
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
    RECT ttext = {padx + MulDiv(6, dpi, 96), MulDiv(4, dpi, 96), rc.right / 2, th - MulDiv(14, dpi, 96)};
    RECT vtext = {padx + MulDiv(6, dpi, 96), th - MulDiv(18, dpi, 96), rc.right / 2, th - 2};
    if (g_statusOn) {
      SetTextColor(hdc, COL_INK);
      if (g_fontUi) SelectObject(hdc, g_fontUi);
      RECT st = {padx + MulDiv(6, dpi, 96), 0, rc.right / 2, th};
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
      DrawTextW(hdc, L"CursorPad", -1, &ttext, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
      SetTextColor(hdc, COL_MUTED);
      if (g_fontSmall) SelectObject(hdc, g_fontSmall);
      DrawTextW(hdc, APP_VERSION_STR, -1, &vtext, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    draw_gutter_thumbs(hdc, dpi, th, fh);

    if (pth->ornate) {
      draw_knight_frame(hdc, rc);
    } else {
      RECT accent = {0, 0, MulDiv(3, dpi, 96), rc.bottom};
      HBRUSH sage = CreateSolidBrush(COL_SAGE);
      FillRect(hdc, &accent, sage);
      DeleteObject(sage);
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
    if (LOWORD(wParam) == ID_CLOSE) DestroyWindow(hwnd);
    if (LOWORD(wParam) == ID_MIN) toggle_hidden();
    if (LOWORD(wParam) == ID_SETTINGS) toggle_settings();
    if (LOWORD(wParam) == ID_ASK_TAB) toggle_ask();
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
    else if (wParam >= HOTKEY_SNIP_BASE && wParam < HOTKEY_SNIP_BASE + SNIP_COUNT)
      paste_line((int)(wParam - HOTKEY_SNIP_BASE + 1));
    return 0;
  case WM_TIMER:
    if (wParam == TIMER_FOLLOW) {
      if (!g_follow || g_hidden) {
        /* двигать нечего — редкий тик только чтобы заметить, когда снова
           включат слежение */
        follow_rate(hwnd, 200);
      } else {
        POINT p;
        GetCursorPos(&p);
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
      KillTimer(hwnd, TIMER_PASTE);
      send_paste();
    }
    if (wParam == TIMER_CURSOR_KEEP && g_skin > 0) apply_scheme_slots();
    if (wParam == TIMER_FILES) files_start_index(FALSE);
    if (wParam == TIMER_FILES_TICK) files_refresh_status();
    if (wParam == TIMER_FILES_PLAN) files_plan_tick();
    return 0;
  case WM_SEARCH_DONE: {
    wchar_t *text = (wchar_t *)lParam;
    if (text) {
      show_answer_text(text);
      free(text);
    }
    return 0;
  }
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
      append_notes(text);
      show_ocr_text(text);
      show_status(L"OCR: текст добавлен в блокнот");
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
    /* своё мы помечаем флажком; без него — скопировал человек */
    if (!g_ownClip) onec_foreign_copy();
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
    int th = MulDiv(TITLE_H, dpi, 96);
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
    if (lParam == NIN_BALLOONUSERCLICK) files_show_changes();
    if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) tray_menu(hwnd);
    if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
      if (g_hidden) restore_from_tray();
      else toggle_follow();
    }
    return 0;
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    share_stop();
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
    UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
    UnregisterHotKey(hwnd, HOTKEY_CURSOR);
    UnregisterHotKey(hwnd, HOTKEY_OCR);
    UnregisterHotKey(hwnd, HOTKEY_MIN);
    UnregisterHotKey(hwnd, HOTKEY_SEARCH);
    RemoveClipboardFormatListener(hwnd);
    unregister_snip_hotkeys(hwnd);
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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
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
    if (argv) LocalFree(argv);
  }
  clear_runas_layer();
  if (!ensure_single_instance()) return 0;
  enable_dpi();

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
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  wc.hIconSm = wc.hIcon;
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

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  finish_update_launch();
  return (int)msg.wParam;
}
