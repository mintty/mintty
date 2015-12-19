// win.c (part of mintty)
// Copyright 2008-13 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#define dont_debug_resize

#include "winpriv.h"
#include "winsearch.h"

#include "term.h"
#include "appinfo.h"
#include "child.h"
#include "charset.h"

#include <locale.h>
#include <getopt.h>
#include <pwd.h>
#include <shellapi.h>
#include <math.h>

#include <sys/cygwin.h>

#if CYGWIN_VERSION_DLL_MAJOR >= 1007
#include <propsys.h>
#include <propkey.h>
#endif


#define dont_debuglog
#ifdef debuglog
  FILE * mtlog = 0;
#endif


HINSTANCE inst;
HWND wnd;
HIMC imc;

static char **main_argv;
static int main_argc;
static ATOM class_atom;
static bool invoked_from_shortcut = false;
static bool invoked_with_appid = false;

static int extra_width, extra_height, norm_extra_width, norm_extra_height;

// State
bool win_is_fullscreen;
static bool go_fullscr_on_max;
static bool resizing;
static int zoom_token = 0;  // for heuristic handling of Shift zoom (#467, #476)

// Options
static bool title_settable = true;
static string border_style = 0;
static string report_geom = 0;
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
static int last_dpi = 0;

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

static HRESULT (WINAPI * pDwmIsCompositionEnabled)(BOOL *) = 0;
static HRESULT (WINAPI * pDwmExtendFrameIntoClientArea)(HWND, const MARGINS *) = 0;

// Helper for loading a system library. Using LoadLibrary() directly is insecure
// because Windows might be searching the current working directory first.
static HMODULE
load_sys_library(string name)
{
  char path[MAX_PATH];
  uint len = GetSystemDirectory(path, MAX_PATH);
  if (len && len + strlen(name) + 1 < MAX_PATH) {
    path[len] = '\\';
    strcpy(&path[len + 1], name);
    return LoadLibrary(path);
  }
  else
    return 0;
}

static void
load_dwm_funcs(void)
{
  HMODULE dwm = load_sys_library("dwmapi.dll");
  if (dwm) {
    pDwmIsCompositionEnabled =
      (void *)GetProcAddress(dwm, "DwmIsCompositionEnabled");
    pDwmExtendFrameIntoClientArea =
      (void *)GetProcAddress(dwm, "DwmExtendFrameIntoClientArea");
  }
}

#define dont_debug_dpi

static bool per_monitor_dpi_aware = false;

#define WM_DPICHANGED 0x02E0
const int Process_System_DPI_Aware = 1;
const int Process_Per_Monitor_DPI_Aware  = 2;
const int MDT_Effective_DPI = 0;
static HRESULT (WINAPI * pGetProcessDpiAwareness)(HANDLE hprocess, int * value) = 0;
static HRESULT (WINAPI * pSetProcessDpiAwareness)(int value) = 0;
static HRESULT (WINAPI * pGetDpiForMonitor)(HMONITOR hmonitor, int dpiType, UINT * dpiX, UINT * dpiY);

static void
load_shcore_funcs(void)
{
  HMODULE shc = load_sys_library("shcore.dll");
#ifdef debug_dpi
  printf("load_shcore_funcs shc %d\n", !!shc);
#endif
  if (shc) {
    pGetProcessDpiAwareness =
      (void *)GetProcAddress(shc, "GetProcessDpiAwareness");
    pSetProcessDpiAwareness =
      (void *)GetProcAddress(shc, "SetProcessDpiAwareness");
    pGetDpiForMonitor =
      (void *)GetProcAddress(shc, "GetDpiForMonitor");
#ifdef debug_dpi
      printf("SetProcessDpiAwareness %d GetProcessDpiAwareness %d\n", !!pSetProcessDpiAwareness, !!pGetProcessDpiAwareness);
#endif
  }
}

static bool
set_per_monitor_dpi_aware()
{
  if (pSetProcessDpiAwareness && pGetProcessDpiAwareness) {
    HRESULT hr = pSetProcessDpiAwareness(Process_Per_Monitor_DPI_Aware);
    // E_ACCESSDENIED:
    // The DPI awareness is already set, either by calling this API previously
    // or through the application (.exe) manifest.
    if (hr != E_ACCESSDENIED && !SUCCEEDED(hr))
      pSetProcessDpiAwareness(Process_System_DPI_Aware);

    int awareness = 0;
    return SUCCEEDED(pGetProcessDpiAwareness(NULL, &awareness)) &&
      awareness == Process_Per_Monitor_DPI_Aware;
  }
  return false;
}

void
win_set_timer(void (*cb)(void), uint ticks)
{ SetTimer(wnd, (UINT_PTR)cb, ticks, null); }

void
win_set_title(char *title)
{
  if (title_settable) {
    wchar wtitle[strlen(title) + 1];
    if (cs_mbstowcs(wtitle, title, lengthof(wtitle)) >= 0)
      SetWindowTextW(wnd, wtitle);
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

void
win_prefix_title(const char * prefix)
{
  int len = GetWindowTextLengthW(wnd);
  wchar ptitle[strlen(prefix) + len + 1];
  int plen = cs_mbstowcs(ptitle, prefix, lengthof(ptitle));
  wchar * title = & ptitle[plen];
  len = GetWindowTextW(wnd, title, len + 1);
  SetWindowTextW(wnd, ptitle);
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
    delete(title);
    titles[titles_i] = 0;
  }
}

/*
 *  Switch to next or previous application window in z-order
 */

static HWND first_wnd, last_wnd;

static BOOL CALLBACK
wnd_enum_proc(HWND curr_wnd, LPARAM unused(lp)) {
  if (curr_wnd != wnd && !IsIconic(curr_wnd)) {
    WINDOWINFO curr_wnd_info;
    curr_wnd_info.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(curr_wnd, &curr_wnd_info);
    if (class_atom == curr_wnd_info.atomWindowType) {
      first_wnd = first_wnd ?: curr_wnd;
      last_wnd = curr_wnd;
    }
  }
  return true;
}

void
win_switch(bool back, bool alternate)
{
  first_wnd = 0, last_wnd = 0;
  EnumWindows(wnd_enum_proc, 0);
  if (first_wnd) {
    if (back)
      first_wnd = last_wnd;
    else
      SetWindowPos(wnd, last_wnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE
                       | (alternate ? SWP_NOZORDER : SWP_NOREPOSITION));
    BringWindowToTop(first_wnd);
  }
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

  BOOL CALLBACK
  monitor_enum (HMONITOR hMonitor, HDC hdcMonitor, LPRECT monp, LPARAM dwData)
  {
    (void)hdcMonitor, (void)monp, (void)dwData;

    GetMonitorInfo(hMonitor, mip);

    return --moni > 0;
  }

  EnumDisplayMonitors(0, 0, monitor_enum, 0);
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
 */
int
search_monitors(int * minx, int * miny, HMONITOR lookup_mon, bool get_primary, MONITORINFO *mip)
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

  int moni = 0;
  int moni_found = 0;
  * minx = 0;
  * miny = 0;
  HMONITOR refmon = 0;

  BOOL CALLBACK
  monitor_enum(HMONITOR hMonitor, HDC hdcMonitor, LPRECT monp, LPARAM dwData)
  {
    (void)hdcMonitor, (void)monp, (void)dwData;

    moni ++;
    if (hMonitor == lookup_mon) {
      // looking for index of specific monitor
      moni_found = moni;
      return FALSE;
    }

    MONITORINFO mi;
    mi.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(hMonitor, &mi);

    if (get_primary && (mi.dwFlags & MONITORINFOF_PRIMARY)) {
      moni_found = moni;  // fallback to be overridden by monitor found later
      refmon = hMonitor;
    }

    // determining smallest monitor width and height
    RECT fr = mi.rcMonitor;
    if (*minx == 0 || *minx > fr.right - fr.left)
      *minx = fr.right - fr.left;
    if (*miny == 0 || *miny > fr.bottom - fr.top)
      *miny = fr.bottom - fr.top;

#ifdef debug_display_monitors
    if (!lookup_mon)
      printf("Monitor %d: %d,%d...%d,%d %s\n", moni, fr.left, fr.top, fr.right, fr.bottom, mi.dwFlags & MONITORINFOF_PRIMARY ? "primary" : "");
#endif

    return TRUE;
  }

  EnumDisplayMonitors(0, 0, monitor_enum, 0);
  if (lookup_mon) {
    return moni_found;
  }
  else {
    if (!refmon)
      refmon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    mip->cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(refmon, mip);
    return moni;  // number of monitors
  }
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
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
  SetWindowPos(wnd, top ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE);
}

bool
win_is_iconic(void)
{
  return IsIconic(wnd);
}

void
win_get_pos(int *xp, int *yp)
{
  RECT r;
  GetWindowRect(wnd, &r);
  *xp = r.left;
  *yp = r.top;
}

void
win_get_pixels(int *height_p, int *width_p)
{
  RECT r;
  GetWindowRect(wnd, &r);
  *height_p = r.bottom - r.top;
  *width_p = r.right - r.left;
}

void
win_get_screen_chars(int *rows_p, int *cols_p)
{
  MONITORINFO mi;
  get_my_monitor_info(&mi);
  RECT fr = mi.rcMonitor;
  *rows_p = (fr.bottom - fr.top) / font_height;
  *cols_p = (fr.right - fr.left) / font_width;
}

void
win_set_pixels(int height, int width)
{
  trace_resize(("--- win_set_pixels %d %d\n", height, width));
  SetWindowPos(wnd, null, 0, 0,
               width + 2 * PADDING + extra_width,
               height + 2 * PADDING + extra_height,
               SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
}

bool
win_is_glass_available(void)
{
  BOOL result = false;
  if (pDwmIsCompositionEnabled)
    pDwmIsCompositionEnabled(&result);
  return result;
}

static void
update_glass(void)
{
  if (pDwmExtendFrameIntoClientArea) {
    bool enabled =
      cfg.transparency == TR_GLASS && !win_is_fullscreen &&
      !(cfg.opaque_when_focused && term.has_focus);
    pDwmExtendFrameIntoClientArea(wnd, &(MARGINS){enabled ? -1 : 0, 0, 0, 0});
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
  update_glass();

 /* Resize ourselves to exactly cover the nearest monitor. */
  MONITORINFO mi;
  get_my_monitor_info(&mi);
  RECT fr = mi.rcMonitor;
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
  update_glass();

 /* Reinstate the window furniture. */
  LONG style = GetWindowLong(wnd, GWL_STYLE);
  if (border_style) {
    if (strcmp (border_style, "void") != 0) {
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
  int term_x, term_y, term_height, term_width;
  win_get_pixels(&term_height, &term_width);
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

  SetWindowPos(wnd, null, term_x, term_y, term_width, term_height,
               SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOZORDER);
}

static void
win_fix_position(void)
{
  RECT wr;
  GetWindowRect(wnd, &wr);
  MONITORINFO mi;
  get_my_monitor_info(&mi);
  RECT ar = mi.rcWork;

  // Correct edges. Top and left win if the window is too big.
  wr.left -= max(0, wr.right - ar.right);
  wr.top -= max(0, wr.bottom - ar.bottom);
  wr.left = max(wr.left, ar.left);
  wr.top = max(wr.top, ar.top);

  SetWindowPos(wnd, 0, wr.left, wr.top, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void
win_set_chars(int rows, int cols)
{
  trace_resize(("--- win_set_chars %d×%d\n", rows, cols));
  win_set_pixels(rows * font_height, cols * font_width);
  win_fix_position();
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

/*
 * Bell.
 */
void
win_bell(void)
{
  if (cfg.bell_sound || cfg.bell_type) {
    if (cfg.bell_freq)
      Beep(cfg.bell_freq, cfg.bell_len);
    else {
      // 0  MB_OK               Default Beep
      // 1  MB_ICONSTOP         Critical Stop
      // 2  MB_ICONQUESTION     Question
      // 3  MB_ICONEXCLAMATION  Exclamation
      // 4  MB_ICONASTERISK     Asterisk
      // ?  0xFFFFFFFF          Simple Beep
      MessageBeep((cfg.bell_type - 1) * 16);
    }
  }
  if (cfg.bell_taskbar && !term.has_focus)
    flash_taskbar(true);
}

void
win_invalidate_all(void)
{
  InvalidateRect(wnd, null, true);
}

void
win_adapt_term_size(bool sync_size_with_font, bool scale_font_with_size)
{
  trace_resize(("--- win_adapt_term_size full %d Zoomed %d\n", win_is_fullscreen, IsZoomed(wnd)));
  if (IsIconic(wnd))
    return;

  if (sync_size_with_font && !win_is_fullscreen) {
    win_set_chars(term.rows, term.cols);
    //win_fix_position();
    win_invalidate_all();
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

  if (scale_font_with_size && term.cols != 0 && term.rows != 0) {
    // calc preliminary size (without font scaling), as below
    // should use term_height rather than rows; calc and store in term_resize
    int cols0 = max(1, term_width / font_width);
    int rows0 = max(1, term_height / font_height);

    // rows0/term.rows gives a rough scaling factor for font_height
    // cols0/term.cols gives a rough scaling factor for font_width
    // font_height, font_width give a rough scaling indication for font_size
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

    if (font_size1 != font_size)
      win_set_font_size(font_size1, false);
  }

  if (win_search_visible()) {
    term_height -= SEARCHBAR_HEIGHT;
  }

  int cols = max(1, term_width / font_width);
  int rows = max(1, term_height / font_height);
  if (rows != term.rows || cols != term.cols) {
    term_resize(rows, cols);
    struct winsize ws = {rows, cols, cols * font_width, rows * font_height};
    child_resize(&ws);
  }
  win_invalidate_all();

  win_update_search();
  term_schedule_search_update();
  win_schedule_update();
}

/*
 * Maximise or restore the window in response to a server-side request.
 * Argument value of 2 means go fullscreen.
 */
void
win_maximise(int max)
{
  if (max == -2) // toggle full screen
    max = win_is_fullscreen ? 0 : 2;
  if (IsZoomed(wnd)) {
    if (!max)
      ShowWindow(wnd, SW_RESTORE);
    else if (max == 2 && !win_is_fullscreen)
      make_fullscreen();
  }
  else if (max) {
    if (max == 2)
      go_fullscr_on_max = true;
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

static void
update_transparency(void)
{
  int trans = cfg.transparency;
  if (trans == TR_GLASS)
    trans = 0;
  LONG style = GetWindowLong(wnd, GWL_EXSTYLE);
  style = trans ? style | WS_EX_LAYERED : style & ~WS_EX_LAYERED;
  SetWindowLong(wnd, GWL_EXSTYLE, style);
  if (trans) {
    if (cfg.opaque_when_focused && term.has_focus)
      trans = 0;
    SetLayeredWindowAttributes(wnd, 0, 255 - (uchar)trans, LWA_ALPHA);
  }

  update_glass();
}

void
win_update_scrollbar(void)
{
  int scrollbar = term.show_scrollbar ? cfg.scrollbar : 0;
  LONG style = GetWindowLong(wnd, GWL_STYLE);
  SetWindowLong(wnd, GWL_STYLE,
                scrollbar ? style | WS_VSCROLL : style & ~WS_VSCROLL);
  LONG exstyle = GetWindowLong(wnd, GWL_EXSTYLE);
  SetWindowLong(wnd, GWL_EXSTYLE,
                scrollbar < 0 ? exstyle | WS_EX_LEFTSCROLLBAR
                              : exstyle & ~WS_EX_LEFTSCROLLBAR);
  SetWindowPos(wnd, null, 0, 0, 0, 0,
               SWP_NOACTIVATE | SWP_NOMOVE |
               SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void
win_reconfig(void)
{
  trace_resize(("--- win_reconfig\n"));
 /* Pass new config data to the terminal */
  term_reconfig();

  bool font_changed =
    strcmp(new_cfg.font.name, cfg.font.name) ||
    new_cfg.font.size != cfg.font.size ||
    new_cfg.font.isbold != cfg.font.isbold ||
    new_cfg.bold_as_font != cfg.bold_as_font ||
    new_cfg.bold_as_colour != cfg.bold_as_colour ||
    new_cfg.font_smoothing != cfg.font_smoothing;

  if (new_cfg.fg_colour != cfg.fg_colour)
    win_set_colour(FG_COLOUR_I, new_cfg.fg_colour);

  if (new_cfg.bg_colour != cfg.bg_colour)
    win_set_colour(BG_COLOUR_I, new_cfg.bg_colour);

  if (new_cfg.cursor_colour != cfg.cursor_colour)
    win_set_colour(CURSOR_COLOUR_I, new_cfg.cursor_colour);

  /* Copy the new config and refresh everything */
  copy_config(&cfg, &new_cfg);
  if (font_changed) {
    win_init_fonts(cfg.font.size);
    trace_resize((" (win_reconfig -> win_adapt_term_size)\n"));
    win_adapt_term_size(true, false);
  }
  win_update_scrollbar();
  update_transparency();
  win_update_mouse();

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

static bool
confirm_exit(void)
{
  if (!child_is_parent())
    return true;

  /* retrieve list of child processes */
  char * pscmd = "/bin/procps -o pid,ruser=USER -o comm -t %s 2> /dev/null || /bin/ps -ef";
  char * tty = child_tty();
  if (strrchr (tty, '/'))
    tty = strrchr (tty, '/') + 1;
  char cmd[strlen(pscmd) + strlen(tty) + 1];
  sprintf (cmd, pscmd, tty, tty);
  FILE * procps = popen (cmd, "r");
  char * msg_pre = "Processes are running in session:\n";
  char * msg_post = "Close anyway?";
  char * msg = malloc (strlen (msg_pre) + 1);
  strcpy (msg, msg_pre);
  if (procps) {
    char line[999];  // use very long input despite narrow msg box
                     // to avoid high risk of clipping within UTF-8 
                     // and failing the wide character transformation
    bool first = true;
    bool filter_tty = false;
    while (fgets(line, sizeof line, procps)) {
      line[strcspn(line, "\r\n")] = 0;  /* trim newline */
      if (first || !filter_tty || strstr (line, tty))  // should check column position too...
      {
        if (first) {
          if (strstr (line, "TTY")) {
            filter_tty = true;
          }
          first = false;
        }
        msg = realloc (msg, strlen (msg) + strlen (line) + 2);
        strcat (msg, line);
        strcat (msg, "\n");
      }
    }
    pclose(procps);
  }
  msg = realloc (msg, strlen (msg) + strlen (msg_post) + 1);
  strcat (msg, msg_post);

  size_t size = cs_mbstowcs(0, msg, 0) + 1;
  int ret;
  if (size) {
    wchar msgw[size];
    cs_mbstowcs(msgw, msg, size);
    wchar appn[strlen(APPNAME) + 1];
    cs_mbstowcs(appn, APPNAME, sizeof appn);
    ret =
      MessageBoxW(
        wnd, msgw,
        appn, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2
      );
  }
  else {
    ret =
      MessageBox(
        wnd, msg,
        APPNAME, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2
      );
  }

  // Treat failure to show the dialog as confirmation.
  return !ret || ret == IDOK;
}

#define dont_debug_windows_messages
#define dont_debug_windows_mouse_messages

static LRESULT CALLBACK
win_proc(HWND wnd, UINT message, WPARAM wp, LPARAM lp)
{
#ifdef debug_windows_messages
static struct {
  uint wm_;
  char * wm_name;
} wm_names[] = {
#include "wm_names.t"
};
  char * wm_name = "?";
  for (uint i = 0; i < lengthof(wm_names); i++)
    if (message == wm_names[i].wm_) {
      wm_name = wm_names[i].wm_name;
      break;
    }
  if ((message != WM_KEYDOWN || !(lp & 0x40000000))
      && message != WM_TIMER && message != WM_NCHITTEST
#ifndef debug_windows_mouse_messages
      && message != WM_SETCURSOR
      && message != WM_MOUSEMOVE && message != WM_NCMOUSEMOVE
#endif
     )
    printf("[%d] win_proc %04X %s (%04X %08X)\n", (int)time(0), message, wm_name, (unsigned)wp, (unsigned)lp);
#endif
  switch (message) {
    when WM_TIMER: {
      KillTimer(wnd, wp);
      void_fn cb = (void_fn)wp;
      cb();
      return 0;
    }
    when WM_CLOSE:
      if (!cfg.confirm_exit || confirm_exit())
        child_kill((GetKeyState(VK_SHIFT) & 0x80) != 0);
      return 0;
    when WM_COMMAND or WM_SYSCOMMAND:
      switch (wp & ~0xF) {  /* low 4 bits reserved to Windows */
        when IDM_OPEN: term_open();
        when IDM_COPY: term_copy();
        when IDM_PASTE: win_paste();
        when IDM_SELALL: term_select_all(); win_update();
        when IDM_RESET: term_reset(); win_update();
        when IDM_DEFSIZE:
          default_size();
        when IDM_DEFSIZE_ZOOM:
          default_size();
#ifdef doesnotwork_after_shift_drag
          if (GetKeyState(VK_SHIFT) & 0x80) {
            win_set_font_size(cfg.font.size, false);
            default_size();
          }
#endif
        when IDM_FULLSCREEN or IDM_FULLSCREEN_ZOOM:
          if ((wp & ~0xF) == IDM_FULLSCREEN_ZOOM)
            zoom_token = 4;  // override cfg.zoom_font_with_window == 0
          else
            zoom_token = -4;
          win_maximise(win_is_fullscreen ? 0 : 2);

          term_schedule_search_update();
          win_update_search();
        when IDM_SEARCH: win_open_search();
        when IDM_FLIPSCREEN: term_flip_screen();
        when IDM_OPTIONS: win_open_config();
        when IDM_NEW: child_fork(main_argc, main_argv, 0);
        when IDM_NEW_MONI: child_fork(main_argc, main_argv, (int)lp - ' ');
        when IDM_COPYTITLE: win_copy_title();
      }
    when WM_VSCROLL:
      switch (LOWORD(wp)) {
        when SB_BOTTOM:   term_scroll(-1, 0);
        when SB_TOP:      term_scroll(+1, 0);
        when SB_LINEDOWN: term_scroll(0, +1);
        when SB_LINEUP:   term_scroll(0, -1);
        when SB_PAGEDOWN: term_scroll(0, +max(1, term.rows - 1));
        when SB_PAGEUP:   term_scroll(0, -max(1, term.rows - 1));
        when SB_THUMBPOSITION or SB_THUMBTRACK: {
          SCROLLINFO info;
          info.cbSize = sizeof(SCROLLINFO);
          info.fMask = SIF_TRACKPOS;
          GetScrollInfo(wnd, SB_VERT, &info);
          term_scroll(1, info.nTrackPos);
        }
      }
    when WM_MOUSEMOVE: win_mouse_move(false, lp);
    when WM_NCMOUSEMOVE: win_mouse_move(true, lp);
    when WM_MOUSEWHEEL: win_mouse_wheel(wp, lp);
    when WM_LBUTTONDOWN: win_mouse_click(MBT_LEFT, lp);
    when WM_RBUTTONDOWN: win_mouse_click(MBT_RIGHT, lp);
    when WM_MBUTTONDOWN: win_mouse_click(MBT_MIDDLE, lp);
    when WM_LBUTTONUP: win_mouse_release(MBT_LEFT, lp);
    when WM_RBUTTONUP: win_mouse_release(MBT_RIGHT, lp);
    when WM_MBUTTONUP: win_mouse_release(MBT_MIDDLE, lp);
    when WM_KEYDOWN or WM_SYSKEYDOWN:
      if (win_key_down(wp, lp))
        return 0;
    when WM_KEYUP or WM_SYSKEYUP:
      if (win_key_up(wp, lp))
        return 0;
    when WM_CHAR or WM_SYSCHAR:
      child_sendw(&(wchar){wp}, 1);
      return 0;
    when WM_INPUTLANGCHANGEREQUEST:  // catch Shift-Control-0
      if ((GetKeyState(VK_SHIFT) & 0x80) && (GetKeyState(VK_CONTROL) & 0x80))
        if (win_key_down('0', 0x000B0001))
          return 0;
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
          child_sendw((wchar *)buf, len / 2);
        }
        return 1;
      }
    when WM_PAINT:
      win_paint();
      return 0;
    when WM_ACTIVATE:
      if((wp & 0xF) != WA_INACTIVE) {
        flash_taskbar(false);  /* stop */
        term_set_focus(true, true);
      } else {
        term_set_focus(false, true);
      }
      update_transparency();
    when WM_SETFOCUS:
      trace_resize(("# WM_SETFOCUS VK_SHIFT %02X\n", GetKeyState(VK_SHIFT)));
      term_set_focus(true, false);
      CreateCaret(wnd, caretbm, 0, 0);
      //flash_taskbar(false);  /* stop; not needed when leaving search bar */
      win_update();
      ShowCaret(wnd);
      zoom_token = -4;
    when WM_KILLFOCUS:
      win_show_mouse();
      term_set_focus(false, false);
      DestroyCaret();
      win_update();
    when WM_MOVING:
      trace_resize(("# WM_MOVING VK_SHIFT %02X\n", GetKeyState(VK_SHIFT)));
      zoom_token = -4;
    when WM_ENTERSIZEMOVE:
      trace_resize(("# WM_ENTERSIZEMOVE VK_SHIFT %02X\n", GetKeyState(VK_SHIFT)));
      resizing = true;
    when WM_EXITSIZEMOVE or WM_CAPTURECHANGED:  // after mouse-drag resizing
      trace_resize(("# WM_EXITSIZEMOVE (resizing %d) VK_SHIFT %02X\n", resizing, GetKeyState(VK_SHIFT)));
      bool shift = GetKeyState(VK_SHIFT) & 0x80;
      if (resizing) {
        resizing = false;
        win_destroy_tip();
        trace_resize((" (win_proc (WM_EXITSIZEMOVE) -> win_adapt_term_size)\n"));
        win_adapt_term_size(shift, false);
      }
    when WM_SIZING: {  // mouse-drag window resizing
      trace_resize(("# WM_SIZING (resizing %d) VK_SHIFT %02X\n", resizing, GetKeyState(VK_SHIFT)));
      zoom_token = 2;
     /*
      * This does two jobs:
      * 1) Keep the tip uptodate
      * 2) Make sure the window size is _stepped_ in units of the font size.
      */
      LPRECT r = (LPRECT) lp;
      int width = r->right - r->left - extra_width - 2 * PADDING;
      int height = r->bottom - r->top - extra_height - 2 * PADDING;
      int cols = max(1, (float)width / font_width + 0.5);
      int rows = max(1, (float)height / font_height + 0.5);

      int ew = width - cols * font_width;
      int eh = height - rows * font_height;

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
      trace_resize(("# WM_SIZE (resizing %d) VK_SHIFT %02X %d\n", resizing, GetKeyState(VK_SHIFT), zoom_token));
      if (wp == SIZE_RESTORED && win_is_fullscreen)
        clear_fullscreen();
      else if (wp == SIZE_MAXIMIZED && go_fullscr_on_max) {
        go_fullscr_on_max = false;
        make_fullscreen();
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
        bool scale_font = (cfg.zoom_font_with_window || zoom_token > 2)
                          && (zoom_token > 0) && (GetKeyState(VK_SHIFT) & 0x80);

        win_adapt_term_size(false, scale_font);
        if (zoom_token > 0)
          zoom_token = zoom_token >> 1;
      }

      return 0;
    }
    when WM_INITMENU:
      win_update_menus();
      return 0;
    when WM_DPICHANGED:
      if (per_monitor_dpi_aware) {
        LPRECT r = (LPRECT) lp;
        WORD x_dpi = LOWORD(wp);

#ifdef debug_dpi
        printf("WM_DPICHANGED %d L,T,R,B=%d,%d,%d,%d WxH=%dx%d, %d -> %d\n", per_monitor_dpi_aware, r->left, r->top, r->right, r->bottom, r->right - r->left, r->bottom - r->top, last_dpi, x_dpi);
#endif
        if (last_dpi > 0) {
          int font_size1 = round((double)font_size * x_dpi / last_dpi);
#ifdef debug_dpi
          printf("font_size: %d -> %d\n", font_size, font_size1);
#endif
          win_set_font_size(font_size1, false);
        }
        last_dpi = x_dpi;

#ifdef debug_dpi
        printf("SM_CXVSCROLL %d\n", GetSystemMetrics(SM_CXVSCROLL));
#endif

        SetWindowPos(wnd, 0,
          r->left, r->top, r->right - r->left, r->bottom - r->top,
          SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);

        win_adapt_term_size(false, true);
        return 0;
      }
      break;
  }
 /*
  * Any messages we don't process completely above are passed through to
  * DefWindowProc() for default processing.
  */
  return DefWindowProcW(wnd, message, wp, lp);
}

static const char help[] =
  "Usage: " APPNAME " [OPTION]... [ PROGRAM [ARG]... | - ]\n"
  "\n"
  "Start a new terminal session running the specified program or the user's shell.\n"
  "If a dash is given instead of a program, invoke the shell as a login shell.\n"
  "\n"
  "Options:\n"
  "  -c, --config FILE     Load specified config file\n"
  "  -e, --exec            Treat remaining arguments as the command to execute\n"
  "  -h, --hold never|start|error|always  Keep window open after command finishes\n"
  "  -i, --icon FILE[,IX]  Load window icon from file, optionally with index\n"
  "  -l, --log FILE|-      Log output to file or stdout\n"
  "  -o, --option OPT=VAL  Override config file option with given value\n"
  "  -p, --position X,Y    Open window at specified coordinates\n"
  "  -s, --size COLS,ROWS  Set screen size in characters\n"
  "  -t, --title TITLE     Set window title (default: the invoked command)\n"
  "  -u, --utmp            Create a utmp entry\n"
  "  -w, --window normal|min|max|full|hide  Set initial window state\n"
  "      --class CLASS     Set window class name (default: " APPNAME ")\n"
  "  -H, --help            Display help and exit\n"
  "  -V, --version         Print version information and exit\n"
;

static const char short_opts[] = "+:c:C:eh:i:l:o:p:s:t:T:B:R:uw:HVd";

static const struct option
opts[] = {
  {"config",     required_argument, 0, 'c'},
  {"loadconfig", required_argument, 0, 'C'},
  {"exec",       no_argument,       0, 'e'},
  {"hold",       required_argument, 0, 'h'},
  {"icon",       required_argument, 0, 'i'},
  {"log",        required_argument, 0, 'l'},
  {"utmp",       no_argument,       0, 'u'},
  {"option",     required_argument, 0, 'o'},
  {"position",   required_argument, 0, 'p'},
  {"size",       required_argument, 0, 's'},
  {"title",      required_argument, 0, 't'},
  {"Title",      required_argument, 0, 'T'},
  {"Border",     required_argument, 0, 'B'},
  {"Reportpos",  required_argument, 0, 'R'},
  {"window",     required_argument, 0, 'w'},
  {"class",      required_argument, 0, ''},  // short option not enabled
  {"help",       no_argument,       0, 'H'},
  {"version",    no_argument,       0, 'V'},
  {"nodaemon",   no_argument,       0, 'd'},
  {"nopin",      no_argument,       0, ''},  // short option not enabled
  {"store-taskbar-properties", no_argument, 0, ''},  // no short option
  {0, 0, 0, 0}
};

static void
show_msg(FILE *stream, string msg)
{
  if (fputs(msg, stream) < 0 || fflush(stream) < 0)
    MessageBox(0, msg, APPNAME, MB_OK);
}

static no_return __attribute__((format(printf, 1, 2)))
error(char *format, ...)
{
  char *msg;
  va_list va;
  va_start(va, format);
  vasprintf(&msg, format, va);
  va_end(va);
  msg = asform("%s: %s\nTry '--help' for more information.\n",
               main_argv[0], msg);
  show_msg(stderr, msg);
  exit(1);
}

static void __attribute__((format(printf, 1, 2)))
warn(char *format, ...)
{
  char *msg;
  va_list va;
  va_start(va, format);
  vasprintf(&msg, format, va);
  va_end(va);
  msg = asform("%s: %s\n", main_argv[0], msg);
  show_msg(stderr, msg);
}

void
report_pos()
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
    cols = (placement.rcNormalPosition.right - placement.rcNormalPosition.left - norm_extra_width - 2 * PADDING) / font_width;
    rows = (placement.rcNormalPosition.bottom - placement.rcNormalPosition.top - norm_extra_height - 2 * PADDING) / font_height;

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
exit_mintty()
{
  report_pos();
  exit(0);
}

#include <shlobj.h>

static char *
get_shortcut_icon_location(wchar * iconfile)
{
  IShellLinkW * shell_link;
  IPersistFile * persist_file;
  HRESULT hres = OleInitialize(NULL);
  if (hres != S_FALSE && hres != S_OK)
    return 0;

  hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IShellLinkW, (void **) &shell_link);
  if (!SUCCEEDED(hres))
    return 0;

  hres = shell_link->lpVtbl->QueryInterface(shell_link, &IID_IPersistFile,
                                            (void **) &persist_file);
  if (!SUCCEEDED(hres)) {
    shell_link->lpVtbl->Release(shell_link);
    return 0;
  }

  /* Load the shortcut.  */
  hres = persist_file->lpVtbl->Load(persist_file, iconfile, STGM_READ);

  char * result = 0;

  if (SUCCEEDED(hres)) {
    WCHAR wil[MAX_PATH];
    char il[MAX_PATH * cs_cur_max];
    int index;
    shell_link->lpVtbl->GetIconLocation(shell_link, wil, sizeof wil, &index);
    cs_wcstombs(il, wil, sizeof il);
    if (index) {
      char _num[22];
      sprintf(_num, ",%d", index);
      strncat(il, _num, sizeof il - strlen(_num) - 1);
    }
    /* Resolve leading Windows environment variable component.  */
    char * fin;
    if (il[0] == '%' && il[1] && il[1] != '%' && (fin = strchr(&il[2], '%'))) {
      char var[fin - il];
      char * cop = var;
      for (char * v = &il[1]; *v != '%'; v++) {
        if (*v >= 'a' && *v <= 'z')
          *cop = *v - 'a' + 'A';
        else
          *cop = *v;
        cop++;
      }
      *cop = '\0';

      char * val = getenv(var);
      if (val) {
        char resolvil[strlen(val) + strlen(il) + 1];
        sprintf(resolvil, "%s%s", val, fin+1);
        result = strdup(resolvil);
      }
    }
    if (!result)
      result = strdup(il);
  }

  /* Release the pointer to the IPersistFile interface. */
  persist_file->lpVtbl->Release(persist_file);

  /* Release the pointer to the IShellLink interface. */
  shell_link->lpVtbl->Release(shell_link);

  return result;
}

static void
configure_taskbar()
{
#define no_patch_jumplist
#ifdef patch_jumplist
#include "jumplist.h"
  // test data
  wchar * jump_list_title[] = {
    L"title1", L"", L"", L"mä€", L"", L"", L"", L"", L"", L"", 
  };
  wchar * jump_list_cmd[] = {
    L"-o Rows=15", L"-o Rows=20", L"", L"-t mö€", L"", L"", L"", L"", L"", L"", 
  };
  // the patch offered in issue #290 does not seem to work
  setup_jumplist(jump_list_title, jump_list_cmd);
#endif

#if CYGWIN_VERSION_DLL_MAJOR >= 1007
  // initial patch (issue #471) contributed by Johannes Schindelin
  const char * app_id = cfg.app_id;
  const char * relaunch_icon = cfg.icon;
  const char * relaunch_display_name = cfg.app_name;
  const char * relaunch_command = cfg.app_launch_cmd;

#define dont_debug_properties

#ifdef two_witty_ideas_with_bad_side_effects
#warning automatic derivation of an AppId is likely not a good idea
  // If an icon is configured but no app_id, we can derive one from the 
  // icon in order to enable proper taskbar grouping by common icon.
  // However, this has an undesirable side-effect if a shortcut is 
  // pinned (presumably getting some implicit AppID from Windows) and 
  // instances are started from there (with a different AppID...).
  // Disabled.
  if (relaunch_icon && *relaunch_icon && (!app_id || !*app_id)) {
    const char * iconbasename = strrchr(cfg.icon, '/');
    if (iconbasename)
      iconbasename ++;
    else {
      iconbasename = strrchr(cfg.icon, '\\');
      if (iconbasename)
        iconbasename ++;
      else
        iconbasename = cfg.icon;
    }
    char * derived_app_id = malloc(strlen(iconbasename) + 7 + 1);
    strcpy(derived_app_id, "Mintty.");
    strcat(derived_app_id, iconbasename);
    app_id = derived_app_id;
  }
  // If app_name is configured but no app_launch_cmd, we need an app_id 
  // to make app_name effective as taskbar title, so invent one.
  if (relaunch_display_name && *relaunch_display_name && 
      (!app_id || !*app_id)) {
    app_id = "Mintty.AppID";
  }
#endif

  // Set the app ID explicitly, as well as the relaunch command and display name
  if (prevent_pinning || (app_id && *app_id)) {
    HMODULE shell = load_sys_library("shell32.dll");
    HRESULT (WINAPI *pGetPropertyStore)(HWND hwnd, REFIID riid, void **ppv) =
      (void *)GetProcAddress(shell, "SHGetPropertyStoreForWindow");
#ifdef debug_properties
      printf("SHGetPropertyStoreForWindow linked %d\n", !!pGetPropertyStore);
#endif
    if (pGetPropertyStore) {
      size_t size;
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
        if (relaunch_command && *relaunch_command && store_taskbar_properties
            && (size = cs_mbstowcs(0, relaunch_command, 0) + 1)) {
          var.pwszVal = malloc(size * sizeof(wchar));
          if (var.pwszVal) {
#ifdef debug_properties
            printf("AppUserModel_RelaunchCommand=%s\n", relaunch_command);
#endif
            cs_mbstowcs(var.pwszVal, relaunch_command, size);
            var.vt = VT_LPWSTR;
            pps->lpVtbl->SetValue(pps,
                &PKEY_AppUserModel_RelaunchCommand, &var);
          }
        }
        if (relaunch_display_name && *relaunch_display_name &&
            (size = cs_mbstowcs(0, relaunch_display_name, 0) + 1)) {
          var.pwszVal = malloc(size * sizeof(wchar));
          if (var.pwszVal) {
#ifdef debug_properties
            printf("AppUserModel_RelaunchDisplayNameResource=%s\n", relaunch_display_name);
#endif
            cs_mbstowcs(var.pwszVal, relaunch_display_name, size);
            var.vt = VT_LPWSTR;
            pps->lpVtbl->SetValue(pps,
                &PKEY_AppUserModel_RelaunchDisplayNameResource, &var);
          }
        }
        if (relaunch_icon && *relaunch_icon &&
            (size = cs_mbstowcs(0, relaunch_icon, 0) + 1)) {
          var.pwszVal = malloc(size * sizeof(wchar));
          if (var.pwszVal) {
#ifdef debug_properties
            printf("AppUserModel_RelaunchIconResource=%s\n", relaunch_icon);
#endif
            cs_mbstowcs(var.pwszVal, relaunch_icon, size);
            var.vt = VT_LPWSTR;
            pps->lpVtbl->SetValue(pps,
                &PKEY_AppUserModel_RelaunchIconResource, &var);
          }
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
        if (app_id && *app_id &&
            (size = cs_mbstowcs(0, app_id, 0) + 1)) {
          var.pwszVal = malloc(size * sizeof(wchar));
          if (var.pwszVal) {
#ifdef debug_properties
            printf("AppUserModel_ID=%s\n", app_id);
#endif
            cs_mbstowcs(var.pwszVal, app_id, size);
            var.vt = VT_LPWSTR;  // VT_EMPTY should remove but has no effect
            pps->lpVtbl->SetValue(pps,
                &PKEY_AppUserModel_ID, &var);
          }
        }

        pps->lpVtbl->Commit(pps);
        pps->lpVtbl->Release(pps);
      }
    }
  }
#endif
}

int
main(int argc, char *argv[])
{
  main_argv = argv;
  main_argc = argc;
#ifdef debuglog
  mtlog = fopen("/tmp/mtlog", "a");
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

  load_config("/etc/minttyrc", true);
  string rc_file = asform("%s/.minttyrc", home);
  load_config(rc_file, true);
  delete(rc_file);

  for (;;) {
    int opt = getopt_long(argc, argv, short_opts, opts, 0);
    if (opt == -1 || opt == 'e')
      break;
    char *longopt = argv[optind - 1], *shortopt = (char[]){'-', optopt, 0};
    switch (opt) {
      when 'c': load_config(optarg, true);
      when 'C': load_config(optarg, false);
      when 'h': set_arg_option("Hold", optarg);
      when 'i': set_arg_option("Icon", optarg);
      when 'l': set_arg_option("Log", optarg);
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
          error("syntax error in position argument '%s'", optarg);
      when 's':
        if (strcmp(optarg, "maxwidth") == 0)
          maxwidth = true;
        else if (strcmp(optarg, "maxheight") == 0)
          maxheight = true;
        else if (sscanf(optarg, "%u,%u%1s", &cfg.cols, &cfg.rows, (char[2]){}) == 2) {
          remember_arg("Columns");
          remember_arg("Rows");
        }
        else if (sscanf(optarg, "%ux%u%1s", &cfg.cols, &cfg.rows, (char[2]){}) == 2) {
          remember_arg("Columns");
          remember_arg("Rows");
        }
        else
          error("syntax error in size argument '%s'", optarg);
      when 't': set_arg_option("Title", optarg);
      when 'T':
        set_arg_option("Title", optarg);
        title_settable = false;
      when 'B':
        border_style = strdup (optarg);
      when 'R':
        report_geom = strdup (optarg);
      when 'u': cfg.utmp = true;
      when '':
        prevent_pinning = true;
        store_taskbar_properties = true;
      when '': store_taskbar_properties = true;
      when 'w': set_arg_option("Window", optarg);
      when '': set_arg_option("Class", optarg);
      when 'd':
        cfg.daemonize = false;
      when 'H':
        show_msg(stdout, help);
        return 0;
      when 'V':
        show_msg(stdout, VERSION_TEXT);
        return 0;
      when '?':
        error("unknown option '%s'", optopt ? shortopt : longopt);
      when ':':
        error("option '%s' requires an argument",
              longopt[1] == '-' ? longopt : shortopt);
    }
  }

  if (getenv("MINTTY_ROWS")) {
    set_arg_option("Rows", getenv("MINTTY_ROWS"));
    unsetenv("MINTTY_ROWS");
  }
  if (getenv("MINTTY_COLS")) {
    set_arg_option("Columns", getenv("MINTTY_COLS"));
    unsetenv("MINTTY_COLS");
  }
  if (getenv("MINTTY_MONITOR")) {
    monitor = atoi(getenv("MINTTY_MONITOR"));
    unsetenv("MINTTY_MONITOR");
  }
  if (getenv("MINTTY_ICON")) {
    cfg.icon = getenv("MINTTY_ICON");
    icon_is_from_shortcut = true;
    unsetenv("MINTTY_ICON");
  }

#define dont_debug_icon
#ifdef debug_icon
  fprintf(mtlog, "cfgicon %s\n", cfg.icon);
  fprintf(mtlog, "shorcut %d %s\n", invoked_from_shortcut, (char *)cygwin_create_path(CCP_WIN_W_TO_POSIX, sui.lpTitle));
  if (invoked_from_shortcut) {
    char * icon = get_shortcut_icon_location(sui.lpTitle);
    fprintf(mtlog, "icon %s\n", icon);
  }
#endif
  if (invoked_from_shortcut && (!cfg.icon || !*cfg.icon)) {
    cfg.icon = get_shortcut_icon_location(sui.lpTitle);
    icon_is_from_shortcut = true;
  }

  finish_config();

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
  if (daemonize) {  // detach from parent process and terminal
    pid_t pid = fork();
    if (pid < 0)
      fprintf(stderr, "Mintty could not detach from caller, starting anyway.\n");
    if (pid > 0)
      exit(0);  // exit parent process

    setsid();  // detach child process
  }

  load_dwm_funcs();  // must be called after the fork() above!

  load_shcore_funcs();
  per_monitor_dpi_aware = set_per_monitor_dpi_aware();
#ifdef debug_dpi
  printf("per_monitor_dpi_aware %d\n", per_monitor_dpi_aware);
#endif

  // Work out what to execute.
  argv += optind;
  if (*argv && (argv[1] || strcmp(*argv, "-")))
    cmd = *argv;
  else {
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
    if (*argv)
      arg0 = asform("-%s", arg0);

    // Create new argument array.
    argv = newn(char *, 2);
    *argv = arg0;
  }

  // Load icon if specified.
  HICON large_icon = 0, small_icon = 0;
  if (*cfg.icon) {
    string icon_file = strdup(cfg.icon);
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
#if CYGWIN_VERSION_API_MINOR >= 181
    wchar *win_icon_file = cygwin_create_path(CCP_POSIX_TO_WIN_W, icon_file);
    if (win_icon_file) {
      ExtractIconExW(win_icon_file, icon_index, &large_icon, &small_icon, 1);
      free(win_icon_file);
    }
#else
    char win_icon_file[MAX_PATH];
    cygwin_conv_to_win32_path(icon_file, win_icon_file);
    ExtractIconExA(win_icon_file, icon_index, &large_icon, &small_icon, 1);
#endif
    if (!large_icon) {
      small_icon = 0;
      uint error = GetLastError();
      if (error) {
        char msg[1024];
        FormatMessage(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
          0, error, 0, msg, sizeof msg, 0
        );
        warn("could not load icon from '%s': %s", cfg.icon, msg);
      }
      else
        warn("could not load icon from '%s'", cfg.icon);
    }
    delete(icon_file);
  }

  // Set the AppID if specified and the required function is available.
  if (*cfg.app_id) {
    HMODULE shell = load_sys_library("shell32.dll");
    HRESULT (WINAPI *pSetAppID)(PCWSTR) =
      (void *)GetProcAddress(shell, "SetCurrentProcessExplicitAppUserModelID");

    if (pSetAppID) {
      size_t size = cs_mbstowcs(0, cfg.app_id, 0) + 1;
      if (size) {
        wchar buf[size];
        cs_mbstowcs(buf, cfg.app_id, size);
        pSetAppID(buf);
      }
    }
  }

  inst = GetModuleHandle(NULL);

  // Window class name.
  wstring wclass = _W(APPNAME);
  if (*cfg.class) {
    size_t size = cs_mbstowcs(0, cfg.class, 0) + 1;
    if (size) {
      wchar *buf = newn(wchar, size);
      cs_mbstowcs(buf, cfg.class, size);
      wclass = buf;
    }
    else
      fputs("Using default class name due to invalid characters.\n", stderr);
  }

  // Put child command line into window title if we haven't got one already.
  string title = cfg.title;
  if (!*title) {
    size_t len;
    char *argz;
    argz_create(argv, &argz, &len);
    argz_stringify(argz, len, ' ');
    title = argz;
  }

  // Convert title to Unicode. Default to application name if unsuccessful.
  wstring wtitle = _W(APPNAME);
  {
    size_t size = cs_mbstowcs(0, title, 0) + 1;
    if (size) {
      wchar *buf = newn(wchar, size);
      cs_mbstowcs(buf, title, size);
      wtitle = buf;
    }
    else
      fputs("Using default title due to invalid characters.\n", stderr);
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


  // Initialise the fonts, thus also determining their width and height.
  win_init_fonts(cfg.font.size);

  // Reconfigure the charset module now that arguments have been converted,
  // the locale/charset settings have been loaded, and the font width has
  // been determined.
  cs_reconfig();

  // Determine window sizes.
  int term_width = font_width * cfg.cols;
  int term_height = font_height * cfg.rows;

  RECT cr = {0, 0, term_width + 2 * PADDING, term_height + 2 * PADDING};
  RECT wr = cr;
  LONG window_style = WS_OVERLAPPEDWINDOW;
  if (border_style) {
    if (strcmp (border_style, "void") == 0)
      window_style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    else
      window_style &= ~(WS_CAPTION | WS_BORDER);
  }
  AdjustWindowRect(&wr, window_style, false);
  int width = wr.right - wr.left;
  int height = wr.bottom - wr.top;

  if (cfg.scrollbar)
    width += GetSystemMetrics(SM_CXVSCROLL);

  extra_width = width - (cr.right - cr.left);
  extra_height = height - (cr.bottom - cr.top);
  norm_extra_width = extra_width;
  norm_extra_height = extra_height;

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
#define printpos(tag, x, y, mon)  printf("%s %d %d (%ld %ld %ld %ld)\n", tag, x, y, mon.left, mon.top, mon.right, mon.bottom);
#else
#define printpos(tag, x, y, mon)
#endif

  // Create initial window.
  wnd = CreateWindowExW(cfg.scrollbar < 0 ? WS_EX_LEFTSCROLLBAR : 0,
                        wclass, wtitle,
                        window_style | (cfg.scrollbar ? WS_VSCROLL : 0),
                        x, y, width, height,
                        null, null, inst, null);

  // Adapt window position (and maybe size) to special parameters
  // also select monitor if requested
  if (center || right || bottom || left || top || maxwidth || maxheight
      || monitor > 0
     ) {
    MONITORINFO mi;
    get_my_monitor_info(&mi);
    RECT ar = mi.rcWork;
    printpos ("cre", x, y, ar);

    if (monitor > 0) {
      MONITORINFO monmi;
      get_monitor_info(monitor, &monmi);
      RECT monar = monmi.rcWork;

      if (x == (int)CW_USEDEFAULT) {
        // Shift and scale assigned default position to selected monitor.
        win_get_pos(&x, &y);
        printpos ("def", x, y, ar);
        x = monar.left + (x - ar.left) * (monar.right - monar.left) / (ar.right - ar.left);
        y = monar.top + (y - ar.top) * (monar.bottom - monar.top) / (ar.bottom - ar.top);
      }
      else {
        // Shift selected position to selected monitor.
        x += monar.left - ar.left;
        y += monar.top - ar.top;
      }

      ar = monar;
      printpos ("mon", x, y, ar);
    }

    if (cfg.x == (int)CW_USEDEFAULT) {
      if (monitor == 0)
        win_get_pos(&x, &y);
      if (left || right)
        cfg.x = 0;
      if (top || bottom)
        cfg.y = 0;
        printpos ("fix", x, y, ar);
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
      printpos ("pos", x, y, ar);

    if (maxwidth) {
      x = ar.left;
      width = ar.right - ar.left;
    }
    if (maxheight) {
      y = ar.top;
      height = ar.bottom - ar.top;
    }
    printpos ("fin", x, y, ar);

    SetWindowPos(wnd, NULL, x, y, width, height,
      SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
  }

  if (per_monitor_dpi_aware) {
    if (cfg.x != (int)CW_USEDEFAULT) {
      // The first SetWindowPos actually set x and y
      SetWindowPos(wnd, NULL, x, y, width, height,
        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
      // Then, we are placed the windows on the correct monitor and we can
      // now interpret width/height in correct DPI.
      SetWindowPos(wnd, NULL, x, y, width, height,
        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
  }

  if (border_style) {
    LONG style = GetWindowLong(wnd, GWL_STYLE);
    if (strcmp (border_style, "void") == 0) {
      style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    }
    else {
      style &= ~(WS_CAPTION | WS_BORDER);
    }
    SetWindowLong(wnd, GWL_STYLE, style);
    SetWindowPos(wnd, null, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }

  configure_taskbar();

  // The input method context.
  imc = ImmGetContext(wnd);

  // Correct autoplacement, which likes to put part of the window under the
  // taskbar when the window size approaches the work area size.
  if (cfg.x == (int)CW_USEDEFAULT) {
    win_fix_position();
  }

  // Initialise the terminal.
  term_reset();
  term_resize(cfg.rows, cfg.cols);

  // Initialise the scroll bar.
  SetScrollInfo(
    wnd, SB_VERT,
    &(SCROLLINFO){
      .cbSize = sizeof(SCROLLINFO),
      .fMask = SIF_ALL | SIF_DISABLENOSCROLL,
      .nMin = 0, .nMax = cfg.rows - 1,
      .nPage = cfg.rows, .nPos = 0,
    },
    false
  );

  // Set up an empty caret bitmap. We're painting the cursor manually.
  caretbm = CreateBitmap(1, font_height, 1, 1, newn(short, font_height));
  CreateCaret(wnd, caretbm, 0, 0);

  // Initialise various other stuff.
  win_init_drop_target();
  win_init_menus();
  update_transparency();

  // Create child process.
  child_create(
    argv, &(struct winsize){cfg.rows, cfg.cols, term_width, term_height}
  );

  // Finally show the window!
  go_fullscr_on_max = (cfg.window == -1);
  ShowWindow(wnd, go_fullscr_on_max ? SW_SHOWMAXIMIZED : cfg.window);
  SetFocus(wnd);

#ifdef debug_display_monitors_mockup
  {
    int x, y;
    MONITORINFO mi;
    int n = search_monitors(&x, &y, 0, false, &mi);
    printf("%d monitors, smallest %dx%d, current %d,%d...%d,%d\n", n, x, y, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
    n = search_monitors(&x, &y, 0, true, &mi);
    printf("%d monitors, smallest %dx%d, primary %d,%d...%d,%d\n", n, x, y, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
    n = search_monitors(&x, &y, (HMONITOR)(current_monitor + 1), false, 0);
    printf("current monitor: %d\n", n);
    n = search_monitors(&x, &y, (HMONITOR)(primary_monitor + 1), false, 0);
    printf("primary monitor: %d\n", n);
  }
#endif

  // Message loop.
  for (;;) {
    MSG msg;
    while (PeekMessage(&msg, null, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT)
        return msg.wParam;
      if (!IsDialogMessage(config_wnd, &msg))
        DispatchMessage(&msg);
    }
    child_proc();
  }
}
