// winsearch.c (part of mintty)
// Copyright 2015 Kai (kiwiz)
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winsearch.h"


static bool search_initialised = false;
static int prev_height = 0;

static HWND search_wnd;
static HWND search_close_wnd;
static HWND search_prev_wnd;
static HWND search_next_wnd;
static HWND search_edit_wnd;
static WNDPROC default_edit_proc;
static HFONT search_font = 0;

static void win_hide_search(void);

#define SEARCHBARCLASS "SearchBar"

int SEARCHBAR_HEIGHT = 26;


static int
current_delta(bool adjust)
{
  if (term.results.current.len == 0) {
    return 0;
  }

  int y = term.results.current.idx / term.cols - term.sblines;
  int delta = 0;
  if (y < term.disptop) {
    delta = y - term.disptop;
  }
  else if (y >= term.disptop + term.rows) {
    delta = y - (term.disptop + term.rows - 1);
  }

  if (adjust) {
    //printf("search scroll to %d (top %d) by %d\n", y, term.disptop, delta);
    int dist = abs(cfg.search_context);
    if (dist > term.rows / 2)
      dist = term.rows / 2;
    if (delta > 0)
      delta += dist;
    else if (delta < 0)
      delta -= dist;
    else if (cfg.search_context < 0) {
      if (y - term.disptop < dist)
        delta = y - term.disptop - dist;
      else if (term.disptop + term.rows - 1 - y < dist)
        delta = dist - (term.disptop + term.rows - 1 - y);
      //printf("                 -> %d\n", delta);
    }
  }

  return delta;
}

static void
scroll_to_result(result res)
{
  if (res.len == 0) {
    return;
  }

  if (current_delta(false) == 0) {
    // Update term.results.current iff the current result is in screen.
    term.results.current = res;
  }

  // Scroll if we must!
  int delta = current_delta(true);
  if (delta != 0) {
    term_scroll(0, delta);
  }
}

static void
next_result(void)
{
  scroll_to_result(term_search_next());
}

static void
prev_result(void)
{
  scroll_to_result(term_search_prev());
}

static LRESULT CALLBACK
edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  MSG mesg = {.hwnd = hwnd, .message = msg, .wParam = wp, .lParam = lp};
  TranslateMessage(&mesg);

  switch (mesg.message) {
    when WM_KEYDOWN or WM_SYSKEYDOWN:
      switch (mesg.wParam) {
        when VK_ESCAPE:
          term_clear_search();
          win_hide_search();
          win_schedule_update();
          return 0;
        when VK_TAB:
          // FIXME: Still causes beeping...
          SetFocus(wnd);
          return 0;
        when VK_RETURN:
          if (GetKeyState(VK_SHIFT) < 0) {
            prev_result();
          }
          else {
            next_result();
          }
          win_schedule_update();
          return 0;
      }
    when WM_CHAR:
      switch (mesg.wParam) {
        // Necessary to stop the beeping!
        when VK_RETURN or VK_ESCAPE or VK_MENU:
          return 0;
      }
    when WM_MBUTTONUP: {
      mesg.message = WM_PASTE;
      mesg.wParam = 0;
      mesg.lParam = 0;
    }
  }

  return CallWindowProc(default_edit_proc, mesg.hwnd, mesg.message, mesg.wParam, mesg.lParam);
}

#define dont_darken_searchbar

static LRESULT CALLBACK
search_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  bool update = false;
  switch (msg) {
    when WM_COMMAND:
      switch (HIWORD(wp)) {
        when BN_CLICKED: // Equivalent to STN_CLICKED
          if (lp == (long)search_prev_wnd) {
            prev_result();
          }
          if (lp == (long)search_next_wnd) {
            next_result();
          }
          if (lp == (long)search_close_wnd) {
            term_clear_search();
            win_hide_search();
            win_schedule_update();
          }
          win_schedule_update();
          return 0;
        when EN_UPDATE:
          update = true;
      }
    when WM_SHOWWINDOW:
      if (wp) {
        update = true;
      }
#ifdef darken_searchbar
    when WM_CTLCOLOREDIT: {
      bool support_dark_mode = true;
      /// ... determine support_dark_mode as in win_dark_mode
      if (support_dark_mode) {
        HDC hdc = (HDC)wp;
        colour fg = RGB(222, 22, 22); // test value
        colour bg = RGB(22, 22, 22);  // test value
        /// ... retrieve fg, bg from DarkMode_Explorer theme
        SetTextColor(hdc, fg);
        SetBkColor(hdc, bg);
        return (INT_PTR)CreateSolidBrush(bg);
      }
    }
#endif
  }

  if (update) {
    int len = GetWindowTextLengthW(search_edit_wnd) + 1;
    wchar * buf = malloc(sizeof(wchar) * len);
    GetWindowTextW(search_edit_wnd, buf, len);
    term_set_search(buf);
    term_update_search();
    win_schedule_update();
    return 0;
  }

  return CallWindowProc(DefWindowProc, hwnd, msg, wp, lp);
}

static void
place_field(int * curpoi, int width, int * pospoi)
{
  if (* pospoi < 0) {
    * pospoi = * curpoi;
    (* curpoi) += width;
  }
}

static void
win_toggle_search(bool show, bool focus)
{
  RECT cr;
  GetClientRect(wnd, &cr);
  int width = cr.right - cr.left;

  int margin = cell_width / 6 + 1;
  int height = cell_height + margin * 2;
  int button_width = cell_width * 2;
  SEARCHBAR_HEIGHT = height;

  int edit_width = width - button_width * 3 - margin * 2;
  int ctrl_height = height - margin * 2;
  int sf_height = ctrl_height - 4;
#ifdef debug_searchbar
  printf("ctrl/but %d/%d cell %d/%d font h %d s %d\n", ctrl_height, button_width, cell_height, cell_width, font_height, font_size);
#endif

  const wchar * search_bar = cfg.search_bar;
  int pos_close = -1;
  int pos_prev = -1;
  int pos_next = -1;
  int pos_edit = -1;
  int barpos = margin;
  wchar * prev_but = _W("◀");
  wchar * next_but = _W("▶");
  wchar * close_but = _W("X");
  while (search_bar && * search_bar) {
    switch (* search_bar) {
      when 'x' or 'X':
        place_field(& barpos, button_width, & pos_close);
      when '<':
        place_field(& barpos, button_width, & pos_prev);
      when '>':
        place_field(& barpos, button_width, & pos_next);
      when 's' or 'S':
        place_field(& barpos, edit_width, & pos_edit);
      when 0x25B2 ... 0x25B5 or 0x25C0 ... 0x25C5:
        place_field(& barpos, button_width, & pos_prev);
        * prev_but = * search_bar;
      when 0x25B6 ... 0x25BF:
        place_field(& barpos, button_width, & pos_next);
        * next_but = * search_bar;
      when 0x2190 ... 0x2193 or 0x21D0 ... 0x21D3 or 0x21E0 ... 0x21E3
        or 0x21E6 ... 0x21E9 or 0x261C ... 0x261F:
        if ((* search_bar & 0xF) % 6 > 1) {
          place_field(& barpos, button_width, & pos_next);
          * next_but = * search_bar;
        }
        else {
          place_field(& barpos, button_width, & pos_prev);
          * prev_but = * search_bar;
        }
      when 0x21FD or 0x21FE or 0x27F5 or 0x27F6 or 0x261A or 0x261B:
        if ((* search_bar & 0xF) % 5 % 3) {
          place_field(& barpos, button_width, & pos_next);
          * next_but = * search_bar;
        }
        else {
          place_field(& barpos, button_width, & pos_prev);
          * prev_but = * search_bar;
        }
      when 0x3008 ... 0x300B:
        if (* search_bar & 1) {
          place_field(& barpos, button_width, & pos_next);
          * next_but = * search_bar;
        }
        else {
          place_field(& barpos, button_width, & pos_prev);
          * prev_but = * search_bar;
        }
      when 0x2717 or 0x2718 /*or 0x274C or 0x2573*/:
        place_field(& barpos, button_width, & pos_close);
        * close_but = * search_bar;
    }
    search_bar ++;
  }
  place_field(& barpos, button_width, & pos_close);
  place_field(& barpos, button_width, & pos_prev);
  place_field(& barpos, button_width, & pos_next);
  place_field(& barpos, edit_width, & pos_edit);

  // Set up our global variables.
  if (!search_initialised || height != prev_height) {
    if (!search_initialised)
      RegisterClassA(&(WNDCLASSA){
        .style = 0,
        .lpfnWndProc = search_proc,
        .cbClsExtra = 0,
        .cbWndExtra = 0,
        .hInstance = inst,
        .hIcon = NULL,
        .hCursor = NULL,
        .hbrBackground = (HBRUSH)(COLOR_3DFACE + 1),
        .lpszMenuName = NULL,
        .lpszClassName = SEARCHBARCLASS
      });
    else {
      DestroyWindow(search_wnd);
    }

    search_wnd = CreateWindowExA(0, SEARCHBARCLASS, "", WS_CHILD, 0, 0, 0, 0, wnd, 0, inst, NULL);

    //__ label of search bar close button; not actually "localization"
    search_close_wnd = CreateWindowExW(0, W("BUTTON"), close_but, WS_CHILD | WS_VISIBLE,
                                     pos_close, margin, button_width, ctrl_height,
                                     search_wnd, NULL, inst, NULL);
    //__ label of search bar prev button; not actually "localization"
    search_prev_wnd = CreateWindowExW(0, W("BUTTON"), prev_but, WS_CHILD | WS_VISIBLE,
                                     pos_prev, margin, button_width, ctrl_height,
                                     search_wnd, NULL, inst, NULL);
    //__ label of search bar next button; not actually "localization"
    search_next_wnd = CreateWindowExW(0, W("BUTTON"), next_but, WS_CHILD | WS_VISIBLE,
                                     pos_next, margin, button_width, ctrl_height,
                                     search_wnd, NULL, inst, NULL);
    search_edit_wnd = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 0, 0,
                                     search_wnd, NULL, inst, NULL);

#ifdef darken_searchbar
    win_dark_mode(search_prev_wnd);
    win_dark_mode(search_next_wnd);
    win_dark_mode(search_close_wnd);
    // these two do not darken anything
    //win_dark_mode(search_wnd);
    //win_dark_mode(search_edit_wnd);
#endif

    if (search_font)
      DeleteObject(search_font);
    search_font = CreateFontW(sf_height, 0, 0, 0, FW_DONTCARE, false, false, false,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, FIXED_PITCH | FF_DONTCARE,
                             cfg.font.name);
    SendMessage(search_edit_wnd, WM_SETFONT, (WPARAM)search_font, 1);

    default_edit_proc = (WNDPROC)SetWindowLongPtrW(search_edit_wnd, GWLP_WNDPROC, (long)edit_proc);

    if (term.results.query)
      SetWindowTextW(search_edit_wnd, term.results.query);

    search_initialised = true;
    prev_height = height;
  }

  if (show) {
    SetWindowPos(search_wnd, 0,
                 cr.right - width, cr.bottom - height,
                 width, height,
                 SWP_NOZORDER);
    SetWindowPos(search_edit_wnd, 0,
                 pos_edit, margin,
                 edit_width, ctrl_height,
                 SWP_NOZORDER);
    if (focus) {
      SendMessage(search_edit_wnd, EM_SETSEL, 0, -1);
      SetFocus(search_edit_wnd);
    }
  }
  else {
    SetFocus(wnd);
  }

  ShowWindow(search_wnd, show ? SW_SHOW : SW_HIDE);
}

void
win_open_search(void)
{
  win_toggle_search(true, true);
  win_adapt_term_size(false, false);
}

static void
win_hide_search(void)
{
  win_toggle_search(false, false);
  win_adapt_term_size(false, false);
}

void
win_update_search(void)
{
  if (win_search_visible()) {
    win_toggle_search(true, false);
  }
}

void
win_paint_exclude_search(HDC dc)
{
  if (!win_search_visible()) {
    return;
  }
  RECT cr;
  POINT p = {.x = 0, .y = 0};
  GetWindowRect(search_wnd, &cr);
  ClientToScreen(wnd, &p);

  cr.left -= p.x;
  cr.right -= p.x;
  cr.top -= p.y;
  cr.bottom -= p.y;
  ExcludeClipRect(dc, cr.left, cr.top, cr.right, cr.bottom);
}

bool
win_search_visible(void)
{
  return IsWindowVisible(search_wnd);
}
