// wininput.c (part of mintty)
// Copyright 2008-22 Andy Koppe, 2015-2022 Thomas Wolff
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"
#include "winsearch.h"
#include "wintab.h"

#include "charset.h"
#include "child.h"
#include "tek.h"

#include <math.h>
#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM
#include <winnls.h>
#include <termios.h>


static HMENU ctxmenu = NULL;
static HMENU sysmenu;
static int sysmenulen;
//static uint kb_select_key = 0;
static uint super_key = 0;
static uint hyper_key = 0;
// Compose support
static uint compose_key = 0;
static uint last_key_down = 0;
static uint last_key_up = 0;

static uint newwin_key = 0;
static bool newwin_pending = false;
static bool newwin_shifted = false;
static bool newwin_home = false;
static int newwin_monix = 0, newwin_moniy = 0;

static int transparency_pending = 0;
static bool selection_pending = false;
bool kb_input = false;
uint kb_trace = 0;

struct function_def {
  string name;
  union {
    WPARAM cmd;
    void (*fct)(void);
    void (*fct_key)(uint key, mod_keys mods);
  };
  uint (*fct_status)(void);
};

static struct function_def * function_def(char * cmd);


static inline void
show_last_error()
{
  int err = GetLastError();
  if (err) {
    static wchar winmsg[1024];  // constant and < 1273 or 1705 => issue #530
    FormatMessageW(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
      0, err, 0, winmsg, lengthof(winmsg), 0
    );
    printf("Error %d: %ls\n", err, winmsg);
  }
}


/* Icon conversion */

// https://www.nanoant.com/programming/themed-menus-icons-a-complete-vista-xp-solution
// last attempt among lots of googled solution proposals, 
// and the only one that actually works, except that it uses white background
static HBITMAP
icon_bitmap(HICON hIcon)
{
  RECT rect;
  rect.left = rect.top = 0;
  // retrieve needed size of menu icons; but what about per-monitor DPI?
  rect.right = GetSystemMetrics(SM_CXMENUCHECK);
  rect.bottom = GetSystemMetrics(SM_CYMENUCHECK);

  //HWND desktop = GetDesktopWindow();
  //HWND desktop = 0;
  HWND desktop = wnd;

  HDC screen_dev = GetDC(desktop);
  if (screen_dev == NULL)
    return NULL;

  // Create a compatible DC
  HDC dst_hdc = CreateCompatibleDC(screen_dev);
  if (dst_hdc == NULL) {
    ReleaseDC(desktop, screen_dev);
    return NULL;
  }

  // Create a new bitmap of icon size
  HBITMAP bmp = CreateCompatibleBitmap(screen_dev, rect.right, rect.bottom);
  if (bmp == NULL) {
    DeleteDC(dst_hdc);
    ReleaseDC(desktop, screen_dev);
    return NULL;
  }

  // Select it into the compatible DC
  HBITMAP old_dst_bmp = (HBITMAP)SelectObject(dst_hdc, bmp);
  if (old_dst_bmp == NULL)
    return NULL;

  // Fill the background of the compatible DC with the given colour
  SetBkColor(dst_hdc, win_get_sys_colour(COLOR_MENU));
  ExtTextOut(dst_hdc, 0, 0, ETO_OPAQUE, &rect, NULL, 0, NULL);

  // Draw the icon into the compatible DC
  DrawIconEx(dst_hdc, 0, 0, hIcon, rect.right, rect.bottom, 0, NULL, DI_NORMAL);

  // Restore settings
  SelectObject(dst_hdc, old_dst_bmp);
  DeleteDC(dst_hdc);
  ReleaseDC(desktop, screen_dev);
  return bmp;
}


/* Menu handling */

static inline void
show_menu_info(HMENU menu)
{
  MENUINFO mi;
  mi.cbSize = sizeof(MENUINFO);
  mi.fMask = MIM_STYLE | MIM_BACKGROUND;
  GetMenuInfo(menu, &mi);
  printf("menuinfo style %04X brush %p\n", (uint)mi.dwStyle, mi.hbrBack);
}

static void
append_commands(HMENU menu, wstring commands, UINT_PTR idm_cmd, bool add_icons, bool sysmenu)
{
  char * cmds = cs__wcstoutf(commands);
  char * cmdp = cmds;
  int n = 0;
  char sepch = ';';
  if ((uchar)*cmdp <= (uchar)' ')
    sepch = *cmdp++;

  char * paramp;
  while ((paramp = strchr(cmdp, ':'))) {
    *paramp++ = '\0';
    if (sysmenu)
      InsertMenuW(menu, SC_CLOSE, MF_ENABLED, idm_cmd + n, _W(cmdp));
    else
      AppendMenuW(menu, MF_ENABLED, idm_cmd + n, _W(cmdp));
    cmdp = strchr(paramp, sepch);
    if (cmdp)
      *cmdp++ = '\0';

    if (add_icons) {
      MENUITEMINFOW mi;
      mi.cbSize = sizeof(MENUITEMINFOW);
      mi.fMask = MIIM_BITMAP;
      wchar * params = cs__utftowcs(paramp);
      wstring iconfile = wslicon(params);  // default: 0 (no icon)
      free(params);
      HICON icon;
      if (iconfile)
        icon = (HICON) LoadImageW(0, iconfile,
                                  IMAGE_ICON, 0, 0,
                                  LR_DEFAULTSIZE | LR_LOADFROMFILE
                                  | LR_LOADTRANSPARENT);
      else
        icon = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
      HBITMAP bitmap = icon_bitmap(icon);
      mi.hbmpItem = bitmap;
      SetMenuItemInfoW(menu, idm_cmd + n, 0, &mi);
      if (icon)
        DestroyIcon(icon);
    }

    n++;
    if (!cmdp)
      break;
    // check for multi-line separation
    if (*cmdp == '\\' && cmdp[1] == '\n') {
      cmdp += 2;
      while (iswspace(*cmdp))
        cmdp++;
    }
  }
  free(cmds);
}

struct data_add_switcher {
  int tabi;
  bool use_win_icons;
  HMENU menu;
};

static BOOL CALLBACK
wnd_enum_tabs(HWND curr_wnd, LPARAM lParam)
{
  WINDOWINFO curr_wnd_info;
  curr_wnd_info.cbSize = sizeof(WINDOWINFO);
  GetWindowInfo(curr_wnd, &curr_wnd_info);
  if (class_atom == curr_wnd_info.atomWindowType) {
    struct data_add_switcher * pdata = (struct data_add_switcher *)lParam;
    HMENU menu = pdata->menu;
    bool use_win_icons = pdata->use_win_icons;
    int tabi = pdata->tabi;
    pdata->tabi++;

    int len = GetWindowTextLengthW(curr_wnd);
    wchar title[len + 1];
    len = GetWindowTextW(curr_wnd, title, len + 1);

    AppendMenuW((HMENU)menu, MF_ENABLED, IDM_GOTAB + tabi, title);
    MENUITEMINFOW mi;
    mi.cbSize = sizeof(MENUITEMINFOW);
    mi.fMask = MIIM_STATE;
    mi.fState = // (IsIconic(curr_wnd) ? MFS_DISABLED : 0) |
                (curr_wnd == wnd ? MFS_DEFAULT : 0);
      /*
         MFS_DEFAULT: "A menu can contain only one default menu item, 
                      which is displayed in bold."
                      but multiple bold entries seem to work
         MFS_HILITE: highlight is volatile
         MFS_CHECKED: conflict with other MIIM_BITMAP usage
      */
    //if (has_flashed(curr_wnd))
    //  mi.fState |= MFS_HILITE;

    mi.fMask |= MIIM_BITMAP;
    //if (has_flashed(curr_wnd))
    //  mi.hbmpItem = HBMMENU_POPUP_RESTORE;
    //else
    if (IsIconic(curr_wnd))
      mi.hbmpItem = HBMMENU_POPUP_MINIMIZE;
    else
      mi.hbmpItem = HBMMENU_POPUP_MAXIMIZE;

    if (use_win_icons && !IsIconic(curr_wnd)) {
# ifdef show_icon_via_itemdata
# warning does not work
      mi.fMask |= MIIM_DATA;
      mi.hbmpItem = HBMMENU_SYSTEM;
      mi.dwItemData = (ULONG_PTR)curr_wnd;
# endif
      HICON icon = (HICON)GetClassLongPtr(curr_wnd, GCLP_HICONSM);
      if (icon) {
        // convert icon to bitmap
        //https://stackoverflow.com/questions/7375003/how-to-convert-hicon-to-hbitmap-in-vc/16787105#16787105
# ifdef it_could_be_simple_Microsoft
        // simple solution, loses transparency (black border)
        ICONINFO ii;
        GetIconInfo(icon, &ii);
        HBITMAP bitmap = ii.hbmColor;
# else
        HBITMAP bitmap = icon_bitmap(icon);
# endif

        mi.fMask |= MIIM_BITMAP;
        mi.hbmpItem = bitmap;
      }
    }

#ifdef show_icon_via_callback
#warning does not work
    mi.fMask |= MIIM_BITMAP;
    mi.hbmpItem = HBMMENU_CALLBACK;
#endif

#ifdef show_checkmarks
    // this works only if both hbmpChecked and hbmpUnchecked are populated,
    // not using HBMMENU_ predefines
    mi.fMask |= MIIM_CHECKMARKS;
    mi.fMask &= ~MIIM_BITMAP;
    mi.hbmpChecked = mi.hbmpItem;  // test value (from use_win_icons)
    mi.hbmpUnchecked = NULL;
    if (!IsIconic(curr_wnd))
      mi.fState |= MFS_CHECKED;
#endif

    SetMenuItemInfoW((HMENU)menu, IDM_GOTAB + tabi, 0, &mi);
    add_tab(tabi, curr_wnd);
  }
  return true;
}

static void
add_switcher(HMENU menu, bool vsep, bool hsep, bool use_win_icons)
{
  //printf("add_switcher vsep %d hsep %d\n", vsep, hsep);
  uint bar = vsep ? MF_MENUBARBREAK : 0;
  if (hsep)
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
  //__ Context menu, session switcher ("virtual tabs") menu label
  AppendMenuW(menu, MF_DISABLED | bar, 0, _W("Session switcher"));
  AppendMenuW(menu, MF_SEPARATOR, 0, 0);
  struct data_add_switcher data = {
    .tabi = 0,
    .use_win_icons = use_win_icons,
    .menu = menu
  };
  clear_tabs();
  EnumWindows(wnd_enum_tabs, (LPARAM)&data);
}

static bool
add_launcher(HMENU menu, bool vsep, bool hsep)
{
  //printf("add_launcher vsep %d hsep %d\n", vsep, hsep);
  if (*cfg.session_commands) {
    uint bar = vsep ? MF_MENUBARBREAK : 0;
    if (hsep)
      AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    //__ Context menu, session launcher ("virtual tabs") menu label
    AppendMenuW(menu, MF_DISABLED | bar, 0, _W("Session launcher"));
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    append_commands(menu, cfg.session_commands, IDM_SESSIONCOMMAND, true, false);
    return true;
  }
  else
    return false;
}

#define dont_debug_modify_menu

void
win_update_menus(bool callback)
{
  if (callback) {
    // invoked after WM_INITMENU
  }
  else
    return;

  bool shorts = !term.shortcut_override;
  bool clip = shorts && cfg.clip_shortcuts;
  bool alt_fn = shorts && cfg.alt_fn_shortcuts;
  bool ct_sh = shorts && cfg.ctrl_shift_shortcuts;

#ifdef debug_modify_menu
  printf("win_update_menus\n");
#endif

  void
  modify_menu(HMENU menu, UINT item, UINT state, wchar * label, wchar * key)
  // if item is sysentry: ignore state
  // state: MF_ENABLED, MF_GRAYED, MF_CHECKED, MF_UNCHECKED
  // label: if null, use current label
  // key: shortcut description; localize "Ctrl+Alt+Shift+"
  {
    bool sysentry = item >= 0xF000;
#ifdef debug_modify_menu
    if (sysentry)
      printf("mm %04X <%ls> <%ls>\n", item, label, key);
#endif

    MENUITEMINFOW mi;
    mi.cbSize = sizeof(MENUITEMINFOW);
#define dont_debug_menuitem
#ifdef debug_menuitem
    mi.fMask = MIIM_BITMAP | MIIM_STATE | MIIM_STRING | MIIM_DATA;
    mi.dwTypeData = NULL;
    GetMenuItemInfoW(menu, item, 0, &mi);
    mi.cch++;
    mi.dwTypeData = newn(wchar, mi.cch);
    int ok = GetMenuItemInfoW(menu, item, 0, &mi);
    printf("%d %X %d<%ls> <%ls>\n", ok, mi.fState, mi.cch, mi.dwTypeData, (wstring)mi.dwItemData);
    mi.fState &= ~MFS_DEFAULT;  // does not work if used 
                                // in SetMenuItemInfoW with MIIM_STATE
#endif
    mi.fMask = MIIM_STRING;
    if (!label || sysentry) {
      mi.dwTypeData = NULL;
      GetMenuItemInfoW(menu, item, 0, &mi);
      mi.cch++;
      mi.dwTypeData = newn(wchar, mi.cch);
      if (sysentry)
        mi.fMask |= MIIM_DATA;
      GetMenuItemInfoW(menu, item, 0, &mi);
    }

    // prepare info to write
    mi.fMask = MIIM_STRING;
    if (sysentry) {
      if (label) {
        // backup system (localized) label to application data
        if (!mi.dwItemData) {
          mi.dwItemData = (ULONG_PTR)wcsdup(mi.dwTypeData);
          mi.fMask |= MIIM_DATA;  // make sure it's stored
        }
      }
      else if (mi.dwItemData) {
        // restore system (localized) label from backup
        mi.dwTypeData = wcsdup((wstring)mi.dwItemData);
      }
    }
    //don't mi.fMask |= MIIM_ID; mi.wID = ...; would override item ID
    if (label)
      mi.dwTypeData = wcsdup(label);
    if (!sysentry) {
      mi.fMask |= MIIM_STATE | MIIM_FTYPE;
      mi.fState = state;
      mi.fType = MFT_STRING;
    }
    wchar * tab = wcschr(mi.dwTypeData, '\t');
    if (tab)
      *tab = '\0';
    if (key) {
      // append TAB and shortcut to label; localize "Ctrl+Alt+Shift+"
      mod_keys mod = 0;
      if (0 == wcsncmp(key, W("Ctrl+"), 5)) {
        mod |= MDK_CTRL;
        key += 5;
      }
      if (0 == wcsncmp(key, W("Alt+"), 4)) {
        mod |= MDK_ALT;
        key += 4;
      }
      if (0 == wcsncmp(key, W("Shift+"), 6)) {
        mod |= MDK_SHIFT;
        key += 6;
      }
      int len1 = wcslen(mi.dwTypeData) + 1
                 + (mod & MDK_CTRL ? wcslen(_W("Ctrl+")) : 0)
                 + (mod & MDK_ALT ? wcslen(_W("Alt+")) : 0)
                 + (mod & MDK_SHIFT ? wcslen(_W("Shift+")) : 0)
                 + wcslen(key) + 1;
      mi.dwTypeData = renewn(mi.dwTypeData, len1);
      wcscat(mi.dwTypeData, W("\t"));
      if (mod & MDK_CTRL) wcscat(mi.dwTypeData, _W("Ctrl+"));
      if (mod & MDK_ALT) wcscat(mi.dwTypeData, _W("Alt+"));
      if (mod & MDK_SHIFT) wcscat(mi.dwTypeData, _W("Shift+"));
      wcscat(mi.dwTypeData, key);
    }
#ifdef debug_modify_menu
    if (sysentry)
      printf("-> %04X [%04X] %04X <%ls>\n", item, mi.fMask, mi.fState, mi.dwTypeData);
#endif

    SetMenuItemInfoW(menu, item, 0, &mi);

    free(mi.dwTypeData);
  }

  wchar *
  itemlabel(char * label)
  {
    char * loc = _(label);
    if (loc == label)
      // no localization entry
      return null;  // indicate to use system localization
    else
      return _W(label);  // use our localization
  }

  //__ System menu:
  modify_menu(sysmenu, SC_RESTORE, 0, itemlabel(__("&Restore")), null);
  //__ System menu:
  modify_menu(sysmenu, SC_MOVE, 0, itemlabel(__("&Move")), null);
  //__ System menu:
  modify_menu(sysmenu, SC_SIZE, 0, itemlabel(__("&Size")), null);
  //__ System menu:
  modify_menu(sysmenu, SC_MINIMIZE, 0, itemlabel(__("Mi&nimize")), null);
  //__ System menu:
  modify_menu(sysmenu, SC_MAXIMIZE, 0, itemlabel(__("Ma&ximize")), null);
  //__ System menu:
  modify_menu(sysmenu, SC_CLOSE, 0, itemlabel(__("&Close")),
    alt_fn ? W("Alt+F4") : ct_sh ? W("Ctrl+Shift+W") : null
  );

  //__ System menu:
  modify_menu(sysmenu, IDM_NEW, 0, _W("New &Window"),
    alt_fn ? (cfg.tabbar ? W("Sh+Sh+Alt+F2") : W("Alt+F2"))
           : ct_sh ? W("Ctrl+Shift+N") : null
  );
  if (cfg.tabbar)
    //__ System menu:
    modify_menu(sysmenu, IDM_TAB, 0, _W("New &Tab"),
      alt_fn ? W("Alt+F2") : ct_sh ? /*W("Ctrl+Shift+T")*/ null : null
    );

  uint sel_enabled = term.selected ? MF_ENABLED : MF_GRAYED;
  EnableMenuItem(ctxmenu, IDM_OPEN, sel_enabled);
  //__ Context menu:
  modify_menu(ctxmenu, IDM_COPY, sel_enabled, _W("&Copy"),
    clip ? W("Ctrl+Ins") : ct_sh ? W("Ctrl+Shift+C") : null
  );
  // enable/disable predefined extended context menu entries
  // (user-definable ones are handled via fct_status())
  EnableMenuItem(ctxmenu, IDM_COPY_TEXT, sel_enabled);
  EnableMenuItem(ctxmenu, IDM_COPY_TABS, sel_enabled);
  EnableMenuItem(ctxmenu, IDM_COPY_TXT, sel_enabled);
  EnableMenuItem(ctxmenu, IDM_COPY_RTF, sel_enabled);
  EnableMenuItem(ctxmenu, IDM_COPY_HTXT, sel_enabled);
  EnableMenuItem(ctxmenu, IDM_COPY_HFMT, sel_enabled);
  EnableMenuItem(ctxmenu, IDM_COPY_HTML, sel_enabled);

  uint paste_enabled =
    IsClipboardFormatAvailable(CF_TEXT) ||
    IsClipboardFormatAvailable(CF_UNICODETEXT) ||
    IsClipboardFormatAvailable(CF_HDROP)
    ? MF_ENABLED : MF_GRAYED;
  //__ Context menu:
  modify_menu(ctxmenu, IDM_PASTE, paste_enabled, _W("&Paste "),
    clip ? W("Shift+Ins") : ct_sh ? W("Ctrl+Shift+V") : null
  );

  //__ Context menu:
  modify_menu(ctxmenu, IDM_COPASTE, sel_enabled, _W("Copy → Paste"),
    clip ? W("Ctrl+Shift+Ins") : null
  );

  //__ Context menu:
  modify_menu(ctxmenu, IDM_SEARCH, 0, _W("S&earch"),
    alt_fn ? W("Alt+F3") : ct_sh ? W("Ctrl+Shift+H") : null
  );

  uint logging_enabled = (logging || *cfg.log) ? MF_ENABLED : MF_GRAYED;
  uint logging_checked = logging ? MF_CHECKED : MF_UNCHECKED;
  //__ Context menu:
  modify_menu(ctxmenu, IDM_TOGLOG, logging_enabled | logging_checked, _W("&Log to File"),
    null
  );

  uint charinfo = show_charinfo ? MF_CHECKED : MF_UNCHECKED;
  //__ Context menu:
  modify_menu(ctxmenu, IDM_TOGCHARINFO, charinfo, _W("Character &Info"),
    null
  );

  uint vt220kb = term.vt220_keys ? MF_CHECKED : MF_UNCHECKED;
  //__ Context menu:
  modify_menu(ctxmenu, IDM_TOGVT220KB, vt220kb, _W("VT220 Keyboard"),
    null
  );

  //__ Context menu:
  modify_menu(ctxmenu, IDM_RESET, 0, _W("&Reset"),
    alt_fn ? W("Alt+F8") : ct_sh ? W("Ctrl+Shift+R") : null
  );

  uint defsize_enabled =
    IsZoomed(wnd) || term.cols != cfg.cols || term.rows != cfg.rows
    ? MF_ENABLED : MF_GRAYED;
  //__ Context menu:
  modify_menu(ctxmenu, IDM_DEFSIZE_ZOOM, defsize_enabled, _W("&Default Size"),
    alt_fn ? W("Alt+F10") : ct_sh ? W("Ctrl+Shift+D") : null
  );

  uint scrollbar_checked = term.show_scrollbar ? MF_CHECKED : MF_UNCHECKED;
#ifdef allow_disabling_scrollbar
  if (!cfg.scrollbar)
    scrollbar_checked |= MF_GRAYED;
#endif
  //__ Context menu:
  modify_menu(ctxmenu, IDM_SCROLLBAR, scrollbar_checked, _W("Scroll&bar"),
    null
  );

  uint fullscreen_checked = win_is_fullscreen ? MF_CHECKED : MF_UNCHECKED;
  //__ Context menu:
  modify_menu(ctxmenu, IDM_FULLSCREEN_ZOOM, fullscreen_checked, _W("&Full Screen"),
    alt_fn ? W("Alt+F11") : ct_sh ? W("Ctrl+Shift+F") : null
  );

  uint otherscreen_checked = term.show_other_screen ? MF_CHECKED : MF_UNCHECKED;
  //__ Context menu:
  modify_menu(ctxmenu, IDM_FLIPSCREEN, otherscreen_checked, _W("Flip &Screen"),
    alt_fn ? W("Alt+F12") : ct_sh ? W("Ctrl+Shift+S") : null
  );

  uint status_line = term.st_type == 1 ? MF_CHECKED
                   : term.st_type == 0 ? MF_UNCHECKED
                   : MF_GRAYED;
  //__ Context menu:
  modify_menu(ctxmenu, IDM_STATUSLINE, status_line, _W("Status Line"),
    null
  );

  uint options_enabled = config_wnd ? MF_GRAYED : MF_ENABLED;
  EnableMenuItem(ctxmenu, IDM_OPTIONS, options_enabled);
  EnableMenuItem(sysmenu, IDM_OPTIONS, options_enabled);

  // refresh remaining labels to facilitate (changed) localization
  //__ System menu:
  modify_menu(sysmenu, IDM_COPYTITLE, 0, _W("Copy &Title"), null);
  //__ System menu:
  modify_menu(sysmenu, IDM_OPTIONS, 0, _W("&Options..."), null);

  // update user-defined menu functions (checked/enabled)
  void
  check_commands(HMENU menu, wstring commands, UINT_PTR idm_cmd)
  {
    char * cmds = cs__wcstoutf(commands);
    char * cmdp = cmds;
    int n = 0;
    char sepch = ';';
    if ((uchar)*cmdp <= (uchar)' ')
      sepch = *cmdp++;

    char * paramp;
    while ((paramp = strchr(cmdp, ':'))) {
      *paramp++ = '\0';
      char * newcmdp = strchr(paramp, sepch);
      if (newcmdp)
        *newcmdp++ = '\0';

      struct function_def * fudef = function_def(paramp);
      // localize
      wchar * label = _W(cmdp);
      uint status = 0;
      if (fudef && fudef->fct_status) {
        status = fudef->fct_status();
        //EnableMenuItem(menu, idm_cmd + n, status);  // done by modify_menu
      }
      modify_menu(menu, idm_cmd + n, status, label, null);

      cmdp = newcmdp;
      n++;
      if (!cmdp)
        break;
      // check for multi-line separation
      if (*cmdp == '\\' && cmdp[1] == '\n') {
        cmdp += 2;
        while (iswspace(*cmdp))
          cmdp++;
      }
    }
    free(cmds);
  }
  if (*cfg.ctx_user_commands)
    check_commands(ctxmenu, cfg.ctx_user_commands, IDM_CTXMENUFUNCTION);
  if (*cfg.sys_user_commands)
    check_commands(sysmenu, cfg.sys_user_commands, IDM_SYSMENUFUNCTION);

#ifdef vary_sysmenu
  static bool switcher_in_sysmenu = false;
  if (!switcher_in_sysmenu) {
    add_switcher(sysmenu, true, false, true);
    switcher_in_sysmenu = true;
  }
#endif
  (void)sysmenulen;
}

static bool
add_user_commands(HMENU menu, bool vsep, bool hsep, wstring title, wstring commands, UINT_PTR idm_cmd)
{
  //printf("add_user_commands vsep %d hsep %d\n", vsep, hsep);
  if (*commands) {
    uint bar = vsep ? MF_MENUBARBREAK : 0;
    if (hsep)
      AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    if (title) {
      AppendMenuW(menu, MF_DISABLED | bar, 0, title);
      AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    }

    append_commands(menu, commands, idm_cmd, false, false);
    return true;
  }
  else
    return false;
}

static void
win_init_ctxmenu(bool extended_menu, bool with_user_commands)
{
#ifdef debug_modify_menu
  printf("win_init_ctxmenu\n");
#endif
  //__ Context menu:
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_OPEN, _W("Ope&n"));
  AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_COPY, 0);
  if (extended_menu) {
    //__ Context menu:
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_COPY_TEXT, _W("Copy as text"));
    //__ Context menu:
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_COPY_TABS, _W("Copy with TABs"));
    //__ Context menu:
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_COPY_RTF, _W("Copy as RTF"));
    //__ Context menu:
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_COPY_HTXT, _W("Copy as HTML text"));
    //__ Context menu:
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_COPY_HFMT, _W("Copy as HTML"));
    //__ Context menu:
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_COPY_HTML, _W("Copy as HTML full"));
  }
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_PASTE, 0);
  if (extended_menu) {
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_COPASTE, 0);
  }
  //__ Context menu:
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_SELALL, _W("Select &All"));
  //__ Context menu:
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_SAVEIMG, _W("Save as &Image"));
  if (tek_mode) {
    AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TEKRESET, W("Tektronix RESET"));
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TEKPAGE, W("Tektronix PAGE"));
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TEKCOPY, W("Tektronix COPY"));

  }
  AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_SEARCH, 0);
  if (extended_menu) {
    //__ Context menu: write terminal window contents as HTML file
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_HTML, _W("HTML Screen Dump"));
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TOGLOG, 0);
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TOGCHARINFO, 0);
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TOGVT220KB, 0);
  }
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_RESET, 0);
  if (extended_menu) {
    //__ Context menu: clear scrollback buffer (lines scrolled off the window)
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_CLRSCRLBCK, _W("Clear Scrollback"));
  }
  AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_DEFSIZE_ZOOM, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_SCROLLBAR, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_FULLSCREEN_ZOOM, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_FLIPSCREEN, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_STATUSLINE, 0);
  AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  if (extended_menu) {
    //__ Context menu: generate a TTY BRK condition (tty line interrupt)
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_BREAK, _W("Send Break"));
    AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  }

  if (with_user_commands && *cfg.ctx_user_commands) {
    append_commands(ctxmenu, cfg.ctx_user_commands, IDM_CTXMENUFUNCTION, false, false);
    AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  }
  else if (with_user_commands && *cfg.user_commands) {
    append_commands(ctxmenu, cfg.user_commands, IDM_USERCOMMAND, false, false);
    AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  }

  //__ Context menu:
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_OPTIONS, _W("&Options..."));
}

void
win_init_menus(void)
{
#ifdef debug_modify_menu
  printf("win_init_menus\n");
#endif

  sysmenu = GetSystemMenu(wnd, false);

  if (*cfg.sys_user_commands)
    append_commands(sysmenu, cfg.sys_user_commands, IDM_SYSMENUFUNCTION, false, true);
  else {
    //__ System menu:
    InsertMenuW(sysmenu, SC_CLOSE, MF_ENABLED, IDM_COPYTITLE, _W("Copy &Title"));
    //__ System menu:
    InsertMenuW(sysmenu, SC_CLOSE, MF_ENABLED, IDM_OPTIONS, _W("&Options..."));
    InsertMenuW(sysmenu, SC_CLOSE, MF_ENABLED, IDM_NEW, 0);
    if (cfg.tabbar)
      InsertMenuW(sysmenu, SC_CLOSE, MF_ENABLED, IDM_TAB, 0);
  }

  InsertMenuW(sysmenu, SC_CLOSE, MF_SEPARATOR, 0, 0);

  sysmenulen = GetMenuItemCount(sysmenu);
}

static void
open_popup_menu(bool use_text_cursor, string menucfg, mod_keys mods)
{
  //printf("open_popup_menu txtcur %d <%s> %X\n", use_text_cursor, menucfg, mods);
  /* Create a new context menu structure every time the menu is opened.
     This was a fruitless attempt to achieve its proper DPI scaling.
     It also supports opening different menus (Ctrl+ for extended menu).
     if (mods & MDK_CTRL) open extended menu...
   */
  if (ctxmenu)
    DestroyMenu(ctxmenu);

  ctxmenu = CreatePopupMenu();
  //show_menu_info(ctxmenu);

  if (!menucfg) {
    if (mods & MDK_ALT)
      menucfg = *cfg.menu_altmouse ? cfg.menu_altmouse : "ls";
    else if (mods & MDK_CTRL)
      menucfg = *cfg.menu_ctrlmouse ? cfg.menu_ctrlmouse : "e|ls";
    else
      menucfg = *cfg.menu_mouse ? cfg.menu_mouse : "b";
  }

  bool vsep = false;
  bool hsep = false;
  bool init = false;
  bool wicons = strchr(menucfg, 'W');
  while (*menucfg) {
    if (*menucfg == '|')
      // Windows mangles the menu style if the flag MF_MENUBARBREAK is used 
      // as triggered by vsep...
      vsep = true;
    else if (!strchr(menucfg + 1, *menucfg)) {
      // suppress duplicates except separators
      bool ok = true;
      switch (*menucfg) {
        when 'b': if (!init) {
                    win_init_ctxmenu(false, false);
                    init = true;
                  }
        when 'x': if (!init) {
                    win_init_ctxmenu(true, false);
                    init = true;
                  }
        when 'e': if (!init) {
                    win_init_ctxmenu(true, true);
                    init = true;
                  }
        when 'u': ok = add_user_commands(ctxmenu, vsep, hsep & !vsep,
                                         null,
                                         cfg.ctx_user_commands, IDM_CTXMENUFUNCTION
                                         )
                       ||
                       add_user_commands(ctxmenu, vsep, hsep & !vsep,
                                         //__ Context menu, user commands
                                         _W("User commands"),
                                         cfg.user_commands, IDM_USERCOMMAND
                                         );
        when 'W': wicons = true;
        when 's': add_switcher(ctxmenu, vsep, hsep & !vsep, wicons);
        when 'l': ok = add_launcher(ctxmenu, vsep, hsep & !vsep);
        when 'T': use_text_cursor = true;
        when 'P': use_text_cursor = false;
      }
      if (ok) {
        vsep = false;
        hsep = true;
      }
    }
    menucfg++;
  }
  win_update_menus(false);  // dispensable; also called via WM_INITMENU
  //show_menu_info(ctxmenu);

  POINT p;
  if (use_text_cursor) {
    GetCaretPos(&p);
    ClientToScreen(wnd, &p);
  }
  else
    GetCursorPos(&p);

  TrackPopupMenu(
    ctxmenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
    p.x, p.y, 0, wnd, null
  );
}

void
win_popup_menu(mod_keys mods)
{
  open_popup_menu(false, null, mods);
}

bool
win_title_menu(bool leftbut)
{
  string title_menu = leftbut ? cfg.menu_title_ctrl_l : cfg.menu_title_ctrl_r;
  if (*title_menu) {
    open_popup_menu(false, title_menu, 0);
    return true;
  }
  else
    return false;
}


/* Mouse and Keyboard modifiers */

typedef enum {
  ALT_CANCELLED = -1, ALT_NONE = 0, ALT_ALONE = 1,
  ALT_OCT = 8, ALT_DEC = 10, ALT_HEX = 16
} alt_state_t;
static alt_state_t alt_state;
static alt_state_t old_alt_state;
static int alt_code;
static bool alt_uni;

static bool lctrl;  // Is left Ctrl pressed?
static int lctrl_time;

mod_keys
get_mods(void)
{
  inline bool is_key_down(uchar vk) { return GetKeyState(vk) & 0x80; }
  lctrl_time = 0;
  lctrl = is_key_down(VK_LCONTROL) && (lctrl || !is_key_down(VK_RMENU));
  bool super = super_key && is_key_down(super_key);
  bool hyper = hyper_key && is_key_down(hyper_key);
  return
    is_key_down(VK_SHIFT) * MDK_SHIFT
    | is_key_down(VK_MENU) * MDK_ALT
    | (lctrl | is_key_down(VK_RCONTROL)) * MDK_CTRL
    | (is_key_down(VK_LWIN) | is_key_down(VK_RWIN)) * MDK_WIN
    | super * MDK_SUPER
    | hyper * MDK_HYPER
    ;
}


/* Mouse handling */

static void
update_mouse(mod_keys mods)
{
static bool last_app_mouse = false;

  // unhover (end hovering) if hover modifiers are withdrawn
  if (term.hovering && (char)(mods & ~cfg.click_target_mod) != cfg.opening_mod) {
    term.hovering = false;
    win_update(false);
  }

  bool new_app_mouse =
    (term.mouse_mode || term.locator_1_enabled)
    // disable app mouse pointer while showing "other" screen (flipped)
    && !term.show_other_screen
    // disable app mouse pointer while not targetting app
    && (cfg.clicks_target_app ^ ((mods & cfg.click_target_mod) != 0));

  if (new_app_mouse != last_app_mouse) {
    //HCURSOR cursor = LoadCursor(null, new_app_mouse ? IDC_ARROW : IDC_IBEAM);
    HCURSOR cursor = win_get_cursor(new_app_mouse);
    SetClassLongPtr(wnd, GCLP_HCURSOR, (LONG_PTR)cursor);
    SetCursor(cursor);
    last_app_mouse = new_app_mouse;
  }
}

void
win_update_mouse(void)
{ update_mouse(get_mods()); }

void
win_capture_mouse(void)
{ SetCapture(wnd); }

static bool mouse_showing = true;

void
win_show_mouse(void)
{
  if (!mouse_showing) {
    ShowCursor(true);
    mouse_showing = true;
  }
}

static void
hide_mouse(void)
{
  POINT p;
  if (term.hide_mouse && mouse_showing && GetCursorPos(&p) && WindowFromPoint(p) == wnd) {
    ShowCursor(false);
    mouse_showing = false;
  }
}

static pos
translate_pos(int x, int y)
{
  int rows = term.rows;
  if (term.st_active) {
    rows = term.st_rows;
    y = max(0, y - term.rows * cell_height);
  }
  return (pos){
    .x = floorf((x - PADDING) / (float)cell_width),
    .y = floorf((y - PADDING - OFFSET) / (float)cell_height),
    .pix = min(max(0, x - PADDING), term.cols * cell_width - 1),
    .piy = min(max(0, y - PADDING - OFFSET), rows * cell_height - 1),
    .r = (cfg.elastic_mouse && !term.mouse_mode)
         ? (x - PADDING) % cell_width > cell_width / 2
         : 0
  };
}

pos last_pos = {-1, -1, -1, -1, false};
static LPARAM last_lp = -1;
static int button_state = 0;

bool click_focus_token = false;
static mouse_button last_button = -1;
static mod_keys last_mods;
static pos last_click_pos;
static bool last_skipped = false;
static mouse_button skip_release_token = -1;
static uint last_skipped_time;
static bool mouse_state = false;

static pos
get_mouse_pos(LPARAM lp)
{
  last_lp = lp;
  return translate_pos(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
}

bool
win_mouse_click(mouse_button b, LPARAM lp)
{
  mouse_state = true;
  bool click_focus = click_focus_token;
  click_focus_token = false;

  static uint last_time, count;

  win_show_mouse();
  mod_keys mods = get_mods();
  pos p = get_mouse_pos(lp);

  uint t = GetMessageTime();
  bool dblclick = b == last_button
                  && p.x == last_click_pos.x && p.y == last_click_pos.y
                  && t - last_time <= GetDoubleClickTime();
  if (!dblclick || ++count > 3)
    count = 1;
  //printf("mouse %d (focus %d skipped %d) ×%d\n", b, click_focus, last_skipped, count);

  SetFocus(wnd);  // in case focus was in search bar

  bool res = false;

  if (click_focus && b == MBT_LEFT && count == 1
      && // not in application mouse mode
         !(term.mouse_mode && term.report_focus &&
           cfg.clicks_target_app ^ ((mods & cfg.click_target_mod) != 0)
          )
     ) {
    //printf("suppressing focus-click selection, t %d\n", t);
    // prevent accidental selection when focus-clicking into the window (#717)
    last_skipped = true;
    last_skipped_time = t;
    skip_release_token = b;
    res = true;
  }
  else {
    if (last_skipped && dblclick) {
      // recognize double click also in application mouse modes
      term_mouse_click(b, mods, p, 1);
    }
    res = term_mouse_click(b, mods, p, count);
    last_skipped = false;
  }
  last_pos = (pos){INT_MIN, INT_MIN, INT_MIN, INT_MIN, false};
  last_click_pos = p;
  last_time = t;
  last_button = b;
  last_mods = mods;

  if (alt_state > ALT_NONE)
    alt_state = ALT_CANCELLED;
  switch (b) {
    when MBT_RIGHT:
      button_state |= 1;
    when MBT_MIDDLE:
      button_state |= 2;
    when MBT_LEFT:
      button_state |= 4;
    when MBT_4:
      button_state |= 8;
    otherwise:;
  }

  return res;
}

void
win_mouse_release(mouse_button b, LPARAM lp)
{
  mouse_state = false;

  if (b == skip_release_token) {
    skip_release_token = -1;
    return;
  }

  term_mouse_release(b, get_mods(), get_mouse_pos(lp));
  ReleaseCapture();
  switch (b) {
    when MBT_RIGHT:
      button_state &= ~1;
    when MBT_MIDDLE:
      button_state &= ~2;
    when MBT_LEFT:
      button_state &= ~4;
    when MBT_4:
      button_state &= ~8;
    otherwise:;
  }
}

void
win_mouse_move(bool nc, LPARAM lp)
{
  if (tek_mode == TEKMODE_GIN) {
    int y = GET_Y_LPARAM(lp) - PADDING - OFFSET;
    int x = GET_X_LPARAM(lp) - PADDING;
    tek_move_to(y, x);
    return;
  }

  if (lp == last_lp)
    return;

  win_show_mouse();

  pos p = get_mouse_pos(lp);
  if (nc || (p.x == last_pos.x && p.y == last_pos.y && p.r == last_pos.r))
    return;
  if (last_skipped && last_button == MBT_LEFT && mouse_state) {
    // allow focus-selection if distance spanned 
    // is large enough or with sufficient delay (#717)
    uint dist = sqrt(sqr(p.x - last_click_pos.x) + sqr(p.y - last_click_pos.y));
    uint diff = GetMessageTime() - last_skipped_time;
    //printf("focus move %d %d\n", dist, diff);
    if (dist * diff > 999) {
      term_mouse_click(last_button, last_mods, last_click_pos, 1);
      last_skipped = false;
      skip_release_token = -1;
    }
  }

  last_pos = p;
  term_mouse_move(get_mods(), p);
}

void
win_mouse_wheel(POINT wpos, bool horizontal, int delta)
{
  pos tpos = translate_pos(wpos.x, wpos.y);

  int lines_per_notch;
  SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines_per_notch, 0);

  term_mouse_wheel(horizontal, delta, lines_per_notch, get_mods(), tpos);
}

void
win_get_locator_info(int *x, int *y, int *buttons, bool by_pixels)
{
  POINT p = {-1, -1};

  if (GetCursorPos(&p)) {
    if (ScreenToClient(wnd, &p)) {
      if (p.x < PADDING)
        p.x = 0;
      else
        p.x -= PADDING;
      if (p.x >= term.cols * cell_width)
        p.x = term.cols * cell_width - 1;
      if (p.y < OFFSET + PADDING)
        p.y = 0;
      else
        p.y -= OFFSET + PADDING;
      if (term.st_active) {
        p.y = max(0, p.y - term.rows * cell_height);
        if (p.y >= term.st_rows * cell_height)
          p.y = term.st_rows * cell_height - 1;
      }
      else if (p.y >= term.rows * cell_height)
        p.y = term.rows * cell_height - 1;

      if (by_pixels) {
        *x = p.x;
        *y = p.y;
      } else {
        *x = floorf(p.x / (float)cell_width);
        *y = floorf(p.y / (float)cell_height);
      }
    }
  }

  *buttons = button_state;
}


/* Support functions */

static void
toggle_scrollbar(void)
{
  if (cfg.scrollbar) {
    term.show_scrollbar = !term.show_scrollbar;
    win_update_scrollbar(true);
  }
}

static int previous_transparency;
static bool transparency_tuned;

#define dont_debug_transparency

static void
cycle_transparency(void)
{
  cfg.transparency = ((cfg.transparency + 16) / 16 * 16) % 128;
  win_update_transparency(cfg.transparency, false);
}

static void
set_transparency(int t)
{
  if (t >= 128)
    t = 127;
  else if (t < 0)
    t = 0;
  cfg.transparency = t;
  win_update_transparency(t, false);
}

static void
cycle_pointer_style()
{
  cfg.cursor_type = (cfg.cursor_type + 1) % 3;
  term.cursor_invalid = true;
  term_schedule_cblink();
  win_update(false);
}


/*
   Some auxiliary functions for user-defined key assignments.
 */

static void
unicode_char()
{
  alt_state = ALT_HEX;
  old_alt_state = ALT_ALONE;
  alt_code = 0;
  alt_uni = true;
}

static void
menu_text()
{
  open_popup_menu(true, null, get_mods());
}

static void
menu_pointer()
{
  //win_popup_menu(get_mods());
  open_popup_menu(false, null, get_mods());
}

static void
transparency_level()
{
  if (!transparency_pending) {
    previous_transparency = cfg.transparency;
    transparency_pending = 1;
    transparency_tuned = false;
  }
  if (cfg.opaque_when_focused)
    win_update_transparency(cfg.transparency, false);
}

static void
toggle_opaque()
{
  force_opaque = !force_opaque;
  win_update_transparency(cfg.transparency, force_opaque);
}

static void
newwin_begin(uint key, mod_keys mods)
{
  if (key) {
    newwin_pending = true;
    newwin_home = false; newwin_monix = 0; newwin_moniy = 0;

    newwin_key = key;
    if (mods & MDK_SHIFT)
      newwin_shifted = true;
    else
      newwin_shifted = false;
  }
}

static void
window_full()
{
  win_maximise(2);
}

static void
window_max()
{
  win_maximise(1);
}

static void
window_toggle_max()
{
  if (IsZoomed(wnd))
    win_maximise(0);
  else
    win_maximise(1);
}

static void
win_toggle_screen_on()
{
  win_keep_screen_on(!keep_screen_on);
}

static void
window_restore()
{
  win_maximise(0);
}

static void
window_min()
{
  win_set_iconic(true);
}

void
toggle_vt220()
{
  term.vt220_keys = !term.vt220_keys;
}

void
toggle_auto_repeat()
{
  term.auto_repeat = !term.auto_repeat;
}

void
toggle_bidi()
{
  term.disable_bidi = !term.disable_bidi;
}

void
toggle_dim_margins()
{
  term.dim_margins = !term.dim_margins;
}

void
toggle_status_line()
{
  if (term.st_type == 1)
    term_set_status_type(0, 0);
  else if (term.st_type == 0)
    term_set_status_type(1, 0);
}

static void scroll_HOME()
  { SendMessage(wnd, WM_VSCROLL, SB_TOP, 0); }
static void scroll_END()
  { SendMessage(wnd, WM_VSCROLL, SB_BOTTOM, 0); }
static void scroll_PRIOR()
  { SendMessage(wnd, WM_VSCROLL, SB_PAGEUP, 0); }
static void scroll_NEXT()
  { SendMessage(wnd, WM_VSCROLL, SB_PAGEDOWN, 0); }
static void scroll_UP()
  { SendMessage(wnd, WM_VSCROLL, SB_LINEUP, 0); }
static void scroll_DOWN()
  { SendMessage(wnd, WM_VSCROLL, SB_LINEDOWN, 0); }
static void scroll_LEFT()
  { SendMessage(wnd, WM_VSCROLL, SB_PRIOR, 0); }
static void scroll_RIGHT()
  { SendMessage(wnd, WM_VSCROLL, SB_NEXT, 0); }

static void switch_NEXT()
  { win_switch(false, true); }
static void switch_PREV()
  { win_switch(true, true); }
static void switch_visible_NEXT()
  { win_switch(false, false); }
static void switch_visible_PREV()
  { win_switch(true, false); }

static void
nop()
{
}

static uint
mflags_copy()
{
  return term.selected ? MF_ENABLED : MF_GRAYED;
}

static uint
mflags_paste()
{
  return
    IsClipboardFormatAvailable(CF_TEXT) ||
    IsClipboardFormatAvailable(CF_UNICODETEXT) ||
    IsClipboardFormatAvailable(CF_HDROP)
    ? MF_ENABLED : MF_GRAYED;
}

static void
lock_title()
{
  title_settable = false;
}

static void
clear_title()
{
  win_set_title("");
}

static void
refresh()
{
  win_invalidate_all(false);
}

static void
super_down(uint key, mod_keys mods)
{
  super_key = key;
  (void)mods;
}

static void
hyper_down(uint key, mod_keys mods)
{
  hyper_key = key;
  (void)mods;
}

static void
compose_down(uint key, mod_keys mods)
{
  compose_key = key;
  last_key_down = key;
  (void)mods;
}

static void
kb_select(uint key, mod_keys mods)
{
  (void)mods;
  (void)key;
  // note kb_select_key for re-anchor handling?
  //kb_select_key = key;

  // start and anchor keyboard selection
  term.sel_pos = (pos){.y = term.curs.y, .x = term.curs.x, .r = 0};
  term.sel_anchor = term.sel_pos;
  term.sel_start = term.sel_pos;
  term.sel_end = term.sel_pos;
  term.sel_rect = mods & MDK_ALT;
  selection_pending = true;
}

static uint
mflags_kb_select()
{
  return selection_pending;
}

static void
no_scroll(uint key, mod_keys mods)
{
  (void)mods;
  (void)key;
  if (!term.no_scroll) {
    term.no_scroll = -1;
    sync_scroll_lock(true);
    win_prefix_title(_W("[NO SCROLL] "));
    term_flush();
  }
}

static void
scroll_mode(uint key, mod_keys mods)
{
  (void)mods;
  (void)key;
  if (!term.scroll_mode) {
    term.scroll_mode = -1;
    sync_scroll_lock(true);
    win_prefix_title(_W("[SCROLL MODE] "));
    term_flush();
  }
}

static void
refresh_scroll_title()
{
  win_unprefix_title(_W("[NO SCROLL] "));
  win_unprefix_title(_W("[SCROLL MODE] "));
  win_unprefix_title(_W("[NO SCROLL] "));
  if (term.no_scroll)
    win_prefix_title(_W("[NO SCROLL] "));
  if (term.scroll_mode)
    win_prefix_title(_W("[SCROLL MODE] "));
}

static void
clear_scroll_lock()
{
  bool scrlock0 = term.no_scroll || term.scroll_mode;
  if (term.no_scroll < 0) {
    term.no_scroll = 0;
  }
  if (term.scroll_mode < 0) {
    term.scroll_mode = 0;
  }
  bool scrlock = term.no_scroll || term.scroll_mode;
  if (scrlock != scrlock0) {
    sync_scroll_lock(term.no_scroll || term.scroll_mode);
    refresh_scroll_title();
  }
}

static void
toggle_no_scroll(uint key, mod_keys mods)
{
#ifdef debug_vk_scroll
  printf("toggle_no_scroll\n");
#endif
  (void)mods;
  (void)key;
  term.no_scroll = !term.no_scroll;
  sync_scroll_lock(term.no_scroll || term.scroll_mode);
  if (!term.no_scroll) {
    refresh_scroll_title();
    term_flush();
  }
  else
    win_prefix_title(_W("[NO SCROLL] "));
}

static uint
mflags_no_scroll()
{
  return term.no_scroll ? MF_CHECKED : MF_UNCHECKED;
}

static void
toggle_scroll_mode(uint key, mod_keys mods)
{
  (void)mods;
  (void)key;
  term.scroll_mode = !term.scroll_mode;
  sync_scroll_lock(term.no_scroll || term.scroll_mode);
  if (!term.scroll_mode) {
    refresh_scroll_title();
    term_flush();
  }
  else
    win_prefix_title(_W("[SCROLL MODE] "));
}

static uint
mflags_scroll_mode()
{
  return term.scroll_mode ? MF_CHECKED : MF_UNCHECKED;
}

static uint
mflags_lock_title()
{
  return title_settable ? MF_ENABLED : MF_GRAYED;
}

static uint
mflags_defsize()
{
  return
    IsZoomed(wnd) || term.cols != cfg.cols || term.rows != cfg.rows
    ? MF_ENABLED : MF_GRAYED;
}

static uint
mflags_fullscreen()
{
  return win_is_fullscreen ? MF_CHECKED : MF_UNCHECKED;
}

static uint
mflags_zoomed()
{
  return IsZoomed(wnd) ? MF_CHECKED: MF_UNCHECKED;
}

static uint
mflags_always_top()
{
  return win_is_always_on_top ? MF_CHECKED: MF_UNCHECKED;
}

static uint
mflags_screen_on()
{
  return keep_screen_on ? MF_CHECKED: MF_UNCHECKED;
}

static uint
mflags_flipscreen()
{
  return term.show_other_screen ? MF_CHECKED : MF_UNCHECKED;
}

static uint
mflags_scrollbar_outer()
{
  return term.show_scrollbar ? MF_CHECKED : MF_UNCHECKED
#ifdef allow_disabling_scrollbar
         | cfg.scrollbar ? 0 : MF_GRAYED
#endif
  ;
}

static uint
mflags_scrollbar_inner()
{
  if (cfg.scrollbar)
    return term.show_scrollbar ? MF_CHECKED : MF_UNCHECKED;
  else
    return MF_GRAYED;
}

static uint
mflags_opaque()
{
  return force_opaque ? MF_CHECKED : MF_UNCHECKED;
}

static uint
mflags_open()
{
  return term.selected ? MF_ENABLED : MF_GRAYED;
}

static uint
mflags_logging()
{
  return ((logging || *cfg.log) ? MF_ENABLED : MF_GRAYED)
       | (logging ? MF_CHECKED : MF_UNCHECKED)
  ;
}

static uint
mflags_char_info()
{
  return show_charinfo ? MF_CHECKED : MF_UNCHECKED;
}

static uint
mflags_vt220()
{
  return term.vt220_keys ? MF_CHECKED : MF_UNCHECKED;
}

static uint
mflags_auto_repeat()
{
  return term.auto_repeat ? MF_CHECKED : MF_UNCHECKED;
}

static uint
mflags_bidi()
{
  return (cfg.bidi == 0
         || (cfg.bidi == 1 && (term.on_alt_screen ^ term.show_other_screen))
         ) ? MF_GRAYED
           : term.disable_bidi ? MF_UNCHECKED : MF_CHECKED;
}

static uint
mflags_dim_margins()
{
  return term.dim_margins ? MF_CHECKED : MF_UNCHECKED;
}

static uint
mflags_status_line()
{
  return term.st_type == 1 ? MF_CHECKED
       : term.st_type == 0 ? MF_UNCHECKED
       : MF_GRAYED;
}

static uint
mflags_options()
{
  return config_wnd ? MF_GRAYED : MF_ENABLED;
}

static uint
mflags_tek_mode()
{
  return tek_mode ? MF_ENABLED : MF_GRAYED;
}

static uint
mflags_tabbar()
{
  return win_tabbar_visible() ? MF_CHECKED : MF_UNCHECKED;
}

static void
toggle_tabbar()
{
  cfg.tabbar = !cfg.tabbar;
  if (cfg.tabbar)
    win_open_tabbar();
  else
    win_close_tabbar();
}

static void hor_left_1() { horscroll(-1); }
static void hor_right_1() { horscroll(1); }
static void hor_out_1() { horsizing(1, false); }
static void hor_in_1() { horsizing(-1, false); }
static void hor_narrow_1() { horsizing(-1, true); }
static void hor_wide_1() { horsizing(1, true); }
static void hor_left_mult() { horscroll(-term.cols / 10); }
static void hor_right_mult() { horscroll(term.cols / 10); }
static void hor_out_mult() { horsizing(term.cols / 10, false); }
static void hor_in_mult() { horsizing(-term.cols / 10, false); }
static void hor_narrow_mult() { horsizing(-term.cols / 10, true); }
static void hor_wide_mult() { horsizing(term.cols / 10, true); }

// user-definable functions
static struct function_def cmd_defs[] = {
#ifdef support_sc_defs
#warning these do not work, they crash
  {"restore", {SC_RESTORE}, 0},
  {"move", {SC_MOVE}, 0},
  {"resize", {SC_SIZE}, 0},
  {"minimize", {SC_MINIMIZE}, 0},
  {"maximize", {SC_MAXIMIZE}, 0},
  {"menu", {SC_KEYMENU}, 0},
  {"close", {SC_CLOSE}, 0},
#endif

  {"new-window", {IDM_NEW}, 0},
  {"new-window-cwd", {IDM_NEW_CWD}, 0},
  {"new-tab", {IDM_TAB}, 0},
  {"new-tab-cwd", {IDM_TAB_CWD}, 0},
  {"toggle-tabbar", {.fct = toggle_tabbar}, mflags_tabbar},

  {"hor-left-1", {.fct = hor_left_1}, 0},
  {"hor-right-1", {.fct = hor_right_1}, 0},
  {"hor-out-1", {.fct = hor_out_1}, 0},
  {"hor-in-1", {.fct = hor_in_1}, 0},
  {"hor-narrow-1", {.fct = hor_narrow_1}, 0},
  {"hor-wide-1", {.fct = hor_wide_1}, 0},
  {"hor-left-mult", {.fct = hor_left_mult}, 0},
  {"hor-right-mult", {.fct = hor_right_mult}, 0},
  {"hor-out-mult", {.fct = hor_out_mult}, 0},
  {"hor-in-mult", {.fct = hor_in_mult}, 0},
  {"hor-narrow-mult", {.fct = hor_narrow_mult}, 0},
  {"hor-wide-mult", {.fct = hor_wide_mult}, 0},

  //{"default-size", {IDM_DEFSIZE}, 0},
  {"default-size", {IDM_DEFSIZE_ZOOM}, mflags_defsize},
  {"toggle-fullscreen", {IDM_FULLSCREEN_ZOOM}, mflags_fullscreen},
  {"fullscreen", {.fct = window_full}, mflags_fullscreen},
  {"win-max", {.fct = window_max}, mflags_zoomed},
  {"win-toggle-max", {.fct = window_toggle_max}, mflags_zoomed},
  {"win-restore", {.fct = window_restore}, 0},
  {"win-icon", {.fct = window_min}, 0},
  {"close", {.fct = win_close}, 0},
  {"win-toggle-always-on-top", {.fct = win_toggle_on_top}, mflags_always_top},
  {"win-toggle-keep-screen-on", {.fct = win_toggle_screen_on}, mflags_screen_on},

  {"unicode-char", {.fct = unicode_char}, 0},

  {"new", {.fct_key = newwin_begin}, 0},  // deprecated
  {"new-key", {.fct_key = newwin_begin}, 0},
  {"options", {IDM_OPTIONS}, mflags_options},
  {"menu-text", {.fct = menu_text}, 0},
  {"menu-pointer", {.fct = menu_pointer}, 0},

  {"search", {IDM_SEARCH}, 0},
  {"scrollbar-outer", {IDM_SCROLLBAR}, mflags_scrollbar_outer},
  {"scrollbar-inner", {.fct = toggle_scrollbar}, mflags_scrollbar_inner},
  {"cycle-pointer-style", {.fct = cycle_pointer_style}, 0},
  {"cycle-transparency-level", {.fct = transparency_level}, 0},
  {"toggle-opaque", {.fct = toggle_opaque}, mflags_opaque},

  {"copy", {IDM_COPY}, mflags_copy},
  {"copy-text", {IDM_COPY_TEXT}, mflags_copy},
  {"copy-tabs", {IDM_COPY_TABS}, mflags_copy},
  {"copy-plain", {IDM_COPY_TXT}, mflags_copy},
  {"copy-rtf", {IDM_COPY_RTF}, mflags_copy},
  {"copy-html-text", {IDM_COPY_HTXT}, mflags_copy},
  {"copy-html-format", {IDM_COPY_HFMT}, mflags_copy},
  {"copy-html-full", {IDM_COPY_HTML}, mflags_copy},
  {"paste", {IDM_PASTE}, mflags_paste},
  {"paste-path", {.fct = win_paste_path}, mflags_paste},
  {"copy-paste", {IDM_COPASTE}, mflags_copy},
  {"select-all", {IDM_SELALL}, 0},
  {"clear-scrollback", {IDM_CLRSCRLBCK}, 0},
  {"copy-title", {IDM_COPYTITLE}, 0},
  {"lock-title", {.fct = lock_title}, mflags_lock_title},
  {"clear-title", {.fct = clear_title}, 0},
  {"reset", {IDM_RESET}, 0},
  {"reset-noask", {IDM_RESET_NOASK}, 0},
  {"tek-reset", {IDM_TEKRESET}, mflags_tek_mode},
  {"tek-page", {IDM_TEKPAGE}, mflags_tek_mode},
  {"tek-copy", {IDM_TEKCOPY}, mflags_tek_mode},
  {"save-image", {IDM_SAVEIMG}, 0},
  {"break", {IDM_BREAK}, 0},
  {"flipscreen", {IDM_FLIPSCREEN}, mflags_flipscreen},
  {"open", {IDM_OPEN}, mflags_open},
  {"toggle-logging", {IDM_TOGLOG}, mflags_logging},
  {"toggle-char-info", {IDM_TOGCHARINFO}, mflags_char_info},
  {"export-html", {IDM_HTML}, 0},
  {"print-screen", {.fct = print_screen}, 0},
  {"toggle-vt220", {.fct = toggle_vt220}, mflags_vt220},
  {"toggle-auto-repeat", {.fct = toggle_auto_repeat}, mflags_auto_repeat},
  {"toggle-bidi", {.fct = toggle_bidi}, mflags_bidi},
  {"refresh", {.fct = refresh}, 0},
  {"toggle-dim-margins", {.fct = toggle_dim_margins}, mflags_dim_margins},
  {"toggle-status-line", {.fct = toggle_status_line}, mflags_status_line},

  {"super", {.fct_key = super_down}, 0},
  {"hyper", {.fct_key = hyper_down}, 0},
  {"compose", {.fct_key = compose_down}, 0},
  {"kb-select", {.fct_key = kb_select}, mflags_kb_select},
  {"no-scroll", {.fct_key = no_scroll}, mflags_no_scroll},
  {"toggle-no-scroll", {.fct_key = toggle_no_scroll}, mflags_no_scroll},
  {"scroll-mode", {.fct_key = scroll_mode}, mflags_scroll_mode},
  {"toggle-scroll-mode", {.fct_key = toggle_scroll_mode}, mflags_scroll_mode},

  {"scroll_top", {.fct = scroll_HOME}, 0},
  {"scroll_end", {.fct = scroll_END}, 0},
  {"scroll_pgup", {.fct = scroll_PRIOR}, 0},
  {"scroll_pgdn", {.fct = scroll_NEXT}, 0},
  {"scroll_lnup", {.fct = scroll_UP}, 0},
  {"scroll_lndn", {.fct = scroll_DOWN}, 0},
  {"scroll_prev", {.fct = scroll_LEFT}, 0},
  {"scroll_next", {.fct = scroll_RIGHT}, 0},

  {"switch-prev", {.fct = switch_PREV}, 0},
  {"switch-next", {.fct = switch_NEXT}, 0},
  {"switch-visible-prev", {.fct = switch_visible_PREV}, 0},
  {"switch-visible-next", {.fct = switch_visible_NEXT}, 0},

  {"void", {.fct = nop}, 0}
};

static struct function_def *
function_def(char * cmd)
{
  for (uint i = 0; i < lengthof(cmd_defs); i++)
    if (!strcmp(cmd, cmd_defs[i].name))
      return &cmd_defs[i];
  return 0;
}


/* Keyboard handling */

static void
send_syscommand2(WPARAM cmd, LPARAM p)
{
  SendMessage(wnd, WM_SYSCOMMAND, cmd, p);
}

static void
send_syscommand(WPARAM cmd)
{
  SendMessage(wnd, WM_SYSCOMMAND, cmd, ' ');
}


typedef enum {
  COMP_CLEAR = -1,
  COMP_NONE = 0,
  COMP_PENDING = 1, COMP_ACTIVE = 2
} comp_state_t;
static comp_state_t comp_state = COMP_NONE;

static struct {
  wchar kc[4];
  char * s;
} composed[] = {
#include "composed.t"
};
static wchar compose_buf[lengthof(composed->kc) + 4];
static int compose_buflen = 0;

void
compose_clear(void)
{
  comp_state = COMP_CLEAR;
  compose_buflen = 0;
  last_key_down = 0;
  last_key_up = 0;
}

void
win_key_reset(void)
{
  alt_state = ALT_NONE;
  compose_clear();
}

wchar *
char_code_indication(uint * what)
{
static wchar cci_buf[13];

  if (alt_state > ALT_ALONE) {
    int ac = alt_code;
    int i = lengthof(cci_buf);
    cci_buf[--i] = 0;
    do {
      int digit = ac % alt_state;
      cci_buf[--i] = digit > 9 ? digit - 10 + 'A' : digit + '0';
      ac /= alt_state;
    } while (ac && i);
    if (alt_state == ALT_HEX && alt_uni && i > 1) {
      cci_buf[--i] = '+';
      cci_buf[--i] = 'U';
    }
    *what = alt_state;
    return &cci_buf[i];
  }
  else if (alt_state == ALT_ALONE) {
    *what = 4;
    //return W(" ");
    return 0;  // don't obscure text when just pressing Alt
  }
  else if (comp_state > COMP_NONE) {
    int i;
    for (i = 0; i < compose_buflen; i++)
      cci_buf[i] = compose_buf[i];
    cci_buf[i++] = ' ';
    cci_buf[i] = 0;
    *what = 2;
    return cci_buf;
  }
  else
    return 0;
}

// notify margin bell ring enabled
void
provide_input(wchar c1)
{
  if (term.margin_bell && c1 != '\e')
    term.ring_enabled = true;
}

#define dont_debug_virtual_key_codes
#define dont_debug_key
#define dont_debug_alt
#define dont_debug_compose

#ifdef debug_virtual_key_codes
static struct {
  uint vk_;
  char * vk_name;
} vk_names[] = {
#include "_vk.t"
};

static string
vk_name(uint key)
{
  for (uint i = 0; i < lengthof(vk_names); i++)
    if (key == vk_names[i].vk_)
      return vk_names[i].vk_name;
  static char vk_name[3];
  sprintf(vk_name, "%02X", key & 0xFF);
  return vk_name;
}
#endif

#ifdef debug_compose
# define debug_key
#endif

#ifdef debug_key
#define trace_key(tag)	printf(" key(%s)\n", tag)
#else
#define trace_key(tag)	(void)0
#endif

#ifdef debug_alt
#define trace_alt	printf
#else
#define trace_alt(...)	
#endif

// key names for user-definable functions
static struct {
  uchar vkey;
  char unmod;
  string nam;
} vktab[] = {
  {VK_CANCEL, 0, "Break"},
  {VK_BACK, 1, "Back"},
  {VK_TAB, 0, "Tab"},
  {VK_RETURN, 0, "Enter"},
  {VK_PAUSE, 1, "Pause"},
  {VK_ESCAPE, 0, "Esc"},
  {VK_SPACE, 0, "Space"},
  {VK_SNAPSHOT, 1, "PrintScreen"},
  {VK_LWIN, 1, "LWin"},
  {VK_RWIN, 1, "RWin"},
  {VK_APPS, 1, "Menu"},
  {VK_NUMLOCK, 1, "NumLock"},
  {VK_CAPITAL, 1, "CapsLock"},
  {VK_SCROLL, 1, "ScrollLock"},
  // exotic keys:
  {VK_SELECT, 1, "Select"},
  {VK_PRINT, 1, "Print"},
  {VK_EXECUTE, 1, "Exec"},
  {VK_HELP, 1, "Help"},
  {VK_SLEEP, 1, "Sleep"},
  {VK_ATTN, 1, "Attn"},
  {VK_CRSEL, 1, "CrSel"},
  {VK_EXSEL, 1, "ExSel"},
  {VK_EREOF, 1, "ErEof"},
  {VK_PLAY, 1, "Play"},
  {VK_ZOOM, 1, "Zoom"},
  // cursor keys, editing keypad, and numeric keypad application keys
  {VK_INSERT, 2, "Insert"},
  {VK_DELETE, 2, "Delete"},
  {VK_HOME, 2, "Home"},
  {VK_END, 2, "End"},
  {VK_PRIOR, 2, "Prior"},
  {VK_NEXT, 2, "Next"},
  {VK_LEFT, 2, "Left"},
  {VK_RIGHT, 2, "Right"},
  {VK_UP, 2, "Up"},
  {VK_DOWN, 2, "Down"},
  {VK_CLEAR, 2, "Begin"},
  {VK_DIVIDE, 3, "Divide"},
  {VK_MULTIPLY, 3, "Multiply"},
  {VK_SUBTRACT, 3, "Subtract"},
  {VK_ADD, 3, "Add"},
};

// simulate a key release/press sequence; reverse of win_key_fake()
static int
win_key_nullify(uchar vk)
{
  if (!cfg.manage_leds || (cfg.manage_leds < 4 && vk == VK_SCROLL))
    return 0;

#ifdef heuristic_detection_of_ScrollLock_auto_repeat_glitch
  if (vk == VK_SCROLL) {
    int st = GetKeyState(VK_SCROLL);
    //printf("win_key_nullify st %d key %d\n", term.no_scroll || term.scroll_mode, st);
    // heuristic detection of race condition with auto-repeat
    // without setting KeyFunctions=ScrollLock:toggle-no-scroll;
    // handled in common with heuristic compensation in win_key_up
    if ((st & 1) == (term.no_scroll || term.scroll_mode)) {
      return 0;  // nothing sent
    }
  }
#endif

  INPUT ki[2];
  ki[0].type = INPUT_KEYBOARD;
  ki[1].type = INPUT_KEYBOARD;
  ki[0].ki.dwFlags = KEYEVENTF_KEYUP;
  ki[1].ki.dwFlags = 0;
  ki[0].ki.wVk = vk;
  ki[1].ki.wVk = vk;
  ki[0].ki.wScan = 0;
  ki[1].ki.wScan = 0;
  ki[0].ki.time = 0;
  ki[1].ki.time = 0;
  ki[0].ki.dwExtraInfo = 0;
  ki[1].ki.dwExtraInfo = 0;
  return SendInput(2, ki, sizeof(INPUT));
}

#define dont_debug_def_keys 1

static int
pick_key_function(wstring key_commands, char * tag, int n, uint key, mod_keys mods, mod_keys mod0, uint scancode)
{
  char * ukey_commands = cs__wcstoutf(key_commands);
  char * cmdp = ukey_commands;
  char sepch = ';';
  if ((uchar)*cmdp <= (uchar)' ')
    sepch = *cmdp++;
#ifdef debug_def_keys
  printf("pick_key_function (%s) <%s> %d\n", ukey_commands, tag, n);
#endif

  // derive modifiers from specification prefix, module their order;
  // in order to abstract from the order and support flexible configuration,
  // the modifiers could have been collected separately already instead of 
  // prefixing them to the tag (before calling pick_key_function) but 
  // that would have been more substantial redesign; or the prefix could 
  // be normalized here by sorting; better solution: collect info here
  mod_keys tagmods(char * k)
  {
    mod_keys m = mod0;
    char * sep = strrchr(k, '+');
    if (sep)
      for (; *k && k < sep; k++)
        switch (*k) {
          when 'S': m |= MDK_SHIFT;
          when 'A': m |= MDK_ALT;
          when 'C': m |= MDK_CTRL;
          when 'W': m |= MDK_WIN;
          when 'U': m |= MDK_SUPER;
          when 'Y': m |= MDK_HYPER;
        }
    return m;
  }

  mod_keys mod_tag = tagmods(tag ?: "");
  char * tag0 = tag ? strchr(tag, '+') : 0;
  if (tag0)
    tag0++;
  else
    tag0 = tag;

#if defined(debug_def_keys) && debug_def_keys > 0
  printf("key_fun tag <%s> tag0 <%s> mod %X\n", tag ?: "(null)", tag0 ?: "(null)", mod_tag);
#endif

  int ret = false;

  char * paramp;
  while ((tag || n >= 0) && (paramp = strchr(cmdp, ':'))) {
    ret = false;

    *paramp = '\0';
    paramp++;
    char * sepp = strchr(paramp, sepch);
    if (sepp)
      *sepp = '\0';

    mod_keys mod_cmd = tagmods(cmdp);
    char * cmd0 = strrchr(cmdp, '+');
    if (cmd0)
      cmd0++;
    else
      cmd0 = cmdp;

    if (*cmdp == '*') {
      mod_cmd = mod_tag;
      cmd0 = cmdp;
      cmd0++;
      if (*cmd0 == '+')
        cmd0++;
    }

#if defined(debug_def_keys) && debug_def_keys > 1
    printf("tag <%s>: cmd <%s> cmd0 <%s> mod %X fct <%s>\n", tag, cmdp, cmd0, mod_cmd, paramp);
#endif

    if (tag ? (mod_cmd == mod_tag && !strcmp(cmd0, tag0)) : n == 0) {
#if defined(debug_def_keys) && debug_def_keys == 1
      printf("tag <%s>: cmd <%s> fct <%s>\n", tag, cmdp, paramp);
#endif
      wchar * fct = cs__utftowcs(paramp);

      if (key == VK_CAPITAL || key == VK_SCROLL || key == VK_NUMLOCK) {
        // nullify the keyboard state effect implied by the Lock key; 
        // use fake keyboard events, but avoid the recursion, 
        // fake events have scancode 0, ignore them also in win_key_up;
        // alternatively, we could hook the keyboard (low-level) and 
        // swallow the Lock key, but then it's not handled anymore so 
        // we'd need to fake its keyboard state effect 
        // (SetKeyboardState, and handle the off transition...) 
        // or consider it otherwise, all getting very tricky...
        if (!scancode) {
          ret = true;
        }
        else {
          if (key == VK_SCROLL) {
#ifdef debug_vk_scroll
            printf("pick VK_SCROLL\n");
#endif
            sync_scroll_lock(term.no_scroll || term.scroll_mode);
          }
          else
            win_key_nullify(key);
        }
      }

      uint code;
      if (ret) {
      }
      else if ((*fct == '"' && fct[wcslen(fct) - 1] == '"') ||
          (*fct == '\'' && fct[wcslen(fct) - 1] == '\''))
      {
        int len = wcslen(fct) - 2;
        if (len > 0) {
          provide_input(fct[1]);
          child_sendw(&fct[1], wcslen(fct) - 2);
          ret = true;
        }
      }
      else if (*fct == '^' && wcslen(fct) == 2) {
        char cc[2];
        cc[1] = ' ';
        if (fct[1] == '?')
          cc[1] = '\177';
        else if (fct[1] == ' ' || (fct[1] >= '@' && fct[1] <= '_')) {
          cc[1] = fct[1] & 0x1F;
        }
        if (cc[1] != ' ') {
          if (mods & MDK_ALT) {
            cc[0] = '\e';
            child_send(cc, 2);
          }
          else {
            provide_input(cc[1]);
            child_send(&cc[1], 1);
          }
          ret = true;
        }
      }
      else if (*fct == '`' && fct[wcslen(fct) - 1] == '`') {
        fct[wcslen(fct) - 1] = 0;
        char * cmd = cs__wcstombs(&fct[1]);
        if (*cmd) {
          term_cmd(cmd);
          ret = true;
        }
        free(cmd);
      }
      else if (sscanf (paramp, "%u%c", & code, &(char){0}) == 1) {
        char buf[33];
        int len = sprintf(buf, mods ? "\e[%i;%u~" : "\e[%i~", code, mods + 1);
        child_send(buf, len);
        ret = true;
      }
      else if (!*paramp) {
        // empty definition (e.g. "A+Enter:;"), shall disable 
        // further shortcut handling for the input key but 
        // trigger fall-back to "normal" key handling (with mods)
        ret = -1;
      }
      else {
        struct function_def * fudef = function_def(paramp);
        if (fudef) {
          if (fudef->cmd < 0xF000)
            send_syscommand(fudef->cmd);
          else
            fudef->fct_key(key, mods);

          ret = true;
          // should we trigger ret = false if (fudef->fct_key == kb_select)
          // so the case can be handled further in win_key_down ?
        }
        else {
          // invalid definition (e.g. "A+Enter:foo;"), shall 
          // not cause any action (return true) but provide a feedback
          win_bell(&cfg);
          ret = true;
        }
      }

      free(fct);
#ifdef common_return_handling
#warning produces bad behaviour; appends "~" input
      break;
#endif
      free(ukey_commands);

      if (key == VK_SCROLL) {
#ifdef debug_vk_scroll
        printf("pick VK_SCROLL break scn %d ret %d\n", scancode, ret);
#endif
        if (scancode && ret == true /*sic!*/)
          // don't call this if ret == -1
          sync_scroll_lock(term.no_scroll || term.scroll_mode);
      }

      return ret;
    }
    else if (key == VK_CAPITAL && cfg.compose_key == MDK_CAPSLOCK) {
      // support config ComposeKey=capslock:
      // nullify the keyboard state lock effect, see above,
      // so we can support config ComposeKey=capslock;
      // avoid the recursion with fake events that have scancode 0
      if (scancode)
        win_key_nullify(key);
      else
        return false;
    }

    n--;
    if (sepp) {
      cmdp = sepp + 1;
      // check for multi-line separation
      if (*cmdp == '\\' && cmdp[1] == '\n') {
        cmdp += 2;
        while (iswspace(*cmdp))
          cmdp++;
      }
    }
    else
      break;
  }
  free(ukey_commands);

#ifdef debug_vk_scroll
  if (key == VK_SCROLL)
    printf("pick VK_SCROLL return\n");
#endif
#ifdef common_return_handling
  // try to set ScrollLock keyboard LED consistently
#warning interferes with key functions (see above); does not work anyway
  if (key == VK_CAPITAL || key == VK_SCROLL || key == VK_NUMLOCK) {
    // nullify the keyboard state effect implied by the Lock key; 
    // use fake keyboard events, but avoid the recursion, 
    // fake events have scancode 0, ignore them also in win_key_up;
    // alternatively, we could hook the keyboard (low-level) and 
    // swallow the Lock key, but then it's not handled anymore so 
    // we'd need to fake its keyboard state effect 
    // (SetKeyboardState, and handle the off transition...) 
    // or consider it otherwise, all getting very tricky...
    if (!scancode) {
      ret = true;
    }
    else
      if (ret != true)
        win_key_nullify(key);
    }
#endif

  return false;
}

void
user_function(wstring commands, int n)
{
  pick_key_function(commands, 0, n, 0, 0, 0, 0);
}

static void
insert_alt_code(void)
{
  if (cs_cur_max < 4 && !alt_uni) {
    char buf[4];
    int pos = sizeof buf;
    do
      buf[--pos] = alt_code;
    while (alt_code >>= 8);
    provide_input(buf[pos]);
    child_send(buf + pos, sizeof buf - pos);
  }
  else if (alt_code < 0x10000) {
    wchar wc = alt_code;
    if (wc < 0x20)
      MultiByteToWideChar(CP_OEMCP, MB_USEGLYPHCHARS,
                          (char[]){wc}, 1, &wc, 1);
    provide_input(wc);
    child_sendw(&wc, 1);
  }
  else {
    xchar xc = alt_code;
    provide_input(' ');
    child_sendw((wchar[]){high_surrogate(xc), low_surrogate(xc)}, 2);
  }
  compose_clear();
}

// The ToUnicode function for converting keyboard states to characters may
// return multiple wchars due to dead keys and ligatures defined in the
// keyboard layout. The latter aren't limited to actual ligatures but can be any
// sequence of wchars.
//
// Unfortunately MSDN doesn't define a maximum length.
//
// The semi-official limit is four:
// http://www.siao2.com/2015/08/07/8770668856267196989.aspx
//
// However, KbdEdit supports up to nine:
// http://www.kbdedit.com/manual/high_level_ligatures.html
//
// And in this ill-tempered thread on unicode.org, it was found that ligatures
// can be up to sixteen wchars long:
// https://www.unicode.org/mail-arch/unicode-ml/y2015-m08/0023.html
//
// So let's go with the biggest number.
#define TO_UNICODE_MAX 16

bool
win_key_down(WPARAM wp, LPARAM lp)
{
  uint scancode = HIWORD(lp) & (KF_EXTENDED | 0xFF);
  bool extended = HIWORD(lp) & KF_EXTENDED;
  bool repeat = HIWORD(lp) & KF_REPEAT;
  uint count = LOWORD(lp);

  uint key = wp;
  last_key_down = key;
  last_key_up = 0;

  if (comp_state == COMP_ACTIVE)
    comp_state = COMP_PENDING;
  else if (comp_state == COMP_CLEAR && !repeat)
    comp_state = COMP_NONE;

#ifdef debug_virtual_key_codes
  printf("win_key_down %02X %s scan %d ext %d rpt %d/%d other %02X\n", key, vk_name(key), scancode, extended, repeat, count, HIWORD(lp) >> 8);
#endif

static LONG last_key_time = 0;

  LONG message_time = GetMessageTime();
  if (repeat) {
#ifdef auto_repeat_cursor_keys_option
    switch (key) {
      when VK_PRIOR ... VK_DOWN: do not return...;
    }
#endif
    if (!term.auto_repeat)
      return true;
    if (term.repeat_rate &&
        message_time - last_key_time < 1000 / term.repeat_rate)
      return true;
  }
  if (repeat && term.repeat_rate &&
      message_time - last_key_time < 2000 / term.repeat_rate)
    /* Key repeat seems to be continued. */
    last_key_time += 1000 / term.repeat_rate;
  else
    last_key_time = message_time;

  if (key == VK_PROCESSKEY) {
    TranslateMessage(
      &(MSG){.hwnd = wnd, .message = WM_KEYDOWN, .wParam = wp, .lParam = lp}
    );
    return true;
  }

  uchar kbd[256];
  GetKeyboardState(kbd);
  inline bool is_key_down(uchar vk) { return kbd[vk] & 0x80; }
#ifdef debug_virtual_key_codes
  printf("-- [%u %c%u] Shift %d:%d/%d Ctrl %d:%d/%d Alt %d:%d/%d\n",
         (int)GetMessageTime(), lctrl_time ? '+' : '=', (int)GetMessageTime() - lctrl_time,
         is_key_down(VK_SHIFT), is_key_down(VK_LSHIFT), is_key_down(VK_RSHIFT),
         is_key_down(VK_CONTROL), is_key_down(VK_LCONTROL), is_key_down(VK_RCONTROL),
         is_key_down(VK_MENU), is_key_down(VK_LMENU), is_key_down(VK_RMENU));
#endif

  // Fix AltGr detection;
  // workaround for broken Windows on-screen keyboard (#692)
  if (!cfg.old_altgr_detection) {
    static bool lmenu_tweak = false;
    if (key == VK_MENU && !scancode) {
      extended = true;
      scancode = 312;
      kbd[VK_LMENU] = 0x00;
      kbd[VK_RMENU] = 0x80;
      lmenu_tweak = true;
    }
    else if (lmenu_tweak) {
      kbd[VK_LMENU] = 0x00;
      kbd[VK_RMENU] = 0x80;
      lmenu_tweak = false;
    }
  }

  // Distinguish real LCONTROL keypresses from fake messages sent for AltGr.
  // It's a fake if the next message is an RMENU with the same timestamp.
  // Or, as of buggy TeamViewer, if the RMENU comes soon after (#783).
  if (key == VK_CONTROL && !extended) {
    lctrl = true;
    lctrl_time = GetMessageTime();
    //printf("lctrl (true) %d (%d)\n", lctrl, is_key_down(VK_LCONTROL));
  }
  else if (lctrl_time) {
    lctrl = !(key == VK_MENU && extended 
              && GetMessageTime() - lctrl_time <= cfg.ctrl_alt_delay_altgr);
    lctrl_time = 0;
    //printf("lctrl (time) %d (%d)\n", lctrl, is_key_down(VK_LCONTROL));
  }
  else {
    lctrl = is_key_down(VK_LCONTROL) && (lctrl || !is_key_down(VK_RMENU));
    //printf("lctrl (else) %d (%d)\n", lctrl, is_key_down(VK_LCONTROL));
  }

  bool numlock = kbd[VK_NUMLOCK] & 1;
  bool shift = is_key_down(VK_SHIFT);
  bool lalt = is_key_down(VK_LMENU);
  bool ralt = is_key_down(VK_RMENU);
  bool alt = lalt | ralt;
  trace_alt("alt %d lalt %d ralt %d\n", alt, lalt, ralt);
  bool rctrl = is_key_down(VK_RCONTROL);
  bool ctrl = lctrl | rctrl;
  bool ctrl_lalt_altgr = cfg.ctrl_alt_is_altgr & ctrl & lalt & !ralt;
  //bool altgr0 = ralt | ctrl_lalt_altgr;
  // Alt/AltGr detection and handling could do with a complete revision 
  // from scratch; on the other hand, no unnecessary risk should be taken, 
  // so another hack is added.
  bool lctrl0 = is_key_down(VK_LCONTROL);
  bool altgr0 = (ralt & lctrl0) | ctrl_lalt_altgr;

  bool external_hotkey = false;
  if (ralt && !scancode && cfg.external_hotkeys) {
    // Support external hot key injection by overriding disabled Alt+Fn
    // and fix buggy StrokeIt (#833).
    trace_alt("ralt = false\n");
    ralt = false;
    if (cfg.external_hotkeys > 1)
      external_hotkey = true;
  }

  bool altgr = ralt | ctrl_lalt_altgr;
  // While this should more properly reflect the AltGr modifier state, 
  // with the current implementation it has the opposite effect;
  // it spoils Ctrl+AltGr with modify_other_keys mode.
  //altgr = (ralt & lctrl0) | ctrl_lalt_altgr;

  bool win = (is_key_down(VK_LWIN) && key != VK_LWIN)
          || (is_key_down(VK_RWIN) && key != VK_RWIN);
  trace_alt("alt %d lalt %d ralt %d altgr %d\n", alt, lalt, ralt, altgr);

  mod_keys mods = shift * MDK_SHIFT
                | alt * MDK_ALT
                | ctrl * MDK_CTRL
                | win * MDK_WIN
                ;
  bool super = super_key && is_key_down(super_key);
  bool hyper = hyper_key && is_key_down(hyper_key);
  mods |= super * MDK_SUPER | hyper * MDK_HYPER;

  update_mouse(mods);

  // Workaround for Windows clipboard history pasting simply injecting Ctrl+V
  // (mintty/wsltty#139)
  if (key == 'V' && mods == MDK_CTRL && !scancode) {
    win_paste();
    return true;
  }

  if (key == VK_MENU) {
    if (!repeat && mods == MDK_ALT && alt_state == ALT_NONE)
      alt_state = ALT_ALONE;
    return true;
  }

  old_alt_state = alt_state;
  if (alt_state > ALT_NONE)
    alt_state = ALT_CANCELLED;

  // Exit when pressing Enter or Escape while holding the window open after
  // the child process has died.
  if ((key == VK_RETURN || key == VK_ESCAPE) && !mods && !child_is_alive())
    exit_mintty();

  // On <Escape> or <Enter> key, restore keyboard IME state to alphanumeric mode.
  if ( cfg.key_alpha_mode && (key == VK_RETURN || key == VK_ESCAPE) && !mods) {
    HIMC hImc = ImmGetContext(wnd);
    if (ImmGetOpenStatus(hImc)) {
      ImmSetConversionStatus(hImc, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
    }
    ImmReleaseContext(wnd, hImc);
  }

  // Handling special shifted key functions
  if (newwin_pending) {
    if (!extended) {  // only accept numeric keypad
      switch (key) {
        when VK_HOME : newwin_monix--; newwin_moniy--;
        when VK_UP   : newwin_moniy--;
        when VK_PRIOR: newwin_monix++; newwin_moniy--;
        when VK_LEFT : newwin_monix--;
        when VK_CLEAR: newwin_monix = 0; newwin_moniy = 0; newwin_home = true;
        when VK_RIGHT: newwin_monix++;
        when VK_END  : newwin_monix--; newwin_moniy++;
        when VK_DOWN : newwin_moniy++;
        when VK_NEXT : newwin_monix++; newwin_moniy++;
        when VK_INSERT or VK_DELETE:
                       newwin_monix = 0; newwin_moniy = 0; newwin_home = false;
      }
    }
    return true;
  }
  if (transparency_pending) {
    transparency_pending = 2;
    switch (key) {
      when VK_HOME  : set_transparency(previous_transparency);
      when VK_CLEAR : if (win_is_glass_available()) {
                        cfg.transparency = TR_GLASS;
                        win_update_transparency(TR_GLASS, false);
                      }
      when VK_DELETE: set_transparency(0);
      when VK_INSERT: set_transparency(127);
      when VK_END   : set_transparency(TR_HIGH);
      when VK_UP    : set_transparency(cfg.transparency + 1);
      when VK_PRIOR : set_transparency(cfg.transparency + 16);
      when VK_LEFT  : set_transparency(cfg.transparency - 1);
      when VK_RIGHT : set_transparency(cfg.transparency + 1);
      when VK_DOWN  : set_transparency(cfg.transparency - 1);
      when VK_NEXT  : set_transparency(cfg.transparency - 16);
      otherwise: transparency_pending = 0;
    }
#ifdef debug_transparency
    printf("==%d\n", transparency_pending);
#endif
    if (transparency_pending) {
      transparency_tuned = true;
      return true;
    }
  }
  if (selection_pending) {
    bool sel_adjust = false;
    //WPARAM scroll = 0;
    int sbtop = -sblines();
    int sbbot = term_last_nonempty_line();
    int oldisptop = term.disptop;
    //printf("y %d disptop %d sb %d..%d\n", term.sel_pos.y, term.disptop, sbtop, sbbot);
    switch (key) {
      when VK_CLEAR:
        // re-anchor keyboard selection
        term.sel_anchor = term.sel_pos;
        term.sel_start = term.sel_pos;
        term.sel_end = term.sel_pos;
        term.sel_rect = mods & MDK_ALT;
        sel_adjust = true;
      when VK_LEFT:
        if (term.sel_pos.x > 0)
          term.sel_pos.x--;
        sel_adjust = true;
      when VK_RIGHT:
        if (term.sel_pos.x < term.cols)
          term.sel_pos.x++;
        sel_adjust = true;
      when VK_UP:
        if (term.sel_pos.y > sbtop) {
          if (term.sel_pos.y <= term.disptop)
            term_scroll(0, -1);
          term.sel_pos.y--;
          sel_adjust = true;
        }
      when VK_DOWN:
        if (term.sel_pos.y < sbbot) {
          if (term.sel_pos.y + 1 >= term.disptop + term.rows)
            term_scroll(0, +1);
          term.sel_pos.y++;
          sel_adjust = true;
        }
      when VK_PRIOR:
        //scroll = SB_PAGEUP;
        term_scroll(0, -max(1, term.rows - 1));
        term.sel_pos.y += term.disptop - oldisptop;
        sel_adjust = true;
      when VK_NEXT:
        //scroll = SB_PAGEDOWN;
        term_scroll(0, +max(1, term.rows - 1));
        term.sel_pos.y += term.disptop - oldisptop;
        sel_adjust = true;
      when VK_HOME:
        //scroll = SB_TOP;
        term_scroll(+1, 0);
        term.sel_pos.y += term.disptop - oldisptop;
        term.sel_pos.y = sbtop;
        term.sel_pos.x = 0;
        sel_adjust = true;
      when VK_END:
        //scroll = SB_BOTTOM;
        term_scroll(-1, 0);
        term.sel_pos.y += term.disptop - oldisptop;
        term.sel_pos.y = sbbot;
        if (sbbot < term.rows) {
          termline *line = term.lines[sbbot];
          if (line)
            for (int j = line->cols - 1; j > 0; j--) {
              term.sel_pos.x = j + 1;
              if (!termchars_equal(&line->chars[j], &term.erase_char))
                break;
            }
        }
        sel_adjust = true;
      when VK_INSERT or VK_RETURN:  // copy
        term_copy();
        selection_pending = false;
      when VK_DELETE or VK_ESCAPE:  // abort
        selection_pending = false;
      otherwise:
        //selection_pending = false;
        win_bell(&cfg);
    }
    //if (scroll) {
    //  if (!term.app_scrollbar)
    //    SendMessage(wnd, WM_VSCROLL, scroll, 0);
    //  sel_adjust = true;
    //}
    if (sel_adjust) {
      if (term.sel_rect) {
        term.sel_start.y = min(term.sel_anchor.y, term.sel_pos.y);
        term.sel_start.x = min(term.sel_anchor.x, term.sel_pos.x);
        term.sel_end.y = max(term.sel_anchor.y, term.sel_pos.y);
        term.sel_end.x = max(term.sel_anchor.x, term.sel_pos.x);
      }
      else if (posle(term.sel_anchor, term.sel_pos)) {
        term.sel_start = term.sel_anchor;
        term.sel_end = term.sel_pos;
      }
      else {
        term.sel_start = term.sel_pos;
        term.sel_end = term.sel_anchor;
      }
      //printf("->sel %d:%d .. %d:%d\n", term.sel_start.y, term.sel_start.x, term.sel_end.y, term.sel_end.x);
      term.selected = true;
      win_update(true);
    }
    if (selection_pending)
      return true;
    else
      term.selected = false;
    return true;
  }
  if (tek_mode == TEKMODE_GIN) {
    int step = (mods & MDK_SHIFT) ? 40 : (mods & MDK_CTRL) ? 1 : 4;
    switch (key) {
      when VK_HOME : tek_move_by(step, -step);
      when VK_UP   : tek_move_by(step, 0);
      when VK_PRIOR: tek_move_by(step, step);
      when VK_LEFT : tek_move_by(0, -step);
      when VK_CLEAR: tek_move_by(0, 0);
      when VK_RIGHT: tek_move_by(0, step);
      when VK_END  : tek_move_by(-step, -step);
      when VK_DOWN : tek_move_by(-step, 0);
      when VK_NEXT : tek_move_by(-step, step);
      otherwise: step = 0;
    }
    if (step)
      return true;
  }

  bool allow_shortcut = true;

  if (!term.shortcut_override && old_alt_state <= ALT_ALONE) {
    // user-defined shortcuts
    //test: W("-:'foo';A+F3:;A+F5:flipscreen;A+F9:\"f9\";C+F10:\"f10\";p:paste;d:`date`;o:\"oo\";ö:\"öö\";€:\"euro\";~:'tilde';[:'[[';µ:'µµ'")
    if (*cfg.key_commands) {
      /* Look up a function tag for either of
         * (modified) special key (like Tab, Pause, ...)
         * (modified) function key
         * Ctrl+Shift-modified character (letter or other layout key)
         Arguably, Ctrl+Shift-character assignments could be 
         overridden by modify_other_keys mode, but we stay consistent 
         with xterm here, where the Translations resource takes 
         priority over modifyOtherKeys mode.
       */
      char * tag = 0;
      mod_keys mod0 = 0;
      int vki = -1;
      for (uint i = 0; i < lengthof(vktab); i++)
        if (key == vktab[i].vkey) {
          vki = i;
          break;
        }
      bool keypad = vktab[vki].vkey == VK_RETURN
                    ? extended
                    : vktab[vki].unmod == 2
                      ? !extended
                      : vktab[vki].unmod == 3;
      bool editpad = !keypad && vktab[vki].unmod >= 2;
      //printf("found %d ext %d kp %d ep %d\n", vki, extended, keypad, editpad);
      if (vki >= 0 && !altgr
          && (mods || vktab[vki].unmod || extended)
          && (!cfg.old_keyfuncs_keypad || !editpad || !term.app_cursor_keys)
          && (!cfg.old_keyfuncs_keypad || !keypad || !term.app_keypad)
         )
      {
        tag = asform("%s%s%s%s%s%s%s%s%s",
                     ctrl ? "C" : "",
                     alt ? "A" : "",
                     shift ? "S" : "",
                     win ? "W" : "",
                     super ? "U" : "",
                     hyper ? "Y" : "",
                     mods ? "+" : "",
                     keypad ? "KP_" : "",
                     vktab[vki].nam);
      }
      else if (VK_F1 <= key && key <= VK_F24) {
        tag = asform("%s%s%s%s%s%s%sF%d",
                     ctrl ? "C" : "",
                     alt ? "A" : "",
                     shift ? "S" : "",
                     win ? "W" : "",
                     super ? "U" : "",
                     hyper ? "Y" : "",
                     mods ? "+" : "",
                     key - VK_F1 + 1);
      }
      else if (
               // !term.modify_other_keys &&
               (mods & (MDK_CTRL | MDK_SHIFT))
                == (cfg.ctrl_exchange_shift
                                     ? MDK_CTRL
                                     : (MDK_CTRL | MDK_SHIFT))
               || (mods & MDK_WIN)
               || (mods & (MDK_SUPER | MDK_HYPER))
               || ((mods & (MDK_CTRL | MDK_ALT)) && cfg.enable_remap_ctrls)
              )
      {
        uchar kbd0[256];
        GetKeyboardState(kbd0);
        wchar wbuf[TO_UNICODE_MAX];
        int wlen = ToUnicode(key, scancode, kbd0, wbuf, lengthof(wbuf), 0);
        wchar w1 = wlen > 0 ? *wbuf : 0;
        kbd0[VK_SHIFT] = 0;
        wlen = ToUnicode(key, scancode, kbd0, wbuf, lengthof(wbuf), 0);
        wchar w2 = wlen > 0 ? *wbuf : 0;
#ifdef debug_def_keys
        printf("VK_*CONTROL %d %d/%d *ctrl %d %d/%d -> %04X; -SHIFT %04X\n",
               is_key_down(VK_CONTROL), is_key_down(VK_LCONTROL), is_key_down(VK_RCONTROL),
               ctrl, lctrl, rctrl,
               w1, w2);
#endif
        if (!w1 || w1 == w2) {
          kbd0[VK_SHIFT] = 0;
          kbd0[VK_LCONTROL] = 0;
          if (!altgr)
            kbd0[VK_CONTROL] = 0;
          wlen = ToUnicode(key, scancode, kbd0, wbuf, lengthof(wbuf), 0);
#ifdef debug_def_keys
          printf("            %d %d/%d *ctrl %d %d/%d -> %d %04X\n",
                 is_key_down(VK_CONTROL), is_key_down(VK_LCONTROL), is_key_down(VK_RCONTROL),
                 ctrl, lctrl, rctrl,
                 wlen, *wbuf);
#endif
          if (wlen == 1 || wlen == 2) {
            wbuf[wlen] = 0;
            char * keytag = cs__wcstoutf(wbuf);
            tag = asform("%s%s%s%s%s%s",
                         alt ? "A" : "",
                         win ? "W" : "",
                         super ? "U" : "",
                         hyper ? "Y" : "",
                         (alt | win | super | hyper) ? "+" : "",
                         keytag);
            mod0 |= MDK_CTRL | MDK_SHIFT;
            free(keytag);
          }
        }
#ifdef debug_def_keys
        printf("key %04X <%s>\n", *wbuf, tag);
#endif

        if (wlen < 0) {
          // Ugly hack to clear dead key state, a la Michael Kaplan.
          memset(kbd0, 0, sizeof kbd0);
          uint scancode = MapVirtualKey(VK_DECIMAL, 0);
          wchar dummy;
          while (ToUnicode(VK_DECIMAL, scancode, kbd0, &dummy, 1, 0) < 0);
        }
      }
      if (tag) {
        int ret = pick_key_function(cfg.key_commands, tag, 0, key, mods, mod0, scancode);
        free(tag);
        if (ret == true)
          return true;
        if (ret == -1)
          allow_shortcut = false;
      }
    }

    // If a user-defined key definition overrides a built-in shortcut 
    // but does not assign its own, the key shall be handled as a key, 
    // (with mods); for subsequent blocks of shortcut handling, we 
    // achieve this by simply jumping over them (infamous goto), 
    // but there are a few cases handled beyond, embedded in general 
    // key handling, which are then guarded by further usage of the 
    // allow_shortcut flag (Alt+Enter, Ctrl+Tab, Alt+Space).
    if (!allow_shortcut)
      goto skip_shortcuts;

    // Copy&paste
    if (cfg.clip_shortcuts && key == VK_INSERT && mods && !alt) {
      if (ctrl)
        term_copy();
      if (shift)
        win_paste();
      return true;
    }

    // Alt+Fn shortcuts
    if ((cfg.alt_fn_shortcuts || external_hotkey)
        && alt && !altgr
        && VK_F1 <= key && key <= VK_F24
       )
    {
      if (!ctrl) {
        switch (key) {
          when VK_F2:
            // defer send_syscommand(IDM_NEW/IDM_TAB) until key released
            // monitor cursor keys to collect parameters meanwhile
            newwin_key = key;
            newwin_pending = true;
            newwin_home = false; newwin_monix = 0; newwin_moniy = 0;
            if (mods & MDK_SHIFT)
              newwin_shifted = true;
            else
              newwin_shifted = false;
          when VK_F3:  send_syscommand(IDM_SEARCH);
          when VK_F4:  send_syscommand(SC_CLOSE);
          when VK_F8:  send_syscommand(IDM_RESET);
          when VK_F10: send_syscommand(IDM_DEFSIZE_ZOOM);
          when VK_F11: send_syscommand(IDM_FULLSCREEN);
          when VK_F12: send_syscommand(IDM_FLIPSCREEN);
        }
      }
      return true;
    }

    // Ctrl+Shift+letter shortcuts
    if (cfg.ctrl_shift_shortcuts && 'A' <= key && key <= 'Z' &&
        mods == (cfg.ctrl_exchange_shift ? MDK_CTRL : (MDK_CTRL | MDK_SHIFT))
       ) {
      switch (key) {
        when 'A': term_select_all();
        when 'C': term_copy();
        when 'V': win_paste();
        when 'I': open_popup_menu(true, "ls", mods);
        when 'N': send_syscommand(IDM_TAB);  // deprecated default assignment
        when 'W': send_syscommand(SC_CLOSE);
        when 'R': send_syscommand(IDM_RESET_NOASK);
        when 'D': send_syscommand(IDM_DEFSIZE);
        when 'F': send_syscommand(cfg.zoom_font_with_window ? IDM_FULLSCREEN_ZOOM : IDM_FULLSCREEN);
        when 'S': send_syscommand(IDM_FLIPSCREEN);
        when 'H': send_syscommand(IDM_SEARCH);
        when 'T': transparency_level();  // deprecated default assignment
        when 'P': cycle_pointer_style(); // deprecated default assignment
        when 'O': toggle_scrollbar();
        when 'U': unicode_char();
      }
      return true;
    }

    // Scrollback and Selection via keyboard
    if (!term.on_alt_screen || term.show_other_screen) {
      mod_keys scroll_mod = cfg.scroll_mod ?: 128;
      if (cfg.pgupdn_scroll && (key == VK_PRIOR || key == VK_NEXT) &&
          !(mods & ~scroll_mod))
        mods ^= scroll_mod;
      if (mods == scroll_mod || term.scroll_mode) {
        WPARAM scroll;
        switch (key) {
          when VK_HOME:  scroll = SB_TOP;
          when VK_END:   scroll = SB_BOTTOM;
          when VK_PRIOR: scroll = SB_PAGEUP;
          when VK_NEXT:  scroll = SB_PAGEDOWN;
          when VK_UP:    scroll = SB_LINEUP;
          when VK_DOWN:  scroll = SB_LINEDOWN;
          when VK_LEFT:  scroll = SB_PRIOR;
          when VK_RIGHT: scroll = SB_NEXT;
          when VK_CLEAR:
            // start and anchor keyboard selection
            term.sel_pos = (pos){.y = term.curs.y, .x = term.curs.x, .r = 0};
            term.sel_anchor = term.sel_pos;
            term.sel_start = term.sel_pos;
            term.sel_end = term.sel_pos;
            term.sel_rect = mods & MDK_ALT;
            selection_pending = true;
            //printf("selection_pending = true\n");
            return true;
          otherwise: goto not_scroll;
        }
        if (!term.app_scrollbar) // prevent recursion
          SendMessage(wnd, WM_VSCROLL, scroll, 0);
        return true;
        not_scroll:;
      }
    }

    // Font zooming
    if (cfg.zoom_shortcuts && (mods & ~MDK_SHIFT) == MDK_CTRL) {
      int zoom;
      switch (key) {
        // numeric keypad keys:
        // -- handle these ahead, i.e. here
        when VK_SUBTRACT:  zoom = -1;
        when VK_ADD:       zoom = 1;
        when VK_NUMPAD0:   zoom = 0;
          // Shift+VK_NUMPAD0 would be VK_INSERT but don't mangle that!
        // normal keys:
        // -- handle these in the course of layout() and other checking,
        // -- see below
        //when VK_OEM_MINUS: zoom = -1; mods &= ~MDK_SHIFT;
        //when VK_OEM_PLUS:  zoom = 1; mods &= ~MDK_SHIFT;
        //when '0':          zoom = 0;
        otherwise: goto not_zoom;
      }
      win_zoom_font(zoom, mods & MDK_SHIFT);
      return true;
      not_zoom:;
    }
  }

  skip_shortcuts:;

  // Context and window menus
  if (key == VK_APPS && !*cfg.key_menu) {
    if (shift)
      send_syscommand(SC_KEYMENU);
    else {
      win_show_mouse();
      open_popup_menu(true, 
                      mods & MDK_CTRL ? cfg.menu_ctrlmenu : cfg.menu_menu, 
                      mods);
    }
    alt_state = ALT_NONE;
    return true;
  }

  // we might consider super and hyper for mods here but we need to filter 
  // them anyway during user-defined key detection (using the '*' prefix)
  //mods |= super * MDK_SUPER | hyper * MDK_HYPER;

  bool zoom_hotkey(void) {
    if (!term.shortcut_override && cfg.zoom_shortcuts
        && (mods & ~MDK_SHIFT) == MDK_CTRL) {
      int zoom;
      switch (key) {
        // numeric keypad keys:
        // -- handle these ahead, see above
        //when VK_SUBTRACT:  zoom = -1;
        //when VK_ADD:       zoom = 1;
        //when VK_NUMPAD0:   zoom = 0;
          // Shift+VK_NUMPAD0 would be VK_INSERT but don't mangle that!
        // normal keys:
        // -- handle these in the course of layout() and other checking,
        // -- so handle the case that something is assigned to them
        // -- e.g. Ctrl+Shift+- -> Ctrl+_
        // -- or even a custom Ctrl+- mapping
        // depending on keyboard layout, these may already be shifted!
        // thus better ignore the shift state, at least for -/+
        when VK_OEM_MINUS: zoom = -1; mods &= ~MDK_SHIFT;
        when VK_OEM_PLUS:  zoom = 1; mods &= ~MDK_SHIFT;
        when '0':          zoom = 0;
        otherwise: return false;
      }
      win_zoom_font(zoom, mods & MDK_SHIFT);
      return true;
    }
    return false;
  }

  // Keycode buffers
  char buf[32];
  int len = 0;

  inline void ch(char c) { buf[len++] = c; }
  inline void esc_if(bool b) { if (b) ch('\e'); }
  void ss3(char c) { ch('\e'); ch('O'); ch(c); }
  void csi(char c) { ch('\e'); ch('['); ch(c); }
  void mod_csi(char c) { len = sprintf(buf, "\e[1;%u%c", mods + 1, c); }
  void mod_ss3(char c) { mods ? mod_csi(c) : ss3(c); }
  void tilde_code(uchar code) {
    trace_key("tilde");
    len = sprintf(buf, mods ? "\e[%i;%u~" : "\e[%i~", code, mods + 1);
  }
  void other_code(wchar c) {
    trace_key("other");
    if (cfg.format_other_keys)
      // xterm "formatOtherKeys: 1": CSI 64 ; 2 u
      len = sprintf(buf, "\e[%u;%uu", c, mods + 1);
    else
      // xterm "formatOtherKeys: 0": CSI 2 7 ; 2 ; 64 ~
      len = sprintf(buf, "\e[27;%u;%u~", mods + 1, c);
  }
  void app_pad_code(char c) {
    void mod_appl_xterm(char c) {len = sprintf(buf, "\eO%u%c", mods + 1, c);}
    if (mods && term.app_keypad) switch (key) {
      when VK_DIVIDE or VK_MULTIPLY or VK_SUBTRACT or VK_ADD or VK_RETURN:
        mod_appl_xterm(c - '0' + 'p');
        return;
    }
    if (term.vt220_keys && mods && term.app_keypad) switch (key) {
      when VK_CLEAR or VK_PRIOR ... VK_DOWN or VK_INSERT or VK_DELETE:
        mod_appl_xterm(c - '0' + 'p');
        return;
    }
    mod_ss3(c - '0' + 'p');
  }
  void strcode(string s) {
    unsigned int code;
    if (sscanf (s, "%u", & code) == 1)
      tilde_code(code);
    else
      len = sprintf(buf, "%s", s);
  }

  bool alt_code_key(char digit) {
    if (old_alt_state > ALT_ALONE) {
      alt_state = old_alt_state;  // stay in alt_state, process key
      if (digit >= 0 && digit < alt_state) {
        alt_code = alt_code * alt_state + digit;
        if (alt_code < 0 || alt_code > 0x10FFFF) {
          win_bell(&cfg);
          alt_state = ALT_NONE;
        }
        else
          win_update(false);
      }
      else
        win_bell(&cfg);
      return true;
    }
    return false;
  }

  bool alt_code_numpad_key(char digit) {
    if (old_alt_state == ALT_ALONE) {
      alt_code = digit;
      alt_state = digit ? ALT_DEC : ALT_OCT;
      return true;
    }
    return alt_code_key(digit);
  }

  bool alt_code_ignore(void) {
    if (old_alt_state > ALT_ALONE) {
      alt_state = old_alt_state;  // keep alt_state, ignore key
      win_bell(&cfg);
      return true;
    }
    else
      return false;
  }

  bool app_pad_key(char symbol) {
    if (extended)
      return false;
    // Mintty-specific: produce app_pad codes not only when vt220 mode is on,
    // but also in PC-style mode when app_cursor_keys is off, to allow the
    // numpad keys to be distinguished from the cursor/editing keys.
    if (term.app_keypad && (!term.app_cursor_keys || term.vt220_keys)) {
      // If NumLock is on, Shift must have been pressed to override it and
      // get a VK code for an editing or cursor key code.
      if (numlock)
        mods |= MDK_SHIFT;
      app_pad_code(symbol);
      return true;
    }
    if (symbol == '.')
      return alt_code_ignore();
    else
      return alt_code_numpad_key(symbol - '0');
  }

  void edit_key(uchar code, char symbol) {
    if (!app_pad_key(symbol)) {
      if (code != 3 || ctrl || alt || shift || !term.delete_sends_del)
        tilde_code(code);
      else
        ch(CDEL);
    }
  }

  void cursor_key(char code, char symbol) {
    if (term.vt52_mode)
      len = sprintf(buf, "\e%c", code);
    else if (!app_pad_key(symbol))
      mods ? mod_csi(code) : term.app_cursor_keys ? ss3(code) : csi(code);
  }

static struct {
  unsigned int combined;
  unsigned int base;
  unsigned int spacing;
} comb_subst[] = {
#include "combined.t"
};

  // Keyboard layout
  bool layout(void) {
    wchar wbuf[TO_UNICODE_MAX];
    int wlen = ToUnicode(key, scancode, kbd, wbuf, lengthof(wbuf), 0);
    trace_alt("layout %d alt %d altgr %d\n", wlen, alt, altgr);
    if (!wlen)     // Unassigned.
      return false;
    if (wlen < 0)  // Dead key.
      return true;

    esc_if(alt);

    // Substitute accent compositions not supported by Windows
    if (wlen == 2)
      for (unsigned int i = 0; i < lengthof(comb_subst); i++)
        if (comb_subst[i].spacing == wbuf[0] && comb_subst[i].base == wbuf[1]
            && comb_subst[i].combined < 0xFFFF  // -> wchar/UTF-16: BMP only
           ) {
          wchar wtmp = comb_subst[i].combined;
          short mblen = cs_wcntombn(buf + len, &wtmp, lengthof(buf) - len, 1);
          // short to recognise 0xFFFD as negative (WideCharToMultiByte...?)
          if (mblen > 0) {
            wbuf[0] = comb_subst[i].combined;
            wlen = 1;
          }
          break;
        }

    // Compose characters
    if (comp_state > 0) {
#ifdef debug_compose
      printf("comp (%d)", wlen);
      for (int i = 0; i < compose_buflen; i++) printf(" %04X", compose_buf[i]);
      printf(" +");
      for (int i = 0; i < wlen; i++) printf(" %04X", wbuf[i]);
      printf("\n");
#endif
      for (int i = 0; i < wlen; i++)
        compose_buf[compose_buflen++] = wbuf[i];
      win_update(false);

      uint comp_len = min((uint)compose_buflen, lengthof(composed->kc));
      bool found = false;
      for (uint k = 0; k < lengthof(composed); k++)
        if (0 == wcsncmp(compose_buf, composed[k].kc, comp_len)) {
          if (comp_len < lengthof(composed->kc) && composed[k].kc[comp_len]) {
            // partial match
            comp_state = COMP_ACTIVE;
            return true;
          }
          else {
            // match
            ///can there be an uncomposed rest in wbuf? should we consider it?
#ifdef utf8_only
            ///alpha, UTF-8 only, unchecked...
            strcpy(buf + len, composed[k].s);
            len += strlen(composed[k].s);
            compose_buflen = 0;
            return true;
#else
            wchar * wc = cs__utftowcs(composed[k].s);
            wlen = 0;
            while (wc[wlen] && wlen < (int)lengthof(wbuf)) {
              wbuf[wlen] = wc[wlen];
              wlen++;
            }
            free(wc);
            found = true;  // fall through, but skip error handling
#endif
          }
        }
      ///should we deliver compose_buf[] first...?
      compose_buflen = 0;
      if (!found) {
        // unknown compose sequence
        win_bell(&cfg);
        // continue without composition
      }
    }
    else
      compose_buflen = 0;

    // Check that the keycode can be converted to the current charset
    // before returning success.
    int mblen = cs_wcntombn(buf + len, wbuf, lengthof(buf) - len, wlen);
#ifdef debug_ToUnicode
    printf("wlen %d:", wlen);
    for (int i = 0; i < wlen; i ++) printf(" %04X", wbuf[i] & 0xFFFF);
    printf("\n");
    printf("mblen %d:", mblen);
    for (int i = 0; i < mblen; i ++) printf(" %02X", buf[i] & 0xFF);
    printf("\n");
#endif
    bool ok = mblen > 0;
    len = ok ? len + mblen : 0;
    return ok;
  }

  wchar undead_keycode(void) {
    wchar wc;
    int len = ToUnicode(key, scancode, kbd, &wc, 1, 0);
#ifdef debug_key
    printf("undead %02X scn %d -> %d %04X\n", key, scancode, len, wc);
#endif
    if (len < 0) {
      // Ugly hack to clear dead key state, a la Michael Kaplan.
      uchar empty_kbd[256];
      memset(empty_kbd, 0, sizeof empty_kbd);
      uint scancode = MapVirtualKey(VK_DECIMAL, 0);
      wchar dummy;
      while (ToUnicode(VK_DECIMAL, scancode, empty_kbd, &dummy, 1, 0) < 0);
      return wc;
    }
    return len == 1 ? wc : 0;
  }

  void modify_other_key(void) {
    wchar wc = undead_keycode();
    if (!wc) {
#ifdef debug_key
      printf("modf !wc mods %X shft %d\n", mods, mods & MDK_SHIFT);
#endif
      if (mods & MDK_SHIFT) {
        kbd[VK_SHIFT] = 0;
        wc = undead_keycode();
      }
    }
#ifdef debug_key
    printf("modf wc %04X (ctrl %d key %02X)\n", wc, ctrl, key);
#endif
    if (wc) {
      if (altgr && !is_key_down(VK_LMENU))
        mods &= ~ MDK_ALT;
      if (!altgr && (mods == MDK_CTRL) && wc > '~' && key <= 'Z') {
        // report control char on non-latin keyboard layout
        other_code(key);
      }
      else
        other_code(wc);
    }
  }

  bool char_key(void) {
    alt = lalt & !ctrl_lalt_altgr;
    trace_alt("char_key alt %d (l %d r %d altgr %d)\n", alt, lalt, ralt, altgr);

    // Sync keyboard layout with our idea of AltGr.
    kbd[VK_CONTROL] = altgr ? 0x80 : 0;

    // Don't handle Ctrl combinations here.
    // Need to check there's a Ctrl that isn't part of Ctrl+LeftAlt==AltGr.
    if ((ctrl & !ctrl_lalt_altgr) | (lctrl & rctrl))
      return false;

    // Try the layout.
    if (layout())
      return true;

    // This prevents AltGr from behaving like Alt in modify_other_keys mode.
    if (!cfg.altgr_is_alt && altgr0)
      return false;

    if (ralt) {
      // Try with RightAlt/AltGr key treated as Alt.
      kbd[VK_CONTROL] = 0;
      trace_alt("char_key ralt; alt = true\n");
      alt = true;
      layout();
      return true;
    }
    return !ctrl;
  }

  bool altgr_key(void) {
    if (!altgr)
      return false;

    trace_alt("altgr_key alt %d -> %d\n", alt, lalt & !ctrl_lalt_altgr);
    alt = lalt & !ctrl_lalt_altgr;

    // Sync keyboard layout with our idea of AltGr.
    kbd[VK_CONTROL] = altgr ? 0x80 : 0;

    // Don't handle Ctrl combinations here.
    // Need to check there's a Ctrl that isn't part of Ctrl+LeftAlt==AltGr.
    if ((ctrl & !ctrl_lalt_altgr) | (lctrl & rctrl))
      return false;

    // Try the layout.
    return layout();
  }

  void ctrl_ch(uchar c) {
    esc_if(alt);
    if (shift && !cfg.ctrl_exchange_shift) {
      // Send C1 control char if the charset supports it.
      // Otherwise prefix the C0 char with ESC.
      if (c < 0x20) {
        wchar wc = c | 0x80;
        int l = cs_wcntombn(buf + len, &wc, cs_cur_max, 1);
        if (l > 0 && buf[len] != '?') {
          len += l;
          return;
        }
      };
      esc_if(!alt);
    }
    ch(c);
  }

  bool ctrl_key(void) {
    bool try_appctrl(wchar wc) {
      switch (wc) {
        when '@' or '[' ... '_' or 'a' ... 'z':
          if (term.app_control & (1 << (wc & 0x1F))) {
            mods = ctrl * MDK_CTRL;
            other_code((wc & 0x1F) + '@');
            return true;
          }
      }
      return false;
    }

    bool try_key(void) {
      wchar wc = undead_keycode();  // should we fold that out into ctrl_key?

      if (try_appctrl(wc))
        return true;

      char c;
      switch (wc) {
        when '@' or '[' ... '_' or 'a' ... 'z': c = CTRL(wc);
        when '/': c = CTRL('_');
        when '?': c = CDEL;
        otherwise: return false;
      }
      ctrl_ch(c);
      return true;
    }

    bool try_shifts(void) {
      shift = is_key_down(VK_LSHIFT) & is_key_down(VK_RSHIFT);
      if (try_key())
        return true;
      shift = is_key_down(VK_SHIFT);
      if (shift || (key >= '0' && key <= '9' && !term.modify_other_keys)) {
        kbd[VK_SHIFT] ^= 0x80;
        if (try_key())
          return true;
        kbd[VK_SHIFT] ^= 0x80;
      }
      return false;
    }

    if (try_shifts())
      return true;
    if (altgr) {
      // Try with AltGr treated as Alt.
      kbd[VK_CONTROL] = 0;
      trace_alt("ctrl_key altgr alt = true\n");
      alt = true;
      return try_shifts();
    }
    return false;
  }

  bool vk_special(string key_mapped) {
    if (!* key_mapped) {
      if (!layout())
        return false;
    }
    else if ((key_mapped[0] & ~037) == 0 && key_mapped[1] == 0)
      ctrl_ch(key_mapped[0]);
    else
      strcode(key_mapped);
    return true;
  }

  switch (key) {
    when VK_RETURN:
      if (old_alt_state > ALT_ALONE) {
        insert_alt_code();
        alt_state = ALT_NONE;
      }
      else
      if (allow_shortcut && !term.shortcut_override && cfg.window_shortcuts
          && alt && !altgr
         )
      {
        trace_resize(("--- Alt-Enter (shift %d ctrl %d)", shift, ctrl));
        send_syscommand((shift && !ctrl) ? IDM_FULLSCREEN_ZOOM : IDM_FULLSCREEN);
        return true;
      }
      else if (extended && term.vt52_mode && term.app_keypad)
        len = sprintf(buf, "\e?M");
      else if (extended && !numlock && term.app_keypad)
        //mod_ss3('M');
        app_pad_code('M' - '@');
      else if (!extended && term.modify_other_keys && (shift || ctrl))
        other_code('\r');
      else if (ctrl && (cfg.old_modify_keys & 32))
        ctrl_ch(CTRL('^'));
      else
        esc_if(alt),
        term.newline_mode ? ch('\r'), ch('\n') : ch(shift ? '\n' : '\r');
    when VK_BACK:
      if (old_alt_state > ALT_ALONE) {
        alt_state = old_alt_state;  // keep alt_state, process key
        alt_code = alt_code / alt_state;
        win_update(false);
      }
      else
      if (cfg.old_modify_keys & 1) {
        if (!ctrl)
          esc_if(alt), ch(term.backspace_sends_bs ? '\b' : CDEL);
        else if (term.modify_other_keys)
          other_code(term.backspace_sends_bs ? '\b' : CDEL);
        else
          ctrl_ch(term.backspace_sends_bs ? CDEL : CTRL('_'));
      }
      else {
        if (term.modify_other_keys > 1 && mods)
          // perhaps also partially if:
          // term.modify_other_keys == 1 && (mods & ~(MDK_CTRL | MDK_ALT)) ?
          other_code(term.backspace_sends_bs ? '\b' : CDEL);
        else {
          esc_if(alt);
          ch(term.backspace_sends_bs ^ ctrl ? '\b' : CDEL);
        }
      }
    when VK_TAB:
      if (alt_code_ignore()) {
      }
      else
      if (!(cfg.old_modify_keys & 2) && term.modify_other_keys > 1 && mods) {
        // perhaps also partially if:
        // term.modify_other_keys == 1 && (mods & ~(MDK_SHIFT | MDK_ALT)) ?
        other_code('\t');
      }
#ifdef handle_alt_tab
      else if (alt) {
        if (cfg.switch_shortcuts) {
          // does not work as Alt+TAB is not passed here anyway;
          // could try something with KeyboardHook:
          // http://www.codeproject.com/Articles/14485/Low-level-Windows-API-hooks-from-C-to-stop-unwante
          win_switch(shift, true);
          return true;
        }
        else
          return false;
      }
#endif
      else if (!ctrl) {
        esc_if(alt);
        shift ? csi('Z') : ch('\t');
      }
      else if (allow_shortcut && cfg.switch_shortcuts) {
        win_switch(shift, lctrl & rctrl);
        return true;
      }
      //else term.modify_other_keys ? other_code('\t') : mod_csi('I');
      else if ((cfg.old_modify_keys & 4) && term.modify_other_keys)
        other_code('\t');
      else {
        esc_if(alt);
        mod_csi('I');
      }
    when VK_ESCAPE:
      if (old_alt_state > ALT_ALONE) {
        alt_state = ALT_CANCELLED;
      }
      else if (comp_state > COMP_NONE) {
        compose_clear();
      }
      else
      if (!(cfg.old_modify_keys & 8) && term.modify_other_keys > 1 && mods)
        other_code('\033');
      else
        term.app_escape_key
        ? ss3('[')
        : ctrl_ch(term.escape_sends_fs ? CTRL('\\') : CTRL('['));
    when VK_PAUSE:
      if (!vk_special(ctrl & !extended ? cfg.key_break : cfg.key_pause))
        // default cfg.key_pause is CTRL(']')
        return false;
    when VK_CANCEL:
      if (!strcmp(cfg.key_break, "_BRK_")) {
        child_break();
        return false;
      }
      if (!vk_special(cfg.key_break))
        // default cfg.key_break is CTRL('\\')
        return false;
    when VK_SNAPSHOT:
      if (!vk_special(cfg.key_prtscreen))
        return false;
    when VK_APPS:
      if (!vk_special(cfg.key_menu))
        return false;
    when VK_SCROLL:
#ifdef debug_vk_scroll
      printf("when VK_SCROLL scn %d\n", scancode);
#endif
      if (scancode)  // prevent recursion...
        // sync_scroll_lock() does not work in this case 
        // if ScrollLock is not defined in KeyFunctions
        win_key_nullify(VK_SCROLL);
      if (!vk_special(cfg.key_scrlock))
        return false;
    when VK_F1 ... VK_F24:
      if (alt_code_ignore()) {
        return true;
      }

      if (key <= VK_F4 && term.vt52_mode) {
        len = sprintf(buf, "\e%c", key - VK_F1 + 'P');
        break;
      }

      if (term.vt220_keys && ctrl && VK_F3 <= key && key <= VK_F10)
        key += 10, mods &= ~MDK_CTRL;

      if (key <= VK_F4)
        mod_ss3(key - VK_F1 + 'P');
      else {
        tilde_code(
          (uchar[]){
            15, 17, 18, 19, 20, 21, 23, 24, 25, 26,
            28, 29, 31, 32, 33, 34, 42, 43, 44, 45
          }[key - VK_F5]
        );
      }
    when VK_INSERT: edit_key(2, '0');
    when VK_DELETE: edit_key(3, '.');
    when VK_PRIOR:  edit_key(5, '9');
    when VK_NEXT:   edit_key(6, '3');
    when VK_HOME:   term.vt220_keys ? edit_key(1, '7') : cursor_key('H', '7');
    when VK_END:    term.vt220_keys ? edit_key(4, '1') : cursor_key('F', '1');
    when VK_UP:     cursor_key('A', '8');
    when VK_DOWN:   cursor_key('B', '2');
    when VK_LEFT:   cursor_key('D', '4');
    when VK_RIGHT:  cursor_key('C', '6');
    when VK_CLEAR:  cursor_key('E', '5');
    when VK_MULTIPLY ... VK_DIVIDE:
      if (term.vt52_mode && term.app_keypad)
        len = sprintf(buf, "\e?%c", key - VK_MULTIPLY + 'j');
      // initiate hex numeric input
      else if (key == VK_ADD && old_alt_state == ALT_ALONE)
        alt_state = ALT_HEX, alt_code = 0, alt_uni = false;
      // initiate decimal numeric input; override user-assigned functions
      else if (key == VK_SUBTRACT && old_alt_state == ALT_ALONE)
        alt_state = ALT_DEC, alt_code = 0, alt_uni = false;
      else if (alt_code_ignore()) {
      }
      else if (mods || (term.app_keypad && !numlock) || !layout())
        app_pad_code(key - VK_MULTIPLY + '*');
    when VK_NUMPAD0 ... VK_NUMPAD9:
      if (term.vt52_mode && term.app_keypad)
        len = sprintf(buf, "\e?%c", key - VK_NUMPAD0 + 'p');
      else if ((term.app_cursor_keys || !term.app_keypad) &&
               alt_code_numpad_key(key - VK_NUMPAD0))
        ;
      else if (layout())
        ;
      else
        app_pad_code(key - VK_NUMPAD0 + '0');
    when 'A' ... 'Z' or ' ': {
      bool check_menu = key == VK_SPACE && !term.shortcut_override
                        && cfg.window_shortcuts && alt && !altgr && !ctrl;
      //// support Ctrl+Shift+AltGr combinations (esp. Ctrl+Shift+@)
      //bool modaltgr = (mods & ~MDK_ALT) == (cfg.ctrl_exchange_shift ? MDK_CTRL : (MDK_CTRL | MDK_SHIFT));
      // support Ctrl+AltGr combinations (esp. Ctrl+@ and Ctrl+Shift+@)
      bool modaltgr = ctrl;
#ifdef debug_key
      printf("-- mods %X alt %d altgr %d/%d ctrl %d lctrl %d/%d (modf %d comp %d)\n", mods, alt, altgr, altgr0, ctrl, lctrl, lctrl0, term.modify_other_keys, comp_state);
#endif
      if (key == ' ' && old_alt_state > ALT_ALONE) {
        insert_alt_code();
        alt_state = ALT_NONE;
      }
      else
      if (key > 'F' && alt_code_ignore()) {
      }
      else
      if (allow_shortcut && check_menu) {
        send_syscommand(SC_KEYMENU);
        return true;
      }
      else if (altgr_key())
        trace_key("altgr");
      else if (!modaltgr && !cfg.altgr_is_alt && altgr0 && !term.modify_other_keys)
        // prevent AltGr from behaving like Alt
        trace_key("!altgr");
      else if (key != ' ' && alt_code_key(key - 'A' + 0xA))
        trace_key("alt");
      else if (term.modify_other_keys > 1 && mods == MDK_SHIFT && !comp_state)
        // catch Shift+space (not losing Alt+ combinations if handled here)
        // only in modify-other-keys mode 2
        modify_other_key();
      else if (!(cfg.old_modify_keys & 16) && term.modify_other_keys > 1 && mods == (MDK_ALT | MDK_SHIFT))
        // catch this case before char_key
        trace_key("alt+shift"),
        modify_other_key();
      else if (char_key())
        trace_key("char");
      else if (term.modify_other_keys > 1 || (term.modify_other_keys && altgr))
        // handle Alt+space after char_key, avoiding undead_ glitch;
        // also handle combinations like Ctrl+AltGr+e
        trace_key("modf"),
        modify_other_key();
      else if (ctrl_key())
        trace_key("ctrl");
      else
        ctrl_ch(CTRL(key));
    }
    when '0' ... '9' or VK_OEM_1 ... VK_OEM_102:
      if (key > '9' && alt_code_ignore()) {
      }
      else
      if (key <= '9' && alt_code_key(key - '0'))
        ;
      else if (char_key())
        trace_key("0... char_key");
      else if (term.modify_other_keys <= 1 && ctrl_key())
        trace_key("0... ctrl_key");
      else if (term.modify_other_keys)
        modify_other_key();
      else if (zoom_hotkey())
        ;
      else if (!cfg.ctrl_controls && (mods & MDK_CTRL))
        return false;
      else if (key <= '9')
        app_pad_code(key);
      else if (VK_OEM_PLUS <= key && key <= VK_OEM_PERIOD)
        app_pad_code(key - VK_OEM_PLUS + '+');
    when VK_PACKET:
      trace_alt("VK_PACKET alt %d lalt %d ralt %d altgr %d altgr0 %d\n", alt, lalt, ralt, altgr, altgr0);
      if (altgr0)
        alt = lalt;
      if (!layout())
        return false;
    otherwise:
      if (!layout())
        return false;
  }

  hide_mouse();
  term_cancel_paste();

  if (len) {
    //printf("[%ld] win_key_down %02X\n", mtime(), key); kb_trace = key;
    clear_scroll_lock();
    provide_input(*buf);
    while (count--)
      child_send(buf, len);
    compose_clear();
    // set token to enforce immediate display of keyboard echo;
    // we cannot win_update_now here; need to wait for the echo (child_proc)
    kb_input = true;
    //printf("[%ld] win_key sent %02X\n", mtime(), key); kb_trace = key;
    if (tek_mode == TEKMODE_GIN)
      tek_send_address();
  }
  else if (comp_state == COMP_PENDING)
    comp_state = COMP_ACTIVE;

  return true;
}

void
win_csi_seq(char * pre, char * suf)
{
  mod_keys mods = get_mods();
  inline bool is_key_down(uchar vk) { return GetKeyState(vk) & 0x80; }
  bool super = super_key && is_key_down(super_key);
  bool hyper = hyper_key && is_key_down(hyper_key);
  mods |= super * MDK_SUPER | hyper * MDK_HYPER;

  if (mods)
    child_printf("\e[%s;%u%s", pre, mods + 1, suf);
  else
    child_printf("\e[%s%s", pre, suf);
}

bool
win_key_up(WPARAM wp, LPARAM lp)
{
  inline bool is_key_down(uchar vk) { return GetKeyState(vk) & 0x80; }

  uint key = wp;
#ifdef debug_virtual_key_codes
  printf("  win_key_up %02X (down %02X) %s\n", key, last_key_down, vk_name(key));
#endif

  if (key == VK_CANCEL) {
    // in combination with Control, this may be the KEYUP event 
    // for VK_PAUSE or VK_SCROLL, so their actual state cannot be 
    // detected properly for use as a modifier; let's try to fix this
    super_key = 0;
    hyper_key = 0;
    compose_key = 0;
  }
  else if (key == VK_SCROLL) {
    // heuristic compensation of race condition with auto-repeat
    sync_scroll_lock(term.no_scroll || term.scroll_mode);
  }

  win_update_mouse();

  uint scancode = HIWORD(lp) & (KF_EXTENDED | 0xFF);
  // avoid impact of fake keyboard events (nullifying implicit Lock states)
  if (!scancode) {
    last_key_up = key;
    return false;
  }

  //printf("comp %d key %02X dn %02X up %02X\n", comp_state, key, last_key_down, last_key_up);
  if (key == last_key_down
      // guard against cases of hotkey injection (#877)
      && (!last_key_up || key == last_key_up)
     )
  {
    if (
        (cfg.compose_key == MDK_CTRL && key == VK_CONTROL) ||
        (cfg.compose_key == MDK_SHIFT && key == VK_SHIFT) ||
        (cfg.compose_key == MDK_ALT && key == VK_MENU)
        || (cfg.compose_key == MDK_SUPER && key == super_key)
        || (cfg.compose_key == MDK_HYPER && key == hyper_key)
        // support KeyFunctions=CapsLock:compose (or other key)
        || key == compose_key
        // support config ComposeKey=capslock
        // needs support by nullifying capslock state (see above)
        || (cfg.compose_key == MDK_CAPSLOCK && key == VK_CAPITAL)
       )
    {
      if (comp_state >= 0) {
        comp_state = COMP_ACTIVE;
        win_update(false);
      }
    }
  }

  last_key_up = key;

  if (newwin_pending) {
    if (key == newwin_key) {
      if (is_key_down(VK_SHIFT))
        newwin_shifted = true;
#ifdef control_AltF2_size_via_token
      if (newwin_shifted /*|| win_is_fullscreen*/)
        clone_size_token = false;
#endif

      newwin_pending = false;

      // Calculate heuristic approximation of selected monitor position
      int x, y;
      MONITORINFO mi;
      search_monitors(&x, &y, 0, newwin_home, &mi);
      RECT r = mi.rcMonitor;
      int refx, refy;
      if (newwin_monix < 0)
        refx = r.left + 10;
      else if (newwin_monix > 0)
        refx = r.right - 10;
      else
        refx = (r.left + r.right) / 2;
      if (newwin_moniy < 0)
        refy = r.top + 10;
      else if (newwin_monix > 0)
        refy = r.bottom - 10;
      else
        refy = (r.top + r.bottom) / 2;
      POINT pt;
      pt.x = refx + newwin_monix * x;
      pt.y = refy + newwin_moniy * y;
      // Find monitor over or nearest to point
      HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
      int moni = search_monitors(&x, &y, mon, true, 0);

#ifdef debug_multi_monitors
      printf("NEW @ %d,%d @ monitor %d\n", pt.x, pt.y, moni);
#endif
      if (newwin_monix || newwin_moniy ||
          (is_key_down(VK_LSHIFT) && is_key_down(VK_RSHIFT))
         )
        // enforce new window, not tab
        send_syscommand2(IDM_NEW_MONI, moni);
      else
        send_syscommand(IDM_TAB);
    }
  }
  if (transparency_pending) {
    transparency_pending--;
#ifdef debug_transparency
    printf("--%d\n", transparency_pending);
#endif
    if (!transparency_tuned)
      cycle_transparency();
    if (!transparency_pending && cfg.opaque_when_focused)
      win_update_transparency(cfg.transparency, true);
  }

#if 0
  // "unhovering" is now handled in update_mouse, based on configured mods
  if (key == VK_CONTROL && term.hovering) {
    term.hovering = false;
    win_update(false);
  }
#endif

  if (key == VK_MENU) {
    if (alt_state > ALT_ALONE && alt_code) {
      insert_alt_code();
    }
    alt_state = ALT_NONE;
    return true;
  }

  return false;
}

// simulate a key press/release sequence
static int
win_key_fake(int vk)
{
  if (!cfg.manage_leds || (cfg.manage_leds < 4 && vk == VK_SCROLL))
    return 0;

  //printf("-> win_key_fake %02X\n", vk);
  INPUT ki[2];
  ki[0].type = INPUT_KEYBOARD;
  ki[1].type = INPUT_KEYBOARD;
  ki[0].ki.dwFlags = 0;
  ki[1].ki.dwFlags = KEYEVENTF_KEYUP;
  ki[0].ki.wVk = vk;
  ki[1].ki.wVk = vk;
  ki[0].ki.wScan = 0;
  ki[1].ki.wScan = 0;
  ki[0].ki.time = 0;
  ki[1].ki.time = 0;
  ki[0].ki.dwExtraInfo = 0;
  ki[1].ki.dwExtraInfo = 0;
  return SendInput(2, ki, sizeof(INPUT));
}

void
do_win_key_toggle(int vk, bool on)
{
  // this crap does not work
  return;

  // use some heuristic combination to detect the toggle state
  int delay = 33333;
  usleep(delay);
  int st = GetKeyState(vk);  // volatile; save in case of debugging
  int ast = GetAsyncKeyState(vk);  // volatile; save in case of debugging
#define dont_debug_key_state
#ifdef debug_key_state
  uchar kbd[256];
  GetKeyboardState(kbd);
  printf("do_win_key_toggle %02X %d (st %02X as %02X kb %02X)\n", vk, on, st, ast, kbd[vk]);
#endif
  if (((st | ast) & 1) != on) {
    win_key_fake(vk);
    usleep(delay);
  }
  /* It is possible to switch the LED only and revert the actual 
     virtual input state of the current thread as it was by using 
     SetKeyboardState in win_key_down, but this "fix" would only 
     apply to the current window while the effective state remains 
     switched for other windows which makes no sense.
   */
}

static void
win_key_toggle(int vk, bool on)
{
  //printf("send IDM_KEY_DOWN_UP %02X\n", vk | (on ? 0x10000 : 0));
  send_syscommand2(IDM_KEY_DOWN_UP, vk | (on ? 0x10000 : 0));
}

void
win_led(int led, bool set)
{
  //printf("\n[%ld] win_led %d %d\n", mtime(), led, set);
  int led_keys[] = {VK_NUMLOCK, VK_CAPITAL, VK_SCROLL};
  if (led <= 0)
    for (uint i = 0; i < lengthof(led_keys); i++)
      win_key_toggle(led_keys[i], set);
  else if (led <= (int)lengthof(led_keys))
    win_key_toggle(led_keys[led - 1], set);
}

bool
get_scroll_lock(void)
{
  return GetKeyState(VK_SCROLL);
}

void
sync_scroll_lock(bool locked)
{
  //win_led(3, term.no_scroll);
  //do_win_key_toggle(VK_SCROLL, locked);
  int st = GetKeyState(VK_SCROLL);
  //printf("sync_scroll_lock %d key %d\n", locked, st);
  if (st ^ locked)
    win_key_fake(VK_SCROLL);
}

