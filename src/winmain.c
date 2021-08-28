// winmain.c (part of mintty)
// Copyright 2008-13 Andy Koppe, 2015-2020 Thomas Wolff
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#define dont_debuglog
#ifdef debuglog
FILE * mtlog = 0;
#endif

char * mintty_debug;

#define dont_debug_resize

#include "winpriv.h"
#include "winsearch.h"
#include "winimg.h"
#include "jumplist.h"
#include "wintab.h"

#include "term.h"
#include "appinfo.h"
#include "child.h"
#include "charset.h"
#include "tek.h"

#include <locale.h>
#include <getopt.h>
#if CYGWIN_VERSION_API_MINOR < 74
#define getopt_long_only getopt_long
typedef UINT_PTR uintptr_t;
#endif
#include <pwd.h>

#include <mmsystem.h>  // PlaySound for MSys
#include <shellapi.h>
#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM

#ifdef __CYGWIN__
#include <sys/cygwin.h>  // cygwin_internal
#endif

#if CYGWIN_VERSION_DLL_MAJOR >= 1007
#include <propsys.h>
#include <propkey.h>
#endif

#include <sys/stat.h>
#include <fcntl.h>  // open flags
#include <sys/utsname.h>

#ifndef INT16
#define INT16 short
#endif

#ifndef GWL_USERDATA
#define GWL_USERDATA -21
#endif


char * home;
char * cmd;
bool icon_is_from_shortcut = false;

HINSTANCE inst;
HWND wnd;
HIMC imc;
ATOM class_atom;

static char **main_argv;
static int main_argc;
static bool invoked_from_shortcut = false;
wstring shortcut = 0;
static bool invoked_with_appid = false;
static uint hotkey = 0;
static mod_keys hotkey_mods = 0;
static HHOOK kb_hook = 0;


//filled by win_adjust_borders:
static LONG window_style;
static int term_width, term_height;
static int width, height;
static int extra_width, extra_height, norm_extra_width, norm_extra_height;

int ini_width, ini_height;

// State
bool win_is_fullscreen;
static bool is_init = false;
bool win_is_always_on_top = false;
static bool go_fullscr_on_max;
static bool resizing;
static bool moving = false;
static bool wm_user = false;
static bool disable_poschange = true;
static bool poschanging = false;
static int zoom_token = 0;  // for heuristic handling of Shift zoom (#467, #476)
static bool default_size_token = false;
bool clipboard_token = false;
bool keep_screen_on = false;

// Options
bool title_settable = true;
static string border_style = 0;
static string report_geom = 0;
static bool report_moni = false;
bool report_child_pid = false;
static bool report_winpid = false;
static int monitor = 0;
static bool center = false;
static bool right = false;
static bool bottom = false;
static bool left = false;
static bool top = false;
static bool maxwidth = false;
static bool maxheight = false;
static bool store_taskbar_properties = false;
static bool prevent_pinning = false;
bool support_wsl = false;
wchar * wslname = 0;
wstring wsl_basepath = W("");
static uint wsl_ver = 0;
static char * wsl_guid = 0;
static bool wsl_launch = false;
static bool start_home = false;
#ifdef WSLTTY_APPX
static bool wsltty_appx = true;
#else
static bool wsltty_appx = false;
#endif


static HBITMAP caretbm;

#if WINVER < 0x600

typedef struct {
  int cxLeftWidth;
  int cxRightWidth;
  int cyTopHeight;
  int cyBottomHeight;
} MARGINS;

#else

#include <uxtheme.h>

#endif

#include <shlobj.h>


unsigned long
mtime(void)
{
#if CYGWIN_VERSION_API_MINOR >= 74
  struct timespec tim;
  clock_gettime(CLOCK_MONOTONIC, &tim);
  return tim.tv_sec * 1000 + tim.tv_nsec / 1000000;
#else
  return time(0);
#endif
}


#define dont_debug_dir

#ifdef debug_dir
#define trace_dir(d)	show_info(d)
#else
#define trace_dir(d)	
#endif


#ifdef debug_resize
#define SetWindowPos(wnd, after, x, y, cx, cy, flags)	printf("SWP[%s] %ld %ld\n", __FUNCTION__, (long int)cx, (long int)cy), Set##WindowPos(wnd, after, x, y, cx, cy, flags)
static void
trace_winsize(char * tag)
{
  RECT cr, wr;
  GetClientRect(wnd, &cr);
  GetWindowRect(wnd, &wr);
  printf("winsize[%s] @%d/%d %d %d cl %d %d + %d/%d\n", tag, (int)wr.left, (int)wr.top, (int)(wr.right - wr.left), (int)(wr.bottom - wr.top), (int)(cr.right - cr.left), (int)(cr.bottom - cr.top), extra_width, norm_extra_width);
}
#else
#define trace_winsize(tag)	
#endif


static HRESULT (WINAPI * pDwmIsCompositionEnabled)(BOOL *) = 0;
static HRESULT (WINAPI * pDwmExtendFrameIntoClientArea)(HWND, const MARGINS *) = 0;
static HRESULT (WINAPI * pDwmEnableBlurBehindWindow)(HWND, void *) = 0;
static HRESULT (WINAPI * pDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD) = 0;

static HRESULT (WINAPI * pSetWindowCompositionAttribute)(HWND, void *) = 0;
static BOOL (WINAPI * pSystemParametersInfo)(UINT, UINT, PVOID, UINT) = 0;

static BOOLEAN (WINAPI * pShouldAppsUseDarkMode)(void) = 0; /* undocumented */
static DWORD (WINAPI * pSetPreferredAppMode)(DWORD) = 0; /* undocumented */
static HRESULT (WINAPI * pSetWindowTheme)(HWND, const wchar_t *, const wchar_t *) = 0;

#define HTHEME HANDLE
static COLORREF (WINAPI * pGetThemeSysColor)(HTHEME hth, int colid) = 0;
static HTHEME (WINAPI * pOpenThemeData)(HWND, LPCWSTR pszClassList) = 0;
static HRESULT (WINAPI * pCloseThemeData)(HTHEME) = 0;

// Helper for loading a system library. Using LoadLibrary() directly is insecure
// because Windows might be searching the current working directory first.
static HMODULE
load_sys_library(string name)
{
  char path[MAX_PATH];
  uint len = GetSystemDirectoryA(path, MAX_PATH);
  if (len && len + strlen(name) + 1 < MAX_PATH) {
    path[len] = '\\';
    strcpy(&path[len + 1], name);
    return LoadLibraryA(path);
  }
  else
    return 0;
}

static void
load_dwm_funcs(void)
{
  HMODULE dwm = load_sys_library("dwmapi.dll");
  HMODULE user32 = load_sys_library("user32.dll");
  HMODULE uxtheme = load_sys_library("uxtheme.dll");

  if (dwm) {
    pDwmIsCompositionEnabled =
      (void *)GetProcAddress(dwm, "DwmIsCompositionEnabled");
    pDwmExtendFrameIntoClientArea =
      (void *)GetProcAddress(dwm, "DwmExtendFrameIntoClientArea");
    pDwmEnableBlurBehindWindow =
      (void *)GetProcAddress(dwm, "DwmEnableBlurBehindWindow");
    pDwmSetWindowAttribute = 
      (void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
  }
  if (user32) {
    pSetWindowCompositionAttribute =
      (void *)GetProcAddress(user32, "SetWindowCompositionAttribute");
    pSystemParametersInfo =
      (void *)GetProcAddress(user32, "SystemParametersInfoW");
  }
  if (uxtheme) {
    DWORD win_version = GetVersion();
    uint build = HIWORD(win_version);
    win_version = ((win_version & 0xff) << 8) | ((win_version >> 8) & 0xff);
    //printf("Windows %d.%d Build %d\n", win_version >> 8, win_version & 0xFF, build);
    if (win_version >= 0x0A00 && build >= 17763) { // minimum version 1809
      pShouldAppsUseDarkMode = 
        (void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(132)); /* ordinal */
      pSetPreferredAppMode = 
        (void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)); /* ordinal */
        // this would be AllowDarkModeForApp before Windows build 18362
    }
    pSetWindowTheme = 
      (void *)GetProcAddress(uxtheme, "SetWindowTheme");

    pOpenThemeData =
      (void *)GetProcAddress(uxtheme, "OpenThemeData");
    pCloseThemeData =
      (void *)GetProcAddress(uxtheme, "CloseThemeData");
    if (pOpenThemeData && pCloseThemeData)
      pGetThemeSysColor =
        (void *)GetProcAddress(uxtheme, "GetThemeSysColor");
  }
}

void *
load_library_func(string lib, string func)
{
  HMODULE hm = load_sys_library(lib);
  if (hm)
    return GetProcAddress(hm, func);
  return 0;
}


#define dont_debug_dpi

#define DPI_UNAWARE 0
#define DPI_AWAREV1 1
#define DPI_AWAREV2 2
int per_monitor_dpi_aware = DPI_UNAWARE;  // dpi_awareness
uint dpi = 96;
// DPI handling V2
static bool is_in_dpi_change = false;

const int Process_System_DPI_Aware = 1;
const int Process_Per_Monitor_DPI_Aware = 2;
static HRESULT (WINAPI * pGetProcessDpiAwareness)(HANDLE hprocess, int * value) = 0;
static HRESULT (WINAPI * pSetProcessDpiAwareness)(int value) = 0;
static HRESULT (WINAPI * pGetDpiForMonitor)(HMONITOR mon, int type, uint * x, uint * y) = 0;

//DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#ifndef _DPI_AWARENESS_CONTEXTS_
typedef HANDLE DPI_AWARENESS_CONTEXT;
#endif
#define DPI_AWARENESS_CONTEXT_UNAWARE           ((DPI_AWARENESS_CONTEXT)-1)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE      ((DPI_AWARENESS_CONTEXT)-2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
static DPI_AWARENESS_CONTEXT (WINAPI * pSetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT dpic) = 0;
static HRESULT (WINAPI * pEnableNonClientDpiScaling)(HWND win) = 0;
static BOOL (WINAPI * pAdjustWindowRectExForDpi)(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi) = 0;
static INT (WINAPI * pGetSystemMetricsForDpi)(INT index, UINT dpi) = 0;

static void
load_dpi_funcs(void)
{
  HMODULE shc = load_sys_library("shcore.dll");
  HMODULE user = load_sys_library("user32.dll");
#ifdef debug_dpi
  printf("load_dpi_funcs shcore %d user32 %d\n", !!shc, !!user);
#endif
  if (shc) {
    pGetProcessDpiAwareness =
      (void *)GetProcAddress(shc, "GetProcessDpiAwareness");
    pSetProcessDpiAwareness =
      (void *)GetProcAddress(shc, "SetProcessDpiAwareness");
    pGetDpiForMonitor =
      (void *)GetProcAddress(shc, "GetDpiForMonitor");
  }
  if (user) {
    pSetThreadDpiAwarenessContext =
      (void *)GetProcAddress(user, "SetThreadDpiAwarenessContext");
    pEnableNonClientDpiScaling =
      (void *)GetProcAddress(user, "EnableNonClientDpiScaling");
    pAdjustWindowRectExForDpi =
      (void *)GetProcAddress(user, "AdjustWindowRectExForDpi");
    pGetSystemMetricsForDpi =
      (void *)GetProcAddress(user, "GetSystemMetricsForDpi");
  }
#ifdef debug_dpi
  printf("SetProcessDpiAwareness %d GetProcessDpiAwareness %d GetDpiForMonitor %d SetThreadDpiAwarenessContext %d EnableNonClientDpiScaling %d AdjustWindowRectExForDpi %d GetSystemMetricsForDpi %d\n", !!pSetProcessDpiAwareness, !!pGetProcessDpiAwareness, !!pGetDpiForMonitor, !!pSetThreadDpiAwarenessContext, !!pEnableNonClientDpiScaling, !!pAdjustWindowRectExForDpi, !!pGetSystemMetricsForDpi);
#endif
}

void
set_dpi_auto_scaling(bool on)
{
  (void)on;
#if 0
 /* this was an attempt to get the Options menu to scale with DPI by
    disabling DPI awareness while constructing the menu in win_open_config;
    but then (if DPI zooming > 100% in Windows 10)
    any font change would resize the terminal by the zoom factor;
    also in a later Windows 10 update, it works without this
 */
#warning failed DPI tweak
  if (pSetThreadDpiAwarenessContext) {
    if (on)
      pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    else
      pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
  }
#endif
}

static int
set_per_monitor_dpi_aware(void)
{
  int res = DPI_UNAWARE;
  // DPI handling V2: make EnableNonClientDpiScaling work, at last
  if (pSetThreadDpiAwarenessContext && cfg.handle_dpichanged == 2 &&
      pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    res = DPI_AWAREV2;
  else if (cfg.handle_dpichanged == 1 &&
           pSetProcessDpiAwareness && pGetProcessDpiAwareness) {
    HRESULT hr = pSetProcessDpiAwareness(Process_Per_Monitor_DPI_Aware);
    // E_ACCESSDENIED:
    // The DPI awareness is already set, either by calling this API previously
    // or through the application (.exe) manifest.
    if (hr != E_ACCESSDENIED && !SUCCEEDED(hr))
      pSetProcessDpiAwareness(Process_System_DPI_Aware);

    int awareness = 0;
    if (SUCCEEDED(pGetProcessDpiAwareness(NULL, &awareness)) &&
        awareness == Process_Per_Monitor_DPI_Aware)
      res = DPI_AWAREV1;
  }
#ifdef debug_dpi
  printf("dpi_awareness %d\n", res);
#endif
  return res;
}

void
win_set_timer(void (*cb)(void), uint ticks)
{ SetTimer(wnd, (UINT_PTR)cb, ticks, null); }

void
win_keep_screen_on(bool on)
{
  keep_screen_on = on;
  if (on)
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED /*| ES_AWAYMODE_REQUIRED*/);
  else
    SetThreadExecutionState(ES_CONTINUOUS);
}


/*
  Session management: maintain list of window titles.
 */

#define dont_debug_tabbar

struct tabinfo * tabinfo = 0;
int ntabinfo = 0;

static HWND
get_prev_tab(bool all)
{
  HWND prev = 0;
  for (int w = 0; w < ntabinfo; w++)
    if (tabinfo[w].wnd != wnd) {
      if (all || !IsIconic(tabinfo[w].wnd))
        prev = tabinfo[w].wnd;
    }
    else if (prev)
      return prev;
  return prev;
}

static HWND
get_next_tab(bool all)
{
  HWND next = 0;
  for (int w = ntabinfo - 1; w >= 0; w--)
    if (tabinfo[w].wnd != wnd) {
      if (all || !IsIconic(tabinfo[w].wnd)) {
        next = tabinfo[w].wnd;
      }
    }
    else if (next)
      return next;
  return next;
}

static void
clear_tabinfo()
{
  for (int i = 0; i < ntabinfo; i++) {
    free(tabinfo[i].title);
  }
  if (tabinfo) {
    free(tabinfo);
    tabinfo = 0;
    ntabinfo = 0;
  }
}

static void
add_tabinfo(unsigned long tag, HWND wnd, wchar * title)
{
  struct tabinfo * newtabinfo = renewn(tabinfo, ntabinfo + 1);
  if (newtabinfo) {
    tabinfo = newtabinfo;
    tabinfo[ntabinfo].tag = tag;
    tabinfo[ntabinfo].wnd = wnd;
    tabinfo[ntabinfo].title = wcsdup(title);
    ntabinfo++;
  }
}

static void
sort_tabinfo()
{
  int comp_tabinfo(const void * t1, const void * t2)
  {
    if (((struct tabinfo *)t1)->tag < ((struct tabinfo *)t2)->tag)
      return -1;
    if (((struct tabinfo *)t1)->tag > ((struct tabinfo *)t2)->tag)
      return 1;
    else
      return 0;
  }
  qsort(tabinfo, ntabinfo, sizeof(struct tabinfo), comp_tabinfo);
}

static void
win_hide_other_tabs(HWND to_top)
{
  BOOL CALLBACK wnd_hide_tab(HWND curr_wnd, LPARAM lp)
  {
    HWND to_top = (HWND)lp;
    WINDOWINFO curr_wnd_info;
    curr_wnd_info.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(curr_wnd, &curr_wnd_info);
    if (class_atom == curr_wnd_info.atomWindowType) {
      //printf("[%p] hiding %p (unless top %p)\n", wnd, curr_wnd, to_top);
      if (curr_wnd != to_top && !IsIconic(curr_wnd)) {
        // do not use either of 
        // ShowWindow(SW_HIDE) or SetWindowPos(SWP_HIDEWINDOW);
        // it is hard to restore from those states and window handling 
        // interferes with them in a number of unpleasant ways;
        // esp. killing such a tab window will leave the others invisible

        // we could PostMessage(curr_wnd, WM_USER, 0, WIN_HIDE)
        // and handle that; currently synchronous handling seems sufficient

        // pseudo-hide background tabs by max transparency
        LONG style = GetWindowLong(curr_wnd, GWL_EXSTYLE);
        style |= WS_EX_LAYERED;
        SetWindowLong(curr_wnd, GWL_EXSTYLE, style);
        SetLayeredWindowAttributes(curr_wnd, 0, 0, LWA_ALPHA);
      }
    }
    return true;
  }

  EnumWindows(wnd_hide_tab, (LPARAM)to_top);
}

// support tabbar
int
sync_level(void)
{
  if (!cfg.window)
    return 0;  // avoid trouble if hidden
  return max(cfg.geom_sync, cfg.tabbar);
}

static bool
manage_tab_hiding(void)
{
  /* The mechanism of hiding non-foreground tabs in order to support 
     transparency for tabbed windows is disabled because it does not 
     work properly and causes inconsistent and buggy behaviour in various 
     window management situations, e.g. switching the tab on maximising 
     or even looping tab switching when restoring from fullscreen.
   */
  return false;
  //return sync_level() > 1;
}

/*
  Notify tab focus. Manage hidden tab status.
 */
static void
win_set_tab_focus(char tag)
{
  (void)tag;

  // guard by is_init to avoid hiding background tabs by early WM_ACTIVATE
  if (is_init && manage_tab_hiding()) {
    //printf("[%p] set_tab_focus %c focus %d\n", wnd, tag, GetFocus() == wnd);
    // don't need to unhide as we don't hide above
    //ShowWindow(wnd, SW_SHOW);  // in case it was a hidden tab

    // restore by clearing pseudo-hidden state
    win_update_transparency(cfg.transparency, cfg.opaque_when_focused);

    // hide background tabs
    if (cfg.window)  // not hidden explicitly
      win_hide_other_tabs(wnd);
  }
}

/*
  Enumerate all windows of the mintty class.
  ///TODO: Maintain a local list of them.
  To be used for tab bar display.
 */
static void
refresh_tab_titles(bool trace)
{
  BOOL CALLBACK wnd_enum_tabs(HWND curr_wnd, LPARAM lp)
  {
    bool trace = (bool)lp;

    WINDOWINFO curr_wnd_info;
    curr_wnd_info.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(curr_wnd, &curr_wnd_info);
    if (class_atom == curr_wnd_info.atomWindowType) {
      int len = GetWindowTextLengthW(curr_wnd);
      if (!len) {
        // check whether already terminating
        LONG fini = GetWindowLong(curr_wnd, GWL_USERDATA);
        if (fini) {
#ifdef debug_tabbar
          printf("[%8p] get tab %8p: fini\n", wnd, curr_wnd);
#endif
          return true;
        }
      }
      wchar title[len + 1];
      GetWindowTextW(curr_wnd, title, len + 1);
#ifdef debug_tabbar
      printf("[%8p] get tab %8p: <%ls>\n", wnd, curr_wnd, title);
#endif

      static bool sort_tabs_by_time = true;

      if (sort_tabs_by_time) {
        DWORD pid;
        GetWindowThreadProcessId(curr_wnd, &pid);
        HANDLE ph = OpenProcess(PROCESS_QUERY_INFORMATION, 0, pid);
        // PROCESS_QUERY_LIMITED_INFORMATION ?
        FILETIME cr_time, dummy;
        if (GetProcessTimes(ph, &cr_time, &dummy, &dummy, &dummy)) {
          unsigned long long crtime = ((unsigned long long)cr_time.dwHighDateTime << 32) | cr_time.dwLowDateTime;
          add_tabinfo(crtime, curr_wnd, title);
          if (trace) {
#ifdef debug_tabbar
            SYSTEMTIME start_time;
            if (FileTimeToSystemTime(&cr_time, &start_time))
              printf("  %04d-%02d-%02d_%02d:%02d:%02d.%03d\n",
                     start_time.wYear, start_time.wMonth, start_time.wDay,
                     start_time.wHour, start_time.wMinute, 
                     start_time.wSecond, start_time.wMilliseconds);
#endif
          }
        }
        CloseHandle(ph);
      }
      else
        add_tabinfo((unsigned long)curr_wnd, curr_wnd, title);

    }
    return true;
  }

  clear_tabinfo();
  EnumWindows(wnd_enum_tabs, (LPARAM)trace);
  sort_tabinfo();
#if defined(debug_tabbar) || defined(debug_win_switch)
  for (int w = 0; w < ntabinfo; w++)
    printf("[%d] %p eq %d iconic %d <%ls>\n", w, tabinfo[w].wnd, tabinfo[w].wnd == wnd, IsIconic(tabinfo[w].wnd), tabinfo[w].title);
#endif
}

/*
  Update list of windows in all windows of the mintty class.
 */
static void
update_tab_titles()
{
  BOOL CALLBACK wnd_enum_tabs(HWND curr_wnd, LPARAM lp)
  {
    (void)lp;
    WINDOWINFO curr_wnd_info;
    curr_wnd_info.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(curr_wnd, &curr_wnd_info);
    if (class_atom == curr_wnd_info.atomWindowType) {
      if (curr_wnd != wnd) {
        PostMessage(curr_wnd, WM_USER, 0, WIN_TITLE);
#ifdef debug_tabbar
        printf("[%8p] notified %8p to update tabbar\n", wnd, curr_wnd);
#endif
      }
    }
    return true;
  }
  if (sync_level() || win_tabbar_visible()) {
    // update my own list
    refresh_tab_titles(true);
    // support tabbar
    win_update_tabbar();
    // tell the others to update their's
    EnumWindows(wnd_enum_tabs, 0);
  }
}


/*
   Window system colour configuration.
   Applicable to current window if switched via WM_SETFOCUS/WM_KILLFOCUS.
   This is not enabled as it causes unpleasant flickering of the taskbar;
   also there is no visible effect on border or caption colours...
 */
static void
win_sys_style(bool focus)
{
#ifdef switch_sys_colours
  static INT elements[] = {
    COLOR_ACTIVEBORDER,
    COLOR_ACTIVECAPTION,
    COLOR_GRADIENTACTIVECAPTION,
    COLOR_CAPTIONTEXT  // proof of concept
  };
  static COLORREF colours[] = {
    RGB(0, 255, 0),
    RGB(0, 255, 0),
    RGB(0, 255, 0),
    RGB(255, 0, 0),
  };
  static COLORREF * save = 0;

  if (!save) {
    save = newn(COLORREF, lengthof(elements));
    for (uint i = 0; i < lengthof(elements); i++)
      save[i] = win_get_sys_colour(elements[i]);
  }
  if (focus)
    SetSysColors(lengthof(elements), elements, colours);
  else
    SetSysColors(lengthof(elements), elements, save);
#else
  (void)focus;
#endif
}

colour
win_get_sys_colour(int colid)
{
  if (pGetThemeSysColor) {
    HTHEME hth = pOpenThemeData(wnd, W("TAB;HEADER;WINDOW"));
    if (hth) {
      colour col = pGetThemeSysColor(hth, colid);
      //printf("colour id %d sys %06X theme %06X\n", colid, GetSysColor(colid), col);
      pCloseThemeData(hth);
      return col;
    }
  }

  return GetSysColor(colid);
}


/*
   Application scrollbar.
 */
static int scroll_len = 0;
static int scroll_dif = 0;

void
win_set_scrollview(int pos, int len, int height)
{
  bool prev = term.app_scrollbar;
  term.app_scrollbar = pos;

  if (term.app_scrollbar != prev)
    win_update_scrollbar(false);

  if (pos) {
    if (len)
      scroll_len = len;
    else
      len = scroll_len;
    if (height >= 0)
      scroll_dif = term.rows - height;
    else if (!prev)
      scroll_dif = 0;
    SetScrollInfo(
      wnd, SB_VERT,
      &(SCROLLINFO){
        .cbSize = sizeof(SCROLLINFO),
        .fMask = SIF_ALL | SIF_DISABLENOSCROLL,
        .nMin = 1,
        .nMax = len,
        .nPage = term.rows - scroll_dif,
        .nPos = pos,
      },
      true  // redraw
    );
  }
}


/*
   Window title functions.
 */

void
win_set_icon(char * s, int icon_index)
{
  HICON large_icon = 0, small_icon = 0;
  wstring icon_file = path_posix_to_win_w(s);
  //printf("win_set_icon <%ls>,%d\n", icon_file, icon_index);
  ExtractIconExW(icon_file, icon_index, &large_icon, &small_icon, 1);
  delete(icon_file);
  SetClassLongPtr(wnd, GCLP_HICONSM, (LONG_PTR)small_icon);
  SetClassLongPtr(wnd, GCLP_HICON, (LONG_PTR)large_icon);
  //SendMessage(wnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
  //SendMessage(wnd, WM_SETICON, ICON_BIG, (LPARAM)large_icon);
}

void
win_set_title(char *title)
{
  //printf("win_set_title settable %d <%s>\n", title_settable, title);
  if (title_settable) {
    wchar wtitle[strlen(title) + 1];
    if (cs_mbstowcs(wtitle, title, lengthof(wtitle)) >= 0) {
      // check current title to suppress unnecessary update_tab_titles()
      int len = GetWindowTextLengthW(wnd);
      wchar oldtitle[len + 1];
      GetWindowTextW(wnd, oldtitle, len + 1);
      if (0 != wcscmp(wtitle, oldtitle)) {
        SetWindowTextW(wnd, wtitle);
        usleep(1000);
        update_tab_titles();
      }
    }
  }
}

void
win_copy_title(void)
{
  int len = GetWindowTextLengthW(wnd);
  wchar title[len + 1];
  len = GetWindowTextW(wnd, title, len + 1);
  win_copy(title, 0, len + 1);
}

char *
win_get_title(void)
{
  int len = GetWindowTextLengthW(wnd);
  wchar title[len + 1];
  GetWindowTextW(wnd, title, len + 1);
  return cs__wcstombs(title);
}

void
win_copy_text(const char *s)
{
  unsigned int size;
  wchar *text = cs__mbstowcs(s);

  if (text == NULL) {
    return;
  }
  size = wcslen(text);
  if (size > 0) {
    win_copy(text, 0, size + 1);
  }
  free(text);
}

void
win_prefix_title(const wstring prefix)
{
  int len = GetWindowTextLengthW(wnd);
  int plen = wcslen(prefix);
  wchar ptitle[plen + len + 1];
  wcscpy(ptitle, prefix);
  wchar * title = & ptitle[plen];
  len = GetWindowTextW(wnd, title, len + 1);
  SetWindowTextW(wnd, ptitle);
  // "[Printing...] " or "TERMINATED"
  update_tab_titles();
}

void
win_unprefix_title(const wstring prefix)
{
  int len = GetWindowTextLengthW(wnd);
  wchar ptitle[len + 1];
  GetWindowTextW(wnd, ptitle, len + 1);
  int plen = wcslen(prefix);
  if (!wcsncmp(ptitle, prefix, plen)) {
    wchar * title = & ptitle[plen];
    SetWindowTextW(wnd, title);
    // "[Printing...] "
    update_tab_titles();
  }
}

/*
 * Title stack (implemented as fixed-size circular buffer)
 */
static wstring titles[16];
static uint titles_i;

void
win_save_title(void)
{
  int len = GetWindowTextLengthW(wnd);
  wchar *title = newn(wchar, len + 1);
  GetWindowTextW(wnd, title, len + 1);
  delete(titles[titles_i]);
  titles[titles_i++] = title;
  if (titles_i == lengthof(titles))
    titles_i = 0;
}

void
win_restore_title(void)
{
  if (!titles_i)
    titles_i = lengthof(titles);
  wstring title = titles[--titles_i];
  if (title) {
    SetWindowTextW(wnd, title);
    update_tab_titles();
    delete(title);
    titles[titles_i] = 0;
  }
}

/*
 *  Switch to next or previous application window in z-order
 */

void
win_post_sync_msg(HWND target, int level)
{
  if (sync_level()) {
    if (win_is_fullscreen)
      PostMessage(target, WM_USER, 0, WIN_FULLSCREEN);
    else if (IsZoomed(wnd))
      PostMessage(target, WM_USER, 0, WIN_MAXIMIZE);
    else if (level >= 3 && IsIconic(wnd))
      PostMessage(target, WM_USER, 0, WIN_MINIMIZE);
    else {
      RECT r;
      GetWindowRect(wnd, &r);
#ifdef debug_tabs
      printf("switcher %d,%d %d,%d\n", (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top));
#endif
      PostMessage(target, WM_USER,
                  MAKEWPARAM(r.right - r.left, r.bottom - r.top),
                  MAKELPARAM(r.left, r.top));
    }
  }
}

#ifdef use_init_position
static void
win_init_position()
{
  BOOL CALLBACK wnd_call_sync(HWND curr_wnd, LPARAM lp)
  {
    (void)lp;
    WINDOWINFO curr_wnd_info;
    curr_wnd_info.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(curr_wnd, &curr_wnd_info);
    if (class_atom == curr_wnd_info.atomWindowType) {
      if (curr_wnd != wnd && !IsIconic(curr_wnd)) {
        PostMessage(curr_wnd, WM_USER, 0, WIN_INIT_POS);
        return false;
      }
    }
    return true;
  }

  if (EnumWindows(wnd_call_sync, (LPARAM)4)) {
    // all callbacks succeeded
    win_synctabs(4);
  }
}
#endif

void
win_to_top(HWND top_wnd)
{
  // this would block if target window is blocked:
  // BringWindowToTop(top_wnd);

  // this does not work properly (see comments at when WM_USER:)
  // PostMessage(top_wnd, WM_USER, 0, WIN_TOP);

  // one of these works:
  int fgok = SetForegroundWindow(top_wnd);
  // SetActiveWindow(top_wnd);

  if (IsIconic(top_wnd))
    ShowWindow(top_wnd, SW_RESTORE);

  //printf("[%p] win_to_top %p ok %d\n", wnd, top_wnd, fgok);
  if (!fgok && cfg.tabbar) {
    // clicked on non-existent tab: clear vanished tab from tabbar
    win_bell(&cfg);
    update_tab_titles();
  }
}

#define dont_debug_sessions 1

#ifdef old_win_switch
static HWND first_wnd, last_wnd;
static HWND prev_wnd, next_wnd;
static bool wnd_passed;

static BOOL CALLBACK
wnd_enum_proc(HWND curr_wnd, LPARAM unused(lp))
{
#ifdef debug_sessions
  WINDOWINFO curr_wnd_info;
  curr_wnd_info.cbSize = sizeof(WINDOWINFO);
  GetWindowInfo(curr_wnd, &curr_wnd_info);
  if (class_atom == curr_wnd_info.atomWindowType) {
    int len = GetWindowTextLengthW(curr_wnd);
    wchar title[len + 1];
    GetWindowTextW(curr_wnd, title, len + 1);
    printf("[%8p.%d]%1s %2s %8p %ls\n", wnd, (int)unused_lp,
           curr_wnd == wnd ? "=" : IsIconic(curr_wnd) ? "i" : "",
           !first_wnd && curr_wnd != wnd && !IsIconic(curr_wnd) ? "->" : "",
           curr_wnd, title);
  }
#endif
  if (curr_wnd == wnd)
    wnd_passed = true;
  else if (!IsIconic(curr_wnd)) {
    WINDOWINFO curr_wnd_info;
    curr_wnd_info.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(curr_wnd, &curr_wnd_info);
    if (class_atom == curr_wnd_info.atomWindowType) {
      first_wnd = first_wnd ?: curr_wnd;
      last_wnd = curr_wnd;
      if (!wnd_passed)
        prev_wnd = curr_wnd;
      else
        if (!next_wnd)
          next_wnd = curr_wnd;
    }
  }
  return true;
}
#endif

/*
   Cycle mintty windows. Skip iconized windows, unless second parameter true.
 */
void
win_switch(bool back, bool alternate)
{
  //printf("[%p] win_switch %d %d\n", wnd, back, alternate);

  // avoid being pushed behind other windows (#652)
  // but do it below, not here (wsltty#47)
  //SetWindowPos(wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

#ifdef old_win_switch
  (void)get_next_tab; (void)get_prev_tab;

#if defined(debug_sessions) && debug_sessions > 1
  first_wnd = 0, last_wnd = 0, prev_wnd = 0, next_wnd = 0, wnd_passed = false;
  EnumChildWindows(0, wnd_enum_proc, 1);
  first_wnd = 0, last_wnd = 0, prev_wnd = 0, next_wnd = 0, wnd_passed = false;
  EnumDesktopWindows(0, wnd_enum_proc, 8);
#endif

  first_wnd = 0, last_wnd = 0, prev_wnd = 0, next_wnd = 0, wnd_passed = false;
  EnumWindows(wnd_enum_proc, 0);
  if (!prev_wnd)
    prev_wnd = last_wnd;
  if (!next_wnd)
    next_wnd = first_wnd;

  if (first_wnd) {
    if (back)
      first_wnd = prev_wnd;
    else if (true)
      first_wnd = next_wnd;
    else if (back)
      first_wnd = last_wnd;
    else {
      // avoid being pushed behind other windows (#652)
      SetWindowPos(wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
      SetWindowPos(wnd, last_wnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE
                       | (alternate ? SWP_NOZORDER : SWP_NOREPOSITION));
    }
    win_to_top(first_wnd);
  }
#else
  refresh_tab_titles(false);
  win_to_top(back ? get_prev_tab(alternate) : get_next_tab(alternate));
  // support tabbar
  if (sync_level())
    win_post_sync_msg(back ? get_prev_tab(alternate) : get_next_tab(alternate), sync_level());
  win_update_tabbar();
#endif
}


/*
 *  Virtual Tabs
 */

#define dont_debug_tabs

static uint tabn = 0;
static HWND * tabs = 0;

void
clear_tabs()
{
  if (tabn)
    delete(tabs);
  tabn = 0;
  tabs = 0;
}

void
add_tab(uint tabi, HWND wndi)
{
  if (tabi == tabn) {
    tabn++;
    tabs = renewn(tabs, tabn);
    tabs[tabi] = wndi;
  }
}

static HWND
get_tab(uint tabi)
{
  if (tabi < tabn)
    return tabs[tabi];
  else
    return 0;
}


static void
win_gotab(uint n)
{
  HWND tab = get_tab(n);
  //printf("[%p] win_gotab %d %p\n", wnd, n, tab);

  // apparently, we don't have to fiddle with SetWindowPos as in win_switch

  win_to_top(tab);

  // reposition / resize
  if (sync_level()) {
    win_post_sync_msg(tab, 0);  // 0: don't minimize
  }

#ifdef hide_myself
#warning hiding and unhiding tabs is implemented elsewhere
  if (tab == wnd)
    // avoid hiding when switching to myself
    return;
#endif
}

static void
win_synctabs(int level)
{
  BOOL CALLBACK wnd_enum_tabs(HWND curr_wnd, LPARAM lp)
  {
    int level = (int)lp;

    WINDOWINFO curr_wnd_info;
    curr_wnd_info.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(curr_wnd, &curr_wnd_info);
    if (class_atom == curr_wnd_info.atomWindowType) {
      if (curr_wnd != wnd) {
        win_post_sync_msg(curr_wnd, level);
      }
    }
    return true;
  }

#ifdef debug_tabs
  printf("[%8p] win_synctabs\n", wnd);
#endif
  if (wm_user)
    return;
  if (sync_level() >= level)
    EnumWindows(wnd_enum_tabs, (LPARAM)level);
#ifdef debug_tabs
  printf("[%8p] win_synctabs end\n", wnd);
#endif
}


/*
 *  Monitor-related window functions
 */

static void
win_launch(int n)
{
  HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
  int x, y;
  int moni = search_monitors(&x, &y, mon, true, 0);
  child_launch(n, main_argc, main_argv, moni);
}


static void
get_my_monitor_info(MONITORINFO *mip)
{
  HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
  mip->cbSize = sizeof(MONITORINFO);
  GetMonitorInfo(mon, mip);
}


static void
get_monitor_info(int moni, MONITORINFO *mip)
{
  mip->cbSize = sizeof(MONITORINFO);

  struct data_get_monitor_info {
    int moni;
    MONITORINFO *mip;
  };

  BOOL CALLBACK
  monitor_enum(HMONITOR hMonitor, HDC hdcMonitor, LPRECT monp, LPARAM dwData)
  {
    (void)hdcMonitor, (void)monp;
    struct data_get_monitor_info * pdata = (struct data_get_monitor_info *)dwData;

    GetMonitorInfo(hMonitor, pdata->mip);

    return --(pdata->moni) > 0;
  }

  struct data_get_monitor_info data = {
    .moni = moni,
    .mip = mip
  };
  EnumDisplayMonitors(0, 0, monitor_enum, (LPARAM)&data);
}

#define dont_debug_display_monitors_mockup
#define dont_debug_display_monitors

#ifdef debug_display_monitors_mockup
# define debug_display_monitors
static const RECT monitors[] = {
  //(RECT){.left = 0, .top = 0, .right = 1920, .bottom = 1200},
    //    44
    // 3  11  2
    //     5   6
  {0, 0, 1920, 1200},
  {1920, 0, 3000, 1080},
  {-800, 200, 0, 600},
  {0, -1080, 1920, 0},
  {1300, 1200, 2100, 1800},
  {2100, 1320, 2740, 1800},
};
static long primary_monitor = 2 - 1;
static long current_monitor = 1 - 1;  // assumption for MonitorFromWindow
#endif

/*
   search_monitors(&x, &y, 0, false, &moninfo)
     returns number of monitors;
       stores smallest width/height of all monitors
       stores info of current monitor
   search_monitors(&x, &y, 0, true, &moninfo)
     returns number of monitors;
       stores smallest width/height of all monitors
       stores info of primary monitor
   search_monitors(&x, &y, mon, false/true, 0)
     returns index of given monitor (0/primary if not found)
   search_monitors(&x, &y, 0, false, 0)
     returns number of monitors;
       stores virtual screen size
   search_monitors(&x, &y, 0, 2, &moninfo)
     returns number of monitors;
       stores virtual screen top left corner
       stores virtual screen size
   search_monitors(&x, &y, 0, true, 0)
     prints information about all monitors
 */
int
search_monitors(int * minx, int * miny, HMONITOR lookup_mon, int get_primary, MONITORINFO *mip)
{
#ifdef debug_display_monitors_mockup
  BOOL
  EnumDisplayMonitors(HDC hdc, LPCRECT lprcClip, MONITORENUMPROC lpfnEnum, LPARAM dwData)
  {
    (void)lprcClip;
    for (unsigned long moni = 0; moni < lengthof(monitors); moni++) {
      RECT monrect = monitors[moni];
      HMONITOR hMonitor = (HMONITOR)(moni + 1);
      HDC hdcMonitor = hdc;
      //if (hdc) hdcMonitor = (HDC)...;
      //if (hdc) monrect = intersect(hdc.rect, monrect);
      //if (hdc) hdcMonitor.rect = intersection(hdc.rect, lprcClip, monrect);
      if (lpfnEnum(hMonitor, hdcMonitor, &monrect, dwData) == FALSE)
        return TRUE;
    }
    return TRUE;
  }

  BOOL GetMonitorInfo(HMONITOR hMonitor, LPMONITORINFO lpmi)
  {
    long moni = (long)hMonitor - 1;
    lpmi->rcMonitor = monitors[moni];
    lpmi->rcWork = monitors[moni];
    lpmi->dwFlags = 0;
    if (moni == primary_monitor)
      lpmi->dwFlags = MONITORINFOF_PRIMARY;
    return TRUE;
  }

  HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags)
  {
    (void)hwnd, (void)dwFlags;
    return (HMONITOR)current_monitor + 1;
  }
#endif

  struct data_search_monitors {
    HMONITOR lookup_mon;
    int moni;
    int moni_found;
    int *minx, *miny;
    RECT vscr;
    HMONITOR refmon, curmon;
    int get_primary;
    bool print_monitors;
  };

  struct data_search_monitors data = {
    .moni = 0,
    .moni_found = 0,
    .minx = minx,
    .miny = miny,
    .vscr = (RECT){0, 0, 0, 0},
    .refmon = 0,
    .curmon = lookup_mon ? 0 : MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST),
    .get_primary = get_primary,
    .print_monitors = !lookup_mon && !mip && get_primary
  };

  * minx = 0;
  * miny = 0;
#ifdef debug_display_monitors
  data.print_monitors = !lookup_mon;
#endif

  BOOL CALLBACK
  monitor_enum(HMONITOR hMonitor, HDC hdcMonitor, LPRECT monp, LPARAM dwData)
  {
    struct data_search_monitors *data = (struct data_search_monitors *)dwData;
    (void)hdcMonitor, (void)monp;

    (data->moni) ++;
    if (hMonitor == data->lookup_mon) {
      // looking for index of specific monitor
      data->moni_found = data->moni;
      return FALSE;
    }

    MONITORINFO mi;
    mi.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(hMonitor, &mi);

    if (data->get_primary && (mi.dwFlags & MONITORINFOF_PRIMARY)) {
      data->moni_found = data->moni;  // fallback to be overridden by monitor found later
      data->refmon = hMonitor;
    }

    // determining smallest monitor width and height
    RECT fr = mi.rcMonitor;
    if (*(data->minx) == 0 || *(data->minx) > fr.right - fr.left)
      *(data->minx) = fr.right - fr.left;
    if (*(data->miny) == 0 || *(data->miny) > fr.bottom - fr.top)
      *(data->miny) = fr.bottom - fr.top;
    data->vscr.top = min(data->vscr.top, fr.top);
    data->vscr.left = min(data->vscr.left, fr.left);
    data->vscr.right = max(data->vscr.right, fr.right);
    data->vscr.bottom = max(data->vscr.bottom, fr.bottom);

    if (data->print_monitors) {
      uint x, dpi = 0;
      if (pGetDpiForMonitor)
        pGetDpiForMonitor(hMonitor, 0, &x, &dpi);  // MDT_EFFECTIVE_DPI
      printf("Monitor %d %s %s (%3d dpi) w,h %4d,%4d (%4d,%4d...%4d,%4d)\n", 
             data->moni,
             hMonitor == data->curmon ? "current" : "       ",
             mi.dwFlags & MONITORINFOF_PRIMARY ? "primary" : "       ",
             dpi,
             (int)(fr.right - fr.left), (int)(fr.bottom - fr.top),
             (int)fr.left, (int)fr.top, (int)fr.right, (int)fr.bottom);
    }

    return TRUE;
  }

  EnumDisplayMonitors(0, 0, monitor_enum, (LPARAM)&data);

  if (!lookup_mon && !mip && !get_primary) {
    *minx = data.vscr.right - data.vscr.left;
    *miny = data.vscr.bottom - data.vscr.top;
    return data.moni;
  }
  else if (lookup_mon) {
    return data.moni_found;
  }
  else if (mip) {
    if (!data.refmon)  // not detected primary monitor as requested?
      // determine current monitor
      data.refmon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    mip->cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(data.refmon, mip);
    if (get_primary == 2) {
      *minx = data.vscr.left;
      *miny = data.vscr.top;
    }
    return data.moni;  // number of monitors
  }
  else
    return data.moni;  // number of monitors printed
}


/*
   Window manipulation functions.
 */

/*
 * Minimise or restore the window in response to a server-side request.
 */
void
win_set_iconic(bool iconic)
{
  if (iconic ^ IsIconic(wnd))
    ShowWindow(wnd, iconic ? SW_MINIMIZE : SW_RESTORE);
}

/*
 * Move the window in response to a server-side request.
 */
void
win_set_pos(int x, int y)
{
  trace_resize(("--- win_set_pos %d %d\n", x, y));
  if (!IsZoomed(wnd))
    SetWindowPos(wnd, null, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void
win_set_zorder(bool top)
{
  // ensure window to pop up:
  SetWindowPos(wnd, top ? HWND_TOPMOST : HWND_BOTTOM, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE);
  // but do not stick it to the top:
  SetWindowPos(wnd, top ? HWND_NOTOPMOST : HWND_BOTTOM, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE);
}

void
win_toggle_on_top(void)
{
  win_is_always_on_top = !win_is_always_on_top;
  SetWindowPos(wnd, win_is_always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
               0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

bool
win_is_iconic(void)
{
  return IsIconic(wnd);
}

static void
win_get_pos(int *xp, int *yp)
{
  RECT r;
  GetWindowRect(wnd, &r);
  *xp = r.left;
  *yp = r.top;
}

void
win_get_scrpos(int *xp, int *yp, bool with_borders)
{
  RECT r;
  GetWindowRect(wnd, &r);
  *xp = r.left;
  *yp = r.top;
  MONITORINFO mi;
  int vx, vy;
  search_monitors(&vx, &vy, 0, 2, &mi);
  RECT fr = mi.rcMonitor;
  *xp += fr.left - vx;
  *yp += fr.top - vy;
  if (with_borders) {
    *xp += GetSystemMetrics(SM_CXSIZEFRAME) + PADDING;
    *yp += GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CYCAPTION) + OFFSET + PADDING;
  }
}

static int
win_has_scrollbar(void)
{
  LONG style = GetWindowLong(wnd, GWL_STYLE);
  if (style & WS_VSCROLL) {
    LONG exstyle = GetWindowLong(wnd, GWL_EXSTYLE);
    if (exstyle & WS_EX_LEFTSCROLLBAR)
      return -1;
    else
      return 1;
  }
  else
    return 0;
}

void
win_get_pixels(int *height_p, int *width_p, bool with_borders)
{
  trace_winsize("win_get_pixels");
  RECT r;
  //printf("win_get_pixels: width %d win_has_scrollbar %d\n", r.right - r.left, win_has_scrollbar());
  if (with_borders) {
    GetWindowRect(wnd, &r);
    *height_p = r.bottom - r.top;
    *width_p = r.right - r.left;
  }
  else {
    GetClientRect(wnd, &r);
    int sy = win_search_visible() ? SEARCHBAR_HEIGHT : 0;
    *height_p = r.bottom - r.top - 2 * PADDING - OFFSET - sy
              //- extra_height
              ;
    *width_p = r.right - r.left - 2 * PADDING
             //- extra_width
             //- (cfg.scrollbar ? GetSystemMetrics(SM_CXVSCROLL) : 0)
             //- (win_has_scrollbar() ? GetSystemMetrics(SM_CXVSCROLL) : 0)
             ;
  }
}

void
term_save_image(void)
{
  struct timeval now;
  gettimeofday(& now, 0);
  char * copf = save_filename(".png");
  wchar * copyfn = path_posix_to_win_w(copf);
  free(copf);

  if (tek_mode)
    tek_copy(copyfn);  // stored; free'd later
  else {
    HDC dc = GetDC(wnd);
    int height, width;
    win_get_pixels(&height, &width, false);
    save_img(dc, 0, OFFSET, 
                 width + 2 * PADDING, height + 2 * PADDING, copyfn);
    free(copyfn);
    ReleaseDC(wnd, dc);
  }
}

void
win_get_screen_chars(int *rows_p, int *cols_p)
{
  MONITORINFO mi;
  get_my_monitor_info(&mi);
  RECT fr = mi.rcMonitor;
  *rows_p = (fr.bottom - fr.top - 2 * PADDING - OFFSET) / cell_height;
  *cols_p = (fr.right - fr.left - 2 * PADDING) / cell_width;
}

void
win_set_pixels(int height, int width)
{
  trace_resize(("--- win_set_pixels %d %d\n", height, width));
  // avoid resizing if no geometry yet available (#649?)
  if (!height || !width)  // early invocation
    return;

  int sy = win_search_visible() ? SEARCHBAR_HEIGHT : 0;
  // set window size
  SetWindowPos(wnd, null, 0, 0,
               width + extra_width + 2 * PADDING,
               height + extra_height + OFFSET + 2 * PADDING + sy,
               SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
}

bool
win_is_glass_available(void)
{
  BOOL result = false;
#ifdef support_glass
#warning #501: “Just give up on glass effects. Microsoft clearly have.”
  if (pDwmIsCompositionEnabled)
    pDwmIsCompositionEnabled(&result);
#endif
  return result;
}

static void
win_update_blur(bool opaque)
{
// This feature is disabled in config.c as it does not seem to work,
// see https://github.com/mintty/mintty/issues/501
  if (pDwmEnableBlurBehindWindow) {
    bool blur =
      cfg.transparency && cfg.blurred && !win_is_fullscreen &&
      !(opaque && term.has_focus);
#define dont_use_dwmapi_h
#ifdef use_dwmapi_h
#warning dwmapi_include_shown_for_documentation
#include <dwmapi.h>
    DWM_BLURBEHIND bb;
#else
    struct {
      DWORD dwFlags;
      BOOL  fEnable;
      HRGN  hRgnBlur;
      BOOL  fTransitionOnMaximized;
    } bb;
#define DWM_BB_ENABLE 1
#endif
    bb.dwFlags = DWM_BB_ENABLE;
    bb.fEnable = blur;
    bb.hRgnBlur = NULL;
    bb.fTransitionOnMaximized = FALSE;

    pDwmEnableBlurBehindWindow(wnd, &bb);
  }
}

static void
win_update_glass(bool opaque)
{
  bool glass = !(opaque && term.has_focus)
               //&& !win_is_fullscreen
               && cfg.transparency == TR_GLASS
               //&& cfg.glass // decouple glass mode from transparency setting
               ;

  if (pDwmExtendFrameIntoClientArea) {
    pDwmExtendFrameIntoClientArea(wnd, &(MARGINS){glass ? -1 : 0, 0, 0, 0});
  }

  if (pSetWindowCompositionAttribute) {
    enum AccentState
    {
      ACCENT_DISABLED = 0,
      ACCENT_ENABLE_GRADIENT = 1,
      ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
      ACCENT_ENABLE_BLURBEHIND = 3,
      ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
      ACCENT_ENABLE_HOSTBACKDROP = 5,
      ACCENT_INVALID_STATE = 6
    };
    enum WindowCompositionAttribute
    {
      WCA_ACCENT_POLICY = 19,
      WCA_USEDARKMODECOLORS = 26, // does not yield the desired effect (#1005)
    };
    struct ACCENTPOLICY
    {
      //enum AccentState nAccentState;
      int nAccentState;
      int nFlags;
      int nColor;
      int nAnimationId;
    };
    struct WINCOMPATTRDATA
    {
      //enum WindowCompositionAttribute attribute;
      DWORD attribute;
      PVOID pData;
      ULONG dataSize;
    };
    struct ACCENTPOLICY policy = {
      glass ? ACCENT_ENABLE_BLURBEHIND : ACCENT_DISABLED,
      0,
      0,
      0
    };
    struct WINCOMPATTRDATA data = {
      WCA_ACCENT_POLICY,
      (PVOID)&policy,
      sizeof(policy)
    };

    //printf("SetWindowCompositionAttribute %d\n", policy.nAccentState);
    pSetWindowCompositionAttribute(wnd, &data);
  }
}

void
win_dark_mode(HWND w)
{
  if (pShouldAppsUseDarkMode) {
    HIGHCONTRASTW hc;
    hc.cbSize = sizeof hc;
    pSystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0);
    //printf("High Contrast scheme <%ls>\n", hc.lpszDefaultScheme);

    if (!(hc.dwFlags & HCF_HIGHCONTRASTON) && pShouldAppsUseDarkMode()) {
      pSetWindowTheme(w, W("DarkMode_Explorer"), NULL);

      // set DWMWA_USE_IMMERSIVE_DARK_MODE; needed for titlebar
      BOOL dark = 1;
      if (S_OK != pDwmSetWindowAttribute(w, 20, &dark, sizeof dark)) {
        // this would be the call before Windows build 18362
        pDwmSetWindowAttribute(w, 19, &dark, sizeof dark);
      }
    }
  }
}

/*
 * Go full-screen. This should only be called when we are already maximised.
 */
static void
make_fullscreen(void)
{
  win_is_fullscreen = true;

 /* Remove the window furniture. */
  LONG style = GetWindowLong(wnd, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
  SetWindowLong(wnd, GWL_STYLE, style);

 /* The glass effect doesn't work for fullscreen windows */
  win_update_glass(cfg.opaque_when_focused);

 /* Resize ourselves to exactly cover the nearest monitor. */
  MONITORINFO mi;
  get_my_monitor_info(&mi);
  RECT fr = mi.rcMonitor;
  // set window size
  SetWindowPos(wnd, HWND_TOP, fr.left, fr.top,
               fr.right - fr.left, fr.bottom - fr.top, SWP_FRAMECHANGED);
}

/*
 * Clear the full-screen attributes.
 */
static void
clear_fullscreen(void)
{
  win_is_fullscreen = false;
  win_update_glass(cfg.opaque_when_focused);

 /* Reinstate the window furniture. */
  LONG style = GetWindowLong(wnd, GWL_STYLE);
  if (border_style) {
    if (strcmp(border_style, "void") != 0) {
      style |= WS_THICKFRAME;
    }
  }
  else {
    style |= WS_CAPTION | WS_BORDER | WS_THICKFRAME;
  }
  SetWindowLong(wnd, GWL_STYLE, style);
  SetWindowPos(wnd, null, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void
win_set_geom(int y, int x, int height, int width)
{
  trace_resize(("--- win_set_geom %d %d %d %d\n", y, x, height, width));

  if (win_is_fullscreen)
    clear_fullscreen();

  MONITORINFO mi;
  get_my_monitor_info(&mi);
  RECT ar = mi.rcWork;
  int scr_height = ar.bottom - ar.top, scr_width = ar.right - ar.left;

  RECT r;
  GetWindowRect(wnd, &r);
  int term_height = r.bottom - r.top, term_width = r.right - r.left;

  int term_x, term_y;
  win_get_pos(&term_x, &term_y);

  if (x >= 0)
    term_x = x;
  if (y >= 0)
    term_y = y;
  if (width == 0)
    term_width = scr_width;
  else if (width > 0)
    term_width = width;
  if (height == 0)
    term_height = scr_height;
  else if (height > 0)
    term_height = height;

  // set window size
  SetWindowPos(wnd, null, term_x, term_y,
               term_width, term_height,
               SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOZORDER);
}

static void
win_fix_position(void)
{
  // DPI handling V2
  if (is_in_dpi_change)
    // window position needs no correction during DPI change, 
    // avoid position flickering (#695)
    return;

  RECT wr;
  GetWindowRect(wnd, &wr);
  MONITORINFO mi;
  get_my_monitor_info(&mi);
  RECT ar = mi.rcWork;

  // Correct edges. Top and left win if the window is too big.
  wr.top -= max(0, wr.bottom - ar.bottom);
  wr.top = max(wr.top, ar.top);
  wr.left -= max(0, wr.right - ar.right);
  wr.left = max(wr.left, ar.left);
#ifdef workaround_629
  // attempt to workaround left gap (#629); does not seem to work anymore
  WINDOWINFO winfo;
  winfo.cbSize = sizeof(WINDOWINFO);
  GetWindowInfo(wnd, &winfo);
  wr.left = max(wr.left, (int)(ar.left - winfo.cxWindowBorders));
#endif

  SetWindowPos(wnd, 0, wr.left, wr.top, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void
win_set_chars(int rows, int cols)
{
  trace_resize(("--- win_set_chars %d×%d\n", rows, cols));

  if (win_is_fullscreen)
    clear_fullscreen();

  // prevent resizing to same logical size
  // which would remove bottom padding and spoil some Windows magic (#629)
  if (rows != term.rows || cols != term.cols) {
    win_set_pixels(rows * cell_height, cols * cell_width);
    win_fix_position();
  }
  trace_winsize("win_set_chars > win_fix_position");
}


void
taskbar_progress(int i)
{
#if CYGWIN_VERSION_API_MINOR >= 74
static int last_i = 0;
  if (i == last_i)
    return;
  //printf("taskbar_progress %d detect %d\n", i, term.detect_progress);

  ITaskbarList3 * tbl;
  HRESULT hres = CoCreateInstance(&CLSID_TaskbarList, NULL,
                                  CLSCTX_INPROC_SERVER,
                                  &IID_ITaskbarList3, (void **) &tbl);
  if (!SUCCEEDED(hres))
    return;

  hres = tbl->lpVtbl->HrInit(tbl);
  if (!SUCCEEDED(hres)) {
    tbl->lpVtbl->Release(tbl);
    return;
  }

  if (i >= 0)
    hres = tbl->lpVtbl->SetProgressValue(tbl, wnd, i, 100);
  else if (i == -1)
    hres = tbl->lpVtbl->SetProgressState(tbl, wnd, TBPF_NORMAL);
  else if (i == -2)
    hres = tbl->lpVtbl->SetProgressState(tbl, wnd, TBPF_PAUSED);
  else if (i == -3)
    hres = tbl->lpVtbl->SetProgressState(tbl, wnd, TBPF_ERROR);
  else if (i == -8)
    hres = tbl->lpVtbl->SetProgressState(tbl, wnd, TBPF_INDETERMINATE);
  else if (i == -9)
    hres = tbl->lpVtbl->SetProgressState(tbl, wnd, TBPF_NOPROGRESS);

  last_i = i;

  tbl->lpVtbl->Release(tbl);
#else
  (void)i;
#endif
}


// Clockwork
int get_tick_count(void) { return GetTickCount(); }
int cursor_blink_ticks(void) { return GetCaretBlinkTime(); }

static void
flash_taskbar(bool enable)
{
  static bool enabled;
  if (enable != enabled) {
    FlashWindowEx(&(FLASHWINFO){
      .cbSize = sizeof(FLASHWINFO),
      .hwnd = wnd,
      .dwFlags = enable ? FLASHW_TRAY | FLASHW_TIMER : FLASHW_STOP,
      .uCount = 1,
      .dwTimeout = 0
    });
    enabled = enable;
  }
}

static void
flash_border()
{
  //FlashWindow(wnd, 1);
  FlashWindowEx(&(FLASHWINFO){
    .cbSize = sizeof(FLASHWINFO),
    .hwnd = wnd,
    .dwFlags = FLASHW_CAPTION,
    .uCount = 1,
    .dwTimeout = 0
  });
}

/*
 * Bell.
 */
static void
do_win_bell(config * conf, bool margin_bell)
{
  term_bell * bellstate = margin_bell ? &term.marginbell : &term.bell;
  unsigned long now = mtime();

  if (conf->bell_type &&
      (now - bellstate->last_bell >= (unsigned long)conf->bell_interval
       || bellstate->vol != bellstate->last_vol
      )
     )
  {
    do_update();

    bellstate->last_bell = now;
    bellstate->last_vol = bellstate->vol;

    wchar * bell_name = 0;
    void set_bells(char * belli)
    {
      while (*belli) {
        int i = (*belli & 0x0F) - 2;
        if (i >= 0 && i < (int)lengthof(conf->bell_file))
          bell_name = (wchar *)conf->bell_file[i];
        if (bell_name && *bell_name) {
          return;
        }
        belli++;
      }
    }
    switch (bellstate->vol) {
      // no bell volume: 0 1
      // low bell volume: 2 3 4
      // high bell volume: 5 6 7 8
      when 8: set_bells("8765432");
      when 7: set_bells("7658432");
      when 6: set_bells("6758432");
      when 5: set_bells("5678432");
      when 4: set_bells("4325678");
      when 3: set_bells("3425678");
      when 2: set_bells("2345678");
    }

    bool free_bell_name = false;
    if (bell_name && *bell_name) {
      if (wcschr(bell_name, L'/') || wcschr(bell_name, L'\\')) {
        if (bell_name[1] != ':') {
          char * bf = path_win_w_to_posix(bell_name);
          bell_name = path_posix_to_win_w(bf);
          free(bf);
          free_bell_name = true;
        }
      }
      else {
        wchar * bell_file = bell_name;
        char * bf;
        if (!wcschr(bell_name, '.')) {
          int len = wcslen(bell_name);
          bell_file = newn(wchar, len + 5);
          wcscpy(bell_file, bell_name);
          wcscpy(&bell_file[len], W(".wav"));
          bf = get_resource_file(W("sounds"), bell_file, false);
          free(bell_file);
        }
        else
          bf = get_resource_file(W("sounds"), bell_name, false);
        if (bf) {
          bell_name = path_posix_to_win_w(bf);
          free(bf);
          free_bell_name = true;
        }
        else
          bell_name = null;
      }
    }

    if (bell_name && *bell_name && PlaySoundW(bell_name, NULL, SND_ASYNC | SND_FILENAME)) {
      // played
    }
    else if (bellstate->vol <= 1) {
      // muted
    }
    else if (conf->bell_freq)
      Beep(conf->bell_freq, conf->bell_len);
    else if (conf->bell_type > 0) {
      //  1 -> 0x00000000 MB_OK              Default Beep
      //  2 -> 0x00000010 MB_ICONSTOP        Critical Stop
      //  3 -> 0x00000020 MB_ICONQUESTION    Question
      //  4 -> 0x00000030 MB_ICONEXCLAMATION Exclamation
      //  5 -> 0x00000040 MB_ICONASTERISK    Asterisk
      MessageBeep((conf->bell_type - 1) * 16);
    } else if (conf->bell_type < 0)
      // -1 -> 0xFFFFFFFF                    Simple Beep
      MessageBeep(0xFFFFFFFF);

    if (free_bell_name)
      free(bell_name);
  }

  if (cfg.bell_flash_style & FLASH_FRAME)
    flash_border();
  if (term.bell_taskbar && (!term.has_focus || win_is_iconic()))
    flash_taskbar(true);
  if (term.bell_popup)
    win_set_zorder(true);
}

void
win_bell(config * conf)
{
  do_win_bell(conf, false);
}

void
win_margin_bell(config * conf)
{
  do_win_bell(conf, true);
}


void
win_invalidate_all(bool clearbg)
{
  InvalidateRect(wnd, null, true);
  win_flush_background(clearbg);
}


#ifdef debug_dpi
static void
print_system_metrics(int dpi, string tag)
{
# ifndef SM_CXPADDEDBORDER
# define SM_CXPADDEDBORDER 92
# endif
  printf("metrics /%d [%s]\n"
         "        border %d/%d %d/%d edge %d/%d %d/%d\n"
         "        frame  %d/%d %d/%d size %d/%d %d/%d\n"
         "        padded %d/%d\n"
         "        caption %d/%d\n"
         "        scrollbar %d/%d\n",
         dpi, tag,
         GetSystemMetrics(SM_CXBORDER), pGetSystemMetricsForDpi(SM_CXBORDER, dpi),
         GetSystemMetrics(SM_CYBORDER), pGetSystemMetricsForDpi(SM_CYBORDER, dpi),
         GetSystemMetrics(SM_CXEDGE), pGetSystemMetricsForDpi(SM_CXEDGE, dpi),
         GetSystemMetrics(SM_CYEDGE), pGetSystemMetricsForDpi(SM_CYEDGE, dpi),
         GetSystemMetrics(SM_CXFIXEDFRAME), pGetSystemMetricsForDpi(SM_CXFIXEDFRAME, dpi),
         GetSystemMetrics(SM_CYFIXEDFRAME), pGetSystemMetricsForDpi(SM_CYFIXEDFRAME, dpi),
         GetSystemMetrics(SM_CXSIZEFRAME), pGetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi),
         GetSystemMetrics(SM_CYSIZEFRAME), pGetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi),
         GetSystemMetrics(SM_CXPADDEDBORDER), pGetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi),
         GetSystemMetrics(SM_CYCAPTION), pGetSystemMetricsForDpi(SM_CYCAPTION, dpi),
         GetSystemMetrics(SM_CXVSCROLL), pGetSystemMetricsForDpi(SM_CXVSCROLL, dpi)
         );
}
#endif

static void
win_adjust_borders(int t_width, int t_height)
{
  term_width = t_width;
  term_height = t_height;
  RECT cr = {0, 0, term_width + 2 * PADDING, term_height + OFFSET + 2 * PADDING};
  RECT wr = cr;
  window_style = WS_OVERLAPPEDWINDOW;
  if (border_style) {
    if (strcmp(border_style, "void") == 0)
      window_style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    else
      window_style &= ~(WS_CAPTION | WS_BORDER);
  }

  if (pGetDpiForMonitor && pAdjustWindowRectExForDpi) {
    HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    uint x, dpi;
    pGetDpiForMonitor(mon, 0, &x, &dpi);  // MDT_EFFECTIVE_DPI
    pAdjustWindowRectExForDpi(&wr, window_style, false, 0, dpi);
#ifdef debug_dpi
    RECT wr0 = cr;
    AdjustWindowRect(&wr0, window_style, false);
    printf("adjust borders dpi %3d: %d %d\n", dpi, (int)(wr.right - wr.left), (int)(wr.bottom - wr.top));
    printf("                      : %d %d\n", (int)(wr0.right - wr0.left), (int)(wr0.bottom - wr0.top));
    print_system_metrics(dpi, "win_adjust_borders");
#endif
  }
  else
    AdjustWindowRect(&wr, window_style, false);

  width = wr.right - wr.left;
  height = wr.bottom - wr.top;
#ifdef debug_resize
  printf("win_adjust_borders w/h %d %d\n", width, height);
#endif

  if (term.app_scrollbar || cfg.scrollbar)
    width += GetSystemMetrics(SM_CXVSCROLL);

  extra_width = width - (cr.right - cr.left);
  extra_height = height - (cr.bottom - cr.top);
  norm_extra_width = extra_width;
  norm_extra_height = extra_height;
}

void
win_adapt_term_size(bool sync_size_with_font, bool scale_font_with_size)
{
  trace_resize(("--- win_adapt_term_size sync_size %d scale_font %d (full %d Zoomed %d)\n", sync_size_with_font, scale_font_with_size, win_is_fullscreen, IsZoomed(wnd)));
  if (IsIconic(wnd))
    return;

#ifdef debug_dpi
  HDC dc = GetDC(wnd);
  printf("monitor size %dmm*%dmm res %d*%d dpi/dev %d",
         GetDeviceCaps(dc, HORZSIZE), GetDeviceCaps(dc, VERTSIZE), 
         GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES),
         GetDeviceCaps(dc, LOGPIXELSY));
  //googled this:
  //int physical_width = GetDeviceCaps(dc, DESKTOPHORZRES);
  //int virtual_width = GetDeviceCaps(dc, HORZRES);
  //int dpi = (int)(96f * physical_width / virtual_width);
  //but as observed here, physical_width and virtual_width are always equal
  ReleaseDC(wnd, dc);
  if (pGetDpiForMonitor) {
    HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    uint x, y;
    pGetDpiForMonitor(mon, 0, &x, &y);  // MDT_EFFECTIVE_DPI
    // we might think about scaling the font size by this factor,
    // but this is handled elsewhere; (used to be via WM_DPICHANGED, 
    // now via WM_WINDOWPOSCHANGED and initially)
    printf(" eff %d", y);
  }
  printf("\n");
#endif

  if (sync_size_with_font && !win_is_fullscreen) {
    // enforced win_set_chars(term.rows, term.cols):
    win_set_pixels(term.rows * cell_height, term.cols * cell_width);
    win_fix_position();
    trace_winsize("win_adapt_term_size > win_fix_position");

    win_invalidate_all(false);
    return;
  }

 /* Current window sizes ... */
  RECT cr, wr;
  GetClientRect(wnd, &cr);
  GetWindowRect(wnd, &wr);
  int client_width = cr.right - cr.left;
  int client_height = cr.bottom - cr.top;
  extra_width = wr.right - wr.left - client_width;
  extra_height = wr.bottom - wr.top - client_height;
  if (!win_is_fullscreen) {
    norm_extra_width = extra_width;
    norm_extra_height = extra_height;
  }
  int term_width = client_width - 2 * PADDING;
  int term_height = client_height - 2 * PADDING;
  if (!sync_size_with_font /*&& win_tabbar_visible()*/) {
    // apparently insignificant if sync_size_with_font && win_is_fullscreen
    term_height -= OFFSET;
  }
  if (!sync_size_with_font && win_search_visible()) {
    term_height -= SEARCHBAR_HEIGHT;
  }

  if (scale_font_with_size && term.cols != 0 && term.rows != 0) {
    // calc preliminary size (without font scaling), as below
    // should use term_height rather than rows; calc and store in term_resize
    int cols0 = max(1, term_width / cell_width);
    int rows0 = max(1, term_height / cell_height);

    // rows0/term.rows gives a rough scaling factor for cell_height
    // cols0/term.cols gives a rough scaling factor for cell_width
    // cell_height, cell_width give a rough scaling indication for font_size
    // height or width could be considered more according to preference
    bool bigger = rows0 * cols0 > term.rows * term.cols;
    int font_size1 =
      // heuristic best approach taken...
      // bigger
      //   ? max(font_size * rows0 / term.rows, font_size * cols0 / term.cols)
      //   : min(font_size * rows0 / term.rows, font_size * cols0 / term.cols);
      // bigger
      //   ? font_size * rows0 / term.rows + 2
      //   : font_size * rows0 / term.rows;
      bigger
        ? (font_size * rows0 / term.rows + font_size * cols0 / term.cols) / 2 + 1
        : (font_size * rows0 / term.rows + font_size * cols0 / term.cols) / 2;
      // bigger
      //   ? font_size * rows0 * cols0 / (term.rows * term.cols)
      //   : font_size * rows0 * cols0 / (term.rows * term.cols);
      trace_resize(("term size %d %d -> %d %d\n", term.rows, term.cols, rows0, cols0));
      trace_resize(("font size %d -> %d\n", font_size, font_size1));

    // heuristic attempt to stabilize font size roundtrips, esp. after fullscreen
    if (!bigger) font_size1 = font_size1 * 20 / 19;

    if (font_size1 != font_size)
      win_set_font_size(font_size1, false);
  }

  int cols = max(1, term_width / cell_width);
  int rows = max(1, term_height / cell_height);
  if (rows != term.rows || cols != term.cols) {
    term_resize(rows, cols);
    struct winsize ws = {rows, cols, cols * cell_width, rows * cell_height};
    child_resize(&ws);
  }
  else {  // also notify font size changes; filter identical updates later
    struct winsize ws = {rows, cols, cols * cell_width, rows * cell_height};
    child_resize(&ws);
  }
  win_invalidate_all(false);

  win_update_search();
  // support tabbar
  win_update_tabbar();

  term_schedule_search_update();
  win_schedule_update();
}

static int
win_fix_taskbar_max(int show_cmd)
{
  if (border_style && show_cmd == SW_SHOWMAXIMIZED) {
    // (SW_SHOWMAXIMIZED == SW_MAXIMIZE)
    // workaround for Windows failing to consider the taskbar properly 
    // when maximizing without WS_CAPTION in style (#732)
    MONITORINFO mi;
    get_my_monitor_info(&mi);
    RECT ar = mi.rcWork;
    RECT mr = mi.rcMonitor;
    if (mr.top != ar.top || mr.bottom != ar.bottom || mr.left != ar.left || mr.right != ar.right) {
      show_cmd = SW_RESTORE;
      // set window size
      SetWindowPos(wnd, null, 
                   ar.left, ar.top, ar.right - ar.left, ar.bottom - ar.top, 
                   SWP_NOZORDER);
      win_adapt_term_size(false, false);
    }
  }
  return show_cmd;
}

/*
 * Maximise or restore the window in response to a server-side request.
 * Argument value of 2 means go fullscreen.
 */
void
win_maximise(int max)
{
//printf("win_max %d is_full %d IsZoomed %d\n", max, win_is_fullscreen, IsZoomed(wnd));
  if (max == -2) // toggle full screen
    max = win_is_fullscreen ? 0 : 2;
  if (IsZoomed(wnd)) {
    if (!max)
      ShowWindow(wnd, SW_RESTORE);
    else if (max == 2 && !win_is_fullscreen)
      make_fullscreen();
  }
  else if (max) {
    if (max == 2) {  // full screen
      go_fullscr_on_max = true;
      ShowWindow(wnd, SW_MAXIMIZE);
    }
    else if (max == 1) {  // maximize
      // this would apply the workaround to consider the taskbar
      // but it would make maximizing irreversible, so let's not do it here
      //ShowWindow(wnd, win_fix_taskbar_max(SW_MAXIMIZE));
      // rather let Windows maximize as it prefers, including the bug
      ShowWindow(wnd, SW_MAXIMIZE);
    }
    else
      ShowWindow(wnd, SW_MAXIMIZE);
  }
}

/*
 * Go back to configured window size.
 */
static void
default_size(void)
{
  if (IsZoomed(wnd))
    ShowWindow(wnd, SW_RESTORE);
  win_set_chars(cfg.rows, cfg.cols);
}

void
win_update_transparency(int trans, bool opaque)
{
  //printf("win_update_transparency %d opaque %d\n", trans, opaque);
  if (trans == TR_GLASS)
    trans = 0;
  LONG style = GetWindowLong(wnd, GWL_EXSTYLE);
  style = trans ? style | WS_EX_LAYERED : style & ~WS_EX_LAYERED;
  SetWindowLong(wnd, GWL_EXSTYLE, style);
  if (trans) {
    if (opaque && term.has_focus)
      trans = 0;
    SetLayeredWindowAttributes(wnd, 0, 255 - (uchar)trans, LWA_ALPHA);
  }

  win_update_blur(opaque);
  win_update_glass(opaque);
}

void
win_update_scrollbar(bool inner)
{
  // enforce outer scrollbar if switched on
  int scrollbar = term.show_scrollbar ? (cfg.scrollbar ?: !inner) : 0;
  // keep config consistent with enforced scrollbar
  if (scrollbar && !cfg.scrollbar)
    cfg.scrollbar = 1;
  if (term.app_scrollbar && !scrollbar) {
    //printf("enforce application scrollbar %d->%d->%d\n", scrollbar, cfg.scrollbar, cfg.scrollbar ?: 1);
    scrollbar = cfg.scrollbar ?: 1;
  }

  LONG style = GetWindowLong(wnd, GWL_STYLE);
  SetWindowLong(wnd, GWL_STYLE,
                scrollbar ? style | WS_VSCROLL : style & ~WS_VSCROLL);

  default_size_token = true;  // prevent font zooming after Ctrl+Shift+O
  LONG exstyle = GetWindowLong(wnd, GWL_EXSTYLE);
  SetWindowLong(wnd, GWL_EXSTYLE,
                scrollbar < 0 ? exstyle | WS_EX_LEFTSCROLLBAR
                              : exstyle & ~WS_EX_LEFTSCROLLBAR);

  default_size_token = true;  // prevent font zooming after Ctrl+Shift+O
  if (inner || IsZoomed(wnd))
    SetWindowPos(wnd, null, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE |
                 SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  else {
    RECT wr;
    GetWindowRect(wnd, &wr);
    if (scrollbar && !(style & WS_VSCROLL))
      wr.right += GetSystemMetrics(SM_CXVSCROLL);
    else if (!scrollbar && (style & WS_VSCROLL))
      wr.right -= GetSystemMetrics(SM_CXVSCROLL);
    // set window size
    SetWindowPos(wnd, null, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOACTIVATE | SWP_NOMOVE |
                 SWP_NOZORDER | SWP_FRAMECHANGED);
  }

  win_fix_position();
}

void
win_font_cs_reconfig(bool font_changed)
{
  bool old_ambig_wide = cs_ambig_wide;
  cs_reconfig();
  if (term.report_font_changed && font_changed)
    if (term.report_ambig_width)
      child_write(cs_ambig_wide ? "\e[2W" : "\e[1W", 4);
    else
      child_write("\e[0W", 4);
  else if (term.report_ambig_width && old_ambig_wide != cs_ambig_wide)
    child_write(cs_ambig_wide ? "\e[2W" : "\e[1W", 4);
}

static void
font_cs_reconfig(bool font_changed)
{
  //printf("font_cs_reconfig font_changed %d\n", font_changed);
  if (font_changed) {
    win_init_fonts(cfg.font.size);
    if (tek_mode)
      tek_init(false, cfg.tek_glow);
    trace_resize((" (font_cs_reconfig -> win_adapt_term_size)\n"));
    win_adapt_term_size(true, false);
  }
  win_update_scrollbar(true); // assume "inner", shouldn't change anyway
  win_update_transparency(cfg.transparency, cfg.opaque_when_focused);
  win_update_mouse();

  win_font_cs_reconfig(font_changed);
}

void
win_reconfig(void)
{
  trace_resize(("--- win_reconfig\n"));
 /* Pass new config data to the terminal */
  term_reconfig();

  bool font_changed =
    wcscmp(new_cfg.font.name, cfg.font.name) ||
    new_cfg.font.size != cfg.font.size ||
    new_cfg.font.weight != cfg.font.weight ||
    new_cfg.font.isbold != cfg.font.isbold ||
    new_cfg.bold_as_font != cfg.bold_as_font ||
    new_cfg.bold_as_colour != cfg.bold_as_colour ||
    new_cfg.font_smoothing != cfg.font_smoothing;

  bool emojistyle_changed = new_cfg.emojis != cfg.emojis;

  if (new_cfg.fg_colour != cfg.fg_colour)
    win_set_colour(FG_COLOUR_I, new_cfg.fg_colour);

  if (new_cfg.bg_colour != cfg.bg_colour)
    win_set_colour(BG_COLOUR_I, new_cfg.bg_colour);

  if (new_cfg.cursor_colour != cfg.cursor_colour)
    win_set_colour(CURSOR_COLOUR_I, new_cfg.cursor_colour);

  /* Copy the new config and refresh everything */
  copy_config("win_reconfig", &cfg, &new_cfg);

  if (emojistyle_changed) {
    clear_emoji_data();
    win_invalidate_all(false);
  }

  font_cs_reconfig(font_changed);
}

static bool
confirm_exit(void)
{
#ifdef use_ps
  if (!child_is_parent())
    return true;

  /* command to retrieve list of child processes */
  //procps is ASCII-limited
  //char * pscmd = "LC_ALL=C.UTF-8 /bin/procps -o pid,ruser=USER -o comm -t %s 2> /dev/null || LC_ALL=C.UTF-8 /bin/ps -ef";
  //char * pscmd = "LC_ALL=C.UTF-8 /bin/ps -ef";
  char * pscmd = "LC_ALL=C.UTF-8 /bin/ps -es | /bin/sed -e 's,  *,	&,g' | /bin/cut -f 2,3,5-99 | /bin/tr -d '	'";
  char * tty = child_tty();
  if (strrchr(tty, '/'))
    tty = strrchr(tty, '/') + 1;
  char cmd[strlen(pscmd) + strlen(tty) + 1];
  sprintf(cmd, pscmd, tty, tty);
  FILE * procps = popen(cmd, "r");

  int cmdpos = 0;
  char * msg = newn(char, 1);
  strcpy(msg, "");
  if (procps) {
    int ll = 999;  // use very long input despite narrow msg box
                   // to avoid high risk of clipping within multi-byte char
                   // and failing the wide character transformation
    char line[ll]; // heap-allocated to prevent #530
    bool first = true;
    bool filter_tty = false;
    while (fgets(line, sizeof line, procps)) {
      line[strcspn(line, "\r\n")] = 0;  /* trim newline */
      if (first || !filter_tty || strstr(line, tty))  // should check column position too...
      {
        if (first) {
          if (strstr(line, "TTY")) {
            filter_tty = true;
          }
          char * cmd = strstr(line, " COMMAND");
          if (!cmd)
            cmd = strstr(line, " CMD");
          if (cmd)
            cmdpos = cmd + 1 - line;
          first = false;
        }
        msg = renewn(msg, strlen(msg) + strlen(line) + 4);
        strcat(msg, "|");  // cmdpos += strlen(prefix) outside loop!
        strcat(msg, line);
        strcat(msg, "\n");
      }
    }
    pclose(procps);
  }
  cmdpos += 1;
  wchar * proclist = cs__utftowcs(msg);
  int cpos = 0;
  for (uint i = 0; i < wcslen(proclist); i++) {
    if (cpos == 0) {
      if (proclist[i] == '|') {
        proclist[i] = 0x254E;  // │┃┆┇┊┋┠╎╏║╟
        cpos = 1;
      }
      else
        cpos = -1;
    }
    if (cpos > 0 && (!cmdpos || cpos <= cmdpos) && proclist[i] == ' ')
      proclist[i] = 0x2007;
    if (proclist[i] == '\n')
      cpos = 0;
    else
      cpos ++;
  }
#else
  wchar * proclist = grandchild_process_list();
  if (!proclist)
    return true;
#endif

  wchar * msg_pre = _W("Processes are running in session:");
  wchar * msg_post = _W("Close anyway?");

  wchar * wmsg = newn(wchar, wcslen(msg_pre) + wcslen(proclist) + wcslen(msg_post) + 2);
  wcscpy(wmsg, msg_pre);
  wcscat(wmsg, W("\n"));
  wcscat(wmsg, proclist);
  wcscat(wmsg, msg_post);
  free(proclist);

  int ret = message_box_w(
              wnd, wmsg,
              W(APPNAME), MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2,
              null
            );
  free(wmsg);

  // Treat failure to show the dialog as confirmation.
  return !ret || ret == IDOK;
}

void
win_close(void)
{
  if (!support_wsl && *cfg.exit_commands) {
    // try to determine foreground program
    char * fg_prog = foreground_prog();
    if (fg_prog) {
      // match program base name
      char * exits = cs__wcstombs(cfg.exit_commands);
      char * paste = matchconf(exits, fg_prog);
      if (paste) {
        child_send(paste, strlen(paste));
        free(exits);  // also frees paste which points into exits
        free(fg_prog);
        return;  // don't close terminal
      }
      free(exits);
      free(fg_prog);
    }
  }

  if (!cfg.confirm_exit || confirm_exit())
    child_kill((GetKeyState(VK_SHIFT) & 0x80) != 0);
}


/*
   Mouse pointer style.
 */

static struct {
  void * tag;
  wchar * name;
} cursorstyles[] = {
  {IDC_APPSTARTING, W("appstarting")},
  {IDC_ARROW, W("arrow")},
  {IDC_CROSS, W("cross")},
  {IDC_HAND, W("hand")},
  {IDC_HELP, W("help")},
  {IDC_IBEAM, W("ibeam")},
  {IDC_ICON, W("icon")},
  {IDC_NO, W("no")},
  {IDC_SIZE, W("size")},
  {IDC_SIZEALL, W("sizeall")},
  {IDC_SIZENESW, W("sizenesw")},
  {IDC_SIZENS, W("sizens")},
  {IDC_SIZENWSE, W("sizenwse")},
  {IDC_SIZEWE, W("sizewe")},
  {IDC_UPARROW, W("uparrow")},
  {IDC_WAIT, W("wait")},
};

static HCURSOR cursors[2] = {0, 0};

HCURSOR
win_get_cursor(bool appmouse)
{
  return cursors[appmouse];
}

void
set_cursor_style(bool appmouse, wchar * style)
{
  HCURSOR c = 0;
  if (wcschr(style, '.')) {
    char * pf = get_resource_file(W("pointers"), style, false);
    wchar * wpf = 0;
    if (pf) {
      wpf = path_posix_to_win_w(pf);
      free(pf);
    }
    if (wpf) {
      c = LoadImageW(null, wpf, IMAGE_CURSOR, 
                           0, 0,
                           LR_DEFAULTSIZE |
                           LR_LOADFROMFILE | LR_LOADTRANSPARENT);
      free(wpf);
    }
  }
  if (!c)
    for (uint i = 0; i < lengthof(cursorstyles); i++)
      if (0 == wcscmp(style, cursorstyles[i].name)) {
        c = LoadCursor(null, cursorstyles[i].tag);
        break;
      }
  if (!c)
    c = LoadCursor(null, appmouse ? IDC_ARROW : IDC_IBEAM);

  if (!IS_INTRESOURCE(cursors[appmouse]))
    DestroyCursor(cursors[appmouse]);
  cursors[appmouse] = c;
  SetClassLongPtr(wnd, GCLP_HCURSOR, (LONG_PTR)c);
  SetCursor(c);
}

static void
win_init_cursors()
{
  set_cursor_style(true, W("arrow"));
  set_cursor_style(false, W("ibeam"));
}


/*
   Diagnostic functions.
 */

void
show_message(char * msg, UINT type)
{
  FILE * out = (type & (MB_ICONWARNING | MB_ICONSTOP)) ? stderr : stdout;
  char * outmsg = cs__utftombs(msg);
  if (fputs(outmsg, out) < 0 || fputs("\n", out) < 0 || fflush(out) < 0) {
    wchar * wmsg = cs__utftowcs(msg);
    message_box_w(0, wmsg, W(APPNAME), type, null);
    delete(wmsg);
  }
  delete(outmsg);
}

void
show_info(char * msg)
{
  show_message(msg, MB_OK);
}

static char *
opterror_msg(string msg, bool utf8params, string p1, string p2)
{
  // Note: msg is in UTF-8,
  // parameters are in current encoding unless utf8params is true
  if (!utf8params) {
    if (p1) {
      wchar * w = cs__mbstowcs(p1);
      p1 = cs__wcstoutf(w);
      free(w);
    }
    if (p2) {
      wchar * w = cs__mbstowcs(p2);
      p2 = cs__wcstoutf(w);
      free(w);
    }
  }

  char * fullmsg;
  int len = asprintf(&fullmsg, msg, p1, p2);
  if (!utf8params) {
    if (p1)
      free((char *)p1);
    if (p2)
      free((char *)p2);
  }

  if (len > 0)
    return fullmsg;
  else
    return null;
}

bool
print_opterror(FILE * stream, string msg, bool utf8params, string p1, string p2)
{
  char * fullmsg = opterror_msg(msg, utf8params, p1, p2);
  bool ok = false;
  if (fullmsg) {
    char * outmsg = cs__utftombs(fullmsg);
    delete(fullmsg);
    ok = fprintf(stream, "%s.\n", outmsg);
    if (ok)
      ok = fflush(stream);
    delete(outmsg);
  }
  return ok;
}

static void
print_error(string msg)
{
  print_opterror(stderr, msg, true, "", "");
}

static void
option_error(char * msg, char * option, int err)
{
  finish_config();  // ensure localized message
  // msg is in UTF-8, option is in current encoding
  char * optmsg = opterror_msg(_(msg), false, option, null);
  //char * fullmsg = asform("%s\n%s", optmsg, _("Try '--help' for more information"));
  char * fullmsg = strdup(optmsg);
  strappend(fullmsg, "\n");
  if (err) {
    strappend(fullmsg, asform("[Error info %d]\n", err));
  }
  strappend(fullmsg, _("Try '--help' for more information"));
  show_message(fullmsg, MB_ICONWARNING);
  exit(1);
}

static void
show_iconwarn(wchar * winmsg)
{
  char * msg = _("Could not load icon");
  char * in = cs__wcstoutf(cfg.icon);

  char * fullmsg;
  int len;
  if (winmsg) {
    char * wmsg = cs__wcstoutf(winmsg);
    len = asprintf(&fullmsg, "%s '%s':\n%s", msg, in, wmsg);
    free(wmsg);
  }
  else
    len = asprintf(&fullmsg, "%s '%s'", msg, in);
  free(in);
  if (len > 0) {
    show_message(fullmsg, MB_ICONWARNING);
    free(fullmsg);
  }
  else
    show_message(msg, MB_ICONWARNING);
}


/*
   Message handling.
 */

#define dont_debug_messages
#define dont_debug_only_input_messages
#define dont_debug_only_focus_messages
#define dont_debug_only_sizepos_messages
#define dont_debug_mouse_messages
#define dont_debug_hook

static void win_global_keyboard_hook(bool on);

static LPARAM
screentoclient(HWND wnd, LPARAM lp)
{
  POINT wpos = {.x = GET_X_LPARAM(lp), .y = GET_Y_LPARAM(lp)};
  ScreenToClient(wnd, &wpos);
  return MAKELPARAM(wpos.x, wpos.y);
}

static bool
in_client_area(HWND wnd, LPARAM lp)
{
  POINT wpos = {.x = GET_X_LPARAM(lp), .y = GET_Y_LPARAM(lp)};
  ScreenToClient(wnd, &wpos);
  int height, width;
  win_get_pixels(&height, &width, false);
  height += OFFSET + 2 * PADDING;
  width += 2 * PADDING;
  return wpos.y >= 0 && wpos.y < height && wpos.x >= 0 && wpos.x < width;
}

static LRESULT CALLBACK
win_proc(HWND wnd, UINT message, WPARAM wp, LPARAM lp)
{
#ifdef debug_messages
static struct {
  uint wm_;
  char * wm_name;
} wm_names[] = {
#include "_wm.t"
};
  char * wm_name = "WM_?";
  for (uint i = 0; i < lengthof(wm_names); i++)
    if (message == wm_names[i].wm_ && !strstr(wm_names[i].wm_name, "FIRST")) {
      wm_name = wm_names[i].wm_name;
      break;
    }
  if ((message != WM_KEYDOWN || !(lp & 0x40000000))
      && message != WM_TIMER && message != WM_NCHITTEST
# ifndef debug_mouse_messages
      && message != WM_SETCURSOR
      && message != WM_MOUSEMOVE && message != WM_NCMOUSEMOVE
# endif
     )
# ifdef debug_only_sizepos_messages
    if (strstr(wm_name, "POSCH") || strstr(wm_name, "SIZ"))
# endif
# ifdef debug_only_focus_messages
    if (strstr(wm_name, "ACTIVATE") || strstr(wm_name, "FOCUS"))
# endif
# ifdef debug_only_input_messages
    if (strstr(wm_name, "MOUSE") || strstr(wm_name, "BUTTON") || strstr(wm_name, "CURSOR") || strstr(wm_name, "KEY"))
# endif
    printf("[%d]->%8p %04X %s (%08X %08X)\n", (int)time(0), wnd, message, wm_name, (unsigned)wp, (unsigned)lp);
#endif

  switch (message) {
    when WM_NCCREATE:
      if (cfg.handle_dpichanged && pEnableNonClientDpiScaling) {
        //CREATESTRUCT * csp = (CREATESTRUCT *)lp;
        resizing = true;
        BOOL res = pEnableNonClientDpiScaling(wnd);
        resizing = false;
        (void)res;
#ifdef debug_dpi
        uint err = GetLastError();
        int wmlen = 1024;  // size of heap-allocated array
        wchar winmsg[wmlen];  // constant and < 1273 or 1705 => issue #530
        FormatMessageW(
              FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
              0, err, 0, winmsg, wmlen, 0
        );
        printf("NC:EnableNonClientDpiScaling: %d %ls\n", !!res, winmsg);
#endif
        return 1;
      }

    when WM_TIMER: {
      KillTimer(wnd, wp);
      void_fn cb = (void_fn)wp;
      cb();
      return 0;
    }

    when WM_CLOSE:
      win_close();
      return 0;

#ifdef show_icon_via_callback
    when WM_MEASUREITEM: {
      MEASUREITEMSTRUCT* lpmis = (MEASUREITEMSTRUCT*)lp;
      if (lpmis) {
        lpmis->itemWidth += 2;
        if (lpmis->itemHeight < 16)
          lpmis->itemHeight = 16;
      }
    }

//https://www.nanoant.com/programming/themed-menus-icons-a-complete-vista-xp-solution
    when WM_DRAWITEM: {
# ifdef debug_drawicon
      printf("WM_DRAWITEM\n");
# endif
      DRAWITEMSTRUCT* lpdis = (DRAWITEMSTRUCT*)lp;
      /// this is the wrong wnd anyway...
      HICON icon = (HICON)GetClassLongPtr(wnd, GCLP_HICONSM);
      if (!lpdis || lpdis->CtlType != ODT_MENU)
        break; // not for a menu
      if (!icon)
        break;
      DrawIcon(lpdis->hDC,
               lpdis->rcItem.left - 16,
               lpdis->rcItem.top
                      + (lpdis->rcItem.bottom - lpdis->rcItem.top - 16) / 2,
               icon);
// -> Invalid cursor handle.
      DestroyIcon(icon);
    }
#endif

    when WM_USER:  // reposition and resize
#ifdef debug_tabs
      printf("[%8p] WM_USER %d,%d %d,%d\n", wnd, (INT16)LOWORD(lp), (INT16)HIWORD(lp), LOWORD(wp), HIWORD(wp));
#endif
      wm_user = true;
      if (!wp && lp == WIN_TOP) { // Ctrl+Alt or session switcher
        // these do not work:
        // BringWindowToTop(wnd);
        // SetForegroundWindow(wnd);
        // SetActiveWindow(wnd);

        // this would work, kind of, 
        // but blocks previous window from raising on next click:
        SetWindowPos(wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetWindowPos(wnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        ShowWindow(wnd, SW_RESTORE);
      }
      else if (!wp && lp == WIN_TITLE) {
        if (sync_level() || win_tabbar_visible()) {
          refresh_tab_titles(false);
          // support tabbar
          win_update_tabbar();
        }
      }
      else if (sync_level()) {
#ifdef debug_tabs
        printf("[%8p] switched %d,%d %d,%d\n", wnd, (INT16)LOWORD(lp), (INT16)HIWORD(lp), LOWORD(wp), HIWORD(wp));
#endif
#ifdef use_init_position
        if (win_tabbar_visible()) {
          // support tabbar; however, the purpose of this handling is unclear
          if (!wp && lp == WIN_INIT_POS)
            win_synctabs(4);
          else
            win_handle_sync_msg(wp, lp);
        }
        else
#endif
        if (!wp) {
          if (lp == WIN_MINIMIZE && sync_level() >= 3)
            ShowWindow(wnd, SW_MINIMIZE);
          else if (lp == WIN_FULLSCREEN && sync_level())
            win_maximise(2);
          else if (lp == WIN_MAXIMIZE && sync_level())
            win_maximise(1);
        }
        else if (sync_level()) {
          if (win_is_fullscreen)
            clear_fullscreen();
          if (IsZoomed(wnd))
            win_maximise(0);
#ifdef attempt_to_restore_tabset_consistently
          // if a set of synchronized windows (tab set) is minimized, 
          // one window restored and closed, the others remain minimized;
          // this is a failed attempt to fix that inconsistency
          //printf("WM_USER sync iconic %d\n", IsIconic(wnd));
          if (IsIconic(wnd))
            ShowWindow(wnd, SW_RESTORE);
#endif

          // (INT16) to handle multi-monitor negative coordinates properly
          // set window size
          SetWindowPos(wnd, null,
                       //GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
                       (INT16)LOWORD(lp), (INT16)HIWORD(lp),
                       LOWORD(wp), HIWORD(wp),
                       SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        }
      }
#ifdef debug_tabs
      printf("[%8p] WM_USER end\n", wnd);
#endif
      wm_user = false;

    when WM_COMMAND or WM_SYSCOMMAND: {
# ifdef debug_messages
      static struct {
        uint idm_;
        char * idm_name;
      } idm_names[] = {
# include "_winidm.t"
      };
      char * idm_name = "IDM_?";
      for (uint i = 0; i < lengthof(idm_names); i++)
        if ((wp & ~0xF) == idm_names[i].idm_) {
          idm_name = idm_names[i].idm_name;
          break;
        }
      printf("                           %04X %s\n", (int)wp, idm_name);
# endif
      if ((wp & ~0xF) >= 0xF000)
        ; // skip WM_SYSCOMMAND from Windows here (but process own ones)
      else if ((wp & ~0xF) >= IDM_GOTAB)
        win_gotab(wp - IDM_GOTAB);
      else if ((wp & ~0xF) >= IDM_CTXMENUFUNCTION)
        user_function(cfg.ctx_user_commands, wp - IDM_CTXMENUFUNCTION);
      else if ((wp & ~0xF) >= IDM_SYSMENUFUNCTION)
        user_function(cfg.sys_user_commands, wp - IDM_SYSMENUFUNCTION);
      else if ((wp & ~0xF) >= IDM_SESSIONCOMMAND)
        win_launch(wp - IDM_SESSIONCOMMAND);
      else if ((wp & ~0xF) >= IDM_USERCOMMAND)
        user_command(cfg.user_commands, wp - IDM_USERCOMMAND);
      else
      switch (wp & ~0xF) {  /* low 4 bits reserved to Windows */
        when IDM_BREAK: child_break();
        when IDM_OPEN: term_open();
        when IDM_COPY: term_copy();
        when IDM_COPY_TEXT: term_copy_as('t');
        when IDM_COPY_TABS: term_copy_as('T');
        when IDM_COPY_TXT: term_copy_as('p');
        when IDM_COPY_RTF: term_copy_as('r');
        when IDM_COPY_HTXT: term_copy_as('h');
        when IDM_COPY_HFMT: term_copy_as('f');
        when IDM_COPY_HTML: term_copy_as('H');
        when IDM_COPASTE: term_copy(); win_paste();
        when IDM_CLRSCRLBCK: term_clear_scrollback(); term.disptop = 0;
        when IDM_TOGLOG: toggle_logging();
        when IDM_HTML: term_export_html(GetKeyState(VK_SHIFT) & 0x80);
        when IDM_TOGCHARINFO: toggle_charinfo();
        when IDM_TOGVT220KB: toggle_vt220();
        when IDM_PASTE: win_paste();
        when IDM_SELALL: term_select_all(); win_update(false);
        when IDM_RESET: winimgs_clear(); term_reset(true); win_update(false);
          if (tek_mode)
            tek_reset();
        when IDM_TEKRESET:
          if (tek_mode)
            tek_reset();
        when IDM_TEKPAGE:
          if (tek_mode)
            tek_page();
        when IDM_TEKCOPY:
          if (tek_mode)
            term_save_image();
        when IDM_SAVEIMG:
          term_save_image();
        when IDM_DEFSIZE:
          default_size_token = true;
          default_size();
        when IDM_DEFSIZE_ZOOM:
          if (GetKeyState(VK_SHIFT) & 0x80) {
            // Shift+Alt+F10 should restore both window size and font size

            // restore default font size first:
            win_zoom_font(0, false);

            // restore window size:
            default_size_token = true;
            default_size();  // or defer to WM_PAINT
          }
          else {
            default_size();
          }
        when IDM_FULLSCREEN or IDM_FULLSCREEN_ZOOM: {
          bool ctrl = GetKeyState(VK_CONTROL) & 0x80;
          bool shift = GetKeyState(VK_SHIFT) & 0x80;
          if (((wp & ~0xF) == IDM_FULLSCREEN_ZOOM && shift)
           || (cfg.zoom_font_with_window && shift && !ctrl)
             )
            zoom_token = 4;
          else {
            zoom_token = -4;
            default_size_token = true;
          }
          win_maximise(win_is_fullscreen ? 0 : 2);

          term_schedule_search_update();
          win_update_search();
          // support tabbar
          win_update_tabbar();
        }
        when IDM_SCROLLBAR:
          term.show_scrollbar = !term.show_scrollbar;
          win_update_scrollbar(false);
        when IDM_SEARCH: win_open_search();
        when IDM_FLIPSCREEN: term_flip_screen();
        when IDM_OPTIONS: win_open_config();
        when IDM_NEW: {
          HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
          int x, y;
          int moni = search_monitors(&x, &y, mon, true, 0);
          child_fork(main_argc, main_argv, moni, get_mods() & MDK_SHIFT, false);
        }
        when IDM_NEW_CWD: {
          HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
          int x, y;
          int moni = search_monitors(&x, &y, mon, true, 0);
          child_fork(main_argc, main_argv, moni, get_mods() & MDK_SHIFT, true);
        }
        when IDM_NEW_MONI: {
          int moni = lp;
          child_fork(main_argc, main_argv, moni, get_mods() & MDK_SHIFT, false);
        }
        when IDM_COPYTITLE: win_copy_title();
        when IDM_KEY_DOWN_UP: {
          bool on = lp & 0x10000;
          int vk = lp & 0xFFFF;
          //printf("IDM_KEY_DOWN_UP -> do_win_key_toggle %02X\n", vk);
          do_win_key_toggle(vk, on);
        }
      }
    }

    when WM_APP:
      update_available_version(wp);

    when WM_VSCROLL:
      //printf("WM_VSCROLL %d\n", LOWORD(wp));
      if (!term.app_scrollbar)
        switch (LOWORD(wp)) {
          when SB_LINEUP:   term_scroll(0, -1);
          when SB_LINEDOWN: term_scroll(0, +1);
          when SB_PAGEUP:   term_scroll(0, -max(1, term.rows - 1));
          when SB_PAGEDOWN: term_scroll(0, +max(1, term.rows - 1));
          when SB_THUMBPOSITION or SB_THUMBTRACK: {
            SCROLLINFO info;
            info.cbSize = sizeof(SCROLLINFO);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(wnd, SB_VERT, &info);
            term_scroll(1, info.nTrackPos);
          }
          when SB_TOP:      term_scroll(+1, 0);
          when SB_BOTTOM:   term_scroll(-1, 0);
          //when SB_ENDSCROLL: ;
          // these two may be used by mintty keyboard shortcuts (not by Windows)
          when SB_PRIOR:    term_scroll(SB_PRIOR, 0);
          when SB_NEXT:     term_scroll(SB_NEXT, 0);
        }
      else {
        switch (LOWORD(wp)) {
          when SB_LINEUP:
            //win_key_down(VK_UP, 1);
            win_csi_seq("65", "#e");
          when SB_LINEDOWN:
            //win_key_down(VK_DOWN, 1);
            win_csi_seq("66", "#e");
          when SB_PAGEUP:
            //win_key_down(VK_PRIOR, 1);
            win_csi_seq("5", "#e");
          when SB_PAGEDOWN:
            //win_key_down(VK_NEXT, 1);
            win_csi_seq("6", "#e");
          when SB_TOP:
            child_printf("\e[0#d");
          when SB_BOTTOM:
            child_printf("\e[%u#d", scroll_len);
          when SB_THUMBPOSITION or SB_THUMBTRACK: {
            SCROLLINFO info;
            info.cbSize = sizeof(SCROLLINFO);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(wnd, SB_VERT, &info);
            child_printf("\e[%u#d", info.nTrackPos);
          }
        }
        // while holding the mouse button on the scrollbar (e.g. dragging), 
        // messages are not dispatched to the application;
        // so in order to make any response effective on the screen, 
        // we need to call the child_proc function here;
        // additional delay avoids incomplete delivery of such echo (#1033),
        // 1ms is not sufficient
        usleep(5555);
        child_proc();
      }

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

    when WM_MOUSEWHEEL or WM_MOUSEHWHEEL: {
      bool horizontal = message == WM_MOUSEHWHEEL;
      // check whether in client area (terminal pane) or over scrollbar...
      POINT wpos = {.x = GET_X_LPARAM(lp), .y = GET_Y_LPARAM(lp)};
      ScreenToClient(wnd, &wpos);
      int height, width;
      win_get_pixels(&height, &width, false);
      height += OFFSET + 2 * PADDING;
      width += 2 * PADDING;
      int delta = GET_WHEEL_DELTA_WPARAM(wp);  // positive means up or right
      //printf("%d %d %d %d %d\n", wpos.y, wpos.x, height, width, delta);
      if (wpos.y >= 0 && wpos.y < height) {
        if (wpos.x >= 0 && wpos.x < width)
          win_mouse_wheel(wpos, horizontal, delta);
        else if (!horizontal) {
          int hsb = win_has_scrollbar();
          if (hsb && term.app_scrollbar) {
            int wsb = GetSystemMetrics(SM_CXVSCROLL);
            if ((hsb > 0 && wpos.x >= width && wpos.x < width + wsb)
             || (hsb < 0 && wpos.x < 0 && wpos.x >= - wsb)
               )
            {
              if (delta > 0) // mouse wheel up
                //win_key_down(VK_UP, 1);
                win_csi_seq("65", "#e");
              else // mouse wheel down
                //win_key_down(VK_DOWN, 1);
                win_csi_seq("66", "#e");
            }
          }
        }
      }
    }

    when WM_MOUSEMOVE: win_mouse_move(false, lp);
    when WM_NCMOUSEMOVE: win_mouse_move(true, screentoclient(wnd, lp));
    when WM_LBUTTONDOWN: win_mouse_click(MBT_LEFT, lp);
    when WM_RBUTTONDOWN: win_mouse_click(MBT_RIGHT, lp);
    when WM_MBUTTONDOWN: win_mouse_click(MBT_MIDDLE, lp);
    when WM_XBUTTONDOWN:
      switch (HIWORD(wp)) {
        when XBUTTON1: win_mouse_click(MBT_4, lp);
        when XBUTTON2: win_mouse_click(MBT_5, lp);
      }
    when WM_LBUTTONUP: win_mouse_release(MBT_LEFT, lp);
    when WM_RBUTTONUP: win_mouse_release(MBT_RIGHT, lp);
    when WM_MBUTTONUP: win_mouse_release(MBT_MIDDLE, lp);
    when WM_XBUTTONUP:
      switch (HIWORD(wp)) {
        when XBUTTON1: win_mouse_release(MBT_4, lp);
        when XBUTTON2: win_mouse_release(MBT_5, lp);
      }
    when WM_NCLBUTTONDOWN or WM_NCLBUTTONDBLCLK:
      if (in_client_area(wnd, lp)) {
        // clicked within "client area";
        // Windows sends the NC message nonetheless when Ctrl+Alt is held
        if (win_mouse_click(MBT_LEFT, screentoclient(wnd, lp)))
          return 0;
      }
      else
      if (wp == HTCAPTION && get_mods() == MDK_CTRL) {
        if (win_title_menu(true))
          return 0;
      }
    when WM_NCRBUTTONDOWN or WM_NCRBUTTONDBLCLK:
      if (in_client_area(wnd, lp)) {
        // clicked within "client area";
        // Windows sends the NC message nonetheless when Ctrl+Alt is held
        if (win_mouse_click(MBT_RIGHT, screentoclient(wnd, lp)))
          return 0;
      }
      else
      if (wp == HTCAPTION && (sync_level() > 0 || get_mods() == MDK_CTRL)) {
        if (win_title_menu(false))
          return 0;
      }
    when WM_NCMBUTTONDOWN or WM_NCMBUTTONDBLCLK:
      if (in_client_area(wnd, lp)) {
        if (win_mouse_click(MBT_MIDDLE, screentoclient(wnd, lp)))
          return 0;
      }
    when WM_NCXBUTTONDOWN or WM_NCXBUTTONDBLCLK:
      if (in_client_area(wnd, lp))
        switch (HIWORD(wp)) {
          when XBUTTON1: if (win_mouse_click(MBT_4, screentoclient(wnd, lp)))
                           return 0;
          when XBUTTON2: if (win_mouse_click(MBT_5, screentoclient(wnd, lp)))
                           return 0;
        }
    when WM_NCLBUTTONUP:
      if (in_client_area(wnd, lp)) {
        win_mouse_release(MBT_LEFT, screentoclient(wnd, lp));
        return 0;
      }
    when WM_NCRBUTTONUP:
      if (in_client_area(wnd, lp)) {
        win_mouse_release(MBT_RIGHT, screentoclient(wnd, lp));
        return 0;
      }
    when WM_NCMBUTTONUP:
      if (in_client_area(wnd, lp)) {
        win_mouse_release(MBT_MIDDLE, screentoclient(wnd, lp));
        return 0;
      }
    when WM_NCXBUTTONUP:
      if (in_client_area(wnd, lp))
        switch (HIWORD(wp)) {
          when XBUTTON1: win_mouse_release(MBT_4, screentoclient(wnd, lp));
                         return 0;
          when XBUTTON2: win_mouse_release(MBT_5, screentoclient(wnd, lp));
                         return 0;
        }

    when WM_KEYDOWN or WM_SYSKEYDOWN:
      //printf("[%ld] WM_KEY %02X\n", mtime(), (int)wp);
      if (win_key_down(wp, lp))
        return 0;

    when WM_KEYUP or WM_SYSKEYUP:
      if (win_key_up(wp, lp))
        return 0;

    when WM_CHAR or WM_SYSCHAR:
      provide_input(wp);
      child_sendw(&(wchar){wp}, 1);
      return 0;

    when WM_UNICHAR:
      if (wp == UNICODE_NOCHAR)
        return true;
      else if (wp > 0xFFFF) {
        provide_input(0xFFFF);
        child_sendw((wchar[]){high_surrogate(wp), low_surrogate(wp)}, 2);
        return false;
      }
      else {
        provide_input(wp);
        child_sendw(&(wchar){wp}, 1);
        return false;
      }

    when WM_MENUCHAR:
      // this is sent after leaving the system menu with ESC 
      // and typing a key; insert the key and prevent the beep
      provide_input(wp);
      child_sendw(&(wchar){wp}, 1);
      return MNC_CLOSE << 16;

#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031D
#endif
    // Try to clear selection when clipboard content is updated (#742)
    when WM_CLIPBOARDUPDATE:
      if (clipboard_token)
        clipboard_token = false;
      else {
        term.selected = false;
        win_update(false);
      }
      return 0;

#ifdef catch_lang_change
    // this is rubbish; only the initial change would be captured anyway;
    // if (Shift-)Control-digit is mapped as a keyboard switch shortcut 
    // on Windows level, it is intentionally overridden and does not 
    // need to be re-tweaked here
    when WM_INPUTLANGCHANGEREQUEST:  // catch Shift-Control-0 (#233)
      // guard win_key_down with key state in order to avoid key '0' floods
      // as generated by non-key language change events (#472)
      if ((GetKeyState(VK_SHIFT) & 0x80) && (GetKeyState(VK_CONTROL) & 0x80))
        if (win_key_down('0', 0x000B0001))
          return 0;
#endif

    when WM_INPUTLANGCHANGE:
      win_set_ime_open(ImmIsIME(GetKeyboardLayout(0)) && ImmGetOpenStatus(imc));

    when WM_IME_NOTIFY:
      if (wp == IMN_SETOPENSTATUS)
        win_set_ime_open(ImmGetOpenStatus(imc));

    when WM_IME_STARTCOMPOSITION:
      ImmSetCompositionFont(imc, &lfont);

    when WM_IME_COMPOSITION:
      if (lp & GCS_RESULTSTR) {
        LONG len = ImmGetCompositionStringW(imc, GCS_RESULTSTR, null, 0);
        if (len > 0) {
          char buf[len];
          ImmGetCompositionStringW(imc, GCS_RESULTSTR, buf, len);
          provide_input(*(wchar *)buf);
          child_sendw((wchar *)buf, len / 2);
        }
        return 1;
      }

    when WM_THEMECHANGED or WM_WININICHANGE or WM_SYSCOLORCHANGE:
      // Size of window border (border, title bar, scrollbar) changed by:
      //   Personalization of window geometry (e.g. Title Bar Size)
      //     -> Windows sends WM_SYSCOLORCHANGE
      //   Performance Option "Use visual styles on windows and borders"
      //     -> Windows sends WM_THEMECHANGED and WM_SYSCOLORCHANGE
      // and in both case a couple of WM_WININICHANGE

      win_adjust_borders(cell_width * cfg.cols, cell_height * cfg.rows);
      RedrawWindow(wnd, null, null, 
                   RDW_FRAME | RDW_INVALIDATE |
                   RDW_UPDATENOW | RDW_ALLCHILDREN);
      win_update_search();
      // support tabbar
      win_update_tabbar();
      // update dark mode
      if (message == WM_WININICHANGE) {
        // SetWindowTheme will cause an asynchronous WM_THEMECHANGED message,
        // so guard it by WM_WININICHANGE;
        // this will switch from Light to Dark mode immediately but not back!
        //win_dark_mode(wnd);
      }

    when WM_FONTCHANGE:
      font_cs_reconfig(true);

    when WM_PAINT:
      win_paint();

#ifdef handle_default_size_asynchronously
      if (default_size_token) {
        default_size();
        default_size_token = false;
      }
#endif

      return 0;

    when WM_MOUSEACTIVATE:
      // prevent accidental selection on activation (#717)
      if (LOWORD(lp) == HTCLIENT && HIWORD(lp) == WM_LBUTTONDOWN)
        if (!getenv("ConEmuPID"))
#ifdef suppress_click_on_focus_at_message_level
#warning this would also obstruct mouse function in the search bar
          // ignore focus click
          return MA_ACTIVATEANDEAT;
#else
          // support selective mouse click suppression
          click_focus_token = true;
#endif

    when WM_ACTIVATE:
      //printf("[%p] WM_ACTIVATE act %d focus %d posch %d\n", wnd, (wp & 0xF) != WA_INACTIVE, GetFocus() == wnd, poschanging);
      if ((wp & 0xF) != WA_INACTIVE) {
        flash_taskbar(false);  /* stop */
        term_set_focus(true, true);

        // tab management: unhide this tab and hide others
        if (!poschanging)  // skip while moving; may cause tab switching
          win_set_tab_focus('A');
      } else {
        term_set_focus(false, true);
      }
      win_update_transparency(cfg.transparency, cfg.opaque_when_focused);
      win_key_reset();
#ifdef adapt_term_size_on_activate
      // support tabbar?
      // this was included in the original patch but its purpose is unclear
      // and it causes some flickering
      win_adapt_term_size(false, false);
#endif

    when WM_SETFOCUS:
      trace_resize(("# WM_SETFOCUS VK_SHIFT %02X\n", (uchar)GetKeyState(VK_SHIFT)));
      term_set_focus(true, false);

      // tab management: do not repeat here; may cause tab switching
      //win_set_tab_focus('F');  // unhide this tab and hide others

      win_sys_style(true);
      CreateCaret(wnd, caretbm, 0, 0);
      //flash_taskbar(false);  /* stop; not needed when leaving search bar */
      win_update(false);
      ShowCaret(wnd);
      zoom_token = -4;

    when WM_KILLFOCUS:
      win_show_mouse();
      term_set_focus(false, false);
      win_sys_style(false);
      win_destroy_tip();
      DestroyCaret();
      win_update(false);

    when WM_INITMENU:
      // win_update_menus is already called before calling TrackPopupMenu
      // which is supposed to initiate this message;
      // however, if we skip the call here, the "New" item will 
      // not be initialised !?!
      win_update_menus(true);
      return 0;

    when WM_MOVING:
      trace_resize(("# WM_MOVING VK_SHIFT %02X\n", (uchar)GetKeyState(VK_SHIFT)));
      win_destroy_tip();
      zoom_token = -4;
      moving = true;

    when WM_ENTERSIZEMOVE:
      trace_resize(("# WM_ENTERSIZEMOVE VK_SHIFT %02X\n", (uchar)GetKeyState(VK_SHIFT)));
      resizing = true;

    when WM_SIZING: {  // mouse-drag window resizing
      trace_resize(("# WM_SIZING (resizing %d) VK_SHIFT %02X\n", resizing, (uchar)GetKeyState(VK_SHIFT)));
      zoom_token = 2;
     /*
      * This does two jobs:
      * 1) Keep the tip uptodate
      * 2) Make sure the window size is _stepped_ in units of the font size.
      */
      LPRECT r = (LPRECT) lp;
      int width = r->right - r->left - extra_width - 2 * PADDING;
      int height = r->bottom - r->top - extra_height - 2 * PADDING - OFFSET;
      int cols = max(1, (float)width / cell_width + 0.5);
      int rows = max(1, (float)height / cell_height + 0.5);

      int ew = width - cols * cell_width;
      int eh = height - rows * cell_height;

      if (wp >= WMSZ_BOTTOM) {
        wp -= WMSZ_BOTTOM;
        r->bottom -= eh;
      }
      else if (wp >= WMSZ_TOP) {
        wp -= WMSZ_TOP;
        r->top += eh;
      }

      if (wp == WMSZ_RIGHT)
        r->right -= ew;
      else if (wp == WMSZ_LEFT)
        r->left += ew;

      win_show_tip(r->left + extra_width, r->top + extra_height, cols, rows);

      return ew || eh;
    }

    when WM_SIZE: {
      trace_resize(("# WM_SIZE (resizing %d) VK_SHIFT %02X\n", resizing, (uchar)GetKeyState(VK_SHIFT)));
      if (wp == SIZE_RESTORED && win_is_fullscreen)
        clear_fullscreen();
      else if (wp == SIZE_MAXIMIZED && go_fullscr_on_max) {
        go_fullscr_on_max = false;
        make_fullscreen();
      }
      else if (wp == SIZE_MINIMIZED) {
        win_synctabs(3);
      }

      if (!resizing) {
        trace_resize((" (win_proc (WM_SIZE) -> win_adapt_term_size)\n"));
        // enable font zooming on Shift unless
#ifdef does_not_enable_shift_maximize_initially
        // - triggered by Windows shortcut (with Windows key)
        // - triggered by Ctrl+Shift+F (zoom_token < 0)
        if ((zoom_token >= 0) && !(GetKeyState(VK_LWIN) & 0x80))
          if (zoom_token < 1)  // accept overriding zoom_token 4
            zoom_token = 1;
#else
        // - triggered by Windows shortcut (with Windows key)
        if (!(GetKeyState(VK_LWIN) & 0x80))
          if (zoom_token < 1)  // accept overriding zoom_token 4
            zoom_token = 1;
#endif
        bool ctrl = GetKeyState(VK_CONTROL) & 0x80;
        bool scale_font = (cfg.zoom_font_with_window || zoom_token > 2)
                       && (zoom_token > 0) && (GetKeyState(VK_SHIFT) & 0x80)
                       && !default_size_token
                       // override font zooming to support FancyZones
                       // (#487, microsoft/PowerToys#1050)
                       && !ctrl
                       ;
        //printf("WM_SIZE scale_font %d zoom_token %d\n", scale_font, zoom_token);
        int rows0 = term.rows0, cols0 = term.cols0;
        win_adapt_term_size(false, scale_font);
        if (wp == SIZE_MAXIMIZED) {
          term.rows0 = rows0;
          term.cols0 = cols0;
        }
        if (zoom_token > 0)
          zoom_token = zoom_token >> 1;
        default_size_token = false;
      }

      return 0;
    }

    when WM_EXITSIZEMOVE or WM_CAPTURECHANGED: { // after mouse-drag resizing
      trace_resize(("# WM_EXITSIZEMOVE (resizing %d) VK_SHIFT %02X\n", resizing, (uchar)GetKeyState(VK_SHIFT)));
      bool shift = GetKeyState(VK_SHIFT) & 0x80;

      //printf("WM_EXITSIZEMOVE resizing %d shift %d\n", resizing, shift);
      if (resizing) {
        resizing = false;
        win_destroy_tip();
        trace_resize((" (win_proc (WM_EXITSIZEMOVE) -> win_adapt_term_size)\n"));
        win_adapt_term_size(shift, false);
      }

      win_synctabs(2);
    }

    when WM_MOVE:
      // enable coupled moving of window tabs on Win+Shift moving;
      // (#600#issuecomment-366643426, if SessionGeomSync ≥ 2);
      // avoid mutual repositioning (endless flickering);
      // as an additional condition, position synchronization shall 
      // only be done if the window has the focus; otherwise this 
      // has bad impact when a window is (tried to be) restored 
      // after the window set was minimized; the taskbar icons 
      // would inconsistently be disabled except one, and after closing 
      // windows, remaining ones would not be restored at all anymore, 
      // also the window title sometimes appeared mysteriously corrupted
      //printf("WM_MOVE moving %d focus %d\n", moving, GetFocus() == wnd);
      if (!moving && GetFocus() == wnd)
        win_synctabs(2);
      moving = false;

#define WP ((WINDOWPOS *) lp)

    when WM_WINDOWPOSCHANGING:
      poschanging = true;
      trace_resize(("# WM_WINDOWPOSCHANGING %3X (resizing %d) %d %d @ %d %d\n", WP->flags, resizing, WP->cy, WP->cx, WP->y, WP->x));

    when WM_WINDOWPOSCHANGED: {
      poschanging = false;
      if (disable_poschange)
        // avoid premature Window size adaptation (#649?)
        break;

      trace_resize(("# WM_WINDOWPOSCHANGED %3X (resizing %d) %d %d @ %d %d\n", WP->flags, resizing, WP->cy, WP->cx, WP->y, WP->x));
      if (per_monitor_dpi_aware == DPI_AWAREV1) {
        // not necessary for DPI handling V2
        bool dpi_changed = true;
        if (cfg.handle_dpichanged && pGetDpiForMonitor) {
          HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
          uint x, y;
          pGetDpiForMonitor(mon, 0, &x, &y);  // MDT_EFFECTIVE_DPI
#ifdef debug_dpi
          printf("WM_WINDOWPOSCHANGED %d -> %d (aware %d handle %d)\n", dpi, y, per_monitor_dpi_aware, cfg.handle_dpichanged);
#endif
          if (y != dpi) {
            dpi = y;
          }
          else
            dpi_changed = false;
        }

        if (dpi_changed && cfg.handle_dpichanged) {
          // remaining glitch:
          // start mintty -p @1; move it to other monitor;
          // columns will be less
          //win_init_fonts(cfg.font.size);
          font_cs_reconfig(true);
          win_adapt_term_size(true, false);
        }
      }
    }

    when WM_GETDPISCALEDSIZE: {
      // here we could adjust the RECT passed to WM_DPICHANGED ...
#ifdef debug_dpi
      SIZE * sz = (SIZE *)lp;
      printf("WM_GETDPISCALEDSIZE dpi %d w/h %d/%d\n", (int)wp, (int)sz->cx, (int)sz->cy);
#endif
      return 0;
    }

    when WM_DPICHANGED: {
      if (!cfg.handle_dpichanged) {
#ifdef debug_dpi
        printf("WM_DPICHANGED (unhandled) %d (aware %d handle %d)\n", dpi, per_monitor_dpi_aware, cfg.handle_dpichanged);
#endif
        break;
      }

      if (per_monitor_dpi_aware == DPI_AWAREV2) {
        is_in_dpi_change = true;

        UINT new_dpi = LOWORD(wp);
        LPRECT r = (LPRECT) lp;

#ifdef debug_dpi
        printf("WM_DPICHANGED %d -> %d (handled) (aware %d handle %d) w/h %d/%d\n",
               dpi, new_dpi, per_monitor_dpi_aware, cfg.handle_dpichanged,
               (int)(r->right - r->left), (int)(r->bottom - r->top));
#endif
        dpi = new_dpi;

        int y = term.rows, x = term.cols;
        // set window size
        SetWindowPos(wnd, 0, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        font_cs_reconfig(true);

        // reestablish terminal size
        if (term.rows != y || term.cols != x) {
#ifdef debug_dpi
          printf("term w/h %d/%d -> %d/%d, fixing\n", x, y, term.cols, term.rows);
#endif
          // win_fix_position also clips the window to desktop size
          win_set_chars(y, x);
        }

        is_in_dpi_change = false;
        return 0;
      } else if (per_monitor_dpi_aware == DPI_AWAREV1) {
#ifdef handle_dpi_on_dpichanged
        bool dpi_changed = true;
        if (pGetDpiForMonitor) {
          HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
          uint x, y;
          pGetDpiForMonitor(mon, 0, &x, &y);  // MDT_EFFECTIVE_DPI
#ifdef debug_dpi
          printf("WM_DPICHANGED handled: %d -> %d DPI (aware %d)\n", dpi, y, per_monitor_dpi_aware);
#endif
          if (y != dpi) {
            dpi = y;
          }
          else
            dpi_changed = false;
        }
#ifdef debug_dpi
        else
          printf("WM_DPICHANGED (unavailable)\n");
#endif

        if (dpi_changed) {
          // this RECT is adjusted with respect to the monitor dpi already,
          // so we don't need to consider GetDpiForMonitor
          LPRECT r = (LPRECT) lp;
          // try to stabilize font size roundtrip; 
          // heuristic tweak of window size to compensate for 
          // font scaling rounding errors that would continuously 
          // decrease the window size if moving between monitors repeatedly
          long width = (r->right - r->left) * 20 / 19;
          long height = (r->bottom - r->top) * 20 / 19;
          // set window size
          SetWindowPos(wnd, 0, r->left, r->top, width, height,
                       SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
          int y = term.rows, x = term.cols;
          win_adapt_term_size(false, true);
          //?win_init_fonts(cfg.font.size);
          // try to stabilize terminal size roundtrip
          if (term.rows != y || term.cols != x) {
            // win_fix_position also clips the window to desktop size
            win_set_chars(y, x);
          }
#ifdef debug_dpi
          printf("SM_CXVSCROLL %d\n", GetSystemMetrics(SM_CXVSCROLL));
#endif
          return 0;
        }
        break;
#endif // handle_dpi_on_dpichanged
      }
      break;
    }

#ifdef debug_stylestuff
    when WM_STYLECHANGING: {
      printf("STYLE %08X -> %08X\n", ((STYLESTRUCT *)lp)->styleOld, ((STYLESTRUCT *)lp)->styleNew);
      //return 0;
    }

    when WM_ERASEBKGND:
#endif

    when WM_NCHITTEST: {
      LRESULT result = DefWindowProcW(wnd, message, wp, lp);

      // implement Ctrl+Alt+click to move window
      if (result == HTCLIENT &&
          (GetKeyState(VK_MENU) & 0x80) && (GetKeyState(VK_CONTROL) & 0x80))
        // redirect click target from client area to caption
        return HTCAPTION;
      else
        return result;
    }

    when WM_SETHOTKEY:
#ifdef debug_hook
      show_info(asform("WM_SETHOTKEY %X %02X", wp >> 8, wp & 0xFF));
#endif
      if (wp & 0xFF) {
        // Set up implicit startup hotkey as defined via Windows shortcut
        if (!hotkey)
          win_global_keyboard_hook(true);
        hotkey = wp & 0xFF;
        ushort mods = wp >> 8;
        hotkey_mods = !!(mods & HOTKEYF_SHIFT) * MDK_SHIFT
                    | !!(mods & HOTKEYF_ALT) * MDK_ALT
                    | !!(mods & HOTKEYF_CONTROL) * MDK_CTRL;
      }
      else {
        hotkey = 0;
        win_global_keyboard_hook(false);
      }
  }

 /*
  * Any messages we don't process completely above are passed through to
  * DefWindowProc() for default processing.
  */
  return DefWindowProcW(wnd, message, wp, lp);
}

static LRESULT CALLBACK
hookprockbll(int nCode, WPARAM wParam, LPARAM lParam)
{
  if (term.shortcut_override)
    return CallNextHookEx(0, nCode, wParam, lParam);

  LPKBDLLHOOKSTRUCT kbdll = (LPKBDLLHOOKSTRUCT)lParam;
  uint key = kbdll->vkCode;
#ifdef debug_hook
  printf("hooked ll %d wm %03lX vk %02X sc %d fl %04X ex %04lX\n", 
         nCode, (long)wParam, 
         key, (uint)kbdll->scanCode, (uint)kbdll->flags, (ulong)kbdll->dwExtraInfo);
#endif

  bool is_hooked_hotkey(WPARAM wParam, uint key, mod_keys mods)
  {
#ifdef debug_hook
    show_info(asform("key %02X mods %02X hooked %02X mods %02X", key, mods, hotkey, hotkey_mods));
#endif
    return wParam == WM_KEYDOWN &&
      // hotkey/modifiers could be
      // * derived from invocation shortcut (implemented)
      // * configurable explicitly (not implemented)
      (key == hotkey && mods == hotkey_mods);
  }
  if (is_hooked_hotkey(wParam, key, get_mods())) {
    if (GetFocus() == wnd && IsWindowVisible(wnd)) {
      // this probably makes no sense after IsWindowVisible(wnd):
      ShowWindow(wnd, SW_SHOW);  // in case it was started with -w hide
      // put the window away
      //ShowWindow(wnd, SW_HIDE);
      // rather minimize and keep icon in taskbar (#1035)
      ShowWindow(wnd, SW_MINIMIZE);
    }
    else {
      // These do not work:
      //win_to_top(wnd);
      //win_set_zorder(true);
      // Need to minimize first:
      ShowWindow(wnd, SW_MINIMIZE);
      ShowWindow(wnd, SW_RESTORE);
    }
    // Return to prevent multiple mintty windows from flickering
    // Return 1 to swallow hotkey
    return 1;
  }

  bool hook = false;
#ifdef check_swallow
  // this should be factored out to wininput.c, if ever to be used
  bool is_hooked_key(WPARAM wParam, uint key)
  {
    return wParam == WM_KEYDOWN &&
      (key == VK_LWIN || key == VK_RWIN
       || key == VK_CAPITAL || key == VK_SCROLL || key == VK_NUMLOCK
      )
    ;
  }
  hook = GetFocus() == wnd && is_hooked_key(wParam, key);
#endif
  if (hook) {
    LPARAM lp = 1;
    lp |= (LPARAM)kbdll->flags << 24 | (LPARAM)kbdll->scanCode << 16;
    bool swallow_key = false;
    // here we have another opportunity to set a flag to swallow the key,
    // based on dynamic processing (win_key_down, win_key_up)
    win_proc(wnd, wParam, key, lp);
    // if we have processed the key (e.g. as modifier or user-defined key)
    if (swallow_key) {
      // return non-zero to swallow
      return 1;
    }
  }

  return CallNextHookEx(0, nCode, wParam, lParam);
}

static void
hook_windows(int id, HOOKPROC hookproc, bool global)
{
  kb_hook = SetWindowsHookExW(id, hookproc, 0, global ? 0 : GetCurrentThreadId());
}

static void
win_global_keyboard_hook(bool on)
{
  if (on)
    hook_windows(WH_KEYBOARD_LL, hookprockbll, true);
  else if (kb_hook)
    UnhookWindowsHookEx(kb_hook);
}

bool
win_get_ime(void)
{
  return ImmGetOpenStatus(imc);
}

void
win_set_ime(bool open)
{
  ImmSetOpenStatus(imc, open);
  win_set_ime_open(open);
}


void
report_pos(void)
{
  if (report_geom) {
    int x, y;
    //win_get_pos(&x, &y);  // would not consider maximised/minimised
    WINDOWPLACEMENT placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(wnd, &placement);
    x = placement.rcNormalPosition.left;
    y = placement.rcNormalPosition.top;
    int cols = term.cols;
    int rows = term.rows;
    cols = (placement.rcNormalPosition.right - placement.rcNormalPosition.left - norm_extra_width - 2 * PADDING) / cell_width;
    rows = (placement.rcNormalPosition.bottom - placement.rcNormalPosition.top - norm_extra_height - 2 * PADDING - OFFSET) / cell_height;

    printf("%s", main_argv[0]);
    printf(*report_geom == 'o' ? " -o Columns=%d -o Rows=%d" : " -s %d,%d", cols, rows);
    printf(*report_geom == 'o' ? " -o X=%d -o Y=%d" : " -p %d,%d", x, y);
    char * winstate = 0;
    if (win_is_fullscreen)
      winstate = "full";
    else if (IsZoomed(wnd))
      winstate = "max";
    else if (IsIconic(wnd))
      winstate = "min";
    if (winstate)
      printf(*report_geom == 'o' ? " -o Window=%s" : " -w %s", winstate);
    printf("\n");
  }
}

void
exit_mintty(void)
{
  report_pos();

  // bring next window to top
  if (sync_level()) {
    HWND wnd_other = FindWindowExW(NULL, wnd,
        (LPCWSTR)(uintptr_t)class_atom, NULL);
    if (wnd_other)
      win_to_top(wnd_other);
  }

  // restore ScrollLock LED
  term_set_focus(false, false);

  // could there be a lag until the window is actually destroyed?
  // so we'd have to add a safeguard here...
  SetWindowTextA(wnd, "");
  // indicate "terminating"
  SetWindowLong(wnd, GWL_USERDATA, -1);
  // flush properties cache
  SetWindowPos(wnd, null, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
               | SWP_NOREPOSITION | SWP_NOACTIVATE | SWP_NOCOPYBITS);
  update_tab_titles();

#ifdef activate_other_tab_at_exit
  // tab management: ensure other tab gets on top when exiting; not needed
  if (sync_level())
    win_switch(true, false);
#endif

  exit(0);
}


#if CYGWIN_VERSION_DLL_MAJOR >= 1005
typedef void * * voidrefref;
#else
typedef void * voidrefref;
#define STARTF_TITLEISLINKNAME 0x00000800
#define STARTF_TITLEISAPPID 0x00001000
#endif

static wchar *
get_shortcut_icon_location(wchar * iconfile, bool * wdpresent)
{
  IShellLinkW * shell_link;
  IPersistFile * persist_file;
  HRESULT hres = OleInitialize(NULL);
  if (hres != S_FALSE && hres != S_OK)
    return 0;

  hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IShellLinkW, (voidrefref) &shell_link);
  if (!SUCCEEDED(hres))
    return 0;

  hres = shell_link->lpVtbl->QueryInterface(shell_link, &IID_IPersistFile,
                                            (voidrefref) &persist_file);
  if (!SUCCEEDED(hres)) {
    shell_link->lpVtbl->Release(shell_link);
    return 0;
  }

  /* Load the shortcut.  */
  hres = persist_file->lpVtbl->Load(persist_file, iconfile, STGM_READ);

  wchar * result = 0;

  if (SUCCEEDED(hres)) {
    WCHAR wil[MAX_PATH + 1];
    * wil = 0;
    int index;
    hres = shell_link->lpVtbl->GetIconLocation(shell_link, wil, MAX_PATH, &index);
    if (!SUCCEEDED(hres) || !*wil)
      goto iconex;

    wchar * wicon = wil;

    /* Append ,icon-index if non-zero.  */
    wchar * widx = W("");
    if (index) {
      char idx[22];
      sprintf(idx, ",%d", index);
      widx = cs__mbstowcs(idx);
    }

    /* Resolve leading Windows environment variable component.  */
    wchar * wenv = W("");
    wchar * fin;
    if (wil[0] == '%' && wil[1] && wil[1] != '%' && (fin = wcschr(&wil[2], '%'))) {
      char var[fin - wil];
      char * cop = var;
      wchar * v;
      for (v = &wil[1]; *v != '%'; v++) {
        if (wil[2] == 'y' && *v >= 'a' && *v <= 'z')
          // capitalize %SystemRoot%
          *cop = *v - 'a' + 'A';
        else
          *cop = *v;
        cop++;
      }
      *cop = '\0';
      v ++;
      wicon = v;

      char * val = getenv(var);
      if (val) {
        wenv = cs__mbstowcs(val);
      }
    }

    result = newn(wchar, wcslen(wenv) + wcslen(wicon) + wcslen(widx) + 1);
    wcscpy(result, wenv);
    wcscpy(&result[wcslen(result)], wicon);
    wcscpy(&result[wcslen(result)], widx);
    if (* widx)
      free(widx);
    if (* wenv)
      free(wenv);

    // also retrieve working directory:
    if (wdpresent) {
      hres = shell_link->lpVtbl->GetWorkingDirectory(shell_link, wil, MAX_PATH);
      *wdpresent = SUCCEEDED(hres) && *wil;
    }
#ifdef use_shortcut_description
    // also retrieve shortcut description:
    static wchar * shortcut = 0;
    uint sdlen = 55;
    wchar * sd = newn(wchar, sdlen + 1);
    do {
      // Note: this is the "Comment:" field, not the shortcut name
      hres = shell_link->lpVtbl->GetDescription(shell_link, sd, sdlen);
      if (hres != S_OK)
        break;
      if (wcslen(sd) < sdlen - 1) {
        shortcut = wcsdup(sd);
        break;
      }
      sdlen += 55;
      sd = renewn(sd, sdlen + 1);
    } while (true);
    delete(sd);
#endif
  }
  iconex:

  /* Release the pointer to the IPersistFile interface. */
  persist_file->lpVtbl->Release(persist_file);

  /* Release the pointer to the IShellLink interface. */
  shell_link->lpVtbl->Release(shell_link);

  return result;
}

static wchar *
get_shortcut_appid(wchar * shortcut)
{
#if CYGWIN_VERSION_API_MINOR >= 74
  DWORD win_version = GetVersion();
  win_version = ((win_version & 0xff) << 8) | ((win_version >> 8) & 0xff);
  if (win_version < 0x0601)
    return 0;  // PropertyStore not supported on Windows XP

  HRESULT hres = OleInitialize(NULL);
  if (hres != S_FALSE && hres != S_OK)
    return 0;

  IShellLink * link;
  hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, 
                          &IID_IShellLink, (voidrefref) &link);
  if (!SUCCEEDED(hres))
    return 0;

  wchar * res = 0;

  IPersistFile * file;
  hres = link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (voidrefref) &file);
  if (!SUCCEEDED(hres))
    goto rel1;

  hres = file->lpVtbl->Load(file, (LPCOLESTR)shortcut, STGM_READ | STGM_SHARE_DENY_NONE);
  if (!SUCCEEDED(hres))
    goto rel2;

  IPropertyStore * store;
  hres = link->lpVtbl->QueryInterface(link, &IID_IPropertyStore, (voidrefref) &store);
  if (!SUCCEEDED(hres))
    goto rel3;

  PROPVARIANT pv;
  hres = store->lpVtbl->GetValue(store, &PKEY_AppUserModel_ID, &pv);
  if (!SUCCEEDED(hres))
    goto rel3;

  if (pv.vt == VT_LPWSTR)
    res = wcsdup(pv.pwszVal);

  PropVariantClear(&pv);
rel3:
  store->lpVtbl->Release(store);
rel2:
  file->lpVtbl->Release(file);
rel1:
  link->lpVtbl->Release(link);

  return res;
#else
  (void)shortcut;
  return 0;
#endif
}


#if CYGWIN_VERSION_API_MINOR >= 74

static HKEY
regopen(HKEY key, wstring subkey)
{
  HKEY hk = 0;
  RegOpenKeyW(key, subkey, &hk);
  return hk;
}

static void
regclose(HKEY key)
{
  if (key)
    RegCloseKey(key);
}

static int
getlxssinfo(bool list, wstring wslname, uint * wsl_ver,
            char ** wsl_guid, wstring * wsl_rootfs, wstring * wsl_icon)
{
  static wstring lxsskeyname = W("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Lxss");
  HKEY lxss = regopen(HKEY_CURRENT_USER, lxsskeyname);
  if (!lxss)
    return 1;

#ifdef use_wsl_getdistconf
  typedef enum
  {
    WSL_DISTRIBUTION_FLAGS_NONE = 0,
    //...
  } WSL_DISTRIBUTION_FLAGS;
  HRESULT (WINAPI * pWslGetDistributionConfiguration)
           (PCWSTR name, ULONG *distVersion, ULONG *defaultUID,
            WSL_DISTRIBUTION_FLAGS *,
            PSTR **defaultEnvVars, ULONG *defaultEnvVarCount
           ) =
    // this works only in 64 bit mode
    load_library_func("wslapi.dll", "WslGetDistributionConfiguration");
#endif

  wchar * legacy_icon()
  {
    // "%LOCALAPPDATA%/lxss/bash.ico"
    char * icf = getenv("LOCALAPPDATA");
    if (icf) {
      wchar * icon = cs__mbstowcs(icf);
      icon = renewn(icon, wcslen(icon) + 15);
      wcscat(icon, W("\\lxss\\bash.ico"));
      return icon;
    }
    return 0;
  }

  int getlxssdistinfo(bool list, HKEY lxss, wchar * guid)
  {
    wchar * rootfs;
    wchar * icon = 0;

    wchar * bp = getregstr(lxss, guid, W("BasePath"));
    if (!bp)
      return 3;

    wchar * pn = getregstr(lxss, guid, W("PackageFamilyName"));
    wchar * pfn = 0;
    if (pn) {  // look for installation directory and icon file
      rootfs = newn(wchar, wcslen(bp) + 8);
      wcscpy(rootfs, bp);
      wcscat(rootfs, W("\\rootfs"));
      HKEY appdata = regopen(HKEY_CURRENT_USER, W("Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\SystemAppData"));
      HKEY package = regopen(appdata, pn);
      pfn = getregstr(package, W("Schemas"), W("PackageFullName"));
      regclose(package);
      regclose(appdata);
      // "%ProgramW6432%/WindowsApps/<PackageFullName>/images/icon.ico"
      char * prf = getenv("ProgramW6432");
      if (prf && pfn) {
        icon = cs__mbstowcs(prf);
        icon = renewn(icon, wcslen(icon) + wcslen(pfn) + 30);
        wcscat(icon, W("\\WindowsApps\\"));
        wcscat(icon, pfn);
        wcscat(icon, W("\\images\\icon.ico"));
      }
    }
    else {  // legacy
      rootfs = newn(wchar, wcslen(bp) + 8);
      wcscpy(rootfs, bp);
      wcscat(rootfs, W("\\rootfs"));

      char * rootdir = path_win_w_to_posix(rootfs);
      struct stat fstat_buf;
      if (stat (rootdir, & fstat_buf) == 0 && S_ISDIR (fstat_buf.st_mode)) {
        // non-app or imported deployment
      }
      else {
        // legacy Bash on Windows
        free(rootfs);
        rootfs = wcsdup(bp);
      }
      free(rootdir);

      icon = legacy_icon();
    }

    wchar * name = getregstr(lxss, guid, W("DistributionName"));
#ifdef use_wsl_getdistconf
    // this has currently no benefit, and it does not work in 32-bit cygwin
    if (pWslGetDistributionConfiguration) {
      ULONG ver, uid, varc;
      WSL_DISTRIBUTION_FLAGS flags;
      PSTR * vars;
      if (S_OK == pWslGetDistributionConfiguration(name, &ver, &uid, &flags, &vars, &varc)) {
        for (uint i = 0; i < varc; i++)
          CoTaskMemFree(vars[i]);
        CoTaskMemFree(vars);
        //printf("%d %ls %d uid %d %X\n", (int)res, name, (int)ver, (int)uid, (uint)flags);
      }
    }
#endif

    if (list) {
      printf("WSL distribution name [7m%ls[m\n", name);
      printf("-- guid %ls\n", guid);
      printf("-- flag %u\n", getregval(lxss, guid, W("Flags")));
      printf("-- root %ls\n", rootfs);
      if (pn)
        printf("-- pack %ls\n", pn);
      if (pfn)
        printf("-- full %ls\n", pfn);
      printf("-- icon %ls\n", icon);
    }

    *wsl_icon = icon;
    *wsl_ver = 1 + ((getregval(lxss, guid, W("Flags")) >> 3) & 1);
    *wsl_guid = cs__wcstoutf(guid);
    char * rootdir = path_win_w_to_posix(rootfs);
    struct stat fstat_buf;
    if (stat (rootdir, & fstat_buf) == 0 && S_ISDIR (fstat_buf.st_mode)) {
      *wsl_rootfs = rootfs;
    }
    else if (wslname) {
      free(rootfs);
      rootfs = newn(wchar, wcslen(wslname) + 8);
      wcscpy(rootfs, W("\\\\wsl$\\"));
      wcscat(rootfs, wslname);
      *wsl_rootfs = rootfs;
    }
    free(rootdir);
    return 0;
  }

  if (!list && (!wslname || !*wslname)) {
    wchar * dd = getregstr(HKEY_CURRENT_USER, lxsskeyname, W("DefaultDistribution"));
    int err;
    if (dd) {
      err = getlxssdistinfo(false, lxss, dd);
      free(dd);
    }
    else {  // Legacy "Bash on Windows" installed only, no registry info
#ifdef set_basepath_here
      // "%LOCALAPPDATA%\\lxss"
      char * icf = getenv("LOCALAPPDATA");
      if (icf) {
        wchar * rootfs = cs__mbstowcs(icf);
        rootfs = renewn(rootfs, wcslen(rootfs) + 6);
        wcscat(rootfs, W("\\lxss"));
        *wsl_rootfs = rootfs;
        *wsl_ver = 1;
        *wsl_guid = "";
        *wsl_icon = legacy_icon();
        err = 0;
      }
      else
        err = 7;
#else
      *wsl_ver = 1;
      *wsl_guid = "";
      *wsl_rootfs = W("");  // activate legacy tricks in winclip.c
      *wsl_icon = legacy_icon();
      err = 0;
#endif
    }
    regclose(lxss);
    return err;
  }
  else {
    DWORD nsubkeys = 0;
    DWORD maxlensubkey;
    DWORD ret;
    // prepare enumeration of distributions
    ret = RegQueryInfoKeyW(lxss,
                           NULL, NULL, // class
                           NULL,
                           &nsubkeys, &maxlensubkey, // subkeys
                           NULL,
                           NULL, NULL, NULL, // values
                           NULL, NULL);
    // enumerate the distribution subkeys
    for (uint i = 0; i < nsubkeys; i++) {
      DWORD keylen = maxlensubkey + 2;
      wchar subkey[keylen];
      ret = RegEnumKeyW(lxss, i, subkey, keylen);
      if (ret == ERROR_SUCCESS) {
          wchar * dn = getregstr(lxss, subkey, W("DistributionName"));
          if (list) {
            getlxssdistinfo(true, lxss, subkey);
          }
          else if (0 == wcscmp(dn, wslname)) {
            int err = getlxssdistinfo(false, lxss, subkey);
            regclose(lxss);
            return err;
          }
      }
    }
    regclose(lxss);
    return 9;
  }
}

#ifdef not_used
bool
wexists(wstring fn)
{
  WIN32_FIND_DATAW ffd;
  HANDLE hFind = FindFirstFileW(fn, &ffd);
  bool ok = hFind != INVALID_HANDLE_VALUE;
  FindClose(hFind);
  return ok;
}
#endif

bool
waccess(wstring fn, int amode)
{
  string f = path_win_w_to_posix(fn);
  bool ok = access(f, amode) == 0;
  delete(f);
  return ok;
}

static int
select_WSL(char * wsl)
{
  wslname = cs__mbstowcs(wsl ?: "");
  wstring wsl_icon;
  // set --rootfs implicitly
  int err = getlxssinfo(false, wslname, &wsl_ver, &wsl_guid, &wsl_basepath, &wsl_icon);
  if (!err) {
    // set --title
    if (title_settable)
      set_arg_option("Title", strdup(wsl && *wsl ? wsl : "WSL"));
    // set --icon if WSL specific icon exists
    if (wsl_icon) {
      if (!icon_is_from_shortcut && waccess(wsl_icon, R_OK))
        cfg.icon = wsl_icon;
      else
        delete(wsl_icon);
    }
    // set implicit option --wsl
    support_wsl = true;
    if (cfg.old_locale) {
      // enforce UTF-8 for WSL:
      // also set implicit options -o Locale=C -o Charset=UTF-8
      set_arg_option("Locale", strdup("C"));
      set_arg_option("Charset", strdup("UTF-8"));
    }
    if (0 == wcscmp(cfg.app_id, W("@")))
      // setting an implicit AppID fixes mintty/wsltty#96 but causes #784
      // so an explicit config value derives AppID from wsl distro name
      set_arg_option("AppID", asform("%s.%s", APPNAME, wsl ?: "WSL"));
  }
  else {
    free(wslname);
    wslname = 0;
  }
  return err;
}

#endif


typedef void (* CMDENUMPROC)(wstring label, wstring cmd, wstring icon, int icon_index);

static wstring * jumplist_title = 0;
static wstring * jumplist_cmd = 0;
static wstring * jumplist_icon = 0;
static int * jumplist_ii = 0;
static int jumplist_len = 0;

static void
cmd_enum(wstring label, wstring cmd, wstring icon, int icon_index)
{
  jumplist_title = renewn(jumplist_title, jumplist_len + 1);
  jumplist_cmd = renewn(jumplist_cmd, jumplist_len + 1);
  jumplist_icon = renewn(jumplist_icon, jumplist_len + 1);
  jumplist_ii = renewn(jumplist_ii, jumplist_len + 1);

  jumplist_title[jumplist_len] = label;
  jumplist_cmd[jumplist_len] = cmd;
  jumplist_icon[jumplist_len] = icon;
  jumplist_ii[jumplist_len] = icon_index;
  jumplist_len++;
}

wstring
wslicon(wchar * params)
{
  wstring icon = 0;  // default: no icon
#if CYGWIN_VERSION_API_MINOR >= 74
  wchar * wsl = wcsstr(params, W("--WSL"));
  if (wsl) {
    wsl += 5;
    if (*wsl == '=')
      wsl++;
    else if (*wsl <= ' ')
      ; // SP or NUL: no WSL distro specified
    else
      wsl = 0;
  }
  if (wsl) {
    wchar * sp = wcsstr(wsl, W(" "));
    int len;
    if (sp)
      len = sp - wsl;
    else
      len = wcslen(wsl);
    if (len) {
      wchar * wslname = newn(wchar, len + 1);
      wcsncpy(wslname, wsl, len);
      wslname[len] = 0;
      uint ver;
      char * guid;
      wstring basepath;
      int err = getlxssinfo(false, wslname, &ver, &guid, &basepath, &icon);
      free(wslname);
      if (!err) {
        delete(basepath);
        free(guid);
      }
    }
    if (!icon) {  // no WSL distro specified or failed to find icon
      char * wslico = get_resource_file(W("icon"), W("wsl.ico"), false);
      if (wslico) {
        icon = path_posix_to_win_w(wslico);
        free(wslico);
      }
      else {
        char * lappdata = getenv("LOCALAPPDATA");
        if (lappdata && *lappdata) {
          wslico = asform("%s/wsltty/wsl.ico", lappdata);
          icon = cs__mbstowcs(wslico);
          free(wslico);
        }
      }
    }
  }
#else
  (void)params;
#endif
  return icon;
}

static void
enum_commands(wstring commands, CMDENUMPROC cmdenum)
{
  char * cmds = cs__wcstoutf(commands);
  char * cmdp = cmds;
  char sepch = ';';
  if ((uchar)*cmdp <= (uchar)' ')
    sepch = *cmdp++;

  char * paramp;
  while ((paramp = strchr(cmdp, ':'))) {
    *paramp = '\0';
    paramp++;
    char * sepp = strchr(paramp, sepch);
    if (sepp)
      *sepp = '\0';

    wchar * params = cs__utftowcs(paramp);
    wstring icon = wslicon(params);  // default: 0 (no icon)
    //printf("	task <%s> args <%ls> icon <%ls>\n", cmdp, params, icon);
    cmdenum(_W(cmdp), params, icon, 0);

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
  free(cmds);
}


static void
configure_taskbar(wchar * app_id)
{
  if (*cfg.task_commands) {
    enum_commands(cfg.task_commands, cmd_enum);
    setup_jumplist(app_id, jumplist_len, jumplist_title, jumplist_cmd, jumplist_icon, jumplist_ii);
  }

#if CYGWIN_VERSION_DLL_MAJOR >= 1007
  // initial patch (issue #471) contributed by Johannes Schindelin
  wchar * relaunch_icon = (wchar *) cfg.icon;
  wchar * relaunch_display_name = (wchar *) cfg.app_name;
  wchar * relaunch_command = (wchar *) cfg.app_launch_cmd;

#define dont_debug_properties

  // Set the app ID explicitly, as well as the relaunch command and display name
  if (prevent_pinning || (app_id && *app_id)) {
    HMODULE shell = load_sys_library("shell32.dll");
    HRESULT (WINAPI *pGetPropertyStore)(HWND hwnd, REFIID riid, void **ppv) =
      (void *)GetProcAddress(shell, "SHGetPropertyStoreForWindow");
#ifdef debug_properties
      printf("SHGetPropertyStoreForWindow linked %d\n", !!pGetPropertyStore);
#endif
    if (pGetPropertyStore) {
      IPropertyStore *pps;
      HRESULT hr;
      PROPVARIANT var;

      hr = pGetPropertyStore(wnd, &IID_IPropertyStore, (void **) &pps);
#ifdef debug_properties
      printf("IPropertyStore found %d\n", SUCCEEDED(hr));
#endif
      if (SUCCEEDED(hr)) {
        // doc: https://msdn.microsoft.com/en-us/library/windows/desktop/dd378459%28v=vs.85%29.aspx
        // def: typedef struct tagPROPVARIANT PROPVARIANT: propidl.h
        // def: enum VARENUM (VT_*): wtypes.h
        // def: PKEY_*: propkey.h
        if (relaunch_command && *relaunch_command && store_taskbar_properties) {
#ifdef debug_properties
          printf("AppUserModel_RelaunchCommand=%ls\n", relaunch_command);
#endif
          var.pwszVal = relaunch_command;
          var.vt = VT_LPWSTR;
          pps->lpVtbl->SetValue(pps,
              &PKEY_AppUserModel_RelaunchCommand, &var);
        }
        if (relaunch_display_name && *relaunch_display_name) {
#ifdef debug_properties
          printf("AppUserModel_RelaunchDisplayNameResource=%ls\n", relaunch_display_name);
#endif
          var.pwszVal = relaunch_display_name;
          var.vt = VT_LPWSTR;
          pps->lpVtbl->SetValue(pps,
              &PKEY_AppUserModel_RelaunchDisplayNameResource, &var);
        }
        if (relaunch_icon && *relaunch_icon) {
#ifdef debug_properties
          printf("AppUserModel_RelaunchIconResource=%ls\n", relaunch_icon);
#endif
          var.pwszVal = relaunch_icon;
          var.vt = VT_LPWSTR;
          pps->lpVtbl->SetValue(pps,
              &PKEY_AppUserModel_RelaunchIconResource, &var);
        }
        if (prevent_pinning) {
          var.boolVal = VARIANT_TRUE;
#ifdef debug_properties
          printf("AppUserModel_PreventPinning=%d\n", var.boolVal);
#endif
          var.vt = VT_BOOL;
          // PreventPinning must be set before setting ID
          pps->lpVtbl->SetValue(pps,
              &PKEY_AppUserModel_PreventPinning, &var);
        }
#ifdef set_userpinned
DEFINE_PROPERTYKEY(PKEY_AppUserModel_StartPinOption, 0x9f4c2855,0x9f79,0x4B39,0xa8,0xd0,0xe1,0xd4,0x2d,0xe1,0xd5,0xf3,12);
#define APPUSERMODEL_STARTPINOPTION_USERPINNED 2
#warning needs Windows 8/10 to build...
        {
          var.uintVal = APPUSERMODEL_STARTPINOPTION_USERPINNED;
#ifdef debug_properties
          printf("AppUserModel_StartPinOption=%d\n", var.uintVal);
#endif
          var.vt = VT_UINT;
          pps->lpVtbl->SetValue(pps,
              &PKEY_AppUserModel_StartPinOption, &var);
        }
#endif
        if (app_id && *app_id) {
#ifdef debug_properties
          printf("AppUserModel_ID=%ls\n", app_id);
#endif
          var.pwszVal = app_id;
          var.vt = VT_LPWSTR;  // VT_EMPTY should remove but has no effect
          pps->lpVtbl->SetValue(pps,
              &PKEY_AppUserModel_ID, &var);
        }

        pps->lpVtbl->Commit(pps);
        pps->lpVtbl->Release(pps);
      }
    }
  }
#endif
}


/*
   Expand window group id (AppID or Class) by placeholders.
 */
static wchar *
group_id(wstring id)
{
  if (wcschr(id, '%')) {
    wchar * pc = (wchar *)id;
    int pcn = 0;
    while (*pc)
      if (*pc++ == '%')
        pcn++;
    struct utsname name;
    if (pcn <= 5 && uname(&name) >= 0) {
      char * _ = strchr(name.sysname, '_');
      if (_)
        *_ = 0;
      char * fmt = cs__wcstoutf(id);
      char * icon = cs__wcstoutf(icon_is_from_shortcut ? cfg.icon : W(""));
      char * wsln = cs__wcstoutf(wslname ?: W(""));
      char * ai = asform(fmt,
                         name.sysname,
                         name.release,
                         name.machine,
                         icon,
                         wsln);
      id = cs__utftowcs(ai);
      free(ai);
      free(wsln);
      free(icon);
      free(fmt);
    }
  }
  return (wchar *)id;
}


#define usage __("Usage:")
#define synopsis __("[OPTION]... [ PROGRAM [ARG]... | - ]")
static char help[] =
  //__ help text (output of -H / --help), after initial line ("synopsis")
  __("Start a new terminal session running the specified program or the user's shell.\n"
  "If a dash is given instead of a program, invoke the shell as a login shell.\n"
  "\n"
  "Options:\n"
// 12345678901234567890123456789012345678901234567890123456789012345678901234567890
  "  -c, --config FILE     Load specified config file (cf. -C or -o ThemeFile)\n"
  "  -e, --exec ...        Treat remaining arguments as the command to execute\n"
  "  -h, --hold never|start|error|always  Keep window open after command finishes\n"
  "  -p, --position X,Y    Open window at specified coordinates\n"
  "  -p, --position center|left|right|top|bottom  Open window at special position\n"
  "  -p, --position @N     Open window on monitor N\n"
  "  -s, --size COLS,ROWS  Set screen size in characters (also COLSxROWS)\n"
  "  -s, --size maxwidth|maxheight  Set max screen size in given dimension\n"
  "  -t, --title TITLE     Set window title (default: the invoked command) (cf. -T)\n"
  "  -w, --window normal|min|max|full|hide  Set initial window state\n"
  "  -i, --icon FILE[,IX]  Load window icon from file, optionally with index\n"
  "  -l, --log FILE|-      Log output to file or stdout\n"
  "      --nobidi|--nortl  Disable bidi (right-to-left support)\n"
  "  -o, --option OPT=VAL  Set/Override config file option with given value\n"
  "  -B, --Border frame|void  Use thin/no window border\n"
  "  -R, --Report s|o      Report window position (short/long) after exit\n"
  "      --nopin           Make this instance not pinnable to taskbar\n"
  "  -D, --daemon          Start new instance with Windows shortcut key\n"
  "  -H, --help            Display help and exit\n"
  "  -V, --version         Print version information and exit\n"
  "See manual page for further command line options and configuration.\n"
);

static const char short_opts[] = "+:c:C:eh:i:l:o:p:s:t:T:B:R:uw:HVdD~P:";

enum {
  OPT_FG       = 0x80,
  OPT_BG       = 0x81,
  OPT_CR       = 0x82,
  OPT_SELFG    = 0x83,
  OPT_SELBG    = 0x84,
  OPT_FONT     = 0x85,
  OPT_FS       = 0x86,
  OPT_GEOMETRY = 0x87,
  OPT_EN       = 0x88,
  OPT_LF       = 0x89,
  OPT_SL       = 0x8A,
};

static const struct option
opts[] = {
  {"config",     required_argument, 0, 'c'},
  {"loadconfig", required_argument, 0, 'C'},
  {"configdir",  required_argument, 0, ''},
  {"exec",       no_argument,       0, 'e'},
  {"hold",       required_argument, 0, 'h'},
  {"icon",       required_argument, 0, 'i'},
  {"log",        required_argument, 0, 'l'},
  {"logfile",    required_argument, 0, ''},
  {"utmp",       no_argument,       0, 'u'},
  {"option",     required_argument, 0, 'o'},
  {"position",   required_argument, 0, 'p'},
  {"size",       required_argument, 0, 's'},
  {"title",      required_argument, 0, 't'},
  {"Title",      required_argument, 0, 'T'},
  {"tabbar",     optional_argument, 0, ''},
  {"Border",     required_argument, 0, 'B'},
  {"Report",     required_argument, 0, 'R'},
  {"Reportpos",  required_argument, 0, 'R'},  // compatibility variant
  {"window",     required_argument, 0, 'w'},
  {"class",      required_argument, 0, ''},  // short option not enabled
  {"dir",        required_argument, 0, ''},  // short option not enabled
  {"nobidi",     no_argument,       0, ''},  // short option not enabled
  {"nortl",      no_argument,       0, ''},  // short option not enabled
  {"wsl",        no_argument,       0, ''},  // short option not enabled
#if CYGWIN_VERSION_API_MINOR >= 74
  {"WSL",        optional_argument, 0, ''},  // short option not enabled
  {"WSLmode",    optional_argument, 0, ''},  // short option not enabled
#endif
  {"pcon",       required_argument, 0, 'P'},
  {"rootfs",     required_argument, 0, ''},  // short option not enabled
  {"dir~",       no_argument,       0, '~'},
  {"help",       no_argument,       0, 'H'},
  {"version",    no_argument,       0, 'V'},
  {"nodaemon",   no_argument,       0, 'd'},
  {"daemon",     no_argument,       0, 'D'},
  {"nopin",      no_argument,       0, ''},  // short option not enabled
  {"store-taskbar-properties", no_argument, 0, ''},  // no short option
  {"trace",      required_argument, 0, ''},  // short option not enabled
  // further xterm-style convenience options, all without short option:
  {"fg",         required_argument, 0, OPT_FG},
  {"bg",         required_argument, 0, OPT_BG},
  {"cr",         required_argument, 0, OPT_CR},
  {"selfg",      required_argument, 0, OPT_SELFG},
  {"selbg",      required_argument, 0, OPT_SELBG},
  {"fn",         required_argument, 0, OPT_FONT},
  {"font",       required_argument, 0, OPT_FONT},
  {"fs",         required_argument, 0, OPT_FS},
  {"geometry",   required_argument, 0, OPT_GEOMETRY},
  {"en",         required_argument, 0, OPT_EN},
  {"lf",         required_argument, 0, OPT_LF},
  {"sl",         required_argument, 0, OPT_SL},
  {0, 0, 0, 0}
};

int
main(int argc, char *argv[])
{
  main_argv = argv;
  main_argc = argc;
  mintty_debug = getenv("MINTTY_DEBUG") ?: "";
#ifdef debuglog
  mtlog = fopen("/tmp/mtlog", "a");
  {
    char timbuf [22];
    struct timeval now;
    gettimeofday(& now, 0);
    strftime(timbuf, sizeof (timbuf), "%Y-%m-%d %H:%M:%S", localtime(& now.tv_sec));
    fprintf(mtlog, "[%s.%03d] %s\n", timbuf, (int)now.tv_usec / 1000, argv[0]);
    fflush(mtlog);
  }
#endif
  init_config();
  cs_init();

  // Determine home directory.
  home = getenv("HOME");
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
  // Before Cygwin 1.5, the passwd structure is faked.
  struct passwd *pw = getpwuid(getuid());
#endif
  home = home ? strdup(home) :
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
    (pw && pw->pw_dir && *pw->pw_dir) ? strdup(pw->pw_dir) :
#endif
    asform("/home/%s", getlogin());

  // Set size and position defaults.
  STARTUPINFOW sui;
  GetStartupInfoW(&sui);
  cfg.window = sui.dwFlags & STARTF_USESHOWWINDOW ? sui.wShowWindow : SW_SHOW;
  cfg.x = cfg.y = CW_USEDEFAULT;
  invoked_from_shortcut = sui.dwFlags & STARTF_TITLEISLINKNAME;
  invoked_with_appid = sui.dwFlags & STARTF_TITLEISAPPID;
  // shortcut or AppId would be found in sui.lpTitle
# ifdef debuglog
  fprintf(mtlog, "shortcut %d %ls\n", invoked_from_shortcut, sui.lpTitle);
# endif
  // conclude whether started via Win+R (may be considered to set login mode)
  //invoked_from_win_r = !invoked_from_shortcut & (sui.dwFlags & STARTF_USESHOWWINDOW);
# ifdef debug_startupinfo
  char * sinfo = asform("STARTUPINFO <%s> <%s> %08X %d\n",
        cs__wcstombs(sui.lpDesktop ?: u""), cs__wcstombs(sui.lpTitle ?: u""),
        sui.dwFlags, sui.wShowWindow);
  show_info(sinfo);
# endif

  // Options triggered via wsl*.exe
#if CYGWIN_VERSION_API_MINOR >= 74
  char * exename = *argv;
  const char * exebasename = strrchr(exename, '/');
  if (exebasename)
    exebasename ++;
  else
    exebasename = exename;
  if (0 == strncmp(exebasename, "wsl", 3)) {
    char * exearg = strchr(exebasename, '-');
    if (exearg)
      exearg ++;
    int err = select_WSL(exearg);
    if (err)
      option_error(__("WSL distribution '%s' not found"), exearg ?: _("(Default)"), err);
    else {
      wsl_launch = true;
      wsltty_appx = true;
    }
  }

  char * getlocalappdata(void)
  {
    // get appx-redirected system dir, as investigated by Biswapriyo Nath
#ifndef KF_FLAG_FORCE_APP_DATA_REDIRECTION
#define KF_FLAG_FORCE_APP_DATA_REDIRECTION 0x00080000
#endif
    HMODULE shell = load_sys_library("shell32.dll");
    HRESULT (WINAPI *pSHGetKnownFolderPath)(const GUID*, DWORD, HANDLE, wchar**) =
      (void *)GetProcAddress(shell, "SHGetKnownFolderPath");
    if (!pSHGetKnownFolderPath)
      return 0;
    wchar * wlappdata;
    long hres = pSHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_FORCE_APP_DATA_REDIRECTION, 0, &wlappdata);
    if (hres)
      return 0;
    else
      return path_win_w_to_posix(wlappdata);
  }

  char * lappdata = 0;
  if (wsltty_appx)
    lappdata = getlocalappdata();
#endif

  // Load config files
  // try global config file
  load_config("/etc/minttyrc", true);
#if CYGWIN_VERSION_API_MINOR >= 74
  // try Windows APPX local config location (wsltty.appx#3)
  if (wsltty_appx && lappdata && *lappdata) {
    string rc_file = asform("%s/.minttyrc", lappdata);
    load_config(rc_file, 2);
    delete(rc_file);
  }
#endif
  // try Windows config location (#201)
  char * appdata = getenv("APPDATA");
  if (appdata && *appdata) {
    string rc_file = asform("%s/mintty/config", appdata);
    load_config(rc_file, true);
    delete(rc_file);
  }
  if (!support_wsl) {
    // try XDG config base directory default location (#525)
    string rc_file = asform("%s/.config/mintty/config", home);
    load_config(rc_file, true);
    delete(rc_file);
    // try home config file
    rc_file = asform("%s/.minttyrc", home);
    load_config(rc_file, 2);
    delete(rc_file);
  }

  if (getenv("MINTTY_ICON")) {
    //cfg.icon = strdup(getenv("MINTTY_ICON"));
    cfg.icon = cs__utftowcs(getenv("MINTTY_ICON"));
    icon_is_from_shortcut = true;
    unsetenv("MINTTY_ICON");
  }
  if (getenv("MINTTY_PWD")) {
    // if cloned and then launched from Windows shortcut 
    // (by sanitizing taskbar icon grouping, #784, mintty/wsltty#96) 
    // set proper directory
    chdir(getenv("MINTTY_PWD"));
    trace_dir(asform("MINTTY_PWD: %s", getenv("MINTTY_PWD")));
    unsetenv("MINTTY_PWD");
  }

  bool wdpresent = true;
  if (invoked_from_shortcut && sui.lpTitle) {
    shortcut = wcsdup(sui.lpTitle);
    setenv("MINTTY_SHORTCUT", path_win_w_to_posix(shortcut), true);
    wchar * icon = get_shortcut_icon_location(sui.lpTitle, &wdpresent);
# ifdef debuglog
    fprintf(mtlog, "icon <%ls>\n", icon); fflush(mtlog);
# endif
    if (icon) {
      cfg.icon = icon;
      icon_is_from_shortcut = true;
    }
  }
  else {
    // We should check whether we've inherited a MINTTY_SHORTCUT setting
    // from a previous invocation, and if so we should check whether the
    // referred shortcut actually runs the same binary as we're running.
    // If that's not the case, we should unset MINTTY_SHORTCUT here.
  }

  for (;;) {
    int opt = cfg.short_long_opts
      ? getopt_long_only(argc, argv, short_opts, opts, 0)
      : getopt_long(argc, argv, short_opts, opts, 0);
    if (opt == -1 || opt == 'e')
      break;
    char * longopt = argv[optind - 1];
    char * shortopt = (char[]){'-', optopt, 0};
    switch (opt) {
      when 'c': load_config(optarg, 3);
      when 'C': load_config(optarg, false);
      when '': support_wsl = true;
      when '': wsl_basepath = path_posix_to_win_w(optarg);
#if CYGWIN_VERSION_API_MINOR >= 74
      when '': {
        int err = select_WSL(optarg);
        if (err)
          option_error(__("WSL distribution '%s' not found"), optarg ?: _("(Default)"), err);
        else
          wsl_launch = true;
      }
      when '': {
        int err = select_WSL(optarg);
        if (err)
          option_error(__("WSL distribution '%s' not found"), optarg ?: _("(Default)"), err);
      }
#endif
      when '~':
        start_home = true;
        chdir(home);
        trace_dir(asform("~: %s", home));
      when '': {
        int res = chdir(optarg);
        trace_dir(asform("^D: %s", optarg));
        if (res == 0)
          setenv("PWD", optarg, true);  // avoid softlink resolution
        else {
          if (*optarg == '"' || *optarg == '\'')
            if (optarg[strlen(optarg) - 1] == optarg[0]) {
              // strip off embedding quotes as provided when started 
              // from Windows context menu by registry entry
              char * dir = strdup(&optarg[1]);
              dir[strlen(dir) - 1] = '\0';
              res = chdir(dir);
              trace_dir(asform("^D 2: %s", dir));
              if (res == 0)
                setenv("PWD", optarg, true);  // avoid softlink resolution
              free(dir);
            }
        }
        if (res == 0)
          setenv("CHERE_INVOKING", "mintty", true);
      }
      when '':
        if (config_dir)
          option_error(__("Duplicate option '%s'"), "configdir", 0);
        else {
          config_dir = strdup(optarg);
          string rc_file = asform("%s/config", config_dir);
          load_config(rc_file, 2);
          delete(rc_file);
        }
      when '?':
        option_error(__("Unknown option '%s'"), optopt ? shortopt : longopt, 0);
      when ':':
        option_error(__("Option '%s' requires an argument"),
                     longopt[1] == '-' ? longopt : shortopt, 0);
      when 'h': set_arg_option("Hold", optarg);
      when 'i': set_arg_option("Icon", optarg);
      when 'l': // -l , --log
        set_arg_option("Log", optarg);
        set_arg_option("Logging", strdup("1"));
      when '': // --logfile
        set_arg_option("Log", optarg);
        set_arg_option("Logging", strdup("0"));
      when 'o': parse_arg_option(optarg);
      when 'p':
        if (strcmp(optarg, "center") == 0 || strcmp(optarg, "centre") == 0)
          center = true;
        else if (strcmp(optarg, "right") == 0)
          right = true;
        else if (strcmp(optarg, "bottom") == 0)
          bottom = true;
        else if (strcmp(optarg, "left") == 0)
          left = true;
        else if (strcmp(optarg, "top") == 0)
          top = true;
        else if (sscanf(optarg, "@%i%1s", &monitor, (char[2]){}) == 1)
          ;
        else if (sscanf(optarg, "%i,%i%1s", &cfg.x, &cfg.y, (char[2]){}) == 2)
          ;
        else
          option_error(__("Syntax error in position argument '%s'"), optarg, 0);
      when 's':
        if (strcmp(optarg, "maxwidth") == 0)
          maxwidth = true;
        else if (strcmp(optarg, "maxheight") == 0)
          maxheight = true;
        else if (sscanf(optarg, "%u,%u%1s", &cfg.cols, &cfg.rows, (char[2]){}) == 2)
          ;
        else if (sscanf(optarg, "%ux%u%1s", &cfg.cols, &cfg.rows, (char[2]){}) == 2)
          ;
        else
          option_error(__("Syntax error in size argument '%s'"), optarg, 0);
      when 't': set_arg_option("Title", optarg);
      when 'T':
        set_arg_option("Title", optarg);
        title_settable = false;
      when '':
        set_arg_option("TabBar", strdup("1"));
        set_arg_option("SessionGeomSync", optarg ?: strdup("2"));
      when 'B':
        border_style = strdup(optarg);
      when 'R':
        switch (*optarg) {
          when 's' or 'o':
            report_geom = strdup(optarg);
          when 'm':
            report_moni = true;
          when 'f':
            list_fonts(true);
            exit(0);
#if CYGWIN_VERSION_API_MINOR >= 74
          when 'W': {
            wstring wsl_icon;
            getlxssinfo(true, 0, &wsl_ver, &wsl_guid, &wsl_basepath, &wsl_icon);
            exit(0);
          }
#endif
          when 'p':
            report_child_pid = true;
          when 'P':
            report_winpid = true;
          otherwise:
            option_error(__("Unknown option '%s'"), optarg, 0);
        }
      when 'u': cfg.create_utmp = true;
      when '':
        prevent_pinning = true;
        store_taskbar_properties = true;
      when '': store_taskbar_properties = true;
      when 'w': set_arg_option("Window", optarg);
      when '': set_arg_option("Class", optarg);
      when '': cfg.bidi = 0;
      when 'd':
        cfg.daemonize = false;
      when 'D':
        cfg.daemonize_always = true;
      when 'H': {
        finish_config();  // ensure localized message
        //char * helptext = asform("%s %s %s\n\n%s", _(usage), APPNAME, _(synopsis), _(help));
        char * helptext = strdup(_(usage));
        strappend(helptext, " ");
        strappend(helptext, APPNAME);
        strappend(helptext, " ");
        strappend(helptext, _(synopsis));
        strappend(helptext, "\n\n");
        strappend(helptext, _(help));
        show_info(helptext);
        free(helptext);
        return 0;
      }
      when 'V': {
        finish_config();  // ensure localized message
        //char * vertext =
        //  asform("%s\n%s\n%s\n%s\n", 
        //         VERSION_TEXT, COPYRIGHT, LICENSE_TEXT, _(WARRANTY_TEXT));
        char * vertext = strdup(VERSION_TEXT);
        strappend(vertext, "\n");
        strappend(vertext, COPYRIGHT);
        strappend(vertext, "\n");
        strappend(vertext, LICENSE_TEXT);
        strappend(vertext, "\n");
        strappend(vertext, _(WARRANTY_TEXT));
        strappend(vertext, "\n");
        show_info(vertext);
        free(vertext);
        return 0;
      }
      when OPT_FG:
        set_arg_option("ForegroundColour", optarg);
      when OPT_BG:
        set_arg_option("BackgroundColour", optarg);
      when OPT_CR:
        set_arg_option("CursorColour", optarg);
      when OPT_FONT:
        set_arg_option("Font", optarg);
      when OPT_FS:
        set_arg_option("FontSize", optarg);
      when OPT_LF:
        set_arg_option("Log", optarg);
      when OPT_SELFG:
        set_arg_option("HighlightForegroundColour", optarg);
      when OPT_SELBG:
        set_arg_option("HighlightBackgroundColour", optarg);
      when OPT_SL:
        set_arg_option("ScrollbackLines", optarg);
      when OPT_EN: {
#if HAS_LOCALES
        char * loc = setlocale(LC_CTYPE, 0);
        if (loc) {
          loc = strdup(loc);
          char * dot = strchr(loc, '.');
          if (dot)
            *dot = 0;
          set_arg_option("Locale", loc);
          free(loc);
        }
        else
          set_arg_option("Locale", "C");
#else
        set_arg_option("Locale", "C");
#endif
        set_arg_option("Charset", optarg);
      }
      when OPT_GEOMETRY: {  // geometry
        char * oa = optarg;
        int n;

        if (sscanf(oa, "%ux%u", &n, &n) == 2)
          if (sscanf(oa, "%ux%u%n", &cfg.cols, &cfg.rows, &n) == 2)
            oa += n;

        char pmx[2];
        char pmy[2];
        char dum[22];
        if (sscanf(oa, "%1[-+]%21[0-9]%1[-+]%21[0-9]", pmx, dum, pmy, dum) == 4)
          if (sscanf(oa, "%1[-+]%u%1[-+]%u%n", pmx, &cfg.x, pmy, &cfg.y, &n) == 4) {
            if (*pmx == '-') {
              cfg.x = - cfg.x;
              right = true;
            }
            if (*pmy == '-') {
              cfg.y = - cfg.y;
              bottom = true;
            }
            oa += n;
          }

        if (sscanf(oa, "@%i%n", &monitor, &n) == 1)
          oa += n;

        if (*oa)
          option_error(__("Syntax error in geometry argument '%s'"), optarg, 0);
      }
      when '': {
        int tfd = open(optarg, O_WRONLY | O_CREAT | O_APPEND | O_NOCTTY, 0600);
        close(1);
        dup(tfd);
        close(tfd);
      }
      when 'P':
        set_arg_option("ConPTY", optarg);
    }
  }

  copy_config("main after -o", &file_cfg, &cfg);
  if (*cfg.colour_scheme)
    load_scheme(cfg.colour_scheme);
  else if (*cfg.theme_file)
    load_theme(cfg.theme_file);

  if (!wdpresent) {  // shortcut start directory is empty
    WCHAR cd[MAX_PATH + 1];
    WCHAR wd[MAX_PATH + 1];
    GetCurrentDirectoryW(MAX_PATH, cd);		// C:\WINDOWS\System32 ?
    GetSystemDirectoryW(wd, MAX_PATH);		// C:\WINDOWS\system32
    //GetSystemWindowsDirectoryW(wd, MAX_PATH);	// C:\WINDOWS
    int l = wcslen(wd);
#if CYGWIN_VERSION_API_MINOR < 206
#define wcsncasecmp wcsncmp
#endif
    if (0 == wcsncasecmp(cd, wd, l)) {
      // current directory is within Windows system directory
      // and shortcut start directory is empty
      if (support_wsl) {
        chdir(getenv("LOCALAPPDATA"));
        chdir("Temp");
      }
      else
        chdir(home);
    }
  }

  finish_config();

  int term_rows = cfg.rows;
  int term_cols = cfg.cols;
  if (getenv("MINTTY_ROWS")) {
    term_rows = atoi(getenv("MINTTY_ROWS"));
    if (term_rows < 1)
      term_rows = cfg.rows;
    unsetenv("MINTTY_ROWS");
  }
  if (getenv("MINTTY_COLS")) {
    term_cols = atoi(getenv("MINTTY_COLS"));
    if (term_cols < 1)
      term_cols = cfg.cols;
    unsetenv("MINTTY_COLS");
  }
  if (getenv("MINTTY_MONITOR")) {
    monitor = atoi(getenv("MINTTY_MONITOR"));
    unsetenv("MINTTY_MONITOR");
  }
  int run_max = 0;
  if (getenv("MINTTY_MAXIMIZE")) {
    run_max = atoi(getenv("MINTTY_MAXIMIZE"));
    unsetenv("MINTTY_MAXIMIZE");
  }
  if (getenv("MINTTY_TABBAR")) {
    cfg.tabbar = max(cfg.tabbar, atoi(getenv("MINTTY_TABBAR")));
  }

  // if started from console, try to detach from caller's terminal (~daemonizing)
  // in order to not suppress signals
  // (indicated by isatty if linked with -mwindows as ttyname() is null)
  bool daemonize = cfg.daemonize && !isatty(0);
  // disable daemonizing if started from desktop
  if (invoked_from_shortcut)
    daemonize = false;
  // disable daemonizing if started from ConEmu
  if (getenv("ConEmuPID"))
    daemonize = false;
  if (cfg.daemonize_always)
    daemonize = true;
  if (daemonize) {  // detach from parent process and terminal
    pid_t pid = fork();
    if (pid < 0)
      print_error(_("Mintty could not detach from caller, starting anyway"));
    if (pid > 0)
      exit(0);  // exit parent process

    setsid();  // detach child process
  }

  load_dwm_funcs();  // must be called after the fork() above!

  load_dpi_funcs();
  per_monitor_dpi_aware = set_per_monitor_dpi_aware();
#ifdef debug_dpi
  printf("per_monitor_dpi_aware %d\n", per_monitor_dpi_aware);
#endif

#define dont_debug_wsl
#define wslbridge2

  // Work out what to execute.
  argv += optind;
  if (wsl_guid && wsl_launch) {
    argc -= optind;
#ifdef wslbridge2
# ifndef __x86_64__
    argc += 2;  // -V 1/2
# endif
    cmd = "/bin/wslbridge2";
    char * cmd0 = "-wslbridge2";
#else
    cmd = "/bin/wslbridge";
    char * cmd0 = "-wslbridge";
#endif
    bool login_dash = false;
    if (*argv && !strcmp(*argv, "-") && !argv[1]) {
      login_dash = true;
      argv++;
      //argc--;
      //argc++; // for "-l"
    }
#ifdef wslbridge2
    argc += start_home;
#endif
    argc += 10;  // -e parameters

    char ** new_argv = newn(char *, argc + 8 + start_home + (wsltty_appx ? 2 : 0));
    char ** pargv = new_argv;
    if (login_dash) {
      *pargv++ = cmd0;
#ifdef wslbridge_supports_l
#warning redundant option wslbridge -l not needed
      *pargv++ = "-l";
#endif
    }
    else
      *pargv++ = cmd;
#ifdef wslbridge2
# ifndef __x86_64__
    *pargv++ = "-V";
    if (wsl_ver > 1)
      *pargv++ = "2";
    else
      *pargv++ = "1";
# endif
#endif
    if (*wsl_guid) {
#ifdef wslbridge2
      if (*wslname) {
        *pargv++ = "-d";
        *pargv++ = cs__wcstombs(wslname);
      }
#else
      *pargv++ = "--distro-guid";
      *pargv++ = wsl_guid;
#endif
    }
#ifdef wslbridge_t
    *pargv++ = "-t";
#endif
    *pargv++ = "-e";
    *pargv++ = "TERM";
    *pargv++ = "-e";
    *pargv++ = "APPDATA";
    if (!cfg.old_locale) {
      *pargv++ = "-e";
      *pargv++ = "LANG";
      *pargv++ = "-e";
      *pargv++ = "LC_CTYPE";
      *pargv++ = "-e";
      *pargv++ = "LC_ALL";
    }
    if (start_home) {
#ifdef wslbridge2
      *pargv++ = "--wsldir";
      *pargv++ = "~";
#else
      *pargv++ = "-C~";
#endif
    }

#if CYGWIN_VERSION_API_MINOR >= 74
    // provide wslbridge-backend in a reachable place for invocation
    bool copyfile(char * fn, char * tn, bool overwrite)
    {
# ifdef copyfile_posix
      int f = open(fn, O_BINARY | O_RDONLY);
      if (!f)
        return false;
      int t = open(tn, O_CREAT | O_WRONLY | O_BINARY |
                   (overwrite ? O_TRUNC : O_EXCL), 0755);
      if (!t) {
        close(f);
        return false;
      }

      char buf[1024];
      int len;
      bool res = true;
      while ((len = read(t, buf, sizeof buf)) > 0)
        if (write(t, buf, len) < 0) {
          res = false;
          break;
        }
      close(f);
      close(t);
      return res;
# else
      wchar * src = path_posix_to_win_w(fn);
      wchar * dst = path_posix_to_win_w(tn);
      bool ok = CopyFileW(src, dst, !overwrite);
      free(dst);
      free(src);
      return ok;
# endif
    }

    if (wsltty_appx && lappdata && *lappdata) {
#ifdef wslbridge2
      char * wslbridge_backend = asform("%s/wslbridge2-backend", lappdata);
      char * bin_backend = "/bin/wslbridge2-backend";
      bool ok = copyfile(bin_backend, wslbridge_backend, true);
#else
      char * wslbridge_backend = asform("%s/wslbridge-backend", lappdata);
      bool ok = copyfile("/bin/wslbridge-backend", wslbridge_backend, true);
#endif
      (void)ok;

      *pargv++ = "--backend";
      *pargv++ = wslbridge_backend;
      // don't free(wslbridge_backend);
    }
#endif

    while (*argv)
      *pargv++ = *argv++;
    *pargv = 0;
    argv = new_argv;
#ifdef debug_wsl
    while (*new_argv)
      printf("<%s>\n", *new_argv++);
#endif

    // prevent HOME from being propagated back to Windows applications 
    // if called from WSL (mintty/wsltty#76)
    unsetenv("HOME");
  }
  else if (*argv && (argv[1] || strcmp(*argv, "-")))  // argv is a command
    cmd = *argv;
  else {  // argv is empty or only "-"
    // Look up the user's shell.
    cmd = getenv("SHELL");
    cmd = cmd ? strdup(cmd) :
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
      (pw && pw->pw_shell && *pw->pw_shell) ? strdup(pw->pw_shell) :
#endif
      "/bin/sh";

    // Determine the program name argument.
    char *slash = strrchr(cmd, '/');
    char *arg0 = slash ? slash + 1 : cmd;

    // Prepend '-' if a login shell was requested.
    if (*argv || (invoked_from_shortcut && cfg.login_from_shortcut))
      arg0 = asform("-%s", arg0);

    // Create new argument array.
    argv = newn(char *, 2);
    argv[0] = arg0;
    argv[1] = 0;
  }

  // Load icon if specified.
  HICON large_icon = 0, small_icon = 0;
  if (*cfg.icon) {
    //string icon_file = strdup(cfg.icon);
    // could use path_win_w_to_posix(cfg.icon) to avoid the locale trick below
    string icon_file = cs__wcstoutf(cfg.icon);
    uint icon_index = 0;
    char *comma = strrchr(icon_file, ',');
    if (comma) {
      char *start = comma + 1, *end;
      icon_index = strtoul(start, &end, 0);
      if (start != end && !*end)
        *comma = 0;
      else
        icon_index = 0;
    }
    SetLastError(0);
#if HAS_LOCALES
    char * valid_locale = setlocale(LC_CTYPE, 0);
    if (valid_locale) {
      valid_locale = strdup(valid_locale);
      setlocale(LC_CTYPE, "C.UTF-8");
# ifdef __CYGWIN__
#  if CYGWIN_VERSION_API_MINOR >= 222
      cygwin_internal(CW_INT_SETLOCALE);  // fix internal locale
#  endif
# endif
    }
#endif
    wchar *win_icon_file = path_posix_to_win_w(icon_file);
#if HAS_LOCALES
    if (valid_locale) {
      setlocale(LC_CTYPE, valid_locale);
# ifdef __CYGWIN__
#  if CYGWIN_VERSION_API_MINOR >= 222
      cygwin_internal(CW_INT_SETLOCALE);  // fix internal locale
#  endif
# endif
      free(valid_locale);
    }
#endif
    if (win_icon_file) {
      ExtractIconExW(win_icon_file, icon_index, &large_icon, &small_icon, 1);
      free(win_icon_file);
    }
    if (!large_icon) {
      small_icon = 0;
      uint err = GetLastError();
      if (err) {
        int wmlen = 1024;  // size of heap-allocated array
        wchar winmsg[wmlen];  // constant and < 1273 or 1705 => issue #530
        //wchar * winmsg = newn(wchar, wmlen);  // free below!
        FormatMessageW(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
          0, err, 0, winmsg, wmlen, 0
        );
        show_iconwarn(winmsg);
      }
      else
        show_iconwarn(null);
    }
    delete(icon_file);
  }

  // Expand AppID placeholders
  wchar * app_id = 0;
  if (invoked_from_shortcut && sui.lpTitle)
    app_id = get_shortcut_appid(sui.lpTitle);
  if (!app_id)
    app_id = group_id(cfg.app_id);

  // Set the AppID if specified and the required function is available.
  if (*app_id && wcscmp(app_id, W("@")) != 0) {
    HMODULE shell = load_sys_library("shell32.dll");
    HRESULT (WINAPI *pSetAppID)(PCWSTR) =
      (void *)GetProcAddress(shell, "SetCurrentProcessExplicitAppUserModelID");

    if (pSetAppID)
      pSetAppID(app_id);
  }

  inst = GetModuleHandle(NULL);

  // Window class name.
  wstring wclass = W(APPNAME);
  if (*cfg.class)
    wclass = group_id(cfg.class);
#ifdef prevent_grouping_hidden_tabs
  // should an explicitly hidden window not be grouped with a "class" of tabs?
  if (!cfg.window)
    wclass = cs__utftowcs(asform("%d", getpid()));
#endif

  // Put child command line into window title if we haven't got one already.
  wstring wtitle = cfg.title;
  if (!*wtitle) {
    size_t len;
    char *argz;
    argz_create(argv, &argz, &len);
    argz_stringify(argz, len, ' ');
    char * title = argz;
    size_t size = cs_mbstowcs(0, title, 0) + 1;
    if (size) {
      wchar *buf = newn(wchar, size);
      cs_mbstowcs(buf, title, size);
      wtitle = buf;
    }
    else {
      print_error(_("Using default title due to invalid characters in program name"));
      wtitle = W(APPNAME);
    }
  }

  // The window class.
  class_atom = RegisterClassExW(&(WNDCLASSEXW){
    .cbSize = sizeof(WNDCLASSEXW),
    .style = 0,
    .lpfnWndProc = win_proc,
    .cbClsExtra = 0,
    .cbWndExtra = 0,
    .hInstance = inst,
    .hIcon = large_icon ?: LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON)),
    .hIconSm = small_icon,
    .hCursor = LoadCursor(null, IDC_IBEAM),
    .hbrBackground = null,
    .lpszMenuName = null,
    .lpszClassName = wclass,
  });


  // Provide temporary fonts
static int dynfonts = 0;
  void add_font(wchar * fn)
  {
    int n = AddFontResourceExW(fn, FR_PRIVATE, 0);
    if (n)
      dynfonts += n;
    else
      printf("Failed to add font %ls\n", fn);
  }
  handle_file_resources(W("fonts/*"), add_font);
  //printf("Added %d fonts\n", dynfonts);

  // Initialise the fonts, thus also determining their width and height.
  if (per_monitor_dpi_aware && pGetDpiForMonitor) {
    // avoid double win_init_fonts
  }
  else {
    // win_init_fonts here as before
    win_init_fonts(cfg.font.size);
  }

  // Reconfigure the charset module now that arguments have been converted,
  // the locale/charset settings have been loaded, and the font width has
  // been determined.
  cs_reconfig();

  // Determine window sizes.
  win_prepare_tabbar();
  win_adjust_borders(cell_width * term_cols, cell_height * term_rows);

  // Having x == CW_USEDEFAULT but not y still triggers default positioning,
  // whereas y == CW_USEDEFAULT but not x results in an invisible window,
  // so to avoid the latter,
  // require both x and y to be set for custom positioning.
  if (cfg.y == (int)CW_USEDEFAULT)
    cfg.x = CW_USEDEFAULT;

  int x = cfg.x;
  int y = cfg.y;

#define dont_debug_position
#ifdef debug_position
#define printpos(tag, x, y, mon)	printf("%s %d %d (%d %d %d %d)\n", tag, x, y, (int)mon.left, (int)mon.top, (int)mon.right, (int)mon.bottom);
#else
#define printpos(tag, x, y, mon)
#endif

  // Dark mode support, prior to window creation
  if (pSetPreferredAppMode) {
    pSetPreferredAppMode(1); /* AllowDark */
  }

  // Create initial window.
  term.show_scrollbar = cfg.scrollbar;  // hotfix #597
  wnd = CreateWindowExW(cfg.scrollbar < 0 ? WS_EX_LEFTSCROLLBAR : 0,
                        wclass, wtitle,
                        window_style | (cfg.scrollbar ? WS_VSCROLL : 0),
                        x, y, width, height,
                        null, null, inst, null);
  trace_winsize("createwindow");

  // Dark mode support
  win_dark_mode(wnd);

  // Workaround for failing title parameter:
  if (pEnableNonClientDpiScaling)
    SetWindowTextW(wnd, wtitle);

  // Adapt window position (and maybe size) to special parameters
  // also select monitor if requested
  if (center || right || bottom || left || top || maxwidth || maxheight
      || monitor > 0
     ) {
    MONITORINFO mi;
    get_my_monitor_info(&mi);
    RECT ar = mi.rcWork;
    printpos("cre", x, y, ar);

    if (monitor > 0) {
      MONITORINFO monmi;
      get_monitor_info(monitor, &monmi);
      RECT monar = monmi.rcWork;

      if (x == (int)CW_USEDEFAULT) {
        // Shift and scale assigned default position to selected monitor.
        win_get_pos(&x, &y);
        printpos("def", x, y, ar);
        x = monar.left + (x - ar.left) * (monar.right - monar.left) / (ar.right - ar.left);
        y = monar.top + (y - ar.top) * (monar.bottom - monar.top) / (ar.bottom - ar.top);
      }
      else {
        // Shift selected position to selected monitor.
        x += monar.left - ar.left;
        y += monar.top - ar.top;
      }

      ar = monar;
      printpos("mon", x, y, ar);
    }

    if (cfg.x == (int)CW_USEDEFAULT) {
      if (monitor == 0)
        win_get_pos(&x, &y);
      if (left || right)
        cfg.x = 0;
      if (top || bottom)
        cfg.y = 0;
        printpos("fix", x, y, ar);
    }

    if (left)
      x = ar.left + cfg.x;
    else if (right)
      x = ar.right - cfg.x - width;
    else if (center)
      x = (ar.left + ar.right - width) / 2;
    if (top)
      y = ar.top + cfg.y;
    else if (bottom)
      y = ar.bottom - cfg.y - height;
    else if (center)
      y = (ar.top + ar.bottom - height) / 2;
      printpos("pos", x, y, ar);

    if (maxwidth) {
      x = ar.left;
      width = ar.right - ar.left;
    }
    if (maxheight) {
      y = ar.top;
      height = ar.bottom - ar.top;
    }
#ifdef debug_resize
    if (maxwidth || maxheight)
      printf("max w/h %d %d\n", width, height);
#endif
    printpos("fin", x, y, ar);

    // set window size
    SetWindowPos(wnd, NULL, x, y, width, height,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    trace_winsize("-p");
  }

  if (per_monitor_dpi_aware) {
    if (cfg.x != (int)CW_USEDEFAULT) {
      // The first SetWindowPos actually set x and y
      // set window size
      SetWindowPos(wnd, NULL, x, y, width, height,
                   SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
      // Then, we are placed the windows on the correct monitor and we can
      // now interpret width/height in correct DPI.
      SetWindowPos(wnd, NULL, x, y, width, height,
                   SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
    // retrieve initial monitor DPI
    if (pGetDpiForMonitor) {
      HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
      uint x;
      pGetDpiForMonitor(mon, 0, &x, &dpi);  // MDT_EFFECTIVE_DPI
#ifdef debug_dpi
      uint ang, raw;
      pGetDpiForMonitor(mon, 1, &x, &ang);  // MDT_ANGULAR_DPI
      pGetDpiForMonitor(mon, 2, &x, &raw);  // MDT_RAW_DPI
      printf("initial dpi eff %d ang %d raw %d\n", dpi, ang, raw);
      print_system_metrics(dpi, "initial");
#endif
      // recalculate effective font size and adjust window
      /* Note: it would avoid some problems to consider the DPI 
         earlier and create the window at its proper size right away
         but there are some cyclic dependencies among CreateWindow, 
         monitor selection and the respective DPI to be considered,
         so we have to adjust here.
      */
      /* Note: this used to be guarded by
         //if (dpi != 96)
         until 3.5.0
         but the previous initial call to win_init_fonts above 
         is now skipped (if per_monitor_dpi_aware...) to avoid its 
         double invocation, so we need to initialise fonts here always.
      */
      {
        font_cs_reconfig(true);  // calls win_init_fonts(cfg.font.size);
        win_prepare_tabbar();
        trace_winsize("dpi > font_cs_reconfig");
        if (maxwidth || maxheight) {
          // changed terminal size not yet recorded, 
          // but window size hopefully adjusted already
          if (border_style) {
            // workaround for caption-less window exceeding borders (#733)
            RECT wr;
            GetWindowRect(wnd, &wr);
            int w = wr.right - wr.left;
            int h = wr.bottom - wr.top;
            MONITORINFO mi;
            get_my_monitor_info(&mi);
            RECT ar = mi.rcWork;
            if (maxwidth && ar.right - ar.left < w)
              w = ar.right - ar.left;
            if (maxheight && ar.bottom - ar.top < h)
              h = ar.bottom - ar.top;
            SetWindowPos(wnd, null, 0, 0, w, h,
                         SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER
                         | SWP_NOACTIVATE);
          }
        }
        else {
          // consider preset size (term_)
          win_set_chars(term_rows ?: cfg.rows, term_cols ?: cfg.cols);
          trace_winsize("dpi > win_set_chars");
        }
      }
    }
  }
  disable_poschange = false;

  if (border_style) {
    LONG style = GetWindowLong(wnd, GWL_STYLE);
    if (strcmp(border_style, "void") == 0) {
      style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    }
    else {
      style &= ~(WS_CAPTION | WS_BORDER);
    }
    SetWindowLong(wnd, GWL_STYLE, style);
    SetWindowPos(wnd, null, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED
                 | SWP_NOACTIVATE);
    trace_winsize("border_style");
  }

  if (cfg.tabbar && !getenv("MINTTY_DX") && !getenv("MINTTY_DY")) {
    HWND wnd_other = FindWindowExW(NULL, wnd,
        (LPCWSTR)(uintptr_t)class_atom, NULL);
    if (wnd_other && FindWindowExA(wnd_other, NULL, TABBARCLASS, NULL)) {
      if (IsZoomed(wnd_other)) {
        if ((GetWindowLong(wnd_other, GWL_STYLE) & WS_THICKFRAME) == 0) {
          setenvi("MINTTY_DX", 0);
          setenvi("MINTTY_DY", 0);
        }
        else {
          run_max = 1;
        }
      }
      else {
        RECT r;
        GetWindowRect(wnd_other, &r);
        setenvi("MINTTY_X", r.left);
        setenvi("MINTTY_Y", r.top);
        setenvi("MINTTY_DX", r.right - r.left);
        setenvi("MINTTY_DY", r.bottom - r.top);
      }
    }
  }

  {
    // INT16 to handle multi-monitor negative coordinates properly
    INT16 sx = 0, sy = 0, sdx = 1, sdy = 1;
    short si = 0;
    if (getenv("MINTTY_X")) {
      sx = atoi(getenv("MINTTY_X"));
      unsetenv("MINTTY_X");
      si++;
    }
    if (getenv("MINTTY_Y")) {
      sy = atoi(getenv("MINTTY_Y"));
      unsetenv("MINTTY_Y");
      si++;
    }
    if (getenv("MINTTY_DX")) {
      sdx = atoi(getenv("MINTTY_DX"));
      unsetenv("MINTTY_DX");
      si++;
    }
    if (getenv("MINTTY_DY")) {
      sdy = atoi(getenv("MINTTY_DY"));
      unsetenv("MINTTY_DY");
      si++;
    }
    if (sync_level()) {
#ifdef debug_tabs
      printf("[%8p] launched %d,%d %d,%d\n", wnd, sx, sy, sdx, sdy);
#endif
      if (si >= 2 && !sdx && !sdy) {
        win_maximise(2);
      }
      else if (si == 4) {
        // set window size
        SetWindowPos(wnd, null, sx, sy, sdx, sdy,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      trace_winsize("launch");
    }
  }

  configure_taskbar(app_id);

  // The input method context.
  imc = ImmGetContext(wnd);

  // Correct autoplacement, which likes to put part of the window under the
  // taskbar when the window size approaches the work area size.
  if (cfg.x == (int)CW_USEDEFAULT) {
    win_fix_position();
    trace_winsize("fix_pos");
  }

  // Initialise the terminal.
  term_reset(true);
  term.show_scrollbar = !!cfg.scrollbar;
  term_resize(term_rows, term_cols);

  // Initialise the scroll bar.
  SetScrollInfo(
    wnd, SB_VERT,
    &(SCROLLINFO){
      .cbSize = sizeof(SCROLLINFO),
      .fMask = SIF_ALL | SIF_DISABLENOSCROLL,
      .nMin = 0, .nMax = term_rows - 1,
      .nPage = term_rows, .nPos = 0,
    },
    false
  );

  // Set up an empty caret bitmap. We're painting the cursor manually.
  caretbm = CreateBitmap(1, cell_height, 1, 1, newn(short, cell_height));
  CreateCaret(wnd, caretbm, 0, 0);

  // Initialise various other stuff.
  win_init_cursors();
  win_init_menus();
  win_update_transparency(cfg.transparency, cfg.opaque_when_focused);

#ifdef debug_display_monitors_mockup
#define report_moni true
#endif
  if (report_moni) {
    int x, y;
    int n = search_monitors(&x, &y, 0, true, 0);
    printf("%d monitors,      smallest width,height %4d,%4d\n", n, x, y);
#ifndef debug_display_monitors_mockup
    exit(0);
#endif
  }

  // Determine how to show the window.
  go_fullscr_on_max = (cfg.window == -1);
  default_size_token = true;  // prevent font zooming (#708)
  int show_cmd = (go_fullscr_on_max || run_max) ? SW_SHOWMAXIMIZED : cfg.window;
  show_cmd = win_fix_taskbar_max(show_cmd);
  // if (run_max == 2) win_maximise(2); // do that later to reduce flickering

  // Ensure -w full to cover taskbar also with -B void (~#1114)
  //printf("win %d go_full %d run %d show %d\n", cfg.window, go_fullscr_on_max, run_max, show_cmd);
  if (go_fullscr_on_max)
    run_max = 2; // ensure fullscreen is full screen

  // Scale to background image aspect ratio if requested
  win_get_pixels(&ini_height, &ini_width, false);
  if (*cfg.background == '%')
    scale_to_image_ratio();

  // Adjust ConPTY support if requested
  if (cfg.conpty_support != (uchar)-1) {
    char * env = 0;
#ifdef __MSYS__
    env = "MSYS";
#else
#ifdef __CYGWIN__
    env = "CYGWIN";
#endif
#endif
    if (env) {
      char * val = cfg.conpty_support ? "enable_pcon" : "disable_pcon";
      val = asform("%s %s", getenv(env) ?: "", val);
      //printf("%d %s=%s\n", cfg.conpty_support, env, val);
      setenv(env, val, true);
    }
  }

  // Set up clipboard notifications.
  HRESULT (WINAPI * pAddClipboardFormatListener)(HWND) =
    load_library_func("user32.dll", "AddClipboardFormatListener");
  if (pAddClipboardFormatListener) {
    if (cfg.external_hotkeys < 4)
      // send WM_CLIPBOARDUPDATE
      pAddClipboardFormatListener(wnd);
  }

  // Grab the focus into the window.
  /* Do this before even showing the window in order to evade the 
     focus delay enforced by child_create() (#1113).
     (This makes the comment below obsolete but let's keep it just in case.)
  */
  SetFocus(wnd);

  // Create child process.
  /* We could move this below SetFocus() or win_init_drop_target() 
     in order to further reduce the delay until window display (#1113) 
     but at a cost:
     - the window flickers white before displaying its background if this 
       is moved below ShowWindow()
     - child terminal size would get wrong with -w max or -w full
  */
  child_create(
    argv,
    &(struct winsize){term_rows, term_cols, term_cols * cell_width, term_rows * cell_height}
  );

  // Finally show the window.
  ShowWindow(wnd, show_cmd);
  // Cloning fullscreen window
  if (run_max == 2)
    win_maximise(2);

  // Set up tabbar
  if (cfg.tabbar) {
    win_open_tabbar();
  }

  // Initialise drag-and-drop into window.
  win_init_drop_target();

  // Save the non-maximised window size
  term.rows0 = term_rows;
  term.cols0 = term_cols;

#ifdef use_init_position
  if (cfg.tabbar)
    // support tabbar; however, the purpose of this handling is unclear
    win_init_position();
  else
    win_synctabs(4);
#else
  win_synctabs(4);
#endif

  update_tab_titles();

#ifdef always_hook_keyboard
  // Install keyboard hook if we configure an explicit startup hotkey...
  // not implemented
  if (hotkey_configured ...)
    win_global_keyboard_hook(true);
#endif

  if (report_winpid) {
    DWORD wpid = -1;
    DWORD parent = GetWindowThreadProcessId(wnd, &wpid);
    (void)parent;
    printf("%d %d\n", getpid(), (int)wpid);
    fflush(stdout);
  }

#ifdef do_check_unhide_tab_via_enumwindows
  void check_unhide_tab(void)
  {
    bool all_hidden = true;

    BOOL CALLBACK
    wnd_enum_proc(HWND curr_wnd, LPARAM unused(lp))
    {
      WINDOWINFO curr_wnd_info;
      curr_wnd_info.cbSize = sizeof(WINDOWINFO);
      GetWindowInfo(curr_wnd, &curr_wnd_info);
      if (class_atom == curr_wnd_info.atomWindowType) {
        bool layered = GetWindowLong(curr_wnd, GWL_EXSTYLE) & WS_EX_LAYERED;
        BYTE b;
        GetLayeredWindowAttributes(curr_wnd, 0, &b, 0);
        bool hidden = layered && !b;
        if (!hidden) {
          all_hidden = false;
          return false;
        }
      }
      return true;
    }

    if (GetFocus() != wnd && manage_tab_hiding()) {
      EnumWindows(wnd_enum_proc, 0);
      if (all_hidden) {
        //printf("[%p] win_update_transparency %d %d\n", wnd, cfg.transparency, cfg.opaque_when_focused);
        win_update_transparency(cfg.transparency, cfg.opaque_when_focused);
      }
    }
  }
#else
  void check_unhide_or_clear_tab(void)
  {
    bool all_hidden = true;
    bool vanished = false;

    for (int w = 0; w < ntabinfo; w++) {
      HWND curr_wnd = tabinfo[w].wnd;

      WINDOWINFO curr_wnd_info;
      curr_wnd_info.cbSize = sizeof(WINDOWINFO);
      if (!GetWindowInfo(curr_wnd, &curr_wnd_info)) {
        vanished = true;
        if (!manage_tab_hiding())
          break;
      }
      else if (manage_tab_hiding() && class_atom == curr_wnd_info.atomWindowType) {
        bool layered = GetWindowLong(curr_wnd, GWL_EXSTYLE) & WS_EX_LAYERED;
        BYTE b;
        GetLayeredWindowAttributes(curr_wnd, 0, &b, 0);
        bool hidden = layered && !b;
        //printf("[%p] layered %d attr %d hidden %d\n", wnd, layered, b, hidden);
        if (!hidden) {
          all_hidden = false;
          // don't break; continue to check for vanished
        }
      }
    }

#ifdef monitor_focussed_tab_on_top
    // tab management: ensure focussed tab on top
    if (manage_tab_hiding() && term.has_focus && !poschanging) {
      win_set_tab_focus('G');
      return;
    }
#endif

    //printf("check all_hidden %d vanished %d\n", all_hidden, vanished);
    if (manage_tab_hiding() && all_hidden)
      win_update_transparency(cfg.transparency, cfg.opaque_when_focused);
    if (vanished)
      update_tab_titles();
  }
#endif

#ifdef debug_hidden_tabs
  void check_hidden_tabs(void)
  {
    BOOL CALLBACK
    wnd_enum_proc(HWND curr_wnd, LPARAM unused(lp))
    {
        WINDOWINFO curr_wnd_info;
        curr_wnd_info.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo(curr_wnd, &curr_wnd_info);
        if (class_atom == curr_wnd_info.atomWindowType) {
          bool vis1 = GetWindowLong(curr_wnd, GWL_STYLE) & WS_VISIBLE;
          bool vis2 = curr_wnd_info.dwStyle & WS_VISIBLE;
          bool layered = GetWindowLong(curr_wnd, GWL_EXSTYLE) & WS_EX_LAYERED;
          BYTE b;
          GetLayeredWindowAttributes(curr_wnd, 0, &b, 0);
          bool hidden = layered && !b;
          printf("[%p] enum %p focus %d icon %d vis %d/%d/%d lay %d α %d hidden %d\n", wnd, curr_wnd, 
                 GetFocus() == curr_wnd, IsIconic(curr_wnd), 
                 IsWindowVisible(curr_wnd), vis1, vis2, layered, b, hidden);
        }
      return true;
    }
    EnumWindows(wnd_enum_proc, 0);

    win_set_timer(check_hidden_tabs, 999);
  }
  if (manage_tab_hiding())
    win_set_timer(check_hidden_tabs, 999);
#endif

  is_init = true;
  // tab management: secure transparency appearance by hiding other tabs
  win_set_tab_focus('I');  // hide other tabs

  // Message loop.
  for (;;) {
    MSG msg;
    while (PeekMessage(&msg, null, 0, 0, PM_REMOVE)) {
      // stale tab management; does this have performance impact?
      // should we do it only every n-the time, or timer-driven?
#ifdef do_check_unhide_tab_via_enumwindows
      check_unhide_tab();
#else
      check_unhide_or_clear_tab();
#endif

      if (msg.message == WM_QUIT)
        return msg.wParam;
      if (!IsDialogMessage(config_wnd, &msg))
        // msg has not been processed by IsDialogMessage
        DispatchMessage(&msg);
    }
    child_proc();
  }
}
