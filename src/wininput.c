// wininput.c (part of mintty)
// Copyright 2008-12 Andy Koppe, 2015-2018 Thomas Wolff
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"
#include "winsearch.h"

#include "charset.h"
#include "child.h"

#include <math.h>
#include <windowsx.h>
#include <winnls.h>
#include <termios.h>

static HMENU ctxmenu = NULL;
static HMENU sysmenu;
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
  SetBkColor(dst_hdc, GetSysColor(COLOR_MENU));
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
      while (isspace(*cmdp))
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
  uint bar = vsep ? MF_MENUBARBREAK : 0;
  if (hsep)
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
  //__ Context menu, session switcher ("virtual tabs")
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
  if (*cfg.session_commands) {
    uint bar = vsep ? MF_MENUBARBREAK : 0;
    if (hsep)
      AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    //__ Context menu, session launcher ("virtual tabs")
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
  modify_menu(sysmenu, IDM_NEW, 0, _W("Ne&w"),
    alt_fn ? W("Alt+F2") : ct_sh ? W("Ctrl+Shift+N") : null
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
        while (isspace(*cmdp))
          cmdp++;
      }
    }
    free(cmds);
  }
  if (*cfg.ctx_user_commands)
    check_commands(ctxmenu, cfg.ctx_user_commands, IDM_CTXMENUFUNCTION);
  if (*cfg.sys_user_commands)
    check_commands(sysmenu, cfg.sys_user_commands, IDM_SYSMENUFUNCTION);
}

static bool
add_user_commands(HMENU menu, bool vsep, bool hsep, wstring title, wstring commands, UINT_PTR idm_cmd)
{
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
  AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_SEARCH, 0);
  if (extended_menu) {
    //__ Context menu:
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_HTML, _W("HTML Screen Dump"));
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TOGLOG, 0);
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TOGCHARINFO, 0);
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_TOGVT220KB, 0);
  }
  AppendMenuW(ctxmenu, MF_ENABLED, IDM_RESET, 0);
  if (extended_menu) {
    //__ Context menu:
    AppendMenuW(ctxmenu, MF_ENABLED, IDM_CLRSCRLBCK, _W("Clear Scrollback"));
  }
  AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_DEFSIZE_ZOOM, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_SCROLLBAR, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_FULLSCREEN_ZOOM, 0);
  AppendMenuW(ctxmenu, MF_ENABLED | MF_UNCHECKED, IDM_FLIPSCREEN, 0);
  AppendMenuW(ctxmenu, MF_SEPARATOR, 0, 0);
  if (extended_menu) {
    //__ Context menu:
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
  }

  InsertMenuW(sysmenu, SC_CLOSE, MF_SEPARATOR, 0, 0);
}

static void
open_popup_menu(bool use_text_cursor, string menucfg, mod_keys mods)
{
  /* Create a new context menu structure every time the menu is opened.
     This was a fruitless attempt to achieve its proper DPI scaling.
     It also supports opening different menus (Ctrl+ for extended menu).
     if (mods & MDK_CTRL) open extended menu...
   */
  if (ctxmenu)
    DestroyMenu(ctxmenu);

  ctxmenu = CreatePopupMenu();

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


/* Mouse and Keyboard modifiers */

typedef enum {
  ALT_CANCELLED = -1, ALT_NONE = 0, ALT_ALONE = 1,
  ALT_OCT = 8, ALT_DEC = 10, ALT_HEX = 16
} alt_state_t;
static alt_state_t alt_state;
static uint alt_code;

static bool lctrl;  // Is left Ctrl pressed?
static int lctrl_time;

static mod_keys
get_mods(void)
{
  inline bool is_key_down(uchar vk) { return GetKeyState(vk) & 0x80; }
  lctrl_time = 0;
  lctrl = is_key_down(VK_LCONTROL) && (lctrl || !is_key_down(VK_RMENU));
  return
    is_key_down(VK_SHIFT) * MDK_SHIFT
    | is_key_down(VK_MENU) * MDK_ALT
    | (lctrl | is_key_down(VK_RCONTROL)) * MDK_CTRL
    | (is_key_down(VK_LWIN) | is_key_down(VK_RWIN)) * MDK_WIN
    ;
}


/* Mouse handling */

static void
update_mouse(mod_keys mods)
{
  static bool app_mouse;
  bool new_app_mouse =
    term.mouse_mode && !term.show_other_screen &&
    cfg.clicks_target_app ^ ((mods & cfg.click_target_mod) != 0);
  if (new_app_mouse != app_mouse) {
    HCURSOR cursor = LoadCursor(null, new_app_mouse ? IDC_ARROW : IDC_IBEAM);
    SetClassLongPtr(wnd, GCLP_HCURSOR, (LONG_PTR)cursor);
    SetCursor(cursor);
    app_mouse = new_app_mouse;
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
  return (pos){
    .x = floorf((x - PADDING) / (float)cell_width),
    .y = floorf((y - PADDING) / (float)cell_height),
    .r = (cfg.elastic_mouse && !term.mouse_mode)
         ? (x - PADDING) % cell_width > cell_width / 2
         : 0
  };
}

pos last_pos = {-1, -1, false};
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

void
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
  }
  else {
    if (last_skipped && dblclick) {
      // recognize double click also in application mouse modes
      term_mouse_click(b, mods, p, 1);
    }
    term_mouse_click(b, mods, p, count);
    last_skipped = false;
  }
  last_pos = (pos){INT_MIN, INT_MIN, false};
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
win_mouse_wheel(WPARAM wp, LPARAM lp)
{
  // WM_MOUSEWHEEL reports screen coordinates rather than client coordinates
  POINT wpos = {.x = GET_X_LPARAM(lp), .y = GET_Y_LPARAM(lp)};
  ScreenToClient(wnd, &wpos);
  pos tpos = translate_pos(wpos.x, wpos.y);

  int delta = GET_WHEEL_DELTA_WPARAM(wp);  // positive means up
  int lines_per_notch;
  SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines_per_notch, 0);

  term_mouse_wheel(delta, lines_per_notch, get_mods(), tpos);
}

void
win_get_locator_info(int *x, int *y, int *buttons, bool by_pixels)
{
  POINT p = {-1, -1};

  if (GetCursorPos(&p)) {
    if (ScreenToClient(wnd, &p)) {
      if (by_pixels) {
        *x = p.x - PADDING;
        *y = p.y - PADDING;
      } else {
        *x = floorf((p.x - PADDING) / (float)cell_width);
        *y = floorf((p.y - PADDING) / (float)cell_height);
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
  win_update_transparency(false);
}

static void
set_transparency(int t)
{
  if (t >= 128)
    t = 127;
  else if (t < 0)
    t = 0;
  cfg.transparency = t;
  win_update_transparency(false);
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
    win_update_transparency(false);
}

static void
newwin_begin()
{
  newwin_pending = true;
  newwin_home = false; newwin_monix = 0; newwin_moniy = 0;
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
toggle_bidi()
{
  term.disable_bidi = !term.disable_bidi;
}

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
mflags_bidi()
{
  return (cfg.bidi == 0
         || (cfg.bidi == 1 && (term.on_alt_screen ^ term.show_other_screen))
         ) ? MF_GRAYED
           : term.disable_bidi ? MF_UNCHECKED : MF_CHECKED;
}

static uint
mflags_options()
{
  return config_wnd ? MF_GRAYED : MF_ENABLED;
}

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
  //{"new-monitor", {IDM_NEW_MONI}, 0},

  //{"default-size", {IDM_DEFSIZE}, 0},
  {"default-size", {IDM_DEFSIZE_ZOOM}, mflags_defsize},
  {"toggle-fullscreen", {IDM_FULLSCREEN}, mflags_fullscreen},
  {"fullscreen", {.fct = window_full}, mflags_fullscreen},
  {"win-max", {.fct = window_max}, mflags_zoomed},
  {"win-toggle-max", {.fct = window_toggle_max}, mflags_zoomed},
  {"win-restore", {.fct = window_restore}, 0},
  {"win-icon", {.fct = window_min}, 0},
  {"close", {.fct = win_close}, 0},

  {"new", {.fct = newwin_begin}, 0},  // deprecated
  {"new-key", {.fct = newwin_begin}, 0},
  {"options", {IDM_OPTIONS}, mflags_options},
  {"menu-text", {.fct = menu_text}, 0},
  {"menu-pointer", {.fct = menu_pointer}, 0},

  {"search", {IDM_SEARCH}, 0},
  {"scrollbar-outer", {IDM_SCROLLBAR}, mflags_scrollbar_outer},
  {"scrollbar-inner", {.fct = toggle_scrollbar}, mflags_scrollbar_inner},
  {"cycle-pointer-style", {.fct = cycle_pointer_style}, 0},
  {"cycle-transparency-level", {.fct = transparency_level}, 0},

  {"copy", {IDM_COPY}, mflags_copy},
  {"copy-text", {IDM_COPY_TEXT}, mflags_copy},
  {"copy-rtf", {IDM_COPY_RTF}, mflags_copy},
  {"copy-html-text", {IDM_COPY_HTXT}, mflags_copy},
  {"copy-html-format", {IDM_COPY_HFMT}, mflags_copy},
  {"copy-html-full", {IDM_COPY_HTML}, mflags_copy},
  {"paste", {IDM_PASTE}, mflags_paste},
  {"copy-paste", {IDM_COPASTE}, mflags_copy},
  {"select-all", {IDM_SELALL}, 0},
  {"clear-scrollback", {IDM_CLRSCRLBCK}, 0},
  {"copy-title", {IDM_COPYTITLE}, 0},
  {"lock-title", {.fct = lock_title}, mflags_lock_title},
  {"reset", {IDM_RESET}, 0},
  {"break", {IDM_BREAK}, 0},
  {"flipscreen", {IDM_FLIPSCREEN}, mflags_flipscreen},
  {"open", {IDM_OPEN}, mflags_open},
  {"toggle-logging", {IDM_TOGLOG}, mflags_logging},
  {"toggle-char-info", {IDM_TOGCHARINFO}, mflags_char_info},
  {"export-html", {IDM_HTML}, 0},
  {"print-screen", {.fct = print_screen}, 0},
  {"toggle-vt220", {.fct = toggle_vt220}, mflags_vt220},
  {"toggle-bidi", {.fct = toggle_bidi}, mflags_bidi},

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
static uint last_key = 0;

static struct {
  wchar kc[4];
  char * s;
} composed[] = {
#include "composed.t"
};
static wchar compose_buf[lengthof(composed->kc) + 4];
static int compose_buflen = 0;

static void
compose_clear()
{
  comp_state = COMP_CLEAR;
  compose_buflen = 0;
  last_key = 0;
}

void
win_key_reset(void)
{
  alt_state = ALT_NONE;
  compose_clear();
}

#define dont_debug_virtual_key_codes

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

#define dont_debug_key
#define dont_debug_compose

#ifdef debug_compose
# define debug_key
#endif

#ifdef debug_key
#define trace_key(tag)	printf(" <-%s\n", tag)
#else
#define trace_key(tag)	
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

static int
pick_key_function(wstring key_commands, char * tag, int n, uint key, mod_keys mods)
{
  char * ukey_commands = cs__wcstoutf(key_commands);
  char * cmdp = ukey_commands;
  char sepch = ';';
  if ((uchar)*cmdp <= (uchar)' ')
    sepch = *cmdp++;

  char * paramp;
  while ((tag || n >= 0) && (paramp = strchr(cmdp, ':'))) {
    *paramp = '\0';
    paramp++;
    char * sepp = strchr(paramp, sepch);
    if (sepp)
      *sepp = '\0';

#if defined(debug_def_keys) && debug_def_keys > 1
    printf("tag <%s>: cmd <%s> fct <%s>\n", tag, cmdp, paramp);
#endif
    if (tag ? !strcmp(cmdp, tag) : n == 0) {
#if defined(debug_def_keys) && debug_def_keys == 1
      printf("tag <%s>: cmd <%s> fct <%s>\n", tag, cmdp, paramp);
#endif
      int ret = true;
      wchar * fct = cs__utftowcs(paramp);
      if ((*fct == '"' && fct[wcslen(fct) - 1] == '"') ||
          (*fct == '\'' && fct[wcslen(fct) - 1] == '\'')) {
        child_sendw(&fct[1], wcslen(fct) - 2);
      }
      else if (*fct == '`' && fct[wcslen(fct) - 1] == '`') {
        fct[wcslen(fct) - 1] = 0;
        char * cmd = cs__wcstombs(&fct[1]);
        term_cmd(cmd);
        free(cmd);
      }
      else if (!*paramp) {
        // empty definition (e.g. "A+Enter:;"), shall disable 
        // further shortcut handling for the input key but 
        // trigger fall-back to "normal" key handling (with mods)
        ret = -1;
      }
      else {
        ret = false;
        for (uint i = 0; i < lengthof(cmd_defs); i++) {
          if (!strcmp(paramp, cmd_defs[i].name)) {
            if (cmd_defs[i].cmd < 0xF000)
              send_syscommand(cmd_defs[i].cmd);
            else if (cmd_defs[i].fct == newwin_begin) {
              if (key) {
                newwin_begin();
                newwin_key = key;
                if (mods & MDK_SHIFT)
                  newwin_shifted = true;
                else
                  newwin_shifted = false;
              }
            }
            else
              cmd_defs[i].fct();
            ret = true;
            break;
          }
        }
        if (ret != true) {
          // invalid definition (e.g. "A+Enter:foo;"), shall 
          // not cause any action (return true) but provide a feedback
          win_bell(&cfg);
          ret = true;
        }
      }

      free(fct);
      free(ukey_commands);
      return ret;
    }

    n--;
    if (sepp) {
      cmdp = sepp + 1;
      // check for multi-line separation
      if (*cmdp == '\\' && cmdp[1] == '\n') {
        cmdp += 2;
        while (isspace(*cmdp))
          cmdp++;
      }
    }
    else
      break;
  }
  free(ukey_commands);
  return false;
}

void
user_function(wstring commands, int n)
{
  pick_key_function(commands, 0, n, 0, 0);
}

bool
win_key_down(WPARAM wp, LPARAM lp)
{
  uint key = wp;
  last_key = key;

  if (comp_state == COMP_ACTIVE)
    comp_state = COMP_PENDING;
  else if (comp_state == COMP_CLEAR)
    comp_state = COMP_NONE;

  uint scancode = HIWORD(lp) & (KF_EXTENDED | 0xFF);
  bool extended = HIWORD(lp) & KF_EXTENDED;
  bool repeat = HIWORD(lp) & KF_REPEAT;
  uint count = LOWORD(lp);

#ifdef debug_virtual_key_codes
  printf("win_key_down %04X %s scan %d ext %d\n", key, vk_name(key), scancode, extended);
#endif

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
  printf(" [%d %c%d] Shift %d:%d/%d Ctrl %d:%d/%d Alt %d:%d/%d\n",
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
  }
  else if (lctrl_time) {
    lctrl = !(key == VK_MENU && extended 
              && GetMessageTime() - lctrl_time <= cfg.ctrl_alt_delay_altgr);
    lctrl_time = 0;
  }
  else {
    lctrl = is_key_down(VK_LCONTROL) && (lctrl || !is_key_down(VK_RMENU));
  }

  bool numlock = kbd[VK_NUMLOCK] & 1;
  bool shift = is_key_down(VK_SHIFT);
  bool lalt = is_key_down(VK_LMENU);
  bool ralt = is_key_down(VK_RMENU);
  bool alt = lalt | ralt;
  bool external_hotkey = false;
  if (ralt && !scancode && cfg.external_hotkeys) {
    // Support external hot key injection by overriding disabled Alt+Fn
    // and fix buggy StrokeIt (#833).
    ralt = false;
    if (cfg.external_hotkeys > 1)
      external_hotkey = true;
  }
  bool rctrl = is_key_down(VK_RCONTROL);
  bool ctrl = lctrl | rctrl;
  bool ctrl_lalt_altgr = cfg.ctrl_alt_is_altgr & ctrl & lalt & !ralt;
  bool altgr = ralt | ctrl_lalt_altgr;
  bool win = (is_key_down(VK_LWIN) && key != VK_LWIN)
          || (is_key_down(VK_RWIN) && key != VK_RWIN);

  mod_keys mods = shift * MDK_SHIFT
                | alt * MDK_ALT
                | ctrl * MDK_CTRL
                | win * MDK_WIN
                ;

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

  alt_state_t old_alt_state = alt_state;
  if (alt_state > ALT_NONE)
    alt_state = ALT_CANCELLED;

  // Context and window menus
  if (key == VK_APPS && !*cfg.key_menu) {
    if (shift)
      send_syscommand(SC_KEYMENU);
    else {
      win_show_mouse();
      open_popup_menu(false, 
                      mods & MDK_CTRL ? cfg.menu_ctrlmenu : cfg.menu_menu, 
                      mods);
    }
    return true;
  }

  // Exit when pressing Enter or Escape while holding the window open after
  // the child process has died.
  if ((key == VK_RETURN || key == VK_ESCAPE) && !mods && !child_is_alive())
    exit_mintty();

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
      when VK_CLEAR : cfg.transparency = TR_GLASS;
                      win_update_transparency(false);
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
      when VK_CLEAR:  // recalibrate
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
    //  SendMessage(wnd, WM_VSCROLL, scroll, 0);
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

  bool allow_shortcut = true;

  if (!term.shortcut_override) {

#define dont_debug_def_keys 1

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
      if (vki >= 0 && !altgr
          && (mods || vktab[vki].unmod || extended)
          && (!editpad || !term.app_cursor_keys)
          && (!keypad || !term.app_keypad)
         )
      {
        tag = asform("%s%s%s%s%s%s%s",
                     ctrl ? "C" : "",
                     alt ? "A" : "",
                     shift ? "S" : "",
                     win ? "W" : "",
                     mods ? "+" : "",
                     keypad ? "KP_" : "",
                     vktab[vki].nam);
      }
      else if (VK_F1 <= key && key <= VK_F24) {
        tag = asform("%s%s%s%s%sF%d",
                     ctrl ? "C" : "",
                     alt ? "A" : "",
                     shift ? "S" : "",
                     win ? "W" : "",
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
              )
      {
        uchar kbd0[256];
        GetKeyboardState(kbd0);
        wchar wbuf[4];
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
            tag = asform("%s%s%s%s",
                         alt ? "A" : "",
                         win ? "W" : "",
                         (alt | win) ? "+" : "",
                         keytag);
            free(keytag);
          }
        }
#ifdef debug_def_keys
        printf("key %04X <%s>\n", *wbuf, tag);
#endif
      }
      if (tag) {
        int ret = pick_key_function(cfg.key_commands, tag, 0, key, mods);
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

#ifdef check_alt_ret_space_first
    // Moved to switch() below so we can override it with layout().
    // Window menu and fullscreen
    if (cfg.window_shortcuts && alt && !altgr && !ctrl) {
      if (key == VK_RETURN) {
        trace_resize(("--- Alt-Enter (shift %d)", shift));
        send_syscommand(IDM_FULLSCREEN_ZOOM);
        return true;
      }
      else if (key == VK_SPACE) {
        send_syscommand(SC_KEYMENU);
        return true;
      }
    }
#endif

    // Alt+Fn shortcuts
    if ((cfg.alt_fn_shortcuts || external_hotkey)
        && alt && !altgr
        && VK_F1 <= key && key <= VK_F24
       )
    {
      if (!ctrl) {
        switch (key) {
          when VK_F2:
            // defer send_syscommand(IDM_NEW) until key released
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
          when VK_F11: send_syscommand(IDM_FULLSCREEN_ZOOM);
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
        when 'N': send_syscommand(IDM_NEW);
        when 'W': send_syscommand(SC_CLOSE);
        when 'R': send_syscommand(IDM_RESET);
        when 'D': send_syscommand(IDM_DEFSIZE);
        when 'F': send_syscommand(IDM_FULLSCREEN);
        when 'S': send_syscommand(IDM_FLIPSCREEN);
        when 'H': send_syscommand(IDM_SEARCH);
        when 'T': if (!transparency_pending) {
                    previous_transparency = cfg.transparency;
                    transparency_pending = 1;
                    transparency_tuned = false;
                  }
                  if (cfg.opaque_when_focused)
                    win_update_transparency(false);
#ifdef debug_transparency
                  printf("++%d\n", transparency_pending);
#endif
        when 'P': cycle_pointer_style();
        when 'O': toggle_scrollbar();
      }
      return true;
    }

    // Scrollback and Selection via keyboard
    if (!term.on_alt_screen || term.show_other_screen) {
      mod_keys scroll_mod = cfg.scroll_mod ?: 128;
      if (cfg.pgupdn_scroll && (key == VK_PRIOR || key == VK_NEXT) &&
          !(mods & ~scroll_mod))
        mods ^= scroll_mod;
      if (mods == scroll_mod) {
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
  void mod_csi(char c) { len = sprintf(buf, "\e[1;%c%c", mods + '1', c); }
  void mod_ss3(char c) { mods ? mod_csi(c) : ss3(c); }
  void tilde_code(uchar code) {
    len = sprintf(buf, mods ? "\e[%i;%c~" : "\e[%i~", code, mods + '1');
  }
  void other_code(wchar c) {
#ifdef support_alt_meta_combinations
    // not too useful as mintty doesn't support Alt even with F-keys at all
    if (altgr && is_key_down(VK_LMENU))
      len = sprintf(buf, "\e[%u;%du", c, mods + 9);
    else
#endif
    trace_key("other");
    len = sprintf(buf, "\e[%u;%cu", c, mods + '1');
  }
  void app_pad_code(char c) {
    void mod_appl_xterm(char c) {len = sprintf(buf, "\eO%c%c", mods + '1', c);}
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
    if (old_alt_state > ALT_ALONE && digit < old_alt_state) {
      alt_state = old_alt_state;
      alt_code = alt_code * alt_state + digit;
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
    return symbol != '.' && alt_code_numpad_key(symbol - '0');
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
    if (!app_pad_key(symbol))
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
    // ToUnicode returns up to 4 wchars according to
    // http://blogs.msdn.com/b/michkap/archive/2006/03/24/559169.aspx
    // https://web.archive.org/web/20120103012712/http://blogs.msdn.com/b/michkap/archive/2006/03/24/559169.aspx
    wchar wbuf[4];
    int wlen = ToUnicode(key, scancode, kbd, wbuf, lengthof(wbuf), 0);
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
    if (comp_state) {
#ifdef debug_compose
      printf("comp (%d)", wlen);
      for (int i = 0; i < compose_buflen; i++) printf(" %04X", compose_buf[i]);
      printf(" +");
      for (int i = 0; i < wlen; i++) printf(" %04X", wbuf[i]);
      printf("\n");
#endif
      for (int i = 0; i < wlen; i++)
        compose_buf[compose_buflen++] = wbuf[i];
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
    printf("undead %04X scn %d -> %d %04X\n", key, scancode, len, wc);
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
      printf("modf !wc mods %d shft %d\n", mods, mods & MDK_SHIFT);
#endif
      if (mods & MDK_SHIFT) {
        kbd[VK_SHIFT] = 0;
        wc = undead_keycode();
      }
    }
#ifdef debug_key
    printf("modf wc %04X\n", wc);
#endif
    if (wc) {
      if (altgr && !is_key_down(VK_LMENU))
        mods &= ~ MDK_ALT;
      other_code(wc);
    }
  }

  bool char_key(void) {
    alt = lalt & !ctrl_lalt_altgr;

    // Sync keyboard layout with our idea of AltGr.
    kbd[VK_CONTROL] = altgr ? 0x80 : 0;

    // Don't handle Ctrl combinations here.
    // Need to check there's a Ctrl that isn't part of Ctrl+LeftAlt==AltGr.
    if ((ctrl & !ctrl_lalt_altgr) | (lctrl & rctrl))
      return false;

    // Try the layout.
    if (layout())
      return true;

    if (ralt) {
      // Try with RightAlt/AltGr key treated as Alt.
      kbd[VK_CONTROL] = 0;
      alt = true;
      layout();
      return true;
    }
    return !ctrl;
  }

  bool altgr_key(void) {
    if (!altgr)
      return false;

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
      if (allow_shortcut && !term.shortcut_override && cfg.window_shortcuts
          && alt && !altgr && !ctrl
         )
      {
        trace_resize(("--- Alt-Enter (shift %d)", shift));
        send_syscommand(IDM_FULLSCREEN_ZOOM);
        return true;
      }
      else if (extended && !numlock && term.app_keypad)
        //mod_ss3('M');
        app_pad_code('M' - '@');
      else if (!extended && term.modify_other_keys && (shift || ctrl))
        other_code('\r');
      else if (!ctrl)
        esc_if(alt),
        term.newline_mode ? ch('\r'), ch('\n') : ch(shift ? '\n' : '\r');
      else
        ctrl_ch(CTRL('^'));
    when VK_BACK:
      if (!ctrl)
        esc_if(alt), ch(term.backspace_sends_bs ? '\b' : CDEL);
      else if (term.modify_other_keys)
        other_code(term.backspace_sends_bs ? '\b' : CDEL);
      else
        ctrl_ch(term.backspace_sends_bs ? CDEL : CTRL('_'));
    when VK_TAB:
#ifdef handle_alt_tab
      if (alt) {
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
      if (!ctrl)
        shift ? csi('Z') : ch('\t');
      else if (allow_shortcut && cfg.switch_shortcuts) {
        win_switch(shift, lctrl & rctrl);
        return true;
      }
      else
        term.modify_other_keys ? other_code('\t') : mod_csi('I');
    when VK_ESCAPE:
      term.app_escape_key
      ? ss3('[')
      : ctrl_ch(term.escape_sends_fs ? CTRL('\\') : CTRL('['));
    when VK_PAUSE:
      if (!vk_special(ctrl & !extended ? cfg.key_break : cfg.key_pause))
        return false;
    when VK_CANCEL:
      if (!strcmp(cfg.key_break, "_BRK_")) {
        child_break();
        return false;
      }
      if (!vk_special(cfg.key_break))
        return false;
    when VK_SNAPSHOT:
      if (!vk_special(cfg.key_prtscreen))
        return false;
    when VK_APPS:
      if (!vk_special(cfg.key_menu))
        return false;
    when VK_SCROLL:
      if (!vk_special(cfg.key_scrlock))
        return false;
    when VK_F1 ... VK_F24:
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
      if (key == VK_ADD && old_alt_state == ALT_ALONE)
        alt_state = ALT_HEX, alt_code = 0;
      else if (mods || (term.app_keypad && !numlock) || !layout())
        app_pad_code(key - VK_MULTIPLY + '*');
    when VK_NUMPAD0 ... VK_NUMPAD9:
      if ((term.app_cursor_keys || !term.app_keypad) &&
          alt_code_numpad_key(key - VK_NUMPAD0));
      else if (layout())
        ;
      else
        app_pad_code(key - VK_NUMPAD0 + '0');
    when 'A' ... 'Z' or ' ': {
      bool check_menu = key == VK_SPACE && !term.shortcut_override
                        && cfg.window_shortcuts && alt && !altgr && !ctrl;
#ifdef debug_key
      printf("mods %d (modf %d comp %d)\n", mods, term.modify_other_keys, comp_state);
#endif
      if (altgr_key())
        trace_key("altgr");
      else if (allow_shortcut && check_menu) {
        send_syscommand(SC_KEYMENU);
        return true;
      }
      else if (key != ' ' && alt_code_key(key - 'A' + 0xA))
        trace_key("alt");
      else if (term.modify_other_keys > 1 && mods == MDK_SHIFT && !comp_state)
        // catch Shift+space (not losing Alt+ combinations if handled here)
        modify_other_key();
      else if (char_key())
        trace_key("char");
      else if (term.modify_other_keys > 1)
        // handled Alt+space after char_key, avoiding undead_ glitch
        modify_other_key();
      else if (ctrl_key())
        trace_key("ctrl");
      else
        ctrl_ch(CTRL(key));
    }
    when '0' ... '9' or VK_OEM_1 ... VK_OEM_102:
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
    while (count--)
      child_send(buf, len);
    compose_clear();
    // set token to enforce immediate display of keyboard echo;
    // we cannot win_update_now here; need to wait for the echo (child_proc)
    kb_input = true;
    //printf("[%ld] win_key sent %02X\n", mtime(), key); kb_trace = key;
  }
  else if (comp_state == COMP_PENDING)
    comp_state = COMP_ACTIVE;

  return true;
}

bool
win_key_up(WPARAM wp, LPARAM unused(lp))
{
  uint key = wp;
#ifdef debug_virtual_key_codes
  printf("  win_key_up %04X %s\n", key, vk_name(key));
#endif

  win_update_mouse();

  if (key == last_key) {
    if (
        (cfg.compose_key == MDK_CTRL && key == VK_CONTROL) ||
        (cfg.compose_key == MDK_SHIFT && key == VK_SHIFT) ||
        (cfg.compose_key == MDK_ALT && key == VK_MENU)
       )
      comp_state = COMP_ACTIVE;
  }

  if (newwin_pending) {
    if (key == newwin_key) {
      inline bool is_key_down(uchar vk) { return GetKeyState(vk) & 0x80; }
      if (is_key_down(VK_SHIFT))
        newwin_shifted = true;
      if (newwin_shifted || win_is_fullscreen)
        clone_size_token = false;

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
      send_syscommand2(IDM_NEW_MONI, moni);
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
      win_update_transparency(true);
  }

  if (key == VK_CONTROL && term.hovering) {
    term.hovering = false;
    win_update(false);
  }

  if (key != VK_MENU)
    return false;

  if (alt_state > ALT_ALONE && alt_code) {
    if (cs_cur_max < 4) {
      char buf[4];
      int pos = sizeof buf;
      do
        buf[--pos] = alt_code;
      while (alt_code >>= 8);
      child_send(buf + pos, sizeof buf - pos);
      compose_clear();
    }
    else if (alt_code < 0x10000) {
      wchar wc = alt_code;
      if (wc < 0x20)
        MultiByteToWideChar(CP_OEMCP, MB_USEGLYPHCHARS,
                            (char[]){wc}, 1, &wc, 1);
      child_sendw(&wc, 1);
      compose_clear();
    }
    else {
      xchar xc = alt_code;
      child_sendw((wchar[]){high_surrogate(xc), low_surrogate(xc)}, 2);
      compose_clear();
    }
  }

  alt_state = ALT_NONE;
  return true;
}

static int
win_key_fake(uchar vk)
{
  INPUT ki[2];
  ki[0].type = INPUT_KEYBOARD;
  ki[1].type = INPUT_KEYBOARD;
  ki[0].ki.dwFlags = 0;
  ki[1].ki.dwFlags = KEYEVENTF_KEYUP;
  ki[0].ki.wVk = vk;
  ki[1].ki.wVk = vk;
  ki[0].ki.time = 0;
  ki[1].ki.time = 0;
  ki[0].ki.dwExtraInfo = 0;
  ki[1].ki.dwExtraInfo = 0;
  return SendInput(2, ki, sizeof(INPUT));
}

static void
win_vk(int vk, bool on)
{
  if ((GetKeyState(vk) & 1) != on)
    win_key_fake(vk);
  /* It is possible to switch the LED only and revert the actual 
     virtual input state of the current thread as it was by using 
     SetKeyboardState in win_key_down, but this "fix" would only 
     apply to the current window while the effective state remains 
     switched for other windows which makes no sense.
   */
}

void
win_led(int led, bool set)
{
  int led_keys[] = {VK_NUMLOCK, VK_CAPITAL, VK_SCROLL};
  if (led <= 0)
    for (uint i = 0; i < lengthof(led_keys); i++)
      win_vk(led_keys[i], set);
  else if (led <= (int)lengthof(led_keys))
    win_vk(led_keys[led - 1], set);
}

