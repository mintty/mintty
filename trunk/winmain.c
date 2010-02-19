// win.c (part of mintty)
// Copyright 2008-10 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "term.h"
#include "config.h"
#include "appinfo.h"
#include "linedisc.h"
#include "child.h"
#include "charset.h"

#include <locale.h>
#include <process.h>
#include <getopt.h>
#include <imm.h>
#include <winnls.h>
#include <shellapi.h>

#include <sys/cygwin.h>
#include <cygwin/version.h>

HWND wnd;
HINSTANCE inst;
HDC dc;

#if WINVER < 0x500

#define MONITOR_DEFAULTTONEAREST 2 

#define FLASHW_STOP 0
#define FLASHW_CAPTION 1
#define FLASHW_TRAY 2
#define FLASHW_TIMER 4

typedef struct {
  UINT  cbSize;
  HWND  hwnd;
  DWORD dwFlags;
  UINT  uCount;
  DWORD dwTimeout;
} FLASHWINFO, *PFLASHWINFO;

#define LWA_ALPHA	0x02

typedef struct {
  int cxLeftWidth;
  int cxRightWidth;
  int cyTopHeight;
  int cyBottomHeight;
} MARGINS;

#endif

static HMONITOR (WINAPI *pMonitorFromWindow)(HWND,DWORD);
static BOOL (WINAPI *pGetMonitorInfo)(HMONITOR,LPMONITORINFO);
static BOOL (WINAPI *pFlashWindowEx)(PFLASHWINFO);
static BOOL (WINAPI *pSetLayeredWindowAttributes)(HWND,COLORREF,BYTE,DWORD);
static HRESULT (WINAPI *pDwmIsCompositionEnabled)(BOOL *);
static HRESULT (WINAPI *pDwmExtendFrameIntoClientArea)(HWND, const MARGINS *);

static void
load_funcs(void)
{
  HMODULE user = LoadLibrary("user32");
  pMonitorFromWindow = (void *)GetProcAddress(user, "MonitorFromWindow");
  pGetMonitorInfo = (void *)GetProcAddress(user, "GetMonitorInfoA");
  pFlashWindowEx = (void *)GetProcAddress(user, "FlashWindowEx");
  pSetLayeredWindowAttributes =
    (void *)GetProcAddress(user, "SetLayeredWindowAttributes");
  
  HMODULE dwm = LoadLibrary("dwmapi");
  pDwmIsCompositionEnabled =
    (void *)GetProcAddress(dwm, "DwmIsCompositionEnabled");
  pDwmExtendFrameIntoClientArea =
    (void *)GetProcAddress(dwm, "DwmExtendFrameIntoClientArea");
}


static int extra_width, extra_height;

static HBITMAP caretbm;
static int caret_x = -1, caret_y = -1;

static bool child_dead;

static char **main_argv;

void
win_set_timer(void (*cb)(void), uint ticks)
{ SetTimer(wnd, (UINT_PTR)cb, ticks, null); }

void
win_set_title(char *title)
{
  int wlen = cs_mbstowcs(0, title, 0);
  if (wlen >= 0) {
    wchar wtitle[wlen + 1];
    cs_mbstowcs(wtitle, title, wlen + 1);
    SetWindowTextW(wnd, wtitle);
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
win_move(int x, int y)
{
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

/*
 * Report whether the window is iconic, for terminal reports.
 */
bool
win_is_iconic(void)
{ return IsIconic(wnd); }

/*
 * Report the window's position, for terminal reports.
 */
void
win_get_pos(int *x, int *y)
{
  RECT r;
  GetWindowRect(wnd, &r);
  *x = r.left;
  *y = r.top;
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void
win_get_pixels(int *x, int *y)
{
  RECT r;
  GetWindowRect(wnd, &r);
  *x = r.right - r.left;
  *y = r.bottom - r.top;
}

// Clockwork
int get_tick_count(void) { return GetTickCount(); }
int cursor_blink_ticks(void) { return GetCaretBlinkTime(); }

static void
flash_taskbar(bool enable)
{
  static bool enabled;
  if (enable != enabled && pFlashWindowEx) {
    
    pFlashWindowEx(&(FLASHWINFO){
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
  if (cfg.bell_sound)
    MessageBeep(MB_OK);
  if (cfg.bell_taskbar && !term.has_focus)
    flash_taskbar(true);
}

static void
update_sys_cursor(void)
{
  if (term.has_focus && caret_x >= 0 && caret_y >= 0) {
    SetCaretPos(caret_x, caret_y);

    HIMC imc = ImmGetContext(wnd);
    COMPOSITIONFORM cf = {
      .dwStyle = CFS_POINT,
      .ptCurrentPos = {caret_x, caret_y}
    };
    ImmSetCompositionWindow(imc, &cf);
    ImmReleaseContext(wnd, imc);
  }
}

/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */
void
win_set_sys_cursor(int x, int y)
{
  int cx = x * font_width + PADDING;
  int cy = y * font_height + PADDING;
  if (cx != caret_x || cy != caret_y) {
    caret_x = cx;
    caret_y = cy;
    update_sys_cursor();
  }
}

/* Get the rect/size of a full screen window using the nearest available
 * monitor in multimon systems; default to something sensible if only
 * one monitor is present. */
static void
get_fullscreen_rect(RECT *rect)
{
  if (pMonitorFromWindow) {
    HMONITOR mon;
    MONITORINFO mi;
    mon = pMonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof (mi);
    pGetMonitorInfo(mon, &mi);
    *rect = mi.rcMonitor;
  }
  else
    GetClientRect(GetDesktopWindow(), rect);
}

static void
notify_resize(int rows, int cols)
{
  term_resize(rows, cols);
  win_update();
  struct winsize ws = {rows, cols, cols * font_width, rows * font_height};
  child_resize(&ws);
}

void
win_resize(int rows, int cols)
{
 /* If the window is maximized suppress resizing attempts */
  if (IsZoomed(wnd) || (rows == term.rows && cols == term.cols)) 
    return;
  
  notify_resize(rows, cols);
  int width = extra_width + font_width * cols + 2 * PADDING;
  int height = extra_height + font_height * rows + 2 * PADDING;
  SetWindowPos(wnd, null, 0, 0, width, height,
               SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
  win_invalidate_all();
}

static void
reinit_fonts(void)
{
  win_deinit_fonts();
  win_init_fonts();
}

static void
resize_window(bool forced)
{
 /* Current window sizes ... */
  RECT cr, wr;
  GetClientRect(wnd, &cr);
  GetWindowRect(wnd, &wr);
  int client_width = cr.right - cr.left;
  int client_height = cr.bottom - cr.top;
  extra_width = wr.right - wr.left - client_width;
  extra_height = wr.bottom - wr.top - client_height;
  int term_width = client_width - 2 * PADDING;
  int term_height = client_height - 2 * PADDING;
  
  int cols = term.cols, rows = term.rows;
  if (!client_width || !client_height) {
   /* Oh, looks like we're minimised: do nothing */
  }
  else if (IsZoomed(wnd) || forced) {
   /* We're fullscreen, or we were told to resize,
    * this means we must not change the size of
    * the window so the terminal has to change.
    */
    cols = term_width / font_width;
    rows = term_height / font_height;
  }
  else if (term_width != cols * font_width ||
           term_height != rows * font_height) {
   /* Window size isn't what's needed. Let's change it then. */
   /* Make sure the window isn't bigger than the screen. */
    static RECT ss;
    get_fullscreen_rect(&ss);
    int max_client_width = ss.right - ss.left - extra_width;
    int max_client_height = ss.bottom - ss.top - extra_height;
    cols = min(cols, (max_client_width - 2 * PADDING) / font_width);
    rows = min(rows, (max_client_height - 2 * PADDING) / font_height);
    SetWindowPos(wnd, null, 0, 0,
                 font_width * cols + 2 * PADDING + extra_width, 
                 font_height * rows + 2 * PADDING + extra_height,
                 SWP_NOMOVE | SWP_NOZORDER);
  }
  
  if (rows != term.rows || cols != term.cols)
    notify_resize(rows, cols);

  win_invalidate_all();
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
enable_glass(bool enabled)
{
  if (pDwmExtendFrameIntoClientArea)
    pDwmExtendFrameIntoClientArea(wnd, &(MARGINS){enabled ? -1 : 0, 0, 0, 0});
}

/*
 * When the window is fullscreen, there's no caption.
 */
bool
win_is_fullscreen(void)
{
  return !(GetWindowLongPtr(wnd, GWL_STYLE) & WS_CAPTION);
}

/*
 * Go full-screen. This should only be called when we are already
 * maximised.
 */
static void
make_fullscreen(void)
{
 /* Remove the window furniture. */
  DWORD style = GetWindowLongPtr(wnd, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
  SetWindowLongPtr(wnd, GWL_STYLE, style);

 /* Resize ourselves to exactly cover the nearest monitor. */
  RECT ss;
  get_fullscreen_rect(&ss);
  SetWindowPos(wnd, HWND_TOP, ss.left, ss.top, ss.right - ss.left,
               ss.bottom - ss.top, SWP_FRAMECHANGED);
  
 /* The glass effect doesn't work for fullscreen windows */
  enable_glass(false);
  
 /* We may have changed size as a result */
  resize_window(false);
}

/*
 * Clear the full-screen attributes.
 */
static void
clear_fullsceen(void)
{
  DWORD oldstyle, style;

 /* Reinstate the window furniture. */
  style = oldstyle = GetWindowLongPtr(wnd, GWL_STYLE);
  style |= WS_CAPTION | WS_BORDER | WS_THICKFRAME;
  if (style != oldstyle) {
    SetWindowLongPtr(wnd, GWL_STYLE, style);
    SetWindowPos(wnd, null, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
  
  enable_glass(cfg.transparency < 0 &&
               !(cfg.opaque_when_focused && term.has_focus));
}

/*
 * Maximise or restore the window in response to a server-side.
 * Argument value of 2 means go fullscreen.
 */
void
win_maximise(int max)
{
  if (IsZoomed(wnd)) {
    if (!max)
      ShowWindow(wnd, SW_RESTORE);
    else if (max == 2 && !win_is_fullscreen())
      make_fullscreen();
  }
  else if (max) {
    if (max == 2)
      SendMessage(wnd, WM_FULLSCR_ON_MAX, 0, 0);
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
  win_resize(cfg.rows, cfg.cols);
}

static void
reset_term(void)
{
  term_reset();
  term_deselect();
  term_clear_scrollback();
  win_update();
  ldisc_send(null, 0, 0);
}

static void
update_transparency(void)
{
  bool opaque = cfg.opaque_when_focused && term.has_focus;
  if (pSetLayeredWindowAttributes) {
    int trans = max(cfg.transparency, 0);
    SetWindowLong(wnd, GWL_EXSTYLE, trans ? WS_EX_LAYERED : 0);
    if (trans) {
      uchar alpha = opaque ? 255 : 255 - 16 * trans;
      pSetLayeredWindowAttributes(wnd, 0, alpha, LWA_ALPHA);
    }
  }
  enable_glass(cfg.transparency < 0 && !win_is_fullscreen() && !opaque);
}

void
win_reconfig(void)
{
 /*
  * Flush the line discipline's edit buffer in
  * case local editing has just been disabled.
  */
  ldisc_send(null, 0, 0);
  
 /* Pass new config data to the terminal */
  term_reconfig();
  
 /* Enable or disable the scroll bar, etc */
  if (new_cfg.scrollbar != cfg.scrollbar) {
    LONG flag = GetWindowLongPtr(wnd, GWL_STYLE);
    if (new_cfg.scrollbar)
      flag |= WS_VSCROLL;
    else
      flag &= ~WS_VSCROLL;
    SetWindowLongPtr(wnd, GWL_STYLE, flag);
    SetWindowPos(wnd, null, 0, 0, 0, 0,
                SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE |
                 SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
  
  bool font_changed =
    memcmp(&new_cfg.font, &cfg.font, sizeof cfg.font) ||
    new_cfg.bold_as_bright != cfg.bold_as_bright;
  
  /* Copy the new config and refresh everything */
  cfg = new_cfg;
  if (font_changed) {
    font_size = cfg.font.size;
    reinit_fonts();
  }
  win_reconfig_palette();
  update_transparency();
  win_invalidate_all();
  resize_window(false);
  win_update_mouse();

  bool old_ambig_wide = cs_ambig_wide;
  cs_reconfig();
  if (term.report_ambig_width && old_ambig_wide != cs_ambig_wide)
    ldisc_send(cs_ambig_wide ? "\e[2W" : "\e[1W", 4, 0);
}

uint
win_get_font_size(void)
{
  return abs(font_size);
}

void
win_set_font_size(int size)
{
  font_size = size ? sgn(font_size) * min(size, 72) : cfg.font.size;
  reinit_fonts();
  resize_window(false);
}

void
win_zoom_font(int zoom)
{
  win_set_font_size(zoom ? max(1, abs(font_size) + zoom) : 0);
}

static bool
confirm_exit(void)
{
  // Only ask once.
  static bool confirmed = false;
  confirmed |=
    !child_is_parent() ||
    MessageBox(
      wnd,
      "Processes are running in session.\n"
      "Exit anyway?",
      APPNAME, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2
    ) == IDOK;
  return confirmed;
}

static LRESULT CALLBACK
win_proc(HWND wnd, UINT message, WPARAM wp, LPARAM lp)
{
  static bool ignore_clip, need_backend_resize, fullscr_on_max;
  static int new_cols, new_rows;
  static bool resizing;
  static bool was_zoomed;
  static int prev_rows, prev_cols;

  switch (message) {
    when WM_TIMER: {
      KillTimer(wnd, wp);
      void_fn cb = (void_fn)wp;
      cb();
      return 0;
    }
    when WM_CLOSE:
      win_show_mouse();
      if (child_dead)
        exit(0);
      if (!cfg.confirm_exit || confirm_exit())
        child_kill();
      return 0;
    when WM_COMMAND or WM_SYSCOMMAND:
      switch (wp & ~0xF) {  /* low 4 bits reserved to Windows */
        when IDM_OPEN: term_open();
        when IDM_COPY: term_copy();
        when IDM_PASTE: win_paste();
        when IDM_SELALL:
          term_select_all();
          win_update();
        when IDM_RESET: reset_term();
        when IDM_DEFSIZE: default_size();
        when IDM_FULLSCREEN: win_maximise(win_is_fullscreen() ? 0 : 2);
        when IDM_OPTIONS: win_open_config();
        when IDM_NEW:
          spawnv(_P_DETACH, "/proc/self/exe", (void *) main_argv);
      }
    when WM_VSCROLL:
      switch (LOWORD(wp)) {
        when SB_BOTTOM:   term_scroll(-1, 0);
        when SB_TOP:      term_scroll(+1, 0);
        when SB_LINEDOWN: term_scroll(0, +1);
        when SB_LINEUP:   term_scroll(0, -1);
        when SB_PAGEDOWN: term_scroll(0, +term.rows);
        when SB_PAGEUP:   term_scroll(0, -term.rows);
        when SB_THUMBPOSITION or SB_THUMBTRACK: {
          SCROLLINFO info;
          info.cbSize = sizeof(SCROLLINFO);
          info.fMask = SIF_TRACKPOS;
          GetScrollInfo(wnd, SB_VERT, &info);
          term_scroll(1, info.nTrackPos);
        }
      }
    when WM_LBUTTONDOWN: win_mouse_click(MBT_LEFT, lp);
    when WM_RBUTTONDOWN: win_mouse_click(MBT_RIGHT, lp);
    when WM_MBUTTONDOWN: win_mouse_click(MBT_MIDDLE, lp);
    when WM_LBUTTONUP: win_mouse_release(MBT_LEFT, lp);
    when WM_RBUTTONUP: win_mouse_release(MBT_RIGHT, lp);
    when WM_MBUTTONUP: win_mouse_release(MBT_MIDDLE, lp);
    when WM_MOUSEMOVE: win_mouse_move(false, lp);
    when WM_NCMOUSEMOVE: win_mouse_move(true, lp);
    when WM_MOUSEWHEEL: win_mouse_wheel(wp, lp);
    when WM_KEYDOWN or WM_SYSKEYDOWN:
      if (win_key_down(wp, lp))
        return 0;
    when WM_KEYUP or WM_SYSKEYUP:
      if (win_key_up(wp, lp))
        return 0;
    when WM_CHAR or WM_SYSCHAR:
      {
        wchar wc = wp;
        term_seen_key_event();
        luni_send(&wc, 1, 1);
        return 0;
      }
    when WM_INPUTLANGCHANGE:
      update_sys_cursor();
    when WM_IME_STARTCOMPOSITION:
      {
        HIMC imc = ImmGetContext(wnd);
        ImmSetCompositionFont(imc, &lfont);
        ImmReleaseContext(wnd, imc);
      }
    when WM_IME_COMPOSITION:
      if (lp & GCS_RESULTSTR) {
        HIMC imc = ImmGetContext(wnd);
        LONG len = ImmGetCompositionStringW(imc, GCS_RESULTSTR, null, 0);
        if (len > 0) {
          char buf[len];
          ImmGetCompositionStringW(imc, GCS_RESULTSTR, buf, len);
          term_seen_key_event();
          luni_send((wchar *)buf, len / 2, 1);
        }
        ImmReleaseContext(wnd, imc);
        return 1;
      }
    when WM_IGNORE_CLIP:
      ignore_clip = wp;     /* don't panic on DESTROYCLIPBOARD */
    when WM_DESTROYCLIPBOARD:
      if (!ignore_clip)
        term_deselect();
      ignore_clip = false;
      return 0;
    when WM_PAINT:
      win_paint();
      return 0;
    when WM_SETFOCUS:
      term_set_focus(true);
      CreateCaret(wnd, caretbm, font_width, font_height);
      ShowCaret(wnd);
      flash_taskbar(false);  /* stop */
      win_update();
      update_transparency();
    when WM_KILLFOCUS:
      win_show_mouse();
      term_set_focus(false);
      DestroyCaret();
      caret_x = caret_y = -1;   /* ensure caret is replaced next time */
      win_update();
      update_transparency();
    when WM_FULLSCR_ON_MAX: fullscr_on_max = true;
    when WM_MOVE: update_sys_cursor();
    when WM_ENTERSIZEMOVE:
      win_enable_tip();
      resizing = true;
    when WM_EXITSIZEMOVE:
      win_disable_tip();
      resizing = false;
      if (need_backend_resize) {
        need_backend_resize = false;
        notify_resize(new_rows, new_cols);
        win_invalidate_all();
      }
    when WM_SIZING: {
     /*
      * This does two jobs:
      * 1) Keep the sizetip uptodate
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
      
      win_update_tip(r->left + extra_width, r->top + extra_height, cols, rows);
      
      return ew || eh;
    }
    when WM_SIZE: {
      if (wp == SIZE_RESTORED)
        clear_fullsceen();
      if (wp == SIZE_MAXIMIZED && fullscr_on_max) {
        fullscr_on_max = false;
        make_fullscreen();
      }
      int new_width = LOWORD(lp) - 2 * PADDING;
      int new_height = HIWORD(lp) - 2 * PADDING;
      new_cols = max(1, new_width / font_width);
      new_rows = max(1, new_height / font_height);
      if (resizing) {
       /*
        * Don't call child_size in mid-resize. (To prevent
        * massive numbers of resize events getting sent.)
        */
        need_backend_resize = true;
      }
      else if (wp == SIZE_MAXIMIZED && !was_zoomed) {
        was_zoomed = 1;
        prev_rows = term.rows;
        prev_cols = term.cols;
        notify_resize(new_rows, new_cols);
        resize_window(false);
      }
      else if (wp == SIZE_RESTORED && was_zoomed) {
        was_zoomed = 0;
        notify_resize(prev_rows, prev_cols);
        resize_window(false);
      }
      else {
       /* This is an unexpected resize, these will normally happen
        * if the window is too large. Probably either the user
        * selected a huge font or the screen size has changed.
        *
        * This is also called with minimize.
        */
        resize_window(true);
      }
      update_sys_cursor();
      return 0;
    }
    when WM_INITMENU:
      win_update_menus();
      return 0;
  }
 /*
  * Any messages we don't process completely above are passed through to
  * DefWindowProc() for default processing.
  */
  return DefWindowProcW(wnd, message, wp, lp);
}

static const char *help =
  "Usage: %s [OPTION]... [ PROGRAM [ARG]... | - ]\n"
  "\n"
  "If a program is supplied, it is executed with its arguments. Otherwise, the\n"
  "shell to execute is looked up in the SHELL environment variable followed by\n"
  "the user's shell setting in /etc/passwd. Failing that, /bin/sh is used. If\n"
  "the last argument is a single dash, the shell is invoked as a login shell.\n"
  "\n"
  "Options:\n"
  "  -e, --exec            Treat remaining arguments as the command to execute\n"
  "  -p, --position X,Y    Open window at specified coordinates\n"
  "  -s, --size COLS,ROWS  Set screen size in characters\n"
  "  -t, --title TITLE     Set window title (default: the invoked command)\n"
  "      --class CLASS     Set window class name (default: " APPNAME ")\n"
  "  -i, --icon FILE[,IX]  Load window icon from file, optionally with index\n"
  "  -l, --log FILE        Log output to file\n"
  "  -u, --utmp            Create a utmp entry\n"
  "  -h, --hold never|always|error  Keep window open after command terminates?\n"
  "  -c, --config FILE     Load specified config file\n"
  "  -o, --option OPT=VAL  Override config option with given value\n"
  "  -H, --help            Display help and exit\n"
  "  -V, --version         Print version information and exit\n"
;

static const char short_opts[] = "+HVuec:o:p:s:t:i:l:h:";

static const struct option
opts[] = { 
  {"help",     no_argument,       0, 'H'},
  {"version",  no_argument,       0, 'V'},
  {"exec",     no_argument,       0, 'e'},
  {"utmp",     no_argument,       0, 'u'},
  {"config",   required_argument, 0, 'c'},
  {"option",   required_argument, 0, 'o'},
  {"position", required_argument, 0, 'p'},
  {"size",     required_argument, 0, 's'},
  {"title",    required_argument, 0, 't'},
  {"class",    required_argument, 0, 'C'},
  {"icon",     required_argument, 0, 'i'},
  {"log",      required_argument, 0, 'l'},
  {"hold",     required_argument, 0, 'h'},
  {0, 0, 0, 0}
};

static no_return __attribute__((format(printf, 1, 2)))
error(char *f, ...) 
{
  fprintf(stderr, "%s: ", main_argv[0]);
  va_list va;
  va_start(va, f);
  vfprintf(stderr, f, va);
  va_end(va);
  fputc('\n', stderr);
  exit(1);
}

int
main(int argc, char *argv[])
{
  char *title = 0, *icon_file = 0;
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
  bool size_override = false;
  uint rows = 0, cols = 0;
  wchar *class_name = _W(APPNAME);

  setlocale(LC_CTYPE, "");
  main_argv = argv;
  
  load_config("/etc/minttyrc");
  
  char *rc_file;
  asprintf(&rc_file, "%s/.minttyrc", getenv("HOME") ?: "/tmp");
  load_config(rc_file);
  free(rc_file);

  for (;;) {
    int opt = getopt_long(argc, argv, short_opts, opts, 0);
    if (opt == -1 || opt == 'e')
      break;
    switch (opt) {
      when 'c': load_config(optarg);
      when 'o': parse_option(optarg);
      when 't': title = optarg;
      when 'i': icon_file = optarg;
      when 'l': log_file = optarg;
      when 'u': utmp_enabled = true;
      when 'p':
        if (sscanf(optarg, "%i,%i%1s", &x, &y, (char[2]){}) != 2)
          error("syntax error in position argument -- %s", optarg);
      when 's':
        if (sscanf(optarg, "%u,%u%1s", &cols, &rows, (char[2]){}) != 2)
          error("syntax error in size argument -- %s", optarg);
        size_override = true;
      when 'h': {
        int len = strlen(optarg);
        if (memcmp(optarg, "always", len) == 0)
          hold = HOLD_ALWAYS;
        else if (memcmp(optarg, "never", len) == 0)
          hold = HOLD_NEVER;
        else if (memcmp(optarg, "error", len) == 0)
          hold = HOLD_ERROR;
        else
          error("invalid argument to hold option -- %s", optarg);
      }
      when 'C': {
        int len = mbstowcs(0, optarg, 0);
        if (len < 0)
          error("invalid character in class name -- %s", optarg);
        class_name = newn(wchar, len + 1);
        mbstowcs(class_name, optarg, len + 1);
      }
      when 'H':
        printf(help, *argv);
        return 0;
      when 'V':
        puts(VERSION_TEXT);
        return 0;
      otherwise:
        fputs("Try --help for more information.\n", stderr);
        exit(1);
    }
  }
  
  load_funcs();

  HICON small_icon = 0, large_icon = 0;
  if (icon_file) {
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
#if CYGWIN_VERSION_API_MINOR >= 181
    wchar *win_icon_file = cygwin_create_path(CCP_POSIX_TO_WIN_W, icon_file);
    if (!win_icon_file)
      error("invalid icon file path -- %s", icon_file);
    ExtractIconExW(win_icon_file, icon_index, &large_icon, &small_icon, 1);
    free(win_icon_file);
#else
    char win_icon_file[MAX_PATH];
    cygwin_conv_to_win32_path(icon_file, win_icon_file);
    ExtractIconExA(win_icon_file, icon_index, &large_icon, &small_icon, 1);
#endif
    if (!small_icon || !large_icon)
      error("could not load icon -- %s", icon_file);
  }

  if (!size_override) {
    rows = cfg.rows;
    cols = cfg.cols;
  }

  inst = GetModuleHandle(NULL);

  RegisterClassW(&(WNDCLASSW){
    .style = 0,
    .lpfnWndProc = win_proc,
    .cbClsExtra = 0,
    .cbWndExtra = 0,
    .hInstance = inst,
    .hIcon = icon_file ? 0 : LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON)),
    .hCursor = LoadCursor(null, IDC_IBEAM),
    .hbrBackground = null,
    .lpszMenuName = null,
    .lpszClassName = class_name,
  });

 /* Create initial window.
  * Its real size has to be set after loading the fonts and determining their
  * size, but the window has to exist to do that.
  */
  wnd = CreateWindowW(class_name, 0,
                      WS_OVERLAPPEDWINDOW | (cfg.scrollbar ? WS_VSCROLL : 0),
                      x, y, 300, 200, null, null, inst, null);

  update_transparency();
  
  if (icon_file) {
    if (small_icon)
      SendMessage(wnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    if (large_icon)
      SendMessage(wnd, WM_SETICON, ICON_BIG, (LPARAM)large_icon);
  }

 /*
  * Determine extra_{width,height}.
  */
  {
    RECT cr, wr;
    GetWindowRect(wnd, &wr);
    GetClientRect(wnd, &cr);
    extra_width = wr.right - wr.left - cr.right + cr.left;
    extra_height = wr.bottom - wr.top - cr.bottom + cr.top;
  }

  win_init_menus();
  
 /*
  * Initialise the terminal. (We have to do this _after_
  * creating the window, since the terminal is the first thing
  * which will call schedule_timer(), which will in turn call
  * timer_change_cb() which will expect wnd to exist.)
  */
  term_init();
  term_resize(rows, cols);
  ldisc_init();
  
 /*
  * Initialise the fonts, simultaneously correcting the guesses
  * for font_{width,height}.
  */
  font_size = cfg.font.size;
  win_init_fonts();
  win_reset_colours();
  
 /*
  * Set up a caret bitmap, with no content.
  */
  {
    int size = (font_width + 15) / 16 * 2 * font_height;
    char *bits = newn(char, size);
    memset(bits, 0, size);
    caretbm = CreateBitmap(font_width, font_height, 1, 1, bits);
    free(bits);
    CreateCaret(wnd, caretbm, font_width, font_height);
  }

 /*
  * Initialise the scroll bar.
  */
  {
    SCROLLINFO si;
    si.cbSize = sizeof (si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = term.rows - 1;
    si.nPage = term.rows;
    si.nPos = 0;
    SetScrollInfo(wnd, SB_VERT, &si, false);
  }

 /*
  * Resize the window, now we know what size we _really_ want it to be.
  */
  int term_width = font_width * term.cols;
  int term_height = font_height * term.rows;
  SetWindowPos(wnd, null, 0, 0,
               term_width + extra_width + 2 * PADDING,
               term_height + extra_height + 2 * PADDING,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

  // Enable drag & drop.
  win_init_drop_target();

  // Create child process.
  const char *lang = cs_init();
  struct winsize ws = {term.rows, term.cols, term_width, term_height};
  char *cmd = child_create(argv + optind, lang, &ws);
  
  // Set window title.
  win_set_title(title ?: cmd);
  free(cmd);
  
  // Finally show the window!
  ShowWindow(wnd, SW_SHOWDEFAULT);
  
  // Message loop.
  // Also monitoring child events.
  for (;;) {
    DWORD wakeup =
      MsgWaitForMultipleObjects(1, &child_event, false, INFINITE, QS_ALLINPUT);
    if (wakeup == WAIT_OBJECT_0)
      child_dead |= child_proc();
    MSG msg;
    while (PeekMessage(&msg, null, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT)
        return msg.wParam;      
      if (!IsDialogMessage(config_wnd, &msg))
        DispatchMessage(&msg);
      term_send_paste();
    }
  }
}

void
win_show_about(void)
{
  char *text;
  asprintf(&text, "%s\n\n" ABOUT_TEXT, VERSION_TEXT);
  MessageBoxIndirect(&(MSGBOXPARAMS){
    .cbSize = sizeof(MSGBOXPARAMS),
    .hwndOwner = config_wnd,
    .hInstance = inst,
    .lpszCaption = APPNAME,
    .dwStyle = MB_USERICON | MB_OK,
    .lpszIcon = MAKEINTRESOURCE(IDI_MAINICON),
    .lpszText = text
  });
  free(text);
}
