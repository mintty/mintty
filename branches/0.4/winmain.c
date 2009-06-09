// win.c (part of MinTTY)
// Copyright 2008-09 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "term.h"
#include "config.h"
#include "appinfo.h"
#include "linedisc.h"
#include "child.h"
#include "unicode.h"

#include <process.h>
#include <getopt.h>
#include <imm.h>
#include <winnls.h>

HWND wnd;
HINSTANCE inst;
HDC dc;

int offset_width, offset_height;
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
  size_t len = strlen(title);
  wchar wtitle[len + 1];
  size_t wlen =
    mb_to_wc(ucsdata.codepage, 0, title, len, wtitle, len);
  wtitle[wlen] = 0;
  SetWindowTextW(wnd, wtitle);
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
 * Maximise or restore the window in response to a server-side
 * request.
 */
void
win_set_zoom(bool zoom)
{
  if (zoom ^ IsZoomed(wnd))
    ShowWindow(wnd, zoom ? SW_MAXIMIZE : SW_RESTORE);
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

static BOOL
flash_window_ex(DWORD dwFlags, UINT uCount, DWORD dwTimeout)
{
  FLASHWINFO fi;
  fi.cbSize = sizeof (fi);
  fi.hwnd = wnd;
  fi.dwFlags = dwFlags;
  fi.uCount = uCount;
  fi.dwTimeout = dwTimeout;
  return FlashWindowEx(&fi);
}

/*
 * Manage window caption / taskbar flashing, if enabled.
 * 0 = stop, 1 = maintain, 2 = start
 */
static void
flash_window(int mode)
{
  static bool flashing;

  if ((mode == 0) || (cfg.bell_ind == B_IND_DISABLED)) {
   /* stop */
    if (flashing) {
      flashing = 0;
      flash_window_ex(FLASHW_STOP, 0, 0);
    }
  }
  else if (mode == 2) {
   /* start */
    if (!flashing) {
      flashing = 1;
     /* For so-called "steady" mode, we use uCount=2, which
      * seems to be the traditional number of flashes used
      * by user notifications (e.g., by Explorer).
      * uCount=0 appears to enable continuous flashing, per
      * "flashing" mode, although I haven't seen this
      * documented. */
      flash_window_ex(FLASHW_ALL | FLASHW_TIMER,
                      (cfg.bell_ind == B_IND_FLASH ? 0 : 2),
                      0 /* system cursor blink rate */ );
    }
  }
}

/*
 * Bell.
 */
void
win_bell(int mode)
{
  if (mode == BELL_SOUND) {
   /*
    * For MessageBeep style bells, we want to be careful of timing,
    * because they don't have the nice property that each one cancels out 
    * the previous active one. So we limit the rate to one per 50ms or so.
    */
    static int lastbell = 0;
    int belldiff = GetTickCount() - lastbell;
    if (belldiff >= 0 && belldiff < 50)
      return;
    MessageBeep(MB_OK);
    lastbell = GetTickCount();
  }

  if (!term.has_focus)
    flash_window(2);
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
  int cx = x * font_width + offset_width;
  int cy = y * font_height + offset_height;
  if (cx != caret_x || cy != caret_y) {
    caret_x = cx;
    caret_y = cy;
    update_sys_cursor();
  }
}

/* Get the rect/size of a full screen window using the nearest available
 * monitor in multimon systems; default to something sensible if only
 * one monitor is present. */
static int
get_fullscreen_rect(RECT * ss)
{
  HMONITOR mon;
  MONITORINFO mi;
  mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
  mi.cbSize = sizeof (mi);
  GetMonitorInfo(mon, &mi);

 /* structure copy */
  *ss = mi.rcMonitor;
  return true;
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
 /* If the window is maximized supress resizing attempts */
  if (IsZoomed(wnd) || (rows == term.rows && cols == term.cols)) 
    return;
  
  notify_resize(rows, cols);
  int width = extra_width + font_width * cols + 2;
  int height = extra_height + font_height * rows + 2;
  SetWindowPos(wnd, null, 0, 0, width, height,
               SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
  InvalidateRect(wnd, null, true);
}

static void
reset_window(int reinit)
{
 /*
  * This function decides how to resize or redraw when the 
  * user changes something. 
  *
  * This function doesn't like to change the terminal size but if the
  * font size is locked that may be it's only soluion.
  */

 /* Are we being forced to reload the fonts ? */
  bool old_ambig_wide = font_ambig_wide;
  if (reinit > 1) {
    win_deinit_fonts();
    win_init_fonts();
  }

  int cols = term.cols, rows = term.rows;

 /* Current window sizes ... */
  RECT cr, wr;
  GetClientRect(wnd, &cr);
  GetWindowRect(wnd, &wr);
  int client_width = cr.right - cr.left;
  int client_height = cr.bottom - cr.top;
  extra_width = wr.right - wr.left - client_width;
  extra_height = wr.bottom - wr.top - client_height;
  int text_width = client_width - 2;
  int text_height = client_height - 2;
  
  if (!client_width || !client_height) {
    /* Oh, looks like we're minimised: do nothing */
  }
  else if (IsZoomed(wnd) || reinit == -1) {
   /* We're fullscreen, or we were told to resize,
    * this means we must not change the size of
    * the window so the terminal has to change.
    */
    cols = text_width / font_width;
    rows = text_height / font_height;
    offset_width = (text_width % font_width) / 2 + 1;
    offset_height = (text_height % font_height) / 2 + 1;
  }
  else if (text_width != cols * font_width ||
           text_height != rows * font_height) {
   /* Window size isn't what's needed. Let's change it then. */
    offset_width = offset_height = 1;
    static RECT ss;
    get_fullscreen_rect(&ss);
    int max_cols = (ss.right - ss.left - extra_width - 2) / font_width;
    int max_rows = (ss.bottom - ss.top - extra_height - 2) / font_height;
    cols = min(cols, max_cols);
    rows = min(rows, max_rows);
    SetWindowPos(wnd, null, 0, 0,
                 font_width * cols + 2 + extra_width,
                 font_height * rows + 2 + extra_height,
                 SWP_NOMOVE | SWP_NOZORDER);
  }
  
  if (rows != term.rows || cols != term.cols ||
      old_ambig_wide != font_ambig_wide)
    notify_resize(rows, cols);

  InvalidateRect(wnd, null, true);
}

/*
 * Go full-screen. This should only be called when we are already
 * maximised.
 */
static void
make_full_screen(void)
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

 /* We may have changed size as a result */
  reset_window(0);
}

/*
 * Clear the full-screen attributes.
 */
static void
clear_full_screen(void)
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
}

/*
 * Toggle full-screen mode.
 */
static void
flip_full_screen(void)
{
  if (IsZoomed(wnd)) {
    if (GetWindowLongPtr(wnd, GWL_STYLE) & WS_CAPTION)
      make_full_screen();
    else
      ShowWindow(wnd, SW_RESTORE);
  }
  else {
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
  uchar trans = cfg.transparency;
  SetWindowLong(wnd, GWL_EXSTYLE, trans ? WS_EX_LAYERED : 0);
  if (trans) {
    bool opaque = cfg.opaque_when_focused && term.has_focus;
    uchar alpha = opaque ? 255 : 255 - 16 * trans;
    SetLayeredWindowAttributes(wnd, 0, alpha, LWA_ALPHA);
  }
}

void
win_reconfig(void)
{
 /*
  * Flush the line discipline's edit buffer in the
  * case where local editing has just been disabled.
  */
  ldisc_send(null, 0, 0);

 /* Pass new config data to the terminal */
  term_reconfig();

 /* Enable or disable the scroll bar, etc */
  int init_lvl = 1;
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
    init_lvl = 2;
  }
  
  if (memcmp(&new_cfg.font, &cfg.font, sizeof cfg.font) != 0 ||
      strcmp(new_cfg.codepage, cfg.codepage) != 0 ||
      new_cfg.bold_as_bright != cfg.bold_as_bright) {
    font_size = new_cfg.font.size;
    init_lvl = 2;
  }
  
  /* Copy the new config and refresh everything */
  cfg = new_cfg;
  win_reconfig_palette();
  update_transparency();
  InvalidateRect(wnd, null, true);
  reset_window(init_lvl);
  win_update_mouse();
}

void
win_zoom_font(int zoom)
{
  if (!zoom)
    font_size = cfg.font.size;
  else if (abs(font_size) + zoom <= 1)
    font_size = sgn(font_size);
  else 
    font_size += sgn(font_size) * zoom;
  reset_window(2);
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
        when IDM_COPY: term_copy();
        when IDM_PASTE: win_paste();
        when IDM_SELALL:
          term_select_all();
          win_update();
        when IDM_RESET: reset_term();
        when IDM_DEFSIZE: default_size();
        when IDM_FULLSCREEN: flip_full_screen();
        when IDM_OPTIONS: win_open_config();
        when IDM_DUPLICATE:
          spawnv(_P_DETACH, "/proc/self/exe", (void *) main_argv); 
      }
    when WM_VSCROLL:
      if (term.which_screen == 0) {
        switch (LOWORD(wp)) {
          when SB_BOTTOM:   term_scroll(-1, 0);
          when SB_TOP:      term_scroll(+1, 0);
          when SB_LINEDOWN: term_scroll(0, +1);
          when SB_LINEUP:   term_scroll(0, -1);
          when SB_PAGEDOWN: term_scroll(0, +term.rows);
          when SB_PAGEUP:   term_scroll(0, -term.rows);
          when SB_THUMBPOSITION or SB_THUMBTRACK: term_scroll(1, HIWORD(wp));
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
      flash_window(0);  /* stop */
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
        offset_width = offset_height = 1;
        notify_resize(new_rows, new_cols);
        InvalidateRect(wnd, null, true);
      }
    when WM_SIZING: {
     /*
      * This does two jobs:
      * 1) Keep the sizetip uptodate
      * 2) Make sure the window size is _stepped_ in units of the font size.
      */
      LPRECT r = (LPRECT) lp;
      int width = r->right - r->left - extra_width - 2;
      int height = r->bottom - r->top - extra_height - 2;
      int cols = max(1, (width + font_width / 2) / font_width);
      int rows = max(1, (height + font_height / 2) / font_height);

      int ew = width - cols * font_width;
      int eh = height - rows * font_height;
      if (ew != 0) {
        if (wp == WMSZ_LEFT || wp == WMSZ_BOTTOMLEFT ||
            wp == WMSZ_TOPLEFT)
          r->left += ew;
        else
          r->right -= ew;
      }
      if (eh != 0) {
        if (wp == WMSZ_TOP || wp == WMSZ_TOPRIGHT ||
            wp == WMSZ_TOPLEFT)
          r->top += eh;
        else
          r->bottom -= eh;
      }

      win_update_tip(r->left + extra_width, r->top + extra_height, cols, rows);

      return ew || eh;
    }
    when WM_SIZE: {
      if (wp == SIZE_RESTORED)
        clear_full_screen();
      if (wp == SIZE_MAXIMIZED && fullscr_on_max) {
        fullscr_on_max = false;
        make_full_screen();
      }
      int new_width = LOWORD(lp) - 2;
      int new_height = HIWORD(lp) - 2;
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
        reset_window(0);
      }
      else if (wp == SIZE_RESTORED && was_zoomed) {
        was_zoomed = 0;
        notify_resize(prev_rows, prev_cols);
        reset_window(0);
      }
      else {
       /* This is an unexpected resize, these will normally happen
        * if the window is too large. Probably either the user
        * selected a huge font or the screen size has changed.
        *
        * This is also called with minimize.
        */
        reset_window(-1);
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
  return DefWindowProc(wnd, message, wp, lp);
}

static const char *help =
  "Usage: %s [OPTION]... [-e] [ - | COMMAND [ARG]... ]\n"
  "\n"
  "If a command is supplied, it is executed with its arguments. Otherwise,\n"
  "MinTTY looks for a shell to execute in the SHELL environment variable.\n"
  "If that is not set, it tries to read the user's default shell setting\n"
  "from /etc/passwd. Failing that, it falls back to /bin/sh. If the last\n"
  "argument is a single dash, the shell is invoked as a login shell.\n"
  "\n"
  "Options:\n"
  "  -c, --config=FILE     Use specified config file (default: ~/.minttyrc)\n"
  "  -p, --position=X,Y    Open window at specified coordinates\n"
  "  -s, --size=COLS,ROWS  Set screen size in characters\n"
  "  -t, --title=TITLE     Set window title (default: the invoked command)\n"
  "  -l, --log=FILE        Log output to file\n"
  "  -u, --utmp            Create a utmp entry\n"
  "  -h, --hold=never|always|error\n"
  "                        Keep window open after command terminates?\n"
  "  -H, --help            Display help and exit\n"
  "  -V, --version         Print version information and exit\n"
;

static const char short_opts[] = "+HVuec:p:s:t:l:h:";

static const struct option
opts[] = { 
  {"config",   required_argument, 0, 'c'},
  {"position", required_argument, 0, 'p'},
  {"size",     required_argument, 0, 's'},
  {"title",    required_argument, 0, 't'},
  {"log",      required_argument, 0, 'l'},
  {"hold",     required_argument, 0, 'h'},
  {"utmp",     no_argument,       0, 'u'},
  {"help",     no_argument,       0, 'H'},
  {"version",  no_argument,       0, 'V'},
  {0, 0, 0, 0}
};

int
main(int argc, char *argv[])
{
  char *title = 0;
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
  bool size_override = false;
  uint rows = 0, cols = 0;

  for (;;) {
    int opt = getopt_long(argc, argv, short_opts, opts, 0);
    if (opt == -1 || opt == 'e')
      break;
    switch (opt) {
      when 'c':
        config_file = optarg;
      when 'p':
        if (sscanf(optarg, "%i,%i%1s", &x, &y, (char[2]){}) != 2) {
          fprintf(stderr, "%s: syntax error in position argument -- %s\n",
                          *argv, optarg);
          exit(1);
        }
      when 's':
        if (sscanf(optarg, "%u,%u%1s", &cols, &rows, (char[2]){}) != 2) {
          fprintf(stderr, "%s: syntax error in size argument -- %s\n",
                          *argv, optarg);
          exit(1);
        }
        size_override = true;
      when 't':
        title = optarg;
      when 'l':
        log_file = optarg;
      when 'u':
        utmp_enabled = true;
      when 'h': {
        int len = strlen(optarg);
        if (memcmp(optarg, "always", len) == 0)
          hold = HOLD_ALWAYS;
        else if (memcmp(optarg, "never", len) == 0)
          hold = HOLD_NEVER;
        else if (memcmp(optarg, "error", len) == 0)
          hold = HOLD_ERROR;
        else {
          fprintf(stderr, "%s: invalid argument to hold option -- %s\n",
                          *argv, optarg);
          exit(1);
        }
      }
      when 'H':
        printf(help, *argv);
        return 0;
      when 'V':
        puts(APPNAME " " VERSION "\n" COPYRIGHT "\n" LICENSE);
        return 0;
      otherwise:
        exit(1);
    }
  }

  if (!config_file)
    asprintf((char **)&config_file, "%s/.minttyrc", getenv("HOME"));

  load_config();
  
  if (!size_override) {
    rows = cfg.rows;
    cols = cfg.cols;
  }
    
  main_argv = argv;  
  inst = GetModuleHandle(NULL);

 /* Create window class. */
  {
    WNDCLASS wndclass;
    wndclass.style = 0;
    wndclass.lpfnWndProc = win_proc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = inst;
    wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
    wndclass.hCursor = LoadCursor(null, IDC_IBEAM);
    wndclass.hbrBackground = null;
    wndclass.lpszMenuName = null;
    wndclass.lpszClassName = APPNAME;
    RegisterClass(&wndclass);
  }

 /* Create initial window.
  * Its real size has to be set after loading the fonts and determining their
  * size, but the window has to exist to do that.
  */
  wnd = CreateWindow(APPNAME, APPNAME,
                     WS_OVERLAPPEDWINDOW | (cfg.scrollbar ? WS_VSCROLL : 0),
                     x, y, 300, 200, null, null, inst, null);

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
  offset_width = offset_height = 1;
  SetWindowPos(wnd, null, 0, 0,
               term_width + extra_width + 2, term_height + extra_height + 2,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

  // Enable drag & drop.
  win_init_drop_target();

  // Finally show the window!
  update_transparency();
  ShowWindow(wnd, SW_SHOWDEFAULT);
  
  // Create child process.
  struct winsize ws = {term.rows, term.cols, term_width, term_height};
  char *cmd = child_create(argv + optind, &ws);
  
  // Set window title.
  win_set_title(title ?: cmd);
  free(cmd);
  
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
