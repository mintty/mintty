// wininput.c (part of MinTTY)
// Copyright 2008-09 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "linedisc.h"
#include "config.h"
#include "math.h"

static HMENU menu;

void
win_init_menu(void)
{
  menu = CreatePopupMenu();
  AppendMenu(menu, MF_ENABLED, IDM_COPY, "&Copy\tCtrl+Ins");
  AppendMenu(menu, MF_ENABLED, IDM_PASTE, "&Paste\tShift+Ins");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_SELALL, "&Select All");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_RESET, "&Reset");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED,
             IDM_FULLSCREEN, "&Full Screen\tAlt+Enter");
  AppendMenu(menu, MF_ENABLED, IDM_OPTIONS, "&Options...");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_ABOUT, "&About...");

  HMENU sysmenu = GetSystemMenu(wnd, false);
  InsertMenu(sysmenu, 0, MF_BYPOSITION|MF_SEPARATOR, 0, 0);
  InsertMenu(sysmenu, 0, MF_BYPOSITION|MF_ENABLED, IDM_OPTIONS, "&Options...");
}

void
win_popup_menu(void)
{
  POINT p;
  GetCursorPos(&p);
  TrackPopupMenu(
    menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
    p.x, p.y, 0, wnd, null
  );
}

void
win_update_menu(void)
{
  EnableMenuItem(menu, IDM_COPY, term_selected() ? MF_ENABLED : MF_GRAYED);
  EnableMenuItem(
    menu, IDM_PASTE,
    IsClipboardFormatAvailable(CF_TEXT) || 
    IsClipboardFormatAvailable(CF_UNICODETEXT) ||
    IsClipboardFormatAvailable(CF_HDROP)
    ? MF_ENABLED : MF_GRAYED
  );
  bool fullscreen = !(GetWindowLongPtr(wnd, GWL_STYLE) & WS_CAPTION);
  CheckMenuItem(menu, IDM_FULLSCREEN, fullscreen ? MF_CHECKED : MF_UNCHECKED);
}


inline static bool
is_key_down(uchar vk)
{ return GetKeyState(vk) & 0x80; }

static mod_keys
get_mods(void)
{
  bool shift = is_key_down(VK_SHIFT);
  bool alt = is_key_down(VK_MENU);
  bool ctrl = is_key_down(VK_RCONTROL) ||
              (is_key_down(VK_LCONTROL) && !is_key_down(VK_RMENU)); // not AltGr
  return shift * SHIFT | alt * ALT | ctrl * CTRL;
}

static void
update_mouse(mod_keys mods)
{
  static bool app_mouse;
  bool new_app_mouse = 
    term_in_mouse_mode() &&
    cfg.click_targets_app ^ ((mods & cfg.click_target_mod) != 0);
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
win_show_mouse()
{
  if (!mouse_showing) {
    ShowCursor(true);
    mouse_showing = true;
  }
}

void
hide_mouse()
{
  if (mouse_showing) {
    ShowCursor(false);
    mouse_showing = false;
  }
}


static pos
get_mouse_pos(LPARAM lp)
{
  int16 y = HIWORD(lp), x = LOWORD(lp);  
  return (pos){
    .y = floorf((y - offset_height) / (float)font_height), 
    .x = floorf((x - offset_width ) / (float)font_width ),
  };
}

static mouse_button clicked_button;

void
win_mouse_click(mouse_button b, LPARAM lp)
{
  win_show_mouse();
  mod_keys mods = get_mods();
  if (clicked_button) {
    term_mouse_release(b, mods, get_mouse_pos(lp));
    clicked_button = 0;
  }
  static mouse_button last_button;
  static uint last_time, count;
  uint t = GetMessageTime();
  if (b != last_button || t - last_time > GetDoubleClickTime() || ++count > 3)
    count = 1;
  term_mouse_click(b, mods, get_mouse_pos(lp), count);
  last_time = t;
  clicked_button = last_button = b;
}

void
win_mouse_release(mouse_button b, LPARAM lp)
{
  win_show_mouse();
  if (b == clicked_button) {
    term_mouse_release(b, get_mods(), get_mouse_pos(lp));
    clicked_button = 0;
    ReleaseCapture();
  }
}  

/*
 * Windows seems to like to occasionally send MOUSEMOVE events even if the 
 * mouse hasn't moved. Don't do anything in this case.
 */
void
win_mouse_move(bool nc, LPARAM lp)
{
  static bool last_nc;
  static LPARAM last_lp;
  if (nc == last_nc && lp == last_lp)
    return;
  last_nc = nc;
  last_lp = lp;
  win_show_mouse();
  if (!nc)
    term_mouse_move(clicked_button, get_mods(), get_mouse_pos(lp));
}

void
win_mouse_wheel(WPARAM wp, LPARAM lp)      
{
  int delta = -GET_WHEEL_DELTA_WPARAM(wp);
  int lines_per_notch;
  SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines_per_notch, 0);
  term_mouse_wheel(delta, lines_per_notch, get_mods(), get_mouse_pos(lp));
}


/* Keyboard handling */

enum { ALT_NONE = 0, ALT_ALONE = 1, ALT_OCT = 8, ALT_DEC = 10} alt_state;
wchar alt_char;

bool 
win_key_down(WPARAM wParam, LPARAM lParam)
{
  uint key = wParam;
  uint count = LOWORD(lParam);
  mod_keys mods = get_mods();
  bool shift = mods & SHIFT, alt = mods & ALT, ctrl = mods & CTRL;
 
  update_mouse(mods);

  // Alt+keycode
  if (alt_state != ALT_NONE) {
    uint digit = key - VK_NUMPAD0;
    if (alt_state == ALT_ALONE) {
      if (digit < 10) {
        alt_char = digit;
        alt_state = digit ? ALT_DEC : ALT_OCT;
        return 1;
      }
    }
    else if (digit < alt_state) {
      alt_char = alt_char * alt_state + digit;
      return 1;
    }
  }
  
  alt_state = key == VK_MENU && !shift && !ctrl;
  if (alt_state)
    return 1;
  
  // Specials
  if (alt && !ctrl) {
    if (key == VK_F4 && cfg.close_on_alt_f4) {
      SendMessage(wnd, WM_CLOSE, 0, 0);
      return 1;
    }
    if (key == VK_SPACE) {
      SendMessage(wnd, WM_SYSCOMMAND, SC_KEYMENU, ' ');
      return 1;
    }
  }
  
  // Context menu
  if (key == VK_APPS) {
    win_show_mouse();
    POINT p;
    GetCaretPos(&p);
    ClientToScreen(wnd, &p);
    TrackPopupMenu(
      menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
      p.x, p.y, 0, wnd, null
    );
    return 1;
  }
  
  // Clipboard
  if (key == VK_INSERT) {
    if (mods == CTRL) {
      term_copy();
      return 1;
    }
    else if (mods == SHIFT) {
      win_paste();
      return 1;
    }
  }

  // Scrollback
  if (term_which_screen() == 0 && mods == (mod_keys)cfg.scroll_mod) { 
    int scroll;
    switch (key) {
      when VK_HOME:  scroll = SB_TOP;
      when VK_END:   scroll = SB_BOTTOM;
      when VK_PRIOR: scroll = SB_PAGEUP;
      when VK_NEXT:  scroll = SB_PAGEDOWN;
      when VK_UP:    scroll = SB_LINEUP;
      when VK_DOWN:  scroll = SB_LINEDOWN;
      otherwise:
        goto not_scroll;
    }
    SendMessage(wnd, WM_VSCROLL, scroll, 0);
    return 1;
  }
  not_scroll: ;
  
  // Keycode buffer.
  char chars[8];
  int  chars_n = 0;
  void ch(char c) { chars[chars_n++] = c; }
  void esc(bool b) { if (b) ch('\e'); }  
  void ctrl_ch(char c) { ch(c & 0x1F); }
  void str(char *s) { while (*s) ch(*s++); }

  // Grey keys.
  if (alt) {
    switch (key) {
      when VK_ESCAPE or VK_PAUSE or VK_CANCEL or VK_TAB:
        return 0;
      when VK_RETURN:
        if (ctrl)
          return 0;
        // Alt-Enter: toggle fullscreen
        SendMessage(wnd, WM_SYSCOMMAND, IDM_FULLSCREEN, 0);
        return 1;
      when VK_BACK:
        if (ctrl)
          return 0;
        ch('\e');
        ch(shift ? ' ' : cfg.backspace_sends_del ? 0x7F : '\b');
        goto send;
    }
  }
  else { // !alt
    switch (key) {
      when VK_ESCAPE:
        ctrl_ch(shift ? ']' : cfg.escape_sends_fs ? '\\' : '[');
      when VK_PAUSE:
        esc(shift); ctrl_ch(']');
      when VK_CANCEL:
        esc(shift); ctrl_ch('\\');
      when VK_TAB:
        str(ctrl ? (shift ? "\e[z" : "\eOz") : (shift ? "\e[Z" : "\t"));
      when VK_RETURN:
        ctrl 
        ? (esc(shift), ctrl_ch('^'))
        : shift ? ch('\n') : term_newline_mode() ? str("\r\n") : ch('\r');
      when VK_BACK:
        ctrl 
        ? (esc(shift), cfg.backspace_sends_del ? ctrl_ch('_') : ch(0x7F)) 
        : ch(cfg.backspace_sends_del ? 0x7F : '\b');
      otherwise:
        goto not_grey;
    }
    goto send;
  }
  not_grey:
  
  // Arrow keys and clear key.
  { char code;
    switch (key) {
      when VK_UP:    code = 'A';
      when VK_DOWN:  code = 'B';
      when VK_RIGHT: code = 'C';
      when VK_LEFT:  code = 'D';
      when VK_CLEAR: code = 'E';
      when VK_BROWSER_FORWARD:
        code = 'F';
      when VK_BROWSER_BACK:
        code = 'G';
      otherwise:
        goto not_arrow;
    }
    ch('\e');
    if (!mods) 
      ch(term_app_cursor_keys() ? 'O' : '[');
    else { 
      str("[1;"); ch('1' + mods);
    }
    ch(code);
    goto send;
  }
  not_arrow:
  
  // Block of six.
  { char code;
    switch (key) {
      when VK_PRIOR:  code = '5';
      when VK_NEXT:   code = '6';
      when VK_HOME:   code = '1';
      when VK_END:    code = '4';
      when VK_INSERT: code = '2';
      when VK_DELETE: code = '3';
      otherwise:
        goto not_six;
    }
    str("\e["); ch(code);
    if (mods) { ch(';'); ch('1' + mods); }
    ch('~');
    goto send;
  }
  not_six:
  
  // PF keys.
  if (VK_F1 <= key && key <= VK_F4) {
    if (!mods)
      str("\eO");
    else {
      str("\e[1;");
      ch('1' + mods);
    }
    ch(key - VK_F1 + 'P');
    goto send;
  }
  
  // F keys.
  if (VK_F5 <= key && key <= VK_F24) {
    str("\e[");
    uchar code = 
      (uchar[]){
        15, 17, 18, 19, 20, 21, 23, 24,
        25, 26, 28, 29, 31, 32, 33, 34,
        36, 37, 38, 39
      }[key - VK_F5];
    ch('0' + code / 10); ch('0' + code % 10);
    if (mods) { ch(';'); ch('1' + mods); }
    ch('~');
    goto send;
  }
  
  // Special treatment for space.
  if (key == VK_SPACE && mods == CTRL) {
    // For some reason most keyboard layouts map Ctrl-Space to 0x20,
    // whereas we want 0.
    esc(shift); ch(0);
    goto send; 
  }
  
  // Try keyboard layout.
  // ToUnicode produces up to four UTF-16 code units per keypress according
  // to an experiment with Keyboard Layout Creator 1.4. (MSDN doesn't say.)
  uchar keyboard[256];  
  GetKeyboardState(keyboard);
  uint scancode = HIWORD(lParam) & (KF_EXTENDED | 0xFF);
  wchar wchars[4];
  int wchars_n = ToUnicode(key, scancode, keyboard, wchars, 4, 0);
  
  if (wchars_n != 0) {
    // Got normal key or dead key.
    term_cancel_paste();
    term_seen_key_event();
    if (wchars_n > 0) {
      bool meta = alt && !is_key_down(VK_CONTROL);
      do {
        if (meta) ldisc_send("\e", 1, 1);
        luni_send(wchars, wchars_n, 1);
      } while (--count);
    }
    hide_mouse();
    return 1;
  }
  
  // Try to handle Ctrl combinations if keyboard layout didn't.
  if (!ctrl)
    return 0;
  
  { 
    // Keys yielding app-pad sequences.
    // Helpfully, they're in the same order in VK, ASCII, and VT codes.
    char c;
    switch (key) {
      when '0' ... '9':                   c = key;
      when VK_NUMPAD0  ... VK_NUMPAD9:    c = key - VK_NUMPAD0  + '0';
      when VK_MULTIPLY ... VK_DIVIDE:     c = key - VK_MULTIPLY + '*';
      when VK_OEM_PLUS ... VK_OEM_PERIOD: c = key - VK_OEM_PLUS + '+';
      otherwise:
        goto not_app_pad;
    }
    ch('\e');
    ch(alt || shift ? '[' : 'O');
    ch(c + 0x40);
    goto send;
  }
  not_app_pad:
  
  {
    char c;
    if (key == ' ' || ('A' <= key && key <= 'Z'))
      c = key;
    else {
      switch (scancode) {
        when 0x2B
          or 0x56: c = '\\';
        when 0x1A: c = '[';
        when 0x1B: c = ']';
        when 0x28: c = '^';
        when 0x35: c = '_';
        otherwise: 
          goto not_ctrl_ch;
      }
    }
    esc(alt || shift);
    ctrl_ch(c);
    goto send;
  }
  not_ctrl_ch:

  // Key was not handled.
  return 0;

  // Send char buffer.
  send: {
    term_cancel_paste();
    term_seen_key_event();
    do
      ldisc_send(chars, chars_n, 1);
    while (--count);
    hide_mouse();
    return 1;
  }
}

bool 
win_key_up(WPARAM wParam, LPARAM unused(lParam))
{
  win_update_mouse();
  uint key = wParam;

  bool alt = key == VK_MENU;
  if (alt && alt_state != ALT_NONE) {
    if (alt_state == ALT_ALONE) {
      if (cfg.alt_sends_esc)
        ldisc_send((char[]){'\e'}, 1, 1);
    }
    else
      luni_send(&alt_char, 1, 1);
    alt_state = ALT_NONE;
  }
  
  return alt;
}
