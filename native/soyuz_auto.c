/* ---- «Вставить сам»: автоввод в клиент «Союза» через UI Automation (с 2026.10.24) ----

   Одна операция на одно нажатие — каждое нажатие и есть подтверждение. CursorPad
   делает то же, что человек, и только штатными командами клиента:
     1. в «Союзе» должен быть выделен узел дерева ТП (версия, вариант или операция
        варианта), а в буфере — GUID операции-аналога (soyuz_ui.c);
     2. меню узла — Shift+F10, как с клавиатуры;
     3. в меню ищется пункт «Вставить операцию» и нажимается (Invoke);
     4. встала ли операция, скажет база (слежение soyuz_ui.c).
   Перед каждым шагом — проверка: тот ли процесс (PLMClient.exe), узел ли дерева
   в фокусе, есть ли пункт и доступен ли он. Не сошлось — меню закрывается, в окне
   пишется, на каком шаге и что увидели вместо ожидаемого: после обновления клиента
   названия и устройство окон могут поменяться, и вслепую вводить нельзя. Esc во
   время автоввода — стоп. UI Automation зовётся из своего потока (MTA): из окна
   CursorPad (STA с OLE) вызовы к медленному клиенту подвешивали бы интерфейс. */

#include <uiautomationclient.h>

static const CLSID kSzClsidUia = {0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
static const IID kSzIidUia = {0x30cbe57d, 0xd9d0, 0x452a, {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};
static const IID kSzIidInvoke = {0xfb377fbe, 0x8ea6, 0x46d5, {0x9c, 0x73, 0x64, 0x99, 0x64, 0x2d, 0x30, 0x59}};

static const wchar_t kSzPasteItem[] = L"Вставить операцию"; /* пункт меню узла ТП (PMSZ.PLMSOYUZ.CFG.MFG.UNI.STD) */

static HANDLE g_szAutoThread;
static DWORD g_szSoyuzPid; /* процесс клиента, найденный при нажатии */

static void sz_send_keys(const WORD *vk, int n) {
  INPUT in[8];
  memset(in, 0, sizeof(in));
  int k = 0;
  for (int i = 0; i < n && k < 8; i++) in[k].type = INPUT_KEYBOARD, in[k++].ki.wVk = vk[i];
  for (int i = n - 1; i >= 0 && k < 8; i--)
    in[k].type = INPUT_KEYBOARD, in[k].ki.wVk = vk[i], in[k++].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput((UINT)k, in, sizeof(INPUT));
}

static int sz_el_int(IUIAutomationElement *e, PROPERTYID id) {
  VARIANT v;
  VariantInit(&v);
  int r = 0;
  if (SUCCEEDED(e->lpVtbl->GetCurrentPropertyValue(e, id, &v))) {
    if (v.vt == VT_I4) r = v.lVal;
    else if (v.vt == VT_BOOL) r = v.boolVal != VARIANT_FALSE;
  }
  VariantClear(&v);
  return r;
}

static void sz_el_name(IUIAutomationElement *e, wchar_t *out, int cap) {
  out[0] = 0;
  BSTR b = NULL;
  if (SUCCEEDED(e->lpVtbl->get_CurrentName(e, &b)) && b) lstrcpynW(out, b, cap);
  SysFreeString(b);
}

/* пункт меню без «&» и сочетания клавиш после табуляции */
static void sz_menu_text(const wchar_t *s, wchar_t *out, int cap) {
  int k = 0;
  for (; *s && *s != L'\t' && k < cap - 1; s++)
    if (*s != L'&') out[k++] = *s;
  while (k && out[k - 1] == L' ') k--;
  out[k] = 0;
}

/* условие «свойство = число» */
static IUIAutomationCondition *sz_cond_int(IUIAutomation *u, PROPERTYID id, int val) {
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_I4;
  v.lVal = val;
  IUIAutomationCondition *c = NULL;
  u->lpVtbl->CreatePropertyCondition(u, id, v, &c);
  return c;
}

/* Открытое меню клиента — отдельное окно верхнего уровня его процесса с типом
   «меню» (ContextMenuStrip WinForms и ContextMenu WPF — оба так). Пункты ищем
   только в нём: обход всего рабочего стола был бы долгим. *menuSeen — было ли
   меню; names — какие пункты в нём (для сообщения, если нужного нет). */
static IUIAutomationElement *sz_find_item(IUIAutomation *u, DWORD pid, BOOL *menuSeen, wchar_t *names, int ncap) {
  IUIAutomationElement *root = NULL, *found = NULL;
  if (FAILED(u->lpVtbl->GetRootElement(u, &root)) || !root) return NULL;
  IUIAutomationCondition *cp = sz_cond_int(u, UIA_ProcessIdPropertyId, (int)pid);
  IUIAutomationCondition *cmenu = sz_cond_int(u, UIA_ControlTypePropertyId, UIA_MenuControlTypeId);
  IUIAutomationCondition *citem = sz_cond_int(u, UIA_ControlTypePropertyId, UIA_MenuItemControlTypeId);
  IUIAutomationCondition *top = NULL;
  if (cp && cmenu) u->lpVtbl->CreateAndCondition(u, cp, cmenu, &top);
  IUIAutomationElementArray *menus = NULL;
  if (top) root->lpVtbl->FindAll(root, TreeScope_Children, top, &menus);
  int nm = 0;
  if (menus) menus->lpVtbl->get_Length(menus, &nm);
  for (int m = 0; m < nm && !found && citem; m++) {
    IUIAutomationElement *menu = NULL;
    if (FAILED(menus->lpVtbl->GetElement(menus, m, &menu)) || !menu) continue;
    IUIAutomationElementArray *items = NULL;
    menu->lpVtbl->FindAll(menu, TreeScope_Descendants, citem, &items);
    int n = 0;
    if (items) items->lpVtbl->get_Length(items, &n);
    if (n > 0) *menuSeen = TRUE;
    for (int i = 0; i < n && !found; i++) {
      IUIAutomationElement *e = NULL;
      if (FAILED(items->lpVtbl->GetElement(items, i, &e)) || !e) continue;
      wchar_t raw[200], t[200];
      sz_el_name(e, raw, 200);
      sz_menu_text(raw, t, 200);
      if (!wcscmp(t, kSzPasteItem)) {
        found = e;
        continue;
      }
      int l = (int)wcslen(names);
      if (t[0] && l < ncap - 40) _snwprintf(names + l, ncap - l, L"%s«%s»", l ? L", " : L"", t), names[ncap - 1] = 0;
      e->lpVtbl->Release(e);
    }
    if (items) items->lpVtbl->Release(items);
    menu->lpVtbl->Release(menu);
  }
  if (menus) menus->lpVtbl->Release(menus);
  if (top) top->lpVtbl->Release(top);
  if (cp) cp->lpVtbl->Release(cp);
  if (cmenu) cmenu->lpVtbl->Release(cmenu);
  if (citem) citem->lpVtbl->Release(citem);
  root->lpVtbl->Release(root);
  return found;
}

static BOOL sz_aborted(void) { return g_szAutoAbort != 0; }

/* один проход: проверка → меню → пункт → нажать; итог — сообщением окну */
static DWORD WINAPI sz_auto_thread(LPVOID arg) {
  DWORD pid = (DWORD)(UINT_PTR)arg;
  wchar_t msg[600] = L"";
  BOOL ok = FALSE, menuOpen = FALSE;
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  IUIAutomation *u = NULL;
  IUIAutomationElement *focus = NULL, *item = NULL;
  if (FAILED(CoCreateInstance(&kSzClsidUia, NULL, CLSCTX_INPROC_SERVER, &kSzIidUia, (void **)&u)) || !u) {
    lstrcpynW(msg, L"Шаг 1: UI Automation Windows недоступна — автоввод невозможен, вставьте вручную.", 600);
    goto done;
  }
  /* 1. фокус — узел дерева в клиенте «Союза» */
  if (FAILED(u->lpVtbl->GetFocusedElement(u, &focus)) || !focus) {
    lstrcpynW(msg, L"Шаг 1: не видно, где фокус ввода. Щёлкните в «Союзе» по узлу ТП и нажмите снова.", 600);
    goto done;
  }
  wchar_t node[200];
  sz_el_name(focus, node, 200);
  int fpid = sz_el_int(focus, UIA_ProcessIdPropertyId), ftype = sz_el_int(focus, UIA_ControlTypePropertyId);
  if ((DWORD)fpid != pid) {
    lstrcpynW(msg, L"Шаг 1: ввод не в «Союзе». Щёлкните там по версии ТП (или операции варианта), куда вставлять, "
                   L"и нажмите «Вставить сам» снова. Ничего не нажато.",
              600);
    goto done;
  }
  /* Дерево клиента (WinForms SysTreeView32) узлов в UI Automation не показывает: в фокусе
     оказывается само дерево, а не узел. Узнать, тот ли узел выделен, можно только по его
     меню — «Вставить операцию» бывает лишь у версии ТП, варианта и операции варианта. */
  wchar_t cls[200] = L"";
  {
    BSTR b = NULL;
    if (SUCCEEDED(focus->lpVtbl->get_CurrentClassName(focus, &b)) && b) lstrcpynW(cls, b, 200);
    SysFreeString(b);
  }
  if (ftype != UIA_TreeItemControlTypeId && ftype != UIA_TreeControlTypeId && !wcsstr(cls, L"SysTreeView32")) {
    _snwprintf(msg, 600,
               L"Шаг 1: в «Союзе» ввод не в дереве («%s»). Щёлкните в дереве по версии ТП, варианту или операции "
               L"варианта и нажмите снова. Ничего не нажато.",
               node[0] ? node : cls[0] ? cls : L"без названия");
    goto done;
  }
  if (!node[0]) lstrcpynW(node, L"выделенный узел", 200);
  if (sz_aborted()) goto stopped;
  /* 2. меню узла — как с клавиатуры */
  {
    WORD keys[2] = {VK_SHIFT, VK_F10};
    sz_send_keys(keys, 2);
    menuOpen = TRUE;
  }
  /* 3. пункт «Вставить операцию» — ждём меню до трёх секунд */
  BOOL menuSeen = FALSE;
  wchar_t names[400] = L"";
  for (int t = 0; t < 30 && !item && !sz_aborted(); t++) {
    Sleep(100);
    names[0] = 0;
    item = sz_find_item(u, pid, &menuSeen, names, 400);
  }
  if (sz_aborted()) goto stopped;
  if (!item) {
    if (!menuSeen)
      _snwprintf(msg, 600,
                 L"Шаг 2: меню узла «%s» не открылось за 3 с (Shift+F10). Возможно, клиент «Союза» обновился. "
                 L"Ничего не нажато — вставьте вручную: правая кнопка → «%s».",
                 node, kSzPasteItem);
    else
      _snwprintf(msg, 600,
                 L"Шаг 3: в меню узла «%s» нет пункта «%s». Видны: %s. Выделен не тот узел, нет прав технолога или "
                 L"клиент изменился после обновления. Меню закрыто, ничего не нажато.",
                 node, kSzPasteItem, names[0] ? names : L"—");
    goto done;
  }
  if (!sz_el_int(item, UIA_IsEnabledPropertyId)) {
    _snwprintf(msg, 600,
               L"Шаг 3: «%s» в меню недоступна: в буфере не операция или к ТП «%s» нет прав технолога. Меню закрыто.",
               kSzPasteItem, node);
    goto done;
  }
  if (sz_aborted()) goto stopped;
  /* 4. нажать */
  {
    IUIAutomationInvokePattern *inv = NULL;
    if (FAILED(item->lpVtbl->GetCurrentPatternAs(item, UIA_InvokePatternId, &kSzIidInvoke, (void **)&inv)) || !inv) {
      _snwprintf(msg, 600, L"Шаг 4: пункт «%s» нашёлся, но нажать его UI Automation не даёт. Меню закрыто — нажмите сами.",
                 kSzPasteItem);
      goto done;
    }
    HRESULT hr = inv->lpVtbl->Invoke(inv);
    inv->lpVtbl->Release(inv);
    if (FAILED(hr)) {
      _snwprintf(msg, 600, L"Шаг 4: «%s» не нажалась (0x%08lX). Меню закрыто — нажмите сами.", kSzPasteItem,
                 (unsigned long)hr);
      goto done;
    }
    menuOpen = FALSE; /* меню закрылось само */
    ok = TRUE;
    _snwprintf(msg, 600, L"«%s» нажата на «%s» — жду операцию в ТП…", kSzPasteItem, node);
  }
  goto done;
stopped:
  lstrcpynW(msg, L"Автоввод остановлен (Esc). Ничего не нажато.", 600);
done:
  if (menuOpen && !ok) {
    WORD esc[1] = {VK_ESCAPE};
    sz_send_keys(esc, 1);
  }
  if (item) item->lpVtbl->Release(item);
  if (focus) focus->lpVtbl->Release(focus);
  if (u) u->lpVtbl->Release(u);
  CoUninitialize();
  wchar_t *m = (wchar_t *)malloc(600 * sizeof(wchar_t));
  if (m) lstrcpynW(m, msg, 600);
  InterlockedExchange(&g_szAutoBusy, 0);
  if (!g_szWnd || !PostMessageW(g_szWnd, WM_SZ_AUTO, (WPARAM)ok, (LPARAM)m)) free(m);
  return 0;
}

/* «Вставить сам» — кнопка окна «В Союз» */
static void sz_auto_start(void) {
  SoItem *it = sz_cur();
  if (g_szAutoBusy) return;
  if (!it || !sz_guid_mode(it)) {
    lstrcpynW(g_szNote, L"Автоввод — только для операций с аналогом (GUID). Эту вставьте вручную: текст в буфере.", 300);
    sz_refresh();
    return;
  }
  if (g_szClip) {
    lstrcpynW(g_szNote, L"В буфере не операция — сначала «В буфер».", 300);
    sz_refresh();
    return;
  }
  SzFind f = {NULL};
  EnumWindows(sz_find_cb, (LPARAM)&f);
  if (!f.found) {
    lstrcpynW(g_szNote, L"Клиент «Союза» (PLMClient.exe) не найден — откройте ТП в нём.", 300);
    sz_refresh();
    return;
  }
  GetWindowThreadProcessId(f.found, &g_szSoyuzPid);
  if (g_szAutoThread) CloseHandle(g_szAutoThread);
  g_szAutoAbort = 0;
  InterlockedExchange(&g_szAutoBusy, 1);
  g_szAutoThread = CreateThread(NULL, 0, sz_auto_thread, (LPVOID)(UINT_PTR)g_szSoyuzPid, 0, NULL);
  if (!g_szAutoThread) {
    InterlockedExchange(&g_szAutoBusy, 0);
    return;
  }
  lstrcpynW(g_szNote, L"Автоввод: проверяю узел и открываю меню… Esc — стоп", 300);
  sz_refresh();
}
