// wininput.c (part of MinTTY)
// Copyright 2008 Andy Koppe
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
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED,
             IDM_FULLSCREEN, "&Full Screen\tAlt+Enter");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_OPTIONS, "&Options...");
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
win_hide_mouse()
{
  if (mouse_showing) {
    ShowCursor(false);
    mouse_showing = false;
  }
}


static pos
get_pos(LPARAM lp, bool *has_moved_p)
{
  int16 y = HIWORD(lp), x = LOWORD(lp);  
  pos p = {
    .y = floorf((y - offset_height) / (float)font_height), 
    .x = floorf((x - offset_width ) / (float)font_width ),
  };
  static pos last_p;
  if (has_moved_p)
    *has_moved_p = p.x != last_p.x || p.y != last_p.y;
  last_p = p;
  return p;
}

static mod_keys
get_mouse_mods(WPARAM wp)
{
  return
    (wp & MK_SHIFT ? SHIFT : 0) |
    (GetKeyState(VK_MENU) & 0x80 ? ALT : 0) |
    (wp & MK_CONTROL ? CTRL : 0);
}

static mouse_button clicked_button;

void
win_mouse_click(mouse_button b, WPARAM wp, LPARAM lp)
{
  win_show_mouse();
  mod_keys mods = get_mouse_mods(wp);
  if (clicked_button) {
    term_mouse_release(b, mods, get_pos(lp, 0));
    clicked_button = 0;
  }
  static mouse_button last_button;
  static uint last_time, count;
  uint t = GetMessageTime();
  if (b != last_button || t - last_time > GetDoubleClickTime() || ++count > 3)
    count = 1;
  term_mouse_click(b, mods, get_pos(lp, 0), count);
  last_time = t;
  clicked_button = last_button = b;
  SetCapture(wnd);
}

void
win_mouse_release(mouse_button b, WPARAM wp, LPARAM lp)
{
  win_show_mouse();
  if (b == clicked_button) {
    term_mouse_release(b, get_mouse_mods(wp), get_pos(lp, 0));
    clicked_button = 0;
    ReleaseCapture();
  }
}  

/*
 * Windows seems to like to occasionally send MOUSEMOVE events even if the 
 * mouse hasn't moved. Don't do anything in this case.
 */
void
win_mouse_move(bool nc, WPARAM wp, LPARAM lp)
{
  static bool last_nc;
  static LPARAM last_lp;
  if (nc == last_nc && lp == last_lp)
    return;
  last_nc = nc;
  last_lp = lp;
  win_show_mouse();
  if (!nc) {
    bool has_moved;
    pos p = get_pos(lp, &has_moved);
    if (has_moved)  
      term_mouse_move(clicked_button, get_mouse_mods(wp), p);
  }
}

void
win_mouse_wheel(WPARAM wp, LPARAM lp)      
{
  uint lines_per_notch;
  SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines_per_notch, 0);
  static int delta = 0;
  delta -= GET_WHEEL_DELTA_WPARAM(wp) * lines_per_notch;
  int lines = delta / WHEEL_DELTA;
  delta -= lines * WHEEL_DELTA;
  if (lines)
    term_mouse_wheel(lines, get_mouse_mods(wp), get_pos(lp, 0));
}

inline static bool
is_key_down(uchar vk)
{
  return GetKeyState(vk) & 0x80;
}

bool 
win_key_press(WPARAM wParam, LPARAM lParam) {
  uint key = wParam;
  uint flags = HIWORD(lParam);
  uint count = LOWORD(lParam);
  uint scancode = flags & (KF_UP | KF_EXTENDED | 0xFF);

  // Check modifiers.
  bool shift = is_key_down(VK_SHIFT);
  bool alt = flags & KF_ALTDOWN;
  bool ctrl = is_key_down(VK_RCONTROL) ||
              (is_key_down(VK_LCONTROL) && !is_key_down(VK_RMENU)); // not AltGr
  int mods = shift * SHIFT | alt * ALT | ctrl * CTRL;

  // Specials
  if (alt && !ctrl) {
    if (key == VK_F4) {
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
  if (term_which_screen() == 0 && mods == cfg.scroll_mod) { 
    int scroll;
    switch (key) {
      when VK_HOME:  scroll = SB_TOP;
      when VK_END:   scroll = SB_BOTTOM;
      when VK_PRIOR: scroll = SB_PAGEUP;
      when VK_NEXT:  scroll = SB_PAGEDOWN;
      when VK_UP:    scroll = SB_LINEUP;
      when VK_DOWN:  scroll = SB_LINEDOWN;
      otherwise: goto not_scroll;
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
        ch('\e'); ch(shift ? ' ' : 0x7F);
        goto send;
    }
  }
  else { // !alt
    switch (key) {
      when VK_ESCAPE:
        ctrl_ch(cfg.escape_sends_fs ? (shift ? ']' : '\\') : '[');
      when VK_PAUSE:
        esc(shift); ctrl_ch(']');
      when VK_CANCEL:
        esc(shift); ctrl_ch('\\');
      when VK_TAB:
        str(ctrl ? (shift ? "\eOZ" : "\eOz") : (shift ? "\e[Z" : "\t"));
      when VK_RETURN:
        ctrl ? (esc(shift), ctrl_ch('^')) : ch(shift ? '\n' : '\r');
      when VK_BACK:
        ctrl 
        ? (esc(shift), ctrl_ch('_')) 
        : ch(cfg.backspace_sends_del ? 0x7F : '\b');
      otherwise: goto not_grey;
    }
    goto send;
  }
  not_grey:
  
  // Arrow keys and clear key.
  { char code;
    switch (key) {
      when VK_UP:    code = 'A';
      when VK_DOWN:  code = 'B';
      when VK_LEFT:  code = 'D';
      when VK_RIGHT: code = 'C';
      when VK_CLEAR: code = 'E';
      otherwise: goto not_arrow;
    }
    ch('\e');
    ch(term_app_cursor_keys() ? 'O' : '[');
    if (mods) { str("1;"); ch('1' + mods); }
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
      otherwise: goto not_six;
    }
    str("\e["); ch(code);
    if (mods) { ch(';'); ch('1' + mods); }
    ch('~');
    goto send;
  }
  not_six:
  
  // Function keys.
  if (VK_F1 <= key && key <= VK_F24) {
    str("\e[");
    uchar code = 
      (uchar[]){
        11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24,
        25, 26, 28, 29, 31, 32, 33, 34, 36, 37, 38, 39
      }[key - VK_F1];
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
    win_hide_mouse();
    return 1;
  }
  
  // Handle Ctrl combinations if keyboard layout didn't.

  // First, keys yielding control characters.
  if (ctrl) {
    { char c;
      switch (key) {
        when VK_SPACE or 'A' ... 'Z': c = key;
        when VK_OEM_1 or VK_OEM_4:    c = '[';
        when VK_OEM_5 or VK_OEM_102:  c = '\\';
        when VK_OEM_6:                c = ']';
        when VK_OEM_2 or VK_OEM_7:    c = '_';
        when VK_OEM_3 or VK_OEM_8:    c = '^';
        otherwise: goto not_ctrl_ch;
      }
      esc(alt || shift);
      ctrl_ch(c);
      goto send;
    }
  }
  not_ctrl_ch:
    
  // Finally, keys yielding app-pad sequences.
  // Helpfully, they're lined up in the same order in VK, ASCII, and VT codes.
  if (ctrl) {
    char c;
    switch (key) {
      when '0' ... '9':                   c = key;
      when VK_NUMPAD0  ... VK_NUMPAD9:    c = key - VK_NUMPAD0  + '0';
      when VK_MULTIPLY ... VK_DIVIDE:     c = key - VK_MULTIPLY + '*';
      when VK_OEM_PLUS ... VK_OEM_PERIOD: c = key - VK_OEM_PLUS + '+';
      otherwise: goto not_app_pad;
    }
    str("\eO");
    ch(c - '0' + (alt || shift ? 'P' : 'p'));
    goto send;
  }
  not_app_pad:
  
  // Key was not handled.
  return 0;

  // Send char buffer.
  send: {
    term_cancel_paste();
    term_seen_key_event();
    do
      ldisc_send(chars, chars_n, 1);
    while (--count);
    win_hide_mouse();
    return 1;
  }
}
