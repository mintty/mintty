// win.c (part of MinTTY)
// Copyright 2008 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "term.h"
#include "timing.h"
#include "config.h"
#include "appinfo.h"
#include "linedisc.h"
#include "child.h"

#include <imm.h>
#include <winnls.h>

HWND hwnd;
HINSTANCE hinst;
HDC hdc;
HMENU hmenu;
static HBITMAP caretbm;

static char *window_name;

int offset_width, offset_height;
static int extra_width, extra_height;
static int caret_x = -1, caret_y = -1;

static bool resizing;
static bool was_zoomed;
static int prev_rows, prev_cols;

static bool flashing;

enum { TIMER_ID = 1234 };
static int next_time;

void
win_set_title(char *title)
{
  free(window_name);
  window_name = newn(char, 1 + strlen(title));
  strcpy(window_name, title);
  SetWindowText(hwnd, title);
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
void
win_set_iconic(bool iconic)
{
  if (iconic ^ IsIconic(hwnd))
    ShowWindow(hwnd, iconic ? SW_MINIMIZE : SW_RESTORE);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void
win_set_zoom(bool zoom)
{
  if (zoom ^ IsZoomed(hwnd))
    ShowWindow(hwnd, zoom ? SW_MAXIMIZE : SW_RESTORE);
}

/*
 * Move the window in response to a server-side request.
 */
void
win_move(int x, int y)
{
  if (!IsZoomed(hwnd))
    SetWindowPos(hwnd, null, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void
win_set_zorder(bool top)
{
  SetWindowPos(hwnd, top ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE);
}

/*
 * Refresh the window in response to a server-side request.
 */
void
win_refresh(void)
{ InvalidateRect(hwnd, null, true); }

/*
 * Report whether the window is iconic, for terminal reports.
 */
bool
win_is_iconic(void)
{ return IsIconic(hwnd); }

/*
 * Report the window's position, for terminal reports.
 */
void
win_get_pos(int *x, int *y)
{
  RECT r;
  GetWindowRect(hwnd, &r);
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
  GetWindowRect(hwnd, &r);
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
  fi.hwnd = hwnd;
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

  if (!term_has_focus())
    flash_window(2);
}

/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */

static void
sys_cursor_update(void)
{
  COMPOSITIONFORM cf;
  HIMC hIMC;

  if (!term_has_focus())
    return;

  if (caret_x < 0 || caret_y < 0)
    return;

  SetCaretPos(caret_x, caret_y);

  hIMC = ImmGetContext(hwnd);
  cf.dwStyle = CFS_POINT;
  cf.ptCurrentPos.x = caret_x;
  cf.ptCurrentPos.y = caret_y;
  ImmSetCompositionWindow(hIMC, &cf);

  ImmReleaseContext(hwnd, hIMC);
}

void
win_sys_cursor(int x, int y)
{
  if (!term_has_focus())
    return;

 /*
  * Avoid gratuitously re-updating the cursor position and IMM
  * window if there's no actual change required.
  */
  int cx = x * font_width + offset_width;
  int cy = y * font_height + offset_height;
  if (cx == caret_x && cy == caret_y)
    return;
  caret_x = cx;
  caret_y = cy;

  sys_cursor_update();
}

/* Get the rect/size of a full screen window using the nearest available
 * monitor in multimon systems; default to something sensible if only
 * one monitor is present. */
static int
get_fullscreen_rect(RECT * ss)
{
  HMONITOR mon;
  MONITORINFO mi;
  mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
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
  struct winsize ws = {rows, cols, cols * font_width, rows * font_height};
  child_resize(&ws);
}

void
win_resize(int rows, int cols)
{
 /* If the window is maximized supress resizing attempts */
  if (IsZoomed(hwnd) || (rows == term_rows() && cols == term_cols())) 
    return;
    
 /* Sanity checks ... */
  static int first_time = 1;
  static RECT ss;
  switch (first_time) {
    when 1:
     /* Get the size of the screen */
      if (!get_fullscreen_rect(&ss))
        first_time = 2;
    when 0: {
     /* Make sure the values are sane */
      int max_cols = (ss.right - ss.left - extra_width) / 4;
      int max_rows = (ss.bottom - ss.top - extra_height) / 6;
      if (cols > max_cols || rows > max_rows)
        return;
      if (cols < 15)
        cols = 15;
      if (rows < 1)
        rows = 1;
    }
  }

  notify_resize(rows, cols);
  int width = extra_width + font_width * cols;
  int height = extra_height + font_height * rows;
  SetWindowPos(hwnd, null, 0, 0, width, height,
               SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
  InvalidateRect(hwnd, null, true);
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

 /* Current window sizes ... */
  RECT cr, wr;
  GetWindowRect(hwnd, &wr);
  GetClientRect(hwnd, &cr);

  int win_width = cr.right - cr.left;
  int win_height = cr.bottom - cr.top;

 /* Are we being forced to reload the fonts ? */
  if (reinit > 1) {
    win_deinit_fonts();
    win_init_fonts();
  }

 /* Oh, looks like we're minimised */
  if (win_width == 0 || win_height == 0)
    return;

  int cols = term_cols(), rows = term_rows();

 /* Is the window out of position ? */
  if (!reinit &&
      (offset_width != (win_width - font_width * cols) / 2 ||
       offset_height != (win_height - font_height * rows) / 2)) {
    offset_width = (win_width - font_width * cols) / 2;
    offset_height = (win_height - font_height * rows) / 2;
    InvalidateRect(hwnd, null, true);
  }

  if (IsZoomed(hwnd)) {
   /* We're fullscreen, this means we must not change the size of
    * the window so it's the font size or the terminal itself.
    */

    extra_width = wr.right - wr.left - cr.right + cr.left;
    extra_height = wr.bottom - wr.top - cr.bottom + cr.top;

    if (font_width * cols != win_width ||
        font_height * rows != win_height) {
     /* Our only choice at this point is to change the 
      * size of the terminal; Oh well.
      */
      rows = win_height / font_height;
      cols = win_width / font_width;
      offset_height = (win_height % font_height) / 2;
      offset_width = (win_width % font_width) / 2;
      notify_resize(rows, cols);
      InvalidateRect(hwnd, null, true);
    }
    return;
  }

 /* Hmm, a force re-init means we should ignore the current window
  * so we resize to the default font size.
  */
  if (reinit > 0) {
    offset_width = offset_height = 0;
    extra_width = wr.right - wr.left - cr.right + cr.left;
    extra_height = wr.bottom - wr.top - cr.bottom + cr.top;

    if (win_width != font_width * cols ||
        win_height != font_height * rows) {

     /* If this is too large windows will resize it to the maximum
      * allowed window size, we will then be back in here and resize
      * the font or terminal to fit.
      */
      SetWindowPos(hwnd, null, 0, 0, font_width * cols + extra_width,
                   font_height * rows + extra_height,
                   SWP_NOMOVE | SWP_NOZORDER);
    }

    InvalidateRect(hwnd, null, true);
    return;
  }

 /* Okay the user doesn't want us to change the font so we try the 
  * window. But that may be too big for the screen which forces us
  * to change the terminal.
  */
  offset_width = offset_height = 0;
  extra_width = wr.right - wr.left - cr.right + cr.left;
  extra_height = wr.bottom - wr.top - cr.bottom + cr.top;

  if (win_width != font_width * cols ||
      win_height != font_height * rows) {

    static RECT ss;
    get_fullscreen_rect(&ss);

    int win_rows = (ss.bottom - ss.top - extra_height) / font_height;
    int win_cols = (ss.right - ss.left - extra_width) / font_width;

   /* Grrr too big */
    if (rows > win_rows || cols > win_cols) {
      rows = min(rows, win_rows);
      cols = min(cols, win_cols);
      notify_resize(rows, cols);
    }

    SetWindowPos(hwnd, null, 0, 0,
                 font_width * cols + extra_width,
                 font_height * rows + extra_height,
                 SWP_NOMOVE | SWP_NOZORDER);

    InvalidateRect(hwnd, null, true);
  }
}

/*
 * See if we're in full-screen mode.
 */
static bool
is_full_screen()
{
  if (!IsZoomed(hwnd))
    return false;
  if (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CAPTION)
    return false;
  return true;
}

/*
 * Go full-screen. This should only be called when we are already
 * maximised.
 */
static void
make_full_screen()
{
  DWORD style;
  RECT ss;

  assert(IsZoomed(hwnd));

  if (is_full_screen())
    return;

 /* Remove the window furniture. */
  style = GetWindowLongPtr(hwnd, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
  SetWindowLongPtr(hwnd, GWL_STYLE, style);

 /* Resize ourselves to exactly cover the nearest monitor. */
  get_fullscreen_rect(&ss);
  SetWindowPos(hwnd, HWND_TOP, ss.left, ss.top, ss.right - ss.left,
               ss.bottom - ss.top, SWP_FRAMECHANGED);

 /* We may have changed size as a result */
  reset_window(0);

 /* Tick the menu item in the System menu. */
  CheckMenuItem(hmenu, IDM_FULLSCREEN, MF_CHECKED);
}

/*
 * Clear the full-screen attributes.
 */
static void
clear_full_screen()
{
  DWORD oldstyle, style;

 /* Reinstate the window furniture. */
  style = oldstyle = GetWindowLongPtr(hwnd, GWL_STYLE);
  style |= WS_CAPTION | WS_BORDER | WS_THICKFRAME;
  if (style != oldstyle) {
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, null, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }

 /* Untick the menu item in the System menu. */
  CheckMenuItem(hmenu, IDM_FULLSCREEN, MF_UNCHECKED);
}

/*
 * Toggle full-screen mode.
 */
static void
flip_full_screen()
{
  if (is_full_screen()) {
    ShowWindow(hwnd, SW_RESTORE);
  }
  else if (IsZoomed(hwnd)) {
    make_full_screen();
  }
  else {
    SendMessage(hwnd, WM_FULLSCR_ON_MAX, 0, 0);
    ShowWindow(hwnd, SW_MAXIMIZE);
  }
}

static ubyte
alpha()
{
  switch (cfg.transparency) {
    when 1: return 239;
    when 2: return 223;
    when 3: return 207;
    otherwise: return 255;
  }
}

static void
paint(void)
{
  PAINTSTRUCT p;

  HideCaret(hwnd);
  HDC hdc0 = hdc; // save device context
  hdc = BeginPaint(hwnd, &p);

 /*
  * We have to be careful about term_paint(). It will
  * set a bunch of character cells to INVALID and then
  * call do_paint(), which will redraw those cells and
  * _then mark them as done_. This may not be accurate:
  * when painting in WM_PAINT context we are restricted
  * to the rectangle which has just been exposed - so if
  * that only covers _part_ of a character cell and the
  * rest of it was already visible, that remainder will
  * not be redrawn at all. Accordingly, we must not
  * paint any character cell in a WM_PAINT context which
  * already has a pending update due to terminal output.
  * The simplest solution to this - and many, many
  * thanks to Hung-Te Lin for working all this out - is
  * not to do any actual painting at _all_ if there's a
  * pending terminal update: just mark the relevant
  * character cells as INVALID and wait for the
  * scheduled full update to sort it out.
  * 
  * I have a suspicion this isn't the _right_ solution.
  * An alternative approach would be to have terminal.c
  * separately track what _should_ be on the terminal
  * screen and what _is_ on the terminal screen, and
  * have two completely different types of redraw (one
  * for full updates, which syncs the former with the
  * terminal itself, and one for WM_PAINT which syncs
  * the latter with the former); yet another possibility
  * would be to have the Windows front end do what the
  * GTK one already does, and maintain a bitmap of the
  * current terminal appearance so that WM_PAINT becomes
  * completely trivial. However, this should do for now.
  */
  term_paint((p.rcPaint.left - offset_width) / font_width,
             (p.rcPaint.top - offset_height) / font_height,
             (p.rcPaint.right - offset_width - 1) / font_width,
             (p.rcPaint.bottom - offset_height - 1) / font_height,
             !term_update_pending());

  if (p.fErase || p.rcPaint.left < offset_width ||
      p.rcPaint.top < offset_height ||
      p.rcPaint.right >= offset_width + font_width * term_cols() ||
      p.rcPaint.bottom >= offset_height + font_height * term_rows()) {
    HBRUSH fillcolour, oldbrush;
    HPEN edge, oldpen;
    fillcolour = CreateSolidBrush(colours[ATTR_DEFBG >> ATTR_BGSHIFT]);
    oldbrush = SelectObject(hdc, fillcolour);
    edge = CreatePen(PS_SOLID, 0, colours[ATTR_DEFBG >> ATTR_BGSHIFT]);
    oldpen = SelectObject(hdc, edge);

   /*
    * Jordan Russell reports that this apparently
    * ineffectual IntersectClipRect() call masks a
    * Windows NT/2K bug causing strange display
    * problems when the PuTTY window is taller than
    * the primary monitor. It seems harmless enough...
    */
    IntersectClipRect(hdc, p.rcPaint.left, p.rcPaint.top, p.rcPaint.right,
                      p.rcPaint.bottom);

    ExcludeClipRect(hdc, offset_width, offset_height,
                    offset_width + font_width * term_cols(),
                    offset_height + font_height * term_rows());

    Rectangle(hdc, p.rcPaint.left, p.rcPaint.top, p.rcPaint.right,
              p.rcPaint.bottom);

   /* SelectClipRgn(hdc, null); */

    SelectObject(hdc, oldbrush);
    DeleteObject(fillcolour);
    SelectObject(hdc, oldpen);
    DeleteObject(edge);
  }
  SelectObject(hdc, GetStockObject(SYSTEM_FONT));
  SelectObject(hdc, GetStockObject(WHITE_PEN));
  EndPaint(hwnd, &p);
  hdc = hdc0;     // restore original device context
  ShowCaret(hwnd);
}

static void
reconfig(void)
{
  static bool reconfiguring = false;
  
  if (reconfiguring)
    return;
  
  reconfiguring = true;
  config prev_cfg = cfg;
  bool reconfig_result = win_config();
  reconfiguring = false;
  if (!reconfig_result)
    return;

 /*
  * Flush the line discipline's edit buffer in the
  * case where local editing has just been disabled.
  */
  ldisc_send(null, 0, 0);
  win_reconfig_palette();

 /* Pass new config data to the terminal */
  term_reconfig();

 /* Screen size changed ? */
  if (cfg.rows != prev_cfg.rows || cfg.cols != prev_cfg.cols)
    notify_resize(cfg.rows, cfg.cols);

 /* Enable or disable the scroll bar, etc */
  int init_lvl = 1;
  if (cfg.scrollbar != prev_cfg.scrollbar) {
    LONG flag = GetWindowLongPtr(hwnd, GWL_STYLE);
    if (cfg.scrollbar)
      flag |= WS_VSCROLL;
    else
      flag &= ~WS_VSCROLL;
    SetWindowLongPtr(hwnd, GWL_STYLE, flag);
    SetWindowPos(hwnd, null, 0, 0, 0, 0,
                SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE |
                 SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    init_lvl = 2;
  }

  if (IsIconic(hwnd))
    SetWindowText(hwnd, window_name);

  if (strcmp(cfg.font.name, prev_cfg.font.name) != 0 ||
      strcmp(cfg.line_codepage, prev_cfg.line_codepage) != 0 ||
      cfg.font.isbold != prev_cfg.font.isbold ||
      cfg.font.height != prev_cfg.font.height ||
      cfg.font.charset != prev_cfg.font.charset ||
      cfg.font_quality != prev_cfg.font_quality ||
      cfg.bold_as_bright != prev_cfg.bold_as_bright)
    init_lvl = 2;
  
  SetLayeredWindowAttributes(hwnd, 0, alpha(), LWA_ALPHA);
  InvalidateRect(hwnd, null, true);
  reset_window(init_lvl);
}

static bool
confirm_close(void)
{
  return
    !child_is_parent() ||
    MessageBox(
      hwnd,
      "Processes are running in session.\n"
      "Exit anyway?",
      APPNAME " Exit Confirmation", 
      MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2
    ) == IDOK;
}

static LRESULT CALLBACK
win_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
  static int ignore_clip = false;
  static int need_backend_resize = false;
  static int fullscr_on_max = false;

  switch (message) {
    when WM_TIMER:
      if ((UINT_PTR) wp == TIMER_ID) {
        KillTimer(hwnd, TIMER_ID);
        int next;
        if (run_timers(next_time, &next))
          timer_change_cb(next);
      }
      return 0;
    when WM_CLOSE:
      win_show_mouse();
      if (child_exitcode)
        exit(child_exitcode);
      if (confirm_close())
        child_kill();
      return 0;
    when WM_COMMAND or WM_SYSCOMMAND:
      switch (wp & ~0xF) {  /* low 4 bits reserved to Windows */
        when SC_KEYMENU:
          if (lp == 0)
            ldisc_send((char[]){'\e'}, 1, 1);
          return 0;
        when IDM_COPY: term_copy();
        when IDM_PASTE: term_paste();
        when IDM_SELALL:
          term_select_all();
          term_update();
        when IDM_RESET:
          term_reset();
          term_deselect();
          term_update();
          ldisc_send(null, 0, 0);
        when IDM_ABOUT: win_about();
        when IDM_FULLSCREEN: flip_full_screen();
        when IDM_OPTIONS: reconfig();
      }
    when WM_VSCROLL:
      if (term_which_screen() == 0) {
        switch (LOWORD(wp)) {
          when SB_BOTTOM:   term_scroll(-1, 0);
          when SB_TOP:      term_scroll(+1, 0);
          when SB_LINEDOWN: term_scroll(0, +1);
          when SB_LINEUP:   term_scroll(0, -1);
          when SB_PAGEDOWN: term_scroll(0, +term_rows());
          when SB_PAGEUP:   term_scroll(0, -term_rows());
          when SB_THUMBPOSITION or SB_THUMBTRACK:
            term_scroll(1, HIWORD(wp));
        }
      }
    when WM_LBUTTONDOWN: win_mouse_click(MB_LEFT, wp, lp);
    when WM_RBUTTONDOWN: win_mouse_click(MB_RIGHT, wp, lp);
    when WM_MBUTTONDOWN: win_mouse_click(MB_MIDDLE, wp, lp);
    when WM_LBUTTONUP: win_mouse_release(MB_LEFT, wp, lp);
    when WM_RBUTTONUP: win_mouse_release(MB_RIGHT, wp, lp);
    when WM_MBUTTONUP: win_mouse_release(MB_MIDDLE, wp, lp);
    when WM_MOUSEWHEEL: win_mouse_wheel(wp, lp);
    when WM_MOUSEMOVE: win_mouse_move(false, wp, lp);
    when WM_NCMOUSEMOVE: win_mouse_move(true, wp, lp);
    when WM_KEYDOWN or WM_SYSKEYDOWN:
      if (win_key_press(wp, lp))
        return 0;
    when WM_CHAR or WM_SYSCHAR: { // TODO: handle wchar and WM_UNICHAR
      char c = (ubyte) wp;
      term_seen_key_event();
      lpage_send(CP_ACP, &c, 1, 1);
      return 0;
    }
    when WM_INPUTLANGCHANGE:
      sys_cursor_update();
    when WM_IME_STARTCOMPOSITION: {
      HIMC hImc = ImmGetContext(hwnd);
      ImmSetCompositionFont(hImc, &lfont);
      ImmReleaseContext(hwnd, hImc);
    }
    when WM_IGNORE_CLIP:
      ignore_clip = wp;     /* don't panic on DESTROYCLIPBOARD */
    when WM_DESTROYCLIPBOARD:
      if (!ignore_clip) {
        term_deselect();
        term_update();
      }
      ignore_clip = false;
      return 0;
    when WM_PAINT:
      paint();
      return 0;
    when WM_SETFOCUS:
      term_set_focus(true);
      CreateCaret(hwnd, caretbm, font_width, font_height);
      ShowCaret(hwnd);
      flash_window(0);  /* stop */
      term_update();
    when WM_KILLFOCUS:
      win_show_mouse();
      term_set_focus(false);
      DestroyCaret();
      caret_x = caret_y = -1;   /* ensure caret is replaced next time */
      term_update();
    when WM_FULLSCR_ON_MAX: fullscr_on_max = true;
    when WM_MOVE: sys_cursor_update();
    when WM_ENTERSIZEMOVE:
      win_enable_tip();
      resizing = true;
      need_backend_resize = false;
    when WM_EXITSIZEMOVE:
      win_disable_tip();
      resizing = false;
      if (need_backend_resize) {
        notify_resize(cfg.rows, cfg.cols);
        InvalidateRect(hwnd, null, true);
      }
    when WM_SIZING: {
     /*
      * This does two jobs:
      * 1) Keep the sizetip uptodate
      * 2) Make sure the window size is _stepped_ in units of the font size.
      */
      LPRECT r = (LPRECT) lp;
      int width = r->right - r->left - extra_width;
      int height = r->bottom - r->top - extra_height;
      int w = (width + font_width / 2) / font_width;
      if (w < 1)
        w = 1;
      int h = (height + font_height / 2) / font_height;
      if (h < 1)
        h = 1;
      win_update_tip(hwnd, w, h);
      int ew = width - w * font_width;
      int eh = height - h * font_height;
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
      return ew || eh;
    }
    when WM_SIZE: {
      if (wp == SIZE_MINIMIZED || wp == SIZE_RESTORED || wp == SIZE_MAXIMIZED)
        SetWindowText(hwnd, window_name);
      if (wp == SIZE_RESTORED)
        clear_full_screen();
      if (wp == SIZE_MAXIMIZED && fullscr_on_max) {
        fullscr_on_max = false;
        make_full_screen();
      }
      int width = LOWORD(lp);
      int height = HIWORD(lp);
      if (resizing) {
       /*
        * Don't call child_size in mid-resize. (To prevent
        * massive numbers of resize events getting sent
        * down the connection during an NT opaque drag.)
        */
        need_backend_resize = true;
        cfg.rows = max(1, height / font_height);
        cfg.cols = max(1, width / font_width);
      }
      else if (wp == SIZE_MAXIMIZED && !was_zoomed) {
        was_zoomed = 1;
        prev_rows = term_rows();
        prev_cols = term_cols();
        int rows = max(1, height / font_height);
        int cols = max(1, width / font_width);
        notify_resize(rows, cols);
        reset_window(0);
      }
      else if (wp == SIZE_RESTORED && was_zoomed) {
        was_zoomed = 0;
        notify_resize(prev_rows, prev_cols);
        reset_window(2);
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
      sys_cursor_update();
      return 0;
    }
  }
 /*
  * Any messages we don't process completely above are passed through to
  * DefWindowProc() for default processing.
  */
  return DefWindowProc(hwnd, message, wp, lp);
}

int
main(int argc, char *argv[])
{
  if (argc == 2) {
    if (strcmp(argv[1], "--version") == 0) {
      puts(APPNAME " " VERSION "\n" COPYRIGHT);
      return 0;
    }
    if (strcmp(argv[1], "--help") == 0) {
      printf("Usage: %s [COMMAND ARGS...]\n", *argv);
      return 0;
    }
  }

  load_config();
  hinst = GetModuleHandle(NULL);

 /* Create window class. */
  {
    WNDCLASS wndclass;
    wndclass.style = 0;
    wndclass.lpfnWndProc = win_proc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hinst;
    wndclass.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_MAINICON));
    wndclass.hCursor = LoadCursor(null, IDC_IBEAM);
    wndclass.hbrBackground = null;
    wndclass.lpszMenuName = null;
    wndclass.lpszClassName = APPNAME;
    RegisterClass(&wndclass);
  }

 /*
  * Guess some defaults for the window size. This all gets
  * updated later, so we don't really care too much. However, we
  * do want the font width/height guesses to correspond to a
  * large font rather than a small one...
  */
  {
    int guess_width = 25 + 20 * cfg.cols;
    int guess_height = 28 + 20 * cfg.rows;
    RECT r;
    get_fullscreen_rect(&r);
    if (guess_width > r.right - r.left)
      guess_width = r.right - r.left;
    if (guess_height > r.bottom - r.top)
      guess_height = r.bottom - r.top;
    int winmode = WS_OVERLAPPEDWINDOW;
    if (cfg.scrollbar)
      winmode |= WS_VSCROLL;
    int exwinmode = WS_EX_LAYERED;
    hwnd = CreateWindowEx(exwinmode, APPNAME, APPNAME, winmode, CW_USEDEFAULT,
                         CW_USEDEFAULT, guess_width, guess_height, null, null,
                         hinst, null);
    SetLayeredWindowAttributes(hwnd, 0, alpha(), LWA_ALPHA);
    hdc = GetDC(hwnd);
  }

 /*
  * Set up the context menu.
  */
  {
    HMENU m = hmenu = CreatePopupMenu();
    AppendMenu(m, MF_ENABLED, IDM_COPY, "&Copy");
    AppendMenu(m, MF_ENABLED, IDM_PASTE, "&Paste");
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_ENABLED, IDM_SELALL, "&Select All");
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_ENABLED, IDM_RESET, "&Reset Terminal");
    AppendMenu(m, MF_ENABLED | MF_UNCHECKED, IDM_FULLSCREEN, "&Full Screen");
    AppendMenu(m, MF_SEPARATOR, 0, 0);
    AppendMenu(m, MF_ENABLED, IDM_OPTIONS, "&Options...");
    AppendMenu(m, MF_ENABLED, IDM_ABOUT, "&About ...");
  }
  
 /*
  * Initialise the terminal. (We have to do this _after_
  * creating the window, since the terminal is the first thing
  * which will call schedule_timer(), which will in turn call
  * timer_change_cb() which will expect hwnd to exist.)
  */
  term_init();
  term_resize(cfg.rows, cfg.cols);
  ldisc_init();
  
 /*
  * Initialise the fonts, simultaneously correcting the guesses
  * for font_{width,height}.
  */
  win_init_fonts();
  win_init_palette();

 /*
  * Correct the guesses for extra_{width,height}.
  */
  {
    RECT cr, wr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    offset_width = offset_height = 0;
    extra_width = wr.right - wr.left - cr.right + cr.left;
    extra_height = wr.bottom - wr.top - cr.bottom + cr.top;
  }
  
 /*
  * Set up a caret bitmap, with no content.
  */
  {
    char *bits;
    int size = (font_width + 15) / 16 * 2 * font_height;
    bits = newn(char, size);
    memset(bits, 0, size);
    caretbm = CreateBitmap(font_width, font_height, 1, 1, bits);
    free(bits);
    CreateCaret(hwnd, caretbm, font_width, font_height);
  }

 /*
  * Initialise the scroll bar.
  */
  {
    SCROLLINFO si;
    si.cbSize = sizeof (si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = term_rows() - 1;
    si.nPage = term_rows();
    si.nPos = 0;
    SetScrollInfo(hwnd, SB_VERT, &si, false);
  }

 /*
  * Resize the window, now we know what size we _really_ want it to be.
  */
  int term_width = font_width * term_cols();
  int term_height = extra_height + font_height * term_rows();
  SetWindowPos(hwnd, null, 0, 0,
               term_width + extra_width, term_height + extra_height,
               SWP_NOMOVE | SWP_NOREDRAW | SWP_NOZORDER);

  // Create child process and set window title to the executed command.
  struct winsize ws = {term_rows(), term_cols(), term_width, term_height};
  char *cmd = child_create(argv + 1, &ws);
  win_set_title(cmd);
  free(cmd);

  // Finally show the window!
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  SetForegroundWindow(hwnd);
  term_set_focus(GetForegroundWindow() == hwnd);
  UpdateWindow(hwnd);

  // Message loop.
  // Also monitoring child events.
  for (;;) {
    DWORD wakeup =
      MsgWaitForMultipleObjects(1, &child_event, false, INFINITE, QS_ALLINPUT);
    if (wakeup == WAIT_OBJECT_0)
      child_proc();
    MSG msg;
    while (PeekMessage(&msg, null, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT)
        return msg.wParam;      
      DispatchMessage(&msg);
      term_send_paste();
    }
    // Set focus, just in case a message got lost.
    term_set_focus(GetForegroundWindow() == hwnd);
  }
}

void
timer_change_cb(int next)
{
  int ticks = next - get_tick_count();
  if (ticks <= 0)
    ticks = 1;  /* just in case */
  KillTimer(hwnd, TIMER_ID);
  SetTimer(hwnd, TIMER_ID, ticks, null);
  next_time = next;
}
